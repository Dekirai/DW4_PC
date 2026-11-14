// Keyboard and controller bindings -> GameCube pad input (src/game/controls,
// src/game/settings), without a window or device: SDL's key and controller
// names only. The main stick, the D-pad and the C-stick must stay separate
// inputs like on a GameCube pad - the original's quick panels move their
// cursor with the C-stick and the D-pad (8034A24C), the player walks with the
// main stick (8034C4D4) - and settings.ini files from before keep working.
#include "../src/game/controls.hpp"
#include "../src/game/settings.hpp"

#include "dw4/pad.hpp"

#include <SDL3/SDL_scancode.h>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4;
using namespace dw4::game;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

// The bePadData state after one update with this host input.
PadState state_of(const HostPadInput& input) {
    OriginalPads pads;
    OriginalPads::Samples samples{};
    samples[0] = host_pad_sample(input);
    pads.update(samples);
    return pads.states()[0];
}

// The quick panel's cursor input, 8034A24C..8034A294: C-stick trigger/repeat
// or D-pad trigger/repeat.
bool panel_up(const PadState& pad) {
    return ((pad.right_directions.trigger | pad.right_directions.repeat) & pad_direction::positive_y) ||
           ((pad.buttons.trigger | pad.buttons.repeat) & pad_button::up);
}
bool panel_down(const PadState& pad) {
    return ((pad.right_directions.trigger | pad.right_directions.repeat) & pad_direction::negative_y) ||
           ((pad.buttons.trigger | pad.buttons.repeat) & pad_button::down);
}

HostPadInput keys_down(const Settings& settings, std::initializer_list<SDL_Scancode> codes) {
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    for (const auto code : codes) keys[code] = true;
    return keyboard_input(settings, keys.data());
}

void keyboard_defaults() {
    const Settings settings;
    // WASD walk: main stick only, no D-pad bits, the panel cursor stays.
    for (const auto code : {SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D}) {
        const auto input = keys_down(settings, {code});
        check(input.buttons == 0 && input.c_stick_directions == 0 && input.stick_directions != 0, "A movement key left the main stick");
        const auto pad = state_of(input);
        check(pad.left_directions.held != 0 && !panel_up(pad) && !panel_down(pad), "Walking moved the panel cursor");
    }
    check(keys_down(settings, {SDL_SCANCODE_W}).stick_directions == pad_direction::positive_y, "W is not stick up");
    check(keys_down(settings, {SDL_SCANCODE_D}).stick_directions == pad_direction::positive_x, "D is not stick right");
    // The arrow keys are the D-pad: panel cursor and menus, no walking.
    auto pad = state_of(keys_down(settings, {SDL_SCANCODE_DOWN}));
    check(pad.buttons.held == pad_button::down && pad.left_directions.held == 0 && panel_down(pad), "Arrow down is not the D-pad");
    check(keys_down(settings, {SDL_SCANCODE_UP}).buttons == pad_button::up, "Arrow up is not D-pad up");
    check(keys_down(settings, {SDL_SCANCODE_LEFT}).buttons == pad_button::left, "Arrow left is not D-pad left");
    check(keys_down(settings, {SDL_SCANCODE_RIGHT}).buttons == pad_button::right, "Arrow right is not D-pad right");
    // The keypad's 8 / 2 / 4 / 6 are the C-stick.
    pad = state_of(keys_down(settings, {SDL_SCANCODE_KP_8}));
    check(pad.right_directions.held == pad_direction::positive_y && pad.buttons.held == 0 && pad.left_directions.held == 0 && panel_up(pad),
          "Keypad 8 is not C-stick up");
    check(keys_down(settings, {SDL_SCANCODE_KP_2}).c_stick_directions == pad_direction::negative_y, "Keypad 2 is not C-stick down");
    check(keys_down(settings, {SDL_SCANCODE_KP_4}).c_stick_directions == pad_direction::negative_x, "Keypad 4 is not C-stick left");
    check(keys_down(settings, {SDL_SCANCODE_KP_6}).c_stick_directions == pad_direction::positive_x, "Keypad 6 is not C-stick right");
    // Walking up while choosing down: both at once, each on its channel.
    pad = state_of(keys_down(settings, {SDL_SCANCODE_W, SDL_SCANCODE_DOWN}));
    check(pad.left_directions.held == pad_direction::positive_y && panel_down(pad) && !panel_up(pad), "Walk + D-pad mixed channels");
    // Buttons as before.
    check(keys_down(settings, {SDL_SCANCODE_RETURN}).buttons == pad_button::a && keys_down(settings, {SDL_SCANCODE_SPACE}).buttons == pad_button::a,
          "Enter / Space are not A");
    check(keys_down(settings, {SDL_SCANCODE_E}).buttons == pad_button::r && keys_down(settings, {SDL_SCANCODE_Q}).buttons == pad_button::l,
          "E / Q are not R / L");
    check(keys_down(settings, {SDL_SCANCODE_Z}).buttons == pad_button::z && keys_down(settings, {SDL_SCANCODE_J}).buttons == pad_button::x,
          "Z / J are not Z / X");
    // No default key stands for two controls.
    std::array<int, SDL_SCANCODE_COUNT> uses{};
    for (const auto& keys : settings.keys)
        for (const auto code : keys)
            if (code != SDL_SCANCODE_UNKNOWN) check(++uses[code] == 1, "A default key is bound twice");
}

