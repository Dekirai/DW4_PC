#pragma once

// The DW4R2 client (docs/SEAMLESS.md 3.4, 6.3-6.10; round I1a's world, 19):
// one world session per account, over one relay connection, across all of the
// member's stage groups (the wire's "instances", `inst`) at home and in the
// instances (the user's instances, `wid`) it joins.
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
#include "net_log.hpp"
#include "net_stats.hpp"
#include "pacing.hpp"
#include "presence.hpp"
#include "prompt_wire.hpp"
#include "transport.hpp"
#include "wire.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dw4::game::online {

// Round I1b (19.24): development builds answer the arrival prompt without a
// person (DW4_ONLINE_AUTOCHOICE): first = the first choosable row by 19.7.3's
// order (New instance when none), new = New instance, cursor = the
// preselection, back = B.
enum class AutoChoice : std::uint8_t { none, first, new_instance, cursor, back };

struct SessionConfig {
    std::string ticket;
    std::uint32_t protocol = 0;
    std::uint64_t data_hash = 0, settings_hash = 0;
    std::string own_name;
    std::uint32_t rate = 3000;          // replay steps per second until measured (RATE)
    double barrier_timeout = 5.0;       // a solo group goes on as NEW without an answer (3.4)
    double replay_budget = 0.05;        // seconds of catch-up per poll (0: until caught up); the loading screen
                                        // still gets about 15 pictures a second
    double presence_interval = 0.2;     // PRESENCE five times per second
    double presence_moving_interval = 0.1; // PRESENCE2 ten times per second while the figure moves
    double log_interval = 0.1;          // LOG batches every 100 ms
    std::size_t state_limit = 64u << 20; // largest state transfer accepted
    bool trace = false;                 // DW4_ONLINE_TRACE: decisions, membership, NETFP lines on stdout (test runs)
    std::uint32_t max_replay_rate = 0;  // DW4_ONLINE_RATE (test runs): replay steps per second at most (0: as fast as it goes)
    double story_check_interval = 1.0;  // the pending set is looked at once a second (sent when it changed)
    double story_interval = 10.0;       // and sent again every ten seconds while it is not empty (8.3)
    double story_retry_interval = 3.0;  // after a wait / not_member answer: sent again this soon (8.7)
    // Round I1a (19.8.7, 19.18): the member's own settings (its Game Start's
    // difficulty, its Online > Settings cheats), sent with every BARRIER; the
    // bound box (the own story owner key is (member id, box)).
    wire::Settings own_settings;
    std::uint8_t box = 1;
    // DW4_ONLINE_AUTOCHOICE=new (development builds, 19.24): a BARRIER that
    // leaves a hub stage asks for a new instance (plan id 0xFFFF).
    bool plan_new_instance = false;
    // Round I1b, the arrival prompt (19.7.4): the gate form holds the own
    // player's confirming press while the engine names a departure from home
    // (off: every arrival gets the loading-screen form, DW4_ONLINE_TEST_GATE_
    // FORM=0, W11b); World:PlanWaitMs, after which a held press goes out
    // without an answer; the development builds' automatic answer and its
    // delay (a picture of the prompt in a test run).
    bool gate_form = true;
    double plan_wait = 1.0;
    AutoChoice auto_choice = AutoChoice::none;
    // A sequence instead (DW4_ONLINE_AUTOCHOICE=back,first): each prompt
    // takes the next answer, the last one stays (W30c: B, then the first row).
    std::vector<AutoChoice> auto_sequence;
    double auto_choice_delay = 0;
    // A stage folder as the player reads it ("Dry Land: Mummy Desert"; the
    // game's area table). Unset: the folder.
    std::function<std::string(std::string_view)> stage_name;
    // Round J (6.13): a checkpoint's compression (the app's zstd); unset:
    // the bytes go raw. A payload starts with its codec byte (0 raw, 1 this).
    std::function<std::vector<std::uint8_t>(std::span<const std::uint8_t>)> compress, decompress;
    std::uint32_t checkpoint_period = 600; // STATEHASH at the first state-safe tick at or after every multiple
};

enum class Mode : std::uint8_t { connecting, solo, sealed, parked, transition, waiting_state, catching_up, observing, lost, closed };
const char* mode_name(Mode mode) noexcept;

