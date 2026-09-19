#include "dw4/model_player_input.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_timer.hpp"
#include "dw4/pad.hpp"
#include "dw4/viewer.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,std::string category,std::optional<std::string> name,int index,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [hook=std::move(hook)]{return MessengerManager{hook,{}};}},std::move(category),std::move(name),index);
}
constexpr AnimationMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
struct Fixture {
    ActionEventRuntime events;
    NativeMessenger& messenger=events.messenger();
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    std::shared_ptr<ModelPadState> pad=std::make_shared<ModelPadState>();
    ModelPlayerInputServices services;
    Fixture() {
        one->name="ONE";one->work=work;ram->data=std::make_shared<ModelSceneList>();ram->data->append(one);
        ram->number=1;ram->manager=events.model_manager();events.set_model_context(std::make_shared<ModelSceneContext>());
        auto pads=std::make_shared<ModelPadManager>();pads->data=std::make_shared<ModelPadList>();
        pads->data->backing=std::make_shared<ModelPadList::Storage>(5);
        (*pads->data->backing)[1]=pad;events.model_manager()->pads=pads;
        work->sequence.name="SQ_WAIT";work->turn_attack_timers=std::make_shared<std::vector<std::int32_t>>(8,0);
        work->script.original_is_main_lobby=std::uint8_t{0};
    }
    void camera() {
        auto matrix=std::make_shared<AnimationMatrix>(identity);
        services.camera_matrix=[matrix](const ModelSceneManager&){return matrix;};
        // Explicit synthetic inverse provider, not an original-camera fallback.
        services.inverse_camera=[](const AnimationMatrix&)->std::optional<AnimationMatrix>{return identity;};
    }
    MessengerCallResult run(ModelPlayerInputInvocation& call) {return call.resume(messenger,events,services);}
};

