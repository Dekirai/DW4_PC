#pragma once

#include "dw4/messenger.hpp"

namespace dw4 {
class ActionEventRuntime;
class ModelHitLandRam;
struct ModelSceneManager;

// One actual beHitLandDeliv recipient callback, 802FDA0C. The caller retains
// this invocation for one delivery, including nested HITLANDMODEL queries.
class ModelHitLandInvocation {
public:
    ModelHitLandInvocation(std::shared_ptr<ModelHitLandRam>,std::optional<std::string> command,
        std::shared_ptr<const ModelSceneManager> invoking_manager);
    ~ModelHitLandInvocation();
    ModelHitLandInvocation(const ModelHitLandInvocation&)=delete;
    ModelHitLandInvocation& operator=(const ModelHitLandInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelHitLandRam>& ram_owner() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
