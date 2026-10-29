#include "dw4/engine/managers.hpp"
#include "dw4/engine/randomizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace dw4::engine {
namespace {
constexpr float kRadiansToDegrees = 57.2958f; // 80425EF4
// Randomizer watchdog: a swapped enemy that spends this many updates in the
// air (it fell off a ledge it was not made for, or never lands) or drops this
// far below its spawn point is removed; it counts as defeated without a drop.
constexpr int kWatchAirborne = 360;
constexpr float kWatchDrop = 150.0f;

std::vector<Vec3f> vec3_list(const ObjectView& list) {
    std::vector<Vec3f> out;
    if (!list) return out;
    const auto bytes = list.memory(4);
    const auto count = std::size_t(std::max(0, list.i32(2)));
    for (std::size_t i = 0; i < count && (i + 1) * 12 <= bytes.size(); ++i)
        out.push_back({read_f32le(bytes, i * 12), read_f32le(bytes, i * 12 + 4), read_f32le(bytes, i * 12 + 8)});
    return out;
}

std::vector<float> float_list(const ObjectView& list) {
    std::vector<float> out;
    if (!list) return out;
    const auto bytes = list.memory(4);
    const auto count = std::size_t(std::max(0, list.i32(2)));
    for (std::size_t i = 0; i < count && (i + 1) * 4 <= bytes.size(); ++i) out.push_back(read_f32le(bytes, i * 4));
    return out;
}
} // namespace

// beGeneraterData slots: [2] name, [3] base position, [4] _genePosIsGlobal,
// [5] _genePosList, [6] _geneRotYRadianList, [7] _geneEnemyId, [8] _geneEnemySeq,
// [9] _startModeStr, [10] _startData, [11] _fillModeStr, [12] _fillData,
// [13] _callLua, [14] _playerDataList (per player count: [2] count, [3] max).
void GeneraterManager::add_info(Engine& engine, const ObjectView& info) {
    const auto data = info.list(7);
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<GeneRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index;
        ram->active = record ? record.boolean(5) : false;
        // RAM +14 is the authored active byte: active generators run at once.
        ram->started = ram->active;
        if (std::size_t(index) < data.size()) ram->data = data[std::size_t(index)];
        engine.messenger().add_row("GENERATER", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
        ++index;
    }
}

bool GeneraterManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<GeneRam&>(base);
    // 802FD600: a named command only concerns the generator of that data name.
    if (packet.has_string(0) && (!ram.data || packet.string(0) != ram.data.str(2))) return true;
    if (command == "START") {
        if (!ram.work) {
            if (!ram.done) ram.started = true;
        } else {
            ram.system_freeze = false;
        }
        return true;
    }
    if (command == "STOP") {
        if (ram.work) ram.system_freeze = true;
        return true;
    }
    if (command == "STOPREVERS") {
        if (!ram.work) return true;
        ram.system_freeze = true;
        for (std::size_t i = 0; i < ram.enemies.size(); ++i) {
            Packet p;
            engine.messenger().send(engine, "MODELCTRL", i < ram.enemy_models.size() ? ram.enemy_models[i] : ram.enemy_name, ram.enemies[i],
                                    "SQ_ERAS", p);
        }
        ram.remaining += int(ram.enemies.size());
        ram.enemies.clear();
        ram.enemy_models.clear();
        ram.watch.clear();
        if (ram.remaining != 0 && ram.state == 3) ram.state = 2;
        return true;
    }
    if (command == "RESET") {
        ram.started = false;
        ram.work = false;
        ram.done = false;
        ram.state = 0;
        ram.enemies.clear();
        ram.enemy_models.clear();
        ram.watch.clear();
        return true;
    }
    if (command == "RESTART") {
        ram.started = true;
        ram.work = false;
        ram.state = 0;
        ram.enemies.clear();
        ram.enemy_models.clear();
        ram.watch.clear();
        return true;
    }
    if (command == "ONFREEZE" || command == "OFFREEZE") {
        if (ram.work) ram.freeze = command == "ONFREEZE";
        return true;
    }
    if (command == "PERFECTDROP") return true; // item drops are not reconstructed
    return false;
}

