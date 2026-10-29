// DW4R2 on the wire: the 7-byte quantized pads, every message's layout
// round trip, the handshake lines, framing with partial reads and the 1 MiB
// limit, chunked state transfers; the +-0.1 % pacing of sealed instances; the
// local leave combo.
#include "online/leave_combo.hpp"
#include "online/pacing.hpp"
#include "online/pad_wire.hpp"
#include "online/wire.hpp"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game::online;
namespace w = dw4::game::online::wire;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template <class F> bool throws(F&& f) {
    try {
        f();
    } catch (const w::WireError&) {
        return true;
    }
    return false;
}

void pads() {
    // Every button mask of the GameCube pad fits in 16 bits.
    for (const auto mask : {dw4::pad_button::start, dw4::pad_button::up, dw4::pad_button::right, dw4::pad_button::down,
                            dw4::pad_button::left, dw4::pad_button::z, dw4::pad_button::l, dw4::pad_button::r, dw4::pad_button::y,
                            dw4::pad_button::x, dw4::pad_button::a, dw4::pad_button::b})
        check(mask < 0x10000u, "button masks fit in 16 bits");
    check(quantize_axis(0.0f) == 0 && quantize_axis(1.0f) == 74 && quantize_axis(-1.0f) == -74, "axis ends");
    check(quantize_axis(72.0f / 74.0f) == 72, "the full main stick of host_pad_sample is step 72");
    check(quantize_axis(59.0f / 74.0f) == 59, "the full C-stick is step 59");
    check(quantize_axis(2.0f) == 74 && quantize_axis(-7.0f) == -74 && quantize_axis(NAN) == 0, "clamped, NaN neutral");
    check(quantize_axis(0.5f / 74.0f) == 1 && quantize_axis(0.49f / 74.0f) == 0, "round to the nearest step");
    // Every step k/74 survives the round trip exactly: the values a GameCube stick can give.
    for (int k = -74; k <= 74; ++k) check(quantize_axis(float(k) / 74.0f) == k, "k/74 is step k");
    for (int k = -60; k <= 60; ++k) {
        const auto stick = dw4::gamecube_stick(std::int8_t(k), std::int8_t(-k));
        const auto sample = online_pad({true, stick, {}, 0});
        check(sample.left.x == stick.x && sample.left.y == stick.y, "original stick values pass unchanged");
    }
    dw4::PadSample sample{true, {0.3f, -0.71f}, {-0.2f, 0.05f}, dw4::pad_button::a | dw4::pad_button::start | 0x10000u};
    const auto wire_pad = quantize_pad(sample);
    check(wire_pad.buttons == (dw4::pad_button::a | dw4::pad_button::start), "bits above 16 are dropped");
    std::uint8_t bytes[kWirePadBytes];
    write_pad(bytes, wire_pad);
    check(read_pad(bytes) == wire_pad, "7-byte round trip");
    check(bytes[0] == 0x08 && bytes[1] == 0x40 && bytes[2] == std::uint8_t(std::int8_t(22)) && bytes[6] == 0, "byte layout");
    const auto executed = dequantize_pad(wire_pad, true);
    check(executed.left.x == 22.0f / 74.0f && executed.left.y == -53.0f / 74.0f, "executed as k / 74");
    check(online_pad(executed).left.x == executed.left.x, "quantizing is idempotent");
    const auto absent = dequantize_pad(wire_pad, false);
    check(!absent.connected && absent.buttons == 0 && absent.left.x == 0, "an absent slot plays the neutral pad");
}

