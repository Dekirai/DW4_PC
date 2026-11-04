#include "overflow_host.hpp"

#include "discord_presence.hpp"
#include "presence_data.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>

namespace dw4::game::overflow {

namespace {

// The pictures start over at both switches (13.2): the renderer's texture
// cache is keyed by document addresses, the interpolator and the name tags
// hold the other engine's nodes, a film decoder its movie; the playback
// queue still holds the other engine's sound.
void restart_pictures(HostView& view) {
    view.picture.reset();
    if (view.renderer) {
        view.renderer->clear_cache();
        view.renderer->forget_kept_picture();
    }
    view.interpolator.reset();
    view.labels.reset();
    if (view.audio) SDL_ClearAudioStream(view.audio);
}

void draw_black(float alpha) {
    if (alpha <= 0.0f) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const int a = std::clamp(int(alpha * 255.0f + 0.5f), 0, 255);
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, a));
}

std::string clock_text(double seconds) {
    const int whole = int(seconds);
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", whole / 60, whole % 60);
    return text;
}

std::int64_t unix_now() {
    return std::int64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

// The F1 menu over a run (SURVIVAL.md 10): the run's tab with Retire and the
// settings sections; no save states, cheats, saves or online screens. The run
// is paused while it is open.
void draw_run_menu(HostView& view, engine::Engine& run, RunDriver& driver, const RunHost& host) {
    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("DW4 PC (F1)");
    if (ImGui::BeginTabBar("menu")) {
        if (ImGui::BeginTabItem("Data Overflow")) {
            const auto status = driver.status(run);
            ImGui::Text("%s  Lv %d  %s", status.form_name.c_str(), status.level, clock_text(status.seconds).c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("The run waits while this menu is open. Save states, cheats, the saves and the online screens "
                                "are not offered in the simulator; the story waits where you left it.");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Retire")) {
                driver.retire(run, RunEnd::retired);
                toggle_menu(view);
                view.pad_latch = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("ends the run and returns to the story");
            driver.draw_menu(run);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            settings_display_section(view);
            settings_audio_section(view);
            const std::string discord_status = host.discord ? host.discord->status() : std::string("off");
            settings_presence_section(view, discord_status.c_str());
            settings_keyboard_section(view);
            settings_controllers_section(view);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

RunOutcome run_loop(HostView& view, engine::Engine& run, RunDriver& driver, const RunRequest& request, const RunHost& host) {
    RunOutcome outcome;
    outcome.form = request.form;
    // DW4_TRACE_OVERFLOW=1: the run's start, every 600th step and its end.
    static const bool trace = std::getenv("DW4_TRACE_OVERFLOW") != nullptr;
    static const bool trace_presence = view.test_run && std::getenv("DW4_TRACE_PRESENCE") != nullptr;
    const std::int64_t started = unix_now();
    std::optional<discord::Activity> traced_activity;
    std::vector<int> shots = request.shots;
    std::sort(shots.begin(), shots.end());
    // Test runs end with Retire after frames= (else --smoke-test's count).
    const std::uint64_t frame_limit = view.test_run ? std::uint64_t(request.frames ? request.frames : host.test_frames) : 0;
    std::uint64_t steps_done = 0;
    double accumulator = 0;
    auto last = std::chrono::steady_clock::now();
    view.last_present = view.next_present = last;
    std::string message;
    auto message_until = last;
    int fade_in = view.draw ? kFadeFrames : 0; // presents from black
    int fade_out = -1;                         // presents to black once the run is over
    std::optional<RunEnd> end;
    bool retire_asked = false;
    std::array<engine::PadSample, 4> pads{};
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (host_event(view, event)) continue;
            // (No save states in the simulator: the story's F5 / F9 wait for the return.)
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key == SDLK_F5 || event.key.key == SDLK_F9)) {
                message = "Not in the simulator: save states wait until the run is over.";
                message_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            }
        }
        if (view.quit_requested) {
            // The window closes: the run counts as retired, then the program quits.
            driver.retire(run, RunEnd::window_closed);
            end = RunEnd::window_closed;
            break;
        }
        std::array<HostPadInput, 4> live{};
        if (!view.show_menu) live = read_live_input(view);
        if (!end) end = driver.ended();
        // Pacing (the story loop's): test runs one step a pass, else the
        // fixed 59.94 Hz step, at most four a pass. The F1 menu pauses the
        // run (whatever pause_in_menu says: a run is single player and
        // offline; test runs step on, as the story's do); once it is over
        // nothing steps.
        const auto now = std::chrono::steady_clock::now();
        const double pass_seconds = view.dev.presenting() ? 1.0 / view.dev.hz : std::chrono::duration<double>(now - last).count();
        accumulator += pass_seconds;
        last = now;
        int steps = view.test_run && !view.dev.presenting() ? 1 : 0;
        if ((view.show_menu && !view.test_run) || end) {
            accumulator = 0;
            steps = 0;
        } else if (!view.test_run || view.dev.presenting()) {
            while (accumulator >= engine::kFieldSeconds && steps < 4) {
                accumulator -= engine::kFieldSeconds;
                ++steps;
            }
            if (steps == 4) accumulator = 0;
        }
        view.gain = fade_in > 0 ? 1.0f - float(fade_in) / float(kFadeFrames) : fade_out >= 0 ? float(fade_out) / float(kFadeFrames) : 1.0f;
        int ran = 0;
        for (int i = 0; i < steps && !end; ++i) {
            // As in the story: the scene is traversed once after every step.
            if (i > 0 && !run.held()) {
                run.traverse_scene();
                view.picture.items_current = false;
            }
            pads = {};
            pads[0] = live_pad_sample(live[0]);
            driver.before_step(run, pads);
            run.step(pads);
            ++ran;
            mix_audio(view, run, step_samples(run.frame()));
            ++steps_done;
            if (trace && steps_done % 600 == 0)
                std::printf("[overflow %llu] stage %s run hash %016llx\n", (unsigned long long)steps_done, run.stage_name().c_str(),
                            (unsigned long long)run.net_fingerprint());
            if (const auto over = driver.after_step(run)) end = over;
            // A console reset in the run (B + X + START; SURVIVAL.md H3 keeps it
            // out of the director's runs) ends it.
            if (!end && run.rebooting()) {
                driver.retire(run, RunEnd::retired);
                end = RunEnd::retired;
            }
            if (!end && !retire_asked && frame_limit && steps_done >= frame_limit) {
                driver.retire(run, RunEnd::retired);
                retire_asked = true;
            }
            // (A test run whose driver never answers its Retire still ends,
            // a minute later.)
            if (!end && retire_asked && steps_done >= frame_limit + 3600) end = RunEnd::retired;
        }
        if (ran > 0 && view.draw) view.interpolator.advance();
        if (ran > 0 && view.label_mode() != LabelMode::off) view.labels.capture(run, 0);
        if (host.discord) {
            const auto status = driver.status(run);
            discord::Activity activity;
            activity.start = started;
            activity.details = "Data Overflow";
            activity.state = status.form_name + " Lv " + std::to_string(status.level) + " - " + clock_text(status.seconds);
            activity.large_image = kLogoImageKey;
            activity.large_text = "Digimon World 4";
            if (const auto* form = form_by_model(request.form)) activity.small_image = form->key;
            activity.small_text = status.form_name;
            if (trace_presence && (!traced_activity || traced_activity->details != activity.details ||
                                   traced_activity->small_image != activity.small_image)) {
                std::printf("[overflow %llu] presence details='%s' state='%s' small=%s\n", (unsigned long long)steps_done,
                            activity.details.c_str(), activity.state.c_str(), activity.small_image.c_str());
                traced_activity = activity;
            }
            host.discord->set_activity(activity);
        }
        note_stage(view, run);
        const bool smooth = view.smooth_blending();
        if (make_picture(view, run, ran, smooth) == PictureMade::drawn) driver.decorate(run, view.picture.items);
        if (view.draw) {
            const auto scene = present_scene(view, run, accumulator, smooth, false);
            set_menu_navigation(view.show_menu, false);
            new_ui_frame(false);
            try {
                draw_tags(view, scene);
                OverlayFrame frame;
                frame.width = scene.width;
                frame.height = scene.height;
                frame.screen_x = scene.screen.x;
                frame.screen_y = scene.screen.y;
                frame.screen_width = scene.screen.width;
                frame.screen_height = scene.screen.height;
                frame.menu_open = view.show_menu;
                frame.steps = steps_done;
                driver.draw_overlay(run, frame);
                if (view.show_menu) draw_run_menu(view, run, driver, host);
                if (!message.empty() && std::chrono::steady_clock::now() < message_until) {
                    ImGui::SetNextWindowPos(ImVec2(12, 12));
                    ImGui::SetNextWindowBgAlpha(0.6f);
                    ImGui::Begin("##state", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                    ImGui::TextUnformatted(message.c_str());
                    ImGui::End();
                }
                draw_black(fade_in > 0 ? float(fade_in) / float(kFadeFrames) : fade_out >= 0 ? 1.0f - float(fade_out) / float(kFadeFrames) : 0.0f);
            } catch (...) {
                ImGui::EndFrame();
                throw;
            }
            render_ui();
            // Test runs: --overflow-run shots=A,B (run steps), beside --screenshot's file.
            if (ran > 0 && !shots.empty() && !host.screenshot.empty() && std::binary_search(shots.begin(), shots.end(), int(steps_done))) {
                auto path = std::filesystem::path(host.screenshot);
                path.replace_filename(path.stem().string() + "_overflow_" + std::to_string(steps_done) + ".bmp");
                write_screenshot(path.string(), scene.width, scene.height);
                std::cout << "screenshot " << path.string() << " at run step " << steps_done << " stage " << run.stage_name() << '\n';
            }
            finish_present(view, run);
            if (fade_in > 0) --fade_in;
        }
        if (end) {
            // The picture fades to black (no steps) before the story comes back.
            if (!view.draw) break;
            if (fade_out < 0) fade_out = kFadeFrames;
            else if (--fade_out <= 0) break;
        }
        pace_presents(view);
    }
    outcome.end = end.value_or(RunEnd::retired);
    outcome.steps = steps_done;
    outcome.level = driver.status(run).level;
    outcome.run_hash = run.net_fingerprint();
    return outcome;
}

} // namespace

RunOutcome run_from_story(HostView& view, const engine::Engine& story, const RunRequest& request, const RunHost& host) {
    RunOutcome outcome;
    outcome.form = request.form;
    restart_pictures(view);
    // (The press that started the run, F1's Start or a dialog's A, must not
    // reach the run's first steps.)
    view.pad_latch = true;
    try {
        // (Declared first, destroyed last: the story's generator comes back
        // after the run's engine is gone.)
        StoryRandomGuard random;
        TestEnvironmentScope environment;
        RunCards cards;
        auto run = std::make_unique<engine::Engine>(MountedResources(story.resources()));
        prepare_run_engine(*run, request, cards.root());
        auto driver = make_run_driver(request);
        driver->attach(*run, request);
        run->request_stage(request.field);
        if (std::getenv("DW4_TRACE_OVERFLOW"))
            std::printf("[overflow 0] run %s, test variables kept out: %zu\n", describe(request).c_str(), environment.removed().size());
        outcome = run_loop(view, *run, *driver, request, host);
        driver.reset();
        run.reset();
    } catch (const std::exception& error) {
        outcome.end = RunEnd::failed;
        outcome.error = error.what();
    }
    restart_pictures(view);
    view.gain = 1.0f;
    return outcome;
}

void draw_fade(float black) { draw_black(black); }

std::string start_refusal(const engine::Engine& story, bool online_session) {
    if (online_session || story.online().enabled) return "Data Overflow is offline only: not in online sessions.";
    if (story.rebooting()) return "The console is restarting.";
    if (!story.stage_snapshot() || story.loading() || !story.presenting()) return "Wait until the stage has loaded.";
    if (const auto* sys = story.get<engine::GameSystem>()) {
        int joined = 0;
        for (const auto& player : sys->players) joined += player.joined ? 1 : 0;
        if (joined > 1) return "The simulator takes one Digimon at a time: play it with player 1 alone.";
    }
    return {};
}

std::optional<RunRequest> draw_dev_start(DevStartState& state, const std::string& refusal) {
    ImGui::SeparatorText("Data Overflow (test)");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("A development start of the survival mode (round O1a): one Digimon on the Violet Ring, back here when "
                        "the run ends. The story waits meanwhile and goes on exactly where it was; nothing of the run reaches "
                        "your saves. Offline only.");
    ImGui::PopTextWrapPos();
    static const char* forms[] = {"Agumon", "Veemon"};
    ImGui::Combo("Digimon", &state.form, forms, 2);
    ImGui::InputInt("Seed (0: a new one)", &state.seed);
    state.seed = std::max(state.seed, 0);
    ImGui::BeginDisabled(!refusal.empty());
    const bool start = ImGui::Button("Start a run");
    ImGui::EndDisabled();
    if (!refusal.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "%s", refusal.c_str());
    if (!start) return std::nullopt;
    RunRequest request;
    request.form = state.form == 1 ? "p_vmon" : "p_agumon";
    request.seed = state.seed > 0 ? std::uint32_t(state.seed)
                                   : std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF) | 1u;
    return request;
}

} // namespace dw4::game::overflow
