// The randomizer's seeds, enemy pools, loading zone table, the bond of a
// seed to a save (randomizer.ini) and the spoiler log, without game data.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/randomizer.hpp"
#include "../src/game/randomizer_save.hpp"

#include <algorithm>
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
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        const auto zone_seed = rnd::sub_seed(seed, "zones");
        const auto perm = rnd::zone_permutation(zone_seed);
        std::set<int> seen(perm.begin(), perm.end());
        check(seen.size() == entrances.size(), "the zone table is a permutation");
        for (std::size_t i = 0; i < perm.size(); ++i) {
            const auto& from = entrances[i];
            const auto& to = entrances[std::size_t(perm[i])];
            check(from.area == to.area, "entrances stay within their area");
            check(perm[i] != int(i), "every entrance leads somewhere new");
            // The entrance leads to the new dungeon's first floor at point 0 ...
            const auto in = rnd::zone_remap(zone_seed, from.field, from.dungeon, 0);
            check(in && in->stage == to.dungeon && in->point == 0, "the entrance leads to its new dungeon");
            // ... and that dungeon's way back returns to the entrance used.
            const auto out = rnd::zone_remap(zone_seed, to.dungeon, to.field, to.back_point);
            check(out && out->stage == from.field && out->point == from.back_point, "the way back is coupled");
            // The dialogs name the new targets.
            const auto text_in = rnd::zone_dialog_source(zone_seed, from.field, from.text);
            check(text_in && text_in->stage == to.field && text_in->point == to.text, "the entrance dialog names the new dungeon");
            const auto text_out = rnd::zone_dialog_source(zone_seed, to.dungeon, 10);
            check(text_out && text_out->stage == from.dungeon && text_out->point == 10, "the way back dialog names the field");
        }
        // Everything else keeps its target: towers, floors, lobby, gate
        // disks, defeats on the boat / train, boss chains.
        check(!rnd::zone_remap(zone_seed, "stag0104", "dng103f1", 0), "towers stay");
        check(!rnd::zone_remap(zone_seed, "dng101b1", "dng101b2", 0), "floors stay");
        check(!rnd::zone_remap(zone_seed, "dng101b2", "stagml11", 0), "boss floors lead to the lobby");
        check(!rnd::zone_remap(zone_seed, "dng101b1", "stagml11", 3), "gate disks lead to the lobby");
        check(!rnd::zone_remap(zone_seed, "stag0103", "stag0102", 4), "a defeat on the boat stays");
        check(!rnd::zone_remap(zone_seed, "stag0203", "stag0201", 5), "a defeat on the train stays");
        check(!rnd::zone_remap(zone_seed, "stag0101", "dng101b1", 1), "another point is not the entrance");
        check(!rnd::zone_remap(zone_seed, "stag0304", "dng310f1", 0), "the fortress stays");
        check(!rnd::zone_dialog_source(zone_seed, "stag0101", 33), "other lines stay");
    }
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
    check(plain && plain->seed == 18 && plain->enemies == 1 && plain->zones == 1 && plain->drops, "a bare seed turns everything on");
    std::filesystem::remove_all(folder);
}
} // namespace

int main() {
    try {
        seeds();
        cheats();
        families_and_pools();
        zones();
        binding();
    } catch (const std::exception& error) {
        std::cerr << "randomizer test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "randomizer tests passed (" << checks << " checks)\n";
    return 0;
}
