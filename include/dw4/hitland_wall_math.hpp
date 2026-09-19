#pragma once

#include "dw4/collision_math.hpp"

#include <cstdint>

namespace dw4 {
// CHECKWALL 802FE7F0 finite arithmetic under startup NI=1/RN=00. Arithmetic
// operands/results must be finite; side/normal lengths must be positive.
// Loads retain subnormals and signed zeros. Tiny arithmetic results flush
// before rounding. Unsupported reached arithmetic throws CollisionMathError.
// Helpers return local calculations; the recipient owns publication order.

// 802FE2AC / 80129134: Euler (0, binary32 pi/2, 0), including its rounded
// nonzero cosine and the signed zeros produced by the original products.
AnimationMatrix original_hitland_wall_rotation();

float original_hitland_wall_radius(float width);
float original_hitland_wall_half_radius(float radius);
float original_hitland_wall_twice_radius(float radius);
float original_hitland_wall_forward_offset(float radius);
// Reached frsp in pass C, including NI=1 tiny-result flushing.
float original_hitland_wall_narrow(float value);

std::array<float,3> original_hitland_wall_height(
    const std::array<float,3>& position,float down);
std::array<float,3> original_hitland_wall_side(
    const std::array<float,3>& direction,const AnimationMatrix& rotation);

// Distinct original expressions: separately rounded scaled displacement and
// coordinate sum in passes A/B/center C; directly fused endpoints in pass C.
std::array<float,3> original_hitland_wall_offset(
    const std::array<float,3>& base,float scale,const std::array<float,3>& direction);
std::array<float,3> original_hitland_wall_fused_offset(
    const std::array<float,3>& base,float scale,const std::array<float,3>& direction);

std::array<float,3> original_hitland_wall_longitudinal(
    const std::array<float,3>& position,float doubled_radius,float distance,
    const std::array<float,3>& direction);
// The normal's self-ratio is clamped as in the original, then uses the
// independently executed 17-input acos/narrow/cos/narrow result set near 1.
// A ratio outside that verified set is an explicit arithmetic boundary.
std::array<float,3> original_hitland_wall_lateral(
    const std::array<float,3>& position,float radius,float distance,
    const std::array<float,3>& normal);

// 80300940 consumes one scalar: mode 0 maximum, mode 1 minimum. Equal and
// unordered values preserve the destination, including its sign of zero.
void original_hitland_wall_extremum(float& destination,float source,std::uint32_t mode);
} // namespace dw4
