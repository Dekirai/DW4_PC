#include "wire.hpp"

#include <bit>
#include <charconv>
#include <cstring>

namespace dw4::game::online::wire {

const char* type_name(Type type) noexcept {
    switch (type) {
    case Type::input: return "INPUT";
    case Type::log: return "LOG";
    case Type::heading: return "HEADING";
    case Type::barrier: return "BARRIER";
    case Type::barrier_choice: return "BARRIER_CHOICE";
    case Type::split: return "SPLIT";
    case Type::snapshot: return "SNAPSHOT";
    case Type::snaphash: return "SNAPHASH";
    case Type::ready: return "READY";
    case Type::join_at: return "JOIN_AT";
    case Type::netfp: return "NETFP";
    case Type::state: return "STATE";
    case Type::intent: return "INTENT";
    case Type::story: return "STORY";
    case Type::presence: return "PRESENCE";
    case Type::resume: return "RESUME";
    case Type::rate: return "RATE";
    case Type::pong: return "PONG";
    case Type::bye: return "BYE";
    case Type::sealed: return "SEALED";
    case Type::live: return "LIVE";
    case Type::join_req: return "JOIN_REQ";
    case Type::barrier_ok: return "BARRIER_OK";
    case Type::barrier_wait: return "BARRIER_WAIT";
    case Type::state_data: return "STATE_DATA";
    case Type::resync: return "RESYNC";
    case Type::story_ack: return "STORY_ACK";
    case Type::presence_of: return "PRESENCE";
    case Type::room: return "ROOM";
    case Type::ping: return "PING";
    case Type::kick: return "KICK";
    case Type::close: return "CLOSE";
    case Type::rekey: return "REKEY";
    }
    return "?";
}

// ---- writer / reader
void Writer::u16(std::uint16_t v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
}
void Writer::u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}
void Writer::u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}
void Writer::f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
void Writer::str8(std::string_view v) {
    if (v.size() > 255) throw WireError("string longer than 255 bytes");
    u8(std::uint8_t(v.size()));
    out.insert(out.end(), v.begin(), v.end());
}
void Writer::blob(std::span<const std::uint8_t> v) {
    u32(std::uint32_t(v.size()));
    raw(v);
}
void Writer::pad(const WirePad& p) {
    const std::size_t at = out.size();
    out.resize(at + kWirePadBytes);
    write_pad(out.data() + at, p);
}

void Reader::need(std::size_t n) const {
    if (n > in.size() - at) throw WireError("short message");
}
std::uint8_t Reader::u8() {
    need(1);
    return in[at++];
}
std::uint16_t Reader::u16() {
    need(2);
    const std::uint16_t v = std::uint16_t(in[at] | (in[at + 1] << 8));
    at += 2;
    return v;
}
std::uint32_t Reader::u32() {
    need(4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t(in[at + std::size_t(i)]) << (8 * i);
    at += 4;
    return v;
}
std::uint64_t Reader::u64() {
    need(8);
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= std::uint64_t(in[at + std::size_t(i)]) << (8 * i);
    at += 8;
    return v;
}
float Reader::f32() { return std::bit_cast<float>(u32()); }
std::string Reader::str8() {
    const std::size_t n = u8();
    need(n);
    std::string s(reinterpret_cast<const char*>(in.data() + at), n);
    at += n;
    return s;
}
std::vector<std::uint8_t> Reader::blob(std::size_t limit) {
    const std::size_t n = u32();
    if (n > limit) throw WireError("blob too large");
    need(n);
    std::vector<std::uint8_t> v(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + n));
    at += n;
    return v;
}
WirePad Reader::pad() {
    need(kWirePadBytes);
    const WirePad p = read_pad(in.data() + at);
    at += kWirePadBytes;
    return p;
}
void Reader::end() const {
    if (!done()) throw WireError("trailing bytes in message");
}

// ---- handshake
std::string hello_line(std::string_view ticket, std::uint32_t protocol, std::uint64_t data_hash, std::uint64_t settings_hash) {
    return "DW4R2 " + std::string(ticket) + " " + std::to_string(protocol) + " " + std::to_string(data_hash) + " " +
           std::to_string(settings_hash) + "\n";
}

