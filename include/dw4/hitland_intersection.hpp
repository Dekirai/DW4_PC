#pragma once

#include "dw4/animation_math.hpp"
#include "dw4/messenger.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace dw4 {
class ActorSceneNode;
class HitLandQuery;
class HitLandSegment;
class ModelSceneObject;
struct CollisionBox;
struct HitLandTraversalScratch;
struct HitLandDispatchTable;
struct HitLandNodeDispatchServices;
struct HitLandIntersectionVisitor;
std::shared_ptr<HitLandDispatchTable> default_hitland_dispatch_table();

// Actual matrix values are inline in a retained physical stack allocation.
// Unwritten spare elements remain unspecified, independently of logical count.
struct HitLandMatrixList {
    std::int32_t count{},capacity{};
    std::shared_ptr<std::vector<std::optional<AnimationMatrix>>> backing;
};
struct HitLandNodePath {
    std::int32_t count{},capacity{};
    std::shared_ptr<std::vector<std::optional<std::shared_ptr<const ActorSceneNode>>>> backing;
};
struct HitLandIntersectionVisitor {
    HitLandIntersectionVisitor();
    std::uint8_t filter_enabled{}; // +08, independent from the filter target.
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&)> filter; // +0C.
    std::uint32_t time_high{},time_low{}; // +10/+14.
    std::uint8_t bound_time{}; // +18.
    std::shared_ptr<HitLandDispatchTable> dispatch_table=default_hitland_dispatch_table(); // +1C.
    std::shared_ptr<HitLandMatrixList> matrix_stack=std::make_shared<HitLandMatrixList>(); // +20.
    std::shared_ptr<HitLandNodePath> node_path=std::make_shared<HitLandNodePath>(); // +24.
    std::shared_ptr<HitLandTraversalScratch> scratch; // +28, retained.
    std::uint32_t flags{}; // +2C.
    bool query_candidate_callback{}; // +30, known 802FE1A4 adapter only.
    // AABox entry of the original +34 per-type scratch table. Canonical scene
    // bounds currently represent igAABox only; other bound classes need owners.
    std::shared_ptr<CollisionBox> transformed_box;
    std::shared_ptr<HitLandSegment> transformed_segment; // +38, retained transformed ray scratch.
    std::shared_ptr<HitLandQuery> query; // +40, retained.
};

// 801F5F0C: clear matrix count, append identity, then release/clear the captured
// native node path and publish its zero count last. Completed prefixes survive
// a later missing list/backing slot. Canonical native node owners only; arbitrary
// guest destructor callbacks during the two-phase release are outside this profile.
void reset_hitland_intersection(const std::shared_ptr<HitLandIntersectionVisitor>&);
std::shared_ptr<AnimationMatrix> hitland_physical_matrix(const std::shared_ptr<HitLandMatrixList>&,std::int32_t);

struct HitLandIntersectionServices {
    // Actual 8020B06C dispatch, including its callback/type-table gates and
    // concrete node handler. A pending provider retains its internal operation;
    // this is not a flattened mesh or synthetic candidate-list callback.
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&)> node_dispatch;
    std::shared_ptr<HitLandNodeDispatchServices> native_dispatch;
};

// Concrete beHitLandModelTraversal +64 = 8020B13C. The recovered inherited
// +5C/+60 entry/exit are no-ops; the real middle dispatch stays retained.
class HitLandIntersectionInvocation {
public:
    HitLandIntersectionInvocation(std::shared_ptr<HitLandIntersectionVisitor>,std::shared_ptr<const ActorSceneNode>);
    ~HitLandIntersectionInvocation();
    HitLandIntersectionInvocation(const HitLandIntersectionInvocation&)=delete;
    HitLandIntersectionInvocation& operator=(const HitLandIntersectionInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<HitLandIntersectionServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Complete 801238E8 leaf, with original early normal/position publications.
// The return is distance on success or the original negative rejection code.
// Finite used arithmetic is a checked profile; degenerate/nonfinite normals
// reject explicitly after the raw cross-product normal has been published.
float original_hitland_triangle(const HitLandSegment&,const std::array<float,3>& a,
    const std::array<float,3>& b,const std::array<float,3>& c,
    std::array<float,3>& position,std::array<float,3>& normal,std::int32_t side);
// Actual nullable pointer profile: missing/wrong-type segment fails only at
// the first segment read, after the normal publication at 80123A28.
float original_hitland_triangle(const std::shared_ptr<ModelSceneObject>&,
    const std::array<float,3>& a,const std::array<float,3>& b,const std::array<float,3>& c,
    std::array<float,3>& position,std::array<float,3>& normal,std::int32_t side);
} // namespace dw4
