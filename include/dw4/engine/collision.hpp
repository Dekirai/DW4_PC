#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw4::engine {

struct Node;

struct CollisionTriangle {
    Vec3f a, b, c;
    Vec3f normal;
    std::uint32_t kind{}; // beHitLandModelInfo _kind bits: 1 floor, 2 wall, 4 limit
    std::uint32_t group{}; // switchable child (HITLANDMODEL ONHIT / OFFHIT)
    std::uint16_t info = 0xFFFF; // owning beHitLandModelInfo (its name is the ride name)
    std::uint16_t child{};       // child index in the info's check switch
};

// A query segment (801251D0): origin, unit direction and length.
struct HitSegment {
    Vec3f origin, direction;
    float length{};
    static HitSegment between(const Vec3f& start, const Vec3f& end);
};
// A query result (beHitLandResultData): the hit point, the triangle's unit
// normal, the distance from the segment origin, and the info and child hit.
struct HitCandidate {
    Vec3f position, normal;
    float distance{};
    std::uint16_t info = 0xFFFF, child{};
};
// CHECKLAND (80300444): floor in the window, at or below it, any floor; the
// floor point (height -1 without one).
struct LandResult {
    bool in_range{}, adjusted{}, found{};
    Vec3f position;
    std::uint16_t info = 0xFFFF, child{};
};
// CHECKWALL (802FE7F0): the corrected position and the hits that moved it.
struct WallResult {
    Vec3f position;
    bool hit{};
    std::vector<HitCandidate> hits;
};

// Stage collision built from beHitLandModelInfo geometry graphs, queried like
// the original HITLANDMODEL traversal: segments against the triangles of the
// infos whose kind the query flags select.
class CollisionWorld {
public:
    void clear();
    void add_graph(const ObjectView& root, std::uint32_t kind);
    // Adds each child of the check switch as its own group; returns the
    // group ids in child order.
    // A check-switch child that is an igTransform keeps its triangles in its
    // own space and follows the scene node `resolve` gives for it (e.g. the
    // limit circle under a beChangePosTransform).
    std::vector<std::uint32_t> add_switch(const ObjectView& check_switch, std::uint32_t kind, std::uint16_t info = 0xFFFF,
                                          const std::function<const Node*(const ObjectView&)>& resolve = {});
    // Registers an info name; returns its index for add_switch / add_dynamic.
    std::uint16_t add_info(std::string name);
    const std::string& info_name(std::uint16_t info) const;
    void set_group_enabled(std::uint32_t group, bool enabled);
    bool group_enabled(std::uint32_t group) const { return group >= groups_.size() || groups_[group]; }
    // Kind 0x400 geometry (HITLAND_TRAVERSAL): a beCopyTransform child whose
    // matrix the candidate traversal 803009D0 copies from its source
    // transform [12] at query time (80128CF4 at 0x80300CE0). Its triangles
    // stay in the child's space; every query reads the source node set here,
    // the child's bit of the check-switch mask (0x80300CA0..0x80300CBC) and
    // the bound of the copy transform's first child moved by the source
    // translation, unrotated (0x80300CEC..0x80300D80).
    std::uint32_t add_dynamic(const ObjectView& copy_transform, std::uint32_t kind, std::uint16_t info = 0xFFFF,
                              std::uint16_t child = 0);
    // The followed source node and the switch node whose mask word holds the
    // child's enable bit `bit` (read in every query, never cached).
    void set_dynamic(std::uint32_t id, const Node* source, const Node* mask_switch, std::uint32_t bit);
    std::size_t triangle_count() const { return triangles_.size(); }
    const std::vector<CollisionTriangle>& triangles() const { return triangles_; }
    // HITLAND_TRAVERSAL: every segment against the triangles of the selected
    // infos within the X/Z bounds; all front-face hits (801238E8, side 0).
    void query(std::span<const HitSegment> segments, const Vec3f& minimum, const Vec3f& maximum, std::uint32_t flags,
               std::vector<HitCandidate>& out) const;
    LandResult check_land(const Vec3f& position, float width, float up, float down) const;
    WallResult check_wall(const Vec3f& position, const Vec3f& direction, float width, float down, std::uint32_t flags,
                          bool skip_preflight) const;
    // CHECKWALLFULL (802FF748), the query of a riding model: eight rays round
    // the position, first against the ridden walls (kind 0x1000).
    WallResult check_wall_full(const Vec3f& position, float width, float down, std::uint32_t flags) const;
    // HITLAND_TRAVERSAL info selection: the low nine kind bits must meet the
    // query flags; 0x800 on both suppresses, query 0x1000 needs kind 0x1000.
    static bool selects(std::uint32_t kind, std::uint32_t query) {
        if ((kind & 0x800u) && (query & 0x800u)) return false;
        if ((query & 0x1000u) && !(kind & 0x1000u)) return false;
        return (kind & query & 0x1FFu) != 0;
    }
    // Route planning between two steps (the test bots' walking maps) runs
    // thousands of queries while nothing moves. While a Still scope lives,
    // the moving children's enable bits (group or switch mask) and
    // placement are read once, so each query looks only at the enabled
    // ones; the results are the same as without it. Nothing the world
    // follows (node matrices, switch masks) may change meanwhile; a group
    // switched inside the scope is picked up. The answers of check_land and
    // check_wall in such scopes are also kept with the collision state they
    // were found in (the group switches and the enabled moving children
    // with their matrices, compared word for word), so the next plan in the
    // same state reads them instead of querying again.
    class Still {
    public:
        explicit Still(const CollisionWorld& world) : world_(world) {
            if (world_.still_depth_++ == 0) world_.still_stale_ = true;
        }
        ~Still() { --world_.still_depth_; }
        Still(const Still&) = delete;
        Still& operator=(const Still&) = delete;

