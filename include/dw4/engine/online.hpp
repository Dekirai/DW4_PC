#pragma once

// Seamless online co-op (docs/SEAMLESS.md section 7): the switches that turn
// the online-only behaviour on. All off (the default, every offline game,
// save state, story run and direct --host / --join session) is the original
// game: no path below changes anything then.

#include <cstdint>
#include <string>

namespace dw4::engine {

struct OnlineRules {
    bool enabled = false;            // overrides, despawn, pad redirect, input masking
    std::uint8_t player_cameras = 0; // bit per slot with its own box camera; 0 = shared camera (original)
    bool leash_off = false;          // limit_Circle child hit off
    bool scoped_scenes = false;      // proximity rule for local scenes and the status window (round S3)
    bool near_share = false;         // EXP shared by members near the kill (round S3, question 7)
    std::uint8_t present = 0;        // instance members
    std::uint8_t leaving = 0;        // members with a pending LEAVE
    bool boss_lock = false;          // a boss fight runs (set by boss_mat / boss_intro, cleared by boss_end and barriers)
    bool operator==(const OnlineRules&) const = default;
};

// A fork decided inside the simulation (SEAMLESS.md 6.7): `leavers` go one
// way, the other present members the other. `in_stage`: a fork that keeps the
// stage for both sides (boss and story mats, round S3); exit-type forks (an
// exit, the gate disc, give up) are not. `local_leaves`: this copy's slot
// (Engine::set_split_identity) is on the leavers' side, which this copy
// continues; the other side's players are despawned already.
struct SplitResult {
    std::uint64_t frame{};
    std::uint8_t leavers{};
    bool in_stage{};
    bool local_leaves{};
};

// A member's port choice (SEAMLESS.md 6.3 INTENT), applied before a step.
enum class IntentKind : std::uint8_t {
    go_without = 0, // round S3: the members on a boss or story mat proceed without the others
    give_up = 1,    // a defeated member leaves alone (game over in its copy)
};

// An exit the local copy takes (the funcStageChange port or the gate disc,
// SEAMLESS.md 5.2): the client sends HEADING with it, one to two seconds
// before the barrier.
struct ExitHeading {
    std::uint64_t frame{};
    std::string stage;
    int point{};
    std::uint8_t leavers{};  // the members that go (everyone present: the original group move)
};

// What the exit port shows right now (presentation: the client's texts,
// the draw filter of the prompt). Slots are instance slots, -1 none.
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

// The exit port's prompt owner (SEAMLESS.md 5.2): the lowest present, living
// member whose bit is in current & ~previous of the mat (it stepped on in this
// update); -1 for none: a mask change by a step off, a death or a despawn
// asks nobody (never MATgetPlayerNo's slot-0 fallback).
constexpr int exit_prompt_owner(std::uint32_t current, std::uint32_t previous, std::uint32_t living, std::uint8_t present) {
    const std::uint32_t stepped = current & ~previous & living & present & 0xFu;
    for (int slot = 0; slot < 4; ++slot)
        if (stepped & (1u << slot)) return slot;
    return -1;
}

// The side rule of an exit-type departure (SEAMLESS.md 6.7): the living
// members among `going` leave; defeated members stay with the living ones
// who stay; when no living member stays, every present member goes (the
// original group move, no fork). 0 when no living member goes.
constexpr std::uint8_t exit_leavers(std::uint8_t present, std::uint8_t living, std::uint8_t going) {
    living &= present;
    const auto leaving = std::uint8_t(going & living);
    if (!leaving) return 0;
    if ((living & ~leaving) == 0) return present;
    return leaving;
}

} // namespace dw4::engine
