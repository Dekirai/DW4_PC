#pragma once

// PC settings of the runtime (settings.ini): window and presentation, audio
// level, keyboard bindings and the extracted game data folder. The F1 menu
// edits them while playing and writes them back.
#include "graphics_options.hpp"

#include "dw4/ini.hpp"
#include "dw4/pad.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace dw4::game {

// The GameCube controls a key or controller input can stand for: the
// buttons, the D-pad and the digital directions of the main stick and the
// C-stick. Like on a GameCube pad the three direction groups are separate
// inputs: the main stick walks, the D-pad and the C-stick move the quick
// panels' cursor (the D-pad and the main stick move menu cursors).
enum class PadKey {
    a, b, x, y, start, l, r, z,
    stick_up, stick_down, stick_left, stick_right,
    dpad_up, dpad_down, dpad_left, dpad_right,
    cstick_up, cstick_down, cstick_left, cstick_right,
    count
};
constexpr std::size_t kPadKeyCount = std::size_t(PadKey::count);
enum class PadGroup { buttons, stick, dpad, c_stick };
const char* pad_key_name(PadKey key);      // "A", "Start", "Stick up", "D-pad up", "C-stick up", ...
const char* pad_key_ini_name(PadKey key);  // "a", "start", "stick_up", "dpad_up", "cstick_up", ...
PadGroup pad_key_group(PadKey key);
const char* pad_group_name(PadGroup group); // the F1 menu's section line
// Adds the control to a pad's host input (button bit or stick direction).
void press(PadKey key, HostPadInput& input);

// A controller input bound to a GameCube control: a gamepad button or a
// trigger axis pulled past half way. (A controller's left and right stick
// are always the main stick and the C-stick.)
struct PadInput {
    enum class Kind : std::uint8_t { none, button, axis } kind{};
    int code{}; // SDL_GamepadButton or SDL_GamepadAxis
    bool operator==(const PadInput&) const = default;
};
std::string pad_input_name(const PadInput& input); // "a", "leftshoulder", "righttrigger", ""
PadInput pad_input_from_name(std::string_view name);

// [gamepad] nintendo_layout: how the bindings read a Nintendo pad's face
// buttons (Switch Pro, Switch 2 Pro, Wii U Pro): by the letters printed on
// them (the binding "a" is the A button) or by position like an Xbox pad
// ("a" is the bottom button, B on these pads). pad_family.hpp.
enum class NintendoLayout : std::uint8_t { by_label, by_position };
// [gamepad] gamecube_layout: a GameCube controller plays 1:1 (original, its
// A is the game's A ...) or through the bindings (by its own labels).
enum class GameCubeLayout : std::uint8_t { original, bindings };
// [gamepad] wii_driver: SDL's Wii driver, which reads the Wii U Pro
// Controller. auto (the default): on only while a 057e:0330 device (a Wii U
// Pro Controller or a Wii Remote Plus) is connected, so a Wii remote bar
// such as the DolphinBar (057e:0306) never reaches it; on; off.
enum class WiiDriver : std::uint8_t { automatic, on, off };
// [gamepad] menu_buttons: which controller buttons the port's menus (F1, the
// online screens, the on-screen keyboard) take. layout (the default): the
// controller's own A / Cross confirms and B / Circle goes back, whatever the
// game's bindings (a Nintendo pad as its Nintendo layout reads it, a
// GameCube pad's A and B); bindings: the game's A binding confirms and its
// B binding goes back.
enum class MenuButtons : std::uint8_t { layout, bindings };
// [gamepad] thor_style: the AYN Thor's own controller setting "Controller
// style" (Thor Settings, or its quick settings tile), which decides the key
// codes of its face buttons, printed in the Nintendo layout (A right, B
// bottom, X top, Y left): standard sends the printed letters' codes (SDL's
// SOUTH is the printed A), xbox sends positions like an Xbox pad (SOUTH is
// the bottom button, printed B). auto (the default): the device's setting as
// Android reports it (persist.sys.gamepad.type), read again while the game
// runs (pad_family.hpp).
enum class ThorStyle : std::uint8_t { automatic, standard, xbox };

