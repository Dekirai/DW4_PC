#include "dw4/engine/managers.hpp"
#include "dw4/engine/save_menu.hpp"
#include "dw4/engine/state_archive.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/sfd.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <stdexcept>

namespace dw4::engine {

// ---------------------------------------------------------------------------
// Timers (8031DE44 family, 30.8 fixed time scaling via >> 18)
// ---------------------------------------------------------------------------
std::uint64_t scaled_clock(std::int64_t world, float scale) {
    const auto coarse = static_cast<std::uint32_t>(static_cast<std::uint64_t>(world) >> 18);
    const float scaled = static_cast<float>(coarse) * scale;
    std::int64_t integer;
    if (scaled >= 0x1p63f) integer = std::numeric_limits<std::int64_t>::max();
    else if (scaled <= -0x1p63f) integer = std::numeric_limits<std::int64_t>::min();
    else integer = static_cast<std::int64_t>(scaled);
    return static_cast<std::uint64_t>(integer) << 18;
}

void timer_start(NodeTimer& timer, std::int64_t world, std::int64_t end) {
    timer.bias = static_cast<std::int64_t>(std::uint64_t{0} - scaled_clock(world, timer.scale));
    timer.end = end;
    timer.saved_world = world;
    timer.cached_frames = 1;
    timer.previous = 0;
}

std::int64_t timer_time(const NodeTimer& timer, std::int64_t world) {
    return static_cast<std::int64_t>(scaled_clock(world, timer.scale) + static_cast<std::uint64_t>(timer.bias));
}

float timer_frames(NodeTimer& timer, std::int64_t world) {
    if (timer.saved_world == world) return timer.cached_frames;
    const auto current = timer_time(timer, world);
    const auto elapsed = current - timer.previous;
    if (elapsed != 0) {
        timer.cached_frames = std::min(static_cast<float>(static_cast<double>(elapsed)) / 16666667.0f, 3.0f);
        timer.previous = current;
    } else timer.cached_frames = 0;
    timer.saved_world = world;
    return timer.cached_frames;
}

bool timer_expired(const NodeTimer& timer, std::int64_t world) { return timer.end < timer_time(timer, world); }

std::int64_t frames_to_time(float frames) {
    if (frames == 0 || !std::isfinite(frames)) return 0;
    const float scaled = 1000000000.0f * frames;
    return static_cast<std::int64_t>(scaled / 60.0f);
}

namespace {
template <class RamT>
std::vector<RamT*> make_rams(Engine& engine, Manager* manager, const ObjectView& info, std::vector<std::unique_ptr<RamT>>& out,
                             const std::string& category) {
    std::vector<RamT*> created;
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<RamT>();
        ram->manager = manager;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        if (!category.empty()) engine.messenger().add_row(category, ram->info_name, ram->index, ram.get());
        created.push_back(ram.get());
        out.push_back(std::move(ram));
    }
    return created;
}
} // namespace

// ---------------------------------------------------------------------------
// Scenes: every igSceneInfo root is attached below the global render root.
// ---------------------------------------------------------------------------
void SceneManager::add_info(Engine& engine, const ObjectView& info) {
    scenes_.push_back(info);
    if (const auto root = info.ref(5)) engine.attach_scene_root(engine.scenes().node(root));
}

void SceneManager::clear_stage(Engine&) { scenes_.clear(); }

ObjectView SceneManager::find_scene(std::string_view name) const {
    for (const auto& scene : scenes_) if (scene.str(2) == name) return scene;
    return {};
}

// ---------------------------------------------------------------------------
// Layer control (80301368): ORDER / SWITCH / REMOVE / GETNAMELIST / ...
// ---------------------------------------------------------------------------
void LayerManager::add_info(Engine& engine, const ObjectView& info) {
    groups_.clear();
    for (const auto& group : info.list(7)) groups_.emplace_back(group ? std::string(group.str(2)) : std::string("NONE"));
    make_rams(engine, this, info, rams_, "LAYERCTL");
}

void LayerManager::clear_stage(Engine&) {
    rams_.clear();
    groups_.clear();
    layer_switch_.reset();
    group_nodes_.clear();
    ordered_ = false;
}

void LayerManager::order(Engine& engine, Ram&) {
    Node* root = engine.global_root();
    if (!ordered_) {
        layer_switch_ = std::make_unique<Node>();
        layer_switch_->kind = NodeKind::switch_node;
        layer_switch_->name = "LayerSwt";
        // Host the switch under LAYERPARENT of the light-state root, as 80301368.
        Node* host = nullptr;
        for (auto* child : root->children)
            if (child->kind == NodeKind::light_state_set) host = child;
        if (!host) {
            engine.note("layer ORDER without an igLightStateSet root");
            host = root;
        }
        Node* parent = nullptr;
        for (auto* child : host->children) if (child->name == "LAYERPARENT") parent = child;
        (parent ? parent : host)->children.push_back(layer_switch_.get());
        ordered_ = true;
    }
    for (const auto& name : groups_) {
        auto group = std::make_unique<Node>();
        group->kind = NodeKind::group;
        group->name = name;
        layer_switch_->children.push_back(group.get());
        group_nodes_.push_back(std::move(group));
    }
    auto none = std::make_unique<Node>();
    none->kind = NodeKind::group;
    none->name = "NONE";
    Node* none_node = none.get();
    layer_switch_->children.push_back(none_node);
    group_nodes_.push_back(std::move(none));
    layer_switch_->mask.assign((layer_switch_->children.size() + 31) / 32, 0);
    // Move every non-ROOTATTR, non-light-state child into its named layer group.
    for (std::size_t i = 0; i < root->children.size();) {
        Node* child = root->children[i];
        if (child->name != "ROOTATTR" && child->kind != NodeKind::light_state_set) {
            Node* target = none_node;
            for (auto* group : layer_switch_->children) if (group->name == child->name) { target = group; break; }
            target->children.push_back(child);
            root->children.erase(root->children.begin() + std::ptrdiff_t(i));
        } else ++i;
    }
}

