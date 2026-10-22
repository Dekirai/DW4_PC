#include "dw4/pad.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dw4 {
namespace {

void validate(PadStick stick) {
    if (!std::isfinite(stick.x) || !std::isfinite(stick.y) ||
        std::abs(stick.x) > 2 || std::abs(stick.y) > 2) {
        throw std::invalid_argument("Original pad axes must be finite and within [-2,2]");
    }
}

std::uint32_t directions(PadStick stick) {
    // Match the separate single-precision products/addition at 80311AA4-B8.
    const float xx = stick.x * stick.x;
    const float yy = stick.y * stick.y;
    const float squared_length = xx + yy;
    if (std::sqrt(squared_length) < 0.4f) return 0;

    // The guest calls double atan2(y,x), then rounds its result to float.
    const float angle = static_cast<float>(std::atan2(
        static_cast<double>(stick.y), static_cast<double>(stick.x)));
    constexpr float quarter_pi = 0x1.921fb6p-1f; // 80426BC8, 0x3F490FDB.
    constexpr float three_quarter_pi = 0x1.2d97c8p+1f; // 80426BD8.
    constexpr float half_overlap = 0.5f * quarter_pi;
    std::uint32_t result{};
    if (angle < -three_quarter_pi + half_overlap ||
        angle > three_quarter_pi - half_overlap) result |= pad_direction::negative_x;
    if (angle >= -three_quarter_pi - half_overlap &&
        angle <= -quarter_pi + half_overlap) result |= pad_direction::negative_y;
    if (angle > -quarter_pi - half_overlap &&
        angle < quarter_pi + half_overlap) result |= pad_direction::positive_x;
    if (angle >= quarter_pi - half_overlap &&
        angle <= three_quarter_pi + half_overlap) result |= pad_direction::positive_y;
    return result;
}

void update_mask(PadMaskState& state, std::uint32_t held) noexcept {
    state.previous = state.held;
    state.held = held;
    state.trigger = (held ^ state.previous) & held;
    if (held != state.previous) {
        state.repeat = state.trigger;
        state.repeat_countdown = 10;
    } else {
        --state.repeat_countdown;
        if (state.repeat_countdown == 0) {
            state.repeat = held;
            state.repeat_countdown = 6;
        } else {
            state.repeat = 0;
        }
    }
}

} // namespace

std::uint32_t gamecube_buttons(std::uint16_t raw_buttons) noexcept {
    constexpr std::array<std::uint32_t, 16> mapping{
        pad_button::left, pad_button::right, pad_button::down, pad_button::up,
        pad_button::z, pad_button::r, pad_button::l, 0,
        pad_button::a, pad_button::b, pad_button::x, pad_button::y,
        pad_button::start, 0, 0, 0
    };
    std::uint32_t result{};
    for (std::size_t bit = 0; bit < mapping.size(); ++bit) {
        if ((raw_buttons & (1u << bit)) != 0) result |= mapping[bit];
    }
    return result;
}

void merge(HostPadInput& into, const HostPadInput& other) noexcept {
    into.buttons |= other.buttons;
    into.stick_directions |= other.stick_directions;
    into.c_stick_directions |= other.c_stick_directions;
    if (other.stick.x != 0.0f || other.stick.y != 0.0f) into.stick = other.stick;
    if (other.c_stick.x != 0.0f || other.c_stick.y != 0.0f) into.c_stick = other.c_stick;
}

PadSample host_pad_sample(const HostPadInput& input) noexcept {
    auto stick = [](PadStick analog, std::uint32_t directions, float full) {
        if (analog.x == 0.0f && analog.y == 0.0f) {
            // Opposite directions held together: right beats left and down
            // beats up, as the port's direction buttons always did.
            if (directions & pad_direction::negative_x) analog.x = -1.0f;
            if (directions & pad_direction::positive_x) analog.x = 1.0f;
            if (directions & pad_direction::positive_y) analog.y = 1.0f;
            if (directions & pad_direction::negative_y) analog.y = -1.0f;
        }
        const auto axis = [full](float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) * full : 0.0f; };
        return PadStick{axis(analog.x), axis(analog.y)};
    };
    PadSample sample;
    sample.connected = true;
    sample.buttons = input.buttons;
    sample.left = stick(input.stick, input.stick_directions, 72.0f / 74.0f);
    sample.right = stick(input.c_stick, input.c_stick_directions, 59.0f / 74.0f);
    return sample;
}

PadStick gamecube_stick(std::int8_t x, std::int8_t y, bool c_stick) noexcept {
    const int sign_x = x < 0 ? -1 : 1;
    const int sign_y = y < 0 ? -1 : 1;
    int ax = std::max(0, std::abs(static_cast<int>(x)) - 15);
    int ay = std::max(0, std::abs(static_cast<int>(y)) - 15);
    const int axis = c_stick ? 59 : 72;
    const int diagonal = c_stick ? 31 : 40;
    const int maximum = axis * diagonal;
    const int divisor = diagonal * std::max(ax, ay) + (axis - diagonal) * std::min(ax, ay);
    if (divisor > maximum) {
        ax = ax * maximum / divisor;
        ay = ay * maximum / divisor;
    }
    return {static_cast<float>(sign_x * ax) / 74.0f,
        static_cast<float>(sign_y * ay) / 74.0f};
}

void OriginalPads::update(const Samples& samples, bool frozen) {
    if (frozen) return;
    for (const auto& sample : samples) {
        if (sample.connected) {
            validate(sample.left);
            validate(sample.right);
        }
    }
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        const auto& sample = samples[slot];
        auto& state = states_[slot];
        state.connected = sample.connected;
        state.left = sample.connected ? sample.left : PadStick{};
        state.right = sample.connected ? sample.right : PadStick{};
        update_mask(state.left_directions, directions(state.left));
        update_mask(state.right_directions, directions(state.right));
        update_mask(state.buttons, sample.connected ? sample.buttons : 0);
    }
}

} // namespace dw4
