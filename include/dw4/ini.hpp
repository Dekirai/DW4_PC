#pragma once

// A small INI document for the PC settings (settings.ini): "[section]"
// headers, "key=value" lines, ';' / '#' comments. Unknown sections, keys and
// comments survive a load / save round trip; set() changes a value in place
// or appends the key to its section.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {

class IniFile {
public:
    static IniFile parse(std::string_view text);
    static std::optional<IniFile> load(const std::filesystem::path& path);
    std::string text() const;
    bool save(const std::filesystem::path& path) const;

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string get_or(std::string_view section, std::string_view key, std::string_view fallback) const;
    int get_int(std::string_view section, std::string_view key, int fallback) const;
    bool get_bool(std::string_view section, std::string_view key, bool fallback) const;

    void set(std::string_view section, std::string_view key, std::string_view value);
    void set_int(std::string_view section, std::string_view key, int value);
    void set_bool(std::string_view section, std::string_view key, bool value);
    // Drops every line of the key in the section; true when one was there.
    bool remove(std::string_view section, std::string_view key);
    // Drops the section's header (and its blank lines) when no key and no
    // comment is left in it, and the blank line set() put before it when it
    // was the document's last; true when it went.
    bool remove_section_if_empty(std::string_view section);

private:
    struct Line {
        std::string text;    // the original line (comments, blanks, headers)
        std::string section; // the section the line belongs to
        std::string key;     // empty for non key lines
        std::string value;
    };
    std::vector<Line> lines_;
};

} // namespace dw4
