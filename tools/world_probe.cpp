// Round I1a's first tasks (docs/SEAMLESS.md 19.25, "measured r71"): headless
// measurements with the game data for the world without rooms. A development
// tool, not a test: it drives engines directly (one at a time, as the online
// rules require), prints what it measured and asserts nothing. It writes
// nothing but its output (the memory card folder is a throwaway one under the
// system temp folder).
//
//   dw4_world_probe DATA settings   which settings a GameEngineLink restore takes,
//                                   and what a host's other settings do to a joiner (19.8.7)
//   dw4_world_probe DATA redirect   a redirect of a parked stage change against a
//                                   NEXTSTAGE to that stage and point (19.8.3)
//   dw4_world_probe DATA guestnew   a guest's NEW (the replace with the host's story and settings)
//                                   against a Game Start with them (19.30.4)
//   dw4_world_probe DATA go [CASE]  round I1b's first task (19.30.7): the GO departure against
//                                   the exit port's own departure to the same stage and point
//                                   (both copies of the fork, replays, the zone shuffle off and on)
//   dw4_world_probe DATA homeover [CASE]  a stage group left with only defeated players
//                                   after a HOME, with the alive mask of every stage change, and the
//                                   original's game over offline (19.6.5)
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_membership.hpp"
#include "dw4/engine/randomizer.hpp"
#include "dw4/engine/tick_state.hpp"
#include "online/engine_adapter.hpp"
#include "online/room_settings.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define DW4_GETPID _getpid
#else
#include <unistd.h>
#define DW4_GETPID getpid
#endif

using namespace dw4::engine;
using dw4::PadSample;
using dw4::game::online::RoomSettings;

namespace {

std::filesystem::path g_data;

std::filesystem::path card_root() {
    return std::filesystem::temp_directory_path() / ("dw4_world_probe_cards_" + std::to_string(DW4_GETPID()));
}

void new_engine(std::optional<Engine>& engine) {
    engine.reset();
    engine.emplace(g_data);
    engine->set_card_root(card_root());
}

// Walk in a slot-specific direction that turns every few seconds, attack and
// jump now and then (the pads of tests/online_tests.cpp).
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
    return p;
}

std::array<PadSample, 4> pads_at(std::uint64_t frame) {
    std::array<PadSample, 4> pads{};
    for (int s = 0; s < 4; ++s) pads[std::size_t(s)] = pad_for(frame, s);
    return pads;
}

void step(Engine& engine, const std::array<PadSample, 4>& pads) {
    engine.step(pads);
    if (!engine.held()) engine.traverse_scene();
}

// A departure as the original's records end it (online_exits.cpp
// departure_work): SETGENERATEPOS, then NEXTSTAGE, delivered in the next step.
void post_departure(Engine& engine, const std::string& stage, int point) {
    Packet at;
    at.ints.push_back(point);
    engine.messenger().post(engine.time() + kFrameNs, "PLAYERPOINT", std::nullopt, -1, "SETGENERATEPOS", std::move(at));
    Packet next;
    next.strings.emplace_back(stage);
    engine.messenger().post(engine.time() + kFrameNs, "STAGECTL", std::nullopt, -1, "NEXTSTAGE", std::move(next));
}

std::string settings_text(const RoomSettings& s) {
    char line[160];
    std::snprintf(line, sizeof line, "diff %d cam %d light %d fast %d rand %u/%d/%d/%d", s.difficulty, s.camera_mode, s.light_mode,
                  s.fast_loading ? 1 : 0, s.rand_seed, s.rand_enemies, s.rand_zones, s.rand_drops ? 1 : 0);
    return line;
}

std::vector<std::string> file_names(const Engine& engine) {
    std::vector<std::string> names;
    for (const auto& f : engine.loaded_files()) names.push_back(f.logical_path);
    return names;
}

// ---------------------------------------------------------------------------
// settings (19.8.7)
// ---------------------------------------------------------------------------
// The host: a Game Start with settings H (the modes in GameSystem as
// GameEngineLink::after_live_step writes them, the cheats as start_online
// sets them), direct into stag0101; at frame 1100 the field's dungeon
// entrance (SETGENERATEPOS 0, NEXTSTAGE dng101b1, which the zone shuffle
// remaps); the joiner restores the stag0101 load snapshot through
// GameEngineLink::restore with its own settings J and replays the host's pads.
struct HostRun {
    std::vector<std::uint8_t> load_bytes;               // the stag0101 load snapshot
    std::uint64_t load_frame{};
    std::vector<std::array<PadSample, 4>> log;          // every step after the load step
    std::map<std::uint64_t, std::uint64_t> fingerprints; // post-step frame -> net fingerprint
    std::uint64_t post_frame{};                         // the pre-step frame the departure was posted at (the joiner posts it too)
    std::uint64_t change_frame{};                       // the step whose NEXTSTAGE made the stage change pending
    std::string second_stage;                          // the stage the change loaded (a zone remap shows here)
    std::uint64_t second_load_frame{};
    std::vector<std::string> first_files, second_files; // the loaded files at the end of each load
    std::uint64_t load_end{};                            // the stag0101 visit's load end (round J's K, as a frame)
    std::vector<std::uint8_t> tick_bytes;                // round J's checkpoint at tick_frame
    std::uint64_t tick_hash{}, tick_frame{};
};

void set_modes(Engine& engine, const RoomSettings& s) {
    if (auto* sys = engine.get<GameSystem>()) {
        sys->difficulty = s.difficulty;
        sys->camera_mode = s.camera_mode;
        sys->light_mode = s.light_mode;
    }
}

HostRun run_host(const RoomSettings& h, std::uint64_t steps) {
    HostRun run;
    std::optional<Engine> holder;
    new_engine(holder);
    auto& engine = *holder;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].joined = true;
    set_modes(engine, h);
    dw4::game::online::apply_room_cheats(h, engine.cheats());
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 1;
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.set_view_slot(0);
    engine.request_stage("stag0101");
    std::uint64_t serial = 0;
    bool posted = false, first_loaded = false, second_loaded = false;
    for (std::uint64_t i = 0; i < steps; ++i) {
        const std::uint64_t frame = engine.frame();
        if (!posted && run.load_frame && frame >= run.load_frame + 1100 && engine.membership_safe()) {
            post_departure(engine, "dng101b1", 0);
            run.post_frame = frame;
            posted = true;
        }
        const auto pads = pads_at(frame);
        const bool before = engine.pending_stage().has_value();
        step(engine, pads);
        if (!before && engine.pending_stage() && posted && !run.change_frame) run.change_frame = engine.frame();
        const auto* snapshot = engine.stage_snapshot();
        if (snapshot && snapshot->serial != serial) {
            serial = snapshot->serial;
            if (run.load_bytes.empty()) {
                run.load_bytes = engine.snapshot_bytes(SnapshotForm::raw);
                run.load_frame = engine.frame();
            } else {
                run.second_stage = snapshot->stage;
                run.second_load_frame = engine.frame();
                run.log.push_back(pads);
            }
        } else if (!run.load_bytes.empty()) {
            run.log.push_back(pads);
        }
        if (!first_loaded && run.load_frame && !engine.loading() && engine.stage_name() == "stag0101") {
            first_loaded = true;
            run.first_files = file_names(engine);
        }
        if (!second_loaded && run.second_load_frame && !engine.loading()) {
            second_loaded = true;
            run.second_files = file_names(engine);
        }
        if (!run.load_bytes.empty()) run.fingerprints[engine.frame()] = engine.net_fingerprint();
        if (!run.load_end && engine.stage_name() == "stag0101" && engine.load_end_frame()) run.load_end = engine.load_end_frame();
        // Round J's checkpoint at T = load + 900 (the first state-safe step).
        if (run.tick_bytes.empty() && run.load_frame && engine.frame() >= run.load_frame + 900 && !posted && engine.state_safe()) {
            run.tick_bytes = engine.save_tick_state(TickForm::raw);
            run.tick_hash = engine.tick_state_hash();
            run.tick_frame = engine.frame();
        }
    }
    engine.set_online(OnlineRules{});
    return run;
}

struct JoinResult {
    std::uint64_t compared{};
    std::uint64_t first_diff{}; // 0: none
    std::string second_stage;
    bool files_differ = false;
    std::string why;
};

enum class JoinModes { snapshot, own };

JoinResult run_joiner(const HostRun& host, const RoomSettings& j, JoinModes modes) {
    JoinResult r;
    std::optional<Engine> holder;
    dw4::game::online::EngineHooks hooks;
    hooks.engine = [&]() -> Engine& { return *holder; };
    hooks.fresh_engine = [&] { new_engine(holder); };
    dw4::game::online::GameEngineLink link(hooks, j);
    if (!link.restore(host.load_bytes)) {
        r.why = "restore failed";
        return r;
    }
    auto& engine = *holder;
    // The joiner's own modes after the restore (what the joiner would hold if
    // its own difficulty, camera and light mode were applied, as its Game
    // Start does): the stage's file list reads them during the load.
    if (modes == JoinModes::own) set_modes(engine, j);
    std::uint64_t serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
    bool second_loaded = false;
    std::vector<std::string> second_files;
    for (const auto& pads : host.log) {
        // (The departure is the probe's own message, not in the pads: the
        // joiner gets it at the same frame, as a script would post it.)
        if (engine.frame() == host.post_frame) post_departure(engine, "dng101b1", 0);
        step(engine, pads);
        const auto* snapshot = engine.stage_snapshot();
        if (snapshot && snapshot->serial != serial) {
            serial = snapshot->serial;
            r.second_stage = snapshot->stage;
        }
        if (!second_loaded && !r.second_stage.empty() && !engine.loading()) {
            second_loaded = true;
            second_files = file_names(engine);
        }
        const auto it = host.fingerprints.find(engine.frame());
        if (it == host.fingerprints.end()) continue;
        ++r.compared;
        if (!r.first_diff && it->second != engine.net_fingerprint()) r.first_diff = engine.frame();
    }
    r.files_differ = second_loaded && second_files != host.second_files;
    engine.set_online(OnlineRules{});
    return r;
}

