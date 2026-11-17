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
#include "menu_ops.hpp"
#include "movie_decoder.hpp"
#include "overhead_labels.hpp"
#include "pad_family.hpp"
#include "pad_host.hpp"
#include "pad_usb.hpp"
#include "settings.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/pad.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct ImFont;
struct ImGuiContext;

namespace dw4::game {

class ButtonPrompts;
class GcAdapter;
class GhostFigures;
class Switch2Reader;
class TexturePack;
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
    std::uint64_t movie_drawn = 0; // the decoder's last picture drawn (MovieDecoder::picture_id; DW4_FRAME_STATS counts each once)
    int last_picture_slot = -1; // Engine::picture_slot of the last picture (smooth motion restarts on a change)
    // A picture's list and shadow with the story's ghost figures in
    // (present_scene's copies; kept here, as the renderer keeps the shadow's
    // address). Empty in a Data Overflow run.
    std::vector<engine::DrawItem> ghost_items;
    engine::ShadowFrame ghost_shadow;
    // The engine's documents as last handed to the renderer for texture
    // streaming (a digest of their addresses; note_stage).
    std::uint64_t documents_digest{};
    std::size_t documents_count{};
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
    // The Switch 2 Pro Controller reader (switch2_pro.hpp; null in test runs
    // unless DW4_TEST_SWITCH2=1) and every controller it has, its notice (F1
    // > Controls; game_main shows it once in play), and
    // saves/controllers.log (null in test runs).
    Switch2Reader* switch2_reader = nullptr;
    std::vector<Switch2Status> switch2_devices;
    Switch2Watch switch2;
    ControllerLog* controller_log = nullptr;
    UsbFindings findings;          // (the adapter reader's latest look at the device list)
    std::uint32_t findings_scans = 0;
    UsbFindings logged_findings;   // (the device list as last written to the log)
    std::uint32_t logged_scans = 0;
    Switch2Notice logged_notice = Switch2Notice::none;

    // The F1 menu.
    bool show_menu = false;
    // It shows on the second screen (its Menu tab, screen2.hpp), where it was
    // opened by touch; the window shows the game meanwhile. Closing it ends
    // that.
    bool menu_on_screen2 = false;
    std::string menu_tab; // a tab to select when it first shows (DW4_MENU_TAB)
    std::string menu_section; // a Settings section scrolled to when it first shows (DW4_MENU_SECTION: Graphics)
    std::optional<KeyCapture> capture;
    // The console F1 menu (console_menu.hpp) shows F1: set while F1 is
    // closed for its next opening (Android; a PC test run with
    // DW4_MENU_STYLE=console), off for an opening where Developer > Classic
    // menu was chosen. host_event latches the pad when it closes such a menu.
    bool console_menu = false;
    // Keys pressed while the console menu shows (host_event; the menu takes
    // them every pass).
    std::vector<SDL_Keycode> console_keys;
    // The menu script's virtual pad (menu_script.hpp): player 1's menu keys
    // come from it instead of the first pad.
    SDL_Gamepad* menu_pad_override = nullptr;
    // The console menu's binding capture shows (also while it waits for the
    // button that chose the slot to be let go): the Back key cancels it.
    bool console_capturing = false;
    // Counts the captures the Back key cancelled (the console menu sees it).
    std::uint32_t capture_cancels = 0;
    bool settings_changed = false;
    bool quit_requested = false; // the window was closed
    // Another window has the focus: on Android the notification shade, the
    // quick settings, a dialog or the app switcher's live tile while the game
    // stays on the screen (so Android does not pause it), on the PC another
    // program (Alt+Tab) or the game's window minimized; the loop pauses an
    // offline game meanwhile when [menu] pause_unfocused says so.
    bool unfocused = false;

    // Pictures.
    PictureState picture;
    MotionInterpolator interpolator; // smooth motion above 60 presents per second
    OverheadLabels labels;           // overhead name tags
    int trace_labels = 0;            // DW4_TRACE_LABELS
    std::set<std::uint64_t> traced_occlusion; // the labels the last picture hid (traces)

    // The menu button for touch (Android, or DW4_TOUCH_UI=1): shown for a
    // few seconds after a touch and while the F1 menu is open
    // (draw_touch_menu_button); the last touch anywhere on the window.
    bool touch_ui = false;
    std::chrono::steady_clock::time_point last_touch;
    // The AYN Thor's controller style as the device reports it (Android's
    // persist.sys.gamepad.type, read again every second; nullopt: unknown)
    // and the one the Thor's pads are read with now (update_controllers).
    std::optional<ThorStyle> thor_device;
    ThorStyle thor_used = ThorStyle::standard;
    std::chrono::steady_clock::time_point thor_checked;

