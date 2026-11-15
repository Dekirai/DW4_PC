// The console F1 menu's navigation model (src/game/console_nav;
// docs/CONSOLE_MENU.md 6): focus areas, the page stack and its memory,
// edges, repeat and latch, modals, touch gestures and the capture's clock,
// driven as the game drives it, without ImGui or SDL.
#include "../src/game/console_nav.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4::game::console;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

NavRow row(const char* id, RowKind kind, bool focusable = true, int cells = 0) {
    NavRow r;
    r.id = id;
    r.kind = kind;
    r.focusable = focusable;
    r.cells = cells;
    r.second = kind == RowKind::binding;
    return r;
}

// A page: a section, two toggles, an info, a choice, a link, an action.
std::vector<NavRow> page() {
    return {row("s", RowKind::section, false), row("t1", RowKind::toggle), row("t2", RowKind::toggle), row("i", RowKind::info, false),
            row("c", RowKind::choice),         row("l", RowKind::link),    row("a", RowKind::action)};
}

// One pass with these buttons down at `t`.
struct Driver {
    NavCore nav;
    Buttons buttons;
    double t = 0;
    std::vector<Command> press(std::uint32_t down, const std::vector<NavRow>& rows, double dt = 0.016) {
        t += dt;
        buttons.update(down, t);
        return nav.step(buttons, t, rows, 4);
    }
    // Pressed one pass, released the next.
    std::vector<Command> tap(Button b, const std::vector<NavRow>& rows) {
        auto out = press(bit(b), rows);
        press(0, rows);
        return out;
    }
};

bool has(const std::vector<Command>& out, Cmd cmd) {
    for (const auto& c : out)
        if (c.cmd == cmd) return true;
    return false;
}

void focus_skips_sections_and_info() {
    const auto rows = page();
    check(first_focusable(rows) == 1 && last_focusable(rows) == 6, "first and last focusable");
    check(next_focusable(rows, 2, 1, false) == 4, "down skips the info");
    check(next_focusable(rows, 1, -1, false) == -1 && next_focusable(rows, 1, -1, true) == 6, "wrap only when asked");
    Driver d;
    d.nav.set_categories({"saves", "general"});
    d.nav.area = Area::list;
    d.tap(Button::down, rows);
    check(d.nav.focus_id == "t2", "down from the first row");
    d.tap(Button::down, rows);
    check(d.nav.focus_id == "c", "down past the info");
}

void wrap_only_on_a_fresh_press_and_repeat() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves"});
    d.nav.area = Area::list;
    d.nav.set_focus(rows, 6);
    // Held down from the last row: it stays.
    d.press(bit(Button::down), rows); // fresh: wraps to the first
    check(d.nav.focus_id == "t1", "a fresh press wraps");
    d.nav.set_focus(rows, 5);
    d.press(0, rows);
    d.press(bit(Button::down), rows); // fresh: to the last
    check(d.nav.focus_id == "a", "down to the last");
    for (int i = 0; i < 60; ++i) d.press(bit(Button::down), rows); // a second held
    check(d.nav.focus_id == "a", "held: no wrap");
    // Repeat timing: 350 ms, then every 80 ms.
    Buttons b;
    int fired = 0;
    for (int ms = 0; ms <= 1000; ms += 10) {
        b.update(bit(Button::up), ms / 1000.0);
        fired += b.fired(Button::up) ? 1 : 0;
    }
    // At 0, 350, 430, ..., 990: 1 + 9.
    check(fired == 9 || fired == 10, "repeat after 350 ms, then every 80 ms (" + std::to_string(fired) + ")");
    Buttons l;
    int shoulder = 0;
    for (int ms = 0; ms <= 1000; ms += 10) {
        l.update(bit(Button::l1), ms / 1000.0);
        shoulder += l.fired(Button::l1) ? 1 : 0;
    }
    check(shoulder == 1, "L1 never repeats");
}

void categories_and_sidebar() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves", "general", "display"});
    check(d.nav.area == Area::sidebar && d.nav.category() == 0, "the sidebar on Saves at first");
    auto out = d.tap(Button::up, rows);
    check(d.nav.category() == 2 && has(out, Cmd::category), "sidebar up wraps to the last category");
    d.tap(Button::r1, rows);
    check(d.nav.category() == 0, "R1 wraps forward");
    d.tap(Button::l1, rows);
    check(d.nav.category() == 2, "L1 wraps back");
    d.tap(Button::a, rows);
    check(d.nav.area == Area::list, "A goes to the list");
    d.tap(Button::down, rows); // t2
    d.tap(Button::r1, rows);
    check(d.nav.category() == 0 && d.nav.area == Area::list, "L1 / R1 in the list keep the list");
    d.tap(Button::l1, rows);
    check(d.nav.category() == 2 && d.nav.focus_id == "t2", "the category's root comes back with its row");
}

