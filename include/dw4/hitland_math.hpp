#pragma once

#include "dw4/collision_math.hpp"

namespace dw4 {
// HITLANDDELIVER/HITWALL 802FDAF0..DC0C. Only current/previous X and Z
// participate; Y is the original positive zero. Preserve NI=1 subtraction,
// ordered fused squared length, rounded length, reciprocal and products.
// Finite used coordinates, finite positive squared length and finite output
// are the bounded native profile. Zero/underflowed displacement rejects;
// the caller retains its earlier packet prefix and exact X/Z equality gate.
std::array<float,3> original_hitland_wall_direction(
    const std::array<float,3>& current,const std::array<float,3>& previous);

// Common HITLANDMODEL traversal broad phase 80300974. Four ordered inclusive
// comparisons of X/Z only; ignored Y and later components are not validated.
// Unordered reached comparisons reject, retaining the original short circuit.
bool original_hitland_xz_overlap(const CollisionBox& model,const CollisionBox& query);
} // namespace dw4
