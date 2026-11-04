#include "wire.hpp"

#include <algorithm>
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
    case Type::welcome: return "WELCOME";
    case Type::sealed: return "SEALED";
    case Type::live: return "LIVE";
    case Type::join_req: return "JOIN_REQ";
    case Type::barrier_ok: return "BARRIER_OK";
    case Type::barrier_wait: return "BARRIER_WAIT";
    case Type::state_data: return "STATE_DATA";
    case Type::presence_of: return "PRESENCE";
    case Type::resync: return "RESYNC";
    case Type::story_ack: return "STORY_ACK";
    case Type::room: return "ROOM";
    case Type::ping: return "PING";
    case Type::close: return "CLOSE";
    case Type::roster: return "ROSTER";
    case Type::reassign: return "REASSIGN";
    case Type::netstat: return "NETSTAT";
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
std::string Reader::str8(std::size_t limit) {
    const std::size_t n = u8();
    if (n > limit) throw WireError("string too long");
    need(n);
    std::string s(reinterpret_cast<const char*>(in.data() + at), n);
    for (const char c : s)
        if (c < 0x20 || c > 0x7E) throw WireError("string is not printable ASCII");
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
std::vector<std::uint8_t> Reader::rest() {
    std::vector<std::uint8_t> v(in.begin() + std::ptrdiff_t(at), in.end());
    at = in.size();
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
    char hex[17];
    for (int i = 0; i < 16; ++i) hex[i] = "0123456789abcdef"[(settings_hash >> (60 - 4 * i)) & 0xF];
    hex[16] = 0;
    return "DW4R2 " + std::string(ticket) + " " + std::to_string(protocol) + " " + std::to_string(data_hash) + " " + hex + "\n";
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
template <class T> bool number(std::string_view text, T& value, int base = 10) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
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
    if (!number(w[2], hello.protocol) || !number(w[3], hello.data_hash) || !number(w[4], hello.settings_hash, 16)) return std::nullopt;
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

// ---- checks
namespace {
void check_mask(std::uint8_t mask) {
    if (mask & 0xF0) throw WireError("slot mask above slot 3");
}
void check_stage(const std::string& stage, bool allow_empty) {
    if (stage.empty() ? !allow_empty : stage.size() > kMaxStage) throw WireError("bad stage name");
    for (const char c : stage)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) throw WireError("bad stage name");
}
std::string stage_of(Reader& r, bool allow_empty) {
    auto s = r.str8(kMaxStage);
    check_stage(s, allow_empty);
    return s;
}
void put_stage(Writer& w, const std::string& stage, bool allow_empty) {
    check_stage(stage, allow_empty);
    w.str8(stage);
}
void check_pad(const WirePad& p) {
    auto ok = [](std::int8_t v) { return v >= -kStickSteps && v <= kStickSteps; };
    if (!ok(p.lx) || !ok(p.ly) || !ok(p.rx) || !ok(p.ry)) throw WireError("stick value outside the GameCube range");
}
std::vector<WirePad> read_pads(Reader& r, std::size_t count, std::size_t max) {
    if (count < 1 || count > max) throw WireError("pad count out of range");
    std::vector<WirePad> pads(count);
    for (auto& p : pads) {
        p = r.pad();
        check_pad(p);
    }
    return pads;
}
} // namespace

// ---- tick records
void write_tick(Writer& w, const TickBody& tick) {
    check_mask(tick.present);
    if (tick.events.size() > 255) throw WireError("too many events in a tick");
    w.u8(tick.present);
    w.u8(std::uint8_t(tick.events.size()));
    for (int slot = 0; slot < 4; ++slot)
        if (tick.present & (1u << slot)) w.pad(tick.pads[std::size_t(slot)]);
    for (const auto& e : tick.events) {
        Writer p;
        switch (e.kind) {
        case EventKind::join:
            p.u8(e.slot);
            p.u8(e.point);
            p.raw(e.carry);
            break;
        case EventKind::leave: p.u8(e.slot); break;
        case EventKind::intent:
            p.u8(e.slot);
            p.u8(e.intent_kind);
            p.i32(e.argument);
            break;
        case EventKind::lead_from: p.u32(e.tick); break;
        case EventKind::delay:
            p.u8(e.slot);
            p.u8(e.delay);
            break;
        default: throw WireError("unknown event kind");
        }
        if (p.out.size() > 0xFFFF) throw WireError("event too long");
        w.u8(std::uint8_t(e.kind));
        w.u16(std::uint16_t(p.out.size()));
        w.raw(p.out);
    }
}

TickBody read_tick(Reader& r) {
    TickBody tick;
    tick.present = r.u8();
    check_mask(tick.present);
    const int events = r.u8();
    for (int slot = 0; slot < 4; ++slot)
        if (tick.present & (1u << slot)) tick.pads[std::size_t(slot)] = r.pad();
    for (int i = 0; i < events; ++i) {
        const std::uint8_t type = r.u8();
        const std::size_t length = r.u16();
        r.need(length);
        Reader p{r.in.subspan(r.at, length)};
        r.at += length;
        Event e;
        e.kind = EventKind(type);
        switch (e.kind) {
        case EventKind::join:
            e.slot = p.u8();
            e.point = p.u8();
            e.carry = p.rest();
            break;
        case EventKind::leave: e.slot = p.u8(); break;
        case EventKind::intent:
            e.slot = p.u8();
            e.intent_kind = p.u8();
            e.argument = p.i32();
            break;
        case EventKind::lead_from: e.tick = p.u32(); break;
        case EventKind::delay:
            e.slot = p.u8();
            e.delay = p.u8();
            break;
        default: continue; // an event type of a newer server: skipped
        }
        p.end();
        if (e.slot > 3) throw WireError("event slot above 3");
        tick.events.push_back(std::move(e));
    }
    return tick;
}

std::uint8_t BarrierOk::present() const {
    if (decision == Decision::dissolve) return 1;
    std::uint8_t mask = 0;
    for (const auto to : slot_map)
        if (to >= 0 && to < 4) mask |= std::uint8_t(1u << to);
    for (const auto& c : co_arrivals)
        if (c.slot < 4) mask |= std::uint8_t(1u << c.slot);
    if (decision == Decision::join || decision == Decision::co_arrive) mask |= std::uint8_t(1u << (own_slot & 3));
    return mask;
}

// ---- client -> server
std::vector<std::uint8_t> encode(const Input& m) {
    if (m.pads.empty() || m.pads.size() > kMaxInputPads) throw WireError("pad count out of range");
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.first);
    w.u8(std::uint8_t(m.pads.size()));
    for (const auto& p : m.pads) w.pad(p);
    return std::move(w.out);
}
Input decode_input(std::span<const std::uint8_t> b) {
    Reader r{b};
    Input m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.first = r.u32();
    m.pads = read_pads(r, r.u8(), kMaxInputPads);
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Log& m) {
    if (m.pads.empty() || m.pads.size() > kMaxLogPads) throw WireError("pad count out of range");
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.first);
    w.u16(std::uint16_t(m.pads.size()));
    for (const auto& p : m.pads) w.pad(p);
    return std::move(w.out);
}
Log decode_log(std::span<const std::uint8_t> b) {
    Reader r{b};
    Log m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.first = r.u32();
    m.pads = read_pads(r, r.u16(), kMaxLogPads);
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Heading& m) {
    Writer w;
    w.u32(m.inst);
    put_stage(w, m.stage, false);
    w.u8(m.point);
    w.u64(m.context);
    return std::move(w.out);
}
Heading decode_heading(std::span<const std::uint8_t> b) {
    Reader r{b};
    Heading m;
    m.inst = r.u32();
    m.stage = stage_of(r, false);
    m.point = r.u8();
    m.context = r.u64();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Barrier& m) {
    if (m.carry.size() > kMaxCarry) throw WireError("carry above 65,533 bytes");
    check_mask(m.alive);
    check_mask(m.present);
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.tick);
    put_stage(w, m.stage, false);
    w.u8(m.point);
    w.u8(m.alive);
    w.u8(m.present);
    w.u64(m.context);
    w.blob(m.carry);
    return std::move(w.out);
}
Barrier decode_barrier(std::span<const std::uint8_t> b) {
    Reader r{b};
    Barrier m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.stage = stage_of(r, false);
    m.point = r.u8();
    m.alive = r.u8();
    m.present = r.u8();
    check_mask(m.alive);
    check_mask(m.present);
    m.context = r.u64();
    m.carry = r.blob(kMaxCarry);
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const BarrierChoice& m) {
    Writer w;
    w.u32(m.inst);
    w.u8(std::uint8_t(m.choice));
    return std::move(w.out);
}
BarrierChoice decode_barrier_choice(std::span<const std::uint8_t> b) {
    Reader r{b};
    BarrierChoice m;
    m.inst = r.u32();
    const auto c = r.u8();
    if (c > 1) throw WireError("unknown barrier choice");
    m.choice = Choice(c);
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Split& m) {
    check_mask(m.leavers);
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.tick);
    w.u8(m.leavers);
    w.u8(std::uint8_t(m.kind));
    return std::move(w.out);
}
Split decode_split(std::span<const std::uint8_t> b) {
    Reader r{b};
    Split m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.leavers = r.u8();
    check_mask(m.leavers);
    const auto kind = r.u8();
    if (kind > 1) throw WireError("unknown split kind");
    if (m.leavers == 0) throw WireError("empty leaver mask");
    m.kind = SplitKind(kind);
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Chunk& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.total);
    w.u32(m.offset);
    w.raw(m.bytes);
    return std::move(w.out);
}
Chunk decode_chunk(std::span<const std::uint8_t> b) {
    Reader r{b};
    Chunk m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.total = r.u32();
    m.offset = r.u32();
    m.bytes = r.rest();
    if (std::uint64_t(m.offset) + m.bytes.size() > m.total) throw WireError("chunk beyond its total");
    return m;
}

