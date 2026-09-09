#include "dw4/model_motion.hpp"

#include <cmath>
#include <cstddef>
#include <bit>

namespace dw4 {
namespace {
constexpr float tau = 0x1.921fb6p+2f; // GDJEB2 804268DC.
constexpr float radians_per_degree = 0x1.1df46ap-6f; // 80426804.
void finite(float value) {
    if (!std::isfinite(value)) throw MotionError("Model motion cannot publish a non-finite pose.");
}
float wrap(float angle, std::size_t& remaining) {
    finite(angle);
    while (angle < 0 || angle >= tau) {
        if (remaining == 0) throw MotionError("Original model angle wrapping exceeded its native operation budget.");
        --remaining;
        const float next = angle < 0 ? angle + tau : angle - tau;
        if (next == angle) throw MotionError("Original model angle wrapping cannot advance at this float magnitude.");
        // Within a float exponent bin >= 4, subtracting this odd-mantissa tau
        // rounds to one fixed integer ULP decrement (no half-ULP ties). Batch
        // those identical steps with integer mantissas, leaving a full step
        // before the exponent boundary. The smaller/tie bins use actual adds.
        // This preserves repeated float rounding even after millions of turns;
        // replacing the loop with fmod would change the resulting angle.
        const auto current_bits = std::bit_cast<std::uint32_t>(angle);
        const auto next_bits = std::bit_cast<std::uint32_t>(next);
        const auto magnitude = current_bits & 0x7fffffffu;
        const auto next_magnitude = next_bits & 0x7fffffffu;
        const auto exponent = magnitude & 0x7f800000u;
        if (exponent >= 0x41800000u && exponent == (next_magnitude & 0x7f800000u) &&
            ((current_bits ^ next_bits) & 0x80000000u) == 0) {
            const auto decrement = magnitude - next_magnitude;
            const auto count = (magnitude - exponent) / decrement;
            if (count > 1) {
                angle = std::bit_cast<float>((current_bits & 0x80000000u) | (magnitude - (count-1)*decrement));
                continue;
            }
        }
        angle = next;
    }
    return angle;
}
float interpolate(float current, float desired, float step, std::size_t& remaining) {
    current = wrap(current,remaining); desired = wrap(desired,remaining);
    float forward, backward;
    if (current <= desired) {
        const float previous_target = desired - tau;
        forward = desired - current; backward = current - previous_target;
    } else {
        const float next_target = desired + tau;
        backward = current - desired; forward = next_target - current;
    }
    if (forward < backward) return forward > step ? current + step : desired;
    return backward > step ? current - step : desired;
}
void euler_basis(std::array<float,16>& matrix, const std::array<float,3>& angles) {
    for (const auto value : angles) finite(value);
    for (std::size_t i=12;i<15;++i) finite(matrix[i]);
    // 801292D4 special-cases exact zero before calling double sin/cos. Keep
    // those branches, their positive-zero products and the original FMA order.
    const float sx = angles[0] == 0 ? 0 : static_cast<float>(std::sin(static_cast<double>(angles[0])));
    const float cx = angles[0] == 0 ? 1 : static_cast<float>(std::cos(static_cast<double>(angles[0])));
    const float sy = angles[1] == 0 ? 0 : static_cast<float>(std::sin(static_cast<double>(angles[1])));
    const float cy = angles[1] == 0 ? 1 : static_cast<float>(std::cos(static_cast<double>(angles[1])));
    const float sxsy = angles[1] == 0 ? 0 : sx * sy;
    const float sz = angles[2] == 0 ? 0 : static_cast<float>(std::sin(static_cast<double>(angles[2])));
    const float cz = angles[2] == 0 ? 1 : static_cast<float>(std::cos(static_cast<double>(angles[2])));
    const float cxsz = angles[2] == 0 ? 0 : cx * sz;
    const float cxcz = cx * cz, sxsz = sx * sz, sxcz = sx * cz;
    matrix[0] = cy * cz; matrix[1] = cy * sz; matrix[2] = -sy;
    matrix[4] = std::fma(sxsy,cz,-cxsz);
    matrix[5] = std::fma(sxsy,sz,cxcz); matrix[6] = sx * cy;
    matrix[8] = std::fma(cxcz,sy,sxsz);
    matrix[9] = std::fma(cxsz,sy,-sxcz); matrix[10] = cx * cy;
    // 80129134 resets these components from globals initialized at 8012F57C;
    // 8030C85C then restores only translation XYZ with 80128D90.
    matrix[3] = 0; matrix[7] = 0; matrix[11] = 0; matrix[15] = 1;
    for (const auto value : matrix) finite(value);
}
} // namespace

void set_player_turn_rates(ModelMotionPose& pose) noexcept {
    pose.turn.rate = {0,7,0}; // 80456F68 and 8045700C, already radians/frame.
}
void synchronize_model_heading(ModelMotionPose& pose) {
    // 8012D434 transforms (0,0,1) without translation. Retain zero products:
    // directly copying the Z column changes signed-zero and non-finite cases.
    std::array<float,3> forward;
    for (std::size_t row=0;row<3;++row) {
        const float y = 0.0f * pose.matrix[4+row];
        const float xy = std::fma(0.0f,pose.matrix[row],y);
        forward[row] = std::fma(1.0f,pose.matrix[8+row],xy);
    }
    // The caller uses only X/Z; the original temporary Y is not published.
    finite(forward[0]); finite(forward[2]);
    const float heading = static_cast<float>(std::atan2(static_cast<double>(forward[0]),
        static_cast<double>(forward[2])));
    finite(heading);
    pose.turn.desired[1] = heading; pose.turn.applied[1] = heading;
}

bool turn_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time, bool force) {
    auto candidate_timer = timer;
    auto angles = pose.turn.desired;
    if (!force) {
        std::size_t remaining = 4096;
        for (std::size_t i=0;i<3;++i) {
            if (pose.turn.applied[i] == pose.turn.desired[i]) continue;
            if (pose.turn.rate[i] == 0) angles[i] = pose.turn.applied[i];
            else {
                finite(pose.turn.rate[i]);
                const float delta = motion_timer_frames(candidate_timer,world_time);
                const float step = pose.turn.rate[i] * delta;
                angles[i] = interpolate(pose.turn.applied[i],pose.turn.desired[i],step,remaining);
            }
        }
        if (angles == pose.turn.applied) { timer = candidate_timer; return false; }
    }
    auto matrix = pose.matrix; euler_basis(matrix,angles);
    pose.matrix = matrix; pose.turn.applied = angles; timer = candidate_timer;
    return true;
}

