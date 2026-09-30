// beWaterPlain (manager 80317xxx): rippling water surfaces.

#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace dw4::engine {
namespace {

std::vector<float> float_list(const ObjectView& list) {
    std::vector<float> out;
    if (!list) return out;
    const auto data = list.memory(4);
    const std::size_t count = std::min<std::size_t>(std::size_t(std::max(list.i32(2), 0)), data.size() / 4);
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t bits = std::uint32_t(data[i * 4]) | (std::uint32_t(data[i * 4 + 1]) << 8) |
                                   (std::uint32_t(data[i * 4 + 2]) << 16) | (std::uint32_t(data[i * 4 + 3]) << 24);
        std::memcpy(&out[i], &bits, 4);
    }
    return out;
}

std::vector<int> int_list(const ObjectView& list) {
    std::vector<int> out;
    for (float f : float_list(list)) {
        std::int32_t v;
        std::memcpy(&v, &f, 4);
        out.push_back(v);
    }
    return out;
}

// The geometry node drawing a vertex array (the water surface mesh).
GeometryData* find_geometry(Engine& engine, Node* node, const ObjectView& vertices, int depth) {
    if (!node || depth > 64) return nullptr;
    for (const auto* attr : node->geometries)
        if (const auto array = attr->object.ref(4);
            array && array.document() == vertices.document() && array.reference() == vertices.reference())
            return const_cast<GeometryData*>(engine.scenes().geometry(attr));
    for (auto* child : node->children)
        if (auto* found = find_geometry(engine, child, vertices, depth + 1)) return found;
    return nullptr;
}

} // namespace

void WaterPlainManager::add_info(Engine&, const ObjectView& info) {
    for (const auto& record : info.list(6)) {
        if (!record) continue;
        Water w;
        w.vertices = record.ref(21);
        w.weight = float_list(record.ref(22));
        w.map = int_list(record.ref(23));
        w.width = int(record.u32(24));
        w.height = int(record.u32(25));
        w.origin = record.vec3(26).value_or(Vec3f{});
        w.max = record.vec3(27).value_or(Vec3f{});
        w.min = record.vec3(28).value_or(Vec3f{});
        w.cell = record.f32(31, 4.0f);
        w.c_sum = record.f32(12);
        w.c_cur = record.f32(13);
        w.range1_top = record.f32(15);
        w.range1_bottom = record.f32(16);
        w.range1_amount = record.f32(17);
        w.range2_top = record.f32(18);
        w.range2_bottom = record.f32(19);
        w.range2_amount = record.f32(20);
        const auto buffers = record.list(32);
        for (std::size_t i = 0; i < 2 && i < buffers.size(); ++i) w.buffers[i] = float_list(buffers[i]);
        w.limit_max = record.f32(33);
        w.limit_min = record.f32(34);
        w.boundary = record.f32(35);
        if (const auto move = record.ref(37)) {
            w.period = move.i64(4);
            for (const auto& data : move.list(6)) {
                if (!data) continue;
                Drop drop;
                drop.time = data.i64(2);
                drop.strength = data.f32(3);
                if (const auto points = data.ref(4)) {
                    const auto values = float_list(points);
                    for (std::size_t i = 0; i + 2 < values.size(); i += 3) drop.points.push_back({values[i], values[i + 1], values[i + 2]});
                }
                w.drops.push_back(std::move(drop));
            }
        }
        const std::size_t points = std::size_t(std::max(w.width, 0)) * std::size_t(std::max(w.height, 0));
        if (w.width < 3 || w.height < 3 || w.weight.size() < points || w.map.size() < points) continue;
        for (auto& buffer : w.buffers)
            if (buffer.empty()) buffer.assign(w.map.size(), 0.0f);
        waters_.push_back(std::move(w));
    }
}

void WaterPlainManager::clear_stage(Engine&) { waters_.clear(); }

// 803177FC: inside the surface's height bands and its rectangle less the margin.
bool WaterPlainManager::inside(const Water& w, const Vec3f& p) const {
    const bool band = (w.origin.y + w.range1_top >= p.y && w.origin.y + w.range1_bottom < p.y) ||
                      (w.origin.y + w.range2_top >= p.y && w.origin.y + w.range2_bottom < p.y);
    if (!band) return false;
    return w.origin.x + w.min.x + w.cell <= p.x && w.origin.x + w.max.x - w.cell >= p.x && w.origin.z + w.min.z + w.cell <= p.z &&
           w.origin.z + w.max.z - w.cell >= p.z;
}

