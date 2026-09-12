#pragma once

#include <cstdint>

namespace dw4 {
// 800A3FDC then scalar division by 16666667.0f: signed nanoseconds ->
// nominal-frame units. Preserve the original int64 -> binary64 -> binary32
// conversion stages; this does not clamp elapsed time or own a timer.
float original_elapsed_frames(std::int64_t elapsed);
// beSystem 80316320..8031635C: wrapping world timestamp difference followed
// by original_elapsed_frames. Uncapped; distinct from a named MotionTimer.
float original_system_frame_delta(std::uint64_t current,std::uint64_t previous);

// Recovered beBaseInfoRamTimer fields; addresses and arithmetic are in
// MOTION.md. Time values use guest nanosecond units. This is per-RAM timer
// state, not an OS clock, pause controller, named dictionary or scheduler.
struct MotionTimer {
    float time_scale = 1;
    std::int64_t time_bias{}, end_time{};
    std::uint64_t saved_world_time{};
    std::int64_t previous_time{};
    float cached_frames{};
    bool operator==(const MotionTimer&) const = default;
};

// Field-specific numerical stages for actual named helpers. These do not
// inspect unrelated cache fields or own any RAM/list/clock service.
// Scale must be finite; finite multiplication overflow keeps the original
// signed saturation. The result is the wrapping scaled coarse clock.
std::uint64_t original_scaled_motion_clock(std::uint64_t world_time,float scale);
// 8031E5D4..8031E638: accumulated time may come from a fresh named lookup on a
// different timer. Read/write only the outer captured timer's previous/cache/
// saved fields and publish the earlier W1. No scale/end/bias access.
float publish_motion_timer_frames(MotionTimer& captured,std::int64_t accumulated_time,std::uint64_t world_time);

// 8031DE44: create/reset the named timer's recovered fields. Preserves scale
// and previous_time when reusing an existing timer. The initial cached result
// is ONE frame, including immediate queries at the reset timestamp.
void start_motion_timer(MotionTimer& timer, std::uint64_t world_time, std::int64_t end_time = 0);
// 8031E36C: original coarse clock, scale and bias, with wrapping 64-bit math.
// It retains the original low 32 bits of world_time>>18 and float rounding.
std::int64_t motion_timer_time(const MotionTimer& timer, std::uint64_t world_time);
// 8031E4C4: repeated queries at the same world timestamp return cached_frames.
// Fresh signed elapsed time is converted through double then float, divided
// by 16666667.0f, and capped above at 3. Negative deltas are not clamped away.
float motion_timer_frames(MotionTimer& timer, std::uint64_t world_time);
bool motion_timer_expired(const MotionTimer& timer, std::uint64_t world_time);
// 8031E654: preserve phase when changing a named timer's speed. On a real
// change the cached frame result becomes zero; timestamps remain untouched.
void change_motion_timer_speed(MotionTimer& timer, std::uint64_t world_time, float speed);
} // namespace dw4
