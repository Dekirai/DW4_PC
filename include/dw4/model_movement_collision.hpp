#pragma once

#include "dw4/messenger.hpp"
#include "dw4/motion.hpp"

namespace dw4 {
class ActionEventRuntime;
class ModelSceneRam;
class ModelSceneInfo;
class ModelPlayerWork;
struct ModelSceneManager;

enum class ModelFloorMotion { rising,falling,land };
// Original 8030A0A0 modes 3/4/5. Only the reached vertical record is read;
// land writes active=0 then speed=+0, preserving its mode and other fields.
bool model_floor_motion(const std::shared_ptr<MotionChannelList>&,ModelFloorMotion);

struct ModelMovementCollisionScope {
    std::weak_ptr<ModelSceneManager> manager;
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelPlayerWork> work;
};
struct ModelMovementHitRequest {
    std::optional<std::string> prefix;
    std::string event,source_name;
    // The other 8030B6EC source arguments are all original literal zero.
};
struct ModelMovementRideTarget {std::string name;std::int32_t ram_no{};};
struct ModelMovementCollisionServices {
    // Owns the complete actual 8030B6EC invocation and its fresh ONE/context,
    // Lua and merge effects. An incomplete callback must retain its operation.
    std::function<MessengerCallResult(const ModelMovementCollisionScope&,
        const ModelMovementHitRequest&)> hit_script;
    // Remaining 80309F58..A088 type gate, release or identity/init operation.
    // Current ride and captured returned target remain distinct. A null ride
    // and no target complete directly without requiring this provider.
    std::function<MessengerCallResult(const ModelMovementCollisionScope&,
        const std::optional<ModelMovementRideTarget>&)> ride_update;
};

// Actual 80309C74, called after translated != 0 by the original frame.
// Incoming parent/work are outer captures; this helper never retargets them
// through RAM.parent or ONE. Its own null/flag gates precede reached reads.
class ModelMovementCollisionInvocation {
public:
    ModelMovementCollisionInvocation(std::shared_ptr<ModelSceneRam>,
        std::shared_ptr<ModelSceneInfo> captured_parent,std::shared_ptr<ModelPlayerWork> captured_work);
    ~ModelMovementCollisionInvocation();
    ModelMovementCollisionInvocation(const ModelMovementCollisionInvocation&)=delete;
    ModelMovementCollisionInvocation& operator=(const ModelMovementCollisionInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&,
        std::shared_ptr<ModelMovementCollisionServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const ModelMovementCollisionScope& captured() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