// The network facts of this member's session (the in-game indicator, F1 >
// Online, saves/online.log): what the relay reports and what this machine
// measured. Negative: not known (yet).
struct NetView {
    double rtt_ms = -1;           // round trip to the relay (the relay's NETSTAT, else the socket's TCP estimate)
    // How long a PING waits here for this game's next pass (half the time
    // between the passes it came between, the median of the newest 31): the
    // relay's round trip holds it, the network's does not (at 60 pictures a
    // second about 8 ms; round 62 review: "Ping 34 ms" at 24 ms of network).
    double pass_wait_ms = -1;
    double rtt_spread_ms = -1;    // its spread (p95 - median) as the relay saw it
    int suggested_delay = 0;      // the relay's suggestion (PING) for the next sealed start
    int margin_ms = 0;            // the relay's arrival margin of this member's pads (0.1 % quantile, PING)
    std::uint64_t late = 0, filled = 0, sealed = 0; // this member's pads in the current sealed stream (NETSTAT)
    bool from_relay = false;      // the relay sent NETSTAT (a server of this round or later)
    double tcp_rtt_ms = -1;       // the socket's TCP round-trip estimate (the network alone)
    long long tcp_loss = -1;      // TCP loss episodes on the connection (fast retransmissions, timeouts)
    int reconnects = 0;           // connections lost in this session
    int resyncs = 0;              // RESYNCs this member went through
    int skipped_frames = 0;       // server frames of a known type that did not decode and were skipped (informational only)
    // The network's round trip as a ping would show it: the relay's minus the
    // wait for this game's pass, or the socket's TCP estimate; negative: not known.
    double network_rtt_ms() const {
        if (rtt_ms < 0) return rtt_ms;
        return from_relay && pass_wait_ms > 0 ? std::max(0.0, rtt_ms - pass_wait_ms) : rtt_ms;
    }
};

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
    // A running line while the game plays on (19.5.5: "Kim lost the
    // connection. Waiting for Kim... 52 s", counting down).
    std::string status;
};

// Round I1b, the arrival prompt as the overlay draws it (19.5.3, 19.7.3-19.7.5):
// the gate form over the frozen game where the gate's list was, or the
// loading-screen form on the own loading screen of a held barrier. The rows
// come in the relay's order (decision 40); `cursor` == rows.size() is *New
// instance*.
enum class PromptForm : std::uint8_t { gate, loading };
struct PromptView {
    PromptForm form = PromptForm::gate;
    std::string stage;                  // the arrival stage (folder)
    std::vector<wire::OptionRow> rows;
    std::size_t cursor = 0;
    // The player moved the cursor (up / down) or changed *New instance*'s
    // visibility since the prompt opened: a refresh keeps the cursor he moved
    // on its instance (or on *New instance*); an unmoved cursor takes the
    // refresh's preselection (decision 40: a row that lost its claim to it is
    // never kept preselected; round I1b's second review).
    bool moved = false;
    wire::Visibility visibility = wire::Visibility::public_; // *New instance*'s (Left / Right)
    bool details = false;               // X
    std::string notice;                 // "Kim's instance is full now."
    std::string notice_host;            // the host the notice names: kept through refreshes while no row of his is choosable
    int rtt_ms = -1;                    // the own round trip (the header's "Your ping"); -1 unknown
    double opened_at = 0;
    bool new_instance() const { return cursor >= rows.size(); }
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

    // saves/online.log (net_log.hpp): the session's events and its link every
    // ten seconds (nothing secret). Not owned; null: none.
    void set_log(OnlineLog* log) { online_log_ = log; }
    // A picture was presented (the input latency of the ticks that ran since the last one).
    void presented(double now) { stats_.presented(now); }
    const NetStats& stats() const { return stats_; }
    const NetView& net() const { return net_; }
    // Steps the pacing ran without a picture (backlogs) and steps that found
    // no sealed tick when due (diagnostics).
    std::size_t backlog_runs() const { return backlog_runs_; }
    std::size_t starved_steps() const { return pace_.starved(); }
    std::size_t drained_steps() const { return pace_.drained(); }
    double playout_level() const { return pace_.level(); }

