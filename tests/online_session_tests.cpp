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
    check(!b.session->step_ready() && b.session->overlay(world.now).loading == "Loading the room story...", "the hold stops the steps");
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
    // The owner leaves; B makes a new flag: refused and dropped.
    world.hub.owner_away = true;
    b.engine.story.flags[393] = 1;
    check(world.run_until(3, [&] { return std::count(world.hub.dropped.flags.begin(), world.hub.dropped.flags.end(), 393) == 1; }),
          "the flag is refused and recorded as dropped");
    check(!b.session->confirmed_story()->flags[393], "not in the confirmed story");
    const auto stories_dropped = world.hub.stories;
    world.run(5);
    check(world.hub.stories == stories_dropped, "a refused value is not sent again before the barrier");
    (void)a.session->take_notices();
    (void)b.session->take_notices();
    const auto ta2 = a.session->tick();
    a.input = [ta2](const fake::Client& c) -> std::uint32_t { return c.engine.stage() == "stag0101" && c.session->tick() == ta2 + 60 ? fake::kExitButton : 0u; };
    b.input = nullptr;
    check(world.run_until(20, [&] { return a.engine.stage() == "stag0102" && b.engine.stage() == "stag0102" && a.session->mode() == Mode::sealed &&
                                           b.session->mode() == Mode::sealed; }),
          "both in stag0102");
    check(b.engine.story.flags.at(393) == 0, "the barrier removed the dropped flag from B's engine");
    check(b.engine.story.flags.at(19) == 1 && b.engine.story.counters.at(7) == 1, "the confirmed values stay");
    bool told_b = false, told_a = false;
    for (const auto& n : b.session->take_notices()) told_b |= n.find("Story progress was not saved") != std::string::npos;
    for (const auto& n : a.session->take_notices()) told_a |= n.find("Story progress was not saved") != std::string::npos;
    check(told_b && !told_a, "only B is told");
    // The owner is back. B's STORY with new growth is lost with its dying
    // connection (never answered): after the reconnect the pending set goes
    // again at once, not ten seconds after the lost one.
    world.hub.owner_away = false;
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
    // A room whose owner has no story yet (8.6, the user's decision of
    // 31.10): what B makes is refused not_started and dropped, and B is told
    // why (once).
    world.hub.not_started = true;
    (void)b.session->take_notices();
    b.engine.story.flags[395] = 1;
    check(world.run_until(3, [&] { return std::count(world.hub.dropped.flags.begin(), world.hub.dropped.flags.end(), 395) == 1; }),
          "the flag is refused and recorded as dropped (not started)");
    bool told_not_started = false;
    for (const auto& n : b.session->take_notices()) told_not_started |= n.find("has not started its story yet") != std::string::npos;
    check(told_not_started, "B is told that the owner's story has not started");
    world.hub.not_started = false;
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
    // The room view's roster: the relay's ROSTER as it came (areas, levels, flags).
    {
        w::Roster roster;
        roster.entries.push_back({1, "stag0101", 7, 12, 300, 320, 1 | 4});
        roster.entries.push_back({2, "", 7, 9, 0, 250, 1 | 2});
        world.hub.send(1, w::Type::roster, w::encode(roster), world.now);
        world.run(0.5);
        const auto& seen = a.session->roster();
        check(seen.size() == 2 && seen[0].stage == "stag0101" && seen[0].level == 12 && seen[0].flags == 5 && seen[1].stage.empty() &&
                  seen[1].max_hp == 250,
              "the roster reaches the room view");
    }
    {
        // An informational frame that does not decode (round 62 review: round 60's PRESENCE2 to clients on 0x8F, the
        // number NETSTAT holds since 0.1.2) is skipped; the session stays connected and sealed.
        std::vector<std::uint8_t> other(40, 0x55);
        world.hub.send(1, w::Type::netstat, other, world.now);
        world.hub.send(1, w::Type::roster, {0x07}, world.now);
        world.hub.send(1, w::Type::presence2_of, {0x02, 0x00}, world.now);
        world.run(0.5);
        check(a.session->mode() == Mode::sealed && a.session->net().reconnects == 0 && a.session->net().skipped_frames == 3,
              "undecodable NETSTAT / ROSTER / PRESENCE2 frames are skipped, not a lost connection");
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
    for (const auto* donor : {"kim", "lee", "kim", "kim"}) {
        world.hub.ask_checkpoint(donor, world.now);
        world.run(0.77);
    }
    world.run(1);
    check(world.hub.uploads.size() == 4, "four uploads (" + std::to_string(world.hub.uploads.size()) + ")");
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
        std::cout << "checkpoint reports\n";
        checkpoint_reports();
        std::cout << "READY after a refused READY\n";
        ready_after_a_refused_ready();
        std::cout << "checkpoint check tick\n";
        checkpoint_check_tick();
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
