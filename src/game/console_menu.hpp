#pragma once

// Android's F1 menu in console style (docs/CONSOLE_MENU.md): the categories
// as large entries on the left, a list of large rows on the right, every row
// reachable with the D-pad and A / B (L1 / R1 change the category, Left /
// Right a value), big touch targets with taps, drags and flings, the
// system keyboard for text, the controller binding capture, confirms for
// what cannot be undone. The same settings as the PC's F1 menu through the
// same apply layer (menu_ops.hpp, f1_actions.hpp): a different
// presentation, one source of truth. The PC's F1 menu (game_main.cpp
// draw_f1_menu) is unchanged; on the PC the console menu shows only in a
// test run with DW4_MENU_STYLE=console.
//
// One state for the top screen and the lower screen's Menu panel
// (screen2.hpp): both show the same page, row and modal. update() runs once
// per pass with player 1's menu keys (pad_host.hpp MenuKeys) and the
// keyboard keys host_event buffered; draw() draws into the window (top) or
// the lower screen's panel and takes that context's touches.
#include "console_nav.hpp"
#include "f1_actions.hpp"
#include "pad_host.hpp"

#include <SDL3/SDL_keycode.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game {

struct HostView;

enum class ConsolePlace : std::uint8_t { top, lower };
// Where the menus' rules come from (DW4_MENU_PLATFORM in a PC test run).
enum class MenuPlatform : std::uint8_t { pc, android };

struct ConsoleInput {
    std::optional<MenuKeys> pad; // player 1's menu keys (none: no pad)
    std::vector<SDL_Keycode> keys; // keys pressed this pass (host_event's buffer)
    double now = 0;                // seconds (steady clock)
};

namespace console {
struct Row;
struct Page;
struct State;
} // namespace console

class ConsoleMenu {
public:
    explicit ConsoleMenu(F1Actions& actions);
    ~ConsoleMenu();
    ConsoleMenu(const ConsoleMenu&) = delete;
    ConsoleMenu& operator=(const ConsoleMenu&) = delete;

    // F1 opened in console style (the one-shots DW4_MENU_TAB, _SECTION,
    // _PAGE, _DIALOG, a world session's Online page; the buttons held now
    // are latched) / closed (a modal, the capture, the text sheet are lost;
    // nothing is applied).
    void opened(HostView& view, const std::string& tab, const std::string& section, double now);
    void closed(HostView& view);
    // Once per pass while F1 shows in console style.
    void update(HostView& view, const ConsoleInput& input);
    // Into the current ImGui context: the whole window (top) or the lower
    // screen's panel.
    void draw(HostView& view, ConsolePlace place);
    // Something moves (a drag or fling, the top's focus easing, the capture's
    // countdown, a press highlight, a lock reason's blink, the Performance
    // page): the lower screen draws every pass meanwhile.
    bool animating() const;
    // Developer > Classic menu was chosen: the PC's F1 menu until it closes.
    bool classic_requested() const;
    // The menu asks to close (the menu's B, Start, Close, a loaded state);
    // the host closes it through toggle_menu_logged and latches the pad.
    // The input's name, or empty.
    std::string take_close_request();
    // Test runs and the menu script: where the menu is.
    std::string page_path() const;
    std::string focus_row() const;
    std::string dialog_kind() const; // "", "picker", "confirm", "text", "capture"
    // The shown value of a row of the current page ("" when there is none).
    std::string row_value(HostView& view, const std::string& id);
    // A modal for a picture (DW4_MENU_DIALOG=picker:<row> ...).
    bool open_dialog(HostView& view, const std::string& spec, double now);
    // Phase D: a place to show (focus areas: the list, pad mode when `show_cursor`).
    bool request_focus(const std::string& page_path, const std::string& row_id, bool show_cursor);
    void set_input_mode(bool touch);
    // Typing for the menu script: the text sheet's field (the context that
    // shows it).
    bool text_sheet_open() const;

private:
    std::unique_ptr<console::State> s_;
};

// The menus' platform rules (Android, or a PC test run's DW4_MENU_PLATFORM).
MenuPlatform menu_platform(const HostView& view);
// Whether the controller's View / Back button toggles F1: not on Android
// with the AYN Thor's controller (its Back key does; SELECT stays a game
// button), everywhere else as before.
bool back_toggles_menu(MenuPlatform platform, PadFamily family);

// The console menu's log lines (also kept for the menu script's "expect
// log"): printed to stdout.
void menu_log(const std::string& line);
// The lines since `from` (an index of menu_log_count()).
std::vector<std::string> menu_log_since(std::size_t from);
std::size_t menu_log_count();

} // namespace dw4::game
