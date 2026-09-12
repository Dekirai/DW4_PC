#include "dw4/model_frame.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_timer.hpp"
#include "dw4/model_wait_actual.hpp"
#include "dw4/model_motion_actual.hpp"
#include "dw4/model_subcommand_update.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw ModelSceneError(message);}
const std::string& reached_name(const std::optional<std::string>& value) {
    if(!value)fail("Actual model frame compares a null sequence name.");
    return *value;
}
// Untyped first-name removal, 8031E8BC. It does not allocate or query time.
void remove_ai_timer(ModelSceneRam& ram) {
    if(!ram.data)return;
    const auto found=find_model_named(ram.data,"AITIMER");if(!found)return;
    const auto list=ram.data;
    const auto slot=[&](std::int32_t index)->std::shared_ptr<ModelSceneObject>& {
        if(!list->backing || index<0 || static_cast<std::size_t>(index)>=list->backing->size())
            fail("AI timer removal reached unavailable actual list backing.");
        return (*list->backing)[static_cast<std::size_t>(index)];
    };
    std::int32_t index{};for(;index<list->count;++index)if(slot(index)==found)break;
    if(index==list->count)index=-1;
    slot(index).reset();
    const auto count=list->count;
    if(count>0 && index>=0 && index<count) {
        for(auto next=index;next<count-1;++next)slot(next)=slot(next+1);
        list->count=count-1;
    }
    slot(list->count).reset();
}
}
struct ModelFrameInvocation::Impl {
    enum class Phase { capture,gates,sequence_gate,remove,reset,control_gate,input,control_tail,direct,
        status,position_gate,motion,position_lock,ride_gate,ride,wait_gate,wait,invulnerability,
        subcommand_capture,subcommands,collision_gate,collision,ride_tail_gate,ride_tail,completed };
    ModelFrameContext context;
    NativeMessenger* messenger{};
    std::shared_ptr<ModelFrameServices> services;
    std::unique_ptr<ModelPlayerInputInvocation> input;
    std::unique_ptr<ModelStatusUpdateInvocation> status;
    std::unique_ptr<ModelMotionInvocation> motion;
    std::unique_ptr<ModelBaseInvocation> direct;
    std::unique_ptr<ModelCommonWait> wait;
    std::shared_ptr<ModelRideData> ride;
    std::unique_ptr<ModelSubcommandUpdateInvocation> subcommands;
    std::shared_ptr<ModelRamTimerAccess> timer;
    std::shared_ptr<ModelFrameExecutionLease> lease=std::make_shared<ModelFrameExecutionLease>();
    Phase phase=Phase::capture;
    bool running{},fault{};
};
ModelFrameInvocation::ModelFrameInvocation(std::shared_ptr<ModelSceneRam> ram):impl_(std::make_unique<Impl>()) {
    if(!ram)fail("Actual model frame requires a RAM owner.");
    impl_->context.ram=std::move(ram);
}
ModelFrameInvocation::~ModelFrameInvocation() {if(!completed())impl_->lease->faulted=true;}
bool ModelFrameInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelFrameInvocation::faulted() const noexcept {return impl_->fault || impl_->lease->faulted;}
const ModelFrameContext& ModelFrameInvocation::captured() const noexcept {return impl_->context;}
const ModelPlayerInputInvocation* ModelFrameInvocation::input() const noexcept {return impl_->input.get();}
const ModelStatusUpdateInvocation* ModelFrameInvocation::status() const noexcept {return impl_->status.get();}
const ModelMotionInvocation* ModelFrameInvocation::motion() const noexcept {return impl_->motion.get();}
const ModelSubcommandUpdateInvocation* ModelFrameInvocation::subcommands() const noexcept {return impl_->subcommands.get();}
const ModelCommonWait* ModelFrameInvocation::wait() const noexcept {return impl_->wait.get();}
MessengerCallResult ModelFrameInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    std::shared_ptr<ModelFrameServices> supplied) {
    auto& state=*impl_;auto& scope=state.context;
    if(state.running || faulted()) {state.fault=true;state.lease->faulted=true;fail("Actual model frame requires a nonrecursive unfaulted invocation.");}
    if(state.messenger && (state.messenger!=&messenger || scope.manager.lock()!=events.model_manager())) {
        state.fault=true;fail("A retained model frame requires its original messenger and manager.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(state.running);
    const auto check=[&] {if(faulted())fail("A caught recursive call invalidated this model frame.");};
    const auto services=[&]() -> ModelFrameServices* {
        if(state.services && supplied!=state.services)fail("A suspended model frame requires its retained service owner.");
        if(!state.services)state.services=supplied;
        return state.services.get();
    };
    const auto missing=[](const char* name) {return MessengerCallResult{false,0,std::string("Original model frame requires ")+name+"."};};
    const auto complete=[&] {state.phase=Impl::Phase::completed;state.lease->completed=true;return MessengerCallResult{true,0,{}};};
    try {
        events.retain_model_frame(state.lease);
        Guard execution(state.lease->executing);
        for(;;)switch(state.phase) {
        case Impl::Phase::capture:
            state.messenger=&messenger;scope.manager=events.model_manager();
            // Parent capture precedes ensure-data/ONE lookup, even for an
            // inactive RAM. The work reference is loaded before activity.
            scope.parent=scope.ram->parent.lock();scope.one=find_model_one(*scope.ram);
            if(!scope.one)fail("Actual frame dereferences a missing first named ONE before its activity gate.");
            scope.work=scope.one->work;state.phase=Impl::Phase::gates;break;
        case Impl::Phase::gates:
            if(scope.ram->active==0)return complete();
            if(!scope.work)fail("An active model frame reads frozen through a null captured work.");
            if(scope.work->frozen!=0)return complete();
            state.phase=Impl::Phase::sequence_gate;break;
        case Impl::Phase::sequence_gate:
            if(reached_name(scope.work->sequence.name)=="SQ_ERAS") {
                state.direct=std::make_unique<ModelBaseInvocation>(scope.ram,"REMOVE");state.phase=Impl::Phase::remove;
            } else state.phase=Impl::Phase::reset;
            break;
        case Impl::Phase::remove: {
            const auto result=state.direct->resume(messenger,events,events.player_creation_services());check();
            if(!result.completed)return result;
            return complete();
        }
        case Impl::Phase::reset:
            scope.work->key_input=0;scope.work->key_event_name="SQ_NONE";state.phase=Impl::Phase::control_gate;break;
        case Impl::Phase::control_gate:
            if(scope.work->control.mode!=0) {
                if(scope.work->sequence.wait.control_enabled!=0) {
                    state.input=std::make_unique<ModelPlayerInputInvocation>(scope.ram);state.phase=Impl::Phase::input;
                } else state.phase=Impl::Phase::control_tail;
            } else {
                if(scope.work->ai)remove_ai_timer(*scope.ram);
                state.phase=Impl::Phase::status;
            }
            break;
        case Impl::Phase::input: {
            const auto current=services();const ModelPlayerInputServices empty;
            const auto result=state.input->resume(messenger,events,current ? current->input:empty);check();
            if(!result.completed)return result;
            state.phase=Impl::Phase::control_tail;break;
        }
        case Impl::Phase::control_tail: {
            const auto request=update_model_control(*scope.work);
            if(request) {state.direct=std::make_unique<ModelBaseInvocation>(scope.ram,*request);state.phase=Impl::Phase::direct;}
            else state.phase=Impl::Phase::status;
            break;
        }
        case Impl::Phase::direct: {
            const auto result=state.direct->resume(messenger,events,events.player_creation_services());check();
            if(!result.completed)return result;
            state.phase=Impl::Phase::status;break;
        }
        case Impl::Phase::status: {
            if(!state.status)state.status=std::make_unique<ModelStatusUpdateInvocation>(scope.ram);
            const auto current=services();
            const auto result=state.status->resume(messenger,events,events.player_creation_services(),current ? current->status:nullptr);check();
            if(!result.completed)return result;
            state.phase=Impl::Phase::position_gate;break;
        }
        case Impl::Phase::position_gate:
            state.phase=scope.work->motion.position_locked==0 ? Impl::Phase::motion:Impl::Phase::position_lock;break;
        case Impl::Phase::motion: {
            if(!state.timer)state.timer=std::make_shared<ModelRamTimerAccess>(scope.ram);
            if(!state.motion)state.motion=std::make_unique<ModelMotionInvocation>(scope.ram,scope.parent,scope.work,state.timer);
            const auto result=state.motion->resume();check();if(!result.completed)return result;
            state.phase=Impl::Phase::ride_gate;break;
        }
        case Impl::Phase::position_lock: {
            const auto current=services();if(!current || !current->position_lock)return missing("actual position-lock and box execution");
            const auto result=current->position_lock(scope);check();if(!result.completed)return result;
            state.phase=Impl::Phase::ride_gate;break;
        }
        case Impl::Phase::ride_gate:
            state.ride=scope.work->ride_data;state.phase=state.ride ? Impl::Phase::ride:Impl::Phase::wait_gate;break;
        case Impl::Phase::ride: {
            const auto current=services();if(!current || !current->ride)return missing("actual ride update and matrix composition");
            const auto result=current->ride(scope,state.ride);check();if(!result.completed)return result;
            scope.work->motion.translated=1;state.ride.reset();state.phase=Impl::Phase::wait_gate;break;
        }
        case Impl::Phase::wait_gate:
            if(scope.work->sequence.wait.name) {
                state.wait=std::make_unique<ModelCommonWait>(scope.ram,scope.parent,scope.work);state.phase=Impl::Phase::wait;
            } else state.phase=Impl::Phase::invulnerability;
            break;
        case Impl::Phase::wait: {
            const auto result=state.wait->resume(messenger,events);check();if(!result.completed)return result;
            state.phase=Impl::Phase::invulnerability;break;
        }
        case Impl::Phase::invulnerability: {
            if(!state.timer)state.timer=std::make_shared<ModelRamTimerAccess>(scope.ram);
            ModelControlState control(ModelMotionPose::fields_only(std::shared_ptr<ModelMotionFields>(scope.work,&scope.work->motion)),
                std::shared_ptr<ModelControlFields>(scope.work,&scope.work->control),state.timer);
            advance_model_invulnerability(control);state.phase=Impl::Phase::subcommand_capture;break;
        }
        case Impl::Phase::subcommand_capture:
            state.subcommands=std::make_unique<ModelSubcommandUpdateInvocation>(scope.ram,scope.work->subcommands);
            state.phase=Impl::Phase::subcommands;break;
        case Impl::Phase::subcommands: {
            const auto current=services();
            const auto result=state.subcommands->resume(current ? current->subcommands:nullptr);check();
            if(!result.complete())return missing("actual subcommand sound/effect/hitbox execution");
            state.phase=Impl::Phase::collision_gate;break;
        }
        case Impl::Phase::collision_gate:
            state.phase=scope.work->motion.translated!=0 ? Impl::Phase::collision:Impl::Phase::ride_tail_gate;break;
        case Impl::Phase::collision: {
            const auto current=services();if(!current || !current->collision)return missing("actual movement collision/hit processing");
            const auto result=current->collision(scope);check();if(!result.completed)return result;
            state.phase=Impl::Phase::ride_tail_gate;break;
        }
        case Impl::Phase::ride_tail_gate:
            state.ride=scope.work->ride_data;
            if(!state.ride)return complete();
            state.phase=Impl::Phase::ride_tail;break;
        case Impl::Phase::ride_tail: {
            const auto current=services();if(!current || !current->ride_tail)return missing("actual ride-type and final-position update");
            const auto result=current->ride_tail(scope,state.ride);check();if(!result.completed)return result;
            return complete();
        }
        case Impl::Phase::completed:return {true,0,{}};
        }
    }catch(...){state.fault=true;state.lease->faulted=true;throw;}
}
} // namespace dw4
