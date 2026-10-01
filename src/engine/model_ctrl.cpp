#include "dw4/engine/managers.hpp"
#include "dw4/motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace dw4::engine {
namespace {
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

bool contains(Node* root, Node* target, int depth = 0) {
    if (!root || depth > 64) return false;
    if (root == target) return true;
    for (auto* child : root->children) if (contains(child, target, depth + 1)) return true;
    return false;
}

std::string category_for_kind(std::string_view kind) {
    if (kind == "KIND_PLAYER") return "PLAYER";
    if (kind == "KIND_PLAYER2") return "PLAYER2";
    if (kind == "KIND_ENEMY") return "ENEMY";
    if (kind == "KIND_ITEM") return "ITEM";
    if (kind == "KIND_EFFECT") return "EFFECT";
    if (kind == "KIND_PBULLET") return "PBULLET";
    if (kind == "KIND_EBULLET") return "EBULLET";
    return "MODELCTRL";
}
} // namespace

// Translation + X/Y/Z rotation in degrees (801298F0 order), row-vector layout.
Mat4 placement_matrix(const Vec3f& t, const Vec3f& r) {
    auto rot = [](int axis, float angle) {
        Mat4 q;
        const float c = std::cos(angle), s = std::sin(angle);
        if (axis == 0) { q(1, 1) = c; q(1, 2) = s; q(2, 1) = -s; q(2, 2) = c; }
        else if (axis == 1) { q(0, 0) = c; q(0, 2) = -s; q(2, 0) = s; q(2, 2) = c; }
        else { q(0, 0) = c; q(0, 1) = s; q(1, 0) = -s; q(1, 1) = c; }
        return q;
    };
    Mat4 m;
    if (r.x != 0) m = m * rot(0, r.x * kDegrees);
    if (r.y != 0) m = m * rot(1, r.y * kDegrees);
    if (r.z != 0) m = m * rot(2, r.z * kDegrees);
    m(3, 0) = t.x;
    m(3, 1) = t.y;
    m(3, 2) = t.z;
    return m;
}

// ===========================================================================
// MODELCTRL (8030D910 dispatch, 8030C3AC command loop, 8030BDB8 waits)
// ===========================================================================
bool ModelCtrlManager::accepts(const ObjectView& info) const { return info.type() == "beModelCtrlInfo"; }

void ModelCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    // Decode SEQ once per info.
    auto sequences = std::make_unique<std::vector<Sequence>>();
    for (const auto& data : info.list(5)) {
        if (!data || data.str(2) != "SEQ") continue;
        for (const auto& seq : data.list(4)) {
            if (!seq) continue;
            Sequence s;
            s.name = std::string(seq.str(2));
            for (const auto& command : seq.list(3)) if (command) s.commands.push_back(command);
            sequences->push_back(std::move(s));
        }
        break;
    }
    auto* seq_ptr = sequences.get();
    const void* info_key = reinterpret_cast<const void*>(std::uintptr_t(info.reference()) ^ reinterpret_cast<std::uintptr_t>(info.document()));
    sequences_[info_key] = std::move(sequences);
    // 803083CC: the first HIT record selects the bullet/talk categories.
    HitBody* hit = nullptr;
    for (const auto& data : info.list(5)) {
        if (!data || data.str(2) != "HIT") continue;
        auto body = std::make_unique<HitBody>();
        body->player_bullet = data.boolean(4);
        body->enemy_bullet = data.boolean(5);
        body->player_talk = data.boolean(7);
        for (const auto& box : data.list(6))
            if (box) body->boxes.emplace_back(box.vec3(2).value_or(Vec3f{}), box.vec3(3).value_or(Vec3f{}));
        hit = body.get();
        hit_bodies_[info_key] = std::move(body);
        break;
    }
    // "AIH" data: the homing AI of bullets.
    Homing* homing = nullptr;
    for (const auto& data : info.list(5)) {
        if (!data || data.str(2) != "AIH") continue;
        auto h = std::make_unique<Homing>();
        h->pitch = data.boolean(4);
        h->yaw = data.boolean(5);
        h->delay = data.f32(6);
        h->interval = data.f32(7);
        h->range2 = data.f32(8);
        h->pitch_rate = data.f32(9);
        h->yaw_rate = data.f32(10);
        h->steps = data.i32(11);
        homing = h.get();
        homings_[info_key] = std::move(h);
        break;
    }
    // "AIM" data (beModelCtrlInfoDataAIM [4]): the AI maps selected by AIMAP.
    std::vector<ObjectView>* ai_maps = nullptr;
    for (const auto& data : info.list(5)) {
        if (!data || data.str(2) != "AIM") continue;
        auto maps = std::make_unique<std::vector<ObjectView>>();
        for (const auto& map : data.list(4)) if (map) maps->push_back(map);
        ai_maps = maps.get();
        ai_maps_[info_key] = std::move(maps);
        break;
    }
    const std::string category = category_for_kind(info.str(7));
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<ModelRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->category = category;
        ram->sequences = seq_ptr;
        ram->ai_maps = ai_maps;
        ram->homing = homing;
        for (const auto& data : info.list(5))
            if (data && data.str(2) == "TRC") {
                ram->trace = engine.scenes().node(data.ref(4));
                break;
            }
        ram->is_player = category == "PLAYER";
        if (record) {
            for (const auto& data : record.list(10)) {
                if (data && data.str(2) == "ONE") {
                    ram->node = engine.scenes().node(data.ref(4));
                    break;
                }
            }
        }
        if (ram->node) ram->base_matrix = ram->node->matrix;
        engine.messenger().add_row(category, ram->info_name, ram->index, ram.get());
        // Every model kind also answers generic MODELCTRL messages.
        if (category != "MODELCTRL") engine.messenger().add_row("MODELCTRL", ram->info_name, ram->index, ram.get());
        // beNDMWMdlItem registration (803AAAC4): pickups answer DROPITEM.
        if (category == "ITEM") engine.messenger().add_row("DROPITEM", ram->info_name, ram->index, ram.get());
        ram->hit = hit;
        if (hit && !hit->boxes.empty()) {
            if (hit->player_bullet) engine.messenger().add_row("HITPLAYERBULLET", ram->info_name, ram->index, ram.get());
            if (hit->player_talk) engine.messenger().add_row("HITPLAYERTALK", ram->info_name, ram->index, ram.get());
            if (hit->enemy_bullet) engine.messenger().add_row("HITENEMYBULLET", ram->info_name, ram->index, ram.get());
        }
        rams_.push_back(std::move(ram));
    }
}

void ModelCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    ai_maps_.clear();
    item_labels_.fill(false);
    sequences_.clear();
    hit_bodies_.clear();
}

ModelCtrlManager::ModelRam* ModelCtrlManager::find(std::string_view name, int index) {
    for (auto& ram : rams_)
        if (ram->info_name == name && ram->index == index) return ram.get();
    return nullptr;
}

void ModelCtrlManager::select_sequence(Engine& engine, ModelRam& ram, std::string_view name) {
    // Sequence cleanup releases the queued hitboxes (MODEL_HITBOX) and a
    // sequence-long invulnerability; started sequence-dead effects are erased.
    for (const auto& effect : ram.effects)
        if (effect.sequence_dead && effect.started && effect.ram >= 0) {
            Packet erase;
            engine.messenger().send(engine, "MODELCTRL", effect.model, effect.ram, "SQ_ERAS", erase);
        }
    ram.effects.clear();
    ram.hitboxes.clear();
    ++ram.hitbox_generation;
    if (ram.muteki < 0) ram.muteki = 0;
    if (name == "SQ_ERAS") {
        ram.seq_name = "SQ_ERAS";
        ram.seq = nullptr;
        ram.wait_name.reset();
        return;
    }
    if (name == "SQ_ERAS2") {
        Packet packet;
        engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "NDSP", packet);
        return;
    }
    const Sequence* found = nullptr;
    if (ram.sequences)
        for (const auto& s : *ram.sequences) if (s.name == name) { found = &s; break; }
    ram.seq_name = std::string(name);
    ram.seq = found;
    ram.cursor = 0;
    ram.label_passed = false;
    ram.wait_name.reset();
    // Ordinary entry: mode/phase and PDMOVE count reset, flagged channel stops.
    ram.ctrl_mode = 0;
    ram.ctrl_phase = 0;
    ram.pdmove_set = false;
    ram.fall_check = true;
    try { if (ram.channels.allocated()) ram.channels.on_sequence_change(); } catch (const std::exception&) {}
    // Label section (SETLABEL per sequence, e.g. the weapon's "axe" for
    // SQ_ATCK1 or "common" for a damage reaction): start after that LABEL.
    const auto override = ram.labels.find(name);
    const std::string& label = override != ram.labels.end() ? override->second : ram.label;
    if (found && !label.empty()) {
        for (std::size_t i = 0; i < found->commands.size(); ++i) {
            if (found->commands[i].type() == "beModelCtrlLABEL" && found->commands[i].str(2) == label) {
                ram.cursor = i + 1;
                ram.label_passed = true;
                break;
            }
        }
    }
    if (!found) {
        engine.note("MODELCTRL sequence " + std::string(name) + " missing in " + ram.info_name);
        return;
    }
    run_commands(engine, ram);
    // 8030EA8C: the sequence-change hook (vtable +B8) checks sleep and stun.
    if (ram.is_player || ram.category == "ENEMY") status_sequence_check(engine, ram);
}

