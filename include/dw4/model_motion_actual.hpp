#pragma once

#include "dw4/messenger.hpp"
#include "dw4/model_motion.hpp"

#include <memory>

namespace dw4 {
class ModelSceneRam;
class ModelSceneInfo;
class ModelPlayerWork;

// Actual 8030A448 invocation with its outer captured work and parent. It reads
// current RAM data/ONE/node only at the reached turn/translation sites. A null
// supplied endpoint uses the actual RAM manager/context clock when needed.
class ModelMotionInvocation {
public:
    ModelMotionInvocation(std::shared_ptr<ModelSceneRam>,std::shared_ptr<ModelSceneInfo> captured_parent,
        std::shared_ptr<ModelPlayerWork> captured_work,std::shared_ptr<ModelRamTimerAccess> timer={});
    ~ModelMotionInvocation();
    ModelMotionInvocation(const ModelMotionInvocation&)=delete;
    ModelMotionInvocation& operator=(const ModelMotionInvocation&)=delete;
    MessengerCallResult resume();
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const ModelMotionStep& result() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
