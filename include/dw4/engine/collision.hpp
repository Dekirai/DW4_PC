#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"

#include <unordered_map>
#include <vector>

namespace dw4::engine {

struct CollisionTriangle {
    Vec3f a, b, c;
    Vec3f normal;
};

// Static stage collision built from beHitLandModelInfo geometry graphs.
// PROVISIONAL: floor/wall responses here are host approximations of the
// original CHECKLAND/CHECKWALL queries (80300444 / 802FFxxx), which already
// exist in dw4_core's hitland modules and will replace these queries once
// connected to the engine messenger.
class CollisionWorld {
public:
    void clear();
    void add_graph(const ObjectView& root);
    std::size_t triangle_count() const { return triangles_.size(); }
    // Highest floor at (x, z) within [y - down, y + up]; false when none.
    bool floor_height(const Vec3f& position, float up, float down, float& height, Vec3f* normal = nullptr) const;
    // Push a vertical capsule (radius, from y+step to y+height) out of walls.
    Vec3f resolve_walls(const Vec3f& position, float radius, float step, float height) const;

private:
    void add_node(const ObjectView& node, const Mat4& parent, int depth);
    void index_triangle(std::size_t index);
    std::vector<CollisionTriangle> triangles_;
    float cell_ = 20.0f;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid_;
    std::int64_t key(int x, int z) const { return (std::int64_t(x) << 32) ^ std::int64_t(std::uint32_t(z)); }
    void cells(const Vec3f& lo, const Vec3f& hi, std::vector<std::int64_t>& out) const;
};

} // namespace dw4::engine
