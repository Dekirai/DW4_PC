#include "overhead_labels.hpp"

#include "presence_data.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace dw4::game {
namespace {

// A label that moved further than this between two steps jumped (a warp, a
// stage entry): it is not blended.
constexpr float kJumpDistance = 40.0f;
// The actors' bounding boxes hold the bind pose (dng101b1: Agumon's box
// reaches 18.3 above its feet, the figure in its stance about 7.3): the
// label sits at this share of the box.
constexpr float kBoundShare = 0.45f;
// A model of no known form: above its highest joint, this share of the
// joint's height within these bounds (the joints sit inside the figure).
constexpr float kJointMargin = 0.25f, kJointMarginMin = 1.8f, kJointMarginMax = 6.0f;
// The bars' bottom this far above the head (world units; the remote
// players' head points in OverheadLabels::capture too).
constexpr float kHeadGap = 1.0f;
// The head point stands over the head joint, not over the model's origin
// (in a stance the head sits up to 2 units beside it, MetalGarurumon's 3.2
// to 3.4, in a run 4 to 7 ahead of it, which a camera that looks down turns
// into a height): the joint's horizontal offset from the origin, at most
// kHeadReach long, followed by this share per step (some 20 steps to settle
// after a turn; the lunge of an attack moves the tag a little and smoothly).
constexpr float kHeadReach = 3.5f, kHeadFollow = 0.05f;
// The head's rise (FormHead): followed up at once, so a jump, the leap of a
// charged attack or the bounce of a run never pushes the head through the
// bars; held this many steps, and again by every rise that comes back to
// this share of the held one (a run's stride takes some 20 steps and its
// tops differ a little, so a run keeps the tag steady at the stride's top),
// then eased back down by this share per step (half a second after a
// landing).
constexpr int kLiftHold = 30;
constexpr float kLiftRenew = 0.75f, kLiftEase = 0.1f;
// The pose a capture reads is the one traversed for the last picture, a step
// old: a head that rose faster than this per step (the leap of a charged
// attack, up to 2.1) is led by the excess, a jump's (0.3) or a run's (0.5)
// is not.
constexpr float kLiftLeadFree = 0.5f;
// The occlusion probes: this far above the head point (world units), and
// to either side by this share of the window height.
constexpr float kProbeRise = 1.5f, kProbeSpread = 0.012f;
// A label whose player the picture hides is drawn at this opacity.
constexpr float kOccludedAlpha = 0.35f;

engine::Vec3f lerp(const engine::Vec3f& a, const engine::Vec3f& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// The highest point of a node's bounding box in world space (its matrix is
// the model's placement, its children's boxes are in the same frame).
void bound_top(const engine::Node& node, const engine::Mat4& world, int depth, float& top, bool& found) {
    if (node.has_bound) {
        for (int corner = 0; corner < 8; ++corner) {
            const engine::Vec3f p{corner & 1 ? node.bound_max.x : node.bound_min.x, corner & 2 ? node.bound_max.y : node.bound_min.y,
                                  corner & 4 ? node.bound_max.z : node.bound_min.z};
            const auto w = engine::transform_point(p, world);
            if (!found || w.y > top) top = w.y;
            found = true;
        }
        return;
    }
    if (depth >= 3) return;
    for (const auto* child : node.children)
        if (child) bound_top(*child, world, depth + 1, top, found);
}

} // namespace

std::optional<ScreenPoint> project_label(const engine::Vec3f& p, const LabelView& view, float margin) {
    const engine::Mat4 vp = view.camera.view * view.camera.projection;
    const float x = p.x * vp(0, 0) + p.y * vp(1, 0) + p.z * vp(2, 0) + vp(3, 0);
    const float y = p.x * vp(0, 1) + p.y * vp(1, 1) + p.z * vp(2, 1) + vp(3, 1);
    const float w = p.x * vp(0, 3) + p.y * vp(1, 3) + p.z * vp(2, 3) + vp(3, 3);
    if (!(w > 1e-4f)) return std::nullopt;
    // The renderer widens column 0 of perspective projections for the wide
    // viewport (GlRenderer::render): the same x scale here.
    const float ndc_x = x / w * view.widen, ndc_y = y / w;
    if (std::abs(ndc_x) > 1.0f + 2.0f * margin || std::abs(ndc_y) > 1.0f + 2.0f * margin) return std::nullopt;
    const float px = view.wide_x + (ndc_x * 0.5f + 0.5f) * view.wide_width;
    const float py = view.window_height - (view.wide_y + (ndc_y * 0.5f + 0.5f) * view.wide_height);
    const auto eye = camera_eye(view.camera.view);
    const engine::Vec3f d{p.x - eye.x, p.y - eye.y, p.z - eye.z};
    return ScreenPoint{px / std::max(view.scale_x, 1e-3f), py / std::max(view.scale_y, 1e-3f), engine::length(d)};
}

engine::Vec3f camera_eye(const engine::Mat4& view) {
    engine::Mat4 inverse;
    if (!engine::invert(view, inverse)) return {};
    return {inverse(3, 0), inverse(3, 1), inverse(3, 2)};
}

float label_fade(float distance, float near_distance, float far_distance) {
    if (distance <= near_distance) return 1.0f;
    if (distance >= far_distance || far_distance <= near_distance) return 0.0f;
    return 1.0f - (distance - near_distance) / (far_distance - near_distance);
}

const FormHead* form_head(int species) {
    // Round 54's measurement of the skinned figure (SQ_WAIT in dng101b1 and
    // in the lobby; tests/labels_tests.cpp keeps it all): `height` the
    // highest point within 2 units of the head joint, the higher of the two
    // stances; `reach` how far that point stands above the head joint, the
    // more of the two; `joint` the head joint's highest standing height.
    static constexpr FormHead kHeads[16] = {
        {8.3f, 2.3f, 6.0f},   // Agumon
        {9.2f, 3.1f, 6.1f},   // Veemon (its horn and ears)
        {9.6f, 3.6f, 6.0f},   // Guilmon
        {8.2f, 2.4f, 6.0f},   // Dorumon (3.1 in the field's stance would lift the lobby's)
        {11.9f, 2.7f, 11.2f}, // WereGarurumon (the field's crouch, its collar; reach: the head's top, both stances)
        {14.0f, 2.8f, 11.2f}, // H-Kabuterimon (the horn's tip reaches 16.4, further out ahead)
        {12.8f, 2.7f, 10.8f}, // WarGreymon
        {12.7f, 1.5f, 11.3f}, // Cl-Angemon (the wings beside the head reach 15.7)
        {13.8f, 2.6f, 11.2f}, // Beelzemon BM (the raised gun reaches 25.5 beside it)
        {13.2f, 1.9f, 11.3f}, // Alphamon
        {12.8f, 2.7f, 10.8f}, // BK WarGreymon
        {13.7f, 2.5f, 11.2f}, // IPDramon FM
        {14.0f, 2.8f, 11.2f}, // IPDramon PM
        {13.7f, 2.4f, 11.3f}, // MetalGarurumon
        {13.3f, 2.1f, 11.2f}, // Gallantmon CM
        {13.6f, 2.4f, 11.2f}, // Susanomon
    };
    return species >= 0 && species < 16 ? &kHeads[species] : nullptr;
}

float form_label_height(int species) {
    const auto* head = form_head(species);
    return head ? std::max(head->height, head->joint + head->reach) : 30.0f;
}

float form_head_rise(int species, float joint_height) {
    const auto* head = form_head(species);
    return head ? std::max(0.0f, joint_height + head->reach - head->height) : 0.0f;
}

int label_species(std::string_view drawn_model, std::string_view player_model, int stats_species) {
    if (const auto* drawn = form_by_model(drawn_model)) return drawn->species;
    if (stats_species >= 0) return stats_species;
    if (const auto* form = form_by_model(player_model)) return form->species;
    return -1;
}

engine::Vec3f head_point(const engine::Node& node, int species, std::optional<float> pose_top, std::optional<engine::Vec3f> head) {
    const auto& m = node.matrix;
    const engine::Vec3f origin{m(3, 0), m(3, 1), m(3, 2)};
    // A form of the table: its head height plus the head's rise, never the
    // highest joint (a raised weapon or arm, which round 50 followed, made
    // the tag hop); without a pose to follow the higher stance's height.
    if (const auto* form = form_head(species)) {
        if (!head) return {origin.x, origin.y + form_label_height(species) + kHeadGap, origin.z};
        return {origin.x + head->x, origin.y + form->height + kHeadGap + std::max(0.0f, head->y), origin.z + head->z};
    }
    // Else the joints, which sit inside the figure: a margin above the highest.
    if (pose_top && *pose_top - origin.y > 2.0f && *pose_top - origin.y < 150.0f)
        return {origin.x, *pose_top + std::clamp((*pose_top - origin.y) * kJointMargin, kJointMarginMin, kJointMarginMax), origin.z};
    float top = 0;
    bool found = false;
    bound_top(node, m, 0, top, found);
    float height = found ? (top - origin.y) * kBoundShare : 0.0f;
    // A box that is no figure's height (a level-sized cull box, a flat one)
    // gives way to the table.
    if (!found || height < 4.0f || height > 80.0f) height = form_label_height(species);
    return {origin.x, origin.y + height + 1.0f, origin.z};
}

std::optional<float> player_pose_top(const engine::Engine& engine, std::string_view model, int index, const engine::Node& node) {
    const auto* actions = engine.get<engine::ActionManager>();
    const auto* ram = actions ? actions->find(model, index) : nullptr;
    if (!ram || ram->actors.empty() || !ram->actors.front()) return std::nullopt;
    const auto& pose = ram->traversed;
    if (!pose.skeleton || pose.bind_pose || pose.bone_world.empty()) return std::nullopt;
    // GETBONEMATRIX's joint in the model's space (bone * actor matrix), then
    // the model's placement.
    const engine::Mat4 to_world = ram->actors.front()->actor_matrix * node.matrix;
    std::optional<float> top;
    for (const auto& bone : pose.bone_world) {
        const auto p = engine::transform_point({0, 0, 0}, bone * to_world);
        if (std::isfinite(p.y) && (!top || p.y > *top)) top = p.y;
    }
    return top;
}

std::optional<engine::Vec3f> player_head_joint(const engine::Engine& engine, std::string_view model, int index, const engine::Node& node) {
    const auto* actions = engine.get<engine::ActionManager>();
    const auto* ram = actions ? actions->find(model, index) : nullptr;
    if (!ram || ram->actors.empty() || !ram->actors.front()) return std::nullopt;
    const auto& pose = ram->traversed;
    if (!pose.skeleton || pose.bind_pose) return std::nullopt;
    // The rookies' skeletons name it "Head", the other forms' "Head01".
    const auto& names = pose.skeleton->names;
    for (std::size_t bone = 0; bone < names.size() && bone < pose.bone_world.size(); ++bone) {
        std::string name = names[bone];
        for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (name != "head" && name != "head01") continue;
        const auto p = engine::transform_point({0, 0, 0}, pose.bone_world[bone] * ram->actors.front()->actor_matrix * node.matrix);
        if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) return p;
    }
    return std::nullopt;
}

