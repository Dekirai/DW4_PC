// Data Overflow's screens (round O1b lane D; docs/SURVIVAL.md 3, 9, 14.1):
// T9 (every string of overflow_text.inc against the game font's characters,
// with the game data; without it against the glyph list of 9.1; since the
// O1b integration also lane F's texts of overflow_profile.cpp and the
// results' meta lines, and lane E's notices), the NPC's
// rules (3.2), the talk box's reach (3.3), the dialog's state machine (3.4,
// 9.2), the screens' 2D items and the NPC actor (n_gekoa from stag0302).
#include "overflow_lobby.hpp"
#include "overflow_profile.hpp"
#include "overflow_screens.hpp"
#include "overflow_text.hpp"
#include "presentation_actor.hpp"
#include "sim_ui.hpp"

#include "dw4/engine/port_events.hpp"
#include "dw4/pad.hpp"
#include "dw4/resources.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>

namespace ov = dw4::game::overflow;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                                                                              \
    do {                                                                                                                                         \
        ++checks;                                                                                                                                \
        if (!(cond)) {                                                                                                                           \
            ++failures;                                                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                          \
        }                                                                                                                                        \
    } while (0)

namespace {

// 9.1: ASCII and the game's own glyphs.
const std::u32string kGlyphs = U"∈∋∨∧¶⊂⊃∪∩⊆∞↑↓αβγδε";

void test_text_glyphs() {
    std::set<std::string_view> ids;
    for (const auto& entry : ov::all_texts()) {
        CHECK(!entry.text.empty());
        CHECK(ids.insert(entry.id).second);
        for (const char32_t c : ov::code_points(entry.text)) {
            const bool ok = (c >= 0x20 && c < 0x7F) || kGlyphs.find(c) != std::u32string::npos;
            if (!ok) std::printf("  %.*s: U+%04X is not one of the game's glyphs\n", int(entry.id.size()), entry.id.data(), unsigned(c));
            CHECK(ok);
        }
        // A talk page has at most three lines of about 34 characters (3.4).
        if (entry.id.starts_with("intro_") || entry.id.starts_with("help_") || entry.id.starts_with("back_") || entry.id == "crowd") {
            int lines = 1;
            std::size_t length = 0, longest = 0;
            for (const char32_t c : ov::code_points(entry.text)) {
                if (c == U'|') {
                    ++lines;
                    length = 0;
                } else {
                    longest = std::max(longest, ++length);
                }
            }
            CHECK(lines <= 3);
            if (longest > 36) std::printf("  %.*s: a line of %zu characters\n", int(entry.id.size()), entry.id.data(), longest);
            CHECK(longest <= 36);
        }
    }
    // The exact texts of 3.4.
    CHECK(ov::text(ov::Text::intro_6) == "Oh, and if you meet any Gekomon|in there... those are copies. I|am the original. Probably.");
    CHECK(ov::text(ov::Text::ask) == "What will you do?");
    CHECK(ov::text(ov::Text::goodbye) == "Come back any time!");
    CHECK(ov::to_game_text("a|b") == u"a\nb");
    CHECK(ov::to_game_text("∋") == u"∋");
}

// Lane F's texts that reach a screen (overflow_profile.cpp, the run
// driver's results lines; the O1b integration put them under T9): the 23
// Data Records (title, condition, what they open, the results' "NEW!"
// line), the unlock texts of the 16 forms, the Plug Lab's names and
// purchase answers, the save problems, the results' Bits and Sim Bits
// lines; and lane E's director notices with fixed words (4.4, 6.5).
std::vector<std::pair<std::string, std::string>> meta_texts() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& record : ov::record_table()) {
        const std::string key = record.key;
        out.push_back({key + ".title", record.title});
        out.push_back({key + ".condition", record.condition});
        out.push_back({key + ".opens", record.opens});
        out.push_back({key + ".results", std::string("NEW! ") + record.title + "   opens " + record.opens});
    }
    for (const char* model : ov::overflow_forms())
        if (const auto unlock = ov::form_unlock_text(model); !unlock.empty()) out.push_back({std::string(model) + ".unlock", unlock});
    for (int upcnt = 0; upcnt < 7; ++upcnt) out.push_back({"plug_lab_name." + std::to_string(upcnt), ov::plug_lab_name(upcnt)});
    for (const auto result : {ov::BuyResult::bought, ov::BuyResult::not_enough, ov::BuyResult::maxed, ov::BuyResult::bad_stat})
        out.push_back({"buy_result." + std::to_string(int(result)), ov::buy_result_text(result)});
    out.push_back({"save_problem.read_only", "Not saved: overflow.dat was in use when the game started."});
    out.push_back({"save_problem.write", "Not saved: the profile file cannot be written."});
    out.push_back({"results.bits", "Bits 1234567890"});
    out.push_back({"results.sim_bits", "Sim Bits +1234567890   (1234567890)"});
    for (const char* notice : {"The return gate is open", "TIME UP", "Hold out until 25:00", "HP Disk", "¶ Data +100", "Strength Plug +5",
                               "Blossomon appears", "LordKnightmon: the finale", "LordKnightmon defeated", "AWAKENING: Data Destroyer"})
        out.push_back({"notice", notice});
    return out;
}

