#include "host_view.hpp"

#include "button_prompts.hpp"
#include "controls.hpp"
#include "gc_adapter.hpp"
#include "ghost_figures.hpp"
#include "online/presence.hpp"
#include "pad_host.hpp"
#include "switch2_pro.hpp"
#include "texture_pack.hpp"

#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#ifdef SDL_PLATFORM_ANDROID
#include <imgui_internal.h>
#endif

#include "app_paths.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

// (Moved out of game_main.cpp's run() in round O1a: the statements are the
// loop's own, in their order; see host_view.hpp.)

namespace dw4::game {

void PictureState::reset() {
    items.clear();
    items_current = false;
    rendered_stage.clear();
    rendered_serial = 0;
    movie_decoder.reset();
    movie_serial = 0;
    movie_stage.clear();
    last_picture_slot = -1;
    ghost_items.clear();
    ghost_shadow = {};
    documents_digest = 0;
    documents_count = 0;
}

LabelMode HostView::label_mode() const {
    if (test_run) {
        const char* mode = std::getenv("DW4_LABELS");
        return mode ? parse_label_mode(mode) : trace_labels ? LabelMode::all : LabelMode::off;
    }
    return parse_label_mode(settings.overhead_labels);
}

bool HostView::smooth_blending() const {
    return (!test_run || dev.presenting()) && settings.smooth_motion && present_interval < 1.0 / 75.0;
}

PadFamily HostView::prompt_family(std::size_t player) const {
    if (!forced_prompts.empty()) return forced_prompts[std::min(player, forced_prompts.size() - 1)];
    // Test runs keep the original pictures (the reference comparisons).
    if (test_run) return PadFamily::gamecube;
    if (settings.prompts != "auto")
        if (const auto family = family_from_id(settings.prompts)) return *family;
    // A further player without a pad of his own: player 1's pictures.
    if (player > 0 && player >= pads.pads().size()) return pads.player_family(0);
    return pads.player_family(player);
}

PadFamily HostView::family_of(SDL_Gamepad* pad) const { return dw4::game::family_of(pads, pad); }

void controller_hints(const Settings& settings) {
    // (The reasons stand with the values: pad_usb.cpp.)
    for (const auto& hint : controller_hint_values(settings)) SDL_SetHint(hint.name, hint.value);
}

namespace {
HostView* g_ui_view = nullptr;

// The controller's View / Back button opens and closes the F1 menu, unless a
// binding gives it to a game button.
bool back_button_free(const Settings& settings) {
    for (const auto& inputs : settings.pad)
        for (const auto& input : inputs)
            if (input.kind == PadInput::Kind::button && input.code == SDL_GAMEPAD_BUTTON_BACK) return false;
    return true;
}
} // namespace

void toggle_menu(HostView& view) {
    view.show_menu = !view.show_menu;
    view.capture.reset();
    if (!view.show_menu && view.settings_changed && view.settings.save()) view.settings_changed = false;
}

namespace {
// The F1 menu opened or closed by an input, with the input in the log (which
// control reached the game: a controller's View / Back button, Android's Back
// key, a touch, F1).
void toggle_menu_by(HostView& view, const char* input) {
    toggle_menu(view);
    std::printf("menu: %s (%s)\n", view.show_menu ? "open" : "closed", input);
    std::fflush(stdout);
}
} // namespace

bool host_event(HostView& view, const SDL_Event& event) {
    auto& settings = view.settings;
    auto& capture = view.capture;
    // Android: the OpenGL context was lost and SDL made a new one
    // (SDL_androidevents.c): every GL object again, ImGui's too.
    if (event.type == SDL_EVENT_RENDER_DEVICE_RESET && view.draw) {
        std::cout << "lifecycle: the OpenGL context was recreated\n";
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplOpenGL3_Init(gl_is_es() ? "#version 300 es" : "#version 330");
        if (view.renderer) view.renderer->reset_context();
        if (view.prompts) view.prompts->reset_context();
        view.picture.items_current = false;
        return false;
    }
#ifdef SDL_PLATFORM_ANDROID
    // Android only (a PC's browser Back key stays a bindable key, a Windows
    // touch screen's tap stays a click): the Back key
    // (SDL_HINT_ANDROID_TRAP_BACK_BUTTON) opens and closes the F1 menu; a
    // tap in the window's top right corner does too, unless it lands on an
    // open menu's window (then ImGui gets it; the menus are the only things
    // touch works on, docs/ANDROID.md).
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_AC_BACK && !event.key.repeat) {
        toggle_menu_by(view, "the Back key");
        return false;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) view.unfocused = true;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) view.unfocused = false;
    if (event.type == SDL_EVENT_FINGER_DOWN) view.last_touch = std::chrono::steady_clock::now();
    if (event.type == SDL_EVENT_SCREEN_KEYBOARD_SHOWN || event.type == SDL_EVENT_SCREEN_KEYBOARD_HIDDEN)
        note_screen_keyboard(event.type == SDL_EVENT_SCREEN_KEYBOARD_SHOWN);
    if (event.type == SDL_EVENT_FINGER_DOWN && view.window) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(view.window, &width, &height);
        reopen_text_input(view.window, event.tfinger.x * float(width), event.tfinger.y * float(height));
    }
    bool on_button = false;
    if (event.type == SDL_EVENT_FINGER_DOWN && view.window) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(view.window, &width, &height);
        on_button = touch_menu_rect(float(width), float(height)).contains(event.tfinger.x * float(width), event.tfinger.y * float(height));
    }
    // The button is drawn over everything (ImGui's foreground draw list), so
    // a tap on it is the button's even where a large menu reaches under it
    // (the F1 window at a menu size of 300 % covers the button's place).
    if (event.type == SDL_EVENT_FINGER_DOWN && on_button) {
        toggle_menu_by(view, "the touch menu button");
        return false;
    }
    if (event.type == SDL_EVENT_FINGER_DOWN && event.tfinger.x >= 0.9f && event.tfinger.y <= 0.12f) {
        bool on_menu = false;
        if (view.show_menu && ImGui::GetCurrentContext()) {
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            ImGuiWindow* hovered = nullptr;
            ImGuiWindow* under_moving = nullptr;
            ImGui::FindHoveredWindowEx(ImVec2(event.tfinger.x * size.x, event.tfinger.y * size.y), false, &hovered, &under_moving);
            on_menu = hovered != nullptr;
        }
        if (!on_menu) {
            toggle_menu_by(view, "a touch in the top right corner");
            return false;
        }
    }
#endif
    // Controllers coming and going (player pads only: pad_host.hpp) and
    // real input (the prompts follow player 1's last device).
    if (view.controller_log && event.type == SDL_EVENT_GAMEPAD_REMOVED)
        if (SDL_Gamepad* pad = pad_by_id(view.gamepads, event.gdevice.which))
            view.controller_log->write("- pad " + pad_log_line(pad, view.family_of(pad)));
    const bool slots_changed = pad_slots_event(view.gamepads, view.pads, event);
    if (view.controller_log && slots_changed && event.type == SDL_EVENT_GAMEPAD_ADDED)
        if (SDL_Gamepad* pad = pad_by_id(view.gamepads, event.gdevice.which))
            view.controller_log->write("+ pad " + pad_log_line(pad, view.family_of(pad)));
    // A key binding being edited takes the next key (F1 still closes the
    // menu). A controller binding being edited takes the next button or
    // trigger pull.
    if (capture && capture->gamepad &&
        (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
         (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.value > 16000 &&
          (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)))) {
        // In the pad's binding space (a Nintendo pad by label: its A button
        // binds as "a"; a GameCube pad's trigger click as its trigger).
        SDL_Gamepad* source = pad_by_id(view.gamepads, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event.gbutton.which : event.gaxis.which);
        const PadFamily family = source ? view.family_of(source) : PadFamily::generic;
        PadInput input = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
                             ? PadInput{PadInput::Kind::button, binding_button(int(event.gbutton.button), family, settings.nintendo_layout)}
                             : PadInput{PadInput::Kind::axis, int(event.gaxis.axis)};
        if (family == PadFamily::gamecube && input.kind == PadInput::Kind::button) {
            if (input.code == SDL_GAMEPAD_BUTTON_MISC3) input = {PadInput::Kind::axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER};
            if (input.code == SDL_GAMEPAD_BUTTON_MISC4) input = {PadInput::Kind::axis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
        }
        settings.pad[capture->button][capture->slot] = input;
        for (std::size_t b = 0; b < settings.pad.size(); ++b)
            for (std::size_t k = 0; k < settings.pad[b].size(); ++k)
                if ((b != capture->button || k != capture->slot) && settings.pad[b][k] == input) settings.pad[b][k] = {};
        capture.reset();
        view.settings_changed = true;
        return true;
    }
    if (capture && capture->gamepad && event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
        (event.jbutton.button == kThorL2Button || event.jbutton.button == kThorR2Button))
        if (SDL_Gamepad* source = pad_by_id(view.gamepads, event.jbutton.which); source && view.family_of(source) == PadFamily::thor) {
            // (The AYN Thor's L2 / R2 keys in its Digital trigger mode: its
            // trigger, as an analog pull binds.)
            const PadInput input{PadInput::Kind::axis,
                                 event.jbutton.button == kThorL2Button ? int(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) : int(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)};
            settings.pad[capture->button][capture->slot] = input;
            for (std::size_t b = 0; b < settings.pad.size(); ++b)
                for (std::size_t k = 0; k < settings.pad[b].size(); ++k)
                    if ((b != capture->button || k != capture->slot) && settings.pad[b][k] == input) settings.pad[b][k] = {};
            capture.reset();
            view.settings_changed = true;
            return true;
        }
    if (capture && !capture->gamepad && event.type == SDL_EVENT_KEY_DOWN && event.key.scancode != SDL_SCANCODE_F1) {
        settings.keys[capture->button][capture->slot] = event.key.scancode;
        // A key stands for one button only.
        for (std::size_t b = 0; b < settings.keys.size(); ++b)
            for (std::size_t k = 0; k < settings.keys[b].size(); ++k)
                if ((b != capture->button || k != capture->slot) && settings.keys[b][k] == event.key.scancode)
                    settings.keys[b][k] = SDL_SCANCODE_UNKNOWN;
        capture.reset();
        view.settings_changed = true;
        return true;
    }
    if (view.draw) ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) view.quit_requested = true;
    if (event.type == SDL_EVENT_WINDOW_RESIZED && !view.test_run && !(SDL_GetWindowFlags(view.window) & SDL_WINDOW_FULLSCREEN)) {
        settings.width = event.window.data1;
        settings.height = event.window.data2;
        view.settings_changed = true;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK && back_button_free(settings))
        toggle_menu_by(view, "the controller's View / Back button");
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) toggle_menu_by(view, "F1");
#ifndef SDL_PLATFORM_ANDROID
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11 && !event.key.repeat) {
        settings.fullscreen = !settings.fullscreen;
        SDL_SetWindowFullscreen(view.window, settings.fullscreen);
        view.settings_changed = true;
    }
