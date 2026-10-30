// The DW4R2 client against an in-process fake hub (tests/online_fake_hub.hpp,
// the relay's layout) on a simulated clock with 20 ms one-way latency:
// WELCOME, solo-led play with LOG, a mid-stage join by snapshot + replay
// (READY, JOIN_REQ / JOIN_AT, the JOIN event, then sealed ticks with
// identical net fingerprints), an exit fork (REASSIGN, LEAD_FROM), a
// co-arrival (one entry for both, sealed from tick 0), a reconnect with
// RESUME, the barrier timeout of a solo group, and the 1 -> 2 pads.
#include "online_fake_hub.hpp"

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
