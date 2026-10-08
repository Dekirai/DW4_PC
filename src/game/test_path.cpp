#include "test_path.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <queue>

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
                 const std::vector<AvoidBox>& avoid, float width) {
    constexpr float step = Grid::step, climb = Grid::climb, drop = Grid::drop;
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
    auto blocked = [&](float x, float y, float z) {
        for (const auto& box : avoid)
            if (x >= box.min_x && x <= box.max_x && z >= box.min_z && z <= box.max_z && y >= box.min_y && y <= box.max_y) return true;
        return false;
    };
    // A cell's floor is looked up from the floor it is entered from, so
    // heights follow ramps and stairs.
    auto floor_from = [&](int cell, float reference) {
        const engine::Vec3f p{x0_ + float(cell % w_) * step, reference + climb, z0_ + float(cell / w_) * step};
        const auto land = world.check_land(p, 1.0f, 400.0f, 1.0f);
        return land.found ? land.position.y : -100000.0f;
    };
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
    floor_[std::size_t(start)] = floor_from(start, from.y);
    if (floor_[std::size_t(start)] < -99999.0f) floor_[std::size_t(start)] = from.y;
    std::vector<std::uint8_t> reached(cells, 0);
    reached[std::size_t(start)] = 1;
    std::vector<int> queue{start};
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int at = queue[head];
        const int x = at % w_, z = at / w_;
        const float ya = floor_[std::size_t(at)];
        std::uint8_t pits_left = 0; // directions of pits no straight jump crosses
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d], nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= w_ || nz >= h_) continue;
            const int next_cell = nz * w_ + nx;
            const auto b = centre(nx, nz);
            const float yb = reached[std::size_t(next_cell)] ? floor_[std::size_t(next_cell)] : floor_from(next_cell, ya);
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
                    const float yl = floor_from(land, ya + 5.0f);
                    if (yl < -99999.0f) break;
                    if (ya - yl > drop) continue; // still over the pit
                    if (blocked(l.x, yl, l.z)) break;
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
                const float yu = floor_from(next_cell, ya + 4.0f);
                if (yu < -99999.0f || yu - ya <= climb || yu - ya > 12.5f || blocked(b.x, yu, b.z) || wall_between(x, z, nx, nz, yu + 1.0f))
                    continue;
                jumps.push_back({at, next_cell, 7.0f});
                if (!reached[std::size_t(next_cell)]) {
                    reached[std::size_t(next_cell)] = 1;
                    floor_[std::size_t(next_cell)] = yu;
                    queue.push_back(next_cell);
                }
                continue;
            }
            open_steps[std::size_t(at)] |= std::uint8_t(1u << d);
            if (!reached[std::size_t(next_cell)]) {
                reached[std::size_t(next_cell)] = 1;
                floor_[std::size_t(next_cell)] = yb;
                queue.push_back(next_cell);
            }
        }
        // Long jumps: with a run-up of two cells the jump carries about 28
        // units from the very edge, so wider pits are crossed at any angle
        // (15-degree steps, toward the pits found above) onto floor at most a
        // little higher (cell centres up to 32.5 apart: both edges reach
        // past them).
        if (pits_left) {
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
                    const float yl = floor_from(land, ya + 5.0f);
                    if (yl < -99999.0f || yl - ya > 2.0f || ya - yl > 30.0f || blocked(l.x, yl, l.z)) continue;
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
        if (next >= cost_[std::size_t(next_cell)]) return;
        cost_[std::size_t(next_cell)] = next;
        parent_[std::size_t(next_cell)] = at;
        jumped_[std::size_t(next_cell)] = jump ? 1 : 0;
        open.push({next, next_cell});
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
            relax(at, next_cell, g + ((d & 1) ? 1.4142f : 1.0f) + penalty, false);
        }
        for (auto [it, end] = jumps_from.equal_range(at); it != end; ++it) relax(at, it->second->to, g + it->second->cost, true);
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
    for (int z = std::max(0, cz - radius); z <= std::min(h_ - 1, cz + radius); ++z)
        for (int x = std::max(0, cx - radius); x <= std::min(w_ - 1, cx + radius); ++x) {
            if (parent_[std::size_t(z * w_ + x)] == -2) continue;
            const auto q = point(z * w_ + x);
            const float distance = std::hypot(q.x - p.x, q.z - p.z);
            if (distance <= best_distance) {
                best_distance = distance;
                best = z * w_ + x;
            }
        }
    return best;
}

std::optional<engine::Vec3f> WalkMap::nearest(const engine::Vec3f& p, float tolerance) const {
    const int cell = nearest_reached(p, tolerance);
    if (cell < 0) return std::nullopt;
    return point(cell);
}

