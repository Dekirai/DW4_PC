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
//   dw4_world_probe DATA homeover [CASE]  a stage group left with only defeated players
//                                   after a HOME (19.6.5)
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_membership.hpp"
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
    // DISSOLVE: the modes set back in the parked stage change (before the
    // load step) against a Game Start with them: the hub's loaded files.
    std::printf("dissolve: modes of H while in stag0101, own modes O set while the stage change to stagml11 is parked\n");
    auto lobby_files = [&](const RoomSettings& start, const std::optional<RoomSettings>& at_park, std::string& light_line) {
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
        bool posted = false, parked = false;
        std::uint64_t loaded_at = 0;
        std::vector<std::string> files;
        for (int i = 0; i < 6000; ++i) {
            if (!posted && engine.frame() > 1200 && engine.stage_name() == "stag0101" && engine.membership_safe()) {
                post_departure(engine, "stagML11", 3);
                posted = true;
            }
            if (posted && !parked && engine.pending_stage()) {
                parked = true;
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
            if (engine.stage_name() == "stagml11" && !engine.loading() && !loaded_at) {
                loaded_at = engine.frame();
                files = file_names(engine);
                break;
            }
        }
        light_line = "loaded at " + std::to_string(loaded_at);
        engine.set_online(OnlineRules{});
        return files;
    };
    RoomSettings own = base;
    RoomSettings host = all;
    host.fast_loading = false;
    std::string l1, l2, l3;
    const auto direct_own = lobby_files(own, std::nullopt, l1);
    const auto host_kept = lobby_files(host, std::nullopt, l2);
    const auto set_back = lobby_files(host, own, l3);
    auto describe = [](const std::vector<std::string>& files) {
        std::string camera, light;
        for (const auto& f : files) {
            if (f.find("aaaCamera") != std::string::npos) camera += f + " ";
            if (f.find("ACMR") != std::string::npos || f.find("Light") != std::string::npos || f.find("light") != std::string::npos) light += f + " ";
        }
        return std::to_string(files.size()) + " files; camera: " + camera;
    };
    std::printf("  own modes all along (%s): %s, %s\n", settings_text(own).c_str(), describe(direct_own).c_str(), l1.c_str());
    std::printf("  host modes kept (%s): %s, %s; equal to own: %s\n", settings_text(host).c_str(), describe(host_kept).c_str(), l2.c_str(),
                host_kept == direct_own ? "yes" : "no");
    std::printf("  host modes, own set back at the park: %s, %s; equal to own: %s\n", describe(set_back).c_str(), l3.c_str(),
                set_back == direct_own ? "yes" : "no");
    for (const auto& f : host_kept)
        if (std::find(direct_own.begin(), direct_own.end(), f) == direct_own.end()) std::printf("    host modes load %s\n", f.c_str());
    for (const auto& f : direct_own)
        if (std::find(host_kept.begin(), host_kept.end(), f) == host_kept.end()) std::printf("    own modes load %s\n", f.c_str());
    if (set_back != direct_own) {
        for (std::size_t k = 0; k < std::max(set_back.size(), direct_own.size()); ++k) {
            const std::string a = k < set_back.size() ? set_back[k] : "-";
            const std::string b = k < direct_own.size() ? direct_own[k] : "-";
            if (a != b) std::printf("    file %zu: set back %s, own %s\n", k, a.c_str(), b.c_str());
        }
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
};

enum class RedirectAt { none, park, after_first_transition_step, park_stage_only };

RedirectRun run_redirect(const std::string& first_target, int first_point, RedirectAt at, const std::string& to, int to_point,
                         const RoomSettings& settings) {
    RedirectRun run;
    std::optional<Engine> holder;
    new_engine(holder);
    auto& engine = *holder;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].joined = true;
    set_modes(engine, settings);
    dw4::game::online::apply_room_cheats(settings, engine.cheats());
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 1;
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.request_stage("stag0101");
    bool posted = false;
    int transition_steps = 0;
    std::uint64_t serial = 0;
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
            BarrierResult b;
            b.decision = Decision::dissolve;
            b.own_slot = 0;
            b.rules = engine.online();
            engine.set_barrier(b);
            if (at == RedirectAt::park || at == RedirectAt::park_stage_only) {
                engine.request_stage(to);
                if (at == RedirectAt::park)
                    if (auto* points = engine.get<PlayerPointManager>()) points->set_point_no(to_point);
                run.redirect_frame = frame;
            }
        }
        if (run.park_frame && !run.load_frame && at == RedirectAt::after_first_transition_step && transition_steps == 1) {
            engine.request_stage(to);
            if (auto* points = engine.get<PlayerPointManager>()) points->set_point_no(to_point);
            run.redirect_frame = frame;
        }
        if (run.park_frame && !run.load_frame) ++transition_steps;
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
    struct Case {
        const char* name;
        std::string first;
        int first_point;
        RedirectAt at;
        RoomSettings settings;
    };
    const Case cases[] = {
        {"NEXTSTAGE stagml11 point 3 (reference)", "stagML11", 3, RedirectAt::none, base},
        {"NEXTSTAGE stagml02 point 0, redirect at the park", "stagML02", 0, RedirectAt::park, base},
        {"NEXTSTAGE stag0102 point 0, redirect at the park", "stag0102", 0, RedirectAt::park, base},
        {"NEXTSTAGE stagml02 point 0, redirect after the first transition step", "stagML02", 0, RedirectAt::after_first_transition_step, base},
        {"NEXTSTAGE stagml02 point 0, redirect of the stage only", "stagML02", 0, RedirectAt::park_stage_only, base},
    };
    const Case zone_cases[] = {
        {"zone shuffle: NEXTSTAGE stagml11 point 3 (reference)", "stagML11", 3, RedirectAt::none, zones},
        {"zone shuffle: NEXTSTAGE dng101b1 point 0 (remapped), redirect at the park", "dng101b1", 0, RedirectAt::park, zones},
    };
    auto report = [](const RedirectRun& ref, const RedirectRun& r, const char* name) {
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
        std::printf("  %-72s park %llu, load step %llu (+%llu), %s point %d, load snapshot %s", name, static_cast<unsigned long long>(r.park_frame),
                    static_cast<unsigned long long>(r.load_frame), static_cast<unsigned long long>(r.teardown_fields), r.stage.c_str(), r.point_no,
                    same ? "equal" : "differs");
        if (!same) std::printf(" (from byte %zu of %zu)", first_byte, r.canonical.size());
        std::printf(", %llu frames compared, %s\n", static_cast<unsigned long long>(compared),
                    first_diff ? ("differ from " + std::to_string(first_diff)).c_str() : "equal");
    };
    std::printf("redirect probe: stag0101 direct start, departure at frame > 1200; DISSOLVE answered at the park\n");
    const auto ref = run_redirect(cases[0].first, cases[0].first_point, cases[0].at, "stagml11", 3, base);
    report(ref, ref, cases[0].name);
    for (std::size_t k = 1; k < std::size(cases); ++k) {
        const auto r = run_redirect(cases[k].first, cases[k].first_point, cases[k].at, "stagml11", 3, cases[k].settings);
        report(ref, r, cases[k].name);
    }
    const auto zref = run_redirect(zone_cases[0].first, zone_cases[0].first_point, zone_cases[0].at, "stagml11", 3, zones);
    report(zref, zref, zone_cases[0].name);
    const auto z = run_redirect(zone_cases[1].first, zone_cases[1].first_point, zone_cases[1].at, "stagml11", 3, zones);
    report(zref, z, zone_cases[1].name);
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