namespace {
std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && line[at] == ' ') ++at;
        const std::size_t start = at;
        while (at < line.size() && line[at] != ' ') ++at;
        if (at > start) out.push_back(line.substr(start, at - start));
    }
    return out;
}
template <class T> bool number(std::string_view text, T& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}
} // namespace

std::optional<HelloAnswer> parse_hello_answer(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto w = words(line);
    HelloAnswer answer;
    if (w.size() == 3 && w[0] == "OK" && number(w[1], answer.member_id) && number(w[2], answer.server_time_ms)) {
        answer.ok = true;
        return answer;
    }
    if (w.size() == 2 && w[0] == "ERR") {
        answer.error = std::string(w[1]);
        return answer;
    }
    return std::nullopt;
}

std::optional<Hello> parse_hello(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto w = words(line);
    Hello hello;
    if (w.size() != 5 || w[0] != "DW4R2") return std::nullopt;
    hello.ticket = std::string(w[1]);
    if (!number(w[2], hello.protocol) || !number(w[3], hello.data_hash) || !number(w[4], hello.settings_hash)) return std::nullopt;
    return hello;
}

// ---- framing
std::vector<std::uint8_t> encode_frame(Type type, std::span<const std::uint8_t> payload) {
    if (payload.size() > kMaxFrame) throw WireError("frame above 1 MiB");
    Writer w;
    w.out.reserve(payload.size() + 5);
    w.u8(std::uint8_t(type));
    w.u32(std::uint32_t(payload.size()));
    w.raw(payload);
    return std::move(w.out);
}

void FrameReader::feed(std::span<const std::uint8_t> bytes) {
    if (at_ > 0 && at_ == buffer_.size()) {
        buffer_.clear();
        at_ = 0;
    } else if (at_ > 65536) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + std::ptrdiff_t(at_));
        at_ = 0;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

std::optional<Frame> FrameReader::next() {
    if (buffer_.size() - at_ < 5) return std::nullopt;
    const std::uint8_t* p = buffer_.data() + at_;
    const std::uint32_t length = std::uint32_t(p[1]) | (std::uint32_t(p[2]) << 8) | (std::uint32_t(p[3]) << 16) | (std::uint32_t(p[4]) << 24);
    if (length > kMaxFrame) throw WireError("frame above 1 MiB");
    if (buffer_.size() - at_ < 5 + std::size_t(length)) return std::nullopt;
    Frame frame{Type(p[0]), std::vector<std::uint8_t>(p + 5, p + 5 + length)};
    at_ += 5 + std::size_t(length);
    return frame;
}

// ---- ticks
void write_tick(Writer& w, const TickBody& tick) {
    w.u8(tick.present);
    for (int slot = 0; slot < 4; ++slot)
        if (tick.present & (1u << slot)) w.pad(tick.pads[std::size_t(slot)]);
    if (tick.events.size() > 255) throw WireError("too many events in a tick");
    w.u8(std::uint8_t(tick.events.size()));
    for (const auto& e : tick.events) {
        w.u8(std::uint8_t(e.kind));
        switch (e.kind) {
        case EventKind::join:
            w.u8(e.slot);
            w.u32(e.member);
            w.i16(e.point);
            w.blob(e.carry);
            break;
        case EventKind::leave: w.u8(e.slot); break;
        case EventKind::intent:
            w.u8(e.slot);
            w.u8(e.intent_kind);
            w.i32(e.argument);
            break;
        case EventKind::lead_from: w.u32(e.tick); break;
        case EventKind::delay:
            w.u8(e.slot);
            w.u8(e.delay);
            break;
        default: throw WireError("unknown event kind");
        }
    }
}

