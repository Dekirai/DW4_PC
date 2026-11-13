#pragma once

// DW4R2, the relay protocol of the seamless co-op (docs/SEAMLESS.md 6.2, 6.3
// and the appendix "DW4R2 wire layout", which is the one layout the C++
// client, the C# relay hub (server/DW4Online/src/DW4Online.Relay/Protocol),
// the C# fake clients and the Python test client share).
//
// One TCP connection per client and world session (protocol 49, round I1a,
// docs/SEAMLESS.md 19.18; the layouts agreed in DW4_PC-wt/i1a-contract.md).
// The client sends one line
//   DW4R2 <ticket> <protocol> <dataHash> <settingsHash>\n
// (protocol and dataHash in decimal, settingsHash as 16 hex digits, sent as
// 0 and ignored since protocol 49) and the relay answers
//   OK <member> <serverTimeMs>\n   or   ERR <code>\n
// (member = the member id, a u32 for the server run; codes protocol, ticket,
// data, full, busy, banned).
// Then frames follow in both directions: type u8 | length u32 LE | payload,
// at most 1 MiB; WELCOME is the server's first frame.
//
// Every integer is little endian; f32 is IEEE single. str = u8 length +
// printable ASCII (stage names 1-24 of A-Z a-z 0-9 _, empty only where no
// stage is allowed), blob = u32 length + bytes. inst = instance id u32,
// visit = its stage load count u32, tick u32. A pad is 6 bytes
// (pad_wire.hpp); masks use bits 0-3 (slots 0-3).
//
// A tick record (SEALED, LIVE, the log of STATE_DATA) is
//   u8 mask (the slots whose pads follow) | u8 event count | pads in slot
//   order | events
// and an event is u8 type | u16 payload length | payload:
//   JOIN 1      u8 slot | u8 entrance point | carry (the rest)
//   LEAVE 2     u8 slot
//   INTENT 3    u8 slot | u8 kind (1 go without, 2 give up, 3 leave, 4 go
//               along: I1b) | i32 argument
//   LEAD_FROM 4 u32 tick (the record is the last sealed tick; the remaining
//               member leads alone from that tick)
//   DELAY 5     u8 slot | u8 delay
//   HOME 6      u8 slot mask | u8 reason (1 leave, 2 ended, 3 removed, 4
//               staff): exactly these slots take the home departure (19.6.5)
//   GO 7        u8 slot | u8 point | str stage (round I1b, 19.30.7: applied like
//               INTENT at the first membership-safe step, every GO of that step
//               together; it expires engine::kGoExpiryTicks after its tick)
// Unknown event types are skipped (a newer server).
#include "dw4/engine/online_story.hpp"
#include "pad_wire.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game::online::wire {

using dw4::game::online::WirePad;

inline constexpr std::size_t kMaxFrame = 1u << 20;
inline constexpr std::size_t kStateChunk = 60 * 1024; // SNAPSHOT / STATE_DATA chunks
inline constexpr std::size_t kMaxInputPads = 32;      // INPUT: 1..32 pads
inline constexpr std::size_t kMaxLogPads = 1200;      // LOG: 1..1200 pads
inline constexpr std::size_t kMaxCarry = 65535 - 2;   // BARRIER / READY carry (it becomes a JOIN event with a u16 length: slot, point, carry)
inline constexpr std::size_t kMaxStage = 24;          // stage names
inline constexpr std::uint32_t kNoTick = 0xFFFFFFFFu; // WELCOME without a last tick
// Protocol 48 (the READY check of a join from a checkpoint, SEAMLESS.md
// 6.13.5): the donor of a checkpoint of tick T (the first tick not in the
// state) sends NETFP for its *check tick* T + kReadyCheckTicks - 1 too, a
// quarter second later; a joiner rebuilt from that checkpoint replays at least
// to the check tick and its READY names it (or a later multiple of 60), so the
// relay compares the rebuild with the donor after kReadyCheckTicks replayed
// steps without waiting for the next multiple of 60 (up to a second before).
inline constexpr std::uint32_t kReadyCheckTicks = 15;
inline constexpr std::uint32_t check_tick(std::uint32_t checkpoint_tick) { return checkpoint_tick + kReadyCheckTicks - 1; }

