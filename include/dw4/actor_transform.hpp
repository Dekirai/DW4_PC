#pragma once

#include "dw4/actor_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
struct ActorTransformUpdateServices {
    // Actual nonnull igTransformSource virtual+5C. The output aliases the node's
    // inline +20 matrix and time is the already captured incoming r5/r6 pair.
    // The provider retains any unfinished operation and its partial matrix writes.
    std::function<MessengerCallResult(const std::shared_ptr<ModelSceneObject>&,
        const std::shared_ptr<AnimationMatrix>&,std::uint64_t)> input_update;
};

// 801D73A8, verified +9C binding for exact igTransform (vtable804B34FC)
// and beCopyTransform (804D4BA4). Other concrete bindings remain unresolved.
// The source-copy link at +6C is independent and is not read by this operation.
class ActorTransformUpdateInvocation {
public:
    ActorTransformUpdateInvocation(std::shared_ptr<const ActorSceneNode>,std::uint64_t time);
    ~ActorTransformUpdateInvocation();
    ActorTransformUpdateInvocation(const ActorTransformUpdateInvocation&)=delete;
    ActorTransformUpdateInvocation& operator=(const ActorTransformUpdateInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<ActorTransformUpdateServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
