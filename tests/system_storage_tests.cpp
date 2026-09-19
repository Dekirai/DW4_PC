#include "dw4/system_storage.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using Op=SystemStorageOperation;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& call,const char* reason) {
    ++checks;try {call();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
const char* label(Op op) {
    switch(op) {
    case Op::set_world_factor:return "factor";
    case Op::set_managers_enabled:return "managers";
    case Op::save_viewer_children:return "save";
    case Op::register_error_infos:return "register";
    case Op::select_error_switch:return "switch";
    case Op::clear_error_switch:return "clear";
    case Op::set_error_switch_bit:return "bit";
    case Op::remove_error_children:return "remove";
    case Op::restore_viewer_children:return "restore";
    }
    throw std::runtime_error("Unknown fixture operation.");
}
// The native packet transport and state machine run here. Viewer, manager
// sweeps and world-factor operations are explicitly authored service seams.
struct Fixture {
    std::shared_ptr<ModelSceneSystem> system=std::make_shared<ModelSceneSystem>();
    std::shared_ptr<SystemStorageState> state=std::make_shared<SystemStorageState>();
    std::shared_ptr<SystemStorageServices> services=std::make_shared<SystemStorageServices>();
    std::shared_ptr<SystemStorageMessenger> binding=std::make_shared<SystemStorageMessenger>();
    std::shared_ptr<SystemStorageViewer> viewer=std::make_shared<SystemStorageViewer>();
    std::shared_ptr<int> selected=std::make_shared<int>(7);
    std::vector<std::string> trace;
    std::int32_t status{};
    std::uint8_t pad_reply{};
    unsigned samples{},lookups{},viewer_lookups{};
    std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)> delivery;
    Fixture() {
        system->context=std::make_shared<ModelSceneContext>();system->context->world_time=100;
        binding->context=std::make_shared<ModelSceneContext>();binding->context->world_time=350;
        binding->messenger=std::make_shared<NativeMessenger>();binding->delivery=std::make_shared<MessengerServices>();
        viewer->scene_info=std::make_shared<int>(1);viewer->renderer=std::make_shared<int>(2);
        services->drive_status=[this] {++samples;return SystemStorageValueResult{true,status,{}};};
        services->messenger=[this](const auto&) {++lookups;return SystemStorageOwnerResult<SystemStorageMessenger>{true,binding,{}};};
        services->viewer=[this](const auto&,bool restore) {
            ++viewer_lookups;trace.emplace_back(restore ? "viewer:restore":"viewer");
            return SystemStorageOwnerResult<SystemStorageViewer>{true,viewer,{}};
        };
        services->operation=[this](const SystemStorageOperationRequest& request) {
            return [this,request] {
                trace.push_back(std::string(label(request.operation))+":"+std::to_string(request.value));
                if(request.operation==Op::clear_error_switch || request.operation==Op::set_error_switch_bit)
                    check(request.target==selected,"Switch operation lost the selected physical owner.");
                return SystemStorageOperationResult{true,request.operation==Op::select_error_switch ? selected:nullptr,{}};
            };
        };
        binding->delivery->active=[](ControllerObjectId){return std::optional<std::uint8_t>{1};};
        binding->delivery->manager=[this](ControllerObjectId) {
            MessengerManager result;
            result.structured=[this](NativeMessenger& messenger,const MessengerDelivery& call) {
                if(delivery)return delivery(messenger,call);
                return ordinary(messenger,call);
            };
            return result;
        };
        binding->messenger->register_recipient({0,1},"STAGECTL",{},-1);
        binding->messenger->register_recipient({0,2},"CAMERA",{},-1);
        binding->messenger->register_recipient({0,3},"SOUND",{},-1);
    }
    MessengerCallResult ordinary(NativeMessenger& messenger,const MessengerDelivery& call) {
        check(!call.message.name && call.message.index==-1,"Storage send lost original wildcard selection.");
        if(call.message.category=="STAGECTL") {
            const auto& values=messenger.buffer().strings();
            check(values.size()==1 && values.front(),"Storage PADINPUT did not supply its sole nullable string argument.");
            const auto argument=*values.front();trace.push_back("PAD:"+argument);
            check(call.message.command=="PADINPUT","Storage changed the STAGECTL command.");
            if(argument=="STATUS") {
                check(messenger.buffer().booleans()==std::vector<std::uint8_t>{0},"STATUS did not seed physical boolean zero.");
                messenger.buffer().replace_booleans(std::array<std::uint8_t,1>{pad_reply});
            } else check(messenger.buffer().booleans().empty(),"A later PADINPUT selection retained STATUS results.");
        } else trace.push_back(call.message.category+":"+call.message.command.value_or("null"));
        return {true,1,{}};
    }
    MessengerCallResult run() {SystemStorageInvocation call(system,state);return call.resume(services);}
};
void defaults_classification_and_short_paths() {
    Fixture initial;
    check(initial.state->dvd_status==0 && initial.state->error_phase==0 && initial.state->is_dvd_error==0 &&
        initial.state->is_now_freeze==0 && initial.state->is_now_pad_enable==0 && initial.system->reset_enabled==1,
        "Storage scalar defaults do not match reflected beSystem fields.");
    for(const auto status:{std::numeric_limits<std::int32_t>::min(),-3,-2,0,1,2,3,7,8,9,10,12,13,
        std::numeric_limits<std::int32_t>::max()}) {
        Fixture f;f.status=status;f.state->error_phase=-2;f.state->is_dvd_error=9;f.state->dvd_status=6;
        f.system->context.reset();f.services->messenger={};f.services->viewer={};f.services->operation={};
        const auto result=f.run();
        check(result.completed && result.value==1 && f.samples==1 && f.lookups==0 && f.viewer_lookups==0,
            "An unclassified or ready status invented downstream service work.");
        check(f.state->dvd_status==6 && f.state->error_phase==-2 && f.state->is_dvd_error==(status==0 ? 0:9) &&
            f.system->reset_enabled==1,"Status classification changed unrelated storage fields.");
    }
    for(const auto phase:{4,255,std::numeric_limits<std::int32_t>::max()}) {
        Fixture f;f.state->error_phase=phase;const auto result=f.run();
        check(result.completed && result.value==0 && f.state->error_phase==phase && f.trace.empty(),
            "An unknown positive error phase was reset or claimed ready.");
    }
    Fixture held;held.status=1;held.state->is_dvd_error=255;held.state->error_phase=3;
    check(held.run().value==0 && held.trace.empty(),"A retained nonzero error flag failed to gate phase-three restore.");
}
void complete_error_lifecycle() {
    Fixture f;f.status=5;
    const auto first=f.run();
    check(first.completed && first.value==0 && f.state->error_phase==2 && f.state->dvd_status==5 &&
        f.state->is_dvd_error==1 && f.state->is_now_freeze==1 && f.state->is_now_pad_enable==1,
        "Initial storage error failed to publish its freeze phase and flags.");
    check(f.trace==std::vector<std::string>{"PAD:STATUS","PAD:DISABLE","viewer","factor:0","managers:0",
        "CAMERA:ONFREEZE","SOUND:ONFREEZE","save:0","register:0"},"Error freeze operation order differs from the original.");
    check(f.binding->messenger->deferred_pause_anchor()==350 && f.lookups==2,
        "Freeze did not reread its messenger owner or used system time instead of messenger context time.");
    f.trace.clear();f.pad_reply=1;
    check(f.run().value==0 && f.state->error_phase==3 && f.trace==std::vector<std::string>{
        "PAD:STATUS","viewer","switch:0","clear:0","bit:0"},"Second error update failed to select the actual status-five mask bit.");
    f.trace.clear();check(f.run().value==0 && f.trace==std::vector<std::string>{"PAD:STATUS"},
        "Persistent phase-three error replayed freeze or switch operations.");
    f.status=0;f.trace.clear();const auto restored=f.run();
    check(restored.completed && restored.value==0 && f.state->error_phase==0 && f.state->is_dvd_error==0 &&
        f.state->is_now_pad_enable==0 && f.state->is_now_freeze==1 && f.binding->messenger->deferred_pause_anchor()==0,
        "Restore lost its false completion or incorrectly cleared the retained freeze flag.");
    check(f.trace==std::vector<std::string>{"viewer:restore","remove:0","factor:1","managers:1","SOUND:OFFREEZE",
        "CAMERA:OFFREEZE","restore:0","PAD:ENABLE"},"Restore operation order differs from the original.");
    f.trace.clear();check(f.run().value==1 && f.trace.empty(),"Only the next healthy update should report ready.");
}
void mappings_existing_pause_and_live_branch() {
    for(const auto pair:{std::pair{-1,4},std::pair{4,1},std::pair{5,0},std::pair{6,2},std::pair{11,3}}) {
        Fixture f;f.status=pair.first;f.state->is_dvd_error=7;f.state->dvd_status=123;f.state->error_phase=3;f.pad_reply=255;
        const auto result=f.run();
        check(result.completed && result.value==0 && f.state->error_phase==3 && f.state->dvd_status==pair.first &&
            f.system->reset_enabled==(pair.first==-1 ? 0:1),"Changed error status did not enter phase two or gate fatal reset.");
        check(f.trace==std::vector<std::string>{"PAD:STATUS","viewer","switch:0","clear:0","bit:"+std::to_string(pair.second)},
            "DVD status jump table selected the wrong display bit.");
    }
    Fixture already;already.status=4;already.pad_reply=1;already.binding->messenger->set_deferred_pause(1,777);
    check(already.run().value==0 && already.state->is_now_freeze==0 && already.state->is_now_pad_enable==0 &&
        already.binding->messenger->deferred_pause_anchor()==777 &&
        std::find(already.trace.begin(),already.trace.end(),"managers:0")==already.trace.end(),
        "An existing deferred pause was overwritten or claimed as this error's freeze.");
    already.state->error_phase=3;already.status=0;already.trace.clear();
    check(already.run().value==0 && already.binding->messenger->deferred_pause_anchor()==777 &&
        already.trace==std::vector<std::string>{"viewer:restore","remove:0","factor:1","CAMERA:OFFREEZE","restore:0"},
        "A foreign pause was resumed or received unsupported SOUND/OFFREEZE.");
    Fixture zero;zero.status=6;zero.pad_reply=1;zero.binding->context->world_time=0;
    check(zero.run().value==0 && zero.state->is_now_freeze==1 && zero.binding->messenger->deferred_pause_anchor()==0,
        "Pause at time zero invented a nonzero sentinel or lost original freeze ownership.");
    Fixture changed;changed.status=11;
    changed.delivery=[&](NativeMessenger& messenger,const MessengerDelivery& call) {
        const auto result=changed.ordinary(messenger,call);
        if(call.message.category=="STAGECTL" && messenger.buffer().strings().front()=="DISABLE")changed.state->error_phase=-9;
        return result;
    };
    check(changed.run().value==1 && changed.state->error_phase==-9 && changed.state->is_now_pad_enable==1 &&
        changed.viewer_lookups==0,"The helper failed to reload its phase after PADINPUT callbacks.");
}
void retained_messages_operations_and_fields() {
    Fixture f;f.status=-1;unsigned sound_calls{};bool sound_ready{};
    f.delivery=[&](NativeMessenger& messenger,const MessengerDelivery& call) {
        if(call.message.category=="SOUND") {
            ++sound_calls;if(!sound_ready)return MessengerCallResult{false,0,"sound pending"};
        }
        return f.ordinary(messenger,call);
    };
    SystemStorageInvocation call(f.system,f.state);
    check(!call.resume(f.services).completed && f.system->reset_enabled==0 && f.state->error_phase==1 &&
        f.binding->messenger->deferred_pause_anchor()==350 && sound_calls==1,
        "A pending freeze send failed to preserve its reset/phase/pause prefix.");
    const auto retained=f.binding->messenger;
    f.binding->messenger=std::make_shared<NativeMessenger>();
    f.services->messenger=[](const auto&)->SystemStorageOwnerResult<SystemStorageMessenger>{throw std::runtime_error("lookup replay");};
    check(!call.resume(f.services).completed && f.samples==1 && f.lookups==2 && sound_calls==2,
        "Retrying a pending SOUND send repeated status, owner lookup or completed messages.");
    sound_ready=true;check(call.resume(f.services).completed && f.state->error_phase==2 && retained->selected().category=="SOUND",
        "Pending send retargeted a replacement messenger allocation.");
    const auto count=f.trace.size();check(call.resume().completed && f.trace.size()==count && sound_calls==3,
        "A completed storage invocation replayed service work.");

    Fixture op;op.status=5;op.pad_reply=1;bool ready{};unsigned attempts{},factories{};
    const auto original=op.services->operation;const auto scene=op.viewer->scene_info;
    op.services->operation=[&](const SystemStorageOperationRequest& request) -> SystemStorageCall {
        if(request.operation!=Op::save_viewer_children)return original(request);
        ++factories;
        return [&,request] {++attempts;check(request.viewer->scene_info==scene,"Pending scene service changed its captured viewer owner.");
            return SystemStorageOperationResult{ready,{},ready ? "":"scene pending"};};
    };
    SystemStorageInvocation waiting(op.system,op.state);
    check(!waiting.resume(op.services).completed && factories==1 && attempts==1,"Scene service did not suspend at its reached boundary.");
    op.viewer->scene_info=std::make_shared<int>(9);op.services->operation=original;
    op.state->error_phase=std::numeric_limits<std::int32_t>::max();ready=true;
    check(waiting.resume(op.services).completed && factories==1 && attempts==2 &&
        op.state->error_phase==std::numeric_limits<std::int32_t>::min(),
        "Pending scene service was rebound or phase increment lost live modulo arithmetic.");

    Fixture mask;mask.status=1;mask.state->error_phase=2;mask.state->dvd_status=5;bool bit_ready{};unsigned bit_calls{};
    const auto default_op=mask.services->operation;
    mask.services->operation=[&](const SystemStorageOperationRequest& request) -> SystemStorageCall {
        if(request.operation==Op::clear_error_switch)return [&] {
            mask.state->dvd_status=11;return SystemStorageOperationResult{true,{},{}};
        };
        if(request.operation==Op::set_error_switch_bit)return [&,request] {
            ++bit_calls;check(request.value==3,"Switch selection did not read status after mask clear.");
            return SystemStorageOperationResult{bit_ready,{},"bit pending"};
        };
        return default_op(request);
    };
    SystemStorageInvocation bits(mask.system,mask.state);
    check(!bits.resume(mask.services).completed && bit_calls==1,"Mask bit operation did not suspend.");
    mask.state->dvd_status=123;bit_ready=true;
    check(bits.resume(mask.services).completed && bit_calls==2 && mask.state->error_phase==3,
        "Pending selected-bit call was skipped when live status changed afterward.");

    Fixture context;context.status=4;context.pad_reply=1;
    const auto original_context=context.system->context;
    const auto context_operation=context.services->operation;context.services->operation={};
    SystemStorageInvocation captured(context.system,context.state);
    check(!captured.resume(context.services).completed,"An unavailable factor binding completed.");
    context.system->context.reset();
    context.services->operation=[&](const SystemStorageOperationRequest& request) {
        if(request.operation==Op::set_world_factor)
            check(request.context==original_context,"An unavailable factor binding retargeted its already captured context argument.");
        return context_operation(request);
    };
    check(captured.resume(context.services).completed,"A late factor binding lost the original captured nonnull context.");

    Fixture lookup;lookup.status=1;lookup.state->error_phase=2;lookup.state->dvd_status=5;
    const auto lookup_context=lookup.system->context;lookup.services->viewer={};
    SystemStorageInvocation owner_lookup(lookup.system,lookup.state);
    check(!owner_lookup.resume(lookup.services).completed,"Missing viewer lookup invented a manager.");
    lookup.system->context=std::make_shared<ModelSceneContext>();
    lookup.services->viewer=[&](const auto& owner,bool restore) {
        check(owner==lookup_context && !restore,"Unavailable viewer lookup retargeted its captured context or field profile.");
        return SystemStorageOwnerResult<SystemStorageViewer>{true,lookup.viewer,{}};
    };
    check(owner_lookup.resume(lookup.services).completed,"Captured viewer lookup failed to resume with its original context.");
}
void pending_faults_and_lifetimes() {
    Fixture f;SystemStorageInvocation missing(f.system,f.state);
    check(!missing.resume().completed && f.samples==0,"Missing status service fabricated a healthy drive.");
    unsigned attempts{};bool ready{};
    f.services->drive_status=[&] {++attempts;return SystemStorageValueResult{ready,0,"sample pending"};};
    check(!missing.resume(f.services).completed,"Pending status sample was claimed complete.");
    f.services->drive_status=[]()->SystemStorageValueResult{throw std::runtime_error("replacement selected");};
    ready=true;check(missing.resume(f.services).value==1 && attempts==2,"Pending status provider was replaced.");
    Fixture unsupported;unsupported.status=4;unsupported.pad_reply=1;unsupported.services->operation={};
    SystemStorageInvocation stop(unsupported.system,unsupported.state);
    check(!stop.resume(unsupported.services).completed && unsupported.state->is_dvd_error==1 &&
        unsupported.state->error_phase==1 && unsupported.viewer_lookups==1,
        "Missing world/scene service was silently treated as completed.");
    SystemStorageInvocation overlap(unsupported.system,unsupported.state);
    rejects([&] {overlap.resume(unsupported.services);},"Overlapping storage invocations were accepted.");
    check(!unsupported.state->faulted,"Rejecting overlap corrupted the original suspended invocation.");
    Fixture malformed;malformed.status=4;malformed.delivery=[](NativeMessenger& messenger,const MessengerDelivery&) {
        messenger.buffer().clear_booleans();return MessengerCallResult{true,1,{}};
    };
    SystemStorageInvocation bad(malformed.system,malformed.state);
    rejects([&] {bad.resume(malformed.services);},"A missing physical STATUS boolean was replaced by false.");
    check(bad.faulted() && malformed.state->faulted && malformed.state->is_dvd_error==1 && malformed.state->error_phase==1,
        "A malformed STATUS reply rolled back the published error prefix.");
    rejects([&] {bad.resume(malformed.services);},"A faulted storage prefix replayed.");
    Fixture null;null.state->error_phase=2;null.services->viewer=[](const auto&,bool) {return SystemStorageOwnerResult<SystemStorageViewer>{true,{},{}};};
    SystemStorageInvocation null_owner(null.system,null.state);
    rejects([&] {null_owner.resume(null.services);},"Represented null viewer manager was accepted.");
    Fixture recursive;SystemStorageInvocation reentry(recursive.system,recursive.state);
    recursive.services->drive_status=[&] {
        try {reentry.resume(recursive.services);}catch(const SystemStorageError&) {}
        return SystemStorageValueResult{true,0,{}};
    };
    rejects([&] {reentry.resume(recursive.services);},"Caught recursive sampling did not poison its active invocation.");
    check(reentry.faulted() && recursive.state->faulted,"Recursive failure lost its owner fault.");
    auto abandoned=std::make_shared<SystemStorageState>();
    {SystemStorageInvocation partial(std::make_shared<ModelSceneSystem>(),abandoned);check(!partial.resume().completed,"Missing sampler completed.");}
    check(abandoned->faulted,"Abandoning an entered storage update silently permitted restart.");
    struct Combined : ModelSceneSystem {SystemStorageState storage;};
    auto combined=std::make_shared<Combined>();std::weak_ptr<Combined> weak=combined;
    {
        auto alias=std::shared_ptr<SystemStorageState>(combined,&combined->storage);
        SystemStorageInvocation held(combined,alias);combined.reset();alias.reset();
        check(!weak.expired(),"Aliasing storage state failed to retain its enclosing system.");
        auto services=std::make_shared<SystemStorageServices>();services->drive_status=[] {return SystemStorageValueResult{true,0,{}};};
        check(held.resume(services).value==1,"Aliasing storage state could not complete the healthy path.");
    }
    check(weak.expired(),"Completed storage invocation created an enclosing-system ownership cycle.");
}
}
int main() {
    try {
        defaults_classification_and_short_paths();complete_error_lifecycle();mappings_existing_pause_and_live_branch();
        retained_messages_operations_and_fields();pending_faults_and_lifetimes();
        std::cout<<"System storage checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<"System storage after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
