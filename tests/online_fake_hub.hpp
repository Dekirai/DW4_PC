#pragma once

// Test doubles for the DW4R2 client: a deterministic fake engine (its state
// is a hash of every pad it executed, stage loads, joins, leaves and forks)
// and an in-process fake hub that speaks docs/SEAMLESS.md 6.3 over memory
// pipes on a simulated clock: solo-led and sealed instances, server-paced
// sealing at 59.94 Hz with immediate fill, barrier decisions (DISSOLVE / NEW
// for hubs, co-arrival, JOIN by catch-up, NEW), JOIN_REQ / JOIN_AT, state
// delivery with LIVE ticks for observers, SPLIT and REKEY, RESUME, PING with
// a suggested delay, NETFP collection.
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

class Engine final : public EngineLink {
public:
    std::vector<std::string> route{"stagis10", "stagml11", "stag0101", "stag0102", "dng101b1"};
    std::string stage_ = "stagis10";
    std::uint64_t serial_ = 1, state_ = 7, frame_ = 0;
    std::uint8_t present_ = 1;
    std::optional<std::string> pending_;
    int transition_left_ = 0;
    std::optional<w::BarrierOk> barrier_;
    std::optional<SplitInfo> split_;
    std::vector<std::uint8_t> load_snapshot_;
    int identity_ = 0;
    bool replay_ = false;
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
    bool membership_safe() const override { return !pending_; }
    bool joinable(w::NotJoinable& reason) const override {
        if (is_hub_stage(stage_)) {
            reason = w::NotJoinable::hub;
            return false;
        }
        if (pending_) {
            reason = w::NotJoinable::loading;
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
    std::uint8_t alive_mask() const override { return present_; }
    std::vector<std::uint8_t> carry(int slot) const override { return {std::uint8_t(slot), 0xC0, 0xDE}; }
    std::optional<SplitInfo> take_split() override { return std::exchange(split_, std::nullopt); }
    PresenceFacts presence(int slot) const override {
        PresenceFacts f;
        f.in_stage = !pending_;
        f.x = float(state_ & 0xFF);
        f.z = float(slot);
        f.level = 3;
        f.hp = f.max_hp = 50;
        return f;
    }

    void step(const std::array<dw4::PadSample, 4>& pads) {
        ++frame_;
        if (pending_) {
            if (--transition_left_ > 0) return;
            // The load step: the barrier's membership, the new stage, the snapshot.
            stage_ = *pending_;
            pending_.reset();
            ++serial_;
            frame_ = 0;
            if (barrier_) {
                std::uint8_t present = 0;
                for (int s = 0; s < 4; ++s)
                    if (barrier_->present & (1u << s)) present |= std::uint8_t(1u << s);
                present_ = present;
                for (const auto& c : barrier_->co_arrivals) state_ = mix(state_, 0x5000 + c.slot + c.bytes.size());
                barrier_.reset();
            }
            state_ = mix(state_, serial_);
            take_load_snapshot();
            return;
        }
        for (int s = 0; s < 4; ++s) {
            const auto& p = pads[std::size_t(s)];
            const bool here = (present_ & (1u << s)) != 0;
            if (!here || !p.connected) continue; // absent slots play the neutral pad (6.5)
            state_ = mix(state_, std::uint64_t(s) << 40 | std::uint64_t(p.buttons) << 8 | std::uint64_t(std::uint8_t(std::int8_t(p.left.x * 74.0f))));
        }
        const int lead = present_ ? std::countr_zero(unsigned(present_)) : 0;
        if (pads[std::size_t(lead)].connected && (pads[std::size_t(lead)].buttons & kExitButton)) {
            const auto it = std::find(route.begin(), route.end(), stage_);
            if (it != route.end() && it + 1 != route.end()) {
                pending_ = *(it + 1);
                transition_left_ = 3;
                return;
            }
        }
        if (std::popcount(unsigned(present_)) >= 2)
            for (int s = 0; s < 4; ++s)
                if ((present_ & (1u << s)) && pads[std::size_t(s)].connected && (pads[std::size_t(s)].buttons & kForkButton)) {
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
        take_load_snapshot();
        ++restores;
        return true;
    }
    void set_replay(bool on) override { replay_ = on; }
    void start_online(int slot, std::uint8_t) override { identity_ = slot; }
    void set_split_identity(int slot) override { identity_ = slot; }
    void set_fork_sides(std::span<const w::Fork>) override {}
    void set_barrier(const w::BarrierOk& ok) override { barrier_ = ok; }
    void join_player(int slot, std::span<const std::uint8_t> carry, int point) override {
        present_ |= std::uint8_t(1u << slot);
        state_ = mix(mix(state_, 0x1000 + std::uint64_t(slot) + std::uint64_t(point)), carry.size());
    }
    void remove_player(int slot) override {
        present_ &= std::uint8_t(~(1u << slot));
        state_ = mix(state_, 0x2000 + std::uint64_t(slot));
    }
    void intent(int slot, w::IntentKind kind, int argument) override { state_ = mix(state_, 0x3000 + slot * 16 + int(kind) + argument); }
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
        bool observer_of = false;
        std::uint32_t observing = 0; // instance being caught up on
        int observe_slot = 0;
        std::vector<std::uint8_t> carry;
        std::optional<w::Barrier> parked;
        double parked_at = 0;
    };
    struct Instance {
        std::uint32_t id = 0, visit = 0;
        std::string stage;
        std::map<int, std::uint32_t> slots; // slot -> member
        bool sealed = false;
        std::vector<std::uint8_t> snapshot;
        std::map<std::uint32_t, w::TickBody> log; // ticks since the snapshot
        double s0 = 0;                      // S(t) = s0 + t * field
        std::uint32_t next_seal = 0;
        std::map<std::uint32_t, std::map<int, w::WirePad>> pads; // tick -> slot -> pad
        std::map<int, w::WirePad> last;
        std::map<int, int> fills;
        std::map<std::uint32_t, std::vector<w::Event>> events; // tick -> events
        std::set<std::uint32_t> observers;
        std::optional<std::uint32_t> pending_joiner;
        std::map<std::uint32_t, std::map<std::uint32_t, std::uint64_t>> netfp; // tick -> member -> hash
        std::map<std::uint32_t, std::uint64_t> snaphash; // member -> hash
        std::uint8_t present() const {
            std::uint8_t m = 0;
            for (const auto& [s, _] : slots) m |= std::uint8_t(1u << s);
            return m;
        }
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
    double co_arrival_hold = 3.0;
    std::vector<std::string> log_lines;
    std::map<std::uint32_t, Member> members;
    std::map<std::uint32_t, Instance> instances;
    std::vector<std::unique_ptr<Conn>> conns;
    std::uint32_t next_inst = 100;
    std::uint64_t netfp_compared = 0, netfp_mismatches = 0, late_pads = 0, fills_total = 0, splits = 0, snaphash_mismatches = 0;
    double last_ping = 0;
    std::uint32_t ping_nonce = 0;

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

    void tick(double now) {
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
                    const std::string answer = hello ? "OK " + std::to_string(c->member) + " 1000\n" : "ERR bad_hello\n";
                    c->outbound.emplace_back(now + latency, std::vector<std::uint8_t>(answer.begin(), answer.end()));
                    c->hello = hello.has_value();
                    for (auto& [id, other] : members)
                        if (id != c->member) {
                            send(id, w::Type::room, w::encode(w::Room{w::RoomEvent::joined, c->member, members[c->member].name, 0}), now);
                            send(c->member, w::Type::room, w::encode(w::Room{w::RoomEvent::joined, id, other.name, 0}), now);
                        }
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
        // Co-arrival holds that ran out: NEW.
        for (auto& [id, m] : members)
            if (m.parked && answer_barriers && now - m.parked_at > co_arrival_hold) decide(m, now, true);
    }

    void seal(Instance& inst, double now) {
        if (!inst.sealed) return;
        while (inst.s0 + double(inst.next_seal) * kFieldSeconds <= now) {
            const std::uint32_t t = inst.next_seal++;
            w::TickBody body;
            body.present = inst.present();
            for (const auto& [slot, member] : inst.slots) {
                auto& pads = inst.pads[t];
                if (const auto p = pads.find(slot); p != pads.end()) {
                    body.pads[std::size_t(slot)] = p->second;
                    inst.last[slot] = p->second;
                    inst.fills[slot] = 0;
                } else {
                    // Filled at once: the last sealed pad (six ticks), then neutral.
                    auto pad = ++inst.fills[slot] <= 6 ? inst.last[slot] : w::WirePad{};
                    pad.flags = 1;
                    body.pads[std::size_t(slot)] = pad;
                    ++fills_total;
                }
            }
            inst.pads.erase(t);
            if (const auto e = inst.events.find(t); e != inst.events.end()) {
                body.events = e->second;
                inst.events.erase(e);
            }
            inst.log[t] = body;
            const auto payload = w::encode(w::Sealed{inst.id, inst.visit, t, body});
            std::set<std::uint32_t> to(inst.observers.begin(), inst.observers.end());
            for (const auto& [slot, member] : inst.slots) to.insert(member);
            for (const auto member : to) send(member, w::Type::sealed, payload, now);
        }
    }

    Instance& new_instance(const std::string& stage, std::uint32_t visit) {
        auto& inst = instances[next_inst];
        inst.id = next_inst++;
        inst.visit = visit;
        inst.stage = stage;
        return inst;
    }

    void decide(Member& m, double now, bool hold_over) {
        const auto b = *m.parked;
        auto* from = instances.contains(b.inst) ? &instances[b.inst] : nullptr;
        w::BarrierOk ok;
        ok.inst = b.inst;
        ok.visit = b.visit;
        ok.rule_flags = 1;
        // Joinable instance of the target stage?
        Instance* target = nullptr;
        if (!is_hub_stage(b.stage))
            for (auto& [id, inst] : instances)
                if (inst.stage == b.stage && id != b.inst && !inst.slots.empty() && inst.slots.size() < 4 && !inst.snapshot.empty()) target = &inst;
        // Another member parked for the same stage: co-arrival (the first parked is the base).
        Member* partner = nullptr;
        if (!is_hub_stage(b.stage))
            for (auto& [id, other] : members)
                if (id != m.id && other.parked && other.parked->stage == b.stage) partner = &other;
        if (partner && !target) {
            Member& base = partner->parked_at <= m.parked_at ? *partner : m;
            Member& late = &base == &m ? *partner : m;
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
            base_ok.slot_map = {0, -1, -1, -1};
            base_ok.slot_members = {base.id, late.id, 0, 0};
            base_ok.co_arrivals = {{1, late.parked->carries.empty() ? std::vector<std::uint8_t>{} : late.parked->carries[0].bytes}};
            base_ok.present = 0b11;
            w::BarrierOk late_ok = base_ok;
            late_ok.inst = late.parked->inst;
            late_ok.visit = late.parked->visit;
            late_ok.decision = w::Decision::co_arrive;
            late_ok.own_slot = 1;
            late_ok.co_arrivals.clear();
            base.inst = late.inst = inst.id;
            base.slot = 0;
            late.slot = 1;
            base.parked.reset();
            late.parked.reset();
            late.observing = inst.id; // gets the base's snapshot once it is uploaded
            pending_co_arrival[inst.id] = late.id;
            send(base.id, w::Type::barrier_ok, w::encode(base_ok), now);
            send(late.id, w::Type::barrier_ok, w::encode(late_ok), now);
            log_lines.push_back("co-arrival " + base.name + " + " + late.name + " in " + b.stage);
            leave_instance_if_empty(from);
            return;
        }
        if (target) {
            int slot = 0;
            while (target->slots.contains(slot)) ++slot;
            ok.decision = w::Decision::join;
            ok.new_inst = target->id;
            ok.new_visit = target->visit;
            ok.own_slot = std::uint8_t(slot);
            ok.present = target->present();
            for (const auto& [s, id] : target->slots) ok.slot_members[std::size_t(s)] = id;
            m.inst = target->id;
            m.slot = slot;
            m.observing = target->id;
            m.observe_slot = slot;
            m.carry = b.carries.empty() ? std::vector<std::uint8_t>{} : b.carries[0].bytes;
            m.parked.reset();
            send(m.id, w::Type::barrier_ok, w::encode(ok), now);
            // The state: snapshot and every tick since; LIVE / SEALED keep coming.
            w::StateData state;
            state.snapshot = target->snapshot;
            state.first = 0;
            for (std::uint32_t t = 0; target->log.contains(t); ++t) state.ticks.push_back(target->log.at(t));
            const auto body = w::encode_state_body(state);
            for (const auto& c : w::make_chunks(target->id, target->visit, std::uint8_t(slot), body)) send(m.id, w::Type::state_data, w::encode(c, true), now);
            target->observers.insert(m.id);
            log_lines.push_back("join " + m.name + " -> " + target->stage + " slot " + std::to_string(slot) + " after " +
                                std::to_string(target->log.size()) + " ticks");
            leave_instance_if_empty(from);
            return;
        }
        auto& inst = new_instance(b.stage, 1);
        inst.slots[0] = m.id;
        ok.decision = w::Decision::new_instance;
        ok.new_inst = inst.id;
        ok.new_visit = 1;
        ok.own_slot = 0;
        ok.slot_map = {-1, -1, -1, -1};
        ok.slot_map[std::size_t(m.slot)] = 0;
        ok.slot_members = {m.id, 0, 0, 0};
        ok.present = 1;
        m.inst = inst.id;
        m.slot = 0;
        m.parked.reset();
        send(m.id, w::Type::barrier_ok, w::encode(ok), now);
        log_lines.push_back("new " + m.name + " " + b.stage);
        leave_instance_if_empty(from);
    }
    std::map<std::uint32_t, std::uint32_t> pending_co_arrival; // instance -> late member

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
            w::Live live{inst.id, inst.visit, l.first, {}};
            for (std::size_t i = 0; i < l.pads.size(); ++i) {
                const std::uint32_t t = l.first + std::uint32_t(i);
                if (inst.log.contains(t)) continue;
                w::TickBody body;
                body.present = std::uint8_t(1u << m.slot);
                body.pads[std::size_t(m.slot)] = l.pads[i];
                inst.log[t] = body;
                if (live.ticks.empty()) live.first = t;
                live.ticks.push_back(body);
            }
            if (!live.ticks.empty())
                for (const auto observer : inst.observers) send(observer, w::Type::live, w::encode(live), now);
            break;
        }
        case w::Type::input: {
            const auto in = w::decode_input(f.payload);
            auto it = instances.find(m.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            if (in.tick < inst.next_seal && inst.sealed) {
                ++late_pads;
                break;
            }
            inst.pads[in.tick][m.slot] = in.pad;
            break;
        }
        case w::Type::barrier: {
            const auto b = w::decode_barrier(f.payload);
            m.parked = b;
            m.parked_at = now;
            if (auto it = instances.find(b.inst); it != instances.end()) {
                // The old visit ends at B: nothing is sealed after it.
                auto& inst = it->second;
                inst.sealed = false;
                inst.log.erase(inst.log.upper_bound(b.tick), inst.log.end());
            }
            if (!answer_barriers) break;
            // Hubs at once; fields wait a little for a partner (co-arrival).
            if (is_hub_stage(b.stage)) decide(m, now, false);
            else {
                bool partner = false;
                for (auto& [oid, other] : members)
                    if (oid != id && other.parked && other.parked->stage == b.stage) partner = true;
                bool target = false;
                for (auto& [iid, inst] : instances)
                    if (inst.stage == b.stage && iid != b.inst && !inst.slots.empty() && !inst.snapshot.empty()) target = true;
                if (partner || target || co_arrival_hold <= 0) decide(m, now, false);
            }
            break;
        }
        case w::Type::snapshot: {
            const auto c = w::decode_chunk(f.payload, false);
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
                    state.snapshot = inst.snapshot;
                    const auto body = w::encode_state_body(state);
                    for (const auto& ch : w::make_chunks(inst.id, inst.visit, 1, body)) send(co->second, w::Type::state_data, w::encode(ch, true), now);
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
            // Compare with the members' fingerprint of that tick.
            if (const auto fp = inst.netfp.find(r.tick); fp != inst.netfp.end())
                for (const auto& [member, hash] : fp->second) {
                    ++netfp_compared;
                    if (hash != r.fingerprint) ++netfp_mismatches;
                }
            w::Event join{w::EventKind::join, std::uint8_t(m.observe_slot), id, 1, r.carry};
            if (!inst.sealed) {
                // Solo-led: the leader chooses J.
                inst.pending_joiner = id;
                const auto leader = inst.slots.begin()->second;
                send(leader, w::Type::join_req, w::encode(w::JoinReq{inst.id, inst.visit, std::uint8_t(m.observe_slot)}), now);
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
            const auto j = w::decode_join_at(f.payload);
            auto it = instances.find(j.inst);
            if (it == instances.end() || !it->second.pending_joiner) break;
            auto& inst = it->second;
            const auto joiner = *inst.pending_joiner;
            inst.pending_joiner.reset();
            auto& jm = members[joiner];
            inst.events[j.tick].push_back(w::Event{w::EventKind::join, std::uint8_t(jm.observe_slot), joiner, 1, jm.carry});
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
            if (it == instances.end()) break;
            auto& inst = it->second;
            // The leavers become a child instance (REKEY); the stayers keep the id.
            if (m.slot >= 0 && (s.leavers & (1u << m.slot))) {
                auto& child = new_instance(inst.stage, inst.visit);
                child.slots[m.slot] = id;
                child.snapshot = inst.snapshot;
                child.log.insert(inst.log.begin(), inst.log.upper_bound(s.tick));
                inst.slots.erase(m.slot);
                m.inst = child.id;
                send(id, w::Type::rekey, w::encode(w::Rekey{inst.id, inst.visit, child.id, child.visit}), now);
                log_lines.push_back("fork " + m.name + " -> " + std::to_string(child.id));
            }
            if (inst.slots.size() <= 1) inst.sealed = false;
            break;
        }
        case w::Type::resume: {
            const auto r = w::decode_resume(f.payload);
            auto it = instances.find(r.inst);
            if (it == instances.end()) break;
            auto& inst = it->second;
            for (auto t = inst.log.upper_bound(r.last_tick); t != inst.log.end(); ++t)
                send(id, w::Type::sealed, w::encode(w::Sealed{inst.id, inst.visit, t->first, t->second}), now);
            log_lines.push_back("resume " + m.name + " from " + std::to_string(r.last_tick + 1));
            break;
        }
        case w::Type::presence: {
            auto p = w::decode_presence(f.payload, false);
            p.member = id;
            for (auto& [oid, other] : members)
                if (oid != id && other.inst != m.inst) send(oid, w::Type::presence_of, w::encode(p, true), now);
            break;
        }
        default: break;
        }
    }
};

} // namespace fake