    // Presentation.
    bool backlog_run() const { return pace_.catching_up(); } // no pictures for these steps
    double audio_ratio() const { return paces() ? pace_.ratio() : 1.0; }
    bool take_engine_replaced();
    std::vector<std::string> take_notices();
    Overlay overlay(double now) const;
    const std::map<std::uint32_t, RemoteMember>& remote() const { return remote_; }
    // Name tags of room members outside the own instance, interpolated (and
    // in the hub stages their ghost figures, from PRESENCE2).
    std::vector<RemoteSample> remote_tags(double now) const { return tracks_.sample(now); }
    // The hub takes PRESENCE2 (its WELCOME said so): this member sends it.
    bool presence_v2() const { return presence_v2_; }
    std::string member_name(std::uint32_t member) const;
    // The relation flags of a member's last PRESENCE / MEMBER_INFO (wire::
    // MemberInfo: 1 friend, 2 party, 4 staff tag, 8 in the own instance).
    std::uint8_t member_flags(std::uint32_t member) const;
    // Round I1a: the own instance as the relay last described it
    // (INSTANCE_INFO; wid 0 = home), and the instance this engine is in
    // (BARRIER_OK's wid; 0 = home).
    const std::optional<wire::InstanceInfo>& instance() const { return instance_; }
    std::uint32_t wid() const { return wid_; }
    bool in_instance() const { return wid_ != 0; }
    bool is_host() const { return wid_ != 0 && host_ == member_id_; }
    std::uint32_t host() const { return host_; }
    // The instance's members for the Discord party and the F1 page: the
    // players of INSTANCE_INFO (1 at home).
    int instance_players() const;
    // The relay no longer knows this engine's instance (19.6.10): the engine
    // takes the home departure itself at the next membership-safe step.
    bool going_home() const { return instance_gone_; }
    // The story owner key of the current visit (19.8.4: the host's in an
    // instance, the own at home; {0xFFFFFFFF, 0} while the engine holds a
    // story whose owner key is no longer known, after a server restart or a
    // lost instance, until the DISSOLVE's replace) and whether the engine
    // holds the member's own story.
    std::pair<std::uint32_t, std::uint8_t> visit_owner() const { return visit_owner_; }
    bool holds_own_story() const { return visit_owner_ == std::make_pair(member_id_, own_box_); }

    // Facts.
    Mode mode() const { return mode_; }
    bool connected() const { return hello_done_ && transport_ != nullptr; }
    bool closed() const { return mode_ == Mode::closed; }
    std::string close_reason() const { return close_reason_; }
    // The relay closed the session because a newer connection of the same
    // account took its place (another PC pressed Play), or the account may
    // not play (locked, banned, deleted): no reconnect either way.
    bool replaced() const { return close_code_ == wireCloseReplaced; }
    // Blocks until the relay answered the HELLO (true), refused it or the
    // connection was lost (false, closed() tells), or `timeout` seconds passed
    // (false). Frames after the answer wait for the next poll.
    bool await_hello(double timeout);
    std::uint32_t member_id() const { return member_id_; }
    int own_slot() const { return slot_; }
    std::uint8_t present() const { return present_; }
    std::uint32_t inst() const { return inst_; }
    std::uint32_t visit() const { return visit_; }
    std::uint32_t tick() const { return tick_; }
    // The tick at which this member's own JOIN applied in this visit (none
    // until then, none in a visit it did not join), and whether its engine
    // came from a checkpoint (6.13.5) that no classic state replaced since.
    std::optional<std::uint32_t> joined_tick() const { return joined_tick_; }
    bool from_checkpoint() const { return from_checkpoint_; }
    int delay() const { return delay_; }
    bool hub() const { return hub_; }
    std::uint32_t measured_rate() const { return config_.rate; }
    // The member's own confirmed story (19.8.4: the keeper with the own owner
    // key; STORY_ACK after WELCOME and on every revision, a DISSOLVE's
    // replace). Nothing until the server sent one. The title applies it at
    // game_started (8.5): a Game Start is always at home.
    const std::optional<engine::RoomStory>& confirmed_story() const { return own_confirmed_; }
    // The confirmed story of the current visit's owner (the host's in an
    // instance, the own at home): the pending set's base.
    const std::optional<engine::RoomStory>& visit_story() const { return confirmed_; }
    // The Game Start's hold (round G-core, 8.8): no live step while set (the
    // title is a solo instance: as a loading hitch), the loading line "Loading
    // the room story..." meanwhile. The host sets it when a Game Start finds no
    // confirmed story and clears it once the story came.
    void hold_for_story(bool on) { story_hold_ = on; }
    bool story_held() const { return story_hold_; }
    // The host must supply a new connection (lost, reconnecting).
    bool wants_transport() const { return mode_ == Mode::lost || (resume_after_lost_ && !transport_); }

