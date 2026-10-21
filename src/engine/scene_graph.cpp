#include "dw4/engine/scene_graph.hpp"
#include "dw4/engine/particles.hpp"
#include "dw4/animation_math.hpp"
#include "dw4/motion.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dw4::engine {
namespace {

constexpr std::uint32_t kNullRef = 0xffffffffu;

// 8012D3F4: t * (b - a) + a per component, one fused multiply/add (fmadds).
Vec3f lerp(const Vec3f& a, const Vec3f& b, float t) {
    return {std::fma(t, b.x - a.x, a.x), std::fma(t, b.y - a.y, a.y), std::fma(t, b.z - a.z, a.z)};
}

// 8012D59C: cubic Bezier from p0 to p1 with control points c0, c1, built from
// six 8012D3F4 steps (de Casteljau).
Vec3f bezier(const Vec3f& p0, const Vec3f& c0, const Vec3f& c1, const Vec3f& p1, float t) {
    const Vec3f a = lerp(p0, c0, t), b = lerp(c0, c1, t), c = lerp(c1, p1, t);
    return lerp(lerp(a, b, t), lerp(b, c, t), t);
}

// 8012C15C (dw4::original_quaternion_nlerp): linear blend on the shorter arc
// (b negated when the dot product is below 0), then normalized; no
// spherical weights.
Vec4f nlerp(const Vec4f& a, const Vec4f& b, float t) {
    const auto r = original_quaternion_nlerp({a.x, a.y, a.z, a.w}, {b.x, b.y, b.z, b.w}, t);
    return {r[0], r[1], r[2], r[3]};
}

// 8012BE90 (dw4::original_interpolate_rotation): polynomial slerp, nothing
// normalized. Its native boundary rejects non-finite input; such keys keep
// the first quaternion instead of throwing out of the frame.
Vec4f alchemy_slerp(const Vec4f& a, const Vec4f& b, float t) {
    try {
        const auto r = original_interpolate_rotation({a.x, a.y, a.z, a.w}, {b.x, b.y, b.z, b.w}, std::clamp(t, 0.0f, 1.0f));
        return {r[0], r[1], r[2], r[3]};
    } catch (const std::exception&) {
        return a;
    }
}

// 8012BBF4: quaternion (x, y, z, w) to a rotation matrix with zero
// translation. Row vectors as everywhere in Alchemy; this is the transpose of
// quaternion_matrix (math.cpp), with the signs of 8012BCDC / skinning.cpp.
Mat4 alchemy_quaternion_matrix(const Vec4f& q) {
    const float x2 = 2.0f * q.x, y2 = 2.0f * q.y, z2 = 2.0f * q.z;
    Mat4 m;
    m(0, 0) = 1.0f - (y2 * q.y + z2 * q.z);
    m(0, 1) = y2 * q.x - z2 * q.w;
    m(0, 2) = z2 * q.x + y2 * q.w;
    m(1, 0) = y2 * q.x + z2 * q.w;
    m(1, 1) = 1.0f - (x2 * q.x + z2 * q.z);
    m(1, 2) = z2 * q.y - x2 * q.w;
    m(2, 0) = z2 * q.x - y2 * q.w;
    m(2, 1) = z2 * q.y + x2 * q.w;
    m(2, 2) = 1.0f - (x2 * q.x + y2 * q.y);
    return m;
}

// 801292D4 (dw4::original_model_turn_basis): Rx * Ry * Rz of radians.
Mat4 alchemy_euler_matrix(const Vec3f& radians) {
    Mat4 m;
    try {
        original_model_turn_basis(m.m, {radians.x, radians.y, radians.z});
    } catch (const std::exception&) {
        m = Mat4{};
    }
    return m;
}

// The original converts 64-bit nanosecond spans to float through bits
// 18..49 (rotlwi/insrwi, then the 0x43300000 double trick, 80566D08).
float coarse_time(std::int64_t ns) { return static_cast<float>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(ns) >> 18)); }

// Key blend factor: numerator / denominator, 0 below 1e-6 (80566D4C, 80566CA0).
float blend_ratio(std::int64_t numerator, std::int64_t denominator) {
    const float d = coarse_time(denominator);
    return d >= 1e-6f ? coarse_time(numerator) / d : 0.0f;
}

template <class T>
T key_at(const std::vector<T>& list, std::size_t i, const T& fallback) {
    return list.empty() ? fallback : list[std::min(i, list.size() - 1)];
}

// One vec3 channel from key a (index i) to key b: NONE = key a, LINEAR =
// 8012D3F4, otherwise BEZIER = 8012D59C with the control points 2i and 2i+1
// of the channel's _interpolationData list (802094B8 and the rotation /
// scale parts of 80209578).
Vec3f blend_vec3(std::uint8_t method, const Vec3f& a, const Vec3f& b, const std::vector<Vec3f>& tangents,
                 std::size_t i, float alpha) {
    if (method == TransformSequence::none) return a;
    if (method == TransformSequence::linear || 2 * i + 1 >= tangents.size()) return lerp(a, b, alpha);
    return bezier(a, tangents[2 * i], tangents[2 * i + 1], b, alpha);
}

Vec3f xyz(const Vec4f& v) { return {v.x, v.y, v.z}; }

std::vector<Vec3f> vec3_list(const ObjectView& list) {
    std::vector<Vec3f> out;
    if (!list) return out;
    const auto count = list.u32(2);
    const auto data = list.memory(4);
    if (data.size() < std::size_t(count) * 12) return out;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i)
        out[i] = {read_f32le(data, i * 12), read_f32le(data, i * 12 + 4), read_f32le(data, i * 12 + 8)};
    return out;
}

std::vector<Vec4f> vec4_list(const ObjectView& list) {
    std::vector<Vec4f> out;
    if (!list) return out;
    const auto count = list.u32(2);
    const auto data = list.memory(4);
    if (data.size() < std::size_t(count) * 16) return out;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i)
        out[i] = {read_f32le(data, i * 16), read_f32le(data, i * 16 + 4), read_f32le(data, i * 16 + 8), read_f32le(data, i * 16 + 12)};
    return out;
}

std::vector<std::int64_t> long_list(const ObjectView& list) {
    std::vector<std::int64_t> out;
    if (!list) return out;
    const auto count = list.u32(2);
    const auto data = list.memory(4);
    if (data.size() < std::size_t(count) * 8) return out;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t lo = read_u32le(data, i * 8), hi = read_u32le(data, i * 8 + 4);
        out[i] = static_cast<std::int64_t>((hi << 32) | lo);
    }
    return out;
}

std::vector<std::uint16_t> u16_list(const ObjectView& list) {
    std::vector<std::uint16_t> out;
    if (!list) return out;
    const auto count = list.u32(2);
    const auto data = list.memory(4);
    if (data.size() < std::size_t(count) * 2) return out;
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) out[i] = std::uint16_t(data[i * 2] | (data[i * 2 + 1] << 8));
    return out;
}

// igCompressedAnimationSequenceQS (fields 801C2E40): quantized keys, each
// component base + value * range (801E2F08 translation, 801E2E54 quaternion);
// a key time is (value & 0x7FFF) << 23 ns, bit 15 its sign (801E2F98).
std::shared_ptr<TransformSequence> compressed_sequence(const ObjectView& input) {
    auto seq = std::make_shared<TransformSequence>();
    const auto translations = u16_list(input.ref(2));
    const auto quaternions = u16_list(input.ref(3));
    const auto times = u16_list(input.ref(4));
    const float translation_base = input.f32(5), translation_range = input.f32(6);
    const float quaternion_base = input.f32(7), quaternion_range = input.f32(8);
    auto value = [](std::uint16_t v, float base, float range) { return base + float(v) * range; };
    for (std::size_t k = 0; k + 2 < translations.size(); k += 3)
        seq->translations.push_back({value(translations[k], translation_base, translation_range),
                                     value(translations[k + 1], translation_base, translation_range),
                                     value(translations[k + 2], translation_base, translation_range)});
    for (std::size_t k = 0; k + 3 < quaternions.size(); k += 4)
        seq->rotations.push_back({value(quaternions[k], quaternion_base, quaternion_range),
                                  value(quaternions[k + 1], quaternion_base, quaternion_range),
                                  value(quaternions[k + 2], quaternion_base, quaternion_range),
                                  value(quaternions[k + 3], quaternion_base, quaternion_range)});
    for (const auto t : times) {
        const std::int64_t magnitude = std::int64_t(t & 0x7FFFu) << 23;
        seq->times.push_back((t & 0x8000u) ? -magnitude : magnitude);
    }
    seq->duration = input.i64(9);
    seq->play_mode = input.i32(10);
    // _drivenChannels (+0x38, slot 12: 1 translation, 2 quaternion) and
    // _interpolationMethod (+0x39..+0x3B, slot 13).
    seq->compressed = true;
    const auto channels = input.raw(12);
    seq->driven_channels = channels.empty() ? std::uint8_t(TransformSequence::translation | TransformSequence::quaternion) : channels[0];
    const auto methods = input.raw(13);
    for (std::size_t k = 0; k < 3 && k < methods.size(); ++k) seq->methods[k] = methods[k];
    seq->valid = !seq->times.empty();
    return seq;
}