struct WireError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The frame types (SEAMLESS.md 6.3, "Message types"): one number per message
// on both sides, never given twice. A new message takes the next free number
// of its direction (client to server 0x15, server to client 0x91 after rounds
// 60 and 62) and goes into the registry checks of tests/online_wire_tests.cpp
// and WireGoldenTests.cs. 0x8F is NETSTAT, in the field since DW4Online 0.1.2:
// a client that cannot decode a frame of a type it knows drops the connection
// (only the informational ones are skipped, Session::informational).
enum class Type : std::uint8_t {
    // client -> server (0x01-0x3F)
    input = 0x01,
    log = 0x02,
    heading = 0x03,
    barrier = 0x04,
    barrier_choice = 0x05,
    split = 0x06,
    snapshot = 0x07,
    snaphash = 0x08,
    ready = 0x09,
    join_at = 0x0A,
    netfp = 0x0B,
    state = 0x0C,
    intent = 0x0D,
    story = 0x0E,
    presence = 0x0F,
    resume = 0x10,
    rate = 0x11,
    pong = 0x12,
    bye = 0x13,
    presence2 = 0x14,
    // round J (docs/SEAMLESS.md 6.13.7): only to a hub whose WELCOME has flag 4
    statehash = 0x15,
    checkpoint = 0x16,
    checkpoint_bad = 0x17,
    // protocol 50 (round I1b, 19.7.4): the gate form's plan and the prompt's answer (prompt_wire.hpp)
    plan = 0x18,
    choice = 0x19,
    // protocol 49 (round I1a, 19.18): the host's or staff's instance change
    instance_op = 0x1B,
    report = 0x1E,      // round I1b (moderation): *Report player* from the player card
    // server -> client (0x80-0xBF)
    welcome = 0x80,
    sealed = 0x81,
    live = 0x82,
    join_req = 0x83,
    barrier_ok = 0x84,
    barrier_wait = 0x85,
    state_data = 0x86,
    presence_of = 0x87,
    resync = 0x88,
    story_ack = 0x89,
    room = 0x8A,   // retired with protocol 49 (never sent to it; the number stays reserved)
    ping = 0x8B,
    close = 0x8C,
    roster = 0x8D, // retired with protocol 49 (as ROOM)
    reassign = 0x8E,
    netstat = 0x8F, // round 62: the member's link (older clients skip it)
    presence2_of = 0x90, // round 60: PRESENCE2 to a member that sends it (12.1)
    // protocol 50 (round I1b, 19.7.3): the arrival prompt's rows (prompt_wire.hpp)
    options = 0x91,
    checkpoint_ask = 0x92, // round J: upload the tick state at the next state-safe tick
    // protocol 49 (round I1a, 19.18)
    instance_info = 0x95,
    world_notice = 0x96,
    member_info = 0x97,
    // round I1b's go along (19.30.5, 19.18): a go-along offer for this member
    offer = 0x9B,
};
const char* type_name(Type type) noexcept;

struct Frame {
    Type type{};
    std::vector<std::uint8_t> payload;
};

// Little endian writer / bounds-checked reader.
struct Writer {
    std::vector<std::uint8_t> out;
    void u8(std::uint8_t v) { out.push_back(v); }
    void u16(std::uint16_t v);
    void u32(std::uint32_t v);
    void u64(std::uint64_t v);
    void i8(std::int8_t v) { u8(std::uint8_t(v)); }
    void i16(std::int16_t v) { u16(std::uint16_t(v)); }
    void i32(std::int32_t v) { u32(std::uint32_t(v)); }
    void f32(float v);
    void str8(std::string_view v);
    void blob(std::span<const std::uint8_t> v);
    void raw(std::span<const std::uint8_t> v) { out.insert(out.end(), v.begin(), v.end()); }
    void pad(const WirePad& p);
};
struct Reader {
    std::span<const std::uint8_t> in;
    std::size_t at = 0;
    void need(std::size_t n) const;
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    std::int8_t i8() { return std::int8_t(u8()); }
    std::int16_t i16() { return std::int16_t(u16()); }
    std::int32_t i32() { return std::int32_t(u32()); }
    float f32();
    std::string str8(std::size_t limit = 255);
    std::vector<std::uint8_t> blob(std::size_t limit = kMaxFrame * 16);
    std::vector<std::uint8_t> rest();
    WirePad pad();
    bool done() const { return at == in.size(); }
    void end() const; // throws unless every byte was read
};

// ---- handshake lines
std::string hello_line(std::string_view ticket, std::uint32_t protocol, std::uint64_t data_hash, std::uint64_t settings_hash);
struct HelloAnswer {
    bool ok{};
    std::uint32_t member_id{};
    std::uint64_t server_time_ms{};
    std::string error; // the code of ERR
};
// Parses the answer line (without the '\n'); nothing for a malformed line.
std::optional<HelloAnswer> parse_hello_answer(std::string_view line);
// Server side (fake hubs, tests): the parts of a hello line.
struct Hello {
    std::string ticket;
    std::uint32_t protocol{};
    std::uint64_t data_hash{}, settings_hash{};
};
std::optional<Hello> parse_hello(std::string_view line);

// ---- framing
std::vector<std::uint8_t> encode_frame(Type type, std::span<const std::uint8_t> payload);
// Collects bytes from a stream and cuts frames (throws WireError on a frame
// above kMaxFrame).
class FrameReader {
public:
    void feed(std::span<const std::uint8_t> bytes);
    std::optional<Frame> next();
    std::size_t buffered() const { return buffer_.size() - at_; }

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t at_ = 0;
};

