#pragma once

// PC settings of the runtime (settings.ini): window and presentation, audio
// level, keyboard bindings and the extracted game data folder. The F1 menu
// edits them while playing and writes them back.
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
    // [audio]
    int volume = 100;        // master volume in percent
    // [menu]
    bool pause_in_menu = true;
    // [preferences]
    bool skip_intro = false;
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
    // [mods]
    std::filesystem::path texture_pack; // replacement PNGs (default mods/textures)
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
    // "up", "down", "left", "right") are migrated, see settings.cpp.
    static Settings parse(IniFile ini);
    // Writes the values into the document (the next save() writes the file).
    void store();
    bool save();
    const IniFile& ini() const { return ini_; }

private:
    IniFile ini_;
};

} // namespace dw4::game
