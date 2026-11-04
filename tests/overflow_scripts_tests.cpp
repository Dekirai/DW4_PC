// Data Overflow with the game data (argument 1, the CMake cache value
// DW4_TEST_DATA; skipped without): T10 of docs/SURVIVAL.md 14.1 and the run
// engine's hooks on the Violet Ring.
//
// T10 (a): the native player hit handler under RuleSet::original against
// the stages' own hit_p_<form>.lua over hidden fights (dng101b1 with a
// levelled Agumon, staggf01 with a level 1 Veemon that falls): two engines,
// one after the other (the system generator is process-wide), the same
// spawns and pads; the net fingerprint (models, HP, sequences, positions,
// flags, the generator state, ...) equal on every step, the hits counted.
// T10 (b): the Violet Ring's native field start against its first.lua with
// the boss lines left out: the layers, the stage switches, the HUD panels,
// the fade, the camera mode and the pad input equal; the boss and its bar
// only in the original.
// Run hooks: the run's donors (H2), the native start (H10), aimed port
// hitboxes (H6) with the Art factor and the skill rule (H4), the run curve
// and the level-up rule (H4), the player hit rule (H10: no reaction, MUTEKI),
// the director's step and notifications (H9, H5) and the exits (H3).
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/overflow.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
using dw4::PadSample;

int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path card_dir() {
    auto dir = std::filesystem::temp_directory_path() / "dw4_overflow_tests_cards";
    std::filesystem::create_directories(dir);
    return dir;
}

std::array<PadSample, 4> pads_at(std::uint64_t i, bool walk) {
    std::array<PadSample, 4> pads{};
    dw4::HostPadInput input;
    if (walk) input.stick = {std::cos(float(i) * 0.01f) * 0.6f, std::sin(float(i) * 0.01f) * 0.6f};
    pads[0] = dw4::host_pad_sample(input);
    pads[0].connected = true;
    return pads;
}

void host_step(Engine& engine, const std::array<PadSample, 4>& pads) {
    engine.step(pads);
    if (!engine.held()) engine.traverse_scene();
}

Vec3f position(const ModelCtrlManager::ModelRam& ram) {
    return ram.node ? Vec3f{ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)} : Vec3f{};
}

// A direct start into `stage` with `form` (fast loading), until the stage
// runs; then `settle` more steps.
void start(Engine& engine, const std::string& stage, const std::string& form, int settle) {
    engine.set_card_root(card_dir());
    engine.cheats().fast_loading = true;
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[0].digimon = form;
    engine.request_stage(stage);
    int after = -1;
    for (int i = 0; i < 4000; ++i) {
        host_step(engine, pads_at(0, false));
        if (!engine.loading() && engine.stage_active() && engine.stage_name() == stage) {
            if (after < 0) after = 0;
            if (++after >= settle) return;
        }
    }
    throw std::runtime_error("the stage " + stage + " did not start");
}

