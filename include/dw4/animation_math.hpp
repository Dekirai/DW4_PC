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

} // namespace dw4