w::TickBody tick_with_events() {
    w::TickBody t;
    t.present = 0b1011;
    t.pads[0] = {0x4000, 10, -20, 0, 0, 0};
    t.pads[1] = {0x0008, -74, 74, 3, -3, 1};
    t.pads[3] = {0, 0, 0, 59, 0, 0};
    w::Event join{w::EventKind::join, 2, 5, {1, 2, 3, 4}};
    w::Event leave{w::EventKind::leave, 1};
    w::Event intent{w::EventKind::intent, 3};
    intent.intent_kind = 2;
    intent.argument = -77;
    w::Event lead{w::EventKind::lead_from};
    lead.tick = 123456;
    w::Event delay{w::EventKind::delay, 0};
    delay.delay = 4;
    t.events = {join, leave, intent, lead, delay};
    return t;
}

void messages() {
    // Ticks.
    const auto t = tick_with_events();
    w::Writer tw;
    w::write_tick(tw, t);
    check(tw.out.size() == 1 + 3 * 7 + 1 + (1 + 1 + 2 + 4 + 4) + 2 + 7 + 5 + 3, "tick body size");
    w::Reader tr{tw.out};
    check(w::read_tick(tr) == t && tr.done(), "tick round trip");
    // Client -> server.
    w::Input in{7, 2, 99, {0x8000, 1, 2, 3, 4, 0}};
    auto in2 = w::decode_input(w::encode(in));
    check(in2.inst == 7 && in2.visit == 2 && in2.tick == 99 && in2.pad == in.pad, "INPUT");
    check(w::encode(in).size() == 4 + 4 + 4 + 7, "INPUT is 19 bytes");
    w::Log log{7, 2, 100, {{1, 0, 0, 0, 0, 0}, {2, 0, 0, 0, 0, 0}}};
    auto log2 = w::decode_log(w::encode(log));
    check(log2.first == 100 && log2.pads.size() == 2 && log2.pads[1].buttons == 2, "LOG");
    w::Heading heading{7, "stag0102", 3};
    auto heading2 = w::decode_heading(w::encode(heading));
    check(heading2.stage == "stag0102" && heading2.point == 3, "HEADING");
    w::Barrier barrier{7, 2, 500, "dng101b1", 1, 0b11, 0b11, 0x1122334455667788ull, {{0, {9, 9}}, {1, {}}}};
    auto barrier2 = w::decode_barrier(w::encode(barrier));
    check(barrier2.tick == 500 && barrier2.stage == "dng101b1" && barrier2.context == barrier.context && barrier2.carries == barrier.carries,
          "BARRIER");
    auto choice = w::decode_barrier_choice(w::encode(w::BarrierChoice{7, w::Choice::alone}));
    check(choice.choice == w::Choice::alone, "BARRIER_CHOICE");
    auto split = w::decode_split(w::encode(w::Split{7, 2, 800, 0b10, w::SplitKind::in_stage}));
    check(split.tick == 800 && split.leavers == 2 && split.kind == w::SplitKind::in_stage, "SPLIT");
    auto hash = w::decode_snaphash(w::encode(w::SnapHash{7, 3, 0xdeadbeefcafef00dull}));
    check(hash.hash == 0xdeadbeefcafef00dull && hash.visit == 3, "SNAPHASH");
    auto ready = w::decode_ready(w::encode(w::Ready{7, 3, 120, 42, 4100, {5, 6}}));
    check(ready.tick == 120 && ready.fingerprint == 42 && ready.rate == 4100 && ready.carry.size() == 2, "READY");
    check(w::decode_join_at(w::encode(w::JoinAt{7, 3, 333})).tick == 333, "JOIN_AT");
    check(w::decode_netfp(w::encode(w::NetFp{7, 3, 60, 77})).hash == 77, "NETFP");
    auto state = w::decode_state(w::encode(w::State{7, 3, w::state_flag::joinable | w::state_flag::boss_lock, 5, w::NotJoinable::boss}));
    check(state.flags == 5 && state.reason == w::NotJoinable::boss, "STATE");
    auto intent = w::decode_intent(w::encode(w::Intent{7, 3, w::IntentKind::give_up, 0}));
    check(intent.kind == w::IntentKind::give_up, "INTENT");
    w::Presence presence{0, "stagml11", 9, 1.5f, -2.0f, 300.25f, 0.5f, 3, 12, 80, 120, 10, 30, w::presence_flag::defeated};
    check(w::decode_presence(w::encode(presence, false), false) == presence, "PRESENCE (client)");
    presence.member = 4;
    check(w::decode_presence(w::encode(presence, true), true) == presence, "PRESENCE (server)");
    auto resume = w::decode_resume(w::encode(w::Resume{7, 3, 1, 999}));
    check(resume.slot == 1 && resume.last_tick == 999, "RESUME");
    check(w::decode_rate(w::encode(std::uint32_t(4321))) == 4321, "RATE");
    check(w::decode_pong(w::encode(w::Pong{5, 123})).client_time_ms == 123, "PONG");
    // Server -> client.
    auto sealed = w::decode_sealed(w::encode(w::Sealed{7, 3, 61, t}));
    check(sealed.tick == 61 && sealed.body == t, "SEALED");
    auto live = w::decode_live(w::encode(w::Live{7, 3, 10, {t, w::TickBody{}}}));
    check(live.first == 10 && live.ticks.size() == 2 && live.ticks[0] == t, "LIVE");
    check(w::decode_join_req(w::encode(w::JoinReq{7, 3, 2})).slot == 2, "JOIN_REQ");
    w::BarrierOk ok;
    ok.inst = 7;
    ok.visit = 3;
    ok.decision = w::Decision::co_arrive;
    ok.new_inst = 8;
    ok.new_visit = 1;
    ok.own_slot = 2;
    ok.slot_map = {0, -1, 2, 1};
    ok.co_arrivals = {{2, {1}}, {3, {2, 2}}};
    ok.story_revision = 5;
    ok.story = {1, 2, 3};
    ok.rule_flags = 7;
    ok.present = 0b0111;
    auto ok2 = w::decode_barrier_ok(w::encode(ok));
    check(ok2.decision == w::Decision::co_arrive && ok2.new_inst == 8 && ok2.slot_map == ok.slot_map && ok2.co_arrivals == ok.co_arrivals &&
              ok2.story == ok.story && ok2.dropped.empty() && ok2.present == 7,
          "BARRIER_OK");
    auto wait = w::decode_barrier_wait(w::encode(w::BarrierWait{7, w::WaitReason::entry_scene, 12, {"kim", "lee"}}));
    check(wait.reason == w::WaitReason::entry_scene && wait.seconds_left == 12 && wait.names.size() == 2, "BARRIER_WAIT");
    w::StateData body;
    body.snapshot.assign(150000, 0x5A);
    body.first = 0;
    body.ticks.assign(3000, t);
    body.forks = {{800, 2, w::SplitKind::exit, 0}};
    const auto bytes = w::encode_state_body(body);
    auto chunks = w::make_chunks(7, 3, 1, bytes);
    check(chunks.size() == (bytes.size() + w::kStateChunk - 1) / w::kStateChunk, "chunk count");
    w::ChunkAssembler assembler;
    std::optional<std::vector<std::uint8_t>> whole;
    for (const auto& c : chunks) {
        auto c2 = w::decode_chunk(w::encode(c, true), true);
        check(c2.slot == 1, "STATE_DATA chunk slot");
        whole = assembler.add(c2, 64u << 20);
    }
    check(whole && *whole == bytes, "chunks reassemble");
    auto body2 = w::decode_state_body(*whole);
    check(body2.snapshot == body.snapshot && body2.ticks.size() == 3000 && body2.ticks[2999] == t && body2.forks == body.forks, "STATE_DATA body");
    check(throws([&] { w::ChunkAssembler a; a.add(chunks[1], 64u << 20); }), "a chunk out of order is refused");
    check(throws([&] { w::ChunkAssembler a; a.add(chunks[0], 1000); }), "a transfer above its limit is refused");
    auto empty_chunks = w::make_chunks(1, 1, 0, {});
    check(empty_chunks.size() == 1 && w::ChunkAssembler{}.add(empty_chunks[0], 10).value().empty(), "an empty body is one chunk");
    auto room = w::decode_room(w::encode(w::Room{w::RoomEvent::joined, 3, "kim", 0}));
    check(room.kind == w::RoomEvent::joined && room.name == "kim", "ROOM");
    auto ping = w::decode_ping(w::encode(w::Ping{9, 1000, -2, 3}));
    check(ping.margin == -2 && ping.delay == 3, "PING");
    check(w::decode_reason(w::encode(w::Reason{"kicked"})).reason == "kicked", "KICK / CLOSE");
    check(w::decode_inst_visit(w::encode(w::InstVisit{4, 5})).visit == 5, "RESYNC");
    // Malformed payloads.
    auto short_input = w::encode(in);
    short_input.pop_back();
    check(throws([&] { w::decode_input(short_input); }), "a short INPUT is refused");
    auto long_input = w::encode(in);
    long_input.push_back(0);
    check(throws([&] { w::decode_input(long_input); }), "trailing bytes are refused");
    std::vector<std::uint8_t> bad_tick{0x10};
    check(throws([&] { w::Reader r{bad_tick}; w::read_tick(r); }), "a present bit above slot 3 is refused");
    std::vector<std::uint8_t> bad_event{0x00, 0x01, 0x09};
    check(throws([&] { w::Reader r{bad_event}; w::read_tick(r); }), "an unknown event kind is refused");
    std::vector<std::uint8_t> bad_slot{0x00, 0x01, 0x02, 0x07};
    check(throws([&] { w::Reader r{bad_slot}; w::read_tick(r); }), "an event slot above 3 is refused");
    std::vector<std::uint8_t> huge_count{0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f};
    check(throws([&] { w::decode_state_body(huge_count); }), "a tick count beyond the body is refused");
}

