// The tick state's completeness (docs/SEAMLESS.md 6.13.3, round J).
//
// Without game data: XXH64's reference values, the header and section
// layout, refusals of foreign bytes. With the game data (argument 1 or
// DW4_TEST_DATA): in real stages, online with one to four players walking,
// attacking and jumping, at state-safe ticks after the load end
//  - idempotence: the state applied into the engine it was taken from saves
//    the same bytes again;
//  - divergence trials: the state saved at T, 1-60 steps with random wrong
//    pads, the state applied in place, the same steps with the true pads:
//    every net fingerprint must equal the straight run's (a field the
//    serializer misses keeps the wrong pads' value and shows); a trial whose
//    window made a node lazily or changed the topology is skipped and counted;
//  - the rebuild (6.13.2 steps 1-5): a fresh engine restores the visit's
//    load snapshot, replays the visit's ticks up to the load end, applies the
//    state of T and runs on with the true pads, equal to the straight run.
// Arguments after the data: a stage, players, steps, trial spacing (one
// scenario instead of the set), for development.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_membership.hpp"
#include "dw4/engine/tick_state.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4::engine;
using dw4::PadSample;
int checks{};
int failures{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void soft(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "  FAIL " << message << '\n';
    }
}

void hash_vectors() {
    // XXH64 reference values (xxhsum).
    check(xxh64({}) == 0xEF46DB3751D8E999ull, "XXH64 of nothing");
    const std::string abc = "abc";
    check(xxh64({reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()}) == 0x44BC2CF5AD770999ull, "XXH64 of abc");
    std::string long_text;
    for (int i = 0; i < 100; ++i) long_text += char('a' + i % 26);
    const auto h1 = xxh64({reinterpret_cast<const std::uint8_t*>(long_text.data()), long_text.size()});
    long_text[50] ^= 1;
    const auto h2 = xxh64({reinterpret_cast<const std::uint8_t*>(long_text.data()), long_text.size()});
    check(h1 != h2, "XXH64 sees one bit");
}

void refusals() {
    std::vector<std::uint8_t> junk(64, 7);
    bool threw = false;
    try {
        (void)decode_tick_header(junk);
    } catch (const TickStateError& e) {
        threw = e.section() == "header";
    }
    check(threw, "foreign bytes are refused in the header");
}

PadSample pad_for(std::uint64_t frame, int slot) {
    PadSample p;
    p.connected = true;
    const std::uint64_t phase = (frame / std::uint64_t(150 + 40 * slot) + std::uint64_t(slot)) % 8;
    static const int dirs[8][2] = {{0, 60}, {42, 42}, {60, 0}, {42, -42}, {0, -60}, {-42, -42}, {-60, 0}, {-42, 42}};
    if ((frame / 90) % 5 != 4) {
        p.left.x = float(dirs[phase][0]) / 74.0f;
        p.left.y = float(dirs[phase][1]) / 74.0f;
    }
    if ((frame + std::uint64_t(slot) * 11) % 37 < 2) p.buttons |= dw4::pad_button::a;
    if ((frame + std::uint64_t(slot) * 7) % 211 < 2) p.buttons |= dw4::pad_button::x;
    if ((frame + std::uint64_t(slot) * 13) % 401 < 2) p.buttons |= dw4::pad_button::b;
    return p;
}

std::array<PadSample, 4> pads_at(std::uint64_t frame) {
    std::array<PadSample, 4> pads{};
    for (int s = 0; s < 4; ++s) pads[std::size_t(s)] = pad_for(frame, s);
    return pads;
}

std::array<PadSample, 4> wrong_pads(std::mt19937& rng) {
    std::array<PadSample, 4> pads{};
    static const std::uint32_t buttons[] = {dw4::pad_button::a, dw4::pad_button::b, dw4::pad_button::x, dw4::pad_button::y,
                                            dw4::pad_button::r, dw4::pad_button::l, dw4::pad_button::z};
    for (auto& p : pads) {
        p.connected = true;
        p.left.x = float(int(rng() % 149) - 74) / 74.0f;
        p.left.y = float(int(rng() % 149) - 74) / 74.0f;
        for (const auto b : buttons)
            if (rng() % 6 == 0) p.buttons |= b;
    }
    return pads;
}