// 802FC9FC: FIND_SPACE a free RAM of the enemy model, place it on the next
// generation point and GENERATE it with the record number and maximum HP.
int GeneraterManager::spawn(Engine& engine, GeneRam& ram, int count) {
    const auto points = vec3_list(ram.data.ref(5));
    const auto turns = float_list(ram.data.ref(6));
    if (points.empty()) return 0;
    int spawned = 0;
    for (; spawned < count; ++spawned) {
        std::string model = ram.enemy_name;
        int index = -1;
        if (!ram.shuffled) {
            Packet find;
            engine.messenger().send(engine, "MODELCTRL", ram.enemy_name, -1, "FIND_SPACE", find);
            if (find.ints.empty()) break;
            index = find.ints[0];
        } else {
            // Randomizer: the drawn family; with all its RAMs out the next
            // pool families in order, then the generator's own.
            const std::string wanted = draw_model(engine, ram);
            std::vector<std::string> order{wanted};
            const auto at = std::find(pool_.begin(), pool_.end(), wanted);
            for (std::size_t k = 1; at != pool_.end() && k < pool_.size(); ++k)
                order.push_back(pool_[(std::size_t(at - pool_.begin()) + k) % pool_.size()]);
            order.push_back(ram.enemy_name);
            for (const auto& candidate : order) {
                Packet find;
                engine.messenger().send(engine, "MODELCTRL", candidate, -1, "FIND_SPACE", find);
                if (find.ints.empty()) continue;
                model = candidate;
                index = find.ints[0];
                break;
            }
            if (index < 0) break;
        }
        ram.enemies.push_back(index);
        ram.enemy_models.push_back(model);
        const auto point = std::size_t(ram.point) % points.size();
        Vec3f position = points[point];
        if (!ram.data.boolean(4)) {
            const auto base = ram.data.vec3(3).value_or(Vec3f{});
            position = {base.x + position.x, base.y + position.y, base.z + position.z};
        }
        const float turn = point < turns.size() ? turns[point] * kRadiansToDegrees : 0.0f;
        std::string seq(ram.data.str(8));
        // A swapped enemy without the generator's entry sequence enters
        // with SQ_ENTR, which every family has.
        if (model != ram.enemy_name && !seq.empty())
            if (auto* models = engine.get<ModelCtrlManager>(); models && !models->has_sequence(model, seq)) seq.clear();
        ram.watch.push_back({position.y, 0});
        Packet generate;
        generate.vectors = {position, Vec3f{0, turn, 0}};
        generate.strings.emplace_back(seq.empty() ? std::string("SQ_ENTR") : seq);
        generate.ints = {ram.enemy_record, ram.enemy_max_hp};
        engine.messenger().send(engine, "MODELCTRL", model, index, "GENERATE", generate);
        char at[64];
        std::snprintf(at, sizeof(at), " at (%.0f, %.0f, %.0f)", position.x, position.y, position.z);
        engine.log("generator " + std::string(ram.data.str(2)) + " spawns " + model + "#" + std::to_string(index) + at);
        ram.point = int((point + 1) % points.size());
    }
    return spawned;
}

// 802FCC44: forget enemies that are no longer generated; a defeated one drops
// from the player-count entry's item list (the last enemy of a finished wave
// from the "last" list) for the current difficulty.
void GeneraterManager::refresh(Engine& engine, GeneRam& ram, bool last_pass) {
    auto* models = engine.get<ModelCtrlManager>();
    for (std::size_t i = 0; i < ram.enemies.size();) {
        const std::string& name = i < ram.enemy_models.size() ? ram.enemy_models[i] : ram.enemy_name;
        auto* model = models ? models->find(name, ram.enemies[i]) : nullptr;
        if (model && model->generated) {
            // Randomizer watchdog (shuffled generators only): a swapped enemy
            // that fell or never lands is erased and counts as defeated
            // without a drop.
            if (ram.shuffled && name != ram.enemy_name && i < ram.watch.size() && model->node) {
                // (The generator's own family belongs on its points; an
                // enemy standing on a floor, a moving one included, is fine.)
                auto& watch = ram.watch[i];
                watch.airborne = model->on_ground ? 0 : watch.airborne + 1;
                if (watch.airborne > kWatchAirborne || (watch.airborne > 0 && model->node->matrix(3, 1) < watch.spawn_y - kWatchDrop)) {
                    engine.log("randomizer: watchdog removes " + name + "#" + std::to_string(ram.enemies[i]) + " of " +
                               std::string(ram.data.str(2)));
                    Packet p;
                    engine.messenger().send(engine, "MODELCTRL", name, ram.enemies[i], "SQ_ERAS", p);
                    forget(ram, i);
                    ++watchdog_removals_;
                    continue;
                }
            }
            ++i;
            continue;
        }
        if (model && model->hp <= 0 && model->node) {
            const auto& m = model->node->matrix;
            drop(engine, ram, Vec3f{m(3, 0), m(3, 1), m(3, 2)}, last_pass && ram.enemies.size() == 1);
        }
        forget(ram, i);
    }
}