#endif
    return false;
}

namespace {
// The AYN Thor's controller style the Thor's pads are read with: settings.ini
// thor_style, or automatic: the device's setting (Android's
// persist.sys.gamepad.type, read again every second, so a switch in the
// quick settings takes effect while the game runs), Standard when unknown.
void update_thor_style(HostView& view) {
    const auto now = std::chrono::steady_clock::now();
#ifdef SDL_PLATFORM_ANDROID
    if (now >= view.thor_checked) {
        view.thor_checked = now + std::chrono::seconds(1);
        view.thor_device = thor_style_from_property(android_property("persist.sys.gamepad.type"));
    }
#else
    (void)now;
#endif
    ThorStyle style = view.settings.thor_style;
    if (style == ThorStyle::automatic) style = view.thor_device.value_or(ThorStyle::standard);
    if (style == view.thor_used && thor_codes() == style) return;
    view.thor_used = style;
    set_thor_codes(style);
    bool thor_pad = false;
    for (const auto& pad : view.pads.pads()) thor_pad = thor_pad || pad.family == PadFamily::thor;
    if (thor_pad || view.settings.thor_style != ThorStyle::automatic) {
        const std::string line = std::string("thor: controller style ") + (style == ThorStyle::xbox ? "Xbox" : "Standard") +
                                 (view.settings.thor_style != ThorStyle::automatic ? " (settings.ini)" : view.thor_device ? " (the device's setting)" : " (assumed)");
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        if (view.controller_log) view.controller_log->write(line);
    }
}
} // namespace

void update_controllers(HostView& view) {
    if (!view.draw) return;
    update_thor_style(view);
    if (!view.test_run) update_sdl_and_adapter(view);
    // The Switch 2 Pro Controller (switch2_pro.hpp; also in a test run with
    // DW4_TEST_SWITCH2=1): its pads, the reader's log lines, each pad's
    // player lights, and what F1 > Controls says about it.
    if (view.switch2_reader) {
        auto& reader = *view.switch2_reader;
        reader.update();
        const auto lines = reader.take_log();
        if (view.controller_log)
            for (const auto& line : lines) view.controller_log->write(line);
        view.switch2_devices = reader.status();
        for (std::size_t i = 0; i < view.gamepads.size(); ++i)
            if (const SDL_JoystickID id = SDL_GetGamepadID(view.gamepads[i]); reader.owns(id)) reader.set_player(id, int(i));
    }
    const Switch2Notice notice =
        view.switch2.update(std::chrono::steady_clock::now(), switch2_notice(view.findings, view.switch2_devices, view.switch2_reader != nullptr));
    if (view.controller_log && notice != view.logged_notice) {
        view.logged_notice = notice;
        view.controller_log->write(std::string("switch 2 pro notice: ") + switch2_notice_name(notice));
    }
}

