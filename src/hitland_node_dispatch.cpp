#include "dw4/hitland_node_dispatch.hpp"
#include "dw4/actor_transform.hpp"
#include "dw4/game_clock.hpp"

#include "dw4/actor_scene.hpp"
#include "dw4/hitland_geometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>

namespace dw4 {
namespace {
constexpr std::int32_t limit=65536;
[[noreturn]] void fail(const char* reason) {throw HitLandQueryError(reason);}
std::int32_t add(std::int32_t value,std::uint32_t increment) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)+increment);
}
template<class List> void grow(const std::shared_ptr<List>& list,std::int32_t count) {
    auto capacity=std::max<std::int32_t>(list->capacity,4);
    while(capacity<count) {
        if(capacity>=limit)fail("HITLAND native list growth exceeds its allocation bound.");
        capacity=capacity<1024 ? capacity*2:capacity+1024;
    }
    if(capacity>limit)fail("HITLAND native list capacity exceeds its allocation bound.");
    const auto previous=list->backing;
    using Storage=typename decltype(list->backing)::element_type;
    auto replacement=std::make_shared<Storage>(static_cast<std::size_t>(capacity));
    list->backing=replacement;
    if(previous) {
        const auto copied=std::min(list->capacity,capacity);
        if(copied<0 || static_cast<std::size_t>(copied)>previous->size())
            fail("HITLAND native list growth exceeds its old physical capacity.");
        std::copy_n(previous->begin(),copied,replacement->begin());
    }
    list->capacity=capacity;list->count=count;
}
template<class List> auto& physical(const std::shared_ptr<List>& list,std::int32_t index) {
    if(!list || !list->backing || index<0 || index>=limit ||
        static_cast<std::size_t>(index)>=list->backing->size())
        fail("HITLAND native operation reaches unavailable physical list storage.");
    return (*list->backing)[static_cast<std::size_t>(index)];
}
AnimationMatrix paired_copy(const AnimationMatrix& source) {
    AnimationMatrix result;
    for(std::size_t i=0;i<16;++i) {
        auto bits=std::bit_cast<std::uint32_t>(source[i]);
        if((bits&0x7f800000u)==0)bits&=0x80000000u;
        result[i]=std::bit_cast<float>(bits);
    }
    return result;
}
float finite(float value) {
    if(!std::isfinite(value))fail("HITLAND matrix composition exceeds its finite scalar profile.");
    return value;
}
constexpr float minimum=std::numeric_limits<float>::min();
float product(float a,float b) {
    finite(a);finite(b);const float result=finite(a*b);
    if(std::abs(result)<minimum || (std::abs(result)==minimum &&
        std::abs(static_cast<double>(a)*b)<minimum))return std::copysign(0.0f,result);
    return result;
}
float fused(float a,float b,float c) {
    finite(a);finite(b);finite(c);const float result=finite(std::fma(a,b,c));
    const float magnitude=std::abs(result);
    if(magnitude<minimum)return std::copysign(0.0f,result);
    if(magnitude!=minimum)return result;
    const double multiplied=static_cast<double>(a)*b,added=multiplied+c;
    const double virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+(c-virtual_addend);
    if(std::abs(added)<minimum || (std::abs(added)==minimum &&
        ((added>0 && error<0) || (added<0 && error>0))))return std::copysign(0.0f,result);
    return result;
}
void compose(AnimationMatrix& destination,const AnimationMatrix& source) {
    // 80128774 captures all node-matrix operands, writes a local result, then
    // publishes through the paired copy. Its first rounded product is Y.
    const auto node=source;AnimationMatrix result;
    for(std::size_t row=0;row<4;++row)for(std::size_t column=0;column<4;++column) {
        const auto at=column*4;
        float value=product(node[at+1],destination[row+4]);
        value=fused(node[at],destination[row],value);
        value=fused(node[at+2],destination[row+8],value);
        result[at+row]=fused(node[at+3],destination[row+12],value);
    }
    destination=paired_copy(result);
}
struct ClassRegistration {std::uint32_t index,target;};
std::map<std::string,ClassRegistration> registrations;
std::mutex registration_mutex;
}

