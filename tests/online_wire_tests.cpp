// DW4R2 on the wire: the 6-byte quantized pads, every message's layout
// round trip, golden bytes shared with the relay's C# tests (one layout:
// server/DW4Online/tests/DW4Online.Tests/WireGoldenTests.cs), the handshake
// lines, framing with partial reads and the 1 MiB limit, chunked state
// transfers; the +-0.1 % pacing of sealed instances; the local leave combo;
// the input latency statistics, the link quality and the rotated online log
// of round 62.
#include "online/leave_combo.hpp"
#include "online/net_log.hpp"
#include "online/net_stats.hpp"
#include "online/pacing.hpp"
#include "online/pad_wire.hpp"
#include "online/wire.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    check(read_pad(bytes) == wire_pad, "6-byte round trip");
    check(kWirePadBytes == 6 && bytes[0] == 0x08 && bytes[1] == 0x40 && bytes[2] == std::uint8_t(std::int8_t(22)), "byte layout");
    const auto executed = dequantize_pad(wire_pad, true);
    check(executed.left.x == 22.0f / 74.0f && executed.left.y == -53.0f / 74.0f, "executed as k / 74");
    check(online_pad(executed).left.x == executed.left.x, "quantizing is idempotent");
    const auto absent = dequantize_pad(wire_pad, false);
    check(!absent.connected && absent.buttons == 0 && absent.left.x == 0, "an absent slot plays the neutral pad");
}

