// The tick state (docs/SEAMLESS.md 6.13.2, round J): the archive, the ids,
// the header and sections, and the engine's own sections (the engine's
// fields, the generator, the game system, the messenger, the demo works, the
// scene graph's nodes and attributes). The managers' sections are their
// Manager::tick_state overrides (tick_state_managers.cpp).
#include "dw4/engine/tick_state.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online_membership.hpp"
#include "dw4/igb_document.hpp"
#include "online_detail.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <tuple>

namespace dw4 {
void serialize(engine::StateArchive& a, PadState& v); // save_state.cpp
} // namespace dw4

namespace dw4::engine {

void serialize(StateArchive& a, Cheats& v);           // save_state.cpp
void serialize(StateArchive& a, OnlineRules& v);      // save_state.cpp
void serialize(StateArchive& a, PersonalSession& v);  // save_state.cpp
void serialize_system(StateArchive& a, GameSystem& v); // save_state.cpp

namespace {

// The version: the snapshot's (it rises with every simulation change, 6.12)
// times 1000 plus this layout's own number.
// 2: every compiled chunk's label in the Lua section (lane jready's review).
constexpr std::uint32_t kTickLayout = 2;
constexpr std::uint32_t kMagic = 0x54345744; // "DW4T"

// Section tags (fixed order).
enum : std::uint32_t {
    kJournal = 0,
    kStructure = 9,
    kEngine = 1,
    kGenerator = 2,
    kSystem = 3,
    kMessenger = 4,
    kDemo = 5,
    kNodes = 6,
    kAttributes = 7,
    kLua = 8,
    kManagerBase = 0x100, // + the manager's index
};

const char* section_name(std::uint32_t tag) {
    switch (tag) {
    case kJournal: return "journal";
    case kStructure: return "structure";
    case kEngine: return "engine";
    case kGenerator: return "generator";
    case kSystem: return "system";
    case kMessenger: return "messenger";
    case kDemo: return "demo";
    case kNodes: return "nodes";
    case kAttributes: return "attributes";
    case kLua: return "lua";
    default: return nullptr;
    }
}

// ---- XXH64 (the public algorithm; Yann Collet's specification).
constexpr std::uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull, P3 = 1609587929392839161ull,
                        P4 = 9650029242287828579ull, P5 = 2870177450012600261ull;
inline std::uint64_t rotl(std::uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline std::uint64_t read64(const std::uint8_t* p) {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
inline std::uint32_t read32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline std::uint64_t round64(std::uint64_t acc, std::uint64_t input) {
    acc += input * P2;
    acc = rotl(acc, 31);
    return acc * P1;
}
inline std::uint64_t merge64(std::uint64_t acc, std::uint64_t val) {
    acc ^= round64(0, val);
    return acc * P1 + P4;
}

std::uint64_t mix_text(std::uint64_t h, std::string_view text) {
    for (const unsigned char c : text) h = (h ^ c) * 0x100000001b3ull;
    return (h ^ 0xFF) * 0x100000001b3ull;
}
std::uint64_t mix_u64(std::uint64_t h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) h = (h ^ ((v >> (8 * i)) & 0xFF)) * 0x100000001b3ull;
    return h;
}

constexpr std::uint32_t kOwned = 0x80000000u;

} // namespace

std::uint64_t xxh64(std::span<const std::uint8_t> bytes, std::uint64_t seed) {
    const std::uint8_t* p = bytes.data();
    const std::uint8_t* const end = p + bytes.size();
    std::uint64_t h;
    if (bytes.size() >= 32) {
        const std::uint8_t* const limit = end - 32;
        std::uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        do {
            v1 = round64(v1, read64(p));
            v2 = round64(v2, read64(p + 8));
            v3 = round64(v3, read64(p + 16));
            v4 = round64(v4, read64(p + 24));
            p += 32;
        } while (p <= limit);
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        h = merge64(h, v1);
        h = merge64(h, v2);
        h = merge64(h, v3);
        h = merge64(h, v4);
    } else {
        h = seed + P5;
    }
    h += bytes.size();
    while (p + 8 <= end) {
        h ^= round64(0, read64(p));
        h = rotl(h, 27) * P1 + P4;
        p += 8;
    }
    if (p + 4 <= end) {
        h ^= std::uint64_t(read32(p)) * P1;
        h = rotl(h, 23) * P2 + P3;
        p += 4;
    }
    while (p < end) {
        h ^= (*p) * P5;
        h = rotl(h, 11) * P1;
        ++p;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}

std::uint32_t tick_state_version() { return snapshot_version() * 1000u + kTickLayout; }

// ---- ids
TickIds::TickIds(Engine& engine) : engine_(engine) {
    auto add_document = [&](const IgbDocument* d) {
        if (!d || document_ids_.contains(d)) return;
        documents_.push_back(d);
        document_ids_[d] = std::uint32_t(documents_.size());
    };
    for (const auto& file : engine.files_) add_document(file.document.get());
    for (const auto& retired : engine.retired_) add_document(retired.get());
    engine.scenes_.for_each_node([&](const IgbDocument* d, std::uint32_t reference, const Node* node) {
        node_ids_[node] = (std::uint64_t(document(d)) << 32) | reference;
    });
    engine.scenes_.for_each_attribute([&](const IgbDocument* d, std::uint32_t reference, const Attribute* attribute) {
        attribute_ids_[attribute] = (std::uint64_t(document(d)) << 32) | reference;
    });
    // Owned nodes: the engine's (owner 0 the global root, 1 the scene
    // copies), then each manager's (owner 2 + its index).
    auto add_owned = [&](std::uint32_t owner, const std::vector<Node*>& nodes) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!nodes[i]) continue;
            const std::uint64_t id = (std::uint64_t(kOwned | owner) << 32) | std::uint32_t(i);
            node_ids_[nodes[i]] = id;
            owned_[id] = nodes[i];
            owned_list_.push_back({owner, nodes[i]});
        }
    };
    add_owned(0, {&engine.global_root_});
    {
        std::vector<Node*> copies;
        for (const auto& node : engine.copy_nodes_) copies.push_back(node.get());
        add_owned(1, copies);
        // The copies' own attributes (copied line colours).
        for (std::size_t i = 0; i < engine.copy_attributes_.size(); ++i)
            attribute_ids_[engine.copy_attributes_[i].get()] = (std::uint64_t(kOwned | 1u) << 32) | std::uint32_t(i);
    }
    for (std::size_t m = 0; m < engine.managers_.size(); ++m) {
        std::vector<Node*> nodes;
        engine.managers_[m]->owned_nodes(nodes);
        add_owned(2 + std::uint32_t(m), nodes);
        std::vector<const Ram*> rams;
        engine.managers_[m]->tick_rams(rams);
        register_rams(std::uint32_t(m) + 1, rams);
    }
}

