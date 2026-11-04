// Data Overflow, round O1a lane C: T7 of docs/SURVIVAL.md 14.1 (the
// director's unit tests). One program, one ctest entry per part:
//   offers   (no data)  eligibility, weights, never an empty window or the
//                       same offer twice, the Train caps, the plug points,
//                       the test bot's priority
//   rng      (no data)  the director's own generator: a seed gives one
//                       stream, the system generator is not drawn by it;
//                       the story's generator survives a run engine
//   spawn    (data)     the Violet Ring's walkable cells, spawn points on
//                       them at the ring's distance, the living count
//                       topped up to A(t), an encirclement that reuses the
//                       RAMs of the 16 farthest enemies
//   balance  (data)     14.3: no one-hit enemies (the worst hit of every
//                       family at its phase's expected levels, Corrupted
//                       included, measured with the run's damage-taken
//                       rule, at most 35 % of MaxHP), quick early kills of
//                       the start Art and TECH1, enough RAMs per phase, no
//                       Guardian family in the pools
// `report` prints the balance table without failing (the tuning pass).
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/overflow.hpp"
#include "dw4/engine/overflow_director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4::engine;
using namespace dw4::engine::overflow;

int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

// ---------------------------------------------------------------------------
// offers
// ---------------------------------------------------------------------------
void offers() {
    check(train_cap(0) == 300 && train_cap(1) == 750 && train_cap(2) == 1250 && train_cap(3) == 1750 && train_cap(4) == 2500 && train_cap(9) == 2500,
          "the Train caps 300 > 750 > 1250 > 1750 > 2500");
    check(plug_points(1) == 12 && plug_points(20) == 50, "a Plug: 10 + 2 x run level");
    OfferState state;
    state.groups = {true, true, false}; // Agumon / Veemon in O1a: Blast and Heal
    auto eligible = eligible_offers(state);
    check(std::count_if(eligible.begin(), eligible.end(), [](const Offer& o) { return o.kind == OfferKind::forge && o.value == 2; }) == 1,
          "Forge 2 is open after Forge 1");
    check(std::count_if(eligible.begin(), eligible.end(), [](const Offer& o) { return o.kind == OfferKind::train; }) == 2,
          "Train for the form's two groups only");
    check(std::count_if(eligible.begin(), eligible.end(), [](const Offer& o) { return o.kind == OfferKind::plug; }) == 11, "every plug stat");
    // Draws: three different offers, by the weights.
    std::uint64_t random = 12345;
    std::map<OfferKind, int> lines;
    for (int i = 0; i < 4000; ++i) {
        const auto window = draw_offers(state, random);
        check(window.size() == 3, "a window has three lines");
        for (std::size_t a = 0; a < window.size(); ++a)
            for (std::size_t b = a + 1; b < window.size(); ++b) check(!(window[a] == window[b]), "never the same offer twice");
        for (const auto& offer : window) ++lines[offer.kind];
    }
    const double forge = double(lines[OfferKind::forge]) / 4000.0, train = double(lines[OfferKind::train]) / 4000.0;
    std::cout << "  offers: Forge in " << forge * 100 << " % of the windows, Train lines " << train << " a window, Plug lines "
              << double(lines[OfferKind::plug]) / 4000.0 << " a window\n";
    check(forge > 0.55 && forge < 0.85, "Forge comes often (25 of 70 by weight, first of three draws)");
    check(lines[OfferKind::plug] > 4000, "plugs take lines by their weight");
    // Spent: Forge 5, both groups at Train 4: only plugs, still three lines.
    OfferState spent = state;
    spent.forge = 5;
    spent.train = {4, 4, 0};
    for (int i = 0; i < 200; ++i) {
        const auto window = draw_offers(spent, random);
        check(window.size() == 3 && std::all_of(window.begin(), window.end(), [](const Offer& o) { return o.kind == OfferKind::plug; }),
              "no empty window: the plugs fill it");
    }
    // The bot's priority: Forge, Train Blast, Train Heal, then Strength first.
    check(offer_priority({OfferKind::forge, 3}) < offer_priority({OfferKind::train, kBlastSkill}) &&
              offer_priority({OfferKind::train, kBlastSkill}) < offer_priority({OfferKind::train, kHealSkill}) &&
              offer_priority({OfferKind::train, kHealSkill}) < offer_priority({OfferKind::plug, 2}) &&
              offer_priority({OfferKind::plug, 2}) < offer_priority({OfferKind::plug, 0}),
          "the priority list");
    // The tables: the phase pools and their weights.
    for (int phase = 1; phase <= 4; ++phase) {
        const auto pool = phase_pool(phase);
        for (const auto& entry : pool)
            check(entry.row->phase <= phase && entry.weight == (entry.row->phase == phase ? 3.0f : 0.5f) * entry.row->weight, "pool weights");
    }
    check(phase_pool(4).size() == 21 && phase_pool(1).size() == 6, "21 families, six in phase 1");
}

