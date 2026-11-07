// make_run_driver: the RunDriver a run plays with (overflow_run.hpp), round
// O1a lane C: the host's side of the engine's OverflowDirector
// (dw4/engine/overflow_director.hpp).
//
// The driver gives the run's engine the run's rules with the director
// (attach, before the arena's request), turns this machine's pads into the
// run's moves (move only, SURVIVAL.md 10: the stick, A for a labelled item,
// L for the item panel; START pauses) or the test bots' pads, the menu keys
// into the director's commands (the level-up choice, the pause, Retire, the
// results), and draws O1a's plain overlay boxes (9.4-9.6: the game's window
// models come with O1b). Test runs (a bot) take the offers by a policy
// (offers=priority / random) and close the results by themselves; summary=
// and log= write the run's summary (T6) and its run log (13.5).
//
// The meta (round O1b lane F, overflow_profile.hpp): the store's profile
// adds the form's loadout, Plug Lab points and opened gear to the request
// (apply_meta: a test run's own options win), the driver hands them to the
// director's RunConfig (lane E's fields) and records the run in the meta
// file when its results open (Sim Bits, records, bests; a window that
// closes counts as Retire).
#include "overflow_profile.hpp"
#include "overflow_run.hpp"
#include "overflow_screens.hpp"
#include "sim_ui.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/engine/overflow.hpp"
#include "dw4/engine/overflow_director.hpp"
#include "dw4/pad.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>

