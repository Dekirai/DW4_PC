#include "dw4/engine/engine.hpp"
#include "online_detail.hpp"
#include "dw4/engine/online_membership.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/packages.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
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
    traced_no_recipient_.clear();
    // 803040C0 releases the message objects (+0x10, the records) and clears
    // the made flag (+0x30): the next select or post makes new ones (80303C0C).
    objects_.clear();
}

void MessageObjects::clear() {
    objects_.clear();
    idle_.clear();
    current_ = 0;
    made_ = false;
}

void MessageObjects::select() {
    // 80305344 / 80305F9C (0x80305364 / 0x80305FBC): made, the current
    // object's counts are reset (80304618); otherwise 80303C0C makes the
    // current object and the records, each with its own.
    if (made_) {
        objects_[current_].bools = 0;
        return;
    }
    if (static const bool trace = std::getenv("DW4_TRACE_MESSAGE_OBJECTS") != nullptr; trace) std::printf("M\n");
    objects_.assign(1 + kRecords, Object{});
    current_ = 0;
    idle_.clear();
    for (std::size_t i = 1; i <= kRecords; ++i) idle_.push_back(i);
    made_ = true;
}

void MessageObjects::append(std::uint8_t value) {
    if (!made_) return;
    auto& object = objects_[current_];
    if (object.bools == 0) object.first_bool = value;
    ++object.bools;
}

std::size_t MessageObjects::commit() {
    // 80305F9C takes the last record of the list, a new one (802CF068, with
    // a fresh object) when that one is in use; 803061BC swaps.
    std::size_t record;
    if (idle_.empty()) {
        objects_.push_back(Object{});
        record = objects_.size() - 1;
    } else {
        record = idle_.back();
        idle_.pop_back();
    }
    const std::size_t filled = current_;
    current_ = record;
    return filled;
}

std::size_t MessageObjects::deliver(std::size_t object) {
    objects_[current_].bools = 0; // 0x80304D78: the select
    const std::size_t previous = current_;
    current_ = object;
    return previous;
}

void MessageObjects::retire(std::size_t previous) {
    objects_[previous].bools = 0;
    idle_.push_back(previous);
}

void MessageObjects::end_pass() {
    if (made_) objects_[current_].bools = 0;
}

namespace {
bool trace_message_objects() {
    static const bool trace = std::getenv("DW4_TRACE_MESSAGE_OBJECTS") != nullptr;
    return trace;
}
bool trace_sends() {
    static const bool trace = std::getenv("DW4_TRACE_SENDS") != nullptr;
    return trace;
}
void print_object(const MessageObjects::State& state) {
    std::printf(" %zu %d %u", state.current, state.bools, unsigned(state.first_bool));
}
} // namespace

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
    // The select (80305344) resets the current message object, the sender's
    // argument bools go into it (80306964), then the dispatch (80305A28).
    const bool model = modelling();
    // DW4_TRACE_SENDS: every modelled send in the format of the Dolphin X /
    // C / B logs (hooks 0x80305354 select, 0x80305A38 dispatch, 0x8030697C
    // bool), the bools as the message object takes them.
    if (model && trace_sends()) {
        std::printf("[%llu] X %.*s %s %08x\n[%llu] C %.*s\n", static_cast<unsigned long long>(engine.frame()), int(category.size()),
                    category.data(), name ? std::string(*name).c_str() : "-", unsigned(index), static_cast<unsigned long long>(engine.frame()),
                    int(command.size()), command.data());
        for (const auto value : packet.bools) std::printf("[%llu] B %08x\n", static_cast<unsigned long long>(engine.frame()), unsigned(value));
    }
    if (model) {
        objects_.select();
        for (const auto value : packet.bools) objects_.append(value);
    }
    return dispatch(engine, category, name, index, command, packet, model);
}

std::size_t Messenger::dispatch(Engine& engine, std::string_view category, std::optional<std::string_view> name,
                                std::int32_t index, std::string_view command, Packet& packet, bool model) {
    if (command == "SEARCH") {
        // 0x80305AE0..0x80305C40: a bool (any row) and the count answer.
        const auto n = count(category, name, index);
        packet.bools.push_back(n != 0);
        packet.ints.push_back(static_cast<std::int32_t>(n));
        if (model) objects_.append(n != 0);
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
        // 80305A28 calls no handler when no row fits: the message is
        // dropped, its reply lists stay empty and the sender reads "no
        // answer" (8030690C / 80306A30 = 0); sd returns nothing to a script
        // either way. Every such message of the story, the exit survey (1-4
        // players) and the stage loads is the original's too: the stage has
        // no info, child or RAM index of that name (scripts shared between
        // stages, more GENERATEs than RAMs, typos such as g_bush02z, the
        // lobby's HUD without i_stpnl05, stag0103's boat meter instead of
        // hpmt / hpnm, stag0203 without g_000bcaps or weapon packages), or a
        // fixed sender meets a stage without that manager (the loader's
        // TEXTCTL INITIALIZE 0x802F9B0C, the quick panel's SUBWND ISRUN
        // 0x8034B28C, the SUBWND programs' GENERATER ONFREEZE 0x8034FC1C).
        // DW4_TRACE_NO_RECIPIENT=1 lists each once per stage.
        static const bool trace = std::getenv("DW4_TRACE_NO_RECIPIENT") != nullptr;
        if (trace) {
            std::string text = std::string(category) + " " + std::string(command) + (name ? " (" + std::string(*name) + ")" : std::string());
            if (traced_no_recipient_.insert(text).second) engine.log("no recipient: " + text);
        }
        return 0;
    }
    for (auto* ram : recipients) {
        if (!ram || !ram->manager) continue;
        // A handler's answers go into the message object current when it
        // answers (a nested send resets and refills it first, a nested post
        // swaps another one in).
        const std::size_t before = packet.bools.size();
        if (!ram->manager->command(engine, *ram, command, packet)) ram->manager->fallback(engine, *ram, command, packet);
        if (model)
            for (std::size_t i = before; i < packet.bools.size(); ++i) {
                if (trace_sends()) std::printf("[%llu] B %08x\n", static_cast<unsigned long long>(engine.frame()), unsigned(packet.bools[i]));
                objects_.append(packet.bools[i]);
            }
    }
    return recipients.size();
}

