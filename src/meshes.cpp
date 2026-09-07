#include "dw4/meshes.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace dw4 {
namespace {
constexpr std::uint32_t max_vertices = 1024u * 1024u;
constexpr std::uint32_t max_strip_indices = 4u * 1024u * 1024u;
constexpr std::uint32_t max_triangles = 1024u * 1024u;
constexpr std::size_t stream_table_bytes = 20u * 4u;

[[noreturn]] void fail(const std::string& message) {
    throw MeshError("Mesh: " + message);
}

std::uint32_t little_word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) fail("truncated 32-bit stream value");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
        (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

float coordinate(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const auto value = std::bit_cast<float>(little_word(bytes, offset));
    if (!std::isfinite(value)) fail("position stream contains a non-finite coordinate");
    return value;
}

std::uint32_t little_index(std::span<const std::uint8_t> bytes, std::size_t index) {
    const auto offset = index * 2;
    if (offset > bytes.size() || bytes.size() - offset < 2) fail("truncated 16-bit index");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8);
}

std::uint32_t required_reference(const IgbDocument& document, const IgbObject& object, std::uint16_t slot) {
    const auto ref = document.field_reference(object, slot);
    if (!ref) fail("required geometry or vertex-stream reference is null");
    return *ref;
}

struct MeshSource {
    std::uint32_t vertex_count{};
    std::uint32_t triangle_count{};
    std::span<const std::uint8_t> positions;
    std::span<const std::uint8_t> indices;
    std::span<const std::uint8_t> lengths;
    bool strips{};
};

template<class Emit> void strip_triangles(const MeshSource& source, Emit&& emit) {
    std::size_t start = 0;
    for (std::size_t group = 0; group < source.lengths.size() / 4; ++group) {
        const auto length = little_word(source.lengths, group * 4);
        for (std::uint32_t triangle = 0; triangle < length - 2; ++triangle) {
            auto a = little_index(source.indices, start + triangle);
            auto b = little_index(source.indices, start + triangle + 1);
            const auto c = little_index(source.indices, start + triangle + 2);
            if (triangle & 1u) std::swap(a, b);
            if (a != b && b != c && a != c) emit(a, b, c);
        }
        start += length;
    }
}

MeshSource inspect_mesh(const IgbDocument& document, const IgbObject& geometry) {
    if (!document.is_a(geometry, "igGeometryAttr1_5")) fail("selected reference is not an igGeometryAttr1_5");
    if (document.metadata.header.byte_order != IgbByteOrder::little)
        fail("big-endian vertex-stream storage has not been verified");
    const auto topology = document.field_u32(geometry, 6);
    if (topology != 3 && topology != 4) fail("topology enum " + std::to_string(topology) + " is not a supported triangle profile");
    if (document.field_u32(geometry, 8) != 0 || document.field_i32(geometry, 10) != 0)
        fail("nonzero geometry offset/state fields have not been verified");
    const auto& vertices = document.object(required_reference(document, geometry, 4));
    if (!document.is_a(vertices, "igVertexArray1_1")) fail("geometry vertex reference is not an igVertexArray1_1");
    if (document.field_u32(vertices, 4) != 0) fail("nonzero vertex-array offset/state has not been verified");
    MeshSource result;
    result.vertex_count = document.field_u32(vertices, 3);
    const auto primitive_count = document.field_u32(geometry, 7);
    if (!result.vertex_count || result.vertex_count > max_vertices)
        fail("vertex count must be between 1 and 1,048,576");
    const auto table = document.memory(required_reference(document, vertices, 2));
    if (table.size() != stream_table_bytes) fail("vertex stream table must contain exactly twenty 32-bit references");
    for (std::size_t i = 0; i < 20; ++i) {
        const auto ref = little_word(table, i * 4);
        if (ref == 0xffffffffu) {
            if (i == 0) fail("position stream reference is null");
            continue;
        }
        // Nested stream references are not typed object fields. Validate every
        // populated table element as a memory record before using stream zero.
        const auto memory = document.memory(ref);
        if (i == 0) result.positions = memory;
    }
    if (result.positions.size() != std::uint64_t(result.vertex_count) * 12)
        fail("position stream byte count is not exactly vertex_count * 12");
    if (topology == 3) {
        if (document.field_reference(geometry, 5)) fail("indexed triangle lists have not been verified");
        if (document.field_reference(geometry, 13)) fail("grouped triangle lists have not been verified");
        if (std::uint64_t(primitive_count) * 3 != result.vertex_count)
            fail("unindexed triangle count does not match the vertex count");
        result.triangle_count = primitive_count;
    } else {
        result.strips = true;
        const auto& index_array = document.object(required_reference(document, geometry, 5));
        if (!document.is_a(index_array, "igIndexArray")) fail("strip index reference is not an igIndexArray");
        const auto index_count = document.field_u32(index_array, 3);
        if (index_count < 3 || index_count > max_strip_indices) fail("strip index count is outside the 3..4,194,304 limit");
        if (document.field_u32(index_array, 4) != 0 || document.field_u32(index_array, 5) != 0)
            fail("index format/offset differs from the verified little-endian 16-bit profile");
        result.indices = document.memory(required_reference(document, index_array, 2));
        if (result.indices.size() != std::uint64_t(index_count) * 2) fail("index byte count is not exactly index_count * 2");
        for (std::uint32_t index = 0; index < index_count; ++index)
            if (little_index(result.indices, index) >= result.vertex_count) fail("index points outside the vertex array");
        const auto& length_array = document.object(required_reference(document, geometry, 13));
        if (!document.is_a(length_array, "igPrimLengthArray1_1")) fail("primitive lengths reference is not an igPrimLengthArray1_1");
        const auto group_count = document.field_u32(length_array, 3);
        if (!group_count || group_count != primitive_count || group_count > index_count / 3)
            fail("strip group count does not match its geometry or index count");
        if (document.field_u32(length_array, 4) != 32) fail("primitive lengths are not the verified 32-bit profile");
        result.lengths = document.memory(required_reference(document, length_array, 2));
        if (result.lengths.size() != std::uint64_t(group_count) * 4) fail("primitive-length byte count is inconsistent");
        std::uint64_t sum = 0;
        for (std::uint32_t group = 0; group < group_count; ++group) {
            const auto length = little_word(result.lengths, static_cast<std::size_t>(group) * 4);
            if (length < 3) fail("triangle strip has fewer than three indices");
            sum += length;
            if (sum > index_count) fail("primitive lengths exceed the index buffer");
        }
        if (sum != index_count) fail("primitive lengths do not consume the index buffer exactly");
        strip_triangles(result, [&](std::uint32_t, std::uint32_t, std::uint32_t) {
            if (result.triangle_count == max_triangles) fail("expanded triangles exceed the 1,048,576 limit");
            ++result.triangle_count;
        });
        if (!result.triangle_count) fail("triangle strips contain only repeated-index degenerates");
    }
    return result;
}

} // namespace