namespace dw4::game::overflow {

namespace {

namespace ov = engine::overflow;

std::string clock_text(double seconds) {
    const int whole = int(seconds);
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", whole / 60, whole % 60);
    return text;
}

std::string option(const RunRequest& request, std::string_view key, std::string fallback = {}) {
    const auto found = request.options.find(key);
    return found == request.options.end() ? fallback : found->second;
}

// The meta's options in the director's RunConfig (lane E's fields of round
// O1b: the loadout and the gear the records opened). A build without a
// field copies nothing for it; an absent option keeps what the config has
// (lane E's test key loadout=). The Plug Lab's points are the host's
// (give_plug_lab): RunConfig::plug_lab stays at zeros, so they are given
// once whatever the director does with that field.
template <typename Config>
void copy_meta(Config& config, const RunRequest& request) {
    if constexpr (requires { config.loadout_armor; })
        if (const auto armor = option(request, "armor"); !armor.empty()) config.loadout_armor = armor;
    if constexpr (requires { config.loadout_chip; })
        if (const auto chip = option(request, "chip"); !chip.empty()) config.loadout_chip = chip;
    if constexpr (requires { config.gear_open; }) {
        const std::string text = option(request, "gear_open");
        for (std::size_t begin = 0; begin < text.size();) {
            const std::size_t end = std::min(text.find(',', begin), text.size());
            if (end > begin) config.gear_open.push_back(text.substr(begin, end - begin));
            begin = end + 1;
        }
    }
}
// The run's end for the meta file, from the director's getters (those lane
// E adds are read when this build has them).
template <typename Director>
void fill_report(const Director& director, RunReport& report) {
    using O = std::remove_cvref_t<decltype(director.outcome())>;
    const O outcome = director.outcome();
    report.outcome = outcome == O::cleared ? "cleared" : outcome == O::defeated ? "defeated" : "retired";
    if constexpr (requires { O::time_up; })
        if (outcome == O::time_up) report.outcome = "time_up";
    if constexpr (requires { director.guardians_defeated(); })
        for (const auto& family : director.guardians_defeated()) report.guardians.push_back(std::string(family));
    if constexpr (requires { director.awakened(); }) report.awakened = director.awakened();
    if constexpr (requires { director.casts(); }) report.casts = int(director.casts());
    if constexpr (requires { director.tech_defeats(); }) {
        const auto tech = director.tech_defeats();
        for (std::size_t e = 0; e < 4 && e < std::size(tech); ++e) report.tech_defeats[e] = int(tech[e]);
    }
    if constexpr (requires { director.crates(); }) report.crates = int(director.crates());
}

// "NAME:F,NAME:F" (the balance passes' factor lists).
std::vector<std::pair<std::string, float>> pairs(const std::string& text) {
    std::vector<std::pair<std::string, float>> out;
    for (std::size_t begin = 0; begin < text.size();) {
        const std::size_t end = std::min(text.find(',', begin), text.size());
        const std::string token = text.substr(begin, end - begin);
        if (const auto colon = token.find(':'); colon != std::string::npos) out.push_back({token.substr(0, colon), std::strtof(token.c_str() + colon + 1, nullptr)});
        begin = end + 1;
    }
    return out;
}

class DirectorDriver final : public RunDriver {
public:
    explicit DirectorDriver(RunRequest request) : request_(std::move(request)) {
        // The meta file's part of the run (8.2-8.4): the request's own values win.
        apply_meta(meta().profile(), request_);
        ov::RunConfig config;
        config.form = request_.form;
        config.field = request_.field;
        config.seed = request_.seed;
        config.bot = request_.bot;
        const std::string tier = option(request_, "tier", "normal");
        config.tier = tier == "hard" ? 1 : tier == "veryhard" ? 2 : 0;
        test_ = !request_.bot.empty();
        // Test runs take the offers by a policy (priority unless offers= says
        // otherwise; the human bot as a newcomer: the first line or a random one).
        config.offers = option(request_, "offers", test_ ? (request_.bot == "human" ? "newcomer" : "priority") : "");
        if (config.offers == "player") config.offers.clear();
        // (results_hold=STEPS, test runs with pictures: the results stay that long;
        // their lines appear one per 8 steps.)
        config.results_hold = test_ ? std::max(1, std::atoi(option(request_, "results_hold", "60").c_str())) : 0;
        // Development switches of the balance passes (initial values of SURVIVAL.md otherwise).
        auto real = [&](std::string_view key, float& value) {
            if (const auto text = option(request_, key); !text.empty()) value = std::strtof(text.c_str(), nullptr);
        };
        real("damage_per_minute", config.tuning.damage_per_minute);
        real("hp_per_minute", config.tuning.hp_per_minute);
        real("damage_growth", config.tuning.damage_growth);
        real("damage_late_minutes", config.tuning.damage_late_minutes);
        real("damage_growth_late", config.tuning.damage_growth_late);
        real("hp_late_minutes", config.tuning.hp_late_minutes);
        real("hp_growth_late", config.tuning.hp_growth_late);
        real("hp_growth", config.tuning.hp_growth);
        // (KEY=A,B,C,D,E: the knots at 0:00, 5:00, 10:00, 15:00, 20:00.)
        auto five = [&](std::string_view key, std::array<float, 5>& value) {
            if (const auto text = option(request_, key); !text.empty())
                std::sscanf(text.c_str(), "%f,%f,%f,%f,%f", &value[0], &value[1], &value[2], &value[3], &value[4]);
        };
        five("alive", config.tuning.alive_at);
        five("refill", config.tuning.refill_at);
        five("contact", config.tuning.contact_at);
        five("shot", config.tuning.shot_at);
        // (KEY=SECONDS: a run time.)
        auto seconds = [&](std::string_view key, int& value) {
            if (const auto text = option(request_, key); !text.empty()) value = int(std::lround(std::strtod(text.c_str(), nullptr) * 60.0));
        };
        seconds("surge_first", config.tuning.surge_first);
        seconds("keep_gap", config.tuning.keep_gap_until);
        seconds("corrupted_from", config.tuning.corrupted_from);
        seconds("phase_ramp", config.tuning.phase_ramp_steps);
        seconds("phase_intro_time", config.tuning.phase_intro_steps);
        // (The forge round of 05.11: phase 4's own entry, the run's end, a
        // test policy without the early Forge.)
        seconds("last_ramp", config.tuning.last_ramp_steps);
        seconds("last_intro_time", config.tuning.last_intro_steps);
        seconds("end", config.tuning.end_steps);
        seconds("forge_from", config.forge_from);
        if (const auto sure = option(request_, "forge_sure"); !sure.empty()) config.forge_sure = std::atoi(sure.c_str());
        if (const auto extra = option(request_, "surge_extra"); !extra.empty()) config.tuning.surge_extra = std::atoi(extra.c_str());
        real("gap_radius", config.tuning.gap_radius);
        real("damage_warm_minutes", config.tuning.damage_warm_minutes);
        real("ring_min", config.tuning.ring_min);
        real("ring_max", config.tuning.ring_max);
        real("slow_min", config.tuning.slow_min);
        real("slow_max", config.tuning.slow_max);
        real("corrupted_hp", config.tuning.corrupted_hp);
        real("exp", config.tuning.exp_factor);
        real("art_range", config.tuning.art_range);
        real("art_range_moving", config.tuning.art_range_moving);
        real("damage_warm_up", config.tuning.damage_warm_up);
        real("warm_up", config.tuning.warm_up);
        real("mp_regen", config.tuning.mp_regen);
        real("behind_far", config.tuning.behind_far);
        real("visible_min", config.tuning.visible_min);
        real("ahead_share", config.tuning.ahead_share);
        real("ahead_angle_min", config.tuning.ahead_angle_min);
        real("ahead_angle_max", config.tuning.ahead_angle_max);
        real("phase_intro", config.tuning.phase_intro);
        real("art_class", config.art_class);
        real("bot_flee", config.tuning.bot_flee);
        if (const auto gap = option(request_, "window_gap"); !gap.empty()) config.tuning.level_window_gap = std::atoi(gap.c_str());
        if (const auto muteki = option(request_, "muteki"); !muteki.empty()) config.tuning.muteki = std::atoi(muteki.c_str());
        // (family_damage=FAMILY:F,...: a family's own damage factor.)
        for (const auto& [key, value] : pairs(option(request_, "family_damage"))) config.family_damage[key] = value;
        // (technique_cooldown=ROW:STEPS,...: a technique's own cooldown by its pcTech base row; 0: never cast.)
        for (const auto& [key, value] : pairs(option(request_, "technique_cooldown"))) config.technique_cooldown[std::atoi(key.c_str())] = int(value);
        // (family_ring=FAMILY:R,...: a family's own spawn ring, 0-2.)
        for (const auto& [key, value] : pairs(option(request_, "family_ring"))) config.family_ring[key] = int(value);
        // Round O1b lane E: the Guardians and the finale (guardians=0: O1a's
        // run), the spike's keys (guardian_set=F1,F2,F3 forces the phases'
        // Guardians, guardian_first=SECONDS brings the first one sooner,
        // elite=F,... makes those Guardians the elite fallback) and the
        // Guardians' HP factors.
        if (const auto on = option(request_, "guardians"); !on.empty()) config.tuning.guardians = on != "0";
        seconds("guardian_first", config.tuning.guardian_first);
        seconds("guardian_half", config.tuning.guardian_half_steps);
        // (return_gate=0: the results 4 s after the clear on the Violet Ring too;
        // forge_start=N: Forge families open at the start, tests and pictures.)
        if (const auto gate = option(request_, "return_gate"); !gate.empty()) config.tuning.return_gate = gate != "0";
        if (const auto forge = option(request_, "forge_start"); !forge.empty()) config.forge_start = std::atoi(forge.c_str());
        // (art_skill=N: the Art's class skill at the start; phase=SECONDS: a phase's
        // length, the finale at four of them: pictures of the finale and the gate.)
        if (const auto skill = option(request_, "art_skill"); !skill.empty()) config.art_skill_start = std::atoi(skill.c_str());
        seconds("phase", config.tuning.phase_steps);
        // (pwr_up_late=SECONDS,FROM_SECONDS: Pwr Up's cooldown from that run time on.)
        if (const auto late = option(request_, "pwr_up_late"); !late.empty()) {
            double cooldown = 0, from = 0;
            if (std::sscanf(late.c_str(), "%lf,%lf", &cooldown, &from) == 2) {
                config.tuning.pwr_up_late_cooldown = int(std::lround(cooldown * 60.0));
                config.tuning.pwr_up_late_from = int(std::lround(from * 60.0));
            }
        }
        real("guardian_hp", config.tuning.guardian_hp);
        real("finale_hp", config.tuning.finale_hp);
        auto list = [](const std::string& text) {
            std::vector<std::string> out;
            for (std::size_t begin = 0; begin < text.size();) {
                const std::size_t end = std::min(text.find(',', begin), text.size());
                if (end > begin) out.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            return out;
        };
        {
            const auto set = list(option(request_, "guardian_set"));
            for (std::size_t i = 0; i < set.size() && i < 3; ++i) config.guardians[i] = set[i];
        }
        config.elite = list(option(request_, "elite"));
        // The loadout and the opened gear (lanes D and F set them; tests: loadout=ARMOR,CHIP).
        // (positional: "loadout=,wisdom_chip" keeps the empty armor field; "-" means empty too)
        if (const std::string text = option(request_, "loadout"); !text.empty()) {
            const std::size_t comma = text.find(',');
            const std::string armor = text.substr(0, comma);
            const std::string chip = comma == std::string::npos ? std::string() : text.substr(comma + 1);
            config.loadout_armor = armor == "-" ? std::string() : armor;
            config.loadout_chip = chip == "-" ? std::string() : chip;
        }
        timing_ = option(request_, "timing") == "1";
        // commands=STEP KIND[VALUE],... (test runs with pictures): the menu keys
        // at given director steps (frozen ones count), e.g. 600p,640m1,660a: pause, the cursor down, A.
        const std::string script = option(request_, "commands");
        for (std::size_t begin = 0; begin < script.size();) {
            const std::size_t end = std::min(script.find(',', begin), script.size());
            const std::string token = script.substr(begin, end - begin);
            std::size_t digits = 0;
            while (digits < token.size() && std::isdigit(static_cast<unsigned char>(token[digits]))) ++digits;
            if (digits > 0 && digits < token.size())
                script_.push_back({std::stoull(token.substr(0, digits)), token[digits], digits + 1 < token.size() ? std::atoi(token.c_str() + digits + 1) : 0});
            begin = end + 1;
        }
        copy_meta(config, request_);
        tier_ = config.tier;
        bits_random_ = bits_seed(request_.seed);
        director_ = std::make_unique<ov::OverflowDirector>(std::move(config));
    }

    void attach(engine::Engine& run, const RunRequest&) override {
        rules_ = std::make_shared<ov::OverflowRules>(ov::RuleSet::run);
        director_->attach(run, *rules_);
        // (curve=B,L,S: the balance passes' NEXTEXP terms.)
        if (const auto curve = option(request_, "curve"); !curve.empty())
            std::sscanf(curve.c_str(), "%d,%d,%d", &rules_->levels.curve_base, &rules_->levels.curve_linear, &rules_->levels.curve_square);
        // (buff=STEPS,RANK_STEPS: the force buffs' time in a run; buff_kept=ROW,...: those keeping DW4's time.)
        if (const auto buff = option(request_, "buff"); !buff.empty())
            std::sscanf(buff.c_str(), "%d,%d", &rules_->buff_steps, &rules_->buff_rank_steps);
        {
            const std::string rows = option(request_, "buff_kept");
            for (std::size_t begin = 0; begin < rows.size();) {
                const std::size_t end = std::min(rows.find(',', begin), rows.size());
                if (end > begin) rules_->buff_kept.insert(std::atoi(rows.substr(begin, end - begin).c_str()));
                begin = end + 1;
            }
        }
        // (technique_damage=FAMILY:F,...: a pcTech family's damage factor.)
        for (const auto& [key, value] : pairs(option(request_, "technique_damage"))) rules_->damage.technique_factor[std::atoi(key.c_str())] = value;
        run.set_overflow_rules(rules_);
    }

    void before_step(engine::Engine& run, std::array<engine::PadSample, 4>& pads) override {
        if (timing_) step_started_ = std::chrono::steady_clock::now();
        if (director_->screen() == ov::Screen::results) {
            if (!results_step_) results_step_ = director_->steps();
        } else {
            results_step_.reset();
        }
        const engine::PadSample live = pads[0];
        const std::uint32_t trigger = live.buttons & ~last_buttons_;
        last_buttons_ = live.buttons;
        // The stick as up / down presses for the menus (an edge past half way).
        const int stick = live.left.y > 0.5f ? -1 : live.left.y < -0.5f ? 1 : 0;
        const int stick_x = live.left.x > 0.5f ? 1 : live.left.x < -0.5f ? -1 : 0;
        const int moved = stick != last_stick_ ? stick : 0;
        const int moved_x = stick_x != last_stick_x_ ? stick_x : 0;
        last_stick_ = stick;
        last_stick_x_ = stick_x;
        if (!test_) {
            const auto screen = director_->screen();
            if (screen != ov::Screen::none) {
                if ((trigger & pad_button::up) || moved < 0) director_->command_move(-1);
                if ((trigger & pad_button::down) || moved > 0) director_->command_move(1);
                if (screen == ov::Screen::retire_ask && ((trigger & (pad_button::left | pad_button::right)) || moved_x != 0))
                    director_->command_move(1);
                // (The results take A only once every line is there, at least
                // after 1 s: an A of the fight must not skip them.)
                if ((trigger & pad_button::a) && (screen != ov::Screen::results || results_ready(run))) director_->command_accept();
                if (trigger & pad_button::b) director_->command_back();
                if ((trigger & pad_button::start) && screen != ov::Screen::level_up && screen != ov::Screen::results) director_->command_pause();
            } else if (trigger & pad_button::start) {
                director_->command_pause();
            }
        }
        for (const auto& command : script_) {
            if (command.step != director_->steps() || command.done) continue;
            const_cast<ScriptCommand&>(command).done = true;
            switch (command.kind) {
            case 'p': director_->command_pause(); break;
            case 'm': director_->command_move(command.value); break;
            case 'a': director_->command_accept(); break;
            case 'b': director_->command_back(); break;
            default: break;
            }
        }
        engine::PadSample pad;
        if (request_.bot == "kite") {
            pad = director_->bot_pad(run);
        } else if (request_.bot == "human") {
            // (The balance round of 05.11: a newcomer, the director's human_pad.)
            pad = director_->human_pad(run);
        } else if (request_.bot == "circle") {
            // (Lane A's isolation test, T3: a slow circle at full stick; the
            // movement check of 14.2: a player who never stops walking.)
            ++circle_;
            const float angle = float(circle_) * 0.01f;
            pad = director_->filter_pad(run, host_pad_sample({0, 0, 0, {std::sin(angle), std::cos(angle)}, {}}));
        } else if (request_.bot == "stand") {
            // (The movement check of 14.2: a player who never walks.)
            pad = director_->filter_pad(run, host_pad_sample({}));
        } else {
            pad = director_->filter_pad(run, live);
        }
        director_->record_pad(pad);
        pads[0] = pad;
    }

    std::optional<RunEnd> after_step(engine::Engine& run) override {
        if (timing_ && director_->run_steps() > 0 && !run.loading()) {
            // (T8: the run's steps, the stage load left out.)
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - step_started_).count();
            step_ms_.push_back(float(ms));
            // (The five slowest steps with their run step, for the log.)
            slowest_.push_back({float(ms), director_->run_steps()});
            std::sort(slowest_.begin(), slowest_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            if (slowest_.size() > 5) slowest_.pop_back();
        }
        give_plug_lab(run);
        count_bits(run);
        // The results open: the meta file learns the run (8.5).
        if (director_->screen() == ov::Screen::results) record_meta(run);
        if (end_) return end_;
        if (director_->over()) {
            switch (director_->outcome()) {
            case ov::Outcome::defeated: end_ = RunEnd::defeated; break;
            case ov::Outcome::cleared: end_ = RunEnd::cleared; break;
            case ov::Outcome::time_up: end_ = RunEnd::time_up; break;
            default: end_ = asked_.value_or(RunEnd::retired); break;
            }
            finish(run);
        }
        return end_;
    }

    void retire(engine::Engine& run, RunEnd why) override {
        if (end_) return;
        if (why == RunEnd::window_closed) {
            // The window closes: the run counts as retired at once.
            end_ = why;
            finish(run);
            return;
        }
        asked_ = why;
        director_->command_retire();
    }

    std::optional<RunEnd> ended() const override { return end_; }

    // The run's screens in the game's look (round O1b lane D,
    // overflow_screens.hpp): SimUi's 2D items after the run's picture.
    void decorate(engine::Engine& run, std::vector<engine::DrawItem>& items) override {
        ui_.set_resources(run.resources());
        if (!ui_.ready()) return;
        fill_screen_data(run);
        ui_.begin();
        draw_run_screens(ui_, data_, director_->steps());
        ui_.finish(items);
    }

    void draw_overlay(engine::Engine& run, const OverlayFrame& frame) override {
        // (O1a's plain boxes stay as the fallback without the game font.)
        if (ui_.ready()) return;
        // (The boxes grow with the picture: about 28 px on a 960-line screen.)
        const float size = std::max(18.0f, float(frame.screen_height) / 34.0f);
        if (frame.font) ImGui::PushFont(static_cast<ImFont*>(frame.font), size);
        draw_hud(run, frame);
        switch (director_->screen()) {
        case ov::Screen::level_up: draw_level_up(run, frame); break;
        case ov::Screen::pause: draw_list(frame, "PAUSE", {"Resume", "Build", "Retire"}, director_->cursor(), "A Choose    B Resume"); break;
        case ov::Screen::build: draw_lines(frame, "BUILD", director_->build_lines(run), "A / B Back"); break;
        case ov::Screen::retire_ask: draw_list(frame, "End the simulation?", {"Yes", "No"}, director_->cursor(), "A Choose    B Back"); break;
        case ov::Screen::results: {
            auto lines = director_->result_lines(run);
            for (auto& line : meta_lines()) lines.push_back(std::move(line));
            draw_lines(frame, director_->outcome() == ov::Outcome::cleared ? "SIMULATION COMPLETE" : "SIMULATION ENDED", lines,
                       "A: Return to the story.");
            break;
        }
        case ov::Screen::none: break;
        }
        if (frame.font) ImGui::PopFont();
    }

    void draw_menu(engine::Engine& run) override {
        ImGui::Separator();
        ImGui::Text("Phase %d   enemies %d   defeated %d   Art %s", director_->phase(), director_->alive(), director_->defeats(),
                    director_->art_name().c_str());
        for (const auto& line : director_->build_lines(run)) ImGui::TextUnformatted(line.c_str());
    }

    RunStatus status(const engine::Engine& run) const override {
        RunStatus status;
        status.form_name = form_display_name(request_.form);
        status.level = director_->level(run);
        status.seconds = director_->run_seconds();
        status.defeated_by = director_->defeated_by();
        return status;
    }

private:
    // The results are open since this director step (the A of the results waits, results_wait).
    std::optional<std::uint64_t> results_step_;
    bool results_ready(engine::Engine& run) const {
        if (!results_step_) return true;
        auto lines = director_->result_lines(run);
        for (auto& line : meta_lines()) lines.push_back(std::move(line));
        return director_->steps() - *results_step_ >= results_wait(fit_result_lines(lines, 32).size());
    }
    // The screens' data from the director (lane D's screens; lane E's finale,
    // lane F's meta last; o1b contract D4, F 17:40).
    std::uint64_t screen_step_ = 0; // the director step the open screen opened at (the results' pacing)
    std::optional<std::uint64_t> cleared_step_; // the director step the clear was first drawn at (the banner)
    void fill_screen_data(engine::Engine& run) {
        auto& d = data_;
        d.seconds = director_->run_seconds();
        d.level = director_->level(run);
        d.defeats = director_->defeats();
        d.exp_fraction = 0;
        if (const auto* sys = run.get<engine::GameSystem>()) {
            // (NEXTEXP is cumulative: the bar runs inside the level, 9.3.)
            const auto& stats = sys->players[0].stats;
            const int floor = rules_ && stats.level > 1 ? rules_->next_exp(stats.level - 1) : 0;
            if (stats.next_exp > 0) d.exp_fraction = level_progress(stats.exp, floor, stats.next_exp);
        }
        build_row(director_->build_lines(run), d);
        d.notice = !director_->notice().empty() && director_->notice_step() + 180 > director_->steps() ? director_->notice() : std::string();
        // The finale (4.4, lane E): LordKnightmon is in and the quest timer
        // counts down (1 step = 1001/60000 s; it stops with the run's clock
        // at the clear); TIME UP at its 0:00.
        d.finale = director_->finale();
        d.finale_left = double(director_->finale_left_steps()) * 1001.0 / 60000.0;
        d.time_up = director_->time_up();
        // (hud_finale=SECONDS, test runs with pictures: the finale timer's
        // look at that many seconds left; presentation only.)
        if (const auto left = option(request_, "hud_finale"); !left.empty()) {
            d.finale = true;
            d.finale_left = std::strtod(left.c_str(), nullptr);
        }
        const auto screen = director_->screen();
        // (Director steps, not presented pictures: the results' lines come
        // at the same pace at any refresh rate.)
        if (int(screen) != int(d.screen)) screen_step_ = director_->steps();
        d.screen_frames = director_->steps() - screen_step_;
        switch (screen) {
        case ov::Screen::none: d.screen = RunScreen::none; break;
        case ov::Screen::level_up: d.screen = RunScreen::level_up; break;
        case ov::Screen::pause: d.screen = RunScreen::pause; break;
        case ov::Screen::build: d.screen = RunScreen::build; break;
        case ov::Screen::retire_ask: d.screen = RunScreen::retire_ask; break;
        case ov::Screen::results: d.screen = RunScreen::results; break;
        }
        d.cursor = director_->cursor();
        d.offers.clear();
        if (screen == ov::Screen::level_up)
            for (const auto& offer : director_->offers())
                d.offers.push_back({director_->offer_text(run, offer), director_->offer_detail(run, offer), director_->offer_about(run, offer)});
        d.build_lines = screen == ov::Screen::build ? director_->build_lines(run) : std::vector<std::string>{};
        d.result_lines = screen == ov::Screen::results ? director_->result_lines(run) : std::vector<std::string>{};
        d.cleared = director_->outcome() == ov::Outcome::cleared;
        // The clear banner (4.4: where the original shows i_bgclear; lane E
        // sends none): the steps since LordKnightmon's fall was first drawn.
        if (d.cleared && !cleared_step_) cleared_step_ = director_->steps();
        d.clear_frames = cleared_step_ ? director_->steps() - *cleared_step_ : 0;
        // The meta's part (lane F), last: the HUD's Bits; on the results the
        // run's Bits, the Sim Bits, the NEW! records and a save problem.
        fill_meta_screen(d, run);
    }

    // The run's summary and log (T6), once.
    void finish(engine::Engine& run) {
        if (finished_) return;
        finished_ = true;
        record_meta(run);
        const std::string summary = director_->summary_text(run);
        if (const auto path = option(request_, "summary"); !path.empty()) {
            std::ofstream out(path, std::ios::binary);
            out << summary;
            if (timing_) out << timing_text();
            // The meta state the run used (13.5) and what it earned.
            out << "meta_used" << (meta_used().empty() ? std::string(" none") : meta_used()) << '\n';
            out << "meta_bits " << reward_.sim_bits << " sim_bits " << meta().profile().sim_bits << " new_records " << reward_.new_records.size() << '\n';
        }
        if (const auto path = option(request_, "log"); !path.empty()) {
            std::ofstream out(path, std::ios::binary);
            out << director_->log_text();
            // The meta state the run used (13.5: loadout, Plug Lab points by
            // UPCNT index, opened gear) as request options: the run again is
            // `--overflow-run <the run's keys> <these>` without a profile.
            out << "meta_used" << (meta_used().empty() ? std::string(" none") : meta_used()) << '\n';
        }
        if (test_ || timing_) {
            std::printf("overflow: %s", summary.substr(0, summary.find("\nnote ")).c_str());
            std::printf("\n%s", timing_text().c_str());
        }
    }

    // The run into the meta file, once (the results open, or the run ends
    // without them: the window closed).
    void record_meta(engine::Engine& run) {
        if (recorded_) return;
        recorded_ = true;
        RunReport report;
        report.form = request_.form;
        report.tier = tier_;
        report.seconds = director_->run_seconds();
        report.level = director_->level(run);
        report.defeats = director_->defeats();
        fill_report(*director_, report);
        if (end_ == RunEnd::window_closed) report.outcome = "retired";
        report.bits = run_bits(run);
        recorded_bits_ = report.bits;
        reward_ = meta().record_run(report);
        if (test_ || timing_)
            std::printf("overflow: meta %s bits %lld -> Sim Bits +%lld (balance %lld), %zu new records%s%s; used%s\n", report.outcome.c_str(),
                        (long long)report.bits, (long long)reward_.sim_bits, (long long)meta().profile().sim_bits, reward_.new_records.size(),
                        meta().path().empty() ? "" : meta().last_error().empty() ? ", written to " : ", NOT written to ", meta().path().empty() ? "" : meta().path().string().c_str(),
                        meta_used().empty() ? " none" : meta_used().c_str());
        if (!meta().last_error().empty()) std::fprintf(stderr, "overflow: the meta file was not written: %s\n", meta().last_error().c_str());
    }

public:
    // The meta's part of lane D's run screens (RunScreenData of
    // overflow_screens.hpp; a template, so this lane builds without D):
    // the HUD's Bits (9.3) and, on the results, the Sim Bits, the NEW!
    // records and a save problem (9.6). D's fill_screen_data calls it last
    // (lane F's merge adds that call).
    template <typename ScreenData>
    void fill_meta_screen(ScreenData& d, const engine::Engine& run) const {
        if constexpr (requires { d.bits; })
            d.bits = int(std::clamp<std::int64_t>(run_bits(run), 0, 2'147'483'647));
        if constexpr (requires { d.result_lines.push_back(std::string()); })
            if (director_->screen() == ov::Screen::results)
                for (auto& line : meta_lines()) d.result_lines.push_back(std::move(line));
    }

private:

    // " armor=K chip=K plug_lab=... gear_open=..." (the request's meta options).
    std::string meta_used() const {
        std::string text;
        for (const char* key : {"armor", "chip", "plug_lab", "gear_open"})
            if (const auto value = option(request_, key); !value.empty()) text += std::string(" ") + key + "=" + value;
        return text;
    }

    // The meta's lines under the results (9.6: the Sim Bits and the new Data
    // Records with "NEW!").
    std::vector<std::string> meta_lines() const {
        std::vector<std::string> lines;
        if (!recorded_) return lines;
        // (The run's bits as recorded: the pickups and the credited defeats, 4.7.)
        lines.push_back("Bits " + std::to_string(recorded_bits_));
        lines.push_back("Sim Bits +" + std::to_string(reward_.sim_bits) + "   (" + std::to_string(meta().profile().sim_bits) + ")");
        // (Only an unlock this build has is named: the records page marks the
        // others "coming later", O2-O4.)
        for (const auto& key : reward_.new_records)
            if (const auto* record = find_record(key))
                lines.push_back(std::string("NEW! ") + record->title + (record_built(*record) ? std::string("   opens ") + record->opens : std::string()));
        // The player learns when the profile is not saved (locked at the
        // start, a failed write): the progress lives only until the game ends.
        if (const auto problem = meta().save_problem(); !problem.empty()) lines.push_back(problem);
        return lines;
    }

    // The bits of the run's defeats (4.7, defeat_bits): credited at the
    // defeat, the run's engine untouched.
    void count_bits(engine::Engine& run) {
        const auto* sys = run.get<engine::GameSystem>();
        if (!sys) return;
        const auto& stats = sys->players[0].stats;
        while (counted_defeats_ < director_->defeats()) {
            ++counted_defeats_;
            credited_bits_ += defeat_bits(stats.level, stats.bit_rate, bits_random_);
        }
    }

public:
    // The run's bits so far: the run player's BIT (pickups) and the
    // credited defeats' (the HUD's Bits, 9.3).
    std::int64_t run_bits(const engine::Engine& run) const {
        const auto* sys = run.get<engine::GameSystem>();
        return credited_bits_ + (sys ? sys->players[0].bits : 0);
    }

private:
    // The Plug Lab's points (8.2) on the run player once the run has started
    // (the host owns them; RunConfig::plug_lab stays at zeros): UPCNT,
    // RECALCPLAYER, full HP and MP, the species' store.
    void give_plug_lab(engine::Engine& run) {
        if (plug_given_ || director_->run_steps() == 0) return;
        plug_given_ = true;
        const auto points = parse_plug_lab(option(request_, "plug_lab"));
        if (std::all_of(points.begin(), points.end(), [](int p) { return p == 0; })) return;
        auto* sys = run.get<engine::GameSystem>();
        if (!sys) return;
        auto& player = sys->players[0];
        for (std::size_t i = 0; i < points.size(); ++i) player.upcnt[i] += points[i];
        sys->recalc_player(run, 0);
        player.hp = player.max_hp;
        player.mp = player.max_mp;
        sys->store_species(0);
    }

    std::string timing_text() const {
        if (step_ms_.empty()) return {};
        auto sorted = step_ms_;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (const float v : sorted) sum += v;
        char line[160];
        std::snprintf(line, sizeof line, "step_ms mean %.3f p95 %.3f max %.3f (%zu steps); slowest", sum / double(sorted.size()),
                      double(sorted[sorted.size() * 95 / 100]), double(sorted.back()), sorted.size());
        std::string text = line;
        for (const auto& [ms, step] : slowest_) {
            std::snprintf(line, sizeof line, " %.2f@%llu", double(ms), (unsigned long long)step);
            text += line;
        }
        return text + "\n";
    }

    // O1a's HUD line: the run clock, the level, the defeats; the last visible
    // change (a weapon rung, a pickup) for three seconds.
    void draw_hud(engine::Engine& run, const OverlayFrame& frame) {
        const float x = float(frame.screen_x) + float(frame.screen_width) * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(x, float(frame.screen_y) + 6.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##overflow_hud", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("%s   Lv %d   Defeated %d   Bits %lld", clock_text(director_->run_seconds()).c_str(), director_->level(run), director_->defeats(),
                    (long long)run_bits(run));
        if (!director_->notice().empty() && director_->notice_step() + 180 > director_->steps())
            ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.47f, 1.0f), "%s", director_->notice().c_str());
        ImGui::End();
    }

    static void begin_box(const OverlayFrame& frame, const char* id, float width) {
        const float x = float(frame.screen_x) + float(frame.screen_width) * 0.5f;
        const float y = float(frame.screen_y) + float(frame.screen_height) * 0.45f;
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, float(frame.screen_height)));
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    }

