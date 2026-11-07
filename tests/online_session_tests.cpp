// The DW4R2 client against an in-process fake hub (tests/online_fake_hub.hpp,
// the relay's layout) on a simulated clock with 20 ms one-way latency:
// WELCOME, solo-led play with LOG, a mid-stage join by snapshot + replay
// (READY, JOIN_REQ / JOIN_AT, the JOIN event, then sealed ticks with
// identical net fingerprints), an exit fork (REASSIGN, LEAD_FROM), a
// co-arrival (one entry for both, sealed from tick 0), a reconnect with
// RESUME, the barrier timeout of a solo group, the 1 -> 2 pads and the room
// story (STORY / STORY_ACK, the barrier merge, the dropped set).
#include "online_fake_hub.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <iostream>
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

struct World {
    fake::Hub hub;
    std::vector<std::unique_ptr<fake::Client>> clients;
    double now = 0;
    static constexpr double kPass = 1.0 / 240.0;

    fake::Client& add(const std::string& name, std::uint32_t id) {
        auto c = std::make_unique<fake::Client>();
        c->name = name;
        SessionConfig config;
        config.ticket = "t-" + name;
        config.protocol = 44;
        config.data_hash = 1;
        config.settings_hash = 2;
        config.own_name = name;
        config.replay_budget = 0.004;
        config.trace = std::getenv("DW4_SESSION_TEST_TRACE") != nullptr; // (debugging a test: the client trace on stdout)
        c->session = std::make_unique<Session>(c->engine, config);
        c->engine.take_load_snapshot();
        c->session->attach(hub.connect(id, name), now);
        clients.push_back(std::move(c));
        return *clients.back();
    }
    void run(double seconds) {
        const double end = now + seconds;
        while (now < end) {
            now += kPass;
            hub.tick(now);
            for (auto& c : clients) c->frame(now, kPass);
        }
    }
    template <class F> bool run_until(double limit, F&& done) {
        const double end = now + limit;
        while (now < end) {
            run(0.05);
            if (done()) return true;
        }
        describe();
        return false;
    }
    void describe() const {
        for (const auto& line : hub.log_lines) std::cout << "  hub: " << line << '\n';
        for (const auto& c : clients)
            std::printf("  %s: mode %s stage %s inst %u visit %u tick %u slot %d present %x steps %d restores %d replayed %d\n", c->name.c_str(),
                        mode_name(c->session->mode()), c->engine.stage().c_str(), c->session->inst(), c->session->visit(), c->session->tick(),
                        c->session->own_slot(), c->session->present(), c->steps, c->engine.restores, c->engine.replayed);
        for (const auto& c : clients)
            if (c->session->closed()) std::printf("  %s closed: %s\n", c->name.c_str(), c->session->close_reason().c_str());
    }
};

// Press `button` for one tick at `tick` of the visit in `stage`.
std::function<std::uint32_t(const fake::Client&)> press_in(std::map<std::string, std::uint32_t> at, std::uint32_t button) {
    return [at, button](const fake::Client& c) -> std::uint32_t {
        const auto it = at.find(c.engine.stage());
        return it != at.end() && c.session->tick() == it->second ? button : 0u;
    };
}

void print_log(const fake::Hub& hub) {
    for (const auto& line : hub.log_lines) std::cout << "  hub: " << line << '\n';
}

void solo_join_fork() {
    World world;
    world.hub.co_arrival_hold = 0.5;
    world.hub.suggested_delay = 4; // 20 ms one way: ceil((40 ms + one playout tick + a host pass) / 16.7 ms)
    auto& a = world.add("kim", 1);
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A reaches stag0101 solo");
    check(a.session->inst() >= 100 && !a.session->hub(), "a registered field instance");
    world.run(8);
    const auto inst_a = a.session->inst();
    auto& hub_inst = world.hub.instances[inst_a];
    check(hub_inst.log.size() + 20 > a.session->tick() && hub_inst.log.size() > 400, "the server has A's log (LOG every 100 ms)");
    check(!hub_inst.snapshot.empty(), "A uploaded its load snapshot");

    auto& b = world.add("lee", 2);
    b.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(20, [&] { return b.session->mode() == Mode::sealed && a.session->mode() == Mode::sealed; }),
          "B joins A's stag0101 by catch-up; both sealed");
    world.run(0.5); // both past J
    print_log(world.hub);
    check(b.engine.restores >= 1 && b.engine.replayed > 400, "B replayed A's log as an observer");
    check(b.engine.audio_steps == std::uint64_t(b.engine.replayed), "the replay kept the music position");
    check(b.session->own_slot() == 1 && a.session->own_slot() == 0, "slots");
    check(a.session->present() == 0b11 && b.session->present() == 0b11, "both present (" + std::to_string(a.session->present()) + ", " + std::to_string(b.session->present()) + ")");
    check(world.hub.netfp_mismatches == 0 && world.hub.netfp_compared >= 1, "READY's fingerprint equals A's");
    const auto compared = world.hub.netfp_compared;
    // The joiner's first pads come late by design (the server fills them).
    world.run(1);
    const auto late_after_join = world.hub.late_pads;
    world.run(30);
    check(world.hub.netfp_mismatches == 0, "identical net fingerprints every 60 ticks while sealed");
    check(world.hub.netfp_compared >= compared + 25, "about one comparison per second");
    check(a.engine.net_fingerprint() != 0 && a.session->tick() > 1500, "A played on");
    check(world.hub.late_pads == late_after_join, "with the delay from the round trip plus the playout tick no pad is late (" +
                                                      std::to_string(world.hub.late_pads - late_after_join) + ")");
    const auto notices_a = a.session->take_notices();
    bool joined = false;
    for (const auto& n : notices_a) joined |= n.find("joined") != std::string::npos;
    check(joined, "A is told about the joiner");
    // B forks away (an exit-type fork): both sides go on alone.
    // The fork executes on a multiple of 60 (a pad sampled at tick t runs at
    // t + d): the two sides' net fingerprints differ from the fork tick on,
    // so neither may report that tick's as the instance's (S2 on real game
    // clients: the relay resynced the stayer).
    const std::uint32_t fork_at = (b.session->tick() / 60 + 2) * 60 - std::uint32_t(b.session->delay());
    b.input = [fork_at](const fake::Client& c) -> std::uint32_t { return c.session->tick() == fork_at ? fake::kForkButton : 0u; };
    const auto mismatches_before_fork = world.hub.netfp_mismatches;
    check(world.run_until(10, [&] { return a.session->mode() == Mode::solo && b.session->mode() == Mode::solo; }),
          "both solo after the fork (LEAD_FROM)");
    check(world.hub.netfp_mismatches == mismatches_before_fork, "no fingerprint vote at the fork tick (" +
                                                                    std::to_string(world.hub.netfp_mismatches - mismatches_before_fork) + ")");
    world.run(0.2); // the SPLIT frames reach the hub
    check(world.hub.splits == 2, "both members reported the SPLIT (" + std::to_string(world.hub.splits) + ")");
    check(world.hub.reassigns == 1 && b.session->inst() != a.session->inst(), "B continues under a new instance id (REASSIGN)");
    check(a.session->present() == 0b01 && b.session->present() == 0b10, "each side despawned the other");
    world.run(3);
    check(a.session->mode() == Mode::solo && b.session->mode() == Mode::solo, "still solo");
    // Two instances of one stage: each sees the other as a remote name tag.
    const auto tags = a.session->remote_tags(world.now);
    check(tags.size() == 1 && tags[0].name == "lee" && tags[0].stage == "stag0101" && tags[0].level == 3, "A sees B's tag from PRESENCE");
}

void presence_tracks() {
    PresenceTracks tracks;
    w::Presence p;
    p.member = 5;
    p.stage = "stagml11";
    p.inst = 1;
    p.level = 7;
    p.hp = p.max_hp = 40;
    for (int i = 0; i <= 5; ++i) {
        p.x = float(i * 10);
        tracks.add(p, "kim", i * 0.2);
    }
    // At t = 1.1 the tag is drawn at t = 0.9: between the packets of 0.8 and 1.0.
    auto s = tracks.sample(1.1);
    check(s.size() == 1 && std::abs(s[0].x - 45.0f) < 0.01f && s[0].name == "kim" && s[0].level == 7, "interpolated one interval behind");
    s = tracks.sample(1.25);
    check(s.size() == 1 && std::abs(s[0].x - 50.0f) < 0.01f, "holds the last packet after it");
    check(tracks.sample(3.1).empty(), "stale after two seconds");
    p.stage = "stag0101";
    p.x = 500;
    tracks.add(p, "kim", 1.3);
    s = tracks.sample(1.4);
    check(s.size() == 1 && s[0].x == 500.0f && s[0].stage == "stag0101", "a stage change does not glide");
    p.stage.clear();
    tracks.add(p, "kim", 1.5);
    check(tracks.sample(1.6).empty(), "hidden (an empty stage): no tag");
    p.stage = "stag0101";
    p.hp = 0;
    tracks.add(p, "kim", 1.7);
    check(tracks.sample(1.8).empty(), "defeated (hp 0): no tag");
}