void add_strip(std::vector<std::uint32_t>& out, const std::vector<std::uint32_t>& source, std::size_t start, std::size_t length) {
    for (std::size_t i = 2; i < length; ++i) {
        std::uint32_t a = source[start + i - 2], b = source[start + i - 1], c = source[start + i];
        if (i & 1u) std::swap(a, b);
        if (a == b || b == c || a == c) continue;
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
    }
}

} // namespace

GeometryData decode_geometry(const ObjectView& attr) {
    GeometryData g;
    try {
        const auto vertices = attr.ref(4);
        if (!vertices) { g.diagnostic = "no vertex array"; return g; }
        const auto count = vertices.u32(3);
        const auto table = vertices.memory(2);
        if (!count || table.size() < 80) { g.diagnostic = "empty vertex array"; return g; }
        const auto* document = vertices.document();
        auto stream = [&](int index) -> std::span<const std::uint8_t> {
            const auto ref = read_u32le(table, std::size_t(index) * 4);
            if (ref == kNullRef) return {};
            return document->memory(ref);
        };
        const auto positions = stream(0);
        if (positions.size() < std::size_t(count) * 12) { g.diagnostic = "short position stream"; return g; }
        g.positions.resize(count);
        g.bounds_min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        g.bounds_max = {-g.bounds_min.x, -g.bounds_min.y, -g.bounds_min.z};
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& p = g.positions[i];
            p = {read_f32le(positions, i * 12), read_f32le(positions, i * 12 + 4), read_f32le(positions, i * 12 + 8)};
            g.bounds_min = {std::min(g.bounds_min.x, p.x), std::min(g.bounds_min.y, p.y), std::min(g.bounds_min.z, p.z)};
            g.bounds_max = {std::max(g.bounds_max.x, p.x), std::max(g.bounds_max.y, p.y), std::max(g.bounds_max.z, p.z)};
        }
        if (const auto normals = stream(1); normals.size() >= std::size_t(count) * 12) {
            g.normals.resize(count);
            for (std::uint32_t i = 0; i < count; ++i)
                g.normals[i] = {read_f32le(normals, i * 12), read_f32le(normals, i * 12 + 4), read_f32le(normals, i * 12 + 8)};
        }
        if (const auto colors = stream(2); colors.size() >= std::size_t(count) * 4) {
            g.colors.resize(count);
            for (std::uint32_t i = 0; i < count; ++i) g.colors[i] = read_u32le(colors, i * 4);
        }
        if (const auto uvs = stream(11); uvs.size() >= std::size_t(count) * 8) {
            g.texcoords.resize(std::size_t(count) * 2);
            for (std::uint32_t i = 0; i < count * 2; ++i) g.texcoords[i] = read_f32le(uvs, i * 4);
        }
        const auto format = vertices.raw(6);
        if (format.size() >= 2) {
            const std::uint32_t weights = format[1] & 0x0fu;
            const auto weight_data = vertices.memory(7);
            const auto index_data = vertices.memory(8);
            if (weights && weight_data.size() >= std::size_t(count) * 4 * weights && index_data.size() >= std::size_t(count) * weights) {
                g.weight_count = weights;
                g.weights.resize(std::size_t(count) * weights);
                for (std::size_t i = 0; i < g.weights.size(); ++i) g.weights[i] = read_f32le(weight_data, i * 4);
                g.blend_indices.assign(index_data.begin(), index_data.begin() + std::ptrdiff_t(std::size_t(count) * weights));
            }
        }
        // Source index list.
        std::vector<std::uint32_t> source;
        if (const auto index_array = attr.ref(5)) {
            const auto index_count = index_array.u32(3);
            const auto data = index_array.memory(2);
            const auto index_format = index_array.u32(4);
            if (index_format == 0 && data.size() >= std::size_t(index_count) * 2) {
                source.resize(index_count);
                for (std::uint32_t i = 0; i < index_count; ++i) source[i] = read_u16le(data, i * 2);
            } else if (data.size() >= std::size_t(index_count) * 4) {
                source.resize(index_count);
                for (std::uint32_t i = 0; i < index_count; ++i) source[i] = read_u32le(data, i * 4);
            }
        } else {
            source.resize(count);
            for (std::uint32_t i = 0; i < count; ++i) source[i] = i;
        }
        for (auto& index : source) if (index >= count) index = 0;
        const auto primitive = attr.u32(6);
        const auto primitives = attr.u32(7);
        std::vector<std::size_t> lengths;
        if (const auto length_array = attr.ref(13)) {
            const auto groups = length_array.u32(3);
            const auto data = length_array.memory(2);
            for (std::uint32_t i = 0; i < groups && (i + 1) * 4 <= data.size(); ++i) lengths.push_back(read_u32le(data, i * 4));
        }
        if (primitive == 3) {
            const std::size_t n = std::min<std::size_t>(source.size() / 3 * 3, std::size_t(primitives) * 3);
            g.indices.assign(source.begin(), source.begin() + std::ptrdiff_t(n ? n : source.size() / 3 * 3));
        } else if (primitive == 4) {
            if (lengths.empty()) lengths.push_back(source.size());
            std::size_t start = 0;
            for (auto length : lengths) {
                if (start + length > source.size()) break;
                add_strip(g.indices, source, start, length);
                start += length;
            }
        } else if (primitive == 5) {
            if (lengths.empty()) lengths.push_back(source.size());
            std::size_t start = 0;
            for (auto length : lengths) {
                if (start + length > source.size()) break;
                for (std::size_t i = 2; i < length; ++i) {
                    g.indices.push_back(source[start]);
                    g.indices.push_back(source[start + i - 1]);
                    g.indices.push_back(source[start + i]);
                }
                start += length;
            }
        } else {
            g.diagnostic = "unsupported primitive " + std::to_string(primitive);
            return g;
        }
        g.valid = !g.indices.empty();
    } catch (const std::exception& error) {
        g.diagnostic = error.what();
        g.valid = false;
    }
    return g;
}

namespace {
// The original node clock: ((time >> 18) as float * scale) << 18.
std::int64_t scaled_time(std::int64_t now, float scale) {
    const auto coarse = static_cast<std::uint32_t>(static_cast<std::uint64_t>(now) >> 18);
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<float>(coarse) * scale)) << 18);
}
} // namespace

std::int64_t NodeAnim::time(std::int64_t now) {
    // 803113E0: the first visit starts the clock at `start` (803111A0).
    if (first) {
        bias = start - scaled_time(now, scale);
        before = now;
        first = false;
    }
    if (fixed != 0) return fixed;
    // 80311230: the play time counts real time; a step longer than two
    // frames advances the clock by two frames only.
    const std::int64_t step = now - before;
    played += step;
    constexpr std::int64_t two_frames = 33333333;
    if (step > two_frames) bias -= step - two_frames;
    before = now;
    std::int64_t t = scaled_time(now, scale) + bias;
    if (t < loop_end || loop_end <= 0) return t;
    while (t > loop_end) t -= loop_end;
    t += loop_start;
    bias = t - scaled_time(now, scale);
    return t;
}

