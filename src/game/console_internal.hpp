#pragma once

// The console F1 menu's insides (console_menu.cpp draws and runs it,
// console_pages.cpp builds its pages): a page's rows with their values and
// callbacks, rebuilt every pass from Settings and F1Actions (immediate mode:
// nothing of the settings is kept here), and the one State both screens show.
#include "console_glyphs.hpp"
#include "console_menu.hpp"
#include "console_nav.hpp"
#include "f1_actions.hpp"
#include "menu_ops.hpp"

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dw4::game {
struct HostView;
}

namespace dw4::game::console {

struct Option {
    std::string label;
    double value = 0;
};

// A question before something that cannot be undone (or replaces a lot).
struct Confirm {
    std::string row;   // the row that asked (logs)
    std::string title; // "Overwrite state 2?"
    std::string text;
    std::vector<InfoLine> notes;
    std::vector<std::string> buttons;              // left to right
    std::vector<std::function<void()>> actions;    // per button; empty: cancel
    int focus = 0;                                 // the start focus (the safe button unless the spec says otherwise)
    int danger = -1;                               // the destructive button (red)
};

struct Cell {
    std::string label;
    PromptFace glyph; // binding cells: the input's picture
    bool has_glyph = false;
    std::string locked; // why it does nothing now
    std::function<void()> run;
    std::shared_ptr<Confirm> confirm;
};

struct Row {
    std::string id;
    RowKind kind = RowKind::info;
    std::string label, desc, value, badge, locked;
    InfoTone tone = InfoTone::dim; // info rows
    std::vector<InfoLine> extra;   // coloured lines under the label, always shown
    bool destructive = false;
    std::string more;              // a folding info's text ("More")
    std::vector<PromptFace> glyphs; // info rows: button pictures under the text (the prompts' preview)
    PadFamily glyph_family = PadFamily::generic;
    // toggle
    bool on = false;
    std::function<void(bool)> toggle;
    // choice / stepper: the options (a foreign value among them), the current one
    std::vector<Option> options;
    int current = -1;
    std::function<void(int)> pick;
    std::function<void(int)> step;     // Left / Right instead of the index arithmetic (the preset's confirm)
    std::function<void(int)> choose;   // the picker's answer instead of pick (the preset's confirm)
    std::vector<int> picker_hidden;    // options the picker leaves out (the preset's "Custom")
    // action / entry
    std::function<void()> run;
    std::shared_ptr<Confirm> confirm;
    // link
    std::string target;
    // text / password / number
    std::string text, hint;
    std::size_t max_length = 64;
    double min = 0, max = 0;
    std::function<void(const std::string&)> commit;
    // cells / binding
    std::vector<Cell> cells;
    PadKey key = PadKey::a;
    std::function<void(int)> clear_cell; // binding: X / a long press
    // entry
    std::string right;
    float progress = -1;
    InfoTone right_tone = InfoTone::dim;
    bool highlight = false;
    // B taken by the row (Phase D)
    std::function<void()> on_back;

    NavRow nav() const;
};

struct Page {
    std::string title;
    std::vector<Row> rows;
    bool valid = true; // false: the page is gone (back to its parent)
    bool live = false; // redrawn every frame (Performance)
};

struct Layout {
    bool valid = false;
    float view = 0, content = 0;            // the list's visible and whole height (u)
    std::vector<std::pair<float, float>> rows; // each row's top and height (u)
    int visible_rows = 4;
};

struct Hit {
    enum Kind : std::uint8_t {
        row, arrow_left, arrow_right, cell_button, category, back, close, picker_entry, picker_outside, confirm_button, capture_cancel, capture_clear,
        text_done, text_cancel, card
    } kind = row;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // px of the drawing context
    int index = -1;                       // row / category / entry / button
    int cell = -1;
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

struct TouchPlace {
    Gesture list, sidebar, picker;
    Gesture* active = nullptr;
    Hit pressed;
    bool has_press = false;
    double repeat_next = 0;
    std::vector<Hit> hits;
    float sidebar_scroll = 0, picker_scroll = 0;
};

struct State {
    explicit State(F1Actions& a) : actions(a) {}
    F1Actions& actions;
    NavCore nav;
    Buttons buttons;
    Page page;
    std::vector<NavRow> nav_rows;
    double now = 0, last_draw = 0;
    MenuPlatform platform = MenuPlatform::pc;
    bool developer = false, trace = false, layout_log = false;
    bool latch_next = false;
    std::string close_request;
    bool classic = false;
    std::set<std::string> expanded; // folded infos open
    // ---- modals' content
    std::string picker_row, picker_title;
    std::vector<Option> picker_options;
    std::vector<int> picker_index; // the row's option index of each entry
    int picker_current = -1;
    std::shared_ptr<Confirm> confirm;
    // the text sheet
    std::string text_row, text_title, text_hint, text_error;
    RowKind text_kind = RowKind::text;
    std::array<char, 256> text_buffer{};
    std::size_t text_max = 64;
    double text_min = 0, text_max_value = 0;
    std::function<void(const std::string&)> text_commit;
    int text_frames = 0;
    bool text_logged_active = false;
    bool text_release = false; // the field lets go (ClearActiveID in the context that shows it)
    // the capture
    CaptureClock capture;
    PadKey capture_key = PadKey::a;
    std::size_t capture_slot = 0;
    bool capture_running = false;
    std::uint32_t capture_cancels = 0;
    // the lock reason's blink
    std::string blink_row;
    double blink_until = 0;
    // scrolling
    Layout layout[2];
    int last_place = 0;
    bool reveal = false;
    float ease_from = 0, ease_to = 0;
    double ease_start = -1;
    float stick_scroll = 0;
    TouchPlace touch[2];
    std::string last_layout_log[2];
    std::string last_hits[2];
    float hint_width = 0, hint_room = 0; // the hint bar's entries and the room for them (px)
    std::string last_focus_trace;
    // a row's result line for a while (a saved state, a cloud answer)
    std::string note_row, note_text;
    InfoTone note_tone = InfoTone::ok;
    double note_until = 0;
    // live texts (2 Hz)
    double live_at = -1;
    std::string live_fps, live_memory;
    std::array<float, 240> live_frames{};
};

// ---- console_pages.cpp
// The categories' root page ids in the sidebar's order and their titles.
std::vector<std::string> category_roots(const State& st);
const char* category_title(const std::string& root);
// The page at the nav's path into `page` (rows, title, validity).
void build_page(State& st, HostView& view, Page& page);
// The title of every page on the path ("Saves > Cloud backup").
std::string page_trail(State& st, HostView& view);
// A row's logged value ("On", the option's label, the text).
std::string row_value_text(const Row& row);

// ---- console_menu.cpp, for the pages
void open_confirm(State& st, std::shared_ptr<Confirm> confirm);
void log_change(State& st, const Row& row, const std::string& value);
void log_action(State& st, const Row& row, const std::string& action, bool confirmed);
void request_close(State& st, const char* input);
ops::Target page_ops(HostView& view);

} // namespace dw4::game::console
