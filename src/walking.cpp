#include "dw4/walking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace dw4 {
namespace {
constexpr std::size_t max_meshes = 4096;
constexpr std::size_t max_vertices = 1048576;
constexpr std::size_t max_triangles = 262144;
constexpr std::size_t max_candidates = 8192;
constexpr double coordinate_limit = 1000000;
constexpr double geometric_epsilon = 1e-9;
constexpr double height_epsilon = 1e-5;

void finite(double value, const char* description) {
    if (!std::isfinite(value)) throw std::invalid_argument(description);
}
void coordinate(double value) {
    finite(value, "Walking coordinates must be finite");
    if (std::abs(value) > coordinate_limit)
        throw std::invalid_argument("Walking coordinates exceed the supported range");
}
void nonnegative(double value) {
    finite(value, "Walking distances must be finite");
    if (value < 0 || value > coordinate_limit)
        throw std::invalid_argument("Walking distances exceed the supported range");
}
template<class Triangle>
std::array<double, 3> barycentric(const Triangle& t, double x, double z) {
    const double a = ((t.z[1] - t.z[2]) * (x - t.x[2])
        + (t.x[2] - t.x[1]) * (z - t.z[2])) / t.denominator;
    const double b = ((t.z[2] - t.z[0]) * (x - t.x[2])
        + (t.x[0] - t.x[2]) * (z - t.z[2])) / t.denominator;
    return {a, b, 1 - a - b};
}
template<class Triangle>
double height(const Triangle& t, const std::array<double, 3>& weights) {
    return t.y[0] * weights[0] + t.y[1] * weights[1] + t.y[2] * weights[2];
}
bool reachable(double height_value, double from, double up, double down) {
    return height_value <= from + up + height_epsilon
        && height_value >= from - down - height_epsilon;
}
using Point2 = std::array<double, 2>;
using Point3 = std::array<double, 3>;
double cross(Point2 a, Point2 b) { return a[0] * b[1] - a[1] * b[0]; }
Point2 difference(Point2 a, Point2 b) { return {a[0] - b[0], a[1] - b[1]}; }
double point_segment_distance(Point2 p, Point2 a, Point2 b) {
    const auto ab = difference(b, a), ap = difference(p, a);
    const double length = ab[0] * ab[0] + ab[1] * ab[1];
    const double t = length > geometric_epsilon * geometric_epsilon
        ? std::clamp((ap[0] * ab[0] + ap[1] * ab[1]) / length, 0.0, 1.0) : 0;
    return std::hypot(ap[0] - ab[0] * t, ap[1] - ab[1] * t);
}
double segment_distance(Point2 a, Point2 b, Point2 c, Point2 d) {
    const auto ab = difference(b, a), cd = difference(d, c), ac = difference(c, a);
    const double denominator = cross(ab, cd);
    if (std::abs(denominator) > geometric_epsilon * geometric_epsilon) {
        const double t = cross(ac, cd) / denominator, u = cross(ac, ab) / denominator;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0;
    }
    return std::min({point_segment_distance(a, c, d), point_segment_distance(b, c, d),
        point_segment_distance(c, a, b), point_segment_distance(d, a, b)});
}
std::vector<Point3> clip_height(std::span<const Point3> polygon, double y, bool above) {
    std::vector<Point3> output;
    output.reserve(polygon.size() + 1);
    if (polygon.empty()) return output;
    auto previous = polygon.back();
    bool previous_inside = above ? previous[1] >= y : previous[1] <= y;
    for (const auto& current : polygon) {
        const bool current_inside = above ? current[1] >= y : current[1] <= y;
        if (current_inside != previous_inside) {
            const double t = (y - previous[1]) / (current[1] - previous[1]);
            output.push_back({previous[0] + t * (current[0] - previous[0]), y,
                previous[2] + t * (current[2] - previous[2])});
        }
        if (current_inside) output.push_back(current);
        previous = current; previous_inside = current_inside;
    }
    return output;
}
bool polygon_contains(std::span<const Point2> polygon, Point2 point) {
    bool positive = false, negative = false;
    double area = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const double side = cross(difference(b, a), difference(point, a));
        positive |= side > geometric_epsilon; negative |= side < -geometric_epsilon;
        area += cross(a, b);
    }
    return std::abs(area) > geometric_epsilon && !(positive && negative);
}
double polygon_distance(std::span<const Point2> polygon, Point2 from, Point2 to) {
    if (polygon_contains(polygon, from) || polygon_contains(polygon, to)) return 0;
    double distance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < polygon.size(); ++i)
        distance = std::min(distance, segment_distance(from, to, polygon[i], polygon[(i + 1) % polygon.size()]));
    return distance;
}
}