struct Scenario {
    std::string stage;
    int players = 2;
    std::vector<std::string> species;
    std::uint64_t steps = 3000;
    std::uint64_t every = 97;
    // Slot 1's own status window (round 63) from this frame: START, then
    // A (Equipment) / B / DOWN / A (Folder) / B / B / B, 120 frames apart;
    // its copies of the window models and lines stay after it closes. 0: none.
    std::uint64_t menu_at = 0;
    // Diagnosis: the straight run keeps its raw states of these frames, and
    // a trial's true-pad steps are compared with them section by section.
    std::uint64_t dump_from = 0, dump_to = 0;
    // A member joins (online_join_player, slot `players`, p_girumon) at the
    // first joinable tick from this frame (0: none).
    std::uint64_t join_at = 0;
    // One more rebuild from the first state at or after this frame (0: none).
    std::uint64_t rebuild_from = 0;
    std::array<PadSample, 4> pads(std::uint64_t frame) const {
        auto out = pads_at(frame);
        if (menu_at && frame + 30 >= menu_at && frame <= menu_at + 1000) {
            auto& pad = out[1];
            pad = PadSample{};
            pad.connected = true;
            static constexpr std::uint32_t presses[] = {dw4::pad_button::start, dw4::pad_button::a, dw4::pad_button::b, dw4::pad_button::down,
                                                        dw4::pad_button::a,     dw4::pad_button::b, dw4::pad_button::b, dw4::pad_button::b};
            if (frame >= menu_at) {
                const std::uint64_t t = frame - menu_at;
                if (t % 120 < 3 && t / 120 < std::size(presses)) pad.buttons = presses[t / 120];
            }
        }
        return out;
    }
};

void host_step(Engine& engine, const std::array<PadSample, 4>& pads) {
    engine.step(pads);
    if (!engine.held()) engine.traverse_scene();
}

Engine& start(std::unique_ptr<Engine>& holder, const std::filesystem::path& data, const Scenario& sc) {
    holder = std::make_unique<Engine>(data);
    Engine& engine = *holder;
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_tick_state_tests_cards");
    // A session's common sound banks come long before any online visit (the
    // original loads them at boot, 802F902C; the port with the session's first
    // sound effect), so they stand before the stage's, as a restored engine
    // has them (SoundManager::restore); a direct start would load them after
    // the stage's, and a request two banks hold would find another bank than
    // a classic joiner's. (missing_cues loads them and changes nothing else.)
    if (auto* sound = engine.get<SoundManager>()) (void)sound->missing_cues(engine, {});
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    for (int s = 0; s < sc.players; ++s) {
        sys->players[std::size_t(s)].joined = true;
        if (s < int(sc.species.size())) sys->players[std::size_t(s)].digimon = sc.species[std::size_t(s)];
    }
    OnlineRules rules;
    rules.enabled = true;
    rules.present = std::uint8_t((1u << sc.players) - 1u);
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.request_stage(sc.stage);
    return engine;
}

struct Straight {
    std::map<std::uint64_t, std::uint64_t> fingerprints; // post-step frame -> net fingerprint
    std::vector<std::uint8_t> snapshot;                  // the visit's load snapshot
    std::vector<std::array<PadSample, 4>> load_log;      // the visit's steps from the load step to the load end
    std::uint64_t load_frame{}, load_end{};
    std::map<std::uint64_t, std::vector<std::uint8_t>> states; // Scenario::dump_from..dump_to
    std::uint64_t joined{}; // the pre-step frame of the join (0: none)
    // The canonical states at a few state-safe frames (the classic joiner's
    // comparison) and every step's pads since the load step.
    std::map<std::uint64_t, std::vector<std::uint8_t>> canonical;
    std::vector<std::array<PadSample, 4>> visit_log;
    // The STATEHASH (canonical hash) at every state-safe frame that is a
    // multiple of kHashEvery after the load end: a rebuilt engine's own
    // STATEHASH reports soon after its join are compared with these.
    std::map<std::uint64_t, std::uint64_t> state_hashes;
};

constexpr std::uint64_t kHashEvery = 20;

