#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace dw4 {
class ModelSceneRam;
class ModelSceneTimer;

// Read the actual registered RAM manager/context clock at the caller's current
// original helper site. No OS clock or timestamp captured at sequence entry.
using ModelWorldClockReader=std::function<std::uint64_t(const ModelSceneRam&)>;
// Original RAM.manager -> manager.context -> context world-time fields.
// Resolve both current references at the reached helper site.
std::uint64_t model_world_time(const ModelSceneRam&);

// Actual named timer operations on one retained RAM. Each helper looks up the
// current RAM.data; only its own captured timer survives its clock/read phases.
// The facade never owns a copied optional timer or caches a named record.
class ModelRamTimerAccess {
public:
    explicit ModelRamTimerAccess(std::shared_ptr<ModelSceneRam>);
    ModelRamTimerAccess(std::shared_ptr<ModelSceneRam>,ModelWorldClockReader);
    ModelRamTimerAccess(const ModelRamTimerAccess&)=delete;
    ModelRamTimerAccess& operator=(const ModelRamTimerAccess&)=delete;
    ModelRamTimerAccess(ModelRamTimerAccess&&)=delete;
    ModelRamTimerAccess& operator=(ModelRamTimerAccess&&)=delete;
    const std::shared_ptr<ModelSceneRam>& ram_owner() const noexcept { return ram_; }
    bool faulted() const noexcept { return faulted_; }

    // 8031E138: untyped first-name presence; no clock or timer-field access.
    bool exists(std::string_view name);
    // 8031E238: absent means expired without a clock. A present record's end
    // is captured before a fresh complete time lookup; equality is not expired.
    bool expired(std::string_view name);
    // 8031DE44: append a missing named record before reading context time;
    // explicit start restarts an existing timer, preserving scale/previous time.
    std::shared_ptr<ModelSceneTimer> start(std::string_view name,std::int64_t end_time=0);
    // 8031E36C: fresh lookup, context clock, then scale and bias.
    std::int64_t time(std::string_view name);
    // 8031E4C4: capture T/saved time, read W1; on a changed timestamp call the
    // complete fresh lookup/time helper (W2), then publish back into captured T.
    float frames(std::string_view name);

private:
    std::shared_ptr<ModelSceneRam> ram_;
    ModelWorldClockReader clock_;
    bool executing_{},faulted_{};
    class Operation;
    std::uint64_t world_time();
    std::int64_t time_impl(std::string_view name);
};
} // namespace dw4
