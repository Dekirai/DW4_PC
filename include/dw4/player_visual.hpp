#pragma once

#include "dw4/skinning.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;
class PlayerVisualError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct PlayerModelInfo {
    std::uint32_t reference_index{};
    std::string name;
    std::uint32_t instance_count{};
    bool supported{};
    std::string diagnostic;
};
struct PlayerVisualRequest {
    std::uint32_t model_reference{};
    std::uint32_t instance_index{};
    std::uint32_t branch_index{}; // Explicit igLod child; no distance-based LOD.
};
struct PlayerVisualInfo {
    PlayerVisualRequest request;
    std::string model_name;
    std::uint32_t model_node_reference{};
    std::uint32_t lod_reference{};
    std::uint32_t actor_reference{};
    std::uint32_t appearance_reference{};
    std::uint32_t skin_reference{};
    std::uint32_t geometry_reference{};
    std::uint32_t branch_count{};
    bool supported{};
    std::string diagnostic;
    std::string provenance;
};
struct PlayerVisualData {
    PlayerVisualInfo info;
    MeshData mesh; // Skin-local source positions; UVs remain an explicit load.
    SkinningData skin;
};

// Only exact exported beModelCtrlInfo records of KIND_PLAYER are candidates.
std::vector<PlayerModelInfo> list_igb_player_models(const IgbDocument& document);
PlayerVisualInfo inspect_igb_player_visual(const IgbDocument& document, PlayerVisualRequest request);
// Follows the selected model instance's ONE record and explicit appearance
// branch into a single supported skin. The authored model-node matrix is only
// provenance: original player generation replaces it with PLAYERPOINT state.
// This does not execute player generation, automatic LOD or actor controllers.
PlayerVisualData decode_igb_player_visual(const IgbDocument& document, PlayerVisualRequest request);
} // namespace dw4