// ---- enumerations
enum class Mode : std::uint8_t { hub = 0, solo = 1, sealed = 2 }; // WELCOME, BARRIER_OK, REASSIGN
// test (0xF0): round I1b's scenario hooks (slot u8 | hook u8 | argument i32),
// stamped only by a relay with World:TestHooks / World:TestGoHook on; only
// development builds apply it (TestHook), every other build skips it like an
// unknown event (the scenarios run development builds only).
enum class EventKind : std::uint8_t { join = 1, leave = 2, intent = 3, lead_from = 4, delay = 5, home = 6, go = 7, test = 0xF0 };
// test (0x7F, development builds): ask the relay for a TEST event in the own
// stage group (argument = hook | value << 8; a relay without World:TestHooks
// ignores it).
enum class IntentKind : std::uint8_t { go_without = 1, give_up = 2, leave = 3, go_along = 4, test = 0x7F };
// INTENT 4 go along's argument (round I1b): the offer id; 0 the player's
// Cancel; kGoEndedArgument the client's report that its stamped GO ended
// without a departure (it expired after 300 ticks, or did nothing at its step).
constexpr std::int32_t kGoEndedArgument = -1;
// The TEST event's hooks (development builds; docs/ROUND_I1B_GO_NOTES.md):
// defeat: the slot's player falls (0 HP, SQ_DEAD, as poison's last tick);
// busy: the stage group is neither membership-safe nor joinable for
// `argument` ticks (a shared scene's stand-in); mat: the slot's player stands
// on the mat "mat" + letter (bits 8-15) + two-digit number (bits 0-7) of
// `argument` (a walk through a floor the test characters do not survive).
enum class TestHook : std::uint8_t { defeat = 1, busy = 2, mat = 3 };
// HOME's reason (19.6.5).
enum class HomeReason : std::uint8_t { leave = 1, ended = 2, removed = 3, staff = 4 };
// JOIN_REQ's kind (protocol 49): a JOIN, a HOME (`slot` holds the mask) or a GO (I1b).
enum class JoinKind : std::uint8_t { join = 1, home = 2, go = 3 };
// BARRIER_OK's arrival (protocol 49): how this barrier put the member into an
// instance (the toasts of 19.5.6).
enum class Arrival : std::uint8_t { none = 0, new_instance = 1, auto_join = 2, co_arrival = 3, guest_new = 4 };
enum class StoryMode : std::uint8_t { merge = 0, replace = 1 };
enum class Visibility : std::uint8_t { public_ = 0, friends = 1, private_ = 2 };
enum class SplitKind : std::uint8_t { exit = 0, in_stage = 1 };
enum class Choice : std::uint8_t { wait = 0, alone = 1 };
// STATE reasons (why an instance is not joinable).
enum class NotJoinable : std::uint8_t { none = 0, entry_scene = 1, scene = 2, menu = 3, loading = 4, boss = 5, fork = 6, hub = 7 };
enum class Decision : std::uint8_t { new_instance = 1, join = 2, dissolve = 3, co_arrive = 4 };
// 5 choice (round I1b, 19.7.5): the relay holds a fresh arrival's barrier for the prompt; OPTIONS follows.
enum class WaitReason : std::uint8_t { co_arrival = 1, not_joinable = 2, boss_lock = 3, snapshot = 4, choice = 5 };
// Why a barrier ended in NEW next to an instance of the same stage.
enum class Notice : std::uint8_t { none = 0, other_context = 1, full = 2, too_long = 3, hold_expired = 4, went_alone = 5, join_failed = 6 };
enum class StatePurpose : std::uint8_t { join = 1, co_arrival = 2, resync = 3, resume = 4 };
// 2 kicked and 3 room closed are the rooms' (retired with protocol 49); 9
// account: the account was locked, banned or deleted (no reconnect).
enum class CloseReason : std::uint8_t {
    left = 1, kicked = 2, room_closed = 3, replaced = 4, protocol = 5, abuse = 6, timeout = 7, stopping = 8, account = 9
};
namespace state_flag {
// load_ended (protocol 47, round J's review): the visit reached its load end K;
// the relay takes the first such STATE's context as the visit's entry context
// (docs/SEAMLESS.md 3.7), joinable or not.
inline constexpr std::uint8_t joinable = 1, membership_safe = 2, boss_lock = 4, load_ended = 8;
// Round I1b (19.18, 19.30.7): no present slot alive (the go-along wait and the
// join holds read it: no rescue joins).
inline constexpr std::uint8_t no_living = 16;
inline constexpr std::uint8_t known = joinable | membership_safe | boss_lock | load_ended | no_living;
}

// The settings block of protocol 49 (11 bytes: BARRIER's own, BARRIER_OK's
// and INSTANCE_INFO's): difficulty u8 (0-2) | camera u8 (0-1) | light u8
// (0-1) | fast loading u8 (0/1) | randomizer seed u32 | drops u8 (0/1) |
// enemies u8 (0-2) | zones u8 (0-2). The values of room_settings.hpp's
// RoomSettings (to_wire / from_wire there).
struct Settings {
    std::uint8_t difficulty{}, camera{}, light = 1, fast{};
    std::uint32_t seed{};
    std::uint8_t drops{}, enemies{}, zones{};
    bool operator==(const Settings&) const = default;
};
void write_settings(Writer& w, const Settings& s);
Settings read_settings(Reader& r);

// ---- tick records and events
struct Event {
    EventKind kind{};
    std::uint8_t slot{};                // (GO: the going slot)
    std::uint8_t point{};               // JOIN: the entrance point; GO: the point
    std::vector<std::uint8_t> carry;    // JOIN: the joiner's carry (engine bytes)
    std::uint8_t intent_kind{};         // INTENT
    std::int32_t argument{};            // INTENT, TEST
    std::uint32_t tick{};               // LEAD_FROM
    std::uint8_t delay{};               // DELAY
    std::uint8_t mask{};                // HOME: the slots that go home
    std::uint8_t reason{};              // HOME: HomeReason
    std::string stage;                  // GO: the target stage
    std::uint8_t hook{};                // TEST: TestHook (argument: its value)
    bool operator==(const Event&) const = default;
};
struct TickBody {
    std::uint8_t present{};             // the mask: the slots whose pads follow
    std::array<WirePad, 4> pads{};      // valid for the mask's slots
    std::vector<Event> events;
    bool operator==(const TickBody&) const = default;
};
void write_tick(Writer& w, const TickBody& tick);
TickBody read_tick(Reader& r);