bool LayerManager::command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
    if (command == "ORDER") { order(engine, ram); return true; }
    if (!layer_switch_) return true;
    auto find = [&](std::string_view name) -> int {
        for (std::size_t i = 0; i < layer_switch_->children.size(); ++i)
            if (layer_switch_->children[i]->name == name) return int(i);
        return -1;
    };
    if (command == "SWITCH") {
        if (packet.strings.size() < 2) return true;
        const auto group = packet.string(0);
        const auto mode = packet.string(1);
        // SwtCtl (802D38DC): CLR / DISPOFF hide, SET / DISPON show, FLIP toggles.
        const int value = mode == "CLR" || mode == "DISPOFF" ? 0 : mode == "SET" || mode == "DISPON" ? 1 : mode == "FLIP" ? 2 : -1;
        if (group == "ALL") {
            if (value == 1) layer_switch_->set_all_children(true);
            else if (value == 0) layer_switch_->set_all_children(false);
            return true;
        }
        const int index = find(group);
        if (index < 0) return true;
        if (value == 1) layer_switch_->set_child_enabled(std::size_t(index), true);
        else if (value == 0) layer_switch_->set_child_enabled(std::size_t(index), false);
        else if (value == 2) layer_switch_->set_child_enabled(std::size_t(index), !layer_switch_->child_enabled(std::size_t(index)));
        return true;
    }
    if (command == "REMOVE") {
        if (packet.strings.empty()) return true;
        const int index = find(packet.string(0));
        if (index < 0) return true;
        // Remove the child and shift the remaining mask bits down.
        std::vector<bool> bits;
        for (std::size_t i = 0; i < layer_switch_->children.size(); ++i) bits.push_back(layer_switch_->child_enabled(i));
        layer_switch_->children.erase(layer_switch_->children.begin() + index);
        bits.erase(bits.begin() + index);
        layer_switch_->mask.assign((bits.size() + 31) / 32, 0);
        for (std::size_t i = 0; i < bits.size(); ++i) layer_switch_->set_child_enabled(i, bits[i]);
        return true;
    }
    if (command == "GETNAMELIST") {
        for (auto* child : layer_switch_->children) packet.strings.emplace_back(child->name);
        return true;
    }
    if (command == "GETLAYERNODE" || command == "INFO") {
        if (packet.strings.empty()) return true;
        const int index = find(packet.string(0));
        if (index >= 0) packet.bools.push_back(1);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Background color
// ---------------------------------------------------------------------------
void BkColorManager::add_info(Engine& engine, const ObjectView& info) {
    color_ = info.vec4(7).value_or(Vec4f{0, 0, 0, 1});
    make_rams(engine, this, info, rams_, "BKCOLOR");
}
void BkColorManager::clear_stage(Engine&) {
    rams_.clear();
    color_ = {0, 0, 0, 1};
}
bool BkColorManager::command(Engine&, Ram&, std::string_view command, Packet& packet) {
    if (command == "SETCOLOR" && !packet.floats.empty()) {
        if (packet.floats.size() >= 3) color_ = {packet.floats[0], packet.floats[1], packet.floats[2], 1};
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
void CameraManager::add_info(Engine& engine, const ObjectView& info) {
    auto created = make_rams(engine, this, info, rams_, "CAMERA");
    // Camera graph paths come from the igSceneInfo with the same name.
    std::vector<CameraPath> paths;
    if (auto* scenes = engine.get<SceneManager>()) {
        for (const auto& scene : scenes->scenes()) {
            if (scene.str(2) != info.str(2)) continue;
            for (const auto& path : scene.list(7)) {
                if (!path) continue;
                CameraPath p;
                for (const auto& node : path.list(2)) {
                    if (!node) continue;
                    Node* n = engine.scenes().node(node);
                    p.nodes.push_back(n);
                    if (n->kind == NodeKind::camera) p.camera = n;
                }
                if (!p.nodes.empty()) p.name = p.nodes.back()->name;
                if (p.camera) paths.push_back(std::move(p));
            }
        }
    }
    // A demo camera info (e.g. a dungeon's "entry" in ACMR_pack.igb) carries
    // its graph path and length in beCameraDemoData.
    std::optional<CameraPath> demo;
    std::int64_t demo_duration = 0;
    if (const auto data = info.ref(7); data && data.type() == "beCameraDemoData") {
        CameraPath p;
        if (const auto path = data.ref(2))
            for (const auto& node : path.list(2)) {
                if (!node) continue;
                Node* n = engine.scenes().node(node);
                p.nodes.push_back(n);
                if (n->kind == NodeKind::camera) p.camera = n;
            }
        if (!p.nodes.empty()) {
            p.name = p.nodes.back()->name;
            demo = std::move(p);
            demo_duration = data.i64(3);
            if (demo_duration <= 0) demo_duration = std::int64_t(data.f32(4) * 1e9f);
        }
    }
    std::optional<BoxData> box;
    if (const auto data = info.ref(7); data && data.type() == "beCameraBoxData") {
        BoxData b;
        for (const auto& node : data.list(4)) if (node && !b.pivot) b.pivot = engine.scenes().node(node);
        for (const auto& node : data.list(5)) b.eyes.push_back(node ? engine.scenes().node(node) : nullptr);
        for (const auto& node : data.list(6)) b.targets.push_back(node ? engine.scenes().node(node) : nullptr);
        for (const auto& geometry : data.list(3)) {
            const auto bound = geometry ? geometry.ref(3) : ObjectView{};
            b.boxes.push_back({bound ? bound.vec3(2).value_or(Vec3f{}) : Vec3f{}, bound ? bound.vec3(3).value_or(Vec3f{}) : Vec3f{}});
        }
        if (b.pivot && !b.eyes.empty() && b.eyes[0] && !b.targets.empty() && b.targets[0]) box = std::move(b);
    }
    for (auto* ram : created) {
        ram->paths = paths;
        ram->demo_data = demo;
        ram->demo_data_duration = demo_duration;
        ram->box = box;
    }
}

void CameraManager::clear_stage(Engine&) {
    rams_.clear();
    viewer_.reset();
    frame_.reset();
    centre_ = {-1.0f, -1.0f, -1.0f};
    centre_moved_ = false;
    blend_ = 0.0f;
    box_ = previous_box_ = 0;
}

bool CameraManager::named_demo_running(const Engine& engine) const {
    for (const auto& ram : rams_)
        if (ram->named_active && engine.time() - ram->named_start <= ram->named_duration) return true;
    return false;
}

namespace {
void set_translation(Mat4& m, const Vec3f& v) {
    m(3, 0) = v.x;
    m(3, 1) = v.y;
    m(3, 2) = v.z;
}
Vec3f translation_of(const Mat4& m) { return {m(3, 0), m(3, 1), m(3, 2)}; }

// 8012B8A8: the rotation part of a matrix as a quaternion (x, y, z, w).
Vec4f matrix_quaternion(const Mat4& m) {
    const float trace = m(0, 0) + m(1, 1) + m(2, 2);
    Vec4f q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m(1, 2) - m(2, 1)) / s, (m(2, 0) - m(0, 2)) / s, (m(0, 1) - m(1, 0)) / s, 0.25f * s};
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const float s = std::sqrt(1.0f + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0f;
        q = {0.25f * s, (m(0, 1) + m(1, 0)) / s, (m(2, 0) + m(0, 2)) / s, (m(1, 2) - m(2, 1)) / s};
    } else if (m(1, 1) > m(2, 2)) {
        const float s = std::sqrt(1.0f + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0f;
        q = {(m(0, 1) + m(1, 0)) / s, 0.25f * s, (m(1, 2) + m(2, 1)) / s, (m(2, 0) - m(0, 2)) / s};
    } else {
        const float s = std::sqrt(1.0f + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0f;
        q = {(m(2, 0) + m(0, 2)) / s, (m(1, 2) + m(2, 1)) / s, 0.25f * s, (m(0, 1) - m(1, 0)) / s};
    }
    return q;
}

// 8012C15C: spherical interpolation along the shorter arc.
Vec4f quaternion_slerp(Vec4f a, Vec4f b, float t) {
    float cosine = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cosine < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }
    float wa = 1.0f - t, wb = t;
    if (cosine < 0.9999f) {
        const float angle = std::acos(cosine), sine = std::sin(angle);
        wa = std::sin((1.0f - t) * angle) / sine;
        wb = std::sin(t * angle) / sine;
    }
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// 801299D8: the rotation of a quaternion, keeping the translation.
void set_rotation(Mat4& m, const Vec4f& q) {
    const Vec3f keep = translation_of(m);
    m = quaternion_matrix(q.x, q.y, q.z, q.w);
    set_translation(m, keep);
}

// SHK direction tables (804DD8D8 / 804DD8FC), indexed by pattern digit.
constexpr float kShakeX[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
constexpr float kShakeY[9] = {0, 1, 1, 0, -1, -1, -1, 0, 1};

// 802F414C copies the igCamera's _horizontalFieldOfView (+2C), _nearPlane
// (+30; 0 means far / 1000, 80425320) and _farPlane (+34) into the viewer;
// 8040B834 passes them to setPerspective 801DB6E4 with the vertical field of
// view unset (-1.0, 80462E28), so the horizontal one is kept for the 640x448
// viewer window (8028BB10/8028BB18). The video copy stretches the 448 lines
// to the 480-line picture, the same NDC as a 640x480 viewport.
Mat4 camera_projection(float fov_x_degrees, float near_plane, float far_plane) {
    if (near_plane == 0.0f) near_plane = far_plane / 1000.0f;
    return perspective_horizontal(fov_x_degrees * std::numbers::pi_v<float> / 180.0f, 640.0f / 448.0f, near_plane,
                                  far_plane);
}
} // namespace

// 802F4030 + 8040F870: the viewer takes the path, restarts its clock (+58..+70
// cleared, the first step adds nothing) and becomes the current viewer.
void CameraManager::set_viewer(const CameraRam& ram, const CameraPath& path, std::int64_t duration) {
    viewer_ = Viewer{&ram, &path, 0, duration, 0, false};
}

// 802F2BC0: the box camera. The pivot stands at the players' centre; the
// camera box containing the centre selects the rotation and distance
// transforms, blended in over 120 steps (about one per frame while the
// players move, 1.5 times that while they stand); SHAKE offsets the live
// transforms. On the original the step is the RAM's CAMHOKAN timer, read on
// every blend frame after the blend (0x802F3260-0x802F32C4): 8031E138 tells
// whether the timer exists (0x8031E210-0x8031E220) and 8031DE44 creates it
// only when it does not, so on a blend's first frame (stamped with that
// frame: step 1); 8031E4C4 returns the frames since its last read, the time
// delta / 16666667 at most 3 (80452BF8/80452BFC), kept for the rest of the
// frame (0x8031E5B8-0x8031E638). A running blend so steps 0.991 or 1.007 per
// field (the NTSC field on the >> 18 clock); the end of the blend removes the
// timer (8031E8BC at 0x802F316C). The port keeps the same timer per RAM.
// "Stand" means the centre equals last frame's bit for bit (fcmpu,
// 0x802F2D24..0x802F2D3C). The box camera's path becomes the current viewer
// only when the centre enters another box (0x802F2F70..0x802F30E8); without
// players it does nothing (0x802F2C68), so a demo viewer stays current.
void CameraManager::update_box(Engine& engine, CameraRam& ram) {
    auto& box = *ram.box;
    Node* eye = box.eyes[0];
    Node* target = box.targets[0];
    // First run after SETBOXCAMERA (RAM +18 == 0): the players' centre is
    // reset to (-1, -1, -1) (80425238) and the box (+4C) to 0, so the first
    // box found is copied directly instead of blended.
    if (ram.state == 0) {
        centre_ = {-1.0f, -1.0f, -1.0f};
        box_ = 0;
        ram.state = 1;
    }
    // PLAYER GETMATRIX: 2 players halve, 3 divide, 4 quarter, else the sum.
    std::vector<Vec3f> players;
    if (auto* models = engine.get<ModelCtrlManager>())
        for (const auto& model : models->rams())
            if (model->is_player && model->generated && model->node) players.push_back(translation_of(model->node->matrix));
    if (players.empty()) return;
    Vec3f centre{};
    for (const auto& p : players) centre = centre + p;
    if (players.size() == 2) centre = centre * 0.5f;
    else if (players.size() == 3) centre = centre * (1.0f / 3.0f);
    else if (players.size() == 4) centre = centre * 0.25f;
    centre_moved_ = centre.x != centre_.x || centre.y != centre_.y || centre.z != centre_.z;
    if (centre_moved_) centre_ = centre;
    // The last frame's shake offset is taken back (not on its first frame).
    if (ram.shake) {
        if (ram.shake->fresh) ram.shake->fresh = false;
        else {
            set_translation(eye->matrix, ram.shake->eye);
            set_translation(target->matrix, ram.shake->target);
        }
    }
    if (centre_moved_) {
        set_translation(box.pivot->matrix, centre_);
        int found = 0;
        for (std::size_t k = 1; k < box.boxes.size(); ++k) {
            const auto& [lo, hi] = box.boxes[k];
            if (centre_.x >= lo.x && centre_.y >= lo.y && centre_.z >= lo.z && centre_.x <= hi.x && centre_.y <= hi.y &&
                centre_.z <= hi.z) {
                found = int(k);
                break;
            }
        }
        if (found != 0 && found != box_) {
            previous_box_ = box_;
            box_ = found;
            saved_eye_ = eye->matrix;
            saved_target_ = target->matrix;
            blend_ = 0.0f;
            // The scene camera's path with no length: evaluated as it stands.
            if (!ram.paths.empty()) set_viewer(ram, ram.paths.front(), 0);
        }
    }
    const std::size_t k = std::size_t(box_);
    if (box_ != previous_box_ && k < box.eyes.size() && k < box.targets.size() && box.eyes[k] && box.targets[k]) {
        if (previous_box_ == 0 || blend_ >= 120.0f) {
            previous_box_ = box_;
            eye->matrix = box.eyes[k]->matrix;
            target->matrix = box.targets[k]->matrix;
            ram.hokan.reset(); // 0x802F316C: 8031E8BC removes CAMHOKAN
        } else {
            const float t = blend_ / 120.0f;
            auto blend = [&](Node* live, const Mat4& saved, const Mat4& goal) {
                const Vec3f a = translation_of(saved), b = translation_of(goal);
                set_translation(live->matrix, a + (b - a) * t);
                set_rotation(live->matrix, quaternion_slerp(matrix_quaternion(saved), matrix_quaternion(goal), t));
            };
            blend(eye, saved_eye_, box.eyes[k]->matrix);
            blend(target, saved_target_, box.targets[k]->matrix);
            if (!ram.hokan) timer_start(ram.hokan.emplace(), engine.time());
            const float frames = timer_frames(*ram.hokan, engine.time());
            blend_ = centre_moved_ ? blend_ + frames : std::fma(1.5f, frames, blend_); // fadds / fmadds
        }
    }
    // 802F3370: the shake offsets both live transforms by the pattern's
    // direction; SHAKEONE steps the pattern, SHAKEALL ends it.
    if (ram.shake) {
        auto& shake = *ram.shake;
        const char c = shake.index < shake.pattern.size() ? shake.pattern[shake.index] : '0';
        const int digit = std::clamp(c - '0', 0, 8);
        shake.eye = translation_of(eye->matrix);
        shake.target = translation_of(target->matrix);
        for (Node* node : {eye, target}) {
            Vec3f p = translation_of(node->matrix);
            p.x += kShakeX[digit] * shake.amplitude;
            p.y += kShakeY[digit] * shake.amplitude;
            set_translation(node->matrix, p);
        }
        const std::int64_t now = engine.time();
        if (now >= shake.step_end) {
            ++shake.index;
            if (shake.index >= shake.pattern.size()) shake.index = 0;
            shake.step_end = now + std::int64_t(1.0e9f * shake.step / 60.0f);
        }
        if (now >= shake.all_end) {
            set_translation(eye->matrix, shake.eye);
            set_translation(target->matrix, shake.target);
            ram.shake.reset();
        }
    }
}

bool CameraManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<CameraRam&>(base);
    if (command == "SETDEMOCAMERA") {
        // 802F37AC case 6: the demo camera info of that name starts over (its
        // next update makes it the current viewer), every other demo camera
        // stops, and the box camera stops while such a demo camera exists.
        const auto name = packet.string(0);
        bool named_info = false;
        for (const auto& other : rams_) named_info = named_info || (other->demo_data && other->info_name == name);
        ram.named_active = false;
        if (ram.box) ram.active = !named_info;
        else if (ram.demo_data && ram.info_name == name) {
            ram.active = true;
            ram.state = 0;
        } else ram.active = false;
        if (named_info) return true;
        // Without such an info the original keeps the box camera and
        // ISDEMOCAMERA answers 0 at once. Port fallback (older than the demo
        // camera infos): a path of this camera whose node has the name plays
        // for its longest transform sequence (at least one frame so
        // ISDEMOCAMERA reports it).
        ram.named_path = -1;
        for (std::size_t i = 0; i < ram.paths.size() && ram.named_path < 0; ++i)
            for (auto* node : ram.paths[i].nodes)
                if (node && node->name == name) { ram.named_path = int(i); break; }
        ram.named_duration = kFrameNs;
        if (ram.named_path >= 0) {
            const auto& path = ram.paths[std::size_t(ram.named_path)];
            for (auto* node : path.nodes)
                if (node && node->sequence && node->sequence->valid && !node->sequence->times.empty())
                    ram.named_duration = std::max(ram.named_duration, node->sequence->duration > 0 ? node->sequence->duration
                                                                                                   : node->sequence->times.back());
            set_viewer(ram, path, ram.named_duration);
        } else engine.note("CAMERA demo path " + std::string(name));
        ram.named_active = true;
        ram.named_start = engine.time();
        return true;
    }
    if (command == "ISDEMOCAMERA") {
        // 802F37AC case 8, one answer for the broadcast: an active demo camera
        // info runs until its update has set the viewer up (+18 == 0) and the
        // viewer's clock has reached the end (+70).
        if (&ram != rams_.front().get()) return true;
        bool running = named_demo_running(engine);
        for (const auto& other : rams_)
            if (other->active && !other->box && other->demo_data)
                running = running || other->state == 0 || (viewer_ && !viewer_->finished);
        packet.bools.push_back(running ? 1 : 0);
        return true;
    }
    if (command == "SETBOXCAMERA") {
        // 802F37AC case 5: the box camera runs again from a fresh state (+18
        // and its work +1C..+24 cleared); every other camera stops. The
        // viewer stays as it is until the box camera sets its own.
        ram.active = ram.box.has_value();
        if (ram.box) ram.state = 0;
        ram.named_active = false;
        return true;
    }
    if (command == "GETPOSITION") {
        // 802F3A60: the players' centre of the box camera.
        packet.vectors.push_back(centre_);
        return true;
    }
    if (command == "SHAKE") {
        // 802F3B2C: only the box camera shakes.
        if (!ram.box) return true;
        Shake shake;
        shake.pattern = std::string(packet.string(0));
        shake.amplitude = packet.real(0);
        shake.step = packet.real(1);
        shake.total = packet.real(2);
        shake.all_end = engine.time() + std::int64_t(1.0e9f * shake.total / 60.0f);
        shake.step_end = engine.time(); // SHAKEONE cleared: the first update steps
        ram.shake = std::move(shake);
        return true;
    }
    if (command == "SHOWPOLYGON" || command == "HIDEPOLYGON") return true; // debug box display
    return false;
}

void CameraManager::update(Engine& engine) {
    // 802F3710 for each active camera: the box update for a box camera
    // (one box camera drives the shared box state), the demo update else.
    bool box_done = false;
    for (auto& ram : rams_) {
        if (!ram->active) continue;
        if (ram->box) {
            if (!box_done && !named_demo_running(engine)) update_box(engine, *ram);
            box_done = true;
        } else if (ram->demo_data && ram->state == 0) {
            // 802F3568 state 0: the viewer plays the demo path from 0 for the
            // beCameraDemoData length and becomes the current viewer.
            set_viewer(*ram, *ram->demo_data, ram->demo_data_duration);
            ram->state = 1;
        }
    }
}

// 802F4268 (beCameraMode vfunc +0x78) ticks before the managers: a viewer
// set by this frame's demo or camera update is first drawn next frame, at
// its time 0, and the demo waiting on ISDEMOCAMERA sees the end the frame
// the viewer reaches it. Dolphin shows it at the boss intro: the frame the
// demo sends SETDEMOCAMERA still draws the box camera, and the demo camera,
// its end and the cut to the box camera come one frame after the world
// events the same demo posts.
void CameraManager::begin_frame(Engine& engine) {
    // The viewer's clock advances by the world step (at most 50 ms, nothing
    // on its first update) and stops at the duration (+70 finished); the
    // path is still evaluated there, so the last pose stays.
    if (viewer_ && !viewer_->finished) {
        const std::int64_t now = engine.time();
        const std::int64_t step = viewer_->last != 0 ? std::min<std::int64_t>(now - viewer_->last, 50'000'000) : 0;
        viewer_->time += step;
        viewer_->last = now;
        if (viewer_->time >= viewer_->duration) {
            viewer_->finished = true;
            viewer_->time = viewer_->duration;
        }
    }
    FrameCamera camera;
    compute_camera(engine, camera);
    frame_ = camera;
}

void CameraManager::frame_camera(Engine& engine, FrameCamera& camera) {
    if (frame_) camera = *frame_;
    else compute_camera(engine, camera);
}

void CameraManager::compute_camera(Engine& engine, FrameCamera& camera) {
    camera.projection = camera_projection(54.4322f, 10.0f, 2000.0f);
    camera.view = look_at({0, 50, 300}, {0, 0, 0}, {0, 1, 0});
    // The current viewer's path at its clock. Before the box or a demo camera
    // has set one, the stage's scene camera (8040E7DC): the active box
    // camera's path, else the first camera with a path.
    const CameraRam* chosen = viewer_ ? viewer_->ram : nullptr;
    for (const auto& ram : rams_)
        if (!chosen && !ram->paths.empty() && ram->box && ram->active) chosen = ram.get();
    for (const auto& ram : rams_)
        if (!chosen && !ram->paths.empty()) chosen = ram.get();
    if (chosen) {
        const CameraRam* ram = chosen;
        const auto& path = viewer_ ? *viewer_->path : ram->paths.front();
        const std::int64_t at = viewer_ ? viewer_->time : 0;
        // World matrices along the path; the last transform places the camera
        // relative to its parent group, whose origin is the look-at pivot.
        Mat4 world, pivot;
        const Node* last_transform = nullptr;
        for (auto* node : path.nodes)
            if (node->kind == NodeKind::transform || node->kind == NodeKind::model_node) last_transform = node;
        for (auto* node : path.nodes) {
            if (node->kind != NodeKind::transform && node->kind != NodeKind::model_node) continue;
            if (node == last_transform) pivot = world;
            world = (node->sequence && node->sequence->valid ? node->sequence->evaluate(at) : node->matrix) * world;
        }
        if (path.camera) {
            // igCamera fields (804B0F14): _horizontalFieldOfView (default 45,
            // 80566CD4), _nearPlane, _farPlane.
            const float fov = path.camera->object.f32(8, 45.0f);
            const float near_plane = path.camera->object.f32(9, 10.0f);
            const float far_plane = path.camera->object.f32(10, 2000.0f);
            camera.projection = camera_projection(fov, near_plane, far_plane);
        }
        // Without a viewer and box data: follow the first player with the
        // authored angle and distance.
        if (!viewer_ && !ram->box) {
            if (auto* models = engine.get<ModelCtrlManager>()) {
                if (const auto* player = models->player(0); player && player->node) {
                    const auto& pm = player->node->matrix;
                    const Vec3f offset{world(3, 0) - pivot(3, 0), world(3, 1) - pivot(3, 1), world(3, 2) - pivot(3, 2)};
                    const Vec3f pivot_local{pivot(3, 0), pivot(3, 1), pivot(3, 2)};
                    world(3, 0) = pm(3, 0) + pivot_local.x + offset.x;
                    world(3, 1) = pm(3, 1) + pivot_local.y + offset.y;
                    world(3, 2) = pm(3, 2) + pivot_local.z + offset.z;
                }
            }
        }
        Mat4 view;
        if (invert(world, view)) camera.view = view;
    }
}

// ---------------------------------------------------------------------------
// Stage control (beNDMWStageCtl): NEXTSTAGE / PADINPUT
// ---------------------------------------------------------------------------
void StageCtlManager::add_info(Engine& engine, const ObjectView& info) { make_rams(engine, this, info, rams_, "STAGECTL"); }
void StageCtlManager::clear_stage(Engine&) {
    rams_.clear();
    reset_state_ = 0;
}

// 80347BB4: START pressed on pad 1 while B and X are held arms the soft
// reset; held for a second it stops the sounds and the status window, hides
// the layers, freezes the models and generators and, two frames later,
// returns to the title.
void StageCtlManager::update(Engine& engine) {
    if (rams_.empty()) return;
    const auto& pad = engine.pads()[0].buttons;
    const bool combination = (pad.held & pad_button::b) && (pad.held & pad_button::x);
    auto& messenger = engine.messenger();
    auto send = [&](const char* category, const char* command, std::initializer_list<const char*> strings = {}) {
        Packet packet;
        for (const char* s : strings) packet.strings.emplace_back(std::string(s));
        messenger.send(engine, category, std::nullopt, -1, command, packet);
        return packet;
    };
    auto subwnd_running = [&] {
        Packet run = send("SUBWND", "ISRUN");
        return run.boolean(0);
    };
    switch (reset_state_) {
    case 0:
        if (engine.soft_reset_allowed() && (pad.trigger & pad_button::start) && combination) {
            reset_time_ = engine.time() + 1'000'000'000;
            reset_state_ = 1;
        }
        break;
    case 1:
        if (!engine.soft_reset_allowed() || !(pad.held & pad_button::start) || !combination) {
            reset_state_ = 0;
            break;
        }
        if (engine.time() < reset_time_) break;
        send("SOUND", "BGMSTOP");
        send("SOUND", "ENVSTOP");
        send("SOUND", "SESTOP");
        if (subwnd_running()) {
            send("SUBWND", "REMOVE");
            send("LAYERCTL", "SWITCH", {"ALL", "DISPOFF"});
        }
        reset_state_ = 2;
        break;
    case 2:
        if (subwnd_running()) break;
        send("LAYERCTL", "SWITCH", {"ALL", "DISPOFF"});
        send("MODELCTRL", "ONFREEZE");
        send("GENERATER", "ONFREEZE");
        reset_state_ = 3;
        break;
    case 3:
    case 4:
        ++reset_state_;
        break;
    case 5:
        engine.request_stage("stagis10");
        reset_state_ = 6;
        break;
    default:
        break;
    }
}
bool StageCtlManager::command(Engine& engine, Ram&, std::string_view command, Packet& packet) {
    if (command == "NEXTSTAGE") {
        // 80347F94: the sound effects and the ambience stop at once.
        if (!packet.strings.empty()) {
            Packet stop;
            engine.messenger().send(engine, "SOUND", std::nullopt, -1, "SESTOP", stop);
            engine.messenger().send(engine, "SOUND", std::nullopt, -1, "ENVSTOP", stop);
            engine.request_stage(std::string(packet.string(0)));
        }
        return true;
    }
    if (command == "ISRESETSTART") {
        // 803481B4: a soft reset is running.
        packet.bools.push_back(reset_state_ != 0 ? 1 : 0);
        return true;
    }
    if (command == "PADINPUT") {
        const auto mode = packet.string(0);
        if (mode == "ENABLE") engine.set_pad_input_enabled(true);
        else if (mode == "DISABLE") engine.set_pad_input_enabled(false);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SWITCHCTRL (beSwitchCtrl): ALLON / ALLOF on the controlled switch.
// ---------------------------------------------------------------------------
void SwitchCtrlManager::add_info(Engine& engine, const ObjectView& info) { make_rams(engine, this, info, rams_, "SWITCHCTRL"); }
void SwitchCtrlManager::clear_stage(Engine&) { rams_.clear(); }
bool SwitchCtrlManager::command(Engine& engine, Ram& ram, std::string_view command, Packet&) {
    if (command != "ALLON" && command != "ALLOF") return false;
    // The controlled root is the info's first object reference (slot 7).
    if (const auto target = ram.info.ref(7)) {
        Node* node = engine.scenes().node(target);
        if (node && node->kind == NodeKind::switch_node) node->set_all_children(command == "ALLON");
    }
    return true;
}

// NDMW logo (803B18BC): title.cpp, next to the other card worker users.
void LogoManager::add_info(Engine& engine, const ObjectView& info) { make_rams(engine, this, info, rams_, "LOGO"); }
void LogoManager::clear_stage(Engine&) { rams_.clear(); }

// ---------------------------------------------------------------------------
// Sound / SE / movie requests
// ---------------------------------------------------------------------------
bool SoundManager::accepts(const ObjectView& info) const {
    const auto& t = info.type();
    return t == "beSoundInfo" || t == "beSeInfo" || t == "beCriFxInfo" || t == "beMovieInfo" || t == "beStaticInfo";
}
void SoundManager::add_info(Engine& engine, const ObjectView& info) {
    const auto& t = info.type();
    std::string category = t == "beSoundInfo" ? "SOUND" : t == "beSeInfo" ? "SE" : t == "beMovieInfo" ? "MOVIE" : "";
    make_rams(engine, this, info, rams_, category);
    if (t == "beSeInfo") {
        for (const auto& set : info.list(5)) {
            if (!set) continue;
            auto& cues = se_sets_[std::string(set.str(2))];
            for (const auto& cue : set.list(4))
                if (cue) cues.push_back({std::string(cue.str(2)), std::string(cue.str(3)), cue.i16(4)});
        }
        return;
    }
    if (t == "beStaticInfo") {
        // The stage's sfp banks (beCriSfpData), next to the common allse.sfp.
        for (const auto& data : info.list(5))
            if (data && data.type() == "beCriSfpData") add_bank(engine, data, false);
        return;
    }
    if (t != "beSoundInfo") return;
    // The stage's sounds ([5] beSoundData) and the files they index ([7]).
    for (const auto& data : info.list(5))
        if (data)
            sounds_[std::string(data.str(2))] = {data.i16(4), data.i16(5), data.i16(8), std::uint32_t(data.i32(6))};
    files_.clear();
    for (const auto& file : info.list(7)) files_.emplace_back(file ? std::string(file.str(2)) : std::string());
}
void SoundManager::clear_stage(Engine&) {
    // The streams keep playing into the next stage (its scripts stop or
    // replace them); only the stage's sound list goes.
    rams_.clear();
    sounds_.clear();
    files_.clear();
    movie_.reset();
    movie_stream_.decoder.reset();
    // The stage's banks go; one-shot effects finish, loops end.
    std::erase_if(banks_, [](const auto& bank) { return !bank->common; });
    std::erase_if(voices_, [](const SeVoice& voice) { return voice.loop; });
    tracks_.clear();
    se_sets_.clear();
    cues_.clear();
    owner_loops_.clear();
}

void SoundManager::action_started(Engine& engine, const std::string& model, int index, std::string_view action) {
    const std::string owner = model + "#" + std::to_string(index);
    if (const auto loop = owner_loops_.find(owner); loop != owner_loops_.end()) {
        stop_effects(loop->second, owner);
        owner_loops_.erase(loop);
    }
    std::erase_if(cues_, [&](const PendingCue& cue) { return cue.owner == owner; });
    const auto set = se_sets_.find(model);
    if (set == se_sets_.end()) return;
    for (const auto& cue : set->second) {
        if (cue.action != action) continue;
        if (cue.delay <= 0) play_cue(engine, cue.sound, owner);
        else cues_.push_back({cue.sound, owner, cue.delay});
    }
}

void SoundManager::play_cue(Engine& engine, const std::string& sound, const std::string& owner) {
    const auto found = sounds_.find(sound);
    if (found == sounds_.end() || found->second.request == 0xffffffffu) {
        Packet packet;
        packet.strings.emplace_back(sound);
        play(engine, packet);
        return;
    }
    if (found->second.type == 4) owner_loops_[owner] = sound;
    play_request(engine, sound, found->second, owner);
}

void SoundManager::stop_effects(const std::string& name, const std::string& owner) {
    auto matches = [&](const std::string& n, const std::string& o) {
        return (name.empty() || n == name) && (owner.empty() || o == owner);
    };
    std::erase_if(tracks_, [&](const SeTrack& track) { return matches(track.name, track.owner); });
    std::erase_if(voices_, [&](const SeVoice& voice) { return matches(voice.name, voice.owner); });
}

void SoundManager::add_bank(Engine& engine, const ObjectView& data, bool common) {
    std::string name(data.str(2));
    for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& bank : banks_)
        if (bank->name == name) return;
    const auto bytes = data.memory(4);
    auto bank = std::make_shared<SeBank>();
    bank->name = name;
    bank->common = common;
    try {
        bank->bank = std::make_unique<sfp::Bank>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    } catch (const std::exception& error) {
        engine.note("audio sfp bank " + name + ": " + error.what());
        return;
    }
    for (std::size_t i = 0; i < bank->bank->sample_count(); ++i) {
        std::uint32_t loop = 0;
        bank->pcm.push_back(bank->bank->decode(i, &loop));
        bank->loop_start.push_back(loop);
    }
    banks_.push_back(std::move(bank));
}

namespace {
// 0.1 dB volume (ADXT_SetOutVol) as a gain factor.
float volume_gain(int volume) { return std::pow(10.0f, float(std::min(volume, 0)) / 200.0f); }

// The AX mixer (MIXInitChannel 80259334, stereo output): the left side is
// attenuated by the pan table at the pan, the right side by the table at
// 127 - pan (0.1 dB, 804C79AC), so a centred voice plays at -3 dB on each
// side; -90.4 dB and below is silence, +6 dB the ceiling.
constexpr std::int16_t kMixPan[128] = {
    0,    0,    -1,   -1,   -1,   -2,   -2,   -2,   -3,   -3,   -4,   -4,   -4,   -5,   -5,   -5,
    -6,   -6,   -7,   -7,   -7,   -8,   -8,   -9,   -9,   -10,  -10,  -10,  -11,  -11,  -12,  -12,
    -13,  -13,  -14,  -14,  -14,  -15,  -15,  -16,  -16,  -17,  -17,  -18,  -18,  -19,  -20,  -20,
    -21,  -21,  -22,  -22,  -23,  -23,  -24,  -25,  -25,  -26,  -26,  -27,  -28,  -28,  -29,  -30,
    -30,  -31,  -32,  -33,  -33,  -34,  -35,  -36,  -36,  -37,  -38,  -39,  -40,  -40,  -41,  -42,
    -43,  -44,  -45,  -46,  -47,  -48,  -49,  -50,  -51,  -52,  -54,  -55,  -56,  -57,  -59,  -60,
    -61,  -63,  -64,  -66,  -67,  -69,  -71,  -72,  -74,  -76,  -78,  -80,  -83,  -85,  -87,  -90,
    -93,  -96,  -99,  -102, -106, -110, -115, -120, -126, -133, -140, -150, -163, -180, -210, -904};
float mix_level(int db10) { return db10 <= -904 ? 0.0f : std::pow(10.0f, float(std::min(db10, 60)) / 200.0f); }
std::pair<float, float> mix_pan(int pan) {
    pan = std::clamp(pan, 0, 127);
    return {mix_level(kMixPan[pan]), mix_level(kMixPan[127 - pan])};
}
// 8045D130: an SFP split's pan as the AX pan (803BA3F0).
constexpr std::uint8_t kSfpPan[128] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   2,   2,   2,   2,   3,
    3,   4,   4,   5,   5,   6,   7,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  19,  20,  21,  23,
    24,  26,  28,  29,  31,  33,  35,  37,  39,  41,  43,  46,  48,  50,  52,  54,  56,  58,  60,  62,  64,  66,
    68,  70,  72,  74,  76,  78,  80,  82,  84,  87,  89,  91,  93,  94,  96,  98,  100, 101, 103, 104, 106, 107,
    108, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 119, 120, 121, 121, 122, 123, 123, 124, 124, 124, 125,
    125, 125, 126, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127};
} // namespace

// PLAY <name> (80314FC8): a stream sound starts on its channel. A channel
// still playing fades out first (10 frames) and the request comes back 11
// frames later; the same BGM again is kept. PLAYatPAUSE (80314FBC) starts it
// paused (802F5828 keeps the pause) until PAUSEOFF.
bool SoundManager::play(Engine& engine, Packet& packet, bool paused) {
    const std::string name(packet.string(0));
    const auto found = sounds_.find(name);
    if (found == sounds_.end()) {
        engine.note("audio PLAY " + name + " (not in the stage sound list)");
        return true;
    }
    const auto& data = found->second;
    if (data.type != 1 && data.type != 2) {
        if (data.request != 0xffffffffu) return play_request(engine, name, data);
        // 802F5A80: without a request id the sound effect is an ADX file on
        // an SE stream channel, kept paused by PLAYatPAUSE (+60).
        Stream* target = &streams_[2];
        for (std::size_t channel = 2; channel < streams_.size(); ++channel)
            if (!streams_[channel].decoder || streams_[channel].name == name) {
                target = &streams_[channel];
                break;
            }
        if (open_stream(engine, *target, name, data)) target->paused = paused;
        return true;
    }
    auto& stream = streams_[std::size_t(data.type - 1)];
    if (data.type == 1 && current_bgm_ == name && stream.decoder && stream.fade_left == 0) return true;
    if (stream.decoder) {
        if (stream.fade_left == 0) stream.fade_left = 10;
        Packet again;
        again.strings.emplace_back(name);
        engine.messenger().post(engine.time() + kFrameNs * 11, "SOUND", std::nullopt, -1, paused ? "PLAYatPAUSE" : "PLAY",
                                std::move(again));
        return true;
    }
    if (!open_stream(engine, stream, name, data)) return true;
    stream.paused = paused;
    if (data.type == 1) current_bgm_ = name;
    return true;
}

bool SoundManager::open_stream(Engine& engine, Stream& stream, const std::string& name, const SoundData& data) {
    // "[archive.entry]" names an ADX file of an AFS archive (802A9828).
    const std::string file = data.file >= 0 && data.file < int(files_.size()) ? files_[std::size_t(data.file)] : std::string();
    unsigned archive = 0, entry = 0;
    char close = 0;
    if (std::sscanf(file.c_str(), "[%u.%u%c", &archive, &entry, &close) != 3 || close != ']') {
        engine.note("audio stream file " + file + " for " + name);
        return false;
    }
    try {
        const auto& archives = engine.resources().archives();
        if (archive >= archives.size()) throw std::runtime_error("no archive " + std::to_string(archive));
        stream.decoder = std::make_unique<adx::Decoder>(archives[archive].read(entry, 64u * 1024u * 1024u));
        stream.file = file;
        stream.frames_read = 0;
    } catch (const std::exception& error) {
        engine.note("audio stream " + file + ": " + error.what());
        return false;
    }
    stream.name = name;
    stream.gain = volume_gain(data.volume);
    stream.fade_left = 0;
    stream.paused = false;
    stream.ready_in = 10; // the ADX stream buffers before it reports ready
    stream.ramp_frames = stream.ramp_left = 0;
    stream.ramp_from = stream.ramp_to = 1.0f;
    stream.phase = 1.0;
    stream.previous = stream.next = {};
    return true;
}

// A sound-effect request (8031CCFC SEQ_SET/SEQ_PLAY): the bank holding the
// request's group (803C5188) runs its sequence on a track.
bool SoundManager::play_request(Engine& engine, const std::string& name, const SoundData& data, const std::string& owner) {
    if (!common_loaded_) {
        // Loaded once at boot by the original (802F902C).
        common_loaded_ = true;
        try {
            const auto document = read_igb_document(engine.resources().read("_common/sbiallse.igb"));
            for (const auto& object : document.objects)
                if (document.metadata.object_types[object.type_index].name == "beCriSfpData")
                    add_bank(engine, ObjectView(&document, object.reference_index), true);
        } catch (const std::exception& error) {
            engine.note(std::string("audio _common/sbiallse.igb: ") + error.what());
        }
    }
    std::string file = data.file >= 0 && data.file < int(files_.size()) ? files_[std::size_t(data.file)] : std::string();
    file = file.substr(file.find_last_of('/') + 1);
    for (auto& c : file) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::shared_ptr<SeBank> bank;
    for (const auto& candidate : banks_)
        if (candidate->bank->has_group(data.request) && (!bank || candidate->name == file)) bank = candidate;
    const auto sequence = bank ? bank->bank->sequence(data.request) : std::span<const std::uint8_t>{};
    if (sequence.empty()) {
        char id[16];
        std::snprintf(id, sizeof id, "%08X", data.request);
        engine.note("audio SE request " + std::string(id) + " " + name + " (no bank)");
        return true;
    }
    // A looping effect (type 4) already sounding keeps its voice.
    if (data.type == 4)
        for (const auto& voice : voices_)
            if (voice.name == name && voice.owner == owner && voice.loop) return true;
    if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace)
        engine.log("sound effect " + name + " from " + bank->name + " at frame " + std::to_string(engine.frame()));
    SeTrack track;
    track.bank = bank;
    track.sequence = sfp::Sequence(sequence);
    track.name = name;
    track.owner = owner;
    track.gain = volume_gain(data.volume) * float(se_volume_ ? se_volume_ : 127) / 127.0f;
    run_track(track);
    if (!track.sequence.finished()) tracks_.push_back(std::move(track));
    return true;
}

// Runs a track's due events (803C49F4): a note starts its split's sample.
void SoundManager::run_track(SeTrack& track) {
    constexpr std::size_t kMaxVoices = 48;
    while (track.wait_ms <= 0.0 && !track.sequence.finished()) {
        const auto event = track.sequence.next();
        if (event.kind == sfp::Event::Kind::end) break;
        track.wait_ms += double(event.wait_ms);
        if (event.kind != sfp::Event::Kind::note || event.velocity == 0) continue;
        const auto* split = track.bank->bank->split(event.key);
        if (!split) continue;
        SeVoice voice;
        voice.bank = track.bank;
        voice.sample = split->sample;
        voice.name = track.name;
        voice.owner = track.owner;
        voice.loop = track.bank->bank->sample(split->sample).loop;
        if (split->detune) {
            random_ = random_ * 1103515245u + 12345u;
            const int cents = int((random_ >> 16) % (2u * split->detune)) - split->detune;
            voice.pitch = std::exp2(double(cents) / 1200.0);
        }
        const float gain = track.gain * float(track.bank->bank->level(event.velocity)) / 255.0f;
        const auto [left, right] = mix_pan(kSfpPan[std::min<std::size_t>(split->pan, 127)]);
        voice.left = gain * left;
        voice.right = gain * right;
        voices_.push_back(std::move(voice));
        if (voices_.size() > kMaxVoices) voices_.erase(voices_.begin());
    }
}

void SoundManager::mix_voices(float* mix, std::size_t frames) {
    for (auto& voice : voices_) {
        const auto& pcm = voice.bank->pcm[std::size_t(voice.sample)];
        const auto& sample = voice.bank->bank->sample(std::size_t(voice.sample));
        const std::size_t loop = voice.bank->loop_start[std::size_t(voice.sample)];
        const double step = double(sample.rate) * voice.pitch / double(kOutputRate);
        for (std::size_t i = 0; i < frames && voice.position < pcm.size(); ++i) {
            std::size_t after = voice.position + 1;
            if (after >= pcm.size()) after = voice.loop && loop < pcm.size() ? loop : voice.position;
            const float a = float(pcm[voice.position]);
            const float value = a + (float(pcm[after]) - a) * float(voice.phase);
            mix[i * 2] += value * voice.left;
            mix[i * 2 + 1] += value * voice.right;
            voice.phase += step;
            while (voice.phase >= 1.0 && voice.position < pcm.size()) {
                voice.phase -= 1.0;
                if (++voice.position >= pcm.size() && voice.loop && loop < pcm.size()) voice.position = loop;
            }
        }
    }
    std::erase_if(voices_, [](const SeVoice& voice) { return voice.position >= voice.bank->pcm[std::size_t(voice.sample)].size(); });
}

SoundManager::Stream* SoundManager::stream_named(std::string_view name) {
    for (auto& stream : streams_)
        if (stream.decoder && stream.name == name) return &stream;
    return nullptr;
}

void SoundManager::stop(int channel) {
    auto& stream = streams_[std::size_t(channel)];
    stream.decoder.reset();
    stream.fade_left = 0;
    stream.name.clear();
    if (channel == 0) current_bgm_.clear();
}

void SoundManager::render(std::int16_t* out, std::size_t frames) {
    std::vector<float> mix(frames * 2, 0.0f);
    for (auto& stream : streams_) mix_stream(stream, mix.data(), frames);
    mix_stream(movie_stream_, mix.data(), frames);
    mix_voices(mix.data(), frames);
    // Peaks where effects meet the music bend towards full scale instead of
    // clipping.
    constexpr float knee = 24576.0f;
    for (std::size_t i = 0; i < frames * 2; ++i) {
        float value = mix[i];
        const float over = std::abs(value) - knee;
        if (over > 0.0f) value = std::copysign(knee + over / (1.0f + over / 8192.0f), value);
        out[i] = static_cast<std::int16_t>(std::clamp(value, -32768.0f, 32767.0f));
    }
}

// One stream resampled into the mix.
void SoundManager::mix_stream(Stream& stream, float* mix, std::size_t frames) {
    {
        if (!stream.decoder || stream.paused || stream.ready_in > 0) return;
        const auto& header = stream.decoder->header();
        float ramp = stream.ramp_to;
        if (stream.ramp_left > 0 && stream.ramp_frames > 0)
            ramp = stream.ramp_to + (stream.ramp_from - stream.ramp_to) * float(stream.ramp_left) / float(stream.ramp_frames);
        const float gain = stream.gain * ramp * (stream.fade_left > 0 ? float(stream.fade_left) / 10.0f : 1.0f);
        const std::uint32_t channels = header.channels;
        // 802A7F34: a stereo stream's channels sit hard left and right (pan
        // 0 / 127, full level on their side), a mono stream in the centre.
        const auto [centre_left, centre_right] = mix_pan(64);
        std::int16_t source[16];
        auto fetch = [&](std::array<float, 2>& frame) {
            if (stream.decoder->read(source, 1) == 0) return false;
            ++stream.frames_read;
            if (channels > 1) {
                frame[0] = float(source[0]);
                frame[1] = float(source[1]);
            } else {
                frame[0] = float(source[0]) * centre_left;
                frame[1] = float(source[0]) * centre_right;
            }
            return true;
        };
        const double step = double(header.sample_rate) / double(kOutputRate);
        bool ended = false;
        for (std::size_t i = 0; i < frames && !ended; ++i) {
            while (stream.phase >= 1.0) {
                stream.previous = stream.next;
                if (!fetch(stream.next)) {
                    ended = true;
                    break;
                }
                stream.phase -= 1.0;
            }
            if (ended) break;
            const float t = float(stream.phase);
            mix[i * 2] += (stream.previous[0] + (stream.next[0] - stream.previous[0]) * t) * gain;
            mix[i * 2 + 1] += (stream.previous[1] + (stream.next[1] - stream.previous[1]) * t) * gain;
            stream.phase += step;
        }
        if (ended) stream.decoder.reset();
    }
}

// 8031EDFC: MOVIE PLAY finds the movie's beMovieInfoData and starts the
// Sofdec player on the file (video stream 0xE0, ADX sound in 0xC0).
void SoundManager::play_movie(Engine& engine, const Ram& ram, const std::string& name) {
    Movie movie;
    movie.name = name;
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    };
    for (const auto& data : ram.info.list(5))
        if (data && lower(std::string(data.str(2))) == lower(name)) {
            movie.script = std::string(data.str(5));
            break;
        }
    movie_stream_.decoder.reset();
    try {
        auto streams = sfd::demux(engine.resources().read(lower(name), 512u * 1024u * 1024u));
        const auto info = sfd::video_info(streams.video);
        if (info.frame_rate > 0.0) movie.frame_rate = info.frame_rate;
        movie.pictures = info.pictures;
        movie.video = std::move(streams.video);
        if (!streams.audio.empty()) {
            movie_stream_.decoder = std::make_unique<adx::Decoder>(std::move(streams.audio));
            movie_stream_.name = name;
            movie_stream_.gain = 1.0f;
            movie_stream_.fade_left = movie_stream_.ready_in = movie_stream_.ramp_frames = movie_stream_.ramp_left = 0;
            movie_stream_.paused = false;
            movie_stream_.ramp_from = movie_stream_.ramp_to = 1.0f;
            movie_stream_.phase = 1.0;
            movie_stream_.previous = movie_stream_.next = {};
        }
    } catch (const std::exception& error) {
        engine.note("movie " + name + ": " + error.what());
    }
    movie.start = engine.time();
    movie.serial = ++movie_serial_;
    movie_ = std::move(movie);
}

// 8031EC10: the movie ends after its last picture or when player 1 presses
// A or START; its end script runs.
void SoundManager::end_movie(Engine& engine) {
    const std::string script = movie_ ? movie_->script : std::string();
    movie_.reset();
    movie_stream_.decoder.reset();
    if (!script.empty()) engine.run_script(script);
}
bool SoundManager::command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
    const auto& t = ram.info.type();
    if (t == "beMovieInfo") {
        if (command == "PLAY") {
            play_movie(engine, ram, std::string(packet.string(0)));
            return true;
        }
        if (command == "ISPLAY" || command == "ISRUN") {
            packet.bools.push_back(movie_ ? 1 : 0);
            return true;
        }
        if (command == "STOP") {
            if (movie_) end_movie(engine);
            return true;
        }
        return false;
    }
    if (t == "beSoundInfo") {
        if (command == "PLAY") return play(engine, packet);
        if (command == "PLAYatPAUSE") return play(engine, packet, true);
        if (command == "PAUSEOFF") {
            // 802F61F8: the channel playing that sound leaves its pause.
            if (auto* stream = stream_named(packet.string(0))) stream->paused = false;
            return true;
        }
        if (command == "VOLUMECTL2") {
            // name/F<from>/F<to>/F<frames>: a gain ramp on that sound.
            if (auto* stream = stream_named(packet.string(0))) {
                stream->ramp_from = packet.real(0, 1.0f);
                stream->ramp_to = packet.real(1, 1.0f);
                stream->ramp_frames = stream->ramp_left = std::max(0, int(packet.real(2, 0.0f)));
            }
            return true;
        }
        if (command == "BGMSTOP") { stop(0); return true; }
        if (command == "ENVSTOP") { stop(1); return true; }
        if (command == "STOP") {
            for (std::size_t channel = 0; channel < streams_.size(); ++channel) stop(int(channel));
            tracks_.clear();
            voices_.clear();
            return true;
        }
        if (command == "SNDFVOLUME") {
            // 802F7244: the Sound Factory master volume, set once (803C2734).
            if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace)
                engine.log("SNDFVOLUME " + std::to_string(packet.integer(0, 0)));
            if (!se_volume_) {
                const int volume = packet.integer(0, 0);
                se_volume_ = volume <= 0 || volume > 127 ? 127 : volume;
            }
            return true;
        }
        if (command == "SESTOP") {
            // All sound effects, or those of one sound.
            const std::string name = packet.strings.empty() ? std::string() : std::string(packet.string(0));
            stop_effects(name, {});
            if (name.empty()) {
                cues_.clear();
                owner_loops_.clear();
            }
            for (std::size_t channel = 2; channel < streams_.size(); ++channel)
                if (name.empty() || streams_[channel].name == name) stop(int(channel));
            return true;
        }
    }
    // A started stream reports ready once its buffer is filled; scripts wait
    // for ISALLREADY to drop after PLAYatPAUSE and to return.
    if (command == "ISREADY") {
        const auto* stream = stream_named(packet.string(0));
        packet.bools.push_back(!stream || stream->ready_in == 0 ? 1 : 0);
        return true;
    }
    if (command == "ISBGMREADY" || command == "ISENVREADY") {
        const auto& stream = streams_[command == "ISBGMREADY" ? 0 : 1];
        packet.bools.push_back(!stream.decoder || stream.ready_in == 0 ? 1 : 0);
        return true;
    }
    if (command == "ISALLREADY") {
        bool ready = true;
        for (const auto& stream : streams_) ready = ready && (!stream.decoder || stream.ready_in == 0);
        packet.bools.push_back(ready ? 1 : 0);
        return true;
    }
    if (command.starts_with("IS")) {
        packet.bools.push_back(1);
        return true;
    }
    engine.note("audio " + std::string(command) + (packet.strings.empty() ? "" : " " + std::string(packet.string(0))));
    return true;
}
namespace {
// The stream state a save state keeps (simulation-visible fields and the
// decoding position, so the music goes on where it was).
struct StreamState {
    std::string name, file;
    std::uint64_t frames_read{};
    float gain = 1.0f;
    int fade_left{}, ready_in{}, ramp_frames{}, ramp_left{};
    bool paused{};
    float ramp_from = 1.0f, ramp_to = 1.0f;
};
struct SoundState {
    std::vector<StreamState> streams;
    std::string current_bgm;
    int se_volume{};
    std::uint32_t random{};
};
} // namespace