void GeneraterManager::forget(GeneRam& ram, std::size_t member) {
    ram.enemies.erase(ram.enemies.begin() + std::ptrdiff_t(member));
    if (member < ram.enemy_models.size()) ram.enemy_models.erase(ram.enemy_models.begin() + std::ptrdiff_t(member));
    if (member < ram.watch.size()) ram.watch.erase(ram.watch.begin() + std::ptrdiff_t(member));
}

// Randomizer (enemy shuffle), made at the stage's first wave: the pool (the
// shuffleable families the stage loaded), the families that stay (bosses,
// 1-RAM mid-bosses, a family with a ded_<family>.lua here) and the
// generators that stay: every one a stage script starts next to an e_ / f_
// model name (intro and demo fights wait for that model).
void GeneraterManager::prepare_shuffle(Engine& engine) {
    shuffle_ready_ = true;
    pool_.clear();
    fixed_generators_.clear();
    fixed_families_.clear();
    own_families_.clear();
    if (!engine.cheats().enemy_shuffle() || randomizer::fixed_stage(engine.stage_name())) return;
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    std::map<std::string, int, std::less<>> counts;
    std::set<std::string, std::less<>> named; // every e_ / f_ model info of the stage
    for (const auto& model : models->rams()) {
        if (model->info_name.starts_with("e_") || model->info_name.starts_with("f_")) named.insert(model->info_name);
        if (model->info_name.starts_with("e_") && model->category == "ENEMY") ++counts[model->info_name];
    }
    // The families the stage's generators use and those only story
    // generators (below) use.
    std::map<std::string, std::vector<std::string>, std::less<>> generators_of;
    if (auto* db = engine.get<DatabaseManager>())
        for (const auto& ram : rams_)
            if (ram->data) generators_of[db->text("enemy", ram->data.i32(7), "MODEL")].emplace_back(ram->data.str(2));
    std::vector<std::string> present;
    for (const auto& [name, count] : counts) {
        // A family that cannot be hurt (no hit script: a wave of it could
        // never be cleared) stays out too.
        if (randomizer::boss_family(name) || count < 2 || engine.has_script("ded_" + name + ".lua") || !engine.has_script("hit_" + name + ".lua"))
            fixed_families_.insert(name);
        else present.push_back(name);
    }
    for (const auto& name : named)
        if (!counts.contains(name)) fixed_families_.insert(name);
    // The families the stage itself loaded (RAMs not from a donor stage): a
    // generator of any other family finds no RAM in the original (FIND_SPACE)
    // and never spawns, so it stays dormant here too.
    for (const auto& [name, count] : counts)
        if (!engine.randomizer_donor(name)) own_families_.insert(name);
    // The pool the stage loader made (with the donor families it loaded),
    // else the stage's own families.
    const auto seed = randomizer::sub_seed(engine.cheats().rand_seed, "enemies");
    const auto pool = !engine.randomizer_pool().empty() ? engine.randomizer_pool()
                                                         : randomizer::stage_pool(seed, engine.stage_name(), present, 0);
    for (const auto& name : pool)
        if (counts.contains(name) && !fixed_families_.contains(name)) pool_.push_back(name);
    // Lua 4 chunks keep their string constants NUL-terminated.
    engine.for_each_script([&](std::string_view, std::string_view chunk) {
        bool model_named = false;
        for (const auto& name : named)
            if (chunk.find(name + std::string(1, '\0')) != std::string_view::npos) model_named = true;
        if (!model_named) return;
        for (const auto& ram : rams_) {
            if (!ram->data) continue;
            const std::string generator(ram->data.str(2));
            if (chunk.find(generator + std::string(1, '\0')) != std::string_view::npos) fixed_generators_.insert(generator);
        }
    });
    // A family only story generators use here (an intro or demo fight's)
    // joins no other wave.
    for (const auto& [name, generators] : generators_of)
        if (!generators.empty() && std::all_of(generators.begin(), generators.end(), [&](const std::string& g) { return fixed_generators_.contains(g); })) {
            fixed_families_.insert(name);
            pool_.erase(std::remove(pool_.begin(), pool_.end(), name), pool_.end());
        }
    std::string text = "randomizer: pool of " + engine.stage_name() + ":";
    for (const auto& name : pool_) text += " " + name;
    if (!fixed_generators_.empty()) {
        text += "; fixed generators:";
        for (const auto& name : fixed_generators_) text += " " + name;
    }
    std::string dormant_families;
    int dormant = 0;
    for (const auto& [name, generators] : generators_of)
        if (name.starts_with("e_") && !own_families_.contains(name)) {
            dormant_families += " " + name;
            dormant += int(generators.size());
        }
    if (dormant != 0) text += "; dormant generators: " + std::to_string(dormant) + " of" + dormant_families;
    engine.log(text);
}