PlayerCarry joiner_carry() {
    PlayerCarry c;
    c.slot = GameSystem::PlayerSlot{};
    c.slot.digimon = "p_girumon";
    c.slot.weapons = {"w_common02", "", ""};
    c.slot.stats.level = 5;
    c.personal.gate_stage_name = "GateOff";
    c.personal.gate_stage_no = -1;
    return c;
}

Straight run_straight(const std::filesystem::path& data, const Scenario& sc) {
    Straight out;
    std::unique_ptr<Engine> holder;
    Engine& engine = start(holder, data, sc);
    std::uint64_t serial = 0;
    bool fresh_visit = false;
    for (std::uint64_t i = 0; i < sc.steps; ++i) {
        const auto pads = sc.pads(engine.frame());
        if (sc.join_at && !out.joined && engine.frame() >= sc.join_at && engine.joinable()) {
            out.joined = engine.frame();
            engine.online_join_player(sc.players, joiner_carry(), 0);
        }
        host_step(engine, pads);
        if (const auto* s = engine.stage_snapshot(); s && s->serial != serial && !out.load_end) {
            serial = s->serial;
            out.snapshot = engine.snapshot_bytes(SnapshotForm::raw);
            out.load_frame = engine.frame();
            out.load_log.clear();
            out.visit_log.clear();
            fresh_visit = true;
        } else if (serial && !out.load_end) {
            out.load_log.push_back(pads);
        }
        if (!out.load_end && engine.load_end_frame()) out.load_end = engine.load_end_frame();
        // The first visit only (a boss stage may end in another stage).
        if (out.load_end && (engine.stage_name() != sc.stage || engine.pending_stage())) break;
        out.fingerprints[engine.frame()] = engine.net_fingerprint();
        if (sc.dump_to) {
            static std::string last;
            const auto why = engine.state_unsafe_reason();
            if (why != last) std::cout << "  straight " << engine.frame() << ": " << (why.empty() ? std::string("safe") : why) << " busy " << int(engine.busy_slots()) << std::endl;
            last = why;
        }
        if (serial && !fresh_visit) out.visit_log.push_back(pads);
        fresh_visit = false;
        if (out.load_end && engine.frame() >= out.load_end + 300 && engine.frame() % 500 == 0 && out.canonical.size() < 6 && engine.state_safe())
            out.canonical[engine.frame()] = engine.save_tick_state(TickForm::canonical);
        if (out.load_end && engine.frame() > out.load_end && engine.frame() % kHashEvery == 0 && engine.state_safe())
            out.state_hashes[engine.frame()] = engine.tick_state_hash();
        if (sc.dump_to && engine.frame() >= sc.dump_from && engine.frame() <= sc.dump_to && engine.state_safe())
            out.states[engine.frame()] = engine.save_tick_state();
    }
    engine.set_online(OnlineRules{});
    return out;
}

bool dense_rebuilds() {
    static const bool on = [] {
        const char* text = std::getenv("DW4_TICKSTATE_REBUILDS");
        return text && std::string(text) == "all";
    }();
    return on;
}

