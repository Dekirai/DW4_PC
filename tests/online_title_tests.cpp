// Round I1a (docs/SEAMLESS.md 19.5.1): Play's title driver. Without game
// data: the decisions per screen and the press rhythm. With the game data
// (DW4_TEST_DATA): the real title, the character select and the online
// card, driven from different start offsets into stagML11 with the card
// box's character, and the difficulty dialog (answered by a test run's
// difficulty, or handed to the player and back).

#include "dw4/card_store.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/pad.hpp"
#include "dw4/save_data.hpp"
#include "online/character.hpp"
#include "online/title_driver.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace dw4::game::online;
namespace button = dw4::pad_button;

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

TitleView title_view() {
    TitleView v;
    v.stage = "stagis10";
    v.ready = true;
    return v;
}

TitleView panel_view(int state, int sub, int detail) {
    auto v = title_view();
    v.title_state = 8;
    v.ctrl_state = 2;
    dw4::engine::LoadIntf2Manager::PanelView p;
    p.state = state;
    p.sub = sub;
    p.detail = detail;
    v.panel = p;
    return v;
}

using Kind = TitleDriver::Want::Kind;

void decisions() {
    TitleDriver d;
    d.start({2, std::nullopt, 5400});
    auto v = title_view();
    v.title_state = 1;
    v.title_sub = 2;
    v.title_step = 1;
    check(d.decide(v).kind == Kind::press && d.decide(v).button == button::start, "press start: START");
    v.title_detail = 1000; // the prompt timed out: the attract demo
    check(d.decide(v).kind == Kind::wait, "the attract demo: nothing");
    v = title_view();
    v.title_state = 2;
    v.title_sub = 1;
    v.title_step = 1;
    check(d.decide(v).button == button::a, "Game Start under the cursor: A");
    v.title_cursor = 2;
    check(d.decide(v).button == button::up, "the cursor on Online: up");
    v.ready = false;
    check(d.decide(v).kind == Kind::wait, "a load: nothing");
    v.ready = true;
    v.stage = "stagis09";
    check(d.decide(v).kind == Kind::wait, "another stage: nothing");

    auto p = panel_view(3, 2, 0);
    check(d.decide(p).button == button::down, "Create Digimon under the cursor: down");
    p.panel->cursor = 1;
    check(d.decide(p).button == button::a, "Call Digimon: A");
    p.panel->detail = 1;
    check(d.decide(p).kind == Kind::wait, "the cursor moving: nothing");
    TitleDriver fresh;
    fresh.start({std::nullopt, std::nullopt, 5400});
    check(fresh.decide(panel_view(3, 2, 0)).kind == Kind::hand_over, "no online character: the original new-character flow");
    check(d.decide(panel_view(4, 2, 0)).kind == Kind::hand_over, "the partner selection: the player's");

    p = panel_view(6, 1, 0);
    p.panel->slot_phase = 1;
    p.panel->slot_cursor = 1;
    check(d.decide(p).button == button::up, "slot B under the cursor: up");
    p.panel->slot_cursor = 0;
    check(d.decide(p).button == button::a, "slot A: A");
    p.panel->slot_step = 1;
    check(d.decide(p).kind == Kind::wait, "the slot cursor moving: nothing");
    p = panel_view(6, 1, 1);
    p.panel->message_prompt = true;
    p.panel->message_step = 1;
    check(d.decide(p).kind == Kind::hand_over, "a card message: the player's");
    p = panel_view(6, 1, 1);
    p.entry_state = 5;
    check(d.decide(p).kind == Kind::wait, "the card read: nothing");

    // The boxes 0 1 / 2 3: box 2 from 1 (left, then down), box 3, box 0.
    p = panel_view(7, 1, 3);
    p.panel->box_cursor = 1;
    check(d.decide(p).button == button::down, "box 2 from box 1: down first");
    p.panel->box_cursor = 3;
    check(d.decide(p).button == button::left, "box 2 from box 3: left");
    p.panel->box_cursor = 2;
    check(d.decide(p).button == button::a, "box 2: A");
    p.panel->box_step = 1;
    check(d.decide(p).kind == Kind::wait, "the box cursor moving: nothing");
    TitleDriver zero;
    zero.start({0, std::nullopt, 5400});
    p = panel_view(7, 1, 3);
    p.panel->box_cursor = 3;
    check(zero.decide(p).button == button::up, "box 0 from box 3: up");
    p.panel->box_cursor = 1;
    check(zero.decide(p).button == button::left, "box 0 from box 1: left");

    // The difficulty dialog: the player's, or a test run's answer.
    p = panel_view(8, 0, 0);
    p.panel->detail2 = 1;
    p.panel->diff_done = false;
    p.panel->diff_count = 3;
    p.panel->diff_phase = 1;
    check(d.decide(p).kind == Kind::player, "the difficulty dialog: the player's");
    TitleDriver hard;
    hard.start({0, 2, 5400});
    check(hard.decide(p).button == button::down, "Very Hard from Normal: down");
    p.panel->diff_cursor = 2;
    check(hard.decide(p).button == button::a, "Very Hard: A");
    p.panel->diff_count = 2; // (a box with one clear: Normal / Hard)
    p.panel->diff_cursor = 1;
    check(hard.decide(p).button == button::a, "Very Hard asked, Hard the last row: A");
    p = panel_view(8, 3, 1);
    check(d.decide(p).button == button::start, "the status window, controller 1: START");
    check(d.decide(panel_view(8, 1, 1)).kind == Kind::wait, "waiting for the other panels: nothing");
}

