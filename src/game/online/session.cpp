#include "session.hpp"

#include "pad_wire.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>

namespace dw4::game::online {

namespace w = wire;

const char* mode_name(Mode mode) noexcept {
    switch (mode) {
    case Mode::connecting: return "connecting";
    case Mode::solo: return "solo";
    case Mode::sealed: return "sealed";
    case Mode::parked: return "parked";
    case Mode::transition: return "transition";
    case Mode::waiting_state: return "waiting_state";
    case Mode::catching_up: return "catching_up";
    case Mode::observing: return "observing";
    case Mode::lost: return "lost";
    case Mode::closed: return "closed";
    }
    return "?";
}

bool is_hub_stage(std::string_view stage) {
    auto lower = [](std::string_view s) {
        std::string out(s);
        for (auto& c : out) c = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
        return out;
    };
    const auto s = lower(stage);
    if (s.rfind("stagis", 0) == 0) return true;
    return s == "stagml02" || s == "stagml11" || s == "stagml12" || s == "stagml13" || s == "stagml14";
}

namespace {
int members(std::uint8_t mask) { return std::popcount(unsigned(mask)); }
int lowest(std::uint8_t mask) { return mask ? std::countr_zero(unsigned(mask)) : -1; }
double clock_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
} // namespace

Session::Session(EngineLink& engine, SessionConfig config) : engine_(engine), config_(std::move(config)) {}

void Session::send(w::Type type, const std::vector<std::uint8_t>& payload) {
    if (!transport_ || !hello_done_) return;
    const auto frame = w::encode_frame(type, payload);
    if (!transport_->send(frame)) lose_connection(now_);
}

void Session::notice(std::string text) { notices_.push_back(std::move(text)); }

std::string Session::member_name(std::uint32_t member) const {
    if (const auto it = names_.find(member); it != names_.end() && !it->second.empty()) return it->second;
    return member ? "Player " + std::to_string(member) : std::string("A player");
}

std::string Session::slot_name(int slot) const {
    return slot >= 0 && slot < 4 ? member_name(slot_members_[std::size_t(slot)]) : std::string("A player");
}

// ---- connection
void Session::attach(std::unique_ptr<Transport> transport, double now, std::string ticket) {
    now_ = now;
    if (!ticket.empty()) config_.ticket = ticket;
    transport_ = std::move(transport);
    reader_ = {};
    inbox_.clear();
    line_.clear();
    hello_done_ = false;
    const auto hello = w::hello_line(config_.ticket, config_.protocol, config_.data_hash, config_.settings_hash);
    if (!transport_->send(std::span(reinterpret_cast<const std::uint8_t*>(hello.data()), hello.size()))) lose_connection(now);
}

void Session::lose_connection(double now) {
    if (transport_) transport_->close();
    transport_.reset();
    hello_done_ = false;
    if (mode_ == Mode::closed || mode_ == Mode::lost) return;
    if (mode_ == Mode::connecting) {
        mode_ = Mode::closed;
        close_reason_ = "The relay did not answer.";
        return;
    }
    // A solo member plays on (its LOG waits); everyone else freezes.
    resume_after_lost_ = true;
    if (mode_ == Mode::solo || mode_ == Mode::transition) {
        notice("Connection lost, reconnecting...");
        return;
    }
    mode_before_lost_ = mode_;
    mode_ = Mode::lost;
    last_tick_at_ = now;
}

void Session::leave(const std::string& reason) {
    flush_log(now_, true);
    send(w::Type::bye, w::encode(w::Reason{reason}));
    if (transport_) transport_->close();
    transport_.reset();
    mode_ = Mode::closed;
    close_reason_ = "You left the room.";
}

void Session::poll(double now) {
    now_ = now;
    if (transport_) {
        inbox_.clear();
        const bool open = transport_->receive(inbox_);
        std::size_t used = 0;
        if (!hello_done_) {
            while (used < inbox_.size() && !hello_done_) {
                const char c = char(inbox_[used++]);
                if (c != '\n') {
                    line_ += c;
                    if (line_.size() > 256) {
                        lose_connection(now);
                        return;
                    }
                    continue;
                }
                const auto answer = w::parse_hello_answer(line_);
                line_.clear();
                if (!answer || !answer->ok) {
                    const std::string code = answer ? answer->error : std::string("bad_answer");
                    if (transport_) transport_->close();
                    transport_.reset();
                    mode_ = Mode::closed;
                    close_reason_ = "The relay refused the connection (" + code + ").";
                    return;
                }
                hello_done_ = true;
                member_id_ = answer->member_id;
                if (!config_.own_name.empty()) names_[member_id_] = config_.own_name;
                if (resume_after_lost_) {
                    resume_after_lost_ = false;
                    send(w::Type::resume, w::encode(w::Resume{inst_, visit_, std::uint8_t(slot_), tick_ ? tick_ - 1 : 0}));
                    if (mode_ == Mode::lost) mode_ = mode_before_lost_;
                    last_tick_at_ = now;
                    flush_log(now, true);
                } else if (mode_ == Mode::connecting) {
                    mode_ = Mode::solo;
                    hub_ = is_hub_stage(engine_.stage());
                    log_first_ = tick_;
                    send(w::Type::rate, w::encode(config_.rate));
                }
            }
        }
        if (hello_done_ && used < inbox_.size()) {
            try {
                reader_.feed(std::span(inbox_).subspan(used));
                while (auto frame = reader_.next()) {
                    handle(*frame, now);
                    if (!transport_) break;
                }
            } catch (const w::WireError&) {
                lose_connection(now);
                return;
            }
        }
        if (!open && transport_) lose_connection(now);
    }
    if (mode_ == Mode::catching_up) run_catch_up(now);
    // A solo group parked without an answer goes on as NEW (3.4); a larger
    // group keeps waiting.
    if (mode_ == Mode::parked && members(group_at_barrier_) == 1 && now - parked_at_ > config_.barrier_timeout) {
        w::BarrierOk ok;
        ok.inst = inst_;
        ok.visit = visit_;
        ok.decision = w::Decision::new_instance;
        ok.new_inst = 0; // not registered: never offered to joiners
        ok.new_visit = visit_ + 1;
        ok.own_slot = 0;
        ok.slot_map = {-1, -1, -1, -1};
        ok.slot_map[std::size_t(slot_)] = 0;
        ok.slot_members = {member_id_, 0, 0, 0};
        ok.rule_flags = 1;
        ok.present = 1;
        notice("The server did not answer; you continue on your own.");
        on_barrier_ok(ok, now);
    }
    if (mode_ == Mode::solo || mode_ == Mode::sealed) {
        flush_log(now, false);
        if (state_pending_) report_state(now, false);
        if (now - presence_at_ >= config_.presence_interval) {
            presence_at_ = now;
            const auto facts = engine_.presence(slot_);
            if (facts.in_stage) {
                w::Presence p;
                p.stage = engine_.stage();
                p.inst = inst_;
                p.x = facts.x;
                p.y = facts.y;
                p.z = facts.z;
                p.yaw = facts.yaw;
                p.species = std::uint8_t(std::clamp(facts.species, 0, 255));
                p.level = std::uint16_t(std::clamp(facts.level, 0, 65535));
                p.hp = std::uint16_t(std::clamp(facts.hp, 0, 65535));
                p.max_hp = std::uint16_t(std::clamp(facts.max_hp, 0, 65535));
                p.mp = std::uint16_t(std::clamp(facts.mp, 0, 65535));
                p.max_mp = std::uint16_t(std::clamp(facts.max_mp, 0, 65535));
                p.flags = std::uint8_t((facts.defeated ? w::presence_flag::defeated : 0) | (facts.hidden ? w::presence_flag::hidden : 0));
                send(w::Type::presence, w::encode(p, false));
            }
        }
    }
}

void Session::handle(const w::Frame& frame, double now) {
    const auto& p = frame.payload;
    switch (frame.type) {
    case w::Type::sealed: {
        auto m = w::decode_sealed(p);
        on_sealed(m.inst, m.visit, m.tick, std::move(m.body), now);
        break;
    }
    case w::Type::live: {
        auto m = w::decode_live(p);
        for (std::size_t i = 0; i < m.ticks.size(); ++i) on_sealed(m.inst, m.visit, m.first + std::uint32_t(i), std::move(m.ticks[i]), now);
        break;
    }
    case w::Type::join_req: {
        const auto m = w::decode_join_req(p);
        if (mode_ != Mode::solo || m.inst != inst_ || m.visit != visit_ || hub_ || join_at_) break;
        // 1 -> 2 (6.4): J = the current tick + d; delayed sampling at once,
        // the last pad repeated for the ticks before J without a sample.
        const std::uint32_t j = tick_ + std::uint32_t(std::max(1, delay_));
        join_at_ = j;
        delayed_.assign(std::size_t(j - tick_), last_own_);
        input_next_ = j;
        send(w::Type::join_at, w::encode(w::JoinAt{inst_, visit_, j}));
        break;
    }
    case w::Type::barrier_ok: on_barrier_ok(w::decode_barrier_ok(p), now); break;
    case w::Type::barrier_wait: {
        const auto m = w::decode_barrier_wait(p);
        std::string names;
        for (std::size_t i = 0; i < m.names.size(); ++i) names += (i ? (i + 1 == m.names.size() ? " and " : ", ") : "") + m.names[i];
        if (names.empty()) names = "the others";
        boss_choice_ = m.reason == w::WaitReason::boss_lock;
        loading_ = m.reason == w::WaitReason::co_arrival    ? "Waiting for " + names + "..."
                   : m.reason == w::WaitReason::entry_scene ? "Joining " + names + "..."
                                                            : names + " fight a boss. Wait for them, or go alone?";
        break;
    }
    case w::Type::state_data: {
        const auto chunk = w::decode_chunk(p, true);
        if (mode_ == Mode::solo || mode_ == Mode::sealed || mode_ == Mode::connecting) break;
        if (chunk.inst != inst_ || chunk.visit != visit_) break;
        if (auto body = assembler_.add(chunk, config_.state_limit)) {
            auto state = w::decode_state_body(*body);
            state.inst = chunk.inst;
            state.visit = chunk.visit;
            state.slot = chunk.slot;
            // Before the own load step ran (parked / transition) it waits.
            if (mode_ == Mode::parked || mode_ == Mode::transition) stashed_state_ = std::move(state);
            else on_state(std::move(state), now);
        } else {
            loading_ = "Joining... (receiving " + std::to_string(assembler_.received() / 1024) + " of " +
                       std::to_string(assembler_.total() / 1024) + " KB)";
        }
        break;
    }
    case w::Type::resync: {
        const auto m = w::decode_inst_visit(p);
        if (m.inst != inst_ || m.visit != visit_) break;
        notice("Lost sync with the group - catching up again...");
        fallback_ = engine_.snapshot_bytes(false);
        own_carry_ = engine_.carry(slot_);
        mode_ = Mode::waiting_state;
        assembler_.reset();
        break;
    }
    case w::Type::presence_of: {
        auto m = w::decode_presence(p, true);
        if (m.member == member_id_) break;
        auto& r = remote_[m.member];
        r.name = member_name(m.member);
        tracks_.add(m, r.name, now);
        r.presence = std::move(m);
        r.received_at = now;
        break;
    }
    case w::Type::room: {
        const auto m = w::decode_room(p);
        if (m.kind == w::RoomEvent::joined) {
            if (!m.name.empty()) names_[m.member] = m.name;
            if (m.member != member_id_) notice(member_name(m.member) + " joined the room");
        } else if (m.kind == w::RoomEvent::left) {
            if (m.member != member_id_) notice(member_name(m.member) + " left the room");
            remote_.erase(m.member);
            tracks_.forget(m.member);
        } else if (m.kind == w::RoomEvent::owner_present) {
            notice(m.value ? "The room owner is back" : "The room owner is away");
        }
        break;
    }
    case w::Type::ping: {
        const auto m = w::decode_ping(p);
        if (m.delay >= 1 && m.delay <= 10 && mode_ != Mode::sealed) delay_ = m.delay; // sealed: DELAY events change it
        const auto ms = std::uint64_t(clock_seconds() * 1000.0);
        send(w::Type::pong, w::encode(w::Pong{m.nonce, ms}));
        break;
    }
    case w::Type::rekey: {
        const auto m = w::decode_rekey(p);
        if (m.inst == inst_ && m.visit == visit_) {
            inst_ = m.new_inst;
            visit_ = m.new_visit;
        }
        break;
    }
    case w::Type::kick:
    case w::Type::close: {
        const auto m = w::decode_reason(p);
        flush_log(now, true);
        if (transport_) transport_->close();
        transport_.reset();
        mode_ = Mode::closed;
        close_reason_ = frame.type == w::Type::kick ? "You were removed from the room (" + m.reason + ")."
                                                    : "The room was closed (" + m.reason + ").";
        break;
    }
    case w::Type::story_ack: break; // round S2
    default: break;                  // unknown frames from a newer server are skipped
    }
}

void Session::on_sealed(std::uint32_t inst, std::uint32_t visit, std::uint32_t tick, w::TickBody body, double now) {
    if (inst != inst_ || visit != visit_) return;
    if (tick < tick_) return; // executed already, or after the barrier of a visit
    if (mode_ == Mode::parked) return; // ticks sealed after B are discarded (after BARRIER_OK inst / visit are the new ones)
    ticks_[tick] = std::move(body);
    last_tick_at_ = now;
}

// ---- steps
int Session::steps_allowed(int host_steps, double elapsed) {
    switch (mode_) {
    case Mode::solo:
    case Mode::transition: return host_steps;
    case Mode::sealed:
    case Mode::observing: {
        // Only consecutive ticks from the next one count as waiting.
        std::size_t waiting = 0;
        for (auto it = ticks_.find(tick_); it != ticks_.end() && it->first == tick_ + waiting; ++it) ++waiting;
        return pace_.due_steps(elapsed, waiting);
    }
    default: return 0;
    }
}

std::array<PadSample, 4> Session::pads_of(const w::TickBody& body) const {
    std::array<PadSample, 4> pads{};
    for (int s = 0; s < 4; ++s) {
        const bool here = (body.present & (1u << s)) != 0;
        pads[std::size_t(s)] = dequantize_pad(body.pads[std::size_t(s)], here);
    }
    return pads;
}

bool Session::apply_events(bool replaying) {
    bool changed = false;
    while (!events_.empty() && events_.front().tick <= tick_) {
        const auto& e = events_.front().event;
        bool allowed = true;
        w::NotJoinable reason{};
        if (e.kind == w::EventKind::join) allowed = engine_.joinable(reason);
        else if (e.kind == w::EventKind::leave || e.kind == w::EventKind::intent) allowed = engine_.membership_safe();
        if (!allowed) break; // waits for a step the instance allows it (6.5)
        const int slot = e.slot;
        switch (e.kind) {
        case w::EventKind::join:
            engine_.join_player(slot, e.carry, e.point);
            present_ = std::uint8_t(present_ | (1u << slot));
            slot_members_[std::size_t(slot)] = e.member;
            if (!replaying && e.member != member_id_) notice(member_name(e.member) + " joined");
            if (slot == slot_ && mode_ == Mode::observing) become_member(now_);
            break;
        case w::EventKind::leave:
            if (!replaying && slot != slot_) notice(slot_name(slot) + " left");
            engine_.remove_player(slot);
            present_ = std::uint8_t(present_ & ~(1u << slot));
            slot_members_[std::size_t(slot)] = 0;
            break;
        case w::EventKind::intent: engine_.intent(slot, w::IntentKind(e.intent_kind), e.argument); break;
        case w::EventKind::lead_from: lead_from_ = e.tick; break;
        case w::EventKind::delay:
            if (slot == slot_ && !replaying) {
                // The member repeats or skips one sample (6.4).
                const int d = std::clamp(int(e.delay), 1, 10);
                // A longer delay repeats the last sample for the ticks in
                // between; a shorter one skips sending until the sent pads
                // run out (before_step sends up to tick + d).
                if (d > delay_ && mode_ == Mode::sealed)
                    for (int k = delay_; k < d; ++k) send_input_ahead(last_own_);
                delay_ = d;
            }
            break;
        }
        events_.pop_front();
        changed = true;
    }
    return changed;
}

void Session::send_input_ahead(const w::WirePad& pad) {
    send(w::Type::input, w::encode(w::Input{inst_, visit_, input_next_, pad}));
    ++input_next_;
}

void Session::start_sealed(const w::WirePad& pad) {
    // Pads for the first d ticks go out at once (6.7), then one per step.
    input_next_ = tick_;
    for (int k = 0; k < delay_; ++k) send_input_ahead(pad);
    pace_.reset();
    filled_run_ = {};
    mode_ = Mode::sealed;
    last_tick_at_ = now_;
}

std::array<PadSample, 4> Session::before_step(const PadSample& local, double now) {
    now_ = now;
    const auto own = quantize_pad(local);
    std::array<PadSample, 4> pads{};
    switch (mode_) {
    case Mode::sealed:
    case Mode::observing: {
        if (lead_from_ && tick_ >= *lead_from_ && mode_ == Mode::sealed) {
            // From L the remaining member leads alone, resampling with delay 0.
            lead_from_.reset();
            mode_ = Mode::solo;
            log_first_ = tick_;
            log_.clear();
            return before_step(local, now);
        }
        const auto it = ticks_.find(tick_);
        if (it == ticks_.end()) return last_pads_; // the host ran a step it was not given (should not happen)
        w::TickBody body = std::move(it->second);
        ticks_.erase(it);
        for (auto& e : body.events) events_.push_back({tick_, std::move(e)});
        apply_events(false);
        if (mode_ == Mode::sealed) {
            while (input_next_ <= tick_ + std::uint32_t(delay_)) send_input_ahead(own);
            last_own_ = own;
        }
        for (int s = 0; s < 4; ++s) {
            const bool here = (body.present & (1u << s)) != 0;
            filled_run_[std::size_t(s)] = here && body.pads[std::size_t(s)].filled() ? filled_run_[std::size_t(s)] + 1 : 0;
        }
        pads = pads_of(body);
        break;
    }
    case Mode::solo: {
        apply_events(false);
        w::WirePad pad = own;
        if (join_at_) {
            // Delayed sampling until J: this tick plays the sample taken d
            // ticks ago; the new sample is for tick + d (from J an INPUT).
            pad = delayed_.empty() ? last_own_ : delayed_.front();
            if (!delayed_.empty()) delayed_.pop_front();
            if (tick_ + std::uint32_t(delay_) >= *join_at_) send_input_ahead(own);
            else delayed_.push_back(own);
        }
        last_own_ = pad;
        pads[std::size_t(slot_)] = dequantize_pad(pad, true);
        for (int s = 0; s < 4; ++s)
            if (s != slot_) pads[std::size_t(s)] = dequantize_pad({}, false);
        if (!hub_) {
            if (log_.empty()) log_first_ = tick_;
            log_.push_back(pad);
        }
        break;
    }
    case Mode::transition:
        // The transition steps take the pads of B (6.7).
        pads = last_pads_;
        return pads;
    default: return last_pads_;
    }
    last_pads_ = pads;
    return pads;
}

void Session::after_step(double now) {
    now_ = now;
    if (mode_ == Mode::transition) {
        if (engine_.load_serial() != serial_at_barrier_) begin_visit(now);
        return;
    }
    if (mode_ != Mode::solo && mode_ != Mode::sealed && mode_ != Mode::observing) return;
    const std::uint32_t executed = tick_;
    const bool member = mode_ != Mode::observing;
    if (member && !hub_ && executed % 60 == 0) send(w::Type::netfp, w::encode(w::NetFp{inst_, visit_, executed, engine_.net_fingerprint()}));
    ++tick_;
    if (join_at_ && tick_ >= *join_at_) {
        // From J the instance is sealed; SEALED(J) brings the JOIN.
        join_at_.reset();
        flush_log(now, true);
        delayed_.clear();
        pace_.reset();
        filled_run_ = {};
        mode_ = Mode::sealed;
        last_tick_at_ = now;
    }
    if (const auto split = engine_.take_split()) {
        const auto side = split->local_leaves ? std::uint8_t(split->leavers) : std::uint8_t(present_ & ~split->leavers);
        if (member) send(w::Type::split, w::encode(w::Split{inst_, visit_, executed, split->leavers,
                                                             split->in_stage ? w::SplitKind::in_stage : w::SplitKind::exit}));
        present_ = std::uint8_t(present_ & side);
        for (int s = 0; s < 4; ++s)
            if (!(present_ & (1u << s))) slot_members_[std::size_t(s)] = 0;
        std::erase_if(events_, [&](const PendingEvent& e) {
            return e.event.kind != w::EventKind::lead_from && e.event.kind != w::EventKind::join && !(side & (1u << e.event.slot));
        });
        if (member && mode_ == Mode::sealed && members(present_) == 1) {
            // This side has one member left: it leads alone from the next tick.
            mode_ = Mode::solo;
            ticks_.clear();
            log_first_ = tick_;
            log_.clear();
        }
    }
    if (engine_.supports_barriers()) {
        if (engine_.pending_stage()) park(now);
    } else if (member && engine_.load_serial() != serial_at_barrier_) {
        // An engine without the membership API: the stage load is reported
        // afterwards, the visit goes on as a solo NEW.
        serial_at_barrier_ = engine_.load_serial();
        flush_log(now, true);
        w::Barrier b{inst_, visit_, executed, engine_.stage(), std::int16_t(engine_.pending_point()), engine_.alive_mask(), present_,
                     engine_.session_context_hash(), {}};
        send(w::Type::barrier, w::encode(b));
        ++visit_;
        tick_ = 0;
        log_.clear();
        log_first_ = 0;
        hub_ = is_hub_stage(engine_.stage());
    }
    if (member && (mode_ == Mode::solo || mode_ == Mode::sealed)) report_state(now, false);
}

void Session::flush_log(double now, bool force) {
    if (log_.empty() || hub_ || !hello_done_) return;
    if (!force && now - log_sent_at_ < config_.log_interval) return;
    for (std::size_t at = 0; at < log_.size(); at += 0xFFFF) {
        w::Log m{inst_, visit_, log_first_ + std::uint32_t(at), {}};
        m.pads.assign(log_.begin() + std::ptrdiff_t(at), log_.begin() + std::ptrdiff_t(std::min(log_.size(), at + 0xFFFF)));
        send(w::Type::log, w::encode(m));
    }
    log_first_ += std::uint32_t(log_.size());
    log_.clear();
    log_sent_at_ = now;
}

void Session::report_state(double now, bool force) {
    if (hub_) return;
    w::NotJoinable reason{};
    const bool joinable = engine_.joinable(reason);
    const std::uint8_t flags = std::uint8_t((joinable ? w::state_flag::joinable : 0) | (engine_.membership_safe() ? w::state_flag::membership_safe : 0) |
                                            (engine_.boss_lock() ? w::state_flag::boss_lock : 0));
    const std::uint64_t context = engine_.session_context_hash();
    if (flags == state_flags_ && context == state_context_ && !force) {
        state_pending_ = false;
        return;
    }
    // The boss lock and the context at once, the rest at most once a second.
    const bool urgent = force || context != state_context_ || ((flags ^ state_flags_) & w::state_flag::boss_lock);
    if (!urgent && now - state_sent_at_ < 1.0) {
        state_pending_ = true;
        return;
    }
    state_flags_ = flags;
    state_context_ = context;
    state_sent_at_ = now;
    state_pending_ = false;
    send(w::Type::state, w::encode(w::State{inst_, visit_, flags, context, joinable ? w::NotJoinable::none : reason}));
}

// ---- barrier
void Session::park(double now) {
    flush_log(now, true);
    barrier_tick_ = tick_ - 1;
    serial_at_barrier_ = engine_.load_serial();
    group_at_barrier_ = present_;
    own_carry_ = engine_.carry(slot_);
    w::Barrier b;
    b.inst = inst_;
    b.visit = visit_;
    b.tick = barrier_tick_;
    b.stage = *engine_.pending_stage();
    b.point = std::int16_t(engine_.pending_point());
    b.alive = engine_.alive_mask();
    b.present = present_;
    b.context = engine_.session_context_hash();
    b.carries.push_back({std::uint8_t(slot_), own_carry_});
    send(w::Type::barrier, w::encode(b));
    ticks_.clear(); // ticks sealed after B are discarded
    events_.clear();
    join_at_.reset();
    delayed_.clear();
    lead_from_.reset();
    decision_.reset();
    loading_.clear();
    boss_choice_ = false;
    parked_at_ = now;
    mode_ = Mode::parked;
}

void Session::on_barrier_ok(const w::BarrierOk& ok, double now) {
    if (mode_ == Mode::parked) {
        if (ok.inst != inst_ || ok.visit != visit_) return;
        decision_ = ok;
        if (ok.decision == w::Decision::join || ok.decision == w::Decision::co_arrive) {
            // The own engine still loads the stage, as a solo NEW: its load
            // snapshot is the fallback if the target moves on (6.6).
            w::BarrierOk solo = ok;
            solo.decision = w::Decision::new_instance;
            solo.slot_map = {-1, -1, -1, -1};
            solo.slot_map[std::size_t(slot_)] = 0;
            solo.slot_members = {member_id_, 0, 0, 0};
            solo.co_arrivals.clear();
            solo.own_slot = 0;
            solo.present = 1;
            engine_.set_barrier(solo);
        } else {
            engine_.set_barrier(ok);
        }
        inst_ = ok.new_inst;
        visit_ = ok.new_visit;
        slot_ = ok.own_slot;
        present_ = ok.present;
        slot_members_ = ok.slot_members;
        loading_.clear();
        boss_choice_ = false;
        mode_ = Mode::transition;
        return;
    }
    if (mode_ == Mode::waiting_state || mode_ == Mode::catching_up || mode_ == Mode::observing) {
        // The target moved on during the catch-up (6.6): a barrier into the
        // same stage makes this a co-arrival of the new visit (its state
        // follows); anything else: back to the own fallback, as NEW.
        decision_ = ok;
        inst_ = ok.new_inst;
        visit_ = ok.new_visit;
        slot_ = ok.own_slot;
        present_ = ok.present;
        slot_members_ = ok.slot_members;
        ticks_.clear();
        events_.clear();
        assembler_.reset();
        ready_sent_ = false;
        if (ok.decision == w::Decision::co_arrive || ok.decision == w::Decision::join) {
            mode_ = Mode::waiting_state;
            loading_ = "Joining...";
            return;
        }
        if (!engine_.restore(fallback_)) {
            mode_ = Mode::closed;
            close_reason_ = "The stage could not be restored.";
            return;
        }
        // The fallback is the own solo load: slot 0 alone.
        slot_ = 0;
        present_ = 1;
        slot_members_ = {member_id_, 0, 0, 0};
        engine_replaced_ = true;
        engine_.start_online(slot_, present_);
        engine_.set_split_identity(slot_);
        engine_.set_replay(false);
        notice(ok.decision == w::Decision::new_instance ? "The group moved on; you play this stage on your own." : "");
        std::erase_if(notices_, [](const std::string& s) { return s.empty(); });
        tick_ = 0;
        hub_ = is_hub_stage(engine_.stage());
        begin_visit(now);
    }
}

void Session::begin_visit(double now) {
    // The load step ran: tick 0 drives the next step (6.7).
    // (ticks_ holds only ticks of the new visit: the old visit's went at the
    // barrier, and a joiner's LIVE ticks that came during the transition stay.)
    tick_ = 0;
    events_.clear();
    log_.clear();
    log_first_ = 0;
    serial_at_barrier_ = engine_.load_serial();
    hub_ = is_hub_stage(engine_.stage());
    state_flags_ = 0xFF;
    const auto decision = decision_ ? decision_->decision : w::Decision::new_instance;
    const w::WirePad pad = last_own_;
    switch (decision) {
    case w::Decision::new_instance:
    case w::Decision::dissolve:
        if (!hub_ && inst_ != 0) {
            // The leader uploads the load snapshot, every member sends the
            // canonical hash (6.8).
            if (lowest(present_) == slot_) {
                const auto raw = engine_.snapshot_bytes(false);
                for (const auto& c : w::make_chunks(inst_, visit_, std::uint8_t(slot_), raw)) send(w::Type::snapshot, w::encode(c, false));
            }
            const auto canonical = engine_.snapshot_bytes(true);
            send(w::Type::snaphash, w::encode(w::SnapHash{inst_, visit_, w::fnv1a64(canonical)}));
        }
        if (members(present_) <= 1 || decision == w::Decision::dissolve) {
            mode_ = Mode::solo;
        } else {
            start_sealed(pad);
        }
        report_state(now, true);
        break;
    case w::Decision::join:
    case w::Decision::co_arrive:
        // Keep the own load snapshot as the fallback; the instance's state
        // comes next.
        fallback_ = engine_.snapshot_bytes(false);
        ready_sent_ = false;
        mode_ = Mode::waiting_state;
        loading_ = "Joining...";
        // The state may have come during the transition steps.
        if (stashed_state_) {
            auto state = std::move(*stashed_state_);
            stashed_state_.reset();
            on_state(std::move(state), now);
        }
        break;
    }
}

void Session::on_state(w::StateData state, double now) {
    if (!engine_.restore(state.snapshot)) {
        // Damaged state: back to the own stage.
        notice("The group's state could not be loaded; you play this stage on your own.");
        if (!fallback_.empty() && engine_.restore(fallback_)) {
            engine_replaced_ = true;
            engine_.start_online(0, 1);
            slot_ = 0;
            present_ = 1;
            inst_ = 0;
            decision_.reset();
            begin_visit(now);
        } else {
            mode_ = Mode::closed;
            close_reason_ = "The stage could not be restored.";
        }
        return;
    }
    engine_replaced_ = true;
    tick_ = state.first;
    // The log, plus the LIVE / SEALED ticks that came meanwhile.
    for (std::size_t i = 0; i < state.ticks.size(); ++i) ticks_.emplace(state.first + std::uint32_t(i), std::move(state.ticks[i]));
    std::erase_if(ticks_, [&](const auto& t) { return t.first < state.first; });
    state_first_ = state.first;
    state_last_ = state.first + std::uint32_t(state.ticks.size());
    events_.clear();
    replay_fingerprints_.clear();
    hub_ = is_hub_stage(engine_.stage());
    const bool co_arrival = decision_ && decision_->decision == w::Decision::co_arrive && state.ticks.empty();
    if (co_arrival) {
        // The base spawned this member in its load step: a member from tick 0.
        engine_.set_split_identity(slot_);
        engine_.set_replay(false);
        serial_at_barrier_ = engine_.load_serial();
        send(w::Type::snaphash, w::encode(w::SnapHash{inst_, visit_, w::fnv1a64(engine_.snapshot_bytes(true))}));
        loading_.clear();
        if (members(present_) <= 1) mode_ = Mode::solo;
        else start_sealed(last_own_);
        report_state(now, true);
        return;
    }
    engine_.set_split_identity(-1);
    engine_.set_fork_sides(state.forks);
    engine_.set_replay(true);
    replayed_steps_ = 0;
    replay_seconds_ = 0;
    mode_ = Mode::catching_up;
    run_catch_up(now);
}

void Session::run_catch_up(double now) {
    const double start = clock_seconds();
    std::uint32_t done = 0;
    while (mode_ == Mode::catching_up) {
        const auto it = ticks_.find(tick_);
        if (it == ticks_.end()) break;
        w::TickBody body = std::move(it->second);
        ticks_.erase(it);
        for (auto& e : body.events) events_.push_back({tick_, std::move(e)});
        apply_events(true);
        engine_.replay_step(pads_of(body));
        (void)engine_.take_split(); // an observer keeps the side that keeps the instance id
        if (tick_ % 60 == 0) replay_fingerprints_[tick_] = engine_.net_fingerprint();
        ++tick_;
        ++done;
        if (engine_.supports_barriers() && engine_.pending_stage()) break; // the server redirects (co-arrival or NEW)
        if (config_.replay_budget > 0 && (done & 63) == 0 && clock_seconds() - start > config_.replay_budget) break;
    }
    replay_seconds_ += clock_seconds() - start;
    replayed_steps_ += done;
    const std::uint32_t newest = ticks_.empty() ? tick_ : ticks_.rbegin()->first + 1;
    const std::uint32_t remaining = newest > tick_ ? newest - tick_ : 0;
    const double rate = replay_seconds_ > 0.05 ? double(replayed_steps_) / replay_seconds_ : double(config_.rate);
    const double behind = rate > 60.0 ? double(remaining) / (rate - 59.94) : 0;
    loading_ = "Joining... (catching up, about " + std::to_string(int(behind + 0.999)) + " s)";
    if (!ready_sent_ && remaining <= 10 && tick_ > state_first_ && !replay_fingerprints_.empty()) {
        const auto last = std::prev(replay_fingerprints_.end());
        if (replayed_steps_ > 600 && replay_seconds_ > 0) config_.rate = std::uint32_t(std::min(1e6, rate));
        send(w::Type::ready, w::encode(w::Ready{inst_, visit_, last->first, last->second, config_.rate, own_carry_}));
        send(w::Type::rate, w::encode(config_.rate));
        ready_sent_ = true;
        engine_.set_replay(false);
        loading_.clear();
        pace_.reset();
        mode_ = Mode::observing;
        last_tick_at_ = now;
    }
}

void Session::become_member(double now) {
    // The JOIN for this member applied: from here it sends pads; the server
    // fills them until they arrive.
    engine_.set_split_identity(slot_);
    input_next_ = tick_ + 1;
    for (int k = 0; k < delay_; ++k) send_input_ahead(last_own_);
    mode_ = Mode::sealed;
    last_tick_at_ = now;
    const int other = lowest(std::uint8_t(present_ & ~(1u << slot_)));
    notice(other >= 0 ? "You joined " + slot_name(other) : std::string("You joined the group"));
}

// ---- choices and presentation
void Session::choose_boss(w::Choice choice) {
    if (mode_ != Mode::parked) return;
    send(w::Type::barrier_choice, w::encode(w::BarrierChoice{inst_, choice}));
    boss_choice_ = false;
}

void Session::send_intent(w::IntentKind kind, int argument) {
    if (mode_ != Mode::solo && mode_ != Mode::sealed) return;
    send(w::Type::intent, w::encode(w::Intent{inst_, visit_, kind, argument}));
}

void Session::send_heading(const std::string& stage, int point) {
    send(w::Type::heading, w::encode(w::Heading{inst_, stage, std::int16_t(point)}));
}

void Session::set_rate(std::uint32_t steps_per_second) {
    config_.rate = steps_per_second;
    send(w::Type::rate, w::encode(steps_per_second));
}

bool Session::take_engine_replaced() {
    const bool replaced = engine_replaced_;
    engine_replaced_ = false;
    return replaced;
}

std::vector<std::string> Session::take_notices() { return std::exchange(notices_, {}); }

Overlay Session::overlay(double now) const {
    Overlay o;
    o.loading = loading_;
    o.boss_choice = boss_choice_;
    o.connection_lost = mode_ == Mode::lost || (resume_after_lost_ && !hello_done_);
    if (o.connection_lost && o.loading.empty() && mode_ == Mode::lost) o.loading = "Connection lost, reconnecting...";
    if (mode_ == Mode::catching_up && state_last_ > state_first_)
        o.progress = float(std::clamp(double(tick_ - state_first_) / double(std::max(1u, state_last_ - state_first_)), 0.0, 1.0));
    o.waiting_for_server = (mode_ == Mode::sealed || mode_ == Mode::observing) && now - last_tick_at_ > 0.5;
    for (int s = 0; s < 4; ++s) o.lagging[std::size_t(s)] = filled_run_[std::size_t(s)] >= 30;
    o.slow_uplink = o.lagging[std::size_t(slot_)];
    return o;
}

} // namespace dw4::game::online
