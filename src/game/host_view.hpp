#pragma once

// The host's parts that work on any engine (docs/SURVIVAL.md 13.3, round O1a
// lane A): the window's events, this machine's controllers and keyboard as
// pads, the sound output, the picture (draw list, smooth motion, the
// renderer, name tags, films, the kept picture), the ImGui frame around it
// and the F1 menu's settings sections. game_main's loop calls them with the
// story engine exactly as before (the statements moved out of run() in
// their order, nothing else changed); the Data Overflow run loop
// (overflow_host.hpp) calls the same functions with its own engine, so
// texture packs, widescreen, smooth motion, name tags and the F1 settings
// never drift apart between the two loops.

#include "dev_present.hpp"
#include "gl_renderer.hpp"
#include "interpolation.hpp"
#include "movie_decoder.hpp"
#include "overhead_labels.hpp"
#include "pad_family.hpp"
#include "settings.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/pad.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct ImFont;

namespace dw4::game {

class ButtonPrompts;
class GcAdapter;
class GhostFigures;
namespace online {
struct RemoteSample;
}

// A key or controller button of the controls tables waiting for input.
struct KeyCapture {
    std::size_t button{};
    std::size_t slot{};
    bool gamepad{};
};

// One NTSC field of output at 48 kHz is 800.8 frames.
inline constexpr int kAudioFrameSamples = engine::SoundManager::kOutputRate / 60;

// Output samples of the step that reached `frame`: one NTSC field (800.8 at
// 48 kHz), so music and movie sound keep pace with world time.
inline int step_samples(std::uint64_t frame) {
    constexpr std::uint64_t rate = engine::SoundManager::kOutputRate;
    const auto upto = [](std::uint64_t f) { return f * rate * 1001 / 60000; };
    return int(upto(frame) - upto(frame ? frame - 1 : 0));
}

// The picture of the last update and what the pictures keep between passes
// (an engine switch starts it over: reset()).
struct PictureState {
    std::vector<engine::DrawItem> items;
    engine::ShadowFrame shadow;
    engine::FrameCamera camera;
    engine::Vec4f clear{};
    bool items_current = false; // `items` is the draw list of the last update
    std::string rendered_stage;
    std::uint64_t rendered_serial{};
    std::unique_ptr<MovieDecoder> movie_decoder;
    std::uint32_t movie_serial = 0;
    std::string movie_stage;    // stage the decoder's movie played in
    int last_picture_slot = -1; // Engine::picture_slot of the last picture (smooth motion restarts on a change)
    // A picture's list and shadow with the story's ghost figures in
    // (present_scene's copies; kept here, as the renderer keeps the shadow's
    // address). Empty in a Data Overflow run.
    std::vector<engine::DrawItem> ghost_items;
    engine::ShadowFrame ghost_shadow;
    void reset();
};

// The story loop's online presentation for present_scene (round 60's ghost
// figures, docs/SEAMLESS.md 12): the room members' figures and their presence
// samples of this pass. A Data Overflow run passes none: nothing online
// reaches a run (OVERFLOW_HOST.md 3).
struct RemotePresentation {
    GhostFigures* ghosts = nullptr;                             // drawn into a copy of the picture's list and shadow
    const std::vector<online::RemoteSample>* samples = nullptr; // the remote tags stand where their figures do
    std::optional<engine::Vec3f> own_position;                  // this machine's player (the figures' camera fade)
    double* cost_max = nullptr;                                 // DW4_TRACE_GHOSTS: the figures' own milliseconds a pass
    // Data Overflow's lobby (round O1b): the NPC and the dialog's 2D items,
    // drawn like the figures into the copy at the present's own time.
    std::function<void(std::vector<engine::DrawItem>&, engine::ShadowFrame*, const engine::FrameCamera&, double)> overlay;
};

// The window, its renderer, the sound output, the controllers, the F1 menu's
// state and the pictures' state of the process: one for the story engine
// and every Data Overflow run.
struct HostView {
    HostView(Settings& host_settings, const DevPresent& host_dev) : settings(host_settings), dev(host_dev) {}
    HostView(const HostView&) = delete;
    HostView& operator=(const HostView&) = delete;

    Settings& settings;
    const DevPresent& dev;
    bool test_run = false; // --smoke-test
    bool draw = false;     // pictures are drawn (a window and an OpenGL context)
    SDL_Window* window = nullptr;
    GlRenderer* renderer = nullptr; // null without pictures
    int samples = 0;                // the window's multisampling
    ImFont* tag_font = nullptr;     // the overhead name tags' font

