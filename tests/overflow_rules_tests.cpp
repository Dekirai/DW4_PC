// Data Overflow (docs/SURVIVAL.md 13.4): the run engine's rules without game
// data. The curve, the technique lock and the level-up rule; the damage
// rules (Art factor, tags, techniques, elements, the 35 % clamp, Corrupted,
// Guardians); the poison rule (its ticks through the damage-taken rule, at
// most 10 % of MaxHP a poisoning); the exits (H3: the first request, the director's field change,
// every other change refused and reported once; no soft or console reset);
// the native game over (H10); the rule set "original" leaving every hook
// but the player hit script alone; an engine without rules unchanged.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/overflow.hpp"

#include <iostream>
#include <memory>
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

struct Recorder : Director {
    std::vector<RunEnd> ends;
    int steps{};
    void step(Engine&) override { ++steps; }
    void run_end(Engine&, const RunEnd& end) override { ends.push_back(end); }
};

void curve() {
    // 4.5: 80 EXP for level 2, the step from 10 to 11 about 1,000.
    check(run_next_exp(1, 80, 50, 6) == 80, "NEXTEXP(1) = 80");
    check(run_next_exp(2, 80, 50, 6) == 80 + 136, "NEXTEXP(2) = 80 + (80 + 50 + 6)");
    check(run_next_exp(10, 80, 50, 6) - run_next_exp(9, 80, 50, 6) == 80 + 450 + 486, "the step to level 11 is 1,016");
    check(run_next_exp(10, 80, 50, 6) == 4760, "NEXTEXP(10) = 4,760");
    int last = 0;
    for (int level = 1; level < 999; ++level) {
        const int value = run_next_exp(level, 80, 50, 6);
        check(value > last, "the curve rises");
        last = value;
    }
    check(run_next_exp(0, 80, 50, 6) == 0, "no EXP below level 1");
    OverflowRules rules;
    check(rules.next_exp(1) == 80, "the rules use the run's curve");
    rules.levels.run_curve = false;
    check(rules.next_exp(1) == 341 && rules.next_exp(2) == 2 * 2 * 142 + 400, "and DW4's when it is off");
    rules.levels.exp_factor = 1.5f;
    check(rules.exp_gain(10) == 15 && rules.exp_gain(0) == 0 && rules.exp_gain(1) == 1, "the tier's EXP factor");
}

void technique_lock() {
    // 6.4: DW4's 1 / 12 / 22 / 23 / 33 / 34 / 44 become 1 / 8 / 15 / 16 / 22 / 23 / 30.
    const int dw4[] = {1, 12, 22, 23, 33, 34, 44};
    const int run[] = {1, 8, 15, 16, 22, 23, 30};
    for (int i = 0; i < 7; ++i) check(run_technique_level(dw4[i], 2, 3) == run[i], "TECHn_LEVEL x 2/3 rounded up");
    OverflowRules rules;
    check(rules.technique_level(12) == 8 && rules.technique_level(0) == 0, "the rules' lock");
}

void level_rule() {
    // 4.5: the fractions stay, +3 % of the new MaxHP, a full bar stays full.
    check(run_level_hp(180, 180, 200, 0.03) == 200, "a full bar stays full");
    check(run_level_hp(90, 180, 200, 0.03) == 100 + 6, "half stays half, plus 3 %");
    check(run_level_hp(1, 180, 200, 0.03) == 1 + 6, "a hurt Digimon stays hurt");
    check(run_level_hp(0, 180, 200, 0.03) == 6, "(the heal also from 0)");
    check(run_level_hp(1, 1000, 100, 0.0) == 1, "never below 1 while alive");
    check(run_level_hp(50, 60, 80, 0.0) == 66, "MP keeps its fraction without the heal");
}

