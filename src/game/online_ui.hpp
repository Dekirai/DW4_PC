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

// The running room as this machine's session sees it (the relay's ROSTER
// and the own instance), filled by the host every frame.
struct RoomLive {
    bool active = false;                 // a room session runs
    std::string code, name;              // the room
    std::uint32_t member = 0xFFFFFFFFu;  // the own room slot (0-9)
    int own_slot = 0;                    // the engine slot in the own instance
    std::uint32_t inst = 0;
    std::string mode;                    // solo, sealed, parked, ...
    int delay = 0;
    bool leave_prompt = false;           // B + X + START asked "Leave room?"
    bool in_game = false;                // the game started (past the title's Game Start / Call Digimon)
    std::string character;               // the online character's save state (CharacterSync::status_text)
    struct Member {
        std::uint32_t member = 0;        // room slot 0-9
        std::string name, stage;         // stage empty: hidden (loading, a scene, a menu)
        std::uint32_t inst = 0;
        int level = 0, hp = 0, max_hp = 0;
        bool connected = false, lagging = false, owner = false;
    };
    std::vector<Member> members;         // the last ROSTER
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
    char room_name[48]{}, room_password[80]{};
    int room_players = 4, room_difficulty = 0, room_camera = 0, room_light = 1;
    bool room_fast = false;
    int rand_seed = 0, rand_enemies = 0, rand_zones = 0;
    bool rand_drops = false;
    char join_password[80]{};
    std::string selected;                // the selected room's code
    bool leave_asked = false;            // "Leave room?" in the room view
    online::Screen last_screen = online::Screen::welcome;
    bool focus_pending = false;          // put the controller's focus on the new screen's first item
    bool close_requested = false;        // out: "Play" in the room view (the host closes the screens)
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

// Sets a form field by its script name ("login.name", "create.players", ...);
// false for an unknown field or value.
bool set_ui_field(OnlineUiState& state, const std::string& field, const std::string& value);

// One area line for a stage folder: "Death Valley: Humid Cave", "Menus" for
// the title and save screens, "Loading or in a scene" for a hidden member
// ("Title screen" for the title's hub instance, which has no stage).
std::string area_line(const std::string& stage, bool hub = false);

// The names of the title options as the room settings use them.
const char* camera_mode_name(int mode);
const char* light_mode_name(int mode);

// The screens of the lobby (the body of the F1 Online tab and of the title's
// online screens). `live`: the running room (nullptr when none).
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
