#include "console_internal.hpp"

#include "host_view.hpp"
#include "pad_family.hpp"

#include <SDL3/SDL_keyboard.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

// (console_menu.hpp says what the console menu is; docs/CONSOLE_MENU.md
// has the whole design.)

namespace dw4::game {

// ---------------------------------------------------------------- the log

namespace {
std::deque<std::string> g_menu_log;
std::size_t g_menu_log_base = 0; // the index of g_menu_log.front()
bool g_menu_log_keep = false;
} // namespace

void menu_log_keep(bool keep) { g_menu_log_keep = keep; }

void menu_log(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    if (!g_menu_log_keep) return;
    g_menu_log.push_back(line);
    if (g_menu_log.size() > 4000) {
        g_menu_log.pop_front();
        ++g_menu_log_base;
    }
}

std::size_t menu_log_count() { return g_menu_log_base + g_menu_log.size(); }

std::vector<std::string> menu_log_since(std::size_t from) {
    std::vector<std::string> out;
    for (std::size_t i = std::max(from, g_menu_log_base); i < menu_log_count(); ++i) out.push_back(g_menu_log[i - g_menu_log_base]);
    return out;
}

MenuPlatform menu_platform(const HostView& view) {
#ifdef SDL_PLATFORM_ANDROID
    (void)view;
    return MenuPlatform::android;
#else
    static const bool android = [] {
        const char* text = std::getenv("DW4_MENU_PLATFORM");
        return text && std::string(text) == "android";
    }();
    return view.test_run && android ? MenuPlatform::android : MenuPlatform::pc;
#endif
}

namespace console {

NavRow Row::nav() const {
    NavRow r;
    r.id = id;
    r.kind = kind;
    r.focusable = kind != RowKind::section && kind != RowKind::progress && (kind != RowKind::info || !more.empty());
    r.cells = int(cells.size());
    r.second = kind == RowKind::binding;
    r.own_back = bool(on_back);
    return r;
}

namespace {

double steady_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---- colours (docs/CONSOLE_MENU.md 3)
ImU32 rgba(float r, float g, float b, float a = 1.0f) {
    return IM_COL32(int(r * 255.0f + 0.5f), int(g * 255.0f + 0.5f), int(b * 255.0f + 0.5f), int(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}
const ImU32 kBackground = rgba(0.035f, 0.04f, 0.055f, 0.96f);
const ImU32 kSidebar = rgba(0.07f, 0.08f, 0.10f);
const ImU32 kSelected = rgba(0.16f, 0.29f, 0.48f);
const ImU32 kAccent = rgba(0.26f, 0.59f, 0.98f);
const ImU32 kFocusFill = rgba(0.26f, 0.59f, 0.98f, 0.30f);
const ImU32 kPressed = rgba(1, 1, 1, 0.10f);
const ImU32 kText = rgba(0.93f, 0.94f, 0.96f);
const ImU32 kValue = rgba(0.55f, 0.80f, 1.0f);
const ImU32 kDim = rgba(0.60f, 0.64f, 0.71f);
const ImU32 kWarn = rgba(1.0f, 0.75f, 0.4f);
const ImU32 kError = rgba(1.0f, 0.45f, 0.40f);
const ImU32 kOk = rgba(0.49f, 0.85f, 0.57f);
const ImU32 kHair = rgba(1, 1, 1, 0.25f);
const ImU32 kButton = rgba(0.12f, 0.14f, 0.18f);
const ImU32 kCard = rgba(0.09f, 0.10f, 0.13f, 0.98f);

ImU32 tone_colour(InfoTone tone) {
    switch (tone) {
    case InfoTone::normal: return kText;
    case InfoTone::dim: return kDim;
    case InfoTone::warn: return kWarn;
    case InfoTone::error: return kError;
    case InfoTone::ok: return kOk;
    }
    return kDim;
}
ImU32 faded(ImU32 c, float alpha) {
    const unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFF;
    return (c & ~IM_COL32_A_MASK) | (ImU32(float(a) * alpha) << IM_COL32_A_SHIFT);
}

// ---- text
struct Ink {
    ImDrawList* dl = nullptr;
    ImFont* font = nullptr;
    float scale = 1.0f;
    float u(float v) const { return v * scale; }
    ImVec2 size(float size_u, const std::string& text, float wrap_px = 0.0f) const {
        if (text.empty()) return ImVec2(0, 0);
        return font->CalcTextSizeA(u(size_u), FLT_MAX, wrap_px, text.c_str());
    }
    void text(float size_u, float x, float y, ImU32 col, const std::string& t, float wrap_px = 0.0f) const {
        if (!t.empty()) dl->AddText(font, u(size_u), ImVec2(x, y), col, t.c_str(), nullptr, wrap_px);
    }
    void chevron(float cx, float cy, float h, bool right, ImU32 col, float thick) const {
        const float w = h * 0.5f;
        if (right) dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(cx - w * 0.5f, cy - h * 0.5f), ImVec2(cx + w * 0.5f, cy), ImVec2(cx - w * 0.5f, cy + h * 0.5f)}.data(), 3, col, 0, thick);
        else dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(cx + w * 0.5f, cy - h * 0.5f), ImVec2(cx - w * 0.5f, cy), ImVec2(cx + w * 0.5f, cy + h * 0.5f)}.data(), 3, col, 0, thick);
    }
    void check_mark(float cx, float cy, float h, ImU32 col, float thick) const {
        dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(cx - h * 0.45f, cy), ImVec2(cx - h * 0.1f, cy + h * 0.35f), ImVec2(cx + h * 0.5f, cy - h * 0.4f)}.data(), 3,
                        col, 0, thick);
    }
};

std::string upper(std::string text) {
    for (auto& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// The text shortened with "..." to fit `width` px (whole UTF-8 characters).
std::string fit_text(const Ink& ink, float size_u, const std::string& text, float width) {
    if (width <= 0 || ink.size(size_u, text).x <= width) return text;
    std::string cut = text;
    while (!cut.empty()) {
        // (One character off the end: its continuation bytes, then its first byte.)
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
        if (!cut.empty()) cut.pop_back();
        if (ink.size(size_u, cut + "...").x <= width) return cut + "...";
    }
    return "...";
}

// A text with a path wider than a line ("C:\Users\...\saves\states"): its
// own lines, broken after a '/' or '\' (else at spaces), so ImGui's wrap does
// not cut the path anywhere ("...Car|d A"). Any other text comes back as it
// is (ImGui wraps it at its spaces).
std::string break_paths(const Ink& ink, float size_u, const std::string& text, float width) {
    if (width <= 0 || text.find_first_of("/\\") == std::string::npos) return text;
    bool needed = false;
    for (std::size_t i = 0; i < text.size() && !needed;) {
        const std::size_t end = std::min(text.find_first_of(" \n", i), text.size());
        if (end > i) {
            const std::string word = text.substr(i, end - i);
            needed = word.find_first_of("/\\") != std::string::npos && ink.size(size_u, word).x > width;
        }
        i = end + 1;
    }
    if (!needed) return text;
    const auto trimmed = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    std::string out, line;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') {
            out += trimmed(line) + '\n';
            line.clear();
            ++i;
            continue;
        }
        // The next piece: up to and with a run of spaces, a '/' or a '\'.
        std::size_t end = i;
        while (end < text.size() && text[end] != ' ' && text[end] != '\n' && text[end] != '/' && text[end] != '\\') ++end;
        if (end < text.size() && (text[end] == '/' || text[end] == '\\')) ++end;
        while (end < text.size() && text[end] == ' ') ++end;
        const std::string piece = text.substr(i, end - i);
        i = end;
        if (line.empty() || ink.size(size_u, trimmed(line + piece)).x <= width) {
            line += piece;
        } else {
            out += trimmed(line) + '\n';
            line = piece;
        }
    }
    return out + trimmed(line);
}

// An info row's pictures (the prompts' preview: "A {A}  B {B} ..."): each
// with its caption before it, wrapped onto lines of `width` px. The
// positions (x from the line's start, the line) of each picture's caption.
struct GlyphSpot {
    float x = 0;
    int line = 0;
};
std::vector<GlyphSpot> glyph_spots(const Ink& ink, const Row& r, float width, int* lines) {
    std::vector<GlyphSpot> spots;
    float x = 0;
    int line = 0;
    for (std::size_t i = 0; i < r.glyphs.size(); ++i) {
        const std::string caption = i < r.glyph_captions.size() ? r.glyph_captions[i] : std::string();
        const float w = (caption.empty() ? 0.0f : ink.size(16, caption).x + ink.u(4)) + ink.u(22);
        if (x > 0 && x + w > width) {
            x = 0;
            ++line;
        }
        spots.push_back({x, line});
        x += w + ink.u(caption.empty() ? 4 : 14);
    }
    if (lines) *lines = r.glyphs.empty() ? 0 : line + 1;
    return spots;
}

// The widest label of a choice's / stepper's options (px): its value zone
// stays the same while it steps (cached per row and menu size).
float widest_option(State& st, const Ink& ink, const Row& r) {
    std::size_t key = std::hash<float>{}(ink.scale) ^ (r.options.size() * 0x9E3779B97F4A7C15ull);
    for (const auto& o : r.options) key = key * 31 + std::hash<std::string>{}(o.label);
    auto& w = st.widest[r.id];
    if (w.key != key || w.width <= 0) {
        w.key = key;
        w.width = 0;
        for (const auto& o : r.options) w.width = std::max(w.width, ink.size(19, o.label).x);
    }
    return w.width;
}

// Whether Left (-1) / Right (+1) changes a choice or a stepper (its arrow lit).
bool can_step(const Row& r, int dir) {
    if (!r.locked.empty()) return false;
    if (r.can_step) return r.can_step(dir);
    return dir < 0 ? r.current > 0 : r.current < int(r.options.size()) - 1;
}

const char* modal_name(ModalKind kind) {
    switch (kind) {
    case ModalKind::picker: return "picker";
    case ModalKind::confirm: return "confirm";
    case ModalKind::text: return "text";
    case ModalKind::capture: return "capture";
    case ModalKind::none: break;
    }
    return "";
}

std::uint32_t key_bits(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return bit(Button::up);
    case SDLK_DOWN: return bit(Button::down);
    case SDLK_LEFT: return bit(Button::left);
    case SDLK_RIGHT: return bit(Button::right);
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return bit(Button::a);
    case SDLK_ESCAPE: case SDLK_BACKSPACE: return bit(Button::b);
    case SDLK_PAGEUP: return bit(Button::l1);
    case SDLK_PAGEDOWN: return bit(Button::r1);
    case SDLK_HOME: return bit(Button::home);
    case SDLK_END: return bit(Button::end);
    case SDLK_DELETE: return bit(Button::x);
    default: return 0;
    }
}

Row* find_row(State& st, const std::string& id) {
    for (auto& r : st.page.rows)
        if (r.id == id) return &r;
    return nullptr;
}

void rebuild(State& st, HostView& view) {
    st.nav.set_categories(category_roots(st));
    build_page(st, view, st.page);
    if (!st.page.valid) {
        st.nav.page_invalid();
        menu_log("menu: page " + st.nav.path() + " (the page went)");
        build_page(st, view, st.page);
    }
    st.nav_rows.clear();
    for (const auto& r : st.page.rows) st.nav_rows.push_back(r.nav());
}

void blink(State& st, const Row& row) {
    st.blink_row = row.id;
    st.blink_until = st.now + 0.6;
    if (!row.locked.empty()) log_action(st, row, "locked (" + row.locked + ")", false);
}

void log_page(State& st) { menu_log("menu: page " + st.nav.path()); }

// ---- modals
void open_picker(State& st, const Row& row) {
    st.picker_row = row.id;
    st.picker_title = row.label;
    st.picker_options.clear();
    st.picker_index.clear();
    int focus = 0;
    for (int i = 0; i < int(row.options.size()); ++i) {
        if (std::find(row.picker_hidden.begin(), row.picker_hidden.end(), i) != row.picker_hidden.end()) continue;
        if (i == row.current) focus = int(st.picker_options.size());
        st.picker_options.push_back(row.options[std::size_t(i)]);
        st.picker_index.push_back(i);
    }
    st.picker_current = row.current;
    st.touch[0].picker_scroll = st.touch[1].picker_scroll = 0;
    st.nav.open_modal(ModalKind::picker, row.id, int(st.picker_options.size()), focus, st.now, st.buttons);
}

