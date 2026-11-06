// Data Overflow's screens (round O1b lane D; docs/SURVIVAL.md 3, 9, 14.1):
// T9 (every string of overflow_text.inc against the game font's characters,
// with the game data; without it against the glyph list of 9.1), the NPC's
// rules (3.2), the talk box's reach (3.3), the dialog's state machine (3.4,
// 9.2), the screens' 2D items and the NPC actor (n_gekoa from stag0302).
#include "overflow_lobby.hpp"
#include "overflow_screens.hpp"
#include "overflow_text.hpp"
#include "presentation_actor.hpp"
#include "sim_ui.hpp"

#include "dw4/resources.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    CHECK(row.techniques[2] == "∩∞");
    CHECK(ov::quest_timer_text(59.0) == "0:59");
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
}

void test_actor(const dw4::MountedResources& resources) {
    dw4::game::PresentationActor actor("stag0302.igb", "n_gekoa");
    actor.load(resources);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!actor.ready() && !actor.failed() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(actor.ready());
    std::printf("actor n_gekoa: %s\n", actor.ready() ? "loaded" : "not loaded");
}

} // namespace

int main(int argc, char** argv) {
    test_text_glyphs();
    test_npc_rules();
    test_menu();
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
