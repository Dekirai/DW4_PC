#pragma once

// The account and room screens (docs/SEAMLESS.md 10) in ImGui: the title
// menu's third entry "Online" opens them over the title (large, in the
// game's colours), and the F1 Online tab draws the same screens of one
// OnlineLobby (online/lobby.hpp). Everything is English and ASCII; the
// screens work with a controller first (the D-pad or stick moves, A presses,
// B goes back a screen and from the first one to the title menu, text fields
// open an on-screen keyboard), the keyboard (arrows, Enter, Esc, typing) and
// the mouse.
//
// Presentation only: nothing here touches the engine. The host (game_main)
// starts and ends the session the lobby prepares.
#include "online/lobby.hpp"

struct ImFont;

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game {

// The running world session as this machine's session sees it (the relay's
// INSTANCE_INFO and the own stage group), filled by the host every frame.
struct RoomLive {
    bool active = false;                 // the world session runs
    std::string code, name;              // (unused since round I1a)
    std::uint32_t member = 0xFFFFFFFFu;  // the own member id
    int own_slot = 0;                    // the engine slot in the own instance
    std::uint32_t inst = 0;
    std::string mode;                    // solo, sealed, parked, ...
    int delay = 0;
    bool leave_prompt = false;           // B + X + START asked "Go offline?"
    bool in_game = false;                // the game started (past the title's Game Start / Call Digimon)
    std::string character;               // the online character's save state (CharacterSync::status_text)
    std::string story;                   // "Story: yours" / "Story: <host>'s, ..." (round G-core)
    // Round I1a (19.5.4): the own instance (wid 0: home), its host, its
    // visibility (0 Public, 1 Friends, 2 Private), whether this member hosts it.
    std::uint32_t wid = 0, host = 0;
    std::string host_name;
    int visibility = 0;
    bool is_host = false;
    bool may_remove = false;             // round I1b: the host, or staff playing in the instance (Remove on the player card)
    std::string area;                    // the instance's area key (death_valley, ...)
    struct Member {
        std::uint32_t member = 0;        // the member id
        std::string name, stage;         // stage empty: hidden (loading, a scene, a menu)
        std::uint32_t inst = 0;
        int level = 0, hp = 0, max_hp = 0;
        bool connected = false, lagging = false, owner = false; // owner: the instance's host
    };
    std::vector<Member> members;         // the instance's players (INSTANCE_INFO)
    // Round I1b (19.30.6): the *Go along* box: one row per offer (newest
    // first), the own waiting request's row (A cancels it) and its line.
    struct GoRow {
        std::uint16_t id = 0;
        std::string text;                // "Kim is in Ancient Ruins B1." / the closing line
        std::string grey;                // why A does nothing now ("(defeated)", ...); "" usable
        bool closing = false;            // its 3 s closing line
        bool own = false;                // the own request is on this row
        bool cancellable = false;        // ... and still waits (A: Cancel); stamped or going: nothing to cancel
    };
    std::vector<GoRow> go_rows;
    std::string go_line;                 // the waiting row's text ("Going along when Kim's floor is ready...")
};

// The form fields and the selection (kept across frames; the test-run script
// fills them too, so a screenshot shows a filled form).
struct OnlineUiState {
    char login_name[32]{}, login_password[160]{};
    bool login_remember = true;
    char reg_name[32]{}, reg_password[160]{}, reg_again[160]{}, reg_email[128]{};
    bool reg_remember = true;
    char rec_name[32]{}, rec_code[96]{}, rec_password[160]{}, rec_again[160]{};
    char pw_current[160]{}, pw_next[160]{}, pw_again[160]{};
    char reset_password[160]{};
    // Online > Settings (round I1a): the online preferences' form, filled
    // from the server's when the screen opens.
    int set_visibility = 0, set_camera = 0, set_light = 1;
    bool set_fast = false;
    int rand_seed = 0, rand_enemies = 0, rand_zones = 0;
    bool rand_drops = false;
    bool settings_filled = false;
    bool leave_asked = false;            // "Go offline?" on the playing screen
    int leave_prompt_frame = -10;        // the last frame the playing screen showed "Go offline?" (its buttons take the focus when it opens)
    int leave_prompt_frames = 0;         // the frames in a row it has shown it
    // The Instance page (19.5.4): "Leave instance?" asked; out: the host's
    // visibility choice and the confirmed leave, which the host sends.
    bool leave_instance_asked = false;
    bool focus_visibility = false;       // in: the menu opened from the status window's line ("View: change")
    // Round I1b (19.30.6): in: View opened the menu while a go-along notice
    // had been up for half a second (the box's first row takes the focus;
    // earlier the cursor stays below the box, so a reflexive A goes nowhere);
    // out: A on a row (the offer id), A on the own waiting row (Cancel), B in
    // the box (that offer's notices off; the menu closes).
    bool focus_go = false;
    std::uint16_t focus_go_id = 0;       // in: the row focus_go focuses (0: the first; the own waiting row's offer while it waits)
    bool focus_below_go = false;         // in: the menu opened within a notice's first half second: the cursor below the box
    int go_requested = 0;
    bool go_cancel_requested = false;
    int go_closed = 0;
    int visibility_requested = -1;
    bool leave_instance_requested = false;
    // The player card (round I1b, 19.5.2 / 19.5.4): A on another member's row
    // opens it (*Report*, and *Remove* for the host and staff playing); in: the
    // member it shows (0: closed), the open question; out: the confirmed
    // removal and the report with its reason (1 griefing, 2 cheating, 3 name,
    // 4 other), which the host sends.
    std::uint32_t card_member = 0;
    bool card_remove_asked = false, card_report_open = false;
    bool card_focus = false;             // the controller's focus goes to the card's first button
    bool card_cursor = false;            // (UI scripts' card: as A on the row with the controller, its cursor shown)
    std::uint32_t remove_requested = 0;
    std::uint32_t report_requested = 0;
    int report_reason_requested = 0;
    online::Screen last_screen = online::Screen::welcome;
    bool focus_pending = false;          // put the controller's focus on the new screen's first item
    bool close_requested = false;        // out: "Play" on the playing screen (the host closes the screens)
    bool stay_requested = false;         // out: "Stay" answered B + X + START's "Go offline?" (the host closes that prompt)
    // The on-screen keyboard: a text field the controller activated.
    struct Keyboard {
        bool open = false;
        char* target = nullptr;          // the field's buffer
        std::size_t size = 0;
        std::string label;
        bool secret = false;             // a password: shown as dots
        bool shift = false, symbols = false;
        bool focus = false;              // the controller's focus goes to the first key
    } keyboard;
};

// Sets a form field by its script name ("login.name", "settings.camera", ...);
// false for an unknown field or value.
bool set_ui_field(OnlineUiState& state, const std::string& field, const std::string& value);

// One area line for a stage folder: "Death Valley: Humid Cave", "Menus" for
// the title and save screens, "Loading or in a scene" for a hidden member
// ("Title screen" for the title's hub instance, which has no stage).
std::string area_line(const std::string& stage, bool hub = false);

// Whether the lobby's refusal (online/cpu_gate.hpp) comes before the
// server's state wherever the online state is told (F1 > Online, the second
// screen's Online panel): on Android, where the device itself cannot play
// online yet; on the PC the CPU gate's refusal shows after it, once logged in.
inline constexpr bool online_refusal_first() {
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
}

// The names of the title options as the room settings use them.
const char* camera_mode_name(int mode);
const char* light_mode_name(int mode);

// The screens of the lobby (the body of the F1 Online tab and of the title's
// online screens). `live`: the running world session (nullptr when none).
void draw_online_screens(online::OnlineLobby& lobby, OnlineUiState& state, const RoomLive* live);

// The online screens over the title (the title menu's third entry opens
// them): centred on the 4:3 picture, in the game's colours, with `heading`
// and `body` (null: ImGui's own) at sizes that follow the window's height.
// B, Esc or Back go back a screen; from the first screen (or a closed gate)
// they ask the host to close the screens: `close` is set, and the title menu
// takes the input again.
struct TitleOnline {
    bool active = false;                 // shown over the title, takes the input
    bool focus = false;                  // give it the keyboard / controller focus this frame
    bool close = false;                  // out: back from the first screen
};
void draw_title_online(online::OnlineLobby& lobby, OnlineUiState& state, TitleOnline& screens, const RoomLive* live, ImFont* heading,
                       ImFont* body);

} // namespace dw4::game