    // Presents.
    double present_interval = 1.0 / 60.0;
    double last_present_seconds = 0; // the last present's own interval (DW4_TRACE_GHOSTS' longest present)
    std::array<float, 240> frame_times{}; // ms per presented frame (F1 graph)
    std::size_t frame_time_at = 0;
    std::chrono::steady_clock::time_point last_present, next_present;
    // The pass's start (the loop sets it): DW4_FRAME_STATS measures the
    // pass's own work up to the swap and the swap's wait.
    std::chrono::steady_clock::time_point pass_started;

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
// ... with the input in the log ("menu: open (the Back key)").
void toggle_menu_logged(HostView& view, const char* input);
// Android's Back key (the AYN Thor's): F1 opens and closes; while the
// console menu captures a binding it only cancels the capture.
void menu_back_key(HostView& view);
// Player 1's menu keys (pad_host.hpp menu_keys) from the menu script's
// virtual pad or the first pad; none without a pad.
std::optional<MenuKeys> player1_menu_keys(HostView& view);
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
// in a run) start over. True when it did. Every pass it also hands the
// renderer the engine's documents for texture pack streaming (scanned and
// read ahead while a stage loads) and lets it upload what was read
// (GlRenderer::stream_textures). DW4_TRACE_TEXTURES=1: a line per stage
// with its texture memory and streaming figures.
bool note_stage(HostView& view, engine::Engine& engine, GhostFigures* ghosts = nullptr);
// DW4_TRACE_TEXTURES=1: the texture line of `stage` (note_stage prints it
// at every stage change, the story loop once more at its end).
void trace_textures(HostView& view, const std::string& stage);
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
// (Its part without the Switch 2 Pro Controller: SDL's Wii driver and the
// GameCube adapter; not in test runs.)
void update_sdl_and_adapter(HostView& view);
// A new ImGui frame (`test_gamepad`: a UI test run's controller keys count
// as a controller's).
void new_ui_frame(bool test_gamepad);
// Player 1's controller as ImGui's gamepad keys into the current ImGui
// context (new_ui_frame does it for the window's; the second screen's context
// takes them while the F1 menu shows there).
void feed_menu_pad(HostView& view);
// Whether player 1's controller holds any of the menus' keys now (a button,
// the D-pad, a trigger or a stick off its centre): the second screen's F1
// menu draws at once then, else every other pass.
bool menu_pad_held(HostView& view);
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
// DW4_FRAME_STATS: one engine step's time (the story loop around
// Engine::step; nothing is kept without the switch).
void frame_stats_step(double ms);
// DW4_FRAME_STATS: one frame of the second screen after the window's swap
// (screen2.hpp): its time and whether it drew and swapped its surface.
void frame_stats_screen2(double ms, bool swapped);
// DW4_FRAME_STATS: a movie picture the window drew for the first time: its
// decoding and RGBA conversion (MovieDecoder's worker), how long the pass
// waited for it, and its upload and draw in the pass.
void frame_stats_movie(double decode_ms, double convert_ms, double waited_ms, double upload_ms);

// ---- The F1 menu's one apply layer (menu_ops.hpp) with this view's side
// effects: the swap interval, the display's rate, the renderer's graphics,
// the Thor's controller style.
ops::Target host_ops(HostView& view);
// The anti-aliasing levels the GPU offers (menu_choices.hpp kMsaaValues'
// first ones; GL_MAX_SAMPLES) and the texture filters (TextureFilter's first
// ones; anisotropic only as far as the GPU goes).
int msaa_levels_offered();
int texture_filters_offered(const GlRenderer::Caps& caps);

// ---- The F1 menu's picture hooks (test runs only, docs/CONSOLE_MENU.md:
// the proof that the PC's F1 menu stays as it was): DW4_MENU_EXPAND=1 every
// collapsing header and tree node of F1 open, DW4_MENU_STILL=1 the live
// values fixed (the frame time graph at 16.67 ms, "now 60 fps", the
// texture counts, the play time and the Debug tab's frame 0). Set once by
// game_main in a test run; off otherwise (the calls are then the same as
// before).
void set_menu_picture_hooks(bool expand, bool still);
bool menu_still();
// An F1 collapsing header / tree node's flags (ImGuiTreeNodeFlags): with
// DW4_MENU_EXPAND also DefaultOpen.
int f1_tree_flags(int flags = 0);

// ---- The F1 menu's settings sections (inside the Settings tab)
void settings_display_section(HostView& view);
// Graphics: texture quality, filtering, LOD bias, sharpening, shadow
// quality, texture memory and the texture pack (`pack`: its folder, null
// without a renderer; `reload` reads the folder again).
struct TexturePackPanel {
    const TexturePack* pack = nullptr;
    std::filesystem::path folder;  // the folder in use
    std::function<void()> reload;  // reads the folder again (PackLoader), then GlRenderer::set_texture_pack
    bool loading = false;          // a reload is reading the folder
};
void settings_graphics_section(HostView& view, const TexturePackPanel& panel);
void settings_audio_section(HostView& view);
// `discord_status`: DiscordPresence::status(), "off" without one.
void settings_presence_section(HostView& view, const char* discord_status);
void settings_keyboard_section(HostView& view);
void settings_controllers_section(HostView& view);
// ---- The menus' size (settings.hpp ui_scale_factor)
// The ImGui fonts the menus take: ImGui's pixel font at 13 px (scale 1, the
// PC's look) and its scalable one (any other scale). Set once the window's
// ImGui has them (open_game_window).
void set_ui_fonts(ImFont* pixel, ImFont* scalable);
// ImGui's scalable font (the console menu draws with it at every scale; it is
// in the atlas on the PC too); null before set_ui_fonts.
ImFont* ui_scalable_font();
// The menus' scale for the window (settings.ini ui_scale; auto: the
// display's scale on Android, 1 on the PC; test runs: DW4_UI_SCALE=percent)
// into ImGui's style: every size of the dark style scaled, the font size,
// the scalable font above 1. Called before every ImGui frame; only a change
// does anything (logged as "ui: menus at N %"). Returns the scale.
float update_ui_scale(SDL_Window* window, const Settings& settings, bool test_run);
// The scale in use (1 until update_ui_scale set another).
float ui_scale();
// The second screen's ImGui context (screen2.hpp) at its own scale: the dark
// style at 1x scaled to `scale` and the font that scale takes, into the
// current context; and the scale ui_px uses while it draws (returns the one
// before).
void apply_ui_style(float scale);
float exchange_ui_scale(float scale);
// A length of the menus' layout at the scale in use (12 px at 1x).
inline float ui_px(float pixels) { return pixels * ui_scale(); }

// ---- Text input
// The system keyboard for the menus' text fields: ImGui's SDL backend starts
// SDL's text input while a field is active, which opens Android's keyboard
// (no hardware keyboard: SDL shows its screen keyboard). Android only: a
// password field opens it as a password field's (no suggestions, nothing
// learned). Once, after ImGui's SDL backend started.
void install_text_input_hook();
// Android: a tap on the active field (window pixels) after the player put
// the keyboard away (Back, the keyboard's own button) brings the keyboard
// back (the field stays active for ImGui, so its backend would not start
// the text input again).
void reopen_text_input(SDL_Window* window, float x, float y);
// (SDL's SCREEN_KEYBOARD_SHOWN / HIDDEN events: whether the keyboard is on
// the screen now.)
void note_screen_keyboard(bool shown);
// Whether the system keyboard is on the screen (the last SHOWN / HIDDEN;
// false on the PC).
bool screen_keyboard_visible();
// Whether the context's active item is a password field (an InputText with
// ImGuiInputTextFlags_Password).
bool password_field_active(const ImGuiContext& context);
// ... a number field (ImGuiInputTextFlags_CharsDecimal: the console menu's).
bool numeric_field_active(const ImGuiContext& context);
// ... a plain field: a name or a path (the console menu marks its field with
// set_plain_text_field; 0: none), typed without suggestions, autocorrection
// or capitals.
bool plain_field_active(const ImGuiContext& context);
void set_plain_text_field(unsigned id);
// SDL's text input on `window` (stopped first when it runs) for a field of
// that kind: on Android a password field's (SDL's TEXT_PASSWORD_HIDDEN, no
// autocorrection, so the keyboard offers no suggestions and learns nothing)
// or a text field's, with the keyboard's input restarted so it takes the
// kind. The top screen's fields and the second screen's share it.
// `numeric` (Android): a number field's keyboard (SDL's TEXTINPUT_TYPE_NUMBER);
// `plain` (Android): a text field's without suggestions, autocorrection or
// capitals (Android: a visible password's input type, which no keyboard corrects).
void start_text_input(SDL_Window* window, bool secret, bool numeric = false, bool plain = false);

// ---- Touch (Android; DW4_TOUCH_UI=1 shows it in a PC test run)
// The menu button's rectangle in window pixels: a small square in the top
// right corner, inside the bar the 4:3 picture leaves on a wider window (the
// HUD stays on the 4:3 picture, also in widescreen), never over it there.
struct TouchRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};
TouchRect touch_menu_rect(float width, float height);
// The button over the picture (ImGui's foreground): three lines on a dark
// square, shown after a touch for kTouchButtonSeconds and while the F1 menu
// is open, then fading out; its tap (host_event) opens and closes the menu.
inline constexpr double kTouchButtonSeconds = 4.0;
void draw_touch_menu_button(HostView& view);

// ---- Android's display and the app's lifecycle
// The rate the game presents at asked of the display (Android: 60, or a
// frame limit above 60 up to 120; nothing on the PC).
void request_frame_rate(const Settings& settings);
// What the lifecycle watch (game_main) left for the loop: the app came back
// to the foreground (the loop starts its clock again: no catch-up of the
// time it stood), and Android said memory is short (the renderer lets its
// unused textures go). Each is taken once.
enum class LifecycleNote : std::uint8_t { foreground = 1, low_memory = 2 };
void note_lifecycle(LifecycleNote note);
bool take_lifecycle(LifecycleNote note);

// The SDL hints for the controller families, before SDL_Init: SDL's Wii
// driver only with wii_driver=on (automatic turns it on later, while a Wii
// U Pro Controller's ids are connected: update_controllers); no libusb, and
// the Switch 2 Pro Controller left to the port's reader (pad_usb.hpp
// controller_hint_values).
void controller_hints(const Settings& settings);

} // namespace dw4::game
