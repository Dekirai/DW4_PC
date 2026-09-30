#include "menu_text.hpp"

#include <algorithm>
#include <cstdio>

namespace dw4::engine::menu {
namespace {

// Line sets a TEXTORDER entry clears first (the 804E8xxx tables, one per
// player; the player index is the id's high half).
enum class ClearSet { join, choice, difficulty, message, choice_lines, slots, slot_label, slot_warning, box, status, status_check };

struct TextOrderEntry {
    int id;         // low 16 bits of the message id
    int call;       // text script call number of player 0 (player p adds p * 10000)
    ClearSet clears;
    bool continued; // 0x100000: the next entry's call follows
};

// TEXTORDER 804E93DC, the entries the menus use.
constexpr TextOrderEntry kTextOrder[] = {
    {0x10, 116, ClearSet::status, false},        {0x110, 136, ClearSet::status_check, false},
    {0x300, 150, ClearSet::box, false},          {0x301, 151, ClearSet::box, false},
    {0x302, 152, ClearSet::box, false},          {0x303, 153, ClearSet::box, false},
    {0x400, 160, ClearSet::join, false},         {0x401, 161, ClearSet::choice, false},
    {0x402, 163, ClearSet::choice, false},       {0x403, 165, ClearSet::difficulty, false},
    {0x404, 168, ClearSet::message, false},      {0x500, 200, ClearSet::message, false},
    {0x502, 201, ClearSet::message, false},      {0x503, 204, ClearSet::message, true},
    {0x504, 300, ClearSet::choice_lines, false}, {0x505, 205, ClearSet::message, false},
    {0x507, 208, ClearSet::message, false},      {0x508, 211, ClearSet::message, false},
    {0x50A, 213, ClearSet::message, false},      {0x50B, 216, ClearSet::message, false},
    {0x50D, 218, ClearSet::message, false},      {0x50E, 221, ClearSet::message, false},
    {0x510, 222, ClearSet::message, false},      {0x511, 225, ClearSet::message, false},
    {0x513, 227, ClearSet::message, false},      {0x515, 230, ClearSet::message, false},
    {0x517, 232, ClearSet::message, false},      {0x519, 235, ClearSet::message, false},
    {0x51B, 236, ClearSet::message, false},      {0x51C, 239, ClearSet::message, false},
    {0x51D, 240, ClearSet::message, false},      {0x51F, 243, ClearSet::message, false},
    {0x521, 246, ClearSet::message, false},      {0x522, 248, ClearSet::message, false},
    {0x523, 250, ClearSet::message, false},      {0x524, 251, ClearSet::message, false},
    {0x525, 252, ClearSet::message, false},      {0x527, 255, ClearSet::message, false},
    {0x529, 257, ClearSet::message, false},      {0x52A, 260, ClearSet::message, false},
    {0x52B, 262, ClearSet::message, false},      {0x52C, 264, ClearSet::message, false},
    {0x52D, 265, ClearSet::message, false},      {0x52F, 268, ClearSet::message, false},
    {0x530, 270, ClearSet::message, false},
    // GameCube memory card texts (txt_Tar.lua).
    {0x2000, 2000, ClearSet::message, false},    {0x2002, 2002, ClearSet::message, false},
    {0x2003, 2005, ClearSet::message, false},    {0x2004, 2007, ClearSet::message, false},
    {0x2005, 2009, ClearSet::message, false},    {0x2007, 2012, ClearSet::message, false},
    {0x2008, 2015, ClearSet::message, false},    {0x2009, 2017, ClearSet::message, false},
    {0x200A, 2019, ClearSet::message, false},    {0x200B, 2021, ClearSet::message, false},
    {0x200C, 2050, ClearSet::slots, false},      {0x200D, 2100, ClearSet::message, false},
    {0x200E, 2102, ClearSet::message, false},    {0x200F, 2104, ClearSet::message, false},
    {0x2010, 2106, ClearSet::message, false},    {0x2011, 2108, ClearSet::message, false},
    {0x2012, 2110, ClearSet::message, false},    {0x2013, 2112, ClearSet::message, false},
    {0x2014, 2114, ClearSet::message, false},    {0x2015, 2117, ClearSet::message, false},
    {0x2016, 2119, ClearSet::message, false},    {0x2017, 2120, ClearSet::message, false},
    {0x2018, 2122, ClearSet::message, false},    {0x201A, 2125, ClearSet::message, false},
    {0x201B, 2127, ClearSet::message, false},    {0x201D, 2130, ClearSet::message, false},
    {0x201E, 2132, ClearSet::message, false},    {0x201F, 2134, ClearSet::message, false},
    {0x2024, 2144, ClearSet::message, false},    {0x2026, 2147, ClearSet::message, false},
    {0x2027, 2150, ClearSet::message, false},    {0x2028, 2152, ClearSet::message, false},
    {0x2029, 2154, ClearSet::message, false},    {0x202A, 2156, ClearSet::message, false},
    {0x202B, 2300, ClearSet::message, false},    {0x202C, 2301, ClearSet::message, false},
    {0x202D, 2302, ClearSet::message, false},    {0x202E, 2304, ClearSet::slot_warning, false},
    {0x2100, 2200, ClearSet::slot_label, false}, {0x2101, 2201, ClearSet::slot_label, false},
    {0x2200, 2400, ClearSet::slot_label, false}, {0x2201, 2401, ClearSet::slot_label, false},
};

const TextOrderEntry* find_entry(int id) {
    for (const auto& entry : kTextOrder)
        if (entry.id == (id & 0xFFFF)) return &entry;
    return nullptr;
}

void clear_lines(Engine& engine, const TextOrderEntry& entry, int player) {
    auto clear = [&](std::string_view layer, int index) { text_line_clear(engine, layer, player, index); };
    switch (entry.clears) {
    case ClearSet::join: clear("CHRSELFONT01", 0); break;
    case ClearSet::choice: clear("CHRSELFONT02", 0); clear("CHRSELFONT02", 1); break;
    case ClearSet::difficulty:
        for (int i = 0; i < 3; ++i) clear("CHRSELFONT15", i);
        clear("CHRSELFONT07", 0);
        clear("CHRSELFONT07", 1);
        break;
    case ClearSet::message:
        for (int i = 0; i < 4; ++i) clear("CHRSELFONT06", i);
        for (int i = 0; i < 2; ++i) clear("CHRSELFONT16", i);
        break;
    case ClearSet::choice_lines: clear("CHRSELFONT16", 0); clear("CHRSELFONT16", 1); break;
    case ClearSet::slots: clear("CHRSELFONT07", 0); clear("CHRSELFONT07", 1); break;
    case ClearSet::slot_label: clear("CHRSELFONT07", 10); break;
    case ClearSet::slot_warning: clear("CHRSELFONT07", 11); break;
    case ClearSet::box: clear("CHRSELFONT08", entry.id & 3); break;
    case ClearSet::status: clear("CHRSELFONT09", 1); break;
    case ClearSet::status_check: clear("CHRSELFONT09", 0); break;
    }
}

void run_call(Engine& engine, int id, int call) {
    const int player = (id >> 16) & 0xF;
    if (auto* sys = engine.get<GameSystem>()) sys->text_call = player * 10000 + call;
    engine.run_script((id & 0xF000) == 0 ? "txt_mem.lua" : "txt_Tar.lua");
}

} // namespace

bool query(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
           std::string_view command, std::optional<std::string_view> argument) {
    Packet packet;
    if (argument) packet.strings.emplace_back(std::string(*argument));
    engine.messenger().send(engine, category, name, index, command, packet);
    return !packet.bools.empty() && packet.bools[0];
}

std::size_t send(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
                 std::string_view command) {
    Packet packet;
    return engine.messenger().send(engine, category, name, index, command, packet);
}

void text_command(Engine& engine, std::string_view command, std::initializer_list<std::string_view> strings) {
    Packet packet;
    for (auto s : strings) packet.strings.emplace_back(std::string(s));
    engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, command, packet);
}

