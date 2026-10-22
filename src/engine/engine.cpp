#include "dw4/engine/engine.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/packages.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
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
    by_category_stale_ = true;
}

void Messenger::remove_rows(Manager* manager) {
    std::erase_if(rows_, [&](const MessengerRow& row) { return row.recipient && row.recipient->manager == manager; });
    by_category_stale_ = true;
}

void Messenger::clear() {
    // A stage change reloads every module (NEXTSTAGE 80347F2C -> 8028A87C):
    // the messenger's reset hook 803040C0 also drops the deferred record
    // pool and the pause.
    rows_.clear();
    deferred_.clear();
    deferred_records_ = kDeferredRecords;
    paused_at_ = 0;
    by_category_stale_ = true;
}

template <class Visit>
void Messenger::candidates(std::string_view category, std::optional<std::string_view> name, Visit&& visit) const {
    if (by_category_stale_) {
        by_category_.clear();
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto& rows = by_category_[rows_[i].category];
            rows.all.push_back(std::uint32_t(i));
            if (rows_[i].name) rows.named[*rows_[i].name].push_back(std::uint32_t(i));
            else rows.unnamed.push_back(std::uint32_t(i));
        }
        by_category_stale_ = false;
    }
    const auto found = by_category_.find(category);
    if (found == by_category_.end()) return;
    const auto& rows = found->second;
    if (!name) {
        for (const auto i : rows.all)
            if (!visit(rows_[i])) return;
        return;
    }
    // A named selection takes the rows of that name and the unnamed ones:
    // both lists merged back into row order.
    static const std::vector<std::uint32_t> none;
    const auto named = rows.named.find(*name);
    const auto& a = named == rows.named.end() ? none : named->second;
    const auto& b = rows.unnamed;
    std::size_t ia = 0, ib = 0;
    while (ia < a.size() || ib < b.size()) {
        const std::uint32_t i = ib >= b.size() || (ia < a.size() && a[ia] < b[ib]) ? a[ia++] : b[ib++];
        if (!visit(rows_[i])) return;
    }
}

namespace {
// A row takes a message when the category is its own, the name (if both
// have one) is its own and the index (if neither is -1) is its own;
// candidates() gives the rows that fit the first two.
bool index_matches(const MessengerRow& row, std::int32_t index) { return index == -1 || row.index == -1 || row.index == index; }
} // namespace

std::size_t Messenger::count(std::string_view category, std::optional<std::string_view> name, std::int32_t index) const {
    std::size_t n = 0;
    candidates(category, name, [&](const MessengerRow& row) {
        if (index_matches(row, index)) ++n;
        return true;
    });
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
    if (index == -2) {
        // 80305A28 (0x80305D3C..0x80305DE4): index -2 goes to the first RAM
        // of the target whose in-use flag (RAM +0x14, the record's [5]) is
        // clear, and to that one only; with every RAM in use nothing
        // happens. Scripts GENERATE into a free RAM that way (Lucemon's
        // x_035frunn01a grids, Infermon's split).
        bool any = false;
        candidates(category, name, [&](const MessengerRow& row) {
            any = true;
            if (row.recipient && !row.recipient->active) {
                recipients.push_back(row.recipient);
                return false;
            }
            return true;
        });
        if (any && recipients.empty()) return 0;
    } else {
        // (The candidate rows in row order: the same selection as the
        // original's walk over all rows.)
        candidates(category, name, [&](const MessengerRow& row) {
            if (index_matches(row, index)) recipients.push_back(row.recipient);
            return true;
        });
    }
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
    // 80305F9C prepares the message in the pool's last record and makes a new
    // one when that record is still in use.
    if (deferred_.size() >= deferred_records_) ++deferred_records_;
    DeferredMessage message{time, std::move(category), std::move(name), index, std::move(command), std::move(arguments)};
    auto it = std::upper_bound(deferred_.begin(), deferred_.end(), message.time,
                               [](std::int64_t t, const DeferredMessage& m) { return t < m.time; });
    deferred_.insert(it, std::move(message));
}

void Messenger::pause(const Engine& engine, bool on) { paused_at_ = on ? engine.time() : 0; }

