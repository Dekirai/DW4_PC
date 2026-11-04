#include "ghost_figures.hpp"

#include "overhead_labels.hpp"
#include "presence_data.hpp"

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>

namespace dw4::game {

using engine::ActorPose;
using engine::AnimationDatabase;
using engine::DrawItem;
using engine::Mat4;
using engine::MotionState;
using engine::Node;
using engine::NodeKind;
using engine::ObjectView;
using engine::RenderState;
using engine::SceneCache;
using engine::TraversalContext;
using engine::Vec3f;

engine::Mat4 ghost_matrix(const Vec3f& position, float yaw) {
    // A turn about Y whose Z row is (sin yaw, 0, cos yaw): the facing the
    // sender's presence took from its node (atan2 of its Z row).
    Mat4 m;
    const float c = std::cos(yaw), s = std::sin(yaw);
    m(0, 0) = c;
    m(0, 2) = -s;
    m(2, 0) = s;
    m(2, 2) = c;
    m(3, 0) = position.x;
    m(3, 1) = position.y;
    m(3, 2) = position.z;
    return m;
}

bool same_stage(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

GhostInsert ghost_insert_points(const std::vector<DrawItem>& items) {
    GhostInsert at;
    bool blended_seen = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& s = items[i].state;
        const bool world = !s.projection && !s.viewport;
        if (!s.blend_enabled) {
            if (world) at.opaque = i + 1;
        } else {
            if (!blended_seen) {
                blended_seen = true;
                at.blended = std::max(at.blended, i); // (at least the first blended item's place)
            }
            if (world) at.blended = i + 1;
        }
    }
    if (!blended_seen) at.blended = items.size();
    at.blended = std::max(at.blended, at.opaque);
    return at;
}

float ghost_camera_alpha(const Vec3f& eye, const Vec3f& centre, std::optional<Vec3f> own, float radius) {
    constexpr float kNear = 25.0f, kFar = 60.0f; // eye distances: faint from kFar down to kNear
    float alpha = 1.0f;
    const float d = engine::length(centre - eye);
    if (d < kFar) alpha = std::min(alpha, kCameraAlpha + (1.0f - kCameraAlpha) * std::clamp((d - kNear) / (kFar - kNear), 0.0f, 1.0f));
    if (own && radius > 0) {
        const Vec3f to_own = *own - eye;
        const float length = engine::length(to_own);
        if (length > 1.0f) {
            const Vec3f dir = to_own * (1.0f / length);
            const float along = engine::dot(centre - eye, dir);
            // In front of the own player (by more than half the radius) and
            // close to the line of sight: a passer-by in the way.
            if (along > 0 && along < length - radius * 0.5f) {
                const float off = engine::length(centre - eye - dir * along);
                if (off < radius) alpha = std::min(alpha, kCameraAlpha + (1.0f - kCameraAlpha) * std::clamp((off / radius - 0.6f) / 0.4f, 0.0f, 1.0f));
            }
        }
        // Standing in the own player (every member arrives at the same entry
        // point of its own lobby): faint from kOverlapFar in to kOverlapNear.
        constexpr float kOverlapNear = 5.0f, kOverlapFar = 12.0f;
        const float flat = std::hypot(centre.x - own->x, centre.z - own->z);
        if (flat < kOverlapFar && std::abs(centre.y - own->y) < 30.0f)
            alpha = std::min(alpha, kCameraAlpha + (1.0f - kCameraAlpha) * std::clamp((flat - kOverlapNear) / (kOverlapFar - kOverlapNear), 0.0f, 1.0f));
    }
    return alpha;
}

struct GhostFigures::Model {
    std::shared_ptr<const IgbDocument> document;
    std::unique_ptr<SceneCache> cache;
    Node* root{};   // the model instance (beModelCtrlNode2), placed by its runtime matrix
    Node* shadow{}; // its shadow proxy level ("Kage", the igLod's first child)
    std::unique_ptr<AnimationDatabase> database;
    std::string idle, run; // SQ_WAIT's and SQ_WALK's clips of the first weapon label
    std::vector<Node*> actors;
    bool failed{};
};

struct GhostFigures::Weapon {
    std::shared_ptr<const IgbDocument> document;
    std::unique_ptr<SceneCache> cache;
    std::vector<std::unique_ptr<Node>> selects; // one igModelViewMatrixBoneSelect per part, on this form's bone
    bool failed{};
};

struct GhostFigures::Figure {
    std::uint64_t key{};
    std::uint32_t segment{};
    int species{};
    std::string weapon;
    Vec3f position{};
    float yaw{};
    std::string clip;
    bool loop = true;
    float speed{};
    double alpha{};       // the arrival / leave fade
    double camera = 1.0;  // the camera fade (ghost_camera_alpha, eased)
    bool leaving{};
    double born{};        // the figure's clock starts here
    MotionState motion;
    bool started{};
    ActorPose pose;
    std::vector<Mat4> locals;
};

namespace {
std::string package(const std::string& name) { return "_common/" + name + ".igb"; }

// A weapon package name (the weapon table's MODEL column: w_sword01,
// w_common01): anything else from the network is not loaded.
bool weapon_package(std::string_view name) {
    if (name.size() < 3 || name.size() > 24 || name.substr(0, 2) != "w_") return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// The model's infos named `name` of a parsed package.
ObjectView find_info(const IgbDocument& document, std::string_view type, std::string_view name) {
    if (!document.info_list_reference) return {};
    for (const auto& element : document.object_list(*document.info_list_reference)) {
        if (!element) continue;
        ObjectView info(&document, *element);
        if (info.type() == type && info.name() == name) return info;
    }
    return {};
}

// SQ_WAIT / SQ_WALK of the model's control info: the first ACTION of the
// sequence (its first weapon label, "dual"), and that action's last
// MOTIONSET clip (RUNND1 sets waitD, then runD).
std::string sequence_clip(const ObjectView& control, const ObjectView& action, std::string_view sequence) {
    if (!control || !action) return {};
    std::string action_name;
    for (const auto& data : control.list(5)) {
        if (!data || data.str(2) != "SEQ") continue;
        for (const auto& seq : data.list(4)) {
            if (!seq || seq.str(2) != sequence) continue;
            for (const auto& command : seq.list(3))
                if (command && command.type() == "beModelCtrlACTION") {
                    action_name = std::string(command.str(2));
                    break;
                }
        }
    }
    if (action_name.empty()) return {};
    std::string clip;
    for (const auto& data : action.list(9)) {
        if (!data || data.str(2) != action_name) continue;
        for (const auto& command : data.list(3))
            if (command && command.type() == "beAction2MOTIONSET") clip = std::string(command.str(2));
    }
    return clip;
}
} // namespace

GhostFigures::GhostFigures() = default;

GhostFigures::~GhostFigures() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void GhostFigures::set_resources(const MountedResources& resources) {
    std::lock_guard lock(mutex_);
    if (resources_) return;
    resources_ = std::make_unique<MountedResources>(resources);
}

bool GhostFigures::request(const std::string& path) {
    std::lock_guard lock(mutex_);
    if (!resources_) return false;
    if (requested_[path]) return true;
    requested_[path] = true;
    jobs_.push_back({path});
    if (!thread_.joinable()) thread_ = std::thread([this] { worker(); });
    wake_.notify_one();
    return true;
}

std::size_t GhostFigures::requested_packages() {
    std::lock_guard lock(mutex_);
    return requested_.size();
}

void GhostFigures::worker() {
    // Reads and parses packages (a copy of the resources; the engine's own
    // object is never touched here). Below normal work: one at a time.
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        std::shared_ptr<const IgbDocument> document;
        std::string error;
        try {
            document = std::make_shared<const IgbDocument>(read_igb_document(resources_->read(job.path)));
        } catch (const std::exception& e) {
            error = e.what();
        }
        std::lock_guard lock(mutex_);
        if (!requested_.contains(job.path)) continue; // (a weapon package dropped meanwhile)
        if (document) done_[job.path] = std::move(document);
        else failed_[job.path] = error.empty() ? "unreadable" : error;
    }
}

void GhostFigures::preload(int species, const std::string& weapon) {
    // (Every frame for every member: nothing is built for a known one.)
    if (species >= 0 && species < 256) {
        const auto index = std::size_t(species);
        if (forms_requested_.size() <= index) forms_requested_.resize(index + 1);
        if (!forms_requested_[index])
            if (const auto* form = form_by_species(species)) forms_requested_[index] = request(package(form->model));
    }
    if (weapon_package(weapon)) use_weapon(weapon);
}

void GhostFigures::use_weapon(const std::string& name) {
    if (auto found = weapons_.find(name); found != weapons_.end()) {
        found->second.used = tick_;
        return;
    }
    if (weapons_.size() >= kMaxWeapons) {
        // The package asked for longest ago goes (never one of this pass:
        // with more than kMaxWeapons at once the new one waits).
        auto oldest = weapons_.end();
        for (auto it = weapons_.begin(); it != weapons_.end(); ++it)
            if (it->second.used < tick_ && (oldest == weapons_.end() || it->second.used < oldest->second.used)) oldest = it;
        if (oldest == weapons_.end()) return;
        {
            std::lock_guard lock(mutex_);
            const auto& path = oldest->second.path;
            requested_.erase(path);
            done_.erase(path);
            failed_.erase(path);
            std::erase_if(jobs_, [&](const Job& job) { return job.path == path; });
        }
        weapons_.erase(oldest);
    }
    WeaponPackage entry;
    entry.path = package(name);
    entry.used = tick_;
    if (!request(entry.path)) return; // (no resources yet)
    weapons_.emplace(name, std::move(entry));
}

int GhostFigures::shown() const {
    // Members, not figures: a form change or a jump fades one figure over
    // into another of the same member. A figure still on the screen counts,
    // a staying one once its form is loaded, so a member whose new form is
    // still loading while its old figure fades out stays counted.
    std::vector<std::uint64_t> keys;
    for (const auto& f : figures_) {
        const auto it = models_.find(f->species);
        const bool loaded = it != models_.end() && it->second && it->second->root;
        if (!(f->alpha > 0.0 || (!f->leaving && loaded))) continue;
        if (std::find(keys.begin(), keys.end(), f->key) == keys.end()) keys.push_back(f->key);
    }
    return int(keys.size());
}

std::size_t GhostFigures::loaded_forms() const {
    std::size_t n = 0;
    for (const auto& [species, model] : models_) n += model && model->root ? 1 : 0;
    return n;
}

GhostFigures::Model* GhostFigures::model(int species) {
    if (auto found = models_.find(species); found != models_.end()) return found->second.get();
    const auto* form = form_by_species(species);
    if (!form) return nullptr;
    const auto path = package(form->model);
    std::shared_ptr<const IgbDocument> document;
    {
        std::lock_guard lock(mutex_);
        if (auto failed = failed_.find(path); failed != failed_.end()) {
            std::fprintf(stderr, "ghosts: %s: %s\n", path.c_str(), failed->second.c_str());
            auto model = std::make_unique<Model>();
            model->failed = true;
            return models_.emplace(species, std::move(model)).first->second.get();
        }
        auto found = done_.find(path);
        if (found == done_.end()) return nullptr; // still loading
        document = std::move(found->second);
        done_.erase(found);
    }
    // The nodes, the animation database and the clips, on this thread (the
    // GeometryData of a first traversal too: their serials are not atomic).
    auto model = std::make_unique<Model>();
    model->document = document;
    model->cache = std::make_unique<SceneCache>();
    const auto action = find_info(*document, "beAction2Info", form->model);
    const auto control = find_info(*document, "beModelCtrlInfo", form->model);
    if (action) {
        const auto records = action.list(6);
        if (!records.empty() && records.front()) {
            model->root = model->cache->node(records.front().ref(11));
            for (const auto& actor : records.front().list(13))
                if (actor) model->actors.push_back(model->cache->node(actor));
        }
        if (const auto db = action.ref(8)) model->database = std::make_unique<AnimationDatabase>(db);
    }
    if (!model->root || !model->database || model->root->kind != NodeKind::model_node) {
        std::fprintf(stderr, "ghosts: %s has no player model %s\n", path.c_str(), form->model);
        model->failed = true;
        model->root = nullptr;
        return models_.emplace(species, std::move(model)).first->second.get();
    }
    for (Node* child : model->root->children)
        if (child->kind == NodeKind::lod && !child->children.empty()) {
            model->shadow = child->children.front();
            break;
        }
    model->idle = sequence_clip(control, action, "SQ_WAIT");
    model->run = sequence_clip(control, action, "SQ_WALK");
    if (model->idle.empty() || !model->database->has_clip(model->idle)) model->idle = model->database->first_clip_name();
    if (model->run.empty() || !model->database->has_clip(model->run)) model->run = model->idle;
    return models_.emplace(species, std::move(model)).first->second.get();
}

GhostFigures::Weapon* GhostFigures::weapon(const std::string& name, const Model& model) {
    const auto entry = weapons_.find(name);
    if (entry == weapons_.end()) return nullptr; // (not a weapon package, or not asked for: preload)
    entry->second.used = tick_;
    auto& forms = entry->second.forms;
    if (auto found = forms.find(&model); found != forms.end()) return found->second.get();
    const auto& path = entry->second.path;
    std::shared_ptr<const IgbDocument> document;
    {
        std::lock_guard lock(mutex_);
        if (failed_.contains(path)) {
            auto weapon = std::make_unique<Weapon>();
            weapon->failed = true;
            return forms.emplace(&model, std::move(weapon)).first->second.get();
        }
        const auto found = done_.find(path);
        if (found == done_.end()) return nullptr; // still loading
        document = found->second; // (kept: the other forms build their own parts from it)
    }
    auto weapon = std::make_unique<Weapon>();
    weapon->document = document;
    weapon->cache = std::make_unique<SceneCache>();
    // beWeaponInfo (WeaponManager::add_info): list 7 the parts, list 8 their
    // bones; GENERATE hangs each part under the model's igActor nodes in an
    // igModelViewMatrixBoneSelect of its bone.
    const auto info = find_info(*document, "beWeaponInfo", name);
    const auto& skeleton = model.database->skeleton();
    if (info) {
        const auto parts = info.list(7);
        const auto joints = info.list(8);
        for (std::size_t i = 0; i < parts.size() && i < joints.size(); ++i) {
            if (!parts[i] || !joints[i]) continue;
            const std::string joint(joints[i].str(2));
            const auto bone = std::find(skeleton.names.begin(), skeleton.names.end(), joint);
            Node* part = weapon->cache->node(parts[i]);
            if (bone == skeleton.names.end() || !part) continue;
            auto select = std::make_unique<Node>();
            select->kind = NodeKind::bone_select;
            select->name = joint;
            select->bone_index = int(bone - skeleton.names.begin());
            select->children.push_back(part);
            weapon->selects.push_back(std::move(select));
        }
    }
    if (weapon->selects.empty()) weapon->failed = true;
    return forms.emplace(&model, std::move(weapon)).first->second.get();
}

void GhostFigures::update(std::vector<GhostTarget> targets, std::optional<Vec3f> own, double now) {
    const double dt = last_update_ > 0 ? std::clamp(now - last_update_, 0.0, 0.25) : 0.0;
    last_update_ = now;
    // The nearest kMaxFigures (one per member).
    std::erase_if(targets, [](const GhostTarget& t) { return !form_by_species(t.species); });
    if (own && targets.size() > kMaxFigures) {
        std::stable_sort(targets.begin(), targets.end(), [&](const GhostTarget& a, const GhostTarget& b) {
            return engine::length(a.position - *own) < engine::length(b.position - *own);
        });
    }
    if (targets.size() > kMaxFigures) targets.resize(kMaxFigures);
    for (const auto& t : targets) preload(t.species, t.weapon);
    for (auto& figure : figures_) {
        const auto found = std::find_if(targets.begin(), targets.end(), [&](const GhostTarget& t) { return t.key == figure->key; });
        // Gone, another form or a jump: this figure fades out where it is
        // (a new one fades in at the new place).
        if (found == targets.end() || found->species != figure->species || found->segment != figure->segment) figure->leaving = true;
    }
    for (const auto& t : targets) {
        auto it = std::find_if(figures_.begin(), figures_.end(), [&](const auto& f) { return f->key == t.key && !f->leaving; });
        if (it == figures_.end()) {
            auto figure = std::make_unique<Figure>();
            figure->key = t.key;
            figure->segment = t.segment;
            figure->species = t.species;
            figure->born = now;
            figures_.push_back(std::move(figure));
            it = figures_.end() - 1;
        }
        auto& f = **it;
        f.position = t.position;
        f.yaw = t.yaw;
        f.clip = t.clip;
        f.loop = t.loop;
        f.speed = t.speed;
        f.weapon = t.weapon;
    }
    for (auto& figure : figures_) {
        const auto* m = model(figure->species);
        const bool ready = m && m->root;
        if (figure->leaving) figure->alpha -= dt / kFadeSeconds;
        else if (ready) figure->alpha += dt / kFadeSeconds;
        figure->alpha = std::clamp(figure->alpha, 0.0, 1.0);
    }
    std::erase_if(figures_, [](const auto& f) { return f->leaving && f->alpha <= 0.0; });
    ++tick_;
}

void GhostFigures::clear() {
    figures_.clear();
    drawn_ = 0;
}

void GhostFigures::pose(Figure& f, Model& m, double now) {
    // The figure's own clock, MOTIONSET's way (ActionManager::execute): a new
    // clip fades in over kMotionBlendFrames from the one before (or, while a
    // fade still runs, from the pose drawn last).
    const auto time = std::int64_t((now - f.born) * 1e9);
    std::string clip = f.clip;
    if (clip.empty() || !m.database->has_clip(clip)) clip = f.speed > kWalkSpeed ? m.run : m.idle;
    auto& motion = f.motion;
    if (!f.started || clip != motion.clip) {
        if (!f.started) {
            motion = MotionState{};
        } else if (motion.layered()) {
            motion.previous_pose = f.locals;
            motion.previous_clip.clear();
            motion.previous_fresh = false;
            motion.hidden = false;
        } else {
            motion.previous_clip = motion.clip;
            motion.previous_clock = motion.clock;
            motion.previous_fresh = motion.fresh;
            motion.previous_pose.clear();
        }
        motion.blend_start = time;
        motion.blend = f.started ? engine::frames_to_time(kMotionBlendFrames) : 0;
        motion.clip = clip;
        motion.clock = engine::StateClock{};
        motion.fresh = true;
        f.started = true;
    }
    // (The loop mode belongs to the clip in every database: the wait and run
    // clips loop, a one-shot clip holds its last picture.)
    m.database->set_looping(clip, f.clip.empty() || f.loop);
    motion.update(time);
    engine::evaluate_pose(*m.database, motion, time, f.pose, f.locals);
    motion.evaluated();
}

void GhostFigures::draw(std::vector<DrawItem>& items, engine::ShadowFrame* shadow, const engine::FrameCamera& camera,
                        std::optional<Vec3f> own, double now) {
    drawn_ = 0;
    if (figures_.empty()) return;
    // The scene's lights and fog: those of its first lit 3D item (every
    // model of the stage hangs below the same root light set).
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
    std::optional<Vec3f> eye;
    if (engine::invert(camera.view, inverse_view)) eye = Vec3f{inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
    const double dt = last_draw_ > 0 ? std::clamp(now - last_draw_, 0.0, 0.25) : 0.0;
    last_draw_ = now;
    std::vector<DrawItem> opaque, blended;
    for (auto& figure : figures_) {
        auto& f = *figure;
        if (f.alpha <= 0.0) continue;
        Model* m = model(f.species);
        if (!m || !m->root) continue;
        pose(f, *m, now);
        const float height = form_label_height(f.species);
        if (eye) {
            const Vec3f centre{f.position.x, f.position.y + height * 0.5f, f.position.z};
            const auto own_centre = own ? std::optional<Vec3f>(Vec3f{own->x, own->y + 10.0f, own->z}) : std::nullopt;
            const double target = ghost_camera_alpha(*eye, centre, own_centre, std::max(8.0f, height * 0.45f));
            const double step = dt / kCameraFadeSeconds;
            f.camera = f.camera < target ? std::min(target, f.camera + step) : std::max(target, f.camera - step);
        }
        const float alpha = float(f.alpha * f.camera);
        if (alpha <= 0.004f) continue;
        const Mat4 world = ghost_matrix(f.position, f.yaw);
        const auto time = std::int64_t((now - f.born) * 1e9);
        Weapon* arms = f.weapon.empty() ? nullptr : weapon(f.weapon, *m);
        if (arms && arms->failed) arms = nullptr;
        // The weapon's parts hang under the actors for this traversal only
        // (the nodes are this form's, shared by its figures).
        if (arms)
            for (Node* actor : m->actors)
                for (const auto& select : arms->selects) actor->children.push_back(select.get());
        std::vector<DrawItem> out;
        TraversalContext context;
        context.cache = m->cache.get();
        context.time = time;
        context.camera = camera;
        context.cull_camera = camera;
        context.output = &out;
        context.actor_pose = [&f](const Node&, bool) -> const ActorPose* { return &f.pose; };
        m->root->runtime_matrix = world;
        engine::traverse(m->root, context, Mat4{}, base, time);
        // The shadow proxy, as the stage's beCopyModelCtrl2 "Kage" copies cast
        // the players' (into the light's map; a fading figure casts while it
        // is mostly there).
        if (shadow && shadow->active && m->shadow && f.alpha >= 0.5) {
            TraversalContext caster = context;
            caster.output = &shadow->casters;
            caster.modelview_base = shadow->light_view;
            caster.projection_base = shadow->light_projection;
            engine::traverse(m->shadow, caster, world, RenderState{}, time);
        }
        if (arms)
            for (Node* actor : m->actors)
                for (const auto& select : arms->selects) std::erase(actor->children, select.get());
        ++drawn_;
        const std::uint64_t instance = 0x9e3779b97f4a7c15ull ^ (f.key * 0x100000001b3ull) ^ (std::uint64_t(f.segment) << 40);
        for (auto& item : out) item.instance ^= instance;
        if (alpha >= 0.999f) {
            opaque.insert(opaque.end(), std::make_move_iterator(out.begin()), std::make_move_iterator(out.end()));
            continue;
        }
        // Semi-transparent: the nearest surface's depth first (no colour),
        // then the colour where that depth is, faded; the figure never shows
        // its own inside or its far side through the near one.
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
                item.state.blend_src = 4; // SRC_ALPHA
                item.state.blend_dst = 5; // ONE_MINUS_SRC_ALPHA
            }
            item.state.depth_write = false;
            blended.push_back(std::move(item));
        }
    }
    if (opaque.empty() && blended.empty()) return;
    const auto at = ghost_insert_points(items);
    items.insert(items.begin() + std::ptrdiff_t(at.blended), std::make_move_iterator(blended.begin()), std::make_move_iterator(blended.end()));
    items.insert(items.begin() + std::ptrdiff_t(at.opaque), std::make_move_iterator(opaque.begin()), std::make_move_iterator(opaque.end()));
}

std::vector<std::string> GhostFigures::describe() const {
    std::vector<std::string> lines;
    for (const auto& f : figures_) {
        char line[256];
        const auto it = models_.find(f->species);
        const bool ready = it != models_.end() && it->second && it->second->root;
        std::snprintf(line, sizeof line, "member=%llu form=%d%s segment=%u at=(%.1f, %.1f, %.1f) yaw=%.2f clip=%s speed=%.1f alpha=%.2f camera=%.2f%s%s",
                      static_cast<unsigned long long>(f->key), f->species, ready ? "" : " (loading)", f->segment, f->position.x, f->position.y,
                      f->position.z, f->yaw, f->motion.clip.empty() ? "-" : f->motion.clip.c_str(), f->speed, f->alpha, f->camera,
                      f->leaving ? " leaving" : "", f->weapon.empty() ? "" : (" weapon=" + f->weapon).c_str());
        lines.emplace_back(line);
    }
    return lines;
}

} // namespace dw4::game
