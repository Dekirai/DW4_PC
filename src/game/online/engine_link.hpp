#pragma once

// The engine as the DW4R2 client drives it: the calls of docs/SEAMLESS.md 7
// that the client relies on. The game implements it on dw4::engine::Engine
// (engine_adapter.hpp; the membership API of section 7 is detected at compile
// time, so the client builds before the membership lane is joined), the tests
// on a deterministic fake engine.
//
// Live steps run in the host loop (pictures, audio, achievements); the client
// only runs replay steps itself (catch-up). Every call that changes the
// simulation is made between steps, at a step every member makes it.
#include "dw4/engine/online_story.hpp"
#include "dw4/pad.hpp"
#include "wire.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dw4::game::online {

// Engine::take_split (7.3): a fork decided inside the simulation in the
// step just run.
struct SplitInfo {
    std::uint8_t leavers{}; // the side that leaves the instance id (the relay's leaver mask, the child)
    bool in_stage{};
    bool local_leaves{};    // this copy is on that side
};

// Engine::take_heading (5.2): an exit this copy takes, one to two seconds
// before its barrier (HEADING lets the server hold a co-arrival).
struct HeadingInfo {
    std::string stage;
    int point{};
};

// What a member shows the others (PRESENCE, name tags; PRESENCE2, the ghost
// figures of the hub stages). Read from the engine, never written to it.
struct PresenceFacts {
    bool in_stage = false; // false: title screens, loading
    float x{}, y{}, z{}, yaw{};
    int species{}, level{}, hp{}, max_hp{}, mp{}, max_mp{};
    bool defeated = false, hidden = false;
    // PRESENCE2: `figure_hidden` is `hidden` without the windows (a status
    // or shop window only makes the member `busy`: its figure stands where
    // it is), or the player model not shown (before its entry); the motion
    // clip its figure plays and whether that loops; the weapon package it
    // holds (POWERSOCKETNAME[0], empty: none).
    bool figure_hidden = false, busy = false;
    std::string clip;
    bool clip_loops = true;
    std::string weapon;
};

class EngineLink {
public:
    virtual ~EngineLink() = default;

    // ---- facts after a step
    virtual std::string stage() const = 0;
    // Engine::pending_stage (7.3): the stage a NEXTSTAGE of the last step
    // waits to load; nothing while none is pending.
    virtual std::optional<std::string> pending_stage() const = 0;
    // The entrance point of the pending stage (PLAYERPOINT's generate point).
    virtual int pending_point() const = 0;
    // False on an engine without the membership API: the client then never
    // parks at a barrier and reports each stage load afterwards.
    virtual bool supports_barriers() const = 0;
    // Counts the stage loads (StageSnapshot::serial): it changes in the load
    // step.
    virtual std::uint64_t load_serial() const = 0;
    // The running visit's load snapshot (raw: the leader's real values;
    // canonical: presentation-only fields zeroed, 6.8).
    virtual std::vector<std::uint8_t> snapshot_bytes(bool canonical) const = 0;
    virtual std::uint64_t net_fingerprint() = 0;
    virtual bool membership_safe() const = 0;
    virtual bool joinable(wire::NotJoinable& reason) const = 0;
    virtual bool boss_lock() const = 0;
    virtual std::uint64_t session_context_hash() const = 0;
    virtual std::uint8_t alive_mask() const = 0;
    // The instance's present slots as the engine has them (OnlineRules::present).
    virtual std::uint8_t present_mask() const = 0;
    virtual std::vector<std::uint8_t> carry(int slot) const = 0;
    virtual std::optional<SplitInfo> take_split() = 0;
    virtual std::optional<HeadingInfo> take_heading() { return std::nullopt; }
    virtual PresenceFacts presence(int slot) const = 0;
    // The engine's story values (FLAG, FLAGFOLDER, COUNTER; SEAMLESS.md 8.3):
    // the pending set against the confirmed room story comes from them.
    virtual engine::StoryValues story_values() const { return {}; }

    // ---- changes between steps
    // A replay step (catch-up): the step, the picture's traversal, the music
    // position (the sound rendered and dropped); no port events.
    virtual void replay_step(const std::array<PadSample, 4>& pads) = 0;
    // A fresh engine from snapshot bytes (the running one is dropped first;
    // never two engines at once). False on damaged bytes.
    virtual bool restore(std::span<const std::uint8_t> snapshot) = 0;
    virtual void set_replay(bool on) = 0;
    // The online rules for a member in `slot` (session start, after every
    // restore): rules on, console reset off, cheats from the room.
    virtual void start_online(int own_slot, std::uint8_t present) = 0;
    virtual void set_split_identity(int slot) = 0; // -1: an observer keeps the side that keeps the instance id
    virtual void set_fork_sides(std::span<const wire::Fork> forks) = 0;
    virtual void set_barrier(const wire::BarrierOk& decision) = 0;
    // False when the carry could not be decoded (damaged bytes from the
    // network): the join is dropped, alike on every member (the same bytes).
    virtual bool join_player(int slot, std::span<const std::uint8_t> carry, int entrance_point) = 0;
    virtual void remove_player(int slot) = 0;
    // A LEAVE reached its tick but waits for a membership-safe step (6.5):
    // the slot plays the neutral, connected pad and its prompts and windows
    // take the leader's pad meanwhile.
    virtual void mark_leaving(int slot) = 0;
    virtual void intent(int slot, wire::IntentKind kind, int argument) = 0;
    // Round I1a (docs/SEAMLESS.md 19.6.5, 19.6.10): HOME for a slot mask (the
    // home departure; a fork of ForkKind::home for part of the present
    // slots) and the local way home of a solo engine whose instance is gone.
    // The defaults: an engine without them (test fakes) does nothing.
    virtual void home(std::uint8_t mask) { (void)mask; }
    virtual bool depart_home() { return false; }
    // Presentation only: whose camera the picture uses (a defeated member
    // watches the nearest living member).
    virtual void set_view_slot(int slot) = 0;

    // ---- Round J: the tick state (docs/SEAMLESS.md 6.13). The defaults are
    // an engine without one (test fakes): never state-safe, no checkpoints.
    virtual bool state_safe() const { return false; }
    virtual bool load_ended() const { return false; } // the visit reached its load end
    virtual std::vector<std::uint8_t> save_tick_state(bool canonical) { (void)canonical; return {}; }
    virtual std::uint64_t tick_state_hash() { return 0; }
    // "" applied; else the section that failed ("header", "topology", ...).
    virtual std::string apply_tick_state(std::span<const std::uint8_t> bytes) {
        (void)bytes;
        return "unsupported";
    }
};

// Hub and UI stages (2): stagML02, stagML11-14 and every stagIS*. Solo
// instances, never joined, not logged.
bool is_hub_stage(std::string_view stage);
// Round I1a (19.5.4): the stage names a script chunk mentions (the Lua
// string constants "stagML11", "stag0102", "dng101b1", ...): true when it
// names at least one and every one is a hub stage, i.e. an exit mat's
// funcStageChange leads home whichever of its calls runs.
bool chunk_leads_home(std::string_view chunk);

} // namespace dw4::game::online
