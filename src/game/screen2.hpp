#pragma once

// The second screen's panels (docs/ANDROID.md, phase E): three tabs, Online
// (the account lobby's state, the world's counts, the friends online, the
// running instance and its members), Status (the party: every joined player's
// form, level, HP, MP and EXP, read from the running game) and Menu (the F1
// menu, opened there by touch on demand). One set of ImGui code for both
// platforms and both forms:
//
// - on Android with a second display (the AYN Thor's lower screen) they fill
//   that display, drawn by a second ImGui context into the display's own
//   surface after the game's swap (screen2_android.cpp);
// - on the PC, and on Android without a second display (none, switched off,
//   video out, [screen2] mode=off), they are an overlay window over the game
//   with the Online and Status tabs (the Menu is F1 itself there), off until
//   the player turns it on in F1 > Settings > Second screen ([screen2]
//   overlay), so a PC player sees nothing new unless he opens it.
//
// Presentation only: the panels read the engine's GameSystem after a step
// (as the overhead name tags do), the lobby and the session's facts; nothing
// here reaches the simulation, a save state or a fingerprint.
#include "online_ui.hpp"
#include "settings.hpp"

#include <SDL3/SDL_events.h>

#include <functional>
#include <string>
#include <vector>

namespace dw4::engine {
class Engine;
}

namespace dw4::game {

class OverheadLabels;

// ---- What the panels show (the host fills it every frame)

struct Screen2Player {
    std::string name;            // "Kim" (ASCII), "Player 2" without a name
    std::string form;            // "Agumon"; empty when unknown
    int slot = -1;               // the GameSystem slot (-1: another machine's online member)
    bool own = false;            // this machine's player
    int level = 0, hp = 0, max_hp = 0, mp = 0, max_mp = 0;
    int exp = -1, next_exp = -1; // -1: not known (an online member)
    int bits = -1;
    std::string where;           // an online member's area line
};

struct Screen2Status {
    bool game = false;           // a game runs (past the title's menus)
    bool loading = false;        // a stage is loading
    std::string area;            // "Death Valley: Humid Cave"; empty on the menus
    std::string difficulty;      // "Normal" ...
    std::vector<Screen2Player> party;
};
// The joined local players (GameSystem::players) and the online members the
// name tags know in this stage (OverheadLabels::remote_players). `own_slot`:
// this machine's slot (0 offline).
Screen2Status screen2_status(engine::Engine& engine, const OverheadLabels& labels, int own_slot);

struct Screen2Online {
    online::OnlineLobby* lobby = nullptr; // the account lobby (null: this run has none)
    const RoomLive* live = nullptr;       // the running world session (null: none)
    std::string direct;                   // a direct --host / --join session's line (empty: none)
};

// The F1 menu as the Menu tab sees it.
struct Screen2Menu {
    bool open = false;                    // the F1 menu is open
    bool here = false;                    // ... on this screen (the Menu tab draws it)
    bool pauses = false;                  // it pauses the game ([menu] pause)
    // The menu draws its own header and close button and scrolls itself (the
    // console menu, console_menu.hpp): the Menu tab shows only menu.draw,
    // without "Close the menu", the panel's scrollbar and its drag.
    bool own_chrome = false;
    std::function<void()> draw;           // the F1 menu's tabs (into the current window)
    // Out: what the player asked for this frame.
    enum class Ask { none, open, close, move_here };
    Ask ask = Ask::none;
    std::string tab;                      // out: the F1 tab to open on ("Online")
};

enum class Screen2Place { lower, overlay };

// The panels into the current ImGui window (the lower screen's whole window,
// or the overlay's): the tab bar ([screen2] tab; the Menu tab only with
// `menu`) and the chosen panel. A changed tab sets `settings_changed`.
// Returns whether the Online panel showed (the lobby refreshes its counts
// while it does).
bool draw_screen2_panels(Settings& settings, bool& settings_changed, Screen2Place place, const Screen2Status& status,
                         const Screen2Online& online, Screen2Menu* menu);
// The lower screen's form: the panels fill the whole display (inside
// screen2_frame's `draw`). Returns whether the Online panel showed.
bool draw_screen2_screen(Settings& settings, bool& settings_changed, const Screen2Status& status, const Screen2Online& online,
                         Screen2Menu& menu);
// The overlay form: a window over the game while [screen2] overlay is on
// (its close button turns it off). Returns whether the Online panel showed.
bool draw_screen2_overlay(Settings& settings, bool& settings_changed, const Screen2Status& status, const Screen2Online& online);
// F1 > Settings > "Second screen": [screen2] mode (Android), the overlay,
// and what the device reports.
// `scroll_to`: DW4_MENU_SECTION's section; "Screen2" scrolls the Settings tab
// here once (menu screenshots).
void settings_screen2_section(Settings& settings, bool& settings_changed, std::string& scroll_to);

// ---- The second display (Android: screen2_android.cpp; the PC has none)

struct Screen2Display {
    bool found = false;      // a second display: FLAG_PRESENTATION, not private, not the game's, not off
    int id = -1;
    std::string name;
    int width = 0, height = 0, dpi = 0;
    bool surface = false;    // its surface is there: the panels draw on it
};
Screen2Display screen2_display();
// Whether the lower screen is wanted ([screen2] mode): Android shows or
// dismisses its Presentation. Only a change does anything.
void screen2_request(bool wanted);
// One frame of the lower screen after the game's swap: `input` before the
// second ImGui context's new frame (its io: the menus' navigation), `draw`
// inside it; the context has the lower screen's size, touch, clock and the
// menus' scale for its density ([window] ui_scale, else the display's).
// Drawn every other call unless `now` (a touch in the queue draws at once);
// false when there is no lower screen (nothing drawn).
bool screen2_frame(const std::function<void()>& input, const std::function<void()>& draw, int ui_scale_setting, bool now);
// SDL_EVENT_RENDER_DEVICE_RESET (a new OpenGL context): the lower screen's
// ImGui context and surface go before the window's ImGui backend starts
// again (the shared font texture is then made again by it); both come back
// at the next frame.
void screen2_device_reset();
// SDL_EVENT_WILL_ENTER_BACKGROUND (the lifecycle watch, on the game's
// thread): the lower screen's EGL surface goes before SDL's pause lets go
// of the context (SDL_androidevents.c); it is made again at the next frame.
void screen2_enter_background();
// The game ends: the second ImGui context and the surface go (before the
// window's ImGui context).
void screen2_shutdown();
// Typing for a text field of the lower screen (its window is not
// focusable, so the system keyboard is the game window's): while one is
// active the lower screen starts SDL's text input, and the window's text
// and editing keys go to it here (true: taken; host_event, before ImGui).
bool screen2_take_event(const SDL_Event& event);
// The lower screen stopped drawing (its surface or display went, [screen2]
// mode off) while one of its fields had the keyboard: the keyboard goes and
// the field is left (the host calls it every frame without a lower screen).
void screen2_release_input();
// A touch for the lower screen's queue as Android's LowerScreen gives it
// (action 0 down, 1 move, 2 up, 3 cancel; surface pixels): the menu
// script's "tap ... lower" (nothing on the PC).
void screen2_touch(int action, float x, float y);
// Seconds since the last touch on the lower screen (1e9 without one).
double screen2_seconds_since_touch();
// The lower screen's picture as a BMP file at its next frame (test runs'
// screenshots: NAME_screen2.bmp beside the window's).
void screen2_request_shot(const std::string& path);

} // namespace dw4::game