void ModelCtrlManager::run_commands(Engine& engine, ModelRam& ram) {
    for (int guard = 0; guard < 4096 && ram.seq; ++guard) {
        if (ram.cursor >= ram.seq->commands.size()) return;
        const auto& command = ram.seq->commands[ram.cursor];
        const auto& type = command.type();
        const std::string name(command.str(2));
        if (type == "beModelCtrlACTION") {
            Packet packet;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, name, packet);
            ++ram.cursor;
        } else if (type == "beModelCtrlWCHECK") {
            ram.wait_name = name;
            ++ram.cursor;
            return;
        } else if (type == "beModelCtrlJUMP") {
            select_sequence(engine, ram, name);
            return;
        } else if (type == "beModelCtrlLABEL") {
            if (ram.label_passed) return;
            ram.label_passed = true;
            ++ram.cursor;
        } else if (type == "beModelCtrlMSGBOX") {
            ++ram.cursor;
        } else if (type == "beModelCtrlCTRL") {
            // 80308EC0: exact control names.
            if (name == "onWalk") { ram.ctrl_mode = 1; ram.ctrl_phase = 0; }
            else if (name == "toWalk") { ram.ctrl_mode = 2; ram.ctrl_phase = 0; }
            else if (name == "toWait") { ram.ctrl_mode = 3; ram.ctrl_phase = 0; }
            else if (name == "isLive") { ram.ctrl_mode = 4; ram.ctrl_phase = 0; }
            else if (name == "onFall") ram.fall_check = true;
            else if (name == "noFall") ram.fall_check = false;
            ++ram.cursor;
        } else if (type == "beModelCtrlPDMOVE") {
            // 8030AEE0: Stop/Move parameter pairs.
            if (!ram.pdmove_set) { ram.pdmove = {0, 0, 0, 0}; ram.pdmove_set = true; }
            if (name == "Stop") { ram.pdmove[0] = command.f32(3); ram.pdmove[1] = command.f32(4); }
            else if (name == "Move") { ram.pdmove[2] = command.f32(3); ram.pdmove[3] = command.f32(4); }
            ++ram.cursor;
        } else if (type == "beModelCtrlMOVERL") {
            // 8030ACBC: JOINT keeps the model and bone names; ACTOR takes the
            // model's trace data (GETTRCDATA), not reconstructed yet.
            ram.follow_model.clear();
            ram.follow_bone.clear();
            ram.follow_trace = nullptr;
            if (name == "JOINT") {
                ram.follow_model = std::string(command.str(3));
                ram.follow_bone = std::string(command.str(4));
            } else {
                Packet trace;
                engine.messenger().send(engine, "MODELCTRL", std::string(command.str(3)), ram.index, "GETTRCDATA", trace);
                if (trace.objects.size() == 1) ram.follow_trace = static_cast<Node*>(trace.objects[0]);
                if (static const bool trace_log = std::getenv("DW4_TRACE_FOLLOW") != nullptr; trace_log)
                    engine.log("follow " + ram.info_name + "#" + std::to_string(ram.index) + " trace of " +
                               std::string(command.str(3)) + (ram.follow_trace ? "" : " (none)"));
            }
            ++ram.cursor;
        } else if (type == "beModelCtrlMOVE" || type == "beModelCtrlMOVEEX") {
            ram.follow_model.clear(); // 8030A71C: a new move ends the follow
            ram.follow_trace = nullptr;
            try {
                auto motion = dw4::decode_motion_command(*command.document(), command.reference());
                if (type == "beModelCtrlMOVE") {
                    // 8030A6BC: MOVE name selects channel and mode.
                    if (name == "Move") { motion.channel = 2; motion.setup.mode = dw4::MotionMode::accelerate_positive; }
                    else if (name == "Stop") { motion.channel = 2; motion.setup.mode = dw4::MotionMode::stop_positive; }
                    else if (name == "Push") { motion.channel = 2; motion.setup.mode = dw4::MotionMode::impulse_positive; }
                    else if (name == "Back") { motion.channel = 2; motion.setup.mode = dw4::MotionMode::impulse_negative; }
                    else if (name == "Jump") { motion.channel = 1; motion.setup.mode = dw4::MotionMode::impulse_positive; }
                    else if (name == "Fall") { motion.channel = 1; motion.setup.mode = dw4::MotionMode::accelerate_negative; }
                }
                ram.channels.apply(motion);
            } catch (const std::exception& error) {
                engine.note(std::string("MODELCTRL MOVE decode: ") + error.what());
            }
            ++ram.cursor;
        } else if (type == "beModelCtrlHITBOX") {
            // Set appends one beModelCtrlSCHitBox; AllClear drops them all.
            if (name == "Set") {
                ModelRam::Hitbox box;
                box.kind = command.i32(3);
                box.delay = command.f32(4);
                const float length = command.f32(5);
                box.end = length == -1.0f ? 100000000.0f : box.delay + length;
                box.offset = command.vec3(6).value_or(Vec3f{});
                box.size = command.vec3(7).value_or(Vec3f{});
                ram.hitboxes.push_back(box);
            } else if (name == "AllClear") {
                ram.hitboxes.clear();
                ++ram.hitbox_generation;
            }
            ++ram.cursor;
        } else if (type == "beModelCtrlMUTEKI") {
            // "ON" -1: invulnerable for the rest of the sequence; "OFF" ends it;
            // otherwise the frame count.
            const float frames = command.f32(3);
            ram.muteki = name == "OFF" ? 0.0f : frames;
            ++ram.cursor;
        } else if (type == "beModelCtrlLABELCTRL") {
            // The label section a later sequence starts from (like SETLABEL).
            ram.labels[std::string(command.str(3))] = name;
            ++ram.cursor;
        } else if (type == "beModelCtrlAIMAP") {
            // Select the AI map by name.
            if (ram.ai_maps)
                for (std::size_t i = 0; i < ram.ai_maps->size(); ++i)
                    if ((*ram.ai_maps)[i].str(2) == name) ram.ai_map = int(i);
            ++ram.cursor;
        } else if (type == "beModelCtrlLUACALL") {
            // 8030B8A4: BHit, WHit and FHit keep the name for after this
            // model's hitbox hits something, it meets a wall or the floor
            // (bullets erase themselves). Any other mode runs
            // "<name>_<info>.lua" now with the mode as MDLisCallName's call
            // name (e.g. "swof", "magic", "setaid").
            const auto mode = command.str(3);
            ++ram.cursor;
            if (mode == "BHit") {
                ram.lua_bhit = name;
            } else if (mode == "WHit") {
                ram.lua_whit = name;
            } else if (mode == "FHit") {
                ram.lua_fhit = name;
            } else {
                const auto generation = ram.hitbox_generation;
                const auto* seq = ram.seq;
                run_model_script(engine, ram, name, mode);
                if (ram.seq != seq || ram.hitbox_generation != generation) return;
            }
        } else {
            engine.note("MODELCTRL command " + type);
            ++ram.cursor;
        }
    }
}

bool ModelCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<ModelRam&>(base);
    if (ram.category == "PLAYER2" && capsule_command(engine, ram, command, packet)) return true;
    if (command == "GENERATE" || command == "GENERATE_NDSP") {
        ram.generated = true;
        ram.active = true;
        std::string seq = "SQ_WAIT";
        if (packet.strings.size() == 1 && packet.strings[0]) seq = *packet.strings[0];
        else if (packet.strings.size() >= 2 && packet.strings[1]) seq = *packet.strings[1];
        if (ram.hp == 0) ram.hp = ram.max_hp = 100;
        if (ram.item_label >= 0) item_label(engine, ram, false);
        ram.item_table = -1;
        ram.item_player = -1;
        // Drop form (802FCE60): I<record>/I<drop table id>.
        if (ram.category == "ITEM" && packet.ints.size() >= 2) {
            ram.item_record = packet.ints[0];
            ram.item_table = packet.ints[1];
            ram.item_label = -1;
            int level = 1;
            if (auto* sys = engine.get<GameSystem>()) level = std::max(1, sys->players[0].stats.level);
            ram.item_device = roll_item_device(ram.item_table, ram.item_record, level);
        } else if (packet.ints.size() >= 2) {
            // Generator form: I<record>/I<max HP> (802FC9FC).
            ram.record = packet.ints[0];
            ram.hp = ram.max_hp = std::max(1, packet.ints[1]);
        }
        ram.muteki = 0;
        ram.capsule_on = ram.capsule_ended = false;
        ram.capsule_age = 0;
        ram.ai_timer_end = -1;
        ram.ai_request.clear();
        ram.homing_state = ram.homing_steps = 0;
        ram.follow_model.clear();
        ram.follow_bone.clear();
        ram.follow_trace = nullptr;
        if (ram.is_player) {
            if (auto* sys = engine.get<GameSystem>()) {
                sys->prepare_player(engine, ram.index);
                const auto& slot = sys->players[std::size_t(std::clamp(ram.index, 0, 3))];
                const auto& stats = slot.stats;
                // A defeated player comes back with full HP (DEADRESTART).
                if (slot.hp <= 0) sys->players[std::size_t(std::clamp(ram.index, 0, 3))].hp = slot.max_hp;
                ram.max_hp = std::max(1, slot.max_hp);
                ram.hp = std::max(1, slot.hp);
                if (auto* weapons = engine.get<WeaponManager>())
                    if (const auto* rows = weapons->labels(stats.weapon_model))
                        for (const auto& [sequence, label] : *rows) ram.labels[sequence] = label;
                // 8034DC60: PLAYERARMS GENERATE of POWERSOCKETNAME[0] hangs the weapon on,
                // posted (8030650C) so the weapon package is registered by then.
                if (!slot.weapons[0].empty())
                    engine.messenger().post(engine.time() + kFrameNs, "PLAYERARMS", slot.weapons[0], ram.index, "GENERATE", {});
                // 8034DCFC..8034DE94: level, HP and MP meters and numbers.
                auto hud = [&](const char* category, const char* name, const char* command, Packet p) {
                    engine.messenger().send(engine, category, name, ram.index, command, p);
                };
                Packet level, hp_percent, hp_value, mp_percent, mp_value;
                level.ints.push_back(stats.level);
                hp_percent.floats.push_back(float(slot.hp * 100 / std::max(1, slot.max_hp)));
                hp_value.ints.push_back(slot.hp);
                mp_percent.floats.push_back(float(slot.mp * 100 / std::max(1, slot.max_mp)));
                mp_value.ints.push_back(slot.mp);
                hud("NUMBERCTRL", "lvnm", "SETVALUE", level);
                hud("METERCTRL", "hpmt", "SETPERCENT", hp_percent);
                hud("NUMBERCTRL", "hpnm", "SETVALUE", hp_value);
                hud("METERCTRL", "mpmt", "SETPERCENT", mp_percent);
                hud("NUMBERCTRL", "mpnm", "SETVALUE", mp_value);
            }
        }
        ram.attach_name.clear();
        ram.attach_index = -1;
        ram.attach_lock = false;
        // Effect form (80310340): source RAM/name, sequence, owner name/RAM,
        // offset and rotation relative to the owner, position lock.
        if (ram.node && packet.strings.size() >= 3 && packet.ints.size() >= 2) {
            ram.attach_name = std::string(packet.string(2));
            ram.attach_index = packet.ints[1];
            ram.attach_offset = packet.vectors.empty() ? Vec3f{} : packet.vectors[0];
            ram.attach_rotation = packet.vectors.size() > 1 ? packet.vectors[1] : Vec3f{};
            ram.attach_lock = packet.boolean(0);
            place_attached(ram);
        } else if (ram.node && !packet.vectors.empty())
            ram.node->matrix = placement_matrix(packet.vectors[0], packet.vectors.size() > 1 ? packet.vectors[1] : Vec3f{});
        else if (ram.node && !packet.matrices.empty())
            ram.node->matrix = packet.matrices[0];
        // 8034D8B4: a player placed by neither vector nor matrix stands on its point.
        if (ram.is_player && ram.node && packet.vectors.empty() && packet.matrices.empty()) {
            if (auto* points = engine.get<PlayerPointManager>())
                if (auto m = points->point_matrix(points->point_no(), ram.index)) ram.node->matrix = *m;
        }
        if (ram.node) {
            // 8030DAA4..8030DADC: synchronize the Y heading with the placed matrix.
            const auto& m = ram.node->matrix;
            ram.applied = {0, std::atan2(m(2, 0), m(2, 2)), 0};
            ram.desired = ram.applied;
        }
        select_sequence(engine, ram, seq);
        if (ram.category == "PLAYER2" && ram.node) {
            // 8034E8FC: the capsule remembers its stage and position.
            if (auto* sys = engine.get<GameSystem>()) {
                auto& chip = sys->bit_chips[std::size_t(std::clamp(ram.index, 0, 3))];
                chip.stage = engine.stage_name();
                const auto& m = ram.node->matrix;
                chip.position = {m(3, 0), m(3, 1), m(3, 2)};
            }
        }
        return true;
    }
    if (command == "REMOVE") {
        if (ram.generated) {
            Packet p;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "NDSP", p);
            ram.generated = false;
            ram.active = false;
            ram.seq = nullptr;
            ram.seq_name.clear();
            ram.wait_name.reset();
        }
        return true;
    }
    if (command == "GETINFONAME") { packet.strings.emplace_back(ram.info_name); return true; }
    if (command == "GETINFONAME_RUN") { if (ram.generated) packet.strings.emplace_back(ram.info_name); return true; }
    if (command == "ISRUN") { packet.bools.push_back(ram.generated ? 1 : 0); return true; }
    if (command == "ISSEQNAME") {
        packet.bools.push_back(ram.generated && !packet.strings.empty() && ram.seq_name == packet.string(0) ? 1 : 0);
        return true;
    }
    if (command == "CHKBULLET") {
        hit_check(engine, ram, packet);
        return true;
    }
    if (command == "GETHP") { packet.ints.push_back(ram.generated ? ram.hp : 0); return true; }
    if (command == "SETHP") {
        if (ram.generated && !packet.ints.empty()) {
            ram.hp = packet.ints[0];
            ram.max_hp = std::max(ram.max_hp, ram.hp);
        }
        return true;
    }
    if (command == "GETRECNO") { packet.ints.push_back(ram.record >= 0 ? ram.record : ram.index); return true; }
    if (command == "FIND_SPACE") {
        // First free RAM of this info answers (generator spawn).
        if (!ram.generated && packet.ints.empty()) packet.ints.push_back(ram.index);
        return true;
    }
    if (command == "SETLABEL") {
        // "<sequence>/<label>": the label section that sequence starts from.
        if (packet.strings.size() >= 2) ram.labels[std::string(packet.string(0))] = std::string(packet.string(1));
        else if (packet.strings.size() == 1) ram.label = std::string(packet.string(0));
        return true;
    }
    if (command == "GETSEQNAMELIST") {
        packet.strings.emplace_back("SQ_ERAS");
        if (ram.sequences) for (const auto& s : *ram.sequences) packet.strings.emplace_back(s.name);
        return true;
    }
    if (command == "ONCTRL" || command == "OFCTRL") {
        ram.control_enabled = command == "ONCTRL";
        return true;
    }
    if (command == "SETLODHIGH" || command == "RESETLODHIGH") return true;
    if (command == "ONFREEZE" || command == "OFFREEZE") {
        ram.frozen = command == "ONFREEZE" && ram.generated;
        if (ram.generated) {
            Packet p;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, std::string(command), p);
        }
        return true;
    }
    if (command == "MOVETRANS") {
        if (ram.generated && ram.node && packet.vectors.size() >= 2)
            ram.node->matrix = placement_matrix(packet.vectors[0], packet.vectors[1]);
        return true;
    }
    if (command == "SETMATRIX") {
        if (ram.generated && ram.node && !packet.matrices.empty()) ram.node->matrix = packet.matrices[0];
        return true;
    }
    if (command == "GETMATRIX" || command == "GETMATRIX_ALL" || command == "GETMATRIX_ACTIVE") {
        if (command == "GETMATRIX_ACTIVE" && !ram.generated) return true;
        packet.ints.push_back(ram.index);
        packet.matrices.push_back(ram.node ? ram.node->matrix : Mat4{});
        return true;
    }
    if (command == "ACTION1" && ram.category == "ITEM") {
        if (pick_item(engine, ram, packet.integer(0, -1))) packet.bools.push_back(1);
        return true;
    }
    if (command == "SETEFFECT") {
        // 8030DD34: model, sequence, delay, offset (+ rotation when two vectors),
        // sequence-dead and position-lock flags.
        if (!ram.generated || packet.strings.empty()) return true;
        ModelRam::Effect effect;
        effect.model = std::string(packet.string(0));
        effect.sequence = std::string(packet.string(1));
        effect.delay = packet.real(0);
        if (!packet.vectors.empty()) effect.offset = packet.vectors[0];
        if (packet.vectors.size() == 2) effect.rotation = packet.vectors[1];
        effect.sequence_dead = packet.boolean(0);
        effect.lock = packet.boolean(1);
        ram.effects.push_back(std::move(effect));
        return true;
    }
    if (command == "GETTRCDATA") {
        // The model's trace recorder (TRC data) for MOVERL ACTOR.
        if (ram.trace) packet.objects.push_back(ram.trace);
        return true;
    }
    if (command == "SETSOUND" ||
        command == "ACTION1" || command == "SETCONVEYOR" || command == "SETQUICKSAND" ||
        command == "SETSLIPPEDFLOOR" || command == "ONHITBULLETBOX" || command == "OFHITBULLETBOX") {
        engine.note("MODELCTRL " + std::string(command));
        return true;
    }
    // Players and enemies (beNDMWMdlPEBase) take the status commands of
    // 804E28E0 (vtable +C4, status.cpp).
    if ((ram.is_player || ram.category == "ENEMY") && status_command(engine, ram, command, packet)) return true;
    // Any other command selects the named sequence of a generated model; a
    // name the model has no sequence for leaves the current one running.
    if (!ram.generated) return true;
    bool known = command == "SQ_ERAS" || command == "SQ_ERAS2";
    if (ram.sequences)
        for (const auto& s : *ram.sequences) known = known || s.name == command;
    if (known) select_sequence(engine, ram, command);
    else engine.note("MODELCTRL command " + std::string(command) + " without a sequence in " + ram.info_name);
    return true;
}

