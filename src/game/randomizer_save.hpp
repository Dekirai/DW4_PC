#pragma once

// The randomizer's bond to a save: a randomized game keeps its seed and modes
// for its whole run, whatever settings.ini says later. The bond is a sidecar
// next to the save, randomizer.ini, with one section per saved character
// (keyed by the character record's UNIQUE, the value the game draws from the
// world clock when the character is made), and a spoiler log beside it:
//
//   saves/randomizer.ini                      [character_1A2B3C4D] seed=..., drops=..., enemies=..., zones=...
//   saves/randomizer_1A2B3C4D_spoiler.txt     the seed, the modes, the loading zone table, the enemy pools
//
// The folder is the one that holds the save (saves/ next to the program; a
// test run's --card-dir / DW4_CARD_DIR folder). Online sessions use the
// host's settings and bind nothing.
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
    int zones{};   // 0 off, 1 dungeon entrances within an area
    bool active() const { return seed != 0 && (drops || enemies != 0 || zones != 0); }
    bool operator==(const RandomizerConfig&) const = default;
};

// The config into the cheats (drop_seed = seed with drops on) and back.
void apply_randomizer(const RandomizerConfig& config, engine::Cheats& cheats);
RandomizerConfig randomizer_from_cheats(const engine::Cheats& cheats);
// "seed[,enemies[,zones[,drops]]]" (test runs: DW4_RANDOMIZER).
std::optional<RandomizerConfig> parse_randomizer(std::string_view text);

std::optional<RandomizerConfig> load_randomizer_binding(const std::filesystem::path& folder, std::uint32_t unique);
bool store_randomizer_binding(const std::filesystem::path& folder, std::uint32_t unique, std::string_view name,
                              const RandomizerConfig& config);
// Writes randomizer_<UNIQUE>_spoiler.txt for the config; false on an error.
bool write_randomizer_spoiler(const std::filesystem::path& folder, std::uint32_t unique, const RandomizerConfig& config);
std::string randomizer_unique_key(std::uint32_t unique);

} // namespace dw4::game
