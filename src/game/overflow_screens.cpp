#include "overflow_screens.hpp"

#include "overflow_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dw4::game::overflow {

namespace {
std::string clock(double seconds) {
    const int whole = std::max(0, int(seconds));
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", whole / 60, whole % 60);
    return text;
}

void title_window(SimUi& ui, float x, float y, float w, float h, std::string_view title, UiColor color = ui_colors::yellow) {
    ui.window(x, y, w, h);
    ui.text(x + 18, y + 12, title, color, 22.0f);
    ui.rect(x + 14, y + 40, w - 28, 1.5f, UiColor{0.6f, 0.75f, 1, 0.6f});
}

// HUD text over the arena's bright effects: a dark copy one unit down and
// right under it (one more item a line).
void shadow_text(SimUi& ui, float x, float y, std::string_view line, UiColor color, float size, int align = 0, float width = 0) {
    ui.text(x + 1.5f, y + 1.5f, line, UiColor{0, 0, 0, 0.85f}, size, align, width);
    ui.text(x, y, line, color, size, align, width);
}

void draw_hud(SimUi& ui, const RunScreenData& d, std::uint64_t frame) {
    // The EXP bar: a thin bar in the HUD's purple under the top edge.
    ui.rect(0, 0, 640, 5, UiColor{0, 0, 0, 0.55f});
    ui.rect(0, 0, 640 * std::clamp(d.exp_fraction, 0.0f, 1.0f), 5, ui_colors::purple);
    // The run timer top centre; in the finale the quest timer counts down
    // top left, where the original's quest timer sits (i_timenm0 at
    // V<-290,217>, 4.4): the arena's boss bar (i_bosshp02, LordKnightmon's)
    // runs across the top centre from about x 90.
    float level_y = 10;
    if (d.finale || d.time_up) {
        // The quest timer's look (i_timenm0): a dark plate with the numbers.
        const float px = 6, py = 8, pw = 80, ph = 36;
        ui.gradient(px, py, pw, ph, UiColor{0.25f, 0.05f, 0.05f, 0.85f}, UiColor{0.1f, 0.02f, 0.02f, 0.85f});
        ui.rect(px, py, pw, 2, ui_colors::orange);
        ui.rect(px, py + ph - 2, pw, 2, ui_colors::orange);
        const bool hurry = d.finale_left < 60.0;
        const bool blink = hurry && (frame / 15) % 2 == 0;
        ui.text(px, py + 6, d.time_up ? std::string("0:00") : quest_timer_text(d.finale_left), blink ? ui_colors::red : ui_colors::white, 24.0f, 2, pw);
        if (d.time_up) ui.text(0, 200, text(Text::time_up), ui_colors::red, 48.0f, 2, 640);
        level_y = py + ph + 4; // (the level under the plate)
    } else {
        shadow_text(ui, 270, 10, clock(d.seconds), ui_colors::white, 24.0f, 2, 100);
    }
    // Defeated and Bits top right, the level top left.
    shadow_text(ui, 440, 10, format(Text::hud_defeated, d.defeats), ui_colors::white, 16.0f, 1, 190);
    shadow_text(ui, 440, 30, format(Text::hud_bits, d.bits), ui_colors::cyan, 16.0f, 1, 190);
    shadow_text(ui, 10, level_y, format(Text::hud_level, d.level), ui_colors::yellow, 18.0f);
    // The build row above the original HUD (i_stpnl at V<-260,-160>): the
    // weapon in its value colour with its bar, the techniques, the gear, on
    // a dark band (the grey of a technique not learned stays readable over
    // the arena's glow).
    const float y = 372;
    if (!d.weapon.empty() || !d.techniques.empty() || !d.gear.empty()) {
        float end = 290;
        for (const auto& technique : d.techniques) end += ui.text_width(technique, 15.0f) + 10.0f;
        if (!d.gear.empty()) end += 6 + ui.text_width(d.gear, 15.0f);
        ui.rect(144, y - 3, end - 144 + 6, 29, UiColor{0, 0, 0, 0.5f});
    }
    if (!d.weapon.empty()) {
        ui.text(150, y, d.weapon, grade_color(d.weapon), 15.0f);
        ui.bar(150, y + 18, 120, 5, d.weapon_fraction, grade_color(d.weapon));
    }
    float x = 290;
    for (std::size_t i = 0; i < d.techniques.size(); ++i) {
        const bool on = i < d.learned.size() && d.learned[i];
        ui.text(x, y, d.techniques[i], on ? ui_colors::white : UiColor{0.7f, 0.7f, 0.75f, 1}, 15.0f);
        x += ui.text_width(d.techniques[i], 15.0f) + 10.0f;
    }
    if (!d.gear.empty()) ui.text(x + 6, y, d.gear, ui_colors::green, 15.0f);
    // A Guardian's bar where the arena has none.
    if (d.guardian) {
        shadow_text(ui, 170, 58, *d.guardian, ui_colors::orange, 16.0f, 2, 300);
        ui.bar(170, 78, 300, 8, float(d.guardian_hp) / float(std::max(1, d.guardian_max)), ui_colors::red);
    }
    // (TIME UP once: the banner, not lane E's notice line above it as well.)
    if (!d.notice.empty() && !(d.time_up && d.notice == text(Text::time_up))) shadow_text(ui, 0, 100, d.notice, ui_colors::yellow, 18.0f, 2, 640);
    // The clear banner (4.4, i_bgclear's place) for 4 s after LordKnightmon's fall.
    if (d.cleared && d.clear_frames < 240) ui.text(0, 200, text(Text::results_complete), ui_colors::yellow, 40.0f, 2, 640);
    if (d.seconds >= 20 * 60 && !d.finale && !d.time_up) shadow_text(ui, 0, 38, text(Text::hud_hold_out), ui_colors::orange, 15.0f, 2, 640);
}

void draw_level_up(SimUi& ui, const RunScreenData& d) {
    // The quick panel list in the middle (9.4).
    const float w = 560, x = 40, row = 30;
    const std::string about = d.cursor >= 0 && d.cursor < int(d.offers.size()) ? ui.wrap(d.offers[std::size_t(d.cursor)].about, 15.0f, w - 120) : "";
    const float about_lines = float(1 + std::count(about.begin(), about.end(), '|'));
    const float h = 60 + row * float(d.offers.size()) + 40 + 17.0f * about_lines;
    const float y = 240 - h * 0.5f;
    ui.window(x, y, w, h);
    ui.text(x + 18, y + 12, text(Text::level_up), ui_colors::yellow, 24.0f);
    ui.text(x + 180, y + 15, format(Text::hud_level, d.level), ui_colors::white, 20.0f);
    for (std::size_t i = 0; i < d.offers.size(); ++i) {
        const float ly = y + 52 + row * float(i);
        const bool on = int(i) == d.cursor;
        if (on) ui.cursor_bar(x + 10, ly - 2, w - 20, row - 2);
        ui.text(x + 24, ly + 3, d.offers[i].text, on ? ui_colors::white : ui_colors::grey, 18.0f);
        ui.text(x + 290, ly + 5, d.offers[i].detail, on ? ui_colors::cyan : UiColor{0.45f, 0.55f, 0.65f, 1}, 15.0f);
    }
    const float fy = y + 52 + row * float(d.offers.size()) + 8;
    ui.rect(x + 14, fy, w - 28, 1.5f, UiColor{0.6f, 0.75f, 1, 0.6f});
    ui.text(x + 18, fy + 10, text(Text::level_up_take), ui_colors::yellow, 16.0f);
    if (!about.empty()) ui.text(x + 100, fy + 10, about, ui_colors::white, 15.0f);
}

void draw_list(SimUi& ui, std::string_view title, const std::vector<std::string>& lines, int cursor, std::string_view help) {
    const float w = 300, x = 170, row = 30;
    const float h = 50 + row * float(lines.size()) + 40;
    const float y = 220 - h * 0.5f;
    title_window(ui, x, y, w, h, title);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float ly = y + 50 + row * float(i);
        const bool on = int(i) == cursor;
        if (on) ui.cursor_bar(x + 10, ly - 2, w - 20, row - 2);
        ui.text(x + 30, ly + 3, lines[i], on ? ui_colors::white : ui_colors::grey, 20.0f);
    }
    ui.text(x + 18, y + h - 28, help, ui_colors::grey, 14.0f);
}