std::vector<WalkingWorld::Triangle> WalkingWorld::read_triangles(std::span<const MeshData> meshes, bool walls) {
    std::vector<Triangle> triangles;
    if (meshes.size() > max_meshes) throw std::invalid_argument("Too many walking floor meshes");
    std::size_t vertices = 0, triangle_total = 0;
    for (const auto& mesh : meshes) {
        if (mesh.positions.size() > max_vertices - vertices)
            throw std::invalid_argument("Walking floor vertex limit exceeded");
        vertices += mesh.positions.size();
        if (mesh.indices.size() % 3 != 0)
            throw std::invalid_argument("Walking floor indices must form complete triangles");
        const auto count = mesh.indices.size() / 3;
        if (count > max_triangles - triangle_total)
            throw std::invalid_argument("Walking floor triangle limit exceeded");
        triangle_total += count;
        for (const auto& v : mesh.positions) { coordinate(v.x); coordinate(v.y); coordinate(v.z); }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            Triangle triangle;
            for (std::size_t j = 0; j < 3; ++j) {
                if (mesh.indices[i + j] >= mesh.positions.size())
                    throw std::invalid_argument("Walking floor index is out of range");
                const auto v = mesh.positions[mesh.indices[i + j]];
                triangle.x[j] = v.x; triangle.y[j] = v.y; triangle.z[j] = v.z;
            }
            const double ax = triangle.x[1] - triangle.x[0];
            const double ay = triangle.y[1] - triangle.y[0];
            const double az = triangle.z[1] - triangle.z[0];
            const double bx = triangle.x[2] - triangle.x[0];
            const double by = triangle.y[2] - triangle.y[0];
            const double bz = triangle.z[2] - triangle.z[0];
            const double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
            const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (length < geometric_epsilon || (!walls && std::abs(ny) / length < 0.573576436351046))
                continue;
            triangle.denominator = (triangle.z[1] - triangle.z[2]) * (triangle.x[0] - triangle.x[2])
                + (triangle.x[2] - triangle.x[1]) * (triangle.z[0] - triangle.z[2]);
            if (!walls && std::abs(triangle.denominator) < geometric_epsilon) continue;
            triangle.min_x = *std::min_element(triangle.x.begin(), triangle.x.end());
            triangle.max_x = *std::max_element(triangle.x.begin(), triangle.x.end());
            triangle.min_z = *std::min_element(triangle.z.begin(), triangle.z.end());
            triangle.max_z = *std::max_element(triangle.z.begin(), triangle.z.end());
            triangles.push_back(triangle);
        }
    }
    return triangles;
}

WalkingWorld::WalkingWorld(std::span<const MeshData> meshes, std::span<const MeshData> walls)
    : triangles_(read_triangles(meshes, false)), wall_triangles_(read_triangles(walls, true)) {
    if (triangles_.empty()) throw std::invalid_argument("No supported walking floor triangles");
    order_.resize(triangles_.size());
    std::iota(order_.begin(), order_.end(), std::uint32_t{});
    nodes_.reserve(triangles_.size() / 2 + 1);
    build_node(0, static_cast<std::uint32_t>(order_.size()));
    wall_order_.resize(wall_triangles_.size());
    std::iota(wall_order_.begin(), wall_order_.end(), std::uint32_t{});
    wall_nodes_.reserve(wall_triangles_.size() / 2 + 1);
    if (!wall_order_.empty()) build_node(0, static_cast<std::uint32_t>(wall_order_.size()), true);
}