std::shared_ptr<const void> SoundManager::persistent() const {
    auto state = std::make_shared<SoundState>();
    for (const auto& stream : streams_) {
        StreamState s;
        if (stream.decoder) {
            s.name = stream.name;
            s.file = stream.file;
            s.frames_read = stream.frames_read;
        }
        s.gain = stream.gain;
        s.fade_left = stream.fade_left;
        s.ready_in = stream.ready_in;
        s.ramp_frames = stream.ramp_frames;
        s.ramp_left = stream.ramp_left;
        s.paused = stream.paused;
        s.ramp_from = stream.ramp_from;
        s.ramp_to = stream.ramp_to;
        state->streams.push_back(std::move(s));
    }
    state->current_bgm = current_bgm_;
    state->se_volume = se_volume_;
    state->random = random_;
    return state;
}

void SoundManager::restore(Engine& engine, const std::shared_ptr<const void>& opaque) {
    if (!opaque) return;
    const auto& state = *static_cast<const SoundState*>(opaque.get());
    for (std::size_t i = 0; i < streams_.size() && i < state.streams.size(); ++i) {
        const auto& s = state.streams[i];
        auto& stream = streams_[i];
        stream = Stream{};
        if (!s.file.empty()) {
            unsigned archive = 0, entry = 0;
            char close = 0;
            try {
                const auto& archives = engine.resources().archives();
                if (std::sscanf(s.file.c_str(), "[%u.%u%c", &archive, &entry, &close) == 3 && archive < archives.size()) {
                    stream.decoder = std::make_unique<adx::Decoder>(archives[archive].read(entry, 64u * 1024u * 1024u));
                    std::int16_t scratch[16 * 512];
                    const std::uint32_t channels = std::max<std::uint32_t>(1, stream.decoder->header().channels);
                    std::uint64_t left = s.frames_read;
                    while (left > 0) {
                        const auto chunk = std::size_t(std::min<std::uint64_t>(left, sizeof scratch / sizeof scratch[0] / channels));
                        const auto got = stream.decoder->read(scratch, chunk);
                        if (got == 0) break;
                        left -= got;
                    }
                    stream.name = s.name;
                    stream.file = s.file;
                    stream.frames_read = s.frames_read - left;
                }
            } catch (const std::exception&) {
                stream.decoder.reset();
            }
        }
        stream.gain = s.gain;
        stream.fade_left = s.fade_left;
        stream.ready_in = s.ready_in;
        stream.ramp_frames = s.ramp_frames;
        stream.ramp_left = s.ramp_left;
        stream.paused = s.paused;
        stream.ramp_from = s.ramp_from;
        stream.ramp_to = s.ramp_to;
    }
    current_bgm_ = state.current_bgm;
    se_volume_ = state.se_volume;
    random_ = state.random;
}