namespace {

// pi / 180 as loaded by 80208084 / 80209578 (80566D3C).
constexpr float kDegreesToRadians = 0.0174532925f;
static_assert(std::bit_cast<std::uint32_t>(kDegreesToRadians) == 0x3C8EFA35u);

// The tail of 80208084 / 80209578. A rotated matrix turns about _center:
// translation = centre - centre * R (8012D434). The translation channel then
// adds its key, and the scale channel scales rows 0..2 (80128F88), so the
// scale applies before the rotation and does not scale the centre term.
void turn_about_center(Mat4& m, const Vec3f& center) {
    const Vec3f turned{center.x * m(0, 0) + center.y * m(1, 0) + center.z * m(2, 0),
                       center.x * m(0, 1) + center.y * m(1, 1) + center.z * m(2, 1),
                       center.x * m(0, 2) + center.y * m(1, 2) + center.z * m(2, 2)};
    m(3, 0) = center.x - turned.x;
    m(3, 1) = center.y - turned.y;
    m(3, 2) = center.z - turned.z;
}

void add_translation(Mat4& m, const Vec3f& t) {
    m(3, 0) += t.x;
    m(3, 1) += t.y;
    m(3, 2) += t.z;
}

void scale_rows(Mat4& m, const Vec3f& s) {
    for (int c = 0; c < 4; ++c) {
        m(0, c) *= s.x;
        m(1, c) *= s.y;
        m(2, c) *= s.z;
    }
}

} // namespace

Mat4 TransformSequence::key(std::size_t index) const {
    Mat4 m;
    if (compressed) {
        // 801E32B8 before the first / after the last key: 8012BBF4 of the
        // raw key, then 80128D90 sets the translation.
        if (driven_channels & quaternion) m = alchemy_quaternion_matrix(key_at(rotations, index, Vec4f{0, 0, 0, 1}));
        if (driven_channels & translation) {
            const Vec3f t = key_at(translations, index, Vec3f{});
            m(3, 0) = t.x;
            m(3, 1) = t.y;
            m(3, 2) = t.z;
        }
        return m;
    }
    // 80208084: the stored key (quaternions are not normalized).
    if (driven_channels & quaternion) {
        m = alchemy_quaternion_matrix(key_at(rotations, index, Vec4f{0, 0, 0, 1}));
        turn_about_center(m, center);
    } else if (driven_channels & euler) {
        m = alchemy_euler_matrix(xyz(key_at(rotations, index, Vec4f{})) * kDegreesToRadians);
        turn_about_center(m, center);
    }
    if (driven_channels & translation) add_translation(m, key_at(translations, index, Vec3f{}));
    if (driven_channels & scale) scale_rows(m, key_at(scales, index, Vec3f{1, 1, 1}));
    return m;
}

Mat4 TransformSequence::blend(std::size_t i, std::size_t j, float alpha) const {
    Mat4 m;
    if (driven_channels & quaternion) {
        // 80209858 / QS 801E36F0: NONE = key i, SLERP = 8012BE90, otherwise
        // (LINEAR, BEZIER) the normalized linear blend 8012C15C.
        const Vec4f a = key_at(rotations, i, Vec4f{0, 0, 0, 1});
        Vec4f q = a;
        if (methods[1] == slerp) q = alchemy_slerp(a, key_at(rotations, j, Vec4f{0, 0, 0, 1}), alpha);
        else if (methods[1] != none) q = nlerp(a, key_at(rotations, j, Vec4f{0, 0, 0, 1}), alpha);
        m = alchemy_quaternion_matrix(q);
        if (!compressed) turn_about_center(m, center);
    } else if ((driven_channels & euler) && !compressed) {
        // Euler degrees, interpolated like a vec3 channel (method +0x4A).
        const Vec3f e = blend_vec3(methods[1], xyz(key_at(rotations, i, Vec4f{})), xyz(key_at(rotations, j, Vec4f{})),
                                   rotation_tangents, i, alpha);
        m = alchemy_euler_matrix(e * kDegreesToRadians);
        turn_about_center(m, center);
    }
    if (driven_channels & translation) {
        // 802094B8 / QS 801E3C20. This QS version serializes no translation
        // control points (no +0x40 list), so its BEZIER falls back to LINEAR.
        const Vec3f t = blend_vec3(methods[0], key_at(translations, i, Vec3f{}), key_at(translations, j, Vec3f{}),
                                   translation_tangents, i, alpha);
        if (compressed) {
            m(3, 0) = t.x;
            m(3, 1) = t.y;
            m(3, 2) = t.z;
        } else {
            add_translation(m, t);
        }
    }
    if ((driven_channels & scale) && !compressed)
        scale_rows(m, blend_vec3(methods[2], key_at(scales, i, Vec3f{1, 1, 1}), key_at(scales, j, Vec3f{1, 1, 1}),
                                 scale_tangents, i, alpha));
    return m;
}

// 80209B74 (igTransformSequence1_5) and 801E2FF0 (QS): the sequence time is
// time - _keyframeTimeOffset (QS: no offset). Inside [first key, first key +
// _animationDuration] the time is used as is, so the end itself evaluates the
// last key; the play mode applies only beyond the end: REPEAT and LOOP wrap
// by the duration, BOUNCE runs back on odd passes, CLAMP holds the last key.
// Before the first key (or for an unknown play mode) key 0 applies. Between
// the last key and the end, LOOP blends the last key into key 0; the other
// modes hold the last key.
Mat4 TransformSequence::evaluate(std::int64_t time) const {
    if (!valid || times.empty()) return Mat4{};
    const std::size_t count = times.size();
    const std::int64_t t = compressed ? time : time - time_offset;
    std::int64_t first = times.front(), last = times.back(), end = first + duration;
    if (uniform && !compressed) {
        // _uniformTimeEnabled: keys every times[0] from 0; the last key time
        // counts the _quatList keys (80209BFC).
        first = 0;
        last = times.front() * (static_cast<std::int64_t>(rotations.size()) - 1);
        end = duration;
    }
    // 80209C58: no duration evaluates key 0. (QS has no such test; its
    // division by a zero duration is undefined, so it gets the same answer.)
    if (duration == 0) return key(0);
    std::int64_t at = t;
    if (t > end) {
        const std::int64_t passes = (t - first) / duration, rest = (t - first) % duration; // 802077D0
        switch (play_mode) {
        case bounce: at = (passes & 1) ? end - rest : first + rest; break;
        case repeat:
        case loop: at = first + rest; break;
        case clamp: return key(count - 1);
        default: return key(0);
        }
    } else if (t < first) {
        return key(0);
    }
    if (at > last) {
        if (play_mode != loop) return key(count - 1);
        if (uniform && !compressed) {
            const float step = coarse_time(times.front());
            const float alpha = step >= 1e-6f ? static_cast<float>(std::fmod(double(coarse_time(at)), double(step))) / step : 0.0f;
            return blend(count - 1, 0, alpha);
        }
        return blend(count - 1, 0, blend_ratio(at - last, end - last));
    }
    if (uniform && !compressed) {
        // 80209AC0: key index = truncated time / spacing, blend = the
        // remainder (800A2CB4 fmod) / spacing.
        const float step = coarse_time(times.front()), x = coarse_time(at);
        if (!(step > 0.0f)) return key(0);
        const auto i = static_cast<std::size_t>(std::max(0.0f, std::trunc(x / step)));
        return blend(i, i + 1, static_cast<float>(std::fmod(double(x), double(step))) / step);
    }
    if (count < 2) return key(0);
    // 80209900 / 801E34C8: the first key j >= 1 whose time is not before the
    // sequence time; the blend runs from key j - 1.
    std::size_t j = 1;
    while (j + 1 < count && times[j] < at) ++j;
    return blend(j - 1, j, blend_ratio(at - times[j - 1], times[j] - times[j - 1]));
}

void Node::set_child_enabled(std::size_t index, bool enabled) {
    const std::size_t word = index / 32;
    if (word >= mask.size()) mask.resize(word + 1, 0);
    if (enabled) mask[word] |= 1u << (index % 32);
    else mask[word] &= ~(1u << (index % 32));
}

void Node::set_all_children(bool enabled) {
    for (std::size_t i = 0; i < children.size(); ++i) set_child_enabled(i, enabled);
}

