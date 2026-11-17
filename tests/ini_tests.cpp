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
    // A section set() appended and whose keys went again leaves no trace.
    auto appended = dw4::IniFile::parse(kSample);
    const auto before = appended.text();
    appended.set("screen2", "mode", "off");
    check(appended.text() != before, "A new section is appended");
    check(!appended.remove_section_if_empty("screen2"), "A section with a key stays");
    appended.remove("screen2", "mode");
    check(appended.remove_section_if_empty("Screen2"), "An empty section goes");
    check(appended.text() == before, "The document is as it was");
    check(!appended.remove_section_if_empty("screen2"), "A missing section is nothing to remove");
    auto commented = dw4::IniFile::parse("[a]\n; a note\n[b]\nx=1\n");
    check(!commented.remove_section_if_empty("a"), "A comment keeps its section");
    auto middle = dw4::IniFile::parse("[a]\nx=1\n\n[b]\n\n[c]\ny=2\n");
    check(middle.remove_section_if_empty("b") && middle.text() == "[a]\nx=1\n\n[c]\ny=2\n", "An empty section in the middle goes with its blank line");
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
