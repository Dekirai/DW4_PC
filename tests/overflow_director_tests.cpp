// Data Overflow, round O1a lane C: T7 of docs/SURVIVAL.md 14.1 (the
// director's unit tests). One program, one ctest entry per part:
//   offers   (no data)  eligibility, weights, never an empty window or the
//                       same offer twice, the Train caps, the plug points,
//                       Train waiting for its group's first technique, the
//                       resistance plugs' smaller weight, the test bot's
//                       priority
//   rng      (no data)  the director's own generator: a seed gives one
//                       stream, the system generator is not drawn by it;
//                       the story's generator survives a run engine
//   spawn    (data)     the Violet Ring's walkable cells, spawn points on
//                       them at the ring's distance (one in the picture at
//                       least visible_min away), the living count topped
//                       up to A(t), an encirclement that reuses the RAMs of
//                       the 16 farthest enemies and keeps their HP, the
//                       followers a walking player leaves behind brought
//                       back on its way
//   gap      (data)     a spawn point never closes the player's last way
//                       out (Tuning::keep_gap_until)
//   human    (data)     the human bot (bot=human): one seed, one run; it
//                       walks, stops at times, its Art defeats enemies
//   poison   (data)     4.8: a poison tick never takes the last HP and never
//                       ends the run; MRβ02's poison through the run's rule
//                       takes at most 10 % of MaxHP a poisoning
//   balance  (data)     14.3: no one-hit enemies (the worst hit of every
//                       family, its poison included, at its phase's
//                       expected levels, Corrupted included, measured with
//                       the run's damage-taken rule, at most 35 % of MaxHP),
//                       no wall at a phase change (a new phase's families
//                       hit at its start about as the phase before hit at
//                       its end), quick early kills of the start Art and
//                       TECH1, enough RAMs per phase, no Guardian family in
//                       the pools
//   finale   (data)     round O1b lane E (4.4, 6.5): a Guardian by
//                       guardian_first (the boss bar, the camera shake, A(t)
//                       halved, its clamp counted as guardian_clamps, its
//                       defeat and drops, the separate packages' scripts),
//                       the finale's quest timer and TIME UP, the Awakening
//                       by a set skill with its burst, the Drain Ram through
//                       BLTgetDamageValue, the clear and the return gate
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
    std::uint64_t random = 12345;
    // The sure first Forge (the forge round of 05.11): the first line of each
    // of the first kForgeSure offers while Forge 1 is all the form has, the
    // other two lines by weight; not after them, not once a Forge is taken,
    // never for a form whose class has a single family.
    {
        OfferState sure = state;
        std::map<OfferKind, int> second;
        for (int i = 0; i < 3000; ++i) {
            sure.drawn = i % kForgeSure;
            const auto window = draw_offers(sure, random);
            check(window.size() == 3 && window[0] == Offer{OfferKind::forge, 2}, "the first Forge on the first line of the first offers");
            check(std::count_if(window.begin(), window.end(), [](const Offer& o) { return o.kind == OfferKind::forge; }) == 1, "one Forge line");
            ++second[window[1].kind];
        }
        check(second[OfferKind::train] > 0 && second[OfferKind::plug] > 0, "the other lines by weight");
        int forge_lines = 0;
        sure.drawn = kForgeSure;
        for (int i = 0; i < 2000; ++i) forge_lines += draw_offers(sure, random)[0].kind == OfferKind::forge ? 1 : 0;
        std::cout << "  offers: the first Forge on line 1 after the sure offers: " << forge_lines / 20.0 << " %\n";
        check(forge_lines > 500 && forge_lines < 1000, "after the first offers the Forge line is drawn by weight again (25 of 70)");
        sure.drawn = 0;
        sure.forge = 2;
        forge_lines = 0;
        for (int i = 0; i < 2000; ++i) forge_lines += draw_offers(sure, random)[0].kind == OfferKind::forge ? 1 : 0;
        check(forge_lines < 1000, "Forge 3 is not sure");
        sure.forge = 1;
        sure.forge_families = 1;
        for (int i = 0; i < 200; ++i) {
            const auto window = draw_offers(sure, random);
            check(std::none_of(window.begin(), window.end(), [](const Offer& o) { return o.kind == OfferKind::forge; }),
                  "no Forge for a class with one family");
        }
        sure.forge_families = 5;
        sure.forge_sure = 0;
        forge_lines = 0;
        for (int i = 0; i < 2000; ++i) forge_lines += draw_offers(sure, random)[0].kind == OfferKind::forge ? 1 : 0;
        check(forge_lines < 1000, "forge_sure 0: the balance round's draws");
    }
    // Draws: three different offers, by the weights (after the sure Forge).
    state.drawn = kForgeSure;
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
    // Train waits for its group's first technique (two levels ahead): Heal Lt
    // at run level 15 (Agumon's and Veemon's TECH3) shows no Train Heal at 2.
    OfferState early = state;
    early.unlock_level = {1, 15, 0};
    early.level = 2;
    auto none = eligible_offers(early);
    check(std::none_of(none.begin(), none.end(), [](const Offer& o) { return o.kind == OfferKind::train && o.value == kHealSkill; }) &&
              std::any_of(none.begin(), none.end(), [](const Offer& o) { return o.kind == OfferKind::train && o.value == kBlastSkill; }),
          "no Train Heal at level 2 for a Heal Lt of level 15, Train Blast for Fire");
    early.level = 13;
    none = eligible_offers(early);
    check(std::any_of(none.begin(), none.end(), [](const Offer& o) { return o.kind == OfferKind::train && o.value == kHealSkill; }),
          "Train Heal from two levels before Heal Lt");
    // A resistance plug weighs a quarter of a stat plug.
    {
        std::map<int, int> by_stat;
        std::uint64_t r = 99;
        for (int i = 0; i < 6000; ++i)
            for (const auto& offer : draw_offers(spent, r)) ++by_stat[offer.value];
        std::cout << "  offers: plug lines Strength " << by_stat[2] << ", Fire RES " << by_stat[7] << '\n';
        check(by_stat[7] * 2 < by_stat[2], "a resistance plug comes less often than a stat plug");
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
    // (The mechanisms with the review round's crowd: A(t) = 14 + 3.2 a
    // minute, the refill unlimited, no way out kept; `gap` tests that rule.)
    config.tuning.alive_at = {14.0f, 30.0f, 46.0f, 62.0f, 78.0f};
    config.tuning.refill_at = {12.0f, 12.0f, 12.0f, 12.0f, 12.0f};
    config.tuning.keep_gap_until = 0;
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
    // A point in the picture comes at least visible_min away (the review of
    // O1a: enemies entered next to the player): the slow walkers' 26-36 ring.
    int visible = 0, hidden = 0;
    for (int i = 0; i < 300; ++i) {
        Vec3f at;
        if (!director.sample_spawn_point(engine, 26.0f, 36.0f, i % 3 != 0, at, 40.0f)) continue;
        const float d = flat(at, me);
        if (on_screen(engine, {at.x, at.y + 5.0f, at.z})) {
            ++visible;
            check(d >= 39.0f, "a spawn in the picture is not next to the player (" + std::to_string(d) + ")");
        } else {
            ++hidden;
            check(d >= 25.0f && d <= 37.0f, "a hidden spawn keeps its ring (" + std::to_string(d) + ")");
        }
    }
    std::cout << "  spawn: 26-36 ring with visible_min 40: " << visible << " in the picture, " << hidden << " out of it\n";
    check(visible + hidden >= 280, "the near ring has room");
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
    ModelCtrlManager::ModelRam* farthest = nullptr;
    {
        const Vec3f at = position(*models->player(0));
        for (const auto& ram : models->rams())
            if (living(*ram)) {
                before.insert({ram->info_name, ram->index});
                if (!farthest || flat(position(*ram), at) > flat(position(*farthest), at)) farthest = ram.get();
            }
    }
    // (A hurt enemy keeps its HP when it comes again: the farthest at half.)
    check(farthest != nullptr, "a living enemy");
    const std::pair<std::string, int> hurt{farthest->info_name, farthest->index};
    {
        const Messenger::PortOnly scope(engine.messenger());
        Packet half;
        half.ints.push_back(std::max(1, farthest->max_hp / 2));
        engine.messenger().send(engine, "MODELCTRL", farthest->info_name, farthest->index, "SETHP", half);
    }
    director.encircle_now(engine);
    // (Four a step: the 16 are placed within four steps, T8.)
    for (int i = 0; i < 5; ++i) run_step(run, false);
    const Vec3f here = position(*models->player(0));
    int ring = 0;
    double hurt_share = -1;
    for (const auto& ram : models->rams())
        if (living(*ram)) {
            after.insert({ram->info_name, ram->index});
            if (std::fabs(flat(position(*ram), here) - 45.0f) < 6.0f) ++ring;
            if (ram->info_name == hurt.first && ram->index == hurt.second) hurt_share = double(ram->hp) / double(std::max(1, ram->max_hp));
        }
    std::cout << "  spawn: encirclement: " << before.size() << " living before, " << after.size() << " after, " << ring
              << " on the 45-unit ring; the hurt one at " << hurt_share * 100.0 << " % of its HP\n";
    check(before.size() >= 14, "enough enemies for an encirclement");
    check(std::includes(before.begin(), before.end(), after.begin(), after.end()) && after.size() + 2 >= before.size(),
          "the encirclement made no new RAM");
    check(ring >= int(std::min<std::size_t>(before.size(), 16)) - 3, "the encircled enemies stand round the player");
    check(hurt_share > 0.4 && hurt_share < 0.6, "an encircled enemy keeps the HP it had (not healed)");
    // Left behind (4.3): a player who walks on leaves its followers behind;
    // they come again on its way. A standing player's followers come
    // closer and stay.
    const auto recycled_before = director.recycled();
    for (int i = 0; i < 900; ++i) run_step(run, false);
    const auto standing = director.recycled() - recycled_before;
    for (int i = 0; i < 900; ++i) {
        std::array<PadSample, 4> pads{};
        const float angle = float(i) * 0.01f;
        pads[0] = director.filter_pad(engine, dw4::host_pad_sample({0, 0, 0, {std::sin(angle), std::cos(angle)}, {}}));
        engine.step(pads);
        if (!engine.held()) engine.traverse_scene();
    }
    const auto walking = director.recycled() - recycled_before - standing;
    std::cout << "  spawn: brought back in 15 s standing " << standing << ", walking " << walking << '\n';
    check(walking >= 8 && walking > 2 * standing, "a walking player's followers come again on its way");
}

