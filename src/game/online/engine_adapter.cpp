#include "engine_adapter.hpp"

#include "dw4/engine/managers.hpp"

#if __has_include("dw4/engine/online.hpp")
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_membership.hpp"
#define DW4_ONLINE_ENGINE_API 1
#endif

#include "dw4/save_data.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace dw4::game::online {

namespace {
std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0x100000001b3ull;
}

// The output samples of the step that reached `frame` (one NTSC field at
// 48 kHz, game_main's step_samples).
int step_samples(std::uint64_t frame) {
    constexpr std::uint64_t rate = engine::SoundManager::kOutputRate;
    const auto upto = [](std::uint64_t f) { return f * rate * 1001 / 60000; };
    return int(upto(frame) - upto(frame ? frame - 1 : 0));
}

void advance_music(engine::Engine& engine, std::uint64_t& samples) {
    if (auto* sound = engine.get<engine::SoundManager>()) {
        static thread_local std::vector<std::int16_t> scratch;
        const int n = step_samples(engine.frame());
        scratch.resize(std::size_t(n) * 2);
        if (!engine.rebooting()) sound->render(scratch.data(), std::size_t(n));
        samples += std::uint64_t(n);
    }
}
} // namespace

std::string GameEngineLink::stage() const { return e().stage_name(); }

std::optional<std::string> GameEngineLink::pending_stage() const {
#ifdef DW4_ONLINE_ENGINE_API
    if (const auto p = e().pending_stage()) return std::string(*p);
#endif
    return std::nullopt;
}

int GameEngineLink::pending_point() const {
    // The player point the stage change leads to: PLAYERPOINT's point number,
    // which the exit's SETGENERATEPOS sets before its NEXTSTAGE (and a zone
    // remap of the randomizer with it). The stage snapshot holds the point the
    // current stage was entered at: at a barrier (the next stage pending) that
    // was the departing stage's, so a joiner who came through stag0401 point 3
    // was put in at point 0, across the field from its instance.
    if (const auto* points = e().get<engine::PlayerPointManager>()) return int(points->point_no());
    const auto* snapshot = e().stage_snapshot();
    return snapshot ? int(snapshot->point_no) : 0;
}

bool GameEngineLink::supports_barriers() const {
#ifdef DW4_ONLINE_ENGINE_API
    return true;
#else
    return false;
#endif
}

std::uint64_t GameEngineLink::load_serial() const {
    const auto* snapshot = e().stage_snapshot();
    return snapshot ? snapshot->serial : 0;
}

std::vector<std::uint8_t> GameEngineLink::snapshot_bytes(bool canonical) const {
#ifdef DW4_ONLINE_ENGINE_API
    return e().snapshot_bytes(canonical ? engine::SnapshotForm::canonical : engine::SnapshotForm::raw);
#else
    (void)canonical; // no canonical form yet: the raw bytes (frames_read included)
    const auto* snapshot = e().stage_snapshot();
    return snapshot ? engine::encode_snapshot(*snapshot) : std::vector<std::uint8_t>{};
#endif
}

std::uint64_t GameEngineLink::net_fingerprint() {
#ifdef DW4_ONLINE_ENGINE_API
    return e().net_fingerprint();
#else
    std::ostringstream random;
    random << engine::fight::generator();
    return mix(e().fingerprint(), std::hash<std::string>{}(random.str()));
#endif
}

bool GameEngineLink::membership_safe() const {
#ifdef DW4_ONLINE_ENGINE_API
    return e().membership_safe();
#else
    return e().presenting() && !e().loading();
#endif
}