TickBody read_tick(Reader& r) {
    TickBody tick;
    tick.present = r.u8();
    if (tick.present & 0xF0) throw WireError("present mask above slot 3");
    for (int slot = 0; slot < 4; ++slot)
        if (tick.present & (1u << slot)) tick.pads[std::size_t(slot)] = r.pad();
    const int events = r.u8();
    for (int i = 0; i < events; ++i) {
        Event e;
        e.kind = EventKind(r.u8());
        switch (e.kind) {
        case EventKind::join:
            e.slot = r.u8();
            e.member = r.u32();
            e.point = r.i16();
            e.carry = r.blob(kMaxFrame);
            break;
        case EventKind::leave: e.slot = r.u8(); break;
        case EventKind::intent:
            e.slot = r.u8();
            e.intent_kind = r.u8();
            e.argument = r.i32();
            break;
        case EventKind::lead_from: e.tick = r.u32(); break;
        case EventKind::delay:
            e.slot = r.u8();
            e.delay = r.u8();
            break;
        default: throw WireError("unknown event kind");
        }
        if (e.kind != EventKind::lead_from && e.slot > 3) throw WireError("event slot above 3");
        tick.events.push_back(std::move(e));
    }
    return tick;
}

// ---- encoders
namespace {
void iv(Writer& w, std::uint32_t inst, std::uint32_t visit) {
    w.u32(inst);
    w.u32(visit);
}
Reader reader(std::span<const std::uint8_t> p) { return Reader{p}; }
} // namespace

