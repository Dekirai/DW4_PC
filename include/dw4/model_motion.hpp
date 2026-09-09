#pragma once

#include "dw4/motion.hpp"

#include <array>
#include <cstdint>

namespace dw4 {
// Caller-prepared beModelCtrl work fields at 80/8C/98: desired Euler angles,
// per-axis rates and previously applied Euler angles. All angles are radians.
// This does not construct the original player or choose its initial turn rates.
struct ModelTurn {
    std::array<float, 3> desired{}, rate{}, applied{};
    bool operator==(const ModelTurn&) const = default;
};
struct ModelMotionPose {
    explicit ModelMotionPose(std::array<float, 16> model_matrix) : matrix(model_matrix) {}
    std::array<float, 16> matrix; // Owned ONE model transform, column-major.
    ModelTurn turn;
    bool translated{}; // Original work+78 (_isHitCheck), written by the motion tick.
    bool position_locked{}; // Original work+A8 (_isPosLock), cleared by MOVE/MOVEEX.
    bool operator==(const ModelMotionPose&) const = default;
};
struct ModelMotionStep {
    MotionStep channels;
    bool turned{};
};

// 8030C85C: skip equal axes and zero rates; otherwise use KEIKATIMER, repeated
// float tau wrapping and the shortest arc (negative direction on a tie).
// Force applies desired angles directly without reading the timer or rates.
// Rebuilds the original Euler basis, resets its bottom row, preserves position,
// then publishes applied angles. Returns whether that matrix path ran.
// Exactly batches repeated float wrapping within eligible exponent bins.
// Native guard: at most 4096 iterations per call; non-finite used parameters,
// stagnant original loops or exhausted work stop with pose/timer unchanged.
// sin/cos use double host libm rounded to float; see MODEL_MOTION.md for limits.
bool turn_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time, bool force = false);
// Player preparation stores (0,7,0) at 8034D788..8034D790. This only initializes
// the turn rates; it does not allocate an original player or prepare its model.
void set_player_turn_rates(ModelMotionPose& pose) noexcept;
// Transform original +Z by the model basis using the recovered FMA order, then
// store float(atan2(forwardX,forwardZ)) in desired/applied Y. Other fields and the
// complete matrix are preserved. Host atan2 has the same libm limit as turning.
void synchronize_model_heading(ModelMotionPose& pose);
} // namespace dw4
