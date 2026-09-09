#pragma once

#include "dw4/igb_document.hpp"
#include "dw4/motion_timer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace dw4 {
struct ModelMotionPose;
struct ModelMotionStep;
class MotionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class MotionMode : std::int32_t {
    accelerate_positive = 0, accelerate_negative = 1,
    stop_positive = 2, stop_negative = 3,
    impulse_positive = 4, impulse_negative = 5
};

// Owned beModelCtrlMoveObject fields. Values and time use original nominal
// frame units, not seconds. Stop parameters are consumed only by stop_motion.
struct MotionState {
    bool active{};
    float end_time{};
    MotionMode mode{};
    float value{}, acceleration{}, speed{};
    bool stop_on_sequence{};
    float stop_value{}, stop_frames{};
    bool operator==(const MotionState&) const = default;
};
struct MotionSetup {
    MotionMode mode{};
    float value{}, frames{}, end_time{};
};
struct MotionStop {
    bool on_sequence{};
    float value{}, frames{};
};

// GDJEB2 8030A1F4 / 8030A254 / 8030A2D8. Configure preserves speed in
// modes 0..3 and stored stop parameters. Stop changes 0->2 or 1->3; other
// modes deactivate immediately. Advance subtracts expiry time BEFORE its
// delta<=3 clamp and integrates the updated speed. Exact zero does not expire
// or deactivate. Signed finite parameters/deltas keep their original meaning.
// Finite zero-frame parameters are valid: preserve derived infinity/NaN and
// ordered comparisons (NaN does not trigger a speed clamp or zero crossing).
// Invalid modes or non-finite authored parameters/deltas throw, preserving
// state. NaN payloads, FP exception flags and guest subnormal modes are not
// modeled. Check a displacement before passing it to a rendering boundary.
void configure_motion(MotionState& state, MotionSetup setup);
void stop_motion(MotionState& state);
float advance_motion(MotionState& state, float delta_frames);

struct MotionCommand {
    std::uint32_t reference_index{};
    std::string type, name;
    // Unknown names on exact MOVE are verified no-ops. MOVEEX validates its
    // four-byte control code before deriving channel/mode from the original.
    std::optional<std::uint32_t> channel;
    MotionSetup setup;
    std::optional<MotionStop> stop;
};
// Reads one exact beModelCtrlMOVE or beModelCtrlMOVEEX object, never infers a
// command from names alone. Bounded local little-endian schema, name<=64 bytes.
// This is parameter decoding, not sequence selection or whole handler startup.
MotionCommand decode_motion_command(const IgbDocument& document, std::uint32_t reference);

struct MotionStep {
    std::array<float, 3> translation{}; // Original local X/Y/Z displacement.
    float rotation{}; // Channel 3 scalar displacement, before degrees->radians.
    bool translation_updated{}; // At least one translation channel began active.
};
class MotionChannels {
public:
    // Applies only the recovered channel effects of 8030A6BC. The caller must
    // separately establish its timer, model work and attachment lifecycle.
    void apply(const MotionCommand& command);
    MotionStep advance(float delta_frames); // Original order: rotation, X, Y, Z.
    MotionStep advance(MotionTimer& timer, std::uint64_t world_time);
    // 8030A448: rotation channel, model turning, then local translation. Uses
    // an explicit owned model pose; no attachment, collision or trace callbacks.
    ModelMotionStep advance_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time);
    // 8030EA1C..8030EA74: stop flagged XYZ channels using their stored stop
    // parameters; always deactivate rotation and clear only its speed.
    void on_sequence_change();
    const std::array<MotionState, 4>& states() const noexcept { return states_; }
    bool allocated() const noexcept { return allocated_; }
private:
    std::array<MotionState, 4> states_{};
    bool allocated_{};
};

// 80128DC8: column-major 4x4, including the fourth translation component.
// Retains model-basis scale/rotation and original Y, fused X, fused Z, add-T
// arithmetic order. This performs no collision response or matrix normalization.
// This native matrix boundary rejects non-finite inputs/results atomically;
// the scalar state above preserves the original arithmetic that produced them.
void translate_model_local(std::array<float, 16>& matrix, const std::array<float, 3>& translation);
} // namespace dw4