void handshake_and_frames() {
    check(w::hello_line("abc.def", 44, 18446744073709551615ull, 12) == "DW4R2 abc.def 44 18446744073709551615 12\n", "hello line");
    const auto hello = w::parse_hello("DW4R2 abc.def 44 18446744073709551615 12\r");
    check(hello && hello->ticket == "abc.def" && hello->protocol == 44 && hello->data_hash == 18446744073709551615ull, "hello parse");
    check(!w::parse_hello("DW4R1 HOST x") && !w::parse_hello("DW4R2 t 1 2") && !w::parse_hello("DW4R2 t x 2 3"), "bad hello lines");
    const auto ok = w::parse_hello_answer("OK 17 1730000000123");
    check(ok && ok->ok && ok->member_id == 17 && ok->server_time_ms == 1730000000123ull, "OK line");
    const auto err = w::parse_hello_answer("ERR bad_ticket");
    check(err && !err->ok && err->error == "bad_ticket", "ERR line");
    check(!w::parse_hello_answer("OK x 1") && !w::parse_hello_answer("HELLO"), "bad answers");
    // Frames arrive in pieces.
    std::vector<std::uint8_t> stream;
    for (std::uint32_t i = 0; i < 50; ++i) {
        const auto f = w::encode_frame(w::Type::netfp, w::encode(w::NetFp{1, 1, i, i * 3ull}));
        stream.insert(stream.end(), f.begin(), f.end());
    }
    w::FrameReader reader;
    std::mt19937 random(5);
    std::size_t at = 0, seen = 0;
    while (at < stream.size()) {
        const std::size_t n = std::min<std::size_t>(stream.size() - at, 1 + random() % 23);
        reader.feed({stream.data() + at, n});
        at += n;
        while (auto frame = reader.next()) {
            check(frame->type == w::Type::netfp && w::decode_netfp(frame->payload).tick == seen, "frames in order");
            ++seen;
        }
    }
    check(seen == 50 && reader.buffered() == 0, "every frame cut");
    std::vector<std::uint8_t> big{std::uint8_t(w::Type::snapshot), 0x01, 0x00, 0x10, 0x00};
    w::FrameReader big_reader;
    big_reader.feed(big);
    check(throws([&] { big_reader.next(); }), "a frame above 1 MiB is refused");
    check(throws([&] { w::encode_frame(w::Type::snapshot, std::vector<std::uint8_t>(w::kMaxFrame + 1)); }), "and never sent");
}

