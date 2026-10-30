#pragma once

// The DW4R2 client (docs/SEAMLESS.md 3.4, 6.3-6.10): one session per room,
// over one relay connection, across all of the member's instances.
//
//   solo        a one-member instance: the member steps on its own clock with
//               input delay 0 and streams the pads it applied (LOG, every
//               100 ms; hub stages are not logged);
//   sealed      two or more members: every machine executes only the ticks
//               the server sealed (SEALED), its own pad included, at its own
//               59.94 Hz pace within +-0.1 % of the server's; it sends its pad
//               d ticks ahead (INPUT);
//   parked      after the barrier tick B (the step that executed NEXTSTAGE)
//               until BARRIER_OK (5 s for a solo group, then NEW on its own);
//   transition  the module reload, held fields and the load step, with the
//               pads of B for the present slots; they consume no ticks;
//   waiting_state / catching_up / observing
//               a mid-stage join or co-arrival: the own load snapshot is kept
//               as the fallback, the instance's snapshot restored and the log
//               replayed as an observer (no pictures, no port events, the
//               music position kept), READY within ten ticks of the newest,
//               then live ticks until the JOIN event spawns the member;
//   lost        the relay connection broke: a sealed member freezes ("Connection
//               lost, reconnecting..."), a solo member plays on.
//
// Live steps run in the host loop: before_step() gives the pads of the step
// (and applies the membership events that are due, between steps, where the
// instance allows them), the host steps the engine, after_step() reports
// (LOG, NETFP every 60 ticks, STATE, SPLIT, BARRIER). Catch-up steps run in
// poll().
#include "engine_link.hpp"
#include "pacing.hpp"
#include "presence.hpp"
#include "transport.hpp"
#include "wire.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dw4::game::online {

struct SessionConfig {
    std::string ticket;
    std::uint32_t protocol = 0;
    std::uint64_t data_hash = 0, settings_hash = 0;
    std::string own_name;
    std::uint32_t rate = 3000;          // replay steps per second until measured (RATE)
    double barrier_timeout = 5.0;       // a solo group goes on as NEW without an answer (3.4)
    double replay_budget = 0.012;       // seconds of catch-up per poll (0: until caught up)
    double presence_interval = 0.2;     // PRESENCE five times per second
    double log_interval = 0.1;          // LOG batches every 100 ms
    std::size_t state_limit = 64u << 20; // largest state transfer accepted
    bool trace = false;                 // DW4_ONLINE_TRACE: decisions, membership, NETFP lines on stdout (test runs)
    std::uint32_t max_replay_rate = 0;  // DW4_ONLINE_RATE (test runs): replay steps per second at most (0: as fast as it goes)
};

enum class Mode : std::uint8_t { connecting, solo, sealed, parked, transition, waiting_state, catching_up, observing, lost, closed };
const char* mode_name(Mode mode) noexcept;

struct RemoteMember {
    std::string name;
    wire::Presence presence;
    double received_at = -1;
};

struct Overlay {
    std::string loading;        // the loading screen's line (barrier holds, catch-up)
    float progress = -1;        // catch-up progress 0..1 (-1: none)
    bool connection_lost = false;
    bool waiting_for_server = false; // sealed: no tick for more than half a second
    bool slow_uplink = false;   // the server fills this member's pads
    std::array<bool, 4> lagging{};
    bool boss_choice = false;   // "Wait for <names>" / "Go alone" on the loading screen
};

class Session {
public:
    Session(EngineLink& engine, SessionConfig config);

    // A connected transport (the first one, or after a reconnect with a new
    // ticket): the hello line goes out, a reconnect sends RESUME after OK.
    void attach(std::unique_ptr<Transport> transport, double now, std::string ticket = {});
    // Network in, messages, catch-up steps, timers. Call every loop pass.
    void poll(double now);
    // How many live steps the host runs now: `host_steps` is what its own
    // clock would run, `elapsed` the real seconds since the last pass.
    int steps_allowed(int host_steps, double elapsed);
    // The session paces the steps (sealed / observing): the host keeps no
    // accumulator of its own.
    bool paces() const { return mode_ == Mode::sealed || mode_ == Mode::observing; }
    // The next live step may run now (the host checks it before each step of
    // a pass: a barrier, a missing sealed tick or a restore stops the pass).
    bool step_ready() const;
    std::array<PadSample, 4> before_step(const PadSample& local, double now);
    void after_step(double now);