void update_sdl_and_adapter(HostView& view) {
    // SDL's Wii driver, automatic: the device list is looked at only when
    // SDL counts a device change (a Bluetooth pairing, a plugged pad).
    if (const std::uint32_t changes = SDL_hid_device_change_count(); !view.hid_changes || changes != view.hid_changes) {
        view.hid_changes = changes ? changes : 1;
        bool found = false;
        if (view.settings.wii_driver == WiiDriver::automatic)
            if (SDL_hid_device_info* devices = SDL_hid_enumerate(kVendorNintendo, kProductWiiUPro)) {
                found = true;
                SDL_hid_free_enumeration(devices);
            }
        view.wii_u_ids = found;
    }
    if (const bool wanted = wii_driver_wanted(view.settings.wii_driver, view.wii_u_ids); wanted != view.wii_hint) {
        // (SDL takes the hint at its next device scan: the pad follows.)
        view.wii_hint = wanted;
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, wanted ? "1" : "0");
    }
    if (view.gc_adapter) {
        // Released while the window is in the background (Dolphin can take
        // it then); its ports stay as idle pads meanwhile.
        const bool focused = view.window && (SDL_GetWindowFlags(view.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        view.gc_adapter->set_enabled(view.settings.gc_adapter && focused, view.settings.gc_adapter);
        view.gc_adapter->update();
        // What Windows lists of the adapter and of a Switch 2 Pro Controller
        // (the adapter reader's look: Bluetooth for the notice, every node
        // for the log). (Copied only after a new look: it lists nodes.)
        const std::uint32_t scans = view.gc_adapter->scans();
        if (scans != view.findings_scans) {
            view.findings_scans = scans;
            view.findings = view.gc_adapter->findings();
        }
        const UsbFindings& windows = view.findings;
        if (view.controller_log) {
            // (The first look, then every change.)
            if (scans != view.logged_scans && (view.logged_scans == 0 || windows != view.logged_findings)) {
                view.logged_findings = windows;
                view.controller_log->write(usb_findings_line(windows));
                for (const auto& line : usb_findings_node_lines(windows)) view.controller_log->write(line);
            }
            view.logged_scans = scans;
        }
    }
}

std::array<HostPadInput, 4> read_live_input(HostView& view) {
    update_controllers(view);
    std::array<HostPadInput, 4> live{};
    // Keyboard: the bound keys of settings.ini [keyboard]; controllers:
    // [gamepad] and the sticks.
    live[0] = keyboard_input(view.settings, SDL_GetKeyboardState(nullptr));
    for (std::size_t i = 0; i < view.gamepads.size() && i < 4; ++i) {
        const auto pad = pad_input(view, view.gamepads[i]);
        if (i == 0) merge(live[0], pad);
        else live[i] = pad;
    }
    if (view.pad_latch) {
        bool held = false;
        for (const auto& in : live) held = held || in.buttons || in.stick_directions || in.c_stick_directions || in.stick.x != 0 || in.stick.y != 0;
        if (held) live = {};
        else view.pad_latch = false;
    }
    return live;
}

HostPadInput pad_input(const HostView& view, SDL_Gamepad* pad) { return pad_input(view.settings, view.family_of(pad), pad); }

engine::PadSample live_pad_sample(const HostPadInput& live) {
    float stick_x = live.stick.x, stick_y = live.stick.y;
    if (stick_x == 0 && stick_y == 0) {
        const auto keys = live.stick_directions;
        if (keys & pad_direction::negative_x) stick_x = -1;
        if (keys & pad_direction::positive_x) stick_x = 1;
        if (keys & pad_direction::positive_y) stick_y = 1;
        if (keys & pad_direction::negative_y) stick_y = -1;
    }
    return host_pad_sample({live.buttons, 0, live.c_stick_directions, {stick_x, stick_y}, live.c_stick});
}

void mix_audio(HostView& view, engine::Engine& engine, int frames) {
    auto* sound = engine.get<engine::SoundManager>();
    if (!sound || (!view.audio && !view.audio_dump_path && !view.audio_mix)) return;
    std::vector<std::int16_t> pcm(std::size_t(frames) * 2);
    // A rebooting console is silent (the reset stops the DSP).
    if (!engine.rebooting()) sound->render(pcm.data(), std::size_t(frames));
    // Master volume (settings.ini [audio] volume, percent).
    if (view.settings.volume < 100)
        for (auto& sample : pcm) sample = std::int16_t(int(sample) * view.settings.volume / 100);
    // The host's fades (Data Overflow); never below 1 otherwise.
    if (view.gain < 1.0f) {
        const float gain = std::max(view.gain, 0.0f);
        for (auto& sample : pcm) sample = std::int16_t(float(sample) * gain);
    }
    if (view.audio) SDL_PutAudioStreamData(view.audio, pcm.data(), int(pcm.size() * sizeof(std::int16_t)));
    if (view.audio_dump_path) view.audio_dump.insert(view.audio_dump.end(), pcm.begin(), pcm.end());
}

void trace_textures(HostView& view, const std::string& stage) {
    static const bool trace = std::getenv("DW4_TRACE_TEXTURES") && std::atoi(std::getenv("DW4_TRACE_TEXTURES")) != 0;
    if (!trace || !view.renderer) return;
    const auto s = view.renderer->texture_stats();
    constexpr double mib = 1024.0 * 1024.0;
    std::printf("textures %s: game %zu (%.1f MiB), pack %zu (%.1f MiB), peak %.1f MiB; uploads %zu (%.1f MiB, %.1f ms, longest pass %.2f ms); "
                "draws before their pack texture %zu; evicted %zu, failed %zu; scanned %zu images in %.0f ms, loaded %zu in %.0f ms, %zu queued%s%s\n",
                stage.empty() ? "-" : stage.c_str(), s.originals, double(s.original_bytes) / mib, s.replacements, double(s.replacement_bytes) / mib,
                double(s.peak_bytes) / mib, s.uploads, double(s.upload_bytes) / mib, s.upload_ms, s.longest_upload_pass_ms, s.missing_draws,
                s.evictions, s.failed, s.images_scanned, s.scan_ms, s.loads, s.load_ms, s.pending, s.last_error.empty() ? "" : "; last error: ",
                s.last_error.c_str());
    if (s.gpu_available_kb >= 0) std::printf("textures %s: GPU memory available %.0f MiB\n", stage.c_str(), double(s.gpu_available_kb) / 1024.0);
    std::fflush(stdout);
    view.renderer->reset_texture_stats();
}

bool note_stage(HostView& view, engine::Engine& engine, GhostFigures* ghosts) {
    auto& picture = view.picture;
    const std::uint64_t serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
    bool changed = false;
    if (picture.rendered_stage != engine.stage_name() || picture.rendered_serial != serial) {
        if (view.renderer) {
            trace_textures(view, picture.rendered_stage);
            view.renderer->clear_cache();
        }
        picture.rendered_stage = engine.stage_name();
        picture.rendered_serial = serial;
        view.interpolator.reset();
        view.labels.reset();
        if (ghosts) ghosts->clear(); // (a new stage: no figure fades across the load)
        changed = true;
    }
    // Texture pack streaming: the documents the engine holds (handed over
    // when they change: a stage's are scanned and read ahead while it
    // loads), then this pass's uploads.
    if (view.renderer) {
        const auto& files = engine.loaded_files();
        // (The fonts' document is the text manager's own, not a loaded file.)
        const auto* text = engine.get<engine::TextCtlManager>();
        const auto font = text ? text->font_document() : nullptr;
        std::uint64_t digest = 0xcbf29ce484222325ull;
        for (const auto& file : files) {
            digest ^= std::uint64_t(reinterpret_cast<std::uintptr_t>(file.document.get()));
            digest *= 0x100000001b3ull;
        }
        digest ^= std::uint64_t(reinterpret_cast<std::uintptr_t>(font.get()));
        if (digest != picture.documents_digest || files.size() != picture.documents_count) {
            std::vector<std::shared_ptr<const IgbDocument>> documents;
            documents.reserve(files.size() + 1);
            for (const auto& file : files)
                if (file.document) documents.push_back(file.document);
            if (font) documents.push_back(font);
            view.renderer->stream_textures(&documents);
            picture.documents_digest = digest;
            picture.documents_count = files.size();
        } else {
            view.renderer->stream_textures(nullptr);
        }
    }
    return changed;
}

PictureMade make_picture(HostView& view, engine::Engine& engine, int steps, bool smooth_blending) {
    auto& picture = view.picture;
    // A field the running update covers (Engine::held: the movie's 30 Hz
    // frames) shows the last update's picture again, without a traversal
    // (only a catch-up that left no draw list draws it).
    if (engine.held() && steps > 0 && (picture.items_current || !view.draw)) return PictureMade::kept;
    if (!view.draw) {
        engine.traverse_scene();
        return PictureMade::traversed;
    }
    // The picture's own frustum cull follows the renderer's widescreen
    // widening (GlRenderer::render, the viewports of present_scene): subtrees
    // the original's cull skips that lie in the wider picture are drawn
    // without their updates.
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(view.window, &width, &height);
    engine.picture_widen = 1.0f;
    if (view.settings.widescreen && width * 3 > height * 4 && height > 0) {
        const int screen_width = height * 4 / 3;
        engine.picture_widen = (float(screen_width) / float(height)) / (float(width) / float(height));
    }
    // Smooth motion draws between the previous step's camera and this one:
    // the picture's cull keeps what is inside either.
    engine.picture_previous_view = smooth_blending ? view.interpolator.previous_view() : std::nullopt;
    // Seamless co-op: a new picture camera (a defeated member watches a
    // partner) starts its smooth motion anew.
    if (const int slot = engine.picture_slot(); slot != picture.last_picture_slot) {
        if (picture.last_picture_slot >= 0) view.interpolator.reset();
        picture.last_picture_slot = slot;
        engine.picture_previous_view = std::nullopt;
    }
    engine.render(picture.items, picture.camera, picture.clear, &picture.shadow);
    dev_hide(view.dev, picture.items, picture.shadow);
    picture.items_current = true;
    return PictureMade::drawn;
}

PresentedScene present_scene(HostView& view, engine::Engine& engine, double accumulator, bool smooth_blending, bool report_blend,
                             const RemotePresentation* remote) {
    auto& picture = view.picture;
    auto& items = picture.items;
    auto& camera = picture.camera;
    auto& shadow = picture.shadow;
    auto* renderer = view.renderer;
    PresentedScene scene;
    if (!view.interpolator.captured()) view.interpolator.capture(items, camera, shadow);
    const float present_alpha = float(accumulator / engine::kFieldSeconds);
    scene.present_alpha = present_alpha;
    // (DW4_DEV_DUMP: what the blend did, for presents.csv.)
    if (smooth_blending) view.interpolator.blend(items, camera, shadow, present_alpha, report_blend ? &scene.blend_report : nullptr);
    // Ghost figures (ghost_figures.hpp, the story loop online): at the
    // present's own time, into a copy of the picture's list and shadow (a held
    // field draws the list again as it is), never the simulation's.
    const std::vector<engine::DrawItem>* frame_items = &items;
    const engine::ShadowFrame* frame_shadow = &shadow;
    auto& ghost_items = picture.ghost_items;
    auto& ghost_shadow = picture.ghost_shadow;
    if (!ghost_items.empty()) {
        ghost_items.clear();
        ghost_shadow = {};
    }
    if (remote && remote->ghosts && remote->ghosts->active() && engine.presenting()) {
        const auto ghost_draw_start = std::chrono::steady_clock::now();
        ghost_items = items;
        ghost_shadow = shadow;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        remote->ghosts->draw(ghost_items, &ghost_shadow, camera, remote->own_position, now);
        frame_items = &ghost_items;
        frame_shadow = &ghost_shadow;
        if (remote->cost_max)
            *remote->cost_max =
                std::max(*remote->cost_max, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ghost_draw_start).count());
    }
    if (remote && remote->overlay && engine.presenting()) {
        if (frame_items != &ghost_items) {
            ghost_items = items;
            ghost_shadow = shadow;
        }
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        remote->overlay(ghost_items, &ghost_shadow, camera, now);
        frame_items = &ghost_items;
        frame_shadow = &ghost_shadow;
    }
    renderer->set_shadow(frame_shadow);
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(view.window, &width, &height);
    scene.width = width;
    scene.height = height;
    Viewport screen;
    if (width * 3 > height * 4) { screen.height = height; screen.width = height * 4 / 3; }
    else { screen.width = width; screen.height = width * 3 / 4; }
    screen.x = (width - screen.width) / 2;
    screen.y = (height - screen.height) / 2;
    // Widescreen: the 3D scene uses the whole window width.
    Viewport wide = screen;
    if (view.settings.widescreen && width * 3 > height * 4) {
        wide.x = 0;
        wide.width = width;
    }
    scene.screen = screen;
    scene.wide = wide;
    // From the step that took a stage change on, the original presents
    // nothing new: the last picture stays until the loader draws.
    // Overhead name tags: where they stand on this picture and whether it
    // hides their players (its depth buffer, read right after the scene is
    // drawn).
    const auto tag_mode = view.label_mode();
    scene.tag_mode = tag_mode;
    auto& tags = scene.tags;
    auto& tag_view = scene.tag_view;
    auto& labels = view.labels;
    if (engine.presenting() || !renderer->draw_kept_picture(width, height)) {
        renderer->render(*frame_items, camera, picture.clear, screen, wide, width, height);
        if (tag_mode != LabelMode::off && !OverheadLabels::hidden(engine)) {
            scene.tags_shown = true;
            tag_view.camera = camera;
            tag_view.widen = wide.width > screen.width && wide.height > 0 && screen.height > 0
                                 ? (float(screen.width) / float(screen.height)) / (float(wide.width) / float(wide.height))
                                 : 1.0f;
            tag_view.wide_x = float(wide.x);
            tag_view.wide_y = float(wide.y);
            tag_view.wide_width = float(wide.width);
            tag_view.wide_height = float(wide.height);
            tag_view.window_height = float(height);
            // (Last frame's scale: ImGui sets it with the next NewFrame.)
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            tag_view.scale_x = scale.x > 0 ? scale.x : 1.0f;
            tag_view.scale_y = scale.y > 0 ? scale.y : 1.0f;
            // (Between the two steps like the picture: smooth motion's condition.)
            tags = labels.blended(smooth_blending ? present_alpha : 1.0f);
            // Remote tags at the presence sample of this pass, the one their
            // ghost figures stand at (no tag trails its figure).
            if (remote && remote->samples)
                for (auto& tag : tags)
                    if (tag.key >= 0x100)
                        for (const auto& sample : *remote->samples)
                            if (sample.member == tag.key - 0x100) {
                                tag.feet = {sample.x, sample.y, sample.z};
                                tag.head = remote_head(tag.feet, sample.species);
                            }
            view.traced_occlusion.clear();
            for (auto& tag : tags) {
                if ((tag.hp <= 0 && !tag.capsule) || !label_visible(tag_mode, tag.own, labels.players())) continue;
                const bool hidden_now = occluded_in_picture(
                    occlusion_probes(tag.head, tag_view),
                    [&](float x, float y) { return renderer->read_depth(int(x), height - 1 - int(y), width, height); },
                    tag_view.camera.projection);
                tag.occlusion = labels.ease_occlusion(tag.key, hidden_now);
                if (hidden_now) view.traced_occlusion.insert(tag.key);
            }
        }
        // A playing movie covers the screen with its current picture.
        // After its end Sofdec's last picture stays up until the stage
        // changes (the opening's end script switches a frame later).
        auto& movie_decoder = picture.movie_decoder;
        if (const auto* sound = engine.get<engine::SoundManager>(); sound && sound->movie() && !engine.rebooting()) {
            const auto* movie = sound->movie();
            if (!movie_decoder || picture.movie_serial != movie->serial) {
                movie_decoder = std::make_unique<MovieDecoder>(movie->video);
                picture.movie_serial = movie->serial;
                picture.movie_stage = engine.stage_name();
            }
            // The picture the player's last frame took, from that frame's
            // second field on (none before the first).
            if (movie->visible >= 0 && movie_decoder->seek(std::size_t(movie->visible)))
                renderer->draw_movie(movie_decoder->rgba().data(), movie_decoder->width(), movie_decoder->height(), screen, width, height);
        } else if (movie_decoder && picture.movie_stage == engine.stage_name()) {
            // A movie that stopped (a skip, the teardown) leaves the picture
            // its codec took last on the layer.
            if (sound && sound->movie_still().serial == picture.movie_serial && sound->movie_still().picture >= 0)
                movie_decoder->seek(std::size_t(sound->movie_still().picture));
            if (!movie_decoder->rgba().empty())
                renderer->draw_movie(movie_decoder->rgba().data(), movie_decoder->width(), movie_decoder->height(), screen, width, height);
            else
                movie_decoder.reset();
        } else {
            movie_decoder.reset();
        }
        renderer->keep_picture(width, height);
    }
    return scene;
}

