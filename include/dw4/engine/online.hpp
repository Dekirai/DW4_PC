#pragma once

// Seamless online co-op, the engine side (docs/SEAMLESS.md sections 3.7, 6.5,
// 6.7, 7). Everything here only acts while Engine::online().enabled (or a
// nonzero camera mask): with the defaults below the engine is the original
// game, bit for bit (det.sh, the story runs and the save state suite prove it
// in every round).
//
// This header holds the plain value types the engine keeps (it is included by
// engine.hpp). The types that carry a GameSystem player slot (PlayerCarry,
// BarrierResult) and their byte layouts are in online_membership.hpp.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::engine {

class Engine;

// The switches that turn the online-only behaviour on (7.2). Simulation
// state: part of the stage snapshot (a joiner restores it) and of the net
// fingerprint, not of Engine::fingerprint. Defaults are all off. Changed only
// between steps (Engine::set_online and the membership calls), at a tick every
// member applies.
struct OnlineRules {
    bool enabled = false;            // overrides, despawn, pad redirect, input masking
    std::uint8_t player_cameras = 0; // bit per slot with its own box camera; 0 = shared camera (original)
    bool leash_off = false;          // limit_Circle child hit off
    bool scoped_scenes = false;      // proximity rule for local scenes and the status window (round S3)
    bool near_share = false;         // EXP shared by members near the kill (round S3, question 7)
    std::uint8_t present = 0;        // instance members (bit per slot)
    std::uint8_t leaving = 0;        // members with a pending LEAVE (bit per slot)
    bool boss_lock = false;          // a boss fight runs (set by boss_mat / boss_intro, cleared by boss_end and barriers)
    bool operator==(const OnlineRules&) const = default;
};

// The personal session values of one member (3.7, data/online/session_values.txt):
// GATESTAGE and the flags only hub stages write and no joinable stage reads.
// They travel with the member: kept aside at a join (PlayerCarry), written
// back into the member's engine at DISSOLVE. A flag absent from `flags` was
// absent from the engine's FLAG table (FLGisOn creates zero entries, so
// absent and 0 are told apart to restore exactly).
struct PersonalSession {
    std::optional<std::string> gate_stage_name;
    std::int32_t gate_stage_no = 0;
    std::map<std::int32_t, std::uint8_t> flags;
    bool operator==(const PersonalSession&) const = default;
};

// The kinds of a fork (6.7). `mask` in Engine::online_split names the side
// that continues the script run:
//   exit     - the leavers through an exit or the gate disc; the stayers keep
//              the instance (the leavers become a departing child);
//   in_stage - the participants of a story trigger or a "Go without them";
//              they keep the instance, the others become a new joinable one;
//   give_up  - a defeated member gives up; the only fork that makes a side
//              of defeated members on purpose; the others keep the instance.
//   home     - round I1a (docs/SEAMLESS.md 19.6.5): HOME, a server decision
//              about accounts: exactly the mask leaves, defeated or not (no
//              side rule); like give_up applied before a step, so its
//              recorded side is one frame later than an exit's.
enum class ForkKind : std::uint8_t { exit = 0, in_stage = 1, give_up = 2, home = 3 };

// How a barrier's story reaches the engine (round I1a, 19.8.3 / 19.30.4):
// merge (rounds S2-G: the story's values added to the engine's) or replace
// (the return home and a guest's NEW: the story subset becomes the story's).
enum class StoryMode : std::uint8_t { merge = 0, replace = 1 };

// A fork in a replayed history (STATE_DATA): at the step of `frame` (the
// engine's frame() after that step), take the side named by `leavers` when
// `take_leavers`, else the other one. `leavers` may name either side: the
// continuing side of online_split (SplitResult::leavers) or the side that
// leaves the instance id (SplitResult::child, the relay's leaver mask).
struct ForkSide {
    std::uint64_t frame{};
    std::uint8_t leavers{};
    bool take_leavers{};
    bool operator==(const ForkSide&) const = default;
};

// The fork of the last step (Engine::take_split). `leavers` is the side that
// continued the script run, after the side rule for defeated members;
// `kept` the side that keeps the instance id; `local_leaves` whether this
// copy took the leavers' side; `child` the side that leaves the instance id
// (exit and give up: the leavers; in-stage: the others), which the relay's
// SPLIT and fork lists name. The copy's side is already applied (the other
// side's players are despawned at the end of the step).
struct SplitResult {
    std::uint64_t frame{};
    std::uint8_t leavers{};
    std::uint8_t kept{};
    bool in_stage{};
    bool local_leaves{};
    std::uint8_t child{};
    bool operator==(const SplitResult&) const = default;
};