// ---------------------------------------------------------------------------
// gap (the balance round of 05.11): a spawn never closes the last way out
// ---------------------------------------------------------------------------
void set_level(Engine& engine, int level);
int spawn_around(Engine& engine, const FamilyRow& row, int count, float ring);

// The sector (of eight, Tuning::keep_gap_until) of a point round `me`.
int sector_of(const Vec3f& me, const Vec3f& at) {
    const float angle = std::atan2(at.x - me.x, at.z - me.z) + 3.14159265f;
    return std::clamp(int(angle / (3.14159265f / 4.0f)), 0, 7);
}

void gap(const std::filesystem::path& data) {
    RunConfig config;
    config.seed = 6;
    config.offers = "priority";
    config.tuning.abilities = false;
    config.tuning.spawning = false;
    config.tuning.keep_gap_until = 1200 * 60;
    auto run = start_run(data, config, true);
    until_started(run);
    auto& engine = *run.engine;
    auto& director = *run.director;
    auto* models = engine.get<ModelCtrlManager>();
    const Vec3f me = position(*models->player(0));
    // Six of the eight sectors closed: a Goblimon in the middle of each at 25 units.
    const auto* row = family_row("e_goburi");
    check(row != nullptr, "Goblimon in the table");
    {
        const Messenger::PortOnly scope(engine.messenger());
        for (int k = 0; k < 6; ++k) {
            Packet find;
            engine.messenger().send(engine, "MODELCTRL", row->family, -1, "FIND_SPACE", find);
            check(!find.ints.empty(), "a free Goblimon RAM");
            const float a = -3.14159265f + (float(k) + 0.5f) * 3.14159265f / 4.0f;
            Packet generate;
            generate.vectors = {Vec3f{me.x + 25.0f * std::sin(a), me.y, me.z + 25.0f * std::cos(a)}, Vec3f{}};
            generate.strings.emplace_back(std::string("SQ_ENTR"));
            generate.ints = {row->record, 99999};
            engine.messenger().send(engine, "MODELCTRL", row->family, find.ints[0], "GENERATE", generate);
        }
    }
    for (int i = 0; i < 2; ++i) run_step(run, false);
    int points = 0, in_gap = 0;
    std::array<int, 8> by_sector{};
    for (int i = 0; i < 300; ++i) {
        Vec3f at;
        if (!director.sample_spawn_point(engine, 26.0f, 38.0f, false, at)) continue;
        ++points;
        const int s = sector_of(me, at);
        ++by_sector[std::size_t(s)];
        in_gap += s >= 6 ? 1 : 0;
    }
    std::cout << "  gap: six sectors closed, " << points << " spawn points, " << in_gap << " in the last way out (sectors 6-7)\n";
    check(points >= 250, "the spawner still finds points");
    check(in_gap == 0, "no spawn point closes the last way out");
    for (int k = 0; k < 6; ++k) check(by_sector[std::size_t(k)] > 0, "the closed sectors take the spawns");
}

