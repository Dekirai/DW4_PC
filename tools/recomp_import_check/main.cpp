#include "frontend_config.hpp"

#include <array>
#include <iostream>
#include <string>
#include <string_view>

int main() {
    using moderngekko::frontend::SafeImportComponent;
    constexpr std::array valid = {
        "ndmw.afs", "DATA.AFS", "opening.bnr", "a", "file-01_name.ext",
        "some file", "COM0", "COM10.txt", "file..name", "CONSOLE.txt",
    };
    constexpr std::array invalid = {
        "", ".", "..", "../escape", "..\\escape", "/absolute", "C:drive",
        "a/b", "a\\b", "a:stream", "a*", "a?", "a<", "a>", "a|", "a\"",
        "trailing.", "trailing ", "CON", "con.txt", "PRN.log", "NUL", "AUX",
        "COM1", "com9.txt", "LPT1.bin", "lpt9", "bad\nname", "bad\tname",
        "CON .txt", "CONIN$", "CONOUT$",
    };
    unsigned checks = 0;
    const auto check = [&](bool accepted, bool expected, std::string_view label) {
        ++checks;
        if (accepted != expected) {
            std::cerr << "Unexpected import path decision: " << label << '\n';
            return false;
        }
        return true;
    };
    for (const auto* name : valid)
        if (!check(SafeImportComponent(name), true, name)) return 1;
    for (const auto* name : invalid)
        if (!check(SafeImportComponent(name), false, name)) return 1;
    if (!check(SafeImportComponent(std::string_view("a\0b", 3)), false, "embedded NUL") ||
        !check(SafeImportComponent(std::string(1, '\x7f')), false, "DEL") ||
        !check(SafeImportComponent(std::string(1, '\xff')), false, "non-ASCII") ||
        !check(SafeImportComponent(std::string(255, 'a')), true, "255 characters") ||
        !check(SafeImportComponent(std::string(256, 'a')), false, "256 characters")) return 1;
    std::cout << checks << " import path checks passed. No game code executed.\n";
}
