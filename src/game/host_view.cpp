#include "host_view.hpp"

#include "button_prompts.hpp"
#include "controls.hpp"
#include "gc_adapter.hpp"
#include "ghost_figures.hpp"
#include "online/presence.hpp"
#include "pad_host.hpp"

#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

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

void controller_hints(const Settings& settings, bool test_run) {
    // (The reasons stand with the values: pad_usb.cpp.)
    for (const auto& hint : controller_hint_values(settings, test_run)) SDL_SetHint(hint.name, hint.value);
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

bool host_event(HostView& view, const SDL_Event& event) {
    auto& settings = view.settings;
    auto& capture = view.capture;
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
        toggle_menu(view);
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) toggle_menu(view);
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11 && !event.key.repeat) {
        settings.fullscreen = !settings.fullscreen;
        SDL_SetWindowFullscreen(view.window, settings.fullscreen);
        view.settings_changed = true;
    }
    return false;
}

void update_controllers(HostView& view) {
    if (view.test_run || !view.draw) return;
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
        // The Switch 2 Pro Controller: Windows' list (the adapter reader's
        // look at it) against what SDL opened.
        const std::uint32_t scans = view.gc_adapter->scans();
        const UsbFindings windows = view.gc_adapter->findings();
        bool sdl_pad = false;
        for (auto* pad : view.gamepads) sdl_pad = sdl_pad || sdl_switch2_pad(pad);
        const Switch2Notice notice = view.switch2.update(std::chrono::steady_clock::now(), windows, sdl_pad, view.libusb.state);
        if (view.controller_log) {
            // (The first look, then every change.)
            if (scans != view.logged_scans && (view.logged_scans == 0 || windows != view.logged_findings)) {
                view.logged_findings = windows;
                view.controller_log->write(usb_findings_line(windows));
            }
            view.logged_scans = scans;
            if (notice != view.logged_notice) {
                view.logged_notice = notice;
                view.controller_log->write(std::string("switch 2 pro: ") + switch2_notice_name(notice));
            }
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

bool note_stage(HostView& view, engine::Engine& engine, GhostFigures* ghosts) {
    auto& picture = view.picture;
    const std::uint64_t serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
    if (picture.rendered_stage != engine.stage_name() || picture.rendered_serial != serial) {
        if (view.renderer) view.renderer->clear_cache();
        picture.rendered_stage = engine.stage_name();
        picture.rendered_serial = serial;
        view.interpolator.reset();
        view.labels.reset();
        if (ghosts) ghosts->clear(); // (a new stage: no figure fades across the load)
        return true;
    }
    return false;
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
    const MenuKeys keys = menu_keys(view.settings, family, read_gamepad(pad), family != PadFamily::gamecube || trigger_clicks(pad));
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

void finish_present(HostView& view, engine::Engine& engine, const std::function<void()>& swapped) {
    // Keep about three frames of audio queued across frame hitches.
    if (view.audio) {
        const int queued = SDL_GetAudioStreamQueued(view.audio) / int(2 * sizeof(std::int16_t));
        if (queued < kAudioFrameSamples * 2) mix_audio(view, engine, kAudioFrameSamples * 3 - queued);
    }
    SDL_GL_SwapWindow(view.window);
    if (swapped) swapped();
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
        if (ImGui::Checkbox("Fullscreen (F11)", &settings.fullscreen)) {
            SDL_SetWindowFullscreen(view.window, settings.fullscreen);
            view.settings_changed = true;
        }
        if (ImGui::Checkbox("Widescreen (the 3D view fills wide windows, 2D stays 4:3)", &settings.widescreen)) view.settings_changed = true;
        if (ImGui::Checkbox("VSync", &settings.vsync)) {
            SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
            view.settings_changed = true;
        }
        if (ImGui::SliderInt("Frame limit", &settings.fps_limit, 0, 300, settings.fps_limit ? "%d fps" : "off")) view.settings_changed = true;
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
                             overlay, 0.0f, std::max(33.4f, worst), ImVec2(0, 48));
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
    }
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
            // A Switch 2 Pro Controller that does not play: why and what to do.
            if (const std::string text = switch2_notice_text(view.switch2.current(), view.gc_adapter->findings()); !text.empty())
                ImGui::TextWrapped("%s", text.c_str());
        }
        // libusb-1.0.dll (the Switch 2 Pro Controller over USB).
        ImGui::TextDisabled("%s", libusb_summary(view.libusb).c_str());
        if (view.controller_log) ImGui::TextDisabled("Controller log: %s", view.controller_log->path().string().c_str());
        // Button prompts: automatic (player 1's last device) or one family.
        {
            static constexpr PadFamily kChoices[] = {PadFamily::ps5, PadFamily::ps4, PadFamily::xbox360, PadFamily::xbox_one, PadFamily::switch_pro,
                                                     PadFamily::switch2_pro, PadFamily::wiiu_pro, PadFamily::gamecube, PadFamily::keyboard};
            static constexpr const char* kNames[] = {"Automatic", "DualSense", "DualShock 4", "Xbox 360", "Xbox One/Series", "Switch Pro",
                                                     "Switch 2 Pro", "Wii U Pro", "GameCube", "Keyboard"};
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
            ImGui::TextDisabled("Switch Pro, Switch 2 Pro and Wii U Pro controllers.");
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
            if (ImGui::Checkbox("GameCube adapter in Wii U mode (WinUSB)", &settings.gc_adapter)) view.settings_changed = true;
            ImGui::TextDisabled("(released while the game is in the background, so Dolphin can use it then)");
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

} // namespace dw4::game
