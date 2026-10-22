#include "test_path.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <queue>
#include <set>

namespace dw4::game {
namespace {

// A walking grid over an area of the stage collision.
class Grid {
public:
    static constexpr float step = 5.0f;   // grid spacing
    static constexpr float climb = 9.0f;  // largest height step up between cells
    static constexpr float drop = 60.0f;  // largest ledge walked off
    static constexpr float radius = 2.5f; // clearance kept from walls

    Grid(const engine::CollisionWorld& world, float x0, float z0, int w, int h, float reference, std::uint32_t walls)
        : world_(world), x0_(x0), z0_(z0), w_(w), h_(h), reference_(reference), walls_(walls),
          floor_(std::size_t(w) * std::size_t(h), std::numeric_limits<float>::quiet_NaN()) {}

    int w() const { return w_; }
    int h() const { return h_; }
    int index(int x, int z) const { return z * w_ + x; }
    std::pair<int, int> cell_at(float x, float z) const {
        const int cx = int(std::lround((x - x0_) / step)), cz = int(std::lround((z - z0_) / step));
        return {std::clamp(cx, 0, w_ - 1), std::clamp(cz, 0, h_ - 1)};
    }
    engine::Vec3f point(int cx, int cz) { return {x0_ + float(cx) * step, floor_of(cx, cz), z0_ + float(cz) * step}; }
    float floor_of(int cx, int cz) {
        auto& slot = floor_[std::size_t(index(cx, cz))];
        if (!std::isnan(slot)) return slot;
        const engine::Vec3f p{x0_ + float(cx) * step, reference_ + 20.0f, z0_ + float(cz) * step};
        const auto land = world_.check_land(p, 1.0f, 400.0f, 1.0f);
        slot = land.found ? land.position.y : -100000.0f;
        return slot;
    }
    bool passable(int ax, int az, int bx, int bz) {
        const float ya = floor_of(ax, az), yb = floor_of(bx, bz);
        if (ya < -99999.0f || yb < -99999.0f || yb - ya > climb || ya - yb > drop) return false;
        const engine::Vec3f a = point(ax, az), b = point(bx, bz);
        engine::Vec3f direction{b.x - a.x, 0.0f, b.z - a.z};
        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        direction = {direction.x / length, 0.0f, direction.z / length};
        const engine::Vec3f middle{(a.x + b.x) * 0.5f, std::max(ya, yb), (a.z + b.z) * 0.5f};
        return !world_.check_wall(middle, direction, radius, 4.0f, walls_, false).hit;
    }

private:
    const engine::CollisionWorld& world_;
    float x0_, z0_;
    int w_, h_;
    float reference_;
    std::uint32_t walls_;
    std::vector<float> floor_;
};

} // namespace

std::vector<engine::Vec3f> find_walk_path(const engine::CollisionWorld& world, const engine::Vec3f& from, const engine::Vec3f& to,
                                          std::uint32_t wall_flags) {
    constexpr float margin = 160.0f; // searched area around start and goal
    const float step = Grid::step;
    const float x0 = std::min(from.x, to.x) - margin, z0 = std::min(from.z, to.z) - margin;
    const int w = std::min(400, int((std::abs(from.x - to.x) + 2 * margin) / step) + 1);
    const int h = std::min(400, int((std::abs(from.z - to.z) + 2 * margin) / step) + 1);
    Grid grid(world, x0, z0, w, h, std::max(from.y, to.y), wall_flags);
    const engine::CollisionWorld::Still still(world); // nothing moves while the route is searched
    if (std::getenv("DW4_TRACE_GRID")) {
        // Probe the column straight south of the start.
        const auto [cx, cz] = grid.cell_at(from.x, from.z);
        for (int z = cz; z > std::max(0, cz - 30); --z)
            std::printf("grid (%.0f,%.0f) floor %.1f -> next %d\n", grid.point(cx, z).x, grid.point(cx, z).z, grid.floor_of(cx, z),
                        grid.passable(cx, z, cx, z - 1) ? 1 : 0);
    }
    const auto [sx, sz] = grid.cell_at(from.x, from.z);
    const auto [gx, gz] = grid.cell_at(to.x, to.z);
    std::vector<float> cost(std::size_t(w) * std::size_t(h), std::numeric_limits<float>::infinity());
    std::vector<int> parent(std::size_t(w) * std::size_t(h), -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    auto heuristic = [&](int x, int z) { return std::hypot(float(x - gx), float(z - gz)); };
    cost[std::size_t(grid.index(sx, sz))] = 0.0f;
    open.push({heuristic(sx, sz), grid.index(sx, sz)});
    int reached = -1, best = grid.index(sx, sz);
    float best_distance = heuristic(sx, sz);
    int expanded = 0;
    while (!open.empty() && expanded < 60000) {
        const auto [f, at] = open.top();
        open.pop();
        const int x = at % w, z = at / w;
        const float g = cost[std::size_t(at)];
        if (f > g + heuristic(x, z) + 1e-3f) continue;
        ++expanded;
        if (heuristic(x, z) < best_distance) {
            best_distance = heuristic(x, z);
            best = at;
        }
        if (x == gx && z == gz) {
            reached = at;
            break;
        }
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz) continue;
                const int nx = x + dx, nz = z + dz;
                if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                const float next = g + ((dx && dz) ? 1.4142f : 1.0f);
                if (next >= cost[std::size_t(grid.index(nx, nz))]) continue;
                if (!grid.passable(x, z, nx, nz)) continue;
                cost[std::size_t(grid.index(nx, nz))] = next;
                parent[std::size_t(grid.index(nx, nz))] = at;
                open.push({next + heuristic(nx, nz), grid.index(nx, nz)});
            }
    }
    // Without a full route, walk to the closest reached point.
    const int end = reached >= 0 ? reached : best;
    if (end == grid.index(sx, sz)) return {};
    std::vector<engine::Vec3f> points;
    for (int at = end; at >= 0; at = parent[std::size_t(at)]) points.push_back(grid.point(at % w, at / w));
    std::reverse(points.begin(), points.end());
    // Thin out: every fourth point and the end.
    std::vector<engine::Vec3f> thinned;
    for (std::size_t i = 4; i < points.size(); i += 4) thinned.push_back(points[i]);
    if (reached >= 0) thinned.push_back(to);
    else if (!points.empty()) thinned.push_back(points.back());
    return thinned;
}

WalkMap::WalkMap(const engine::CollisionWorld& world, const engine::Vec3f& from, float range, std::uint32_t wall_flags,
                 const std::vector<AvoidBox>& avoid, float width, const std::vector<MovingFloor>& moving) {
    constexpr float step = Grid::step, climb = Grid::climb, drop = Grid::drop;
    const engine::CollisionWorld::Still still(world); // nothing moves while the map is built
    // The grid is aligned to the world (multiples of three cells), so the
    // samples do not move with the player.
    x0_ = std::floor((from.x - range) / (3 * step)) * (3 * step);
    z0_ = std::floor((from.z - range) / (3 * step)) * (3 * step);
    w_ = int((from.x + range - x0_) / step) + 1;
    h_ = int((from.z + range - z0_) / step) + 1;
    const std::size_t cells = std::size_t(w_) * std::size_t(h_);
    floor_.assign(cells, -100000.0f);
    parent_.assign(cells, -2);
    jumped_.assign(cells, 0);
    support_.assign(cells, -1);
    // The cells a moving platform's top covers somewhere along its path.
    const std::size_t floors = std::min<std::size_t>(moving.size(), 100);
    std::vector<std::int8_t> sweep(cells, -1);
    std::vector<float> sweep_top(floors, 0.0f);
    for (std::size_t m = 0; m < floors; ++m) {
        const auto& f = moving[m];
        if (f.centres.empty()) continue;
        sweep_top[m] = f.centres.front().y;
        for (const auto& c : f.centres) {
            const int xa = std::max(0, int(std::ceil((c.x - f.half_x - x0_) / step)));
            const int xb = std::min(w_ - 1, int(std::floor((c.x + f.half_x - x0_) / step)));
            const int za = std::max(0, int(std::ceil((c.z - f.half_z - z0_) / step)));
            const int zb = std::min(h_ - 1, int(std::floor((c.z + f.half_z - z0_) / step)));
            for (int z = za; z <= zb; ++z)
                for (int x = xa; x <= xb; ++x)
                    if (sweep[std::size_t(z * w_ + x)] < 0) sweep[std::size_t(z * w_ + x)] = std::int8_t(m);
        }
    }
    auto blocked = [&](float x, float y, float z) {
        for (const auto& box : avoid)
            if (x >= box.min_x && x <= box.max_x && z >= box.min_z && z <= box.max_z && y >= box.min_y && y <= box.max_y) return true;
        return false;
    };
    // A cell's floor is looked up from the floor it is entered from, so
    // heights follow ramps and stairs. A moving platform's floor is the
    // platform where it stands now, and its top over a pit anywhere along its
    // path.
    struct Probe {
        float y;
        int support;
    };
    auto probe = [&](int cell, float reference) {
        const engine::Vec3f p{x0_ + float(cell % w_) * step, reference + climb, z0_ + float(cell / w_) * step};
        const auto land = world.check_land(p, 1.0f, 400.0f, 1.0f);
        Probe r{land.found ? land.position.y : -100000.0f, -1};
        if (land.found)
            for (std::size_t m = 0; m < floors; ++m)
                if (!moving[m].centres.empty() && land.info == moving[m].info && land.child == moving[m].child) {
                    // (A platform not moving now has no path: plain floor.)
                    r.support = int(m);
                    return r;
                }
        const int s = sweep[std::size_t(cell)];
        if (s >= 0 && r.y < sweep_top[std::size_t(s)] - climb && sweep_top[std::size_t(s)] <= reference + climb)
            r = {sweep_top[std::size_t(s)], s};
        return r;
    };
    auto floor_from = [&](int cell, float reference) { return probe(cell, reference).y; };
    auto centre = [&](int x, int z) { return engine::Vec3f{x0_ + float(x) * step, 0.0f, z0_ + float(z) * step}; };
    // A wall across the way from one cell centre to the next at height y.
    auto wall_between = [&](int ax, int az, int bx, int bz, float y) {
        const auto a = centre(ax, az), b = centre(bx, bz);
        const float length = std::hypot(b.x - a.x, b.z - a.z);
        const engine::Vec3f direction{(b.x - a.x) / length, 0.0f, (b.z - a.z) / length};
        const engine::Vec3f middle{(a.x + b.x) * 0.5f, y, (a.z + b.z) * 0.5f};
        const auto wall = world.check_wall(middle, direction, width, 4.0f, wall_flags, false);
        if (wall.hit)
            for (const auto& hit : wall.hits) {
                auto& blocker = blockers_[{hit.info, hit.child}];
                if (!blocker.count++) {
                    blocker.first = hit.position;
                    blocker.normal = hit.normal;
                }
            }
        return wall.hit;
    };
    // Pass 1: flood the reachable cells, recording which of the eight steps
    // out of each cell are open and which gaps are jumped.
    static constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, kDz[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    std::vector<std::uint8_t> open_steps(cells, 0);
    struct Jump {
        int from, to;
        float cost;
    };
    std::vector<Jump> jumps;
    const int start = nearest_cell(from);
    const auto first = probe(start, from.y);
    floor_[std::size_t(start)] = first.y;
    support_[std::size_t(start)] = std::int8_t(first.support);
    if (floor_[std::size_t(start)] < -99999.0f) floor_[std::size_t(start)] = from.y;
    std::vector<std::uint8_t> reached(cells, 0);
    reached[std::size_t(start)] = 1;
    std::vector<int> queue{start};
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int at = queue[head];
        const int x = at % w_, z = at / w_;
        const float ya = floor_[std::size_t(at)];
        const int sa = support_[std::size_t(at)];
        std::uint8_t pits_left = 0; // directions of pits no straight jump crosses
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d], nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= w_ || nz >= h_) continue;
            const int next_cell = nz * w_ + nx;
            const auto b = centre(nx, nz);
            const Probe pb = reached[std::size_t(next_cell)] ? Probe{floor_[std::size_t(next_cell)], support_[std::size_t(next_cell)]}
                                                             : probe(next_cell, ya);
            const float yb = pb.y;
            if (yb < -99999.0f) continue; // outside the level
            if (ya - yb <= drop && blocked(b.x, yb, b.z)) continue;
            // A pit (deeper than a ledge drop): try to jump it.
            if (ya - yb > drop) {
                // The jump (X) carries about 28 units over 40 frames and rises
                // 14: gaps of up to four cells (20 units, diagonals three).
                const int longest = (d & 1) ? 3 : 4;
                pits_left |= std::uint8_t(1u << d);
                for (int k = 2; k <= longest; ++k) {
                    const int lx = x + kDx[d] * k, lz = z + kDz[d] * k;
                    if (lx < 0 || lz < 0 || lx >= w_ || lz >= h_) break;
                    const int land = lz * w_ + lx;
                    const auto l = centre(lx, lz);
                    const auto pl = probe(land, ya + 5.0f);
                    const float yl = pl.y;
                    if (yl < -99999.0f) break;
                    if (ya - yl > drop) continue; // still over the pit
                    if (blocked(l.x, yl, l.z)) break;
                    if (sa >= 0 && pl.support == sa) break; // the platform carries the player there
                    if (yl - ya > 12.0f || ya - yl > 30.0f) break;
                    bool clear = true;
                    for (int i = 0; i < k && clear; ++i)
                        clear = !wall_between(x + kDx[d] * i, z + kDz[d] * i, x + kDx[d] * (i + 1), z + kDz[d] * (i + 1), ya);
                    if (!clear) break;
                    jumps.push_back({at, land, float(k) * ((d & 1) ? 1.4142f : 1.0f) + 8.0f});
                    pits_left &= std::uint8_t(~(1u << d));
                    if (!reached[std::size_t(land)]) {
                        reached[std::size_t(land)] = 1;
                        floor_[std::size_t(land)] = yl;
                        support_[std::size_t(land)] = std::int8_t(pl.support);
                        queue.push_back(land);
                    }
                    break;
                }
                continue;
            }
            // Steps up to `climb`; ledges are walked off (one way) up to `drop`.
            if (yb - ya > climb || wall_between(x, z, nx, nz, std::max(ya, yb))) {
                // A step face of up to 12 units (stairs of 10-unit blocks;
                // the player's wall probe sits 4 above its feet) is jumped
                // up: the jump rises about 13.6.
                if (d & 1) continue;
                const auto pu = probe(next_cell, ya + 4.0f);
                const float yu = pu.y;
                if (yu < -99999.0f || yu - ya <= climb || yu - ya > 12.5f || blocked(b.x, yu, b.z) ||
                    (sa >= 0 && pu.support == sa) || wall_between(x, z, nx, nz, yu + 1.0f))
                    continue;
                jumps.push_back({at, next_cell, 7.0f});
                if (!reached[std::size_t(next_cell)]) {
                    reached[std::size_t(next_cell)] = 1;
                    floor_[std::size_t(next_cell)] = yu;
                    support_[std::size_t(next_cell)] = std::int8_t(pu.support);
                    queue.push_back(next_cell);
                }
                continue;
            }
            open_steps[std::size_t(at)] |= std::uint8_t(1u << d);
            if (!reached[std::size_t(next_cell)]) {
                reached[std::size_t(next_cell)] = 1;
                floor_[std::size_t(next_cell)] = yb;
                support_[std::size_t(next_cell)] = std::int8_t(pb.support);
                queue.push_back(next_cell);
            }
        }
        // Long jumps: with a run-up of two cells the jump carries about 28
        // units from the very edge, so wider pits are crossed at any angle
        // (15-degree steps, toward the pits found above) onto floor at most a
        // little higher (cell centres up to 32.5 apart: both edges reach
        // past them). Not from or onto a moving platform (its hops need no
        // run-up across the stage floor).
        if (pits_left && sa < 0) {
            const auto a = centre(x, z);
            auto cell_at = [&](float px, float pz) {
                const int cx = int(std::lround((px - x0_) / step)), cz = int(std::lround((pz - z0_) / step));
                return cx < 0 || cz < 0 || cx >= w_ || cz >= h_ ? -1 : cz * w_ + cx;
            };
            for (int angle = 0; angle < 24; ++angle) {
                const int d = ((angle + 1) / 3) % 8; // nearest of the eight directions
                if (!(pits_left & (1u << d))) continue;
                const float radians = float(angle) * 0.2617994f;
                const float ux = std::cos(radians), uz = std::sin(radians);
                bool runup = true;
                for (const float back : {5.0f, 10.0f}) {
                    const int behind = cell_at(a.x - ux * back, a.z - uz * back);
                    const float yr = behind < 0 ? -100000.0f : floor_from(behind, ya);
                    runup = runup && std::abs(yr - ya) <= 3.0f;
                }
                if (!runup) continue;
                for (float distance = 22.5f; distance <= 32.5f; distance += 2.5f) {
                    const int land = cell_at(a.x + ux * distance, a.z + uz * distance);
                    if (land < 0) break;
                    const auto l = centre(land % w_, land / w_);
                    const auto pl = probe(land, ya + 5.0f);
                    const float yl = pl.y;
                    if (yl < -99999.0f || pl.support >= 0 || yl - ya > 2.0f || ya - yl > 30.0f || blocked(l.x, yl, l.z)) continue;
                    // Floor beyond the landing and to both sides of it (a
                    // cell centre at the very rim may lie just off the floor).
                    bool inside = true;
                    for (const auto [ox, oz] : {std::pair{ux, uz}, std::pair{-uz, ux}, std::pair{uz, -ux}}) {
                        const int around = cell_at(l.x + ox * step, l.z + oz * step);
                        inside = inside && around >= 0 && std::abs(floor_from(around, yl) - yl) <= 3.0f;
                    }
                    if (!inside) continue;
                    bool clear = true;
                    for (float t = 0.0f; t < distance && clear; t += step) {
                        const engine::Vec3f middle{a.x + ux * (t + step * 0.5f), ya + 6.0f, a.z + uz * (t + step * 0.5f)};
                        clear = !world.check_wall(middle, {ux, 0.0f, uz}, width, 4.0f, wall_flags, false).hit;
                    }
                    if (!clear) break;
                    jumps.push_back({at, land, distance / step + 8.0f});
                    if (!reached[std::size_t(land)]) {
                        reached[std::size_t(land)] = 1;
                        floor_[std::size_t(land)] = yl;
                        queue.push_back(land);
                    }
                    break;
                }
            }
        }
    }
    // Pass 2: steps from the nearest cell beside a wall (walls are not slid
    // along, so routes keep to the middle of corridors).
    std::vector<std::uint8_t> wall_distance(cells, 255);
    std::vector<int> edge;
    for (const int at : queue)
        if (open_steps[std::size_t(at)] != 0xFF) {
            wall_distance[std::size_t(at)] = 0;
            edge.push_back(at);
        }
    for (std::size_t head = 0; head < edge.size(); ++head) {
        const int at = edge[head];
        if (wall_distance[std::size_t(at)] >= 2) continue;
        for (int d = 0; d < 8; d += 2) {
            if (!(open_steps[std::size_t(at)] & (1u << d))) continue;
            const int next_cell = (at / w_ + kDz[d]) * w_ + at % w_ + kDx[d];
            if (wall_distance[std::size_t(next_cell)] <= wall_distance[std::size_t(at)] + 1) continue;
            wall_distance[std::size_t(next_cell)] = std::uint8_t(wall_distance[std::size_t(at)] + 1);
            edge.push_back(next_cell);
        }
    }
    // Pass 3: cheapest routes from the start; cells beside walls and jumps
    // cost extra.
    std::multimap<int, const Jump*> jumps_from;
    for (const auto& jump : jumps) jumps_from.emplace(jump.from, &jump);
    cost_.assign(cells, std::numeric_limits<float>::infinity());
    cost_[std::size_t(start)] = 0.0f;
    parent_[std::size_t(start)] = -1;
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    open.push({0.0f, start});
    auto relax = [&](int at, int next_cell, float next, bool jump) {
        // A hop onto or off a moving platform waits for it.
        if (support_[std::size_t(next_cell)] != support_[std::size_t(at)]) next += 40.0f;
        if (next >= cost_[std::size_t(next_cell)]) return;
        cost_[std::size_t(next_cell)] = next;
        parent_[std::size_t(next_cell)] = at;
        jumped_[std::size_t(next_cell)] = jump ? 1 : 0;
        open.push({next, next_cell});
    };
    // Cells in an enemy fire lane (FireLanes) at their floor's height cost
    // as much as a dozen steps.
    const auto* lanes = FireLanes::active();
    auto fire = [&](int cell) {
        if (!lanes || lanes->empty()) return 0.0f;
        const auto p = point(cell);
        const auto it = lanes->find({int(std::lround(p.x / step)), int(std::lround(p.z / step))});
        return it != lanes->end() && p.y >= it->second.first && p.y <= it->second.second ? 12.0f : 0.0f;
    };
    while (!open.empty()) {
        const auto [g, at] = open.top();
        open.pop();
        if (g > cost_[std::size_t(at)]) continue;
        for (int d = 0; d < 8; ++d) {
            if (!(open_steps[std::size_t(at)] & (1u << d))) continue;
            const int next_cell = (at / w_ + kDz[d]) * w_ + at % w_ + kDx[d];
            const auto near_wall = wall_distance[std::size_t(next_cell)];
            const float penalty = near_wall == 0 ? 4.0f : near_wall == 1 ? 1.5f : 0.0f;
            relax(at, next_cell, g + ((d & 1) ? 1.4142f : 1.0f) + penalty + fire(next_cell), false);
        }
        for (auto [it, end] = jumps_from.equal_range(at); it != end; ++it)
            relax(at, it->second->to, g + it->second->cost + fire(it->second->to), true);
    }
}

int WalkMap::nearest_cell(const engine::Vec3f& p) const {
    const int cx = std::clamp(int(std::lround((p.x - x0_) / Grid::step)), 0, w_ - 1);
    const int cz = std::clamp(int(std::lround((p.z - z0_) / Grid::step)), 0, h_ - 1);
    return cz * w_ + cx;
}

engine::Vec3f WalkMap::point(int cell) const {
    return {x0_ + float(cell % w_) * Grid::step, floor_[std::size_t(cell)], z0_ + float(cell / w_) * Grid::step};
}

int WalkMap::nearest_reached(const engine::Vec3f& p, float tolerance) const {
    const int radius = int(std::ceil(tolerance / Grid::step));
    const int centre = nearest_cell(p);
    const int cx = centre % w_, cz = centre / w_;
    int best = -1;
    float best_distance = tolerance;
    // (The stage floor first: a target is not walked to on a moving
    // platform's path while floor next to it is in reach.)
    for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (int z = std::max(0, cz - radius); z <= std::min(h_ - 1, cz + radius); ++z)
            for (int x = std::max(0, cx - radius); x <= std::min(w_ - 1, cx + radius); ++x) {
                if (parent_[std::size_t(z * w_ + x)] == -2 || (pass == 0 && support_[std::size_t(z * w_ + x)] >= 0)) continue;
                const auto q = point(z * w_ + x);
                const float distance = std::hypot(q.x - p.x, q.z - p.z);
                if (distance <= best_distance) {
                    best_distance = distance;
                    best = z * w_ + x;
                }
            }
    return best;
}

