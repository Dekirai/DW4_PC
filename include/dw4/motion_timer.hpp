#pragma once

#include <cstdint>

namespace dw4 {
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
