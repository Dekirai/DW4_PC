#pragma once

// Observe-only engine events for the PC host (achievements, presence): what
// happened in a step, reported from a few hooks in the reconstructed game
// logic. The engine collects them only while a host asks for them
// (Engine::set_port_events); collecting never changes the simulation, and
// the list is neither part of Engine::fingerprint() nor of a StageSnapshot,
// so save states, lockstep sessions and the Dolphin comparisons are the same
// with and without a host reading them.
#include <cstdint>
#include <string>
#include <string_view>

namespace dw4::engine {

class Engine;

struct PortEvent {
    enum class Kind : std::uint8_t {
        // A boss fell. text: the boss key (the dungeon bosses' ded_e_<key>.lua
        // death scripts: blosso, mammos, skulgrey, anomalo, tonogeko, mecha4,
        // diabolo, mecha5, ldknight, reported when the script sets its first
        // flag; the boss stages' defeat flags: apocaly 67, belvam 68, luce 69,
        // devl3 70). value: the difficulty.
        boss_defeated,
        // An enemy fell to a hit (BLTcalcEXPUp). slot: the defeating player
        // (-1 none), value: its enemy record, text: its model (e_nume, ...).
        enemy_defeated,
        // slot reached the level in value.
        level_up,
        // slot became the species in value (the Digi-Lab's change; extra: the
        // species before).
        digivolution,
        // slot reached the species in value for the first time (SETSHINKA).
        form_reached,
        // slot can now use the pcTech row in value (a new technique or a
        // higher rank of one; extra: the rank 0..4).
        technique,
        // Player 1's card list got the card in value (extra: the booster
        // card's RARE value, 0 for a script's card).
        card,
        // A quest's goal was reached: value 1..8, extra: the difficulty.
        quest_done,
        // slot was defeated (its bit capsule is left behind).
        player_defeated,
        // slot's hit was critical (BLTisCritical).
        critical_hit,
        // A stage script changed a story flag (FLGset): value the flag,
        // extra the new value.
        story_flag,
        // The character select made a new character for slot (a new game;
        // value: its UNIQUE as int32). The randomizer binds only new games.
        new_character,
    };
    Kind kind{};
    std::int32_t slot = -1;
    std::int32_t value{};
    std::int32_t extra{};
    std::string text;
};

const char* port_event_name(PortEvent::Kind kind);

// FLGset set flag from `before` to `after` (called for every FLGset): a
// running boss death script's boss, and on a change the story flag event
// and, on a rising edge, the boss stages' defeat flags and the quests' goals.
void port_flag_changed(Engine& engine, std::int32_t flag, int before, int after);
// A script starts (Engine::run_script): true for a dungeon boss's death
// script, which port_script_finished must close when the run ends.
bool port_script_started(Engine& engine, std::string_view name);
void port_script_finished(Engine& engine);

} // namespace dw4::engine