// A member's port choice that enters the simulation (INTENT, 6.3).
enum class IntentKind : std::uint8_t { go_without = 0, give_up = 1 };

// The server's barrier decision (3.4).
enum class Decision : std::uint8_t { new_instance = 0, join = 1, dissolve = 2, co_arrive = 3 };

// Engine::snapshot_bytes: the raw bytes (what a save state file and the
// uploaded instance snapshot keep) or the canonical form for hashes and
// comparisons, with every presentation-only field zeroed (today the music
// streams' decoding position, SoundManager's frames_read: it advances only
// where the host renders audio; 6.8).
enum class SnapshotForm : std::uint8_t { raw = 0, canonical = 1 };

// An exit the local copy takes (the funcStageChange port or the gate disc,
// 5.2): the client sends HEADING with it, one to two seconds before the
// barrier.
struct ExitHeading {
    std::uint64_t frame{};
    std::string stage;
    int point{};
    std::uint8_t leavers{}; // the members that go (everyone present: the original group move)
};

// What the exit port shows right now (presentation: the client's texts, the
// draw filter of the prompt). Slots are instance slots, -1 none.
struct ExitPromptView {
    bool active{};            // an exit prompt or its departure runs
    int owner = -1;           // the member who stepped on and answered first
    int asking = -1;          // the member whose prompt is open (the owner first)
    std::uint8_t yes{};       // members who said yes so far (the owner included)
    std::uint8_t stays{};     // present members who would stay if the owner alone went
    std::uint8_t sees_fade{}; // members whose picture shows the exit's fade (i_fadewn01)
    std::string stage;        // the destination
    bool boss_destination{};  // a boss stage (warning text)
};

// The exit port's prompt owner (5.2): the lowest present, living member whose
// bit is in current & ~previous of the mat (it stepped on in this update); -1
// for none: a mask change by a step off, a death or a despawn asks nobody
// (never MATgetPlayerNo's slot-0 fallback).
constexpr int exit_prompt_owner(std::uint32_t current, std::uint32_t previous, std::uint32_t living, std::uint8_t present) {
    const std::uint32_t stepped = current & ~previous & living & present & 0xFu;
    for (int slot = 0; slot < 4; ++slot)
        if (stepped & (1u << slot)) return slot;
    return -1;
}

// The side rule of an exit-type departure (6.7): the living members among
// `going` leave; defeated members stay with the living ones who stay; when no
// living member stays, every present member goes (the original group move,
// no fork). 0 when no living member goes. Engine::online_split applies the
// same rule.
constexpr std::uint8_t exit_leavers(std::uint8_t present, std::uint8_t living, std::uint8_t going) {
    living &= present;
    const auto leaving = std::uint8_t(going & living);
    if (!leaving) return 0;
    if ((living & ~leaving) == 0) return present;
    return leaving;
}

// The engine's view of the room's settings that reach the simulation online
// (6.12: cheats and rebalancing are refused; the randomizer, the drops and
// fast loading come from the room).
struct Cheats;
Cheats online_cheats(const Cheats& room);

// Session value tables (3.7), generated by tools/online/session_census.py
// into data/online/session_values.txt; a ctest keeps them equal.
struct SessionValueTables {
    std::vector<std::int32_t> context_flags;
    std::vector<std::int32_t> context_counters;
    std::vector<std::int32_t> context_folders; // flag folder entries
    std::vector<std::int32_t> personal_flags;  // GATESTAGE is personal as well
};
const SessionValueTables& session_value_tables();

// The engine's environment variables that would change the simulation (6.12).
// An online session refuses to start while any of them is set (the message
// names it); a ctest greps getenv("DW4_..") in src and fails for a variable
// that is neither here nor in online_presentation_env().
const std::vector<std::string_view>& online_forbidden_env();
// Variables of test tools and the presentation that never change the
// simulated state (or only in test runs the online session never uses).
const std::vector<std::string_view>& online_presentation_env();
// The forbidden variables that are set in this process.
std::vector<std::string> online_forbidden_env_set();

// The hash of bytes on the wire (FNV-1a, 64 bit): SNAPHASH is
// online_hash(Engine::snapshot_bytes(SnapshotForm::canonical)) (6.8).
std::uint64_t online_hash(std::span<const std::uint8_t> bytes);

// The net fingerprint's camera part (6.9): every slot's PlayerBox state
// (CameraManager::player_box_hash; 0 without per-slot cameras).
std::uint64_t net_fingerprint_cameras(const Engine& engine);

} // namespace dw4::engine
