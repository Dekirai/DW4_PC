#include "dw4/system_frame.hpp"
#include "dw4/application_frame.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw SystemFrameError(reason);}
}
struct SystemFrameInvocation::Impl {
    enum class Phase {reset_sample_gate,reset_sample,reset_gate,black,flush,retrace,disk,reset,
        time,stage,storage,tail,done};
    std::shared_ptr<SystemFrameState> state;
    std::shared_ptr<SystemFrameServices> services;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::function<MessengerCallResult()> callback;
    std::unique_ptr<SystemStageInvocation> stage;
    std::unique_ptr<SystemStorageInvocation> storage;
    std::uint32_t reset_kind{},storage_result{};
    Phase phase=Phase::reset_sample_gate;
    bool owns{},executing{},fault{};
};
SystemFrameInvocation::SystemFrameInvocation(std::shared_ptr<SystemFrameState> state)
    :impl_(std::make_unique<Impl>()) {impl_->state=std::move(state);}
SystemFrameInvocation::~SystemFrameInvocation() {
    auto& s=*impl_;if(s.owns && !completed())s.state->fault_=true;
}
bool SystemFrameInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool SystemFrameInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult SystemFrameInvocation::resume(std::shared_ptr<SystemFrameServices> services) {
    auto& s=*impl_;
    if(s.fault || s.executing) {
        s.fault=true;if(s.owns && s.state)s.state->fault_=true;
        fail("System update requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.executing);
    const auto check=[&] {if(s.fault || s.state->fault_)fail("The retained system update was invalidated.");};
    const auto finish=[&] {
        s.phase=Impl::Phase::done;s.state->active_frame_.reset();s.token.reset();s.owns=false;
        return MessengerCallResult{true,0,{}};
    };
    const auto call=[&](std::function<MessengerCallResult()> selected,const char* reason) {
        if(!s.callback)s.callback=std::move(selected);
        if(!s.callback)return MessengerCallResult{false,0,reason};
        const auto result=s.callback();check();
        if(result.completed) {s.callback={};check();}
        return result;
    };
    try {
        if(!s.state)fail("Original system update dereferences a null system.");
        if(s.state->fault_)fail("A faulted system update cannot restart its completed prefix.");
        if(!s.owns) {
            if(!s.state->active_frame_.expired())fail("Another pending update owns this system.");
            s.state->active_frame_=s.token;s.owns=true;
        }
        if(s.services && services!=s.services)fail("A pending system update requires its retained service owner.");
        if(!s.services)s.services=std::move(services);
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::reset_sample_gate:
            if(s.state->reset_release!=0)s.phase=Impl::Phase::reset_gate;
            else {s.state->reset_last=s.state->reset_now;s.phase=Impl::Phase::reset_sample;}
            break;
        case Impl::Phase::reset_sample: {
            const auto result=call(s.services ? s.services->sample_reset:nullptr,
                "Original system update requires reset-button sampling.");
            if(!result.completed)return result;
            s.state->reset_now=result.value!=0 ? 1:0;
            // Read both live bytes after the platform call. Noncanonical last
            // values retain the original bitwise falling-edge expression.
            const auto last=s.state->reset_last,now=s.state->reset_now;
            s.state->reset_release=(last&(last^now))!=0 ? 1:0;
            s.phase=Impl::Phase::reset_gate;break;
        }
        case Impl::Phase::reset_gate:
            s.phase=s.state->reset_enabled!=0 && s.state->reset_release!=0 ? Impl::Phase::black:Impl::Phase::time;break;
        case Impl::Phase::black: {
            std::function<MessengerCallResult()> selected;
            if(s.services && s.services->set_black)selected=[f=s.services->set_black] {return f(1);};
            const auto result=call(std::move(selected),"Original system reset requires display blanking.");
            if(!result.completed)return result;s.phase=Impl::Phase::flush;break;
        }
        case Impl::Phase::flush: {
            const auto result=call(s.services ? s.services->flush_video:nullptr,
                "Original system reset requires the display flush.");
            if(!result.completed)return result;s.phase=Impl::Phase::retrace;break;
        }
        case Impl::Phase::retrace: {
            const auto result=call(s.services ? s.services->wait_retrace:nullptr,
                "Original system reset requires the display synchronization point.");
            if(!result.completed)return result;s.phase=Impl::Phase::disk;break;
        }
        case Impl::Phase::disk: {
            const auto result=call(s.services ? s.services->check_disk:nullptr,
                "Original system reset requires the storage-presence check.");
            if(!result.completed)return result;s.reset_kind=result.value!=0 ? 0:1;s.phase=Impl::Phase::reset;break;
        }
        case Impl::Phase::reset: {
            std::function<MessengerCallResult()> selected;
            if(s.services && s.services->reset_system)
                selected=[f=s.services->reset_system,kind=s.reset_kind] {return f(kind,0,0);};
            const auto result=call(std::move(selected),"Original system reset requires the host reset action.");
            if(!result.completed)return result;return finish();
        }
        case Impl::Phase::time:
            publish_system_frame_time(*s.state);s.phase=Impl::Phase::stage;break;
        case Impl::Phase::stage: {
            if(!s.stage)s.stage=std::make_unique<SystemStageInvocation>(s.state,
                std::shared_ptr<SystemStageState>(s.state,&s.state->stage));
            const auto result=s.stage->resume(s.services ? s.services->stage:nullptr);check();
            if(!result.completed)return result;s.phase=Impl::Phase::storage;break;
        }
        case Impl::Phase::storage: {
            if(!s.storage)s.storage=std::make_unique<SystemStorageInvocation>(s.state,
                std::shared_ptr<SystemStorageState>(s.state,&s.state->storage));
            const auto result=s.storage->resume(s.services ? s.services->storage:nullptr);check();
            if(!result.completed)return result;s.storage_result=result.value;s.phase=Impl::Phase::tail;break;
        }
        case Impl::Phase::tail:
            if((s.storage_result&0xff)!=0 && s.state->is_active!=0 && s.state->is_first_time!=0)
                s.state->is_first_time=0;
            return finish();
        case Impl::Phase::done:return {true,0,{}};
        }
        }
    }catch(...) {s.fault=true;if(s.owns)s.state->fault_=true;throw;}
}
std::function<MessengerCallResult()> make_system_frame_update(
    std::shared_ptr<SystemFrameState> state,std::shared_ptr<SystemFrameServices> services) {
    struct Retained {
        std::shared_ptr<SystemFrameState> state;
        std::shared_ptr<SystemFrameServices> services;
        std::unique_ptr<SystemFrameInvocation> active;
    };
    auto retained=std::make_shared<Retained>();retained->state=std::move(state);retained->services=std::move(services);
    return [retained] {
        if(!retained->active)retained->active=std::make_unique<SystemFrameInvocation>(retained->state);
        const auto result=retained->active->resume(retained->services);
        if(result.completed)retained->active.reset();
        return result;
    };
}
} // namespace dw4