bool GeneraterManager::shuffle_generator(Engine& engine, const GeneRam& ram) const {
    if (!engine.cheats().enemy_shuffle() || pool_.size() < 2 || !ram.data) return false;
    if (!ram.enemy_name.starts_with("e_") || fixed_families_.contains(ram.enemy_name)) return false;
    // A dormant generator (no RAM of its family that the stage loaded
    // itself) stays dormant, and a boss family's record never goes to a
    // swapped enemy.
    if (!own_families_.contains(ram.enemy_name) || randomizer::boss_family(ram.enemy_name)) return false;
    return !fixed_generators_.contains(ram.data.str(2));
}

// The family of the next enemy: one per wave (drawn when the wave starts) or
// one per enemy, from the seed, the stage, the generator and its draw count.
std::string GeneraterManager::draw_model(Engine& engine, GeneRam& ram) {
    if (engine.cheats().rand_enemies == 2 && !ram.wave_model.empty()) return ram.wave_model;
    const auto seed = randomizer::sub_seed(engine.cheats().rand_seed, "enemies");
    const std::string name = engine.stage_name() + "/" + std::string(ram.data.str(2));
    return pool_[std::size_t(randomizer::draw(seed, name, ram.draws++) % pool_.size())];
}

int GeneraterManager::randomized_record(Engine& engine, const std::string& table, int record) {
    auto* db = engine.get<DatabaseManager>();
    if (!db) return record;
    std::vector<int> rows;
    for (int r = 0, n = db->rows(table); r < n; ++r)
        if (!db->text(table, r, "NAME").empty()) rows.push_back(r);
    const auto at = std::find(rows.begin(), rows.end(), record);
    if (at == rows.end() || rows.size() < 2) return record;
    // splitmix64 from the seed and the table name: the same seed gives the
    // same game.
    std::uint64_t state = engine.cheats().drop_seed;
    for (const char c : table) state = state * 131 + std::uint8_t(c);
    auto next = [&] {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    auto shuffled = rows;
    for (std::size_t i = shuffled.size() - 1; i > 0; --i) std::swap(shuffled[i], shuffled[std::size_t(next() % (i + 1))]);
    return shuffled[std::size_t(at - rows.begin())];
}

void GeneraterManager::drop(Engine& engine, GeneRam& ram, const Vec3f& position, bool last) {
    const auto players = ram.data.list(14);
    if (players.empty()) return;
    const auto& counts = players[std::size_t(std::clamp(ram.player_count, 1, int(players.size())) - 1)];
    if (!counts) return;
    const auto lists = counts.list(last ? 5 : 4);
    int difficulty = 0;
    if (auto* sys = engine.get<GameSystem>()) difficulty = sys->difficulty;
    if (lists.empty()) return;
    const auto& data = lists[std::size_t(std::clamp(difficulty, 0, int(lists.size()) - 1))];
    if (!data) return;
    int roll = int(fight::random() % 10000u);
    for (const auto& one : data.list(3)) {
        if (!one) continue;
        roll -= one.i32(5);
        if (roll > 0) continue;
        const std::string table(one.str(2));
        int record = one.i32(3);
        int table_id = -1;
        for (int t = 0; t < 7; ++t)
            if (table == GameSystem::item_tables[t]) table_id = t;
        if (table_id < 0) return;
        // Randomizer option: drops swap within their table by a seeded
        // permutation; key items (impItem) stay.
        if (engine.cheats().drop_seed && table_id != 2) record = randomized_record(engine, table, record);
        static const char* models[7] = {nullptr, nullptr, "g_000key", "g_000weapon", "g_000armor", "g_000armor", "g_000access"};
        std::string model = models[table_id] ? models[table_id] : std::string();
        if (model.empty())
            if (auto* db = engine.get<DatabaseManager>()) model = db->text(table, record, "MODEL");
        if (model.empty()) return;
        Packet find;
        engine.messenger().send(engine, "MODELCTRL", model, -1, "FIND_SPACE", find);
        if (find.ints.empty()) return;
        Packet generate;
        generate.vectors.push_back(position);
        generate.ints = {record, table_id};
        engine.messenger().send(engine, "MODELCTRL", model, find.ints[0], "GENERATE", generate);
        engine.log("drop " + table + " #" + std::to_string(record) + " (" + model + ")");
        return;
    }
}

// Test tool (DW4_RANDOMIZER_SWEEP=1): instead of the stage's own waves,
// every e_ generator in turn spawns its wave at once (whatever its start
// mode), lets its enemies enter for 150 updates and erases them; one line per
// generator reports the models it got and any problem (none spawned, a model
// without a hit script, one the watchdog removed, one not generated). With
// the randomizer on it shows every shuffled wave of a stage spawning.
void GeneraterManager::sweep(Engine& engine) {
    int live_players = 0;
    if (auto* models = engine.get<ModelCtrlManager>())
        for (int slot = 0; slot < 4; ++slot) live_players += models->player(slot) ? 1 : 0;
    if (live_players == 0) return;
    if (++sweep_wait_ < 60) return;
    auto* models = engine.get<ModelCtrlManager>();
    auto* db = engine.get<DatabaseManager>();
    while (sweep_index_ < rams_.size()) {
        auto& ram = *rams_[sweep_index_];
        if (ram.data && db && db->text("enemy", ram.data.i32(7), "MODEL").starts_with("e_")) break;
        ++sweep_index_;
    }
    if (sweep_index_ >= rams_.size()) {
        if (!sweep_reported_) {
            engine.log("sweep done " + engine.stage_name() + ": " + std::to_string(sweep_count_) + " generators, " +
                       std::to_string(sweep_failures_) + " with problems");
            sweep_reported_ = true;
        }
        return;
    }
    auto& ram = *rams_[sweep_index_];
    if (!ram.work) {
        ram.started = true;
        start_work(engine, ram, live_players);
        const auto players = ram.data.list(14);
        const auto& counts = players.empty() ? ObjectView{} : players[std::size_t(std::clamp(ram.player_count, 1, int(players.size())) - 1)];
        sweep_wanted_ = std::max(1, counts ? counts.i32(3) : 1);
        sweep_got_ = spawn(engine, ram, sweep_wanted_);
        sweep_models_ = ram.enemy_models;
        sweep_watchdog_ = 0;
        sweep_wait_ = 60;
        return;
    }
    const int before = watchdog_removals_;
    refresh(engine, ram);
    sweep_watchdog_ += watchdog_removals_ - before;
    if (sweep_wait_ < 60 + 150) return;
    std::string problems;
    if (sweep_got_ == 0) problems += " none spawned";
    std::set<std::string, std::less<>> seen;
    for (const auto& model : sweep_models_) {
        if (!seen.insert(model).second) continue;
        if (!engine.has_script("hit_" + model + ".lua")) problems += " no hit script for " + model;
        // (A family the stage loads itself plays its cues as the original
        // does; only a donor family's sounds are the randomizer's.)
        if (auto* sound = engine.get<SoundManager>(); sound && sound->donor_set(model)) {
            const auto missing = sound->missing_cues(engine, model);
            if (missing)
                for (const auto& cue : *missing) problems += " sound " + cue + " of " + model;
        }
    }
    for (std::size_t i = 0; i < ram.enemies.size(); ++i) {
        const auto* model = models ? models->find(ram.enemy_models[i], ram.enemies[i]) : nullptr;
        if (!model || !model->generated || !model->seq) problems += " not running " + ram.enemy_models[i];
    }
    if (sweep_watchdog_ > 0) problems += " watchdog removed " + std::to_string(sweep_watchdog_);
    std::string text = "sweep " + engine.stage_name() + " " + std::string(ram.data.str(2)) + " " + ram.enemy_name + (ram.shuffled ? " shuffled" : "") +
                       " want " + std::to_string(sweep_wanted_) + " got " + std::to_string(sweep_got_) + ":";
    for (const auto& model : sweep_models_) text += " " + model;
    if (!problems.empty()) text += (ram.shuffled ? " | PROBLEM" : " | original:") + problems;
    engine.log(text);
    ++sweep_count_;
    sweep_failures_ += problems.empty() || !ram.shuffled ? 0 : 1;
    for (std::size_t i = 0; i < ram.enemies.size(); ++i) {
        Packet p;
        engine.messenger().send(engine, "MODELCTRL", ram.enemy_models[i], ram.enemies[i], "SQ_ERAS", p);
    }
    ram.enemies.clear();
    ram.enemy_models.clear();
    ram.watch.clear();
    ram.started = ram.work = false;
    ram.done = true;
    ram.state = 0;
    ++sweep_index_;
    sweep_wait_ = 30;
}

// Test tool (DW4_RANDOMIZER_SWEEP=static, with DW4_RANDOMIZER): the static
// check of a stage's shuffled waves, once it runs with a player and no wave
// starts. For every e_ generator whether it is shuffled (else why it stays);
// for a shuffled one the floor under each spawn point (none, a pit's respawn
// mat, quicksand: the generator's own family lands there as well) and each
// pool family against the own one: its land cylinder (HITLANDDELIVER width,
// the step it climbs, the drop it walks down), its body (above the swing:
// body boxes from 9 above the root), its hit script, and whether it has the
// generator's entry sequence (else SQ_ENTR). PROBLEM marks what a stand-in
// would meet and the own family not: no hit script, hovering, a smaller
// climb, a deeper drop, no land cylinder.
void GeneraterManager::static_check(Engine& engine) {
    if (sweep_reported_) return;
    auto* models = engine.get<ModelCtrlManager>();
    auto* db = engine.get<DatabaseManager>();
    auto* hitland = engine.get<HitLandManager>();
    if (!models || !db || !hitland) return;
    int live_players = 0;
    for (int slot = 0; slot < 4; ++slot) live_players += models->player(slot) ? 1 : 0;
    if (live_players == 0 || ++sweep_wait_ < 60) return;
    sweep_reported_ = true;
    if (!shuffle_ready_) prepare_shuffle(engine);
    std::vector<MatCtrlManager::MatBox> boxes;
    if (auto* mats = engine.get<MatCtrlManager>()) boxes = mats->mat_boxes();
    auto hazard_at = [&](const Vec3f& p) -> std::string {
        for (const auto& b : boxes) {
            if (p.x < b.min.x || p.x > b.max.x || p.z < b.min.z || p.z > b.max.z || p.y < b.min.y - 5.0f || p.y > b.max.y + 10.0f) continue;
            const std::string script = b.name + ".lua";
            if (engine.script_mentions(script, "ADDHP") && engine.script_mentions(script, "REMOVE")) return "pit";
            if (engine.script_mentions(script, "QuickSand") || engine.script_mentions(script, "QUICKSAND")) return "sand";
        }
        return {};
    };
    struct Traits {
        std::optional<HitLandManager::DeliveryShape> shape;
        bool hovers{}, hit{};
    };
    auto traits = [&](const std::string& family) {
        Traits t;
        t.shape = hitland->delivery_shape(family, 0);
        t.hit = engine.has_script("hit_" + family + ".lua");
        for (const auto& e : models->rams())
            if (e->info_name == family && e->hit && !e->hit->boxes.empty()) {
                float bottom = std::numeric_limits<float>::infinity();
                for (const auto& [lo, hi] : e->hit->boxes) bottom = std::min({bottom, lo.y, hi.y});
                t.hovers = bottom > 9.0f;
                break;
            }
        return t;
    };
    int generators = 0, problems = 0;
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.data) continue;
        const std::string own = db->text("enemy", ram.data.i32(7), "MODEL");
        if (!own.starts_with("e_")) continue;
        ++generators;
        ram.enemy_name = own; // (no wave runs in this mode)
        const std::string name(ram.data.str(2));
        std::string text = "check " + engine.stage_name() + " " + name + " " + own;
        if (!shuffle_generator(engine, ram)) {
            const char* why = !engine.cheats().enemy_shuffle() ? "shuffle off"
                              : fixed_families_.contains(own) ? "family stays"
                              : !own_families_.contains(own)  ? "dormant"
                              : fixed_generators_.contains(name) ? "story generator"
                              : pool_.size() < 2               ? "no pool"
                                                               : "stays";
            engine.log(text + " stays (" + why + ")");
            continue;
        }
        const auto points = vec3_list(ram.data.ref(5));
        const auto base = ram.data.vec3(3).value_or(Vec3f{});
        int none = 0, pits = 0, sand = 0;
        for (auto p : points) {
            if (!ram.data.boolean(4)) p = {base.x + p.x, base.y + p.y, base.z + p.z};
            const auto land = hitland->world().check_land({p.x, p.y + 1.0f, p.z}, 3.0f, 2.0f, 400.0f);
            if (!land.found) {
                ++none;
                continue;
            }
            const auto hazard = hazard_at(land.position);
            pits += hazard == "pit" ? 1 : 0;
            sand += hazard == "sand" ? 1 : 0;
        }
        text += " shuffled, " + std::to_string(points.size()) + " points";
        if (none) text += ", " + std::to_string(none) + " over no floor";
        if (pits) text += ", " + std::to_string(pits) + " over a pit";
        if (sand) text += ", " + std::to_string(sand) + " on quicksand";
        text += ":";
        const auto mine = traits(own);
        const std::string seq(ram.data.str(8));
        std::string problem;
        for (const auto& family : pool_) {
            if (family == own) continue;
            const auto t = traits(family);
            text += " " + family;
            if (!t.hit) problem += " no hit script for " + family;
            if (t.hovers && !mine.hovers) problem += " " + family + " hovers above the swing";
            if (!t.shape) problem += " no land cylinder for " + family;
            else if (mine.shape) {
                if (t.shape->up < mine.shape->up) problem += " " + family + " climbs less";
                if (t.shape->down > mine.shape->down) problem += " " + family + " walks off deeper drops";
                if (t.shape->width > mine.shape->width) {
                    char wide[32];
                    std::snprintf(wide, sizeof wide, "(%.2f wide)", t.shape->width);
                    text += wide;
                }
            }
            if (!seq.empty() && !models->has_sequence(family, seq)) text += "(enters with SQ_ENTR)";
        }
        if (!problem.empty()) {
            text += " | PROBLEM" + problem;
            ++problems;
        }
        engine.log(text);
    }
    engine.log("check done " + engine.stage_name() + ": " + std::to_string(generators) + " generators, " + std::to_string(problems) +
               " with problems");
}

