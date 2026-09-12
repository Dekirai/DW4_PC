#pragma once

#include "dw4/model_scene.hpp"

namespace dw4 {
class HitLandQueryError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct HitLandTraversalScratch;
struct HitLandNodePath;

// Concrete igLineSegment (0x24 bytes in the original): the second endpoint is
// represented by a direction and length, not retained as a detached endpoint.
class HitLandSegment final : public ModelSceneObject {
public:
    HitLandSegment():ModelSceneObject("igLineSegment") {}
    std::array<float,3> origin{},direction{0,1,0}; // +08/+14.
    float length{}; // +20.
};

// The visitor's actual 0x48-byte candidate argument. Pointer fields retain their
// original owners/physical aliases; matrix may alias a matrix-stack allocation.
struct HitLandVisitorPayload {
    std::uint32_t kind{}; // +00.
    std::shared_ptr<HitLandTraversalScratch> scratch; // +04.
    std::shared_ptr<HitLandNodePath> node_path; // +08.
    std::shared_ptr<AnimationMatrix> matrix; // +0C.
    std::shared_ptr<ModelSceneObject> segment; // +10.
    float distance{}; // +14.
    std::array<float,3> position{},local_position{},normal{}; // +18/+24/+30.
    // Last field is the primitive stream's element offset, not a triangle ordinal.
    std::int32_t attribute_index{},group_index{},triangle_index{}; // +3C/+40/+44.
};
class HitLandCandidate final : public ModelSceneObject {
public:
    HitLandCandidate():ModelSceneObject("beHitLandResultData") {name=std::string{};}
    // Inherited name is the original +08; the copied payload starts at +10.
    std::int32_t child_index{}; // +0C.
    HitLandVisitorPayload payload;
};

class HitLandQuery final : public ModelSceneObject {
public:
    HitLandQuery();
    std::array<float,3> maximum{},minimum{}; // +08/+14.
    std::optional<std::string> current_name=std::string{}; // +20, metadata's nonnull empty default.
    std::int32_t current_index{}; // +24.
    std::uint32_t flags{}; // +28.
    std::int32_t segment_count{},candidate_count{}; // +2C/+34, independent of list counts.
    std::shared_ptr<ModelSceneList> segments,candidates; // +30/+38 reusable allocations.

    void reset() noexcept; // 803011FC: only flags and the two query counts.
    std::shared_ptr<HitLandSegment> append_segment(const std::array<float,3>& start,
        const std::array<float,3>& end); // Virtual +5C, 8030111C.
    std::shared_ptr<HitLandCandidate> append_candidate(HitLandVisitorPayload&);
        // Virtual +60, 80300E90: transforms the input normal in place first.
};

// Reached physical operations shared by query storage and traversal scratch.
// Physical reads ignore both logical count and capacity, retain null entries,
// and reject uninitialized spare slots produced by native growth.
std::shared_ptr<ModelSceneObject> hitland_physical_object(
    const std::shared_ptr<ModelSceneList>&,std::int32_t index);
void hitland_append_object(const std::shared_ptr<ModelSceneList>&,std::shared_ptr<ModelSceneObject>);
void hitland_clear_objects(const std::shared_ptr<ModelSceneList>&);
} // namespace dw4
