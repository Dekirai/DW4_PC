#pragma once

// The console F1 menu's navigation model (docs/CONSOLE_MENU.md), without
// ImGui or SDL so tests/console_nav_tests.cpp drives it as the game does:
// the focus areas (the category sidebar, the list of rows, a modal), the
// page stack with a remembered row and scroll per page, the buttons'
// edges, repeat and latch, the choice and stepper steps, touch taps, drags,
// flings and long presses, and the binding capture's deadline.
//
// console_menu.cpp builds a page's rows every pass (Row, with callbacks),
// hands their NavRow view to NavCore::step with player 1's buttons and
// carries out the commands it returns; the rows' values never live here.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dw4::game::console {

enum class RowKind : std::uint8_t {
    section,  // a heading (not focusable)
    info,     // text (focusable only when it folds out: "More")
    toggle,   // On / Off
    choice,   // one of a list (Left / Right step, A: the picker)
    stepper,  // a number on a grid (Left / Right step, held: faster)
    action,   // A runs it (maybe after a confirm)
    link,     // A or Right opens a sub-page
    text,     // A opens the text sheet (the system keyboard)
    password, // ... a password field's
    number,   // ... a number field's
    cells,    // 2-3 buttons in one row (Left / Right choose)
    binding,  // a controller binding's three slots (A: capture, X: clear)
    entry,    // a title, a text, a value on the right (an achievement, a revision, a member)
    progress, // a bar with text (not focusable)
};
// Rows whose Left / Right change a value or a cell (the others: Left goes
// to the sidebar).
bool value_row(RowKind kind);
bool text_row(RowKind kind);

// What navigation needs of a row.
struct NavRow {
    std::string id;           // stable: "display.frame_limit", "controls.bind.buttons.a"
    RowKind kind = RowKind::info;
    bool focusable = false;
    int cells = 0;            // cells / binding: how many
    bool second = false;      // X does something (binding: clear the slot)
    bool own_back = false;    // B is the row's (row.on_back), not the page's
};

// The logical buttons of a pass (player 1's MenuKeys, the keyboard).
enum class Button : std::uint8_t { up, down, left, right, a, b, x, y, l1, r1, l2, r2, start, home, end, count };
constexpr std::uint32_t bit(Button b) { return 1u << unsigned(b); }
inline constexpr std::uint32_t kDirections = bit(Button::up) | bit(Button::down) | bit(Button::left) | bit(Button::right);

// Edges, repeat and latch of the buttons. Directions (and L2 / R2's pages)
// repeat after 350 ms, then every 80 ms; L1 / R1 never repeat. A latched
// button counts only after it was let go (held when F1, a modal or a page
// opened, or a capture ended).
class Buttons {
public:
    static constexpr double kRepeatDelay = 0.35, kRepeatEvery = 0.08;
    void update(std::uint32_t down, double now);
    bool held(Button b) const { return (down_ & ~latched_ & bit(b)) != 0; }
    bool fresh(Button b) const { return (fresh_ & bit(b)) != 0; }      // pressed this pass
    bool fired(Button b) const { return (fired_ & bit(b)) != 0; }      // pressed or repeated this pass
    double held_for(Button b, double now) const;                         // seconds since its press (0 when up)
    void latch() { latched_ = down_; fresh_ = fired_ = 0; }
    // Keys of this pass (the keyboard: each key event is a press of its own).
    void inject(std::uint32_t bits) { fresh_ |= bits; fired_ |= bits; }
    std::uint32_t down() const { return down_; }
    std::uint32_t any_fresh() const { return fresh_; }

private:
    std::uint32_t down_ = 0, latched_ = 0, fresh_ = 0, fired_ = 0;
    double pressed_at_[unsigned(Button::count)]{};
    double next_[unsigned(Button::count)]{};
};

// ---- Focus within a page's rows
// The next focusable row from `from` in `dir` (+1 / -1), wrapping only when
// `wrap`; -1 when none. from = -1 starts before the first row.
int next_focusable(const std::vector<NavRow>& rows, int from, int dir, bool wrap);
int first_focusable(const std::vector<NavRow>& rows);
int last_focusable(const std::vector<NavRow>& rows);
// The row with `id`; gone: the next focusable row below the place it had
// (`old_index`), else above; none focusable: -1.
int resolve_focus(const std::vector<NavRow>& rows, const std::string& id, int old_index);

// ---- Modals
enum class ModalKind : std::uint8_t { none, picker, confirm, text, capture };
struct Modal {
    ModalKind kind = ModalKind::none;
    std::string row;   // the row that opened it
    int focus = 0;     // picker: the entry; confirm: the button
    int count = 0;     // picker entries / confirm buttons
    double opened = 0; // when it opened (a confirm's A counts 250 ms later, after a release)
    static constexpr double kConfirmDelay = 0.25;
};

// ---- What a pass's buttons ask for
enum class Cmd : std::uint8_t {
    none,
    activate,     // A on the focused row (its cell `cell`)
    change,       // Left / Right on a value row (dir; for cells and bindings the cell moved instead)
    second,       // X on a row that has a second action
    open_link,    // Right on a link (A on a link is `activate`)
    page_back,    // B left a sub-page (the page stack popped)
    row_back,     // B on a row that takes it itself
    close,        // F1 closes (`input` names the button)
    category,     // the category changed (sidebar Up / Down, L1 / R1)
    modal_ok,     // picker: choose `modal.focus`; confirm: answer `modal.focus`; text: done
    modal_cancel, // picker, confirm, text: cancel
    modal_move,   // the modal's focus moved
    scroll,       // the right stick scrolls the list (dir: -1 up, +1 down; the focus stays)
    reveal,       // the focus row should come into view
    cursor,       // the first press in touch mode only showed the cursor
};
struct Command {
    Cmd cmd = Cmd::none;
    int dir = 0;
    const char* input = "";
};