void open_text(State& st, const Row& row) {
    st.text_row = row.id;
    st.text_title = row.label;
    st.text_hint = row.hint;
    st.text_kind = row.kind;
    st.text_plain = row.plain;
    st.text_next = row.next_focus;
    st.text_error.clear();
    st.text_buffer.fill(0);
    if (row.kind != RowKind::password) std::snprintf(st.text_buffer.data(), st.text_buffer.size(), "%s", row.text.c_str());
    st.text_max = std::min<std::size_t>(row.max_length, st.text_buffer.size() - 1);
    st.text_min = row.min;
    st.text_max_value = row.max;
    st.text_commit = row.commit;
    st.text_frames = 0;
    st.text_logged_active = false;
    st.nav.open_modal(ModalKind::text, row.id, 0, 0, st.now, st.buttons);
    menu_log("menu: edit " + row.id + " start");
}

void close_text(State& st, const char* how) {
    menu_log("menu: edit " + st.text_row + " " + how);
    st.text_buffer.fill(0); // (no password stays in memory)
    st.text_release = true;
    st.nav.close_modal(st.buttons);
}

void commit_text(State& st) {
    const std::string text(st.text_buffer.data());
    if (st.text_kind == RowKind::number) {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
        if (text.empty() || (end && *end) || double(value) < st.text_min || double(value) > st.text_max_value) {
            char line[96];
            std::snprintf(line, sizeof line, "A number from %.0f to %.0f.", st.text_min, st.text_max_value);
            st.text_error = line;
            return;
        }
    }
    const std::string row_id = st.text_row, next_id = st.text_next;
    if (st.text_commit) st.text_commit(text);
    close_text(st, "commit");
    // The focus to the next row (the row's own next one: the password's Log
    // in), without opening it (Account -> Password -> Log in).
    const auto index_of = [&](const std::string& id) {
        return int(std::find_if(st.nav_rows.begin(), st.nav_rows.end(), [&](const NavRow& r) { return r.id == id && r.focusable; }) - st.nav_rows.begin());
    };
    if (const int named = next_id.empty() ? int(st.nav_rows.size()) : index_of(next_id); named < int(st.nav_rows.size())) {
        st.nav.set_focus(st.nav_rows, named);
    } else if (const int at = index_of(row_id); at < int(st.nav_rows.size())) {
        if (const int next = next_focusable(st.nav_rows, at, 1, false); next >= 0) st.nav.set_focus(st.nav_rows, next);
    }
    st.reveal = true;
}

void start_capture(State& st, HostView& view, const Row& row, int slot) {
    st.capture_key = row.key;
    st.capture_slot = std::size_t(std::clamp(slot, 0, 2));
    st.capture = {};
    st.capture.waiting = true;
    st.capture_running = false;
    st.capture_cancels = view.capture_cancels;
    view.console_capturing = true; // (the Back key cancels it from now on, also while it waits)
    st.nav.open_modal(ModalKind::capture, row.id, 0, 0, st.now, st.buttons);
}

void end_capture(State& st, HostView& view, const char* why) {
    if (st.capture_running && view.capture) view.capture.reset();
    if (why)
        menu_log(std::string("menu: capture ") + pad_key_name(st.capture_key) + " slot " + std::to_string(st.capture_slot + 1) + " cancelled (" + why +
                 ")");
    st.capture_running = false;
    st.capture = {};
    view.console_capturing = false;
    st.nav.close_modal(st.buttons);
}

void clear_binding(State& st, HostView& view, PadKey key, std::size_t slot) {
    ops::clear_pad_binding(host_ops(view), key, slot);
    menu_log(std::string("menu: Controls > ") + pad_key_name(key) + " slot " + std::to_string(slot + 1) + " = (cleared)");
    if (st.actions.toast) st.actions.toast("Controls", std::string(pad_key_name(key)) + ", input " + std::to_string(slot + 1) + ": cleared");
}

void update_capture(State& st, HostView& view) {
    if (st.nav.modal.kind != ModalKind::capture) return;
    // (The Back key cancelled it in host_event: menu_back_key.)
    if (view.capture_cancels != st.capture_cancels) {
        st.capture_cancels = view.capture_cancels;
        end_capture(st, view, "the Back key");
        return;
    }
    if (st.capture.waiting) {
        // (It starts once the button or finger that chose the slot is up.)
        const bool finger = st.touch[0].active || st.touch[1].active;
        if (st.buttons.down() == 0 && !finger) {
            view.capture = KeyCapture{std::size_t(st.capture_key), st.capture_slot, true};
            st.capture.waiting = false;
            st.capture.start(st.now);
            st.capture_running = true;
        }
        return;
    }
    if (!view.capture) {
        const PadInput input = view.settings.pad[std::size_t(st.capture_key)][st.capture_slot];
        menu_log(std::string("menu: capture ") + pad_key_name(st.capture_key) + " slot " + std::to_string(st.capture_slot + 1) + " = " +
                 pad_input_name(input));
        PadFamily family = view.prompt_family();
        if (family == PadFamily::keyboard) family = view.pads.pads().empty() ? PadFamily::xbox_one : view.pads.pads().front().family;
        if (st.actions.toast)
            st.actions.toast("Controls", std::string(pad_key_name(st.capture_key)) + ", input " + std::to_string(st.capture_slot + 1) + ": " +
                                             pad_input_label(family, view.settings, input));
        st.capture_running = false;
        view.console_capturing = false;
        st.nav.close_modal(st.buttons); // (latched until every button is up)
        return;
    }
    if (st.capture.expired(st.now)) end_capture(st, view, "timeout");
}

// ---- rows' actions
void run_confirmed(State& st, const Row& row, const std::shared_ptr<Confirm>& confirm, const std::function<void()>& run) {
    if (confirm) {
        auto c = std::make_shared<Confirm>(*confirm);
        if (c->row.empty()) c->row = row.id;
        open_confirm(st, c);
        return;
    }
    if (run) {
        log_action(st, row, row.label, false);
        run();
    }
}

void activate(State& st, HostView& view, Row& row) {
    if (!row.locked.empty()) {
        blink(st, row);
        return;
    }
    switch (row.kind) {
    case RowKind::toggle:
        if (row.toggle) {
            row.toggle(!row.on);
            log_change(st, row, row.on ? "Off" : "On");
        }
        break;
    case RowKind::choice:
    case RowKind::stepper:
        if (!row.options.empty()) open_picker(st, row);
        break;
    case RowKind::action:
    case RowKind::entry: run_confirmed(st, row, row.confirm, row.run); break;
    case RowKind::link:
        if (!row.target.empty()) {
            st.nav.open_page(row.target);
            st.buttons.latch();
            log_page(st);
        }
        break;
    case RowKind::text:
    case RowKind::password:
    case RowKind::number: open_text(st, row); break;
    case RowKind::cells: {
        if (st.nav.cell < 0 || st.nav.cell >= int(row.cells.size())) break;
        Cell& cell = row.cells[std::size_t(st.nav.cell)];
        if (!cell.locked.empty()) {
            // (The cell's reason as the row's line for a moment: the row
            // itself is not locked.)
            blink(st, row);
            log_action(st, row, cell.label + " locked (" + cell.locked + ")", false);
            st.note_row = row.id;
            st.note_text = cell.locked;
            st.note_tone = InfoTone::warn;
            st.note_until = st.now + 2.5;
            break;
        }
        // (A save state slot used: Saves opens on it again, docs/CONSOLE_MENU.md.)
        if (row.id.rfind("saves.state.", 0) == 0) st.nav.home["saves"] = row.id;
        if (cell.confirm) {
            auto c = std::make_shared<Confirm>(*cell.confirm);
            if (c->row.empty()) c->row = row.id;
            open_confirm(st, c);
        } else if (cell.run) {
            log_action(st, row, cell.label, false);
            cell.run();
        }
        break;
    }
    case RowKind::binding: start_capture(st, view, row, st.nav.cell); break;
    case RowKind::info:
        if (!row.more.empty()) {
            if (!st.expanded.erase(row.id)) st.expanded.insert(row.id);
        }
        break;
    case RowKind::section:
    case RowKind::progress: break;
    }
    (void)view;
}

void change(State& st, Row& row, int dir) {
    if (!row.locked.empty()) {
        blink(st, row);
        return;
    }
    if (row.kind == RowKind::toggle) {
        if (row.toggle && row.on != (dir > 0)) {
            row.toggle(dir > 0);
            log_change(st, row, dir > 0 ? "On" : "Off");
        }
        return;
    }
    if (row.step) {
        row.step(dir);
        return;
    }
    if (row.options.empty() || !row.pick) return;
    const int target = std::clamp(row.current + dir, 0, int(row.options.size()) - 1);
    if (target == row.current) return;
    row.pick(target);
    log_change(st, row, row.options[std::size_t(target)].label);
}

void modal_answer(State& st, HostView& view, bool ok) {
    switch (st.nav.modal.kind) {
    case ModalKind::picker: {
        const int focus = st.nav.modal.focus;
        st.nav.close_modal(st.buttons);
        menu_log("menu: dialog picker '" + st.picker_title + "' -> " +
                 (ok && focus < int(st.picker_options.size()) ? st.picker_options[std::size_t(focus)].label : std::string("Cancel")));
        if (!ok || focus >= int(st.picker_index.size())) return;
        if (Row* row = find_row(st, st.picker_row); row && row->locked.empty()) {
            const int index = st.picker_index[std::size_t(focus)];
            if (row->choose) {
                // (The preset: its confirm.)
                row->choose(index);
            } else if (row->pick && index != row->current) {
                row->pick(index);
                log_change(st, *row, row->options[std::size_t(index)].label);
            }
        }
        return;
    }
    case ModalKind::confirm: {
        const auto confirm = st.confirm;
        const int focus = st.nav.modal.focus;
        st.nav.close_modal(st.buttons);
        st.confirm.reset();
        if (!confirm) return;
        const bool chosen = ok && focus >= 0 && focus < int(confirm->actions.size()) && confirm->actions[std::size_t(focus)];
        menu_log("menu: dialog confirm '" + confirm->title + "' -> " +
                 (ok && focus < int(confirm->buttons.size()) ? confirm->buttons[std::size_t(focus)] : std::string("Cancel")));
        if (chosen) {
            if (Row* row = find_row(st, confirm->row)) log_action(st, *row, confirm->buttons[std::size_t(focus)], true);
            confirm->actions[std::size_t(focus)]();
        }
        return;
    }
    case ModalKind::text:
        if (ok) commit_text(st);
        else close_text(st, "cancel");
        return;
    case ModalKind::capture:
        end_capture(st, view, ok ? nullptr : "touch");
        return;
    case ModalKind::none: return;
    }
}

} // namespace

void open_confirm(State& st, std::shared_ptr<Confirm> confirm) {
    st.confirm = std::move(confirm);
    st.nav.open_modal(ModalKind::confirm, st.confirm->row, int(st.confirm->buttons.size()), st.confirm->focus, st.now, st.buttons);
}

void log_change(State& st, const Row& row, const std::string& value) {
    menu_log(std::string("menu: ") + category_title(st.nav.stack().empty() ? "" : st.nav.stack().front()) + " > " + row.label + " = " + value);
}

void log_action(State& st, const Row& row, const std::string& action, bool confirmed) {
    menu_log(std::string("menu: ") + category_title(st.nav.stack().empty() ? "" : st.nav.stack().front()) + " > " + row.label + ": " + action +
             (confirmed ? " (confirmed)" : ""));
}

void request_close(State& st, const char* input) {
    if (st.close_request.empty()) st.close_request = input;
}

ops::Target page_ops(HostView& view) { return host_ops(view); }

} // namespace console

// ---------------------------------------------------------------- ConsoleMenu

using namespace console;

ConsoleMenu::ConsoleMenu(F1Actions& actions) : s_(std::make_unique<State>(actions)) {
    s_->developer = std::getenv("DW4_MENU_DEVELOPER") && std::atoi(std::getenv("DW4_MENU_DEVELOPER")) != 0;
    s_->trace = std::getenv("DW4_MENU_TRACE") != nullptr;
    s_->layout_log = std::getenv("DW4_MENU_LAYOUT_LOG") != nullptr;
    s_->nav.set_categories(category_roots(*s_));
}

ConsoleMenu::~ConsoleMenu() = default;