// T9 without the game data for lane F's texts: ASCII and the game's glyphs.
void test_meta_glyphs() {
    const auto texts = meta_texts();
    CHECK(texts.size() > 100);
    for (const auto& [id, line] : texts) {
        CHECK(!line.empty());
        for (const char32_t c : ov::code_points(line)) {
            const bool ok = (c >= 0x20 && c < 0x7F) || kGlyphs.find(c) != std::u32string::npos;
            if (!ok) std::printf("  T9 (meta): %s: U+%04X is not one of the game's glyphs\n", id.c_str(), unsigned(c));
            CHECK(ok);
        }
    }
}

// The text without its printf placeholders (%s, %d: filled with game text
// or digits, which T9 checks where they come from).
std::string placeholders_out(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 1 < text.size() && (text[i + 1] == 's' || text[i + 1] == 'd')) {
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

// T9 with the game data: every code point is in bectext.igb's NORMAL font.
void test_font(const dw4::MountedResources& resources) {
    dw4::game::SimUi ui;
    ui.set_resources(resources);
    CHECK(ui.ready());
    const auto& codes = ui.font_codes();
    std::printf("font: %zu characters\n", codes.size());
    CHECK(codes.size() > 90);
    // The HUD's build row glyphs (build_row: elements, Pwr Up / Pwr Dwn, gear).
    for (const char32_t c : ov::code_points("⊂⊃∪∩↑↓∋∨∧∞αβγδε")) CHECK(codes.contains(std::uint32_t(c)));
    int missing = 0;
    for (const auto& entry : ov::all_texts())
        for (const char32_t c : ov::code_points(placeholders_out(entry.text))) {
            if (c == U'|') continue;
            if (!codes.contains(std::uint32_t(c))) {
                std::printf("  T9: %.*s: U+%04X missing in the font\n", int(entry.id.size()), entry.id.data(), unsigned(c));
                ++missing;
            }
        }
    CHECK(missing == 0);
    std::printf("T9: %zu strings, %d missing glyphs\n", ov::all_texts().size(), missing);
    // The director's own strings (build page, offers, results, F's meta
    // lines) are ASCII and the game glyphs: every printable ASCII draws with
    // fit_to_font (SimUi's text), so none of them has a gap (9.1).
    const auto has = [&](char16_t c) { return codes.contains(std::uint32_t(c)); };
    std::string absent;
    int gaps = 0;
    for (char16_t c = 0x21; c < 0x7F; ++c) {
        if (!has(c)) absent += char(c);
        for (const char16_t f : ov::fit_to_font(std::u16string(1, c), has))
            if (!has(f)) {
                std::printf("  T9: ASCII '%c' has no stand-in in the font\n", char(c));
                ++gaps;
            }
    }
    for (const char* line : {"  Blunt skill 650 / cap 1300, Forge 2 of 5", "Highest hit taken 35 % of MaxHP", "Chips: Drain Ram / Brave Core",
                             "Sim Bits +120   (480)", "NEW! Blossom Data   opens Drain Ram", "Damage dealt: 12,345 (Art 60 %)"})
        for (const char16_t f : ov::fit_to_font(ov::to_game_text(line), has))
            if (!has(f) && f != u' ') {
                std::printf("  T9: \"%s\": U+%04X has no glyph\n", line, unsigned(f));
                ++gaps;
            }
    std::printf("T9: ASCII the font lacks: [%s], drawn with stand-ins; %d gaps\n", absent.c_str(), gaps);
    CHECK(gaps == 0);
    // Lane F's texts and lane E's notices: every character in the font
    // itself (the space is the font's advance), none left to a stand-in.
    const auto texts = meta_texts();
    int meta_missing = 0;
    for (const auto& [id, line] : texts)
        for (const char32_t c : ov::code_points(line))
            if (c != U' ' && !codes.contains(std::uint32_t(c))) {
                std::printf("  T9 (meta): %s: U+%04X missing in the font\n", id.c_str(), unsigned(c));
                ++meta_missing;
            }
    CHECK(meta_missing == 0);
    std::printf("T9: lane F's texts and lane E's notices: %zu strings, %d missing glyphs\n", texts.size(), meta_missing);
}

void test_npc_rules() {
    ov::NpcConditions c;
    c.lobby = true;
    c.intro_seen = true;
    c.gate_open = true;
    auto s = ov::npc_state(c);
    CHECK(s.shown && s.talk && !s.crowd);
    c.players = 2;
    s = ov::npc_state(c);
    CHECK(s.shown && s.talk && s.crowd);
    c.players = 1;
    c.offline = false;
    CHECK(!ov::npc_state(c).shown);
    c.offline = true;
    c.setting = false;
    CHECK(!ov::npc_state(c).shown);
    c.setting = true;
    c.gate_open = false;
    CHECK(!ov::npc_state(c).shown);
    c.gate_open = true;
    c.intro_seen = false;
    CHECK(!ov::npc_state(c).shown);
    c.intro_seen = true;
    c.lobby = false;
    CHECK(!ov::npc_state(c).shown);
    c.lobby = true;
    // Test runs: hidden unless DW4_OVERFLOW asks.
    c.test_run = true;
    CHECK(!ov::npc_state(c).shown);
    c.test = ov::npc_test_mode("show");
    s = ov::npc_state(c);
    CHECK(s.shown && !s.talk);
    c.test = ov::npc_test_mode("1");
    c.intro_seen = false; // (a test asks for it whatever the story)
    s = ov::npc_state(c);
    CHECK(s.shown && s.talk);
    CHECK(ov::npc_test_mode(nullptr) == ov::NpcTestMode::off);
    CHECK(ov::npc_test_mode("0") == ov::NpcTestMode::off);

    std::map<std::int32_t, std::uint8_t> flags;
    CHECK(!ov::gate_open(flags));
    flags[2] = 1;
    CHECK(ov::gate_open(flags));
    flags = {{501, 1}, {4, 0}};
    CHECK(!ov::gate_open(flags));
    flags[86] = 1;
    CHECK(ov::gate_open(flags));

    // The talk box (offset (0,4,7) of a player standing 7 units south of
    // the NPC, facing it) reaches the 10-unit cube; 15 units away it does not.
    CHECK(ov::talk_reaches({50, 14, -86}));
    CHECK(ov::talk_reaches({55, 12, -80}));
    CHECK(!ov::talk_reaches({50, 14, -95}));
    CHECK(!ov::talk_reaches({50, 30, -80}));
}

// 3.3: the A of the talk does not act in the dialog (also when the host gave
// the closed lobby no keys), a held stick does not move the cursor, and
// opening / closing latch the story's pad (the A that closes the dialog must
// not talk again). "Welcome back" opens once, only when the NPC talks and
// player 1 has the control.
void test_lobby_keys() {
    const std::uint32_t A = dw4::pad_button::a, DOWN = dw4::pad_button::down;
    ov::NpcConditions c;
    c.lobby = true;
    c.intro_seen = true;
    c.gate_open = true;
    const auto talk = [](ov::OverflowLobby& lobby) {
        std::vector<dw4::engine::PortEvent> events(1);
        events[0].kind = dw4::engine::PortEvent::Kind::talk_box;
        events[0].slot = 0;
        events[0].x = 50;
        events[0].y = 14;
        events[0].z = -86;
        lobby.take_events(events);
        CHECK(events.empty());
        lobby.stepped(true);
        lobby.stepped(true);
    };
    for (const bool closed_keys : {true, false}) {
        ov::OverflowLobby lobby;
        lobby.meta().seen_intro = true;
        lobby.update(c, std::nullopt, 0.0);
        CHECK(lobby.state().talk);
        // The closed lobby sees the held A (the host's new pattern) or nothing (the old one).
        lobby.input(closed_keys ? A : 0u, 0, 0);
        talk(lobby);
        CHECK(lobby.dialog_open());
        CHECK(lobby.menu().screen() == ov::LobbyScreen::choice);
        CHECK(lobby.take_story_latch()); // (opening latches the story's pad)
        CHECK(!lobby.take_story_latch());
        // The talk's A held on: no choice ("Enter the simulator" stays unchosen).
        for (int i = 0; i < 4; ++i) CHECK(lobby.input(A, 0, 0).kind == ov::LobbyAction::Kind::none);
        CHECK(lobby.menu().screen() == ov::LobbyScreen::choice);
        // A held stick (down) does not move the cursor either.
        lobby.input(0, 0, -1.0f);
        CHECK(lobby.menu().cursor() == 0);
        lobby.input(0, 0, 0);
        // Released: Leave (the fifth choice), the goodbye page, A closes.
        for (int i = 0; i < 4; ++i) {
            lobby.input(DOWN, 0, 0);
            lobby.input(0, 0, 0);
        }
        CHECK(lobby.menu().cursor() == 4);
        lobby.input(A, 0, 0);
        CHECK(lobby.menu().screen() == ov::LobbyScreen::talk);
        CHECK(lobby.menu().pages().size() == 1 && lobby.menu().pages()[0] == ov::text(ov::Text::goodbye));
        lobby.input(0, 0, 0);
        CHECK(!lobby.take_story_latch());
        CHECK(lobby.input(A, 0, 0).kind == ov::LobbyAction::Kind::closed);
        CHECK(!lobby.dialog_open());
        CHECK(lobby.take_story_latch()); // (closing latches the story's pad: the held A stays out)
    }
    // The crowd line (2-4 players) is not closed by the talk's A.
    {
        ov::NpcConditions crowd = c;
        crowd.players = 2;
        ov::OverflowLobby lobby;
        lobby.update(crowd, std::nullopt, 0.0);
        lobby.input(0, 0, 0);
        talk(lobby);
        CHECK(lobby.dialog_open() && !lobby.menu().pages().empty() && lobby.menu().pages()[0] == ov::text(ov::Text::crowd));
        lobby.input(A, 0, 0);
        CHECK(lobby.dialog_open());
        lobby.input(0, 0, 0);
        lobby.input(A, 0, 0);
        CHECK(!lobby.dialog_open());
    }
    // Welcome back: once, only with talk and the control.
    {
        ov::OverflowLobby lobby;
        lobby.meta().seen_intro = true;
        ov::NpcConditions show = c;
        show.test_run = true;
        show.test = ov::npc_test_mode("show");
        lobby.update(show, std::nullopt, 0.0);
        CHECK(lobby.state().shown && !lobby.state().talk);
        ov::RunSummary back;
        back.end = ov::RunEnd::cleared;
        back.seconds = 25 * 60;
        lobby.returned(back);
        lobby.stepped(true);
        CHECK(!lobby.dialog_open()); // ("show" never talks)
        lobby.update(c, std::nullopt, 0.0);
        lobby.stepped(false);
        CHECK(!lobby.dialog_open()); // (an original talk has the control)
        lobby.stepped(true);
        CHECK(lobby.dialog_open() && lobby.menu().screen() == ov::LobbyScreen::talk);
        CHECK(!lobby.menu().pages().empty() && lobby.menu().pages()[0].starts_with("Welcome back"));
        lobby.close();
        CHECK(lobby.take_story_latch());
        lobby.input(0, 0, 0);
        talk(lobby);
        CHECK(lobby.dialog_open() && lobby.menu().screen() == ov::LobbyScreen::choice); // (not Welcome back again)
    }
}

// Lane F's records and per-tier clears (fill_records, a stand-in of F's
// profile and record_table) and the records pages.
struct FakeRecordDef {
    const char* key;
    const char* title;
    const char* condition;
    const char* opens;
};
struct FakeFormStats {
    std::array<int, 3> clears{};
};
struct FakeProfile {
    std::map<std::string, std::int64_t, std::less<>> records;
    std::map<std::string, FakeFormStats, std::less<>> forms;
};
void test_records() {
    std::vector<FakeRecordDef> table;
    static std::vector<std::string> keys;
    for (int i = 0; i < 23; ++i) keys.push_back("record_" + std::to_string(i));
    for (int i = 0; i < 23; ++i) table.push_back({keys[std::size_t(i)].c_str(), "Title", "Defeat Blossomon.", "Drain Ram"});
    FakeProfile profile;
    profile.records["record_1"] = 20261106;
    profile.records["record_2"] = 0;
    profile.forms["p_agumon"].clears = {3, 1, 0};
    ov::LobbyMeta meta;
    CHECK(ov::records_pages(meta) == 2); // (no rows: the stand-ins, the Digimon page)
    ov::fill_records(profile, table, meta);
    CHECK(meta.record_rows.size() == 23);
    CHECK(meta.record_rows[0].date == 0 && meta.record_rows[1].date == 20261106 && meta.record_rows[2].date == 1);
    CHECK(meta.record_rows[1].opens == "Drain Ram" && meta.record_rows[1].condition == "Defeat Blossomon.");
    CHECK(meta.forms["p_agumon"].tier_clears[0] == 3 && meta.forms["p_agumon"].tier_clears[1] == 1);
    CHECK(ov::records_pages(meta) == 4); // (8 + 8 + 7 records, the Digimon page)
    CHECK(ov::record_date(20261106) == "2026-11-06");
    CHECK(ov::record_date(1) == "Done");
    ov::LobbyMenu menu;
    menu.open_screen(ov::LobbyScreen::records, meta);
    ov::MenuKeys right, left;
    right.right = true;
    left.left = true;
    CHECK(menu.records_page() == 0);
    for (int i = 1; i <= 3; ++i) {
        menu.input(right, meta);
        CHECK(menu.records_page() == i);
    }
    menu.input(right, meta);
    CHECK(menu.records_page() == 0);
    menu.input(left, meta);
    CHECK(menu.records_page() == 3);
}

// The EXP bar inside the level (NEXTEXP is cumulative), the results' layout
// with more than 16 / 32 lines, the font's missing ASCII.
void test_run_numbers() {
    // Curve 80/50/6 (run_next_exp): Lv 5 starts at NEXTEXP(4), 65 % of NEXTEXP(5).
    const auto next = [](int level) {
        long long total = 0;
        for (int k = 0; k < level; ++k) total += 80 + 50LL * k + 6LL * k * k;
        return int(total);
    };
    CHECK(ov::level_progress(next(4), next(4), next(5)) == 0.0f);
    CHECK(std::abs(ov::level_progress((next(4) + next(5)) / 2, next(4), next(5)) - 0.5f) < 0.01f);
    CHECK(ov::level_progress(next(5), next(4), next(5)) == 1.0f);
    CHECK(ov::level_progress(10, 0, 0) == 0.0f);
    std::vector<std::string> lines;
    for (int i = 0; i < 19; ++i) lines.push_back("Line " + std::to_string(i));
    lines.push_back("Bits 95");
    lines.push_back("Sim Bits +120   (480)");
    lines.push_back("NEW! Blossom Data   opens Drain Ram");
    CHECK(ov::result_columns(16) == 1);
    CHECK(ov::result_columns(lines.size()) == 2);
    CHECK(ov::fit_result_lines(lines, 32) == lines); // (22 lines: all shown, two columns)
    // The HUD's gear glyphs from lane E's build page lines (9.3).
    ov::RunScreenData row;
    ov::build_row({"Art: Cleaver with Battle Hawk α", "  Blunt skill 650 / cap 1300, Forge 2 of 5", "  Fire: Petty (Blast 10 / cap 750)",
                   "Core: Brave Core β +30   Board: Fire Board α +60", "Chips: Wisdom Chip γ / -   ¶ Data 120", "Time 01:00   Level 5   Defeated 10"},
                  row);
    CHECK(row.gear == "∋β ∨α ∧γ");
    CHECK(row.techniques.size() == 1 && row.techniques[0] == "⊂P");
    ov::build_row({"Art: Cleaver with Battle Hawk α", "Core: -   Board: -", "Chips: - / -   ¶ Data 0"}, row);
    CHECK(row.gear.empty());
    for (int i = 19; i < 40; ++i) lines.insert(lines.end() - 3, "Line " + std::to_string(i));
    const auto fit = ov::fit_result_lines(lines, 32);
    CHECK(fit.size() == 32);
    CHECK(fit[29] == "Bits 95" && fit[30].starts_with("Sim Bits") && fit[31].starts_with("NEW!"));
    CHECK(fit[0] == "Line 0" && fit[28] == "Line 28");
    // A font without '/' and '%' (9.1): stand-ins, no gaps.
    const auto has = [](char16_t c) { return c >= 0x20 && c < 0x7F && c != u'/' && c != u'%' && c != u'['; };
    CHECK(ov::fit_to_font(u"skill 10 / cap 1300", has) == u"skill 10 - cap 1300");
    CHECK(ov::fit_to_font(u"taken 35 % of MaxHP [x]", has) == u"taken 35 pct of MaxHP (x]");
    const auto wide = [&](char16_t c) { return has(c) || c == u'\uFF0F'; };
    CHECK(ov::fit_to_font(u"a/b", wide) == u"a\uFF0Fb");
}

void test_menu() {
    ov::LobbyMeta meta;
    ov::LobbyMenu menu;
    ov::MenuKeys a;
    a.a = true;
    ov::MenuKeys down;
    down.down = true;
    ov::MenuKeys right;
    right.right = true;
    ov::MenuKeys b;
    b.b = true;
    // The first meeting: six pages, then "What will you do?".
    menu.open(false, meta, std::nullopt);
    CHECK(menu.screen() == ov::LobbyScreen::talk);
    CHECK(menu.pages().size() == 6);
    for (int i = 0; i < 5; ++i) CHECK(menu.input(a, meta).kind == ov::LobbyAction::Kind::none);
    CHECK(menu.input(a, meta).kind == ov::LobbyAction::Kind::intro_seen);
    CHECK(menu.screen() == ov::LobbyScreen::choice);
    meta.seen_intro = true;
    // Enter the simulator: the select, Guilmon, the Brave Core, Start, Yes.
    CHECK(menu.input(a, meta).kind == ov::LobbyAction::Kind::none);
    CHECK(menu.screen() == ov::LobbyScreen::select);
    menu.input(right, meta);
    menu.input(right, meta);
    CHECK(menu.face() == 2);
    menu.input(down, meta); // armour
    menu.input(right, meta);
    CHECK(menu.armor() == 1);
    menu.input(down, meta); // chip
    menu.input(right, meta);
    menu.input(right, meta);
    menu.input(down, meta); // level
    menu.input(down, meta); // start
    CHECK(menu.line() == 4);
    menu.input(a, meta);
    CHECK(menu.confirming());
    const auto start = menu.input(a, meta);
    CHECK(start.kind == ov::LobbyAction::Kind::start_run);
    CHECK(start.request.form == "p_girumon");
    CHECK(start.request.options.at("armor") == "brave_core");
    CHECK(start.request.options.at("chip") == "spirit_chip");
    CHECK(start.request.options.at("tier") == "normal");
    CHECK(!menu.is_open());
    // The select starts with the form's last loadout (8.3).
    meta.loadouts["p_vmon"] = {"ice_board", "speed_chip"};
    menu.open(false, meta, std::nullopt);
    menu.input(a, meta);
    CHECK(menu.face() == 2); // (the face stays: Guilmon, no loadout kept)
    CHECK(menu.armor() == 0 && menu.chip() == 0);
    ov::MenuKeys back_left;
    back_left.left = true;
    menu.input(back_left, meta);
    CHECK(menu.face() == 1);
    CHECK(meta.armors[std::size_t(menu.armor())] == "ice_board" && meta.chips[std::size_t(menu.chip())] == "speed_chip");
    menu.close();
    meta.loadouts.clear();
    // Later talks: the choice list at once; Never mind closes after its page.
    menu.open(false, meta, std::nullopt);
    CHECK(menu.screen() == ov::LobbyScreen::choice);
    for (int i = 0; i < 4; ++i) menu.input(down, meta);
    CHECK(menu.cursor() == 4);
    menu.input(a, meta);
    CHECK(menu.screen() == ov::LobbyScreen::talk);
    CHECK(menu.pages().front() == "Come back any time!");
    CHECK(menu.input(a, meta).kind == ov::LobbyAction::Kind::closed);
    CHECK(!menu.is_open());
    // How does it work?: four pages, then the list again.
    menu.open(false, meta, std::nullopt);
    for (int i = 0; i < 3; ++i) menu.input(down, meta);
    menu.input(a, meta);
    CHECK(menu.screen() == ov::LobbyScreen::help);
    CHECK(menu.pages().size() == 4);
    for (int i = 0; i < 4; ++i) menu.input(a, meta);
    CHECK(menu.screen() == ov::LobbyScreen::choice);
    // B on the select goes back; the confirm's No stays in the select.
    menu.input(a, meta);
    CHECK(menu.screen() == ov::LobbyScreen::select);
    menu.input(a, meta);
    CHECK(menu.confirming());
    ov::MenuKeys left;
    left.left = true;
    menu.input(left, meta);
    CHECK(menu.input(a, meta).kind == ov::LobbyAction::Kind::none);
    CHECK(menu.screen() == ov::LobbyScreen::select && !menu.confirming());
    menu.input(b, meta);
    CHECK(menu.screen() == ov::LobbyScreen::choice);
    // The Plug Lab buys through the meta lane.
    menu.input(down, meta);
    menu.input(a, meta);
    CHECK(menu.screen() == ov::LobbyScreen::plug_lab);
    menu.input(down, meta);
    const auto buy = menu.input(a, meta);
    CHECK(buy.kind == ov::LobbyAction::Kind::buy_plug && buy.form == "p_agumon" && buy.stat == 1);
    menu.input(b, meta);
    CHECK(menu.screen() == ov::LobbyScreen::choice);
    // 2-4 players: one line and the talk ends.
    menu.open(true, meta, std::nullopt);
    CHECK(menu.pages().size() == 1);
    CHECK(menu.input(a, meta).kind == ov::LobbyAction::Kind::closed);
    // After a run: "Welcome back" before the list.
    ov::RunSummary run;
    run.end = ov::RunEnd::defeated;
    run.seconds = 12 * 60 + 41;
    menu.open(false, meta, run);
    CHECK(menu.pages().front() == "Welcome back! You lasted 12:41.|Not bad at all!");
    menu.input(a, meta);
    CHECK(menu.screen() == ov::LobbyScreen::choice);
    run.defeated_by = "e_tonogeko";
    CHECK(ov::welcome_back(run) == "Welcome back! Beaten by a|Gekomon? I will pretend I did|not see that.");
    run.end = ov::RunEnd::cleared;
    CHECK(ov::welcome_back(run).starts_with("Welcome back! Simulation"));
    CHECK(ov::quest_timer_text(299.2) == "5:00");
    // The HUD's build row from the director's build page.
    ov::RunScreenData row;
    ov::build_row({"Art: Cleaver with Battle Hawk α", "  Blunt skill 650 / cap 1300, Forge 2 of 5", "  Fire: Mega (Blast 120 / cap 750)",
                   "  Heal Lt: not learned (Heal 1 / cap 300)", "  Bolt: ∞ (Blast 9 / cap 9)", "Time 01:00   Level 3   Defeated 9"},
                  row);
    CHECK(row.weapon == "Battle Hawk α");
    CHECK(row.weapon_fraction > 0.49f && row.weapon_fraction < 0.51f);
    CHECK(row.techniques.size() == 3);
    CHECK(row.techniques[0] == "⊂M" && row.learned[0]);
    CHECK(row.techniques[1] == "H" && !row.learned[1]);
    CHECK(row.techniques[2] == "∪∞"); // (∪ thunder, ∩ dark: SEAMLESS.md, SJIS 81BC-81BF)
    CHECK(ov::text(ov::Text::tech_vmon).starts_with("∪Bolt"));
    CHECK(ov::text(ov::Text::tech_girumon).find("∩Dark") != std::string_view::npos);
    CHECK(ov::text(ov::Text::tech_dorumon).find("∩Dark") != std::string_view::npos);
    CHECK(ov::quest_timer_text(59.0) == "0:59");
    // (Integration review, player lens.) Pwr Up and Pwr Dwn as the game's
    // arrows (not "P" twice beside the rank letter P); the chips "A, B" or
    // "none" (the font has no '/': "- / -" drew as "- - -").
    ov::build_row({"Art: Fang Thrust with Pike α", "  Fire: Petty (Blast 10 / cap 750)", "  Pwr Dwn: not learned (Buff 1 / cap 300)",
                   "  Pwr Up: Petty (Buff 20 / cap 300)", "Core: none   Board: none", "Chips: Wisdom Chip γ, Speed Chip α   ¶ Data 0"},
                  row);
    CHECK(row.techniques.size() == 3 && row.techniques[0] == "⊂P" && row.techniques[1] == "↓" && row.techniques[2] == "↑P");
    CHECK(row.gear == "∧γ ∧α");
    ov::build_row({"Art: Fang Thrust with Pike α", "Core: none   Board: none", "Chips: none   ¶ Data 0"}, row);
    CHECK(row.gear.empty());
    // The select starts at the last form chosen (8.5 `last`).
    {
        ov::LobbyMeta last;
        last.seen_intro = true;
        last.last_form = "p_dorumon";
        ov::LobbyMenu select;
        select.open(false, last, std::nullopt);
        select.input(a, last);
        CHECK(select.screen() == ov::LobbyScreen::select && select.face() == 3);
    }
    // The Plug Lab answers a purchase for about two seconds.
    {
        ov::LobbyMeta lab;
        lab.seen_intro = true;
        ov::LobbyMenu shop;
        shop.open(false, lab, std::nullopt);
        shop.input(down, lab);
        shop.input(a, lab);
        CHECK(shop.screen() == ov::LobbyScreen::plug_lab);
        CHECK(shop.input(a, lab).kind == ov::LobbyAction::Kind::buy_plug);
        shop.set_answer("Not enough Sim Bits.", false);
        CHECK(shop.answer() == "Not enough Sim Bits." && !shop.answer_good());
        for (std::uint64_t i = 0; i < ov::LobbyMenu::kAnswerFrames; ++i) shop.tick();
        CHECK(shop.answer().empty());
        shop.set_answer("Bought.", true);
        shop.input(b, lab);
        CHECK(shop.screen() == ov::LobbyScreen::choice && shop.answer().empty());
    }
}

// The results' layout (integration review): two columns break between
// groups, never between a heading and its entries; A waits for the lines.
void test_results_layout() {
    const std::vector<std::string> lines{"Time 25:00", "Run level 37", "Digimon Agumon", "Guardians Mammothmon, Anomalocarimon",
                                         "Defeated 4653", "   Goburimon 900", "   Gazimon 800", "   Mushroomon 700", "   Vegiemon 600",
                                         "   Tortomon 500", "Damage dealt", "   Cleaver 900000", "   Fire 1000", "   Bomb 900",
                                         "   Cutter 800", "   Heal Lt 0", "   Resist 0", "Highest hit taken 30 % of MaxHP",
                                         "Weapon Battle Hawk, Forge 5", "Bits 32", "Sim Bits +532   (532)"};
    const std::vector<int> ones(lines.size(), 1);
    const std::size_t split = ov::column_break(lines, ones);
    CHECK(split > 0 && split < lines.size() && !lines[split].starts_with(' '));
    CHECK(lines[split - 1] != "Damage dealt" && lines[split - 1] != "Defeated 4653");
    CHECK(lines[split] == "Damage dealt"); // (10 rows left, 11 right)
    // A wrapped line takes its rows into the count.
    std::vector<int> tall = ones;
    tall[3] = 3;
    CHECK(!lines[ov::column_break(lines, tall)].starts_with(' '));
    // Without any group start: halves.
    CHECK(ov::column_break({"   a", "   b", "   c", "   d"}, {}) == 2);
    CHECK(ov::results_wait(31) == 240 && ov::results_wait(3) == 60 && ov::results_wait(0) == 60);
}

// Every screen makes 2D items in the 640 x 480 layer (with the font).
void test_screen_items(const dw4::MountedResources& resources) {
    dw4::game::SimUi ui;
    ui.set_resources(resources);
    if (!ui.ready()) return;
    ov::LobbyMeta meta;
    ov::LobbyMenu menu;
    const ov::LobbyScreen screens[] = {ov::LobbyScreen::talk, ov::LobbyScreen::choice, ov::LobbyScreen::select, ov::LobbyScreen::plug_lab,
                                       ov::LobbyScreen::records, ov::LobbyScreen::help};
    for (const auto screen : screens) {
        menu.open_screen(screen, meta);
        for (int i = 0; i < 10; ++i) menu.tick();
        std::vector<dw4::engine::DrawItem> items;
        ui.begin();
        ov::draw_lobby_menu(ui, menu, meta, 0);
        ui.finish(items);
        CHECK(items.size() > 4);
        for (const auto& item : items) {
            CHECK(item.state.projection && (*item.state.projection)(2, 3) == 0.0f);
            CHECK(item.geometry && item.geometry->valid);
            for (const auto& p : item.geometry->positions) CHECK(p.x >= -321 && p.x <= 321 && p.y >= -241 && p.y <= 241);
        }
    }
    ov::RunScreenData data;
    data.techniques = {"⊂P", "-", "-", "-", "-", "-"};
    data.learned = {true, false, false, false, false, false};
    data.weapon = "Battle Hawk α";
    data.offers = {{"∈ Forge: Crusher", "Blunt cap 1920", "about"}, {"⊂ Train Blast", "cap 750 > 1250", "about"}};
    data.result_lines = {"Time 12:41", "Run level 17", "NEW! Ten Minutes"};
    data.build_lines = {"∈ Battle Hawk α", "∋ Brave Core α"};
    for (const auto screen : {ov::RunScreen::none, ov::RunScreen::level_up, ov::RunScreen::pause, ov::RunScreen::build, ov::RunScreen::retire_ask,
                              ov::RunScreen::results}) {
        data.screen = screen;
        data.screen_frames = 100;
        std::vector<dw4::engine::DrawItem> items;
        ui.begin();
        ov::draw_run_screens(ui, data, 0);
        ui.finish(items);
        CHECK(items.size() > 4);
    }
    data.screen = ov::RunScreen::none;
    data.finale = true;
    data.finale_left = 42;
    std::vector<dw4::engine::DrawItem> items;
    ui.begin();
    ov::draw_run_screens(ui, data, 0);
    ui.finish(items);
    CHECK(items.size() > 4);
    // The clear banner shows for 4 s after the clear (the O1b integration),
    // then only the HUD; TIME UP adds its banner.
    const auto count = [&](const ov::RunScreenData& d) {
        std::vector<dw4::engine::DrawItem> drawn;
        ui.begin();
        ov::draw_run_screens(ui, d, 0);
        ui.finish(drawn);
        return drawn.size();
    };
    const std::size_t plain = count(data);
    data.cleared = true;
    data.clear_frames = 10;
    CHECK(count(data) > plain);
    data.clear_frames = 240;
    CHECK(count(data) == plain);
    data.cleared = false;
    data.time_up = true;
    CHECK(count(data) > plain);
}

void test_actor(const dw4::MountedResources& resources) {
    dw4::game::PresentationActor actor("stag0302.igb", "n_gekoa");
    actor.load(resources);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!actor.ready() && !actor.failed() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(actor.ready());
    // The talk window's face icon: n_gekoaic's picture from the same member.
    if (const auto document = actor.document()) {
        const auto face = ov::model_texture(*document, "n_gekoaic");
        CHECK(face && face->texture && face->image);
        CHECK(!ov::model_texture(*document, "no_such_set"));
    }
    std::printf("actor n_gekoa: %s\n", actor.ready() ? "loaded" : "not loaded");
}

} // namespace

int main(int argc, char** argv) {
    test_text_glyphs();
    test_meta_glyphs();
    test_npc_rules();
    test_menu();
    test_lobby_keys();
    test_run_numbers();
    test_records();
    test_results_layout();
    std::string data = argc > 1 ? argv[1] : "";
    if (data.empty())
        if (const char* env = std::getenv("DW4_GAME_DATA")) data = env;
    if (!data.empty() && std::filesystem::exists(data)) {
        const auto resources = dw4::MountedResources::open(data);
        test_font(resources);
        test_screen_items(resources);
        test_actor(resources);
    } else {
        std::printf("no game data: T9 against the glyph list of 9.1 only\n");
    }
    std::printf("overflow_screens: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