enum class Area : std::uint8_t { sidebar, list };
enum class InputMode : std::uint8_t { pad, touch };

// One page's remembered place.
struct PageMemo {
    std::string row;
    int index = -1;
    float scroll = 0; // in menu units (u)
};

// The whole navigation state: one for the top and the lower screen.
class NavCore {
public:
    // Categories in the sidebar's order (their root page ids).
    void set_categories(std::vector<std::string> roots);
    const std::vector<std::string>& categories() const { return roots_; }
    int category() const { return category_; }
    // Shows a category's root page (its remembered row).
    void select_category(int index);
    // The page path ("saves", "saves/cloud", ...): the stack joined by '/'.
    std::string path() const;
    const std::vector<std::string>& stack() const { return stack_; }
    void open_page(const std::string& id); // a sub-page (the current row is remembered)
    bool pop_page();                       // false at a root
    // The page is gone (a player left, logged out): back to its parent.
    void page_invalid();
    // A place in one go (DW4_MENU_PAGE, Phase D's request_focus): the
    // category of path[0], the pages, the row; `show_cursor` puts the
    // focus area on the list in pad mode.
    bool request_focus(const std::string& page_path, const std::string& row_id, bool show_cursor);

    Area area = Area::sidebar;
    InputMode mode = InputMode::pad;
    Modal modal;
    std::string focus_id;    // the focused row of the shown page
    int focus = -1;          // its index after resolve()
    int cell = 0;            // the focused row's cell (cells, binding)
    std::map<std::string, int> cells; // the last cell per row id
    float scroll = 0;        // the shown page's scroll (u)

    // The page's rows of this pass: the focus is found again by its id.
    void resolve(const std::vector<NavRow>& rows);
    void set_focus(const std::vector<NavRow>& rows, int index);
    // One pass of buttons (`visible`: how many rows a page step moves).
    std::vector<Command> step(Buttons& buttons, double now, const std::vector<NavRow>& rows, int visible, float right_y = 0);
    // The modal opens / closes (the buttons held now are latched).
    void open_modal(ModalKind kind, const std::string& row, int count, int focus, double now, Buttons& buttons);
    void close_modal(Buttons& buttons);
    PageMemo& memo(const std::string& path) { return memo_[path]; }
    void remember();          // the shown page's row and scroll into its memo
    bool first_open = true;   // the first opening in the process: the sidebar on Saves

private:
    void show_page_memo();
    std::vector<std::string> roots_;
    int category_ = 0;
    std::vector<std::string> stack_;
    std::map<std::string, PageMemo> memo_;
};

// ---- Touch
// One finger on a column (the list, the sidebar, a picker): a tap is a
// down and up within 8 u, released on its target; a drag past 8 u
// scrolls 1:1 and is no tap; a release faster than 150 u/s (the mean of
// the last 80 ms) flings, slowing with exp(-dt / 0.3 s), stopping under
// 20 u/s, at most 8000 u/s; a down during a fling only stops it. A long
// press: held 0.6 s (binding slots: clear) or 0.4 s (stepper arrows:
// repeat at 8 steps a second) without moving.
class Gesture {
public:
    static constexpr float kSlop = 8.0f, kFlingStart = 150.0f, kFlingStop = 20.0f, kFlingMax = 8000.0f;
    static constexpr double kFlingDecay = 0.30, kVelocityWindow = 0.08, kPressShow = 0.08;
    // Events in menu units and seconds.
    void down(float x, float y, double now);
    void move(float x, float y, double now);
    // The finger lifted: true when it was a tap (no drag, not stopped a fling, no long press taken).
    bool up(float x, float y, double now);
    void cancel();
    bool active() const { return active_; }
    bool dragging() const { return dragging_; }
    float drag_delta() { const float d = pending_; pending_ = 0; return d; } // scroll this pass (u, positive: content up)
    // The fling's scroll for `dt` seconds (0 when none).
    float fling(double dt);
    bool flinging() const { return velocity_ != 0; }
    void stop() { velocity_ = 0; }
    // Held without moving for `seconds` (once: true at the first pass past it).
    bool long_press(double now, double seconds);
    bool pressed_shown(double now) const; // the press highlight (80 ms still)
    void take_long() { long_taken_ = true; } // the release is no tap (a held arrow repeated)
    float start_x() const { return x0_; }
    float start_y() const { return y0_; }
    double started() const { return t0_; }

private:
    bool active_ = false, dragging_ = false, stopped_fling_ = false, long_taken_ = false;
    float x0_ = 0, y0_ = 0, last_y_ = 0, pending_ = 0, velocity_ = 0;
    double t0_ = 0;
    struct Sample {
        double t;
        float y;
    };
    std::vector<Sample> samples_;
};

// The list's scroll clamped to its content (no overscroll).
float clamp_scroll(float scroll, float content, float view);
// The scroll that shows rows [top, bottom) with half a row's margin
// (`margin`), unchanged when they are in view; the first focusable row
// scrolls to the very top, the last to the very bottom (`first`, `last`).
float reveal_scroll(float scroll, float view, float content, float top, float bottom, float margin, bool first, bool last);

// ---- The binding capture's deadline (10 s)
struct CaptureClock {
    static constexpr double kSeconds = 10.0;
    double deadline = 0;
    bool waiting = false; // the capture waits for the button / finger that started it to be let go
    void start(double now) { deadline = now + kSeconds; }
    bool expired(double now) const { return deadline > 0 && now >= deadline; }
    float left(double now) const { return deadline > 0 ? float(std::max(0.0, deadline - now) / kSeconds) : 0.0f; }
};

} // namespace dw4::game::console