std::vector<std::uint8_t> encode(const Input& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    w.pad(m.pad);
    return w.out;
}
std::vector<std::uint8_t> encode(const Log& m) {
    if (m.pads.size() > 0xFFFF) throw WireError("log batch too long");
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.first);
    w.u16(std::uint16_t(m.pads.size()));
    for (const auto& p : m.pads) w.pad(p);
    return w.out;
}
std::vector<std::uint8_t> encode(const Heading& m) {
    Writer w;
    w.u32(m.inst);
    w.str8(m.stage);
    w.i16(m.point);
    return w.out;
}
std::vector<std::uint8_t> encode(const Barrier& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    w.str8(m.stage);
    w.i16(m.point);
    w.u8(m.alive);
    w.u8(m.present);
    w.u64(m.context);
    w.u8(std::uint8_t(m.carries.size()));
    for (const auto& c : m.carries) {
        w.u8(c.slot);
        w.blob(c.bytes);
    }
    return w.out;
}
std::vector<std::uint8_t> encode(const BarrierChoice& m) {
    Writer w;
    w.u32(m.inst);
    w.u8(std::uint8_t(m.choice));
    return w.out;
}
std::vector<std::uint8_t> encode(const Split& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    w.u8(m.leavers);
    w.u8(std::uint8_t(m.kind));
    return w.out;
}
std::vector<std::uint8_t> encode(const Chunk& m, bool with_slot) {
    Writer w;
    iv(w, m.inst, m.visit);
    if (with_slot) w.u8(m.slot);
    w.u32(m.offset);
    w.u32(m.total);
    w.blob(m.bytes);
    return w.out;
}
std::vector<std::uint8_t> encode(const SnapHash& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u64(m.hash);
    return w.out;
}
std::vector<std::uint8_t> encode(const Ready& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    w.u64(m.fingerprint);
    w.u32(m.rate);
    w.blob(m.carry);
    return w.out;
}
std::vector<std::uint8_t> encode(const JoinAt& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    return w.out;
}
std::vector<std::uint8_t> encode(const NetFp& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    w.u64(m.hash);
    return w.out;
}
std::vector<std::uint8_t> encode(const State& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u8(m.flags);
    w.u64(m.context);
    w.u8(std::uint8_t(m.reason));
    return w.out;
}
std::vector<std::uint8_t> encode(const Intent& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u8(std::uint8_t(m.kind));
    w.i32(m.argument);
    return w.out;
}
std::vector<std::uint8_t> encode(const Presence& m, bool with_member) {
    Writer w;
    if (with_member) w.u32(m.member);
    w.str8(m.stage);
    w.u32(m.inst);
    w.f32(m.x);
    w.f32(m.y);
    w.f32(m.z);
    w.f32(m.yaw);
    w.u8(m.species);
    w.u16(m.level);
    w.u16(m.hp);
    w.u16(m.max_hp);
    w.u16(m.mp);
    w.u16(m.max_mp);
    w.u8(m.flags);
    return w.out;
}
std::vector<std::uint8_t> encode(const Resume& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u8(m.slot);
    w.u32(m.last_tick);
    return w.out;
}
std::vector<std::uint8_t> encode(std::uint32_t rate) {
    Writer w;
    w.u32(rate);
    return w.out;
}
std::vector<std::uint8_t> encode(const Pong& m) {
    Writer w;
    w.u32(m.nonce);
    w.u64(m.client_time_ms);
    return w.out;
}
std::vector<std::uint8_t> encode(const Sealed& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.tick);
    write_tick(w, m.body);
    return w.out;
}
std::vector<std::uint8_t> encode(const Live& m) {
    if (m.ticks.size() > 0xFFFF) throw WireError("live batch too long");
    Writer w;
    iv(w, m.inst, m.visit);
    w.u32(m.first);
    w.u16(std::uint16_t(m.ticks.size()));
    for (const auto& t : m.ticks) write_tick(w, t);
    return w.out;
}
std::vector<std::uint8_t> encode(const JoinReq& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u8(m.slot);
    return w.out;
}
std::vector<std::uint8_t> encode(const BarrierOk& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    w.u8(std::uint8_t(m.decision));
    w.u32(m.new_inst);
    w.u32(m.new_visit);
    w.u8(m.own_slot);
    for (const auto s : m.slot_map) w.i8(s);
    for (const auto member : m.slot_members) w.u32(member);
    w.u8(std::uint8_t(m.co_arrivals.size()));
    for (const auto& c : m.co_arrivals) {
        w.u8(c.slot);
        w.blob(c.bytes);
    }
    w.u32(m.story_revision);
    w.blob(m.story);
    w.blob(m.dropped);
    w.u8(m.rule_flags);
    w.u8(m.present);
    return w.out;
}
std::vector<std::uint8_t> encode(const BarrierWait& m) {
    Writer w;
    w.u32(m.inst);
    w.u8(std::uint8_t(m.reason));
    w.u16(m.seconds_left);
    w.u8(std::uint8_t(m.names.size()));
    for (const auto& n : m.names) w.str8(n);
    return w.out;
}
std::vector<std::uint8_t> encode_state_body(const StateData& m) {
    Writer w;
    w.blob(m.snapshot);
    w.u32(m.first);
    w.u32(std::uint32_t(m.ticks.size()));
    for (const auto& t : m.ticks) write_tick(w, t);
    w.u8(std::uint8_t(m.forks.size()));
    for (const auto& f : m.forks) {
        w.u32(f.tick);
        w.u8(f.leavers);
        w.u8(std::uint8_t(f.kind));
        w.u8(f.side);
    }
    return w.out;
}
std::vector<std::uint8_t> encode(const Room& m) {
    Writer w;
    w.u8(std::uint8_t(m.kind));
    w.u32(m.member);
    w.str8(m.name);
    w.u32(m.value);
    return w.out;
}
std::vector<std::uint8_t> encode(const Ping& m) {
    Writer w;
    w.u32(m.nonce);
    w.u64(m.server_time_ms);
    w.i16(m.margin);
    w.u8(m.delay);
    return w.out;
}
std::vector<std::uint8_t> encode(const Reason& m) {
    Writer w;
    w.str8(m.reason);
    return w.out;
}
std::vector<std::uint8_t> encode(const InstVisit& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    return w.out;
}
std::vector<std::uint8_t> encode(const Rekey& m) {
    Writer w;
    iv(w, m.inst, m.visit);
    iv(w, m.new_inst, m.new_visit);
    return w.out;
}

