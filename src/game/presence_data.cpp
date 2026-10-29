#include "presence_data.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace dw4::game {
namespace {

// Region and place as each stage's loading card (loadingUSA/ld + the folder
// from its third letter) shows them. stag0000 is a development copy of the
// practice stage (its card in Japanese), stagML14 the Digi-Lab's
// digivolution scene (no card of its own), stagBS04..06 the three Zone X
// fights.
constexpr AreaInfo kAreas[] = {
    {"stagis07", "logo", "Menus", "Menus"},
    {"stagis08", "logo", "Menus", "Menus"},
    {"stagis09", "logo", "Menus", "Menus"},
    {"stagis10", "logo", "Menus", "Menus"},
    {"stagis11", "logo", "Menus", "Menus"},
    {"stagis12", "logo", "Menus", "Menus"},
    {"stagis13", "logo", "Menus", "Menus"},
    {"stagis20", "logo", "Menus", "Menus"},
    {"stagml02", "area_home_server", "Home Server", "Commander Room"},
    {"stagml11", "area_home_server", "Home Server", "Main Lobby: Terminal Area"},
    {"stagml12", "area_home_server", "Home Server", "Main Lobby: Central Area"},
    {"stagml13", "area_home_server", "Home Server", "Main Lobby: Shop Area"},
    {"stagml14", "area_home_server", "Home Server", "Digivolution"},
    {"stag0000", "area_practice", "Death Valley", "Practice Stage"},
    {"stag0001", "area_practice", "Death Valley", "Practice Stage"},
    {"stag0101", "area_death_valley", "Death Valley", "Goblin Pass"},
    {"stag0102", "area_death_valley", "Death Valley", "Abyss of Grief"},
    {"stag0103", "area_death_valley", "Death Valley", "Numenume River"},
    {"stag0104", "area_death_valley", "Death Valley", "Doom Dome"},
    {"dng101b1", "area_death_valley_dungeons", "Death Valley", "Humid Cave"},
    {"dng101b2", "area_death_valley_dungeons", "Death Valley", "Humid Cave"},
    {"dng102b1", "area_death_valley_dungeons", "Death Valley", "Cliff Dungeon"},
    {"dng102b2", "area_death_valley_dungeons", "Death Valley", "Cliff Dungeon"},
    {"dng102b3", "area_death_valley_dungeons", "Death Valley", "Cliff Dungeon"},
    {"dng103f1", "area_death_valley_dungeons", "Death Valley", "Dome Base"},
    {"dng103f2", "area_death_valley_dungeons", "Death Valley", "Dome Base"},
    {"stag0201", "area_dry_land", "Dry Land", "Mummy Desert"},
    {"stag0202", "area_dry_land", "Dry Land", "Grief Wasteland"},
    {"stag0203", "area_dry_land", "Dry Land", "Train Line"},
    {"stag0204", "area_dry_land", "Dry Land", "Station X"},
    {"stag0205", "area_dry_land", "Dry Land", "Power Plant"},
    {"dng204b1", "area_dry_land_dungeons", "Dry Land", "Sand Labyrinth"},
    {"dng204b2", "area_dry_land_dungeons", "Dry Land", "Sand Labyrinth"},
    {"dng205b1", "area_dry_land_dungeons", "Dry Land", "Ancient Ruins"},
    {"dng205b2", "area_dry_land_dungeons", "Dry Land", "Ancient Ruins"},
    {"dng205b3", "area_dry_land_dungeons", "Dry Land", "Ancient Ruins"},
    {"dng206f1", "area_dry_land_dungeons", "Dry Land", "Virus Lab"},
    {"dng206f2", "area_dry_land_dungeons", "Dry Land", "Virus Lab"},
    {"stag0301", "area_venom_jungle", "Venom Jungle", "Gekogeko Land"},
    {"stag0302", "area_venom_jungle", "Venom Jungle", "Mangrove Forest"},
    {"stag0303", "area_venom_jungle", "Venom Jungle", "Venom Swamp"},
    {"stag0304", "area_venom_jungle", "Venom Jungle", "Dread Note"},
    {"dng307b1", "area_venom_jungle_dungeons", "Venom Jungle", "Geko Path"},
    {"dng307b2", "area_venom_jungle_dungeons", "Venom Jungle", "Geko Path"},
    {"dng308b1", "area_venom_jungle_dungeons", "Venom Jungle", "Vine Tunnel"},
    {"dng308b2", "area_venom_jungle_dungeons", "Venom Jungle", "Vine Tunnel"},
    {"dng308b3", "area_venom_jungle_dungeons", "Venom Jungle", "Vine Tunnel"},
    {"dng309b1", "area_venom_jungle_dungeons", "Venom Jungle", "Vein"},
    {"dng309b2", "area_venom_jungle_dungeons", "Venom Jungle", "Vein"},
    {"dng309b3", "area_venom_jungle_dungeons", "Venom Jungle", "Vein"},
    {"dng309b4", "area_venom_jungle_dungeons", "Venom Jungle", "Vein"},
    {"dng310f1", "area_venom_jungle_dungeons", "Venom Jungle", "Dread Bridge"},
    {"dng310f2", "area_venom_jungle_dungeons", "Venom Jungle", "Dread Bridge"},
    {"dng310f3", "area_venom_jungle_dungeons", "Venom Jungle", "Dread Bridge"},
    {"stag0401", "area_machine_pit", "Machine Pit", "Battle Arena"},
    {"stag0403", "area_machine_pit", "Machine Pit", "Mechanical Core"},
    {"stag0404", "area_machine_pit", "Machine Pit", "Core Area"},
    {"dng411b1", "area_machine_pit_dungeons", "Machine Pit", "Mecha Nest"},
    {"dng411b2", "area_machine_pit_dungeons", "Machine Pit", "Mecha Nest"},
    {"dng411b3", "area_machine_pit_dungeons", "Machine Pit", "Mecha Nest"},
    {"dng412b1", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine"},
    {"dng412b2", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine"},
    {"dng412b3", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine"},
    {"dng412b4", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine"},
    {"stagbs01", "area_apocalymon", "Death Valley", "Apokarimon Space"},
    {"stagbs02", "area_belphemon", "Dry Land", "Demon Shift"},
    {"stagbs03", "area_lucemon", "Venom Jungle", "Hades Obelisk"},
    {"stagbs04", "area_zone_x", "Machine Pit", "Zone X"},
    {"stagbs05", "area_zone_x", "Machine Pit", "Zone X"},
    {"stagbs06", "area_zone_x", "Machine Pit", "Zone X"},
    {"staggf01", "area_fortress", "Fortress", "Guard Room"},
    {"staggf02", "area_fortress", "Fortress", "Rotating Hall"},
    {"staggf03", "area_fortress", "Fortress", "X Digimon Lab"},
    {"staggf04", "area_fortress", "Fortress", "GobGob Room"},
    {"dngq1b1e", "area_quests", "Machine Pit", "Barbarian Cave"},
    {"dngq1b2e", "area_quests", "Machine Pit", "Barbarian Cave"},
    {"dngq2b1e", "area_quests", "Machine Pit", "Undead Yard"},
    {"dngq2b2e", "area_quests", "Machine Pit", "Undead Yard"},
    {"dngq2b2h", "area_quests", "Machine Pit", "Undead Yard"},
    {"dngq3b1e", "area_quests", "Machine Pit", "Forbidden Shrine"},
    {"dngq3b2e", "area_quests", "Machine Pit", "Forbidden Shrine"},
    {"dngq4b1e", "area_quests", "Machine Pit", "Captive Dungeon"},
    {"dngq4b1h", "area_quests", "Machine Pit", "Captive Dungeon"},
    {"dngq4b1v", "area_quests", "Machine Pit", "Captive Dungeon"},
    {"dngq4b2e", "area_quests", "Machine Pit", "Captive Dungeon"},
    {"dngq4b2h", "area_quests", "Machine Pit", "Captive Dungeon"},
    {"dngq4b2v", "area_quests", "Machine Pit", "Captive Dungeon"},
    {"dngq5b1e", "area_quests", "Machine Pit", "Trader Hall"},
    {"dngq5b1h", "area_quests", "Machine Pit", "Trader Hall"},
    {"dngq5b1v", "area_quests", "Machine Pit", "Trader Hall"},
    {"dngq6b01", "area_quests", "Machine Pit", "Dungeon of Doom"},
    {"dngq7b1e", "area_quests", "Machine Pit", "Item Road"},
    {"dngq7b2e", "area_quests", "Machine Pit", "Item Road"},
    {"dngq8b1e", "area_quests", "Machine Pit", "Cursed Dungeon"},
    {"dngq8b2e", "area_quests", "Machine Pit", "Cursed Dungeon"},
    {"dngq8b3e", "area_quests", "Machine Pit", "Cursed Dungeon"},
    {"dngq8b3h", "area_quests", "Machine Pit", "Cursed Dungeon"},
    {"dngq8b3v", "area_quests", "Machine Pit", "Cursed Dungeon"},
};

constexpr AreaImage kImages[] = {
    {"logo", "Title / menus", "ldagis11"}, // the title logo; the card only when it is missing
    {"area_home_server", "Home Server", "ldagml12"},
    {"area_practice", "Practice Stage", "ldag0001"},
    {"area_death_valley", "Death Valley", "ldag0102"},
    {"area_death_valley_dungeons", "Death Valley dungeons", "ldg101b1"},
    {"area_dry_land", "Dry Land", "ldag0201"},
    {"area_dry_land_dungeons", "Dry Land dungeons", "ldg205b1"},
    {"area_venom_jungle", "Venom Jungle", "ldag0301"},
    {"area_venom_jungle_dungeons", "Venom Jungle dungeons", "ldg309b1"},
    {"area_machine_pit", "Machine Pit", "ldag0401"},
    {"area_machine_pit_dungeons", "Machine Pit dungeons", "ldg411b1"},
    {"area_apocalymon", "Apokarimon Space (boss)", "ldagbs01"},
    {"area_belphemon", "Demon Shift (boss)", "ldagbs02"},
    {"area_lucemon", "Hades Obelisk (boss)", "ldagbs03"},
    {"area_zone_x", "Zone X (boss)", "ldagbs06"},
    {"area_fortress", "Fortress", "ldaggf03"},
    {"area_quests", "Quest dungeons", "ldgq8b1e"},
};

constexpr FormInfo kForms[] = {
    {0, "p_agumon", "Agumon", "form_agumon"},
    {1, "p_vmon", "Veemon", "form_veemon"},
    {2, "p_girumon", "Guilmon", "form_guilmon"},
    {3, "p_dorumon", "Dorumon", "form_dorumon"},
    {4, "p_weregaru", "WereGarurumon", "form_weregarurumon"},
    {5, "p_hekabut", "H-Kabuterimon", "form_hkabuterimon"},
    {6, "p_wargrey", "WarGreymon", "form_wargreymon"},
    {7, "p_angelr", "Cl-Angemon", "form_clangemon"},
    {8, "p_beelzeb", "Beelzemon BM", "form_beelzemon"},
    {9, "p_alpha", "Alphamon", "form_alphamon"},
    {10, "p_bwargrey", "BK WarGreymon", "form_bkwargreymon"},
    {11, "p_impdrafm", "IPDramonFM", "form_ipdramonfm"},
    {12, "p_impdrapm", "IPDramonPM", "form_ipdramonpm"},
    {13, "p_metalgaru", "MetalGarurumon", "form_metalgarurumon"},
    {14, "p_dukecrim", "Gallantmon CM", "form_gallantmoncm"},
    {15, "p_susanoo", "Susanomon", "form_susanomon"},
};

bool equal_folded(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::span<const AreaInfo> area_table() { return kAreas; }
std::span<const AreaImage> area_images() { return kImages; }

const AreaInfo* find_area(std::string_view stage) {
    for (const auto& area : kAreas)
        if (equal_folded(area.folder, stage)) return &area;
    return nullptr;
}

std::string area_display_name(const AreaInfo& area) {
    if (std::string_view(area.region) == area.place) return area.place;
    return std::string(area.region) + ": " + area.place;
}

bool is_menu_stage(std::string_view stage) {
    return stage.size() >= 6 && equal_folded(stage.substr(0, 6), "stagis");
}

std::span<const FormInfo> forms() { return kForms; }

const FormInfo* form_by_species(int species) {
    return species >= 0 && species < int(std::size(kForms)) ? &kForms[species] : nullptr;
}

const FormInfo* form_by_model(std::string_view model) {
    for (const auto& form : kForms)
        if (equal_folded(form.model, model)) return &form;
    return nullptr;
}

const char* difficulty_name(int difficulty) {
    switch (difficulty) {
    case 1: return "Hard";
    case 2: return "Very Hard";
    default: return "Normal";
    }
}

std::string ascii_player_name(std::u16string_view name) {
    std::string text;
    for (const char16_t c : name) {
        if (c == 0) break;
        if (c >= 0xFF01 && c <= 0xFF5E) text += char(c - 0xFF01 + 0x21);
        else if (c == 0x3000 || c == 0x20) text += ' ';
        else if (c > 0x20 && c < 0x7F) text += char(c);
        else text += '?';
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

} // namespace dw4::game