// Round J's join from a checkpoint (Session's checkpoint path): the load
// snapshot through GameEngineLink::restore, the visit's load ticks up to its
// load end K, then the tick state at T, its hash, and the ticks from T.
JoinResult run_checkpoint_joiner(const HostRun& host, const RoomSettings& j) {
    JoinResult r;
    if (host.tick_bytes.empty() || !host.load_end) {
        r.why = "no checkpoint";
        return r;
    }
    std::optional<Engine> holder;
    dw4::game::online::EngineHooks hooks;
    hooks.engine = [&]() -> Engine& { return *holder; };
    hooks.fresh_engine = [&] { new_engine(holder); };
    dw4::game::online::GameEngineLink link(hooks, j);
    if (!link.restore(host.load_bytes)) {
        r.why = "restore failed";
        return r;
    }
    auto& engine = *holder;
    std::size_t k = 0;
    while (k < host.log.size() && !engine.load_end_frame()) step(engine, host.log[k++]);
    if (engine.load_end_frame() != host.load_end) {
        r.why = "load end at " + std::to_string(engine.load_end_frame()) + ", the host's at " + std::to_string(host.load_end);
        return r;
    }
    try {
        engine.apply_tick_state(host.tick_bytes);
    } catch (const std::exception& e) {
        r.why = std::string("apply_tick_state refused: ") + e.what();
        return r;
    }
    if (engine.tick_state_hash() != host.tick_hash) r.why = "tick state hash differs after the apply; ";
    for (std::size_t i = std::size_t(host.tick_frame - host.load_frame); i < host.log.size(); ++i) {
        if (engine.frame() == host.post_frame) post_departure(engine, "dng101b1", 0);
        step(engine, host.log[i]);
        const auto* snapshot = engine.stage_snapshot();
        if (snapshot && snapshot->stage != "stag0101") r.second_stage = snapshot->stage;
        const auto it = host.fingerprints.find(engine.frame());
        if (it == host.fingerprints.end()) continue;
        ++r.compared;
        if (!r.first_diff && it->second != engine.net_fingerprint()) r.first_diff = engine.frame();
    }
    engine.set_online(OnlineRules{});
    return r;
}