// ---- client -> server
struct Input {         // inst | visit | u32 first tick | u8 count (1-32) | pads
    std::uint32_t inst{}, visit{}, first{};
    std::vector<WirePad> pads;
};
struct Log {           // inst | visit | u32 first tick | u16 count (1-1200) | pads
    std::uint32_t inst{}, visit{}, first{};
    std::vector<WirePad> pads;
};
// Protocol 50 (round I1b, 19.30.5) appends u8 asked: the slots the exit port
// asked on the mat for this departure that stay (no go-along notice for them).
struct Heading {       // inst | str stage | u8 point | u64 session context hash | u8 asked
    std::uint32_t inst{};
    std::string stage;
    std::uint8_t point{};
    std::uint64_t context{};
    std::uint8_t asked{};
};
// Protocol 49 appends u32 travel token (0 in I1a) | u16 plan id (0 in I1a;
// 0xFFFF: DW4_ONLINE_AUTOCHOICE=new, development builds) | the member's own
// settings (its Game Start's difficulty, its Online > Settings cheats).
struct Barrier {       // inst | visit | u32 barrier tick | str stage | u8 point | u8 alive | u8 present | u64 context | blob own carry | ...
    std::uint32_t inst{}, visit{}, tick{};
    std::string stage;
    std::uint8_t point{};
    std::uint8_t alive{}, present{};
    std::uint64_t context{};
    std::vector<std::uint8_t> carry;
    std::uint32_t token{};
    std::uint16_t plan{};
    Settings own;
    static constexpr std::uint16_t plan_new_instance = 0xFFFF;
};
struct BarrierChoice { // inst | u8 choice
    std::uint32_t inst{};
    Choice choice{};
};
struct Split {         // inst | visit | u32 fork tick | u8 leavers (the side that moves to the child) | u8 kind
    std::uint32_t inst{}, visit{}, tick{};
    std::uint8_t leavers{};
    SplitKind kind{};
};
struct Chunk {         // SNAPSHOT / STATE_DATA: inst | visit | u32 total | u32 offset | chunk (the rest)
    std::uint32_t inst{}, visit{};
    std::uint32_t total{}, offset{};
    std::vector<std::uint8_t> bytes;
};
struct SnapHash {      // inst | visit | u64 canonical hash
    std::uint32_t inst{}, visit{};
    std::uint64_t hash{};
};
// tick: a multiple of 60; from a checkpoint (protocol 48) its check tick or a
// later multiple of 60 (kReadyCheckTicks).
struct Ready {         // inst | visit | u32 tick | u64 net fingerprint | u32 rate | blob carry [| u8 flags]
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t fingerprint{};
    std::uint32_t rate{};
    std::vector<std::uint8_t> carry;
    std::uint8_t flags{}; // round J: written only when nonzero
    static constexpr std::uint8_t from_checkpoint = 1;
};
// Round J (6.13.4): the canonical tick state's XXH64 at a checkpoint tick
// (the first tick not in the state) with the visit's load end K.
// flags: 1 bytes kept; 2 load_end_only (round J's review: a solo leader has
// nobody to compare with; it tells only K, hash 0, and computes nothing).
struct StateHash {     // inst | visit | u32 tick | u64 hash | u32 load end | u8 flags
    static constexpr std::uint8_t kept = 1, load_end_only = 2;
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t hash{};
    std::uint32_t load_end{};
    std::uint8_t flags{};
};
struct Checkpoint {    // inst | visit | u32 tick | u64 hash | u32 load end | u32 total | u32 offset | chunk (the rest)
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t hash{};
    std::uint32_t load_end{}, total{}, offset{};
    std::vector<std::uint8_t> bytes;
};
enum class CheckpointBadReason : std::uint8_t { version = 1, decode = 2, load_end = 3, topology = 4, hash = 5 };
struct CheckpointBad { // inst | visit | u32 tick | u8 reason | str section
    std::uint32_t inst{}, visit{}, tick{};
    CheckpointBadReason reason{};
    std::string section;
};
struct CheckpointAsk { // inst | visit | u32 tick (0: the next state-safe tick) | u8 flags
    std::uint32_t inst{}, visit{}, tick{};
    std::uint8_t flags{};
    static constexpr std::uint8_t periodic = 1;  // with the next STATEHASH
    static constexpr std::uint8_t diagnosis = 2;
};
struct JoinAt {        // inst | visit | u32 tick J
    std::uint32_t inst{}, visit{}, tick{};
};
// tick: a multiple of 60; or (protocol 48) the check tick of the member's own
// checkpoint upload (kReadyCheckTicks).
struct NetFp {         // inst | visit | u32 tick | u64 hash
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t hash{};
};
struct State {         // inst | visit | u32 tick | u8 flags | u64 context | u8 reason
    std::uint32_t inst{}, visit{}, tick{};
    std::uint8_t flags{};
    std::uint64_t context{};
    NotJoinable reason{};
};
struct Intent {        // inst | visit | u8 kind | i32 argument (kind 3 leave: solo-led and sealed; the others sealed only)
    std::uint32_t inst{}, visit{};
    IntentKind kind{};
    std::int32_t argument{};
};
// PRESENCE: [u8 member (server -> client)] | str stage (empty: hidden) | inst |
// f32 x | f32 y | f32 z | f32 yaw | u8 species | u16 level | u16 hp |
// u16 max hp | u16 mp | u16 max mp. A member sends at most every 150 ms; an
// empty stage while it loads or watches a scene (its tag hides), hp 0 while
// it is defeated.
//
// PRESENCE2 (round 60: the ghost figures of the hub stages, SEAMLESS.md 12):
// [u8 member (server -> client)] | u8 version (1) | the PRESENCE fields |
// str clip | u8 flags | u8 warp | u32 sent ms | str weapon. `clip` is the
// motion the member's figure plays ("runD", "waitS"; 0-16 of A-Z a-z 0-9 _,
// empty: unknown); flags 1: the clip loops, 2: busy (a status or shop
// window is open: the figure stands where it is, the tag hides); `warp`
// counts the member's jumps (stage loads, warps: never glided across);
// `sent ms` is the sender's clock in milliseconds (wrapping: the time of the
// step whose place it carries), the receivers' interpolation clock; `weapon`
// the package of the weapon its figure holds (0-24 of the same characters,
// empty: none). Sent only to a hub whose WELCOME carries Welcome::presence_v2
// (an older hub closes a connection that sends a frame it does not know); the
// hub forwards PRESENCE2 to the members that send it and PRESENCE to the
// others. A later version keeps this layout and appends, within 256 bytes in
// all: a reader takes what it knows (a version 1 hub forwards only the
// version 1 part it checked, written at version 1; a larger one it drops).
// Protocol 49: to clients `member` is a u32 followed by u8 relation flags
// (1 friend, 2 party, 4 staff tag, 8 a member of the receiver's instance).
struct Presence {
    std::uint32_t member{};
    std::string stage;
    std::uint32_t inst{};
    float x{}, y{}, z{}, yaw{};
    std::uint8_t species{};
    std::uint16_t level{}, hp{}, max_hp{}, mp{}, max_mp{};
    // PRESENCE2 (version 0: a PRESENCE, without the fields below).
    std::uint8_t version{};
    std::string clip;
    std::uint8_t flags{};
    std::uint8_t warp{};
    std::uint32_t sent_ms{};
    std::string weapon;
    std::uint8_t relation{}; // (server -> client, protocol 49: the relation flags)
    static constexpr std::uint8_t loop = 1;
    static constexpr std::uint8_t busy = 2;
    static constexpr std::uint8_t known_flags = loop | busy;
    bool operator==(const Presence&) const = default;
};
inline constexpr std::uint8_t kPresenceVersion = 1;
inline constexpr std::size_t kMaxClip = 16;
inline constexpr std::size_t kMaxWeapon = 24;
// STORY (SEAMLESS.md 8.3): the member's pending set against the confirmed
// room story, at every barrier (before BARRIER) and every ten seconds while it
// is not empty. inst | visit (the member's instance now) | u32 the confirmed
// revision it was computed against | u8 phase (0: no claim, else the engine's
// phase index 1-7) | u16 n + n x u16 flags | u8 n + n x u8 keys | u8 n + n x
// (u8 counter | u32 line inst | u32 line visit | i32 growth): the cumulative
// growth of an additive counter in an engine line (instance and visit the
// growth was made in; a fork's child counts from its fork under its own id).
// Protocol 49 appends the owner key: u32 owner member | u8 owner box (the
// story owner of the member's current visit, 19.8.4).
struct Story {
    std::uint32_t inst{}, visit{}, revision{};
    engine::StoryDelta delta;
    std::uint32_t owner{};
    std::uint8_t box = 1;
};
inline constexpr std::size_t kMaxStoryLines = 32;
// STORY_ACK: u32 revision | blob story (1,100 bytes, story::encode_story; empty
// while the room has none) | u8 flags (1: the answer to this member's STORY) |
// u16 n + n x (u8 kind | u16 index | u8 reason) refused keys
// (engine::StoryRefused). The server sends it after WELCOME (the confirmed
// story to start from), to every connected member whenever the room story's
// revision rises, and to a STORY's sender with what it refused.
// Protocol 49 appends the owner key of the keeper this revision belongs to
// (u32 member | u8 box); refusal 12 = owner (the STORY's key was not the
// sender's current story owner).
struct StoryAck {
    std::uint32_t revision{};
    std::vector<std::uint8_t> story;
    std::uint8_t flags{};
    std::vector<engine::StoryRefused> refused;
    std::uint32_t owner{};
    std::uint8_t box = 1;
    static constexpr std::uint8_t answer = 1;
};
struct Resume {        // inst | visit | u8 slot | u32 last executed tick | str stage (protocol 49: the engine's stage)
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    std::uint32_t last_tick{};
    std::string stage;
};
struct Pong {          // u32 nonce | u64 client time ms
    std::uint32_t nonce{};
    std::uint64_t client_time_ms{};
};
struct Bye {           // u8 reason
    std::uint8_t reason{};
};

