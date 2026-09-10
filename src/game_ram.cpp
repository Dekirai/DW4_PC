#include "dw4/game_ram.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/database.hpp"

#include <array>
#include <bit>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw GameDataError(message); }
std::int32_t integer(const NativeMessenger& messenger,std::size_t index) {
    const auto& values=messenger.buffer().integers();
    if(index>=values.size()) fail("Original GAMERAM command is missing a required signed integer argument.");
    return values[index];
}
std::size_t player_index(std::int32_t index) {
    if(index<0 || index>=static_cast<std::int32_t>(GameDataState::player_count))
        fail("Original GAMERAM player-list access requires an existing row from zero through three.");
    return static_cast<std::size_t>(index);
}
bool magic_effect(std::int32_t remainder) {
    switch(remainder) {
    case 40:case 45:case 55:case 60:case 50:case -50:case 65:case 80:case 75:return true;
    default:return false;
    }
}
std::int32_t skill_ceiling(std::int32_t level) {
    const auto word=std::bit_cast<std::uint32_t>(level);
    // Every guest multiply/add wraps independently; signed host overflow is
    // never used to approximate the PowerPC result.
    const auto value=level<=36 ? word*30u : level<=107 ? word*20u+360u :
        level<=227 ? word*10u+1430u : word*8u+1884u;
    return std::bit_cast<std::int32_t>(value);
}
std::int32_t skill_value(const GameDataList& row,std::int32_t index) {
    const auto values=row.require<GameDataIntList>("SKILL")->data;
    if(!values || index<0 || static_cast<std::size_t>(index)>=values->size())
        fail("Original P2ESKILLUP requires an existing selected SKILL array element.");
    return values->at(static_cast<std::size_t>(index));
}
}

struct GameRamRuntime::Impl {
    struct Pending {
        enum class Phase { required, pc_tech, selected_skill } phase=Phase::required;
        std::optional<std::string> command,subcommand;
        std::array<std::int32_t,4> arguments{};
        std::shared_ptr<GameDataList> player;
        std::int32_t ceiling{},technique_row{},skill_index{};
        std::optional<MessengerDispatch> query;
        std::optional<GameStartRuntime> start;
        std::string diagnostic;
    };
    GameDataState data;
    ControllerObjectId manager=ControllerObjectId::allocate_native();
    ControllerObjectId info=ControllerObjectId::allocate_native();
    ControllerObjectId ram=ControllerObjectId::allocate_native();
    std::map<std::pair<std::uint64_t,std::size_t>,Pending> pending;
    GameDatabaseCache databases;
    GameNameServices names;
    bool fault{};

    void prepare_skill(Pending& state,const NativeMessenger& messenger) {
        state.arguments={integer(messenger,0),integer(messenger,1),integer(messenger,2),integer(messenger,3)};
        state.player=data.player(player_index(state.arguments[0]));
        state.ceiling=skill_ceiling(state.player->require<GameDataInt>("LEVEL")->data);
        const auto category=state.arguments[1]/10000;
        if(category!=4 && category!=8 && category!=9) {
            state.skill_index=state.player->require<GameDataInt>("WEAPONSKILLNO")->data;
            state.phase=Pending::Phase::selected_skill;
            return;
        }
        const auto items=state.player->require<GameDataObjectList>("WAZALIST")->data;
        if(!items || items->size()>65536) fail("Original P2ESKILLUP requires a bounded, owned WAZALIST.");
        for(const auto& object:*items) {
            const auto* item=std::get_if<std::shared_ptr<GameDataWaza>>(&object);
            if(!item || !*item) fail("Original P2ESKILLUP WAZALIST contains an absent or different item type.");
            if((*item)->base_no==state.arguments[1]%10000) {
                state.technique_row=(*item)->record_no;
                if(databases.pc_tech) {
                    state.skill_index=databases.pc_tech->integer(state.technique_row,"SKILL");
                    state.phase=Pending::Phase::selected_skill;
                } else state.phase=Pending::Phase::pc_tech;
                return;
            }
        }
        // 80371524 leaves its output untouched if no item matches. A fresh
        // empty factory row does not authorize an invented technique zero.
        state.diagnostic="Original P2ESKILLUP requires the selected player's matching WAZALIST technique; no row was produced.";
    }

    MessengerCallResult advance(Pending& state,NativeMessenger& messenger,ActionEventRuntime& runtime) {
        if(state.start)return state.start->resume(data,databases,messenger,runtime,names);
        if(state.phase==Pending::Phase::pc_tech) {
            if(!state.query) {
                messenger.select("DATABASE","pcTech",0);
                state.query.emplace(messenger.begin_send("GETDATABASE"));
            }
            const auto result=runtime.resume(*state.query);
            if(result.stop==MessengerStop::faulted) fail("Original P2ESKILLUP pcTech database delivery faulted.");
            if(result.stop!=MessengerStop::completed)
                return {false,0,"Original P2ESKILLUP pcTech database query requires its retained service: "+result.diagnostic};
            const auto& objects=messenger.buffer().objects();
            const auto* database=objects.empty() ? nullptr : std::get_if<MessengerDatabase>(&objects[0]);
            if(!database || !*database) fail("Original P2ESKILLUP pcTech query requires a non-null database at current object zero.");
            databases.pc_tech=*database;
            state.query.reset();
            state.skill_index=databases.pc_tech->integer(state.technique_row,"SKILL");
            state.phase=Pending::Phase::selected_skill;
        }
        if(state.phase==Pending::Phase::selected_skill) {
            // This read follows the database callback and uses the retained
            // source row, even if a nested GAMEDATAINIT replaced the live row.
            const auto value=skill_value(*state.player,state.skill_index);
            if(value>=state.ceiling) return {true,1,{}};
            state.phase=Pending::Phase::required;
            state.diagnostic="Original P2ESKILLUP probability, random, stat and effect services are required after selecting skill "+
                std::to_string(state.skill_index)+" (value "+std::to_string(value)+", ceiling "+std::to_string(state.ceiling)+").";
        }
        return {false,0,state.diagnostic};
    }