HeadFollow advance_head_follow(std::optional<HeadFollow> current, engine::Vec3f offset, float rise, int steps) {
    offset.y = 0.0f;
    if (const float length = std::sqrt(offset.x * offset.x + offset.z * offset.z); length > kHeadReach)
        offset = offset * (kHeadReach / length);
    rise = std::max(0.0f, rise);
    if (!current) return {offset, rise, kLiftHold, rise};
    steps = std::clamp(steps, 1, 240);
    HeadFollow next = *current;
    const float target = rise + std::max(0.0f, (rise - current->rise) / float(steps) - kLiftLeadFree);
    next.rise = rise;
    // Each of the steps towards the same target (a catch-up update runs a
    // few; a long stall settles fully within some 200).
    for (int step = 0; step < steps; ++step) {
        next.offset = next.offset + (offset - next.offset) * kHeadFollow;
        if (target >= next.lift * kLiftRenew) {
            next.lift = std::max(next.lift, target);
            next.hold = kLiftHold;
        } else if (next.hold > 0) {
            --next.hold;
        } else {
            next.lift += (target - next.lift) * kLiftEase;
        }
    }
    next.offset.y = 0.0f;
    return next;
}

std::optional<engine::Vec3f> OverheadLabels::follow_head(std::uint64_t key, const engine::Node& node, int species, std::optional<engine::Vec3f> joint,
                                                         int steps) {
    auto it = std::find_if(head_follows_.begin(), head_follows_.end(), [&](const auto& entry) { return entry.first == key; });
    if (!joint) {
        if (it != head_follows_.end()) head_follows_.erase(it);
        return std::nullopt;
    }
    const auto& m = node.matrix;
    const engine::Vec3f offset{joint->x - m(3, 0), 0.0f, joint->z - m(3, 2)};
    const float rise = form_head_rise(species, joint->y - m(3, 1));
    if (it == head_follows_.end()) {
        head_follows_.emplace_back(key, advance_head_follow(std::nullopt, offset, rise, steps));
        it = std::prev(head_follows_.end());
    } else {
        it->second = advance_head_follow(it->second, offset, rise, steps);
    }
    return engine::Vec3f{it->second.offset.x, it->second.lift, it->second.offset.z};
}

