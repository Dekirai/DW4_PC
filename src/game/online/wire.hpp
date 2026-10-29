#pragma once

// DW4R2, the relay protocol of the seamless co-op (docs/SEAMLESS.md 6.2, 6.3).
// One TCP connection per client and room. The client sends one line
//   DW4R2 <ticket> <protocol> <dataHash> <settingsHash>\n
// (protocol, dataHash and settingsHash in decimal) and the relay answers
//   OK <memberId> <serverTimeMs>\n   or   ERR <code>\n
// Then frames follow in both directions: type u8 | length u32 LE | payload.
// A frame above 1 MiB is refused; state transfers come in chunks.
//
// Every integer is little endian; i8 / i16 / i32 are two's complement; f32
// is IEEE single. str8 = u8 length + bytes (ASCII), blob = u32 length +
// bytes. inst = instance id u32, visit = stage load count u32, tick u32.
// A tick body (SEALED, LIVE, the log of STATE_DATA) is
//   u8 present | pad (7 B, pad_wire.hpp) for every present slot in slot order |
//   u8 event count | events
// and an event is u8 kind followed by
//   JOIN      u8 slot | u32 member | i16 entrance point | blob carry
//   LEAVE     u8 slot
//   INTENT    u8 slot | u8 kind | i32 argument
//   LEAD_FROM u32 tick (from here the remaining member leads alone)
//   DELAY     u8 slot | u8 delay
// The layouts of the payloads are next to each struct below.
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
inline constexpr std::size_t kStateChunk = 60 * 1024; // STATE_DATA / SNAPSHOT chunk size the client sends and expects