std::uint32_t WalkingWorld::build_node(std::uint32_t begin, std::uint32_t end, bool walls) {
    auto& triangles = walls ? wall_triangles_ : triangles_;
    auto& order = walls ? wall_order_ : order_;
    auto& nodes = walls ? wall_nodes_ : nodes_;
    Node node;
    node.min_x = node.min_z = std::numeric_limits<double>::infinity();
    node.max_x = node.max_z = -std::numeric_limits<double>::infinity();
    node.begin = begin; node.end = end;
    for (auto i = begin; i < end; ++i) {
        const auto& t = triangles[order[i]];
        node.min_x = std::min(node.min_x, t.min_x); node.max_x = std::max(node.max_x, t.max_x);
        node.min_z = std::min(node.min_z, t.min_z); node.max_z = std::max(node.max_z, t.max_z);
    }
    const auto index = static_cast<std::uint32_t>(nodes.size());
    nodes.push_back(node);
    if (end - begin > 8) {
        const bool split_x = node.max_x - node.min_x >= node.max_z - node.min_z;
        const auto middle = begin + (end - begin) / 2;
        std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
            [&](auto a, auto b) {
                const auto& ta = triangles[a]; const auto& tb = triangles[b];
                const double ca = split_x ? ta.min_x + ta.max_x : ta.min_z + ta.max_z;
                const double cb = split_x ? tb.min_x + tb.max_x : tb.min_z + tb.max_z;
                return ca == cb ? a < b : ca < cb;
            });
        const auto left = build_node(begin, middle, walls), right = build_node(middle, end, walls);
        nodes[index].left = left; nodes[index].right = right;
    }
    return index;
}

std::vector<std::uint32_t> WalkingWorld::candidates(double min_x, double max_x,
    double min_z, double max_z, bool walls) const {
    const auto& triangles = walls ? wall_triangles_ : triangles_;
    const auto& order = walls ? wall_order_ : order_;
    const auto& nodes = walls ? wall_nodes_ : nodes_;
    if (walls && nodes.empty()) return {};
    if (nodes.empty()) throw std::logic_error("Walking floor world has no initialized hierarchy");
    std::vector<std::uint32_t> result;
    std::array<std::uint32_t, 64> pending{};
    std::size_t count = 1;
    while (count) {
        const auto& node = nodes[pending[--count]];
        if (node.max_x < min_x - geometric_epsilon || node.min_x > max_x + geometric_epsilon
            || node.max_z < min_z - geometric_epsilon || node.min_z > max_z + geometric_epsilon) continue;
        if (node.left) {
            if (count + 2 > pending.size()) throw std::runtime_error("Walking hierarchy depth limit exceeded");
            pending[count++] = node.left; pending[count++] = node.right;
        } else {
            for (auto i = node.begin; i < node.end; ++i) {
                const auto& t = triangles[order[i]];
                if (t.max_x < min_x - geometric_epsilon || t.min_x > max_x + geometric_epsilon
                    || t.max_z < min_z - geometric_epsilon || t.min_z > max_z + geometric_epsilon) continue;
                if (result.size() == max_candidates)
                    throw std::runtime_error("Walking floor query overlap limit exceeded");
                result.push_back(order[i]);
            }
        }
    }
    return result;
}

std::optional<double> WalkingWorld::floor(double x, double z, double reference_y,
    double max_up, double max_down) const {
    std::optional<double> result;
    for (const auto index : candidates(x, x, z, z)) {
        const auto& triangle = triangles_[index];
        const auto weights = barycentric(triangle, x, z);
        if (*std::min_element(weights.begin(), weights.end()) < -geometric_epsilon) continue;
        const double value = height(triangle, weights);
        if (!reachable(value, reference_y, max_up, max_down)) continue;
        if (!result || std::abs(value - reference_y) < std::abs(*result - reference_y)
            || (std::abs(value - reference_y) == std::abs(*result - reference_y) && value < *result))
            result = value;
    }
    return result;
}

std::optional<float> WalkingWorld::find_floor(float x, float z, float reference_y,
    float max_up, float max_down) const {
    coordinate(x); coordinate(z); coordinate(reference_y);
    nonnegative(max_up); nonnegative(max_down);
    const auto result = floor(x, z, reference_y, max_up, max_down);
    return result ? std::optional<float>(static_cast<float>(*result)) : std::nullopt;
}