std::uint32_t TickIds::document(const IgbDocument* d) const {
    if (!d) return 0;
    const auto it = document_ids_.find(d);
    if (it == document_ids_.end()) throw TickStateError("ids", "a document outside the engine's files");
    return it->second;
}
const IgbDocument* TickIds::document(std::uint32_t id) const {
    if (id == 0) return nullptr;
    if (id > documents_.size()) throw TickStateError("ids", "a document id beyond the engine's files");
    return documents_[id - 1];
}
void TickIds::node_id(const Node* node, std::uint32_t& a, std::uint32_t& b) const {
    if (!node) {
        a = b = 0;
        return;
    }
    const auto it = node_ids_.find(node);
    if (it == node_ids_.end()) throw TickStateError("ids", "a node without an id: " + node->name);
    a = std::uint32_t(it->second >> 32);
    b = std::uint32_t(it->second);
}
Node* TickIds::node(std::uint32_t a, std::uint32_t b) const {
    if (a == 0) return nullptr;
    if (a & kOwned) {
        const auto it = owned_.find((std::uint64_t(a) << 32) | b);
        if (it == owned_.end()) throw TickStateError("ids", "an owned node the engine does not have");
        return it->second;
    }
    return engine_.scenes_.node(document(a), b); // made by id when this engine has not made it yet
}
void TickIds::attribute_id(const Attribute* attribute, std::uint32_t& a, std::uint32_t& b) const {
    if (!attribute) {
        a = b = 0;
        return;
    }
    const auto it = attribute_ids_.find(attribute);
    if (it == attribute_ids_.end()) throw TickStateError("ids", "an attribute without an id");
    a = std::uint32_t(it->second >> 32);
    b = std::uint32_t(it->second);
}
const Attribute* TickIds::attribute(std::uint32_t a, std::uint32_t b) const {
    if (a == 0) return nullptr;
    if (a & kOwned) {
        if ((a & ~kOwned) != 1u || b >= engine_.copy_attributes_.size()) throw TickStateError("ids", "an owned attribute the engine does not have");
        return engine_.copy_attributes_[b].get();
    }
    return engine_.scenes_.attribute_at(document(a), b);
}
void TickIds::register_rams(std::uint32_t manager, const std::vector<const Ram*>& rams) {
    for (std::size_t i = 0; i < rams.size(); ++i) {
        const std::uint64_t id = (std::uint64_t(manager) << 32) | std::uint32_t(i);
        ram_ids_[rams[i]] = id;
        rams_[id] = rams[i];
    }
}
void TickIds::ram_id(const Ram* ram, std::uint32_t& a, std::uint32_t& b) const {
    if (!ram) {
        a = b = 0;
        return;
    }
    const auto it = ram_ids_.find(ram);
    if (it == ram_ids_.end()) throw TickStateError("ids", "a RAM without an id: " + ram->info_name);
    a = std::uint32_t(it->second >> 32);
    b = std::uint32_t(it->second);
}
Ram* TickIds::ram(std::uint32_t a, std::uint32_t b) const {
    if (a == 0) return nullptr;
    const auto it = rams_.find((std::uint64_t(a) << 32) | b);
    if (it == rams_.end()) throw TickStateError("ids", "a RAM the engine does not have");
    return const_cast<Ram*>(it->second);
}