void rhythm() {
    TitleDriver d;
    d.start({0, std::nullopt, 200});
    check(d.covering() && d.changed(), "started: the loading screen");
    auto v = title_view();
    v.title_state = 1;
    v.title_sub = 2;
    v.title_step = 1;
    check(d.step(v) == button::start, "the first step presses");
    check(d.step(v) == 0, "then one step up");
    int pressed_at = -1;
    for (int i = 0; i < 40 && pressed_at < 0; ++i)
        if (d.step(v) == button::start) pressed_at = i;
    check(pressed_at >= 25 && pressed_at <= 30, "the same screen again after half a second (" + std::to_string(pressed_at) + ")");
    (void)d.step(v);
    v.title_state = 2;
    v.title_sub = 1;
    v.title_step = 1;
    check(d.step(v) == button::a, "a new screen: at once");
    // The dialog lifts the loading screen and gives it back.
    auto p = panel_view(8, 0, 0);
    p.panel->detail2 = 1;
    p.panel->diff_done = false;
    check(d.step(p) == 0 && d.phase() == TitleDriver::Phase::player && !d.covering() && d.active() && d.changed(), "the dialog: the player's");
    p.panel->diff_done = true;
    (void)d.step(p);
    check(d.phase() == TitleDriver::Phase::driving && d.covering() && d.changed(), "after the dialog: driving again");
    auto lobby = title_view();
    lobby.stage = "stagml11";
    lobby.game_started = true;
    lobby.ready = false;
    (void)d.step(lobby);
    check(d.active(), "the lobby loading: still driving");
    lobby.ready = true;
    (void)d.step(lobby);
    check(d.phase() == TitleDriver::Phase::done && !d.active() && d.changed(), "the lobby plays: done (" + d.note() + ")");
    check(d.step(lobby) == 0, "done: nothing more");

    TitleDriver slow;
    slow.start({0, std::nullopt, 50});
    for (int i = 0; i < 60; ++i) (void)slow.step(title_view());
    check(slow.phase() == TitleDriver::Phase::gave_up && !slow.covering(), "no lobby in time: the player takes over");
    TitleDriver none;
    none.start({std::nullopt, std::nullopt, 50});
    (void)none.step(panel_view(3, 2, 0));
    check(none.phase() == TitleDriver::Phase::handed_over, "no online character: handed over at Create Digimon");
}