LabelMode parse_label_mode(std::string_view text) {
    std::string mode(text);
    for (auto& c : mode) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (mode == "all" || mode == "on" || mode == "true" || mode == "1") return LabelMode::all;
    if (mode == "others") return LabelMode::others;
    if (mode == "off" || mode == "false" || mode == "0" || mode == "none") return LabelMode::off;
    return LabelMode::automatic;
}

const char* label_mode_name(LabelMode mode) {
    switch (mode) {
    case LabelMode::all: return "all";
    case LabelMode::others: return "others";
    case LabelMode::off: return "off";
    default: return "auto";
    }
}

bool label_visible(LabelMode mode, bool own, int players) {
    switch (mode) {
    case LabelMode::all: return true;
    case LabelMode::others: return !own;
    case LabelMode::off: return false;
    default: return players >= 2;
    }
}

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) out += i + 1 == names.size() ? " and " : ", ";
        out += names[i];
    }
    return out;
}

bool online_member_defeated(const engine::Engine& engine, int slot) {
    if (slot < 0 || slot > 3 || !(engine.online().present & (1u << slot))) return false;
    const auto* sys = engine.get<engine::GameSystem>();
    const auto* models = engine.get<engine::ModelCtrlManager>();
    if (!sys || !models) return false;
    if (const auto* player = models->player(slot)) return player->hp <= 0; // generated: dying at 0 HP
    if (sys->players[std::size_t(slot)].joined && sys->players[std::size_t(slot)].hp <= 0) return true;
    for (const auto& ram : models->rams())
        if (ram->generated && ram->index == slot && ram->info_name == "g_000bcaps") return true; // the capsule is out
    return false;
}

