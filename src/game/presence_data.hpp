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
// and the place its loading card names.
struct AreaInfo {
    const char* folder; // stage folder, lower case ("dng101b1")
    const char* key;    // art asset key of its area group ("area_death_valley_dungeons")
    const char* region; // "Death Valley"
    const char* place;  // "Humid Cave"
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