Node* Node::select_lod(float d2) {
    const auto current = [this]() -> Node* {
        return lod_child >= 0 && std::size_t(lod_child) < children.size() ? children[std::size_t(lod_child)] : nullptr;
    };
    // 0x801F79B4..0x801F79D4: inside [_lastLower, _lastUpper) the cached
    // child stays (fcmpo: a NaN distance falls through to the search).
    if (d2 >= lod_lower && d2 < lod_upper) return current();
    const auto count = static_cast<std::int32_t>(lod_ranges.size());
    const auto range = [&](std::int32_t i) { return i >= 0 && i < count ? lod_ranges[std::size_t(i)] : 0.0f; };
    std::int32_t index{}, next{};
    if (d2 >= lod_lower) {
        // 0x801F7A34: upward from _lastIndex + 2 while the range is reached.
        next = lod_index + 2;
        while (next < count && d2 >= lod_ranges[std::size_t(next)]) ++next;
        index = next - 1;
    } else {
        // 0x801F7A04: downward from _lastIndex while the range is above.
        index = lod_index;
        while (index >= 0 && d2 < lod_ranges[std::size_t(index)]) --index;
        next = index + 1;
    }
    lod_index = index;
    if (index < 0) {
        lod_lower = 0.0f; // 0x801F7B10 (r2-0x7580 = 0.0)
        lod_child = -1;
    } else {
        lod_lower = range(index);
        lod_child = index < static_cast<std::int32_t>(children.size()) ? index : -1; // 0x801F7A84
    }
    // 0x801F7B48: FLT_MAX (0x8055DE2C) past the last range.
    lod_upper = next >= count ? std::numeric_limits<float>::max() : range(next);
    // 0x801F7B68..0x801F7C98: the hysteresis h widens the band to
    // ((sqrt(lower) - h)^2, (sqrt(upper) + h)^2).
    const auto hysteresis = static_cast<std::int32_t>(lod_hysteresis.size());
    if (hysteresis) {
        if (index >= 0 && index < hysteresis) {
            const float r = lod_lower > 0.0f ? std::sqrt(lod_lower) : lod_lower;
            const float h = lod_hysteresis[std::size_t(index)];
            lod_lower = lod_lower + (h * h - 2.0f * h * r);
        }
        if (next < hysteresis) {
            const float r = lod_upper > 0.0f ? std::sqrt(lod_upper) : lod_upper;
            const float h = lod_hysteresis[std::size_t(next)];
            lod_upper = lod_upper + (h * h + 2.0f * h * r);
        }
    }
    return current();
}

void Node::reset_lod() {
    lod_child = -1;
    lod_index = -1;
    lod_lower = lod_upper = 0.0f;
}

TextureSampler texture_sampler(const ObjectView& texture) {
    TextureSampler s;
    if (!texture) return s;
    // igTextureAttr::apply (0x800C3684..0x800C36B4) pushes _wrapS/_wrapT
    // (800FF6F4), _minFilter (800FF634) and _magFilter (800FF67C) into the
    // visual context's texture record; 800FE744 turns the IG wraps into GX
    // ones through 80490984 = {CLAMP, REPEAT, CLAMP, REPEAT}: GX never mirrors.
    const auto wrap = [](std::int32_t v) { return v >= 0 && v < 4 ? int(v & 1) : 1; };
    s.wrap_s = wrap(texture.i32(7, 1));
    s.wrap_t = wrap(texture.i32(8, 1));
    const auto image = texture.ref(12);
    // Palette images: the format table 804906A4 (+12) marks the IG formats
    // 1, 4 and 6 (GX C8 with the built-in TLUTs 804F887C) and the indexed
    // 0x10000 / 0x10001 / 0x10002 with their igClut (GX C8 / C4 / C14X2;
    // the game's indexed8 images are 0x10000). For them 800FE744 skips
    // GXInitTexObj (0x800FED84) and zeroes the TLUT stamp +152 (0x800FED94),
    // so the bind loop 800FCA9C rebuilds the object right after it with
    // GXInitTexObjCI (802563C4 at 0x800FCC34 / 0x800FCD4C; the stamp never
    // matches the generation, which starts at 1, 0x800FC6F8). That is
    // GXInitTexObj 80256178 with its defaults: mag
    // LINEAR (0x802561DC), min LINEAR (0x8025628C) or, mipmapped,
    // LIN_MIP_NEAR (0xA0, 0x80256204). The GXInitTexObjLOD of 800FE744
    // (0x800FEE50) ran before and is lost, so the attribute filters never
    // reach a palette texture; the wraps do.
    const auto format = image ? image.u32(11) : 0u;
    const bool palette = format == 1 || format == 4 || format == 6 || format == 0x10000 || format == 0x10001 || format == 0x10002;
    if (palette) {
        // Mipmapped: record +60 = _imageCount - 1 (0x800FE15C) or, with
        // _mipmapMode 1, the generated levels log2(min(w, h)) (0x800FE288).
        const auto w = image.u32(2), h = image.u32(3);
        const bool mipmapped = texture.i32(15, 1) > 1 || (texture.i32(10) == 1 && std::min(w, h) >= 2);
        s.mag_filter = 1;
        s.min_filter = mipmapped ? 4 : 1; // LINEAR_MIPMAP_NEAREST : LINEAR
    } else {
        // GXInitTexObjLOD (0x800FEE50) with the attribute filters, min
        // through 80414A40, mag through 80414A58.
        s.mag_filter = texture.i32(5, 1);
        s.min_filter = texture.i32(6, 1);
    }
    return s;
}

