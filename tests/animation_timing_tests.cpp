#include "dw4/animation_timing.hpp"
#include "dw4/animations.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
int checks{};
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    ++checks; try { action(); } catch (const AnimationError&) { return; }
    throw std::runtime_error("Invalid animation timing was accepted.");
}
constexpr std::uint64_t tick = 1u << 18;
void clocks() {
    AnimationStateClock state; AnimationClockCache cache;
    check(advance_animation_clock(state, cache, tick-1) == 0, "Sub-tick input was retained.");
    check(advance_animation_clock(state, cache, tick) == std::int64_t(tick), "First coarse tick.");
    state.time_bias = -17;
    check(advance_animation_clock(state, cache, tick) == std::int64_t(tick)-17, "Cache ignored per-state bias.");
    AnimationStateClock other{2, 7, -1};
    check(advance_animation_clock(other, cache, tick) == std::int64_t(2*tick)+7, "Shared cache ignored another state's scale.");
    check(advance_animation_clock(state, cache, tick) == std::int64_t(tick)-17, "Independent state was contaminated.");
    state.time_bias = 0;
    check(advance_animation_clock(state, cache, std::uint64_t{1} << 50) == 0, "Clock's 32-bit coarse wrap changed.");
    check(advance_animation_clock(state, cache, (std::uint64_t{1} << 49)) == std::int64_t(std::uint64_t{1} << 49),
        "Animation state clock incorrectly used a signed 32-bit coarse value.");
    state.time_scale = -0.5f;
    check(advance_animation_clock(state, cache, 3*tick) == -std::int64_t(tick), "Negative clock must truncate toward zero.");
    state.time_scale = 1; state.time_bias = 123;
    const auto before = advance_animation_clock(state, cache, 7*tick+13);
    change_animation_speed(state, 7*tick+13, 0.5f);
    check(state.local_time == before, "Speed adjustment unexpectedly sampled local time.");
    check(advance_animation_clock(state, cache, 7*tick+13) == before, "Speed adjustment lost phase continuity.");
    check(advance_animation_clock(state, cache, 9*tick) == before+std::int64_t(tick), "New speed was not applied.");
    auto invalid = state;
    rejects([&]{ change_animation_speed(invalid, 0, std::numeric_limits<float>::infinity()); });
    check(invalid.time_scale == state.time_scale && invalid.time_bias == state.time_bias, "Invalid speed partially changed state.");
    invalid.time_scale = std::numeric_limits<float>::quiet_NaN();
    const auto cached = cache;
    rejects([&]{ advance_animation_clock(invalid, cache, 25); });
    check(cache.world_time == cached.world_time && cache.scaled_time == cached.scaled_time, "Invalid state poisoned shared cache.");
    state = {std::numeric_limits<float>::max(), 0, 0};
    check(advance_animation_clock(state, cache, 4*tick) == -std::int64_t(tick), "Positive saturated clock wrap differs.");
    state.time_scale = -std::numeric_limits<float>::max();
    check(advance_animation_clock(state, cache, 4*tick) == 0, "Negative saturated clock wrap differs.");
    // Original static cache starts with time=-1, scale=-1, scaled value=0.
    cache = {}; state = {-1, 27, 0};
    check(advance_animation_clock(state, cache, std::numeric_limits<std::uint64_t>::max()) == 27,
        "Initial shared-cache sentinel behavior changed.");
}
void blends() {
    auto blend = prepare_animation_blend(8, false);
    check(blend.duration == 133333336 && blend.start_ratio == 0 && blend.ratio_range == 1 && blend.start_time == 1,
        "MOTIONSET did not retain original float rounding and pending sentinel.");
    auto immediate = prepare_animation_blend(-0.0f, false);
    check(immediate.duration == 1 && immediate.start_ratio == 1 && immediate.ratio_range == 0, "Zero-frame special branch.");
    immediate = prepare_animation_blend(std::numeric_limits<float>::quiet_NaN(), true);
    check(immediate.duration == 1 && immediate.start_ratio == 1, "isNoHokan must bypass the unused authored float.");
    rejects([&]{ prepare_animation_blend(std::numeric_limits<float>::quiet_NaN(), false); });
    check(prepare_animation_blend(-8, false).duration == -133333336, "Negative authored blend duration changed.");
    AnimationStateClock state{2, 999, 0}; AnimationClockCache cache;
    begin_animation_blend(state, blend, cache, 7*tick, 1234);
    check(state.local_time == 1234 && blend.start_time == 7*tick, "Blend start failed to establish its animation origin.");
    check(animation_blend_ratio(blend, 7*tick) == 0, "New blend did not start at its initial ratio.");
    check(animation_blend_ratio(blend, 7*tick+66666668) == 0.5f, "Halfway blend ratio.");
    check(animation_blend_ratio(blend, 7*tick+200000000) == 1, "Upper-only blend clamp.");
    blend = {100, 100, 0.25f, 0.5f};
    check(animation_blend_ratio(blend, 50) == 0, "Negative elapsed ratio was incorrectly clamped.");
    check(animation_blend_ratio(blend, 100) == 0.25f, "Nonzero initial blend ratio.");
    check(animation_blend_ratio(blend, 200) == 0.75f, "Ratio range was replaced by an endpoint.");
    blend.duration = 0;
    check(std::isnan(animation_blend_ratio(blend, 100)), "Zero-over-zero blend must remain NaN.");
    check(animation_blend_ratio(blend, 101) == 0.75f, "Positive infinity ratio must upper-clamp.");
    check(std::isinf(animation_blend_ratio(blend, 99)) && animation_blend_ratio(blend, 99) < 0,
        "Negative infinity ratio was replaced.");
}
void wrapping() {
    AnimationWrapContext context;
    check(context.offset() == 0, "Action-specific offset applied before initialization.");
    check(context.map(2.25f, 1, AnimationPlayMode::repeat) == 0.25f && context.cached_cycle() == 2, "Repeat remainder.");
    check(context.map(3.25f, 1, AnimationPlayMode::loop) == 0.25f, "LOOP differs from REPEAT.");
    check(context.map(3.25f, 1, AnimationPlayMode::bounce) == 0.75f, "Shared cached remainder lost bounce parity.");
    check(context.map(3.25f, 1, AnimationPlayMode::repeat) == 0.25f, "Bounce polluted shared remainder.");
    check(context.map(-0.25f, 1, AnimationPlayMode::repeat) == 0, "Negative remainder should clamp to zero.");
    check(context.map(-1.25f, 1, AnimationPlayMode::bounce) == 1, "Negative odd cycle behavior changed.");
    check(context.map(1, 1, AnimationPlayMode::clamp) == 0, "Exact CLAMP endpoint must take original remainder branch.");
    check(context.map(std::nextafter(1.0f, 2.0f), 1, AnimationPlayMode::clamp) == 1, "Strict upper clamp.");
    check(context.map(-1, 1, AnimationPlayMode::clamp) == 0, "Lower clamp.");
    context.initialize_action_offset();
    check(std::bit_cast<std::uint32_t>(context.offset()) == 0xbd088889, "Original ACTION2 offset bits.");
    const auto end = 1.0f + context.offset();
    check(context.map(2, 1, AnimationPlayMode::offset_clamp) == end, "Offset clamp endpoint.");
    check(context.map(end, 1, AnimationPlayMode::offset_clamp) == 0, "Exact offset endpoint remainder.");
    check(context.map(5, 1, static_cast<AnimationPlayMode>(99)) == 0, "Unknown mode default branch.");
    rejects([&]{ context.map(1, 0, AnimationPlayMode::repeat); });
    rejects([&]{ context.map(std::numeric_limits<float>::infinity(), 1, AnimationPlayMode::repeat); });
    rejects([&]{ context.map(1, std::numeric_limits<float>::denorm_min(), AnimationPlayMode::repeat); });
    AnimationSourceClock source(1);
    check(source.sample_time(-1, context) == -1, "Original source metadata cache sentinel changed.");
    source.set_mode(AnimationPlayMode::bounce);
    check(source.sample_time(-1, context) == -1, "Mode change invalidated the original initial cache hit.");
    check(source.sample_time(0, context) == 0 && source.sample_time(-1, context) == 0,
        "A sampled source incorrectly restored its initial cache sentinel.");
    source.set_mode(AnimationPlayMode::repeat);
    check(source.sample_time(std::int64_t(tick)-1, context) == 0, "Source sub-tick conversion.");
    check(std::bit_cast<std::uint32_t>(source.sample_time(tick, context)) == 0x3989705f, "Source's original time-unit constant.");
    source.set_mode(AnimationPlayMode::clamp);
    const auto retained = source.sample_time(2000000000, context);
    check(retained == 1, "Source clamp beyond duration.");
    source.set_mode(AnimationPlayMode::repeat);
    check(source.sample_time(2000000000, context) == retained, "Mode change incorrectly invalidated source cache.");
    check(source.sample_time(2000000001, context) < 1, "New source timestamp failed to refresh cache.");
    check(source.sample_time(std::int64_t{1} << 49, context) == 0, "Source clock must use signed coarse conversion.");
    AnimationSourceClock short_source(0, AnimationPlayMode::clamp);
    check(std::bit_cast<std::uint32_t>(short_source.sample_time(1000000000, context)) == 0x3d4cccce,
        "Short sources require original 0.05 fallback.");
    rejects([]{ AnimationSourceClock invalid(-1); });
    auto copy = source;
    copy.set_mode(AnimationPlayMode::bounce);
    check(source.mode() == AnimationPlayMode::repeat, "Independent source copies share mutable mode.");
}
} // namespace
int main() {
    try { clocks(); blends(); wrapping(); std::cout << checks << " animation timing checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
