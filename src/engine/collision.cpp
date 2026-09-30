#include "dw4/engine/collision.hpp"

#include "dw4/engine/scene_graph.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace dw4::engine {

void CollisionWorld::clear() {
    dynamic_.clear();
    triangles_.clear();
    grid_.clear();
    groups_.assign(1, true);
}

std::vector<std::uint32_t> CollisionWorld::add_switch(const ObjectView& check_switch, std::uint32_t kind) {
    std::vector<std::uint32_t> ids;
    if (!check_switch) return ids;
    const auto children = check_switch.is_a("igGroup") ? check_switch.list(7) : std::vector<ObjectView>{check_switch};
    for (const auto& child : children) {
        const auto group = std::uint32_t(groups_.size());
        groups_.push_back(true);
        ids.push_back(group);
        add_node(child, Mat4{}, 1, kind, group);
    }
    return ids;
}

void CollisionWorld::set_group_enabled(std::uint32_t group, bool enabled) {
    if (group < groups_.size()) groups_[group] = enabled;
}

void CollisionWorld::cells(const Vec3f& lo, const Vec3f& hi, std::vector<std::int64_t>& out) const {
    const int x0 = int(std::floor(lo.x / cell_)), x1 = int(std::floor(hi.x / cell_));
    const int z0 = int(std::floor(lo.z / cell_)), z1 = int(std::floor(hi.z / cell_));
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) out.push_back(key(x, z));
}

void CollisionWorld::index_triangle(std::size_t index) {
    const auto& t = triangles_[index];
    const Vec3f lo{std::min({t.a.x, t.b.x, t.c.x}), 0, std::min({t.a.z, t.b.z, t.c.z})};
    const Vec3f hi{std::max({t.a.x, t.b.x, t.c.x}), 0, std::max({t.a.z, t.b.z, t.c.z})};
    if ((hi.x - lo.x) / cell_ > 64 || (hi.z - lo.z) / cell_ > 64) {
        // Very large triangles go into a coarse list checked everywhere.
        grid_[key(INT32_MAX, INT32_MAX)].push_back(std::uint32_t(index));
        return;
    }
    std::vector<std::int64_t> keys;
    cells(lo, hi, keys);
    for (auto k : keys) grid_[k].push_back(std::uint32_t(index));
}

void CollisionWorld::add_graph(const ObjectView& root, std::uint32_t kind) { add_node(root, Mat4{}, 0, kind, 0); }

std::uint32_t CollisionWorld::add_dynamic(const ObjectView& copy_transform, std::uint32_t kind) {
    Dynamic d;
    // The copy transform's own matrix is replaced by its source; its
    // children are read in its space.
    if (copy_transform)
        for (const auto& child : copy_transform.list(7)) add_node(child, Mat4{}, 1, kind, 0, &d.local);
    for (const auto& t : d.local)
        for (const auto& v : {t.a, t.b, t.c}) d.reach = std::max(d.reach, length(v));
    dynamic_.push_back(std::move(d));
    return std::uint32_t(dynamic_.size() - 1);
}

void CollisionWorld::set_dynamic(std::uint32_t id, const Mat4& matrix, bool enabled) {
    if (id >= dynamic_.size()) return;
    dynamic_[id].matrix = matrix;
    dynamic_[id].enabled = enabled;
}

void CollisionWorld::dynamic_near(const Vec3f& p, float reach, std::vector<CollisionTriangle>& out) const {
    for (const auto& d : dynamic_) {
        if (!d.enabled || d.local.empty()) continue;
        const Vec3f origin = transform_point(Vec3f{}, d.matrix);
        const float dx = p.x - origin.x, dz = p.z - origin.z;
        const float limit = d.reach + reach;
        if (dx * dx + dz * dz > limit * limit) continue;
        for (const auto& local : d.local) {
            CollisionTriangle t = local;
            t.a = transform_point(local.a, d.matrix);
            t.b = transform_point(local.b, d.matrix);
            t.c = transform_point(local.c, d.matrix);
            const Vec3f n = cross(t.b - t.a, t.c - t.a);
            if (length(n) < 1e-8f) continue;
            t.normal = normalize(n);
            t.group = UINT32_MAX; // not a switchable static group
            out.push_back(t);
        }
    }
}

void CollisionWorld::add_node(const ObjectView& node, const Mat4& parent, int depth, std::uint32_t kind, std::uint32_t group,
                              std::vector<CollisionTriangle>* into) {
    if (!node || depth > 64) return;
    Mat4 model = parent;
    if (node.is_a("igTransform")) {
        if (auto m = node.mat4(8)) model = *m * parent;
    }
    if (node.is_a("igGeometry")) {
        for (const auto& attr : node.list(8)) {
            if (!attr || !attr.is_a("igGeometryAttr")) continue;
            const auto data = decode_geometry(attr);
            if (!data.valid) continue;
            for (std::size_t i = 0; i + 2 < data.indices.size(); i += 3) {
                CollisionTriangle t;
                t.a = transform_point(data.positions[data.indices[i]], model);
                t.b = transform_point(data.positions[data.indices[i + 1]], model);
                t.c = transform_point(data.positions[data.indices[i + 2]], model);
                const Vec3f n = cross(t.b - t.a, t.c - t.a);
                if (length(n) < 1e-8f) continue;
                t.normal = normalize(n);
                t.kind = kind;
                t.group = group;
                if (into) {
                    into->push_back(t);
                    continue;
                }
                triangles_.push_back(t);
                index_triangle(triangles_.size() - 1);
            }
        }
    }
    if (node.is_a("igGroup"))
        for (const auto& child : node.list(7)) add_node(child, model, depth + 1, kind, group, into);
}

