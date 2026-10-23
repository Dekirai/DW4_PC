#include "dw4/engine/managers.hpp"
#include "dw4/animation_math.hpp"
#include "dw4/engine/save_menu.hpp"
#include "dw4/engine/state_archive.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/sfd.hpp"

#include <algorithm>
#include <bit>
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

// 8031DE44 (re)starts a named timer: bias (+0x18/+0x1C), end (+0x20/+0x24),
// the remembered world time (+0x28/+0x2C) and one cached frame (+0x38,
// flt_80452BE8). The last time sample of 8031E4C4 (+0x30/+0x34) is never
// written: a new timer has 0 there (and so has a new NodeTimer), a restarted
// one keeps the sample of its last frame query. Only the ACTION 'FADE' timer
// is both restarted while it exists (FADESET 802F0800) and queried for
// frames (802F057C); CAMHOKAN (802F3264) and KEIKATIMER (8030A6FC, 8030C228)
// are only made when 8031E138 finds them missing, every other timer is only
// checked for expiry (8031E238).
void timer_start(NodeTimer& timer, std::int64_t world, std::int64_t end) {
    timer.bias = static_cast<std::int64_t>(std::uint64_t{0} - scaled_clock(world, timer.scale));
    timer.end = end;
    timer.saved_world = world;
    timer.cached_frames = 1;
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

// 0x802F317C..0x802F3250, once for the eye and once for the target: the
// live transform's translation is 8012D3F4 (t * (goal - saved) + saved, one
// fused multiply/add per axis) and its rotation the 8012B8A8 quaternions of
// the saved and the goal matrix blended by 8012C15C (a normalized linear
// blend, not a spherical one) and written back by 801299D8 (normalized,
// nine rotation entries; the translation stays).
void blend_transform(Mat4& live, const Mat4& saved, const Mat4& goal, float t) {
    const Vec3f a = translation_of(saved), b = translation_of(goal);
    set_translation(live, {std::fma(t, b.x - a.x, a.x), std::fma(t, b.y - a.y, a.y), std::fma(t, b.z - a.z, a.z)});
    try {
        const auto q = dw4::original_quaternion_nlerp(dw4::original_matrix_rotation(saved.m),
                                                      dw4::original_matrix_rotation(goal.m), t);
        dw4::original_normalized_rotation_matrix(q, live.m);
    } catch (const dw4::AnimationMathError&) {
        // A non-finite box transform keeps the live rotation.
    }
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
            blend_transform(eye->matrix, saved_eye_, box.eyes[k]->matrix, t);
            blend_transform(target->matrix, saved_target_, box.targets[k]->matrix, t);
            if (!ram.hokan) timer_start(ram.hokan.emplace(), engine.time());
            const float frames = timer_frames(*ram.hokan, engine.time());
            blend_ = centre_moved_ ? blend_ + frames : std::fma(1.5f, frames, blend_); // fadds / fmadds
        }
    }
    // DW4_TRACE_CAMERA: blend, boxes, moved flag, the players' centre and
    // the live transforms after the blend (the state at 0x802F32C8), float
    // bits, for the Dolphin box camera log (work/dref/fix_r16camera).
    if (static const bool trace = std::getenv("DW4_TRACE_CAMERA") != nullptr; trace) {
        auto bits = [](float v) { return std::bit_cast<std::uint32_t>(v); };
        std::printf("[%llu] camera %08x %d %d %d", static_cast<unsigned long long>(engine.frame()), bits(blend_), box_,
                    previous_box_, centre_moved_ ? 1 : 0);
        for (float v : {centre.x, centre.y, centre.z}) std::printf(" %08x", bits(v));
        for (const Node* node : {eye, target})
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 3; ++column) std::printf(" %08x", bits(node->matrix(row, column)));
        std::printf("\n");
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
        // 0x802F3474: when "SHAKEONE" ran out (8031E238 on the 2^18 ns
        // quantised clock, strict '<'; missing counts as run out) the next
        // digit comes (0 after the last) and the timer restarts with the step
        // frames (0x802F34C8); 0x802F34D4: "SHAKEALL" running out ends it.
        const std::int64_t now = engine.time();
        if (!shake.one || timer_expired(*shake.one, now)) {
            ++shake.index;
            if (shake.index >= shake.pattern.size()) shake.index = 0;
            if (!shake.one) shake.one.emplace();
            timer_start(*shake.one, now, std::int64_t(1.0e9f * shake.step / 60.0f));
        }
        if (timer_expired(shake.all, now)) {
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
        // 0x802F3DF4: 8031DE44 (re)starts "SHAKEALL" with (int64)(1e9f *
        // total / 60f); 0x802F3E00 removes "SHAKEONE", so the first update steps.
        timer_start(shake.all, engine.time(), std::int64_t(1.0e9f * shake.total / 60.0f));
        shake.one.reset();
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
// the layers, freezes the models and generators, holds the messenger's
// posted messages and, two frames later, returns to the title.
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
        messenger.pause(engine, true); // 0x80347EB8, until the stage teardown
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
    if (t == "beCriFxInfo") {
        // 802F51C4: the Cri appends every beCriFxInfo to its info list
        // (+0x2C); every stage's first is named REVERB, its _reverb (slot 6)
        // is the effect's bank << 8 | preset.
        fx_infos_.emplace_back(std::string(info.str(2)), info.i32(6));
        return;
    }
    if (t != "beSoundInfo") return;
    // The SOUND RAM's first update (80314C00 state 0) selects REVERB.
    fx_due_ = true;
    // The stage's sounds ([5] beSoundData) and the files they index ([7]).
    for (const auto& data : info.list(5))
        if (data)
            sounds_[std::string(data.str(2))] = {data.i16(4), data.i16(5), data.i16(8), std::uint32_t(data.i32(6)), data.i16(9)};
    files_.clear();
    for (const auto& file : info.list(7)) files_.emplace_back(file ? std::string(file.str(2)) : std::string());
    // The SOUND RAM init (80314AC8): [9] _baseVolume (0.1 dB, -30 in every
    // stage) goes to the Cri's +0x34 (80314B0C), the base of every stream
    // volume and fade (802F5828); [10] _sndfVolume (75) to the SNDF master
    // volume, set once (80314B18 -> 802F7244).
    base_volume_ = info.i16(9);
    set_sndf_volume(info.i16(10));
}
void SoundManager::clear_stage(Engine& engine) {
    // The Cri's stage end (802F523C): the effect of the first beCriFxInfo
    // if it were named NONE, in practice effects off (the reverb's delay
    // lines go); then the info list empties.
    select_effect(engine, "NONE");
    fx_infos_.clear();
    fx_due_ = false;
    // The streams keep playing into the next stage (its scripts stop or
    // replace them); only the stage's sound list goes.
    rams_.clear();
    sounds_.clear();
    files_.clear();
    // The per-stage hook 80314904 clears only the repeat guard list
    // (+0x28). The current BGM (+0x24) belongs to the beSound manager made
    // once by the engine plugin (80318A24 -> 80318EE8): it survives the
    // stage change, and only PLAY / BGMSTOP / VOLUMECTL write it.
    se_recent_.clear();
    movie_.reset();
    movie_stream_.decoder.reset();
    // The stage's banks go; one-shot effects finish, loops end.
    std::erase_if(banks_, [](const auto& bank) { return !bank->common; });
    std::erase_if(voices_, [](const SeVoice& voice) { return voice.loop; });
    tracks_.clear();
    se_sets_.clear();
}