std::vector<std::uint8_t> encode(const SnapHash& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u64(m.hash);
    return std::move(w.out);
}
SnapHash decode_snaphash(std::span<const std::uint8_t> b) {
    Reader r{b};
    SnapHash m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.hash = r.u64();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Ready& m) {
    if (m.carry.size() > kMaxCarry) throw WireError("carry above 65,533 bytes");
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.tick);
    w.u64(m.fingerprint);
    w.u32(m.rate);
    w.blob(m.carry);
    return std::move(w.out);
}
Ready decode_ready(std::span<const std::uint8_t> b) {
    Reader r{b};
    Ready m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.fingerprint = r.u64();
    m.rate = r.u32();
    m.carry = r.blob(kMaxCarry);
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const JoinAt& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.tick);
    return std::move(w.out);
}
JoinAt decode_join_at(std::span<const std::uint8_t> b) {
    Reader r{b};
    JoinAt m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const NetFp& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.tick);
    w.u64(m.hash);
    return std::move(w.out);
}
NetFp decode_netfp(std::span<const std::uint8_t> b) {
    Reader r{b};
    NetFp m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.hash = r.u64();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const State& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.tick);
    w.u8(m.flags);
    w.u64(m.context);
    w.u8(std::uint8_t(m.reason));
    return std::move(w.out);
}
State decode_state(std::span<const std::uint8_t> b) {
    Reader r{b};
    State m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.tick = r.u32();
    m.flags = r.u8();
    if (m.flags > 7) throw WireError("unknown state flags");
    m.context = r.u64();
    m.reason = NotJoinable(r.u8());
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Story& m) {
    const auto& d = m.delta;
    if (d.flags.size() > 1024 || d.folder.size() > 12 || d.counters.size() > kMaxStoryLines) throw WireError("story set too large");
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.revision);
    w.u8(d.phase);
    w.u16(std::uint16_t(d.flags.size()));
    for (const auto f : d.flags) w.u16(std::uint16_t(f));
    w.u8(std::uint8_t(d.folder.size()));
    for (const auto k : d.folder) w.u8(std::uint8_t(k));
    w.u8(std::uint8_t(d.counters.size()));
    for (const auto& g : d.counters) {
        w.u8(std::uint8_t(g.counter));
        w.u32(g.inst);
        w.u32(g.visit);
        w.i32(g.growth);
    }
    return std::move(w.out);
}
Story decode_story(std::span<const std::uint8_t> b) {
    Reader r{b};
    Story m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.revision = r.u32();
    auto& d = m.delta;
    d.phase = r.u8();
    const auto nf = r.u16();
    if (nf > 1024) throw WireError("story: too many flags");
    for (unsigned i = 0; i < nf; ++i) {
        const auto f = r.u16();
        if (f >= 1024) throw WireError("story: flag out of range");
        d.flags.push_back(f);
    }
    const auto nk = r.u8();
    if (nk > 12) throw WireError("story: too many keys");
    for (unsigned i = 0; i < nk; ++i) {
        const auto k = r.u8();
        if (k >= 12) throw WireError("story: key out of range");
        d.folder.push_back(k);
    }
    const auto nc = r.u8();
    if (nc > kMaxStoryLines) throw WireError("story: too many counter lines");
    for (unsigned i = 0; i < nc; ++i) {
        engine::CounterGrowth g;
        g.counter = r.u8();
        if (g.counter >= 16) throw WireError("story: counter out of range");
        g.inst = r.u32();
        g.visit = r.u32();
        g.growth = r.i32();
        d.counters.push_back(g);
    }
    r.end();
    return m;
}
std::vector<std::uint8_t> encode(const StoryAck& m) {
    if (!m.story.empty() && m.story.size() != engine::story::kStoryBytes) throw WireError("story_ack: story blob size");
    if (m.refused.size() > 0xFFFF) throw WireError("story_ack: too many refused keys");
    Writer w;
    w.u32(m.revision);
    w.blob(m.story);
    w.u8(m.flags);
    w.u16(std::uint16_t(m.refused.size()));
    for (const auto& x : m.refused) {
        w.u8(std::uint8_t(x.kind));
        w.u16(std::uint16_t(x.index));
        w.u8(std::uint8_t(x.reason));
    }
    return std::move(w.out);
}
StoryAck decode_story_ack(std::span<const std::uint8_t> b) {
    Reader r{b};
    StoryAck m;
    m.revision = r.u32();
    m.story = r.blob(engine::story::kStoryBytes);
    if (!m.story.empty() && m.story.size() != engine::story::kStoryBytes) throw WireError("story_ack: story blob size");
    m.flags = r.u8();
    const auto n = r.u16();
    for (unsigned i = 0; i < n; ++i) {
        engine::StoryRefused x;
        const auto kind = r.u8();
        if (kind > 3) throw WireError("story_ack: unknown key kind");
        x.kind = engine::StoryRefused::Kind(kind);
        x.index = r.u16();
        x.reason = engine::StoryRefusal(r.u8());
        m.refused.push_back(x);
    }
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Intent& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u8(std::uint8_t(m.kind));
    w.i32(m.argument);
    return std::move(w.out);
}
Intent decode_intent(std::span<const std::uint8_t> b) {
    Reader r{b};
    Intent m;
    m.inst = r.u32();
    m.visit = r.u32();
    const auto kind = r.u8();
    if (kind != 1 && kind != 2) throw WireError("unknown intent");
    m.kind = IntentKind(kind);
    m.argument = r.i32();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Presence& m, bool with_member) {
    Writer w;
    if (with_member) w.u8(m.member);
    put_stage(w, m.stage, true);
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
    return std::move(w.out);
}
Presence decode_presence(std::span<const std::uint8_t> b, bool with_member) {
    Reader r{b};
    Presence m;
    if (with_member) m.member = r.u8();
    m.stage = stage_of(r, true);
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
    r.end();
    for (const float v : {m.x, m.y, m.z, m.yaw})
        if (!(v == v) || v > 3.0e38f || v < -3.0e38f) throw WireError("presence position is not finite");
    return m;
}