// A category's home row (Saves' last used state slot): the sidebar, L1 / R1
// come back to it instead of the last focused row; a reopened menu keeps its
// place (no select_category).
void home_rows() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves", "general"});
    d.tap(Button::a, rows); // the list, t1
    d.tap(Button::down, rows);
    d.tap(Button::down, rows); // c
    d.nav.home["saves"] = "t2";
    d.tap(Button::r1, rows);
    d.tap(Button::l1, rows);
    d.nav.resolve(rows);
    check(d.nav.category() == 0 && d.nav.focus_id == "t2", "Saves comes back on its home row");
    d.tap(Button::down, rows); // c
    d.nav.remember();
    check(d.nav.focus_id == "c", "the place itself stays where it is until the category changes");
}

void left_right() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves"});
    d.nav.area = Area::list;
    d.nav.set_focus(rows, 4); // the choice
    auto out = d.tap(Button::left, rows);
    check(has(out, Cmd::change) && out.front().dir == -1 && d.nav.area == Area::list, "Left on a value row changes it");
    d.nav.set_focus(rows, 6); // the action
    d.tap(Button::left, rows);
    check(d.nav.area == Area::sidebar, "Left on another row goes to the sidebar");
    d.nav.area = Area::list;
    d.nav.set_focus(rows, 5);
    out = d.tap(Button::right, rows);
    check(has(out, Cmd::open_link), "Right opens a link");
    // Cells: Left / Right move the cell, clamped.
    const std::vector<NavRow> cells = {row("b", RowKind::binding, true, 3)};
    d.nav.set_focus(cells, 0);
    d.tap(Button::right, cells);
    d.tap(Button::right, cells);
    d.tap(Button::right, cells);
    check(d.nav.cell == 2, "the cell is clamped at the last");
    d.tap(Button::left, cells);
    check(d.nav.cell == 1 && d.nav.cells["b"] == 1, "the cell is remembered");
    out = d.tap(Button::x, cells);
    check(has(out, Cmd::second), "X clears a binding's slot");
    // A stepper held 1.5 s steps five at a time.
    const std::vector<NavRow> stepper = {row("v", RowKind::stepper)};
    d.nav.set_focus(stepper, 0);
    int big = 0;
    for (int i = 0; i < 120; ++i)
        for (const auto& c : d.press(bit(Button::right), stepper))
            if (c.cmd == Cmd::change && c.dir == 5) ++big;
    check(big > 0, "a held stepper speeds up");
    d.press(0, stepper);
}

void back_chain() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves", "general"});
    d.nav.area = Area::list;
    d.nav.set_focus(rows, 5); // the link
    d.nav.open_page("cloud");
    check(d.nav.path() == "saves/cloud", "a sub-page");
    d.nav.resolve(rows);
    d.tap(Button::down, rows);
    auto out = d.tap(Button::b, rows);
    check(has(out, Cmd::page_back) && d.nav.path() == "saves", "B goes back a page");
    d.nav.resolve(rows);
    check(d.nav.focus_id == "l", "the parent page's row it was opened from");
    d.tap(Button::b, rows);
    check(d.nav.area == Area::sidebar, "B at a root goes to the sidebar");
    out = d.tap(Button::b, rows);
    check(has(out, Cmd::close) && std::string(out.front().input) == "the menu's B", "B in the sidebar closes");
    // Start closes from anywhere but a modal.
    d.nav.area = Area::list;
    out = d.tap(Button::start, rows);
    check(has(out, Cmd::close) && std::string(out.front().input) == "Start", "Start closes");
    // A row that takes B itself.
    std::vector<NavRow> own = {row("go", RowKind::action)};
    own[0].own_back = true;
    d.nav.set_focus(own, 0);
    out = d.tap(Button::b, own);
    check(has(out, Cmd::row_back), "row.on_back");
}

void latch() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves"});
    d.nav.area = Area::list;
    d.press(bit(Button::a), rows);
    d.buttons.latch(); // (F1 opened with A held)
    auto out = d.press(bit(Button::a), rows);
    check(!has(out, Cmd::activate), "a latched A does nothing while held");
    d.press(0, rows);
    out = d.press(bit(Button::a), rows);
    check(has(out, Cmd::activate), "A counts after it was let go");
}

void focus_by_id() {
    auto rows = page();
    check(resolve_focus(rows, "c", 0) == 4, "by id");
    // The row went: the next focusable below its place, else above.
    rows.erase(rows.begin() + 4);
    check(resolve_focus(rows, "c", 4) == 4 && rows[4].id == "l", "the next below");
    std::vector<NavRow> tail = {row("x", RowKind::toggle), row("y", RowKind::toggle)};
    check(resolve_focus(tail, "gone", 5) == 1, "else the one above");
    // The focus stays on its row when the layout changes (menu size).
    Driver d;
    d.nav.set_categories({"display"});
    d.nav.area = Area::list;
    d.nav.focus_id = "c";
    d.nav.resolve(page());
    check(d.nav.focus == 4, "found again");
}

