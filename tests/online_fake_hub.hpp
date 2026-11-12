#pragma once

// Test doubles for the DW4R2 client: a deterministic fake engine (its state
// is a hash of every pad it executed, stage loads, joins, leaves and forks)
// and an in-process fake hub that speaks the relay's layout (docs/SEAMLESS.md
// 6.3 and the appendix "DW4R2 wire layout") over memory pipes on a simulated
// clock: WELCOME with a solo hub instance, solo-led and sealed instances,
// server-paced sealing at 59.94 Hz with immediate fill, barrier decisions
// (DISSOLVE for hubs, NEW, co-arrival, JOIN by catch-up), JOIN_REQ / JOIN_AT,
// state delivery with LIVE ticks for observers, SPLIT with REASSIGN and
// LEAD_FROM, RESUME, PING with a suggested delay, ROOM lag notices, NETFP
// collection, the room story (STORY merged with the C++ story rules, STORY_ACK
// after WELCOME and on every revision, the story and the dropped set in every
// BARRIER_OK; owner away with the drop policy refuses and records). The real
// hub is server/DW4Online/src/DW4Online.Relay/Hub.
#include "dw4/engine/online.hpp"
#include "online/engine_link.hpp"
#include "online/pacing.hpp"
#include "online/session.hpp"
#include "online/transport.hpp"
#include "online/wire.hpp"

#include <bit>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace fake {
using namespace dw4::game::online;
namespace w = dw4::game::online::wire;

inline std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0x100000001b3ull;
}

// Buttons the fake engine reacts to (from the lowest present slot for Z, any
// slot for X): Z takes the stage's exit (NEXTSTAGE to the next stage of the
// route), X makes that slot fork away (exit-type fork).
inline constexpr std::uint32_t kExitButton = dw4::pad_button::z;
inline constexpr std::uint32_t kForkButton = dw4::pad_button::x;
// Y opens a prompt owned by that slot (an exit prompt without a time limit:
// no membership step while it is open); B on the owner's pad closes it. A
// leaving owner's prompt closes by itself as No in the next step (6.5,
// Engine::close_leaving_prompt): nobody else's pad answers it.
inline constexpr std::uint32_t kPromptButton = dw4::pad_button::y;
inline constexpr std::uint32_t kAnswerButton = dw4::pad_button::b;

class Engine final : public EngineLink {
public:
    std::vector<std::string> route{"stagis10", "stagml11", "stag0101", "stag0102", "dng101b1"};
    std::string stage_ = "stagis10";
    std::uint64_t serial_ = 1, state_ = 7, frame_ = 0;
    std::uint8_t present_ = 1;
    std::optional<std::string> pending_;
    int transition_left_ = 0;
    int transition_steps = 3; // steps from NEXTSTAGE to the load step (a real stage load: about 750)
    std::optional<w::BarrierOk> barrier_;
    std::optional<SplitInfo> split_;
    std::vector<std::uint8_t> load_snapshot_;
    int identity_ = 0;
    int prompt_owner_ = -1;
    std::uint8_t leaving_ = 0;
    bool replay_ = false;
    dw4::engine::StoryValues story; // FLAG / FLAGFOLDER / COUNTER (tests set values as a script would)
    int story_merges = 0;
    int story_replaces = 0; // round I1a: barriers whose story replaced the subset
    int restores = 0, replayed = 0;
    std::uint64_t audio_steps = 0;

    std::vector<std::uint8_t> serialize() const {
        w::Writer wr;
        wr.str8(stage_);
        wr.u64(serial_);
        wr.u64(state_);
        wr.u64(frame_);
        wr.u8(present_);
        return wr.out;
    }
    void take_load_snapshot() { load_snapshot_ = serialize(); }

    // Round I1b: the lobby gate's destination menu (in stagml11 while set): A
    // on it departs to gate_target (the press is a rising edge of the menu's
    // reader, the own slot), B closes it. pending_departure_choice names it.
    bool gate_menu = false;
    std::string gate_target = "stag0101";
    int gate_point = 0;
    int gate_back_point = 1; // (the lobby's point next to the gate, as the engine's census names it; -1 none)
    int gate_confirms = 0, gate_cancels = 0;
    std::uint32_t gate_previous = 0;
    std::optional<dw4::engine::DepartureChoice> pending_departure_choice() const override {
        if (!gate_menu || stage_ != "stagml11" || pending_) return std::nullopt;
        dw4::engine::DepartureChoice d;
        d.stage = gate_target;
        d.point = gate_point;
        d.purpose = dw4::engine::DepartureChoice::Purpose::gate;
        d.menu = "i_zzstchgcur";
        d.items = 4;
        d.reader = identity_;
        d.back_point = gate_back_point;
        return d;
    }

    std::string stage() const override { return stage_; }
    std::optional<std::string> pending_stage() const override { return pending_; }
    int pending_point() const override { return 1; }
    bool supports_barriers() const override { return true; }
    std::uint64_t load_serial() const override { return serial_; }
    std::vector<std::uint8_t> snapshot_bytes(bool canonical) const override {
        auto bytes = load_snapshot_;
        if (!canonical) bytes.push_back(0xAA); // a presentation-only byte that differs between machines
        return bytes;
    }
    std::uint64_t net_fingerprint() override { return mix(mix(mix(state_, frame_), present_), std::hash<std::string>{}(stage_)); }
    // (A fork this step, not yet reported, and a GO's departure on its way are
    // not safe, as Engine::membership_safe's pending fork and the departure's
    // fade: the membership events behind a GO wait, 6.5.)
    bool membership_safe() const override { return !pending_ && prompt_owner_ < 0 && test_busy <= 0 && !split_ && go_at_ == 0; }
    bool joinable(w::NotJoinable& reason) const override {
        if (is_hub_stage(stage_)) {
            reason = w::NotJoinable::hub;
            return false;
        }
        if (pending_) {
            reason = w::NotJoinable::loading;
            return false;
        }
        if (prompt_owner_ >= 0) {
            reason = w::NotJoinable::menu;
            return false;
        }
        // The entry scene: the first 120 frames of a visit.
        if (frame_ < 120) {
            reason = w::NotJoinable::entry_scene;
            return false;
        }
        return true;
    }
    bool boss_lock() const override { return false; }
    std::uint64_t session_context_hash() const override { return 0x1234; }
    std::uint8_t alive_mask() const override { return std::uint8_t(present_ & ~dead); }
    std::uint8_t present_mask() const override { return present_; }
    std::vector<std::uint8_t> carry(int slot) const override { return {std::uint8_t(slot), 0xC0, 0xDE}; }
    std::optional<SplitInfo> take_split() override { return std::exchange(split_, std::nullopt); }
    dw4::engine::StoryValues story_values() const override { return story; }
    PresenceFacts presence(int slot) const override {
        PresenceFacts f;
        f.in_stage = !pending_;
        f.x = figure_x ? *figure_x : figure_walk ? float(frame_) * *figure_walk : float(state_ & 0xFF);
        f.z = float(slot);
        f.level = 3;
        f.max_hp = 50;
        f.hp = figure_hp.value_or(f.max_hp);
        f.clip = figure_clip;
        f.weapon = "w_test01";
        f.busy = figure_busy;
        f.figure_hidden = figure_hidden;
        return f;
    }
    // PRESENCE2's figure facts (round 60); figure_x holds the figure still,
    // figure_walk moves it this far every step (else it follows the state, a
    // new place every step); figure_hidden: the model is not shown (a
    // scene); figure_hp: HP other than the maximum (a script may raise it
    // above).
    std::optional<float> figure_x, figure_walk;
    std::optional<int> figure_hp;
    std::string figure_clip = "waitD";
    bool figure_busy = false, figure_hidden = false;
    // A departure the stage announces (Engine::take_heading: its NEXTSTAGE
    // is due after a scene; a test sets it, the next step reports it).
    std::optional<HeadingInfo> heading;
    std::optional<HeadingInfo> take_heading() override { return std::exchange(heading, std::nullopt); }
    // Round J (checkpoint tests): a tick state of state_ and frame_, state-safe
    // and past the visit's load end from frame 10; tick_state_throws makes its
    // writer fail as a TickStateError does (a node outside the serializer's lists).
    bool tick_states = false;
    bool tick_state_throws = false;
    int tick_state_hashes = 0;
    bool state_safe() const override { return tick_states && !pending_; }
    bool load_ended() const override { return tick_states && !pending_ && frame_ >= 10; }
    std::vector<std::uint8_t> save_tick_state(bool canonical) override {
        (void)canonical;
        if (tick_state_throws) throw std::runtime_error("tick state: a node outside the scene cache");
        return serialize();
    }
    std::uint64_t tick_state_hash() override {
        if (tick_state_throws) throw std::runtime_error("tick state: a node outside the scene cache");
        ++tick_state_hashes;
        return mix(state_, frame_);
    }
    int tick_state_applies = 0;
    std::string apply_tick_state(std::span<const std::uint8_t> bytes) override {
        if (!tick_states) return "unsupported";
        try {
            w::Reader r{bytes};
            stage_ = r.str8();
            serial_ = r.u64();
            state_ = r.u64();
            frame_ = r.u64();
            present_ = r.u8();
        } catch (const w::WireError&) {
            return "header";
        }
        ++tick_state_applies;
        return "";
    }