// ---------------------------------------------------------------------------
// human (the balance round of 05.11): the human bot plays the same run for
// the same seed, walks, and flees from a near enemy late
// ---------------------------------------------------------------------------
void human(const std::filesystem::path& data) {
    std::vector<std::string> trails;
    for (int pass = 0; pass < 2; ++pass) {
        RunConfig config;
        config.seed = 7;
        config.bot = "human";
        config.offers = "newcomer";
        auto run = start_run(data, config, true);
        until_started(run);
        auto& engine = *run.engine;
        auto* models = engine.get<ModelCtrlManager>();
        std::string trail;
        float walked = 0.0f;
        Vec3f last = position(*models->player(0));
        int moving = 0;
        for (int i = 0; i < 2400; ++i) {
            std::array<PadSample, 4> pads{};
            pads[0] = run.director->human_pad(engine);
            moving += (std::fabs(pads[0].left.x) + std::fabs(pads[0].left.y)) > 0.05f ? 1 : 0;
            engine.step(pads);
            if (!engine.held()) engine.traverse_scene();
            const Vec3f at = position(*models->player(0));
            walked += flat(at, last);
            last = at;
            if (i % 60 == 59) {
                char text[64];
                std::snprintf(text, sizeof text, "%.2f,%.2f;", double(at.x), double(at.z));
                trail += text;
            }
        }
        std::cout << "  human: pass " << pass + 1 << ": walked " << walked << " units in 40 s, the stick moved in " << moving
                  << " of 2400 steps, level " << run.director->level(engine) << ", " << run.director->defeats() << " defeats\n";
        check(walked > 300.0f, "the human bot walks");
        check(moving > 1200 && moving < 2400, "it walks most of the time and stops at times");
        check(run.director->defeats() > 0, "its Art defeats enemies");
        trails.push_back(trail);
    }
    check(trails[0] == trails[1], "one seed, one run of the human bot");
}

// ---------------------------------------------------------------------------
// poison (4.8): the run's poison rule on the run's engine
// ---------------------------------------------------------------------------

void poison(const std::filesystem::path& data) {
    RunConfig config;
    config.seed = 4;
    config.offers = "priority";
    config.tuning.abilities = false;
    config.tuning.spawning = false;
    config.tuning.time_scaling = false;
    auto run = start_run(data, config, false);
    until_started(run);
    auto& engine = *run.engine;
    auto* sys = engine.get<GameSystem>();
    auto* models = engine.get<ModelCtrlManager>();
    // A tick never takes the last HP: 5 HP, a poison of 50 a tick, 8 ticks.
    auto& player = sys->players[0];
    auto* self = models->find(player.digimon, 0);
    check(self != nullptr, "the run's player");
    self->hp = player.hp = 5;
    {
        const Messenger::PortOnly scope(engine.messenger());
        Packet poison;
        poison.ints = {7 * 120, 50};
        engine.messenger().send(engine, "MODELCTRL", player.digimon, 0, "SETPOISON", poison);
    }
    for (int i = 0; i < 1200; ++i) run_step(run, false);
    std::cout << "  poison: 5 HP and 8 ticks of 50: " << self->hp << " HP left, outcome " << int(run.director->outcome()) << '\n';
    check(self->hp == 1 && player.hp == 1, "poison never takes the last HP in a run");
    check(run.director->outcome() == Outcome::none, "and never ends the run");
    // MRβ02's poison (eneatk 29, KOUKARANK 3, 2700 frames) through the hit
    // rule: level 25 at 10:00's damage factor, six of them round the player
    // (the player healed every second so it lives on).
    set_level(engine, 25);
    run.rules->damage.taken_factor = run.director->damage_factor(10.0);
    const auto* row = family_row("e_mecha2");
    check(row != nullptr, "MRβ02 in the table");
    // (Its poison is a shot from range: half of them near, half farther.)
    spawn_around(engine, *row, 5, 12.0f);
    spawn_around(engine, *row, 5, 28.0f);
    for (int i = 0; i < 3600; ++i) {
        if (i % 60 == 0) self->hp = player.hp = player.max_hp;
        run_step(run, false);
    }
    std::cout << "  poison: MRβ02 x10 for 60 s: " << run.director->poisonings() << " poisonings, the largest "
              << run.director->highest_poison() * 100.0 << " % of MaxHP\n";
    check(run.director->poisonings() > 0, "MRβ02 poisons");
    check(run.director->highest_poison() <= 0.1001, "one poisoning takes at most 10 % of MaxHP");
}

