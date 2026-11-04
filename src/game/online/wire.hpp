#pragma once

// DW4R2, the relay protocol of the seamless co-op (docs/SEAMLESS.md 6.2, 6.3
// and the appendix "DW4R2 wire layout", which is the one layout the C++
// client, the C# relay hub (server/DW4Online/src/DW4Online.Relay/Protocol),
// the C# fake clients and the Python test client share).
//
// One TCP connection per client and room. The client sends one line
//   DW4R2 <ticket> <protocol> <dataHash> <settingsHash>\n
// (protocol and dataHash in decimal, settingsHash as 16 hex digits) and the
// relay answers
//   OK <member> <serverTimeMs>\n   or   ERR <code>\n
// (member = the room slot 0-9; codes protocol, ticket, data, settings, full).
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
//   INTENT 3    u8 slot | u8 kind (1 go without, 2 give up) | i32 argument
//   LEAD_FROM 4 u32 tick (the record is the last sealed tick; the remaining
//               member leads alone from that tick)
//   DELAY 5     u8 slot | u8 delay
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

struct WireError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The frame types (SEAMLESS.md 6.3, "Message types"): one number per message
// on both sides, never given twice. A new message takes the next free number
// of its direction (client to server 0x14, server to client 0x90 after round
// 62) and goes into the registry checks of tests/online_wire_tests.cpp and
// WireGoldenTests.cs. 0x8F is NETSTAT, in the field since DW4Online 0.1.2: a
// client that cannot decode a frame of a type it knows drops the connection
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
    room = 0x8A,
    ping = 0x8B,
    close = 0x8C,
    roster = 0x8D,
    reassign = 0x8E,
    netstat = 0x8F, // round 62: the member's link (older clients skip it)
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
enum class EventKind : std::uint8_t { join = 1, leave = 2, intent = 3, lead_from = 4, delay = 5 };
enum class IntentKind : std::uint8_t { go_without = 1, give_up = 2 };
enum class SplitKind : std::uint8_t { exit = 0, in_stage = 1 };
enum class Choice : std::uint8_t { wait = 0, alone = 1 };
// STATE reasons (why an instance is not joinable).
enum class NotJoinable : std::uint8_t { none = 0, entry_scene = 1, scene = 2, menu = 3, loading = 4, boss = 5, fork = 6, hub = 7 };
enum class Decision : std::uint8_t { new_instance = 1, join = 2, dissolve = 3, co_arrive = 4 };
enum class WaitReason : std::uint8_t { co_arrival = 1, not_joinable = 2, boss_lock = 3, snapshot = 4 };
// Why a barrier ended in NEW next to an instance of the same stage.
enum class Notice : std::uint8_t { none = 0, other_context = 1, full = 2, too_long = 3, hold_expired = 4, went_alone = 5, join_failed = 6 };
enum class StatePurpose : std::uint8_t { join = 1, co_arrival = 2, resync = 3, resume = 4 };
enum class RoomEvent : std::uint8_t {
    joined = 1, left = 2, owner_present = 3, owner_away = 4, lagging = 5, lag_over = 6, connected = 7, disconnected = 8
};
enum class CloseReason : std::uint8_t {
    left = 1, kicked = 2, room_closed = 3, replaced = 4, protocol = 5, abuse = 6, timeout = 7, stopping = 8
};
namespace state_flag {
inline constexpr std::uint8_t joinable = 1, membership_safe = 2, boss_lock = 4;
}