// ---- server -> client
// WELCOME: u32 member | inst | visit | u8 slot | u8 mode | str stage |
// u32 last tick (kNoTick: none) | u8 delay | u16 max catch-up seconds |
// u8 flags (1 resumable: the member still holds that slot; send RESUME;
// 2 presence_v2: the hub takes PRESENCE2, round 60; 4 checkpoints; 8 world;
// 16 seat held; 32 staff) | u32 wid (the instance the relay has the member
// in, 0: none) | u8 box (the bound box).
struct Welcome {
    std::uint32_t member{};
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    Mode mode{};
    std::string stage;
    std::uint32_t last_tick = kNoTick;
    std::uint8_t delay{};
    std::uint16_t max_catch_up{};
    std::uint8_t flags{};
    static constexpr std::uint8_t resumable = 1;
    static constexpr std::uint8_t presence_v2 = 2;
    static constexpr std::uint8_t checkpoints = 4; // round J: the hub takes STATEHASH / CHECKPOINT / CHECKPOINT_BAD
    static constexpr std::uint8_t world = 8, seat_held = 16, staff = 32;
    std::uint32_t wid{};
    std::uint8_t box = 1;
};
struct Ticks {         // SEALED / LIVE: inst | visit | u32 first tick | u16 count | tick records
    std::uint32_t inst{}, visit{}, first{};
    std::vector<TickBody> ticks;
};
// Protocol 49: + u8 kind (1 join, 2 home: `slot` holds the HOME mask, the
// leader answers JOIN_AT and SEALED(J) carries HOME; 3 go: + str stage | u8 point).
struct JoinReq {       // inst | visit | u8 slot | u8 the leader's delay | u8 kind [| str stage | u8 point]
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    std::uint8_t delay{};
    JoinKind kind = JoinKind::join;
    std::string stage;
    std::uint8_t point{};
};
struct CoArrival {     // u8 slot | u32 member | blob carry
    std::uint8_t slot{};
    std::uint32_t member{};
    std::vector<std::uint8_t> carry;
    bool operator==(const CoArrival&) const = default;
};
// BARRIER_OK: old inst | old visit | u8 decision | new inst | new visit |
// str stage | u8 point | u8 own slot | 4 x i8 slot map (old slot -> new slot
// or -1) | u8 mode | u8 delay | u8 n + n co-arrivals | u32 story revision |
// blob story (1,100 bytes as in STORY_ACK, the same bytes for every member of
// the group; empty: none) | u8 notice | u8 n + n str names | u32 wid (0: home)
// | u32 host member | u8 visibility | u8 arrival | settings (the instance's;
// at a DISSOLVE the member's own) | u64 settings hash | u32 owner member | u8
// owner box (the story owner of the visit that starts) | u8 story mode (0
// merge, 1 replace) | str redirect stage (empty: none; DISSOLVE only) | u8
// redirect point. (Protocol 49; the dropped blob of 48 is gone.)
struct BarrierOk {
    std::uint32_t inst{}, visit{};
    Decision decision{};
    std::uint32_t new_inst{}, new_visit{};
    std::string stage;
    std::uint8_t point{};
    std::uint8_t own_slot{};
    std::array<std::int8_t, 4> slot_map{-1, -1, -1, -1};
    Mode mode{};
    std::uint8_t delay{};
    std::vector<CoArrival> co_arrivals;
    std::uint32_t story_revision{};
    std::vector<std::uint8_t> story;
    Notice notice{};
    std::vector<std::string> names;
    std::uint32_t wid{}, host{};
    Visibility visibility{};
    Arrival arrival{};
    Settings settings;
    std::uint64_t settings_hash{};
    std::uint32_t owner{};
    std::uint8_t box = 1;
    StoryMode story_mode{};
    std::string redirect_stage;
    std::uint8_t redirect_point{};
    // The instance's members after the decision (NEW: the slot map's new
    // slots and the co-arrivals; DISSOLVE: slot 0).
    std::uint8_t present() const;
};
// BARRIER_WAIT: inst | u8 reason | u16 seconds left | u8 n + n str names |
// u8 flags (1: send BARRIER_CHOICE).
struct BarrierWait {
    std::uint32_t inst{};
    WaitReason reason{};
    std::uint16_t seconds_left{};
    std::vector<std::string> names;
    std::uint8_t flags{};
    static constexpr std::uint8_t choice_needed = 1;
};
struct Fork {          // u32 tick | u8 leaver mask (the child side) | u8 kind | u8 side (0 stayers, 1 leavers: the side that leads to the instance)
    std::uint32_t tick{};
    std::uint8_t leavers{};
    SplitKind kind{};
    std::uint8_t side{};
    bool operator==(const Fork&) const = default;
};
// The assembled STATE_DATA blob: u8 purpose | u8 slot | u8 codec (0) |
// blob snapshot (as the leader uploaded it) | u8 n + n forks | u32 first tick |
// u32 count | count tick records. inst and visit come from the chunks.
struct StateData {
    std::uint32_t inst{}, visit{};
    StatePurpose purpose{};
    std::uint8_t slot{};
    std::vector<std::uint8_t> snapshot;
    std::vector<Fork> forks;
    // Round J (codec 2): the load ticks [0, K), the checkpoint of tick T
    // (the first tick not in it), its hash, flags (1 agreed) and bytes.
    std::vector<TickBody> load_ticks;
    std::uint32_t checkpoint_tick{};
    std::uint64_t checkpoint_hash{};
    std::uint8_t checkpoint_flags{};
    std::optional<std::vector<std::uint8_t>> checkpoint;
    std::uint32_t first{};
    std::vector<TickBody> ticks;
};
// ---- protocol 49 (round I1a, 19.18)
// INSTANCE_INFO: u32 wid (0: the member is home; the rest empty) | u32 host |
// u8 visibility | settings | str area | str story label | u8 n + n x (u32
// member | str stage | u16 level | u8 flags: 1 connected, 2 lagging, 4 staff,
// 8 host, 16 in the receiver's stage group) | u8 seats taken | u8 held | u8
// reserved | u16 grace seconds left | u8 own role (0 host, 1 guest, 2 staff
// playing, 3 spectator).
struct InstanceMember {
    std::uint32_t member{};
    std::string stage;
    std::uint16_t level{};
    std::uint8_t flags{};
    static constexpr std::uint8_t connected = 1, lagging = 2, staff = 4, host = 8, same_group = 16;
    bool operator==(const InstanceMember&) const = default;
};
enum class Role : std::uint8_t { host = 0, guest = 1, staff = 2, spectator = 3 };
struct InstanceInfo {
    std::uint32_t wid{}, host{};
    Visibility visibility{};
    Settings settings;
    std::string area, story_label;
    std::vector<InstanceMember> members;
    std::uint8_t taken{}, held{}, reserved{};
    std::uint16_t grace{};
    Role role{};
};
// WORLD_NOTICE: u8 kind | u32 member | str name | u32 argument | str text.
enum class NoticeKind : std::uint8_t {
    joined = 1, left = 2, went_to = 3, visibility = 4, removed = 5, host_away = 6, host_back = 7, ended = 8,
    lagging = 9, lag_over = 10, shared_progress = 11, restart_in = 12, go_refused = 13,
    report = 14 // round I1b: the answer to REPORT (argument ReportResult; member and name: the reported player)
};
// WORLD_NOTICE 14's argument (i1b-contract.md moderation 16:25).
enum class ReportResult : std::uint32_t { sent = 0, limit = 1, not_available = 2, failed = 3 };
// WORLD_NOTICE ended's argument (19.6.5's reasons).
enum class EndReason : std::uint8_t { host_left = 1, host_defeated = 2, host_lost = 3, staff = 4, empty = 5, server_stop = 6 };
struct WorldNotice {
    NoticeKind kind{};
    std::uint32_t member{};
    std::string name;
    std::uint32_t argument{};
    std::string text;
};
// WORLD_NOTICE go_refused's argument (round I1b, 19.30.6).
enum class GoRefused : std::uint8_t { closed = 1, defeated = 2, busy = 3, moved = 4, ended = 5, not_offered = 6 };
// OFFER (round I1b, 19.30.5): u16 offer id | u8 op (1 open, 2 changed, 3
// closed) | u8 flags | u8 n + n x u32 entrant (the first entrant first) | str
// stage (where the first entrant still in the dungeon is) | u8 notice seconds |
// u8 close reason (0 none, 1 left, 2 defeated, 3 ended, 4 gone: over for
// this receiver, no closing line).
enum class OfferOp : std::uint8_t { open = 1, changed = 2, closed = 3 };
enum class OfferClose : std::uint8_t { none = 0, left = 1, defeated = 2, ended = 3, gone = 4 };
struct Offer {
    std::uint16_t id{};
    OfferOp op{};
    std::uint8_t flags{};
    std::vector<std::uint32_t> entrants;
    std::string stage;
    std::uint8_t notice_seconds{};
    OfferClose close{};
    // flags: show the notice now; a late notice (names where they are now);
    // reserved (I2's blocks); the receiver's own request waits; its GO is
    // stamped; the target is boss-locked; the receiver closed it with B; a
    // silent reopen.
    static constexpr std::uint8_t notice = 1, late = 2, unnamed = 4, waiting = 8, stamped = 16, boss = 32, closed_by_b = 64,
                                  reopened = 128;
    bool operator==(const Offer&) const = default;
};
// MEMBER_INFO: u32 member | str name | u16 level | u8 flags (1 friend, 2
// party, 4 staff tag, 8 in the receiver's instance).
struct MemberInfo {
    std::uint32_t member{};
    std::string name;
    std::uint16_t level{};
    std::uint8_t flags{};
    static constexpr std::uint8_t friend_ = 1, party = 2, staff = 4, own_instance = 8;
};
// INSTANCE_OP (client): u8 op (1 visibility, 2 remove member: I1b, 3 end) | u32 argument.
enum class InstanceOpKind : std::uint8_t { visibility = 1, remove = 2, end = 3 };
struct InstanceOp {
    InstanceOpKind op{};
    std::uint32_t argument{};
};
// REPORT (client, round I1b): u32 member (the reported one) | u8 reason (1 griefing, 2 cheating, 3 name, 4 other) | u32 wid (the
// instance both share, 0 none). The relay answers with WORLD_NOTICE 14.
enum class ReportReason : std::uint8_t { griefing = 1, cheating = 2, name = 3, other = 4 };
struct Report {
    std::uint32_t member{};
    ReportReason reason{ReportReason::other};
    std::uint32_t wid{};
};
struct Ping {          // u32 nonce | u64 server time ms | i16 arrival margin ms (p0.1) | u8 suggested delay
    std::uint32_t nonce{};
    std::uint64_t server_time_ms{};
    std::int16_t margin_ms{};
    std::uint8_t delay{};
};
struct NetStat {       // u16 rtt ms | u16 spread ms | u8 delay | i16 margin ms | u32 sealed | u32 filled | u32 late | u8 flags (1 in a sealed stream)
    std::uint16_t rtt_ms{}, spread_ms{};
    std::uint8_t delay{};
    std::int16_t margin_ms{};
    std::uint32_t sealed{}, filled{}, late{};
    std::uint8_t flags{};
    static constexpr std::uint8_t in_stream = 1;
};
struct Close {         // u8 reason | str text
    CloseReason reason{};
    std::string text;
};
struct InstVisit {     // RESYNC: inst | visit
    std::uint32_t inst{}, visit{};
};
// REASSIGN: old inst | visit | new inst | u32 from tick | u8 mode | u8 flags
// (1 departing): after a fork this member's copy is the child; its stream
// comes with the new id from that tick.
struct Reassign {
    std::uint32_t inst{}, visit{}, new_inst{}, from_tick{};
    Mode mode{};
    std::uint8_t flags{};
};