struct Trials {
    int saved{}, idempotent{}, trials{}, passed{}, skipped{}, rebuilds{};
    double save_ms{}, apply_ms{}, hash_ms{};
    std::size_t bytes{};
    std::map<std::string, int> unsafe;
};

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// The in-place trials on one engine that follows the straight run's pads.
void run_trials(const std::filesystem::path& data, const Scenario& sc, const Straight& straight, Trials& t,
                std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>>& kept) {
    std::unique_ptr<Engine> holder;
    Engine& engine = start(holder, data, sc);
    std::mt19937 rng(0x5EED);
    std::uint64_t next_trial = 0;
    bool rebuild_from_taken = false;
    int reported = 0;
    while (engine.frame() < straight.fingerprints.rbegin()->first) {
        if (straight.joined && engine.frame() == straight.joined) engine.online_join_player(sc.players, joiner_carry(), 0);
        host_step(engine, sc.pads(engine.frame()));
        const auto frame = engine.frame();
        if (!straight.fingerprints.contains(frame)) break;
        if (straight.fingerprints.at(frame) != engine.net_fingerprint()) {
            soft(false, sc.stage + ": the trial engine left the straight run at frame " + std::to_string(frame));
            break;
        }
        if (!straight.load_end || frame < straight.load_end || frame < next_trial) continue;
        const auto why = engine.state_unsafe_reason();
        if (!why.empty()) {
            ++t.unsafe[why];
            continue;
        }
        next_trial = frame + sc.every;
        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> bytes;
        try {
            bytes = engine.save_tick_state();
        } catch (const std::exception& e) {
            soft(false, sc.stage + ": save at " + std::to_string(frame) + ": " + e.what());
            break;
        }
        t.save_ms += ms_since(t0);
        t.bytes = std::max(t.bytes, bytes.size());
        ++t.saved;
        if (t.saved == 1 && std::getenv("DW4_TICKSTATE_SECTIONS")) {
            std::cout << "  sections:";
            for (const auto& section : tick_sections(bytes))
                if (section.bytes.size() > 2048) std::cout << ' ' << section.name << '=' << section.bytes.size() / 1024 << "K";
            std::cout << std::endl;
        }
        t0 = std::chrono::steady_clock::now();
        (void)engine.tick_state_hash();
        t.hash_ms += ms_since(t0);
        // States for the rebuild: the first and then every 12th (after a
        // member's window, its copies come back through the journal).
        // (DW4_TICKSTATE_REBUILDS=all, diagnosis: every state, each rebuild for kRebuildHashSteps steps.)
        const bool extra = sc.rebuild_from && frame >= sc.rebuild_from && !std::exchange(rebuild_from_taken, true);
        if (t.saved == 1 || (t.saved % 12 == 0 && kept.size() < 6) || extra || (dense_rebuilds() && kept.size() < 80)) kept.push_back({frame, bytes});
        // Idempotence: applied into itself, it saves the same bytes.
        t0 = std::chrono::steady_clock::now();
        try {
            engine.apply_tick_state(bytes);
        } catch (const TickStateError& e) {
            soft(false, sc.stage + ": apply at " + std::to_string(frame) + ": " + e.what());
            break;
        }
        t.apply_ms += ms_since(t0);
        const auto again = engine.save_tick_state();
        if (again == bytes) ++t.idempotent;
        else soft(false, sc.stage + ": not idempotent at " + std::to_string(frame) + " (" + first_differing_section(bytes, again) + ")");
        // The divergence trial.
        int window = 1 + int(rng() % 60);
        // (No window across the join: its tick comes from the straight run.)
        if (straight.joined && frame < straight.joined && frame + std::uint64_t(window) + 1 >= straight.joined) {
            next_trial = straight.joined + 1;
            continue;
        }
        const auto nodes_before = engine.scenes().node_count();
        const auto topology_before = engine.topology_hash();
        for (int k = 0; k < window && engine.online().enabled; ++k) host_step(engine, wrong_pads(rng));
        const bool topology_changed = engine.topology_hash() != topology_before || engine.scenes().node_count() != nodes_before ||
                                      engine.stage_name() != sc.stage || engine.pending_stage();
        try {
            engine.apply_tick_state(bytes);
        } catch (const TickStateError& e) {
            if (topology_changed && e.section() == "topology") {
                ++t.skipped;
                // The engine is lost for this run: rebuild it from the straight run.
                break;
            }
            soft(false, sc.stage + ": apply after the window at " + std::to_string(frame) + ": " + e.what());
            break;
        }
        if (topology_changed) ++t.skipped;
        ++t.trials;
        bool equal = true;
        bool diagnosed = false;
        for (int k = 0; k < window; ++k) {
            host_step(engine, sc.pads(engine.frame()));
            const auto f = engine.frame();
            if (!straight.fingerprints.contains(f)) break;
            if (const auto kept = straight.states.find(f); kept != straight.states.end() && !diagnosed && engine.state_safe()) {
                const auto mine = engine.save_tick_state();
                if (mine != kept->second) {
                    diagnosed = true;
                    // DW4_TICKSTATE_DUMP=DIR: both states for tools/online/tickstate_diff.py.
                    if (const char* dir = std::getenv("DW4_TICKSTATE_DUMP")) {
                        const auto base = std::filesystem::path(dir) / (sc.stage + "_" + std::to_string(f));
                        std::ofstream(base.string() + "_straight.bin", std::ios::binary)
                            .write(reinterpret_cast<const char*>(kept->second.data()), std::streamsize(kept->second.size()));
                        std::ofstream(base.string() + "_trial.bin", std::ios::binary)
                            .write(reinterpret_cast<const char*>(mine.data()), std::streamsize(mine.size()));
                    }
                    std::cout << "  trial at " << frame << ": the state differs first at frame " << f << " in "
                              << first_differing_section(kept->second, mine) << std::endl;
                }
            }
            if (straight.fingerprints.at(f) != engine.net_fingerprint()) {
                equal = false;
                if (reported < 8 && straight.states.count(f))
                    std::cout << "  (at " << f << " the trial engine is " << (engine.state_safe() ? std::string("safe") : engine.state_unsafe_reason())
                              << ", its state " << (engine.state_safe() && engine.save_tick_state() == straight.states.at(f) ? "equal" : "other") << ")" << std::endl;
                if (reported++ < 8)
                    std::cout << "  FAIL " << sc.stage << ": trial at " << frame << " (window " << window << ") differs from frame " << f
                              << (topology_changed ? " (a node was made in the window)" : "") << '\n';
                break;
            }
        }
        if (equal) ++t.passed;
        else if (!topology_changed) {
            ++failures;
            break; // the engine is off the straight run now
        } else {
            break;
        }
    }
    engine.set_online(OnlineRules{});
}