void SoundManager::serialize_persistent(StateArchive& a, std::shared_ptr<const void>& opaque) {
    bool present = opaque != nullptr;
    a.io(present);
    if (!present) return;
    auto state = a.reading() ? std::make_shared<SoundState>() : std::make_shared<SoundState>(*static_cast<const SoundState*>(opaque.get()));
    std::uint32_t count = std::uint32_t(state->streams.size());
    a.io(count);
    if (a.reading()) {
        if (count > 64) throw std::runtime_error("save state holds an implausible stream count");
        state->streams.resize(count);
    }
    for (auto& s : state->streams) {
        a.io(s.name);
        a.io(s.file);
        a.io(s.frames_read);
        a.io(s.gain);
        a.io(s.fade_left);
        a.io(s.ready_in);
        a.io(s.ramp_frames);
        a.io(s.ramp_left);
        a.io(s.paused);
        a.io(s.ramp_from);
        a.io(s.ramp_to);
    }
    a.io(state->current_bgm);
    a.io(state->se_volume);
    a.io(state->random);
    if (a.reading()) opaque = state;
}

void SoundManager::update(Engine& engine) {
    for (auto& stream : streams_) {
        if (stream.ready_in > 0) --stream.ready_in;
        if (stream.ramp_left > 0 && !stream.paused) --stream.ramp_left;
    }
    for (std::size_t channel = 0; channel < streams_.size(); ++channel)
        if (streams_[channel].fade_left > 0 && --streams_[channel].fade_left == 0) stop(int(channel));
    for (auto& track : tracks_) {
        track.wait_ms -= double(engine.frame_delta()) / 1e6; // OSGetTick milliseconds of one field
        run_track(track);
    }
    std::erase_if(tracks_, [](const SeTrack& track) { return track.sequence.finished(); });
    for (std::size_t i = 0; i < cues_.size();) {
        if (--cues_[i].frames > 0) {
            ++i;
            continue;
        }
        const PendingCue cue = cues_[i];
        cues_.erase(cues_.begin() + std::ptrdiff_t(i));
        play_cue(engine, cue.sound, cue.owner);
    }
    if (movie_) {
        const auto& pad = engine.pads()[0];
        const bool skip = (pad.buttons.trigger & (pad_button::a | pad_button::start)) != 0;
        // Ends once the time for the last picture has passed.
        if (skip || movie_->frame_rate <= 0.0 || movie_->picture_at(engine.time()) >= movie_->pictures) end_movie(engine);
    }
}