    void step(const std::array<dw4::PadSample, 4>& pads) {
        ++frame_;
        if (test_busy > 0) --test_busy;
        if (home_at_ && frame_ >= home_at_ && !pending_) {
            home_at_ = 0;
            pending_ = "stagml11";
            transition_left_ = transition_steps;
            return;
        }
        if (go_at_ && frame_ >= go_at_ && !pending_) {
            // (Round I1b: the GO's departure ends with its NEXTSTAGE, the stage as named.)
            go_at_ = 0;
            pending_ = go_stage_;
            transition_left_ = transition_steps;
            return;
        }
        if (pending_) {
            if (--transition_left_ > 0) return;
            // The load step: the barrier's membership, the new stage, the snapshot.
            stage_ = *pending_;
            pending_.reset();
            ++serial_;
            frame_ = 0;
            prompt_owner_ = -1;
            leaving_ = 0;
            if (barrier_) {
                // The story first (8.5; round I1a: merged, or replaced at the
                // return home and a guest's NEW), as Engine::apply_barrier.
                if (!barrier_->story.empty()) {
                    const auto room = dw4::engine::story::decode_story(barrier_->story, barrier_->story_revision);
                    if (barrier_->story_mode == w::StoryMode::replace) {
                        dw4::engine::story::replace(story, room);
                        ++story_replaces;
                    } else {
                        dw4::engine::story::apply(story, room, {});
                    }
                    ++story_merges;
                }
                present_ = barrier_->present();
                for (const auto& c : barrier_->co_arrivals) state_ = mix(state_, 0x5000 + c.slot + c.carry.size());
                barrier_.reset();
            }
            state_ = mix(state_, serial_);
            take_load_snapshot();
            return;
        }
        auto masked = pads;
        for (int s = 0; s < 4; ++s)
            if (leaving_ & (1u << s)) masked[std::size_t(s)] = dw4::PadSample{true, {}, {}, 0}; // leaving: neutral and connected (6.5)
        if (stage_ == "stagml11" && identity_ >= 0 && identity_ < 4) {
            const std::uint32_t held = masked[std::size_t(identity_)].buttons;
            const std::uint32_t pressed = held & ~gate_previous;
            gate_previous = held;
            if (gate_menu && (pressed & dw4::pad_button::a)) {
                gate_menu = false;
                ++gate_confirms;
                state_ = mix(state_, 0x8000 + std::uint64_t(gate_point));
                pending_ = gate_target;
                transition_left_ = transition_steps;
                return;
            }
            if (gate_menu && (pressed & dw4::pad_button::b)) {
                gate_menu = false;
                ++gate_cancels;
                state_ = mix(state_, 0x8100);
            }
        }
        for (int s = 0; s < 4; ++s) {
            const auto& p = masked[std::size_t(s)];
            const bool here = (present_ & (1u << s)) != 0;
            if (!here || !p.connected) continue; // absent slots play the neutral pad (6.5)
            state_ = mix(state_, std::uint64_t(s) << 40 | std::uint64_t(p.buttons) << 8 | std::uint64_t(std::uint8_t(std::int8_t(p.left.x * 74.0f))));
        }
        if (prompt_owner_ < 0) {
            for (int s = 0; s < 4; ++s)
                if ((present_ & (1u << s)) && masked[std::size_t(s)].connected && (masked[std::size_t(s)].buttons & kPromptButton)) {
                    prompt_owner_ = s;
                    state_ = mix(state_, 0x6000 + std::uint64_t(s));
                    break;
                }
        } else if (leaving_ & (1u << prompt_owner_)) {
            state_ = mix(state_, 0x6200 + std::uint64_t(prompt_owner_)); // closed as No
            prompt_owner_ = -1;
        } else {
            const int reader = prompt_owner_;
            if (masked[std::size_t(reader)].connected && (masked[std::size_t(reader)].buttons & kAnswerButton)) {
                prompt_owner_ = -1;
                state_ = mix(state_, 0x6100 + std::uint64_t(reader));
            }
        }
        const int lead = present_ ? std::countr_zero(unsigned(present_)) : 0;
        // (The first ticks of a visit take no exit: a sealed start repeats the
        // barrier tick's pad for its first d ticks, as the transition steps.)
        if (frame_ >= 16 && masked[std::size_t(lead)].connected && (masked[std::size_t(lead)].buttons & kExitButton)) {
            const auto it = std::find(route.begin(), route.end(), stage_);
            if (it != route.end() && it + 1 != route.end()) {
                pending_ = *(it + 1);
                transition_left_ = transition_steps;
                return;
            }
        }
        if (std::popcount(unsigned(present_)) >= 2)
            for (int s = 0; s < 4; ++s)
                if ((present_ & (1u << s)) && masked[std::size_t(s)].connected && (masked[std::size_t(s)].buttons & kForkButton)) {
                    SplitInfo split{std::uint8_t(1u << s), false, identity_ == s};
                    const std::uint8_t side = split.local_leaves ? split.leavers : std::uint8_t(present_ & ~split.leavers);
                    present_ &= side;
                    state_ = mix(state_, 0x7000 + side);
                    split_ = split;
                    break;
                }
    }
    void replay_step(const std::array<dw4::PadSample, 4>& pads) override {
        ++replayed;
        ++audio_steps; // the music position
        step(pads);
    }
    bool restore(std::span<const std::uint8_t> bytes) override {
        try {
            w::Reader r{bytes};
            stage_ = r.str8();
            serial_ = r.u64();
            state_ = r.u64();
            frame_ = r.u64();
            present_ = r.u8();
        } catch (const w::WireError&) {
            return false;
        }
        pending_.reset();
        barrier_.reset();
        split_.reset();
        prompt_owner_ = -1;
        leaving_ = 0;
        take_load_snapshot();
        ++restores;
        return true;
    }
    void set_replay(bool on) override { replay_ = on; }
    void start_online(int slot, std::uint8_t) override { identity_ = slot; }
    void set_split_identity(int slot) override { identity_ = slot; }
    void set_fork_sides(std::span<const w::Fork>) override {}
    void set_barrier(const w::BarrierOk& ok) override {
        barrier_ = ok;
        // Round I1a (19.8.3, M2): a DISSOLVE redirected: the pending stage replaced.
        if (ok.decision == w::Decision::dissolve && !ok.redirect_stage.empty() && pending_) {
            pending_ = ok.redirect_stage;
            redirected_point = ok.redirect_point;
        }
        if (ok.decision == w::Decision::dissolve) own_settings_set_back = true;
        if (ok.story_mode == w::StoryMode::replace && ok.wid != 0 && ok.decision != w::Decision::dissolve) host_settings = ok.settings;
    }
    // Round I1a (19.6.5, 19.6.10): HOME for a mask (a fork of exactly the
    // mask, the leavers' copy departs for the lobby) and the local way home.
    void home(std::uint8_t mask) override {
        mask &= present_;
        if (!mask) return;
        state_ = mix(state_, 0x8000 + mask);
        if (mask != present_) {
            SplitInfo split{mask, false, (mask & (1u << identity_)) != 0};
            present_ = split.local_leaves ? mask : std::uint8_t(present_ & ~mask);
            split_ = split;
            if (!split.local_leaves) return;
        }
        // (The departure's records take a while, as the real one's fade:
        // the NEXTSTAGE comes 30 frames later.)
        home_at_ = frame_ + 30;
        ++homes; // (this copy departs)
    }
    bool depart_home() override {
        if (std::popcount(unsigned(present_)) != 1 || !membership_safe() || home_at_) return false;
        ++homes;
        home_at_ = frame_ + 30;
        return true;
    }
    std::uint64_t home_at_ = 0;
    int homes = 0;
    // Round I1b (19.30.7): GO for a mask (the step's GOs together): the slots
    // that are present, not leaving and alive (`dead` marks the fallen) go;
    // the exit's side rule; part of the present slots: a fork, the leavers'
    // copy departs to the stage as named.
    struct GoCall {
        std::uint8_t mask{};
        std::string stage;
        int point{};
        std::uint64_t frame{};
    };
    std::vector<GoCall> go_calls;
    std::uint8_t dead = 0;
    bool go(std::uint8_t mask, const std::string& stage, int point) override {
        go_calls.push_back({mask, stage, point, frame_});
        const std::uint8_t alive = std::uint8_t(present_ & ~dead);
        mask &= std::uint8_t(alive & ~leaving_);
        if (!mask) return false;
        const std::uint8_t leavers = dw4::engine::exit_leavers(present_, alive, mask);
        if (!leavers) return false;
        state_ = mix(state_, 0x9000 + leavers);
        if (leavers != present_) {
            SplitInfo split{leavers, false, (leavers & (1u << identity_)) != 0};
            present_ = split.local_leaves ? leavers : std::uint8_t(present_ & ~leavers);
            split_ = split;
            if (!split.local_leaves) return false;
        }
        go_stage_ = stage;
        go_at_ = frame_ + 30;
        ++gos;
        return true;
    }
    std::uint64_t go_at_ = 0;
    std::string go_stage_;
    int gos = 0;
    // Round I1b's scenario hooks (the TEST event; the session applies them in
    // development builds only): defeat marks the slot fallen, busy keeps the
    // engine not membership-safe for that many steps.
    struct TestCall {
        int slot{};
        w::TestHook hook{};
        int argument{};
        std::uint64_t frame{};
    };
    std::vector<TestCall> test_calls;
    int test_busy = 0;
    void test_hook(int slot, w::TestHook hook, int argument) override {
        test_calls.push_back({slot, hook, argument, frame_});
        if (hook == w::TestHook::defeat) dead = std::uint8_t(dead | (1u << (slot & 3)));
        if (hook == w::TestHook::busy) test_busy = argument;
    }
    std::optional<int> redirected_point;
    bool own_settings_set_back = false;
    std::optional<w::Settings> host_settings;
    bool join_player(int slot, std::span<const std::uint8_t> carry, int point) override {
        if (carry.empty()) return false; // (a carry the engine cannot read)
        present_ |= std::uint8_t(1u << slot);
        state_ = mix(mix(state_, 0x1000 + std::uint64_t(slot) + std::uint64_t(point)), carry.size());
        return true;
    }
    void remove_player(int slot) override {
        present_ &= std::uint8_t(~(1u << slot));
        leaving_ &= std::uint8_t(~(1u << slot));
        state_ = mix(state_, 0x2000 + std::uint64_t(slot));
    }
    void mark_leaving(int slot) override {
        if (!(present_ & (1u << slot)) || (leaving_ & (1u << slot))) return;
        leaving_ |= std::uint8_t(1u << slot);
        state_ = mix(state_, 0x4000 + std::uint64_t(slot));
    }
    int intents = 0; // INTENT events applied
    void intent(int slot, w::IntentKind kind, int argument) override {
        ++intents;
        state_ = mix(state_, 0x3000 + slot * 16 + int(kind) + argument);
    }
    void set_view_slot(int) override {}
};

