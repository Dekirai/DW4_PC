#pragma once

// Data Overflow's strings (overflow_text.inc, docs/SURVIVAL.md 9.7): one
// table, read by the lobby and run screens and checked by T9.
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace dw4::game::overflow {

enum class Text : int {
#define DW4_OVERFLOW_TEXT(id, text) id,
#include "overflow_text.inc"
#undef DW4_OVERFLOW_TEXT
    count_
};

struct TextEntry {
    std::string_view id, text;
};

// The UTF-8 string of an entry.
std::string_view text(Text id);
// Every entry (T9).
std::span<const TextEntry> all_texts();
// printf into the entry ("%s" / "%d").
std::string format(Text id, ...);
// UTF-8 to UTF-16 (the game font's codes), `|` to a line break.
std::u16string to_game_text(std::string_view utf8);
// The code points of a UTF-8 string (T9).
std::u32string code_points(std::string_view utf8);
// The game font lacks some ASCII (# $ % * / ; = @ [ \ ] ` { } ~): a code
// `has` does not know draws as its full-width form where the font has one,
// else as a near stand-in ('/' "-", '%' "pct", '[' "(", ...), so the
// director's strings ("skill 10 / cap 1300", "35 % of MaxHP") have no gaps.
std::u16string fit_to_font(std::u16string_view text, const std::function<bool(char16_t)>& has);

} // namespace dw4::game::overflow
