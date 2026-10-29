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

#include <functional>

namespace dw4::game::online {

struct EngineHooks {
    // The engine the game runs now (the same object after a re-emplace).
    std::function<engine::Engine&()> engine;
    // Drops the running engine and constructs a fresh one with the session's
    // mods and card store (never two at once: the system generator is static).
    std::function<void()> fresh_engine;
    // Presentation: whether the name tags hide (loads, scenes, menus).
    std::function<bool()> hidden;
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
    bool membership_safe() const override;
    bool joinable(wire::NotJoinable& reason) const override;
    bool boss_lock() const override;
    std::uint64_t session_context_hash() const override;
    std::uint8_t alive_mask() const override;
    std::vector<std::uint8_t> carry(int slot) const override;
    std::optional<SplitInfo> take_split() override;
    PresenceFacts presence(int slot) const override;

    void replay_step(const std::array<PadSample, 4>& pads) override;
    bool restore(std::span<const std::uint8_t> snapshot) override;
    void set_replay(bool on) override;
    void start_online(int own_slot, std::uint8_t present) override;
    void set_split_identity(int slot) override;
    void set_fork_sides(std::span<const wire::Fork> forks) override;
    void set_barrier(const wire::BarrierOk& decision) override;
    void join_player(int slot, std::span<const std::uint8_t> carry, int entrance_point) override;
    void remove_player(int slot) override;
    void intent(int slot, wire::IntentKind kind, int argument) override;
    void set_view_slot(int slot) override;

    const RoomSettings& settings() const { return settings_; }
    bool replaying() const { return replay_; }
    // Seconds of output the last replay rendered for the music position.
    std::uint64_t replay_samples() const { return replay_samples_; }

private:
    engine::Engine& e() const { return hooks_.engine(); }
    EngineHooks hooks_;
    RoomSettings settings_;
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
Calibration calibrate_replay_rate(engine::Engine& engine, const std::string& stage, int max_steps, double max_seconds);

} // namespace dw4::game::online