void settings_probe() {
    RoomSettings base; // Normal, camera 0, light 1, original loading, no randomizer
    RoomSettings fast = base;
    fast.fast_loading = true;
    RoomSettings rand = base;
    rand.rand_seed = 4242;
    rand.rand_enemies = 1;
    rand.rand_zones = 1;
    rand.rand_drops = true;
    RoomSettings rand_other = rand;
    rand_other.rand_seed = 777;
    RoomSettings drops = base;
    drops.rand_seed = 4242;
    drops.rand_drops = true;
    RoomSettings enemies = base;
    enemies.rand_seed = 4242;
    enemies.rand_enemies = 2;
    RoomSettings zones = base;
    zones.rand_seed = 4242;
    zones.rand_zones = 1;
    RoomSettings hard = base;
    hard.difficulty = 2;
    RoomSettings camera = base;
    camera.camera_mode = 1;
    RoomSettings light = base;
    light.light_mode = 0;
    RoomSettings all = rand;
    all.difficulty = 2;
    all.camera_mode = 1;
    all.light_mode = 0;
    all.fast_loading = true;

    struct Case {
        const char* name;
        RoomSettings host, joiner;
        JoinModes modes;
    };
    const Case cases[] = {
        {"control: same settings", base, base, JoinModes::snapshot},
        {"control: all non-default on both", all, all, JoinModes::snapshot},
        {"joiner fast loading", base, fast, JoinModes::snapshot},
        {"host fast loading", fast, base, JoinModes::snapshot},
        {"joiner randomizer (all)", base, rand, JoinModes::snapshot},
        {"host randomizer (all)", rand, base, JoinModes::snapshot},
        {"other seed", rand, rand_other, JoinModes::snapshot},
        {"host drops only", drops, base, JoinModes::snapshot},
        {"host enemies only", enemies, base, JoinModes::snapshot},
        {"host zones only", zones, base, JoinModes::snapshot},
        {"host Very Hard, joiner Normal (link)", hard, base, JoinModes::snapshot},
        {"host Very Hard, joiner Normal (own modes)", hard, base, JoinModes::own},
        {"host camera 1, joiner 0 (link)", camera, base, JoinModes::snapshot},
        {"host camera 1, joiner 0 (own modes)", camera, base, JoinModes::own},
        {"host light 0, joiner 1 (link)", light, base, JoinModes::snapshot},
        {"host light 0, joiner 1 (own modes)", light, base, JoinModes::own},
        {"host all, joiner default (link)", all, base, JoinModes::snapshot},
    };
    constexpr std::uint64_t kSteps = 4200;
    std::map<std::string, HostRun> hosts;
    std::vector<std::string> base_files;
    std::printf("settings probe: host Game Start with H direct into stag0101, dungeon entrance at load + 1100, %llu steps;\n"
                "joiner GameEngineLink::restore of the stag0101 load snapshot with its own J, the host's pads replayed\n",
                static_cast<unsigned long long>(kSteps));
    for (const auto& c : cases) {
        const auto key = settings_text(c.host);
        if (!hosts.contains(key)) {
            const auto t0 = std::chrono::steady_clock::now();
            hosts[key] = run_host(c.host, kSteps);
            const auto& h = hosts[key];
            std::printf("  host [%s]: stag0101 load step %llu, change %llu -> %s (load step %llu), %.1f s\n", key.c_str(),
                        static_cast<unsigned long long>(h.load_frame), static_cast<unsigned long long>(h.change_frame), h.second_stage.c_str(),
                        static_cast<unsigned long long>(h.second_load_frame),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            std::string camera_file, generator_file;
            for (const auto& f : h.first_files) {
                if (f.find("aaaCamera") != std::string::npos) camera_file = f;
                if (f.find("generator") != std::string::npos) generator_file = f;
            }
            std::printf("    stag0101 files %zu (camera %s, generator %s), %s files %zu\n", h.first_files.size(), camera_file.c_str(),
                        generator_file.c_str(), h.second_stage.c_str(), h.second_files.size());
            // The files against the first host's (the defaults).
            if (base_files.empty()) {
                base_files = h.first_files;
                for (const auto& f : h.first_files) std::printf("      %s\n", f.c_str());
            } else {
                for (const auto& f : h.first_files)
                    if (std::find(base_files.begin(), base_files.end(), f) == base_files.end()) std::printf("      + %s\n", f.c_str());
                for (const auto& f : base_files)
                    if (std::find(h.first_files.begin(), h.first_files.end(), f) == h.first_files.end()) std::printf("      - %s\n", f.c_str());
            }
        }
        const auto& host = hosts[key];
        const auto r = run_joiner(host, c.joiner, c.modes);
        if (!r.why.empty()) {
            std::printf("  %-44s %s\n", c.name, r.why.c_str());
            continue;
        }
        std::string where = "equal";
        if (r.first_diff) {
            char text[160];
            if (r.first_diff < host.change_frame)
                std::snprintf(text, sizeof text, "differs from frame %llu (load + %llu, before the stage change)",
                              static_cast<unsigned long long>(r.first_diff), static_cast<unsigned long long>(r.first_diff - host.load_frame));
            else
                std::snprintf(text, sizeof text, "differs from frame %llu (stage change + %llu)", static_cast<unsigned long long>(r.first_diff),
                              static_cast<unsigned long long>(r.first_diff - host.change_frame));
            where = text;
        }
        std::printf("  %-44s J [%s]: %llu frames compared, %s; second stage %s%s%s\n", c.name, settings_text(c.joiner).c_str(),
                    static_cast<unsigned long long>(r.compared), where.c_str(), r.second_stage.c_str(),
                    r.second_stage != host.second_stage ? " (host: " : "", r.second_stage != host.second_stage ? (host.second_stage + ")").c_str() : "");
        if (r.files_differ) std::printf("    the second stage's loaded files differ from the host's\n");
    }
    // Round J's checkpoint joins with the same pairs.
    const Case checkpoint_cases[] = {
        {"checkpoint: same settings", base, base, JoinModes::snapshot},
        {"checkpoint: all non-default on both", all, all, JoinModes::snapshot},
        {"checkpoint: joiner fast loading", base, fast, JoinModes::snapshot},
        {"checkpoint: joiner randomizer (all)", base, rand, JoinModes::snapshot},
        {"checkpoint: host randomizer (all)", rand, base, JoinModes::snapshot},
        {"checkpoint: host enemies only", enemies, base, JoinModes::snapshot},
        {"checkpoint: host Very Hard, joiner Normal", hard, base, JoinModes::snapshot},
        {"checkpoint: host all, joiner default", all, base, JoinModes::snapshot},
    };
    for (const auto& c : checkpoint_cases) {
        const auto& host = hosts[settings_text(c.host)];
        const auto r = run_checkpoint_joiner(host, c.joiner);
        std::printf("  %-44s J [%s]: K %llu, T %llu: %s%llu frames compared, %s; second stage %s (host %s)\n", c.name, settings_text(c.joiner).c_str(),
                    static_cast<unsigned long long>(host.load_end), static_cast<unsigned long long>(host.tick_frame), r.why.c_str(),
                    static_cast<unsigned long long>(r.compared),
                    r.first_diff ? ("differs from frame " + std::to_string(r.first_diff) +
                                    (r.first_diff >= host.change_frame ? " (stage change + " + std::to_string(r.first_diff - host.change_frame) + ")" : ""))
                                       .c_str()
                                 : "equal",
                    r.second_stage.c_str(), host.second_stage.c_str());
    }
    // DISSOLVE: the own settings set back in the parked stage change (before
    // the module reload and the load step) against the same departure with
    // the own settings all along: the module reload's length (park to load
    // step), the files the stagml11 load reads, the fast loading the load
    // latched and the settings the engine holds after the load. (Fingerprints
    // of these runs are not comparable: their stag0101 visits ran on
    // different settings, so their histories differ.)
    std::printf("dissolve: settings H while in stag0101, the own settings O set while the stage change to stagml11 is parked\n");
    struct Lobby {
        std::uint64_t park{}, load{}, loaded{};
        std::vector<std::string> files;
        bool fast_latch{};
        std::string held;
    };
    auto lobby = [&](const RoomSettings& start, const std::optional<RoomSettings>& at_park) {
        Lobby r;
        std::optional<Engine> holder;
        new_engine(holder);
        auto& engine = *holder;
        auto* sys = engine.get<GameSystem>();
        sys->direct_start = true;
        sys->players[0].joined = true;
        set_modes(engine, start);
        dw4::game::online::apply_room_cheats(start, engine.cheats());
        OnlineRules rules;
        rules.enabled = true;
        rules.present = 1;
        engine.set_online(rules);
        engine.set_split_identity(0);
        engine.request_stage("stag0101");
        bool posted = false;
        std::uint64_t serial = 0;
        for (int i = 0; i < 6000; ++i) {
            if (!posted && engine.frame() > 1200 && engine.stage_name() == "stag0101" && engine.membership_safe()) {
                post_departure(engine, "stagML11", 3);
                posted = true;
            }
            if (posted && !r.park && engine.pending_stage()) {
                r.park = engine.frame();
                serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
                BarrierResult b;
                b.decision = Decision::dissolve;
                b.own_slot = 0;
                b.rules = engine.online();
                engine.set_barrier(b);
                if (at_park) {
                    set_modes(engine, *at_park);
                    dw4::game::online::apply_room_cheats(*at_park, engine.cheats());
                }
            }
            step(engine, pads_at(engine.frame()));
            if (r.park && !r.load && engine.stage_snapshot() && engine.stage_snapshot()->serial != serial) {
                r.load = engine.frame();
                r.fast_latch = engine.fast_loading();
            }
            if (r.load && engine.stage_name() == "stagml11" && !engine.loading()) {
                r.loaded = engine.frame();
                r.files = file_names(engine);
                break;
            }
        }
        const auto* s = engine.get<GameSystem>();
        const auto& c = engine.cheats();
        char text[160];
        std::snprintf(text, sizeof text, "diff %d cam %d light %d fast %d rand %u/%d/%d drops %u", s->difficulty, s->camera_mode, s->light_mode,
                      c.fast_loading ? 1 : 0, c.rand_seed, c.rand_enemies, c.rand_zones, c.drop_seed);
        r.held = text;
        engine.set_online(OnlineRules{});
        return r;
    };
    auto describe = [](const Lobby& l) {
        std::string camera;
        for (const auto& f : l.files)
            if (f.find("aaaCamera") != std::string::npos) camera += f + " ";
        return "module reload " + std::to_string(l.load - l.park) + " steps (park " + std::to_string(l.park) + ", load step " + std::to_string(l.load) +
               "), load latched fast loading " + (l.fast_latch ? "on" : "off") + ", loaded at " + std::to_string(l.loaded) + ", " +
               std::to_string(l.files.size()) + " files (camera " + camera + "), holds [" + l.held + "]";
    };
    RoomSettings all_but_fast = all;
    all_but_fast.fast_loading = false;
    const std::pair<RoomSettings, RoomSettings> pairs[] = {{all, base}, {base, all}, {all_but_fast, base}};
    for (const auto& [host, own] : pairs) {
        std::printf("  H [%s], O [%s]\n", settings_text(host).c_str(), settings_text(own).c_str());
        const auto direct_own = lobby(own, std::nullopt);
        const auto host_kept = lobby(host, std::nullopt);
        const auto set_back = lobby(host, own);
        std::printf("    O all along:          %s\n", describe(direct_own).c_str());
        std::printf("    H kept:               %s\n", describe(host_kept).c_str());
        std::printf("    H, O set at the park: %s\n", describe(set_back).c_str());
        const bool same = set_back.load - set_back.park == direct_own.load - direct_own.park && set_back.files == direct_own.files &&
                          set_back.fast_latch == direct_own.fast_latch && set_back.held == direct_own.held;
        std::printf("    set back equals O all along (reload length, files, latch, settings): %s\n", same ? "yes" : "no");
        for (std::size_t k = 0; k < std::max(set_back.files.size(), direct_own.files.size()); ++k) {
            const std::string a = k < set_back.files.size() ? set_back.files[k] : "-";
            const std::string b = k < direct_own.files.size() ? direct_own.files[k] : "-";
            if (a != b) std::printf("      file %zu: set back %s, O all along %s\n", k, a.c_str(), b.c_str());
        }
        for (const auto& f : host_kept.files)
            if (std::find(direct_own.files.begin(), direct_own.files.end(), f) == direct_own.files.end()) std::printf("      H loads %s\n", f.c_str());
    }
}

// ---------------------------------------------------------------------------
// redirect (19.8.3)
// ---------------------------------------------------------------------------
struct RedirectRun {
    std::uint64_t park_frame{};      // the step that made the stage change pending
    std::uint64_t teardown_fields{}; // steps from the park to the load step (the module reload)
    std::uint64_t load_frame{};
    std::string stage;
    int point_no = -1;
    std::vector<std::uint8_t> canonical;
    std::map<std::uint64_t, std::uint64_t> fingerprints;
    std::uint64_t redirect_frame{};
    std::uint8_t present_at_park{};  // the engine at BARRIER_OK: its present slots
    std::uint8_t present_after{};    // and after the load step (the DISSOLVE's solo engine)
};

// Where the redirect goes: none (a NEXTSTAGE as it was), at the park (on_barrier_ok,
// before set_barrier's first step), after the module reload's first held field,
// before the load step (the last held field: what follows is apply_barrier, the
// load step's first work, engine.cpp 2062-2066), or the stage alone at the park.
enum class RedirectAt { none, park, after_first_transition_step, before_load_step, park_stage_only };

struct RedirectSetup {
    std::uint8_t present = 1; // the stage group's present slots (slot 0 always)
    int own_slot = 0;         // this engine's slot; the DISSOLVE moves it to slot 0
};

RedirectRun run_redirect(const std::string& first_target, int first_point, RedirectAt at, const std::string& to, int to_point,
                         const RoomSettings& settings, const RedirectSetup& setup = {}, std::uint64_t load_after_park = 21) {
    RedirectRun run;
    std::optional<Engine> holder;
    new_engine(holder);
    auto& engine = *holder;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    const char* species[4] = {"p_agumon", "p_dorumon", "p_agumon", "p_dorumon"};
    for (int s = 0; s < 4; ++s)
        if (setup.present & (1u << s)) {
            sys->players[std::size_t(s)].joined = true;
            if (s > 0) sys->players[std::size_t(s)].digimon = species[s];
        }
    set_modes(engine, settings);
    dw4::game::online::apply_room_cheats(settings, engine.cheats());
    OnlineRules rules;
    rules.enabled = true;
    rules.present = setup.present;
    engine.set_online(rules);
    engine.set_split_identity(setup.own_slot);
    engine.set_view_slot(setup.own_slot);
    engine.request_stage("stag0101");
    bool posted = false;
    std::uint64_t transition_steps = 0;
    std::uint64_t serial = 0;
    auto redirect = [&](bool with_point, std::uint64_t frame) {
        engine.request_stage(to);
        if (with_point)
            if (auto* points = engine.get<PlayerPointManager>()) points->set_point_no(to_point);
        run.redirect_frame = frame;
    };
    for (int i = 0; i < 5200; ++i) {
        const std::uint64_t frame = engine.frame();
        if (!posted && frame > 1200 && engine.stage_name() == "stag0101" && engine.membership_safe()) {
            post_departure(engine, first_target, first_point);
            posted = true;
        }
        // Parked: the client sends BARRIER and steps no more until BARRIER_OK
        // (Session::park, on_barrier_ok -> set_barrier); the DISSOLVE answer
        // comes before the next step.
        if (posted && !run.park_frame && engine.pending_stage()) {
            run.park_frame = frame;
            run.present_at_park = engine.online().present;
            BarrierResult b;
            b.decision = Decision::dissolve;
            b.own_slot = setup.own_slot;
            b.rules = engine.online();
            engine.set_barrier(b);
            if (at == RedirectAt::park || at == RedirectAt::park_stage_only) redirect(at == RedirectAt::park, frame);
        }
        if (run.park_frame && !run.load_frame) {
            if (at == RedirectAt::after_first_transition_step && transition_steps == 1) redirect(true, frame);
            if (at == RedirectAt::before_load_step && transition_steps + 1 == load_after_park) redirect(true, frame);
            ++transition_steps;
        }
        step(engine, pads_at(frame));
        const auto* snapshot = engine.stage_snapshot();
        if (snapshot && snapshot->serial != serial) {
            serial = snapshot->serial;
            if (run.park_frame && !run.load_frame) {
                run.load_frame = engine.frame();
                run.teardown_fields = run.load_frame - run.park_frame;
                run.stage = snapshot->stage;
                run.point_no = snapshot->point_no;
                run.canonical = engine.snapshot_bytes(SnapshotForm::canonical);
                run.present_after = engine.online().present;
            }
        }
        if (run.load_frame) run.fingerprints[engine.frame()] = engine.net_fingerprint();
    }
    engine.set_online(OnlineRules{});
    return run;
}

void redirect_probe() {
    RoomSettings base;
    RoomSettings zones = base;
    zones.rand_seed = 4242;
    zones.rand_zones = 1;
    auto report = [](const RedirectRun& ref, const RedirectRun& r, const std::string& name) {
        std::size_t first_byte = 0;
        const bool same = r.canonical == ref.canonical;
        if (!same) {
            const std::size_t n = std::min(r.canonical.size(), ref.canonical.size());
            while (first_byte < n && r.canonical[first_byte] == ref.canonical[first_byte]) ++first_byte;
        }
        std::uint64_t compared = 0, first_diff = 0;
        for (const auto& [frame, fp] : r.fingerprints) {
            const auto it = ref.fingerprints.find(frame);
            if (it == ref.fingerprints.end()) continue;
            ++compared;
            if (!first_diff && it->second != fp) first_diff = frame;
        }
        std::printf("  %-80s park %llu (present %x), redirect %llu, load step %llu (+%llu, present %x), %s point %d, load snapshot %s", name.c_str(),
                    static_cast<unsigned long long>(r.park_frame), unsigned(r.present_at_park), static_cast<unsigned long long>(r.redirect_frame),
                    static_cast<unsigned long long>(r.load_frame), static_cast<unsigned long long>(r.teardown_fields), unsigned(r.present_after),
                    r.stage.c_str(), r.point_no, same ? "equal" : "differs");
        if (!same) std::printf(" (from byte %zu of %zu)", first_byte, r.canonical.size());
        std::printf(", %llu frames compared, %s\n", static_cast<unsigned long long>(compared),
                    first_diff ? ("differ from " + std::to_string(first_diff)).c_str() : "equal");
    };
    struct Group {
        const char* name;
        RedirectSetup setup;
        RoomSettings settings;
        const char* other; // the departure the redirect replaces
        int other_point;
    };
    // Solo; a parked guest of a stage group of two and of three (own slot 1),
    // as at a stage group's DISSOLVE that leads home or an instance's end; and
    // the zone shuffle (its remap happens in NEXTSTAGE).
    const Group groups[] = {
        {"solo", {1, 0}, base, "stagML02", 0},
        {"two present, own slot 1", {0b011, 1}, base, "stagML02", 0},
        {"three present, own slot 1", {0b111, 1}, base, "stag0102", 0},
        {"solo, zone shuffle", {1, 0}, zones, "dng101b1", 0},
    };
    std::printf("redirect probe: stag0101 direct start, departure at frame > 1200; DISSOLVE (own slot as named) answered at the park;\n"
                "each redirect to stagml11 point 3 against that group's NEXTSTAGE stagML11 point 3\n");
    for (const auto& g : groups) {
        const auto ref = run_redirect("stagML11", 3, RedirectAt::none, "stagml11", 3, g.settings, g.setup);
        report(ref, ref, std::string(g.name) + ": NEXTSTAGE stagML11 point 3 (reference)");
        const std::pair<RedirectAt, const char*> places[] = {
            {RedirectAt::park, "at the park"},
            {RedirectAt::after_first_transition_step, "after the first transition step"},
            {RedirectAt::before_load_step, "before the load step (the top of apply_barrier)"},
            {RedirectAt::park_stage_only, "of the stage only, at the park"},
        };
        for (const auto& [where, text] : places) {
            if (g.settings.rand_zones && where != RedirectAt::park) continue;
            const auto r = run_redirect(g.other, g.other_point, where, "stagml11", 3, g.settings, g.setup, ref.teardown_fields);
            report(ref, r, std::string(g.name) + ": NEXTSTAGE " + g.other + " point " + std::to_string(g.other_point) + ", redirect " + text);
        }
    }
}

// ---------------------------------------------------------------------------
// homeover (19.6.5)
// ---------------------------------------------------------------------------
void kill(Engine& engine, int slot) {
    // A defeat as in a fight (tests/online_tests.cpp Planned::kill).
    const Messenger::PortOnly port_only(engine.messenger());
    Packet packet;
    parse_packet_arguments("I<" + std::to_string(slot) + ">/I<0>/I<0>/I<0>", packet);
    engine.messenger().send(engine, "GAMERAM", std::nullopt, 0, "ADDHP", packet);
    const auto& digimon = engine.get<GameSystem>()->players[std::size_t(slot)].digimon;
    Packet dead;
    engine.messenger().send(engine, "PLAYER", std::string_view(digimon), slot, "SQ_DEAD", dead);
}

// A player's model onto a position, as a script moves it (PLAYER SETMATRIX,
// model_ctrl.cpp 1146-1153). False: no generated model.
bool teleport(Engine& engine, int slot, const Vec3f& at) {
    const auto* models = engine.get<ModelCtrlManager>();
    const auto* p = models ? models->player(slot) : nullptr;
    if (!p || !p->generated || !p->node) return false;
    auto matrix = p->node->matrix;
    matrix(3, 0) = at.x;
    matrix(3, 1) = at.y;
    matrix(3, 2) = at.z;
    const Messenger::PortOnly port_only(engine.messenger());
    Packet packet;
    packet.matrices.push_back(matrix);
    const auto& digimon = engine.get<GameSystem>()->players[std::size_t(slot)].digimon;
    engine.messenger().send(engine, "PLAYER", std::string_view(digimon), slot, "SETMATRIX", packet);
    return true;
}

struct HomeCase {
    const char* name;
    int players;
    std::uint8_t killed;      // slots defeated from kill_from (in slot order, a frame apart)
    std::uint8_t home;        // the slots HOME takes; this copy keeps the others
    std::uint64_t home_after; // frames after the last kill
    std::uint64_t kill_from = 1200;
    bool online = true;       // online rules (else offline: the original's rules)
    int onto_mat = -1;        // this slot is put onto dng101b1's matz01 (the way back) 60 frames before the defeats and stands there
    const char* stage = "dng101b1";
    bool home_first = false;  // HOME takes its slots 300 frames before the defeats (a guest that left: the host falls alone, W27)
    std::uint32_t after = ~0u; // the kept slot's buttons from its defeat on (~0: the probe's usual pads, A every 37 frames);
                               // the game over's message waits for A (W27)
};

void homeover_probe(int only) {
    const HomeCase cases[] = {
        {"2 players: 1 defeated stays, HOME takes living 0", 2, 0b0010, 0b0001, 300},
        {"2 players: same, HOME 10 frames after the defeat", 2, 0b0010, 0b0001, 10},
        {"3 players: 1 and 2 defeated stay, HOME takes living 0", 3, 0b0110, 0b0001, 300},
        {"2 players: HOME takes defeated 1, living 0 stays", 2, 0b0010, 0b0010, 300},
        {"2 players: nobody defeated, HOME takes 0", 2, 0b0000, 0b0001, 300},
        {"2 players: both defeated, no HOME (online rules)", 2, 0b0011, 0b0000, 300},
        {"1 player defeated, no HOME (online rules)", 1, 0b0001, 0b0000, 300},
        {"2 players: defeat at 1800, HOME 10 frames after", 2, 0b0010, 0b0001, 10, 1800},
        {"2 players: defeat at 1800, HOME 300 frames after", 2, 0b0010, 0b0001, 300, 1800},
        {"2 players: defeat at 2600, HOME 10 frames after", 2, 0b0010, 0b0001, 10, 2600},
        {"offline: 1 player defeated", 1, 0b0001, 0b0000, 300, 1200, false},
        {"offline: 2 players, both defeated", 2, 0b0011, 0b0000, 300, 1200, false},
        {"offline: 1 player on matz01 from 1140, defeat due at 1200", 1, 0b0001, 0b0000, 300, 1200, false, 0},
        {"1 player on matz01 from 1140, defeat due at 1200 (online rules)", 1, 0b0001, 0b0000, 300, 1200, true, 0},
        {"stag0101: 1 player defeated (online rules)", 1, 0b0001, 0b0000, 300, 1200, true, -1, "stag0101"},
        {"stag0101: offline, 1 player defeated", 1, 0b0001, 0b0000, 300, 1200, false, -1, "stag0101"},
        {"stag0101: HOME takes living 1, then 0 defeated (online rules)", 2, 0b0001, 0b0010, 300, 1500, true, -1, "stag0101", true},
        {"dng101b1: HOME takes living 1, then 0 defeated (online rules)", 2, 0b0001, 0b0010, 300, 1500, true, -1, "dng101b1", true},
        {"stag0101: 1 player defeated, START held (online rules)", 1, 0b0001, 0b0000, 300, 1200, true, -1, "stag0101", false, dw4::pad_button::start},
        {"stag0101: offline, 1 player defeated, START held", 1, 0b0001, 0b0000, 300, 1200, false, -1, "stag0101", false, dw4::pad_button::start},
        {"stag0101: 1 player defeated, nothing pressed (online rules)", 1, 0b0001, 0b0000, 300, 1200, true, -1, "stag0101", false, 0},
        {"stag0101: 1 player defeated, START held and A now and then (online rules)", 1, 0b0001, 0b0000, 300, 1200, true, -1, "stag0101", false,
         dw4::pad_button::start | 0x80000000u},
    };
    const char* species[4] = {"p_agumon", "p_dorumon", "p_agumon", "p_dorumon"}; // (three or four players load the q_ packages: these two exist)
    std::printf("homeover probe: dng101b1 direct start; HOME emulated in the copy that keeps the other slots\n"
                "(Engine::online_remove_player per HOME slot: the despawn and membership step the fork's other side gets);\n"
                "every stage change made pending is listed with the alive and present masks a BARRIER would carry\n"
                "(GameEngineLink::alive_mask / present_mask, read after the step that made it pending, as Session::park reads them)\n");
    for (int index = 0; index < int(std::size(cases)); ++index) {
        if (only >= 0 && index != only) continue;
        const auto& c = cases[index];
        std::optional<Engine> holder;
        new_engine(holder);
        dw4::game::online::EngineHooks hooks;
        hooks.engine = [&]() -> Engine& { return *holder; };
        hooks.fresh_engine = [&] { new_engine(holder); };
        dw4::game::online::GameEngineLink link(hooks, RoomSettings{});
        auto& engine = *holder;
        auto* sys = engine.get<GameSystem>();
        sys->direct_start = true;
        for (int s = 0; s < c.players; ++s) {
            sys->players[std::size_t(s)].joined = true;
            sys->players[std::size_t(s)].digimon = species[s];
        }
        if (c.online) {
            OnlineRules rules;
            rules.enabled = true;
            rules.present = std::uint8_t((1u << c.players) - 1u);
            engine.set_online(rules);
        }
        int keep = 0;
        for (int s = 0; s < c.players; ++s)
            if (!(c.home & (1u << s))) {
                keep = s;
                break;
            }
        engine.set_split_identity(keep);
        engine.set_view_slot(keep);
        engine.request_stage(c.stage);
        std::uint64_t kills_done_at = 0, home_at = 0, first_change = 0;
        int next_kill = 0;
        std::string events, mat_at_kill;
        bool on_mat = false;
        for (int i = 0; i < 6000; ++i) {
            const std::uint64_t frame = engine.frame();
            // Onto the mat a second before the defeat, standing still from then on
            // (a defeat in the frame of the move did not take: the mat's script
            // takes and gives back control in that frame).
            if (c.onto_mat >= 0 && !on_mat && frame + 60 >= c.kill_from && engine.stage_name() == c.stage && engine.membership_safe()) {
                const auto* mats = engine.get<MatCtrlManager>();
                const auto centre = mats ? mats->mat_centre("matz01") : std::nullopt;
                if (!centre || !teleport(engine, c.onto_mat, *centre)) mat_at_kill += "(teleport failed) ";
                on_mat = true;
            }
            if (c.home_first && !home_at && frame + 300 >= c.kill_from && engine.stage_name() == c.stage && engine.membership_safe()) {
                for (int s = 0; s < 4; ++s)
                    if (c.home & (1u << s)) engine.online_remove_player(s);
                home_at = frame;
            }
            if (frame >= c.kill_from && !kills_done_at && engine.stage_name() == c.stage && engine.membership_safe() && (!c.home_first || home_at)) {
                while (next_kill < 4 && !(c.killed & (1u << next_kill))) ++next_kill;
                if (next_kill < 4) {
                    kill(engine, next_kill); // one defeat a frame
                    ++next_kill;
                } else {
                    kills_done_at = frame;
                    if (const auto* mats = engine.get<MatCtrlManager>()) {
                        char text[64];
                        std::snprintf(text, sizeof text, "matz01 players %x", unsigned(mats->players_on("matz01")));
                        mat_at_kill += text;
                    }
                }
            }
            if (kills_done_at && !home_at && c.home && !c.home_first && frame >= kills_done_at + c.home_after && engine.membership_safe()) {
                for (int s = 0; s < 4; ++s)
                    if (c.home & (1u << s)) engine.online_remove_player(s);
                home_at = frame;
            }
            const bool before = engine.pending_stage().has_value();
            auto pads = pads_at(frame);
            if (on_mat) {
                pads[std::size_t(c.onto_mat)] = PadSample{};
                pads[std::size_t(c.onto_mat)].connected = true;
            }
            if (c.after != ~0u && kills_done_at) {
                // (Bit 31: A every 40 frames beside the held buttons, as the online script's talk action presses it.)
                const bool a_now = (c.after & 0x80000000u) && frame % 40 < 3;
                pads[std::size_t(keep)] = PadSample{};
                pads[std::size_t(keep)].connected = true;
                pads[std::size_t(keep)].buttons = (c.after & 0x7FFFFFFFu) | (a_now ? dw4::pad_button::a : 0u);
            }
            step(engine, pads);
            if (!before && engine.pending_stage()) {
                if (!first_change) first_change = engine.frame();
                char text[160];
                std::snprintf(text, sizeof text, "%s[%llu %s point %d alive %x present %x]", events.empty() ? "" : " ",
                              static_cast<unsigned long long>(engine.frame()), std::string(*engine.pending_stage()).c_str(), link.pending_point(),
                              unsigned(link.alive_mask()), unsigned(link.present_mask()));
                events += text;
            }
            if (first_change && engine.frame() > first_change + 2000) break;
        }
        std::printf("  %-52s %s, kills done %llu (%s), HOME %llu, stage changes: %s; final stage %s\n", c.name, c.online ? "online rules" : "offline",
                    static_cast<unsigned long long>(kills_done_at), mat_at_kill.c_str(), static_cast<unsigned long long>(home_at),
                    events.empty() ? "none" : events.c_str(), engine.stage_name().c_str());
        engine.set_online(OnlineRules{});
    }
}

// ---------------------------------------------------------------------------
// guestnew (19.30.4, round 72's first task)
// ---------------------------------------------------------------------------
// A guest's NEW: an engine on its own story S_g and settings O leaves the
// lobby (stagML11 direct start, neutral pads, the gate departure to stag0201
// point 0 posted at the first presenting frame from kPostFrame) and is
// answered NEW at its park with the host's story S_h (story mode replace) and
// the host's settings H (modes and cheats set at the park, the place where
// M1 sets the own ones back at a DISSOLVE). The reference is a Game Start
// with S_h and H that goes the same way (its own NEW merges S_h, as the
// host's own barrier does). Compared: the load snapshot (canonical) and the
// net fingerprints for kCompareFrames after the load step; solo and as the
// base of a co-arrival of two (the co-arrival's carry spawned at the load
// step). A difference is broken down by the snapshot's flags and counters.
RoomStory make_story(int phase_index, int difficulty, std::initializer_list<int> flags, std::initializer_list<int> keys, int counter7) {
    RoomStory s;
    const auto phases = story::phase_flags();
    if (phase_index >= 1 && phase_index <= int(phases.size())) s.flags[std::size_t(phases[std::size_t(phase_index - 1)])] = 1;
    for (const int f : flags) s.flags[std::size_t(f)] = 1;
    for (const int k : keys) s.folder[std::size_t(k)] = 1;
    s.counters[7] = counter7;
    story::derive_copies(s, difficulty);
    return s;
}

struct GuestRun {
    std::uint64_t post_frame{}, park_frame{}, load_frame{};
    std::string stage;
    std::vector<std::uint8_t> canonical;
    std::map<std::uint64_t, std::uint64_t> fingerprints;
    std::uint8_t present_after{};
    std::string held;
    RoomStory story_at_park;                    // the engine's story values at its park (the reference: what the host's
                                                // STORY at the barrier gives his keeper)
    std::map<std::int32_t, std::uint8_t> flags_at_load; // FLAG right after the load step
};

constexpr std::uint64_t kPostFrame = 1500;
constexpr std::uint64_t kCompareFrames = 3000;

std::string held_settings(Engine& engine) {
    const auto* s = engine.get<GameSystem>();
    const auto& c = engine.cheats();
    char text[160];
    std::snprintf(text, sizeof text, "diff %d cam %d light %d fast %d rand %u/%d/%d drops %u", s->difficulty, s->camera_mode, s->light_mode,
                  c.fast_loading ? 1 : 0, c.rand_seed, c.rand_enemies, c.rand_zones, c.drop_seed);
    return text;
}

RoomStory story_of(Engine& engine) {
    RoomStory s;
    for (const auto& [f, v] : engine.flags())
        if (f >= 0 && f < story::kFlags) s.flags[std::size_t(f)] = v;
    if (const auto* sys = engine.get<GameSystem>())
        for (const auto& [k, v] : sys->flag_folder)
            if (k >= 0 && k < story::kFolder) s.folder[std::size_t(k)] = v;
    for (const auto& [c, v] : engine.counters())
        if (c >= 0 && c < story::kCounters) s.counters[std::size_t(c)] = v;
    return s;
}

// `align`: FLAG set to these right after the load step (the stage's scripts
// run after it), so the compared fingerprints show whatever else differs.
GuestRun run_guest_new(const RoomStory& own_story, const RoomSettings& own, const std::optional<std::pair<RoomStory, RoomSettings>>& host,
                       bool co_arrival, bool replace = true, const std::map<std::int32_t, std::uint8_t>* align = nullptr) {
    GuestRun run;
    std::optional<Engine> holder;
    new_engine(holder);
    auto& engine = *holder;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].joined = true;
    // The Game Start (GameEngineLink::after_live_step): the modes, the story.
    set_modes(engine, own);
    dw4::game::online::apply_room_cheats(own, engine.cheats());
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 1;
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.set_view_slot(0);
    engine.apply_room_story(own_story);
    engine.request_stage("stagml11");
    std::uint64_t serial = 0;
    PadSample neutral;
    neutral.connected = true;
    std::array<PadSample, 4> idle{};
    idle[0] = neutral;
    for (int i = 0; i < 12000; ++i) {
        const std::uint64_t frame = engine.frame();
        if (std::getenv("DW4_PROBE_DEBUG") && frame % 500 == 0)
            std::printf("    [%llu] stage %s presenting %d loading %d pending %d active %d pads %d demo %zu safe %d\n",
                        static_cast<unsigned long long>(frame), engine.stage_name().c_str(), engine.presenting() ? 1 : 0, engine.loading() ? 1 : 0,
                        engine.pending_stage() ? 1 : 0, engine.stage_active() ? 1 : 0, engine.pad_input_enabled() ? 1 : 0,
                        engine.demo().running_unscoped(), engine.membership_safe() ? 1 : 0);
        // (The lobby always runs a demo, so it is never membership-safe: the
        // gate's own departure runs inside its menu's demo too.)
        if (!run.post_frame && frame >= kPostFrame && engine.stage_name() == "stagml11" && engine.presenting() && !engine.loading()) {
            post_departure(engine, "stag0201", 0);
            run.post_frame = frame;
        }
        if (run.post_frame && !run.park_frame && engine.pending_stage()) {
            run.park_frame = frame;
            run.story_at_park = story_of(engine);
            serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
            BarrierResult b;
            b.decision = Decision::new_instance;
            b.slot_map = {0, -1, -1, -1};
            b.own_slot = 0;
            if (co_arrival) {
                PlayerCarry carry = engine.carry_player(0);
                carry.slot.digimon = "p_dorumon";
                carry.slot.unique = 0x4242;
                carry.slot.name = u"Lee";
                carry.chip = {};
                b.co_arrivals.emplace_back(1, std::move(carry));
            }
            b.rules = engine.online();
            if (host) {
                // The guest's NEW: the host's story replaces the own; the host's modes and cheats now.
                b.story = host->first;
                b.story_mode = replace ? StoryMode::replace : StoryMode::merge;
                set_modes(engine, host->second);
                dw4::game::online::apply_room_cheats(host->second, engine.cheats());
            } else {
                // The reference (the host's own NEW): his confirmed story merged.
                b.story = own_story;
            }
            engine.set_barrier(b);
        }
        const bool after_load = run.load_frame != 0;
        step(engine, after_load ? pads_at(frame) : idle);
        const auto* snapshot = engine.stage_snapshot();
        if (run.park_frame && !run.load_frame && snapshot && snapshot->serial != serial) {
            run.load_frame = engine.frame();
            run.stage = snapshot->stage;
            run.canonical = engine.snapshot_bytes(SnapshotForm::canonical);
            run.present_after = engine.online().present;
            run.held = held_settings(engine);
            run.flags_at_load = engine.flags();
            if (align) engine.flags() = *align;
        }
        if (run.load_frame) {
            run.fingerprints[engine.frame() - run.load_frame] = engine.net_fingerprint();
            if (engine.frame() - run.load_frame >= kCompareFrames) break;
        }
    }
    engine.set_online(OnlineRules{});
    return run;
}