const ModelCtrlManager::ModelRam* ModelCtrlManager::player(int index) const {
    for (const auto& ram : rams_)
        if (ram->is_player && ram->generated && ram->index == index) return ram.get();
    return nullptr;
}

namespace {
float wrap_angle(float a) {
    constexpr float tau = 6.28318530718f;
    while (a > 3.14159265f) a -= tau;
    while (a < -3.14159265f) a += tau;
    return a;
}
} // namespace

void ModelCtrlManager::update_player(Engine& engine, ModelRam& ram) {
    const auto& pad = engine.pads()[std::size_t(std::clamp(ram.index, 0, 3))];
    const float sx = pad.left.x, sy = pad.left.y;
    const float magnitude = std::sqrt(sx * sx + sy * sy);
    ram.key_input = ram.control_enabled && magnitude > 0.25f;
    if (ram.key_input) {
        // 8034C67C..8034C6B8: desired heading from the inverse camera matrix and stick.
        FrameCamera camera;
        if (auto* cameras = engine.get<CameraManager>()) cameras->frame_camera(engine, camera);
        Mat4 inverse;
        if (invert(camera.view, inverse)) {
            const Vec3f forward{inverse(2, 0), inverse(2, 1), inverse(2, 2)};
            ram.desired[1] = std::atan2(forward.x, forward.z) + std::atan2(sx, -sy);
        }
    }
    // 8034C4E8: while a direction is held, its octant counter restarts at 30
    // (804E7C30) and every counter counts down; all eight still running means
    // the stick circled, and A then starts the Spiral Attack (SQ_ATCK2).
    bool spiral = false;
    if (const auto mask = pad.left_directions.held; mask != 0) {
        static constexpr int octant[16] = {-1, 0, 2, 1, 4, -1, 3, -1, 6, 7, -1, -1, 5, -1, -1, -1};
        if (const int slot = octant[mask & 15u]; slot >= 0) ram.spin[std::size_t(slot)] = 30;
        int running = 0;
        for (auto& counter : ram.spin)
            if (counter > 0) {
                --counter;
                ++running;
            }
        spiral = running >= 8;
    }
    // 8034C770: with B held, tapping one direction twice within 15 frames
    // starts the Slam Attack (SQ_ATCK3).
    const auto direction = pad.left_directions.held;
    const bool stick = direction != 0;
    if (ram.control_enabled && !engine.main_lobby() && (pad.buttons.held & dw4::pad_button::b)) {
        if (ram.slam_timer > 0) ram.slam_timer = std::max(0.0f, ram.slam_timer - 1.0f);
        switch (ram.slam_state) {
        case 0: if (!stick) ram.slam_state = 1; break;
        case 1:
            if (stick) {
                ram.slam_timer = 15.0f;
                ram.slam_direction = direction;
                ram.slam_state = 2;
            }
            break;
        case 2:
            if (ram.slam_timer == 0) ram.slam_state = 0;
            else if (!stick) ram.slam_state = 3;
            break;
        case 3:
            if (ram.slam_timer == 0) ram.slam_state = 0;
            else if (stick) {
                ram.slam_state = 0;
                if (direction == ram.slam_direction && ram.seq_name != "SQ_JUMP" && ram.seq_name != "SQ_FALL") {
                    select_sequence(engine, ram, "SQ_ATCK3");
                    return;
                }
            }
            break;
        default: ram.slam_state = 0; break;
        }
    } else {
        ram.slam_state = 0;
    }
    // 8034C3C8 buttons while standing or walking: B guards (outside the main
    // lobby), X jumps, A attacks - or, in the main lobby, appends the talk
    // hitbox (kind 60000, delay 1, length 1, offset (0,4,7), size 3).
    const bool free = ram.seq_name != "SQ_JUMP" && ram.seq_name != "SQ_FALL";
    if (ram.control_enabled && free) {
        const auto trigger = pad.buttons.trigger;
        auto& messenger = engine.messenger();
        auto panel = [&](const char* command) {
            Packet p;
            messenger.send(engine, "NDMWPANEL", std::nullopt, ram.index, command, p);
            return p;
        };
        const bool has_panel = messenger.count("NDMWPANEL", std::nullopt, ram.index) != 0;
        // 8034C960: with a panel up, ISCHECK reports a decided line: a disk
        // plays SQ_ITEM, a technique sets its label on SQ_WAZA and plays it.
        // X still jumps, Y asks for the emergency heal, START is refused.
        if (ram.panel_open) {
            Packet check = panel("ISCHECK");
            if (check.bools.empty() || !check.bools[0]) {
                ram.panel_open = false;
            } else if (check.bools.size() > 2 && check.bools[2]) {
                if (check.bools[1]) {
                    select_sequence(engine, ram, "SQ_ITEM");
                } else {
                    ram.labels["SQ_WAZA"] = std::string(check.string(0));
                    select_sequence(engine, ram, "SQ_WAZA");
                }
                return;
            }
        }
        if (ram.panel_open) {
            if (trigger & dw4::pad_button::x) {
                select_sequence(engine, ram, "SQ_JUMP");
            } else if (trigger & dw4::pad_button::y) {
                if (!panel("GENEEMGHEAL").bools.empty()) ram.panel_open = true;
            } else if (trigger & dw4::pad_button::start) {
                Packet sound;
                sound.strings.emplace_back("CURSOR_NG");
                messenger.send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
            }
            return;
        }
        // 8034D0CC: START opens the status window, refused while a panel runs.
        if ((trigger & dw4::pad_button::start) && messenger.count("SUBWND", std::nullopt, -1)) {
            Packet status;
            status.ints.push_back(ram.index);
            status.strings.emplace_back("STATUS");
            messenger.send(engine, "SUBWND", std::nullopt, 0, "GENERATE", status);
            return;
        }
        if (has_panel && (trigger & dw4::pad_button::y)) {
            if (!panel("GENEEMGHEAL").bools.empty()) ram.panel_open = true;
            return;
        }
        if (has_panel && (trigger & (dw4::pad_button::l | dw4::pad_button::r | dw4::pad_button::z))) {
            const char* command = (trigger & dw4::pad_button::l) ? "GENERATEITEM"
                                  : (trigger & dw4::pad_button::r) ? "GENERATEWAZA" : "GENERATESTAT";
            if (!panel(command).bools.empty()) ram.panel_open = true;
            return;
        }
        if (!engine.main_lobby() && (trigger & dw4::pad_button::b)) {
            select_sequence(engine, ram, "SQ_GUARD");
            return;
        }
        if (trigger & dw4::pad_button::x) {
            select_sequence(engine, ram, "SQ_JUMP");
            return;
        }
        if (trigger & dw4::pad_button::a) {
            if (engine.main_lobby()) {
                ModelRam::Hitbox talk;
                talk.kind = 60000;
                talk.delay = 1;
                talk.end = 2;
                talk.offset = {0, 4, 7};
                talk.size = {3, 3, 3};
                ram.hitboxes.push_back(talk);
            } else if (Packet pick; (pick.ints.push_back(ram.index), engine.messenger().count("DROPITEM", std::nullopt, -1)) &&
                       (engine.messenger().send(engine, "DROPITEM", std::nullopt, -1, "ACTION1", pick), !pick.bools.empty())) {
                return; // 8034CEB0: DROPITEM ACTION1 took an item instead of attacking
            } else if (engine.time() >= ram.attack_ready) {
                // ATDELAY: the next attack waits the player's ATTACKDELAY frames.
                int delay = 30;
                if (auto* sys = engine.get<GameSystem>()) delay = sys->players[std::size_t(std::clamp(ram.index, 0, 3))].stats.attack_delay;
                ram.attack_ready = engine.time() + frames_to_time(float(delay));
                ram.combo_phase = 0;
                if (spiral) ram.spin.fill(0);
                select_sequence(engine, ram, spiral ? "SQ_ATCK2" : "SQ_ATCK1");
                return;
            }
        }
    }
    apply_control(engine, ram);
}