// 803130D4 (the SE set's ACTION_BROADCAST handler): the set of the model's
// name (the last one of that name; no stage has two) sends the model
// MODELCTRL SETSOUND name/F<delay> for each of the action's sounds, in list
// order (80305344 / 80306FDC / 80306B28 / 80305A28), and plays nothing
// itself: the model queues a sound subcommand (8030DE1C) whose delay counts
// the system step from the next subcommand pass on (ModelCtrlManager::
// update_sounds), and the subcommand cleanup of the model's next sequence
// start fades a type 4 sound (803106C0).
void SoundManager::action_started(Engine& engine, const std::string& model, int index, std::string_view action) {
    const auto set = se_sets_.find(model);
    if (set == se_sets_.end()) return;
    for (const auto& cue : set->second) {
        if (cue.action != action) continue;
        Packet packet;
        packet.strings.emplace_back(cue.sound);
        packet.floats.push_back(float(cue.delay)); // s16 [4] as a float (800A3FDC)
        engine.messenger().send(engine, "MODELCTRL", model, index, "SETSOUND", packet);
    }
}

// 803105DC: SOUND PLAY of the subcommand's name (the repeat guard included);
// the reply carries the play id and the sound's type.
std::uint64_t SoundManager::play_model_sound(Engine& engine, const std::string& name, int& type) {
    const auto found = sounds_.find(name);
    type = found != sounds_.end() ? found->second.type : 0;
    return play(engine, name);
}

// 802F64F0: the BGM entry (Cri +0x20) holding the play id, else the
// ambience entry (+0x24), else every entry of the SE list (+0x28) holding
// it restarts its fade (8031D5D0) with the levels and frames.
void SoundManager::fade_id(std::uint64_t id, float from, float to, float frames) {
    if (!id) return;
    if (streams_[0].serial == id) return streams_[0].fade.start(from, to, frames);
    if (streams_[1].serial == id) return streams_[1].fade.start(from, to, frames);
    for (std::size_t channel = 2; channel < streams_.size(); ++channel)
        if (streams_[channel].serial == id) streams_[channel].fade.start(from, to, frames);
    // An sfp entry: the play id of its tracks and voices.
    const bool sounding = std::any_of(tracks_.begin(), tracks_.end(), [&](const SeTrack& t) { return t.id == id; }) ||
                          std::any_of(voices_.begin(), voices_.end(), [&](const SeVoice& v) { return v.id == id; });
    if (!sounding) return;
    auto fade = std::find_if(seq_fades_.begin(), seq_fades_.end(), [&](const SeqFade& f) { return f.id == id; });
    if (fade == seq_fades_.end()) fade = seq_fades_.insert(seq_fades_.end(), SeqFade{id});
    fade->fade.start(from, to, frames);
}

void SoundManager::stop_effects(std::uint64_t id) {
    std::erase_if(tracks_, [&](const SeTrack& track) { return !id || track.id == id; });
    std::erase_if(voices_, [&](const SeVoice& voice) { return !id || voice.id == id; });
    std::erase_if(seq_fades_, [&](const SeqFade& fade) { return !id || fade.id == id; });
}

// 802F53B8 for the sfp entries: an entry whose sequence still sounds steps
// a running fade (8031DDB8) and sets the level as its SEQ volume (8031CF94:
// SNDF command 4, taken to reach the voices already sounding); a fade to 0.0
// stops the entry once the volume is 12 or less (802F5624..802F5634). An
// entry whose sequence has ended is freed.
void SoundManager::step_seq_fades() {
    for (std::size_t i = 0; i < seq_fades_.size();) {
        auto& fade = seq_fades_[i];
        const std::uint64_t id = fade.id;
        const bool sounding = std::any_of(tracks_.begin(), tracks_.end(), [&](const SeTrack& t) { return t.id == id; }) ||
                              std::any_of(voices_.begin(), voices_.end(), [&](const SeVoice& v) { return v.id == id; });
        if (!sounding) {
            seq_fades_.erase(seq_fades_.begin() + std::ptrdiff_t(i));
            continue;
        }
        if (fade.fade.running) {
            fade.volume = fade.fade.step_seq();
            for (auto& voice : voices_)
                if (voice.id == id) voice.level = float(fade.volume) / 127.0f;
            if (fade.fade.to == 0.0f && fade.volume <= 12) {
                stop_effects(id);
                continue;
            }
        }
        ++i;
    }
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
// Engine time (ns) as the CRI model's CPU ticks (486 MHz).
std::int64_t cri_ticks(std::int64_t ns) { return ns / 1000 * 486 + ns % 1000 * 486 / 1000; }

// An ADX stream's output volume (ADXT_SetOutVol 8029A754: + the header's
// AINF volume, which no DW4 ADX file has; the renderer clamps it to
// -999..0, 802A6B8C, and the AX mixer input (MIXSetInput) is silent from
// -90.4 dB on).
float out_gain(int volume) {
    volume = std::clamp(volume, -999, 0);
    return volume <= -904 ? 0.0f : std::pow(10.0f, float(volume) / 200.0f);
}

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

int SoundManager::busy(int channel) const {
    const int status = cri_.status(channel);
    return status >= 1 && status <= 4 ? 1 : 0;
}

void SoundManager::pause_channel(Engine& engine, int channel, bool paused) {
    static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr;
    if (trace && cri_.active(channel) && cri_.handles[std::size_t(channel)].paused != paused)
        engine.log("stream " + std::string(paused ? "pause" : "resume") + " slot " + std::to_string(channel) + " frame " +
                   std::to_string(engine.frame()));
    cri_.pause(channel, paused, cri_ticks(engine.time()));
}

// 802F7244 (from the SOUND RAM init 80314B18 with beSoundInfo [10]
// _sndfVolume, 75 in every stage, and from SNDFVOLUME): only while the Cri's
// +0x38 is unset; a value outside 1..127 becomes 127.
void SoundManager::set_sndf_volume(int volume) {
    if (se_volume_) return;
    se_volume_ = volume <= 0 || volume > 127 ? 127 : volume;
}

// The SOUND PLAY handler (80314FC8; PLAYatPAUSE 80314FBC sets the Cri's
// pause flag +0x3C first, so the next sound that starts stays paused until
// PAUSEOFF). A BGM that is the RAM's current one (+0x24) is kept; a BGM or
// ambience whose channel still runs fades that out, and the request comes
// back as PLAY 11 frames later (the pause flag stays set meanwhile). A sound
// effect (type 0, 3 and above) requested again less than 49 999 998 ns of
// world time after its last start is ignored (803151B0..803152F0: the RAM's
// list +0x28 by the sound's index; the stage's sounds have distinct names).
std::uint64_t SoundManager::play(Engine& engine, const std::string& name) {
    const auto found = sounds_.find(name);
    if (found == sounds_.end()) {
        engine.note("audio PLAY " + name + " (not in the stage sound list)");
        return 0;
    }
    const SoundData data = found->second;
    if (data.type == 1 || data.type == 2) {
        if (data.type == 1 && !current_bgm_.empty() && current_bgm_ == name) return 0;
        const auto id = start_bgm_env(engine, name, data);
        if (!id) {
            if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace)
                engine.log("PLAY " + name + " again in 11 frames (channel busy), frame " + std::to_string(engine.frame()));
            Packet again;
            again.strings.emplace_back(name);
            engine.messenger().post(engine.time() + kFrameNs * 11, "SOUND", std::nullopt, -1, "PLAY", std::move(again));
            return 0;
        }
        if (data.type == 1) current_bgm_ = name;
        return id;
    }
    const std::int64_t now = engine.time();
    if (const auto recent = se_recent_.find(name); recent != se_recent_.end() && now < recent->second) return 0;
    const auto id = start_effect(engine, name, data);
    if (id) se_recent_[name] = now + 49999998; // 803152C4..803152D0: 0x2FB0000 - 0xF82
    return id;
}

// 802F5828: the BGM channel (Cri +0x20) or the ambience channel (+0x24).
std::uint64_t SoundManager::start_bgm_env(Engine& engine, const std::string& name, const SoundData& data) {
    if (data.request != 0xffffffffu) return 0;
    const int channel = data.type - 1;
    auto& stream = streams_[std::size_t(channel)];
    if (busy(channel) == 1) {
        // A sound still runs: it fades out (1.0 -> 0.0 in 10 frames, the
        // floats 8042543C / 80425438 / 80425440) unless it already fades.
        if (!stream.fade.running) stream.fade.start(1.0f, 0.0f, 10.0f);
        return 0;
    }
    return start_stream(engine, channel, name, data);
}

