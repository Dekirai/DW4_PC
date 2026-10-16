#include "dw4/engine/collision.hpp"

#include "dw4/engine/scene_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace dw4::engine {

void CollisionWorld::clear() {
    dynamic_.clear();
    triangles_.clear();
    grid_.clear();
    groups_.assign(1, true);
}

std::uint16_t CollisionWorld::add_info(std::string name) {
    info_names_.push_back(std::move(name));
    return std::uint16_t(info_names_.size() - 1);
}

const std::string& CollisionWorld::info_name(std::uint16_t info) const {
    static const std::string none;
    return info < info_names_.size() ? info_names_[info] : none;
}

std::vector<std::uint32_t> CollisionWorld::add_switch(const ObjectView& check_switch, std::uint32_t kind, std::uint16_t info,
                                                    const std::function<const Node*(const ObjectView&)>& resolve) {
    std::vector<std::uint32_t> ids;
    if (!check_switch) return ids;
    const auto children = check_switch.is_a("igGroup") ? check_switch.list(7) : std::vector<ObjectView>{check_switch};
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto group = std::uint32_t(groups_.size());
        groups_.push_back(true);
        ids.push_back(group);
        if (const Node* node = resolve && children[i] && children[i].is_a("igTransform") ? resolve(children[i]) : nullptr) {
            Dynamic d;
            for (const auto& child : children[i].list(7)) add_node(child, Mat4{}, 1, kind, group, &d.local);
            for (auto& t : d.local) {
                t.info = info;
                t.child = std::uint16_t(i);
                for (const auto& v : {t.a, t.b, t.c}) d.reach = std::max(d.reach, length(v));
            }
            d.node = node;
            d.grouped = true;
            d.group = group;
            dynamic_.push_back(std::move(d));
            continue;
        }
        const auto first = triangles_.size();
        add_node(children[i], Mat4{}, 1, kind, group);
        for (std::size_t t = first; t < triangles_.size(); ++t) {
            triangles_[t].info = info;
            triangles_[t].child = std::uint16_t(i);
        }
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

std::uint32_t CollisionWorld::add_dynamic(const ObjectView& copy_transform, std::uint32_t kind, std::uint16_t info,
                                          std::uint16_t child) {
    Dynamic d;
    // The copy transform's own matrix is replaced by its source; its
    // children are read in its space.
    if (copy_transform)
        for (const auto& node : copy_transform.list(7)) add_node(node, Mat4{}, 1, kind, 0, &d.local);
    for (auto& t : d.local) {
        t.info = info;
        t.child = child;
    }
    for (const auto& t : d.local)
        for (const auto& v : {t.a, t.b, t.c}) d.reach = std::max(d.reach, length(v));
    dynamic_.push_back(std::move(d));
    return std::uint32_t(dynamic_.size() - 1);
}

void CollisionWorld::set_dynamic(std::uint32_t id, const Node* source, bool enabled) {
    if (id >= dynamic_.size()) return;
    dynamic_[id].node = source;
    dynamic_[id].enabled = enabled;
}

void CollisionWorld::dynamic_near(const Vec3f& p, float reach, std::vector<CollisionTriangle>& out) const {
    for (const auto& d : dynamic_) {
        if (d.local.empty() || !d.node) continue;
        if (d.grouped ? !(d.group < groups_.size() && groups_[d.group]) : !d.enabled) continue;
        // 803009D0 copies a kind 0x400 child's source matrix into the child
        // (80128CF4 at 0x80300CE0) in every candidate pass, before its bounds
        // test: a model moved earlier in this frame (a ridden car or boat) is
        // met where it is now. Reading the node here is that copy.
        const Mat4& matrix = d.node->matrix;
        const Vec3f origin = transform_point(Vec3f{}, matrix);
        const float dx = p.x - origin.x, dz = p.z - origin.z;
        const float limit = d.reach + reach;
        if (dx * dx + dz * dz > limit * limit) continue;
        for (const auto& local : d.local) {
            CollisionTriangle t = local;
            t.a = transform_point(local.a, matrix);
            t.b = transform_point(local.b, matrix);
            t.c = transform_point(local.c, matrix);
            const Vec3f n = cross(t.b - t.a, t.c - t.a);
            if (length(n) < 1e-8f) continue;
            t.normal = normalize(n);
            t.group = UINT32_MAX; // checked above
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
// 801238E8 with side 0: the unit normal of (b - a) x (c - a) must face the
// segment (dot < 0, beyond the 805669E4 epsilon); the plane hit must lie on
// the segment and inside the triangle (the three edge products share a sign).
// Returns the distance, or a negative rejection.
float triangle_hit(const HitSegment& segment, const CollisionTriangle& t, Vec3f& position) {
    const Vec3f& n = t.normal;
    const float facing = dot(n, segment.direction);
    constexpr float epsilon = 5.0000001e-7f; // 0x350637BD
    if (!std::signbit(facing)) return -2.0f;
    if (facing >= -epsilon) return -2.0f;
    const float distance = -dot(n, segment.origin - t.a) / facing;
    if (std::signbit(distance) || distance > segment.length) return -3.0f;
    position = segment.origin + segment.direction * distance;
    const Vec3f pa = t.a - position, pb = t.b - position, pc = t.c - position;
    const float x = dot(cross(pb, pc), n), y = dot(cross(pa, pb), n), z = dot(cross(pc, pa), n);
    return std::signbit(x) == std::signbit(y) && std::signbit(y) == std::signbit(z) ? distance : -4.0f;
}

float hit_distance(const Vec3f& a, const Vec3f& b) {
    const Vec3f d = a - b;
    return std::sqrt(dot(d, d));
}

void extend(Vec3f& minimum, Vec3f& maximum, const Vec3f& p) {
    minimum.x = std::min(minimum.x, p.x);
    minimum.z = std::min(minimum.z, p.z);
    maximum.x = std::max(maximum.x, p.x);
    maximum.z = std::max(maximum.z, p.z);
}
} // namespace

HitSegment HitSegment::between(const Vec3f& start, const Vec3f& end) {
    HitSegment s;
    s.origin = start;
    s.direction = end - start;
    s.length = std::sqrt(dot(s.direction, s.direction));
    if (s.length > 5.0000001e-7f) s.direction = s.direction * (1.0f / s.length);
    return s;
}

void CollisionWorld::query(const std::vector<HitSegment>& segments, const Vec3f& minimum, const Vec3f& maximum,
                           std::uint32_t flags, std::vector<HitCandidate>& out) const {
    std::vector<std::int64_t> keys;
    cells({minimum.x, 0, minimum.z}, {maximum.x, 0, maximum.z}, keys);
    keys.push_back(key(INT32_MAX, INT32_MAX));
    std::vector<std::uint32_t> indices;
    std::set<std::uint32_t> seen;
    for (auto k : keys) {
        const auto it = grid_.find(k);
        if (it == grid_.end()) continue;
        for (auto index : it->second)
            if (seen.insert(index).second) indices.push_back(index);
    }
    std::sort(indices.begin(), indices.end()); // the infos' traversal order
    std::vector<CollisionTriangle> moving;
    const Vec3f center{(minimum.x + maximum.x) * 0.5f, 0, (minimum.z + maximum.z) * 0.5f};
    const float reach = std::max(maximum.x - minimum.x, maximum.z - minimum.z) * 0.5f + 1.0f;
    dynamic_near(center, reach, moving);
    auto test = [&](const CollisionTriangle& t) {
        if (!selects(t.kind, flags)) return;
        for (const auto& segment : segments) {
            Vec3f position;
            const float distance = triangle_hit(segment, t, position);
            if (distance < 0.0f) continue;
            HitCandidate c;
            c.position = position;
            c.normal = t.normal;
            c.distance = hit_distance(segment.origin, position);
            c.info = t.info;
            c.child = t.child;
            if (static const bool trace = [] {
                    const char* v = std::getenv("DW4_TRACE_HITLAND");
                    return v && v[0] == '2';
                }();
                trace && !(flags & 1u))
                std::fprintf(stderr, "  wallhit kind %u info %u child %u n=(%.2f,%.2f,%.2f) d=%.2f at (%.1f,%.1f,%.1f)\n", t.kind,
                             unsigned(t.info), unsigned(t.child), t.normal.x, t.normal.y, t.normal.z, c.distance, position.x,
                             position.y, position.z);
            out.push_back(c);
        }
    };
    for (auto index : indices) {
        const auto& t = triangles_[index];
        if (enabled(t)) test(t);
    }
    for (const auto& t : moving) test(t);
}

// 80300444: four rays from Y 1000 down to -100 at the corners of the width;
// the highest floor inside the window, else the nearest one below, else the
// lowest.
LandResult CollisionWorld::check_land(const Vec3f& p, float width, float up, float down) const {
    const float right = width + p.x, far = width + p.z, left = p.x - width, near = p.z - width;
    constexpr float top = 1000.0f, bottom = -100.0f;
    const std::vector<HitSegment> segments{
        HitSegment::between({right, top, far}, {right, bottom, far}), HitSegment::between({left, top, far}, {left, bottom, far}),
        HitSegment::between({right, top, near}, {right, bottom, near}), HitSegment::between({left, top, near}, {left, bottom, near})};
    std::vector<HitCandidate> hits;
    query(segments, {left, bottom, near}, {right, top, far}, 1u, hits);
    auto inside = [&](float height) { return up + height > p.y && height - down < p.y; };
    LandResult result;
    result.position = {p.x, -1.0f, p.z};
    int selected = -1;
    float best = 0.0f;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const float height = hits[i].position.y;
        if (!inside(height)) continue;
        const float delta = height - p.y;
        if (selected < 0 || delta > best) {
            selected = int(i);
            best = delta;
        }
    }
    if (selected < 0)
        for (std::size_t i = 0; i < hits.size(); ++i) {
            const float height = hits[i].position.y;
            if (height >= p.y) continue;
            const float delta = p.y - height;
            if (selected < 0 || delta < best) {
                selected = int(i);
                best = delta;
            }
        }
    if (selected < 0)
        for (std::size_t i = 0; i < hits.size(); ++i)
            if (i == 0 || hits[i].position.y <= hits[std::size_t(selected)].position.y) selected = int(i);
    if (selected < 0) return result;
    const auto& hit = hits[std::size_t(selected)];
    result.found = true;
    result.position.y = hit.position.y;
    result.in_range = inside(hit.position.y);
    result.adjusted = p.y - hit.position.y <= 0.0f;
    result.info = hit.info;
    result.child = hit.child;
    return result;
}

// 802FE7F0: H = 1.42 width around the position raised by `down`, the side
// axis the direction turned a quarter about Y. A wide preflight (unless the
// previous query hit) finds any wall; the longitudinal pass pulls the
// position back along the direction from the nearest hit, the lateral pass
// pushes it out along the nearest wall's normal.
WallResult CollisionWorld::check_wall(const Vec3f& position, const Vec3f& direction, float width, float down,
                                      std::uint32_t flags, bool skip_preflight) const {
    WallResult result;
    result.position = position;
    flags &= ~1u;
    const float radius = 1.42f * width;
    // Euler (0, pi/2, 0) applied to the direction (8012D434), then normalized.
    Vec3f side{direction.z, direction.y, -direction.x};
    {
        const float length = std::sqrt(dot(side, side));
        if (length <= 0.0f) return result;
        side = side * (1.0f / length);
    }
    const Vec3f base{position.x, down + position.y, position.z};
    const Vec3f plus = base + direction * radius, minus = base + direction * -radius;
    auto bounds = [&](const std::vector<HitSegment>& segments, Vec3f& lo, Vec3f& hi) {
        lo = hi = segments.front().origin;
        for (const auto& s : segments) {
            extend(lo, hi, s.origin);
            extend(lo, hi, s.origin + s.direction * s.length);
        }
        lo.y = hi.y = position.y;
    };
    auto parallel = [&](float displacement) {
        return std::vector<HitSegment>{HitSegment::between(minus, plus),
                                       HitSegment::between(minus + side * displacement, plus + side * displacement),
                                       HitSegment::between(minus + side * -displacement, plus + side * -displacement)};
    };
    std::vector<HitCandidate> hits;
    Vec3f lo, hi;
    if (!skip_preflight) {
        const auto segments = parallel(radius);
        bounds(segments, lo, hi);
        query(segments, lo, hi, flags, hits);
        if (hits.empty()) return result;
    }
    // Longitudinal correction: back off along the direction by 2H - distance.
    {
        hits.clear();
        const auto segments = parallel(radius * 0.5f);
        bounds(segments, lo, hi);
        query(segments, lo, hi, flags, hits);
        if (!hits.empty()) {
            std::size_t selected = 0;
            for (std::size_t i = 1; i < hits.size(); ++i)
                if (hits[i].distance < hits[selected].distance) selected = i;
            const float scale = -((radius + radius) - hits[selected].distance);
            result.position = result.position + direction * scale;
            result.hits.push_back(hits[selected]);
            result.hit = true;
        }
    }
    // Lateral correction: two segments sideways from a point ahead.
    {
        hits.clear();
        const Vec3f raised{result.position.x, down + result.position.y, result.position.z};
        const Vec3f center = raised + direction * (3.0f * radius * 0.25f);
        const std::vector<HitSegment> segments{HitSegment::between(center, center + side * radius),
                                               HitSegment::between(center, center + side * -radius)};
        bounds(segments, lo, hi);
        query(segments, lo, hi, flags, hits);
        if (!hits.empty()) {
            std::size_t selected = 0;
            for (std::size_t i = 1; i < hits.size(); ++i)
                if (hits[i].distance < hits[selected].distance) selected = i;
            const Vec3f& normal = hits[selected].normal;
            // cos(acos(|n|^2 / (|n| |n|))) of the unit normal: one.
            const float squared = dot(normal, normal);
            const float ratio = std::min(1.0f, squared / (std::sqrt(squared) * std::sqrt(squared)));
            const float scale = (radius - hits[selected].distance) * ratio;
            result.position = result.position + normal * scale;
            result.hits.push_back(hits[selected]);
            result.hit = true;
        }
    }
    return result;
}

// 802FF748: eight rays 45 degrees apart (the first along +X, each next one
// turned by Euler (0, pi/4, 0)) from the position raised by `down`. A pass of
// 3.5 width against kind 0x1000 geometry (the walls of a boat or a moving
// floor) and, without a hit there, a pass of 2.15 width against every wall;
// the nearest hit pushes the position out along its normal.
WallResult CollisionWorld::check_wall_full(const Vec3f& position, float width, float down, std::uint32_t flags) const {
    WallResult result;
    result.position = position;
    flags &= ~1u;
    auto pass = [&](float radius, std::uint32_t query_flags) {
        const Vec3f base{position.x, down + position.y, position.z};
        std::vector<HitSegment> segments;
        Vec3f lo = base, hi = base;
        Vec3f direction{1.0f, 0.0f, 0.0f};
        constexpr float turn = 0.70710677f;
        for (int i = 0; i < 8; ++i) {
            const Vec3f end = base + direction * radius;
            segments.push_back(HitSegment::between(base, end));
            extend(lo, hi, end);
            if (i == 7) break;
            direction = normalize(Vec3f{direction.x * turn + direction.z * turn, direction.y,
                                        direction.z * turn - direction.x * turn});
        }
        lo.y = hi.y = position.y;
        std::vector<HitCandidate> hits;
        query(segments, lo, hi, query_flags, hits);
        if (hits.empty()) return false;
        std::size_t selected = 0;
        for (std::size_t i = 1; i < hits.size(); ++i)
            if (hits[i].distance < hits[selected].distance) selected = i;
        const Vec3f& normal = hits[selected].normal;
        // cos(acos(|n|^2 / (|n| |n|))), as in the lateral wall pass: one.
        const float squared = dot(normal, normal);
        const float ratio = std::min(1.0f, squared / (std::sqrt(squared) * std::sqrt(squared)));
        result.position = result.position + normal * ((radius - hits[selected].distance) * ratio);
        result.hits.push_back(hits[selected]);
        result.hit = true;
        return true;
    };
    if (!pass(3.5f * width, flags | 0x1000u)) pass(2.15f * width, flags);
    return result;
}

} // namespace dw4::engine
