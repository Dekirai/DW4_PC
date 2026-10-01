#include "dw4/ini.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

namespace dw4 {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

IniFile IniFile::parse(std::string_view text) {
    IniFile ini;
    std::string section;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view raw = text.substr(start, end - start);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        start = end + 1;
        Line line;
        line.text = std::string(raw);
        const auto content = trim(raw);
        if (content.size() >= 2 && content.front() == '[' && content.back() == ']') {
            section = std::string(trim(content.substr(1, content.size() - 2)));
            line.section = section;
        } else {
            line.section = section;
            const auto eq = content.find('=');
            if (!content.empty() && content.front() != ';' && content.front() != '#' && eq != std::string_view::npos) {
                line.key = std::string(trim(content.substr(0, eq)));
                line.value = std::string(trim(content.substr(eq + 1)));
            }
        }
        ini.lines_.push_back(std::move(line));
    }
    return ini;
}

std::optional<IniFile> IniFile::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str());
}

std::string IniFile::text() const {
    std::string out;
    for (const auto& line : lines_) {
        out += line.text;
        out += '\n';
    }
    return out;
}

bool IniFile::save(const std::filesystem::path& path) const {
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text();
    return bool(out);
}

std::optional<std::string> IniFile::get(std::string_view section, std::string_view key) const {
    for (const auto& line : lines_)
        if (!line.key.empty() && same(line.section, section) && same(line.key, key)) return line.value;
    return std::nullopt;
}

std::string IniFile::get_or(std::string_view section, std::string_view key, std::string_view fallback) const {
    auto value = get(section, key);
    return value ? *value : std::string(fallback);
}

int IniFile::get_int(std::string_view section, std::string_view key, int fallback) const {
    const auto value = get(section, key);
    if (!value) return fallback;
    int result = fallback;
    const auto* begin = value->data();
    const auto [end, error] = std::from_chars(begin, begin + value->size(), result);
    return error == std::errc() && end == begin + value->size() ? result : fallback;
}

bool IniFile::get_bool(std::string_view section, std::string_view key, bool fallback) const {
    const auto value = get(section, key);
    if (!value) return fallback;
    if (same(*value, "true") || same(*value, "yes") || same(*value, "on") || *value == "1") return true;
    if (same(*value, "false") || same(*value, "no") || same(*value, "off") || *value == "0") return false;
    return fallback;
}

void IniFile::set(std::string_view section, std::string_view key, std::string_view value) {
    std::size_t last_in_section = lines_.size();
    bool section_found = false;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        auto& line = lines_[i];
        if (!same(line.section, section)) continue;
        if (!line.key.empty() && same(line.key, key)) {
            line.value = std::string(value);
            line.text = line.key + "=" + line.value;
            return;
        }
        section_found = true;
        if (!trim(line.text).empty()) last_in_section = i;
    }
    Line line;
    line.section = std::string(section);
    line.key = std::string(key);
    line.value = std::string(value);
    line.text = line.key + "=" + line.value;
    if (!section_found) {
        if (!lines_.empty() && !trim(lines_.back().text).empty()) lines_.push_back(Line{"", lines_.back().section, "", ""});
        lines_.push_back(Line{"[" + std::string(section) + "]", std::string(section), "", ""});
        lines_.push_back(std::move(line));
        return;
    }
    lines_.insert(lines_.begin() + std::ptrdiff_t(last_in_section + 1), std::move(line));
}

void IniFile::set_int(std::string_view section, std::string_view key, int value) { set(section, key, std::to_string(value)); }

void IniFile::set_bool(std::string_view section, std::string_view key, bool value) {
    set(section, key, value ? "true" : "false");
}

} // namespace dw4