// 802F5A04 for a sound effect.
std::uint64_t SoundManager::start_effect(Engine& engine, const std::string& name, const SoundData& data) {
    if (data.request == 0xffffffffu) {
        // An ADX file (802F5A80) on one of the four SE stream handles.
        const int index = choose_se_channel(data.priority);
        if (index < 0) return 0;
        const int channel = 2 + int(index);
        const auto id = start_stream(engine, channel, name, data);
        if (id) streams_[std::size_t(channel)].priority = data.priority; // entry +34
        return id;
    }
    // An sfp request (802F5B64): a new play id when its sequence starts
    // (802F5F14: the Cri serial + 1 becomes the entry's).
    const std::uint64_t id = serial_ + 1;
    if (!play_request(engine, name, data, id)) return 0;
    serial_ = id;
    return id;
}

// 802F56B0 with ADX entries (the first four of the Cri's SE list +0x28,
// 802F66C8): a free entry, else the entry with the oldest serial among those
// whose priority does not exceed the sound's (the most recent sound of all
// is never replaced, the scan starts below the Cri's serial), stopped first.
int SoundManager::choose_se_channel(int priority) {
    for (std::size_t i = 0; i < kSeStreams; ++i)
        if (streams_[2 + i].serial == 0) return int(i);
    int best = -1;
    std::uint64_t oldest = serial_;
    int lowest = 0x7FFF;
    for (std::size_t i = 0; i < kSeStreams; ++i) {
        const auto& entry = streams_[2 + i];
        if (entry.priority <= priority && entry.serial < oldest && entry.priority <= lowest) {
            oldest = entry.serial;
            lowest = entry.priority;
            best = int(i);
        }
    }
    // (Without a candidate the original returns its caller's r31, the Cri
    // pointer, after stopping the entry that garbage index addresses; 802F5A04
    // then plays nothing, which is all the port keeps.)
    if (best >= 0) stop(2 + best);
    return best;
}

// The start on the chosen entry (802F58FC.. / 802F5AD4..): the sound's volume
// into the fade object (+12), ADXT_Pause on, ADXT_SetOutVol(volume + the Cri's
// base volume +0x34, 8031CF94), ADXT_StartAfs; then the pause flag +0x3C is
// used up (the sound stays paused) or the handle leaves its pause; the Cri's
// serial (+0x18) becomes the entry's.
std::uint64_t SoundManager::start_stream(Engine& engine, int channel, const std::string& name, const SoundData& data) {
    auto& stream = streams_[std::size_t(channel)];
    stream.volume = data.volume;
    pause_channel(engine, channel, true);
    stream.out_volume = data.volume + base_volume_;
    if (!open_stream(engine, stream, name, data)) return 0;
    if (pause_next_) pause_next_ = false;
    else pause_channel(engine, channel, false);
    stream.serial = ++serial_;
    stream.name = name;
    if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace)
        engine.log("stream start slot " + std::to_string(channel) + " " + name + " volume " + std::to_string(stream.out_volume) +
                   (cri_.handles[std::size_t(channel)].paused ? " (paused)" : "") + " frame " + std::to_string(engine.frame()));
    return stream.serial;
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
    // ADXT_StartAfs on the channel's handle: the stream is ready once its
    // first block came off the (simulated) disc and was decoded.
    cri_.start(int(&stream - streams_.data()), stream_file(engine, archive, entry, stream.decoder->header()));
    stream.phase = 1.0;
    stream.previous = stream.next = {};
    return true;
}

// A file the stage loader asks for (Engine::process_stage_loader): the igb
// directory's asynchronous read (8006FD34) goes through the loader's ADXSTM
// (8051B1D0), which reads the AFS entry from its first sector in requests of
// at most 64 sectors, one at a time (every capture: 64, 64, ..., the rest).
std::uint32_t SoundManager::read_entry(Engine& engine, unsigned archive, unsigned entry, std::uint32_t first, std::uint32_t count) {
    const auto [offset, size] = disc_extent(engine, archive, entry);
    const std::uint32_t sectors = (size + 2047) / 2048;
    if (first >= sectors) return 0;
    count = std::min(count, sectors - first);
    cri_.load(offset + std::uint64_t(first) * 2048, count);
    return count;
}

// Where an AFS entry lies on the disc ("[archive.entry]": the entry inside
// the archive file) and its size in bytes.
std::pair<std::uint64_t, std::uint32_t> SoundManager::disc_extent(Engine& engine, unsigned archive, unsigned entry) const {
    const auto& resources = engine.resources();
    std::uint64_t base = 0;
    if (const auto offset = resources.archive_disc_offset(archive)) {
        base = *offset;
    } else if (archive < resources.catalog().archives.size()) {
        // Without the disc's FST (a bare files/ folder): the GDJEB2 layout.
        std::string name = resources.catalog().archives[archive].filename;
        for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (name == "ndmw.afs") base = 0x2dbfb298;
        else if (name == "area.afs") base = 0x0f420298;
    }
    std::uint32_t size = 0;
    if (archive < resources.archives().size() && entry < resources.archives()[archive].entries().size()) {
        const auto& e = resources.archives()[archive].entries()[entry];
        base += e.offset;
        size = e.size;
    }
    return {base, size};
}

// The ADX file as the CRI stream reader sees it: where it lies on the disc,
// its size in sectors, header and loop sectors.
CriStreams::File SoundManager::stream_file(Engine& engine, unsigned archive, unsigned entry, const adx::Header& header) const {
    CriStreams::File file;
    const auto [offset, size] = disc_extent(engine, archive, entry);
    file.disc_offset = offset;
    file.sectors = (size + 2047) / 2048;
    file.header_bytes = header.data_offset;
    file.channels = header.channels;
    file.sample_rate = header.sample_rate;
    file.samples = header.samples;
    file.loop = header.loop;
    file.loop_start_sector = header.loop_begin_byte / 2048;
    file.loop_end_sector = (header.loop_end_byte + 2047) / 2048;
    return file;
}

// A sound-effect request (8031CCFC SEQ_SET/SEQ_PLAY): the bank holding the
// request's group (803C5188) runs its sequence on a track. The SEQ volume is
// 127 (802F5E94: SetOutVol 127; the sound's own volume [8] only goes into
// the entry), under the SNDF master volume (802F7244, 75 in the game). Once
// the bank is found the Cri's pause flag is used up (802F5E98), started or not.
void SoundManager::load_common_banks(Engine& engine) {
    if (common_loaded_) return;
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

bool SoundManager::play_request(Engine& engine, const std::string& name, const SoundData& data, std::uint64_t id) {
    load_common_banks(engine);
    std::string file = data.file >= 0 && data.file < int(files_.size()) ? files_[std::size_t(data.file)] : std::string();
    file = file.substr(file.find_last_of('/') + 1);
    for (auto& c : file) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::shared_ptr<SeBank> bank;
    for (const auto& candidate : banks_)
        if (candidate->bank->has_group(data.request) && (!bank || candidate->name == file)) bank = candidate;
    const auto sequence = bank ? bank->bank->sequence(data.request) : std::span<const std::uint8_t>{};
    if (bank) pause_next_ = false;
    if (sequence.empty()) {
        char id[16];
        std::snprintf(id, sizeof id, "%08X", data.request);
        engine.note("audio SE request " + std::string(id) + " " + name + " (no bank)");
        return false;
    }
    // (Every request takes a new entry, a type 4 sound that sounds already
    // too: 802F5DD4..802F5E94 have no such test.)
    if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace)
        engine.log("sound effect " + name + " from " + bank->name + " at frame " + std::to_string(engine.frame()));
    SeTrack track;
    track.bank = bank;
    track.sequence = sfp::Sequence(sequence);
    track.name = name;
    track.id = id;
    track.gain = float(se_volume_ ? se_volume_ : 127) / 127.0f;
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
        voice.id = track.id;
        // A note of an entry that fades starts at the entry's SEQ volume.
        for (const auto& fade : seq_fades_)
            if (fade.id == track.id) voice.level = float(fade.volume) / 127.0f;
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
        // The AUX A send (803BA5B4): the split's level 1..127 as
        // 10 * 20 log10(level / 127), truncated (0.1 dB), the voice's
        // AUXA_VOLUME articulator; 0 is off (-90.4 dB). MIX sends it after
        // the fader with the pan and input of the main bus (80259918: fader
        // + auxA + pan + span), so the send is the main level moved by that
        // much (every split of the game: fader index 15 = 0 dB, AUX A after
        // the fader, level 64 = -5.9 dB or 0). A split that takes the
        // channel's sends (+0x15, or a type 0 program) is never shipped and
        // stays dry here.
        if (!split->channel_aux && track.bank->bank->program_type() != 0 && split->aux_a) {
            const double ratio = double(float(split->aux_a) / 127.0f);
            const float aux = mix_level(int(10.0 * (20.0 * std::log10(ratio))));
            voice.aux_left = voice.left * aux;
            voice.aux_right = voice.right * aux;
        }
        voice.level_from = voice.level_to = voice.level; // (a new channel starts without a ramp)
        voices_.push_back(std::move(voice));
        if (voices_.size() > kMaxVoices) voices_.erase(voices_.begin());
    }
}

