#include "dw4/engine/managers.hpp"
#include "dw4/motion.hpp"

#include <algorithm>
#include <cmath>
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
    sequences_.clear();
    hit_bodies_.clear();
}

ModelCtrlManager::ModelRam* ModelCtrlManager::find(std::string_view name, int index) {
    for (auto& ram : rams_)
        if (ram->info_name == name && ram->index == index) return ram.get();
    return nullptr;
}

void ModelCtrlManager::select_sequence(Engine& engine, ModelRam& ram, std::string_view name) {
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
    // Weapon label section: start after the selected LABEL when one is set.
    if (found && !ram.label.empty()) {
        for (std::size_t i = 0; i < found->commands.size(); ++i) {
            if (found->commands[i].type() == "beModelCtrlLABEL" && found->commands[i].str(2) == ram.label) {
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
        } else if (type == "beModelCtrlMOVE" || type == "beModelCtrlMOVEEX") {
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
        } else if (type == "beModelCtrlMUTEKI" || type == "beModelCtrlLABELCTRL" || type == "beModelCtrlHITBOX" ||
                   type == "beModelCtrlAIMAP") {
            ++ram.cursor;
        } else if (type == "beModelCtrlLUACALL") {
            const auto mode = command.str(3);
            if (mode != "BHit" && mode != "WHit" && mode != "FHit") engine.note("MODELCTRL LUACALL " + std::string(mode));
            ++ram.cursor;
        } else {
            engine.note("MODELCTRL command " + type);
            ++ram.cursor;
        }
    }
}

bool ModelCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<ModelRam&>(base);
    if (command == "GENERATE" || command == "GENERATE_NDSP") {
        ram.generated = true;
        ram.active = true;
        std::string seq = "SQ_WAIT";
        if (packet.strings.size() == 1 && packet.strings[0]) seq = *packet.strings[0];
        else if (packet.strings.size() >= 2 && packet.strings[1]) seq = *packet.strings[1];
        if (ram.hp == 0) ram.hp = ram.max_hp = 100;
        // Position / rotation vectors supplied with GENERATE place the model.
        if (ram.node && !packet.vectors.empty())
            ram.node->matrix = placement_matrix(packet.vectors[0], packet.vectors.size() > 1 ? packet.vectors[1] : Vec3f{});
        if (ram.is_player && ram.node) {
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
    if (command == "GETRECNO") { packet.ints.push_back(ram.index); return true; }
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
    if (command == "SETLABEL" || command == "SETEFFECT" || command == "SETSOUND" ||
        command == "GETTRCDATA" || command == "ACTION1" || command == "SETCONVEYOR" || command == "SETQUICKSAND" ||
        command == "SETSLIPPEDFLOOR" || command == "ONHITBULLETBOX" || command == "OFHITBULLETBOX") {
        engine.note("MODELCTRL " + std::string(command));
        return true;
    }
    // Any other command selects the named sequence of a generated model.
    if (ram.generated) select_sequence(engine, ram, command);
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
    // 8034C3C8: in the main lobby, A appends a talk hitbox (kind 60000, delay 1,
    // length 1, offset (0,4,7), size 3) instead of attacking.
    if (ram.control_enabled && engine.main_lobby() && (pad.buttons.trigger & dw4::pad_button::a) && ram.talk_delay < 0)
        ram.talk_delay = 1;
    // X publishes SQ_JUMP unless the player is already airborne.
    if (ram.control_enabled && (pad.buttons.trigger & dw4::pad_button::x) &&
        (ram.seq_name == "SQ_WAIT" || ram.seq_name == "SQ_WALK")) {
        select_sequence(engine, ram, "SQ_JUMP");
        return;
    }
    // 80309168: post-input control transitions.
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
    if (ram.is_player) {
        // PROVISIONAL collision response (see collision.hpp).
        if (auto* hitland = engine.get<HitLandManager>(); hitland && hitland->world().triangle_count()) {
            auto& world = hitland->world();
            next = world.resolve_walls(next, 3.0f, 2.0f, 12.0f);
            float floor = 0;
            const bool rising = next.y > position.y + 0.001f;
            if (!rising && world.floor_height(next, 6.0f, ram.on_ground ? 6.0f : 0.5f, floor)) {
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
                if (world.floor_height({next.x, before, next.z}, 0.5f, ram.fall_speed + 0.5f, floor)) {
                    next.y = floor;
                    ram.on_ground = true;
                    ram.fall_speed = 0;
                }
            }
        }
    }
    matrix[12] = next.x;
    matrix[13] = next.y;
    matrix[14] = next.z;
    std::copy(matrix.begin(), matrix.end(), m.m.begin());
}

void ModelCtrlManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.generated) continue;
        // 8030CB2C: an erased model is removed on its next update.
        if (ram.seq_name == "SQ_ERAS") {
            Packet p;
            command(engine, ram, "REMOVE", p);
            continue;
        }
        if (ram.is_player) update_player(engine, ram);
        if (ram.is_player || ram.channels.allocated()) advance_motion(engine, ram);
        if (ram.talk_delay >= 0 && ram.talk_delay-- == 0) check_talk(engine, ram);
        if (!ram.wait_name) continue;
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
        } else {
            engine.note("MODELCTRL wait " + wait);
        }
        if (done) {
            ram.wait_name.reset();
            run_commands(engine, ram);
        }
    }
}

