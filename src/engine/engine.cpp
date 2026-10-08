#include "dw4/engine/engine.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/packages.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace dw4::engine {

// ---------------------------------------------------------------------------
// Packet argument parsing (802ECA74)
// ---------------------------------------------------------------------------
void parse_packet_arguments(std::string_view text, Packet& packet) {
    if (text.empty()) return;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto slash = text.find('/', start);
        const auto piece = text.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (piece.size() >= 2 && piece[1] == '<') {
            const auto body_end = piece.find('>');
            const auto body = piece.substr(2, body_end == std::string_view::npos ? std::string_view::npos : body_end - 2);
            switch (piece[0]) {
            case 'F': {
                float value = 0;
                std::from_chars(body.data(), body.data() + body.size(), value);
                packet.floats.push_back(value);
                break;
            }
            case 'I': {
                std::int32_t value = 0;
                std::from_chars(body.data(), body.data() + body.size(), value);
                packet.ints.push_back(value);
                break;
            }
            case 'L': {
                std::int64_t value = 0;
                std::from_chars(body.data(), body.data() + body.size(), value);
                packet.longs.push_back(value);
                break;
            }
            case 'V': {
                Vec3f v{};
                std::string copy(piece.substr(1));
                std::sscanf(copy.c_str(), " < %f , %f , %f > ", &v.x, &v.y, &v.z);
                packet.vectors.push_back(v);
                break;
            }
            default: packet.strings.emplace_back(std::string(piece)); break;
            }
        } else {
            packet.strings.emplace_back(std::string(piece));
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
}

void Manager::fallback(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
    (void)packet;
    engine.note(type_name_ + " command " + std::string(command) + " (" + ram.info_name + ")");
}

// ---------------------------------------------------------------------------
// Messenger (803050A8 / 80305344 / 80305A28 / deferred queue)
// ---------------------------------------------------------------------------
void Messenger::add_row(std::string category, std::optional<std::string> name, std::int32_t index, Ram* recipient) {
    rows_.push_back({std::move(category), std::move(name), index, recipient});
}

void Messenger::remove_rows(Manager* manager) {
    std::erase_if(rows_, [&](const MessengerRow& row) { return row.recipient && row.recipient->manager == manager; });
}

void Messenger::clear() {
    rows_.clear();
    deferred_.clear();
}

namespace {
bool row_matches(const MessengerRow& row, std::string_view category, std::optional<std::string_view> name, std::int32_t index) {
    if (row.category != category) return false;
    if (name && row.name && *row.name != *name) return false;
    if (index != -1 && row.index != -1 && row.index != index) return false;
    return true;
}
} // namespace

std::size_t Messenger::count(std::string_view category, std::optional<std::string_view> name, std::int32_t index) const {
    std::size_t n = 0;
    for (const auto& row : rows_) if (row_matches(row, category, name, index)) ++n;
    return n;
}

std::size_t Messenger::send(Engine& engine, std::string_view category, std::optional<std::string_view> name,
                            std::int32_t index, std::string_view command, Packet& packet) {
    if (command == "SEARCH") {
        const auto n = count(category, name, index);
        packet.bools.push_back(n != 0);
        packet.ints.push_back(static_cast<std::int32_t>(n));
        return n;
    }
    // Capture the matching recipients first; handlers may register or send.
    std::vector<Ram*> recipients;
    for (const auto& row : rows_) if (row_matches(row, category, name, index)) recipients.push_back(row.recipient);
    if (recipients.empty()) {
        engine.note("no recipient: " + std::string(category) + " " + std::string(command) +
                    (name ? " (" + std::string(*name) + ")" : std::string()));
        return 0;
    }
    for (auto* ram : recipients) {
        if (!ram || !ram->manager) continue;
        if (!ram->manager->command(engine, *ram, command, packet)) ram->manager->fallback(engine, *ram, command, packet);
    }
    return recipients.size();
}

void Messenger::post(std::int64_t time, std::string category, std::optional<std::string> name, std::int32_t index,
                     std::string command, Packet arguments) {
    DeferredMessage message{time, std::move(category), std::move(name), index, std::move(command), std::move(arguments)};
    auto it = std::upper_bound(deferred_.begin(), deferred_.end(), message.time,
                               [](std::int64_t t, const DeferredMessage& m) { return t < m.time; });
    deferred_.insert(it, std::move(message));
}

void Messenger::update(Engine& engine) {
    while (!deferred_.empty() && deferred_.front().time <= engine.time()) {
        DeferredMessage message = std::move(deferred_.front());
        deferred_.erase(deferred_.begin());
        std::optional<std::string_view> name;
        if (message.name) name = *message.name;
        send(engine, message.category, name, message.index, message.command, message.arguments);
    }
}

// ---------------------------------------------------------------------------
// Demo manager (802F7580, 802F7BDC, 802F7EC0)
// ---------------------------------------------------------------------------
void DemoManager::add(DemoRecord record) {
    if (!current_) {
        current_ = std::make_unique<DemoWork>();
        in_switch_ = false;
    }
    current_->records.push_back(std::move(record));
}

void DemoManager::begin_switch(DemoRecord record) {
    if (in_switch_) return;
    record.type = 3;
    add(std::move(record));
    in_switch_ = true;
}

void DemoManager::add_case(std::int32_t value) {
    if (!current_ || !in_switch_) return;
    DemoRecord record;
    record.type = 4;
    record.case_value = value;
    current_->records.push_back(std::move(record));
}

void DemoManager::end_switch() {
    if (!current_ || !in_switch_) return;
    in_switch_ = false;
    DemoRecord record;
    record.type = 5;
    current_->records.push_back(std::move(record));
}

void DemoManager::run() {
    if (!current_ || in_switch_) return;
    running_.push_back(std::move(current_));
    current_.reset();
    in_switch_ = false;
}

void DemoManager::clear() {
    current_.reset();
    running_.clear();
    in_switch_ = false;
}

std::vector<std::string> DemoManager::describe() const {
    std::vector<std::string> out;
    for (std::size_t w = 0; w < running_.size(); ++w) {
        const auto& work = *running_[w];
        std::string line = "work " + std::to_string(w) + " at " + std::to_string(work.cursor) + "/" + std::to_string(work.records.size());
        if (work.cursor < work.records.size()) {
            const auto& r = work.records[work.cursor];
            line += " type " + std::to_string(r.type) + " " + r.category + " " + (r.name ? *r.name : std::string("nil")) + " " +
                    std::to_string(r.index) + " " + r.command + " " + (r.argument ? *r.argument : std::string());
        }
        out.push_back(line);
    }
    return out;
}

void DemoManager::update(Engine& engine) {
    for (std::size_t w = running_.size(); w-- > 0;) {
        if (w >= running_.size()) continue;
        auto* work = running_[w].get();
        auto& records = work->records;
        std::size_t cursor = work->cursor;
        while (cursor < records.size()) {
            bool block = false;
            const DemoRecord record = records[cursor];
            if (record.type == 4) {
                while (cursor < records.size()) {
                    ++cursor;
                    if (cursor >= records.size() || records[cursor].type == 5) break;
                }
            } else if (record.type == 7) {
                if (record.argument) engine.run_script(*record.argument);
            } else if (record.type >= 0 && record.type <= 3) {
                Packet packet;
                if (record.argument) parse_packet_arguments(*record.argument, packet);
                std::optional<std::string_view> name;
                if (record.name) name = *record.name;
                if (record.delay_frames > 0) {
                    engine.messenger().post(engine.time() + kFrameNs * record.delay_frames, record.category, record.name,
                                            record.index, record.command, std::move(packet));
                } else {
                    engine.messenger().send(engine, record.category, name, record.index, record.command, packet);
                }
                if (record.type == 2) {
                    if (!packet.bools.empty()) { if (packet.bools[0]) block = true; }
                    else block = true;
                } else if (record.type == 1) {
                    if (!packet.bools.empty()) { if (packet.bools[0] != 1) block = true; }
                    else block = true;
                } else if (record.type == 3) {
                    if (!packet.ints.empty()) {
                        const auto value = packet.ints[0];
                        while (cursor < records.size()) {
                            ++cursor;
                            if (cursor >= records.size()) break;
                            if (records[cursor].type == 4 && records[cursor].case_value == value) break;
                            if (records[cursor].type == 5) break;
                        }
                    }
                }
            }
            if (block) break;
            ++cursor;
        }
        if (w < running_.size() && running_[w].get() == work) work->cursor = cursor;
    }
    std::erase_if(running_, [](const std::unique_ptr<DemoWork>& work) { return work->cursor >= work->records.size(); });
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
Engine::Engine(const std::filesystem::path& data_root) : resources_(MountedResources::open(data_root)) {
    global_root_.kind = NodeKind::group;
    global_root_.name = "GlobalRoot";
    cheats_.god = std::getenv("DW4_GOD") != nullptr;
    cheats_.one_hit = std::getenv("DW4_ONE_HIT") != nullptr;
    cheats_.infinite_mp = std::getenv("DW4_INFINITE_MP") != nullptr;
    // A new engine is a new boot: beSystem makes its generator then (8021B2F4
    // seeds 5150), so an online session's fresh engine starts every machine
    // on the same stream.
    fight::generator().seed(SystemRandom::kBootSeed);
    register_default_managers(*this);
}

Engine::~Engine() = default;

void Engine::add_manager(std::unique_ptr<Manager> manager) { managers_.push_back(std::move(manager)); }

Manager* Engine::manager(std::string_view type_name) const {
    for (const auto& m : managers_) if (m->type_name() == type_name) return m.get();
    return nullptr;
}

void Engine::note(std::string text) {
    for (auto& n : notes_) {
        if (n.text == text) { ++n.count; return; }
    }
    log("unimplemented: " + text);
    notes_.push_back({std::move(text), 1});
}

void Engine::log(std::string text) {
    std::cerr << "[" << frame_ << "] " << text << '\n';
    log_.push_back(std::move(text));
    while (log_.size() > 400) log_.pop_front();
}

void Engine::request_stage(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    pending_stage_ = std::move(name);
}

std::string Engine::loader_state() const {
    if (loader_state_ < 0) return "idle";
    return "state " + std::to_string(loader_state_);
}

void Engine::attach_scene_root(Node* root) {
    if (!root) return;
    if (std::find(global_root_.children.begin(), global_root_.children.end(), root) != global_root_.children.end()) return;
    global_root_.children.push_back(root);
}

Node* Engine::find_named_node(Node* from, std::string_view name) {
    if (!from) return nullptr;
    if (from->name == name) return from;
    for (auto* child : from->children) if (auto* found = find_named_node(child, name)) return found;
    return nullptr;
}

void Engine::begin_stage_load() {
    const std::string next = pending_stage_;
    pending_stage_.clear();
    log("stage change -> " + next);
    for (auto& m : managers_) m->clear_stage(*this);
    schedule_.clear();
    messenger_.clear();
    demo_.clear();
    global_root_.children.clear();
    for (auto& file : files_) retired_.push_back(file.document);
    for (auto& file : files_) scenes_.release_document(file.document.get());
    files_.clear();
    retired_.clear();
    deferred_infos_.clear();
    lua_infos_.clear();
    common_queue_.clear();
    lua_.reset();
    stage_name_ = next;
    stage_folder_ = next;
    loading_node_ = nullptr;
    soft_reset_allowed_ = false;
    loader_state_ = 0;
    // What survives into this stage, for save states.
    StageSnapshot snapshot;
    snapshot.stage = next;
    snapshot.time = time_;
    snapshot.frame = frame_;
    snapshot.flags = flags_;
    snapshot.counters = counters_;
    snapshot.pad_input_enabled = pad_input_enabled_;
    snapshot.cheats = cheats_;
    snapshot.pads = pads_;
    {
        std::ostringstream random;
        random << fight::generator();
        snapshot.random = random.str();
    }
    if (auto* sys = get<GameSystem>()) snapshot.system = std::make_shared<const GameSystem>(*sys);
    if (auto* sound = get<SoundManager>()) snapshot.sound = sound->persistent();
    snapshot.serial = ++snapshot_serial_;
    snapshot_ = std::move(snapshot);
}

void Engine::restore(const StageSnapshot& snapshot) {
    time_ = snapshot.time;
    frame_ = snapshot.frame;
    flags_ = snapshot.flags;
    counters_ = snapshot.counters;
    pad_input_enabled_ = snapshot.pad_input_enabled;
    cheats_ = snapshot.cheats;
    pads_ = snapshot.pads;
    {
        std::istringstream random(snapshot.random);
        random >> fight::generator();
    }
    if (auto* sys = get<GameSystem>(); sys && snapshot.system) {
        const Ram keep = sys->ram_;
        *sys = *static_cast<const GameSystem*>(snapshot.system.get());
        sys->ram_ = keep;
        sys->registered_ = false;
    }
    if (auto* sound = get<SoundManager>()) sound->restore(*this, snapshot.sound);
    // The rest of the step that started the load (Engine::step).
    pending_stage_ = snapshot.stage;
    begin_stage_load();
    process_stage_loader();
    if (auto* camera = get<CameraManager>()) camera->begin_frame(*this);
    messenger_.update(*this);
    demo_.update(*this);
    update_managers();
}

std::uint64_t Engine::fingerprint() {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 0x100000001b3ull;
        }
    };
    auto mix_value = [&](auto value) { mix(&value, sizeof value); };
    mix_value(frame_);
    mix_value(time_);
    mix(stage_name_.data(), stage_name_.size());
    mix_value(loader_state_);
    for (const auto& [key, value] : flags_) {
        mix_value(key);
        mix_value(value);
    }
    for (const auto& [key, value] : counters_) {
        mix_value(key);
        mix_value(value);
    }
    if (auto* sys = get<GameSystem>())
        for (const auto& p : sys->players) {
            mix_value(p.joined);
            mix_value(p.hp);
            mix_value(p.mp);
            mix_value(p.bits);
            mix_value(p.stats.level);
            mix_value(p.stats.exp);
        }
    if (auto* models = get<ModelCtrlManager>())
        for (const auto& ram : models->rams()) {
            if (!ram->generated) continue;
            mix(ram->info_name.data(), ram->info_name.size());
            mix_value(ram->index);
            mix_value(ram->hp);
            mix(ram->seq_name.data(), ram->seq_name.size());
            if (ram->node)
                for (int i = 0; i < 3; ++i) mix_value(ram->node->matrix(3, i));
        }
    return h;
}