bool GameEngineLink::joinable(wire::NotJoinable& reason) const {
    if (is_hub_stage(e().stage_name())) {
        reason = wire::NotJoinable::hub;
        return false;
    }
    if (e().loading() || !e().presenting()) {
        reason = wire::NotJoinable::loading;
        return false;
    }
    // (A member's own talk or map recorder holds up no join, SEAMLESS.md 5.3.)
    if (e().demo().running_unscoped() > 0) {
        reason = wire::NotJoinable::scene;
        return false;
    }
#ifdef DW4_ONLINE_ENGINE_API
    if (e().online().boss_lock) {
        reason = wire::NotJoinable::boss;
        return false;
    }
    if (!e().joinable()) {
        reason = wire::NotJoinable::menu;
        return false;
    }
    return true;
#else
    reason = wire::NotJoinable::loading; // no joins without the membership API
    return false;
#endif
}

bool GameEngineLink::boss_lock() const {
#ifdef DW4_ONLINE_ENGINE_API
    return e().online().boss_lock;
#else
    return false;
#endif
}

std::uint64_t GameEngineLink::session_context_hash() const {
#ifdef DW4_ONLINE_ENGINE_API
    return e().session_context_hash();
#else
    return 0;
#endif
}

std::uint8_t GameEngineLink::alive_mask() const {
    std::uint8_t mask = 0;
    if (const auto* sys = e().get<engine::GameSystem>())
        for (int s = 0; s < 4; ++s)
            if (sys->players[std::size_t(s)].joined && sys->players[std::size_t(s)].hp > 0) mask |= std::uint8_t(1u << s);
    return mask;
}

std::uint8_t GameEngineLink::present_mask() const {
#ifdef DW4_ONLINE_ENGINE_API
    if (e().online().enabled) return e().online().present;
#endif
    return std::uint8_t(1u << (own_slot_ & 3));
}

std::vector<std::uint8_t> GameEngineLink::carry(int slot) const {
#ifdef DW4_ONLINE_ENGINE_API
    return engine::encode_carry(e().carry_player(slot));
#else
    (void)slot;
    return {};
#endif
}

std::optional<SplitInfo> GameEngineLink::take_split() {
#ifdef DW4_ONLINE_ENGINE_API
    // The relay names the side that leaves the instance id (exit, give up:
    // the leavers; in-stage: the others), and whether this copy is on it.
    if (const auto split = e().take_split())
        return SplitInfo{split->child, split->in_stage, split->child == split->leavers ? split->local_leaves : !split->local_leaves};
#endif
    return std::nullopt;
}

std::optional<HeadingInfo> GameEngineLink::take_heading() {
#ifdef DW4_ONLINE_ENGINE_API
    if (auto heading = e().take_heading()) return HeadingInfo{std::move(heading->stage), heading->point};
#endif
    return std::nullopt;
}

PresenceFacts GameEngineLink::presence(int slot) const {
    PresenceFacts f;
    auto& engine = e();
    const auto* sys = engine.get<engine::GameSystem>();
    const auto* models = engine.get<engine::ModelCtrlManager>();
    if (!sys || !models || slot < 0 || slot > 3) return f;
    const auto& player = sys->players[std::size_t(slot)];
    const auto* ram = models->player(slot);
    if (!player.joined || !ram || !ram->node || engine.stage_name().rfind("stagis", 0) == 0) return f; // title screens: no tag
    f.in_stage = engine.presenting() && !engine.loading();
    f.x = ram->node->matrix(3, 0);
    f.y = ram->node->matrix(3, 1);
    f.z = ram->node->matrix(3, 2);
    f.yaw = std::atan2(ram->node->matrix(2, 0), ram->node->matrix(2, 2));
    f.species = player.stats.ready ? player.stats.species : 0;
    f.level = player.stats.level;
    f.hp = player.hp;
    f.max_hp = player.max_hp;
    f.mp = player.mp;
    f.max_mp = player.max_mp;
    f.defeated = player.hp <= 0 || !ram->generated;
    f.hidden = hooks_.hidden ? hooks_.hidden() : false;
    // PRESENCE2 (the ghost figures): read only, nothing here changes the
    // engine. The figure hides where the tag hides, except for a status or
    // shop window (busy), and while the action switch does not show the
    // model (SQ_HIDE before the entry, a FADESET HIDE).
    f.busy = hooks_.busy ? hooks_.busy() : false;
    f.figure_hidden = hooks_.figure_hidden ? hooks_.figure_hidden() : f.hidden;
    if (const auto* actions = engine.get<engine::ActionManager>()) {
        if (!actions->drawn(ram->node)) f.figure_hidden = true;
        if (const auto* action = actions->find(player.digimon, slot)) {
            f.clip = action->motion.clip;
            f.clip_loops = !action->database || action->database->looping(f.clip);
        }
    }
    f.weapon = player.weapons[0];
    return f;
}

