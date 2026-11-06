#include "presentation_actor.hpp"

#include "ghost_figures.hpp"

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/packages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <numbers>

namespace dw4::game {

using engine::DrawItem;
using engine::Mat4;
using engine::Node;
using engine::NodeKind;
using engine::ObjectView;
using engine::RenderState;
using engine::SceneCache;
using engine::TraversalContext;
using engine::Vec3f;

struct PresentationActor::Model {
    std::shared_ptr<const IgbDocument> document;
    std::unique_ptr<SceneCache> cache;
    Node* root{};
    Node* shadow{};
    std::unique_ptr<engine::AnimationDatabase> database;
    ObjectView action, control;
    float height = 20.0f;
};

namespace {
ObjectView find_info(const IgbDocument& document, std::string_view type, std::string_view name) {
    if (!document.info_list_reference) return {};
    for (const auto& element : document.object_list(*document.info_list_reference)) {
        if (!element) continue;
        ObjectView info(&document, *element);
        if (info.type() == type && info.name() == name) return info;
    }
    return {};
}
double now_seconds_or(double now, double fallback) { return now > 0 ? now : fallback; }
} // namespace

PresentationActor::PresentationActor(std::string package, std::string model) : package_(std::move(package)), model_name_(std::move(model)) {}

PresentationActor::~PresentationActor() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void PresentationActor::load(const MountedResources& resources) {
    std::lock_guard lock(mutex_);
    if (requested_) return;
    requested_ = true;
    resources_ = std::make_unique<MountedResources>(resources);
    thread_ = std::thread([this] { worker(); });
}

void PresentationActor::worker() {
    // One package, below the story's work: read and parse it from the copy
    // of the resources (a stage package: its first main member).
    std::shared_ptr<const IgbDocument> document;
    std::string error;
    try {
        if (package_.find('/') == std::string::npos) {
            const auto descriptor = load_package_descriptor(*resources_, package_);
            const PackageMember* main = nullptr;
            for (const auto& member : descriptor.members)
                if (member.suffix != "/s") {
                    main = &member;
                    break;
                }
            if (!main) throw std::runtime_error("no main member");
            document = std::make_shared<const IgbDocument>(read_package_member(*resources_, *main));
        } else {
            document = std::make_shared<const IgbDocument>(read_igb_document(resources_->read(package_)));
        }
    } catch (const std::exception& e) {
        error = e.what();
    }
    std::lock_guard lock(mutex_);
    document_ = std::move(document);
    error_ = error.empty() && !document_ ? "unreadable" : error;
    done_ = true;
}

bool PresentationActor::ready() {
    if (model_) return model_->root != nullptr;
    if (failed_) return false;
    std::shared_ptr<const IgbDocument> document;
    {
        std::lock_guard lock(mutex_);
        if (!done_) return false;
        if (!document_) {
            std::fprintf(stderr, "actor: %s: %s\n", package_.c_str(), error_.c_str());
            failed_ = true;
            return false;
        }
        document = document_;
    }
    // The nodes, the animation database and the clips, on this thread (as
    // the ghost figures build theirs).
    auto model = std::make_unique<Model>();
    model->document = document;
    model->cache = std::make_unique<SceneCache>();
    model->action = find_info(*document, "beAction2Info", model_name_);
    model->control = find_info(*document, "beModelCtrlInfo", model_name_);
    if (model->action) {
        const auto records = model->action.list(6);
        if (!records.empty() && records.front()) model->root = model->cache->node(records.front().ref(11));
        if (const auto db = model->action.ref(8)) model->database = std::make_unique<engine::AnimationDatabase>(db);
    }
    if (!model->root || !model->database || model->root->kind != NodeKind::model_node) {
        std::fprintf(stderr, "actor: %s has no model %s\n", package_.c_str(), model_name_.c_str());
        failed_ = true;
        return false;
    }
    for (Node* child : model->root->children)
        if (child->kind == NodeKind::lod && !child->children.empty()) {
            model->shadow = child->children.front();
            break;
        }
    model_ = std::move(model);
    return true;
}

std::string PresentationActor::sequence_clip(const std::string& sequence) {
    // The sequence's first ACTION and that action's last MOTIONSET clip
    // (ghost_figures.cpp's sequence_clip).
    if (!model_ || !model_->control || !model_->action) return {};
    std::string action_name;
    for (const auto& data : model_->control.list(5)) {
        if (!data || data.str(2) != "SEQ") continue;
        for (const auto& seq : data.list(4)) {
            if (!seq || seq.str(2) != sequence) continue;
            for (const auto& command : seq.list(3))
                if (command && command.type() == "beModelCtrlACTION") {
                    action_name = std::string(command.str(2));
                    break;
                }
            if (!action_name.empty()) break;
        }
    }
    if (action_name.empty()) return {};
    std::string clip;
    for (const auto& data : model_->action.list(9)) {
        if (!data || data.str(2) != action_name) continue;
        for (const auto& command : data.list(3))
            if (command && command.type() == "beAction2MOTIONSET") clip = std::string(command.str(2));
    }
    return clip;
}

void PresentationActor::set_place(Vec3f position, float yaw) {
    position_ = position;
    yaw_ = yaw;
}

void PresentationActor::update_fade(double now) {
    const double dt = last_fade_ > 0 ? std::clamp(now - last_fade_, 0.0, 0.25) : 0.0;
    last_fade_ = now;
    const bool loaded = model_ && model_->root;
    if (visible_ && loaded) alpha_ += dt / kFadeSeconds;
    else if (!visible_) alpha_ -= dt / kFadeSeconds;
    alpha_ = std::clamp(alpha_, 0.0, 1.0);
}

void PresentationActor::set_visible(bool visible, double now) {
    ready();
    update_fade(now);
    if (visible && !visible_ && alpha_ <= 0.0) born_ = now, started_ = false;
    visible_ = visible;
}

void PresentationActor::set_sequence(const std::string& sequence) { sequence_ = sequence.empty() ? "SQ_WAIT" : sequence; }

void PresentationActor::face(Vec3f point, double now, float rate) {
    const double dt = last_face_ > 0 ? std::clamp(now - last_face_, 0.0, 0.25) : 0.0;
    last_face_ = now;
    const float dx = point.x - position_.x, dz = point.z - position_.z;
    if (dx * dx + dz * dz < 1e-4f) return;
    const float target = std::atan2(dx, dz);
    float delta = target - yaw_;
    while (delta > std::numbers::pi_v<float>) delta -= 2 * std::numbers::pi_v<float>;
    while (delta < -std::numbers::pi_v<float>) delta += 2 * std::numbers::pi_v<float>;
    const float step = rate * float(dt);
    yaw_ += std::clamp(delta, -step, step);
}

void PresentationActor::draw(std::vector<DrawItem>& items, engine::ShadowFrame* shadow, const engine::FrameCamera& camera,
                             std::optional<Vec3f> own, double now) {
    drawn_items_ = 0;
    update_fade(now);
    if (alpha_ <= 0.0 || !ready()) return;
    auto& m = *model_;
    // The clip of the wanted sequence (a missing one: the database's first).
    std::string clip = sequence_clip(sequence_);
    if (clip.empty() || !m.database->has_clip(clip)) clip = sequence_clip("SQ_WAIT");
    if (clip.empty() || !m.database->has_clip(clip)) clip = m.database->first_clip_name();
    const auto time = std::int64_t((now - now_seconds_or(born_, now)) * 1e9);
    if (!started_ || clip != motion_.clip) {
        if (!started_) {
            motion_ = engine::MotionState{};
        } else {
            motion_.previous_clip = motion_.clip;
            motion_.previous_clock = motion_.clock;
            motion_.previous_fresh = motion_.fresh;
            motion_.previous_pose.clear();
        }
        motion_.blend_start = time;
        motion_.blend = started_ ? engine::frames_to_time(kMotionBlendFrames) : 0;
        motion_.clip = clip;
        motion_.clock = engine::StateClock{};
        motion_.fresh = true;
        started_ = true;
    }
    m.database->set_looping(clip, true);
    motion_.update(time);
    engine::evaluate_pose(*m.database, motion_, time, pose_, locals_);
    motion_.evaluated();

    // The scene's lights and fog (its first lit 3D item), as the figures.
    RenderState base;
    for (const auto& item : items) {
        const auto& s = item.state;
        if (!s.blend_enabled && !s.projection && !s.viewport && !s.view && s.lighting && !s.lights.empty()) {
            base.lights = s.lights;
            base.fog = s.fog;
            base.fog_params = s.fog_params;
            break;
        }
    }
    Mat4 inverse_view;
    if (engine::invert(camera.view, inverse_view)) {
        const Vec3f eye{inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
        const Vec3f centre{position_.x, position_.y + m.height * 0.5f, position_.z};
        const auto own_centre = own ? std::optional<Vec3f>(Vec3f{own->x, own->y + 10.0f, own->z}) : std::nullopt;
        camera_ = ghost_camera_alpha(eye, centre, own_centre, 8.0f);
    }
    const float alpha = float(alpha_ * camera_);
    if (alpha <= 0.004f) return;
    const Mat4 world = ghost_matrix(position_, yaw_);
    std::vector<DrawItem> out;
    TraversalContext context;
    context.cache = m.cache.get();
    context.time = time;
    context.camera = camera;
    context.cull_camera = camera;
    context.output = &out;
    context.actor_pose = [this](const Node&, bool) -> const engine::ActorPose* { return &pose_; };
    m.root->runtime_matrix = world;
    engine::traverse(m.root, context, Mat4{}, base, time);
    if (shadow && shadow->active && m.shadow && alpha_ >= 0.5) {
        TraversalContext caster = context;
        caster.output = &shadow->casters;
        caster.modelview_base = shadow->light_view;
        caster.projection_base = shadow->light_projection;
        engine::traverse(m.shadow, caster, world, RenderState{}, time);
    }
    constexpr std::uint64_t kInstance = 0x6E5F67656B6F61ull; // "n_gekoa"
    for (auto& item : out) item.instance ^= kInstance;
    drawn_items_ = out.size();
    const auto at = ghost_insert_points(items);
    if (alpha >= 0.999f) {
        items.insert(items.begin() + std::ptrdiff_t(at.opaque), std::make_move_iterator(out.begin()), std::make_move_iterator(out.end()));
        return;
    }
    // Fading: the nearest surface's depth first, then the colour faded.
    std::vector<DrawItem> blended;
    for (const auto& item : out) {
        DrawItem depth = item;
        depth.state.color_write = false;
        depth.state.blend_enabled = false;
        depth.state.modulate.reset();
        blended.push_back(std::move(depth));
    }
    for (auto& item : out) {
        auto modulate = item.state.modulate.value_or(engine::Vec4f{1, 1, 1, 1});
        modulate.w *= alpha;
        item.state.modulate = modulate;
        if (!item.state.blend_enabled) {
            item.state.blend_enabled = true;
            item.state.blend_src = 4;
            item.state.blend_dst = 5;
        }
        item.state.depth_write = false;
        blended.push_back(std::move(item));
    }
    items.insert(items.begin() + std::ptrdiff_t(at.blended), std::make_move_iterator(blended.begin()), std::make_move_iterator(blended.end()));
}

} // namespace dw4::game