    // Presentation.
    bool backlog_run() const { return pace_.catching_up(); } // no pictures for these steps
    double audio_ratio() const { return paces() ? pace_.ratio() : 1.0; }
    bool take_engine_replaced();
    std::vector<std::string> take_notices();
    Overlay overlay(double now) const;
    const std::map<std::uint32_t, RemoteMember>& remote() const { return remote_; }
    // Name tags of room members outside the own instance, interpolated.
    std::vector<RemoteSample> remote_tags(double now) const { return tracks_.sample(now); }
    std::string member_name(std::uint32_t member) const;
    // The relay's ROSTER (once per second): every room member's stage (empty:
    // hidden), instance, level, HP and flags (1 connected, 2 lagging, 4
    // owner), for the room view's areas (docs/SEAMLESS.md 10, 12).
    const std::vector<wire::RosterEntry>& roster() const { return roster_; }

    // Facts.
    Mode mode() const { return mode_; }
    bool connected() const { return hello_done_ && transport_ != nullptr; }
    bool closed() const { return mode_ == Mode::closed; }
    std::string close_reason() const { return close_reason_; }
    std::uint32_t member_id() const { return member_id_; }
    int own_slot() const { return slot_; }
    std::uint8_t present() const { return present_; }
    std::uint32_t inst() const { return inst_; }
    std::uint32_t visit() const { return visit_; }
    std::uint32_t tick() const { return tick_; }
    int delay() const { return delay_; }
    bool hub() const { return hub_; }
    std::uint32_t measured_rate() const { return config_.rate; }
    // The host must supply a new connection (lost, reconnecting).
    bool wants_transport() const { return mode_ == Mode::lost || (resume_after_lost_ && !transport_); }

    // The member's own choices.
    void choose_boss(wire::Choice choice);
    void send_intent(wire::IntentKind kind, int argument);
    void send_heading(const std::string& stage, int point);
    void set_rate(std::uint32_t steps_per_second);
    void leave(const std::string& reason = "left");

private:
    struct PendingEvent {
        std::uint32_t tick;
        wire::Event event;
    };
    void send(wire::Type type, const std::vector<std::uint8_t>& payload);
    void handle(const wire::Frame& frame, double now);
    void on_welcome(const wire::Welcome& welcome, double now);
    // The new instance of a BARRIER_OK: ids, slot, members, mode, delay, the
    // snapshot's uploader, the notice.
    void take_decision(const wire::BarrierOk& ok);
    void on_sealed(std::uint32_t inst, std::uint32_t visit, std::uint32_t tick, wire::TickBody body, double now);
    void on_barrier_ok(const wire::BarrierOk& ok, double now);
    void on_state(wire::StateData state, double now);
    void lose_connection(double now);
    bool apply_events(bool replaying);
    std::array<PadSample, 4> pads_of(const wire::TickBody& body) const;
    void flush_log(double now, bool force);
    void begin_visit(double now);
    void park(double now);
    void report_state(double now, bool force);
    void become_member(double now);
    void finish_resync(double now);
    std::uint32_t roster_member_for(int slot) const;
    void run_catch_up(double now);
    void send_input_ahead(const wire::WirePad& pad);
    void start_sealed(const wire::WirePad& pad);
    void notice(std::string text);
    void trace(const char* format, ...) const;
    std::string slot_name(int slot) const;