std::optional<engine::Vec3f> WalkMap::nearest(const engine::Vec3f& p, float tolerance, int* support) const {
    const int cell = nearest_reached(p, tolerance);
    if (cell < 0) return std::nullopt;
    if (support) *support = support_[std::size_t(cell)];
    return point(cell);
}

std::optional<engine::Vec3f> WalkMap::reached_inside(float min_x, float min_z, float max_x, float max_z, bool stage_floor_only) const {
    const engine::Vec3f middle{(min_x + max_x) * 0.5f, 0.0f, (min_z + max_z) * 0.5f};
    std::optional<engine::Vec3f> best;
    float best_distance = std::numeric_limits<float>::infinity();
    const int x0 = std::max(0, int(std::ceil((min_x - x0_) / Grid::step))), x1 = std::min(w_ - 1, int(std::floor((max_x - x0_) / Grid::step)));
    const int z0 = std::max(0, int(std::ceil((min_z - z0_) / Grid::step))), z1 = std::min(h_ - 1, int(std::floor((max_z - z0_) / Grid::step)));
    // (Stage floor first: a mat over a pit is stood on at its rim rather
    // than on a platform crossing it.)
    for (int pass = 0; pass < (stage_floor_only ? 1 : 2) && !best; ++pass)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                if (parent_[std::size_t(z * w_ + x)] == -2 || (pass == 0 && support_[std::size_t(z * w_ + x)] >= 0)) continue;
                const auto q = point(z * w_ + x);
                const float distance = std::hypot(q.x - middle.x, q.z - middle.z);
                if (distance < best_distance) {
                    best_distance = distance;
                    best = q;
                }
            }
    return best;
}

bool WalkMap::stage_floor_near(const engine::Vec3f& p, float tolerance) const {
    const int cell = nearest_reached(p, tolerance);
    return cell >= 0 && support_[std::size_t(cell)] < 0;
}

float WalkMap::cost_to(const engine::Vec3f& p, float tolerance) const {
    const int cell = nearest_reached(p, tolerance);
    return cell < 0 ? std::numeric_limits<float>::infinity() : cost_[std::size_t(cell)];
}

std::vector<WalkMap::Waypoint> WalkMap::route_to(const engine::Vec3f& p) const {
    int end = -1;
    float best = std::numeric_limits<float>::infinity();
    for (int cell = 0; cell < w_ * h_; ++cell) {
        if (parent_[std::size_t(cell)] == -2) continue;
        const auto q = point(cell);
        // (A moving platform's path counts 10 units further.)
        const float distance = std::hypot(q.x - p.x, q.z - p.z) + (support_[std::size_t(cell)] >= 0 ? 10.0f : 0.0f);
        if (distance < best) {
            best = distance;
            end = cell;
        }
    }
    std::vector<int> cells;
    for (int at = end; at >= 0; at = parent_[std::size_t(at)]) cells.push_back(at);
    std::reverse(cells.begin(), cells.end());
    // Every third cell, the take-off and landing cells of jumps and hops,
    // and the end.
    std::vector<Waypoint> thinned;
    auto support_of = [&](std::size_t i) { return int(support_[std::size_t(cells[i])]); };
    for (std::size_t i = 1; i < cells.size(); ++i) {
        const bool takeoff = i + 1 < cells.size() && (jumped_[std::size_t(cells[i + 1])] || support_of(i + 1) != support_of(i));
        const bool landing = jumped_[std::size_t(cells[i])] != 0;
        const bool hop = support_of(i) != support_of(i - 1);
        if (i % 3 == 0 || takeoff || landing || hop || i + 1 == cells.size()) thinned.push_back({point(cells[i]), landing, support_of(i)});
    }
    return thinned;
}

std::vector<WalkMap::Waypoint> WalkMap::route_to(const engine::Vec3f& p, float tolerance) const {
    const int radius = int(std::ceil(tolerance / Grid::step));
    const int centre = nearest_cell(p);
    const int cx = centre % w_, cz = centre / w_;
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (int z = std::max(0, cz - radius); z <= std::min(h_ - 1, cz + radius); ++z)
            for (int x = std::max(0, cx - radius); x <= std::min(w_ - 1, cx + radius); ++x) {
                const int cell = z * w_ + x;
                if (parent_[std::size_t(cell)] == -2 || (pass == 0 && support_[std::size_t(cell)] >= 0)) continue;
                const auto q = point(cell);
                if (std::hypot(q.x - p.x, q.z - p.z) > tolerance) continue;
                if (best < 0 || cost_[std::size_t(cell)] < cost_[std::size_t(best)]) best = cell;
            }
    return best < 0 ? route_to(p) : route_to(point(best));
}

std::vector<engine::Vec3f> WalkMap::samples() const {
    std::vector<engine::Vec3f> out;
    for (int cell = 0; cell < w_ * h_; ++cell)
        if (parent_[std::size_t(cell)] != -2 && support_[std::size_t(cell)] < 0 && (cell % w_) % 3 == 0 && (cell / w_) % 3 == 0)
            out.push_back(point(cell));
    return out;
}

namespace {
float flat(const engine::Vec3f& a, const engine::Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }
std::pair<int, int> coarse(float x, float z) { return {int(std::floor(x / 20.0f)), int(std::floor(z / 20.0f))}; }
std::string mat_key(std::string_view name) { return std::string(name.rfind("mat", 0) == 0 ? name.substr(3) : name); }
engine::Vec3f position_of(const engine::ModelCtrlManager::ModelRam& ram) {
    return {ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)};
}
engine::Vec3f mat_centre(const engine::MatCtrlManager::MatBox& box, float y) {
    return {(box.min.x + box.max.x) * 0.5f, y, (box.min.z + box.max.z) * 0.5f};
}
// A wall switch shows it is on (SQ_SWON, then SQ_ONLOOP until it turns off).
// (Some fall back to SQ_WAIT at once, with its label set to ON.)
bool switched_on(const engine::ModelCtrlManager::ModelRam& ram) {
    if (ram.seq_name == "SQ_SWON" || ram.seq_name == "SQ_ONLOOP") return true;
    const auto label = ram.labels.find(ram.seq_name);
    return label != ram.labels.end() && label->second == "ON";
}
// The flat distance from p to a model's body boxes (80307EAC: the root
// matrix moves the box centre, the authored half extents stay).
float body_gap(const engine::ModelCtrlManager::ModelRam& e, const engine::Vec3f& p) {
    if (!e.node) return std::numeric_limits<float>::infinity();
    const auto& m = e.node->matrix;
    if (!e.hit || e.hit->boxes.empty()) return std::hypot(p.x - m(3, 0), p.z - m(3, 2));
    float gap = std::numeric_limits<float>::infinity();
    for (const auto& [lo, hi] : e.hit->boxes) {
        const engine::Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const float wx = c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0) + m(3, 0);
        const float wz = c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2) + m(3, 2);
        const float dx = std::max(0.0f, std::abs(p.x - wx) - (hi.x - lo.x) * 0.5f);
        const float dz = std::max(0.0f, std::abs(p.z - wz) - (hi.z - lo.z) * 0.5f);
        gap = std::min(gap, std::hypot(dx, dz));
    }
    return gap;
}
// How far a model's body boxes reach out of its origin (flat, the farthest
// box corner as body_gap places the boxes); 0 without boxes.
// A talk box reaching further than kFarTalk is walked to and pressed by its
// edge (consoles: staggf03 g_covercnsz05 16.8, stagml12 g_evenconsz05 15.8,
// dngq3b2e g_d3pedstal 14.1; the levers of stag0101-0301, 10.6, and the
// wall switches stay pressed by their origin).
constexpr float kFarTalk = 12.0f;
// The ground kept clear round a structure seen healing itself (plan,
// without the test cheats): the poison plants' spit hit at up to 46.
constexpr float kHealerKeep = 45.0f;
// Pickups gone for while crossing a field (set_transit): within this.
constexpr float kTransitPick = 30.0f;
float box_reach(const engine::ModelCtrlManager::ModelRam& e) {
    if (!e.node || !e.hit || e.hit->boxes.empty()) return 0.0f;
    const auto& m = e.node->matrix;
    float reach = 0.0f;
    for (const auto& [lo, hi] : e.hit->boxes) {
        const engine::Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const float ox = c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0);
        const float oz = c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2);
        reach = std::max(reach, std::hypot(std::abs(ox) + (hi.x - lo.x) * 0.5f, std::abs(oz) + (hi.z - lo.z) * 0.5f));
    }
    return reach;
}
// The top of a model's body boxes in world Y (80307EAC: the root matrix moves
// the box centre, the authored half extents stay); the root's Y without boxes.
float body_top(const engine::ModelCtrlManager::ModelRam& e) {
    if (!e.node) return -std::numeric_limits<float>::infinity();
    const auto& m = e.node->matrix;
    if (!e.hit || e.hit->boxes.empty()) return m(3, 1);
    float top = -std::numeric_limits<float>::infinity();
    for (const auto& [lo, hi] : e.hit->boxes) {
        const engine::Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const float wy = c.x * m(0, 1) + c.y * m(1, 1) + c.z * m(2, 1) + m(3, 1);
        top = std::max(top, wy + (hi.y - lo.y) * 0.5f);
    }
    return top;
}
// The bottom of a model's body boxes in world Y (as body_top).
float body_bottom(const engine::ModelCtrlManager::ModelRam& e) {
    if (!e.node) return std::numeric_limits<float>::infinity();
    const auto& m = e.node->matrix;
    if (!e.hit || e.hit->boxes.empty()) return m(3, 1);
    float bottom = std::numeric_limits<float>::infinity();
    for (const auto& [lo, hi] : e.hit->boxes) {
        const engine::Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const float wy = c.x * m(0, 1) + c.y * m(1, 1) + c.z * m(2, 1) + m(3, 1);
        bottom = std::min(bottom, wy - (hi.y - lo.y) * 0.5f);
    }
    return bottom;
}
// The player's swing (CHKBULLET 10000/30000) spans its feet to 8 above. An
// enemy (not a structure or boss, which have their own approach) whose body
// starts above that from its own floor hovers out of every swing there
// (stag0404 e_impdra: body 13..28 above its root); A is not taken in the air
// (8034C3C8 skips the buttons in SQ_JUMP/SQ_FALL).
bool hovering(const engine::ModelCtrlManager::ModelRam& e) {
    if (e.category != "ENEMY" || e.info_name.rfind("g_", 0) == 0 || e.info_name.rfind("f_", 0) == 0 || !e.node) return false;
    return body_bottom(e) > e.node->matrix(3, 1) + 9.0f;
}
// Swings from floor y meet the body when feet..feet+8 overlaps it.
bool swing_meets(float y, const engine::ModelCtrlManager::ModelRam& e) { return y + 7.0f > body_bottom(e) && y < body_top(e) - 1.0f; }
// Hit reactions and status effects hold the player in place; that is not
// being stuck.
bool held_by_hit(const engine::ModelCtrlManager::ModelRam& ram) {
    for (const char* name : {"SQ_LDMG", "SQ_MDMG", "SQ_SDMG", "SQ_DDMG", "SQ_STUN", "SQ_SLEEP", "SQ_BIND", "SQ_DOWN"})
        if (ram.seq_name == name) return true;
    return false;
}
} // namespace

// The goal mats: "matz02", or alternatives "matz15|matz16" (exits to the
// same stage), as keys without "mat".
std::set<std::string> goal_keys(const std::string& goal) {
    std::set<std::string> keys;
    std::size_t start = 0;
    while (start <= goal.size()) {
        const auto end = std::min(goal.find('|', start), goal.size());
        if (end > start) keys.insert(mat_key(std::string_view(goal).substr(start, end - start)));
        start = end + 1;
    }
    return keys;
}

bool ExploreBot::hazard_mat(engine::Engine& engine, const std::string& mat) {
    const auto it = hazards_.find(mat);
    if (it != hazards_.end()) return it->second;
    const std::string script = mat + ".lua";
    const bool hazard = engine.script_mentions(script, "ADDHP") && engine.script_mentions(script, "REMOVE");
    hazards_[mat] = hazard;
    return hazard;
}

// A structure that heals itself while struck is left alone (no cheats).
// The area 3 poison plants g_poison01z03 (hit_g_poison01z03.lua: -50 a blow
// of 100 HP, at 0 SQ_DDMG and +100) spit poison at the player around them
// (70104, about 5 % of the HP a blow) and never fall: a fresh no-cheat chain
// took 986 such blows in stag0301/0302 at levels 33 to 37, every stage visit
// (a fresh ExploreBot) fighting them again until its 900 fruitless frames
// were over. A structure whose HP rises while it is fought is not fought
// again, also in later visits (regenerating_names). (The only other such
// hit script, the area 2 g_atcksabtnz02 devices, is the same and sets no
// flag either: neither opens a way.)
void ExploreBot::note_regenerating(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& target, int frame) {
    if (engine.cheats().one_hit || engine.cheats().god || target.info_name.rfind("g_", 0) != 0) return;
    if (regenerating_names_.insert(target.info_name).second && std::getenv("DW4_TRACE_EXPLORE"))
        std::printf("[%d] explore: %s heals itself, left alone\n", frame, target.info_name.c_str());
    unkillable_.insert(&target);
    unreachable_[&target] = frame + 6000;
    planned_ = -1000;
}

// Without the test cheats an enemy this far around a quicksand pit's guard
// box (the sink grown by 25) is not walked to: it comes to the player, who
// holds its waves away from the pit (see the hold in plan()). The sand pulls
// at least 0.26 a frame and a fight drifts with every swing, guard recoil
// and knock-back; walking out to the waves at 45 from the guard box a level
// 18 no-cheat run still fell into dng205b3's sinks 8..9 times in 90000
// frames, at 90 once or twice (5 heals instead of 9, more waves cleared).
// (At most the reached sand's own clearance less 15: a small room keeps
// enemies to fight.)
constexpr float kSandFightMargin = 90.0f;

bool perilous_floor(engine::Engine& engine, const engine::Vec3f& p) {
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (!mats) return false;
    for (const auto& box : mats->mat_boxes()) {
        if (p.x < box.min.x || p.x > box.max.x || p.z < box.min.z || p.z > box.max.z || p.y < box.min.y - 5.0f || p.y > box.max.y + 10.0f) continue;
        const std::string script = box.name + ".lua";
        if ((engine.script_mentions(script, "ADDHP") && engine.script_mentions(script, "REMOVE")) || engine.script_mentions(script, "QuickSand") ||
            engine.script_mentions(script, "QUICKSAND"))
            return true;
    }
    return false;
}

bool ExploreBot::quicksand_mat(engine::Engine& engine, const std::string& mat) {
    const auto it = quicksands_.find(mat);
    if (it != quicksands_.end()) return it->second;
    const std::string script = mat + ".lua";
    const bool sand = engine.script_mentions(script, "QuickSand") || engine.script_mentions(script, "QUICKSAND");
    quicksands_[mat] = sand;
    return sand;
}

bool ExploreBot::trap_mat(engine::Engine& engine, const std::string& mat) {
    const auto it = traps_.find(mat);
    if (it != traps_.end()) return it->second;
    const std::string script = mat + ".lua";
    const bool trap = engine.script_mentions(script, "MATisPlayer") && engine.script_mentions(script, "SETLABEL");
    traps_[mat] = trap;
    return trap;
}

const std::vector<ExploreBot::Beam>& ExploreBot::beams(engine::Engine& engine, int frame) {
    if (frame - beams_at_ < 600 && frame >= beams_at_) return beams_;
    beams_at_ = frame;
    beams_.clear();
    auto* mats = engine.get<engine::MatCtrlManager>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!mats || !models) return beams_;
    for (const auto& box : mats->mat_boxes()) {
        if (!trap_mat(engine, box.name)) continue;
        const std::string script = box.name + ".lua";
        bool armed = false;
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node || e->is_player) continue;
            const auto at = position_of(*e);
            if (at.x < box.min.x || at.x > box.max.x || at.z < box.min.z || at.z > box.max.z) continue;
            if (!engine.script_mentions(script, e->info_name)) continue;
            const auto& m = e->node->matrix;
            const float length = std::hypot(m(2, 0), m(2, 2));
            if (length < 1e-4f) continue;
            beams_.push_back({at, {m(2, 0) / length, 0.0f, m(2, 2) / length}, box.min, box.max});
            armed = true;
        }
        if (!armed) beams_.push_back({{}, {}, box.min, box.max});
    }
    return beams_;
}

bool ExploreBot::exit_mat(engine::Engine& engine, const std::string& mat) {
    const auto it = exits_.find(mat);
    if (it != exits_.end()) return it->second;
    // (funcStageChange, or a script of its own ending in STAGECTL NEXTSTAGE.)
    const std::string script = mat + ".lua";
    const bool exit = (engine.script_mentions(script, "funcStageChange") || engine.script_mentions(script, "NEXTSTAGE")) && !portal(engine, mat);
    exits_[mat] = exit;
    return exit;
}

bool ExploreBot::portal(engine::Engine& engine, const std::string& mat) {
    const auto it = portals_.find(mat);
    if (it != portals_.end()) return it->second;
    const std::string script = mat + ".lua";
    const bool link = engine.script_mentions(script, "trans.lua") && !engine.script_mentions(script, "funcStageChange");
    portals_[mat] = link;
    return link;
}

namespace {
// The player's jump (X) and walk, measured on the stage floor: a jump lands
// 39 steps after the press at the height it left; on the ground and in the
// air the speed follows the stick's direction at once and gains 0.038 a step
// from standing up to 0.7 (a standing jump carries 20.6 units, a running
// one 26.6); without the stick it stops within a few steps.
constexpr int kHopFlight = 39;
constexpr float kWalkSpeed = 0.7f;
constexpr float kWalkAccel = 0.038f;
// The distance covered in `steps` from speed `v` with the stick held.
float covered(float v, int steps) {
    float distance = 0.0f;
    for (int i = 1; i <= steps; ++i) distance += std::min(kWalkSpeed, v + kWalkAccel * float(i));
    return distance;
}
} // namespace

bool ExploreBot::Platform::sideways() const {
    if (trail.size() < 60 || moving_steps * 3 < int(trail.size())) return false;
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
    for (const auto& p : trail) {
        x0 = std::min(x0, p.x), x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
        z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
    }
    return std::max(x1 - x0, z1 - z0) >= 10.0f && y1 - y0 < 3.0f;
}

void ExploreBot::track_platforms(engine::Engine& engine, const engine::Vec3f& me, int frame) {
    auto* models = engine.get<engine::ModelCtrlManager>();
    auto* hitland = engine.get<engine::HitLandManager>();
    if (!models || !hitland) return;
    const auto& world = hitland->world();
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->is_player || e->category == "ENEMY" || e->category == "ITEM") continue;
        const auto at = position_of(*e);
        const auto known = std::find_if(platforms_.begin(), platforms_.end(), [&](const Platform& p) { return p.model == e.get(); });
        if (known != platforms_.end()) {
            auto& p = *known;
            if (p.last_frame == frame) continue;
            // (Across a gap too: against the last position seen.)
            if (!p.trail.empty() && flat(at, p.trail.back()) > 0.02f) p.moved_at = frame;
            if (p.last_frame != frame - 1) {
                p.trail.clear(); // a gap: the steps no longer follow each other
                p.moving_steps = 0;
            }
            if (!p.trail.empty() && flat(at, p.trail.back()) > 0.02f) ++p.moving_steps;
            p.trail.push_back(at);
            p.last_frame = frame;
            if (p.trail.size() > 4000) {
                p.trail.erase(p.trail.begin(), p.trail.begin() + 1000);
                p.moving_steps = 0;
                for (std::size_t i = 1; i < p.trail.size(); ++i) p.moving_steps += flat(p.trail[i], p.trail[i - 1]) > 0.02f ? 1 : 0;
            }
            continue;
        }
        if (flat(at, me) > 600.0f || not_platforms_.count(e.get())) continue;
        auto [last, fresh] = stirring_.try_emplace(e.get(), at);
        const bool moved = !fresh && flat(at, last->second) > 0.02f;
        last->second = at;
        if (!moved) continue;
        // Its own floor under its origin (the floor check answers with the
        // model's name and index, 8030949C)?
        const auto land = world.check_land({at.x, at.y + 30.0f, at.z}, 1.0f, 400.0f, 1.0f);
        if (!land.found || land.info == 0xFFFF || land.child != std::uint16_t(e->index) || world.info_name(land.info) != e->info_name) {
            not_platforms_.insert(e.get());
            continue;
        }
        Platform p;
        p.model = e.get();
        p.info = land.info;
        p.child = land.child;
        p.top = land.position.y - at.y;
        // The top's extents: where the floor check stops answering with it.
        auto own = [&](float x, float z) {
            const auto l = world.check_land({x, land.position.y + 5.0f, z}, 1.0f, 400.0f, 1.0f);
            return l.found && l.info == p.info && l.child == p.child;
        };
        static constexpr float kDirs[4][2] = {{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
        float reach[4]{};
        for (int d = 0; d < 4; ++d)
            while (reach[d] < 40.0f && own(at.x + kDirs[d][0] * (reach[d] + 0.5f), at.z + kDirs[d][1] * (reach[d] + 0.5f))) reach[d] += 0.5f;
        p.half_x = (reach[0] + reach[1]) * 0.5f;
        p.half_z = (reach[2] + reach[3]) * 0.5f;
        p.offset = {(reach[0] - reach[1]) * 0.5f, 0.0f, (reach[2] - reach[3]) * 0.5f};
        p.trail.push_back(at);
        p.last_frame = frame;
        p.moved_at = frame;
        p.moving_steps = 1;
        if (std::getenv("DW4_TRACE_EXPLORE"))
            std::printf("[%d] explore: platform %s#%d at (%.0f,%.0f,%.0f), top %.1f half %.1f x %.1f\n", frame, e->info_name.c_str(), e->index,
                        at.x, at.y, at.z, land.position.y, p.half_x, p.half_z);
        platforms_.push_back(std::move(p));
        planned_ = -1000;
    }
}

engine::Vec3f ExploreBot::predict(const Platform& p, int k, bool* known) const {
    if (known) *known = false;
    const auto& t = p.trail;
    const std::size_t n = t.size();
    if (n == 0) return {};
    const engine::Vec3f top{p.offset.x, p.top, p.offset.z};
    const auto c = t[n - 1];
    if (n < 2 || k <= 0) {
        if (known) *known = k <= 0;
        return c + top;
    }
    const engine::Vec3f v = c - t[n - 2];
    if (std::hypot(v.x, v.z) < 0.02f) return c + top; // standing (it may set off any time)
    // The latest earlier step at the same place moving the same way: the
    // path from there on is the path from now on.
    if (n >= std::size_t(k) + 3)
        for (std::size_t j = n - 2 - std::size_t(k); j >= 1; --j) {
            const auto& q = t[j];
            if (std::abs(q.x - c.x) > 0.75f || std::abs(q.z - c.z) > 0.75f || std::abs(q.y - c.y) > 0.75f) continue;
            const engine::Vec3f w = q - t[j - 1];
            if (std::abs(w.x - v.x) > 0.1f || std::abs(w.z - v.z) > 0.1f) continue;
            if (known) *known = true;
            return c + (t[j + std::size_t(k)] - q) + top;
        }
    return c + v * float(k) + top;
}

