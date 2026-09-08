#pragma once

#include "dw4/meshes.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;
class PlayerPlacementError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
using PlayerPlacementMatrix = std::array<float,16>;
struct PlayerPlacementInfo {
    std::uint32_t reference_index{};
    std::string name;
    std::uint32_t polygon_switch_reference{};
    std::uint32_t group_count{};
    bool supported{};
    std::string diagnostic;
};
struct PlayerPlacement {
    std::uint32_t info_reference{};
    std::uint32_t group_reference{};
    std::uint32_t transform_reference{};
    std::uint32_t group_index{};
    std::uint32_t player_index{};
    std::string name;
    PlayerPlacementMatrix matrix{}; // Original column-major matrix; no normalization or parent composition.
};

// Only exact bePoint01Info descriptors on the explicit igInfoList are eligible.
// This static inspection profile requires four finite affine point transforms
// per group. Listing is bounded across all exported groups and points.
std::vector<PlayerPlacementInfo> list_igb_player_placements(const IgbDocument& document);
// Explicit selection only: no SETGENERATEPOS state, startup scheduling, or
// out-of-range fallback. The original matrix replaces the generated actor's
// outer transform; authored transforms below that boundary remain separate.
PlayerPlacement decode_igb_player_placement(const IgbDocument& document,
    std::uint32_t info_reference, std::uint32_t group_index, std::uint32_t player_index);
// Applies the placement once to points already expressed below the replaced
// actor-node boundary, with finite-input and output-overflow validation.
std::vector<Vec3> apply_player_placement(const PlayerPlacement& placement, std::span<const Vec3> positions);
} // namespace dw4