    // Round I1b: the arrival prompt (19.7), open in either form; nothing while
    // none is open. prompt_input takes the own pad's held buttons once per
    // picture (its presses: D-pad / stick up and down the rows, left and
    // right the visibility on *New instance*, A choose, B back, X details);
    // the game gets neutral pads while the gate form holds its press.
    const std::optional<PromptView>& prompt() const { return prompt_; }
    void prompt_input(std::uint32_t buttons, double now);
    // The gate form holds a press (waiting for OPTIONS, the overlay open, or
    // its answer going out); before_step gives the engine neutral pads
    // meanwhile. An observer (the session tests).
    bool holding_press() const { return gate_phase_ != GatePhase::idle; }

    // The member's own choices.
    void choose_boss(wire::Choice choice);
    void send_intent(wire::IntentKind kind, int argument);
    // Round I1a: *Leave instance* (INTENT 3: the relay stamps HOME for the own
    // slot; for the host it ends the instance) and the host's visibility
    // (INSTANCE_OP 1). False outside an instance. While the INTENT cannot go
    // (a barrier, a load, a catch-up, an engine playing on apart from its
    // instance after a reconnect, whose INTENT the relay would ignore) the
    // wish is kept and sent once it can, with a notice.
    bool leave_instance();
    bool leave_pending() const { return leave_wanted_; }
    bool set_visibility(wire::Visibility visibility);
    // The own settings change (the Game Start's difficulty, Online > Settings).
    void set_own_settings(const wire::Settings& settings) { config_.own_settings = settings; }
    void send_heading(const std::string& stage, int point);
    void set_rate(std::uint32_t steps_per_second);
    void leave(const std::string& reason = "left");
    // Development builds' scenario hook (DW4_ONLINE_TEST_CUT_AT_PARK, W7e): the
    // link is lost here and now, as a cut cable would lose it.
    void test_cut(double now) { lose_connection(now); }

private:
    // Round I1b (session_prompt.cpp): the gate form's input hold of the own
    // confirming press (19.7.4), PLAN / OPTIONS / CHOICE, the loading-screen
    // form at a held barrier (19.7.5), the automatic answer of test runs.
    enum class GatePhase : std::uint8_t { idle, waiting, open, release, cancel };
    PadSample gate_filter(const PadSample& local, double now);
    std::array<PadSample, 4> step_pads(const PadSample& local, double now);
    void send_plan(double now);
    void withdraw_plan(std::uint16_t plan, const char* why);
    void on_options(const wire::Options& options, double now);
    void open_prompt(PromptForm form, const wire::Options& options, double now);
    void answer_prompt(bool back, double now);
    void close_prompt(const char* why);
    void auto_answer(double now);
    void prompt_lost(double now);
    std::string prompt_rows_text() const;
    GatePhase gate_phase_ = GatePhase::idle;
    std::uint16_t gate_plan_ = 0;       // the plan id of the held press (0 while its PLAN waits for the throttle)
    double gate_since_ = 0;             // when the press was held
    engine::DepartureChoice gate_target_;
    std::optional<engine::DepartureChoice> last_departure_; // the named departure the own press last started (also without
    double last_departure_at_ = 0;                           // the gate form): *Back to the lobby*'s point next to its mat
    std::uint16_t next_plan_ = 1;
    double plan_sent_at_ = -100;        // PLAN's throttle: one a second
    std::uint16_t departure_plan_ = 0;  // the plan id the next BARRIER from home names
    bool engine_a_ = false;             // the engine's last own pad had A held (a press is its rising edge)
    std::optional<PromptView> prompt_;
    std::uint16_t prompt_plan_ = 0;     // the open gate form's plan id
    bool prompt_hold_ = false;          // BARRIER_WAIT choice: the relay holds the barrier for the prompt (no 5 s fallback)
    std::uint32_t prompt_buttons_ = 0;  // prompt_input's last buttons (its presses are the new ones)
    double auto_answer_at_ = -1;        // the automatic answer is due (test runs)
    AutoChoice auto_now_ = AutoChoice::none; // and which one
    bool chosen_join_ = false;          // the arrival came from a row the player chose (the toasts)
    bool chosen_here_ = false;          // and that row had players at the arrival stage ("Joining <host>...", 19.5.3)
    std::string chosen_host_;

