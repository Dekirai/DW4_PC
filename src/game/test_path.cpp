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
    static constexpr float climb = 9.0f;  // largest height step between cells
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
        if (ya < -99999.0f || yb < -99999.0f || std::abs(ya - yb) > climb) return false;
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
    constexpr float step = Grid::step, climb = Grid::climb;
    x0_ = from.x - range;
    z0_ = from.z - range;
    w_ = h_ = int(2 * range / step) + 1;
    const std::size_t cells = std::size_t(w_) * std::size_t(h_);
    floor_.assign(cells, -100000.0f);
    parent_.assign(cells, -2);
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
    // Pass 1: flood the reachable cells, recording which of the eight steps
    // out of each cell are open.
    static constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, kDz[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    std::vector<std::uint8_t> open_steps(cells, 0);
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
            const float bx = x0_ + float(nx) * step, bz = z0_ + float(nz) * step;
            if (blocked(bx, bz)) continue;
            const float yb = reached[std::size_t(next_cell)] ? floor_[std::size_t(next_cell)] : floor_from(next_cell, ya);
            if (yb < -99999.0f || std::abs(ya - yb) > climb) continue;
            const float ax = x0_ + float(x) * step, az = z0_ + float(z) * step;
            const float length = std::hypot(bx - ax, bz - az);
            const engine::Vec3f direction{(bx - ax) / length, 0.0f, (bz - az) / length};
            const engine::Vec3f middle{(ax + bx) * 0.5f, std::max(ya, yb), (az + bz) * 0.5f};
            if (const auto wall = world.check_wall(middle, direction, width, 4.0f, wall_flags, false); wall.hit) {
                for (const auto& hit : wall.hits) {
                    auto& blocker = blockers_[{hit.info, hit.child}];
                    if (!blocker.count++) {
                        blocker.first = hit.position;
                        blocker.normal = hit.normal;
                    }
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
    // Pass 3: cheapest routes from the start; cells beside walls cost extra.
    std::vector<float> cost(cells, std::numeric_limits<float>::infinity());
    cost[std::size_t(start)] = 0.0f;
    parent_[std::size_t(start)] = -1;
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    open.push({0.0f, start});
    while (!open.empty()) {
        const auto [g, at] = open.top();
        open.pop();
        if (g > cost[std::size_t(at)]) continue;
        for (int d = 0; d < 8; ++d) {
            if (!(open_steps[std::size_t(at)] & (1u << d))) continue;
            const int next_cell = (at / w_ + kDz[d]) * w_ + at % w_ + kDx[d];
            const auto near_wall = wall_distance[std::size_t(next_cell)];
            const float penalty = near_wall == 0 ? 4.0f : near_wall == 1 ? 1.5f : 0.0f;
            const float next = g + ((d & 1) ? 1.4142f : 1.0f) + penalty;
            if (next >= cost[std::size_t(next_cell)]) continue;
            cost[std::size_t(next_cell)] = next;
            parent_[std::size_t(next_cell)] = at;
            open.push({next, next_cell});
        }
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

std::optional<engine::Vec3f> WalkMap::nearest(const engine::Vec3f& p, float tolerance) const {
    const int radius = int(std::ceil(tolerance / Grid::step));
    const int centre = nearest_cell(p);
    const int cx = centre % w_, cz = centre / w_;
    std::optional<engine::Vec3f> best;
    float best_distance = tolerance;
    for (int z = std::max(0, cz - radius); z <= std::min(h_ - 1, cz + radius); ++z)
        for (int x = std::max(0, cx - radius); x <= std::min(w_ - 1, cx + radius); ++x) {
            if (parent_[std::size_t(z * w_ + x)] == -2) continue;
            const auto q = point(z * w_ + x);
            const float distance = std::hypot(q.x - p.x, q.z - p.z);
            if (distance <= best_distance) {
                best_distance = distance;
                best = q;
            }
        }
    return best;
}

std::vector<engine::Vec3f> WalkMap::route_to(const engine::Vec3f& p) const {
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
    std::vector<engine::Vec3f> points;
    for (int at = end; at >= 0; at = parent_[std::size_t(at)]) points.push_back(point(at));
    std::reverse(points.begin(), points.end());
    std::vector<engine::Vec3f> thinned;
    for (std::size_t i = 3; i < points.size(); i += 3) thinned.push_back(points[i]);
    if (!points.empty() && (thinned.empty() || thinned.back().x != points.back().x || thinned.back().z != points.back().z))
        thinned.push_back(points.back());
    return thinned;
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
} // namespace

void ExploreBot::plan(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame) {
    planned_ = frame;
    route_.clear();
    next_ = 0;
    target_ = nullptr;
    to_goal_ = false;
    striking_ = false;
    what_ = "nothing";
    goal_point_ = position_of(player);
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (!hitland || !models) return;
    const WithoutLimitCircle without_limit(*hitland);
    const auto me = position_of(player);
    const auto flags = hitland->delivery_flags(player.info_name, player.index);
    const std::string goal_key = mat_key(goal);
    std::vector<AvoidBox> avoid;
    std::vector<engine::MatCtrlManager::MatBox> boxes;
    if (mats) boxes = mats->mat_boxes();
    for (const auto& box : boxes) {
        // Other stage change mats would open their prompt.
        const auto key = mat_key(box.name);
        if (key.empty() || key[0] != 'z' || key == goal_key) continue;
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
    // Live enemies, then pickups: the nearest one that can be walked to.
    for (int pass = 0; pass < 2; ++pass) {
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        float best_distance = 1e9f;
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node) continue;
            if (pass == 0 ? (e->category != "ENEMY" || e->hp <= 0) : (e->category != "ITEM" || e->item_table < 0)) continue;
            if (const auto it = unreachable_.find(e.get()); it != unreachable_.end() && frame - it->second < 300) continue;
            const auto at = position_of(*e);
            if (!map.nearest(at, pass == 0 ? 14.0f : 8.0f)) {
                unreachable_[e.get()] = frame;
                continue;
            }
            if (flat(at, me) < best_distance) {
                best_distance = flat(at, me);
                best = e.get();
            }
        }
        if (best) {
            go(position_of(*best), best, (pass == 0 ? "enemy " : "item ") + best->info_name);
            return;
        }
    }
    // The goal mat once it can be reached.
    std::optional<engine::Vec3f> goal_centre;
    for (const auto& box : boxes)
        if (mat_key(box.name) == goal_key)
            goal_centre = engine::Vec3f{(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f};
    if (goal_centre) {
        if (map.nearest(*goal_centre, 8.0f)) {
            go(*goal_centre, nullptr, "goal " + goal);
            to_goal_ = true;
            return;
        }
        if (flat(*goal_centre, me) > 280.0f) {
            const auto path = find_walk_path(hitland->world(), me, *goal_centre, flags);
            if (!path.empty() && path.back().x == goal_centre->x && path.back().z == goal_centre->z) {
                route_ = path;
                goal_point_ = *goal_centre;
                what_ = "goal " + goal + " (far)";
                to_goal_ = true;
                return;
            }
        }
    }
    // Gimmicks that take hits (boxes, buttons, breakable walls): each is
    // attacked for a while, then left alone.
    {
        const engine::ModelCtrlManager::ModelRam* best = nullptr;
        float best_distance = 1e9f;
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node || !e->hit || !e->control_enabled || e->hp <= 0 || e->info_name.rfind("g_", 0) != 0) continue;
            if (e->category == "ITEM" || e->category == "ENEMY" || e->category == "PLAYER") continue;
            if (!engine.has_script("hit_" + e->info_name + ".lua")) continue;
            if (const auto it = struck_.find(e.get()); it != struck_.end() && it->second >= 240) continue;
            if (const auto it = unreachable_.find(e.get()); it != unreachable_.end() && frame - it->second < 300) continue;
            const auto at = position_of(*e);
            if (!map.nearest(at, 20.0f)) continue;
            if (flat(at, me) < best_distance) {
                best_distance = flat(at, me);
                best = e.get();
            }
        }
        if (best) {
            go(position_of(*best), best, "gimmick " + best->info_name + "#" + std::to_string(best->index));
            striking_ = true;
            return;
        }
    }
    // Mats not stood on yet (they start enemy waves and events).
    const engine::MatCtrlManager::MatBox* best_mat = nullptr;
    float best_distance = 1e9f;
    for (const auto& box : boxes) {
        const auto key = mat_key(box.name);
        if (key.empty() || key[0] == 'z' || mats_done_.count(box.name)) continue;
        const engine::Vec3f centre{(box.min.x + box.max.x) * 0.5f, me.y, (box.min.z + box.max.z) * 0.5f};
        if (!map.nearest(centre, 6.0f)) continue;
        if (flat(centre, me) < best_distance) {
            best_distance = flat(centre, me);
            best_mat = &box;
        }
    }
    if (best_mat) {
        go({(best_mat->min.x + best_mat->max.x) * 0.5f, me.y, (best_mat->min.z + best_mat->max.z) * 0.5f}, nullptr, "mat " + best_mat->name);
        return;
    }
    // Unvisited ground, preferring the goal's side.
    std::optional<engine::Vec3f> frontier;
    float best_score = 1e9f;
    for (const auto& q : map.samples()) {
        if (visited_.count(coarse(q.x, q.z))) continue;
        const float score = flat(q, me) + (goal_centre ? 0.25f * flat(q, *goal_centre) : 0.0f);
        if (score < best_score) {
            best_score = score;
            frontier = q;
        }
    }
    if (frontier) {
        go(*frontier, nullptr, "explore");
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
                        mats_done_.count(box.name) ? "done" : "open",
                        map.nearest({(box.min.x + box.max.x) * 0.5f, me.y, (box.min.z + box.max.z) * 0.5f}, 6.0f) ? " reachable" : "");
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->category != "PLAYER" && e->category != "INTERFACE")
                std::printf("      model %s#%d %s hp %d (%.0f,%.0f,%.0f)\n", e->info_name.c_str(), e->index, e->category.c_str(), e->hp,
                            e->node->matrix(3, 0), e->node->matrix(3, 1), e->node->matrix(3, 2));
    }
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
        planned_ = -1000;
    }
    for (const float ox : {-8.0f, 8.0f})
        for (const float oz : {-8.0f, 8.0f}) visited_.insert(coarse(me.x + ox, me.z + oz));
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (auto* mats = engine.get<engine::MatCtrlManager>())
        for (const auto& box : mats->mat_boxes())
            if (me.x >= box.min.x - 2 && me.x <= box.max.x + 2 && me.z >= box.min.z - 2 && me.z <= box.max.z + 2) mats_done_.insert(box.name);
    // The planned enemy or item must still be there.
    const engine::ModelCtrlManager::ModelRam* target = nullptr;
    if (target_ && models)
        for (const auto& e : models->rams())
            if (e.get() == target_ && e->generated && e->node && (e->category == "ITEM" || e->hp > 0)) target = e.get();
    stalled_ = std::hypot(me.x - last_.x, me.z - last_.z) < 0.1f ? stalled_ + 1 : 0;
    last_ = me;
    const bool lost = target_ && !target;
    if (lost || frame - planned_ >= 45 || (!target && next_ >= route_.size() && flat(goal_point_, me) < 6.0f) || stalled_ >= 90) {
        if (stalled_ >= 90) {
            stalled_ = 0;
            // Walking against something: give the spot up.
            visited_.insert(coarse(goal_point_.x, goal_point_.z));
            if (target_) unreachable_[target_] = frame;
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
    while (next_ < route_.size() && flat(route_[next_], me) < 5.0f) ++next_;
    const float close = target ? 25.0f : 8.0f;
    if (flat(aim, me) > close && next_ < route_.size()) {
        decision.x = route_[next_].x;
        decision.z = route_[next_].z;
    } else {
        decision.x = aim.x;
        decision.z = aim.z;
    }
    if (target && (target->category == "ENEMY" || striking_)) {
        const float d2 = (aim.x - me.x) * (aim.x - me.x) + (aim.z - me.z) * (aim.z - me.z);
        decision.reach = flat(aim, me) <= close ? 400.0f : 4.0f;
        if (d2 <= 400.0f || (d2 <= 6400.0f && stalled_ > 20)) {
            decision.attack = true;
            decision.jump_attack = aim.y > me.y + 8.0f;
            if (striking_) ++struck_[target];
        }
    } else if (target) {
        // A pickup: some are taken by touch, others (keys) with A.
        decision.reach = flat(aim, me) <= close ? 9.0f : 4.0f;
        if (flat(aim, me) <= 3.0f) decision.pick_up = true;
    }
    if (to_goal_ && flat(goal_point_, me) < 20.0f) decision.confirm = true;
    // Without control (a text window or a prompt) only A goes through: on
    // another stage change mat the prompt is answered "No" (down, then A),
    // elsewhere text windows are advanced.
    no_control_ = player.control_enabled ? 0 : no_control_ + 1;
    if (player.control_enabled) declined_ = false;
    else {
        decision.attack = decision.pick_up = false;
        bool on_other_exit = false;
        if (auto* mats = engine.get<engine::MatCtrlManager>())
            for (const auto& box : mats->mat_boxes()) {
                const auto key = mat_key(box.name);
                if (!key.empty() && key[0] == 'z' && key != mat_key(goal) && me.x >= box.min.x - 2 && me.x <= box.max.x + 2 &&
                    me.z >= box.min.z - 2 && me.z <= box.max.z + 2)
                    on_other_exit = true;
            }
        if (on_other_exit) {
            decision.confirm = false;
            if (!declined_ && no_control_ >= 40 && no_control_ < 43) decision.press_down = true;
            if (!declined_ && no_control_ == 60) {
                decision.press_a = true;
                declined_ = true;
            }
        } else if (!decision.confirm && no_control_ > 90 && no_control_ % 45 < 3) {
            decision.press_a = true;
        }
    }
    // Stuck at a corner (walls stop the player instead of sliding it):
    // sidestep left, right or back for a moment, then take the route again.
    if (stalled_ >= 25 && !decision.attack && frame >= escape_until_) {
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
