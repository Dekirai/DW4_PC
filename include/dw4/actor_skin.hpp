#pragma once

#include "dw4/action_actors.hpp"
#include "dw4/animation_combiner.hpp"
#include "dw4/skinning.hpp"

namespace dw4 {

struct ActorSkinFrame;

class ActorSkinError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One source-qualified actor's explicit main-skin geometry. Owned decoded data
// stays immutable while actor and combiner caches advance independently.
class ActorSkinBinding {
public:
    ActorSkinBinding(std::string source,std::uint32_t ram_reference,
        std::uint32_t combiner_reference,std::size_t actor_index,
        std::uint32_t actor_reference,std::uint32_t appearance_reference,SkinningData skin);
    const std::string& source() const noexcept { return source_; }
    std::uint32_t ram_reference() const noexcept { return ram_reference_; }
    std::uint32_t combiner_reference() const noexcept { return combiner_reference_; }
    std::size_t actor_index() const noexcept { return actor_index_; }
    std::uint32_t actor_reference() const noexcept { return actor_reference_; }
    std::uint32_t appearance_reference() const noexcept { return appearance_reference_; }
    std::uint32_t skin_reference() const noexcept { return *skin_.info.skin_reference; }
    std::uint32_t selector_reference() const noexcept { return *skin_.info.selector_reference; }
    std::uint32_t database_reference() const noexcept { return *skin_.info.database_reference; }
    std::uint32_t skeleton_reference() const noexcept { return *skin_.info.skeleton_reference; }
    std::uint32_t geometry_reference() const noexcept { return skin_.info.geometry_reference; }
    const SkinningData& skin() const noexcept { return skin_; }
private:
    friend ActorSkinFrame capture_actor_skin(const ActorSkinBinding&,const ActionActorGroup&,const AnimationCombiner&);
    friend SkinPose evaluate_actor_skin(const ActorSkinBinding&,const ActorSkinFrame&);
    std::shared_ptr<const std::uint8_t> identity_{std::make_shared<const std::uint8_t>(std::uint8_t{})};
    std::string source_;
    std::uint32_t ram_reference_{},combiner_reference_{},actor_reference_{},appearance_reference_{};
    std::size_t actor_index_{};
    SkinningData skin_;
};

struct ActorSkinFrame {
    AnimationMatrix model_view;
    std::vector<AnimationMatrix> palette_matrices;
    std::vector<Vec3> joint_positions; // Joint ordinal order, from owning bone cache translations.
private:
    friend ActorSkinFrame capture_actor_skin(const ActorSkinBinding&,const ActionActorGroup&,const AnimationCombiner&);
    friend SkinPose evaluate_actor_skin(const ActorSkinBinding&,const ActorSkinFrame&);
    std::shared_ptr<const std::uint8_t> binding_identity_;
};

ActorSkinBinding decode_actor_skin_binding(const IgbDocument& document,
    const ActionActorGroup& actors,std::size_t actor_index,std::uint32_t geometry_reference);

// Copy the actor's selected existing arrays. This never samples, traverses,
// invalidates caches, transforms vertices or fabricates missing matrices.
ActorSkinFrame capture_actor_skin(const ActorSkinBinding& binding,
    const ActionActorGroup& actors,const AnimationCombiner& combiner);

// Original single-influence CPU deformation from this binding's captured
// palette. Root/model-view composition remains the separate draw operation.
SkinPose evaluate_actor_skin(const ActorSkinBinding& binding,const ActorSkinFrame& frame);

} // namespace dw4
