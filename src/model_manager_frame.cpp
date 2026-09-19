#include "dw4/model_manager_frame.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ModelManagerFrameError(reason);}
std::shared_ptr<ModelSceneList> require_list(std::shared_ptr<ModelSceneList> list) {
    if(!list)fail("Original manager frame dereferences a null physical list.");
    return list;
}
std::shared_ptr<ModelSceneObject> physical(const std::shared_ptr<ModelSceneList::Storage>& backing,std::int32_t index) {
    if(index<0 || index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
        fail("Original manager frame reaches an unavailable physical pointer slot.");
    return (*backing)[static_cast<std::size_t>(index)];
}
std::shared_ptr<ModelSceneList> ram_list(const std::shared_ptr<ModelSceneObject>& info) {
    if(!info)fail("Original manager frame dereferences a null info occurrence.");
    if(const auto value=std::dynamic_pointer_cast<ModelSceneInfo>(info))return require_list(value->rams);
    if(const auto value=std::dynamic_pointer_cast<ModelHitLandInfo>(info))return require_list(value->rams);
    if(const auto value=std::dynamic_pointer_cast<ModelHitLandGeometryInfo>(info))return require_list(value->rams);
    fail("Original manager frame reached an info type whose RAM-list field is not represented.");
}
}
struct ModelManagerFrameInvocation::Impl {
    enum class Phase {gate,outer,inner,enter,call,done};
    std::shared_ptr<ModelSceneManager> manager;
    std::shared_ptr<ModelManagerFrameExecutionState> execution;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<ModelSceneList> rams;
    std::shared_ptr<ModelSceneObject> ram;
    ModelManagerRamFactory factory;
    ModelManagerRamCall callback;
    std::int32_t info_count{},info_index{},ram_count{},ram_index{};
    Phase phase=Phase::gate;
    bool owns{},running{},fault{};
};
ModelManagerFrameInvocation::ModelManagerFrameInvocation(std::shared_ptr<ModelSceneManager> manager)
    :impl_(std::make_unique<Impl>()) {impl_->manager=std::move(manager);}
ModelManagerFrameInvocation::~ModelManagerFrameInvocation() {
    if(impl_->owns && !completed())impl_->execution->faulted=true;
}
bool ModelManagerFrameInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool ModelManagerFrameInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult ModelManagerFrameInvocation::resume() {
    auto& s=*impl_;
    if(s.fault || s.running) {
        s.fault=true;if(s.owns)s.execution->faulted=true;
        fail("Manager frame requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.running);
    const auto check=[&] {
        if(s.fault || s.execution->faulted || s.manager->manager_frame_execution!=s.execution)
            fail("The retained manager frame execution owner was invalidated.");
    };
    const auto finish=[&] {
        s.phase=Impl::Phase::done;s.execution->active.reset();s.token.reset();s.owns=false;
        return MessengerCallResult{true,0,{}};
    };
    try {
        if(!s.manager)fail("Original manager frame dereferences a null manager.");
        if(!s.owns) {
            if(!s.manager->manager_frame_execution)
                s.manager->manager_frame_execution=std::make_shared<ModelManagerFrameExecutionState>();
            s.execution=s.manager->manager_frame_execution;
            if(s.execution->faulted)fail("A faulted manager frame cannot restart its completed callback prefix.");
            if(!s.execution->active.expired())fail("Another pending frame owns this manager.");
            s.execution->active=s.token;s.owns=true;
        }
        check();
        for(;;)switch(s.phase) {
        case Impl::Phase::gate: {
            const auto system=s.manager->system;
            if(!system)fail("Original manager frame calls the active-system leaf through a null system.");
            const auto first_time=system->is_first_time; // 80316808 reads +2D before +2C.
            const auto active=system->is_active;
            if(first_time!=0 || (active&1)==0)return finish();
            if(s.manager->update_enabled==0)return finish();
            const auto current_system=s.manager->system; // Original reload at 802F2928.
            if(!current_system)fail("Original manager frame reads frame-skip through a null system.");
            if(current_system->is_frame_skip!=0)return finish();
            s.info_count=require_list(s.manager->infos)->count;
            s.phase=Impl::Phase::outer;break;
        }
        case Impl::Phase::outer: {
            if(s.info_index>=s.info_count)return finish();
            // The manager's info list/backing are live for every occurrence.
            const auto infos=require_list(s.manager->infos);
            s.rams=ram_list(physical(infos->backing,s.info_index));
            s.ram_count=s.rams->count;s.ram_index=0;s.phase=Impl::Phase::inner;break;
        }
        case Impl::Phase::inner: {
            if(s.ram_index>=s.ram_count) {
                s.rams.reset();++s.info_index;s.phase=Impl::Phase::outer;break;
            }
            // 802F2970 captures RAM backing before the manager vtable load.
            // A null RAM is an argument, not a dereference in this caller.
            const auto backing=s.rams->backing;
            s.factory=s.manager->ram_frame_factory;
            s.ram=physical(backing,s.ram_index);
            s.phase=Impl::Phase::enter;break;
        }
        case Impl::Phase::enter: {
            if(!s.factory)s.factory=s.manager->ram_frame_factory;
            if(!s.factory)return {false,0,"Original manager frame requires the reached virtual+84 implementation."};
            s.callback=s.factory(s.manager,s.ram);check();
            if(!s.callback)fail("A bound manager virtual+84 factory returned no operation.");
            s.factory={};s.phase=Impl::Phase::call;break;
        }
        case Impl::Phase::call: {
            const auto result=s.callback();check();
            if(!result.completed)return result;
            s.callback={};s.ram.reset();check();
            ++s.ram_index;s.phase=Impl::Phase::inner;break;
        }
        case Impl::Phase::done:return {true,0,{}};
        }
    }catch(...) {s.fault=true;if(s.owns)s.execution->faulted=true;throw;}
}
std::function<MessengerCallResult()> make_model_manager_update(std::shared_ptr<ModelSceneManager> manager) {
    struct Retained {std::shared_ptr<ModelSceneManager> manager;std::unique_ptr<ModelManagerFrameInvocation> active;};
    auto state=std::make_shared<Retained>();state->manager=std::move(manager);
    return [state] {
        if(!state->active)state->active=std::make_unique<ModelManagerFrameInvocation>(state->manager);
        const auto result=state->active->resume();
        if(result.completed)state->active.reset();
        return result;
    };
}
} // namespace dw4