struct Settings {
    // [window]
    int width = 1280;
    int height = 960;
    bool fullscreen = false; // borderless desktop fullscreen
    bool vsync = true;
    bool overflow_npc = true; // [overflow] npc: Data Overflow's Simulator Keeper in the lobby (SURVIVAL.md 3.2)
    bool widescreen = false; // 3D fills wide windows, 2D stays 4:3
    int fps_limit = 0;       // presented frames per second, 0 = no limit
    bool smooth_motion = true; // blend between the 60 Hz steps when presenting faster
    int msaa = 4;            // samples, applied at start-up
    // [window] ui_scale: the size of the port's menus (the F1 menu, the
    // first start's screens, the controller pages, the notices) in percent;
    // 0 = auto: the display's scale on Android (ui_scale_factor), 100 on the
    // PC. Written only once it is not auto (a PC's file stays as it was).
    int ui_scale = 0;
    // [graphics]: texture quality, filtering, LOD bias, sharpening, shadow
    // quality and the texture memory budget (graphics_options.hpp).
    GraphicsOptions graphics;
    // [audio]
    int volume = 100;        // master volume in percent
    // [menu]
    bool pause_in_menu = true;
    // [preferences]
    bool skip_intro = false;
    // [preferences] language: the game text's display language (docs/L10N.md):
    // "en" (the game's own English text) or a language pack's code (lang/<code>
    // next to the program). Presentation only, never part of the game state.
    std::string language = "en";
    // [gameplay]: optional rebalancing and randomizer (100 / 0 = original)
    int exp_rate = 100, damage_dealt = 100, damage_taken = 100;
    // [randomizer]: the seed new games bind to their save (0 off; the old
    // [gameplay] drop_seed migrates as seed + drops), item drops, enemies
    // (0 off, 1 every enemy on its own, 2 one family per wave) and loading
    // zones (0 off, 1 dungeon entrances within an area).
    std::uint32_t rand_seed = 0;
    bool rand_drops = false;
    int rand_enemies = 0;
    int rand_zones = 0;
    // [gameplay] loading = original / fast: stage loads with the GameCube's
    // disc and loading-thread time, or without it (engine::Cheats::fast_loading).
    bool fast_loading = false;
    // [keyboard]: up to three keys per control (PadKey).
    static constexpr std::size_t kKeysPerButton = 3;
    std::array<std::array<SDL_Scancode, kKeysPerButton>, kPadKeyCount> keys{};
    // [gamepad]: up to three controller inputs per control, for every controller.
    std::array<std::array<PadInput, kKeysPerButton>, kPadKeyCount> pad{};
    // [gamepad] the controller families (pad_family.hpp): the Nintendo and
    // GameCube layouts, the button pictures ("auto": each player's device,
    // player 1's last used one; else a family id such as "dualsense" or
    // "keyboard" for every player), SDL's Wii driver for the Wii U Pro
    // Controller, the menus' buttons and the GameCube adapter in Wii U mode
    // (gc_adapter.hpp: WinUSB, released while the window is in the
    // background).
    NintendoLayout nintendo_layout = NintendoLayout::by_label;
    GameCubeLayout gamecube_layout = GameCubeLayout::original;
    std::string prompts = "auto";
    WiiDriver wii_driver = WiiDriver::automatic;
    MenuButtons menu_buttons = MenuButtons::layout;
    ThorStyle thor_style = ThorStyle::automatic; // (written only once it is not auto)
    bool gc_adapter = true;
    // [mods]
    std::filesystem::path texture_pack; // the texture pack folder (default mods/textures)
    bool dump_textures = false;         // write decoded textures to mods/dump/textures
    std::set<std::string> disabled_mods; // content mods (mods/<id>) switched off
    // [hud] overhead name tags (overhead_labels.hpp): auto (only with two
    // or more players), all, others (not over this machine's player), off;
    // their size in percent and whether the bars show numbers.
    std::string overhead_labels = "auto";
    int label_scale = 100;
    bool label_numbers = false;
    // [discord] Rich Presence (discord_presence.hpp) and the application id.
    static constexpr const char* kDiscordClientId = "1560921673354453093";
    bool discord = true;
    std::string discord_client_id = kDiscordClientId;
    // [achievements] local achievements (achievements.hpp) and their
    // unlock notices.
    bool achievements = true;
    bool achievement_popups = true;
    // [online] net_indicator: the small ping / input delay box in online
    // sessions (round 62).
    bool net_indicator = true;
    // [data]
    std::filesystem::path data;
    // The save files of memory card slots A and B (GCI bytes; relative paths
    // are next to the program). Slot B holds a card when its file exists.
    std::filesystem::path save_file = "saves/savedata.dat";
    std::filesystem::path save_file_b = "saves/savedata_b.dat";
    // [data] save_migrated: the old saves/GC/USA save was looked for (and
    // copied into save_file / save_file_b) once.
    bool save_migrated = false;
    // A GCI folder root (Card A / Card B, e.g. Dolphin's GC/USA) used instead
    // of the save files; empty: the save files.
    std::filesystem::path cards;
    // What the slots hold (engine::parse_card_setting: "none", "unformatted",
    // a block count ...), empty: a 2043-block card in A, B when its file (or
    // GCI folder) exists.
    std::string card_a, card_b;

    std::filesystem::path file; // where the settings are read from and saved to

    Settings();
    void reset_keys();
    void reset_pad();
    // local/settings.ini or settings.ini in the working directory, else
    // settings.ini next to the program.
    static Settings load();
    // The settings of a document (load() without the file search). Bindings
    // from before the separate D-pad and C-stick inputs (keyboard / gamepad
    // "up", "down", "left", "right") are migrated, see settings.cpp; so are
    // face button bindings from before the Nintendo layout (they keep
    // reading every pad by position).
    static Settings parse(IniFile ini);
    // Writes the values into the document (the next save() writes the file).
    void store();
    bool save();
    const IniFile& ini() const { return ini_; }

private:
    IniFile ini_;
};

// The menus' scale (ImGui) for a ui_scale setting (percent, 0 = auto) and
// the display's scale (SDL_GetWindowDisplayScale: Android's density, the
// Thor's 369 dpi panel 2.31). Auto follows the display where the port asks
// for it (Android: follow_display) and is 1 elsewhere, so the PC looks as
// before; a set percent counts everywhere. Between 1 and 4.
float ui_scale_factor(int setting_percent, float display_scale, bool follow_display);
// The menu sizes the F1 menu offers (percent; 0 = auto).
inline constexpr int kUiScaleChoices[] = {0, 100, 125, 150, 175, 200, 225, 250, 300};

} // namespace dw4::game
