#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"
#include "dw4/system_stage.hpp"
#include "dw4/system_storage.hpp"

namespace dw4 {
class SystemFrameError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// One original beSystem allocation. Its base is shared directly by the
// existing player-manager gates and timing consumers; stage/storage members
// hold additional recovered fields, not independently selected game systems.
// These represented defaults do not run initialization or request ndmw.igb.
struct SystemFrameState : ModelSceneSystem {
    SystemFrameState()=default;
    SystemFrameState(const SystemFrameState&)=delete;
    SystemFrameState& operator=(const SystemFrameState&)=delete;
    std::uint8_t reset_now{},reset_last{},reset_release{}; // +60/+61/+62.
    SystemStageState stage;
    SystemStorageState storage;
    bool faulted() const noexcept {return fault_;}
private:
    friend class SystemFrameInvocation;
    std::weak_ptr<void> active_frame_;
    bool fault_{};
};
struct SystemFrameServices {
    // Platform intent/effects at the actual original SDK calls. Values are
    // raw32 return words. A missing reached operation suspends; no console
    // hardware access, fabricated button state or reset success is supplied.
    std::function<MessengerCallResult()> sample_reset,flush_video,wait_retrace,check_disk;
    std::function<MessengerCallResult(std::uint32_t)> set_black;
    std::function<MessengerCallResult(std::uint32_t,std::uint32_t,std::uint32_t)> reset_system;
    std::shared_ptr<SystemStageServices> stage;
    std::shared_ptr<SystemStorageServices> storage;
};

// Actual beSystem virtual+74 at80316258. Retains reset, stage and storage
// continuations, with the original timing leaf between reset and loading.
// There is no ordinary model-manager active/first-time/frame-skip admission
// gate here. A completed reset exits even if the host reset service returns.
class SystemFrameInvocation {
public:
    explicit SystemFrameInvocation(std::shared_ptr<SystemFrameState>);
    ~SystemFrameInvocation();
    SystemFrameInvocation(const SystemFrameInvocation&)=delete;
    SystemFrameInvocation& operator=(const SystemFrameInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<SystemFrameServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Reusable concrete binding for SerialSchedulerManager: keep one pending
// system invocation; create the next update only after it completes.
std::function<MessengerCallResult()> make_system_frame_update(
    std::shared_ptr<SystemFrameState>,std::shared_ptr<SystemFrameServices>);
} // namespace dw4
