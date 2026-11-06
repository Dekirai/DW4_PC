#include "overflow_text.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>

namespace dw4::game::overflow {

namespace {
constexpr std::array<TextEntry, std::size_t(Text::count_)> kTexts{{
#define DW4_OVERFLOW_TEXT(id, text) {#id, text},
#include "overflow_text.inc"
#undef DW4_OVERFLOW_TEXT
}};
} // namespace

std::string_view text(Text id) {
    const auto index = std::size_t(id);
    return index < kTexts.size() ? kTexts[index].text : std::string_view{};
}

std::span<const TextEntry> all_texts() { return kTexts; }

std::string format(Text id, ...) {
    const std::string pattern(text(id));
    char buffer[512];
    va_list args;
    va_start(args, id);
    std::vsnprintf(buffer, sizeof buffer, pattern.c_str(), args);
    va_end(args);
    return buffer;
}

std::u32string code_points(std::string_view utf8) {
    std::u32string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = c;
        std::size_t extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (std::size_t k = 0; k < extra && i < utf8.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

std::u16string fit_to_font(std::u16string_view text, const std::function<bool(char16_t)>& has) {
    std::u16string out;
    out.reserve(text.size());
    for (const char16_t c : text) {
        if (c == u'\n' || c < 0x21 || c > 0x7E || has(c)) {
            out.push_back(c);
            continue;
        }
        if (const char16_t full = char16_t(c - 0x21 + 0xFF01); has(full)) {
            out.push_back(full);
            continue;
        }
        std::u16string_view near;
        switch (c) {
        case u'#': near = u"No."; break;
        case u'$': near = u"S"; break;
        case u'%': near = u"pct"; break;
        case u'*': near = u"x"; break;
        case u'/': near = u"-"; break;
        case u';': near = u","; break;
        case u'=': near = u":"; break;
        case u'@': near = u"at"; break;
        case u'[': case u'{': near = u"("; break;
        case u']': case u'}': near = u")"; break;
        case u'\\': case u'~': near = u"-"; break;
        case u'`': near = u"'"; break;
        default: break;
        }
        if (!near.empty() && std::all_of(near.begin(), near.end(), [&](char16_t n) { return has(n); })) out += near;
        else out.push_back(c);
    }
    return out;
}

std::u16string to_game_text(std::string_view utf8) {
    std::u16string out;
    for (const char32_t cp : code_points(utf8)) {
        if (cp == U'|') out.push_back(u'\n');
        else if (cp < 0x10000) out.push_back(char16_t(cp));
    }
    return out;
}

} // namespace dw4::game::overflow
