#include "dw4/action_event_runtime.hpp"
#include "dw4/model_frame.hpp"
#include "dw4/model_wait_actual.hpp"
#include "dw4/model_subcommand_update.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
IgbDocument document(const pf::RuntimeFixture& fixture) {
    auto doc=fixture.document();const auto zero=*doc.field(doc.object(fixture.ones[0]),3);
    auto active=*doc.field(doc.object(fixture.state),4);active.slot=4;
    doc.objects.at(doc.references.at(fixture.model).value_index).fields.push_back(active);
    for(const auto ref:fixture.model_rams) {
        auto& ram=doc.objects.at(doc.references.at(ref).value_index);
        for(auto slot:{2u,6u,7u,8u,9u}) {auto field=zero;field.slot=static_cast<std::uint16_t>(slot);ram.fields.push_back(field);}
        active.slot=5;ram.fields.push_back(active);
    }
    return doc;
}
struct Fixture {
    pf::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(document(source),"synthetic/frame.igb",19);
    ActionEventRuntime events;
    ControllerObjectId id=events.register_model_scene_ram(scene,source.model,source.model_rams[0],0);
    std::shared_ptr<ModelSceneRam> ram=events.model_ram(id);
    std::shared_ptr<ModelSceneInfo> info=ram->parent.lock();
    std::shared_ptr<ModelSceneOne> one=find_model_one(*ram);
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    std::shared_ptr<ModelFrameServices> services=std::make_shared<ModelFrameServices>();
    Fixture() {
        one->work=work;ram->active=1;work->sequence.name="SQ_WAIT";work->script.original_is_main_lobby=0;
        work->turn_attack_timers=std::make_shared<std::vector<std::int32_t>>(8,0);
        const auto pads=std::make_shared<ModelPadManager>();pads->data=std::make_shared<ModelPadList>();
        pads->data->backing=std::make_shared<ModelPadList::Storage>(5);
        for(auto& pad:*pads->data->backing)pad=std::make_shared<ModelPadState>();
        events.model_manager()->pads=pads;
        events.model_manager()->system=std::make_shared<ModelSceneSystem>(ModelSceneSystem{1});
        services->subcommands=std::make_shared<ModelSubcommandUpdateServices>();
        events.set_model_context(std::make_shared<ModelSceneContext>(ModelSceneContext{100}));
        events.set_model_frame_services(services);
    }
    std::shared_ptr<ModelSceneSequence> sequence(const char* name,
        std::initializer_list<std::pair<const char*,const char*>> commands) {
        auto data=std::make_shared<ModelSceneSequence>();data->name=name;
        data->commands=std::make_shared<ModelSceneList>("igNamedObjectList");
        for(const auto& [type,text]:commands) {
            auto command=std::make_shared<ModelSceneCommand>(type);command->name=text;data->commands->append(command);
        }
        find_model_seq(*info)->sequences->append(data);return data;
    }
    void pending_sound() {
        ModelSubcommandQueue queue(work->subcommands);queue.append_sound(0,"synthetic-frame-sound");
    }
    void complete_sound() {
        services->subcommands->play_sound=[](const ModelSubcommandUpdateScope&,const auto&) {
            // Explicit synthetic SOUND failure reply; no native audio is claimed.
            return ModelSoundPlayResult{true,0,4};
        };
    }
};
void original_capture_and_early_gates() {
    Fixture inactive;inactive.ram->active=0;inactive.one->work.reset();
    auto stopped=inactive.events.begin_model_frame(inactive.id);
    check(inactive.events.resume(*stopped).completed && stopped->captured().one==inactive.one &&
        !stopped->captured().work && !stopped->status(),"Inactive frame did not load ONE/work before returning without a null-work dereference.");
    Fixture missing;missing.ram->active=0;missing.ram->data.reset();auto invalid=missing.events.begin_model_frame(missing.id);
    rejects([&]{missing.events.resume(*invalid);},"Inactive frame skipped its reached missing ONE dereference.");
    check(missing.ram->data && invalid->captured().parent==missing.info && invalid->faulted(),
        "Missing-ONE failure lost the earlier parent capture/data allocation prefix.");
    Fixture frozen;frozen.work->frozen=255;frozen.work->sequence.name.reset();frozen.work->key_input=9;
    frozen.work->key_event_name="unchanged";auto frozen_frame=frozen.events.begin_model_frame(frozen.id);
    check(frozen.events.resume(*frozen_frame).completed && frozen.work->key_input==9 && frozen.work->key_event_name=="unchanged",
        "Frozen frame read/reset sequence or input fields after its nonzero gate.");
    Fixture unnamed;unnamed.work->sequence.name.reset();unnamed.work->key_input=9;
    auto unnamed_frame=unnamed.events.begin_model_frame(unnamed.id);
    rejects([&]{unnamed.events.resume(*unnamed_frame);},"Active frame silently repaired its null sequence name.");
    check(unnamed.work->key_input==9,"A failed sequence comparison executed later input resets.");
}
void prefix_and_retained_helper_boundaries() {
    Fixture empty;empty.events.model_manager()->system.reset();auto empty_frame=empty.events.begin_model_frame(empty.id);
    check(empty.events.resume(*empty_frame).completed && empty_frame->subcommands()->completed(),
        "An empty actual wrapper queried unused system time or required a fake helper provider.");
    Fixture f;f.work->key_input=7;f.work->key_event_name="old";f.work->ai=std::make_shared<ModelWorkAi>();
    f.pending_sound();
    auto record=std::make_shared<ModelSceneData>();record->name="AITIMER";f.ram->data->append(record);
    auto frame=f.events.begin_model_frame(f.id);const auto first=f.events.resume(*frame);
    check(!first.completed && first.diagnostic.find("subcommand")!=std::string::npos && frame->status()->completed() &&
        f.work->key_input==0 && f.work->key_event_name=="SQ_NONE" && !find_model_named(f.ram->data,"AITIMER"),
        "Actual reset/mode-zero AI removal/status/motion prefix did not reach its real subcommand boundary.");
    f.work->key_input=5;f.complete_sound();
    check(f.events.resume(*frame).completed && f.work->key_input==5 && !frame->input(),
        "Resuming the frame replayed its reset or incorrectly called mode-zero input.");
    Fixture queue;queue.pending_sound();auto pending=queue.events.begin_model_frame(queue.id);
    check(!queue.events.resume(*pending).completed,"A missing subcommand provider became a completed frame.");
    const auto captured=queue.work->subcommands;queue.work->subcommands=std::make_shared<ModelSubcommandWrapper>();
    queue.services->subcommands->play_sound=[&](const ModelSubcommandUpdateScope& scope,const auto&) {
        check(scope.ram==queue.ram && pending->subcommands()->captured_wrapper()==captured,
            "Subcommand retry read the replacement E4 slot instead of its captured wrapper.");
        return ModelSoundPlayResult{true,0,4};
    };
    check(queue.events.resume(*pending).completed,"Retained subcommand provider did not return to the actual frame tail.");
}
void actual_input_and_replacement_scopes() {
    Fixture f;f.work->control.mode=2;f.work->sequence.wait.control_enabled=1;
    const auto pad=model_pad(f.events.model_manager()->pads,0);pad->direction=1;pad->left_y=1;
    const auto replacement=make_model_player_work();replacement->sequence.name="OLD";
    replacement->script.original_is_main_lobby=0;
    f.sequence("NEXT",{{"beModelCtrlCTRL","isLive"}});
    const auto camera=std::make_shared<AnimationMatrix>(AnimationMatrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
    unsigned camera_reads{},inverses{};bool ready{};
    f.services->input.camera_matrix=[&](const ModelSceneManager&) {
        ++camera_reads;f.one->work=replacement;f.work->key_event_name="NEXT";return camera;
    };
    f.services->input.inverse_camera=[&](const AnimationMatrix& value)->std::optional<AnimationMatrix> {
        ++inverses;if(!ready)return {};return value; // Exact inverse of this synthetic identity fixture.
    };
    auto frame=f.events.begin_model_frame(f.id);check(!f.events.resume(*frame).completed && camera_reads==1 &&
        frame->input()->captured_work()==f.work && f.one->work==replacement && !frame->status(),
        "Input suspension failed to retain its work before replacement or ran later status early.");
    const auto history=*f.work->turn_attack_timers;ready=true;
    check(f.events.resume(*frame).completed && camera_reads==1 && inverses==2 && *f.work->turn_attack_timers==history,
        "Resumed input repeated its pad/history/camera prefix.");
    check(frame->captured().work==f.work && frame->status()->captured_work()==replacement &&
        replacement->sequence.name=="NEXT" && replacement->control.mode==4 && f.work->key_event_name=="NEXT" &&
        f.work->sequence.name=="SQ_WAIT",
        "Actual input/control/direct-entry/status did not preserve their independent old/new work captures.");
}
void wait_resume_and_failure_prefixes() {
    Fixture f;f.work->sequence.command_data=f.sequence("COMMANDS",{{"beModelCtrlCTRL","isLive"}});
    f.work->sequence.wait.name="Atck03";f.work->sequence.wait.control_enabled=0;
    f.work->sequence.label_count=8;
    auto frame=f.events.begin_model_frame(f.id);
    check(f.events.resume(*frame).completed && frame->wait() && frame->wait()->completed() &&
        f.work->sequence.command_index==1 && !f.work->sequence.wait.name && f.work->control.mode==4 &&
        f.work->sequence.label_count==8 && f.work->subcommands->list_owner()->empty(),
        "Frame wait did not directly resume C3AC, or repeated ordinary entry/+B8 effects.");
    Fixture recursive;recursive.pending_sound();auto poisoned=recursive.events.begin_model_frame(recursive.id);
    recursive.services->subcommands->play_sound=[&](const ModelSubcommandUpdateScope&,const auto&) {
        try{recursive.events.resume(*poisoned);}catch(const std::exception&){}
        return ModelSoundPlayResult{true,0,4};
    };
    rejects([&]{recursive.events.resume(*poisoned);},"A caught recursive frame failure let the outer frame continue.");
    check(poisoned->faulted() && recursive.work->key_event_name=="SQ_NONE",
        "Recursive frame failure lost permanent fault or completed input-reset prefix.");
}
void frame_exclusivity_and_abandonment() {
    Fixture f;f.pending_sound();auto first=f.events.begin_model_frame(f.id);
    check(!f.events.resume(*first).completed,"Exclusive-frame fixture did not suspend at its missing effect provider.");
    f.work->key_input=9;auto second=f.events.begin_model_frame(f.id);
    rejects([&]{f.events.resume(*second);},"A second frame executed over an unfinished original per-RAM call.");
    check(first->faulted() && second->faulted() && f.work->key_input==9,
        "Competing frame entry did not preserve prior fields and invalidate both uncertain continuations.");
    Fixture abandoned;abandoned.pending_sound();auto pending=abandoned.events.begin_model_frame(abandoned.id);
    check(!abandoned.events.resume(*pending).completed,"Abandonment fixture did not reach its retained provider.");
    pending.reset();auto replay=abandoned.events.begin_model_frame(abandoned.id);
    rejects([&]{abandoned.events.resume(*replay);},"Destroying a suspended frame permitted automatic prefix replay.");
    Fixture sequential;auto done=sequential.events.begin_model_frame(sequential.id);
    check(sequential.events.resume(*done).completed,"Sequential fixture did not finish its first actual frame.");
    auto next=sequential.events.begin_model_frame(sequential.id);
    check(sequential.events.resume(*next).completed,"A completed prior frame blocked the next ordinary manager update.");
}
void pending_frame_excludes_unrelated_execution() {
    Fixture f;f.pending_sound();
    f.work->sequence.command_data=f.sequence("COMMANDS",{{"beModelCtrlCTRL","isLive"}});
    auto unrelated=f.events.begin_model_sequence_resume(f.ram,f.info,f.work);
    auto& messenger=f.events.messenger();messenger.select("MODELCTRL",f.info->name,0);
    auto dispatch=messenger.begin_send("GETHP");
    ModelBaseInvocation base(f.ram,"SQ_WAIT");
    ModelPlayerInputInvocation input(f.ram);
    ModelStatusUpdateInvocation status(f.ram);
    auto frame=f.events.begin_model_frame(f.id);
    check(!f.events.resume(*frame).completed,"Unrelated-execution fixture did not pause at SOUND.");
    rejects([&]{unrelated->resume(f.events);},"A direct command loop overtook a paused actual frame.");
    rejects([&]{f.events.resume(dispatch);},"An unrelated messenger send overtook a paused actual frame.");
    rejects([&]{f.events.messenger();},"Mutable messenger access bypassed the retained actual frame.");
    rejects([&]{base.resume(messenger,f.events,{});},"An unrelated base invocation overtook a paused actual frame.");
    rejects([&]{input.resume(messenger,f.events);},"An unrelated input callback overtook a paused actual frame.");
    rejects([&]{status.resume(messenger,f.events,{});},"An unrelated status callback overtook a paused actual frame.");
    check(f.work->control.mode==0 && f.work->sequence.command_index==0 && !frame->faulted(),
        "Rejected unrelated execution changed guest work or poisoned the still-valid original frame.");
    f.complete_sound();
    check(f.events.resume(*frame).completed,"Supplying the missing service could not finish the owning frame.");
    check(unrelated->resume(f.events).completed && f.work->control.mode==4,
        "A completed owning frame did not release its execution gate for a later command loop.");
}
}
int main() {
    try {
        original_capture_and_early_gates();prefix_and_retained_helper_boundaries();
        actual_input_and_replacement_scopes();wait_resume_and_failure_prefixes();frame_exclusivity_and_abandonment();
        pending_frame_excludes_unrelated_execution();
        std::cout<<"Actual model frame checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Actual frame after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