struct WireError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Type : std::uint8_t {
    // client -> server
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
    // server -> client
    sealed = 0x41,
    live = 0x42,
    join_req = 0x43,
    barrier_ok = 0x44,
    barrier_wait = 0x45,
    state_data = 0x46,
    resync = 0x47,
    story_ack = 0x48,
    presence_of = 0x49,
    room = 0x4A,
    ping = 0x4B,
    kick = 0x4C,
    close = 0x4D,
    rekey = 0x4E,
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
    std::string str8();
    std::vector<std::uint8_t> blob(std::size_t limit = kMaxFrame * 16);
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

// ---- tick bodies and events
enum class EventKind : std::uint8_t { join = 1, leave = 2, intent = 3, lead_from = 4, delay = 5 };
struct Event {
    EventKind kind{};
    std::uint8_t slot{};
    std::uint32_t member{};             // JOIN: the member's id (names, notices)
    std::int16_t point{};               // JOIN: the entrance point
    std::vector<std::uint8_t> carry;    // JOIN: the joiner's carry (engine bytes)
    std::uint8_t intent_kind{};         // INTENT
    std::int32_t argument{};            // INTENT
    std::uint32_t tick{};               // LEAD_FROM
    std::uint8_t delay{};               // DELAY
    bool operator==(const Event&) const = default;
};
struct TickBody {
    std::uint8_t present{};
    std::array<WirePad, 4> pads{};      // valid for present slots
    std::vector<Event> events;
    bool operator==(const TickBody&) const = default;
};
void write_tick(Writer& w, const TickBody& tick);
TickBody read_tick(Reader& r);

// ---- client -> server
struct Input {         // inst | visit | tick | pad
    std::uint32_t inst{}, visit{}, tick{};
    WirePad pad;
};
struct Log {           // inst | visit | u32 first tick | u16 count | count pads
    std::uint32_t inst{}, visit{}, first{};
    std::vector<WirePad> pads;
};
struct Heading {       // inst | str8 stage | i16 point
    std::uint32_t inst{};
    std::string stage;
    std::int16_t point{};
};
struct Carry {
    std::uint8_t slot{};
    std::vector<std::uint8_t> bytes;
    bool operator==(const Carry&) const = default;
};
struct Barrier {       // inst | visit | u32 barrier tick | str8 stage | i16 point | u8 alive | u8 present | u64 context | u8 n | n x (u8 slot | blob carry)
    std::uint32_t inst{}, visit{}, tick{};
    std::string stage;
    std::int16_t point{};
    std::uint8_t alive{}, present{};
    std::uint64_t context{};
    std::vector<Carry> carries;
};
enum class Choice : std::uint8_t { wait = 0, alone = 1 };
struct BarrierChoice { // inst | u8 choice
    std::uint32_t inst{};
    Choice choice{};
};
enum class SplitKind : std::uint8_t { exit = 0, in_stage = 1 };
struct Split {         // inst | visit | tick | u8 leavers | u8 kind
    std::uint32_t inst{}, visit{}, tick{};
    std::uint8_t leavers{};
    SplitKind kind{};
};
struct Chunk {         // SNAPSHOT / STATE_DATA chunk: inst | visit | [u8 slot (STATE_DATA)] | u32 offset | u32 total | blob bytes
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    std::uint32_t offset{}, total{};
    std::vector<std::uint8_t> bytes;
};
struct SnapHash {      // inst | visit | u64 canonical hash
    std::uint32_t inst{}, visit{};
    std::uint64_t hash{};
};
struct Ready {         // inst | visit | tick | u64 net fingerprint | u32 rate | blob carry
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t fingerprint{};
    std::uint32_t rate{};
    std::vector<std::uint8_t> carry;
};
struct JoinAt {        // inst | visit | tick
    std::uint32_t inst{}, visit{}, tick{};
};
struct NetFp {         // inst | visit | tick | u64 hash
    std::uint32_t inst{}, visit{}, tick{};
    std::uint64_t hash{};
};
namespace state_flag {
inline constexpr std::uint8_t joinable = 1, membership_safe = 2, boss_lock = 4;
}
enum class NotJoinable : std::uint8_t { none = 0, hub = 1, entry_scene = 2, demo = 3, menu = 4, loading = 5, boss = 6, fork = 7 };
struct State {         // inst | visit | u8 flags | u64 context | u8 reason
    std::uint32_t inst{}, visit{};
    std::uint8_t flags{};
    std::uint64_t context{};
    NotJoinable reason{};
};
enum class IntentKind : std::uint8_t { go_without = 1, give_up = 2 };
struct Intent {        // inst | visit | u8 kind | i32 argument
    std::uint32_t inst{}, visit{};
    IntentKind kind{};
    std::int32_t argument{};
};
namespace presence_flag {
inline constexpr std::uint8_t defeated = 1, hidden = 2; // hidden: loading, demo, menu
}
struct Presence {      // [u32 member (server -> client)] | str8 stage | inst | f32 x | f32 y | f32 z | f32 yaw | u8 species | u16 level | u16 hp | u16 max hp | u16 mp | u16 max mp | u8 flags
    std::uint32_t member{};
    std::string stage;
    std::uint32_t inst{};
    float x{}, y{}, z{}, yaw{};
    std::uint8_t species{};
    std::uint16_t level{}, hp{}, max_hp{}, mp{}, max_mp{};
    std::uint8_t flags{};
    bool operator==(const Presence&) const = default;
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

// ---- server -> client
struct Sealed {        // inst | visit | tick | tick body
    std::uint32_t inst{}, visit{}, tick{};
    TickBody body;
};
struct Live {          // inst | visit | u32 first tick | u16 count | count tick bodies
    std::uint32_t inst{}, visit{}, first{};
    std::vector<TickBody> ticks;
};
struct JoinReq {       // inst | visit | u8 slot
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
};
enum class Decision : std::uint8_t { new_instance = 0, join = 1, dissolve = 2, co_arrive = 3 };
struct BarrierOk {     // inst | visit | u8 decision | u32 new inst | u32 new visit | u8 own slot | 4 x i8 slot map | 4 x u32 slot members | u8 n | n x (u8 slot | blob carry) | u32 story revision | blob story | blob dropped | u8 rule flags | u8 present
    std::uint32_t inst{}, visit{};
    Decision decision{};
    std::uint32_t new_inst{}, new_visit{};
    std::uint8_t own_slot{};
    std::array<std::int8_t, 4> slot_map{-1, -1, -1, -1}; // old slot -> new slot (-1: not in the new instance)
    std::array<std::uint32_t, 4> slot_members{};          // the member in each slot of the new instance (0: empty)
    std::vector<Carry> co_arrivals;
    std::uint32_t story_revision{};
    std::vector<std::uint8_t> story, dropped;
    std::uint8_t rule_flags{}; // bit 0 online rules on (always 1 online), bit 1 own cameras, bit 2 leash off
    std::uint8_t present{};
};
enum class WaitReason : std::uint8_t { co_arrival = 0, entry_scene = 1, boss_lock = 2 };
struct BarrierWait {   // inst | u8 reason | u16 seconds left | u8 n | n x str8 name
    std::uint32_t inst{};
    WaitReason reason{};
    std::uint16_t seconds_left{};
    std::vector<std::string> names;
};
struct Fork {          // u32 tick | u8 leavers | u8 kind | u8 side (0: the stayers, 1: the leavers)
    std::uint32_t tick{};
    std::uint8_t leavers{};
    SplitKind kind{};
    std::uint8_t side{};
    bool operator==(const Fork&) const = default;
};
// The assembled STATE_DATA body: blob snapshot (raw, the leader's) | u32 first tick | u32 count | count tick bodies | u8 n | n forks
struct StateData {
    std::uint32_t inst{}, visit{};
    std::uint8_t slot{};
    std::vector<std::uint8_t> snapshot;
    std::uint32_t first{};
    std::vector<TickBody> ticks;
    std::vector<Fork> forks;
};
enum class RoomEvent : std::uint8_t { joined = 0, left = 1, owner_present = 2, story_frozen = 3, story_revision = 4 };
struct Room {          // u8 kind | u32 member | str8 name | u32 value
    RoomEvent kind{};
    std::uint32_t member{};
    std::string name;
    std::uint32_t value{};
};
struct Ping {          // u32 nonce | u64 server time ms | i16 arrival margin (ticks, p0.1) | u8 suggested delay
    std::uint32_t nonce{};
    std::uint64_t server_time_ms{};
    std::int16_t margin{};
    std::uint8_t delay{};
};
struct Reason {        // KICK / CLOSE / BYE: str8 reason
    std::string reason;
};
struct InstVisit {     // RESYNC: inst | visit
    std::uint32_t inst{}, visit{};
};
// REKEY: the side of a fork that does not keep the instance id continues
// under a new one (sent to that side's members before the first frame with
// the new id): u32 old inst | u32 old visit | u32 new inst | u32 new visit
struct Rekey {
    std::uint32_t inst{}, visit{}, new_inst{}, new_visit{};
};

// Encoders return the payload; decoders throw WireError on short or long
// payloads.
std::vector<std::uint8_t> encode(const Input&);
std::vector<std::uint8_t> encode(const Log&);
std::vector<std::uint8_t> encode(const Heading&);
std::vector<std::uint8_t> encode(const Barrier&);
std::vector<std::uint8_t> encode(const BarrierChoice&);
std::vector<std::uint8_t> encode(const Split&);
std::vector<std::uint8_t> encode(const Chunk&, bool with_slot);
std::vector<std::uint8_t> encode(const SnapHash&);
std::vector<std::uint8_t> encode(const Ready&);
std::vector<std::uint8_t> encode(const JoinAt&);
std::vector<std::uint8_t> encode(const NetFp&);
std::vector<std::uint8_t> encode(const State&);
std::vector<std::uint8_t> encode(const Intent&);
std::vector<std::uint8_t> encode(const Presence&, bool with_member);
std::vector<std::uint8_t> encode(const Resume&);
std::vector<std::uint8_t> encode(std::uint32_t rate); // RATE: u32 steps per second
std::vector<std::uint8_t> encode(const Pong&);
std::vector<std::uint8_t> encode(const Sealed&);
std::vector<std::uint8_t> encode(const Live&);
std::vector<std::uint8_t> encode(const JoinReq&);
std::vector<std::uint8_t> encode(const BarrierOk&);
std::vector<std::uint8_t> encode(const BarrierWait&);
std::vector<std::uint8_t> encode_state_body(const StateData&);
std::vector<std::uint8_t> encode(const Room&);
std::vector<std::uint8_t> encode(const Ping&);
std::vector<std::uint8_t> encode(const Reason&);
std::vector<std::uint8_t> encode(const InstVisit&);
std::vector<std::uint8_t> encode(const Rekey&);

Input decode_input(std::span<const std::uint8_t>);
Log decode_log(std::span<const std::uint8_t>);
Heading decode_heading(std::span<const std::uint8_t>);
Barrier decode_barrier(std::span<const std::uint8_t>);
BarrierChoice decode_barrier_choice(std::span<const std::uint8_t>);
Split decode_split(std::span<const std::uint8_t>);
Chunk decode_chunk(std::span<const std::uint8_t>, bool with_slot);
SnapHash decode_snaphash(std::span<const std::uint8_t>);
Ready decode_ready(std::span<const std::uint8_t>);
JoinAt decode_join_at(std::span<const std::uint8_t>);
NetFp decode_netfp(std::span<const std::uint8_t>);
State decode_state(std::span<const std::uint8_t>);
Intent decode_intent(std::span<const std::uint8_t>);
Presence decode_presence(std::span<const std::uint8_t>, bool with_member);
Resume decode_resume(std::span<const std::uint8_t>);
std::uint32_t decode_rate(std::span<const std::uint8_t>);
Pong decode_pong(std::span<const std::uint8_t>);
Sealed decode_sealed(std::span<const std::uint8_t>);
Live decode_live(std::span<const std::uint8_t>);
JoinReq decode_join_req(std::span<const std::uint8_t>);
BarrierOk decode_barrier_ok(std::span<const std::uint8_t>);
BarrierWait decode_barrier_wait(std::span<const std::uint8_t>);
// The body of the assembled chunks (inst, visit and slot come from the chunks).
StateData decode_state_body(std::span<const std::uint8_t>);
Room decode_room(std::span<const std::uint8_t>);
Ping decode_ping(std::span<const std::uint8_t>);
Reason decode_reason(std::span<const std::uint8_t>);
InstVisit decode_inst_visit(std::span<const std::uint8_t>);
Rekey decode_rekey(std::span<const std::uint8_t>);

// Splits a large body into chunks of kStateChunk (at least one, also for an
// empty body).
std::vector<Chunk> make_chunks(std::uint32_t inst, std::uint32_t visit, std::uint8_t slot, std::span<const std::uint8_t> body);
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