// ---------------------------------------------------------------------------
// With the game data.
CharacterRecord record_for(std::uint32_t unique, const char* name, bool cleared) {
    dw4::save::Record r;
    r.blank = false;
    r.used = true;
    r.unique = unique;
    r.digimon = "p_dorumon";
    r.name = dw4::save::name_to_card(name);
    r.level = 12;
    r.hp = r.max_hp = 160;
    r.devices.fill(-1);
    r.disks.fill(-1);
    r.bank.fill(-1);
    r.power_socket.fill(-1);
    r.power_pocket.fill(-1);
    r.main_slot = r.sub_slot = -1;
    r.main_pocket.fill(-1);
    r.base_level.fill(1);
    // A box with a clear: hostLoad.lua's LISsetDifficultMax offers the dialog
    // (flag 77, found by bisecting the 1,024 story flags against the dialog;
    // flag 711 alone does too).
    if (cleared) r.flags[77] = 1;
    CharacterRecord out{};
    const auto bytes = dw4::save::encode(r);
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

struct Drive {
    bool reached = false;
    std::string stage;
    int frames = 0, presses = 0, difficulty = -1, player_steps = 0;
    std::uint32_t unique = 0;
    std::string phase, note;
    bool dialog = false;
};

// The title from stagIS10 with the online card (box `box` holds the
// character) and the online rules; `idle` steps before the driver starts;
// `player` answers the dialog as a player would when the driver hands it over.
Drive drive(const std::filesystem::path& data, int box, int idle, std::optional<int> difficulty, bool cleared,
            const std::function<std::uint32_t(const TitleView&, int)>& player = nullptr) {
    Drive out;
    const auto folder = std::filesystem::temp_directory_path() / ("dw4_title_tests_" + std::to_string(box) + "_" + std::to_string(idle));
    std::filesystem::create_directories(folder);
    const auto card_a = folder / "online_card.dat", card_b = folder / "never_written.dat";
    std::filesystem::remove(card_b);
    const auto gci = card_with_record(nullptr, record_for(4242u + std::uint32_t(box), "Kim", cleared), box, false, 0);
    {
        auto store = dw4::save::CardStore::single_file(card_a, card_b);
        std::string error;
        if (!store.write(0, gci, &error)) throw std::runtime_error("the test card: " + error);
    }
    dw4::engine::Engine engine(data);
    engine.set_card_store(dw4::save::CardStore::single_file(card_a, card_b));
    engine.set_card_setting(0, {});
    dw4::engine::CardSetting absent;
    absent.mode = dw4::engine::CardSetting::Mode::none;
    engine.set_card_setting(1, absent);
    dw4::engine::OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0001;
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.set_view_slot(0);
    engine.request_stage("stagis10");
    TitleDriver driver;
    int player_step = 0;
    for (int i = 0; i < 9000; ++i) {
        std::array<dw4::PadSample, 4> pads{};
        pads[0].connected = true;
        const auto view = read_title(engine);
        if (i == idle) driver.start({box, difficulty, 5400});
        if (i >= idle) {
            pads[0].buttons = driver.step(view);
            if (driver.phase() == TitleDriver::Phase::player) {
                out.dialog = true;
                ++out.player_steps;
                if (player) pads[0].buttons = player(view, player_step++);
            }
            if (view.panel && view.panel->state == 8 && view.panel->detail2 == 1) out.dialog = true;
            if (!driver.active() && i > idle) break;
        }
        engine.step(pads);
        if (!engine.held()) engine.traverse_scene();
    }
    out.frames = int(engine.frame());
    out.presses = driver.presses();
    out.phase = title_phase_name(driver.phase());
    out.note = driver.note();
    out.stage = engine.stage_name();
    if (const auto* sys = engine.get<dw4::engine::GameSystem>()) {
        out.reached = driver.phase() == TitleDriver::Phase::done && sys->game_started && sys->players[0].joined;
        out.unique = sys->players[0].unique;
        out.difficulty = sys->difficulty;
    }
    engine.set_online(dw4::engine::OnlineRules{});
    std::filesystem::remove_all(folder);
    return out;
}

void with_data(const std::filesystem::path& data) {
    // Box 0 and box 3 (the grid's far corner), at once and after the title
    // idled for a while (the press-start prompt's own timing, the attract
    // demo's way back).
    for (const auto& [box, idle] : {std::pair{0, 0}, std::pair{3, 0}, std::pair{0, 777}, std::pair{2, 2400}}) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = drive(data, box, idle, std::nullopt, false);
        std::cout << "  box " << box << ", start after " << idle << " steps: " << r.phase << " (" << r.note << ") in " << r.stage << " at frame "
                  << r.frames << ", " << r.presses << " presses, unique " << r.unique << ", "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s\n";
        check(r.reached && r.unique == 4242u + std::uint32_t(box) && (r.stage == "stagML11" || r.stage == "stagml11") && !r.dialog,
              "Play's drive reaches the lobby with box " + std::to_string(box) + "'s character (start after " + std::to_string(idle) + ")");
    }
    // The difficulty dialog of a box with the story's end: a test run's
    // answer (Hard), and the player's (down, A) while the loading screen
    // is lifted.
    {
        const auto r = drive(data, 1, 0, 1, true);
        std::cout << "  the dialog answered by the run: " << r.phase << " (" << r.note << "), difficulty " << r.difficulty << ", dialog " << r.dialog
                  << '\n';
        check(r.reached && r.dialog && r.difficulty == 1, "the difficulty dialog: a test run's Hard");
    }
    {
        const auto player = [](const TitleView& view, int step) -> std::uint32_t {
            if (!view.panel || view.panel->diff_phase != 1 || view.panel->diff_step != 0) return 0;
            if (view.panel->diff_cursor == 0) return step % 10 == 0 ? button::down : 0;
            return step % 10 == 5 ? button::a : 0;
        };
        const auto r = drive(data, 1, 0, std::nullopt, true, player);
        std::cout << "  the dialog answered by the player: " << r.phase << " (" << r.note << "), difficulty " << r.difficulty << ", "
                  << r.player_steps << " steps the player's\n";
        check(r.reached && r.dialog && r.player_steps > 0 && r.difficulty == 1, "the difficulty dialog: the player's choice, then the drive");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        decisions();
        rhythm();
        if (argc > 1 && argv[1][0] && std::filesystem::exists(argv[1])) with_data(argv[1]);
        else std::cout << "  (no game data: the title drives skipped)\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    std::cout << "online_title: " << checks << " checks passed\n";
    return 0;
}
