#include "dw4/hitland_primitives.hpp"

#include "dw4/actor_geometry.hpp"
#include "dw4/hitland_intersection.hpp"
#include "dw4/hitland_query.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw HitLandQueryError(message);}
template<class T> const std::shared_ptr<T>& required(const std::shared_ptr<T>& owner,const char* message) {
    if(!owner)fail(message);return owner;
}
std::int32_t signed_word(std::uint32_t value) {return std::bit_cast<std::int32_t>(value);}

template<class Segment> bool enumerate(std::shared_ptr<ActorGeometryAttribute> attribute,
    const Segment& segment,std::array<float,3>& position,std::array<float,3>& normal,
    std::int32_t& group_cursor,std::int32_t& stream_cursor,std::int32_t side,std::uint8_t continuation,
    const std::function<void()>& post_service) {
    const auto checkpoint=[&] {if(post_service)post_service();};
    if(!attribute)return false;
    if(!attribute->is_primary()) {
        if(!attribute->is_alternate())return false;
        const bool rejected=attribute->prepare_alternate();checkpoint();
        if(rejected)return false;
        fail("Original HITLAND igGeometryAttr2 reaches a null primary attribute virtual call at 801F6A04.");
    }
    const auto format=required(attribute->vertices,"HITLAND primitive format reads a null vertex array.")->get_format();
    checkpoint();
    if(!format)fail("HITLAND primitive format getter returned a null format pointer.");
    if(format->word&0xff0u)return false;
    const auto vertex_base=attribute->vertex_base;
    const auto primitive_count=attribute->primitive_count;
    const auto primitive_mode=attribute->primitive_mode;
    ActorVertexMapping mapping;
    required(attribute->vertices,"HITLAND primitive map reads a null vertex array.")->map(mapping,0);
    checkpoint();
    const auto vertex_pointer=mapping.position;
    const auto stride=mapping.stride;
    ActorGeometryPointer index_pointer;
    std::shared_ptr<ActorIndexArray> index_owner;
    if(const auto index=attribute->index) {
        index_pointer=index->map(0,0,-1);
        checkpoint();
        index_owner=attribute->index; // Original 801F66A0 reload follows map.
    }
    const auto groups=attribute->groups();
    checkpoint();
    if(!continuation || group_cursor==-1 || stream_cursor==-1) {
        group_cursor=-1;stream_cursor=-1;
    }
    bool found=false;
    std::uint32_t step{};
    if(primitive_mode==3)step=3;
    else if(primitive_mode==4)step=1;
    if(step) {
        std::uint32_t stream{},attempts{};
        for(std::uint32_t group=0;group<primitive_count && !found;++group) {
            std::uint32_t remaining{};
            if(groups) {const auto length=groups->element(group);checkpoint();remaining=length-2u;}
            do {
                if(++attempts>1048576u)fail("HITLAND primitive enumeration exceeds its checked operation bound.");
                const bool reversed=primitive_mode==4 && side!=2 && (stream&1u);
                const auto first=index_pointer ? index_pointer.read_u16(stream*2u):stream;
                const auto second_offset=stream+(reversed ? 2u:1u);
                const auto second=index_pointer ? index_pointer.read_u16(second_offset*2u):second_offset;
                const auto a_offset=(vertex_base+first)*stride;
                const auto b_offset=(vertex_base+second)*stride;
                const auto third_offset=stream+(reversed ? 1u:2u);
                const auto third=index_pointer ? index_pointer.read_u16(third_offset*2u):third_offset;
                const auto c_offset=(vertex_base+third)*stride;
                // Caller-local outputs cannot alias mapped bytes. The original
                // leaf reads all three vertices before its first output store.
                const auto a=vertex_pointer.read_vec3(a_offset);
                const auto b=vertex_pointer.read_vec3(b_offset);
                const auto c=vertex_pointer.read_vec3(c_offset);
                const auto distance=original_hitland_triangle(segment,a,b,c,position,normal,side);
                if(distance>=0 && (signed_word(group)>group_cursor ||
                    (signed_word(group)==group_cursor && signed_word(stream)>stream_cursor))) {
                    group_cursor=signed_word(group);
                    found=true;
                    stream_cursor=signed_word(stream);
                    break;
                }
                remaining-=step;stream+=step;
            }while(signed_word(remaining)>0);
            if(!found && groups)stream+=2u;
        }
    }
    required(attribute->vertices,"HITLAND primitive unmap reads the current null vertex array.")->unmap(mapping,1);
    checkpoint();
    if(index_pointer) {
        required(index_owner,"HITLAND primitive index unmap reads its retained null index owner.")->unmap(index_pointer,1,0,-1);
        checkpoint();
    }
    return found;
}
} // namespace

bool original_hitland_primitive(const std::shared_ptr<ActorGeometryAttribute>& attribute,
    const HitLandSegment& segment,std::array<float,3>& position,std::array<float,3>& normal,
    std::int32_t& group_cursor,std::int32_t& stream_cursor,std::int32_t side,std::uint8_t continuation,
    const std::function<void()>& post_service) {
    return enumerate(attribute,segment,position,normal,group_cursor,stream_cursor,side,continuation,post_service);
}
bool original_hitland_primitive(const std::shared_ptr<ActorGeometryAttribute>& attribute,
    const std::shared_ptr<ModelSceneObject>& segment,std::array<float,3>& position,std::array<float,3>& normal,
    std::int32_t& group_cursor,std::int32_t& stream_cursor,std::int32_t side,std::uint8_t continuation,
    const std::function<void()>& post_service) {
    const auto captured=segment;
    return enumerate(attribute,captured,position,normal,group_cursor,stream_cursor,side,continuation,post_service);
}
} // namespace dw4
