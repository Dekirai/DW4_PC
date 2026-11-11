#include "pad_host.hpp"

#include <algorithm>

namespace dw4::game {

SDL_Gamepad* pad_by_id(const std::vector<SDL_Gamepad*>& gamepads, SDL_JoystickID id) {
    for (auto* pad : gamepads)
        if (SDL_GetGamepadID(pad) == id) return pad;
    return nullptr;
}

bool pad_slots_event(std::vector<SDL_Gamepad*>& gamepads, PromptTracker& tracker, const SDL_Event& event) {
    // Real input: the prompts follow player 1's last device.
    if (const auto kind = real_input(event); kind == InputKind::keyboard) tracker.key_input();
    else if (kind == InputKind::pad) tracker.pad_input(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event.gbutton.which : event.gaxis.which);
    if (event.type == SDL_EVENT_GAMEPAD_ADDED && !pad_by_id(gamepads, event.gdevice.which)) {
        SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which);
        if (!pad) return false;
        const PadIdentity identity = pad_identity(pad);
        if (!player_pad(identity)) {
            // (A Wii remote of SDL's Wii driver: it takes no slot.)
            SDL_CloseGamepad(pad);
            return false;
        }
        gamepads.push_back(pad);
        tracker.pad_added(event.gdevice.which, classify(identity), identity.name);
        return true;
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        bool removed = false;
        for (auto it = gamepads.begin(); it != gamepads.end(); ++it)
            if (SDL_GetGamepadID(*it) == event.gdevice.which) {
                SDL_CloseGamepad(*it);
                gamepads.erase(it);
                removed = true;
                break;
            }
        tracker.pad_removed(event.gdevice.which);
        return removed;
    }
    return false;
}

PadFamily family_of(const PromptTracker& tracker, SDL_Gamepad* pad) {
    const SDL_JoystickID id = SDL_GetGamepadID(pad);
    for (const auto& known : tracker.pads())
        if (known.id == id) return known.family;
    return PadFamily::generic;
}

bool trigger_clicks(SDL_Gamepad* pad) {
    return SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_MISC3) && SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_MISC4);
}

HostPadInput pad_input(const Settings& settings, PadFamily family, const GamepadState& state, bool clicks) {
    if (family == PadFamily::gamecube && settings.gamecube_layout == GameCubeLayout::original) return gamecube_input(state, clicks);
    return gamepad_input(settings, binding_state(state, family, settings.nintendo_layout, clicks));
}

HostPadInput pad_input(const Settings& settings, PadFamily family, SDL_Gamepad* pad) {
    const bool clicks = family != PadFamily::gamecube || trigger_clicks(pad);
    return pad_input(settings, family, read_gamepad(pad), clicks);
}

MenuKeys menu_keys(const Settings& settings, PadFamily family, const GamepadState& state, bool clicks) {
    MenuKeys keys;
    keys.back = state.buttons[SDL_GAMEPAD_BUTTON_BACK];
    if (settings.menu_buttons == MenuButtons::bindings) {
        const HostPadInput in = pad_input(settings, family, state, clicks);
        const auto on = [&](std::uint32_t bit) { return (in.buttons & bit) != 0; };
        keys.face_down = on(pad_button::a);
        keys.face_right = on(pad_button::b);
        keys.face_left = on(pad_button::x);
        keys.face_up = on(pad_button::y);
        keys.start = on(pad_button::start);
        keys.up = on(pad_button::up);
        keys.down = on(pad_button::down);
        keys.left = on(pad_button::left);
        keys.right = on(pad_button::right);
        keys.l1 = on(pad_button::l);
        keys.r1 = on(pad_button::r);
        keys.left_x = in.stick.x, keys.left_y = in.stick.y;
        keys.right_x = in.c_stick.x, keys.right_y = in.c_stick.y;
        return keys;
    }
    // The binding space with the default layout: a Nintendo pad by its
    // labels (or by position), a GameCube pad's A / B / X / Y and its
    // triggers at the click; Xbox, PlayStation and other pads as SDL
    // reports them.
    const GamepadState s = binding_state(state, family, settings.nintendo_layout, clicks);
    const auto b = [&](SDL_GamepadButton button) { return s.buttons[std::size_t(button)]; };
    keys.face_down = b(SDL_GAMEPAD_BUTTON_SOUTH);
    keys.face_right = b(SDL_GAMEPAD_BUTTON_EAST);
    keys.face_left = b(SDL_GAMEPAD_BUTTON_WEST);
    keys.face_up = b(SDL_GAMEPAD_BUTTON_NORTH);
    keys.start = b(SDL_GAMEPAD_BUTTON_START);
    keys.up = b(SDL_GAMEPAD_BUTTON_DPAD_UP);
    keys.down = b(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    keys.left = b(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    keys.right = b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    keys.l1 = b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    keys.r1 = b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    const auto axis = [&](SDL_GamepadAxis a) { return std::clamp(float(s.axes[std::size_t(a)]) / 32767.0f, -1.0f, 1.0f); };
    keys.l2 = std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f);
    keys.r2 = std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f);
    // (SDL's y points down.)
    keys.left_x = axis(SDL_GAMEPAD_AXIS_LEFTX), keys.left_y = -axis(SDL_GAMEPAD_AXIS_LEFTY);
    keys.right_x = axis(SDL_GAMEPAD_AXIS_RIGHTX), keys.right_y = -axis(SDL_GAMEPAD_AXIS_RIGHTY);
    return keys;
}

} // namespace dw4::game
