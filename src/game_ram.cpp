#include "dw4/game_ram.hpp"
#include "dw4/action_event_runtime.hpp"

#include <array>
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
}

struct GameRamRuntime::Impl {
    struct Pending {
        std::optional<std::string> command,subcommand;
        std::array<std::int32_t,3> arguments{};
        std::shared_ptr<GameDataList> player;
        std::string diagnostic;
    };
    GameDataState data;
    ControllerObjectId manager=ControllerObjectId::allocate_native();
    ControllerObjectId info=ControllerObjectId::allocate_native();
    ControllerObjectId ram=ControllerObjectId::allocate_native();
    std::map<std::pair<std::uint64_t,std::size_t>,Pending> pending;
    bool fault{};

    MessengerCallResult call(NativeMessenger& messenger,const MessengerDelivery& delivery) {
        if(fault) fail("GAMERAM has an uncertain outcome; replay is forbidden.");
        if(delivery.recipient!=ram || !delivery.dispatch_id)
            fail("GAMERAM delivery does not belong to its generated RAM and retained send.");
        const auto key=std::pair{delivery.dispatch_id,delivery.row_index};
        if(const auto found=pending.find(key);found!=pending.end()) {
            // Original command/arguments/target-row reads already happened.
            // A nested call may subsequently change the live message/buffer.
            return {false,0,found->second.diagnostic};
        }
        try {
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
            if(command=="FIGHTCALC") {
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
                } else required.diagnostic="Original GAMERAM FIGHTCALC service is required: "+*required.subcommand;
            } else required.diagnostic="Original GAMERAM command service is required: "+command.value_or("<null>");
            if(pending.size()>=256) fail("GAMERAM pending native services exceed 256 retained deliveries.");
            const auto entry=pending.emplace(key,std::move(required)).first;
            return {false,0,entry->second.diagnostic};
        } catch(...) {fault=true;throw;}
    }
};

GameRamRuntime::GameRamRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()) {
    const auto owner=impl_;
    runtime.register_native_controller(owner->ram,{
        [owner]()->std::optional<std::uint8_t> {return std::uint8_t{1};},
        [owner] {
            return MessengerManager{
                [owner](NativeMessenger& messenger,const MessengerDelivery& delivery) {return owner->call(messenger,delivery);},
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
} // namespace dw4