const Attribute* SceneCache::attribute(const ObjectView& view) {
    if (!view) return nullptr;
    const Key key{view.document(), view.reference()};
    if (auto found = attributes_.find(key); found != attributes_.end()) return found->second.get();
    auto a = std::make_unique<Attribute>();
    a->object = view;
    const auto& type = view.type();
    if (type == "igGeometryAttr1_5" || view.is_a("igGeometryAttr")) {
        a->kind = AttrKind::geometry;
    } else if (type == "igTextureBindAttr") {
        a->kind = AttrKind::texture_bind;
        a->unit = view.i32(5);
        a->texture = view.ref(4);
        if (a->texture) {
            a->image = a->texture.ref(12);
            a->sampler = texture_sampler(a->texture);
        }
    } else if (type == "igTextureStateAttr") {
        a->kind = AttrKind::texture_state;
        a->enabled = view.boolean(4);
        a->unit = view.i32(5);
    } else if (type == "igTextureMatrixStateAttr") {
        a->kind = AttrKind::texture_matrix_state;
        a->enabled = view.boolean(4);
        a->unit = view.i32(5);
    } else if (type == "igColorAttr") {
        a->kind = AttrKind::color;
        a->color = view.vec4(4).value_or(Vec4f{1, 1, 1, 1});
    } else if (type == "igMaterialAttr") {
        a->kind = AttrKind::material;
        a->f0 = view.f32(4);
        a->color = view.vec4(5).value_or(Vec4f{1, 1, 1, 1});
        a->ambient = view.vec4(6).value_or(Vec4f{0.2f, 0.2f, 0.2f, 1});
        a->emission = view.vec4(7).value_or(Vec4f{0, 0, 0, 1});
        a->specular = view.vec4(8).value_or(Vec4f{0, 0, 0, 1});
        a->e0 = view.i32(9);
    } else if (type == "igBlendFunctionAttr") {
        a->kind = AttrKind::blend_function;
        a->e0 = view.i32(4, 4);
        a->e1 = view.i32(5, 5);
        a->e2 = view.i32(6, 0);
    } else if (type == "igBlendStateAttr") {
        a->kind = AttrKind::blend_state;
        a->enabled = view.boolean(4);
    } else if (type == "igAlphaFunctionAttr") {
        a->kind = AttrKind::alpha_function;
        a->e0 = view.i32(4, 7);
        a->f0 = view.f32(5);
    } else if (type == "igAlphaStateAttr") {
        a->kind = AttrKind::alpha_state;
        a->enabled = view.boolean(4);
    } else if (type == "igDepthStateAttr") {
        a->kind = AttrKind::depth_state;
        a->enabled = view.boolean(4, true);
    } else if (type == "igDepthWriteStateAttr") {
        a->kind = AttrKind::depth_write_state;
        a->enabled = view.boolean(4, true);
    } else if (type == "igDepthFunctionAttr") {
        a->kind = AttrKind::depth_function;
        a->e0 = view.i32(4, 3);
    } else if (type == "igCullFaceAttr") {
        a->kind = AttrKind::cull_face;
        a->enabled = view.boolean(4);
        a->e0 = view.i32(5, 0); // 0 BACK, 1 FRONT (0x8048F570)
    } else if (type == "igLightingStateAttr") {
        a->kind = AttrKind::lighting_state;
        a->enabled = view.boolean(4);
    } else if (type == "igLightStateAttr") {
        a->kind = AttrKind::light_state;
        a->texture = view.ref(4); // the igLightAttr
        a->enabled = view.boolean(5);
        a->light = attribute(view.ref(4));
    } else if (type == "igLightAttr") {
        a->kind = AttrKind::light;
        a->e0 = view.i32(4);
        a->ambient = view.vec4(7).value_or(Vec4f{});
        a->color = view.vec4(8).value_or(Vec4f{1, 1, 1, 1});
        a->specular = view.vec4(9).value_or(Vec4f{});
        const auto direction = view.vec3(10).value_or(Vec3f{0, 0, -1});
        const auto position = view.vec3(6).value_or(Vec3f{});
        a->matrix(0, 0) = direction.x;
        a->matrix(0, 1) = direction.y;
        a->matrix(0, 2) = direction.z;
        a->matrix(1, 0) = position.x;
        a->matrix(1, 1) = position.y;
        a->matrix(1, 2) = position.z;
    } else if (type == "igFogAttr") {
        a->kind = AttrKind::fog;
        a->e0 = view.i32(4);
        a->f0 = view.f32(5);
        a->f1 = view.f32(6);
        a->emission = Vec4f{view.f32(7), 0, 0, 0};
        a->color = view.vec4(8).value_or(Vec4f{1, 1, 1, 1});
    } else if (type == "igFogStateAttr") {
        a->kind = AttrKind::fog_state;
        a->enabled = view.boolean(4);
    } else if (type == "igShadeModelAttr") {
        a->kind = AttrKind::shade_model;
        a->e0 = view.i32(4);
    } else if (type == "igModelViewMatrixAttr") {
        a->kind = AttrKind::model_view_matrix;
        a->matrix = view.mat4(4).value_or(Mat4{});
    } else if (type == "igProjectionMatrixAttr") {
        a->kind = AttrKind::projection_matrix;
        a->matrix = view.mat4(4).value_or(Mat4{});
    } else if (type == "igViewportAttr") {
        a->kind = AttrKind::viewport;
        a->viewport[0] = view.i32(4);
        a->viewport[1] = view.i32(5);
        a->viewport[2] = view.i32(6);
        a->viewport[3] = view.i32(7);
    } else if (type == "igColorMaskAttr") {
        a->kind = AttrKind::color_mask;
        for (int i = 0; i < 4; ++i) a->mask[i] = view.boolean(std::uint16_t(4 + i), true);
    } else if (type == "igVertexBlendStateAttr") {
        a->kind = AttrKind::vertex_blend_state;
        a->enabled = view.boolean(4);
    } else if (type == "igNormalizeNormalsStateAttr") {
        a->kind = AttrKind::normalize_normals;
        a->enabled = view.boolean(4);
    } else if (type == "igTextureCoordSourceAttr") {
        // Fields (registration 800AEFF4, size 0x18): [4] _source (+0xC),
        // [5] the texture coordinate set (+0x10), [6] _unit (+0x14). The
        // apply (800C3AA0 -> 800FF8F8 -> 800FF8D8) stores them per unit in
        // the visual context (+0x1FC / +0x23C) for the texgen setup 800FCEA8.
        a->kind = AttrKind::texcoord_source;
        a->e0 = view.i32(4);
        a->e1 = view.i32(5);
        a->unit = view.i32(6);
    }
    auto* raw = a.get();
    attributes_.emplace(key, std::move(a));
    return raw;
}

Node* SceneCache::find(const IgbDocument* document, std::uint32_t reference) const {
    const auto found = nodes_.find(Key{document, reference});
    return found == nodes_.end() ? nullptr : found->second.get();
}

void SceneCache::release_document(const IgbDocument* document) {
    for (auto it = attributes_.begin(); it != attributes_.end();) {
        if (it->first.document == document) {
            geometry_.erase(it->second.get());
            it = attributes_.erase(it);
        } else ++it;
    }
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (it->first.document == document) it = nodes_.erase(it);
        else ++it;
    }
}