// One client: engine, session and the host loop's own 59.94 Hz clock.
struct Client {
    std::string name;
    Engine engine;
    std::unique_ptr<Session> session;
    double accumulator = 0;
    // The buttons this client holds in a step (stage, mode and tick known).
    std::function<std::uint32_t(const Client&)> input;
    int steps = 0;
    std::uint64_t pictures = 0;

    void frame(double now, double elapsed) {
        session->poll(now);
        if (session->take_engine_replaced()) accumulator = 0;
        accumulator += elapsed;
        int host = 0;
        while (accumulator >= kFieldSeconds && host < 4) {
            accumulator -= kFieldSeconds;
            ++host;
        }
        if (session->paces()) accumulator = 0;
        const int n = session->steps_allowed(host, elapsed);
        for (int i = 0; i < n; ++i) {
            if (!session->step_ready()) break;
            dw4::PadSample local{true, {0.25f, 0}, {}, input ? input(*this) : 0u};
            const auto pads = session->before_step(local, now);
            engine.step(pads);
            session->after_step(now);
            ++steps;
        }
        session->poll(now);
    }
};

class Hub {
public:
    struct Conn {
        std::unique_ptr<Transport> pipe;
        std::uint32_t member = 0;
        bool hello = false;
        std::string line;
        w::FrameReader reader;
        std::deque<std::pair<double, std::vector<std::uint8_t>>> inbound, outbound; // (due time, bytes)
        bool dropped = false;
    };
    struct Member {
        std::uint32_t id = 0;
        std::string name;
        std::uint32_t inst = 0;
        int slot = 0;
        Conn* conn = nullptr;
        std::uint32_t observing = 0; // instance being caught up on
        int observe_slot = 0;
        int old_slot = 0;            // its slot in the group it arrived with (a fallback's slot map)
        std::vector<std::uint8_t> carry;
        std::optional<w::Barrier> parked;
        double parked_at = 0;
        double last_wait = -10;
        bool lagging = false;
        bool resyncing = false;
    };
    struct Instance {
        std::uint32_t id = 0, visit = 0;
        std::string stage;
        std::map<int, std::uint32_t> slots; // slot -> member
        bool hub = false;
        bool sealed = false;
        std::vector<std::uint8_t> snapshot;
        std::map<std::uint32_t, w::TickBody> log; // ticks since the snapshot
        double s0 = 0;                      // S(t) = s0 + t * field
        std::uint32_t next_seal = 0;
        std::optional<std::uint32_t> lead_from; // sealing ends before this tick (one member left)
        std::map<std::uint32_t, std::map<int, w::WirePad>> pads; // tick -> slot -> pad
        std::map<int, w::WirePad> last;
        std::map<int, int> fills;
        std::map<std::uint32_t, std::vector<w::Event>> events; // tick -> events
        std::set<std::uint32_t> observers;
        std::optional<std::uint32_t> pending_joiner;
        std::optional<std::pair<std::uint8_t, std::uint8_t>> pending_home; // JOIN_REQ home asked of the solo leader: mask, reason
        std::optional<w::Event> pending_go;                               // round I1b: JOIN_REQ go asked of the solo leader
        std::map<std::uint32_t, std::map<std::uint32_t, std::uint64_t>> netfp; // tick -> member -> hash
        std::map<std::uint32_t, std::uint64_t> snaphash; // member -> hash
        std::set<std::uint32_t> splits;                  // fork ticks already processed
        std::map<int, std::uint32_t> leave_at;           // slot -> its LEAVE's tick (sealed through it)
        std::uint8_t present() const {
            std::uint8_t m = 0;
            for (const auto& [s, _] : slots) m |= std::uint8_t(1u << s);
            return m;
        }
        w::Mode mode() const { return hub ? w::Mode::hub : slots.size() >= 2 || sealed ? w::Mode::sealed : w::Mode::solo; }
    };