// ---------------------------------------------------------------------------
// Texture control
// ---------------------------------------------------------------------------
void TextureCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    make_rams(engine, this, info, rams_, "TEXTURECTRL");
    const bool pack = info.boolean(7);
    for (const auto& item : info.list(8)) {
        if (!item) continue;
        if (pack) pack_data_.push_back(item);
        else ctrl_attrs_.push_back(item);
    }
    pending_ = true;
}

void TextureCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    pack_data_.clear();
    ctrl_attrs_.clear();
    owned_.clear();
    pending_ = false;
}

void TextureCtrlManager::bind_all(Engine& engine) {
    for (const auto& ctrl : ctrl_attrs_) {
        const auto name = ctrl.str(2);
        ObjectView texture;
        for (const auto& data : pack_data_) {
            if (data.str(2) == name) { texture = data.ref(3); break; }
        }
        if (!texture) continue;
        Node* set = engine.scenes().node(ctrl.ref(3));
        if (!set) continue;
        auto bind = std::make_unique<Attribute>();
        bind->kind = AttrKind::texture_bind;
        bind->unit = 0;
        bind->texture = texture;
        bind->image = texture.ref(12);
        bind->sampler.mag_filter = texture.i32(5, 1);
        bind->sampler.min_filter = texture.i32(6, 1);
        bind->sampler.wrap_s = texture.i32(7, 1);
        bind->sampler.wrap_t = texture.i32(8, 1);
        bind->object = texture;
        auto& attrs = set->attributes;
        attrs.erase(std::remove_if(attrs.begin(), attrs.end(), [](const Attribute* a) { return a->kind == AttrKind::texture_bind; }),
                    attrs.end());
        attrs.push_back(bind.get());
        owned_.push_back(std::move(bind));
    }
}