void Messenger::post(std::int64_t time, std::string category, std::optional<std::string> name, std::int32_t index,
                     std::string command, Packet arguments) {
    // 80305F9C prepares the message in the pool's last record and makes a new
    // one when that record is still in use (every record holds a message).
    if (modelling()) {
        const auto in_use = static_cast<std::size_t>(std::count_if(
            deferred_.begin(), deferred_.end(), [](const DeferredMessage& m) { return m.object != DeferredMessage::kPortOnly; }));
        if (in_use >= deferred_records_) ++deferred_records_;
    }
    // DW4_TRACE_POSTS=1: every posted message with its delivery time.
    if (static const bool trace = std::getenv("DW4_TRACE_POSTS") != nullptr; trace)
        std::printf("post %s %s %s for %lld\n", category.c_str(), name ? name->c_str() : "-", command.c_str(), static_cast<long long>(time));
    DeferredMessage message{time, std::move(category), std::move(name), index, std::move(command), std::move(arguments)};
    message.trigger = trigger_;
    if (modelling()) {
        // 80305F9C resets the current object (or makes all), the poster's
        // arguments go into it, 803061BC swaps it into the record.
        objects_.select();
        for (const auto value : message.arguments.bools) objects_.append(value);
        const auto before = objects_.state();
        message.object = objects_.commit();
        if (trace_message_objects()) {
            std::printf("S");
            print_object(objects_.state());
            print_object(before);
            std::printf("\n");
        }
    }
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
        end_pass(engine);
        return;
    }
    // Otherwise the head is delivered while its time is not after now, at
    // most as many times as the pool had records when the pass began: a
    // message a delivery posts for now usually still arrives in this pass.
    std::size_t budget = deferred_records_;
    // (The port-only messages due now, at most the ones queued now: a
    // port-only delivery that posts for now again waits for the next pass.)
    std::size_t port_only_budget = deferred_.size();
    while (budget > 0 && !deferred_.empty() && deferred_.front().time <= now) {
        if (deferred_.front().object == DeferredMessage::kPortOnly) {
            if (port_only_budget == 0) break;
            --port_only_budget;
        } else {
            --budget;
        }
        DeferredMessage message = std::move(deferred_.front());
        deferred_.erase(deferred_.begin());
        const Engine::TriggerScope trigger(engine, message.trigger);
        std::optional<std::string_view> name;
        if (message.name) name = *message.name;
        if (static const bool trace = std::getenv("DW4_TRACE_POSTS") != nullptr; trace)
            std::printf("[%llu] deliver %s %s (due %lld, now %lld)\n", static_cast<unsigned long long>(engine.frame()),
                        message.category.c_str(), message.command.c_str(), static_cast<long long>(message.time), static_cast<long long>(now));
        if (objects_.holds(message.object)) {
            // 0x80304D78..0x80304E58: select, the delivered object becomes
            // current for the dispatch (its arguments in it) and stays so;
            // the record keeps the previous one (80304A04, to the list end).
            if (trace_message_objects()) {
                std::printf("D");
                print_object(objects_.state(message.object));
                print_object(objects_.state());
                std::printf("\n");
            }
            const std::size_t previous = objects_.deliver(message.object);
            dispatch(engine, message.category, name, message.index, message.command, message.arguments, true);
            objects_.retire(previous);
        } else {
            // A message posted inside a port-only scope: no message object.
            dispatch(engine, message.category, name, message.index, message.command, message.arguments, false);
        }
    }
    end_pass(engine);
}

