// make_run_driver: the RunDriver a run plays with (overflow_run.hpp).
//
// Round O1a lane A has no director yet, so this is the host's stand-in:
// the run's engine plays its field as DW4 loads it, player 1 walks with
// this machine's controllers (or bot=circle in test runs), and the run ends
// by Retire (F1 > Data Overflow, a test run's frame limit), the window
// closing, or when the engine leaves its field (a game over's capsule or a
// soft reset, which lane B's hooks keep out of the director's runs). Lane C
// replaces this file's make_run_driver with its director's driver.

#include "overflow_run.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/pad.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace dw4::game::overflow {

namespace {

class HostStandIn final : public RunDriver {
public:
    explicit HostStandIn(RunRequest request) : request_(std::move(request)) {}

    void attach(engine::Engine&, const RunRequest&) override {}

    void before_step(engine::Engine&, std::array<engine::PadSample, 4>& pads) override {
        ++steps_;
        // bot=circle (test runs): player 1 walks a slow circle.
        if (request_.bot == "circle") {
            const float angle = float(steps_) * 0.01f;
            pads[0] = host_pad_sample({0, 0, 0, {std::sin(angle), std::cos(angle)}, {}});
        }
    }

    std::optional<RunEnd> after_step(engine::Engine& run) override {
        if (end_) return end_;
        if (!run.loading() && run.stage_name() == request_.field) arrived_ = true;
        if (arrived_ && (run.stage_name() != request_.field || run.pending_stage())) end_ = RunEnd::defeated;
        // A defeated player ends the run three seconds later (before DW4's
        // bit capsule takes it out of the field).
        const auto* sys = run.get<engine::GameSystem>();
        const bool down = arrived_ && sys && sys->players[0].stats.ready && sys->players[0].hp <= 0;
        down_steps_ = down ? down_steps_ + 1 : 0;
        if (down_steps_ >= 180) end_ = RunEnd::defeated;
        return end_;
    }

    void retire(engine::Engine&, RunEnd why) override {
        if (!end_) end_ = why;
    }

    std::optional<RunEnd> ended() const override { return end_; }

    void draw_overlay(engine::Engine& run, const OverlayFrame& frame) override {
        const auto now = status(run);
        const int whole = int(now.seconds);
        ImGui::SetNextWindowPos(ImVec2(float(frame.width) * 0.5f, 8.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("##overflow", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("DATA OVERFLOW (test)   %s  Lv %d   %02d:%02d", now.form_name.c_str(), now.level, whole / 60, whole % 60);
        ImGui::TextDisabled("F1 > Data Overflow > Retire returns to the story.");
        ImGui::End();
    }

    RunStatus status(const engine::Engine& run) const override {
        RunStatus status;
        status.form_name = form_display_name(request_.form);
        if (const auto* sys = run.get<engine::GameSystem>(); sys && sys->players[0].stats.ready) status.level = sys->players[0].stats.level;
        status.seconds = double(steps_) * engine::kFieldSeconds;
        return status;
    }

private:
    RunRequest request_;
    std::uint64_t steps_ = 0;
    int down_steps_ = 0;
    bool arrived_ = false;
    std::optional<RunEnd> end_;
};

} // namespace

std::unique_ptr<RunDriver> make_run_driver(const RunRequest& request) { return std::make_unique<HostStandIn>(request); }

} // namespace dw4::game::overflow