    double latency = 0.02; // one way
    double jitter = 0.0;   // extra one-way delay, uniform in [0, jitter], order kept (TCP)
    std::uint64_t jitter_state = 88172645463325252ull;
    double delay_of(std::deque<std::pair<double, std::vector<std::uint8_t>>>& queue, double now) {
        jitter_state ^= jitter_state << 13;
        jitter_state ^= jitter_state >> 7;
        jitter_state ^= jitter_state << 17;
        const double extra = jitter * double(jitter_state % 10000) / 10000.0;
        const double due = now + latency + extra;
        return queue.empty() ? due : std::max(due, queue.back().first);
    }
    int suggested_delay = 3;
    bool answer_barriers = true;
    // Round I1b, the arrival prompt: the rows PLAN is answered with (none:
    // nothing to choose) and a barrier from a hub stage without a plan is
    // held for (BARRIER_WAIT choice + OPTIONS); PLANs dropped unanswered
    // (W2b's hook); what came.
    std::optional<w::Options> prompt;
    bool drop_plans = false;
    std::vector<w::Plan> plans;
    std::vector<w::PromptChoice> choices;
    std::vector<std::uint16_t> barrier_plans; // the plan id of every BARRIER from a hub stage into a field
    std::set<std::uint32_t> prompt_held;
    double co_arrival_hold = 3.0;
    // Round 60: WELCOME announces PRESENCE2 (a hub of this round), and what came.
    bool presence_v2 = false;
    // Round J: WELCOME's flag 4 (this hub takes STATEHASH) and the reports that came (member name, report).
    bool checkpoints = false;
    std::vector<std::pair<std::string, w::StateHash>> statehashes;
    // Protocol 48: the checkpoint uploads that began (member name, tick) and every NETFP (member name, report).
    std::vector<std::pair<std::string, std::uint32_t>> uploads;
    std::vector<std::pair<std::string, w::NetFp>> netfps;
    // Protocol 48, the joiner's side: with checkpoint_joins a JOIN asks the
    // target's lowest member for a checkpoint and the joiner's state is that
    // checkpoint (codec 2: the load ticks [0, K), the tick state of T, the
    // ticks from T) once its upload is complete; every READY is noted.
    bool checkpoint_joins = false;
    struct StoredUpload {
        std::uint32_t visit{}, tick{}, load_end{}, total{};
        std::uint64_t hash{};
        std::vector<std::uint8_t> bytes;
    };
    std::map<std::uint32_t, StoredUpload> stored_checkpoints, running_uploads; // instance -> upload
    std::map<std::uint32_t, std::vector<std::uint32_t>> checkpoint_waiters;     // instance -> joiners
    struct ReadySeen {
        std::string name;
        std::uint32_t tick{};
        std::uint64_t fingerprint{};
        bool from_checkpoint{};
    };
    std::vector<ReadySeen> readies;
    // CHECKPOINT_ASK to a member (a join's ask: its next state-safe tick).
    void ask_checkpoint(const std::string& name, double now) {
        for (auto& [id, m] : members)
            if (m.name == name && instances.contains(m.inst))
                send(id, w::Type::checkpoint_ask, w::encode(w::CheckpointAsk{m.inst, instances[m.inst].visit, 0, 0}), now);
    }
    // A joiner's first READYs answered with its whole state again, as the relay answers a READY mismatch.
    int refuse_ready = 0, ready_refused = 0;
    std::uint64_t presence_frames = 0, presence2_frames = 0, unknown_frames = 0; // (unknown: PRESENCE2 without presence_v2)
    std::vector<w::Presence> presence2_seen; // every PRESENCE2 that came, its member set
    double state_delay = 0.0; // extra delay of STATE_DATA (a slow state transfer)
    double state_pacing = 0.0; // STATE_DATA paced like the relay's state delivery: sent this much later, LIVE ticks go first
    std::vector<std::tuple<double, std::uint32_t, std::vector<std::uint8_t>>> paced_state;
    double wait_hold = 0.0;   // a field barrier is held this long with BARRIER_WAIT every second (a join hold), then decided
    std::vector<std::string> log_lines;
    std::map<std::uint32_t, Member> members;
    std::map<std::uint32_t, Instance> instances;
    std::vector<std::unique_ptr<Conn>> conns;
    std::uint32_t next_inst = 100;
    std::uint64_t netfp_compared = 0, netfp_mismatches = 0, late_pads = 0, fills_total = 0, splits = 0, snaphash_mismatches = 0, reassigns = 0;
    std::map<std::uint32_t, std::uint64_t> input_frames, input_pads; // INPUT frames and the pads in them, per member
    double last_ping = 0;
    std::uint32_t ping_nonce = 0;
    // The room story (8.3-8.6).
    dw4::engine::RoomStory room_story;
    dw4::engine::story::StoryLines story_lines;
    dw4::engine::StoryKeys dropped;
    bool owner_away = false; // with the drop policy: pending values are refused and recorded in `dropped`
    bool not_started = false; // the owner has no story yet (8.6): refused not_started and recorded in `dropped`
    bool story_lost = false; // STORY frames are counted and then lost (no merge, no answer), as on a dying connection
    int story_wait = 0;      // the next STORY frames are answered "wait" for every key (a visit too young, 8.7)
    int story_queued = 0;    // the next STORY frames are answered "queued" for every key (earned, merged later; round G-core)
    int stories = 0, story_acks = 0;
    // Round I1a: every BARRIER_OK passes shape_ok (a test makes it a world
    // decision: wid, host, arrival, owner, story mode, settings, redirect).
    std::function<void(std::uint32_t member, w::BarrierOk&)> shape_ok;
    w::BarrierOk with_story(w::BarrierOk ok, std::uint32_t member) const {
        ok.story_revision = room_story.revision;
        ok.story = dw4::engine::story::encode_story(room_story);
        if (shape_ok) shape_ok(member, ok);
        return ok;
    }
    void broadcast_story(double now) {
        w::StoryAck ack{room_story.revision, dw4::engine::story::encode_story(room_story), 0, {}};
        for (auto& [id, _] : members) send(id, w::Type::story_ack, w::encode(ack), now);
    }
    // Round I1a: the last STORY's owner key, the INTENT leaves, the wid the
    // next WELCOME of a member names (0: the relay knows none).
    std::uint32_t last_story_owner = 0;
    std::vector<w::Story> story_frames; // every STORY that came (its owner key and delta)
    int leave_intents = 0;
    // Round I1b (moderation): the INSTANCE_OPs and REPORTs that came, and the
    // JOIN_ATs (a JOIN_REQ home answered or not).
    std::vector<std::pair<std::uint32_t, w::InstanceOp>> instance_ops;
    std::vector<std::pair<std::uint32_t, w::Report>> reports;
    int join_ats = 0;
    std::map<std::uint32_t, std::uint32_t> welcome_wid;
    std::set<std::uint32_t> wid_forgotten; // members whose next WELCOME is not resumable (the relay restarted)
    void on_story(Member& m, const w::Story& s, double now) {
        ++stories;
        last_story_owner = s.owner;
        story_frames.push_back(s);
        if (story_lost) return;
        w::StoryAck answer{0, {}, w::StoryAck::answer, {}};
        using K = dw4::engine::StoryRefused::Kind;
        bool changed = false;
        if (story_wait > 0 || story_queued > 0) {
            const auto reason = story_wait > 0 ? dw4::engine::StoryRefusal::wait : dw4::engine::StoryRefusal::queued;
            if (story_wait > 0) --story_wait;
            else --story_queued;
            const auto wait = [&](K kind, std::int32_t index) { answer.refused.push_back({kind, index, reason}); };
            if (s.delta.phase) wait(K::phase, s.delta.phase);
            for (const auto f : s.delta.flags) wait(K::flag, f);
            for (const auto k : s.delta.folder) wait(K::folder, k);
            for (const auto& g : s.delta.counters) wait(K::counter, g.counter);
        } else if (owner_away || not_started) {
            const auto why = not_started ? dw4::engine::StoryRefusal::not_started : dw4::engine::StoryRefusal::owner_away;
            const auto refuse = [&](K kind, std::int32_t index) { answer.refused.push_back({kind, index, why}); };
            if (s.delta.phase) {
                refuse(K::phase, s.delta.phase);
                dropped.flags.push_back(dw4::engine::story::phase_flags()[std::size_t(s.delta.phase - 1)]);
            }
            for (const auto f : s.delta.flags) {
                refuse(K::flag, f);
                if (std::find(dropped.flags.begin(), dropped.flags.end(), f) == dropped.flags.end()) dropped.flags.push_back(f);
            }
            for (const auto k : s.delta.folder) {
                refuse(K::folder, k);
                if (std::find(dropped.folder.begin(), dropped.folder.end(), k) == dropped.folder.end()) dropped.folder.push_back(k);
            }
            for (const auto& g : s.delta.counters) {
                refuse(K::counter, g.counter);
                if (std::find(dropped.counters.begin(), dropped.counters.end(), g.counter) == dropped.counters.end()) dropped.counters.push_back(g.counter);
            }
        } else {
            auto r = dw4::engine::story::merge(room_story, s.delta, 0, story_lines);
            answer.refused = r.refused;
            changed = r.changed;
            room_story = r.story;
        }
        answer.revision = room_story.revision;
        answer.story = dw4::engine::story::encode_story(room_story);
        send(m.id, w::Type::story_ack, w::encode(answer), now);
        ++story_acks;
        if (changed) {
            log_lines.push_back("story " + m.name + " -> revision " + std::to_string(room_story.revision));
            broadcast_story(now);
        }
    }

    std::unique_ptr<Transport> connect(std::uint32_t member, const std::string& name) {
        auto pipe = make_memory_pipe();
        auto conn = std::make_unique<Conn>();
        conn->pipe = std::move(pipe.server);
        conn->member = member;
        auto& m = members[member];
        m.id = member;
        m.name = name;
        m.conn = conn.get();
        conns.push_back(std::move(conn));
        return std::move(pipe.client);
    }
    void drop(std::uint32_t member) {
        auto& m = members[member];
        if (m.conn) {
            m.conn->dropped = true;
            m.conn->pipe->close();
        }
        m.conn = nullptr;
    }

    void send(std::uint32_t member, w::Type type, const std::vector<std::uint8_t>& payload, double now) {
        auto it = members.find(member);
        if (it == members.end() || !it->second.conn || !it->second.conn->hello) return; // nothing before the OK line
        auto& queue = it->second.conn->outbound;
        queue.emplace_back(delay_of(queue, now), w::encode_frame(type, payload));
    }
    void send_ticks(std::uint32_t member, w::Type type, const Instance& inst, std::uint32_t first, std::vector<w::TickBody> ticks, double now) {
        send(member, type, w::encode(w::Ticks{inst.id, inst.visit, first, std::move(ticks)}), now);
    }

    void welcome(Conn& c, double now) {
        auto& m = members[c.member];
        // A reconnect within the window keeps the slot (RESUME follows).
        const bool resumable = m.inst != 0 && instances.contains(m.inst) && !instances[m.inst].hub && instances[m.inst].slots.contains(m.slot) &&
                               instances[m.inst].slots.at(m.slot) == m.id;
        if (!resumable) {
            auto& hub = new_instance("", 1);
            hub.hub = true;
            hub.slots[0] = m.id;
            m.inst = hub.id;
            m.slot = 0;
        }
        const auto& inst = instances[m.inst];
        w::Welcome welcome{m.id, inst.id, inst.visit, std::uint8_t(m.slot), inst.mode(), inst.stage, w::kNoTick, 0, 30,
                           std::uint8_t((resumable && !wid_forgotten.contains(m.id) ? w::Welcome::resumable : 0) | (presence_v2 ? w::Welcome::presence_v2 : 0) |
                                        (checkpoints ? w::Welcome::checkpoints : 0))};
        welcome.wid = welcome_wid.contains(m.id) ? welcome_wid.at(m.id) : 0;
        send(m.id, w::Type::welcome, w::encode(welcome), now);
        send(m.id, w::Type::story_ack, w::encode(w::StoryAck{room_story.revision, dw4::engine::story::encode_story(room_story), 0, {}}), now);
        // Protocol 49: the names come with MEMBER_INFO (19.18).
        for (auto& [id, other] : members)
            if (id != c.member) {
                send(id, w::Type::member_info, w::encode(w::MemberInfo{c.member, m.name, 1, w::MemberInfo::own_instance}), now);
                send(c.member, w::Type::member_info, w::encode(w::MemberInfo{id, other.name, 1, w::MemberInfo::own_instance}), now);
            }
    }

