#pragma once

// The account and room screens (docs/SEAMLESS.md 10) in ImGui: the small
// Online panel at the bottom right of the title screen and the F1 Online tab
// draw the same screens of one OnlineLobby (online/lobby.hpp). Everything is
// English and ASCII; the screens work with the mouse, the keyboard and a
// controller (ImGui's navigation: the D-pad or stick moves, A presses, B goes
// back; the controller's View / Back button opens and closes the title panel
// and the F1 menu).
//
// Presentation only: nothing here touches the engine. The host (game_main)
// starts and ends the session the lobby prepares.
#include "online/lobby.hpp"

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

// The screens of the lobby (the body of the F1 Online tab and of the active
// title panel). `live`: the running room (nullptr when none).
void draw_online_screens(online::OnlineLobby& lobby, OnlineUiState& state, const RoomLive* live);

// The title screen's panel at the bottom right of the display. Inactive it
// is one compact box (the mouse or the View button opens it); active it
// shows the screens and takes the input. Returns whether it is active after
// this frame (the host blocks the game's input while it is).
struct TitlePanel {
    bool active = false;
    bool focus = false;                  // give it the keyboard / controller focus this frame
    bool hovered = false;                // the mouse is over it (the host's click rule)
};
void draw_title_panel(online::OnlineLobby& lobby, OnlineUiState& state, TitlePanel& panel);

} // namespace dw4::game