// 80309168: post-input control transitions of the CTRL modes.
void ModelCtrlManager::apply_control(Engine& engine, ModelRam& ram) {
    std::string request;
    if (ram.ctrl_mode == 1 && ram.channels.allocated()) {
        std::optional<bool> moving;
        if (ram.ctrl_phase == 0) moving = ram.key_input;
        else if (ram.ctrl_phase == 1 && !ram.key_input) moving = false;
        else if (ram.ctrl_phase == 2 && ram.key_input) moving = true;
        if (moving) {
            dw4::MotionCommand motion;
            motion.type = "beModelCtrlMOVE";
            motion.channel = 2;
            const std::size_t first = *moving ? 2 : 0;
            motion.setup = {*moving ? dw4::MotionMode::accelerate_positive : dw4::MotionMode::stop_positive,
                            ram.pdmove[first], ram.pdmove[first + 1], 0};
            try { ram.channels.apply(motion); } catch (const std::exception&) {}
            ram.ctrl_phase = *moving ? 1 : 2;
        }
    } else if (ram.ctrl_mode == 2 && ram.key_input) request = "SQ_WALK";
    else if (ram.ctrl_mode == 3 && !ram.key_input) request = "SQ_WAIT";
    if (!request.empty() && request != ram.seq_name) select_sequence(engine, ram, request);
}

void ModelCtrlManager::advance_motion(Engine& engine, ModelRam& ram) {
    if (!ram.node) return;
    auto& m = ram.node->matrix;
    const Vec3f position{m(3, 0), m(3, 1), m(3, 2)};
    // Turning (8030C85C): shortest arc at the stored rate per frame.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (ram.applied[axis] == ram.desired[axis]) continue;
        const float rate = axis == 1 ? ram.turn_rate_y : 0.0f;
        if (rate == 0) continue;
        const float diff = wrap_angle(ram.desired[axis] - ram.applied[axis]);
        if (std::abs(diff) <= rate) ram.applied[axis] = ram.desired[axis];
        else ram.applied[axis] += diff > 0 ? rate : -rate;
    }
    std::array<float, 16> matrix{};
    std::copy(m.m.begin(), m.m.end(), matrix.begin());
    try {
        dw4::original_model_turn_basis(matrix, ram.applied);
        matrix[12] = position.x;
        matrix[13] = position.y;
        matrix[14] = position.z;
        if (ram.channels.allocated()) {
            const auto step = ram.channels.advance(1.0f);
            if (step.translation_updated) dw4::translate_model_local(matrix, step.translation);
        }
    } catch (const std::exception& error) {
        engine.note(std::string("model motion: ") + error.what());
        return;
    }
    Vec3f next{matrix[12], matrix[13], matrix[14]};
    bool wall_hit = false, floor_hit = false;
    if (!ram.is_player && ram.category != "ENEMY" && (!ram.lua_whit.empty() || !ram.lua_fhit.empty())) {
        // PROVISIONAL wall and floor hits (8030 9C74: HITLANDDELIVER HITWALL
        // and the landing check) on the host collision query.
        if (auto* hitland = engine.get<HitLandManager>(); hitland && hitland->world().triangle_count()) {
            const auto& world = hitland->world();
            std::uint32_t mask = hitland->mask(ram.info_name);
            if (mask == 0) mask = 7u;
            const float radius = std::max(hitland->radius(ram.info_name), 1.0f);
            if (!ram.lua_whit.empty()) {
                const Vec3f pushed = world.resolve_walls(next, radius, -radius, radius, mask);
                const float dx = pushed.x - next.x, dz = pushed.z - next.z;
                wall_hit = dx * dx + dz * dz > 1e-4f;
            }
            float floor = 0;
            if (!ram.lua_fhit.empty() &&
                world.floor_height({next.x, position.y, next.z}, 0.0f, std::max(position.y - next.y, 0.0f) + 0.5f, floor,
                                   nullptr, mask))
                floor_hit = next.y <= floor + 0.5f;
        }
    }
    if (ram.is_player || ram.category == "ENEMY") {
        // PROVISIONAL collision response (see collision.hpp).
        if (auto* hitland = engine.get<HitLandManager>(); hitland && hitland->world().triangle_count()) {
            auto& world = hitland->world();
            std::uint32_t mask = hitland->mask(ram.info_name);
            if (mask == 0) mask = ram.is_player ? 7u : 11u;
            // OPEN QUESTION: kind 4 "limit" geometry (e.g. stag0101's inward-facing
            // limit_Circle around the path) would block the only route if queried,
            // so it is left out of wall queries until its gate is understood.
            next = world.resolve_walls(next, 3.0f, 2.0f, 12.0f, mask & ~4u);
            // PROVISIONAL model-to-model separation (the original delivers
            // CHECKWALL between the models' HitLandDeliv cylinders).
            const float own = std::max(hitland->radius(ram.info_name), 2.0f);
            for (const auto& other : rams_) {
                if (other.get() == &ram || !other->generated || !other->node) continue;
                if (!other->is_player && other->category != "ENEMY") continue;
                if (other->seq_name == "SQ_ERAS") continue;
                const auto& o = other->node->matrix;
                if (std::abs(o(3, 1) - next.y) > 10.0f) continue;
                const float reach = own + std::max(hitland->radius(other->info_name), 2.0f);
                const float dx = next.x - o(3, 0), dz = next.z - o(3, 2);
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d >= reach) continue;
                if (d > 1e-4f) {
                    next.x = o(3, 0) + dx / d * reach;
                    next.z = o(3, 2) + dz / d * reach;
                } else {
                    next.x = o(3, 0) + reach;
                }
            }
            float floor = 0;
            const bool rising = next.y > position.y + 0.001f;
            if (!rising && world.floor_height(next, 6.0f, ram.on_ground ? 6.0f : 0.5f, floor, nullptr, mask)) {
                next.y = floor;
                ram.on_ground = true;
                ram.fall_speed = 0;
                // Landing ends a falling Y channel (CHECKLAND response), which
                // completes the sequence's Landing wait.
                if (auto list = ram.channels.list_owner(); list && list->size() > 1) {
                    auto& y = list->mutable_entries()[1];
                    if (y && y->active && y->speed <= 0) {
                        y->active = false;
                        y->speed = 0;
                    }
                }
            } else if (!rising && ram.fall_check) {
                ram.on_ground = false;
                ram.fall_speed = std::min(ram.fall_speed + 0.12f, 4.0f);
                const float before = next.y;
                next.y -= ram.fall_speed;
                if (world.floor_height({next.x, before, next.z}, 0.5f, ram.fall_speed + 0.5f, floor, nullptr, mask)) {
                    next.y = floor;
                    ram.on_ground = true;
                    ram.fall_speed = 0;
                }
            }
        }
    }
    // PROVISIONAL: a model that fell far below its last floor returns there
    // (the original recovery from bottomless areas is not identified yet).
    if (ram.on_ground) {
        ram.last_ground = next;
        ram.has_ground = true;
    } else if (ram.has_ground && next.y < ram.last_ground.y - 300.0f) {
        next = ram.last_ground;
        ram.fall_speed = 0;
        ram.on_ground = true;
        if (ram.is_player) engine.log("player " + std::to_string(ram.index) + " fell out and was put back");
    }
    matrix[12] = next.x;
    matrix[13] = next.y;
    matrix[14] = next.z;
    std::copy(matrix.begin(), matrix.end(), m.m.begin());
    if (wall_hit) run_model_script(engine, ram, ram.lua_whit, "WHit");
    else if (floor_hit) run_model_script(engine, ram, ram.lua_fhit, "FHit");
}

void ModelCtrlManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.generated) continue;
        // 8030CB2C: an erased model is removed on its next update.
        if (ram.seq_name == "SQ_ERAS") {
            Packet p;
            const Mat4 last = ram.node ? ram.node->matrix : Mat4{};
            command(engine, ram, "REMOVE", p);
            if (ram.item_label >= 0) item_label(engine, ram, false);
            if (ram.is_player) {
                // Player erase hook (vtable +B4, 8034E3C0): the defeated player
                // leaves its bit capsule, whose script ends the attempt.
                static const char* entries[] = {"SQ_ENTR1P", "SQ_ENTR2P", "SQ_ENTR3P", "SQ_ENTR4P"};
                Packet capsule;
                capsule.matrices.push_back(last);
                capsule.strings.emplace_back(entries[std::clamp(ram.index, 0, 3)]);
                engine.messenger().send(engine, "MODELCTRL", "g_000bcaps", ram.index, "GENERATE", capsule);
                Packet run;
                engine.messenger().send(engine, "MODELCTRL", "g_000bcaps", ram.index, "ISRUN", run);
                if (run.boolean(0))
                    if (auto* sys = engine.get<GameSystem>()) sys->store_bit_chip(ram.index);
                engine.log("player " + std::to_string(ram.index) + " defeated");
            }
            continue;
        }
        if (ram.frozen) continue;
        if (ram.muteki > 0) ram.muteki = std::max(0.0f, ram.muteki - 1.0f);
        // 8030CCDC: input, AI and the control transitions run only while the
        // sequence has a CTRL mode; otherwise a running AITIMER is dropped.
        if (ram.ctrl_mode != 0) {
            if (ram.is_player) update_player(engine, ram);
            else if (ram.category == "ENEMY") update_enemy(engine, ram);
            else if (ram.category == "PLAYER2") update_capsule(engine, ram);
            else {
                if (ram.homing) update_homing(engine, ram);
                apply_control(engine, ram);
            }
        } else {
            if (ram.ai_maps) ram.ai_timer_end = -1;
            if (ram.is_player) ram.key_input = false;
        }
        // 8030CD18: the status update of players and enemies (vtable +C0).
        if (ram.is_player || ram.category == "ENEMY") update_status(engine, ram);
        if (ram.category == "ITEM" && ram.item_table >= 0) update_item(engine, ram);
        if (ram.follow_trace) {
            // 8030CD94: the recorder's last world matrix.
            if (ram.node && ram.follow_trace->recorded) ram.node->matrix = *ram.follow_trace->recorded;
        } else if (!ram.follow_model.empty()) follow_bone(engine, ram);
        else if (ram.is_player || ram.channels.allocated()) advance_motion(engine, ram);
        if (!ram.hitboxes.empty()) update_hitboxes(engine, ram);
        if (!ram.effects.empty() && ram.generated) update_effects(engine, ram);
        if (ram.attach_lock && ram.generated) place_attached(ram);
        if (!ram.generated || !ram.wait_name) continue;
        const auto& wait = *ram.wait_name;
        bool done = false;
        if (wait == "Action") {
            Packet packet;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "ISCOMEND", packet);
            done = !packet.bools.empty() && packet.bools[0];
        } else if (wait == "TurnY" || wait == "Landing" || wait == "Moving" || wait == "Rotation") {
            const auto states = ram.channels.states();
            if (wait == "TurnY" || wait == "Landing") done = !states[1].active;
            else if (wait == "Moving") done = !states[0].active && !states[1].active && !states[2].active;
            else done = !states[3].active;
            if (wait == "Landing" && ram.is_player && !ram.on_ground) done = false;
        } else if (ram.is_player && (wait == "Atck01" || wait == "Atck02" || wait == "Atck03")) {
            done = player_wait(engine, ram, wait);
        } else {
            engine.note("MODELCTRL wait " + wait);
        }
        if (done) {
            ram.wait_name.reset();
            run_commands(engine, ram);
        }
    }
}