// ---------------------------------------------------------------------------
// balance (14.3)
// ---------------------------------------------------------------------------
// The expected levels at the phases' starts and at the run's end (14.3):
// the medians of the kite bot's T6 runs (the forge round of 05.11, Agumon
// and Veemon: 15-16 at 5:00, 23-25 at 10:00, 31 at 15:00, 35 at 20:00 and
// 38 at the run's end, 25:00; the balance round of 05.11 15 / 24 / 31 and 36
// at its end, 20:00; the review round's 15 / 25 / 32 / 40, 4.5's estimate 12
// / 22 / 32 / 40). A slow run meets a phase a few levels lower, where a
// Corrupted enemy's critical hit can pass 35 %: the run's clamp takes it.
constexpr int kLevelAt[5] = {1, 15, 24, 31, 38};
// No wall at a phase change (the review of O1a): see balance().
constexpr double kWallStep = 3.0, kStartShare = 0.16;

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
    for (const char* form : {"p_agumon", "p_vmon", "p_girumon", "p_dorumon"}) {
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
            // (A phase's highest A(t): its end's knot, at most the cap.)
            const Tuning tuning;
            int highest[5] = {0, 0, 0, 0, 0};
            for (int p = 1; p <= 4; ++p) highest[p] = std::min(tuning.alive_cap, int(OverflowDirector::knots(tuning.alive_at, 5.0 * p)));
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
            const double minutes = level == 1 ? 0.0 : 4.0; // (the warm-up HP: x 0.6 until 4:00, then rising to x 1 at 6:00)
            const int tech = db->number("shinka", stats.species, "TECH1").value_or(0);
            for (const auto& row : family_table()) {
                if (row.phase != 1) continue;
                const double hp = double(db->number("enemy", row.record, "HPMAX").value_or(1)) * run.director->hp_factor(minutes);
                const int mamori = db->number("enemy", row.record, "MAMORI").value_or(1);
                const double art = double(stats.tsuyosa * 6 + skill * 4) / double(mamori) * 2.5 * double(art_class_factor(form));
                const int spirit = db->number("enemy", row.record, "SEISHIN").value_or(1);
                static const char* resist[] = {"RES_FIRE", "RES_ICE", "RES_THUNDER", "RES_DARK"};
                const std::string f(form);
                const int element = f == "p_agumon" ? 0 : f == "p_vmon" ? 2 : f == "p_girumon" ? 3 : 1; // (fire, thunder, dark, ice)
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
        // with the time factor there, Corrupted x 1.5 on top. (A hit's share
        // includes the whole of the poison it left.) And no wall at a phase
        // change: a new phase's families hit at its start at most
        // kWallStep times what the phase before hit at its end, and no
        // walking family of phase 2-4 more than kStartShare at its start
        // (the review of O1a: phase 2 ended near 3 %, phase 3 began at
        // 16-21 %, and every run ended in its first two minutes).
        std::array<std::array<double, 2>, 5> phase_worst{};
        for (int phase = 1; phase <= 4; ++phase)
            for (int end = 0; end < 2; ++end) {
                const int level = kLevelAt[phase - 1 + end];
                // (Phase 4 ends with the run: 25:00 since the forge round.)
                const double minutes =
                    phase == 4 && end ? double(Tuning{}.end_steps) / 3600.0 - 0.01 : 5.0 * double(phase - 1 + end) - (end ? 0.01 : 0.0);
                set_level(engine, level);
                rules.damage.taken_factor = run.director->damage_factor(minutes);
                {
                    // (The near and far factors of that time: the larger of the two for
                    // every hit, the ranged families shoot from 12 units here.)
                    const Tuning tuning;
                    const float range = std::max(OverflowDirector::knots(tuning.contact_at, minutes), OverflowDirector::knots(tuning.shot_at, minutes));
                    rules.damage.near_factor = rules.damage.far_factor = range;
                }
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
                    if (!row.ranged) {
                        auto& worst = phase_worst[std::size_t(phase)][std::size_t(end)];
                        worst = std::max(worst, share);
                        if (phase >= 2 && end == 0 && share > kStartShare) {
                            failed = true;
                            if (first_failure.empty())
                                first_failure = std::string(form) + ": " + row.family + " hits for more than " + std::to_string(int(kStartShare * 100)) +
                                                " % at its phase's start";
                        }
                    }
                    // (Start over per family and level: the table keeps the worst.)
                    run.director->reset_hits_by_family();
                }
            }
        for (int phase = 2; phase <= 4; ++phase) {
            const double before = phase_worst[std::size_t(phase - 1)][1], after = phase_worst[std::size_t(phase)][0];
            std::printf("  wall %s phase %d: the phase before ends at %4.1f %%, it starts at %4.1f %% (x%.1f)\n", form, phase, before * 100.0,
                        after * 100.0, before > 0 ? after / before : 0.0);
            if (after > kWallStep * before) {
                failed = true;
                if (first_failure.empty()) first_failure = std::string(form) + ": a wall at the start of phase " + std::to_string(phase);
            }
        }
    }
    if (!report) check(!failed, first_failure);
}

// ---------------------------------------------------------------------------
// finale (round O1b lane E, SURVIVAL.md 4.4 and 6.5): the Guardians, the
// finale, the Awakening and the return gate on the run's engine
// ---------------------------------------------------------------------------
bool generated(Engine& engine, std::string_view name) {
    for (const auto& ram : engine.get<ModelCtrlManager>()->rams())
        if (ram->generated && ram->info_name == name) return true;
    return false;
}

bool has_event(const OverflowDirector& director, const std::string& text) { return director.log_text().find(text) != std::string::npos; }