// ---- archive
void TickArchive::node(Node*& node) {
    std::uint32_t a = 0, b = 0;
    if (!reading()) ids_.node_id(node, a, b);
    io(a);
    io(b);
    if (reading()) node = ids_.node(a, b);
}
void TickArchive::nodes(std::vector<Node*>& nodes) {
    std::uint32_t count = std::uint32_t(nodes.size());
    io(count);
    if (reading()) {
        if (count > 1u << 20) throw TickStateError("nodes", "an implausible node list");
        nodes.resize(count);
    }
    for (auto& n : nodes) node(n);
}
void TickArchive::attribute(const Attribute*& attribute) {
    std::uint32_t a = 0, b = 0;
    if (!reading()) ids_.attribute_id(attribute, a, b);
    io(a);
    io(b);
    if (reading()) attribute = ids_.attribute(a, b);
}
void TickArchive::view(ObjectView& view) {
    std::uint32_t a = view ? ids_.document(view.document()) : 0, b = view.reference();
    io(a);
    io(b);
    if (reading()) view = a ? ObjectView(ids_.document(a), b) : ObjectView{};
}
void TickArchive::ram_base(Ram*& ram) {
    std::uint32_t a = 0, b = 0;
    if (!reading()) ids_.ram_id(ram, a, b);
    io(a);
    io(b);
    if (reading()) ram = ids_.ram(a, b);
}

