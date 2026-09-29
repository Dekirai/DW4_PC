#include "dw4/engine/managers.hpp"

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
    sequences_[reinterpret_cast<const void*>(std::uintptr_t(info.reference()) ^ reinterpret_cast<std::uintptr_t>(info.document()))] =
        std::move(sequences);
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
        rams_.push_back(std::move(ram));
    }
}

void ModelCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    sequences_.clear();
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
    if (command == "ONCTRL" || command == "OFCTRL" || command == "SETLODHIGH" || command == "RESETLODHIGH") return true;
    if (command == "ONFREEZE" || command == "OFFREEZE") {
        if (ram.generated) {
            Packet p;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, std::string(command), p);
        }
        return true;
    }
    if (command == "MOVETRANS") {
        if (ram.generated && ram.node && packet.vectors.size() >= 2) {
            const auto t = packet.vectors[0];
            const auto r = packet.vectors[1];
            Mat4 m;
            // Rotation about X, then Y, then Z (801298F0 applied in that order), then translation.
            auto rot = [](int axis, float angle) {
                Mat4 q;
                const float c = std::cos(angle), s = std::sin(angle);
                if (axis == 0) { q(1, 1) = c; q(1, 2) = s; q(2, 1) = -s; q(2, 2) = c; }
                else if (axis == 1) { q(0, 0) = c; q(0, 2) = -s; q(2, 0) = s; q(2, 2) = c; }
                else { q(0, 0) = c; q(0, 1) = s; q(1, 0) = -s; q(1, 1) = c; }
                return q;
            };
            if (r.x != 0) m = m * rot(0, r.x * kDegrees);
            if (r.y != 0) m = m * rot(1, r.y * kDegrees);
            if (r.z != 0) m = m * rot(2, r.z * kDegrees);
            m(3, 0) = t.x;
            m(3, 1) = t.y;
            m(3, 2) = t.z;
            ram.node->matrix = m;
        }
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
    if (command == "SETLABEL" || command == "SETEFFECT" || command == "SETSOUND" || command == "CHKBULLET" ||
        command == "GETTRCDATA" || command == "ACTION1" || command == "SETCONVEYOR" || command == "SETQUICKSAND" ||
        command == "SETSLIPPEDFLOOR" || command == "ONHITBULLETBOX" || command == "OFHITBULLETBOX") {
        engine.note("MODELCTRL " + std::string(command));
        return true;
    }
    // Any other command selects the named sequence of a generated model.
    if (ram.generated) select_sequence(engine, ram, command);
    return true;
}

void ModelCtrlManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.generated || !ram.wait_name) continue;
        const auto& wait = *ram.wait_name;
        bool done = false;
        if (wait == "Action") {
            Packet packet;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "ISCOMEND", packet);
            done = !packet.bools.empty() && packet.bools[0];
        } else if (wait == "TurnY" || wait == "Landing" || wait == "Moving" || wait == "Rotation") {
            done = true; // no motion channels on these models
        } else {
            engine.note("MODELCTRL wait " + wait);
        }
        if (done) {
            ram.wait_name.reset();
            run_commands(engine, ram);
        }
    }
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
                c.first = command.f32(3);
                c.loop = std::string(command.str(5));
            }
            action.commands.push_back(std::move(c));
        }
        actions->push_back(std::move(action));
    }
    auto* action_ptr = actions.get();
    actions_[reinterpret_cast<const void*>(std::uintptr_t(info.reference()) ^ reinterpret_cast<std::uintptr_t>(info.document()))] =
        std::move(actions);
    Node* switch_node = engine.scenes().node(info.ref(7));
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
        // Actor animation playback is handled by the actor runtime when present.
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
            ready = true; // no animation combiner on this RAM
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