// Encoders return the payload; decoders throw WireError on short or long
// payloads and on values out of range.
std::vector<std::uint8_t> encode(const Input&);
std::vector<std::uint8_t> encode(const Log&);
std::vector<std::uint8_t> encode(const Heading&);
std::vector<std::uint8_t> encode(const Barrier&);
std::vector<std::uint8_t> encode(const BarrierChoice&);
std::vector<std::uint8_t> encode(const Split&);
std::vector<std::uint8_t> encode(const Chunk&);
std::vector<std::uint8_t> encode(const SnapHash&);
std::vector<std::uint8_t> encode(const Ready&);
std::vector<std::uint8_t> encode(const JoinAt&);
std::vector<std::uint8_t> encode(const NetFp&);
std::vector<std::uint8_t> encode(const StateHash&);
std::vector<std::uint8_t> encode(const Checkpoint&);
std::vector<std::uint8_t> encode(const CheckpointBad&);
std::vector<std::uint8_t> encode(const CheckpointAsk&);
std::vector<std::uint8_t> encode(const State&);
std::vector<std::uint8_t> encode(const Intent&);
std::vector<std::uint8_t> encode(const Presence&, bool with_member);
// PRESENCE2 at version kPresenceVersion (whatever `version` holds).
std::vector<std::uint8_t> encode_presence2(const Presence&, bool with_member);
std::vector<std::uint8_t> encode(const Resume&);
std::vector<std::uint8_t> encode(const Story&);
std::vector<std::uint8_t> encode(const StoryAck&);
std::vector<std::uint8_t> encode(std::uint32_t rate); // RATE: u32 steps per second
std::vector<std::uint8_t> encode(const Pong&);
std::vector<std::uint8_t> encode(const Bye&);
std::vector<std::uint8_t> encode(const Welcome&);
std::vector<std::uint8_t> encode(const Ticks&);
std::vector<std::uint8_t> encode(const JoinReq&);
std::vector<std::uint8_t> encode(const BarrierOk&);
std::vector<std::uint8_t> encode(const BarrierWait&);
std::vector<std::uint8_t> encode_state_body(const StateData&);
std::vector<std::uint8_t> encode(const InstanceInfo&);
std::vector<std::uint8_t> encode(const WorldNotice&);
std::vector<std::uint8_t> encode(const MemberInfo&);
std::vector<std::uint8_t> encode(const InstanceOp&);
std::vector<std::uint8_t> encode(const Report&);
std::vector<std::uint8_t> encode(const Offer&);
std::vector<std::uint8_t> encode(const Ping&);
std::vector<std::uint8_t> encode(const NetStat&);
std::vector<std::uint8_t> encode(const Close&);
std::vector<std::uint8_t> encode(const InstVisit&);
std::vector<std::uint8_t> encode(const Reassign&);