// PRESENCE2 (round 60): the sender's clock spaces the packets, a held-up
// packet does not shift the others, jumps never glide, a short way past the
// newest packet, the clip at the drawn time, busy and the weapon.
void presence2_tracks() {
    PresenceTracks tracks;
    w::Presence p;
    p.member = 2;
    p.stage = "stagml11";
    p.inst = 1;
    p.level = 7;
    p.hp = p.max_hp = 40;
    p.version = w::kPresenceVersion;
    p.flags = w::Presence::loop;
    p.weapon = "w_test01";
    // Ten a second, x 10 per packet (100 units per second); the network
    // delays them 50 ms, the fourth 130 ms (jitter). The clock offset is
    // 15.05 s (the smallest delay); drawn 0.16 s behind on the sender's clock.
    for (int i = 0; i <= 8; ++i) {
        p.x = float(i * 10);
        p.sent_ms = std::uint32_t(5000 + i * 100);
        p.clip = i < 5 ? "waitD" : "runD";
        tracks.add(p, "kim", 20.0 + i * 0.1 + (i == 3 ? 0.13 : 0.05));
    }
    auto s = tracks.sample(20.9); // drawn time 5.69
    check(s.size() == 1 && std::abs(s[0].x - 69.0f) < 0.05f, "drawn on the sender's clock (" + std::to_string(s.empty() ? -1 : s[0].x) + ")");
    check(s[0].v2 && s[0].clip == "runD" && s[0].loop && s[0].weapon == "w_test01" && !s[0].busy, "the clip at the drawn time, the weapon");
    check(std::abs(s[0].speed - 100.0f) < 0.5f, "the speed between the packets");
    s = tracks.sample(20.8); // 5.59
    check(std::abs(s[0].x - 59.0f) < 0.05f && s[0].clip == "runD", "evenly spaced through the late packet");
    s = tracks.sample(20.66); // 5.45
    check(std::abs(s[0].x - 45.0f) < 0.05f && s[0].clip == "waitD", "the clip of the packet before the drawn time");
    // Past the newest packet: on for at most kExtrapolate, then it holds.
    s = tracks.sample(21.2); // 5.99
    check(std::abs(s[0].x - (80.0f + 100.0f * float(PresenceTracks::kExtrapolate))) < 0.05f, "a short way past the newest packet, then it holds");
    // A warp: the next packet's warp counter changed, far away. No glide.
    p.warp = 1;
    p.x = 500;
    p.sent_ms = 5900;
    tracks.add(p, "kim", 20.95);
    const auto before = tracks.sample(21.0); // 5.79
    check(std::abs(before[0].x - 79.0f) < 0.05f, "before the warp's packet time: the old way");
    const auto after = tracks.sample(21.06); // 5.85
    check(after[0].x == 80.0f && after[0].segment == before[0].segment, "between the last old packet and the warp: it holds there");
    const auto warped = tracks.sample(21.12); // 5.91
    check(warped[0].x == 500.0f && warped[0].segment != before[0].segment, "at the warp's time: the new place, a new segment");
    // A step faster than any walk without a warp counter change snaps too.
    p.x = 900;
    p.sent_ms = 6000;
    tracks.add(p, "kim", 21.05);
    check(tracks.sample(21.3)[0].segment != warped[0].segment, "a jump of 400 units in 0.1 s is a new segment");
    // Busy: the tag hides, the figure stays.
    p.flags = w::Presence::busy;
    p.sent_ms = 6100;
    tracks.add(p, "kim", 21.15);
    s = tracks.sample(21.4);
    check(s.size() == 1 && s[0].busy && !s[0].loop, "busy and the clip's loop flag come along");
    // A restarted sender clock starts the track over.
    p.sent_ms = 10;
    p.x = 0;
    tracks.add(p, "kim", 21.3);
    s = tracks.sample(21.35);
    check(s.size() == 1 && s[0].x == 0.0f, "a sender clock that went back starts over");
    // Yaw goes the short way round.
    PresenceTracks turn;
    p.flags = 0;
    p.yaw = 3.0f;
    p.sent_ms = 100;
    turn.add(p, "kim", 1.0);
    p.yaw = -3.0f;
    p.sent_ms = 200;
    turn.add(p, "kim", 1.1);
    const float yaw = turn.sample(1.21)[0].yaw; // halfway
    check(std::abs(yaw) > 3.0f, "yaw turns through pi, not through 0 (" + std::to_string(yaw) + ")");
}

// PRESENCE2 end to end over the fake hub: only after a WELCOME that
// announces it; else PRESENCE as before (an older hub).
void presence2_session() {
    for (const bool v2 : {false, true}) {
        World world;
        world.hub.presence_v2 = v2;
        auto& a = world.add("kim", 1);
        auto& b = world.add("lee", 2);
        b.engine.figure_clip = "runD";
        world.run(3);
        check(a.session->presence_v2() == v2 && b.session->presence_v2() == v2, "the WELCOME flag decides");
        check(world.hub.unknown_frames == 0, "no PRESENCE2 to a hub that does not take it");
        if (v2) check(world.hub.presence2_frames > 10 && world.hub.presence_frames == 0, "PRESENCE2 only");
        else check(world.hub.presence_frames > 10 && world.hub.presence2_frames == 0, "PRESENCE only");
        const auto tags = a.session->remote_tags(world.now);
        check(tags.size() == 1 && tags[0].name == "lee", "A has B's sample");
        check(tags[0].v2 == v2 && tags[0].clip == (v2 ? "runD" : "") && tags[0].weapon == (v2 ? "w_test01" : ""),
              "PRESENCE2 brings the clip and the weapon");
    }
    // Ten a second while the figure changes, five while it stands.
    World world;
    world.hub.presence_v2 = true;
    auto& a = world.add("kim", 1);
    a.engine.figure_x = 5.0f;
    world.run(2);
    const auto standing = world.hub.presence2_frames;
    world.run(4);
    const auto still = world.hub.presence2_frames - standing;
    check(still >= 19 && still <= 21, "five a second while nothing changes (" + std::to_string(still) + ")");
    a.engine.figure_x.reset();
    const auto moving_from = world.hub.presence2_frames;
    world.run(4);
    const auto moving = world.hub.presence2_frames - moving_from;
    check(moving >= 36 && moving <= 41, "ten a second while it moves (" + std::to_string(moving) + ")");
}

// How evenly a figure walks: the steps of the samples drawn at `rate`
// pictures a second against the even step (speed / rate): the mean, the
// standard deviation and the largest difference from 1 of their ratios.
struct Evenness {
    double mean{}, sd{}, worst{};
    std::size_t frames{};
};
Evenness evenness(const std::vector<double>& xs, double speed, double rate) {
    Evenness e;
    double sum = 0, squares = 0;
    for (std::size_t i = 1; i < xs.size(); ++i) {
        const double r = (xs[i] - xs[i - 1]) / (speed / rate);
        sum += r;
        squares += r * r;
        e.worst = std::max(e.worst, std::abs(r - 1.0));
        ++e.frames;
    }
    if (e.frames == 0) return e;
    e.mean = sum / double(e.frames);
    e.sd = std::sqrt(std::max(0.0, squares / double(e.frames) - e.mean * e.mean));
    return e;
}
std::string describe(const Evenness& e) {
    char text[160];
    std::snprintf(text, sizeof text, "%zu frames, mean %.3f, sd %.3f, worst %.3f", e.frames, e.mean, e.sd, e.worst);
    return text;
}

// The review of round 60 measured the walk of a ghost figure per picture:
// the clock offset rose 2 ms a packet and dropped back at every new smallest
// one (a saw: 8.5 % standard deviation, single pictures 1.85x, with 30+-20 ms
// of jitter 3.65x), and a sender at 144 Hz stamped its poll time (+-13 %).
// Now the offset moves at most kOffsetSlew towards the smallest lag of the
// last packets and the packets carry the time of their step: a walk at a
// steady speed moves the same way every picture, for any sender and
// receiver picture rate and with the network's jitter (the relay is TCP:
// a held-up packet holds up the ones after it).
void presence2_smooth() {
    struct Case {
        double sender_hz, present_hz, delay, jitter;
    };
    for (const Case c : {Case{60, 60, 0.0005, 0}, Case{60, 144, 0.0005, 0}, Case{144, 60, 0.0005, 0}, Case{144, 144, 0.0005, 0},
                         Case{60, 60, 0.03, 0.01}, Case{60, 144, 0.03, 0.02}, Case{144, 144, 0.03, 0.04}, Case{144, 60, 0.03, 0.04},
                         Case{30, 144, 0.08, 0.04}}) {
        std::uint32_t seed = 12345;
        auto uniform = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return double(seed >> 8) / double(1u << 24);
        };
        const double speed = 42.0; // units per second (a run in the lobby)
        struct Packet {
            double arrives;
            w::Presence p;
        };
        std::vector<Packet> packets;
        // The sender: a pass every 1 / sender_hz (with a little scheduling
        // noise); the poll before the pass's steps sends the place of the
        // last step, stamped with that step's time (Session::track_figure).
        double presence_at = -100, accumulator = 0, last_pass = 0, arrives = 0;
        int executed = -1;
        for (int j = 0;; ++j) {
            const double pass = j / c.sender_hz + uniform() * 0.0005;
            if (pass > 30.0) break;
            if (pass - presence_at >= 0.1 && executed >= 0) {
                w::Presence p;
                p.member = 1;
                p.stage = "stagml11";
                p.inst = 1;
                p.hp = p.max_hp = 10;
                p.version = w::kPresenceVersion;
                p.x = float(speed * executed * kFieldSeconds);
                p.sent_ms = std::uint32_t(std::llround(executed * kFieldSeconds * 1000.0)) + 1000000u;
                presence_at = pass;
                arrives = std::max(arrives, pass + c.delay + uniform() * c.jitter);
                packets.push_back({arrives, p});
            }
            accumulator += pass - last_pass;
            last_pass = pass;
            while (accumulator >= kFieldSeconds) {
                accumulator -= kFieldSeconds;
                ++executed;
            }
        }
        // The receiver: the packets that came by a picture's pass, then the
        // sample of the picture (online_now, a little later).
        PresenceTracks tracks;
        std::size_t next = 0;
        std::vector<double> xs;
        for (int i = 0;; ++i) {
            const double q = i / c.present_hz;
            if (q > 30.0) break;
            while (next < packets.size() && packets[next].arrives <= q) tracks.add(packets[next++].p, "kim", q);
            const auto s = tracks.sample(q + 0.002);
            if (q >= 3.0) xs.push_back(s.empty() ? -1.0 : s[0].x);
        }
        const auto e = evenness(xs, speed, c.present_hz);
        char name[96];
        std::snprintf(name, sizeof name, "sender %.0f Hz, pictures %.0f Hz, %.0f+%.0f ms: ", c.sender_hz, c.present_hz, c.delay * 1000,
                      c.jitter * 1000);
        check(std::find(xs.begin(), xs.end(), -1.0) == xs.end(), std::string(name) + "a sample every picture");
        check(e.sd < 0.015 && e.worst < 0.06, std::string(name) + "an even walk (" + describe(e) + ")");
    }
    // The route gets faster by 60 ms (the packets come that much earlier
    // from the twentieth on) or slower: the drawn time moves over at most
    // kOffsetSlew faster or slower, it never jumps.
    for (const double change : {-0.06, 0.06}) {
        PresenceTracks tracks;
        w::Presence p;
        p.member = 1;
        p.stage = "stagml11";
        p.inst = 1;
        p.hp = p.max_hp = 10;
        p.version = w::kPresenceVersion;
        int k = 0;
        std::vector<double> xs;
        for (int i = 0; i < 144 * 8; ++i) {
            const double q = i / 144.0;
            while (k * 0.1 + 0.09 + (k >= 20 ? change : 0.0) <= q) {
                p.x = float(k * 4.2);
                p.sent_ms = std::uint32_t(1000 + k * 100);
                tracks.add(p, "kim", q);
                ++k;
            }
            const auto s = tracks.sample(q + 0.002);
            if (q > 1.0) xs.push_back(s.empty() ? -1.0 : s[0].x);
        }
        const auto e = evenness(xs, 42.0, 144.0);
        check(std::find(xs.begin(), xs.end(), -1.0) == xs.end() && e.worst < 0.05,
              std::string(change < 0 ? "a faster" : "a slower") + " route: no jump (" + describe(e) + ")");
    }
}

