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
    auto blocked = [&](float x, float z) {
        for (const auto& box : avoid)
            if (x >= box.min_x && x <= box.max_x && z >= box.min_z && z <= box.max_z) return true;
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
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d], nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= w_ || nz >= h_) continue;
            const int next_cell = nz * w_ + nx;
            const auto b = centre(nx, nz);
            if (blocked(b.x, b.z)) continue;
            const float yb = reached[std::size_t(next_cell)] ? floor_[std::size_t(next_cell)] : floor_from(next_cell, ya);
            if (yb < -99999.0f) continue; // outside the level
            // A pit (deeper than a ledge drop): try to jump it.
            if (ya - yb > drop) {
                // The jump (X) carries about 28 units over 40 frames and rises
                // 14: gaps of up to four cells (20 units, diagonals three).
                const int longest = (d & 1) ? 3 : 4;
                for (int k = 2; k <= longest; ++k) {
                    const int lx = x + kDx[d] * k, lz = z + kDz[d] * k;
                    if (lx < 0 || lz < 0 || lx >= w_ || lz >= h_) break;
                    const int land = lz * w_ + lx;
                    const auto l = centre(lx, lz);
                    if (blocked(l.x, l.z)) break;
                    const float yl = floor_from(land, ya + 5.0f);
                    if (yl < -99999.0f) break;
                    if (ya - yl > drop) continue; // still over the pit
                    if (yl - ya > 12.0f || ya - yl > 30.0f) break;
                    bool clear = true;
                    for (int i = 0; i < k && clear; ++i)
                        clear = !wall_between(x + kDx[d] * i, z + kDz[d] * i, x + kDx[d] * (i + 1), z + kDz[d] * (i + 1), ya);
                    if (!clear) break;
                    jumps.push_back({at, land, float(k) * ((d & 1) ? 1.4142f : 1.0f) + 8.0f});
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
                if (yu < -99999.0f || yu - ya <= climb || yu - ya > 12.5f || wall_between(x, z, nx, nz, yu + 1.0f)) continue;
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
bool switched_on(const engine::ModelCtrlManager::ModelRam& ram) { return ram.seq_name == "SQ_SWON" || ram.seq_name == "SQ_ONLOOP"; }
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
    const bool hazard = engine.script_mentions(script, "QuickSand") || engine.script_mentions(script, "QUICKSAND") ||
                        (engine.script_mentions(script, "ADDHP") && engine.script_mentions(script, "REMOVE"));
    hazards_[mat] = hazard;
    return hazard;
}

bool ExploreBot::exit_mat(engine::Engine& engine, const std::string& mat) {
    const auto it = exits_.find(mat);
    if (it != exits_.end()) return it->second;
    const bool exit = engine.script_mentions(mat + ".lua", "funcStageChange") && !portal(engine, mat);
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
    next_ = 0;
    target_ = nullptr;
    target_mat_.clear();
    target_portal_.clear();
    to_goal_ = false;
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
    for (const auto& box : boxes) {
        // Other stage change mats would open their prompt.
        const auto key = mat_key(box.name);
        if (goals.count(key) || (!exit_mat(engine, box.name) && !hazard_mat(engine, box.name))) continue;
        const AvoidBox a{box.min.x - 4.0f, box.min.z - 4.0f, box.max.x + 4.0f, box.max.z + 4.0f};
        if (me.x >= a.min_x && me.x <= a.max_x && me.z >= a.min_z && me.z <= a.max_z) continue;
        avoid.push_back(a);
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
    // The kept target counts as nearer, so two equally far ones do not take
    // turns.
    auto keep = [&](const void* target, float cost) { return target == previous ? cost * 0.6f : cost; };
    const bool defending = frame < defend_until_;
    // Live enemies (structures such as towers first), then pickups: the
    // cheapest to walk to. While defending enemies are only struck in
    // passing (update), except structures.
    for (int pass = 0; pass < 2; ++pass) {
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        float best_cost = std::numeric_limits<float>::infinity();
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node) continue;
            if (pass == 0 ? (e->category != "ENEMY" || e->hp <= 0) : (e->category != "ITEM" || e->item_table < 0)) continue;
            if (left_alone(e.get())) continue;
            const auto at = position_of(*e);
            const bool structure = e->info_name.rfind("g_", 0) == 0;
            if (pass == 0 && defending && !structure) continue;
            // A structure's body keeps the player further off.
            float cost = map.cost_to(at, pass == 1 ? 8.0f : structure ? 30.0f : 14.0f);
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
            return;
        }
    }
    // Defeated enemies stay until their model is removed; a wave counts as
    // cleared only then, and leaving its mat earlier stops the generator.
    if (!defending && frame >= wait_skip_until_)
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && e->hp <= 0 && flat(position_of(*e), me) < 200.0f) {
                goal_point_ = me;
                what_ = "wait for the defeated";
                if (waiting_since_ < 0) waiting_since_ = frame;
                if (frame - waiting_since_ > 900) wait_skip_until_ = frame + 1800; // a body that never goes
                return;
            }
    waiting_since_ = -1;
    // The goal mat once it can be reached.
    // (Of several, a reachable one; the point walked to is reached floor
    // inside the mat, whose middle may lie in a wall.)
    std::optional<engine::Vec3f> goal_centre, goal_inside;
    for (const auto& box : boxes) {
        if (!goals.count(mat_key(box.name))) continue;
        if (!goal_centre) goal_centre = mat_centre(box, (box.min.y + box.max.y) * 0.5f);
        if (!goal_inside) goal_inside = map.reached_inside(box.min.x + 1.0f, box.min.z + 1.0f, box.max.x - 1.0f, box.max.z - 1.0f);
    }
    if (goal_centre) {
        if (goal_inside) {
            go(*goal_inside, nullptr, "goal " + goal);
            to_goal_ = true;
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
                // Walls that break only to an element (fire, thunder) need a
                // technique or chip the bot does not use.
                if (engine.script_mentions("hit_" + e->info_name + ".lua", "BLPgetAttackZokusei")) continue;
                kind = Gimmick::strike;
            } else {
                continue;
            }
            // A switch seen switched on is done for a long while (pairs that
            // power each other back on would otherwise take turns forever).
            if (kind != Gimmick::strike)
                if (const auto on = switched_.find(e.get()); on != switched_.end() && frame - on->second < 6000) continue;
            const int budget = kind == Gimmick::strike ? 240 : kind == Gimmick::talk ? 12 : 60;
            const auto spent = struck_.find(e.get());
            if (spent != struck_.end() && spent->second >= budget) {
                if (kind == Gimmick::strike || frame - struck_at_[e.get()] < 900) continue;
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
        static constexpr const char* kinds[] = {"", "strike ", "press ", "step on "};
        go(position_of(*best), best, std::string(kinds[int(best_kind)]) + best->info_name + "#" + std::to_string(best->index));
        route_ = map.route_to(position_of(*best), best_kind == Gimmick::step ? 6.0f : 20.0f);
        gimmick_ = best_kind;
        return true;
    };
    if (pick_gimmick(false)) return;
    // Mats not stood on yet (they start enemy waves and events).
    const engine::MatCtrlManager::MatBox* best_mat = nullptr;
    float best_cost = std::numeric_limits<float>::infinity();
    for (const auto& box : boxes) {
        const auto key = mat_key(box.name);
        if (key.empty() || goals.count(key) || exit_mat(engine, box.name) || portal(engine, box.name) || hazard_mat(engine, box.name) ||
            mats_done_.count(box.name))
            continue;
        float cost = map.cost_to(mat_centre(box, me.y), 6.0f);
        if (box.name == previous_mat) cost *= 0.6f;
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
        unreachable_.clear();
        planned_ = -1000;
        what_ = "forget the explored ground";
        return;
    }
    if (goal_centre) go(*goal_centre, nullptr, "goal " + goal + " (closest)");
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
    if (target_) unreachable_[target_] = frame + 1800;
    if (!target_mat_.empty()) mats_done_.insert(target_mat_);
    if (!target_portal_.empty()) portals_taken_.insert(target_portal_);
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
        portals_.clear();
        exits_.clear();
        hazards_.clear();
        switched_.clear();
        health_.clear();
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
    // A generator cleared for the first time is progress.
    if (frame % 30 == 0)
        if (auto* generators = engine.get<engine::GeneraterManager>())
            for (const auto& [name, done] : generators->cleared())
                if (done && cleared_generators_.insert(name).second) progress_at_ = frame;
    const auto visited_before = visited_.size();
    for (const float ox : {-8.0f, 8.0f})
        for (const float oz : {-8.0f, 8.0f}) visited_.insert(coarse(me.x + ox, me.z + oz));
    if (visited_.size() != visited_before) progress_at_ = frame;
    auto* models = engine.get<engine::ModelCtrlManager>();
    // A mat counts as stood on near its centre or after a while inside (a
    // radar-started wave needs the player well inside).
    if (auto* mats = engine.get<engine::MatCtrlManager>())
        for (const auto& box : mats->mat_boxes())
            if (me.x >= box.min.x && me.x <= box.max.x && me.z >= box.min.z && me.z <= box.max.z) {
                if ((++mat_frames_[box.name] > 150 || flat(mat_centre(box, me.y), me) < 15.0f) && mats_done_.insert(box.name).second)
                    progress_at_ = frame;
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
            if (e.get() == target_ && ((gimmick_ == Gimmick::talk && switched_on(*e)) || (gimmick_ == Gimmick::step && e->ctrl_mode == 0)))
                switched_[target_] = frame;
    if (target && gimmick_ == Gimmick::strike) {
        auto [it, fresh] = health_.emplace(target_, target->hp);
        if (!fresh && target->hp < it->second) struck_[target_] = 1; // it takes damage: keep at it
        it->second = target->hp;
    }
    if (target_ && !target && gimmick_ != Gimmick::none) {
        // A switch went on (or a gimmick broke): on to the next, e.g. the
        // other switch of a pair that must be pressed within moments.
        struck_[target_] = gimmick_ == Gimmick::strike ? 240 : 60;
        struck_at_[target_] = frame;
        progress_at_ = frame;
    }
    stalled_ = std::hypot(me.x - last_.x, me.z - last_.z) < 0.1f && !held_by_hit(player) ? stalled_ + 1 : 0;
    last_ = me;
    // In the air after a jump: keep steering at the landing.
    if (leap_ && frame - leap_frame_ < 70 && (!player.on_ground || frame - leap_frame_ < 5)) {
        decision.x = leap_->x;
        decision.z = leap_->z;
        decision.reach = 0.25f;
        return decision;
    }
    leap_.reset();
    // A long fight without progress elsewhere (waves that come back until
    // something else is done): fight only nearby enemies for a while.
    if (frame - progress_at_ > 2400 && frame >= defend_until_ && what_.rfind("enemy", 0) == 0) {
        defend_until_ = frame + 2400;
        progress_at_ = frame;
        planned_ = -1000;
    }
    const bool lost = target_ && !target;
    if (lost || frame - planned_ >= 45 || (!target && next_ >= route_.size() && flat(goal_point_, me) < 6.0f) || stalled_ >= 90) {
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
    auto& watch = watch_[watch_key];
    if (frame - watch.seen > 300) watch = {std::numeric_limits<float>::infinity(), frame, frame};
    watch.seen = frame;
    // Progress is measured along the route (waypoints left), so a detour that
    // first leads away is not given up.
    const float remaining = next_ < route_.size() ? float(route_.size() - next_) * 15.0f : flat(aim, me);
    if (remaining < watch.best - 2.0f) {
        watch.best = remaining;
        watch.since = frame;
    }
    const bool watched = gimmick_ != Gimmick::none || !target_mat_.empty() || !target_portal_.empty() || what_ == "explore" ||
                         what_.rfind("item", 0) == 0;
    if (watched && frame - watch.since > 600) {
        if (std::getenv("DW4_TRACE_EXPLORE")) std::printf("[%d] explore gives up %s\n", frame, what_.c_str());
        give_up(frame);
        watch.since = frame;
    }
    while (next_ < route_.size() && flat(route_[next_].point, me) < 5.0f) ++next_;
    const float close = !target ? 8.0f : gimmick_ == Gimmick::step ? 3.0f : gimmick_ == Gimmick::talk ? 12.0f : 25.0f;
    if (flat(aim, me) > close && next_ < route_.size()) {
        decision.x = route_[next_].point.x;
        decision.z = route_[next_].point.z;
        // A gap: jump from the take-off point toward the landing.
        if (route_[next_].jump) {
            const auto& takeoff = next_ > 0 ? route_[next_ - 1].point : me;
            if (flat(takeoff, me) < 4.0f && player.on_ground) {
                decision.jump = true;
                leap_ = route_[next_].point;
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
    } else if (target && gimmick_ == Gimmick::talk) {
        // Wall switches sit inside the wall: keep pushing toward the origin
        // (facing it) and press A; the attack's talk box reaches 8.5 ahead.
        decision.reach = 0.25f;
        if (flat(aim, me) <= 12.0f || (flat(aim, me) <= 15.0f && stalled_ > 5)) {
            decision.press_a = frame % 35 < 3;
            if (decision.press_a) spend(12);
            else watch.since = frame;
        }
    } else if (target && (target->category == "ENEMY" || gimmick_ == Gimmick::strike)) {
        const float d2 = (aim.x - me.x) * (aim.x - me.x) + (aim.z - me.z) * (aim.z - me.z);
        // Structures (towers) are walked up to until their body stops the
        // player; others are attacked from 20 units.
        const bool structure = target->category == "ENEMY" && target->info_name.rfind("g_", 0) == 0;
        decision.reach = structure ? 1.0f : flat(aim, me) <= close ? 400.0f : 4.0f;
        if (d2 <= 400.0f || (d2 <= 6400.0f && stalled_ > 20)) {
            decision.attack = true;
            decision.jump_attack = aim.y > me.y + 8.0f;
            if (gimmick_ == Gimmick::strike && !held_by_hit(player)) spend(240);
        }
    } else if (target) {
        // A pickup: some are taken by touch, others (keys) with A.
        decision.reach = flat(aim, me) <= close ? 9.0f : 4.0f;
        if (flat(aim, me) <= 3.0f) decision.pick_up = true;
    }
    if (to_goal_ && flat(goal_point_, me) < 20.0f) decision.confirm = true;
    // Defending: strike an enemy that comes close, without chasing it.
    if (frame < defend_until_ && !decision.attack && !decision.press_a && models)
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category == "ENEMY" && e->hp > 0 && flat(position_of(*e), me) < 12.0f) {
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
    // Stuck at a corner (walls stop the player instead of sliding it):
    // sidestep left, right or back for a moment, then take the route again.
    // Not while pushing into a wall switch on purpose.
    if (stalled_ >= 25 && !decision.attack && !(gimmick_ == Gimmick::talk && flat(aim, me) <= 15.0f) && frame >= escape_until_) {
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
