#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class SerialSchedulerError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// One actual native manager update binding. The callback must perform its
// represented original virtual+74 operation; absence is a pending boundary.
class SerialSchedulerManager final : public ModelSceneObject {
public:
    explicit SerialSchedulerManager(std::function<MessengerCallResult()> update={});
    std::function<MessengerCallResult()> update;
};
// scheduler+08 is an ordered-list wrapper; wrapper+08 is its physical list.
// Registration/sorting are separate operations. Entries preserve caller order.
struct SerialSchedulerRegistry {
    std::shared_ptr<ModelSceneList> entries;
};
struct SerialSchedulerState {
    SerialSchedulerState();
    SerialSchedulerState(const SerialSchedulerState&)=delete;
    SerialSchedulerState& operator=(const SerialSchedulerState&)=delete;
    std::shared_ptr<SerialSchedulerRegistry> registered; // +08.
    std::shared_ptr<ModelSceneList> cache;                // +0C.
    bool faulted() const noexcept {return fault_;}
private:
    friend class SerialSchedulerInvocation;
    std::weak_ptr<void> active_batch_;
    bool fault_{};
    std::vector<std::shared_ptr<ModelSceneObject>> unpublished_references_;
};

// Physical word lookup ignores count/capacity. Internal released-reference
// markers preserve original readable pointer identity until a later zero pass;
// uninitialized or dangling words are explicit reached boundaries.
std::shared_ptr<ModelSceneObject> serial_scheduler_physical_object(
    const std::shared_ptr<ModelSceneList>&,std::uint32_t index);

// Exact8028B7B0: capture source count, size cache, copy source owners, call
// captured-count manager virtuals, then clear the live cache. Source and cache
// pointer reloads remain at their original sites. One retained manager/callback
// survives suspension; a completed invocation does not replay the batch.
class SerialSchedulerInvocation {
public:
    explicit SerialSchedulerInvocation(std::shared_ptr<SerialSchedulerState>);
    ~SerialSchedulerInvocation();
    SerialSchedulerInvocation(const SerialSchedulerInvocation&)=delete;
    SerialSchedulerInvocation& operator=(const SerialSchedulerInvocation&)=delete;
    MessengerCallResult resume();
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Suitable for ApplicationFrameState::scheduler. Repeated calls resume one
// pending batch; after completion the next call starts a new actual batch.
std::function<MessengerCallResult()> make_serial_scheduler_update(std::shared_ptr<SerialSchedulerState>);
} // namespace dw4
