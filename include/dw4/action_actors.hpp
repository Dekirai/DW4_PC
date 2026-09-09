#pragma once

#include "dw4/animation_math.hpp"
#include "dw4/igb_document.hpp"

#include <optional>
#include <string>

namespace dw4 {
class AnimationCombiner;
class AnimationSourceRuntime;

class ActionActorError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ActionActorDefinition {
    std::uint32_t reference{}, appearance_reference{};
    std::size_t bone_count{}, joint_count{};
    AnimationMatrix transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
};

struct ActionActorState {
    std::uint32_t reference{}, appearance_reference{};
    std::optional<std::uint32_t> combiner_reference;
    std::vector<AnimationMatrix> bone_matrices, joint_matrices;
    AnimationMatrix model_view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    AnimationMatrix inverse_view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    bool cache_used{};
};

// Owns the verified ACTION2 allocation/deallocation effects for every actor in
// one source-qualified RAM. These identity arrays are not an evaluated pose:
// original actor traversal and its shared combiner hierarchy remain separate.
class ActionActorGroup {
public:
    ActionActorGroup(std::string source, std::uint32_t ram_reference,
        std::uint32_t combiner_reference, std::vector<ActionActorDefinition> definitions);
    const std::string& source() const noexcept { return source_; }
    std::uint32_t ram_reference() const noexcept { return ram_reference_; }
    std::uint32_t combiner_reference() const noexcept { return combiner_reference_; }
    const std::vector<ActionActorDefinition>& definitions() const noexcept { return definitions_; }
    const std::vector<ActionActorState>& states() const noexcept { return states_; }
    // Explicit native cache access; activation validates ownership and sizes.
    ActionActorState& state(std::size_t index) { return states_.at(index); }
    // 802F1F60: stop the ENTIRE loop at the first allocated bone array, before
    // assigning its combiner. Returns the number of newly allocated actors.
    std::size_t activate(std::uint32_t combiner_reference);
    // Ordinary actor-entry traversal at already converted node time. Retain
    // view state and write/evaluate the owning combiner's shared matrices.
    void traverse(AnimationCombiner& combiner,AnimationSourceRuntime& sources,
        std::size_t actor_index,const AnimationMatrix& model_view,
        const AnimationMatrix& inverse_view,std::uint64_t node_time);
    // 802F1D18: release both arrays for all actors, retaining combiner links.
    void deactivate() noexcept;
private:
    std::string source_;
    std::uint32_t ram_reference_{}, combiner_reference_{};
    std::vector<ActionActorDefinition> definitions_;
    std::vector<ActionActorState> states_;
};

// Prepared profile: local typed lists, unique actors/appearances, null actor
// combiner/matrix fields, finite transforms and empty modifier/attribute
// lists. The explicit combiner selects the skeleton used for allocation.
ActionActorGroup decode_action_actor_group(const IgbDocument& document,
    std::uint32_t ram_reference, std::uint32_t combiner_reference, std::string source);

} // namespace dw4