void Messenger::update(Engine& engine) {
    const auto now = engine.time();
    // 80304C58 while paused: every posted time moves on by the world time
    // since the pause or the last pass, nothing is delivered.
    if (paused_at_ != 0) {
        const auto shift = now - paused_at_;
        for (auto& message : deferred_) message.time += shift;
        paused_at_ = now;
        return;
    }
    // Otherwise the head is delivered while its time is not after now, at
    // most as many times as the pool had records when the pass began: a
    // message a delivery posts for now usually still arrives in this pass.
    for (std::size_t budget = deferred_records_; budget > 0 && !deferred_.empty() && deferred_.front().time <= now; --budget) {
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

void Engine::add_manager(std::unique_ptr<Manager> manager) {
    managers_.push_back(std::move(manager));
    found_managers_.clear();
}

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
    std::string text = loader_state_ == kLoaderBoot ? std::string("boot reads")
                       : loader_state_ == kLoaderDescriptor ? std::string("descriptor")
                                                            : "state " + std::to_string(loader_state_);
    if (loader_file_.active)
        text += ", " + loader_file_.item.name + " " + std::to_string(loader_file_.read) + "/" + std::to_string(loader_file_.sectors) + " sectors";
    return text;
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

void Engine::begin_stage_load(const StageSnapshot* restoring) {
    const std::string next = pending_stage_;
    pending_stage_.clear();
    log("stage change -> " + next);
    // The update that asks for the next descriptor (beSystem 80316258) first
    // releases the outgoing stage (8006F794): its length follows what the
    // stage holds (loader_teardown_cost); a save state keeps it.
    if (restoring) {
        loader_teardown_ = restoring->loader_teardown;
        loader_common_ = restoring->loader_common;
    } else {
        loader_teardown_ = loader_teardown_cost();
    }
    if (std::getenv("DW4_TRACE_LOADER")) {
        std::size_t objects = 0, memories = 0;
        for (const auto& file : files_)
            if (file.document) {
                objects += file.document->objects.size();
                memories += file.document->memories.size();
            }
        log("loader teardown of " + stage_name_ + ": " + std::to_string(files_.size()) + " files, " + std::to_string(objects) +
            " objects, " + std::to_string(memories) + " memory blocks -> " + std::to_string(loader_teardown_) + " milli-fields");
    }
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
    // Compiled chunks are keyed by their document's address: a new document
    // may reuse a freed one's.
    lua_programs_.clear();
    stage_name_ = next;
    stage_folder_ = next;
    loading_node_ = nullptr;
    soft_reset_allowed_ = false;
    // The session's first load starts with the boot's reads.
    loader_state_ = loader_common_ ? kLoaderBoot : kLoaderDescriptor;
    loader_boot_step_ = 0;
    loader_file_ = {};
    loader_queue_.clear();
    loader_index_ = 0;
    loader_members_.clear();
    loader_preliminary_.clear();
    loader_skip_ = 0;
    loader_cost_ = 0;
    loader_processing_ = 0;
    // The load timing of this load and the stage it starts (a save state's
    // cheats are in place by now).
    loader_fast_ = cheats_.fast_loading;
    if (loader_fast_ && std::getenv("DW4_TRACE_LOADER")) log("loader: fast loading");
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
    snapshot.console_reset = console_reset_;
    {
        std::ostringstream random;
        random << fight::generator();
        snapshot.random = random.str();
    }
    if (auto* points = get<PlayerPointManager>()) snapshot.point_no = points->point_no();
    snapshot.loader_teardown = loader_teardown_;
    snapshot.loader_common = loader_common_;
    snapshot.stage_active = stage_active_;
    if (auto* sys = get<GameSystem>()) snapshot.system = std::make_shared<const GameSystem>(*sys);
    if (auto* sound = get<SoundManager>()) snapshot.sound = sound->persistent();
    snapshot.serial = ++snapshot_serial_;
    snapshot_ = std::move(snapshot);
}

void Engine::restore(const StageSnapshot& snapshot) {
    time_ = snapshot.time;
    frame_ = snapshot.frame;
    world_delta_ = frame_delta(); // the field that step added (no stage is active in it)
    flags_ = snapshot.flags;
    counters_ = snapshot.counters;
    pad_input_enabled_ = snapshot.pad_input_enabled;
    cheats_ = snapshot.cheats;
    pads_ = snapshot.pads;
    console_reset_ = snapshot.console_reset;
    {
        std::istringstream random(snapshot.random);
        random >> fight::generator();
    }
    if (auto* points = get<PlayerPointManager>()) points->set_point_no(snapshot.point_no);
    if (auto* sys = get<GameSystem>(); sys && snapshot.system) {
        const Ram keep = sys->ram_;
        *sys = *static_cast<const GameSystem*>(snapshot.system.get());
        sys->ram_ = keep;
        sys->registered_ = false;
    }
    if (auto* sound = get<SoundManager>()) sound->restore(*this, snapshot.sound);
    // The managers run on until the loader's state 0 clears beSystem +0x2C,
    // some fields after the change.
    stage_active_ = snapshot.stage_active;
    // The rest of the step that started the load (Engine::step).
    pending_stage_ = snapshot.stage;
    begin_stage_load(&snapshot);
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
    mix_value(loader_index_);
    mix_value(loader_skip_);
    mix_value(loader_file_.booked);
    mix_value(stage_active_);
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
            mix_value(p.unique); // a new game's UNIQUE once came from std::random_device
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

// ---------------------------------------------------------------------------
// Stage loader timing
// ---------------------------------------------------------------------------
// The original reads every file of a stage load from the disc while the
// loading screen shows, one at a time, and its load time is the sum of those
// reads and of the updates its state machine needs (Dolphin captures with an
// OSReport log of the loader's states, directory requests and DVD commands,
// work/dref/fix_r22load2: 12 stage loads after the title, the boot's three,
// two in-game changes). The port parses its files at once but its loader
// waits for the same reads on the simulated drive (disc_timing.hpp) and steps
// like the original's updates:
// - A directory request (8006FD34) in an update is read by the loader's
//   ADXSTM (8051B1D0, after the ADX streams' readers in each server pass)
//   from the next server tick on, 64 sectors at a time; the loading thread
//   asks for the next chunk once it has used the last one (chunk_extra). The
//   file is routed (80070130 true, 80070140) in the update after the one
//   that saw its last chunk booked, later by the time the thread spends on
//   it after that (LoaderCost::post). The next request follows in the next
//   update.
// - An update that takes the original longer than a field (the teardown of
//   the outgoing stage, routing a large stage member, registering the stage's
//   infos in state 5, start.lua) covers the following retraces: the loader
//   skips them, and a read it asked for is issued with the last of them.
namespace {
// What the original's loading thread spends on a file besides reading it
// (Dolphin captures of 15 loads, 687 chunks; igAdxFile::read 802AA1B0 asks
// for the next 64 sectors only when the parser wants more data, and its wait
// 802A9DC4 sleeps to the next retrace, so a chunk that takes the thread more
// than the rest of its field delays the next request by whole fields):
// - a chunk before the object buffer (header, types, entries, the reference
//   index): one field more; a chunk inside it (object records of
//   15000-18000 fields) 1.1;
// - the chunk holding the start of the object buffer: the objects are made
//   first, 0.8 + 0.33 per 1000 objects (1079: 1, 8535: 3, 23931: 9, 40382:
//   14; within 1), counted in milli-fields like the rest (the r29 captures
//   at a second BGM phase, where no refill hides it: every stage's /s
//   member, 470-680 objects, takes one field more over its first two
//   chunks, already in the first from about 600 objects on);
// - the chunk where the memory blocks start: two more;
// - memory chunks: none (at times one);
// - after the last chunk the file is ready 0.49 + 0.32 per 1000 memory
//   blocks + 1.13 per MB of them later (111 files: 12 one off);
// - the update that routes the file takes 0.081 fields per 1000 memory
//   blocks (the stage members with 12400 and more cover a second retrace).
struct LoaderCost {
    int object_chunk{-1}; // the chunk holding the start of the object buffer (-1: no objects)
    int memory_chunk{-1}; // the chunk holding the start of the memory blocks (-1: none)
    int stall{};          // milli-fields the objects take
    int post{};
    std::int64_t route{};
};
LoaderCost loader_cost(const IgbDocument* document) {
    LoaderCost cost;
    if (!document) return cost;
    const std::size_t chunk = 64 * 2048;
    const std::size_t objects = document->objects.size(), memories = document->memories.size();
    std::size_t memory_bytes = 0;
    for (const auto& m : document->memories) memory_bytes += m.size;
    const std::size_t memory_start = memories ? document->memories.front().offset : document->consumed_bytes;
    const std::size_t object_start = memory_start - std::min<std::size_t>(memory_start, document->metadata.header.object_buffer_size);
    if (objects) cost.object_chunk = int(object_start / chunk);
    if (memories) cost.memory_chunk = int(memory_start / chunk);
    cost.stall = int(800 + objects * 33 / 100);
    cost.post = int((492 + memories * 32 / 100 + memory_bytes * 1132 / 1000000) / 1000);
    cost.route = std::int64_t(memories) * 81 / 1000;
    return cost;
}

// Milli-fields the loading thread spends on chunk `chunk` of a file beyond
// the field the next request takes anyway; the captures' memory chunks cost a
// field more one time in 13, object chunks one time in 10 (the main thread's
// share of a field varies), which the load's running sum spreads evenly. Over
// both r29 capture sets this predicts 259 of the 302 chunks no refill hides
// (the truncated object cost before: 230), the rest within a field.
std::int64_t chunk_extra(int chunk, int object_chunk, int memory_chunk, int stall) {
    if (object_chunk < 0) return 0;
    if (chunk < object_chunk) return 1000;
    if (chunk == object_chunk) return stall;
    if (memory_chunk < 0 || chunk < memory_chunk) return 1100;
    if (chunk == memory_chunk) return 2000;
    return 80;
}

bool trace_loader() {
    static const bool on = std::getenv("DW4_TRACE_LOADER") != nullptr;
    return on;
}
} // namespace

// The update after NEXTSTAGE (80316258: the managers release the outgoing
// stage's RAMs, 8006F794 its directories) asks for the next descriptor at its
// end; it grows with what the stage's files hold: 1.5 fields + 0.7 per 1000
// memory blocks + 0.18 per 1000 objects (captures: the descriptor request
// 1.54 fields into the update after stagIS09, 8.59 after stagIS10 (5875
// blocks, 14804 objects), 16.25 after stag0203, 20.58 after stag0204; the
// logos, 6.31, take 2 fields longer than this).
std::int32_t Engine::loader_teardown_cost() const {
    if (files_.empty()) return 0;
    std::size_t memories = 0, objects = 0;
    for (const auto& file : files_)
        if (file.document) {
            memories += file.document->memories.size();
            objects += file.document->objects.size();
        }
    return std::int32_t(1500 + memories * 7 / 10 + objects * 18 / 100);
}

// State 5 (802FA53C) registers the stage's infos; the update grows with the
// model RAMs they make: 0.05 fields + 0.0024 per RAM (12 captures, 0.9 fields
// for the lobby's 199 RAMs to 3.7 for 1344, within 0.5).
std::int64_t Engine::loader_register_cost(const std::vector<ObjectView>& infos) const {
    (void)infos;
    const auto* models = get<ModelCtrlManager>();
    return 50 + std::int64_t(models ? models->rams().size() : 0) * 24 / 10;
}

// State 7: TEXTCTL INITIALIZE and start.lua, 0.45 fields + 0.0177 per model
// it generates (9 generated: below 0.6 fields, 78: 1.77; within 0.2).
std::int64_t Engine::loader_start_cost() const {
    std::size_t generated = 0;
    if (const auto* models = get<ModelCtrlManager>())
        for (const auto& ram : models->rams()) generated += ram->generated ? 1 : 0;
    return 450 + std::int64_t(generated) * 177 / 10;
}

void Engine::loader_request(const LoaderItem& item) {
    loader_file_ = {};
    loader_file_.item = item;
    loader_file_.active = true;
    std::optional<ResourceLocation> location;
    const PackageMember* member = item.member >= 0 && std::size_t(item.member) < loader_members_.size()
                                      ? &loader_members_[std::size_t(item.member)] : nullptr;
    if (member) {
        location = member->location;
    } else if (const auto* record = resources_.catalog().find(item.name)) {
        location = record->location;
    }
    std::size_t bytes = 0;
    if (location) {
        loader_file_.archive = unsigned(location->archive_index);
        loader_file_.entry = unsigned(location->entry_index);
        if (location->archive_index < resources_.archives().size() &&
            location->entry_index < resources_.archives()[location->archive_index].entries().size())
            bytes = resources_.archives()[location->archive_index].entries()[location->entry_index].size;
        loader_file_.sectors = std::uint32_t((bytes + 2047) / 2048);
    }
    // The file itself is parsed now and routed when the original has it.
    if (!item.timing_only) {
        try {
            if (member) loader_file_.document = std::make_shared<const IgbDocument>(read_package_member(resources_, *member));
            else loader_file_.document = std::make_shared<const IgbDocument>(read_igb_document(resources_.read(item.name)));
        } catch (const std::exception& error) {
            log("load failed: " + item.name + ": " + error.what());
        }
    }
    const auto cost = loader_cost(loader_file_.document.get());
    loader_file_.parse = cost.post;
    loader_file_.route_cost = cost.route;
    loader_file_.object_chunk = cost.object_chunk;
    loader_file_.memory_chunk = cost.memory_chunk;
    loader_file_.stall = cost.stall;
    if (trace_loader() && std::getenv("DW4_TRACE_LOADER_CHUNKS") && loader_file_.document) {
        // Analysis aid: what each 64-sector chunk of the file holds.
        const auto& doc = *loader_file_.document;
        const std::size_t chunk_bytes = 64 * 2048;
        const std::size_t n = (doc.bytes().size() + chunk_bytes - 1) / chunk_bytes;
        std::vector<std::size_t> objs(n), fields(n), mems(n);
        std::vector<std::map<std::string, std::size_t>> types(n);
        for (const auto& o : doc.objects) {
            if (o.fields.empty()) continue;
            const std::size_t k = (o.fields.front().offset - 8) / chunk_bytes;
            if (k < n) { ++objs[k]; fields[k] += o.fields.size(); }
        }
        for (const auto& m : doc.memories) {
            const std::size_t k = m.offset / chunk_bytes;
            if (k >= n) continue;
            ++mems[k];
            const std::string type = m.memory_type_index >= 0 && std::size_t(m.memory_type_index) < doc.metadata.field_types.size()
                                         ? doc.metadata.field_types[std::size_t(m.memory_type_index)].name : std::string("?");
            types[k][type] += m.size;
        }
        std::string text = "loader chunks " + item.name + ":";
        for (std::size_t k = 0; k < n; ++k) {
            text += " " + std::to_string(k) + "{o" + std::to_string(objs[k]) + " f" + std::to_string(fields[k]) + " m" + std::to_string(mems[k]);
            for (const auto& [type, size] : types[k]) text += " " + type + "=" + std::to_string(size);
            text += "}";
        }
        log(text);
    }
    if (trace_loader())
        log("loader F " + item.name + " bytes " + std::to_string(bytes) + " objects " +
            std::to_string(loader_file_.document ? loader_file_.document->objects.size() : 0) + " object data " +
            "chunk " + std::to_string(loader_file_.object_chunk) + " / memory chunk " + std::to_string(loader_file_.memory_chunk) + ", stall " + std::to_string(loader_file_.stall) + " parse " +
            std::to_string(loader_file_.parse) + " route " + std::to_string(loader_file_.route_cost));
    // Fast loading: the file is there as soon as it is asked for.
    if (loader_fast_) {
        loader_file_.issued = true;
        loader_file_.read = loader_file_.sectors;
    }
}

// The size of a queued file on the disc (fast loading's budget).
std::size_t Engine::loader_item_bytes(const LoaderItem& item) const {
    if (item.timing_only) return 0; // not parsed
    std::optional<ResourceLocation> location;
    if (item.member >= 0 && std::size_t(item.member) < loader_members_.size()) location = loader_members_[std::size_t(item.member)].location;
    else if (const auto* record = resources_.catalog().find(item.name)) location = record->location;
    if (!location || location->archive_index >= resources_.archives().size()) return 0;
    const auto& entries = resources_.archives()[location->archive_index].entries();
    return location->entry_index < entries.size() ? entries[location->entry_index].size : 0;
}

// 80070130 for the pending directory: its last chunk was booked and the
// original processed it.
bool Engine::loader_ready() const {
    if (!loader_file_.active || !loader_file_.issued) return false;
    if (loader_fast_) return true;
    if (loader_file_.sectors == 0 || !get<SoundManager>()) return true;
    if (loader_file_.read < loader_file_.sectors) return false;
    return loader_file_.booked >= 0 && std::int64_t(frame_) >= loader_file_.booked + 1 + loader_file_.parse;
}

// The routing of a ready file (802F9E98 0x802F9EF8..0x802FA100) by queue:
// 0 preliminary (/s members, registered when state 100 ends), 1 stage
// members, 2 common files.
void Engine::loader_route(const LoaderItem& item, int kind) {
    const auto document = loader_file_.document;
    if (item.timing_only || !document) return;
    const bool member = item.member >= 0;
    files_.push_back({member ? stage_name_ + loader_members_[std::size_t(item.member)].specifier : item.name, document, member});
    if (std::getenv("DW4_TRACE_LOAD")) log("load " + files_.back().logical_path);
    if (!document->info_list_reference) return;
    for (const auto& element : document->object_list(*document->info_list_reference)) {
        if (!element) continue;
        ObjectView info(document.get(), *element);
        if (kind == 0) loader_preliminary_.push_back(info);
        else if (info.type() == "beLuaInfo") lua_infos_.push_back(info);
        else deferred_infos_.push_back(info);
    }
}

// One call of 802F9E98 over the current queue: true once every file is
// routed; otherwise requests the next file or routes the pending one.
bool Engine::loader_queue_step(int kind) {
    if (loader_fast_) return loader_queue_fast(kind);
    if (loader_index_ >= loader_queue_.size()) return true;
    if (!loader_file_.active) {
        loader_request(loader_queue_[loader_index_]);
        return false;
    }
    if (!loader_ready()) return false;
    if (trace_loader()) log("loader R " + loader_file_.item.name);
    loader_cost_ += loader_file_.route_cost;
    loader_route(loader_file_.item, kind);
    loader_file_ = {};
    ++loader_index_;
    return false;
}

// Fast loading's 802F9E98: the queue's files in their order, each asked for
// (parsed) and routed in the same update, more of them while the files this
// update took stay within kFastLoadBytes; true once every file is routed
// (in the update that routes the last one).
bool Engine::loader_queue_fast(int kind) {
    std::size_t bytes = 0;
    while (loader_index_ < loader_queue_.size()) {
        const std::size_t size = loader_item_bytes(loader_queue_[loader_index_]);
        if (bytes > 0 && bytes + size > kFastLoadBytes) return false;
        bytes += size;
        loader_request(loader_queue_[loader_index_]);
        if (trace_loader()) log("loader R " + loader_file_.item.name);
        loader_route(loader_file_.item, kind);
        loader_file_ = {};
        ++loader_index_;
    }
    return true;
}

void Engine::process_stage_loader() {
    if (loader_state_ == -1) return;
    // Fast loading: an update every field, nothing waits for the drive.
    if (loader_fast_) {
        loader_skip_ = 0;
        loader_cost_ = 0;
        const int before = loader_state_;
        loader_update();
        if (trace_loader() && loader_state_ != before) log("loader state " + std::to_string(loader_state_));
        return;
    }
    auto* sound = get<SoundManager>();
    // The read on its way: the frame that first sees its last chunk booked
    // (the driver tick after the previous step's update booked it).
    if (loader_file_.issued && loader_file_.booked < 0 && (!sound || !sound->loading())) loader_file_.booked = std::int64_t(frame_);
    // The original's update is still running: no update for this retrace.
    if (loader_skip_ > 0) --loader_skip_;
    else {
        loader_cost_ = 0;
        const int before = loader_state_;
        loader_update();
        loader_skip_ = int(loader_cost_ / 1000);
        if (trace_loader() && loader_state_ != before) log("loader state " + std::to_string(loader_state_));
        if (trace_loader() && loader_skip_ > 0) log("loader update covers " + std::to_string(loader_skip_) + " more fields");
    }
    if (!loader_file_.active) return;
    // A request is read from the server tick after the update that made it
    // (a long update: with the last retrace it covers), chunk by chunk.
    if (!loader_file_.issued) {
        if (loader_skip_ > 0) return;
        loader_file_.issued = true;
        if (!sound || !loader_file_.sectors) {
            loader_file_.read = loader_file_.sectors;
            return;
        }
        if (loader_file_.raw_offset) {
            sound->read_disc(*loader_file_.raw_offset, loader_file_.sectors);
            loader_file_.read = loader_file_.sectors;
        } else {
            loader_file_.read = sound->read_entry(*this, loader_file_.archive, loader_file_.entry, 0, 64);
        }
        return;
    }
    // The next chunk: from the tick after the booking, later by the whole
    // fields the loading thread spends on the booked chunk (chunk_extra; the
    // load's running sum keeps their fractions).
    if (loader_file_.read >= loader_file_.sectors || loader_file_.booked < 0 || !sound) return;
    const int chunk = int((loader_file_.read - 1) / 64);
    if (!loader_file_.chunk_costed) {
        const std::int64_t before = loader_processing_ / 1000;
        loader_processing_ += chunk_extra(chunk, loader_file_.object_chunk, loader_file_.memory_chunk, loader_file_.stall);
        loader_file_.wait = loader_processing_ / 1000 - before;
        loader_file_.chunk_costed = true;
    }
    const std::int64_t wait = loader_file_.wait;
    if (std::int64_t(frame_) < loader_file_.booked + wait) return;
    loader_file_.booked = -1;
    loader_file_.chunk_costed = false;
    loader_file_.read += sound->read_entry(*this, loader_file_.archive, loader_file_.entry, loader_file_.read, 64);
}

void Engine::loader_update() {
    switch (loader_state_) {
    case kLoaderBoot: {
        // The session's first load: the boot loader's reads before the first
        // descriptor (every capture): ndmw.igb (53 sectors) at the first
        // update, routed 3 fields after its booking; 2 fields later the CRI
        // AFS partitions read their directories one sector at a time, each
        // asked for with the booking of the one before: ndmw.afs from its
        // start (4 sectors; every capture: 0x2dbfb298.., ADXSTM positions
        // 0..3), one field later area.afs from its start (6); the descriptor
        // request follows 3 fields after the last booking.
        struct BootRead {
            const char* archive; // nullptr: ndmw.igb
            std::uint32_t sector;
            int ready;           // fields from the booking to the next request
        };
        static constexpr BootRead kBoot[] = {{nullptr, 0, 5},       {"ndmw.afs", 0, 0}, {"ndmw.afs", 1, 0}, {"ndmw.afs", 2, 0},
                                             {"ndmw.afs", 3, 1},    {"area.afs", 0, 0}, {"area.afs", 1, 0}, {"area.afs", 2, 0},
                                             {"area.afs", 3, 0},    {"area.afs", 4, 0}, {"area.afs", 5, 3}};
        // Fast loading: these reads (timing only) take no time.
        if (loader_fast_) loader_boot_step_ = int(std::size(kBoot));
        if (loader_file_.active) {
            if (!loader_ready()) return;
            loader_file_ = {};
            ++loader_boot_step_;
        }
        if (loader_boot_step_ >= int(std::size(kBoot))) {
            loader_state_ = kLoaderDescriptor;
            loader_update();
            return;
        }
        const auto& read = kBoot[loader_boot_step_];
        loader_file_.active = true;
        loader_file_.item.name = read.archive ? std::string(read.archive) + " directory" : std::string("ndmw.igb");
        loader_file_.sectors = read.archive ? 1 : 53;
        loader_file_.parse = read.ready - 1;
        std::uint64_t offset = 0x5703c298; // GDJEB2's ndmw.igb without the disc's FST
        if (!read.archive) {
            if (const auto known = resources_.catalog_disc_offset()) offset = *known;
        } else if (auto* sound = get<SoundManager>()) {
            for (unsigned i = 0; i < resources_.catalog().archives.size(); ++i) {
                std::string name = resources_.catalog().archives[i].filename;
                for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (name == read.archive) offset = sound->archive_offset(*this, i);
            }
            offset += std::uint64_t(read.sector) * 2048;
        }
        loader_file_.raw_offset = offset;
        if (trace_loader()) log("loader boot read " + loader_file_.item.name);
        return;
    }
    case kLoaderDescriptor: {
        // beSystem (80316258): the update after NEXTSTAGE releases the old
        // stage and asks for <stage>.igb (0x803165A0); once that directory is
        // ready (0x80316674) its beFileListInfo starts the file-list manager,
        // whose update follows in the same frame (state 0).
        if (!loader_file_.active) {
            loader_request({stage_name_ + ".igb"});
            loader_cost_ += loader_teardown_;
            return;
        }
        if (!loader_ready()) return;
        loader_file_ = {};
        try {
            const auto descriptor = load_package_descriptor(resources_, stage_name_ + ".igb");
            log("stage " + stage_name_ + " section " + descriptor.section + ", " + std::to_string(descriptor.members.size()) + " members");
            loader_members_ = descriptor.members;
        } catch (const std::exception& error) {
            log(std::string("stage load failed: ") + error.what());
            loader_state_ = -1;
            return;
        }
        loader_state_ = 0;
        [[fallthrough]];
    }
    case 0: {
        // 802F8A64 state 0 clears beSystem +0x2C (_isActive, 0x802F8B54):
        // no manager updates until state 8.
        stage_active_ = false;
        // loadingUSA/ld<stage name from its third letter>.igb shows while
        // the stage loads (its scene with a beLoadingNode, later the LOADING
        // layer): asked for at 0x802F8D54, routed in state 1.
        loader_state_ = 1;
        if (stage_name_.size() > 2) {
            const std::string path = "loadingusa/ld" + stage_name_.substr(2) + ".igb";
            if (const auto* record = resources_.catalog().find(path); record && record->location) loader_request({path});
        }
        return;
    }
    case 1: {
        if (loader_file_.active) {
            if (!loader_ready()) return;
            // 0x802F8DE0: the card's infos are registered at once.
            if (const auto document = loader_file_.document) {
                files_.push_back({loader_file_.item.name, document, false});
                if (document->info_list_reference)
                    for (const auto& element : document->object_list(*document->info_list_reference)) {
                        if (!element) continue;
                        ObjectView info(document.get(), *element);
                        if (info.type() == "beLuaInfo") lua_infos_.push_back(info);
                        else register_info(info);
                    }
            }
            loading_node_ = find_loading_node(&global_root_);
            loader_file_ = {};
        }
        // 0x802F8F30: the first load of a session also reads BecText.igb and
        // _common/sbiallse.igb (the port loads its own copies when used);
        // then the preliminary "/s" members (state 100), the rest in state 2.
        loader_queue_.clear();
        loader_index_ = 0;
        if (loader_common_) {
            loader_queue_.push_back({"bectext.igb", -1, true});
            loader_queue_.push_back({"_common/sbiallse.igb", -1, true});
            loader_common_ = false;
        }
        for (std::size_t i = 0; i < loader_members_.size(); ++i)
            if (loader_members_[i].suffix == "/s") loader_queue_.push_back({loader_members_[i].specifier, int(i)});
        loader_state_ = loader_queue_.empty() ? 102 : 100;
        return;
    }
    case 100:
        if (!loader_queue_step(0)) return;
        // 802FA53C registers the preliminary infos; with a SOUND RAM among
        // them (SOUND SEARCH, 0x802F9524) the loader sends MAKEBINLIST and
        // goes to state 101, without one straight to 102.
        for (const auto& info : loader_preliminary_) register_info(info);
        loader_preliminary_.clear();
        loader_state_ = messenger_.count("SOUND", std::nullopt, -1) != 0 ? 101 : 102;
        return;
    case 101:
        // SOUND ISDONETRANS (0x802F957C) holds the state until it answers 1;
        // every capture spends one update here.
        loader_state_ = 102;
        return;
    case 102:
        loader_queue_.clear();
        loader_index_ = 0;
        for (std::size_t i = 0; i < loader_members_.size(); ++i)
            if (loader_members_[i].suffix != "/s") loader_queue_.push_back({loader_members_[i].specifier, int(i)});
        loader_state_ = 2;
        return;
    case 2:
        if (!loader_queue_step(1)) return;
        loader_state_ = 3;
        return;
    case 3:
        install_lua();
        run_script("func.lua");
        run_script("boot.lua");
        // State 4 reads the common file list boot.lua made (system +0x34).
        loader_queue_.clear();
        loader_index_ = 0;
        for (const auto& path : common_queue_) loader_queue_.push_back({path});
        common_queue_.clear();
        loader_state_ = 4;
        return;
    case 4: {
        // State 4 (802F973C) ends once the queued files are routed (802F9E98)
        // and the loading screen played its minimum time (80311674; not with
        // fast loading); then it holds its lock time (80311660).
        const bool queued = loader_queue_step(2);
        const bool finished = !loading_node_ || loader_fast_ || loading_node_->anim->finished();
        if (!queued || !finished) return;
        if (loading_node_) loading_node_->anim->fixed = loading_node_->anim->lock;
        loader_state_ = 20;
        return;
    }
    case 20:
        loader_state_ = 21;
        return;
    case 21:
        loader_state_ = 5;
        return;
    case 5: {
        auto infos = std::move(deferred_infos_);
        deferred_infos_.clear();
        if (trace_loader()) {
            std::map<std::string, int> types;
            for (const auto& info : infos) ++types[std::string(info.type())];
            std::string text = "loader state 5 registers " + std::to_string(infos.size()) + " infos:";
            for (const auto& [type, count] : types) text += " " + type + "=" + std::to_string(count);
            log(text);
        }
        for (const auto& info : infos) register_info(info);
        loader_cost_ += loader_register_cost(infos);
        if (trace_loader())
            if (auto* models = get<ModelCtrlManager>()) log("loader state 5 model RAMs " + std::to_string(models->rams().size()));
        loader_state_ = 6;
        return;
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
        return;
    }
    case 7: {
        Packet packet;
        messenger_.send(*this, "TEXTCTL", std::nullopt, -1, "INITIALIZE", packet);
        run_script("start.lua");
        loader_cost_ += loader_start_cost();
        if (trace_loader())
            if (auto* models = get<ModelCtrlManager>()) {
                std::size_t generated = 0;
                for (const auto& ram : models->rams()) generated += ram->generated ? 1 : 0;
                log("loader start.lua generated " + std::to_string(generated) + " of " + std::to_string(models->rams().size()) + " model RAMs");
            }
        loader_state_ = 8;
        return;
    }
    case 8:
        // The step after start.lua sets beSystem +0x2C (0x802F9B68): the
        // managers run from this step on (after the loader, 8028B7B0), so
        // the RAMs start.lua created get their first update here.
        stage_active_ = true;
        loader_state_ = 9;
        return;
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
        return;
    default:
        loader_state_ = -1;
        return;
    }
}

namespace {
// Test tool: DW4_CLOCK_FILE=<path> with lines "<frame> <world ns>" sets the
// world clock to that value in the step that reaches the frame (other frames
// keep adding one field). With the times a Dolphin run logged (an AR hook on
// the world clock update, 0x8040F490 bl 8028AF84, calling OSReport) the port
// sees the original's sampled clock, jitter and phase included: the named RAM
// timers quantise it to 2^18 ns (AITIMER 8031E238, KEIKATIMER 8031E4C4), so
// AI decisions and the channel, turn and MUTEKI steps follow its phase.
const std::map<std::uint64_t, std::int64_t>& clock_file() {
    static const std::map<std::uint64_t, std::int64_t> table = [] {
        std::map<std::uint64_t, std::int64_t> out;
        if (const char* path = std::getenv("DW4_CLOCK_FILE")) {
            if (std::FILE* f = std::fopen(path, "r")) {
                unsigned long long frame = 0;
                long long world = 0;
                while (std::fscanf(f, "%llu %lld", &frame, &world) == 2) out[frame] = world;
                std::fclose(f);
            }
        }
        return out;
    }();
    return table;
}
} // namespace

void Engine::step(const std::array<PadSample, 4>& samples) {
    // The console reloads the game (rebooting): nothing of the old one runs.
    if (rebooting_) {
        ++frame_;
        ++reboot_fields_;
        return;
    }
    auto input = samples;
    // STAGECTL PADINPUT DISABLE: the game sees released controls.
    if (!pad_input_enabled_)
        for (auto& sample : input) { sample.buttons = 0; sample.left = {}; sample.right = {}; }
    try { pads_.update(input); } catch (const std::exception& error) { note(std::string("pad input: ") + error.what()); }
    // The frame clock advances first so updates and the following render
    // observe the same time (joint clamps and fades end exactly on a key).
    // One NTSC field of world time (8028AF84), see field_time.
    const auto before = time_;
    time_ += field_time(frame_ + 1) - field_time(frame_);
    ++frame_;
    if (const auto& clock = clock_file(); !clock.empty())
        if (const auto it = clock.find(frame_); it != clock.end()) time_ = it->second;
    // 80316258 keeps the world time of its last update (+0x18) and stores the
    // difference as the frame step (+0x28) each frame (system_step).
    world_delta_ = time_ - before;
    // beSystem's update (80316258) first reads the reset button (the
    // controllers' combination presses it, ConsoleReset); its latched release
    // blackens the picture and restarts the disc (OSResetSystem 0x80316304),
    // nothing after it runs.
    if (console_reset_.update(samples)) {
        rebooting_ = true;
        reboot_fields_ = 0;
        log("reset button: the console reboots");
        return;
    }
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
//
// Every manager class (the 51 vtables deriving from the info manager base,
// beAction2 .. beNDMWMdl*) has the shared traversal 802F28F8 at +0x74: it
// calls the RAM update (+0x84) of each RAM only while 80316808 says the
// stage is active (beSystem +0x2C set and +0x2D clear), the manager's +0x1C
// is set and beSystem +0x2E is clear. +0x2D is only set by beSystem's
// reset hook (vtable +0x70, 8031624C) when the application reloads every
// module (8028A87C) and cleared by its next update (803167E4); nothing in
// the retail code writes +0x2E; a manager's +0x1C is cleared only while a
// disc error freezes the game (80316974). The modules outside that base keep
// running: beSystem, bePadManager, beMessenger, beDemoManager, beCameraMode
// (CameraManager::begin_frame), beCri (the sound driver) and the loader.
void Engine::update_managers() {
    for (auto& m : managers_)
        if (m->scheduled_at_registration()) m->update(*this);
    if (!stage_active_) {
        for (auto& m : managers_)
            if (!m->scheduled_at_registration()) m->update_inactive(*this);
        return;
    }
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

TraversalContext Engine::frame_context(std::vector<DrawItem>* out, FrameCamera& camera, Vec4f& clear_color) {
    clear_color = {0, 0, 0, 1};
    if (auto* bk = get<BkColorManager>()) clear_color = bk->color();
    if (auto* cam = get<CameraManager>()) cam->frame_camera(*this, camera);
    TraversalContext context;
    // The stage camera's igFrustCullTraversal (8040B3EC) traverses the
    // whole frame (ROOTATTR: scenes and 2D layers) with the game's camera.
    context.cull = true;
    context.cull_camera = camera;
    context.picture_cull = !debug_camera;
    context.picture_widen = picture_widen;
    if (debug_camera) camera = *debug_camera;
    context.cache = &scenes_;
    context.time = time_;
    context.camera = camera;
    context.output = out;
    if (auto* models = get<ModelCtrlManager>()) models->install_actor_hook(*this, context);
    if (auto* actions = get<ActionManager>())
        context.actor_pose = [this, actions](const Node& actor, bool traversed) {
            return traversed ? actions->pose_for(actor, time_, true) : actions->peek_pose(actor, time_);
        };
    Mat4 inverse_view;
    if (invert(camera.view, inverse_view)) context.camera_position = {inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
    return context;
}

void Engine::traverse_scene() {
    // As render(): the same nodes in the same order with the same camera,
    // time and poses; only the draw items (geometry, particle quads, skin
    // palettes, lights) are left out.
    if (rebooting_) return;
    FrameCamera camera;
    Vec4f clear_color;
    TraversalContext context = frame_context(nullptr, camera, clear_color);
    traverse(&global_root_, context);
    if (auto* shadows = get<ShadowManager>()) shadows->traverse(context);
}

void Engine::render(std::vector<DrawItem>& out, FrameCamera& camera, Vec4f& clear_color, ShadowFrame* shadow) {
    out.clear();
    // VISetBlack (0x803162E0): a rebooting console shows black.
    if (rebooting_) {
        clear_color = {0, 0, 0, 1};
        if (shadow) shadow->active = false;
        return;
    }
    TraversalContext context = frame_context(&out, camera, clear_color);
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
