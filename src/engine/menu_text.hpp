#pragma once

// Menu text and messenger helpers shared by the character select and the save
// room panels (TEXTORDER 804E93DC, the per-player font lines 804EA468).
#include "dw4/engine/managers.hpp"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace dw4::engine::menu {

bool query(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
           std::string_view command, std::optional<std::string_view> argument = std::nullopt);
std::size_t send(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
                 std::string_view command);
void text_command(Engine& engine, std::string_view command, std::initializer_list<std::string_view> strings);

// 804EA468: per-player line names, "font" + player digit + two-digit index.
std::string player_font(int player, int index);

// 80362614: clear the entry's lines, then run the text script for its call;
// with `continued`, an entry flagged 0x100000 also runs the next entry's call.
void text_order_show(Engine& engine, int id, bool continued = false);
// 803627F4: clear the entry's lines only.
void text_order_clear(Engine& engine, int id);
// 803625DC: hide one player line.
void text_line_clear(Engine& engine, std::string_view layer, int player, int index);
// 803622B4: a literal on one player line (centred, default colour, shown).
void text_set_literal(Engine& engine, std::string_view layer, int player, int index, std::string_view text);
// 80362598: a game-text (Shift-JIS) literal on one player line with the
// alignment and colour of text_set_string.
void text_set_text(Engine& engine, std::string_view layer, int player, int index, std::string_view text, int align,
                   bool cancel_color);
// 803628AC: a UTF-16 string on one player line; align 0 left, 1 centre,
// 2 right; the cancel colour greys it.
void text_set_string(Engine& engine, std::string_view layer, int player, int index, const std::u16string& text,
                     int align, bool cancel_color);

struct PartnerRow {
    const char* model;  // p_agumon
    const char* open;   // face window opening sequence
    const char* change; // face window change sequence
};
// 804EA5A8: the sixteen player Digimon and their face window sequences.
extern const PartnerRow kPartnerRows[16];
// Row of a DIGIMONNAME (0 when unknown, like 80353AD8's search).
int partner_row(std::string_view model);

} // namespace dw4::engine::menu