// ---------------------------------------------------------------------------
// rng
// ---------------------------------------------------------------------------
void rng() {
    std::uint64_t a = 77, b = 77, c = 78;
    bool differ = false;
    for (int i = 0; i < 100; ++i) {
        const auto x = next_random(a), y = next_random(b), z = next_random(c);
        check(x == y, "one seed, one stream");
        differ = differ || x != z;
    }
    check(differ, "another seed, another stream");
    for (int i = 0; i < 1000; ++i) {
        const double u = next_unit(a);
        check(u >= 0.0 && u < 1.0, "next_unit in [0, 1)");
    }
    // The director's draws leave the system generator alone.
    auto& system = fight::generator();
    system.seed(SystemRandom::kBootSeed);
    const SystemRandom before = system;
    OverflowDirector director(RunConfig{});
    OfferState state;
    state.groups = {true, false, false};
    std::uint64_t random = 9;
    for (int i = 0; i < 100; ++i) (void)draw_offers(state, random);
    check(system == before, "offers and the director's own draws never touch fight::generator");
    // The story's generator across a run engine's construction: the host's
    // StoryRandomGuard (overflow_host tests it); here the reseed it guards against.
    {
        for (int i = 0; i < 99; ++i) (void)system();
        const SystemRandom story = system;
        {
            Engine run{dw4::MountedResources{}};
            check(!(system == story), "an Engine's construction reseeds the process-wide generator");
        }
        system = story; // (what the guard does)
        check(system == story, "and the story's state can be put back");
    }
}

// ---------------------------------------------------------------------------
// A headless run as the host makes it (overflow_run.cpp prepare_run_engine)
// ---------------------------------------------------------------------------
struct TestRun {
    std::unique_ptr<Engine> engine;
    std::shared_ptr<OverflowRules> rules;
    std::unique_ptr<OverflowDirector> director;
};

std::filesystem::path card_dir() {
    auto dir = std::filesystem::temp_directory_path() / "dw4_overflow_director_tests_cards";
    std::filesystem::create_directories(dir / "Card A");
    std::filesystem::create_directories(dir / "Card B");
    return dir;
}

TestRun start_run(const std::filesystem::path& data, RunConfig config, bool god) {
    TestRun run;
    run.engine = std::make_unique<Engine>(data);
    auto& engine = *run.engine;
    engine.set_card_root(card_dir());
    CardSetting none;
    none.mode = CardSetting::Mode::none;
    engine.set_card_setting(0, none);
    engine.set_card_setting(1, none);
    engine.cheats() = Cheats{};
    engine.cheats().fast_loading = true;
    engine.cheats().god = god;
    engine.set_port_events(false);
    run.rules = std::make_shared<OverflowRules>(RuleSet::run);
    const std::string form = config.form;
    const auto seed = config.seed;
    run.director = std::make_unique<OverflowDirector>(std::move(config));
    run.director->attach(engine, *run.rules);
    engine.set_overflow_rules(run.rules);
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].digimon = form;
    sys->players[0].joined = true;
    for (std::size_t slot = 1; slot < sys->players.size(); ++slot) sys->players[slot].joined = false;
    sys->ninzu = 1;
    fight::generator().seed(seed);
    engine.request_stage("stagbs02");
    return run;
}

void run_step(TestRun& run, bool bot) {
    std::array<PadSample, 4> pads{};
    pads[0] = bot ? run.director->bot_pad(*run.engine) : run.director->filter_pad(*run.engine, dw4::host_pad_sample({}));
    run.engine->step(pads);
    if (!run.engine->held()) run.engine->traverse_scene();
}

void until_started(TestRun& run) {
    for (int i = 0; i < 4000 && run.director->run_steps() < 2; ++i) run_step(run, false);
    check(run.director->run_steps() >= 2, "the run started on the Violet Ring");
}