Node* SceneCache::node(const IgbDocument* document, std::uint32_t reference) {
    const Key key{document, reference};
    if (auto found = nodes_.find(key); found != nodes_.end()) return found->second.get();
    auto owned = std::make_unique<Node>();
    Node* n = owned.get();
    nodes_.emplace(key, std::move(owned));
    const ObjectView view(document, reference);
    n->object = view;
    n->name = std::string(view.str(2));
    const auto& type = view.type();
    auto children_from = [&](std::uint16_t slot) {
        for (const auto& child : view.list(slot))
            if (child && child.is_a("igNode")) n->children.push_back(node(child.document(), child.reference()));
    };
    auto attributes_from = [&](std::uint16_t slot, std::vector<const Attribute*>& out) {
        for (const auto& item : view.list(slot))
            if (item) if (const auto* a = attribute(item)) out.push_back(a);
    };
    if (type == "ParticleTimer") {
        // An igAttrSet whose attributes are igParticleAttr draws.
        n->kind = NodeKind::particle_timer;
        children_from(7);
        for (const auto& attr : view.list(8))
            if (attr && attr.type() == "igParticleAttr") n->particles.push_back(attr);
    } else if (view.is_a("igGeometry")) {
        n->kind = NodeKind::geometry;
        std::vector<const Attribute*> attrs;
        attributes_from(8, attrs);
        for (const auto* a : attrs) {
            if (a->kind == AttrKind::geometry) n->geometries.push_back(a);
            else n->attributes.push_back(a);
        }
        children_from(7);
    } else if (type == "igLightSet") {
        n->kind = NodeKind::light_set;
        attributes_from(7, n->attributes);
    } else if (view.is_a("igSwitch")) {
        n->kind = NodeKind::switch_node;
        children_from(7);
        if (const auto mask = view.ref(8)) {
            const auto data = mask.memory(4);
            for (std::size_t i = 0; i + 4 <= data.size(); i += 4) n->mask.push_back(read_u32le(data, i));
        }
    } else if (type == "beCopyModelCtrl2") {
        n->kind = NodeKind::copy_model;
        children_from(7);
        if (auto m = view.mat4(8)) { n->matrix = *m; n->has_matrix = true; }
        if (const auto source = view.ref(12)) n->copy_source = node(source.document(), source.reference());
    } else if (view.is_a("beNodeAnim")) {
        n->kind = NodeKind::node_anim;
        children_from(7);
        NodeAnim anim;
        anim.start = view.i64(8);
        anim.loop_start = view.i64(9);
        anim.loop_end = view.i64(10);
        anim.fixed = view.i64(11);
        anim.first = view.boolean(12);
        anim.scale = view.f32(14, 1.0f);
        anim.bias = view.i64(15);
        anim.before = view.i64(16);
        anim.played = view.i64(17);
        if (view.is_a("beLoadingNode")) {
            anim.loading = true;
            anim.lock = view.i64(18);
            anim.play_limit = view.i64(19);
        }
        n->anim = anim;
    } else if (view.is_a("beModelCtrlNode2")) {
        n->kind = NodeKind::model_node;
        children_from(7);
        if (auto m = view.mat4(8)) { n->matrix = *m; n->has_matrix = true; }
        n->time_scale = view.f32(13, 1.0f);
        n->time_bias = view.i64(14);
    } else if (view.is_a("igTransform")) {
        n->kind = NodeKind::transform;
        children_from(7);
        if (auto m = view.mat4(8)) { n->matrix = *m; n->has_matrix = true; }
        n->texture_transform = (view.i32(9) & 8) != 0;
        if (const auto input = view.ref(11); input && input.is_a("igTransformSequence")) {
            auto seq = std::make_shared<TransformSequence>();
            seq->translations = vec3_list(input.ref(2));
            seq->rotations = vec4_list(input.ref(3));
            seq->scales = vec3_list(input.ref(4));
            seq->uniform = !input.raw(7).empty() && input.raw(7)[0] != 0;
            seq->play_mode = input.i32(8);
            seq->center = input.vec3(10).value_or(Vec3f{});
            seq->times = long_list(input.ref(11));
            seq->translation_tangents = vec3_list(input.ref(12));
            seq->rotation_tangents = vec3_list(input.ref(13));
            seq->scale_tangents = vec3_list(input.ref(14));
            seq->driven_channels = input.raw(15).empty() ? std::uint8_t(0) : input.raw(15)[0];
            const auto methods = input.raw(16);
            for (std::size_t k = 0; k < 3 && k < methods.size(); ++k) seq->methods[k] = methods[k];
            seq->time_offset = input.i64(17);
            seq->duration = input.i64(18);
            seq->valid = !seq->times.empty();
            n->sequence = std::move(seq);
        } else if (input && input.is_a("igCompressedAnimationSequenceQS")) {
            n->sequence = compressed_sequence(input);
        }
    } else if (view.is_a("igBillboard")) {
        n->kind = NodeKind::billboard;
        n->billboard_mode = view.i32(8);
        n->billboard_axis = view.vec3(9).value_or(Vec3f{0, 0, 1});
        n->billboard_center = view.vec3(10).value_or(Vec3f{});
        children_from(7);
    } else if (view.is_a("igBlendMatrixSelect")) {
        n->kind = NodeKind::blend_matrix_select;
        attributes_from(8, n->attributes);
        children_from(7);
        if (const auto list = view.ref(10)) {
            const auto data = list.memory(4);
            const auto count = list.u32(2);
            for (std::uint32_t i = 0; i < count && (i + 1) * 4 <= data.size(); ++i)
                n->blend_indices.push_back(static_cast<std::int32_t>(read_u32le(data, i * 4)));
        }
    } else if (view.is_a("igModelViewMatrixBoneSelect")) {
        n->kind = NodeKind::bone_select;
        n->bone_index = view.i32(8, -1);
        children_from(7);
    } else if (view.is_a("igActor")) {
        n->kind = NodeKind::actor;
        children_from(7);
        if (auto m = view.mat4(14)) n->actor_matrix = *m;
        if (const auto appearance = view.ref(11)) {
            if (const auto skin = appearance.ref(3))
                if (const auto graph = skin.ref(3)) n->appearance_roots.push_back(node(graph.document(), graph.reference()));
            for (const auto& skin : appearance.list(4))
                if (skin) if (const auto graph = skin.ref(3)) n->appearance_roots.push_back(node(graph.document(), graph.reference()));
            for (const auto& extra : appearance.list(7))
                if (extra && extra.is_a("igNode")) n->appearance_roots.push_back(node(extra.document(), extra.reference()));
        }
    } else if (view.is_a("igLod")) {
        n->kind = NodeKind::lod;
        children_from(7);
        const auto floats = [&](std::uint16_t slot, std::vector<float>& out) {
            if (const auto list = view.ref(slot)) {
                const auto data = list.memory(4);
                const auto count = list.u32(2);
                for (std::uint32_t i = 0; i < count && (i + 1) * 4 <= data.size(); ++i) out.push_back(read_f32le(data, i * 4));
            }
        };
        floats(8, n->lod_ranges);
        floats(14, n->lod_hysteresis); // no shipped igLod has one (players: [0, 0, 10000] / [0, 0])
        n->lod_center = view.vec3(13).value_or(Vec3f{});
    } else if (view.is_a("igAttrSet")) {
        n->kind = NodeKind::attr_set;
        attributes_from(8, n->attributes);
        children_from(7);
    } else if (view.is_a("igCamera")) {
        n->kind = NodeKind::camera;
    } else if (type == "igProjectiveShadowShader") {
        n->kind = NodeKind::shadow_shader;
        children_from(7);
    } else if (type == "igTransformRecorder") {
        n->kind = NodeKind::transform_recorder;
        children_from(7);
    } else if (type == "igLightStateSet") {
        // A group whose light states ([8]) switch lights on for its children.
        n->kind = NodeKind::light_state_set;
        children_from(7);
        attributes_from(8, n->attributes);
    } else if (type == "beResetNode") {
        // 802C0B64: an igGroup (parent meta 801BF938, 36 bytes) with the
        // flags _isResetTime ([8], +0x20) and _isResetRotAndScale ([9],
        // +0x21) of the field table 804CFFC4. Only stagBS04 has them
        // (f_devl1e, f_devl1f2, f_devl1f3; all [8] 0, [9] 1).
        n->kind = NodeKind::reset_node;
        children_from(7);
        n->reset_time = view.boolean(8);
        n->reset_rot_scale = view.boolean(9);
    } else if (view.is_a("igGroup")) {
        n->kind = NodeKind::group;
        children_from(7);
    }
    return n;
}

const GeometryData* SceneCache::geometry(const Attribute* attribute) {
    if (!attribute) return nullptr;
    if (auto found = geometry_.find(attribute); found != geometry_.end()) return found->second.get();
    auto data = std::make_unique<GeometryData>(decode_geometry(attribute->object));
    auto* raw = data.get();
    geometry_.emplace(attribute, std::move(data));
    return raw;
}

namespace {
// `picture` false (a traversal without draw items): the lights, which only
// the draw items use, are not collected.
void apply(const Attribute* a, RenderState& s, Mat4& model, bool picture) {
    switch (a->kind) {
    case AttrKind::texture_bind:
        if (a->unit == 0) s.texture = a;
        break;
    case AttrKind::texture_state:
        if (a->unit == 0 && !s.texture_forced) s.texture_enabled = a->enabled;
        break;
    case AttrKind::color: s.color = a->color; break;
    case AttrKind::material:
        s.diffuse = a->color;
        s.ambient = a->ambient;
        s.emission = a->emission;
        break;
    case AttrKind::blend_function:
        s.blend_src = a->e0;
        s.blend_dst = a->e1;
        s.blend_equation = a->e2;
        break;
    case AttrKind::blend_state:
        if (!s.blend_forced) s.blend_enabled = a->enabled;
        break;
    case AttrKind::alpha_function:
        s.alpha_func = a->e0;
        s.alpha_ref = a->f0;
        break;
    case AttrKind::alpha_state: s.alpha_test = a->enabled; break;
    case AttrKind::depth_state: s.depth_test = a->enabled; break;
    case AttrKind::depth_write_state: s.depth_write = a->enabled; break;
    case AttrKind::depth_function: s.depth_func = a->e0; break;
    case AttrKind::cull_face:
        s.cull_enabled = a->enabled;
        s.cull_mode = a->e0;
        break;
    case AttrKind::lighting_state:
        if (!s.lighting_forced) s.lighting = a->enabled;
        break;
    case AttrKind::light_state:
        if (picture && a->enabled && a->light) s.lights.push_back(a->light);
        break;
    case AttrKind::fog: s.fog_params = a; break;
    case AttrKind::fog_state: s.fog = a->enabled; break;
    case AttrKind::model_view_matrix:
        s.view = a->matrix;
        model = Mat4{};
        break;
    case AttrKind::projection_matrix: s.projection = a->matrix; break;
    case AttrKind::viewport:
        s.viewport = std::array<std::int32_t, 4>{a->viewport[0], a->viewport[1], a->viewport[2], a->viewport[3]};
        break;
    case AttrKind::color_mask: s.color_write = a->mask[0] || a->mask[1] || a->mask[2]; break;
    case AttrKind::vertex_blend_state: s.vertex_blend = a->enabled; break;
    case AttrKind::texcoord_source:
        if (a->unit == 0) s.texcoord_source = a->e0;
        break;
    default: break;
    }
}

// Runtime ACTION2 attributes. FADESET / FLASHSET append theirs to the
// beModelCtrlNode2 attribute list (+0x88), which the node's traversal
// (8030FAA4) pushes as overrides (801D5190: the stack keeps the outermost
// override as its effective top), so deeper igTextureStateAttr /
// igLightingStateAttr / igBlendStateAttr in the skin graphs cannot undo
// them: the 4-frame FLASH draws the model untextured and unlit, white.
// FADESET (802F0800) adds only igColorAttr, igBlendStateAttr (on) and
// igLightingStateAttr (off), no igBlendFunctionAttr: the function stays the
// model's own (the warp beam x_099etrns01a keeps its additive SRC_ALPHA/ONE)
// or the default SRC_ALPHA/ONE_MINUS_SRC_ALPHA.
void apply_overrides(const Node& n, RenderState& s) {
    if (n.color_override) {
        const auto c = *n.color_override;
        s.modulate = s.modulate ? Vec4f{s.modulate->x * c.x, s.modulate->y * c.y, s.modulate->z * c.z, s.modulate->w * c.w} : c;
    }
    if (n.blend_override && !s.blend_forced) {
        s.blend_enabled = *n.blend_override;
        s.blend_forced = true;
    }
    if (n.texture_override && !s.texture_forced) {
        s.texture_enabled = *n.texture_override;
        s.texture_forced = true;
    }
    if (n.lighting_override && !s.lighting_forced) {
        s.lighting = *n.lighting_override;
        s.lighting_forced = true;
    }
}
} // namespace