void controller_defaults() {
    const Settings settings;
    GamepadState pad;
    // The D-pad is the D-pad only.
    pad.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP] = true;
    auto input = gamepad_input(settings, pad);
    check(input.buttons == pad_button::up && input.stick_directions == 0 && input.stick.x == 0 && input.stick.y == 0,
          "The controller's D-pad moved the main stick");
    check(panel_up(state_of(input)) && state_of(input).left_directions.held == 0, "The controller's D-pad missed the panel or walked");
    // The left stick walks and leaves the panel alone.
    pad = {};
    pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = -32767; // SDL: negative Y is up
    input = gamepad_input(settings, pad);
    check(input.buttons == 0 && input.stick.y == 1.0f && input.c_stick.y == 0, "The left stick is not the main stick");
    auto state = state_of(input);
    check(state.left_directions.held == pad_direction::positive_y && !panel_up(state) && !panel_down(state), "The left stick moved the panel cursor");
    // The right stick is the C-stick (59/74 at full deflection).
    pad = {};
    pad.axes[SDL_GAMEPAD_AXIS_RIGHTY] = 32767;
    input = gamepad_input(settings, pad);
    state = state_of(input);
    check(input.c_stick.y == -1.0f && state.right.y == -59.0f / 74.0f && panel_down(state) && state.left_directions.held == 0,
          "The right stick is not the C-stick");
    // 20 % deadzone per axis.
    pad = {};
    pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 6000;
    pad.axes[SDL_GAMEPAD_AXIS_RIGHTX] = -6000;
    input = gamepad_input(settings, pad);
    check(input.stick.x == 0 && input.c_stick.x == 0, "A resting stick counted as moved");
    // Triggers past half way, shoulders, face buttons.
    pad = {};
    pad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 20000;
    pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 10000;
    check(gamepad_input(settings, pad).buttons == pad_button::l, "Trigger thresholds changed");
    pad = {};
    pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = true;
    pad.buttons[SDL_GAMEPAD_BUTTON_SOUTH] = true;
    pad.buttons[SDL_GAMEPAD_BUTTON_EAST] = true;
    check(gamepad_input(settings, pad).buttons == (pad_button::z | pad_button::a | pad_button::b), "Face buttons / Z changed");
    // A button bound to a stick direction walks (rebinding in the F1 menu).
    Settings walk_dpad;
    walk_dpad.pad[std::size_t(PadKey::stick_up)][0] = {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_DPAD_UP};
    walk_dpad.pad[std::size_t(PadKey::dpad_up)] = {};
    pad = {};
    pad.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP] = true;
    input = gamepad_input(walk_dpad, pad);
    check(input.stick_directions == pad_direction::positive_y && input.buttons == 0, "A D-pad bound to the stick did not walk");
    // Keyboard and first controller together (player 1).
    auto keyboard = keys_down(settings, {SDL_SCANCODE_W});
    pad = {};
    pad.axes[SDL_GAMEPAD_AXIS_RIGHTY] = -32767;
    merge(keyboard, gamepad_input(settings, pad));
    state = state_of(keyboard);
    check(state.left_directions.held == pad_direction::positive_y && panel_up(state) && state.buttons.held == 0, "Keyboard + controller mixed channels");
}

