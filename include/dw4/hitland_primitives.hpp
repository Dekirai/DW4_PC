#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace dw4 {
class ActorGeometryAttribute;
class HitLandSegment;
class ModelSceneObject;

// Actual 801F64E4 primitive enumeration. One call maps the reached live array
// owners, scans from the beginning and selects the first hit after the signed
// group/stream cursor. The stream cursor is an element offset, not a triangle
// ordinal. Position/normal and cursor publications survive a later failure.
// Outputs are caller-local storage, as in the original 801F6050 caller; they
// must not alias mapped vertex/index bytes. Mapping operations are synchronous.
bool original_hitland_primitive(const std::shared_ptr<ActorGeometryAttribute>&,
    const HitLandSegment&,std::array<float,3>& position,std::array<float,3>& normal,
    std::int32_t& group_cursor,std::int32_t& stream_cursor,std::int32_t side,
    std::uint8_t continue_search,const std::function<void()>& post_service = {});
// Actual producer overload: nullable/wrong-type segment owners are consumed
// only at the leaf call, after reached maps and cursor-reset publications.
bool original_hitland_primitive(const std::shared_ptr<ActorGeometryAttribute>&,
    const std::shared_ptr<ModelSceneObject>& segment,
    std::array<float,3>& position,std::array<float,3>& normal,
    std::int32_t& group_cursor,std::int32_t& stream_cursor,std::int32_t side,
    std::uint8_t continue_search,const std::function<void()>& post_service = {});
} // namespace dw4