    struct PendingEvent {
        std::uint32_t tick;
        wire::Event event;
    };
    void send(wire::Type type, const std::vector<std::uint8_t>& payload);
    void handle(const wire::Frame& frame, double now);
    // Frames that only feed the indicator, the name tags or the menus
    // (NETSTAT, PRESENCE, PRESENCE2, INSTANCE_INFO, WORLD_NOTICE, MEMBER_INFO):
    // one that does not decode is skipped (19.18).
    static bool informational(wire::Type type) noexcept;
    void on_welcome(const wire::Welcome& welcome, double now);
    // Round I1a's review: the engine holds a story whose owner key is no longer
    // valid (another's, after a server restart or a lost instance): no STORY
    // goes for it (the relay may have given that key's id to this member) until
    // a BARRIER_OK names the next visit's owner (the DISSOLVE's replace).
    void forget_foreign_visit();
    // The client's own answer to a barrier the relay left unanswered (3.4;
    // round I1a's review: inside the world it keeps what the engine holds).
    void barrier_fallback(double now);
    // A kept *Leave instance* wish goes out once the INTENT can (leave_instance).
    void send_pending_leave();
    void on_world_notice(const wire::WorldNotice& notice);
    // The toast of an arrival into an instance (19.5.6), once BARRIER_OK and
    // the instance's INSTANCE_INFO are both there.
    void arrival_toast();
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
    void go_alone();
    std::uint32_t roster_member_for(int slot) const;
    void run_catch_up(double now);
    void send_input_ahead(const wire::WirePad& pad);
    // A pad for a tick the relay sealed already, sampled while a backlog runs
    // at once (a resume after a long cut): such pads go together, up to 32 in
    // one INPUT, instead of one frame each (round I1a, W7d: 2,800 single
    // frames after a 45 s cut broke the relay's frame budget).
    void queue_late_pad(const wire::WirePad& pad);
    void flush_late_pads();
    void start_sealed(const wire::WirePad& pad);
    void notice(std::string text);
    // Room story (8.3): the pending set as STORY (at a barrier always, else
    // when it changed or every story_interval while it is not empty), the
    // answer, the engine lines of the additive counters.
    void send_story(double now, bool at_barrier);
    // PRESENCE / PRESENCE2 (12): the figure's facts for the others' tags and
    // ghost figures; track_figure (after each live step) counts the warps and
    // keeps PRESENCE2's step clock. hide_presence: an empty stage at once when
    // the last one sent showed the member (it leaves its stage: a departure,
    // a barrier, a load), so the others' tag and figure go now instead of
    // when the last one turns stale.
    void send_presence(double now);
    void hide_presence(double now);
    void depart(double now);
    void transmit(wire::Presence p, double now);
    void track_figure(double now);
    void on_story_ack(const wire::StoryAck& ack);
    // A confirmed story of `owner` (STORY_ACK, BARRIER_OK): the own and / or the visit's.
    void take_story(const engine::RoomStory& story, std::pair<std::uint32_t, std::uint8_t> owner);
    void open_line();
    void trace(const char* format, ...) const;
    // The input delay changes (traced with its source: welcome, ping, join_req, barrier_ok, delay_event).
    void set_delay(int d, const char* why);
    // Traces a mode change once (called where the mode may have changed).
    void note_mode();
    // The latency window line (every few seconds while a stage runs).
    void report_window(double now, bool force);
    // One line of saves/online.log (when set).
    void log(const char* format, ...) const;
    std::string slot_name(int slot) const;
    std::string host_name() const;
    std::string stage_title(std::string_view stage) const { return config_.stage_name ? config_.stage_name(stage) : std::string(stage); }
    // Round J (6.13.4-6.13.5).
    void checkpoint_tick(double now);
    void upload_checkpoint();
    bool rebuild_from_checkpoint(wire::StateData& state, double now);

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
    static constexpr std::uint8_t wireCloseReplaced = 4; // wire::CloseReason::replaced
    std::uint8_t close_code_ = 0;                          // the relay's CLOSE reason (0: none)
    std::uint32_t member_id_ = 0;
    // The HELLO answer line out of the received bytes; returns how many of them it used.
    std::size_t read_hello(double now);
    // The own story owner key as of the last WELCOME (a reconnect's WELCOME with
    // another member id: the relay restarted and numbers its members anew).
    std::pair<std::uint32_t, std::uint8_t> own_key_{0, 1};

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
    std::array<std::uint32_t, 4> slot_members_{kNoMember, kNoMember, kNoMember, kNoMember}; // member id per slot
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
    std::uint16_t skipped_buttons_ = 0; // buttons of samples skipped after a shorter delay (sent with the next pad)
    struct LatePads {
        std::uint32_t inst = 0, visit = 0, first = 0;
        std::vector<wire::WirePad> pads;
    };
    LatePads late_pads_;                // queue_late_pad's batch (empty outside a backlog run)