// 6.13.2 steps 1-5: a fresh engine from the visit's load snapshot, its load
// ticks, the state of T applied, then the true pads.
constexpr int kRebuildReplaySteps = 20;  // the joiner's catch-up from T (replay mode)
constexpr int kRebuildHashSteps = 1200; // its STATEHASH compared over two periods
void run_rebuild(const std::filesystem::path& data, const Scenario& sc, const Straight& straight, std::uint64_t at,
                 const std::vector<std::uint8_t>& bytes, Trials& t) {
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_tick_state_tests_cards");
    engine.restore(decode_snapshot(straight.snapshot));
    if (!engine.held()) engine.traverse_scene();
    engine.set_split_identity(0);
    engine.set_replay(true);
    for (const auto& pads : straight.load_log) host_step(engine, pads);
    engine.set_replay(false);
    soft(engine.frame() == straight.load_end, sc.stage + ": the rebuild reaches the load end (" + std::to_string(engine.frame()) + " vs " +
                                                  std::to_string(straight.load_end) + ")");
    soft(engine.load_end_frame() == straight.load_end, sc.stage + ": the rebuild's own load end equals K");
    try {
        engine.apply_tick_state(bytes);
    } catch (const TickStateError& e) {
        soft(false, sc.stage + ": rebuild apply of " + std::to_string(at) + ": " + e.what());
        engine.set_online(OnlineRules{});
        return;
    }
    soft(engine.frame() == at, sc.stage + ": the rebuilt engine is at T");
    const auto again = engine.save_tick_state();
    soft(again == bytes, sc.stage + ": the rebuild saves T's bytes (" + (again == bytes ? std::string() : first_differing_section(bytes, again)) + ")");
    // As a joiner: its first steps from T are a catch-up in replay mode, then
    // it plays live; its STATEHASH reports from its join on are compared with
    // the members' (6.13.4). Since protocol 48 it joins about 25 ticks after T,
    // so its first report may come right after the rebuild (S3: c rebuilt at
    // 1753, its STATEHASH of 1800 differed while every NETFP was equal).
    engine.set_replay(true);
    int steps = 0, hashes = 0;
    bool hash_failed = false;
    while (engine.frame() < straight.fingerprints.rbegin()->first && !(dense_rebuilds() && steps >= kRebuildHashSteps)) {
        if (steps == kRebuildReplaySteps) engine.set_replay(false);
        if (straight.joined && engine.frame() == straight.joined) engine.online_join_player(sc.players, joiner_carry(), 0);
        host_step(engine, sc.pads(engine.frame()));
        ++steps;
        if (!straight.fingerprints.contains(engine.frame())) break;
        if (straight.fingerprints.at(engine.frame()) != engine.net_fingerprint()) {
            soft(false, sc.stage + ": the rebuild of " + std::to_string(at) + " differs from frame " + std::to_string(engine.frame()));
            engine.set_online(OnlineRules{});
            return;
        }
        if (const auto h = straight.state_hashes.find(engine.frame());
            !hash_failed && steps <= kRebuildHashSteps && h != straight.state_hashes.end() && engine.state_safe()) {
            ++hashes;
            if (engine.tick_state_hash() != h->second) {
                hash_failed = true;
                std::string where;
                if (const auto kept = straight.states.find(engine.frame()); kept != straight.states.end()) {
                    const auto mine = engine.save_tick_state();
                    where = " in " + first_differing_section(kept->second, mine);
                    // DW4_TICKSTATE_DUMP=DIR: both states for tools/online/tickstate_diff.py.
                    if (const char* dir = std::getenv("DW4_TICKSTATE_DUMP")) {
                        const auto base =
                            std::filesystem::path(dir) / (sc.stage + "_rebuild_" + std::to_string(at) + "_" + std::to_string(engine.frame()));
                        std::ofstream(base.string() + "_straight.bin", std::ios::binary)
                            .write(reinterpret_cast<const char*>(kept->second.data()), std::streamsize(kept->second.size()));
                        std::ofstream(base.string() + "_rebuild.bin", std::ios::binary)
                            .write(reinterpret_cast<const char*>(mine.data()), std::streamsize(mine.size()));
                    }
                }
                soft(false, sc.stage + ": the rebuild of " + std::to_string(at) + " has another STATEHASH at frame " +
                                std::to_string(engine.frame()) + " (" + std::to_string(steps) + " steps after T" + where + ")");
            }
        }
    }
    ++t.rebuilds;
    std::cout << "  rebuild of " << at << ": equal for " << steps << " steps, " << hashes << " STATEHASH compared\n";
    engine.set_online(OnlineRules{});
}

