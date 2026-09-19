#pragma once

#include "dw4/hitland_intersection.hpp"
#include "dw4/collision_math.hpp"

namespace dw4 {
struct HitLandGeometryServices;
struct ActorTransformUpdateServices;
class NativeGameClock;

// Original table entries are concrete function targets, not serialized type IDs.
enum class HitLandNodeHandler : std::uint32_t {
    empty=0x801f6dd8,group=0x801f6de0,transform=0x801f6ed4,geometry=0x801f707c
};
struct HitLandDispatchTable {
    std::int32_t count{},capacity{};
    std::shared_ptr<std::vector<std::optional<std::uint32_t>>> backing;
};
// Explicit runtime class registration before traversal. Native class indices
// are shared across visitors and never taken from IGB metadata type indices.
void register_hitland_node_type(const std::shared_ptr<HitLandIntersectionVisitor>&,
    const std::shared_ptr<const ActorSceneNode>&);

struct HitLandNodeDispatchServices {
    std::shared_ptr<HitLandGeometryServices> geometry;
    // Reached original global clock virtual+68 when visitor.bound_time is zero.
    std::function<std::optional<std::uint64_t>()> global_clock;
    // Explicit shared runtime clock. The callback above takes precedence at
    // entry; a selected owner survives service replacement while suspended.
    std::shared_ptr<NativeGameClock> game_clock;
    // Override for the original node virtual+9C, selected before native entry.
    // Without an override the recovered igTransform/beCopyTransform body runs.
    std::function<MessengerCallResult(const std::shared_ptr<const ActorSceneNode>&,
        std::uint64_t)> transform_update;
    std::shared_ptr<ActorTransformUpdateServices> actor_transform;
    // The complete missing-bound prefix801F7148..73C4 must publish any new
    // canonical node bound and return its captured owner. Null is a completed
    // no-eligible-attribute result only when the provider established that.
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&,std::shared_ptr<const CollisionBox>&)> geometry_bound;
    // A reached table target outside the four recovered concrete handlers.
    std::function<MessengerCallResult(std::uint32_t,
        const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&)> other_handler;
};

void hitland_push_path(const std::shared_ptr<HitLandNodePath>&,std::shared_ptr<const ActorSceneNode>);
void hitland_pop_path(const std::shared_ptr<HitLandNodePath>&);
// 8021B044 appends a PSQ copy, then80128774 composes prior*node using Y-first
// scalar arithmetic; the physical appended matrix survives a later failure.
void hitland_push_transform(const std::shared_ptr<HitLandMatrixList>&,const AnimationMatrix&);
void hitland_pop_transform(const std::shared_ptr<HitLandMatrixList>&);

// Retained8020B06C, its generic child traversal8020B274, and concrete node
// handlers. Pending operations preserve their current path and stack prefix.
class HitLandNodeDispatchInvocation {
public:
    HitLandNodeDispatchInvocation(std::shared_ptr<HitLandIntersectionVisitor>,std::shared_ptr<const ActorSceneNode>);
    ~HitLandNodeDispatchInvocation();
    HitLandNodeDispatchInvocation(const HitLandNodeDispatchInvocation&)=delete;
    HitLandNodeDispatchInvocation& operator=(const HitLandNodeDispatchInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<HitLandNodeDispatchServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
