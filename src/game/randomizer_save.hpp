#pragma once

// The randomizer's bond to a save: a randomized game keeps its seed and modes
// for its whole run, whatever settings.ini says later. The bond is a sidecar
// next to the save, randomizer.ini, with one section per saved character
// (keyed by the character record's UNIQUE, the value the game draws from the
// world clock when the character is made), and a spoiler log beside it:
//
//   saves/randomizer.ini                      [character_1A2B3C4D] seed=..., drops=..., enemies=..., zones=..., zone_rule=...
//   saves/randomizer_1A2B3C4D_spoiler.txt     the seed, the modes, the loading zone table, the enemy pools
//
// The folder is the one that holds the save (saves/ next to the program; a
// test run's --card-dir / DW4_CARD_DIR folder). Online sessions use the
// host's settings and bind nothing.
//
// Only a NEW game binds the [randomizer] settings by itself; a called save
// without a bond (one from before, or made with the randomizer off) plays the
// original game until the player binds the settings to it in the F1 menu
// (test runs: DW4_RANDOMIZER binds every character unless
// DW4_RANDOMIZER_BIND=new).
#include "dw4/engine/engine.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace dw4::game {

struct RandomizerConfig {
    std::uint32_t seed{};
    bool drops{};
    int enemies{}; // 0 off, 1 every enemy drawn on its own, 2 one family per wave
    int zones{};   // 0 off, else the dungeon entrance shuffle by its rule (randomizer::kZoneRule*: 1 round 48, 2 current)
    bool active() const { return seed != 0 && (drops || enemies != 0 || zones != 0); }
    bool operator==(const RandomizerConfig&) const = default;
};

// The config into the cheats (drop_seed = seed with drops on) and back.
void apply_randomizer(const RandomizerConfig& config, engine::Cheats& cheats);
RandomizerConfig randomizer_from_cheats(const engine::Cheats& cheats);
// "seed[,enemies[,zones[,drops]]]" (test runs: DW4_RANDOMIZER; zones is the
// rule number, 1 the round 48 table; left out: the rule a new game binds).
std::optional<RandomizerConfig> parse_randomizer(std::string_view text);

// A bond's zones=dungeons without zone_rule (written before the rules were
// numbered) is rule 1, the table that game was played with. A bond with
// seed 0 is an explicit "plays the original game" (the F1 menu's choice).
std::optional<RandomizerConfig> load_randomizer_binding(const std::filesystem::path& folder, std::uint32_t unique);
bool store_randomizer_binding(const std::filesystem::path& folder, std::uint32_t unique, std::string_view name,
                              const RandomizerConfig& config);
// Writes randomizer_<UNIQUE>_spoiler.txt for the config; false on an error.
bool write_randomizer_spoiler(const std::filesystem::path& folder, std::uint32_t unique, const RandomizerConfig& config);
std::string randomizer_unique_key(std::uint32_t unique);
// What a config plays, for the F1 menu: "seed 123 - enemies: each on its
// own, loading zones: dungeon entrances, item drops: shuffled" (the zone
// rule named when it is not the current one), or "the original game".
std::string describe_randomizer(const RandomizerConfig& config);

// What player 1's character gets when it comes into play: its bond; else the
// settings (bound to it) when they randomize and it is a new game (or
// `bind_called`, test runs); else the original game.
enum class BondChoice { use_bond, bind_settings, original };
BondChoice choose_randomizer_bond(bool has_bond, bool new_game, bool settings_active, bool bind_called);

} // namespace dw4::game
