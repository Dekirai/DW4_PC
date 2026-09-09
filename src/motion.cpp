#include "dw4/motion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace dw4 {
void MotionChannels::on_sequence_change() {
    if (!allocated_) return;
    auto candidate = states_;
    for (std::size_t i=0;i<3;++i) if (candidate[i].stop_on_sequence) stop_motion(candidate[i]);
    candidate[3].active=false; candidate[3].speed=0;
    states_=candidate;
}
namespace {
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
[[noreturn]] void fail(const char* message) { throw MotionError(message); }
void finite(float value) {
    if (!std::isfinite(value)) fail("Motion parameters and arithmetic must remain finite.");
}
void validate_mode(MotionMode mode) {
    const auto number = static_cast<std::int32_t>(mode);
    if (number < 0 || number > 5) fail("Unsupported original motion mode.");
}
void validate(const MotionState& state) {
    validate_mode(state.mode);
    // Authored parameters are finite; derived runtime fields may contain IEEE
    // infinity/NaN, including after valid original zero-frame commands.
    for (const float value : {state.value,state.stop_value,state.stop_frames}) finite(value);
}
float acceleration(MotionSetup setup) {
    validate_mode(setup.mode);
    finite(setup.value); finite(setup.frames); finite(setup.end_time);
    const float squared = setup.frames * setup.frames;
    const float denominator = squared * 0.5f;
    return setup.value / denominator;
}
void configure(MotionState& state, MotionSetup setup) {
    const auto acc = acceleration(setup);
    state.mode = setup.mode; state.value = setup.value;
    state.acceleration = acc; state.end_time = setup.end_time;
    state.stop_on_sequence = false;
    if (setup.mode == MotionMode::impulse_positive) state.speed = setup.value;
    else if (setup.mode == MotionMode::impulse_negative) state.speed = -setup.value;
    state.active = true;
}
void stop(MotionState& state) {
    state.end_time = 0;
    if (state.mode == MotionMode::accelerate_positive || state.mode == MotionMode::accelerate_negative) {
        const auto mode = state.mode == MotionMode::accelerate_positive ? MotionMode::stop_positive : MotionMode::stop_negative;
        configure(state,{mode,state.stop_value,state.stop_frames,0});
    } else { state.active = false; state.speed = 0; }
    state.stop_on_sequence = false;
}
float advance(MotionState& state, float delta) {
    if (!state.active) return 0;
    if (state.end_time != 0) {
        const float remaining = state.end_time - delta;
        if (remaining < 0) {
            stop(state);
            if (!state.active) return 0;
        } else state.end_time = remaining;
    }
    delta = std::min(delta,3.0f);
    const float change = delta * state.acceleration;
    float speed = state.speed;
    switch (state.mode) {
    case MotionMode::accelerate_positive:
        speed += change;
        if (speed > state.value) speed = state.value;
        break;
    case MotionMode::accelerate_negative:
        speed -= change;
        if (speed < -state.value) speed = -state.value;
        break;
    case MotionMode::stop_positive: case MotionMode::impulse_positive:
        speed -= change;
        if (speed < 0) { speed = 0; state.active = false; }
        break;
    case MotionMode::stop_negative: case MotionMode::impulse_negative:
        speed += change;
        if (speed > 0) { speed = 0; state.active = false; }
        break;
    }
    const float distance = speed * delta;
    state.speed = speed;
    return distance;
}
const IgbFieldValue& field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, const char* type, std::size_t size) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != size || doc.metadata.field_types.at(value->type_index).name != type)
        fail("Unsupported serialized motion field.");
    return *value;
}
float number(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    field(doc,object,slot,"igFloatMetaField",4);
    const auto value = doc.field_f32(object,slot); finite(value); return value;
}
} // namespace

void configure_motion(MotionState& state, MotionSetup setup) {
    validate(state); auto candidate = state; configure(candidate,setup); state = candidate;
}
void stop_motion(MotionState& state) {
    validate(state); auto candidate = state; stop(candidate); state = candidate;
}
float advance_motion(MotionState& state, float delta_frames) {
    validate(state); finite(delta_frames);
    auto candidate = state; const auto distance = advance(candidate,delta_frames); state = candidate; return distance;
}

