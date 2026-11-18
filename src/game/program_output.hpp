#pragma once

#include <filesystem>
#include <string>

namespace dw4::game {

// Where the program's own lines (stdout and stderr: "saves: ...", "mod: ...",
// the reason a start failed) go. The release game is a Windows program
// without a console window (CMake DW4_GAME_CONSOLE=OFF, DW4_GAME_WINDOWED):
//  - output the starter redirected (a test run's pipe or file, "> out.txt")
//    stays where it is;
//  - started from a console (cmd, PowerShell), the lines go to that console
//    until the run turns out to play the game (leave_console);
//  - else (a start from Explorer or a shortcut) to `log_file`, written anew at
//    each start (the previous one is kept as `<stem>.previous<ext>`), or
//    nowhere when it cannot be written.
// stderr goes where stdout goes (a stream without a handle is never left: the
// C runtime would end the program at its first line). A console build
// (development builds) changes nothing. Returns where the lines go ("" when
// nothing changed or nowhere).
std::string route_program_output(const std::filesystem::path& log_file);

// A run that plays the game (not --help or a tool, which print and exit) and
// was started from a console: one line there names `log_file`, the game's
// lines go to it from now on, and the process leaves the console (FreeConsole;
// the review of 0.2.1). The prompt came back at once (it does not wait for a
// Windows program), so the console is the player's again: attached, its Ctrl+C
// reached SDL's quit and closing its window ended the game at once (LAN guests
// and online partners lost the session). `program` names the program in that
// line. Returns where the lines go now ("" when nothing changed).
std::string leave_console(const std::filesystem::path& log_file, const std::string& program);

} // namespace dw4::game