void ConsoleMenu::opened(HostView& view, const std::string& tab, const std::string& section, double now) {
    State& st = *s_;
    st.now = now;
    st.platform = menu_platform(view);
    st.classic = false;
    st.close_request.clear();
    st.nav.modal = {};
    st.nav.set_categories(category_roots(st));
    // One-shots (docs/CONSOLE_MENU.md 2 g).
    std::string target;
    if (tab == "Settings" || tab == "Gameplay") target = "general";
    else if (tab == "Controls") target = "controls";
    else if (tab == "Saves") target = "saves";
    else if (tab == "Online") target = "online";
    else if (tab == "Cheats") target = "cheats";
    else if (tab == "Achievements") target = "achievements";
    else if (tab == "Debug") target = st.developer ? "developer" : "general";
    std::string row;
    if (section == "Graphics") target = "graphics";
    else if (section == "Screen2") {
        target = "display";
        row = "display.screen2_mode";
    }
    static bool page_done = false;
    std::optional<InputMode> input_mode;
    if (!page_done && (view.test_run || st.platform == MenuPlatform::android)) {
        page_done = true;
        if (const char* page = std::getenv("DW4_MENU_PAGE")) {
            const std::string text(page);
            const auto hash = text.find('#');
            target = text.substr(0, hash);
            row = hash == std::string::npos ? std::string() : text.substr(hash + 1);
        }
        if (const char* input = std::getenv("DW4_MENU_INPUT")) input_mode = std::string(input) == "touch" ? InputMode::touch : InputMode::pad;
    }
    if (!target.empty()) {
        st.nav.request_focus(target, row, true);
        st.nav.first_open = false;
    } else if (st.nav.first_open) {
        st.nav.first_open = false;
        st.nav.select_category(0);
        st.nav.area = Area::sidebar;
    }
    if (input_mode) st.nav.mode = *input_mode;
    st.latch_next = true;
    st.reveal = true;
    rebuild(st, view);
    menu_log("menu: page " + st.nav.path() + " (opened)");
    static bool dialog_done = false;
    if (!dialog_done && (view.test_run || st.platform == MenuPlatform::android)) {
        dialog_done = true;
        if (const char* dialog = std::getenv("DW4_MENU_DIALOG")) open_dialog(view, dialog, now);
    }
}

void ConsoleMenu::closed(HostView& view) {
    State& st = *s_;
    if (st.nav.modal.kind == ModalKind::capture) end_capture(st, view, nullptr);
    if (st.nav.modal.kind == ModalKind::text) close_text(st, "cancel");
    view.console_capturing = false;
    // (A state load that waited for its card is not done: nothing is applied.)
    if (st.pending_load >= 0) menu_log("menu: load state " + std::to_string(st.pending_load + 1) + " dropped (the menu closed)");
    st.pending_load = -1;
    st.peek = false;
    st.nav.modal = {};
    st.confirm.reset();
    st.nav.remember();
    st.classic = false;
    for (auto& place : st.touch) {
        place.list.cancel();
        place.sidebar.cancel();
        place.picker.cancel();
        place.has_press = false;
        place.active = nullptr;
        place.sidebar_reveal = true;
    }
}

bool ConsoleMenu::classic_requested() const { return s_->classic; }

std::string ConsoleMenu::take_close_request() { return std::exchange(s_->close_request, std::string()); }

std::string ConsoleMenu::page_path() const { return s_->nav.path(); }

std::string ConsoleMenu::focus_row() const { return s_->nav.focus_id; }

std::string ConsoleMenu::dialog_kind() const { return s_->nav.modal.kind == ModalKind::none ? std::string("none") : std::string(modal_name(s_->nav.modal.kind)); }

bool ConsoleMenu::text_sheet_open() const { return s_->nav.modal.kind == ModalKind::text; }

void ConsoleMenu::set_input_mode(bool touch) { s_->nav.mode = touch ? InputMode::touch : InputMode::pad; }

bool ConsoleMenu::request_focus(const std::string& page_path, const std::string& row_id, bool show_cursor) {
    const bool ok = s_->nav.request_focus(page_path, row_id, show_cursor);
    s_->reveal = true;
    return ok;
}

std::string ConsoleMenu::row_value(HostView& view, const std::string& id) {
    rebuild(*s_, view);
    if (const Row* row = find_row(*s_, id)) return row_value_text(*row);
    return {};
}

bool ConsoleMenu::open_dialog(HostView& view, const std::string& spec, double now) {
    State& st = *s_;
    st.now = now;
    const auto colon = spec.find(':');
    if (colon == std::string::npos) return false;
    const std::string kind = spec.substr(0, colon), id = spec.substr(colon + 1);
    rebuild(st, view);
    Row* row = find_row(st, id);
    if (!row) return false;
    st.nav.focus_id = id;
    st.nav.resolve(st.nav_rows);
    st.nav.area = Area::list;
    if (kind == "picker" && !row->options.empty()) open_picker(st, *row);
    else if (kind == "text") open_text(st, *row);
    else if (kind == "capture") start_capture(st, view, *row, 0);
    else if (kind == "confirm") {
        if (row->confirm) open_confirm(st, std::make_shared<Confirm>(*row->confirm));
        else
            for (const auto& cell : row->cells)
                if (cell.confirm) {
                    open_confirm(st, std::make_shared<Confirm>(*cell.confirm));
                    break;
                }
        if (row->choose) row->choose(1); // (the preset: its confirm)
    } else return false;
    return st.nav.modal.kind != ModalKind::none;
}

void ConsoleMenu::update(HostView& view, const ConsoleInput& input) {
    State& st = *s_;
    st.now = input.now;
    st.platform = menu_platform(view);
    ++st.pass;
    // ---- the buttons of this pass
    std::uint32_t down = 0;
    float right_y = 0;
    if (input.pad) {
        const MenuKeys& k = *input.pad;
        const auto set = [&](bool on, Button b) {
            if (on) down |= bit(b);
        };
        set(k.up || k.left_y > 0.5f, Button::up);
        set(k.down || k.left_y < -0.5f, Button::down);
        set(k.left || k.left_x < -0.5f, Button::left);
        set(k.right || k.left_x > 0.5f, Button::right);
        set(k.face_down, Button::a);
        set(k.face_right, Button::b);
        set(k.face_left, Button::x);
        set(k.face_up, Button::y);
        set(k.l1, Button::l1);
        set(k.r1, Button::r1);
        // (L2 / R2 page only with the controller's own layout.)
        if (view.settings.menu_buttons == MenuButtons::layout) {
            set(k.l2 > 0.5f, Button::l2);
            set(k.r2 > 0.5f, Button::r2);
        }
        set(k.start, Button::start);
        right_y = k.right_y;
    }
    // A hardware keyboard's arrows held (the system's repeats are no presses:
    // host_event; the menu's own repeat runs on these), not while a text
    // field takes the typing.
    if (!(ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput)) {
        int count = 0;
        if (const bool* keys = SDL_GetKeyboardState(&count); keys && count > SDL_SCANCODE_UP) {
            if (keys[SDL_SCANCODE_UP]) down |= bit(Button::up);
            if (keys[SDL_SCANCODE_DOWN]) down |= bit(Button::down);
            if (keys[SDL_SCANCODE_LEFT]) down |= bit(Button::left);
            if (keys[SDL_SCANCODE_RIGHT]) down |= bit(Button::right);
        }
    }
    st.buttons.update(down, input.now);
    if (st.latch_next) {
        st.buttons.latch();
        st.latch_next = false;
    }
    std::uint32_t keys = 0;
    for (const SDL_Keycode key : input.keys) keys |= key_bits(key);
    if (keys) st.buttons.inject(keys);
    // ---- live texts at 2 Hz (rounded)
    if (input.now - st.live_at >= 0.5) {
        st.live_at = input.now;
        char text[96];
        std::snprintf(text, sizeof text, "%.0f", menu_still() ? 60.0 : view.present_interval > 0 ? 1.0 / view.present_interval : 0.0);
        st.live_fps = text;
        st.live_frames = view.frame_times;
        if (menu_still()) st.live_frames.fill(1000.0f / 60.0f);
    }
    rebuild(st, view);
    update_capture(st, view);
    // A state load after its card was shown (it holds the game's thread for
    // seconds: the replay since its stage loaded).
    if (st.pending_load >= 0) {
        if (st.pending_load_drawn) {
            const int slot = std::exchange(st.pending_load, -1);
            const std::string row = st.pending_load_row;
            if (st.actions.load_state && st.actions.load_state(std::size_t(slot))) {
                request_close(st, "Load state");
            } else {
                st.note_row = row;
                st.note_text = st.actions.state_message ? *st.actions.state_message : std::string();
                st.note_tone = InfoTone::warn;
                st.note_until = st.now + 6.0;
            }
            rebuild(st, view);
        }
        st.built_pass = st.pass;
        return;
    }
    // Y held: a look at the game (the top screen; no modal).
    st.peek = st.last_place == 0 && st.nav.modal.kind == ModalKind::none && st.nav.mode == InputMode::pad && st.buttons.held(Button::y);
    if (st.peek) {
        st.built_pass = st.pass;
        return;
    }
    const int visible = std::max(1, st.layout[st.last_place].visible_rows);
    const auto commands = st.nav.step(st.buttons, input.now, st.nav_rows, visible, right_y);
    for (const Command& c : commands) {
        Row* row = st.nav.focus >= 0 && st.nav.focus < int(st.page.rows.size()) ? &st.page.rows[std::size_t(st.nav.focus)] : nullptr;
        switch (c.cmd) {
        case Cmd::activate:
            if (row) activate(st, view, *row);
            break;
        case Cmd::change:
            if (row) change(st, *row, c.dir);
            break;
        case Cmd::second:
            if (row && row->kind == RowKind::binding) {
                if (!row->locked.empty()) blink(st, *row);
                else clear_binding(st, view, row->key, std::size_t(std::max(st.nav.cell, 0)));
            }
            break;
        case Cmd::open_link:
            if (row) activate(st, view, *row);
            break;
        case Cmd::page_back:
        case Cmd::category:
            log_page(st);
            st.reveal = true;
            break;
        case Cmd::row_back:
            if (row && row->on_back) row->on_back();
            break;
        case Cmd::close: request_close(st, c.input); break;
        case Cmd::modal_ok: modal_answer(st, view, true); break;
        case Cmd::modal_cancel: modal_answer(st, view, false); break;
        case Cmd::scroll: st.stick_scroll += float(c.dir) * 14.0f; break;
        case Cmd::reveal:
        case Cmd::cursor: st.reveal = true; break;
        case Cmd::modal_move:
        case Cmd::none: break;
        }
        // (A command may have changed the page: the rest act on the new one.)
        rebuild(st, view);
    }
    st.built_pass = st.pass; // (draw shows this build)
    if (st.trace) {
        const std::string focus = st.nav.path() + "#" + st.nav.focus_id;
        if (focus != st.last_focus_trace) {
            st.last_focus_trace = focus;
            menu_log("menu: focus " + focus);
        }
    }
}

bool ConsoleMenu::animating() const {
    const State& st = *s_;
    for (const auto& place : st.touch)
        if (place.list.active() || place.list.flinging() || place.sidebar.active() || place.sidebar.flinging() || place.picker.active() ||
            place.picker.flinging() || place.has_press)
            return true;
    if (st.ease_start >= 0) return true;
    if (st.nav.modal.kind == ModalKind::capture || st.nav.modal.kind == ModalKind::text) return true;
    if (st.pending_load >= 0 || st.peek) return true;
    if (st.now < st.blink_until) return true;
    if (st.page.live) return true;
    if (st.stick_scroll != 0) return true;
    return false;
}

// ---------------------------------------------------------------- drawing

