#pragma once

#include "dw4/hitland_intersection.hpp"
#include "dw4/hitland_query.hpp"
#include "dw4/collision_math.hpp"

namespace dw4 {

// Optional overrides and remaining unrecovered operations. Absent overrides
// use the native AABox scratch transform and original matrix inverse. Providers
// perform the named original operation and retain any unfinished nested call.
struct HitLandGeometryServices {
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const CollisionBox>&,std::shared_ptr<const CollisionBox>&)> transform_bound;
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&,const std::shared_ptr<const CollisionBox>&)> volume_test;
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&,const std::shared_ptr<const CollisionBox>&)> segment_bound_test;
    // 8012A92C writes the caller's inverse matrix; completion cannot substitute
    // a host camera inverse or an identity matrix for that operation.
    std::function<MessengerCallResult(const std::shared_ptr<AnimationMatrix>&,AnimationMatrix&)> inverse_matrix;
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&,HitLandVisitorPayload&)> candidate;
};

// 8012D498 and the world-distance tail at 801F63BC. These helpers retain the
// original scalar operation order, homogeneous-W branch and NI=1 arithmetic.
void original_hitland_point(std::array<float,3>& output,const std::array<float,3>& input,
    const AnimationMatrix& matrix);
float original_hitland_distance(const std::array<float,3>& origin,const std::array<float,3>& position);
// 801252D0: transforms both endpoints, then always renormalizes the resulting
// direction (unlike append_segment's short-segment threshold).
void original_hitland_transform_segment(HitLandSegment&,const AnimationMatrix&);

// Actual 801F6050 candidate producer. Captures the original matrix element,
// attribute/segment lists and counts, while reloading reached physical backing
// and current callback metadata. Suspension never replays a mapped triangle.
class HitLandGeometryInvocation {
public:
    HitLandGeometryInvocation(std::shared_ptr<HitLandIntersectionVisitor>,std::shared_ptr<const ActorSceneNode>);
    ~HitLandGeometryInvocation();
    HitLandGeometryInvocation(const HitLandGeometryInvocation&)=delete;
    HitLandGeometryInvocation& operator=(const HitLandGeometryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<HitLandGeometryServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
