#pragma once

#include "dw4/animation_math.hpp"

namespace dw4 {
// Player input 8034C67C..C6B8, given the original caller's inverse matrix.
// Computes its +Z direction, the two MSL atan2 results and their rounded sum.
// Finite reached operands only; unused matrix fields are not inspected.
// NI=1/RN=00 single arithmetic preserves signed zero and flushes tiny results
// before rounding. Unsupported reached arithmetic throws AnimationMathError.
float original_player_input_heading(const AnimationMatrix& inverse_camera,
    float left_x,float left_y);
} // namespace dw4