void TextureCtrlManager::update(Engine& engine) {
    if (!pending_) return;
    pending_ = false;
    bind_all(engine);
}

// ---------------------------------------------------------------------------
// PLAYERPOINT (bePoint01, 80312A70 / 803129E0)
// ---------------------------------------------------------------------------
void PlayerPointManager::add_info(Engine& engine, const ObjectView& info) {
    infos_.push_back(info);
    make_rams(engine, this, info, rams_, "PLAYERPOINT");
}

void PlayerPointManager::clear_stage(Engine&) {
    rams_.clear();
    infos_.clear();
}

std::optional<Mat4> PlayerPointManager::point_matrix(int group, int player) const {
    for (const auto& info : infos_) {
        const auto groups = info.list(8);
        if (groups.empty()) continue;
        if (group > int(groups.size()) - 1) group = 0; // 803129E0 local fallback
        if (group < 0) return std::nullopt;
        const auto& g = groups[std::size_t(group)];
        if (!g) return Mat4{}; // null group: identity (804F89B8)
        const auto points = g.list(7);
        if (player < 0 || std::size_t(player) >= points.size() || !points[std::size_t(player)]) return std::nullopt;
        return points[std::size_t(player)].mat4(8);
    }
    return std::nullopt;
}

bool PlayerPointManager::command(Engine&, Ram&, std::string_view command, Packet& packet) {
    if (command == "SETGENERATEPOS") {
        if (!packet.ints.empty()) point_no_ = packet.ints[0];
        return true;
    }
    if (command == "GETGENERATEPOS") {
        packet.ints.push_back(point_no_);
        return true;
    }
    if (command == "GETMATRIX") {
        const int player = packet.ints.size() > 1 ? packet.ints[1] : 0;
        if (auto m = point_matrix(point_no_, player)) packet.matrices.push_back(*m);
        return true;
    }
    if (command == "GETMATRIXDIRECT") {
        const int group = packet.ints.size() > 0 ? packet.ints[0] : 0;
        const int player = packet.ints.size() > 1 ? packet.ints[1] : 0;
        if (auto m = point_matrix(group, player)) packet.matrices.push_back(*m);
        return true;
    }
    if (command == "GETPOINTNAME") {
        for (const auto& info : infos_)
            for (const auto& g : info.list(8)) packet.strings.emplace_back(g ? std::string(g.str(2)) : std::string());
        return true;
    }
    if (command == "SHOWPOLYGON" || command == "HIDEPOLYGON") return true;
    return false;
}

// ---------------------------------------------------------------------------
// HitLand collision geometry
// ---------------------------------------------------------------------------
void HitLandManager::add_info(Engine& engine, const ObjectView& info) {
    if (info.type() == "beHitLandDelivInfo") {
        const auto rams = info.list(6);
        std::int32_t index = 0;
        for (const auto& record : rams) {
            auto ram = std::make_unique<DelivRam>();
            ram->manager = this;
            ram->info = info;
            ram->ram = record;
            ram->info_name = std::string(info.str(2));
            ram->index = index++;
            ram->active = record ? record.boolean(5) : false;
            if (record) {
                ram->node = engine.scenes().node(record.ref(11));
                ram->flags = std::uint32_t(record.i32(12));
                ram->width = record.f32(13);
                ram->up = record.f32(14);
                ram->down = record.f32(15);
                ram->position = record.vec3(16).value_or(Vec3f{});
                ram->previous = record.vec3(17).value_or(Vec3f{});
                ram->land = record.vec3(18).value_or(Vec3f{});
            }
            engine.messenger().add_row("HITLANDDELIVER", ram->info_name, ram->index, ram.get());
            deliveries_.push_back(std::move(ram));
        }
        return;
    }
    const auto info_index = world_.add_info(std::string(info.str(2)));
    make_rams(engine, this, info, rams_, "HITLANDMODEL");
    // _checkSwitch [8] selects the queried geometry; _kind [10] its query bits.
    // Kind bit 0x400 marks model-attached geometry whose children follow the
    // source model transform (HITLAND_TRAVERSAL.md); that dynamic collision is
    // not reconstructed, so it is left out of the static world.
    const auto kind = std::uint32_t(info.i32(10));
    if (kind & 0x400u) {
        // Each check-switch child is a beCopyTransform following its model
        // instance ([12]), e.g. one per box of g_box.
        const auto check = info.ref(8);
        const auto children = check && check.is_a("igGroup") ? check.list(7) : std::vector<ObjectView>{check};
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto& child = children[i];
            if (!child) continue;
            followers_.push_back(
                {world_.add_dynamic(child, kind, info_index, std::uint16_t(i)), child.ref(12), std::string(info.str(2))});
        }
        return;
    }
    // Each check-switch child is its own switchable group, addressed by the
    // matching name of the info's name list [9] (e.g. "hithideliftz00").
    const auto groups = world_.add_switch(info.ref(8), kind, info_index,
                                          [&](const ObjectView& transform) -> const Node* { return engine.scenes().node(transform); });
    const auto names = info.list(9);
    for (std::size_t i = 0; i < groups.size() && i < names.size(); ++i) {
        if (!names[i] || names[i].str(2).empty()) continue;
        auto child = std::make_unique<HitRam>();
        child->manager = this;
        child->info = info;
        child->info_name = std::string(names[i].str(2));
        child->index = 0;
        child->active = true;
        child->group = groups[i];
        engine.messenger().add_row("HITLANDMODEL", child->info_name, 0, child.get());
        children_.push_back(std::move(child));
    }
}

bool HitLandManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    if (auto* delivery = dynamic_cast<DelivRam*>(&base)) return deliver(engine, *delivery, command, packet);
    if (command == "CHECKLAND" || command == "CHECKWALL" || command == "CHECKWALLFULL") return query(engine, command, packet);
    if (command != "ONHIT" && command != "OFFHIT") return false;
    info_off_[base.info_name] = command == "OFFHIT";
    for (const auto& child : children_)
        if (child.get() == &base) world_.set_group_enabled(child->group, command == "ONHIT");
    return true;
}

void HitLandManager::update(Engine& engine) {
    if (followers_.empty()) return;
    // The copy transforms take their model's matrix (80128CF4); a removed
    // model (a broken box) leaves no collision behind.
    auto* models = engine.get<ModelCtrlManager>();
    for (const auto& follower : followers_) {
        const Node* node = engine.scenes().node(follower.source);
        bool live = false;
        if (node && models)
            for (const auto& ram : models->rams())
                if (ram->node == node) {
                    live = ram->generated;
                    break;
                }
        const auto off = info_off_.find(follower.info);
        world_.set_dynamic(follower.id, node ? node->matrix : Mat4{}, live && (off == info_off_.end() || !off->second));
        if (static const bool trace = std::getenv("DW4_TRACE_HITLAND") != nullptr; trace && live && engine.frame() % 300 == 0)
            engine.log("hitland " + follower.info + " follows (" + std::to_string(node->matrix(3, 0)) + ", " +
                       std::to_string(node->matrix(3, 1)) + ", " + std::to_string(node->matrix(3, 2)) + ")");
    }
}