ModelMotionStep MotionChannels::advance_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time) {
    ModelMotionStep result;
    if (!allocated_) { pose.translated = false; return result; }
    auto candidate_channels = *this; auto candidate_pose = pose; auto candidate_timer = timer;
    candidate_pose.translated = false;
    const float delta = motion_timer_frames(candidate_timer,world_time);
    auto& states = candidate_channels.states_;
    if (states[3].active) {
        result.channels.rotation = advance_motion(states[3],delta);
        const float radians = radians_per_degree * result.channels.rotation;
        candidate_pose.turn.desired[1] = radians + candidate_pose.turn.desired[1];
        // Preserve the PPC ordered branch: -0 stays -0, unordered stays
        // unordered. MSVC folds the float comparison/negation into fabs even
        // with /fp:strict; expressing the IEEE classification in bits avoids it.
        auto magnitude_bits = std::bit_cast<std::uint32_t>(radians);
        if (magnitude_bits > 0x80000000u && magnitude_bits <= 0xff800000u)
            magnitude_bits ^= 0x80000000u;
        const float magnitude = std::bit_cast<float>(magnitude_bits);
        candidate_pose.turn.rate[1] = 1.25f * magnitude;
    }
    result.turned = turn_model(candidate_pose,candidate_timer,world_time);
    result.channels.translation_updated = states[0].active || states[1].active || states[2].active;
    if (result.channels.translation_updated) {
        for (std::size_t i=0;i<3;++i) result.channels.translation[i] = advance_motion(states[i],delta);
        translate_model_local(candidate_pose.matrix,result.channels.translation);
        candidate_pose.translated = true;
    }
    *this = candidate_channels; pose = candidate_pose; timer = candidate_timer;
    return result;
}
} // namespace dw4