float view_change(const Mat4& a, const Mat4& b) {
    float most = 0.0f;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) most = std::max(most, std::fabs(a(r, c) - b(r, c)));
    return most;
}

void finale(const std::filesystem::path& data) {
    // (1) A Guardian by guardian_first: the scripts of the separate packages
    // (Engine::route_donor's scripts_only), the boss bar, the camera shake at
    // its entry, A(t)'s half, its clamp (guardian_clamps, not 14.2's clamp),
    // its defeat and its drops.
    {
        RunConfig config;
        config.seed = 5;
        config.offers = "priority";
        config.guardians = {"e_blosso", "e_skulgrey", "e_tonogeko"};
        config.tuning.guardian_first = 90;
        config.tuning.guardian_half_steps = 600;
        config.tuning.spawning = false;
        auto run = start_run(data, config, true);
        until_started(run);
        auto& engine = *run.engine;
        auto& director = *run.director;
        auto& rules = *run.rules;
        check(engine.has_script("bck_e_skulgrey.lua"), "SkullGreymon's bck_ script comes from its stage's main member (scripts_only)");
        check(engine.has_script("hit_e_tonogeko.lua"), "TonosamaGekomon's hit script comes from its stage's main member (scripts_only)");
        auto* cameras = engine.get<CameraManager>();
        FrameCamera last;
        cameras->frame_camera(engine, last);
        float before = 0.0f, after = 0.0f;
        std::uint64_t entered = 0;
        for (int i = 0; i < 400 && !director.guardian_bar(engine); ++i) {
            run_step(run, false);
            FrameCamera now;
            cameras->frame_camera(engine, now);
            if (director.run_steps() >= 40) before = std::max(before, view_change(now.view, last.view));
            last = now;
        }
        const auto bar = director.guardian_bar(engine);
        check(bar.has_value(), "the first Guardian comes at guardian_first");
        entered = director.run_steps();
        check(entered >= 90 && entered <= 100, "... at its run step (" + std::to_string(entered) + ")");
        for (int i = 0; i < 40; ++i) {
            run_step(run, false);
            FrameCamera now;
            cameras->frame_camera(engine, now);
            after = std::max(after, view_change(now.view, last.view));
            last = now;
        }
        std::cout << "  finale: " << bar->name << " " << bar->hp << " / " << bar->max_hp << " HP at run step " << entered
                  << ", the view's largest step " << before << " before its entry, " << after << " after it\n";
        check(bar->name == "Blossomon" && bar->hp == bar->max_hp && bar->max_hp >= 250 * 6, "the bar shows Blossomon at full HP (HPMAX x 6)");
        check(generated(engine, "i_bosshp02"), "the arena's boss bar i_bosshp02 is up");
        check(after > 0.05f && after > 4.0f * before, "the camera shakes at the Guardian's entry");
        int guardian_ram = -1;
        for (const auto& ram : engine.get<ModelCtrlManager>()->rams())
            if (ram->generated && ram->info_name == "e_blosso") guardian_ram = ram->index;
        check(guardian_ram >= 0, "Blossomon's RAM");
        check(rules.enemy_class("e_blosso", guardian_ram) == EnemyClass::guardian, "its hits take the Guardian rules");
        const auto& t = director.config().tuning;
        const int full = std::min(t.alive_cap, int(OverflowDirector::knots(t.alive_at, director.run_seconds() / 60.0)));
        check(director.alive_target() == full / 2, "A(t) halves while a Guardian lives (" + std::to_string(director.alive_target()) + " of " + std::to_string(full) + ")");
        // Its clamp: a hit of three MaxHP takes guardian_clamp (30 %), counted apart.
        auto* sys = engine.get<GameSystem>();
        const int max_hp = sys->players[0].max_hp;
        ModelScriptContext hit;
        hit.name = sys->players[0].digimon;
        hit.index = 0;
        hit.shooter_name = "e_blosso";
        hit.shooter = guardian_ram;
        const int clamps = rules.tally.clamps, guardian_clamps = rules.tally.guardian_clamps;
        const int taken = rules.taken(engine, hit, 0, max_hp, max_hp * 3);
        check(taken == int(std::lround(double(max_hp) * double(t.guardian_clamp))), "a Guardian's hit takes at most 30 % of MaxHP (" + std::to_string(taken) + ")");
        check(rules.last_taken.guardian_clamped && !rules.last_taken.clamped, "... as the Guardian clamp, not 14.2's");
        check(rules.tally.guardian_clamps == guardian_clamps + 1 && rules.tally.clamps == clamps, "guardian_clamps counts it, clamps does not");
        check(director.summary_text(engine).find("\nguardian_clamps " + std::to_string(rules.tally.guardian_clamps) + "\n") != std::string::npos,
              "the summary's guardian_clamps line");
        // A(t) is whole again after guardian_half_steps.
        while (director.run_steps() < entered + 610) run_step(run, false);
        check(director.guardian_bar(engine).has_value(), "Blossomon still stands (no one-hit yet)");
        check(director.alive_target() == std::min(t.alive_cap, int(OverflowDirector::knots(t.alive_at, director.run_seconds() / 60.0))),
              "A(t) is whole again after guardian_half_steps");
        // Its defeat (the one-hit cheat: the Art takes all its HP) and its drops.
        engine.cheats().one_hit = true;
        bool xdata = false, disk = false;
        for (int i = 0; i < 3600 && !(xdata && disk); ++i) {
            run_step(run, false);
            if (!director.guardians_defeated().empty()) {
                xdata = xdata || generated(engine, "g_000xdata1");
                disk = disk || generated(engine, "g_000item");
            }
        }
        check(director.guardians_defeated().size() == 1 && director.guardians_defeated()[0] == "e_blosso", "the Art defeats Blossomon");
        check(!director.guardian_bar(engine), "its bar is gone");
        check(xdata && disk, "it drops the ¶ Data UP 100 and the Recovery disk");
        check(has_event(director, "guardian e_blosso defeated"), "the run log has its defeat");
        std::cout << "  finale: Blossomon defeated at run step " << director.run_steps() << ", both drops\n";
    }
    // (2) The finale under the quest timer and TIME UP (short phases: 5 s).
    {
        RunConfig config;
        config.seed = 6;
        config.offers = "priority";
        config.guardians = {"e_mammos", "", ""};
        config.tuning.phase_steps = 300;
        config.tuning.end_steps = 1500;
        config.tuning.spawning = false;
        config.tuning.abilities = false;
        auto run = start_run(data, config, true);
        until_started(run);
        auto& engine = *run.engine;
        auto& director = *run.director;
        while (director.run_steps() < 1210) run_step(run, false);
        const auto bar = director.guardian_bar(engine);
        check(director.finale() && bar && bar->name == "LordKnightmon", "LordKnightmon comes at 4 phases (the finale)");
        check(has_event(director, "guardian e_mammos left"), "the phase Guardian still alive leaves for it");
        check(director.finale_left_steps() == int(1500 - director.run_steps()), "the quest timer counts to the run's end");
        while (director.outcome() == Outcome::none && director.run_steps() < 1600) run_step(run, false);
        std::cout << "  finale: outcome " << int(director.outcome()) << " at run step " << director.run_steps() << ", notice '" << director.notice() << "'\n";
        check(director.outcome() == Outcome::time_up && director.time_up(), "the timer's 0:00 is TIME UP (Outcome::time_up)");
        check(director.notice() == "TIME UP", "TIME UP shows");
        check(director.guardians_defeated().empty(), "no Guardian fell");
        for (int i = 0; i < 300 && director.screen() != Screen::results; ++i) run_step(run, false);
        check(director.screen() == Screen::results && !director.return_gate_open(), "the results come without a return gate");
    }
    // (3) The Awakening by a set skill, its burst, the Drain Ram's hook in
    // BLTgetDamageValue, the finale's clear and the Violet Ring's return gate.
    {
        RunConfig config;
        config.seed = 7;
        config.bot = "kite";
        config.offers = "priority";
        config.forge_start = 5;
        config.loadout_chip = "drain_ram";
        config.gear_open = {"drain_ram"};
        config.guardians = {"e_blosso", "e_skulgrey", "e_tonogeko"};
        config.tuning.phase_steps = 600;
        config.tuning.end_steps = 6000;
        auto run = start_run(data, config, true);
        until_started(run);
        auto& engine = *run.engine;
        auto& director = *run.director;
        auto& rules = *run.rules;
        auto* sys = engine.get<GameSystem>();
        auto* db = engine.get<DatabaseManager>();
        auto& player = sys->players[0];
        check(player.stats.hp_drain > 0, "the Drain Ram in the weapon pocket gives HPDRAIN");
        const int skill = db->number("weapon", 0, "SKILL").value_or(0);
        const int panels = rules.tally.panels + rules.tally.panels_skipped;
        player.stats.skill[std::size_t(skill)] = 2500;
        director.skill_changed(engine, 0, skill, 1, 2500);
        const auto* weapon = player.device(player.power_socket[0]);
        check(director.awakened() && weapon && weapon->record >= 250 && weapon->record < 266, "the Awakening puts the signature in the socket");
        check(director.notice().rfind("AWAKENING: ", 0) == 0, "the AWAKENING line (" + director.notice() + ")");
        check(rules.tally.panels + rules.tally.panels_skipped == panels + 1, "the Awakening's SQ_TECUP panel (6.5)");
        engine.cheats().one_hit = true;
        bool charge = false;
        for (int i = 0; i < 9000 && director.outcome() == Outcome::none; ++i) {
            run_step(run, true);
            charge = charge || generated(engine, "x_100charge01");
        }
        std::cout << "  finale: " << director.awakening_bursts() << " bursts, " << director.defeats() << " defeats, drain " << rules.tally.drain_given
                  << " given / " << rules.tally.drain_capped << " capped, outcome " << int(director.outcome()) << " at run step " << director.run_steps() << '\n';
        check(director.awakening_bursts() > 0 && charge, "the awakened Art's hits burst (KATCX2: x_100charge01)");
        check(rules.tally.drain_given > 0, "the Drain Ram's HPDRAIN goes through the run's cap (lua_host BLTgetDamageValue)");
        check(director.outcome() == Outcome::cleared, "LordKnightmon falls (the one-hit cheat): the run is cleared");
        // The return gate: no results after the old 4 s; the bot walks in.
        const std::uint64_t cleared = director.steps();
        for (int i = 0; i < 160 && director.screen() != Screen::results; ++i) run_step(run, true);
        check(generated(engine, "g_dreturngate") && has_event(director, "gate open"), "the return gate g_dreturngate comes");
        for (int i = 0; i < 3600 && director.screen() != Screen::results; ++i) run_step(run, true);
        std::cout << "  finale: results at director step " << director.steps() << ", gate entered " << director.gate_entered() << '\n';
        check(director.screen() == Screen::results && director.gate_entered(), "walking into the gate ends the run");
        check(director.steps() > cleared + 150, "the results waited for the gate (" + std::to_string(director.steps() - cleared) + " steps after the clear)");
        check(has_event(director, "gate entered"), "the run log has the gate");
        check(director.summary_text(engine).find("\ngate 1 entered\n") != std::string::npos, "the summary's gate line");
    }
    // (4) The force buffs last 6-10 s by rank in a run (6.4): Agumon's Pwr Up
    // (TECH2, run level 8) with enemies near; the original's TIME is 30-54 s.
    {
        RunConfig config;
        config.seed = 8;
        config.offers = "priority";
        config.tuning.spawning = false;
        config.tuning.guardians = false;
        auto run = start_run(data, config, true);
        until_started(run);
        auto& engine = *run.engine;
        auto& rules = *run.rules;
        check(rules.buff_steps == 0 && rules.buff_time(80, 0, 1920) == 1920, "the run's force buffs keep DW4's time by default");
        rules.buff_steps = 360; // (the 6-10 s of SURVIVAL.md 6.4, a balance pass's key buff=360,60)
        check(rules.buff_time(80, 0, 1800 + 120) == 360 && rules.buff_time(80, 4, 3240) == 600 && rules.buff_time(75, 2, 300) == 300,
              "buff=360,60: 6 s at Petty, 10 s at ∞, never longer than DW4's");
        rules.buff_kept = {75};
        check(rules.buff_time(75, 0, 1800) == 1800, "buff_kept keeps a technique's DW4 time");
        rules.buff_kept.clear();
        set_level(engine, 10);
        spawn_around(engine, *family_row("e_goburi"), 3, 18.0f);
        const auto* self = engine.get<ModelCtrlManager>()->player(0);
        int longest = 0;
        for (int i = 0; i < 900; ++i) {
            run_step(run, false);
            if (self && self->status) longest = std::max(longest, self->status->timer[6]);
        }
        std::cout << "  finale: Pwr Up's POWERUP timer at most " << longest << " frames\n";
        check(longest > 0 && longest <= 360, "buff=360,60: Agumon's Petty Pwr Up lasts at most 6 s in a run (" + std::to_string(longest) + " frames)");
    }
}

} // namespace