// MODEL_HITBOX queue: every entry advances once per frame in list order. The
// first check happens when elapsed reaches the delay; later passes check
// again until elapsed reaches the end, which removes the entry unchecked.
void ModelCtrlManager::update_hitboxes(Engine& engine, ModelRam& ram) {
    const auto generation = ram.hitbox_generation;
    for (std::size_t i = 0; i < ram.hitboxes.size();) {
        auto& box = ram.hitboxes[i];
        box.elapsed += 1.0f;
        bool check = false;
        if (!box.started) {
            if (box.elapsed >= box.delay) {
                box.started = true;
                check = true;
            }
        } else if (box.elapsed >= box.end) {
            ram.hitboxes.erase(ram.hitboxes.begin() + std::ptrdiff_t(i));
            continue;
        } else {
            check = true;
        }
        if (check) {
            const auto copy = box;
            fire_hitbox(engine, ram, copy);
            if (ram.hitbox_generation != generation || !ram.generated) return;
        }
        ++i;
    }
}

// Start routine: world box from the source matrix and authored offset with
// boxSize as full extents, then CHKBULLET to the category of the bullet mode
// (kind / 10000: 6 talk, 7-8 enemy bullet, otherwise player bullet; below
// 10000 only kinds under 1000 are player bullets).
void ModelCtrlManager::fire_hitbox(Engine& engine, ModelRam& ram, const ModelRam::Hitbox& box) {
    if (!ram.node) return;
    const auto& m = ram.node->matrix;
    const Vec3f o = box.offset;
    const Vec3f center{o.x * m(0, 0) + o.y * m(1, 0) + o.z * m(2, 0) + m(3, 0),
                       o.x * m(0, 1) + o.y * m(1, 1) + o.z * m(2, 1) + m(3, 1),
                       o.x * m(0, 2) + o.y * m(1, 2) + o.z * m(2, 2) + m(3, 2)};
    const Vec3f half{box.size.x * 0.5f, box.size.y * 0.5f, box.size.z * 0.5f};
    const int group = box.kind / 10000;
    int mode = 0;
    if (group == 0) mode = box.kind < 1000 ? 0 : 2;
    else if (group == 6) mode = 1;
    else if (group == 7 || group == 8) mode = 2;
    static const char* categories[] = {"HITPLAYERBULLET", "HITPLAYERTALK", "HITENEMYBULLET"};
    // An effect bullet (a technique's x_... model) shoots for the model that
    // generated it (the GENERATE source strings / RAM of 80310340).
    const bool owned = !ram.attach_name.empty() && ram.attach_index >= 0;
    Packet packet;
    packet.ints = {box.kind, owned ? ram.attach_index : ram.index};
    packet.vectors = {Vec3f{center.x - half.x, center.y - half.y, center.z - half.z},
                      Vec3f{center.x + half.x, center.y + half.y, center.z + half.z}};
    packet.strings.emplace_back(owned ? ram.attach_name : ram.info_name);
    engine.messenger().send(engine, categories[mode], std::nullopt, -1, "CHKBULLET", packet);
    // A delivered result (even false) runs the source's BHit script.
    if (!packet.bools.empty() && !ram.lua_bhit.empty()) run_model_script(engine, ram, ram.lua_bhit, "BHit");
}

// "%s_%s.lua" (80426850) with the model as script context and the call
// mode for MDLisCallName (8030B6EC, 80535948).
void ModelCtrlManager::run_model_script(Engine& engine, ModelRam& ram, std::string_view prefix, std::string_view mode) {
    ModelScriptContext context;
    context.category = ram.category;
    context.name = ram.info_name;
    context.index = ram.index;
    context.call_name = std::string(mode);
    engine.push_model_context(std::move(context));
    const bool ran = engine.run_script(std::string(prefix) + "_" + ram.info_name + ".lua");
    if (static const bool trace = std::getenv("DW4_TRACE_LUACALL") != nullptr; trace)
        engine.log("luacall " + std::string(prefix) + "_" + ram.info_name + ".lua #" + std::to_string(ram.index) + " mode '" +
                   std::string(mode) + "'" + (ran ? "" : " (no script)"));
    engine.pop_model_context();
}

// Player controller waits (vtable +A4, 8034D214). Atck01: the attack chain
// continues after the action only when A stayed held; Atck02: releasing A
// returns to SQ_WAIT, otherwise continue after the action; Atck03: wait while
// A is held.
bool ModelCtrlManager::player_wait(Engine& engine, ModelRam& ram, std::string_view wait) {
    const auto& pad = engine.pads()[std::size_t(std::clamp(ram.index, 0, 3))];
    const bool held = ram.control_enabled && (pad.buttons.held & dw4::pad_button::a);
    auto action_done = [&] {
        Packet packet;
        engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "ISCOMEND", packet);
        return !packet.bools.empty() && packet.bools[0];
    };
    if (wait == "Atck01") {
        if (ram.combo_phase == 0) {
            ram.combo_released = false;
            ram.combo_phase = 1;
        }
        if (!held) ram.combo_released = true;
        if (!action_done()) return false;
        ram.combo_phase = 0;
        if (!ram.combo_released) return true;
        select_sequence(engine, ram, "SQ_WAIT");
        return false;
    }
    if (wait == "Atck02") {
        if (!held) {
            select_sequence(engine, ram, "SQ_WAIT");
            return false;
        }
        return action_done();
    }
    return !held;
}

// Effect subcommands (80310754 / start 80310340): after the delay, FIND_SPACE a
// RAM of the effect model and GENERATE it on this model. Sequence-dead effects
// stay queued so a sequence change erases them.
void ModelCtrlManager::update_effects(Engine& engine, ModelRam& ram) {
    for (std::size_t i = 0; i < ram.effects.size();) {
        auto& effect = ram.effects[i];
        effect.elapsed += 1.0f;
        if (effect.started || effect.elapsed < effect.delay) { ++i; continue; }
        effect.started = true;
        Packet find;
        engine.messenger().send(engine, "MODELCTRL", effect.model, -1, "FIND_SPACE", find);
        if (find.ints.empty()) {
            engine.note("effect model " + effect.model);
            ram.effects.erase(ram.effects.begin() + std::ptrdiff_t(i));
            continue;
        }
        effect.ram = find.ints[0];
        Packet generate;
        generate.ints = {ram.index, ram.index};
        generate.strings.emplace_back(ram.info_name);
        generate.strings.emplace_back(effect.sequence);
        generate.strings.emplace_back(ram.info_name);
        generate.vectors = {effect.offset, effect.rotation};
        generate.bools.push_back(effect.lock ? 1 : 0);
        const std::string model = effect.model;
        const int target = effect.ram;
        const bool keep = effect.sequence_dead;
        if (!keep) ram.effects.erase(ram.effects.begin() + std::ptrdiff_t(i));
        else ++i;
        engine.messenger().send(engine, "MODELCTRL", model, target, "GENERATE", generate);
        if (!ram.generated) return;
    }
}

// An attached effect (8030D534): the owner's matrix, the offset in its space
// (80128DC8), then local rotations about X, Y and Z in radians (801298F0).
void ModelCtrlManager::place_attached(ModelRam& ram) {
    const auto* owner = find(ram.attach_name, ram.attach_index);
    if (!owner || !owner->node || !ram.node) return;
    const Vec3f v = ram.attach_offset;
    Mat4 m = translation(v.x, v.y, v.z) * owner->node->matrix;
    const Vec3f r = ram.attach_rotation;
    const float angles[3] = {r.x, r.y, r.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (angles[axis] == 0.0f) continue;
        Mat4 q;
        const float c = std::cos(angles[axis]), s = std::sin(angles[axis]);
        if (axis == 0) { q(1, 1) = c; q(1, 2) = s; q(2, 1) = -s; q(2, 2) = c; }
        else if (axis == 1) { q(0, 0) = c; q(0, 2) = -s; q(2, 0) = s; q(2, 2) = c; }
        else { q(0, 0) = c; q(0, 1) = s; q(1, 0) = -s; q(1, 1) = c; }
        m = q * m;
    }
    ram.node->matrix = m;
}

// KIND_PLAYER2 bit capsule (beNDMWMdlPlayer2, 8034EB78): switches on while a
// live player is within 30 units; with no live player left it runs
// end_g_000bcaps.lua, the stage's game over.
void ModelCtrlManager::update_capsule(Engine& engine, ModelRam& ram) {
    if (!ram.node) return;
    if (ram.capsule_age < 10) ++ram.capsule_age;
    const auto& m = ram.node->matrix;
    bool any = false;
    float best = 1e30f;
    for (const auto& other : rams_) {
        if (!other->is_player || !other->generated || !other->node) continue;
        const auto& o = other->node->matrix;
        const float dx = o(3, 0) - m(3, 0), dy = o(3, 1) - m(3, 1), dz = o(3, 2) - m(3, 2);
        best = std::min(best, dx * dx + dy * dy + dz * dz);
        any = true;
    }
    if (!any) {
        if (!ram.capsule_ended && engine.demo().running() == 0) {
            ram.capsule_ended = true;
            engine.run_script("end_g_000bcaps.lua");
        }
        return;
    }
    if (!ram.capsule_on && best < 900.0f) {
        ram.capsule_on = true;
        select_sequence(engine, ram, "SQ_SWON");
    } else if (ram.capsule_on && best > 900.0f) {
        ram.capsule_on = false;
        select_sequence(engine, ram, "SQ_SWOF");
    }
}