MotionCommand decode_motion_command(const IgbDocument& doc, std::uint32_t reference) {
    if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("Big-endian motion records are unverified.");
    const auto& object = doc.object(reference);
    const auto& type = doc.metadata.object_types.at(object.type_index).name;
    if (type != "beModelCtrlMOVE" && type != "beModelCtrlMOVEEX") fail("Expected exact MOVE or MOVEEX object.");
    const auto* name = doc.field(object,2);
    if (!name || name->size < 4 || name->size > 72 ||
        doc.metadata.field_types.at(name->type_index).name != "igStringMetaField") fail("Unsupported motion command name schema.");
    const auto text = doc.field_string(object,2);
    if (text.size() > 64 || text.find('\0') != std::string_view::npos) fail("Invalid motion command name.");
    MotionCommand result; result.reference_index = reference; result.type = type; result.name = text;
    result.setup.value = number(doc,object,3); result.setup.frames = number(doc,object,4);
    if (type == "beModelCtrlMOVE") {
        if (text == "Move") { result.channel = 2; result.setup.mode = MotionMode::accelerate_positive; }
        else if (text == "Stop") { result.channel = 2; result.setup.mode = MotionMode::stop_positive; }
        else if (text == "Push") { result.channel = 2; result.setup.mode = MotionMode::impulse_positive; }
        else if (text == "Back") { result.channel = 2; result.setup.mode = MotionMode::impulse_negative; }
        else if (text == "Jump") { result.channel = 1; result.setup.mode = MotionMode::impulse_positive; }
        else if (text == "Fall") { result.channel = 1; result.setup.mode = MotionMode::accelerate_negative; }
    } else {
        if (text.size() < 4) fail("MOVEEX control code needs four bytes.");
        const int channel = static_cast<unsigned char>(text[1]) - 'X' + (text[0] == 'T' ? 0 : 2);
        if (channel < 0 || channel > 3) fail("MOVEEX control code selects an invalid channel.");
        int mode;
        if (text[3] == 'A') mode = 0;
        else if (text[3] == 'D') mode = 2;
        else if (text[3] == 'T') mode = 4;
        else fail("MOVEEX control code has an unverified mode.");
        if (text[2] == 'M') ++mode;
        result.channel = static_cast<std::uint32_t>(channel);
        result.setup.mode = static_cast<MotionMode>(mode);
        result.setup.end_time = number(doc,object,5);
        const auto& flag = field(doc,object,6,"igBoolMetaField",1);
        const auto enabled = doc.field_bytes(flag)[0];
        if (enabled > 1) fail("Unsupported MOVEEX sequence-stop flag.");
        result.stop = MotionStop{enabled != 0,number(doc,object,7),number(doc,object,8)};
    }
    return result;
}

void MotionChannels::apply(const MotionCommand& command) {
    if (command.type != "beModelCtrlMOVE" && command.type != "beModelCtrlMOVEEX") fail("Unsupported native motion command type.");
    auto candidate = states_;
    if (command.channel) {
        if (*command.channel >= candidate.size()) fail("Motion command channel is outside the four-channel set.");
        auto& state = candidate[*command.channel]; configure_motion(state,command.setup);
        if (command.stop) {
            finite(command.stop->value); finite(command.stop->frames);
            state.stop_on_sequence = command.stop->on_sequence;
            state.stop_value = command.stop->value; state.stop_frames = command.stop->frames;
        }
    }
    states_ = candidate; allocated_ = true;
}
MotionStep MotionChannels::advance(float delta_frames) {
    finite(delta_frames); MotionStep result;
    if (!allocated_) return result;
    auto candidate = states_;
    if (candidate[3].active) result.rotation = advance_motion(candidate[3],delta_frames);
    result.translation_updated = candidate[0].active || candidate[1].active || candidate[2].active;
    if (result.translation_updated) for (std::size_t i = 0; i < 3; ++i)
        result.translation[i] = advance_motion(candidate[i],delta_frames);
    states_ = candidate; return result;
}
MotionStep MotionChannels::advance(MotionTimer& timer, std::uint64_t world_time) {
    if (!allocated_) return {}; // Original empty channel list does not query the timer.
    auto candidate = timer;
    const auto result = advance(motion_timer_frames(candidate,world_time));
    timer = candidate; return result;
}
void translate_model_local(std::array<float,16>& matrix, const std::array<float,3>& translation) {
    for (const auto value : matrix) finite(value);
    for (const auto value : translation) finite(value);
    auto candidate = matrix;
    for (std::size_t row = 0; row < 4; ++row) {
        const float y = matrix[4+row] * translation[1]; finite(y);
        const float xy = std::fma(matrix[row],translation[0],y); finite(xy);
        const float xyz = std::fma(matrix[8+row],translation[2],xy); finite(xyz);
        candidate[12+row] = matrix[12+row] + xyz; finite(candidate[12+row]);
    }
    matrix = candidate;
}
} // namespace dw4