void set_menu_navigation(bool menus_take_input, bool cursor_always) {
    constexpr ImGuiConfigFlags nav = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    auto& io = ImGui::GetIO();
    if (menus_take_input) io.ConfigFlags |= nav;
    else io.ConfigFlags &= ~nav;
    io.ConfigNavCursorVisibleAlways = cursor_always;
}

void set_ui_view(HostView* view) {
    g_ui_view = view;
    // (The backend reads no pad itself: menu_pad below gives ImGui player
    // 1's pad through pad_host.hpp's menu_keys.)
    if (view && view->draw) ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
}

namespace {
// ImGui's gamepad keys from player 1's pad (pad_host.hpp menu_keys): with
// menu_buttons=layout (the default) the pad's own A / Cross activates and
// B / Circle goes back on every family (a Nintendo pad as its Nintendo
// layout reads it, a GameCube pad's A and B), whatever the game's bindings;
// with menu_buttons=bindings the game's A and B do.
void menu_pad(HostView& view) {
    if (view.gamepads.empty()) return; // (no pad: a UI test run's keys stay its own)
    SDL_Gamepad* pad = view.gamepads.front();
    const PadFamily family = view.family_of(pad);
    const MenuKeys keys = menu_keys(view.settings, family, read_pad(pad, family), family != PadFamily::gamecube || trigger_clicks(pad));
    auto& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, keys.face_down);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, keys.face_right);
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, keys.face_left);
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp, keys.face_up);
    io.AddKeyEvent(ImGuiKey_GamepadStart, keys.start);
    io.AddKeyEvent(ImGuiKey_GamepadBack, keys.back);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, keys.up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, keys.down);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, keys.left);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, keys.right);
    io.AddKeyEvent(ImGuiKey_GamepadL1, keys.l1);
    io.AddKeyEvent(ImGuiKey_GamepadR1, keys.r1);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, keys.l2 > 0.1f, keys.l2);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, keys.r2 > 0.1f, keys.r2);
    const auto analog = [&](ImGuiKey k, float v) {
        const float value = std::clamp((v - 0.25f) / 0.75f, 0.0f, 1.0f);
        io.AddKeyAnalogEvent(k, value > 0.1f, value);
    };
    analog(ImGuiKey_GamepadLStickLeft, -keys.left_x);
    analog(ImGuiKey_GamepadLStickRight, keys.left_x);
    analog(ImGuiKey_GamepadLStickUp, keys.left_y);
    analog(ImGuiKey_GamepadLStickDown, -keys.left_y);
    analog(ImGuiKey_GamepadRStickLeft, -keys.right_x);
    analog(ImGuiKey_GamepadRStickRight, keys.right_x);
    analog(ImGuiKey_GamepadRStickUp, keys.right_y);
    analog(ImGuiKey_GamepadRStickDown, -keys.right_y);
}
} // namespace

void new_ui_frame(bool test_gamepad) {
    if (g_ui_view && g_ui_view->window) update_ui_scale(g_ui_view->window, g_ui_view->settings, g_ui_view->test_run);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // (The adapter's ports and the Wii driver also while the menus take the input.)
    if (g_ui_view) update_controllers(*g_ui_view);
    if (g_ui_view) menu_pad(*g_ui_view);
    if (test_gamepad) ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasGamepad;
    ImGui::NewFrame();
}

void draw_tags(HostView& view, const PresentedScene& scene) {
    if (!scene.tags_shown) return;
    const float size = float(scene.height) / scene.tag_view.scale_y / 960.0f * float(view.settings.label_scale) / 100.0f;
    draw_overhead_labels(ImGui::GetBackgroundDrawList(), scene.tags, scene.tag_view, scene.tag_mode, view.labels.players(), std::max(size, 0.5f),
                         view.settings.label_numbers, view.tag_font);
}

void render_ui() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void write_bmp(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba) {
    std::ofstream out(path, std::ios::binary);
    const int row = width * 3;
    const int pad = (4 - row % 4) % 4;
    const std::uint32_t size = 54 + std::uint32_t((row + pad) * height);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("BM", 2);
    u32(size); u32(0); u32(54); u32(40); u32(std::uint32_t(width)); u32(std::uint32_t(height));
    u16(1); u16(24); u32(0); u32(size - 54); u32(2835); u32(2835); u32(0); u32(0);
    const char zeros[3]{};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* p = &rgba[std::size_t((y * width + x) * 4)];
            const char bgr[3]{char(p[2]), char(p[1]), char(p[0])};
            out.write(bgr, 3);
        }
        out.write(zeros, pad);
    }
}

void write_screenshot(const std::string& path, int width, int height) {
    std::vector<std::uint8_t> pixels(std::size_t(width * height * 4));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    write_bmp(path, width, height, pixels);
}

namespace {
// DW4_FRAME_STATS=SECONDS (any platform; on Android --env in dw4args): every
// SECONDS a line with the presents per second and the percentiles of the
// present interval, of the pass's own work up to the swap (input, the
// steps, drawing the scene and the menus) and of the swap's wait (vsync, the
// GPU), and the frame rate the display runs at: frame pacing on a device.
// It also counts the presents by the engine steps since the one before (0:
// the picture repeats, 1, 2 or more: a step skipped on the screen) and how
// often a 0 and a 2+ follow each other (the jitter of a 59.94 Hz game on a
// 60 Hz panel shows as a lone 0 every ~16.7 s; pairs in a row are judder).
struct FrameStats {
    double period = 0;
    std::chrono::steady_clock::time_point started, last_swap;
    std::vector<float> interval, work, swap, step;
    std::size_t steps_at_present = 0;           // s.step.size() at the last present
    std::array<std::size_t, 3> by_steps{};      // presents after 0, 1, 2+ steps
    std::size_t flips = 0;                      // a 0 next to a 2+
    int previous_bucket = 1;
};
FrameStats& frame_stats_state() {
    static FrameStats stats = [] {
        FrameStats s;
        if (const char* text = std::getenv("DW4_FRAME_STATS")) s.period = std::atof(text);
        return s;
    }();
    return stats;
}
void frame_stats_step_into(FrameStats& s, double ms) {
    if (s.period > 0) s.step.push_back(float(ms));
}

float percentile(std::vector<float>& values, double p) {
    if (values.empty()) return 0;
    const auto at = std::size_t(std::clamp(p, 0.0, 1.0) * double(values.size() - 1) + 0.5);
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(at), values.end());
    return values[at];
}
void frame_stats(HostView& view, std::chrono::steady_clock::time_point before_swap) {
    auto& s = frame_stats_state();
    if (s.period <= 0) return;
    const auto now = std::chrono::steady_clock::now();
    if (s.started == std::chrono::steady_clock::time_point{}) {
        s.started = s.last_swap = now;
        return;
    }
    const auto ms = [](auto a, auto b) { return float(std::chrono::duration<double, std::milli>(b - a).count()); };
    s.interval.push_back(ms(s.last_swap, now));
    if (view.pass_started != std::chrono::steady_clock::time_point{} && view.pass_started <= before_swap) s.work.push_back(ms(view.pass_started, before_swap));
    s.swap.push_back(ms(before_swap, now));
    s.last_swap = now;
    {
        const std::size_t since = s.step.size() - s.steps_at_present;
        s.steps_at_present = s.step.size();
        const int bucket = since >= 2 ? 2 : int(since);
        ++s.by_steps[std::size_t(bucket)];
        if ((bucket == 0 && s.previous_bucket == 2) || (bucket == 2 && s.previous_bucket == 0)) ++s.flips;
        s.previous_bucket = bucket;
    }
    const double elapsed = std::chrono::duration<double>(now - s.started).count();
    if (elapsed < s.period) return;
    const std::size_t presents = s.interval.size();
    float display_hz = 0;
    if (view.window)
        if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(view.window))) display_hz = mode->refresh_rate;
    const std::size_t steps = s.step.size();
    std::printf("frames: %.1f presents/s over %.1f s (display %.0f Hz); interval p50 %.2f p90 %.2f p99 %.2f max %.2f ms; "
                "work p50 %.2f p90 %.2f p99 %.2f max %.2f ms; swap p50 %.2f p99 %.2f ms; %.1f steps/s, step p50 %.2f p90 %.2f p99 %.2f max %.2f ms\n",
                double(presents) / elapsed, elapsed, double(display_hz), double(percentile(s.interval, 0.5)), double(percentile(s.interval, 0.9)),
                double(percentile(s.interval, 0.99)), double(percentile(s.interval, 1.0)), double(percentile(s.work, 0.5)),
                double(percentile(s.work, 0.9)), double(percentile(s.work, 0.99)), double(percentile(s.work, 1.0)),
                double(percentile(s.swap, 0.5)), double(percentile(s.swap, 0.99)), double(steps) / elapsed, double(percentile(s.step, 0.5)),
                double(percentile(s.step, 0.9)), double(percentile(s.step, 0.99)), double(percentile(s.step, 1.0)));
    std::printf("frames: presents after 0 / 1 / 2+ steps: %zu / %zu / %zu, a 0 next to a 2+: %zu\n", s.by_steps[0], s.by_steps[1], s.by_steps[2],
                s.flips);
    std::fflush(stdout);
    s.interval.clear();
    s.work.clear();
    s.swap.clear();
    s.step.clear();
    s.steps_at_present = 0;
    s.by_steps = {};
    s.flips = 0;
    s.started = now;
}
} // namespace