    void tick(double now) {
        for (auto it = paced_state.begin(); it != paced_state.end();)
            if (std::get<0>(*it) <= now) {
                send(std::get<1>(*it), w::Type::state_data, std::get<2>(*it), now);
                it = paced_state.erase(it);
            } else {
                ++it;
            }
        for (auto& c : conns) {
            if (c->dropped) continue;
            std::vector<std::uint8_t> bytes;
            const bool open = c->pipe->receive(bytes);
            if (!bytes.empty()) c->inbound.emplace_back(delay_of(c->inbound, now), std::move(bytes));
            while (!c->inbound.empty() && c->inbound.front().first <= now) {
                auto data = std::move(c->inbound.front().second);
                c->inbound.pop_front();
                std::size_t at = 0;
                while (!c->hello && at < data.size()) {
                    const char ch = char(data[at++]);
                    if (ch != '\n') {
                        c->line += ch;
                        continue;
                    }
                    const auto hello = w::parse_hello(c->line);
                    const std::string answer = hello ? "OK " + std::to_string(c->member) + " 1000\n" : "ERR protocol\n";
                    c->outbound.emplace_back(now + latency, std::vector<std::uint8_t>(answer.begin(), answer.end()));
                    c->hello = hello.has_value();
                    if (c->hello) welcome(*c, now);
                }
                if (at < data.size()) c->reader.feed(std::span(data).subspan(at));
                while (auto f = c->reader.next()) handle(c->member, *f, now);
            }
            while (!c->outbound.empty() && c->outbound.front().first <= now) {
                c->pipe->send(c->outbound.front().second);
                c->outbound.pop_front();
            }
            if (!open) c->dropped = true;
        }
        if (now - last_ping >= 1.0) {
            last_ping = now;
            for (auto& [id, m] : members) send(id, w::Type::ping, w::encode(w::Ping{++ping_nonce, 0, 1, std::uint8_t(suggested_delay)}), now);
        }
        for (auto& [id, inst] : instances) seal(inst, now);
        // Held barriers: BARRIER_WAIT every second, then the decision.
        for (auto& [id, m] : members)
            if (m.parked && answer_barriers && wait_hold > 0 && !is_hub_stage(m.parked->stage)) {
                if (now - m.parked_at >= wait_hold) decide(m, now);
                else if (now - m.last_wait >= 1.0) {
                    m.last_wait = now;
                    send(id, w::Type::barrier_wait, w::encode(w::BarrierWait{m.parked->inst, w::WaitReason::not_joinable,
                                                                             std::uint16_t(wait_hold - (now - m.parked_at)), {"kim"}, 0}),
                         now);
                }
            }
        // Co-arrival holds that ran out: NEW.
        for (auto& [id, m] : members)
            if (m.parked && answer_barriers && wait_hold <= 0 && now - m.parked_at > co_arrival_hold && !prompt_held.contains(id)) decide(m, now);
    }

    void lag(std::uint32_t member, bool on, double now) {
        auto& m = members[member];
        if (m.lagging == on) return;
        m.lagging = on;
        for (auto& [id, other] : members)
            send(id, w::Type::world_notice, w::encode(w::WorldNotice{on ? w::NoticeKind::lagging : w::NoticeKind::lag_over, member, m.name, 0, {}}), now);
    }

    void seal(Instance& inst, double now) {
        if (!inst.sealed) return;
        while (inst.sealed && inst.s0 + double(inst.next_seal) * kFieldSeconds <= now) {
            if (inst.lead_from && inst.next_seal >= *inst.lead_from) {
                // One member left: it leads alone from here (LEAD_FROM was sealed before).
                inst.sealed = false;
                inst.lead_from.reset();
                break;
            }
            const std::uint32_t t = inst.next_seal++;
            for (auto it = inst.leave_at.begin(); it != inst.leave_at.end();)
                if (t > it->second) {
                    inst.slots.erase(it->first);
                    it = inst.leave_at.erase(it);
                } else {
                    ++it;
                }
            w::TickBody body;
            body.present = inst.present();
            for (const auto& [slot, member] : inst.slots) {
                auto& pads = inst.pads[t];
                if (const auto p = pads.find(slot); p != pads.end()) {
                    body.pads[std::size_t(slot)] = p->second;
                    inst.last[slot] = p->second;
                    inst.fills[slot] = 0;
                    lag(member, false, now);
                } else {
                    // Filled at once: the last sealed pad (six ticks), then neutral.
                    body.pads[std::size_t(slot)] = ++inst.fills[slot] <= 6 ? inst.last[slot] : w::WirePad{};
                    ++fills_total;
                    if (inst.fills[slot] >= 30) lag(member, true, now);
                }
            }
            inst.pads.erase(t);
            if (const auto e = inst.events.find(t); e != inst.events.end()) {
                body.events = e->second;
                inst.events.erase(e);
            }
            inst.log[t] = body;
            std::set<std::uint32_t> members_now;
            for (const auto& [slot, member] : inst.slots) members_now.insert(member);
            for (const auto member : members_now) send_ticks(member, w::Type::sealed, inst, t, {body}, now);
            for (const auto observer : inst.observers)
                if (!members_now.contains(observer)) send_ticks(observer, w::Type::live, inst, t, {body}, now);
        }
    }

    Instance& new_instance(const std::string& stage, std::uint32_t visit) {
        auto& inst = instances[next_inst];
        inst.id = next_inst++;
        inst.visit = visit;
        inst.stage = stage;
        return inst;
    }

    void decide(Member& m, double now) {
        const auto b = *m.parked;
        auto* from = instances.contains(b.inst) ? &instances[b.inst] : nullptr;
        w::BarrierOk ok;
        ok.inst = b.inst;
        ok.visit = b.visit;
        ok.stage = b.stage;
        ok.point = b.point;
        if (is_hub_stage(b.stage)) {
            // DISSOLVE: its own solo hub instance, slot 0.
            auto& hub = new_instance(b.stage, 1);
            hub.hub = true;
            hub.slots[0] = m.id;
            ok.decision = w::Decision::dissolve;
            ok.new_inst = hub.id;
            ok.new_visit = 1;
            ok.own_slot = 0;
            ok.slot_map[std::size_t(m.slot)] = 0;
            ok.mode = w::Mode::hub;
            m.inst = hub.id;
            m.slot = 0;
            m.parked.reset();
            send(m.id, w::Type::barrier_ok, w::encode(with_story(ok, m.id)), now);
            log_lines.push_back("dissolve " + m.name + " " + b.stage);
            leave_instance_if_empty(from);
            return;
        }
        // Joinable instance of the target stage?
        Instance* target = nullptr;
        for (auto& [id, inst] : instances)
            if (inst.stage == b.stage && id != b.inst && !inst.hub && !inst.slots.empty() && inst.slots.size() < 4 && !inst.snapshot.empty()) target = &inst;
        // Another member parked for the same stage: co-arrival (the first parked is the base).
        Member* partner = nullptr;
        for (auto& [id, other] : members)
            if (id != m.id && other.parked && other.parked->stage == b.stage) partner = &other;
        if (partner && !target) {
            Member& base = partner->parked_at <= m.parked_at ? *partner : m;
            Member& late = &base == &m ? *partner : m;
            auto* late_from = instances.contains(late.parked->inst) ? &instances[late.parked->inst] : nullptr;
            auto& inst = new_instance(b.stage, 1);
            inst.slots[0] = base.id;
            inst.slots[1] = late.id;
            w::BarrierOk base_ok = ok;
            base_ok.inst = base.parked->inst;
            base_ok.visit = base.parked->visit;
            base_ok.decision = w::Decision::new_instance;
            base_ok.new_inst = inst.id;
            base_ok.new_visit = 1;
            base_ok.own_slot = 0;
            base_ok.slot_map = {-1, -1, -1, -1};
            base_ok.slot_map[std::size_t(base.slot)] = 0;
            base_ok.mode = w::Mode::sealed;
            base_ok.delay = std::uint8_t(suggested_delay);
            base_ok.co_arrivals = {{1, late.id, late.parked->carry}};
            w::BarrierOk late_ok = base_ok;
            late_ok.inst = late.parked->inst;
            late_ok.visit = late.parked->visit;
            late_ok.decision = w::Decision::co_arrive;
            late_ok.own_slot = 1;
            late_ok.slot_map = {-1, -1, -1, -1};
            late_ok.co_arrivals.clear();
            base.inst = late.inst = inst.id;
            base.slot = 0;
            late.slot = 1;
            base.parked.reset();
            late.parked.reset();
            pending_co_arrival[inst.id] = late.id;
            send(base.id, w::Type::barrier_ok, w::encode(with_story(base_ok, base.id)), now);
            send(late.id, w::Type::barrier_ok, w::encode(with_story(late_ok, late.id)), now);
            log_lines.push_back("co-arrival " + base.name + " + " + late.name + " in " + b.stage);
            leave_instance_if_empty(from);
            leave_instance_if_empty(late_from);
            return;
        }
        if (target) {
            int slot = 0;
            while (target->slots.contains(slot)) ++slot;
            ok.decision = w::Decision::join;
            ok.new_inst = target->id;
            ok.new_visit = target->visit;
            ok.own_slot = std::uint8_t(slot);
            ok.mode = target->mode();
            ok.delay = std::uint8_t(suggested_delay);
            m.old_slot = m.slot;
            m.inst = target->id;
            m.slot = slot;
            m.observing = target->id;
            m.observe_slot = slot;
            m.carry = b.carry;
            m.parked.reset();
            send(m.id, w::Type::barrier_ok, w::encode(with_story(ok, m.id)), now);
            if (checkpoint_joins) {
                // The state comes with the donor's upload (send_checkpoint_states).
                running_uploads.erase(target->id);
                checkpoint_waiters[target->id].push_back(m.id);
                send(target->slots.begin()->second, w::Type::checkpoint_ask, w::encode(w::CheckpointAsk{target->id, target->visit, 0, 0}), now);
                log_lines.push_back("join " + m.name + " -> " + target->stage + " slot " + std::to_string(slot) + " from a checkpoint");
                leave_instance_if_empty(from);
                return;
            }
            // The state: snapshot and every tick since; LIVE / SEALED keep coming.
            w::StateData state;
            state.purpose = w::StatePurpose::join;
            state.slot = std::uint8_t(slot);
            state.snapshot = target->snapshot;
            state.first = 0;
            for (std::uint32_t t = 0; target->log.contains(t); ++t) state.ticks.push_back(target->log.at(t));
            const auto body = w::encode_state_body(state);
            for (const auto& c : w::make_chunks(target->id, target->visit, body)) {
                // A slow transfer (state_delay) holds everything behind it too (TCP order);
                // a paced one (state_pacing) lets the LIVE ticks go first.
                if (state_pacing > 0) {
                    paced_state.emplace_back(now + state_pacing, m.id, w::encode(c));
                } else if (state_delay > 0 && m.conn) {
                    auto& queue = m.conn->outbound;
                    queue.emplace_back(std::max(delay_of(queue, now), now + state_delay), w::encode_frame(w::Type::state_data, w::encode(c)));
                } else {
                    send(m.id, w::Type::state_data, w::encode(c), now);
                }
            }
            target->observers.insert(m.id);
            log_lines.push_back("join " + m.name + " -> " + target->stage + " slot " + std::to_string(slot) + " after " +
                                std::to_string(target->log.size()) + " ticks");
            leave_instance_if_empty(from);
            return;
        }
        // NEW: the instance keeps its id with the next visit when the whole
        // group arrived, else a new one.
        const bool reuse = from && from->slots.size() == 1;
        auto& inst = reuse ? *from : new_instance(b.stage, 1);
        if (reuse) {
            inst.stage = b.stage;
            inst.visit += 1;
            inst.hub = false;
            inst.snapshot.clear();
            inst.log.clear();
            inst.snaphash.clear();
            inst.netfp.clear();
            inst.slots.clear();
        }
        inst.slots[0] = m.id;
        ok.decision = w::Decision::new_instance;
        ok.new_inst = inst.id;
        ok.new_visit = inst.visit;
        ok.own_slot = 0;
        ok.slot_map = {-1, -1, -1, -1};
        ok.slot_map[std::size_t(m.slot)] = 0;
        ok.mode = w::Mode::solo;
        m.inst = inst.id;
        m.slot = 0;
        m.parked.reset();
        send(m.id, w::Type::barrier_ok, w::encode(with_story(ok, m.id)), now);
        log_lines.push_back("new " + m.name + " " + b.stage);
        if (!reuse) leave_instance_if_empty(from);
    }
    std::map<std::uint32_t, std::uint32_t> pending_co_arrival; // instance -> late member