namespace {

struct RowGeom {
    float h = 0;               // px
    float label_w = 0;         // the label's wrap width
    bool value_below = false;  // the value zone on a line of its own
    float value_w = 0;         // the value zone's width
    float label_h = 0, desc_h = 0, extra_h = 0, locked_h = 0, badge_h = 0, value_h = 0;
};

bool has_value_zone(const Row& r) {
    return r.kind == RowKind::toggle || r.kind == RowKind::choice || r.kind == RowKind::stepper || text_row(r.kind) || r.kind == RowKind::cells ||
           r.kind == RowKind::binding;
}

RowGeom measure(State& st, const Ink& ink, const Row& r, float w) {
    RowGeom g;
    const float pad = ink.u(14);
    const float inner = w - pad * 2 - ink.u(6);
    switch (r.kind) {
    case RowKind::section: g.h = ink.u(30); return g;
    case RowKind::info: {
        if (!r.more.empty()) {
            g.label_w = inner - ink.u(70);
            g.label_h = ink.size(19, r.label, g.label_w).y;
            if (st.expanded.count(r.id)) g.desc_h = ink.u(6) + ink.size(16, break_paths(ink, 16, r.more, inner), inner).y;
            g.h = std::max(ink.u(52), g.label_h + g.desc_h + ink.u(20));
            return g;
        }
        g.label_w = inner;
        g.label_h = ink.size(16, break_paths(ink, 16, r.label, inner), inner).y;
        for (const auto& line : r.extra) g.extra_h += ink.u(2) + ink.size(16, break_paths(ink, 16, line.text, inner), inner).y;
        int lines = 0;
        glyph_spots(ink, r, inner, &lines);
        g.extra_h += float(lines) * ink.u(28);
        g.h = g.label_h + g.extra_h + ink.u(12);
        return g;
    }
    case RowKind::progress:
        g.label_h = ink.size(16, r.label, inner).y;
        g.h = g.label_h + (r.graph.empty() ? ink.u(12 + 14 + 10) : ink.u(12 + 160 + 10));
        return g;
    default: break;
    }
    if (has_value_zone(r)) {
        if (r.kind == RowKind::cells || r.kind == RowKind::binding) g.value_w = std::max(float(r.cells.size()) * ink.u(r.kind == RowKind::binding ? 96 : 104), ink.u(130));
        else g.value_w = std::clamp(0.38f * w, ink.u(130), ink.u(220));
        // (A choice's values wider than its zone: the zone grows, else it
        // takes a line of its own; from the widest value, so the zone and
        // its arrows stay where they are while it steps.)
        if ((r.kind == RowKind::choice || r.kind == RowKind::stepper) && !r.options.empty()) {
            const float need = widest_option(st, ink, r) + ink.u(96);
            if (need > g.value_w) g.value_w = std::min(need, inner * 0.62f);
            if (need > g.value_w) g.value_w = inner + 1.0f; // (below)
        }
        g.label_w = inner - g.value_w - ink.u(12);
        if (g.label_w < std::max(inner * 0.32f, ink.u(90))) {
            g.value_below = true;
            g.label_w = inner;
            g.value_w = inner;
        }
    } else {
        g.label_w = inner - (r.kind == RowKind::link ? ink.u(28) : 0);
        if (r.kind == RowKind::entry && (!r.right.empty() || r.progress >= 0)) g.label_w -= ink.u(120);
    }
    g.label_h = ink.size(19, r.label, g.label_w).y;
    if (!r.desc.empty()) g.desc_h = ink.u(3) + ink.size(15, r.desc, g.label_w).y;
    for (const auto& line : r.extra) g.extra_h += ink.u(3) + ink.size(15, line.text, g.label_w).y;
    if (!r.locked.empty()) g.locked_h = ink.u(3) + ink.size(15, r.locked, g.label_w).y;
    if (!r.badge.empty()) g.badge_h = ink.u(6 + 20);
    float content = g.label_h + g.desc_h + g.extra_h + g.locked_h + g.badge_h;
    if (g.value_below) {
        g.value_h = r.kind == RowKind::toggle ? ink.u(30) : ink.u(48);
        content += ink.u(8) + g.value_h;
    }
    g.h = std::max(ink.u(52), content + ink.u(20));
    return g;
}

// The value zone of a row (toggle, choice / stepper with arrows, text, cells).
void draw_value(State& st, HostView& view, const Ink& ink, const Row& r, float x0, float y0, float x1, float y1, bool focused, int row_index,
                TouchPlace& tp, float alpha, float clip0, float clip1) {
    // (A target of a row the list's edge cuts is cut there too: never over the header.)
    const auto hit = [&](Hit::Kind kind, float hx0, float hy0, float hx1, float hy1, int cell) {
        hy0 = std::max(hy0, clip0);
        hy1 = std::min(hy1, clip1);
        if (hy1 > hy0) tp.hits.push_back({kind, hx0, hy0, hx1, hy1, row_index, cell});
    };
    const float cy = (y0 + y1) * 0.5f;
    const ImU32 value_col = faded(kValue, alpha);
    switch (r.kind) {
    case RowKind::toggle: {
        const float tw = ink.u(56), th = ink.u(30), kr = ink.u(12);
        const float tx1 = x1, tx0 = tx1 - tw, ty0 = cy - th * 0.5f;
        ink.dl->AddRectFilled(ImVec2(tx0, ty0), ImVec2(tx1, ty0 + th), faded(r.on ? kAccent : rgba(0.25f, 0.27f, 0.32f), alpha), th * 0.5f);
        const float kx = r.on ? tx1 - th * 0.5f : tx0 + th * 0.5f;
        ink.dl->AddCircleFilled(ImVec2(kx, cy), kr, faded(kText, alpha));
        const std::string word = r.on ? "On" : "Off";
        const ImVec2 ws = ink.size(19, word);
        ink.text(19, tx0 - ink.u(10) - ws.x, cy - ws.y * 0.5f, r.on ? value_col : faded(kDim, alpha), word);
        break;
    }
    case RowKind::choice:
    case RowKind::stepper: {
        const float aw = ink.u(44);
        // (Lit when the step does something: the preset's Custom steps both ways.)
        // (A locked row's arrows as if it were not: the whole row is faded.)
        const bool locked = !r.locked.empty();
        const bool left_on = locked ? r.current > 0 : can_step(r, -1);
        const bool right_on = locked ? r.current < int(r.options.size()) - 1 : can_step(r, 1);
        ink.chevron(x0 + aw * 0.5f, cy, ink.u(16), false, faded(left_on ? kText : kHair, alpha), ink.u(2.5f));
        ink.chevron(x1 - aw * 0.5f, cy, ink.u(16), true, faded(right_on ? kText : kHair, alpha), ink.u(2.5f));
        const std::string text = r.current >= 0 && r.current < int(r.options.size()) ? r.options[std::size_t(r.current)].label : r.value;
        const float tw = x1 - x0 - aw * 2;
        const ImVec2 ts = ink.size(19, text, tw);
        ink.dl->PushClipRect(ImVec2(x0 + aw, y0), ImVec2(x1 - aw, y1), true);
        if (ts.y > ink.u(26)) ink.text(19, x0 + aw, cy - ts.y * 0.5f, value_col, text, tw);
        else ink.text(19, x0 + aw + (tw - ts.x) * 0.5f, cy - ts.y * 0.5f, value_col, text);
        ink.dl->PopClipRect();
        hit(Hit::arrow_left, x0, y0, x0 + aw, y1, -1);
        hit(Hit::arrow_right, x1 - aw, y0, x1, y1, -1);
        break;
    }
    case RowKind::text:
    case RowKind::password:
    case RowKind::number: {
        const bool empty = r.text.empty();
        const std::string shown = empty ? "Not set" : r.kind == RowKind::password ? "****" : r.text;
        const ImVec2 ts = ink.size(19, shown);
        ink.dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
        ink.text(19, std::max(x0, x1 - ts.x), cy - ts.y * 0.5f, empty ? faded(kDim, alpha) : value_col, shown);
        ink.dl->PopClipRect();
        break;
    }
    case RowKind::cells:
    case RowKind::binding: {
        const int n = int(r.cells.size());
        if (n == 0) break;
        const float gap = ink.u(6);
        const float cw = (x1 - x0 - gap * float(n - 1)) / float(n);
        const float ch = ink.u(44);
        PadFamily family = view.prompt_family();
        if (family == PadFamily::keyboard) family = view.pads.pads().empty() ? PadFamily::xbox_one : view.pads.pads().front().family;
        for (int i = 0; i < n; ++i) {
            const Cell& cell = r.cells[std::size_t(i)];
            const float cx0 = x0 + float(i) * (cw + gap), cx1 = cx0 + cw;
            const float cy0 = cy - ch * 0.5f, cy1 = cy + ch * 0.5f;
            const bool cell_focus = focused && st.nav.cell == i;
            ink.dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx1, cy1), faded(cell_focus ? kSelected : kButton, alpha), ink.u(6));
            if (cell_focus) ink.dl->AddRect(ImVec2(cx0, cy0), ImVec2(cx1, cy1), kAccent, ink.u(6), 0, ink.u(2));
            float tx = cx0 + ink.u(8);
            if (cell.has_glyph) {
                draw_console_glyph(ink.dl, family, cell.glyph, tx, cy - ink.u(11), ink.u(22), faded(IM_COL32_WHITE, alpha));
                tx += ink.u(26);
            }
            const ImU32 tc = !cell.locked.empty() ? faded(kDim, alpha * 0.45f) : r.kind == RowKind::binding && cell.label == "-" ? faded(kDim, alpha)
                                                                                                                                 : faded(kText, alpha);
            const ImVec2 ts = ink.size(r.kind == RowKind::binding ? 15 : 19, cell.label);
            ink.dl->PushClipRect(ImVec2(cx0, cy0), ImVec2(cx1, cy1), true);
            const float text_x = cell.has_glyph ? tx : cx0 + std::max(ink.u(4), (cw - ts.x) * 0.5f);
            ink.text(r.kind == RowKind::binding ? 15 : 19, text_x, cy - ts.y * 0.5f, tc, cell.label);
            ink.dl->PopClipRect();
            // (The cell takes the row's whole height: a 48 u target at least.)
            hit(Hit::cell_button, cx0, std::min(cy0, y0), cx1, std::max(cy1, y1), i);
        }
        break;
    }
    default: break;
    }
}

void button_rect(const Ink& ink, float x0, float y0, float x1, float y1, const std::string& label, ImU32 fill, bool focus, float size = 19) {
    ink.dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill, ink.u(8));
    if (focus) ink.dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), kAccent, ink.u(8), 0, ink.u(2));
    const ImVec2 ts = ink.size(size, label);
    ink.text(size, (x0 + x1 - ts.x) * 0.5f, (y0 + y1 - ts.y) * 0.5f, kText, label);
}

struct HintEntry {
    std::vector<PromptFace> faces;
    std::string text;
};

} // namespace