std::optional<double> WalkingWorld::follow_floor(double start_x, double start_z,
    double start_y, double end_x, double end_z, double max_up, double max_down) const {
    struct Interval { double begin, end, start_height, end_height; };
    std::vector<Interval> intervals;
    for (const auto index : candidates(std::min(start_x, end_x), std::max(start_x, end_x),
        std::min(start_z, end_z), std::max(start_z, end_z))) {
        const auto& triangle = triangles_[index];
        const auto start = barycentric(triangle, start_x, start_z);
        const auto end = barycentric(triangle, end_x, end_z);
        double first = 0, last = 1;
        bool outside = false;
        for (std::size_t i = 0; i < 3; ++i) {
            const double delta = end[i] - start[i];
            if (std::abs(delta) < geometric_epsilon) {
                if (start[i] < -geometric_epsilon) { outside = true; break; }
            } else {
                const double boundary = -start[i] / delta;
                if (delta > 0) first = std::max(first, boundary);
                else last = std::min(last, boundary);
            }
        }
        if (outside || last <= first || first > 1 || last < 0) continue;
        intervals.push_back({first, last, height(triangle, start), height(triangle, end)});
    }
    double progress = 0, current_height = start_y;
    for (std::size_t transitions = 0; transitions < 256; ++transitions) {
        const Interval* selected = nullptr;
        double best_distance = std::numeric_limits<double>::infinity();
        for (const auto& interval : intervals) {
            if (interval.begin > progress + geometric_epsilon || interval.end <= progress + geometric_epsilon) continue;
            const double value = interval.start_height + (interval.end_height - interval.start_height) * progress;
            if (!reachable(value, current_height, max_up, max_down)) continue;
            const double distance = std::abs(value - current_height);
            if (distance < best_distance - height_epsilon
                || (std::abs(distance - best_distance) <= height_epsilon
                    && (!selected || interval.end > selected->end))) {
                selected = &interval; best_distance = distance;
            }
        }
        if (!selected) return std::nullopt;
        progress = selected->end;
        current_height = selected->start_height + (selected->end_height - selected->start_height) * progress;
        if (progress >= 1 - geometric_epsilon) return current_height;
    }
    throw std::runtime_error("Walking segment transition limit exceeded");
}

bool WalkingWorld::blocked_by_walls(const std::array<double, 3>& from, const std::array<double, 3>& to,
    double radius, double bottom, double top) const {
    const double low = std::min(from[1], to[1]) + bottom;
    const double high = std::max(from[1], to[1]) + top;
    const Point2 start{from[0], from[2]}, finish{to[0], to[2]};
    for (const auto index : candidates(std::min(from[0], to[0]) - radius, std::max(from[0], to[0]) + radius,
        std::min(from[2], to[2]) - radius, std::max(from[2], to[2]) + radius, true)) {
        const auto& triangle = wall_triangles_[index];
        const std::array<Point3, 3> points{{{triangle.x[0], triangle.y[0], triangle.z[0]},
            {triangle.x[1], triangle.y[1], triangle.z[1]}, {triangle.x[2], triangle.y[2], triangle.z[2]}}};
        const auto lower_clipped = clip_height(points, low, true);
        const auto clipped = clip_height(lower_clipped, high, false);
        if (clipped.empty()) continue;
        std::array<Point2, 8> projected{};
        if (clipped.size() > projected.size()) throw std::runtime_error("Walking wall clipping limit exceeded");
        for (std::size_t i = 0; i < clipped.size(); ++i) projected[i] = {clipped[i][0], clipped[i][2]};
        const std::span<const Point2> polygon(projected.data(), clipped.size());
        const double swept_distance = polygon_distance(polygon, start, finish);
        if (swept_distance >= radius - height_epsilon) continue;
        const double initial_distance = polygon_distance(polygon, start, start);
        const double final_distance = polygon_distance(polygon, finish, finish);
        // An entry point can already overlap a wall. Permit only an outward
        // path that never deepens that overlap, so it remains possible to leave.
        if (initial_distance < radius && final_distance > initial_distance + height_epsilon
            && swept_distance >= initial_distance - height_epsilon) continue;
        return true;
    }
    return false;
}

