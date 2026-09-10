#pragma once

#include "dw4/animation_math.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace dw4 {
class CollisionMathError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

struct CollisionBox {
    std::array<float,3> minimum{},maximum{};
    bool operator==(const CollisionBox&) const = default;
};

struct CollisionBoxDistances {
    std::array<float,3> minimum{},maximum{};
    bool operator==(const CollisionBoxDistances&) const = default;
};

// Original 80122D04 eight-corner transform: inverted X returns unchanged;
// otherwise transform the minimum point once, then add ordered matrix edges.
// Finite inverted input bounds remain valid.
CollisionBox original_transform_box(const AnimationMatrix& matrix,const CollisionBox& box);
// Original scalar center and separate absolute center-to-endpoint distances.
std::array<float,3> original_box_center(const CollisionBox& box);
CollisionBoxDistances original_box_distances(const CollisionBox& box);
// Transform the local box, then restore the previously captured local
// distances around its transformed center, as 80307EAC does for target bodies.
CollisionBox original_body_box(const AnimationMatrix& matrix,const CollisionBox& box,
    const CollisionBoxDistances& distances);
// Six original inclusive comparisons; touching and finite inverted boxes
// follow the same ordered tests without a separate emptiness predicate.
bool original_boxes_overlap(const CollisionBox& incoming,const CollisionBox& body);

// 8030FEEC uses 80122D04/8012D498 to transform the offset as a degenerate
// AABox, then applies half the authored full extents along world axes.
// Includes the original homogeneous-W branch under startup NI=1/RN=00.
// Finite inputs/results are required; negative extents retain inverted bounds.
CollisionBox original_hitbox_box(const AnimationMatrix& source_matrix,
    const std::array<float,3>& offset,const std::array<float,3>& box_size);
} // namespace dw4