// 802FCFA8: a wave starts (the generator's work is allocated): the enemy
// comes from DATABASE enemy.
void GeneraterManager::start_work(Engine& engine, GeneRam& ram, int live_players) {
    ram.work = true;
    ram.freeze = ram.system_freeze = false;
    ram.player_count = live_players;
    ram.remaining = 0;
    ram.enemies.clear();
    ram.enemy_models.clear();
    ram.watch.clear();
    // _genePoint lives in the beGeneraterInfoWork allocated here, so
    // a restarted generator spawns from its first point again.
    ram.point = 0;
    ram.enemy_record = ram.data.i32(7);
    if (auto* db = engine.get<DatabaseManager>()) {
        ram.enemy_name = db->text("enemy", ram.enemy_record, "MODEL");
        ram.enemy_max_hp = db->number("enemy", ram.enemy_record, "HPMAX").value_or(1);
    }
    // Randomizer: whether this wave's enemies are swapped (and, one
    // family per wave, which one).
    if (engine.cheats().enemy_shuffle()) {
        if (!shuffle_ready_) prepare_shuffle(engine);
        ram.shuffled = shuffle_generator(engine, ram);
        ram.wave_model.clear();
        if (ram.shuffled && engine.cheats().rand_enemies == 2) {
            const auto seed = randomizer::sub_seed(engine.cheats().rand_seed, "enemies");
            const std::string name = engine.stage_name() + "/" + std::string(ram.data.str(2)) + "/wave";
            ram.wave_model = pool_[std::size_t(randomizer::draw(seed, name, std::uint64_t(ram.clears)) % pool_.size())];
        }
    } else {
        ram.shuffled = false;
    }
}