    EngineLink& engine_;
    SessionConfig config_;
    std::unique_ptr<Transport> transport_;
    wire::FrameReader reader_;
    std::vector<std::uint8_t> inbox_;
    std::string line_;
    bool hello_done_ = false;
    bool resume_after_lost_ = false;
    Mode mode_ = Mode::connecting;
    Mode mode_before_lost_ = Mode::solo;
    std::string close_reason_;
    std::uint32_t member_id_ = 0;

    // The instance.
    std::uint32_t inst_ = 0, visit_ = 0;
    std::optional<std::uint32_t> previous_inst_; // after REASSIGN: frames in flight with the old id
    wire::Mode mode_of_instance_ = wire::Mode::hub;
    int uploader_slot_ = 0;                     // the slot that uploads the visit's snapshot
    int slot_ = 0;
    std::uint8_t present_ = 1;
    int delay_ = 2;
    std::uint32_t tick_ = 0;            // the next tick of this visit to execute
    bool hub_ = true;
    static constexpr std::uint32_t kNoMember = 0xFFFFFFFFu;
    std::array<std::uint32_t, 4> slot_members_{kNoMember, kNoMember, kNoMember, kNoMember}; // room member index per slot
    std::map<std::uint32_t, wire::TickBody> ticks_; // sealed / live ticks not executed yet
    std::deque<PendingEvent> events_;
    std::array<PadSample, 4> last_pads_{};
    wire::WirePad last_own_{};
    std::optional<std::uint32_t> lead_from_;

    // Solo leader: the pads applied since the last LOG, and the switch to
    // sealed at J (JOIN_REQ / JOIN_AT).
    std::vector<wire::WirePad> log_;
    std::uint32_t log_first_ = 0;
    double log_sent_at_ = 0;
    std::optional<std::uint32_t> join_at_;
    std::deque<wire::WirePad> delayed_;
    std::uint32_t input_next_ = 0;      // sealed: the next tick this member sends a pad for

    // Barrier.
    std::uint32_t barrier_tick_ = 0;
    std::uint64_t serial_at_barrier_ = 0;
    std::optional<wire::BarrierOk> decision_;
    double parked_at_ = 0;
    std::uint8_t group_at_barrier_ = 1;
    std::vector<std::uint8_t> own_carry_;
    std::string loading_;
    bool boss_choice_ = false;

    // Join / co-arrival.
    std::vector<std::uint8_t> fallback_;
    std::optional<wire::StateData> stashed_state_;
    wire::ChunkAssembler assembler_;
    std::uint32_t state_first_ = 0, state_last_ = 0;
    std::map<std::uint32_t, std::uint64_t> replay_fingerprints_;
    bool ready_sent_ = false;
    bool resyncing_ = false;            // RESYNC: the catch-up ends as a member again, without a JOIN
    std::uint64_t replayed_steps_ = 0;
    double replay_seconds_ = 0;
    double catch_up_started_ = 0;
    std::uint32_t gap_traced_ = 0xFFFFFFFFu; // the tick a catch-up waits for, traced once

    // Reports.
    std::uint8_t state_flags_ = 0xFF;
    std::uint64_t state_context_ = 0;
    double state_sent_at_ = -100;
    bool state_pending_ = false;
    double presence_at_ = -100;

    // Pacing and lag (ROOM / ROSTER lagging flags by room member).
    PaceController pace_;
    std::set<std::uint32_t> lagging_members_;
    std::set<std::uint32_t> known_members_;
    std::set<std::uint32_t> disconnected_members_;      // ROOM disconnected, not connected again yet
    std::map<std::uint32_t, std::uint32_t> roster_inst_; // ROSTER: room member -> its instance (or join target)
    std::vector<wire::RosterEntry> roster_;              // the last ROSTER as it came
    bool hello_seen_ = false;
    double last_tick_at_ = 0;
    double now_ = 0;

    std::map<std::uint32_t, RemoteMember> remote_;
    PresenceTracks tracks_;
    std::map<std::uint32_t, std::string> names_;
    std::vector<std::string> notices_;
    bool engine_replaced_ = false;
};

} // namespace dw4::game::online