namespace {
Node* find_loading_node(Node* node, int depth = 0) {
    if (!node || depth > 64) return nullptr;
    if (node->kind == NodeKind::node_anim && node->anim && node->anim->loading) return node;
    for (auto* child : node->children)
        if (auto* found = find_loading_node(child, depth + 1)) return found;
    return nullptr;
}
} // namespace

void Engine::load_file(const std::string& logical_path, bool member, std::vector<ObjectView>& deferred) {
    std::shared_ptr<const IgbDocument> document;
    try {
        document = std::make_shared<const IgbDocument>(read_igb_document(resources_.read(logical_path)));
    } catch (const std::exception& error) {
        log("load failed: " + logical_path + ": " + error.what());
        return;
    }
    files_.push_back({logical_path, document, member});
    if (std::getenv("DW4_TRACE_LOAD")) log("load " + logical_path);
    if (!document->info_list_reference) return;
    for (const auto& element : document->object_list(*document->info_list_reference)) {
        if (!element) continue;
        ObjectView info(document.get(), *element);
        if (info.type() == "beLuaInfo") {
            lua_infos_.push_back(info);
            continue;
        }
        deferred.push_back(info);
    }
}

bool Engine::load_common_now(const std::string& logical_path) {
    for (const auto& file : files_)
        if (file.logical_path == logical_path) return true;
    const auto before = files_.size();
    std::vector<ObjectView> infos;
    load_file(logical_path, false, infos);
    for (const auto& info : infos) register_info(info);
    return files_.size() != before;
}