    // Sound: the playback stream (none in test runs), DW4_AUDIO_DUMP's file
    // and record, DW4_AUDIO_MIX (test runs mix without a device).
    SDL_AudioStream* audio = nullptr;
    const char* audio_dump_path = nullptr;
    bool audio_mix = false;
    std::vector<std::int16_t> audio_dump;
    // The host's own gain on top of the volume setting (the Data Overflow
    // fades); at 1 the mix is the one before it existed.
    float gain = 1.0f;

    // Controllers; after a menu closed the game's pad waits until every
    // button is up (the B that closed it must not also cancel the game's menu).
    std::vector<SDL_Gamepad*> gamepads;
    bool pad_latch = false;
    // The controllers' families and the device player 1 used last
    // (pad_family.hpp; the prompts follow it).
    PromptTracker pads;
    // The prompts' pictures (null without a window) and a test run's
    // families (DW4_PROMPTS=<family id>[,<family id> ...]: one for every
    // player, or player 1's, 2's ... with the last one for the rest; test
    // runs show the original pictures otherwise).
    ButtonPrompts* prompts = nullptr;
    std::vector<PadFamily> forced_prompts;
    // The Wii U / Switch GameCube adapter through WinUSB (gc_adapter.hpp;
    // null in test runs and without a window): its ports become SDL pads.
    GcAdapter* gc_adapter = nullptr;
    // update_controllers: SDL's HID device change count when the 057e:0330
    // devices were last looked for, whether one is connected, and the Wii
    // driver hint as set.
    std::uint32_t hid_changes = 0;
    bool wii_u_ids = false;
    bool wii_hint = false;

    // The F1 menu.
    bool show_menu = false;
    std::string menu_tab; // a tab to select when it first shows (DW4_MENU_TAB)
    std::optional<KeyCapture> capture;
    bool settings_changed = false;
    bool quit_requested = false; // the window was closed

    // Pictures.
    PictureState picture;
    MotionInterpolator interpolator; // smooth motion above 60 presents per second
    OverheadLabels labels;           // overhead name tags
    int trace_labels = 0;            // DW4_TRACE_LABELS
    std::set<std::uint64_t> traced_occlusion; // the labels the last picture hid (traces)

    // Presents.
    double present_interval = 1.0 / 60.0;
    double last_present_seconds = 0; // the last present's own interval (DW4_TRACE_GHOSTS' longest present)
    std::array<float, 240> frame_times{}; // ms per presented frame (F1 graph)
    std::size_t frame_time_at = 0;
    std::chrono::steady_clock::time_point last_present, next_present;

