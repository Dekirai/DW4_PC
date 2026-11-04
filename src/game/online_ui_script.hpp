#pragma once

// The account and room screens in a test run (--online-ui FILE, development
// builds against a loopback server only): a script presses the game's pad
// (the title menu's third entry opens the screens as a player does), drives
// the lobby through the same calls the screens make, opens the F1 Online tab,
// fills form fields for the screenshots and takes them. tools/online_test
// (run.py ui) runs it with hidden windows; the S2 done-when "driven through
// the client's own code paths in a headless test mode" uses it as well.
//
// One command per line ('#' starts a comment). Every command waits until the
// lobby has no request in flight, then runs; commands that send a request
// wait for its answer too. "$NAME" in a word is the environment variable
// NAME (passwords stay out of the script).
//
//   wait N                     N passes of the game loop (pictures; also while the engine waits)
//   sleep SECONDS              real time
//   wait-title [SECONDS]       until the title presents (its press start or its menu; 120 s)
//   wait-menu [SECONDS]        until the title menu waits for input, the screens closed (120 s)
//   to-menu [SECONDS]          START at the title until its menu waits (120 s)
//   pad NAME...                presses on the game's pad, one after the other (a quarter second
//                              apart, each held three steps; on the title menu each waits until the
//                              menu takes input again): A B X Y START UP DOWN LEFT RIGHT
//   expect-cursor N            the title menu's cursor (0 Game Start, 1 Option, 2 Online)
//   wait-screens [SECONDS]     until the online screens show over the title (30 s)
//   expect-screens open|closed
//   panel open|close           the online screens over the title, opened without the menu
//   open                       the lobby without a panel (version gate, remembered login)
//   menu online|cheats|close   the F1 menu on its Online (or Cheats) tab
//   screen NAME                go to a screen (welcome login register account password rooms create join ...)
//   back                       the Back button
//   view                       the controller's View / Back button (opens / closes the F1 menu)
//   key NAME...                keys for the menus' navigation: up down left right enter space escape tab,
//                              pad-up pad-down pad-left pad-right pad-a pad-b pad-x pad-y pad-start (the
//                              controller's, also without one; one frame each, in order)
//   fill FIELD VALUE...        a form field (online_ui.hpp set_ui_field: login.name, create.players, ...)
//   login NAME PASSWORD [remember]
//   register NAME PASSWORD [EMAIL]
//   ack                        "I saved it" (the recovery code)
//   recover NAME CODE PASSWORD
//   logout | logout-all
//   password CURRENT NEW
//   refresh                    the room list
//   wait-room NAME [SECONDS]   until the room list shows that room (refreshing; 60 s)
//   create NAME [password=P] [max=N] [difficulty=N] [camera=N] [light=N] [fast] [seed=N] [enemies=N] [zones=N] [drops]
//   join NAME|CODE [password=P]
//   confirm | cancel           the "end the offline game" question
//   select NAME|CODE           a room of the list (the Join button's room)
//   wait-session [SECONDS]     until the room's session runs (60 s)
//   wait-members N [SECONDS]   until the room view lists N members or more (60 s; <=N: N or fewer)
//   wait-stage STAGE [SECONDS] until the engine is in that stage (120 s)
//   kick NAME | leave | close CODE
//   wait-left [SECONDS]        until the session ended (60 s)
//   expect-screen NAME | expect-gate NAME | expect-error TEXT | expect-info TEXT | expect-logged-in | expect-logged-out
//   shot NAME                  a screenshot NAME.bmp beside --screenshot's file
//   end                        the run ends
#include "online/lobby.hpp"
#include "online_ui.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dw4::game {

// What the script sees of the host and may change.
struct UiScriptHost {
    online::OnlineLobby& lobby;
    OnlineUiState& state;
    TitleOnline& screens;      // the online screens over the title
    bool& show_menu;
    std::string& menu_tab;     // the F1 tab to select
    bool online_active = false; // a room session runs
    bool title_shown = false;   // the title presents (press start or the menu)
    bool menu_open = false;     // the title menu shows (state 2)
    bool menu_waiting = false;  // the title menu waits for input
    int cursor = 0;             // its cursor
    bool title_entry = false;   // the menu has the third entry
    bool screens_shown = false; // the online screens showed over the title (the last frame)
    std::string stage;          // the engine's stage
    int frame = 0;
    double now = 0;             // seconds
    std::string shot;           // out: a screenshot name for this frame
    bool view_button = false;   // out: the controller's View / Back button
    bool open_screens = false;  // out: open the online screens over the title
    std::uint32_t pad = 0;      // out: game pad buttons to press (dw4::pad_button bits)
    std::vector<int> keys;      // out: ImGui keys to press (the menus' keyboard and controller navigation)
    bool end = false;           // out: the run ends
};

class OnlineUiScript {
public:
    bool load(const std::filesystem::path& file, std::string& error);
    bool active() const { return loaded_ && !done_; }
    bool loaded() const { return loaded_; }
    // One frame. Throws std::runtime_error when an expectation fails.
    void update(UiScriptHost& host);

private:
    struct Line {
        int number = 0;
        std::vector<std::string> words;
    };
    bool run(const Line& line, UiScriptHost& host); // true: done with this line
    std::vector<Line> lines_;
    std::size_t at_ = 0;
    bool loaded_ = false, done_ = false;
    long long passes_ = 0;         // update() calls
    long long wait_until_ = -1;    // pass
    double deadline_ = -1;         // seconds (a wait-* command)
    bool sent_ = false;            // the current line's request went out
    double refreshed_at_ = -1;
    std::uint64_t serial_ = 0;     // the room list a refresh waits to replace
    std::size_t key_at_ = 1;       // the next word of a key or pad command
    double next_press_ = -1;       // seconds: the next pad press
};

} // namespace dw4::game
