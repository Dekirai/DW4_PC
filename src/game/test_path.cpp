#include "test_path.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <queue>

namespace dw4::game {

std::vector<engine::Vec3f> find_walk_path(const engine::CollisionWorld& world, const engine::Vec3f& from, const engine::Vec3f& to,
                                          std::uint32_t wall_flags) {
    constexpr float step = 5.0f;       // grid spacing
    constexpr float climb = 9.0f;      // largest height step between cells
    constexpr float margin = 160.0f;   // searched area around start and goal
    constexpr float radius = 2.5f;     // clearance kept from walls
    const std::uint32_t walls = wall_flags; // the player's wall kinds
    const float x0 = std::min(from.x, to.x) - margin, z0 = std::min(from.z, to.z) - margin;
    const int w = std::min(400, int((std::abs(from.x - to.x) + 2 * margin) / step) + 1);
    const int h = std::min(400, int((std::abs(from.z - to.z) + 2 * margin) / step) + 1);
    const float reference = std::max(from.y, to.y);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> floor(std::size_t(w) * std::size_t(h), nan);
    auto cell_at = [&](float x, float z) {
        const int cx = int(std::lround((x - x0) / step)), cz = int(std::lround((z - z0) / step));
        return std::pair<int, int>{std::clamp(cx, 0, w - 1), std::clamp(cz, 0, h - 1)};
    };
    auto floor_of = [&](int cx, int cz) -> float {
        auto& slot = floor[std::size_t(cz) * std::size_t(w) + std::size_t(cx)];
        if (!std::isnan(slot)) return slot;
        const engine::Vec3f p{x0 + float(cx) * step, reference + 20.0f, z0 + float(cz) * step};
        const auto land = world.check_land(p, 1.0f, 400.0f, 1.0f);
        slot = land.found ? land.position.y : -100000.0f;
        return slot;
    };
    auto passable = [&](int ax, int az, int bx, int bz) {
        const float ya = floor_of(ax, az), yb = floor_of(bx, bz);
        if (ya < -99999.0f || yb < -99999.0f || std::abs(ya - yb) > climb) return false;
        const engine::Vec3f a{x0 + float(ax) * step, ya, z0 + float(az) * step};
        const engine::Vec3f b{x0 + float(bx) * step, yb, z0 + float(bz) * step};
        engine::Vec3f direction{b.x - a.x, 0.0f, b.z - a.z};
        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        direction = {direction.x / length, 0.0f, direction.z / length};
        const engine::Vec3f middle{(a.x + b.x) * 0.5f, std::max(ya, yb), (a.z + b.z) * 0.5f};
        return !world.check_wall(middle, direction, radius, 4.0f, walls, false).hit;
    };
    if (std::getenv("DW4_TRACE_GRID")) {
        // Probe the column straight south of the start.
        const auto [cx, cz] = cell_at(from.x, from.z);
        for (int z = cz; z > std::max(0, cz - 30); --z)
            std::printf("grid (%.0f,%.0f) floor %.1f -> next %d\n", x0 + float(cx) * step, z0 + float(z) * step, floor_of(cx, z),
                        passable(cx, z, cx, z - 1) ? 1 : 0);
    }
    const auto [sx, sz] = cell_at(from.x, from.z);
    const auto [gx, gz] = cell_at(to.x, to.z);
    const auto index = [&](int x, int z) { return z * w + x; };
    std::vector<float> cost(std::size_t(w) * std::size_t(h), std::numeric_limits<float>::infinity());
    std::vector<int> parent(std::size_t(w) * std::size_t(h), -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    auto heuristic = [&](int x, int z) { return std::hypot(float(x - gx), float(z - gz)); };
    cost[std::size_t(index(sx, sz))] = 0.0f;
    open.push({heuristic(sx, sz), index(sx, sz)});
    int reached = -1, best = index(sx, sz);
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
                if (next >= cost[std::size_t(index(nx, nz))]) continue;
                if (!passable(x, z, nx, nz)) continue;
                cost[std::size_t(index(nx, nz))] = next;
                parent[std::size_t(index(nx, nz))] = at;
                open.push({next + heuristic(nx, nz), index(nx, nz)});
            }
    }
    // Without a full route, walk to the closest reached point.
    const int end = reached >= 0 ? reached : best;
    if (end == index(sx, sz)) return {};
    std::vector<engine::Vec3f> points;
    for (int at = end; at >= 0; at = parent[std::size_t(at)]) {
        const int x = at % w, z = at / w;
        points.push_back({x0 + float(x) * step, floor_of(x, z), z0 + float(z) * step});
    }
    std::reverse(points.begin(), points.end());
    // Thin out: every fourth point and the end.
    std::vector<engine::Vec3f> thinned;
    for (std::size_t i = 4; i < points.size(); i += 4) thinned.push_back(points[i]);
    if (reached >= 0) thinned.push_back(to);
    else if (!points.empty()) thinned.push_back(points.back());
    return thinned;
}

} // namespace dw4::game