// beNDMWMdlPlayer2 commands (8034ED34).
bool ModelCtrlManager::capsule_command(Engine& engine, ModelRam& ram, std::string_view command, Packet& packet) {
    auto* sys = engine.get<GameSystem>();
    const int slot = std::clamp(ram.index, 0, 3);
    if (command == "GENERATE") {
        // A capsule still out is removed before it is generated again.
        if (ram.generated) {
            Packet p;
            this->command(engine, ram, "REMOVE", p);
        }
        return false;
    }
    if (command == "GENESTAGE") {
        // The player's capsule left in this stage comes back where it was.
        if (!sys) return true;
        const auto& chip = sys->bit_chips[std::size_t(slot)];
        if (chip.stage.empty() || chip.stage != engine.stage_name()) return true;
        Packet p;
        p.vectors.push_back(chip.position);
        char entry[16];
        std::snprintf(entry, sizeof entry, "SQ_ENTR%dP", ram.index + 1);
        p.strings.emplace_back(entry);
        engine.messenger().post(engine.time() + kFrameNs, "MODELCTRL", "g_000bcaps", ram.index, "GENERATE", p);
        return true;
    }
    if (command == "RAISE") {
        // Ten updates after it appeared: a defeated player comes back where
        // the caster stands with the given HP; its bits come back and, for
        // the same species, the given percentage of the EXP; the capsule
        // leaves.
        if (ram.capsule_age < 10 || !sys) return true;
        const Mat4 matrix = packet.matrices.empty() ? Mat4{} : packet.matrices[0];
        const int percent = packet.integer(0, 0);
        const int hp = packet.integer(1, 0);
        bool alive = false;
        for (const auto& other : rams_)
            if (other->is_player && other->generated && other->index == ram.index) alive = true;
        auto& player = sys->players[std::size_t(slot)];
        if (!alive) {
            player.hp = std::clamp(hp, 1, std::max(1, player.max_hp));
            Packet generate;
            generate.matrices.push_back(matrix);
            generate.strings.emplace_back("SQ_CMBK");
            engine.messenger().send(engine, "MODELCTRL", player.digimon.empty() ? "p_agumon" : player.digimon, ram.index,
                                    "GENERATE", generate);
        }
        auto& chip = sys->bit_chips[std::size_t(slot)];
        const int exp = player.digimon == chip.digimon ? chip.exp * percent / 100 : 0;
        engine.log("player " + std::to_string(ram.index) + (alive ? "" : " raised,") + " bits +" + std::to_string(chip.bits) +
                   ", EXP +" + std::to_string(exp));
        player.bits = std::clamp(player.bits + chip.bits, 0, 9999999);
        chip.bits = 0;
        if (player.digimon == chip.digimon) {
            player.stats.exp += exp;
            chip.exp = 0;
        }
        engine.messenger().post(engine.time() + kFrameNs, "PLAYER2", std::nullopt, ram.index, "SQ_EXIT", {});
        chip.stage.clear();
        return true;
    }
    return false;
}

// PROVISIONAL enemy behaviour until the original enemy controller (AIMAP,
// beActionStarter) is reconstructed: face the nearest player, walk towards
// it and attack at close range after a cooldown.
// AIM map AI (803084B8), run through the control logic (80309168): when
// AITIMER expires, the distance to the nearest player picks the map's near /
// middle / far / outside table; a roll below 16 picks the first entry whose
// cumulative weight exceeds it. The entry sets the heading (target direction
// plus its angle), the requested sequence and the next AITIMER; until then the
// same sequence stays requested.
// AIH homing AI (80308A2C): after [6] frames and then every [7] frames (at
// most [11] times) a bullet steers one step of [10] radians towards the
// nearest enemy (enemy bullets: player) within the reach; with [4] it also
// tilts towards the target's height every frame.
void ModelCtrlManager::update_homing(Engine& engine, ModelRam& ram) {
    const Homing& h = *ram.homing;
    if (!ram.node) return;
    bool live = true, tick = false;
    switch (ram.homing_state) {
    case 0:
        ram.ai_timer_end = engine.time() + frames_to_time(h.delay);
        ram.homing_state = 1;
        [[fallthrough]];
    case 1:
        if (engine.time() >= ram.ai_timer_end) {
            ram.homing_state = 2;
            tick = true;
        }
        break;
    case 2:
        ram.ai_timer_end = engine.time() + frames_to_time(h.interval);
        ram.homing_state = 3;
        [[fallthrough]];
    case 3:
        if (engine.time() >= ram.ai_timer_end) {
            ram.homing_state = 2;
            tick = true;
        }
        break;
    default:
        live = false;
        break;
    }
    if (!((live && h.pitch) || (tick && h.yaw))) return;
    if (tick && ++ram.homing_steps >= h.steps) ram.homing_state = 4;
    // 8031C67C: the nearest target of the other side.
    const bool enemy_bullet = ram.category == "EBULLET";
    const auto& m = ram.node->matrix;
    const ModelRam* target = nullptr;
    float best = 1e30f;
    for (const auto& other : rams_) {
        if (!other->generated || !other->node || other.get() == &ram) continue;
        if (enemy_bullet ? !other->is_player : other->category != "ENEMY") continue;
        const auto& o = other->node->matrix;
        const float dx = o(3, 0) - m(3, 0), dy = o(3, 1) - m(3, 1), dz = o(3, 2) - m(3, 2);
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best) {
            best = d2;
            target = other.get();
        }
    }
    if (static const bool trace = std::getenv("DW4_TRACE_HOMING") != nullptr; trace && tick)
        engine.log("homing " + ram.info_name + "#" + std::to_string(ram.index) + " tick " +
                   (target ? target->info_name + " d2 " + std::to_string(best) : std::string("no target")));
    if (!target || best > h.range2) return;
    const auto& o = target->node->matrix;
    auto turn = [](float from, float to, float rate) {
        const float diff = wrap_angle(to - from);
        return std::abs(diff) <= rate ? to : from + (diff > 0 ? rate : -rate);
    };
    if (live && h.pitch) {
        const float pitch = -std::atan2(o(3, 1) - m(3, 1), std::sqrt(best));
        ram.applied[0] = ram.desired[0] = turn(ram.applied[0], pitch, h.pitch_rate);
    }
    if (tick && h.yaw) {
        // 8030C85C turns one step; the heading then stays where it got to.
        const float yaw = std::atan2(o(3, 0) - m(3, 0), o(3, 2) - m(3, 2));
        ram.applied[1] = ram.desired[1] = turn(ram.applied[1], yaw, h.yaw_rate);
        if (static const bool trace = std::getenv("DW4_TRACE_HOMING") != nullptr; trace)
            engine.log("homing " + ram.info_name + "#" + std::to_string(ram.index) + " steers to " + target->info_name +
                       " heading " + std::to_string(ram.applied[1]) + " step " + std::to_string(ram.homing_steps));
    }
}

// 8030CDD0: ACTION <model> GETBONEMATRIX <bone> times MODELCTRL <model>
// GETMATRIX (same index) places the following model.
void ModelCtrlManager::follow_bone(Engine& engine, ModelRam& ram) {
    if (!ram.node) return;
    Packet bone;
    bone.strings.emplace_back(ram.follow_bone);
    engine.messenger().send(engine, "ACTION", ram.follow_model, ram.index, "GETBONEMATRIX", bone);
    if (bone.matrices.size() != 1) return;
    const auto* target = find(ram.follow_model, ram.index);
    if (!target || !target->node) return;
    ram.node->matrix = bone.matrices[0] * target->node->matrix;
    if (static const bool trace = std::getenv("DW4_TRACE_FOLLOW") != nullptr; trace && engine.frame() % 60 == 0)
        engine.log("follow " + ram.info_name + "#" + std::to_string(ram.index) + " on " + ram.follow_model + " at (" +
                   std::to_string(ram.node->matrix(3, 0)) + ", " + std::to_string(ram.node->matrix(3, 1)) + ", " +
                   std::to_string(ram.node->matrix(3, 2)) + ")");
}

void ModelCtrlManager::update_ai_map(Engine& engine, ModelRam& ram) {
    if (!ram.control_enabled || !ram.node || ram.ai_maps->empty()) return;
    std::string request = ram.ai_request;
    if (ram.ai_timer_end < 0 || engine.time() >= ram.ai_timer_end) {
        const auto& map = (*ram.ai_maps)[std::size_t(std::clamp(ram.ai_map, 0, int(ram.ai_maps->size()) - 1))];
        const auto& m = ram.node->matrix;
        const ModelRam* target = nullptr;
        float best = 1e30f;
        for (const auto& other : rams_) {
            if (!other->is_player || !other->generated || !other->node) continue;
            const auto& o = other->node->matrix;
            const float dx = o(3, 0) - m(3, 0), dy = o(3, 1) - m(3, 1), dz = o(3, 2) - m(3, 2);
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; target = other.get(); }
        }
        if (!target) return;
        ram.turn_rate_y = map.f32(6);
        const std::uint16_t band = best < map.f32(3) ? 7 : best < map.f32(4) ? 8 : best < map.f32(5) ? 9 : 10;
        const auto entries = map.list(band);
        if (!entries.empty()) {
            const int roll = int(fight::random() & 15u);
            ObjectView pick = entries.back();
            for (const auto& entry : entries)
                if (entry && roll < entry.i32(5)) { pick = entry; break; }
            const auto& o = target->node->matrix;
            ram.desired[1] = std::atan2(o(3, 0) - m(3, 0), o(3, 2) - m(3, 2)) + pick.f32(3);
            request = std::string(pick.str(2));
            ram.ai_timer_end = engine.time() + frames_to_time(pick.f32(4));
        }
    }
    ram.key_input = request == "SQ_WALK";
    ram.ai_request = request;
    if (request.empty() || request == "SQ_NONE") apply_control(engine, ram);
    else if (request != ram.seq_name) select_sequence(engine, ram, request);
}

void ModelCtrlManager::update_enemy(Engine& engine, ModelRam& ram) {
    if (ram.ai_maps) {
        update_ai_map(engine, ram);
        return;
    }
    if (ram.ai_timer > 0) --ram.ai_timer;
    ram.key_input = false;
    ram.turn_rate_y = 0.2f;
    if (!ram.control_enabled || !ram.node) return;
    if (ram.seq_name != "SQ_WAIT" && ram.seq_name != "SQ_WALK") return;
    const auto& m = ram.node->matrix;
    const ModelRam* target = nullptr;
    float best = 1e30f;
    for (const auto& other : rams_) {
        if (!other->is_player || !other->generated || !other->node) continue;
        const auto& o = other->node->matrix;
        const float dx = o(3, 0) - m(3, 0), dz = o(3, 2) - m(3, 2);
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d < best) { best = d; target = other.get(); }
    }
    if (target && best < 80.0f) {
        const auto& o = target->node->matrix;
        ram.desired[1] = std::atan2(o(3, 0) - m(3, 0), o(3, 2) - m(3, 2));
        if (best < 10.0f) {
            if (ram.ai_timer == 0 && ram.sequences &&
                std::any_of(ram.sequences->begin(), ram.sequences->end(), [](const Sequence& q) { return q.name == "SQ_ATCK1"; })) {
                ram.ai_timer = 120;
                select_sequence(engine, ram, "SQ_ATCK1");
                return;
            }
        } else {
            ram.key_input = true;
        }
    }
    apply_control(engine, ram);
}