Vec3f position(const ModelCtrlManager::ModelRam& ram) {
    return ram.node ? Vec3f{ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)} : Vec3f{};
}
float flat(const Vec3f& a, const Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }

bool living(const ModelCtrlManager::ModelRam& ram) {
    return ram.generated && ram.category == "ENEMY" && ram.hp > 0 && ram.seq_name != "SQ_ERAS" && ram.info_name.starts_with("e_");
}

// ---------------------------------------------------------------------------
// spawn
// ---------------------------------------------------------------------------
void spawn(const std::filesystem::path& data) {
    RunConfig config;
    config.seed = 3;
    config.offers = "priority";
    config.tuning.abilities = false; // (nothing dies: the living count is the spawner's)
    auto run = start_run(data, config, true);
    until_started(run);
    auto& engine = *run.engine;
    auto& director = *run.director;
    auto* models = engine.get<ModelCtrlManager>();
    const Vec3f centre = director.arena_centre();
    std::cout << "  spawn: arena centre (" << centre.x << ", " << centre.z << "), radius " << director.arena_radius() << '\n';
    check(director.arena_radius() > 95.0f && director.arena_radius() < 125.0f, "the Violet Ring: a circle of r about 115");
    check(director.walkable(centre.x, centre.z) && !director.walkable(centre.x + 140.0f, centre.z), "inside walkable, past the wall not");
    // Spawn points: on walkable cells, 40-60 units from the player.
    const Vec3f me = position(*models->player(0));
    int points = 0;
    for (int i = 0; i < 300; ++i) {
        Vec3f at;
        if (!director.sample_spawn_point(engine, 40.0f, 60.0f, i % 3 != 0, at)) continue;
        ++points;
        const float d = flat(at, me);
        check(d >= 39.0f && d <= 61.0f, "a ring point 40-60 units away (" + std::to_string(d) + ")");
        check(director.walkable(at.x, at.z), "a ring point on a walkable cell");
    }
    check(points >= 280, "the ring has room on the Violet Ring");
    // A(t): 14 + 3.2 a minute, two spawns every ten steps.
    for (int i = 0; i < 600; ++i) run_step(run, false);
    int alive = 0;
    for (const auto& ram : models->rams()) alive += living(*ram) ? 1 : 0;
    std::cout << "  spawn: after 10 s " << alive << " enemies (A = " << int(14 + 3.2 * 10.0 / 60.0) << ")\n";
    check(alive >= 13 && alive <= 15, "the living count tops up to A(t)");
    // An encirclement reuses the RAMs of the 16 farthest: the same RAMs live
    // after it, 16 of them about 45 units round the player.
    for (int i = 0; i < 1200; ++i) run_step(run, false);
    std::set<std::pair<std::string, int>> before, after;
    for (const auto& ram : models->rams())
        if (living(*ram)) before.insert({ram->info_name, ram->index});
    director.encircle_now(engine);
    for (int i = 0; i < 2; ++i) run_step(run, false);
    const Vec3f here = position(*models->player(0));
    int ring = 0;
    for (const auto& ram : models->rams())
        if (living(*ram)) {
            after.insert({ram->info_name, ram->index});
            if (std::fabs(flat(position(*ram), here) - 45.0f) < 6.0f) ++ring;
        }
    std::cout << "  spawn: encirclement: " << before.size() << " living before, " << after.size() << " after, " << ring
              << " on the 45-unit ring\n";
    check(before.size() >= 14, "enough enemies for an encirclement");
    check(std::includes(before.begin(), before.end(), after.begin(), after.end()) && after.size() + 2 >= before.size(),
          "the encirclement made no new RAM");
    check(ring >= int(std::min<std::size_t>(before.size(), 16)) - 3, "the encircled enemies stand round the player");
}

// ---------------------------------------------------------------------------
// balance (14.3)
// ---------------------------------------------------------------------------
// The levels at the phases' starts: the kite bot's runs (T6, Agumon, 20
// seeds) reach 15-16 at 5:00, 25-27 at 10:00 and 34 at 15:00; the test
// takes a level less than the slowest (4.5's estimate was 12 / 22 / 32 / 40).
constexpr int kLevelAt[5] = {1, 14, 24, 32, 40};

