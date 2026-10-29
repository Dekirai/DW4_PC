// The randomizer's seeds, enemy pools, loading zone table, the bond of a
// seed to a save (randomizer.ini) and the spoiler log, without game data.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/randomizer.hpp"
#include "../src/game/randomizer_save.hpp"
#include "dw4/ini.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::engine;
namespace rnd = dw4::engine::randomizer;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void seeds() {
    check(rnd::sub_seed(1234, "enemies") == rnd::sub_seed(1234, "enemies"), "a sub-seed is a function of seed and salt");
    check(rnd::sub_seed(1234, "enemies") != rnd::sub_seed(1234, "zones"), "the salts give different sub-seeds");
    check(rnd::sub_seed(1234, "zones") != rnd::sub_seed(1235, "zones"), "the seeds give different sub-seeds");
    check(rnd::draw(7, "dng101b1/GENE-03", 0) == rnd::draw(7, "dng101b1/GENE-03", 0), "a draw is repeatable");
    check(rnd::draw(7, "dng101b1/GENE-03", 0) != rnd::draw(7, "dng101b1/GENE-03", 1), "the counter changes a draw");
    check(rnd::draw(7, "dng101b1/GENE-03", 0) != rnd::draw(7, "dng101b1/GENE-04", 0), "the name changes a draw");
    // Draws spread over a small pool.
    std::array<int, 4> hits{};
    for (std::uint64_t n = 0; n < 4000; ++n) ++hits[std::size_t(rnd::draw(99, "stage/GENE", n) % 4)];
    for (const int h : hits) check(h > 800 && h < 1200, "draws spread evenly");
}

void cheats() {
    Cheats c;
    check(c.original(), "default cheats are the original game");
    c.rand_seed = 5;
    check(c.original(), "a seed without a mode changes nothing");
    c.rand_enemies = 1;
    check(c.enemy_shuffle() && !c.original(), "the enemy shuffle is not the original game");
    c.rand_enemies = 0;
    c.rand_zones = 1;
    check(c.zone_shuffle() && !c.original(), "the zone shuffle is not the original game");
    c.rand_seed = 0;
    check(!c.zone_shuffle() && c.original(), "seed 0 is off");
    // The snapshot carries the randomizer.
    StageSnapshot snapshot;
    snapshot.stage = "dng101b1";
    snapshot.cheats.rand_seed = 77;
    snapshot.cheats.rand_enemies = 2;
    snapshot.cheats.rand_zones = 1;
    snapshot.cheats.drop_seed = 77;
    const auto back = decode_snapshot(encode_snapshot(snapshot));
    check(back.cheats.rand_seed == 77 && back.cheats.rand_enemies == 2 && back.cheats.rand_zones == 1 && back.cheats.drop_seed == 77,
          "the snapshot keeps the randomizer");
}

