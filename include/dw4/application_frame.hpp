#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class NativeGameClock;
class ApplicationFrameError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Represented igInsightCore time fields. Share this exact base owner with
// MODELCTRL managers: world_time is the original context +50/+54 field.
// The caller supplies the separately owned application timer (+30), not the
// global HITLAND timer singleton. Construction does not start it or implement
// the original registry, manager startup, renderer or dispatcher factories.
struct ApplicationFrameState : ModelSceneContext {
    explicit ApplicationFrameState(std::shared_ptr<NativeGameClock> timer);
    ApplicationFrameState(const ApplicationFrameState&)=delete;
    ApplicationFrameState& operator=(const ApplicationFrameState&)=delete;
    std::shared_ptr<NativeGameClock> timer;
    std::uint64_t current_time{},frame_time{}; // +38/+40, metadata zero defaults.
    std::int32_t current_to_world_factor{};   // +48, raw signed fixed-point word.
    // Reached virtual+5C calls on +20 then +0C. A selected callback keeps its
    // continuation across suspension. An absent callback is never a no-op.
    std::function<MessengerCallResult()> dispatcher,scheduler;
    bool faulted() const noexcept {return fault_;}
private:
    friend class ApplicationFrameInvocation;
    std::weak_ptr<void> active_frame_;
    bool fault_{};
};

// Only the explicit factor store at8028ACF0/F4 in the original post-default
// callback. Other initialization effects and timer start remain separate.
void initialize_application_frame_factor(ApplicationFrameState&) noexcept;
// Low64 product followed by arithmetic shift18, with no clamp or wide-product
// substitution. The return value is the original signed result's raw words.
std::uint64_t original_application_world_increment(std::uint64_t delta,std::int32_t factor) noexcept;

// Exact beSystem timing leaf80316320..64. Requires system.context, publishes
// frame_time then frame_time_f then saved_world_time. No clock sample occurs.
// Reset-button handling before this leaf and later system effects are separate.
void publish_system_frame_time(ModelSceneSystem&);

// Original8028AF84: sample the retained timer, publish the application time
// prefix, then dispatcher and scheduler in order. Each new invocation is one
// outer update. Repeated resume after completion never samples or calls again.
// A pending invocation exclusively owns its application until completion;
// throwing, recursive or abandoned started work faults that application.
class ApplicationFrameInvocation {
public:
    explicit ApplicationFrameInvocation(std::shared_ptr<ApplicationFrameState>);
    ~ApplicationFrameInvocation();
    ApplicationFrameInvocation(const ApplicationFrameInvocation&)=delete;
    ApplicationFrameInvocation& operator=(const ApplicationFrameInvocation&)=delete;
    MessengerCallResult resume();
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