// Round O1b lane E: the gear in a run (SURVIVAL.md 7): the loadout lands in
// its slots and raises the stats; the window offers gear; a core steps by
// its offers and opens the core pocket at beta.
void gear(const std::filesystem::path& data) {
    RunConfig plain;
    plain.form = "p_girumon";
    plain.seed = 3;
    plain.tuning.spawning = false;
    plain.tuning.abilities = false;
    auto base = start_run(data, plain, true);
    until_started(base);
    const auto before = base.engine->get<GameSystem>()->players[0].stats;
    RunConfig config = plain;
    config.loadout_armor = "brave_core";
    config.loadout_chip = "wisdom_chip";
    auto run = start_run(data, config, true);
    until_started(run);
    auto& engine = *run.engine;
    auto* sys = engine.get<GameSystem>();
    auto& player = sys->players[0];
    const auto* core = player.device(player.main_slot[0]);
    const auto* chip = player.device(player.power_pocket[0]);
    check(core && core->type == 1 && core->record == 0, "the loadout's Brave Core alpha in MAINSLOT");
    check(chip && chip->type == 3 && chip->record == 0, "the loadout's Wisdom Chip alpha in the weapon pocket");
    check(player.stats.mamori > before.mamori, "the core raises DEF (" + std::to_string(before.mamori) + " > " + std::to_string(player.stats.mamori) + ")");
    check(player.stats.kashikosa > before.kashikosa, "the chip raises INT");
    check(run.director->build_lines(engine).size() >= 4, "the build page lists the gear");
    // The run log's header carries the loadout, the meta state and the Guardian keys (SURVIVAL.md 13).
    const std::string log = run.director->log_text();
    check(log.find("\nloadout brave_core wisdom_chip\n") != std::string::npos, "the run log's header names the loadout");
    check(log.find("\nmeta plug_lab 0 0 0 0 0 0 0 0 0 0 0 gear_open -\n") != std::string::npos, "the run log's header names the meta state");
    check(log.find("\nguardians 1 guardian_set - - - elite - guardian_hp 6 finale_hp 14\n") != std::string::npos, "the run log's header names the Guardian keys");
    check(log.find("loadout none") == std::string::npos, "no fixed 'loadout none' in the header");
    std::cout << "  gear: DEF " << before.mamori << " -> " << player.stats.mamori << ", INT " << before.kashikosa << " -> " << player.stats.kashikosa << '\n';
    // Gear names in the offers: the core's next grade waits for run level 22.
    const auto beta = Offer{OfferKind::core, gear_index("brave_core")};
    check(run.director->offer_text(engine, beta) == "Brave Core β", "the core's next grade by its own name: " + run.director->offer_text(engine, beta));
    check(run.director->offer_text(engine, Offer{OfferKind::board, gear_index("armor_board")}) == "Armor Board α", "a board by its own name");
    // Every SQ_TECUP of the run (skill ups, train offers, the chips' ¶ Data
    // power-ups) counts in the rules' panel gap: a second panel in the same
    // frame is left out.
    auto& rules = *run.rules;
    rules.last_panel_frame = -1;
    const int shown = rules.tally.panels, skipped = rules.tally.panels_skipped;
    const bool first = rules.show_panel(engine, 0, player.digimon);
    const bool second = rules.show_panel(engine, 0, player.digimon);
    check(first && !second, "a second SQ_TECUP inside the panel gap is left out");
    check(rules.tally.panels == shown + 1 && rules.tally.panels_skipped == skipped + 1, "the panel gap's tally counts both");
    check(rules.last_panel_frame == std::int64_t(engine.frame()), "the panel gap starts at the shown panel");
    // The Drain Ram's run cap (4.8): HPDRAIN gives at most 2 % of MaxHP a second.
    rules.drain_second = {-1, -1, -1, -1};
    const int drain_cap = std::max(1, int(std::lround(double(player.max_hp) * double(rules.damage.drain_cap))));
    check(rules.damage.drain_cap > 0.019f && rules.damage.drain_cap < 0.021f, "the Drain Ram's cap is 2 % of MaxHP a second");
    const int small = rules.drain(engine, 0, 1);
    const int big = rules.drain(engine, 0, drain_cap * 3);
    const int after_cap = rules.drain(engine, 0, 5);
    check(small == 1 && big == drain_cap - 1 && after_cap == 0,
          "HPDRAIN stops at the second's cap (" + std::to_string(small) + ", " + std::to_string(big) + ", " + std::to_string(after_cap) + " of " + std::to_string(drain_cap) + ")");
    rules.drain_second[0] = -1; // (a new second)
    check(rules.drain(engine, 0, drain_cap) == drain_cap, "a new second gives the cap again");
    std::cout << "  gear: Drain Ram cap " << drain_cap << " HP a second (MaxHP " << player.max_hp << ")\n";
}

