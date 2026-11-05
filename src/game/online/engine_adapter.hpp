#pragma once

// EngineLink on the game's dw4::engine::Engine.
//
// The membership API of docs/SEAMLESS.md 7 is built in parallel (round 52,
// membership lane) in include/dw4/engine/online.hpp. When that header exists
// (__has_include), the adapter calls it with the section 7 names:
//   Engine::pending_stage(), membership_safe(), joinable(), session_context_hash(),
//   net_fingerprint(), snapshot_bytes(SnapshotForm::raw / canonical),
//   carry_player(slot) + encode_carry / decode_carry, online_join_player(slot, carry, point),
//   online_remove_player(slot), online_intent(slot, IntentKind, argument),
//   take_split(), set_barrier(BarrierResult), set_online(OnlineRules), online(),
//   set_replay(bool), set_view_slot(int), set_fork_sides(span<const ForkSide>),
//   set_split_identity(int).
// Without it the adapter runs today's engine: no barriers (each stage load is
// reported afterwards, the member stays solo), no joins, the plain
// fingerprint with the system generator's state, the raw snapshot as the
// canonical form, and the console reset switched off at every online engine
// start (Engine::set_reset_enabled(false)).
#include "engine_link.hpp"
#include "room_settings.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/online_membership.hpp"

#include <array>
#include <functional>
#include <optional>

namespace dw4::game::online {

struct EngineHooks {
    // The engine the game runs now (the same object after a re-emplace).
    std::function<engine::Engine&()> engine;
    // Drops the running engine and constructs a fresh one with the session's
    // mods and card store (never two at once: the system generator is static).
    std::function<void()> fresh_engine;
    // Presentation: whether the name tags hide (loads, scenes, menus).
    std::function<bool()> hidden;
    // Presentation (PRESENCE2, the ghost figures): the same without the
    // status and shop windows, and whether one of those is open (the member
    // is busy: its figure stays, its tag hides). Unset: `hidden`, not busy.
    std::function<bool()> figure_hidden;
    std::function<bool()> busy;
};

class GameEngineLink final : public EngineLink {
public:
    GameEngineLink(EngineHooks hooks, RoomSettings settings) : hooks_(std::move(hooks)), settings_(settings) {}

    std::string stage() const override;
    std::optional<std::string> pending_stage() const override;
    int pending_point() const override;
    bool supports_barriers() const override;
    std::uint64_t load_serial() const override;
    std::vector<std::uint8_t> snapshot_bytes(bool canonical) const override;
    std::uint64_t net_fingerprint() override;
    bool state_safe() const override;
    bool load_ended() const override;
    std::vector<std::uint8_t> save_tick_state(bool canonical) override;
    std::uint64_t tick_state_hash() override;
    std::string apply_tick_state(std::span<const std::uint8_t> bytes) override;
    bool membership_safe() const override;
    bool joinable(wire::NotJoinable& reason) const override;
    bool boss_lock() const override;
    std::uint64_t session_context_hash() const override;
    std::uint8_t alive_mask() const override;
    std::uint8_t present_mask() const override;
    std::vector<std::uint8_t> carry(int slot) const override;
    std::optional<SplitInfo> take_split() override;
    std::optional<HeadingInfo> take_heading() override;
    PresenceFacts presence(int slot) const override;
    engine::StoryValues story_values() const override;

    void replay_step(const std::array<PadSample, 4>& pads) override;
    bool restore(std::span<const std::uint8_t> snapshot) override;
    void set_replay(bool on) override;
    void start_online(int own_slot, std::uint8_t present) override;
    void set_split_identity(int slot) override;
    void set_fork_sides(std::span<const wire::Fork> forks) override;
    void set_barrier(const wire::BarrierOk& decision) override;
    bool join_player(int slot, std::span<const std::uint8_t> carry, int entrance_point) override;
    void remove_player(int slot) override;
    void mark_leaving(int slot) override;
    void intent(int slot, wire::IntentKind kind, int argument) override;
    void set_view_slot(int slot) override;

    const RoomSettings& settings() const { return settings_; }
    // After every live step: once the title starts a game (GameSystem::
    // game_started, set by TITLE2 GAMESTART right after the title wrote its
    // own camera and light mode and the character select its difficulty),
    // the room's difficulty, camera mode and light mode replace them and the
    // room story (set_room_story) replaces the character's FLAG, FLAGFOLDER
    // and COUNTER, before gameStart.lua runs (a later step) and before the
    // first stage loads, so every instance of the room uses the same
    // difficulty and camera data (3.1, 8.5). The title is the member's own
    // solo instance, so each machine does it in its own line.
    // The trigger is the title's GAMESTART (Engine::title_game_start), not
    // game_started's rising edge: that flag stays set after "Continue? No"
    // took the player back to the title, and the title's option menu sets it
    // without a game start, so the next Game Start of the session got neither
    // the room story nor the room's modes (round 56 review).
    // Round G-core (docs/SEAMLESS.md 8.8): never without a confirmed story.
    // Without one the Game Start stays pending (game_start() stays true) and
    // nothing is applied: the host holds the next live step until the story
    // arrives and calls this again. A character call of player 1 outside the
    // title's mode (Engine::common_rebuilt, the call shop's path) is treated
    // as a Game Start. Returns true when the story and modes were applied.
    bool after_live_step();
    // A Game Start (or a rebuilt common story) is waiting for after_live_step
    // (set by the step's GAMESTART): the host hands the confirmed room story
    // over first.
    bool game_start() const;
    // The room story for game_started (SEAMLESS.md 8.5): the confirmed room
    // story the session has when the game starts. Without one the
    // character's own story stays until the first barrier's merge (the room
    // owner's own story is the room story anyway).
    void set_room_story(std::optional<engine::RoomStory> story) { room_story_ = std::move(story); }
    const std::optional<engine::RoomStory>& room_story() const { return room_story_; }
    // The online character's record of `slot` for an automatic save
    // (SEAMLESS.md 9): GameSystem::make_record from the state the member
    // carries (Engine::carry_player) on a copy of GameSystem, since
    // make_record changes state (store_species) and the live engine must stay
    // as every member has it. None before a game started and for a slot
    // without a character or with a defeated one (its last save stands).
    std::optional<std::array<std::uint8_t, 3316>> character_record(int slot) const;
    // The game started (GameSystem::game_started).
    bool game_started() const;
    bool replaying() const { return replay_; }
    // Seconds of output the last replay rendered for the music position.
    std::uint64_t replay_samples() const { return replay_samples_; }

private:
    engine::Engine& e() const { return hooks_.engine(); }
    EngineHooks hooks_;
    RoomSettings settings_;
    std::optional<engine::RoomStory> room_story_;
    bool replay_ = false;
    int own_slot_ = 0;
    std::uint64_t replay_samples_ = 0;
};

// The replay rate calibration (docs/SEAMLESS.md 6.6): a hidden replay of a
// four-player field at the room join, before the online engine starts (one
// engine at a time). `stage` direct-started with four joined players, a
// deterministic walk-and-attack pattern on every pad, timed after the load
// for `max_steps` steps or `max_seconds`: steps per second of the whole
// simulation step plus the picture traversal and the music position, the
// work of a catch-up step.
struct Calibration {
    double steps_per_second = 0;
    int steps = 0;
    double seconds = 0;
    std::string stage;
    bool ok = false;
};
// `music` false leaves the music position out (to measure what it costs).
Calibration calibrate_replay_rate(engine::Engine& engine, const std::string& stage, int max_steps, double max_seconds, bool music = true);

} // namespace dw4::game::online
