#pragma once

// The tick state (docs/SEAMLESS.md 6.13.2, round J): the engine's whole
// simulation state after a step and its simulation traversal, taken at a
// state-safe tick (Engine::state_safe) and put back into an engine of the
// same build and data whose topology equals the taker's (the same files in
// the same order, the same RAMs, rows and owned nodes). Online only: an
// offline engine never takes or applies one.
//
// Layout: the header (magic "DW4T", the version, the form, stage, frame,
// visit load end, topology hash, section count), then sections (tag u32,
// length u32, bytes) in a fixed order, so a mismatch can be located.
// Pointers travel as ids: documents by their place in the engine's file
// list, scene nodes by (document, object reference) for the SceneCache's
// nodes and by (owner, ordinal) for the nodes an owner made, RAMs by
// (manager, ordinal). The raw form carries everything; the canonical form
// zeroes the presentation-only fields (TickArchive::shown) and is for hashes
// (XXH64) and comparisons, never decoded into an engine.
#include "dw4/engine/math.hpp"
#include "dw4/engine/state_archive.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw4 {
class IgbDocument;
}

namespace dw4::engine {

class Engine;
class ObjectView;
struct Node;
struct Attribute;
struct Ram;
class Manager;

enum class TickForm : std::uint8_t { raw = 0, canonical = 1 };

// The layout version; it follows every simulation change (6.12), so a
// decoder refuses another build's state.
std::uint32_t tick_state_version();

// XXH64 (seed 0) over bytes.
std::uint64_t xxh64(std::span<const std::uint8_t> bytes, std::uint64_t seed = 0);

// A failed decode or apply: the section it happened in ("header",
// "topology", a manager's type name, ...).
class TickStateError : public std::runtime_error {
public:
    TickStateError(std::string section, const std::string& what)
        : std::runtime_error(section + ": " + what), section_(std::move(section)) {}
    const std::string& section() const { return section_; }

private:
    std::string section_;
};

// What a header says (decode_tick_header).
struct TickHeader {
    std::uint32_t version{};
    TickForm form{};
    std::string stage;
    std::uint64_t frame{};
    std::uint32_t load_end{};      // the visit's load end K (6.13.2 step 2), 0 unknown
    std::uint64_t topology{};      // the topology hash of the taker
    std::uint32_t sections{};
};
TickHeader decode_tick_header(std::span<const std::uint8_t> bytes);

// One section of a state (tag, its name, its bytes), for diagnosis
// (tools/online/tickstate_diff.py reads the same layout).
struct TickSection {
    std::uint32_t tag{};
    std::string name;
    std::span<const std::uint8_t> bytes;
};
std::vector<TickSection> tick_sections(std::span<const std::uint8_t> bytes);
// The first section whose bytes differ between two states ("" equal).
std::string first_differing_section(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);

// The pointer ids of one engine (built once per save or apply).
class TickIds {
public:
    explicit TickIds(Engine& engine);
    std::uint32_t document(const IgbDocument* document) const;
    const IgbDocument* document(std::uint32_t id) const;
    // Node ids: 0 null; a SceneCache node (document id, reference); an owned
    // node (0x8000'0000 | owner, ordinal).
    void node_id(const Node* node, std::uint32_t& a, std::uint32_t& b) const;
    Node* node(std::uint32_t a, std::uint32_t b) const; // creates a SceneCache node by id
    // Attribute ids: (document id, reference) of the SceneCache's attributes.
    void attribute_id(const Attribute* attribute, std::uint32_t& a, std::uint32_t& b) const;
    const Attribute* attribute(std::uint32_t a, std::uint32_t b) const;
    // RAM ids: (manager index + 1, ordinal in its RAM list); 0 null.
    void register_rams(std::uint32_t manager, const std::vector<const Ram*>& rams);
    void ram_id(const Ram* ram, std::uint32_t& a, std::uint32_t& b) const;
    Ram* ram(std::uint32_t a, std::uint32_t b) const;
    const std::vector<std::pair<std::uint32_t, Node*>>& owned_nodes() const { return owned_list_; }

private:
    Engine& engine_;
    std::vector<const IgbDocument*> documents_;
    std::unordered_map<const IgbDocument*, std::uint32_t> document_ids_;
    std::unordered_map<const Node*, std::uint64_t> node_ids_;
    std::unordered_map<std::uint64_t, Node*> owned_;
    std::vector<std::pair<std::uint32_t, Node*>> owned_list_;
    std::unordered_map<const Attribute*, std::uint64_t> attribute_ids_;
    std::unordered_map<const Ram*, std::uint64_t> ram_ids_;
    std::unordered_map<std::uint64_t, const Ram*> rams_;
};

// The archive of a tick state: StateArchive with the engine's ids and the
// form. One io() per field serves both directions.
class TickArchive : public StateArchive {
public:
    TickArchive(std::vector<std::uint8_t>& out, Engine& engine, TickIds& ids, TickForm form)
        : StateArchive(out), engine_(engine), ids_(ids), form_(form) {}
    TickArchive(std::span<const std::uint8_t> in, Engine& engine, TickIds& ids)
        : StateArchive(in), engine_(engine), ids_(ids), form_(TickForm::raw) {}
    Engine& engine() const { return engine_; }
    TickIds& ids() const { return ids_; }
    TickForm form() const { return form_; }
    bool canonical() const { return form_ == TickForm::canonical; }

    void node(Node*& node);
    void node(const Node*& node) {
        Node* n = const_cast<Node*>(node);
        this->node(n);
        node = n;
    }
    void nodes(std::vector<Node*>& nodes);
    void attribute(const Attribute*& attribute);
    void view(ObjectView& view);
    template <class R> void ram(R*& ram) {
        Ram* r = ram;
        ram_base(r);
        ram = static_cast<R*>(r);
    }
    // A presentation-only value: the canonical form writes its default.
    template <class T> void shown(T& value) {
        if (canonical() && !reading()) {
            T zero{};
            io(zero);
        } else {
            io(value);
        }
    }
    // A value both sides must already share (topology): the reader checks it.
    template <class T> void same(const T& value, const char* what) {
        T v = value;
        io(v);
        if (reading() && !(v == value)) throw TickStateError("topology", what);
    }

private:
    void ram_base(Ram*& ram);
    Engine& engine_;
    TickIds& ids_;
    TickForm form_;
};

// Serializers of the small value types the sections share.
inline void serialize(StateArchive& a, Mat4& v) { a.io(v.m); }
inline void serialize(StateArchive& a, Vec4f& v) {
    a.io(v.x);
    a.io(v.y);
    a.io(v.z);
    a.io(v.w);
}

} // namespace dw4::engine