// The same over the fake hub: PRESENCE2's clock is the time of the step
// whose place it carries (the polls come at any time between the steps),
// and the walk A draws of B is even at every pass.
void presence2_walk() {
    World world;
    world.hub.presence_v2 = true;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    b.engine.figure_walk = 0.7f; // units a step: 42 a second
    world.run(3);
    const double speed = 0.7 / kFieldSeconds;
    const w::Presence* before = nullptr;
    double worst = 0;
    int pairs = 0;
    for (const auto& p : world.hub.presence2_seen) {
        if (p.member != 2 || p.stage.empty()) continue;
        if (before && p.x > before->x && p.warp == before->warp) {
            const double seconds = double(std::int32_t(p.sent_ms - before->sent_ms)) / 1000.0;
            worst = std::max(worst, std::abs((p.x - before->x) / seconds / speed - 1.0));
            ++pairs;
        }
        before = &p;
    }
    // (sent_ms has whole milliseconds: up to 1 % of 100 ms.)
    check(pairs > 20 && worst < 0.015, "PRESENCE2's clock is the step's (" + std::to_string(pairs) + " pairs, worst " + std::to_string(worst) + ")");
    std::vector<double> xs;
    for (int i = 0; i < 480; ++i) {
        world.run(World::kPass * 0.999);
        const auto tags = a.session->remote_tags(world.now);
        if (tags.size() == 1) xs.push_back(tags[0].x);
    }
    const auto e = evenness(xs, speed, 1.0 / World::kPass);
    check(xs.size() == 480 && e.sd < 0.02 && e.worst < 0.08, "A draws B's walk evenly at every pass (" + describe(e) + ")");
}

// Leaving a hub stage (the round 60 review): B's figure faded out at the mat
// (its scene), came back there about three seconds later when B answered
// the barrier and stood two seconds until its last packet turned stale. A
// departure (HEADING) or a barrier now hides B at once until its next stage
// load.
void presence2_departure() {
    World world;
    world.hub.presence_v2 = true;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    b.engine.figure_x = 30.0f;
    world.run(2);
    auto sees = [&](const char* stage) {
        for (const auto& t : a.session->remote_tags(world.now))
            if (t.name == "lee" && (!stage || t.stage == stage)) return true;
        return false;
    };
    const std::string first = b.engine.stage();
    check(sees(nullptr), "A sees B");
    // The mat's scene: B's model is not shown.
    b.engine.figure_hidden = true;
    world.run(0.5);
    check(!sees(nullptr), "B is hidden while its model is not shown");
    // Its NEXTSTAGE is due (HEADING); the scene ends, B's model shows on its
    // own screen again until the stage changes: not on A's.
    b.engine.heading = HeadingInfo{"stag0101", 1};
    world.run(0.1);
    b.engine.figure_hidden = false;
    bool seen = false;
    for (int i = 0; i < 300; ++i) {
        world.run(0.01);
        seen |= sees(nullptr);
    }
    check(!seen, "after a departure B stays hidden on A's screen");
    // NEXTSTAGE and a slow load: still nothing, then B in its next stage.
    b.engine.transition_steps = 120;
    bool exit = true;
    b.input = [&exit](const fake::Client& c) -> std::uint32_t { return exit && c.engine.frame_ >= 20 ? fake::kExitButton : 0u; };
    check(world.run_until(5, [&] { return b.engine.pending_.has_value(); }), "B takes the exit");
    exit = false;
    for (int i = 0; i < 100; ++i) {
        world.run(0.01);
        seen |= sees(nullptr);
    }
    check(!seen, "nothing while B loads");
    check(world.run_until(10, [&] { return b.engine.stage() != first && sees(b.engine.stage().c_str()); }), "B shows in its next stage");

    // A barrier without a departure (an exit without a scene): B goes from
    // A's screen at once, not when its last packet turns stale.
    World world2;
    world2.hub.presence_v2 = true;
    auto& c = world2.add("kim", 1);
    auto& d = world2.add("lee", 2);
    d.engine.figure_x = 30.0f;
    d.engine.transition_steps = 600;
    world2.run(2);
    auto c_sees_d = [&] { return !c.session->remote_tags(world2.now).empty(); };
    check(c_sees_d(), "C sees D");
    bool take = true;
    d.input = [&take](const fake::Client& client) -> std::uint32_t { return take && client.engine.frame_ >= 20 ? fake::kExitButton : 0u; };
    check(world2.run_until(5, [&] { return d.engine.pending_.has_value(); }), "D takes the exit");
    take = false;
    const double parked = world2.now;
    check(world2.run_until(3, [&] { return !c_sees_d(); }) && world2.now - parked < 0.2,
          "D goes from C's screen at once (" + std::to_string(world2.now - parked) + " s)");

    // HP above the maximum (MDLaddHp allows it): shown full, never dropped.
    d.engine.figure_hp = 80;
    check(world2.run_until(15, [&] {
              const auto tags = c.session->remote_tags(world2.now);
              return tags.size() == 1 && tags[0].hp == 50 && tags[0].max_hp == 50;
          }),
          "HP above the maximum goes as the maximum");
}

void co_arrival() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    a.input = press_in({{"stagis10", 20}, {"stagml11", 200}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 260}}, fake::kExitButton); // one second later
    check(world.run_until(20, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; }),
          "a co-arrival: both start stag0101 sealed");
    print_log(world.hub);
    check(a.engine.stage() == "stag0101" && b.engine.stage() == "stag0101", "same stage");
    check(b.engine.restores == 1 && b.engine.replayed == 0, "B restored the base's load snapshot, nothing to replay");
    check(a.session->inst() == b.session->inst(), "one instance");
    check(world.hub.snaphash_mismatches == 0, "the canonical snapshot hashes agree");
    world.run(20);
    check(world.hub.netfp_mismatches == 0 && world.hub.netfp_compared > 15, "identical fingerprints");

    // A reconnect: B's connection drops for two seconds.
    world.hub.drop(2);
    world.run(0.5);
    check(b.session->mode() == Mode::lost && b.session->overlay(world.now).connection_lost, "B freezes with the overlay");
    const auto b_tick = b.session->tick();
    world.run(1.5);
    check(b.session->tick() == b_tick, "a sealed member runs no unsealed ticks");
    check(b.session->wants_transport(), "B asks for a new connection");
    b.session->attach(world.hub.connect(2, "lee"), world.now, "t-lee-2");
    check(world.run_until(5, [&] { return b.session->mode() == Mode::sealed && b.session->tick() + 4 >= a.session->tick(); }),
          "B resumes and catches up");
    world.run(10);
    check(world.hub.netfp_mismatches == 0, "identical fingerprints after the reconnect");
    check(world.hub.fills_total > 60, "the server filled B's pads meanwhile");
}

// Round I1a's integration (W7d): a member cut for 45 s resumes into its sealed
// group and runs the ~2,700 ticks it missed at once. The pads it samples for
// ticks the relay sealed meanwhile go together, up to 32 in one INPUT (before:
// one frame each, which broke the relay's frame budget of 400 a second with a
// burst of 2,000 and closed the resumed member as abuse).
void long_cut_resume_pads() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    a.input = press_in({{"stagis10", 20}, {"stagml11", 200}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 260}}, fake::kExitButton);
    check(world.run_until(20, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; }), "both sealed");
    world.run(2.0);
    world.hub.drop(2);
    world.run(45.0);
    check(b.session->mode() == Mode::lost && b.session->wants_transport(), "B waits for a new connection");
    const auto frames_before = world.hub.input_frames[2], pads_before = world.hub.input_pads[2];
    b.session->attach(world.hub.connect(2, "lee"), world.now, "t-lee-2");
    check(world.run_until(10, [&] { return b.session->mode() == Mode::sealed && b.session->tick() + 8 >= a.session->tick(); }),
          "B resumes and catches up 45 s");
    world.run(1.0); // (the frames reach the hub after its latency)
    const auto frames = world.hub.input_frames[2] - frames_before, pads = world.hub.input_pads[2] - pads_before;
    check(pads >= 2500, "B's pads of the missed ticks went (" + std::to_string(pads) + ")");
    check(frames * 8 < pads, "in few INPUT frames (" + std::to_string(frames) + " frames for " + std::to_string(pads) + " pads)");
    world.run(5.0);
    check(b.session->mode() == Mode::sealed && world.hub.netfp_mismatches == 0, "B plays on sealed with identical fingerprints");
}

// The target leaves the stage while the joiner still waits for its state:
// the joiner restores its own load snapshot and plays the stage as NEW.
void target_moves_on() {
    World world;
    world.hub.co_arrival_hold = 0.5;
    world.hub.suggested_delay = 4;
    auto& a = world.add("kim", 1);
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A in stag0101");
    world.run(4);
    world.hub.state_delay = 3.0; // the state transfer is slow
    auto& b = world.add("lee", 2);
    b.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return b.session->mode() == Mode::waiting_state; }), "B waits for A's state");
    const auto a_tick = a.session->tick();
    a.input = [a_tick](const fake::Client& c) -> std::uint32_t {
        return c.engine.stage() == "stag0101" && c.session->tick() == a_tick + 30 ? fake::kExitButton : 0u;
    };
    check(world.run_until(10, [&] { return b.session->mode() == Mode::solo && a.engine.stage() == "stag0102"; }),
          "B falls back to its own stag0101 when A leaves");
    print_log(world.hub);
    check(b.engine.stage() == "stag0101" && b.session->own_slot() == 0 && b.session->present() == 1, "B alone in slot 0 of its own load");
    check(b.engine.restores >= 1 && b.session->inst() != 0, "restored from the fallback, a registered instance");
    world.run(2);
    check(!world.hub.instances[b.session->inst()].snapshot.empty(), "B uploaded the fallback as its instance's snapshot");
    bool notice = false;
    for (const auto& n : b.session->take_notices()) notice |= n.find("on your own") != std::string::npos;
    check(notice, "B is told why");
}

// The relay sends a joiner the target's state and LIVE ticks at once, while
// the joiner still runs its own transition steps (a real stage load: about
// 750 steps, 12 s): the LIVE ticks belong to the new visit and must be kept
// although the joiner's tick count still runs on the old one (S17b on real
// game clients waited for good at the first tick after the state's log).
void live_during_transition() {
    World world;
    world.hub.co_arrival_hold = 0.5;
    world.hub.suggested_delay = 4;
    // Both start together; B stays in its lobby longer than A has been in the
    // field, so B's tick count is past A's when the JOIN comes.
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    a.input = press_in({{"stagis10", 30}, {"stagml11", 700}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 30}, {"stagml11", 2400}}, fake::kExitButton);
    b.engine.transition_steps = 300;
    world.hub.state_pacing = 0.3;
    check(world.run_until(80, [&] { return b.session->mode() == Mode::transition && b.engine.stage() == "stagml11"; }),
          "B in its transition steps after the JOIN");
    check(b.session->tick() > a.session->tick(), "B's tick count (the lobby's) is past A's (" + std::to_string(b.session->tick()) + " / " +
                                                     std::to_string(a.session->tick()) + ")");
    check(world.run_until(20, [&] { return b.session->mode() == Mode::sealed && a.session->mode() == Mode::sealed; }),
          "B catches up with the LIVE ticks that came during its transition and joins");
    world.run(5);
    check(world.hub.netfp_mismatches == 0 && world.hub.netfp_compared >= 1, "identical net fingerprints");
}