// HITLANDMODEL CHECKLAND (80300444) / CHECKWALL (802FE7F0). Every
// HITLANDMODEL row receives the send; the first answer fills the booleans,
// which later rows take as already answered (the leaves' own guard).
bool HitLandManager::query(Engine& engine, std::string_view command, Packet& packet) {
    (void)engine;
    if (!packet.bools.empty() || packet.vectors.empty() || packet.floats.empty()) return true;
    if (command == "CHECKLAND") {
        if (packet.floats.size() < 3) return true;
        const auto result = world_.check_land(packet.vectors[0], packet.floats[0], packet.floats[1], packet.floats[2]);
        if (result.found) {
            packet.ints.clear();
            packet.ints.push_back(result.child);
            packet.strings.emplace_back(world_.info_name(result.info));
        }
        packet.vectors.clear();
        packet.bools = {std::uint8_t(result.in_range), std::uint8_t(result.adjusted), std::uint8_t(result.found)};
        packet.vectors.push_back(result.position);
        return true;
    }
    if (packet.ints.empty() || packet.vectors.empty() || packet.floats.size() < 3) return true;
    WallResult result;
    if (command == "CHECKWALLFULL") {
        // 802FF748: the riding query takes only the position.
        result = world_.check_wall_full(packet.vectors[0], packet.floats[0], packet.floats[2], std::uint32_t(packet.ints[0]));
    } else {
        if (packet.ints.size() < 2 || packet.vectors.size() < 2) return true;
        result = world_.check_wall(packet.vectors[0], packet.vectors[1], packet.floats[0], packet.floats[2],
                                   std::uint32_t(packet.ints[0]), packet.ints[1] != 0);
    }
    packet.ints.clear();
    for (const auto& hit : result.hits) {
        packet.ints.push_back(hit.child);
        packet.strings.emplace_back(world_.info_name(hit.info));
    }
    packet.bools.clear();
    packet.vectors.clear();
    packet.vectors.push_back(result.position);
    packet.bools.push_back(result.hit ? 1 : 0);
    return true;
}

// 802FDA0C: HITWALL / HITWALLFULL query the walls from the previous to the
// current node position and move the node; HITFLOORCHECK / HITFLOOR keep the
// floor below the node (a ground plane at Y 0 below every stage), HITFLOOR and
// HITFLOORADJUST put the node on it.
bool HitLandManager::deliver(Engine& engine, DelivRam& ram, std::string_view command, Packet& packet) {
    auto& messenger = engine.messenger();
    const bool full = command == "HITWALLFULL";
    if (command == "HITWALL" || full) {
        if (!ram.node) return true;
        const Vec3f current{ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)};
        bool moved = false;
        Packet answer;
        bool queried = false;
        if (full || current.x != ram.previous.x || current.z != ram.previous.z) {
            answer.vectors.push_back(current);
            if (full) {
                answer.vectors.push_back(current);
            } else {
                Vec3f direction{current.x - ram.previous.x, 0.0f, current.z - ram.previous.z};
                const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
                direction = direction * (1.0f / length);
                answer.vectors.push_back(direction);
            }
            answer.floats = {ram.width, ram.up, ram.down};
            answer.ints = {std::int32_t(ram.flags), ram.position.x == 0.0f ? 0 : 1};
            messenger.send(engine, "HITLANDMODEL", std::nullopt, -1, full ? "CHECKWALLFULL" : "CHECKWALL", answer);
            queried = true;
            const bool hit = !answer.bools.empty() && answer.bools[0] == 1;
            if (static const bool trace = std::getenv("DW4_TRACE_HITLAND") != nullptr; trace && hit) {
                std::string walls;
                for (const auto& name : answer.strings) walls += " " + (name ? *name : std::string("?"));
                char line[160];
                std::snprintf(line, sizeof line, "hitwall %s#%d at (%.1f, %.1f, %.1f) by", ram.info_name.c_str(), ram.index,
                              current.x, current.y, current.z);
                engine.log(line + walls);
            }
            ram.position = hit ? Vec3f{1.0f, 0.0f, 0.0f} : Vec3f{};
            if (!answer.vectors.empty()) {
                if (hit) {
                    ram.node->matrix(3, 0) = answer.vectors[0].x;
                    ram.node->matrix(3, 1) = answer.vectors[0].y;
                    ram.node->matrix(3, 2) = answer.vectors[0].z;
                    moved = true;
                }
                ram.previous = answer.vectors[0];
            }
        }
        (void)queried;
        packet.bools = {std::uint8_t(moved), std::uint8_t(moved), 0};
        return true;
    }
    const bool floor_check = command == "HITFLOORCHECK", floor = command == "HITFLOOR";
    if (floor_check || floor) {
        if (!ram.node) return true;
        Packet answer;
        answer.vectors.push_back({ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)});
        answer.floats = {ram.width, ram.up, ram.down};
        answer.ints.push_back(std::int32_t(ram.flags));
        messenger.send(engine, "HITLANDMODEL", std::nullopt, -1, "CHECKLAND", answer);
        Vec3f found{0.0f, -1.0f, 0.0f};
        std::uint8_t in_range = 0, adjusted = 0, valid = 0;
        if (answer.bools.size() >= 3 && !answer.vectors.empty()) {
            found = answer.vectors[0];
            in_range = answer.bools[0];
            adjusted = answer.bools[1];
            valid = answer.bools[2];
        }
        if (found.y >= 1.0f) {
            ram.land = ram.previous = found;
            ram.land_valid = valid != 0;
        } else if (ram.node->matrix(3, 1) < 0.0f) {
            ram.land = ram.previous = {found.x, 0.0f, found.z};
            ram.land_valid = true;
            in_range = adjusted = 1;
        } else {
            ram.land_valid = false;
            in_range = 1;
            adjusted = 0;
        }
        packet.bools = {in_range, adjusted};
        // The ride: the floor's info name and child (the CHECKLAND answer).
        packet.strings = answer.strings;
        packet.ints = answer.ints;
        if (floor && ram.land_valid) {
            ram.node->matrix(3, 0) = ram.land.x;
            ram.node->matrix(3, 1) = ram.land.y;
            ram.node->matrix(3, 2) = ram.land.z;
        }
        return true;
    }
    if (command == "HITFLOORADJUST") {
        if (ram.land_valid && ram.node) {
            ram.node->matrix(3, 0) = ram.land.x;
            ram.node->matrix(3, 1) = ram.land.y;
            ram.node->matrix(3, 2) = ram.land.z;
        }
        return true;
    }
    return false;
}

void HitLandManager::clear_stage(Engine&) {
    deliveries_.clear();
    followers_.clear();
    info_off_.clear();
    rams_.clear();
    children_.clear();
    world_.clear();
}

// ---------------------------------------------------------------------------
void ParticleCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    for (auto* ram : make_rams(engine, this, info, rams_, "MODELCTRL")) {
        ram->transform = engine.scenes().node(ram->ram.ref(11));
        ram->timer = engine.scenes().node(ram->ram.ref(12));
    }
}

void ParticleCtrlManager::clear_stage(Engine&) { rams_.clear(); }

void ParticleCtrlManager::stop(ParticleRam& ram) {
    // 803123DC: the timer stops, the instance's switch bit goes and the
    // outer switch with the last one.
    ram.running = false;
    ram.owner.clear();
    if (ram.timer) {
        ram.timer->particle_start = -1;
        ram.timer->particle_paused = -1;
    }
}

bool ParticleCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<ParticleRam&>(base);
    if (command == "FIND_SPACE") {
        if (!ram.running && packet.ints.empty()) packet.ints.push_back(ram.index);
        return true;
    }
    if (command == "GENERATE") {
        Mat4 m;
        if (!packet.strings.empty()) {
            // The owner's matrix (MODELCTRL GETMATRIX), the offset in its
            // space, then the rotations about X, Y and Z.
            const std::string owner(packet.string(0));
            const int index = packet.integer(0, 0);
            const Vec3f offset = packet.vectors.empty() ? Vec3f{} : packet.vectors[0];
            Vec3f rotation{};
            if (packet.vectors.size() == 2) {
                rotation = packet.vectors[1];
                // 80312554: a heading above 2 pi is drawn from the system
                // generator, 0 .. 6.143 radians.
                if (rotation.y > 6.28319f) rotation.y = float(fight::random() % 6144u) / 1000.0f;
            }
            if (auto* models = engine.get<ModelCtrlManager>())
                if (const auto* target = models->find(owner, index); target && target->node) m = target->node->matrix;
            m = translation(offset.x, offset.y, offset.z) * m;
            auto rotate = [&](int axis, float angle) {
                if (angle == 0.0f) return;
                Mat4 q;
                const float c = std::cos(angle), s = std::sin(angle);
                if (axis == 0) { q(1, 1) = c; q(1, 2) = s; q(2, 1) = -s; q(2, 2) = c; }
                else if (axis == 1) { q(0, 0) = c; q(0, 2) = -s; q(2, 0) = s; q(2, 2) = c; }
                else { q(0, 0) = c; q(0, 1) = s; q(1, 0) = -s; q(1, 1) = c; }
                m = q * m;
            };
            rotate(0, rotation.x);
            rotate(1, rotation.y);
            rotate(2, rotation.z);
            ram.owner = packet.boolean(0) ? owner : std::string();
            ram.owner_index = index;
            ram.offset = offset;
        }
        if (ram.transform) ram.transform->matrix = m;
        ram.running = true;
        ram.state = 0;
        if (static const bool trace = std::getenv("DW4_TRACE_PARTICLES") != nullptr; trace)
            engine.log("particles " + ram.info_name + "#" + std::to_string(ram.index) + " at (" + std::to_string(m(3, 0)) + ", " +
                       std::to_string(m(3, 1)) + ", " + std::to_string(m(3, 2)) + ")" + (ram.timer ? "" : " without timer"));
        return true;
    }
    if (command == "ISRUN") {
        packet.bools.push_back(ram.running ? 1 : 0);
        return true;
    }
    if (command == "SQ_ERAS") {
        if (ram.running) ram.state = 2;
        return true;
    }
    if (command == "GETSEQNAMELIST") {
        for (const char* name : {"GENERATE", "GENERATE", "SQ_ERAS"}) packet.strings.emplace_back(name);
        return true;
    }
    if (command == "ONFREEZE" || command == "OFFREEZE") {
        if (ram.running && ram.timer && ram.timer->particle_start >= 0) {
            if (command == "ONFREEZE" && ram.timer->particle_paused < 0) ram.timer->particle_paused = engine.time();
            if (command == "OFFREEZE" && ram.timer->particle_paused >= 0) {
                ram.timer->particle_start += engine.time() - ram.timer->particle_paused;
                ram.start += engine.time() - ram.timer->particle_paused;
                ram.timer->particle_paused = -1;
            }
        }
        return true;
    }
    return command == "ONCTRL" || command == "OFCTRL";
}

void ParticleCtrlManager::update(Engine& engine) {
    auto* models = engine.get<ModelCtrlManager>();
    for (auto& owned : rams_) {
        auto& ram = *owned;
        Node* outer = engine.scenes().node(ram.info.ref(7));
        Node* instances = engine.scenes().node(ram.info.ref(8));
        if (!ram.running) continue;
        // A locked instance follows its owner (80312298).
        if (!ram.owner.empty() && models && ram.transform)
            if (const auto* target = models->find(ram.owner, ram.owner_index); target && target->node)
                ram.transform->matrix = translation(ram.offset.x, ram.offset.y, ram.offset.z) * target->node->matrix;
        if (ram.state == 0) {
            if (outer && outer->kind == NodeKind::switch_node) outer->set_child_enabled(0, true);
            if (instances && instances->kind == NodeKind::switch_node) instances->set_child_enabled(std::size_t(ram.index), true);
            if (ram.timer) {
                ram.timer->particle_start = engine.time();
                ram.timer->particle_paused = -1;
            }
            ram.start = engine.time();
            ram.state = 1;
            continue;
        }
        const bool paused = ram.timer && ram.timer->particle_paused >= 0;
        if (ram.state == 1 && (paused || engine.time() - ram.start < ram.info.i64(10))) continue;
        stop(ram);
        if (instances && instances->kind == NodeKind::switch_node) {
            instances->set_child_enabled(std::size_t(ram.index), false);
            bool any = false;
            for (std::size_t i = 0; i < instances->children.size(); ++i) any = any || instances->child_enabled(i);
            if (!any && outer && outer->kind == NodeKind::switch_node) outer->set_child_enabled(0, false);
        }
    }
}

// ---------------------------------------------------------------------------
void LightCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    make_rams(engine, this, info, rams_, "LIGHTCTRL");
    for (const auto& set : info.list(7)) {
        if (!set) continue;
        auto& lights = sets_[std::string(set.str(2))];
        for (const auto& light : set.list(7))
            if (light && light.type() == "igLightAttr") lights.push_back(light);
    }
}