void SoundManager::mix_voices(float* mix, std::size_t frames, std::size_t at, float* aux) {
    for (auto& voice : voices_) {
        const auto& pcm = voice.bank->pcm[std::size_t(voice.sample)];
        const auto& sample = voice.bank->bank->sample(std::size_t(voice.sample));
        const std::size_t loop = voice.bank->loop_start[std::size_t(voice.sample)];
        const double step = double(sample.rate) * voice.pitch / double(kOutputRate);
        float* const sends = voice.aux_left != 0.0f || voice.aux_right != 0.0f ? aux : nullptr;
        for (std::size_t i = 0; i < frames && voice.position < pcm.size(); ++i) {
            std::size_t after = voice.position + 1;
            if (after >= pcm.size()) after = voice.loop && loop < pcm.size() ? loop : voice.position;
            const float a = float(pcm[voice.position]);
            const float level = voice.level_from + (voice.level_to - voice.level_from) * float(at + i) / float(kAxFrameOutput);
            const float value = (a + (float(pcm[after]) - a) * float(voice.phase)) * level;
            mix[i * 2] += value * voice.left;
            mix[i * 2 + 1] += value * voice.right;
            if (sends) {
                sends[i * 2] += value * voice.aux_left;
                sends[i * 2 + 1] += value * voice.aux_right;
            }
            voice.phase += step;
            while (voice.phase >= 1.0 && voice.position < pcm.size()) {
                voice.phase -= 1.0;
                if (++voice.position >= pcm.size() && voice.loop && loop < pcm.size()) voice.position = loop;
            }
        }
    }
    std::erase_if(voices_, [](const SeVoice& voice) { return voice.position >= voice.bank->pcm[std::size_t(voice.sample)].size(); });
}

// 802F5F98: the entry stops: serial, type and name cleared, the fade object
// reset (volume 0, levels 0.0), ADXT_SetOutVol(-999); 8031CE14 pauses the
// handle and stops it (ADXT_Stop: a read on the drive still finishes).
void SoundManager::stop(int channel) {
    auto& stream = streams_[std::size_t(channel)];
    stream.serial = 0;
    stream.name.clear();
    stream.fade = Fade{};
    stream.volume = 0;
    stream.out_volume = -999;
    stream.decoder.reset();
    cri_.handles[std::size_t(channel)].paused = true;
    cri_.stop(channel);
}

template <class F> void SoundManager::for_named(std::string_view name, F&& f) {
    if (streams_[0].name == name) return f(0);
    if (streams_[1].name == name) return f(1);
    for (std::size_t channel = 2; channel < streams_.size(); ++channel)
        if (streams_[channel].name == name) f(int(channel));
}

// 802F6FB0: the entry of that name is PLAYING (ADXT status 3, paused or not):
// the BGM entry, else the ambience's, else every running SE entry of that
// name (none: ready). "ALL": every running entry (serial set) of an ADX handle.
bool SoundManager::ready(std::string_view name) const {
    if (name == "ALL") {
        for (std::size_t channel = 0; channel < streams_.size(); ++channel)
            if (streams_[channel].serial && cri_.status(int(channel)) != 3) return false;
        return true;
    }
    if (streams_[0].name == name) return cri_.status(0) == 3;
    if (streams_[1].name == name) return cri_.status(1) == 3;
    for (std::size_t channel = 2; channel < streams_.size(); ++channel)
        if (!name.empty() && streams_[channel].name == name && streams_[channel].serial && cri_.status(int(channel)) != 3)
            return false;
    return true;
}

void SoundManager::render(std::int16_t* out, std::size_t frames) {
    std::vector<float> mix(frames * 2, 0.0f);
    for (std::size_t channel = 0; channel < streams_.size(); ++channel) {
        // The DSP plays a handle's ARAM ring from PLAYING until it played out
        // (status 3 and 4), not while the handle is paused.
        const auto& h = cri_.handles[channel];
        const int status = cri_.status(int(channel));
        if (streams_[channel].serial && (status == 3 || status == 4) && !h.paused)
            mix_stream(streams_[channel], out_gain(streams_[channel].out_volume), false, mix.data(), frames);
    }
    // The movie's sound has no base volume; it plays once Sofdec unpaused it.
    // Neither the streams nor the movie send to AUX A (the RNA's AUX levels
    // stay at -99.9 dB, 802A7B5C).
    if (movie_ && movie_->sound_on) mix_stream(movie_stream_, 1.0f, true, mix.data(), frames);
    // The effect voices and the AUX A bus, AX frame by AX frame.
    for (std::size_t done = 0; done < frames;) {
        if (ax_fill_ == 0) begin_ax_frame();
        const std::size_t count = std::min(frames - done, kAxFrameOutput - ax_fill_);
        float* const part = mix.data() + done * 2;
        mix_voices(part, count, ax_fill_, aux_emit_ ? aux_sends_.data() + ax_fill_ * 2 : nullptr);
        if (aux_emit_)
            for (std::size_t i = 0; i < count * 2; ++i) part[i] += aux_return_[ax_fill_ * 2 + i];
        ax_fill_ += count;
        done += count;
        if (ax_fill_ == kAxFrameOutput) {
            end_ax_frame();
            ax_fill_ = 0;
        }
    }
    // The AX mix has no compressor: the output saturates at 16 bits (the
    // DSP's saturating accumulator; Dolphin's dumps keep peaks up to 32729
    // unchanged and clip only sums above full scale).
    for (std::size_t i = 0; i < frames * 2; ++i) out[i] = static_cast<std::int16_t>(std::clamp(mix[i], -32768.0f, 32767.0f));
}