void barrier_timeout() {
    World world;
    world.hub.answer_barriers = false;
    auto& a = world.add("kim", 1);
    a.input = press_in({{"stagis10", 30}}, fake::kExitButton);
    check(world.run_until(3, [&] { return a.session->mode() == Mode::parked; }), "parked after the barrier tick");
    const auto steps = a.steps;
    world.run(2);
    check(a.steps == steps && a.session->mode() == Mode::parked, "nothing runs while parked");
    check(world.run_until(5, [&] { return a.session->mode() == Mode::solo && a.engine.stage() == "stagml11"; }),
          "a solo group goes on as NEW after five seconds");
    check(a.session->inst() == 0, "unregistered");
}

// A held barrier (BARRIER_WAIT every second, as the relay's join hold) is an
// answer: the solo group keeps waiting past the five-second timeout and takes
// the server's decision.
void barrier_hold() {
    World world;
    world.hub.wait_hold = 9.0;
    auto& a = world.add("kim", 1);
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.session->mode() == Mode::parked && a.engine.stage() == "stagml11"; }), "parked at the field barrier");
    world.run(7);
    check(a.session->mode() == Mode::parked, "still parked after seven seconds: the server holds the barrier");
    check(a.session->overlay(world.now).loading.find("Joining") != std::string::npos, "the loading line names the hold");
    check(world.run_until(5, [&] { return a.session->mode() == Mode::solo && a.engine.stage() == "stag0101"; }), "the server's NEW after the hold");
    check(a.session->inst() >= 100, "a registered instance, not the timeout's");
}

// The room story through the client (SEAMLESS.md 8.3-8.6): the confirmed
// story comes after WELCOME; nothing goes from the title (its character
// select holds a record's own story); a flag and a key the guest's engine
// makes in its lobby reach the room story within about a second (STORY), the
// other member's confirmed story follows (STORY_ACK on the revision) but its
// engine gets them only at its next barrier; an additive counter both engines
// of a shared instance grow counts once (one engine line); with the room
// owner away (drop) the guest's new flag is refused and the next barrier
// removes it from the guest's engine with the notice.
void room_story() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6; // (no pad comes late with 20 ms one way: the exit press is not filled away)
    world.hub.room_story.flags[392] = 1;
    world.hub.room_story.revision = 5;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    a.input = press_in({{"stagis10", 20}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 600}}, fake::kExitButton);
    b.engine.story.flags[426] = 1; // (the character select's own record)
    check(world.run_until(10, [&] { return a.engine.stage() == "stagml11" && a.session->mode() == Mode::solo; }), "A in its lobby");
    check(a.session->confirmed_story() && a.session->confirmed_story()->revision == 5 && a.session->confirmed_story()->flags[392] == 1,
          "the confirmed story after WELCOME");
    world.run(3);
    check(b.engine.stage() == "stagis10" && world.hub.stories == 0, "no STORY from the title");
    // Round G-core: the Game Start's hold (OnlineRun holds the title until the
    // confirmed story came): no live step, the loading line, and B's title
    // goes on once it is released.
    b.session->hold_for_story(true);
    check(!b.session->step_ready() && b.session->overlay(world.now).loading == "Loading your story...", "the hold stops the steps");
    const auto held_tick = b.session->tick();
    world.run(1);
    check(b.session->tick() == held_tick && world.hub.stories == 0, "no step and no STORY while held");
    b.session->hold_for_story(false);
    check(b.session->step_ready(), "released"); 
    check(world.run_until(10, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo; }), "B in its lobby");
    check(world.run_until(3, [&] { return world.hub.room_story.flags[426] == 1; }), "B's value goes from the lobby");
    b.engine.story.flags[19] = 1;  // a persistent flag (a talk)
    b.engine.story.flags[23] = 1;  // a temporary: never sent
    b.engine.story.folder[2] = 1;  // a key item
    const double t0 = world.now;
    check(world.run_until(5, [&] { return world.hub.room_story.flags[19] && world.hub.room_story.folder[2]; }), "B's progress reaches the room story");
    check(world.now - t0 < 1.6, "within about a second (" + std::to_string(world.now - t0) + " s)");
    check(!world.hub.room_story.flags[23], "temporaries are never sent");
    check(world.hub.room_story.flags[706] && world.hub.room_story.flags[520], "the normal copies follow the working set and the keys");
    check(world.run_until(2, [&] { return a.session->confirmed_story()->revision == world.hub.room_story.revision; }),
          "A's confirmed story follows (STORY_ACK on the revision)");
    check(!a.engine.story.flags.count(19), "A's engine does not get it mid-stage");
    const auto stories = world.hub.stories;
    world.run(12);
    check(world.hub.stories == stories, "an empty pending set sends nothing (" + std::to_string(world.hub.stories - stories) + ")");
    // The server lets a value wait (B's visit is too young, 8.7): it goes
    // again within a few seconds, not ten seconds later or at the barrier.
    world.hub.story_wait = 1;
    b.engine.story.flags[394] = 1;
    const double t_wait = world.now;
    check(world.run_until(8, [&] { return world.hub.room_story.flags[394] == 1; }), "a value the server let wait goes again");
    check(world.now - t_wait < 5.0, "within a few seconds (" + std::to_string(world.now - t_wait) + " s)");
    // Round G-core: a queued value (earned, the server merges it when due) is
    // not sent again before the next barrier, and nobody is told.
    world.hub.story_queued = 1;
    (void)b.session->take_notices();
    b.engine.story.flags[396] = 1;
    check(world.run_until(3, [&] { return world.hub.story_queued == 0; }), "the value goes once and is queued");
    const auto stories_queued = world.hub.stories;
    world.run(12);
    check(world.hub.stories == stories_queued, "a queued value is not sent again (" + std::to_string(world.hub.stories - stories_queued) + ")");
    check(b.session->take_notices().empty(), "a queued value needs no notice");
    check(!world.hub.room_story.flags[396], "the fake hub never merged it (the real one does when due)");
    // Both take the gate to stag0101 half a second apart: a co-arrival, the
    // load step merges the room story into both engines.
    const auto ta = a.session->tick(), tb = b.session->tick();
    a.input = [ta](const fake::Client& c) -> std::uint32_t { return c.engine.stage() == "stagml11" && c.session->tick() == ta + 60 ? fake::kExitButton : 0u; };
    b.input = [tb](const fake::Client& c) -> std::uint32_t { return c.engine.stage() == "stagml11" && c.session->tick() == tb + 90 ? fake::kExitButton : 0u; };
    check(world.run_until(20, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; }), "both sealed in stag0101");
    check(a.engine.story.flags.count(19) && a.engine.story.flags.at(19) == 1 && a.engine.story.folder.at(2) == 1 && a.engine.story.flags.at(392) == 1,
          "A's load merged the room story");
    // stag0001's attempts: both engines of the instance grow COUNTER 7 alike.
    a.engine.story.counters[7] = 1;
    b.engine.story.counters[7] = 1;
    check(world.run_until(4, [&] { return world.hub.room_story.counters[7] == 1; }), "the counter's growth reaches the room story");
    world.run(3);
    check(world.hub.room_story.counters[7] == 1 && world.hub.story_lines.size() == 1, "one engine line, counted once");
    // (Round I1a, 19.8.5: the owner-away policy and its dropped sets are
    // retired; the host is in his instance or in his grace.)
    (void)a.session->take_notices();
    (void)b.session->take_notices();
    // B's STORY with new growth is lost with its dying connection (never
    // answered): after the reconnect the pending set goes again at once, not
    // ten seconds after the lost one.
    world.hub.story_lost = true;
    const auto lost_before = world.hub.stories;
    b.engine.story.counters[7] = 3;
    check(world.run_until(3, [&] { return world.hub.stories > lost_before; }), "B sends the growth");
    world.hub.drop(2);
    world.hub.story_lost = false;
    world.run(1.0);
    check(b.session->wants_transport(), "B asks for a new connection");
    b.session->attach(world.hub.connect(2, "lee"), world.now, "t-lee-3");
    const double t1 = world.now;
    check(world.run_until(8, [&] { return world.hub.room_story.counters[7] == 3; }), "the growth reaches the room story after the reconnect");
    check(world.now - t1 < 4.0, "soon after the reconnect (" + std::to_string(world.now - t1) + " s)");
    world.run(12);
    check(world.hub.room_story.counters[7] == 3, "counted once");
}

// Two co-arrived members; returns once both play sealed.
// (Delay 6: no pad of theirs comes late with 20 ms one way.)
void co_arrive(World& world, fake::Client& a, fake::Client& b) {
    a.input = press_in({{"stagis10", 20}, {"stagml11", 200}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 230}}, fake::kExitButton);
    check(world.run_until(20, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; }), "both sealed");
}

// B opens a prompt that only its own pad answers (an exit prompt has no time
// limit) and leaves the room: its LEAVE waits for a membership-safe step.
// From the LEAVE's tick B's slot is marked leaving (6.5) and its prompt closes
// as No by itself, the step comes and the LEAVE applies; without the mark
// nobody could close it and A kept B's player for good (A does not even see
// B's prompt: its pad must not answer it, second S1 review).
void leave_with_open_prompt() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    co_arrive(world, a, b);
    world.run(3);
    const std::uint32_t at = b.session->tick() + 30;
    b.input = [at](const fake::Client& c) -> std::uint32_t { return c.session->tick() == at ? fake::kPromptButton : 0u; };
    a.input = [](const fake::Client&) -> std::uint32_t { return fake::kAnswerButton; }; // A presses B all the time
    check(world.run_until(5, [&] { return a.engine.prompt_owner_ == 1; }), "B's prompt is open on A's machine (B: " + std::to_string(b.engine.prompt_owner_) + ", at " + std::to_string(at) + ")");
    world.run(1);
    check(a.engine.prompt_owner_ == 1, "A's pad does not answer B's prompt");
    a.input = nullptr; // A only walks
    world.hub.stamp_leave(2);
    world.hub.drop(2);
    check(world.run_until(10, [&] { return a.session->present() == 0b01 && a.engine.prompt_owner_ < 0; }),
          "the leaving member's prompt closes by itself and the LEAVE applies");
    check(a.session->mode() == Mode::solo && a.engine.present_ == 0b01, "A leads alone");
}