void families_and_pools() {
    for (const char* boss : {"e_blosso", "e_mammos", "e_skulgrey", "e_anomalo", "e_tonogeko", "e_diabolo", "e_ldknight", "f_luce", "f_devl3"})
        check(rnd::boss_family(boss), std::string("boss stays: ") + boss);
    for (const char* plain : {"e_nume", "e_goburi", "e_mummy", "e_otama", "e_megadra"})
        check(!rnd::boss_family(plain), std::string("shuffleable: ") + plain);
    for (const char* stage : {"stagbs01", "stagbs06", "staggf03", "stagml11", "stagis10"})
        check(rnd::fixed_stage(stage), std::string("fixed stage: ") + stage);
    for (const char* stage : {"dng101b1", "stag0101", "staggf01", "dngq3b1e"}) check(!rnd::fixed_stage(stage), std::string("enemy stage: ") + stage);
    check(rnd::stage_tier("dng101b1") == 1 && rnd::stage_tier("stag0204") == 2 && rnd::stage_tier("dng310f2") == 3 &&
              rnd::stage_tier("dng412b4") == 4 && rnd::stage_tier("dngq8b3e") == 4 && rnd::stage_tier("dngq1b1e") == 1,
          "stage tiers");
    check(rnd::tier_weight(1, 1) > rnd::tier_weight(1, 2) && rnd::tier_weight(1, 2) > rnd::tier_weight(1, 4), "the pool favours the area's tier");
    // The pool of a known stage: its own families first, no bosses, at most 6.
    const auto seed = rnd::sub_seed(42, "enemies");
    const auto pool = rnd::stage_pool(seed, "dng102b3", {}, 0);
    check(pool == std::vector<std::string>{"e_mecha1", "e_nume", "e_ogre", "e_scum"}, "dng102b3's own families");
    const auto runtime = rnd::stage_pool(seed, "unknown01", {"e_nume", "e_blosso", "e_scum"}, 0);
    check(runtime == std::vector<std::string>{"e_nume", "e_scum"}, "an unknown stage uses what it loaded, bosses left out");
    const auto wide = rnd::stage_pool(seed, "dng101b1", {}, 6);
    check(wide.size() == 6 && wide.front() == "e_nume", "extra families fill the pool up to 6");
    check(std::set<std::string>(wide.begin(), wide.end()).size() == wide.size(), "no family twice");
    for (const auto& name : wide) check(!rnd::boss_family(name), "no boss in a pool");
    check(rnd::stage_pool(seed, "dng101b1", {}, 6) == wide, "the pool is a function of seed and stage");
    // Over many seeds a tier 1 stage draws tier 1 families more often than tier 4.
    int low = 0, high = 0;
    for (std::uint32_t s = 1; s <= 300; ++s)
        for (const auto& name : rnd::stage_pool(rnd::sub_seed(s, "enemies"), "dng101b1", {}, 2)) {
            const auto* f = rnd::family(name);
            if (!f || name == "e_nume") continue;
            if (f->tier == 1) ++low;
            if (f->tier == 4) ++high;
        }
    check(low > high, "tier weighting");
    for (const auto& f : rnd::families())
        if (!f.boss) check(f.donor && f.tier >= 1 && f.tier <= 4, std::string("donor and tier for ") + f.name);
}

void zones() {
    const auto entrances = rnd::zone_entrances();
    check(entrances.size() == 9, "nine dungeon entrances in four areas");
    for (const int rule : {rnd::kZoneRuleDerangement, rnd::kZoneRuleAny})
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        const auto zone_seed = rnd::sub_seed(seed, "zones");
        const auto perm = rnd::zone_permutation(zone_seed, rule);
        std::set<int> seen(perm.begin(), perm.end());
        check(seen.size() == entrances.size(), "the zone table is a permutation");
        for (std::size_t i = 0; i < perm.size(); ++i) {
            const auto& from = entrances[i];
            const auto& to = entrances[std::size_t(perm[i])];
            check(from.area == to.area, "entrances stay within their area");
            if (rule == rnd::kZoneRuleDerangement) check(perm[i] != int(i), "rule 1: every entrance leads somewhere new");
            // The entrance leads to the new dungeon's first floor at point 0 ...
            const auto in = rnd::zone_remap(zone_seed, rule, from.field, from.dungeon, 0);
            check(in && in->stage == to.dungeon && in->point == 0, "the entrance leads to its new dungeon");
            // ... and that dungeon's way back returns to the entrance used.
            const auto out = rnd::zone_remap(zone_seed, rule, to.dungeon, to.field, to.back_point);
            check(out && out->stage == from.field && out->point == from.back_point, "the way back is coupled");
            // The dialogs name the new targets.
            const auto text_in = rnd::zone_dialog_source(zone_seed, rule, from.field, from.text);
            check(text_in && text_in->stage == to.field && text_in->point == to.text, "the entrance dialog names the new dungeon");
            const auto text_out = rnd::zone_dialog_source(zone_seed, rule, to.dungeon, 10);
            check(text_out && text_out->stage == from.dungeon && text_out->point == 10, "the way back dialog names the field");
        }
        // Everything else keeps its target: towers, floors, lobby, gate
        // disks, defeats on the boat / train, boss chains.
        check(!rnd::zone_remap(zone_seed, rule, "stag0104", "dng103f1", 0), "towers stay");
        check(!rnd::zone_remap(zone_seed, rule, "dng101b1", "dng101b2", 0), "floors stay");
        check(!rnd::zone_remap(zone_seed, rule, "dng101b2", "stagml11", 0), "boss floors lead to the lobby");
        check(!rnd::zone_remap(zone_seed, rule, "dng101b1", "stagml11", 3), "gate disks lead to the lobby");
        check(!rnd::zone_remap(zone_seed, rule, "stag0103", "stag0102", 4), "a defeat on the boat stays");
        check(!rnd::zone_remap(zone_seed, rule, "stag0203", "stag0201", 5), "a defeat on the train stays");
        check(!rnd::zone_remap(zone_seed, rule, "stag0101", "dng101b1", 1), "another point is not the entrance");
        check(!rnd::zone_remap(zone_seed, rule, "stag0304", "dng310f1", 0), "the fortress stays");
        check(!rnd::zone_dialog_source(zone_seed, rule, "stag0101", 33), "other lines stay");
    }
}

