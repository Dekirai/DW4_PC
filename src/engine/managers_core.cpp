#include "dw4/engine/managers.hpp"
#include "dw4/engine/save_menu.hpp"
#include "dw4/igb_document.hpp"

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
        const int value = mode == "DISPOFF" ? 0 : mode == "DISPON" ? 1 : mode == "FLIP" ? 2 : -1;
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
    for (auto* ram : created) {
        ram->paths = paths;
        ram->demo_data = demo;
        ram->demo_data_duration = demo_duration;
    }
}

void CameraManager::clear_stage(Engine&) {
    rams_.clear();
    current_.reset();
}

bool CameraManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<CameraRam&>(base);
    if (command == "SETDEMOCAMERA") {
        // Play the named camera once: a demo camera info of that name uses its
        // beCameraDemoData path and length; otherwise a path of this camera
        // whose node has the name plays for its longest transform sequence
        // (at least one frame so ISDEMOCAMERA reports it).
        const auto name = packet.string(0);
        if (ram.demo_data && ram.info_name == name) {
            ram.demo_path = -2;
            ram.demo_duration = std::max<std::int64_t>(ram.demo_data_duration, kFrameNs);
            ram.demo_active = true;
            ram.demo_start = engine.time();
            return true;
        }
        ram.demo_path = -1;
        bool named_info = false;
        for (const auto& other : rams_) named_info = named_info || (other->demo_data && other->info_name == name);
        if (named_info) {
            ram.demo_active = false;
            return true;
        }
        for (std::size_t i = 0; i < ram.paths.size() && ram.demo_path < 0; ++i)
            for (auto* node : ram.paths[i].nodes)
                if (node && node->name == name) { ram.demo_path = int(i); break; }
        ram.demo_duration = kFrameNs;
        if (ram.demo_path >= 0)
            for (auto* node : ram.paths[std::size_t(ram.demo_path)].nodes)
                if (node && node->sequence && node->sequence->valid && !node->sequence->times.empty())
                    ram.demo_duration = std::max(ram.demo_duration, node->sequence->duration > 0 ? node->sequence->duration
                                                                                                 : node->sequence->times.back());
        else engine.note("CAMERA demo path " + std::string(name));
        ram.demo_active = true;
        ram.demo_start = engine.time();
        return true;
    }
    if (command == "ISDEMOCAMERA") {
        // One answer for the broadcast: whether any camera demo still runs.
        if (&ram != rams_.front().get()) return true;
        bool active = false;
        for (auto& other : rams_) {
            if (other->demo_active && engine.time() - other->demo_start > other->demo_duration) other->demo_active = false;
            active = active || other->demo_active;
        }
        packet.bools.push_back(active ? 1 : 0);
        return true;
    }
    if (command == "SETBOXCAMERA") {
        ram.box_camera = true;
        ram.demo_active = false;
        ram.demo_path = -1;
        return true;
    }
    return false;
}

void CameraManager::update(Engine&) {}

void CameraManager::frame_camera(Engine& engine, FrameCamera& camera) {
    camera.projection = perspective(54.4322f * std::numbers::pi_v<float> / 180.0f, 4.0f / 3.0f, 10.0f, 2000.0f);
    camera.view = look_at({0, 50, 300}, {0, 0, 0}, {0, 1, 0});
    // A running demo camera info takes over from the stage camera.
    const CameraRam* demo_info = nullptr;
    for (const auto& ram : rams_)
        if (ram->demo_active && ram->demo_path == -2 && engine.time() - ram->demo_start <= ram->demo_duration) demo_info = ram.get();
    for (const auto& owned : rams_) {
        const CameraRam* ram = demo_info ? demo_info : owned.get();
        if (!demo_info && ram->paths.empty()) continue;
        const bool demo = demo_info || (ram->demo_active && ram->demo_path >= 0);
        const auto& path = demo_info ? *ram->demo_data : ram->paths[std::size_t(demo ? ram->demo_path : 0)];
        const std::int64_t at = demo ? std::min(engine.time() - ram->demo_start, ram->demo_duration) : 0;
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
            const float fov = path.camera->object.f32(8, 54.4322f);
            const float near_plane = path.camera->object.f32(9, 10.0f);
            const float far_plane = path.camera->object.f32(10, 2000.0f);
            camera.projection = perspective(fov * std::numbers::pi_v<float> / 180.0f, 4.0f / 3.0f, near_plane, far_plane);
        }
        // Follow the first player: keep the authored angle and distance.
        if (!demo) {
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
        break;
    }
}

