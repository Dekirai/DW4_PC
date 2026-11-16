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
//    each start (the previous one is kept as `<stem>.previous<ext>`), or
//    nowhere when it cannot be written.
// stderr goes where stdout goes (a stream without a handle is never left: the
// C runtime would end the program at its first line). A console build
// (development builds) changes nothing. Returns where the lines go ("" when
// nothing changed or nowhere).
std::string route_program_output(const std::filesystem::path& log_file);

} // namespace dw4::game
