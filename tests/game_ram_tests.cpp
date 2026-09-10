#include "dw4/game_ram.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/assets.hpp"
#include "dw4/model_game_data.hpp"
#include "dw4/model_script_messenger.hpp"
#include "dw4/model_script_runtime.hpp"
#include "dw4/resources.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool condition,const char* message) {++checks;if(!condition)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid GAMERAM operation was accepted.");
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
MessengerDispatch prepare(ActionEventRuntime& runtime,std::string command,std::optional<std::int32_t> index={}) {
    auto& messenger=runtime.messenger();messenger.select("GAMERAM",std::nullopt,-1);
    if(index)messenger.buffer().add_int(*index);
    return messenger.begin_send(std::move(command));
}
void completed(ActionEventRuntime& runtime,MessengerDispatch& dispatch) {
    check(runtime.resume(dispatch).stop==MessengerStop::completed,"Original GAMERAM command did not complete.");
}
MessengerDataList returned(ActionEventRuntime& runtime,std::size_t index=0) {
    const auto& objects=runtime.messenger().buffer().objects();
    check(index<objects.size(),"GETPLAYER omitted its retained row object.");
    const auto* row=std::get_if<MessengerDataList>(&objects[index]);
    check(row && *row,"GETPLAYER returned an identity sentinel, copied type or null instead of a native named row.");
    return *row;
}

void ownership_and_getplayer() {
    ActionEventRuntime runtime;GameRamRuntime game(runtime);
    check(game.manager_id().is_native() && game.info_id().is_native() && game.ram_id().is_native() &&
        game.manager_id()!=game.info_id() && game.manager_id()!=game.ram_id() && game.info_id()!=game.ram_id() &&
        game.ram_id()!=ControllerObjectId{0,0},"Generated manager/info/RAM identities are not distinct from each other and serialized objects.");
    check(runtime.messenger().registration_count()==1 && runtime.messenger().has_category("GAMERAM"),"GAMERAM creation registered the wrong number of rows.");
    rejects([&]{runtime.register_native_controller(game.ram_id(),{[]{return std::optional<std::uint8_t>{std::uint8_t{1}};},[]{return MessengerManager{};}});});
    std::array<std::shared_ptr<GameDataInt>,4> hp;
    for(std::int32_t index=0;index<4;++index) {
        auto get=prepare(runtime,"GETPLAYER",index);completed(runtime,get);
        const auto row=returned(runtime);check(row==game.state().player(static_cast<std::size_t>(index)),"GETPLAYER copied its existing row.");
        check(runtime.messenger().buffer().integers()==std::vector<std::int32_t>{index},"GETPLAYER cleared original input lists.");
        hp[index]=row->require<GameDataInt>("HP");check(hp[index]->data==0 && row->require<GameDataInt>("MHP")->data==0,"Factory state invented selected-player HP.");
        auto fixed=prepare(runtime,"GETPLAYER"+std::to_string(index+1),99);completed(runtime,fixed);
        check(returned(runtime)==row,"Fixed GETPLAYER ordinal consumed int0 or selected the wrong row.");
    }
    for(std::size_t i=0;i<hp.size();++i)for(std::size_t j=i+1;j<hp.size();++j)check(hp[i]!=hp[j],"Four generated player HP wrappers alias each other.");
    auto& messenger=runtime.messenger();messenger.select("GAMERAM","gameram",0);messenger.buffer().set_enabled_only(1);
    auto active=messenger.begin_send("GETPLAYER1");completed(runtime,active);check(returned(runtime)==game.state().player(0),"Registered native RAM did not expose original active byte1/name/index.");
    messenger.select("GAMERAM","wrong-name",0);auto unmatched=messenger.begin_send("GETPLAYER1");completed(runtime,unmatched);
    check(messenger.buffer().objects().empty(),"Native GAMERAM registration lost its actual name.");
}