Walker::Walker(WalkingSettings settings) : settings_(settings) {
    for (const auto value : {settings.speed, settings.step_up, settings.step_down,
        settings.spawn_snap_down, settings.max_frame_seconds, settings.max_substep_distance,
        settings.body_radius, settings.body_bottom, settings.body_top})
        nonnegative(value);
    if (settings.speed <= 0 || settings.speed > 1000 || settings.step_up > 1000
        || settings.step_down > 1000 || settings.spawn_snap_down > 10000
        || settings.max_frame_seconds <= 0 || settings.max_frame_seconds > 0.25f
        || settings.max_substep_distance < 0.1f || settings.max_substep_distance > 10
        || settings.body_radius < 0.01f || settings.body_radius > 100
        || settings.body_top <= settings.body_bottom || settings.body_top > 1000)
        throw std::invalid_argument("Walking settings exceed the supported preview ranges");
}

void Walker::reset(Vec3 position, const WalkingWorld& world, float facing) {
    coordinate(position.x); coordinate(position.y); coordinate(position.z);
    finite(facing, "Walking facing must be finite");
    const auto floor_y = world.floor(position.x, position.z, position.y, 0, settings_.spawn_snap_down);
    if (!floor_y) throw std::runtime_error("No walking floor beneath the selected player entry point");
    position_ = {position.x, *floor_y, position.z};
    facing_ = facing; moving_ = false; grounded_ = true;
}

void Walker::advance(float input_x, float input_z, double dt, const WalkingWorld& world) {
    finite(input_x, "Walking input must be finite"); finite(input_z, "Walking input must be finite");
    finite(dt, "Walking frame time must be finite");
    if (dt < 0) throw std::invalid_argument("Walking frame time must be nonnegative");
    if (!grounded_) throw std::logic_error("Walking requires a successful reset first");
    moving_ = false;
    const double magnitude = std::hypot(double(input_x), double(input_z));
    if (magnitude < 1e-7 || dt == 0) return;
    const double scale = settings_.speed * std::min(dt, double(settings_.max_frame_seconds)) / std::max(1.0, magnitude);
    const double move_x = input_x * scale, move_z = input_z * scale;
    const auto steps = static_cast<std::size_t>(std::ceil(std::hypot(move_x, move_z) / settings_.max_substep_distance));
    if (steps == 0) return;
    auto candidate_position = position_;
    for (std::size_t i = 0; i < steps; ++i) {
        const double step_x = move_x / steps, step_z = move_z / steps;
        const auto attempt = [&](double dx, double dz) {
            const double next_x = candidate_position[0] + dx, next_z = candidate_position[2] + dz;
            if (std::abs(next_x) > coordinate_limit || std::abs(next_z) > coordinate_limit) return false;
            const auto next_y = world.follow_floor(candidate_position[0], candidate_position[2], candidate_position[1],
                next_x, next_z, settings_.step_up, settings_.step_down);
            if (!next_y) return false;
            const std::array<double, 3> destination{next_x, *next_y, next_z};
            if (world.blocked_by_walls(candidate_position, destination,
                settings_.body_radius, settings_.body_bottom, settings_.body_top)) return false;
            candidate_position = destination;
            return true;
        };
        if (attempt(step_x, step_z)) continue;
        if (std::abs(step_x) < geometric_epsilon || std::abs(step_z) < geometric_epsilon) break;
        // Keep the original axis components, rather than accelerating the
        // unblocked axis to the full requested diagonal speed.
        const bool prefer_x = std::abs(step_x) >= std::abs(step_z);
        if (prefer_x ? (attempt(step_x, 0) || attempt(0, step_z))
            : (attempt(0, step_z) || attempt(step_x, 0))) continue;
        break;
    }
    moving_ = std::hypot(candidate_position[0] - position_[0], candidate_position[2] - position_[2]) > 1e-7;
    position_ = candidate_position;
    facing_ = static_cast<float>(std::atan2(double(input_x), double(input_z)));
}

Vec3 Walker::position() const noexcept {
    return {static_cast<float>(position_[0]), static_cast<float>(position_[1]), static_cast<float>(position_[2])};
}

} // namespace dw4