// `count` enemies of the stage's families on a ring around player 1.
int spawn_ring(Engine& engine, int count, float ring, float spread = 0.0f) {
    auto* models = engine.get<ModelCtrlManager>();
    auto* db = engine.get<DatabaseManager>();
    auto* lands = engine.get<HitLandManager>();
    std::vector<std::pair<std::string, int>> families; // name, record
    std::set<std::string> seen;
    for (const auto& ram : models->rams())
        if (ram->category == "ENEMY" && ram->info_name.starts_with("e_") && seen.insert(ram->info_name).second)
            for (int r = 0; r < db->rows("enemy"); ++r)
                if (db->text("enemy", r, "MODEL") == ram->info_name) {
                    families.push_back({ram->info_name, r});
                    break;
                }
    check(!families.empty(), engine.stage_name() + " has enemy families");
    const auto* player = models->player(0);
    const Vec3f p = player ? position(*player) : Vec3f{};
    const Messenger::PortOnly scope(engine.messenger());
    int spawned = 0;
    for (int k = 0; k < count * 4 && spawned < count; ++k) {
        const auto& [name, record] = families[std::size_t(k) % families.size()];
        Packet find;
        engine.messenger().send(engine, "MODELCTRL", name, -1, "FIND_SPACE", find);
        if (find.ints.empty()) continue;
        const float a = 6.2831853f * float(spawned) / float(std::max(1, count));
        const float r = ring + spread * float(spawned % 3);
        Vec3f at{p.x + r * std::cos(a), p.y + 10.0f, p.z + r * std::sin(a)};
        if (lands) {
            const auto land = lands->world().check_land({at.x, p.y + 60.0f, at.z}, 1.0f, 400.0f, 400.0f);
            at.y = land.found ? land.position.y : p.y;
        }
        Packet generate;
        generate.vectors = {at, Vec3f{0, 0, 0}};
        generate.strings.emplace_back(std::string("SQ_ENTR"));
        generate.ints = {record, db->number("enemy", record, "HPMAX").value_or(50)};
        engine.messenger().send(engine, "MODELCTRL", name, find.ints[0], "GENERATE", generate);
        ++spawned;
    }
    Packet on;
    engine.messenger().send(engine, "ENEMY", std::nullopt, -1, "ONCTRL", on);
    engine.messenger().send(engine, "PLAYER", std::nullopt, -1, "ONCTRL", on);
    engine.set_pad_input_enabled(true);
    return spawned;
}

// ---------------------------------------------------------------------------
// T10 (a)
// ---------------------------------------------------------------------------
struct Fight {
    std::string stage, form;
    int levels{}, enemies{}, frames{};
    float ring{};
    bool walk{};
};
struct FightTrace {
    std::vector<std::uint64_t> fingerprints;
    int hits{}, spawned{};
    std::set<std::string> sequences; // the player's sequences
    double seconds{};
};

FightTrace record_fight(const std::filesystem::path& data, const Fight& fight, bool native) {
    const auto t0 = std::chrono::steady_clock::now();
    FightTrace trace;
    Engine engine(data);
    if (native) engine.set_overflow_rules(std::make_shared<OverflowRules>(RuleSet::original));
    start(engine, fight.stage, fight.form, 120);
    auto* sys = engine.get<GameSystem>();
    if (fight.levels) sys->cheat_levels(engine, 0, fight.levels);
    trace.spawned = spawn_ring(engine, fight.enemies, fight.ring);
    auto* models = engine.get<ModelCtrlManager>();
    int last_hp = sys->players[0].hp;
    for (int i = 0; i < fight.frames; ++i) {
        host_step(engine, pads_at(std::uint64_t(i), fight.walk && (i / 400) % 2 == 1));
        trace.fingerprints.push_back(engine.net_fingerprint());
        const int hp = sys->players[0].hp;
        if (hp < last_hp) ++trace.hits;
        last_hp = hp;
        if (const auto* player = models->player(0); player && player->generated) trace.sequences.insert(player->seq_name);
    }
    trace.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return trace;
}

void t10_hits(const std::filesystem::path& data) {
    const Fight fights[] = {
        {"dng101b1", "p_agumon", 15, 20, 3600, 14.0f, true},
        {"staggf01", "p_vmon", 0, 10, 3000, 14.0f, false},
    };
    for (const auto& fight : fights) {
        const auto script = record_fight(data, fight, false);
        const auto native = record_fight(data, fight, true);
        std::size_t equal = 0;
        while (equal < script.fingerprints.size() && equal < native.fingerprints.size() &&
               script.fingerprints[equal] == native.fingerprints[equal])
            ++equal;
        std::string sequences;
        for (const auto& s : script.sequences) sequences += " " + s;
        std::cout << "  T10a " << fight.stage << " " << fight.form << ": " << script.spawned << " enemies, " << script.hits
                  << " hits on the player, sequences" << sequences << "; " << equal << " of " << script.fingerprints.size()
                  << " steps equal (" << script.seconds << " s / " << native.seconds << " s)\n";
        check(script.hits >= 10, fight.stage + ": the fight hit the player");
        check(equal == script.fingerprints.size() && native.fingerprints.size() == script.fingerprints.size(),
              fight.stage + ": the native player hit handler equals hit_p_" + fight.form.substr(2) + ".lua (first difference at step " +
                  std::to_string(equal) + ")");
        check(script.hits == native.hits && script.sequences == native.sequences, fight.stage + ": the same hits and reactions");
    }
}