// A member that lost the fingerprint vote (6.9) catches up from the state
// and goes back to sending its pads as a member: no JOIN comes for a resync
// (the relay ignores READY of members; S1 review: the client stayed an
// observer for good and was lag-removed).
void resync_back_to_member() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    co_arrive(world, a, b);
    world.run(10);
    check(world.hub.netfp_mismatches == 0, "equal before");
    b.engine.state_ ^= 0x55; // a desync on B's machine
    world.run(2);
    check(world.hub.netfp_mismatches > 0, "the fingerprints differ");
    const auto restores = b.engine.restores;
    world.hub.resync(2, world.now);
    check(world.run_until(10, [&] { return b.engine.restores > restores && b.session->mode() == Mode::sealed; }), "B caught up and is a member again");
    check(world.hub.resync_ready == 1, "B told the relay (READY)");
    world.run(1);
    const auto mismatches = world.hub.netfp_mismatches;
    const auto compared = world.hub.netfp_compared;
    const auto fills = world.hub.fills_total;
    world.run(15);
    check(world.hub.netfp_mismatches == mismatches && world.hub.netfp_compared >= compared + 10, "equal fingerprints after the resync");
    check(world.hub.fills_total == fills && !world.hub.members[2].lagging, "B sends its pads again (none filled)");
    check(b.session->present() == 0b11 && a.session->present() == 0b11, "both still present");
    bool notice = false;
    for (const auto& n : b.session->take_notices()) notice |= n.find("Back in sync") != std::string::npos;
    check(notice, "B is told");
}

// A RESYNC that reaches a member parked after its barrier is no resync: the
// visit it was about ended there (second S1 review: the state threw the
// member back into the stage it had left, restored as its load).
void resync_while_parked() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    co_arrive(world, a, b);
    world.run(3);
    const std::uint32_t at = a.session->tick() + 30;
    a.input = [at](const fake::Client& c) -> std::uint32_t { return c.engine.stage() == "stag0101" && c.session->tick() == at ? fake::kExitButton : 0u; };
    world.hub.answer_barriers = false;
    check(world.run_until(5, [&] { return b.session->mode() == Mode::parked && a.session->mode() == Mode::parked; }), "both park at the group's barrier");
    world.run(0.2);
    // B's fingerprint lost a vote just before the relay saw its BARRIER: RESYNC reaches it parked, the group's
    // decision (B is the base: NEW) comes right after, the old visit's state after that.
    world.hub.members[2].parked_at = 0;
    world.hub.state_pacing = 0.3;
    world.hub.resync(2, world.now);
    world.hub.co_arrival_hold = 0.05;
    world.hub.answer_barriers = true;
    check(world.run_until(20, [&] { return b.engine.stage() == "stag0102" && b.session->mode() == Mode::sealed && a.session->mode() == Mode::sealed; }),
          "B ignores the RESYNC and goes on to the next stage with A");
    world.run(5);
    check(b.engine.stage() == "stag0102" && b.session->inst() == a.session->inst(), "one instance in stag0102");
}

// The partner leaves while a member catches up after a RESYNC: the LEAD_FROM
// is in the ticks the replay runs, and the member leads alone afterwards
// (second S1 review: it went back to sealed and waited for good).
void resync_then_alone() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    co_arrive(world, a, b);
    world.run(5);
    b.engine.state_ ^= 0x55; // a desync on B's machine
    world.hub.state_pacing = 0.6; // the state comes after the partner's LEAVE and LEAD_FROM
    world.hub.resync(2, world.now);
    world.run(0.05);
    world.hub.stamp_leave(1);
    world.hub.drop(1);
    check(world.run_until(10, [&] { return b.session->mode() == Mode::solo; }), "B leads alone after the resync");
    const auto tick = b.session->tick();
    world.run(2);
    check(b.session->mode() == Mode::solo && b.session->tick() > tick + 60, "B plays on");
    check(world.hub.resync_ready == 1, "B ended its resync (READY)");
}

// The server took a member out while it plays (its own LEAVE comes as a
// sealed tick): it continues alone from its own state, the others' players
// go, nothing more is sealed for it (before, it removed its own player and
// waited for ticks nobody sent).
void taken_out_continues_alone() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    co_arrive(world, a, b);
    world.run(3);
    world.hub.stamp_leave(2);
    check(world.run_until(5, [&] { return b.session->mode() == Mode::solo && b.engine.present_ == 0b10; }),
          "B continues alone with its own player only (present " + std::to_string(b.engine.present_) + ")");
    const auto steps = b.steps;
    world.run(2);
    check(b.steps > steps + 100 && b.session->present() == 0b10, "B plays on");
    check(b.session->inst() == 0, "unregistered until its next barrier (a lone arrival, answered as instance 0)");
    check(a.session->mode() == Mode::solo && a.engine.present_ == 0b01, "A leads alone");
    bool notice = false;
    for (const auto& n : b.session->take_notices()) notice |= n.find("on your own") != std::string::npos;
    check(notice, "B is told");
}

void pacing_and_lag() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    a.input = press_in({{"stagis10", 20}, {"stagml11", 100}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 120}}, fake::kExitButton);
    check(world.run_until(20, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; }), "sealed");
    world.run(30);
    // Round I1a: the own instance as the relay describes it (INSTANCE_INFO).
    {
        w::InstanceInfo info;
        info.wid = 7;
        info.host = 1;
        info.area = "death_valley";
        info.members = {{1, "stag0101", 12, 1 | 8 | 16}, {2, "stag0101", 9, 1 | 2 | 16}};
        info.taken = 2;
        info.role = w::Role::host;
        world.hub.send(1, w::Type::instance_info, w::encode(info), world.now);
        world.run(0.5);
        const auto& seen = a.session->instance();
        check(seen && seen->wid == 7 && seen->members.size() == 2 && seen->members[0].level == 12 && seen->members[1].flags == 19 &&
                  a.session->instance_players() == 2,
              "INSTANCE_INFO reaches the Online menu");
    }
    {
        // An informational frame that does not decode (round 62 review: round 60's PRESENCE2 to clients on 0x8F, the
        // number NETSTAT holds since 0.1.2) is skipped; the session stays connected and sealed.
        std::vector<std::uint8_t> other(40, 0x55);
        world.hub.send(1, w::Type::netstat, other, world.now);
        world.hub.send(1, w::Type::instance_info, {0x07}, world.now);
        world.hub.send(1, w::Type::presence2_of, {0x02, 0x00}, world.now);
        world.run(0.5);
        check(a.session->mode() == Mode::sealed && a.session->net().reconnects == 0 && a.session->net().skipped_frames == 3,
              "undecodable NETSTAT / INSTANCE_INFO / PRESENCE2 frames are skipped, not a lost connection");
        world.hub.send(1, w::Type::netstat, w::encode(w::NetStat{30, 4, 3, 12, 900, 2, 1, w::NetStat::in_stream}), world.now);
        world.run(0.5);
        const auto& net = a.session->net();
        check(net.from_relay && net.rtt_ms == 30 && net.sealed == 900, "the next NETSTAT counts");
        // A PING waits here for the next pass: half the gap between passes (1/240 s here), which the network's round
        // trip leaves out (round 62 review: "Ping 34 ms" at 24 ms of network with 60 passes a second).
        check(net.pass_wait_ms > 1.5 && net.pass_wait_ms < 2.6, "the pass wait: " + std::to_string(net.pass_wait_ms));
        check(std::abs(net.network_rtt_ms() - (30 - net.pass_wait_ms)) < 1e-9, "the network's round trip");
    }
    const double ratio = a.session->audio_ratio();
    check(ratio >= 0.999 && ratio <= 1.001, "the audio ratio stays within 0.1 %");
    // Lag: the server fills B's pads when they stop (B's uplink gone, its downlink alive).
    world.hub.latency = 0.02;
    auto* conn = world.hub.members[2].conn;
    conn->inbound.clear();
    const double cut_until = world.now + 1.0;
    while (world.now < cut_until) {
        world.now += World::kPass;
        conn->inbound.clear();
        std::vector<std::uint8_t> drain;
        conn->pipe->receive(drain); // B's frames are lost
        world.hub.tick(world.now);
        for (auto& c : world.clients) c->frame(world.now, World::kPass);
    }
    check(a.session->overlay(world.now).lagging[1], "A sees B lagging (ROOM lagging after 30 filled pads)");
    check(b.session->overlay(world.now).slow_uplink, "B sees its own pads filled");
}
// Round I1a (docs/SEAMLESS.md 19): the world session over the fake hub. A
// leaves home and hosts a new instance (the toast once INSTANCE_INFO came);
// B arrives from home and is joined automatically (I1a's first row): its own
// load takes the host's story (replace) and settings, its toasts name the
// host; its STORY carries the host's owner key; *Leave instance* sends INTENT
// 3; the relay's HOME for B's slot forks B off and takes it home (a DISSOLVE
// with the own story replaced, the own settings back, the redirect's point);
// A's instance is forgotten by the relay (a WELCOME without it): A takes the
// home departure itself.
bool has_notice(std::vector<std::string> notices, const std::string& part) {
    for (const auto& n : notices)
        if (n.find(part) != std::string::npos) return true;
    return false;
}

