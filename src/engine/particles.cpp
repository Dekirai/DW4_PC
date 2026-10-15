#include "dw4/engine/particles.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dw4::engine {
namespace {

float read_f32(std::span<const std::uint8_t> data, std::size_t at) {
    if (at + 4 > data.size()) return 0.0f;
    std::uint32_t bits = std::uint32_t(data[at]) | (std::uint32_t(data[at + 1]) << 8) | (std::uint32_t(data[at + 2]) << 16) |
                         (std::uint32_t(data[at + 3]) << 24);
    float value;
    std::memcpy(&value, &bits, 4);
    return std::isfinite(value) ? value : 0.0f;
}

// c0 + c1 t + c2 t^2 with the three terms `step` bytes apart.
float quadratic(std::span<const std::uint8_t> p, std::size_t at, std::size_t step, float t) {
    return read_f32(p, at) + read_f32(p, at + step) * t + read_f32(p, at + 2 * step) * t * t;
}

std::uint8_t unit_byte(float value) { return std::uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); }

} // namespace

std::span<const std::uint8_t> particle_record(std::span<const std::uint8_t> data, std::uint32_t index, std::uint32_t stride) {
    // 800E09C8: record i is read at data(+0x24) + i * stride(+0x20) without
    // any record length check. The base update reads up to +0x8C, the Bec one
    // up to +0xB0, so with a stride below 0xB4 (139 arrays use 160: the menu
    // cursor ping y_messcr has its only record, y_dbreak02, y_d2torch,
    // y_atcksabtnz02 and y_031fldmg02a 4-15) the reads past the stride come
    // from the next record as in the original; fields past the end of the
    // data read as 0.
    const std::size_t base = std::size_t(index) * stride;
    if (base >= data.size()) return {};
    return data.subspan(base, std::min<std::size_t>(0xB4, data.size() - base));
}

bool build_particles(const ObjectView& particle_attr, float time, const Mat4& model, const Mat4& view, GeometryData& out) {
    const auto array = particle_attr.ref(5);
    if (!array) return false;
    const auto format = array.ref(2);
    const std::uint32_t first = particle_attr.u32(7);
    const std::uint32_t count = particle_attr.u32(6);
    const float start = array.f32(5), end = array.f32(6), extra = array.f32(22);
    const std::uint32_t stride = array.u32(8) ? array.u32(8) : 180u;
    const auto data = array.memory(9);
    const bool loop = array.boolean(26), continuous = array.boolean(27), textured = array.boolean(29);
    const float start_offset = array.f32(28);
    const float x_images = array.f32(24, 1.0f), y_images = array.f32(25, 1.0f);
    const bool bec = continuous || textured; // 8031A318: otherwise the base update runs
    const int rotation_terms = format ? format.i32(10) : 0;
    const bool one_size = format && format.i32(5) == 0;

    if (bec) {
        // 8031A340: the loop wraps start offset + lifetime; the particles
        // start after the offset.
        if (loop && start_offset + end > 0.0f) time = std::fmod(time, start_offset + end);
        if (time > start_offset + end || time < start_offset) return false;
        time -= start_offset;
    } else if (time > end + extra || time < start) {
        return false; // 800E0EC0
    }

    // Camera axes in world space (row-vector view: columns of the rotation).
    const Vec3f right{view(0, 0), view(1, 0), view(2, 0)};
    const Vec3f up{view(0, 1), view(1, 1), view(2, 1)};
    const float scale = length(Vec3f{model(0, 0), model(0, 1), model(0, 2)});

    // Particles first(+0x34) .. first + count(+0x38) (loop test 800E0C74).
    for (std::uint32_t i = first; i < first + count; ++i) {
        const auto p = particle_record(data, i, stride);
        if (p.empty()) break;
        float t = time;
        if (bec) {
            const float offset = read_f32(p, 0x90);
            if (offset != 0.0f && end > 0.0f) t = std::fmod(time + offset, end);
        }
        const Vec3f local{quadratic(p, 0x00, 0x0C, t), quadratic(p, 0x04, 0x0C, t), quadratic(p, 0x08, 0x0C, t)};
        const Vec3f center = transform_point(local, model);
        const float r = quadratic(p, 0x24, 0x10, t), g = quadratic(p, 0x28, 0x10, t);
        const float b = quadratic(p, 0x2C, 0x10, t), a = quadratic(p, 0x30, 0x10, t);
        float w = quadratic(p, 0x54, 0x08, t), h = one_size ? w : quadratic(p, 0x58, 0x08, t);
        w *= scale;
        h *= scale;
        if (w <= 0.0f || h <= 0.0f || a <= 0.0f) continue;
        float u0 = read_f32(p, 0x6C), v0 = read_f32(p, 0x70), u1 = read_f32(p, 0x74), v1 = read_f32(p, 0x78);
        if (bec) {
            u0 += read_f32(p, 0x94) * t + read_f32(p, 0x9C) * t * t;
            v0 += read_f32(p, 0x98) * t + read_f32(p, 0xA0) * t * t;
            u1 += read_f32(p, 0xA4) * t + read_f32(p, 0xAC) * t * t;
            v1 += read_f32(p, 0xA8) * t + read_f32(p, 0xB0) * t * t;
            // The rectangle snaps down to the image grid of the texture.
            if (x_images > 1.0f) {
                u0 -= std::fmod(u0, 1.0f / x_images);
                u1 -= std::fmod(u1, 1.0f / x_images);
            }
            if (y_images > 1.0f) {
                v0 -= std::fmod(v0, 1.0f / y_images);
                v1 -= std::fmod(v1, 1.0f / y_images);
            }
        }
        Vec3f ax = right * (w * 0.5f), ay = up * (h * 0.5f);
        if (!bec && rotation_terms > 0) {
            // 800E0C44: the sprite turns in the view plane.
            const float angle = read_f32(p, 0x84) + read_f32(p, 0x80) * t + read_f32(p, 0x7C) * t * t;
            const float c = std::cos(angle), s = std::sin(angle);
            const Vec3f rx = ax * c + ay * s, ry = ay * c - ax * s;
            ax = rx;
            ay = ry;
        }
        const auto index = std::uint32_t(out.positions.size());
        out.positions.push_back(center - ax + ay);
        out.positions.push_back(center + ax + ay);
        out.positions.push_back(center + ax - ay);
        out.positions.push_back(center - ax - ay);
        const std::uint32_t color = std::uint32_t(unit_byte(r)) | (std::uint32_t(unit_byte(g)) << 8) |
                                    (std::uint32_t(unit_byte(b)) << 16) | (std::uint32_t(unit_byte(a)) << 24);
        out.colors.insert(out.colors.end(), 4, color);
        out.texcoords.insert(out.texcoords.end(), {u0, v0, u1, v0, u1, v1, u0, v1});
        out.indices.insert(out.indices.end(), {index, index + 2, index + 1, index, index + 3, index + 2});
    }
    out.valid = !out.indices.empty();
    return out.valid;
}

} // namespace dw4::engine