void LightCtrlManager::clear_stage(Engine&) {
    rams_.clear();
    sets_.clear();
}

bool LightCtrlManager::command(Engine& engine, Ram&, std::string_view command, Packet& packet) {
    if (command != "SETCOLOR") return false;
    const auto found = sets_.find(packet.string(0));
    if (found == sets_.end() || packet.vectors.size() < 3) return true;
    for (const auto& light : found->second) {
        Attribute* attr = engine.scenes().edit_attribute(light);
        if (!attr) continue;
        const auto& a = packet.vectors[0];
        const auto& d = packet.vectors[1];
        const auto& s = packet.vectors[2];
        attr->ambient = {a.x, a.y, a.z, 1};
        attr->color = {d.x, d.y, d.z, 1};
        attr->specular = {s.x, s.y, s.z, 1};
    }
    return true;
}

// ---------------------------------------------------------------------------
bool PassiveManager::accepts(const ObjectView& info) const {
    const auto& t = info.type();
    return t.starts_with("be") || t == "ParticleInfo" || t == "igInfo" || t == "igAnimationDatabase";
}
void PassiveManager::add_info(Engine& engine, const ObjectView& info) {
    // Build metadata (beOptInfo, e.g. "beCameraOPT.ini" with its dates) and
    // the animation databases ACTION2 infos reference need no runtime.
    if (const auto& t = info.type(); t != "beOptInfo" && t != "igAnimationDatabase") engine.note("runtime for " + t);
    make_rams(engine, this, info, rams_, "");
}
void PassiveManager::clear_stage(Engine&) { rams_.clear(); }

// ---------------------------------------------------------------------------
bool GameSystem::script_call(Engine& engine, const std::string& method, std::span<const Lua4Value> args,
                             std::vector<Lua4Value>& out) {
    auto arg_int = [&](std::size_t i, int fallback = 0) {
        return i < args.size() && args[i].type() == Lua4Type::Number ? int(args[i].as_number()) : fallback;
    };
    // A DW4_CARD_RECORD test character is in place before the stage scripts
    // pick the player models.
    static const bool test_record = std::getenv("DW4_CARD_RECORD") != nullptr;
    if (test_record && !players[0].stats.ready && method.starts_with("SYS")) prepare_player(engine, 0);
    if (method == "SYSisJoinPlayer") {
        const int slot = arg_int(0);
        out = {Lua4Value::number(slot >= 0 && slot < 4 && players[std::size_t(slot)].joined ? 1 : 0)};
        return true;
    }
    if (method == "SYSgetPlayerName") {
        const int slot = std::clamp(arg_int(0), 0, 3);
        out = {Lua4Value::string(players[std::size_t(slot)].digimon)};
        return true;
    }
    if (method == "SYSgetWeaponName") {
        const int slot = std::clamp(arg_int(0), 0, 3);
        const int index = std::clamp(arg_int(1), 0, 2);
        const auto& weapon = players[std::size_t(slot)].weapons[std::size_t(index)];
        if (weapon.empty()) out = {};
        else out = {Lua4Value::string(weapon)};
        return true;
    }
    if (method == "SYSgetPlayerNinzu") {
        int n = 0;
        for (const auto& p : players) n += p.joined ? 1 : 0;
        out = {Lua4Value::number(n)};
        return true;
    }
    if (method == "SYSgetDifficulty") { out = {Lua4Value::number(difficulty)}; return true; }
    if (method == "PLAYERgetPos") {
        // 802EB4C4 -> 802EE1F0: PLAYERPOINT GETGENERATEPOS through the
        // messenger (80305F58, strings 80424D0C/80424D18), int 0 of the reply
        // (80306B00): the generate point SETGENERATEPOS chose before the stage
        // change, so first.lua picks the entrance (stagML12 point 3: the
        // transporter after the evolution scene).
        Packet packet;
        engine.messenger().send(engine, "PLAYERPOINT", std::nullopt, -1, "GETGENERATEPOS", packet);
        out = {Lua4Value::number(packet.ints.empty() ? 0 : packet.ints[0])};
        return true;
    }
    if (method == "SYSsetDifficulty") { difficulty = arg_int(0); return true; }
    if (method == "LISsetDifficultMax") { difficulty_max = arg_int(0); return true; }
    if (method == "SYSgetCameraMode") { out = {Lua4Value::number(camera_mode)}; return true; }
    if (method == "SYSctlPlayer") {
        // ("HP"|"MP", "GETHP"|"GETMHP"|"GETMP"|"GETMMP", player, 0): GAMERAM player
        // status. Values stay at the new-game defaults until GAMERAM exists.
        const std::string op = args.size() > 1 && args[1].type() == Lua4Type::String ? std::string(args[1].as_string()) : "";
        const int slot = std::clamp(arg_int(2), 0, 3);
        const auto& p = players[std::size_t(slot)];
        int value = 0;
        if (op == "GETHP") value = p.hp;
        else if (op == "GETMHP") value = p.max_hp;
        else if (op == "GETMP") value = p.mp;
        else if (op == "GETMMP") value = p.max_mp;
        else engine.note("SYSctlPlayer " + op);
        out = {Lua4Value::number(value)};
        return true;
    }
    // CTRLITEM folders (802EBF5C / 802EC064 / 802EC164 / 802EC26C).
    auto arg_str = [&](std::size_t i) {
        return i < args.size() && args[i].type() == Lua4Type::String ? std::string(args[i].as_string()) : std::string();
    };
    if (method == "SYSctlDeviceFolder") {
        out = {Lua4Value::number(control_item("DEVICE", arg_str(0), arg_int(1), arg_str(2), arg_int(3), arg_int(4), arg_int(5)))};
        return true;
    }
    if (method == "SYSctlDiskFolder") {
        out = {Lua4Value::number(control_item("DISK", arg_str(0), arg_int(1), {}, arg_int(2), 0, arg_int(3)))};
        return true;
    }
    if (method == "SYSctlFlagFolder") {
        out = {Lua4Value::number(control_item("FLAG", arg_str(0), 0, {}, arg_int(1), 0, 0))};
        return true;
    }
    if (method == "SYSctlBit") {
        out = {Lua4Value::number(control_item("BIT", arg_str(0), arg_int(1), {}, arg_int(2), 0, 0))};
        return true;
    }
    if (method == "SYSisGetShinkaDigimon") {
        // 802EC3E8: the player's BASE_ISUSE byte for a species.
        const auto& used = players[std::size_t(std::clamp(arg_int(0), 0, 3))].base_isuse;
        const auto found = used.find(arg_int(1));
        out = {Lua4Value::number(found != used.end() ? found->second : 0)};
        return true;
    }
    if (method == "SYSctlCardFolder") {
        // 802EC430: player 1's CARDLIST; KAZU counts owned cards, SET / CLR
        // succeed on a change.
        auto& cards = players[0].cards;
        const std::string op = arg_str(0);
        const int card = arg_int(1);
        int result = 0;
        if (op == "KAZU") {
            for (const auto& [id, owned] : cards) result += owned == 1 ? 1 : 0;
        } else if (op == "SET" || op == "CLR") {
            auto& owned = cards[card];
            if ((op == "SET") != (owned != 0)) {
                owned = op == "SET" ? 1 : 0;
                result = 1;
            }
        }
        out = {Lua4Value::number(result)};
        return true;
    }
    if (method == "SYSgetPlayerLevel") {
        out = {Lua4Value::number(players[std::size_t(std::clamp(arg_int(0), 0, 3))].stats.level)};
        return true;
    }
    if (method == "SYSgetPlayerDigimon") {
        out = {Lua4Value::number(players[std::size_t(std::clamp(arg_int(0), 0, 3))].stats.species)};
        return true;
    }
    if (method == "TXTgetCallNo") { out = {Lua4Value::number(text_call)}; return true; }
    if (method == "SYSgetLightMode") { out = {Lua4Value::number(light_mode)}; return true; }
    if (method == "SYSgetGateStageName") { out = {Lua4Value::string(gate_stage_name)}; return true; }
    if (method == "SYSgetGateStageNo") { out = {Lua4Value::number(gate_stage_no)}; return true; }
    if (method == "SYSsetGateStage") {
        if (!args.empty() && args[0].type() == Lua4Type::String) gate_stage_name = std::string(args[0].as_string());
        gate_stage_no = arg_int(1);
        return true;
    }
    (void)engine;
    return false;
}

// ---------------------------------------------------------------------------
// The managers in the plugins' registration order with the context
// (8028A268): the engine plugin 80318A24 (beSystem, beMessenger, beDBManager,
// beLua, beDemoManager, ...), then the game plugin 80346F74 (StageCtl,
// PanelWaza, Logo, Title2, ..., the model classes last). The order only
// decides which manager accepts an info and how the managers without an info
// follow; the updates run in the scheduler order, by first info of the stage
// (Engine::update_managers). The messenger and the demo manager run in
// Engine::step before these.
void register_default_managers(Engine& engine) {
    engine.add_manager(std::make_unique<GameSystem>());           // beSystem
    engine.add_manager(std::make_unique<DatabaseManager>());      // beDBManager
    engine.add_manager(std::make_unique<BkColorManager>());       // beBkColor
    engine.add_manager(std::make_unique<SoundManager>());         // beSound, beSeCtl, beMovie
    engine.add_manager(std::make_unique<LayerManager>());         // beLayerCtl
    engine.add_manager(std::make_unique<TextCtlManager>());       // beFont
    engine.add_manager(std::make_unique<SaveUtilManager>());      // beSaveUtil
    engine.add_manager(std::make_unique<WaterPlainManager>());    // beWaterPlain
    engine.add_manager(std::make_unique<ChangePosManager>());     // beChangePosTransform
    engine.add_manager(std::make_unique<HitLandManager>());       // beHitLandModel, beHitLandDeliv
    engine.add_manager(std::make_unique<SelectCtrlManager>());    // beSelectCtrl
    engine.add_manager(std::make_unique<MeterCtrlManager>());     // beMeterCtrl
    engine.add_manager(std::make_unique<ActionManager>());        // beAction2
    engine.add_manager(std::make_unique<WeaponManager>());        // beWeapon
    engine.add_manager(std::make_unique<CameraManager>());        // beCameraCtrl
    engine.add_manager(std::make_unique<ShadowManager>());        // beShadow01
    engine.add_manager(std::make_unique<GeneraterManager>());     // beGenerater
    engine.add_manager(std::make_unique<PlayerPointManager>());   // bePoint01
    engine.add_manager(std::make_unique<MatCtrlManager>());       // beMatCtrl
    engine.add_manager(std::make_unique<ActionStarterManager>()); // beActionStarter
    engine.add_manager(std::make_unique<ParticleCtrlManager>());  // beParticleCtrl2
    engine.add_manager(std::make_unique<SwitchCtrlManager>());    // beSwitchCtrl
    engine.add_manager(std::make_unique<LightCtrlManager>());     // beLightCtrl
    engine.add_manager(std::make_unique<NumberCtrlManager>());    // beNumberCtrl
    engine.add_manager(std::make_unique<TextureCtrlManager>());   // beTextureCtrl
    engine.add_manager(std::make_unique<SceneManager>());         // igSceneInfo (no update)
    engine.add_manager(std::make_unique<StageCtlManager>());      // beNDMWStageCtl
    engine.add_manager(std::make_unique<PanelManager>());         // beNDMWPanelWaza
    engine.add_manager(std::make_unique<LogoManager>());          // beNDMWLogo
    engine.add_manager(std::make_unique<Title2Manager>());        // beNDMWTitle2
    engine.add_manager(std::make_unique<LoadCtrl2Manager>());     // beNDMWLoadCtrl2
    engine.add_manager(std::make_unique<LoadIntf2Manager>());     // beNDMWLoadIntf2
    engine.add_manager(std::make_unique<SaveCtrlManager>());      // beNDMWSaveCtrl
    engine.add_manager(std::make_unique<SaveIntfManager>());      // beNDMWSaveIntf
    engine.add_manager(std::make_unique<SubWindowManager>());     // beNDMWStatus
    engine.add_manager(std::make_unique<ModelCtrlManager>());     // beNDMWMdl* model classes
    engine.add_manager(std::make_unique<PassiveManager>());       // infos without a runtime
}

} // namespace dw4::engine
