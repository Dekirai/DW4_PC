#pragma once

// Host devices -> GameCube pad input: the keyboard and controller bindings of
// settings.ini and a controller's sticks become a dw4::HostPadInput, which
// dw4::host_pad_sample turns into the original device's sample. The main
// stick, the D-pad and the C-stick stay separate inputs (pad.hpp).
#include "settings.hpp"

#include "dw4/pad.hpp"

#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstdint>

namespace dw4::game {

// One controller's state as SDL reports it (a snapshot, so the mapping can be
// checked without a device).
struct GamepadState {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
};
GamepadState read_gamepad(SDL_Gamepad* pad);

// A trigger bound to a control counts as pressed past half way.
inline constexpr std::int16_t kTriggerPressed = 16000;
// Stick axes inside 20 % of full deflection count as centred.
inline constexpr float kStickDeadzone = 0.2f;

// The keyboard through the [keyboard] bindings; `keys` is SDL's keyboard
// state (SDL_SCANCODE_COUNT entries).
HostPadInput keyboard_input(const Settings& settings, const bool* keys);
// A controller through the [gamepad] bindings: its left stick is the main
// stick and its right stick the C-stick; its D-pad is the D-pad only (unless
// bound to stick directions).
HostPadInput gamepad_input(const Settings& settings, const GamepadState& pad);

} // namespace dw4::game
