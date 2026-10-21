// beShadow01: projected shadows (igProjectiveShadowShader).

#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdlib>

namespace dw4::engine {
namespace {

// 0x804152A8: the eight blur sample offsets (x, y) of the map's blur quads.
constexpr float kBlurOffsets[16] = {0.5625f, 0.4375f, 0.0625f, 0.9375f, 0.3125f, 0.6875f, 0.6875f, 0.8125f,
                                    0.8125f, 0.1875f, 0.9375f, 0.5625f, 0.4375f, 0.0625f, 0.1875f, 0.3125f};

// 801FD040 starts the map border at 1 texel (0x801FD05C) and the blur quad
// builder loc_801FCBE4 raises it to ceil(6 * offset) + 1 for every sample
// offset beyond it (0x801FCE28..0x801FCE88), whatever _blurState is; the
// border then insets _shadowInnerViewport (0x801FD1D0) and the texture
// matrix flt_804F8CC8 (0x801FD288..0x801FD2EC).
constexpr int shadow_border() {
    int border = 1;
    for (const float offset : kBlurOffsets) {
        const float scaled = 6.0f * offset;
        if (scaled <= float(border)) continue;
        int ceiling = int(scaled);
        if (float(ceiling) < scaled) ++ceiling;
        border = ceiling + 1;
    }
    return border;
}
static_assert(shadow_border() == 7);

// 80129B38: a row-vector orthographic projection (depth to -1..1).
Mat4 orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane) {
    Mat4 r;
    r(0, 0) = 2.0f / (right - left);
    r(1, 1) = 2.0f / (top - bottom);
    r(2, 2) = -2.0f / (far_plane - near_plane);
    r(3, 0) = -(right + left) / (right - left);
    r(3, 1) = -(top + bottom) / (top - bottom);
    r(3, 2) = -(far_plane + near_plane) / (far_plane - near_plane);
    return r;
}

} // namespace

void ShadowManager::add_info(Engine& engine, const ObjectView& info) {
    if (const auto shader = info.ref(9)) {
        // The field defaults of the shader's registration (801B56B4, table
        // 0x804AD81C): _shadowFactor 0.3, _shadowTexW/H 128, box +-5, near
        // 0.1, far 1000, _upVector (1,0,0).
        shader_ = true;
        factor_ = shader.f32(13, 0.3f);
        width_ = std::clamp(shader.i32(14, 128), 32, 1024);
        height_ = std::clamp(shader.i32(15, 128), 32, 1024);
        top_ = shader.f32(48, 5.0f);
        bottom_ = shader.f32(49, -5.0f);
        right_ = shader.f32(50, 5.0f);
        left_ = shader.f32(51, -5.0f);
        // 80314690: beShadow01 sets _drawReceiver 0, _useOrthographic 1 and
        // _blurState 0 (the only path drawn here) and replaces the default
        // box (_top == 5, 0x80314708) with +-100 (+0xD4..+0xE0).
        if (top_ == 5.0f) {
            top_ = right_ = 100.0f;
            bottom_ = left_ = -100.0f;
        }
        near_ = shader.f32(27, 0.1f);
        far_ = shader.f32(28, 1000.0f);
        up_ = shader.vec3(29).value_or(Vec3f{1, 0, 0});
        // 8031476C (0x80314888..0x803148B8) gives the shader's _light (an
        // igLightAttr, [45]) the new position and its direction, and 801FE718
        // copies both into _lightPos/_lightDir ([8]/[9]) every traversal.
        direction_ = shader.vec3(9).value_or(Vec3f{0, -1, 0});
        if (const auto light = shader.ref(45)) direction_ = light.vec3(10).value_or(direction_);
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

void ShadowManager::traverse(const TraversalContext& context, std::vector<DrawItem>* casters, std::vector<DrawItem>* receivers) {
    TraversalContext caster = context;
    caster.output = casters;
    caster.actor_hook = nullptr;
    for (auto* root : casters_) dw4::engine::traverse(root, caster);
    caster.output = receivers;
    for (auto* root : receivers_) dw4::engine::traverse(root, caster);
}

void ShadowManager::traverse(const TraversalContext& context) {
    if (active()) traverse(context, nullptr, nullptr);
}

void ShadowManager::render(Engine& engine, const TraversalContext& context, ShadowFrame& frame) {
    frame.casters.clear();
    frame.receivers.clear();
    frame.active = active();
    if (!frame.active) return;
    frame.factor = factor_;
    frame.width = width_;
    frame.height = height_;
    frame.border = shadow_border();
    // 801FE5E4 (0x801FE640): lookAt(_lightPos, _lightPos + _lightDir, _upVector).
    frame.light_view = look_at(*light_, *light_ + direction_, up_);
    // 801FE5E4 (0x801FE66C): ortho(_left, _right, _bottom, _top, near, far).
    frame.light_projection = orthographic(left_, right_, bottom_, top_, near_, far_);
    traverse(context, &frame.casters, &frame.receivers);
    if (static const bool trace = std::getenv("DW4_TRACE_SHADOW") != nullptr; trace && engine.frame() % 120 == 0) {
        const std::size_t receiving = frame.receivers.size();
        engine.log("shadow light (" + std::to_string(light_->x) + ", " + std::to_string(light_->y) + ", " +
                   std::to_string(light_->z) + ") dir (" + std::to_string(direction_.x) + ", " +
                   std::to_string(direction_.y) + ", " + std::to_string(direction_.z) + ") up (" +
                   std::to_string(up_.x) + ", " + std::to_string(up_.y) + ", " + std::to_string(up_.z) +
                   ") casters " + std::to_string(casters_.size()) + " items " +
                   std::to_string(frame.casters.size()) + " receiver items " + std::to_string(receiving));
    }
}

} // namespace dw4::engine
