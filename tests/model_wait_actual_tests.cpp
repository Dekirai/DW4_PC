#include "dw4/model_wait_actual.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_pad.hpp"
#include "dw4/pad.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> std::string rejects(F call) {
    ++checks;try{call();}catch(const std::exception& error){return error.what();}
    throw std::runtime_error("Invalid actual wait operation was accepted.");
}
IgbDocument document(const pf::RuntimeFixture& fixture) {
    auto doc=fixture.document();const auto zero=*doc.field(doc.object(fixture.ones[0]),3);
    auto active=*doc.field(doc.object(fixture.state),4);active.slot=4;
    doc.objects.at(doc.references.at(fixture.model).value_index).fields.push_back(active);
    for(const auto ref:fixture.model_rams) {
        auto& ram=doc.objects.at(doc.references.at(ref).value_index);
        for(auto slot:{2u,6u,7u,8u,9u}) {auto value=zero;value.slot=static_cast<std::uint16_t>(slot);ram.fields.push_back(value);}
        active.slot=5;ram.fields.push_back(active);
    }
    return doc;
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
struct Fixture {
    pf::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(document(source),"synthetic/wait.igb",42);
    ActionEventRuntime events;
    std::shared_ptr<ModelSceneInfo> parent=scene->models().info(source.model);
    std::shared_ptr<ModelSceneRam> ram=scene->models().ram(source.model_rams[0]);
    std::shared_ptr<ModelSceneOne> one=find_model_one(*ram);
    ControllerObjectId id=events.register_model_scene_ram(scene,source.model,source.model_rams[0],0);
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    NativeMessenger& messenger=events.messenger();
    Fixture() {
        one->work=work;one->node.reset();ram->active=1;work->sequence.wait.control_enabled=0;
        events.set_model_context(std::make_shared<ModelSceneContext>(ModelSceneContext{42}));
        messenger.select("MODELCTRL",parent->name,0);
    }
    std::shared_ptr<ModelSceneSequence> data(std::initializer_list<std::pair<const char*,const char*>> commands) {
        auto seq=std::make_shared<ModelSceneSequence>();seq->commands=std::make_shared<ModelSceneList>("igNamedObjectList");
        for(const auto& [type,name]:commands) {auto command=std::make_shared<ModelSceneCommand>(type);command->name=name;seq->commands->append(command);}
        return seq;
    }
    void resume_data(const std::shared_ptr<ModelPlayerWork>& captured,const char* next="next") {
        captured->sequence.command_data=data({{"beModelCtrlWCHECK",next}});captured->sequence.command_index=0;
    }
    void provider(std::optional<std::string> name,std::int32_t index,Hook hook) {
        events.register_native_controller(ControllerObjectId::allocate_native(),{
            []()->std::optional<std::uint8_t>{return std::uint8_t{1};},[hook=std::move(hook)]{return MessengerManager{hook,{}};}},
            "ACTION",std::move(name),index);
    }
    void pads() {
        auto manager=std::make_shared<ModelPadManager>();manager->data=std::make_shared<ModelPadList>();
        manager->data->backing=std::make_shared<ModelPadList::Storage>();
        for(unsigned i=0;i<5;++i)manager->data->backing->push_back(std::make_shared<ModelPadState>());
        // Physical pad selection ignores these logical fields.
        manager->data->count=0;manager->data->capacity=0;events.model_manager()->pads=manager;
    }
};
std::shared_ptr<MotionState> motion(bool active) {
    auto value=std::make_shared<MotionState>();value->active=active;
    value->mode=static_cast<MotionMode>(999);value->speed=std::numeric_limits<float>::quiet_NaN();return value;
}
void narrow_activity() {
    rejects([]{model_wait_motion_active({},ModelWaitMotion::moving);});
    auto empty=std::make_shared<MotionChannelList>();
    for(const auto mode:{ModelWaitMotion::moving,ModelWaitMotion::rotation,ModelWaitMotion::vertical})
        check(!model_wait_motion_active(empty,mode),"Empty original movement list did not skip all entry reads.");
    const auto yes=motion(true),no=motion(false);
    auto first=std::make_shared<MotionChannelList>(MotionChannelList::Entries{yes,{},{},{}});
    check(model_wait_motion_active(first,ModelWaitMotion::moving),"Moving did not short-circuit its first active entry.");
    auto second=std::make_shared<MotionChannelList>(MotionChannelList::Entries{{},yes});
    check(model_wait_motion_active(second,ModelWaitMotion::vertical),"Vertical wait read unused/null entries or numeric fields.");
    rejects([&]{model_wait_motion_active(second,ModelWaitMotion::moving);});
    auto third=std::make_shared<MotionChannelList>(MotionChannelList::Entries{no,yes,{}});
    check(model_wait_motion_active(third,ModelWaitMotion::moving),"Moving read the unused third entry after active Y.");
    auto alias=std::make_shared<MotionChannelList>(MotionChannelList::Entries{no,no,yes,{}});
    check(model_wait_motion_active(alias,ModelWaitMotion::moving),"Actual activity rejected repeated physical entry owners.");
    auto rotation=std::make_shared<MotionChannelList>(MotionChannelList::Entries{{},{},{},yes});
    check(model_wait_motion_active(rotation,ModelWaitMotion::rotation),"Rotation validated unrelated entries.");
    rejects([&]{model_wait_motion_active(second,ModelWaitMotion::rotation);});
    auto short_list=std::make_shared<MotionChannelList>(MotionChannelList::Entries{no});
    rejects([&]{model_wait_motion_active(short_list,ModelWaitMotion::moving);});
}
void common_motion_and_resume() {
    for(const auto* name:{"TurnY","Landing","Moving","Rotation"}) {
        Fixture f;f.work->sequence.wait.name=name;
        const auto yes=motion(true);f.work->control.movement=std::make_shared<MotionChannelList>(MotionChannelList::Entries{yes,yes,yes,yes});
        f.ram->data.reset();ModelCommonWait wait(f.ram,{},f.work);
        check(wait.resume(f.messenger,f.events).completed && f.work->sequence.wait.name==name && !f.ram->data,
            "Active common motion wait read RAM/ONE, timer, parent or command data.");
    }
    Fixture f;f.work->sequence.name="SQ_WAIT";f.work->sequence.wait.name="Moving";f.resume_data(f.work,"after motion");
    f.work->sequence.label_count=9;f.work->control.mode=7;f.work->control.phase=3;f.work->control.bullet_hit_script="keep";
    ModelSubcommandQueue queue(f.work->subcommands);queue.append_sound(10,"keep");const auto wrapper=f.work->subcommands;
    ModelCommonWait wait(f.ram,{},f.work);
    check(wait.resume(f.messenger,f.events).completed && f.work->sequence.wait.name=="after motion" && f.work->sequence.command_index==1,
        "Inactive common motion wait did not resume its captured C3AC cursor.");
    check(f.work->sequence.label_count==9 && f.work->control.mode==7 && f.work->control.phase==3 &&
        f.work->control.bullet_hit_script=="keep" && f.work->subcommands==wrapper && queue.entries().size()==1,
        "Wait command resume repeated entry cleanup/resets or player +B8.");
    check(wait.resume(f.messenger,f.events).completed && queue.entries().size()==1,"Completed wait resumed commands twice.");
    Fixture bad;bad.work->sequence.wait.name.reset();bad.work->control.movement.reset();ModelCommonWait direct(bad.ram,{},bad.work);
    check(rejects([&]{direct.resume(bad.messenger,bad.events);}).find("wait name")!=std::string::npos && direct.faulted(),
        "Direct common helper treated null caller wait as an unknown name or motion query.");
}
void actual_queries() {
    Fixture f;f.work->sequence.wait.name="Action";f.work->control.movement.reset();f.ram->number=5;
    const auto original_name=f.parent->name;ModelCommonWait wait(f.ram,f.parent,f.work);
    check(!wait.resume(f.messenger,f.events).completed,"Missing ACTION registration completed a required query.");
    f.parent->name="changed";f.ram->number=8;unsigned calls{};bool ready{};
    std::shared_ptr<const MessengerBuffer> packet;
    f.provider(original_name,5,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++calls;check(delivery.message.name==original_name && delivery.message.index==5 && delivery.message.command=="ISCOMEND",
            "Suspended actual query rebuilt its consumed parent-name/RAM-number arguments.");
        if(!packet) {packet=live.buffer_owner();live.buffer().add_int(73);}
        check(live.buffer_owner()==packet && live.buffer().integers()==std::vector<std::int32_t>{73},
            "Actual ACTION retry selected a new packet.");
        if(!ready)return MessengerCallResult{false,0,"query unavailable"};
        f.work->sequence.wait.name="callback";live.buffer().add_bool(0);return MessengerCallResult{true,1,{}};
    });
    check(!wait.resume(f.messenger,f.events).completed && calls==1,"Partial ACTION query was treated as a real return.");
    ready=true;check(wait.resume(f.messenger,f.events).completed && calls==2 && f.work->sequence.wait.name=="callback",
        "A false ISCOMEND result cleared a callback-produced live wait.");
    check(f.messenger.buffer_depth()==0,"Actual ACTION wait invented a packet push/pop.");

    Fixture yes;yes.work->control.movement.reset();yes.work->sequence.wait.name="Action";yes.parent->name.reset();yes.resume_data(yes.work,"continued");
    yes.provider("",0,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        check(delivery.message.name=="","Null captured parent name did not use the original empty fallback.");
        yes.one->work=make_model_player_work();yes.work->sequence.wait.name="replace before clear";
        live.buffer().add_bool(255);return MessengerCallResult{true,1,{}};
    });
    ModelCommonWait completed(yes.ram,yes.parent,yes.work);
    check(completed.resume(yes.messenger,yes.events).completed && completed.captured_work()==yes.work &&
        yes.work->sequence.wait.name=="continued" && !yes.one->work->sequence.command_data,
        "Successful common query resumed a replacement ONE.work or rejected a nonzero raw boolean.");

    Fixture absent;absent.work->sequence.wait.name="Action";
    absent.provider(absent.parent->name,0,[](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    ModelCommonWait no_boolean(absent.ram,absent.parent,absent.work);
    rejects([&]{no_boolean.resume(absent.messenger,absent.events);});
    check(no_boolean.faulted() && absent.work->sequence.wait.name=="Action","Absent boolean slot fabricated false or cleared the wait.");
    Fixture parentless;parentless.work->sequence.wait.name="Action";ModelCommonWait null_parent(parentless.ram,{},parentless.work);
    rejects([&]{null_parent.resume(parentless.messenger,parentless.events);});
}
void independent_fallback_and_live_mode() {
    Fixture f;f.work->sequence.wait.name="unhandled";f.work->control.movement.reset();
    ModelCommonWait common(f.ram,f.parent,f.work);
    const auto parent=std::make_shared<ModelSceneInfo>();parent->name="fresh parent";
    const auto next=make_model_player_work();next->sequence.wait={"Atck01",0,0,7};f.resume_data(next,"old captured tail");
    const auto one=std::make_shared<ModelSceneOne>();one->name="ONE";one->work=next;
    f.ram->parent=parent;f.ram->data=std::make_shared<ModelSceneList>();f.ram->data->append(one);f.ram->number=3;
    bool ready{};unsigned queries{};const auto replacement=make_model_player_work();
    f.provider("fresh parent",3,[&](NativeMessenger& live,const MessengerDelivery&) {
        ++queries;check(next->sequence.wait.attack_state==1 && next->sequence.wait.attack_mode==1,
            "Atck01 reset/latch prefix did not precede ACTION query.");
        one->work=replacement;f.ram->parent.reset();f.ram->number=8;
        if(!ready)return MessengerCallResult{false,0,"retained query"};
        next->sequence.wait.attack_mode=0;next->sequence.wait.name="changed";live.buffer().add_bool(1);
        return MessengerCallResult{true,1,{}};
    });
    check(!common.resume(f.messenger,f.events).completed && common.player_fallback()->captured_parent()==parent &&
        common.player_fallback()->captured_work()==next && common.captured_work()==f.work,
        "Common and player fallback reused one parent/work capture.");
    ready=true;check(common.resume(f.messenger,f.events).completed && queries==2 && next->sequence.wait.attack_state==0 &&
        next->sequence.wait.attack_mode==0 && next->sequence.wait.name=="old captured tail" && f.work->sequence.wait.name=="unhandled" &&
        !replacement->sequence.command_data,"Atck01 ignored callback-mutated mode or resumed a newly installed work.");
}
void actual_pad_sites() {
    Fixture disabled;disabled.work->sequence.wait={"unknown",0,-7,8};disabled.ram->parent.reset();
    ModelPlayerWait no_pad(disabled.ram);
    check(no_pad.resume(disabled.messenger,disabled.events).completed && !no_pad.captured_parent(),
        "Disabled unknown player wait consumed missing pad or parent-name owners.");
    Fixture enabled;enabled.work->sequence.wait={std::nullopt,1,0,0};ModelPlayerWait null_pad(enabled.ram);
    check(rejects([&]{null_pad.resume(enabled.messenger,enabled.events);}).find("pad")!=std::string::npos,
        "Player wait compared its null name before the enabled pad lookup.");
    Fixture named;named.pads();named.work->sequence.wait={std::nullopt,1,0,0};ModelPlayerWait null_name(named.ram);
    check(rejects([&]{null_name.resume(named.messenger,named.events);}).find("wait name")!=std::string::npos,
        "Player wait did not reach the null name after valid enabled pad input.");
    for(const auto [number,redirect]:{std::pair{3,0},std::pair{9,0},std::pair{-1,1}}) {
        Fixture f;f.pads();f.ram->number=number;f.work->sequence.wait={"Atck03",255,0,0};
        const auto pads=f.events.model_manager()->pads;pads->redirect=static_cast<std::uint8_t>(redirect);
        (*pads->data->backing)[number==3?3u:4u]->held=pad_button::a;
        ModelPlayerWait wait(f.ram);
        check(wait.resume(f.messenger,f.events).completed && f.work->sequence.wait.name=="Atck03" && !f.work->sequence.command_data,
            "Player fallback used cached registration index or ignored the physical fifth pad.");
    }
    Fixture negative;negative.pads();negative.ram->number=-1;negative.work->sequence.wait={"unknown",1,0,0};
    ModelPlayerWait bad(negative.ram);rejects([&]{bad.resume(negative.messenger,negative.events);});
    Fixture physical;physical.pads();physical.work->sequence.wait={"unknown",1,0,0};
    (*physical.events.model_manager()->pads->data->backing)[0].reset();ModelPlayerWait null_slot(physical.ram);
    check(rejects([&]{null_slot.resume(physical.messenger,physical.events);}).find("physical pad")!=std::string::npos,
        "A null addressable pad slot was replaced with empty input.");
    Fixture missing;missing.ram->data.reset();ModelPlayerWait no_one(missing.ram);
    rejects([&]{no_one.resume(missing.messenger,missing.events);});
    check(missing.ram->data && no_one.captured_parent()==missing.parent && !no_one.captured_work(),
        "Player wait lost the parent capture or RAM-data ensure prefix before missing ONE.");
    Fixture released;released.work->sequence.wait={"Atck03",0,7,9};released.ram->parent.reset();released.resume_data(released.work,"released");
    ModelPlayerWait immediate(released.ram);
    check(immediate.resume(released.messenger,released.events).completed && released.work->sequence.wait.name=="released" &&
        released.work->sequence.wait.attack_state==7 && released.work->sequence.wait.attack_mode==9,
        "Atck03 release read a parent/ACTION service or changed unrelated attack fields.");
    for(const auto state:{-1,2,17}) {
        Fixture f;f.work->sequence.wait={"Atck01",0,state,9};ModelPlayerWait wait(f.ram);
        check(wait.resume(f.messenger,f.events).completed && f.work->sequence.wait.attack_state==state &&
            f.work->sequence.wait.attack_mode==9,"Invalid Atck01 state queried ACTION or changed its state fields.");
    }
}
void nested_sequence_and_old_command_resume() {
    Fixture f;find_model_seq(*f.parent)->sequences=std::make_shared<ModelSceneList>("igNamedObjectList");
    const auto idle=f.data({{"beModelCtrlACTION","NESTED"}});idle->name="SQ_WAIT";find_model_seq(*f.parent)->sequences->append(idle);
    const auto original=f.data({{"beModelCtrlCTRL","isLive"},{"beModelCtrlWCHECK","old tail"}});
    f.work->sequence.wait={"Atck02",0,7,9};f.work->sequence.command_data=original;
    const auto fresh=make_model_player_work();fresh->sequence.name="OTHER";fresh->sequence.wait.name="new work wait";
    unsigned nested{},queries{};bool ready{};
    f.provider(f.parent->name,0,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        check(delivery.message.command=="NESTED","Atck02 did not enter real nested SQ_WAIT commands.");
        ++nested;f.one->work=fresh;f.work->sequence.command_data=original;f.work->sequence.wait.name="nested wait";
        f.parent->name="after nested";f.ram->number=2;return MessengerCallResult{true,1,{}};
    });
    f.provider("after nested",2,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++queries;check(delivery.message.command=="ISCOMEND" && f.one->work==fresh && nested==1,
            "Atck02 query did not follow its completed nested controller invocation.");
        if(!ready)return MessengerCallResult{false,0,"query after nested"};
        live.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
    });
    ModelPlayerWait wait(f.ram);
    check(!wait.resume(f.messenger,f.events).completed && nested==1 && f.work->sequence.command_index==1 &&
        f.work->sequence.wait.name=="nested wait","Atck02 skipped/replayed its real nested sequence before querying.");
    ready=true;check(wait.resume(f.messenger,f.events).completed && nested==1 && queries==2 &&
        f.work->sequence.command_index==2 && f.work->sequence.wait.name=="old tail" && fresh->sequence.wait.name=="new work wait" &&
        wait.captured_work()==f.work,"Atck02 resumed newly installed work or repeated nested entry while its query was pending.");

    Fixture icon;find_model_seq(*icon.parent)->sequences=std::make_shared<ModelSceneList>("igNamedObjectList");
    auto empty=icon.data({});empty->name="SQ_WAIT";find_model_seq(*icon.parent)->sequences->append(empty);
    icon.work->sequence.wait={"Atck01",0,0,0};
    icon.provider(icon.parent->name,0,[](NativeMessenger& live,const MessengerDelivery&){live.buffer().add_bool(1);return MessengerCallResult{true,1,{}};});
    ModelPlayerWait release(icon.ram);check(release.resume(icon.messenger,icon.events).completed &&
        icon.work->sequence.name=="SQ_WAIT" && icon.work->subcommands->list_owner()->size()==1,
        "Atck01 release bypassed the real ordinary entry and required player +B8 effect.");

    Fixture no;find_model_seq(*no.parent)->sequences=std::make_shared<ModelSceneList>("igNamedObjectList");
    const auto idle_wait=no.data({{"beModelCtrlWCHECK","nested idle"}});idle_wait->name="SQ_WAIT";
    find_model_seq(*no.parent)->sequences->append(idle_wait);no.work->sequence.wait={"Atck02",0,0,0};
    no.provider(no.parent->name,0,[&](NativeMessenger& live,const MessengerDelivery&) {
        check(no.work->sequence.wait.name=="nested idle","Atck02 query preceded its actual SQ_WAIT wait publication.");
        no.work->sequence.wait.name="false callback";live.buffer().add_bool(0);return MessengerCallResult{true,1,{}};
    });
    ModelPlayerWait unfinished(no.ram);
    check(unfinished.resume(no.messenger,no.events).completed && no.work->sequence.wait.name=="false callback" &&
        no.work->sequence.command_index==1 && no.work->subcommands->list_owner()->size()==1,
        "False Atck02 query discarded nested/callback wait state or resumed its command cursor.");
}
void faults_and_abandonment() {
    Fixture f;f.work->sequence.wait.name="Action";ModelCommonWait wait(f.ram,f.parent,f.work);unsigned calls{};
    f.provider(f.parent->name,0,[&](NativeMessenger& live,const MessengerDelivery&) {
        ++calls;rejects([&]{wait.resume(live,f.events);});live.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
    });
    rejects([&]{wait.resume(f.messenger,f.events);});
    check(wait.faulted() && calls==1 && f.work->sequence.wait.name=="Action","Caught recursive wait failure was consumed as successful query.");
    rejects([&]{wait.resume(f.messenger,f.events);});check(calls==1,"Faulted wait replayed its query.");

    Fixture pending;pending.work->sequence.wait.name="Moving";
    pending.work->sequence.command_data=pending.data({{"beModelCtrlACTION","MISSING"}});
    auto first=std::make_unique<ModelCommonWait>(pending.ram,pending.parent,pending.work);
    check(!first->resume(pending.messenger,pending.events).completed && !pending.work->sequence.wait.name,
        "Missing command service lost the already cleared wait prefix.");
    first.reset();auto resumed=pending.events.begin_model_sequence_resume(pending.ram,pending.parent,pending.work);
    rejects([&]{resumed->resume(pending.events);});

    Fixture held;held.work->sequence.wait.name="Action";
    held.provider(held.parent->name,0,[](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{false,0,"held"};});
    auto query=std::make_unique<ModelCommonWait>(held.ram,held.parent,held.work);
    check(!query->resume(held.messenger,held.events).completed,"Query abandonment fixture did not suspend.");
    query.reset();ModelCommonWait replacement(held.ram,held.parent,held.work);
    rejects([&]{replacement.resume(held.messenger,held.events);});
}
}
int main() {
    try {narrow_activity();common_motion_and_resume();actual_queries();independent_fallback_and_live_mode();actual_pad_sites();
        nested_sequence_and_old_command_resume();faults_and_abandonment();std::cout<<"Actual wait checks: "<<checks<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<"Actual wait after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
