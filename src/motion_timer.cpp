#include "dw4/motion_timer.hpp"
#include "dw4/motion.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
void validate(const MotionTimer& timer) {
    if (!std::isfinite(timer.time_scale) || !std::isfinite(timer.cached_frames))
        throw MotionError("Motion timer scale and cached frames must be finite.");
}
std::uint64_t scaled_clock(std::uint64_t world_time, float scale) {
    const auto coarse = static_cast<std::uint32_t>(world_time >> 18);
    const float clock = static_cast<float>(coarse);
    const float scaled = clock * scale;
    // __cvt_dbl_usll at 800A4130 actually applies signed saturation and
    // truncation here, including overflow to infinity from finite scale.
    // Avoid undefined host casts at the signed 64-bit limits.
    std::int64_t integer;
    if (scaled >= 0x1p63f) integer = std::numeric_limits<std::int64_t>::max();
    else if (scaled <= -0x1p63f) integer = std::numeric_limits<std::int64_t>::min();
    else integer = static_cast<std::int64_t>(scaled);
    return std::bit_cast<std::uint64_t>(integer) << 18;
}
} // namespace

void start_motion_timer(MotionTimer& timer, std::uint64_t world_time, std::int64_t end_time) {
    validate(timer);
    const auto clock = scaled_clock(world_time,timer.time_scale);
    timer.time_bias = std::bit_cast<std::int64_t>(std::uint64_t{0} - clock);
    timer.end_time = end_time; timer.saved_world_time = world_time; timer.cached_frames = 1;
}
std::int64_t motion_timer_time(const MotionTimer& timer, std::uint64_t world_time) {
    validate(timer);
    return std::bit_cast<std::int64_t>(scaled_clock(world_time,timer.time_scale) +
        std::bit_cast<std::uint64_t>(timer.time_bias));
}
float motion_timer_frames(MotionTimer& timer, std::uint64_t world_time) {
    validate(timer);
    if (timer.saved_world_time == world_time) return timer.cached_frames;
    const auto current = motion_timer_time(timer,world_time);
    const auto elapsed = std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(current) -
        std::bit_cast<std::uint64_t>(timer.previous_time));
    if (elapsed != 0) {
        const float converted = static_cast<float>(static_cast<double>(elapsed));
        timer.cached_frames = std::min(converted / 16666667.0f,3.0f);
        timer.previous_time = current;
    } else timer.cached_frames = 0;
    timer.saved_world_time = world_time;
    return timer.cached_frames;
}
bool motion_timer_expired(const MotionTimer& timer, std::uint64_t world_time) {
    return timer.end_time < motion_timer_time(timer,world_time);
}
void change_motion_timer_speed(MotionTimer& timer, std::uint64_t world_time, float speed) {
    validate(timer);
    if (!std::isfinite(speed)) throw MotionError("Motion timer speed must be finite.");
    if (timer.time_scale==speed) return;
    const auto bias=std::bit_cast<std::uint64_t>(timer.time_bias)+scaled_clock(world_time,timer.time_scale)-scaled_clock(world_time,speed);
    timer.time_bias=std::bit_cast<std::int64_t>(bias); timer.time_scale=speed; timer.cached_frames=0;
}
} // namespace dw4
