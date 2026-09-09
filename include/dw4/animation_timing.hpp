#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace dw4 {
// Owned numeric fields of igAnimationState, not an actor or complete combiner.
struct AnimationStateClock {
    float time_scale = 1;
    std::int64_t time_bias{};
    std::int64_t local_time{};
};
// 801D3628 shares this cache across states. Scope one cache to an owned runtime;
// never use process-global mutable state for independent game sessions.
struct AnimationClockCache {
    std::uint64_t world_time = std::numeric_limits<std::uint64_t>::max();
    float time_scale = -1;
    std::int64_t scaled_time{}; // Before the original <<18 operation.
};
std::int64_t advance_animation_clock(AnimationStateClock& state,
    AnimationClockCache& cache, std::uint64_t world_time);
// ACTION2's time-origin adjustment. Does not sample or update local_time.
void change_animation_speed(AnimationStateClock& state,
    std::uint64_t world_time, float time_scale);

struct AnimationBlendWindow {
    std::uint64_t start_time = 1; // Explicit MOTIONSET's pending-start sentinel.
    std::int64_t duration{};
    float start_ratio{};
    float ratio_range = 1;
};
// MOTIONSET's hokanTime conversion and isNoHokan branch. This prepares numeric
// blend fields only; it does not allocate states, select tracks or run ACTION2.
AnimationBlendWindow prepare_animation_blend(float original_frames, bool no_interpolation);
void begin_animation_blend(AnimationStateClock& state, AnimationBlendWindow& blend,
    AnimationClockCache& cache, std::uint64_t world_time, std::int64_t animation_start_time);
// 801D0668..801D06AC: upper clamp only, followed by fused weight arithmetic.
// Zero/negative durations preserve original IEEE results. Consumers of poses
// must reject unusable weights rather than silently replacing them.
float animation_blend_ratio(const AnimationBlendWindow& blend, std::uint64_t world_time);

enum class AnimationPlayMode : std::int32_t {
    repeat = 0, clamp = 1, bounce = 2, loop = 3, offset_clamp = 4
};
// 801E4FE8's remainder cache is shared between sources and play modes.
// The cached remainder is stored before BOUNCE applies its odd-cycle reversal.
class AnimationWrapContext {
public:
    // ACTION2 initialization 802EFDC8 sets this shared offset to -1/30f.
    // Zero is the pre-initialization BSS value; initialization is explicit.
    void initialize_action_offset() noexcept;
    float offset() const noexcept { return offset_; }
    float map(float seconds, float duration, AnimationPlayMode mode);
    std::int32_t cached_cycle() const noexcept { return cycle_; }
private:
    float offset_{}, input_{}, duration_{}, remainder_{};
    std::int32_t cycle_{};
};
// Numeric source cache from 801E52FC. Each actual shared animation source owns
// one instance. Changing its mode does NOT invalidate a same-time sample.
// Copies are independent; callers sharing an original source must share this
// instance too. Serialized IGB bytes are never changed.
class AnimationSourceClock {
public:
    explicit AnimationSourceClock(float duration, AnimationPlayMode mode = AnimationPlayMode::repeat);
    float duration() const noexcept { return duration_; }
    AnimationPlayMode mode() const noexcept { return mode_; }
    void set_mode(AnimationPlayMode mode) noexcept { mode_ = mode; }
    float sample_time(std::int64_t local_time, AnimationWrapContext& context);
private:
    float duration_{};
    AnimationPlayMode mode_{};
    std::optional<std::int64_t> saved_time_;
    float saved_seconds_{};
};
} // namespace dw4