void Engine::register_info(const ObjectView& info) {
    bool accepted = false;
    for (auto& m : managers_) {
        if (m->accepts(info)) {
            m->add_info(*this, info);
            // 802F251C: an accepted info schedules its manager (8028A398);
            // the scheduler appends it unless it is already active (80041E40
            // with an empty dependency order -> 800694EC).
            const Scheduled entry{m.get(), m->schedule_slot(info)};
            if (std::find(schedule_.begin(), schedule_.end(), entry) == schedule_.end()) schedule_.push_back(entry);
            accepted = true;
            break;
        }
    }
    if (!accepted) note("info type " + info.type());
}

void Engine::process_stage_loader() {
    if (loader_state_ < 0) return;
    switch (loader_state_) {
    case 0: {
        // 802F8A64 state 0/1: loadingUSA/ld<stage name from its third
        // letter>.igb shows while the stage loads (its scene with a
        // beLoadingNode, later the LOADING layer).
        if (stage_name_.size() > 2) {
            const std::string path = "loadingusa/ld" + stage_name_.substr(2) + ".igb";
            if (const auto* record = resources_.catalog().find(path); record && record->location) {
                std::vector<ObjectView> infos;
                load_file(path, false, infos);
                for (const auto& info : infos) register_info(info);
                loading_node_ = find_loading_node(&global_root_);
            }
        }
        loader_state_ = 1;
        break;
    }
    case 1: {
        try {
            const auto descriptor = load_package_descriptor(resources_, stage_name_ + ".igb");
            std::string section = descriptor.section;
            log("stage " + stage_name_ + " section " + section + ", " + std::to_string(descriptor.members.size()) + " members");
            // Preliminary "/s" members first (state 1), then the rest (state 2).
            std::vector<ObjectView> preliminary;
            for (const auto& member : descriptor.members) {
                if (member.suffix != "/s") continue;
                const auto document = std::make_shared<const IgbDocument>(read_package_member(resources_, member));
                files_.push_back({stage_name_ + member.specifier, document, true});
                if (document->info_list_reference)
                    for (const auto& element : document->object_list(*document->info_list_reference))
                        if (element) preliminary.push_back(ObjectView(document.get(), *element));
            }
            for (const auto& info : preliminary) register_info(info);
            for (const auto& member : descriptor.members) {
                if (member.suffix == "/s") continue;
                const auto document = std::make_shared<const IgbDocument>(read_package_member(resources_, member));
                files_.push_back({stage_name_ + member.specifier, document, true});
                if (!document->info_list_reference) continue;
                for (const auto& element : document->object_list(*document->info_list_reference)) {
                    if (!element) continue;
                    ObjectView info(document.get(), *element);
                    if (info.type() == "beLuaInfo") lua_infos_.push_back(info);
                    else deferred_infos_.push_back(info);
                }
            }
        } catch (const std::exception& error) {
            log(std::string("stage load failed: ") + error.what());
            loader_state_ = -1;
            return;
        }
        loader_state_ = 3;
        break;
    }
    case 3:
        install_lua();
        run_script("func.lua");
        run_script("boot.lua");
        loader_state_ = 4;
        break;
    case 4: {
        auto queue = std::move(common_queue_);
        common_queue_.clear();
        for (const auto& path : queue) load_file(path, false, deferred_infos_);
        loader_state_ = 10;
        break;
    }
    case 10:
        // State 4: the loading screen plays at least its minimum time
        // (80311674), then holds its lock time (80311660).
        if (loading_node_) {
            if (!loading_node_->anim->finished()) break;
            loading_node_->anim->fixed = loading_node_->anim->lock;
        }
        loader_state_ = 5;
        break;
    case 5: {
        auto infos = std::move(deferred_infos_);
        deferred_infos_.clear();
        for (const auto& info : infos) register_info(info);
        loader_state_ = 6;
        break;
    }
    case 6: {
        Packet packet;
        messenger_.send(*this, "LAYERCTL", std::nullopt, -1, "ORDER", packet);
        // The loading screen stays on its layer until the stage scripts
        // remove it.
        Packet loading;
        loading.strings = {std::string("LOADING"), std::string("SET")};
        messenger_.send(*this, "LAYERCTL", std::nullopt, -1, "SWITCH", loading);
        loader_state_ = 7;
        break;
    }
    case 7: {
        Packet packet;
        messenger_.send(*this, "TEXTCTL", std::nullopt, -1, "INITIALIZE", packet);
        run_script("start.lua");
        loader_state_ = 8;
        break;
    }
    case 8:
        loader_state_ = 9;
        break;
    case 9:
        run_script("first.lua");
        // The soft reset works outside the logo, opening, title and ending
        // stages (stagIS08 / 09 / 10 / 12).
        soft_reset_allowed_ = stage_name_ != "stagis08" && stage_name_ != "stagis09" && stage_name_ != "stagis10" &&
                              stage_name_ != "stagis12";
        loader_state_ = -1;
        log("stage " + stage_name_ + " started");
        // DW4_TRACE_SCHEDULE=1 logs the manager update order of the stage.
        if (std::getenv("DW4_TRACE_SCHEDULE")) {
            std::string order = "schedule:";
            for (const auto& entry : schedule_) order += " " + entry.manager->slot_name(entry.slot);
            log(order);
        }
        break;
    default:
        loader_state_ = -1;
        break;
    }
}