void report_guest(const GuestRun& ref, const GuestRun& r, const std::string& name) {
    std::size_t first_byte = 0;
    const bool same = r.canonical == ref.canonical;
    if (!same) {
        const std::size_t n = std::min(r.canonical.size(), ref.canonical.size());
        while (first_byte < n && r.canonical[first_byte] == ref.canonical[first_byte]) ++first_byte;
    }
    std::uint64_t compared = 0, first_diff = 0;
    for (const auto& [k, fp] : r.fingerprints) {
        const auto it = ref.fingerprints.find(k);
        if (it == ref.fingerprints.end()) continue;
        ++compared;
        if (!first_diff && it->second != fp) first_diff = k + 1;
    }
    std::printf("  %-66s post %llu park %llu load step %llu (ref %llu/%llu/%llu), %s, present %x, holds [%s]; load snapshot %s", name.c_str(),
                static_cast<unsigned long long>(r.post_frame), static_cast<unsigned long long>(r.park_frame),
                static_cast<unsigned long long>(r.load_frame), static_cast<unsigned long long>(ref.post_frame),
                static_cast<unsigned long long>(ref.park_frame), static_cast<unsigned long long>(ref.load_frame), r.stage.c_str(),
                unsigned(r.present_after), r.held.c_str(), same ? "equal" : "differs");
    if (!same) std::printf(" (from byte %zu of %zu / ref %zu)", first_byte, r.canonical.size(), ref.canonical.size());
    std::printf(", %llu frames compared, %s\n", static_cast<unsigned long long>(compared),
                first_diff ? ("differ from load + " + std::to_string(first_diff - 1)).c_str() : "equal");
    if (same || r.canonical.empty() || ref.canonical.empty()) return;
    // The break-down: the flags and counters of both snapshots.
    try {
        const auto a = decode_snapshot(r.canonical);
        const auto b = decode_snapshot(ref.canonical);
        auto show = [](const auto& ma, const auto& mb, const char* what) {
            std::set<std::int32_t> keys;
            for (const auto& [k, v] : ma) keys.insert(k);
            for (const auto& [k, v] : mb) keys.insert(k);
            int shown = 0;
            for (const auto k : keys) {
                const auto ia = ma.find(k);
                const auto ib = mb.find(k);
                const bool ha = ia != ma.end(), hb = ib != mb.end();
                if (ha == hb && (!ha || ia->second == ib->second)) continue;
                if (shown++ < 40)
                    std::printf("      %s %d: guest %s, reference %s%s\n", what, k, ha ? std::to_string(int(ia->second)).c_str() : "-",
                                hb ? std::to_string(int(ib->second)).c_str() : "-", story::in_replaced_subset(k) ? " (story subset)" : "");
            }
            if (shown > 40) std::printf("      ... %d %s differ in all\n", shown, what);
            return shown;
        };
        const int flags = show(a.flags, b.flags, "flag");
        const int counters = show(a.counters, b.counters, "counter");
        std::printf("    frames %llu / %llu, time %lld / %lld, %d flags and %d counters differ\n", static_cast<unsigned long long>(a.frame),
                    static_cast<unsigned long long>(b.frame), static_cast<long long>(a.time), static_cast<long long>(b.time), flags, counters);
        // Which other fields differ: the reference's fields put into the
        // guest's snapshot one after another until the bytes are equal.
        auto guest = a;
        const auto target = encode_snapshot(b, SnapshotForm::canonical);
        std::string fields;
        const std::pair<const char*, std::function<void()>> swaps[] = {
            {"flags", [&] { guest.flags = b.flags; }},
            {"counters", [&] { guest.counters = b.counters; }},
            {"pad_input_enabled", [&] { guest.pad_input_enabled = b.pad_input_enabled; }},
            {"cheats", [&] { guest.cheats = b.cheats; }},
            {"random", [&] { guest.random = b.random; }},
            {"point_no", [&] { guest.point_no = b.point_no; }},
            {"loader_teardown", [&] { guest.loader_teardown = b.loader_teardown; }},
            {"loader_common", [&] { guest.loader_common = b.loader_common; }},
            {"stage_active", [&] { guest.stage_active = b.stage_active; }},
            {"system (GameSystem)", [&] { guest.system = b.system; }},
            {"sound", [&] { guest.sound = b.sound; }},
            {"online", [&] { guest.online = b.online; }},
            {"personal", [&] { guest.personal = b.personal; }},
        };
        auto before = encode_snapshot(guest, SnapshotForm::canonical);
        for (const auto& [field, swap] : swaps) {
            if (before == target) break;
            swap();
            auto after = encode_snapshot(guest, SnapshotForm::canonical);
            if (after != before) fields += std::string(fields.empty() ? "" : ", ") + field;
            before = std::move(after);
        }
        std::printf("    snapshot fields that differ: %s%s\n", fields.empty() ? "none" : fields.c_str(),
                    before == target ? "" : " (and more: still not equal after every swap)");
        if (std::string(fields).find("system") != std::string::npos) {
            const auto* ga = static_cast<const GameSystem*>(a.system.get());
            const auto* gb = static_cast<const GameSystem*>(b.system.get());
            if (ga && gb) {
                std::printf("      GameSystem: difficulty %d/%d camera %d/%d light %d/%d gate %s/%s %d/%d folder %zu/%zu\n", ga->difficulty, gb->difficulty,
                            ga->camera_mode, gb->camera_mode, ga->light_mode, gb->light_mode, ga->gate_stage_name.value_or("-").c_str(),
                            gb->gate_stage_name.value_or("-").c_str(), ga->gate_stage_no, gb->gate_stage_no, ga->flag_folder.size(),
                            gb->flag_folder.size());
                for (const auto& [k, v] : ga->flag_folder)
                    if (!gb->flag_folder.contains(k) || gb->flag_folder.at(k) != v) std::printf("      folder %d: guest %d, reference %s\n", k, int(v),
                                                                                                 gb->flag_folder.contains(k) ? std::to_string(int(gb->flag_folder.at(k))).c_str() : "-");
                for (const auto& [k, v] : gb->flag_folder)
                    if (!ga->flag_folder.contains(k)) std::printf("      folder %d: guest -, reference %d\n", k, int(v));
            }
        }
    } catch (const std::exception& e) {
        std::printf("    (snapshot decode failed: %s)\n", e.what());
    }
}