void frame_stats_step(double ms) { frame_stats_step_into(frame_stats_state(), ms); }

void finish_present(HostView& view, engine::Engine& engine, const std::function<void()>& swapped) {
    // Keep about three frames of audio queued across frame hitches.
    if (view.audio) {
        const int queued = SDL_GetAudioStreamQueued(view.audio) / int(2 * sizeof(std::int16_t));
        if (queued < kAudioFrameSamples * 2) mix_audio(view, engine, kAudioFrameSamples * 3 - queued);
    }
    // (The frame's GL errors, the scene's and the menus': DW4_GL_CHECK.)
    if (view.renderer && view.renderer->checking_gl()) view.renderer->check_gl_errors("in the frame");
    const auto before_swap = std::chrono::steady_clock::now();
    SDL_GL_SwapWindow(view.window);
    if (swapped) swapped();
    frame_stats(view, before_swap);
    // A minimized window's swap does not wait for the display: the loop
    // would spin and take the CPU from a second game on this PC (two games
    // for one online room, round 62). Its passes are spaced 4 ms apart (the
    // steps keep their own clock).
    if (SDL_GetWindowFlags(view.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) SDL_DelayPrecise(4'000'000);
    {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - view.last_present).count();
        view.last_present = now;
        view.present_interval += (std::min(seconds, 0.1) - view.present_interval) * 0.1;
        if (view.dev.presenting()) view.present_interval = 1.0 / view.dev.hz;
        view.frame_times[view.frame_time_at++ % view.frame_times.size()] = float(seconds * 1000.0);
        view.last_present_seconds = seconds;
    }
}

void pace_presents(HostView& view) {
    if (!view.test_run && view.settings.fps_limit > 0) {
        const auto interval = std::chrono::nanoseconds(1'000'000'000LL / view.settings.fps_limit);
        view.next_present += interval;
        const auto now = std::chrono::steady_clock::now();
        if (view.next_present > now)
            SDL_DelayPrecise(Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(view.next_present - now).count()));
        else if (now - view.next_present > interval * 4)
            view.next_present = now;
    }
}