void set_level(Engine& engine, int level) {
    auto* sys = engine.get<GameSystem>();
    auto& player = sys->players[0];
    player.stats.level = level;
    sys->recalc_player(engine, 0);
    player.hp = player.max_hp;
    // (The model keeps its own MaxHP, as apply_level_ups sets it: the hit script reads it.)
    if (auto* self = engine.get<ModelCtrlManager>()->find(player.digimon, 0)) {
        self->max_hp = player.max_hp;
        self->hp = player.hp;
    }
}

int spawn_around(Engine& engine, const FamilyRow& row, int count, float ring) {
    auto* models = engine.get<ModelCtrlManager>();
    const Vec3f me = position(*models->player(0));
    int made = 0;
    const Messenger::PortOnly scope(engine.messenger());
    for (int k = 0; k < count; ++k) {
        Packet find;
        engine.messenger().send(engine, "MODELCTRL", row.family, -1, "FIND_SPACE", find);
        if (find.ints.empty()) break;
        const float a = 6.2831853f * float(k) / float(count);
        Packet generate;
        generate.vectors = {Vec3f{me.x + ring * std::sin(a), me.y, me.z + ring * std::cos(a)}, Vec3f{}};
        generate.strings.emplace_back(std::string("SQ_ENTR"));
        generate.ints = {row.record, 99999};
        engine.messenger().send(engine, "MODELCTRL", row.family, find.ints[0], "GENERATE", generate);
        Packet on;
        engine.messenger().send(engine, "MODELCTRL", row.family, find.ints[0], "ONCTRL", on);
        ++made;
    }
    return made;
}

void remove_enemies(Engine& engine) {
    auto* models = engine.get<ModelCtrlManager>();
    const Messenger::PortOnly scope(engine.messenger());
    for (const auto& ram : models->rams())
        if (ram->generated && ram->category == "ENEMY" && ram->info_name.starts_with("e_")) {
            Packet p;
            engine.messenger().send(engine, "MODELCTRL", ram->info_name, ram->index, "REMOVE", p);
        }
}