// ---------------------------------------------------------------------------
// T10 (b)
// ---------------------------------------------------------------------------
struct StartState {
    std::vector<std::pair<std::string, bool>> layers;
    std::vector<std::vector<std::uint32_t>> switches;
    std::map<std::string, std::string> hud; // "name#index" -> "generated seq"
    bool demo_camera{}, pad_input{};
    bool boss{}, boss_bar{};
    std::string fade;
};

const Node* find_layer_switch(const Node* node, int depth = 0) {
    if (!node || depth > 64) return nullptr;
    if (node->kind == NodeKind::switch_node)
        for (const auto* child : node->children)
            if (child && child->name == "WN00") return node;
    for (const auto* child : node->children)
        if (const auto* found = find_layer_switch(child, depth + 1)) return found;
    return nullptr;
}

StartState start_state(const std::filesystem::path& data, bool native) {
    Engine engine(data);
    if (native) engine.set_overflow_rules(std::make_shared<OverflowRules>(RuleSet::run));
    engine.cheats().god = true; // (the original's boss must not end the comparison)
    start(engine, "stagbs02", "p_agumon", 1);
    for (int i = 0; i < 700; ++i) host_step(engine, pads_at(0, false));
    StartState state;
    if (const auto* layers = find_layer_switch(engine.global_root()))
        for (std::size_t i = 0; i < layers->children.size(); ++i) state.layers.push_back({layers->children[i]->name, layers->child_enabled(i)});
    for (const auto& row : engine.messenger().rows()) {
        if (row.category != "SWITCHCTRL" || !row.recipient) continue;
        const auto target = row.recipient->info.ref(7);
        if (const Node* node = target ? engine.scenes().node(target) : nullptr; node && node->kind == NodeKind::switch_node)
            state.switches.push_back(node->mask);
    }
    auto* models = engine.get<ModelCtrlManager>();
    for (const auto& ram : models->rams()) {
        const auto& name = ram->info_name;
        if (name.starts_with("i_stpnl")) state.hud[name + "#" + std::to_string(ram->index)] = (ram->generated ? "on " : "off ") + ram->seq_name;
        if (name == "i_fadewn01" && ram->index == 0) state.fade = ram->seq_name;
        if (name == "f_belvam" && ram->generated) state.boss = true;
        if (name == "i_bosshp02" && ram->generated) state.boss_bar = true;
    }
    Packet demo;
    {
        const Messenger::PortOnly scope(engine.messenger());
        engine.messenger().send(engine, "CAMERA", std::nullopt, -1, "ISDEMOCAMERA", demo);
    }
    state.demo_camera = demo.boolean(0);
    state.pad_input = engine.pad_input_enabled();
    return state;
}