std::vector<std::uint8_t> encode(const Resume& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u8(m.slot);
    w.u32(m.last_tick);
    return std::move(w.out);
}
Resume decode_resume(std::span<const std::uint8_t> b) {
    Reader r{b};
    Resume m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.slot = r.u8();
    m.last_tick = r.u32();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(std::uint32_t rate) {
    Writer w;
    w.u32(rate);
    return std::move(w.out);
}
std::uint32_t decode_rate(std::span<const std::uint8_t> b) {
    Reader r{b};
    const auto v = r.u32();
    r.end();
    return v;
}

std::vector<std::uint8_t> encode(const Pong& m) {
    Writer w;
    w.u32(m.nonce);
    w.u64(m.client_time_ms);
    return std::move(w.out);
}
Pong decode_pong(std::span<const std::uint8_t> b) {
    Reader r{b};
    Pong m;
    m.nonce = r.u32();
    m.client_time_ms = r.u64();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Bye& m) {
    Writer w;
    w.u8(m.reason);
    return std::move(w.out);
}
Bye decode_bye(std::span<const std::uint8_t> b) {
    Reader r{b};
    Bye m;
    m.reason = r.u8();
    r.end();
    return m;
}

// ---- server -> client
std::vector<std::uint8_t> encode(const Welcome& m) {
    Writer w;
    w.u8(m.member);
    w.u32(m.inst);
    w.u32(m.visit);
    w.u8(m.slot);
    w.u8(std::uint8_t(m.mode));
    put_stage(w, m.stage, true);
    w.u32(m.last_tick);
    w.u8(m.delay);
    w.u16(m.max_catch_up);
    w.u8(m.flags);
    return std::move(w.out);
}
Welcome decode_welcome(std::span<const std::uint8_t> b) {
    Reader r{b};
    Welcome m;
    m.member = r.u8();
    m.inst = r.u32();
    m.visit = r.u32();
    m.slot = r.u8();
    m.mode = Mode(r.u8());
    m.stage = stage_of(r, true);
    m.last_tick = r.u32();
    m.delay = r.u8();
    m.max_catch_up = r.u16();
    m.flags = r.u8();
    r.end();
    if (m.slot > 3 || std::uint8_t(m.mode) > 2) throw WireError("bad welcome");
    return m;
}

std::vector<std::uint8_t> encode(const Ticks& m) {
    if (m.ticks.size() > 0xFFFF) throw WireError("too many ticks in one frame");
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.first);
    w.u16(std::uint16_t(m.ticks.size()));
    for (const auto& t : m.ticks) write_tick(w, t);
    return std::move(w.out);
}
Ticks decode_ticks(std::span<const std::uint8_t> b) {
    Reader r{b};
    Ticks m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.first = r.u32();
    const std::size_t n = r.u16();
    m.ticks.reserve(n);
    for (std::size_t i = 0; i < n; ++i) m.ticks.push_back(read_tick(r));
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const JoinReq& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u8(m.slot);
    w.u8(m.delay);
    return std::move(w.out);
}
JoinReq decode_join_req(std::span<const std::uint8_t> b) {
    Reader r{b};
    JoinReq m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.slot = r.u8();
    m.delay = r.u8();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const BarrierOk& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u8(std::uint8_t(m.decision));
    w.u32(m.new_inst);
    w.u32(m.new_visit);
    put_stage(w, m.stage, true);
    w.u8(m.point);
    w.u8(m.own_slot);
    for (const auto s : m.slot_map) w.i8(s);
    w.u8(std::uint8_t(m.mode));
    w.u8(m.delay);
    if (m.co_arrivals.size() > 4) throw WireError("more than four co-arrivals");
    w.u8(std::uint8_t(m.co_arrivals.size()));
    for (const auto& c : m.co_arrivals) {
        w.u8(c.slot);
        w.u8(c.member);
        w.blob(c.carry);
    }
    w.u32(m.story_revision);
    w.blob(m.story);
    w.blob(m.dropped);
    w.u8(std::uint8_t(m.notice));
    if (m.names.size() > 255) throw WireError("too many names");
    w.u8(std::uint8_t(m.names.size()));
    for (const auto& n : m.names) w.str8(n);
    return std::move(w.out);
}
BarrierOk decode_barrier_ok(std::span<const std::uint8_t> b) {
    Reader r{b};
    BarrierOk m;
    m.inst = r.u32();
    m.visit = r.u32();
    const auto decision = r.u8();
    if (decision < 1 || decision > 4) throw WireError("unknown barrier decision");
    m.decision = Decision(decision);
    m.new_inst = r.u32();
    m.new_visit = r.u32();
    m.stage = stage_of(r, true);
    m.point = r.u8();
    m.own_slot = r.u8();
    if (m.own_slot > 3) throw WireError("own slot above 3");
    for (auto& s : m.slot_map) {
        s = r.i8();
        if (s < -1 || s > 3) throw WireError("slot map out of range");
    }
    m.mode = Mode(r.u8());
    if (std::uint8_t(m.mode) > 2) throw WireError("unknown instance mode");
    m.delay = r.u8();
    const int n = r.u8();
    if (n > 4) throw WireError("more than four co-arrivals");
    for (int i = 0; i < n; ++i) {
        CoArrival c;
        c.slot = r.u8();
        if (c.slot > 3) throw WireError("co-arrival slot above 3");
        c.member = r.u8();
        c.carry = r.blob(kMaxCarry);
        m.co_arrivals.push_back(std::move(c));
    }
    m.story_revision = r.u32();
    m.story = r.blob(1u << 16);
    m.dropped = r.blob(1u << 16);
    m.notice = Notice(r.u8());
    const int names = r.u8();
    for (int i = 0; i < names; ++i) m.names.push_back(r.str8(64));
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const BarrierWait& m) {
    Writer w;
    w.u32(m.inst);
    w.u8(std::uint8_t(m.reason));
    w.u16(m.seconds_left);
    if (m.names.size() > 255) throw WireError("too many names");
    w.u8(std::uint8_t(m.names.size()));
    for (const auto& n : m.names) w.str8(n);
    w.u8(m.flags);
    return std::move(w.out);
}
BarrierWait decode_barrier_wait(std::span<const std::uint8_t> b) {
    Reader r{b};
    BarrierWait m;
    m.inst = r.u32();
    m.reason = WaitReason(r.u8());
    m.seconds_left = r.u16();
    const int n = r.u8();
    for (int i = 0; i < n; ++i) m.names.push_back(r.str8(64));
    m.flags = r.u8();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode_state_body(const StateData& m) {
    Writer w;
    w.u8(std::uint8_t(m.purpose));
    w.u8(m.slot);
    w.u8(0); // codec: raw tick records
    w.blob(m.snapshot);
    if (m.forks.size() > 255) throw WireError("too many forks");
    w.u8(std::uint8_t(m.forks.size()));
    for (const auto& f : m.forks) {
        w.u32(f.tick);
        w.u8(f.leavers);
        w.u8(std::uint8_t(f.kind));
        w.u8(f.side);
    }
    w.u32(m.first);
    w.u32(std::uint32_t(m.ticks.size()));
    for (const auto& t : m.ticks) write_tick(w, t);
    return std::move(w.out);
}
StateData decode_state_body(std::span<const std::uint8_t> b) {
    Reader r{b};
    StateData m;
    const auto purpose = r.u8();
    if (purpose < 1 || purpose > 4) throw WireError("unknown state purpose");
    m.purpose = StatePurpose(purpose);
    m.slot = r.u8();
    if (m.slot > 3) throw WireError("state slot above 3");
    if (r.u8() != 0) throw WireError("unknown state codec");
    m.snapshot = r.blob(4u << 20);
    const int forks = r.u8();
    for (int i = 0; i < forks; ++i) {
        Fork f;
        f.tick = r.u32();
        f.leavers = r.u8();
        check_mask(f.leavers);
        const auto kind = r.u8();
        if (kind > 1) throw WireError("unknown split kind");
        f.kind = SplitKind(kind);
        f.side = r.u8();
        m.forks.push_back(f);
    }
    m.first = r.u32();
    const std::uint32_t count = r.u32();
    m.ticks.reserve(std::min<std::uint32_t>(count, 1u << 20));
    for (std::uint32_t i = 0; i < count; ++i) m.ticks.push_back(read_tick(r));
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Room& m) {
    Writer w;
    w.u8(std::uint8_t(m.kind));
    w.u8(m.member);
    w.u64(m.account);
    w.str8(m.name);
    w.u8(m.flags);
    return std::move(w.out);
}
Room decode_room(std::span<const std::uint8_t> b) {
    Reader r{b};
    Room m;
    m.kind = RoomEvent(r.u8());
    m.member = r.u8();
    m.account = r.u64();
    m.name = r.str8(64);
    m.flags = r.u8();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const NetStat& m) {
    Writer w;
    w.u16(m.rtt_ms);
    w.u16(m.spread_ms);
    w.u8(m.delay);
    w.i16(m.margin_ms);
    w.u32(m.sealed);
    w.u32(m.filled);
    w.u32(m.late);
    w.u8(m.flags);
    return std::move(w.out);
}
NetStat decode_netstat(std::span<const std::uint8_t> b) {
    Reader r{b};
    NetStat m;
    m.rtt_ms = r.u16();
    m.spread_ms = r.u16();
    m.delay = r.u8();
    m.margin_ms = r.i16();
    m.sealed = r.u32();
    m.filled = r.u32();
    m.late = r.u32();
    m.flags = r.u8();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Ping& m) {
    Writer w;
    w.u32(m.nonce);
    w.u64(m.server_time_ms);
    w.i16(m.margin_ms);
    w.u8(m.delay);
    return std::move(w.out);
}
Ping decode_ping(std::span<const std::uint8_t> b) {
    Reader r{b};
    Ping m;
    m.nonce = r.u32();
    m.server_time_ms = r.u64();
    m.margin_ms = r.i16();
    m.delay = r.u8();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Close& m) {
    Writer w;
    w.u8(std::uint8_t(m.reason));
    w.str8(m.text.size() > 200 ? m.text.substr(0, 200) : m.text);
    return std::move(w.out);
}
Close decode_close(std::span<const std::uint8_t> b) {
    Reader r{b};
    Close m;
    m.reason = CloseReason(r.u8());
    m.text = r.str8();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Roster& m) {
    if (m.entries.size() > 255) throw WireError("too many roster entries");
    Writer w;
    w.u8(std::uint8_t(m.entries.size()));
    for (const auto& e : m.entries) {
        w.u8(e.member);
        put_stage(w, e.stage, true);
        w.u32(e.inst);
        w.u16(e.level);
        w.u16(e.hp);
        w.u16(e.max_hp);
        w.u8(e.flags);
    }
    return std::move(w.out);
}
Roster decode_roster(std::span<const std::uint8_t> b) {
    Reader r{b};
    Roster m;
    const int n = r.u8();
    for (int i = 0; i < n; ++i) {
        RosterEntry e;
        e.member = r.u8();
        e.stage = stage_of(r, true);
        e.inst = r.u32();
        e.level = r.u16();
        e.hp = r.u16();
        e.max_hp = r.u16();
        e.flags = r.u8();
        m.entries.push_back(std::move(e));
    }
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const InstVisit& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    return std::move(w.out);
}
InstVisit decode_inst_visit(std::span<const std::uint8_t> b) {
    Reader r{b};
    InstVisit m;
    m.inst = r.u32();
    m.visit = r.u32();
    r.end();
    return m;
}

std::vector<std::uint8_t> encode(const Reassign& m) {
    Writer w;
    w.u32(m.inst);
    w.u32(m.visit);
    w.u32(m.new_inst);
    w.u32(m.from_tick);
    w.u8(std::uint8_t(m.mode));
    w.u8(m.flags);
    return std::move(w.out);
}
Reassign decode_reassign(std::span<const std::uint8_t> b) {
    Reader r{b};
    Reassign m;
    m.inst = r.u32();
    m.visit = r.u32();
    m.new_inst = r.u32();
    m.from_tick = r.u32();
    m.mode = Mode(r.u8());
    m.flags = r.u8();
    r.end();
    return m;
}

// ---- chunks
std::vector<Chunk> make_chunks(std::uint32_t inst, std::uint32_t visit, std::span<const std::uint8_t> body) {
    std::vector<Chunk> chunks;
    std::size_t offset = 0;
    do {
        const std::size_t n = std::min(kStateChunk, body.size() - offset);
        Chunk c;
        c.inst = inst;
        c.visit = visit;
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