namespace {
// The billboard code normalizes as v * (1 / |v|): |v|^2 = z*z + (x*x + y*y),
// its square root from frsqrte plus three Newton steps (|v|^2 * rsqrt,
// constants 0.5 / 3.0 at r2-0x7530 / r2-0x7528), then one float division.
// A zero vector divides by its zero length there (NaN rows); it stays zero
// here.
Vec3f billboard_normalize(const Vec3f& v) {
    const float length_squared = v.z * v.z + (v.x * v.x + v.y * v.y);
    if (!(length_squared > 0.0f)) return v;
    const float inverse = 1.0f / std::sqrt(length_squared);
    return {v.x * inverse, v.y * inverse, v.z * inverse};
}

// 801D53EC: "the up and the eye direction are parallel" is
// fabs(fabs(dot) - 1) < 1e-6 (double at r2-0x7508).
bool billboard_parallel(const Vec3f& a, const Vec3f& b) {
    return double(std::fabs(std::fabs(a.z * b.z + (a.x * b.x + a.y * b.y)) - 1.0f)) < 1e-6;
}

// 801D53EC(out, up, look): the rows X, Y, Z of an orthonormal basis with
// Y = normalized up; X = up x look and Z = X x up, so Z is the eye direction
// with its part along Y removed (the raw up pointer goes into the second
// cross product, 801D597C). With up and look parallel the basis falls back
// to world X (Z = X x up, X = up x Z) or, when up is world X as well, to
// world Z (X = up x Z, Z = X x up). Translation 0, w 1.
Mat4 billboard_basis(const Vec3f& raw_up, const Vec3f& raw_look) {
    const Vec3f up = billboard_normalize(raw_up);
    const Vec3f look = billboard_normalize(raw_look);
    Vec3f x, z;
    if (!billboard_parallel(up, look)) {
        x = billboard_normalize(cross(up, look));
        z = billboard_normalize(cross(x, raw_up));
    } else if (const Vec3f world_x{1, 0, 0}; !billboard_parallel(up, world_x)) {
        z = billboard_normalize(cross(world_x, up));
        x = billboard_normalize(cross(up, z));
    } else {
        x = billboard_normalize(cross(up, Vec3f{0, 0, 1}));
        z = billboard_normalize(cross(x, up));
    }
    Mat4 r;
    r(0, 0) = x.x; r(0, 1) = x.y; r(0, 2) = x.z;
    r(1, 0) = up.x; r(1, 1) = up.y; r(1, 2) = up.z;
    r(2, 0) = z.x; r(2, 1) = z.y; r(2, 2) = z.z;
    return r;
}

// 8012D434: v * the upper 3x3 of m (no translation).
Vec3f rotate_vector(const Vec3f& v, const Mat4& m) {
    return {v.z * m(2, 0) + (v.x * m(0, 0) + v.y * m(1, 0)), v.z * m(2, 1) + (v.x * m(0, 1) + v.y * m(1, 1)),
            v.z * m(2, 2) + (v.x * m(0, 2) + v.y * m(1, 2))};
}
} // namespace

Mat4 billboard_matrix(std::int32_t mode, const Vec3f& axis, const Vec3f& center, const Mat4& world,
                      const Mat4& inverse_view) {
    // 801D5ADC(node, out, world, inverse view): the position is the world
    // translation (80128DAC) plus _center through the world rotation
    // (8012D434); look = the eye (inverse view row 3) - position.
    const Vec3f c = rotate_vector(center, world);
    const Vec3f position{world(3, 0) + c.x, world(3, 1) + c.y, world(3, 2) + c.z};
    const Vec3f look{inverse_view(3, 0) - position.x, inverse_view(3, 1) - position.y, inverse_view(3, 2) - position.z};
    const Vec3f world_y{0, 1, 0};
    Vec3f up = world_y; // modes 0 and < 0 / >= 4 (801D5D80): an upright billboard
    switch (mode) {
    case 1: // 801D5B9C: _axis through the world rotation
        up = rotate_vector(axis, world);
        break;
    case 2: // 801D5BB0: world Y made orthogonal to the eye direction,
            // look x (Y x look) with the cross product 8012CB74 (a x b)
        up = cross(look, cross(world_y, look));
        break;
    case 3: { // 801D5BE8 (skipped without an inverse view): camera Y (world Y
              // through the inverse view) made orthogonal to the eye direction
              // the same way, each cross product normalized
        const Vec3f side = billboard_normalize(cross(rotate_vector(world_y, inverse_view), look));
        up = billboard_normalize(cross(look, side));
        break;
    }
    default: break;
    }
    // 801D53EC builds the rotation, 80128D90 then sets only the translation.
    Mat4 r = billboard_basis(up, look);
    r(3, 0) = position.x;
    r(3, 1) = position.y;
    r(3, 2) = position.z;
    return r;
}