    // The name tags' mode (settings.ini [hud]; test runs: DW4_LABELS, all
    // with DW4_TRACE_LABELS, else off).
    LabelMode label_mode() const;
    // Smooth motion draws between the previous step and this one.
    bool smooth_blending() const;
    // The family a player's prompts show now (0..3): settings.ini [gamepad]
    // prompts for every player, or automatic: player 1's last device, a
    // further player's own pad (player 1's family for an empty slot).
    PadFamily prompt_family(std::size_t player = 0) const;
    // A connected pad's family (generic when unknown).
    PadFamily family_of(SDL_Gamepad* pad) const;
};

// ---- Window events and input
// One window event before the keys only the story loop handles (F5, F9,
// F2): a key or controller binding being edited takes it (true: nothing
// else sees it); else ImGui, quit, the window's size, controllers coming
// and going, the View button and F1 (the menu), F11 (fullscreen).
bool host_event(HostView& view, const SDL_Event& event);
// The F1 menu opens or closes (F1, the controller's View / Back button).
void toggle_menu(HostView& view);
// This machine's devices as the game's pads take them: player 1 is the
// keyboard and the first gamepad, players 2-4 the further gamepads, each on
// its own controller slot (the main stick, the D-pad and the C-stick stay
// separate inputs, controls.hpp); nothing while pad_latch waits for every
// button to be up.
std::array<HostPadInput, 4> read_live_input(HostView& view);
// Player 1's live sample as the story loop composes it without input
// scripts and bots: movement keys push a centred main stick fully.
engine::PadSample live_pad_sample(const HostPadInput& live);

// ---- Sound
// Mixes `frames` output frames of the engine's sound (silent while the
// console reboots) at the master volume and the host gain into the playback
// stream and DW4_AUDIO_DUMP's record.
void mix_audio(HostView& view, engine::Engine& engine, int frames);

// ---- The picture of a pass
// Every stage load, also of the same stage (the texture cache is keyed by
// document addresses, which a new load may reuse): the renderer's cache,
// smooth motion, the name tags and the story's ghost figures (`ghosts`; none
// in a run) start over. True when it did.
bool note_stage(HostView& view, engine::Engine& engine, GhostFigures* ghosts = nullptr);
enum class PictureMade { kept, drawn, traversed };
// The draw list of this pass (drawn: view.picture.items, which the caller
// may still add 2D items to), or without pictures the traversal of it; a
// field the running update covers (Engine::held) keeps the last picture.
PictureMade make_picture(HostView& view, engine::Engine& engine, int steps, bool smooth_blending);

// What present_scene drew, for the ImGui frame and the dumps after it.
struct PresentedScene {
    int width = 0, height = 0;
    Viewport screen, wide;
    float present_alpha = 0;
    BlendReport blend_report;
    LabelMode tag_mode = LabelMode::off;
    bool tags_shown = false;
    std::vector<OverheadLabel> tags;
    LabelView tag_view;
};
// The picture into the window: smooth motion's blend, the 4:3 screen and the
// widescreen viewport, the scene, the name tags' places and occlusion, a
// playing film, the kept picture (after a stage change the last picture
// stays until the loader draws). `remote` (the story loop online): the ghost
// figures at the present's own time in a copy of the list and shadow, the
// remote tags at their presence samples.
PresentedScene present_scene(HostView& view, engine::Engine& engine, double accumulator, bool smooth_blending, bool report_blend,
                             const RemotePresentation* remote = nullptr);
// ImGui's keyboard and controller navigation while a menu takes the input
// (never while the game reads the pad); `cursor_always`: the cursor shows at
// once (the game's own menus do).
void set_menu_navigation(bool menus_take_input, bool cursor_always);
// The view whose first controller drives the menus (ImGui's gamepad keys
// come from it through pad_host.hpp's menu_keys: the pad's own A / Cross
// activates and B / Circle goes back, or with menu_buttons=bindings the
// game's A and B) and whose prompts the ImGui texts draw.
void set_ui_view(HostView* view);
// Once per frame with a window and outside test runs: SDL's Wii driver
// (settings.ini wii_driver; automatic: on while a 057e:0330 device is
// connected, looked for when SDL counts a device change) and the GameCube
// adapter's reader (on while the setting allows it and the window has the
// input focus, so Dolphin can take the adapter while this game is in the
// background).
void update_controllers(HostView& view);
// A new ImGui frame (`test_gamepad`: a UI test run's controller keys count
// as a controller's).
void new_ui_frame(bool test_gamepad);
// A pad's input through the bindings in its family's binding space (a
// GameCube pad on its original layout: 1:1).
HostPadInput pad_input(const HostView& view, SDL_Gamepad* pad);
// Overhead name tags over the picture, under the menus.
void draw_tags(HostView& view, const PresentedScene& scene);
// ImGui's draw data over the picture.
void render_ui();
// The window's pixels as a BMP file.
void write_screenshot(const std::string& path, int width, int height);
void write_bmp(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba);
// After the menus: about three frames of sound queued across hitches, the
// swap (`swapped` right after it: the story loop's online session notes the
// present), a minimized or covered window's 4 ms pause (its swap does not
// wait for the display), the present interval and the F1 frame time graph.
void finish_present(HostView& view, engine::Engine& engine, const std::function<void()>& swapped = {});
// Frame limit (settings.ini fps_limit): presents are spaced out; the
// simulation keeps its fixed 60 Hz steps.
void pace_presents(HostView& view);

// ---- The F1 menu's settings sections (inside the Settings tab)
void settings_display_section(HostView& view);
void settings_audio_section(HostView& view);
// `discord_status`: DiscordPresence::status(), "off" without one.
void settings_presence_section(HostView& view, const char* discord_status);
void settings_keyboard_section(HostView& view);
void settings_controllers_section(HostView& view);
// The SDL hints for the controller families, before SDL_Init: SDL's Wii
// driver only with wii_driver=on (automatic turns it on later, while a Wii
// U Pro Controller's ids are connected: update_controllers).
void controller_hints(const Settings& settings);

} // namespace dw4::game
