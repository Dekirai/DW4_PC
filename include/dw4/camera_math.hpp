#pragma once

#include "dw4/animation_math.hpp"

#include <span>

namespace dw4 {
// Matrix portion of USA 8012A860: transpose the 3x3 rotation and publish its
// negative rotated translation. This specialized operation does not test or
// invert scale/shear. Source/destination may coincide or partially overlap;
// preserve the original interleaved reads/writes, including alias effects.
// NI=1/RN=00 arithmetic retains subnormal operands and flushes tiny results
// before rounding. Unsupported reached arithmetic throws AnimationMathError
// after preserving every completed destination store. No camera is selected.
void original_camera_view_inverse(std::span<const float,16> source,
    std::span<float,16> destination);
} // namespace dw4
