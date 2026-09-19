#pragma once

#include "dw4/hitland_traversal.hpp"

namespace dw4 {
// Original CHECKWALL leaf 802FE7F0. Retains the original request, three query
// passes and already published metadata across actual traversal suspension.
// The enclosing receiver owns the manager's wall-enabled dispatch gate.
class HitLandWallInvocation {
public:
    explicit HitLandWallInvocation(std::shared_ptr<HitLandTraversalManager>);
    ~HitLandWallInvocation();
    HitLandWallInvocation(const HitLandWallInvocation&)=delete;
    HitLandWallInvocation& operator=(const HitLandWallInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,std::shared_ptr<HitLandIntersectionServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Resolve a fresh receiver per messenger delivery. Implements CHECKLAND,
// CHECKWALL and the original disabled-wall gate; enabled CHECKWALLFULL remains
// a distinct pending operation. Original manager registration is still required.
MessengerManager make_hitland_query_receiver(std::shared_ptr<HitLandTraversalManager>,
    std::shared_ptr<HitLandIntersectionServices> = {});
} // namespace dw4