void GeneraterManager::update(Engine& engine) {
    if (static const char* const sweeping = std::getenv("DW4_RANDOMIZER_SWEEP"); sweeping) {
        if (std::string_view(sweeping) == "static") static_check(engine);
        else sweep(engine);
        return;
    }
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.started || !ram.data) continue;
        if (!ram.work) {
            // 802FCFA8: requires the players.
            int live_players = 0;
            if (auto* models = engine.get<ModelCtrlManager>())
                for (int slot = 0; slot < 4; ++slot) live_players += models->player(slot) ? 1 : 0;
            if (live_players == 0) continue;
            start_work(engine, ram, live_players);
        }
        if (ram.freeze || ram.system_freeze) {
            refresh(engine, ram);
            continue;
        }
        const auto players = ram.data.list(14);
        const auto& counts = players.empty() ? ObjectView{} : players[std::size_t(std::clamp(ram.player_count, 1, int(players.size())) - 1)];
        const int max_count = counts ? counts.i32(3) : 1;
        const std::string start_mode(ram.data.str(9));
        const std::string fill_mode(ram.data.str(11));
        switch (ram.state) {
        case 0: {
            bool go = false;
            if (start_mode.starts_with('d')) go = true;
            else if (start_mode.starts_with('t')) {
                // "time" (802FD21C): the first pass makes the named timer
                // "BOOT_TIMER" (8031E138 / 8031DE44, end (int64)(1e9f *
                // _startData / 60f)); later passes check it on the 2^18 ns
                // quantised clock with a strict '<' (8031E238) and remove it
                // once it ran out (8031E8BC).
                if (!ram.boot_timer) timer_start(ram.boot_timer.emplace(), engine.time(), frames_to_time(ram.data.f32(10)));
                else if (timer_expired(*ram.boot_timer, engine.time())) {
                    go = true;
                    ram.boot_timer.reset();
                }
            } else if (start_mode.starts_with('r')) {
                // "radar" (802FD288): the nearest live player's squared distance to
                // the base position is at most _startData.
                const auto base = ram.data.vec3(3).value_or(Vec3f{});
                if (auto* models = engine.get<ModelCtrlManager>())
                    for (int slot = 0; slot < 4; ++slot)
                        if (const auto* player = models->player(slot); player && player->node) {
                            const auto& m = player->node->matrix;
                            const float dx = m(3, 0) - base.x, dy = m(3, 1) - base.y, dz = m(3, 2) - base.z;
                            if (dx * dx + dy * dy + dz * dz <= ram.data.f32(10)) go = true;
                        }
            }
            if (!go) break;
            ram.state = 1;
            [[fallthrough]];
        }
        case 1: {
            const int total = counts ? counts.i32(2) : 1;
            ram.remaining = total - spawn(engine, ram, max_count);
            ram.state = 2;
            if (ram.remaining <= 0) ram.state = 3;
            else if (fill_mode.starts_with('t')) {
                // 0x802FD380: 8031DE44 (re)starts "GENE_TIMER" with _fillData frames.
                if (!ram.gene_timer) ram.gene_timer.emplace();
                timer_start(*ram.gene_timer, engine.time(), frames_to_time(ram.data.f32(12)));
            }
            break;
        }
        case 2:
            refresh(engine, ram);
            if (fill_mode.starts_with('w')) {
                if (!ram.enemies.empty()) break;
                ram.remaining -= spawn(engine, ram, std::min(max_count, ram.remaining));
            } else if (fill_mode.starts_with('a')) {
                if (int(ram.enemies.size()) == max_count) break;
                ram.remaining -= spawn(engine, ram, 1);
            } else if (fill_mode.starts_with('t')) {
                // 0x802FD484: one enemy each time "GENE_TIMER" runs out (a
                // missing timer counts as run out); the last one removes it
                // (0x802FD4E0), otherwise it restarts (0x802FD510).
                if (ram.gene_timer && !timer_expired(*ram.gene_timer, engine.time())) break;
                ram.remaining -= spawn(engine, ram, 1);
                if (ram.remaining <= 0) ram.gene_timer.reset();
                else {
                    if (!ram.gene_timer) ram.gene_timer.emplace();
                    timer_start(*ram.gene_timer, engine.time(), frames_to_time(ram.data.f32(12)));
                }
            }
            if (ram.remaining <= 0) ram.state = 3;
            break;
        case 3:
            refresh(engine, ram, true);
            if (!ram.enemies.empty()) break;
            // 802FD538: the wave is cleared; run _callLua and finish.
            ram.started = false;
            ram.work = false;
            ram.done = true;
            ram.state = 0;
            ++ram.clears;
            if (const auto lua = ram.data.str(13); !lua.empty()) {
                engine.log("generator " + std::string(ram.data.str(2)) + " cleared -> " + std::string(lua));
                engine.run_script(lua);
            }
            break;
        default: break;
        }
    }
}

} // namespace dw4::engine