void t10_field_start(const std::filesystem::path& data) {
    const auto script = start_state(data, false);
    const auto native = start_state(data, true);
    std::cout << "  T10b stagBS02: " << script.layers.size() << " layers, " << script.switches.size() << " stage switches, "
              << script.hud.size() << " HUD panel RAMs; boss " << script.boss << " / " << native.boss << ", boss bar "
              << script.boss_bar << " / " << native.boss_bar << ", fade " << script.fade << " / " << native.fade << '\n';
    if (script.layers != native.layers || script.hud != native.hud) {
        std::cout << "  layers (first.lua / native):";
        for (std::size_t i = 0; i < std::max(script.layers.size(), native.layers.size()); ++i)
            std::cout << ' ' << (i < script.layers.size() ? script.layers[i].first + "=" + std::to_string(script.layers[i].second) : "-") << '/'
                      << (i < native.layers.size() ? native.layers[i].first + "=" + std::to_string(native.layers[i].second) : "-");
        std::cout << "\n  HUD (first.lua / native):";
        for (const auto& [key, value] : script.hud)
            std::cout << ' ' << key << '=' << value << '/' << (native.hud.contains(key) ? native.hud.at(key) : "-");
        std::cout << '\n';
    }
    check(!script.layers.empty() && script.layers == native.layers, "the layers equal first.lua's");
    check(script.switches == native.switches, "the stage switches (SWITCHCTRL ALLON) equal first.lua's");
    check(!script.hud.empty() && script.hud == native.hud, "the HUD panels equal first.lua's");
    check(script.fade == native.fade, "the fade equals first.lua's");
    check(script.demo_camera == native.demo_camera && !native.demo_camera, "the box camera, as after first.lua's intro");
    check(script.pad_input && native.pad_input, "the pad input is on");
    check(script.boss && script.boss_bar, "first.lua generates the boss and its bar");
    check(!native.boss && !native.boss_bar, "the native start leaves the boss lines out");
}

// ---------------------------------------------------------------------------
// The run's hooks on the Violet Ring
// ---------------------------------------------------------------------------
struct TestDirector : Director {
    OverflowRules* rules{};
    int fields{}, kills{}, levels{}, hits{}, misses{}, clamped{}, art_casts{};
    int refilled{};          // level-ups that filled a hurt Digimon to the top
    float muteki_after{-1};  // the player's MUTEKI right after a damaging hit
    std::vector<RunEnd> ends;
    int last_hp{}, last_max{};
    void field_started(Engine&) override { ++fields; }
    void enemy_defeated(Engine&, int, int, std::string_view, int) override { ++kills; }
    void level_up(Engine& engine, int slot, int) override {
        ++levels;
        const auto& p = engine.get<GameSystem>()->players[std::size_t(slot)];
        if (last_max > 0 && last_hp * 10 < last_max * 9 && p.hp == p.max_hp) ++refilled;
    }
    void player_hit(Engine& engine, const HitTaken& hit) override {
        ++hits;
        if (hit.missed) ++misses;
        if (hit.clamped) ++clamped;
        if (hit.damage > 0 && hit.hp_after > 0)
            if (auto* p = engine.get<ModelCtrlManager>()->find(engine.get<GameSystem>()->players[std::size_t(hit.slot)].digimon, hit.slot))
                muteki_after = p->muteki;
    }
    void run_end(Engine&, const RunEnd& end) override { ends.push_back(end); }
    // H6: an Art every 75 frames, aimed at the nearest enemy within 22.
    void step(Engine& engine) override {
        auto& p = engine.get<GameSystem>()->players[0];
        last_hp = p.hp;
        last_max = p.max_hp;
        if (engine.frame() % 75 != 0 || engine.loading()) return;
        auto* models = engine.get<ModelCtrlManager>();
        auto* self = models->find(p.digimon, 0);
        if (!self || !self->generated || !self->node) return;
        const Vec3f me = position(*self);
        float best = 22.0f * 22.0f;
        const ModelCtrlManager::ModelRam* target = nullptr;
        for (const auto& ram : models->rams())
            if (ram->generated && ram->category == "ENEMY" && ram->info_name.starts_with("e_") && ram->node && ram->hp > 0) {
                const Vec3f q = position(*ram);
                const float d2 = (q.x - me.x) * (q.x - me.x) + (q.z - me.z) * (q.z - me.z);
                if (d2 < best) {
                    best = d2;
                    target = ram.get();
                }
            }
        if (!target) return;
        const Vec3f q = position(*target);
        ModelCtrlManager::ModelRam::Hitbox box;
        box.kind = 10000;
        box.delay = 1;
        box.end = 9;
        box.offset = {0, 4, 0};
        box.size = {8, 8, 8};
        box.tag = 1;
        box.aimed = true;
        box.yaw = std::atan2(q.x - me.x, q.z - me.z);
        box.shift = std::sqrt(best);
        models->port_hitbox(*self, box);
        ++art_casts;
    }
};