void world_session() {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.suggested_delay = 6;
    w::Settings host_settings{1, 1, 0, 0, 4242, 1, 1, 0};
    world.hub.shape_ok = [&](std::uint32_t member, w::BarrierOk& ok) {
        if (ok.decision == w::Decision::dissolve) {
            ok.owner = member;
            ok.story_mode = w::StoryMode::replace;
            if (member == 2) {
                ok.redirect_stage = "stagml11";
                ok.redirect_point = 3;
            }
            return;
        }
        ok.wid = 7;
        ok.host = 1;
        ok.owner = 1;
        ok.settings = host_settings;
        ok.arrival = member == 1 ? w::Arrival::new_instance : w::Arrival::auto_join;
        if (member != 1) ok.story_mode = w::StoryMode::replace;
    };
    auto& a = world.add("kim", 1);
    a.input = press_in({{"stagis10", 20}, {"stagml11", 100}}, fake::kExitButton);
    check(world.run_until(20, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A leaves home");
    check(a.session->in_instance() && a.session->wid() == 7 && a.session->is_host(), "A hosts instance 7");
    w::InstanceInfo info;
    info.wid = 7;
    info.host = 1;
    info.area = "death_valley";
    info.members = {{1, "stag0101", 5, 1 | 8 | 16}};
    info.taken = 1;
    world.hub.send(1, w::Type::instance_info, w::encode(info), world.now);
    world.run(0.3);
    check(has_notice(a.session->take_notices(), "New instance (Public). View: Online menu"), "A's toast: a new instance");
    auto& b = world.add("lee", 2);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 100}}, fake::kExitButton);
    check(world.run_until(30, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; }), "B joined A");
    check(b.session->wid() == 7 && b.session->host() == 1 && !b.session->is_host() && b.engine.story_replaces >= 1 && b.engine.host_settings &&
              *b.engine.host_settings == host_settings,
          "B's own load took the host's story (replace) and settings");
    info.members = {{1, "stag0101", 5, 1 | 8 | 16}, {2, "stag0101", 3, 1 | 16}};
    info.taken = 2;
    info.role = w::Role::guest;
    world.hub.send(2, w::Type::member_info, w::encode(w::MemberInfo{1, "kim", 5, w::MemberInfo::own_instance}), world.now);
    world.hub.send(2, w::Type::instance_info, w::encode(info), world.now);
    world.run(0.3);
    const auto b_notices = b.session->take_notices();
    check(has_notice(b_notices, "You joined kim's instance. kim is in stag0101. Leave: Online menu") &&
              has_notice(b_notices, "You're a guest in kim's story. Your level, items and Digimon stay yours."),
          "B's toasts name the host and where he is");
    // B's STORY goes to the host's keeper (the host's owner key).
    const auto stories = world.hub.stories;
    b.engine.story.flags[19] = 1;
    check(world.run_until(5, [&] { return world.hub.stories > stories; }) && world.hub.last_story_owner == 1, "B's STORY carries the host's key");
    // *Leave instance*: INTENT 3.
    check(b.session->leave_instance(), "B may leave");
    world.run(0.3);
    check(world.hub.leave_intents == 1, "the relay got INTENT leave");
    // The relay's HOME for B's slot (19.6.5): B forks off, goes home on its own
    // story and settings; A plays on alone in the instance.
    {
        auto& inst = world.hub.instances[a.session->inst()];
        w::Event home{w::EventKind::home};
        home.mask = std::uint8_t(1u << b.session->own_slot());
        home.reason = std::uint8_t(w::HomeReason::leave);
        inst.events[inst.next_seal + 12].push_back(home);
    }
    check(world.run_until(20, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo && b.session->wid() == 0; }),
          "B is home");
    check(b.engine.homes == 1 && b.engine.own_settings_set_back && b.engine.redirected_point == 3 && b.engine.story_replaces >= 2,
          "the home departure, the own settings back, the redirect, the own story replaced");
    {
        const auto home_notices = b.session->take_notices();
        std::string all;
        for (const auto& n : home_notices) all += " [" + n + "]";
        check(has_notice(home_notices, "Back on your own story."), "B's toast at home:" + all);
    }
    check(a.engine.stage() == "stag0101" && a.session->present() == std::uint8_t(1u << a.session->own_slot()) && a.session->in_instance(),
          "A plays on alone in its instance");
    // The relay forgets A's instance (it restarted or ended it while A was
    // away): the WELCOME of A's reconnect names none, A goes home by itself.
    world.hub.wid_forgotten.insert(1);
    world.hub.drop(1);
    world.run(1.0);
    check(a.session->wants_transport(), "A reconnects");
    a.session->attach(world.hub.connect(1, "kim"), world.now, "t-kim-2");
    check(world.run_until(20, [&] { return a.engine.stage() == "stagml11" && a.session->mode() == Mode::solo && !a.session->going_home(); }),
          "A took the home departure itself");
    check(a.engine.homes == 1 && !a.session->in_instance(), "one local departure, home");
    const auto a_notices = a.session->take_notices();
    check(has_notice(a_notices, "The instance has ended."), "A is told the instance ended");
}

int value(const std::map<std::int32_t, std::uint8_t>& values, std::int32_t key) {
    const auto it = values.find(key);
    return it == values.end() ? 0 : int(it->second);
}

// Round I1a's review: B (member `id`) alone; its departure from home is an
// automatic join into host 9's instance 7 as a guest (the replace brings the
// host's story: flag 19, key 4); every barrier into a hub stage is a DISSOLVE
// with B's own story (the hub's room story: flag 20, key 1) by the replace.
fake::Client& guest_of_nine(World& world, std::uint32_t id = 2) {
    dw4::engine::RoomStory host;
    host.flags[19] = 1;
    host.folder[4] = 1;
    host.revision = 40;
    world.hub.room_story = {};
    world.hub.room_story.flags[20] = 1;
    world.hub.room_story.folder[1] = 1;
    world.hub.room_story.revision = 3;
    world.hub.shape_ok = [host](std::uint32_t member, w::BarrierOk& ok) {
        if (ok.decision == w::Decision::dissolve) {
            ok.owner = member;
            ok.story_mode = w::StoryMode::replace;
            return;
        }
        ok.wid = 7;
        ok.host = 9;
        ok.owner = 9;
        ok.box = 1;
        ok.arrival = w::Arrival::guest_new;
        ok.story_mode = w::StoryMode::replace;
        ok.story = dw4::engine::story::encode_story(host);
        ok.story_revision = host.revision;
    };
    auto& b = world.add("lee", id);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 100}}, fake::kExitButton);
    check(world.run_until(20, [&] { return b.engine.stage() == "stag0101" && b.session->mode() == Mode::solo; }), "B in the field");
    check(b.session->wid() == 7 && b.session->host() == 9 && !b.session->is_host() && !b.session->holds_own_story() &&
              b.session->visit_owner() == std::make_pair(std::uint32_t(9), std::uint8_t(1)),
          "B is a guest in 9's instance");
    check(value(b.engine.story.flags, 19) == 1 && value(b.engine.story.flags, 20) == 0 && value(b.engine.story.folder, 4) == 1,
          "B's engine holds the host's story");
    world.run(0.5);
    return b;
}

// Round I1a's review (blocker): a server restart while a guest plays in a
// field of someone else's instance. A restarted relay numbers its members
// from 1 again, so the guest may get its host's former id (9): a STORY with
// the old owner key would then name the guest's own story, and the relay
// would write the host's values into it. No STORY goes while the engine holds
// the host's story; the local way home's DISSOLVE replaces it with the own,
// and from home the STORY carries the new own key.
void restart_story_key(std::uint32_t new_id) {
    World world;
    world.hub.co_arrival_hold = 0.3;
    world.hub.suggested_delay = 6;
    auto& b = guest_of_nine(world);
    // The field's progress on the host's story, made while the connection drops (the host's keeper never sees it).
    b.engine.story.flags[393] = 1;
    world.hub.drop(2);
    world.run(1.0);
    check(b.session->wants_transport(), "B reconnects");
    const auto seen = world.hub.story_frames.size();
    b.session->attach(world.hub.connect(new_id, "lee"), world.now, "t-lee-2");
    check(world.run_until(25, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo && !b.session->going_home(); }),
          "B took the local way home");
    for (std::size_t i = seen; i < world.hub.story_frames.size(); ++i) {
        const auto& f = world.hub.story_frames[i];
        const bool foreign = std::find(f.delta.flags.begin(), f.delta.flags.end(), 19) != f.delta.flags.end() ||
                             std::find(f.delta.flags.begin(), f.delta.flags.end(), 393) != f.delta.flags.end() ||
                             std::find(f.delta.folder.begin(), f.delta.folder.end(), 4) != f.delta.folder.end();
        check(!foreign, "a STORY after the restart carried the host's values (owner " + std::to_string(f.owner) + ")");
    }
    check(!world.hub.room_story.flags[19] && !world.hub.room_story.flags[393] && !world.hub.room_story.folder[4],
          "B's own keeper got none of the host's values");
    check(b.session->wid() == 0 && b.session->holds_own_story() && b.session->visit_owner() == std::make_pair(new_id, std::uint8_t(1)),
          "home on the new own key");
    check(value(b.engine.story.flags, 19) == 0 && value(b.engine.story.flags, 393) == 0 && value(b.engine.story.flags, 20) == 1 &&
              value(b.engine.story.folder, 1) == 1 && value(b.engine.story.folder, 4) == 0,
          "B's lobby engine holds its own story (the DISSOLVE's replace)");
    check(has_notice(b.session->take_notices(), "The server restarted. Back on your own story."), "B is told");
    // Lobby progress goes up under the new own key.
    const auto before = world.hub.story_frames.size();
    b.engine.story.flags[22] = 1;
    check(world.run_until(5, [&] { return world.hub.story_frames.size() > before; }) && world.hub.story_frames.back().owner == new_id,
          "the lobby's STORY carries the new own key");
    check(world.run_until(5, [&] { return world.hub.room_story.flags[22] == 1; }), "and reaches the own keeper");
}

// Round I1a's integration (W24): an ordered server stop sends CLOSE stopping.
// The server comes back on the same address, so the session reconnects as
// after a lost link (before: closed for good, the guest stayed in the field
// on the host's story); the new process's WELCOME without the wid starts the
// local way home (19.6.10).
void close_stopping_reconnects() {
    World world;
    world.hub.co_arrival_hold = 0.3;
    world.hub.suggested_delay = 6;
    auto& b = guest_of_nine(world);
    world.hub.send(2, w::Type::close, w::encode(w::Close{w::CloseReason::stopping, ""}), world.now);
    world.run(0.5);
    world.hub.drop(2);
    world.run(1.0);
    check(!b.session->closed(), "B's session is not closed by CLOSE stopping");
    check(b.session->wants_transport(), "B reconnects after CLOSE stopping");
    b.session->attach(world.hub.connect(5, "lee"), world.now, "t-lee-2");
    check(world.run_until(25, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo && !b.session->going_home(); }),
          "B took the local way home after the restart");
    check(b.session->wid() == 0 && b.session->holds_own_story() && b.session->visit_owner() == std::make_pair(std::uint32_t(5), std::uint8_t(1)),
          "home on the new own key");
    check(value(b.engine.story.flags, 19) == 0 && value(b.engine.story.flags, 20) == 1 && value(b.engine.story.folder, 4) == 0,
          "B's lobby engine holds its own story");
    check(has_notice(b.session->take_notices(), "The server restarted. Back on your own story."), "B is told");
}

// Round I1a's integration (W6, W7b): HOME for a member that leads its stage
// group alone comes through JOIN_REQ home; the relay seals only J (HOME,
// LEAD_FROM J + 1) and the member leads alone again after it. That one sealed
// tick never filled the pacer's playout buffer (two waiting ticks), so the
// member froze at J for good (W6: b in dng101b1 after a's gate disc).
void solo_leader_home() {
    World world;
    world.hub.co_arrival_hold = 0.3;
    world.hub.suggested_delay = 4;
    auto& b = guest_of_nine(world);
    world.run(2.0);
    check(b.session->mode() == Mode::solo, "B leads its stage group alone");
    world.hub.ask_home(2, std::uint8_t(1u << b.session->own_slot()), w::HomeReason::ended, world.now);
    check(world.run_until(20, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo && !b.session->going_home(); }),
          "B took the home departure from the one sealed tick J");
    check(b.session->wid() == 0 && b.session->holds_own_story(), "B is home on its own story");
}

