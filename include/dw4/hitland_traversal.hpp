#pragma once

#include "dw4/hitland_query.hpp"
#include "dw4/messenger.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace dw4 {
struct HitLandIntersectionVisitor;
struct HitLandIntersectionServices;

// igIntList's logical size, capacity and physical allocation are separate.
// Fresh spare words have no invented value; reading one requires a later store.
struct HitLandIndexList {
    std::int32_t count{},capacity{};
    std::shared_ptr<std::vector<std::optional<std::int32_t>>> backing;
};
struct HitLandTraversalScratch {
    std::shared_ptr<ModelSceneList> segments; // Original scratch +0C.
    std::shared_ptr<HitLandIndexList> indices; // Original scratch +10.
};
struct HitLandTraversalManager {
    std::shared_ptr<ModelSceneList> infos; // +18, ordered actual info occurrences.
    std::shared_ptr<HitLandQuery> query; // +20; a caller may retain an older query.
    std::shared_ptr<HitLandIntersectionVisitor> visitor; // +24.
    std::shared_ptr<HitLandTraversalScratch> scratch; // +28.
};

// The exact clear/append sites in 803009D0, including count-before-store and
// capacity-copy growth. These helpers do not validate unrelated spare words.
void hitland_clear_indices(const std::shared_ptr<HitLandIndexList>&);
void hitland_append_zero_index(const std::shared_ptr<HitLandIndexList>&);
std::int32_t hitland_physical_index(const std::shared_ptr<HitLandIndexList>&,std::int32_t);

// Actual 803009D0 invocation. It retains its passed query independently of the
// manager's current query and pauses only at a reached real visitor operation.
class HitLandTraversalInvocation {
public:
    HitLandTraversalInvocation(std::shared_ptr<HitLandTraversalManager>,std::shared_ptr<HitLandQuery>);
    ~HitLandTraversalInvocation();
    HitLandTraversalInvocation(const HitLandTraversalInvocation&)=delete;
    HitLandTraversalInvocation& operator=(const HitLandTraversalInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<HitLandIntersectionServices> = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
