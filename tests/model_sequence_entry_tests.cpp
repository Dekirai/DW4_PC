#include "dw4/action_event_runtime.hpp"
#include "dw4/model_player_sequence.hpp"
#include "dw4/model_timer.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F call,const char* reason) {
    ++checks;try{call();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
IgbDocument document(const pf::RuntimeFixture& fixture) {
    auto doc=fixture.document();
    const auto zero=*doc.field(doc.object(fixture.ones[0]),3);
    auto active=*doc.field(doc.object(fixture.state),4);active.slot=4;
    doc.objects.at(doc.references.at(fixture.model).value_index).fields.push_back(active);
    for(const auto ref:fixture.model_rams) {
        auto& ram=doc.objects.at(doc.references.at(ref).value_index);
        for(auto slot:{2u,6u,7u,8u,9u}) {auto value=zero;value.slot=static_cast<std::uint16_t>(slot);ram.fields.push_back(value);}
        active.slot=5;ram.fields.push_back(active);
    }
    return doc;
}
struct Fixture {
    pf::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(document(source),"synthetic/entry.igb",12);
    ActionEventRuntime events;
    std::shared_ptr<ModelSceneInfo> info=scene->models().info(source.model);
    std::shared_ptr<ModelSceneRam> ram=scene->models().ram(source.model_rams[0]);
    std::shared_ptr<ModelSceneOne> one=find_model_one(*ram);
    ControllerObjectId id=events.register_model_scene_ram(scene,source.model,source.model_rams[0],0);
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    Fixture() {
        one->work=work;one->node.reset();ram->active=1;
        events.set_model_context(std::make_shared<ModelSceneContext>(ModelSceneContext{42}));
        events.messenger().select("MODELCTRL",info->name,0);
    }
    std::shared_ptr<ModelSceneSequence> sequence(const char* name,std::initializer_list<std::pair<const char*,const char*>> commands) {
        auto seq=std::make_shared<ModelSceneSequence>();seq->name=name;
        seq->commands=std::make_shared<ModelSceneList>("igNamedObjectList");
        for(const auto& [type,label]:commands) {auto command=std::make_shared<ModelSceneCommand>(type);command->name=label;seq->commands->append(command);}
        find_model_seq(*info)->sequences->append(seq);return seq;
    }
};
void work_only_entry_and_idle_callback() {
    Fixture f;const auto sequence=f.sequence("TEST",{{"beModelCtrlCTRL","isLive"},{"beModelCtrlWCHECK","Action"}});
    ModelBaseInvocation first(f.ram,"TEST");
    check(first.resume(f.events.messenger(),f.events).completed && first.sequence_entry()->completed() && first.player_sequence()->completed(),
        "A real command-loop return did not reach and complete player +B8.");
    check(f.work->control.mode==4 && f.work->sequence.wait.name=="Action" && f.work->sequence.command_index==2 &&
        f.work->sequence.command_data.actual_owner()==sequence && !f.one->node && !find_model_timer(*f.ram,"KEIKATIMER"),
        "Work-only sequence entry read an unrelated node/timer or lost actual selected data/cursor.");
    ModelBaseInvocation second(f.ram,"TEST");
    check(second.resume(f.events.messenger(),f.events).completed && second.captured_work()==f.work,
        "A second entry could not reuse its retained work executor.");
    // Use an empty real idle sequence to observe the original +B8 self-send.
    auto seq=find_model_seq(*f.info);seq->sequences=std::make_shared<ModelSceneList>("igNamedObjectList");
    f.sequence("SQ_WAIT",{});
    ModelBaseInvocation idle(f.ram,"SQ_WAIT");
    check(idle.resume(f.events.messenger(),f.events).completed && idle.player_sequence()->captured_work()==f.work,
        "Original idle entry did not run its fresh player callback.");
    ModelSubcommandQueue queue(f.work->subcommands);
    const auto& effect=std::get<ModelEffectSubcommandState>(queue.entries().back().value);
    check(queue.entries().size()==1 && effect.command.model_name=="i_pcnumic" && effect.command.sequence_name=="SQ_OPENP0" &&
        effect.command.delay==480 && effect.command.offset==std::array<float,3>{0,10,0},
        "Idle player +B8 did not append its actual delayed MODELCTRL effect.");
}
void retained_action_and_new_callback_work() {
    Fixture f;f.sequence("TEST",{{"beModelCtrlCTRL","onWalk"},{"beModelCtrlACTION","IDLE"},
        {"beModelCtrlCTRL","isLive"},{"beModelCtrlWCHECK","Done"}});
    auto& messenger=f.events.messenger();
    bool ready{};unsigned completed{};
    const auto selected_name=f.info->name;
    const auto recipient=ControllerObjectId::allocate_native();
    MessengerManager manager{[&](NativeMessenger&,const MessengerDelivery& delivery) {
        check(delivery.message.name==selected_name && delivery.message.index==0 && delivery.message.command=="IDLE",
            "Suspended ACTION delivery re-read changed source identity.");
        if(!ready)return MessengerCallResult{false,0,"synthetic action boundary"};
        ++completed;return MessengerCallResult{true,1,{}};
    },{}};
    f.events.register_native_controller(recipient,{[]{return std::optional<std::uint8_t>{1};},[manager]{return manager;}},
        "ACTION",selected_name,0);
    ModelBaseInvocation call(f.ram,"TEST");
    check(!call.resume(messenger,f.events).completed && f.work->control.mode==1 && !call.player_sequence(),
        "Suspended ACTION replayed or skipped the original command/player-callback boundary.");
    const auto fresh=make_model_player_work();fresh->sequence.name="NESTED";f.one->work=fresh;
    f.info->name="renamed";f.ram->number=3;f.work->sequence.label_count=17;ready=true;
    check(call.resume(messenger,f.events).completed && completed==1 && f.work->control.mode==4 &&
        f.work->sequence.label_count==17 && f.work->sequence.command_index==4,
        "Resuming ACTION repeated entry resets or redirected the captured work generation.");
    check(call.captured_work()==f.work && call.player_sequence()->captured_work()==fresh &&
        call.player_sequence()->reconciliation_work()==fresh && fresh->sequence.name=="NESTED",
        "Player +B8 failed to independently resolve the replacement work after the old command loop.");
}
void nested_jump_runs_both_player_callbacks() {
    Fixture f;find_model_seq(*f.info)->sequences=std::make_shared<ModelSceneList>("igNamedObjectList");
    const auto inner=f.sequence("SQ_WAIT",{});
    f.sequence("OUTER",{{"beModelCtrlJUMP","SQ_WAIT"},{"beModelCtrlCTRL","isLive"}});
    ModelBaseInvocation call(f.ram,"OUTER");
    check(call.resume(f.events.messenger(),f.events).completed && call.player_sequence()->completed(),
        "A same-work direct JUMP did not return through ordinary player callbacks.");
    ModelSubcommandQueue queue(f.work->subcommands);
    check(f.work->sequence.command_data.actual_owner()==inner && f.work->sequence.name=="SQ_WAIT" &&
        f.work->sequence.command_index==0 && f.work->control.mode==0 && queue.entries().size()==2,
        "Nested entry lost its data/cursor, executed the old tail or skipped either original +B8 callback.");
}
void retained_manager_after_facade_destruction() {
    auto fixture=std::make_unique<Fixture>();fixture->sequence("TEST",{{"beModelCtrlACTION","UNAVAILABLE"}});
    auto& messenger=fixture->events.messenger();auto dispatch=messenger.begin_send("TEST");
    check(fixture->events.resume(dispatch).stop==MessengerStop::service_required,"Lifetime fixture did not retain an actual manager callback.");
    const auto lease=fixture->ram->manager.lock();
    fixture.reset();
    check(lease && messenger.resume(dispatch,{}).stop==MessengerStop::service_required,
        "Retained manager callback used its destroyed event facade.");
}
void cleanup_retains_its_captured_wrapper() {
    Fixture f;f.sequence("CLEAN",{{"beModelCtrlCTRL","isLive"}});
    ModelSubcommandQueue old(f.work->subcommands);old.append_sound(0,"sound");
    ModelSubcommandServices start;start.play_sound=[](const auto&){return ModelSoundPlayResult{true,71,4};};
    check(old.update(1,{{"source",0},{}},start).complete(),"Synthetic sound setup did not reach a cleanup handle.");
    auto services=std::make_shared<ModelSequenceEntryServices>();bool ready{};unsigned downs{};
    services->subcommands.sound_volume_down=[&](std::uint64_t handle) {check(handle==71,"Cleanup changed its captured sound.");if(!ready)return false;++downs;return true;};
    f.events.set_model_sequence_services(services);f.work->control.bullet_hit_script="old";
    ModelBaseInvocation call(f.ram,"CLEAN");
    check(!call.resume(f.events.messenger(),f.events).completed && f.work->sequence.name=="CLEAN" &&
        f.work->control.mode==0 && f.work->control.bullet_hit_script=="old", "Cleanup did not retain the exact pre/post-entry split.");
    const auto next=std::make_shared<ModelSubcommandWrapper>();f.work->subcommands=next;
    ModelSubcommandQueue fresh(next);fresh.append_sound(10,"later");ready=true;
    check(call.resume(f.events.messenger(),f.events).completed && old.entries().empty() && fresh.entries().size()==1 && downs==1 &&
        !f.work->control.bullet_hit_script && f.work->control.mode==4,"Resumed cleanup targeted the replacement E4 wrapper or repeated its prefix.");
}
void actual_clock_and_fault_prefixes() {
    Fixture f;ModelRamTimerAccess timer(f.ram);auto record=timer.start("KEIKATIMER");
    check(record->timer.saved_world_time==42,"Actual timer did not read its current manager context.");
    const auto second=std::make_shared<ModelSceneContext>(ModelSceneContext{77});f.events.set_model_context(second);
    timer.start("KEIKATIMER");check(record->timer.saved_world_time==77,"Timer retained the old manager context reference.");
    const auto manager=std::make_shared<ModelSceneManager>();manager->context=std::make_shared<ModelSceneContext>(ModelSceneContext{99});
    f.ram->manager=manager;timer.start("KEIKATIMER");check(record->timer.saved_world_time==99,"Timer retained its original manager instead of the current backlink.");
    manager->context.reset();rejects([&]{timer.start("NEW");},"A null actual manager context was replaced with a fabricated clock.");
    check(find_model_timer(*f.ram,"NEW") && timer.faulted(),"Missing context rolled back preceding named timer allocation.");
    Fixture missing;ModelBaseInvocation call(missing.ram,"ABSENT");
    rejects([&]{call.resume(missing.events.messenger(),missing.events);},"A missing sequence command-data dereference was treated as completed gameplay.");
    check(missing.work->sequence.name=="ABSENT" && !missing.work->sequence.command_data && call.faulted() && !call.player_sequence(),
        "Missing sequence failure discarded original name/data publication or ran player +B8.");
}
}
int main() {
    try {work_only_entry_and_idle_callback();retained_action_and_new_callback_work();nested_jump_runs_both_player_callbacks();
        cleanup_retains_its_captured_wrapper();actual_clock_and_fault_prefixes();retained_manager_after_facade_destruction();
        std::cout<<"Actual model sequence entry checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Actual sequence entry after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
