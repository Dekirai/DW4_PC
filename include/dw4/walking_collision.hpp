#pragma once

#include "dw4/meshes.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dw4 {
class IgbDocument;

class WalkingCollisionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads one explicitly exported category-1 beHitLandModelInfo. The verified
// static profile is an outer switch containing the info's inner switch, with
// direct geometry children and no transforms. Stored coordinates therefore are
// world coordinates. The original CHECKLAND traversal reads the inner switch
// directly, retaining its stored child selection independently of the outer
// wrapper's mask. The wrapper is checked for the known static relationship.
// This reads original floor assets, not the complete original collision manager
// or query algorithm. Walls, dynamic objects and transformed profiles reject.
// Limits: 4,096 exports, 256 floor shapes and 1,048,576 combined vertices and
// triangles. Source documents and game bytes are never modified.
std::vector<MeshData> decode_walking_collision(const IgbDocument& document,
    std::uint32_t info_reference);

// The same static geometry profile for an explicitly selected exact category-2
// wall model. Retains the original inner switch mask, including an empty result
// when all walls are disabled. Door models stay in their stored state; this does
// not implement door interactions or the original wall response algorithm.
std::vector<MeshData> decode_walking_walls(const IgbDocument& document,
    std::uint32_t info_reference);

} // namespace dw4
