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
    {"stagis07", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis08", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis09", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis10", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis11", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis12", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis13", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagis20", "logo", "Menus", "Menus", nullptr, "hub", nullptr, false},
    {"stagml02", "area_home_server", "Home Server", "Commander Room", nullptr, "hub", nullptr, false},
    {"stagml11", "area_home_server", "Home Server", "Main Lobby: Terminal Area", nullptr, "hub", nullptr, false},
    {"stagml12", "area_home_server", "Home Server", "Main Lobby: Central Area", nullptr, "hub", nullptr, false},
    {"stagml13", "area_home_server", "Home Server", "Main Lobby: Shop Area", nullptr, "hub", nullptr, false},
    {"stagml14", "area_home_server", "Home Server", "Digivolution", nullptr, "hub", nullptr, false},
    {"stag0000", "area_practice", "Death Valley", "Practice Stage", "death_valley", "field", nullptr, false},
    {"stag0001", "area_practice", "Death Valley", "Practice Stage", "death_valley", "field", nullptr, false},
    {"stag0101", "area_death_valley", "Death Valley", "Goblin Pass", "death_valley", "field", nullptr, true},
    {"stag0102", "area_death_valley", "Death Valley", "Abyss of Grief", "death_valley", "field", nullptr, false},
    {"stag0103", "area_death_valley", "Death Valley", "Numenume River", "death_valley", "field", nullptr, false},
    {"stag0104", "area_death_valley", "Death Valley", "Doom Dome", "death_valley", "field", nullptr, false},
    {"dng101b1", "area_death_valley_dungeons", "Death Valley", "Humid Cave", "death_valley", "dungeon", "dng101", false},
    {"dng101b2", "area_death_valley_dungeons", "Death Valley", "Humid Cave", "death_valley", "dungeon", "dng101", false},
    {"dng102b1", "area_death_valley_dungeons", "Death Valley", "Cliff Dungeon", "death_valley", "dungeon", "dng102", false},
    {"dng102b2", "area_death_valley_dungeons", "Death Valley", "Cliff Dungeon", "death_valley", "dungeon", "dng102", false},
    {"dng102b3", "area_death_valley_dungeons", "Death Valley", "Cliff Dungeon", "death_valley", "dungeon", "dng102", false},
    {"dng103f1", "area_death_valley_dungeons", "Death Valley", "Dome Base", "death_valley", "dungeon", "dng103", false},
    {"dng103f2", "area_death_valley_dungeons", "Death Valley", "Dome Base", "death_valley", "dungeon", "dng103", false},
    {"stag0201", "area_dry_land", "Dry Land", "Mummy Desert", "dry_land", "field", nullptr, true},
    {"stag0202", "area_dry_land", "Dry Land", "Grief Wasteland", "dry_land", "field", nullptr, false},
    {"stag0203", "area_dry_land", "Dry Land", "Train Line", "dry_land", "field", nullptr, false},
    {"stag0204", "area_dry_land", "Dry Land", "Station X", "dry_land", "field", nullptr, false},
    {"stag0205", "area_dry_land", "Dry Land", "Power Plant", "dry_land", "field", nullptr, false},
    {"dng204b1", "area_dry_land_dungeons", "Dry Land", "Sand Labyrinth", "dry_land", "dungeon", "dng204", false},
    {"dng204b2", "area_dry_land_dungeons", "Dry Land", "Sand Labyrinth", "dry_land", "dungeon", "dng204", false},
    {"dng205b1", "area_dry_land_dungeons", "Dry Land", "Ancient Ruins", "dry_land", "dungeon", "dng205", false},
    {"dng205b2", "area_dry_land_dungeons", "Dry Land", "Ancient Ruins", "dry_land", "dungeon", "dng205", false},
    {"dng205b3", "area_dry_land_dungeons", "Dry Land", "Ancient Ruins", "dry_land", "dungeon", "dng205", false},
    {"dng206f1", "area_dry_land_dungeons", "Dry Land", "Virus Lab", "dry_land", "dungeon", "dng206", false},
    {"dng206f2", "area_dry_land_dungeons", "Dry Land", "Virus Lab", "dry_land", "dungeon", "dng206", false},
    {"stag0301", "area_venom_jungle", "Venom Jungle", "Gekogeko Land", "venom_jungle", "field", nullptr, true},
    {"stag0302", "area_venom_jungle", "Venom Jungle", "Mangrove Forest", "venom_jungle", "field", nullptr, false},
    {"stag0303", "area_venom_jungle", "Venom Jungle", "Venom Swamp", "venom_jungle", "field", nullptr, false},
    {"stag0304", "area_venom_jungle", "Venom Jungle", "Dread Note", "venom_jungle", "field", nullptr, false},
    {"dng307b1", "area_venom_jungle_dungeons", "Venom Jungle", "Geko Path", "venom_jungle", "dungeon", "dng307", false},
    {"dng307b2", "area_venom_jungle_dungeons", "Venom Jungle", "Geko Path", "venom_jungle", "dungeon", "dng307", false},
    {"dng308b1", "area_venom_jungle_dungeons", "Venom Jungle", "Vine Tunnel", "venom_jungle", "dungeon", "dng308", false},
    {"dng308b2", "area_venom_jungle_dungeons", "Venom Jungle", "Vine Tunnel", "venom_jungle", "dungeon", "dng308", false},
    {"dng308b3", "area_venom_jungle_dungeons", "Venom Jungle", "Vine Tunnel", "venom_jungle", "dungeon", "dng308", false},
    {"dng309b1", "area_venom_jungle_dungeons", "Venom Jungle", "Vein", "venom_jungle", "dungeon", "dng309", false},
    {"dng309b2", "area_venom_jungle_dungeons", "Venom Jungle", "Vein", "venom_jungle", "dungeon", "dng309", false},
    {"dng309b3", "area_venom_jungle_dungeons", "Venom Jungle", "Vein", "venom_jungle", "dungeon", "dng309", false},
    {"dng309b4", "area_venom_jungle_dungeons", "Venom Jungle", "Vein", "venom_jungle", "dungeon", "dng309", false},
    {"dng310f1", "area_venom_jungle_dungeons", "Venom Jungle", "Dread Bridge", "venom_jungle", "dungeon", "dng310", false},
    {"dng310f2", "area_venom_jungle_dungeons", "Venom Jungle", "Dread Bridge", "venom_jungle", "dungeon", "dng310", false},
    {"dng310f3", "area_venom_jungle_dungeons", "Venom Jungle", "Dread Bridge", "venom_jungle", "dungeon", "dng310", false},
    {"stag0401", "area_machine_pit", "Machine Pit", "Battle Arena", "machine_pit", "field", nullptr, true},
    {"stag0403", "area_machine_pit", "Machine Pit", "Mechanical Core", "machine_pit", "field", nullptr, false},
    {"stag0404", "area_machine_pit", "Machine Pit", "Core Area", "machine_pit", "field", nullptr, false},
    {"dng411b1", "area_machine_pit_dungeons", "Machine Pit", "Mecha Nest", "machine_pit", "dungeon", "dng411", false},
    {"dng411b2", "area_machine_pit_dungeons", "Machine Pit", "Mecha Nest", "machine_pit", "dungeon", "dng411", false},
    {"dng411b3", "area_machine_pit_dungeons", "Machine Pit", "Mecha Nest", "machine_pit", "dungeon", "dng411", false},
    {"dng412b1", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine", "machine_pit", "dungeon", "dng412", false},
    {"dng412b2", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine", "machine_pit", "dungeon", "dng412", false},
    {"dng412b3", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine", "machine_pit", "dungeon", "dng412", false},
    {"dng412b4", "area_machine_pit_dungeons", "Machine Pit", "Electro Mine", "machine_pit", "dungeon", "dng412", false},
    {"stagbs01", "area_apocalymon", "Death Valley", "Apokarimon Space", "death_valley", "boss", "dng103", false},
    {"stagbs02", "area_belphemon", "Dry Land", "Demon Shift", "dry_land", "boss", "dng206", false},
    {"stagbs03", "area_lucemon", "Venom Jungle", "Hades Obelisk", "venom_jungle", "boss", "dng310", false},
    {"stagbs04", "area_zone_x", "Machine Pit", "Zone X", "machine_pit", "boss", "zone_x", false},
    {"stagbs05", "area_zone_x", "Machine Pit", "Zone X", "machine_pit", "boss", "zone_x", false},
    {"stagbs06", "area_zone_x", "Machine Pit", "Zone X", "machine_pit", "boss", "zone_x", false},
    {"staggf01", "area_fortress", "Fortress", "Guard Room", "death_valley", "field", nullptr, false},
    {"staggf02", "area_fortress", "Fortress", "Rotating Hall", "death_valley", "field", nullptr, false},
    {"staggf03", "area_fortress", "Fortress", "X Digimon Lab", "death_valley", "field", nullptr, false},
    {"staggf04", "area_fortress", "Fortress", "GobGob Room", "death_valley", "field", nullptr, false},
    {"dngq1b1e", "area_quests", "Machine Pit", "Barbarian Cave", "machine_pit", "dungeon", "dngq1", false},
    {"dngq1b2e", "area_quests", "Machine Pit", "Barbarian Cave", "machine_pit", "dungeon", "dngq1", false},
    {"dngq2b1e", "area_quests", "Machine Pit", "Undead Yard", "machine_pit", "dungeon", "dngq2", false},
    {"dngq2b2e", "area_quests", "Machine Pit", "Undead Yard", "machine_pit", "dungeon", "dngq2", false},
    {"dngq2b2h", "area_quests", "Machine Pit", "Undead Yard", "machine_pit", "dungeon", "dngq2", false},
    {"dngq3b1e", "area_quests", "Machine Pit", "Forbidden Shrine", "machine_pit", "dungeon", "dngq3", false},
    {"dngq3b2e", "area_quests", "Machine Pit", "Forbidden Shrine", "machine_pit", "dungeon", "dngq3", false},
    {"dngq4b1e", "area_quests", "Machine Pit", "Captive Dungeon", "machine_pit", "dungeon", "dngq4", false},
    {"dngq4b1h", "area_quests", "Machine Pit", "Captive Dungeon", "machine_pit", "dungeon", "dngq4", false},
    {"dngq4b1v", "area_quests", "Machine Pit", "Captive Dungeon", "machine_pit", "dungeon", "dngq4", false},
    {"dngq4b2e", "area_quests", "Machine Pit", "Captive Dungeon", "machine_pit", "dungeon", "dngq4", false},
    {"dngq4b2h", "area_quests", "Machine Pit", "Captive Dungeon", "machine_pit", "dungeon", "dngq4", false},
    {"dngq4b2v", "area_quests", "Machine Pit", "Captive Dungeon", "machine_pit", "dungeon", "dngq4", false},
    {"dngq5b1e", "area_quests", "Machine Pit", "Trader Hall", "machine_pit", "dungeon", "dngq5", false},
    {"dngq5b1h", "area_quests", "Machine Pit", "Trader Hall", "machine_pit", "dungeon", "dngq5", false},
    {"dngq5b1v", "area_quests", "Machine Pit", "Trader Hall", "machine_pit", "dungeon", "dngq5", false},
    {"dngq6b01", "area_quests", "Machine Pit", "Dungeon of Doom", "machine_pit", "dungeon", "dngq6", false},
    {"dngq7b1e", "area_quests", "Machine Pit", "Item Road", "machine_pit", "dungeon", "dngq7", false},
    {"dngq7b2e", "area_quests", "Machine Pit", "Item Road", "machine_pit", "dungeon", "dngq7", false},
    {"dngq8b1e", "area_quests", "Machine Pit", "Cursed Dungeon", "machine_pit", "dungeon", "dngq8", false},
    {"dngq8b2e", "area_quests", "Machine Pit", "Cursed Dungeon", "machine_pit", "dungeon", "dngq8", false},
    {"dngq8b3e", "area_quests", "Machine Pit", "Cursed Dungeon", "machine_pit", "dungeon", "dngq8", false},
    {"dngq8b3h", "area_quests", "Machine Pit", "Cursed Dungeon", "machine_pit", "dungeon", "dngq8", false},
    {"dngq8b3v", "area_quests", "Machine Pit", "Cursed Dungeon", "machine_pit", "dungeon", "dngq8", false},
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

std::string area_names_json() {
    std::string out = "{\n  \"format\": \"dw4-areas/2\",\n  \"source\": \"src/game/presence_data.cpp (dw4_presence_tests --export-areas)\",\n";
    out += "  \"areas\": [\n";
    const auto text_or_null = [](const char* value) { return value ? std::string("\"") + value + "\"" : std::string("null"); };
    for (std::size_t i = 0; i < std::size(kAreas); ++i) {
        // (Folders, names and keys are plain ASCII without quotes or backslashes.)
        const auto& a = kAreas[i];
        out += std::string("    {\"stage\": \"") + a.folder + "\", \"name\": \"" + area_display_name(a) + "\", \"area\": " + text_or_null(a.area) +
               ", \"kind\": \"" + a.kind + "\", \"dungeon\": " + text_or_null(a.dungeon) + ", \"gate\": " + (a.gate ? "true" : "false") + "}";
        out += i + 1 < std::size(kAreas) ? ",\n" : "\n";
    }
    return out + "  ]\n}\n";
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