void damage_rules() {
    OverflowRules rules;
    Engine engine{dw4::MountedResources{}};
    ModelScriptContext art;
    art.bullet_kind = 10000;
    art.port_tag = 7;
    check(rules.always_hits(art), "an Art always hits");
    check(rules.dealt(engine, art, 10, -1) == 25, "the Art factor 2.5");
    rules.damage.tag_factor[7] = 1.2f;
    check(rules.dealt(engine, art, 10, -1) == 30, "a tag's factor on top");
    check(rules.dealt(engine, art, 10, 0) == 30, "no element factor by default");
    rules.damage.element_factor[0] = 1.15f;
    check(rules.dealt(engine, art, 100, 0) == 345, "the form's element");
    ModelScriptContext technique;
    technique.bullet_kind = 40005; // a technique of family 5
    check(!rules.always_hits(technique), "a technique is not an Art (it hits anyway)");
    check(rules.dealt(engine, technique, 40, -1) == 40, "a technique without a factor");
    rules.damage.technique_factor[5] = 0.5f;
    check(rules.dealt(engine, technique, 40, -1) == 20, "a technique family's factor");
    check(rules.dealt(engine, technique, 1, -1) == 1, "never below 1");
    check(rules.tally.dealt_by[7] == 25 + 30 + 30 + 345 && rules.tally.dealt_by[-40005] == 40 + 20 + 1, "the damage by source");

    ModelScriptContext enemy;
    enemy.bullet_kind = 70001;
    enemy.shooter_name = "e_goburi";
    enemy.shooter = 3;
    check(rules.taken(engine, enemy, 0, 180, 30) == 30 && !rules.last_taken.clamped, "a normal hit below the clamp");
    check(rules.taken(engine, enemy, 0, 180, 100) == 63 && rules.last_taken.clamped, "the 35 % clamp of a non-Guardian");
    check(rules.tally.clamps == 1, "the clamp is counted");
    rules.set_enemy_class("e_goburi", 3, EnemyClass::corrupted);
    check(rules.enemy_class("e_goburi", 3) == EnemyClass::corrupted && rules.enemy_class("e_goburi", 4) == EnemyClass::normal,
          "the enemy classes by RAM");
    check(rules.taken(engine, enemy, 0, 180, 20) == 30, "Corrupted x 1.5");
    rules.damage.taken_factor = 1.2f;
    check(rules.taken(engine, enemy, 0, 1000, 100) == 180, "the time and tier factor");
    rules.set_enemy_class("e_goburi", 3, EnemyClass::guardian);
    check(rules.taken(engine, enemy, 0, 180, 300) == 360 && !rules.last_taken.clamped, "a Guardian is never clamped");
    rules.set_enemy_class("e_goburi", 3, EnemyClass::normal);
    check(rules.enemy_class("e_goburi", 3) == EnemyClass::normal, "back to normal");
    check(rules.tally.highest_hit_share > 0.34 && rules.tally.highest_hit_share < 0.36, "the highest non-Guardian hit share");
}

// 4.8, the review of O1a: poison went around the run's damage rule (MRβ02's
// rank 3 poison, 23 ticks of 3 % of MaxHP: 69 % from a hit of 1).
void poison_rule() {
    // DW4's: MaxHP x KOUKARANK / 100 a tick, KOUKATIME 2700 / 120 + 1 ticks.
    check(run_poison(30, 23, 1000, 1.0, 0, 1.0).tick == 30 && run_poison(30, 23, 1000, 1.0, 0, 1.0).ticks == 23, "no limit: DW4's poison");
    const auto rank3 = run_poison(30, 23, 1000, 1.0, 5, 0.10);
    check(rank3.tick == 30 && rank3.ticks == 3, "rank 3: 3 % a tick, the 10 % total cuts the ticks (" + std::to_string(rank3.ticks) + ")");
    const auto rank8 = run_poison(80, 23, 1000, 1.0, 5, 0.10);
    check(rank8.tick == 80 && rank8.ticks == 1, "rank 8: one tick of 8 %");
    const auto scaled = run_poison(30, 23, 1000, 0.4, 5, 0.10);
    check(scaled.tick == 12 && scaled.ticks == 5, "the damage-taken factor scales the tick; at most five ticks");
    check(run_poison(30, 23, 1000, 0.01, 5, 0.10).tick == 1, "never below 1 a tick");
    const auto big = run_poison(30, 23, 100, 9.0, 5, 0.10);
    check(big.tick == 10 && big.ticks == 1, "a tick is at most the total");
    check(run_poison(0, 23, 1000, 1.0, 5, 0.10).ticks == 0 && run_poison(30, 0, 1000, 1.0, 5, 0.10).tick == 0, "no poison");
    for (int rank = 1; rank <= 8; ++rank)
        for (const double factor : {0.17, 0.4, 1.0, 1.5, 3.1 * 1.5 * 1.3})
            for (const int max_hp : {150, 1000, 4000}) {
                const auto p = run_poison(max_hp * rank / 100, 23, max_hp, factor, 5, 0.10);
                check(p.tick * p.ticks <= std::max(1, max_hp / 10), "one poisoning takes at most 10 % of MaxHP");
            }
    // Through the rules: the shooter's factors, Guardians unchanged.
    OverflowRules rules;
    ModelScriptContext enemy;
    enemy.shooter_name = "e_mecha2";
    enemy.shooter = 1;
    rules.damage.taken_factor = 2.0f;
    rules.damage.family_factor["e_mecha2"] = 0.4f;
    const auto normal = rules.poison(enemy, 1000, 30, 23);
    check(normal.tick == 24 && normal.ticks == 4, "the run's factors: 30 x 2 x 0.4 = 24, four ticks within 10 %");
    rules.set_enemy_class("e_mecha2", 1, EnemyClass::corrupted);
    check(rules.poison(enemy, 1000, 30, 23).tick == 36, "Corrupted x 1.5");
    rules.set_enemy_class("e_mecha2", 1, EnemyClass::guardian);
    check(rules.poison(enemy, 1000, 30, 23).tick == 30 && rules.poison(enemy, 1000, 30, 23).ticks == 23, "a Guardian's poison is DW4's");
    check(rules.player_hit.poison_keeps_one, "poison never takes the last HP in a run");
}

