#pragma once

#include "mesh_renderer.hpp"
#include "dw4/actor_scene.hpp"
#include "dw4/player_runtime_binding.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dw4::viewer {
struct WeaponPreviewSelection {
    std::uint32_t model_reference{},action_reference{},ram_index{},actor_index{},geometry_reference{};
};
struct WeaponPreviewFrame {
    SkinPose body_pose;
    std::vector<AnimationMatrix> model_views;
    std::uint64_t time{};
    std::uint32_t appearance_reference{};
    std::size_t attachment_count{};
};

// Explicit ACTION/weapon inspection on one live prepared player owner. This
// does not execute the full player constructor, sequence loop or game camera.
// Construction dispatches the named ACTION at time zero, then real GENERATE.
// All missing runtime/traversal/material services stop with a diagnostic.
class WeaponPreview {
public:
    WeaponPreview(std::shared_ptr<ActorSceneResource> body,std::shared_ptr<ActorSceneResource> weapon,
        WeaponPreviewSelection selection,std::string weapon_name,std::string action_name,
        AnimationMatrix placement);
    ~WeaponPreview();
    WeaponPreview(WeaponPreview&&) noexcept;
    WeaponPreview& operator=(WeaponPreview&&) noexcept;
    WeaponPreview(const WeaponPreview&)=delete;
    WeaponPreview& operator=(const WeaponPreview&)=delete;
    // Forward-only prepared time in the nonnegative signed 64-bit clock range.
    // Capture skin and attached children before another actor can replace the
    // shared combiner root. View changes at equal time do not resample clips.
    const WeaponPreviewFrame& frame(const AnimationMatrix& view,const AnimationMatrix& inverse_view,
        std::uint64_t time);
    void set_placement(const AnimationMatrix& placement);
    // Explicit host inspection selection through the existing ACTION owner.
    // This preserves source clocks/combiner history, without synthesizing the
    // original player input or locomotion state machine.
    void dispatch_action(std::string_view name,std::uint64_t time);
    // Stable mesh/image owners for one upload. Each new frame changes body
    // positions only; update GPU batch zero from frame.body_pose.positions.
    std::span<const MeshBatch> batches() const;
    const PlayerRuntimeBinding& binding() const;
    const ActionRuntime& action() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4::viewer