void guestnew_probe() {
    // The host: Hard, chapter 4 (flag 4: Dry Land's gate open), the training
    // (503), two bosses and a world event, keys 0-2, COUNTER 7 = 5. The
    // guest: Normal, chapter 2, the training only, COUNTER 7 = 1.
    const RoomStory host_story = make_story(4, 1, {503, 67, 68, 19, 20}, {0, 1, 2}, 5);
    const RoomStory guest_story = make_story(2, 0, {503}, {}, 1);
    RoomSettings base;
    RoomSettings host_hard = base;
    host_hard.difficulty = 1;
    RoomSettings host_rand = host_hard;
    host_rand.rand_seed = 4242;
    host_rand.rand_enemies = 1;
    host_rand.rand_drops = true;
    RoomSettings host_fast = host_rand;
    host_fast.fast_loading = true;
    RoomSettings host_modes = host_rand;
    host_modes.camera_mode = 1;
    host_modes.light_mode = 0;
    std::printf("guestnew probe: lobby stagml11 direct start on the guest's own story and settings, gate departure to stag0201 point 0 at the\n"
                "first presenting frame from %llu, NEW answered at the park with the host's story (replace) and settings; reference: a\n"
                "Game Start with the host's story and settings the same way; %llu frames compared after the load step\n",
                static_cast<unsigned long long>(kPostFrame), static_cast<unsigned long long>(kCompareFrames));
    struct Case {
        const char* name;
        RoomSettings own, host;
        bool co_arrival;
        bool replace = true;
    };
    const Case cases[] = {
        {"solo: other story, same settings (default)", base, base, false},
        {"solo: other story, host Hard", base, host_hard, false},
        {"solo: other story, host Hard + randomizer", base, host_rand, false},
        {"solo: other story, host Hard + randomizer + camera 1 + light 0", base, host_modes, false},
        {"solo: other story, host Hard + randomizer + fast loading", base, host_fast, false},
        {"solo: guest Hard + randomizer, host default (the other way)", host_rand, base, false},
        {"co-arrival of two: other story, host Hard + randomizer", base, host_rand, true},
        {"co-arrival of two: other story, same settings", base, base, true},
        {"counter-proof: no replace (merge), host Hard + randomizer", base, host_rand, false, false},
    };
    for (const auto& c : cases) {
        const auto ref = run_guest_new(host_story, c.host, std::nullopt, c.co_arrival);
        // The host's confirmed story at the barrier: his engine's story at its
        // park (his STORY goes before his BARRIER and is merged first).
        const auto host_now = ref.story_at_park;
        const auto guest = run_guest_new(guest_story, c.own, std::make_pair(host_now, c.host), c.co_arrival, c.replace);
        report_guest(ref, guest, c.name);
        // The same with FLAG aligned to the reference's right after the load
        // step: what differs besides the flag table.
        const auto aligned = run_guest_new(guest_story, c.own, std::make_pair(host_now, c.host), c.co_arrival, c.replace, &ref.flags_at_load);
        std::uint64_t compared = 0, first_diff = 0;
        for (const auto& [k, fp] : aligned.fingerprints) {
            const auto it = ref.fingerprints.find(k);
            if (it == ref.fingerprints.end() || k == 0) continue;
            ++compared;
            if (!first_diff && it->second != fp) first_diff = k + 1;
        }
        std::printf("    FLAG aligned after the load step: %llu frames compared, %s\n", static_cast<unsigned long long>(compared),
                    first_diff ? ("differ from load + " + std::to_string(first_diff - 1)).c_str() : "equal");
    }
}