void reset_and_shared_model_wrappers() {
    ActionEventRuntime runtime;GameRamRuntime game(runtime);
    auto row0=game.state().player(0),row1=game.state().player(1),common=game.state().common();
    auto old_common=common->require<GameDataInt>("NINZU");old_common->data=4;
    auto target=std::make_shared<ModelScriptWork>(ControllerObjectId{7,100},"prepared/player.igb","p_test",0,
        std::make_shared<AnimationMatrix>(identity()));
    target->hp=row0->require<GameDataInt>("HP");target->max_hp=row0->require<GameDataInt>("MHP");
    target->hp->data=17;target->max_hp->data=29;
    check(row0->require<GameDataInt>("HP")->data==17,"Model HP preparation copied rather than retained the native wrapper.");
    auto reset1=prepare(runtime,"GAMEDATAINIT",1);completed(runtime,reset1);
    check(game.state().player(0)==row0 && game.state().player(1)!=row1 && game.state().common()==common &&
        common->require<GameDataInt>("NINZU")==old_common && old_common->data==4,"Nonzero row reset changed unrelated row/common identities.");
    auto reset0=prepare(runtime,"GAMEDATAINIT",0);completed(runtime,reset0);
    const auto replacement=game.state().player(0);
    check(replacement!=row0 && game.state().common()==common && common->require<GameDataInt>("NINZU")!=old_common &&
        common->require<GameDataInt>("NINZU")->data==0 && old_common->data==4,"Row zero reset did not repopulate the same common-list owner.");
    check(target->hp==row0->require<GameDataInt>("HP") && target->hp->data==17 && target->max_hp->data==29 &&
        replacement->require<GameDataInt>("HP")!=target->hp && replacement->require<GameDataInt>("HP")->data==0,
        "Reset silently retargeted existing MODELCTRL HP aliases.");
    auto get=prepare(runtime,"GETPLAYER",0);completed(runtime,get);check(returned(runtime)==replacement,"GETPLAYER returned a stale cached row after reset.");
    check(runtime.resume(reset0).stop==MessengerStop::completed && game.state().player(0)==replacement,"Completed dispatch retry reset the row twice.");
}

void retained_endpoint_lifetime_and_multiple_managers() {
    ActionEventRuntime runtime;std::shared_ptr<GameDataList> original;
    ControllerObjectId old_id;
    {GameRamRuntime game(runtime);original=game.state().player(2);old_id=game.ram_id();}
    auto get=prepare(runtime,"GETPLAYER3");completed(runtime,get);check(returned(runtime)==original,"Destroyed facade invalidated registered native manager/data ownership.");
    GameRamRuntime second(runtime);check(second.ram_id()!=old_id,"A later generated manager reused a native RAM identity.");
    // Existing nonempty recipient caches intentionally do not incorporate a
    // later registration. Select a new name key to observe the two actual rows.
    auto& messenger=runtime.messenger();messenger.select("GAMERAM","gameram",-1);messenger.buffer().add_int(2);
    auto both=messenger.begin_send("GETPLAYER");completed(runtime,both);
    check(returned(runtime,0)==original && returned(runtime,1)==second.state().player(2),"Ordered GAMERAM registrations deduplicated distinct native owners.");
}

void fill_other_lists(MessengerBuffer& buffer) {
    buffer.set_enabled_only(255);buffer.add_bool(7);buffer.add_float(1.25f);buffer.add_int64(-99);
    buffer.add_vector({1,2,3});buffer.add_matrix(identity());buffer.add_object(std::nullopt);
}
void other_lists(const MessengerBuffer& buffer) {
    check(buffer.enabled_only()==255 && buffer.booleans()==std::vector<std::uint8_t>{7} &&
        buffer.floats()==std::vector<float>{1.25f} && buffer.integers64()==std::vector<std::int64_t>{-99} &&
        buffer.vectors()==std::vector<std::array<float,3>>{{1,2,3}} && buffer.matrices()==std::vector<AnimationMatrix>{identity()} &&
        buffer.objects().size()==1 && std::holds_alternative<std::monostate>(buffer.objects()[0]) &&
        buffer.strings()==std::vector<std::optional<std::string>>{"P2PKOUKAMAGIC"},"P2PKOUKAMAGIC changed a non-integer list or enabled-only gate.");
}
void ordinary_magic_gate() {
    for(std::int32_t target=0;target<4;++target)for(std::int32_t source=0;source<4;++source) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto& messenger=runtime.messenger();
        messenger.select("GAMERAM",std::nullopt,-1);fill_other_lists(messenger.buffer());
        messenger.buffer().add_string("P2PKOUKAMAGIC");
        for(const auto value:{target,source,80000})messenger.buffer().add_int(value);
        auto send=messenger.begin_send("FIGHTCALC");completed(runtime,send);
        const std::vector<std::int32_t> expected=target==source ? std::vector<std::int32_t>{1} : std::vector<std::int32_t>{target,source,80000};
        check(messenger.buffer().integers()==expected && game.state().player(target)->require<GameDataInt>("HP")->data==0,
            "Unequal/default magic gate invented damage or returned the wrong integer list.");
        other_lists(messenger.buffer());
    }
    for(const auto kind:{0,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),-80040,80001,90000}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto& messenger=runtime.messenger();
        messenger.select("GAMERAM",std::nullopt,-1);messenger.buffer().add_string("P2PKOUKAMAGIC");
        for(const auto value:{2,2,kind})messenger.buffer().add_int(value);
        auto send=messenger.begin_send("FIGHTCALC");completed(runtime,send);
        check(messenger.buffer().integers()==std::vector<std::int32_t>{1},"Signed remainder incorrectly entered a positive effect branch.");
    }
}