    MessengerCallResult call(NativeMessenger& messenger,const MessengerDelivery& delivery,ActionEventRuntime& runtime) {
        if(fault) fail("GAMERAM has an uncertain outcome; replay is forbidden.");
        if(delivery.recipient!=ram || !delivery.dispatch_id)
            fail("GAMERAM delivery does not belong to its generated RAM and retained send.");
        const auto key=std::pair{delivery.dispatch_id,delivery.row_index};
        try {
            if(const auto found=pending.find(key);found!=pending.end()) {
                const auto result=advance(found->second,messenger,runtime);
                if(result.completed)pending.erase(found);
                return result;
            }
            const auto command=delivery.message.command;
            if(command=="GETPLAYER" || command=="GETPLAYER1" || command=="GETPLAYER2" ||
                command=="GETPLAYER3" || command=="GETPLAYER4") {
                const auto index=command=="GETPLAYER" ? player_index(integer(messenger,0)) :
                    static_cast<std::size_t>(command->back()-'1');
                messenger.buffer().add_data_list(data.player(index));
                return {true,1,{}};
            }
            if(command=="GAMEDATAINIT") {
                data.reset_player(player_index(integer(messenger,0)));
                return {true,1,{}};
            }
            Pending required;
            required.command=command;
            if(command=="GAMESTART") {
                // Both reads precede cardinality and player-row bounds checks.
                const auto source=integer(messenger,0);
                const auto& strings=messenger.buffer().strings();
                if(strings.empty())fail("Original GAMESTART requires its selected-model string slot.");
                const auto model=strings[0];
                const auto& objects=messenger.buffer().objects();
                if(objects.size()==1)required.start.emplace(source,model,GameStartName{objects[0]});
                else if(strings.size()==2)required.start.emplace(source,model,GameStartName{strings[1]});
                else return {true,1,{}};
            } else if(command=="FIGHTCALC") {
                const auto& strings=messenger.buffer().strings();
                if(strings.empty() || !strings.front()) fail("Original GAMERAM FIGHTCALC requires its subcommand string.");
                required.subcommand=strings.front();
                if(required.subcommand=="P2PKOUKAMAGIC") {
                    required.arguments={integer(messenger,0),integer(messenger,1),integer(messenger,2)};
                    // 80376F14..28 resolves the target row BEFORE comparing RAMs.
                    required.player=data.player(player_index(required.arguments[0]));
                    if(required.arguments[0]!=required.arguments[1]) return {true,1,{}};
                    const auto remainder=required.arguments[2]%10000;
                    if(!magic_effect(remainder)) {
                        messenger.buffer().clear_integers();
                        messenger.buffer().add_int(1);
                        return {true,1,{}};
                    }
                    required.diagnostic="Original GAMERAM P2PKOUKAMAGIC effect service is required for remainder "+std::to_string(remainder)+".";
                } else if(required.subcommand=="P2ESKILLUP") prepare_skill(required,messenger);
                else required.diagnostic="Original GAMERAM FIGHTCALC service is required: "+*required.subcommand;
            } else required.diagnostic="Original GAMERAM command service is required: "+command.value_or("<null>");
            if(pending.size()>=256) fail("GAMERAM pending native services exceed 256 retained deliveries.");
            const auto entry=pending.emplace(key,std::move(required)).first;
            const auto result=advance(entry->second,messenger,runtime);
            if(result.completed)pending.erase(entry);
            return result;
        } catch(...) {fault=true;throw;}
    }
};

GameRamRuntime::GameRamRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()) {
    const auto owner=impl_;
    runtime.register_native_controller(owner->ram,{
        [owner]()->std::optional<std::uint8_t> {return std::uint8_t{1};},
        [owner,&runtime] {
            return MessengerManager{
                [owner,&runtime](NativeMessenger& messenger,const MessengerDelivery& delivery) {return owner->call(messenger,delivery,runtime);},
                [](NativeMessenger&,const MessengerDelivery&,std::string_view) {return MessengerCallResult{true,0,{}};}};
        }},"GAMERAM","gameram",0);
}
GameRamRuntime::~GameRamRuntime()=default;
ControllerObjectId GameRamRuntime::manager_id() const noexcept {return impl_->manager;}
ControllerObjectId GameRamRuntime::info_id() const noexcept {return impl_->info;}
ControllerObjectId GameRamRuntime::ram_id() const noexcept {return impl_->ram;}
const GameDataState& GameRamRuntime::state() const noexcept {return impl_->data;}
std::size_t GameRamRuntime::pending_count() const noexcept {return impl_->pending.size();}
bool GameRamRuntime::faulted() const noexcept {return impl_->fault;}
void GameRamRuntime::set_name_services(GameNameServices services) {impl_->names=std::move(services);}
} // namespace dw4