// MODEL_HITBOX start routine: world box from the source matrix and authored
// offset, then CHKBULLET to the category the kind selects (60000 = talk).
void ModelCtrlManager::check_talk(Engine& engine, ModelRam& ram) {
    if (!ram.node) return;
    const auto& m = ram.node->matrix;
    const Vec3f offset{0, 4, 7};
    const Vec3f center{offset.x * m(0, 0) + offset.y * m(1, 0) + offset.z * m(2, 0) + m(3, 0),
                       offset.x * m(0, 1) + offset.y * m(1, 1) + offset.z * m(2, 1) + m(3, 1),
                       offset.x * m(0, 2) + offset.y * m(1, 2) + offset.z * m(2, 2) + m(3, 2)};
    const Vec3f half{1.5f, 1.5f, 1.5f};
    Packet packet;
    packet.ints = {60000, ram.index};
    packet.vectors = {Vec3f{center.x - half.x, center.y - half.y, center.z - half.z},
                      Vec3f{center.x + half.x, center.y + half.y, center.z + half.z}};
    packet.strings.emplace_back(ram.info_name);
    engine.messenger().send(engine, "HITPLAYERTALK", std::nullopt, -1, "CHKBULLET", packet);
}

// 8030E56C: base CHKBULLET target path. Gates, body overlap, target hit script
// "hit_<name>.lua" (8030B6EC) and the forced turn toward the source.
bool ModelCtrlManager::hit_check(Engine& engine, ModelRam& ram, Packet& packet) {
    if (!ram.generated || !ram.control_enabled || ram.seq_name == "SQ_ERAS" || !ram.hit || !ram.node) return false;
    if (packet.vectors.size() < 2) return false;
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
    if (!ram.actions) return;
    const Action* found = nullptr;
    for (const auto& a : *ram.actions) if (a.name == name) { found = &a; break; }
    if (!found) {
        engine.note("ACTION " + std::string(name) + " missing in " + ram.info_name);
        return;
    }
    if (!ram.action_active) {
        ram.action_active = true;
        ram.fade_state = 0;
        ram.wait_state = 0;
    }
    ram.visible = true;
    if (ram.current == found) { apply_visuals(ram); return; }
    ram.current = found;
    ram.cursor = 0;
    run(engine, ram);
    apply_visuals(ram);
}

bool ActionManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<ActionRam&>(base);
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
