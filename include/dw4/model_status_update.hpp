#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class ActionEventRuntime;
class ModelBaseInvocation;
class ModelRamTimerAccess;
struct ModelPlayerCreateServices;

struct ModelStatusUpdateServices {
    // FIND_SPACE object0 identifies the actual icon RAM. Retain that owner:
    // 803ACB30 reads its number again after the nested GENERATE returns.
    std::function<std::shared_ptr<ModelSceneRam>(ControllerObjectId)> resolve_ram;
};

// One actual player virtual+C0 (803ACD88). Its fresh ONE/work capture differs
// from the enclosing frame and from the post-entry +B8 reconciliation helper.
class ModelStatusUpdateInvocation {
public:
    explicit ModelStatusUpdateInvocation(std::shared_ptr<ModelSceneRam>,
        std::shared_ptr<ModelRamTimerAccess> actual_timer={});
    ~ModelStatusUpdateInvocation();
    ModelStatusUpdateInvocation(const ModelStatusUpdateInvocation&)=delete;
    ModelStatusUpdateInvocation& operator=(const ModelStatusUpdateInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&,
        const ModelPlayerCreateServices&,
        std::shared_ptr<ModelStatusUpdateServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelSceneOne>& captured_one() const;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const;
    const ModelBaseInvocation* direct_entry() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