    // The relay stamps a member's LEAVE (its connection timed out, or it
    // left): at the next seal + 8, its pads end with that tick, the remaining
    // member leads alone after it (LEAD_FROM).
    // HOME for a solo-led stage group (19.6.5, the relay's TrySoloHome): JOIN_REQ
    // kind home to its leader; its JOIN_AT(J) seals J alone with HOME and
    // LEAD_FROM(J + 1), so the leader leads alone again after it.
    void ask_home(std::uint32_t member, std::uint8_t mask, w::HomeReason reason, double now) {
        auto& m = members[member];
        auto& inst = instances[m.inst];
        inst.pending_home = std::make_pair(mask, std::uint8_t(reason));
        w::JoinReq req{inst.id, inst.visit, mask, std::uint8_t(suggested_delay)};
        req.kind = w::JoinKind::home;
        send(member, w::Type::join_req, w::encode(req), now);
        log_lines.push_back("join_req home " + m.name);
    }

    // Round I1b (19.30.7): GO for a member: sealed, stamped at the next seal +
    // `ahead` (the relay's stamp tick); solo-led: JOIN_REQ kind go to its
    // leader, whose JOIN_AT(J) seals J alone with GO and LEAD_FROM(J + 1).
    // Round I1b's scenario hook (World:TestGoHook / TestHooks): a TEST event in
    // the member's stage group at `ahead` ticks (before a GO stamped there).
    std::uint32_t stamp_test(std::uint32_t member, w::TestHook hook, int argument, std::uint32_t ahead = 8) {
        auto& m = members[member];
        auto& inst = instances[m.inst];
        w::Event test{w::EventKind::test};
        test.slot = std::uint8_t(m.slot);
        test.hook = std::uint8_t(hook);
        test.argument = argument;
        const std::uint32_t j = inst.next_seal + ahead;
        inst.events[j].push_back(test);
        return j;
    }
    std::uint32_t stamp_go(std::uint32_t member, const std::string& stage, std::uint8_t point, std::uint32_t ahead = 8) {
        auto& m = members[member];
        auto& inst = instances[m.inst];
        w::Event go{w::EventKind::go};
        go.slot = std::uint8_t(m.slot);
        go.point = point;
        go.stage = stage;
        const std::uint32_t j = inst.next_seal + ahead;
        inst.events[j].push_back(go);
        log_lines.push_back("go " + m.name + " at " + std::to_string(j));
        return j;
    }
    void ask_go(std::uint32_t member, const std::string& stage, std::uint8_t point, double now) {
        auto& m = members[member];
        auto& inst = instances[m.inst];
        w::Event go{w::EventKind::go};
        go.slot = std::uint8_t(m.slot);
        go.point = point;
        go.stage = stage;
        inst.pending_go = go;
        w::JoinReq req{inst.id, inst.visit, std::uint8_t(m.slot), std::uint8_t(suggested_delay)};
        req.kind = w::JoinKind::go;
        req.stage = stage;
        req.point = point;
        send(member, w::Type::join_req, w::encode(req), now);
        log_lines.push_back("join_req go " + m.name);
    }
    std::vector<std::pair<std::string, w::Intent>> go_intents; // INTENT 4 as received (member name, message)
    std::vector<std::pair<std::string, w::State>> states;     // STATE as received
    std::vector<std::pair<std::string, w::Heading>> headings; // HEADING as received

    std::uint32_t stamp_leave(std::uint32_t member) {
        auto& m = members[member];
        auto& inst = instances[m.inst];
        const std::uint32_t j = inst.next_seal + 8;
        inst.events[j].push_back(w::Event{w::EventKind::leave, std::uint8_t(m.slot)});
        inst.leave_at[m.slot] = j;
        if (inst.slots.size() == 2) {
            const std::uint32_t lead = j + 1;
            w::Event e{w::EventKind::lead_from};
            e.tick = lead;
            inst.events[lead - 1].push_back(e);
            inst.lead_from = lead;
        }
        log_lines.push_back("leave " + m.name + " at " + std::to_string(j));
        return j;
    }

    // The states of the joins that waited for a checkpoint of this instance.
    void send_checkpoint_states(std::uint32_t inst_id, double now) {
        auto& inst = instances[inst_id];
        const auto& cp = stored_checkpoints.at(inst_id);
        if (cp.visit != inst.visit) return;
        for (const auto id : std::exchange(checkpoint_waiters[inst_id], {})) {
            auto& m = members[id];
            w::StateData state;
            state.purpose = w::StatePurpose::join;
            state.slot = std::uint8_t(m.observe_slot);
            state.snapshot = inst.snapshot;
            for (std::uint32_t t = 0; t < cp.load_end && inst.log.contains(t); ++t) state.load_ticks.push_back(inst.log.at(t));
            state.checkpoint_tick = cp.tick;
            state.checkpoint_hash = cp.hash;
            state.checkpoint = cp.bytes;
            state.first = cp.tick;
            for (std::uint32_t t = cp.tick; inst.log.contains(t); ++t) state.ticks.push_back(inst.log.at(t));
            for (const auto& c : w::make_chunks(inst.id, inst.visit, w::encode_state_body(state))) send(id, w::Type::state_data, w::encode(c), now);
            inst.observers.insert(id); // (LIVE from here, after the state, as the relay pumps)
            log_lines.push_back("checkpoint state for " + m.name + " from tick " + std::to_string(cp.tick) + " with " +
                                std::to_string(state.ticks.size()) + " ticks");
        }
    }

    // A fingerprint vote the member lost (6.9): RESYNC, then the state with
    // every tick since the snapshot; its pads are filled until it sends again.
    void resync(std::uint32_t member, double now) {
        auto& m = members[member];
        auto& inst = instances[m.inst];
        send(member, w::Type::resync, w::encode(w::InstVisit{inst.id, inst.visit}), now);
        w::StateData state;
        state.purpose = w::StatePurpose::resync;
        state.slot = std::uint8_t(m.slot);
        state.snapshot = inst.snapshot;
        state.first = 0;
        for (std::uint32_t t = 0; inst.log.contains(t); ++t) state.ticks.push_back(inst.log.at(t));
        for (const auto& c : w::make_chunks(inst.id, inst.visit, w::encode_state_body(state))) {
            // (A paced one lets the sealed ticks go first, as the relay builds the state outside its lock.)
            if (state_pacing > 0) paced_state.emplace_back(now + state_pacing, member, w::encode(c));
            else send(member, w::Type::state_data, w::encode(c), now);
        }
        m.resyncing = true;
        log_lines.push_back("resync " + m.name + " after " + std::to_string(state.ticks.size()) + " ticks");
    }
    std::uint32_t resync_ready = 0;

