#include "dw4/system_storage.hpp"

#include <bit>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw SystemStorageError(reason);}
MessengerCallResult pending(std::string diagnostic,const char* fallback) {
    return {false,0,diagnostic.empty() ? fallback:std::move(diagnostic)};
}
bool error_status(std::int32_t value) {return value==-1 || value==4 || value==5 || value==6 || value==11;}
std::int32_t error_bit(std::int32_t value) {
    switch(value) {case -1:return 4;case 4:return 1;case 5:return 0;case 6:return 2;case 11:return 3;default:return -1;}
}
}
struct SystemStorageInvocation::Impl {
    enum class Phase {
        poll,error_messenger,pad_query,pad_reply,pad_disable,pad_disabled,branch,
        freeze_viewer,freeze_messenger,freeze_time,pause_test,disable_managers,pause_messages,
        freeze_camera,freeze_sound,save_children,register_infos,advance,
        switch_viewer,switch_select,switch_clear,switch_bit,
        restore_viewer,restore_messenger,remove_children,restore_time,restore_test,
        enable_managers,unpause_messages,unfreeze_sound,unfreeze_camera,restore_children,
        restore_pad_test,pad_enable,restore_finish,done
    };
    std::shared_ptr<ModelSceneSystem> system;
    std::shared_ptr<SystemStorageState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0),selected_switch;
    std::shared_ptr<SystemStorageMessenger> messenger_binding;
    std::shared_ptr<NativeMessenger> messenger;
    std::shared_ptr<SystemStorageViewer> viewer;
    std::shared_ptr<ModelSceneContext> lookup_context;
    std::function<SystemStorageValueResult()> poll;
    decltype(SystemStorageServices::messenger) resolve_messenger;
    decltype(SystemStorageServices::viewer) resolve_viewer;
    SystemStorageCall operation;
    std::optional<SystemStorageOperationRequest> request;
    std::optional<MessengerDispatch> dispatch;
    Phase phase=Phase::poll;
    std::int32_t selected_bit{-1};
    std::uint32_t result{};
    bool owns{},running{},fault{},lookup_entered{};
};
SystemStorageInvocation::SystemStorageInvocation(std::shared_ptr<ModelSceneSystem> system,
    std::shared_ptr<SystemStorageState> state):impl_(std::make_unique<Impl>()) {
    impl_->system=std::move(system);impl_->state=std::move(state);
}
SystemStorageInvocation::~SystemStorageInvocation() {
    if(impl_->owns && !completed())impl_->state->faulted=true;
}
bool SystemStorageInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool SystemStorageInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult SystemStorageInvocation::resume(const std::shared_ptr<SystemStorageServices>& services) {
    auto& s=*impl_;
    if(s.fault || s.running) {
        s.fault=true;if(s.owns)s.state->faulted=true;
        fail("System storage requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,s.result,{}};
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.running);
    const auto check=[&] {
        if(s.fault || s.state->faulted || s.state->active_invocation.lock()!=s.token)
            fail("The retained system storage execution owner was invalidated.");
    };
    const auto finish=[&](std::uint32_t result) {
        s.result=result;s.phase=Impl::Phase::done;s.state->active_invocation.reset();s.token.reset();s.owns=false;
        return MessengerCallResult{true,result,{}};
    };
    const auto messenger=[&]() -> std::optional<MessengerCallResult> {
        if(!s.lookup_entered) {
            s.lookup_context=s.system->context;s.lookup_entered=true;
            if(!s.lookup_context)fail("Original system beMessenger lookup dereferences a null application context.");
        }
        if(!s.resolve_messenger && services)s.resolve_messenger=services->messenger;
        if(!s.resolve_messenger)return pending({},"Original system storage requires its context beMessenger lookup.");
        const auto result=s.resolve_messenger(s.lookup_context);check();
        if(!result.completed)return pending(result.diagnostic,"Original system beMessenger lookup is pending.");
        if(!result.owner || !result.owner->messenger)fail("Original system storage reaches a null beMessenger owner.");
        s.messenger_binding=result.owner;s.messenger=result.owner->messenger;s.resolve_messenger={};
        s.lookup_context.reset();s.lookup_entered=false;check();
        return {};
    };
    const auto viewer=[&](bool restore) -> std::optional<MessengerCallResult> {
        if(!s.lookup_entered) {
            s.lookup_context=s.system->context;s.lookup_entered=true;
            if(!s.lookup_context)fail("Original system viewer lookup dereferences a null application context.");
        }
        if(!s.resolve_viewer && services)s.resolve_viewer=services->viewer;
        if(!s.resolve_viewer)return pending({},"Original system storage requires its context viewer-manager lookup.");
        const auto result=s.resolve_viewer(s.lookup_context,restore);check();
        if(!result.completed)return pending(result.diagnostic,"Original system viewer-manager lookup is pending.");
        if(!result.owner)fail("Original system storage dereferences its null viewer manager.");
        s.viewer=std::make_shared<SystemStorageViewer>(*result.owner);s.resolve_viewer={};
        s.lookup_context.reset();s.lookup_entered=false;check();return {};
    };
    const auto operation=[&](SystemStorageOperation kind,std::int32_t value=0) -> std::optional<MessengerCallResult> {
        if(!s.request) {
            const auto context=kind==SystemStorageOperation::set_world_factor ? s.system->context:nullptr;
            if(kind==SystemStorageOperation::set_world_factor && !context)
                fail("Original system world-factor call dereferences its null application context.");
            s.request=SystemStorageOperationRequest{kind,s.system,context,s.viewer,s.selected_switch,value};
        }
        if(!s.operation && services && services->operation) {
            const auto factory=services->operation;
            s.operation=factory(*s.request);check();
        }
        if(!s.operation)return pending({},"Original system storage requires its reached scene, clock or manager operation.");
        const auto result=s.operation();check();
        if(!result.completed)return pending(result.diagnostic,"Original system storage operation is pending.");
        if(kind==SystemStorageOperation::select_error_switch)s.selected_switch=result.owner;
        s.operation={};s.request.reset();check();return {};
    };
    const auto send=[&](const char* category,const char* command,const char* argument=nullptr,bool status=false)
        -> std::optional<MessengerCallResult> {
        if(!s.dispatch) {
            s.messenger->select(category,{},-1);
            if(argument)s.messenger->buffer().add_string(std::string(argument));
            if(status)s.messenger->buffer().add_bool(0);
            s.dispatch.emplace(s.messenger->begin_send(command));
        }
        const MessengerServices empty;
        const auto delivery=s.messenger_binding->delivery;
        const auto result=s.messenger->resume(*s.dispatch,delivery ? *delivery:empty);check();
        if(result.stop==MessengerStop::faulted)fail("Original system storage message delivery faulted.");
        if(result.stop!=MessengerStop::completed)return pending(result.diagnostic,"Original system storage message delivery is pending.");
        s.dispatch.reset();check();return {};
    };
    try {
        if(!s.system || !s.state)fail("Original system storage requires its system and state owners.");
        if(s.state->faulted)fail("A faulted system storage prefix cannot be restarted.");
        if(!s.owns) {
            if(!s.state->active_invocation.expired())fail("Another invocation owns the pending system storage update.");
            s.state->active_invocation=s.token;s.owns=true;
        }
        check();
        for(;;)switch(s.phase) {
        case Impl::Phase::poll: {
            if(!s.poll && services)s.poll=services->drive_status;
            if(!s.poll)return pending({},"Original system storage requires an actual drive-status provider.");
            const auto result=s.poll();check();
            if(!result.completed)return pending(result.diagnostic,"Original drive-status sample is pending.");
            s.poll={};check();
            if(result.value==0)s.state->is_dvd_error=0;
            if(error_status(result.value)) {
                if(result.value==-1)s.system->reset_enabled=0;
                if(s.state->is_dvd_error==0) {s.state->dvd_status=result.value;s.state->error_phase=1;}
                else if(s.state->dvd_status!=result.value) {s.state->dvd_status=result.value;s.state->error_phase=2;}
                s.state->is_dvd_error=1;s.phase=Impl::Phase::error_messenger;
            } else s.phase=Impl::Phase::branch;
            break;
        }
        case Impl::Phase::error_messenger:
            if(auto result=messenger())return *result;s.phase=Impl::Phase::pad_query;break;
        case Impl::Phase::pad_query:
            if(auto result=send("STAGECTL","PADINPUT","STATUS",true))return *result;
            s.phase=Impl::Phase::pad_reply;break;
        case Impl::Phase::pad_reply: {
            const auto& values=s.messenger->buffer().booleans();
            if(values.empty())fail("Original system PADINPUT query requires physical boolean slot zero.");
            s.phase=values.front()!=0 ? Impl::Phase::branch:Impl::Phase::pad_disable;break;
        }
        case Impl::Phase::pad_disable:
            if(auto result=send("STAGECTL","PADINPUT","DISABLE"))return *result;
            s.phase=Impl::Phase::pad_disabled;break;
        case Impl::Phase::pad_disabled:
            s.state->is_now_pad_enable=1;s.phase=Impl::Phase::branch;break;
        case Impl::Phase::branch:
            if(s.state->error_phase<=0)return finish(1);
            if(s.state->error_phase==1)s.phase=Impl::Phase::freeze_viewer;
            else if(s.state->error_phase==2)s.phase=Impl::Phase::switch_viewer;
            else if(s.state->error_phase==3 && s.state->is_dvd_error==0)s.phase=Impl::Phase::restore_viewer;
            else return finish(0);
            break;
        case Impl::Phase::freeze_viewer:
            if(auto result=viewer(false))return *result;s.phase=Impl::Phase::freeze_messenger;break;
        case Impl::Phase::freeze_messenger:
            if(auto result=messenger())return *result;s.phase=Impl::Phase::freeze_time;break;
        case Impl::Phase::freeze_time:
            if(auto result=operation(SystemStorageOperation::set_world_factor,0))return *result;
            s.phase=Impl::Phase::pause_test;break;
        case Impl::Phase::pause_test:
            if(s.messenger->deferred_pause_anchor()==0) {
                s.state->is_now_freeze=1;s.phase=Impl::Phase::disable_managers;
            } else {s.state->is_now_freeze=0;s.phase=Impl::Phase::freeze_camera;}
            break;
        case Impl::Phase::disable_managers:
            if(auto result=operation(SystemStorageOperation::set_managers_enabled,0))return *result;
            s.phase=Impl::Phase::pause_messages;break;
        case Impl::Phase::pause_messages:
            if(!s.messenger_binding->context)fail("Original messenger pause dereferences its own null context.");
            s.messenger->set_deferred_pause(1,s.messenger_binding->context->world_time);
            s.phase=Impl::Phase::freeze_camera;break;
        case Impl::Phase::freeze_camera:
            if(auto result=send("CAMERA","ONFREEZE"))return *result;s.phase=Impl::Phase::freeze_sound;break;
        case Impl::Phase::freeze_sound:
            if(auto result=send("SOUND","ONFREEZE"))return *result;s.phase=Impl::Phase::save_children;break;
        case Impl::Phase::save_children:
            if(!s.viewer->scene_info)fail("Original system freeze dereferences its null viewer scene info.");
            if(auto result=operation(SystemStorageOperation::save_viewer_children))return *result;
            s.phase=Impl::Phase::register_infos;break;
        case Impl::Phase::register_infos:
            if(auto result=operation(SystemStorageOperation::register_error_infos))return *result;
            s.phase=Impl::Phase::advance;break;
        case Impl::Phase::advance:
            s.state->error_phase=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(s.state->error_phase)+1u);
            return finish(0);
        case Impl::Phase::switch_viewer:
            if(auto result=viewer(false))return *result;s.phase=Impl::Phase::switch_select;break;
        case Impl::Phase::switch_select:
            if(!s.viewer->scene_info)fail("Original system error switch dereferences its null viewer scene info.");
            if(auto result=operation(SystemStorageOperation::select_error_switch))return *result;
            s.phase=Impl::Phase::switch_clear;break;
        case Impl::Phase::switch_clear:
            if(!s.selected_switch)fail("Original system error switch dereferences its null or wrong-type switch.");
            if(auto result=operation(SystemStorageOperation::clear_error_switch))return *result;
            s.selected_bit=error_bit(s.state->dvd_status);
            s.phase=Impl::Phase::switch_bit;break;
        case Impl::Phase::switch_bit: {
            if(s.selected_bit>=0)if(auto result=operation(SystemStorageOperation::set_error_switch_bit,s.selected_bit))return *result;
            s.phase=Impl::Phase::advance;break;
        }
        case Impl::Phase::restore_viewer:
            if(auto result=viewer(true))return *result;s.phase=Impl::Phase::restore_messenger;break;
        case Impl::Phase::restore_messenger:
            if(auto result=messenger())return *result;s.phase=Impl::Phase::remove_children;break;
        case Impl::Phase::remove_children:
            if(!s.viewer->scene_info)fail("Original system restore dereferences its null viewer scene info.");
            if(auto result=operation(SystemStorageOperation::remove_error_children))return *result;
            s.phase=Impl::Phase::restore_time;break;
        case Impl::Phase::restore_time:
            if(auto result=operation(SystemStorageOperation::set_world_factor,1))return *result;
            s.phase=Impl::Phase::restore_test;break;
        case Impl::Phase::restore_test:
            s.phase=s.state->is_now_freeze!=0 ? Impl::Phase::enable_managers:Impl::Phase::unfreeze_camera;break;
        case Impl::Phase::enable_managers:
            if(auto result=operation(SystemStorageOperation::set_managers_enabled,1))return *result;
            s.phase=Impl::Phase::unpause_messages;break;
        case Impl::Phase::unpause_messages:
            s.messenger->set_deferred_pause(0,0);s.phase=Impl::Phase::unfreeze_sound;break;
        case Impl::Phase::unfreeze_sound:
            if(auto result=send("SOUND","OFFREEZE"))return *result;s.phase=Impl::Phase::unfreeze_camera;break;
        case Impl::Phase::unfreeze_camera:
            if(auto result=send("CAMERA","OFFREEZE"))return *result;s.phase=Impl::Phase::restore_children;break;
        case Impl::Phase::restore_children:
            if(auto result=operation(SystemStorageOperation::restore_viewer_children))return *result;
            s.phase=Impl::Phase::restore_pad_test;break;
        case Impl::Phase::restore_pad_test:
            s.phase=s.state->is_now_pad_enable!=0 ? Impl::Phase::pad_enable:Impl::Phase::restore_finish;break;
        case Impl::Phase::pad_enable:
            if(auto result=send("STAGECTL","PADINPUT","ENABLE"))return *result;
            s.phase=Impl::Phase::restore_finish;break;
        case Impl::Phase::restore_finish:
            s.state->is_now_pad_enable=0;s.state->error_phase=0;return finish(0);
        case Impl::Phase::done:return {true,s.result,{}};
        }
    }catch(...) {s.fault=true;if(s.owns)s.state->faulted=true;throw;}
}
} // namespace dw4