std::optional<engine::Vec3f> WalkMap::reached_inside(float min_x, float min_z, float max_x, float max_z) const {
    const engine::Vec3f middle{(min_x + max_x) * 0.5f, 0.0f, (min_z + max_z) * 0.5f};
    std::optional<engine::Vec3f> best;
    float best_distance = std::numeric_limits<float>::infinity();
    const int x0 = std::max(0, int(std::ceil((min_x - x0_) / Grid::step))), x1 = std::min(w_ - 1, int(std::floor((max_x - x0_) / Grid::step)));
    const int z0 = std::max(0, int(std::ceil((min_z - z0_) / Grid::step))), z1 = std::min(h_ - 1, int(std::floor((max_z - z0_) / Grid::step)));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            if (parent_[std::size_t(z * w_ + x)] == -2) continue;
            const auto q = point(z * w_ + x);
            const float distance = std::hypot(q.x - middle.x, q.z - middle.z);
            if (distance < best_distance) {
                best_distance = distance;
                best = q;
            }
        }
    return best;
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
        const float distance = std::hypot(q.x - p.x, q.z - p.z);
        if (distance < best) {
            best = distance;
            end = cell;
        }
    }
    std::vector<int> cells;
    for (int at = end; at >= 0; at = parent_[std::size_t(at)]) cells.push_back(at);
    std::reverse(cells.begin(), cells.end());
    // Every third cell, the take-off and landing cells of jumps, and the end.
    std::vector<Waypoint> thinned;
    for (std::size_t i = 1; i < cells.size(); ++i) {
        const bool takeoff = i + 1 < cells.size() && jumped_[std::size_t(cells[i + 1])];
        const bool landing = jumped_[std::size_t(cells[i])] != 0;
        if (i % 3 == 0 || takeoff || landing || i + 1 == cells.size()) thinned.push_back({point(cells[i]), landing});
    }
    return thinned;
}

std::vector<WalkMap::Waypoint> WalkMap::route_to(const engine::Vec3f& p, float tolerance) const {
    const int radius = int(std::ceil(tolerance / Grid::step));
    const int centre = nearest_cell(p);
    const int cx = centre % w_, cz = centre / w_;
    int best = -1;
    for (int z = std::max(0, cz - radius); z <= std::min(h_ - 1, cz + radius); ++z)
        for (int x = std::max(0, cx - radius); x <= std::min(w_ - 1, cx + radius); ++x) {
            const int cell = z * w_ + x;
            if (parent_[std::size_t(cell)] == -2) continue;
            const auto q = point(cell);
            if (std::hypot(q.x - p.x, q.z - p.z) > tolerance) continue;
            if (best < 0 || cost_[std::size_t(cell)] < cost_[std::size_t(best)]) best = cell;
        }
    return best < 0 ? route_to(p) : route_to(point(best));
}

