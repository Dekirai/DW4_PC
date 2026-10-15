#pragma once

#include <array>
#include <stdexcept>

namespace dw4 {

class AnimationMathError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Recovered USA quaternion interpolation at guest 0x8012be90. Components are
// XYZW. Uses the original shortest-sign choice, 0.08 threshold, unnormalized
// linear branch and polynomial spherical branch. Neither input nor output is
// normalized. Blend must be finite and in [0,1]; malformed math throws.
std::array<float, 4> original_interpolate_rotation(const std::array<float, 4>& a,
    const std::array<float, 4>& b, float blend);

using AnimationMatrix = std::array<float, 16>;
// 80128690: conventional column-major left * right (guest r5 * r4).
// Round the first product to float, then fuse terms 1, 2 and 3 in order.
// Uses the startup NI=1, nearest-even profile: retain subnormal inputs and
// flush each tiny unrounded result to signed zero. Inputs/results must be finite.
AnimationMatrix original_matrix_multiply(const AnimationMatrix& left, const AnimationMatrix& right);
// Single-influence CPU skin loop 800FAA70: first X product, fused Y/Z terms,
// then a separate translation add under startup NI=1/RN=00. Return XYZ only;
// no homogeneous division. All inputs and returned components must be finite.
std::array<float,3> original_skin_point(const AnimationMatrix& matrix, const std::array<float,3>& point);
// 8012BCDC writes only the nine rotation entries of an existing flat matrix.
// Scalar operations use startup NI=1/RN=00, retaining subnormal inputs and
// flushing tiny arithmetic results. Preserve all seven untouched entries.
void original_rotation_matrix(const std::array<float, 4>& rotation, AnimationMatrix& matrix);
// 8012B8A8: inverse conversion used by interrupted-transition snapshots.
// Preserves branch/tie order and nonunit output; does not orthonormalize.
std::array<float, 4> original_matrix_rotation(const AnimationMatrix& matrix);
// 8012C15C: normalized linear blend (1 - t) * a + t * b on the shorter arc
// (b negated when the fused XYZW dot product is below 0), divided by its
// length; no spherical weights. Keeps the guest operation order and fused
// multiply/adds. Like the guest it does not check its input: a zero-length
// blend divides by zero.
std::array<float, 4> original_quaternion_nlerp(const std::array<float, 4>& a, const std::array<float, 4>& b, float t);
// 801299D8: normalizes the quaternion, then writes only the nine rotation
// entries of an existing flat matrix (same layout and signs as 8012BCDC);
// the other seven entries, the translation among them, stay. No input checks.
void original_normalized_rotation_matrix(const std::array<float, 4>& rotation, AnimationMatrix& matrix);

} // namespace dw4