// 803176F0: the grid point under p takes the band's depth times the strength.
void WaterPlainManager::push(Water& w, const Vec3f& p, float strength) {
    const int cell = int((p.x - w.min.x - w.origin.x) / w.cell) + int((p.z - w.min.z - w.origin.z) / w.cell) * w.width;
    if (cell < 0 || std::size_t(cell) >= w.map.size() || w.map[std::size_t(cell)] == -1) return;
    float amount = -2.0f; // 80452578
    if (w.origin.y + w.range1_top >= p.y && w.origin.y + w.range1_bottom < p.y) amount = w.range1_amount;
    else if (w.origin.y + w.range2_top >= p.y && w.origin.y + w.range2_bottom < p.y) amount = w.range2_amount;
    const int vertex = w.map[std::size_t(cell)];
    auto& current = w.buffers[w.current];
    if (vertex >= 0 && std::size_t(vertex) < current.size()) current[std::size_t(vertex)] = amount * strength;
}

void WaterPlainManager::update(Engine& engine) {
    if (waters_.empty()) return;
    auto* models = engine.get<ModelCtrlManager>();
    for (auto& w : waters_) {
        if (!w.geometry && !w.searched) {
            w.geometry = find_geometry(engine, engine.global_root(), w.vertices, 0);
            w.searched = true;
            if (std::getenv("DW4_TRACE_WATER"))
                engine.log("water at (" + std::to_string(w.origin.x) + ", " + std::to_string(w.origin.y) + ", " +
                           std::to_string(w.origin.z) + ") grid " + std::to_string(w.width) + "x" + std::to_string(w.height) +
                           (w.geometry ? " mesh " + std::to_string(w.geometry->positions.size()) : std::string(" without mesh")));
        }
        // 803178CC: players and enemies standing in the water press it down.
        if (models)
            for (const auto& ram : models->rams()) {
                if (!ram->generated || !ram->node || !(ram->is_player || ram->category == "ENEMY")) continue;
                const auto& m = ram->node->matrix;
                const Vec3f p{m(3, 0), m(3, 1), m(3, 2)};
                if (inside(w, p)) push(w, p, 1.0f);
            }
        // beWaterMove: drops at their times within the period.
        if (!w.drops.empty() && w.period > 0) {
            const std::int64_t elapsed = engine.time() - w.move_start;
            if (elapsed >= w.period || w.move_start == 0) {
                w.move_start = engine.time();
                w.move_index = 0;
            }
            const std::int64_t now = engine.time() - w.move_start;
            if (w.move_index < w.drops.size() && now >= w.drops[w.move_index].time) {
                for (const auto& point : w.drops[w.move_index].points) {
                    const Vec3f p = point + w.origin;
                    if (inside(w, p)) push(w, p, w.drops[w.move_index].strength);
                }
                ++w.move_index;
            }
        }
        step(w);
        if (static const bool trace = std::getenv("DW4_TRACE_WATER") != nullptr; trace && engine.frame() % 120 == 0) {
            float peak = 0.0f;
            for (float v : w.buffers[w.current]) peak = std::max(peak, std::abs(v));
            engine.log("water (" + std::to_string(w.origin.x) + ", " + std::to_string(w.origin.z) + ") peak " + std::to_string(peak));
        }
    }
}

// 8031716C: one step of the wave equation over the inner grid points; the
// result becomes the vertices' heights, with normals from the neighbours.
void WaterPlainManager::step(Water& w) {
    auto& current = w.buffers[w.current];
    auto& next = w.buffers[1 - w.current];
    auto height = [&](int point) {
        if (point < 0 || std::size_t(point) >= w.weight.size() || w.weight[std::size_t(point)] == 0.0f) return w.boundary;
        const int vertex = w.map[std::size_t(point)];
        return vertex >= 0 && std::size_t(vertex) < current.size() ? current[std::size_t(vertex)] : w.boundary;
    };
    GeometryData* g = w.geometry;
    for (int z = 1; z < w.height - 1; ++z)
        for (int x = 1; x < w.width - 1; ++x) {
            const int point = z * w.width + x;
            const float weight = w.weight[std::size_t(point)];
            if (weight == 0.0f) continue;
            const int vertex = w.map[std::size_t(point)];
            if (vertex < 0 || std::size_t(vertex) >= current.size() || std::size_t(vertex) >= next.size()) continue;
            const float left = height(point - 1), right = height(point + 1);
            const float down = height(point + w.width), up = height(point - w.width);
            float value = (w.c_sum * (left + right + down + up) + w.c_cur * current[std::size_t(vertex)] - next[std::size_t(vertex)]) * weight;
            value = std::clamp(value, w.limit_min, w.limit_max);
            next[std::size_t(vertex)] = value;
            if (!g || std::size_t(vertex) >= g->positions.size()) continue;
            g->positions[std::size_t(vertex)].y = value;
            if (std::size_t(vertex) < g->normals.size()) {
                const Vec3f a{2.0f * w.cell, right - left, 0.0f};
                const Vec3f b{0.0f, up - down, -2.0f * w.cell};
                Vec3f n = cross(a, b);
                if (n.y < 0.0f) n = n * -1.0f;
                const float len = length(n);
                if (len > 1e-6f) g->normals[std::size_t(vertex)] = n * (1.0f / len);
            }
        }
    w.current = 1 - w.current;
    if (g) g->serial = GeometrySerial{};
}

} // namespace dw4::engine
