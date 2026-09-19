#pragma once

#include "dw4/hitland_traversal.hpp"

namespace dw4 {
struct HitLandFloorSelection {
    bool found{},in_range{},adjusted{};
    std::int32_t index{};
    float height{-1.f};
};

// 80300694..80300854: three ordered passes over actual query candidates.
// Passes compare rounded differences and retain their original tie rules.
// NI=1/RN=00 finite arithmetic; invalid reached storage/arithmetic faults.
HitLandFloorSelection original_hitland_floor_selection(const HitLandQuery&,
    float position_y,float up_range,float down_range);

// CHECKLAND leaf 80300444, called by the original HITLANDMODEL dispatcher.
// Captures input coordinates/ranges and the actual query, retaining traversal
// across suspension. Results publish to the messenger's current packet.
class HitLandFloorInvocation {
public:
    explicit HitLandFloorInvocation(std::shared_ptr<HitLandTraversalManager>);
    ~HitLandFloorInvocation();
    HitLandFloorInvocation(const HitLandFloorInvocation&)=delete;
    HitLandFloorInvocation& operator=(const HitLandFloorInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,std::shared_ptr<HitLandIntersectionServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A resolved native CHECKLAND receiver for one messenger delivery. Construct
// through the manager resolver so each delivery retains its own invocation.
// Registration/manager startup remain the caller's responsibility. Unrecovered
// commands suspend explicitly; this does not manufacture wall responses.
MessengerManager make_hitland_floor_receiver(std::shared_ptr<HitLandTraversalManager>,
    std::shared_ptr<HitLandIntersectionServices> = {});
} // namespace dw4