void exits() {
    Recorder director;
    auto rules = std::make_shared<OverflowRules>();
    rules->director = &director;
    Engine engine{dw4::MountedResources{}};
    check(engine.overflow() == nullptr && !engine.overflow_run(), "no rules on a new engine");
    engine.set_overflow_rules(rules);
    check(engine.overflow() == rules.get() && engine.overflow_run(), "the run's rules");
    check(!engine.console_reset().enabled(), "no console reset in a run");
    engine.set_reset_enabled(true);
    check(!engine.console_reset().enabled(), "and the stage cannot switch it on");
    engine.set_soft_reset_allowed(true);
    check(!engine.soft_reset_allowed(), "no soft reset in a run");
    engine.request_stage("stagBS02");
    check(engine.pending_stage() && *engine.pending_stage() == "stagbs02", "the host's first request passes");
    check(director.ends.empty(), "no end for it");
    engine.request_stage("stagML11");
    check(engine.pending_stage() && *engine.pending_stage() == "stagbs02", "a later stage change is refused");
    check(director.ends.size() == 1 && director.ends[0].reason == EndReason::stage_exit && director.ends[0].target == "stagml11",
          "the director gets the end once with its target");
    engine.request_stage("stagIS10");
    check(director.ends.size() == 1, "later ends are dropped");
    rules->reset_end();
    rules->field_change(engine, "stagBS04");
    check(engine.pending_stage() && *engine.pending_stage() == "stagbs04", "the director's own field change passes");
    check(director.ends.size() == 1, "(no new end for it)");
    {
        // H9 on an engine without a stage on its way.
        Recorder stepper;
        auto own = std::make_shared<OverflowRules>();
        own->director = &stepper;
        Engine idle{dw4::MountedResources{}};
        idle.set_overflow_rules(own);
        idle.step({});
        idle.step({});
        check(stepper.steps == 2 && own->tally.steps == 2, "the director steps with the engine (H9)");
    }
    // H10: the original's game over ends the run instead.
    rules->reset_end();
    check(engine.run_script("end_g_000bcaps.lua"), "end_g_000bcaps.lua is answered natively");
    check(director.ends.size() == 2 && director.ends[1].reason == EndReason::game_over, "as the run's game over");
    check(!rules->script(engine, "first.lua"), "first.lua is native only on an arena with a native start");
}

void original_rule_set() {
    // RuleSet::original (T10): only the player hit script changes hands.
    auto rules = std::make_shared<OverflowRules>(RuleSet::original);
    Engine engine{dw4::MountedResources{}};
    engine.set_overflow_rules(rules);
    check(engine.overflow() && !engine.overflow_run(), "the original rule set changes no game rule");
    engine.set_soft_reset_allowed(true);
    check(engine.soft_reset_allowed(), "the soft reset stays");
    engine.request_stage("stagBS02");
    engine.request_stage("stagML11");
    check(engine.pending_stage() && *engine.pending_stage() == "stagml11", "every stage change stays");
    check(!rules->script(engine, "end_g_000bcaps.lua") && !rules->script(engine, "first.lua"), "only hit_p_<form>.lua is native");
    check(!rules->ended(), "no run end");
}

void without_rules() {
    Engine engine{dw4::MountedResources{}};
    engine.set_soft_reset_allowed(true);
    check(engine.soft_reset_allowed(), "an engine without rules keeps its soft reset");
    engine.request_stage("stagML11");
    check(engine.pending_stage() && *engine.pending_stage() == "stagml11", "and its stage changes");
    check(!engine.overflow_run() && !engine.overflow(), "and has no rules");
    check(has_native_field_start("stagbs02") && !has_native_field_start("stagml11"), "the Violet Ring has a native start");
}

} // namespace

int main() {
    try {
        curve();
        technique_lock();
        level_rule();
        damage_rules();
        poison_rule();
        exits();
        original_rule_set();
        without_rules();
    } catch (const std::exception& error) {
        std::cerr << "overflow rules test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overflow rules: " << checks << " checks passed\n";
    return 0;
}