// ---- decoders
Input decode_input(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Input m{r.u32(), r.u32(), r.u32(), {}};
    m.pad = r.pad();
    r.end();
    return m;
}
Log decode_log(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Log m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.first = r.u32();
    const int n = r.u16();
    for (int i = 0; i < n; ++i) m.pads.push_back(r.pad());
    r.end();
    return m;
}
Heading decode_heading(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Heading m;
    m.inst = r.u32();
    m.stage = r.str8();
    m.point = r.i16();
    r.end();
    return m;
}
Barrier decode_barrier(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Barrier m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.stage = r.str8();
    m.point = r.i16();
    m.alive = r.u8();
    m.present = r.u8();
    m.context = r.u64();
    const int n = r.u8();
    for (int i = 0; i < n; ++i) {
        Carry c;
        c.slot = r.u8();
        c.bytes = r.blob(kMaxFrame);
        m.carries.push_back(std::move(c));
    }
    r.end();
    return m;
}
BarrierChoice decode_barrier_choice(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    BarrierChoice m;
    m.inst = r.u32();
    m.choice = Choice(r.u8());
    r.end();
    return m;
}
Split decode_split(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Split m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.leavers = r.u8();
    m.kind = SplitKind(r.u8());
    r.end();
    return m;
}
Chunk decode_chunk(std::span<const std::uint8_t> p, bool with_slot) {
    auto r = reader(p);
    Chunk m;
    m.inst = r.u32();
    m.visit = r.u32();
    if (with_slot) m.slot = r.u8();
    m.offset = r.u32();
    m.total = r.u32();
    m.bytes = r.blob(kMaxFrame);
    r.end();
    return m;
}
SnapHash decode_snaphash(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    SnapHash m{r.u32(), r.u32(), 0};
    m.hash = r.u64();
    r.end();
    return m;
}
Ready decode_ready(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Ready m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.fingerprint = r.u64();
    m.rate = r.u32();
    m.carry = r.blob(kMaxFrame);
    r.end();
    return m;
}
JoinAt decode_join_at(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    JoinAt m{r.u32(), r.u32(), 0};
    m.tick = r.u32();
    r.end();
    return m;
}
NetFp decode_netfp(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    NetFp m{r.u32(), r.u32(), 0, 0};
    m.tick = r.u32();
    m.hash = r.u64();
    r.end();
    return m;
}
State decode_state(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    State m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.flags = r.u8();
    m.context = r.u64();
    m.reason = NotJoinable(r.u8());
    r.end();
    return m;
}
Intent decode_intent(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Intent m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.kind = IntentKind(r.u8());
    m.argument = r.i32();
    r.end();
    return m;
}
Presence decode_presence(std::span<const std::uint8_t> p, bool with_member) {
    auto r = reader(p);
    Presence m;
    if (with_member) m.member = r.u32();
    m.stage = r.str8();
    m.inst = r.u32();
    m.x = r.f32();
    m.y = r.f32();
    m.z = r.f32();
    m.yaw = r.f32();
    m.species = r.u8();
    m.level = r.u16();
    m.hp = r.u16();
    m.max_hp = r.u16();
    m.mp = r.u16();
    m.max_mp = r.u16();
    m.flags = r.u8();
    r.end();
    return m;
}
Resume decode_resume(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Resume m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.slot = r.u8();
    m.last_tick = r.u32();
    r.end();
    return m;
}
std::uint32_t decode_rate(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    const auto v = r.u32();
    r.end();
    return v;
}
Pong decode_pong(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Pong m;
    m.nonce = r.u32();
    m.client_time_ms = r.u64();
    r.end();
    return m;
}
Sealed decode_sealed(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Sealed m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.body = read_tick(r);
    r.end();
    return m;
}
Live decode_live(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Live m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.first = r.u32();
    const int n = r.u16();
    for (int i = 0; i < n; ++i) m.ticks.push_back(read_tick(r));
    r.end();
    return m;
}
JoinReq decode_join_req(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    JoinReq m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.slot = r.u8();
    r.end();
    return m;
}
BarrierOk decode_barrier_ok(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    BarrierOk m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.decision = Decision(r.u8());
    m.new_inst = r.u32();
    m.new_visit = r.u32();
    m.own_slot = r.u8();
    for (auto& s : m.slot_map) s = r.i8();
    for (auto& member : m.slot_members) member = r.u32();
    const int n = r.u8();
    for (int i = 0; i < n; ++i) {
        Carry c;
        c.slot = r.u8();
        c.bytes = r.blob(kMaxFrame);
        m.co_arrivals.push_back(std::move(c));
    }
    m.story_revision = r.u32();
    m.story = r.blob(kMaxFrame);
    m.dropped = r.blob(kMaxFrame);
    m.rule_flags = r.u8();
    m.present = r.u8();
    r.end();
    return m;
}
BarrierWait decode_barrier_wait(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    BarrierWait m;
    m.inst = r.u32();
    m.reason = WaitReason(r.u8());
    m.seconds_left = r.u16();
    const int n = r.u8();
    for (int i = 0; i < n; ++i) m.names.push_back(r.str8());
    r.end();
    return m;
}
StateData decode_state_body(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    StateData m;
    m.snapshot = r.blob(64u << 20);
    m.first = r.u32();
    const std::uint32_t n = r.u32();
    if (n > p.size()) throw WireError("tick count beyond the body");
    m.ticks.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) m.ticks.push_back(read_tick(r));
    const int forks = r.u8();
    for (int i = 0; i < forks; ++i) {
        Fork f;
        f.tick = r.u32();
        f.leavers = r.u8();
        f.kind = SplitKind(r.u8());
        f.side = r.u8();
        m.forks.push_back(f);
    }
    r.end();
    return m;
}
Room decode_room(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Room m;
    m.kind = RoomEvent(r.u8());
    m.member = r.u32();
    m.name = r.str8();
    m.value = r.u32();
    r.end();
    return m;
}
Ping decode_ping(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Ping m;
    m.nonce = r.u32();
    m.server_time_ms = r.u64();
    m.margin = r.i16();
    m.delay = r.u8();
    r.end();
    return m;
}
Reason decode_reason(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Reason m{r.str8()};
    r.end();
    return m;
}
InstVisit decode_inst_visit(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    InstVisit m{r.u32(), 0};
    m.visit = r.u32();
    r.end();
    return m;
}
Rekey decode_rekey(std::span<const std::uint8_t> p) {
    auto r = reader(p);
    Rekey m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.new_inst = r.u32();
    m.new_visit = r.u32();
    r.end();
    return m;
}

