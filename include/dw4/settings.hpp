#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dw4 {

// These configure the native workbench. Game-specific fields are preferences
// only until a game runtime exists; they never modify original game files.
struct Settings {
    int width = 1280;
    int height = 720;
    bool borderless = false;
    bool vsync = true;
    int fps_limit = 144; // 0 means unlimited.
    int msaa = 0;      // Requested GL samples; applied when a context is created.
    std::string aspect = "original";
    std::string controls = "keyboard_mouse";
    std::string gameplay = "original";
    std::filesystem::path data_path;
    bool operator==(const Settings&) const = default;
};

struct SettingsLoad {
    Settings value;
    std::vector<std::string> warnings;
};

// Invalid values are restored to defaults; unknown keys are ignored.
SettingsLoad load_settings(const std::filesystem::path& file);
void save_settings(const std::filesystem::path& file, const Settings& settings);
std::vector<std::string> validate_settings(Settings& settings);
std::string path_utf8(const std::filesystem::path& path);
std::filesystem::path utf8_path(const std::string& text);

} // namespace dw4