// Rule 1 is the round 48 table, bit for bit (bound games keep their
// layout): its derangements leave only two layouts, the one of the lanes'
// story seeds and the one of seeds 5 and 7 (the round 48 review's survey).
// Rule 2 draws every arrangement of an area, the own dungeon included.
void zone_rules() {
    auto layout = [](std::uint32_t seed, int rule) { return rnd::zone_permutation(rnd::sub_seed(seed, "zones"), rule); };
    const std::vector<int> round48_a = {1, 0, 3, 2, 5, 6, 4, 8, 7}; // printed by the round 48 build (f915905)
    const std::vector<int> round48_b = {1, 0, 3, 2, 6, 4, 5, 8, 7};
    for (const std::uint32_t seed : {1u, 2u, 3u, 99u, 777u, 4242u, 12345u, 20261029u})
        check(layout(seed, rnd::kZoneRuleDerangement) == round48_a, "rule 1 keeps the round 48 layout of seed " + std::to_string(seed));
    for (const std::uint32_t seed : {5u, 7u})
        check(layout(seed, rnd::kZoneRuleDerangement) == round48_b, "rule 1 keeps the round 48 layout of seed " + std::to_string(seed));
    std::set<std::vector<int>> rule1, rule2;
    std::array<int, 4> own_area{}; // seeds under rule 2 whose area keeps every entrance's own dungeon
    std::set<std::vector<int>> area3;
    const int seeds = 4000;
    for (std::uint32_t seed = 1; seed <= std::uint32_t(seeds); ++seed) {
        rule1.insert(layout(seed, rnd::kZoneRuleDerangement));
        const auto perm = layout(seed, rnd::kZoneRuleAny);
        rule2.insert(perm);
        area3.insert(std::vector<int>(perm.begin() + 4, perm.begin() + 7));
        for (int area = 1; area <= 4; ++area) {
            bool own = true;
            for (std::size_t i = 0; i < perm.size(); ++i)
                if (rnd::zone_entrances()[i].area == area) own &= perm[i] == int(i);
            own_area[std::size_t(area - 1)] += own;
        }
    }
    check(rule1.size() == 2, "rule 1: two layouts in all, " + std::to_string(rule1.size()));
    check(rule2.size() == 48, "rule 2: every one of the 2 * 2 * 6 * 2 layouts comes up, " + std::to_string(rule2.size()));
    check(area3.size() == 6, "rule 2: area 3 takes all six arrangements");
    for (int area = 1; area <= 4; ++area) {
        const double share = double(own_area[std::size_t(area - 1)]) / seeds;
        const double expected = area == 3 ? 1.0 / 6 : 0.5;
        check(share > expected * 0.85 && share < expected * 1.15,
              "rule 2: area " + std::to_string(area) + " keeps its own dungeons in about " + std::to_string(expected) + " of the seeds");
    }
    check(layout(20261029, rnd::kZoneRuleAny) != layout(5, rnd::kZoneRuleAny) || layout(20261029, rnd::kZoneRuleAny) != layout(99, rnd::kZoneRuleAny),
          "rule 2 tells the story seeds apart");
    check(layout(77, 3) == layout(77, rnd::kZoneRuleAny), "a rule above the last known plays the last known");
    check(std::string(rnd::zone_rule_text(1)).find("rule 1") != std::string::npos &&
              std::string(rnd::zone_rule_text(2)).find("rule 2") != std::string::npos,
          "the rules have names");
    Cheats c;
    c.rand_seed = 20261029;
    c.rand_zones = std::uint8_t(rnd::kZoneRuleAny);
    const std::string text = rnd::spoiler(c);
    check(text.find("rule 2") != std::string::npos, "the spoiler names the rule");
}