void explicit_effect_and_bad_input_boundaries() {
    for(const auto kind:{80040,80045,80055,80060,80050,-80050,80065,80080,80075}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto& messenger=runtime.messenger();
        messenger.select("GAMERAM",std::nullopt,-1);fill_other_lists(messenger.buffer());messenger.buffer().add_string("P2PKOUKAMAGIC");
        for(const auto value:{1,1,kind})messenger.buffer().add_int(value);
        const NativeMessenger& read_only=messenger;auto send=messenger.begin_send("FIGHTCALC");
        const auto first=runtime.resume(send);
        check(first.stop==MessengerStop::service_required && game.pending_count()==1 && !game.faulted() &&
            first.diagnostic.find("effect service")!=std::string::npos,"Unimplemented original magic effect reported success.");
        other_lists(read_only.buffer());
        check(read_only.buffer().integers()==std::vector<std::int32_t>{1,1,kind},"Missing magic effect fabricated the final integer1 reply.");
        rejects([&]{runtime.messenger();});
        const auto retry=runtime.resume(send);check(retry.stop==MessengerStop::service_required && retry.diagnostic==first.diagnostic &&
            game.pending_count()==1,"Unchanged effect retry reparsed/replaced its retained request.");
    }
    for(const auto command:{"GAMEREMOVE","FIGHTCALC"}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto send=prepare(runtime,command);
        if(std::string(command)=="FIGHTCALC")runtime.messenger().buffer().add_string("UNIMPLEMENTED_SUBCOMMAND");
        check(runtime.resume(send).stop==MessengerStop::service_required && game.pending_count()==1,
            "An unimplemented removal/combat service was substituted with successful empty behavior.");
    }
    for(const auto index:{-1,4}) {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto send=prepare(runtime,"GETPLAYER",index);
        check(runtime.resume(send).stop==MessengerStop::faulted && game.faulted(),"Invalid native row index was accepted.");
    }
    {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto send=prepare(runtime,"GETPLAYER");
        check(runtime.resume(send).stop==MessengerStop::faulted && game.faulted(),"Missing GETPLAYER int0 invented a player index.");
    }
    {
        ActionEventRuntime runtime;GameRamRuntime game(runtime);auto& messenger=runtime.messenger();
        messenger.select("GAMERAM",std::nullopt,-1);messenger.buffer().add_string("P2PKOUKAMAGIC");
        for(const auto value:{-1,2,80000})messenger.buffer().add_int(value);
        auto send=messenger.begin_send("FIGHTCALC");
        check(runtime.resume(send).stop==MessengerStop::faulted && game.faulted(),"Unequal-RAM branch bypassed the original target-row lookup prerequisite.");
    }
}