engine::StoryValues GameEngineLink::story_values() const {
    auto& engine = e();
    engine::StoryValues v;
    v.flags = engine.flags();
    v.counters = engine.counters();
    if (const auto* sys = engine.get<engine::GameSystem>()) v.folder.insert(sys->flag_folder.begin(), sys->flag_folder.end());
    return v;
}

void GameEngineLink::replay_step(const std::array<PadSample, 4>& pads) {
    auto& engine = e();
    engine.step(pads);
    // The picture's traversal (it advances simulation clocks) and the music
    // position (the sound rendered and dropped), as a save state replay.
    if (!engine.held()) engine.traverse_scene();
    advance_music(engine, replay_samples_);
}

bool GameEngineLink::restore(std::span<const std::uint8_t> bytes) {
    engine::StageSnapshot snapshot;
    try {
        snapshot = engine::decode_snapshot(bytes);
    } catch (const std::exception&) {
        return false;
    }
    hooks_.fresh_engine();
    auto& engine = e();
    engine.restore(snapshot);
    // The restored load step's picture traversal (it advances simulation
    // clocks), as the members' live load step had it and as every replayed
    // step has it (replay_step; a save state's load_step_drawn).
    if (!engine.held()) engine.traverse_scene();
    start_online(own_slot_, 0);
    return true;
}

void GameEngineLink::set_replay(bool on) {
    replay_ = on;
    if (on) replay_samples_ = 0;
#ifdef DW4_ONLINE_ENGINE_API
    e().set_replay(on);
#endif
    if (on) e().set_port_events(false); // no achievement or story credit from a replay (7.5)
}

void GameEngineLink::start_online(int own_slot, std::uint8_t present) {
    own_slot_ = own_slot;
    auto& engine = e();
    // The room's cheats after every construction and restore (6.12): the
    // same on every machine, the settings hash makes sure of it.
    apply_room_cheats(settings_, engine.cheats());
#ifdef DW4_ONLINE_ENGINE_API
    // The engine's online rules keep the console reset off (7.4); a restore
    // keeps the snapshot's members (present 0).
    auto rules = engine.online();
    rules.enabled = true;
    if (present) rules.present = present;
    engine.set_online(rules);
    engine.set_split_identity(own_slot);
    engine.set_view_slot(own_slot);
#else
    // Without the online rules: the console reset off at the start of the
    // member's own engine (present != 0), never after a restore, where it is
    // the snapshot's state (a save request turns it on again, 7.4).
    if (present) engine.set_reset_enabled(false);
#endif
}

void GameEngineLink::set_split_identity(int slot) {
    if (slot >= 0 && slot < 4) own_slot_ = slot; // (a joiner's slot, after its JOIN)
#ifdef DW4_ONLINE_ENGINE_API
    e().set_split_identity(slot);
#else
    (void)slot;
#endif
}

void GameEngineLink::set_fork_sides(std::span<const wire::Fork> forks) {
#ifdef DW4_ONLINE_ENGINE_API
    // A fork at tick t happened in the step after the load step's frame + t
    // (tick 0 drives the first step after the load step, 6.7), and
    // ForkSide::frame is the engine's frame after that step.
    const auto* snapshot = e().stage_snapshot();
    const std::uint64_t load_frame = snapshot ? snapshot->frame : 0;
    std::vector<engine::ForkSide> sides;
    for (const auto& f : forks) sides.push_back(engine::ForkSide{load_frame + f.tick + 1, f.leavers, f.side != 0});
    e().set_fork_sides(sides);
#else
    (void)forks;
#endif
}

