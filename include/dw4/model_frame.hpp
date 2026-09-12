#pragma once

#include "dw4/model_player_input.hpp"
#include "dw4/model_status_update.hpp"

namespace dw4 {
class ModelCommonWait;
class ModelSubcommandWrapper;
class ModelRideData;
class ModelMotionInvocation;
class ModelSubcommandUpdateInvocation;
struct ModelSubcommandUpdateServices;
class ModelMovementCollisionInvocation;
struct ModelMovementCollisionServices;
// Host continuation lease only; no guest work field or owning back-reference.
struct ModelFrameExecutionLease {bool completed{},faulted{},executing{};};

struct ModelFrameContext {
    std::weak_ptr<ModelSceneManager> manager;
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelSceneOne> one;
    std::shared_ptr<ModelPlayerWork> work;
};
struct ModelFrameServices {
    ModelPlayerInputServices input;
    std::shared_ptr<ModelStatusUpdateServices> status;
    // Providers execute their real original helper, retaining any internal
    // partial operation. An absent/unfinished provider suspends this phase.
    std::function<MessengerCallResult(const ModelFrameContext&)> position_lock;
    std::function<MessengerCallResult(const ModelFrameContext&,const std::shared_ptr<ModelRideData>&)> ride;
    std::shared_ptr<ModelSubcommandUpdateServices> subcommands;
    std::shared_ptr<ModelMovementCollisionServices> collision;
    std::function<MessengerCallResult(const ModelFrameContext&,const std::shared_ptr<ModelRideData>&)> ride_tail;
};

// Actual player-manager +84 (8030CB2C), one explicit per-RAM call. This is not
// the enclosing manager scheduler. Captured outer work survives all independent
// input/status/wait callbacks and their nested ordinary sequence entries.
class ModelFrameInvocation {
public:
    explicit ModelFrameInvocation(std::shared_ptr<ModelSceneRam>);
    ~ModelFrameInvocation();
    ModelFrameInvocation(const ModelFrameInvocation&)=delete;
    ModelFrameInvocation& operator=(const ModelFrameInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&,
        std::shared_ptr<ModelFrameServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const ModelFrameContext& captured() const noexcept;
    const ModelPlayerInputInvocation* input() const noexcept;
    const ModelStatusUpdateInvocation* status() const noexcept;
    const ModelMotionInvocation* motion() const noexcept;
    const ModelSubcommandUpdateInvocation* subcommands() const noexcept;
    const ModelMovementCollisionInvocation* collision() const noexcept;
    const ModelCommonWait* wait() const noexcept;
private:
    friend class ActionEventRuntime;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