struct HomeCase {
    const char* name;
    int players;
    std::uint8_t killed;     // slots defeated at 1200 (in slot order, a frame apart)
    std::uint8_t home;       // the slots HOME takes; this copy keeps the others
    std::uint64_t home_after; // frames after the last kill
    std::uint64_t kill_from = 1200;
};

void homeover_probe(int only) {
    const HomeCase cases[] = {
        {"2 players: 1 defeated stays, HOME takes living 0", 2, 0b0010, 0b0001, 300},
        {"2 players: same, HOME 10 frames after the defeat", 2, 0b0010, 0b0001, 10},
        {"3 players: 1 and 2 defeated stay, HOME takes living 0", 3, 0b0110, 0b0001, 300},
        {"2 players: HOME takes defeated 1, living 0 stays", 2, 0b0010, 0b0010, 300},
        {"2 players: nobody defeated, HOME takes 0", 2, 0b0000, 0b0001, 300},
        {"2 players: both defeated, no HOME (the original)", 2, 0b0011, 0b0000, 300},
        {"1 player defeated, no HOME (the original, solo)", 1, 0b0001, 0b0000, 300},
        {"2 players: defeat at 1800, HOME 10 frames after", 2, 0b0010, 0b0001, 10, 1800},
        {"2 players: defeat at 1800, HOME 300 frames after", 2, 0b0010, 0b0001, 300, 1800},
        {"2 players: defeat at 2600, HOME 10 frames after", 2, 0b0010, 0b0001, 10, 2600},
    };
    const char* species[4] = {"p_agumon", "p_dorumon", "p_veemon", "p_guilmon"};
    std::printf("homeover probe: dng101b1 direct start, online rules; HOME emulated in the copy that keeps the other slots\n"
                "(Engine::online_remove_player per HOME slot: the despawn and membership step the fork's other side gets)\n");
    for (int index = 0; index < int(std::size(cases)); ++index) {
        if (only >= 0 && index != only) continue;
        const auto& c = cases[index];
        std::optional<Engine> holder;
        new_engine(holder);
        auto& engine = *holder;
        auto* sys = engine.get<GameSystem>();
        sys->direct_start = true;
        for (int s = 0; s < c.players; ++s) {
            sys->players[std::size_t(s)].joined = true;
            sys->players[std::size_t(s)].digimon = species[s];
        }
        OnlineRules rules;
        rules.enabled = true;
        rules.present = std::uint8_t((1u << c.players) - 1u);
        engine.set_online(rules);
        int keep = 0;
        for (int s = 0; s < 4; ++s)
            if ((rules.present & (1u << s)) && !(c.home & (1u << s))) {
                keep = s;
                break;
            }
        engine.set_split_identity(keep);
        engine.set_view_slot(keep);
        engine.request_stage("dng101b1");
        std::uint64_t kills_done_at = 0, home_at = 0, over_at = 0;
        int next_kill = 0;
        std::string over_stage;
        for (int i = 0; i < 6000; ++i) {
            const std::uint64_t frame = engine.frame();
            if (frame >= c.kill_from && !kills_done_at && engine.stage_name() == "dng101b1" && engine.membership_safe()) {
                while (next_kill < 4 && !(c.killed & (1u << next_kill))) ++next_kill;
                if (next_kill < 4) {
                    kill(engine, next_kill); // one defeat a frame
                    ++next_kill;
                } else {
                    kills_done_at = frame;
                }
            }
            if (kills_done_at && !home_at && c.home && frame >= kills_done_at + c.home_after && engine.membership_safe()) {
                for (int s = 0; s < 4; ++s)
                    if (c.home & (1u << s)) engine.online_remove_player(s);
                home_at = frame;
            }
            const bool before = engine.pending_stage().has_value();
            step(engine, pads_at(frame));
            if (!before && engine.pending_stage() && !over_at) {
                over_at = engine.frame();
                over_stage = std::string(*engine.pending_stage());
            }
            if (over_at && engine.frame() > over_at + 2000) break;
        }
        int alive = 0, generated = 0;
        if (const auto* models = engine.get<ModelCtrlManager>())
            for (int s = 0; s < 4; ++s)
                if (const auto* p = models->player(s); p && p->generated) {
                    ++generated;
                    alive += p->hp > 0 ? 1 : 0;
                }
        std::printf("  %-56s kills done %llu, HOME %llu, stage change %s at %llu (%s), final stage %s present %x, players generated %d alive %d\n",
                    c.name, static_cast<unsigned long long>(kills_done_at), static_cast<unsigned long long>(home_at), over_at ? "yes" : "none",
                    static_cast<unsigned long long>(over_at),
                    over_at ? ("+" + std::to_string(over_at - (home_at ? home_at : kills_done_at)) + " -> " + over_stage).c_str() : "-",
                    engine.stage_name().c_str(), unsigned(engine.online().present), generated, alive);
        engine.set_online(OnlineRules{});
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dw4_world_probe DATA settings|redirect|homeover\n");
        return 2;
    }
    g_data = argv[1];
    const std::string what = argv[2];
    std::filesystem::create_directories(card_root());
    try {
        if (what == "settings") settings_probe();
        else if (what == "redirect") redirect_probe();
        else if (what == "homeover") homeover_probe(argc > 3 ? std::atoi(argv[3]) : -1);
        else {
            std::fprintf(stderr, "unknown probe %s\n", what.c_str());
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "probe failed: %s\n", e.what());
        return 1;
    }
    std::error_code ec;
    std::filesystem::remove_all(card_root(), ec);
    return 0;
}