void GameEngineLink::set_barrier(const wire::BarrierOk& ok) {
#ifdef DW4_ONLINE_ENGINE_API
    engine::BarrierResult result;
    switch (ok.decision) {
    case wire::Decision::new_instance: result.decision = engine::Decision::new_instance; break;
    case wire::Decision::join: result.decision = engine::Decision::join; break;
    case wire::Decision::dissolve: result.decision = engine::Decision::dissolve; break;
    case wire::Decision::co_arrive: result.decision = engine::Decision::co_arrive; break;
    }
    for (int s = 0; s < 4; ++s) result.slot_map[std::size_t(s)] = ok.slot_map[std::size_t(s)];
    for (const auto& c : ok.co_arrivals) {
        // A co-arrival's carry is another member's BARRIER blob, passed on by
        // the relay unchecked: damaged bytes drop that co-arrival (on every
        // member of the base alike) instead of ending the game.
        try {
            result.co_arrivals.emplace_back(int(c.slot), engine::decode_carry(c.carry));
        } catch (const std::exception&) {
        }
    }
    // The engine's own slot: NEW and the others name the new slot; DISSOLVE
    // keeps the player of this machine's OLD slot (the relay names the new
    // slot 0 in own_slot, the old one is the slot map's source of 0).
    result.own_slot = ok.own_slot;
    if (ok.decision == wire::Decision::dissolve) {
        result.own_slot = own_slot_;
        for (int s = 0; s < 4; ++s)
            if (ok.slot_map[std::size_t(s)] == 0) {
                result.own_slot = s;
                break;
            }
    }
    // The room story and the dropped set (SEAMLESS.md 8.5, 8.6): every member
    // of the group gets the same bytes, so a damaged blob fails alike on every
    // member (no merge this time) instead of ending the game.
    if (!ok.story.empty()) {
        try {
            result.story = engine::story::decode_story(ok.story, ok.story_revision);
            result.dropped = engine::story::decode_keys(ok.dropped);
        } catch (const std::exception&) {
            result.story.reset();
            result.dropped = {};
        }
    }
    result.rules = e().online();
    result.rules.enabled = true;
    result.rules.present = ok.present();
    e().set_barrier(result);
    own_slot_ = ok.own_slot;
#else
    own_slot_ = ok.own_slot;
#endif
}

bool GameEngineLink::join_player(int slot, std::span<const std::uint8_t> carry, int point) {
#ifdef DW4_ONLINE_ENGINE_API
    // The carry is the joiner's READY blob, passed on by the relay unchecked.
    engine::PlayerCarry decoded;
    try {
        decoded = engine::decode_carry(carry);
    } catch (const std::exception&) {
        return false;
    }
    e().online_join_player(slot, decoded, point);
    return true;
#else
    (void)slot, (void)carry, (void)point;
    return false;
#endif
}

void GameEngineLink::mark_leaving(int slot) {
#ifdef DW4_ONLINE_ENGINE_API
    e().online_mark_leaving(slot);
#else
    (void)slot;
#endif
}

void GameEngineLink::remove_player(int slot) {
#ifdef DW4_ONLINE_ENGINE_API
    e().online_remove_player(slot);
#else
    (void)slot;
#endif
}

void GameEngineLink::intent(int slot, wire::IntentKind kind, int argument) {
#ifdef DW4_ONLINE_ENGINE_API
    e().online_intent(slot, kind == wire::IntentKind::give_up ? engine::IntentKind::give_up : engine::IntentKind::go_without, argument);
#else
    (void)slot, (void)kind, (void)argument;
#endif
}

void GameEngineLink::set_view_slot(int slot) {
#ifdef DW4_ONLINE_ENGINE_API
    e().set_view_slot(slot);
#else
    (void)slot;
#endif
}

