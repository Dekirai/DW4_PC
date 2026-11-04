#include "session.hpp"

#include "pad_wire.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <thread>

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
// The UI stages (stagIS*: title, character select, save room, call shop,
// staff roll): the character select loads a record with its own story before
// the room story replaces it at game_started, so no STORY goes from them.
bool ui_stage(std::string_view stage) {
    return stage.size() >= 6 && (stage[4] == 'I' || stage[4] == 'i') && (stage[5] == 'S' || stage[5] == 's') && stage.substr(0, 4) == "stag";
}
int members(std::uint8_t mask) { return std::popcount(unsigned(mask)); }
int lowest(std::uint8_t mask) { return mask ? std::countr_zero(unsigned(mask)) : -1; }
double clock_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
} // namespace

Session::Session(EngineLink& engine, SessionConfig config) : engine_(engine), config_(std::move(config)) {
    // The rate the server plans joins and resyncs with is steps per second of
    // the wall clock. Before a catch-up measured it, the calibration's steps
    // per second of pure replay count for the share of a host pass the
    // catch-up gets: its budget out of the budget plus one 60 Hz picture
    // (SEAMLESS.md 6.6: a 12 ms budget gave about 70 % of the reported rate).
    if (config_.max_replay_rate == 0 && config_.replay_budget > 0)
        config_.rate = std::max<std::uint32_t>(
            1, std::uint32_t(double(config_.rate) * config_.replay_budget / (config_.replay_budget + kFieldSeconds)));
}

void Session::send(w::Type type, const std::vector<std::uint8_t>& payload) {
    if (!transport_ || !hello_done_) return;
    const auto frame = w::encode_frame(type, payload);
    if (!transport_->send(frame)) lose_connection(now_);
}

void Session::notice(std::string text) { notices_.push_back(std::move(text)); }

void Session::trace(const char* format, ...) const {
    if (!config_.trace) return;
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    std::printf("online-trace: %s\n", line);
    std::fflush(stdout);
}

void Session::log(const char* format, ...) const {
    if (!online_log_ || !online_log_->is_open()) return;
    char line[768];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    online_log_->line("%s", line);
}

void Session::set_delay(int d, const char* why) {
    if (d == delay_) return;
    trace("delay %d -> %d (%s) mode=%s inst=%u visit=%u tick=%u", delay_, d, why, mode_name(mode_), inst_, visit_, tick_);
    log("input delay %d -> %d ticks (%s, %s, stage %s)", delay_, d, why, mode_name(mode_), engine_.stage().c_str());
    delay_ = d;
}

void Session::note_mode() {
    if (mode_ == traced_mode_) return;
    trace("mode %s -> %s inst=%u visit=%u tick=%u d=%d", mode_name(traced_mode_), mode_name(mode_), inst_, visit_, tick_, delay_);
    log("mode %s -> %s (instance %u visit %u tick %u, stage %s, input delay %d, present %x)", mode_name(traced_mode_), mode_name(mode_), inst_,
        visit_, tick_, engine_.stage().c_str(), delay_, unsigned(present_));
    if (mode_ == Mode::closed && !close_reason_.empty()) log("closed: %s", close_reason_.c_str());
    traced_mode_ = mode_;
}

void Session::report_window(double now, bool force) {
    stats_.roll_recent(now);
    if (transport_ && now - tcp_checked_at_ >= 1.0) {
        tcp_checked_at_ = now;
        net_.tcp_rtt_ms = transport_->tcp_rtt_ms();
        net_.tcp_loss = transport_->tcp_retransmits();
        if (!net_.from_relay) net_.rtt_ms = net_.tcp_rtt_ms;
    }
    if (!force && now - window_at_ < 5.0) return;
    window_at_ = now;
    const auto w = stats_.take_window();
    log_window_.add(w);
    if (force || now - log_window_at_ >= 10.0) {
        log_window_at_ = now;
        const auto& l = log_window_;
        if (l.samples)
            log("link: %s, stage %s, input delay %d ticks (%.0f ms), round trip %.0f ms (network about %.0f, wait for this game's pass %.1f, "
                "spread %.0f, tcp %.1f), relay margin %d ms, "
                "own pads %llu (not sealed as sent: %llu), latency avg %.0f ms p95 %.0f max %.0f ms, relay: %llu sealed, %llu filled, "
                "%llu late, backlog runs %zu, starved %zu, drained %zu, tcp loss episodes %lld",
                mode_name(mode_), engine_.stage().c_str(), delay_, delay_ * kFieldSeconds * 1000.0, net_.rtt_ms, net_.network_rtt_ms(),
                net_.pass_wait_ms, net_.rtt_spread_ms, net_.tcp_rtt_ms,
                net_.margin_ms, static_cast<unsigned long long>(l.samples), static_cast<unsigned long long>(l.dropped), l.exec_avg() * 1000.0,
                l.exec_quantile(0.95) * 1000.0, l.exec_max * 1000.0, static_cast<unsigned long long>(net_.sealed),
                static_cast<unsigned long long>(net_.filled), static_cast<unsigned long long>(net_.late), backlog_runs_, pace_.starved(),
                pace_.drained(), net_.tcp_loss);
        log_window_ = {};
    }
    if (!w.samples && !force) return;
    trace("net mode=%s inst=%u visit=%u tick=%u d=%d samples=%llu dropped=%llu exec_avg=%.1f exec_p50=%.1f exec_p95=%.1f exec_max=%.1f "
          "present_avg=%.1f present_max=%.1f steps=%.2f starved=%zu backlogs=%zu level=%.2f rtt=%.1f spread=%.1f margin=%d late=%llu filled=%llu "
          "sealed=%llu",
          mode_name(mode_), inst_, visit_, tick_, delay_, static_cast<unsigned long long>(w.samples), static_cast<unsigned long long>(w.dropped),
          w.exec_avg() * 1000.0, w.exec_quantile(0.5) * 1000.0, w.exec_quantile(0.95) * 1000.0, w.exec_max * 1000.0, w.present_avg() * 1000.0,
          w.present_max * 1000.0, w.steps_avg(), pace_.starved(), backlog_runs_, pace_.level(), net_.rtt_ms, net_.rtt_spread_ms, net_.margin_ms,
          static_cast<unsigned long long>(net_.late), static_cast<unsigned long long>(net_.filled), static_cast<unsigned long long>(net_.sealed));
}

std::string Session::member_name(std::uint32_t member) const {
    if (const auto it = names_.find(member); it != names_.end() && !it->second.empty()) return it->second;
    return member != kNoMember ? "Player " + std::to_string(member + 1) : std::string("A player");
}

std::uint32_t Session::roster_member_for(int slot) const {
    // A JOIN names no member: the roster's one member of this instance that
    // no slot holds yet (the joiner catches up on it).
    (void)slot;
    std::uint32_t found = kNoMember;
    for (const auto& [member, inst] : roster_inst_) {
        if (inst != inst_ || member == member_id_ || std::find(slot_members_.begin(), slot_members_.end(), member) != slot_members_.end()) continue;
        if (found != kNoMember) return kNoMember; // more than one: unknown
        found = member;
    }
    return found;
}

std::string Session::slot_name(int slot) const {
    return slot >= 0 && slot < 4 && slot_members_[std::size_t(slot)] != kNoMember ? member_name(slot_members_[std::size_t(slot)])
                                                                                   : std::string("A player");
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
    // STORY answers come in order on one connection: those of the old one
    // never come, so nothing sent on it waits for an answer, and the pending
    // set goes again on the new one (8.3).
    story_in_flight_.clear();
    story_last_ = {};
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
    ++net_.reconnects;
    log("connection to the relay lost (%s); reconnecting", mode_name(mode_));
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
    (void)reason;
    send(w::Type::bye, w::encode(w::Bye{0}));
    if (transport_) transport_->close();
    transport_.reset();
    mode_ = Mode::closed;
    close_reason_ = "You left the room.";
}