// One stream resampled into the mix (audio only: the game never reads the
// decoder; a one-shot that runs out just falls silent).
void SoundManager::mix_stream(Stream& stream, float gain, bool sofdec, float* mix, std::size_t frames) {
    if (!stream.decoder) return;
    const auto& header = stream.decoder->header();
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
    const double step = rna_rate(header.sample_rate, sofdec) / double(kOutputRate);
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

// 802F6F10 (the SOUND RAM's first update 80314E78 with "REVERB", the Cri's
// stage end 802F523C with "NONE"): only the first info of the Cri's list
// counts; its _reverb goes to the SNDF as 0xA470hh00 (bank hh, SNDF +0x984)
// and 0xA400ll00 (preset ll, 803BC448 -> 803BF698), 0x7F turns the effects
// off. The SNDF runs the commands at its next tick, here at once.
void SoundManager::select_effect(Engine& engine, std::string_view name) {
    int value = 0x7f;
    if (!fx_infos_.empty() && fx_infos_.front().first == name) value = fx_infos_.front().second;
    fx_bank_select_ = std::uint8_t((value >> 8) & 0x7f);
    sndf_effect(engine, fx_bank_select_, value & 0x7f);
}

// 803BF698: bank 0xFF only with preset 0x7F, preset 0xFF does nothing,
// preset 0x7F shuts both AUX effects down. Otherwise the bank with that id
// (allse.sfp is bank 0) and a preset it has, unless that pair is already set
// up: the running AUX A / B effects shut down and lose their callbacks, the
// preset's effects start (AXFXReverbStdInit with time, pre-delay, damping,
// coloration and mix, each word / 1000) and AUX A's callback is registered.
// (The original first checks the effect's memory, 0xCD38 + 0x180 per ms of
// pre-delay, against its heap; the port always has it.)
void SoundManager::sndf_effect(Engine& engine, int bank, int preset) {
    bank &= 0xff;
    preset &= 0xff;
    if ((bank == 0xff && preset != 0x7f) || preset == 0xff) return;
    static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr;
    auto shut_down = [&] {
        if (fx_type_a_ == 0) reverb_a_.shutdown();
        aux_a_callback_ = false; // AXRegisterAuxACallback(NULL)
        // (AUX B never has an effect: no preset of the game uses it.)
    };
    if (preset == 0x7f) {
        if (trace && fx_type_a_ != 0xff) engine.log("sound effects off, frame " + std::to_string(engine.frame()));
        shut_down();
        fx_type_a_ = fx_type_b_ = fx_bank_ = fx_preset_ = 0xff;
        return;
    }
    load_common_banks(engine);
    const SeBank* found = nullptr;
    for (const auto& candidate : banks_)
        if (candidate->bank->id() == bank) {
            found = candidate.get();
            break;
        }
    if (!found || (fx_bank_ == bank && fx_preset_ == preset)) return;
    const auto effect = found->bank->effect(preset);
    if (!effect) return;
    shut_down();
    fx_type_a_ = effect->type_a;
    fx_type_b_ = effect->type_b;
    fx_bank_ = std::uint8_t(bank);
    fx_preset_ = std::uint8_t(preset);
    if (fx_type_a_ == 0 && effect->a.size() == 5) {
        const auto param = [&](std::size_t i) { return float(effect->a[i]) / 1000.0f; };
        aux_a_callback_ = reverb_a_.init(param(3), param(0), param(4), param(2), param(1));
        if (trace)
            engine.log("sound effect AUX A ReverbStd (bank " + std::to_string(bank) + " preset " + std::to_string(preset) + ": time " +
                       std::to_string(param(0)) + " pre-delay " + std::to_string(param(1)) + " damping " +
                       std::to_string(param(2)) + " coloration " + std::to_string(param(3)) + " mix " + std::to_string(param(4)) +
                       (aux_a_callback_ ? "" : ", rejected") + "), frame " + std::to_string(engine.frame()));
    } else if (fx_type_a_ != 0xff) {
        engine.note("audio AUX A effect type " + std::to_string(fx_type_a_) + " (not implemented)");
    }
    if (fx_type_b_ != 0xff) engine.note("audio AUX B effect type " + std::to_string(fx_type_b_) + " (not implemented)");
}

// The AX frame interrupt: the AUX A callback runs and the buffer the DSP adds
// in this frame (two frames old) is resampled from 32 kHz to the output rate,
// linearly from the last returned sample (a third of a DSP sample late).
void SoundManager::begin_ax_frame() {
    // MIXUpdateSettings (8025A530, the AX frame callback): a channel whose
    // input changed (the SEQ volume of a VOLUMECTL fade reaches the voice's
    // volume articulator, 803B8F28 -> AXART 802450BC -> MIX) keeps its old
    // level as the current one and gives the DSP the new one as a ramp,
    // (new - current) / 160 per sample with the ramp flags (8025B138..: PB
    // VE +0x19C / +0x19E, the bus volumes and their deltas), so a fade step
    // reaches the output at the next AX frame, linearly across it.
    for (auto& voice : voices_) {
        voice.level_from = voice.level_to;
        voice.level_to = voice.level;
    }
    const auto* back = aux_a_.begin_frame(aux_a_callback_ ? &reverb_a_ : nullptr);
    aux_emit_ = back != nullptr;
    aux_sends_.fill(0.0f);
    if (!back) {
        aux_return_.fill(0.0f);
        aux_last_ = {};
        return;
    }
    constexpr std::size_t n = axfx::kFrameSamples;
    for (std::size_t c = 0; c < 2; ++c) {
        const std::int32_t* in = back->data() + c * n;
        auto at = [&](std::size_t j) { return j == 0 ? aux_last_[c] : float(in[j - 1]); };
        for (std::size_t i = 0; i < kAxFrameOutput; ++i) {
            const std::size_t position = (i + 1) * 2; // in thirds of a DSP sample
            const std::size_t j = position / 3;
            const float a = at(j);
            aux_return_[i * 2 + c] = position % 3 == 0 ? a : a + (at(j + 1) - a) * float(position % 3) / 3.0f;
        }
        aux_last_[c] = float(in[n - 1]);
    }
}

// The end of the AX frame: the DSP stores the frame's AUX A sends (32 kHz
// from the output rate, every second sample between two).
void SoundManager::end_ax_frame() {
    if (!aux_emit_) return;
    std::array<std::int32_t, 2 * axfx::kFrameSamples> sends{};
    for (std::size_t j = 0; j < axfx::kFrameSamples; ++j)
        for (std::size_t c = 0; c < 2; ++c) {
            const std::size_t i = j * 3 / 2;
            const float value = j % 2 == 0 ? aux_sends_[i * 2 + c] : (aux_sends_[i * 2 + c] + aux_sends_[(i + 1) * 2 + c]) * 0.5f;
            sends[j * 2 + c] = std::int32_t(std::floor(value + 0.5f));
        }
    aux_a_.end_frame(sends);
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
            movie_stream_.phase = 1.0;
            movie_stream_.previous = movie_stream_.next = {};
        }
    } catch (const std::exception& error) {
        engine.note("movie " + name + ": " + error.what());
    }
    // Sofdec (MW player) starts in its prep state: 803E3D1C creates the
    // movie's ADXT paused (the reference captures' ADXT_Pause log: pause 1 at
    // MOVIE PLAY, status PREP), and the MW server leaves it (803E38C4 /
    // 803E374C: ADXT_Pause 0) only once its buffers are filled, in the
    // update 29.0 fields after MOVIE PLAY (MOVIE PLAY's update, the 8-field
    // update mwPlyStart takes, ten 2-field updates of the 30 Hz movie loop;
    // the same in every capture, the drive model being deterministic). The
    // first picture follows four updates (8 fields) later, the others every
    // second field (the captures' XFB: picture k at MOVIE PLAY + 37 + 2k).
    const std::int64_t now = engine.time();
    movie.sound_start = now + kMovieSoundNs;
    movie.start = now + kMoviePictureNs;
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
        if (command == "PLAY" || command == "PLAYatPAUSE") {
            if (command == "PLAYatPAUSE") pause_next_ = true; // 80314FBC: Cri +0x3C
            play(engine, std::string(packet.string(0)));
            return true;
        }
        if (command == "PAUSEOFF") {
            // 802F61F8: the entry of that name leaves its pause.
            for_named(packet.string(0), [&](int channel) { pause_channel(engine, channel, false); });
            return true;
        }
        if (command == "VOLUMECTL2") {
            // name/F<from>/F<to>/F<frames> (80315500, at least three floats):
            // 802F6334 starts a fade of the entry of that name (8031D5D0).
            if (packet.floats.size() >= 3)
                for_named(packet.string(0), [&](int channel) {
                    streams_[std::size_t(channel)].fade.start(packet.real(0, 1.0f), packet.real(1, 1.0f), packet.real(2, 0.0f));
                });
            return true;
        }
        // 802F60FC: BGMSTOP (1) also clears the RAM's current BGM (+0x24);
        // SESTOP (4) stops every sound effect entry, whatever it names.
        if (command == "BGMSTOP") {
            stop(0);
            current_bgm_.clear();
            return true;
        }
        if (command == "ENVSTOP") { stop(1); return true; }
        if (command == "SESTOP") {
            for (std::size_t channel = 2; channel < streams_.size(); ++channel) stop(int(channel));
            stop_effects(0);
            return true;
        }
        if (command == "VOLUMECTL") {
            // 80315340: UP fades 0.0 -> 1.0, DOWN 1.0 -> 0.0 over 10 frames
            // (80427088..80427090): with a play id the entries holding it
            // (802F64F0), else the entries of the name in string 1 (802F6334),
            // and a current BGM (+0x24) of that name is forgotten. Only a
            // model's sound subcommand sends it (its end call, 803106C0).
            const auto mode = packet.string(0);
            const bool up = mode == "UP", down = mode == "DOWN";
            if (!packet.longs.empty()) {
                if (up || down) fade_id(std::uint64_t(packet.longs[0]), up ? 0.0f : 1.0f, up ? 1.0f : 0.0f, 10.0f);
            } else if (packet.strings.size() > 1) {
                const std::string name(packet.string(1));
                if (up || down)
                    for_named(name, [&](int channel) { streams_[std::size_t(channel)].fade.start(up ? 0.0f : 1.0f, up ? 1.0f : 0.0f, 10.0f); });
                if (!current_bgm_.empty() && current_bgm_ == name) current_bgm_.clear();
            }
            return true;
        }
        if (command == "STOP") {
            // 80314F80: the entries holding that play id (802F6038).
            if (!packet.longs.empty()) {
                const auto id = std::uint64_t(packet.longs[0]);
                if (id && (streams_[0].serial == id || streams_[1].serial == id)) stop(streams_[0].serial == id ? 0 : 1);
                else if (id)
                    for (std::size_t channel = 2; channel < streams_.size(); ++channel)
                        if (streams_[channel].serial == id) stop(int(channel));
            }
            return true;
        }
        if (command == "SNDFVOLUME") {
            if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace)
                engine.log("SNDFVOLUME " + std::to_string(packet.integer(0, 0)));
            set_sndf_volume(packet.integer(0, 0));
            return true;
        }
    }
    // 803156B8: ISREADY <name> and ISALLREADY (no name: "ALL"); ISBGMREADY /
    // ISENVREADY ask for the name the BGM / ambience entry holds.
    if (command == "ISREADY" || command == "ISALLREADY") {
        const std::string_view name = packet.strings.empty() ? std::string_view("ALL") : packet.string(0);
        const bool result = ready(name);
        if (static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr; trace) {
            static std::string last;
            const std::string line = std::string(command) + " " + std::string(name) + " -> " + (result ? "1" : "0");
            if (line != last) engine.log(line + " frame " + std::to_string(engine.frame()));
            last = line;
        }
        packet.bools.push_back(result ? 1 : 0);
        return true;
    }
    if (command == "ISBGMREADY" || command == "ISENVREADY") {
        packet.bools.push_back(ready(streams_[command == "ISBGMREADY" ? 0 : 1].name) ? 1 : 0);
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
// The stream entries a save state keeps (everything the game sees, and the
// decoding position, so the music goes on where it was).
struct StreamState {
    std::string name, file;
    std::uint64_t frames_read{}, serial{};
    int priority{}, volume{}, out_volume{-999};
    SoundManager::Fade fade;
};
struct SoundState {
    std::vector<StreamState> streams;
    CriStreams cri;
    std::string current_bgm;
    std::uint64_t serial{};
    bool pause_next{};
    int se_volume{}, base_volume{};
    std::uint32_t random{};
};
} // namespace

