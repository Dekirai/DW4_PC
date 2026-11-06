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
// O1b: the loadout, the Plug Lab's UPCNT points by UPCNT index, the gear the
// records opened). A build without a field copies nothing for it.
template <typename Config>
void copy_meta(Config& config, const RunRequest& request) {
    if constexpr (requires { config.plug_lab; }) config.plug_lab = parse_plug_lab(option(request, "plug_lab"));
    if constexpr (requires { config.loadout_armor; }) config.loadout_armor = option(request, "armor");
    if constexpr (requires { config.loadout_chip; }) config.loadout_chip = option(request, "chip");
    if constexpr (requires { config.gear_open; }) {
        const std::string text = option(request, "gear_open");
        for (std::size_t begin = 0; begin < text.size();) {
            const std::size_t end = std::min(text.find(',', begin), text.size());
            if (end > begin) config.gear_open.push_back(text.substr(begin, end - begin));
            begin = end + 1;
        }
    }
}
// Whether the host gives the run player its Plug Lab points (no director
// field for them in this build).
template <typename Config>
constexpr bool host_plug_lab() {
    return !requires(Config config) { config.plug_lab; };
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
        config.results_hold = test_ ? 60 : 0;
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
        // (family_ring=FAMILY:R,...: a family's own spawn ring, 0-2.)
        for (const auto& [key, value] : pairs(option(request_, "family_ring"))) config.family_ring[key] = int(value);
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
        // (technique_damage=FAMILY:F,...: a pcTech family's damage factor.)
        for (const auto& [key, value] : pairs(option(request_, "technique_damage"))) rules_->damage.technique_factor[std::atoi(key.c_str())] = value;
        run.set_overflow_rules(rules_);
    }

    void before_step(engine::Engine& run, std::array<engine::PadSample, 4>& pads) override {
        if (timing_) step_started_ = std::chrono::steady_clock::now();
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
                if (trigger & pad_button::a) director_->command_accept();
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
        if constexpr (host_plug_lab<ov::RunConfig>()) give_plug_lab(run);
        count_bits(run);
        // The results open: the meta file learns the run (8.5).
        if (director_->screen() == ov::Screen::results) record_meta(run);
        if (end_) return end_;
        if (director_->over()) {
            switch (director_->outcome()) {
            case ov::Outcome::defeated: end_ = RunEnd::defeated; break;
            case ov::Outcome::cleared: end_ = RunEnd::cleared; break;
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

    void draw_overlay(engine::Engine& run, const OverlayFrame& frame) override {
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
        return status;
    }

private:
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
        }
        if (const auto path = option(request_, "log"); !path.empty()) {
            std::ofstream out(path, std::ios::binary);
            out << director_->log_text();
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
        reward_ = meta().record_run(report);
        if (test_ || timing_)
            std::printf("overflow: meta %s bits %lld -> Sim Bits +%lld (balance %lld), %zu new records%s%s\n", report.outcome.c_str(),
                        (long long)report.bits, (long long)reward_.sim_bits, (long long)meta().profile().sim_bits, reward_.new_records.size(),
                        meta().path().empty() ? "" : ", written to ", meta().path().empty() ? "" : meta().path().string().c_str());
        if (!meta().last_error().empty()) std::fprintf(stderr, "overflow: the meta file was not written: %s\n", meta().last_error().c_str());
    }

    // The meta's lines under the results (9.6: the Sim Bits and the new Data
    // Records with "NEW!").
    std::vector<std::string> meta_lines() const {
        std::vector<std::string> lines;
        if (!recorded_) return lines;
        lines.push_back("Sim Bits +" + std::to_string(reward_.sim_bits) + "   (" + std::to_string(meta().profile().sim_bits) + ")");
        for (const auto& key : reward_.new_records)
            if (const auto* record = find_record(key)) lines.push_back(std::string("NEW! ") + record->title + "   opens " + record->opens);
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
    // (a build whose director does not take RunConfig::plug_lab): UPCNT,
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
