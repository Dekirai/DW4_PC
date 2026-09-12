#include "dw4/actor_geometry.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) { throw ActorSceneError(reason); }
std::uint32_t scalar(const ActorGeometryPointer& pointer,std::uint32_t relative,std::uint32_t width) {
    // Original add/mullw operates in the guest's 32-bit address domain. Buffer
    // identity is retained separately; a wrapped offset is checked when read.
    const auto at=pointer.byte_offset+relative;
    if(!pointer.buffer || at>pointer.buffer->bytes.size() || pointer.buffer->bytes.size()-at<width)
        fail("Geometry mapping requires its reached physical bytes.");
    std::uint32_t result{};
    for(std::uint32_t i=0;i<width;++i)result|=std::uint32_t(pointer.buffer->bytes[at+i])<<(8*i);
    return result;
}
}
std::uint16_t ActorGeometryPointer::read_u16(std::uint32_t offset) const {
    return static_cast<std::uint16_t>(scalar(*this,offset,2));
}
std::uint32_t ActorGeometryPointer::read_u32(std::uint32_t offset) const { return scalar(*this,offset,4); }
float ActorGeometryPointer::read_float(std::uint32_t offset) const { return std::bit_cast<float>(read_u32(offset)); }
std::array<float,3> ActorGeometryPointer::read_vec3(std::uint32_t offset) const {
    return {read_float(offset),read_float(offset+4),read_float(offset+8)};
}
std::shared_ptr<ActorVertexFormat> ActorVertexArray::get_format() {
    if(format_service)return format_service(*this);
    return format; // Concrete 800E326C returns the current inline format word.
}
void ActorVertexArray::map(ActorVertexMapping& mapping,std::int32_t mode) {
    if(map_service) {map_service(*this,mapping,mode);return;}
    // Concrete GameCube 801008C0. The actual collision caller supplies zeroed
    // input fields. Other semantic maps remain separate reached services.
    if(!mapping.length)mapping.length=count-mapping.first;
    if(mapping.selector!=0)fail("Vertex mapping requires its native non-position semantic service.");
    if(!streams || streams->slots.empty())fail("Position mapping requires its reached stream-table slot zero.");
    mapping.position={streams->slots[0],mapping.first*12u};
    mapping.stride=12;
    mapping.state=0;
    if(flags&4u) {
        if(!synchronize_service)fail("Vertex mapping requires its reached native GPU synchronization service.");
        synchronize_service(*this);
    }
}
void ActorVertexArray::unmap(ActorVertexMapping& mapping,std::int32_t mode) {
    if(unmap_service) {unmap_service(*this,mapping,mode);return;}
    unmap_state=1; // 80100C1C; neither mode nor mapping is read.
}
ActorGeometryPointer ActorIndexArray::map(std::int32_t mode,std::uint32_t first,std::int32_t length) {
    if(map_service)return map_service(*this,mode,first,length);
    return {backing,first*2u}; // 800DFFE8 ignores format, count and length.
}
void ActorIndexArray::unmap(const ActorGeometryPointer& pointer,std::int32_t mode,std::uint32_t first,std::int32_t length) {
    if(unmap_service)unmap_service(*this,pointer,mode,first,length);
    // The concrete 800E0008 body is blr, with no reads or publications.
}
std::uint32_t ActorPrimitiveLengths::element(std::uint32_t index) {
    if(element_service)return element_service(*this,index);
    return ActorGeometryPointer{backing,index*4u}.read_u32(); // 800EADB4 ignores count/element_bits.
}
std::shared_ptr<ActorPrimitiveLengths> ActorGeometryAttribute::groups() {
    if(groups_service)return groups_service(*this);
    return primitive_lengths; // Concrete 800C692C, no allocation or list validation.
}
bool ActorGeometryAttribute::prepare_alternate() {
    if(!alternate_prefix_service)fail("Alternate geometry requires its reached native semantic and mapping preparation service.");
    return alternate_prefix_service(*this);
}
} // namespace dw4
