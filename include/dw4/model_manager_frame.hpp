#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class ModelManagerFrameError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Native continuation ownership only. These are not original manager fields.
struct ModelManagerFrameExecutionState {
    std::weak_ptr<void> active;
    bool faulted{};
};

// Actual beBaseInfoManager virtual +74, USA 802F28F8. The manager and each
// reached info/RAM occurrence remain physical owners; no registration IDs or
// assumed player count participate. Counts are captured signed words, while
// list/backing/virtual reloads occur at their original sites.
class ModelManagerFrameInvocation {
public:
    explicit ModelManagerFrameInvocation(std::shared_ptr<ModelSceneManager>);
    ~ModelManagerFrameInvocation();
    ModelManagerFrameInvocation(const ModelManagerFrameInvocation&)=delete;
    ModelManagerFrameInvocation& operator=(const ModelManagerFrameInvocation&)=delete;
    MessengerCallResult resume();
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A reusable actual +74 binding for SerialSchedulerManager. Pending calls
// retain their invocation; the next call after completion begins a new frame.
std::function<MessengerCallResult()> make_model_manager_update(std::shared_ptr<ModelSceneManager>);
} // namespace dw4