std::vector<MeshInfo> list_igb_meshes(const IgbDocument& document) {
    std::vector<MeshInfo> result;
    for (const auto& geometry : document.objects) {
        if (!document.is_a(geometry, "igGeometryAttr1_5")) continue;
        MeshInfo info;
        info.reference_index = geometry.reference_index;
        info.name = "Geometry " + std::to_string(geometry.reference_index);
        info.topology_name = "Unsupported geometry";
        try {
            const auto topology = document.field_u32(geometry, 6);
            info.topology_name = topology == 3 ? "Triangles (enum 3)" : topology == 4 ? "Triangle strips (enum 4)" : "Topology enum " + std::to_string(topology);
            if (topology == 3) info.triangle_count = document.field_u32(geometry, 7);
            if (const auto vertex_ref = document.field_reference(geometry, 4))
                info.vertex_count = document.field_u32(document.object(*vertex_ref), 3);
            const auto source = inspect_mesh(document, geometry);
            info.vertex_count = source.vertex_count;
            info.triangle_count = source.triangle_count;
            info.supported = true;
        } catch (const std::exception& error) {
            info.diagnostic = error.what();
        }
        result.push_back(std::move(info));
    }
    return result;
}

MeshData decode_igb_mesh(const IgbDocument& document, std::uint32_t reference_index) {
    try {
        const auto source = inspect_mesh(document, document.object(reference_index));
        MeshData result;
        result.positions.reserve(source.vertex_count);
        result.indices.reserve(static_cast<std::size_t>(source.triangle_count) * 3);
        for (std::uint32_t index = 0; index < source.vertex_count; ++index) {
            const auto offset = static_cast<std::size_t>(index) * 12;
            const Vec3 point{coordinate(source.positions, offset), coordinate(source.positions, offset + 4),
                coordinate(source.positions, offset + 8)};
            result.positions.push_back(point);
            if (!source.strips) result.indices.push_back(index);
            if (index == 0) result.bounds = {point, point};
            else {
                result.bounds.min.x = std::min(result.bounds.min.x, point.x);
                result.bounds.min.y = std::min(result.bounds.min.y, point.y);
                result.bounds.min.z = std::min(result.bounds.min.z, point.z);
                result.bounds.max.x = std::max(result.bounds.max.x, point.x);
                result.bounds.max.y = std::max(result.bounds.max.y, point.y);
                result.bounds.max.z = std::max(result.bounds.max.z, point.z);
            }
        }
        if (source.strips) strip_triangles(source, [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            result.indices.push_back(a); result.indices.push_back(b); result.indices.push_back(c);
        });
        return result;
    } catch (const IgbError& error) {
        throw MeshError(error.what());
    }
}

} // namespace dw4
