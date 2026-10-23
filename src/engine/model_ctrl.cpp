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

// The model class managers of the game plugin (80346F74: beNDMWMdlPlayer2,
// PBullet, EBullet, Object, Item, Effect, MapBase, MapCursor, Player, Enemy);
// each accepts the infos of its KIND and is scheduled by the first of them
// (Engine::update_managers). The rank is the class's schedule slot.
int update_rank(std::string_view kind) {
    static constexpr std::string_view kOrder[] = {"KIND_PLAYER2", "KIND_PBULLET", "KIND_EBULLET", "KIND_OBJECT", "KIND_ITEM",
                                                  "KIND_EFFECT",  "KIND_MAPBASE", "KIND_MAPCURSOR", "KIND_PLAYER", "KIND_ENEMY"};
    for (int i = 0; i < int(std::size(kOrder)); ++i)
        if (kind == kOrder[i]) return i;
    return 3; // an unknown kind as an object
}

// The named RAM timer "AITIMER" of the AIM map (0x80308868 / 0x803085B4)
// and the AIH homing (0x80308BAC / 0x80308BC0): 8031DE44 makes it on first
// use and restarts it with end = (int64)(1e9f * frames / 60f) (800A4130); the
// check 8031E238 counts a missing timer as expired, otherwise
// end < ((world >> 18) * scale << 18) + bias (8031E36C, signed 64-bit).
using ModelRam = ModelCtrlManager::ModelRam;
void start_ai_timer(ModelRam& ram, std::int64_t world, std::int64_t end) {
    if (!ram.ai_timer) ram.ai_timer.emplace();
    timer_start(*ram.ai_timer, world, end);
}
bool ai_timer_expired(const ModelRam& ram, std::int64_t world) {
    return !ram.ai_timer || timer_expired(*ram.ai_timer, world);
}

// The named RAM timer "KEIKATIMER": the shared move handler 8030A6BC
// (0x8030A6F8..0x8030A718, MOVE / MOVEEX / MOVERL) and MUTEKI 8030C1F0 make
// it when missing (8031E138, then 8031DE44 with end 0: the first query at
// that world time gives one frame).
void ensure_keika(ModelRam& ram, std::int64_t world) {
    if (ram.keika) return;
    ram.keika.emplace();
    dw4::start_motion_timer(*ram.keika, static_cast<std::uint64_t>(world), 0);
}
// 8031E4C4: the frames since the last query, (q(now) - previous) / 16666667
// capped at 3, 0 when the clock did not move, cached per world time. The
// original always has the timer here; making it is the port's fallback.
float keika_frames(ModelRam& ram, std::int64_t world) {
    ensure_keika(ram, world);
    return dw4::motion_timer_frames(*ram.keika, static_cast<std::uint64_t>(world));
}

// The turn of 8030C85C, not forced: an axis whose applied angle (+98)
// differs from the wanted one (+80) and whose rate (+8C: only the AIH
// homing sets the X rate; +90: the Y rate; +94 is never written) is not 0
// steps by rate * KEIKATIMER frames (0x8030C8E0). Both angles are reduced to
// [0, tau) with the stored tau (804268DC) by repeated float add/subtract;
// the shorter arc wins (equal: the negative one) and a remaining distance up
// to the step snaps to the reduced target, which is stored reduced. True
// when an applied angle changed (0x8030C9C8..0x8030CA0C: then the basis is
// rebuilt around the position).
bool turn_step(ModelRam& ram, std::int64_t world) {
    const auto before = ram.applied;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (ram.applied[axis] == ram.desired[axis]) continue;
        const float rate = axis == 0 ? ram.turn_rate_x : axis == 1 ? ram.turn_rate_y : 0.0f;
        if (rate == 0.0f) continue;
        const float delta = rate * keika_frames(ram, world);
        constexpr float tau = 6.2831854820251465f;
        auto reduce = [&](float angle) {
            // (Port guard: angles the float loop could not reduce would hang it.)
            if (!(std::abs(angle) < 1.0e6f)) angle = std::isfinite(angle) ? std::fmod(angle, tau) : 0.0f;
            while (angle < 0.0f) angle = angle + tau;
            while (angle >= tau) angle = angle - tau;
            return angle;
        };
        const float current = reduce(ram.applied[axis]), target = reduce(ram.desired[axis]);
        float positive, negative;
        if (current > target) {
            negative = current - target;
            positive = (target + tau) - current;
        } else {
            positive = target - current;
            negative = current - (target - tau);
        }
        if (positive >= negative) ram.applied[axis] = negative <= delta ? target : current - delta;
        else ram.applied[axis] = positive <= delta ? target : current + delta;
    }
    return ram.applied != before;
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
    // The full (document, reference) pair: an XOR of both collided across
    // documents and freed live data. An entry that already exists (the same
    // info again) is kept, since RAMs may point into it.
    const InfoKey info_key{static_cast<const void*>(info.document()), info.reference()};
    auto& seq_slot = sequences_[info_key];
    if (!seq_slot) seq_slot = std::move(sequences);
    auto* seq_ptr = seq_slot.get();
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
        auto& slot = hit_bodies_[info_key];
        if (!slot) slot = std::move(body);
        hit = slot.get();
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
        auto& slot = homings_[info_key];
        if (!slot) slot = std::move(h);
        homing = slot.get();
        break;
    }
    // "AIM" data (beModelCtrlInfoDataAIM [4]): the AI maps selected by AIMAP.
    std::vector<ObjectView>* ai_maps = nullptr;
    for (const auto& data : info.list(5)) {
        if (!data || data.str(2) != "AIM") continue;
        auto maps = std::make_unique<std::vector<ObjectView>>();
        for (const auto& map : data.list(4)) if (map) maps->push_back(map);
        auto& slot = ai_maps_[info_key];
        if (!slot) slot = std::move(maps);
        ai_maps = slot.get();
        break;
    }
    const std::string category = category_for_kind(info.str(7));
    const int rank = update_rank(info.str(7));
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
            // 8030F8CC: the RAM's own data ("ONE" node, "TRC" recorder).
            for (const auto& data : record.list(10)) {
                if (!data) continue;
                if (data.str(2) == "ONE" && !ram->node) ram->node = engine.scenes().node(data.ref(4));
                else if (data.str(2) == "TRC") ram->trace = engine.scenes().node(data.ref(4));
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
        // Group the RAMs by class (one slot each), then as accepted.
        const auto at = std::upper_bound(update_order_.begin(), update_order_.end(), rank,
                                         [](int r, const UpdateEntry& e) { return r < e.rank; });
        update_order_.insert(at, {rank, ram.get()});
        if (ram->is_player) player_rams_.push_back(ram.get());
        rams_.push_back(std::move(ram));
        rams_generation_ = next_rams_generation();
    }
}

void ModelCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    player_rams_.clear();
    rams_generation_ = next_rams_generation();
    update_order_.clear();
    ai_maps_.clear();
    item_labels_.fill(false);
    sequences_.clear();
    hit_bodies_.clear();
    homings_.clear(); // keyed by documents of the stage
}

ModelCtrlManager::ModelRam* ModelCtrlManager::find(std::string_view name, int index) {
    for (auto& ram : rams_)
        if (ram->info_name == name && ram->index == index) return ram.get();
    return nullptr;
}

namespace {
// 8030A71C: a position lock (+A8: a MOVERL follow or a locked effect form)
// ends; the facing it left on the node (the yaw of its +Z axis, 8012D434 /
// 800A2C74) becomes the controller's Y angles (+9C/+84), from which later
// frames rebuild the basis. Only +A8 is cleared: the attach node (+AC), the
// trace (+B0) and the bone names (+B4/+B8) stay.
void end_follow(ModelCtrlManager::ModelRam& ram) {
    if (!ram.pos_lock) return;
    ram.pos_lock = false;
    if (ram.node) {
        const auto& m = ram.node->matrix;
        ram.applied[1] = ram.desired[1] = std::atan2(m(2, 0), m(2, 2));
    }
}
// DW4_TRACE_LOCK=1 logs position locks: effect forms that lock, the move
// commands that end a lock, and locked models whose walls and floors run.
bool trace_lock() {
    static const bool on = std::getenv("DW4_TRACE_LOCK") != nullptr;
    return on;
}
void trace_unlock(Engine& engine, const ModelCtrlManager::ModelRam& ram, std::string_view type) {
    if (!trace_lock() || !ram.pos_lock) return;
    engine.log("lock " + ram.info_name + "#" + std::to_string(ram.index) + " ends by " + std::string(type) + " in " + ram.seq_name +
               (ram.attach_node ? " (effect form on " + ram.attach_name + "#" + std::to_string(ram.attach_index) + ")" : std::string()));
}
} // namespace

// 803108A0, the subcommand cleanup of every sequence start except SQ_ERAS and
// of REMOVE: each entry of the ctrl's list (+0xE4) gets its end call (vtable
// +0x64) in list order, an effect entry (meta 80534C20) only when it is tied
// to the sequence (+0x38); a started one erases its effect RAM with SQ_ERAS,
// a hitbox's end call is empty. Then every entry is released and the count
// set to 0 (0x80310984..0x80310A50).
void ModelCtrlManager::release_subcommands(Engine& engine, ModelRam& ram) {
    for (const auto& effect : ram.effects)
        if (effect.sequence_dead && effect.started && effect.ram >= 0) {
            Packet erase;
            engine.messenger().send(engine, "MODELCTRL", effect.model, effect.ram, "SQ_ERAS", erase);
        }
    ram.effects.clear();
    ram.hitboxes.clear();
    ++ram.hitbox_generation;
}

void ModelCtrlManager::select_sequence(Engine& engine, ModelRam& ram, std::string_view name) {
    // DW4_TRACE_SEQ=<model> logs that model's sequence changes (=* every
    // model's, to set beside the Dolphin S log of 8030E978).
    if (static const char* trace = std::getenv("DW4_TRACE_SEQ"); trace && (ram.info_name == trace || std::string_view(trace) == "*"))
        engine.log("seq " + ram.info_name + "#" + std::to_string(ram.index) + " " + ram.seq_name + " -> " + std::string(name) +
                   (ram.node ? " at (" + std::to_string(int(ram.node->matrix(3, 0))) + "," + std::to_string(int(ram.node->matrix(3, 1))) + "," +
                                   std::to_string(int(ram.node->matrix(3, 2))) + ")"
                             : std::string()));
    // 0x8030E884..0x8030E8C4: the command "SQ_ERAS" - a JUMP is the command
    // handler too (8030BCB8 -> vtable +8C) - first runs the class's erase
    // hook (vtable +B4) for a generated model, then starts the sequence
    // (8030F530). Only the player class (vtable 804E7DAC: 8034E3C0) and the
    // item class (804EDC5C: 803ABE0C, its name label) have one; the other
    // model classes keep the empty 8030F5D8 (scan of every vtable with the
    // update 8030CB2C at +0x84).
    //
    // That is all SQ_ERAS does: no subcommand cleanup (803108A0) and no end
    // of a sequence-long invulnerability. The queued effects and hitboxes
    // stay for the rest of this update, whose subcommand pass (803107E0 at
    // 0x8030D008) still runs after the wait (0x8030CFEC) that resumed the
    // command list; REMOVE drops them on the model's next update (0x8030CC58
    // -> 803108A0 at 0x8030DC68). x_200mind03, the invisible carrier of the
    // Bind / Sleep / Freeze / Pwr Dwn techniques, lands, starts ACTION ATCK0n,
    // whose action starter queues x_200mind02 SQ_ATCKnn with delay 0 (the
    // gold ring with the bullet box), and jumps to SQ_ERAS at once: the ring
    // starts in that frame (Dolphin: ring 12 frames after BINDD1, the enemy
    // bound 6 frames later). The port used to clear the list here, so these
    // four technique families never showed their ring or hit.
    if (name == "SQ_ERAS") {
        if (ram.generated) {
            if (ram.is_player) player_erase_hook(engine, ram);
            else if (ram.category == "ITEM") item_erase_hook(engine, ram);
        }
        ram.seq_name = "SQ_ERAS";
        ram.seq = nullptr;
        ++ram.sequence_serial;
        ram.wait_name.reset();
        return;
    }
    // 803108A0, every other sequence start (0x8030E9D0, SQ_ERAS2 0x8030E920).
    release_subcommands(engine, ram);
    if (name == "SQ_ERAS2") {
        Packet packet;
        engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "NDSP", packet);
        // 0x8030E934: 8031EA54 drops every named timer of the RAM, as REMOVE.
        ram.ai_timer.reset();
        ram.attack_delay.reset();
        ram.keika.reset();
        ram.named_timers.clear();
        return;
    }
    const Sequence* found = nullptr;
    if (ram.sequences)
        for (const auto& s : *ram.sequences) if (s.name == name) { found = &s; break; }
    ram.seq_name = std::string(name);
    ram.seq = found;
    ++ram.sequence_serial;
    ram.cursor = 0;
    ram.label_passed = false;
    ram.wait_name.reset();
    // 0x8030EA0C: an ordinary start ends a sequence-long invulnerability
    // (MUTEKI ON -1); SQ_ERAS and SQ_ERAS2 keep it.
    if (ram.muteki < 0) ram.muteki = 0;
    // Ordinary entry: mode/phase and PDMOVE count reset, flagged channel stops.
    ram.ctrl_mode = 0;
    ram.ctrl_phase = 0;
    ram.pdmove_set = false;
    // 8030E9D4: every sequence start drops the LUACALL BHit/WHit/FHit
    // scripts (ctrl +30/+34/+38); a stale one would run in a later
    // sequence (a wall touched while dying jumped e_mecha4 back to an
    // attack, leaving it alive at 0 HP).
    ram.lua_bhit.clear();
    ram.lua_whit.clear();
    ram.lua_fhit.clear();
    // 8030E9F8: every sequence starts with wall, floor and fall checks on.
    ram.wall_check = ram.floor_check = ram.fall_check = true;
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
    // 8030EA8C: the sequence-change hook (vtable +B8) after the commands ran;
    // a JUMP selects through the same path (8030BCB8 -> vtable +8C, 8030D910),
    // so a jumped-to sequence runs the hook before the outer one does.
    if (ram.is_player) player_sequence_hook(engine, ram);
    else if (ram.category == "ENEMY") status_sequence_check(engine, ram);
    else if (ram.category == "ITEM") item_sequence_hook(engine, ram);
}