std::shared_ptr<const void> SoundManager::persistent() const {
    auto state = std::make_shared<SoundState>();
    for (const auto& stream : streams_) {
        StreamState s;
        s.name = stream.name;
        if (stream.serial) {
            s.file = stream.file;
            s.frames_read = stream.frames_read;
        }
        s.serial = stream.serial;
        s.priority = stream.priority;
        s.volume = stream.volume;
        s.out_volume = stream.out_volume;
        s.fade = stream.fade;
        state->streams.push_back(std::move(s));
    }
    state->current_bgm = current_bgm_;
    state->serial = serial_;
    state->pause_next = pause_next_;
    state->se_volume = se_volume_;
    state->base_volume = base_volume_;
    state->random = random_;
    state->cri = cri_;
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
                    stream.file = s.file;
                    stream.frames_read = s.frames_read - left;
                }
            } catch (const std::exception&) {
                stream.decoder.reset();
            }
        }
        stream.name = s.name;
        stream.serial = s.serial;
        stream.priority = s.priority;
        stream.volume = s.volume;
        stream.out_volume = s.out_volume;
        stream.fade = s.fade;
    }
    current_bgm_ = state.current_bgm;
    serial_ = state.serial;
    pause_next_ = state.pause_next;
    se_volume_ = state.se_volume;
    base_volume_ = state.base_volume;
    random_ = state.random;
    cri_ = state.cri;
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
        a.io(s.serial);
        a.io(s.priority);
        a.io(s.volume);
        a.io(s.out_volume);
        a.io(s.fade.running);
        a.io(s.fade.from);
        a.io(s.fade.to);
        a.io(s.fade.position);
        a.io(s.fade.increment);
    }
    a.io(state->current_bgm);
    a.io(state->serial);
    a.io(state->pause_next);
    a.io(state->se_volume);
    a.io(state->base_volume);
    a.io(state->random);
    auto& cri = state->cri;
    a.io(cri.drive.buffer_start_offset);
    a.io(cri.drive.buffer_end_offset);
    a.io(cri.drive.buffer_start_time);
    a.io(cri.drive.buffer_end_time);
    a.io(cri.inflight.slot);
    a.io(cri.inflight.done);
    for (auto& h : cri.handles) {
        a.io(h.file.disc_offset);
        a.io(h.file.sectors);
        a.io(h.file.header_bytes);
        a.io(h.file.channels);
        a.io(h.file.sample_rate);
        a.io(h.file.samples);
        a.io(h.file.loop);
        a.io(h.file.loop_start_sector);
        a.io(h.file.loop_end_sector);
        a.io(h.active);
        a.io(h.bound);
        a.io(h.stopping);
        a.io(h.busy);
        a.io(h.done);
        a.io(h.paused);
        a.io(h.first_block);
        a.io(h.decoding);
        a.io(h.state);
        a.io(h.limit);
        a.io(h.pos);
        a.io(h.request);
        a.io(h.write_pos);
        a.io(h.written);
        a.io(h.consumed);
        a.io(h.decoded);
        a.io(h.sj);
        a.io(h.moved);
        a.io(h.reclaimed);
        a.io(h.dsp_start);
        a.io(h.played_base);
    }
    a.io(cri.loader.active);
    a.io(cri.loader.busy);
    a.io(cri.loader.done);
    a.io(cri.loader.booked);
    a.io(cri.loader.disc_offset);
    a.io(cri.loader.sectors);
    a.io(cri.loader.pos);
    a.io(cri.loader.request);
    if (a.reading() && (cri.inflight.slot < -1 || cri.inflight.slot > CriStreams::kLoaderSlot))
        throw std::runtime_error("save state holds an invalid stream read");
    // (A decoding handle divides by its channel count, 18 bytes per frame.)
    if (a.reading())
        for (const auto& h : cri.handles)
            if (h.active && (h.file.channels < 1 || h.file.channels > 8 || h.state > 5))
                throw std::runtime_error("save state holds an invalid stream handle");
    if (a.reading() && (cri.loader.pos > cri.loader.sectors || cri.loader.request > 64))
        throw std::runtime_error("save state holds an invalid loader read");
    if (a.reading()) opaque = state;
}