// A classic joiner (the catch-up of today, 6.6): the visit's load snapshot
// and every tick since in replay mode. Its canonical tick state must equal
// the straight run's at the same frames: the members' STATEHASH reports of
// an instance a member joined by catch-up are compared (6.13.4).
void run_classic(const std::filesystem::path& data, const Scenario& sc, const Straight& straight) {
    if (straight.canonical.empty()) return;
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_tick_state_tests_cards");
    engine.restore(decode_snapshot(straight.snapshot));
    if (!engine.held()) engine.traverse_scene();
    engine.set_split_identity(-1);
    engine.set_replay(true);
    int compared = 0;
    std::size_t at = 0;
    while (at < straight.visit_log.size() && engine.frame() < straight.canonical.rbegin()->first) {
        if (straight.joined && engine.frame() == straight.joined) engine.online_join_player(sc.players, joiner_carry(), 0);
        host_step(engine, straight.visit_log[at++]);
        if (const auto it = straight.canonical.find(engine.frame()); it != straight.canonical.end()) {
            ++compared;
            if (!engine.state_safe()) {
                soft(false, sc.stage + ": the classic joiner is not state-safe at " + std::to_string(it->first) + " (" + engine.state_unsafe_reason() + ")");
                continue;
            }
            const auto mine = engine.save_tick_state(TickForm::canonical);
            if (mine != it->second) {
                soft(false, sc.stage + ": the classic joiner's canonical state differs at " + std::to_string(it->first) + " in " +
                                first_differing_section(it->second, mine));
                if (const char* dir = std::getenv("DW4_TICKSTATE_DUMP")) {
                    const auto base = std::filesystem::path(dir) / (sc.stage + "_classic_" + std::to_string(it->first));
                    std::ofstream(base.string() + "_straight.bin", std::ios::binary)
                        .write(reinterpret_cast<const char*>(it->second.data()), std::streamsize(it->second.size()));
                    std::ofstream(base.string() + "_classic.bin", std::ios::binary)
                        .write(reinterpret_cast<const char*>(mine.data()), std::streamsize(mine.size()));
                }
                break;
            }
        }
    }
    std::cout << "  classic joiner: " << compared << " canonical states compared" << std::endl;
    engine.set_online(OnlineRules{});
}

