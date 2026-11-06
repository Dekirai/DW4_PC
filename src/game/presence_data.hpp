#pragma once

// Readable names for the PC host's presence features (Discord Rich Presence,
// overhead name tags, achievements): the areas of the stage folders, the 16
// player forms and the difficulty. The game has no table of area names; the
// one here is copied from its loading cards (loadingUSA/ld*.igb, the region
// and the place they show) and the forms from the "shinka" table's NAME and
// MODEL_NAME columns, so the host can name them in every stage (the
// database is not loaded everywhere).
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace dw4::game {

// One stage folder: its Discord large image key (an area group), the region
// and the place its loading card names, and the online world's columns
// (docs/SEAMLESS.md 19.30.2), each explicit, none derived from the others:
// stagBS01-03's dungeon keys and the Fortress's area follow from no image
// key or stage name.
struct AreaInfo {
    const char* folder;  // stage folder, lower case ("dng101b1")
    const char* key;     // art asset key of its area group ("area_death_valley_dungeons")
    const char* region;  // "Death Valley"
    const char* place;   // "Humid Cave"
    const char* area;    // the world's area: "death_valley", "dry_land", "venom_jungle", "machine_pit"; nullptr for hub and UI stages
    const char* kind;    // "hub" (stagIS*, stagML*), "field", "dungeon" or "boss"
    const char* dungeon; // dungeon and boss stages: the dungeon they belong to ("dng205", stagBS01 "dng103", BS04-06 "zone_x", "dngq1"); else nullptr
    bool gate;           // an arrival stage of the lobby gate after the training (stag0101, 0201, 0301, 0401)
};

// An area group's art: the key, a title and the loading card the suggested
// picture comes from (loadingUSA/<card>.igb).
struct AreaImage {
    const char* key;
    const char* title;
    const char* card;
};

std::span<const AreaInfo> area_table();
std::span<const AreaImage> area_images();
// The area of a stage folder (any letter case); nullptr for an unknown one.
const AreaInfo* find_area(std::string_view stage);
// "Death Valley: Humid Cave" ("Practice Stage" when region and place agree).
std::string area_display_name(const AreaInfo& area);
// The table as data/online/areas.json holds it (format dw4-areas/2: stage
// folder, display name, area, kind, dungeon and gate per line, LF): the
// DW4Online server reads it (DW4Online.Domain.World.AreaTable: the website's
// stage and area names, the relay's areas, docs/SEAMLESS.md 19.30.2); the
// presence test compares the file.
std::string area_names_json();
// The title, logo, character select and save screens (stagIS*).
bool is_menu_stage(std::string_view stage);

// Discord's art key of the title / menus.
inline constexpr const char* kLogoImageKey = "logo";

struct FormInfo {
    int species;       // "shinka" row
    const char* model; // MODEL_NAME, the player model ("p_agumon")
    const char* name;  // the game's NAME ("Agumon", "BK WarGreymon")
    const char* key;   // art asset key ("form_agumon")
};
std::span<const FormInfo> forms(); // the 16 rows in order
const FormInfo* form_by_species(int species);
const FormInfo* form_by_model(std::string_view model);

// "Normal", "Hard", "Very Hard" (GameSystem::difficulty 0..2).
const char* difficulty_name(int difficulty);

// PLAYERNAME (full-width forms, U+FF01..U+FF5E, the ideographic space) as
// ASCII; other characters become '?'. Stops at a terminating 0.
std::string ascii_player_name(std::u16string_view name);

} // namespace dw4::game