void draw_lines(SimUi& ui, std::string_view title, const std::vector<std::string>& all, std::string_view help, std::size_t shown,
                UiColor title_color) {
    // (Past 16 lines two columns, past 32 the meta lines stay: 9.6.)
    const auto lines = fit_result_lines(all, 32);
    const std::size_t count = lines.size();
    const int columns = result_columns(count);
    const float w = 560, x = 40, row = columns == 1 ? 21.0f : 19.0f, size = columns == 1 ? 16.0f : 14.0f;
    const float column_w = (w - 44) / float(columns);
    // A line wider than its column wraps (its further rows indented like a
    // group's entries); only a word wider than the column draws smaller.
    std::vector<std::vector<std::string>> wrapped(count);
    std::vector<int> heights(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& line = lines[i];
        if (ui.text_width(line, size) <= column_w - 6) {
            wrapped[i] = {line};
        } else {
            const std::size_t indent = std::min(line.find_first_not_of(' '), line.size());
            const std::string pad = std::string(indent, ' ') + "   ";
            const std::string folded = ui.wrap(line, size, column_w - 6 - ui.text_width(pad, size));
            for (std::size_t begin = 0; begin <= folded.size();) {
                const std::size_t end = std::min(folded.find('|', begin), folded.size());
                std::string part = folded.substr(begin, end - begin);
                if (!wrapped[i].empty()) part = pad + part.substr(std::min(part.find_first_not_of(' '), part.size()));
                wrapped[i].push_back(std::move(part));
                begin = end + 1;
            }
        }
        heights[i] = int(wrapped[i].size());
    }
    // Two columns break between groups (a heading and its indented entries
    // stay in one column).
    const std::size_t split = columns == 1 ? count : column_break(lines, heights);
    int rows_left = 0, rows_right = 0;
    for (std::size_t i = 0; i < count; ++i) (i < split ? rows_left : rows_right) += heights[i];
    const int rows = std::max({1, rows_left, rows_right});
    const float h = 50 + row * float(rows) + 40;
    const float y = std::max(16.0f, 236 - h * 0.5f);
    title_window(ui, x, y, w, h, title, title_color);
    int r = 0;
    for (std::size_t i = 0; i < count && i < shown; ++i) {
        if (i == split) r = 0;
        const std::size_t c = i < split ? 0 : 1;
        const bool news = lines[i].find("NEW!") != std::string::npos;
        for (const auto& part : wrapped[i]) {
            const float width = ui.text_width(part, size);
            const float fit = width > column_w - 6 ? size * (column_w - 6) / width : size;
            ui.text(x + 22 + column_w * float(c), y + 50 + row * float(r), part, news ? ui_colors::yellow : ui_colors::white, fit);
            ++r;
        }
    }
    if (shown >= count && !help.empty()) ui.text(x + 18, y + h - 28, help, ui_colors::grey, 15.0f);
}