namespace {
struct ActorScope {
    const ActorPose* pose{};
    Mat4 actor_model{};
    std::shared_ptr<const std::vector<Mat4>> palette;
    const Node* pose_actor{}; // beCopyModelCtrl2: actors take this actor's pose
};

// The first actor below a model instance (through LOD and switches).
Node* first_actor(Node* n, int depth) {
    if (!n || depth > 32) return nullptr;
    if (n->kind == NodeKind::actor) return n;
    for (auto* child : n->children)
        if (auto* found = first_actor(child, depth + 1)) return found;
    return nullptr;
}

void visit(Node* n, TraversalContext& ctx, const Mat4& parent_model, const RenderState& parent_state, std::int64_t time,
           const ActorScope& parent_scope, int depth) {
    if (!n || n->hidden || depth > 256) return;
    Mat4 model = parent_model;
    RenderState state = parent_state;
    ActorScope scope = parent_scope;
    switch (n->kind) {
    case NodeKind::attr_set:
        for (const auto* a : n->attributes) apply(a, state, model, ctx.output != nullptr);
        break;
    case NodeKind::blend_matrix_select: {
        for (const auto* a : n->attributes) apply(a, state, model, ctx.output != nullptr);
        if (ctx.output && scope.pose && scope.pose->skeleton) {
            auto palette = std::make_shared<std::vector<Mat4>>();
            const auto& skel = *scope.pose->skeleton;
            for (const auto joint : n->blend_indices) {
                if (scope.pose->bind_pose || joint < 0 || std::size_t(joint) >= skel.inverse_joint.size()) {
                    palette->push_back(Mat4{});
                    continue;
                }
                const auto bone = std::size_t(skel.joint_bone[std::size_t(joint)]);
                palette->push_back(bone < scope.pose->bone_world.size()
                                       ? skel.inverse_joint[std::size_t(joint)] * scope.pose->bone_world[bone]
                                       : Mat4{});
            }
            scope.palette = std::move(palette);
        }
        break;
    }
    case NodeKind::bone_select:
        if (scope.pose && n->bone_index >= 0 && std::size_t(n->bone_index) < scope.pose->bone_world.size() && !scope.pose->bind_pose)
            model = scope.pose->bone_world[std::size_t(n->bone_index)] * scope.actor_model;
        break;
    case NodeKind::light_set:
        if (ctx.output)
            for (const auto* a : n->attributes) if (a->kind == AttrKind::light) state.lights.push_back(a);
        return;
    case NodeKind::light_state_set:
        for (const auto* a : n->attributes) apply(a, state, model, ctx.output != nullptr);
        break;
    case NodeKind::transform: {
        Mat4 local = n->sequence && n->sequence->valid ? n->sequence->evaluate(time) : n->matrix;
        if (n->runtime_matrix) local = *n->runtime_matrix * local;
        if (n->texture_transform) {
            // UV animation (icon atlases, scrolling surfaces).
            state.texture_matrix = state.texture_matrix ? local * *state.texture_matrix : local;
            break;
        }
        model = local * model;
        break;
    }
    case NodeKind::particle_timer: {
        // The running effect's particles as camera-facing quads.
        if (n->particle_start < 0 || !ctx.output) break;
        const std::int64_t now = n->particle_paused >= 0 ? n->particle_paused : ctx.time;
        const float seconds = float(double(now - n->particle_start) * 1e-9);
        const Mat4 view = state.view.value_or(ctx.camera.view);
        if (n->particle_geometry.size() < n->particles.size()) n->particle_geometry.resize(n->particles.size());
        apply_overrides(*n, state);
        for (std::size_t i = 0; i < n->particles.size(); ++i) {
            auto& geometry = n->particle_geometry[i];
            if (!geometry) geometry = std::make_shared<GeometryData>();
            *geometry = GeometryData{}; // a new serial: the renderer uploads the new quads
            if (!build_particles(n->particles[i], seconds, model, view, *geometry)) continue;
            DrawItem item;
            item.geometry = geometry.get();
            item.node = n;
            item.model = Mat4{};
            item.state = state;
            item.layer = ctx.layer;
            item.order = ctx.order++;
            ctx.output->push_back(std::move(item));
        }
        break;
    }
    case NodeKind::transform_recorder:
        // igTransformRecorder traversal 801DE9C0 (table 0x80415194): _m
        // (+0x20, vtable +0x94) = modelview stack top * the traversal's
        // inverse view (+0xA4), i.e. the world matrix; _mvm (+0x60, vtable
        // +0x98) = the modelview matrix itself. One matrix each, no double
        // buffer: MOVERL JOINT (8030CD94) reads _m through the field meta
        // 805646FC (offset 0x20, registered by 801ABF70).
        n->recorded = model;
        break;
    case NodeKind::reset_node:
        // beResetNode traversal 80312C28 (registered by 80313004 on the
        // traversal 805656E4). The matrix stack holds modelview matrices:
        // with _isResetRotAndScale it copies the traversal's view matrix
        // (+0x64; the recorders' _m uses its inverse at +0xA4, 801DE9C0),
        // puts the stack top's translation into it (80128DAC / 80128D90)
        // and pushes that for the children, popped after them (8020B274).
        // In world space: the parent's rotation and scale are dropped and
        // only its world position stays, so the subtree is aligned to the
        // world axes (Daemon's SQ_ATCKON1 spiral under f_devl1e stays fixed
        // while the pivot turns). _isResetTime pushes the base of the
        // traversal's time stack (+0xF0) as the children's time.
        if (n->reset_rot_scale) {
            Mat4 placed;
            placed(3, 0) = model(3, 0);
            placed(3, 1) = model(3, 1);
            placed(3, 2) = model(3, 2);
            model = placed;
        }
        if (n->reset_time) time = ctx.time;
        break;
    case NodeKind::copy_model: {
        // The source instance's placement and its actor's pose.
        if (Node* source = n->copy_source) {
            model = (source->runtime_matrix ? *source->runtime_matrix : source->matrix) * model;
            if (!n->copy_resolved) {
                n->copy_actor = first_actor(source, 0);
                n->copy_resolved = true;
            }
            scope.pose_actor = n->copy_actor;
        }
        break;
    }
    case NodeKind::node_anim:
        if (n->anim) time = n->anim->time(time);
        break;
    case NodeKind::model_node: {
        Mat4 local = n->matrix;
        if (n->runtime_matrix) local = *n->runtime_matrix;
        model = local * model;
        // Original node clock: ((time >> 18) as float * scale) << 18 + bias.
        const auto coarse = static_cast<std::uint32_t>(static_cast<std::uint64_t>(time) >> 18);
        const auto scaled = static_cast<std::int64_t>(static_cast<float>(coarse) * n->time_scale);
        time = static_cast<std::int64_t>((static_cast<std::uint64_t>(scaled) << 18) + static_cast<std::uint64_t>(n->time_bias));
        break;
    }
    case NodeKind::billboard: {
        // igBillboard traversal 801DD62C (table 0x80415194 slot 27, registered
        // by 801DB310): the world matrix = modelview stack top * the
        // traversal's inverse view (+0xA4), 801D5ADC builds the billboard's
        // world matrix from it and the children get that * view (+0x64)
        // (80128690 at 801DD76C). Here `model` already is the world matrix.
        const Mat4 view = state.view.value_or(ctx.camera.view);
        Mat4 inverse_view;
        if (invert(view, inverse_view))
            model = billboard_matrix(n->billboard_mode, n->billboard_axis, n->billboard_center, model, inverse_view);
        break;
    }
    case NodeKind::lod: {
        // igLod traversal 801DE598 (table 0x80415194, registered by
        // 801DB310): _center through the modelview matrix (8012D498), and
        // the SQUARED eye distance z*z + (x*x + y*y) picks the one child
        // (801F799C); the ranges are squared distances. The players'
        // [0, 0, 10000] show Kage (child 0) never, High (1) up to 100 units
        // and Low (2) beyond; the shadow proxies draw Kage directly.
        apply_overrides(*n, state);
        const Vec3f eye = transform_point(n->lod_center, model * state.view.value_or(ctx.camera.view));
        if (Node* child = n->select_lod(eye.z * eye.z + (eye.x * eye.x + eye.y * eye.y)))
            visit(child, ctx, model, state, time, scope, depth + 1);
        return;
    }
    case NodeKind::geometry: {
        for (const auto* a : n->attributes) apply(a, state, model, ctx.output != nullptr);
        apply_overrides(*n, state);
        if (n->runtime_geometry) {
            if (n->runtime_geometry->valid && ctx.output) {
                DrawItem item;
                item.geometry = n->runtime_geometry.get();
                item.node = n;
                item.model = model;
                item.state = state;
                if (n->runtime_texture) {
                    item.state.texture = n->runtime_texture;
                    item.state.texture_enabled = true;
                }
                item.layer = ctx.layer;
                item.order = ctx.order++;
                ctx.output->push_back(std::move(item));
            }
            break;
        }
        if (!ctx.output) break; // no draw items (nor geometry decoding)
        for (const auto* g : n->geometries) {
            const auto* data = ctx.cache->geometry(g);
            if (!data || !data->valid) continue;
            DrawItem item;
            item.geometry = data;
            item.node = n;
            item.model = model;
            item.state = state;
            item.layer = ctx.layer;
            item.order = ctx.order++;
            if (data->weight_count && scope.palette) item.palette = scope.palette;
            ctx.output->push_back(std::move(item));
        }
        break;
    }
    case NodeKind::actor: {
        if (ctx.actor_hook) ctx.actor_hook(*n, model, ctx);
        apply_overrides(*n, state);
        ActorScope actor;
        actor.actor_model = n->actor_matrix * model;
        actor.pose = ctx.actor_pose ? ctx.actor_pose(scope.pose_actor ? *scope.pose_actor : *n) : nullptr;
        for (auto* root : n->appearance_roots) visit(root, ctx, actor.actor_model, state, time, actor, depth + 1);
        scope = actor;
        break;
    }
    case NodeKind::camera:
        return;
    default: break;
    }
    apply_overrides(*n, state);
    for (std::size_t i = 0; i < n->children.size(); ++i) {
        if (!n->child_enabled(i)) continue;
        visit(n->children[i], ctx, model, state, n->fixed_time ? *n->fixed_time : time, scope, depth + 1);
    }
}
} // namespace

void traverse(Node* n, TraversalContext& ctx, const Mat4& parent_model, const RenderState& parent_state, std::int64_t time) {
    visit(n, ctx, parent_model, parent_state, time, ActorScope{}, 0);
}

void traverse(Node* root, TraversalContext& context) {
    traverse(root, context, Mat4{}, RenderState{}, context.time);
}

} // namespace dw4::engine