void Engine::step(const std::array<PadSample, 4>& samples) {
    auto input = samples;
    // STAGECTL PADINPUT DISABLE: the game sees released controls.
    if (!pad_input_enabled_)
        for (auto& sample : input) { sample.buttons = 0; sample.left = {}; sample.right = {}; }
    try { pads_.update(input); } catch (const std::exception& error) { note(std::string("pad input: ") + error.what()); }
    // The frame clock advances first so updates and the following render
    // observe the same time (joint clamps and fades end exactly on a key).
    // One NTSC field of world time (8028AF84), see field_time.
    time_ += field_time(frame_ + 1) - field_time(frame_);
    ++frame_;
    if (!pending_stage_.empty()) begin_stage_load();
    process_stage_loader();
    // The current camera viewer (802F4268, a scheduled module ahead of the
    // demo, the messenger and the managers) sets the frame's camera before
    // they run; see CameraManager::begin_frame.
    if (auto* camera = get<CameraManager>()) camera->begin_frame(*this);
    messenger_.update(*this);
    demo_.update(*this);
    update_managers();
}

// The scheduler run (8028B7B0): the managers scheduled at registration, then
// the stage's managers in the order of their first info. So beAction2 runs
// before beModelCtrl, both before beSelectCtrl and the menu managers
// (beNDMWStatus, beNDMWPanelWaza), and beFont comes last, as the stage files
// list their infos. A manager without an info in this stage is not scheduled
// in the original; the port still runs it after the others (idle).
void Engine::update_managers() {
    for (auto& m : managers_)
        if (m->scheduled_at_registration()) m->update(*this);
    // 8028B7B0 walks a copy, so a manager scheduled during the run (a file
    // loaded on demand) first runs in the next frame.
    schedule_run_ = schedule_;
    for (const auto& entry : schedule_run_)
        if (!entry.manager->scheduled_at_registration()) entry.manager->update_slot(*this, entry.slot);
    for (auto& m : managers_)
        if (!m->scheduled_at_registration() &&
            std::none_of(schedule_run_.begin(), schedule_run_.end(), [&](const Scheduled& e) { return e.manager == m.get(); }))
            m->update(*this);
}