void settings_display_section(HostView& view) {
    auto& settings = view.settings;
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
#ifndef SDL_PLATFORM_ANDROID
        // (Android: always the whole screen.)
        if (ImGui::Checkbox("Fullscreen (F11)", &settings.fullscreen)) {
            SDL_SetWindowFullscreen(view.window, settings.fullscreen);
            view.settings_changed = true;
        }
#endif
        {
            // The menus' size (settings.ini ui_scale).
            int choice = 0;
            for (int i = 0; i < int(std::size(kUiScaleChoices)); ++i)
                if (settings.ui_scale == kUiScaleChoices[i]) choice = i;
            const float display_scale = view.window ? SDL_GetWindowDisplayScale(view.window) : 1.0f;
#ifdef SDL_PLATFORM_ANDROID
            const bool follow = true;
#else
            const bool follow = false;
#endif
            const std::string automatic =
                "Automatic (" + std::to_string(int(std::lround(ui_scale_factor(0, display_scale, follow) * 100.0f))) + " %)";
            if (ImGui::BeginCombo("Menu size", choice == 0 ? automatic.c_str() : (std::to_string(settings.ui_scale) + " %").c_str())) {
                for (int i = 0; i < int(std::size(kUiScaleChoices)); ++i) {
                    const std::string name = i == 0 ? automatic : std::to_string(kUiScaleChoices[i]) + " %";
                    if (ImGui::Selectable(name.c_str(), i == choice) && i != choice) {
                        settings.ui_scale = kUiScaleChoices[i];
                        view.settings_changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Checkbox("Widescreen (the 3D view fills wide windows, 2D stays 4:3)", &settings.widescreen)) view.settings_changed = true;
        if (ImGui::Checkbox("VSync", &settings.vsync)) {
            SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
            view.settings_changed = true;
        }
        if (ImGui::SliderInt("Frame limit", &settings.fps_limit, 0, 300, settings.fps_limit ? "%d fps" : "off")) {
            view.settings_changed = true;
            request_frame_rate(settings);
        }
        if (ImGui::Checkbox("Smooth motion above 60 fps", &settings.smooth_motion)) view.settings_changed = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(now %.0f fps)", view.present_interval > 0 ? 1.0 / view.present_interval : 0.0);
        ImGui::TextDisabled("The game itself always runs at 60 updates per second.");
        {
            float worst = 0, total = 0;
            for (const float t : view.frame_times) {
                worst = std::max(worst, t);
                total += t;
            }
            char overlay[96];
            std::snprintf(overlay, sizeof overlay, "avg %.2f ms  worst %.2f ms", total / float(view.frame_times.size()), worst);
            ImGui::PlotLines("Frame time", view.frame_times.data(), int(view.frame_times.size()), int(view.frame_time_at % view.frame_times.size()),
                             overlay, 0.0f, std::max(33.4f, worst), ImVec2(0, ui_px(48)));
        }
        static const int sample_choices[] = {0, 2, 4, 8};
        static const char* sample_names[] = {"Off", "2x", "4x", "8x"};
        int choice = 0;
        for (int i = 0; i < 4; ++i)
            if (settings.msaa >= sample_choices[i]) choice = i;
        if (ImGui::Combo("Anti-aliasing (MSAA)", &choice, sample_names, 4)) {
            settings.msaa = sample_choices[choice];
            view.settings_changed = true;
        }
        ImGui::TextDisabled("Anti-aliasing applies at the next start (now %dx).", view.samples > 1 ? view.samples : 0);
#ifndef SDL_PLATFORM_ANDROID
        static const int sizes[][2] = {{1280, 960}, {1440, 1080}, {1920, 1440}, {1280, 720}, {1920, 1080}, {2560, 1440}};
        if (!settings.fullscreen) {
            // The buttons wrap onto the next line instead of running past
            // the window's edge.
            const float line_end = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::TextUnformatted("Window size:");
            for (const auto& size : sizes) {
                const std::string label = std::to_string(size[0]) + "x" + std::to_string(size[1]);
                const float button = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + button <= line_end) ImGui::SameLine();
                if (ImGui::SmallButton(label.c_str())) {
                    SDL_SetWindowSize(view.window, size[0], size[1]);
                    settings.width = size[0];
                    settings.height = size[1];
                    view.settings_changed = true;
                }
            }
        }
#endif
    }
}

namespace {
// A path as UTF-8 for ImGui, and back.
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
std::filesystem::path from_utf8(const char* text) {
    const std::string bytes(text);
    return std::filesystem::path(std::u8string(bytes.begin(), bytes.end()));
}
} // namespace

void settings_graphics_section(HostView& view, const TexturePackPanel& panel) {
    auto& settings = view.settings;
    if (!ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (view.menu_section == "Graphics") {
        ImGui::SetScrollHereY(0.0f);
        view.menu_section.clear();
    }
    auto& g = settings.graphics;
    bool changed = false;
    const GlRenderer::Caps caps = view.renderer ? view.renderer->caps() : GlRenderer::Caps{};
    ImGui::PushTextWrapPos(0.0f);
    {
        static const char* names[] = {texture_quality_label(TextureQuality::original), texture_quality_label(TextureQuality::high),
                                      texture_quality_label(TextureQuality::ultra)};
        int value = int(g.texture_quality);
        if (ImGui::Combo("Texture quality", &value, names, 3)) {
            g.texture_quality = TextureQuality(value);
            changed = true;
        }
        if (panel.loading) {
            ImGui::TextDisabled("Reading the texture pack folder %s ...", utf8(panel.folder).c_str());
        } else if (panel.pack) {
            const auto& index = panel.pack->index();
            if (!index.error.empty())
                ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "The texture pack is not used: %s", index.error.c_str());
            else if (index.textures.empty())
                ImGui::TextDisabled("No texture pack in %s. Original shows the game's own textures; High and Ultra use a pack's "
                                    "2x or 4x textures when there is one.",
                                    utf8(panel.folder).c_str());
            else
                ImGui::TextDisabled("%s%s%zu pack textures (%s). Original shows the game's own textures; High and Ultra the pack's "
                                    "2x or 4x level.",
                                    index.name.c_str(), index.name.empty() ? "" : ": ", index.textures.size(),
                                    index.format == 2 ? "pack format 2" : "loose PNG / DDS files");
            // (Files for other devices, such as ASTC for phones: a note, not a fault.)
            if (!index.other_encodings.empty()) {
                std::string names;
                for (const auto& name : index.other_encodings) names += (names.empty() ? "" : ", ") + name;
                ImGui::TextDisabled("The pack also holds files for other devices (%s); this PC uses its own.", names.c_str());
            }
            if (!index.warnings.empty() && ImGui::TreeNode("pack_warnings", "%zu manifest warnings", index.warnings.size())) {
                for (const auto& w : index.warnings) ImGui::BulletText("%s", w.c_str());
                ImGui::TreePop();
            }
        }
    }
    {
        static const char* names[] = {texture_filter_label(TextureFilter::bilinear), texture_filter_label(TextureFilter::trilinear),
                                      texture_filter_label(TextureFilter::aniso2),   texture_filter_label(TextureFilter::aniso4),
                                      texture_filter_label(TextureFilter::aniso8),   texture_filter_label(TextureFilter::aniso16)};
        int count = 6;
        if (!caps.anisotropic) count = 2;
        else
            while (count > 2 && float(anisotropy(TextureFilter(count - 1))) > caps.max_anisotropy) --count;
        int value = std::min(int(g.texture_filter), count - 1);
        if (ImGui::Combo("Texture filtering", &value, names, count)) {
            g.texture_filter = TextureFilter(value);
            changed = true;
        }
        ImGui::TextDisabled("Bilinear samples the game's textures as the GameCube does (full size only). Trilinear gives them "
                            "mipmaps (less shimmer on distant floors); anisotropic keeps surfaces at a slant sharp.%s",
                            caps.anisotropic ? "" : " This GPU offers no anisotropic filtering.");
    }
    if (ImGui::SliderFloat("Mip LOD bias", &g.lod_bias, GraphicsOptions::kLodBiasMin, GraphicsOptions::kLodBiasMax, "%+.2f")) changed = true;
    ImGui::TextDisabled("For mipmapped textures: below 0 sharper (may shimmer), above 0 softer.");
    {
        static const char* names[] = {sharpening_label(Sharpening::off), sharpening_label(Sharpening::low), sharpening_label(Sharpening::high)};
        int value = int(g.sharpening);
        if (ImGui::Combo("Sharpening", &value, names, 3)) {
            g.sharpening = Sharpening(value);
            changed = true;
        }
        ImGui::TextDisabled("Contrast adaptive sharpening of the game picture (not the menus).");
    }
    {
        static const char* names[] = {shadow_quality_label(ShadowQuality::original), shadow_quality_label(ShadowQuality::high),
                                      shadow_quality_label(ShadowQuality::ultra)};
        int value = int(g.shadow_quality);
        if (ImGui::Combo("Shadow quality", &value, names, 3)) {
            g.shadow_quality = ShadowQuality(value);
            changed = true;
        }
        ImGui::TextDisabled("The resolution of the characters' shadow map (the game's own is 128 x 128).");
    }
    {
        static const int sizes[] = {512, 1024, 2048, 4096};
        // (A value settings.ini set that is none of these shows as itself,
        // as a fifth entry, until another one is chosen.)
        std::string other;
        int value = -1;
        for (int i = 0; i < 4; ++i)
            if (g.texture_memory_mb == sizes[i]) value = i;
        if (value < 0) other = std::to_string(g.texture_memory_mb) + " MiB (settings.ini)";
        const char* names[] = {"512 MiB", "1 GiB", "2 GiB", "4 GiB", other.c_str()};
        const int count = value < 0 ? 5 : 4;
        if (value < 0) value = 4;
        if (ImGui::Combo("Texture memory", &value, names, count) && value < 4) {
            g.texture_memory_mb = sizes[value];
            changed = true;
        }
        if (view.renderer) {
            const auto s = view.renderer->texture_stats();
            constexpr double mib = 1024.0 * 1024.0;
            ImGui::TextDisabled("In use: %zu game textures (%.0f MiB), %zu pack textures (%.0f MiB)%s. The least recently drawn "
                                "textures make room when this fills up.",
                                s.originals, double(s.original_bytes) / mib, s.replacements, double(s.replacement_bytes) / mib,
                                s.pending ? ", more loading" : "");
        }
    }
    if (changed) {
        g = g.clamped();
        if (view.renderer) view.renderer->set_graphics(g);
        view.settings_changed = true;
    }
    // The pack folder (empty: mods/textures beside the program).
    {
        static char folder[1024]{};
        static bool editing = false;
        if (!editing) {
            const std::string text = utf8(settings.texture_pack);
            std::snprintf(folder, sizeof folder, "%s", text.c_str());
        }
        ImGui::InputTextWithHint("Texture pack folder", "mods/textures", folder, sizeof folder);
        editing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings.texture_pack = from_utf8(folder);
            view.settings_changed = true;
            if (panel.reload) panel.reload();
        }
    }
    if (ImGui::Button("Reload texture pack") && panel.reload) panel.reload();
    ImGui::SameLine();
    if (ImGui::Checkbox("Dump textures (to mods/dump/textures)", &settings.dump_textures)) {
        view.settings_changed = true;
        if (panel.reload) panel.reload();
    }
    ImGui::TextDisabled("A pack is a folder with a dw4pack.tsv manifest and DDS files, or loose PNG / DDS files named ..._<16 hex "
                        "digit hash>.png / .dds (docs/TEXTURES.md).");
    ImGui::PopTextWrapPos();
}

void settings_audio_section(HostView& view) {
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderInt("Volume", &view.settings.volume, 0, 100, "%d%%")) view.settings_changed = true;
    }
}

void settings_presence_section(HostView& view, const char* discord_status) {
    auto& settings = view.settings;
    if (ImGui::CollapsingHeader("Name tags, Discord, achievements", ImGuiTreeNodeFlags_DefaultOpen)) {
        // (LabelMode order: automatic, all, others, off.)
        static const char* label_modes[] = {"Auto (two or more players)", "All players", "Other players", "Off"};
        int mode = int(parse_label_mode(settings.overhead_labels));
        if (ImGui::Combo("Name tags", &mode, label_modes, 4)) {
            settings.overhead_labels = label_mode_name(LabelMode(mode));
            view.settings_changed = true;
        }
        if (ImGui::SliderInt("Name tag size", &settings.label_scale, 50, 200, "%d%%")) view.settings_changed = true;
        if (ImGui::Checkbox("HP / MP numbers on name tags", &settings.label_numbers)) view.settings_changed = true;
        ImGui::TextDisabled("Name, level, HP and MP over the players' Digimon (not in cutscenes, menus and loads).");
        if (ImGui::Checkbox("Discord Rich Presence", &settings.discord)) view.settings_changed = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", discord_status);
        ImGui::TextDisabled("Shows your form, level, area and difficulty in Discord while the Discord app runs.");
        if (ImGui::Checkbox("Achievements", &settings.achievements)) view.settings_changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Unlock notices", &settings.achievement_popups)) view.settings_changed = true;
        if (ImGui::Checkbox("Network indicator in online sessions", &settings.net_indicator)) view.settings_changed = true;
        ImGui::TextDisabled("Ping and input delay in the lower left corner while you play online (details in F1 > Online).");
    }
}

void settings_keyboard_section(HostView& view) {
    auto& settings = view.settings;
    auto& capture = view.capture;
    if (ImGui::CollapsingHeader("Keyboard (player 1)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Click a key to change it, right-click to clear it.");
        ImGui::TextDisabled("As on the GameCube the main stick walks, the D-pad and the C-stick move the");
        ImGui::TextDisabled("quick panels' cursor (L / R), the main stick and the D-pad move menu cursors.");
        for (int group = 0; group < 4; ++group) {
            ImGui::SeparatorText(pad_group_name(PadGroup(group)));
            if (!ImGui::BeginTable(("keys" + std::to_string(group)).c_str(), 4, ImGuiTableFlags_SizingStretchSame)) continue;
            for (std::size_t b = 0; b < settings.keys.size(); ++b) {
                if (pad_key_group(PadKey(b)) != PadGroup(group)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pad_key_name(PadKey(b)));
                for (std::size_t k = 0; k < settings.keys[b].size(); ++k) {
                    ImGui::TableNextColumn();
                    const bool waiting = capture && !capture->gamepad && capture->button == b && capture->slot == k;
                    const SDL_Scancode code = settings.keys[b][k];
                    std::string label = waiting ? "press a key..." : code == SDL_SCANCODE_UNKNOWN ? "-" : SDL_GetScancodeName(code);
                    label += "##" + std::to_string(b * 8 + k);
                    if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) capture = KeyCapture{b, k};
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        settings.keys[b][k] = SDL_SCANCODE_UNKNOWN;
                        capture.reset();
                        view.settings_changed = true;
                    }
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Default keys")) {
            settings.reset_keys();
            capture.reset();
            view.settings_changed = true;
        }
    }
}

