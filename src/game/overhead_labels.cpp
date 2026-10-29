#include "overhead_labels.hpp"

#include "presence_data.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::game {
namespace {

// A label that moved further than this between two steps jumped (a warp, a
// stage entry): it is not blended.
constexpr float kJumpDistance = 40.0f;
// The actors' bounding boxes hold the bind pose (dng101b1: Agumon's box
// reaches 18.3 above its feet, the figure in its stance about 7.3): the
// label sits at this share of the box.
constexpr float kBoundShare = 0.45f;
// Above the highest joint, this share of the joint's height within these
// bounds: the bars sit just above the head. The joints sit inside the
// figure or at its top: Agumon's highest is 5.6 to 5.9 above its feet, the
// top of its head 7.3 (a 4-player lobby picture); Veemon's highest are the
// ears' roots at 7.4 to 7.7, the top of its head.
constexpr float kJointMargin = 0.25f, kJointMarginMin = 1.8f, kJointMarginMax = 6.0f;
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

float form_label_height(int species) {
    // Head heights of the 16 forms (world units above the model's origin),
    // used when the model has no pose and no bounding box (Agumon: 7.3).
    static constexpr float kHeights[16] = {8, 8, 8, 8, 14, 16, 14, 14, 14, 14, 14, 18, 16, 14, 16, 17};
    return species >= 0 && species < 16 ? kHeights[species] : 30.0f;
}

engine::Vec3f head_point(const engine::Node& node, int species, std::optional<float> pose_top) {
    const auto& m = node.matrix;
    const engine::Vec3f origin{m(3, 0), m(3, 1), m(3, 2)};
    // The joints sit inside the figure: a margin above the highest.
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

void OverheadLabels::capture(engine::Engine& engine, int own_slot) {
    std::vector<OverheadLabel> local;
    int joined = 0;
    auto* sys = engine.get<engine::GameSystem>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    for (int slot = 0; sys && models && slot < 4; ++slot) {
        const auto& player = sys->players[std::size_t(slot)];
        if (!player.joined) continue;
        ++joined;
        const auto* ram = models->player(slot);
        if (!ram || !ram->node) continue;
        OverheadLabel label;
        label.key = std::uint64_t(slot);
        label.slot = slot;
        label.own = slot == own_slot;
        const int species = player.stats.ready ? player.stats.species : -1;
        const auto* form = form_by_model(player.digimon);
        label.head = head_point(*ram->node, species >= 0 ? species : form ? form->species : -1,
                                player_pose_top(engine, ram->info_name, slot, *ram->node));
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
        label.head = {remote.position.x, remote.position.y + form_label_height(remote.species) + 1.0f, remote.position.z};
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
        if (mode == LabelMode::off || label.hp <= 0 || !label_visible(mode, label.own, players)) continue;
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
