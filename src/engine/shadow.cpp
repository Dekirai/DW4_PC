// beShadow01: projected shadows (igProjectiveShadowShader).

#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdlib>

namespace dw4::engine {
namespace {

// A row-vector orthographic projection (GX-style depth mapped to GL clip).
Mat4 orthographic(float half, float near_plane, float far_plane) {
    Mat4 r;
    r(0, 0) = 1.0f / half;
    r(1, 1) = 1.0f / half;
    r(2, 2) = -2.0f / (far_plane - near_plane);
    r(3, 2) = -(far_plane + near_plane) / (far_plane - near_plane);
    return r;
}

} // namespace

void ShadowManager::add_info(Engine& engine, const ObjectView& info) {
    if (const auto shader = info.ref(9)) {
        // 80314690: the shader's defaults; the light box is +-100 (+212..+224).
        shader_ = true;
        factor_ = shader.f32(13, 0.4f);
        size_ = std::clamp(shader.i32(14, 256), 32, 1024);
        extent_ = std::max(std::abs(shader.f32(48, 100.0f)), std::abs(shader.f32(49, -100.0f)));
        near_ = shader.f32(27, 0.1f);
        far_ = shader.f32(28, 1000.0f);
    }
    if (const auto caster = info.ref(8))
        if (Node* node = engine.scenes().node(caster)) casters_.push_back(node);
    if (const auto receiver = info.ref(7))
        if (Node* node = engine.scenes().node(receiver)) receivers_.push_back(node);
}

void ShadowManager::clear_stage(Engine&) {
    receivers_.clear();
    casters_.clear();
    shader_ = false;
    light_.reset();
}

void ShadowManager::update(Engine& engine) {
    if (!shader_) return;
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    const auto* actions = engine.get<ActionManager>();
    // 8031C1FC: the average position of the players (PLAYER GETMATRIX).
    Vec3f sum{};
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        const auto* player = models->player(i);
        if (!player || !player->node || !player->generated) continue;
        const auto& m = player->node->runtime_matrix ? *player->node->runtime_matrix : player->node->matrix;
        sum = sum + Vec3f{m(3, 0), m(3, 1), m(3, 2)};
        ++count;
    }
    if (count) light_ = Vec3f{sum.x / float(count), sum.y / float(count) + 200.0f, sum.z / float(count)};
    // A shadow proxy only casts while the instance it copies is out.
    for (auto* root : casters_)
        for (std::size_t i = 0; i < root->children.size(); ++i) {
            Node* child = root->children[i];
            if (child->kind != NodeKind::copy_model) continue;
            bool live = false;
            for (const auto& ram : models->rams())
                if (ram->node == child->copy_source) {
                    live = ram->generated && (!actions || actions->drawn(child->copy_source));
                    break;
                }
            child->hidden = !live;
            if (root->kind == NodeKind::switch_node) root->set_child_enabled(i, live);
        }
}

void ShadowManager::render(Engine& engine, const TraversalContext& context, ShadowFrame& frame) {
    frame.casters.clear();
    frame.receivers.clear();
    frame.active = shader_ && light_.has_value() && !casters_.empty() && !receivers_.empty();
    if (!frame.active) return;
    frame.factor = factor_;
    frame.size = size_;
    frame.light_view = look_at(*light_, *light_ - Vec3f{0, 1, 0}, {0, 0, -1});
    frame.light_projection = orthographic(extent_, near_, far_);
    TraversalContext caster = context;
    caster.output = &frame.casters;
    caster.actor_hook = nullptr;
    for (auto* root : casters_) traverse(root, caster);
    caster.output = &frame.receivers;
    for (auto* root : receivers_) traverse(root, caster);
    if (static const bool trace = std::getenv("DW4_TRACE_SHADOW") != nullptr; trace && engine.frame() % 120 == 0) {
        const std::size_t receiving = frame.receivers.size();
        engine.log("shadow light (" + std::to_string(light_->x) + ", " + std::to_string(light_->y) + ", " +
                   std::to_string(light_->z) + ") casters " + std::to_string(casters_.size()) + " items " +
                   std::to_string(frame.casters.size()) + " receiver items " + std::to_string(receiving));
    }
}

} // namespace dw4::engine