int ExploreBot::platform_under(engine::Engine& engine, const engine::Vec3f& p) const {
    auto* hitland = engine.get<engine::HitLandManager>();
    if (!hitland || platforms_.empty()) return -1;
    const auto land = hitland->world().check_land({p.x, p.y + 5.0f, p.z}, 1.0f, 400.0f, 1.0f);
    if (!land.found || land.position.y < p.y - 8.0f) return -1;
    for (std::size_t i = 0; i < platforms_.size(); ++i)
        if (platforms_[i].hop_on && platforms_[i].info == land.info && platforms_[i].child == land.child) return int(i);
    return -1;
}

std::size_t ExploreBot::pending_hop(int standing_on, const engine::Vec3f& me) const {
    std::size_t i = next_;
    if (standing_on >= 0) {
        // Riding: the waypoints up to the last on this platform's path are
        // where it carries the player anyway (a hop lands far from them).
        for (std::size_t j = next_; j < route_.size(); ++j)
            if (route_[j].support == standing_on) i = j + 1;
        return i;
    }
    // On the stage floor: a hop just ahead.
    while (i < route_.size() && route_[i].support < 0 && flat(route_[i].point, me) < 15.0f) ++i;
    return i < route_.size() && route_[i].support >= 0 ? i : route_.size();
}

bool ExploreBot::hop_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const engine::Vec3f& me, bool airborne,
                          int frame, Decision& decision) {
    auto* hitland = engine.get<engine::HitLandManager>();
    if (!hitland || (platforms_.empty() && !hop_)) return false;
    const auto& world = hitland->world();
    const bool trace = std::getenv("DW4_TRACE_EXPLORE") != nullptr;
    auto steer = [&](const engine::Vec3f& p, float reach) {
        decision.x = p.x;
        decision.z = p.z;
        decision.reach = reach;
    };
    // A support's own motion over the next k steps (a ride goes on through
    // a jump until the landing, 80309F58).
    auto drift = [&](int support, int k) {
        if (support < 0 || k <= 0) return engine::Vec3f{};
        return predict(platforms_[std::size_t(support)], k) - predict(platforms_[std::size_t(support)], 0);
    };
    // Trap lines (a laser gun firing across the pit while a player stands
    // in its mat; its beam is 5 wide, the player's body 5): kept out of
    // where possible.
    const auto& lines = beams(engine, frame);
    // The distance from p to the nearest trap line (0 inside a trap mat
    // without one); 6 or less is in the way.
    auto trap_gap = [&](const engine::Vec3f& p) {
        float gap = std::numeric_limits<float>::infinity();
        for (const auto& b : lines) {
            if (p.x < b.lo.x - 9.0f || p.x > b.hi.x + 9.0f || p.z < b.lo.z - 9.0f || p.z > b.hi.z + 9.0f || p.y < b.lo.y || p.y > b.hi.y)
                continue;
            if (b.axis.x == 0.0f && b.axis.z == 0.0f) {
                if (p.x >= b.lo.x && p.x <= b.hi.x && p.z >= b.lo.z && p.z <= b.hi.z) gap = 0.0f;
                continue;
            }
            const float dx = p.x - b.origin.x, dz = p.z - b.origin.z;
            if (dx * b.axis.x + dz * b.axis.z >= -3.0f) gap = std::min(gap, std::abs(dz * b.axis.x - dx * b.axis.z));
        }
        return gap;
    };
    auto in_trap = [&](const engine::Vec3f& p) { return trap_gap(p) <= 6.0f; };
    // The point of a platform's top (4 units in from its edges) nearest to p
    // clear of the trap lines (else nearest to p); `free` tells which. With
    // `later` (the platform's centre a little later) the point must stay
    // clear while the platform carries it there.
    auto inside = [&](int i, const engine::Vec3f& centre, const engine::Vec3f& p, bool* free = nullptr,
                      const engine::Vec3f* later = nullptr) {
        const auto& q = platforms_[std::size_t(i)];
        const float ix = std::max(0.0f, q.half_x - 4.0f), iz = std::max(0.0f, q.half_z - 4.0f);
        auto gap_of = [&](const engine::Vec3f& c) {
            return later ? std::min(trap_gap(c), trap_gap(c + (*later - centre))) : trap_gap(c);
        };
        const engine::Vec3f nearest{std::clamp(p.x, centre.x - ix, centre.x + ix), centre.y, std::clamp(p.z, centre.z - iz, centre.z + iz)};
        if (free) *free = true;
        if (gap_of(nearest) >= 9.0f) return nearest;
        // (Clear of the lines, 9 units off them where the top allows.)
        std::optional<engine::Vec3f> best;
        float best_score = std::numeric_limits<float>::infinity();
        for (float dz = -iz; dz <= iz + 0.01f; dz += 1.0f)
            for (float dx = -ix; dx <= ix + 0.01f; dx += 1.0f) {
                const engine::Vec3f c{centre.x + dx, centre.y, centre.z + dz};
                const float gap = gap_of(c);
                if (gap <= 6.0f) continue;
                const float score = flat(c, p) + std::max(0.0f, 9.0f - gap) * 2.0f;
                if (score < best_score) {
                    best_score = score;
                    best = c;
                }
            }
        if (free) *free = best.has_value();
        return best.value_or(nearest);
    };
    // Stage floor (not a platform) at about height y.
    auto stage_floor = [&](float x, float y, float z) {
        const auto l = world.check_land({x, y + 9.0f, z}, 1.0f, 400.0f, 1.0f);
        if (!l.found || std::abs(l.position.y - y) > 3.0f) return false;
        for (const auto& q : platforms_)
            if (q.info == l.info && q.child == l.child) return false;
        return true;
    };
    // Riding: stay where the platform carries the player (kept on its top)
    // and out of the trap lines; when it carries the player into one with
    // no clear part on this side, jump over it, steering onto the part clear
    // of it at touch-down.
    auto ride = [&](int p) {
        const auto& q = platforms_[std::size_t(p)];
        // (At the middle, room for a run-up either way, unless the way there
        // crosses a trap line: then where it is.)
        const auto centre = predict(q, 0);
        auto rest = inside(p, centre, centre);
        for (int k = 1; k <= 4; ++k)
            if (in_trap(me + (rest - me) * (float(k) / 4.0f))) {
                rest = inside(p, centre, me);
                break;
            }
        if (player.control_enabled && !airborne && !held_by_hit(player)) {
            const auto carried = me + drift(p, 8);
            bool free_soon = false;
            const auto soon = inside(p, predict(q, 8), carried, &free_soon);
            bool crossing = !free_soon;
            if (!crossing && flat(soon, carried) > 3.0f)
                for (int k = 1; k <= 4 && !crossing; ++k) crossing = in_trap(carried + (soon - carried) * (float(k) / 4.0f));
            bool known = false, clear = false;
            const auto later = predict(q, kHopFlight, &known), after = predict(q, kHopFlight + 20);
            const auto base = me + drift(p, kHopFlight);
            const auto landing = inside(p, later, base, &clear, &after);
            if (crossing && known && clear && flat(landing, base) <= covered(0.0f, kHopFlight - 1) - 1.5f) {
                hop_ = Hop{p, p, frame, frame, frame + kHopFlight, {}, landing, true};
                decision.jump = true;
                if (trace)
                    std::printf("[%d] explore: hop over the trap on %d, landing (%.1f,%.1f,%.1f)\n", frame, p, landing.x, landing.y, landing.z);
                steer(landing - drift(p, kHopFlight), 0.25f);
                return;
            }
        }
        steer(rest, 1.0f);
    };
    if (hop_) {
        auto& hop = *hop_;
        if (!hop.jumped) {
            // The run-up: on along it until the jump step or the edge.
            const auto ahead = world.check_land({me.x + hop.dir.x * 2.5f, me.y + 5.0f, me.z + hop.dir.z * 2.5f}, 1.0f, 400.0f, 1.0f);
            const bool edge = !ahead.found || ahead.position.y < me.y - 3.0f;
            if ((frame >= hop.jump_at || edge) && !airborne && player.control_enabled && !held_by_hit(player)) {
                decision.jump = true;
                hop.jumped = true;
                hop.land_at = frame + kHopFlight;
            } else if (frame - hop.started > 60) {
                hop_.reset(); // never got off the ground
                return false;
            }
            steer({me.x + hop.dir.x * 10.0f, me.y, me.z + hop.dir.z * 10.0f}, 0.25f);
            return true;
        }
        // In the air: the stick on where the landing will be at touch-down,
        // less what the take-off platform still carries the player.
        const int rest = std::max(1, hop.land_at - frame);
        const auto carried = drift(hop.from, rest);
        auto landing = hop.landing;
        if (hop.to >= 0) {
            const auto after = predict(platforms_[std::size_t(hop.to)], rest + 20);
            landing = inside(hop.to, predict(platforms_[std::size_t(hop.to)], rest), me + carried, nullptr, &after);
        }
        steer(landing - carried, 0.25f);
        if ((frame > hop.land_at - kHopFlight + 4 && !airborne) || frame > hop.land_at + 40) {
            if (trace)
                std::printf("[%d] explore: hop landed at (%.1f,%.1f,%.1f) on %d (aimed at %d)\n", frame, me.x, me.y, me.z,
                            platform_under(engine, me), hop.to);
            hop_.reset();
            hop_wait_since_ = -1;
        }
        return true;
    }
    const int on = platform_under(engine, me);
    const std::size_t i = pending_hop(on, me);
    if (i >= route_.size()) {
        if (on < 0) {
            hop_wait_since_ = -1;
            return false;
        }
        // Riding with nothing to hop to.
        ride(on);
        return true;
    }
    const int to = route_[i].support;
    if (to == on || to >= int(platforms_.size())) return false;
    if (hop_wait_since_ < 0) hop_wait_since_ = frame;
    if (frame - hop_wait_since_ > 3600) {
        // The platform never came within reach: the target is given up.
        if (trace) std::printf("[%d] explore gives up hopping onto %d (%s)\n", frame, to, what_.c_str());
        hop_wait_since_ = -1;
        give_up(frame);
        return false;
    }
    const auto& target_point = route_[i].point;
    // Landings on the stage floor round the route's landing, with floor
    // 3.5 units round them.
    if (to < 0 && flat(target_point, hop_spots_for_) > 0.1f) {
        hop_spots_for_ = target_point;
        hop_spots_.clear();
        for (float dz = -25.0f; dz <= 25.0f; dz += 2.5f)
            for (float dx = -25.0f; dx <= 25.0f; dx += 2.5f) {
                const float x = target_point.x + dx, z = target_point.z + dz, y = target_point.y;
                if (stage_floor(x, y, z) && stage_floor(x + 3.5f, y, z) && stage_floor(x - 3.5f, y, z) && stage_floor(x, y, z + 3.5f) &&
                    stage_floor(x, y, z - 3.5f) && !in_trap({x, y, z}))
                    hop_spots_.push_back({x, y, z});
            }
    }
    // Where to wait on the stage floor: at the edge where the platform's
    // clear part comes closest along its path, 10 units back (room for a
    // run-up to nearly full speed).
    if (on < 0 && to >= 0 && (flat(target_point, hop_wait_for_) > 0.1f || hop_wait_to_ != to)) {
        hop_wait_for_ = target_point;
        hop_wait_to_ = to;
        const auto takeoff = i > 0 && route_[i - 1].support < 0 ? route_[i - 1].point : me;
        const auto& q = platforms_[std::size_t(to)];
        const engine::Vec3f top{q.offset.x, q.top, q.offset.z};
        std::optional<engine::Vec3f> closest;
        for (std::size_t j = 0; j < q.trail.size(); j += 4) {
            bool clear = false;
            const auto spot = inside(to, q.trail[j] + top, takeoff, &clear);
            if (clear && (!closest || flat(spot, takeoff) < flat(*closest, takeoff))) closest = spot;
        }
        hop_wait_ = takeoff;
        if (closest) {
            const float gap = flat(*closest, takeoff);
            const engine::Vec3f u{(takeoff.x - closest->x) / std::max(gap, 0.01f), 0.0f, (takeoff.z - closest->z) / std::max(gap, 0.01f)};
            // The edge: the first stage floor from the platform's side.
            for (float t = 0.0f; t <= gap + 10.0f; t += 1.0f) {
                const engine::Vec3f e{closest->x + u.x * t, takeoff.y, closest->z + u.z * t};
                if (!stage_floor(e.x, e.y, e.z)) continue;
                const engine::Vec3f back{e.x + u.x * 10.0f, e.y, e.z + u.z * 10.0f};
                hop_wait_ = stage_floor(back.x, back.y, back.z) && !in_trap(back) ? back : e;
                break;
            }
        }
        if (trace)
            std::printf("[%d] explore: wait for %d at (%.1f,%.1f,%.1f)\n", frame, to, hop_wait_.x, hop_wait_.y, hop_wait_.z);
    }
    auto wait = on < 0 && to >= 0 ? hop_wait_ : me;
    if (on < 0 && to < 0) wait = i > 0 ? route_[i - 1].point : me;
    // Can a run-up of r steps (from standing) and a jump land on the next
    // support? The first r that can is taken.
    if (player.control_enabled && !airborne && !held_by_hit(player))
        for (int r = 0; r <= 30; r += 2) {
            const float run = covered(0.0f, r);
            const float flight = covered(std::min(kWalkSpeed, kWalkAccel * float(r)), kHopFlight - 1);
            bool known = true;
            if (on >= 0) predict(platforms_[std::size_t(on)], r + kHopFlight, &known);
            if (!known) break;
            const auto run_drift = drift(on, r), all_drift = drift(on, r + kHopFlight);
            // Where the player would land without moving itself.
            const engine::Vec3f base{me.x + all_drift.x, me.y, me.z + all_drift.z};
            engine::Vec3f landing{};
            if (to >= 0) {
                const auto centre = predict(platforms_[std::size_t(to)], r + kHopFlight, &known);
                if (!known) break;
                // (Clear of the trap lines while it carries the player on.)
                const auto after = predict(platforms_[std::size_t(to)], r + kHopFlight + 20);
                landing = inside(to, centre, base, nullptr, &after);
            } else {
                float best = std::numeric_limits<float>::infinity();
                for (const auto& spot : hop_spots_)
                    if (flat(spot, base) < best) {
                        best = flat(spot, base);
                        landing = spot;
                    }
                if (!std::isfinite(best)) break;
            }
            const float length = flat(landing, base);
            if (length - run > flight - 1.5f) continue;
            const engine::Vec3f dir = length > 0.01f ? engine::Vec3f{(landing.x - base.x) / length, 0.0f, (landing.z - base.z) / length}
                                                     : engine::Vec3f{};
            // The take-off must still be on the support (a longer run-up
            // leaves it too).
            const engine::Vec3f takeoff{me.x + run_drift.x + dir.x * run, me.y, me.z + run_drift.z + dir.z * run};
            bool on_support = true;
            if (on >= 0) {
                const auto& q = platforms_[std::size_t(on)];
                const auto c = predict(q, r);
                on_support = std::abs(takeoff.x - c.x) <= q.half_x - 1.5f && std::abs(takeoff.z - c.z) <= q.half_z - 1.5f;
            } else if (r > 0) {
                on_support = stage_floor(takeoff.x, me.y, takeoff.z);
            }
            // (Not from inside a trap mat: a laser fires at once and hits a
            // player still on the ground there.)
            if (!on_support || (r > 0 && in_trap(takeoff) && !in_trap(me))) break;
            hop_ = Hop{on, to, frame, frame + r, frame + r + kHopFlight, dir, landing, false};
            if (trace)
                std::printf("[%d] explore: hop from %d to %d: run %d steps, landing (%.1f,%.1f,%.1f) %.1f away (%s)\n", frame, on, to, r,
                            landing.x, landing.y, landing.z, length, what_.c_str());
            if (r == 0) {
                decision.jump = true;
                hop_->jumped = true;
            }
            steer({me.x + dir.x * 10.0f, me.y, me.z + dir.z * 10.0f}, 0.25f);
            return true;
        }
    // Waiting for the platform; enemies coming close are struck.
    if (on >= 0) {
        ride(on);
        if (hop_) return true;
    } else {
        steer(wait, 1.0f);
    }
    if (auto* models = engine.get<engine::ModelCtrlManager>(); models && !airborne)
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && e->hp > 0 && flat(position_of(*e), me) < 12.0f) {
                decision.attack = true;
                break;
            }
    return true;
}