    // Barrier.
    std::uint32_t barrier_tick_ = 0;
    std::uint64_t serial_at_barrier_ = 0;
    std::optional<wire::BarrierOk> decision_;
    double parked_at_ = 0;
    std::uint8_t group_at_barrier_ = 1;
    std::string barrier_stage_;          // the BARRIER's target stage
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
    std::optional<std::uint32_t> resync_lead_; // a LEAD_FROM the resync's replay reached (the partner left meanwhile)
    std::uint8_t alone_pending_ = 0;    // taken out of the group: the others' players to remove at a membership-safe step
    std::uint64_t replayed_steps_ = 0;
    double replay_seconds_ = 0;
    double catch_up_started_ = 0;
    double starved_ = 0, starved_since_ = 0; // catch-up time spent waiting for ticks (not part of the wall-clock rate)
    std::uint32_t gap_traced_ = 0xFFFFFFFFu; // the tick a catch-up waits for, traced once

    // Reports.
    std::uint8_t state_flags_ = 0xFF;
    std::uint64_t state_context_ = 0;
    double state_sent_at_ = -100;
    bool state_pending_ = false;
    double presence_at_ = -100;
    // PRESENCE2 (round 60): the hub takes it, the last PRESENCE / PRESENCE2
    // sent, the warp counter and the figure's place after the last live step.
    static constexpr float kWarpStep = 24.0f; // units in one step: no walk or run moves this far
    bool presence_v2_ = false;
    std::optional<wire::Presence> presence_sent_;
    std::uint8_t warp_ = 0;
    std::uint64_t figure_serial_ = 0;
    bool figure_known_ = false;
    float figure_x_ = 0, figure_y_ = 0, figure_z_ = 0;
    // A departure (HEADING: the stage's NEXTSTAGE is due) or a barrier hides
    // the member until the next stage load (the load serial moves on), at
    // most kDepartingLimit: nothing visible goes at the place it leaves from.
    static constexpr double kDepartingLimit = 10.0;
    bool departing_ = false;
    std::uint64_t departing_serial_ = 0;
    double departing_at_ = 0;
    // PRESENCE2's clock: the time of the last live step on a steady field
    // grid (one field after the step before, at most kStepSlack behind the
    // wall clock), so the packets' times are spaced like their places at
    // any picture rate; after kStepStale without a step, the poll time.
    static constexpr double kStepSlack = 0.05, kStepStale = 0.1;
    double step_at_ = -100;

    // Pacing and lag (INSTANCE_INFO's and WORLD_NOTICE's lagging members).
    PaceController pace_;
    std::set<std::uint32_t> lagging_members_;
    // Round I1a: the world (19.18). The instance this engine is in (BARRIER_OK),
    // its host, how this member came into it (the toast waits for
    // INSTANCE_INFO), the relay's description of it, the members' flags.
    std::uint32_t wid_ = 0, host_ = 0;
    std::optional<wire::Arrival> arrival_pending_;
    wire::Visibility arrival_visibility_{};
    std::optional<wire::InstanceInfo> instance_;
    std::map<std::uint32_t, std::uint8_t> member_flags_;
    bool instance_gone_ = false;      // the local way home is due (19.6.10)
    bool leave_wanted_ = false;       // a *Leave instance* waiting until the INTENT can go
    std::string gone_notice_;         // its text once the departure ran
    std::string host_away_name_;      // WORLD_NOTICE host_away: the running line's host and its end (-1: none)
    double host_away_until_ = -1;
    std::uint8_t own_box_ = 1;
    bool hello_seen_ = false;
    double welcome_at_ = -100; // the first WELCOME (the members named right after it are no news)
    double last_tick_at_ = 0;
    double now_ = 0;
    double last_poll_at_ = -1;  // the previous poll (a PING waited at most the gap since)
    double poll_gap_ = 0;
    std::array<double, 31> pass_waits_{};
    std::size_t pass_wait_count_ = 0;

