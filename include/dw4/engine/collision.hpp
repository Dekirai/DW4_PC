#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"

#include <functional>
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
    // Kind 0x400 geometry (HITLAND_TRAVERSAL): a beCopyTransform child whose
    // matrix is copied from its source transform [12] every frame. Its
    // triangles stay in the child's space and follow set_dynamic.
    std::uint32_t add_dynamic(const ObjectView& copy_transform, std::uint32_t kind, std::uint16_t info = 0xFFFF,
                              std::uint16_t child = 0);
    void set_dynamic(std::uint32_t id, const Mat4& matrix, bool enabled);
    std::size_t triangle_count() const { return triangles_.size(); }
    const std::vector<CollisionTriangle>& triangles() const { return triangles_; }
    // HITLAND_TRAVERSAL: every segment against the triangles of the selected
    // infos within the X/Z bounds; all front-face hits (801238E8, side 0).
    void query(const std::vector<HitSegment>& segments, const Vec3f& minimum, const Vec3f& maximum, std::uint32_t flags,
               std::vector<HitCandidate>& out) const;
    LandResult check_land(const Vec3f& position, float width, float up, float down) const;
    WallResult check_wall(const Vec3f& position, const Vec3f& direction, float width, float down, std::uint32_t flags,
                          bool skip_preflight) const;
    // HITLAND_TRAVERSAL info selection: the low nine kind bits must meet the
    // query flags; 0x800 on both suppresses, query 0x1000 needs kind 0x1000.
    static bool selects(std::uint32_t kind, std::uint32_t query) {
        if ((kind & 0x800u) && (query & 0x800u)) return false;
        if ((query & 0x1000u) && !(kind & 0x1000u)) return false;
        return (kind & query & 0x1FFu) != 0;
    }

private:
    void add_node(const ObjectView& node, const Mat4& parent, int depth, std::uint32_t kind, std::uint32_t group,
                  std::vector<CollisionTriangle>* into = nullptr);
    struct Dynamic {
        std::vector<CollisionTriangle> local;
        float reach{};       // largest local distance from the origin
        Mat4 matrix{};
        bool enabled{};
        const Node* node{};  // a followed transform: its matrix, enabled by the group
        std::uint32_t group{};
    };
    // The enabled dynamic triangles within `reach` of p, in world space.
    void dynamic_near(const Vec3f& p, float reach, std::vector<CollisionTriangle>& out) const;
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