// A gear part's glyph with its grade ("Brave Core β +30" -> "∋β"); "" for an empty part ("-").
std::string gear_glyph(const char* glyph, std::string_view name) {
    if (name.empty() || name == "-" || name == "none") return {};
    std::string out = glyph;
    for (const char* grade : {"α", "β", "γ", "δ", "ε"})
        if (name.find(grade) != std::string_view::npos) return out + grade;
    return out;
}
} // namespace

void build_row(const std::vector<std::string>& lines, RunScreenData& d) {
    d.weapon.clear();
    d.weapon_fraction = 0;
    d.techniques.clear();
    d.learned.clear();
    d.gear.clear();
    const auto add_gear = [&](const char* glyph, std::string_view name) {
        if (const auto part = gear_glyph(glyph, name); !part.empty()) d.gear += (d.gear.empty() ? "" : " ") + part;
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (i == 0) {
            if (const auto with = line.find(" with "); with != std::string::npos) d.weapon = line.substr(with + 6);
            continue;
        }
        // The gear (lane E's "Core: X   Board: Y" and "Chips: A / B   ¶ Data N"
        // lines): the ∋ / ∨ / ∧ glyphs with each part's grade (9.3).
        if (line.rfind("Core: ", 0) == 0) {
            const std::string_view rest = std::string_view(line).substr(6);
            const auto board = rest.find("   Board: ");
            add_gear("∋", rest.substr(0, board));
            if (board != std::string_view::npos) add_gear("∨", rest.substr(board + 10));
            continue;
        }
        if (line.rfind("Chips: ", 0) == 0) {
            // ("Chips: A, B" or "none"; the older "A / B" with "-" reads too.)
            std::string_view rest = std::string_view(line).substr(7);
            if (const auto data = rest.find("   ¶"); data != std::string_view::npos) rest = rest.substr(0, data);
            while (!rest.empty()) {
                const auto comma = std::min(rest.find(", "), rest.find(" / "));
                add_gear("∧", rest.substr(0, comma));
                if (comma == std::string_view::npos) break;
                rest = rest.substr(comma + (rest.compare(comma, 3, " / ") == 0 ? 3 : 2));
            }
            continue;
        }
        if (line.rfind("  ", 0) != 0) continue;
        if (const auto skill = line.find(" skill "); skill != std::string::npos && i == 1) {
            int value = 0, cap = 0;
            if (std::sscanf(line.c_str() + skill + 7, "%d / cap %d", &value, &cap) == 2 && cap > 0) d.weapon_fraction = float(value) / float(cap);
            continue;
        }
        const auto colon = line.find(": ");
        if (colon == std::string::npos || colon < 3) continue;
        const std::string name = line.substr(2, colon - 2);
        const std::string rank = line.substr(colon + 2, line.find(" (", colon) - colon - 2);
        // (The element glyphs; Pwr Up / Pwr Dwn as the game's arrows, so the
        // two P's and the rank letter P do not stand side by side.)
        std::string glyph = name == "Fire"                      ? "⊂"
                            : name == "Ice" || name == "Freeze" ? "⊃"
                            : name == "Bolt"                    ? "∪"
                            : name == "Dark"                    ? "∩"
                            : name == "Pwr Up"                  ? "↑"
                            : name == "Pwr Dwn"                 ? "↓"
                                                                : name.substr(0, 1);
        const bool learned = rank != "not learned";
        if (learned) glyph += rank == "∞" ? std::string("∞") : rank.substr(0, 1);
        d.techniques.push_back(glyph);
        d.learned.push_back(learned);
    }
}