// beNDMWMdlPlayer +B8 (8034E1B0): standing in SQ_WAIT, the player's number
// icon i_pcnumic (SQ_OPENP<ram>) follows 480 frames later, 10 above the
// model (SETEFFECT, ends with the sequence, attached); a hit, death, sleep
// or stun (PanelCloseSeqList 804E2738) takes an open quick panel down; then
// the status check (803AE1A4).
void ModelCtrlManager::player_sequence_hook(Engine& engine, ModelRam& ram) {
    if (ram.seq_name == "SQ_WAIT") {
        Packet effect;
        effect.strings.emplace_back("i_pcnumic");
        effect.strings.emplace_back("SQ_OPENP" + std::to_string(ram.index));
        effect.floats.push_back(480.0f);       // 80457290
        effect.vectors.push_back({0, 10, 0});  // 80456F68 / 80457294
        effect.bools = {1, 1};
        engine.messenger().send(engine, "MODELCTRL", ram.info_name, ram.index, "SETEFFECT", effect);
    }
    static constexpr std::string_view kPanelClose[] = {"SQ_SDMG", "SQ_MDMG", "SQ_LDMG", "SQ_DDMG", "SQ_DEAD", "SQ_SLEP", "SQ_STUN"};
    if (ram.panel_open && std::find(std::begin(kPanelClose), std::end(kPanelClose), ram.seq_name) != std::end(kPanelClose)) {
        ram.panel_open = false;
        Packet remove;
        engine.messenger().send(engine, "NDMWPANEL", std::nullopt, ram.index, "REMOVE", remove);
    }
    status_sequence_check(engine, ram);
}