std::shared_ptr<HitLandDispatchTable> default_hitland_dispatch_table() {
    // Original visitor initialization shares the registered function table.
    static const auto table=std::make_shared<HitLandDispatchTable>();return table;
}
void register_hitland_node_type(const std::shared_ptr<HitLandIntersectionVisitor>& visitor,
    const std::shared_ptr<const ActorSceneNode>& node) {
    if(!visitor || !node)fail("HITLAND type registration requires its actual visitor and node.");
    if(!node->is_a("igNode"))fail("HITLAND registration requires an actual igNode family.");
    const auto target=static_cast<std::uint32_t>(node->is_a("igGeometry") ? HitLandNodeHandler::geometry:
        node->is_a("igTransform") ? HitLandNodeHandler::transform:
        node->is_a("igGroup") ? HitLandNodeHandler::group:HitLandNodeHandler::empty);
    std::lock_guard lock(registration_mutex);
    auto found=registrations.find(node->type());
    if(found==registrations.end()) {
        if(registrations.size()>=static_cast<std::size_t>(limit))fail("HITLAND type registration exceeds its native class bound.");
        found=registrations.emplace(node->type(),ClassRegistration{static_cast<std::uint32_t>(registrations.size()),target}).first;
    } else if(found->second.target!=target)fail("HITLAND runtime class has conflicting declared inheritance.");
    const auto index=static_cast<std::int32_t>(found->second.index);
    const auto table=visitor->dispatch_table;
    if(!table)fail("HITLAND type registration reaches a null function table.");
    std::const_pointer_cast<ActorSceneNode>(node)->set_runtime_dispatch_index(found->second.index);
    if(index>=table->count) {
        if(index+1<=table->capacity)table->count=index+1;else grow(table,index+1);
    }
    if(table->count && index>=0 && index<table->count)physical(table,index)=target;
}
void hitland_push_path(const std::shared_ptr<HitLandNodePath>& path,std::shared_ptr<const ActorSceneNode> node) {
    if(!path)fail("HITLAND path append reaches a null path.");
    const auto index=path->count;
    if(index<path->capacity)path->count=add(index,1);else grow(path,add(index,1));
    auto& destination=physical(path,index);
    if(destination && *destination)fail("HITLAND raw path append would discard an unreleased spare node.");
    destination=std::move(node);
}
void hitland_pop_path(const std::shared_ptr<HitLandNodePath>& path) {
    if(!path)fail("HITLAND path pop reaches a null current path.");
    const auto index=add(path->count,0xffffffffu);
    auto& value=physical(path,index);
    if(!value)fail("HITLAND path pop releases an uninitialized pointer.");
    // Canonical node destruction has no arbitrary guest callback. Count stays
    // unchanged through the release; the original80041C10 removes this slot.
    value->reset();
    if(path->count>0 && index>=0 && index<path->count)path->count=add(path->count,0xffffffffu);
    physical(path,path->count)=std::shared_ptr<const ActorSceneNode>{};
}
void hitland_push_transform(const std::shared_ptr<HitLandMatrixList>& stack,const AnimationMatrix& node) {
    if(!stack)fail("HITLAND transform append reaches a null matrix stack.");
    const auto index=stack->count;
    if(index<stack->capacity)stack->count=add(index,1);else grow(stack,add(index,1));
    if(index==0) {physical(stack,index)=paired_copy(node);return;}
    const auto previous=hitland_physical_matrix(stack,add(index,0xffffffffu));
    physical(stack,index)=paired_copy(*previous);
    auto destination=hitland_physical_matrix(stack,add(stack->count,0xffffffffu));
    compose(*destination,node);
}
void hitland_pop_transform(const std::shared_ptr<HitLandMatrixList>& stack) {
    if(!stack)fail("HITLAND transform pop reaches a null current stack.");
    stack->count=add(stack->count,0xffffffffu);
    const auto popped=hitland_physical_matrix(stack,stack->count);
    const auto discarded=paired_copy(*popped);(void)discarded;
}

