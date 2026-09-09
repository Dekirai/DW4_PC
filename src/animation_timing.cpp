#include "dw4/animation_timing.hpp"
#include "dw4/animations.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
void finite(float value) {
    if (!std::isfinite(value)) throw AnimationError("Animation timing inputs must be finite.");
}
std::int64_t signed_bits(std::uint64_t value) { return std::bit_cast<std::int64_t>(value); }
std::uint64_t bits(std::int64_t value) { return std::bit_cast<std::uint64_t>(value); }
std::int64_t integer(float value) {
    // 800A4130, despite its __cvt_dbl_usll name: signed saturation/truncation.
    // Finite operands may overflow to infinity; no undefined host float cast.
    if (value >= 0x1p63f) return std::numeric_limits<std::int64_t>::max();
    if (value <= -0x1p63f) return std::numeric_limits<std::int64_t>::min();
    if (std::isnan(value)) throw AnimationError("Invalid animation time conversion.");
    return static_cast<std::int64_t>(value);
}
std::int64_t scaled_time(std::uint64_t time, float scale) {
    const auto coarse = static_cast<std::uint32_t>(time >> 18);
    const float converted = static_cast<float>(coarse);
    return integer(converted * scale);
}
std::uint64_t scaled_bits(std::uint64_t time, float scale) {
    return bits(scaled_time(time, scale)) << 18;
}
float original_long_float(std::int64_t value) {
    // 800A3FDC converts to double first, then executes frsp.
    return static_cast<float>(static_cast<double>(value));
}
} // namespace

std::int64_t advance_animation_clock(AnimationStateClock& state,
    AnimationClockCache& cache, std::uint64_t world_time) {
    finite(state.time_scale);
    auto candidate = cache;
    if (candidate.world_time != world_time || candidate.time_scale != state.time_scale) {
        candidate.scaled_time = scaled_time(world_time, state.time_scale);
        candidate.world_time = world_time;
        candidate.time_scale = state.time_scale;
    }
    const auto local = signed_bits((bits(candidate.scaled_time) << 18) + bits(state.time_bias));
    cache = candidate;
    state.local_time = local;
    return local;
}
void change_animation_speed(AnimationStateClock& state,
    std::uint64_t world_time, float time_scale) {
    finite(state.time_scale); finite(time_scale);
    const auto bias = bits(state.time_bias) + scaled_bits(world_time, state.time_scale) -
        scaled_bits(world_time, time_scale);
    state.time_bias = signed_bits(bias);
    state.time_scale = time_scale;
}
AnimationBlendWindow prepare_animation_blend(float original_frames, bool no_interpolation) {
    if (no_interpolation) return {1, 1, 1, 0};
    finite(original_frames);
    if (original_frames == 0) return {1, 1, 1, 0};
    const float scaled = 1000000000.0f * original_frames;
    const float duration = scaled / 60.0f;
    return {1, integer(duration), 0, 1};
}
void begin_animation_blend(AnimationStateClock& state, AnimationBlendWindow& blend,
    AnimationClockCache& cache, std::uint64_t world_time, std::int64_t animation_start_time) {
    finite(state.time_scale);
    auto candidate = state;
    candidate.time_bias = signed_bits(bits(animation_start_time) - scaled_bits(world_time, state.time_scale));
    advance_animation_clock(candidate, cache, world_time);
    state = candidate;
    blend.start_time = world_time;
}
float animation_blend_ratio(const AnimationBlendWindow& blend, std::uint64_t world_time) {
    finite(blend.start_ratio); finite(blend.ratio_range);
    const float duration = original_long_float(blend.duration);
    const float elapsed = original_long_float(signed_bits(world_time - blend.start_time));
    float ratio = elapsed / duration;
    if (ratio > 1) ratio = 1;
    return std::fma(ratio, blend.ratio_range, blend.start_ratio);
}

void AnimationWrapContext::initialize_action_offset() noexcept {
    offset_ = std::bit_cast<float>(std::uint32_t{0xbd088889});
}
float AnimationWrapContext::map(float seconds, float duration, AnimationPlayMode mode) {
    finite(seconds); finite(duration);
    if (duration <= 0) throw AnimationError("Animation wrapping requires a positive duration.");
    const auto code = static_cast<std::int32_t>(mode);
    if (code < 0 || code > 4) return 0; // Original default switch branch.
    if (mode == AnimationPlayMode::offset_clamp) duration += offset_;
    if (mode == AnimationPlayMode::clamp || mode == AnimationPlayMode::offset_clamp) {
        // Ordering and strict comparisons matter, including exact endpoints.
        if (seconds > duration) return duration;
        if (seconds < 0) return 0;
    }
    if (!(duration > 0) || !std::isfinite(duration))
        throw AnimationError("Animation wrapping duration is invalid after its offset.");
    if (input_ != seconds || duration_ != duration) {
        const float quotient = seconds / duration;
        // Actual Enbaya time conversion and its minimum duration stay within
        // this range. Reject invalid generic calls before fctiwz's overflow case.
        if (!std::isfinite(quotient) || quotient >= 0x1p31f || quotient < -0x1p31f)
            throw AnimationError("Animation cycle count exceeds the verified signed range.");
        const auto cycle = static_cast<std::int32_t>(quotient);
        const float converted = static_cast<float>(cycle);
        const float remainder = std::fma(-converted, duration, seconds);
        input_ = seconds; duration_ = duration; cycle_ = cycle;
        remainder_ = remainder > 0 ? remainder : 0;
    }
    return mode == AnimationPlayMode::bounce && (static_cast<std::uint32_t>(cycle_) & 1) ?
        duration - remainder_ : remainder_;
}
AnimationSourceClock::AnimationSourceClock(float duration, AnimationPlayMode mode)
    : duration_(duration), mode_(mode) {
    finite(duration);
    if (duration < 0) throw AnimationError("Animation source duration must not be negative.");
}
float AnimationSourceClock::sample_time(std::int64_t local_time, AnimationWrapContext& context) {
    if (saved_time_ && *saved_time_ == local_time) return saved_seconds_;
    const auto coarse = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits(local_time) >> 18));
    const float seconds = static_cast<float>(coarse) *
        std::bit_cast<float>(std::uint32_t{0x3989705f});
    const float duration = duration_ <= std::bit_cast<float>(std::uint32_t{0x3d088889}) ?
        std::bit_cast<float>(std::uint32_t{0x3d4cccce}) : duration_;
    const float result = context.map(seconds, duration, mode_);
    saved_time_ = local_time; saved_seconds_ = result;
    return result;
}
} // namespace dw4