    void draw_level_up(engine::Engine& run, const OverlayFrame& frame) {
        begin_box(frame, "##overflow_levelup", float(frame.screen_width) * 0.86f);
        ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.47f, 1.0f), "LEVEL UP!   Lv %d", director_->level(run));
        ImGui::Separator();
        const auto& offers = director_->offers();
        for (std::size_t i = 0; i < offers.size(); ++i) {
            const bool on = int(i) == director_->cursor();
            const std::string text = director_->offer_text(run, offers[i]);
            ImGui::TextColored(on ? ImVec4(1, 1, 1, 1) : ImVec4(0.72f, 0.72f, 0.72f, 1), "%s %s", on ? ">" : " ", text.c_str());
            ImGui::SameLine(float(frame.screen_width) * 0.36f);
            ImGui::TextColored(on ? ImVec4(0.6f, 0.9f, 1.0f, 1) : ImVec4(0.5f, 0.6f, 0.7f, 1), "%s", director_->offer_detail(run, offers[i]).c_str());
        }
        ImGui::Separator();
        ImGui::TextDisabled("A Take");
        if (const int cursor = director_->cursor(); cursor >= 0 && cursor < int(offers.size())) {
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1), "%s", director_->offer_about(run, offers[std::size_t(cursor)]).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
    }

    static void draw_list(const OverlayFrame& frame, const char* title, std::initializer_list<const char*> lines, int cursor, const char* help) {
        begin_box(frame, "##overflow_list", float(frame.screen_width) * 0.42f);
        ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.47f, 1.0f), "%s", title);
        ImGui::Separator();
        int i = 0;
        for (const char* line : lines) {
            const bool on = i++ == cursor;
            ImGui::TextColored(on ? ImVec4(1, 1, 1, 1) : ImVec4(0.72f, 0.72f, 0.72f, 1), "%s %s", on ? ">" : " ", line);
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", help);
        ImGui::End();
    }

    static void draw_lines(const OverlayFrame& frame, const char* title, const std::vector<std::string>& lines, const char* help) {
        begin_box(frame, "##overflow_lines", float(frame.screen_width) * 0.7f);
        ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.47f, 1.0f), "%s", title);
        ImGui::Separator();
        for (const auto& line : lines) ImGui::TextUnformatted(line.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("%s", help);
        ImGui::End();
    }

    SimUi ui_;
    RunScreenData data_;
    RunRequest request_;
    std::unique_ptr<ov::OverflowDirector> director_;
    std::shared_ptr<ov::OverflowRules> rules_;
    std::optional<RunEnd> end_, asked_;
    bool test_ = false, finished_ = false, timing_ = false;
    bool recorded_ = false, plug_given_ = false;
    int tier_ = 0;
    RunReward reward_;
    std::uint64_t bits_random_ = 0;
    int counted_defeats_ = 0;
    std::int64_t credited_bits_ = 0;
    std::int64_t recorded_bits_ = 0; // the run bits the meta file was given (the results' Bits line)
    std::uint32_t last_buttons_ = 0;
    int last_stick_ = 0, last_stick_x_ = 0;
    std::uint64_t circle_ = 0;
    std::chrono::steady_clock::time_point step_started_{};
    struct ScriptCommand {
        std::uint64_t step;
        char kind;
        int value;
        bool done = false;
    };
    std::vector<ScriptCommand> script_;
    std::vector<float> step_ms_;
    std::vector<std::pair<float, std::uint64_t>> slowest_;
};

} // namespace

std::unique_ptr<RunDriver> make_run_driver(const RunRequest& request) { return std::make_unique<DirectorDriver>(request); }

} // namespace dw4::game::overflow
