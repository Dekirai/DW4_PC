#include "dw4/ini.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

constexpr const char* kSample = "; DW4 PC settings\r\n"
                                "[window]\r\n"
                                "width=1280\r\n"
                                "vsync = false\r\n"
                                "\r\n"
                                "[data]\r\n"
                                "path=E:\\Games\\DW4\r\n";

void test_read() {
    const auto ini = dw4::IniFile::parse(kSample);
    check(ini.get_int("window", "width", 0) == 1280, "Integers are read");
    check(!ini.get_bool("window", "vsync", true), "Spaces around keys and values are trimmed");
    check(ini.get_or("DATA", "PATH", "") == "E:\\Games\\DW4", "Sections and keys match without case");
    check(ini.get_int("window", "height", 960) == 960, "Missing keys take the fallback");
    check(ini.get_int("data", "path", 7) == 7, "Values that are not numbers take the fallback");
}

void test_write() {
    auto ini = dw4::IniFile::parse(kSample);
    ini.set_int("window", "width", 1920);
    ini.set_int("window", "height", 1080);
    ini.set("keyboard", "a", "Return");
    const auto text = ini.text();
    check(text.find("; DW4 PC settings\n") == 0, "Comments survive");
    check(text.find("width=1920\nvsync = false\nheight=1080\n") != std::string::npos,
          "Changed values stay in place, new keys follow the section's last line");
    check(text.find("path=E:\\Games\\DW4\n\n[keyboard]\na=Return\n") != std::string::npos, "New sections are appended");
    const auto again = dw4::IniFile::parse(text);
    check(again.get_int("window", "height", 0) == 1080 && again.get_or("keyboard", "a", "") == "Return",
          "A saved document reads back");
}

void test_remove() {
    auto ini = dw4::IniFile::parse(kSample);
    check(ini.remove("WINDOW", "Width"), "A key is removed without case");
    check(!ini.remove("window", "width"), "A removed key is gone");
    check(!ini.remove("data", "width"), "Other sections keep their keys");
    check(ini.text() == "; DW4 PC settings\n[window]\nvsync = false\n\n[data]\npath=E:\\Games\\DW4\n",
          "Only the key's line goes");
}
} // namespace

int main() {
    try {
        test_read();
        test_write();
        test_remove();
    } catch (const std::exception& error) {
        std::cerr << "ini tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ini tests passed (" << checks << " checks)\n";
    return 0;
}
