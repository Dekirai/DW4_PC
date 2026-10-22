#include "controls.hpp"

#include <SDL3/SDL_keyboard.h>

#include <cmath>

namespace dw4::game {

GamepadState read_gamepad(SDL_Gamepad* pad) {
    GamepadState state;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) state.buttons[std::size_t(b)] = SDL_GetGamepadButton(pad, SDL_GamepadButton(b));
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) state.axes[std::size_t(a)] = SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a));
    return state;
}

HostPadInput keyboard_input(const Settings& settings, const bool* keys) {
    HostPadInput input;
    for (std::size_t b = 0; b < settings.keys.size(); ++b)
        for (const SDL_Scancode code : settings.keys[b])
            if (code > SDL_SCANCODE_UNKNOWN && code < SDL_SCANCODE_COUNT && keys[code]) press(PadKey(b), input);
    return input;
}

HostPadInput gamepad_input(const Settings& settings, const GamepadState& pad) {
    HostPadInput input;
    for (std::size_t b = 0; b < settings.pad.size(); ++b)
        for (const auto& bound : settings.pad[b]) {
            bool down = false;
            if (bound.kind == PadInput::Kind::button && bound.code >= 0 && bound.code < SDL_GAMEPAD_BUTTON_COUNT)
                down = pad.buttons[std::size_t(bound.code)];
            else if (bound.kind == PadInput::Kind::axis && bound.code >= 0 && bound.code < SDL_GAMEPAD_AXIS_COUNT)
                down = pad.axes[std::size_t(bound.code)] > kTriggerPressed;
            if (down) press(PadKey(b), input);
        }
    auto axis = [&](SDL_GamepadAxis a, float sign) {
        const float v = sign * pad.axes[std::size_t(a)] / 32767.0f;
        return std::abs(v) > kStickDeadzone ? v : 0.0f;
    };
    input.stick = {axis(SDL_GAMEPAD_AXIS_LEFTX, 1.0f), axis(SDL_GAMEPAD_AXIS_LEFTY, -1.0f)};
    input.c_stick = {axis(SDL_GAMEPAD_AXIS_RIGHTX, 1.0f), axis(SDL_GAMEPAD_AXIS_RIGHTY, -1.0f)};
    return input;
}

} // namespace dw4::game