void ExploreBot::plan(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame) {
    const void* previous = target_;
    const std::string previous_mat = target_mat_;
    planned_ = frame;
    route_.clear();
    boss_stand_.reset();
    const auto previous_perch = perch_;
    perch_.reset();
    next_ = 0;
    target_ = nullptr;
    target_mat_.clear();
    target_portal_.clear();
    target_radar_.clear();
    to_goal_ = false;
    closest_ = false;
    runup_ready_ = false;
    gimmick_ = Gimmick::none;
    what_ = "nothing";
    goal_point_ = position_of(player);
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (!hitland || !models) return;
    const WithoutLimitCircle without_limit(*hitland);
    const auto me = position_of(player);
    // DW4_TRACE_EXPLORE: talk gimmicks whose box reaches far out of the
    // model (gimmick_tolerance, talk_in_reach), once each.
    if (std::getenv("DW4_TRACE_EXPLORE")) {
        static std::set<std::pair<std::string, int>> far_talk;
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->hit && e->hit->player_talk && box_reach(*e) > kFarTalk &&
                far_talk.insert({e->info_name, e->index}).second)
                std::printf("[%d] explore: the talk box of %s#%d reaches %.1f out of it\n", frame, e->info_name.c_str(), e->index, box_reach(*e));
    }
    const auto flags = hitland->delivery_flags(player.info_name, player.index);
    const auto goals = goal_keys(goal);
    std::vector<AvoidBox> avoid;
    std::vector<engine::MatCtrlManager::MatBox> boxes;
    if (mats) boxes = mats->mat_boxes();
    // A pit inside a quicksand basin is kept well away from (the sand pulls
    // a player standing near it in); the sand itself is walked over.
    bool sandy = false;
    for (const auto& box : boxes) sandy = sandy || quicksand_mat(engine, box.name);
    std::vector<AvoidBox> pits;
    for (const auto& box : boxes) {
        // Other stage change mats would open their prompt.
        const auto key = mat_key(box.name);
        const bool pit = hazard_mat(engine, box.name);
        const bool next_in_sequence = mat_sequence_next_ < mat_sequence_.size() && mat_sequence_[mat_sequence_next_] == box.name;
        const bool avoided = !avoid_mats_.empty() && box.name.rfind(avoid_mats_, 0) == 0 && !next_in_sequence;
        if (goals.count(key) || (!exit_mat(engine, box.name) && !pit && !avoided)) continue;
        // A pit a moving platform crosses ends at the mat's top: its banks
        // and the platform stand just above it (dng412b4: mat top 96, floor
        // 100), and the hops start and end at its rim.
        bool crossed = false;
        if (pit && !sandy)
            for (const auto& p : platforms_)
                for (std::size_t j = 0; j < p.trail.size() && !crossed; j += 10)
                    crossed = p.trail[j].x >= box.min.x && p.trail[j].x <= box.max.x && p.trail[j].z >= box.min.z && p.trail[j].z <= box.max.z;
        const float grow = pit && sandy ? 25.0f : 4.0f, above = pit && sandy ? 35.0f : crossed ? 1.0f : 5.0f;
        const AvoidBox a{box.min.x - grow, box.min.z - grow, box.max.x + grow, box.max.z + grow, box.min.y - 5.0f, box.max.y + above};
        if (pit) pits.push_back(a);
        auto inside = [&](const AvoidBox& b) {
            return me.x >= b.min_x && me.x <= b.max_x && me.z >= b.min_z && me.z <= b.max_z && me.y >= b.min_y && me.y <= b.max_y;
        };
        if (inside(a)) {
            // Fallen into the basin: its sink stays closed for the way out.
            const AvoidBox core{box.min.x - 4.0f, box.min.z - 4.0f, box.max.x + 4.0f, box.max.z + 4.0f, box.min.y - 5.0f, box.max.y + 5.0f};
            if (pit && sandy && !inside(core)) avoid.push_back(core);
            continue;
        }
        avoid.push_back(a);
    }
    // `margin` widens the boxes (enemies near the sand edge: a hit throws the
    // player 35 units, over the edge).
    auto in_pit = [&](const engine::Vec3f& p, float margin) {
        for (const auto& a : pits)
            if (p.x >= a.min_x - margin && p.x <= a.max_x + margin && p.z >= a.min_z - margin && p.z <= a.max_z + margin && p.y >= a.min_y &&
                p.y <= a.max_y)
                return true;
        return false;
    };
    // A mat that switches a wall on only while the player stands in it
    // (MATisPlayer, HITLANDMODEL ONHIT/OFFHIT) and the wall lies outside the
    // mat: the player meets that wall only after leaving the mat, when it is
    // off again, so it is planned off (else the plan flips each time the
    // player steps out and back in). Switched back after the plan.
    struct MatWalls {
        engine::HitLandManager& hitland;
        std::vector<std::pair<std::string, bool>> saved;
        ~MatWalls() {
            for (auto it = saved.rbegin(); it != saved.rend(); ++it) hitland.set_child_hit(it->first, it->second);
        }
    } mat_walls{*hitland, {}};
    for (const auto& box : boxes) {
        if (me.x < box.min.x || me.x > box.max.x || me.z < box.min.z || me.z > box.max.z || me.y < box.min.y || me.y > box.max.y) continue;
        const std::string script = box.name + ".lua";
        if (!engine.script_mentions(script, "MATisPlayer") || !engine.script_mentions(script, "HITLANDMODEL") ||
            !engine.script_mentions(script, "ONHIT") || !engine.script_mentions(script, "OFFHIT"))
            continue;
        for (const auto& row : engine.messenger().rows()) {
            if (row.category != "HITLANDMODEL" || !row.name || !engine.script_mentions(script, *row.name)) continue;
            const std::string& name = *row.name;
            const auto on = hitland->child_hit(name);
            if (!on || !*on) continue;
            bool seen = false;
            for (const auto& [saved_name, state] : mat_walls.saved) seen = seen || saved_name == name;
            if (seen) continue;
            auto it = child_boxes_.find(name);
            if (it == child_boxes_.end()) {
                // The triangles that change when the child is switched once.
                ChildBox found;
                auto& world = hitland->world();
                const auto& triangles = world.triangles();
                std::vector<char> before(triangles.size());
                for (std::size_t i = 0; i < triangles.size(); ++i) before[i] = world.group_enabled(triangles[i].group) ? 1 : 0;
                hitland->set_child_hit(name, false);
                for (std::size_t i = 0; i < triangles.size(); ++i) {
                    if ((world.group_enabled(triangles[i].group) ? 1 : 0) == before[i]) continue;
                    found.found = true;
                    for (const auto& v : {triangles[i].a, triangles[i].b, triangles[i].c}) {
                        found.lo = {std::min(found.lo.x, v.x), std::min(found.lo.y, v.y), std::min(found.lo.z, v.z)};
                        found.hi = {std::max(found.hi.x, v.x), std::max(found.hi.y, v.y), std::max(found.hi.z, v.z)};
                    }
                }
                hitland->set_child_hit(name, true);
                it = child_boxes_.emplace(name, found).first;
            }
            const auto& wall = it->second;
            if (!wall.found) continue;
            // A wall within reach of the mat is met while standing in it.
            if (wall.hi.x >= box.min.x - 5.0f && wall.lo.x <= box.max.x + 5.0f && wall.hi.z >= box.min.z - 5.0f && wall.lo.z <= box.max.z + 5.0f)
                continue;
            hitland->set_child_hit(name, false);
            mat_walls.saved.push_back({name, true});
        }
    }
    // Recent enemy fire lanes make the routes go round them where they can.
    FireLaneCells lanes;
    for (const auto& [key, cell] : fire_cells_)
        if (frame - cell.frame <= 150) lanes[key] = {cell.lo, cell.hi};
    // Without the test cheats the ground round a structure seen healing
    // itself (regenerating_names: the area 3 poison plants spit at a player
    // 20 to 46 units off, ~5 % of the HP a hit, and never fall) counts as a
    // fire lane within kHealerKeep, and nothing there is walked to (near_healer):
    // a no-cheat chain was defeated beside them again and again, walking to
    // items, enemies and its own bit capsule there.
    std::vector<engine::Vec3f> healers;
    if (!engine.cheats().one_hit && !engine.cheats().god && !regenerating_names_.empty())
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && regenerating_names_.count(e->info_name)) healers.push_back(position_of(*e));
    for (const auto& h : healers) {
        const int r = int(std::ceil(kHealerKeep / Grid::step));
        const int cx = int(std::lround(h.x / Grid::step)), cz = int(std::lround(h.z / Grid::step));
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dz * dz > r * r) continue;
                const auto [it, fresh] = lanes.try_emplace({cx + dx, cz + dz}, h.y - 15.0f, h.y + 40.0f);
                if (!fresh) it->second = {std::min(it->second.first, h.y - 15.0f), std::max(it->second.second, h.y + 40.0f)};
            }
    }
    const auto near_healer = [&](const engine::Vec3f& p) {
        for (const auto& h : healers)
            if (flat(h, p) < kHealerKeep && p.y > h.y - 15.0f && p.y < h.y + 40.0f) return true;
        return false;
    };
    const FireLanes fire_guard(lanes);
    // The moving platforms' paths (in platforms_ order; one that does not
    // keep moving sideways is plain floor where it stands).
    std::vector<MovingFloor> moving;
    for (auto& p : platforms_) {
        MovingFloor floor;
        // (One just seen setting off counts while it keeps moving, so no
        // route takes it for fixed floor meanwhile.)
        p.hop_on = p.trail.size() < 60 ? p.moving_steps * 2 >= int(p.trail.size()) : p.sideways();
        if (p.hop_on) {
            floor.info = p.info;
            floor.child = p.child;
            floor.half_x = p.half_x;
            floor.half_z = p.half_z;
            for (std::size_t j = 0; j < p.trail.size(); j += 3)
                floor.centres.push_back({p.trail[j].x + p.offset.x, p.trail[j].y + p.top, p.trail[j].z + p.offset.z});
        }
        moving.push_back(std::move(floor));
    }
    // Floors a fall began from lately (avoid_fall), unless the player stands
    // on one.
    std::erase_if(fall_spots_, [&](const auto& spot) { return frame >= spot.second; });
    for (const auto& [p, until] : fall_spots_) {
        const AvoidBox a{p.x - 8.0f, p.z - 8.0f, p.x + 8.0f, p.z + 8.0f, p.y - 6.0f, p.y + 6.0f};
        if (me.x < a.min_x || me.x > a.max_x || me.z < a.min_z || me.z > a.max_z || me.y < a.min_y || me.y > a.max_y) avoid.push_back(a);
    }
    const WalkMap map(hitland->world(), me, 700.0f, flags, avoid, hitland->delivery_width(player.info_name, player.index), moving);
    // A point's distance from the nearest quicksand pit's guard box (flat).
    const auto pit_clear = [&](const engine::Vec3f& q) {
        float clear = std::numeric_limits<float>::infinity();
        for (const auto& a : pits) {
            const float dx = std::max({a.min_x - q.x, 0.0f, q.x - a.max_x}), dz = std::max({a.min_z - q.z, 0.0f, q.z - a.max_z});
            clear = std::min(clear, std::hypot(dx, dz));
        }
        return clear;
    };
    // Whether a point stands on a quicksand mat (where the sand pulls).
    auto on_sand = [&](const engine::Vec3f& p) {
        for (const auto& box : boxes)
            if (p.x >= box.min.x && p.x <= box.max.x && p.z >= box.min.z && p.z <= box.max.z && p.y >= box.min.y - 5.0f &&
                p.y <= box.max.y + 10.0f && quicksand_mat(engine, box.name))
                return true;
        return false;
    };
    // How far the reached sand gets from the pits (see kSandFightMargin):
    // the enemy margin stays below it, so a small room still has enemies to
    // fight.
    float sand_clear = 0.0f;
    if (sandy && !engine.cheats().one_hit && !engine.cheats().god)
        for (const auto& q : map.samples())
            for (const auto& box : boxes)
                if (q.x >= box.min.x && q.x <= box.max.x && q.z >= box.min.z && q.z <= box.max.z && quicksand_mat(engine, box.name)) {
                    sand_clear = std::max(sand_clear, pit_clear(q));
                    break;
                }
    auto go = [&](const engine::Vec3f& where, const void* target, std::string what) {
        route_ = map.route_to(where);
        target_ = target;
        goal_point_ = where;
        what_ = std::move(what);
    };
    auto left_alone = [&](const void* target) {
        const auto it = unreachable_.find(target);
        return it != unreachable_.end() && frame < it->second;
    };
    // The post an enemy is fought from when walking up to it does not do:
    // a hovering enemy's perch (reached floor within 40 of it whose swing
    // meets its body, a step beside the enemy's floor, close to the enemy:
    // it comes after the player) or a place next to a turret, 3 to 11 units
    // from its body, rather behind it and out of its lanes than in front
    // (it fires along its +Z and turns to its target, but not while the hits
    // stun it; turrets whose collision fills the way are fought from the
    // front). Not at a rim (three of the four neighbours on the same floor,
    // all four next to a turret: its bullets throw the player back), the
    // cheapest to walk to; the previous one is kept while it serves.
    auto find_perch = [&](const engine::ModelCtrlManager::ModelRam& e, bool turret) -> std::optional<engine::Vec3f> {
        const auto at = position_of(e);
        const float axis = std::atan2(e.node->matrix(2, 0), e.node->matrix(2, 2));
        const float bx = std::round(at.x / Grid::step) * Grid::step, bz = std::round(at.z / Grid::step) * Grid::step;
        std::optional<engine::Vec3f> best;
        float best_cost = std::numeric_limits<float>::infinity();
        for (float dz = -40.0f; dz <= 40.0f; dz += Grid::step)
            for (float dx = -40.0f; dx <= 40.0f; dx += Grid::step) {
                if (dx * dx + dz * dz > 1600.0f) continue;
                const auto q = map.nearest(engine::Vec3f{bx + dx, at.y, bz + dz}, 1.0f);
                if (!q || !swing_meets(q->y, e)) continue;
                const float off = std::abs(std::remainder(std::atan2(q->x - at.x, q->z - at.z) - axis, 6.2831853f));
                if (turret && (body_gap(e, *q) < 3.0f || body_gap(e, *q) > 11.0f)) continue;
                if (const auto skip = perch_skip_.find({int(std::lround(q->x / Grid::step)), int(std::lround(q->z / Grid::step))});
                    skip != perch_skip_.end() && frame < skip->second)
                    continue;
                int level = 0;
                for (const auto [ox, oz] : {std::pair{Grid::step, 0.0f}, std::pair{-Grid::step, 0.0f}, std::pair{0.0f, Grid::step}, std::pair{0.0f, -Grid::step}})
                    if (const auto n = map.nearest(engine::Vec3f{q->x + ox, q->y, q->z + oz}, 1.0f); n && std::abs(n->y - q->y) < 1.0f) ++level;
                if (level < (turret ? 4 : 3)) continue;
                float cost = map.cost_to(*q, 1.0f);
                if (turret) {
                    cost += (3.1415927f - off) * 10.0f + (in_fire(*q, frame) ? 10.0f : 0.0f);
                    // (A post whose swing reaches other turrets too strikes them at once.)
                    for (const auto& [other, shots] : shots_)
                        if (other != &e && !shots.empty()) {
                            const auto* o = static_cast<const engine::ModelCtrlManager::ModelRam*>(other);
                            if (o->generated && o->hp > 0 && o->node && body_gap(*o, *q) <= 8.0f && swing_meets(q->y, *o)) cost -= 20.0f;
                        }
                } else {
                    cost += 2.0f * flat(*q, at);
                }
                if (previous_perch && flat(*previous_perch, *q) < 1.0f && std::abs(previous_perch->y - q->y) < 1.0f) cost *= 0.5f;
                if (cost < best_cost) {
                    best_cost = cost;
                    best = q;
                }
            }
        return best;
    };
    // Fallen into a quicksand basin: out along the cheapest route to floor
    // outside it (walking straight away from the sink only pins the player
    // against the basin walls).
    if (sandy && in_pit(me, 0.0f)) {
        std::optional<engine::Vec3f> out;
        float out_cost = std::numeric_limits<float>::infinity();
        for (const auto& q : map.samples()) {
            if (in_pit(q, 0.0f)) continue;
            const float cost = map.cost_to(q, 1.0f);
            if (cost < out_cost) {
                out_cost = cost;
                out = q;
            }
        }
        if (out) {
            go(*out, nullptr, "out of the basin");
            return;
        }
    }
    // The kept target counts as nearer, so two equally far ones do not take
    // turns.
    auto keep = [&](const void* target, float cost) { return target == previous ? cost * 0.6f : cost; };
    // (Crossing a field to an exit, set_transit: as while defending. A no-cheat
    // chain spent 33000 frames a crossing of stag0302 and 31000 of the swamp
    // stag0303 hunting e_maridevi and the like, which the cheat run's one-hit
    // blows took in passing: 12800 and 3700.)
    const bool defending = frame < defend_until_ || transit_;
    // A started radar generator waits for a player near its base: walk
    // into its range. Before the mats only one the mat underfoot started
    // (leaving that mat would stop it again), after them any.
    auto radar = [&](bool underfoot_only) {
        auto* generators = engine.get<engine::GeneraterManager>();
        if (!generators) return false;
        for (const auto& waiting : generators->radar_waiting()) {
            if (const auto skip = radar_skip_.find(waiting.name); skip != radar_skip_.end() && frame < skip->second) continue;
            if (underfoot_only) {
                bool started_here = false;
                for (const auto& box : boxes)
                    if (me.x >= box.min.x && me.x <= box.max.x && me.z >= box.min.z && me.z <= box.max.z &&
                        engine.script_mentions(box.name + ".lua", waiting.name))
                        started_here = true;
                if (!started_here) continue;
            }
            const float dy = waiting.base.y - me.y;
            if (waiting.range_sq <= dy * dy + 25.0f) continue; // out of reach from the floor
            const float reach = std::sqrt(waiting.range_sq - dy * dy) - 5.0f;
            if (reach < 3.0f || flat(waiting.base, me) <= reach) continue;
            if (!std::isfinite(map.cost_to(waiting.base, reach))) continue;
            go(waiting.base, nullptr, "radar " + waiting.name);
            route_ = map.route_to(waiting.base, reach);
            target_radar_ = waiting.name;
            return true;
        }
        return false;
    };
    // A wave the mat underfoot started runs only while a player stays on it
    // (leaving or falling off stops the generator; only a wave that started
    // while the player stood on it, not one the mat merely names, e.g. in a
    // STOPREVERS): for up to 3000 frames
    // only enemies and pickups on it are gone for (or a radar base).
    const engine::MatCtrlManager::MatBox* hold = nullptr;
    if (auto* generators = engine.get<engine::GeneraterManager>())
        for (const auto& box : boxes) {
            if (me.x < box.min.x || me.x > box.max.x || me.z < box.min.z || me.z > box.max.z) continue;
            const std::string script = box.name + ".lua";
            if (!engine.script_mentions(script, "START")) continue;
            // (Not a wave that restarts until something else is done.)
            for (const auto& status : generators->status())
                if (status.started && !status.done && status.clears < 2 && engine.script_mentions(script, status.name) &&
                    gen_mats_[status.name].count(box.name))
                    hold = &box;
        }
    // Each mat is held for 3000 frames in all (crossing it again later
    // does not start over).
    if (hold) {
        auto& left = hold_left_.try_emplace(hold->name, 3000).first->second;
        left -= hold_mat_ == hold->name ? std::min(45, frame - hold_since_) : 0;
        hold_mat_ = hold->name;
        hold_since_ = frame;
        if (left <= 0) hold = nullptr;
    }
    if (!hold) hold_mat_.clear();
    auto on_hold = [&](const engine::Vec3f& p) {
        return p.x >= hold->min.x - 10.0f && p.x <= hold->max.x + 10.0f && p.z >= hold->min.z - 10.0f && p.z <= hold->max.z + 10.0f;
    };
    // The goal mat once it can be reached.
    // (Of several, a reachable one; the point walked to is reached floor
    // inside the mat, whose middle may lie in a wall. A mat reaching only a
    // few units into a niche has no grid cell inside: the reached cell at its
    // rim is walked to and the player goes on straight into the mat.)
    std::optional<engine::Vec3f> goal_centre, goal_inside, goal_point;
    for (const auto& box : boxes) {
        if (!goals.count(mat_key(box.name))) continue;
        if (!goal_centre) goal_centre = mat_centre(box, (box.min.y + box.max.y) * 0.5f);
        if (!goal_inside) goal_inside = map.reached_inside(box.min.x + 1.0f, box.min.z + 1.0f, box.max.x - 1.0f, box.max.z - 1.0f);
    }
    if (goal_centre && !goal_inside)
        for (const auto& box : boxes) {
            if (!goals.count(mat_key(box.name))) continue;
            const auto rim = map.reached_inside(box.min.x - 5.0f, box.min.z - 5.0f, box.max.x + 5.0f, box.max.z + 5.0f);
            if (!rim || rim->y < box.min.y - 5.0f || rim->y > box.max.y + 5.0f) continue;
            goal_inside = rim;
            goal_point = engine::Vec3f{std::clamp(rim->x, box.min.x + 2.0f, box.max.x - 2.0f), rim->y,
                                       std::clamp(rim->z, box.min.z + 2.0f, box.max.z - 2.0f)};
            break;
        }
    auto go_goal = [&] {
        go(*goal_inside, nullptr, "goal " + goal + (goal_point ? " (rim)" : ""));
        if (goal_point) goal_point_ = *goal_point;
        to_goal_ = true;
    };
    const bool in_sand = player.ride && player.ride->kind == engine::ModelCtrlManager::ModelRam::Ride::Kind::quicksand;
    // An open exit is taken before anything else; if standing on it does
    // nothing (a gate not up yet), the rest comes first from then on.
    if (goal_inside && frame >= goal_later_until_ && !goal_first_failed_ && !in_sand) {
        go_goal();
        return;
    }
    // The next mat of a given sequence.
    if (mat_sequence_next_ < mat_sequence_.size())
        for (const auto& box : boxes) {
            if (box.name != mat_sequence_[mat_sequence_next_]) continue;
            if (const auto inside = map.reached_inside(box.min.x + 1.0f, box.min.z + 1.0f, box.max.x - 1.0f, box.max.z - 1.0f);
                inside && inside->y >= box.min.y - 5.0f && inside->y <= box.max.y + 5.0f) {
                go(*inside, nullptr, "sequence mat " + box.name);
                target_mat_ = box.name;
                return;
            }
        }
    // The moving platform the player rides (its own floor under the player,
    // seen moving within the last 60 frames), else -1, and whether a reached
    // point lies on it: on its path (the walking map's moving floor) or on
    // its collision where it stands (a platform the map takes for fixed, one
    // whose recorded steps did not keep moving sideways, is plain floor
    // there).
    int ride = -1;
    if (const auto land = hitland->world().check_land({me.x, me.y + 5.0f, me.z}, 1.0f, 400.0f, 1.0f); land.found && land.position.y >= me.y - 8.0f)
        for (std::size_t i = 0; i < platforms_.size() && ride < 0; ++i)
            if (platforms_[i].info == land.info && platforms_[i].child == land.child && frame - platforms_[i].moved_at <= 60) ride = int(i);
    const auto on_ride = [&](const engine::Vec3f& q, int support) {
        if (support == ride) return true;
        const auto land = hitland->world().check_land({q.x, q.y + 5.0f, q.z}, 1.0f, 400.0f, 1.0f);
        const auto& p = platforms_[std::size_t(ride)];
        return land.found && land.info == p.info && land.child == p.child;
    };
    std::set<const void*> off_ride;
    // Live enemies (structures such as towers first), then pickups: the
    // cheapest to walk to. While defending enemies are only struck in
    // passing (update), except structures.
    for (int pass = 0; pass < 2; ++pass) {
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        float best_cost = std::numeric_limits<float>::infinity();
        std::optional<engine::Vec3f> best_perch;
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node) continue;
            if (pass == 0 ? (e->category != "ENEMY" || e->hp <= 0) : (e->category != "ITEM" || e->item_table < 0)) continue;
            // Hits reach a model through its hit script; without one (a
            // boss's head or decoration) it cannot be hurt.
            if (pass == 0 && !engine.has_script("hit_" + e->info_name + ".lua")) continue;
            // A full folder leaves the item lying (pick_item: 36 devices, 9 of
            // a disk): not walked to.
            if (pass == 1 && e->item_table != 1 && e->item_table != 2)
                if (auto* sys = engine.get<engine::GameSystem>()) {
                    auto& slot = sys->players[std::size_t(std::clamp(player.index, 0, 3))];
                    if (e->item_table == 0 ? slot.disk_count(e->item_record) >= 9 : slot.devices.size() >= 36) continue;
                }
            if (left_alone(e.get()) || unkillable_.count(e.get())) continue;
            const auto at = position_of(*e);
            const bool structure = e->info_name.rfind("g_", 0) == 0;
            if (pass == 0 && structure && regenerating_names_.count(e->info_name) && !engine.cheats().one_hit && !engine.cheats().god) continue;
            const bool boss = e->info_name.rfind("f_", 0) == 0;
            if (!boss && (pass == 0 || e->item_table != 2) && near_healer(at)) continue; // (key items are fetched all the same)
            if (transit_ && pass == 1 && e->item_table != 2 && flat(at, me) > kTransitPick) continue;
            const bool hover = pass == 0 && hovering(*e);
            // (A structure seen firing along a lane is a turret.)
            const auto fired = shots_.find(e.get());
            const bool turret = pass == 0 && structure && fired != shots_.end() && !fired->second.empty();
            if (pass == 0 && defending && !structure && !boss && !hover) continue;
            // (Turrets are fought with real blows too. Leaving them alone
            // until the stage was explored to the end kept the way shut where
            // their bodies fill it - stag0201's g_cannon01 pair before the
            // movepass to matz02: 180000 frames without reaching it - and in
            // stag0204, once its gates were open, the level 25 no-cheat chain
            // crossed its g_cannon02 rows in 37000 frames under 48 shells of
            // 15 % of the maximum HP (eneatk TYPE 2: no level helps) and was
            // out of heals in dng206f1; fighting them (4 blows each,
            // hit_g_cannon02.lua -25 of 100) it crossed in 21800 frames under
            // 30 and went on to dng206f2. On a first visit both ways took the
            // same shells, 33 and 37.)
            if (hold && !on_hold(at)) continue;
            // Near a pit in a quicksand basin: struck only in passing (with
            // real blows further off as well, see the hold below), where the
            // sand pulls (the enemy on a quicksand mat) or right at the pit.
            // (An enemy off the sand is walked to: dng205b3's GENE-14 wave,
            // eight e_mecha2 on the floor at y 35 north of the sand, some 100
            // off the matt23 sink and staying there, was never fought; its
            // flag 188 opens the door to Anomalocarimon, and a fresh level 21
            // to 26 no-cheat chain gave key item 5 up ten times in 3 million
            // frames, the wave's members respawning 1264 times meanwhile.)
            if (sandy && in_pit(at, std::min(kSandFightMargin, std::max(0.0f, sand_clear - 15.0f))) &&
                (in_pit(at, 15.0f) || on_sand(at))) {
                unreachable_[e.get()] = frame + 120;
                continue;
            }
            // A structure's or a boss's body keeps the player further off (a
            // boss may stand on a platform its body covers).
            float cost = map.cost_to(at, pass == 1 ? 8.0f : boss ? 60.0f : structure ? 30.0f : 14.0f);
            // A hovering enemy counts by the walk to its perch (a turret by the
            // walk to its post, if there is one out of its fire).
            std::optional<engine::Vec3f> perch;
            if (hover || turret) {
                perch = find_perch(*e, turret);
                if (perch) cost = map.cost_to(*perch, 1.0f);
                else if (hover) cost = std::numeric_limits<float>::infinity();
            }
            if (!std::isfinite(cost)) {
                unreachable_[e.get()] = frame + 300;
                continue;
            }
            // On a moving platform an enemy is gone for only where the ride
            // lets the player strike it: its nearest reached floor (a perch's
            // post) lies on the ride, and a structure's or a boss's body
            // within a swing of it (update: 15). One out of reach from the
            // ride is struck once the ride carries the player there. (The
            // stag0103 boat, walled all round, passes the shore's
            // g_numepodz01 pods with their bodies 18 and more off its deck,
            // and the walking map found shore floor past its rail: after a pod
            // the bot pressed against the rail, open to the pods' missiles,
            // and every hit on the player outside SQ_GUARD costs the boat 5
            // of its 100 HP, hit_p_agumon.lua: the twentieth sank it, game
            // over and back to stag0102.) Without the test cheats the rule in
            // update also leaves an enemy whose way rides a platform.
            if (pass == 0 && ride >= 0) {
                int support = -1;
                const auto stand =
                    perch ? map.nearest(*perch, 1.0f, &support) : map.nearest(at, boss ? 60.0f : structure ? 30.0f : 14.0f, &support);
                if (!stand || !on_ride(*stand, support) || (!perch && (structure || boss) && body_gap(*e, *stand) > 15.0f)) {
                    if (std::getenv("DW4_TRACE_EXPLORE") && !off_ride_.count(e.get()))
                        std::printf("[%d] explore: %s#%d is out of reach from the ride\n", frame, e->info_name.c_str(), e->index);
                    off_ride.insert(e.get());
                    continue;
                }
            }
            if (structure) cost *= 0.3f;
            cost = keep(e.get(), cost);
            if (cost < best_cost) {
                best_cost = cost;
                best = e.get();
                best_perch = perch;
            }
        }
        if (pass == 0) off_ride_.swap(off_ride);
        if (best) {
            go(position_of(*best), best, (pass == 0 ? "enemy " : "item ") + best->info_name);
            waiting_since_ = -1;
            if (best_perch) {
                perch_ = best_perch;
                route_ = map.route_to(*best_perch, 1.0f);
                what_ += " (perch)";
                if (std::getenv("DW4_TRACE_EXPLORE") && !(previous_perch && flat(*previous_perch, *best_perch) < 1.0f))
                    std::printf("[%d] explore: %s body y %.0f..%.0f %s, post (%.0f,%.0f,%.0f)\n", frame, best->info_name.c_str(),
                                body_bottom(*best), body_top(*best), hovering(*best) ? "hovers above the swing" : "fires along lanes",
                                best_perch->x, best_perch->y, best_perch->z);
            }
            // A structure (a cannon whose beam or shots run along its +Z) is
            // walked up to from the side: the cheapest reached point next to
            // its body at least 30 degrees off that axis.
            if (pass == 0 && !best_perch && best->info_name.rfind("g_", 0) == 0 && best->node) {
                const auto& m = best->node->matrix;
                const auto at = position_of(*best);
                float reach = 4.0f;
                if (best->hit)
                    for (const auto& [lo, hi] : best->hit->boxes) reach = std::max({reach, (hi.x - lo.x) * 0.5f, (hi.z - lo.z) * 0.5f});
                const float axis = std::atan2(m(2, 0), m(2, 2));
                std::optional<engine::Vec3f> flank;
                float flank_cost = std::numeric_limits<float>::infinity();
                for (int step = 0; step < 24; ++step) {
                    const float angle = float(step) * 0.2617994f;
                    float off = std::remainder(angle - axis, 6.2831853f);
                    if (std::abs(off) < 0.5235988f) continue;
                    const engine::Vec3f p{at.x + std::sin(angle) * (reach + 8.0f), at.y, at.z + std::cos(angle) * (reach + 8.0f)};
                    const float cost = map.cost_to(p, 3.0f);
                    if (cost < flank_cost) {
                        flank_cost = cost;
                        flank = p;
                    }
                }
                if (flank) route_ = map.route_to(*flank, 3.0f);
            }
            // A boss whose body starts above the player's swing box (feet to
            // feet + 8; Apocalymon's body y 30..70 hovers over the stagBS01
            // dais at y 30, the floor round it is y 20): no swing reaches it
            // from below and A is not taken in the air (8034C3C8 runs only
            // standing or walking), so the route leads up onto its floor, to
            // the cheapest reached point next to its body there.
            if (pass == 0 && best->info_name.rfind("f_", 0) == 0 && best->node && best->hit && !best->hit->boxes.empty()) {
                const auto& m = best->node->matrix;
                float bottom = 1e30f, top = -1e30f;
                engine::Vec3f lo{1e30f, 0.0f, 1e30f}, hi{-1e30f, 0.0f, -1e30f};
                for (const auto& [box_lo, box_hi] : best->hit->boxes) {
                    // 80307EAC: the root matrix moves the centre, the half extents stay.
                    const engine::Vec3f c{(box_lo.x + box_hi.x) * 0.5f, (box_lo.y + box_hi.y) * 0.5f, (box_lo.z + box_hi.z) * 0.5f};
                    const engine::Vec3f half{(box_hi.x - box_lo.x) * 0.5f, (box_hi.y - box_lo.y) * 0.5f, (box_hi.z - box_lo.z) * 0.5f};
                    const engine::Vec3f w{c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0) + m(3, 0), c.x * m(0, 1) + c.y * m(1, 1) + c.z * m(2, 1) + m(3, 1),
                                          c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2) + m(3, 2)};
                    bottom = std::min(bottom, w.y - half.y);
                    top = std::max(top, w.y + half.y);
                    lo = {std::min(lo.x, w.x - half.x), 0.0f, std::min(lo.z, w.z - half.z)};
                    hi = {std::max(hi.x, w.x + half.x), 0.0f, std::max(hi.z, w.z + half.z)};
                }
                const auto my_floor = map.nearest(me, 5.0f);
                if (bottom > (my_floor ? my_floor->y : me.y) + 7.0f) {
                    const engine::Vec3f centre{(lo.x + hi.x) * 0.5f, 0.0f, (lo.z + hi.z) * 0.5f};
                    const float half = std::max(hi.x - lo.x, hi.z - lo.z) * 0.5f;
                    float stand_cost = std::numeric_limits<float>::infinity();
                    for (int step = 0; step < 24; ++step) {
                        const float angle = float(step) * 0.2617994f;
                        const engine::Vec3f p{centre.x + std::sin(angle) * (half + 6.0f), bottom, centre.z + std::cos(angle) * (half + 6.0f)};
                        const auto floor = map.nearest(p, 3.0f);
                        if (!floor || floor->y < bottom - 7.0f || floor->y > top) continue;
                        const float cost = map.cost_to(p, 3.0f);
                        if (cost < stand_cost) {
                            stand_cost = cost;
                            boss_stand_ = *floor;
                        }
                    }
                    if (boss_stand_) route_ = map.route_to(*boss_stand_, 3.0f);
                    if (std::getenv("DW4_TRACE_EXPLORE"))
                        std::printf("[%d] explore: %s body y %.0f..%.0f above the swing, stand %s (%.0f,%.0f,%.0f)\n", frame, best->info_name.c_str(),
                                    bottom, top, boss_stand_ ? "at" : "none", boss_stand_ ? boss_stand_->x : 0.0f,
                                    boss_stand_ ? boss_stand_->y : 0.0f, boss_stand_ ? boss_stand_->z : 0.0f);
                }
            }
            return;
        }
    }
    // Defeated enemies stay until their model is removed; a wave counts as
    // cleared only then, and leaving its mat earlier stops the generator.
    if (!defending && frame >= wait_skip_until_ && !in_sand)
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && e->hp <= 0 && flat(position_of(*e), me) < 200.0f) {
                // Waited for at the point the wait began (a drifting point
                // would let quicksand pull the player along).
                if (waiting_since_ < 0) {
                    waiting_since_ = frame;
                    wait_point_ = me;
                }
                goal_point_ = wait_point_;
                what_ = "wait for the defeated";
                if (frame - waiting_since_ > 900) wait_skip_until_ = frame + 1800; // a body that never goes
                return;
            }
    waiting_since_ = -1;
    if (goal_centre) {
        if (goal_inside && frame >= goal_later_until_) {
            go_goal();
            return;
        }
        // (Not while the goal waits for the rest either: the far route and
        // the other targets would take turns at 280 units, and its watchdog
        // would put the goal off again and again.)
        if (flat(*goal_centre, me) > 280.0f && frame >= goal_later_until_) {
            const auto path = find_walk_path(hitland->world(), me, *goal_centre, flags);
            if (!path.empty() && path.back().x == goal_centre->x && path.back().z == goal_centre->z) {
                for (const auto& point : path) route_.push_back({point, false});
                goal_point_ = *goal_centre;
                what_ = "goal " + goal + " (far)";
                to_goal_ = true;
                return;
            }
        }
    }
    if (hold) {
        if (radar(true)) return;
        // (In a quicksand room the reached point nearest the middle that is
        // not at the pit, else the pull drags the player in.)
        std::optional<engine::Vec3f> inside;
        if (!pits.empty() && !engine.cheats().one_hit && !engine.cheats().god) {
            // With real blows the fight lasts: the reached point of the mat
            // farthest from the pits (also without sand: dng206f2's matg20
            // wave was held at the pit's rim beside the moving platforms,
            // and a level 26 no-cheat chain was thrown in three times in
            // 3000 frames, out of heals). The sand pulls toward the sink at 0.26
            // to 0.51 a frame (SETQUICKSAND, 80309674) whenever the player
            // does not walk - every swing, guard and knock-back - and a
            // guard's recoil (SQ_GUARDR, ~15) or a hit throws it further:
            // held at the middle of dng205b3's matg06 room, 45 from the sink
            // centre, a level 18 no-cheat run fell in 8..14 times a visit,
            // each fall back with 1 HP (ADDHP absolute, 8036F220) and a heal.
            float farthest = -1.0f;
            for (const auto& q : map.samples()) {
                if (q.x < hold->min.x + 1.0f || q.x > hold->max.x - 1.0f || q.z < hold->min.z + 1.0f || q.z > hold->max.z - 1.0f) continue;
                if (const float clear = pit_clear(q); clear > farthest) {
                    farthest = clear;
                    inside = q;
                }
            }
        } else if (sandy) {
            const engine::Vec3f middle{(hold->min.x + hold->max.x) * 0.5f, 0.0f, (hold->min.z + hold->max.z) * 0.5f};
            float nearest = std::numeric_limits<float>::infinity();
            for (const auto& q : map.samples()) {
                if (q.x < hold->min.x + 1.0f || q.x > hold->max.x - 1.0f || q.z < hold->min.z + 1.0f || q.z > hold->max.z - 1.0f ||
                    in_pit(q, 5.0f))
                    continue;
                if (flat(q, middle) < nearest) {
                    nearest = flat(q, middle);
                    inside = q;
                }
            }
        } else {
            inside = map.reached_inside(hold->min.x + 1.0f, hold->min.z + 1.0f, hold->max.x - 1.0f, hold->max.z - 1.0f);
        }
        if (inside) {
            go(*inside, nullptr, "hold the wave of " + hold->name);
            return;
        }
    }
    // A wave stopped by leaving its mat after all of it spawned and with
    // none of it left (the last enemy fought down off the mat: dng412b3's
    // GENE-23 e_mecha4 down the platform's south ramp) clears only once a
    // player stands on that mat again (its script STARTs it, 802FD5C4; a
    // stopped generator finishes nothing, 802FD174..802FD194), and its door with it:
    // back onto the mat, for up to 6000 frames per wave.
    if (auto* generators = engine.get<engine::GeneraterManager>())
        for (const auto& status : generators->status()) {
            if (!status.stopped || !status.filled || status.done || status.alive > 0 || status.clears >= 2) continue;
            auto& left = rejoin_left_.try_emplace(status.name, 6000).first->second;
            left -= rejoin_gen_ == status.name ? std::min(45, frame - rejoin_since_) : 0;
            if (left <= 0) continue;
            for (const auto& box : boxes) {
                if (!gen_mats_[status.name].count(box.name)) continue;
                if (const auto skip = mat_skip_.find(box.name); skip != mat_skip_.end() && frame < skip->second) continue;
                const std::string script = box.name + ".lua";
                if (!engine.script_mentions(script, "START") || !engine.script_mentions(script, status.name)) continue;
                const auto inside = map.reached_inside(box.min.x + 1.0f, box.min.z + 1.0f, box.max.x - 1.0f, box.max.z - 1.0f);
                if (!inside || inside->y < box.min.y - 5.0f || inside->y > box.max.y + 5.0f) continue;
                rejoin_gen_ = status.name;
                rejoin_since_ = frame;
                go(*inside, nullptr, "rejoin the stopped wave " + status.name + " on " + box.name);
                target_mat_ = box.name;
                return;
            }
        }
    rejoin_gen_.clear();
    // Gimmicks with a hit script: floor switches (an AIM map, no bullet
    // boxes: stood on while their AI is live), wall switches (a talk box:
    // A facing them) and things that take hits (boxes, breakable walls:
    // attacked). Each gets a budget; switches are tried again later. Untried
    // gimmicks come before the mats, tried ones after them.
    // How close to a gimmick's origin the walk must get: a talk box may reach
    // far out of its model (staggf03's console g_covercnsz05 stands 17 units
    // behind the railing at x 30, its box reaches over it to x 34): then
    // within 10 of the box's far edge counts (the player's talk box sits
    // 8.5 ahead).
    const auto gimmick_tolerance = [](const engine::ModelCtrlManager::ModelRam& e, Gimmick kind) {
        if (kind == Gimmick::step) return 6.0f;
        if (kind == Gimmick::talk && box_reach(e) > kFarTalk) return box_reach(e) + 10.0f;
        return 20.0f;
    };
    auto pick_gimmick = [&](bool tried) {
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        Gimmick best_kind = Gimmick::none;
        float best_cost = std::numeric_limits<float>::infinity();
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node || !e->control_enabled || e->hp <= 0 || e->info_name.rfind("g_", 0) != 0) continue;
            if (e->category == "ITEM" || e->category == "ENEMY" || e->category == "PLAYER") continue;
            if (!engine.has_script("hit_" + e->info_name + ".lua")) continue;
            const bool boxes = e->hit && !e->hit->boxes.empty();
            Gimmick kind = Gimmick::none;
            if (e->ai_maps && !e->ai_maps->empty() && !(boxes && (e->hit->player_bullet || e->hit->player_talk))) {
                if (e->ctrl_mode == 0) continue; // switched on: its AI is no longer live
                kind = Gimmick::step;
            } else if (boxes && e->hit->player_talk) {
                if (switched_on(*e)) continue;
                kind = Gimmick::talk;
            } else if (boxes && e->hit->player_bullet) {
                // Walls that break only to an element (fire, thunder): struck
                // when the player's attack (a power pocket chip) has it,
                // otherwise hit with the first technique (R panel, A).
                kind = Gimmick::strike;
                const std::string hit = "hit_" + e->info_name + ".lua";
                if (engine.script_mentions(hit, "BLPgetAttackZokusei")) {
                    static constexpr const char* elements[] = {"fire", "ice", "thunder", "dark"};
                    int wanted = -1;
                    for (int i = 0; i < 4 && wanted < 0; ++i)
                        if (engine.script_mentions(hit, elements[i])) wanted = i;
                    int own = -1, first = -1;
                    if (auto* sys = engine.get<engine::GameSystem>()) {
                        const auto& slot = sys->players[std::size_t(std::clamp(player.index, 0, 3))];
                        own = slot.stats.attack_zokusei;
                        // The first technique of the panel is the one cast.
                        if (auto* db = engine.get<engine::DatabaseManager>(); db && !slot.wazalist.empty()) {
                            const auto element = db->text("pcTech", slot.wazalist[0], "ZOKUSEI");
                            for (int i = 0; i < 4 && first < 0; ++i)
                                if (element == elements[i]) first = i;
                        }
                    }
                    if (wanted < 0 || own != wanted) {
                        if (wanted < 0 || first != wanted) continue; // nothing this player has breaks it
                        kind = Gimmick::tech;
                    }
                }
            } else {
                continue;
            }
            // Without the test cheats a wall that stopped a live turret's
            // shots is left standing (shields_): the player fights on
            // behind it.
            if (kind == Gimmick::strike && !engine.cheats().one_hit && !engine.cheats().god)
                if (const auto shield = shields_.find(e.get()); shield != shields_.end()) {
                    const auto* turret = static_cast<const engine::ModelCtrlManager::ModelRam*>(shield->second);
                    if (turret->generated && turret->hp > 0) continue;
                }
            // A switch seen switched on is done for a long while (pairs that
            // power each other back on would otherwise take turns forever).
            if (kind != Gimmick::strike) {
                if (switches_done_.count(e.get())) continue;
                if (const auto on = switched_.find(e.get()); on != switched_.end() && frame - on->second < 6000) continue;
            }
            const int budget = kind == Gimmick::strike ? 240 : kind == Gimmick::talk ? 12 : kind == Gimmick::tech ? 3 : 60;
            const auto spent = struck_.find(e.get());
            if (spent != struck_.end() && spent->second >= budget) {
                if (kind == Gimmick::strike || kind == Gimmick::tech || frame - struck_at_[e.get()] < 900) continue;
                // A wall switch that went on before and now takes its presses
                // without going on again (its script ignores it once its flag
                // is set; the model fell back to SQ_WAIT) is done.
                if (kind == Gimmick::talk && switched_.count(e.get())) {
                    switches_done_.insert(e.get());
                    continue;
                }
                spent->second = 1; // tried again later, after the mats
            }
            // A wall that was started on is finished first (it takes several hits).
            const bool was_tried = kind != Gimmick::strike && struck_.count(e.get()) && struck_[e.get()] > 0 && e.get() != previous;
            if (was_tried != tried) continue;
            if (left_alone(e.get())) continue;
            const auto at = position_of(*e);
            if (kind == Gimmick::strike && near_healer(at)) continue; // (switches are worked all the same)
            const float tolerance = gimmick_tolerance(*e, kind);
            float cost = keep(e.get(), map.cost_to(at, tolerance));
            // A wall being broken (or walked to) is finished first.
            if (kind == Gimmick::strike && (struck_.count(e.get()) || e.get() == previous)) cost *= 0.3f;
            // A switch whose script also switches walls back on may lock a
            // door behind the player: last.
            if (kind != Gimmick::strike && engine.script_mentions("hit_" + e->info_name + ".lua", "ONHIT")) cost += 400.0f;
            if (cost < best_cost) {
                best_cost = cost;
                best = e.get();
                best_kind = kind;
            }
        }
        if (!best) return false;
        static constexpr const char* kinds[] = {"", "strike ", "press ", "step on ", "cast at "};
        go(position_of(*best), best, std::string(kinds[int(best_kind)]) + best->info_name + "#" + std::to_string(best->index));
        route_ = map.route_to(position_of(*best), gimmick_tolerance(*best, best_kind));
        gimmick_ = best_kind;
        return true;
    };
    // The mat being walked to is kept while it is open and reachable (a far
    // gimmick at the edge of the planning window would take turns with it).
    if (!previous_mat.empty() && !mats_done_.count(previous_mat))
        if (const auto skip = mat_skip_.find(previous_mat); skip == mat_skip_.end() || frame >= skip->second)
            for (const auto& box : boxes)
                if (box.name == previous_mat && std::isfinite(map.cost_to(mat_centre(box, me.y), 6.0f))) {
                    go(mat_centre(box, me.y), nullptr, "mat " + box.name);
                    target_mat_ = box.name;
                    return;
                }
    if (pick_gimmick(false)) return;
    if (radar(true)) return;
    // Mats not stood on yet (they start enemy waves and events).
    const engine::MatCtrlManager::MatBox* best_mat = nullptr;
    float best_cost = std::numeric_limits<float>::infinity();
    for (const auto& box : boxes) {
        const auto key = mat_key(box.name);
        // A wave mat (matgNN starting GENE-NN) stood on before the last
        // switch or first clear whose generator never started (its flags may
        // be set now) is open again.
        if (mats_done_.count(box.name) && world_change_ > mat_done_at_[box.name] && box.name.rfind("matg", 0) == 0 && box.name.size() > 4)
            if (auto* generators = engine.get<engine::GeneraterManager>()) {
                const std::string gene = "GENE-" + box.name.substr(4);
                for (const auto& status : generators->status())
                    if (status.name == gene && !status.started && !status.done && status.clears == 0 &&
                        engine.script_mentions(box.name + ".lua", gene))
                        mats_done_.erase(box.name);
            }
        if (key.empty() || goals.count(key) || exit_mat(engine, box.name) || portal(engine, box.name) || hazard_mat(engine, box.name) ||
            quicksand_mat(engine, box.name) || mats_done_.count(box.name) || (!avoid_mats_.empty() && box.name.rfind(avoid_mats_, 0) == 0))
            continue;
        if (const auto skip = mat_skip_.find(box.name); skip != mat_skip_.end() && frame < skip->second) continue;
        // (A mat over a pit that only a moving platform's path crosses is no
        // place to stand on.)
        if (!map.stage_floor_near(mat_centre(box, me.y), 6.0f) && !map.reached_inside(box.min.x, box.min.z, box.max.x, box.max.z, true)) continue;
        float cost = map.cost_to(mat_centre(box, me.y), 6.0f);
        if (box.name == previous_mat) cost *= 0.6f;
        if (mats_ever_.count(box.name)) cost += 100000.0f; // never stood on first
        if (cost < best_cost) {
            best_cost = cost;
            best_mat = &box;
        }
    }
    if (best_mat) {
        go(mat_centre(*best_mat, me.y), nullptr, "mat " + best_mat->name);
        target_mat_ = best_mat->name;
        return;
    }
    if (radar(false)) return;
    if (pick_gimmick(true)) return;
    // Unvisited ground, preferring the goal's side; the point being walked
    // to is kept until it is reached or given up.
    std::optional<engine::Vec3f> frontier;
    if (frontier_ && !visited_.count(coarse(frontier_->x, frontier_->z)) && map.nearest(*frontier_, 6.0f)) frontier = frontier_;
    float best_score = 1e9f;
    for (const auto& q : map.samples()) {
        if (frontier) break;
        if (visited_.count(coarse(q.x, q.z))) continue;
        const float score = flat(q, me) + (goal_centre ? 0.25f * flat(q, *goal_centre) : 0.0f);
        if (score < best_score) {
            best_score = score;
            frontier = q;
        }
    }
    frontier_ = frontier;
    if (frontier) {
        go(*frontier, nullptr, "explore");
        return;
    }
    // The room is done and the goal is not here: take a maze link (a mat
    // that moves the player to another room of the stage).
    if (!goal_inside) {
        const engine::MatCtrlManager::MatBox* best_link = nullptr;
        float best_link_cost = std::numeric_limits<float>::infinity();
        for (const auto& box : boxes) {
            if (!portal(engine, box.name) || portals_taken_.count(box.name)) continue;
            const float cost = map.cost_to(mat_centre(box, me.y), 6.0f);
            if (cost < best_link_cost) {
                best_link_cost = cost;
                best_link = &box;
            }
        }
        if (best_link) {
            go(mat_centre(*best_link, me.y), nullptr, "link " + best_link->name);
            target_portal_ = best_link->name;
            return;
        }
    }
    // Everything seen: forget the visited ground and mats once in a while, so
    // waves stopped by leaving their mat are started again and opened doors
    // are walked through.
    if (frame - last_reset_ > 3000) {
        last_reset_ = frame;
        visited_.clear();
        mats_done_.clear();
        mat_frames_.clear();
        // Targets given up for a moment are tried again, not those left
        // alone for taking no damage.
        std::erase_if(unreachable_, [&](const auto& entry) { return entry.second - frame < 2000; });
        // Nor turrets whose posts were given up for the stage: one may stand
        // in the only way on (its body filling it), so the stage is not done
        // without it (from now on its walk to the post counts a quarter).
        for (const void* turret : unreached_) {
            unkillable_.erase(turret);
            unreachable_.erase(turret);
            fruitless_runs_.erase(turret);
            approach_.erase(turret);
            forgiven_.insert(turret);
            if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore tries a given-up post again\n", frame);
        }
        unreached_.clear();
        planned_ = -1000;
        what_ = "forget the explored ground";
        return;
    }
    if (goal_centre) {
        // Wait at the nearest reached point (walking on at the centre would
        // step off a pillar into the pit around it).
        go(*goal_centre, nullptr, "goal " + goal + " (closest)");
        goal_point_ = route_.empty() ? me : route_.back().point;
        closest_ = true;
    }
    // Nothing left to explore: what stops the walk (DW4_TRACE_EXPLORE).
    if (std::getenv("DW4_TRACE_EXPLORE") && frame % 600 < 45) {
        const auto samples = map.samples();
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
        for (const auto& q : samples) {
            x0 = std::min(x0, q.x);
            x1 = std::max(x1, q.x);
            z0 = std::min(z0, q.z);
            z1 = std::max(z1, q.z);
        }
        for (const auto& [key, blocker] : map.blockers())
            std::printf("      blocked by info %d child %d: %d first at (%.1f,%.1f,%.1f) normal (%.2f,%.2f,%.2f)\n", key.first, key.second,
                        blocker.count, blocker.first.x, blocker.first.y, blocker.first.z, blocker.normal.x, blocker.normal.y, blocker.normal.z);
        std::printf("      %zu reachable samples in (%.0f..%.0f, %.0f..%.0f), %zu visited cells\n", samples.size(), x0, x1, z0, z1, visited_.size());
        for (const auto& box : boxes)
            std::printf("      mat %s (%.0f..%.0f, %.0f..%.0f) %s%s\n", box.name.c_str(), box.min.x, box.max.x, box.min.z, box.max.z,
                        mats_done_.count(box.name) ? "done" : "open", map.nearest(mat_centre(box, me.y), 6.0f) ? " reachable" : "");
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category != "PLAYER" && e->category != "INTERFACE")
                std::printf("      model %s#%d %s hp %d (%.0f,%.0f,%.0f)%s%s reach %.1f gap %.1f\n", e->info_name.c_str(), e->index, e->category.c_str(),
                            e->hp, e->node->matrix(3, 0), e->node->matrix(3, 1), e->node->matrix(3, 2),
                            e->hit && e->hit->player_bullet ? " bullet" : "", e->hit && e->hit->player_talk ? " talk" : "", box_reach(*e),
                            body_gap(*e, me));
    }
}