void ModelCtrlManager::run_commands(Engine& engine, ModelRam& ram) {
    // DW4_DUMP_SEQ=<model> (research): every sequence of that model with its
    // commands' types and first string fields, once.
    if (static const char* dump = std::getenv("DW4_DUMP_SEQ"); dump && ram.info_name == dump && ram.sequences) {
        static std::vector<std::string> dumped;
        if (std::find(dumped.begin(), dumped.end(), ram.info_name) == dumped.end()) {
            dumped.push_back(ram.info_name);
            for (const auto& sequence : *ram.sequences) {
                engine.log("dumpseq " + ram.info_name + " " + sequence.name);
                for (const auto& c : sequence.commands)
                    engine.log("dumpseq     " + std::string(c.type()) + " " + std::string(c.str(2)) + " | " + std::string(c.str(3)) + " | " +
                               std::string(c.str(4)));
            }
        }
    }
    // 8030C3AC loads the command list (r24 = ctrl +0x18) and the index (r30 =
    // ctrl +0x1C) ONCE (0x8030C3D8 / 0x8030C3DC) and walks that list until the
    // index reaches its count (0x8030C82C, 'index != count'), a handler
    // returns 0 (only WCHECK, 8030BCFC) or a LABEL comes after the label
    // section started (+0x20 set: stop without advancing); every other
    // handler returns 1 and the index advances after it. At the end the index
    // is stored into ctrl +0x1C (0x8030C844 'stw r30,0x1c(r29)'), skipped only
    // after a JUMP (8030BCB8 returns 1 into r25, 0x8030C818).
    //
    // A sequence started from inside a handler (MDLsetSeq in a LUACALL script
    // or the AIMAP WPos script, a model command an ACTION start sends back)
    // runs nested through 8030D910 -> 0x8030EA88 on the same ctrl and stores
    // its own index, but LUACALL (8030B8A4, single exit 0x8030BB28) and AIMAP
    // (8030BB3C, 0x8030BC9C) return 1: the outer loop goes on with the OLD
    // list (its ACTION, MUTEKI, WCHECK ... act on the model as usual) and its
    // final store overwrites the NEW sequence's cursor. Lucemon's SQ_CHACK1
    // ends with LUACALL rl2/chack1, whose script starts SQ_SWITCH7; SWITCH7
    // inherits index 4 and runs MUTEKI OFFWALL/OFFFLOOR, ACTION ACTN02 and
    // WCHECK a second time when its first wait ends (its rise takes 202
    // fields, not 201).
    const Sequence* const list = ram.seq;
    if (!list) return;
    std::size_t at = ram.cursor;
    bool jumped = false;
    bool more = true;
    const auto serial = ram.sequence_serial;
    // (Port guard: an inherited index past the end of a shorter list stops;
    // the original's '!=' compare would read past the list.)
    while (more && at < list->commands.size()) {
        const auto& command = list->commands[at];
        const auto& type = command.type();
        const std::string name(command.str(2));
        if (type == "beModelCtrlACTION") {
            Packet packet;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, name, packet);
            ++at;
        } else if (type == "beModelCtrlWCHECK") {
            // 8030BCFC: the wait name (ctrl +0x4C), returns 0.
            ram.wait_name = name;
            ++at;
            more = false;
        } else if (type == "beModelCtrlJUMP") {
            select_sequence(engine, ram, name);
            jumped = true;
            break;
        } else if (type == "beModelCtrlLABEL") {
            // Inline at 0x8030C7BC: +0x20 set -> stop here (index kept).
            if (ram.label_passed) {
                more = false;
            } else {
                ram.label_passed = true;
                ++at;
            }
        } else if (type == "beModelCtrlMSGBOX") {
            ++at; // 8030BCB0: returns 1
        } else if (type == "beModelCtrlCTRL") {
            // 80308EC0: exact control names.
            if (name == "onWalk") { ram.ctrl_mode = 1; ram.ctrl_phase = 0; }
            else if (name == "toWalk") { ram.ctrl_mode = 2; ram.ctrl_phase = 0; }
            else if (name == "toWait") { ram.ctrl_mode = 3; ram.ctrl_phase = 0; }
            else if (name == "isLive") { ram.ctrl_mode = 4; ram.ctrl_phase = 0; }
            else if (name == "onFall") ram.fall_check = true;
            else if (name == "noFall") ram.fall_check = false;
            ++at;
        } else if (type == "beModelCtrlPDMOVE") {
            // 8030AEE0: Stop/Move parameter pairs.
            if (!ram.pdmove_set) { ram.pdmove = {0, 0, 0, 0}; ram.pdmove_set = true; }
            if (name == "Stop") { ram.pdmove[0] = command.f32(3); ram.pdmove[1] = command.f32(4); }
            else if (name == "Move") { ram.pdmove[2] = command.f32(3); ram.pdmove[3] = command.f32(4); }
            ++at;
        } else if (type == "beModelCtrlMOVERL") {
            // 8030ACBC: JOINT takes the model's trace data (GETTRCDATA, the
            // recorder under its animated route: boats, trains, rails) and
            // locks only when exactly one came back (0x8030ACE4..0x8030AD44);
            // anything else (ACTOR) keeps the model and bone names and locks
            // (0x8030AD4C..0x8030AE40). The shared move handler 8030A6BC first
            // makes a missing KEIKATIMER, ends a running lock (8030A71C) and
            // gives an empty channel list its four move objects (0x8030A81C),
            // as for MOVE: a model whose life starts with MOVERL has four
            // inactive channels, so once it is unlocked (a JOINT without trace
            // data) 8030A448 no longer returns at once: it queries the
            // KEIKATIMER and turns the model (a player by its stick).
            ensure_keika(ram, engine.time());
            trace_unlock(engine, ram, type);
            end_follow(ram);
            try {
                ram.channels.allocate();
            } catch (const std::exception& error) {
                engine.note(std::string("MODELCTRL MOVERL channels: ") + error.what());
            }
            if (name != "JOINT") {
                ram.follow_model = std::string(command.str(3));
                ram.follow_bone = std::string(command.str(4));
                ram.pos_lock = true;
            } else {
                Packet trace;
                engine.messenger().send(engine, "MODELCTRL", std::string(command.str(3)), ram.index, "GETTRCDATA", trace);
                if (trace.objects.size() == 1) {
                    ram.follow_trace = static_cast<Node*>(trace.objects[0]);
                    ram.pos_lock = true;
                }
                if (static const bool trace_log = std::getenv("DW4_TRACE_FOLLOW") != nullptr; trace_log)
                    engine.log("follow " + ram.info_name + "#" + std::to_string(ram.index) + " trace of " +
                               std::string(command.str(3)) + (trace.objects.size() == 1 ? "" : " (none)"));
            }
            ++at;
        } else if (type == "beModelCtrlMOVE" || type == "beModelCtrlMOVEEX") {
            // 8030A6BC (the handler of MOVE, MOVEEX and MOVERL): a missing
            // KEIKATIMER is made (0x8030A6F8..0x8030A718); a new move ends a
            // position lock (8030A71C: a MOVERL follow or a locked effect
            // form, which then moves on from where it was placed); an empty
            // channel list gets its four move objects (0x8030A81C).
            ensure_keika(ram, engine.time());
            trace_unlock(engine, ram, type);
            end_follow(ram);
            try {
                ram.channels.allocate();
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
            ++at;
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
            ++at;
        } else if (type == "beModelCtrlMUTEKI") {
            // 8030C25C: OFFWALL / ONWALL / OFFFLOOR / ONFLOOR switch the wall
            // and floor checks; otherwise the invulnerable frames ("ON" -1:
            // for the rest of the sequence; "OFF" ends it). 8030C1F0 first
            // makes a missing KEIKATIMER (0x8030C224..0x8030C244).
            ensure_keika(ram, engine.time());
            if (name == "OFFWALL") ram.wall_check = false;
            else if (name == "ONWALL") ram.wall_check = true;
            else if (name == "OFFFLOOR") ram.floor_check = false;
            else if (name == "ONFLOOR") ram.floor_check = true;
            else ram.muteki = name == "OFF" ? 0.0f : command.f32(3);
            ++at;
        } else if (type == "beModelCtrlLABELCTRL") {
            // The label section a later sequence starts from (like SETLABEL).
            ram.labels[std::string(command.str(3))] = name;
            ++at;
        } else if (type == "beModelCtrlAIMAP") {
            // 8030BB3C: select the AI map by name; a WPOS map asks
            // aim_<model>.lua (call name "WPos") for its world target. The
            // AITIMER ends so the next update picks from the new map. A
            // sequence the script starts changes nothing here: the target is
            // stored, the AITIMER removed and 1 returned (0x8030BC9C).
            if (ram.ai_maps)
                for (std::size_t i = 0; i < ram.ai_maps->size(); ++i)
                    if ((*ram.ai_maps)[i].str(2) == name) ram.ai_map = int(i);
            if (ram.ai_maps && ram.ai_map >= 0 && ram.ai_map < int(ram.ai_maps->size()) &&
                (*ram.ai_maps)[std::size_t(ram.ai_map)].str(11) == "WPOS") {
                Vec3f target{};
                ModelScriptContext context;
                context.category = ram.category;
                context.name = ram.info_name;
                context.index = ram.index;
                context.call_name = "WPos";
                context.world_target = &target;
                engine.push_model_context(std::move(context));
                engine.run_script("aim_" + ram.info_name + ".lua");
                engine.pop_model_context();
                ram.ai_world_target = target;
            }
            ram.ai_timer.reset(); // 0x8030BC94: 8031E8BC removes the AITIMER
            ++at;
        } else if (type == "beModelCtrlLUACALL") {
            // 8030B8A4: BHit, WHit and FHit keep the name for after this
            // model's hitbox hits something, it meets a wall or the floor
            // (bullets erase themselves). Any other mode runs
            // "<name>_<info>.lua" now with the mode as MDLisCallName's call
            // name (e.g. "swof", "magic", "setaid"). Always returns 1
            // (0x8030BB28), also when the script started another sequence.
            const auto mode = command.str(3);
            if (mode == "BHit") {
                ram.lua_bhit = name;
            } else if (mode == "WHit") {
                ram.lua_whit = name;
            } else if (mode == "FHit") {
                ram.lua_fhit = name;
            } else {
                run_model_script(engine, ram, name, mode);
            }
            ++at;
        } else {
            // (The original has a handler for every authored command type; an
            // unknown one would repeat forever there. The port skips it.)
            engine.note("MODELCTRL command " + type);
            ++at;
        }
    }
    if (jumped) return;
    // DW4_TRACE_SEQINT=1 logs every sequence started inside the loop whose
    // cursor the old list's index overwrites.
    if (static const bool trace = std::getenv("DW4_TRACE_SEQINT") != nullptr; trace && ram.sequence_serial != serial)
        engine.log("seqint " + ram.info_name + "#" + std::to_string(ram.index) + " " + list->name + " left at " + std::to_string(at) +
                   " -> " + ram.seq_name + " cursor " + std::to_string(ram.cursor) + " := " + std::to_string(at) +
                   (ram.wait_name ? " wait " + *ram.wait_name : std::string()));
    ram.cursor = at;
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
        // _mHp / _hp (info work +0x08 / +0x0C) are new beDataObjInt objects
        // of the fresh work (construct flags 802C6D84 / 802C6DA4; their int
        // has no default, 802DD7B4) and _recNo (+0x10) defaults to -1
        // (802C6DA8..802C6DB0). Only the enemy hook fills them from the
        // generator ints (0x8034BCC0..0x8034BCEC); a max HP still 0 after the
        // hook becomes 100 / 100 (0x8030DA60..0x8030DA78). A life no longer
        // keeps the last one's HP or record.
        ram.hp = ram.max_hp = 0;
        ram.record = -1;
        // The item GENERATE hook 803AAC90 makes a fresh item work (8033A8A8:
        // no label, blink phase and "COLOR" timer gone). A label still shown
        // would keep its line taken in the original; the port drops it (the
        // erase hook at SQ_ERAS normally did that already).
        if (ram.item_label >= 0) item_erase_hook(engine, ram);
        ram.item_lit = false;
        ram.item_color_timer.reset();
        ram.item_table = -1;
        // Drop form (802FCE60): I<record>/I<drop table id>.
        if (ram.category == "ITEM" && packet.ints.size() >= 2) {
            ram.item_record = packet.ints[0];
            ram.item_table = packet.ints[1];
            ram.item_label = -1;
            int level = 1;
            if (auto* sys = engine.get<GameSystem>()) level = std::max(1, sys->players[0].stats.level);
            ram.item_device = roll_item_device(ram.item_table, ram.item_record, level);
        } else if (packet.ints.size() >= 2 && ram.category != "EFFECT" && ram.category != "PBULLET" && ram.category != "EBULLET") {
            // Generator form: I<record>/I<max HP> (802FC9FC). The effect and
            // bullet kinds (8034B928 / 8034C134 / 8034B730 -> 8030D260) read
            // their ints as source and owner RAM, never as record or HP.
            ram.record = packet.ints[0];
            ram.hp = ram.max_hp = std::max(1, packet.ints[1]);
        }
        if (ram.max_hp == 0) ram.hp = ram.max_hp = 100;
        ram.muteki = 0;
        // The KIND vtable +0x98 hook (8034BB48 enemy, 8034BEA8 object,
        // 8034D61C player) installs a fresh info work at ONE+0x14 on every
        // GENERATE, so _isOnCtrl (+0x44) and _isOnFreeze start at their
        // defaults again (802C6D18), also after an OFCTRL before an erase.
        // The SETLABEL/LABELCTRL lists (info +0x24/+0x28/+0x2C, 8030B014;
        // read at a sequence start by 8030D084) start empty with it: a
        // respawned player runs SQ_ENTR's unlabelled 'dual' section until the
        // weapon's labels come with the posted PLAYERARMS GENERATE.
        ram.control_enabled = true;
        ram.frozen = false;
        // _directionLimiter (+0x8C X, +0x90 Y, +0x94 Z rate) is field 30 of
        // the info work without a default in 802C6D18, so every fresh work
        // (802C6340 base, 8033B040 enemy, 803387A0 capsule, ...) starts at
        // (0, 0, 0); only the player hook writes (0, 7, 0) right after
        // making its work (8034D77C..8034D790: 80456F68 = 0, 8045700C = 7).
        // A rotating channel (1.25 * step) or an AIM map rate left from the
        // last life does not survive the GENERATE.
        ram.turn_rate_x = 0.0f;
        ram.turn_rate_y = ram.is_player ? 7.0f : 0.0f;
        ram.aim_height = 0.0f;
        ram.labels.clear();
        ram.ride.reset();
        ram.capsule_on = ram.capsule_ended = false;
        ram.capsule_age = 0;
        // The named RAM timers (AITIMER, ATDELAY, KEIKATIMER) live in the
        // RAM's data list (+0x28), not in the info work: they stay over a
        // GENERATE (8034B928 / 8030D260 do not touch them); REMOVE and
        // SQ_ERAS2 drop them (8031EA54).
        ram.ai_request.clear();
        // _svLODHighRange (+0xA4) lives in the info work, made anew by every
        // GENERATE with -1 (0x8041ED64).
        ram.lod_high_saved = -1.0f;
        ram.ai_map = 0; // 8030DB3C: a new beModelCtrlInfoAI (_mapNo 0, no world target)
        ram.ai_world_target = {};
        ram.homing_state = ram.homing_steps = 0;
        ram.follow_model.clear();
        ram.follow_bone.clear();
        ram.follow_trace = nullptr;
        // _isPosLock (+A8), the attach node (+AC) and the translated byte
        // (+78) start at 0 / null with the fresh work.
        ram.pos_lock = false;
        ram.attach_node = nullptr;
        ram.translated = false;
        // _moveObjectList (info work +0xC8, field 39) is an object field with
        // the construct flag (802C6F54..802C6F70, meta 802C840C), so the fresh
        // work owns a NEW beModelCtrlMoveObjectList with count 0, not the old
        // channels. The first MOVE / MOVEEX / MOVERL of this life sees the
        // count 0 (8030A6BC, 0x8030A820) and makes four new move objects
        // (802C85DC at 0x8030A890 / 900 / 970 / 9E0) whose fields all start
        // at 0 (802C876C: _endTime, _value, _acc, _speed 0.0; the rest has no
        // default); 8030A1F4 sets the speed (+1C) only in modes 4 / 5, so a
        // 'Move' accelerates from 0 again. Until then 8030A448 finds the
        // count 0 and neither queries the KEIKATIMER nor steps or turns
        // (0x8030A484..0x8030A48C). Megadramon's bolt x_027emagc01a#1, made
        // again every MAGIC cycle, kept the last bolt's 0.75 and hit 23-25
        // frames early (dng411b1, dng412b1, dngq6b01).
        if (auto list = ram.channels.list_owner()) list->mutable_entries().clear();
        // The rest of the fresh work: _keyInput (+0x70) has no default; the
        // port's single-string SETLABEL label belongs to the label lists
        // above; _subCommand (+0xE4) is a new beModelCtrlSubCom whose list is
        // constructed empty (802C7020..802C703C, 802C5484), so the sequence
        // cleanup of this GENERATE finds no subcommand of the last life (no
        // SQ_ERAS for its sequence-dead effects, no queued hitbox).
        ram.key_input = false;
        ram.label.clear();
        ram.effects.clear();
        ram.hitboxes.clear();
        ++ram.hitbox_generation;
        // beNDMWMdlPEBaseInfoWork (players and enemies, 803398B0) holds the
        // status work: _dispRamNoList, _dispPosNoList, _timerList,
        // _isFirstList, _downDataList and _upDataList (+0xF0..+0x10C) are
        // null and _poisonDamage / _poisonEndurance (+0x100 / +0x104) 0 in
        // every fresh work; the next status command makes them (803AEBB4).
        // An enemy remade in a RAM no longer inherits the DOWN / UP values,
        // poison damage or icons of the last one.
        ram.status.reset();
        if (ram.is_player) {
            // beNDMWMdlPlayerInfoWork (80339494): the player hook gives
            // _turnAtkTimer (+0x114) a new list of eight zeros (8034D7B8..
            // 8034D864); _bodyAtkSt0 / _bodyAtkPad / _bodyAtkTimer (+0x118 /
            // +0x11C / +0x120, the Slam double tap), _isPanel (+0x124) and
            // _wCheckSt / _wCheckMode (+0x128 / +0x12C, the A combo) have no
            // default. A respawned player starts without the last life's
            // Spiral history, Slam tap or combo phase.
            ram.spin.fill(0);
            ram.slam_state = 0;
            ram.slam_direction = 0;
            ram.slam_timer = 0.0f;
            ram.panel_open = false;
            ram.combo_phase = 0;
            ram.combo_released = false;
        }
        if (ram.is_player) {
            if (auto* sys = engine.get<GameSystem>()) {
                // 0x8034DA7C..0x8034DB24: the hook clears GAMERAM UPDATA[0..6]
                // and DOWNDATA[0..5] (804E7C68 / 804E7C6C = 0) before
                // RECALCPLAYER (0x8034DCF4): the status values of the last
                // life (its fresh status work no longer times them) go.
                auto& gameram = sys->players[std::size_t(std::clamp(ram.index, 0, 3))];
                std::fill_n(gameram.updata.begin(), 7, 0);
                std::fill_n(gameram.downdata.begin(), 6, 0);
                sys->prepare_player(engine, ram.index);
                const auto& slot = sys->players[std::size_t(std::clamp(ram.index, 0, 3))];
                const auto& stats = slot.stats;
                // A defeated player comes back with full HP (DEADRESTART).
                if (slot.hp <= 0) sys->players[std::size_t(std::clamp(ram.index, 0, 3))].hp = slot.max_hp;
                ram.max_hp = std::max(1, slot.max_hp);
                ram.hp = std::max(1, slot.hp);
                // A direct stage start made the player only now, after
                // boot.lua listed the weapon packages: load them.
                if (auto* weapons = engine.get<WeaponManager>(); weapons && sys->direct_start)
                    for (const auto& weapon : slot.weapons)
                        if (!weapon.empty() && !weapons->labels(weapon)) engine.load_common_now("_common/" + weapon + ".igb");
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
                // 0x8034DEA4..0x8034DED8: in the five stages of 804E7C80
                // (strcmp 8009D5D8) the hook sends PLAYER <ram> SETMETAL, so a
                // player respawned there is metal again like at the stage
                // start (the stage scripts' funcStageIn sends it as well).
                static constexpr std::string_view kMetalStages[] = {"dng204b1", "dng204b2", "dng205b1", "dng205b2", "dng205b3"};
                std::string stage = engine.stage_name();
                std::transform(stage.begin(), stage.end(), stage.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                if (std::find(std::begin(kMetalStages), std::end(kMetalStages), stage) != std::end(kMetalStages)) {
                    Packet metal;
                    engine.messenger().send(engine, "PLAYER", std::nullopt, ram.index, "SETMETAL", metal);
                }
            }
        }
        ram.attach_name.clear();
        ram.attach_index = -1;
        ram.owner_name.clear();
        ram.owner_index = -1;
        // Effect form (8030D260, sent by 80310340 and by scripts): ints source
        // RAM, owner RAM; strings source name, sequence, owner name; offset
        // and rotation relative to the source, position lock. Only the
        // effect and bullet kinds run it (their GENERATE hooks 8034B730 /
        // 8034B928 / 8034C134 call 8030D260), and it applies as soon as the
        // packet has two strings (0x8030D368 / 0x8030D378). The source places
        // the effect (GETMATRIX of string0/int0, 8030D4A8..8030D4BC) and the
        // lock follows it (+0xAC, 8030D4C0..8030D530); string2/int1 name the
        // owner for the bullets (work +0xDC/+0xE0, 8030D468..8030D4A4, read
        // by 8030FEEC). A missing owner string reads as none; the original
        // reads int1 past the list's end, the port takes -1 (no owner).
        const bool effect_kind = ram.category == "EFFECT" || ram.category == "PBULLET" || ram.category == "EBULLET";
        // The object hook 8034BEA8 instead takes string0 / int0 of a packet
        // with ints as the owner (+0xDC/+0xE0, 0x8034BFF8..0x8034C09C) and
        // leaves the placement to 8030D610 (world coordinates): Lucemon's
        // x_035frunn01a grid "f_luce/I<0>/V<-25,10,25>/V<0,0,0>/SQ_BOM"
        // (KIND_OBJECT, sent to a free RAM with index -2) stands at
        // (-25, 10, 25) and hits for f_luce; Apocalymon's f_bit
        // "f_apocaly/I<0>/V<0,30,0>/V<0,90,0>/SQ_ENTR" belongs to f_apocaly.
        if (ram.info.str(7) == "KIND_OBJECT" && !packet.ints.empty() && !packet.strings.empty()) {
            ram.owner_name = std::string(packet.string(0));
            ram.owner_index = packet.ints[0];
        }
        if (ram.node && effect_kind && packet.strings.size() >= 2) {
            ram.attach_name = std::string(packet.string(0));
            ram.attach_index = packet.ints.empty() ? 0 : packet.ints[0];
            ram.owner_name = packet.strings.size() > 2 ? std::string(packet.string(2)) : std::string();
            ram.owner_index = packet.ints.size() > 1 ? packet.ints[1] : -1;
            ram.attach_offset = packet.vectors.empty() ? Vec3f{} : packet.vectors[0];
            ram.aim_height = ram.attach_offset.y; // 0x8030D5E0: info work +0x54
            ram.attach_rotation = packet.vectors.size() > 1 ? packet.vectors[1] : Vec3f{};
            place_attached(ram);
            // The lock (bool 0, 80306A1C at 0x8030D40C) keeps the source's
            // node from the GETMATRIX reply (object 0: only a generated model
            // answers) as +AC and the offset as +BC..+C4, and sets _isPosLock
            // (8030D4C0..8030D530). The rotation is applied only here: the
            // later placements (8030CD54) take the source matrix and the
            // offset alone.
            if (packet.boolean(0)) {
                const auto* source = find(ram.attach_name, ram.attach_index);
                ram.attach_node = source && source->generated ? source->node : nullptr;
                ram.pos_lock = true;
                if (trace_lock()) {
                    const Vec3f r = ram.attach_rotation;
                    char line[256];
                    std::snprintf(line, sizeof line, "lock %s#%d %s on %s#%d%s offset (%.2f, %.2f, %.2f) rotation (%.3f, %.3f, %.3f)",
                                  ram.info_name.c_str(), ram.index, seq.c_str(), ram.attach_name.c_str(), ram.attach_index,
                                  ram.attach_node ? "" : " (no node)", ram.attach_offset.x, ram.attach_offset.y, ram.attach_offset.z,
                                  r.x, r.y, r.z);
                    engine.log(line);
                }
            }
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
    // The player class hooks of OFCTRL (+0xA8 8034DF40), ONFREEZE (+0xAC
    // 8034E078) and REMOVE (+0xB0 8034E594) close an open quick panel
    // (ctrl +0x124) with NDMWPANEL REMOVE, for a generated model.
    auto close_panel = [&] {
        if (!ram.is_player || !ram.generated || !ram.panel_open) return;
        ram.panel_open = false;
        Packet remove;
        engine.messenger().send(engine, "NDMWPANEL", std::nullopt, ram.index, "REMOVE", remove);
    };
    if (command == "REMOVE") {
        if (ram.generated) {
            close_panel(); // 0x8030DC1C, before ACTION NDSP
            Packet p;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, "NDSP", p);
            // 0x8030DC68: the subcommand cleanup 803108A0 (a model in SQ_ERAS
            // kept its list until now; its started sequence-dead effects go here).
            release_subcommands(engine, ram);
            ram.generated = false;
            ram.active = false;
            ram.seq = nullptr;
            ram.seq_name.clear();
            ram.wait_name.reset();
            ram.ride.reset();
            // 0x8030DC88: 8031EA54 drops every named timer of the RAM.
            ram.ai_timer.reset();
            ram.attack_delay.reset();
            ram.keika.reset();
            ram.named_timers.clear();
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
    // Case 16: the record of the control work (+10), zero unless a
    // generator GENERATE set it; only a generated model answers (8030D910
    // case 16 tests the RAM's +0x14 flag, which GENERATE sets and REMOVE
    // clears, and its control work).
    if (command == "GETRECNO") {
        if (ram.generated) packet.ints.push_back(std::max(ram.record, 0));
        return true;
    }
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
        // 8030E0AC / 8030E0D0: only a generated model's _isOnCtrl changes.
        if (ram.generated) ram.control_enabled = command == "ONCTRL";
        if (command == "OFCTRL") close_panel(); // 0x8030E0F8
        return true;
    }
    if (command == "SETLODHIGH" || command == "RESETLODHIGH") {
        // 0x8030E1D4 SETLODHIGH: when the "ONE" node's first child is an
        // igLod with three children (Kage/High/Low), range[2] goes to the
        // ctrl work (+0xA4, only while that holds -1) and becomes 1e8
        // (0x804DEBAC, if the list has more than two entries): High up to
        // 10000 units. 0x8030E29C RESETLODHIGH puts the kept value back and
        // stores -1. Both reset the selection cache (vtable +0x7C, 801F7CB8).
        // No shipped script or data sends either command.
        Node* lod = ram.generated && ram.node && !ram.node->children.empty() ? ram.node->children[0] : nullptr;
        if (!lod || lod->kind != NodeKind::lod) return true;
        if (command == "SETLODHIGH") {
            if (ram.lod_high_saved != -1.0f || lod->children.size() != 3) return true;
            ram.lod_high_saved = lod->lod_ranges.size() > 2 ? lod->lod_ranges[2] : 0.0f;
            if (lod->lod_ranges.size() > 2) lod->lod_ranges[2] = 1.0e8f;
        } else {
            if (ram.lod_high_saved == -1.0f) return true;
            if (lod->lod_ranges.size() > 2) lod->lod_ranges[2] = ram.lod_high_saved;
            ram.lod_high_saved = -1.0f;
        }
        lod->reset_lod();
        return true;
    }
    if (command == "ONFREEZE" || command == "OFFREEZE") {
        ram.frozen = command == "ONFREEZE" && ram.generated;
        if (command == "ONFREEZE") close_panel(); // 0x8030E168
        if (ram.generated) {
            Packet p;
            engine.messenger().send(engine, "ACTION", ram.info_name, ram.index, std::string(command), p);
        }
        return true;
    }
    if (command == "MOVETRANS") {
        if (ram.generated && ram.node && packet.vectors.size() >= 2) {
            ram.node->matrix = placement_matrix(packet.vectors[0], packet.vectors[1]);
            // 8030E400: desired Y = rotY; 8030C85C (rebuild) sets the basis
            // from the desired angles and keeps them as applied, so the yaw
            // holds and X/Z of the rotation are dropped.
            ram.desired[1] = packet.vectors[1].y * 0.0174533f;
            ram.applied = ram.desired;
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
        return true;
    }
    if (command == "SETMATRIX") {
        if (ram.generated && ram.node && !packet.matrices.empty()) {
            ram.node->matrix = packet.matrices[0];
            // 8030DF50: applied/desired Y from the matrix's +Z axis.
            const auto& m = ram.node->matrix;
            ram.applied[1] = ram.desired[1] = std::atan2(m(2, 0), m(2, 2));
        }
        return true;
    }
    if (command == "GETMATRIX" || command == "GETMATRIX_ALL") {
        // 8030D910 cases 10 / 11: index, matrix and node (GETMATRIX only of a
        // generated model).
        if (command == "GETMATRIX" && !ram.generated) return true;
        packet.ints.push_back(ram.index);
        packet.matrices.push_back(ram.node ? ram.node->matrix : Mat4{});
        packet.objects.push_back(ram.node);
        return true;
    }
    if (command == "GETMATRIX_ACTIVE") {
        // Case 12: only the matrix, of a generated model not invulnerable.
        if (ram.generated && ram.muteki == 0.0f) packet.matrices.push_back(ram.node ? ram.node->matrix : Mat4{});
        return true;
    }
    if (command == "SETCONVEYOR" || command == "SETQUICKSAND" || command == "SETSLIPPEDFLOOR") {
        // Cases 25-27: I<1> starts the floor effect unless the model already
        // rides something; I<0> ends any ride.
        if (!ram.generated || !ram.node) return true;
        if (packet.integer(0) == 0) {
            ram.ride.reset();
            return true;
        }
        if (ram.ride) return true;
        using Kind = ModelRam::Ride::Kind;
        const auto& m = ram.node->matrix;
        const Vec3f position{m(3, 0), m(3, 1), m(3, 2)};
        ModelRam::Ride ride;
        ride.own = translation(position.x, position.y, position.z);
        if (command == "SETCONVEYOR") {
            // RideOn2: its transform starts as the model's matrix; V0 is the step.
            ride.kind = Kind::conveyor;
            ride.type = 0x66;
            ride.own = m;
            ride.point = packet.vectors.empty() ? Vec3f{} : packet.vectors[0];
        } else if (command == "SETQUICKSAND") {
            // RideOn3: V0 the centre, F inner / outer radius, outer / inner speed.
            ride.kind = Kind::quicksand;
            ride.type = 0x67;
            ride.point = packet.vectors.empty() ? Vec3f{} : packet.vectors[0];
            ride.radius_near = packet.real(0);
            ride.radius_far = packet.real(1);
            ride.speed_far = packet.real(2);
            ride.speed_near = packet.real(3);
        } else {
            // RideOn4: F the velocity blend per frame and the rest threshold.
            ride.kind = Kind::slipped;
            ride.type = 0x68;
            ride.point = {position.x, 0.0f, position.z};
            ride.rate = packet.real(0);
            ride.rest = packet.real(1);
        }
        ride.last = ride.own;
        ram.ride = std::move(ride);
        if (std::getenv("DW4_TRACE_RIDE")) engine.log(ram.info_name + "#" + std::to_string(ram.index) + " " + std::string(command));
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
        command == "ACTION1" || command == "ONHITBULLETBOX" || command == "OFHITBULLETBOX") {
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
    for (const auto* ram : player_rams_)
        if (ram->generated && ram->index == index) return ram;
    return nullptr;
}

void ModelCtrlManager::update_player(Engine& engine, ModelRam& ram) {
    const auto& pad = engine.pads()[std::size_t(std::clamp(ram.index, 0, 3))];
    const float sx = pad.left.x, sy = pad.left.y;
    // 8034C4C8 bl 80312138 (the player's bePadData), 8034C4D4 lwz r25,0x24:
    // walking (+70) and the heading need the held mask of the left stick's
    // directions; without one 8034C6C4 clears +70 and keeps the heading. The
    // mask (80311AA4..80311BBC, pad.cpp directions) is empty below the
    // activation radius _enaLen 0.4 and covers every angle above it, so a
    // stick at 0.3 neither steps nor turns. There is no other magnitude test.
    ram.key_input = ram.control_enabled && pad.left_directions.held != 0;
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
        // 0x8034C778..0x8034C7A8: down by the system frame step, not below 0.
        if (ram.slam_timer > 0) ram.slam_timer = std::max(0.0f, ram.slam_timer - engine.system_step());
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
    // The handler leaves a request (+74) for 80309168: B asks for the guard
    // first (8034C6CC, not while jumping); X, a panel decision and A replace
    // it, panel shortcuts and START keep it. Without a request the walk/wait
    // transitions run.
    const auto trigger = pad.buttons.trigger;
    std::string request;
    if (ram.control_enabled && !engine.main_lobby() && ram.seq_name != "SQ_JUMP" && (trigger & dw4::pad_button::b)) request = "SQ_GUARD";
    auto finish = [&] {
        if (request.empty()) apply_control(engine, ram);
        else if (request != ram.seq_name) select_sequence(engine, ram, request);
    };
    // START is taken only standing still with nothing else held (8034CBD0 /
    // 8034D0B8).
    const bool start_alone = (trigger & dw4::pad_button::start) && !ram.key_input && (pad.buttons.held & ~dw4::pad_button::start) == 0;
    const bool free = ram.seq_name != "SQ_JUMP" && ram.seq_name != "SQ_FALL";
    if (ram.control_enabled && free) {
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
        // A panel that ISCHECK reports closed keeps the restricted set for
        // this frame (8034C9AC -> 8034CAF8).
        const bool panel_branch = ram.panel_open;
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
        if (panel_branch) {
            // The key flag (+70) is set before the panel branch (8034C6BC,
            // 8034C960) and 80309168 applies the walk/wait change after this
            // handler, so the player walks (or guards) with a panel open.
            if (trigger & dw4::pad_button::x) {
                select_sequence(engine, ram, "SQ_JUMP");
                return;
            } else if (trigger & dw4::pad_button::y) {
                if (!panel("GENEEMGHEAL").bools.empty()) ram.panel_open = true;
            } else if (start_alone) {
                Packet sound;
                sound.strings.emplace_back("CURSOR_NG");
                messenger.send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
            }
            finish();
            return;
        }
        if (has_panel && (trigger & dw4::pad_button::y)) {
            if (!panel("GENEEMGHEAL").bools.empty()) ram.panel_open = true;
            finish();
            return;
        }
        if (has_panel && (trigger & (dw4::pad_button::l | dw4::pad_button::r | dw4::pad_button::z))) {
            const char* command = (trigger & dw4::pad_button::l) ? "GENERATEITEM"
                                  : (trigger & dw4::pad_button::r) ? "GENERATEWAZA" : "GENERATESTAT";
            if (!panel(command).bools.empty()) ram.panel_open = true;
            finish();
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
            } else if (!ram.attack_delay || timer_expired(*ram.attack_delay, engine.time())) {
                // The named RAM timer "ATDELAY" (8031E238 at 0x8034CEF8, missing
                // = expired): the next attack waits the player's ATTACKDELAY
                // frames, restarted by 8031DE44 (0x8034D080) with the integer
                // end (int64)frames * 1000000000 / 60 (__div2i 800A3AF8).
                int delay = 30;
                if (auto* sys = engine.get<GameSystem>()) delay = sys->players[std::size_t(std::clamp(ram.index, 0, 3))].stats.attack_delay;
                if (!ram.attack_delay) ram.attack_delay.emplace();
                timer_start(*ram.attack_delay, engine.time(), std::int64_t(delay) * 1000000000 / 60);
                ram.combo_phase = 0;
                if (spiral) ram.spin.fill(0);
                select_sequence(engine, ram, spiral ? "SQ_ATCK2" : "SQ_ATCK1");
                // 8034D084..8034D0B0: the HUD portrait ring blinks for the
                // delay, sd MODELCTRL i_stpnl05 <player> with the sequence
                // (ATTACKDELAY - 22) / 4 (srawi/addze: truncated) of the
                // SQ_DLTM01..SQ_DLTM13 table at 804E7C34. The original does
                // not check the index; one outside the table reads past it.
                static constexpr const char* kDelayRings[13] = {"SQ_DLTM01", "SQ_DLTM02", "SQ_DLTM03", "SQ_DLTM04", "SQ_DLTM05",
                                                                "SQ_DLTM06", "SQ_DLTM07", "SQ_DLTM08", "SQ_DLTM09", "SQ_DLTM10",
                                                                "SQ_DLTM11", "SQ_DLTM12", "SQ_DLTM13"};
                if (const int ring = (delay - 22) / 4; ring >= 0 && ring < 13) {
                    Packet blink;
                    messenger.send(engine, "MODELCTRL", "i_stpnl05", ram.index, kDelayRings[ring], blink);
                }
                return;
            }
        }
        // 8034D0B8 (after A): START opens the status window, refused with
        // CURSOR_NG while a panel of any player runs (NDMWPANEL ISRUN, row -1).
        if (start_alone && messenger.count("SUBWND", std::nullopt, -1)) {
            Packet run;
            messenger.send(engine, "NDMWPANEL", std::nullopt, -1, "ISRUN", run);
            if (!run.bools.empty() && run.bools[0]) {
                Packet sound;
                sound.strings.emplace_back("CURSOR_NG");
                messenger.send(engine, "SOUND", std::nullopt, -1, "PLAY", sound);
            } else {
                Packet status;
                status.ints.push_back(ram.index);
                status.strings.emplace_back("STATUS");
                messenger.send(engine, "SUBWND", std::nullopt, 0, "GENERATE", status);
            }
        }
    }
    finish();
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

// 8030A448: returns whether a translation channel ran (ctrl work +78), which
// lets the walls and floors run this frame. It clears +78 and returns at once
// while the work's channel list is empty (0x8030A478..0x8030A48C): before the
// first MOVE of a life nothing turns or moves.
bool ModelCtrlManager::advance_motion(Engine& engine, ModelRam& ram) {
    if (!ram.node) return false;
    try {
        if (!ram.channels.allocated()) return false;
    } catch (const std::exception& error) {
        engine.note(std::string("model motion: ") + error.what());
        return false;
    }
    auto& m = ram.node->matrix;
    const Vec3f position{m(3, 0), m(3, 1), m(3, 2)};
    if (static const char* traced = std::getenv("DW4_TRACE_MOTION"); traced && ram.info_name == traced && (engine.frame() % 10 == 0 || std::getenv("DW4_TRACE_MOTION_ALL"))) {
        const auto s = ram.channels.states();
        char line[320];
        std::snprintf(line, sizeof line,
                      "motion %s#%d %s (%.1f, %.1f, %.1f) ch0 active=%d speed=%.2f ch1 active=%d mode=%d speed=%.2f end=%.1f ch2 active=%d speed=%.2f ch3 active=%d speed=%.2f yaw=%.3f wait=%s",
                      ram.info_name.c_str(), ram.index, ram.seq_name.c_str(), position.x, position.y, position.z, int(s[0].active),
                      s[0].speed, int(s[1].active), int(s[1].mode), s[1].speed, s[1].end_time, int(s[2].active), s[2].speed,
                      int(s[3].active), s[3].speed, ram.applied[1], ram.wait_name ? ram.wait_name->c_str() : "-");
        engine.log(line);
    }
    // The channels advance first by the KEIKATIMER frames (0x8030A498
    // 8031E4C4, only with an allocated list; 8030A2D8 steps each channel by
    // speed * min(frames, 3)): the rotation channel of MOVEEX R?xxx (channel
    // 3) turns the wanted yaw by its step in degrees (0x8030A4A8-0x8030A4F4:
    // +84 += pi/180 * step, the yaw rate +90 = 1.25 * |that|) before the
    // model turns, the translation channels move it after the turn.
    dw4::MotionStep step;
    {
        const bool rotating = ram.channels.states()[3].active;
        const float frames = keika_frames(ram, engine.time());
        try {
            step = ram.channels.advance(frames);
        } catch (const std::exception& error) {
            engine.note(std::string("model motion: ") + error.what());
            return false;
        }
        if (rotating) {
            const float turn = 0.0174532924f * step.rotation;
            ram.desired[1] = turn + ram.desired[1];
            ram.turn_rate_y = 1.25f * (turn < 0.0f ? -turn : turn);
        }
    }
    // Turning (8030C85C at 0x8030A50C, not forced; turn_step): every axis
    // with a rate steps toward its wanted angle by rate * KEIKATIMER frames.
    // Only when an applied angle changed (fcmpu 0x8030C9D0..0x8030CA0C) does
    // 8030C85C rebuild the basis, straight into the node matrix: 80128DAC
    // keeps the translation, 80129134 builds the rotation from the applied
    // angles, 80128D90 puts the translation back (0x8030CA10..0x8030CB14).
    // Otherwise the node matrix stays as it is - the turn a ride gave it
    // (8030CEB0 last^-1 * current, which never touches +80 / +98), the tilt
    // of a recorder a MOVERL follow left or the X / Z rotation of a GENERATE
    // placement - until the model turns itself. (The port rebuilt it every
    // frame: on the stag0203 train the player kept his world heading when
    // the car turned; in Dolphin he turns with it.)
    const bool turned = turn_step(ram, engine.time());
    std::array<float, 16> matrix{};
    std::copy(m.m.begin(), m.m.end(), matrix.begin());
    try {
        if (turned) {
            dw4::original_model_turn_basis(matrix, ram.applied);
            matrix[12] = position.x;
            matrix[13] = position.y;
            matrix[14] = position.z;
        }
        // A running translation channel copies the (possibly rebuilt) node
        // matrix (80128CF4 at 0x8030A610), moves it in its own axes
        // (80128DC8 at 0x8030A664) and sets it back (vtable +0x94).
        if (step.translation_updated) dw4::translate_model_local(matrix, step.translation);
    } catch (const std::exception& error) {
        engine.note(std::string("model motion: ") + error.what());
        return false;
    }
    if (turned || step.translation_updated) std::copy(matrix.begin(), matrix.end(), m.m.begin());
    return step.translation_updated;
}

// 8030CEB0: the ride's own step (vtable +5C: 802E9370 none for a platform,
// 8030962C conveyor, 80309674 quicksand, 803098BC slipped floor), then the
// model keeps its place on the ridden transform: its matrix taken relative
// to last frame's (rotation only, the position as an offset) and placed on
// the current one.
void ModelCtrlManager::ride_step(ModelRam& ram) {
    using Kind = ModelRam::Ride::Kind;
    auto& ride = *ram.ride;
    auto& m = ram.node->matrix;
    const Vec3f position{m(3, 0), m(3, 1), m(3, 2)};
    auto set_translation = [](Mat4& matrix, const Vec3f& v) {
        matrix(3, 0) = v.x;
        matrix(3, 1) = v.y;
        matrix(3, 2) = v.z;
    };
    if (ride.kind != Kind::platform) set_translation(ride.last, {});
    switch (ride.kind) {
    case Kind::platform:
        if (!ride.node) return;
        break;
    case Kind::conveyor:
        set_translation(ride.own, ride.point);
        break;
    case Kind::quicksand: {
        // Towards the centre: the inner speed within the inner radius, the
        // outer one beyond the outer radius, linear between.
        const Vec3f to = ride.point - position;
        const float distance = length(to);
        float speed = ride.speed_near;
        if (distance >= ride.radius_far) speed = ride.speed_far;
        else if (distance > ride.radius_near)
            speed = ride.speed_near -
                    (distance - ride.radius_near) / ((ride.radius_far - ride.radius_near) / (ride.speed_near - ride.speed_far));
        set_translation(ride.own, distance > 0.0f ? to * (speed / distance) : Vec3f{});
        break;
    }
    case Kind::slipped: {
        // The model's own step since last frame (X/Z) sets a velocity once
        // (at most 0.6), after which the velocity blends towards each step by
        // the rate and replaces it; within the rest threshold it stops.
        Vec3f step{position.x - ride.point.x, 0.0f, position.z - ride.point.z};
        if (ride.state == 0) {
            set_translation(ride.own, {});
            ride.state = 1;
            return;
        }
        if (ride.state == 1) {
            if (dot(step, step) >= 0.36f) step = normalize(step) * 0.6f;
            ride.velocity = step;
            set_translation(ride.own, {});
            ride.state = 2;
            return;
        }
        Vec3f velocity = ride.velocity + (step - ride.velocity) * ride.rate;
        if (std::abs(velocity.x) <= ride.rest) {
            velocity.y = 0.0f;
            if (std::abs(velocity.z) <= ride.rest) velocity = {};
        }
        ride.velocity = velocity;
        set_translation(ride.own, velocity);
        set_translation(m, {ride.point.x, position.y, ride.point.z});
        break;
    }
    }
    const Mat4& current = ride.kind == Kind::platform ? ride.node->matrix : ride.own;
    if (static const char* traced = std::getenv("DW4_TRACE_RIDE"); traced && traced[0] == '2')
        std::fprintf(stderr, "ride %s#%d on %s: (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f)\n", ram.info_name.c_str(), ram.index,
                     ride.name.c_str(), ride.last(3, 0), ride.last(3, 1), ride.last(3, 2), current(3, 0), current(3, 1), current(3, 2));
    Mat4 rotation = ride.last;
    const Vec3f offset{m(3, 0) - rotation(3, 0), m(3, 1) - rotation(3, 1), m(3, 2) - rotation(3, 2)};
    set_translation(rotation, {});
    Mat4 inverse;
    if (!invert(rotation, inverse)) inverse = Mat4{};
    Mat4 local = m;
    set_translation(local, offset);
    m = local * inverse * current;
    ride.last = current;
}

// 80309F58 after a landing: a named HitLand floor (a model's collision) is
// ridden from now on (8030949C: MODELCTRL name child GETMATRIX, its node and
// matrix), an unnamed one ends a platform ride; the floor effects stay.
void ModelCtrlManager::land_ride(Engine& engine, ModelRam& ram, const Packet& floor) {
    if (ram.ride && ram.ride->type >= 0x64) return;
    const std::string name(floor.string(0));
    if (name.empty()) {
        if (ram.ride && std::getenv("DW4_TRACE_RIDE")) engine.log(ram.info_name + "#" + std::to_string(ram.index) + " leaves " + ram.ride->name);
        ram.ride.reset();
        return;
    }
    const std::int32_t child = floor.integer(0);
    if (ram.ride && ram.ride->name == name && ram.ride->type == child) return;
    Packet matrix;
    engine.messenger().send(engine, "MODELCTRL", name, child, "GETMATRIX", matrix);
    ModelRam::Ride ride;
    ride.name = name;
    ride.type = child;
    ride.node = matrix.objects.empty() ? nullptr : static_cast<const Node*>(matrix.objects[0]);
    if (ride.node) ride.last = ride.node->matrix;
    if (std::getenv("DW4_TRACE_RIDE"))
        engine.log(ram.info_name + "#" + std::to_string(ram.index) + " rides " + name + "#" + std::to_string(child) +
                   (ride.node ? " at (" + std::to_string(ride.last(3, 0)) + ", " + std::to_string(ride.last(3, 1)) + ", " +
                                    std::to_string(ride.last(3, 2)) + ")"
                              : " (no node)"));
    ram.ride = std::move(ride);
}

// 80309C74 after the frame's motion (8030D028): the model's HITLANDDELIVER
// cylinder meets the walls (HITWALL moves the node out, WHit runs on a hit)
// and the floor: a falling model lands on it (HITFLOORADJUST, the vertical
// motion stops, FHit), a walking one keeps to floors within its step range
// and otherwise falls (SQ_FALL). A rising jump skips the floor.
void ModelCtrlManager::collide(Engine& engine, ModelRam& ram) {
    auto& messenger = engine.messenger();
    if (!ram.node || !messenger.count("HITLANDDELIVER", ram.info_name, ram.index)) return;
    const auto* seq = ram.seq;
    if (ram.wall_check) {
        Packet wall;
        messenger.send(engine, "HITLANDDELIVER", ram.info_name, ram.index, ram.ride ? "HITWALLFULL" : "HITWALL", wall);
        if (!ram.lua_whit.empty() && wall.bools.size() > 1 && wall.bools[1]) {
            run_model_script(engine, ram, ram.lua_whit, "WHit");
            if (!ram.generated || ram.seq != seq) return;
        }
    }
    if (!ram.floor_check) return;
    // 8030A0A0 modes 3 / 4 / 5 on the vertical motion (channel 1).
    dw4::MotionState* vertical = nullptr;
    if (auto list = ram.channels.list_owner(); list && list->size() > 1) {
        auto& entry = list->mutable_entries()[1];
        if (entry) vertical = &*entry;
    }
    const bool rising = vertical && vertical->active && vertical->mode == dw4::MotionMode::impulse_positive;
    const bool falling = vertical && vertical->active && vertical->mode == dw4::MotionMode::accelerate_negative;
    auto stop_vertical = [&] {
        if (vertical && vertical->active) {
            vertical->active = false;
            vertical->speed = 0.0f;
        }
    };
    if (rising) return;
    Packet floor;
    messenger.send(engine, "HITLANDDELIVER", ram.info_name, ram.index, "HITFLOORCHECK", floor);
    if (floor.bools.empty()) return;
    const bool in_range = floor.bools[0] != 0;
    const bool reached = floor.bools.size() > 1 && floor.bools[1] != 0;
    bool landed = false;
    if (ram.fall_check) {
        if (falling) {
            if (reached) {
                Packet adjust;
                messenger.send(engine, "HITLANDDELIVER", ram.info_name, ram.index, "HITFLOORADJUST", adjust);
                stop_vertical();
                landed = true;
            }
        } else if (in_range) {
            Packet adjust;
            messenger.send(engine, "HITLANDDELIVER", ram.info_name, ram.index, "HITFLOORADJUST", adjust);
            stop_vertical();
            ram.on_ground = true;
            return;
        } else {
            if (ram.seq_name == "SQ_SLEP") return;
            ram.on_ground = false;
            select_sequence(engine, ram, "SQ_FALL");
            return;
        }
    }
    if (!ram.lua_fhit.empty() && reached) {
        run_model_script(engine, ram, ram.lua_fhit, "FHit");
        if (!ram.generated) return;
    }
    if (landed) {
        ram.on_ground = true;
        land_ride(engine, ram, floor);
    }
}

int ModelCtrlManager::schedule_slot(const ObjectView& info) const { return update_rank(info.str(7)); }

std::string ModelCtrlManager::slot_name(int slot) const {
    static constexpr const char* kNames[] = {"beNDMWMdlPlayer2", "beNDMWMdlPBullet", "beNDMWMdlEBullet", "beNDMWMdlObject",
                                             "beNDMWMdlItem",    "beNDMWMdlEffect",  "beNDMWMdlMapBase", "beNDMWMdlMapCursor",
                                             "beNDMWMdlPlayer",  "beNDMWMdlEnemy"};
    return slot >= 0 && slot < int(std::size(kNames)) ? kNames[slot] : type_name_;
}

void ModelCtrlManager::update(Engine& engine) {
    for (int slot = 0; slot < 10; ++slot) update_slot(engine, slot);
}

// 802F28F8 of one model class: its RAMs in the order their infos came.
void ModelCtrlManager::update_slot(Engine& engine, int slot) {
    const auto first = std::lower_bound(update_order_.begin(), update_order_.end(), slot,
                                        [](const UpdateEntry& e, int r) { return e.rank < r; });
    const auto count = std::size_t(std::upper_bound(first, update_order_.end(), slot,
                                                    [](int r, const UpdateEntry& e) { return r < e.rank; }) - first);
    const auto begin = std::size_t(first - update_order_.begin());
    for (std::size_t i = begin; i < begin + count && i < update_order_.size(); ++i) {
        const auto& entry = update_order_[i];
        auto& ram = *entry.ram;
        if (!ram.generated) continue;
        // 8030CB2C: an erased model is removed on its next update (the
        // player's bit capsule and an item's name label went with the SQ_ERAS
        // start, player_erase_hook / item_erase_hook).
        if (ram.seq_name == "SQ_ERAS") {
            Packet p;
            command(engine, ram, "REMOVE", p);
            continue;
        }
        if (ram.frozen) continue;
        // 8030CCDC: input, AI and the control transitions run only while the
        // sequence has a CTRL mode; otherwise a running AITIMER is dropped.
        if (ram.ctrl_mode != 0) {
            if (ram.is_player) update_player(engine, ram);
            else if (ram.category == "ENEMY") update_enemy(engine, ram);
            else if (ram.category == "PLAYER2") update_capsule(engine, ram);
            else if (ram.category == "ITEM") {
                // 80309168 runs the class update (vtable +A0, 803AB5AC for
                // beNDMWMdlItem) only with _isOnCtrl set (0x8030918C): no item
                // label, pickup or contact take outside the CTRL sequences.
                if (ram.control_enabled && ram.item_table >= 0) update_item(engine, ram);
                apply_control(engine, ram);
            } else if (ram.ai_maps && !ram.ai_maps->empty()) {
                // 80308FF4, the AI slot (vtable +A0) of beModelCtrl and of
                // most model classes (objects, bullets, effects): the AIM map,
                // else the AIH homing. Floor switches (g_dbutton*) are objects
                // whose NEAR map picks SQ_SWON when a player stands on them.
                update_ai_map(engine, ram);
            } else {
                if (ram.homing) update_homing(engine, ram);
                apply_control(engine, ram);
            }
        } else {
            // 0x8030CD00: with an AI work (+0x50, made for AIM or AIH data,
            // 8030DAE8..8030DB4C) 8031E8BC removes the AITIMER.
            if (ram.ai_maps || ram.homing) ram.ai_timer.reset();
            if (ram.is_player) ram.key_input = false;
        }
        // 8030CD18: the status update of players and enemies (vtable +C0).
        if (ram.is_player || ram.category == "ENEMY") update_status(engine, ram);
        // 8030CD30..8030CD4C: 8030A448 runs unless the position is locked
        // (_isPosLock +A8); it clears the translated byte (+78) and checks the
        // channel list itself. A locked model (0x8030CD54) is placed instead,
        // without channel steps, turning or a KEIKATIMER query, and keeps +78
        // as the last 8030A448 left it: on the locked effect form's source
        // node (+AC), else on the recorder of a MOVERL JOINT (+B0), else on
        // the bone of a MOVERL ACTOR (+B4/+B8).
        if (!ram.pos_lock) {
            ram.translated = advance_motion(engine, ram);
        } else if (ram.attach_node) {
            place_locked(engine, ram);
        } else if (ram.follow_trace) {
            // 8030CD94: the recorder's last world matrix (_m, +0x20: the TRC
            // data +0x10 plus the offset of the field meta 805646FC), as the
            // last traversal left it.
            if (ram.node && ram.follow_trace->recorded) ram.node->matrix = *ram.follow_trace->recorded;
            if (static const bool trace = std::getenv("DW4_TRACE_FOLLOW") != nullptr; trace && engine.frame() % 60 == 0 && ram.node)
                engine.log("follow " + ram.info_name + "#" + std::to_string(ram.index) + " trace at (" +
                           std::to_string(ram.node->matrix(3, 0)) + ", " + std::to_string(ram.node->matrix(3, 1)) + ", " +
                           std::to_string(ram.node->matrix(3, 2)) + ")" + (ram.follow_trace->recorded ? "" : " (not recorded)"));
        } else if (!ram.follow_model.empty()) {
            follow_bone(engine, ram);
        }
        // 8030CEB0: a ride carries the model and always lets the walls and
        // floors run.
        if (ram.ride && ram.node) {
            ride_step(ram);
            ram.translated = true; // 0x8030CFC8
        }
        // 8030CFEC: the wait runs after the motion and before the walls and
        // floors, so a jump's apex goes straight into its fall.
        if (ram.generated && ram.wait_name) check_wait(engine, ram);
        // 8030C334 (0x8030CFFC, after the wait, before the subcommands): the
        // invulnerable frames count down by the KEIKATIMER frames, not below 0.
        if (ram.muteki > 0.0f) {
            float left = ram.muteki - keika_frames(ram, engine.time());
            if (left < 0.0f) left = 0.0f;
            ram.muteki = left;
        }
        // 803107E0 (0x8030D008): one pass over the subcommand list, in list
        // order, over the entries it had when the pass began (the count is
        // read once, r31): an entry a callback appends waits for the next
        // pass, and a sequence start inside the pass (803108A0 empties the
        // list) leaves the new sequence's entries to the next pass too. A
        // model that went to SQ_ERAS in this update (in its wait, or a bullet
        // in its own BHit script) keeps its list, so its delay-0 effects still
        // start here. (The port keeps hitboxes and effects in two lists and
        // runs the hitboxes first.)
        const auto generation = ram.hitbox_generation;
        const std::size_t effects = ram.effects.size();
        if (!ram.hitboxes.empty()) update_hitboxes(engine, ram);
        if (static const bool trace = std::getenv("DW4_TRACE_EFFECT") != nullptr;
            trace && ram.generated && ram.effects.size() > (ram.hitbox_generation == generation ? effects : 0))
            engine.log("effect pass " + ram.info_name + "#" + std::to_string(ram.index) + ": " +
                       std::to_string(ram.effects.size() - (ram.hitbox_generation == generation ? effects : 0)) +
                       " effect(s) queued during the hitbox pass wait");
        if (effects && ram.generated && ram.hitbox_generation == generation) update_effects(engine, ram, effects);
        // 8030D00C..8030D028: walls and floors while +78 is set.
        if (ram.translated && ram.generated) {
            if (ram.pos_lock && trace_lock() && engine.messenger().count("HITLANDDELIVER", ram.info_name, ram.index))
                engine.log("lock " + ram.info_name + "#" + std::to_string(ram.index) + " walls and floors while locked in " + ram.seq_name);
            collide(engine, ram);
        }
        // 8030D02C: the slipped floor remembers where the model ended.
        if (ram.ride && ram.ride->kind == ModelRam::Ride::Kind::slipped && ram.node)
            ram.ride->point = {ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)};
    }
}

void ModelCtrlManager::check_wait(Engine& engine, ModelRam& ram) {
    {
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
// The hitboxes are subcommands of the ctrl's list (+0xE4, 80310DAC) like the
// effects: 80310754 adds the system frame step (Engine::system_step, world
// time delta / 16666667) to elapsed, not 1.0 (0x80310764..0x80310778).
void ModelCtrlManager::update_hitboxes(Engine& engine, ModelRam& ram) {
    const auto generation = ram.hitbox_generation;
    const float step = engine.system_step();
    // 803107E0 reads the count once and lowers it with each removal (r31).
    std::size_t count = ram.hitboxes.size();
    for (std::size_t i = 0; i < count && i < ram.hitboxes.size();) {
        auto& box = ram.hitboxes[i];
        box.elapsed += step;
        bool check = false;
        if (!box.started) {
            if (box.elapsed >= box.delay) {
                box.started = true;
                check = true;
            }
        } else if (box.elapsed >= box.end) {
            ram.hitboxes.erase(ram.hitboxes.begin() + std::ptrdiff_t(i));
            --count;
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
    // An effect bullet (a technique's x_... model) shoots for its owner, work
    // +0xDC/+0xE0 when set, else for itself (0x803101F4..0x8031023C): the
    // model at the root of the effect chain, so the hit scripts' GETRECNO
    // and FIGHTCALC see the enemy record or the player of the technique.
    const bool owned = !ram.owner_name.empty() && ram.owner_index >= 0;
    Packet packet;
    packet.ints = {box.kind, owned ? ram.owner_index : ram.index};
    packet.vectors = {Vec3f{center.x - half.x, center.y - half.y, center.z - half.z},
                      Vec3f{center.x + half.x, center.y + half.y, center.z + half.z}};
    // 0x803101F0: sub_80306D68 adds the firing model's own node matrix (for
    // a fireball the bullet effect itself, not its owner); a hit target turns
    // towards its translation.
    packet.matrices.push_back(m);
    packet.strings.emplace_back(owned ? ram.owner_name : ram.info_name);
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
    // (The name is taken before the script runs: it may start another
    // sequence, which clears the LUACALL name `prefix` points at.)
    const std::string script = std::string(prefix) + "_" + ram.info_name + ".lua";
    const bool ran = engine.run_script(script);
    if (static const bool trace = std::getenv("DW4_TRACE_LUACALL") != nullptr; trace)
        engine.log("luacall " + script + " #" + std::to_string(ram.index) + " mode '" + std::string(mode) + "'" + (ran ? "" : " (no script)"));
    engine.pop_model_context();
}

// Player controller waits (vtable +A4, 8034D214). Atck01: the attack chain
// continues after the action only when A stayed held; Atck02: releasing A
// selects SQ_WAIT (its start clears the wait name, 8030F418) and the action
// check still follows; held, the wait ends with the action; Atck03: wait
// while A is held.
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
        // 0x8034D47C..0x8034D604: A released selects SQ_WAIT (vtable +0x8C),
        // and the action check still follows: an ended action clears the wait
        // and runs the command loop on the sequence now current (SQ_WAIT,
        // whose section stops at its next LABEL). (Before: no action check
        // after the release.)
        if (!held) select_sequence(engine, ram, "SQ_WAIT");
        return action_done();
    }
    return !held;
}

// Effect subcommands (80310754 / start 80310340): after the delay, FIND_SPACE a
// RAM of the effect model and GENERATE it on this model. Sequence-dead effects
// stay queued so a sequence change erases them. The delay counts the system
// frame step, *(*(*(model +0x10) +0x10) +0x28) (0x80310764..0x80310778),
// which beSystem's update 80316258 sets to the world time delta / 16666667
// every frame: 1.001 per NTSC field, more or less with a jittering clock
// (Dolphin, DW4_CLOCK_FILE), so a delay can end one frame later than a
// +1.0 count.
void ModelCtrlManager::update_effects(Engine& engine, ModelRam& ram, std::size_t count) {
    const float step = engine.system_step();
    const auto generation = ram.hitbox_generation;
    // The first `count` entries: those the pass began with (803107E0, r31).
    for (std::size_t i = 0; i < count && i < ram.effects.size();) {
        auto& effect = ram.effects[i];
        effect.elapsed += step;
        if (effect.started || effect.elapsed < effect.delay) { ++i; continue; }
        effect.started = true;
        Packet find;
        engine.messenger().send(engine, "MODELCTRL", effect.model, -1, "FIND_SPACE", find);
        if (find.ints.empty()) {
            // The messenger answers FIND_SPACE itself (0x80305C44..0x80305CF4):
            // the first row of the info whose +0x14 (generated: GENERATE sets
            // it, REMOVE clears it, a row in SQ_ERAS is still taken) is clear.
            // With every row of the effect model in use nothing answers,
            // 80310340 returns 0 (0x8031038C) and 803107E0 drops the entry,
            // so the original does not start that effect either (the four
            // x_029eblst01a rows while three e_mecha5 fire, the three
            // x_bossgurd01a rows when f_devl22's SQ_GUARD comes again before
            // the last guard's effects are removed). Only a model without
            // rows would be a gap of the port.
            if (engine.messenger().count("MODELCTRL", effect.model, -1) == 0) engine.note("effect model without MODELCTRL rows " + effect.model);
            // DW4_TRACE_EFFECT: who asked, and the rows of that model (all generated, or none).
            if (static const bool trace = std::getenv("DW4_TRACE_EFFECT") != nullptr; trace) {
                std::string rows;
                for (const auto& other : rams_)
                    if (other->info_name == effect.model)
                        rows += " #" + std::to_string(other->index) + (other->generated ? " " + other->seq_name : std::string(" free"));
                engine.log("effect " + effect.model + " not started for " + ram.info_name + "#" + std::to_string(ram.index) + " " + ram.seq_name +
                           ": no free MODELCTRL row" + (rows.empty() ? std::string(" (no beModelCtrl rows)") : ";" + rows));
            }
            ram.effects.erase(ram.effects.begin() + std::ptrdiff_t(i));
            --count;
            continue;
        }
        effect.ram = find.ints[0];
        // 0x803104A8..0x803104F4: the owner is this model's own owner (work
        // +0xDC/+0xE0) when it has one, else this model, so an effect started
        // by an effect still shoots for the model at the root of the chain.
        const bool owned = !ram.owner_name.empty() && ram.owner_index >= 0;
        Packet generate;
        generate.ints = {ram.index, owned ? ram.owner_index : ram.index};
        generate.strings.emplace_back(ram.info_name);
        generate.strings.emplace_back(effect.sequence);
        generate.strings.emplace_back(owned ? ram.owner_name : ram.info_name);
        generate.vectors = {effect.offset, effect.rotation};
        generate.bools.push_back(effect.lock ? 1 : 0);
        const std::string model = effect.model;
        const int target = effect.ram;
        const bool keep = effect.sequence_dead;
        // DW4_TRACE_EFFECT logs every effect start with its source and owner.
        if (static const bool trace = std::getenv("DW4_TRACE_EFFECT") != nullptr; trace) {
            const auto* made = this->find(model, target);
            engine.log("effect " + model + "#" + std::to_string(target) + " (" + (made ? made->category : std::string("?")) + ") " +
                       effect.sequence + " from " + ram.info_name + "#" + std::to_string(ram.index) + " owner " +
                       std::string(generate.string(2)) + "#" + std::to_string(generate.ints[1]));
        }
        if (!keep) {
            ram.effects.erase(ram.effects.begin() + std::ptrdiff_t(i));
            --count;
        } else {
            ++i;
        }
        engine.messenger().send(engine, "MODELCTRL", model, target, "GENERATE", generate);
        // A sequence start (or REMOVE) the new effect caused emptied the list.
        if (!ram.generated || ram.hitbox_generation != generation) return;
    }
}

// An attached effect (8030D534): the source's matrix (GETMATRIX of the
// GENERATE string0/int0), the offset in its space (80128DC8), then local
// rotations about X, Y and Z in radians (801298F0).
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

// 0x8030CD54..0x8030CD90: a locked effect form stands on its source every
// update: the node it kept (+AC; read whether that model is still generated
// or not) copied (80128CF4 from +0x20), moved by the offset +BC..+C4 in its
// own axes (80128DC8) and set on the effect's node (vtable +0x94). The
// GENERATE rotation is not applied again, so a rotated locked effect lines
// up with its source from its first update on. (The port placed it after the
// walls and floors, a frame late for its hitboxes, and kept the rotation.)
void ModelCtrlManager::place_locked(Engine& engine, ModelRam& ram) {
    if (!ram.node || !ram.attach_node) return;
    std::array<float, 16> matrix{};
    std::copy(ram.attach_node->matrix.m.begin(), ram.attach_node->matrix.m.end(), matrix.begin());
    try {
        dw4::translate_model_local(matrix, {ram.attach_offset.x, ram.attach_offset.y, ram.attach_offset.z});
    } catch (const std::exception& error) {
        engine.note(std::string("locked effect: ") + error.what());
        return;
    }
    std::copy(matrix.begin(), matrix.end(), ram.node->matrix.m.begin());
}

// The player's erase hook (vtable +B4, 8034E3C0), run when SQ_ERAS starts:
// the defeated player leaves its bit capsule where it stands (MODELCTRL
// g_000bcaps GENERATE with the node matrix and SQ_ENTR<n>P), whose script
// ends the attempt when nobody is left; a capsule that runs keeps the bits
// and EXP (GAMERAM BKCHIPBITEXP); an open quick panel closes (+0x124,
// NDMWPANEL REMOVE). The removal follows on the next update, so the capsule
// is there one frame before it (two-player dng411b1 run: Dolphin's capsule
// starts SQ_ENTR1P in the frame of the defeated player's SQ_ERAS; the port
// made it with the REMOVE, a frame late).
void ModelCtrlManager::player_erase_hook(Engine& engine, ModelRam& ram) {
    static const char* entries[] = {"SQ_ENTR1P", "SQ_ENTR2P", "SQ_ENTR3P", "SQ_ENTR4P"};
    Packet capsule;
    capsule.matrices.push_back(ram.node ? ram.node->matrix : Mat4{});
    capsule.strings.emplace_back(entries[std::clamp(ram.index, 0, 3)]);
    engine.messenger().send(engine, "MODELCTRL", "g_000bcaps", ram.index, "GENERATE", capsule);
    Packet run;
    engine.messenger().send(engine, "MODELCTRL", "g_000bcaps", ram.index, "ISRUN", run);
    if (run.boolean(0))
        if (auto* sys = engine.get<GameSystem>()) sys->store_bit_chip(ram.index);
    if (ram.panel_open) {
        ram.panel_open = false;
        Packet remove;
        engine.messenger().send(engine, "NDMWPANEL", std::nullopt, ram.index, "REMOVE", remove);
    }
    engine.log("player " + std::to_string(ram.index) + " defeated");
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
    // The waits run on the same named AITIMER as the AIM map (8031DE44 at
    // 0x80308BAC / 0x80308C04, 8031E238 at 0x80308BC0 / 0x80308C18): a timer
    // started this frame is not expired yet, even for 0 frames.
    switch (ram.homing_state) {
    case 0:
        start_ai_timer(ram, engine.time(), frames_to_time(h.delay));
        ram.homing_state = 1;
        [[fallthrough]];
    case 1:
        if (ai_timer_expired(ram, engine.time())) {
            ram.homing_state = 2;
            tick = true;
        }
        break;
    case 2:
        start_ai_timer(ram, engine.time(), frames_to_time(h.interval));
        ram.homing_state = 3;
        [[fallthrough]];
    case 3:
        if (ai_timer_expired(ram, engine.time())) {
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
        // 8031C67C: enemies come from ENEMY GETMATRIX_ACTIVE (not while
        // invulnerable), players from PLAYER GETMATRIX.
        if (!enemy_bullet && other->muteki != 0.0f) continue;
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
                   (target ? target->info_name + " d2 " + std::to_string(best) : std::string("no target")) + " aim height " +
                   std::to_string(ram.aim_height));
    if (!target || best > h.range2) return;
    const auto& o = target->node->matrix;
    // 0x80308D28: following the height sets the X rate (+8C) to the AIH
    // pitch rate and the wanted pitch (+80) toward the target's height plus
    // the bullet's own placement height (info work +0x54, the Y of the
    // effect offset 8030D260 stored): -atan2 (double 800A2C74, frsp) of that
    // height difference over the distance sqrt(d2).
    if (live && h.pitch) {
        ram.turn_rate_x = h.pitch_rate;
        const float dy = (o(3, 1) + ram.aim_height) - m(3, 1);
        const float distance = std::sqrt(best);
        ram.desired[0] = -static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(distance)));
    }
    // 0x80308E40: a steering step sets the Y rate (+90) to the AIH yaw rate
    // and the wanted yaw (+84) toward the target.
    if (tick && h.yaw) {
        ram.turn_rate_y = h.yaw_rate;
        ram.desired[1] = static_cast<float>(
            std::atan2(static_cast<double>(o(3, 0) - m(3, 0)), static_cast<double>(o(3, 2) - m(3, 2))));
    }
    // 0x80308E70: the turn 8030C85C (not forced) steps every axis by its rate
    // times the KEIKATIMER frames and rebuilds the basis around the position
    // when an angle moved; then the wanted yaw takes the applied one
    // (0x80308E74), so the heading stays where this step got to. The wanted
    // pitch stays: the frame's motion turn (0x8030A50C) steps it again.
    if (turn_step(ram, engine.time())) {
        std::array<float, 16> matrix{};
        std::copy(ram.node->matrix.m.begin(), ram.node->matrix.m.end(), matrix.begin());
        const Vec3f position{matrix[12], matrix[13], matrix[14]};
        try {
            dw4::original_model_turn_basis(matrix, ram.applied);
            matrix[12] = position.x;
            matrix[13] = position.y;
            matrix[14] = position.z;
            std::copy(matrix.begin(), matrix.end(), ram.node->matrix.m.begin());
        } catch (const std::exception& error) {
            engine.note(std::string("homing turn: ") + error.what());
        }
    }
    ram.desired[1] = ram.applied[1];
    if (static const bool trace = std::getenv("DW4_TRACE_HOMING") != nullptr; trace && tick && h.yaw)
        engine.log("homing " + ram.info_name + "#" + std::to_string(ram.index) + " steers to " + target->info_name +
                   " heading " + std::to_string(ram.applied[1]) + " step " + std::to_string(ram.homing_steps));
}

// 8030CDD0: ACTION <model> GETBONEMATRIX <bone> times MODELCTRL <model>
// GETMATRIX (same index) places the following model.
void ModelCtrlManager::follow_bone(Engine& engine, ModelRam& ram) {
    if (!ram.node) return;
    Packet bone;
    bone.strings.emplace_back(ram.follow_bone);
    engine.messenger().send(engine, "ACTION", ram.follow_model, ram.index, "GETBONEMATRIX", bone);
    if (bone.matrices.size() != 1) return;
    // 0x8030CE34..0x8030CE54: MODELCTRL GETMATRIX answers only for a
    // generated model (8030D910 case 10).
    const auto* target = find(ram.follow_model, ram.index);
    if (!target || !target->generated || !target->node) return;
    ram.node->matrix = bone.matrices[0] * target->node->matrix;
    if (static const bool trace = std::getenv("DW4_TRACE_FOLLOW") != nullptr; trace && engine.frame() % 60 == 0)
        engine.log("follow " + ram.info_name + "#" + std::to_string(ram.index) + " on " + ram.follow_model + " at (" +
                   std::to_string(ram.node->matrix(3, 0)) + ", " + std::to_string(ram.node->matrix(3, 1)) + ", " +
                   std::to_string(ram.node->matrix(3, 2)) + ")");
}

void ModelCtrlManager::update_ai_map(Engine& engine, ModelRam& ram) {
    // 8030CC68: every frame starts with no key input and no request (+70/
    // +74); the AI (vtable +A0) runs only while _isOnCtrl (+44) is set, the
    // control transitions of 80309168 always.
    if (!ram.node) return;
    ram.key_input = false;
    if (!ram.control_enabled || ram.ai_maps->empty()) {
        apply_control(engine, ram);
        return;
    }
    std::string request = ram.ai_request;
    // 0x803085B4: 8031E238 on the named AITIMER (missing = expired; the end
    // against the 2^18 ns quantised clock with a strict '<').
    if (ai_timer_expired(ram, engine.time())) {
        const auto& map = (*ram.ai_maps)[std::size_t(std::clamp(ram.ai_map, 0, int(ram.ai_maps->size()) - 1))];
        const auto& m = ram.node->matrix;
        // 80308654: [11] WPOS steers to the map's world target, otherwise a
        // player (803086D8 bl 8031C67C with 'PLAYER' and [11]).
        Vec3f goal{};
        float best = 1e30f;
        const std::string_view mode = map.str(11);
        if (mode == "WPOS") {
            goal = ram.ai_world_target;
            const float dx = goal.x - m(3, 0), dy = goal.y - m(3, 1), dz = goal.z - m(3, 2);
            best = dx * dx + dy * dy + dz * dz;
        } else {
            // 803085D4 makes a null [11] 'NEAR'; 8031C67C r29 = ([11] == 'NEAR')
            // and 8031C454 picks with 8031BF74 (NEAR: the smallest squared
            // distance, strict '<', the first of equals) or 8031C02C (any
            // other string: the smallest deviation of the direction to the
            // player from the own heading). The disc's AIM maps all hold NEAR
            // or WPOS (948 / 71 in ndmw.afs, every difficulty), so the angle
            // mode only serves modded data (an empty string counts as null).
            const bool nearest = mode.empty() || mode == "NEAR";
            float heading = 0.0f, best_angle = 0.0f;
            if (!nearest) {
                // 8031C874: 8012D434 turns +Z (804F8B10, set to (0,0,1) at
                // 8012F798) by the copied model matrix (the zero products
                // keep its signed zeros); double atan2(x, z).
                const float x = 1.0f * m(2, 0) + (0.0f * m(0, 0) + 0.0f * m(1, 0));
                const float z = 1.0f * m(2, 2) + (0.0f * m(0, 2) + 0.0f * m(1, 2));
                heading = static_cast<float>(std::atan2(static_cast<double>(x), static_cast<double>(z)));
            }
            const ModelRam* target = nullptr;
            for (const auto& other : rams_) {
                if (!other->is_player || !other->generated || !other->node) continue;
                const auto& o = other->node->matrix;
                if (nearest) {
                    const float dx = o(3, 0) - m(3, 0), dy = o(3, 1) - m(3, 1), dz = o(3, 2) - m(3, 2);
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < best) { best = d2; target = other.get(); }
                    continue;
                }
                // 8031C0D0..8031C13C: |atan2(dx, dz) - heading| (fneg unless
                // >= 0), minus 2pi (804529EC) while above 2pi - not wrapped to
                // [-pi, pi]; the first player or a strictly smaller deviation.
                float deviation = static_cast<float>(std::atan2(static_cast<double>(o(3, 0) - m(3, 0)),
                                                                static_cast<double>(o(3, 2) - m(3, 2)))) - heading;
                if (!(deviation >= 0.0f)) deviation = -deviation;
                constexpr float two_pi = 0x1.921fb6p+2f; // 0x40C90FDB
                while (deviation > two_pi) deviation -= two_pi;
                if (!target || best_angle > deviation) { best_angle = deviation; target = other.get(); }
            }
            if (!target) {
                // 803086DC: no player, nothing written: no request.
                apply_control(engine, ram);
                return;
            }
            const auto& o = target->node->matrix;
            goal = {o(3, 0), o(3, 1), o(3, 2)};
            if (!nearest) {
                // 8031C158..8031C190: the chosen player's squared distance.
                const float dx = m(3, 0) - o(3, 0), dy = m(3, 1) - o(3, 1), dz = m(3, 2) - o(3, 2);
                best = dz * dz + (dx * dx + dy * dy);
            }
        }
        ram.turn_rate_y = map.f32(6);
        const std::uint16_t band = best < map.f32(3) ? 7 : best < map.f32(4) ? 8 : best < map.f32(5) ? 9 : 10;
        const auto entries = map.list(band);
        if (!entries.empty()) {
            const int roll = int(fight::random() & 15u);
            ObjectView pick = entries.back();
            for (const auto& entry : entries)
                if (entry && roll < entry.i32(5)) { pick = entry; break; }
            ram.desired[1] = std::atan2(goal.x - m(3, 0), goal.z - m(3, 2)) + pick.f32(3);
            request = std::string(pick.str(2));
            // 0x80308868: 8031DE44 restarts it with (int64)(1e9f * frames / 60f).
            start_ai_timer(ram, engine.time(), frames_to_time(pick.f32(4)));
        }
    }
    ram.key_input = request == "SQ_WALK";
    ram.ai_request = request;
    if (request.empty() || request == "SQ_NONE") apply_control(engine, ram);
    else if (request != ram.seq_name) select_sequence(engine, ram, request);
}

void ModelCtrlManager::update_enemy(Engine& engine, ModelRam& ram) {
    // 80308FF4 (vtable +A0) finds no AIM or AIH data: the control logic of
    // 80309168 sees no key input and the model stands.
    if (ram.ai_maps) {
        update_ai_map(engine, ram);
        return;
    }
    ram.key_input = false;
    if (ram.node) apply_control(engine, ram);
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
    int box = -1;
    for (const auto& [lo, hi] : ram.hit->boxes) {
        ++box;
        // 80307EAC (from 8030E6A4): the root matrix moves only the box centre
        // (80122D04 on the box, then its centre); the authored half extents
        // are put back around it, so turning or scaling does not grow it.
        const Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const Vec3f half{(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f};
        const Vec3f w{c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0) + m(3, 0),
                      c.x * m(0, 1) + c.y * m(1, 1) + c.z * m(2, 1) + m(3, 1),
                      c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2) + m(3, 2)};
        const Vec3f wmin{w.x - half.x, w.y - half.y, w.z - half.z}, wmax{w.x + half.x, w.y + half.y, w.z + half.z};
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
    // 0x8030E778..0x8030E7C8: a shooter that is not a player ('p') has to
    // answer MODELCTRL <name> <index> GETRECNO with exactly one record (the
    // record the hit script's FIGHTCALC uses). One that is gone by now (a
    // removed enemy whose shot is still flying) answers nothing (-9): the
    // target's hit script is skipped, but the bullet still counts as
    // delivered (0x8030E85C..0x8030E878) and its BHit script runs.
    if (const std::string_view shooter = packet.string(0); !shooter.starts_with('p')) {
        Packet ask;
        engine.messenger().send(engine, "MODELCTRL", std::string(shooter), packet.integer(1, -1), "GETRECNO", ask);
        if (ask.ints.size() != 1) {
            if (trace) engine.log("  shooter " + std::string(shooter) + "#" + std::to_string(packet.integer(1, -1)) + " gone: no hit script");
            if (packet.bools.empty()) packet.bools.push_back(1);
            return true;
        }
    }
    ModelScriptContext context;
    context.category = ram.category;
    context.name = ram.info_name;
    context.index = ram.index;
    context.bullet_kind = packet.integer(0);
    context.shooter = packet.integer(1, -1);
    context.shooter_name = std::string(packet.string(0));
    context.bullet_box = box;
    // 0x8030E718..0x8030E76C, before the hit script: the translation of the
    // packet's matrix #0 (the firing hitbox model's own matrix, 0x803101F0)
    // and the own node's. The shooter name / index only feed GETRECNO and
    // the script (FIGHTCALC).
    const bool has_source = !packet.matrices.empty();
    const float source_x = has_source ? packet.matrices[0](3, 0) : 0.0f, source_z = has_source ? packet.matrices[0](3, 2) : 0.0f;
    const float own_x = m(3, 0), own_z = m(3, 2);
    engine.push_model_context(std::move(context));
    engine.run_script("hit_" + ram.info_name + ".lua");
    const auto done = engine.pop_model_context();
    if (done.turn && has_source) {
        // 8030E810..8030E854: with the script's turn flag (byte_80535944) the
        // wanted yaw +84 = atan2(dx, dz) (double 800A2C74, then frsp), then
        // the forced 8030C85C: every applied angle takes the wanted one at
        // once and the basis is rebuilt around the position.
        const float heading = static_cast<float>(std::atan2(static_cast<double>(source_x - own_x), static_cast<double>(source_z - own_z)));
        if (trace) {
            char line[160];
            std::snprintf(line, sizeof line, "  turn to (%.3f,%.3f) from (%.3f,%.3f) heading %.4f", source_x, source_z, own_x, own_z, heading);
            engine.log(line);
        }
        ram.desired[1] = heading;
        ram.applied = ram.desired;
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
    // (document, reference) as in ModelCtrlManager::add_info; never replace
    // an entry RAMs may already point into.
    auto& action_slot = actions_[{static_cast<const void*>(info.document()), info.reference()}];
    if (!action_slot) action_slot = std::move(actions);
    auto* action_ptr = action_slot.get();
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
    const auto time = static_cast<std::int64_t>(scaled_clock(world_time, ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
    // The traversal's combiner evaluation (801D0C14) updates the combiner
    // first (801CFFB4 at 0x801D0CB8): a fade whose weight reached 1 retires
    // the states before it. That leaves the pose at this time as it is.
    ram.motion.update(time);
    if (ram.pose_time != world_time) {
        evaluate_pose(*ram.database, ram.motion, time, ram.pose, ram.locals);
        ram.pose_time = world_time;
    }
    // The evaluated bone locals stay in the combiner (+0x40) until the next
    // evaluation, the bone matrices in +0x54: MOTIONSET bakes from the one,
    // GETBONEMATRIX (802F18A4) reads the other, neither evaluates.
    if (!ram.locals.empty()) ram.combiner_cache = ram.locals;
    ram.traversed = ram.pose;
    ram.traversed_time = world_time;
    return &ram.pose;
}

const ActorPose* ActionManager::peek_pose(const Node& actor, std::int64_t world_time) {
    auto found = actor_owner_.find(&actor);
    if (found == actor_owner_.end()) return nullptr;
    auto& ram = *found->second;
    if (!ram.database) return nullptr;
    if (ram.pose_time == world_time) return &ram.pose;
    const auto time = static_cast<std::int64_t>(scaled_clock(world_time, ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
    evaluate_pose(*ram.database, ram.motion, time, ram.peek, ram.peek_locals);
    return &ram.peek;
}

bool ActionManager::drawn(const Node* node) const {
    for (const auto& ram : rams_)
        if (ram->node == node) return !ram->switch_node || ram->switch_node->child_enabled(ram->switch_child);
    return true;
}

const Node* ActionManager::mask_switch(const ObjectView& other) const {
    // igSwitch [8] is the bit mask object (+20), its [4] the mask words
    // (+10) that 802066A8 / 0x80300CB8 index.
    if (!other || !other.is_a("igSwitch")) return nullptr;
    const auto mask = other.ref(8);
    if (!mask) return nullptr;
    const auto words = mask.memory(4);
    for (const auto& ram : rams_) {
        const Node* own = ram->switch_node;
        if (!own || !own->object || !own->object.is_a("igSwitch")) continue;
        const auto own_mask = own->object.ref(8);
        if (!own_mask) continue;
        if (own_mask.document() == mask.document() && own_mask.reference() == mask.reference()) return own;
        if (!words.empty() && own_mask.memory(4).data() == words.data()) return own;
    }
    return nullptr;
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
        // 802F0800 ends with 8031DE44(ram, 'FADE', 0, 0): a fade still running
        // (an OUT 10 frames into an IN 12, the erase effect x_099eerde01a)
        // keeps that timer and its last sample, see timer_start.
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
            // 802F0400: a name with 'S' as its fourth letter keeps its
            // frames as the minimum (one value for all actions) and goes on;
            // otherwise the wait lasts minimum + (random % span) / 100 frames,
            // span = 100 * frames - 100 * minimum (fmsubs, then fctiwz).
            if (c.name.size() > 3 && c.name[3] == 'S') {
                random_wait_min_ = c.first;
                return true;
            }
            const auto span = std::uint32_t(std::int32_t(std::fma(100.0f, c.first, -(100.0f * random_wait_min_))));
            const auto roll = span ? fight::random() % span : fight::random();
            const float frames = random_wait_min_ + float(roll) / 100.0f;
            auto& timer = ram.timers["WAIT"];
            timer_start(timer, engine.time(), frames_to_time(frames));
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
        // 802F1160: every MOTIONSET adds its clip to the actor's combiner as a
        // new state from now on (loc_801D26FC at 0x802F128C, the time set by
        // loc_801CFFB4 at 0x802F12B0), also when that clip is already playing.
        // The state fades in over `first` frames (1e9 * first / 60 ns), or cuts
        // in for first == 0 and for the first MOTIONSET after the action became
        // active (work+0x10, 0x802F11F4). With two states or more still in the
        // combiner (0x802F1238: a cross-fade its last update had not finished),
        // loc_801D2840 (0x802F1248) first removes them all (801D215C) and adds
        // one fixed state built from the combiner's cached bone matrices
        // (+0x40), the output of its last evaluation - the scene traversal of
        // the last picture, not the pose at this time. The new clip fades
        // from that. The 'S' freezes of field 3 (0x802F12C0, 0x802F1370) are
        // left out: every MOTIONSET in the stage data has "RR" there.
        const auto now = static_cast<std::int64_t>(scaled_clock(engine.time(), ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
        auto& m = ram.motion;
        if (m.layered()) {
            std::vector<Mat4> baked = ram.combiner_cache;
            // Never traversed (no cached output yet): the pose at this time.
            if (baked.empty() && ram.database) motion_locals(*ram.database, m, now, baked);
            m.previous_clip.clear();
            m.previous_pose = std::move(baked);
            m.hidden = false;
        } else {
            m.previous_clip = m.clip;
            m.previous_start = m.start;
            m.previous_pose.clear();
        }
        m.blend_start = now;
        m.blend = ram.motion_immediate ? 0 : frames_to_time(c.first);
        ram.motion_immediate = false;
        m.clip = c.name;
        m.start = now;
        // loc_801CFFB4 at 0x802F12B0 updates the combiner at node time: the new
        // state's fade starts now, and a state that cuts in retires the others.
        m.update(now);
        // 0x802F1440-0x802F14CC: 'L' loops the clip, anything else plays it once.
        if (ram.database) ram.database->set_looping(c.name, !c.loop.empty() && c.loop[0] == 'L');
        ram.motion_work = 1; // 0x802F14E4
        ram.pose_time = -1;
        return true;
    }
    engine.note("ACTION command " + c.type);
    return true;
}

void ActionManager::run(Engine& engine, ActionRam& ram) {
    if (!ram.current) return;
    // DW4_TRACE_ACTION=model (or *) also logs every command as it runs.
    static const char* trace = std::getenv("DW4_TRACE_ACTION");
    while (ram.cursor < ram.current->commands.size()) {
        const auto& c = ram.current->commands[ram.cursor];
        if (trace && (ram.info_name == trace || std::string_view(trace) == "*"))
            engine.log("action " + ram.info_name + "#" + std::to_string(ram.index) + " " + ram.current->name + "[" +
                       std::to_string(ram.cursor) + "] " + c.type.substr(9) + " " + c.name + " " + std::to_string(c.first));
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
    // The default path of 802F1794 (every name but NDSP and the special
    // commands) activates the RAM and sets its switch-mask bit first
    // (0x802F1E94..0x802F1EF4 for an inactive RAM, 0x802F20E4..0x802F2120 for
    // an active one) and looks the action up only after that (800695CC at
    // 0x802F2144): a name the model has no action for still shows the model
    // and turns its collision on (kind 0x400 children follow that bit).
    if (!ram.action_active) {
        ram.action_active = true;
        // 0x802F1EC0-0x802F1EC8: work +14 (motion), +34 (fade), +44 (wait).
        ram.motion_work = 0;
        ram.fade_state = 0;
        ram.wait_state = 0;
        // 0x802F1EA0-0x802F20DC: activation lets the next MOTIONSET cut in
        // (work+0x10 = 1 at 0x802F1F00) and appends the database's first
        // animation (loc_801D2810 -> 801D2044 -> 801D1BCC) as a replacing base
        // state (combine mode 0, transition 4, active: 0x802F20C4-0x802F20D0).
        // That shows only the first animation but deletes nothing: the states
        // from before (after NDSP, say) stay in the combiner and count, so
        // WAITCHECK 'M' waits and the next MOTIONSET bakes (and its cut-in
        // retires the bake at once). Combiner +0x10 = 100000000 there
        // (0x802F1F38-0x802F1F50) is the request time 801D0A38 stamps into new
        // states (+0x98); only the optional cleanCombinerStates pass 801D0A90
        // reads it, which is off (byte 0x704(r13)), so poses do not change.
        if (ram.database) {
            const auto now = node_time(ram.node_clock, engine.time());
            auto& m = ram.motion;
            m.hidden = !m.clip.empty();
            m.previous_clip.clear();
            m.previous_pose.clear();
            m.clip = ram.database->first_clip_name();
            m.start = now;
            m.blend = 0;
            m.blend_start = now;
            ram.pose_time = -1;
        }
        ram.motion_immediate = true;
    } else {
        ram.motion_immediate = false; // 0x802F212C, also for the action already running
    }
    ram.visible = true;
    const Action* found = nullptr;
    for (const auto& a : *ram.actions) if (a.name == name) { found = &a; break; }
    if (!found) engine.note("ACTION " + std::string(name) + " missing in " + ram.info_name);
    // 0x802F2148..0x802F2158: the action already current (work +8) runs no
    // command and sends no broadcast.
    if (ram.current == found) {
        if (trace && (ram.info_name == trace || std::string_view(trace) == "*"))
            engine.log("action " + ram.info_name + "#" + std::to_string(ram.index) + " " + std::string(name) + " already current");
        apply_visuals(ram);
        return;
    }
    if (!found) {
        // A missing name while another action is current: the original stores
        // the null action and its command loop 802F1510 reads through it
        // (lwz 0xC(r29) at 0x802F16A4), a fault no stage data reaches. Keep
        // the model shown, run nothing and send no broadcast.
        ram.current = nullptr;
        ram.cursor = 0;
        apply_visuals(ram);
        return;
    }
    ram.current = found;
    ram.cursor = 0;
    // 0x802F21B0: the commands run (802F1510) with the switch bit already
    // set, and only then 80305ED0 at 0x802F21D8 sends ACTION_BROADCAST
    // (info name, RAM index, action name): the action starters (802F228C)
    // and the SE sets (803130D4, MODELCTRL SETSOUND: here SoundManager::
    // action_started) see the model as its first commands left it.
    run(engine, ram);
    apply_visuals(ram);
    if (auto* sound = engine.get<SoundManager>()) sound->action_started(engine, ram.info_name, ram.index, name);
    Packet broadcast;
    broadcast.strings.emplace_back(ram.info_name);
    broadcast.ints.push_back(ram.index);
    if (engine.messenger().count("ACTION_BROADCAST", std::nullopt, -1))
        engine.messenger().send(engine, "ACTION_BROADCAST", std::nullopt, -1, name, broadcast);
}

bool ActionManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<ActionRam&>(base);
    if (command == "GETBONEMATRIX") {
        // 802F18A4: the named joint (igSkeleton lookup, an unnamed root
        // joint for "") of the first actor in the model's space, read from
        // the bone array the last traversal of the model's actors filled
        // (actor +0x24 with +0x79, else combiner vtable +0x70 = 80215C38:
        // combiner +0x54 + i * 64) without evaluating the combiner. The
        // traversal comes after the updates, so the matrix is the last
        // picture's, and while no actor of the model is traversed (culled,
        // hidden) it stays as old as the last visit. Before the first
        // traversal the array holds 801D13DC's identities.
        // The case answers only while the action runs (0x802F18A4..0x802F18AC:
        // RAM +0x14, which an action start sets at 0x802F1EA8 and NDSP clears
        // at 0x802F1CD4, else straight to the exit 0x802F21DC): after NDSP,
        // or before the first action, there is no answer and a bone follower
        // (8030CDD0 wants exactly one) keeps the matrix it last had.
        const std::string bone(packet.string(0));
        if (!ram.action_active) {
            if (static const bool trace = std::getenv("DW4_TRACE_FOLLOW") != nullptr; trace)
                engine.log("GETBONEMATRIX " + ram.info_name + "#" + std::to_string(ram.index) + " '" + bone +
                           "': no answer, the action is not running");
            return true;
        }
        const Node* actor = ram.actors.empty() ? nullptr : ram.actors.front();
        if (actor && !ram.traversed.skeleton && ram.database) {
            const auto& names = ram.database->skeleton().names;
            for (std::size_t i = 0; i < names.size(); ++i)
                if (names[i] == bone) {
                    packet.matrices.push_back(Mat4{});
                    return true;
                }
        }
        const ActorPose* pose = actor && ram.traversed.skeleton ? &ram.traversed : nullptr;
        if (!pose && std::getenv("DW4_TRACE_BONES")) engine.log("GETBONEMATRIX " + ram.info_name + ": no pose");
        // DW4_TRACE_FOLLOW=2: every query on matrices older than the last frame's traversal.
        if (static const char* trace = std::getenv("DW4_TRACE_FOLLOW"); trace && std::string_view(trace) == "2" &&
            (ram.traversed_time < 0 || engine.time() - ram.traversed_time > 20000000))
            engine.log("GETBONEMATRIX " + ram.info_name + "#" + std::to_string(ram.index) + " '" + bone + "' from " +
                       (ram.traversed_time < 0 ? std::string("no traversal yet")
                                               : "a traversal " + std::to_string((engine.time() - ram.traversed_time) / 1000000) + " ms ago"));
        if (pose && pose->skeleton) {
            const auto& names = pose->skeleton->names;
            if (std::getenv("DW4_TRACE_BONES") && engine.frame() % 60 == 0) {
                std::string list;
                for (std::size_t i = 0; i < names.size() && i < pose->bone_world.size(); ++i)
                    list += " '" + names[i] + "'(" + std::to_string(pose->bone_world[i](3, 0)) + "," +
                            std::to_string(pose->bone_world[i](3, 2)) + ")";
                engine.log("bones " + ram.info_name + list);
            }
            for (std::size_t i = 0; i < names.size() && i < pose->bone_world.size(); ++i)
                if (names[i] == bone) {
                    packet.matrices.push_back(pose->bone_world[i] * actor->actor_matrix);
                    return true;
                }
            if (std::getenv("DW4_TRACE_FOLLOW")) {
                std::string list;
                for (const auto& n : names) list += " '" + n + "'";
                engine.log("GETBONEMATRIX " + ram.info_name + " has no '" + bone + "':" + list);
            }
        }
        if (bone.empty()) packet.matrices.push_back(Mat4{});
        return true;
    }
    if (command == "ISCOMEND") {
        const bool end = ram.current && ram.wait_state == 0 && ram.cursor == ram.current->commands.size();
        packet.bools.push_back(end ? 1 : 0);
        return true;
    }
    if (command == "NDSP") {
        if (static const char* trace = std::getenv("DW4_TRACE_ACTION"); trace && (ram.info_name == trace || std::string_view(trace) == "*"))
            engine.log("action " + ram.info_name + "#" + std::to_string(ram.index) + " NDSP");
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
        // 802F0EB0, the first phase of 802F16CC: after a MOTIONSET the motion
        // work waits one frame (1 -> 2), then updates the combiner at node
        // time (801CFFB4 at 0x802F0F50) each frame until one state is left
        // (0x802F1070), so WAITCHECK 'M' and the next MOTIONSET count this
        // frame's states. The speed alignment there (0x802F108C) has no
        // counterpart: the port's states run on the node clock.
        if (ram.motion_work == 1) {
            ram.motion_work = 2;
        } else if (ram.motion_work == 2) {
            if (ram.motion.layered()) ram.motion.update(node_time(ram.node_clock, now));
            if (!ram.motion.layered()) ram.motion_work = 0;
        }
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
        // Fade (802F057C). IN adds frames * step (fmadds at 0x802F0608) with no
        // clamp, OUT subtracts it and only clamps below at 0 (0x802F0788); the
        // value (+0x40) keeps that, so a FADESET that restarts the running
        // 'FADE' timer (its old sample stays, timer_start) can step back first
        // and go above 1 (an OUT after a running IN stays full until the value
        // is below 1 again). Only the colour saturates at 1 (GX colour), kept
        // in [0, 1] here. The end checks: IN at value >= limit (0x802F0634),
        // OUT at value <= 0 (0x802F07C0); both delete the timer (8031E8BC).
        if (ram.fade_state == 1 || ram.fade_state == 2) {
            auto found = ram.timers.find("FADE");
            if (found != ram.timers.end()) {
                const float frames = timer_frames(found->second, now);
                if (frames != 0 && ram.color) {
                    Vec4f color = *ram.color;
                    const float value = ram.fade_state == 1 ? std::fma(frames, ram.fade_add, ram.fade_value)
                                                            : std::max(std::fma(-frames, ram.fade_add, ram.fade_value), 0.0f);
                    color.w = std::clamp(value, 0.0f, 1.0f);
                    ram.color = color;
                    ram.fade_value = value;
                    const bool finished = ram.fade_state == 1 ? !(value < ram.fade_limit) : !(value > 0);
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
            // 802F0264: only once the combiner is down to one state (0x802F0300:
            // the count its last update left, from the traversal of the last
            // picture or the motion phase above) and that state's time has
            // reached the clip's duration (0x802F03B8), looping or not.
            ready = true;
            if (ram.database && !ram.motion.clip.empty()) {
                if (const Clip* clip = ram.database->clip(ram.motion.clip)) {
                    const auto t = static_cast<std::int64_t>(scaled_clock(now, ram.node_clock.scale) + static_cast<std::uint64_t>(ram.node_clock.bias));
                    ready = !ram.motion.layered() && t - ram.motion.start >= clip->duration;
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
