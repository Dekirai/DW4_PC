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
// 8012BCDC writes only the nine rotation entries of an existing flat matrix.
// Preserve float operation order and all translation/affine entries.
void original_rotation_matrix(const std::array<float, 4>& rotation, AnimationMatrix& matrix);
// 8012B8A8: inverse conversion used by interrupted-transition snapshots.
// Preserves branch/tie order and nonunit output; does not orthonormalize.
std::array<float, 4> original_matrix_rotation(const AnimationMatrix& matrix);

} // namespace dw4