// The driver side (beCri, a module outside the managers' gate): it keeps
// running during stage loads.
void SoundManager::update_driver(Engine& engine) {
    // One field of the CRI servers: the stream reads on the simulated drive,
    // decoding, PLAYING (disc_timing.hpp). The port's driver tick follows the
    // frame's game update, as the original's servers follow the previous one.
    static const bool trace = std::getenv("DW4_TRACE_SOUND") != nullptr;
    // Test tool: DW4_DISC_CLOCK=<ticks> replaces the drive's Dolphin clock
    // offset (DiscDrive::kDolphinClockOffset), to line a run up with a capture
    // whose world clock stands elsewhere.
    static const char* disc_clock = std::getenv("DW4_DISC_CLOCK");
    if (disc_clock && *disc_clock) cri_.drive.clock_offset = std::strtoll(disc_clock, nullptr, 10);
    // Test tool: DW4_AX_PHASE=<ticks> moves the DSP's frame grid
    // (CriStreams::kAxPhase) to line a run up with a capture.
    static const char* ax_phase = std::getenv("DW4_AX_PHASE");
    if (ax_phase && *ax_phase) cri_.ax_phase = std::strtoll(ax_phase, nullptr, 10);
    const int inflight_before = cri_.inflight.slot;
    const std::int64_t done_before = cri_.inflight.done;
    std::array<bool, CriStreams::kSlots> playing_before{};
    for (int slot = 0; slot < CriStreams::kSlots; ++slot) playing_before[std::size_t(slot)] = cri_.playing(slot);
    // Fast loading (a PC option, latched by the running stage's load): the
    // drive's reads take no time.
    cri_.drive.instant = engine.fast_loading();
    cri_.tick(cri_ticks(engine.time()));
    if (trace) trace_cri(engine, inflight_before, done_before, playing_before);
    // 802F53B8 over every entry holding a sound (serial set): a running one
    // (8031D0F0 == 1) steps its fade while the handle is not paused (8031D33C)
    // and sets the output volume; a fade to 0.0 stops the entry once the
    // volume is -21 dB or less. An entry that stopped or played out (status 0
    // or 5) stops at once.
    for (std::size_t channel = 0; channel < streams_.size(); ++channel) {
        auto& stream = streams_[channel];
        if (!stream.serial) continue;
        if (busy(int(channel)) != 1) {
            if (trace)
                engine.log("stream " + stream.name + " ended (status " + std::to_string(cri_.status(int(channel))) + "), frame " +
                           std::to_string(engine.frame()));
            stop(int(channel));
            continue;
        }
        if (cri_.handles[channel].paused || !stream.fade.running) continue;
        stream.out_volume = stream.fade.step(stream.volume, base_volume_);
        if (stream.fade.to == 0.0f && stream.out_volume <= -210) {
            if (trace)
                engine.log("stream fade-out of " + stream.name + " (volume " + std::to_string(stream.volume) + ") stops, frame " +
                           std::to_string(engine.frame()));
            stop(int(channel));
        }
    }
    step_seq_fades();
    for (auto& track : tracks_) {
        track.wait_ms -= double(engine.frame_delta()) / 1e6; // OSGetTick milliseconds of one field
        run_track(track);
    }
    std::erase_if(tracks_, [](const SeTrack& track) { return track.sequence.finished(); });
}

void SoundManager::trace_cri(Engine& engine, int inflight_before, std::int64_t done_before,
                             const std::array<bool, CriStreams::kSlots>& playing_before) {
    if (cri_.inflight.slot >= 0 && (inflight_before < 0 || cri_.inflight.done != done_before)) {
        const bool loader = cri_.inflight.slot == CriStreams::kLoaderSlot;
        const auto request = loader ? cri_.loader.request : cri_.handles[std::size_t(cri_.inflight.slot)].request;
        const auto offset = loader ? cri_.loader.disc_offset + std::uint64_t(cri_.loader.pos) * 2048
                                   : cri_.handles[std::size_t(cri_.inflight.slot)].file.disc_offset +
                                         std::uint64_t(cri_.handles[std::size_t(cri_.inflight.slot)].pos) * 2048;
        const std::int64_t now = cri_ticks(engine.time());
        char line[160];
        std::snprintf(line, sizeof line, "stream read %s %d: %u sectors at disc 0x%llx, %.2f fields, frame %llu", loader ? "loader" : "slot",
                      cri_.inflight.slot, request, static_cast<unsigned long long>(offset),
                      double(cri_.inflight.done - now) / double(CriStreams::kFieldTicks), static_cast<unsigned long long>(engine.frame()));
        engine.log(line);
        static const bool ring = std::getenv("DW4_TRACE_CRI_RING") != nullptr;
        if (ring && !loader) {
            const auto& h = cri_.handles[std::size_t(cri_.inflight.slot)];
            std::snprintf(line, sizeof line, "  ring: written %llu consumed %llu (used %llu) write_pos %u decoded %llu sj %u aram %llu",
                          static_cast<unsigned long long>(h.written), static_cast<unsigned long long>(h.consumed),
                          static_cast<unsigned long long>(h.written - h.consumed), h.write_pos,
                          static_cast<unsigned long long>(h.decoded), h.sj, static_cast<unsigned long long>(h.moved - h.reclaimed));
            engine.log(line);
        }
    }
    // DW4_TRACE_CRI_RING also logs the renderer's view of the stereo voices
    // each field (compare with a capture's renderer reads).
    static const bool ring_play = std::getenv("DW4_TRACE_CRI_RING") != nullptr;
    if (ring_play)
        for (int slot = 0; slot < 2; ++slot)
            if (cri_.active(slot) && cri_.handles[std::size_t(slot)].dsp_start >= 0) {
                char line[96];
                std::snprintf(line, sizeof line, "  play slot %d: ring position %llu, frame %llu", slot,
                              static_cast<unsigned long long>(cri_.ring_position(slot, cri_ticks(engine.time())) % 4096),
                              static_cast<unsigned long long>(engine.frame()));
                engine.log(line);
            }
    for (int slot = 0; slot < CriStreams::kSlots; ++slot)
        if (cri_.playing(slot) && !playing_before[std::size_t(slot)])
            engine.log("stream PLAYING slot " + std::to_string(slot) +
                       (slot < int(streams_.size()) ? " " + streams_[std::size_t(slot)].name : std::string()) +
                       " frame " + std::to_string(engine.frame()));
}

