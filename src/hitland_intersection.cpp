#include "dw4/hitland_intersection.hpp"

#include "dw4/actor_scene.hpp"
#include "dw4/hitland_query.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr std::int32_t element_limit=65536;
[[noreturn]] void fail(const char* text) {throw HitLandQueryError(text);}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
void append_identity(const std::shared_ptr<HitLandMatrixList>& list) {
    const auto index=list->count;
    if(index<0 || index>=element_limit)fail("HITLAND matrix stack exceeds its checked count profile.");
    if(index<list->capacity)list->count=index+1;
    else {
        auto capacity=std::max<std::int32_t>(4,list->capacity);
        while(capacity<index+1)capacity=capacity<1024 ? capacity*2:capacity+1024;
        if(capacity>element_limit)fail("HITLAND matrix stack growth exceeds its native bound.");
        const auto previous=list->backing;
        auto replacement=std::make_shared<std::vector<std::optional<AnimationMatrix>>>(static_cast<std::size_t>(capacity));
        list->backing=replacement; // 8004155C publishes allocation before old-capacity copy.
        if(previous) {
            const auto copied=std::min(list->capacity,capacity);
            if(copied<0 || static_cast<std::size_t>(copied)>previous->size())
                fail("HITLAND matrix stack growth reads unavailable old capacity backing.");
            std::copy_n(previous->begin(),copied,replacement->begin());
        }
        list->capacity=capacity;list->count=index+1;
    }
    if(!list->backing || static_cast<std::size_t>(index)>=list->backing->size())
        fail("HITLAND matrix stack identity store has no physical slot.");
    (*list->backing)[static_cast<std::size_t>(index)]=identity();
}
auto& path_slot(const std::shared_ptr<HitLandNodePath>& path,std::int32_t index) {
    if(!path || !path->backing || index<0 || static_cast<std::size_t>(index)>=path->backing->size())
        fail("HITLAND node path reads unavailable physical backing.");
    auto& slot=(*path->backing)[static_cast<std::size_t>(index)];
    if(!slot)fail("HITLAND node path reads an uninitialized physical pointer.");
    return *slot;
}
constexpr float minimum=std::numeric_limits<float>::min();
float flush(float value) {return std::abs(value)<minimum ? std::copysign(0.f,value):value;}
float difference(float a,float b) {return flush(a-b);}
float sum(float a,float b) {return flush(a+b);}
float product(float a,float b) {
    const float result=a*b;
    if(std::abs(result)<minimum || (std::abs(result)==minimum &&
        std::abs(static_cast<double>(a)*b)<minimum))return std::copysign(0.f,result);
    return result;
}
float fused(float a,float b,float c) {
    const float result=std::fma(a,b,c);const auto magnitude=std::abs(result);
    if(magnitude<minimum)return std::copysign(0.f,result);
    if(magnitude!=minimum)return result;
    const double multiplied=static_cast<double>(a)*b,added=multiplied+c,virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+(static_cast<double>(c)-virtual_addend);
    if(std::abs(added)<minimum || (std::abs(added)==minimum && ((added>0 && error<0) || (added<0 && error>0))))
        return std::copysign(0.f,result);
    return result;
}
float divide(float a,float b) {
    const float result=a/b;
    if(std::abs(result)<minimum || (std::abs(result)==minimum &&
        std::abs(static_cast<double>(a))<std::abs(static_cast<double>(b))*minimum))return std::copysign(0.f,result);
    return result;
}
using Vector=std::array<float,3>;
Vector subtract(const Vector& a,const Vector& b) {
    return {difference(a[0],b[0]),difference(a[1],b[1]),difference(a[2],b[2])};
}
Vector cross(const Vector& a,const Vector& b) {
    return {fused(a[1],b[2],-product(a[2],b[1])),fused(a[2],b[0],-product(a[0],b[2])),
        fused(a[0],b[1],-product(a[1],b[0]))};
}
float dot(const Vector& a,const Vector& b) {return fused(a[2],b[2],fused(a[0],b[0],product(a[1],b[1])));}
void finite(float value) {if(!std::isfinite(value))fail("Original HITLAND triangle exceeds its finite arithmetic profile.");}
}

void reset_hitland_intersection(const std::shared_ptr<HitLandIntersectionVisitor>& visitor) {
    if(!visitor)fail("Original HITLAND reset dereferences a null visitor.");
    const auto initial=visitor->matrix_stack;
    if(!initial)fail("Original HITLAND reset dereferences a null matrix stack.");
    initial->count=0;
    append_identity(visitor->matrix_stack);
    const auto path=visitor->node_path;
    if(!path)fail("Original HITLAND reset dereferences a null node path after its identity prefix.");
    const auto captured=path->count;
    if(captured>element_limit)fail("Original HITLAND path release exceeds its native bound.");
    // Canonical ActorSceneNode destruction has no arbitrary guest callback.
    // This bounded profile can release and zero together without exposing the
    // original transient dangling pointer values between its two loops.
    for(std::int32_t index=0;index<captured;++index)path_slot(path,index).reset();
    const auto count=path->count;
    if(count>element_limit)fail("Original HITLAND path zeroing exceeds its native bound.");
    for(std::int32_t index=0;index<count;++index)path_slot(path,index).reset();
    path->count=0;
}
std::shared_ptr<AnimationMatrix> hitland_physical_matrix(const std::shared_ptr<HitLandMatrixList>& list,std::int32_t index) {
    if(!list || !list->backing || index<0 || static_cast<std::size_t>(index)>=list->backing->size())
        fail("HITLAND intersection reads an unavailable physical matrix.");
    auto& value=(*list->backing)[static_cast<std::size_t>(index)];
    if(!value)fail("HITLAND intersection reads an uninitialized physical matrix.");
    return {list->backing,&*value};
}