bool GameEngineLink::after_live_step() {
    auto& engine = e();
    if (!engine.title_game_start && !engine.common_rebuilt) return false;
    // (No Game Start without the confirmed room story: the host holds, 8.8.)
    if (!room_story_) return false;
    engine.title_game_start = false;
    engine.common_rebuilt = false;
    auto* sys = engine.get<engine::GameSystem>();
    if (!sys) return false;
    // (The character select wrote DIFFICULTY before GAMESTART, 0x803687D4;
    // gameStart.lua runs a few steps later, once the card worker is gone.)
    engine.apply_room_story(*room_story_);
    sys->difficulty = std::clamp(settings_.difficulty, 0, 2);
    sys->camera_mode = settings_.camera_mode;
    sys->light_mode = settings_.light_mode;
    return true;
}

bool GameEngineLink::game_start() const { return e().title_game_start || e().common_rebuilt; }

bool GameEngineLink::game_started() const {
    const auto* sys = e().get<engine::GameSystem>();
    return sys && sys->game_started;
}

std::optional<std::array<std::uint8_t, 3316>> GameEngineLink::character_record(int slot) const {
    auto& engine = e();
    const auto* sys = engine.get<engine::GameSystem>();
    if (!sys || !sys->game_started || slot < 0 || slot > 3) return std::nullopt;
    const auto& player = sys->players[std::size_t(slot)];
    if (!player.joined || !player.stats.ready || player.unique == 0 || player.digimon.empty() || player.hp <= 0) return std::nullopt;
    // A copy: make_record runs CURRENT2BASE (store_species) first, which the
    // live engine of every member must not see.
    engine::GameSystem copy = *sys;
#ifdef DW4_ONLINE_ENGINE_API
    copy.players[std::size_t(slot)] = engine.carry_player(slot).slot;
#endif
    const auto record = save::encode(copy.make_record(engine, slot));
    std::array<std::uint8_t, 3316> out{};
    std::copy(record.begin(), record.end(), out.begin());
    return out;
}

Calibration calibrate_replay_rate(engine::Engine& engine, const std::string& stage, int max_steps, double max_seconds, bool music) {
    Calibration c;
    c.stage = stage;
    engine.cheats().fast_loading = true;
    if (auto* sys = engine.get<engine::GameSystem>()) {
        sys->direct_start = true;
        for (auto& p : sys->players) p.joined = true;
    }
    engine.request_stage(stage);
    std::array<PadSample, 4> pads{};
    for (auto& p : pads) p.connected = true;
    std::uint64_t samples = 0;
    // The load (bounded), then a few seconds in, timed.
    for (int i = 0; i < 4000 && !(engine.stage_snapshot() && engine.stage_active() && engine.presenting() && !engine.loading()); ++i) {
        engine.step(pads);
        if (!engine.held()) engine.traverse_scene();
    }
    if (!engine.stage_snapshot() || engine.loading()) return c;
    const auto start = std::chrono::steady_clock::now();
    int steps = 0;
    for (int i = 0; i < max_steps; ++i) {
        if ((i & 31) == 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= max_seconds) break;
        ++steps;
        // Every player walks in a slow circle and attacks now and then.
        for (int s = 0; s < 4; ++s) {
            const float angle = float(i + 90 * s) * 0.02f;
            pads[std::size_t(s)].left = {std::round(std::sin(angle) * 60.0f) / 74.0f, std::round(std::cos(angle) * 60.0f) / 74.0f};
            pads[std::size_t(s)].buttons = (i + 17 * s) % 45 < 3 ? std::uint32_t(dw4::pad_button::a) : 0u;
        }
        engine.step(pads);
        if (!engine.held()) engine.traverse_scene();
        if (music) advance_music(engine, samples);
    }
    c.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    c.steps = steps;
    c.steps_per_second = c.seconds > 0 ? double(steps) / c.seconds : 0;
    c.ok = c.steps_per_second > 0;
    return c;
}

} // namespace dw4::game::online
