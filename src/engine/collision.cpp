#include "dw4/engine/collision.hpp"
#include "dw4/engine/gekko_math.hpp"

#include "dw4/engine/scene_graph.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dw4::engine {

void CollisionWorld::clear() {
    dynamic_.clear();
    still_.clear();
    forget_answers();
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
    forget_answers();
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
            still_stale_ = true;
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
    still_stale_ = true;
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

void CollisionWorld::add_graph(const ObjectView& root, std::uint32_t kind) {
    add_node(root, Mat4{}, 0, kind, 0);
    forget_answers();
}

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
    // 0x80300CF0..0x80300D20: the broad phase reads the bound of the copy
    // transform's child 0 (+1C list, slot 0, its igNode bound +C).
    if (copy_transform)
        if (const auto children = copy_transform.list(7); !children.empty() && children.front())
            if (const auto bound = children.front().ref(3); bound && bound.is_a("igAABox")) {
                d.bounded = true;
                d.bound_min = bound.vec3(2).value_or(Vec3f{});
                d.bound_max = bound.vec3(3).value_or(Vec3f{});
            }
    dynamic_.push_back(std::move(d));
    forget_answers();
    return std::uint32_t(dynamic_.size() - 1);
}

void CollisionWorld::set_dynamic(std::uint32_t id, const Node* source, const Node* mask_switch, std::uint32_t bit) {
    if (id >= dynamic_.size()) return;
    dynamic_[id].node = source;
    dynamic_[id].mask_switch = mask_switch;
    dynamic_[id].bit = bit;
    still_stale_ = true;
}

void CollisionWorld::forget_answers() const {
    memos_.clear();
    memo_ = nullptr;
    still_stale_ = true;
}

void CollisionWorld::refresh_still() const {
    still_.clear();
    // The collision state: every group switch, then each enabled moving
    // child with its matrix.
    auto& state = still_state_;
    state.assign((groups_.size() + 31) / 32 + 1, 0u);
    state[0] = std::uint32_t(groups_.size());
    for (std::size_t g = 0; g < groups_.size(); ++g)
        if (groups_[g]) state[1 + g / 32] |= 1u << (g % 32);
    for (std::size_t i = 0; i < dynamic_.size(); ++i) {
        const auto& d = dynamic_[i];
        if (d.local.empty() || !d.node) continue;
        if (d.grouped ? !(d.group < groups_.size() && groups_[d.group]) : (!d.mask_switch || !d.mask_switch->child_enabled(d.bit)))
            continue;
        const Mat4& matrix = d.node->matrix;
        still_.push_back({std::uint32_t(i), transform_point(Vec3f{}, matrix), matrix(3, 0), matrix(3, 2)});
        state.push_back(std::uint32_t(i));
        const auto first = state.size();
        state.resize(first + 16);
        std::memcpy(&state[first], matrix.m.data(), sizeof matrix.m);
    }
    still_stale_ = false;
    // The kept answers of this state. A few states are kept (plans switch
    // walls off and on around their maps); answers are kept only from the
    // second scope in a state on, so a state that never comes back (a
    // platform moving between two plans) costs no bookkeeping. Not while
    // DW4_TRACE_HITLAND=2 prints the queries' hits.
    static const bool keep = [] {
        const char* trace = std::getenv("DW4_TRACE_HITLAND");
        return !(trace && trace[0] == '2');
    }();
    memo_ = nullptr;
    if (!keep) return;
    StillMemo* memo = nullptr;
    for (const auto& kept : memos_)
        if (kept->state == state) {
            memo = kept.get();
            break;
        }
    if (!memo) {
        if (memos_.size() >= 8)
            memos_.erase(std::min_element(memos_.begin(), memos_.end(), [](const auto& a, const auto& b) { return a->used < b->used; }));
        memos_.push_back(std::make_unique<StillMemo>());
        memo = memos_.back().get();
        memo->state = state;
    }
    memo->used = ++memo_clock_;
    if (++memo->scopes < 2) return;
    // (Bounded: a long exploration of one floor keeps adding places.)
    if (memo->land.size() + memo->wall.size() > 400'000) {
        memo->land.clear();
        memo->wall.clear();
    }
    memo_ = memo;
}