void binding() {
    using namespace dw4::game;
    const auto folder = std::filesystem::temp_directory_path() / "dw4_randomizer_tests";
    std::filesystem::remove_all(folder);
    check(!load_randomizer_binding(folder, 0x1234ABCD), "no bond without the file");
    RandomizerConfig config{4242, true, 2, 1};
    check(config.active(), "an active config");
    check(store_randomizer_binding(folder, 0x1234ABCD, "TESTER", config), "the bond is written");
    check(store_randomizer_binding(folder, 0x00000001, "OTHER", RandomizerConfig{9, false, 1, 0}), "a second bond");
    const auto back = load_randomizer_binding(folder, 0x1234ABCD);
    check(back && *back == config, "the bond reads back");
    const auto other = load_randomizer_binding(folder, 1);
    check(other && other->seed == 9 && other->enemies == 1 && !other->drops && other->zones == 0, "bonds are per character");
    check(!load_randomizer_binding(folder, 2), "an unbound character");
    check(write_randomizer_spoiler(folder, 0x1234ABCD, config), "the spoiler log is written");
    std::stringstream text;
    {
        std::ifstream in(folder / "randomizer_1234ABCD_spoiler.txt");
        text << in.rdbuf();
    }
    check(text.str().find("seed 4242") != std::string::npos, "the spoiler names the seed");
    check(text.str().find("[loading zones]") != std::string::npos && text.str().find("[enemy pools]") != std::string::npos,
          "the spoiler lists zones and pools");
    // Config <-> cheats.
    Cheats c;
    apply_randomizer(config, c);
    check(c.rand_seed == 4242 && c.drop_seed == 4242 && c.rand_enemies == 2 && c.rand_zones == 1, "the config into the cheats");
    check(randomizer_from_cheats(c) == config, "and back");
    apply_randomizer(RandomizerConfig{4242, false, 0, 0}, c);
    check(c.drop_seed == 0 && !c.enemy_shuffle() && !c.zone_shuffle(), "modes off");
    const auto parsed = parse_randomizer("17,2,0,0");
    check(parsed && parsed->seed == 17 && parsed->enemies == 2 && parsed->zones == 0 && !parsed->drops, "the test variable");
    const auto plain = parse_randomizer("18");
    check(plain && plain->seed == 18 && plain->enemies == 1 && plain->zones == rnd::kZoneRuleCurrent && plain->drops,
          "a bare seed turns everything on (the current zone rule)");
    const auto old_rule = parse_randomizer("18,1,1,1");
    check(old_rule && old_rule->zones == rnd::kZoneRuleDerangement, "zones 1: the round 48 rule (old test runs replay)");
    // The zone rule in the bond: a new bond names it, a bond written before
    // the rules were numbered plays rule 1.
    check(store_randomizer_binding(folder, 0x0000BEEF, "RULE2", RandomizerConfig{31, false, 0, rnd::kZoneRuleAny}), "a rule 2 bond");
    {
        const auto ini = dw4::IniFile::load(folder / "randomizer.ini");
        check(ini && ini->get_int("character_0000BEEF", "zone_rule", 0) == 2, "the bond names the rule");
    }
    check(load_randomizer_binding(folder, 0x0000BEEF)->zones == rnd::kZoneRuleAny, "a rule 2 bond reads back as rule 2");
    {
        auto ini = dw4::IniFile::load(folder / "randomizer.ini").value();
        ini.set("character_0000CAFE", "name", "ROUND48");
        ini.set_int("character_0000CAFE", "seed", 20261029);
        ini.set_bool("character_0000CAFE", "drops", true);
        ini.set("character_0000CAFE", "enemies", "each");
        ini.set("character_0000CAFE", "zones", "dungeons");
        check(ini.save(folder / "randomizer.ini"), "a round 48 bond written");
    }
    const auto round48 = load_randomizer_binding(folder, 0x0000CAFE);
    check(round48 && round48->seed == 20261029 && round48->zones == rnd::kZoneRuleDerangement, "a round 48 bond plays rule 1");
    Cheats r48;
    apply_randomizer(*round48, r48);
    check(r48.rand_zones == rnd::kZoneRuleDerangement && r48.zone_shuffle(), "and the cheats carry rule 1");
    // Switching the zones off drops the rule.
    check(store_randomizer_binding(folder, 0x0000BEEF, "", RandomizerConfig{31, true, 0, 0}), "zones off");
    {
        const auto ini = dw4::IniFile::load(folder / "randomizer.ini");
        check(ini && !ini->get("character_0000BEEF", "zone_rule"), "no rule without the zone shuffle");
    }
    // "Play this save without the randomizer": an explicit off bond.
    check(store_randomizer_binding(folder, 0x0000BEEF, "", RandomizerConfig{}), "an off bond");
    const auto off = load_randomizer_binding(folder, 0x0000BEEF);
    check(off && !off->active(), "an off bond is a bond that plays the original game");
    check(choose_randomizer_bond(off.has_value(), false, true, false) == BondChoice::use_bond, "and it wins over active settings");
    // The F1 lines.
    check(describe_randomizer(RandomizerConfig{}) == "the original game", "the original game");
    check(describe_randomizer(RandomizerConfig{12, true, 2, rnd::kZoneRuleAny}) ==
              "seed 12 - enemies: one family per wave, loading zones: dungeon entrances, item drops: shuffled",
          "a bond described: " + describe_randomizer(RandomizerConfig{12, true, 2, rnd::kZoneRuleAny}));
    check(describe_randomizer(RandomizerConfig{12, false, 1, 1}).find("(rule 1: no entrance keeps its own dungeon)") != std::string::npos,
          "an old rule is named");
    // A bond written without a name keeps the stored name (the F1 bind button).
    check(store_randomizer_binding(folder, 0x1234ABCD, "", RandomizerConfig{5, true, 0, 0}), "a rebind without a name");
    {
        const auto ini = dw4::IniFile::load(folder / "randomizer.ini");
        check(ini && ini->get_or("character_1234ABCD", "name", "") == "TESTER", "the stored name stays");
        check(ini && ini->get_int("character_1234ABCD", "seed", 0) == 5, "the new seed is stored");
    }
    // Who gets bound: only new games (test runs: every character unless DW4_RANDOMIZER_BIND=new).
    check(choose_randomizer_bond(true, false, true, false) == BondChoice::use_bond, "a bond wins");
    check(choose_randomizer_bond(true, true, false, false) == BondChoice::use_bond, "a bond wins over inactive settings");
    check(choose_randomizer_bond(false, true, true, false) == BondChoice::bind_settings, "a new game binds the settings");
    check(choose_randomizer_bond(false, false, true, false) == BondChoice::original, "a called old save stays original");
    check(choose_randomizer_bond(false, true, false, false) == BondChoice::original, "settings off: original");
    check(choose_randomizer_bond(false, false, true, true) == BondChoice::bind_settings, "test runs bind a called save");
    check(choose_randomizer_bond(false, false, false, true) == BondChoice::original, "test runs without a randomizer");
    std::filesystem::remove_all(folder);
}
} // namespace

int main() {
    try {
        seeds();
        cheats();
        families_and_pools();
        zones();
        zone_rules();
        binding();
    } catch (const std::exception& error) {
        std::cerr << "randomizer test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "randomizer tests passed (" << checks << " checks)\n";
    return 0;
}
