#pragma once

#include "dw4/meshes.hpp"
#include "dw4/scene_controls.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;
class SceneError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
using SceneMatrix = std::array<double,16>; // Column-major, column vectors.

struct SceneInfo {
    std::uint32_t info_reference{};
    std::uint32_t root_reference{};
    std::string name;
    std::string root_type;
    Vec3 up_direction{};
    bool supported{}; // Export descriptor profile; traversal is checked separately.
    std::string diagnostic;
};
struct SceneDiagnostic {
    std::uint32_t node_reference{};
    std::vector<std::uint32_t> path;
    std::string reason;
};
struct SceneInstance {
    std::uint32_t node_reference{};
    std::uint32_t geometry_reference{};
    std::string name;
    std::vector<std::uint32_t> path; // Root through owning geometry node.
    SceneMatrix world_transform{};
    MeshData mesh; // World positions and bounds; UVs remain an explicit host load.
};
struct SceneData {
    SceneInfo info;
    bool complete{};
    std::optional<std::uint32_t> inspected_root_child;
    std::optional<SwitchControlRequest> root_control;
    std::vector<SceneInstance> instances;
    MeshBounds bounds{};
    std::vector<SceneDiagnostic> diagnostics;
    std::size_t visited_nodes{};
    std::size_t transform_nodes{};
    std::size_t vertex_count{};
    std::size_t triangle_count{};
};
struct SceneOptions {
    // Explicit inspection of one child of an igSwitch export root. This replaces
    // only the root's stored selection; nested switches retain their stored bits.
    std::optional<std::uint32_t> inspect_root_child;
    // Explicit original controller command targeting exactly the selected root.
    // Mutually exclusive with inspection; nested switches retain stored state.
    std::optional<SwitchControlRequest> root_control;
};

// Lists explicit igSceneInfo exports. The selected reference passed to decode
// identifies the igSceneInfo, not its root or its index in the returned vector.
std::vector<SceneInfo> list_igb_scenes(const IgbDocument& document);
// Static, local graph profile only. Unsupported behavior records diagnostics
// and complete=false; callers must not present those partial instances as a
// complete scene. Malformed data and resource-limit violations throw SceneError.
SceneData decode_igb_scene(const IgbDocument& document, std::uint32_t info_reference,
    SceneOptions options = {});
} // namespace dw4