float level_progress(int exp, int floor, int next) {
    if (next <= floor) return 0.0f;
    return std::clamp(float(double(exp - floor) / double(next - floor)), 0.0f, 1.0f);
}

std::vector<std::string> fit_result_lines(const std::vector<std::string>& lines, std::size_t most) {
    if (lines.size() <= most) return lines;
    const auto meta = [](const std::string& line) {
        return line.find("NEW!") != std::string::npos || line.rfind("Sim Bits", 0) == 0 || line.rfind("Bits ", 0) == 0;
    };
    const std::size_t kept = std::size_t(std::count_if(lines.begin(), lines.end(), meta));
    const std::size_t room = most > kept ? most - kept : 0;
    std::vector<std::string> out;
    for (const auto& line : lines)
        if (!meta(line) && out.size() < room) out.push_back(line);
    for (const auto& line : lines)
        if (meta(line) && out.size() < most) out.push_back(line);
    return out;
}

int result_columns(std::size_t lines) { return lines <= 16 ? 1 : 2; }

std::size_t column_break(const std::vector<std::string>& lines, const std::vector<int>& heights) {
    const std::size_t count = lines.size();
    if (count < 2) return count;
    auto height = [&](std::size_t i) { return i < heights.size() ? heights[i] : 1; };
    int total = 0;
    for (std::size_t i = 0; i < count; ++i) total += height(i);
    // A group starts at a line that is not indented; the break goes before
    // the group start that leaves the taller column the shortest.
    std::size_t best = count;
    int best_rows = total + 1, left = 0;
    for (std::size_t i = 1; i < count; ++i) {
        left += height(i - 1);
        if (!lines[i].starts_with(' ')) {
            const int rows = std::max(left, total - left);
            if (rows < best_rows) {
                best_rows = rows;
                best = i;
            }
        }
    }
    if (best == count) best = (count + 1) / 2; // (no group start at all: halves)
    return best;
}

