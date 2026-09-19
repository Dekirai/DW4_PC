#include "dw4/application_frame.hpp"
#include "dw4/game_clock.hpp"
#include "dw4/motion_timer.hpp"

#include <bit>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ApplicationFrameError(reason);}
}
ApplicationFrameState::ApplicationFrameState(std::shared_ptr<NativeGameClock> value):timer(std::move(value)) {}
void initialize_application_frame_factor(ApplicationFrameState& state) noexcept {
    state.current_to_world_factor=0x40000;
}
std::uint64_t original_application_world_increment(std::uint64_t delta,std::int32_t factor) noexcept {
    const auto product=delta*static_cast<std::uint64_t>(static_cast<std::int64_t>(factor));
    const auto shifted=product>>18;
    return (product>>63)!=0 ? shifted|(~std::uint64_t{0}<<46):shifted;
}
void publish_system_frame_time(ModelSceneSystem& system) {
    const auto context=system.context;
    if(!context)fail("Original beSystem timing dereferences its missing current application context.");
    const auto now=context->world_time;
    system.frame_time=now-system.saved_world_time;
    system.frame_time_f=original_elapsed_frames(std::bit_cast<std::int64_t>(system.frame_time));
    system.saved_world_time=now;
}
struct ApplicationFrameInvocation::Impl {
    enum class Phase {sample,dispatcher,scheduler,done};
    std::shared_ptr<ApplicationFrameState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<NativeGameClock> timer;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::sample;
    bool owns{},executing{},fault{};
};
ApplicationFrameInvocation::ApplicationFrameInvocation(std::shared_ptr<ApplicationFrameState> state)
    :impl_(std::make_unique<Impl>()) {impl_->state=std::move(state);}
ApplicationFrameInvocation::~ApplicationFrameInvocation() {
    auto& s=*impl_;
    if(s.owns && s.phase!=Impl::Phase::done && s.state)s.state->fault_=true;
}
bool ApplicationFrameInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool ApplicationFrameInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult ApplicationFrameInvocation::resume() {
    auto& s=*impl_;
    if(s.fault || s.executing) {
        s.fault=true;if(s.owns && s.state)s.state->fault_=true;
        fail("Application frame requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.executing);
    auto check=[&] {if(s.fault || s.state->fault_)fail("A caught recursive call faulted the active application frame.");};
    try {
        if(!s.state)fail("Original application frame dereferences a null application owner.");
        if(s.state->fault_)fail("An incomplete application prefix cannot be restarted after a fault.");
        if(!s.owns) {
            const auto active=s.state->active_frame_.lock();
            if(active && active!=s.token)fail("Another pending frame owns this application update.");
            s.state->active_frame_=s.token;s.owns=true;
        }
        while(!completed())switch(s.phase) {
        case Impl::Phase::sample: {
            // Selection occurs at the original +30 load. Replacing state.timer
            // inside its source does not destroy or redirect this active call.
            s.timer=s.state->timer;
            if(!s.timer)fail("Original application frame requires its actual application timer.");
            const auto now=s.timer->time();check();
            s.state->frame_time=now-s.state->current_time;
            s.state->current_time=now;
            s.state->world_time+=original_application_world_increment(s.state->frame_time,s.state->current_to_world_factor);
            s.phase=Impl::Phase::dispatcher;
            break;
        }
        case Impl::Phase::dispatcher:
        case Impl::Phase::scheduler: {
            const bool dispatcher=s.phase==Impl::Phase::dispatcher;
            if(!s.callback)s.callback=dispatcher ? s.state->dispatcher:s.state->scheduler;
            if(!s.callback)return {false,0,dispatcher ?
                "Original application frame requires dispatcher virtual+5C.":
                "Original application frame requires scheduler virtual+5C."};
            const auto result=s.callback();check();
            if(!result.completed)return result;
            s.callback={};
            s.phase=dispatcher ? Impl::Phase::scheduler:Impl::Phase::done;
            break;
        }
        case Impl::Phase::done:break;
        }
        s.state->active_frame_.reset();s.token.reset();s.owns=false;
        return {true,0,{}};
    }catch(...) {
        s.fault=true;if(s.owns)s.state->fault_=true;
        throw;
    }
}
} // namespace dw4