std::vector<std::string> online_notices(engine::Engine& engine, int give_up_held, int give_up_frames) {
    std::vector<std::string> lines;
    if (!engine.online_shared()) return lines;
    auto* sys = engine.get<engine::GameSystem>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!sys || !models) return lines;
    const auto name_of = [&](int slot) {
        std::string name = ascii_player_name(sys->players[std::size_t(std::clamp(slot, 0, 3))].name);
        return name.empty() ? "Player " + std::to_string(slot + 1) : name;
    };
    const int local = engine.view_slot();
    if (online_member_defeated(engine, local)) {
        lines.push_back("Defeated - wait for a raise, or hold START to give up");
        if (give_up_held > 0 && give_up_frames > 0)
            lines.push_back("Giving up... " + std::to_string(std::min(100, give_up_held * 100 / give_up_frames)) + "%");
        if (const int watched = engine.picture_slot(); watched != local) lines.push_back("Watching " + name_of(watched));
    }
    const auto& prompt = engine.exit_prompt();
    if (prompt.active && prompt.owner == local) {
        if (prompt.asking == local) {
            std::vector<std::string> stays;
            for (int slot = 0; slot < 4; ++slot)
                if (prompt.stays & (1u << slot)) stays.push_back(name_of(slot));
            if (!stays.empty()) lines.push_back(join_names(stays) + (stays.size() == 1 ? " stays here" : " stay here") + " unless asked along");
            if (prompt.boss_destination) lines.push_back("Warning: this way leads to a boss");
        } else if (prompt.asking >= 0) {
            lines.push_back("Waiting for " + name_of(prompt.asking));
        }
    } else if (prompt.active && prompt.asking == local && prompt.owner >= 0) {
        lines.push_back(name_of(prompt.owner) + " is leaving this area - come along?");
    }
    return lines;
}