namespace {
// Vertical line through (x, z): intersection height with a triangle.
bool vertical_hit(const CollisionTriangle& t, float x, float z, float& y) {
    const float d = (t.b.x - t.a.x) * (t.c.z - t.a.z) - (t.c.x - t.a.x) * (t.b.z - t.a.z);
    if (std::abs(d) < 1e-8f) return false;
    const float u = ((x - t.a.x) * (t.c.z - t.a.z) - (t.c.x - t.a.x) * (z - t.a.z)) / d;
    const float v = ((t.b.x - t.a.x) * (z - t.a.z) - (x - t.a.x) * (t.b.z - t.a.z)) / d;
    if (u < -1e-4f || v < -1e-4f || u + v > 1.0001f) return false;
    y = t.a.y + u * (t.b.y - t.a.y) + v * (t.c.y - t.a.y);
    return true;
}

Vec3f closest_point(const Vec3f& p, const CollisionTriangle& t) {
    const Vec3f ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return t.a;
    const Vec3f bp = p - t.b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return t.b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return t.a + ab * (d1 / (d1 - d3));
    const Vec3f cp = p - t.c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return t.c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return t.a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1.0f / (va + vb + vc);
    return t.a + ab * (vb * denom) + ac * (vc * denom);
}
} // namespace

bool CollisionWorld::floor_height(const Vec3f& p, float up, float down, float& height, Vec3f* normal,
                                  std::uint32_t mask) const {
    bool found = false;
    float best = -1e30f;
    std::set<std::uint32_t> seen;
    auto test_list = [&](const std::vector<std::uint32_t>& list) {
        for (auto index : list) {
            if (!seen.insert(index).second) continue;
            const auto& t = triangles_[index];
            // CHECKLAND (80300444) queries kind bit 1 only.
            if (!selects(t.kind, mask & 1u) || t.normal.y < 0.25f || !enabled(t)) continue;
            float y;
            if (!vertical_hit(t, p.x, p.z, y)) continue;
            if (y > p.y + up || y < p.y - down) continue;
            if (y > best) {
                best = y;
                found = true;
                if (normal) *normal = t.normal;
            }
        }
    };
    if (auto it = grid_.find(key(int(std::floor(p.x / cell_)), int(std::floor(p.z / cell_)))); it != grid_.end()) test_list(it->second);
    if (auto it = grid_.find(key(INT32_MAX, INT32_MAX)); it != grid_.end()) test_list(it->second);
    std::vector<CollisionTriangle> moving;
    dynamic_near(p, 0.0f, moving);
    for (const auto& t : moving) {
        if (!selects(t.kind, mask & 1u) || t.normal.y < 0.25f) continue;
        float y;
        if (!vertical_hit(t, p.x, p.z, y)) continue;
        if (y > p.y + up || y < p.y - down) continue;
        if (y > best) {
            best = y;
            found = true;
            if (normal) *normal = t.normal;
        }
    }
    if (found) height = best;
    return found;
}

Vec3f CollisionWorld::resolve_walls(const Vec3f& position, float radius, float step, float height, std::uint32_t mask) const {
    Vec3f p = position;
    std::vector<std::int64_t> keys;
    cells({p.x - radius, 0, p.z - radius}, {p.x + radius, 0, p.z + radius}, keys);
    keys.push_back(key(INT32_MAX, INT32_MAX));
    std::set<std::uint32_t> seen;
    std::vector<CollisionTriangle> moving;
    dynamic_near(p, radius, moving);
    for (int iteration = 0; iteration < 3; ++iteration) {
        std::vector<const CollisionTriangle*> candidates;
        for (auto k : keys) {
            auto it = grid_.find(k);
            if (it == grid_.end()) continue;
            for (auto index : it->second) candidates.push_back(&triangles_[index]);
        }
        for (const auto& t : moving) candidates.push_back(&t);
        for (const auto* candidate : candidates) {
            {
                const auto& t = *candidate;
                // CHECKWALL (802FE7F0) excludes the floor bit: wall geometry only.
                if (!selects(t.kind, mask & ~1u) || std::abs(t.normal.y) > 0.7f || !enabled(t)) continue;
                for (float h : {step + (height - step) * 0.25f, step + (height - step) * 0.75f}) {
                    const Vec3f center{p.x, p.y + h, p.z};
                    // One-sided like the original segment test (801238E8,
                    // side 0): only a probe in front of the face is pushed.
                    if (dot(t.normal, center - t.a) < 0.0f) continue;
                    const Vec3f q = closest_point(center, t);
                    // Walls below or above the probe (e.g. a ledge being jumped
                    // over) must not push: test the 3D distance, push horizontally.
                    if (std::abs(q.y - center.y) >= radius) continue;
                    // Wall parts below the step height can be stepped onto.
                    if (q.y < p.y + step) continue;
                    Vec3f d{center.x - q.x, 0, center.z - q.z};
                    const float dist = length(d);
                    if (dist < radius && dist > 1e-5f) {
                        d = d * (1.0f / dist);
                        p.x += d.x * (radius - dist);
                        p.z += d.z * (radius - dist);
                    } else if (dist <= 1e-5f && std::abs(q.y - center.y) < 1e-3f) {
                        Vec3f n{t.normal.x, 0, t.normal.z};
                        if (length(n) > 1e-5f) {
                            n = normalize(n);
                            p.x += n.x * radius;
                            p.z += n.z * radius;
                        }
                    }
                }
            }
        }
    }
    return p;
}

} // namespace dw4::engine
