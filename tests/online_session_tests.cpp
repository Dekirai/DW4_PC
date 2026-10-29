// The DW4R2 client against an in-process fake hub (tests/online_fake_hub.hpp)
// on a simulated clock with 20 ms one-way latency: solo-led play with LOG,
// a mid-stage join by snapshot + replay (READY, JOIN_REQ / JOIN_AT, the JOIN
// event, then sealed ticks with identical net fingerprints), an exit fork,
// a co-arrival (one entry for both, sealed from tick 0), a reconnect with
// RESUME, the barrier timeout of a solo group, and the 1 -> 2 pads.
#include "online_fake_hub.hpp"

#include <cmath>
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
    for (const auto& n : notices_a) joined |= n == "lee joined";
    check(joined, "A's notice names the joiner");
    // B forks away (an exit-type fork): both sides go on alone.
    const std::uint32_t fork_at = b.session->tick() + 60;
    b.input = [fork_at](const fake::Client& c) -> std::uint32_t { return c.session->tick() == fork_at ? fake::kForkButton : 0u; };
    check(world.run_until(10, [&] { return a.session->mode() == Mode::solo && b.session->mode() == Mode::solo; }), "both solo after the fork");
    world.run(0.2); // the SPLIT frames reach the hub
    check(world.hub.splits == 2, "both members reported the SPLIT (" + std::to_string(world.hub.splits) + ")");
    check(b.session->inst() != a.session->inst(), "B continues under a new instance id (REKEY)");
    check(a.session->present() == 0b01 && b.session->present() == 0b10, "each side despawned the other");
    world.run(3);
    check(a.session->mode() == Mode::solo && b.session->mode() == Mode::solo, "still solo");
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
    check(a.session->overlay(world.now).lagging[1], "A sees B lagging (filled pads)");
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
        std::cout << "co-arrival, reconnect\n";
        co_arrival();
        std::cout << "barrier timeout\n";
        barrier_timeout();
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