std::string player_font(int player, int index) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "font%d%02d", player, index);
    return buffer;
}

void text_order_show(Engine& engine, int id, bool continued) {
    const auto* entry = find_entry(id);
    if (!entry) {
        engine.note("TEXTORDER id " + std::to_string(id & 0xFFFF));
        return;
    }
    clear_lines(engine, *entry, (id >> 16) & 0xF);
    run_call(engine, id, entry->call);
    if (continued && entry->continued && entry + 1 != std::end(kTextOrder)) run_call(engine, id, (entry + 1)->call);
}

void text_order_clear(Engine& engine, int id) {
    if (const auto* entry = find_entry(id)) clear_lines(engine, *entry, (id >> 16) & 0xF);
}

void text_line_clear(Engine& engine, std::string_view layer, int player, int index) {
    text_command(engine, "SWITCH", {layer, player_font(player, index), "CLR"});
}

void text_set_literal(Engine& engine, std::string_view layer, int player, int index, std::string_view text) {
    const auto font = player_font(player, index);
    text_command(engine, "ALIGN", {layer, font, "CENTER"});
    text_command(engine, "DEFAULTCOLOR", {layer, font});
    text_command(engine, "TEXTSET", {layer, font, text});
    text_command(engine, "SWITCH", {layer, font, "SET"});
}

