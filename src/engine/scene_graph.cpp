#include "dw4/engine/scene_graph.hpp"
#include "dw4/engine/particles.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dw4::engine {
namespace {

constexpr std::uint32_t kNullRef = 0xffffffffu;

Vec3f lerp(const Vec3f& a, const Vec3f& b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

Vec4f slerp(Vec4f a, Vec4f b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    float wa, wb;
    if (d > 0.9995f) { wa = 1 - t; wb = t; }
    else {
        const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
        const float s = std::sin(angle);
        wa = std::sin((1 - t) * angle) / s;
        wb = std::sin(t * angle) / s;
    }
    Vec4f r{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
    const float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (l > 0) { r.x /= l; r.y /= l; r.z /= l; r.w /= l; }
    return r;
}

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

Mat4 TransformSequence::evaluate(std::int64_t time) const {
    if (!valid || times.empty()) return Mat4{};
    std::int64_t t = time - time_offset;
    const std::int64_t span = duration > 0 ? duration : times.back();
    if (span > 0) {
        if (play_mode == 0) {
            t %= span;
            if (t < 0) t += span;
        } else {
            t = std::clamp<std::int64_t>(t, 0, span);
        }
    }
    std::size_t i = 0;
    while (i + 1 < times.size() && times[i + 1] <= t) ++i;
    float alpha = 0;
    std::size_t j = i;
    if (i + 1 < times.size() && times[i + 1] > times[i]) {
        j = i + 1;
        alpha = std::clamp(static_cast<float>(double(t - times[i]) / double(times[j] - times[i])), 0.0f, 1.0f);
    }
    Vec3f translate{};
    Vec4f rotate{0, 0, 0, 1};
    Vec3f scale{1, 1, 1};
    if (!translations.empty()) translate = lerp(translations[std::min(i, translations.size() - 1)], translations[std::min(j, translations.size() - 1)], alpha);
    if (!rotations.empty()) rotate = slerp(rotations[std::min(i, rotations.size() - 1)], rotations[std::min(j, rotations.size() - 1)], alpha);
    if (!scales.empty()) scale = lerp(scales[std::min(i, scales.size() - 1)], scales[std::min(j, scales.size() - 1)], alpha);
    Mat4 m = translation(-center.x, -center.y, -center.z) * scaling(scale.x, scale.y, scale.z) *
             quaternion_matrix(rotate.x, rotate.y, rotate.z, rotate.w) * translation(center.x, center.y, center.z);
    m(3, 0) += translate.x;
    m(3, 1) += translate.y;
    m(3, 2) += translate.z;
    return m;
}

bool Node::child_enabled(std::size_t index) const {
    if (kind != NodeKind::switch_node) return true;
    const std::size_t word = index / 32;
    if (word >= mask.size()) return false;
    return (mask[word] >> (index % 32)) & 1u;
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
            a->sampler.mag_filter = a->texture.i32(5, 1);
            a->sampler.min_filter = a->texture.i32(6, 1);
            a->sampler.wrap_s = a->texture.i32(7, 1);
            a->sampler.wrap_t = a->texture.i32(8, 1);
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
            seq->play_mode = input.i32(8);
            seq->center = input.vec3(10).value_or(Vec3f{});
            seq->times = long_list(input.ref(11));
            seq->driven_channels = input.raw(15).empty() ? 0u : input.raw(15)[0];
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
        if (const auto list = view.ref(8)) {
            const auto data = list.memory(4);
            const auto count = list.u32(2);
            for (std::uint32_t i = 0; i < count && (i + 1) * 4 <= data.size(); ++i) n->lod_ranges.push_back(read_f32le(data, i * 4));
        }
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
void apply(const Attribute* a, RenderState& s, Mat4& model) {
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
        if (a->enabled && a->light) s.lights.push_back(a->light);
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
    default: break;
    }
}

// Runtime ACTION2 attributes. FADESET / FLASHSET append theirs to the
// beModelCtrlNode2 attribute list (+0x88), which the node's traversal
// (8030FAA4) pushes as overrides (801D5190: the stack keeps the outermost
// override as its effective top), so deeper igTextureStateAttr /
// igLightingStateAttr / igBlendStateAttr in the skin graphs cannot undo
// them: the 4-frame FLASH draws the model untextured and unlit, white.
void apply_overrides(const Node& n, RenderState& s) {
    if (n.color_override) {
        const auto c = *n.color_override;
        s.modulate = s.modulate ? Vec4f{s.modulate->x * c.x, s.modulate->y * c.y, s.modulate->z * c.z, s.modulate->w * c.w} : c;
    }
    if (n.blend_override && !s.blend_forced) {
        s.blend_enabled = *n.blend_override;
        s.blend_forced = true;
        if (s.blend_enabled) {
            s.blend_src = 4;
            s.blend_dst = 5;
        }
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
        for (const auto* a : n->attributes) apply(a, state, model);
        break;
    case NodeKind::blend_matrix_select: {
        for (const auto* a : n->attributes) apply(a, state, model);
        if (scope.pose && scope.pose->skeleton) {
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
        for (const auto* a : n->attributes) if (a->kind == AttrKind::light) state.lights.push_back(a);
        return;
    case NodeKind::light_state_set:
        for (const auto* a : n->attributes) apply(a, state, model);
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
        n->recorded = model;
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
        const Mat4 view = state.view.value_or(ctx.camera.view);
        const Mat4 mv = model * view;
        const float sx = length({mv(0, 0), mv(0, 1), mv(0, 2)});
        const float sy = length({mv(1, 0), mv(1, 1), mv(1, 2)});
        const float sz = length({mv(2, 0), mv(2, 1), mv(2, 2)});
        Mat4 aligned = scaling(sx, sy, sz);
        aligned(3, 0) = mv(3, 0);
        aligned(3, 1) = mv(3, 1);
        aligned(3, 2) = mv(3, 2);
        Mat4 inverse_view;
        if (invert(view, inverse_view)) model = aligned * inverse_view;
        break;
    }
    case NodeKind::lod: {
        apply_overrides(*n, state);
        const Vec3f origin{model(3, 0), model(3, 1), model(3, 2)};
        const float distance = length(origin - ctx.camera_position);
        for (std::size_t i = 0; i < n->children.size(); ++i) {
            const float low = i < n->lod_ranges.size() ? n->lod_ranges[i] : 0.0f;
            const float high = i + 1 < n->lod_ranges.size() ? n->lod_ranges[i + 1] : 3.0e38f;
            if (distance >= low && distance < high) visit(n->children[i], ctx, model, state, time, scope, depth + 1);
        }
        return;
    }
    case NodeKind::geometry: {
        for (const auto* a : n->attributes) apply(a, state, model);
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
        for (const auto* g : n->geometries) {
            const auto* data = ctx.cache->geometry(g);
            if (!data || !data->valid || !ctx.output) continue;
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
