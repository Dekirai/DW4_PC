#pragma once

#include "dw4/animation_math.hpp"

namespace dw4 {
class CollisionMathError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

struct CollisionBox {
    std::array<float,3> minimum{},maximum{};
    bool operator==(const CollisionBox&) const = default;
};

// 8030FEEC uses 80122D04/8012D498 to transform the offset as a degenerate
// AABox, then applies half the authored full extents along world axes.
// Includes the original homogeneous-W branch under startup NI=1/RN=00.
// Finite inputs/results are required; negative extents retain inverted bounds.
CollisionBox original_hitbox_box(const AnimationMatrix& source_matrix,
    const std::array<float,3>& offset,const std::array<float,3>& box_size);
} // namespace dw4