void original_hit(const std::filesystem::path& root) {
    const auto archive=assets::AfsArchive::open(root/"files"/"ndmw.afs");
    const auto script_document=read_igb_document(archive.read(179));
    const std::array registrations{ScriptRegistration{&script_document,"ndmw entry 179",44,237,2783}};
    const ScriptRegistry registry(registrations);
    const auto plan=registry.lookup("hit_p_agumon.lua");
    check(plan.entries.size()==1 && plan.entries[0].state==ScriptLookupState::match &&
        plan.entries[0].script->bytes.size()==3230 && plan.entries[0].script->info.data_index==26,
        "Original entry179 Agumon Hit selection/provenance changed.");
    const auto resources=MountedResources::open(root);
    const auto model=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto hit=decode_model_hit_body(model,1386,"_common/p_agumon.igb",7);
    constexpr std::array<std::uint32_t,4> refs{1725,6417,6420,6622};
    for(std::int32_t selected=0;selected<4;++selected)for(const bool same_source:{true,false}) {
        ActionEventRuntime events;GameRamRuntime game(events);ModelScriptMessenger bridge(events);
        ModelScriptRuntime scripts(registry);std::vector<std::shared_ptr<ModelScriptWork>> targets;
        for(std::uint32_t index=0;index<4;++index) {
            auto binding=decode_player_runtime_binding(model,{1386,546,index,1},"_common/p_agumon.igb",7);
            check(binding.model_ram_reference()==refs[index] && binding.model_name()=="p_agumon",
                "Original target did not use its selected authored MODELCTRL RAM.");
            const auto id=events.register_model(std::move(binding),ModelControlState(ModelMotionPose(identity())),true);
            events.prepare_collision_target(id,hit,true);
            auto target=events.prepare_target_script_work(id);
            auto get=prepare(events,"GETPLAYER",static_cast<std::int32_t>(index));completed(events,get);
            const auto bound=bind_model_game_data(*target,returned(events));
            check(bound.row()==game.state().player(index) && bound.hp()==target->hp && bound.max_hp()==target->max_hp &&
                bound.refilled() && target->hp->data==0 && target->max_hp->data==0,
                "Actual GETPLAYER/model preparation copied aliases or invented initialized HP.");
            scripts.register_work(target);targets.push_back(std::move(target));
        }
        std::vector<std::string> methods;
        scripts.set_host_service([&](auto& context,const auto& call) {
            methods.push_back(call.name);return bridge.handle(context,call);
        });
        const auto source=same_source ? selected : (selected+1)%4;
        const auto& target=targets[static_cast<std::size_t>(selected)];
        const ModelTargetHitRequest input{target->target(),target->source(),target->name(),selected,"hit","Hit",80000,0,
            "p_agumon",source,-1};
        // Retain read-only inspection of the original buffer for this test.
        // No mutable access bypasses the suspended action-event owner.
        const NativeMessenger& inspection=events.messenger();
        check(!scripts.handle(input) && scripts.pending() && !scripts.faulted() && bridge.pending() &&
            game.pending_count()==1 && !game.faulted(),"Original magic Hit skipped its real remaining GAMERAM skill-growth service.");
        const auto& result=scripts.last_lua_result();
        check(result.stop==Lua4RunStop::host_required && result.site.pc==63 && result.instructions==48 && result.host_calls==5 &&
            methods==std::vector<std::string>{"be.BLPcalcKoukaMagic","be.BLTcalcSkillUp"},
            "Original Hit did not advance through actual BLP into its authored BLT call.");
        const auto expected_flag=same_source || selected!=0;
        check(scripts.diagnostic().find("P2ESKILLUP")!=std::string::npos && scripts.context_runtime().context().direction==0 &&
            inspection.buffer().integers()==std::vector<std::int32_t>{source,80000,expected_flag,0} &&
            inspection.buffer().strings()==std::vector<std::optional<std::string>>{"P2ESKILLUP"},
            "Original BLT packet lost its BLP result, current source RAM, kind or zero flag.");
        const auto instructions=result.instructions,calls=result.host_calls;
        rejects([&]{events.messenger();});
        check(!scripts.handle(input) && scripts.last_lua_result().instructions==instructions && scripts.last_lua_result().host_calls==calls &&
            methods==std::vector<std::string>{"be.BLPcalcKoukaMagic","be.BLTcalcSkillUp","be.BLTcalcSkillUp"} &&
            game.pending_count()==1 && scripts.context_runtime().context().direction==0,
            "Unavailable skill-growth retry replayed the completed BLP/Lua/direction prefix.");
        for(std::size_t index=0;index<targets.size();++index)
            check(targets[index]->hp==game.state().player(index)->require<GameDataInt>("HP") && targets[index]->hp->data==0,
                "Pending original Hit replaced another player's HP owner or fabricated damage.");
    }
}
}
int main(int argc,char** argv) {
    try {
        ownership_and_getplayer();reset_and_shared_model_wrappers();retained_endpoint_lifetime_and_multiple_managers();
        ordinary_magic_gate();explicit_effect_and_bad_input_boundaries();
        const auto synthetic=checks;
        if(argc==2)original_hit(std::filesystem::path(argv[1]));
        else if(argc!=1)throw std::runtime_error("Usage: dw4_game_ram_tests [EXTRACTED_ROOT]");
        std::cout<<"GAMERAM tests passed: "<<checks<<" checks (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"GAMERAM tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
