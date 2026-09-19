#pragma once

#include "dw4/animation_math.hpp"

namespace dw4 {

// USA 8012A92C, with adjugate 8012AA04 and determinant 8012AD50. Retains the
// original scalar FMA order and startup NI=1/RN=00 arithmetic. All source and
// intermediate values must be finite; unsupported arithmetic throws
// AnimationMathError. The source and destination may be the same matrix.
// Returns false when abs(determinant) is below binary32 min-normal, leaving
// destination untouched. Equality succeeds. Successful results are stored in
// flat index order; a scaling overflow preserves the already-written prefix.
// Destination values are not read or validated, including on singular input.
bool original_matrix_inverse(const AnimationMatrix& source, AnimationMatrix& destination);

} // namespace dw4
