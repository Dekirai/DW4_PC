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
// reaches 18.3 above its feet, the figure in its stance about 10): the
// label sits at this share of the box.
constexpr float kBoundShare = 0.6f;

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
    // used when the model has no bounding box.
    static constexpr float kHeights[16] = {12, 12, 12, 12, 22, 24, 22, 22, 22, 22, 22, 28, 24, 22, 24, 26};
    return species >= 0 && species < 16 ? kHeights[species] : 30.0f;
}

engine::Vec3f head_point(const engine::Node& node, int species, std::optional<float> pose_top) {
    const auto& m = node.matrix;
    const engine::Vec3f origin{m(3, 0), m(3, 1), m(3, 2)};
    // The joints sit inside the figure: a margin above the highest.
    if (pose_top && *pose_top - origin.y > 2.0f && *pose_top - origin.y < 150.0f) return {origin.x, *pose_top + 7.0f, origin.z};
    float top = 0;
    bool found = false;
    bound_top(node, m, 0, top, found);
    float height = found ? (top - origin.y) * kBoundShare : 0.0f;
    // A box that is no figure's height (a level-sized cull box, a flat one)
    // gives way to the table.
    if (!found || height < 4.0f || height > 80.0f) height = form_label_height(species);
    return {origin.x, origin.y + height + 3.0f, origin.z};
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
        label.head = {remote.position.x, remote.position.y + form_label_height(remote.species) + 3.0f, remote.position.z};
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
    players_ = 0;
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

bool OverheadLabels::hidden(engine::Engine& engine) {
    if (engine.loading() || !engine.presenting() || engine.rebooting() || !engine.pad_input_enabled()) return true;
    if (engine.demo().running() > 0) return true;
    if (is_menu_stage(engine.stage_name())) return true;
    if (const auto* windows = engine.get<engine::SubWindowManager>(); windows && windows->window_open()) return true;
    return false;
}

} // namespace dw4::game