std::size_t Session::read_hello(double now) {
    std::size_t used = 0;
    while (used < inbox_.size() && !hello_done_) {
        const char c = char(inbox_[used++]);
        if (c != '\n') {
            line_ += c;
            if (line_.size() > 256) {
                lose_connection(now);
                return inbox_.size();
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
            return inbox_.size();
        }
        hello_done_ = true;
        member_id_ = answer->member_id;
        if (!config_.own_name.empty()) names_[member_id_] = config_.own_name;
        // The instance comes with WELCOME, the server's first frame.
    }
    return used;
}

bool Session::await_hello(double timeout) {
    const auto clock = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const double until = clock() + timeout;
    while (transport_ && !hello_done_ && mode_ != Mode::closed && clock() < until) {
        inbox_.clear();
        const bool open = transport_->receive(inbox_);
        const std::size_t used = read_hello(now_);
        // (The frames after the answer wait in the reader for the next poll.)
        if (hello_done_ && transport_ && used < inbox_.size()) {
            try {
                reader_.feed(std::span(inbox_).subspan(used));
            } catch (const w::WireError&) {
                lose_connection(now_);
                return false;
            }
        }
        if (!open && transport_) {
            lose_connection(now_);
            return false;
        }
        if (!hello_done_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return hello_done_;
}

void Session::poll(double now) {
    now_ = now;
    poll_gap_ = last_poll_at_ >= 0 ? std::clamp(now - last_poll_at_, 0.0, 1.0) : 0.0;
    last_poll_at_ = now;
    if (transport_) {
        inbox_.clear();
        const bool open = transport_->receive(inbox_);
        std::size_t used = 0;
        if (!hello_done_) {
            used = read_hello(now);
            if (!transport_) return;
        }
        if (hello_done_) {
            // (Frames await_hello received after the answer are in the reader already.)
            try {
                if (used < inbox_.size()) reader_.feed(std::span(inbox_).subspan(used));
                while (auto frame = reader_.next()) {
                    if (informational(frame->type)) {
                        // A frame that only feeds the indicator, the tags or the
                        // room list and does not decode is skipped, not a broken
                        // connection: the same type number given to another
                        // message by another build (round 62 review: round 60's
                        // PRESENCE2 on 0x8F, NETSTAT's number since 0.1.2) would
                        // otherwise reconnect this client once a second.
                        try {
                            handle(*frame, now);
                        } catch (const w::WireError& error) {
                            if (net_.skipped_frames++ == 0)
                                log("a %s frame from the relay did not decode (%s); skipped (another server build?)", w::type_name(frame->type),
                                    error.what());
                        }
                    } else {
                        handle(*frame, now);
                    }
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
    note_mode();
    if (mode_ == Mode::solo || mode_ == Mode::sealed || mode_ == Mode::observing) report_window(now, false);
    // A solo group parked without an answer goes on as NEW (3.4); a larger
    // group keeps waiting. A held barrier (BARRIER_WAIT: the server holds it
    // for a co-arrival, a join or a boss choice) is an answer: the five
    // seconds count from the server's last word, and a boss choice waits.
    if (mode_ == Mode::parked && members(group_at_barrier_) == 1 && !boss_choice_ && now - parked_at_ > config_.barrier_timeout) {
        w::BarrierOk ok;
        ok.inst = inst_;
        ok.visit = visit_;
        ok.decision = w::Decision::new_instance;
        ok.new_inst = 0; // not registered: never offered to joiners
        ok.new_visit = visit_ + 1;
        ok.own_slot = 0;
        ok.slot_map = {-1, -1, -1, -1};
        ok.slot_map[std::size_t(slot_)] = 0;
        ok.mode = w::Mode::solo;
        notice("The server did not answer; you continue on your own.");
        on_barrier_ok(ok, now);
    }
    if (mode_ == Mode::solo || mode_ == Mode::sealed) {
        flush_log(now, false);
        if (state_pending_) report_state(now, false);
        if (now - story_checked_at_ >= config_.story_check_interval) {
            story_checked_at_ = now;
            send_story(now, false);
        }
        send_presence(now);
    }
}

namespace {
// A clip or weapon name as PRESENCE2 carries it: A-Z a-z 0-9 _ up to `limit`
// characters, anything else unknown (empty).
std::string wire_name(const std::string& name, std::size_t limit) {
    if (name.size() > limit) return {};
    for (const char c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return {};
    return name;
}
} // namespace

void Session::send_presence(double now) {
    // PRESENCE2 to a hub that takes it: ten a second while the figure moves,
    // turns or changes its motion (the others' ghost figures walk smoothly),
    // five while nothing changes; PRESENCE five a second to an older hub.
    const double since = now - presence_at_;
    if (since < (presence_v2_ ? config_.presence_moving_interval : config_.presence_interval)) return;
    if (departing_ && (engine_.load_serial() != departing_serial_ || now - departing_at_ > kDepartingLimit)) departing_ = false;
    const auto facts = engine_.presence(slot_);
    if (!facts.in_stage) {
        // Loading, a title screen: the member left the stage it showed (once).
        hide_presence(now);
        presence_at_ = now;
        return;
    }
    w::Presence p;
    // An empty stage hides the tag (loads, scenes, menus, a departure); hp 0
    // shows a defeated member. (HP and MP above their maximum, as a script's
    // MDLaddHp allows, show full.)
    const bool hidden = departing_ || (presence_v2_ ? facts.figure_hidden : facts.hidden);
    p.stage = hidden ? std::string() : engine_.stage();
    p.inst = inst_;
    p.x = facts.x;
    p.y = facts.y;
    p.z = facts.z;
    p.yaw = facts.yaw;
    p.species = std::uint8_t(std::clamp(facts.species, 0, 255));
    p.level = std::uint16_t(std::clamp(facts.level, 0, 65535));
    p.max_hp = std::uint16_t(std::clamp(facts.max_hp, 0, 65535));
    p.max_mp = std::uint16_t(std::clamp(facts.max_mp, 0, 65535));
    p.hp = std::uint16_t(std::clamp(facts.hp, 0, p.max_hp > 0 ? int(p.max_hp) : 65535));
    p.mp = std::uint16_t(std::clamp(facts.mp, 0, p.max_mp > 0 ? int(p.max_mp) : 65535));
    if (facts.defeated) p.hp = 0;
    if (!presence_v2_) {
        transmit(std::move(p), now);
        return;
    }
#ifdef DW4_ONLINE_DEV
    // DW4_ONLINE_TEST_FORM=SECONDS (development builds; the ghost figure
    // scenarios of tools/online_test): the form the others' figures show
    // moves on by one every SECONDS (the engine's form stays as it is).
    static const double form_cycle = [] {
        const char* text = std::getenv("DW4_ONLINE_TEST_FORM");
        return text && *text ? std::atof(text) : 0.0;
    }();
    if (form_cycle > 0) p.species = std::uint8_t((facts.species + int(now / form_cycle)) % 16);
#endif
    p.version = w::kPresenceVersion;
    p.clip = wire_name(facts.clip, w::kMaxClip);
    p.flags = std::uint8_t((facts.clip_loops ? w::Presence::loop : 0) | (facts.busy ? w::Presence::busy : 0));
    p.warp = warp_;
    p.weapon = wire_name(facts.weapon, w::kMaxWeapon);
    // (The clock is compared below without it.)
    if (presence_sent_ && since < config_.presence_interval) {
        auto same = *presence_sent_;
        same.sent_ms = 0;
        const bool still = same.stage == p.stage && same.inst == p.inst && std::abs(same.x - p.x) < 0.01f && std::abs(same.y - p.y) < 0.01f &&
                           std::abs(same.z - p.z) < 0.01f && std::abs(same.yaw - p.yaw) < 0.001f && same.clip == p.clip &&
                           same.flags == p.flags && same.warp == p.warp && same.species == p.species && same.weapon == p.weapon;
        if (still) return;
    }
    transmit(std::move(p), now);
}

void Session::hide_presence(double now) {
    // The last one sent at once with an empty stage: the hub sends it to the
    // members still in the stage it showed (its "left" path), whose tag and
    // figure of this member fade out now.
    if (!presence_sent_ || presence_sent_->stage.empty()) return;
    auto p = *presence_sent_;
    p.stage.clear();
    transmit(std::move(p), now);
}

void Session::depart(double now) {
    // A departure: from here until the next stage load the member shows
    // nowhere (the stage's own scene and fade end on its machine alone).
    if (!departing_) {
        departing_ = true;
        departing_serial_ = engine_.load_serial();
        departing_at_ = now;
        trace("presence hidden until the next stage load (serial %llu)", static_cast<unsigned long long>(departing_serial_));
    }
    hide_presence(now);
}

void Session::transmit(w::Presence p, double now) {
    presence_at_ = now;
    if (p.version == 0) {
        send(w::Type::presence, w::encode(p, false));
    } else {
        // PRESENCE2's clock: the step whose place this is (track_figure),
        // the poll time after a pause without steps; always later than the
        // last one sent (the receivers start over at a clock that goes back).
        const double clock = now - step_at_ <= kStepStale ? step_at_ : now;
        p.sent_ms = std::uint32_t(std::llround(clock * 1000.0));
        if (presence_sent_ && presence_sent_->version != 0 && std::int32_t(p.sent_ms - presence_sent_->sent_ms) <= 0)
            p.sent_ms = presence_sent_->sent_ms + 1;
        send(w::Type::presence2, w::encode_presence2(p, false));
    }
    presence_sent_ = std::move(p);
}

void Session::track_figure(double now) {
    // PRESENCE2's warp counter: a stage load or a step that moved the figure
    // farther than any walk (a warp, a gate) counts, so the others' figures
    // jump instead of gliding across; and its step clock. Reads the engine
    // only.
    if (!presence_v2_) return;
    step_at_ = now - step_at_ < 0.25 ? std::clamp(step_at_ + kFieldSeconds, now - kStepSlack, now) : now;
    const auto serial = engine_.load_serial();
    if (serial != figure_serial_) {
        figure_serial_ = serial;
        ++warp_;
        figure_known_ = false;
    }
    const auto facts = engine_.presence(slot_);
    if (!facts.in_stage || facts.figure_hidden) {
        figure_known_ = false;
        return;
    }
    if (figure_known_) {
        const float dx = facts.x - figure_x_, dy = facts.y - figure_y_, dz = facts.z - figure_z_;
        if (dx * dx + dy * dy + dz * dz > kWarpStep * kWarpStep) ++warp_;
    }
    figure_x_ = facts.x;
    figure_y_ = facts.y;
    figure_z_ = facts.z;
    figure_known_ = true;
}

bool Session::informational(w::Type type) noexcept {
    return type == w::Type::netstat || type == w::Type::presence_of || type == w::Type::presence2_of || type == w::Type::roster ||
           type == w::Type::room;
}

void Session::handle(const w::Frame& frame, double now) {
    const auto& p = frame.payload;
    switch (frame.type) {
    case w::Type::welcome: on_welcome(w::decode_welcome(p), now); break;
    case w::Type::sealed:
    case w::Type::live: {
        auto m = w::decode_ticks(p);
        for (std::size_t i = 0; i < m.ticks.size(); ++i) on_sealed(m.inst, m.visit, m.first + std::uint32_t(i), std::move(m.ticks[i]), now);
        break;
    }
    case w::Type::join_req: {
        const auto m = w::decode_join_req(p);
        if (mode_ != Mode::solo || m.inst != inst_ || m.visit != visit_ || hub_ || join_at_) break;
        // 1 -> 2 (6.4): J = the current tick + d (the server suggests d);
        // delayed sampling at once, the last pad repeated for the ticks
        // before J without a sample.
        if (m.delay >= 1) set_delay(std::min<int>(m.delay, 15), "join_req");
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
        boss_choice_ = (m.flags & w::BarrierWait::choice_needed) != 0 || m.reason == w::WaitReason::boss_lock;
        if (mode_ == Mode::parked) parked_at_ = now; // the server holds the barrier: it answered
        loading_ = m.reason == w::WaitReason::co_arrival  ? "Waiting for " + names + "..."
                   : m.reason == w::WaitReason::boss_lock ? names + " fight a boss. Wait for them, or go alone?"
                                                          : "Joining " + names + "...";
        break;
    }
    case w::Type::state_data: {
        const auto chunk = w::decode_chunk(p);
        // (Parked, nothing is awaited: a state comes after BARRIER_OK. One
        // that reaches a member after its BARRIER belongs to a resync of the
        // visit it left, and restored at the next load it threw the member
        // back into the old stage.)
        if (mode_ == Mode::solo || mode_ == Mode::sealed || mode_ == Mode::connecting || mode_ == Mode::parked) break;
        if (chunk.inst != inst_ || chunk.visit != visit_) break;
        if (auto body = assembler_.add(chunk, config_.state_limit)) {
            auto state = w::decode_state_body(*body);
            state.inst = chunk.inst;
            state.visit = chunk.visit;
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
        // Only a member that plays the instance's sealed ticks catches up
        // again: one parked after its barrier (its BARRIER is out; the state
        // would throw it back into the stage it left, restored as its load),
        // one leading alone or still catching up has nothing to repair.
        if (mode_ != Mode::sealed) {
            trace("resync ignored inst=%u visit=%u tick=%u mode=%s", inst_, visit_, tick_, mode_name(mode_));
            break;
        }
        notice("Lost sync with the group - catching up again...");
        trace("resync inst=%u visit=%u tick=%u", inst_, visit_, tick_);
        ++net_.resyncs;
        log("RESYNC: this machine lost sync with the group at tick %u of instance %u (stage %s); catching up again", tick_, inst_,
            engine_.stage().c_str());
        resyncing_ = true;
        ready_sent_ = false;
        ticks_.clear(); // the state brings every tick from 0 again
        fallback_ = engine_.snapshot_bytes(false);
        own_carry_ = engine_.carry(slot_);
        mode_ = Mode::waiting_state;
        assembler_.reset();
        break;
    }
    case w::Type::presence_of:
    case w::Type::presence2_of: {
        auto m = frame.type == w::Type::presence2_of ? w::decode_presence2(p, true) : w::decode_presence(p, true);
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
        const bool own = m.member == member_id_;
        if (!m.name.empty()) names_[m.member] = m.name;
        switch (m.kind) {
        case w::RoomEvent::joined:
            // (Right after WELCOME the server names every member already in
            // the room: they are no news, only members that come later are.)
            if (!own && known_members_.insert(m.member).second && hello_seen_ && now_ - welcome_at_ > 1.0)
                notice(member_name(m.member) + " joined the room");
            break;
        case w::RoomEvent::left:
            if (!own) notice(member_name(m.member) + " left the room");
            known_members_.erase(m.member);
            remote_.erase(m.member);
            tracks_.forget(m.member);
            lagging_members_.erase(m.member);
            break;
        case w::RoomEvent::owner_present: notice("The room owner is back"); break;
        case w::RoomEvent::owner_away: notice("The room owner is away"); break;
        case w::RoomEvent::lagging: lagging_members_.insert(m.member); break;
        case w::RoomEvent::lag_over: lagging_members_.erase(m.member); break;
        case w::RoomEvent::connected:
            // (Also sent at a member's first connection: "back" only after a loss.)
            if (!own && disconnected_members_.erase(m.member)) notice(member_name(m.member) + " is back");
            break;
        case w::RoomEvent::disconnected:
            if (!own) {
                disconnected_members_.insert(m.member);
                notice(member_name(m.member) + " lost the connection");
            }
            break;
        }
        break;
    }
    case w::Type::roster: {
        // Once per second: who lags (the name tags' connection icon).
        const auto m = w::decode_roster(p);
        lagging_members_.clear();
        roster_inst_.clear();
        roster_ = m.entries;
        for (const auto& e : m.entries) {
            if (e.flags & 2) lagging_members_.insert(e.member);
            roster_inst_[e.member] = e.inst;
        }
        break;
    }
    case w::Type::ping: {
        const auto m = w::decode_ping(p);
        // (The PING came at some time since the last pass: on average half that gap ago.)
        pass_waits_[pass_wait_count_++ % pass_waits_.size()] = poll_gap_ * 500.0;
        {
            std::array<double, 31> waits{};
            const std::size_t n = std::min(pass_wait_count_, pass_waits_.size());
            std::copy_n(pass_waits_.begin(), n, waits.begin());
            std::nth_element(waits.begin(), waits.begin() + std::ptrdiff_t(n / 2), waits.begin() + std::ptrdiff_t(n));
            net_.pass_wait_ms = waits[n / 2];
        }
        if (m.delay >= 1 && m.delay <= 15 && mode_ != Mode::sealed) set_delay(m.delay, "ping"); // sealed: DELAY events change it
        net_.margin_ms = m.margin_ms;
        net_.suggested_delay = m.delay;
        const auto ms = std::uint64_t(clock_seconds() * 1000.0);
        send(w::Type::pong, w::encode(w::Pong{m.nonce, ms}));
        break;
    }
    case w::Type::checkpoint_ask: {
        const auto m = w::decode_checkpoint_ask(p);
        if (m.inst == inst_ && m.visit == visit_ && (mode_ == Mode::solo || mode_ == Mode::sealed) && !hub_) {
            checkpoint_asked_ = true;
            trace("checkpoint asked inst=%u visit=%u tick=%u", inst_, visit_, tick_);
        }
        break;
    }
    case w::Type::netstat: {
        // The relay's view of this member's link (round 62): the network indicator, F1 > Online, online.log.
        const auto m = w::decode_netstat(p);
        net_.rtt_ms = m.rtt_ms;
        net_.rtt_spread_ms = m.spread_ms;
        net_.margin_ms = m.margin_ms;
        net_.late = m.late;
        net_.filled = m.filled;
        net_.sealed = m.sealed;
        net_.from_relay = true;
        break;
    }
    case w::Type::reassign: {
        // After a fork this copy is the child: its stream comes with the new
        // id (frames in flight with the old id are still this instance's).
        const auto m = w::decode_reassign(p);
        if (m.inst == inst_ && m.visit == visit_) {
            previous_inst_ = inst_;
            inst_ = m.new_inst;
            for (auto& line : lines_)
                if (line.awaiting_id) {
                    line.inst = m.new_inst;
                    line.awaiting_id = false;
                }
            trace("reassign inst=%u -> %u from tick=%u", m.inst, m.new_inst, m.from_tick);
        }
        break;
    }
    case w::Type::close: {
        const auto m = w::decode_close(p);
        trace("close reason=%u text=%s", unsigned(m.reason), m.text.c_str());
        flush_log(now, true);
        if (transport_) transport_->close();
        transport_.reset();
        mode_ = Mode::closed;
        close_code_ = std::uint8_t(m.reason);
        close_reason_ = m.reason == w::CloseReason::kicked      ? "You were removed from the room" + (m.text.empty() ? std::string(".") : " (" + m.text + ").")
                        : m.reason == w::CloseReason::room_closed ? "The room was closed" + (m.text.empty() ? std::string(".") : " (" + m.text + ").")
                                                                  : "The relay closed the connection" + (m.text.empty() ? std::string(".") : " (" + m.text + ").");
        break;
    }
    case w::Type::story_ack: on_story_ack(w::decode_story_ack(p)); break;
    default: break;                  // unknown frames from a newer server are skipped
    }
}

void Session::on_sealed(std::uint32_t inst, std::uint32_t visit, std::uint32_t tick, w::TickBody body, double now) {
    if ((inst != inst_ && !(previous_inst_ && inst == previous_inst_)) || visit != visit_) return;
    // Executed already, or after the barrier of a visit. During the
    // transition steps and while a state is awaited (a join, a co-arrival, a
    // resync) tick_ still counts the old visit (or 0 before the state's
    // first tick): the new visit's ticks are kept (the relay sends a joiner
    // the target's LIVE ticks from its STATE_DATA on, while the joiner still
    // loads its own stage; on_state drops the ones before the state).
    if (tick < tick_ && mode_ != Mode::transition && mode_ != Mode::waiting_state) return;
    if (mode_ == Mode::parked) return; // ticks sealed after B are discarded (after BARRIER_OK inst / visit are the new ones)
    ticks_[tick] = std::move(body);
    last_tick_at_ = now;
}

void Session::on_welcome(const w::Welcome& m, double now) {
    if (!hello_seen_) welcome_at_ = now;
    hello_seen_ = true;
    member_id_ = m.member;
    // (Every WELCOME: a reconnect may reach another hub build.)
    presence_v2_ = (m.flags & w::Welcome::presence_v2) != 0;
    hub_checkpoints_ = (m.flags & w::Welcome::checkpoints) != 0;
#ifdef DW4_ONLINE_DEV
    // DW4_ONLINE_TEST_PRESENCE1=1 (development builds; the ghost figure
    // scenario P2 of tools/online_test): this client acts as one from before
    // round 60 or as a client of an older hub: it sends PRESENCE, so the hub
    // sends it PRESENCE too, and its figures come from PRESENCE alone.
    static const bool presence1 = [] {
        const char* text = std::getenv("DW4_ONLINE_TEST_PRESENCE1");
        return text && *text && *text != '0';
    }();
    if (presence1) presence_v2_ = false;
#endif
    presence_sent_.reset();
    if (!config_.own_name.empty()) names_[member_id_] = config_.own_name;
    if (resume_after_lost_) {
        resume_after_lost_ = false;
        if ((m.flags & w::Welcome::resumable) && m.inst == inst_ && m.visit == visit_ && m.slot == slot_) {
            // Still in the instance: only the missing ticks come (6.10).
            send(w::Type::resume, w::encode(w::Resume{inst_, visit_, std::uint8_t(slot_), tick_ ? tick_ - 1 : 0}));
            if (mode_ == Mode::lost) mode_ = mode_before_lost_;
            last_tick_at_ = now;
            flush_log(now, true);
            return;
        }
        // Removed meanwhile: this engine plays on alone until its next
        // barrier, where the server takes it as a lone arrival (round S4
        // uploads its state).
        notice("You were removed from the group; you continue on your own.");
        inst_ = m.inst;
        visit_ = m.visit;
        slot_members_ = {member_id_, kNoMember, kNoMember, kNoMember};
        mode_of_instance_ = w::Mode::hub;
        hub_ = true;
        ticks_.clear();
        events_.clear();
        if (mode_ == Mode::lost || mode_ == Mode::sealed || mode_ == Mode::observing) mode_ = Mode::solo;
        return;
    }
    if (mode_ != Mode::connecting) return;
    // A fresh member: the server's solo hub instance (the title).
    trace("welcome member=%u inst=%u visit=%u slot=%u mode=%u", unsigned(m.member), m.inst, m.visit, unsigned(m.slot), unsigned(m.mode));
    log("relay welcome: member %u, instance %u, input delay %u", unsigned(m.member), m.inst, unsigned(m.delay));
    inst_ = m.inst;
    visit_ = m.visit;
    slot_ = m.slot;
    present_ = std::uint8_t(1u << slot_);
    slot_members_ = {kNoMember, kNoMember, kNoMember, kNoMember};
    slot_members_[std::size_t(slot_)] = member_id_;
    mode_of_instance_ = m.mode;
    if (m.delay >= 1) set_delay(m.delay, "welcome");
    mode_ = Mode::solo;
    hub_ = m.mode == w::Mode::hub || is_hub_stage(engine_.stage());
    log_first_ = tick_;
    send(w::Type::rate, w::encode(config_.rate));
}

void Session::take_decision(const w::BarrierOk& ok) {
    trace("decision %u inst=%u visit=%u -> inst=%u visit=%u stage=%s slot=%u mode=%u delay=%u co_arrivals=%zu notice=%u", unsigned(ok.decision),
          ok.inst, ok.visit, ok.new_inst, ok.new_visit, ok.stage.c_str(), unsigned(ok.own_slot), unsigned(ok.mode), unsigned(ok.delay),
          ok.co_arrivals.size(), unsigned(ok.notice));
    log("barrier: decision %u to stage %s (instance %u visit %u, slot %u, %s, input delay %u, notice %u)", unsigned(ok.decision), ok.stage.c_str(),
        ok.new_inst, ok.new_visit, unsigned(ok.own_slot), ok.mode == w::Mode::sealed ? "sealed" : ok.mode == w::Mode::solo ? "solo-led" : "hub",
        unsigned(ok.delay), unsigned(ok.notice));
    inst_ = ok.new_inst;
    visit_ = ok.new_visit;
    slot_ = ok.own_slot;
    present_ = ok.present();
    mode_of_instance_ = ok.mode;
    if (ok.delay >= 1) set_delay(std::min<int>(ok.delay, 15), "barrier_ok");
    previous_inst_.reset();
    // Who sits where in the new instance (names): the own slot, the
    // co-arrivals; the base group's other members keep their names by slot.
    std::array<std::uint32_t, 4> members{kNoMember, kNoMember, kNoMember, kNoMember};
    for (int s = 0; s < 4; ++s)
        if (const int to = ok.slot_map[std::size_t(s)]; to >= 0 && to < 4) members[std::size_t(to)] = slot_members_[std::size_t(s)];
    for (const auto& c : ok.co_arrivals) members[c.slot & 3u] = c.member;
    members[std::size_t(slot_)] = member_id_;
    slot_members_ = members;
    // The snapshot's uploader: the base group's member in the lowest new slot.
    uploader_slot_ = -1;
    for (const auto to : ok.slot_map)
        if (to >= 0 && (uploader_slot_ < 0 || to < uploader_slot_)) uploader_slot_ = to;
    if (ok.decision == w::Decision::dissolve) uploader_slot_ = 0;
    if (ok.notice != w::Notice::none) {
        std::string names;
        for (std::size_t i = 0; i < ok.names.size(); ++i) names += (i ? (i + 1 == ok.names.size() ? " and " : ", ") : "") + ok.names[i];
        if (names.empty()) names = "Your friends";
        switch (ok.notice) {
        case w::Notice::other_context: notice(names + (ok.names.size() > 1 ? " are" : " is") + " on another quest; you play this stage on your own."); break;
        case w::Notice::full: notice(names + "'s group is full; you play this stage on your own."); break;
        case w::Notice::too_long: notice(names + (ok.names.size() > 1 ? " have" : " has") + " been there too long to catch up; you play this stage on your own."); break;
        case w::Notice::hold_expired: notice("The scene did not end in time; you play this stage on your own."); break;
        case w::Notice::went_alone: notice("You went alone."); break;
        case w::Notice::join_failed: notice("Joining " + names + " failed; you play this stage on your own."); break;
        default: break;
        }
    }
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
        const int due = pace_.due_steps(elapsed, waiting);
        if (pace_.catching_up()) {
            ++backlog_runs_;
            trace("backlog inst=%u visit=%u tick=%u waiting=%zu run=%d", inst_, visit_, tick_, waiting, due);
        }
        return due;
    }
    default: return 0;
    }
}

bool Session::step_ready() const {
    switch (mode_) {
    case Mode::solo:
    case Mode::transition: return true;
    case Mode::sealed: return ticks_.contains(tick_) || (lead_from_ && tick_ >= *lead_from_);
    case Mode::observing: return ticks_.contains(tick_);
    default: return false;
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
    // A membership event (join, leave, intent) waits for a step the instance
    // allows it (6.5), and the membership events behind it wait with it in
    // their order; LEAD_FROM and DELAY are no simulation events and never
    // wait behind them (a partner who left during the next stage's load:
    // its LEAVE waits for the end of the entry scene, but LEAD_FROM must
    // let the remaining member lead alone at its tick, or nobody seals the
    // ticks it waits for; S19b on real game clients).
    bool held = false;
    for (auto it = events_.begin(); it != events_.end() && it->tick <= tick_;) {
        const auto& e = it->event;
        if (e.kind == w::EventKind::leave && e.slot == slot_ && !replaying && mode_ == Mode::sealed) {
            // The server took this member out (6.9, 6.10: lag, a resync too
            // long, too many resyncs, a missing hash): it plays on alone.
            go_alone();
            return true;
        }
        if (e.kind == w::EventKind::join || e.kind == w::EventKind::leave || e.kind == w::EventKind::intent) {
            w::NotJoinable reason{};
            held = held || !(e.kind == w::EventKind::join ? engine_.joinable(reason) : engine_.membership_safe());
            if (held) {
                // 6.5: a LEAVE whose tick came plays the neutral, connected
                // pad from here, and the leader's pad answers its prompts
                // and windows, so the step the LEAVE waits for comes (an exit
                // prompt or a status window it left open has no time limit).
                if (e.kind == w::EventKind::leave) engine_.mark_leaving(e.slot);
                ++it;
                continue;
            }
        }
        const int slot = e.slot;
        switch (e.kind) {
        case w::EventKind::join:
            if (!engine_.join_player(slot, e.carry, e.point)) {
                // A carry the engine could not read: every member drops this
                // join alike (the same bytes).
                trace("join dropped inst=%u visit=%u tick=%u slot=%d (damaged carry)", inst_, visit_, tick_, slot);
                if (slot == slot_ && (mode_ == Mode::observing || mode_ == Mode::catching_up)) {
                    mode_ = Mode::closed;
                    close_reason_ = "Joining the group failed.";
                }
                break;
            }
            present_ = std::uint8_t(present_ | (1u << slot));
            if (slot != slot_ && slot_members_[std::size_t(slot)] == kNoMember) slot_members_[std::size_t(slot)] = roster_member_for(slot);
            trace("join applied inst=%u visit=%u tick=%u slot=%d present=%x%s", inst_, visit_, tick_, slot, unsigned(present_),
                  replaying ? " (replay)" : "");
            if (!replaying) log("join: slot %d joined at tick %u (present %x)", slot, tick_, unsigned(present_));
            if (!replaying && slot != slot_) notice(slot_name(slot) + " joined");
            if (slot == slot_ && mode_ == Mode::observing) become_member(now_);
            break;
        case w::EventKind::leave:
            trace("leave applied inst=%u visit=%u tick=%u slot=%d", inst_, visit_, tick_, slot);
            if (!replaying) log("leave: slot %d left at tick %u", slot, tick_);
            // (Only for a member that was there: a joiner whose carry every
            // engine dropped never was.)
            if (!replaying && slot != slot_ && (present_ & (1u << slot))) notice(slot_name(slot) + " left");
            engine_.remove_player(slot);
            present_ = std::uint8_t(present_ & ~(1u << slot));
            slot_members_[std::size_t(slot)] = kNoMember;
            break;
        case w::EventKind::intent: engine_.intent(slot, w::IntentKind(e.intent_kind), e.argument); break;
        // LEAD_FROM and DELAY are for the member that plays now: never from a
        // replayed history or while observing (a joiner before its JOIN). A
        // resync replays the member's own instance: a LEAD_FROM there that
        // the replay reaches is its own (the partner left during the
        // catch-up; finish_resync leads alone), and its own DELAY counts.
        case w::EventKind::lead_from:
            if (!replaying && mode_ == Mode::sealed) lead_from_ = e.tick;
            else if (replaying && resyncing_) resync_lead_ = e.tick;
            break;
        case w::EventKind::delay:
            if (slot == slot_ && replaying && resyncing_) set_delay(std::clamp(int(e.delay), 1, 15), "delay_replayed");
            if (slot == slot_ && !replaying && mode_ == Mode::sealed) {
                // The member repeats or skips one sample (6.4).
                const int d = std::clamp(int(e.delay), 1, 15);
                // A longer delay repeats the last sample for the ticks in
                // between; a shorter one skips sending until the sent pads
                // run out (before_step sends up to tick + d).
                if (d > delay_ && mode_ == Mode::sealed)
                    for (int k = delay_; k < d; ++k) send_input_ahead(last_own_);
                set_delay(d, "delay_event");
            }
            break;
        }
        it = events_.erase(it);
        changed = true;
    }
    return changed;
}

void Session::send_input_ahead(const w::WirePad& pad) {
    send(w::Type::input, w::encode(w::Input{inst_, visit_, input_next_, {pad}}));
    ++input_next_;
}

void Session::start_sealed(const w::WirePad& pad) {
    // Pads for the first d ticks go out at once (6.7), then one per step.
    input_next_ = tick_;
    skipped_buttons_ = 0;
    for (int k = 0; k < delay_; ++k) send_input_ahead(pad);
    pace_.reset();
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
            if (body.present & (1u << slot_)) stats_.executed(visit_, tick_, now, steps_, body.pads[std::size_t(slot_)]);
            if (input_next_ > tick_ + std::uint32_t(delay_)) {
                // A shorter delay skips samples until the sent pads run out
                // (6.4): their buttons go with the next pad sent, so a press
                // made meanwhile is late by a tick or two, never lost.
                skipped_buttons_ = std::uint16_t(skipped_buttons_ | own.buttons);
            }
            while (input_next_ <= tick_ + std::uint32_t(delay_)) {
                w::WirePad pad = own;
                pad.buttons = std::uint16_t(pad.buttons | skipped_buttons_);
                skipped_buttons_ = 0;
                stats_.sampled(visit_, input_next_, now, steps_, pad);
                send_input_ahead(pad);
            }
            last_own_ = own;
        }
        pads = pads_of(body);
        break;
    }
    case Mode::solo: {
        if (alone_pending_ && engine_.membership_safe()) {
            // Taken out of the group: the others' players go at the first
            // step that allows it (until then they stand, leaving).
            for (int s = 0; s < 4; ++s)
                if (alone_pending_ & (1u << s)) engine_.remove_player(s);
            trace("alone inst=%u tick=%u removed=%x", inst_, tick_, unsigned(alone_pending_));
            alone_pending_ = 0;
        }
        apply_events(false);
        w::WirePad pad = own;
        if (join_at_) {
            // Delayed sampling until J: this tick plays the sample taken d
            // ticks ago; the new sample is for tick + d (from J an INPUT).
            pad = delayed_.empty() ? last_own_ : delayed_.front();
            if (!delayed_.empty()) delayed_.pop_front();
            stats_.sampled(visit_, tick_ + std::uint32_t(delay_), now, steps_, own);
            if (tick_ + std::uint32_t(delay_) >= *join_at_) send_input_ahead(own);
            else delayed_.push_back(own);
        } else {
            stats_.sampled(visit_, tick_, now, steps_, own);
        }
        stats_.executed(visit_, tick_, now, steps_, pad, join_at_.has_value());
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
    ++steps_;
    note_mode();
    if (mode_ == Mode::transition) {
        if (engine_.load_serial() != serial_at_barrier_) begin_visit(now);
        return;
    }
    if (mode_ != Mode::solo && mode_ != Mode::sealed && mode_ != Mode::observing) return;
    if (mode_ != Mode::observing) track_figure(now);
    const std::uint32_t executed = tick_;
    const bool member = mode_ != Mode::observing;
    // A fork in this step: from here the two sides' engines differ (each has
    // despawned the other side), so the fork tick's net fingerprint is not
    // the instance's (a fork on a multiple of 60 made the relay resync the
    // stayer, S2 on real game clients).
    const auto split = engine_.take_split();
    if (member && !hub_ && executed % 60 == 0 && !split) {
        const auto fp = engine_.net_fingerprint();
        send(w::Type::netfp, w::encode(w::NetFp{inst_, visit_, executed, fp}));
        trace("netfp inst=%u visit=%u tick=%u mode=%s present=%x fp=%016llx", inst_, visit_, executed, mode_name(mode_), unsigned(present_),
              static_cast<unsigned long long>(fp));
    }
    ++tick_;
    if (!load_end_ && engine_.load_ended()) load_end_ = tick_;
    if (member && !hub_) checkpoint_tick(now);
    if (mode_ == Mode::sealed && lead_from_ && tick_ >= *lead_from_) {
        // LEAD_FROM: from this tick the remaining member leads alone (its own
        // clock, delay 0, LOG from here); nothing more is sealed.
        lead_from_.reset();
        mode_ = Mode::solo;
        ticks_.clear();
        log_first_ = tick_;
        log_.clear();
        mode_of_instance_ = wire::Mode::solo;
        trace("lead alone inst=%u visit=%u from tick=%u", inst_, visit_, tick_);
    }
    if (join_at_ && tick_ >= *join_at_) {
        // From J the instance is sealed; SEALED(J) brings the JOIN.
        join_at_.reset();
        flush_log(now, true);
        delayed_.clear();
        pace_.reset();
        mode_ = Mode::sealed;
        trace("sealed from J inst=%u visit=%u tick=%u", inst_, visit_, tick_);
        last_tick_at_ = now;
    }
    if (split) {
        const auto side = split->local_leaves ? std::uint8_t(split->leavers) : std::uint8_t(present_ & ~split->leavers);
        if (member) send(w::Type::split, w::encode(w::Split{inst_, visit_, executed, split->leavers,
                                                             split->in_stage ? w::SplitKind::in_stage : w::SplitKind::exit}));
        present_ = std::uint8_t(present_ & side);
        if (split->local_leaves && member && !lines_.empty()) {
            // This copy becomes the child: the parent line ends here (its
            // growth up to the fork), the child's counts from the fork under
            // the id REASSIGN brings (8.3).
            const auto values = engine_.story_values();
            auto& parent = lines_.back();
            std::map<std::int32_t, std::int32_t> grown;
            for (const auto c : engine::story::additive_counters()) {
                const auto it = values.counters.find(c);
                grown[c] = std::max(0, (it == values.counters.end() ? 0 : it->second) - parent.base[c]);
            }
            parent.final_growth = std::move(grown);
            CounterLine child;
            child.visit = visit_;
            child.awaiting_id = true;
            for (const auto c : engine::story::additive_counters()) {
                const auto it = values.counters.find(c);
                child.base[c] = it == values.counters.end() ? 0 : it->second;
            }
            lines_.push_back(std::move(child));
        }
        trace("split inst=%u visit=%u tick=%u child=%x local=%d present=%x", inst_, visit_, executed, unsigned(split->leavers),
              split->local_leaves ? 1 : 0, unsigned(present_));
        for (int s = 0; s < 4; ++s)
            if (!(present_ & (1u << s))) slot_members_[std::size_t(s)] = kNoMember;
        std::erase_if(events_, [&](const PendingEvent& e) {
            return e.event.kind != w::EventKind::lead_from && e.event.kind != w::EventKind::join && !(side & (1u << e.event.slot));
        });
        // (A side with one member left keeps executing sealed ticks until the
        // server's LEAD_FROM: from that tick it leads alone.)
    }
    // (Also from a hub instance: the lobby gate announces a field, 3.4.)
    if (const auto heading = engine_.take_heading(); heading && member) {
        send(w::Type::heading, w::encode(w::Heading{inst_, heading->stage, std::uint8_t(std::clamp(heading->point, 0, 255)),
                                                    engine_.session_context_hash()}));
        // (The scene and fade before its NEXTSTAGE show nowhere else: the
        // others' figure of this member does not come back at the gate.)
        depart(now);
    }
    if (engine_.supports_barriers()) {
        if (engine_.pending_stage()) park(now);
    } else if (member && engine_.load_serial() != serial_at_barrier_) {
        // An engine without the membership API: the stage load is reported
        // afterwards, the visit goes on as a solo NEW.
        serial_at_barrier_ = engine_.load_serial();
        flush_log(now, true);
        w::Barrier b{inst_, visit_, executed, engine_.stage(), std::uint8_t(std::clamp(engine_.pending_point(), 0, 255)), engine_.alive_mask(),
                     present_, engine_.session_context_hash(), {}};
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
    for (std::size_t at = 0; at < log_.size(); at += w::kMaxLogPads) {
        w::Log m{inst_, visit_, log_first_ + std::uint32_t(at), {}};
        m.pads.assign(log_.begin() + std::ptrdiff_t(at), log_.begin() + std::ptrdiff_t(std::min(log_.size(), at + w::kMaxLogPads)));
        send(w::Type::log, w::encode(m));
        if (!transport_) {
            // The connection broke in this frame: the pads from it on go
            // again after the reconnect's RESUME (the relay skips an overlap;
            // a gap would never close, and a joiner sealed from J would wait
            // for those ticks for good).
            log_.erase(log_.begin(), log_.begin() + std::ptrdiff_t(at));
            log_first_ += std::uint32_t(at);
            return;
        }
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
    send(w::Type::state, w::encode(w::State{inst_, visit_, tick_ ? tick_ - 1 : 0, flags, context, joinable ? w::NotJoinable::none : reason}));
    trace("report inst=%u visit=%u tick=%u joinable=%d reason=%u safe=%d boss=%d", inst_, visit_, tick_ ? tick_ - 1 : 0, joinable ? 1 : 0,
          joinable ? 0u : unsigned(reason), (flags & w::state_flag::membership_safe) ? 1 : 0, (flags & w::state_flag::boss_lock) ? 1 : 0);
}

// ---- barrier
void Session::park(double now) {
    // The member leaves its stage: its tag and figure go at once (no
    // presence goes until the next stage's).
    depart(now);
    flush_log(now, true);
    barrier_tick_ = tick_ - 1;
    serial_at_barrier_ = engine_.load_serial();
    group_at_barrier_ = present_;
    own_carry_ = engine_.carry(slot_);
    // The pending set goes first (the server merges it before it decides, so
    // BARRIER_OK brings it back in the room story); then everything refused
    // may be tried again.
    send_story(now, true);
    refused_flags_.clear();
    refused_folder_.clear();
    refused_phase_ = 0;
    w::Barrier b;
    b.inst = inst_;
    b.visit = visit_;
    b.tick = barrier_tick_;
    b.stage = *engine_.pending_stage();
    b.point = std::uint8_t(std::clamp(engine_.pending_point(), 0, 255));
    b.alive = engine_.alive_mask();
    b.present = present_;
    b.context = engine_.session_context_hash();
    b.carry = own_carry_;
    send(w::Type::barrier, w::encode(b));
    ticks_.clear(); // ticks sealed after B are discarded
    events_.clear();
    join_at_.reset();
    delayed_.clear();
    lead_from_.reset();
    previous_inst_.reset();
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
        // The room story the load step merges, and the values it removes
        // from this engine because the server dropped them (8.6).
        if (!ok.story.empty()) {
            try {
                const auto room = engine::story::decode_story(ok.story, ok.story_revision);
                const auto dropped = engine::story::decode_keys(ok.dropped);
                if (engine::story::holds_dropped(engine_.story_values(), room, dropped))
                    notice(not_started_dropped_ ? "Story progress was not saved: the room owner had not started its story yet."
                                                : "Story progress was not saved: the room owner is away.");
                take_story(room);
            } catch (const std::exception&) {
            }
        }
        if (ok.decision == w::Decision::join || ok.decision == w::Decision::co_arrive) {
            // The own engine still loads the stage, as a solo NEW: its load
            // snapshot is the fallback if the target moves on (6.6).
            w::BarrierOk solo = ok;
            solo.decision = w::Decision::new_instance;
            solo.slot_map = {-1, -1, -1, -1};
            solo.slot_map[std::size_t(slot_)] = 0;
            solo.co_arrivals.clear();
            solo.own_slot = 0;
            solo.mode = w::Mode::solo;
            engine_.set_barrier(solo);
        } else {
            engine_.set_barrier(ok);
        }
        take_decision(ok);
        loading_.clear();
        boss_choice_ = false;
        mode_ = Mode::transition;
        return;
    }
    if (!engine_.supports_barriers() && mode_ == Mode::solo && ok.inst == inst_ && ok.visit + 1 == visit_) {
        // The answer to a stage load reported afterwards (an engine without
        // the membership API): the visit goes on under the server's id.
        inst_ = ok.new_inst;
        visit_ = ok.new_visit;
        if (!hub_ && inst_ != 0) {
            const auto raw = engine_.snapshot_bytes(false);
            for (const auto& c : w::make_chunks(inst_, visit_, raw)) send(w::Type::snapshot, w::encode(c));
            send(w::Type::snaphash, w::encode(w::SnapHash{inst_, visit_, w::fnv1a64(engine_.snapshot_bytes(true))}));
        }
        return;
    }
    if (mode_ == Mode::waiting_state || mode_ == Mode::catching_up || mode_ == Mode::observing) {
        // The target moved on during the catch-up (6.6): a barrier into the
        // same stage makes this a co-arrival of the new visit (its state
        // follows); anything else: back to the own fallback, as NEW.
        decision_ = ok;
        take_decision(ok);
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
        slot_members_ = {member_id_, kNoMember, kNoMember, kNoMember};
        mode_of_instance_ = w::Mode::solo;
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
    stats_.forget_in_flight();
    load_end_.reset();
    next_statehash_ = config_.checkpoint_period;
    checkpoint_asked_ = false;
    from_checkpoint_ = false;
    serial_at_barrier_ = engine_.load_serial();
    hub_ = mode_of_instance_ == w::Mode::hub || is_hub_stage(engine_.stage());
    state_flags_ = 0xFF;
    open_line();
    engine_.set_view_slot(slot_);
    trace("visit inst=%u visit=%u stage=%s slot=%d present=%x mode=%u", inst_, visit_, engine_.stage().c_str(), slot_, unsigned(present_),
          unsigned(mode_of_instance_));
    const auto decision = decision_ ? decision_->decision : w::Decision::new_instance;
    const w::WirePad pad = last_own_;
    switch (decision) {
    case w::Decision::new_instance:
    case w::Decision::dissolve:
        if (!hub_ && inst_ != 0) {
            // The leader uploads the load snapshot, every member sends the
            // canonical hash (6.8).
            if (uploader_slot_ == slot_) {
                const auto raw = engine_.snapshot_bytes(false);
                for (const auto& c : w::make_chunks(inst_, visit_, raw)) send(w::Type::snapshot, w::encode(c));
            }
            const auto canonical = engine_.snapshot_bytes(true);
            send(w::Type::snaphash, w::encode(w::SnapHash{inst_, visit_, w::fnv1a64(canonical)}));
            trace("snaphash inst=%u visit=%u hash=%016llx new", inst_, visit_, static_cast<unsigned long long>(w::fnv1a64(canonical)));
        }
        if (mode_of_instance_ != w::Mode::sealed || decision == w::Decision::dissolve) {
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
    if (state.checkpoint && (state.purpose == w::StatePurpose::join || (state.purpose == w::StatePurpose::resync && resyncing_))) {
        if (rebuild_from_checkpoint(state, now)) return;
        // CHECKPOINT_BAD went out: the classic state comes next.
        mode_ = Mode::waiting_state;
        loading_ = "Joining...";
        return;
    }
    from_checkpoint_ = false;
    if (!engine_.restore(state.snapshot)) {
        // Damaged state: back to the own stage.
        notice("The group's state could not be loaded; you play this stage on your own.");
        if (!fallback_.empty() && engine_.restore(fallback_)) {
            engine_replaced_ = true;
            engine_.start_online(0, 1);
            slot_ = 0;
            present_ = 1;
            inst_ = 0;
            mode_of_instance_ = w::Mode::solo;
            decision_.reset();
            begin_visit(now);
        } else {
            mode_ = Mode::closed;
            close_reason_ = "The stage could not be restored.";
        }
        return;
    }
    engine_replaced_ = true;
    engine_.set_view_slot(slot_);
    trace("state purpose=%u inst=%u visit=%u slot=%u ticks=%zu forks=%zu", unsigned(state.purpose), state.inst, state.visit, unsigned(state.slot),
          state.ticks.size(), state.forks.size());
    tick_ = state.first;
    // The log, plus the LIVE / SEALED ticks that came meanwhile.
    for (std::size_t i = 0; i < state.ticks.size(); ++i) ticks_.emplace(state.first + std::uint32_t(i), std::move(state.ticks[i]));
    std::erase_if(ticks_, [&](const auto& t) { return t.first < state.first; });
    state_first_ = state.first;
    state_last_ = state.first + std::uint32_t(state.ticks.size());
    events_.clear();
    replay_fingerprints_.clear();
    hub_ = is_hub_stage(engine_.stage());
    open_line(); // the visit's line from its load (a joiner's growth includes the instance's: one line, taken once)
    const bool co_arrival = state.purpose == w::StatePurpose::co_arrival;
    if (co_arrival) {
        // The base spawned this member in its load step: a member from tick 0,
        // with the base's members and the other co-arrivals present (the
        // co-arrival's BARRIER_OK names only the own slot).
        present_ = engine_.present_mask();
        engine_.set_split_identity(slot_);
        engine_.set_replay(false);
        serial_at_barrier_ = engine_.load_serial();
        const auto hash = w::fnv1a64(engine_.snapshot_bytes(true));
        send(w::Type::snaphash, w::encode(w::SnapHash{inst_, visit_, hash}));
        trace("snaphash inst=%u visit=%u hash=%016llx co-arrival", inst_, visit_, static_cast<unsigned long long>(hash));
        loading_.clear();
        if (mode_of_instance_ != w::Mode::sealed) mode_ = Mode::solo;
        else start_sealed(last_own_);
        report_state(now, true);
        return;
    }
    slot_ = state.slot;
    engine_.set_view_slot(slot_);
    // The instance's members as the snapshot has them (the own slot joins by
    // its JOIN event).
    present_ = engine_.present_mask();
    engine_.set_split_identity(-1);
    engine_.set_fork_sides(state.forks);
    engine_.set_replay(true);
    replayed_steps_ = 0;
    replay_seconds_ = 0;
    starved_ = 0;
    starved_since_ = 0;
    catch_up_started_ = clock_seconds();
    mode_ = Mode::catching_up;
    run_catch_up(now);
}

void Session::run_catch_up(double now) {
    const double start = clock_seconds();
    // The wall clock of the catch-up counts the host's time between passes
    // (its picture, the frame pacing) but not the time it waited for ticks:
    // the rate it reports is the one the server can plan with.
    if (starved_since_ > 0) starved_ += start - starved_since_;
    starved_since_ = 0;
    std::uint32_t done = 0;
    while (mode_ == Mode::catching_up) {
        const auto it = ticks_.find(tick_);
        if (it == ticks_.end()) {
            if (const auto later = ticks_.upper_bound(tick_); later != ticks_.end() && gap_traced_ != tick_) {
                gap_traced_ = tick_;
                trace("catch-up waits for tick %u (next held %u, %zu held)", tick_, later->first, ticks_.size());
            }
            starved_since_ = clock_seconds();
            break;
        }
        // (DW4_ONLINE_RATE, test runs: a slower machine's replay rate.)
        if (config_.max_replay_rate > 0 &&
            double(replayed_steps_ + done) >= (clock_seconds() - catch_up_started_) * double(config_.max_replay_rate))
            break;
        // A record from the LEAD_FROM's tick on: the instance went on past it.
        if (resync_lead_ && tick_ >= *resync_lead_) resync_lead_.reset();
        w::TickBody body = std::move(it->second);
        ticks_.erase(it);
        for (auto& e : body.events) events_.push_back({tick_, std::move(e)});
        apply_events(true);
        engine_.replay_step(pads_of(body));
        (void)engine_.take_split(); // an observer keeps the side that keeps the instance id
        if (tick_ % 60 == 0) replay_fingerprints_[tick_] = engine_.net_fingerprint();
        ++tick_;
        ++done;
        if (!load_end_ && engine_.load_ended()) load_end_ = tick_;
        if (engine_.supports_barriers() && engine_.pending_stage()) {
            if (resyncing_) {
                // A resyncing member is still one of the instance: it reaches
                // the group's barrier like the others (after_step's park).
                send(w::Type::ready, w::encode(w::Ready{inst_, visit_, tick_ - 1, 0, config_.rate, own_carry_}));
                ready_sent_ = true;
                replay_seconds_ += clock_seconds() - start;
                finish_resync(now);
                park(now);
                return;
            }
            break; // a joiner: the server redirects (co-arrival or NEW)
        }
        if (config_.replay_budget > 0 && (done & 63) == 0 && clock_seconds() - start > config_.replay_budget) break;
    }
    replay_seconds_ += clock_seconds() - start;
    replayed_steps_ += done;
    const std::uint32_t newest = ticks_.empty() ? tick_ : ticks_.rbegin()->first + 1;
    const std::uint32_t remaining = newest > tick_ ? newest - tick_ : 0;
    // Steps per second of the wall clock (SEAMLESS.md 6.6): a pass replays
    // for config_.replay_budget, the host's picture and pacing take the
    // rest; the time the catch-up waited for ticks does not count.
    const double wall = clock_seconds() - catch_up_started_ - starved_ - (starved_since_ > 0 ? clock_seconds() - starved_since_ : 0.0);
    const double rate = wall > 0.05 && replayed_steps_ > 0 ? double(replayed_steps_) / wall : double(config_.rate);
    const double behind = rate > 60.0 ? double(remaining) / (rate - 59.94) : 0;
    // (From a checkpoint the few ticks since it go by at once: no countdown, 6.13.5.)
    loading_ = from_checkpoint_ ? std::string("Joining...") : "Joining... (catching up, about " + std::to_string(int(behind + 0.999)) + " s)";
    if (done > 0 && (tick_ / 600 != (tick_ - done) / 600 || (engine_.supports_barriers() && engine_.pending_stage())))
        trace("catch-up inst=%u visit=%u tick=%u remaining=%u rate=%.0f%s", inst_, visit_, tick_, remaining, rate,
              engine_.supports_barriers() && engine_.pending_stage() ? " (stage pending)" : "");
    if (!ready_sent_ && remaining <= 10 && (tick_ > state_first_ || from_checkpoint_) && !replay_fingerprints_.empty()) {
        const auto last = std::prev(replay_fingerprints_.end());
        if (replayed_steps_ > 600 && replay_seconds_ > 0) config_.rate = std::uint32_t(std::min(1e6, rate));
        w::Ready ready{inst_, visit_, last->first, last->second, config_.rate, own_carry_};
        if (from_checkpoint_) ready.flags = w::Ready::from_checkpoint;
        send(w::Type::ready, w::encode(ready));
        trace("ready inst=%u visit=%u tick=%u fp=%016llx rate=%u%s replayed=%llu catchup_ms=%.0f", inst_, visit_, last->first,
              static_cast<unsigned long long>(last->second), config_.rate, from_checkpoint_ ? " from-checkpoint" : "",
              static_cast<unsigned long long>(replayed_steps_), (clock_seconds() - catch_up_started_) * 1000.0);
        send(w::Type::rate, w::encode(config_.rate));
        ready_sent_ = true;
        engine_.set_replay(false);
        loading_.clear();
        pace_.reset();
        last_tick_at_ = now;
        if (resyncing_) {
            // A resync (6.9): this member never left the instance; no JOIN
            // comes, it sends its pads again from here (the server filled
            // them meanwhile).
            finish_resync(now);
            return;
        }
        mode_ = Mode::observing;
    }
}

void Session::finish_resync(double now) {
    resyncing_ = false;
    engine_.set_replay(false);
    engine_.set_split_identity(slot_);
    engine_.set_view_slot(slot_);
    loading_.clear();
    pace_.reset();
    last_tick_at_ = now;
    // (The pads held before the resync are no input of now: neutral until the next sample.)
    last_own_ = {};
    if (resync_lead_ && tick_ >= *resync_lead_) {
        // The partner left during the catch-up: the replay reached the
        // LEAD_FROM, nobody seals the ticks from here; this member leads
        // alone (before, it waited sealed for good).
        trace("resynced inst=%u visit=%u tick=%u slot=%d, leads alone", inst_, visit_, tick_, slot_);
        resync_lead_.reset();
        mode_ = Mode::solo;
        mode_of_instance_ = wire::Mode::solo;
        ticks_.clear();
        log_.clear();
        log_first_ = tick_;
        notice("Back in sync; you continue on your own.");
        return;
    }
    resync_lead_.reset();
    trace("resynced inst=%u visit=%u tick=%u slot=%d", inst_, visit_, tick_, slot_);
    input_next_ = tick_;
    skipped_buttons_ = 0;
    for (int k = 0; k < delay_; ++k) send_input_ahead(last_own_);
    mode_ = Mode::sealed;
    notice("Back in sync with the group.");
}

void Session::go_alone() {
    // 6.9 / 6.10: the server took this member out of its group (its LEAVE
    // came as a sealed tick it executes no more). Its engine plays on alone
    // from its own state, unregistered until its next barrier, where the
    // server takes it as an arrival of its own: the others' players go at
    // the first membership-safe step (marked leaving meanwhile: neutral pads,
    // their prompts closed), nothing is logged or compared.
    trace("alone inst=%u visit=%u tick=%u slot=%d present=%x", inst_, visit_, tick_, slot_, unsigned(present_));
    log("taken out of the group at tick %u (lag, too many resyncs or a missing hash): playing on alone", tick_);
    const std::uint8_t others = std::uint8_t(present_ & ~(1u << slot_));
    for (int s = 0; s < 4; ++s)
        if (others & (1u << s)) {
            engine_.mark_leaving(s);
            slot_members_[std::size_t(s)] = kNoMember;
        }
    alone_pending_ = others;
    present_ = std::uint8_t(1u << slot_);
    ticks_.clear();
    events_.clear();
    lead_from_.reset();
    join_at_.reset();
    delayed_.clear();
    previous_inst_.reset();
    resyncing_ = false;
    mode_ = Mode::solo;
    mode_of_instance_ = wire::Mode::solo;
    // Unregistered (instance 0, as after the barrier timeout's own NEW): no
    // LOG, NETFP or STATE, and its next BARRIER is taken as a lone arrival,
    // whose BARRIER_OK names instance 0.
    inst_ = 0;
    hub_ = true;
    log_.clear();
    log_first_ = tick_;
    notice("You were taken out of the group; you continue on your own and meet again at the next stage.");
}

void Session::become_member(double now) {
    // The JOIN for this member applied: from here it sends pads; the server
    // fills them until they arrive.
    engine_.set_split_identity(slot_);
    engine_.set_view_slot(slot_);
    trace("member inst=%u visit=%u tick=%u slot=%d", inst_, visit_, tick_, slot_);
    input_next_ = tick_ + 1;
    skipped_buttons_ = 0;
    for (int k = 0; k < delay_; ++k) send_input_ahead(last_own_);
    mode_ = Mode::sealed;
    last_tick_at_ = now;
    // Who is there: the roster's members of this instance (a JOIN names no
    // member), else the slots' known names.
    std::vector<std::string> names;
    for (const auto& [member, inst] : roster_inst_)
        if (inst == inst_ && member != member_id_) names.push_back(member_name(member));
    if (names.empty()) {
        const int other = lowest(std::uint8_t(present_ & ~(1u << slot_)));
        if (other >= 0 && slot_members_[std::size_t(other)] != kNoMember) names.push_back(slot_name(other));
    }
    std::string list;
    for (std::size_t i = 0; i < names.size(); ++i) list += (i ? (i + 1 == names.size() ? " and " : ", ") : "") + names[i];
    notice(list.empty() ? std::string("You joined the group") : "You joined " + list);
}

// ---- choices and presentation
void Session::choose_boss(w::Choice choice) {
    if (mode_ != Mode::parked) return;
    send(w::Type::barrier_choice, w::encode(w::BarrierChoice{inst_, choice}));
    boss_choice_ = false;
    parked_at_ = now_; // the five seconds count from the choice
}

void Session::send_intent(w::IntentKind kind, int argument) {
    if (mode_ != Mode::solo && mode_ != Mode::sealed) return;
    send(w::Type::intent, w::encode(w::Intent{inst_, visit_, kind, argument}));
}

void Session::send_heading(const std::string& stage, int point) {
    send(w::Type::heading, w::encode(w::Heading{inst_, stage, std::uint8_t(std::clamp(point, 0, 255)), engine_.session_context_hash()}));
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

// ---- room story (SEAMLESS.md 8.3)
void Session::take_story(const engine::RoomStory& story) {
    if (confirmed_ && story.revision < confirmed_->revision) return; // an older one overtaken in flight
    confirmed_ = story;
}

void Session::open_line() {
    CounterLine line;
    line.inst = inst_;
    line.visit = visit_;
    const auto values = engine_.story_values();
    for (const auto c : engine::story::additive_counters()) {
        const auto it = values.counters.find(c);
        line.base[c] = it == values.counters.end() ? 0 : it->second;
    }
    lines_.assign(1, std::move(line));
}

void Session::send_story(double now, bool at_barrier) {
    if (!confirmed_ || !hello_done_ || !transport_) return;
    if (ui_stage(engine_.stage())) return;
    const auto values = engine_.story_values();
    auto delta = engine::story::pending(*confirmed_, values);
    if (!at_barrier) {
        if (delta.phase && delta.phase <= refused_phase_) delta.phase = 0;
        std::erase_if(delta.flags, [&](std::int32_t f) { return refused_flags_.contains(f); });
        std::erase_if(delta.folder, [&](std::int32_t k) { return refused_folder_.contains(k); });
    }
    std::erase_if(lines_, [](const CounterLine& l) {
        if (!l.final_growth) return false;
        for (const auto& [c, g] : *l.final_growth)
            if (g > (l.taken.contains(c) ? l.taken.at(c) : 0)) return false;
        return true;
    });
    for (auto& line : lines_) {
        if (line.awaiting_id) continue;
        for (const auto c : engine::story::additive_counters()) {
            std::int32_t growth = 0;
            if (line.final_growth) {
                growth = line.final_growth->contains(c) ? line.final_growth->at(c) : 0;
            } else {
                const auto it = values.counters.find(c);
                growth = std::max(0, (it == values.counters.end() ? 0 : it->second) - line.base[c]);
            }
            if (growth > line.taken[c] && delta.counters.size() < w::kMaxStoryLines)
                delta.counters.push_back({c, line.inst, line.visit, growth});
        }
    }
    if (delta.empty()) {
        story_last_ = {};
        return;
    }
    const bool retry_due = story_retry_at_ > 0 && now >= story_retry_at_;
    if (!at_barrier && !retry_due && delta == story_last_ && now - story_sent_at_ < config_.story_interval) return;
    story_retry_at_ = 0;
    w::Story m{inst_, visit_, confirmed_->revision, std::move(delta)};
    send(w::Type::story, w::encode(m));
    trace("story inst=%u visit=%u rev=%u phase=%u flags=%zu keys=%zu counters=%zu%s", inst_, visit_, m.revision, unsigned(m.delta.phase),
          m.delta.flags.size(), m.delta.folder.size(), m.delta.counters.size(), at_barrier ? " barrier" : "");
    story_last_ = m.delta;
    story_sent_at_ = now;
    story_in_flight_.push_back(std::move(m));
    if (story_in_flight_.size() > 64) story_in_flight_.pop_front(); // (an old server that never answers)
}

void Session::on_story_ack(const w::StoryAck& ack) {
    if (!ack.story.empty()) take_story(engine::story::decode_story(ack.story, ack.revision));
    if (!(ack.flags & w::StoryAck::answer) || story_in_flight_.empty()) return;
    const auto sent = std::move(story_in_flight_.front());
    story_in_flight_.pop_front();
    std::set<std::int32_t> again; // counters refused for the rate, wait or not_member: sent again
    for (const auto& x : ack.refused) {
        if (x.reason == engine::StoryRefusal::rate) {
            if (x.kind == engine::StoryRefused::Kind::counter) again.insert(x.index);
            continue;
        }
        if (x.reason == engine::StoryRefusal::held) {
            if (!held_noticed_) notice("The room owner is away: your story progress is kept until they return.");
            held_noticed_ = true;
            continue; // the server keeps it; the confirmed story brings it once merged
        }
        // The owner has no online story yet and its game has not entered a stage: the room starts at zero (8.6), what is
        // done before is dropped like owner_away's drop (the barrier removes it).
        if (x.reason == engine::StoryRefusal::not_started) {
            not_started_dropped_ = true;
            if (!not_started_noticed_)
                notice("The room owner has not started its story yet: story progress made before that is not saved.");
            not_started_noticed_ = true;
        } else if (x.reason == engine::StoryRefusal::owner_away) {
            not_started_dropped_ = false;
        }
        if (x.reason == engine::StoryRefusal::wait || x.reason == engine::StoryRefusal::not_member) {
            // Not yet (the visit is too young or already gave its step; the
            // server has no stage for this member right now): sent again soon,
            // not only at the next barrier, where it would come from a stage
            // that does not set it.
            if (x.kind == engine::StoryRefused::Kind::counter) again.insert(x.index);
            story_retry_at_ = now_ + config_.story_retry_interval;
            continue;
        }
        switch (x.kind) {
        case engine::StoryRefused::Kind::phase: refused_phase_ = std::max<std::uint8_t>(refused_phase_, std::uint8_t(x.index)); break;
        case engine::StoryRefused::Kind::flag: refused_flags_.insert(x.index); break;
        case engine::StoryRefused::Kind::folder: refused_folder_.insert(x.index); break;
        case engine::StoryRefused::Kind::counter: break; // taken as done (dropped ones go back at the barrier)
        }
    }
    trace("story_ack rev=%u refused=%zu", ack.revision, ack.refused.size());
    for (const auto& g : sent.delta.counters) {
        if (again.contains(g.counter)) continue;
        for (auto& line : lines_)
            if (!line.awaiting_id && line.inst == g.inst && line.visit == g.visit) line.taken[g.counter] = std::max(line.taken[g.counter], g.growth);
    }
}

Overlay Session::overlay(double now) const {
    Overlay o;
    o.loading = loading_;
    o.boss_choice = boss_choice_;
    o.connection_lost = mode_ == Mode::lost || (resume_after_lost_ && !hello_done_);
    if (o.connection_lost && o.loading.empty() && mode_ == Mode::lost) o.loading = "Connection lost, reconnecting...";
    if (mode_ == Mode::catching_up && state_last_ > state_first_)
        o.progress = float(std::clamp(double(tick_ - state_first_) / double(std::max(1u, state_last_ - state_first_)), 0.0, 1.0));
    o.waiting_for_server = (mode_ == Mode::sealed || mode_ == Mode::observing) && now - last_tick_at_ > 0.5;
    for (int s = 0; s < 4; ++s)
        o.lagging[std::size_t(s)] = (present_ & (1u << s)) && slot_members_[std::size_t(s)] != kNoMember &&
                                    lagging_members_.contains(slot_members_[std::size_t(s)]);
    o.slow_uplink = lagging_members_.contains(member_id_);
    return o;
}

// ---- Round J: checkpoints and the join from one (docs/SEAMLESS.md 6.13.4-6.13.5)

// After a member's step: STATEHASH at the first state-safe tick at or after
// every checkpoint period (with the load end K), and the donor's upload
// asked for by CHECKPOINT_ASK at the next state-safe tick.
void Session::checkpoint_tick(double now) {
    (void)now;
    if (!hub_checkpoints_ || !load_end_ || inst_ == 0) return;
    bool due = tick_ >= next_statehash_;
    // A whole period passed without a report (a member since a moment, or
    // a stretch without a state-safe tick on every machine alike): the
    // reports go on with the next period.
    if (due && tick_ >= next_statehash_ + config_.checkpoint_period) {
        next_statehash_ = (tick_ / config_.checkpoint_period + 1) * config_.checkpoint_period;
        due = false;
    }
    if (!due && !checkpoint_asked_) return;
    if (!engine_.state_safe()) return;
    if (due) {
        const auto t0 = clock_seconds();
        const auto hash = engine_.tick_state_hash();
        send(w::Type::statehash, w::encode(w::StateHash{inst_, visit_, tick_, hash, *load_end_, 0}));
        // DW4_TRACE_TICKSTATE_DUMP=DIR (test runs): the canonical state of every STATEHASH, for
        // tools/online/tickstate_diff.py.
        if (const char* dir = std::getenv("DW4_TRACE_TICKSTATE_DUMP"); dir && *dir) {
            const auto bytes = engine_.save_tick_state(true);
            const std::string path = std::string(dir) + "/statehash_" + config_.own_name + "_" + std::to_string(inst_) + "_" +
                                     std::to_string(visit_) + "_" + std::to_string(tick_) + ".bin";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fwrite(bytes.data(), 1, bytes.size(), f);
                std::fclose(f);
            }
        }
        trace("statehash inst=%u visit=%u tick=%u hash=%016llx k=%u ms=%.2f", inst_, visit_, tick_, static_cast<unsigned long long>(hash),
              *load_end_, (clock_seconds() - t0) * 1000.0);
        next_statehash_ = (tick_ / config_.checkpoint_period + 1) * config_.checkpoint_period;
    }
    if (checkpoint_asked_) upload_checkpoint();
}

void Session::upload_checkpoint() {
    checkpoint_asked_ = false;
    const auto t0 = clock_seconds();
    std::vector<std::uint8_t> raw;
    std::uint64_t hash = 0;
    try {
        raw = engine_.save_tick_state(false);
        hash = engine_.tick_state_hash();
    } catch (const std::exception& error) {
        log("checkpoint not taken at tick %u: %s", tick_, error.what());
        return;
    }
    if (raw.empty()) return;
    std::vector<std::uint8_t> payload;
    if (config_.compress) {
        payload.push_back(1);
        const auto packed = config_.compress(raw);
        payload.insert(payload.end(), packed.begin(), packed.end());
    } else {
        payload.push_back(0);
        payload.insert(payload.end(), raw.begin(), raw.end());
    }
#ifdef DW4_ONLINE_DEV
    // DW4_ONLINE_TEST_BAD_CHECKPOINT=1 (development builds; scenario JB): the
    // upload is damaged on the way (one byte of the state flipped), so the
    // joiner's rebuild fails and the relay falls back.
    if (const char* bad = std::getenv("DW4_ONLINE_TEST_BAD_CHECKPOINT"); bad && *bad == '1' && payload.size() > 64)
        payload[payload.size() / 2] ^= 0x5A;
#endif
    constexpr std::size_t kChunk = 60 * 1024;
    for (std::size_t off = 0; off < payload.size(); off += kChunk) {
        w::Checkpoint c{inst_, visit_, tick_, hash, *load_end_, std::uint32_t(payload.size()), std::uint32_t(off), {}};
        c.bytes.assign(payload.begin() + std::ptrdiff_t(off), payload.begin() + std::ptrdiff_t(std::min(payload.size(), off + kChunk)));
        send(w::Type::checkpoint, w::encode(c));
    }
    trace("checkpoint inst=%u visit=%u tick=%u raw=%zu sent=%zu ms=%.2f", inst_, visit_, tick_, raw.size(), payload.size(),
          (clock_seconds() - t0) * 1000.0);
}

// A join from a checkpoint (6.13.2 steps 1-5): the visit's load snapshot,
// its load ticks [0, K) (the load end must come exactly at K), the tick
// state of T, its hash; then the ticks from T as a catch-up. false: the
// rebuild failed and CHECKPOINT_BAD went out (the classic state follows).
bool Session::rebuild_from_checkpoint(w::StateData& state, double now) {
    const double t0 = clock_seconds();
    auto bad = [&](w::CheckpointBadReason reason, const std::string& section) {
        send(w::Type::checkpoint_bad, w::encode(w::CheckpointBad{inst_, visit_, state.checkpoint_tick, reason, section.substr(0, 64)}));
        log("checkpoint of tick %u refused (%u): %s", state.checkpoint_tick, unsigned(reason), section.c_str());
        trace("checkpoint bad inst=%u visit=%u tick=%u reason=%u section=%s", inst_, visit_, state.checkpoint_tick, unsigned(reason),
              section.c_str());
        from_checkpoint_ = false;
        return false;
    };
    const auto& payload = *state.checkpoint;
    if (payload.empty()) return bad(w::CheckpointBadReason::decode, "payload");
    std::vector<std::uint8_t> raw;
    try {
        if (payload[0] == 0) raw.assign(payload.begin() + 1, payload.end());
        else if (payload[0] == 1 && config_.decompress) raw = config_.decompress(std::span(payload).subspan(1));
        else return bad(w::CheckpointBadReason::decode, "codec");
    } catch (const std::exception&) {
        return bad(w::CheckpointBadReason::decode, "decompress");
    }
    if (!engine_.restore(state.snapshot)) return bad(w::CheckpointBadReason::decode, "snapshot");
    engine_replaced_ = true;
    slot_ = state.slot;
    engine_.set_view_slot(slot_);
    present_ = engine_.present_mask();
    engine_.set_split_identity(-1);
    engine_.set_fork_sides(state.forks);
    engine_.set_replay(true);
    events_.clear();
    // The load ticks: the load end comes at K exactly (6.13.2 step 2).
    const std::uint32_t k = std::uint32_t(state.load_ticks.size());
    tick_ = 0;
    for (auto& body : state.load_ticks) {
        if (engine_.load_ended()) return bad(w::CheckpointBadReason::load_end, "early");
        for (auto& e : body.events) events_.push_back({tick_, std::move(e)});
        apply_events(true);
        engine_.replay_step(pads_of(body));
        (void)engine_.take_split();
        ++tick_;
    }
    if (!engine_.load_ended()) return bad(w::CheckpointBadReason::load_end, "late");
    if (const auto error = engine_.apply_tick_state(raw); !error.empty()) {
        const auto colon = error.find(':');
        const auto section = error.substr(0, colon);
        return bad(section == "header" ? w::CheckpointBadReason::version : w::CheckpointBadReason::topology, error);
    }
    if (engine_.tick_state_hash() != state.checkpoint_hash) return bad(w::CheckpointBadReason::hash, "hash");
    // At T: the ticks from T follow as a catch-up (a few, 6.13.5).
    from_checkpoint_ = true;
    load_end_ = k;
    tick_ = state.checkpoint_tick;
    next_statehash_ = (tick_ / config_.checkpoint_period + 1) * config_.checkpoint_period;
    rebuild_ms_ = (clock_seconds() - t0) * 1000.0;
    hub_ = is_hub_stage(engine_.stage());
    trace("state purpose=%u inst=%u visit=%u slot=%u checkpoint=%u k=%u ticks=%zu rebuild_ms=%.1f raw=%zu", unsigned(state.purpose), state.inst,
          state.visit, unsigned(state.slot), state.checkpoint_tick, k, state.ticks.size(), rebuild_ms_, raw.size());
    for (std::size_t i = 0; i < state.ticks.size(); ++i) ticks_.emplace(state.first + std::uint32_t(i), std::move(state.ticks[i]));
    std::erase_if(ticks_, [&](const auto& t) { return t.first < state.first; });
    state_first_ = state.first;
    state_last_ = state.first + std::uint32_t(state.ticks.size());
    events_.clear();
    replay_fingerprints_.clear();
    // (The state's own fingerprint: READY can go out after a few ticks.)
    if (tick_ > 0) replay_fingerprints_[tick_ - 1] = engine_.net_fingerprint();
    open_line();
    replayed_steps_ = 0;
    replay_seconds_ = 0;
    starved_ = 0;
    starved_since_ = 0;
    catch_up_started_ = t0;
    mode_ = Mode::catching_up;
    run_catch_up(now);
    return true;
}

} // namespace dw4::game::online