void Messenger::end_pass(const Engine& engine) {
    // 0x80304F8C: every pass of a made messenger ends with 80304618 on the
    // current object.
    if (trace_message_objects() && objects_.made()) {
        std::printf("Z");
        print_object(objects_.state());
        std::printf(" @%llu\n", static_cast<unsigned long long>(engine.frame()));
    }
    objects_.end_pass();
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
    current_->trigger = trigger_;
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
        const Engine::TriggerScope trigger(engine, work->trigger);
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
            } else if (record.type == 8) {
                block = record.port && record.port(engine) != 0;
            } else if (record.type == 9) {
                const std::int32_t value = record.port ? record.port(engine) : 0;
                while (cursor < records.size()) {
                    ++cursor;
                    if (cursor >= records.size()) break;
                    if (records[cursor].type == 4 && records[cursor].case_value == value) break;
                    if (records[cursor].type == 5) break;
                }
            } else if (record.type >= 0 && record.type <= 3) {
                Packet packet;
                if (record.argument) parse_packet_arguments(*record.argument, packet);
                std::optional<std::string_view> name;
                if (record.name) name = *record.name;
                if (record.delay_frames > 0) {
                    // (Online: a departure posted ahead is a solo
                    // instance's heading, Engine::note_departure.)
                    if (record.command == "NEXTSTAGE" && record.category == "STAGECTL" && !packet.strings.empty() && engine.online().enabled) {
                        int point = 0;
                        for (std::size_t k = cursor; k-- > 0;)
                            if (records[k].category == "PLAYERPOINT" && records[k].command == "SETGENERATEPOS" && records[k].argument) {
                                Packet at;
                                parse_packet_arguments(*records[k].argument, at);
                                if (!at.ints.empty()) point = int(at.ints[0]);
                                break;
                            }
                        engine.note_departure(packet.string(0), point, record.delay_frames);
                    }
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
Engine::Engine(const std::filesystem::path& data_root) : Engine(MountedResources::open(data_root)) {}

Engine::Engine(MountedResources resources) : resources_(std::move(resources)) {
    detail::engine_constructed();
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

Engine::~Engine() { detail::engine_destroyed(counted_online_); }

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

// The application loop's module reload (8028A87C, Engine::step) first calls
// the +0x70 reset of each of its 63 modules in the module list's order, then
// deletes every info of the application's list +0x10 (80066e1c at
// 0x8028A910). What the port has of the resets:
// - igMovieManager 802AF560 (module 3): every movie stops (SoundManager::
//   stop_movie);
// - beSystem 8031624C (4): +0x2D set, so no manager updates until its next
//   update (none runs in the reload anyway);
// - beMessenger 80304C38 -> 803040C0 (6): registrations and pending
//   messages go;
// - beCri 802F523C (7): the effect of a beCriFxInfo named NONE (802F6F10,
//   effects off), its beCriFxInfo list released, the stage's banks unloaded
//   (802F6BAC at 0x802F53A0); beSound (15, 802F2628 with its hook 80314904)
//   and beMovie (19, 8031EB48): their RAMs (SoundManager::clear_stage);
// - beDemoManager 802F7D0C (13): the demo works go;
// - the info managers' shared reset 802F2628 (beStaticInfoCtl, beDBManager,
//   beBkColor .. beNDMWMdlEnemy): the class's hook (+0x88), the RAMs and
//   their scheduler entries (8028A400) go (each Manager::clear_stage).
// The reload runs no update and keeps the picture, so of all this only the
// sound is heard before the next update (the effect's reverb tail, the
// stage banks' loops; Dolphin d_ngsr1: the DSP output ends in the field
// after the reload's start); the directories go with beSystem's next update
// (begin_stage_load).
void Engine::reset_modules() {
    for (auto& m : managers_) m->clear_stage(*this);
    schedule_.clear();
    messenger_.clear();
    demo_.clear();
    // Seamless co-op: a running exit ends with the stage.
    exit_port_.reset();
    exit_view_ = ExitPromptView{};
}

void Engine::begin_stage_load(const StageSnapshot* restoring) {
    const std::string next = pending_stage_;
    pending_stage_.clear();
    log("stage change -> " + next);
    // This is the first update after the application loop's module reload
    // (Engine::step, loader_teardown_cost): its world clock sample came late
    // by the reload's fraction of a field, the next one is on time again.
    // The module resets ran at the reload's start (reset_modules); beSystem's
    // update now releases the outgoing stage's directories (8006F794 at
    // 0x8031658C): its documents, scenes and scripts go. A save state keeps
    // the reload's length (informational).
    reloading_ = false;
    if (restoring) {
        loader_teardown_ = restoring->loader_teardown;
        loader_common_ = restoring->loader_common;
        // (A restore starts a new engine with the snapshot's sound state;
        // the resets follow as in the live run, whose snapshot was taken
        // after them.)
        reset_modules();
    } else {
        clock_lateness_ = 0;
    }
    global_root_.children.clear();
    for (auto& file : files_) retired_.push_back(file.document);
    for (auto& file : files_) scenes_.release_document(file.document.get());
    files_.clear();
    retired_.clear();
    deferred_infos_.clear();
    lua_infos_.clear();
    common_queue_.clear();
    randomizer_pool_.clear();
    randomizer_donors_.clear();
    donor_infos_.clear();
    donor_starters_.clear();
    donor_scripts_.clear();
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
    // Online (6.5, 4.4, 4.6): every barrier clears the boss lock; a visit
    // that starts with one member has the shared camera and the leash.
    if (online_.enabled && !restoring) {
        online_.boss_lock = false;
        if (std::popcount(unsigned(online_.present)) < 2) {
            online_.player_cameras = 0;
            online_.leash_off = false;
        }
    }
    // The load timing of this load and the stage it starts (a save state's
    // cheats are in place by now).
    loader_fast_ = cheats_.fast_loading;
    if (loader_fast_ && std::getenv("DW4_TRACE_LOADER")) log("loader: fast loading");
    // What survives into this stage, for save states.
    StageSnapshot snapshot;
    snapshot.stage = next;
    snapshot.time = time_;
    snapshot.clock_lateness = applied_lateness_;
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
    snapshot.online = online_;
    snapshot.personal = personal_;
    snapshot.serial = ++snapshot_serial_;
    snapshot_ = std::move(snapshot);
}

void Engine::restore(const StageSnapshot& snapshot) {
    // The load step's managers run here too (6.12: every entry into the
    // simulation under the MXCSR the engine expects; a no-op by default).
    const detail::MxcsrGuard mxcsr;
    time_ = snapshot.time;
    frame_ = snapshot.frame;
    update_frame_ = frame_; // (a stage load starts in a step that updated)
    hold_fields_ = 0;
    held_ = false;
    applied_lateness_ = snapshot.clock_lateness;
    clock_lateness_ = 0;
    world_delta_ = frame_delta(); // the field that step added (no stage is active in it)
    flags_ = snapshot.flags;
    counters_ = snapshot.counters;
    pad_input_enabled_ = snapshot.pad_input_enabled;
    cheats_ = snapshot.cheats;
    pads_ = snapshot.pads;
    console_reset_ = snapshot.console_reset;
    // Online: the visit's rules and personal values (set_online keeps the
    // room's cheats and the console reset off, as after any construction).
    personal_ = snapshot.personal;
    if (snapshot.online.enabled || online_.enabled) {
        set_online(snapshot.online);
        personal_ = snapshot.personal;
    }
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
    if (auto* camera = get<CameraManager>()) camera->begin_frame(*this);
    messenger_.update(*this);
    process_stage_loader();
    demo_.update(*this);
    apply_leash_rule(); // as the live load step (a no-op offline and while the leash is on)
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

// DW4_TRACE_INFOS=1 logs every registered info (type, name, RAM count and the
// manager that took it).
void Engine::register_info(const ObjectView& info) {
    static const bool trace = std::getenv("DW4_TRACE_INFOS") != nullptr;
    // 8028A7DC offers the info to every module's +0x6C (802F251C) until one
    // takes it; a module takes only infos whose meta is exactly its own
    // (+0x80 == the info's +0x58). No module has the plain igInfo meta (its
    // pointer r13-0x3508 is read only by igInfo's own class functions
    // 8002E9C0 / 8002EB4C and the loader), so a plain igInfo (the nameless
    // one without RAMs in stagIS08 / 10 / 12 / 13) is dropped: no rows, no
    // scheduled module.
    if (info.type() == "igInfo") {
        if (trace) log("info igInfo " + std::string(info.str(2)) + " taken by no module");
        return;
    }
    bool accepted = false;
    for (auto& m : managers_) {
        if (m->accepts(info)) {
            if (trace)
                log("info " + info.type() + " " + std::string(info.str(2)) + " (" + std::to_string(info.list(6).size()) +
                    " RAMs) -> " + m->type_name());
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
// - A directory request (8006FD34) in an update hands the file to the
//   loading thread (igIGBFile::readFile 800528DC through igAdxFile
//   802AA1B0, a thread below the main thread), which takes it up once the
//   main thread's update ends. The thread reads the file in 64-sector
//   chunks through the loader's ADXSTM (8051B1D0, after the ADX streams'
//   readers in each server pass): it asks for the next chunk when the
//   parser wants bytes beyond the last one (loader_work), and the request
//   is issued at the next retrace's server pass. Its wait 802A9DC4 does not
//   follow the retrace: it sleeps 16666666 ns (igAdxFile +0x58 / +0x5C with
//   +0x3C and +0x3D set: the thread sleep of 805622F8, not VIWaitForRetrace
//   802648EC) between looks at the stream's state, so the chunk is taken up
//   at the first look after the server pass marked it done, on a grid that
//   starts at the request and drifts against the fields (Dolphin r46, hooks
//   on the request 0x802AA2F8 / 0x802AA43C and the wait's return 0x802AA318 /
//   0x802AA474 in 956 chunks: the return is the request + 0.00245 +
//   n * 0.999309 fields within 0.002, never earlier than 0.027 fields after
//   the retrace). The work after the file's last chunk ends with 80070130
//   true; the update after that routes it (80070140), and the next request
//   follows in the next update.
// - An update that takes the original longer than a field (the teardown of
//   the outgoing stage, routing a large stage member, registering the stage's
//   infos in state 5, start.lua) covers the following retraces: the loader
//   skips them, and a read it asked for is issued with the last of them.
namespace {
constexpr std::int64_t kNanoField = 1'000'000'000;  // the loading thread's clock: nano-fields from frame 0's retrace
constexpr std::int64_t kLoaderPoll = 999'309'000;   // its sleep between looks at the stream (16666666 ns and the look)
constexpr std::int64_t kLoaderPollOffset = 2'450'000; // the first look's own work after the request
constexpr std::int64_t kLoaderBooked = 27'000'000;  // the loader's pass marks a read done this long after the retrace
// The main thread and the CRI servers take the CPU from each retrace on for
// this long before the loading thread runs (no request or look in 1278
// captured ones falls earlier; the processing times fit best with it).
constexpr std::int64_t kLoaderMainWindow = 31'000'000;
// Where in its frame the thread takes a request up: the end of the main
// thread's update that made it (Dolphin r46: the first request of a file
// 0.030-0.047 fields after the retrace in states 100 and 2, 0.054-0.095 in
// state 4, 0.245-0.264 for the loading card from state 0's update, and the
// descriptor 0.035-0.040 after the update's late world clock sample, 0.160
// after the session's boot reads).
constexpr std::int64_t kLoaderStartQueue = 36'000'000;
constexpr std::int64_t kLoaderStartCommon = 68'000'000;
constexpr std::int64_t kLoaderStartCard = 249'000'000;
constexpr std::int64_t kLoaderStartDescriptor = 40'000'000;
constexpr std::int64_t kLoaderStartAfterBoot = 160'000'000;

// The end of `work` nano-fields of the loading thread's work begun at time
// `t`: after each retrace the main thread's window passes first.
std::int64_t loader_walk(std::int64_t t, std::int64_t work) {
    for (;;) {
        const std::int64_t retrace = (t / kNanoField + 1) * kNanoField;
        if (t + work <= retrace) return t + work;
        work -= retrace - t;
        t = retrace + kLoaderMainWindow;
    }
}

// The wait's return for a chunk requested at `request` whose read the
// server pass of frame `booked` marked done: the first look after it; a look
// that falls in the main thread's window runs once the window ends.
std::int64_t loader_look(std::int64_t request, std::int64_t booked) {
    const std::int64_t done = booked * kNanoField + kLoaderBooked;
    std::int64_t look = request + kLoaderPollOffset + kLoaderPoll;
    while (look < done) {
        look += kLoaderPoll;
        if (look % kNanoField < kLoaderMainWindow) look = look / kNanoField * kNanoField + kLoaderMainWindow;
    }
    if (look % kNanoField < kLoaderMainWindow) look = look / kNanoField * kNanoField + kLoaderMainWindow;
    return look;
}

// The loading thread's work on a file (igIGBFile::readFile 800528DC), in
// nano-fields of its own time, after each 64-sector chunk arrives before it
// asks for the next; the last entry runs on until the file is done. The
// readFile steps in file order, each needing the file's bytes up to some
// offset (Dolphin r46: a hook after each step of 800528DC, 106 files of 17
// captures; the fitted costs, in fields, with the files they were measured
// in left out by name the error was):
// - header and type metadata (8004F974 .. 8004FC94): 0.020 + 0.00242 per
//   object type (within 0.012); external directories 4.6e-6 per byte;
// - the directory entries (800509D4): 0.0038 + 0.000583 per entry (0.003);
// - the reference index (80050C68): 0.0016 + 1.23e-5 per object + 1.16e-5
//   per memory block (0.001);
// - making the objects (80050E80): 0.0034 + 0.000247 per object + 0.000213
//   per memory block (0.02; 30863 objects: 9.7 fields);
// - the object pass (80051AE0): it fills the reader's 128 KB buffer (+0x104
//   = 0x20000 by Dolphin's log, refilled by 800506CC with what was used) at
//   the start of the object buffer, a loop over the objects (0.0137 +
//   1.61e-5 per object + 5.4e-6 per memory block), then the records out of
//   the buffer: 0.0148 + 0.000108 per record + 8.38e-5 per string field +
//   3.66e-5 per string byte + 0.000266 per memory block of the file spread
//   over the records (0.14 per file); a record the buffer does not hold
//   whole refills it up to 128 KB from the record on, so the thread asks
//   for the next chunk a buffer ahead of the records it reads;
// - the memory blocks (the file's +0x84), read in place: 0.0017 + 5.74e-5
//   per block + 1.01e-6 per byte (0.011);
// - after the last byte: the memory entries' two passes (80051CB0), every
//   entry's +0x90 (800527D8), the file's +0x78 and 80051E30: 0.0517 +
//   6.16e-5 per object + 0.000128 per memory block + max(0, -0.0552 +
//   0.000114 per memory block + 9.04e-7 per byte of igCharMetaField memory)
//   (the textures' pixels; 0.2 per file, 1.2 for the stage file with the
//   most).
// Walked from each chunk's arrival with the main thread's window at each
// retrace this puts 941 captured chunk times within 0.14 fields (rms).
std::vector<std::int64_t> loader_work(const IgbDocument* document, std::size_t file_bytes) {
    constexpr std::size_t kChunk = 64 * 2048, kBuffer = 0x20000;
    const std::size_t chunks = std::max<std::size_t>(1, (file_bytes + kChunk - 1) / kChunk);
    std::vector<std::int64_t> work(chunks, 0);
    std::size_t loaded = 0; // chunks asked for
    std::int64_t now = 0;   // work since the last chunk arrived
    auto need = [&](std::size_t end) {
        end = std::min(end, file_bytes);
        while (loaded == 0 || loaded * kChunk < end) {
            if (loaded > 0) {
                work[loaded - 1] += now;
                now = 0;
            }
            ++loaded;
        }
    };
    auto spend = [&](std::int64_t nano) { now += nano; };
    if (!document) {
        // (A file the port could not parse: its bytes as memory.)
        for (std::size_t p = 0; p < file_bytes; p += kChunk) {
            need(p + kChunk);
            spend(std::int64_t(std::min(kChunk, file_bytes - p)) * 1010);
        }
        need(file_bytes);
        spend(51'700'000);
        work[loaded - 1] += now;
        return work;
    }
    const auto& doc = *document;
    const auto& header = doc.metadata.header;
    const auto bytes = doc.bytes();
    auto u32 = [&](std::size_t offset) -> std::uint32_t {
        if (offset + 4 > bytes.size()) return 0;
        const std::uint8_t* p = bytes.data() + offset;
        return header.byte_order == IgbByteOrder::big ? std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]
                                                      : std::uint32_t(p[3]) << 24 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[1]) << 8 | p[0];
    };
    const std::int64_t objects = std::int64_t(doc.objects.size()), memories = std::int64_t(doc.memories.size());
    std::size_t cursor = doc.metadata.metadata_bytes;
    need(48);
    spend(20'000'000);
    need(cursor);
    spend(std::int64_t(doc.metadata.object_types.size()) * 2'420'000);
    if (header.flags & 0x40000000u) {
        const std::size_t size = u32(cursor);
        cursor += size;
        need(cursor);
        spend(std::int64_t(size) * 4'600);
    }
    if (header.flags & 0x10000000u) {
        cursor += u32(cursor);
        need(cursor);
    }
    cursor += header.entry_buffer_size;
    need(cursor);
    spend(3'800'000 + std::int64_t(doc.entries.size()) * 583'000);
    cursor += u32(cursor);
    need(cursor);
    spend(1'600'000 + objects * 12'300 + memories * 11'600);
    if (header.flags & 0x80000000u) cursor += 4;
    need(cursor);
    spend(3'400'000 + objects * 247'000 + memories * 213'000);
    const std::size_t object_start = cursor, object_end = cursor + header.object_buffer_size;
    if (objects) {
        std::vector<char> string_type(doc.metadata.field_types.size(), 0);
        for (std::size_t i = 0; i < string_type.size(); ++i) string_type[i] = doc.metadata.field_types[i].name == "igStringMetaField";
        std::size_t fill = std::min(object_start + kBuffer, object_end);
        need(fill);
        spend(13'700'000 + objects * 16'100 + memories * 5'400);
        spend(14'800'000);
        const std::int64_t per_record = 108'000 + memories * 266'000 / objects;
        std::size_t position = object_start;
        for (const auto& object : doc.objects) {
            const std::size_t length = u32(position + 4);
            if (position + length > fill) {
                fill = std::min(position + kBuffer, object_end);
                need(fill);
            }
            std::int64_t strings = 0, string_bytes = 0;
            for (const auto& field : object.fields)
                if (field.type_index < string_type.size() && string_type[field.type_index]) {
                    ++strings;
                    string_bytes += std::int64_t(field.size >= 4 ? field.size - 4 : 0);
                }
            spend(per_record + strings * 83'800 + string_bytes * 36'600);
            position += std::max<std::size_t>(length, 8);
        }
    }
    spend(1'700'000);
    std::int64_t char_bytes = 0;
    for (const auto& memory : doc.memories) {
        spend(57'400);
        for (std::size_t p = memory.offset, e = memory.offset + memory.size; p < e;) {
            const std::size_t q = std::min(e, (p / kChunk + 1) * kChunk);
            need(q);
            spend(std::int64_t(q - p) * 1'010);
            p = q;
        }
        if (memory.memory_type_index >= 0 && std::size_t(memory.memory_type_index) < doc.metadata.field_types.size() &&
            doc.metadata.field_types[std::size_t(memory.memory_type_index)].name == "igCharMetaField")
            char_bytes += std::int64_t(memory.size);
    }
    need(file_bytes);
    spend(51'700'000 + objects * 61'600 + memories * 127'600 + std::max<std::int64_t>(0, -55'200'000 + memories * 114'000 + char_bytes * 904));
    work[loaded - 1] += now;
    return work;
}

bool trace_loader() {
    static const bool on = std::getenv("DW4_TRACE_LOADER") != nullptr;
    return on;
}

// The objects and memory blocks first reached from `start` through object
// references, memory references and object lists (`seen` marks the
// document's references already counted): what deleting an info frees.
std::pair<std::size_t, std::size_t> reachable(const IgbDocument& document, std::uint32_t start, std::vector<char>& seen) {
    std::size_t objects = 0, memories = 0;
    std::vector<std::uint32_t> stack{start};
    while (!stack.empty()) {
        const auto reference = stack.back();
        stack.pop_back();
        if (reference >= document.references.size() || seen[reference]) continue;
        seen[reference] = 1;
        const auto& entry = document.references[reference];
        if (entry.kind == IgbReferenceKind::memory) {
            if (entry.value_index < document.memories.size()) ++memories;
            continue;
        }
        if (entry.kind != IgbReferenceKind::object || entry.value_index >= document.objects.size()) continue;
        ++objects;
        const auto& object = document.objects[entry.value_index];
        for (const auto& field : object.fields) {
            if (field.type_index >= document.metadata.field_types.size()) continue;
            const auto& type = document.metadata.field_types[field.type_index].name;
            if (type != "igObjectRefMetaField" && type != "igMemoryRefMetaField") continue;
            try {
                if (const auto next = document.field_reference(object, field.slot)) stack.push_back(*next);
            } catch (const std::exception&) {
            }
        }
        if (document.is_a(object, "igObjectList")) {
            try {
                for (const auto& element : document.object_list(reference))
                    if (element) stack.push_back(*element);
            } catch (const std::exception&) {
            }
        }
    }
    return {objects, memories};
}
} // namespace

// NEXTSTAGE's frame ends the application loop's update / render loop: after
// its picture (the endScene retrace) the loop reloads every module (80021944
// -> 8040F570 -> 8028A87C) before the next update's world clock sample
// (8028AF84) and beSystem's descriptor request (80316258: 8006F794 at
// 0x8031658C, 8006FD34 at 0x803165A0). The reload calls every module's +0x70
// reset (reset_modules) and deletes every info of the application's list +0x10
// (80066e1c at 0x8028A910), then 80021944 finishes (8028A730, the
// application's +0xEC, 800607F4), and all of it takes the main thread for
// these milli-fields. With hooks on each reset and each delete (r44timing
// d_td3) the module resets take 0.45 fields (beMessenger 0.13-0.21, beLua
// 0.14-0.23, beLayerCtl, beFont, beCameraCtrl; the other 58 next to nothing)
// and the deletes grow with what the infos hold; the tail in 80021944 after
// 8040F570 is 0.01-0.12 fields, but 1.08 / 1.09 after dng101b1 (1P and 2P),
// which nothing the port keeps explains. Fitted to the whole reload (the
// reload's start to the next world clock sample, 16 Dolphin measurements):
// 0.448 fields + 0.753 per 1000 objects the beDBManagerInfos reach + 0.656 per
// 1000 memory blocks outside the DB / font infos + 0.166 per 1000 other
// objects (the model RAMs and beFontInfos add nothing measurable beyond that).
// Measured / modelled, fields (J->U: hooks on the reload's start 80021944 and
// the loop's return 0x800212C0 and the next world clock sample; W: the world
// clock delta minus a field, the NEXTSTAGE frame's lateness corrected):
//   stagIS08 logos         4.317 /  4.054  J->U d_boot2, d_ngsr1 4.322
//   stagIS09 opening       0.547 /  0.546  J->U d_ngsr1
//   stagIS10 to attract    7.465 /  7.672  J->U d_boot2
//   stagIS10 new game      7.629 /  7.672  J->U d_ngsr1
//   stagIS10 login         7.598 /  7.672  W r43 d_rs_cons
//   stagIS10 2P login      7.663 /  7.672  J->U r44timing d_rs2p101
//   stag0203 change       15.23  / 15.371  W fix_r22load2
//   stag0204 change       19.57  / 19.380  W fix_r29load
//   dng101b1 soft reset   16.585 / 16.775  J->U r44timingrev d_ngsr1
//   dng101b1 2P reset     16.678 / 16.804  J->U r44timing d_rs2p101
//   stag0001 soft reset   16.335 / 16.616  J->U r44timingrev d_ngsr0001
//   dng411b1 3P game over 17.258 / 17.129  W r43 d_mp3g (d_mp3g_late 17.259)
//   stagBS01 game over    18.573 / 18.163  W r43 d_bs01go
//   stag0101 soft reset   20.236 / 20.479  W r43 d_rs_cons
//   stag0101 gate warp    20.451 / 20.479  W r43 d_g1
//   stag0103 2P reset     17.037 / 16.622  W r43 d_rs_2pboat
// The update after the reload comes on the original's field in 15 of the
// 16 (whole fields alike); stag0103's reload, 0.04 past a whole field, ends
// one field early (no monotone count of what it holds puts it above
// dng101b1's 2P reload: 1-2 % fewer objects and memory blocks, 0.36 fields
// longer). Leaving each point out of the fit, 14 of 16 still land on their
// field (stagIS08 then 3.88). (The r44 fit of 5 points - 0.45 + 0.93 / 1000
// DB objects + 1.4 per font + 0.0086 per model RAM + 0.405 / 1000 other
// memory blocks - was 0.7 to 2.1 fields too long or short for dng101b1,
// dng411b1 and stag0101; until r44 the loader added 1.5 + 0.7 / 1000 memory
// blocks + 0.18 / 1000 objects to the update itself: fitted to the update's
// whole world clock delta, one field more than the reload, so every
// descriptor read after a teardown came a field late.) The session's first
// load has nothing to release (0.12 fields: 0).
Engine::TeardownLoad Engine::teardown_load() const {
    TeardownLoad load;
    std::size_t db_memories = 0;
    for (const auto& file : files_) {
        const auto* document = file.document.get();
        if (!document) continue;
        load.objects += document->objects.size();
        load.memories += document->memories.size();
        if (!document->info_list_reference) continue;
        // (In the info list's order, each info taking what an earlier one
        // did not reach, as the captures were measured. A broken info list
        // counts as far as it can be read.)
        std::vector<char> seen(document->references.size(), 0);
        try {
            for (const auto& element : document->object_list(*document->info_list_reference)) {
                if (!element) continue;
                const auto [objects, blocks] = reachable(*document, *element, seen);
                const auto& info = document->object(*element);
                if (info.type_index >= document->metadata.object_types.size()) continue;
                const auto& type = document->metadata.object_types[info.type_index].name;
                if (type == "beFontInfo") {
                    ++load.fonts;
                    db_memories += blocks;
                } else if (type == "beDBManagerInfo") {
                    load.db_objects += objects;
                    db_memories += blocks;
                }
            }
        } catch (const std::exception&) {
        }
    }
    const auto* models = get<ModelCtrlManager>();
    load.rams = models ? models->rams().size() : 0;
    load.rest = load.memories - std::min(load.memories, db_memories);
    return load;
}

std::int32_t Engine::loader_teardown_cost() const {
    if (files_.empty()) return 0;
    const auto load = teardown_load();
    const std::size_t other_objects = load.objects - std::min(load.objects, load.db_objects);
    return std::int32_t(448 + load.db_objects * 753 / 1000 + load.rest * 656 / 1000 + other_objects * 166 / 1000);
}

// The two long updates at a stage's start, fitted to the update lengths of
// 14 Dolphin loads (r46: the world clock delta after each, a whole number of
// fields as the update waits for its retrace; the 7 of round 46's load set and
// 7 new games sent to other stages). State 5 (802FA53C) registers the stage's
// infos: -1.888 fields + 0.00315 per model RAM + 0.0028 per info (all 14
// cover Dolphin's retraces; each left out of the fit: 11 of 14; the r29 fit
// of 0.05 + 0.0024 per RAM covered 10 of 14).
std::int64_t Engine::loader_register_cost(const std::vector<ObjectView>& infos) const {
    const auto* models = get<ModelCtrlManager>();
    const std::int64_t rams = models ? std::int64_t(models->rams().size()) : 0;
    return std::max<std::int64_t>(0, -1888 + rams * 315 / 100 + std::int64_t(infos.size()) * 28 / 10);
}

// State 7: TEXTCTL INITIALIZE and start.lua, -4.064 fields + 0.00425 per
// model RAM + 0.0109 per model it generates (all 14; each left out: 13 of 14,
// dng205b3 covers 2 retraces, dng308b3 1; the r29 fit of 0.45 + 0.0177 per
// generated model covered 11 of 14).
std::int64_t Engine::loader_start_cost() const {
    std::int64_t generated = 0, rams = 0;
    if (const auto* models = get<ModelCtrlManager>()) {
        rams = std::int64_t(models->rams().size());
        for (const auto& ram : models->rams()) generated += ram->generated ? 1 : 0;
    }
    return std::max<std::int64_t>(0, -4064 + rams * 425 / 100 + generated * 109 / 10);
}

void Engine::loader_request(const LoaderItem& item, std::int64_t start) {
    loader_file_ = {};
    loader_file_.item = item;
    loader_file_.active = true;
    loader_file_.request_time = std::int64_t(frame_) * kNanoField + start;
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
    // What the loading thread does with each chunk (loader_work; a file the
    // port reads on its own, such as BecText.igb, is parsed for it alone).
    // The update that routes the file takes 0.081 fields per 1000 memory
    // blocks (the stage members with 12400 and more cover a second retrace).
    if (!loader_fast_) {
        std::shared_ptr<const IgbDocument> timing = loader_file_.document;
        if (!timing && item.timing_only && bytes) {
            try {
                timing = std::make_shared<const IgbDocument>(read_igb_document(resources_.read(item.name)));
            } catch (const std::exception&) {
            }
        }
        loader_file_.work = loader_work(timing.get(), bytes);
    }
    loader_file_.route_cost = loader_file_.document ? std::int64_t(loader_file_.document->memories.size()) * 81 / 1000 : 0;
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
    if (trace_loader()) {
        std::string work;
        for (const auto w : loader_file_.work) work += " " + std::to_string(w / 1'000'000);
        log("loader F " + item.name + " bytes " + std::to_string(bytes) + " objects " +
            std::to_string(loader_file_.document ? loader_file_.document->objects.size() : 0) + ", taken up at +" +
            std::to_string((loader_file_.request_time % kNanoField) / 1'000'000) + ", work per chunk (milli-fields)" + work + ", route " +
            std::to_string(loader_file_.route_cost));
    }
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

// 80070130 for the pending directory: the loading thread finished the file
// before this frame's update (a boot read: its fields after the booking).
bool Engine::loader_ready() const {
    if (!loader_file_.active || !loader_file_.issued) return false;
    if (loader_fast_) return true;
    if (loader_file_.sectors == 0 || !get<SoundManager>()) return true;
    if (loader_file_.read < loader_file_.sectors) return false;
    if (loader_file_.raw_offset) return loader_file_.booked >= 0 && std::int64_t(frame_) >= loader_file_.booked + 1 + loader_file_.parse;
    return loader_file_.ready_frame >= 0 && std::int64_t(frame_) >= loader_file_.ready_frame;
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
        loader_request(loader_queue_[loader_index_], kind == 2 ? kLoaderStartCommon : kLoaderStartQueue);
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
        loader_request(loader_queue_[loader_index_], 0);
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
    // The read on its way: the frame that first sees it booked (the previous
    // step's server pass booked it, SoundManager::server_tick). The loading
    // thread takes the chunk up at its first look after that (loader_look),
    // works on it (loader_work, loader_walk) and asks for the next one, which
    // the server pass of the frame it asks in issues; after the last chunk
    // the file is done when that work ends, and the next update routes it.
    if (loader_file_.issued && loader_file_.booked < 0 && (!sound || !sound->loading())) {
        loader_file_.booked = std::int64_t(frame_);
        if (!loader_file_.raw_offset && loader_file_.work.empty()) {
            loader_file_.issue_frame = loader_file_.booked;
            loader_file_.ready_frame = loader_file_.booked + 1;
        } else if (!loader_file_.raw_offset && loader_file_.read > 0) {
            const std::size_t chunk = std::min<std::size_t>((loader_file_.read - 1) / 64, loader_file_.work.size() - 1);
            const std::int64_t look = loader_look(loader_file_.request_time, loader_file_.booked);
            const std::int64_t end = loader_walk(look, loader_file_.work[chunk]);
            if (trace_loader()) {
                auto at = [](std::int64_t t) {
                    char text[48];
                    std::snprintf(text, sizeof text, "%lld+%.3f", static_cast<long long>(t / kNanoField), double(t % kNanoField) / double(kNanoField));
                    return std::string(text);
                };
                log("loader chunk " + std::to_string(chunk) + " of " + loader_file_.item.name + ": taken up at " + at(look) +
                    (loader_file_.read >= loader_file_.sectors ? ", done at " : ", next asked for at ") + at(end));
            }
            if (loader_file_.read >= loader_file_.sectors) {
                loader_file_.ready_frame = end / kNanoField + 1;
            } else {
                loader_file_.request_time = end;
                loader_file_.issue_frame = end / kNanoField;
            }
        }
    }
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
    // (a long update: with the last retrace it covers; the loading thread
    // takes it up when that update ends), chunk by chunk.
    if (!loader_file_.issued) {
        if (loader_skip_ > 0) return;
        if (std::int64_t(frame_) < loader_file_.request_time / kNanoField) return;
        loader_file_.request_time = std::max(loader_file_.request_time, std::int64_t(frame_) * kNanoField + kLoaderStartQueue);
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
    // The next chunk: issued by the server pass of the frame the loading
    // thread asked for it in.
    if (loader_file_.read >= loader_file_.sectors || loader_file_.booked < 0 || !sound) return;
    if (loader_file_.issue_frame < 0 || std::int64_t(frame_) < loader_file_.issue_frame) return;
    loader_file_.booked = -1;
    loader_file_.issue_frame = -1;
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
        // beSystem (80316258): the first update after the module reload
        // releases the old stage's directories (0x8031658C) and asks for
        // <stage>.igb (0x803165A0) at once; once that directory is ready
        // (0x80316674) its beFileListInfo starts the file-list manager,
        // whose update follows in the same frame (state 0).
        if (!loader_file_.active) {
            // (After a module reload the update starts late by the reload's
            // last field, StageSnapshot::clock_lateness.)
            const std::int64_t late = applied_lateness_ * 3 * kNanoField / kFieldNsPer3;
            loader_request({stage_name_ + ".igb"}, loader_boot_step_ > 0 ? kLoaderStartAfterBoot : late + kLoaderStartDescriptor);
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
            if (const auto* record = resources_.catalog().find(path); record && record->location) loader_request({path}, kLoaderStartCard);
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
    case 100: {
        if (!loader_queue_step(0)) return;
        // 802FA53C registers the preliminary infos; with a SOUND RAM among
        // them (SOUND SEARCH 0x802F9518, its bools[0] read unchecked at
        // 0x802F9524) the loader sends SOUND MAKEBINLIST (0x802F9544) and
        // goes to state 101, without one straight to 102. The messages go
        // through the messenger like the original's (its message objects).
        for (const auto& info : loader_preliminary_) register_info(info);
        loader_preliminary_.clear();
        Packet search;
        messenger_.send(*this, "SOUND", std::nullopt, -1, "SEARCH", search);
        if (search.boolean(0)) {
            Packet list;
            messenger_.send(*this, "SOUND", std::nullopt, -1, "MAKEBINLIST", list);
            loader_state_ = 101;
        } else {
            loader_state_ = 102;
        }
        return;
    }
    case 101: {
        // SOUND ISDONETRANS (0x802F9564) holds the state until its bools[0]
        // is 1 (0x802F9570..0x802F957C); every capture spends one update here.
        Packet done;
        messenger_.send(*this, "SOUND", std::nullopt, -1, "ISDONETRANS", done);
        if (done.boolean(0)) loader_state_ = 102;
        return;
    }
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
        // Randomizer: the enemy pool's families from donor stages (no disc
        // time; every machine loads the same).
        if (cheats_.enemy_shuffle()) load_enemy_donors();
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
        // Randomizer: the donor families after the stage's own infos.
        for (const auto& info : donor_infos_) register_info(info);
        for (const auto& [info, targets] : donor_starters_) {
            register_info(info);
            if (auto* starters = get<ActionStarterManager>()) starters->set_target_filter(info, targets);
        }
        donor_infos_.clear();
        donor_starters_.clear();
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
    const detail::MxcsrGuard mxcsr;
    if (!online_.enabled) {
        step_impl(samples);
        return;
    }
    // Online (7.4): no console reset however the game sets beSystem +0x5F.
    if (console_reset_.enabled()) console_reset_.set_enabled(false);
    const auto effective = online_samples(samples);
    step_impl(effective);
    finish_online_step(effective);
}

void Engine::step_impl(const std::array<PadSample, 4>& samples) {
    torn_down_ = false;
    // The console reloads the game (rebooting): nothing of the old one runs.
    if (rebooting_) {
        ++frame_;
        ++update_frame_;
        ++reboot_fields_;
        return;
    }
    // The original's update of this frame is still running (hold_update): no
    // world clock sample, no input read, no update; the controllers' own
    // reset combination and the work on the retraces (the CRI servers) go on.
    held_ = hold_fields_ > 0;
    if (held_) {
        --hold_fields_;
        ++frame_;
        console_reset_.hold(samples);
        for (auto& m : managers_) m->update_held(*this);
        return;
    }
    // NEXTSTAGE's frame was the last of the application loop's update /
    // render loop: the module reload comes first, from the retrace after its
    // picture on (loader_teardown_cost): the module resets at its start
    // (reset_modules), then its whole fields run no update and keep the last
    // picture (no new XFB until the update after it; presenting()), only the
    // work on the retraces goes on, and the update after it samples the world
    // clock late by the rest of a field (Dolphin d_ngsr1: J at the retrace
    // after NEXTSTAGE's frame, Q and the next sample U 16.585 fields later).
    if (!pending_stage_.empty() && !reloading_) {
        reloading_ = true;
        // (Fast loading, a PC option, takes no time for it either.)
        loader_teardown_ = cheats_.fast_loading ? 0 : loader_teardown_cost();
        if (trace_loader()) {
            std::size_t objects = 0;
            for (const auto& file : files_)
                if (file.document) objects += file.document->objects.size();
            const auto load = teardown_load();
            log("loader teardown of " + stage_name_ + ": " + std::to_string(files_.size()) + " files, " + std::to_string(objects) +
                " objects, " + std::to_string(load.memories) + " memory blocks, " + std::to_string(load.rams) + " model RAMs, " +
                std::to_string(load.db_objects) + " DB objects, " + std::to_string(load.fonts) + " fonts, " + std::to_string(load.rest) +
                " other memory blocks -> module reload " + std::to_string(loader_teardown_) + " milli-fields");
        }
        // The module resets come first (reset_modules): the movies stop (the
        // codecs' retrace waits end with them), the effect goes off and the
        // stage's banks are unloaded before the reload's first field.
        reset_modules();
        set_clock_lateness(std::int64_t(loader_teardown_ % 1000) * (kFieldNsPer3 / 3) / 1000);
        if (loader_teardown_ >= 1000) {
            hold_fields_ = loader_teardown_ / 1000 - 1;
            held_ = true;
            ++frame_;
            console_reset_.hold(samples);
            for (auto& m : managers_) m->update_held(*this);
            return;
        }
    }
    // The pad module (bePadManager 80311F84) updates the four slots from the
    // controllers every frame; STAGECTL PADINPUT DISABLE only changes what
    // its getter hands out (see pads()). In the original it is a scheduled
    // module after beMessenger and the stage loader (8028B7B0's order
    // below: 80311F84 reads each device's state through its vtable +0x78 /
    // +0x7C and writes the slots), so a message delivery or start.lua /
    // first.lua would see the previous frame's slots. The port updates them
    // here because nothing that runs before the managers reads them: every
    // DOL call of the getter 80312138 (26, in beMovie 8031EC10,
    // beNDMWStageCtl 80347BB4, beNDMWTitle2 80369284, the bit capsule
    // 8034EB78, ...) is in a manager's RAM update (vtable +0x84 under the
    // traversal 802F28F8) or a model / window class update below one, no
    // script function reads a pad, and the port's readers are all manager
    // updates. (The stage snapshot taken in begin_stage_load keeps this
    // frame's slots, and a restore does not update them again.)
    try { pads_.update(samples); } catch (const std::exception& error) { note(std::string("pad input: ") + error.what()); }
    // The frame clock advances first so updates and the following render
    // observe the same time (joint clamps and fades end exactly on a key).
    // One NTSC field of world time (8028AF84), see field_time.
    // (After covered fields the sample has moved on by all of them.)
    const auto before = time_;
    time_ += field_time(frame_ + 1) - field_time(update_frame_) + clock_lateness_ - applied_lateness_;
    applied_lateness_ = clock_lateness_;
    ++frame_;
    update_frame_ = frame_;
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
    if (!pending_stage_.empty()) {
        // Online: BARRIER_OK's membership and rules first (6.7).
        if (online_.enabled) apply_barrier();
        begin_stage_load();
        torn_down_ = true;
    }
    // The modules in the serial scheduler's order (8028B7B0, Dolphin r44:
    // the viewer 8040DA1C, 80412E18, beSystem 80316258, beMessenger
    // 80304C58, beCri 802F53B8, the stage loader 802F8A64, bePadManager
    // 80311F84, 80302558, beDemoManager 802F7EC0, 8031BE1C, then the
    // managers' 802F28F8). The current camera viewer (802F4268) sets the
    // frame's camera before the others run (CameraManager::begin_frame); the
    // messenger's pass comes before the loader, so the posts of start.lua
    // and first.lua and the loader's own sends follow the frame's pass
    // (a delay-0 post arrives at the next frame's pass). The CRI servers of
    // the next retrace follow the managers (update_managers).
    if (auto* camera = get<CameraManager>()) camera->begin_frame(*this);
    messenger_.update(*this);
    process_stage_loader();
    demo_.update(*this);
    // Seamless co-op: no leash while OnlineRules::leash_off (a no-op offline).
    apply_leash_rule();
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
    // The movie player (igMovieManager, 802B11BC) is a module MOVIE PLAY
    // appends to the scheduler (8028A398 at 0x802AF254): it runs after
    // everything scheduled before it, whether or not the stage is active.
    // Before it come the CRI servers of the next retrace
    // (SoundManager::server_tick). The original's ADXT server and file
    // server threads (8029D434 at priority 12, 8029D3DC at 14, above the
    // main thread's 16; ADXM_SetupThrd 8029D0D8) wake at every retrace
    // (VIWaitForRetrace 802648EC) and run before the frame's update (r44
    // Dolphin hooks: the ADXT server 0.013 and the file server 0.003 fields
    // ahead of the frame's world clock sample, every field), so everything
    // a frame's update does - the loader's requests and bookings, a stream
    // started or paused by a delivery, start.lua / first.lua, the demo or a
    // manager, the entries' updates (802F53B8) and ISREADY / ISALLREADY
    // (802F6FB0) - meets the pass of its own retrace before it and the pass
    // of the next one after it: a stream's first read comes in the field
    // after its start whatever started it (Dolphin r44 / r46: ADXT_StartAfs
    // in an update, the first read B at the next retrace), PLAYING counts
    // from the next update. The movie codec's update waits for the retrace
    // inside its own update (802AE8B8 -> VIWaitForRetrace) and sees the
    // pass of that retrace, so the pass comes before the movie player.
    auto* sound = get<SoundManager>();
    if (!stage_active_) {
        for (auto& m : managers_)
            if (!m->scheduled_at_registration()) m->update_inactive(*this);
        if (sound) sound->server_tick(*this);
        if (sound) sound->update_movie_player(*this);
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
    if (sound) sound->server_tick(*this);
    if (sound) sound->update_movie_player(*this);
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
    if (!debug_camera) context.picture_previous_view = picture_previous_view;
    if (debug_camera) camera = *debug_camera;
    context.cache = &scenes_;
    context.time = time_;
    context.camera = camera;
    context.output = out;
    context.frame = frame_;
    if (auto* models = get<ModelCtrlManager>()) models->install_actor_hook(*this, context);
    if (auto* actions = get<ActionManager>())
        context.actor_pose = [this, actions](const Node& actor, bool traversed) {
            return traversed ? actions->pose_for(actor, time_) : actions->peek_pose(actor, time_);
        };
    Mat4 inverse_view;
    if (invert(camera.view, inverse_view)) context.camera_position = {inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
    return context;
}

namespace {
// DW4_TRACE_TRAVERSAL_TIME (development measurement, Engine::traversal_ns):
// the CPU time of each traversal pass. Off, it reads no clock.
bool trace_traversal_time() {
    static const bool on = std::getenv("DW4_TRACE_TRAVERSAL_TIME") != nullptr;
    return on;
}
struct PassTimer {
    std::uint64_t& ns;
    std::uint64_t& count;
    bool on = trace_traversal_time();
    std::chrono::steady_clock::time_point start = on ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    ~PassTimer() {
        if (!on) return;
        ns += std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        ++count;
    }
};
} // namespace

// Per-slot cameras (OnlineRules::player_cameras, docs/SEAMLESS.md 4.2): the
// simulation pass of the step, culled against the union of every member
// camera's frustum (the first member's camera first: it takes the
// view-dependent values), without draw items, and the shadow casters under
// every member's light. Every machine of an instance computes every member's
// camera from the lockstep inputs, so the union is the same everywhere. With
// one camera it is the original traversal.
void Engine::traverse_union() {
    if (rebooting_) return;
    PassTimer timer{traversal_ns[2], traversal_count[2]};
    FrameCamera camera;
    Vec4f clear_color;
    TraversalContext context = frame_context(nullptr, camera, clear_color);
    if (auto* cameras = get<CameraManager>()) {
        bool first = true;
        for (int slot = 0; slot < 4; ++slot) {
            if (!(online_.player_cameras & (1u << slot))) continue;
            FrameCamera own;
            cameras->frame_camera(*this, own, slot);
            if (first) context.cull_camera = own;
            else context.extra_cull.push_back(own);
            first = false;
        }
    }
    context.union_cull = true;
    traverse(&global_root_, context);
    if (auto* shadows = get<ShadowManager>()) shadows->traverse(context);
    union_frame_ = frame_;
    union_time_ = time_;
}

void Engine::traverse_scene() {
    // As render(): the same nodes in the same order with the same camera,
    // time and poses; only the draw items (geometry, particle quads, skin
    // palettes, lights) are left out.
    if (rebooting_) return;
    const detail::MxcsrGuard mxcsr;
    if (online_.player_cameras) {
        traverse_union();
        return;
    }
    PassTimer timer{traversal_ns[1], traversal_count[1]};
    FrameCamera camera;
    Vec4f clear_color;
    TraversalContext context = frame_context(nullptr, camera, clear_color);
    traverse(&global_root_, context);
    if (auto* shadows = get<ShadowManager>()) shadows->traverse(context);
}

void Engine::render(std::vector<DrawItem>& out, FrameCamera& camera, Vec4f& clear_color, ShadowFrame* shadow) {
    const detail::MxcsrGuard mxcsr;
    out.clear();
    // VISetBlack (0x803162E0): a rebooting console shows black.
    if (rebooting_) {
        clear_color = {0, 0, 0, 1};
        if (shadow) shadow->active = false;
        return;
    }
    if (online_.player_cameras) {
        // Per-slot cameras: the step's union simulation pass (once per step;
        // a traversal at the same world time changes nothing), then this
        // machine's picture with its own camera, which changes nothing
        // either: every node takes its peek path. The picture keeps the
        // widescreen widening and the smooth motion rule (the previous
        // step's view of this camera, set by the host).
        if (union_frame_ != frame_ || union_time_ != time_) traverse_union();
        PassTimer timer{traversal_ns[3], traversal_count[3]};
        TraversalContext context = frame_context(&out, camera, clear_color);
        FrameCamera local = context.cull_camera;
        if (auto* cameras = get<CameraManager>()) cameras->frame_camera(*this, local, picture_slot());
        context.cull_camera = local;
        context.camera = debug_camera ? *debug_camera : local;
        camera = context.camera;
        context.simulate = false;
        Mat4 inverse_view;
        if (invert(camera.view, inverse_view)) context.camera_position = {inverse_view(3, 0), inverse_view(3, 1), inverse_view(3, 2)};
        // A multi-member instance: other members' item name lines and exit
        // prompts are not this machine's (picture_hidden_roots).
        picture_hidden_roots(picture_hidden_);
        if (!picture_hidden_.empty()) context.picture_hidden = &picture_hidden_;
        traverse(&global_root_, context);
        std::stable_partition(out.begin(), out.end(), [](const DrawItem& item) { return !item.state.blend_enabled; });
        if (shadow) {
            shadow->active = false;
            if (auto* shadows = get<ShadowManager>()) shadows->render(*this, context, *shadow);
        }
        return;
    }
    PassTimer timer{traversal_ns[0], traversal_count[0]};
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
