#include "online/title_driver.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/pad.hpp"

#include <algorithm>
#include <cctype>

namespace dw4::game::online {

namespace {
// Half a second of steps before a screen that did not move gets its press again.
constexpr int kRetrySteps = 30;
constexpr const char* kTitleStage = "stagis10";
} // namespace

TitleView read_title(const engine::Engine& engine, int slot) {
    TitleView v;
    v.stage = engine.stage_name();
    std::transform(v.stage.begin(), v.stage.end(), v.stage.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    v.ready = engine.presenting() && !engine.loading();
    if (const auto* sys = engine.get<engine::GameSystem>()) v.game_started = sys->game_started;
    if (const auto* title = engine.get<engine::Title2Manager>()) {
        v.title_state = title->state();
        v.title_sub = title->sub();
        v.title_step = title->step();
        v.title_detail = title->detail();
        v.title_cursor = title->cursor();
    }
    if (auto* ctrl = engine.get<engine::LoadCtrl2Manager>(); ctrl && ctrl->common().active) {
        v.ctrl_state = ctrl->state();
        const auto& entry = std::as_const(*ctrl).common().players[std::size_t(std::clamp(slot, 0, 3))];
        v.entry_state = entry.state;
        v.entry_active = entry.active;
    }
    if (const auto* panels = engine.get<engine::LoadIntf2Manager>()) v.panel = panels->panel(std::clamp(slot, 0, 3));
    return v;
}

void TitleDriver::start(const Config& config) {
    config_ = config;
    phase_ = Phase::driving;
    note_ = "driving";
    steps_ = presses_ = 0;
    last_press_ = -1000;
    released_ = true;
    changed_ = true;
    last_key_ = {};
}

void TitleDriver::finish(Phase phase, std::string why) {
    phase_ = phase;
    note_ = std::move(why);
    changed_ = true;
}

TitleDriver::Key TitleDriver::key_of(const TitleView& v) {
    Key k{};
    k[0] = v.title_state;
    k[1] = v.title_sub;
    k[2] = v.title_step;
    k[3] = v.title_detail;
    k[4] = v.title_cursor;
    k[5] = v.ctrl_state;
    k[6] = v.entry_state;
    if (v.panel) {
        const auto& p = *v.panel;
        k[7] = p.state;
        k[8] = p.sub;
        k[9] = p.detail;
        k[10] = p.detail2;
        k[11] = p.cursor;
        k[12] = p.slot_cursor + 4 * p.slot_phase + 16 * p.slot_step;
        k[13] = p.box_cursor + 4 * p.box_step;
        k[14] = p.diff_cursor + 4 * p.diff_phase + 16 * p.diff_step;
        k[15] = p.message_step + 4 * (p.message_prompt ? 1 : 0);
    }
    return k;
}

TitleDriver::Want TitleDriver::decide(const TitleView& v) const {
    using Kind = Want::Kind;
    const auto press = [](std::uint32_t button) { return Want{Kind::press, button, ""}; };
    const Want wait{};
    // (Loads, the attract demo stagIS09 and its way back: nothing to press.)
    if (v.stage != kTitleStage || !v.ready) return wait;
    // The character select (LOADCTRL2 GENERATE_TITLE): the own panel.
    if (v.ctrl_state == 2 && v.panel) {
        const auto& p = *v.panel;
        // A card message waiting for A (a card error, a question about the
        // card) is the player's to read.
        const bool message_waits = p.message_prompt && p.message_step == 1;
        if (v.entry_state != 0 || !v.entry_active) {
            // The panel's card flow (reading the card, another panel's card use).
            if (message_waits) return {Kind::hand_over, 0, "a memory card message"};
            return wait;
        }
        switch (p.state) {
        case 3: // Create Digimon / Call Digimon
            if (p.sub != 2 || p.detail != 0) return wait;
            if (!config_.box) return {Kind::hand_over, 0, "no online character yet: the original new-character flow"};
            return press(p.cursor == 1 ? pad_button::a : pad_button::down);
        case 4: // the partner, the name: the new-character flow (only after Create)
        case 5:
            return {Kind::hand_over, 0, "the new-character flow"};
        case 6: // Call Digimon: the card slot (A holds the online character's virtual card)
            if (p.sub == 1 && p.detail == 0 && p.slot_phase == 1 && p.slot_step == 0 && !p.slot_selected)
                return press(p.slot_cursor == 0 ? pad_button::a : pad_button::up);
            if (message_waits) return {Kind::hand_over, 0, "a memory card message"};
            return wait;
        case 7: { // the card file's four boxes (0 1 / 2 3)
            if (p.sub != 1 || p.detail != 3 || p.box_step != 0 || !config_.box) return wait;
            const int target = std::clamp(*config_.box, 0, 3), at = std::clamp(p.box_cursor, 0, 3);
            if (target / 2 != at / 2) return press(at / 2 == 0 ? pad_button::down : pad_button::up);
            if (target % 2 != at % 2) return press(at % 2 == 0 ? pad_button::right : pad_button::left);
            return press(pad_button::a);
        }
        case 8: // the character: the difficulty dialog, the status window, controller 1's START
            if (p.sub == 0 && p.detail == 0 && p.detail2 == 1 && !p.diff_done) {
                if (!config_.difficulty) return {Kind::player, 0, "the difficulty dialog"};
                if (p.diff_phase != 1 || p.diff_step != 0) return wait;
                const int target = std::clamp(*config_.difficulty, 0, std::max(p.diff_count - 1, 0));
                if (p.diff_cursor < target) return press(pad_button::down);
                if (p.diff_cursor > target) return press(pad_button::up);
                return press(pad_button::a);
            }
            if (p.sub == 3 && p.detail == 1) return press(pad_button::start);
            return wait;
        default:
            return wait;
        }
    }
    // TITLE2: "press start", then Game Start (the menu's first entry).
    if (v.title_state == 1 && v.title_sub == 2 && v.title_step == 1 && v.title_detail == 0) return press(pad_button::start);
    if (v.title_state == 2 && v.title_sub == 1 && v.title_step == 1) return press(v.title_cursor == 0 ? pad_button::a : pad_button::up);
    return wait;
}

std::uint32_t TitleDriver::step(const TitleView& v) {
    if (!active()) return 0;
    ++steps_;
    // Done: the game started and the first stage after the title plays
    // (stagML11 after gameStart.lua).
    if (v.game_started && v.ready && v.stage.rfind("stagis", 0) != 0 && !v.stage.empty()) {
        finish(Phase::done, "the game started in " + v.stage);
        return 0;
    }
    if (steps_ > config_.give_up_steps) {
        finish(Phase::gave_up, "no lobby after " + std::to_string(config_.give_up_steps) + " steps");
        return 0;
    }
    const auto want = decide(v);
    if (want.kind == Want::Kind::hand_over) {
        finish(Phase::handed_over, want.why);
        return 0;
    }
    if (want.kind == Want::Kind::player) {
        if (phase_ != Phase::player) {
            phase_ = Phase::player;
            note_ = want.why;
            changed_ = true;
        }
        released_ = true;
        return 0;
    }
    if (phase_ == Phase::player) {
        phase_ = Phase::driving;
        note_ = "driving";
        changed_ = true;
    }
    if (want.kind != Want::Kind::press || !want.button) {
        released_ = true;
        return 0;
    }
    // One step down, at least one up (every press is a new trigger); the same
    // screen gets it again only after kRetrySteps without a change.
    if (!released_) {
        released_ = true;
        return 0;
    }
    const auto key = key_of(v);
    if (key == last_key_ && steps_ - last_press_ < kRetrySteps) return 0;
    last_key_ = key;
    last_press_ = steps_;
    released_ = false;
    ++presses_;
    return want.button;
}

const char* title_phase_name(TitleDriver::Phase phase) {
    switch (phase) {
    case TitleDriver::Phase::off: return "off";
    case TitleDriver::Phase::driving: return "driving";
    case TitleDriver::Phase::player: return "player";
    case TitleDriver::Phase::done: return "done";
    case TitleDriver::Phase::handed_over: return "handed over";
    case TitleDriver::Phase::gave_up: return "gave up";
    }
    return "?";
}

} // namespace dw4::game::online
