#include "dw4/actor_transform.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ActorSceneError(reason);}
}
struct ActorTransformUpdateInvocation::Impl {
    enum class Phase {binding,capture,update,done};
    Phase phase=Phase::binding;
    std::shared_ptr<const ActorSceneNode> node;
    std::shared_ptr<ModelSceneObject> input;
    std::shared_ptr<AnimationMatrix> matrix;
    std::shared_ptr<ActorTransformUpdateServices> services;
    decltype(ActorTransformUpdateServices::input_update) update;
    std::uint64_t time{};
    bool executing{},fault{};
};
ActorTransformUpdateInvocation::ActorTransformUpdateInvocation(std::shared_ptr<const ActorSceneNode> node,std::uint64_t time)
    :impl_(std::make_unique<Impl>()) {impl_->node=std::move(node);impl_->time=time;}
ActorTransformUpdateInvocation::~ActorTransformUpdateInvocation()=default;
bool ActorTransformUpdateInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool ActorTransformUpdateInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult ActorTransformUpdateInvocation::resume(std::shared_ptr<ActorTransformUpdateServices> supplied) {
    auto& state=*impl_;using Phase=Impl::Phase;
    if(state.fault || state.executing) {state.fault=true;fail("Transform update requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Guard {bool& active;explicit Guard(bool& value):active(value){active=true;}~Guard(){active=false;}} guard(state.executing);
    try {
        if(state.services && supplied && state.services!=supplied)
            fail("Retained transform input update requires its original service owner.");
        for(;;)switch(state.phase) {
        case Phase::binding:
            if(!state.node)fail("Original transform update dereferences a null node.");
            if(state.node->type()!="igTransform" && state.node->type()!="beCopyTransform")
                return {false,0,"Transform update requires the concrete node's original virtual+9C binding."};
            state.phase=Phase::capture;break;
        case Phase::capture:
            state.input=state.node->transform_input_owner();
            if(!state.input) {state.phase=Phase::done;break;}
            // ActorSceneNode owns an originally mutable allocation; a const
            // traversal node view does not make its inline matrix immutable.
            state.matrix=std::const_pointer_cast<AnimationMatrix>(state.node->matrix_owner());
            state.phase=Phase::update;break;
        case Phase::update: {
            if(!state.update && supplied && supplied->input_update) {
                state.services=std::move(supplied);state.update=state.services->input_update;
            }
            if(!state.update)return {false,0,"Transform update requires its captured nonnull input's virtual+5C operation."};
            const auto result=state.update(state.input,state.matrix,state.time);
            if(state.fault)fail("A caught recursive call invalidated the transform input update.");
            if(!result.completed)return result;
            state.phase=Phase::done;break;
        }
        case Phase::done:return {true,0,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