bool Engine::main_lobby() const {
    std::string stage = stage_name_;
    std::transform(stage.begin(), stage.end(), stage.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return stage == "stagml02" || stage == "stagml11" || stage == "stagml12" || stage == "stagml13";
}

void Engine::render(std::vector<DrawItem>& out, FrameCamera& camera, Vec4f& clear_color, ShadowFrame* shadow) {
    out.clear();
    clear_color = {0, 0, 0, 1};
    if (auto* bk = get<BkColorManager>()) clear_color = bk->color();
    if (auto* cam = get<CameraManager>()) cam->frame_camera(*this, camera);
    if (debug_camera) camera = *debug_camera;
    TraversalContext context;
    context.cache = &scenes_;
    context.time = time_;
    context.camera = camera;
    context.output = &out;
    if (auto* models = get<ModelCtrlManager>()) models->install_actor_hook(*this, context);
    if (auto* actions = get<ActionManager>())
        context.actor_pose = [this, actions](const Node& actor) { return actions->pose_for(actor, time_, true); };
    Mat4 inverse_view;
    if (invert(camera.view, inverse_view)) context.camera_position = {inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
    traverse(&global_root_, context);
    // The scene traversal submits its packages to the igSorter it holds at
    // +0x44 (geometry 801DDB84 -> 80205684). Its begin (801DB82C) resets the
    // sorter (8020551C), its end (801DB688) flushes it (80204A80, vtable
    // 804B37AC +5C). DW4 runs it with _mode (+0xC) SORT_TRANSPARENT (1) and
    // _transparencyDrawMode (+0x4C) 0: a package whose effective
    // igBlendStateAttr is on goes to _transpPackages (+0x18), every other one
    // is drawn at once in traversal order; the flush appends the closed
    // compound packages (_opaquePackages +0x1C: in the stages only the
    // igProjectiveShadowShader receiver pass 801FE348, which the renderer
    // draws at the boundary) and then the transparent ones in submission
    // order, without a distance sort (bits 4/8 clear). The whole frame is one
    // traversal of ROOTATTR (the layers live under its LAYERPARENT, 80301368),
    // so the 2D layers share both lists with the 3D scene.
    std::stable_partition(out.begin(), out.end(), [](const DrawItem& item) { return !item.state.blend_enabled; });
    if (shadow) {
        shadow->active = false;
        if (auto* shadows = get<ShadowManager>()) shadows->render(*this, context, *shadow);
    }
}

std::vector<std::string> Engine::scene_report() {
    std::vector<std::string> lines;
    auto* scenes = get<SceneManager>();
    if (!scenes) return lines;
    for (const auto& info : scenes->scenes()) {
        const auto root = info.ref(5);
        if (!root) continue;
        std::vector<DrawItem> items;
        TraversalContext context;
        context.cache = &scenes_;
        context.time = time_;
        context.output = &items;
        Node* node = scenes_.node(root);
        // Count what the scene would draw with all switches as stored.
        traverse(node, context);
        std::size_t textured = 0, blended = 0, vertices = 0, lit = 0;
        for (const auto& item : items) {
            textured += item.state.texture_enabled && item.state.texture ? 1 : 0;
            blended += item.state.blend_enabled ? 1 : 0;
            lit += item.state.lighting ? 1 : 0;
            vertices += item.geometry->positions.size();
        }
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, "scene '%s' root '%s' (%s) draws=%zu textured=%zu blended=%zu lit=%zu verts=%zu",
                      std::string(info.str(2)).c_str(), node->name.c_str(), node->object.type().c_str(), items.size(), textured,
                      blended, lit, vertices);
        lines.emplace_back(buffer);
    }
    return lines;
}

} // namespace dw4::engine
