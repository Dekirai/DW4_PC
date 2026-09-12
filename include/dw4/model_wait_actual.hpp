#pragma once

#include "dw4/messenger.hpp"
#include "dw4/motion.hpp"

#include <memory>

namespace dw4 {
class ActionEventRuntime;
class ModelSceneRam;
class ModelSceneInfo;
class ModelPlayerWork;

enum class ModelWaitMotion { moving,rotation,vertical };
// Only the 8030A0A0 modes consumed by common waits. The list/count and reached
// active flags are read directly; unused entries and numeric payloads are not
// validated. Current MotionChannelList represents count and storage together.
bool model_wait_motion_active(const std::shared_ptr<MotionChannelList>&,ModelWaitMotion);

// Actual player-controller virtual +A4 (8034D214). First resume captures current
// RAM.parent, then freshly resolves ONE/work. Those captures survive subsequent
// graph replacement, query suspension, direct +8C and C3AC continuation.
class ModelPlayerWait {
public:
    explicit ModelPlayerWait(std::shared_ptr<ModelSceneRam>);
    ~ModelPlayerWait();
    ModelPlayerWait(const ModelPlayerWait&)=delete;
    ModelPlayerWait& operator=(const ModelPlayerWait&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelSceneInfo>& captured_parent() const noexcept;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Actual common 8030BDB8 invocation on the connected player-controller profile.
// The caller owns the original null-wait gate; directly invoking this helper
// with null work/name is a reached checked dereference. Unknown names delegate
// to a separate player invocation without reusing the incoming common captures.
class ModelCommonWait {
public:
    ModelCommonWait(std::shared_ptr<ModelSceneRam>,std::shared_ptr<ModelSceneInfo> captured_parent,
        std::shared_ptr<ModelPlayerWork> captured_work);
    ~ModelCommonWait();
    ModelCommonWait(const ModelCommonWait&)=delete;
    ModelCommonWait& operator=(const ModelCommonWait&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelSceneInfo>& captured_parent() const noexcept;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const noexcept;
    const ModelPlayerWait* player_fallback() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