// Round O1b lane E: the Guardians' plan (4.4), the gear table (7, the keys of
// the lane contract) and the gear offers (4.6).
void guardians() {
    for (const auto form : kRookies) check(playable_form(form), "a Rookie plays: " + std::string(form));
    check(!playable_form("p_wargrey"), "a Mega is no O1b start");
    std::map<std::string, int> seen;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        const auto plan = draw_guardians(seed);
        check(plan == draw_guardians(seed), "one seed, one plan");
        for (int phase = 1; phase <= 3; ++phase) {
            const auto* row = guardian_row(plan[std::size_t(phase - 1)]);
            check(row && row->phase == phase, "phase " + std::to_string(phase) + "'s Guardian is of its pair");
            ++seen[plan[std::size_t(phase - 1)]];
        }
        check(plan[3] == "e_ldknight", "the finale is LordKnightmon");
    }
    for (const auto& row : guardian_table()) {
        check(!family_row(row.family), std::string("a Guardian is no regular family: ") + row.family);
        check(family_row(row.elite) != nullptr, std::string("its elite is a regular family: ") + row.elite);
        check(row.damage > 0.0f && row.damage <= 1.0f, std::string("its damage factor: ") + row.family);
        if (row.phase <= 3) check(seen[row.family] >= 60, std::string("both of a pair come (200 seeds): ") + row.family);
    }
    const auto forced = draw_guardians(7, {"e_mammos", "", "e_tonogeko"});
    check(forced[0] == "e_mammos" && forced[2] == "e_tonogeko" && guardian_row(forced[1])->phase == 2, "forced Guardians (the spike's key)");
    // The gear keys of the lane contract (docs/ROUND_O1B_E_NOTES.md).
    for (const char* key : {"brave_core", "armor_board", "fire_board", "ice_board", "bolt_board", "dark_board", "wisdom_chip", "spirit_chip",
                            "speed_chip", "pwr_medal", "drain_ram"})
        check(gear_index(key) >= 0, std::string("gear key ") + key);
    check(gear_index("nothing") < 0, "an unknown gear key");
    check(!gear_table()[std::size_t(gear_index("drain_ram"))].start, "the Drain Ram waits for its record (Blossom Data)");
    // Gear lines join the offers by their weight; a window is never empty.
    OfferState state;
    state.groups = {true, true, true};
    state.gear = {{OfferKind::core, gear_index("brave_core")}, {OfferKind::board, gear_index("armor_board")}, {OfferKind::chip, gear_index("wisdom_chip")}};
    const auto eligible = eligible_offers(state);
    check(std::count_if(eligible.begin(), eligible.end(), [](const Offer& o) { return o.kind == OfferKind::core || o.kind == OfferKind::board || o.kind == OfferKind::chip; }) == 3,
          "the gear offers are eligible");
    std::uint64_t random = 99;
    int gear_lines = 0, windows = 0;
    for (int n = 0; n < 2000; ++n) {
        state.drawn = 10;
        const auto drawn = draw_offers(state, random);
        check(drawn.size() == 3, "three lines");
        ++windows;
        for (const auto& o : drawn) gear_lines += o.kind == OfferKind::core || o.kind == OfferKind::board || o.kind == OfferKind::chip ? 1 : 0;
    }
    std::cout << "  guardians: gear in " << gear_lines << " lines of " << windows << " windows\n";
    check(gear_lines > windows / 4 && gear_lines < windows * 2, "gear takes about its 30 % weight");
    check(offer_priority({OfferKind::core, 0}) < offer_priority({OfferKind::plug, 2}), "the priority bot takes gear before plugs");
    // defeated_by (the O1b integration: lane D's "Welcome back" joke): the
    // last damaging hit's family, only once the run ended in a defeat.
    {
        Engine run{dw4::MountedResources{}};
        OverflowDirector director(RunConfig{});
        HitTaken hit;
        hit.shooter = "e_mecha1";
        hit.damage = 10;
        hit.max_hp = 100;
        director.player_hit(run, hit);
        hit.shooter = "e_geko";
        director.player_hit(run, hit);
        HitTaken missed;
        missed.shooter = "e_torta";
        missed.missed = true;
        director.player_hit(run, missed);
        check(director.defeated_by().empty(), "defeated_by is empty while the run goes on");
        director.run_end(run, dw4::engine::overflow::RunEnd{EndReason::player_defeated, {}});
        check(director.outcome() == Outcome::defeated && director.defeated_by() == "e_geko", "defeated_by names the last damaging hit's family");
        OverflowDirector retired(RunConfig{});
        retired.player_hit(run, hit);
        retired.run_end(run, dw4::engine::overflow::RunEnd{EndReason::stage_exit, "stagml11"});
        check(retired.outcome() == Outcome::retired && retired.defeated_by().empty(), "no defeated_by for a run that did not end in a defeat");
    }
}

int main(int argc, char** argv) {
    const std::string part = argc > 1 ? argv[1] : "offers";
    std::string data = argc > 2 ? argv[2] : "";
    if (data.empty())
        if (const char* env = std::getenv("DW4_TEST_DATA")) data = env;
    try {
        if (part == "offers") offers();
        else if (part == "guardians") guardians();
        else if (part == "rng") rng();
        else if (part == "spawn" || part == "gap" || part == "human" || part == "poison" || part == "balance" || part == "report" ||
                 part == "gear" || part == "finale") {
            if (data.empty() || !std::filesystem::exists(data)) {
                std::cout << "overflow " << part << ": skipped (no game data: DW4_TEST_DATA)\n";
                return 0;
            }
            if (part == "spawn") spawn(data);
            else if (part == "gap") gap(data);
            else if (part == "human") human(data);
            else if (part == "poison") poison(data);
            else if (part == "gear") gear(data);
            else if (part == "finale") finale(data);
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