w::TickBody tick_with_events() {
    w::TickBody t;
    t.present = 0b1011;
    t.pads[0] = {0x4000, 10, -20, 0, 0};
    t.pads[1] = {0x0008, -74, 74, 3, -3};
    t.pads[3] = {0, 0, 0, 59, 0};
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

std::string hex(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    for (const auto b : bytes) {
        out += "0123456789abcdef"[b >> 4];
        out += "0123456789abcdef"[b & 15];
    }
    return out;
}

// Round I1a (protocol 49, docs/SEAMLESS.md 19.18; the layouts and golden
// bytes of DW4_PC-wt/i1a-contract.md, held by the relay's
// WireGoldenTests.World_messages_equal_the_clients too).
void world_messages() {
    w::Settings settings{1, 1, 0, 1, 4242, 1, 1, 2};
    {
        w::Writer wr;
        w::write_settings(wr, settings);
        check(hex(wr.out) == "0101000192100000010102", "golden settings");
        w::Writer d;
        w::write_settings(d, w::Settings{});
        check(hex(d.out) == "0000010000000000000000", "golden default settings");
        w::Reader r{wr.out};
        check(w::read_settings(r) == settings, "settings round trip");
        auto bad = wr.out;
        bad[0] = 3;
        check(throws([&] { w::Reader rb{bad}; (void)w::read_settings(rb); }), "a difficulty above 2 is refused");
    }
    {
        w::Story s;
        s.inst = 7;
        s.visit = 2;
        s.revision = 12;
        s.delta.phase = 3;
        s.delta.flags = {19, 392};
        s.delta.folder = {1};
        s.delta.counters = {{7, 4, 2, 1}};
        s.owner = 5;
        s.box = 1;
        check(hex(w::encode(s)) == "07000000" "02000000" "0c000000" "03" "0200" "1300" "8801" "01" "01" "01" "07" "04000000" "02000000" "01000000"
                                   "05000000" "01",
              "golden STORY");
        const auto back = w::decode_story(w::encode(s));
        check(back.owner == 5 && back.box == 1 && back.delta == s.delta, "STORY round trip with the owner key");
    }
    {
        w::StoryAck a{13, {}, w::StoryAck::answer, {}};
        a.refused = {{dw4::engine::StoryRefused::Kind::flag, 23, dw4::engine::StoryRefusal::not_story}, {dw4::engine::StoryRefused::Kind::phase, 5, dw4::engine::StoryRefusal::phase_jump}};
        a.owner = 5;
        a.box = 1;
        check(hex(w::encode(a)) == "0d000000" "00000000" "01" "0200" "01" "1700" "07" "00" "0500" "04" "05000000" "01", "golden STORY_ACK");
        check(w::decode_story_ack(w::encode(a)).owner == 5, "STORY_ACK round trip");
        check(std::uint8_t(dw4::engine::StoryRefusal::owner) == 12, "refusal owner is 12");
    }
    {
        w::BarrierOk ok;
        ok.inst = 7;
        ok.visit = 3;
        ok.decision = w::Decision::co_arrive;
        ok.new_inst = 8;
        ok.new_visit = 1;
        ok.stage = "stag0101";
        ok.point = 2;
        ok.own_slot = 1;
        ok.slot_map = {-1, -1, -1, -1};
        ok.mode = w::Mode::sealed;
        ok.delay = 3;
        check(hex(w::encode(ok)) == "07000000" "03000000" "04" "08000000" "01000000" "08" "7374616730313031" "02" "01" "ffffffff" "02" "03" "00"
                                    "00000000" "00000000" "00" "00"
                                    "00000000" "00000000" "00" "00" "0000010000000000000000" "0000000000000000" "00000000" "01" "00" "00" "00",
              "golden BARRIER_OK (home world part)");
        ok.wid = 7;
        ok.host = 5;
        ok.visibility = w::Visibility::private_;
        ok.arrival = w::Arrival::guest_new;
        ok.settings = settings;
        ok.settings_hash = 0x0102030405060708ull;
        ok.owner = 5;
        ok.box = 1;
        ok.story_mode = w::StoryMode::replace;
        ok.redirect_stage = "stagml11";
        ok.redirect_point = 3;
        const auto bytes = hex(w::encode(ok));
        const std::string tail = "07000000" "05000000" "02" "04" "0101000192100000010102" "0807060504030201" "05000000" "01" "01" "08" "737461676d6c3131" "03";
        check(bytes.size() > tail.size() && bytes.substr(bytes.size() - tail.size()) == tail, "golden BARRIER_OK world part");
        ok.co_arrivals = {{1, 0x01020304u, {}}};
        const auto back = w::decode_barrier_ok(w::encode(ok));
        check(back.wid == 7 && back.host == 5 && back.visibility == w::Visibility::private_ && back.arrival == w::Arrival::guest_new &&
                  back.settings == settings && back.settings_hash == ok.settings_hash && back.owner == 5 && back.story_mode == w::StoryMode::replace &&
                  back.redirect_stage == "stagml11" && back.redirect_point == 3 && back.co_arrivals == ok.co_arrivals,
              "BARRIER_OK world round trip");
        check(hex(w::encode(ok)).find("01" "04030201" "00000000") != std::string::npos, "golden co-arrival row with a u32 member");
    }
    {
        w::InstanceInfo info;
        info.wid = 7;
        info.host = 5;
        info.visibility = w::Visibility::friends;
        info.settings = settings;
        info.area = "dry_land";
        info.members = {{5, "stag0201", 23, 9}, {9, "dng204b1", 12, 17}};
        info.taken = 2;
        info.role = w::Role::guest;
        check(hex(w::encode(info)) == "07000000" "05000000" "01" "0101000192100000010102" "08" "6472795f6c616e64" "00" "02" "05000000" "08"
                                      "7374616730323031" "1700" "09" "09000000" "08" "646e673230346231" "0c00" "11" "02" "00" "00" "0000" "01",
              "golden INSTANCE_INFO");
        const auto back = w::decode_instance_info(w::encode(info));
        check(back.members == info.members && back.area == "dry_land" && back.role == w::Role::guest && back.taken == 2, "INSTANCE_INFO round trip");
        check(hex(w::encode(w::InstanceInfo{})) == "00000000" "00000000" "00" "0000010000000000000000" "00" "00" "00" "00" "00" "00" "0000" "00",
              "golden INSTANCE_INFO home");
    }
    check(hex(w::encode(w::WorldNotice{w::NoticeKind::ended, 5, "kim", 1, ""})) == "08" "05000000" "03" "6b696d" "01000000" "00", "golden WORLD_NOTICE");
    check(hex(w::encode(w::MemberInfo{9, "lee", 12, 9})) == "09000000" "03" "6c6565" "0c00" "09", "golden MEMBER_INFO");
    check(hex(w::encode(w::InstanceOp{w::InstanceOpKind::visibility, 2})) == "01" "02000000", "golden INSTANCE_OP");
    check(throws([&] { (void)w::encode(w::InstanceOp{w::InstanceOpKind::visibility, 3}); }), "a visibility above 2 is refused");
    check(hex(w::encode(w::JoinReq{7, 2, 1, 3, w::JoinKind::home})) == "07000000" "02000000" "01" "03" "02", "golden JOIN_REQ home");
    check(hex(w::encode(w::JoinReq{7, 2, 1, 3})) == "07000000" "02000000" "01" "03" "01", "golden JOIN_REQ join");
    {
        w::JoinReq go{7, 2, 1, 3, w::JoinKind::go, "dng204b1", 0};
        const auto back = w::decode_join_req(w::encode(go));
        check(back.kind == w::JoinKind::go && back.stage == "dng204b1", "JOIN_REQ go round trip");
    }
    {
        w::TickBody t;
        w::Event home{w::EventKind::home};
        home.mask = 0b0101;
        home.reason = std::uint8_t(w::HomeReason::ended);
        t.events = {home};
        w::Writer tw;
        w::write_tick(tw, t);
        check(hex(tw.out) == "00" "01" "06" "0200" "05" "02", "golden tick record with HOME");
        w::Reader r{tw.out};
        const auto back = w::read_tick(r);
        check(back.events.size() == 1 && back.events[0].kind == w::EventKind::home && back.events[0].mask == 5 && back.events[0].reason == 2,
              "HOME round trip");
        w::TickBody g;
        w::Event go{w::EventKind::go, 2, 1};
        go.stage = "dng204b1";
        g.events = {go};
        w::Writer gw;
        w::write_tick(gw, g);
        w::Reader gr{gw.out};
        const auto gb = w::read_tick(gr);
        check(gb.events.size() == 1 && gb.events[0].stage == "dng204b1" && gb.events[0].slot == 2, "GO round trip");
    }
    check(hex(w::encode(w::Intent{7, 2, w::IntentKind::leave, 0})) == "07000000" "02000000" "03" "00000000", "golden INTENT leave");
    check(w::decode_intent(w::encode(w::Intent{7, 2, w::IntentKind::leave, 0})).kind == w::IntentKind::leave, "INTENT leave round trip");
    {
        w::Presence p{9, "stagml11", 1, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6};
        p.relation = 8;
        const auto bytes = hex(w::encode(p, true));
        check(bytes.rfind("09000000" "08" "08" "737461676d6c3131", 0) == 0, "golden PRESENCE out (u32 member, relation)");
        check(w::decode_presence(w::encode(p, true), true) == p, "PRESENCE out round trip");
        p.version = w::kPresenceVersion;
        check(w::decode_presence2(w::encode_presence2(p, true), true) == p, "PRESENCE2 out round trip");
    }
    check(hex(w::encode(w::Resume{1, 2, 3, 4, "stag0101"})) == "01000000" "02000000" "03" "04000000" "08" "7374616730313031", "golden RESUME");
    check(w::decode_resume(w::encode(w::Resume{1, 2, 3, 4, "stag0101"})).stage == "stag0101", "RESUME round trip");
    check(std::uint8_t(w::Type::instance_op) == 0x1B && std::uint8_t(w::Type::instance_info) == 0x95 && std::uint8_t(w::Type::world_notice) == 0x96 &&
              std::uint8_t(w::Type::member_info) == 0x97,
          "protocol 49's numbers");
    check(std::uint8_t(w::CloseReason::account) == 9, "CLOSE account is 9");
}

// Round I1b's go along (docs/SEAMLESS.md 19.18, 19.30; DW4_PC-wt/i1b-contract.md
// "go 2026-11-07 16:15"; the relay's WireGoldenTests.Go_along_messages_equal_the_clients
// holds the same bytes).
void go_messages() {
    check(std::uint8_t(w::Type::offer) == 0x9B, "OFFER is 0x9B");
    // HEADING with the exit port's asked mask appended.
    check(hex(w::encode(w::Heading{7, "stag0201", 0, 0x10ull, 0b0100})) == "07000000" "08" "7374616730323031" "00" "1000000000000000" "04",
          "golden HEADING with asked");
    check(w::decode_heading(w::encode(w::Heading{7, "stag0201", 0, 0x10ull, 0b0100})).asked == 0b0100, "HEADING asked round trip");
    check(throws([&] { (void)w::decode_heading(w::encode(w::Heading{7, "stag0201", 0, 0x10ull, 0})) /* fine */; }) == false, "HEADING asked 0");
    {
        auto bad = w::encode(w::Heading{7, "stag0201", 0, 0x10ull, 0});
        bad.back() = 0x10;
        check(throws([&] { (void)w::decode_heading(bad); }), "HEADING asked above slot 3 is refused");
        bad.pop_back();
        check(throws([&] { (void)w::decode_heading(bad); }), "HEADING without asked is refused (protocol 49)");
    }
    // STATE flag 16, no living member.
    check(hex(w::encode(w::State{7, 3, 61, w::state_flag::no_living | w::state_flag::membership_safe, 5, w::NotJoinable::scene})) ==
              "07000000" "03000000" "3d000000" "12" "0500000000000000" "02",
          "golden STATE with no living member");
    check(w::decode_state(w::encode(w::State{7, 3, 61, 16, 5, w::NotJoinable::none})).flags == 16, "STATE flag 16 decodes");
    // INTENT 4 go along (the offer id) and its withdraw (0).
    check(hex(w::encode(w::Intent{7, 2, w::IntentKind::go_along, 3})) == "07000000" "02000000" "04" "03000000", "golden INTENT go along");
    check(hex(w::encode(w::Intent{7, 2, w::IntentKind::go_along, 0})) == "07000000" "02000000" "04" "00000000", "golden INTENT withdraw");
    // JOIN_REQ kind go with stage and point.
    {
        w::JoinReq req{7, 2, 1, 3, w::JoinKind::go, "dng204b1", 0};
        check(hex(w::encode(req)) == "07000000" "02000000" "01" "03" "03" "08" "646e673230346231" "00", "golden JOIN_REQ go");
        const auto back = w::decode_join_req(w::encode(req));
        check(back.kind == w::JoinKind::go && back.stage == "dng204b1" && back.point == 0 && back.slot == 1, "JOIN_REQ go round trip");
    }
    // Round I1b's scenario hooks: the TEST event (slot | hook | argument) and
    // INTENT kind test (WireGoldenTests.cs holds the same bytes).
    {
        w::TickBody t;
        w::Event test;
        test.kind = w::EventKind::test;
        test.slot = 2;
        test.hook = std::uint8_t(w::TestHook::busy);
        test.argument = 600;
        t.events.push_back(test);
        w::Writer tw;
        w::write_tick(tw, t);
        check(hex(tw.out) == "00" "01" "f0" "0600" "02" "02" "58020000", "golden tick record with TEST " + hex(tw.out));
        w::Reader tr{tw.out};
        check(w::read_tick(tr) == t, "TEST round trip");
        check(hex(w::encode(w::Intent{7, 2, w::IntentKind::test, 2 | (600 << 8)})) == "07000000" "02000000" "7f" "02580200", "golden INTENT test");
        check(w::decode_intent(w::encode(w::Intent{7, 2, w::IntentKind::test, 1})).kind == w::IntentKind::test, "INTENT test decodes");
    }
    // The GO tick event: slot | point | stage.
    {
        w::TickBody t;
        w::Event go;
        go.kind = w::EventKind::go;
        go.slot = 1;
        go.point = 2;
        go.stage = "dng204b1";
        t.events.push_back(go);
        w::Writer tw;
        w::write_tick(tw, t);
        check(hex(tw.out) == "00" "01" "07" "0b00" "01" "02" "08" "646e673230346231", "golden tick record with GO " + hex(tw.out));
        w::Reader tr{tw.out};
        check(w::read_tick(tr) == t, "GO round trip");
    }
    // OFFER: open with the notice (two entrants), the close.
    {
        w::Offer open{3, w::OfferOp::open, w::Offer::notice | w::Offer::late, {5, 9}, "dng204b1", 20, w::OfferClose::none};
        check(hex(w::encode(open)) == "0300" "01" "03" "02" "05000000" "09000000" "08" "646e673230346231" "14" "00", "golden OFFER open");
        check(w::decode_offer(w::encode(open)) == open, "OFFER round trip");
        w::Offer closed{3, w::OfferOp::closed, 0, {5}, "dng204b2", 20, w::OfferClose::left};
        check(hex(w::encode(closed)) == "0300" "03" "00" "01" "05000000" "08" "646e673230346232" "14" "01", "golden OFFER closed");
        auto bytes = w::encode(closed);
        bytes[0] = 0;
        bytes[1] = 0;
        check(throws([&] { (void)w::decode_offer(bytes); }), "OFFER id 0 is refused");
        bytes = w::encode(closed);
        bytes[2] = 4;
        check(throws([&] { (void)w::decode_offer(bytes); }), "OFFER op 4 is refused");
        bytes = w::encode(closed);
        bytes.back() = 5;
        check(throws([&] { (void)w::decode_offer(bytes); }), "OFFER close reason 5 is refused");
        w::Offer five{3, w::OfferOp::open, 0, {1, 2, 3, 4, 5}, "dng204b1", 20, w::OfferClose::none};
        check(throws([&] { (void)w::encode(five); }), "OFFER with five entrants is refused");
    }
    // WORLD_NOTICE go refused (kind 13) with its reason.
    {
        w::WorldNotice n{w::NoticeKind::go_refused, 5, "kim", std::uint32_t(w::GoRefused::closed), "dng204b1"};
        check(hex(w::encode(n)) == "0d" "05000000" "03" "6b696d" "01000000" "08" "646e673230346231", "golden WORLD_NOTICE go refused");
    }
}

void messages() {
    // Tick records: mask, event count, pads, events (type, u16 length, payload).
    const auto t = tick_with_events();
    w::Writer tw;
    w::write_tick(tw, t);
    check(tw.out.size() == 2 + 3 * 6 + (3 + 6) + (3 + 1) + (3 + 6) + (3 + 4) + (3 + 2), "tick record size");
    w::Reader tr{tw.out};
    check(w::read_tick(tr) == t && tr.done(), "tick round trip");
    // Client -> server.
    w::Input in{7, 2, 99, {{0x8000, 1, 2, 3, 4}, {0, 0, 0, 0, 0}}};
    auto in2 = w::decode_input(w::encode(in));
    check(in2.inst == 7 && in2.visit == 2 && in2.first == 99 && in2.pads == in.pads, "INPUT");
    check(w::encode(w::Input{7, 2, 99, {{}}}).size() == 4 + 4 + 4 + 1 + 6, "INPUT with one pad is 19 bytes");
    check(throws([&] { w::encode(w::Input{7, 2, 99, {}}); }), "INPUT needs a pad");
    check(throws([&] { w::encode(w::Input{7, 2, 99, std::vector<WirePad>(33)}); }), "INPUT carries at most 32 pads");
    w::Log log{7, 2, 100, {{1, 0, 0, 0, 0}, {2, 0, 0, 0, 0}}};
    auto log2 = w::decode_log(w::encode(log));
    check(log2.first == 100 && log2.pads.size() == 2 && log2.pads[1].buttons == 2, "LOG");
    check(throws([&] { w::encode(w::Log{7, 2, 0, std::vector<WirePad>(1201)}); }), "LOG carries at most 1200 pads");
    w::Heading heading{7, "stag0102", 3, 0xABCDull};
    auto heading2 = w::decode_heading(w::encode(heading));
    check(heading2.stage == "stag0102" && heading2.point == 3 && heading2.context == 0xABCDull, "HEADING");
    w::Barrier barrier{7, 2, 500, "dng101b1", 1, 0b11, 0b11, 0x1122334455667788ull, {9, 9}};
    auto barrier2 = w::decode_barrier(w::encode(barrier));
    check(barrier2.tick == 500 && barrier2.stage == "dng101b1" && barrier2.context == barrier.context && barrier2.carry == barrier.carry, "BARRIER");
    check(throws([&] { w::encode(w::Barrier{7, 2, 500, "bad name!", 1, 1, 1, 0, {}}); }), "a stage name is A-Z a-z 0-9 _");
    auto choice = w::decode_barrier_choice(w::encode(w::BarrierChoice{7, w::Choice::alone}));
    check(choice.choice == w::Choice::alone, "BARRIER_CHOICE");
    auto split = w::decode_split(w::encode(w::Split{7, 2, 800, 0b10, w::SplitKind::in_stage}));
    check(split.tick == 800 && split.leavers == 2 && split.kind == w::SplitKind::in_stage, "SPLIT");
    check(throws([&] { w::decode_split(w::encode(w::Split{7, 2, 800, 0, w::SplitKind::exit})); }), "SPLIT without leavers is refused");
    auto hash = w::decode_snaphash(w::encode(w::SnapHash{7, 3, 0xdeadbeefcafef00dull}));
    check(hash.hash == 0xdeadbeefcafef00dull && hash.visit == 3, "SNAPHASH");
    auto ready = w::decode_ready(w::encode(w::Ready{7, 3, 120, 42, 4100, {5, 6}}));
    check(ready.tick == 120 && ready.fingerprint == 42 && ready.rate == 4100 && ready.carry.size() == 2, "READY");
    check(w::decode_join_at(w::encode(w::JoinAt{7, 3, 333})).tick == 333, "JOIN_AT");
    check(w::decode_netfp(w::encode(w::NetFp{7, 3, 60, 77})).hash == 77, "NETFP");
    auto state = w::decode_state(w::encode(w::State{7, 3, 61, w::state_flag::joinable | w::state_flag::boss_lock, 5, w::NotJoinable::boss}));
    check(state.tick == 61 && state.flags == 5 && state.reason == w::NotJoinable::boss, "STATE");
    check(w::decode_state(w::encode(w::State{7, 3, 61, w::state_flag::load_ended | w::state_flag::boss_lock, 5, w::NotJoinable::boss})).flags == 12,
          "STATE with the load end");
    check(throws([&] { w::decode_state(w::encode(w::State{7, 3, 61, 32, 5, w::NotJoinable::none})); }), "STATE with an unknown flag is refused");
    auto intent = w::decode_intent(w::encode(w::Intent{7, 3, w::IntentKind::give_up, 0}));
    check(intent.kind == w::IntentKind::give_up, "INTENT");
    w::Presence presence{0, "stagml11", 9, 1.5f, -2.0f, 300.25f, 0.5f, 3, 12, 80, 120, 10, 30};
    check(w::decode_presence(w::encode(presence, false), false) == presence, "PRESENCE (client)");
    presence.member = 4;
    check(w::decode_presence(w::encode(presence, true), true) == presence, "PRESENCE (server)");
    presence.stage.clear();
    check(w::decode_presence(w::encode(presence, true), true).stage.empty(), "PRESENCE with an empty stage (hidden)");
    // PRESENCE2 (round 60): the version, the PRESENCE fields and the figure's.
    w::Presence ghost{0, "stagML11", 9, 1.5f, -2.0f, 300.25f, 0.5f, 3, 12, 80, 120, 10, 30};
    ghost.version = w::kPresenceVersion;
    ghost.clip = "runD";
    ghost.flags = w::Presence::loop | w::Presence::busy;
    ghost.warp = 250;
    ghost.sent_ms = 0xFFFFFFF0u;
    ghost.weapon = "w_aguclaw01";
    const auto ghost_bytes = w::encode_presence2(ghost, false);
    check(ghost_bytes.front() == 1 && w::decode_presence2(ghost_bytes, false) == ghost, "PRESENCE2 (client)");
    ghost.member = 9;
    check(w::decode_presence2(w::encode_presence2(ghost, true), true) == ghost, "PRESENCE2 (server)");
    check(w::encode(ghost, false).size() + 1 + 1 + 4 + 1 + 1 + 4 + 1 + 11 == ghost_bytes.size(), "PRESENCE2 is PRESENCE plus the figure's fields");
    check(w::decode_presence(w::encode(ghost, false), false).version == 0, "PRESENCE carries no version");
    ghost.clip.clear();
    ghost.weapon.clear();
    check(w::decode_presence2(w::encode_presence2(ghost, true), true) == ghost, "PRESENCE2 without a clip or weapon");
    ghost.clip = "run-D";
    check(throws([&] { w::encode_presence2(ghost, false); }), "a clip outside A-Z a-z 0-9 _ is refused");
    ghost.clip = std::string(17, 'a');
    check(throws([&] { w::encode_presence2(ghost, false); }), "a clip of 17 characters is refused");
    ghost.clip = "runD";
    {
        auto bytes = w::encode_presence2(ghost, false);
        auto zero = bytes;
        zero[0] = 0;
        check(throws([&] { w::decode_presence2(zero, false); }), "version 0 is refused");
        auto later = bytes;
        later[0] = 2;
        later.push_back(0xAB);
        later.push_back(0xCD);
        const auto read = w::decode_presence2(later, false);
        check(read.version == 2 && read.clip == "runD" && read.warp == 250, "a later version's appended bytes are skipped");
        auto extra = bytes;
        extra.push_back(0);
        check(throws([&] { w::decode_presence2(extra, false); }), "version 1 with a byte too many is refused");
        auto cut = bytes;
        cut.pop_back();
        check(throws([&] { w::decode_presence2(cut, false); }), "a cut PRESENCE2 is refused");
        auto bad = bytes;
        const std::size_t clip_at = bytes.size() - (1 + 1 + 4 + 1 + ghost.weapon.size()) - ghost.clip.size();
        bad[clip_at] = '-';
        check(throws([&] { w::decode_presence2(bad, false); }), "a received clip with a '-' is refused");
        auto nan = bytes;
        const std::size_t x_at = 1 + 1 + ghost.stage.size() + 4;
        nan[x_at] = 0;
        nan[x_at + 1] = 0;
        nan[x_at + 2] = 0xC0;
        nan[x_at + 3] = 0x7F;
        check(throws([&] { w::decode_presence2(nan, false); }), "a NaN position is refused");
    }
    auto resume = w::decode_resume(w::encode(w::Resume{7, 3, 1, 999}));
    check(resume.slot == 1 && resume.last_tick == 999, "RESUME");
    check(w::decode_rate(w::encode(std::uint32_t(4321))) == 4321, "RATE");
    check(w::decode_pong(w::encode(w::Pong{5, 123})).client_time_ms == 123, "PONG");
    check(w::decode_bye(w::encode(w::Bye{1})).reason == 1, "BYE");
    // Server -> client.
    w::Welcome welcome{3, 41, 1, 0, w::Mode::hub, "", w::kNoTick, 0, 30, 0};
    check((w::decode_welcome(w::encode(w::Welcome{3, 41, 1, 0, w::Mode::hub, "", w::kNoTick, 0, 30,
                                                  std::uint8_t(w::Welcome::resumable | w::Welcome::presence_v2)}))
               .flags & w::Welcome::presence_v2) != 0,
          "WELCOME's presence_v2 flag");
    auto welcome2 = w::decode_welcome(w::encode(welcome));
    check(welcome2.member == 3 && welcome2.inst == 41 && welcome2.mode == w::Mode::hub && welcome2.stage.empty() &&
              welcome2.last_tick == w::kNoTick && welcome2.max_catch_up == 30,
          "WELCOME");
    auto ticks = w::decode_ticks(w::encode(w::Ticks{7, 3, 10, {t, w::TickBody{}}}));
    check(ticks.first == 10 && ticks.ticks.size() == 2 && ticks.ticks[0] == t && ticks.ticks[1] == w::TickBody{}, "SEALED / LIVE");
    const auto join_req = w::decode_join_req(w::encode(w::JoinReq{7, 3, 2, 4}));
    check(join_req.slot == 2 && join_req.delay == 4, "JOIN_REQ");
    w::BarrierOk ok;
    ok.inst = 7;
    ok.visit = 3;
    ok.decision = w::Decision::new_instance;
    ok.new_inst = 8;
    ok.new_visit = 1;
    ok.stage = "dng101b1";
    ok.point = 2;
    ok.own_slot = 0;
    ok.slot_map = {0, -1, -1, 1};
    ok.mode = w::Mode::sealed;
    ok.delay = 3;
    ok.co_arrivals = {{2, 5, {1}}, {3, 6, {2, 2}}};
    ok.story_revision = 5;
    ok.story = {1, 2, 3};
    ok.notice = w::Notice::full;
    ok.names = {"kim", "lee"};
    auto ok2 = w::decode_barrier_ok(w::encode(ok));
    check(ok2.decision == w::Decision::new_instance && ok2.new_inst == 8 && ok2.stage == "dng101b1" && ok2.slot_map == ok.slot_map &&
              ok2.co_arrivals == ok.co_arrivals && ok2.story == ok.story && ok2.mode == w::Mode::sealed &&
              ok2.delay == 3 && ok2.notice == w::Notice::full && ok2.names == ok.names,
          "BARRIER_OK");
    check(ok.present() == 0b1111, "BARRIER_OK: the members are the slot map's new slots and the co-arrivals");
    ok.decision = w::Decision::dissolve;
    check(ok.present() == 0b0001, "DISSOLVE: slot 0 alone");
    auto wait = w::decode_barrier_wait(w::encode(w::BarrierWait{7, w::WaitReason::boss_lock, 12, {"kim", "lee"}, 1}));
    check(wait.reason == w::WaitReason::boss_lock && wait.seconds_left == 12 && wait.names.size() == 2 && wait.flags == 1, "BARRIER_WAIT");
    w::StateData body;
    body.purpose = w::StatePurpose::join;
    body.slot = 1;
    body.snapshot.assign(150000, 0x5A);
    body.first = 0;
    body.ticks.assign(3000, t);
    body.forks = {{800, 2, w::SplitKind::exit, 0}};
    const auto bytes = w::encode_state_body(body);
    auto chunks = w::make_chunks(7, 3, bytes);
    check(chunks.size() == (bytes.size() + w::kStateChunk - 1) / w::kStateChunk, "chunk count");
    w::ChunkAssembler assembler;
    std::optional<std::vector<std::uint8_t>> whole;
    for (const auto& c : chunks) {
        auto c2 = w::decode_chunk(w::encode(c));
        check(c2.inst == 7 && c2.visit == 3 && c2.total == bytes.size(), "STATE_DATA chunk header");
        whole = assembler.add(c2, 64u << 20);
    }
    check(whole && *whole == bytes, "chunks reassemble");
    auto body2 = w::decode_state_body(*whole);
    check(body2.purpose == w::StatePurpose::join && body2.slot == 1 && body2.snapshot == body.snapshot && body2.ticks.size() == 3000 &&
              body2.ticks[2999] == t && body2.forks == body.forks,
          "STATE_DATA body");
    check(throws([&] { w::ChunkAssembler a; a.add(chunks[1], 64u << 20); }), "a chunk out of order is refused");
    check(throws([&] { w::ChunkAssembler a; a.add(chunks[0], 1000); }), "a transfer above its limit is refused");
    auto empty_chunks = w::make_chunks(1, 1, {});
    check(empty_chunks.size() == 1 && w::ChunkAssembler{}.add(empty_chunks[0], 10).value().empty(), "an empty body is one chunk");
    world_messages();
    go_messages();
    auto ping = w::decode_ping(w::encode(w::Ping{9, 1000, -2, 3}));
    check(ping.margin_ms == -2 && ping.delay == 3, "PING");
    // NETSTAT (round 62): golden bytes shared with WireGoldenTests.cs.
    check(hex(w::encode(w::NetStat{24, 6, 3, -2, 1000, 5, 7, w::NetStat::in_stream})) == "1800" "0600" "03" "feff" "e8030000" "05000000" "07000000" "01",
          "golden NETSTAT");
    const auto netstat = w::decode_netstat(w::encode(w::NetStat{24, 6, 3, -2, 1000, 5, 7, 1}));
    check(netstat.rtt_ms == 24 && netstat.spread_ms == 6 && netstat.delay == 3 && netstat.margin_ms == -2 && netstat.sealed == 1000 &&
              netstat.filled == 5 && netstat.late == 7 && netstat.flags == 1,
          "NETSTAT round trip");
    check(std::uint8_t(w::Type::netstat) == 0x8F && std::string(w::type_name(w::Type::netstat)) == "NETSTAT", "NETSTAT type");
    // Round J (SEAMLESS.md 6.13.7): STATEHASH's golden bytes are WireGoldenTests.cs's too.
    check(hex(w::encode(w::StateHash{3, 4, 1200, 0x0123456789ABCDEFull, 17, 1})) == "03000000" "04000000" "b0040000" "efcdab8967452301" "11000000" "01",
          "golden STATEHASH");
    {
        const auto sh = w::decode_statehash(w::encode(w::StateHash{3, 4, 1200, 5, 17, 1}));
        check(sh.tick == 1200 && sh.hash == 5 && sh.load_end == 17 && sh.flags == 1, "STATEHASH round trip");
        bool refused = false;
        try {
            (void)w::decode_statehash(w::encode(w::StateHash{3, 4, 10, 5, 11, 0}));
        } catch (const w::WireError&) {
            refused = true;
        }
        check(refused, "STATEHASH: a load end beyond the tick is refused");
        w::Checkpoint c{3, 4, 1200, 9, 17, 10, 4, {1, 2, 3}};
        const auto cb = w::decode_checkpoint(w::encode(c));
        check(cb.tick == 1200 && cb.load_end == 17 && cb.total == 10 && cb.offset == 4 && cb.bytes == c.bytes, "CHECKPOINT round trip");
        const auto bad = w::decode_checkpoint_bad(w::encode(w::CheckpointBad{1, 2, 3, w::CheckpointBadReason::topology, "journal"}));
        check(bad.reason == w::CheckpointBadReason::topology && bad.section == "journal", "CHECKPOINT_BAD round trip");
        const auto ask = w::decode_checkpoint_ask(w::encode(w::CheckpointAsk{1, 2, 0, 1}));
        check(ask.inst == 1 && ask.visit == 2 && ask.flags == 1, "CHECKPOINT_ASK round trip");
        w::Ready ready{1, 2, 3, 4, 5, {6}};
        check(w::decode_ready(w::encode(ready)).flags == 0 && w::encode(ready).size() == 29, "READY without flags as before");
        ready.flags = w::Ready::from_checkpoint;
        check(w::decode_ready(w::encode(ready)).flags == w::Ready::from_checkpoint, "READY from a checkpoint");
        // STATE_DATA codec 2: the load ticks, the checkpoint and the ticks from T.
        w::StateData sd;
        sd.purpose = w::StatePurpose::join;
        sd.slot = 1;
        sd.snapshot = {7, 7};
        sd.load_ticks.resize(3);
        sd.checkpoint_tick = 650;
        sd.checkpoint_hash = 0xBEEF;
        sd.checkpoint_flags = 1;
        sd.checkpoint = std::vector<std::uint8_t>{0, 1, 2};
        sd.first = 650;
        sd.ticks.resize(2);
        const auto back = w::decode_state_body(w::encode_state_body(sd));
        check(back.checkpoint && *back.checkpoint == *sd.checkpoint && back.load_ticks.size() == 3 && back.checkpoint_tick == 650 &&
                  back.checkpoint_hash == 0xBEEF && back.checkpoint_flags == 1 && back.first == 650 && back.ticks.size() == 2,
              "STATE_DATA with a checkpoint");
        sd.checkpoint.reset();
        check(!w::decode_state_body(w::encode_state_body(sd)).checkpoint, "STATE_DATA without one");
    }
    {
        // The type registry (SEAMLESS.md 6.3; WireGoldenTests.Message_types_are_unique_and_registered): the numbers
        // type_name knows are exactly these. A number given twice does not compile (type_name's switch); a new type
        // fails this until it is entered here and in WireGoldenTests.cs (round 62 review: round 60's PRESENCE2 on 0x8F).
        std::string known;
        for (int b = 0; b < 256; ++b)
            if (std::string(w::type_name(w::Type(b))) != "?") known += (known.empty() ? "" : " ") + std::string(w::type_name(w::Type(b)));
        check(known == "INPUT LOG HEADING BARRIER BARRIER_CHOICE SPLIT SNAPSHOT SNAPHASH READY JOIN_AT NETFP STATE INTENT STORY PRESENCE RESUME "
                       "RATE PONG BYE PRESENCE2 STATEHASH CHECKPOINT CHECKPOINT_BAD INSTANCE_OP WELCOME SEALED LIVE JOIN_REQ BARRIER_OK BARRIER_WAIT "
                       "STATE_DATA PRESENCE RESYNC STORY_ACK ROOM PING CLOSE ROSTER REASSIGN NETSTAT PRESENCE2 CHECKPOINT_ASK INSTANCE_INFO WORLD_NOTICE "
                       "MEMBER_INFO OFFER",
              "type registry: " + known);
        check(std::uint8_t(w::Type::bye) == 0x13 && std::uint8_t(w::Type::reassign) == 0x8E, "the registry's last numbers before NETSTAT");
        // Round 60's PRESENCE2: 0x14 to the relay, 0x90 back (first 0x8F, NETSTAT's since DW4Online 0.1.2).
        check(std::uint8_t(w::Type::presence2) == 0x14 && std::uint8_t(w::Type::presence2_of) == 0x90, "PRESENCE2's numbers");
    }
    auto close = w::decode_close(w::encode(w::Close{w::CloseReason::kicked, "bye"}));
    check(close.reason == w::CloseReason::kicked && close.text == "bye", "CLOSE");
    check(w::decode_inst_visit(w::encode(w::InstVisit{4, 5})).visit == 5, "RESYNC");
    const auto reassign = w::decode_reassign(w::encode(w::Reassign{4, 5, 9, 1200, w::Mode::solo, 1}));
    check(reassign.inst == 4 && reassign.new_inst == 9 && reassign.from_tick == 1200 && reassign.mode == w::Mode::solo && reassign.flags == 1, "REASSIGN");
    // Golden bytes: the relay's C# encoder writes the same (WireGoldenTests.cs).
    check(hex(tw.out) == "0b0500400aec00000800b64a03fd000000003b00" "010600020501020304" "02010001" "0306000302b3ffffff" "04040040e20100"
                         "0502000004",
          "golden tick record " + hex(tw.out));
    check(hex(w::encode(w::Input{7, 2, 99, {{0x8000, 1, 2, 3, 4}}})) == "070000000200000063000000010080010203" "04", "golden INPUT");
    check(hex(w::encode(w::Heading{7, "stag0102", 3, 0xABCDull})) == "0700000008737461673031303203cdab000000000000" "00", "golden HEADING");
    // (Protocol 49: the token, the plan id and the own settings appended; the
    // relay's WireGoldenTests.World_messages_equal_the_clients holds the same.)
    check(hex(w::encode(w::Barrier{7, 2, 500, "dng101b1", 1, 3, 3, 0x1122334455667788ull, {9, 9}})) ==
              "0700000002000000f401000008646e67313031623101030388776655443322110200000009" "09" "00000000" "0000" "0000010000000000000000",
          "golden BARRIER");
    check(hex(w::encode(w::State{7, 3, 61, 5, 5, w::NotJoinable::boss})) == "07000000030000003d00000005050000000000000005", "golden STATE");
    check(hex(w::encode(w::Welcome{3, 41, 1, 0, w::Mode::hub, "", w::kNoTick, 0, 30, 0})) ==
              "03000000" "29000000" "01000000" "00" "00" "00" "ffffffff" "00" "1e00" "00" "00000000" "01",
          "golden WELCOME");
    {
        // (HubCoreTests / WireGoldenTests "PRESENCE2" on the relay's side.)
        w::Presence g{0, "stagml11", 9, 1.5f, -2.0f, 3.0f, 0.25f, 4, 99, 500, 600, 70, 80};
        g.clip = "runD";
        g.flags = w::Presence::loop | w::Presence::busy;
        g.warp = 7;
        g.sent_ms = 0x01020304u;
        g.weapon = "w_a1";
        check(hex(w::encode_presence2(g, false)) == "01" "08737461676d6c3131" "09000000" "0000c03f" "000000c0" "00004040" "0000803e" "04" "6300"
                                                    "f401" "5802" "4600" "5000" "0472756e44" "03" "07" "04030201" "04775f6131",
              "golden PRESENCE2 " + hex(w::encode_presence2(g, false)));
    }
    w::BarrierOk golden;
    golden.inst = 7;
    golden.visit = 3;
    golden.decision = w::Decision::co_arrive;
    golden.new_inst = 8;
    golden.new_visit = 1;
    golden.stage = "stag0101";
    golden.point = 2;
    golden.own_slot = 1;
    golden.slot_map = {-1, -1, -1, -1};
    golden.mode = w::Mode::sealed;
    golden.delay = 3;
    check(hex(w::encode(golden)) == "0700000003000000" "04" "0800000001000000" "087374616730313031" "02" "01" "ffffffff" "02" "03" "00"
                                    "0000000000000000" "00" "00" "00000000" "00000000" "00" "00" "0000010000000000000000" "0000000000000000" "00000000" "01" "00" "00" "00",
          "golden BARRIER_OK " + hex(w::encode(golden)));
    w::StateData small;
    small.purpose = w::StatePurpose::co_arrival;
    small.slot = 1;
    small.snapshot = {0xAA, 0xBB};
    small.forks = {{800, 2, w::SplitKind::exit, 1}};
    small.first = 0;
    small.ticks = {w::TickBody{}};
    check(hex(w::encode_state_body(small)) == "02010002000000aabb0120030000020001000000000100000000" "00",
          "golden STATE_DATA blob " + hex(w::encode_state_body(small)));
    // STORY / STORY_ACK (SEAMLESS.md 8.3; the same bytes in WireGoldenTests.cs).
    w::Story story{7, 2, 12, {}};
    story.delta.phase = 3;
    story.delta.flags = {19, 392};
    story.delta.folder = {1};
    story.delta.counters = {{7, 4, 2, 1}};
    check(hex(w::encode(story)) == "07000000" "02000000" "0c000000" "03" "0200" "1300" "8801" "01" "01" "01" "07" "04000000" "02000000" "01000000" "00000000" "01",
          "golden STORY " + hex(w::encode(story)));
    const auto story_back = w::decode_story(w::encode(story));
    check(story_back.inst == 7 && story_back.visit == 2 && story_back.revision == 12 && story_back.delta == story.delta, "STORY round trip");
    w::StoryAck ack;
    ack.revision = 13;
    ack.flags = w::StoryAck::answer;
    ack.refused = {{dw4::engine::StoryRefused::Kind::flag, 23, dw4::engine::StoryRefusal::not_story},
                   {dw4::engine::StoryRefused::Kind::phase, 5, dw4::engine::StoryRefusal::phase_jump}};
    check(hex(w::encode(ack)) == "0d000000" "00000000" "01" "0200" "01" "1700" "07" "00" "0500" "04" "00000000" "01", "golden STORY_ACK " + hex(w::encode(ack)));
    dw4::engine::RoomStory room_story;
    room_story.flags[3] = 1;
    room_story.counters[1] = 500;
    ack.story = dw4::engine::story::encode_story(room_story);
    ack.refused.clear();
    const auto ack_bytes = w::encode(ack);
    check(ack_bytes.size() == 4 + 4 + 1100 + 1 + 2 + 5, "STORY_ACK with the story");
    const auto ack_back = w::decode_story_ack(ack_bytes);
    check(ack_back.revision == 13 && dw4::engine::story::decode_story(ack_back.story, ack_back.revision).counters[1] == 500, "STORY_ACK round trip");
    auto bad_ack = ack_bytes;
    bad_ack[4] = 0x4B; // a story blob of 1,099 bytes
    bad_ack[5] = 0x04;
    check(throws([&] { w::decode_story_ack(bad_ack); }), "a story blob of the wrong size is refused");
    auto bad_story = w::encode(story);
    bad_story[13] = 0x01;
    bad_story[14] = 0x04; // 1025 flags
    check(throws([&] { w::decode_story(bad_story); }), "too many story flags are refused");
    bad_story = w::encode(story);
    bad_story[18] = 0x04; // flag 0x0488
    check(throws([&] { w::decode_story(bad_story); }), "a story flag beyond 1023 is refused");
    // Malformed payloads.
    auto short_input = w::encode(in);
    short_input.pop_back();
    check(throws([&] { w::decode_input(short_input); }), "a short INPUT is refused");
    auto long_input = w::encode(in);
    long_input.push_back(0);
    check(throws([&] { w::decode_input(long_input); }), "trailing bytes are refused");
    auto wild_stick = w::encode(w::Input{7, 2, 99, {{0, 0, 0, 0, 0}}});
    wild_stick.back() = 0x7F;
    check(throws([&] { w::decode_input(wild_stick); }), "a stick value beyond 74 is refused");
    std::vector<std::uint8_t> bad_tick{0x10, 0x00};
    check(throws([&] { w::Reader r{bad_tick}; w::read_tick(r); }), "a mask bit above slot 3 is refused");
    std::vector<std::uint8_t> unknown_event{0x00, 0x01, 0x09, 0x02, 0x00, 0xAA, 0xBB};
    {
        w::Reader r{unknown_event};
        const auto skipped = w::read_tick(r);
        check(skipped.events.empty() && r.done(), "an unknown event type is skipped by its length");
    }
    std::vector<std::uint8_t> bad_slot{0x00, 0x01, 0x02, 0x01, 0x00, 0x07};
    check(throws([&] { w::Reader r{bad_slot}; w::read_tick(r); }), "an event slot above 3 is refused");
    std::vector<std::uint8_t> short_event{0x00, 0x01, 0x03, 0x02, 0x00, 0x01, 0x02};
    check(throws([&] { w::Reader r{short_event}; w::read_tick(r); }), "a short event payload is refused");
    std::vector<std::uint8_t> huge_count{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f};
    check(throws([&] { w::decode_state_body(huge_count); }), "a tick count beyond the body is refused");
}

void handshake_and_frames() {
    check(w::hello_line("abc.def", 44, 18446744073709551615ull, 0xab12) == "DW4R2 abc.def 44 18446744073709551615 000000000000ab12\n",
          "hello line (settings hash as 16 hex digits)");
    const auto hello = w::parse_hello("DW4R2 abc.def 44 18446744073709551615 000000000000ab12\r");
    check(hello && hello->ticket == "abc.def" && hello->protocol == 44 && hello->data_hash == 18446744073709551615ull &&
              hello->settings_hash == 0xab12,
          "hello parse");
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
    // Round 62: an excess below the backlog rule that lasts (a join's long step left three ticks waiting) drains by
    // extra steps within about a second instead of 17 s per tick at +0.1 %.
    {
        PaceController p;
        double now = 0, next_arrival = 0;
        std::size_t arrived = 0, executed = 0;
        const double pass = 1.0 / 60.0;
        // Steady arrivals, but the first second's ticks pile up to three waiting (below the backlog rule).
        while (now < 10.0) {
            now += pass;
            while (next_arrival <= now) {
                ++arrived;
                next_arrival += kFieldSeconds;
            }
            if (now < 0.06) continue; // the long step: nothing runs
            executed += std::size_t(p.due_steps(pass, arrived - executed));
            if (now > 6.0) check(arrived - executed <= 2, "the excess drained (" + std::to_string(arrived - executed) + " waiting at " + std::to_string(now) + ")");
        }
        check(p.drained() >= 1 && p.drained() <= 3, "a few extra steps (" + std::to_string(p.drained()) + ")");
        check(!p.catching_up(), "no backlog run for it");
    }
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

void net_stats() {
    // A pad sampled at step 10 for tick 13 runs at step 13 (three ticks of input delay, 50 ms); its picture follows.
    NetStats stats;
    const WirePad a{0x0100, 1, 2, 3, 4}, b{0x0200, 0, 0, 0, 0};
    stats.sampled(1, 13, 1.000, 10, a);
    stats.sampled(1, 14, 1.017, 11, b);
    stats.executed(1, 13, 1.050, 13, a);
    stats.presented(1.060);
    stats.executed(1, 14, 1.067, 14, a); // the relay sealed a fill (the previous pad): this input was lost
    stats.presented(1.080);
    const auto w = stats.take_window();
    check(w.samples == 2 && w.dropped == 1, "two inputs ran, one not as sent");
    check(std::abs(w.exec_avg() - 0.050) < 1e-9 && std::abs(w.steps_avg() - 3.0) < 1e-9, "50 ms, three steps");
    check(w.presented == 2 && std::abs(w.present_avg() - 0.0615) < 1e-9, "the pictures after them");
    check(std::abs(w.exec_quantile(0.5) - 3 * kFieldSeconds) < 1e-9, "the median is three fields");
    check(stats.take_window().samples == 0 && stats.total().samples == 2, "a window starts afresh, the total stays");
    // A tick that never runs here (a new visit) is forgotten, not counted.
    stats.sampled(1, 20, 2.0, 20, a);
    stats.forget_in_flight();
    stats.executed(1, 20, 2.05, 23, a);
    check(stats.total().samples == 2, "forgotten samples do not count");
    // The rolling window holds the last seconds only.
    stats.sampled(2, 1, 10.0, 30, a);
    stats.executed(2, 1, 10.0, 30, a);
    stats.roll_recent(10.5);
    check(stats.recent().samples == 1, "the recent window");
    stats.roll_recent(20.0);
    check(stats.recent().samples == 0, "the recent window forgets");

    check(link_quality(3, 25, 0, false) == LinkQuality::good, "d 3 at 25 ms is good");
    check(link_quality(5, 40, 0, false) == LinkQuality::fair, "d 5 is fair");
    check(link_quality(3, 25, 0.02, false) == LinkQuality::fair, "2 % lost inputs are fair");
    check(link_quality(8, 25, 0, false) == LinkQuality::poor, "d 8 is poor");
    check(link_quality(3, 200, 0, false) == LinkQuality::poor, "200 ms is poor");
    check(link_quality(0, -1, 0, true) == LinkQuality::poor, "waiting for the server is poor");
    check(link_quality(0, -1, 0, false) == LinkQuality::good, "solo, nothing known yet");
}

void online_log() {
    // saves/online.log: appended, rotated above its size (online.1.log ... online.3.log, the oldest goes).
    const auto folder = std::filesystem::temp_directory_path() / ("dw4_online_log_test_" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(folder);
    const auto path = folder / "online.log";
    {
        OnlineLog log;
        check(log.open(path, 4096, 3), "the log opens (and makes its folder)");
        for (int i = 0; i < 400; ++i) log.line("line %d of the link: input delay %d ticks", i, i % 9);
    }
    check(std::filesystem::exists(path), "online.log");
    check(std::filesystem::exists(rotated_log_name(path, 1)) && std::filesystem::exists(rotated_log_name(path, 3)), "rotated files");
    check(!std::filesystem::exists(rotated_log_name(path, 4)), "at most three old ones");
    check(std::filesystem::file_size(path) <= 4096 + 200, "the current file stays small");
    check(rotated_log_name(path, 2).filename() == "online.2.log", "rotated names");
    std::ifstream in(path);
    std::string first;
    std::getline(in, first);
    check(first.size() > 24 && first[4] == '-' && first[10] == ' ' && first[19] == '.' && first.find("of the link") != std::string::npos,
          "lines carry the date and time: " + first);
    in.close();
#ifdef _WIN32
    {
        // Two games from one folder (round 62 review): the second finds online.log over its size and held by the first.
        // It writes nothing there and rotates nothing: online.1-3.log keep what they held (OnlineRun::open_log then
        // takes online-2.log).
        const auto two = folder / "two";
        std::filesystem::create_directories(two);
        const auto held = two / "online.log";
        auto put = [](const std::filesystem::path& p, const std::string& text) {
            std::ofstream out(p, std::ios::binary);
            out << text;
        };
        auto get = [](const std::filesystem::path& p) {
            std::ifstream in(p, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        put(held, std::string(10000, 'x'));
        for (int n = 1; n <= 3; ++n) put(rotated_log_name(held, n), "old " + std::to_string(n));
        OnlineLog game1;
        check(game1.open(held, 1u << 20, 3), "the first game holds online.log");
        OnlineLog game2;
        check(!game2.open(held, 4096, 3), "the second game cannot write the held online.log");
        check(get(rotated_log_name(held, 1)) == "old 1" && get(rotated_log_name(held, 2)) == "old 2" && get(rotated_log_name(held, 3)) == "old 3",
              "the older logs are untouched");
        check(std::filesystem::file_size(held) == 10000, "the held log is untouched");
        game1.line("still the first game's");
        game1.close();
        // The first game rotates its own file when it is over the size.
        OnlineLog again;
        check(again.open(held, 4096, 3), "online.log again");
        check(get(rotated_log_name(held, 1)).size() > 10000 && get(rotated_log_name(held, 2)) == "old 1" && get(rotated_log_name(held, 3)) == "old 2",
              "rotated once: online.log became online.1.log, the oldest went");
        check(!std::filesystem::exists(two / "online.log.rotating"), "nothing left aside");
    }
#endif
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

int main() {
    try {
        pads();
        messages();
        handshake_and_frames();
        pacing();
        leave_combo();
        net_stats();
        online_log();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