struct HitLandIntersectionInvocation::Impl {
    std::shared_ptr<HitLandIntersectionVisitor> visitor;
    std::shared_ptr<const ActorSceneNode> node;
    std::shared_ptr<HitLandIntersectionServices> services;
    bool executing{},fault{},done{};
    std::uint32_t result{};
};
HitLandIntersectionInvocation::HitLandIntersectionInvocation(std::shared_ptr<HitLandIntersectionVisitor> visitor,
    std::shared_ptr<const ActorSceneNode> node):impl_(std::make_unique<Impl>()) {
    impl_->visitor=std::move(visitor);impl_->node=std::move(node);
}
HitLandIntersectionInvocation::~HitLandIntersectionInvocation()=default;
bool HitLandIntersectionInvocation::completed() const noexcept {return impl_->done;}
bool HitLandIntersectionInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult HitLandIntersectionInvocation::resume(std::shared_ptr<HitLandIntersectionServices> services) {
    auto& state=*impl_;
    if(state.fault || state.executing) {state.fault=true;fail("HITLAND intersection requires an unfaulted nonrecursive invocation.");}
    if(state.done)return {true,state.result,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(state.executing);
    try {
        if(!state.visitor)fail("Original intersection +64 dereferences a null visitor.");
        if(state.services && services && state.services!=services)fail("Retained HITLAND intersection requires its original service owner.");
        if(!state.services && services && services->node_dispatch)state.services=std::move(services);
        // +5C ->801F6010->8020B134 and +60 ->801F6030->8020B138 are
        // verified no-ops. 8020B06C owns the reached callback/type/node reads.
        if(!state.services || !state.services->node_dispatch)
            return {false,0,"Original HITLAND intersection requires its actual node dispatch and geometry visitor."};
        const auto result=state.services->node_dispatch(state.visitor,state.node);
        if(state.fault)fail("A caught recursive failure invalidated HITLAND intersection.");
        if(!result.completed)return result;
        state.result=result.value;state.done=true;return result;
    }catch(...){state.fault=true;throw;}
}

float original_hitland_triangle(const HitLandSegment& segment,const Vector& a,const Vector& b,const Vector& c,
    Vector& position,Vector& normal,std::int32_t side) {
    const auto ab=subtract(b,a),ac=subtract(c,a);
    normal=cross(ab,ac); // 80123980 publishes even before normal-length failure.
    const float squared=sum(product(normal[2],normal[2]),sum(product(normal[0],normal[0]),product(normal[1],normal[1])));
    if(!(squared>0) || !std::isfinite(squared))fail("Original HITLAND triangle has no supported finite nonzero normal.");
    const float reciprocal=divide(1.f,std::sqrt(squared));
    normal[0]=product(normal[0],reciprocal);normal[1]=product(normal[1],reciprocal);normal[2]=product(normal[2],reciprocal);
    const float facing=dot(normal,segment.direction);finite(facing);
    if(side!=2) {
        if((!std::signbit(facing) && side==0) || (std::signbit(facing) && side==1))return -2.f;
        constexpr float epsilon=0x1.0c6f7ap-21f; // DOL 805669E4: 0x350637BD.
        if(facing>=-epsilon && facing<=epsilon)return -2.f;
    }
    const auto captured_a=a;
    const float distance=divide(-dot(normal,subtract(segment.origin,captured_a)),facing);finite(distance);
    if(std::signbit(distance))return -3.f;
    finite(segment.length);if(distance>segment.length)return -3.f;
    const auto captured_b=b,captured_c=c;
    position[0]=fused(distance,segment.direction[0],segment.origin[0]);
    position[1]=fused(distance,segment.direction[1],segment.origin[1]);
    position[2]=fused(distance,segment.direction[2],segment.origin[2]);
    for(const auto value:position)finite(value);
    const auto pa=subtract(captured_a,position),pb=subtract(captured_b,position),pc=subtract(captured_c,position);
    const auto first=cross(pa,pb),second=cross(pb,pc),third=cross(pc,pa);
    const float x=dot(second,normal),y=dot(first,normal),z=dot(third,normal);
    finite(x);finite(y);finite(z);
    return std::signbit(x)==std::signbit(y) && std::signbit(y)==std::signbit(z) ? distance:-4.f;
}
} // namespace dw4