    // Room story (8.3). A counter line is the engine line its growth is made
    // in: the visit (base = the counters at the visit's start), a fork's child
    // from its fork (its id comes with REASSIGN), a parent line closed at a
    // fork (its final growth is sent until the server took it).
    struct CounterLine {
        std::uint32_t inst = 0, visit = 0;
        bool awaiting_id = false;
        std::map<std::int32_t, std::int32_t> base, taken;
        std::optional<std::map<std::int32_t, std::int32_t>> final_growth;
    };
    // Two confirmed stories (19.8.4): the own (owner = this member) and the
    // current visit's owner's (`confirmed_`, the pending set's base; the own
    // at home). visit_owner_ is that owner's key.
    std::optional<engine::RoomStory> own_confirmed_;
    std::optional<engine::RoomStory> confirmed_;
    std::pair<std::uint32_t, std::uint8_t> visit_owner_{0, 1};
    std::vector<CounterLine> lines_;
    std::deque<wire::Story> story_in_flight_;
    std::set<std::int32_t> refused_flags_, refused_folder_; // not sent again before the next barrier
    std::uint8_t refused_phase_ = 0;
    engine::StoryDelta story_last_;
    double story_sent_at_ = -100, story_checked_at_ = -100;
    // A STORY_ACK said wait / not_member: the pending set goes again at this
    // time even if it did not change (0: none due).
    double story_retry_at_ = 0;
    // Round G-core: the Game Start waits for the confirmed story (hold_for_story).
    bool story_hold_ = false;
    // Round G-core: "<host>'s story moved on" once per visit (inst, visit) of a guest.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> moved_on_noticed_;

    std::map<std::uint32_t, RemoteMember> remote_;
    PresenceTracks tracks_;
    std::map<std::uint32_t, std::string> names_;
    std::vector<std::string> notices_;
    bool engine_replaced_ = false;

    // Network facts and the input latency (net_stats.hpp).
    // Round J: the checkpoints (6.13.4) and the join from one (6.13.5).
    bool hub_checkpoints_ = false;           // the hub's WELCOME has flag 4
    std::optional<std::uint32_t> load_end_;  // K: the visit's ticks up to its load end
    std::uint32_t next_statehash_ = 0;       // the next checkpoint period's tick
    bool checkpoint_asked_ = false;          // CHECKPOINT_ASK: upload at the next state-safe tick
    bool periodic_asked_ = false;            // CHECKPOINT_ASK periodic: upload with the next STATEHASH (an agreed tick)
    bool from_checkpoint_ = false;           // this catch-up started from a checkpoint
    std::optional<std::uint32_t> joined_tick_; // this member's own JOIN in this visit (joined_tick())
    double rebuild_ms_ = 0;                  // its rebuild (snapshot, load ticks, apply, hash)
    // Round J's review: writing the tick state failed in this visit (a
    // TickStateError: a node, closure or bank outside the serializer's
    // lists); no STATEHASH or upload until the next visit (before, the error
    // left after_step and closed every member's game in the same tick).
    bool checkpoints_off_ = false;
    bool load_end_told_ = false;             // a solo leader's load_end_only STATEHASH went out
    // The hash of this tick's STATEHASH, for an upload in the same tick (no second hash).
    std::optional<std::pair<std::uint32_t, std::uint64_t>> tick_hash_;
    // Protocol 48: the check ticks of this member's checkpoint uploads whose
    // NETFP has not gone out yet (it goes out when the tick is executed,
    // wire::kReadyCheckTicks; ascending, a few) and, as a joiner from a
    // checkpoint, the check tick its READY waits for.
    std::vector<std::uint32_t> check_fp_ticks_;
    std::optional<std::uint32_t> ready_check_tick_;
    // DW4_TRACE_TICKSTATE_EVERY (development builds): the dumps after a JOIN
    // go on until this tick.
    std::uint32_t trace_dump_until_ = 0;

    NetStats stats_;
    NetView net_;
    std::uint64_t steps_ = 0;       // live steps run (the latency's step count)
    std::size_t backlog_runs_ = 0;  // passes that ran a backlog without pictures
    Mode traced_mode_ = Mode::connecting;
    double window_at_ = -100;
    double log_window_at_ = -100;
    double tcp_checked_at_ = -100;
    LatencyWindow log_window_;  // the windows since the last log line
    OnlineLog* online_log_ = nullptr;
};

} // namespace dw4::game::online