// ---------------------------------------------------------------------------
// Stage control (beNDMWStageCtl): NEXTSTAGE / PADINPUT
// ---------------------------------------------------------------------------
void StageCtlManager::add_info(Engine& engine, const ObjectView& info) { make_rams(engine, this, info, rams_, "STAGECTL"); }
void StageCtlManager::clear_stage(Engine&) { rams_.clear(); }
bool StageCtlManager::command(Engine& engine, Ram&, std::string_view command, Packet& packet) {
    if (command == "NEXTSTAGE") {
        if (!packet.strings.empty()) engine.request_stage(std::string(packet.string(0)));
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

// ---------------------------------------------------------------------------
// NDMW logo (803B18BC). The original first checks both memory card slots;
// the PC build keeps saves beside the executable, so the check succeeds and
// the controller proceeds to state 2, which runs logo.lua once.
// ---------------------------------------------------------------------------
void LogoManager::add_info(Engine& engine, const ObjectView& info) { make_rams(engine, this, info, rams_, "LOGO"); }
void LogoManager::clear_stage(Engine&) { rams_.clear(); }
void LogoManager::update(Engine& engine) {
    for (auto& ram : rams_) {
        if (ram->state == 0) ram->state = 2;
        if (ram->state == 2) {
            engine.run_script("logo.lua");
            ram->state = 3;
        }
    }
}

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
    movie_playing_ = false;
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
        const float pan = float(split->pan) / 127.0f;
        voice.left = gain * std::min(1.0f, 2.0f * (1.0f - pan));
        voice.right = gain * std::min(1.0f, 2.0f * pan);
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
    for (auto& stream : streams_) {
        if (!stream.decoder || stream.paused || stream.ready_in > 0) continue;
        const auto& header = stream.decoder->header();
        float ramp = stream.ramp_to;
        if (stream.ramp_left > 0 && stream.ramp_frames > 0)
            ramp = stream.ramp_to + (stream.ramp_from - stream.ramp_to) * float(stream.ramp_left) / float(stream.ramp_frames);
        const float gain = stream.gain * ramp * (stream.fade_left > 0 ? float(stream.fade_left) / 10.0f : 1.0f);
        const std::uint32_t channels = header.channels;
        std::int16_t source[16];
        auto fetch = [&](std::array<float, 2>& frame) {
            if (stream.decoder->read(source, 1) == 0) return false;
            frame[0] = float(source[0]);
            frame[1] = float(channels > 1 ? source[1] : source[0]);
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
bool SoundManager::command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
    const auto& t = ram.info.type();
    if (t == "beMovieInfo") {
        if (command == "PLAY") {
            engine.note("movie playback (Sofdec SFD) " + std::string(packet.string(0)));
            movie_playing_ = true;
            movie_end_ = engine.time() + kFrameNs * 2;
            return true;
        }
        if (command == "ISPLAY" || command == "ISRUN") {
            packet.bools.push_back(movie_playing_ ? 1 : 0);
            return true;
        }
        if (command == "STOP") { movie_playing_ = false; return true; }
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
void SoundManager::update(Engine& engine) {
    for (auto& stream : streams_) {
        if (stream.ready_in > 0) --stream.ready_in;
        if (stream.ramp_left > 0 && !stream.paused) --stream.ramp_left;
    }
    for (std::size_t channel = 0; channel < streams_.size(); ++channel)
        if (streams_[channel].fade_left > 0 && --streams_[channel].fade_left == 0) stop(int(channel));
    for (auto& track : tracks_) {
        track.wait_ms -= 1000.0 / 60.0;
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
    if (movie_playing_ && engine.time() >= movie_end_) {
        movie_playing_ = false;
        engine.run_script("nextStage.lua");
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
        if (!rams.empty() && rams.front()) {
            radii_[std::string(info.str(2))] = rams.front().f32(13);
            masks_[std::string(info.str(2))] = std::uint32_t(rams.front().i32(12));
        }
        return;
    }
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
        for (const auto& child : children) {
            if (!child) continue;
            followers_.push_back({world_.add_dynamic(child, kind), child.ref(12), std::string(info.str(2))});
        }
        return;
    }
    // Each check-switch child is its own switchable group, addressed by the
    // matching name of the info's name list [9] (e.g. "hithideliftz00").
    const auto groups = world_.add_switch(info.ref(8), kind);
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
    (void)engine;
    (void)packet;
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

void HitLandManager::clear_stage(Engine&) {
    followers_.clear();
    info_off_.clear();
    rams_.clear();
    children_.clear();
    radii_.clear();
    masks_.clear();
    world_.clear();
}

// ---------------------------------------------------------------------------
bool PassiveManager::accepts(const ObjectView& info) const {
    const auto& t = info.type();
    return t.starts_with("be") || t == "ParticleInfo" || t == "igInfo" || t == "igAnimationDatabase";
}
void PassiveManager::add_info(Engine& engine, const ObjectView& info) {
    engine.note("runtime for " + info.type());
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
    if (method == "PLAYERgetPos") { out = {Lua4Value::number(player_pos)}; return true; }
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
void register_default_managers(Engine& engine) {
    engine.add_manager(std::make_unique<GameSystem>());
    engine.add_manager(std::make_unique<SceneManager>());
    engine.add_manager(std::make_unique<LayerManager>());
    engine.add_manager(std::make_unique<BkColorManager>());
    engine.add_manager(std::make_unique<StageCtlManager>());
    engine.add_manager(std::make_unique<SwitchCtrlManager>());
    engine.add_manager(std::make_unique<LogoManager>());
    engine.add_manager(std::make_unique<SoundManager>());
    engine.add_manager(std::make_unique<SaveUtilManager>());
    // The quick panels run before the models, so the opening trigger is not
    // seen again by the panel in the frame the player opens it.
    engine.add_manager(std::make_unique<PanelManager>());
    engine.add_manager(std::make_unique<SubWindowManager>());
    engine.add_manager(std::make_unique<ModelCtrlManager>());
    engine.add_manager(std::make_unique<ActionManager>());
    engine.add_manager(std::make_unique<CameraManager>());
    engine.add_manager(std::make_unique<TextureCtrlManager>());
    engine.add_manager(std::make_unique<PlayerPointManager>());
    engine.add_manager(std::make_unique<HitLandManager>());
    engine.add_manager(std::make_unique<TextCtlManager>());
    engine.add_manager(std::make_unique<SelectCtrlManager>());
    engine.add_manager(std::make_unique<Title2Manager>());
    engine.add_manager(std::make_unique<LoadCtrl2Manager>());
    engine.add_manager(std::make_unique<LoadIntf2Manager>());
    engine.add_manager(std::make_unique<SaveCtrlManager>());
    engine.add_manager(std::make_unique<SaveIntfManager>());
    engine.add_manager(std::make_unique<DatabaseManager>());
    engine.add_manager(std::make_unique<MatCtrlManager>());
    engine.add_manager(std::make_unique<GeneraterManager>());
    engine.add_manager(std::make_unique<NumberCtrlManager>());
    engine.add_manager(std::make_unique<MeterCtrlManager>());
    engine.add_manager(std::make_unique<WeaponManager>());
    engine.add_manager(std::make_unique<ActionStarterManager>());
    engine.add_manager(std::make_unique<PassiveManager>());
}

} // namespace dw4::engine