CollisionWorld::StillMemo* CollisionWorld::still_memo() const {
    if (still_depth_ <= 0) return nullptr;
    if (still_stale_) refresh_still();
    return memo_;
}

namespace {
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// The fused vector products and the face normal of the ray test (8012CB74,
// 801238E8): gekko::cross / dot / triangle_normal.
Vec3f fused_cross(const Vec3f& a, const Vec3f& b) { return gekko::cross(a, b); }
float fused_dot(const Vec3f& a, const Vec3f& b) { return gekko::dot(a, b); }
Vec3f triangle_normal(const Vec3f& a, const Vec3f& b, const Vec3f& c) { return gekko::triangle_normal(a, b, c); }
} // namespace

LandResult CollisionWorld::check_land(const Vec3f& p, float width, float up, float down) const {
    StillMemo* memo = still_memo();
    if (!memo) return land_query(p, width, up, down);
    const std::array<std::uint32_t, 6> key{bits(p.x), bits(p.y), bits(p.z), bits(width), bits(up), bits(down)};
    if (const auto* answer = memo->land.find(key)) return *answer;
    return memo->land.insert(key, land_query(p, width, up, down));
}

WallResult CollisionWorld::check_wall(const Vec3f& position, const Vec3f& direction, float width, float down,
                                      std::uint32_t flags, bool skip_preflight) const {
    StillMemo* memo = still_memo();
    if (!memo) return wall_query(position, direction, width, down, flags, skip_preflight);
    const std::array<std::uint32_t, 10> key{bits(position.x),  bits(position.y),  bits(position.z), bits(direction.x),
                                            bits(direction.y), bits(direction.z), bits(width),      bits(down),
                                            flags,             skip_preflight ? 1u : 0u};
    if (const auto* answer = memo->wall.find(key)) return *answer;
    return memo->wall.insert(key, wall_query(position, direction, width, down, flags, skip_preflight));
}

