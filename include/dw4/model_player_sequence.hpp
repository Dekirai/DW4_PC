#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class ActionEventRuntime;
class ModelBaseInvocation;
struct ModelPlayerCreateServices;

// One player virtual +B8 (8034E1B0) and its required 803AE1A4 reconciliation.
// Both helpers independently resolve current RAM data and retain their own
// work generation. Nested SQ_STUN is an actual direct base invocation.
class ModelPlayerSequenceInvocation {
public:
    explicit ModelPlayerSequenceInvocation(std::shared_ptr<ModelSceneRam>);
    ~ModelPlayerSequenceInvocation();
    ModelPlayerSequenceInvocation(const ModelPlayerSequenceInvocation&)=delete;
    ModelPlayerSequenceInvocation& operator=(const ModelPlayerSequenceInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&,const ModelPlayerCreateServices&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelSceneInfo>& captured_parent() const;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const;
    const std::shared_ptr<ModelPlayerWork>& reconciliation_work() const;
    const ModelBaseInvocation* direct_entry() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