Input decode_input(std::span<const std::uint8_t>);
Log decode_log(std::span<const std::uint8_t>);
Heading decode_heading(std::span<const std::uint8_t>);
Barrier decode_barrier(std::span<const std::uint8_t>);
BarrierChoice decode_barrier_choice(std::span<const std::uint8_t>);
Split decode_split(std::span<const std::uint8_t>);
Chunk decode_chunk(std::span<const std::uint8_t>);
SnapHash decode_snaphash(std::span<const std::uint8_t>);
Ready decode_ready(std::span<const std::uint8_t>);
JoinAt decode_join_at(std::span<const std::uint8_t>);
NetFp decode_netfp(std::span<const std::uint8_t>);
StateHash decode_statehash(std::span<const std::uint8_t>);
Checkpoint decode_checkpoint(std::span<const std::uint8_t>);
CheckpointBad decode_checkpoint_bad(std::span<const std::uint8_t>);
CheckpointAsk decode_checkpoint_ask(std::span<const std::uint8_t>);
State decode_state(std::span<const std::uint8_t>);
Intent decode_intent(std::span<const std::uint8_t>);
Presence decode_presence(std::span<const std::uint8_t>, bool with_member);
// Throws on version 0 and on damaged fields; the bytes a later version
// appends are skipped (`version` keeps what was sent).
Presence decode_presence2(std::span<const std::uint8_t>, bool with_member);
Resume decode_resume(std::span<const std::uint8_t>);
Story decode_story(std::span<const std::uint8_t>);
StoryAck decode_story_ack(std::span<const std::uint8_t>);
std::uint32_t decode_rate(std::span<const std::uint8_t>);
Pong decode_pong(std::span<const std::uint8_t>);
Bye decode_bye(std::span<const std::uint8_t>);
Welcome decode_welcome(std::span<const std::uint8_t>);
Ticks decode_ticks(std::span<const std::uint8_t>);
JoinReq decode_join_req(std::span<const std::uint8_t>);
BarrierOk decode_barrier_ok(std::span<const std::uint8_t>);
BarrierWait decode_barrier_wait(std::span<const std::uint8_t>);
// The assembled chunks' blob (inst and visit come from the chunks).
StateData decode_state_body(std::span<const std::uint8_t>);
InstanceInfo decode_instance_info(std::span<const std::uint8_t>);
WorldNotice decode_world_notice(std::span<const std::uint8_t>);
MemberInfo decode_member_info(std::span<const std::uint8_t>);
InstanceOp decode_instance_op(std::span<const std::uint8_t>);
Report decode_report(std::span<const std::uint8_t>);
Offer decode_offer(std::span<const std::uint8_t>);
Ping decode_ping(std::span<const std::uint8_t>);
NetStat decode_netstat(std::span<const std::uint8_t>);
Close decode_close(std::span<const std::uint8_t>);
InstVisit decode_inst_visit(std::span<const std::uint8_t>);
Reassign decode_reassign(std::span<const std::uint8_t>);

// Splits a large body into chunks of kStateChunk (at least one, also for an
// empty body).
std::vector<Chunk> make_chunks(std::uint32_t inst, std::uint32_t visit, std::span<const std::uint8_t> body);
// Reassembles chunks in order; returns the body once the last one arrived.
class ChunkAssembler {
public:
    // Throws WireError on a chunk that does not continue the current body.
    std::optional<std::vector<std::uint8_t>> add(const Chunk& chunk, std::size_t limit);
    void reset() { body_.clear(); total_ = 0; inst_ = visit_ = 0; }
    std::size_t received() const { return body_.size(); }
    std::uint32_t total() const { return total_; }

private:
    std::vector<std::uint8_t> body_;
    std::uint32_t total_ = 0, inst_ = 0, visit_ = 0;
};

// FNV-1a 64 (settings hash, canonical snapshot hash).
std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes, std::uint64_t seed = 0xcbf29ce484222325ull) noexcept;

} // namespace dw4::game::online::wire
