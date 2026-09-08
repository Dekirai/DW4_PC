#pragma once

#include "dw4/meshes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;

class SkinningError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Column-major host matrices acting on column vectors. Original flat matrix
// values retain their order; translation is at elements 12, 13 and 14.
using SkinMatrix = std::array<double, 16>;

struct SkinBone {
    std::uint32_t reference_index{};
    std::string name;
    std::int32_t parent{-1}; // Bone ordinal, -1 for a root.
    std::int32_t joint_index{-1}; // -1 denotes a helper without an inverse-bind matrix.
};

struct SkinJoint {
    std::uint32_t bone_index{};
    std::int32_t parent_joint{-1}; // Nearest ancestor with a joint matrix.
    SkinMatrix inverse_bind{};
    SkinMatrix bind_world{};
    SkinMatrix bind_local{};
};

struct VertexInfluence {
    std::uint32_t palette_index{};
    std::uint32_t joint_index{};
    float weight{};
};

struct SkinningInfo {
    bool supported{};
    std::string diagnostic;
    std::uint32_t geometry_reference{};
    std::optional<std::uint32_t> geometry_node_reference;
    std::optional<std::uint32_t> selector_reference;
    std::optional<std::uint32_t> skin_reference;
    std::optional<std::uint32_t> database_reference;
    std::optional<std::uint32_t> skeleton_reference;
    std::vector<std::uint32_t> actor_references;
    std::string skin_name;
    std::string skeleton_name;
    std::string provenance;
    std::uint32_t vertex_count{};
    std::uint32_t bone_count{};
    std::uint32_t joint_count{};
};

struct SkinningData {
    SkinningInfo info;
    std::vector<SkinBone> bones;
    std::vector<SkinJoint> joints;
    std::vector<std::uint32_t> palette_to_joint;
    // Current verified profile has exactly one full-weight influence per vertex.
    std::vector<VertexInfluence> influences;
    std::vector<Vec3> source_positions;
};

struct BoneRotation {
    std::uint32_t bone_index{};
    // Inspection control, not original animation: X then Y then Z, degrees.
    Vec3 degrees{};
};

struct SkinPose {
    std::vector<Vec3> positions;
    std::vector<Vec3> joint_positions; // Joint ordinal order, in stored mesh space.
};

// Requires an explicit unique geometry -> selector -> skin -> database link;
// that database's skeleton list must contain exactly one skeleton. Actor
// appearance links are reported only as provenance. No first-skeleton fallback.
SkinningInfo inspect_igb_skinning(const IgbDocument& document, std::uint32_t geometry_reference);
SkinningData decode_igb_skinning(const IgbDocument& document, std::uint32_t geometry_reference);

// Recover bind frames by affine inversion; do not interpret skeleton offset
// arrays as a reference pose. Helpers have no recovered bind frame and cannot
// be selected. Null rotation reconstructs the original stored positions.
// Nonzero rotation is a user-authored local joint delta, not decoded game motion.
SkinPose evaluate_skinning(const SkinningData& data, std::optional<BoneRotation> rotation = std::nullopt);

} // namespace dw4