std::vector<engine::Vec3f> WalkMap::samples() const {
    std::vector<engine::Vec3f> out;
    for (int cell = 0; cell < w_ * h_; ++cell)
        if (parent_[std::size_t(cell)] != -2 && (cell % w_) % 3 == 0 && (cell / w_) % 3 == 0) out.push_back(point(cell));
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

bool ExploreBot::quicksand_mat(engine::Engine& engine, const std::string& mat) {
    const auto it = quicksands_.find(mat);
    if (it != quicksands_.end()) return it->second;
    const std::string script = mat + ".lua";
    const bool sand = engine.script_mentions(script, "QuickSand") || engine.script_mentions(script, "QUICKSAND");
    quicksands_[mat] = sand;
    return sand;
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

void ExploreBot::plan(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame) {
    const void* previous = target_;
    const std::string previous_mat = target_mat_;
    planned_ = frame;
    route_.clear();
    boss_stand_.reset();
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
        const float grow = pit && sandy ? 25.0f : 4.0f, above = pit && sandy ? 35.0f : 5.0f;
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
    const WalkMap map(hitland->world(), me, 700.0f, flags, avoid, hitland->delivery_width(player.info_name, player.index));
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
    const bool defending = frame < defend_until_;
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
    // Live enemies (structures such as towers first), then pickups: the
    // cheapest to walk to. While defending enemies are only struck in
    // passing (update), except structures.
    for (int pass = 0; pass < 2; ++pass) {
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        float best_cost = std::numeric_limits<float>::infinity();
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node) continue;
            if (pass == 0 ? (e->category != "ENEMY" || e->hp <= 0) : (e->category != "ITEM" || e->item_table < 0)) continue;
            // Hits reach a model through its hit script; without one (a
            // boss's head or decoration) it cannot be hurt.
            if (pass == 0 && !engine.has_script("hit_" + e->info_name + ".lua")) continue;
            if (left_alone(e.get()) || unkillable_.count(e.get())) continue;
            const auto at = position_of(*e);
            const bool structure = e->info_name.rfind("g_", 0) == 0;
            const bool boss = e->info_name.rfind("f_", 0) == 0;
            if (pass == 0 && defending && !structure && !boss) continue;
            if (hold && !on_hold(at)) continue;
            // Near a pit in a quicksand basin: struck only in passing.
            if (sandy && in_pit(at, 0.0f)) {
                unreachable_[e.get()] = frame + 120;
                continue;
            }
            // A structure's or a boss's body keeps the player further off (a
            // boss may stand on a platform its body covers).
            float cost = map.cost_to(at, pass == 1 ? 8.0f : boss ? 60.0f : structure ? 30.0f : 14.0f);
            if (!std::isfinite(cost)) {
                unreachable_[e.get()] = frame + 300;
                continue;
            }
            if (structure) cost *= 0.3f;
            cost = keep(e.get(), cost);
            if (cost < best_cost) {
                best_cost = cost;
                best = e.get();
            }
        }
        if (best) {
            go(position_of(*best), best, (pass == 0 ? "enemy " : "item ") + best->info_name);
            waiting_since_ = -1;
            // A structure (a cannon whose beam or shots run along its +Z) is
            // walked up to from the side: the cheapest reached point next to
            // its body at least 30 degrees off that axis.
            if (pass == 0 && best->info_name.rfind("g_", 0) == 0 && best->node) {
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
        if (flat(*goal_centre, me) > 280.0f) {
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
        if (sandy) {
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
    // Gimmicks with a hit script: floor switches (an AIM map, no bullet
    // boxes: stood on while their AI is live), wall switches (a talk box:
    // A facing them) and things that take hits (boxes, breakable walls:
    // attacked). Each gets a budget; switches are tried again later. Untried
    // gimmicks come before the mats, tried ones after them.
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
            float cost = keep(e.get(), map.cost_to(at, kind == Gimmick::step ? 6.0f : 20.0f));
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
        route_ = map.route_to(position_of(*best), best_kind == Gimmick::step ? 6.0f : 20.0f);
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
                std::printf("      model %s#%d %s hp %d (%.0f,%.0f,%.0f)\n", e->info_name.c_str(), e->index, e->category.c_str(), e->hp,
                            e->node->matrix(3, 0), e->node->matrix(3, 1), e->node->matrix(3, 2));
    }
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
        clears_seen_.clear();
        hold_left_.clear();
        hold_mat_.clear();
        switched_.clear();
        health_.clear();
        lowest_hp_.clear();
        fruitless_.clear();
        fruitless_runs_.clear();
        approach_.clear();
        unkillable_.clear();
        regenerating_.clear();
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
    }
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
    if (airborne && frame - planned_ < 120) {
        // keep the plan while in the air
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
    const float close = !target ? 8.0f : gimmick_ == Gimmick::step ? 3.0f : (gimmick_ == Gimmick::talk || gimmick_ == Gimmick::tech) ? 12.0f : 25.0f;
    // Below a boss whose body starts above the swing: the route up onto its
    // floor is followed however close the boss is (straight at it only runs
    // into the step under its body).
    const bool climbing = boss_stand_ && target && aim.y > me.y + 8.0f;
    if ((flat(aim, me) > close || climbing) && next_ < route_.size()) {
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
            if (span > 21.0f) {
                // A long jump: from a run-up point 8 units back, run along the
                // line and jump at the edge.
                const float ux = (landing.x - takeoff.x) / span, uz = (landing.z - takeoff.z) / span;
                const float along = (me.x - takeoff.x) * ux + (me.z - takeoff.z) * uz;
                const float lateral = std::abs((me.z - takeoff.z) * ux - (me.x - takeoff.x) * uz);
                const engine::Vec3f runup{takeoff.x - ux * 8.0f, takeoff.y, takeoff.z - uz * 8.0f};
                if (runup_ready_ && (lateral > 3.0f || along < -12.0f)) runup_ready_ = false; // knocked off the line
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
            } else if (!ready) {
                decision.x = takeoff.x;
                decision.z = takeoff.z;
                decision.reach = 1.0f;
                stalled_ = 0; // waiting on purpose, for 900 frames at most
                if (platform_wait_since_ < 0) platform_wait_since_ = frame;
                if (frame - platform_wait_since_ < 900) watch.since = frame;
            } else if (flat(takeoff, me) < 4.0f && !airborne) {
                platform_wait_since_ = -1;
                decision.jump = true;
                leap_ = landing;
                leap_frame_ = frame;
            }
        }
    } else {
        decision.x = aim.x;
        decision.z = aim.z;
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
        // (facing it) and press A; the attack's talk box reaches 8.5 ahead.
        decision.reach = 0.25f;
        if (flat(aim, me) <= 12.0f || (flat(aim, me) <= 15.0f && stalled_ > 5)) {
            decision.press_a = frame % 35 < 3;
            if (decision.press_a) spend(12);
            else watch.since = frame;
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
            if (!fresh && target->hp > low->second) regenerating_.insert(target_);
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
    if (frame < defend_until_ && !decision.attack && !decision.press_a && models)
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && e->hp > 0 && flat(position_of(*e), me) < 12.0f &&
                !(unreachable_.count(e.get()) && frame < unreachable_[e.get()]) && !unkillable_.count(e.get())) {
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
    if (stalled_ >= 25 && !decision.attack && !((gimmick_ == Gimmick::talk || gimmick_ == Gimmick::tech) && flat(aim, me) <= 15.0f) &&
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