// ---- tick records and events
struct Event {
    EventKind kind{};
    std::uint8_t slot{};
    std::uint8_t point{};               // JOIN: the entrance point
    std::vector<std::uint8_t> carry;    // JOIN: the joiner's carry (engine bytes)
    std::uint8_t intent_kind{};         // INTENT
    std::int32_t argument{};            // INTENT
    std::uint32_t tick{};               // LEAD_FROM
    std::uint8_t delay{};               // DELAY
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
struct Heading {       // inst | str stage | u8 point | u64 session context hash
    std::uint32_t inst{};
    std::string stage;
    std::uint8_t point{};
    std::uint64_t context{};
};
struct Barrier {       // inst | visit | u32 barrier tick | str stage | u8 point | u8 alive | u8 present | u64 context | blob own carry
    std::uint32_t inst{}, visit{}, tick{};
    std::string stage;
    std::uint8_t point{};
    std::uint8_t alive{}, present{};
    std::uint64_t context{};
    std::vector<std::uint8_t> carry;
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
struct Ready {         // inst | visit | u32 tick (a multiple of 60) | u64 net fingerprint | u32 rate | blob carry
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t fingerprint{};
    std::uint32_t rate{};
    std::vector<std::uint8_t> carry;
};
struct JoinAt {        // inst | visit | u32 tick J
    std::uint32_t inst{}, visit{}, tick{};
};
struct NetFp {         // inst | visit | u32 tick (tick % 60 == 0) | u64 hash
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t hash{};
};
struct State {         // inst | visit | u32 tick | u8 flags | u64 context | u8 reason
    std::uint32_t inst{}, visit{}, tick{};
    std::uint8_t flags{};
    std::uint64_t context{};
    NotJoinable reason{};
};
struct Intent {        // inst | visit | u8 kind | i32 argument (sealed instances only)
    std::uint32_t inst{}, visit{};
    IntentKind kind{};
    std::int32_t argument{};
};
// PRESENCE: [u8 member (server -> client)] | str stage (empty: hidden) | inst |
// f32 x | f32 y | f32 z | f32 yaw | u8 species | u16 level | u16 hp |
// u16 max hp | u16 mp | u16 max mp. A member sends at most every 150 ms; an
// empty stage while it loads or watches a scene (its tag hides), hp 0 while
// it is defeated.
struct Presence {
    std::uint8_t member{};
    std::string stage;
    std::uint32_t inst{};
    float x{}, y{}, z{}, yaw{};
    std::uint8_t species{};
    std::uint16_t level{}, hp{}, max_hp{}, mp{}, max_mp{};
    bool operator==(const Presence&) const = default;
};
// STORY (SEAMLESS.md 8.3): the member's pending set against the confirmed
// room story, at every barrier (before BARRIER) and every ten seconds while it
// is not empty. inst | visit (the member's instance now) | u32 the confirmed
// revision it was computed against | u8 phase (0: no claim, else the engine's
// phase index 1-7) | u16 n + n x u16 flags | u8 n + n x u8 keys | u8 n + n x
// (u8 counter | u32 line inst | u32 line visit | i32 growth): the cumulative
// growth of an additive counter in an engine line (instance and visit the
// growth was made in; a fork's child counts from its fork under its own id).
struct Story {
    std::uint32_t inst{}, visit{}, revision{};
    engine::StoryDelta delta;
};
inline constexpr std::size_t kMaxStoryLines = 32;
// STORY_ACK: u32 revision | blob story (1,100 bytes, story::encode_story; empty
// while the room has none) | u8 flags (1: the answer to this member's STORY) |
// u16 n + n x (u8 kind | u16 index | u8 reason) refused keys
// (engine::StoryRefused). The server sends it after WELCOME (the confirmed
// story to start from), to every connected member whenever the room story's
// revision rises, and to a STORY's sender with what it refused.
struct StoryAck {
    std::uint32_t revision{};
    std::vector<std::uint8_t> story;
    std::uint8_t flags{};
    std::vector<engine::StoryRefused> refused;
    static constexpr std::uint8_t answer = 1;
};
struct Resume {        // inst | visit | u8 slot | u32 last executed tick
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    std::uint32_t last_tick{};
};
struct Pong {          // u32 nonce | u64 client time ms
    std::uint32_t nonce{};
    std::uint64_t client_time_ms{};
};
struct Bye {           // u8 reason
    std::uint8_t reason{};
};

// ---- server -> client
// WELCOME: u8 member | inst | visit | u8 slot | u8 mode | str stage |
// u32 last tick (kNoTick: none) | u8 delay | u16 max catch-up seconds |
// u8 flags (1 resumable: the member still holds that slot; send RESUME).
struct Welcome {
    std::uint8_t member{};
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    Mode mode{};
    std::string stage;
    std::uint32_t last_tick = kNoTick;
    std::uint8_t delay{};
    std::uint16_t max_catch_up{};
    std::uint8_t flags{};
    static constexpr std::uint8_t resumable = 1;
};
struct Ticks {         // SEALED / LIVE: inst | visit | u32 first tick | u16 count | tick records
    std::uint32_t inst{}, visit{}, first{};
    std::vector<TickBody> ticks;
};
struct JoinReq {       // inst | visit | u8 slot | u8 the leader's delay
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    std::uint8_t delay{};
};
struct CoArrival {     // u8 slot | u8 member | blob carry
    std::uint8_t slot{};
    std::uint8_t member{};
    std::vector<std::uint8_t> carry;
    bool operator==(const CoArrival&) const = default;
};
// BARRIER_OK: old inst | old visit | u8 decision | new inst | new visit |
// str stage | u8 point | u8 own slot | 4 x i8 slot map (old slot -> new slot
// or -1) | u8 mode | u8 delay | u8 n + n co-arrivals | u32 story revision |
// blob story (the room story, 1,100 bytes as in STORY_ACK, the same bytes for
// every member of the group; empty: none) | blob dropped (the room's dropped
// set, engine::story::encode_keys: u16 n + n x u16 flags | u8 n + n x u8 keys
// | u8 n + n x u8 counters; empty: none) | u8 notice | u8 n + n str names.
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
    std::vector<std::uint8_t> story, dropped;
    Notice notice{};
    std::vector<std::string> names;
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
    std::uint32_t first{};
    std::vector<TickBody> ticks;
};
struct Room {          // u8 notice | u8 member | u64 account | str name | u8 flags (1 owner)
    RoomEvent kind{};
    std::uint8_t member{};
    std::uint64_t account{};
    std::string name;
    std::uint8_t flags{};
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
struct RosterEntry {   // u8 member | str stage | inst | u16 level | u16 hp | u16 max hp | u8 flags (1 connected, 2 lagging, 4 owner)
    std::uint8_t member{};
    std::string stage;
    std::uint32_t inst{};
    std::uint16_t level{}, hp{}, max_hp{};
    std::uint8_t flags{};
    bool operator==(const RosterEntry&) const = default;
};
struct Roster {        // u8 n + n entries (once per second)
    std::vector<RosterEntry> entries;
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
std::vector<std::uint8_t> encode(const State&);
std::vector<std::uint8_t> encode(const Intent&);
std::vector<std::uint8_t> encode(const Presence&, bool with_member);
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
std::vector<std::uint8_t> encode(const Room&);
std::vector<std::uint8_t> encode(const Ping&);
std::vector<std::uint8_t> encode(const NetStat&);
std::vector<std::uint8_t> encode(const Close&);
std::vector<std::uint8_t> encode(const Roster&);
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
State decode_state(std::span<const std::uint8_t>);
Intent decode_intent(std::span<const std::uint8_t>);
Presence decode_presence(std::span<const std::uint8_t>, bool with_member);
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
Room decode_room(std::span<const std::uint8_t>);
Ping decode_ping(std::span<const std::uint8_t>);
NetStat decode_netstat(std::span<const std::uint8_t>);
Close decode_close(std::span<const std::uint8_t>);
Roster decode_roster(std::span<const std::uint8_t>);
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
