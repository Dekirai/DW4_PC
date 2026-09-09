#pragma once

#include "dw4/animation_math.hpp"
#include "dw4/igb_document.hpp"

#include <span>
#include <string>
#include <vector>

namespace dw4 {

class AnimationHierarchyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AnimationHierarchyBone {
    std::uint32_t reference{};
    std::string name;
    std::int32_t parent{-1};
    std::int32_t joint_index{-1};
};

// Validated skeleton for the original 80202FCC no-modifier traversal. The
// skipped bone zero is an unparented helper; every other parent precedes its
// child. Joint output indices may be permuted. Inverse matrices retain their
// SERIALIZED order, consumed once per joint-bearing bone in traversal order.
class AnimationHierarchyDefinition {
public:
    AnimationHierarchyDefinition(std::uint32_t skeleton_reference,
        std::vector<AnimationHierarchyBone> bones,
        std::vector<AnimationMatrix> inverse_joint_matrices);
    std::uint32_t skeleton_reference() const noexcept { return skeleton_reference_; }
    const std::vector<AnimationHierarchyBone>& bones() const noexcept { return bones_; }
    const std::vector<AnimationMatrix>& inverse_joint_matrices() const noexcept { return inverse_joint_matrices_; }
private:
    std::uint32_t skeleton_reference_{};
    std::vector<AnimationHierarchyBone> bones_;
    std::vector<AnimationMatrix> inverse_joint_matrices_;
};

AnimationHierarchyDefinition decode_animation_hierarchy(const IgbDocument& document,
    std::uint32_t skeleton_reference);

// Preserve the caller's bone_cache[0]; ignore local[0] for composition. For
// bone1+, parent zero copies local, otherwise write parent_cache * local.
// Joint output[bmIdx] = bone_cache * next_serialized_inverse. Both output
// vectors keep their original contents if validation or arithmetic fails.
// Dimensions and finite affine inputs/results are checked. This neither
// samples animation nor changes a combiner's hierarchy-valid state.
void evaluate_animation_hierarchy(const AnimationHierarchyDefinition& definition,
    std::span<const AnimationMatrix> local, std::vector<AnimationMatrix>& bone_cache,
    std::vector<AnimationMatrix>& joint_cache);

} // namespace dw4