void settings_controllers_section(HostView& view) {
    auto& settings = view.settings;
    auto& capture = view.capture;
    if (ImGui::CollapsingHeader("Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushTextWrapPos(0.0f); // (the notes wrap at the window's edge)
        // The detected controllers, player by player (player 1 is also the
        // keyboard).
        const auto& known = view.pads.pads();
        ImGui::BulletText("Player 1: %s%s", known.empty() ? "Keyboard" : known.front().name.c_str(),
                          known.empty() ? "" : (std::string(" (") + family_name(known.front().family) + ") and the keyboard").c_str());
        for (std::size_t i = 1; i < known.size() && i < 4; ++i)
            ImGui::BulletText("Player %zu: %s (%s)", i + 1, known[i].name.c_str(), family_name(known[i].family));
        if (known.size() > 4) ImGui::TextDisabled("(%zu more controllers are not used: four players at most.)", known.size() - 4);
        // What is connected but not (yet) a player's pad.
        if (view.gc_adapter) {
            switch (view.gc_adapter->state()) {
            case GcAdapter::State::no_driver:
                ImGui::TextWrapped("GameCube adapter (Wii U mode) found, but it needs the WinUSB driver: install it on \"WUP-028\" with Zadig, "
                                   "as for Dolphin, then plug the adapter in again.");
                break;
            case GcAdapter::State::busy:
                ImGui::TextWrapped("GameCube adapter (Wii U mode) found, but another program (Dolphin?) is using it. Close that program; "
                                   "the game tries again every two seconds.");
                break;
            default: break;
            }
        }
        // The Switch 2 Pro Controller (switch2_pro.hpp): each one's state, and
        // why one does not play and what to do.
        for (const auto& device : view.switch2_devices) ImGui::TextDisabled("%s", switch2_status_line(device).c_str());
        const Switch2Notice switch2_now = view.switch2.current();
        if (const std::string text = switch2_notice_text(switch2_now, switch2_notice_device(switch2_now, view.switch2_devices)); !text.empty())
            ImGui::TextWrapped("%s", text.c_str());
        if (view.controller_log) ImGui::TextDisabled("Controller log: %s", view.controller_log->path().string().c_str());
        // Button prompts: automatic (player 1's last device) or one family.
        {
            static constexpr PadFamily kChoices[] = {PadFamily::ps5,         PadFamily::ps4,      PadFamily::xbox360,  PadFamily::xbox_one,
                                                     PadFamily::switch_pro,  PadFamily::switch2_pro, PadFamily::wiiu_pro, PadFamily::gamecube,
                                                     PadFamily::thor,        PadFamily::keyboard};
            static constexpr const char* kNames[] = {"Automatic", "DualSense", "DualShock 4", "Xbox 360", "Xbox One/Series", "Switch Pro",
                                                     "Switch 2 Pro", "Wii U Pro", "GameCube", "AYN Thor", "Keyboard"};
            int choice = 0;
            for (int i = 0; i < int(std::size(kChoices)); ++i)
                if (settings.prompts == family_id(kChoices[i])) choice = i + 1;
            if (ImGui::Combo("Button prompts", &choice, kNames, int(std::size(kNames)))) {
                settings.prompts = choice == 0 ? "auto" : family_id(kChoices[choice - 1]);
                view.settings_changed = true;
            }
            ImGui::TextDisabled("Automatic: every player's own controller; player 1's is the controller or keyboard used last.");
            // What the game's button pictures show now.
            prompt_text_disabled(std::string("Now: ") + family_name(view.prompt_family()) +
                                 "   {A} {B} {X} {Y} {START} {L} {R} {Z} {STICK} {CSTICK} {DPAD}");
            for (std::size_t p = 1; p < known.size() && p < 4; ++p)
                if (view.prompt_family(p) != view.prompt_family())
                    ImGui::TextDisabled("Player %zu: %s (the character select's prompts of that player)", p + 1, family_name(view.prompt_family(p)));
        }
        {
            int layout = settings.nintendo_layout == NintendoLayout::by_position ? 1 : 0;
            static const char* kLayouts[] = {"By label (the A button is A)", "By position (like an Xbox controller)"};
            if (ImGui::Combo("Nintendo layout", &layout, kLayouts, 2)) {
                settings.nintendo_layout = layout ? NintendoLayout::by_position : NintendoLayout::by_label;
                view.settings_changed = true;
            }
            ImGui::TextDisabled("Switch Pro, Switch 2 Pro, Wii U Pro controllers and the AYN Thor.");
            bool thor_pad = false;
            for (const auto& pad : known) thor_pad = thor_pad || pad.family == PadFamily::thor;
            if (thor_pad || settings.thor_style != ThorStyle::automatic) {
                // The Thor's own "Controller style" decides its face buttons'
                // codes; automatic follows it.
                int style = settings.thor_style == ThorStyle::standard ? 1 : settings.thor_style == ThorStyle::xbox ? 2 : 0;
                const std::string automatic = std::string("As the Thor's setting (") +
                                              (view.thor_device ? (*view.thor_device == ThorStyle::xbox ? "Xbox" : "Standard") : "unknown") + ")";
                const char* kStyles[] = {automatic.c_str(), "Standard", "Xbox"};
                if (ImGui::Combo("AYN Thor controller style", &style, kStyles, 3)) {
                    settings.thor_style = style == 1 ? ThorStyle::standard : style == 2 ? ThorStyle::xbox : ThorStyle::automatic;
                    view.settings_changed = true;
                }
                ImGui::TextDisabled("Thor Settings > Controller style (Standard: the A button sends A; Xbox: the bottom button sends A).");
            }
            int gc = settings.gamecube_layout == GameCubeLayout::bindings ? 1 : 0;
            static const char* kGameCube[] = {"Original (every button is its own)", "Use the bindings below"};
            if (ImGui::Combo("GameCube controller", &gc, kGameCube, 2)) {
                settings.gamecube_layout = gc ? GameCubeLayout::bindings : GameCubeLayout::original;
                view.settings_changed = true;
            }
            int menu = settings.menu_buttons == MenuButtons::bindings ? 1 : 0;
            static const char* kMenus[] = {"The controller's own A / B", "The game's A / B bindings"};
            if (ImGui::Combo("Menu buttons", &menu, kMenus, 2)) {
                settings.menu_buttons = menu ? MenuButtons::bindings : MenuButtons::layout;
                view.settings_changed = true;
            }
            prompt_text_disabled("This menu and the online screens: {OK} confirms, {BACK} goes back.");
            int wii = settings.wii_driver == WiiDriver::on ? 1 : settings.wii_driver == WiiDriver::off ? 2 : 0;
            static const char* kWii[] = {"Automatic (while one is connected)", "On", "Off"};
            if (ImGui::Combo("Wii U Pro support", &wii, kWii, 3)) {
                settings.wii_driver = wii == 1 ? WiiDriver::on : wii == 2 ? WiiDriver::off : WiiDriver::automatic;
                view.settings_changed = true;
            }
            ImGui::TextDisabled("(SDL's Wii driver, now %s. Wii remotes, also on a DolphinBar, never take a player's place.)",
                                view.wii_hint ? "on" : "off");
#ifndef SDL_PLATFORM_ANDROID
            if (ImGui::Checkbox("GameCube adapter in Wii U mode (WinUSB)", &settings.gc_adapter)) view.settings_changed = true;
            ImGui::TextDisabled("(released while the game is in the background, so Dolphin can use it then)");
#endif
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Click an input and press a controller button or trigger; right-click clears.");
        ImGui::TextDisabled("The left stick is the main stick (walks), the right stick the C-stick.");
        // The names of the inputs: the prompts' family when it is a pad,
        // else the first pad's.
        PadFamily binding_family = view.prompt_family();
        if (binding_family == PadFamily::keyboard) binding_family = known.empty() ? PadFamily::xbox_one : known.front().family;
        if (binding_family == PadFamily::gamecube && settings.gamecube_layout == GameCubeLayout::original)
            ImGui::TextDisabled("A GameCube controller plays 1:1 and does not use these bindings.");
        ImGui::TextDisabled("Shown as on: %s", family_name(binding_family));
        for (int group = 0; group < 4; ++group) {
            ImGui::SeparatorText(pad_group_name(PadGroup(group)));
            if (!ImGui::BeginTable(("pad" + std::to_string(group)).c_str(), 4, ImGuiTableFlags_SizingStretchSame)) continue;
            for (std::size_t b = 0; b < settings.pad.size(); ++b) {
                if (pad_key_group(PadKey(b)) != PadGroup(group)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pad_key_name(PadKey(b)));
                for (std::size_t k = 0; k < settings.pad[b].size(); ++k) {
                    ImGui::TableNextColumn();
                    const bool waiting = capture && capture->gamepad && capture->button == b && capture->slot == k;
                    // (Named as on the controller the bindings are shown for.)
                    std::string label = waiting ? "press..." : pad_input_label(binding_family, settings, settings.pad[b][k]);
                    if (label.empty()) label = "-";
                    label += "##pad" + std::to_string(b * 8 + k);
                    if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) capture = KeyCapture{b, k, true};
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        settings.pad[b][k] = {};
                        capture.reset();
                        view.settings_changed = true;
                    }
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Default controller layout")) {
            settings.reset_pad();
            capture.reset();
            view.settings_changed = true;
        }
        ImGui::PopTextWrapPos();
    }
}

// ---------------------------------------------------------------- the menus' size

namespace {
float g_ui_scale = 1.0f;
bool g_ui_style_kept = false;
ImGuiStyle g_ui_base_style; // the dark style at 1x (sizes are scaled from it, never from a scaled one)
ImFont* g_pixel_font = nullptr;
ImFont* g_scalable_font = nullptr;
} // namespace

void set_ui_fonts(ImFont* pixel, ImFont* scalable) {
    g_pixel_font = pixel;
    g_scalable_font = scalable;
}

float ui_scale() { return g_ui_scale; }

float update_ui_scale(SDL_Window* window, const Settings& settings, bool test_run) {
    if (!ImGui::GetCurrentContext()) return g_ui_scale;
#ifdef SDL_PLATFORM_ANDROID
    const bool follow = true;
#else
    const bool follow = false;
#endif
    int percent = settings.ui_scale;
    // (Test runs read no settings.ini: DW4_UI_SCALE=percent shows a scale,
    // e.g. the Thor's 231 on the PC.)
    if (test_run)
        if (const char* text = std::getenv("DW4_UI_SCALE")) percent = std::atoi(text);
    const float display_scale = window ? SDL_GetWindowDisplayScale(window) : 1.0f;
    const float scale = ui_scale_factor(percent, display_scale, follow);
    if (g_ui_style_kept && scale == g_ui_scale) return g_ui_scale;
    auto& style = ImGui::GetStyle();
    if (!g_ui_style_kept) {
        g_ui_base_style = style;
        g_ui_style_kept = true;
    }
    style = g_ui_base_style;
    if (scale != 1.0f) style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    // ImGui's pixel font is made for 13 px; scaled, its scalable one reads
    // better.
    ImGui::GetIO().FontDefault = scale != 1.0f && g_scalable_font ? g_scalable_font : g_pixel_font;
    g_ui_scale = scale;
    int width = 0, height = 0;
    if (window) SDL_GetWindowSizeInPixels(window, &width, &height);
    std::printf("ui: menus at %d %% (display scale %.2f, ui_scale %s), window %dx%d\n", int(std::lround(scale * 100.0f)), double(display_scale),
                percent > 0 ? std::to_string(percent).c_str() : "auto", width, height);
    std::fflush(stdout);
    return g_ui_scale;
}

// ---------------------------------------------------------------- touch

TouchRect touch_menu_rect(float width, float height) {
    const float side = ui_px(44.0f), margin = ui_px(10.0f);
    TouchRect r;
    r.x1 = width - margin;
    r.x0 = r.x1 - side;
    r.y0 = margin;
    r.y1 = r.y0 + side;
    // The bar right of the 4:3 picture: the button stands in its middle when
    // it fits there.
    const float bar = (width - height * 4.0f / 3.0f) * 0.5f;
    if (bar >= side + margin * 2.0f) {
        r.x0 = width - bar * 0.5f - side * 0.5f;
        r.x1 = r.x0 + side;
    }
    (void)height;
    return r;
}

void draw_touch_menu_button(HostView& view) {
    if (!view.touch_ui || !view.window) return;
    const auto now = std::chrono::steady_clock::now();
    const double since = std::chrono::duration<double>(now - view.last_touch).count();
    float alpha = 1.0f;
    if (!view.show_menu) {
        if (since >= kTouchButtonSeconds + 0.5) return;
        if (since > kTouchButtonSeconds) alpha = float(1.0 - (since - kTouchButtonSeconds) / 0.5);
    }
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const TouchRect r = touch_menu_rect(size.x, size.y);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const auto a = [&](float value) { return int(std::lround(std::clamp(value * alpha, 0.0f, 1.0f) * 255.0f)); };
    const float side = r.x1 - r.x0;
    draw->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), IM_COL32(20, 22, 30, a(0.62f)), side * 0.18f);
    draw->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), IM_COL32(220, 224, 236, a(0.75f)), side * 0.18f, 0, std::max(1.0f, side * 0.04f));
    // Three lines (the menu).
    const float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f, w = side * 0.5f, gap = side * 0.17f, thick = std::max(2.0f, side * 0.07f);
    for (int i = -1; i <= 1; ++i)
        draw->AddLine(ImVec2(cx - w * 0.5f, cy + gap * float(i)), ImVec2(cx + w * 0.5f, cy + gap * float(i)), IM_COL32(240, 242, 248, a(0.95f)), thick);
}