    private:
        const CollisionWorld& world_;
    };

private:
    void add_node(const ObjectView& node, const Mat4& parent, int depth, std::uint32_t kind, std::uint32_t group,
                  std::vector<CollisionTriangle>* into = nullptr);
    struct Dynamic {
        std::vector<CollisionTriangle> local;
        float reach{};        // largest local distance from the origin
        const Node* node{};   // the followed node, whose matrix every query reads
        bool grouped{};       // a check-switch transform: enabled by its group
        std::uint32_t group{};
        // A kind 0x400 child (set_dynamic): the switch node holding the
        // check-switch mask and the child's bit in it.
        const Node* mask_switch{};
        std::uint32_t bit{};
        // The bound (igAABox) of the copy transform's first child, in the
        // child's space; without one every query takes the child.
        bool bounded{};
        Vec3f bound_min, bound_max;
        // `local` in world space at the node matrix `placed_matrix` (bit for
        // bit), so queries between two moves of the node reuse it.
        mutable std::vector<CollisionTriangle> placed;
        mutable Mat4 placed_matrix{};
        mutable bool placed_valid{};
    };
    // A dynamic child enabled at the start of a Still scope, with its
    // placement then: the origin (grouped children) or the translation
    // (bounded copy transforms).
    struct StillDynamic {
        std::uint32_t index{};
        Vec3f origin{};
        float tx{}, tz{};
    };
    mutable std::vector<StillDynamic> still_;
    mutable int still_depth_ = 0;
    mutable bool still_stale_ = true;
    void refresh_still() const;
    // Kept answers per collision state (Still): an open-addressing table
    // keyed by the arguments' bits.
    template <std::size_t N, class Value> class Answers {
    public:
        using Key = std::array<std::uint32_t, N>;
        const Value* find(const Key& key) const {
            if (slots_.empty()) return nullptr;
            const std::uint64_t h = hash(key);
            const std::size_t mask = slots_.size() - 1;
            for (std::size_t i = std::size_t(h) & mask;; i = (i + 1) & mask) {
                const Slot& slot = slots_[i];
                if (!slot.used) return nullptr;
                if (slot.hash == h && slot.key == key) return &slot.value;
            }
        }
        // `key` must not be in the table yet.
        const Value& insert(const Key& key, Value value) {
            if ((count_ + 1) * 2 > slots_.size()) grow();
            const std::uint64_t h = hash(key);
            return place(h, key, std::move(value));
        }
        std::size_t size() const { return count_; }
        void clear() {
            slots_.clear();
            count_ = 0;
        }

    private:
        struct Slot {
            std::uint64_t hash{};
            Key key{};
            Value value{};
            bool used{};
        };
        static std::uint64_t hash(const Key& key) {
            std::uint64_t h = 0x9E3779B97F4A7C15ull;
            for (const auto w : key) h = (h ^ w) * 0xFF51AFD7ED558CCDull;
            h ^= h >> 33; // (murmur finalizer: the low bits pick the slot)
            h *= 0xC4CEB9FE1A85EC53ull;
            return h ^ (h >> 33);
        }
        const Value& place(std::uint64_t h, const Key& key, Value value) {
            const std::size_t mask = slots_.size() - 1;
            std::size_t i = std::size_t(h) & mask;
            while (slots_[i].used) i = (i + 1) & mask;
            slots_[i] = Slot{h, key, std::move(value), true};
            ++count_;
            return slots_[i].value;
        }
        void grow() {
            std::vector<Slot> old = std::move(slots_);
            slots_.assign(old.empty() ? 4096 : old.size() * 2, Slot{});
            count_ = 0;
            for (auto& slot : old)
                if (slot.used) place(slot.hash, slot.key, std::move(slot.value));
        }
        std::vector<Slot> slots_;
        std::size_t count_{};
    };
    struct StillMemo {
        std::vector<std::uint32_t> state;
        Answers<6, LandResult> land;
        Answers<10, WallResult> wall;
        std::uint64_t used{};
        int scopes{}; // Still scopes met in this state (answers are kept from the second on)
    };
    mutable std::vector<std::unique_ptr<StillMemo>> memos_;
    mutable StillMemo* memo_{};
    mutable std::uint64_t memo_clock_{};
    mutable std::vector<std::uint32_t> still_state_;
    void forget_answers() const;
    StillMemo* still_memo() const;
    LandResult land_query(const Vec3f& position, float width, float up, float down) const;
    WallResult wall_query(const Vec3f& position, const Vec3f& direction, float width, float down, std::uint32_t flags,
                          bool skip_preflight) const;
    // The enabled dynamic triangles of the query (X/Z box minimum..maximum,
    // centre p and half-size `reach` of it), in world space: one list per
    // dynamic child, in their order.
    void dynamic_near(const Vec3f& p, float reach, const Vec3f& minimum, const Vec3f& maximum,
                      std::vector<const std::vector<CollisionTriangle>*>& out) const;
    std::vector<Dynamic> dynamic_;
    void index_triangle(std::size_t index);
    bool enabled(const CollisionTriangle& t) const { return t.group >= groups_.size() || groups_[t.group]; }
    std::vector<CollisionTriangle> triangles_;
    std::vector<bool> groups_{true};
    std::vector<std::string> info_names_;
    float cell_ = 20.0f;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid_;
    std::int64_t key(int x, int z) const { return (std::int64_t(x) << 32) ^ std::int64_t(std::uint32_t(z)); }
    void cells(const Vec3f& lo, const Vec3f& hi, std::vector<std::int64_t>& out) const;
};

} // namespace dw4::engine
