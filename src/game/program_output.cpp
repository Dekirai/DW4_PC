#include "program_output.hpp"

#include <cstdio>
#include <iostream>
#include <system_error>

#if defined(_WIN32) && defined(DW4_GAME_WINDOWED)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace dw4::game {

#if defined(_WIN32) && defined(DW4_GAME_WINDOWED)
namespace {
// A standard handle the starter gave this process (a pipe, a file, a console
// it shares): its output stays there.
bool usable(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}
} // namespace

std::string route_program_output(const std::filesystem::path& log_file) {
    if (usable(GetStdHandle(STD_OUTPUT_HANDLE))) return {};
    FILE* stream = nullptr;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // (The prompt of cmd or PowerShell does not wait for a Windows program: the lines come after it.)
        if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) return {};
        freopen_s(&stream, "CONOUT$", "w", stderr);
        std::cout.clear();
        std::cerr.clear();
        std::cout << '\n';
        return "the console it was started from";
    }
    std::error_code error;
    std::filesystem::create_directories(log_file.parent_path(), error);
    if (std::filesystem::exists(log_file, error)) {
        auto previous = log_file;
        previous.replace_extension(".previous" + log_file.extension().string());
        std::filesystem::rename(log_file, previous, error);
    }
    // Truncated once, then both streams append, unbuffered: the lines keep their
    // order, and a crash loses none of them.
    if (_wfopen_s(&stream, log_file.c_str(), L"w") != 0 || !stream) return {};
    std::fclose(stream);
    if (_wfreopen_s(&stream, log_file.c_str(), L"a", stdout) != 0) return {};
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (_wfreopen_s(&stream, log_file.c_str(), L"a", stderr) == 0) std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::cout.clear();
    std::cerr.clear();
    const auto text = log_file.u8string();
    return std::string(text.begin(), text.end());
}
#else
std::string route_program_output(const std::filesystem::path&) { return {}; }
#endif

} // namespace dw4::game
