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
// The expected levels at the phases' starts (14.3): the medians of the kite
// bot's T6 runs (Agumon and Veemon, 20 seeds each: 14-17 at 5:00, 23-27 at
// 10:00, 32-33 at 15:00; 4.5's estimate was 12 / 22 / 32 / 40). A slow run
// meets phase 3 two or three levels lower, where a Corrupted enemy's
// critical hit can pass 35 %: the run's clamp takes it (it never did in T6).
constexpr int kLevelAt[5] = {1, 15, 25, 32, 40};
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

} // namespace

int main(int argc, char** argv) {
    const std::string part = argc > 1 ? argv[1] : "offers";
    std::string data = argc > 2 ? argv[2] : "";
    if (data.empty())
        if (const char* env = std::getenv("DW4_TEST_DATA")) data = env;
    try {
        if (part == "offers") offers();
        else if (part == "rng") rng();
        else if (part == "spawn" || part == "gap" || part == "human" || part == "poison" || part == "balance" || part == "report") {
            if (data.empty() || !std::filesystem::exists(data)) {
                std::cout << "overflow " << part << ": skipped (no game data: DW4_TEST_DATA)\n";
                return 0;
            }
            if (part == "spawn") spawn(data);
            else if (part == "gap") gap(data);
            else if (part == "human") human(data);
            else if (part == "poison") poison(data);
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