    void leave_instance_if_empty(Instance* inst) {
        if (!inst) return;
        for (auto it = inst->slots.begin(); it != inst->slots.end();) {
            const auto& m = members[it->second];
            it = m.inst != inst->id ? inst->slots.erase(it) : std::next(it);
        }
        if (inst->slots.empty()) inst->sealed = false;
    }

    void handle(std::uint32_t id, const w::Frame& f, double now) {
        auto& m = members[id];
        switch (f.type) {
        case w::Type::log: {
            const auto l = w::decode_log(f.payload);
            auto it = instances.find(m.inst); // routed by the member (after a fork the id may be the old one)
            if (it == instances.end()) break;
            auto& inst = it->second;
            std::vector<w::TickBody> fresh;
            std::uint32_t first = 0;
            for (std::size_t i = 0; i < l.pads.size(); ++i) {
                const std::uint32_t t = l.first + std::uint32_t(i);
                if (inst.log.contains(t)) continue;
                w::TickBody body;
                body.present = std::uint8_t(1u << m.slot);
                body.pads[std::size_t(m.slot)] = l.pads[i];
                inst.log[t] = body;
                if (fresh.empty()) first = t;
                fresh.push_back(body);
            }
            if (!fresh.empty())
                for (const auto observer : inst.observers) send_ticks(observer, w::Type::live, inst, first, fresh, now);
            break;
        }
        case w::Type::input: {
            const auto in = w::decode_input(f.payload);
            ++input_frames[id];
            input_pads[id] += in.pads.size();
            auto it = instances.find(m.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            for (std::size_t k = 0; k < in.pads.size(); ++k) {
                const std::uint32_t t = in.first + std::uint32_t(k);
                if (t < inst.next_seal && inst.sealed) {
                    ++late_pads;
                    continue;
                }
                inst.pads[t][m.slot] = in.pads[k];
            }
            break;
        }
        case w::Type::plan: {
            const auto plan = w::decode_plan(f.payload);
            plans.push_back(plan);
            if (drop_plans) break;
            w::Options o = prompt.value_or(w::Options{});
            o.plan = plan.plan;
            o.inst = o.visit = 0;
            o.stage = plan.stage;
            if (!prompt) o.flags = w::Options::nothing;
            send(id, w::Type::options, w::encode(o), now);
            break;
        }
        case w::Type::choice: {
            const auto c = w::decode_choice(f.payload);
            choices.push_back(c);
            if (c.plan != 0 || !m.parked || !prompt_held.contains(id)) break;
            prompt_held.erase(id);
            if (c.choice == w::ChoiceKind::back) {
                // *Back to the lobby*: a DISSOLVE redirected to the lobby (19.7.5).
                auto& hub = new_instance("stagml11", 1);
                hub.hub = true;
                hub.slots[0] = m.id;
                w::BarrierOk ok;
                ok.inst = m.parked->inst;
                ok.visit = m.parked->visit;
                ok.stage = m.parked->stage;
                ok.point = m.parked->point;
                ok.decision = w::Decision::dissolve;
                ok.new_inst = hub.id;
                ok.new_visit = 1;
                ok.own_slot = 0;
                ok.slot_map[std::size_t(m.slot)] = 0;
                ok.mode = w::Mode::hub;
                ok.redirect_stage = "stagml11";
                ok.redirect_point = c.back_point == 0xFF ? 3 : c.back_point;
                m.inst = hub.id;
                m.slot = 0;
                m.parked.reset();
                send(m.id, w::Type::barrier_ok, w::encode(ok), now);
                log_lines.push_back("prompt back: " + m.name);
                break;
            }
            decide(m, now);
            break;
        }
        case w::Type::barrier: {
            const auto b = w::decode_barrier(f.payload);
            m.parked = b;
            const bool from_hub = instances.contains(b.inst) && instances[b.inst].hub && !is_hub_stage(b.stage);
            if (from_hub) barrier_plans.push_back(b.plan);
            if (from_hub && prompt && b.plan == 0 && answer_barriers) {
                // The loading-screen form: held without a time limit, OPTIONS with it.
                m.parked_at = now;
                prompt_held.insert(id);
                send(id, w::Type::barrier_wait, w::encode(w::BarrierWait{b.inst, w::WaitReason::choice, 0, {}, 0}), now);
                w::Options o = *prompt;
                o.plan = 0;
                o.inst = b.inst;
                o.visit = b.visit;
                o.stage = b.stage;
                send(id, w::Type::options, w::encode(o), now);
                log_lines.push_back("prompt hold: " + m.name);
                break;
            }
            // The instance leaves its stage while joiners still catch up on it:
            // they go back to their own fallback as NEW (6.6).
            if (auto it = instances.find(b.inst); it != instances.end()) {
                const auto observers = it->second.observers;
                for (const auto obs : observers) {
                    auto& om = members[obs];
                    if (it->second.slots.contains(om.observe_slot) && it->second.slots.at(om.observe_slot) == obs) continue; // a member already
                    auto& own = new_instance(it->second.stage, 1);
                    own.slots[0] = obs;
                    w::BarrierOk ok;
                    ok.inst = it->second.id;
                    ok.visit = it->second.visit;
                    ok.decision = w::Decision::new_instance;
                    ok.new_inst = own.id;
                    ok.new_visit = 1;
                    ok.stage = it->second.stage;
                    ok.own_slot = 0;
                    ok.slot_map[std::size_t(om.old_slot)] = 0;
                    ok.mode = w::Mode::solo;
                    ok.notice = w::Notice::hold_expired;
                    send(obs, w::Type::barrier_ok, w::encode(ok), now);
                    om.inst = own.id;
                    om.slot = 0;
                    om.observing = 0;
                    it->second.observers.erase(obs);
                    log_lines.push_back("target left: " + om.name + " -> own " + own.stage);
                }
            }
            m.parked_at = now;
            if (auto it = instances.find(b.inst); it != instances.end()) {
                // The old visit ends at B: nothing is sealed after it.
                auto& inst = it->second;
                inst.sealed = false;
                inst.log.erase(inst.log.upper_bound(b.tick), inst.log.end());
            }
            if (!answer_barriers) break;
            // Hubs at once; fields wait a little for a partner (co-arrival).
            if (is_hub_stage(b.stage)) decide(m, now);
            else {
                bool partner = false;
                for (auto& [oid, other] : members)
                    if (oid != id && other.parked && other.parked->stage == b.stage) partner = true;
                bool target = false;
                for (auto& [iid, inst] : instances)
                    if (inst.stage == b.stage && iid != b.inst && !inst.hub && !inst.slots.empty() && !inst.snapshot.empty()) target = true;
                if (wait_hold <= 0 && (partner || target || co_arrival_hold <= 0)) decide(m, now);
            }
            break;
        }
        case w::Type::snapshot: {
            const auto c = w::decode_chunk(f.payload);
            auto it = instances.find(c.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            if (c.offset == 0) {
                inst.snapshot.clear();
                inst.log.clear();
            }
            inst.snapshot.insert(inst.snapshot.end(), c.bytes.begin(), c.bytes.end());
            if (inst.snapshot.size() == c.total) {
                if (const auto co = pending_co_arrival.find(inst.id); co != pending_co_arrival.end()) {
                    w::StateData state;
                    state.purpose = w::StatePurpose::co_arrival;
                    state.slot = std::uint8_t(members[co->second].slot);
                    state.snapshot = inst.snapshot;
                    const auto body = w::encode_state_body(state);
                    for (const auto& ch : w::make_chunks(inst.id, inst.visit, body)) send(co->second, w::Type::state_data, w::encode(ch), now);
                    pending_co_arrival.erase(co);
                }
            }
            break;
        }
        case w::Type::snaphash: {
            const auto h = w::decode_snaphash(f.payload);
            auto it = instances.find(h.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            inst.snaphash[id] = h.hash;
            for (const auto& [other, hash] : inst.snaphash)
                if (hash != h.hash) ++snaphash_mismatches;
            if (inst.slots.size() >= 2 && inst.snaphash.size() == inst.slots.size() && !inst.sealed) {
                // Every hash is in: S(0) after the largest round trip plus two ticks.
                inst.sealed = true;
                inst.s0 = now + 2 * latency + 2 * kFieldSeconds;
                inst.next_seal = 0;
                log_lines.push_back("sealed " + inst.stage + " from tick 0");
            }
            break;
        }
        case w::Type::ready: {
            const auto r = w::decode_ready(f.payload);
            auto it = instances.find(r.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            if (m.resyncing) {
                // A resync ends without a JOIN: the member sends its pads again.
                m.resyncing = false;
                ++resync_ready;
                log_lines.push_back("resynced " + m.name + " at tick " + std::to_string(r.tick));
                break;
            }
            if (refuse_ready > 0 && m.observing == r.inst) {
                --refuse_ready;
                ++ready_refused;
                w::StateData state;
                state.purpose = w::StatePurpose::join;
                state.slot = std::uint8_t(m.observe_slot);
                state.snapshot = inst.snapshot;
                state.first = 0;
                for (std::uint32_t t = 0; inst.log.contains(t); ++t) state.ticks.push_back(inst.log.at(t));
                for (const auto& c : w::make_chunks(inst.id, inst.visit, w::encode_state_body(state))) send(id, w::Type::state_data, w::encode(c), now);
                log_lines.push_back("ready of " + m.name + " at tick " + std::to_string(r.tick) + " refused: the state again");
                break;
            }
            // Compare with the members' fingerprint of that tick.
            const auto fp = inst.netfp.find(r.tick);
            readies.push_back({m.name, r.tick, r.fingerprint, (r.flags & w::Ready::from_checkpoint) != 0});
            if (fp != inst.netfp.end())
                for (const auto& [member, hash] : fp->second) {
                    ++netfp_compared;
                    if (hash != r.fingerprint) ++netfp_mismatches;
                }
            w::Event join{w::EventKind::join, std::uint8_t(m.observe_slot), 1, r.carry};
            if (!inst.sealed) {
                // Solo-led: the leader chooses J.
                inst.pending_joiner = id;
                const auto leader = inst.slots.begin()->second;
                send(leader, w::Type::join_req, w::encode(w::JoinReq{inst.id, inst.visit, std::uint8_t(m.observe_slot), std::uint8_t(suggested_delay)}), now);
            } else {
                const std::uint32_t j = inst.next_seal + 8;
                inst.events[j].push_back(join);
                inst.slots[m.observe_slot] = id;
                inst.observers.erase(id);
            }
            log_lines.push_back("ready " + m.name + " at tick " + std::to_string(r.tick) + " rate " + std::to_string(r.rate));
            break;
        }
        case w::Type::join_at: {
            ++join_ats;
            const auto j = w::decode_join_at(f.payload);
            auto it = instances.find(j.inst);
            if (it != instances.end() && it->second.pending_go) {
                auto& inst = it->second;
                inst.events[j.tick].push_back(*inst.pending_go);
                inst.pending_go.reset();
                w::Event lead{w::EventKind::lead_from};
                lead.tick = j.tick + 1;
                inst.events[j.tick].push_back(lead);
                inst.lead_from = j.tick + 1;
                inst.sealed = true;
                inst.next_seal = j.tick;
                const double logged = inst.log.empty() ? 0.0 : double(inst.log.rbegin()->first + 1);
                inst.s0 = now - double(j.tick) * kFieldSeconds + std::max(0.0, double(j.tick) - logged) * kFieldSeconds + latency;
                log_lines.push_back("go at " + std::to_string(j.tick));
                break;
            }
            if (it != instances.end() && it->second.pending_home) {
                auto& inst = it->second;
                w::Event home{w::EventKind::home};
                home.mask = inst.pending_home->first;
                home.reason = inst.pending_home->second;
                inst.pending_home.reset();
                inst.events[j.tick].push_back(home);
                w::Event lead{w::EventKind::lead_from};
                lead.tick = j.tick + 1;
                inst.events[j.tick].push_back(lead);
                inst.lead_from = j.tick + 1;
                inst.sealed = true;
                inst.next_seal = j.tick;
                const double logged = inst.log.empty() ? 0.0 : double(inst.log.rbegin()->first + 1);
                inst.s0 = now - double(j.tick) * kFieldSeconds + std::max(0.0, double(j.tick) - logged) * kFieldSeconds + latency;
                log_lines.push_back("home at " + std::to_string(j.tick));
                break;
            }
            if (it == instances.end() || !it->second.pending_joiner) break;
            auto& inst = it->second;
            const auto joiner = *inst.pending_joiner;
            inst.pending_joiner.reset();
            auto& jm = members[joiner];
            inst.events[j.tick].push_back(w::Event{w::EventKind::join, std::uint8_t(jm.observe_slot), 1, jm.carry});
            inst.slots[jm.observe_slot] = joiner; // stays an observer too: the leader's LOG until J comes as LIVE
            inst.sealed = true;
            inst.next_seal = j.tick;
            // S(J) when the leader reaches J: its log end plus the ticks to go.
            const double logged = inst.log.empty() ? 0.0 : double(inst.log.rbegin()->first + 1);
            const double ticks_to_go = double(j.tick) - logged;
            inst.s0 = now - double(j.tick) * kFieldSeconds + std::max(0.0, ticks_to_go) * kFieldSeconds + latency;
            log_lines.push_back("join_at " + std::to_string(j.tick));
            break;
        }
        case w::Type::netfp: {
            const auto n = w::decode_netfp(f.payload);
            netfps.emplace_back(m.name, n);
            auto it = instances.find(n.inst);
            if (it == instances.end()) break;
            auto& per = it->second.netfp[n.tick];
            for (const auto& [member, hash] : per)
                if (member != id) {
                    ++netfp_compared;
                    if (hash != n.hash) ++netfp_mismatches;
                }
            per[id] = n.hash;
            break;
        }
        case w::Type::split: {
            const auto s = w::decode_split(f.payload);
            ++splits;
            auto it = instances.find(s.inst);
            if (it == instances.end()) break; // the reporter moved to the child already
            auto& inst = it->second;
            if (!inst.splits.insert(s.tick).second) break; // the other side's report of the same fork
            // The leaver side becomes a child instance (REASSIGN) on the same
            // schedule; a side with one member leads alone after LEAD_FROM.
            auto& child = new_instance(inst.stage, inst.visit);
            child.snapshot = inst.snapshot;
            child.log.insert(inst.log.begin(), inst.log.end());
            child.sealed = inst.sealed;
            child.s0 = inst.s0;
            child.next_seal = inst.next_seal;
            for (int slot = 0; slot < 4; ++slot) {
                if (!(s.leavers & (1u << slot)) || !inst.slots.contains(slot)) continue;
                const auto member = inst.slots.at(slot);
                child.slots[slot] = member;
                inst.slots.erase(slot);
                for (auto& [t, pads] : inst.pads)
                    if (const auto p = pads.find(slot); p != pads.end()) {
                        child.pads[t][slot] = p->second;
                        pads.erase(p);
                    }
                child.last[slot] = inst.last[slot];
                members[member].inst = child.id;
                ++reassigns;
                send(member, w::Type::reassign,
                     w::encode(w::Reassign{inst.id, inst.visit, child.id, child.next_seal, child.slots.size() >= 2 ? w::Mode::sealed : w::Mode::solo, 1}),
                     now);
                log_lines.push_back("fork " + members[member].name + " -> " + std::to_string(child.id));
            }
            for (auto* side : {&inst, &child})
                if (side->sealed && side->slots.size() == 1) {
                    const std::uint32_t lead = side->next_seal + 1;
                    w::Event e{w::EventKind::lead_from};
                    e.tick = lead;
                    side->events[lead - 1].push_back(e);
                    side->lead_from = lead;
                }
            break;
        }
        case w::Type::resume: {
            const auto r = w::decode_resume(f.payload);
            auto it = instances.find(r.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            for (auto t = inst.log.upper_bound(r.last_tick); t != inst.log.end(); ++t) send_ticks(id, w::Type::sealed, inst, t->first, {t->second}, now);
            log_lines.push_back("resume " + m.name + " from " + std::to_string(r.last_tick + 1));
            break;
        }
        case w::Type::story: on_story(m, w::decode_story(f.payload), now); break;
        case w::Type::state: states.emplace_back(m.name, w::decode_state(f.payload)); break;
        case w::Type::heading: headings.emplace_back(m.name, w::decode_heading(f.payload)); break;
        case w::Type::intent: {
            const auto in = w::decode_intent(f.payload);
            if (in.kind == w::IntentKind::leave) ++leave_intents;
            if (in.kind == w::IntentKind::go_along) go_intents.emplace_back(m.name, in);
            break;
        }
        case w::Type::instance_op: instance_ops.emplace_back(id, w::decode_instance_op(f.payload)); break;
        case w::Type::report: reports.emplace_back(id, w::decode_report(f.payload)); break;
        case w::Type::statehash: statehashes.emplace_back(m.name, w::decode_statehash(f.payload)); break;
        case w::Type::checkpoint: {
            const auto c = w::decode_checkpoint(f.payload);
            if (c.offset == 0) uploads.emplace_back(m.name, c.tick);
            if (!checkpoint_joins) break;
            auto& u = running_uploads[c.inst];
            if (c.offset == 0) u = StoredUpload{c.visit, c.tick, c.load_end, c.total, c.hash, {}};
            if (u.tick != c.tick || u.bytes.size() != c.offset) break;
            u.bytes.insert(u.bytes.end(), c.bytes.begin(), c.bytes.end());
            if (u.bytes.size() < u.total) break;
            stored_checkpoints[c.inst] = std::move(u);
            running_uploads.erase(c.inst);
            send_checkpoint_states(c.inst, now);
            break;
        }
        case w::Type::presence: {
            ++presence_frames;
            auto p = w::decode_presence(f.payload, false);
            p.member = std::uint8_t(id);
            for (auto& [oid, other] : members)
                if (oid != id && other.inst != m.inst) send(oid, w::Type::presence_of, w::encode(p, true), now);
            break;
        }
        case w::Type::presence2: {
            // (An older hub refuses a frame type it does not know: counted.)
            if (!presence_v2) {
                ++unknown_frames;
                break;
            }
            ++presence2_frames;
            auto p = w::decode_presence2(f.payload, false);
            p.member = std::uint8_t(id);
            presence2_seen.push_back(p);
            for (auto& [oid, other] : members)
                if (oid != id && other.inst != m.inst) send(oid, w::Type::presence2_of, w::encode_presence2(p, true), now);
            break;
        }
        default: break;
        }
    }
};

} // namespace fake