// Round I1b's integration (fix-019's S22b): what a stage load keeps must be in
// the stage snapshot. An engine plays stag0101 with its random WAITCHECK
// minimum set (stag0101's WAITCHECK 'R' with 'S' sets 120, the original's
// global 80535A60) and changes to stag0102; a fresh engine restores the
// change's snapshot and replays the steps since (a classic joiner, a save
// state, a fallback): its canonical tick state equals the straight engine's at
// state-safe frames after the load end. Before, its minimum was 0 there.
void stage_change_restore(const std::filesystem::path& data) {
    const auto t0 = std::chrono::steady_clock::now();
    Scenario sc{"stag0101", 2, {"p_agumon", "p_dorumon"}};
    std::unique_ptr<Engine> holder;
    Engine& engine = start(holder, data, sc);
    std::uint64_t steps = 0;
    while (!engine.load_end_frame() && steps < 3000) {
        host_step(engine, sc.pads(engine.frame()));
        ++steps;
    }
    check(engine.load_end_frame() != 0, "stage change restore: stag0101 reached its load end");
    for (int i = 0; i < 200; ++i) host_step(engine, sc.pads(engine.frame()));
    auto* actions = engine.get<ActionManager>();
    check(actions != nullptr, "stage change restore: the action manager");
    actions->set_random_wait_min(120.0f);
    const auto serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
    engine.request_stage("stag0102");
    std::vector<std::uint8_t> snapshot;
    std::vector<std::array<PadSample, 4>> visit;
    std::map<std::uint64_t, std::vector<std::uint8_t>> canonical;
    std::uint64_t load_end = 0;
    for (steps = 0; steps < 4000 && canonical.size() < 3; ++steps) {
        const auto pads = sc.pads(engine.frame());
        host_step(engine, pads);
        if (snapshot.empty()) {
            if (const auto* s = engine.stage_snapshot(); s && s->serial != serial) snapshot = engine.snapshot_bytes(SnapshotForm::raw);
            continue;
        }
        visit.push_back(pads);
        if (!load_end && engine.load_end_frame()) load_end = engine.load_end_frame();
        if (load_end && engine.frame() >= load_end + 60 && engine.frame() % 100 == 0 && engine.state_safe())
            canonical[engine.frame()] = engine.save_tick_state(TickForm::canonical);
    }
    check(!snapshot.empty() && load_end != 0 && !canonical.empty(), "stage change restore: stag0102 loaded");
    check(decode_snapshot(snapshot).random_wait_min == 120.0f, "stage change restore: the snapshot keeps the random WAITCHECK minimum");
    const auto straight_min = actions->random_wait_min();
    engine.set_online(OnlineRules{});
    holder.reset();

    Engine restored(data);
    restored.set_card_root(std::filesystem::temp_directory_path() / "dw4_tick_state_tests_cards");
    restored.restore(decode_snapshot(snapshot));
    if (!restored.held()) restored.traverse_scene();
    restored.set_split_identity(-1);
    restored.set_replay(true);
    int compared = 0;
    for (std::size_t at = 0; at < visit.size() && restored.frame() < canonical.rbegin()->first; ++at) {
        host_step(restored, visit[at]);
        if (const auto it = canonical.find(restored.frame()); it != canonical.end()) {
            ++compared;
            const auto mine = restored.save_tick_state(TickForm::canonical);
            soft(mine == it->second, "stage change restore: the restored engine's tick state at " + std::to_string(it->first) +
                                         (mine == it->second ? std::string() : " differs in " + first_differing_section(it->second, mine)));
        }
    }
    soft(compared == int(canonical.size()), "stage change restore: every kept state compared");
    soft(restored.get<ActionManager>()->random_wait_min() == straight_min, "stage change restore: the restored minimum equals the straight one");
    restored.set_online(OnlineRules{});
    std::cout << "stage change restore (stag0101 -> stag0102, minimum 120): " << compared << " states compared; " << ms_since(t0) / 1000.0
              << " s\n";
}

