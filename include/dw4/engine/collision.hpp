#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"

#include <unordered_map>
#include <vector>

namespace dw4::engine {

struct CollisionTriangle {
    Vec3f a, b, c;
    Vec3f normal;
    std::uint32_t kind{}; // beHitLandModelInfo _kind bits: 1 floor, 2 wall, 4 limit
    std::uint32_t group{}; // switchable child (HITLANDMODEL ONHIT / OFFHIT)
};

// Static stage collision built from beHitLandModelInfo geometry graphs.
// PROVISIONAL: floor/wall responses here are host approximations of the
// original CHECKLAND/CHECKWALL queries (80300444 / 802FFxxx), which already
// exist in dw4_core's hitland modules and will replace these queries once
// connected to the engine messenger.
class CollisionWorld {
public:
    void clear();
    void add_graph(const ObjectView& root, std::uint32_t kind);
    // Adds each child of the check switch as its own group; returns the
    // group ids in child order.
    std::vector<std::uint32_t> add_switch(const ObjectView& check_switch, std::uint32_t kind);
    void set_group_enabled(std::uint32_t group, bool enabled);
    // Kind 0x400 geometry (HITLAND_TRAVERSAL): a beCopyTransform child whose
    // matrix is copied from its source transform [12] every frame. Its
    // triangles stay in the child's space and follow set_dynamic.
    std::uint32_t add_dynamic(const ObjectView& copy_transform, std::uint32_t kind);
    void set_dynamic(std::uint32_t id, const Mat4& matrix, bool enabled);
    std::size_t triangle_count() const { return triangles_.size(); }
    const std::vector<CollisionTriangle>& triangles() const { return triangles_; }
    // Highest floor at (x, z) within [y - down, y + up]; false when none.
    // mask: the model's HitLandDeliv kind bits (players 7, enemies 11).
    bool floor_height(const Vec3f& position, float up, float down, float& height, Vec3f* normal = nullptr,
                      std::uint32_t mask = 7) const;
    // Push a vertical capsule (radius, from y+step to y+height) out of walls.
    Vec3f resolve_walls(const Vec3f& position, float radius, float step, float height, std::uint32_t mask = 7) const;
    // HITLAND_TRAVERSAL: the low nine kind bits must intersect the query mask;
    // bit 0x800 on both suppresses the geometry.
    static bool selects(std::uint32_t kind, std::uint32_t query) {
        if ((kind & 0x800u) && (query & 0x800u)) return false;
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
    };
    // The enabled dynamic triangles within `reach` of p, in world space.
    void dynamic_near(const Vec3f& p, float reach, std::vector<CollisionTriangle>& out) const;
    std::vector<Dynamic> dynamic_;
    void index_triangle(std::size_t index);
    bool enabled(const CollisionTriangle& t) const { return t.group >= groups_.size() || groups_[t.group]; }
    std::vector<CollisionTriangle> triangles_;
    std::vector<bool> groups_{true};
    float cell_ = 20.0f;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid_;
    std::int64_t key(int x, int z) const { return (std::int64_t(x) << 32) ^ std::int64_t(std::uint32_t(z)); }
    void cells(const Vec3f& lo, const Vec3f& hi, std::vector<std::int64_t>& out) const;
};

} // namespace dw4::engine
