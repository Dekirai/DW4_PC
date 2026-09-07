#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;

class MeshError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

struct MeshBounds {
    Vec3 min;
    Vec3 max;
};

struct MeshData {
    std::vector<Vec3> positions;
    std::vector<std::uint32_t> indices; // Consecutive triples form triangles.
    MeshBounds bounds;
};

struct MeshInfo {
    std::uint32_t reference_index{}; // Global reference within the owning IGB.
    std::string name;
    std::string topology_name;
    std::uint32_t vertex_count{};
    std::uint32_t triangle_count{};
    bool supported{};
    std::string diagnostic;
};

// Supports unindexed enum-3 triangle lists and indexed enum-4 triangle strips
// with the verified little-endian u16 indices and explicit u32 group lengths.
// Positions remain in the stored local coordinate space. No scene transforms,
// material assignments, skinning, UVs, or normals are reconstructed here.
// Limits: 1,048,576 vertices/expanded triangles, 4,194,304 strip indices; position stream bytes
// must equal vertex_count * 12. Repeated-index strip degenerates are omitted.
// Listing checks descriptors and source ranges; decoding additionally requires
// finite float coordinates. Unsupported geometry remains visible with a reason.
std::vector<MeshInfo> list_igb_meshes(const IgbDocument& document);
MeshData decode_igb_mesh(const IgbDocument& document, std::uint32_t reference_index);

} // namespace dw4