void run(const std::filesystem::path& data, Scenario sc) {
    const auto t0 = std::chrono::steady_clock::now();
    for (auto& c : sc.stage) c = char(std::tolower(static_cast<unsigned char>(c))); // the engine's stage names
    const auto straight = run_straight(data, sc);
    check(straight.load_end != 0, sc.stage + ": the visit reached its load end");
    Trials t;
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> kept;
    if (sc.dump_to) std::cout << "  kept " << straight.states.size() << " straight states for diagnosis" << std::endl;
    run_trials(data, sc, straight, t, kept);
    for (const auto& [at, bytes] : kept) run_rebuild(data, sc, straight, at, bytes, t);
    run_classic(data, sc, straight);
    std::cout << sc.stage << " x" << sc.players << ": load end " << straight.load_end << ", " << t.saved << " states (" << t.idempotent
              << " idempotent), " << t.trials << " trials (" << t.passed << " equal, " << t.skipped << " with a node made), " << t.rebuilds
              << " rebuilds; max " << t.bytes / 1024 << " KB, save " << (t.saved ? t.save_ms / t.saved : 0) << " ms, hash "
              << (t.saved ? t.hash_ms / t.saved : 0) << " ms, apply " << (t.saved ? t.apply_ms / t.saved : 0) << " ms; "
              << ms_since(t0) / 1000.0 << " s\n";
    for (const auto& [why, n] : t.unsafe) std::cout << "  unsafe ticks: " << why << " " << n << '\n';
    soft(t.saved > 0, sc.stage + ": states were taken");
}

} // namespace

int main(int argc, char** argv) {
    try {
        hash_vectors();
        refusals();
        std::string data = argc > 1 ? argv[1] : "";
        if (data.empty())
            if (const char* env = std::getenv("DW4_TEST_DATA")) data = env;
        if (!data.empty() && std::filesystem::exists(data)) {
            std::vector<Scenario> set;
            if (argc > 5) {
                Scenario sc;
                sc.stage = argv[2];
                sc.players = std::atoi(argv[3]);
                sc.steps = std::uint64_t(std::atoll(argv[4]));
                sc.every = std::uint64_t(std::atoll(argv[5]));
                sc.species = {"p_agumon", "p_dorumon", "p_vmon", "p_girumon"};
                if (argc > 6) sc.menu_at = std::uint64_t(std::atoll(argv[6]));
                if (argc > 8) {
                    sc.dump_from = std::uint64_t(std::atoll(argv[7]));
                    sc.dump_to = std::uint64_t(std::atoll(argv[8]));
                }
                if (argc > 9) sc.join_at = std::uint64_t(std::atoll(argv[9]));
                set.push_back(sc);
            } else {
                // (stage, players, species, steps, trial spacing, own window at, dump range, join at)
                set.push_back({"stag0101", 2, {"p_agumon", "p_dorumon"}, 3000, 61, 0, 0, 0, 1200});
                set.push_back({"dng101b1", 2, {"p_agumon", "p_vmon"}, 4000, 61, 700});
                // (The rebuild of 3405: Dorumon's hit script runs 15 steps later, a chunk
                // Girumon's shares and compiled first, at the join; lane jready's review.)
                set.push_back({"stag0102", 3, {"p_agumon", "p_dorumon", "p_vmon"}, 4000, 61, 1600, 0, 0, 900, 3400});
                set.push_back({"stagBS02", 4, {"p_agumon", "p_dorumon", "p_vmon", "p_girumon"}, 3000, 61});
                // Round J's review (STATEHASH runs in every field stage, not only these): a dungeon's boss
                // floor, a boss stage and a quest's boss floor (its kill writes a context flag).
                set.push_back({"dng102b3", 2, {"p_agumon", "p_vmon"}, 3000, 61});
                set.push_back({"stagBS01", 2, {"p_dorumon", "p_girumon"}, 3000, 61});
                set.push_back({"dngq1b2e", 2, {"p_agumon", "p_dorumon"}, 3000, 61});
            }
            if (argc <= 5) stage_change_restore(data);
            for (const auto& sc : set) run(data, sc);
        } else {
            std::cout << "tick state scenarios skipped (no game data: DW4_TEST_DATA)\n";
        }
        std::cout << checks << " tick state checks, " << failures << " failures\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Tick state test failure: " << error.what() << '\n';
        return 1;
    }
}
