#pragma once

#include "dw4/motion.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace dw4 {
// Caller-prepared beModelCtrl work fields at 80/8C/98: desired Euler angles,
// per-axis rates and previously applied Euler angles. All angles are radians.
// This does not construct the original player or choose its initial turn rates.
struct ModelTurn {
    std::array<float, 3> desired{}, rate{}, applied{};
    bool operator==(const ModelTurn&) const = default;
};
// Canonical work fields only. The actual node matrix belongs to a separate
// node owner and is supplied when a pose view is bound.
struct ModelMotionFields {
    ModelTurn turn;
    std::uint8_t translated{},position_locked{}; // Work+78/+A8; preserve raw bytes.
    bool operator==(const ModelMotionFields&) const = default;
};
struct ModelMotionPose {
    using Matrix=std::array<float,16>;
    explicit ModelMotionPose(Matrix model_matrix);
    // A generated work object can share its actual node matrix with an older
    // retained work object, while its turn and control fields stay independent.
    explicit ModelMotionPose(std::shared_ptr<Matrix> node_matrix);
    ModelMotionPose(std::shared_ptr<Matrix> node_matrix,std::shared_ptr<ModelMotionFields> fields);
    // Entry/control consumers can bind captured work without reading ONE.node.
    // Matrix consumers still require an explicit real matrix at their own site.
    static ModelMotionPose fields_only(std::shared_ptr<ModelMotionFields> fields);
    // Candidate copies are detached: failed validation must not write the live
    // node. Assignment publishes values into the destination's existing owner.
    ModelMotionPose(const ModelMotionPose&);
    ModelMotionPose(ModelMotionPose&&) noexcept = default;
    ModelMotionPose& operator=(const ModelMotionPose&);
    ModelMotionPose& operator=(ModelMotionPose&&);
    Matrix& matrix();
    const Matrix& matrix() const;
    std::shared_ptr<Matrix> matrix_owner();
    std::shared_ptr<const Matrix> matrix_owner() const;
    std::shared_ptr<ModelMotionFields> fields_owner();
    std::shared_ptr<const ModelMotionFields> fields_owner() const;
    ModelTurn& turn();
    const ModelTurn& turn() const;
    std::uint8_t& translated();
    const std::uint8_t& translated() const;
    std::uint8_t& position_locked();
    const std::uint8_t& position_locked() const;
    bool operator==(const ModelMotionPose&) const;
private:
    ModelMotionPose(std::shared_ptr<ModelMotionFields>,std::nullptr_t);
    std::shared_ptr<Matrix> matrix_;
    std::shared_ptr<ModelMotionFields> fields_;
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
// Actual named RAM access: each eligible axis independently resolves its timer.
// Completed timer writes survive subsequent checked motion/matrix failures.
// Force, equal-angle and zero-rate gates do not access the timer endpoint.
bool turn_model(ModelMotionPose& pose, ModelRamTimerAccess& timer, bool force = false);
// The force=1 path without an invented KEIKATIMER. Rebuild from desired angles
// and synchronize applied angles while preserving translation and pose flags.
void force_model_turn(ModelMotionPose& pose);
// Player preparation stores (0,7,0) at 8034D788..8034D790. This only initializes
// the turn rates; it does not allocate an original player or prepare its model.
void set_player_turn_rates(ModelMotionPose& pose);
// Transform original +Z by the model basis using the recovered FMA order, then
// store float(atan2(forwardX,forwardZ)) in desired/applied Y. Other fields and the
// complete matrix are preserved. Host atan2 has the same libm limit as turning.
void synchronize_model_heading(ModelMotionPose& pose);
} // namespace dw4
