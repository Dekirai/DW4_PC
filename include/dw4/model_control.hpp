#pragma once

#include "dw4/model_motion.hpp"

#include <optional>
#include <string_view>
#include <variant>
#include <utility>

namespace dw4 {
struct ModelCtrlCommand {
    std::uint32_t reference_index{};
    std::string name;
};
struct PadMotionCommand {
    std::uint32_t reference_index{};
    std::string name;
    float value{}, frames{};
};
struct ModelMutekiCommand {
    std::uint32_t reference_index{};
    std::string name;
    float frames{};
};
using ModelCommand = std::variant<MotionCommand,ModelCtrlCommand,PadMotionCommand,ModelMutekiCommand>;
// Exact MOVE/MOVEEX/CTRL/PDMOVE/MUTEKI records only. Unknown CTRL names are successful
// no-ops; PDMOVE selects Move parameters only for the exact name "Move".
ModelCommand decode_model_command(const IgbDocument& document, std::uint32_t reference_index);

// Owned motion/control fields for one explicitly prepared ONE model. This is
// not the complete beModelCtrlInfoWork or original player construction routine.
struct ModelControlState {
    explicit ModelControlState(ModelMotionPose prepared_pose) : pose(std::move(prepared_pose)) {}
    ModelMotionPose pose;
    MotionChannels channels;
    std::optional<MotionTimer> elapsed_timer; // This RAM's named KEIKATIMER.
    std::int32_t mode{}, phase{}; // _ctrlSt0 / _ctrlSt1, work+3C/+40.
    std::optional<std::array<float,4>> pad_motion; // Stop value/frame, Move value/frame.
    bool check_fall = true; // _isFallCheck, work+DA; CTRL changes the flag only.
    std::optional<std::string> bullet_hit_script,wall_hit_script,floor_hit_script; // Work+30/34/38; null differs from empty.
    std::uint8_t wall_check=1,floor_check=1; // Work+D8/D9.
    float invulnerability_frames{}; // Work+7C; negative persists until ordinary entry.
};
// MOVE/MOVEEX: ensure the timer, release position lock and synchronize heading,
// then configure the channels. MUTEKI ensures that same timer, then changes
// wall/floor flags or assigns its frame value. Existing timers never restart.
// CTRL and PDMOVE have no implicit timer/motion.
// Validation failures preserve the complete owned state.
void apply_model_command(ModelControlState& state, const ModelCommand& command, std::uint64_t world_time);

struct ModelControlDecision {
    std::string requested_sequence;
    bool dispatch_sequence{};
};
// 80309168 after the original input callback: preserve an existing request;
// otherwise apply modes 1/2/3. key_input is the original byte, including its
// distinct !=0 and ==1 branches. The caller supplies current/requested names
// after frame reset/input. A dispatch result still requires the real sequence
// callback; this function never pretends to execute its animation/actions.
ModelControlDecision update_model_control(ModelControlState& state, std::uint8_t key_input,
    std::string_view current_sequence, std::string_view requested_sequence);
// Motion part only, after control/sequence dispatch. Does not execute the outer
// attachment/position-lock, action wait, collision or player update callbacks.
ModelMotionStep advance_model_motion(ModelControlState& state, std::uint64_t world_time);
} // namespace dw4