// 8030E56C: base CHKBULLET target path. Gates, body overlap, target hit script
// "hit_<name>.lua" (8030B6EC) and the forced turn toward the source.
bool ModelCtrlManager::hit_check(Engine& engine, ModelRam& ram, Packet& packet) {
    // DW4_TRACE_HIT logs every CHKBULLET a target receives.
    static const bool trace = std::getenv("DW4_TRACE_HIT") != nullptr;
    if (trace && packet.vectors.size() >= 2) {
        char line[256];
        const Vec3f p = ram.node ? Vec3f{ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)} : Vec3f{};
        std::snprintf(line, sizeof line, "CHKBULLET %d from %s#%d -> %s#%d gen=%d ctrl=%d at (%.1f,%.1f,%.1f) box (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)",
                      packet.integer(0), std::string(packet.string(0)).c_str(), packet.integer(1, -1), ram.info_name.c_str(), ram.index,
                      ram.generated ? 1 : 0, ram.control_enabled ? 1 : 0, p.x, p.y, p.z, packet.vectors[0].x, packet.vectors[0].y,
                      packet.vectors[0].z, packet.vectors[1].x, packet.vectors[1].y, packet.vectors[1].z);
        engine.log(line);
    }
    if (!ram.generated || !ram.control_enabled || ram.seq_name == "SQ_ERAS" || !ram.hit || !ram.node) return false;
    if (packet.vectors.size() < 2) return false;
    // A bullet reaches its own shooter too (a support technique acts when it
    // does; hit scripts drop their own special bullets). MUTEKI blocks
    // everything but p-to-p hits.
    if (ram.muteki != 0 && !(ram.info_name.starts_with('p') && packet.string(0).starts_with('p'))) return false;
    const Vec3f in_min = packet.vectors[0], in_max = packet.vectors[1];
    const auto& m = ram.node->matrix;
    bool overlap = false;
    for (const auto& [lo, hi] : ram.hit->boxes) {
        // General AABox transform (80122D04): bounds of the eight transformed corners.
        Vec3f wmin{1e30f, 1e30f, 1e30f}, wmax{-1e30f, -1e30f, -1e30f};
        for (int corner = 0; corner < 8; ++corner) {
            const Vec3f c{corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z};
            const Vec3f w{c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0) + m(3, 0),
                          c.x * m(0, 1) + c.y * m(1, 1) + c.z * m(2, 1) + m(3, 1),
                          c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2) + m(3, 2)};
            wmin = {std::min(wmin.x, w.x), std::min(wmin.y, w.y), std::min(wmin.z, w.z)};
            wmax = {std::max(wmax.x, w.x), std::max(wmax.y, w.y), std::max(wmax.z, w.z)};
        }
        if (trace) {
            char line[160];
            std::snprintf(line, sizeof line, "  body (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)", wmin.x, wmin.y, wmin.z, wmax.x, wmax.y, wmax.z);
            engine.log(line);
        }
        if (wmin.x <= in_max.x && wmax.x >= in_min.x && wmin.y <= in_max.y && wmax.y >= in_min.y && wmin.z <= in_max.z &&
            wmax.z >= in_min.z) {
            overlap = true;
            break;
        }
    }
    if (!overlap) return false;
    ModelScriptContext context;
    context.category = ram.category;
    context.name = ram.info_name;
    context.index = ram.index;
    context.bullet_kind = packet.integer(0);
    context.shooter = packet.integer(1, -1);
    context.shooter_name = std::string(packet.string(0));
    engine.push_model_context(std::move(context));
    engine.run_script("hit_" + ram.info_name + ".lua");
    const auto done = engine.pop_model_context();
    if (done.turn && done.shooter >= 0) {
        // 8030E81C: face the captured source position.
        if (const auto* source = find(done.shooter_name, done.shooter); source && source->node) {
            const auto& sm = source->node->matrix;
            const float heading = std::atan2(sm(3, 0) - m(3, 0), sm(3, 2) - m(3, 2));
            ram.applied[1] = ram.desired[1] = heading;
            std::array<float, 16> matrix{};
            std::copy(ram.node->matrix.m.begin(), ram.node->matrix.m.end(), matrix.begin());
            const Vec3f position{matrix[12], matrix[13], matrix[14]};
            try {
                dw4::original_model_turn_basis(matrix, ram.applied);
                matrix[12] = position.x;
                matrix[13] = position.y;
                matrix[14] = position.z;
                std::copy(matrix.begin(), matrix.end(), ram.node->matrix.m.begin());
            } catch (const std::exception&) {}
        }
    }
    if (packet.bools.empty()) packet.bools.push_back(1);
    return true;
}

void ModelCtrlManager::install_actor_hook(Engine&, TraversalContext&) {}

// ===========================================================================
// beActionStarter
// ===========================================================================
// [7] beAsTargetModelData: target model [2] with beActionStarterData rows:
// [2] action, [3] effect sequence, [4] delay, [5] offset, [6]/[7] flags
// (Data2 adds a rotation vector).
void ActionStarterManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<Ram>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row("ACTION_BROADCAST", std::nullopt, -1, ram.get());
        rams_.push_back(std::move(ram));
        break; // one broadcast row per starter info
    }
}

bool ActionStarterManager::command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
    const std::string source(packet.string(0));
    const int index = packet.integer(0, -1);
    for (const auto& target : ram.info.list(7)) {
        if (!target || target.str(2) != source) continue;
        for (const auto& data : target.list(3)) {
            if (!data || data.str(2) != command) continue;
            Packet effect;
            effect.strings.emplace_back(ram.info_name);
            effect.strings.emplace_back(std::string(data.str(3)));
            effect.floats.push_back(float(data.i32(4)));
            effect.vectors.push_back(data.vec3(5).value_or(Vec3f{}));
            if (data.type() == "beActionStarterData2") effect.vectors.push_back(data.vec3(8).value_or(Vec3f{}));
            effect.bools = {std::uint8_t(data.boolean(6) ? 1 : 0), std::uint8_t(data.boolean(7) ? 1 : 0)};
            engine.messenger().send(engine, "MODELCTRL", source, index, "SETEFFECT", effect);
        }
        break;
    }
    return true;
}

