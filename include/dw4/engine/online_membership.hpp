#pragma once

// Seamless online co-op: what a member carries between instances and what a
// barrier decides (docs/SEAMLESS.md 3.4, 6.6, 6.7, 7.3), with their byte
// layouts for the relay (StateArchive, little endian; see the appendix of
// SEAMLESS.md).

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_story.hpp" // RoomStory, StoryKeys

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace dw4::engine {

// What a member brings into an instance (a JOIN or a co-arrival) and keeps
// aside when it leaves one: its player slot (GameSystem::players[slot],
// joined forced on), its bit capsule (bit_chips[slot]) and its personal
// session values. `record` is the host's copy of the member's 3,316-byte
// card record (empty unless the client fills it; the engine ignores it).
struct PlayerCarry {
    GameSystem::PlayerSlot slot;
    GameSystem::BitChip chip;
    PersonalSession personal;
    std::vector<std::uint8_t> record;
};

// BARRIER_OK's contents (6.3, 6.7), handed to the engine while it is parked
// after the barrier tick (Engine::set_barrier) and applied in the load step
// right before begin_stage_load, after the module resets and the held fields,
// in this order: the room story (round S2), the membership, the rules.
//   new_instance - slot_map[old slot] = the slot in the new instance (-1: the
//                  slot is not in this group); co_arrivals are spawned there
//                  as joined players before any stage script runs; own_slot
//                  is this machine's slot afterwards (the split identity);
//   dissolve     - every other slot is despawned, the own player moves to
//                  slot 0 and its personal session values are written back;
//   join / co_arrive - the engine goes on into its load step unchanged (the
//                  client keeps that load snapshot's bytes as its fallback
//                  and then catches up on the target's snapshot and log).
// `rules` become Engine::online() for the new visit (present is recomputed
// from the slots joined after the membership; boss_lock is always cleared).
struct BarrierResult {
    Decision decision = Decision::new_instance;
    std::array<int, 4> slot_map{0, 1, 2, 3};
    std::vector<std::pair<int, PlayerCarry>> co_arrivals;
    int own_slot = 0;
    std::optional<RoomStory> story;
    StoryKeys dropped;
    // Round I1a (docs/SEAMLESS.md 19.8.3, 19.30.4): merge (the rounds before)
    // or replace (the return home with the own story, a guest's NEW with the
    // host's): the story subset becomes `story` (story::replace), before the
    // 503 rule and the snapshot. `dropped` is not used with replace.
    StoryMode story_mode = StoryMode::merge;
    OnlineRules rules;
};

// Byte layouts (StateArchive). The carry: the PlayerSlot and BitChip as the
// stage snapshot writes them, the personal values, the record.
std::vector<std::uint8_t> encode_carry(const PlayerCarry& carry);
PlayerCarry decode_carry(std::span<const std::uint8_t> bytes); // throws on damaged data
std::vector<std::uint8_t> encode_barrier(const BarrierResult& barrier);
BarrierResult decode_barrier(std::span<const std::uint8_t> bytes);

// The byte layout version of carries and barriers (follows kSnapshotVersion).
std::uint32_t online_layout_version();

} // namespace dw4::engine