void SoundManager::update(Engine& engine) {
    update_driver(engine);
    if (fx_due_) {
        fx_due_ = false;
        select_effect(engine, "REVERB");
    }
    if (movie_) {
        // (Half a field early: the world clock's rounding.)
        if (!movie_->sound_on && engine.time() >= movie_->sound_start - kFrameNs / 2) movie_->sound_on = true;
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
        bind->sampler = texture_sampler(texture);
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
    // source model transform (HITLAND_TRAVERSAL.md); it stays out of the
    // static world and is met where its model is at query time.
    const auto kind = std::uint32_t(info.i32(10));
    if (kind & 0x400u) {
        // Each check-switch child is a beCopyTransform following its model
        // instance ([12]), e.g. one per box of g_box; child i is enabled by
        // bit i of the check switch's mask (update resolves where it lives).
        const auto check = info.ref(8);
        const auto children = check && check.is_a("igGroup") ? check.list(7) : std::vector<ObjectView>{check};
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto& child = children[i];
            if (!child) continue;
            followers_.push_back({world_.add_dynamic(child, kind, info_index, std::uint16_t(i)), child.ref(12), check,
                                  std::uint32_t(i), std::string(info.str(2))});
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
    // ONHIT (0x802FE584) / OFFHIT (0x802FE6A0) look the message's name up in
    // the info's name list [9] and set (802066A8) / clear (80206714) that
    // child's bit of the check switch mask; here each named child is its own
    // row and group. Kind 0x400 infos author empty name lists (igStringObjList
    // in g_box and every other one of the survey stages), so these commands
    // never touch their children: those follow the ACTION bits (update).
    for (const auto& child : children_)
        if (child.get() == &base) world_.set_group_enabled(child->group, command == "ONHIT");
    return true;
}

void HitLandManager::update(Engine& engine) {
    // The copy transforms take their model's matrix (80128CF4) when a query
    // runs (803009D0 at 0x80300CE0), so the world keeps the model node and
    // reads its matrix then, not here before ModelCtrl moves it. The same
    // pass reads the child's bit of the check switch mask (0x80300CA0) and
    // nothing else: the check switch of every kind 0x400 info shares its bit
    // mask object with the action switch of its model (g_box: igSwitch #12210
    // and the ACTION switch #12637 both hold mask object #12514 in dngq7b1e),
    // and ACTION sets bit `RAM index` when an action starts (0x802F1EB4 /
    // 0x802F20E8) and clears it on NDSP (0x802F1C58), which REMOVE sends
    // (0x8030DC54), and on FADESET HIDE (0x802F0898). A box that breaks
    // (SQ_DDMG DEAD01, SQ_ERAS, REMOVE) loses its collision inside that
    // model's update, a hidden dng310f3 fade scaffold (OFSW01) carries no one
    // and a model not shown yet has none. So the world reads the action
    // switch node's mask at query time.
    auto* actions = engine.get<ActionManager>();
    for (auto& follower : followers_) {
        if (follower.resolved) continue;
        const Node* node = engine.scenes().node(follower.source);
        const Node* mask = actions ? actions->mask_switch(follower.check) : nullptr;
        // Until an action switch on the same mask is known (its info may come
        // later), the check switch's own authored mask (all clear in the data).
        follower.resolved = mask != nullptr;
        if (!mask) mask = engine.scenes().node(follower.check);
        world_.set_dynamic(follower.id, node, mask, follower.bit);
    }
    if (static const bool trace = std::getenv("DW4_TRACE_HITLAND") != nullptr; trace && engine.frame() % 300 == 0) {
        auto* actions_now = engine.get<ActionManager>();
        for (const auto& follower : followers_) {
            const Node* node = engine.scenes().node(follower.source);
            const Node* mask = actions_now ? actions_now->mask_switch(follower.check) : nullptr;
            if (!mask) mask = engine.scenes().node(follower.check);
            if (!node || !mask || !mask->child_enabled(follower.bit)) continue;
            engine.log("hitland " + follower.info + " follows (" + std::to_string(node->matrix(3, 0)) + ", " +
                       std::to_string(node->matrix(3, 1)) + ", " + std::to_string(node->matrix(3, 2)) + ")");
        }
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
    rams_.clear();
    children_.clear();
    world_.clear();
}

// ---------------------------------------------------------------------------
void ParticleCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    for (auto* ram : make_rams(engine, this, info, rams_, "MODELCTRL")) {
        ram->transform = engine.scenes().node(ram->ram.ref(11));
        ram->timer = engine.scenes().node(ram->ram.ref(12));
        // 80312190: the instances start stopped, their timers too (+0x2C).
        if (ram->timer) {
            ram->timer->timer.running = false;
            ram->timer->particle_live = false;
        }
    }
}

void ParticleCtrlManager::clear_stage(Engine&) { rams_.clear(); }

void ParticleCtrlManager::stop(ParticleRam& ram) {
    // 803123DC: the timer stops (_timeRunning 0), the instance's switch bit
    // goes and the outer switch with the last one.
    ram.running = false;
    ram.owner.clear();
    if (ram.timer) {
        ram.timer->timer.running = false;
        ram.timer->particle_live = false;
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
        // 0x80312900..0x80312934: a running instance (+0x14) sets its timer's
        // _timeRunning to 0 / 1, nothing else. The effect time stands from the
        // timer's next traversal on (its last visit's time stays) because the
        // visits move _timeBias along, which also holds back the lifetime
        // test; a frozen timer the traversal does not reach keeps ageing.
        if (ram.running && ram.timer) ram.timer->timer.running = command == "OFFREEZE";
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
                // 0x80312328..0x80312340: _time -1 (the timer's next
                // traversal is the effect's time 0), _timeRunning 1. _timeBias
                // keeps the value of the last visit (0 before the first).
                ram.timer->timer.time = -1;
                ram.timer->timer.running = true;
                ram.timer->particle_live = true;
            }
            ram.start = engine.time();
            ram.state = 1;
            continue;
        }
        if (ram.state == 1) {
            // 0x80312374..0x803123B4: the world time (manager +0xC, +0x50)
            // minus the timer's _timeBias (+0x30), 64-bit and wrapping,
            // against the info's lifetime (+0x28), signed; no exception for a
            // frozen effect. Only the timer's visits move the bias (8031A0A4:
            // to the first visit's time after a start, along with the time
            // while stopped), so an instance whose timer the traversal did
            // not reach since its start (culled, switched off) still has the
            // bias of its last visit and is usually stopped at the next update,
            // and a frozen instance out of view ages on. An instance without
            // a timer (no original counterpart) counts from its start.
            const std::int64_t life = ram.info.i64(10);
            const std::int64_t age =
                ram.timer ? static_cast<std::int64_t>(static_cast<std::uint64_t>(engine.time()) - static_cast<std::uint64_t>(ram.timer->timer.bias))
                          : engine.time() - ram.start;
            if (age < life) continue;
            if (static const bool trace = std::getenv("DW4_TRACE_PARTICLES") != nullptr; trace)
                engine.log("particles " + ram.info_name + "#" + std::to_string(ram.index) + " ends at age " + std::to_string(age / 1000000) +
                           " ms of " + std::to_string(life / 1000000) + " ms, " + std::to_string((engine.time() - ram.start) / 1000000) +
                           " ms after its start" + (ram.timer && ram.timer->timer.time == -1 ? " (timer not visited since)" : ""));
        }
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
    // (A plain igInfo never gets here: no module takes it, Engine::register_info.)
    return t.starts_with("be") || t == "ParticleInfo" || t == "igAnimationDatabase";
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
        // 802EBA8C returns null past the POWERSOCKETNAME count; 80269ECC
        // pushes it as one nil result (the dispatcher returns 1).
        out = {weapon.empty() ? Lua4Value{} : Lua4Value::string(weapon)};
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
        // (A form still open after a direct start's DW4_CARD_RECORD, see
        // apply_record, reads as the first row so a stage's boot.lua can build
        // its file names; the game itself always has the form set.)
        out = {Lua4Value::number(std::max(0, players[std::size_t(std::clamp(arg_int(0), 0, 3))].stats.species))};
        return true;
    }
    if (method == "TXTgetCallNo") { out = {Lua4Value::number(text_call)}; return true; }
    if (method == "SYSgetLightMode") { out = {Lua4Value::number(light_mode)}; return true; }
    if (method == "SYSgetGateStageName") {
        // Dispatch 14 (0x802EA4E0): 802EBE94 reads the GATESTAGENAME string
        // object (+0xC), 80269ECC pushes nil for a null one. start.lua of
        // stagML11 sets 'GateOff' only on nil, which makes g_cyexit SQ_POWEROF.
        out = {gate_stage_name ? Lua4Value::string(*gate_stage_name) : Lua4Value{}};
        return true;
    }
    if (method == "SYSgetGateStageNo") { out = {Lua4Value::number(gate_stage_no)}; return true; }
    if (method == "SYSsetGateStage") {
        // Dispatch 13 (0x802EA4CC) -> 802EBCFC: GATESTAGENO first, then the
        // name. A string argument also takes nil (binding mask 0xA = nil and
        // string tags, 802699FC), which arrives as a null pointer and stores
        // the empty string (r2-0x6F58, 0x802EBD68..0x802EBDE8). Other types
        // and a missing argument (LUA_TNONE) stop the call with the binding's
        // type error (8026A434) before 802EBCFC; not modelled (no script
        // does that), the name is kept then.
        gate_stage_no = arg_int(1);
        if (!args.empty() && args[0].type() == Lua4Type::Nil) gate_stage_name = std::string();
        else if (!args.empty() && args[0].type() == Lua4Type::String) gate_stage_name = std::string(args[0].as_string());
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
