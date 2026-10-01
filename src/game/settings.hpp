#pragma once

// PC settings of the runtime (settings.ini): window and presentation, audio
// level, keyboard bindings and the extracted game data folder. The F1 menu
// edits them while playing and writes them back.
#include "dw4/ini.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace dw4::game {

// The GameCube controller buttons a key can stand for.
enum class PadKey { a, b, x, y, start, up, down, left, right, l, r, z, count };
constexpr std::size_t kPadKeyCount = std::size_t(PadKey::count);
const char* pad_key_name(PadKey key);      // "A", "Start", "Up", ...
const char* pad_key_ini_name(PadKey key);  // "a", "start", "up", ...
std::uint32_t pad_key_bit(PadKey key);     // dw4::pad_button bit

// A controller input bound to a GameCube button: a gamepad button or a
// trigger axis pulled past half way.
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
    std::uint32_t drop_seed = 0; // boot straight to the title (no logos / opening movie)
    // [keyboard]: up to three keys per button.
    static constexpr std::size_t kKeysPerButton = 3;
    std::array<std::array<SDL_Scancode, kKeysPerButton>, kPadKeyCount> keys{};
    // [gamepad]: up to three controller inputs per button, for every controller.
    std::array<std::array<PadInput, kKeysPerButton>, kPadKeyCount> pad{};
    // [mods]
    std::filesystem::path texture_pack; // replacement PNGs (default mods/textures)
    bool dump_textures = false;         // write decoded textures to mods/dump/textures
    std::set<std::string> disabled_mods; // content mods (mods/<id>) switched off
    // [data]
    std::filesystem::path data;
    std::filesystem::path cards; // memory card root (Card A / Card B), empty: saves/GC/USA

    std::filesystem::path file; // where the settings are read from and saved to

    Settings();
    void reset_keys();
    void reset_pad();
    // local/settings.ini or settings.ini in the working directory, else
    // settings.ini next to the program.
    static Settings load();
    bool save();

private:
    IniFile ini_;
};

} // namespace dw4::game