struct HitLandNodeDispatchInvocation::Impl {
    enum class Phase {outer_filter,select,inner_filter,group_gate,time,clock,update,bound,bound_service,
        push_path,producer,push_matrix,children_begin,children,pop_matrix,pop_path,other,done};
    enum class AfterChildren {done,pop_path,pop_matrix};
    std::shared_ptr<HitLandIntersectionVisitor> visitor;
    std::shared_ptr<const ActorSceneNode> node;
    std::shared_ptr<HitLandNodeDispatchServices> services;
    decltype(HitLandIntersectionVisitor::filter) filter;
    std::function<std::optional<std::uint64_t>()> clock;
    decltype(HitLandNodeDispatchServices::transform_update) update;
    decltype(HitLandNodeDispatchServices::geometry_bound) bound_service;
    decltype(HitLandNodeDispatchServices::other_handler) other;
    std::unique_ptr<HitLandGeometryInvocation> producer;
    std::unique_ptr<ActorTransformUpdateInvocation> native_update;
    std::unique_ptr<HitLandNodeDispatchInvocation> child;
    std::shared_ptr<const ActorSceneNodeStorage> children;
    std::shared_ptr<const CollisionBox> bound;
    std::uint64_t time{};
    std::uint32_t target{},result{},child_count{},child_index{};
    bool filter_started{},executing{},fault{};
    Phase phase=Phase::outer_filter;
    AfterChildren after_children=AfterChildren::done;
};
HitLandNodeDispatchInvocation::HitLandNodeDispatchInvocation(std::shared_ptr<HitLandIntersectionVisitor> visitor,
    std::shared_ptr<const ActorSceneNode> node):impl_(std::make_unique<Impl>()) {
    impl_->visitor=std::move(visitor);impl_->node=std::move(node);
}
HitLandNodeDispatchInvocation::~HitLandNodeDispatchInvocation()=default;
bool HitLandNodeDispatchInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool HitLandNodeDispatchInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult HitLandNodeDispatchInvocation::resume(std::shared_ptr<HitLandNodeDispatchServices> supplied) {
    auto& s=*impl_;
    if(s.fault || s.executing) {s.fault=true;fail("HITLAND node dispatch requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.result,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.executing);
    auto check=[&] {if(s.fault)fail("A caught recursive operation faulted HITLAND node dispatch.");};
    auto require_node=[&] {if(!s.node)fail("Original HITLAND dispatch dereferences a null node.");};
    auto finish=[&](std::uint32_t result) {s.result=result;s.phase=Impl::Phase::done;};
    auto pending=[](const char* reason) {return MessengerCallResult{false,0,reason};};
    try {
        if(!s.visitor)fail("Original HITLAND dispatch dereferences a null visitor.");
        if(s.services && supplied && s.services!=supplied)fail("Retained HITLAND node dispatch requires its original services.");
        if(!s.services && supplied)s.services=std::move(supplied);
        while(!completed())switch(s.phase) {
        case Impl::Phase::outer_filter:
        case Impl::Phase::inner_filter: {
            const bool outer=s.phase==Impl::Phase::outer_filter;
            if(!s.filter_started) {
                s.filter_started=true;
                if(s.visitor->filter_enabled) {
                    s.filter=s.visitor->filter;
                    if(!s.filter)fail("Enabled HITLAND filter reaches a null callback.");
                }
            }
            std::uint32_t value=0;
            if(s.filter) {
                const auto result=s.filter(s.visitor,s.node);check();
                if(!result.completed)return result;value=result.value;
            }
            s.filter={};s.filter_started=false;check();
            if((outer && value!=0 && value!=2) || (!outer && value==1)) {finish(0);break;}
            if(value==2) {
                require_node();
                if(s.node->type()!="igNode") {s.after_children=Impl::AfterChildren::done;s.phase=Impl::Phase::children_begin;break;}
            }
            if(outer)s.phase=Impl::Phase::select;
            else s.phase=s.target==static_cast<std::uint32_t>(HitLandNodeHandler::transform) ? Impl::Phase::time:Impl::Phase::bound;
            break;
        }
        case Impl::Phase::select: {
            const auto table=s.visitor->dispatch_table;require_node();
            const auto index=s.node->runtime_dispatch_index();
            if(!index)return pending("Original HITLAND node requires its explicit native runtime type registration.");
            if(*index>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
                fail("Original HITLAND runtime dispatch index exceeds its physical address profile.");
            const auto target=physical(table,static_cast<std::int32_t>(*index));
            if(!target)fail("Original HITLAND dispatch reads an uninitialized function slot.");
            if(*target==0)fail("Original HITLAND dispatch calls a null table target.");
            s.target=*target;
            if(s.target==static_cast<std::uint32_t>(HitLandNodeHandler::empty))finish(0);
            else if(s.target==static_cast<std::uint32_t>(HitLandNodeHandler::group))s.phase=Impl::Phase::group_gate;
            else if(s.target==static_cast<std::uint32_t>(HitLandNodeHandler::transform) ||
                s.target==static_cast<std::uint32_t>(HitLandNodeHandler::geometry))s.phase=Impl::Phase::inner_filter;
            else s.phase=Impl::Phase::other;
            break;
        }
        case Impl::Phase::group_gate: {
            const auto list=s.node->child_list_owner();
            if(!list || list->count()==0)finish(0);else s.phase=Impl::Phase::push_path;
            break;
        }
        case Impl::Phase::time:
            if(s.visitor->bound_time) {s.time=(std::uint64_t(s.visitor->time_high)<<32)|s.visitor->time_low;s.phase=Impl::Phase::update;}
            else s.phase=Impl::Phase::clock;
            break;
        case Impl::Phase::clock: {
            if(!s.clock && s.services)s.clock=s.services->global_clock;
            if(!s.clock && s.services && s.services->game_clock)
                s.clock=[clock=s.services->game_clock]()->std::optional<std::uint64_t> {return clock->time();};
            if(!s.clock)return pending("Original HITLAND transform time requires the actual global clock virtual+68.");
            const auto time=s.clock();check();
            if(!time)return pending("Original HITLAND transform time is pending its actual global clock.");
            s.time=*time;s.phase=Impl::Phase::update;break;
        }
        case Impl::Phase::update: {
            if(!s.update && !s.native_update && s.services)s.update=s.services->transform_update;
            MessengerCallResult result;
            if(s.update)result=s.update(s.node,s.time);
            else {
                if(!s.native_update)s.native_update=std::make_unique<ActorTransformUpdateInvocation>(s.node,s.time);
                result=s.native_update->resume(s.services ? s.services->actor_transform:nullptr);
            }
            check();
            if(!result.completed)return result;s.phase=Impl::Phase::push_path;break;
        }
        case Impl::Phase::bound:
            s.bound=s.node->box_owner();s.phase=s.bound ? Impl::Phase::push_path:Impl::Phase::bound_service;break;
        case Impl::Phase::bound_service: {
            if(!s.bound_service && s.services)s.bound_service=s.services->geometry_bound;
            if(!s.bound_service)return pending("Original HITLAND geometry requires its reached attribute-derived bound construction.");
            const auto result=s.bound_service(s.visitor,s.node,s.bound);check();
            if(!result.completed)return result;
            if(s.bound)s.phase=Impl::Phase::push_path;else finish(0);break;
        }
        case Impl::Phase::push_path:
            hitland_push_path(s.visitor->node_path,s.node);check();
            s.producer=std::make_unique<HitLandGeometryInvocation>(s.visitor,s.node);s.phase=Impl::Phase::producer;break;
        case Impl::Phase::producer: {
            const auto result=s.producer->resume(s.services ? s.services->geometry:nullptr);check();
            if(!result.completed)return result;s.result=result.value;s.producer.reset();check();
            if(s.target==static_cast<std::uint32_t>(HitLandNodeHandler::geometry) || s.result!=0)s.phase=Impl::Phase::pop_path;
            else if(s.target==static_cast<std::uint32_t>(HitLandNodeHandler::transform))s.phase=Impl::Phase::push_matrix;
            else {s.after_children=Impl::AfterChildren::pop_path;s.phase=Impl::Phase::children_begin;}
            break;
        }
        case Impl::Phase::push_matrix: {
            const auto stack=s.visitor->matrix_stack;
            const auto matrix=s.node->matrix_owner();
            hitland_push_transform(stack,*matrix);check();
            s.after_children=Impl::AfterChildren::pop_matrix;s.phase=Impl::Phase::children_begin;break;
        }
        case Impl::Phase::children_begin: {
            require_node();const auto list=s.node->child_list_owner();s.children=list ? list->storage_owner():nullptr;
            const auto count=list ? list->count():0;
            // The original retains physical begin/end pointers. Negative or
            // wrapping guest byte ranges cannot be guessed from a host address.
            if(count<0)fail("Original HITLAND child traversal reaches an unsupported wrapped pointer range.");
            s.child_count=static_cast<std::uint32_t>(count);s.child_index=0;s.result=0;s.phase=Impl::Phase::children;break;
        }
        case Impl::Phase::children: {
            if(s.child_index<s.child_count) {
                if(!s.child) {
                    if(!s.children || s.child_index>=s.children->values().size() || s.child_index>=static_cast<std::uint32_t>(limit))
                        fail("Original HITLAND child traversal reaches unavailable captured physical backing.");
                    s.child=std::make_unique<HitLandNodeDispatchInvocation>(s.visitor,s.children->values()[s.child_index]);
                }
                const auto result=s.child->resume(s.services);check();
                if(!result.completed)return result;s.child.reset();check();
                if(result.value==1 || result.value==2) {s.result=result.value==2 ? 2u:0u;s.child_index=s.child_count;}
                else {++s.child_index;break;}
            }
            if(s.after_children==Impl::AfterChildren::done)finish(s.result==2 ? 2u:0u);
            else s.phase=s.after_children==Impl::AfterChildren::pop_matrix ? Impl::Phase::pop_matrix:Impl::Phase::pop_path;
            break;
        }
        case Impl::Phase::pop_matrix:
            hitland_pop_transform(s.visitor->matrix_stack);check();s.phase=Impl::Phase::pop_path;break;
        case Impl::Phase::pop_path:
            hitland_pop_path(s.visitor->node_path);check();finish(s.result==2 ? 2u:0u);break;
        case Impl::Phase::other: {
            if(!s.other && s.services)s.other=s.services->other_handler;
            if(!s.other)return pending("Original HITLAND dispatch requires its reached concrete function-table target.");
            const auto result=s.other(s.target,s.visitor,s.node);check();
            if(!result.completed)return result;finish(result.value);break;
        }
        case Impl::Phase::done:break;
        }
        return {true,s.result,{}};
    }catch(...) {s.fault=true;throw;}
}
} // namespace dw4
