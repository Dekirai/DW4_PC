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

#include <io.h>
#endif

namespace dw4::game {

#if defined(_WIN32) && defined(DW4_GAME_WINDOWED)
namespace {
// A standard handle the starter gave this process (a pipe, a file, a console
// it shares): its output stays there.
bool usable(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// stderr writes where stdout writes: one file object, one position, the lines
// in order. (A stream without a handle must never be written to: the C runtime
// ends the program at the first line, as Engine::log's std::cerr would.)
void stderr_to_stdout() {
    FILE* stream = nullptr;
    if (freopen_s(&stream, "NUL", "w", stderr) != 0) return;
    if (_fileno(stdout) >= 0 && _fileno(stderr) >= 0) _dup2(_fileno(stdout), _fileno(stderr));
    std::setvbuf(stderr, nullptr, _IONBF, 0);
}
} // namespace

std::string route_program_output(const std::filesystem::path& log_file) {
    if (usable(GetStdHandle(STD_OUTPUT_HANDLE))) {
        // ("> out.txt" in a console gives a Windows program stdout only.)
        if (!usable(GetStdHandle(STD_ERROR_HANDLE))) {
            stderr_to_stdout();
            std::cerr.clear();
        }
        return {};
    }
    FILE* stream = nullptr;
    std::string where;
    if (AttachConsole(ATTACH_PARENT_PROCESS) && freopen_s(&stream, "CONOUT$", "w", stdout) == 0) {
        // (The prompt of cmd or PowerShell does not wait for a Windows program: the lines come after it.)
        where = "the console it was started from";
    } else {
        std::error_code error;
        std::filesystem::create_directories(log_file.parent_path(), error);
        if (std::filesystem::exists(log_file, error)) {
            auto previous = log_file;
            previous.replace_extension(".previous" + log_file.extension().string());
            std::filesystem::rename(log_file, previous, error);
        }
        if (_wfreopen_s(&stream, log_file.c_str(), L"w", stdout) == 0) {
            // (Unbuffered: a crash loses no line.)
            std::setvbuf(stdout, nullptr, _IONBF, 0);
            const auto text = log_file.u8string();
            where.assign(text.begin(), text.end());
        } else {
            freopen_s(&stream, "NUL", "w", stdout); // (a folder the game cannot write to: the lines go nowhere)
        }
    }
    stderr_to_stdout();
    std::cout.clear();
    std::cerr.clear();
    if (where == "the console it was started from") std::cout << '\n';
    return where;
}
#else
std::string route_program_output(const std::filesystem::path&) { return {}; }
#endif

} // namespace dw4::game
