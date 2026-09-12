#include "dw4/hitland_query.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <typeinfo>

namespace dw4 {
namespace {
constexpr std::size_t maximum_slots=262144;
constexpr float minimum_normal=std::numeric_limits<float>::min();
[[noreturn]] void fail(const char* reason) {throw HitLandQueryError(reason);}
struct UninitializedSlot final : ModelSceneObject {
    UninitializedSlot():ModelSceneObject("uninitialized hitland pointer slot") {}
};
bool uninitialized(const std::shared_ptr<ModelSceneObject>& object) {
    return object && dynamic_cast<const UninitializedSlot*>(object.get());
}
std::shared_ptr<ModelSceneObject>& slot(const std::shared_ptr<ModelSceneList>& list,std::int32_t index) {
    if(!list)fail("Original HITLAND operation dereferences a null object list.");
    if(index<0 || std::size_t(index)>=maximum_slots || !list->backing ||
        std::size_t(index)>=list->backing->size())
        fail("Original HITLAND pointer access exceeds its reached physical backing.");
    return (*list->backing)[std::size_t(index)];
}
std::int32_t increment(std::int32_t value) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)+1u);
}
void store(const std::shared_ptr<ModelSceneList>& list,std::int32_t index,
    std::shared_ptr<ModelSceneObject> object) {
    auto& destination=slot(list,index);
    if(destination && !uninitialized(destination))
        fail("Original HITLAND raw pointer overwrite would discard an unreleased spare owner.");
    destination=std::move(object);
}
void grow(const std::shared_ptr<ModelSceneList>& list,std::int32_t count) {
    // 80041660 always calls 8004155C, even if its selected capacity is unchanged.
    std::int32_t capacity=std::max<std::int32_t>(list->capacity,4);
    while(capacity<count) {
        if(std::size_t(capacity)>=maximum_slots)
            fail("Original HITLAND list growth exceeds its bounded allocation profile.");
        capacity=capacity<1024 ? capacity*2 : capacity+1024;
    }
    if(std::size_t(capacity)>maximum_slots)
        fail("Original HITLAND list allocation exceeds its bounded physical profile.");
    const auto previous=list->backing;
    auto replacement=std::make_shared<ModelSceneList::Storage>(std::size_t(capacity),
        std::make_shared<UninitializedSlot>());
    list->backing=replacement; // Original allocation publication precedes old-capacity copy.
    if(previous) {
        const auto copied=std::min(list->capacity,capacity);
        if(copied<0 || std::size_t(copied)>previous->size())
            fail("Original HITLAND capacity copy exceeds its retained old allocation.");
        std::copy_n(previous->begin(),std::size_t(copied),replacement->begin());
    }
    list->capacity=capacity;
    list->count=count;
}
float finite(float value) {
    if(!std::isfinite(value))fail("Original HITLAND arithmetic exceeds its finite scalar profile.");
    return value;
}
float flushed(float result,double exact) {
    finite(result);
    if(std::abs(result)<minimum_normal ||
        (std::abs(result)==minimum_normal && std::abs(exact)<minimum_normal))
        return std::copysign(0.0f,result);
    return result;
}
float difference(float a,float b) {
    finite(a);finite(b);return flushed(a-b,static_cast<double>(a)-b);
}
float product(float a,float b) {
    finite(a);finite(b);return flushed(a*b,static_cast<double>(a)*b);
}
float sum(float a,float b) {
    finite(a);finite(b);return flushed(a+b,static_cast<double>(a)+b);
}
float fused(float a,float b,float c) {
    finite(a);finite(b);finite(c);
    const float result=finite(std::fma(a,b,c));
    const auto magnitude=std::abs(result);
    if(magnitude<minimum_normal)return std::copysign(0.0f,result);
    if(magnitude!=minimum_normal)return result;
    const double multiplied=static_cast<double>(a)*b;
    const double added=multiplied+c;
    const double virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+(c-virtual_addend);
    if(std::abs(added)<minimum_normal || (std::abs(added)==minimum_normal &&
        ((added>0 && error<0) || (added<0 && error>0))))return std::copysign(0.0f,result);
    return result;
}
void set_segment(HitLandSegment& segment,const std::array<float,3>& start,
    const std::array<float,3>& end) {
    // 801251D0 interleaves scalar source loads and stores; endpoint aliases
    // into the current segment therefore must not become an eager snapshot.
    const float x=start[0],y=start[1];segment.origin[0]=x;
    const float end_x=end[0],z=start[2];const float dx=difference(end_x,x);
    segment.origin[1]=y;const float end_y=end[1];segment.origin[2]=z;
    const float end_z=end[2];const float dy=difference(end_y,y);
    segment.direction[0]=dx;const float dz=difference(end_z,z);
    segment.direction[1]=dy;segment.direction[2]=dz;
    const float xx=product(segment.direction[0],segment.direction[0]);
    const float yy=product(segment.direction[1],segment.direction[1]);
    const float zz=product(segment.direction[2],segment.direction[2]);
    float length=sum(zz,sum(xx,yy));
    if(length>0)length=std::sqrt(length);
    segment.length=length;
    // Exact original binary32 threshold; short/zero segments retain their
    // unnormalized displacement. The positive sqrt is independently probed.
    if(length>std::bit_cast<float>(std::uint32_t{0x350637bd})) {
        const float reciprocal=flushed(1.0f/length,1.0/static_cast<double>(length));
        segment.direction[0]=product(segment.direction[0],reciprocal);
        segment.direction[1]=product(segment.direction[1],reciprocal);
        segment.direction[2]=product(segment.direction[2],reciprocal);
    }
}
void transform_normal(HitLandVisitorPayload& payload) {
    const auto matrix=payload.matrix;
    if(!matrix)fail("Original HITLAND candidate normal dereferences a null matrix.");
    // 8012D434 uses Y products, then fused X and Z, without translation or
    // inverse transpose. All source loads precede the three in-place stores.
    const float y=payload.normal[1];
    const float yx=product(y,(*matrix)[4]);
    const float x=payload.normal[0];
    const float yy=product(y,(*matrix)[5]);
    float rx=fused(x,(*matrix)[0],yx);
    const float z=payload.normal[2];
    const float yz=product(y,(*matrix)[6]);
    float ry=fused(x,(*matrix)[1],yy);
    rx=fused(z,(*matrix)[8],rx);
    float rz=fused(x,(*matrix)[2],yz);
    ry=fused(z,(*matrix)[9],ry);
    payload.normal[0]=rx;
    rz=fused(z,(*matrix)[10],rz);
    payload.normal[1]=ry;payload.normal[2]=rz;
}
template<class T> std::shared_ptr<T> reached(const std::shared_ptr<ModelSceneList>& list,
    std::int32_t index,const char* reason) {
    auto object=hitland_physical_object(list,index);
    auto result=std::dynamic_pointer_cast<T>(object);
    if(!result)fail(reason);return result;
}
}