// Enemy bullets are models with hit boxes of the enemy kinds (kind / 10000
// 7 or 8, CHKBULLET to the players) started from an enemy (the effect
// chain's source, 8030D260). Only turrets count (structures, g_ enemies,
// standing still: the fire of one that walks about leaves no lane, and
// ordinary enemies only pause to shoot), and only boxes more than
// 12 units from it (a lane, not a blow). Each box marks the cells under it,
// grown by the player's half width and a margin, with the floor heights at
// which the player's body (7 high) meets it. A bullet seen for the first
// time (or far from where it was: a RAM used again) is a shot.
void ExploreBot::track_fire(engine::Engine& engine, int frame) {
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!models) return;
    // The stored models are read later (posts, the turret in front): keep
    // only those still alive (a removed model, or a state load that builds a
    // new engine on the same stage, leaves stale keys).
    // (A sorted list, rebuilt when the RAM list changes: this runs every step.)
    if (alive_generation_ != models->rams_generation()) {
        alive_.clear();
        for (const auto& e : models->rams()) alive_.push_back(e.get());
        std::sort(alive_.begin(), alive_.end());
        alive_generation_ = models->rams_generation();
    }
    const auto& alive = alive_;
    const auto is_alive = [&](const void* p) { return std::binary_search(alive.begin(), alive.end(), p); };
    const auto dead = [&](const auto& entry) { return !is_alive(entry.first); };
    std::erase_if(shots_, dead);
    std::erase_if(standing_, dead);
    std::erase_if(bullet_speed_, dead);
    std::erase_if(turret_band_, dead);
    for (auto& [key, cell] : fire_cells_)
        if (cell.shooter && !is_alive(cell.shooter)) cell.shooter = nullptr;
    std::map<const void*, std::pair<engine::Vec3f, const void*>> live;
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->is_player) continue;
        const auto at = position_of(*e);
        if (e->category == "ENEMY") {
            auto [stand, fresh] = standing_.try_emplace(e.get(), at, frame);
            if (flat(stand->second.first, at) > 1.0f) stand->second = {at, frame};
            continue;
        }
        // A bullet in SQ_ERAS keeps its boxes until its REMOVE next step
        // (0x8030E884): it is gone already, no fire to wait for.
        if (e->seq_name.rfind("SQ_ERAS", 0) == 0) continue;
        bool enemy_fire = false;
        for (const auto& h : e->hitboxes) enemy_fire = enemy_fire || h.kind / 10000 == 7 || h.kind / 10000 == 8;
        if (!enemy_fire) continue;
        engine::ModelCtrlManager::ModelRam* shooter = nullptr;
        if (!e->attach_name.empty()) shooter = models->find(e->attach_name, e->attach_index);
        if (!shooter && !e->owner_name.empty()) shooter = models->find(e->owner_name, e->owner_index);
        if (!shooter || shooter->category != "ENEMY" || !shooter->node || shooter->info_name.rfind("g_", 0) != 0) continue;
        const auto stand = standing_.find(shooter);
        if (stand == standing_.end() || frame - stand->second.second < 60) continue;
        const auto from = position_of(*shooter);
        if (const auto last = bullets_.find(e.get()); last == bullets_.end() || flat(last->second.first, at) > 20.0f) {
            // (Bullets started within a few steps are one shot.)
            auto& shots = shots_[shooter];
            if (shots.empty() || frame - shots.back() > 4) shots.push_back(frame);
            if (shots.size() > 12) shots.erase(shots.begin());
        } else {
            auto& speed = bullet_speed_[shooter];
            speed = std::max(speed * 0.999f, flat(last->second.first, at));
        }
        live[e.get()] = {at, shooter};
        const auto& m = e->node->matrix;
        for (const auto& h : e->hitboxes) {
            if ((h.kind / 10000 != 7 && h.kind / 10000 != 8) || (!h.started && h.delay - h.elapsed > 20.0f)) continue;
            // As fire_hitbox: the authored offset through the model's matrix, the size as full extents.
            const engine::Vec3f c{h.offset.x * m(0, 0) + h.offset.y * m(1, 0) + h.offset.z * m(2, 0) + m(3, 0),
                                  h.offset.x * m(0, 1) + h.offset.y * m(1, 1) + h.offset.z * m(2, 1) + m(3, 1),
                                  h.offset.x * m(0, 2) + h.offset.y * m(1, 2) + h.offset.z * m(2, 2) + m(3, 2)};
            if (flat(c, from) < 12.0f) continue;
            const float grow = 3.5f;
            const int x0 = int(std::lround((c.x - h.size.x * 0.5f - grow) / Grid::step)), x1 = int(std::lround((c.x + h.size.x * 0.5f + grow) / Grid::step));
            const int z0 = int(std::lround((c.z - h.size.z * 0.5f - grow) / Grid::step)), z1 = int(std::lround((c.z + h.size.z * 0.5f + grow) / Grid::step));
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    auto& cell = fire_cells_[{x, z}];
                    if (frame - cell.frame > 150 || !cell.shooter)
                        cell = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), frame, nullptr};
                    cell.lo = std::min(cell.lo, c.y - h.size.y * 0.5f - 7.5f);
                    cell.hi = std::max(cell.hi, c.y + h.size.y * 0.5f + 0.5f);
                    cell.frame = frame;
                    cell.shooter = shooter;
                }
            auto [band, fresh] = turret_band_.try_emplace(shooter, c.y - h.size.y * 0.5f - 7.5f, c.y + h.size.y * 0.5f + 0.5f);
            band->second = {std::min(band->second.first, c.y - h.size.y * 0.5f - 7.5f), std::max(band->second.second, c.y + h.size.y * 0.5f + 0.5f)};
        }
    }
    // The steady view for crossings that start with a jump into the fire: a
    // bullet seen the step before keeps counting while its box list is empty
    // (a box re-armed: still generated, near where it was, not erasing), and
    // only a bullet new to it is a shot, placed in its turret's sequence.
    std::erase_if(volleys_, dead);
    std::erase_if(phases_, dead);
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->category != "ENEMY" || e->info_name.rfind("g_", 0) != 0) continue;
        auto [entry, fresh] = phases_.try_emplace(e.get());
        auto& phase = entry->second;
        // (First seen in the middle of a sequence: its start is not known.)
        if (fresh) {
            phase.serial = e->sequence_serial;
            phase.seq = e->seq_name;
            continue;
        }
        if (phase.serial == e->sequence_serial) continue;
        // (A run of the firing sequence that ended: its length.)
        if (phase.start >= 0 && !phase.fire_seq.empty() && phase.seq == phase.fire_seq) phase.length = std::max(phase.length, frame - phase.start);
        phase.serial = e->sequence_serial;
        phase.seq = e->seq_name;
        phase.start = frame;
    }
    std::map<const void*, std::pair<engine::Vec3f, const void*>> steady;
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->is_player) continue;
        const auto at = position_of(*e);
        const auto before = steady_bullets_.find(e.get());
        const bool known = before != steady_bullets_.end() && flat(before->second.first, at) <= 20.0f;
        if (const auto seen = live.find(e.get()); seen != live.end()) {
            steady[e.get()] = seen->second;
            if (!known) {
                shot_born_[e.get()] = frame;
                auto& shots = volleys_[seen->second.second];
                if (shots.empty() || frame - shots.back() > 4) {
                    shots.push_back(frame);
                    if (const auto phase = phases_.find(seen->second.second); phase != phases_.end() && phase->second.start >= 0) {
                        auto& ph = phase->second;
                        if (ph.fire_seq.empty()) ph.fire_seq = ph.seq;
                        const int offset = frame - ph.start;
                        bool seen_offset = false;
                        for (const int o : ph.offsets) seen_offset = seen_offset || std::abs(o - offset) <= 3;
                        if (ph.seq == ph.fire_seq && !seen_offset && ph.offsets.size() < 8) {
                            ph.offsets.push_back(offset);
                            std::sort(ph.offsets.begin(), ph.offsets.end());
                        }
                    }
                }
                if (shots.size() > 12) shots.erase(shots.begin());
            }
        } else if (known && e->hitboxes.empty() && e->seq_name.rfind("SQ_ERAS", 0) != 0 && is_alive(before->second.second)) {
            steady[e.get()] = {at, before->second.second};
        }
    }
    // Shots that ended away from the player at a gimmick's body (a bullet
    // of the steady view that starts erasing, or is gone, or whose RAM a new
    // shot took far off): that gimmick shields the ground behind it from the
    // turret (stag0204: the rock walls g_rock02z02 along x 530 stop the
    // g_cannon02 shells short of the corridor the player breaks through; a
    // no-cheat run that broke them took the shells all the way down the
    // corridor, 15 % of the maximum HP each). See shields_.
    if (const auto* me_ram = models->player(0); me_ram && me_ram->node) {
        const auto me = position_of(*me_ram);
        // (The flights that ended this frame raise the turrets' ranges only
        // after all of them are judged: the same in any order.)
        std::vector<std::pair<const void*, int>> flights;
        const auto ended_at = [&](const engine::Vec3f& end, const void* shooter, int age) {
            // (Only a shot cut short counts: one at the end of its flight
            // may end next to anything, stag0204's corridor rocks among
            // them. A turret's longest flight so far is its shots' range.)
            const auto known = shot_life_.find(shooter);
            const int life = known != shot_life_.end() ? known->second : 0;
            const bool short_flight = life >= 30 && age >= 0 && age < life - 8;
            flights.emplace_back(shooter, age);
            if (!short_flight || flat(end, me) < 15.0f) return; // (not at the player or its guard)
            for (const auto& g : models->rams()) {
                if (!g->generated || !g->node || g->hp <= 0 || g->info_name.rfind("g_", 0) != 0 || !g->hit || g->hit->boxes.empty()) continue;
                if (g->category == "ENEMY" || g->category == "ITEM" || g->category == "PLAYER") continue;
                if (body_gap(*g, end) > 6.0f || end.y < body_bottom(*g) - 6.0f || end.y > body_top(*g) + 6.0f) continue;
                if (std::getenv("DW4_TRACE_EXPLORE") && !shields_.count(g.get()))
                    std::printf("[%d] explore: %s#%d stops the shots of %s (%.0f,%.0f,%.0f)\n", frame, g->info_name.c_str(), g->index,
                                static_cast<const engine::ModelCtrlManager::ModelRam*>(shooter)->info_name.c_str(), end.x, end.y, end.z);
                shields_[g.get()] = shooter;
            }
        };
        for (const auto& [shot, last] : steady_bullets_) {
            if (!is_alive(last.second)) continue;
            const auto now = steady.find(shot);
            if (now != steady.end() && flat(now->second.first, last.first) <= 20.0f) continue;
            const auto* e = static_cast<const engine::ModelCtrlManager::ModelRam*>(shot);
            const bool erasing = is_alive(shot) && e->generated && e->node && e->seq_name.rfind("SQ_ERAS", 0) == 0;
            const auto born = shot_born_.find(shot);
            // (A RAM a new shot took this frame: that shot's birth, the old one's age unknown.)
            const int age = born != shot_born_.end() && born->second != frame ? frame - born->second : -1;
            ended_at(erasing ? position_of(*e) : last.first, last.second, age);
            if (born != shot_born_.end() && now == steady.end()) shot_born_.erase(born);
        }
        for (const auto& [shooter, age] : flights) shot_life_[shooter] = std::max(shot_life_[shooter], age);
    }
    std::erase_if(shields_, [&](const auto& entry) { return !is_alive(entry.first) || !is_alive(entry.second); });
    std::erase_if(shot_born_, [&](const auto& entry) { return !is_alive(entry.first); });
    std::erase_if(shot_life_, [&](const auto& entry) { return !is_alive(entry.first); });
    steady_bullets_ = std::move(steady);
    bullets_ = std::move(live);
    if (frame % 30 == 0) std::erase_if(fire_cells_, [&](const auto& entry) { return frame - entry.second.frame > 150; });
}