void pacing() {
    // A server whose clock runs fast or slow by up to 0.05 %, with jitter on
    // the way: the client follows within +-0.1 %, the buffer stays small and
    // nothing starves for 10 minutes.
    for (const double drift : {-0.0005, 0.0, 0.0003, 0.0005}) {
        PaceController pace;
        std::mt19937 random(11);
        std::uniform_real_distribution<double> jitter(0.0, 0.008);
        const double server_field = kFieldSeconds / (1.0 + drift);
        double next_seal = 0.05, now = 0;
        std::vector<double> arrivals;
        std::size_t sealed = 0, executed = 0, max_waiting = 0, catchups = 0;
        double min_ratio = 2, max_ratio = 0;
        const double pass = 1.0 / 144.0;
        while (now < 600.0) {
            now += pass;
            while (next_seal <= now) {
                arrivals.push_back(next_seal + 0.02 + jitter(random));
                next_seal += server_field;
            }
            while (sealed < arrivals.size() && arrivals[sealed] <= now) ++sealed;
            const std::size_t waiting = sealed - executed;
            max_waiting = std::max(max_waiting, waiting);
            const int due = pace.due_steps(pass, waiting);
            check(due >= 0 && std::size_t(due) <= waiting, "never more steps than sealed ticks");
            executed += std::size_t(due);
            if (pace.catching_up()) ++catchups;
            if (now > 60) {
                min_ratio = std::min(min_ratio, pace.ratio());
                max_ratio = std::max(max_ratio, pace.ratio());
            }
        }
        check(min_ratio >= 1.0 - kMaxPaceDeviation - 1e-12 && max_ratio <= 1.0 + kMaxPaceDeviation + 1e-12, "the pace stays within 0.1 %");
        check(catchups == 0, "no backlog runs without a stall");
        check(max_waiting <= 4, "the buffer stays small (" + std::to_string(max_waiting) + ")");
        check(pace.starved() < 20, "almost never starved (" + std::to_string(pace.starved()) + ")");
        check(std::abs((pace.ratio() - 1.0) - drift) < 0.0004, "the ratio follows the drift");
    }
    // A stall: 0.3 s without arrivals, then the backlog runs at once.
    PaceController pace;
    check(pace.due_steps(0.01, 1) == 0, "waits for the playout buffer");
    check(pace.due_steps(0.01, 2) == 1, "starts with one tick in reserve");
    const int burst = pace.due_steps(0.3, 19);
    check(pace.catching_up() && burst == 18, "a backlog above three ticks runs at once, keeping one");
}

void leave_combo() {
    LeaveCombo combo;
    const auto all = dw4::pad_button::b | dw4::pad_button::x | dw4::pad_button::start;
    int fired = 0;
    for (int i = 0; i < 179; ++i) fired += combo.update(all | dw4::pad_button::a);
    check(fired == 0, "not before 180 fields");
    fired += combo.update(all);
    check(fired == 1, "fires in field 180");
    for (int i = 0; i < 400; ++i) fired += combo.update(all);
    check(fired == 1, "once per hold");
    combo.update(dw4::pad_button::b | dw4::pad_button::x);
    for (int i = 0; i < 180; ++i) fired += combo.update(all);
    check(fired == 2, "again after a release");
}
} // namespace

int main() {
    try {
        pads();
        messages();
        handshake_and_frames();
        pacing();
        leave_combo();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