// ---------------------------------------------------------------- display rate, lifecycle

// ---------------------------------------------------------------- text input

namespace {
#ifdef SDL_PLATFORM_ANDROID
using ImeDataFn = void (*)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*);
ImeDataFn g_backend_ime = nullptr;
bool g_secret_input = false;
// The system keyboard on the screen (SDL's SHOWN / HIDDEN events, from the
// window's insets; SDL_ScreenKeyboardShown stays true while the input
// connection lives, also after the player put the keyboard away).
bool g_keyboard_visible = false;

// Before the backend: a password field's text input starts with SDL's
// password type and without autocorrection (Android's keyboard then offers
// no suggestions and learns nothing); the backend sees it active and only
// places it.
void ime_data(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
    const ImGuiContext& g = *context;
    const bool wants = data->WantVisible || data->WantTextInput;
    const bool secret = wants && g.InputTextState.ID != 0 && g.InputTextState.ID == g.ActiveId &&
                        (g.InputTextState.Flags & ImGuiInputTextFlags_Password) != 0;
    if (std::getenv("DW4_TRACE_IME"))
        std::printf("text input: ime visible %d text %d, field %08x active %08x flags %x focus %p\n", int(data->WantVisible), int(data->WantTextInput),
                    unsigned(g.InputTextState.ID), unsigned(g.ActiveId), unsigned(g.InputTextState.Flags), static_cast<void*>(SDL_GetKeyboardFocus()));
    if (SDL_Window* window = SDL_GetKeyboardFocus(); window && wants && secret != g_secret_input) {
        const bool was_active = SDL_TextInputActive(window);
        if (was_active) SDL_StopTextInput(window);
        if (secret) {
            const SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN);
            SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
            SDL_StartTextInputWithProperties(window, props);
            SDL_DestroyProperties(props);
        } else {
            SDL_StartTextInput(window);
        }
        // (The keyboard keeps the last field's kind until its input restarts.)
        android_restart_text_input();
        std::printf("text input: %s field, the system keyboard%s\n", secret ? "a password" : "a text", was_active ? " restarted" : "");
        std::fflush(stdout);
        g_secret_input = secret;
    }
    if (!wants) g_secret_input = false;
    if (g_backend_ime) g_backend_ime(context, viewport, data);
}
#endif
} // namespace

void note_screen_keyboard(bool shown) {
#ifdef SDL_PLATFORM_ANDROID
    if (std::getenv("DW4_TRACE_IME")) std::printf("text input: the screen keyboard %s\n", shown ? "shown" : "hidden");
    g_keyboard_visible = shown;
#else
    (void)shown;
#endif
}

void reopen_text_input(SDL_Window* window, float x, float y) {
#ifdef SDL_PLATFORM_ANDROID
    // (Back on the keyboard also stops SDL's text input: SDLDummyEdit's
    // onKeyPreIme. ImGui's field stays active, and its backend starts the
    // input only when the field's IME data change.)
    if (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantTextInput) return;
    if (SDL_TextInputActive(window) && g_keyboard_visible) return;
    // Only a tap on the active field's line (a tap elsewhere leaves it, and
    // the keyboard would only flash up).
    const ImGuiPlatformImeData& ime = GImGui->PlatformImeData;
    const float line = std::max(ime.InputLineHeight, 1.0f);
    if (y < ime.InputPos.y - line * 0.75f || y > ime.InputPos.y + line * 1.75f) return;
    (void)x;
    if (SDL_TextInputActive(window)) SDL_StopTextInput(window);
    if (g_secret_input) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_StartTextInputWithProperties(window, props);
        SDL_DestroyProperties(props);
    } else {
        SDL_StartTextInput(window);
    }
    android_restart_text_input();
    std::printf("text input: the system keyboard again (a tap on the field after the keyboard was put away)\n");
    std::fflush(stdout);
#else
    (void)window, (void)x, (void)y;
#endif
}

void install_text_input_hook() {
#ifdef SDL_PLATFORM_ANDROID
    auto& platform = ImGui::GetPlatformIO();
    if (platform.Platform_SetImeDataFn == ime_data) return;
    g_backend_ime = platform.Platform_SetImeDataFn;
    platform.Platform_SetImeDataFn = ime_data;
#endif
}

void request_frame_rate(const Settings& settings) {
#ifdef SDL_PLATFORM_ANDROID
    const float hz = settings.fps_limit > 60 ? float(std::min(settings.fps_limit, 120)) : 60.0f;
    static float asked = 0;
    if (hz == asked) return;
    asked = hz;
    android_set_frame_rate(hz);
#else
    (void)settings;
#endif
}

namespace {
std::atomic<unsigned> g_lifecycle_notes{0};
}

void note_lifecycle(LifecycleNote note) { g_lifecycle_notes.fetch_or(unsigned(note)); }

bool take_lifecycle(LifecycleNote note) { return (g_lifecycle_notes.fetch_and(~unsigned(note)) & unsigned(note)) != 0; }

} // namespace dw4::game