void CollisionWorld::dynamic_near(const Vec3f& p, float reach, const Vec3f& minimum, const Vec3f& maximum,
                                  std::vector<const Dynamic*>& out) const {
    auto place = [&](const Dynamic& d) {
        // The child's transform (the copy transform with its source's
        // matrix, or the check-switch transform) is pushed onto the HitLand
        // traversal's identity (8021B044 + 80128774): the stack is deeper
        // than one, so 801F6050 tests its triangles in their own vertex
        // space with the inverse (8012A92C at 0x801F62BC).
        const Mat4& matrix = d.node->matrix;
        if (!d.space_valid || std::memcmp(d.space_matrix.m.data(), matrix.m.data(), sizeof matrix.m) != 0) {
            d.top = Mat4{};
            gekko::premultiply(d.top, matrix);
            d.inverse = Mat4{};
            gekko::invert(d.top, d.inverse); // the status is not checked
            d.space_matrix = matrix;
            d.space_valid = true;
        }
        out.push_back(&d);
    };
    if (still_depth_ > 0) {
        // The same tests as below on the children enabled at the scope's
        // start, with their placement then.
        if (still_stale_) refresh_still();
        for (const auto& s : still_) {
            const auto& d = dynamic_[s.index];
            if (d.grouped) {
                const float dx = p.x - s.origin.x, dz = p.z - s.origin.z;
                const float limit = d.reach + reach;
                if (dx * dx + dz * dz > limit * limit) continue;
            } else if (d.bounded) {
                const float low_x = d.bound_min.x + s.tx, high_x = d.bound_max.x + s.tx;
                const float low_z = d.bound_min.z + s.tz, high_z = d.bound_max.z + s.tz;
                if (!(high_x >= minimum.x) || !(low_x <= maximum.x) || !(high_z >= minimum.z) || !(low_z <= maximum.z)) continue;
            }
            place(d);
        }
        return;
    }
    for (const auto& d : dynamic_) {
        if (d.local.empty() || !d.node) continue;
        // 803009D0 copies a kind 0x400 child's source matrix into the child
        // (80128CF4 at 0x80300CE0) in every candidate pass, before its bounds
        // test: a model moved earlier in this frame (a ridden car or boat) is
        // met where it is now. Reading the node here is that copy.
        const Mat4& matrix = d.node->matrix;
        if (d.grouped) {
            if (!(d.group < groups_.size() && groups_[d.group])) continue;
            const Vec3f origin = transform_point(Vec3f{}, matrix);
            const float dx = p.x - origin.x, dz = p.z - origin.z;
            const float limit = d.reach + reach;
            if (dx * dx + dz * dz > limit * limit) continue;
        } else {
            // 0x80300CA0..0x80300CBC: the child's bit of the check-switch mask
            // word, read in every pass; nothing else (no model state) gates
            // the child. The mask is the ACTION switch's (one shared bit mask
            // object), so a model that NDSP / REMOVE hides loses its
            // collision at that moment, and one an action shows has it.
            if (!d.mask_switch || !d.mask_switch->child_enabled(d.bit)) continue;
            // 0x80300CEC 80128DAC: the copied translation; 0x80300D28..
            // 0x80300D7C add it to the child-0 bound with six single adds (no
            // rotation, no scale); 80300974 keeps the child when the moved
            // box meets the query box in X and Z (inclusive, ordered compares:
            // a NaN rejects). A turned model (the stag0203 car after a curve,
            // a wall placed at 90 degrees) can stick out of that box, and the
            // original then skips it for queries there.
            if (d.bounded) {
                const float tx = matrix(3, 0), tz = matrix(3, 2);
                const float low_x = d.bound_min.x + tx, high_x = d.bound_max.x + tx;
                const float low_z = d.bound_min.z + tz, high_z = d.bound_max.z + tz;
                if (!(high_x >= minimum.x) || !(low_x <= maximum.x) || !(high_z >= minimum.z) || !(low_z <= maximum.z)) continue;
            }
        }
        place(d);
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
                const Vec3f n = fused_cross(t.b - t.a, t.c - t.a);
                if (length(n) < 1e-8f) continue;
                t.normal = triangle_normal(t.a, t.b, t.c);
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
// Returns the distance, or a negative rejection. Its dot products fuse (y
// product rounded, then x and z fused onto it: 0x80123A48 / 0x80123AF4 /
// 0x80123C18..), the hit point is origin + t * direction fused
// (0x80123B3C..0x80123B74) and the edge products are 8012CB74's fused cross
// products, so a hit lands on the original's bits.
float triangle_hit(const HitSegment& segment, const CollisionTriangle& t, Vec3f& position) {
    const Vec3f& n = t.normal;
    const float facing = fused_dot(n, segment.direction);
    constexpr float epsilon = 5.0000001e-7f; // 0x350637BD
    if (!std::signbit(facing)) return -2.0f;
    if (facing >= -epsilon) return -2.0f;
    const float distance = -fused_dot(n, segment.origin - t.a) / facing;
    if (std::signbit(distance) || distance > segment.length) return -3.0f;
    position = {std::fma(distance, segment.direction.x, segment.origin.x), std::fma(distance, segment.direction.y, segment.origin.y),
                std::fma(distance, segment.direction.z, segment.origin.z)};
    const Vec3f pa = t.a - position, pb = t.b - position, pc = t.c - position;
    const float x = fused_dot(fused_cross(pb, pc), n), y = fused_dot(fused_cross(pa, pb), n), z = fused_dot(fused_cross(pc, pa), n);
    return std::signbit(x) == std::signbit(y) && std::signbit(y) == std::signbit(z) ? distance : -4.0f;
}

// 801F63BC..801F643C: the candidate's distance from the segment origin to
// the hit, origin - hit per axis, y * y rounded, x * x and z * z fused onto
// it, the square root (frsqrte and Newton steps) when positive.
float hit_distance(const Vec3f& origin, const Vec3f& hit) {
    const float dy = gekko::subs(origin.y, hit.y), dx = gekko::subs(origin.x, hit.x), dz = gekko::subs(origin.z, hit.z);
    const float squared = gekko::madds(dz, dz, gekko::madds(dx, dx, gekko::muls(dy, dy)));
    return squared > 0.0f ? gekko::sqrt_newton(squared) : squared;
}

void extend(Vec3f& minimum, Vec3f& maximum, const Vec3f& p) {
    minimum.x = std::min(minimum.x, p.x);
    minimum.z = std::min(minimum.z, p.z);
    maximum.x = std::max(maximum.x, p.x);
    maximum.z = std::max(maximum.z, p.z);
}
} // namespace

// 801251D0: direction end - start, length from ((x*x + y*y) + z*z) plain
// (the square root by frsqrte and Newton steps when positive), the direction
// times 1 / length when the length exceeds 5e-7.
HitSegment HitSegment::between(const Vec3f& start, const Vec3f& end) {
    HitSegment s;
    s.origin = start;
    s.direction = end - start;
    const float squared = dot(s.direction, s.direction);
    s.length = squared > 0.0f ? gekko::sqrt_newton(squared) : squared;
    if (s.length > 5.0000001e-7f) s.direction = s.direction * (1.0f / s.length);
    return s;
}

void CollisionWorld::query(std::span<const HitSegment> segments, const Vec3f& minimum, const Vec3f& maximum,
                           std::uint32_t flags, std::vector<HitCandidate>& out) const {
    // Scratch lists kept between queries (no allocation per query).
    thread_local std::vector<std::int64_t> keys;
    thread_local std::vector<const std::vector<std::uint32_t>*> lists;
    thread_local std::vector<std::uint32_t> merged;
    thread_local std::vector<const Dynamic*> moving;
    keys.clear();
    cells({minimum.x, 0, minimum.z}, {maximum.x, 0, maximum.z}, keys);
    keys.push_back(key(INT32_MAX, INT32_MAX));
    lists.clear();
    for (auto k : keys) {
        const auto it = grid_.find(k);
        if (it != grid_.end() && !it->second.empty()) lists.push_back(&it->second);
    }
    // Each triangle once, in index order (the infos' traversal order). A
    // grid list holds its triangles once each in index order (they are
    // indexed as they are added), so a single list is used as it is and two
    // are merged.
    static const std::vector<std::uint32_t> no_triangles;
    const std::vector<std::uint32_t>* indices = &no_triangles;
    if (lists.size() == 1) {
        indices = lists[0];
    } else if (lists.size() == 2) {
        merged.resize(lists[0]->size() + lists[1]->size());
        std::merge(lists[0]->begin(), lists[0]->end(), lists[1]->begin(), lists[1]->end(), merged.begin());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        indices = &merged;
    } else if (lists.size() > 2) {
        merged.clear();
        for (const auto* list : lists) merged.insert(merged.end(), list->begin(), list->end());
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        indices = &merged;
    }
    moving.clear();
    const Vec3f center{(minimum.x + maximum.x) * 0.5f, 0, (minimum.z + maximum.z) * 0.5f};
    const float reach = std::max(maximum.x - minimum.x, maximum.z - minimum.z) * 0.5f + 1.0f;
    dynamic_near(center, reach, minimum, maximum, moving);
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
    for (auto index : *indices) {
        const auto& t = triangles_[index];
        if (enabled(t)) test(t);
    }
    // 801F6050 for a stack deeper than one: each segment goes into the
    // child's vertex space (801252D0 with the inverse), the local hit comes
    // back through the top matrix (8012D498), its distance is measured from
    // the original segment's origin (801F63BC) and the append turns the
    // local normal with the top matrix (80300F30: 8012D434, not normalized).
    thread_local std::vector<HitSegment> local;
    for (const auto* d : moving) {
        local.resize(segments.size());
        for (std::size_t i = 0; i < segments.size(); ++i) {
            local[i] = segments[i];
            gekko::transform_segment(local[i].origin, local[i].direction, local[i].length, d->inverse);
        }
        for (const auto& t : d->local) {
            if (!selects(t.kind, flags)) continue;
            for (std::size_t i = 0; i < segments.size(); ++i) {
                Vec3f position;
                if (triangle_hit(local[i], t, position) < 0.0f) continue;
                HitCandidate c;
                c.position = gekko::transform_point(position, d->top);
                c.normal = gekko::rotate(t.normal, d->top);
                c.distance = hit_distance(segments[i].origin, c.position);
                c.info = t.info;
                c.child = t.child;
                out.push_back(c);
            }
        }
    }
}

// 80300444: four rays from Y 1000 down to -100 at the corners of the width;
// the highest floor inside the window, else the nearest one below, else the
// lowest.
LandResult CollisionWorld::land_query(const Vec3f& p, float width, float up, float down) const {
    const float right = width + p.x, far = width + p.z, left = p.x - width, near = p.z - width;
    constexpr float top = 1000.0f, bottom = -100.0f;
    const std::array<HitSegment, 4> segments{
        HitSegment::between({right, top, far}, {right, bottom, far}), HitSegment::between({left, top, far}, {left, bottom, far}),
        HitSegment::between({right, top, near}, {right, bottom, near}), HitSegment::between({left, top, near}, {left, bottom, near})};
    thread_local std::vector<HitCandidate> hits;
    hits.clear();
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
WallResult CollisionWorld::wall_query(const Vec3f& position, const Vec3f& direction, float width, float down,
                                      std::uint32_t flags, bool skip_preflight) const {
    WallResult result;
    result.position = position;
    flags &= ~1u;
    const float radius = 1.42f * width;
    // 0x802FE974..0x802FEAEC: the direction through the HitLandModel's
    // Euler (0, pi/2, 0) matrix (+0x30, made by 80129134 at 0x802FE2B0 from
    // 0 and 1.5707964: its cosines are -4.371139e-8, not 0) with 8012D434's
    // fused products, then times 1 / length (frsqrte and Newton steps).
    static const Mat4 quarter = [] {
        Mat4 m;
        gekko::euler_matrix(m, 0.0f, 1.5707964f, 0.0f);
        return m;
    }();
    Vec3f side = gekko::rotate(direction, quarter);
    {
        const float squared = dot(side, side);
        // A zero length (a vertical direction) makes the side NaN: no
        // parallel or lateral segment can hit, and the one along the
        // direction has no length either.
        if (!(squared > 0.0f)) return result;
        const float inverse = 1.0f / gekko::sqrt_newton(squared);
        side = {side.x * inverse, side.y * inverse, side.z * inverse};
    }
    const Vec3f base{position.x, down + position.y, position.z};
    const Vec3f plus = base + direction * radius, minus = base + direction * -radius;
    auto bounds = [&](std::span<const HitSegment> segments, Vec3f& lo, Vec3f& hi) {
        lo = hi = segments.front().origin;
        for (const auto& s : segments) {
            extend(lo, hi, s.origin);
            extend(lo, hi, s.origin + s.direction * s.length);
        }
        lo.y = hi.y = position.y;
    };
    auto parallel = [&](float displacement) {
        return std::array<HitSegment, 3>{HitSegment::between(minus, plus),
                                         HitSegment::between(minus + side * displacement, plus + side * displacement),
                                         HitSegment::between(minus + side * -displacement, plus + side * -displacement)};
    };
    thread_local std::vector<HitCandidate> hits;
    hits.clear();
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
            // 0x802FF170..0x802FF1A0: -(2H - distance) times the direction,
            // fused onto the position (fmadds).
            const float scale = -((radius + radius) - hits[selected].distance);
            result.position = {std::fma(scale, direction.x, result.position.x), std::fma(scale, direction.y, result.position.y),
                               std::fma(scale, direction.z, result.position.z)};
            result.hits.push_back(hits[selected]);
            result.hit = true;
        }
    }
    // Lateral correction: two segments sideways from a point ahead.
    {
        hits.clear();
        const Vec3f raised{result.position.x, down + result.position.y, result.position.z};
        const Vec3f center = raised + direction * (3.0f * radius * 0.25f);
        // 0x802FF23C..0x802FF24C / 0x802FF2B4..0x802FF2C0: the ends are
        // +-H times the side fused onto the centre.
        const auto end = [&](float h) {
            return Vec3f{std::fma(h, side.x, center.x), std::fma(h, side.y, center.y), std::fma(h, side.z, center.z)};
        };
        const std::array<HitSegment, 2> segments{HitSegment::between(center, end(radius)), HitSegment::between(center, end(-radius))};
        bounds(segments, lo, hi);
        query(segments, lo, hi, flags, hits);
        if (!hits.empty()) {
            std::size_t selected = 0;
            for (std::size_t i = 1; i < hits.size(); ++i)
                if (hits[i].distance < hits[selected].distance) selected = i;
            const Vec3f& normal = hits[selected].normal;
            // 0x802FF410..0x802FF658: cos(acos(n.n / (|n| |n|))), the ratio
            // clamped to [-1, 1] like 8012947C's asin argument, acos and cos
            // the MSL ones (800A2C34 / 800A257C), each rounded to single.
            const float squared = dot(normal, normal);
            const float length = squared > 0.0f ? gekko::sqrt_newton(squared) : squared;
            const float x = squared / (length * length);
            const float above = x > -1.0f ? x : -1.0f;
            const float clamped = above >= 1.0f ? 1.0f : (x > -1.0f ? x : -1.0f);
            const float ratio = float(gekko::cos(float(gekko::acos(clamped))));
            const float scale = (radius - hits[selected].distance) * ratio;
            // 0x802FF680..0x802FF690: fused onto the position (fmadds).
            result.position = {std::fma(scale, normal.x, result.position.x), std::fma(scale, normal.y, result.position.y),
                               std::fma(scale, normal.z, result.position.z)};
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
    // 0x802FF88C..0x802FF8A0: the turn, made by 80129134 from (0, 0.7853982, 0).
    static const Mat4 turn = [] {
        Mat4 m;
        gekko::euler_matrix(m, 0.0f, 0.78539819f, 0.0f);
        return m;
    }();
    auto pass = [&](float radius, std::uint32_t query_flags) {
        const Vec3f base{position.x, down + position.y, position.z};
        std::array<HitSegment, 8> segments;
        Vec3f lo = base, hi = base;
        Vec3f direction{1.0f, 0.0f, 0.0f};
        for (int i = 0; i < 8; ++i) {
            // 0x802FF908..0x802FF914: the end is radius times the direction
            // fused onto the base.
            const Vec3f end{gekko::madds(radius, direction.x, base.x), gekko::madds(radius, direction.y, base.y),
                            gekko::madds(radius, direction.z, base.z)};
            segments[std::size_t(i)] = HitSegment::between(base, end);
            extend(lo, hi, end);
            if (i == 7) break;
            // 0x802FF9A0..0x802FFAC4: turned by 8012D434, times 1 / length.
            direction = gekko::rotate(direction, turn);
            float length = dot(direction, direction);
            if (length > 0.0f) length = gekko::sqrt_newton(length);
            const float inverse = 1.0f / length;
            direction = {direction.x * inverse, direction.y * inverse, direction.z * inverse};
        }
        lo.y = hi.y = position.y;
        thread_local std::vector<HitCandidate> hits;
        hits.clear();
        query(segments, lo, hi, query_flags, hits);
        if (hits.empty()) return false;
        std::size_t selected = 0;
        for (std::size_t i = 1; i < hits.size(); ++i)
            if (hits[i].distance < hits[selected].distance) selected = i;
        const Vec3f& normal = hits[selected].normal;
        // 0x802FFB64..0x802FFDEC: as the lateral wall pass, cos(acos(...))
        // of the clamped ratio, the push fused onto the position.
        const float squared = dot(normal, normal);
        const float length = squared > 0.0f ? gekko::sqrt_newton(squared) : squared;
        const float x = squared / (length * length);
        const float above = x > -1.0f ? x : -1.0f;
        const float clamped = above >= 1.0f ? 1.0f : (x > -1.0f ? x : -1.0f);
        const float ratio = float(gekko::cos(float(gekko::acos(clamped))));
        const float scale = (radius - hits[selected].distance) * ratio;
        result.position = {gekko::madds(scale, normal.x, result.position.x), gekko::madds(scale, normal.y, result.position.y),
                           gekko::madds(scale, normal.z, result.position.z)};
        result.hits.push_back(hits[selected]);
        result.hit = true;
        return true;
    };
    if (!pass(3.5f * width, flags | 0x1000u)) pass(2.15f * width, flags);
    return result;
}

} // namespace dw4::engine
