#include "dw4/settings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace dw4 {
namespace {
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool parse_int(const std::string& value, int& out) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return false;
    out = parsed;
    return true;
}

bool parse_bool(const std::string& value, bool& out) {
    if (value == "true" || value == "1") { out = true; return true; }
    if (value == "false" || value == "0") { out = false; return true; }
    return false;
}

template <typename T, std::size_t N>
bool contains(const std::array<T, N>& values, const T& candidate) {
    return std::find(values.begin(), values.end(), candidate) != values.end();
}
} // namespace

std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::vector<std::string> validate_settings(Settings& settings) {
    std::vector<std::string> warnings;
    const Settings defaults;
    const std::array<std::pair<int, int>, 4> sizes{{{1280,720}, {1920,1080}, {2560,1440}, {3840,2160}}};
    if (!contains(sizes, std::pair{settings.width, settings.height})) {
        settings.width = defaults.width;
        settings.height = defaults.height;
        warnings.emplace_back("Unsupported resolution; using 1280 x 720.");
    }
    if (!contains(std::array{0,30,60,120,144,240}, settings.fps_limit)) {
        settings.fps_limit = defaults.fps_limit;
        warnings.emplace_back("Invalid FPS limit; using 144.");
    }
    if (!contains(std::array{0,2,4,8}, settings.msaa)) {
        settings.msaa = defaults.msaa;
        warnings.emplace_back("Invalid MSAA value; using Off.");
    }
    if (settings.aspect != "original" && settings.aspect != "16:9") {
        settings.aspect = defaults.aspect;
        warnings.emplace_back("Invalid aspect preference; using Original.");
    }
    if (settings.controls != "keyboard_mouse" && settings.controls != "xbox" &&
        settings.controls != "playstation" && settings.controls != "gamecube") {
        settings.controls = defaults.controls;
        warnings.emplace_back("Invalid control preference; using Keyboard / Mouse.");
    }
    if (settings.gameplay != "original" && settings.gameplay != "modernized") {
        settings.gameplay = defaults.gameplay;
        warnings.emplace_back("Invalid gameplay preference; using Original.");
    }
    const auto path = path_utf8(settings.data_path);
    if (path.find_first_of("\r\n\0", 0, 3) != std::string::npos) {
        settings.data_path.clear();
        warnings.emplace_back("Invalid data path; cleared.");
    }
    return warnings;
}

SettingsLoad load_settings(const std::filesystem::path& file) {
    SettingsLoad result;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        if (ec) result.warnings.emplace_back("Cannot inspect settings: " + ec.message());
        return result;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        result.warnings.emplace_back("Cannot read settings file; using defaults.");
        return result;
    }
    // Cap configuration size before processing untrusted or accidentally wrong input.
    if (std::filesystem::file_size(file, ec) > 1024 * 1024 || ec) {
        result.warnings.emplace_back("Settings file is unreadable or exceeds 1 MiB; using defaults.");
        return result;
    }
    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto key = trim(line.substr(0, equal));
        const auto value = trim(line.substr(equal + 1));
        bool valid = true;
        if (section == "window") {
            if (key == "width") valid = parse_int(value, result.value.width);
            else if (key == "height") valid = parse_int(value, result.value.height);
            else if (key == "borderless") valid = parse_bool(value, result.value.borderless);
            else if (key == "vsync") valid = parse_bool(value, result.value.vsync);
            else if (key == "fps_limit") valid = parse_int(value, result.value.fps_limit);
            else if (key == "msaa") valid = parse_int(value, result.value.msaa);
        } else if (section == "preferences") {
            if (key == "aspect") result.value.aspect = value;
            else if (key == "controls") result.value.controls = value;
            else if (key == "gameplay") result.value.gameplay = value;
        } else if (section == "data" && key == "path") {
            try { result.value.data_path = utf8_path(value); }
            catch (const std::exception&) { valid = false; }
        }
        if (!valid) result.warnings.emplace_back("Invalid value on settings line " + std::to_string(line_number) + ".");
    }
    auto validation = validate_settings(result.value);
    result.warnings.insert(result.warnings.end(), validation.begin(), validation.end());
    return result;
}

void save_settings(const std::filesystem::path& file, const Settings& settings) {
    Settings checked = settings;
    if (!validate_settings(checked).empty()) throw std::runtime_error("Cannot save invalid settings.");
    if (file.empty()) throw std::runtime_error("Settings path is empty.");
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write settings file.");
    output << "; DW4 PC native workbench settings. Game preferences are not implemented yet.\n"
           << "[window]\nwidth=" << settings.width << "\nheight=" << settings.height
           << "\nborderless=" << (settings.borderless ? "true" : "false")
           << "\nvsync=" << (settings.vsync ? "true" : "false")
           << "\nfps_limit=" << settings.fps_limit << "\nmsaa=" << settings.msaa
           << "\n\n[preferences]\naspect=" << settings.aspect << "\ncontrols=" << settings.controls
           << "\ngameplay=" << settings.gameplay << "\n\n[data]\npath=" << path_utf8(settings.data_path) << '\n';
    output.flush();
    if (!output) throw std::runtime_error("Writing settings failed.");
}

} // namespace dw4