void balance(const std::filesystem::path& data, bool report) {
    // Static: no Guardian family in a pool (4.2), enough RAMs per phase (measured below).
    for (const char* guardian : {"e_blosso", "e_mammos", "e_skulgrey", "e_anomalo", "e_tonogeko", "e_diabolo", "e_ldknight"})
        check(!family_row(guardian), std::string("no Guardian family in the pools: ") + guardian);
    bool failed = false;
    std::string first_failure;
    for (const char* form : {"p_agumon", "p_vmon"}) {
        RunConfig config;
        config.form = form;
        config.seed = 5;
        config.offers = "priority";
        config.tuning.spawning = false;
        config.tuning.abilities = false;
        config.tuning.time_scaling = false;
        auto run = start_run(data, config, true);
        until_started(run);
        auto& engine = *run.engine;
        auto& rules = *run.rules;
        auto* db = engine.get<DatabaseManager>();
        auto* sys = engine.get<GameSystem>();
        auto* models = engine.get<ModelCtrlManager>();
        rules.damage.hit_clamp = 100.0f; // (measure the raw hits: the clamp must never be needed)
        // Enough RAMs: every phase's pool >= its highest A(t) + 28 (a surge and an encirclement).
        if (std::string(form) == "p_agumon") {
            std::map<std::string, int> rams;
            for (const auto& ram : models->rams()) ++rams[ram->info_name];
            const int highest[5] = {0, 30, 46, 60, 60};
            for (int phase = 1; phase <= 4; ++phase) {
                int total = 0;
                for (const auto& entry : phase_pool(phase)) total += rams[entry.row->family];
                std::cout << "  balance: phase " << phase << " pool " << total << " RAMs (needs " << highest[phase] + 28 << ")\n";
                check(total >= highest[phase] + 28, "enough RAMs for phase " + std::to_string(phase));
            }
        }
        // Quick early kills (the median roll): the start Art (x 2.5) and TECH1 Petty at levels 1 and 12.
        for (const int level : {1, 12}) {
            set_level(engine, level);
            const auto& stats = sys->players[0].stats;
            const int skill = level == 1 ? 1 : 150;
            const int tech_skill = level == 1 ? 1 : 150;
            const double minutes = level == 1 ? 0.0 : 4.99;
            const int tech = db->number("shinka", stats.species, "TECH1").value_or(0);
            for (const auto& row : family_table()) {
                if (row.phase != 1) continue;
                const double hp = double(db->number("enemy", row.record, "HPMAX").value_or(1)) * (1.0 + double(RunConfig{}.tuning.hp_per_minute) * minutes) * 0.6;
                const int mamori = db->number("enemy", row.record, "MAMORI").value_or(1);
                const double art = double(stats.tsuyosa * 6 + skill * 4) / double(mamori) * 2.5 * double(art_class_factor(form));
                const int spirit = db->number("enemy", row.record, "SEISHIN").value_or(1);
                static const char* resist[] = {"RES_FIRE", "RES_ICE", "RES_THUNDER", "RES_DARK"};
                const int element = std::string(form) == "p_agumon" ? 0 : 2;
                const int res = db->number("enemy", row.record, resist[element]).value_or(0);
                const double technique =
                    double(stats.kashikosa * 10 + tech_skill * 5 + 160 * 20) / double(spirit) * double(1000 - res) / 1000.0 * 1.15;
                const int art_hits = int(std::ceil(hp / std::max(1.0, art))), tech_hits = int(std::ceil(hp / std::max(1.0, technique)));
                if (report)
                    std::printf("  kills %s L%d %-11s HP %5.0f Art %5.1f (%d hits) TECH1 %5.1f (%d)\n", form, level, row.family, hp, art, art_hits,
                                technique, tech_hits);
                // (A family that resists the form's element by half or more
                // stays the element's own rule: Sukamon against Fire.)
                if (art_hits > 2 || (tech_hits > 2 && res < 500)) {
                    failed = true;
                    if (first_failure.empty()) first_failure = std::string(form) + " needs more than two hits for " + row.family;
                }
                (void)tech;
            }
        }
        // No one-hit enemies: each family at its phase's start and end level
        // with the time factor there, Corrupted x 1.5 on top.
        for (int phase = 1; phase <= 4; ++phase)
            for (int end = 0; end < 2; ++end) {
                const int level = kLevelAt[phase - 1 + end];
                const double minutes = 5.0 * double(phase - 1 + end) - (end ? 0.01 : 0.0);
                set_level(engine, level);
                rules.damage.taken_factor = run.director->damage_factor(minutes);
                for (const auto& row : family_table()) {
                    if (row.phase != phase) continue;
                    const int made = spawn_around(engine, row, 6, 12.0f);
                    for (int i = 0; i < 1500; ++i) run_step(run, false);
                    remove_enemies(engine);
                    for (int i = 0; i < 4; ++i) run_step(run, false);
                    const auto& by = run.director->hits_by_family();
                    const auto found = by.find(row.family);
                    const double share = found == by.end() ? 0.0 : found->second.second;
                    const int hits = found == by.end() ? 0 : found->second.first;
                    const double corrupted = share * double(OverflowRules{}.damage.corrupted_factor);
                    if (report || corrupted > 0.35)
                        std::printf("  hits %s phase %d %s L%2d x%.2f %-11s %2d spawned, %3d hits so far, worst %4.1f %% (Corrupted %4.1f %%)\n", form,
                                    phase, end ? "end  " : "start", level, double(rules.damage.taken_factor), row.family, made, hits, share * 100.0,
                                    corrupted * 100.0);
                    if (corrupted > 0.35) {
                        failed = true;
                        if (first_failure.empty()) first_failure = std::string(form) + ": " + row.family + " hits for more than 35 % of MaxHP";
                    }
                    // (Start over per family and level: the table keeps the worst.)
                    run.director->reset_hits_by_family();
                }
            }
    }
    if (!report) check(!failed, first_failure);
}

} // namespace

int main(int argc, char** argv) {
    const std::string part = argc > 1 ? argv[1] : "offers";
    std::string data = argc > 2 ? argv[2] : "";
    if (data.empty())
        if (const char* env = std::getenv("DW4_TEST_DATA")) data = env;
    try {
        if (part == "offers") offers();
        else if (part == "rng") rng();
        else if (part == "spawn" || part == "balance" || part == "report") {
            if (data.empty() || !std::filesystem::exists(data)) {
                std::cout << "overflow " << part << ": skipped (no game data: DW4_TEST_DATA)\n";
                return 0;
            }
            if (part == "spawn") spawn(data);
            else balance(data, part == "report");
        } else {
            std::cerr << "unknown part " << part << '\n';
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "overflow " << part << " test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overflow " << part << ": " << checks << " checks passed\n";
    return 0;
}