void physical_pad_selection() {
    auto manager=std::make_shared<ModelPadManager>();manager->data=std::make_shared<ModelPadList>();
    auto storage=std::make_shared<ModelPadList::Storage>();
    for(int i=0;i<5;++i)storage->push_back(std::make_shared<ModelPadState>());
    manager->data->backing=storage;manager->data->count=-5;manager->data->capacity=0;
    for(int i=0;i<4;++i)check(model_pad(manager,i)==(*storage)[i],"Pad selection incorrectly used logical count/capacity.");
    check(model_pad(manager,4)==(*storage)[4] && model_pad(manager,999)==(*storage)[4],"Large pad number did not select physical fifth entry.");
    rejects([&]{model_pad(manager,-1);},"Negative nonredirected physical slot was accepted.");
    manager->redirect=255;
    check(model_pad(manager,-1)==(*storage)[4] && model_pad(manager,0)==(*storage)[4],"Redirect must select fifth entry before interpreting a negative number.");
    (*storage)[4].reset();check(!model_pad(manager,0),"Original nullable selected pointer was repaired.");
    manager->data->backing.reset();rejects([&]{model_pad(manager,0);},"Null pad backing was accepted.");
    manager->data.reset();rejects([&]{model_pad(manager,0);},"Null pad data list was accepted.");
    rejects([&]{model_pad({},0);},"Null pad manager was accepted.");
}
void movement_captures_and_services() {
    Fixture f;f.pad->direction=2;f.pad->left_x=1;f.pad->left_y=0;
    auto matrix=std::make_shared<AnimationMatrix>(identity);unsigned captures{},inversions{};
    ModelPlayerInputInvocation call(f.ram);
    check(!f.run(call).completed && (*f.work->turn_attack_timers)[2]==29 && f.work->key_input==0,
        "Camera service pause lost the completed history prefix or set movement too soon.");
    check(!f.run(call).completed && (*f.work->turn_attack_timers)[2]==29,"Camera pause repeated history decrement.");
    auto next=make_model_player_work();next->sequence.name="SQ_WAIT";
    f.services.camera_matrix=[&](const ModelSceneManager& owner) {
        ++captures;check(&owner==f.events.model_manager().get(),"Camera read changed the original controller.");
        f.one->work=next;f.ram->parent.reset();f.ram->number=3;return matrix;
    };
    f.services.inverse_camera=[](const AnimationMatrix&)->std::optional<AnimationMatrix>{return {};};
    check(!f.run(call).completed && captures==1,"Pending inverse override did not retain the captured camera owner.");
    f.services.camera_matrix={};
    f.services.inverse_camera=[&](const AnimationMatrix& value)->std::optional<AnimationMatrix> {
        ++inversions;check(&value==matrix.get(),"Camera inverse lost captured matrix identity.");
        f.pad->trigger=pad_button::x;f.pad->left_x=0;f.pad->left_y=1;return identity;
    };
    check(f.run(call).completed && captures==1 && inversions==1 && call.captured_work()==f.work && call.captured_pad()==f.pad &&
        f.work->key_input==1 && f.work->motion.turn.desired[1]==0x1.921fb6p+0f && f.work->key_event_name=="SQ_JUMP" &&
        !next->key_event_name,"Input did not retain stick/work while reading later live trigger buttons.");
    check(f.run(call).completed && inversions==1,"Completed input replayed camera work.");

    Fixture invalid;invalid.pad->direction=1;invalid.work->turn_attack_timers->resize(3);ModelPlayerInputInvocation partial(invalid.ram);
    rejects([&]{invalid.run(partial);},"Short physical history should fault at its reached entry.");
    check(partial.faulted() && (*invalid.work->turn_attack_timers)[0]==29,"Short history failure rolled back the initial write/decrement.");
    Fixture idle;idle.pad->left_x=std::numeric_limits<float>::quiet_NaN();idle.work->turn_attack_timers.reset();
    ModelPlayerInputInvocation unused(idle.ram);check(idle.run(unused).completed,"Stationary input consumed unused history or stick arithmetic.");
}
void native_camera_inverse() {
    Fixture f;f.pad->direction=1;f.pad->left_y=-1;
    const AnimationMatrix view{0,0,1,0,0,1,0,0,-1,0,0,0,3,4,5,1};
    const auto owner=std::make_shared<AnimationMatrix>(view);
    f.services.camera_matrix=[owner](const auto&){return owner;};
    ModelPlayerInputInvocation rotated(f.ram);
    check(f.run(rotated).completed && f.work->key_input==1 && f.work->motion.turn.desired[1]==0x1.921fb6p+0f,
        "Native camera inversion did not transform +Z through the real view matrix.");
    check(*owner==view,"Native player camera inversion changed its retained source owner.");
    *owner=AnimationMatrix{};
    ModelPlayerInputInvocation singular(f.ram);
    check(f.run(singular).completed && f.work->key_input==1 && f.work->motion.turn.desired[1]==0,
        "Singular camera did not preserve the original caller's identity-initialized inverse destination.");
    owner->at(0)=std::numeric_limits<float>::infinity();
    ModelPlayerInputInvocation invalid(f.ram);
    rejects([&]{f.run(invalid);},"Nonfinite camera escaped the native inverse's explicit arithmetic boundary.");
    check(invalid.faulted(),"A failed native inverse did not fault the input continuation.");
}
void actual_viewer_owners() {
    Fixture f;f.pad->direction=1;f.pad->left_y=-1;
    auto viewer=std::make_shared<ViewerSceneInfoManager>();
    auto parameters=make_viewer_parameters();
    parameters->view_matrix={0,0,1,0,0,1,0,0,-1,0,0,0,3,4,5,1};
    viewer->parameters=parameters;f.events.model_manager()->viewer=viewer;
    {
        ModelPlayerInputInvocation call(f.ram);
        check(f.run(call).completed && f.work->key_input==1 &&
            f.work->motion.turn.desired[1]==0x1.921fb6p+0f,
            "Native input did not follow its actual viewer parameters without service overrides.");
    }
    std::weak_ptr<ViewerParameters> retained=parameters;
    const auto selected_matrix=&parameters->view_matrix;
    f.services.inverse_camera=[](const AnimationMatrix&)->std::optional<AnimationMatrix>{return {};};
    {
        ModelPlayerInputInvocation pending(f.ram);
        check(!f.run(pending).completed,"Explicit inverse boundary did not retain the selected native viewer matrix.");
        auto replacement=std::make_shared<ViewerSceneInfoManager>();replacement->parameters=make_viewer_parameters();
        f.events.model_manager()->viewer=replacement;
        viewer->parameters.reset();parameters.reset();viewer.reset();
        check(!retained.expired(),"Replacing viewer parameters destroyed the inline matrix of pending input.");
        f.services.inverse_camera=[&](const AnimationMatrix& source)->std::optional<AnimationMatrix> {
            check(&source==selected_matrix,"Pending input switched to replacement viewer parameters.");
            f.pad->trigger=pad_button::x;return identity;
        };
        check(f.run(pending).completed && f.work->key_event_name=="SQ_JUMP",
            "Retained viewer input failed to resume or lost the later live trigger word.");
    }
    check(retained.expired(),"Completed input destruction leaked the replaced parameters allocation.");

    for(const bool missing_parameters:{false,true}) {
        Fixture invalid;invalid.pad->direction=1;invalid.pad->left_y=-1;
        invalid.events.model_manager()->viewer=missing_parameters ?
            std::make_shared<ViewerSceneInfoManager>():std::shared_ptr<ViewerSceneInfoManager>{};
        ModelPlayerInputInvocation call(invalid.ram);
        rejects([&]{invalid.run(call);},"Represented null viewer state was treated as missing integration or repaired.");
        check(call.faulted() && invalid.work->turn_attack_timers->at(0)==29 && invalid.work->key_input==0,
            "Reached viewer failure lost the completed history prefix or published input afterward.");
    }
    Fixture idle;idle.events.model_manager()->viewer=std::shared_ptr<ViewerSceneInfoManager>{};
    ModelPlayerInputInvocation untouched(idle.ram);
    check(idle.run(untouched).completed,"Stationary input consumed its unused null viewer owner.");

    Fixture overridden;overridden.pad->direction=1;overridden.pad->left_y=-1;
    overridden.events.model_manager()->viewer=std::shared_ptr<ViewerSceneInfoManager>{};overridden.camera();
    ModelPlayerInputInvocation external(overridden.ram);
    check(overridden.run(external).completed,"Explicit camera override lost precedence over native viewer selection.");
    auto valid=std::make_shared<ViewerSceneInfoManager>();valid->parameters=make_viewer_parameters();
    overridden.events.model_manager()->viewer=valid;
    overridden.services.camera_matrix=[](const auto&)->std::shared_ptr<AnimationMatrix>{return {};};
    ModelPlayerInputInvocation null_override(overridden.ram);
    rejects([&]{overridden.run(null_override);},"A returned null override incorrectly fell back to native viewer parameters.");
}
void body_attack_policy() {
    Fixture f;f.pad->held=pad_button::b;f.pad->trigger=pad_button::b;f.work->body_attack_timer=4;
    f.events.model_manager()->system=std::make_shared<ModelSceneSystem>();
    f.events.model_manager()->system->frame_time_f=999;
    f.services.frame_delta=[](const ModelSceneManager&)->std::optional<float>{return {};};
    ModelPlayerInputInvocation first(f.ram);
    check(!f.run(first).completed && f.work->key_event_name=="SQ_GUARD" && f.work->body_attack_state==0,
        "Unavailable override must preserve guard publication and suspend even when actual system time is present.");
    f.services.frame_delta=[&](const ModelSceneManager&)->std::optional<float> {
        f.work->body_attack_timer=100;return 2.0f;
    };
    check(f.run(first).completed && f.work->body_attack_timer==2 && f.work->body_attack_state==1,
        "Body timer subtraction reloaded work after its service instead of captured f2.");
    f.camera();f.pad->trigger=0;f.pad->direction=1;f.pad->left_y=-1;
    ModelPlayerInputInvocation second(f.ram);check(f.run(second).completed && f.work->body_attack_state==2 &&
        f.work->body_attack_timer==15 && f.work->body_attack_pad==1,"Body attack state1 did not save moving direction/window.");
    f.pad->direction=0;ModelPlayerInputInvocation third(f.ram);
    check(f.run(third).completed && f.work->body_attack_state==3,"Body attack state2 did not observe released direction.");
    f.pad->direction=1;f.work->control.mode=7;f.work->control.phase=8;ModelPlayerInputInvocation fourth(f.ram);
    check(f.run(fourth).completed && f.work->key_event_name=="SQ_ATCK3" && f.work->body_attack_state==0 &&
        f.work->control.mode==0 && f.work->control.phase==0,"Body attack state3 did not publish/reset the original fields.");
    f.work->body_attack_timer=std::numeric_limits<float>::quiet_NaN();f.pad->direction=0;
    f.services.frame_delta=[](const ModelSceneManager&)->std::optional<float>{throw std::runtime_error("Unreached delta.");};
    ModelPlayerInputInvocation nan(f.ram);check(f.run(nan).completed && std::isnan(f.work->body_attack_timer),"NaN positive-timer gate should skip delta read.");
}
void actual_body_frame_time() {
    Fixture reached;reached.pad->held=pad_button::b;reached.pad->trigger=pad_button::b;
    reached.work->body_attack_timer=4;
    auto system=std::make_shared<ModelSceneSystem>();system->frame_time_f=1.5f;
    reached.events.model_manager()->system=system;
    auto replacement=std::make_shared<ModelSceneManager>();replacement->system=std::make_shared<ModelSceneSystem>();
    replacement->system->frame_time_f=100;
    reached.ram->manager=replacement;
    ModelPlayerInputInvocation direct(reached.ram);
    check(reached.run(direct).completed && reached.work->body_attack_timer==2.5f && reached.work->body_attack_state==1 &&
        reached.work->key_event_name=="SQ_GUARD", "Default delta must read the invoking controller system, not a replacement RAM manager.");
    system->frame_time_f=7;
    ModelPlayerInputInvocation later(reached.ram);
    check(reached.run(later).completed && reached.work->body_attack_timer==0,
        "A later input invocation must read the current system frame time and clamp the subtraction.");

    Fixture fresh;fresh.camera();fresh.pad->direction=1;fresh.pad->held=pad_button::b;fresh.work->body_attack_timer=6;
    auto old_system=std::make_shared<ModelSceneSystem>();old_system->frame_time_f=100;
    fresh.events.model_manager()->system=old_system;
    auto new_system=std::make_shared<ModelSceneSystem>();new_system->frame_time_f=2;
    fresh.services.inverse_camera=[&](const AnimationMatrix&)->std::optional<AnimationMatrix> {
        fresh.events.model_manager()->system=new_system;fresh.ram->manager.reset();return identity;
    };
    ModelPlayerInputInvocation replaced(fresh.ram);
    check(fresh.run(replaced).completed && fresh.work->body_attack_timer==4,
        "Default delta captured system too early or followed the now-null RAM manager.");

    Fixture missing;missing.pad->held=missing.pad->trigger=pad_button::b;missing.work->body_attack_timer=3;
    ModelPlayerInputInvocation absent(missing.ram);
    rejects([&]{missing.run(absent);},"A reached null system must fault instead of supplying zero or suspending.");
    check(absent.faulted() && missing.work->key_event_name=="SQ_GUARD" && missing.work->body_attack_timer==3 &&
        missing.work->body_attack_state==0,"Missing system must preserve guard and captured timer before any body transition.");

    for(const auto timer:{0.0f,-2.0f,std::numeric_limits<float>::quiet_NaN()}) {
        Fixture unused;unused.pad->held=pad_button::b;unused.work->body_attack_timer=timer;
        ModelPlayerInputInvocation skipped(unused.ram);
        check(unused.run(skipped).completed && (std::isnan(timer) ? std::isnan(unused.work->body_attack_timer):
            unused.work->body_attack_timer==timer),"A nonpositive or unordered timer consumed the missing system.");
    }
    Fixture released;released.work->body_attack_timer=3;released.work->body_attack_state=2;
    ModelPlayerInputInvocation no_b(released.ram);
    check(released.run(no_b).completed && released.work->body_attack_timer==3 && released.work->body_attack_state==0,
        "Released B must reset body state without consuming the system or positive timer.");
    Fixture lobby;lobby.work->script.original_is_main_lobby=std::uint8_t{1};lobby.pad->held=pad_button::b;
    lobby.work->body_attack_timer=3;lobby.work->body_attack_state=2;
    ModelPlayerInputInvocation lobby_call(lobby.ram);
    check(lobby.run(lobby_call).completed && lobby.work->body_attack_timer==3 && lobby.work->body_attack_state==2,
        "Main-lobby input consumed the unused system or body state.");

    Fixture unordered;unordered.pad->held=pad_button::b;unordered.work->body_attack_timer=3;
    unordered.events.model_manager()->system=std::make_shared<ModelSceneSystem>();
    unordered.events.model_manager()->system->frame_time_f=std::numeric_limits<float>::quiet_NaN();
    ModelPlayerInputInvocation nan_delta(unordered.ram);
    check(unordered.run(nan_delta).completed && unordered.work->body_attack_timer==0,
        "Actual unordered delta must reach the original subtraction clamp rather than eager finite validation.");
}
void panels_and_opaque_work() {
    const std::array<std::pair<std::uint32_t,std::string_view>,4> cases{{
        {pad_button::y,"GENEEMGHEAL"},{pad_button::l,"GENERATEITEM"},
        {pad_button::r,"GENERATEWAZA"},{pad_button::z,"GENERATESTAT"}}};
    for(const auto& [button,message]:cases) {
        Fixture f;f.pad->trigger=button;ModelPlayerInputInvocation call(f.ram);
        check(!f.run(call).completed && f.work->panel==0,"Missing panel provider changed panel state.");
        f.ram->number=9;auto replacement=make_model_player_work();f.one->work=replacement;
        provider(f.events,"NDMWPANEL",{},1,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
            check(delivery.message.command==message && delivery.message.index==1 && live.buffer().objects().size()==1 &&
                std::get<std::shared_ptr<ModelPlayerWork>>(live.buffer().objects()[0])==f.work,
                "Panel request lost original selected arguments or captured work pointer.");
            live.buffer().add_bool(0);return MessengerCallResult{true,1,{}};
        });
        check(f.run(call).completed && f.work->panel==1 && replacement->panel==0,"Panel opening must test response count, not boolean truth.");
    }
    Fixture f;f.work->panel=1;ModelPlayerInputInvocation call(f.ram);unsigned queries{};
    std::shared_ptr<const MessengerBuffer> packet;
    provider(f.events,"NDMWPANEL",{},1,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++queries;check(delivery.message.command=="ISCHECK","Panel did not query its original command.");
        if(queries==1) {packet=live.buffer_owner();live.buffer().add_bool(1);return MessengerCallResult{false,0,"Synthetic pause."};}
        check(packet==live.buffer_owner() && live.buffer().booleans().size()==1,"Panel query recreated its retained packet.");
        live.buffer().add_bool(0);live.buffer().add_bool(1);live.buffer().add_string("fire");return MessengerCallResult{true,1,{}};
    });
    check(!f.run(call).completed && f.run(call).completed && queries==2 && f.work->key_event_name=="SQ_WAZA" &&
        f.work->sequence.labels->sequences->at(0)=="SQ_WAZA" && f.work->sequence.labels->names->at(0)=="fire",
        "Panel technique result did not set live label and requested sequence after retained query.");
    Fixture item;item.work->panel=1;
    provider(item.events,"NDMWPANEL",{},1,[](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(1);live.buffer().add_bool(1);live.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
    });
    ModelPlayerInputInvocation item_call(item.ram);check(item.run(item_call).completed && item.work->key_event_name=="SQ_ITEM","Panel item result lost its branch.");
}
void attack_timer_and_live_data() {
    Fixture f;f.pad->trigger=pad_button::a;ModelPlayerInputInvocation call(f.ram);unsigned drops{},icons{},rows{};
    provider(f.events,"DROPITEM",{},-1,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++drops;check(delivery.message.command=="ACTION1" && live.buffer().integers()==std::vector<std::int32_t>{1} &&
            live.buffer().enabled_only()==1,"Attack pickup packet lost RAM number or enabled gate.");
        return MessengerCallResult{true,1,{}};
    });
    check(!f.run(call).completed && drops==1 && f.work->key_event_name=="SQ_ATCK1" && !find_model_timer(*f.ram,"ATDELAY"),
        "Missing player row did not preserve published attack request and absence check.");
    f.services.player_row=[&](const ModelSceneManager&,std::int32_t number) {
        ++rows;check(number==1,"Player data row did not use live RAM number.");f.ram->number=2;
        return std::make_shared<GameDataList>(std::vector<GameDataObject>{std::make_shared<GameDataInt>(GameDataInt{"ATTACKDELAY",26})});
    };
    check(!f.run(call).completed && drops==1 && rows==1 && find_model_timer(*f.ram,"ATDELAY")->timer.end_time==433333333,
        "Missing icon provider replayed pickup/row or omitted original attack timer.");
    f.ram->number=3;
    provider(f.events,"MODELCTRL","i_stpnl05",2,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        ++icons;check(delivery.message.command=="SQ_DLTM02" && delivery.message.index==2,"Attack icon index/selection capture differs from original.");
        return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed && drops==1 && rows==1 && icons==1,"Attack continuation replayed a completed prefix.");
    Fixture consumed;consumed.pad->trigger=pad_button::a;
    provider(consumed.events,"DROPITEM",{},-1,[](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(0);return MessengerCallResult{true,1,{}};
    });
    ModelPlayerInputInvocation consumed_call(consumed.ram);
    check(consumed.run(consumed_call).completed && !consumed.work->key_event_name,"Pickup response count must block attack even if value is false.");
    Fixture lobby;lobby.work->script.original_is_main_lobby=std::uint8_t{1};lobby.pad->trigger=pad_button::a;
    ModelPlayerInputInvocation lobby_call(lobby.ram);
    check(lobby.run(lobby_call).completed && ModelSubcommandQueue(lobby.work->subcommands).entries().size()==1,
        "Lobby A did not append actual hitbox subcommand.");
}
void status_and_failure_prefixes() {
    Fixture f;f.pad->held=f.pad->trigger=pad_button::start;ModelPlayerInputInvocation call(f.ram);
    provider(f.events,"NDMWPANEL",{},-1,[](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(2);return MessengerCallResult{true,1,{}};
    });
    check(!f.run(call).completed,"ISRUN value2 must continue to required SUBWND provider.");
    f.ram->number=2;
    provider(f.events,"SUBWND",{},0,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        check(delivery.message.command=="GENERATE" && live.buffer().strings()==std::vector<std::optional<std::string>>{"STATUS"} &&
            live.buffer().integers()==std::vector<std::int32_t>{2},"Status packet did not read RAM after selection.");
        return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed,"Status request failed to complete.");
    Fixture malformed;malformed.work->panel=1;
    provider(malformed.events,"NDMWPANEL",{},1,[](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
    });
    ModelPlayerInputInvocation bad(malformed.ram);rejects([&]{malformed.run(bad);},"Missing physical boolean2 must fail after query.");
    check(bad.faulted() && malformed.work->panel==1,"Malformed panel response changed unconsumed panel state.");
    Fixture recurse;recurse.pad->direction=1;recurse.camera();ModelPlayerInputInvocation recursive(recurse.ram);
    recurse.services.inverse_camera=[&](const AnimationMatrix&)->std::optional<AnimationMatrix> {
        rejects([&]{recurse.run(recursive);},"Recursive player input was accepted.");return identity;
    };
    rejects([&]{recurse.run(recursive);},"Caught recursive failure did not poison input.");
    check(recursive.faulted() && recurse.work->key_input==0 && (*recurse.work->turn_attack_timers)[0]==29,
        "Recursive failure lost completed counter prefix or published heading afterward.");
}
void spin_and_remaining_boundaries() {
    Fixture f;f.camera();f.pad->direction=1;f.pad->left_y=-1;f.pad->trigger=pad_button::a;
    std::fill(f.work->turn_attack_timers->begin(),f.work->turn_attack_timers->end(),1);
    provider(f.events,"DROPITEM",{},-1,[](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    ModelPlayerInputInvocation spin(f.ram);
    check(!f.run(spin).completed && f.work->key_event_name=="SQ_ATCK2" &&
        *f.work->turn_attack_timers==std::vector<std::int32_t>(8,0),
        "Eight pre-decrement positive history entries must select spin attack and clear the current list.");
    f.services.player_row=[](const ModelSceneManager&,std::int32_t) {
        return std::make_shared<GameDataList>(std::vector<GameDataObject>{std::make_shared<GameDataInt>(GameDataInt{"ATTACKDELAY",100})});
    };
    rejects([&]{f.run(spin);},"Out-of-profile icon-table index was accepted.");
    check(spin.faulted() && find_model_timer(*f.ram,"ATDELAY")->timer.end_time==1666666666 &&
        f.work->key_event_name=="SQ_ATCK2","Icon-table checked failure discarded attack selection or timer start.");

    Fixture gate;gate.pad->trigger=pad_button::a;
    provider(gate.events,"DROPITEM",{},-1,[](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    ModelRamTimerAccess(gate.ram).start("ATDELAY",0);
    ModelPlayerInputInvocation equality(gate.ram);
    check(gate.run(equality).completed && !gate.work->key_event_name,"Attack delay equality must remain unexpired.");

    Fixture hit;hit.pad->trigger=pad_button::a;hit.work->script.original_is_main_lobby=std::uint8_t{1};
    ModelPlayerInputInvocation hit_call(hit.ram);hit.run(hit_call);
    const auto& entries=ModelSubcommandQueue(hit.work->subcommands).entries();
    const auto& command=std::get<ModelHitboxSubcommandState>(entries[0].value).command;
    check(command.kind==60000 && command.delay==1 && command.length==1 && command.offset==std::array<float,3>{0,4,7} &&
        command.box_size==std::array<float,3>{3,3,3},"Lobby hitbox did not retain original factory field values.");

    Fixture sound;sound.work->panel=1;sound.pad->held=sound.pad->trigger=pad_button::start;
    provider(sound.events,"NDMWPANEL",{},1,[](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    ModelPlayerInputInvocation sound_call(sound.ram);
    check(!sound.run(sound_call).completed && sound.work->panel==0,"Empty panel query did not clear panel before later sound boundary.");
    provider(sound.events,"SOUND",{},-1,[](NativeMessenger& live,const MessengerDelivery& delivery) {
        check(delivery.message.command=="PLAY" && live.buffer().strings()==std::vector<std::optional<std::string>>{"CURSOR_NG"},
            "Panel status rejection did not request the original sound.");return MessengerCallResult{true,1,{}};
    });
    check(sound.run(sound_call).completed,"Sound continuation did not finish.");

    Fixture unknown;unknown.work->script.original_is_main_lobby.reset();unknown.work->key_input=1;
    ModelPlayerInputInvocation absent(unknown.ram);rejects([&]{unknown.run(absent);},"Unknown main-lobby byte was treated as true.");
    check(absent.faulted() && unknown.work->key_input==0,"Main-lobby checked failure lost earlier key reset.");
    Fixture null_work;null_work.one->work.reset();ModelPlayerInputInvocation null_call(null_work.ram);
    rejects([&]{null_work.run(null_call);},"Null captured work was accepted.");
    check(null_call.captured_pad()==null_work.pad,"Null work check ran before the original pad-selection prefix.");
}
}
int main() {
    try {
        physical_pad_selection();movement_captures_and_services();native_camera_inverse();actual_viewer_owners();body_attack_policy();actual_body_frame_time();panels_and_opaque_work();
        attack_timer_and_live_data();status_and_failure_prefixes();spin_and_remaining_boundaries();
        std::cout<<"Model player input tests passed: "<<checks<<" checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