Settings parsed(const char* text) { return Settings::parse(IniFile::parse(text)); }

SDL_Scancode key(const Settings& s, PadKey k, std::size_t slot = 0) { return s.keys[std::size_t(k)][slot]; }

void migration() {
    // The old default layout (one direction input per arrow: "W, Up"):
    // WASD walks, the arrows become the D-pad (the new default).
    const char* old_default = "[keyboard]\na=Return, Space, K\nb=Backspace, L\nx=J\ny=I\nstart=Escape, Tab\n"
                              "up=W, Up\ndown=S, Down\nleft=A, Left\nright=D, Right\nl=Q\nr=E\nz=Z\n\n"
                              "[gamepad]\na=a\nb=b\nx=x\ny=y\nstart=start\nup=dpup\ndown=dpdown\nleft=dpleft\nright=dpright\n"
                              "l=lefttrigger, leftshoulder\nr=righttrigger\nz=rightshoulder\n";
    auto s = parsed(old_default);
    const Settings fresh;
    check(s.keys == fresh.keys, "The old default keyboard layout did not become the new default");
    check(s.pad == fresh.pad, "The old default controller layout did not become the new default");
    // Saving writes the new names and drops the old ones.
    s.store();
    const auto text = s.ini().text();
    check(text.find("\nup=") == std::string::npos && text.find("\nleft=") == std::string::npos, "Old direction names were kept");
    check(text.find("stick_up=W\n") != std::string::npos && text.find("dpad_up=Up\n") != std::string::npos &&
              text.find("cstick_up=Keypad 8\n") != std::string::npos,
          "New keyboard names missing");
    check(text.find("dpad_up=dpup\n") != std::string::npos && text.find("stick_up=\n") != std::string::npos, "New gamepad names missing");
    check(text.find("a=Return, Space, K\n") != std::string::npos, "Buttons changed on save");
    const auto again = Settings::parse(IniFile::parse(text));
    check(again.keys == s.keys && again.pad == s.pad, "A migrated file does not read back");

    // Walking on the arrows only (the repository's local/settings.ini): the
    // arrows keep walking, the D-pad keeps no key (the keypad C-stick still
    // moves the panel cursor), the other bindings stay.
    s = parsed("[keyboard]\na=A, K\nb=S\nx=W\ny=Q\nstart=Return\nup=Up\ndown=Down\nleft=Left\nright=Right\nl=1\nr=2\nz=3\n");
    check(key(s, PadKey::stick_up) == SDL_SCANCODE_UP && key(s, PadKey::stick_left) == SDL_SCANCODE_LEFT, "Arrow walking was lost");
    check(key(s, PadKey::dpad_up) == SDL_SCANCODE_UNKNOWN && key(s, PadKey::dpad_right) == SDL_SCANCODE_UNKNOWN,
          "The D-pad took the walking arrows");
    check(key(s, PadKey::cstick_up) == SDL_SCANCODE_KP_8, "The C-stick default was lost");
    check(key(s, PadKey::a) == SDL_SCANCODE_A && key(s, PadKey::x) == SDL_SCANCODE_W && key(s, PadKey::l) == SDL_SCANCODE_1,
          "Buttons changed in migration");
    auto pad = state_of(keys_down(s, {SDL_SCANCODE_UP}));
    check(pad.left_directions.held == pad_direction::positive_y && !panel_up(pad), "Arrow walking moved the panel cursor");

    // Custom walking keys plus arrows: the custom keys walk, the arrows are the D-pad.
    s = parsed("[keyboard]\nup=I, Up\ndown=K, Down\nleft=J, Left\nright=L, Right\na=Space\nb=B\nx=N\ny=M\n");
    check(key(s, PadKey::stick_up) == SDL_SCANCODE_I && key(s, PadKey::stick_up, 1) == SDL_SCANCODE_UNKNOWN, "Custom walking keys lost");
    check(key(s, PadKey::dpad_up) == SDL_SCANCODE_UP && key(s, PadKey::dpad_left) == SDL_SCANCODE_LEFT, "The arrows did not become the D-pad");
    check(key(s, PadKey::stick_left) == SDL_SCANCODE_J, "Custom left lost");

    // New names win over old ones; a file without bindings keeps the defaults.
    s = parsed("[keyboard]\nstick_up=T\nup=W, Up\n");
    check(key(s, PadKey::stick_up) == SDL_SCANCODE_T && key(s, PadKey::dpad_up) == SDL_SCANCODE_UP, "New names did not win");
    s = parsed("");
    check(s.keys == fresh.keys && s.pad == fresh.pad, "An empty file changed the defaults");
    // A default key the file uses elsewhere is dropped from the default.
    s = parsed("[keyboard]\nz=Keypad 8\n");
    check(key(s, PadKey::cstick_up) == SDL_SCANCODE_UNKNOWN && key(s, PadKey::z) == SDL_SCANCODE_KP_8, "A file's key stayed on a default too");
    // Controller: an old custom direction binding becomes the D-pad.
    s = parsed("[gamepad]\nup=y\n");
    check(s.pad[std::size_t(PadKey::dpad_up)][0] == PadInput{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_NORTH}, "Old gamepad up lost");
    check(s.pad[std::size_t(PadKey::y)][0] == PadInput{}, "A default controller input stayed on two controls");
}
// The menus' size (settings.ini [window] ui_scale): auto follows the display
// only where asked (Android), a percent counts everywhere, written only once
// it is not auto; the AYN Thor's controller style likewise.
void menu_size() {
    check(ui_scale_factor(0, 2.30625f, false) == 1.0f, "Auto on the PC is 1x whatever the display");
    check(std::fabs(ui_scale_factor(0, 2.30625f, true) - 2.30625f) < 1e-6f, "Auto on Android is the display's scale (369 dpi: 2.31)");
    check(ui_scale_factor(0, 0.75f, true) == 1.0f && ui_scale_factor(0, 9.0f, true) == 4.0f, "Auto between 1x and 4x");
    check(ui_scale_factor(150, 2.3f, true) == 1.5f && ui_scale_factor(200, 1.0f, false) == 2.0f, "A set percent counts everywhere");
    Settings s = parsed("");
    check(s.ui_scale == 0 && s.thor_style == ThorStyle::automatic, "defaults: auto");
    s.store();
    check(!s.ini().get("window", "ui_scale") && !s.ini().get("gamepad", "thor_style"), "auto is not written into a file without it");
    s = parsed("[window]\nui_scale=175\n[gamepad]\nthor_style=xbox\n");
    check(s.ui_scale == 175 && s.thor_style == ThorStyle::xbox, "read back");
    s.ui_scale = 0;
    s.store();
    check(s.ini().get_or("window", "ui_scale", "") == "auto", "auto again once it was set");
    s = parsed("[window]\nui_scale=auto\n");
    check(s.ui_scale == 0, "auto read");
    s = parsed("[window]\nui_scale=40\n");
    check(s.ui_scale == 100, "a too small percent is 100");
}
} // namespace

int main() {
    try {
        keyboard_defaults();
        controller_defaults();
        migration();
        menu_size();
    } catch (const std::exception& error) {
        std::cerr << "controls tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "controls tests passed (" << checks << " checks)\n";
    return 0;
}
