#pragma once

// The host's controller handling (host_view.cpp) without ImGui or a window,
// so the tests run the same code as the game (docs/ROUND_PADS_NOTES.md):
// which pads take the player slots, the prompt tracker on SDL's events, a
// pad's input through its binding space, and the keys the port's menus
// take from it.
#include "controls.hpp"
#include "pad_family.hpp"
#include "settings.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <vector>

namespace dw4::game {

// One SDL event for the player slots: a GAMEPAD_ADDED player pad
// (player_pad) is opened and appended (players 2-4 are the further pads in
// plug order), a GAMEPAD_REMOVED one closed and dropped (the later slots
// move up), and real input goes to the tracker (the prompts follow it).
// True when a pad was added or removed.
bool pad_slots_event(std::vector<SDL_Gamepad*>& gamepads, PromptTracker& tracker, const SDL_Event& event);
SDL_Gamepad* pad_by_id(const std::vector<SDL_Gamepad*>& gamepads, SDL_JoystickID id);
// A connected pad's family (generic when the tracker does not know it).
PadFamily family_of(const PromptTracker& tracker, SDL_Gamepad* pad);
// Whether SDL reports a GameCube pad's trigger clicks (its own driver does;
// DirectInput database mappings do not).
bool trigger_clicks(SDL_Gamepad* pad);
// A pad's state (read_gamepad) with what its family adds: the AYN Thor's
// L2 / R2 keys folded into its trigger axes (with_digital_triggers).
GamepadState read_pad(SDL_Gamepad* pad, PadFamily family);

// A pad's input through the [gamepad] bindings in its family's binding
// space; a GameCube pad on its original layout plays 1:1.
HostPadInput pad_input(const Settings& settings, PadFamily family, const GamepadState& state, bool clicks);
HostPadInput pad_input(const Settings& settings, PadFamily family, SDL_Gamepad* pad);

// What the port's menus (ImGui's gamepad navigation) take from player 1's
// pad. menu_buttons=layout: the binding space's own buttons, whatever the
// game's bindings: its bottom face button activates (A, Cross, a Nintendo
// pad's A by label or its bottom button by position, the GameCube A), the
// right one goes back, the shoulders and triggers, the D-pad and both
// sticks as they are. menu_buttons=bindings: the game's A activates, B goes
// back, X / Y are the face keys left and up, L / R the shoulders, the main
// stick and the C-stick the sticks. `back`: the pad's View / Back button in
// both.
struct MenuKeys {
    bool face_down = false, face_right = false, face_left = false, face_up = false;
    bool start = false, back = false;
    bool up = false, down = false, left = false, right = false;
    bool l1 = false, r1 = false;
    float l2 = 0, r2 = 0;                                   // 0..1
    float left_x = 0, left_y = 0, right_x = 0, right_y = 0; // -1..1, y up
};
MenuKeys menu_keys(const Settings& settings, PadFamily family, const GamepadState& state, bool clicks);

} // namespace dw4::game