// ---------------------------------------------------------------------------
// mats STAGE: the stage's mat boxes after a direct start (the online
// scenarios' walks to a mat, e.g. stagML11's return mat mate02 for W30b)
// ---------------------------------------------------------------------------
void mats_probe(const std::string& stage) {
    std::optional<Engine> holder;
    new_engine(holder);
    auto& engine = *holder;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].joined = true;
    sys->players[0].digimon = "p_agumon";
    engine.request_stage(stage);
    for (int i = 0; i < 3000 && !(engine.stage_name() == stage && engine.membership_safe()); ++i) step(engine, pads_at(engine.frame()));
    for (int i = 0; i < 120; ++i) step(engine, pads_at(engine.frame()));
    std::printf("mats of %s (frame %llu):\n", engine.stage_name().c_str(), static_cast<unsigned long long>(engine.frame()));
    if (const auto* mats = engine.get<MatCtrlManager>())
        for (const auto& box : mats->mat_boxes())
            std::printf("  %-8s x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n", box.name.c_str(), box.min.x, box.max.x, box.min.y, box.max.y, box.min.z,
                        box.max.z);
}

// ---------------------------------------------------------------------------
// go (round I1b's first task, docs/SEAMLESS.md 19.30.7)
// ---------------------------------------------------------------------------
// Two members in stag0201 (slot 0 walks, slot 1 walks until frame kGoTeleport
// after the load and then stands). The exit port's own departure: slot 1 is
// put onto matz01 (to dng204b1 point 0, g_StChangeKind "run") and answers its
// prompt Yes; the port forks it off (ForkKind::exit) and its copy departs.
// The GO: slot 1 is put next to the mat (not on it) at the same frame, and
// Engine::online_go(slot 1, the port's barrier target) runs before the step
// in which the port forked, so both forks record the same frame. Each run in
// both copies of the fork (split identity 1: the leavers, 0: the stayers),
// and as replays with the recorded fork side (identity -1, the side taken
// from STATE_DATA's fork list) and, for the GO, a counter-proof replay with
// the exit's frame convention (one frame early). The leavers' barrier target
// (stage, point after the zone remap), their load snapshot and the net
// fingerprints after the load are compared between the port and the GO; the
// zone shuffle off and on (seed 4242, zones 1 and 2).
namespace go_probe {

constexpr std::uint64_t kGoTeleport = 600;   // frames after the stag0201 load
constexpr std::uint64_t kAfterLoad = 1200;   // fingerprints compared after the leavers' load step
constexpr std::uint64_t kStayerFrames = 1500; // the stayers' copy after the fork

struct Run {
    std::uint64_t load0{};           // the stag0201 load step
    std::uint64_t teleport{};
    std::optional<SplitResult> split;
    std::optional<ExitHeading> heading;
    std::uint64_t park{};            // the step that made the stage change pending
    std::string barrier_stage;
    int barrier_point{};
    std::uint8_t alive{}, present{};
    std::uint64_t load{};            // the leavers' load step
    std::string load_stage;
    std::vector<std::uint8_t> canonical;
    std::map<std::uint64_t, std::uint64_t> fp;    // post-step frame -> net fingerprint (until the park)
    std::map<std::uint64_t, std::uint64_t> after; // frames after the leavers' load step -> net fingerprint
    std::uint8_t present_end{};
    std::string stage_end;
};

std::uint8_t alive_of(Engine& engine) {
    std::uint8_t alive = 0;
    if (const auto* sys = engine.get<GameSystem>())
        for (int s = 0; s < 4; ++s)
            if (sys->players[std::size_t(s)].joined && sys->players[std::size_t(s)].hp > 0) alive |= std::uint8_t(1u << s);
    return alive;
}

// `go`: the GO's target (empty: the port's run); `go_before`: the post-step
// frame of the step the GO comes before (the port's fork frame); `sides`: a
// replay's fork list (identity -1).
Run run(int identity, int zones, const std::string& go_stage, int go_point, std::uint64_t go_before, const std::vector<ForkSide>& sides) {
    Run r;
    std::optional<Engine> holder;
    new_engine(holder);
    auto& engine = *holder;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    const char* species[2] = {"p_agumon", "p_dorumon"};
    for (int s = 0; s < 2; ++s) {
        sys->players[std::size_t(s)].joined = true;
        sys->players[std::size_t(s)].digimon = species[s];
    }
    if (zones) {
        engine.cheats().rand_seed = 4242;
        engine.cheats().rand_zones = std::uint8_t(zones);
    }
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b11;
    engine.set_online(rules);
    engine.set_split_identity(identity);
    engine.set_view_slot(identity < 0 ? 0 : identity);
    if (!sides.empty()) engine.set_fork_sides(sides);
    engine.request_stage("stag0201");
    const bool port = go_stage.empty();
    std::uint64_t serial = 0, load_serial = 0;
    bool placed = false;
    for (int i = 0; i < 12000; ++i) {
        const std::uint64_t frame = engine.frame();
        if (!r.load0 && engine.stage_name() == "stag0201" && engine.stage_snapshot()) r.load0 = engine.stage_snapshot() ? frame : 0;
        if (!placed && r.load0 && frame >= r.load0 + kGoTeleport && engine.membership_safe()) {
            const auto* mats = engine.get<MatCtrlManager>();
            auto centre = mats ? mats->mat_centre("matz01") : std::nullopt;
            if (!centre) throw std::runtime_error("stag0201 has no matz01");
            // The GO's slot 1 stands beside the mat (60 units off it, on the
            // field side), the port's on it.
            if (!port) centre->x += 60.0f;
            if (!teleport(engine, 1, *centre)) throw std::runtime_error("teleport failed");
            r.teleport = frame;
            placed = true;
        }
        if (!port && placed && !r.split && frame + 1 == go_before) {
            const bool departs = engine.online_go(0b10, go_stage, go_point);
            (void)departs;
            if (auto split = engine.take_split()) r.split = split;
            if (auto heading = engine.take_heading(); heading && !r.heading) r.heading = heading;
        }
        std::array<PadSample, 4> pads{};
        if (r.load) {
            // After the leavers' load: slot 0 (the goer, slot 0 since its NEW) walks by the same pads in both runs.
            pads[0] = pad_for(frame - r.load, 0);
        } else {
            pads = pads_at(frame);
            if (placed) {
                pads[1] = PadSample{};
                pads[1].connected = true;
                // The port's prompt: A now and then answers it (Yes: the cursor starts on it).
                if (port && frame >= r.teleport + 40 && (frame - r.teleport) % 30 < 2) pads[1].buttons = dw4::pad_button::a;
            }
        }
        const bool was_pending = engine.pending_stage().has_value();
        step(engine, pads);
        if (auto split = engine.take_split(); split && !r.split) r.split = split;
        // (The first heading: the port's or the GO's own; the NEXTSTAGE's
        // note_departure of the then solo copy announces it once more.)
        if (auto heading = engine.take_heading(); heading && !r.heading) r.heading = heading;
        if (!r.load) r.fp[engine.frame()] = engine.net_fingerprint();
        if (!was_pending && engine.pending_stage() && !r.park) {
            r.park = engine.frame();
            r.barrier_stage = std::string(*engine.pending_stage());
            if (const auto* points = engine.get<PlayerPointManager>()) r.barrier_point = points->point_no();
            r.alive = alive_of(engine);
            r.present = engine.online().present;
            serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
            BarrierResult b;
            b.decision = Decision::new_instance;
            b.slot_map = {-1, -1, -1, -1};
            for (int s = 0, to = 0; s < 4; ++s)
                if (r.present & (1u << s)) b.slot_map[std::size_t(s)] = to++;
            b.own_slot = 0;
            b.rules = engine.online();
            engine.set_barrier(b);
        }
        const auto* snapshot = engine.stage_snapshot();
        if (r.park && !r.load && snapshot && snapshot->serial != serial) {
            r.load = engine.frame();
            r.load_stage = snapshot->stage;
            r.canonical = engine.snapshot_bytes(SnapshotForm::canonical);
            load_serial = snapshot->serial;
        }
        if (r.load) {
            r.after[engine.frame() - r.load] = engine.net_fingerprint();
            if (engine.frame() - r.load >= kAfterLoad) break;
        }
        // The stayers' copy never parks: it plays on after the fork.
        if (r.split && !r.park && engine.frame() >= r.split->frame + kStayerFrames) break;
    }
    (void)load_serial;
    r.present_end = engine.online().present;
    r.stage_end = engine.stage_name();
    engine.set_online(OnlineRules{});
    return r;
}

// The first post-step frame whose fingerprints differ (0: equal over the frames both have).
std::uint64_t first_diff(const std::map<std::uint64_t, std::uint64_t>& a, const std::map<std::uint64_t, std::uint64_t>& b, std::uint64_t& compared) {
    compared = 0;
    for (const auto& [k, v] : a) {
        const auto it = b.find(k);
        if (it == b.end()) continue;
        ++compared;
        if (it->second != v) return k;
    }
    return 0;
}

std::string split_text(const std::optional<SplitResult>& s) {
    if (!s) return "no fork";
    char t[96];
    std::snprintf(t, sizeof t, "fork at %llu leavers %x kept %x child %x local %d", static_cast<unsigned long long>(s->frame), unsigned(s->leavers),
                  unsigned(s->kept), unsigned(s->child), s->local_leaves ? 1 : 0);
    return t;
}

std::string heading_text(const std::optional<ExitHeading>& h) {
    if (!h) return "no heading";
    char t[128];
    std::snprintf(t, sizeof t, "heading %s point %d leavers %x asked %x at %llu", h->stage.c_str(), h->point, unsigned(h->leavers), unsigned(h->asked),
                  static_cast<unsigned long long>(h->frame));
    return t;
}

void compare_snapshots(const std::vector<std::uint8_t>& go, const std::vector<std::uint8_t>& ref) {
    if (go == ref) {
        std::printf("      load snapshots: equal\n");
        return;
    }
    try {
        const auto a = decode_snapshot(go);
        const auto b = decode_snapshot(ref);
        auto guest = a;
        const auto target = encode_snapshot(b, SnapshotForm::canonical);
        std::string fields;
        const std::pair<const char*, std::function<void()>> swaps[] = {
            {"flags", [&] { guest.flags = b.flags; }},
            {"counters", [&] { guest.counters = b.counters; }},
            {"frame/time", [&] {
                 guest.frame = b.frame;
                 guest.time = b.time;
             }},
            {"pad_input_enabled", [&] { guest.pad_input_enabled = b.pad_input_enabled; }},
            {"cheats", [&] { guest.cheats = b.cheats; }},
            {"random", [&] { guest.random = b.random; }},
            {"point_no", [&] { guest.point_no = b.point_no; }},
            {"loader_teardown", [&] { guest.loader_teardown = b.loader_teardown; }},
            {"loader_common", [&] { guest.loader_common = b.loader_common; }},
            {"stage_active", [&] { guest.stage_active = b.stage_active; }},
            {"system (GameSystem)", [&] { guest.system = b.system; }},
            {"sound", [&] { guest.sound = b.sound; }},
            {"online", [&] { guest.online = b.online; }},
            {"personal", [&] { guest.personal = b.personal; }},
            {"clock_lateness", [&] { guest.clock_lateness = b.clock_lateness; }},
            {"pads", [&] { guest.pads = b.pads; }},
            {"console_reset", [&] { guest.console_reset = b.console_reset; }},
        };
        auto before = encode_snapshot(guest, SnapshotForm::canonical);
        for (const auto& [field, swap] : swaps) {
            if (before == target) break;
            swap();
            auto after = encode_snapshot(guest, SnapshotForm::canonical);
            if (after != before) fields += std::string(fields.empty() ? "" : ", ") + field;
            before = std::move(after);
        }
        std::printf("      load snapshots differ in: %s%s (frames %llu / %llu)\n", fields.empty() ? "none" : fields.c_str(),
                    before == target ? "" : " (and more)", static_cast<unsigned long long>(a.frame), static_cast<unsigned long long>(b.frame));
        if (fields.find("GameSystem") != std::string::npos) {
            const auto* ga = static_cast<const GameSystem*>(a.system.get());
            const auto* gb = static_cast<const GameSystem*>(b.system.get());
            for (int s = 0; ga && gb && s < 4; ++s) {
                const auto& pa = ga->players[std::size_t(s)];
                const auto& pb = gb->players[std::size_t(s)];
                if (pa.joined || pb.joined)
                    std::printf("        player %d: joined %d/%d %s/%s hp %d/%d mp %d/%d bits %d/%d devices %zu/%zu\n", s, pa.joined ? 1 : 0,
                                pb.joined ? 1 : 0, pa.digimon.c_str(), pb.digimon.c_str(), pa.hp, pb.hp, pa.mp, pb.mp, pa.bits, pb.bits,
                                pa.devices.size(), pb.devices.size());
            }
            if (ga && gb)
                std::printf("        gate %s/%s %d/%d ninzu %d/%d\n", ga->gate_stage_name.value_or("-").c_str(), gb->gate_stage_name.value_or("-").c_str(),
                            ga->gate_stage_no, gb->gate_stage_no, ga->ninzu, gb->ninzu);
        }
        if (fields.find("flags") != std::string::npos) {
            int shown = 0;
            std::set<std::int32_t> keys;
            for (const auto& [k, v] : a.flags) keys.insert(k);
            for (const auto& [k, v] : b.flags) keys.insert(k);
            for (const auto k : keys) {
                const auto ia = a.flags.find(k);
                const auto ib = b.flags.find(k);
                const bool ha = ia != a.flags.end(), hb = ib != b.flags.end();
                if (ha == hb && (!ha || ia->second == ib->second)) continue;
                if (shown++ < 20)
                    std::printf("        flag %d: GO %s, port %s\n", k, ha ? std::to_string(int(ia->second)).c_str() : "-",
                                hb ? std::to_string(int(ib->second)).c_str() : "-");
            }
        }
    } catch (const std::exception& e) {
        std::printf("      (snapshot decode failed: %s)\n", e.what());
    }
}

void probe(int only) {
    std::printf("go probe: stag0201, slot 0 walks, slot 1 at matz01 (to dng204b1 point 0) from load + %llu; the exit port's departure\n"
                "(slot 1 on the mat answers Yes) against a GO of slot 1 to the port's barrier target before the port's fork step\n",
                static_cast<unsigned long long>(kGoTeleport));
    const int zone_cases[] = {0, 1, 2};
    for (int ci = 0; ci < 3; ++ci) {
        if (only >= 0 && only != ci) continue;
        const int zones = zone_cases[ci];
        std::printf("  zone shuffle %s\n", zones ? (std::string("on (seed 4242, zones ") + std::to_string(zones) + ")").c_str() : "off");
        const Run pl = run(1, zones, {}, 0, 0, {});
        const Run ps = run(0, zones, {}, 0, 0, {});
        std::printf("    port, leavers' copy: teleport %llu, %s, %s; barrier %s point %d (alive %x present %x) at %llu, load step %llu (%s)\n",
                    static_cast<unsigned long long>(pl.teleport), split_text(pl.split).c_str(), heading_text(pl.heading).c_str(),
                    pl.barrier_stage.c_str(), pl.barrier_point, unsigned(pl.alive), unsigned(pl.present), static_cast<unsigned long long>(pl.park),
                    static_cast<unsigned long long>(pl.load), pl.load_stage.c_str());
        std::printf("    port, stayers' copy: %s, end present %x stage %s\n", split_text(ps.split).c_str(), unsigned(ps.present_end), ps.stage_end.c_str());
        if (!pl.split || pl.barrier_stage.empty()) {
            std::printf("    (the port did not depart: no comparison)\n");
            continue;
        }
        const std::uint64_t fork = pl.split->frame;
        const Run gl = run(1, zones, pl.barrier_stage, pl.barrier_point, fork, {});
        const Run gs = run(0, zones, pl.barrier_stage, pl.barrier_point, fork, {});
        std::printf("    GO, leavers' copy:   teleport %llu, %s, %s; barrier %s point %d (alive %x present %x) at %llu, load step %llu (%s)\n",
                    static_cast<unsigned long long>(gl.teleport), split_text(gl.split).c_str(), heading_text(gl.heading).c_str(),
                    gl.barrier_stage.c_str(), gl.barrier_point, unsigned(gl.alive), unsigned(gl.present), static_cast<unsigned long long>(gl.park),
                    static_cast<unsigned long long>(gl.load), gl.load_stage.c_str());
        std::printf("    GO, stayers' copy:   %s, end present %x stage %s\n", split_text(gs.split).c_str(), unsigned(gs.present_end), gs.stage_end.c_str());
        std::printf("    the GO's target: %s (the port's barrier %s point %d, the mat's named dng204b1 point 0)\n",
                    gl.barrier_stage == pl.barrier_stage && gl.barrier_point == pl.barrier_point ? "the same stage and point" : "DIFFERS",
                    pl.barrier_stage.c_str(), pl.barrier_point);
        if (zones) {
            // Counter-proof of the remap's skip: STAGECTL's own remap of the
            // already remapped target (what a NEXTSTAGE without I<1> sends).
            // The remap is keyed by the field a departure leaves: a GO from a
            // field whose own entrance names the target would be sent on.
            for (const char* from : {"stag0201", "stag0202", "stag0203", "stag0204", "stag0205"}) {
                const auto again = randomizer::zone_remap(randomizer::sub_seed(4242, "zones"), zones, from, pl.barrier_stage, pl.barrier_point);
                std::printf("    counter-proof: a GO from %s to %s point %d with the remap would lead to %s point %d%s\n", from,
                            pl.barrier_stage.c_str(), pl.barrier_point, again ? again->stage.c_str() : pl.barrier_stage.c_str(),
                            again ? again->point : pl.barrier_point, again && again->stage != pl.barrier_stage ? " (WRONG: caught)" : "");
            }
        }
        std::printf("    departure: port fork -> park %llu frames, GO %llu frames; park -> load step: port %llu, GO %llu\n",
                    static_cast<unsigned long long>(pl.park - fork), gl.park ? static_cast<unsigned long long>(gl.park - fork) : 0ull,
                    static_cast<unsigned long long>(pl.load - pl.park), static_cast<unsigned long long>(gl.load - gl.park));
        // Both copies of the GO's fork, live and replayed with the recorded side.
        std::uint64_t n = 0;
        auto d = first_diff(gs.fp, gl.fp, n);
        std::printf("    GO copies before the fork: stayers vs leavers %s\n",
                    d && gl.split && d < gl.split->frame ? ("differ at " + std::to_string(d)).c_str() : "equal up to the fork step");
        if (gl.split) {
            // (The client's fork list: a fork reported with tick t is the
            // engine's frame after the step of t, GameEngineLink::
            // set_fork_sides: for the GO the step it came before, `fork`.)
            const ForkSide stay{fork, gl.split->leavers, false}, leave{fork, gl.split->leavers, true};
            const Run rs = run(-1, zones, pl.barrier_stage, pl.barrier_point, fork, {stay});
            const Run rl = run(-1, zones, pl.barrier_stage, pl.barrier_point, fork, {leave});
            d = first_diff(gs.fp, rs.fp, n);
            std::printf("    GO replay (identity -1) of the stayers' side: %s over %llu frames\n", d ? ("differs at " + std::to_string(d)).c_str() : "equal",
                        static_cast<unsigned long long>(n));
            d = first_diff(gl.fp, rl.fp, n);
            std::uint64_t n2 = 0;
            const auto d2 = first_diff(gl.after, rl.after, n2);
            std::printf("    GO replay of the leavers' side: %s over %llu frames to the park, after the load %s over %llu frames (%s)\n",
                        d ? ("differs at " + std::to_string(d)).c_str() : "equal", static_cast<unsigned long long>(n),
                        d2 ? ("differs at +" + std::to_string(d2)).c_str() : "equal", static_cast<unsigned long long>(n2), rl.load_stage.c_str());
            // Counter-proof: the exit's frame convention (the fork inside the step) misses the GO's side.
            const ForkSide early{fork - 1, gl.split->leavers, true};
            const Run rx = run(-1, zones, pl.barrier_stage, pl.barrier_point, fork, {early});
            d = first_diff(gl.fp, rx.fp, n);
            std::printf("    counter-proof, the leavers' side recorded one frame early (ForkKind::exit's frame): %s (replay ends in %s, present %x)\n",
                        d ? ("differs from " + std::to_string(d)).c_str() : "equal (NOT caught)", rx.stage_end.c_str(), unsigned(rx.present_end));
        }
        // The port's fork, replayed the same way (sanity).
        {
            const ForkSide leave{pl.split->frame, pl.split->leavers, true};
            const Run rl = run(-1, zones, {}, 0, 0, {leave});
            d = first_diff(pl.fp, rl.fp, n);
            std::printf("    port replay of the leavers' side: %s over %llu frames\n", d ? ("differs at " + std::to_string(d)).c_str() : "equal",
                        static_cast<unsigned long long>(n));
        }
        // The leavers' load: GO against the port.
        compare_snapshots(gl.canonical, pl.canonical);
        d = first_diff(gl.after, pl.after, n);
        std::printf("      fingerprints after the load step: %s over %llu frames\n", d ? ("differ from +" + std::to_string(d)).c_str() : "equal",
                    static_cast<unsigned long long>(n));
        d = first_diff(gs.fp, ps.fp, n);
        std::printf("      stayers' copies (GO vs port): %s (the teleport at %llu)\n", d ? ("differ from " + std::to_string(d)).c_str() : "equal",
                    static_cast<unsigned long long>(pl.teleport));
    }
}

} // namespace go_probe

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dw4_world_probe DATA settings|redirect|homeover|guestnew|go\n");
        return 2;
    }
    g_data = argv[1];
    const std::string what = argv[2];
    // The throwaway memory card folder goes on every way out (a failed probe
    // left it in the temp folder).
    struct CardFolder {
        CardFolder() { std::filesystem::create_directories(card_root()); }
        ~CardFolder() {
            std::error_code ec;
            std::filesystem::remove_all(card_root(), ec);
        }
    } cards;
    try {
        if (what == "settings") settings_probe();
        else if (what == "redirect") redirect_probe();
        else if (what == "homeover") homeover_probe(argc > 3 ? std::atoi(argv[3]) : -1);
        else if (what == "guestnew") guestnew_probe();
        else if (what == "mats" && argc > 3) mats_probe(argv[3]);
        else if (what == "go") go_probe::probe(argc > 3 ? std::atoi(argv[3]) : -1);
        else {
            std::fprintf(stderr, "unknown probe %s\n", what.c_str());
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "probe failed: %s\n", e.what());
        return 1;
    }
    return 0;
}