std::shared_ptr<ModelSceneObject> hitland_physical_object(
    const std::shared_ptr<ModelSceneList>& list,std::int32_t index) {
    const auto& object=slot(list,index);
    if(uninitialized(object))fail("Original HITLAND reads an uninitialized physical pointer slot.");
    return object;
}
void hitland_append_object(const std::shared_ptr<ModelSceneList>& list,std::shared_ptr<ModelSceneObject> object) {
    if(!list)fail("Original HITLAND append dereferences a null object list.");
    const auto index=list->count;
    const auto next=increment(index);
    if(index<list->capacity)list->count=next;
    else grow(list,next);
    store(list,index,std::move(object));
}
void hitland_clear_objects(const std::shared_ptr<ModelSceneList>& list) {
    if(!list)fail("Original HITLAND clear dereferences a null object list.");
    const auto count=list->count;
    if(count<0) {
        // The original growing-to-zero branch publishes zero count, then
        // attempts a memset beginning at the negative old-count address.
        list->count=0;
        fail("Original HITLAND clear reaches a negative physical memset address.");
    }
    for(std::int32_t i=0;i<count;++i) {
        auto& object=slot(list,i);
        if(uninitialized(object))fail("Original HITLAND release reads an uninitialized pointer slot.");
        // Foreign final-release destructors could observe the original raw
        // dangling pointers before its second zero pass. Do not invent that
        // behavior using shared_ptr slots. Concrete segments have no callbacks.
        if(object && object.use_count()==1 && typeid(*object)!=typeid(HitLandSegment))
            fail("Original HITLAND clear requires an unrepresented final-release destructor.");
        object.reset();
    }
    for(std::int32_t i=0;i<count;++i)slot(list,i).reset();
    list->count=0;
}

HitLandQuery::HitLandQuery():ModelSceneObject("beHitLandModelOrder"),
    segments(std::make_shared<ModelSceneList>("igLineSegmentList")),
    candidates(std::make_shared<ModelSceneList>("beHitLandResultDataList")) {}
void HitLandQuery::reset() noexcept {flags=0;segment_count=0;candidate_count=0;}
std::shared_ptr<HitLandSegment> HitLandQuery::append_segment(const std::array<float,3>& start,
    const std::array<float,3>& end) {
    if(!segments)fail("Original HITLAND segment append dereferences a null segment list.");
    if(segment_count>=segments->count)hitland_append_object(segments,std::make_shared<HitLandSegment>());
    auto segment=reached<HitLandSegment>(segments,segment_count,"Original HITLAND segment setter reaches a null or unsupported object.");
    set_segment(*segment,start,end);
    segment_count=increment(segment_count);
    return segment;
}
std::shared_ptr<HitLandCandidate> HitLandQuery::append_candidate(HitLandVisitorPayload& input) {
    if(!candidates)fail("Original HITLAND candidate append dereferences a null result list.");
    if(candidate_count>=candidates->count)hitland_append_object(candidates,std::make_shared<HitLandCandidate>());
    transform_normal(input);
    auto candidate=reached<HitLandCandidate>(candidates,candidate_count,
        "Original HITLAND payload copy reaches a null or unsupported result object.");
    candidate->payload=input;
    // Fresh result lookup/name capture occurs after the complete payload copy.
    auto named=reached<HitLandCandidate>(candidates,candidate_count,
        "Original HITLAND name copy reaches a null or unsupported result object.");
    const auto copied_name=current_name;
    if(copied_name && (copied_name->size()>65535 || copied_name->find('\0')!=std::string::npos))
        fail("Original HITLAND name copy exceeds its bounded C-string profile.");
    named->name=copied_name;
    auto indexed=reached<HitLandCandidate>(candidates,candidate_count,
        "Original HITLAND child-index copy reaches a null or unsupported result object.");
    indexed->child_index=current_index;
    candidate_count=increment(candidate_count);
    return candidate;
}
} // namespace dw4
