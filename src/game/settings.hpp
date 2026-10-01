#pragma once

// PC settings of the runtime (settings.ini): window and presentation, audio
// level, keyboard bindings and the extracted game data folder. The F1 menu
// edits them while playing and writes them back.
#include "dw4/ini.hpp"

#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdint>
#include <filesystem>

namespace dw4::game {

// The GameCube controller buttons a key can stand for.
enum class PadKey { a, b, x, y, start, up, down, left, right, l, r, z, count };
constexpr std::size_t kPadKeyCount = std::size_t(PadKey::count);
const char* pad_key_name(PadKey key);      // "A", "Start", "Up", ...
const char* pad_key_ini_name(PadKey key);  // "a", "start", "up", ...
std::uint32_t pad_key_bit(PadKey key);     // dw4::pad_button bit

struct Settings {
    // [window]
    int width = 1280;
    int height = 960;
    bool fullscreen = false; // borderless desktop fullscreen
    bool vsync = true;
    int fps_limit = 0;       // presented frames per second, 0 = no limit
    int msaa = 4;            // samples, applied at start-up
    // [audio]
    int volume = 100;        // master volume in percent
    // [menu]
    bool pause_in_menu = true;
    // [keyboard]: up to three keys per button.
    static constexpr std::size_t kKeysPerButton = 3;
    std::array<std::array<SDL_Scancode, kKeysPerButton>, kPadKeyCount> keys{};
    // [data]
    std::filesystem::path data;

    std::filesystem::path file; // where the settings are read from and saved to

    Settings();
    void reset_keys();
    // local/settings.ini or settings.ini in the working directory, else
    // settings.ini next to the program.
    static Settings load();
    bool save();

private:
    IniFile ini_;
};

} // namespace dw4::game