std::uint64_t results_wait(std::size_t lines) { return std::max<std::uint64_t>(60, lines > 0 ? std::uint64_t(lines - 1) * 8 : 0); }

std::string quest_timer_text(double seconds_left) {
    const int whole = std::max(0, int(std::ceil(seconds_left)));
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d", whole / 60, whole % 60);
    return text;
}

UiColor grade_color(std::string_view name) {
    // DW4's value colours by the grade glyph (9.1).
    if (name.find("ε") != std::string_view::npos) return ui_colors::magenta;
    if (name.find("δ") != std::string_view::npos) return ui_colors::orange;
    if (name.find("γ") != std::string_view::npos) return ui_colors::yellow;
    if (name.find("β") != std::string_view::npos) return ui_colors::green;
    if (name.find("α") != std::string_view::npos) return ui_colors::cyan;
    return ui_colors::white;
}

float run_screens_alpha(const RunScreenData& d) {
    if (d.screen == RunScreen::results || !d.defeated || d.defeat_frames <= 120) return 1.0f;
    return std::clamp(1.0f - float(d.defeat_frames - 120) / 45.0f, 0.0f, 1.0f);
}

void draw_run_screens(SimUi& ui, const RunScreenData& d, std::uint64_t frame) {
    if (d.screen != RunScreen::results) draw_hud(ui, d, frame);
    switch (d.screen) {
    case RunScreen::none: break;
    case RunScreen::level_up: draw_level_up(ui, d); break;
    case RunScreen::pause:
        draw_list(ui, text(Text::pause), {std::string(text(Text::pause_resume)), std::string(text(Text::pause_build)), std::string(text(Text::pause_retire))},
                  d.cursor, text(Text::pause_help));
        break;
    case RunScreen::build: draw_lines(ui, text(Text::build_title), d.build_lines, text(Text::build_help), d.build_lines.size(), ui_colors::yellow); break;
    case RunScreen::retire_ask:
        draw_list(ui, text(Text::retire_ask), {std::string(text(Text::yes)), std::string(text(Text::no))}, d.cursor, text(Text::retire_help));
        break;
    case RunScreen::results: {
        // The lines appear one after another (a tick each, 9.6); A returns
        // (and its line shows) once every line is there, at least after 1 s.
        const std::size_t shown = std::size_t(d.screen_frames / 8) + 1;
        const bool ready = d.screen_frames >= results_wait(fit_result_lines(d.result_lines, 32).size());
        draw_lines(ui, d.cleared ? text(Text::results_complete) : text(Text::results_ended), d.result_lines,
                   ready ? text(Text::results_return) : std::string_view(), shown, d.cleared ? ui_colors::yellow : ui_colors::white);
        break;
    }
    }
}

} // namespace dw4::game::overflow