void OverheadLabels::capture(engine::Engine& engine, int own_slot) {
    std::vector<OverheadLabel> local;
    int joined = 0;
    auto* sys = engine.get<engine::GameSystem>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    // The simulation steps since the last capture (a catch-up update runs
    // several; a frame counter that went back, a state load: one).
    const std::uint64_t frame = engine.frame();
    const int steps = follow_frame_ && frame > *follow_frame_ ? int(std::min<std::uint64_t>(frame - *follow_frame_, 240)) : 1;
    follow_frame_ = frame;
    for (int slot = 0; sys && models && slot < 4; ++slot) {
        const auto& player = sys->players[std::size_t(slot)];
        if (!player.joined) continue;
        ++joined;
        const auto* ram = models->player(slot);
        if (!ram || !ram->node) {
            // Seamless co-op: a defeated partner's capsule carries its name.
            if (!engine.online_shared() || slot == own_slot) continue;
            const auto* capsule = models->find("g_000bcaps", slot);
            if (!capsule || !capsule->generated || !capsule->node) continue;
            OverheadLabel label;
            label.key = 0x80 + std::uint64_t(slot);
            label.slot = slot;
            label.capsule = true;
            const auto& m = capsule->node->matrix;
            label.feet = {m(3, 0), m(3, 1), m(3, 2)};
            label.head = {m(3, 0), m(3, 1) + 18.0f, m(3, 2)};
            label.name = ascii_player_name(player.name);
            if (label.name.empty()) label.name = "Player " + std::to_string(slot + 1);
            label.level = player.stats.level;
            label.max_hp = player.max_hp;
            label.max_mp = player.max_mp;
            local.push_back(std::move(label));
            continue;
        }
        OverheadLabel label;
        label.key = std::uint64_t(slot);
        label.slot = slot;
        label.own = slot == own_slot;
        const int species = label_species(ram->info_name, player.digimon, player.stats.ready ? player.stats.species : -1);
        label.head = head_point(*ram->node, species, player_pose_top(engine, ram->info_name, slot, *ram->node),
                                follow_head(label.key, *ram->node, species, player_head_joint(engine, ram->info_name, slot, *ram->node), steps));
        label.feet = {ram->node->matrix(3, 0), ram->node->matrix(3, 1), ram->node->matrix(3, 2)};
        label.name = ascii_player_name(player.name);
        if (label.name.empty()) label.name = "Player " + std::to_string(slot + 1);
        label.level = player.stats.level;
        label.hp = player.hp;
        label.max_hp = player.max_hp;
        label.mp = player.mp;
        label.max_mp = player.max_mp;
        local.push_back(std::move(label));
    }
    capture(std::move(local), joined, engine.stage_name());
}

