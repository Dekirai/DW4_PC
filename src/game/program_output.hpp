#pragma once

#include <filesystem>
#include <string>

namespace dw4::game {

// Where the program's own lines (stdout and stderr: "saves: ...", "mod: ...",
// the reason a start failed) go. The release game is a Windows program
// without a console window (CMake DW4_GAME_CONSOLE=OFF, DW4_GAME_WINDOWED):
//  - output the starter redirected (a test run's pipe or file, "> out.txt")
//    stays where it is;
//  - started from a console (cmd, PowerShell), the lines go to that console;
//  - else (a start from Explorer or a shortcut) to `log_file`, written anew at
//    each start (the previous one is kept as `<stem>.previous<ext>`).
// A console build (development builds) changes nothing. Returns what it did
// for the log ("" when nothing changed).
std::string route_program_output(const std::filesystem::path& log_file);

} // namespace dw4::game