bool ExploreBot::in_fire(const engine::Vec3f& p, int frame) const {
    const auto it = fire_cells_.find({int(std::lround(p.x / Grid::step)), int(std::lround(p.z / Grid::step))});
    return it != fire_cells_.end() && frame - it->second.frame <= 150 && p.y >= it->second.lo && p.y <= it->second.hi;
}

void ExploreBot::give_up(int frame) {
    platform_wait_since_ = -1;
    // The way to the goal leads nowhere (a gate in front of it not open
    // yet): the rest first for a while.
    if (to_goal_) {
        goal_first_failed_ = true;
        goal_later_until_ = frame + 3000;
    }
    if (target_) unreachable_[target_] = frame + 1800;
    if (!target_mat_.empty()) mat_skip_[target_mat_] = frame + 3000;
    if (!target_portal_.empty()) portals_taken_.insert(target_portal_);
    if (!target_radar_.empty()) radar_skip_[target_radar_] = frame + 3000;
    if (what_ == "explore") {
        visited_.insert(coarse(goal_point_.x, goal_point_.z));
        frontier_.reset();
    }
    planned_ = -1000;
}

ExploreBot::Decision ExploreBot::update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal,
                                        int frame) {
    Decision decision;
    const auto me = position_of(player);
    decision.x = me.x;
    decision.z = me.z;
    if (engine.stage_name() != stage_) {
        stage_ = engine.stage_name();
        visited_.clear();
        mats_done_.clear();
        unreachable_.clear();
        struck_.clear();
        struck_at_.clear();
        mat_frames_.clear();
        frontier_.reset();
        last_reset_ = -100000;
        planned_ = -1000;
        progress_at_ = frame;
        defend_until_ = -1;
        goal_later_until_ = -1;
        on_goal_ = 0;
        model_last_.clear();
        portals_.clear();
        exits_.clear();
        hazards_.clear();
        quicksands_.clear();
        traps_.clear();
        beams_.clear();
        beams_at_ = -100000;
        clears_seen_.clear();
        hold_left_.clear();
        hold_mat_.clear();
        rejoin_gen_.clear();
        rejoin_since_ = -1;
        rejoin_left_.clear();
        switched_.clear();
        health_.clear();
        lowest_hp_.clear();
        fruitless_.clear();
        fruitless_runs_.clear();
        approach_.clear();
        unkillable_.clear();
        regenerating_.clear();
        vanished_.clear();
        enemy_last_.clear();
        enemy_hp_.clear();
        kill_credits_ = 0;
        child_boxes_.clear();
        switches_done_.clear();
        radar_skip_.clear();
        mat_skip_.clear();
        mats_ever_.clear();
        mat_done_at_.clear();
        world_change_ = -1;
        gen_started_.clear();
        gen_mats_.clear();
        goal_first_failed_ = false;
        mat_sequence_.clear();
        mat_sequence_next_ = 0;
        if (const char* list = std::getenv("DW4_MAT_SEQUENCE"))
            for (const char* at = list; *at;) {
                const char* end = at;
                while (*end && *end != ',') ++end;
                if (end != at) mat_sequence_.emplace_back(at, end);
                at = *end ? end + 1 : end;
            }
        avoid_mats_ = std::getenv("DW4_AVOID_MATS") ? std::getenv("DW4_AVOID_MATS") : "";
        portals_taken_.clear();
        cleared_generators_.clear();
        watch_.clear();
        fire_cells_.clear();
        shots_.clear();
        bullets_.clear();
        bullet_speed_.clear();
        turret_band_.clear();
        steady_bullets_.clear();
        shields_.clear();
        shot_born_.clear();
        shot_life_.clear();
        volleys_.clear();
        phases_.clear();
        unreached_.clear();
        forgiven_.clear();
        standing_.clear();
        perch_skip_.clear();
        fire_wait_since_ = fire_go_until_ = -1;
        platforms_.clear();
        stirring_.clear();
        not_platforms_.clear();
        off_ride_.clear();
        hop_.reset();
        hop_wait_since_ = -1;
        hop_spots_for_ = {1e30f, 0.0f, 1e30f};
        hop_wait_for_ = {1e30f, 0.0f, 1e30f};
    }
    track_fire(engine, frame);
    track_platforms(engine, me, frame);
    // Moved to another room of the stage (a maze link): a fresh start there.
    // (A fall respawn keeps what was done.)
    if (flat(me, last_) > 100.0f && frame > 1 && !target_portal_.empty()) {
        portals_taken_.insert(target_portal_);
        visited_.clear();
        mats_done_.clear();
        mat_frames_.clear();
        unreachable_.clear();
        frontier_.reset();
        watch_.clear();
        planned_ = -1000;
        progress_at_ = frame;
    }
    // The mats the player stands in when a generator starts (the waves a mat
    // holds).
    if (auto* generators = engine.get<engine::GeneraterManager>())
        for (const auto& status : generators->status()) {
            bool& was = gen_started_[status.name];
            if (status.started && !was) {
                auto& in = gen_mats_[status.name];
                in.clear();
                if (auto* mats = engine.get<engine::MatCtrlManager>())
                    for (const auto& box : mats->mat_boxes())
                        if (me.x >= box.min.x && me.x <= box.max.x && me.z >= box.min.z && me.z <= box.max.z) in.insert(box.name);
            }
            was = status.started;
        }
    // A generator cleared for the first time is progress.
    if (frame % 30 == 0)
        if (auto* generators = engine.get<engine::GeneraterManager>()) {
            for (const auto& [name, done] : generators->cleared())
                if (done && cleared_generators_.insert(name).second) {
                    progress_at_ = frame;
                    kill_credits_ = 0;
                    world_change_ = frame;
                }
            // A wave cleared again was restarted (until something else is
            // done, e.g. a tower destroyed): stop chasing it for a while.
            for (const auto& status : generators->status()) {
                auto& seen = clears_seen_[status.name];
                if (status.clears > seen && status.clears >= 2 && frame >= defend_until_) {
                    defend_until_ = frame + 2400;
                    planned_ = -1000;
                }
                seen = status.clears;
            }
        }
    // A defeated enemy nearby is progress (a wave whose defeated come back
    // until its count is done is not left for defend mode), at most 16 times
    // between newly cleared generators so endless waves still are.
    if (auto* all = engine.get<engine::ModelCtrlManager>())
        for (const auto& e : all->rams()) {
            // An enemy RAM taken by a new spawn (a generator's FIND_SPACE
            // reuses a defeated enemy's RAM): the marks of the defeated one
            // (lowest HP, no damage, left alone) go, or the new one at full
            // HP would count as healing itself and soon be left alone for
            // the stage (dng206f2's minotaur waves in a god-mode campaign
            // run: GENE-25 and GENE-28 never cleared). Not with the one-hit
            // cheat (the recorded runs keep their path).
            if (!engine.cheats().one_hit && e->category == "ENEMY") {
                const void* key = e.get();
                if (!e->generated || !e->node || e->hp <= 0) {
                    vanished_.insert(key);
                } else if (vanished_.erase(key)) {
                    lowest_hp_.erase(key);
                    fruitless_.erase(key);
                    fruitless_runs_.erase(key);
                    regenerating_.erase(key);
                    unkillable_.erase(key);
                    unreachable_.erase(key);
                    approach_.erase(key);
                    enemy_last_.erase(key);
                }
            }
            if (!e->generated || !e->node || e->category != "ENEMY") continue;
            auto [hp, fresh] = enemy_hp_.emplace(e.get(), e->hp);
            if (!fresh && hp->second > 0 && e->hp <= 0 && flat(position_of(*e), me) < 80.0f && kill_credits_ < 16) {
                ++kill_credits_;
                progress_at_ = frame;
            }
            hp->second = e->hp;
        }
    if (mat_sequence_next_ < mat_sequence_.size())
        if (auto* mats = engine.get<engine::MatCtrlManager>())
            for (const auto& box : mats->mat_boxes())
                if (box.name == mat_sequence_[mat_sequence_next_] && me.x >= box.min.x && me.x <= box.max.x && me.z >= box.min.z &&
                    me.z <= box.max.z && me.y >= box.min.y - 2.0f && me.y <= box.max.y + 2.0f) {
                    if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore stood on sequence mat %s\n", frame, box.name.c_str());
                    ++mat_sequence_next_;
                    planned_ = -1000;
                    progress_at_ = frame;
                    break;
                }
    const auto visited_before = visited_.size();
    for (const float ox : {-8.0f, 8.0f})
        for (const float oz : {-8.0f, 8.0f}) visited_.insert(coarse(me.x + ox, me.z + oz));
    if (visited_.size() != visited_before && what_.rfind("enemy", 0) != 0) progress_at_ = frame;
    auto* models = engine.get<engine::ModelCtrlManager>();
    // A mat counts as stood on near its centre or after a while inside (a
    // radar-started wave needs the player well inside).
    if (auto* mats = engine.get<engine::MatCtrlManager>())
        for (const auto& box : mats->mat_boxes())
            if (me.x >= box.min.x && me.x <= box.max.x && me.z >= box.min.z && me.z <= box.max.z) {
                if ((++mat_frames_[box.name] > 150 || flat(mat_centre(box, me.y), me) < 15.0f) && mats_done_.insert(box.name).second) {
                    progress_at_ = frame;
                    mats_ever_.insert(box.name);
                    mat_done_at_[box.name] = frame;
                }
            }
    // The planned enemy, item or gimmick must still be there (and a switch
    // still off).
    const engine::ModelCtrlManager::ModelRam* target = nullptr;
    if (target_ && models)
        for (const auto& e : models->rams())
            if (e.get() == target_ && e->generated && e->node && (e->category == "ITEM" || e->hp > 0) &&
                !(gimmick_ == Gimmick::step && e->ctrl_mode == 0) && !(gimmick_ == Gimmick::talk && switched_on(*e)))
                target = e.get();
    if (target_ && models && (gimmick_ == Gimmick::talk || gimmick_ == Gimmick::step))
        for (const auto& e : models->rams())
            if (e.get() == target_ && ((gimmick_ == Gimmick::talk && switched_on(*e)) || (gimmick_ == Gimmick::step && e->ctrl_mode == 0))) {
                switched_[target_] = frame;
                world_change_ = frame;
                // A switch may open the way to a goal given up before, and
                // only for a while (stag0202's floor button lowers the boss
                // symbols in front of matz02): head for the goal first again.
                goal_later_until_ = -1;
                goal_first_failed_ = false;
            }
    if (target && gimmick_ == Gimmick::strike) {
        // It takes damage: keep at it. Only a new low counts, so one that
        // heals itself back up does not restart its budget forever.
        auto [it, fresh] = health_.emplace(target_, target->hp);
        if (!fresh && target->hp < it->second) {
            struck_[target_] = 1;
            it->second = target->hp;
        }
    }
    if (target_ && !target) {
        lowest_hp_.erase(target_);
        fruitless_.erase(target_);
    }
    if (target_ && !target && gimmick_ != Gimmick::none) {
        // A switch went on (or a gimmick broke): on to the next, e.g. the
        // other switch of a pair that must be pressed within moments.
        // (A wall switch that went on gets a fresh budget: if it falls back
        // off, e.g. a pair pressed too slowly, it is pressed again later.)
        struck_[target_] = gimmick_ == Gimmick::strike ? 240 : gimmick_ == Gimmick::talk ? 0 : 60;
        struck_at_[target_] = frame;
        progress_at_ = frame;
    }
    stalled_ = std::hypot(me.x - last_.x, me.z - last_.z) < 0.1f && !held_by_hit(player) ? stalled_ + 1 : 0;
    last_ = me;
    // In the air after a jump: keep steering at the landing (on_ground stays
    // set while a jump rises, the sequence tells).
    const bool airborne = !player.on_ground || player.seq_name.rfind("SQ_JUMP", 0) == 0 || player.seq_name == "SQ_FALL";
    if (leap_ && frame - leap_frame_ < 70 && (airborne || frame - leap_frame_ < 5)) {
        decision.x = leap_->x;
        decision.z = leap_->z;
        decision.reach = 0.25f;
        return decision;
    }
    leap_.reset();
    // A long fight without progress elsewhere (waves that come back until
    // something else is done): fight only nearby enemies for a while.
    // (Only once kills no longer count as progress: a single tough enemy,
    // e.g. an arena knight, is not left alone.)
    if (frame - progress_at_ > 2400 && frame >= defend_until_ && what_.rfind("enemy", 0) == 0 &&
        (kill_credits_ >= 16 || frame - progress_at_ > 6000)) {
        defend_until_ = frame + 2400;
        progress_at_ = frame;
        planned_ = -1000;
    }
    const bool lost = target_ && !target;
    // Riding a platform toward a hop: the route through it is kept.
    const int standing_on = platform_under(engine, me);
    const bool riding = standing_on >= 0 && pending_hop(standing_on, me) < route_.size();
    if (airborne && frame - planned_ < 120) {
        // keep the plan while in the air
    } else if ((hop_ || riding) && !lost && frame - planned_ < 1800) {
        // keep the plan through a hop
    } else if (lost || frame - planned_ >= 45 || (!target && !to_goal_ && !closest_ && waiting_since_ < 0 && hold_mat_.empty() && next_ >= route_.size() && flat(goal_point_, me) < 6.0f) ||
        stalled_ >= 90) {
        if (stalled_ >= 90) {
            stalled_ = 0;
            // Walking against something: give the spot up.
            visited_.insert(coarse(goal_point_.x, goal_point_.z));
            frontier_.reset();
            if (target_ && what_.rfind("enemy", 0) != 0) unreachable_[target_] = frame + 300;
        }
        plan(engine, player, goal, frame);
        if (std::getenv("DW4_TRACE_EXPLORE"))
            std::printf("[%d] explore at (%.0f,%.0f,%.0f): %s (%.0f,%.0f) %zu points\n", frame, me.x, me.y, me.z, what_.c_str(), goal_point_.x,
                        goal_point_.z, route_.size());
        target = nullptr;
        if (target_ && models)
            for (const auto& e : models->rams())
                if (e.get() == target_) target = e.get();
    }
    engine::Vec3f aim = target ? position_of(*target) : goal_point_;
    // A hovering enemy or a turret is fought from its post: the route leads there first.
    const bool perching = perch_ && target && target->category == "ENEMY";
    const bool on_perch = perching && flat(*perch_, me) < 4.0f && std::abs(me.y - perch_->y) < 2.0f;
    const engine::Vec3f enemy_at = aim;
    if (perching && !on_perch) aim = *perch_;
    if (!on_perch) perch_since_ = -1;
    // A talk gimmick within the reach of the player's talk box (8.5 ahead):
    // near its origin, or, for a box reaching far out of its model (the
    // staggf03 console behind its railing), with the box 8 units off.
    const auto talk_in_reach = [&](const engine::ModelCtrlManager::ModelRam& t) {
        return flat(aim, me) <= 12.0f || (flat(aim, me) <= 15.0f && stalled_ > 5) || (box_reach(t) > kFarTalk && body_gap(t, me) <= 8.0f);
    };
    // Watchdog: a gimmick, mat, pickup or explored point that is not
    // approached (or worked) for 600 frames is given up for a while.
    const std::string watch_key = what_ == "explore" ? "explore " + std::to_string(coarse(goal_point_.x, goal_point_.z).first) + "," +
                                                           std::to_string(coarse(goal_point_.x, goal_point_.z).second)
                                                     : what_;
    // (A new entry starts fresh too: its zero defaults would read as no
    // progress since frame 0.)
    auto [watched_entry, fresh_watch] = watch_.try_emplace(watch_key);
    auto& watch = watched_entry->second;
    if (fresh_watch || frame - watch.seen > 300) watch = {std::numeric_limits<float>::infinity(), frame, frame};
    watch.seen = frame;
    // Progress is measured along the route (waypoints left), so a detour that
    // first leads away is not given up.
    const float remaining = next_ < route_.size() ? float(route_.size() - next_) * 15.0f : flat(aim, me);
    if (remaining < watch.best - 2.0f) {
        watch.best = remaining;
        watch.since = frame;
    }
    const bool watched = gimmick_ != Gimmick::none || !target_mat_.empty() || !target_portal_.empty() || !target_radar_.empty() ||
                         to_goal_ || what_ == "explore" || what_.rfind("item", 0) == 0;
    // Frames without control (an entrance demo, a text window) are not
    // counted: the player could not have moved.
    if (!player.control_enabled || player.seq_name.rfind("SQ_DEMO", 0) == 0) watch.since = frame;
    if (watched && frame - watch.since > 600) {
        if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore gives up %s\n", frame, what_.c_str());
        give_up(frame);
        watch.since = frame;
    }
    while (next_ < route_.size() && flat(route_[next_].point, me) < 5.0f) ++next_;
    // Without the test cheats an enemy is not chased over moving platforms:
    // a blow or a guard's recoil on a landing throws the player into the
    // pit (back with 1 HP, ADDHP absolute): a level 26 no-cheat chain hopped
    // after an e_minotaru across dng206f2's platforms, guarded on the
    // landing's edge and fell three times in 2000 frames, out of heals. It
    // comes off the platforms to the player or is left for later.
    if (!engine.cheats().one_hit && !engine.cheats().god && target_ && what_.rfind("enemy", 0) == 0 && !(hop_ && hop_->jumped)) {
        bool rides = false;
        for (std::size_t k = next_; k < route_.size() && !rides; ++k) rides = route_[k].support >= 0;
        if (rides) {
            if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore leaves %s (the way rides a moving platform)\n", frame, what_.c_str());
            unreachable_[target_] = frame + 3000;
            hop_.reset();
            planned_ = -1000;
            return decision;
        }
    }
    // A hop onto or off a moving platform along the route (waiting on
    // purpose; hop_step gives up on its own).
    if (hop_step(engine, player, me, airborne, frame, decision)) {
        watch.since = frame;
        stalled_ = 0;
        escape_until_ = -1;
        if (!player.control_enabled) decision.jump = decision.attack = false;
        return decision;
    }
    const float close = !target ? 8.0f : (perching && !on_perch) ? 0.0f : gimmick_ == Gimmick::step ? 3.0f : (gimmick_ == Gimmick::talk || gimmick_ == Gimmick::tech) ? 12.0f : 25.0f;
    // Below a boss whose body starts above the swing: the route up onto its
    // floor is followed however close the boss is (straight at it only runs
    // into the step under its body).
    const bool climbing = boss_stand_ && target && aim.y > me.y + 8.0f;
    // Turret fire ahead: the route enters a lane within 30 units while the
    // player stands out of the fire (one floor below its bullets, say). It
    // walks to the last point out of the fire (a jump's take-off) and waits
    // there until no bullet of the turret is still to pass that point and
    // the turret's shot pattern (the interval that followed the last two the
    // time before) leaves time to get through, or to the turret's post and a
    // first swing: the long gap after a volley. Through means out of the
    // lanes and out of the turret's front within 25 units at the heights of
    // its fire (new bullets start at its muzzle).
    bool hold_fire = false;
    std::optional<engine::Vec3f> fire_entry;
    if ((flat(aim, me) > close || climbing) && next_ < route_.size() && !airborne && player.control_enabled && frame >= fire_go_until_ &&
        !fire_cells_.empty() && !in_fire(me, frame)) {
        std::map<const void*, std::pair<float, float>> turrets; // turret -> heights of its fire
        auto front = [&](const engine::Vec3f& p) {
            for (const auto& [turret, band] : turrets) {
                const auto stand = standing_.find(turret);
                if (stand == standing_.end() || p.y < band.first || p.y > band.second) continue;
                const auto* model = static_cast<const engine::ModelCtrlManager::ModelRam*>(turret);
                if (!model->node || flat(p, stand->second.first) >= 25.0f) continue;
                const auto& m = model->node->matrix;
                if ((p.x - stand->second.first.x) * m(2, 0) + (p.z - stand->second.first.z) * m(2, 2) > 0.0f) return true;
            }
            return false;
        };
        // On the way to a turret's post the crossing ends there (and a swing).
        const bool to_post = perching && target->info_name.rfind("g_", 0) == 0;
        float before = 0.0f, inside = 0.0f;
        int jumps = 0;
        bool entered = false, left = false, jump_in = false;
        std::size_t entry_index = route_.size(); // the waypoint whose way in enters the fire
        engine::Vec3f from = me, last_safe = me, first_hot{};
        for (std::size_t i = next_; i < route_.size() && !left && (entered || before < 30.0f); ++i) {
            const auto& to = route_[i].point;
            const float length = flat(from, to);
            if (length < 0.5f) continue;
            bool counted = false;
            for (float t = std::min(2.5f, length); t <= length && !left; t += 2.5f) {
                const engine::Vec3f p{from.x + (to.x - from.x) * t / length, to.y, from.z + (to.z - from.z) * t / length};
                const bool hot = in_fire(p, frame);
                if (hot) {
                    const auto& cell = fire_cells_.at({int(std::lround(p.x / Grid::step)), int(std::lround(p.z / Grid::step))});
                    turrets.try_emplace(cell.shooter, cell.lo, cell.hi);
                }
                if (!entered && hot) {
                    entered = true;
                    first_hot = p;
                    entry_index = i;
                    // A jump into the fire: waited for at its take-off.
                    jump_in = route_[i].jump;
                    if (jump_in) last_safe = from;
                }
                if (entered && (hot || front(p) || to_post)) {
                    inside += 2.5f;
                    if (route_[i].jump && !counted) ++jumps;
                    counted = true;
                } else if (entered) {
                    left = true;
                } else {
                    last_safe = p;
                    if ((before += 2.5f) >= 30.0f) break;
                }
                left = left || inside >= 80.0f;
            }
            from = to;
        }
        // Every turret firing lately whose lanes reach the entry at its height
        // (not only the last to mark the cells: two need not keep in step).
        if (entered)
            for (const auto& [turret, band] : turret_band_) {
                const auto shots = shots_.find(turret);
                const auto stand = standing_.find(turret);
                if (shots == shots_.end() || shots->second.empty() || frame - shots->second.back() > 300 || stand == standing_.end()) continue;
                if (first_hot.y >= band.first && first_hot.y <= band.second && flat(first_hot, stand->second.first) < 110.0f)
                    turrets.try_emplace(turret, band);
            }
        // Frames from the entry until through: walking at 0.75 a step, a
        // jump up takes about 38 and carries some 12 units, a swing lands 15
        // after the press.
        const float needed = std::max(0.0f, inside - 12.0f * float(jumps)) / 0.75f + float(jumps) * 38.0f + (to_post ? 15.0f : 0.0f) +
                             flat(me, last_safe) / 0.75f;
        bool go = true;
        float least = std::numeric_limits<float>::infinity();
        // A jump up into the lane (from the floor below it, where the bullets
        // pass overhead) is timed on the steady view of the bullets and
        // shots (track_fire): a bullet re-arming its box is no gap, and its
        // re-arm no shot that would spoil the volley pattern.
        const auto& live_bullets = jump_in ? steady_bullets_ : bullets_;
        auto& shot_frames = jump_in ? volleys_ : shots_;
        for (const auto& [turret, band] : turrets) {
            const auto stand = standing_.find(turret);
            if (stand == standing_.end()) continue;
            // Turrets out of step may leave no gap for all: after a long wait
            // only the one the post is for counts.
            if (to_post && turret != target_ && fire_wait_since_ >= 0 && frame - fire_wait_since_ > 300) continue;
            const auto at = stand->second.first;
            // A bullet still to pass the entry.
            const float speed = bullet_speed_[turret];
            for (const auto& [bullet, info] : live_bullets)
                if (info.second == turret && flat(info.first, at) < flat(first_hot, at) + 3.0f) go = false;
            const auto& shots = shot_frames[turret];
            float remaining = -1.0f;
            // A jump up into the lane: the turret's phase in its firing
            // sequence tells the next shot (one learned from at least a
            // whole run): the next shot frame of this run, else the first of
            // the next run (one frame after this one ends); out of that
            // sequence (hit, between runs) no gap is known.
            bool phased = false;
            if (jump_in)
                if (const auto ph = phases_.find(turret); ph != phases_.end() && !ph->second.fire_seq.empty() && !ph->second.offsets.empty() &&
                                                          ph->second.length > ph->second.offsets.back()) {
                    phased = true;
                    const auto* model = static_cast<const engine::ModelCtrlManager::ModelRam*>(turret);
                    if (model->seq_name == ph->second.fire_seq) {
                        const int elapsed = frame - ph->second.start;
                        remaining = float(ph->second.length + 1 - elapsed + ph->second.offsets.front());
                        for (const int o : ph->second.offsets)
                            if (o >= elapsed) {
                                remaining = float(o - elapsed);
                                break;
                            }
                    }
                }
            if (!phased && shots.size() >= 6) {
                std::vector<int> gaps;
                for (std::size_t i = 1; i < shots.size(); ++i) gaps.push_back(shots[i] - shots[i - 1]);
                const std::size_t n = gaps.size();
                // (For a jump into the lane only the latest repeat counts: an
                // overdue shot is no gap.)
                bool matched = false;
                for (std::size_t i = n - 1; i >= 2 && remaining < 0.0f && !matched; --i)
                    if (std::abs(gaps[i - 2] - gaps[n - 2]) <= 4 && std::abs(gaps[i - 1] - gaps[n - 1]) <= 4) {
                        remaining = float(gaps[i] - (frame - shots.back()));
                        matched = jump_in;
                    }
            }
            // (Its next bullet still has to fly to where the player will be.)
            const engine::Vec3f until = to_post && perch_ ? *perch_ : first_hot;
            if (remaining >= 0.0f && speed > 0.1f) remaining += std::max(0.0f, flat(until, at) - 6.0f) / speed;
            // No pattern yet (one that fires only at a player near it shows
            // little of it from further off): quiet for two seconds is a gap.
            if (remaining < 0.0f && (shots.empty() || frame - shots.back() >= 120)) remaining = needed;
            if (remaining < needed) go = false;
            least = std::min(least, remaining);
        }
        // Below the take-off's floor (a step on the way up to it, which the
        // straight walk to the waiting point would only run into: stag0304's
        // stairs to the cannon at (-370,120,100)): on along the route (its
        // jumps included) until the take-off is the waypoint before the jump
        // into the fire; the wait starts there.
        const bool to_takeoff = jump_in && next_ < entry_index && std::abs(last_safe.y - me.y) > 2.0f;
        if (entered && !go && to_takeoff) {
            fire_wait_since_ = -1;
        } else if (entered && !go) {
            if (fire_wait_since_ < 0) {
                fire_wait_since_ = frame;
                if (std::getenv("DW4_TRACE_EXPLORE"))
                    std::printf("[%d] explore waits at (%.0f,%.0f,%.0f) for a gap in the fire of %zu turret(s) ahead\n", frame, last_safe.x,
                                last_safe.y, last_safe.z, turrets.size());
            }
            if (frame - fire_wait_since_ > 450) {
                fire_go_until_ = frame + 300; // waited long enough: on regardless for a while
                fire_wait_since_ = -1;
            } else {
                hold_fire = true;
                fire_entry = last_safe;
            }
        } else {
            if (entered && fire_wait_since_ >= 0 && std::getenv("DW4_TRACE_EXPLORE"))
                std::printf("[%d] explore crosses the fire (needs %.0f of %.0f frames)\n", frame, needed, least);
            // Once going, through without stopping (a jump on the way to a
            // turret's post steers on to the post: the stick held, the jump
            // carries the player well onto the floor above).
            if (entered && fire_wait_since_ >= 0) {
                fire_go_until_ = frame + int(needed);
                fire_leap_to_post_ = to_post && jump_in;
            }
            fire_wait_since_ = -1;
        }
    } else if (in_fire(me, frame)) {
        fire_wait_since_ = -1;
    }
    if (frame >= fire_go_until_) fire_leap_to_post_ = false;
    // A jump up onto the floor of a turret's post steers on to the post as
    // well (as a crossing's jump does): turrets that open fire once a player
    // on their floor comes within range (stag0302's cannon pair on its
    // terrace, about 50 units; their shots follow the player) hit the player
    // as it lands, and a hit at the rim throws it back down the step. With
    // the stick held on the post the jump carries the player some 14 units
    // onto that floor instead of to the rim, where the next hits only push
    // it back a little. Only for a post ahead (within 45 degrees of the
    // jump, at most 40 units on) over open floor at the post's height.
    bool leap_to_post = false;
    auto jump_onto_post_floor = [&](const engine::Vec3f& takeoff, const engine::Vec3f& landing) {
        if (!perching || target->info_name.rfind("g_", 0) != 0 || landing.y < takeoff.y + 5.0f || std::abs(landing.y - perch_->y) > 2.0f)
            return false;
        const float length = flat(landing, *perch_);
        if (length < 5.0f || length > 40.0f) return false;
        const float jump_dir = std::atan2(landing.x - takeoff.x, landing.z - takeoff.z);
        const float post_dir = std::atan2(perch_->x - takeoff.x, perch_->z - takeoff.z);
        if (std::abs(std::remainder(post_dir - jump_dir, 6.2831853f)) > 0.7853982f) return false;
        auto* hitland = engine.get<engine::HitLandManager>();
        if (!hitland) return false;
        const auto flags = hitland->delivery_flags(player.info_name, player.index);
        const float width = hitland->delivery_width(player.info_name, player.index);
        const engine::Vec3f dir{(perch_->x - landing.x) / length, 0.0f, (perch_->z - landing.z) / length};
        for (float t = 0.0f; t <= length; t += 2.5f) {
            const engine::Vec3f p{landing.x + dir.x * t, perch_->y + 5.0f, landing.z + dir.z * t};
            const auto land = hitland->world().check_land(p, 1.0f, 400.0f, 1.0f);
            if (!land.found || std::abs(land.position.y - perch_->y) > 1.5f) return false;
            const engine::Vec3f middle{p.x + dir.x * 1.25f, perch_->y, p.z + dir.z * 1.25f};
            if (t + 2.5f <= length && hitland->world().check_wall(middle, dir, width, 4.0f, flags, false).hit) return false;
        }
        return true;
    };
    if (hold_fire) {
        // To the last point out of the fire, then stand.
        decision.x = flat(*fire_entry, me) > 1.5f ? fire_entry->x : me.x;
        decision.z = flat(*fire_entry, me) > 1.5f ? fire_entry->z : me.z;
        decision.reach = 1.0f;
        stalled_ = 0;
        watch.since = frame;
    } else if ((flat(aim, me) > close || climbing) && next_ < route_.size()) {
        decision.x = route_[next_].point.x;
        decision.z = route_[next_].point.z;
        // A gap: jump from the take-off point toward the landing.
        if (route_[next_].jump) {
            const auto& takeoff = next_ > 0 ? route_[next_ - 1].point : me;
            const auto& landing = route_[next_].point;
            const float span = flat(takeoff, landing);
            // A moving platform at the landing: jump only when it will be
            // under the landing at touch-down (about 24 frames on; the floor
            // there then is the floor now 24 steps of its motion back),
            // else wait at the take-off.
            bool ready = true;
            // With real blows (no test cheat) a row of jumps over a pit (this
            // one and the next ones of the route) starts only while no enemy
            // within kJumpFoe of their landings can strike before the player
            // is over: it attacks, waits (its AI picks next) or walks with
            // less than kJumpFoeWalk frames of its AITIMER left; a shot near
            // a landing counts too. A blow in the air drops the player into
            // the pit (dng103f2's stepping stones and the two e_scum on the
            // far side, which walk 157 frames, wait and fire for 91: 11..14
            // falls in a row at level 15, each one back with 1 HP and a heal
            // used, then the Gate Disk out). The player waits at the take-off
            // of the first stone, out of their range; after kJumpFoeHold
            // frames it jumps anyway.
            // (Not from a floor that gives way, set_hurry: dng310f3's fading
            // tiles, off 60 frames after the player lands on one.)
            bool hold_jump = false;
            if (!engine.cheats().god && !engine.cheats().one_hit && models && flat(takeoff, me) < 8.0f && !hurry_) {
                constexpr float kJumpFoe = 25.0f, kJumpFoeWalk = 150.0f;
                constexpr int kJumpFoeHold = 2400;
                std::vector<engine::Vec3f> landings;
                for (std::size_t i = next_; i < route_.size() && i < next_ + 6; ++i)
                    if (route_[i].jump) landings.push_back(route_[i].point);
                bool danger = false;
                for (const auto& e : models->rams()) {
                    if (danger) break;
                    if (!e->generated || !e->node || e->is_player) continue;
                    const bool shot = e->category == "EBULLET";
                    if (!shot && (e->category != "ENEMY" || e->hp <= 0 || e->info_name.rfind("g_", 0) == 0)) continue;
                    const auto at = position_of(*e);
                    bool near = false;
                    for (const auto& l : landings) near = near || (flat(at, l) < kJumpFoe && std::abs(at.y - l.y) < 20.0f);
                    if (!near) continue;
                    if (!shot && e->seq_name == "SQ_WALK" && e->ai_timer &&
                        float(e->ai_timer->end - engine::timer_time(*e->ai_timer, engine.time())) > kJumpFoeWalk * 16666667.0f)
                        continue;
                    danger = true;
                }
                if (!danger) {
                    jump_hold_since_ = -1;
                } else {
                    if (jump_hold_since_ < 0) jump_hold_since_ = frame;
                    hold_jump = frame - jump_hold_since_ < kJumpFoeHold;
                }
            }
            if (span > 21.0f) {
                // A long jump: from a run-up point 8 units back, run along the
                // line and jump at the edge.
                const float ux = (landing.x - takeoff.x) / span, uz = (landing.z - takeoff.z) / span;
                const float along = (me.x - takeoff.x) * ux + (me.z - takeoff.z) * uz;
                const float lateral = std::abs((me.z - takeoff.z) * ux - (me.x - takeoff.x) * uz);
                const engine::Vec3f runup{takeoff.x - ux * 8.0f, takeoff.y, takeoff.z - uz * 8.0f};
                if (runup_ready_ && (lateral > 3.0f || along < -12.0f)) runup_ready_ = false; // knocked off the line
                if (hold_jump) runup_ready_ = false; // (waits at the run-up point)
                if (!runup_ready_) {
                    decision.x = runup.x;
                    decision.z = runup.z;
                    decision.reach = 1.0f;
                    stalled_ = 0;
                    watch.since = frame;
                    if (flat(runup, me) < 1.5f) runup_ready_ = true;
                } else {
                    decision.x = landing.x;
                    decision.z = landing.z;
                    decision.reach = 0.25f;
                    // Jump at the very edge: past the take-off cell's centre,
                    // once the floor 2 units ahead ends (or 2.5 past it).
                    bool edge = along >= 2.5f;
                    if (!edge && along >= -0.3f)
                        if (auto* hitland = engine.get<engine::HitLandManager>()) {
                            const engine::Vec3f ahead{me.x + ux * 2.0f, me.y + 5.0f, me.z + uz * 2.0f};
                            const auto land = hitland->world().check_land(ahead, 1.0f, 400.0f, 1.0f);
                            edge = !land.found || land.position.y < me.y - 3.0f;
                        }
                    if (edge && !airborne) {
                        decision.jump = true;
                        leap_ = landing;
                        leap_frame_ = frame;
                        runup_ready_ = false;
                    }
                }
                ready = false;
            } else if (auto* hitland = engine.get<engine::HitLandManager>(); hitland && models && flat(takeoff, me) < 8.0f)
                for (const auto& e : models->rams()) {
                    if (!e->generated || !e->node || e->is_player || e->category == "ENEMY") continue;
                    // (Enemy bullets, kind 7 or 8 boxes, are no platforms.)
                    bool enemy_fire = false;
                    for (const auto& h : e->hitboxes) enemy_fire = enemy_fire || h.kind / 10000 == 7 || h.kind / 10000 == 8;
                    if (enemy_fire) continue;
                    const auto at = position_of(*e);
                    if (flat(at, landing) > 30.0f || std::abs(at.y - landing.y) > 20.0f) continue;
                    const auto last = model_last_.find(e.get());
                    if (last == model_last_.end()) continue;
                    const float vx = at.x - last->second.x, vz = at.z - last->second.z;
                    if (std::hypot(vx, vz) < 0.05f || std::hypot(vx, vz) > 5.0f) continue;
                    const engine::Vec3f probe{landing.x - vx * 24.0f, landing.y + 5.0f, landing.z - vz * 24.0f};
                    const auto land = hitland->world().check_land(probe, 1.0f, 400.0f, 1.0f);
                    ready = land.found && std::abs(land.position.y - landing.y) < 4.0f;
                    break;
                }
            if (span > 21.0f) {
                // handled above
            } else if (!ready || hold_jump) {
                decision.x = takeoff.x;
                decision.z = takeoff.z;
                decision.reach = 1.0f;
                stalled_ = 0; // waiting on purpose, for 900 frames at most
                if (platform_wait_since_ < 0) platform_wait_since_ = frame;
                if (hold_jump || frame - platform_wait_since_ < 900) watch.since = frame;
            } else if (flat(takeoff, me) < 4.0f && !airborne) {
                platform_wait_since_ = -1;
                decision.jump = true;
                leap_ = landing;
                leap_frame_ = frame;
                leap_to_post = jump_onto_post_floor(takeoff, landing);
            }
        }
    } else {
        decision.x = aim.x;
        decision.z = aim.z;
    }
    if (decision.jump && (fire_leap_to_post_ || leap_to_post) && perch_ && leap_) {
        leap_ = *perch_;
        decision.x = perch_->x;
        decision.z = perch_->z;
        fire_leap_to_post_ = false;
    }
    const auto spend = [&](int budget) {
        watch.since = frame;
        if (++struck_[target] == budget) {
            struck_at_[target] = frame;
            progress_at_ = frame;
        }
    };
    if (target && gimmick_ == Gimmick::step) {
        // Stand on the switch (its NEAR band is 6 units).
        decision.reach = 1.0f;
        if (flat(aim, me) < 3.0f) spend(60);
    } else if (target && gimmick_ == Gimmick::tech) {
        // Face the wall from up to 12 units, open the technique panel (R),
        // cast the first technique (A) and close the panel again (R).
        decision.reach = 0.25f;
        if (flat(aim, me) <= 12.0f || (flat(aim, me) <= 15.0f && stalled_ > 5)) {
            watch.since = frame;
            if (player.seq_name == "SQ_WAZA") tech_used_ = true;
            if (frame % 30 < 3) {
                if (!player.panel_open) {
                    if (tech_used_) {
                        tech_used_ = false;
                        spend(3);
                    } else {
                        decision.press_r = true;
                    }
                } else if (!tech_used_) {
                    decision.press_a = true;
                } else {
                    decision.press_r = true;
                }
            }
        }
    } else if (target && gimmick_ == Gimmick::talk) {
        // Wall switches sit inside the wall: keep pushing toward the origin
        // (facing it) and press A; the attack's talk box reaches 8.5 ahead
        // (also to a far-reaching box, the staggf03 console behind its railing).
        decision.reach = 0.25f;
        if (talk_in_reach(*target)) {
            decision.press_a = frame % 35 < 3;
            if (decision.press_a) spend(12);
            else watch.since = frame;
        }
    } else if (perching) {
        // On the post: strike once the enemy is in reach, facing it with a
        // short tap of the stick (walking back to the post's middle would
        // turn the player away); otherwise stand and let it come.
        if (!on_perch) {
            decision.reach = 1.0f;
            // A post never got onto (a turret's fire throwing the player back
            // each time, or never a gap to cross it) is given up for a while,
            // as a structure never reached; for the stage the third time.
            // (A forgiven turret's walk to its post counts a quarter: the far
            // explored ground it comes back from is no try at the post.)
            const bool counts = !forgiven_.count(target_) || flat(*perch_, me) <= 40.0f || frame % 4 == 0;
            if (!held_by_hit(player) && counts && ++approach_[target_] > 1800) {
                if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore gives up reaching %s\n", frame, what_.c_str());
                approach_[target_] = 0;
                unreachable_[target_] = frame + 3000;
                if (++fruitless_runs_[target_] >= 3) {
                    unkillable_.insert(target_);
                    unreached_.insert(target_);
                }
                planned_ = -1000;
            }
        } else {
            approach_[target_] = 0;
            stalled_ = 0;
            watch.since = frame;
            if (perch_since_ < 0) perch_since_ = frame;
            decision.x = me.x;
            decision.z = me.z;
            // (A turret's body is near when the swing meets it.)
            if (target->info_name.rfind("g_", 0) == 0 ? body_gap(*target, me) <= 12.0f : flat(enemy_at, me) <= 16.0f) {
                decision.attack = true;
                // A turret recovers from a hit (SQ_SDMG) in about the attack
                // delay and fires at once: every swing as soon as it may.
                decision.fast_attack = target->info_name.rfind("g_", 0) == 0;
                const float want = std::atan2(enemy_at.x - me.x, enemy_at.z - me.z);
                if (std::abs(std::remainder(want - player.applied[1], 6.2831853f)) > 0.5f && frame % 20 >= 16) {
                    decision.x = enemy_at.x;
                    decision.z = enemy_at.z;
                    decision.reach = 0.25f;
                }
            } else if (flat(*perch_, me) > 2.0f) {
                decision.x = perch_->x;
                decision.z = perch_->z;
                decision.reach = 1.0f;
            }
            // Nothing came within reach (or took a hit) for long: another post.
            if (frame - std::max(perch_since_, perch_hit_at_) > 900) {
                perch_skip_[{int(std::lround(perch_->x / Grid::step)), int(std::lround(perch_->z / Grid::step))}] = frame + 3000;
                perch_since_ = -1;
                planned_ = -1000;
            }
        }
        // As below: one whose HP reaches no new low while struck at (a
        // poison vent that heals itself and turns invulnerable) is left alone
        // for a good while, for the stage when seen healing or the third time.
        auto [low, fresh] = lowest_hp_.emplace(target_, target->hp);
        if (!fresh && target->hp > low->second) regenerating_.insert(target_), note_regenerating(engine, *target, frame);
        if (fresh || target->hp < low->second) {
            low->second = target->hp;
            perch_hit_at_ = frame;
            fruitless_[target_] = 0;
        } else if (decision.attack && (fruitless_[target_] += 2) > 1800) {
            if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore leaves %s alone (no damage)\n", frame, what_.c_str());
            fruitless_[target_] = 0;
            if (++fruitless_runs_[target_] >= 3 || regenerating_.count(target_)) unkillable_.insert(target_);
            unreachable_[target_] = frame + 6000;
            planned_ = -1000;
            progress_at_ = frame;
        }
    } else if (target && target->category == "ENEMY" && target->info_name.rfind("f_", 0) == 0 && target->hit &&
               !target->hit->boxes.empty() && std::abs(aim.y - me.y) <= 8.0f) {
        // A boss on the player's level: its body box (as the hit check sees
        // it) and the contact box of its attacks reach furthest on the
        // diagonals, so the bot takes position on a world axis, 8 units off
        // the body face (inside the weapon's reach, outside the contact),
        // strikes from there and returns to the axis after a knockback.
        const auto& m = target->node->matrix;
        engine::Vec3f lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const auto& [box_lo, box_hi] : target->hit->boxes)
            for (int corner = 0; corner < 8; ++corner) {
                const engine::Vec3f c{corner & 1 ? box_hi.x : box_lo.x, corner & 2 ? box_hi.y : box_lo.y, corner & 4 ? box_hi.z : box_lo.z};
                const engine::Vec3f w{c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0) + m(3, 0), 0.0f,
                                      c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2) + m(3, 2)};
                lo = {std::min(lo.x, w.x), 0.0f, std::min(lo.z, w.z)};
                hi = {std::max(hi.x, w.x), 0.0f, std::max(hi.z, w.z)};
            }
        const engine::Vec3f centre{(lo.x + hi.x) * 0.5f, me.y, (lo.z + hi.z) * 0.5f};
        const float dx = me.x - centre.x, dz = me.z - centre.z;
        const bool along_x = std::abs(dx) >= std::abs(dz);
        const float half = along_x ? (hi.x - lo.x) * 0.5f : (hi.z - lo.z) * 0.5f;
        const float along = along_x ? std::abs(dx) : std::abs(dz), across = along_x ? std::abs(dz) : std::abs(dx);
        const float stand = half + 8.0f;
        const engine::Vec3f post{along_x ? centre.x + (dx >= 0 ? stand : -stand) : centre.x, me.y,
                                 along_x ? centre.z : centre.z + (dz >= 0 ? stand : -stand)};
        if (std::hypot(dx, dz) > stand + 40.0f) {
            // Still on the way: follow the route.
        } else if (across > 5.0f || along > stand + 12.0f) {
            decision.x = post.x; // back onto the axis first
            decision.z = post.z;
            decision.reach = 4.0f;
        } else {
            decision.x = centre.x; // face the boss, closing in to the post
            decision.z = centre.z;
            decision.reach = (stand - 1.0f) * (stand - 1.0f);
        }
        if (along - half <= 10.0f && across <= 6.0f) decision.attack = true;
    } else if (target && (target->category == "ENEMY" || gimmick_ == Gimmick::strike)) {
        const float d2 = (aim.x - me.x) * (aim.x - me.x) + (aim.z - me.z) * (aim.z - me.z);
        // Structures (towers) and bosses are walked up to until their body
        // stops the player; others are attacked from 20 units.
        const bool structure = target->category == "ENEMY" &&
                               (target->info_name.rfind("g_", 0) == 0 || target->info_name.rfind("f_", 0) == 0);
        // (One that took no new damage for a while is walked up to 5 units.)
        // A struck gimmick (a breakable wall, a box) is walked up to as well:
        // its body box (80307EAC, authored half extents) is small next to
        // its centre's distance.
        const bool walk_up = structure || (gimmick_ == Gimmick::strike && target->hit && !target->hit->boxes.empty());
        // The player's swing (CHKBULLET 10000) spans its feet to 8 above: an
        // enemy whose body top stays below the feet (at the foot of a terrace
        // step, 10 down; e_mecha2's body is 7 high) is never hit from the
        // ledge however close. Walk onto it to drop down to its level.
        const float drop = me.y - body_top(*target);
        const bool below = !structure && target->category == "ENEMY" && drop > 0.0f && drop <= 25.0f;
        decision.reach = walk_up || below ? 1.0f : flat(aim, me) <= close ? (fruitless_[target_] > 240 ? 25.0f : 400.0f) : 4.0f;
        // A structure is struck once its body is within 15 (a big body keeps
        // the player further from its centre); others from 20. Blocked within
        // 30, swing half the time only, so the sidestep (never while
        // attacking) can free the player.
        const bool in_reach = below                                    ? false
                              : structure && target->category == "ENEMY" ? body_gap(*target, me) <= 15.0f
                              : walk_up                                ? body_gap(*target, me) <= 12.0f
                                                                       : d2 <= 400.0f;
        if (climbing && next_ >= route_.size()) {
            decision.x = boss_stand_->x; // the route ran out below: to the stand point
            decision.z = boss_stand_->z;
        }
        if (!climbing && (in_reach || (!below && d2 <= 900.0f && stalled_ > 20 && frame % 120 < 60))) {
            decision.attack = true;
            decision.jump_attack = aim.y > me.y + 8.0f;
            if (gimmick_ == Gimmick::strike && !held_by_hit(player)) spend(240);
        }
        // An enemy whose HP reaches no new low for a long while is left alone
        // for a good while: a poison vent heals itself and turns invulnerable,
        // a cannon behind rocks is never reached. Attacking counts double; a
        // boss gets longer for its invulnerable phases.
        if (target->category == "ENEMY") {
            // Only a target standing still counts (vents and cannons do; a
            // knight that stuns the player about is no vent), and not while
            // the player is held by a hit.
            const auto now_at = position_of(*target);
            auto [last_at, first_seen] = enemy_last_.emplace(target_, now_at);
            const bool still = !first_seen && flat(last_at->second, now_at) <= 0.05f && !held_by_hit(player);
            last_at->second = now_at;
            // A structure never got within reach of is given up for a while
            // (not counted as taking no damage).
            if ((structure || below) && !in_reach && !held_by_hit(player)) {
                if (++approach_[target_] > 1800) {
                    if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore gives up reaching %s\n", frame, what_.c_str());
                    approach_[target_] = 0;
                    unreachable_[target_] = frame + 3000;
                    if (++fruitless_runs_[target_] >= 3) unkillable_.insert(target_); // the third time for the stage
                    planned_ = -1000;
                }
            } else if (in_reach) {
                approach_[target_] = 0;
            }
            auto [low, fresh] = lowest_hp_.emplace(target_, target->hp);
            if (!fresh && target->hp > low->second) regenerating_.insert(target_), note_regenerating(engine, *target, frame);
            if (fresh || target->hp < low->second) {
                low->second = target->hp;
                fruitless_[target_] = 0;
            } else if (still && !below && (!structure || in_reach) &&
                       (fruitless_[target_] += decision.attack ? 2 : 1) > (target->info_name.rfind("f_", 0) == 0 ? 6000 : 1800)) {
                if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore leaves %s alone (no damage)\n", frame, what_.c_str());
                fruitless_[target_] = 0;
                // For good once it was seen healing itself or the third time
                // (a stage full of vents would otherwise be gone round
                // forever; a cannon that knocks the player back may still be
                // reached later).
                if (++fruitless_runs_[target_] >= 3 || regenerating_.count(target_)) unkillable_.insert(target_);
                unreachable_[target_] = frame + 6000;
                planned_ = -1000;
                progress_at_ = frame;
            }
        }
    } else if (target) {
        // A pickup: some are taken by touch, others (keys) with A.
        decision.reach = flat(aim, me) <= close ? 9.0f : 4.0f;
        if (flat(aim, me) <= 3.0f) decision.pick_up = true;
    }
    // The exit's prompt is answered once it holds the player (A before it
    // would only swing and stop the walk into the mat).
    if (to_goal_ && flat(goal_point_, me) < 20.0f && !player.control_enabled) decision.confirm = true;
    // A panel left open (A would pick from it instead of attacking): close.
    if (player.panel_open && gimmick_ != Gimmick::tech && frame % 30 < 3) {
        decision.press_r = true;
        decision.attack = decision.press_a = decision.confirm = decision.pick_up = false;
    }
    // Standing on the goal with control and nothing happening: its exit is
    // not open yet (a boss gate after the last wave); do the rest first.
    on_goal_ = to_goal_ && flat(goal_point_, me) < 6.0f && player.control_enabled ? on_goal_ + 1 : 0;
    if (on_goal_ > 300) {
        on_goal_ = 0;
        goal_first_failed_ = true;
        goal_later_until_ = frame + 3000;
        mats_done_.clear();
        mat_frames_.clear();
        planned_ = -1000;
    }
    // Defending: strike an enemy that comes close, without chasing it.
    // (Also while crossing a field, set_transit.)
    if ((frame < defend_until_ || transit_) && !decision.attack && !decision.press_a && models)
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && e->hp > 0 && flat(position_of(*e), me) < 12.0f &&
                !(unreachable_.count(e.get()) && frame < unreachable_[e.get()]) && !unkillable_.count(e.get()) &&
                !regenerating_names_.count(e->info_name)) {
                decision.attack = frame % 20 < 3;
                break;
            }
    // Without control (a text window, a prompt, or a demo sequence like
    // SQ_DEMOWAIT holding the player while a message waits) only A goes
    // through: on another stage change mat the prompt is cancelled with B,
    // elsewhere text windows are advanced with A.
    const bool controllable = player.control_enabled && player.seq_name.rfind("SQ_DEMO", 0) != 0;
    no_control_ = controllable ? 0 : no_control_ + 1;
    if (!controllable) {
        decision.attack = decision.pick_up = decision.jump = false;
        decision.press_a = false;
        bool on_other_exit = false;
        if (auto* mats = engine.get<engine::MatCtrlManager>())
            for (const auto& box : mats->mat_boxes()) {
                const auto key = mat_key(box.name);
                if (!goal_keys(goal).count(key) && exit_mat(engine, box.name) && me.x >= box.min.x - 2 && me.x <= box.max.x + 2 &&
                    me.z >= box.min.z - 2 && me.z <= box.max.z + 2)
                    on_other_exit = true;
            }
        bool on_link = false;
        if (!target_portal_.empty())
            if (auto* mats = engine.get<engine::MatCtrlManager>())
                for (const auto& box : mats->mat_boxes())
                    if (box.name == target_portal_ && me.x >= box.min.x - 2 && me.x <= box.max.x + 2 && me.z >= box.min.z - 2 &&
                        me.z <= box.max.z + 2)
                        on_link = true;
        if (on_link) {
            // The maze link: yes (A) moves the player to the next room.
            decision.press_a = frame % 20 < 3;
        } else if (on_other_exit) {
            // B cancels the prompt (no stage change) once it takes input.
            decision.confirm = false;
            decision.press_b = frame % 20 < 3;
        } else if (!decision.confirm && no_control_ > 90 && no_control_ % 45 < 3) {
            decision.press_a = true;
        }
    }
    // Nearby models' positions for the platform speeds next step.
    if (models)
        for (const auto& e : models->rams())
            if (e->generated && e->node && !e->is_player && e->category != "ENEMY" && flat(position_of(*e), me) < 120.0f)
                model_last_[e.get()] = position_of(*e);
    // On quicksand the pull toward its centre is allowed for: the stick
    // leans away from the centre by the share of the pull in the walking
    // speed (about 0.3 of 0.75 units a frame).
    if (player.ride && player.ride->kind == engine::ModelCtrlManager::ModelRam::Ride::Kind::quicksand && player.control_enabled) {
        const auto& centre = player.ride->point;
        const float to_x = decision.x - me.x, to_z = decision.z - me.z, to = std::hypot(to_x, to_z);
        const float out = flat(me, centre);
        if (to > 0.01f && out > 0.01f) {
            const float dx = to_x / to + (me.x - centre.x) / out * 0.4f, dz = to_z / to + (me.z - centre.z) / out * 0.4f;
            const float d = std::max(0.01f, std::hypot(dx, dz));
            const float ahead = std::max(to, 2.0f);
            decision.x = me.x + dx / d * ahead;
            decision.z = me.z + dz / d * ahead;
        }
    }
    // Stuck at a corner (walls stop the player instead of sliding it):
    // sidestep left, right or back for a moment, then take the route again.
    // Not while pushing into a wall switch on purpose.
    if (stalled_ >= 25 && !decision.attack &&
        !((gimmick_ == Gimmick::talk || gimmick_ == Gimmick::tech) && (flat(aim, me) <= 15.0f || (target && talk_in_reach(*target)))) &&
        frame >= escape_until_) {
        escape_until_ = frame + 20;
        escape_turn_ = (escape_turn_ + 1) % 3;
    }
    if (frame < escape_until_) {
        constexpr float turns[3] = {1.5708f, -1.5708f, 3.1416f};
        const float heading = std::atan2(decision.x - me.x, decision.z - me.z) + turns[escape_turn_];
        decision.x = me.x + std::sin(heading) * 10.0f;
        decision.z = me.z + std::cos(heading) * 10.0f;
        decision.reach = 4.0f;
    }
    return decision;
}

} // namespace dw4::game
