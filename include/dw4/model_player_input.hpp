#pragma once

#include "dw4/messenger.hpp"
#include "dw4/model_pad.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace dw4 {
class ActionEventRuntime;
class ModelSceneRam;
class ModelSceneOne;
class ModelPlayerWork;
struct ModelSceneManager;

struct ModelPlayerInputServices {
    // Original controller+2C -> viewer parameters+14 -> view matrix+6C.
    // The owner type is igViewerSceneInfoManager. Missing provider pauses;
    // a returned null owner is the distinct reached null pointer failure.
    std::function<std::shared_ptr<AnimationMatrix>(const ModelSceneManager&)> camera_matrix;
    // Override for 8012A92C on the retained camera owner. Nullopt suspends.
    // Without an override, use the recovered native inverse and the original
    // caller's identity-initialized destination; singular status is ignored.
    std::function<std::optional<AnimationMatrix>(const AnimationMatrix&)> inverse_camera;
    // Optional override for controller+10 -> system+28, reached only for a
    // positive body timer while B is held. Without an override, read the fixed
    // invoking controller's current system->frame_time_f; a null system faults.
    // An installed override returning nullopt preserves its service boundary.
    std::function<std::optional<float>(const ModelSceneManager&)> frame_delta;
    // Original controller+34 -> player-list+44 -> physical live RAM slot.
    // The returned actual row is retained, then ATTACKDELAY is read from it.
    std::function<std::shared_ptr<GameDataList>(const ModelSceneManager&,std::int32_t)> player_row;
};

// Actual player virtual+A0, 8034C3C8. Captures its own fresh ONE/work and pad;
// it never captures/dereferences RAM.parent or enters a sequence itself. It
// publishes key_event_name for the outer input/control consumer to dispatch.
class ModelPlayerInputInvocation {
public:
    explicit ModelPlayerInputInvocation(std::shared_ptr<ModelSceneRam>);
    ~ModelPlayerInputInvocation();
    ModelPlayerInputInvocation(const ModelPlayerInputInvocation&)=delete;
    ModelPlayerInputInvocation& operator=(const ModelPlayerInputInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&,const ModelPlayerInputServices& = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelSceneOne>& captured_one() const noexcept;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const noexcept;
    const std::shared_ptr<ModelPadState>& captured_pad() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