void run_hooks(const std::filesystem::path& data) {
    TestDirector director;
    auto rules = std::make_shared<OverflowRules>(RuleSet::run);
    rules->director = &director;
    director.rules = rules.get();
    rules->donors.families = {"e_goburi", "e_nume"};
    rules->donors.streams = {{"stag0101", {"ST_DEATH"}}};
    rules->skills.cap.fill(2500);
    rules->damage.taken_factor = 0.5f; // (the time and tier factor: a long enough fight for the checks)
    Engine engine(data);
    engine.set_overflow_rules(rules);
    start(engine, "stagbs02", "p_agumon", 300);
    auto* models = engine.get<ModelCtrlManager>();
    auto* sys = engine.get<GameSystem>();
    std::map<std::string, int> rams;
    bool boss = false, hud = false;
    for (const auto& ram : models->rams()) {
        ++rams[ram->info_name];
        boss = boss || (ram->info_name == "f_belvam" && ram->generated);
        hud = hud || (ram->info_name == "i_stpnl01" && ram->generated);
    }
    check(director.fields == 1, "the native field start ran (H10) and told the director");
    check(!boss && hud, "no boss, the HUD is up");
    check(rams["e_goburi"] == 10 && rams["e_nume"] == 10, "the donor families are in the arena (H2): Goblimon 10, Numemon 10 RAMs");
    check(!engine.soft_reset_allowed() && !engine.console_reset().enabled(), "no soft or console reset (H3)");
    // H2 streams: Death Valley's field music plays in the Violet Ring.
    {
        bool listed = false;
        for (const auto& line : engine.log_lines()) listed = listed || line.find("~ST_DEATH (stag0101)") != std::string::npos;
        check(listed, "the donor stream ST_DEATH came from stag0101");
        {
            const Messenger::PortOnly scope(engine.messenger());
            Packet play;
            play.strings.emplace_back(std::string("ST_DEATH"));
            engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", play);
        }
        for (int i = 0; i < 30; ++i) host_step(engine, pads_at(0, false));
        bool missing = false;
        for (const auto& note : engine.notes()) missing = missing || note.text.find("ST_DEATH") != std::string::npos;
        check(!missing, "and SOUND PLAY finds it");
    }
    // The director's helpers: the picture of the box camera, the heal MP.
    {
        const Vec3f me = position(*models->player(0));
        check(on_screen(engine, me, 1.0f), "the player is in the picture");
        FrameCamera camera;
        engine.get<CameraManager>()->frame_camera(engine, camera);
        Mat4 inverse;
        invert(camera.view, inverse);
        const Vec3f eye{inverse(3, 0), inverse(3, 1), inverse(3, 2)};
        const Vec3f behind{me.x + (eye.x - me.x) * 0.6f, me.y, me.z + (eye.z - me.z) * 0.6f};
        check(!on_screen(engine, behind, 4.0f / 3.0f), "the ground behind the player, towards the camera, is not");
        auto* db = engine.get<DatabaseManager>();
        int heal = -1;
        for (int r = 0; r < db->rows("pcTech") && heal < 0; ++r)
            if (db->number("pcTech", r, "SKILL").value_or(0) == kHealSkill) heal = r;
        check(heal >= 0 && technique_mp_cost(engine, heal) == db->number("pcTech", heal, "MP").value_or(0) / 8, "a heal costs pcTech MP / 8");
        check(technique_mp_cost(engine, 0) == 0, "Fire costs nothing (cooldowns)");
    }
    const int start_level = sys->players[0].stats.level;
    const int start_skill = sys->players[0].stats.skill[0];
    check(sys->players[0].stats.next_exp == 80, "the run's curve: 80 EXP for level 2 (H4)");
    const int spawned = spawn_ring(engine, 9, 16.0f);
    int reaction_steps = 0;
    for (int i = 0; i < 2400 && !rules->ended(); ++i) {
        host_step(engine, pads_at(std::uint64_t(i), (i / 300) % 2 == 1));
        if (const auto* player = models->player(0); player && player->generated) {
            const auto& seq = player->seq_name;
            if (seq == "SQ_SDMG" || seq == "SQ_MDMG" || seq == "SQ_LDMG") ++reaction_steps;
        }
        if (director.kills >= 4 && i % 600 == 599) spawn_ring(engine, 5, 16.0f);
    }
    const auto& p = sys->players[0];
    std::cout << "  run hooks: " << spawned << " spawned, " << director.art_casts << " Arts, " << rules->tally.dealt_by[1] << " Art damage, "
              << director.kills << " defeats, level " << start_level << " -> " << p.stats.level << " (EXP " << p.stats.exp << ", next "
              << p.stats.next_exp << "), Blunt skill " << start_skill << " -> " << p.stats.skill[0] << ", " << director.hits
              << " enemy hits (" << director.misses << " missed, " << director.clamped << " clamped), MUTEKI after a hit "
              << director.muteki_after << ", HP " << p.hp << "/" << p.max_hp << ", steps without control "
              << rules->tally.no_control_steps << "/" << rules->tally.steps << ", panels " << rules->tally.panels << " (+"
              << rules->tally.panels_skipped << " left out)\n";
    check(director.art_casts >= 8 && rules->tally.dealt_by[1] > 0, "the aimed port hitboxes hit (H6) with their tag");
    check(director.kills > 0, "Arts defeat enemies (H5: the director heard of it)");
    check(p.stats.skill[0] > start_skill, "the class skill grows by Art hits (H4 skill rule)");
    check(p.stats.next_exp == run_next_exp(p.stats.level, 80, 50, 6), "NEXTEXP follows the run's curve");
    if (p.stats.exp >= 80) check(p.stats.level >= 2 && director.levels >= 1, "80 EXP reach level 2");
    check(director.refilled == 0, "a level-up never refills a hurt Digimon (H4 level-up rule)");
    check(director.hits > 0, "the enemies hit the player (the native hit rule ran)");
    check(reaction_steps == 0, "a normal enemy's hit plays no damage sequence");
    check(director.muteki_after == float(rules->player_hit.muteki) || director.muteki_after < 0, "MUTEKI after a damaging hit");
    check(rules->tally.no_control_steps * 20 <= rules->tally.steps, "at most 5 % of the steps without control");
    // H3: a stage change the director did not ask for ends the run instead.
    const std::size_t ends_before = director.ends.size();
    engine.request_stage("stagml11");
    check(!engine.pending_stage() || *engine.pending_stage() != "stagml11", "a stage change is refused");
    if (ends_before == 0) check(director.ends.size() == 1 && director.ends[0].reason == EndReason::stage_exit, "and reported as the run's end");
}

} // namespace

int main(int argc, char** argv) {
    std::string data = argc > 1 ? argv[1] : "";
    if (data.empty())
        if (const char* env = std::getenv("DW4_TEST_DATA")) data = env;
    if (data.empty() || !std::filesystem::exists(data)) {
        std::cout << "overflow scripts: skipped (no game data: DW4_TEST_DATA)\n";
        return 0;
    }
    try {
        std::cout << "overflow scripts with the game data in " << data << '\n';
        t10_hits(data);
        t10_field_start(data);
        run_hooks(data);
    } catch (const std::exception& error) {
        std::cerr << "overflow scripts test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overflow scripts: " << checks << " checks passed\n";
    return 0;
}