// Round I1a's review: a restart while the member is at home. Its engine holds
// its own story: the STORY goes under the new member id (before: the old key,
// which the relay refuses as reason owner until the next BARRIER_OK).
void restart_at_home() {
    World world;
    world.hub.room_story.flags[20] = 1;
    world.hub.room_story.revision = 3;
    auto& b = world.add("lee", 2);
    b.input = press_in({{"stagis10", 20}}, fake::kExitButton);
    check(world.run_until(20, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo; }), "B at home");
    world.run(0.5);
    world.hub.drop(2);
    world.run(1.0);
    check(b.session->wants_transport(), "B reconnects");
    b.session->attach(world.hub.connect(5, "lee"), world.now, "t-lee-2");
    world.run(1.0);
    check(b.session->member_id() == 5 && b.session->holds_own_story() && b.session->visit_owner() == std::make_pair(std::uint32_t(5), std::uint8_t(1)),
          "the own story's key follows the new id");
    check(b.session->visit_story() && b.session->visit_story()->revision == 3, "the visit's confirmed story is the own");
    const auto before = world.hub.story_frames.size();
    b.engine.story.flags[22] = 1;
    check(world.run_until(5, [&] { return world.hub.story_frames.size() > before; }) && world.hub.story_frames.back().owner == 5,
          "the lobby's STORY carries the new own key");
}

// Round I1a's review: the client's own answer to a barrier the relay left
// unanswered (3.4: a solo group goes on as NEW after five seconds) keeps
// what the engine holds in the world. A guest's move between fields stays a
// guest's in its instance (host, owner key, the host's confirmed story, no
// replace, no own settings); a guest's way home is the DISSOLVE the relay
// would give: the own settings and the own story in place of the host's.
void fallback_in_world() {
    {
        World world;
        world.hub.co_arrival_hold = 0.3;
        world.hub.suggested_delay = 6;
        auto& b = guest_of_nine(world);
        world.hub.answer_barriers = false;
        b.engine.own_settings_set_back = false;
        const int replaces = b.engine.story_replaces;
        b.input = press_in({{"stag0101", b.session->tick() + 30}}, fake::kExitButton);
        check(world.run_until(5, [&] { return b.session->mode() == Mode::parked; }), "B parked at its field exit");
        check(world.run_until(10, [&] { return b.engine.stage() == "stag0102" && b.session->mode() == Mode::solo; }), "B's own NEW after the timeout");
        check(b.session->wid() == 7 && b.session->host() == 9 && b.session->in_instance() && !b.session->is_host() &&
                  b.session->visit_owner() == std::make_pair(std::uint32_t(9), std::uint8_t(1)) && !b.session->holds_own_story(),
              "still a guest in 9's instance");
        check(b.session->visit_story() && b.session->visit_story()->revision == 40, "the host's confirmed story stays the visit's");
        check(b.engine.story_replaces == replaces && !b.engine.own_settings_set_back && value(b.engine.story.flags, 19) == 1,
              "the engine keeps the host's story and settings");
        check(has_notice(b.session->take_notices(), "The server did not answer; you continue on your own."), "B is told");
    }
    {
        World world;
        world.hub.co_arrival_hold = 0.3;
        world.hub.suggested_delay = 6;
        auto& b = guest_of_nine(world);
        world.hub.answer_barriers = false;
        b.engine.own_settings_set_back = false;
        const int replaces = b.engine.story_replaces;
        b.engine.home(b.session->present()); // (the gate disc: a departure into stagML11)
        check(world.run_until(5, [&] { return b.session->mode() == Mode::parked; }), "B parked at its barrier home");
        check(world.run_until(10, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo; }), "B home after the timeout");
        check(b.session->wid() == 0 && b.session->host() == 0 && b.session->holds_own_story(), "home, on the own story");
        check(b.engine.story_replaces == replaces + 1 && b.engine.own_settings_set_back && value(b.engine.story.flags, 19) == 0 &&
                  value(b.engine.story.flags, 20) == 1 && value(b.engine.story.folder, 4) == 0,
              "the lobby loaded on the own story and settings (the local DISSOLVE)");
        const auto notices = b.session->take_notices();
        check(has_notice(notices, "The server did not answer; you continue on your own.") && has_notice(notices, "Back on your own story."),
              "B is told");
    }
}

// Round I1a's review: *Leave instance* while the INTENT cannot go is kept and
// sent once it can: at a barrier (sent in the next stage), and for an engine
// playing on apart from its instance after a reconnect (the relay files its
// INTENT under the home instance and ignores it) until its next stage. The
// host's countdown is a running line, not a toast every few seconds; the
// instance's end takes such an engine home by itself (19.6.10: WORLD_NOTICE
// ended, as no stage group of the instance sends it HOME).
void leave_and_end_in_world() {
    {
        World world;
        world.hub.co_arrival_hold = 0.3;
        world.hub.suggested_delay = 6;
        auto& b = guest_of_nine(world);
        b.input = press_in({{"stag0101", b.session->tick() + 30}}, fake::kExitButton);
        check(world.run_until(5, [&] { return b.session->mode() == Mode::parked; }), "B parked at its field exit");
        check(b.session->leave_instance() && b.session->leave_pending(), "the leave is kept while parked");
        check(has_notice(b.session->take_notices(), "You leave the instance as soon as this stage allows it."), "B is told");
        world.run(0.1);
        check(world.hub.leave_intents == 0, "no INTENT at the barrier");
        check(world.run_until(10, [&] { return b.engine.stage() == "stag0102" && world.hub.leave_intents == 1; }), "the INTENT goes in the next stage");
        check(!b.session->leave_pending(), "sent once");
    }
    {
        World world;
        world.hub.co_arrival_hold = 0.3;
        world.hub.suggested_delay = 6;
        auto& b = guest_of_nine(world);
        // A reconnect the relay does not resume, its WELCOME still naming the instance: B plays on apart from it.
        world.hub.welcome_wid[2] = 7;
        world.hub.wid_forgotten.insert(2);
        world.hub.drop(2);
        world.run(1.0);
        b.session->attach(world.hub.connect(2, "lee"), world.now, "t-lee-2");
        world.run(1.0);
        check(b.session->mode() == Mode::solo && b.session->wid() == 7 && b.engine.stage() == "stag0101", "B plays on apart from its instance");
        b.session->take_notices();
        check(b.session->leave_instance() && b.session->leave_pending(), "the leave is kept");
        world.run(0.5);
        check(world.hub.leave_intents == 0, "no INTENT the relay would ignore");
        world.hub.send(2, w::Type::world_notice, w::encode(w::WorldNotice{w::NoticeKind::host_away, 9, "kim", 52, {}}), world.now);
        world.run(0.3);
        const auto status = b.session->overlay(world.now).status;
        check(status.find("kim lost the connection. Waiting for kim... 5") == 0, "the host's countdown is a running line: " + status);
        check(!has_notice(b.session->take_notices(), "lost the connection"), "not a toast");
        world.hub.send(2, w::Type::world_notice, w::encode(w::WorldNotice{w::NoticeKind::ended, 9, "kim", std::uint32_t(w::EndReason::host_lost), {}}),
                       world.now);
        world.run(0.3);
        check(b.session->overlay(world.now).status.empty(), "the countdown ends");
        check(has_notice(b.session->take_notices(), "kim did not come back. The instance ends."), "B is told");
        check(world.run_until(20, [&] { return b.engine.stage() == "stagml11" && b.session->mode() == Mode::solo && b.session->wid() == 0; }),
              "B took the home departure itself");
        check(b.session->holds_own_story() && value(b.engine.story.flags, 19) == 0 && value(b.engine.story.flags, 20) == 1 && !b.session->leave_pending() &&
                  world.hub.leave_intents == 0,
              "home on the own story; the kept leave is done");
    }
}

// The input delay a member needs so that its pads reach the server before
// their tick is sealed: two members co-arrive, then 20 s sealed play; late
// pads counted from the second second on. Returns the late fraction.
double late_fraction(double one_way, double jitter, int delay) {
    World world;
    world.hub.co_arrival_hold = 3.0;
    world.hub.latency = one_way;
    world.hub.jitter = jitter;
    world.hub.suggested_delay = delay;
    auto& a = world.add("kim", 1);
    auto& b = world.add("lee", 2);
    a.input = press_in({{"stagis10", 20}, {"stagml11", 100}}, fake::kExitButton);
    b.input = press_in({{"stagis10", 20}, {"stagml11", 120}}, fake::kExitButton);
    if (!world.run_until(30, [&] { return a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed; })) return 1.0;
    world.run(1.0);
    const auto late0 = world.hub.late_pads;
    const auto t0 = a.session->tick();
    world.run(20.0);
    const double pads = 2.0 * double(a.session->tick() - t0);
    return pads > 0 ? double(world.hub.late_pads - late0) / pads : 1.0;
}

// The smallest delay without late pads for each one-way time and jitter, and
// what the design formula d = ceil((RTT + J) / field) gives; the client
// executes a tick one playout tick plus up to one host pass after it arrives.
// Round J's review, the client side of the checkpoints: a solo leader tells
// only its load end (no hash, nobody to compare with); every member's
// STATEHASH stays on the 600-tick raster, a classic joiner's too (it kept
// begin_visit's 600 and reported its first member tick, off the raster); a
// tick state writer's error stops the visit's reports instead of leaving the
// step (it closed every member's game in the same STATEHASH tick).
void checkpoint_reports() {
    World world;
    world.hub.checkpoints = true;
    auto& a = world.add("kim", 1);
    a.engine.tick_states = true;
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A reaches stag0101 solo");
    world.run(11.5);
    check(a.session->tick() > 650, "A played past tick 600 alone");
    check(a.engine.tick_state_hashes == 0, "a solo leader computes no tick state hash");
    check(world.hub.statehashes.size() == 1 && (world.hub.statehashes[0].second.flags & w::StateHash::load_end_only) &&
              world.hub.statehashes[0].second.hash == 0 && world.hub.statehashes[0].second.load_end > 0,
          "a solo leader tells its load end once, without a hash (" + std::to_string(world.hub.statehashes.size()) + " reports)");
    auto& b = world.add("lee", 2);
    b.engine.tick_states = true;
    b.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(20, [&] { return b.session->mode() == Mode::sealed && a.session->mode() == Mode::sealed; }), "B joins (classic)");
    check(b.session->tick() > 600 && b.session->tick() < 1200, "B became a member between 600 and 1200 (" + std::to_string(b.session->tick()) + ")");
    world.run(25);
    int from_a = 0, from_b = 0;
    for (const auto& [name, sh] : world.hub.statehashes) {
        if (sh.flags & w::StateHash::load_end_only) continue;
        check(sh.tick % 600 == 0, name + "'s STATEHASH of tick " + std::to_string(sh.tick) + " is on the raster");
        (name == "kim" ? from_a : from_b) += 1;
    }
    check(from_a >= 2 && from_b >= 2 && from_a == from_b, "both report every period (" + std::to_string(from_a) + ", " + std::to_string(from_b) + ")");
    const auto before = world.hub.statehashes.size();
    a.engine.tick_state_throws = b.engine.tick_state_throws = true;
    std::string error;
    try {
        world.run(25);
    } catch (const std::exception& e) {
        error = e.what();
    }
    check(error.empty(), "a tick state error stays in the session (" + error + ")");
    check(world.hub.statehashes.size() == before, "no STATEHASH after it in this visit");
    check(a.session->mode() == Mode::sealed && b.session->mode() == Mode::sealed && world.hub.netfp_mismatches == 0, "both play on together");
}