void text_set_text(Engine& engine, std::string_view layer, int player, int index, std::string_view text, int align,
                   bool cancel_color) {
    const auto font = player_font(player, index);
    text_command(engine, "ALIGN", {layer, font, align == 1 ? "CENTER" : align == 2 ? "RIGHT" : "LEFT"});
    text_command(engine, "TEXTSET", {layer, font, text});
    text_command(engine, "SWITCH", {layer, font, "SET"});
    text_command(engine, cancel_color ? "CANCELCOLOR" : "DEFAULTCOLOR", {layer, font});
}

void text_set_string(Engine& engine, std::string_view layer, int player, int index, const std::u16string& text,
                     int align, bool cancel_color) {
    const auto font = player_font(player, index);
    text_command(engine, "ALIGN", {layer, font, align == 1 ? "CENTER" : align == 2 ? "RIGHT" : "LEFT"});
    Packet set;
    set.strings = {std::string(layer), font};
    auto value = text;
    set.objects.push_back(&value); // a string object, like 80306E2C
    engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", set);
    text_command(engine, "SWITCH", {layer, font, "SET"});
    text_command(engine, cancel_color ? "CANCELCOLOR" : "DEFAULTCOLOR", {layer, font});
}

const PartnerRow kPartnerRows[16] = {
    {"p_agumon", "SQ_agumonOPEN", "SQ_p_agumon"},       {"p_vmon", "SQ_vmonOPEN", "SQ_p_vmon"},
    {"p_girumon", "SQ_girumonOPEN", "SQ_p_girumon"},    {"p_dorumon", "SQ_dorumonOPEN", "SQ_p_dorumon"},
    {"p_weregaru", "SQ_weregaruOPEN", "SQ_p_weregaru"}, {"p_hekabut", "SQ_hekabutOPEN", "SQ_p_hekabut"},
    {"p_wargrey", "SQ_wargreyOPEN", "SQ_p_wargrey"},    {"p_angelr", "SQ_angelrOPEN", "SQ_p_angelr"},
    {"p_beelzeb", "SQ_beelzebOPEN", "SQ_p_beelzeb"},    {"p_alpha", "SQ_alphaOPEN", "SQ_p_alpha"},
    {"p_bwargrey", "SQ_bwargreyOPEN", "SQ_p_bwargrey"}, {"p_impdrafm", "SQ_impdrafmOPEN", "SQ_p_impdrafm"},
    {"p_impdrapm", "SQ_impdrapmOPEN", "SQ_p_impdrapm"}, {"p_metalgaru", "SQ_metalgaruOPEN", "SQ_p_metalgaru"},
    {"p_dukecrim", "SQ_dukecrimOPEN", "SQ_p_dukecrim"}, {"p_susanoo", "SQ_susanooOPEN", "SQ_p_susanoo"},
};

int partner_row(std::string_view model) {
    for (int i = 0; i < 16; ++i)
        if (model == kPartnerRows[i].model) return i;
    return 0;
}

} // namespace dw4::engine::menu