// ---- chunks
std::vector<Chunk> make_chunks(std::uint32_t inst, std::uint32_t visit, std::uint8_t slot, std::span<const std::uint8_t> body) {
    std::vector<Chunk> chunks;
    std::size_t offset = 0;
    do {
        const std::size_t n = std::min(kStateChunk, body.size() - offset);
        Chunk c;
        c.inst = inst;
        c.visit = visit;
        c.slot = slot;
        c.offset = std::uint32_t(offset);
        c.total = std::uint32_t(body.size());
        c.bytes.assign(body.begin() + std::ptrdiff_t(offset), body.begin() + std::ptrdiff_t(offset + n));
        chunks.push_back(std::move(c));
        offset += n;
    } while (offset < body.size());
    return chunks;
}

std::optional<std::vector<std::uint8_t>> ChunkAssembler::add(const Chunk& chunk, std::size_t limit) {
    if (chunk.total > limit) throw WireError("state transfer above its limit");
    if (chunk.offset == 0) {
        body_.clear();
        total_ = chunk.total;
        inst_ = chunk.inst;
        visit_ = chunk.visit;
        body_.reserve(total_);
    } else if (chunk.offset != body_.size() || chunk.total != total_ || chunk.inst != inst_ || chunk.visit != visit_) {
        throw WireError("state chunk out of order");
    }
    if (body_.size() + chunk.bytes.size() > total_) throw WireError("state chunk beyond its total");
    body_.insert(body_.end(), chunk.bytes.begin(), chunk.bytes.end());
    if (body_.size() < total_) return std::nullopt;
    auto done = std::move(body_);
    body_.clear();
    total_ = 0;
    return done;
}

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes, std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    for (const auto b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

} // namespace dw4::game::online::wire
