#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::engine {
namespace {
constexpr float kRadiansToDegrees = 57.2958f; // 80425EF4

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
        for (int index : ram.enemies) {
            Packet p;
            engine.messenger().send(engine, "MODELCTRL", ram.enemy_name, index, "SQ_ERAS", p);
        }
        ram.remaining += int(ram.enemies.size());
        ram.enemies.clear();
        if (ram.remaining != 0 && ram.state == 3) ram.state = 2;
        return true;
    }
    if (command == "RESET") {
        ram.started = false;
        ram.work = false;
        ram.done = false;
        ram.state = 0;
        ram.enemies.clear();
        return true;
    }
    if (command == "RESTART") {
        ram.started = true;
        ram.work = false;
        ram.state = 0;
        ram.enemies.clear();
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
        Packet find;
        engine.messenger().send(engine, "MODELCTRL", ram.enemy_name, -1, "FIND_SPACE", find);
        if (find.ints.empty()) break;
        const int index = find.ints[0];
        ram.enemies.push_back(index);
        const auto point = std::size_t(ram.point) % points.size();
        Vec3f position = points[point];
        if (!ram.data.boolean(4)) {
            const auto base = ram.data.vec3(3).value_or(Vec3f{});
            position = {base.x + position.x, base.y + position.y, base.z + position.z};
        }
        const float turn = point < turns.size() ? turns[point] * kRadiansToDegrees : 0.0f;
        const auto seq = ram.data.str(8);
        Packet generate;
        generate.vectors = {position, Vec3f{0, turn, 0}};
        generate.strings.emplace_back(seq.empty() ? std::string("SQ_ENTR") : std::string(seq));
        generate.ints = {ram.enemy_record, ram.enemy_max_hp};
        engine.messenger().send(engine, "MODELCTRL", ram.enemy_name, index, "GENERATE", generate);
        engine.log("generator " + std::string(ram.data.str(2)) + " spawns " + ram.enemy_name + "#" + std::to_string(index));
        ram.point = int((point + 1) % points.size());
    }
    return spawned;
}

// 802FCC44 (subset): forget enemies that are no longer generated. Item drops
// at the defeat position are not reconstructed.
void GeneraterManager::refresh(Engine& engine, GeneRam& ram) {
    auto* models = engine.get<ModelCtrlManager>();
    std::erase_if(ram.enemies, [&](int index) {
        auto* model = models ? models->find(ram.enemy_name, index) : nullptr;
        return !model || !model->generated;
    });
}

void GeneraterManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.started || !ram.data) continue;
        if (!ram.work) {
            // 802FCFA8: requires the players; the enemy comes from DATABASE enemy.
            int live_players = 0;
            if (auto* models = engine.get<ModelCtrlManager>())
                for (int slot = 0; slot < 4; ++slot) live_players += models->player(slot) ? 1 : 0;
            if (live_players == 0) continue;
            ram.work = true;
            ram.freeze = ram.system_freeze = false;
            ram.player_count = live_players;
            ram.remaining = 0;
            ram.enemies.clear();
            ram.enemy_record = ram.data.i32(7);
            if (auto* db = engine.get<DatabaseManager>()) {
                ram.enemy_name = db->text("enemy", ram.enemy_record, "MODEL");
                ram.enemy_max_hp = db->number("enemy", ram.enemy_record, "HPMAX").value_or(1);
            }
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
                if (ram.timer_end < 0) ram.timer_end = engine.time() + frames_to_time(ram.data.f32(10));
                else if (engine.time() >= ram.timer_end) { go = true; ram.timer_end = -1; }
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
            else if (fill_mode.starts_with('t')) ram.timer_end = engine.time() + frames_to_time(ram.data.f32(12));
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
                if (engine.time() < ram.timer_end) break;
                ram.remaining -= spawn(engine, ram, 1);
                ram.timer_end = engine.time() + frames_to_time(ram.data.f32(12));
            }
            if (ram.remaining <= 0) ram.state = 3;
            break;
        case 3:
            refresh(engine, ram);
            if (!ram.enemies.empty()) break;
            // 802FD538: the wave is cleared; run _callLua and finish.
            ram.started = false;
            ram.work = false;
            ram.done = true;
            ram.state = 0;
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