// ---- header and sections
namespace {
struct Writer {
    std::vector<std::uint8_t> out;
    std::uint32_t sections = 0;
    void section(std::uint32_t tag, const std::vector<std::uint8_t>& bytes) {
        auto put = [&](std::uint32_t v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
            out.insert(out.end(), p, p + 4);
        };
        put(tag);
        put(std::uint32_t(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
        ++sections;
    }
};

void header_io(StateArchive& a, TickHeader& h) {
    std::uint32_t magic = kMagic;
    a.io(magic);
    if (a.reading() && magic != kMagic) throw TickStateError("header", "not a tick state");
    a.io(h.version);
    if (a.reading() && h.version != tick_state_version()) throw TickStateError("header", "a tick state of another version");
    a.io(h.form);
    a.io(h.stage);
    a.io(h.frame);
    a.io(h.load_end);
    a.io(h.topology);
    a.io(h.sections);
}

// The header's length in `bytes` (decoded again).
std::size_t header_length(std::span<const std::uint8_t> bytes, TickHeader& h) {
    StateArchive a(bytes);
    header_io(a, h);
    // StateArchive has no position accessor: measure by re-encoding.
    std::vector<std::uint8_t> again;
    StateArchive w(again);
    auto copy = h;
    header_io(w, copy);
    return again.size();
}
} // namespace

TickHeader decode_tick_header(std::span<const std::uint8_t> bytes) {
    TickHeader h;
    (void)header_length(bytes, h);
    return h;
}

std::vector<TickSection> tick_sections(std::span<const std::uint8_t> bytes) {
    TickHeader h;
    std::size_t at = header_length(bytes, h);
    std::vector<TickSection> out;
    for (std::uint32_t i = 0; i < h.sections; ++i) {
        if (at + 8 > bytes.size()) throw TickStateError("header", "a truncated section table");
        TickSection s;
        s.tag = read32(bytes.data() + at);
        const std::uint32_t length = read32(bytes.data() + at + 4);
        at += 8;
        if (length > bytes.size() - at) throw TickStateError("header", "a truncated section");
        s.bytes = bytes.subspan(at, length);
        at += length;
        if (const char* name = section_name(s.tag)) s.name = name;
        else {
            // A manager's section starts with its type name.
            s.name = "manager " + std::to_string(s.tag - kManagerBase);
            if (s.bytes.size() >= 4) {
                const std::uint32_t n = read32(s.bytes.data());
                if (n > 0 && n < 64 && n + 4 <= s.bytes.size()) s.name = std::string(reinterpret_cast<const char*>(s.bytes.data() + 4), n);
            }
        }
        out.push_back(s);
    }
    if (at != bytes.size()) throw TickStateError("header", "trailing bytes");
    return out;
}

std::string first_differing_section(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    const auto sa = tick_sections(a), sb = tick_sections(b);
    for (std::size_t i = 0; i < std::max(sa.size(), sb.size()); ++i) {
        if (i >= sa.size() || i >= sb.size()) return "section count";
        if (sa[i].tag != sb[i].tag) return "section order";
        if (sa[i].bytes.size() != sb[i].bytes.size() || !std::equal(sa[i].bytes.begin(), sa[i].bytes.end(), sb[i].bytes.begin()))
            return sa[i].name;
    }
    return {};
}

// ---- the engine
namespace {
// Scenario JS (set_tick_state_skip_hp): the model RAMs' HP and the system
// generator are left out; the generator is in the net fingerprint, so a
// rebuild drifts at once whenever the visit drew from it after its load.
bool g_tick_state_skip_hp = false;
}
void Engine::set_tick_state_skip_hp(bool on) { g_tick_state_skip_hp = on; }
bool Engine::tick_state_skip_hp() { return g_tick_state_skip_hp; }

bool Engine::state_safe() const { return state_unsafe_reason().empty(); }

std::string Engine::state_unsafe_reason() const {
    if (!online_.enabled) return "offline";
    if (hub_stage(stage_name_)) return "hub stage";
    if (!stage_active_ || loader_state_ != -1) return "loading";
    if (!pending_stage_.empty() || reloading_) return "stage pending";
    if (torn_down_ || rebooting_) return "teardown";
    if (hold_fields_ > 0 || held_) return "held field";
    if (barrier_ || barrier_pads_) return "barrier";
    if (pending_fork_ || split_) return "fork";
    if (script_depth_ != 0 || trigger_slot_ != -1 || scene_scope_ != -1 || !model_contexts_.empty()) return "inside a script";
    if (!messenger_.between_steps() || !demo_.between_steps()) return "inside a step";
    if (const auto* windows = get<SubWindowManager>(); windows && windows->window_open()) return "window program";
    return {};
}

std::uint64_t Engine::topology_hash() const {
    std::uint64_t h = 0xcbf29ce484222325ull;
    h = mix_text(h, stage_name_);
    for (const auto& file : files_) h = mix_text(h, file.logical_path);
    for (const auto& m : managers_) {
        std::vector<const Ram*> rams;
        m->tick_rams(rams);
        std::vector<Node*> nodes;
        m->owned_nodes(nodes);
        h = mix_u64(mix_u64(mix_text(h, m->type_name()), rams.size()), nodes.size());
    }
    for (const auto& row : messenger_.rows()) {
        h = mix_text(h, row.category);
        h = mix_text(h, row.name ? *row.name : std::string_view("\x01"));
        h = mix_u64(h, std::uint64_t(std::uint32_t(row.index)));
    }
    for (const auto& s : schedule_) h = mix_u64(mix_text(h, s.manager->type_name()), std::uint64_t(s.slot));
    h = mix_u64(h, copy_nodes_.size());
    h = mix_u64(h, attached_roots_.size());
    for (const auto& e : journal_) h = mix_u64(mix_text(mix_text(mix_u64(h, e.kind), e.text), e.text2), (std::uint64_t(std::uint32_t(e.a)) << 32) | std::uint32_t(e.b));
    return h;
}

void Engine::tick_engine(TickArchive& a) {
    a.same(stage_name_, "stage");
    a.same(stage_folder_, "stage folder");
    a.io(time_);
    a.io(frame_);
    a.io(update_frame_);
    a.io(clock_lateness_);
    a.io(applied_lateness_);
    a.io(world_delta_);
    a.io(pad_input_enabled_);
    {
        auto pads = pads_.states();
        a.io(pads);
        if (a.reading()) pads_.restore(pads);
    }
    console_reset_.serialize(a);
    a.io(stage_active_);
    a.io(soft_reset_allowed_);
    a.io(loader_teardown_);
    a.io(loader_common_);
    a.io(loader_fast_);
    a.io(cheats_);
    a.io(flags_);
    a.io(counters_);
    a.io(common_queue_);
    a.nodes(attached_roots_);
    a.node(loading_node_);
    a.io(union_frame_);
    a.io(union_time_);
    a.io(load_end_frame_);
    // Online.
    a.io(online_);
    a.io(personal_);
    a.io(scoped_pads_off_);
    a.io(own_window_hold_);
    a.io(own_scene_hold_);
    a.io(scoped_layers_off_);
    a.io(talk_globals_side_);
    a.io(talk_waits_);
    tick_exit(a);
}

// The Lua VM (Lua4Vm::save_state) and the talk globals (Lua values of the
// VM): every chunk compiled in this visit by (document id, reference) with
// its label (Engine::lua_program_labels_; its closures name their programs by
// that id), made on the reader as its closures or the script runs need them.
// (Before lane jready's review only the closures' programs: a chunk two script
// names share, compiled by a rebuilt engine after T under the other name,
// got another label than the donor's, and its STATEHASH differed until those
// closures were gone.)
void Engine::tick_lua(TickArchive& a) {
    struct Program {
        std::uint64_t id{};
        std::string label;
    };
    std::vector<Program> programs;
    std::vector<std::uint8_t> vm;
    if (!a.reading()) {
        if (!lua_) throw TickStateError("lua", "no VM");
        std::unordered_map<const void*, std::uint64_t> known;
        for (const auto& [key, program] : lua_programs_)
            known[program.identity()] = (std::uint64_t(a.ids().document(static_cast<const IgbDocument*>(key.first))) << 32) | key.second;
        vm = lua_->save_state([&](const void* program) {
            const auto it = known.find(program);
            if (it == known.end()) throw TickStateError("lua", "a closure of a program outside the cache");
            return it->second;
        });
        std::map<std::uint64_t, std::string> labels;
        for (const auto& [key, label] : lua_program_labels_) try {
                labels[(std::uint64_t(a.ids().document(static_cast<const IgbDocument*>(key.first))) << 32) | key.second] = label;
            } catch (const TickStateError&) {
                // (A chunk of a document outside the engine's files: no closure
                // may run it, and no reader could make it.)
            }
        for (const auto& [id, label] : labels) programs.push_back({id, label});
    }
    std::uint32_t count = std::uint32_t(programs.size());
    a.io(count);
    if (a.reading()) {
        if (count > 1u << 16) throw TickStateError("lua", "implausible programs");
        programs.resize(count);
    }
    for (auto& p : programs) {
        a.io(p.id);
        a.io(p.label);
    }
    if (a.reading()) {
        // The donor's labels; a chunk this engine compiled under another label
        // (or that the state does not know: a trial's wrong steps) is made
        // again when it is needed.
        lua_program_labels_.clear();
        for (const auto& p : programs)
            lua_program_labels_[std::make_pair(static_cast<const void*>(a.ids().document(std::uint32_t(p.id >> 32))), std::uint32_t(p.id))] =
                p.label;
        std::erase_if(lua_programs_, [&](const auto& entry) {
            const auto it = lua_program_labels_.find(entry.first);
            return it == lua_program_labels_.end() || it->second != entry.second.info().label;
        });
    }
    auto program_of = [&](std::uint64_t id) -> Lua4Program {
        const auto* document = a.ids().document(std::uint32_t(id >> 32));
        const auto key = std::make_pair(static_cast<const void*>(document), std::uint32_t(id));
        auto found = lua_programs_.find(key);
        if (found == lua_programs_.end()) {
            const auto label = lua_program_labels_.find(key);
            if (label == lua_program_labels_.end()) throw TickStateError("lua", "a closure of a program the state does not list");
            const ObjectView object(document, std::uint32_t(id));
            found = lua_programs_.emplace(key, read_lua4_chunk(object.memory(3), label->second)).first;
        }
        return found->second;
    };
    a.io(vm);
    if (a.reading()) {
        if (!lua_) throw TickStateError("lua", "no VM");
        lua_->load_state(vm, program_of);
    }
    for (auto& side : talk_globals_)
        for (auto& value : side) {
            std::vector<std::uint8_t> bytes;
            bool has = value.has_value();
            a.io(has);
            if (!has) {
                if (a.reading()) value.reset();
                continue;
            }
            if (!a.reading()) lua_->write_value(bytes, *value);
            a.io(bytes);
            if (a.reading()) {
                std::size_t at = 0;
                value = lua_->read_value(bytes, at);
                if (at != bytes.size()) throw TickStateError("lua", "a damaged talk global");
            }
        }
}

void serialize(StateArchive& a, Packet& v) {
    a.io(v.bools);
    a.io(v.ints);
    a.io(v.floats);
    a.io(v.longs);
    a.io(v.vectors);
    a.io(v.strings);
    a.io(v.matrices);
    std::uint32_t objects = std::uint32_t(v.objects.size());
    a.io(objects);
    if (objects) throw TickStateError("messenger", "a packet holds objects");
    if (a.reading()) v.objects.clear();
}

void MessageObjects::tick_state(TickArchive& a) {
    std::uint32_t count = std::uint32_t(objects_.size());
    a.io(count);
    if (a.reading()) {
        if (count > 1u << 16) throw TickStateError("messenger", "implausible message objects");
        objects_.resize(count);
    }
    for (auto& o : objects_) {
        a.io(o.bools);
        a.io(o.first_bool);
    }
    a.io(current_);
    a.io(idle_);
    a.io(made_);
}

void Messenger::tick_state(TickArchive& a) {
    a.same(std::uint32_t(rows_.size()), "messenger rows");
    std::uint32_t count = std::uint32_t(deferred_.size());
    a.io(count);
    if (a.reading()) {
        if (count > 1u << 20) throw TickStateError("messenger", "implausible deferred messages");
        deferred_.resize(count);
    }
    for (auto& d : deferred_) {
        a.io(d.time);
        a.io(d.category);
        a.io(d.name);
        a.io(d.index);
        a.io(d.command);
        a.io(d.arguments);
        a.io(d.object);
        a.io(d.trigger);
        a.io(d.scope);
    }
    a.io(deferred_records_);
    a.io(paused_at_);
    objects_.tick_state(a);
}

bool DemoManager::has_port() const {
    for (const auto& work : running_)
        for (const auto& record : work->records)
            if (record.port) return true;
    return false;
}

void DemoManager::tick_state(TickArchive& a) {
    std::uint32_t count = std::uint32_t(running_.size());
    a.io(count);
    if (a.reading()) {
        if (count > 4096) throw TickStateError("demo", "implausible demo works");
        running_.clear();
        for (std::uint32_t i = 0; i < count; ++i) running_.push_back(std::make_unique<DemoWork>());
    }
    for (auto& work : running_) {
        std::uint32_t records = std::uint32_t(work->records.size());
        a.io(records);
        if (a.reading()) {
            if (records > 1u << 16) throw TickStateError("demo", "implausible demo records");
            work->records.resize(records);
        }
        for (auto& r : work->records) {
            a.engine().tick_port(a, r.port);
            a.io(r.type);
            a.io(r.category);
            a.io(r.name);
            a.io(r.index);
            a.io(r.command);
            a.io(r.argument);
            a.io(r.delay_frames);
            a.io(r.case_value);
        }
        a.io(work->cursor);
        a.io(work->trigger);
        a.io(work->scope);
    }
}

void serialize(StateArchive& a, ParticleTimerState& v) {
    a.io(v.running);
    a.io(v.bias);
    a.io(v.time);
}
void serialize(StateArchive& a, NodeAnim& v) {
    a.io(v.start);
    a.io(v.loop_start);
    a.io(v.loop_end);
    a.io(v.fixed);
    a.io(v.first);
    a.io(v.scale);
    a.io(v.bias);
    a.io(v.before);
    a.io(v.played);
    a.io(v.loading);
    a.io(v.lock);
    a.io(v.play_limit);
}
void serialize(StateArchive& a, TextureSampler& v) {
    a.io(v.mag_filter);
    a.io(v.min_filter);
    a.io(v.wrap_s);
    a.io(v.wrap_t);
}

namespace {
// The mutable fields of one node (the topology of its children included:
// the owned nodes' children change with LAYER ORDER, weapons and copies).
template <class T> bool bits_equal(const T& x, const T& y) { return std::memcmp(&x, &y, sizeof(T)) == 0; }

// The field groups of a node that differ from a fresh Node's (a mask, then
// the groups present; the reader resets the absent ones). Floats compare by
// their bits.
void node_values(TickArchive& a, Node& n) {
    static const Node fresh{};
    enum : std::uint32_t {
        children = 1u << 0, appearance = 1u << 1, matrix = 1u << 2, has_matrix = 1u << 3, mask = 1u << 4, clock = 1u << 5,
        fixed = 1u << 6, colour = 1u << 7, overrides = 1u << 8, hidden = 1u << 9, runtime = 1u << 10, lod = 1u << 11,
        actor = 1u << 12, bone = 1u << 13, copy = 1u << 14, timer = 1u << 15, live = 1u << 16, recorded = 1u << 17, anim = 1u << 18,
    };
    std::uint32_t present = 0;
    if (!a.reading()) {
        if (!n.children.empty()) present |= children;
        if (!n.appearance_roots.empty()) present |= appearance;
        if (!bits_equal(n.matrix, fresh.matrix)) present |= matrix;
        if (n.has_matrix) present |= has_matrix;
        if (!n.mask.empty()) present |= mask;
        if (!bits_equal(n.time_scale, fresh.time_scale) || n.time_bias || n.time_override) present |= clock;
        if (n.fixed_time) present |= fixed;
        if (n.color_override) present |= colour;
        if (n.blend_override || n.texture_override || n.lighting_override) present |= overrides;
        if (n.hidden) present |= hidden;
        if (n.runtime_matrix) present |= runtime;
        if (!n.lod_ranges.empty() || n.lod_child != -1 || n.lod_index != -1 || !bits_equal(n.lod_lower, fresh.lod_lower) ||
            !bits_equal(n.lod_upper, fresh.lod_upper))
            present |= lod;
        if (!bits_equal(n.actor_matrix, fresh.actor_matrix)) present |= actor;
        if (n.bone_index != -1) present |= bone;
        if (n.copy_source || n.copy_actor || n.copy_resolved) present |= copy;
        if (n.timer.running != fresh.timer.running || n.timer.bias || n.timer.time) present |= timer;
        if (n.particle_live) present |= live;
        if (n.recorded) present |= recorded;
        if (n.anim) present |= anim;
    }
    a.same(n.kind, "node kind");
    a.io(present);
    auto group = [&](std::uint32_t bit, auto&& write, auto&& reset) {
        if (present & bit) write();
        else if (a.reading()) reset();
    };
    group(children, [&] { a.nodes(n.children); }, [&] { n.children.clear(); });
    group(appearance, [&] { a.nodes(n.appearance_roots); }, [&] { n.appearance_roots.clear(); });
    group(matrix, [&] { a.io(n.matrix); }, [&] { n.matrix = fresh.matrix; });
    if (a.reading()) n.has_matrix = (present & has_matrix) != 0;
    group(mask, [&] { a.io(n.mask); }, [&] { n.mask.clear(); });
    group(clock, [&] {
        a.io(n.time_scale);
        a.io(n.time_bias);
        a.io(n.time_override);
    }, [&] {
        n.time_scale = fresh.time_scale;
        n.time_bias = 0;
        n.time_override = false;
    });
    group(fixed, [&] { a.io(n.fixed_time); }, [&] { n.fixed_time.reset(); });
    group(colour, [&] { a.io(n.color_override); }, [&] { n.color_override.reset(); });
    group(overrides, [&] {
        a.io(n.blend_override);
        a.io(n.texture_override);
        a.io(n.lighting_override);
    }, [&] {
        n.blend_override.reset();
        n.texture_override.reset();
        n.lighting_override.reset();
    });
    if (a.reading()) n.hidden = (present & hidden) != 0;
    group(runtime, [&] { a.io(n.runtime_matrix); }, [&] { n.runtime_matrix.reset(); });
    group(lod, [&] {
        a.io(n.lod_ranges);
        a.io(n.lod_child);
        a.io(n.lod_index);
        a.io(n.lod_lower);
        a.io(n.lod_upper);
    }, [&] {
        n.lod_ranges.clear();
        n.lod_child = n.lod_index = -1;
        n.lod_lower = fresh.lod_lower;
        n.lod_upper = fresh.lod_upper;
    });
    group(actor, [&] { a.io(n.actor_matrix); }, [&] { n.actor_matrix = fresh.actor_matrix; });
    group(bone, [&] { a.io(n.bone_index); }, [&] { n.bone_index = -1; });
    group(copy, [&] {
        a.node(n.copy_source);
        a.node(n.copy_actor);
        a.io(n.copy_resolved);
    }, [&] {
        n.copy_source = n.copy_actor = nullptr;
        n.copy_resolved = false;
    });
    group(timer, [&] { a.io(n.timer); }, [&] { n.timer = fresh.timer; });
    if (a.reading()) n.particle_live = (present & live) != 0;
    group(recorded, [&] { a.io(n.recorded); }, [&] { n.recorded.reset(); });
    group(anim, [&] { a.io(n.anim); }, [&] { n.anim.reset(); });
}
} // namespace

void Engine::tick_nodes(TickArchive& a) {
    // The SceneCache's nodes by id (made by id on the reader when it has not
    // made them yet), then the owned nodes in the ids' order.
    struct Entry {
        std::uint32_t document, reference;
        Node* node;
    };
    std::vector<Entry> entries;
    if (!a.reading()) {
        scenes_.for_each_node([&](const IgbDocument* d, std::uint32_t reference, const Node* node) {
            entries.push_back({a.ids().document(d), reference, const_cast<Node*>(node)});
        });
        std::sort(entries.begin(), entries.end(), [](const Entry& x, const Entry& y) {
            return std::tie(x.document, x.reference) < std::tie(y.document, y.reference);
        });
    }
    std::uint32_t count = std::uint32_t(entries.size());
    a.io(count);
    if (a.reading() && count > 1u << 22) throw TickStateError("nodes", "implausible node count");
    // Ids first, so every node a value names exists before the values.
    if (a.reading()) entries.resize(count);
    for (auto& e : entries) {
        a.io(e.document);
        a.io(e.reference);
        if (a.reading()) e.node = a.ids().node(e.document, e.reference);
    }
    for (auto& e : entries) node_values(a, *e.node);
    const auto& owned = a.ids().owned_nodes();
    a.same(std::uint32_t(owned.size()), "owned nodes");
    for (const auto& [owner, node] : owned) {
        a.same(owner, "owned node owner");
        node_values(a, *node);
    }
    // The attributes the managers edit (LIGHTCTRL SETCOLOR, TEXTCTL COLOR).
}

namespace {
void attribute_values(TickArchive& a, Attribute& v) {
    a.same(v.kind, "attribute kind");
    a.io(v.unit);
    a.io(v.enabled);
    a.io(v.e0);
    a.io(v.e1);
    a.io(v.e2);
    a.io(v.f0);
    a.io(v.f1);
    a.io(v.color);
    a.io(v.ambient);
    a.io(v.emission);
    a.io(v.specular);
    a.io(v.matrix);
    a.io(v.sampler);
    for (auto& x : v.viewport) a.io(x);
    for (auto& x : v.mask) a.io(x);
}
} // namespace

namespace {
void journal_io(StateArchive& a, std::vector<Engine::TopologyEntry>& journal) {
    std::uint32_t count = std::uint32_t(journal.size());
    a.io(count);
    if (a.reading()) {
        if (count > 1u << 16) throw TickStateError("journal", "implausible journal");
        journal.resize(count);
    }
    for (auto& e : journal) {
        a.io(e.frame);
        a.io(e.kind);
        a.io(e.text);
        a.io(e.text2);
        a.io(e.a);
        a.io(e.b);
    }
}
} // namespace

void Engine::rerun(const TopologyEntry& e) {
    switch (e.kind) {
    case TopologyEntry::package:
        if (!load_common_now(e.text)) throw TickStateError("topology", "the package " + e.text + " does not load");
        break;
    case TopologyEntry::window_copy:
        if (!make_window_copy(e.text, e.a, e.b)) throw TickStateError("topology", "the window copy " + e.text + " has no RAM");
        break;
    case TopologyEntry::line_copies:
        if (auto* texts = get<TextCtlManager>()) texts->replay_copies(*this, e.b, e.a, e.text, e.text2);
        break;
    default: throw TickStateError("topology", "an unknown journal entry");
    }
}

// The attributes the managers edited (LIGHTCTRL SETCOLOR, TEXTCTL COLOR:
// SceneCache::edit_attribute keeps their keys) and the copies' own; the
// reader's edited attributes that the state does not name go back to their
// data.
void Engine::tick_attributes(TickArchive& a) {
    struct Entry {
        std::uint32_t document, reference;
        Attribute* attribute;
    };
    std::vector<Entry> entries;
    if (!a.reading()) {
        scenes_.for_each_edited_attribute([&](const IgbDocument* d, std::uint32_t reference, Attribute* attribute) {
            entries.push_back({a.ids().document(d), reference, attribute});
        });
        std::sort(entries.begin(), entries.end(), [](const Entry& x, const Entry& y) {
            return std::tie(x.document, x.reference) < std::tie(y.document, y.reference);
        });
    }
    std::uint32_t count = std::uint32_t(entries.size());
    a.io(count);
    if (a.reading()) {
        if (count > 1u << 22) throw TickStateError("attributes", "implausible attribute count");
        entries.resize(count);
    }
    std::set<const Attribute*> named;
    for (auto& e : entries) {
        a.io(e.document);
        a.io(e.reference);
        if (a.reading()) {
            e.attribute = scenes_.edit_attribute(ObjectView(a.ids().document(e.document), e.reference));
            if (!e.attribute) throw TickStateError("attributes", "an attribute the engine cannot make");
            named.insert(e.attribute);
        }
        attribute_values(a, *e.attribute);
    }
    if (a.reading()) scenes_.reset_edited_attributes(named);
    a.same(std::uint32_t(copy_attributes_.size()), "copy attributes");
    for (auto& c : copy_attributes_) attribute_values(a, *c);
}

std::vector<std::uint8_t> Engine::save_tick_state(TickForm form) {
    if (const auto why = state_unsafe_reason(); !why.empty()) throw std::logic_error("tick state at an unsafe tick: " + why);
    const detail::MxcsrGuard mxcsr;
    TickIds ids(*this);
    Writer w;
    auto section = [&](std::uint32_t tag, auto&& body) {
        std::vector<std::uint8_t> bytes;
        TickArchive a(bytes, *this, ids, form);
        body(a);
        w.section(tag, bytes);
    };
    section(kJournal, [&](TickArchive& a) { journal_io(a, journal_); });
    section(kStructure, [&](TickArchive& a) {
        for (auto& m : managers_) m->tick_structure(a);
    });
    section(kEngine, [&](TickArchive& a) { tick_engine(a); });
    section(kGenerator, [&](TickArchive& a) {
        if (!g_tick_state_skip_hp) fight::generator().tick_io(a); // (scenario JS's missed field)
    });
    section(kSystem, [&](TickArchive& a) {
        auto* sys = get<GameSystem>();
        a.io(sys->registered_);
        serialize_system(a, *sys);
    });
    section(kMessenger, [&](TickArchive& a) { messenger_.tick_state(a); });
    section(kDemo, [&](TickArchive& a) { demo_.tick_state(a); });
    section(kLua, [&](TickArchive& a) { tick_lua(a); });
    section(kNodes, [&](TickArchive& a) { tick_nodes(a); });
    section(kAttributes, [&](TickArchive& a) { tick_attributes(a); });
    for (std::size_t m = 0; m < managers_.size(); ++m)
        section(kManagerBase + std::uint32_t(m), [&](TickArchive& a) {
            a.same(managers_[m]->type_name(), "manager");
            managers_[m]->tick_state(a);
        });
    // The header in front.
    TickHeader h;
    h.version = tick_state_version();
    h.form = form;
    h.stage = stage_name_;
    h.frame = frame_;
    h.load_end = std::uint32_t(load_end_frame_);
    h.topology = topology_hash();
    h.sections = w.sections;
    std::vector<std::uint8_t> out;
    StateArchive ha(out);
    header_io(ha, h);
    out.insert(out.end(), w.out.begin(), w.out.end());
    return out;
}

void Engine::apply_tick_state(std::span<const std::uint8_t> bytes) {
    const detail::MxcsrGuard mxcsr;
    const auto sections = tick_sections(bytes);
    const auto h = decode_tick_header(bytes);
    if (h.form != TickForm::raw) throw TickStateError("header", "a canonical state cannot be applied");
    if (h.stage != stage_name_) throw TickStateError("topology", "another stage");
    // The journal first: the entries this engine lacks run again in their
    // order (they make the files, RAMs, rows and nodes the values name).
    if (sections.empty() || sections[0].tag != kJournal) throw TickStateError("header", "no journal");
    {
        std::vector<TopologyEntry> theirs;
        StateArchive ja(sections[0].bytes);
        journal_io(ja, theirs);
        if (!ja.at_end()) throw TickStateError("journal", "trailing bytes");
        if (theirs.size() < journal_.size()) throw TickStateError("topology", "this engine's journal is longer");
        for (std::size_t i = 0; i < theirs.size(); ++i) {
            if (i < journal_.size()) {
                auto mine = journal_[i];
                mine.frame = theirs[i].frame;
                if (!(mine == theirs[i])) throw TickStateError("topology", "the journals differ at entry " + std::to_string(i));
                continue;
            }
            rerun(theirs[i]);
        }
        if (journal_.size() != theirs.size()) throw TickStateError("topology", "the journal's re-run made other entries");
        journal_ = theirs;
    }
    // The structure the managers make during play (weapon parts), with the
    // ids of the files and nodes the journal made.
    if (sections.size() < 2 || sections[1].tag != kStructure) throw TickStateError("header", "no structure");
    {
        TickIds early(*this);
        TickArchive sa(sections[1].bytes, *this, early);
        try {
            for (auto& m : managers_) m->tick_structure(sa);
        } catch (const TickStateError&) {
            throw;
        } catch (const std::exception& error) {
            throw TickStateError("structure", error.what());
        }
        if (!sa.at_end()) throw TickStateError("structure", "trailing bytes");
    }
    if (h.topology != topology_hash()) throw TickStateError("topology", "the topology hash differs");
    TickIds ids(*this);
    std::size_t index = 2;
    auto section = [&](std::uint32_t tag, auto&& body) {
        if (index >= sections.size() || sections[index].tag != tag) throw TickStateError("header", "a missing or misplaced section");
        const auto& s = sections[index++];
        TickArchive a(s.bytes, *this, ids);
        try {
            body(a);
        } catch (const TickStateError&) {
            throw;
        } catch (const std::exception& error) {
            throw TickStateError(s.name, error.what());
        }
        if (!a.at_end()) throw TickStateError(s.name, "the section has trailing bytes");
    };
    section(kEngine, [&](TickArchive& a) { tick_engine(a); });
    section(kGenerator, [&](TickArchive& a) {
        if (!g_tick_state_skip_hp) fight::generator().tick_io(a); // (scenario JS's missed field)
    });
    section(kSystem, [&](TickArchive& a) {
        auto* sys = get<GameSystem>();
        a.io(sys->registered_);
        serialize_system(a, *sys);
    });
    section(kMessenger, [&](TickArchive& a) { messenger_.tick_state(a); });
    section(kDemo, [&](TickArchive& a) { demo_.tick_state(a); });
    section(kLua, [&](TickArchive& a) { tick_lua(a); });
    section(kNodes, [&](TickArchive& a) { tick_nodes(a); });
    section(kAttributes, [&](TickArchive& a) { tick_attributes(a); });
    for (std::size_t m = 0; m < managers_.size(); ++m)
        section(kManagerBase + std::uint32_t(m), [&](TickArchive& a) {
            a.same(managers_[m]->type_name(), "manager");
            managers_[m]->tick_state(a);
        });
    if (index != sections.size()) throw TickStateError("header", "extra sections");
}

std::uint64_t Engine::tick_state_hash() {
    const auto bytes = save_tick_state(TickForm::canonical);
    return xxh64(bytes);
}

} // namespace dw4::engine