void invalid_pages() {
    NavCore nav;
    nav.set_categories({"cheats"});
    nav.open_page("player.2");
    nav.page_invalid();
    check(nav.path() == "cheats", "a gone page falls back to its parent");
    check(nav.request_focus("cheats/randomizer", "cheats.seed", true) && nav.path() == "cheats/randomizer" && nav.focus_id == "cheats.seed" &&
              nav.area == Area::list,
          "request_focus");
    check(!nav.request_focus("nowhere", "", false), "an unknown category");
}

void modals() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves"});
    d.nav.area = Area::list;
    d.nav.open_modal(ModalKind::confirm, "a", 2, 1, d.t, d.buttons);
    check(d.nav.modal.focus == 1, "the start focus");
    auto out = d.press(bit(Button::a), rows, 0.05);
    check(!has(out, Cmd::modal_ok), "A within 250 ms does nothing");
    d.press(0, rows);
    d.t += 0.3;
    d.tap(Button::left, rows);
    check(d.nav.modal.focus == 0, "Left moves to the left button");
    out = d.tap(Button::a, rows);
    check(has(out, Cmd::modal_ok), "A answers after 250 ms");
    out = d.tap(Button::r1, rows);
    check(d.nav.category() == 0 && !has(out, Cmd::category), "L1 / R1 do nothing in a modal");
    d.nav.close_modal(d.buttons);
    d.nav.open_modal(ModalKind::picker, "c", 5, 2, d.t, d.buttons);
    d.tap(Button::down, rows);
    d.tap(Button::down, rows);
    d.tap(Button::down, rows);
    check(d.nav.modal.focus == 4, "the picker is clamped");
    out = d.tap(Button::b, rows);
    check(has(out, Cmd::modal_cancel), "B cancels the picker");
    d.nav.close_modal(d.buttons);
    d.nav.open_modal(ModalKind::capture, "b", 0, 0, d.t, d.buttons);
    out = d.tap(Button::b, rows);
    check(out.empty(), "a capture takes nothing (host_event binds it)");
}

void touch_mode_first_press() {
    const auto rows = page();
    Driver d;
    d.nav.set_categories({"saves"});
    d.nav.area = Area::list;
    d.nav.mode = InputMode::touch;
    auto out = d.tap(Button::down, rows);
    check(has(out, Cmd::cursor) && d.nav.mode == InputMode::pad && d.nav.focus_id == "t1", "the first press only shows the cursor");
    d.nav.mode = InputMode::touch;
    out = d.tap(Button::start, rows);
    check(has(out, Cmd::close), "Start acts at once in touch mode");
}

void gestures() {
    Gesture g;
    g.down(10, 100, 0.0);
    g.move(12, 104, 0.05);
    check(g.up(12, 104, 0.1), "a small move is a tap");
    g.down(10, 100, 1.0);
    g.move(10, 80, 1.02);
    check(g.dragging() && std::abs(g.drag_delta() - 20.0f) < 1e-3f, "a drag scrolls 1:1");
    g.move(10, 40, 1.04);
    check(!g.up(10, 40, 1.06), "a drag is no tap");
    check(g.flinging(), "a fast release flings");
    float total = 0;
    for (int i = 0; i < 300; ++i) total += g.fling(1.0 / 60.0);
    check(!g.flinging() && total > 0, "the fling slows and stops (" + std::to_string(total) + " u)");
    g.down(0, 0, 3.0);
    g.move(0, -200, 3.01);
    g.up(0, -400, 3.02);
    check(g.flinging(), "fling");
    g.down(0, 0, 3.03);
    check(!g.flinging() && !g.up(0, 0, 3.05), "a down during a fling only stops it");
    g.down(0, 0, 4.0);
    check(!g.long_press(4.3, 0.6) && g.long_press(4.65, 0.6) && !g.up(0, 0, 4.7), "a long press, then no tap");
    check(clamp_scroll(-5, 100, 50) == 0 && clamp_scroll(80, 100, 50) == 50, "no overscroll");
    check(reveal_scroll(0, 100, 400, 150, 200, 26, false, false) == 126, "the focus row comes into view with half a row");
    check(reveal_scroll(200, 100, 400, 210, 260, 26, true, false) == 0, "the first focusable row: the very top");
    check(reveal_scroll(0, 100, 400, 350, 400, 26, false, true) == 300, "the last: the very bottom");
}

void capture_clock() {
    CaptureClock c;
    c.start(1.0);
    check(!c.expired(10.9) && c.expired(11.0), "10 s");
    check(std::abs(c.left(6.0) - 0.5f) < 1e-4f, "the bar");
}
} // namespace

int main() {
    try {
        focus_skips_sections_and_info();
        wrap_only_on_a_fresh_press_and_repeat();
        categories_and_sidebar();
        home_rows();
        left_right();
        back_chain();
        latch();
        focus_by_id();
        invalid_pages();
        modals();
        touch_mode_first_press();
        gestures();
        capture_clock();
    } catch (const std::exception& error) {
        std::cerr << "console nav tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "console nav tests passed (" << checks << " checks)\n";
    return 0;
}