// Round J's review: after a READY the relay answers with the state again (a
// READY mismatch; from a checkpoint the classic state follows), the joiner
// catches up again and sends READY again (ready_sent_ stayed set: no READY,
// "Joining..." for good).
void ready_after_a_refused_ready() {
    World world;
    world.hub.refuse_ready = 1;
    auto& a = world.add("kim", 1);
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A reaches stag0101 solo");
    world.run(4);
    auto& b = world.add("lee", 2);
    b.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    const bool joined = world.run_until(30, [&] { return b.session->mode() == Mode::sealed && a.session->mode() == Mode::sealed; });
    check(world.hub.ready_refused == 1, "the first READY was answered with the state again");
    check(joined, "B sent READY again after the second catch-up and joined");
    check(b.engine.restores >= 2, "B restored the state twice");
}

// Protocol 48 (SEAMLESS.md 6.13.5): a checkpoint upload of tick T is followed
// by the donor's NETFP of the check tick T + kReadyCheckTicks - 1 (a joiner
// from that checkpoint names it in READY; on a multiple of 60 the ordinary
// NETFP is that report); no other NETFP leaves the 60-tick raster.
void checkpoint_check_tick() {
    World world;
    world.hub.checkpoints = true;
    auto& a = world.add("kim", 1);
    a.engine.tick_states = true;
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A reaches stag0101 solo");
    world.run(2);
    auto& b = world.add("lee", 2);
    b.engine.tick_states = true;
    b.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(20, [&] { return b.session->mode() == Mode::sealed && a.session->mode() == Mode::sealed; }), "B joins; both sealed");
    world.run(1);
    world.hub.netfps.clear();
    // (The last two asks 0.1 s apart: the second upload comes before the
    // first one's check tick, and both check NETFPs go out; lane jready's
    // review: one slot dropped the first.)
    for (const auto& [donor, wait] : std::initializer_list<std::pair<const char*, double>>{{"kim", 0.77}, {"lee", 0.77}, {"kim", 0.1}, {"kim", 0.77}}) {
        world.hub.ask_checkpoint(donor, world.now);
        world.run(wait);
    }
    world.run(1);
    check(world.hub.uploads.size() == 4, "four uploads (" + std::to_string(world.hub.uploads.size()) + ")");
    check(world.hub.uploads[3].second > world.hub.uploads[2].second && world.hub.uploads[3].second < w::check_tick(world.hub.uploads[2].second),
          "the fourth upload before the third one's check tick");
    for (const auto& [donor, tick] : world.hub.uploads) {
        const auto at = w::check_tick(tick);
        int from_donor = 0, from_other = 0;
        for (const auto& [name, n] : world.hub.netfps)
            if (n.tick == at) (name == donor ? from_donor : from_other) += 1;
        check(from_donor == 1, donor + "'s NETFP of the check tick " + std::to_string(at) + " of its upload of " + std::to_string(tick));
        check(from_other == (at % 60 == 0 ? 1 : 0), "the other's NETFP of " + std::to_string(at) + " only on the raster");
    }
    for (const auto& [name, n] : world.hub.netfps) {
        if (n.tick % 60 == 0) continue;
        bool upload = false;
        for (const auto& [donor, tick] : world.hub.uploads) upload = upload || (donor == name && w::check_tick(tick) == n.tick);
        check(upload, name + "'s NETFP of " + std::to_string(n.tick) + " off the raster is the check tick of its own upload");
    }
    check(world.hub.netfp_mismatches == 0, "no mismatch");
}

// Protocol 48, the joiner's side (SEAMLESS.md 6.13.5): a join from a fresh
// checkpoint of tick T catches up to the live tip within a few ticks, but its
// READY waits until it replayed the check tick T + kReadyCheckTicks - 1 and
// names it (or a later multiple of 60), so the relay compares the rebuild with
// the donor's NETFP of that tick (lane jready's review: only the real-client
// scenarios covered this side). Three joins, one after the other.
void checkpoint_join_ready_at_check_tick() {
    World world;
    world.hub.checkpoints = true;
    world.hub.checkpoint_joins = true;
    auto& a = world.add("kim", 1);
    a.engine.tick_states = true;
    a.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
    check(world.run_until(10, [&] { return a.engine.stage() == "stag0101" && a.session->mode() == Mode::solo; }), "A reaches stag0101 solo");
    world.run(3);
    std::vector<fake::Client*> joiners;
    for (const auto* name : {"lee", "max", "ned"}) {
        auto& b = world.add(name, std::uint32_t(2 + joiners.size()));
        b.engine.tick_states = true;
        b.input = press_in({{"stagis10", 30}, {"stagml11", 40}}, fake::kExitButton);
        joiners.push_back(&b);
        check(world.run_until(20, [&] { return b.session->mode() == Mode::sealed; }), std::string(name) + " joins from a checkpoint");
        world.run(1.5);
    }
    check(world.hub.uploads.size() == 3, "one upload per join (" + std::to_string(world.hub.uploads.size()) + ")");
    for (std::size_t n = 0; n < joiners.size() && n < world.hub.uploads.size(); ++n) {
        const auto& b = *joiners[n];
        const auto t = world.hub.uploads[n].second;
        const auto at = w::check_tick(t);
        check(b.engine.tick_state_applies == 1, b.name + " rebuilt from the checkpoint of " + std::to_string(t));
        const fake::Hub::ReadySeen* ready = nullptr;
        for (const auto& r : world.hub.readies)
            if (r.name == b.name) ready = &r;
        check(ready && ready->from_checkpoint, b.name + "'s READY from the checkpoint");
        if (!ready) continue;
        check(ready->tick == at || (ready->tick > at && ready->tick % 60 == 0),
              b.name + "'s READY names the check tick " + std::to_string(at) + " or a later multiple of 60 (" + std::to_string(ready->tick) + ")");
        // (In a sealed instance the READY may come before the donor's report:
        // the relay waits for it, ready_deferred.)
        int reports = 0, equal = 0;
        for (const auto& [name, report] : world.hub.netfps)
            if (name == "kim" && report.tick == ready->tick) {
                ++reports;
                equal += report.hash == ready->fingerprint ? 1 : 0;
            }
        check(reports == 1 && equal == 1, b.name + "'s READY of " + std::to_string(ready->tick) + " equals the donor's NETFP of that tick");
    }
    check(world.hub.netfp_mismatches == 0, "no mismatch");
}

void delay_table(bool print) {
    struct Row {
        double one_way, jitter;
    };
    for (const Row row : {Row{0.005, 0}, Row{0.010, 0}, Row{0.020, 0}, Row{0.030, 0.010}, Row{0.050, 0}, Row{0.050, 0.020}}) {
        int needed = 0;
        for (int d = 1; d <= 10 && !needed; ++d)
            if (late_fraction(row.one_way, row.jitter, d) == 0.0) needed = d;
        const double field = kFieldSeconds;
        // J = the round trip spread: the jitter of both directions.
        const int design = int(std::ceil((2 * row.one_way + 2 * row.jitter) / field));
        const int with_playout = int(std::ceil((2 * row.one_way + 2 * row.jitter) / field + 1.5));
        if (print)
            std::printf("  one way %2.0f ms, jitter %2.0f ms: needs d = %d (design formula %d, with the playout tick %d)\n", row.one_way * 1000,
                        row.jitter * 1000, needed, design, with_playout);
        std::fflush(stdout);
        check(needed > 0 && needed <= with_playout, "the delay with the playout tick is enough");
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--delay-table") {
        try {
            delay_table(true);
        } catch (const std::exception& error) {
            std::fflush(stdout);
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
        return 0;
    }
    try {
        std::cout << "solo, join, fork\n";
        solo_join_fork();
        presence_tracks();
        presence2_tracks();
        presence2_session();
        presence2_smooth();
        presence2_walk();
        presence2_departure();
        std::cout << "co-arrival, reconnect\n";
        co_arrival();
        std::cout << "a 45 s cut, the resume's pads\n";
        long_cut_resume_pads();
        std::cout << "target moves on\n";
        target_moves_on();
        std::cout << "live during transition\n";
        live_during_transition();
        std::cout << "barrier timeout\n";
        barrier_timeout();
        std::cout << "barrier hold\n";
        barrier_hold();
        std::cout << "leave with an open prompt\n";
        leave_with_open_prompt();
        std::cout << "resync\n";
        resync_back_to_member();
        std::cout << "resync while parked\n";
        resync_while_parked();
        std::cout << "resync, then alone\n";
        resync_then_alone();
        std::cout << "taken out, continues alone\n";
        taken_out_continues_alone();
        std::cout << "room story\n";
        room_story();
        std::cout << "world session\n";
        world_session();
        std::cout << "world: a restart's story key\n";
        restart_story_key(9);
        restart_story_key(3);
        restart_at_home();
        std::cout << "world: HOME through JOIN_REQ home\n";
        solo_leader_home();
        std::cout << "world: CLOSE stopping reconnects\n";
        close_stopping_reconnects();
        std::cout << "world: the barrier fallback\n";
        fallback_in_world();
        std::cout << "world: a kept leave, the host away, the end\n";
        leave_and_end_in_world();
        std::cout << "checkpoint reports\n";
        checkpoint_reports();
        std::cout << "READY after a refused READY\n";
        ready_after_a_refused_ready();
        std::cout << "checkpoint check tick\n";
        checkpoint_check_tick();
        std::cout << "checkpoint join: READY at the check tick\n";
        checkpoint_join_ready_at_check_tick();
        std::cout << "pacing, lag\n";
        pacing_and_lag();
        std::cout << "input delay\n";
        delay_table(false);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