void OverheadLabels::capture(std::vector<OverheadLabel> local, int local_players, std::string_view stage) {
    previous_ = std::move(current_);
    current_ = std::move(local);
    players_ = local_players;
    for (const auto& remote : remote_) {
        if (remote.stage.empty() || remote.stage.size() != stage.size() ||
            !std::equal(stage.begin(), stage.end(), remote.stage.begin(),
                        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
            continue;
        ++players_;
        OverheadLabel label;
        label.key = 0x100 + remote.id;
        label.head = {remote.position.x, remote.position.y + form_label_height(remote.species) + kHeadGap, remote.position.z};
        label.feet = remote.position;
        label.name = remote.name;
        label.level = remote.level;
        label.hp = remote.hp;
        label.max_hp = remote.max_hp;
        label.mp = remote.mp;
        label.max_mp = remote.max_mp;
        current_.push_back(std::move(label));
    }
}

void OverheadLabels::reset() {
    previous_.clear();
    current_.clear();
    occlusion_.clear();
    head_follows_.clear();
    follow_frame_.reset();
    players_ = 0;
}

float OverheadLabels::ease_occlusion(std::uint64_t key, bool hidden) {
    auto it = std::find_if(occlusion_.begin(), occlusion_.end(), [&](const Occlusion& entry) { return entry.key == key; });
    if (it == occlusion_.end()) {
        occlusion_.push_back({key, hidden ? 1.0f : 0.0f, hidden ? kHideAfterFrames : 0});
        return occlusion_.back().value;
    }
    // Only a hide that lasts counts: the probes above the head also catch the
    // player's own upright weapon, EXP and damage numbers and effect sprites
    // for a frame or a few (a hidden k2 run: 77 of 81 hides lasted 8 frames
    // or less), real cover (trees, walls) lasts much longer.
    it->hidden_frames = hidden ? std::min(it->hidden_frames + 1, kHideAfterFrames) : 0;
    constexpr float kStep = 0.125f;
    it->value = it->hidden_frames >= kHideAfterFrames ? std::min(1.0f, it->value + kStep) : std::max(0.0f, it->value - kStep);
    return it->value;
}

std::vector<OverheadLabel> OverheadLabels::blended(float alpha) const {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    std::vector<OverheadLabel> out = current_;
    for (auto& label : out) {
        const auto before = std::find_if(previous_.begin(), previous_.end(), [&](const OverheadLabel& p) { return p.key == label.key; });
        if (before == previous_.end()) continue;
        if (engine::length(label.head - before->head) > kJumpDistance) continue;
        label.head = lerp(before->head, label.head, alpha);
    }
    return out;
}

std::vector<OcclusionProbe> occlusion_probes(const engine::Vec3f& head, const LabelView& view) {
    std::vector<OcclusionProbe> probes;
    // Above the head point (it sits just over the head): the picture there
    // is the scene behind the player unless something stands in front.
    const engine::Vec3f p{head.x, head.y + kProbeRise, head.z};
    const engine::Mat4 vp = view.camera.view * view.camera.projection;
    const float x = p.x * vp(0, 0) + p.y * vp(1, 0) + p.z * vp(2, 0) + vp(3, 0);
    const float y = p.x * vp(0, 1) + p.y * vp(1, 1) + p.z * vp(2, 1) + vp(3, 1);
    const float w = p.x * vp(0, 3) + p.y * vp(1, 3) + p.z * vp(2, 3) + vp(3, 3);
    if (!(w > 1e-4f)) return probes;
    const float px = view.wide_x + (x / w * view.widen * 0.5f + 0.5f) * view.wide_width;
    const float py = view.window_height - (view.wide_y + (y / w * 0.5f + 0.5f) * view.wide_height);
    const float spread = view.window_height * kProbeSpread;
    for (const float dx : {0.0f, -spread, spread}) probes.push_back({px + dx, py, w});
    return probes;
}

std::optional<float> view_distance(float window_depth, const engine::Mat4& projection) {
    if (!(window_depth < 1.0f) || window_depth < 0.0f || projection(2, 3) == 0.0f) return std::nullopt;
    // Row vectors: clip z = e.z * P22 + P32, clip w = e.z * P23 + P33.
    const float ndc = window_depth * 2.0f - 1.0f;
    const float divisor = ndc * projection(2, 3) - projection(2, 2);
    if (std::abs(divisor) < 1e-12f) return std::nullopt;
    const float eye_z = (projection(3, 2) - ndc * projection(3, 3)) / divisor;
    return eye_z * projection(2, 3) + projection(3, 3);
}

bool occluded_in_picture(const std::vector<OcclusionProbe>& probes, const std::function<std::optional<float>(float, float)>& depth_at,
                         const engine::Mat4& projection, float margin) {
    if (probes.empty()) return false;
    for (const auto& probe : probes) {
        const auto depth = depth_at(probe.x, probe.y);
        if (!depth) return false;
        const auto distance = view_distance(*depth, projection);
        // Nothing drawn there (the clear) or the scene lies behind: seen.
        if (!distance || *distance > probe.depth - margin) return false;
    }
    return true;
}

std::vector<LabelBox> layout_overhead_labels(const std::vector<OverheadLabel>& labels, const LabelView& view, LabelMode mode, int players,
                                             const std::function<std::pair<float, float>(const OverheadLabel&)>& size) {
    std::vector<LabelBox> boxes(labels.size());
    std::vector<float> distance(labels.size());
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto& label = labels[i];
        if (mode == LabelMode::off || (label.hp <= 0 && !label.capsule) || !label_visible(mode, label.own, players)) continue;
        const auto point = project_label(label.head, view);
        if (!point) continue;
        const float alpha = label_fade(point->distance) * (1.0f - (1.0f - kOccludedAlpha) * std::clamp(label.occlusion, 0.0f, 1.0f));
        if (alpha <= 0.01f) continue;
        const auto [w, h] = size(label);
        auto& box = boxes[i];
        box.shown = true;
        box.anchor_x = point->x;
        box.anchor_y = point->y;
        box.width = w;
        box.height = h;
        box.x = point->x - w * 0.5f;
        box.y = point->y - h; // its bottom (the bars) right above the head
        box.alpha = alpha;
        distance[i] = point->distance;
        order.push_back(i);
    }
    // Nearest first: a label that would overlap one already placed moves up
    // above it (again until it is clear), so tags of players standing
    // behind each other stack instead of covering each other.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return distance[a] < distance[b]; });
    std::vector<std::size_t> placed;
    for (const std::size_t i : order) {
        auto& box = boxes[i];
        for (int pass = 0; pass < 8; ++pass) {
            bool moved = false;
            for (const std::size_t j : placed) {
                const auto& other = boxes[j];
                const float gap = 2.0f;
                const bool overlap = box.x < other.x + other.width && other.x < box.x + box.width && box.y < other.y + other.height + gap &&
                                     other.y < box.y + box.height + gap;
                if (overlap) {
                    box.y = other.y - box.height - gap;
                    moved = true;
                }
            }
            if (!moved) break;
        }
        placed.push_back(i);
    }
    return boxes;
}

bool OverheadLabels::hidden(engine::Engine& engine) {
    if (engine.loading() || !engine.presenting() || engine.rebooting() || !engine.pad_input_enabled()) return true;
    if (engine.demo().running() > 0) return true;
    if (is_menu_stage(engine.stage_name())) return true;
    if (const auto* windows = engine.get<engine::SubWindowManager>(); windows && windows->window_open()) return true;
    return false;
}

} // namespace dw4::game
