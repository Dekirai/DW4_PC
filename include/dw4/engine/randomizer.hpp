#pragma once

// The randomizer (a PC option, off by default; Cheats::rand_seed and the
// modes). Everything it decides comes from the master seed and the game's
// own state (stage, generator name, spawn count), never from the fight
// random number generator or the host clock, so save state replays, the
// campaign driver and lockstep online sessions see the same world.
//
// Seeds: the item drops keep using the master seed itself (Cheats::drop_seed,
// GeneraterManager::randomized_record, unchanged results for a seed); the
// enemy and loading zone shuffles use splitmix64 sub-seeds with a salt.
//
// Enemy shuffle: a generator's enemies become other families while keeping
// the original enemy record (HP, strength, EXP; drops belong to the
// generator). Fixed: boss stages (stagBS*), the stagGF03 fights, bosses and
// 1-RAM mid-bosses, a family whose ded_<family>.lua the stage has, and every
// generator a stage script starts next to an e_ / f_ model name (intro and
// demo fights). See GeneraterManager.
//
// Loading zone shuffle (mode 1): the dungeon entrances within an area swap,
// coupled (the dungeon's way back leads to the entrance used); towers, quests,
// boss chains, the lobby, gate disks and defeats keep their targets.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::engine {
struct Cheats;
class Engine;
}

namespace dw4::engine::randomizer {

// splitmix64's step (state advanced by the golden gamma).
std::uint64_t splitmix64(std::uint64_t& state);
// A sub-seed of the master seed for one shuffle ("enemies", "zones").
std::uint64_t sub_seed(std::uint32_t master, std::string_view salt);
// A draw for one decision: the sub-seed, a name (stage + generator) and a
// counter.
std::uint64_t draw(std::uint64_t seed, std::string_view name, std::uint64_t counter);

// ---------------------------------------------------------------------------
// Enemy families
// ---------------------------------------------------------------------------
struct Family {
    const char* name;  // the model name (beModelCtrlInfo / MODEL of the enemy record)
    int tier;          // the area it first appears in (1-4), 0 for bosses
    bool boss;         // never swapped: bosses and 1-RAM mid-bosses
    const char* donor; // the smallest stage member holding its six infos (enemy pool loads)
};
std::span<const Family> families();
const Family* family(std::string_view name);
bool boss_family(std::string_view name);
// A stage whose fights all stay (stagBS*, stagGF03) or that has no enemies.
bool fixed_stage(std::string_view stage);
// The area tier of a stage (1-4; quests by their area, gate fields 1).
int stage_tier(std::string_view stage);
// The weight of a family of tier `family_tier` in the pool of a stage of tier
// `stage_tier`: its own tier most, one apart less, further rarely.
int tier_weight(int stage_tier, int family_tier);
// The enemy pool of a stage: the families already loaded there (`present`,
// shuffleable ones only) plus up to `extra` more drawn from the game's
// families by tier weight (seeded by stage), at most 6 in all. The order is
// the draw order (present ones first).
// How many families a stage's pool takes beyond its own (from donor stages,
// Engine::load_enemy_donors), the pool holding at most 6 in all.
constexpr int kPoolExtra = 6;
std::vector<std::string> stage_pool(std::uint64_t enemy_seed, std::string_view stage, const std::vector<std::string>& present,
                                    int extra);

// ---------------------------------------------------------------------------
// Loading zones (mode 1: dungeon entrances within an area)
// ---------------------------------------------------------------------------
struct ZoneEntrance {
    const char* field;   // the field stage with the entrance mat
    const char* mat;     // its mat (funcStageChange in mat<name>.lua)
    int text;            // the exit dialog's STCHGFONT00 line (usa_pack beFontInfo index)
    const char* dungeon; // the dungeon's first floor (entered at point 0)
    int back_point;      // the field's player point the dungeon's way back (b1 matz01) leads to
    int area;
};
std::span<const ZoneEntrance> zone_entrances();
// For each entrance (zone_entrances() order) the entrance whose dungeon it
// leads to now: a seeded permutation within every area without fixed points.
std::vector<int> zone_permutation(std::uint64_t zone_seed);
struct StageTarget {
    std::string stage;
    int point{};
};
// The stage change (from, target, player point) after the shuffle, or none
// when it is not one of the shuffled entrances / ways back.
std::optional<StageTarget> zone_remap(std::uint64_t zone_seed, std::string_view from, std::string_view target, int point);
// The exit dialog of a shuffled entrance or way back names the new target:
// for the current stage's STCHGFONT00 text index, the stage whose
// <stage>/separate/usa_pack.igb holds that name and its index there.
std::optional<StageTarget> zone_dialog_source(std::uint64_t zone_seed, std::string_view stage, int text);
// That name for the running stage's line `text` (read from the other
// stage's usa_pack.igb once), none when the line is not shuffled.
std::optional<std::u16string> zone_dialog_text(Engine& engine, int text);

// The spoiler log of a randomized game (seed, modes, the loading zone table,
// every stage's enemy pool).
std::string spoiler(const Cheats& cheats);

} // namespace dw4::engine::randomizer