void ConsoleMenu::draw(HostView& view, ConsolePlace place) {
    State& st = *s_;
    const int pi = place == ConsolePlace::lower ? 1 : 0;
    // (Moved between the screens: the other screen's list is narrower or
    // wider, so the focus row comes into view there; its sidebar too.)
    if (pi != st.last_place) {
        st.reveal = true;
        st.touch[pi].sidebar_reveal = true;
    }
    st.last_place = pi;
    const double now = steady_now();
    const double dt = st.last_draw > 0 ? std::clamp(now - st.last_draw, 0.0, 0.1) : 1.0 / 60.0;
    st.last_draw = now;
    ImGuiIO& io = ImGui::GetIO();
    Ink ink;
    ink.font = ui_scalable_font();
    if (!ink.font) ink.font = ImGui::GetFont();
    ink.scale = ui_scale();
    const auto U = [&](float v) { return v * ink.scale; };
    // (The page update() built this pass: one build per pass.)
    if (st.built_pass != st.pass) rebuild(st, view);
    st.built_pass = st.pass;
    st.nav.resolve(st.nav_rows);
    TouchPlace& tp = st.touch[pi];
    tp.hits.clear();
    // Y held on the top screen: only a note over the game (a look at a change).
    if (st.peek && place == ConsolePlace::top) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ink.dl = dl;
        const std::string text = "Release the button to return to the menu";
        const PadFamily family = view.prompt_family();
        const auto faces = console_faces(ConsoleGlyph::look, family, view.settings, st.platform);
        const ImVec2 ts = ink.size(16, text);
        const float w = ts.x + (faces.empty() ? 0.0f : U(30)) + U(28), h = U(40);
        const float x0 = (io.DisplaySize.x - w) * 0.5f, y0 = io.DisplaySize.y - h - U(16);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), rgba(0.035f, 0.04f, 0.055f, 0.85f), h * 0.5f);
        float x = x0 + U(14);
        for (const auto& f : faces) {
            draw_console_glyph(dl, family, f, x, y0 + (h - U(26)) * 0.5f, U(26));
            x += U(30);
        }
        ink.text(16, x, y0 + (h - ts.y) * 0.5f, kText, text);
        return;
    }
    // The text field lets go of the keyboard in the context that showed it.
    if (st.text_release) {
        st.text_release = false;
        ImGui::ClearActiveID();
    }
    ImVec2 a0, a1;
    bool window = false;
    if (place == ConsolePlace::top) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window = ImGui::Begin("##console_f1", nullptr,
                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar(2);
        a0 = ImVec2(U(8), U(8));
        a1 = ImVec2(io.DisplaySize.x - U(8), io.DisplaySize.y - U(8));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, kBackground);
    } else {
        a0 = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        a1 = ImVec2(a0.x + avail.x, a0.y + avail.y);
    }
    // (A press counts only on the console's own window, or the lower
    // screen's panel: never through a window over it.)
    const bool hovered_here = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    // (A new menu size or width: the focus row and the category into view again.)
    if (tp.last_scale != ink.scale || tp.last_width != a1.x - a0.x) {
        if (tp.last_scale != 0) st.reveal = true;
        tp.sidebar_reveal = true;
        tp.last_scale = ink.scale;
        tp.last_width = a1.x - a0.x;
    }
    ink.dl = ImGui::GetWindowDrawList();
    const bool pad_mode = st.nav.mode == InputMode::pad;
    const ModalKind modal = st.nav.modal.kind;
    // ---------------- the header
    const float hy0 = a0.y, hy1 = a0.y + U(48);
    float close_x1 = a1.x;
    if (place == ConsolePlace::top) {
        const TouchRect r = touch_menu_rect(io.DisplaySize.x, io.DisplaySize.y);
        if (r.y0 < hy1 + U(4)) close_x1 = std::min(close_x1, r.x0 - U(8));
    }
    const std::string close_label = place == ConsolePlace::top ? "Close" : "Close menu";
    const float close_x0 = close_x1 - U(120);
    button_rect(ink, close_x0, hy0, close_x1, hy1, close_label, kButton, false);
    tp.hits.push_back({Hit::close, close_x0, hy0, close_x1, hy1});
    float title_x = a0.x + U(4);
    if (st.nav.stack().size() > 1) {
        const float bx1 = a0.x + U(112);
        ink.dl->AddRectFilled(ImVec2(a0.x, hy0), ImVec2(bx1, hy1), kButton, U(8));
        ink.chevron(a0.x + U(24), (hy0 + hy1) * 0.5f, U(18), false, kText, U(2.5f));
        const ImVec2 ts = ink.size(19, "Back");
        ink.text(19, a0.x + U(40), (hy0 + hy1 - ts.y) * 0.5f, kText, "Back");
        tp.hits.push_back({Hit::back, a0.x, hy0, bx1, hy1});
        title_x = bx1 + U(14);
    }
    {
        // (The lower screen is narrow: on a sub-page only its own title,
        // without the chip.)
        const bool sub_page = st.nav.stack().size() > 1;
        const bool chip_shown = place == ConsolePlace::top || !sub_page;
        float title_x1 = close_x0 - U(8);
        if (chip_shown) {
            const bool paused = st.actions.game_paused && st.actions.game_paused();
            const std::string chip = paused ? "Game paused" : "Game runs";
            const ImVec2 cs = ink.size(15, chip);
            const float cx1 = close_x0 - U(12), cx0 = cx1 - cs.x - U(20);
            const float cy = (hy0 + hy1) * 0.5f;
            ink.dl->AddRectFilled(ImVec2(cx0, cy - U(13)), ImVec2(cx1, cy + U(13)), paused ? rgba(0.2f, 0.22f, 0.28f) : rgba(0.12f, 0.24f, 0.16f), U(13));
            ink.text(15, cx0 + U(10), cy - cs.y * 0.5f, paused ? kDim : kOk, chip);
            title_x1 = cx0 - U(8);
        }
        const std::string title = fit_text(ink, 22, place == ConsolePlace::lower && sub_page ? st.page.title : page_trail(st, view), title_x1 - title_x);
        ink.dl->PushClipRect(ImVec2(title_x, hy0), ImVec2(std::max(title_x, title_x1), hy1), true);
        const ImVec2 ts = ink.size(22, title);
        ink.text(22, title_x, (hy0 + hy1 - ts.y) * 0.5f, kText, title);
        ink.dl->PopClipRect();
    }
    // ---------------- the body
    // (The hint bar's room in both input modes: nothing moves when a tap
    // follows a button, docs/CONSOLE_MENU.md.)
    const float hint_h = U(36);
    const float by0 = hy1 + U(6), by1 = a1.y - hint_h - U(4);
    const float width = a1.x - a0.x;
    const float sw = std::clamp((place == ConsolePlace::top ? 0.26f : 0.30f) * width, U(160), U(260));
    // The sidebar.
    {
        const auto& roots = st.nav.categories();
        const float sx0 = a0.x, sx1 = a0.x + sw;
        ink.dl->AddRectFilled(ImVec2(sx0, by0), ImVec2(sx1, by1), kSidebar, U(8));
        tp.hits.push_back({Hit::sidebar_area, sx0, by0, sx1, by1});
        const float eh = std::clamp((by1 - by0) / float(std::max<std::size_t>(roots.size(), 1)), U(40), U(52));
        const float total = eh * float(roots.size());
        // (Its drag scrolls in the touch part below; what the finger moved
        // as it let go, here: the gesture's u to px.) The chosen category in
        // view when it changed by a button or the menu opened (px).
        if (const float d = tp.sidebar.drag_delta(); d != 0) tp.sidebar_scroll += d * ink.scale;
        const int chosen = st.nav.category();
        if ((tp.sidebar_reveal || tp.sidebar_shown != chosen) && !(tp.active == &tp.sidebar && tp.sidebar.dragging())) {
            const float top = float(chosen) * eh, bottom = top + eh, room = by1 - by0;
            if (top < tp.sidebar_scroll) tp.sidebar_scroll = top;
            else if (bottom > tp.sidebar_scroll + room) tp.sidebar_scroll = bottom - room;
            tp.sidebar_shown = chosen;
            tp.sidebar_reveal = false;
        }
        tp.sidebar_scroll = clamp_scroll(tp.sidebar_scroll, total, by1 - by0);
        ink.dl->PushClipRect(ImVec2(sx0, by0), ImVec2(sx1, by1), true);
        for (std::size_t i = 0; i < roots.size(); ++i) {
            const float ey0 = by0 + float(i) * eh - tp.sidebar_scroll, ey1 = ey0 + eh;
            const bool selected = int(i) == st.nav.category();
            if (selected) {
                ink.dl->AddRectFilled(ImVec2(sx0, ey0), ImVec2(sx1, ey1), kSelected, U(6));
                ink.dl->AddRectFilled(ImVec2(sx0, ey0), ImVec2(sx0 + U(6), ey1), kAccent);
                if (pad_mode && st.nav.area == Area::sidebar && modal == ModalKind::none)
                    ink.dl->AddRect(ImVec2(sx0 + U(1), ey0 + U(1)), ImVec2(sx1 - U(1), ey1 - U(1)), kAccent, U(6), 0, U(2));
            }
            if (tp.has_press && tp.pressed.kind == Hit::category && tp.pressed.index == int(i) && tp.sidebar.pressed_shown(now))
                ink.dl->AddRectFilled(ImVec2(sx0, ey0), ImVec2(sx1, ey1), kPressed, U(6));
            const std::string title = category_title(roots[i]);
            const ImVec2 ts = ink.size(20, title);
            ink.text(20, sx0 + U(20), (ey0 + ey1 - ts.y) * 0.5f, selected ? kText : kDim, title);
            tp.hits.push_back({Hit::category, sx0, std::max(ey0, by0), sx1, std::min(ey1, by1), int(i)});
        }
        ink.dl->PopClipRect();
        if (total > by1 - by0 + 0.5f) {
            const float room = by1 - by0, bar_h = std::max(U(24), room * room / total);
            const float bar_y = by0 + (room - bar_h) * (tp.sidebar_scroll / std::max(total - room, 1.0f));
            ink.dl->AddRectFilled(ImVec2(sx1 - U(4), bar_y), ImVec2(sx1, bar_y + bar_h), rgba(1, 1, 1, 0.3f), U(2));
        }
    }
    // The list.
    const float lx0 = a0.x + sw + U(14), lx1 = a1.x;
    const float lw = lx1 - lx0;
    tp.hits.push_back({Hit::list_area, lx0, by0, lx1, by1});
    std::vector<RowGeom> geoms;
    geoms.reserve(st.page.rows.size());
    float content = 0;
    for (const auto& r : st.page.rows) {
        geoms.push_back(measure(st, ink, r, lw));
        content += geoms.back().h;
    }
    const float view_h = by1 - by0;
    Layout& layout = st.layout[pi];
    layout.valid = true;
    layout.view = view_h / ink.scale;
    layout.content = content / ink.scale;
    layout.rows.clear();
    {
        float y = 0;
        for (const auto& g : geoms) {
            layout.rows.push_back({y / ink.scale, g.h / ink.scale});
            y += g.h;
        }
    }
    // Scrolling (u): the drag, the fling, the right stick, the focus row's reveal (eased on the top screen).
    float& scroll = st.nav.scroll;
    if (st.stick_scroll != 0) {
        scroll += st.stick_scroll;
        st.stick_scroll = 0;
        st.ease_start = -1;
    }
    if (const float d = tp.list.drag_delta(); d != 0) {
        scroll += d; // (u, as the gesture measures)
        st.ease_start = -1;
    }
    if (tp.list.flinging()) {
        scroll += tp.list.fling(dt);
        const float clamped = clamp_scroll(scroll, layout.content, layout.view);
        if (clamped != scroll) tp.list.stop(); // (hard at the ends)
        st.ease_start = -1;
    }
    if (st.reveal && st.nav.focus >= 0 && st.nav.focus < int(layout.rows.size())) {
        st.reveal = false;
        int top_row = st.nav.focus;
        while (top_row > 0 && st.page.rows[std::size_t(top_row - 1)].kind == RowKind::info && st.page.rows[std::size_t(top_row - 1)].more.empty()) --top_row;
        const float top = layout.rows[std::size_t(top_row)].first;
        const auto& fr = layout.rows[std::size_t(st.nav.focus)];
        const bool first = st.nav.focus == first_focusable(st.nav_rows), last = st.nav.focus == last_focusable(st.nav_rows);
        const float target = reveal_scroll(scroll, layout.view, layout.content, top, fr.first + fr.second, fr.second * 0.5f, first, last);
        if (target != scroll) {
            if (place == ConsolePlace::top) {
                st.ease_from = scroll;
                st.ease_to = target;
                st.ease_start = now;
            } else {
                scroll = target;
            }
        }
    } else if (st.reveal && st.nav.focus < 0) {
        st.reveal = false;
    }
    if (st.ease_start >= 0) {
        const double t = std::clamp((now - st.ease_start) / 0.12, 0.0, 1.0);
        const float e = float(1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t));
        scroll = st.ease_from + (st.ease_to - st.ease_from) * e;
        if (t >= 1.0) st.ease_start = -1;
    }
    scroll = clamp_scroll(scroll, layout.content, layout.view);
    if (st.trace && !tp.list.active() && !tp.list.flinging() && st.ease_start < 0) {
        // (Where a drag, a fling or a reveal left the list: once it settled.)
        const std::string settled = st.nav.path() + " " + std::to_string(int(std::lround(scroll)));
        if (settled != st.last_scroll_trace) {
            st.last_scroll_trace = settled;
            menu_log("menu: scroll " + settled);
        }
    }
    // Rows.
    const bool list_focus = pad_mode && st.nav.area == Area::list && modal == ModalKind::none;
    ink.dl->PushClipRect(ImVec2(lx0, by0), ImVec2(lx1, by1), true);
    float y = by0 - scroll * ink.scale;
    int fully_visible = 0;
    std::string layout_lines;
    for (std::size_t i = 0; i < st.page.rows.size(); ++i) {
        const Row& r = st.page.rows[i];
        const RowGeom& g = geoms[i];
        const float y0 = y, y1 = y + g.h;
        y = y1;
        if (y0 >= by0 - 1 && y1 <= by1 + 1) ++fully_visible;
        if (st.layout_log)
            layout_lines += r.id + " " + std::to_string(int(r.kind)) + " " + std::to_string(int(lx0)) + " " + std::to_string(int(y0 + scroll * ink.scale)) +
                            " " + std::to_string(int(lw)) + " " + std::to_string(int(g.h)) + "\n";
        if (y1 < by0 || y0 > by1) continue;
        const float x0 = lx0, x1 = lx1 - U(6);
        const float pad = U(14);
        const bool focused = list_focus && int(i) == st.nav.focus;
        const bool locked = !r.locked.empty();
        const float alpha = locked ? 0.45f : 1.0f;
        // Background, focus, press.
        if (r.kind != RowKind::section && r.kind != RowKind::info && r.kind != RowKind::progress)
            ink.dl->AddLine(ImVec2(x0 + pad, y1 - 0.5f), ImVec2(x1 - pad, y1 - 0.5f), rgba(1, 1, 1, 0.06f));
        if (r.highlight) ink.dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), rgba(1.0f, 0.85f, 0.35f, 0.08f), U(6));
        if (focused) {
            ink.dl->AddRectFilled(ImVec2(x0, y0 + U(1)), ImVec2(x1, y1 - U(1)), kFocusFill, U(6));
            ink.dl->AddRect(ImVec2(x0, y0 + U(1)), ImVec2(x1, y1 - U(1)), kAccent, U(6), 0, U(2));
            ink.dl->AddRectFilled(ImVec2(x0, y0 + U(1)), ImVec2(x0 + U(6), y1 - U(1)), kAccent, U(3));
        }
        if (tp.has_press && tp.pressed.index == int(i) && (tp.pressed.kind == Hit::row) && tp.active && tp.active->pressed_shown(now))
            ink.dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPressed, U(6));
        const float tx = x0 + pad;
        switch (r.kind) {
        case RowKind::section: {
            const std::string text = upper(r.label);
            const ImVec2 ts = ink.size(15, text);
            ink.text(15, tx, y1 - U(8) - ts.y, kDim, text);
            ink.dl->AddLine(ImVec2(tx + ts.x + U(10), y1 - U(8) - ts.y * 0.5f), ImVec2(x1 - pad, y1 - U(8) - ts.y * 0.5f), kHair, 1.0f);
            continue;
        }
        case RowKind::info: {
            if (!r.more.empty()) {
                const ImVec2 ls = ink.size(19, r.label, g.label_w);
                ink.text(19, tx, y0 + U(10), kText, r.label, g.label_w);
                const bool open = st.expanded.count(r.id) != 0;
                const std::string word = open ? "Less" : "More";
                const ImVec2 ws = ink.size(15, word);
                ink.text(15, x1 - pad - ws.x, y0 + U(10) + (ls.y - ws.y) * 0.5f, kValue, word);
                if (open) {
                    const float wrap = x1 - pad - tx - U(6);
                    ink.text(16, tx, y0 + U(10) + ls.y + U(6), kDim, break_paths(ink, 16, r.more, wrap), wrap);
                }
                tp.hits.push_back({Hit::row, x0, std::max(y0, by0), x1, std::min(y1, by1), int(i)});
                continue;
            }
            // (A path wider than the row: broken after its separators.)
            ink.text(16, tx, y0 + U(6), tone_colour(r.tone), break_paths(ink, 16, r.label, g.label_w), g.label_w);
            float ey = y0 + U(6) + g.label_h;
            for (const auto& line : r.extra) {
                ey += U(2);
                const std::string text = break_paths(ink, 16, line.text, g.label_w);
                ink.text(16, tx, ey, tone_colour(line.tone), text, g.label_w);
                ey += ink.size(16, text, g.label_w).y;
            }
            // The pictures, each after its caption ("L {L2}"), wrapped.
            const auto spots = glyph_spots(ink, r, g.label_w, nullptr);
            for (std::size_t k = 0; k < r.glyphs.size() && k < spots.size(); ++k) {
                float gx = tx + spots[k].x;
                const float gy = ey + U(4) + float(spots[k].line) * U(28);
                if (k < r.glyph_captions.size() && !r.glyph_captions[k].empty()) {
                    const ImVec2 cs = ink.size(16, r.glyph_captions[k]);
                    ink.text(16, gx, gy + (U(22) - cs.y) * 0.5f, kDim, r.glyph_captions[k]);
                    gx += cs.x + U(4);
                }
                draw_console_glyph(ink.dl, r.glyph_family, r.glyphs[k], gx, gy, U(22));
            }
            continue;
        }
        case RowKind::progress: {
            ink.text(16, tx, y0 + U(6), kDim, r.label, g.label_w);
            if (!r.graph.empty()) {
                // A line graph (Performance's frame times): 160 u high, its
                // top at graph_full, a line at 16.7 ms.
                const float gx0 = tx, gx1 = x1 - pad, gy0 = y0 + U(6) + g.label_h + U(6), gy1 = gy0 + U(160);
                ink.dl->AddRectFilled(ImVec2(gx0, gy0), ImVec2(gx1, gy1), rgba(0.2f, 0.22f, 0.27f, 0.6f), U(4));
                const float full = std::max(r.graph_full, 1.0f);
                const float frame_y = gy1 - (gy1 - gy0) * std::clamp(16.67f / full, 0.0f, 1.0f);
                ink.dl->AddLine(ImVec2(gx0, frame_y), ImVec2(gx1, frame_y), kHair, 1.0f);
                std::vector<ImVec2> points;
                points.reserve(r.graph.size());
                const float step = r.graph.size() > 1 ? (gx1 - gx0) / float(r.graph.size() - 1) : 0.0f;
                for (std::size_t k = 0; k < r.graph.size(); ++k)
                    points.push_back(ImVec2(gx0 + step * float(k), gy1 - (gy1 - gy0) * std::clamp(r.graph[k] / full, 0.0f, 1.0f)));
                ink.dl->AddPolyline(points.data(), int(points.size()), kAccent, 0, std::max(1.0f, U(1.5f)));
                continue;
            }
            const float bar_y = y0 + U(6) + g.label_h + U(6);
            ink.dl->AddRectFilled(ImVec2(tx, bar_y), ImVec2(x1 - pad, bar_y + U(14)), rgba(0.2f, 0.22f, 0.27f), U(7));
            const float f = std::clamp(r.progress < 0 ? float(std::fmod(now, 1.0)) : r.progress, 0.0f, 1.0f);
            ink.dl->AddRectFilled(ImVec2(tx, bar_y), ImVec2(tx + (x1 - pad - tx) * f, bar_y + U(14)), kAccent, U(7));
            continue;
        }
        default: break;
        }
        // Label, description, coloured lines, the lock's reason, the badge.
        const ImU32 label_col = faded(r.destructive ? kError : kText, alpha);
        float cy = y0 + U(10);
        if (!g.value_below && has_value_zone(r)) {
            const float block = g.label_h + g.desc_h + g.extra_h + g.locked_h + g.badge_h;
            cy = y0 + std::max(U(10), (g.h - block) * 0.5f);
        }
        ink.text(19, tx, cy, label_col, r.label, g.label_w);
        float ly = cy + g.label_h;
        if (!r.desc.empty()) {
            ly += U(3);
            ink.text(15, tx, ly, faded(kDim, alpha), r.desc, g.label_w);
            ly += g.desc_h - U(3);
        }
        for (const auto& line : r.extra) {
            ly += U(3);
            ink.text(15, tx, ly, faded(tone_colour(line.tone), alpha), line.text, g.label_w);
            ly += ink.size(15, line.text, g.label_w).y;
        }
        if (locked) {
            ly += U(3);
            const bool blinking = st.blink_row == r.id && now < st.blink_until && std::fmod(now * 6.0, 1.0) < 0.5;
            ink.text(15, tx, ly, blinking ? kText : kWarn, r.locked, g.label_w);
            ly += g.locked_h - U(3);
        }
        if (!r.badge.empty()) {
            ly += U(6);
            const ImVec2 bs = ink.size(13, r.badge);
            ink.dl->AddRect(ImVec2(tx, ly), ImVec2(tx + bs.x + U(16), ly + U(20)), faded(kDim, alpha), U(10), 0, U(1));
            ink.text(13, tx + U(8), ly + (U(20) - bs.y) * 0.5f, faded(kDim, alpha), r.badge);
        }
        // The value zone / the link's chevron / the entry's right side.
        if (has_value_zone(r)) {
            float vx0, vy0, vy1;
            const float vx1 = x1 - pad;
            if (g.value_below) {
                vx0 = tx;
                vy1 = y1 - U(10);
                vy0 = vy1 - g.value_h;
            } else {
                vx0 = vx1 - g.value_w;
                vy0 = y0;
                vy1 = y1;
            }
            draw_value(st, view, ink, r, vx0, vy0, vx1, vy1, focused, int(i), tp, alpha, by0, by1);
        } else if (r.kind == RowKind::link) {
            ink.chevron(x1 - pad - U(8), (y0 + y1) * 0.5f, U(18), true, faded(kText, alpha), U(2.5f));
        } else if (r.kind == RowKind::entry) {
            if (r.progress >= 0) {
                const float bx1 = x1 - pad, bx0 = bx1 - U(110), bcy = y0 + U(22);
                ink.dl->AddRectFilled(ImVec2(bx0, bcy - U(7)), ImVec2(bx1, bcy + U(7)), rgba(0.2f, 0.22f, 0.27f), U(7));
                ink.dl->AddRectFilled(ImVec2(bx0, bcy - U(7)), ImVec2(bx0 + (bx1 - bx0) * std::clamp(r.progress, 0.0f, 1.0f), bcy + U(7)), kAccent, U(7));
                if (!r.right.empty()) {
                    const ImVec2 rs = ink.size(13, r.right);
                    ink.text(13, bx1 - rs.x, bcy + U(10), kDim, r.right);
                }
            } else if (!r.right.empty()) {
                const ImVec2 rs = ink.size(15, r.right);
                ink.text(15, x1 - pad - rs.x, y0 + U(12), tone_colour(r.right_tone), r.right);
            }
        }
        tp.hits.push_back({Hit::row, x0, std::max(y0, by0), x1, std::min(y1, by1), int(i)});
    }
    ink.dl->PopClipRect();
    layout.visible_rows = std::max(1, fully_visible - 1);
    // The scroll indicator.
    if (layout.content > layout.view + 0.5f) {
        const float bar_h = std::max(U(24), view_h * view_h / content);
        const float bar_y = by0 + (view_h - bar_h) * (scroll / std::max(layout.content - layout.view, 1.0f));
        ink.dl->AddRectFilled(ImVec2(lx1 - U(4), bar_y), ImVec2(lx1, bar_y + bar_h), rgba(1, 1, 1, 0.3f), U(2));
    }
    if (st.layout_log && layout_lines != st.last_layout_log[pi]) {
        st.last_layout_log[pi] = layout_lines;
        std::string lines;
        for (std::size_t at = 0; at < layout_lines.size();) {
            const auto end = layout_lines.find('\n', at);
            menu_log("menu-layout: " + layout_lines.substr(at, end - at) + " " + std::to_string(int(U(19))));
            at = end + 1;
        }
    }
    // ---------------- the hint bar (its room in both modes; touch mode shows
    // only the right side, dimmed: what the buttons would do)
    {
        PadFamily family = view.prompt_family();
        const Settings& settings = view.settings;
        const auto faces = [&](ConsoleGlyph g) { return console_faces(g, family, settings, st.platform); };
        std::vector<HintEntry> left;
        const Row* row = st.nav.focus >= 0 && st.nav.focus < int(st.page.rows.size()) ? &st.page.rows[std::size_t(st.nav.focus)] : nullptr;
        if (!pad_mode) {
            // (Touch: no focus to talk about.)
        } else if (modal == ModalKind::picker) left = {{faces(ConsoleGlyph::ud), "Choose"}, {faces(ConsoleGlyph::ok), "OK"}, {faces(ConsoleGlyph::back), "Cancel"}};
        else if (modal == ModalKind::confirm)
            left = {{faces(ConsoleGlyph::lr), "Choose"}, {faces(ConsoleGlyph::ok), "OK"}, {faces(ConsoleGlyph::back), "Cancel"}};
        else if (modal == ModalKind::text) left = {{faces(ConsoleGlyph::ok), "Done"}, {faces(ConsoleGlyph::back), "Cancel"}};
        else if (modal == ModalKind::capture)
            left = {{{}, std::string("Press a button for ") + pad_key_name(st.capture_key)}, {faces(ConsoleGlyph::close), "Cancel"}};
        else if (st.nav.area == Area::sidebar) left = {{faces(ConsoleGlyph::ok), "Open"}, {faces(ConsoleGlyph::back), "Close menu"}};
        else if (row) {
            switch (row->kind) {
            case RowKind::toggle:
                left = {{faces(ConsoleGlyph::ok), "Switch"}, {faces(ConsoleGlyph::lr), "Off / On"}, {faces(ConsoleGlyph::back), "Back"}};
                break;
            case RowKind::choice:
            case RowKind::stepper:
                left = {{faces(ConsoleGlyph::lr), "Change"}, {faces(ConsoleGlyph::ok), "All choices"}, {faces(ConsoleGlyph::back), "Back"}};
                break;
            case RowKind::link: left = {{faces(ConsoleGlyph::ok), "Open"}, {faces(ConsoleGlyph::back), "Back"}}; break;
            case RowKind::text:
            case RowKind::password:
            case RowKind::number: left = {{faces(ConsoleGlyph::ok), "Edit"}, {faces(ConsoleGlyph::back), "Back"}}; break;
            case RowKind::cells: {
                const std::string name = st.nav.cell >= 0 && st.nav.cell < int(row->cells.size()) ? row->cells[std::size_t(st.nav.cell)].label : "";
                left = {{faces(ConsoleGlyph::lr), "Choose"}, {faces(ConsoleGlyph::ok), name}, {faces(ConsoleGlyph::back), "Back"}};
                break;
            }
            case RowKind::binding: left = {{faces(ConsoleGlyph::lr), "Slot"}, {faces(ConsoleGlyph::ok), "Set"}, {faces(ConsoleGlyph::del), "Clear"}}; break;
            case RowKind::info:
                left = {{faces(ConsoleGlyph::ok), st.expanded.count(row->id) ? "Less" : "More"}, {faces(ConsoleGlyph::back), "Back"}};
                break;
            default:
                left = {{faces(ConsoleGlyph::ok), row->kind == RowKind::action || row->kind == RowKind::entry ? row->label : std::string("OK")},
                        {faces(ConsoleGlyph::back), "Back"}};
                break;
            }
            if (row->on_back) left.back() = {faces(ConsoleGlyph::back), "Close"};
            // (Graphics and Display on the top screen: Y held shows the game.)
            const std::string& root = st.nav.stack().empty() ? std::string() : st.nav.stack().front();
            if (place == ConsolePlace::top && (root == "graphics" || root == "display") && value_row(row->kind))
                if (auto look = faces(ConsoleGlyph::look); !look.empty()) left.push_back({std::move(look), "Look"});
        }
        std::vector<HintEntry> right;
        if (modal == ModalKind::none) {
            auto both = faces(ConsoleGlyph::prev);
            for (const auto& f : faces(ConsoleGlyph::next)) both.push_back(f);
            right.push_back({both, "Category"});
        }
        if (modal != ModalKind::capture) right.push_back({faces(ConsoleGlyph::close), "Close"});
        const float hy = a1.y - hint_h, hcy = hy + hint_h * 0.5f, glyph = U(26), gap = U(16);
        const auto entry_width = [&](const HintEntry& e) {
            return float(e.faces.size()) * (glyph + U(2)) + (e.faces.empty() ? 0.0f : U(4)) + ink.size(16, e.text).x;
        };
        float right_w = 0;
        for (const auto& e : right) right_w += entry_width(e) + gap;
        float left_w = 0;
        for (const auto& e : left) left_w += entry_width(e) + gap;
        if (left_w + right_w > width && right.size() > 1) {
            right_w -= entry_width(right.front()) + gap;
            right.erase(right.begin()); // ("Category" goes first)
        }
        st.hint_width = left_w + right_w;
        st.hint_room = width;
        ink.dl->AddLine(ImVec2(a0.x, hy - U(2)), ImVec2(a1.x, hy - U(2)), rgba(1, 1, 1, 0.08f));
        const float hint_alpha = pad_mode ? 1.0f : 0.45f;
        const auto draw_entries = [&](const std::vector<HintEntry>& entries, float x) {
            for (const auto& e : entries) {
                for (const auto& f : e.faces) {
                    draw_console_glyph(ink.dl, family, f, x, hcy - glyph * 0.5f, glyph, faded(IM_COL32_WHITE, hint_alpha));
                    x += glyph + U(2);
                }
                if (!e.faces.empty()) x += U(4);
                const ImVec2 ts = ink.size(16, e.text);
                ink.text(16, x, hcy - ts.y * 0.5f, faded(kDim, hint_alpha), e.text);
                x += ts.x + gap;
            }
        };
        draw_entries(left, a0.x + U(4));
        draw_entries(right, a1.x - right_w + gap - U(4));
    }
    // ---------------- modals
    const float W = a1.x - a0.x, H = a1.y - a0.y;
    if (st.pending_load >= 0) {
        // (The load holds the game's thread for seconds: this card shows
        // first, the load runs at the next pass.)
        ink.dl->AddRectFilled(a0, a1, rgba(0, 0, 0, 0.5f));
        const float cw = std::min(0.7f * W, U(560)), pad = U(20), wrap = cw - pad * 2;
        const std::string title = "Loading state " + std::to_string(st.pending_load + 1) + "...";
        const std::string text = "The game replays the steps since the state's stage loaded. This takes a few seconds.";
        const float ch = pad + ink.size(22, title, wrap).y + U(10) + ink.size(16, text, wrap).y + pad;
        const float cx0 = a0.x + (W - cw) * 0.5f, cy0 = a0.y + (H - ch) * 0.5f;
        ink.dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cw, cy0 + ch), kCard, U(12));
        ink.dl->AddRect(ImVec2(cx0, cy0), ImVec2(cx0 + cw, cy0 + ch), rgba(1, 1, 1, 0.12f), U(12));
        ink.text(22, cx0 + pad, cy0 + pad, kText, title, wrap);
        ink.text(16, cx0 + pad, cy0 + pad + ink.size(22, title, wrap).y + U(10), kDim, text, wrap);
        st.pending_load_drawn = true;
    }
    if (modal == ModalKind::picker || modal == ModalKind::confirm || modal == ModalKind::capture) {
        ink.dl->AddRectFilled(a0, a1, rgba(0, 0, 0, 0.5f));
        tp.hits.push_back({Hit::picker_outside, a0.x, a0.y, a1.x, a1.y});
    }
    if (modal == ModalKind::picker) {
        const int n = int(st.picker_options.size());
        const float cw = std::min(lw, U(520)), title_h = U(48);
        const float rows_h = float(n) * U(52);
        const float ch = std::min((by1 - by0) * 0.8f, title_h + rows_h + U(8));
        const float cx0 = lx0 + (lw - cw) * 0.5f, cy0 = by0 + ((by1 - by0) - ch) * 0.5f;
        ink.dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cw, cy0 + ch), kCard, U(10));
        tp.hits.push_back({Hit::card, cx0, cy0, cx0 + cw, cy0 + ch});
        const ImVec2 ts = ink.size(22, st.picker_title);
        ink.text(22, cx0 + U(16), cy0 + (title_h - ts.y) * 0.5f, kText, st.picker_title);
        const float ly0 = cy0 + title_h, ly1 = cy0 + ch - U(8), lview = ly1 - ly0;
        float& ps = tp.picker_scroll;
        if (const float d = tp.picker.drag_delta(); d != 0) ps += d * ink.scale; // (the gesture's u; the card's px)
        if (tp.picker.flinging()) ps += tp.picker.fling(dt) * ink.scale;
        // (The focus entry in view.)
        const float fy0 = float(st.nav.modal.focus) * U(52);
        if (pad_mode && !tp.picker.active()) {
            if (fy0 < ps) ps = fy0;
            if (fy0 + U(52) > ps + lview) ps = fy0 + U(52) - lview;
        }
        ps = clamp_scroll(ps, rows_h, lview);
        ink.dl->PushClipRect(ImVec2(cx0, ly0), ImVec2(cx0 + cw, ly1), true);
        for (int i = 0; i < n; ++i) {
            const float ey0 = ly0 + float(i) * U(52) - ps, ey1 = ey0 + U(52);
            if (ey1 < ly0 || ey0 > ly1) continue;
            const bool focus = pad_mode && st.nav.modal.focus == i;
            if (focus) {
                ink.dl->AddRectFilled(ImVec2(cx0 + U(6), ey0 + U(2)), ImVec2(cx0 + cw - U(6), ey1 - U(2)), kFocusFill, U(6));
                ink.dl->AddRect(ImVec2(cx0 + U(6), ey0 + U(2)), ImVec2(cx0 + cw - U(6), ey1 - U(2)), kAccent, U(6), 0, U(2));
            }
            if (tp.has_press && tp.pressed.kind == Hit::picker_entry && tp.pressed.index == i && tp.picker.pressed_shown(now))
                ink.dl->AddRectFilled(ImVec2(cx0 + U(6), ey0 + U(2)), ImVec2(cx0 + cw - U(6), ey1 - U(2)), kPressed, U(6));
            const std::string& label = st.picker_options[std::size_t(i)].label;
            const float wrap = cw - U(16 + 44);
            const ImVec2 es = ink.size(19, label, wrap);
            ink.text(19, cx0 + U(16), (ey0 + ey1 - es.y) * 0.5f, kText, label, wrap);
            if (st.picker_index[std::size_t(i)] == st.picker_current) ink.check_mark(cx0 + cw - U(26), (ey0 + ey1) * 0.5f, U(18), kValue, U(3));
            tp.hits.push_back({Hit::picker_entry, cx0, std::max(ey0, ly0), cx0 + cw, std::min(ey1, ly1), i});
        }
        ink.dl->PopClipRect();
    }
    if (modal == ModalKind::confirm && st.confirm) {
        const Confirm& c = *st.confirm;
        const float cw = std::min(0.7f * W, U(640)), pad = U(20);
        const float wrap = cw - pad * 2;
        float ch = pad + ink.size(22, c.title, wrap).y + U(10) + ink.size(16, c.text, wrap).y;
        for (const auto& note : c.notes) ch += U(6) + ink.size(16, note.text, wrap).y;
        ch += U(20) + U(52) + pad;
        const float cx0 = a0.x + (W - cw) * 0.5f, cy0 = a0.y + std::max(U(8), (H - ch) * 0.5f);
        ink.dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cw, cy0 + ch), kCard, U(12));
        ink.dl->AddRect(ImVec2(cx0, cy0), ImVec2(cx0 + cw, cy0 + ch), rgba(1, 1, 1, 0.12f), U(12));
        tp.hits.push_back({Hit::card, cx0, cy0, cx0 + cw, cy0 + ch});
        float ty = cy0 + pad;
        ink.text(22, cx0 + pad, ty, kText, c.title, wrap);
        ty += ink.size(22, c.title, wrap).y + U(10);
        ink.text(16, cx0 + pad, ty, kDim, c.text, wrap);
        ty += ink.size(16, c.text, wrap).y;
        for (const auto& note : c.notes) {
            ty += U(6);
            ink.text(16, cx0 + pad, ty, tone_colour(note.tone), note.text, wrap);
            ty += ink.size(16, note.text, wrap).y;
        }
        const int n = int(c.buttons.size());
        const float by = cy0 + ch - pad - U(52), bgap = U(12);
        const float bw = (cw - pad * 2 - bgap * float(n - 1)) / float(std::max(n, 1));
        for (int i = 0; i < n; ++i) {
            const float bx0 = cx0 + pad + float(i) * (bw + bgap);
            const bool focus = pad_mode && st.nav.modal.focus == i;
            button_rect(ink, bx0, by, bx0 + bw, by + U(52), c.buttons[std::size_t(i)], i == c.danger ? rgba(0.55f, 0.16f, 0.14f) : kButton, focus);
            tp.hits.push_back({Hit::confirm_button, bx0, by, bx0 + bw, by + U(52), i});
        }
    }
    if (modal == ModalKind::capture) {
        const float cw = std::min(0.8f * W, U(560)), pad = U(20), wrap = cw - pad * 2;
        const std::string title = std::string("Press a button or pull a trigger for ") + pad_key_name(st.capture_key) + " (input " +
                                  std::to_string(st.capture_slot + 1) + " of 3)";
        const float title_h = ink.size(22, title, wrap).y;
        const float ch = pad + title_h + U(12) + U(40) + U(12) + U(10) + U(16) + U(52) + pad;
        const float cx0 = a0.x + (W - cw) * 0.5f, cy0 = a0.y + (H - ch) * 0.5f;
        ink.dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cw, cy0 + ch), kCard, U(12));
        tp.hits.push_back({Hit::card, cx0, cy0, cx0 + cw, cy0 + ch});
        float ty = cy0 + pad;
        ink.text(22, cx0 + pad, ty, kText, title, wrap);
        ty += title_h + U(12);
        PadFamily family = view.prompt_family();
        if (family == PadFamily::keyboard) family = view.pads.pads().empty() ? PadFamily::xbox_one : view.pads.pads().front().family;
        const PadInput now_bound = view.settings.pad[std::size_t(st.capture_key)][st.capture_slot];
        const std::string label = pad_input_label(family, view.settings, now_bound);
        PromptFace face;
        face.face = face_of(family, view.settings.nintendo_layout, now_bound);
        if (face.face != Face::none) draw_console_glyph(ink.dl, family, face, cx0 + pad, ty, U(40));
        ink.text(19, cx0 + pad + U(50), ty + U(8), label.empty() ? kDim : kValue, label.empty() ? std::string("Not set now") : "Now: " + label);
        ty += U(40) + U(12);
        const float left = st.capture.waiting ? 1.0f : st.capture.left(st.now);
        ink.dl->AddRectFilled(ImVec2(cx0 + pad, ty), ImVec2(cx0 + cw - pad, ty + U(10)), rgba(0.2f, 0.22f, 0.27f), U(5));
        ink.dl->AddRectFilled(ImVec2(cx0 + pad, ty), ImVec2(cx0 + pad + (cw - pad * 2) * left, ty + U(10)), kAccent, U(5));
        ty += U(10) + U(16);
        const float bw = (cw - pad * 2 - U(12)) * 0.5f;
        button_rect(ink, cx0 + pad, ty, cx0 + pad + bw, ty + U(52), "Clear", kButton, false);
        tp.hits.push_back({Hit::capture_clear, cx0 + pad, ty, cx0 + pad + bw, ty + U(52)});
        button_rect(ink, cx0 + pad + bw + U(12), ty, cx0 + cw - pad, ty + U(52), "Cancel", kButton, false);
        tp.hits.push_back({Hit::capture_cancel, cx0 + pad + bw + U(12), ty, cx0 + cw - pad, ty + U(52)});
    }
    if (window) ImGui::End();
    else if (place == ConsolePlace::top) ImGui::End();
    // ---------------- the text sheet (its own window: the system keyboard)
    if (modal == ModalKind::text) {
        const float cw = std::min(0.9f * lw, U(640));
        const float field_h = U(56);
        ImVec2 pos;
        if (place == ConsolePlace::top) {
            // (Right under the header; the field's bottom at most at 35 % of the
            // screen's height, above Android's keyboard.)
            const float top = std::min(hy1 + U(4), io.DisplaySize.y * 0.35f - U(48) - field_h - U(24));
            pos = ImVec2((io.DisplaySize.x - cw) * 0.5f, std::max(U(4), top));
        } else {
            pos = ImVec2(a0.x + (W - cw) * 0.5f, a0.y + (H - U(220)) * 0.5f);
        }
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(cw, 0));
        ImGui::SetNextWindowBgAlpha(0.98f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(16), U(14)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, U(12));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(U(12), (field_h - U(22)) * 0.5f));
        if (ImGui::Begin("##console_text", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(ink.font, 22.0f);
            ImGui::TextUnformatted(st.text_title.c_str());
            ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
            if (st.text_kind == RowKind::password) flags |= ImGuiInputTextFlags_Password;
            if (st.text_kind == RowKind::number) flags |= ImGuiInputTextFlags_CharsDecimal;
            if (st.text_frames == 0) ImGui::SetKeyboardFocusHere(0);
            // (A name or a path: host_view.cpp's keyboard without suggestions.)
            set_plain_text_field(st.text_plain ? ImGui::GetID("##console_field") : 0u);
            ImGui::SetNextItemWidth(-1);
            const bool entered = ImGui::InputTextWithHint("##console_field", st.text_hint.c_str(), st.text_buffer.data(), st.text_max + 1, flags);
            if (ImGui::IsItemActive() && !st.text_logged_active) {
                st.text_logged_active = true;
                menu_log("menu: edit " + st.text_row + " active (frame " + std::to_string(st.text_frames + 1) + ")");
            }
            ImGui::PopFont();
            if (!st.text_error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kWarn));
                ImGui::TextWrapped("%s", st.text_error.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            const float bw = (ImGui::GetContentRegionAvail().x - U(12)) * 0.5f;
            ImGui::PushFont(ink.font, 19.0f);
            const bool cancel = ImGui::Button("Cancel##console_text", ImVec2(bw, U(48)));
            ImGui::SameLine(0, U(12));
            const bool done = ImGui::Button("Done##console_text", ImVec2(bw, U(48)));
            ImGui::PopFont();
            const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            ++st.text_frames;
            ImGui::End();
            ImGui::PopStyleVar(3);
            if (entered || done) commit_text(st);
            else if (cancel || escape) close_text(st, "cancel");
        } else {
            ImGui::End();
            ImGui::PopStyleVar(3);
        }
    }
    // ---------------- touch
    {
        const ImVec2 m = io.MousePos;
        const auto hit_at = [&](float x, float y) -> const Hit* {
            // (The last pushed is on top: modals last; a row's arrows and
            // cells before the row itself; a column's background last of all.)
            const Hit* row_hit = nullptr;
            const Hit* area_hit = nullptr;
            for (auto it = tp.hits.rbegin(); it != tp.hits.rend(); ++it)
                if (it->contains(x, y)) {
                    if (st.nav.modal.kind != ModalKind::none) {
                        const bool modal_hit = it->kind == Hit::picker_entry || it->kind == Hit::picker_outside || it->kind == Hit::confirm_button ||
                                               it->kind == Hit::capture_cancel || it->kind == Hit::capture_clear || it->kind == Hit::card;
                        if (!modal_hit) continue;
                    }
                    if (it->kind == Hit::row) {
                        if (!row_hit) row_hit = &*it;
                        continue;
                    }
                    if (it->kind == Hit::list_area || it->kind == Hit::sidebar_area) {
                        if (!area_hit) area_hit = &*it;
                        continue;
                    }
                    return &*it;
                }
            return row_hit ? row_hit : area_hit;
        };
        const bool text_window_hovered = modal == ModalKind::text && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        if (io.MouseClicked[0] && st.trace) {
            const Hit* h = hit_at(m.x, m.y);
            char line[128];
            std::snprintf(line, sizeof line, "menu: touch down %.0f %.0f (%s) on %d", double(m.x), double(m.y), hovered_here ? "this window" : "another window",
                          h ? int(h->kind) : -1);
            menu_log(line);
        }
        if (io.MouseClicked[0] && hovered_here && !text_window_hovered && modal != ModalKind::text && st.pending_load < 0) {
            if (const Hit* h = hit_at(m.x, m.y)) {
                st.nav.mode = InputMode::touch;
                tp.pressed = *h;
                tp.has_press = true;
                tp.active = h->kind == Hit::category || h->kind == Hit::sidebar_area             ? &tp.sidebar
                            : h->kind == Hit::picker_entry || (h->kind == Hit::card && modal == ModalKind::picker) ? &tp.picker
                                                                                                  : &tp.list;
                // (A finger on a fling only stops it.)
                tp.active->down(m.x / ink.scale, m.y / ink.scale, now);
                tp.repeat_next = now + 0.4;
            }
        }
        if (tp.has_press && tp.active && io.MouseDown[0]) {
            tp.active->move(m.x / ink.scale, m.y / ink.scale, now);
            if (tp.active->dragging()) {
                const float d = tp.active->drag_delta();
                if (tp.active == &tp.list) scroll = clamp_scroll(scroll + d, layout.content, layout.view);
                else if (tp.active == &tp.sidebar) tp.sidebar_scroll += d * ink.scale;
                else tp.picker_scroll += d * ink.scale;
            }
            Row* row = tp.pressed.index >= 0 && tp.pressed.index < int(st.page.rows.size()) ? &st.page.rows[std::size_t(tp.pressed.index)] : nullptr;
            // A held arrow repeats (8 steps a second after 0.4 s).
            if (row && (tp.pressed.kind == Hit::arrow_left || tp.pressed.kind == Hit::arrow_right) && !tp.active->dragging() && now >= tp.repeat_next) {
                tp.active->take_long();
                tp.repeat_next = now + 0.125;
                change(st, *row, tp.pressed.kind == Hit::arrow_left ? -1 : 1);
            }
            // A long press on a binding's slot clears it.
            if (row && row->kind == RowKind::binding && tp.pressed.kind == Hit::cell_button && tp.active->long_press(now, 0.6)) {
                if (row->locked.empty()) clear_binding(st, view, row->key, std::size_t(std::max(tp.pressed.cell, 0)));
            }
        }
        if (tp.has_press && tp.active && (io.MouseReleased[0] || !io.MouseDown[0])) {
            const Hit pressed = tp.pressed;
            const bool tap = tp.active->up(m.x / ink.scale, m.y / ink.scale, now);
            tp.has_press = false;
            // (A tap is a release on the target as it was at the press: the
            // place it showed under the finger then.)
            const bool same = pressed.contains(m.x, m.y);
            if (st.trace) {
                char line[128];
                std::snprintf(line, sizeof line, "menu: touch up %.0f %.0f: %s", double(m.x), double(m.y),
                              tap && same ? "a tap" : !tap ? "no tap (a drag, a long press or a fling stopped)" : "off its target");
                menu_log(line);
            }
            if (tap && same) {
                Row* row = pressed.index >= 0 && pressed.index < int(st.page.rows.size()) ? &st.page.rows[std::size_t(pressed.index)] : nullptr;
                switch (pressed.kind) {
                case Hit::row:
                    if (row && st.nav.modal.kind == ModalKind::none) {
                        st.nav.area = Area::list;
                        st.nav.set_focus(st.nav_rows, pressed.index);
                        if (row->kind == RowKind::cells || row->kind == RowKind::binding) break; // (their cells take the taps)
                        activate(st, view, *row);
                    }
                    break;
                case Hit::arrow_left:
                case Hit::arrow_right:
                    if (row) {
                        st.nav.area = Area::list;
                        st.nav.set_focus(st.nav_rows, pressed.index);
                        change(st, *row, pressed.kind == Hit::arrow_left ? -1 : 1);
                    }
                    break;
                case Hit::cell_button:
                    if (row) {
                        st.nav.area = Area::list;
                        st.nav.set_focus(st.nav_rows, pressed.index);
                        st.nav.cell = pressed.cell;
                        st.nav.cells[row->id] = pressed.cell;
                        activate(st, view, *row);
                    }
                    break;
                case Hit::category:
                    if (pressed.index != st.nav.category()) {
                        st.nav.select_category(pressed.index);
                        log_page(st);
                    }
                    st.nav.area = Area::list;
                    break;
                case Hit::back:
                    if (st.nav.pop_page()) log_page(st);
                    break;
                case Hit::close: request_close(st, "Close"); break;
                case Hit::picker_entry:
                    st.nav.modal.focus = pressed.index;
                    modal_answer(st, view, true);
                    break;
                case Hit::picker_outside:
                    if (st.nav.modal.kind == ModalKind::picker) modal_answer(st, view, false);
                    break;
                case Hit::confirm_button:
                    st.nav.modal.focus = pressed.index;
                    st.nav.modal.opened = -1; // (a tap has no 250 ms guard: it is released)
                    modal_answer(st, view, true);
                    break;
                case Hit::capture_cancel: end_capture(st, view, "touch"); break;
                case Hit::capture_clear: {
                    const PadKey key = st.capture_key;
                    const std::size_t slot = st.capture_slot;
                    end_capture(st, view, nullptr);
                    clear_binding(st, view, key, slot);
                    break;
                }
                case Hit::text_done:
                case Hit::text_cancel:
                case Hit::card:
                case Hit::list_area:
                case Hit::sidebar_area: break; // (a column's background: drags only)
                }
            }
            tp.active = nullptr;
        }
        if (tp.sidebar.flinging()) tp.sidebar_scroll += tp.sidebar.fling(dt) * ink.scale;
    }
    // DW4_MENU_LAYOUT_LOG: the touch targets too ("menu-hit: kind x0 y0 x1 y1 scale", px), and the hint bar's width.
    if (st.layout_log) {
        std::string hits;
        char line[128];
        for (const Hit& h : tp.hits) {
            std::snprintf(line, sizeof line, "menu-hit: %d %.0f %.0f %.0f %.0f %.3f", int(h.kind), double(h.x0), double(h.y0), double(h.x1),
                          double(h.y1), double(ink.scale));
            hits += line;
            hits += '\n';
        }
        char bar[96];
        std::snprintf(bar, sizeof bar, "menu-hint: %.0f of %.0f %.3f", double(st.hint_width), double(st.hint_room), double(ink.scale));
        hits += bar;
        hits += '\n';
        if (hits != st.last_hits[pi]) {
            st.last_hits[pi] = hits;
            for (std::size_t at = 0; at < hits.size();) {
                const auto end = hits.find('\n', at);
                menu_log(hits.substr(at, end - at));
                at = end + 1;
            }
        }
    }
}

} // namespace dw4::game
