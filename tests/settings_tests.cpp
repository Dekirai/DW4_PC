#include "dw4/settings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("dw4-settings-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    try {
        const auto absent = dw4::load_settings(path);
        require(absent.value == dw4::Settings{}, "Missing config must use defaults");
        require(absent.warnings.empty(), "Missing config is normal");
        dw4::Settings wanted;
        wanted.width = 3840;
        wanted.height = 2160;
        wanted.borderless = true;
        wanted.vsync = false;
        wanted.fps_limit = 0;
        wanted.msaa = 8;
        wanted.aspect = "16:9";
        wanted.controls = "playstation";
        wanted.gameplay = "modernized";
        wanted.data_path = dw4::utf8_path("Games/Gr\xC3\xB6\xC3\x9F" "e \xE6\x95\xB0\xE6\x8D\xAE/Extracted");
        dw4::save_settings(path, wanted);
        const auto loaded = dw4::load_settings(path);
        require(loaded.warnings.empty(), "Valid file produced warnings");
        require(loaded.value == wanted, "Settings must roundtrip, including UTF-8 paths");
        {
            std::ofstream broken(path, std::ios::trunc);
            broken << "[window]\nwidth=-1\nheight=999999\nfps_limit=144evil\nmsaa=16\nvsync=maybe\n"
                      "[preferences]\naspect=bad\ncontrols=bad\ngameplay=bad\n";
        }
        const auto repaired = dw4::load_settings(path);
        require(repaired.value == dw4::Settings{}, "Invalid values must fall back to defaults");
        require(repaired.warnings.size() >= 6, "Invalid settings should be reported");
        wanted.width = 42;
        bool rejected = false;
        try { dw4::save_settings(path, wanted); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Saving invalid settings must fail");
        {
            std::ofstream good(path, std::ios::trunc);
            good << "\xEF\xBB\xBF; UTF-8 BOM and CRLF\r\n[window]\r\nwidth=1920\r\nheight=1080\r\nvsync=0\r\nunknown=42\r\n";
        }
        const auto forward = dw4::load_settings(path);
        require(forward.value.width == 1920 && forward.value.height == 1080 && !forward.value.vsync,
                "BOM, CRLF and boolean parsing");
        require(forward.warnings.empty(), "Unknown keys should be forward-compatible");
        std::filesystem::remove(path);
        std::cout << "settings_tests: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::cerr << "settings_tests: " << error.what() << '\n';
        return 1;
    }
}