// ===========================================================================
// PLAYERARMS (80318298 registration, 80318474 commands)
// ===========================================================================
void WeaponManager::add_info(Engine& engine, const ObjectView& info) {
    const std::string name(info.str(2));
    auto& rows = labels_[name];
    rows.clear();
    for (const auto& row : info.list(9))
        if (row) rows.emplace_back(std::string(row.str(2)), std::string(row.str(3)));
    auto& arms = arms_[name];
    arms = {};
    for (const auto& part : info.list(7)) arms.geometry.push_back(part);
    for (const auto& joint : info.list(8)) arms.joints.push_back(joint ? std::string(joint.str(2)) : std::string());
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<Ram>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = name;
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row("PLAYERARMS", name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

void WeaponManager::clear_stage(Engine&) {
    while (!attached_.empty()) remove(attached_.begin()->first);
    attached_.clear();
    rams_.clear();
    labels_.clear();
    arms_.clear();
}

void WeaponManager::remove(int index) {
    const auto found = attached_.find(index);
    if (found == attached_.end()) return;
    for (auto* actor : found->second.actors)
        for (const auto& node : found->second.nodes)
            std::erase(actor->children, node.get());
    attached_.erase(found);
}

// 80318474 GENERATE: the weapon row of player index gives the player model
// its sequence labels (SETLABEL) and hangs each weapon part under the model's
// igActor nodes in an igModelViewMatrixBoneSelect of its bone, so the part
// follows the animated skeleton. REMOVE takes the parts off again. A player
// keeps one weapon's parts at a time.
bool WeaponManager::command(Engine& engine, Ram& ram, std::string_view command, Packet&) {
    const int player = std::clamp(ram.index, 0, 3);
    if (command == "REMOVE") {
        remove(player);
        return true;
    }
    if (command != "GENERATE") return false;
    remove(player);
    auto* sys = engine.get<GameSystem>();
    const auto* rows = labels(ram.info_name);
    if (!sys || !rows) return true;
    const auto& digimon = sys->players[std::size_t(player)].digimon;
    for (const auto& [sequence, label] : *rows) {
        Packet p;
        p.strings = {sequence, label};
        engine.messenger().send(engine, "MODELCTRL", digimon, ram.index, "SETLABEL", p);
    }
    auto* actions = engine.get<ActionManager>();
    const auto arms = arms_.find(ram.info_name);
    const auto* action = actions ? actions->find(digimon, ram.index) : nullptr;
    if (!action || arms == arms_.end() || !action->database) return true;
    const auto& skeleton = action->database->skeleton();
    auto& attached = attached_[player];
    attached.actors = action->actors;
    for (std::size_t i = 0; i < arms->second.geometry.size() && i < arms->second.joints.size(); ++i) {
        const auto bone = std::find(skeleton.names.begin(), skeleton.names.end(), arms->second.joints[i]);
        Node* part = engine.scenes().node(arms->second.geometry[i]);
        if (bone == skeleton.names.end() || !part) continue;
        auto select = std::make_unique<Node>();
        select->kind = NodeKind::bone_select;
        select->name = arms->second.joints[i];
        select->bone_index = int(bone - skeleton.names.begin());
        select->children.push_back(part);
        for (auto* actor : attached.actors)
            if (actor) actor->children.push_back(select.get());
        attached.nodes.push_back(std::move(select));
    }
    engine.log("player " + std::to_string(player) + " arms " + ram.info_name + ": " + std::to_string(attached.nodes.size()) +
               " parts on " + std::to_string(attached.actors.size()) + " actors");
    return true;
}

const std::vector<std::pair<std::string, std::string>>* WeaponManager::labels(std::string_view weapon) const {
    const auto found = labels_.find(weapon);
    return found != labels_.end() ? &found->second : nullptr;
}

// ===========================================================================
// ACTION2 (802F1794 dispatch, 802F1510 loop, 802F16CC update)
// ===========================================================================
void ActionManager::add_info(Engine& engine, const ObjectView& info) {
    auto actions = std::make_unique<std::vector<Action>>();
    for (const auto& data : info.list(9)) {
        if (!data) continue;
        Action action;
        action.name = std::string(data.str(2));
        for (const auto& command : data.list(3)) {
            if (!command) continue;
            Command c;
            const auto& type = command.type();
            c.type = type;
            c.name = std::string(command.str(2));
            if (type == "beAction2JOINTSET") {
                c.first = command.f32(3);
                c.second = command.f32(4);
                c.loop = std::string(command.str(5));
            } else if (type == "beAction2FADESET" || type == "beAction2WAITCHECK" || type == "beAction2FLASHSET") {
                c.first = command.f32(3);
                c.second = command.f32(4);
            } else if (type == "beAction2MOTIONSET") {
                c.loop = std::string(command.str(5));
                c.first = command.f32(4);
                c.interpolation = std::string(command.str(3));
            }
            action.commands.push_back(std::move(c));
        }
        actions->push_back(std::move(action));
    }
    auto* action_ptr = actions.get();
    actions_[reinterpret_cast<const void*>(std::uintptr_t(info.reference()) ^ reinterpret_cast<std::uintptr_t>(info.document()))] =
        std::move(actions);
    Node* switch_node = engine.scenes().node(info.ref(7));
    AnimationDatabase* database = nullptr;
    if (const auto db = info.ref(8)) {
        auto& slot = databases_[{static_cast<const void*>(db.document()), db.reference()}];
        if (!slot) slot = std::make_unique<AnimationDatabase>(db);
        database = slot.get();
    }
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<ActionRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->actions = action_ptr;
        ram->switch_node = switch_node && switch_node->kind == NodeKind::switch_node ? switch_node : nullptr;
        if (record) ram->node = engine.scenes().node(record.ref(11));
        ram->database = database;
        if (record) {
            for (const auto& actor : record.list(13)) {
                if (!actor) continue;
                Node* n = engine.scenes().node(actor);
                ram->actors.push_back(n);
                actor_owner_[n] = ram.get();
            }
        }
        if (ram->node) {
            ram->node_clock.scale = ram->node->time_scale;
            ram->node_clock.bias = ram->node->time_bias;
        }
        if (ram->switch_node && ram->node) {
            for (std::size_t i = 0; i < ram->switch_node->children.size(); ++i)
                if (contains(ram->switch_node->children[i], ram->node)) { ram->switch_child = i; break; }
        }
        engine.messenger().add_row("ACTION", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

void ActionManager::clear_stage(Engine&) {
    rams_.clear();
    actions_.clear();
    databases_.clear();
    actor_owner_.clear();
}

const ActionManager::ActionRam* ActionManager::find(std::string_view name, int index) const {
    for (const auto& ram : rams_)
        if (ram->info_name == name && ram->index == index) return ram.get();
    return nullptr;
}

const ActorPose* ActionManager::pose_for(const Node& actor, std::int64_t world_time) {
    auto found = actor_owner_.find(&actor);
    if (found == actor_owner_.end()) return nullptr;
    auto& ram = *found->second;
    if (!ram.database) return nullptr;
    if (ram.pose_time != world_time) {
        const auto time = static_cast<std::int64_t>(scaled_clock(world_time, ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
        evaluate_pose(*ram.database, ram.motion, time, ram.pose);
        ram.pose_time = world_time;
    }
    return &ram.pose;
}

bool ActionManager::drawn(const Node* node) const {
    for (const auto& ram : rams_)
        if (ram->node == node) return !ram->switch_node || ram->switch_node->child_enabled(ram->switch_child);
    return true;
}

void ActionManager::apply_visuals(ActionRam& ram) {
    if (ram.switch_node) ram.switch_node->set_child_enabled(ram.switch_child, ram.visible);
    if (ram.node) {
        ram.node->color_override = ram.color;
        ram.node->blend_override = ram.blend;
        ram.node->texture_override = ram.texture;
        ram.node->lighting_override = ram.lighting;
        ram.node->time_scale = ram.node_clock.scale;
        ram.node->time_bias = ram.node_clock.bias;
    }
}

namespace {
std::int64_t node_time(const NodeTimer& clock, std::int64_t world) {
    return static_cast<std::int64_t>(scaled_clock(world, clock.scale) + static_cast<std::uint64_t>(clock.bias));
}
void node_origin(NodeTimer& clock, std::int64_t world, std::int64_t origin) {
    clock.bias = static_cast<std::int64_t>(static_cast<std::uint64_t>(origin) - scaled_clock(world, clock.scale));
}
} // namespace

bool ActionManager::execute(Engine& engine, ActionRam& ram, const Command& c) {
    const char first = c.name.empty() ? '\0' : c.name[0];
    if (c.type == "beAction2JOINTSET") {
        ram.joint_state = (!c.loop.empty() && c.loop[0] == 'L') ? 1 : 2;
        ram.joint_start = frames_to_time(c.first);
        ram.joint_end = frames_to_time(c.second);
        ram.joint_ended = false;
        node_origin(ram.node_clock, engine.time(), ram.joint_start);
        return true;
    }
    if (c.type == "beAction2FADESET") {
        if (first == 'H') { ram.visible = false; return true; }
        Vec4f color = ram.color.value_or(Vec4f{1, 1, 1, 1});
        const bool incoming = first == 'I';
        color.w = incoming ? 0.0f : c.second;
        ram.color = color;
        ram.blend = true;
        ram.lighting = false;
        ram.fade_value = color.w;
        ram.fade_limit = c.second;
        ram.fade_add = c.first != 0 ? c.second / c.first : c.second;
        ram.fade_state = incoming ? 1 : 2;
        timer_start(ram.timers["FADE"], engine.time());
        return true;
    }
    if (c.type == "beAction2WAITCHECK") {
        switch (first) {
        case 'F': {
            auto& timer = ram.timers["WAIT"];
            timer_start(timer, engine.time(), frames_to_time(c.first));
            ram.wait_state = 1;
            break;
        }
        case 'M': ram.wait_state = 2; break;
        case 'J': ram.wait_state = 3; break;
        case 'R': {
            auto& timer = ram.timers["WAIT"];
            timer_start(timer, engine.time(), frames_to_time(c.first));
            ram.wait_state = 1;
            break;
        }
        default: break;
        }
        return false;
    }
    if (c.type == "beAction2FLASHSET") {
        Vec4f color = ram.color.value_or(Vec4f{1, 1, 1, 1});
        color.x = color.y = 1;
        ram.color = color;
        ram.texture = false;
        ram.lighting = false;
        ram.flash_state = 1;
        timer_start(ram.timers["FLASH"], engine.time(), 0x03f940ac);
        return true;
    }
    if (c.type == "beAction2MOTIONSET") {
        const auto now = static_cast<std::int64_t>(scaled_clock(engine.time(), ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
        auto& m = ram.motion;
        if (m.clip != c.name || !(!c.loop.empty() && c.loop[0] == 'L')) {
            m.previous_clip = m.clip;
            m.previous_start = m.start;
            m.previous_loop = m.loop;
            m.blend_start = now;
            m.blend = m.previous_clip.empty() ? 0 : frames_to_time(c.first);
            m.clip = c.name;
            m.start = now;
            m.loop = !c.loop.empty() && c.loop[0] == 'L';
        }
        ram.pose_time = -1;
        return true;
    }
    engine.note("ACTION command " + c.type);
    return true;
}

void ActionManager::run(Engine& engine, ActionRam& ram) {
    if (!ram.current) return;
    while (ram.cursor < ram.current->commands.size()) {
        const auto& c = ram.current->commands[ram.cursor];
        const bool keep = execute(engine, ram, c);
        ++ram.cursor;
        if (!keep) break;
    }
}

void ActionManager::dispatch(Engine& engine, ActionRam& ram, std::string_view name) {
    // DW4_TRACE_ACTION=model (or *) logs the actions that model is given.
    static const char* trace = std::getenv("DW4_TRACE_ACTION");
    if (trace && (ram.info_name == trace || std::string_view(trace) == "*"))
        engine.log("action " + ram.info_name + "#" + std::to_string(ram.index) + " " + std::string(name));
    if (!ram.actions) return;
    const Action* found = nullptr;
    for (const auto& a : *ram.actions) if (a.name == name) { found = &a; break; }
    if (!found) {
        engine.note("ACTION " + std::string(name) + " missing in " + ram.info_name);
        return;
    }
    if (auto* sound = engine.get<SoundManager>()) sound->action_started(engine, ram.info_name, ram.index, name);
    if (!ram.action_active) {
        ram.action_active = true;
        ram.fade_state = 0;
        ram.wait_state = 0;
    }
    ram.visible = true;
    if (ram.current == found) { apply_visuals(ram); return; }
    ram.current = found;
    ram.cursor = 0;
    // ACTION_BROADCAST (8031308C): starters react to (model, RAM, action).
    Packet broadcast;
    broadcast.strings.emplace_back(ram.info_name);
    broadcast.ints.push_back(ram.index);
    if (engine.messenger().count("ACTION_BROADCAST", std::nullopt, -1))
        engine.messenger().send(engine, "ACTION_BROADCAST", std::nullopt, -1, name, broadcast);
    run(engine, ram);
    apply_visuals(ram);
}

bool ActionManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<ActionRam&>(base);
    if (command == "GETBONEMATRIX") {
        // The named bone in the model's space from the current pose; no name
        // gives the model's own origin.
        const std::string bone(packet.string(0));
        if (bone.empty()) {
            packet.matrices.push_back(Mat4{});
            return true;
        }
        if (ram.actors.empty()) return true;
        const Node* actor = ram.actors.front();
        const ActorPose* pose = pose_for(*actor, engine.time());
        if (!pose || !pose->skeleton) return true;
        const auto& names = pose->skeleton->names;
        for (std::size_t i = 0; i < names.size() && i < pose->bone_world.size(); ++i)
            if (names[i] == bone) {
                packet.matrices.push_back(pose->bone_world[i] * actor->actor_matrix);
                break;
            }
        return true;
    }
    if (command == "ISCOMEND") {
        const bool end = ram.current && ram.wait_state == 0 && ram.cursor == ram.current->commands.size();
        packet.bools.push_back(end ? 1 : 0);
        return true;
    }
    if (command == "NDSP") {
        ram.color.reset();
        ram.blend.reset();
        ram.texture.reset();
        ram.lighting.reset();
        ram.visible = false;
        ram.action_active = false;
        ram.current = nullptr;
        ram.cursor = 0;
        ram.fade_state = ram.wait_state = ram.flash_state = ram.joint_state = 0;
        ram.timers.clear();
        apply_visuals(ram);
        return true;
    }
    if (command == "ONFREEZE" || command == "OFFREEZE") {
        const bool freeze = command == "ONFREEZE";
        ram.frozen = freeze;
        const float speed = freeze ? 0.0f : 1.0f;
        auto rescale = [&](NodeTimer& t) {
            const auto bias = static_cast<std::uint64_t>(t.bias) + scaled_clock(engine.time(), t.scale) - scaled_clock(engine.time(), speed);
            t.bias = static_cast<std::int64_t>(bias);
            t.scale = speed;
            t.cached_frames = 0;
        };
        rescale(ram.node_clock);
        for (auto& [_, t] : ram.timers) rescale(t);
        apply_visuals(ram);
        return true;
    }
    dispatch(engine, ram, command);
    return true;
}

void ActionManager::update(Engine& engine) {
    const auto now = engine.time();
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.action_active || ram.frozen) { continue; }
        // Joint section.
        if (ram.joint_state == 1 || ram.joint_state == 2) {
            auto local = node_time(ram.node_clock, now);
            bool changed = false;
            for (int count = 0; local > ram.joint_end && count < 100000; ++count) {
                local = ram.joint_state == 1 ? local - ram.joint_end + ram.joint_start : ram.joint_end;
                changed = true;
                if (ram.joint_end <= ram.joint_start && ram.joint_state == 1) { local = ram.joint_start; break; }
            }
            if (changed) {
                node_origin(ram.node_clock, now, local);
                ram.joint_ended = true;
            }
        }
        // Fade.
        if (ram.fade_state == 1 || ram.fade_state == 2) {
            auto found = ram.timers.find("FADE");
            if (found != ram.timers.end()) {
                const float frames = timer_frames(found->second, now);
                if (frames != 0 && ram.color) {
                    Vec4f color = *ram.color;
                    if (ram.fade_state == 1) color.w = std::fma(frames, ram.fade_add, ram.fade_value);
                    else color.w = std::max(std::fma(-frames, ram.fade_add, ram.fade_value), 0.0f);
                    color.w = std::clamp(color.w, 0.0f, 1.0f);
                    ram.color = color;
                    ram.fade_value = color.w;
                    const bool finished = ram.fade_state == 1 ? !(color.w < ram.fade_limit) : !(color.w > 0);
                    if (finished) {
                        if (ram.fade_state == 1) {
                            if (!ram.texture) { ram.color.reset(); ram.lighting.reset(); }
                            ram.blend.reset();
                        }
                        ram.timers.erase("FADE");
                        ram.fade_state = 3;
                    }
                }
            }
        }
        // Wait.
        bool ready = false;
        if (ram.wait_state == 1) {
            auto found = ram.timers.find("WAIT");
            ready = found == ram.timers.end() || timer_expired(found->second, now);
        } else if (ram.wait_state == 2) {
            ready = true;
            if (ram.database && !ram.motion.clip.empty()) {
                if (const Clip* clip = ram.database->clip(ram.motion.clip)) {
                    const auto t = static_cast<std::int64_t>(scaled_clock(now, ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
                    ready = t - ram.motion.start >= clip->duration;
                }
            }
        } else if (ram.wait_state == 3) {
            ready = ram.joint_ended;
        }
        if (ready) {
            ram.wait_state = 0;
            run(engine, ram);
        }
        // Flash.
        if (ram.flash_state == 1) {
            auto found = ram.timers.find("FLASH");
            if (found == ram.timers.end() || timer_expired(found->second, now)) {
                if (!ram.blend) { ram.color.reset(); ram.lighting.reset(); }
                ram.texture.reset();
                ram.timers.erase("FLASH");
                ram.flash_state = 0;
            }
        }
        apply_visuals(ram);
    }
}

} // namespace dw4::engine
