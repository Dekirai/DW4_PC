#include "dw4/hitland_traversal.hpp"
#include "dw4/hitland_intersection.hpp"
#include "dw4/hitland_math.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ModelSceneError(reason);}
template<class T> const std::shared_ptr<T>& require(const std::shared_ptr<T>& owner,const char* reason) {
    if(!owner)fail(reason);return owner;
}
constexpr std::int32_t allocation_limit=1048576;
void resize_indices(const std::shared_ptr<HitLandIndexList>& list,std::int32_t count) {
    auto capacity=std::max(list->capacity,std::int32_t{4});
    while(capacity<count) {
        if(capacity>=allocation_limit)fail("HITLAND index allocation exceeds its native host bound.");
        capacity=capacity<1024 ? capacity*2:capacity+1024;
    }
    if(capacity>allocation_limit)fail("HITLAND index allocation exceeds its native host bound.");
    // 8004155C publishes the new allocation before copying old capacity words.
    const auto old=list->backing;
    const auto copied=std::min(list->capacity,capacity);
    list->backing=std::make_shared<std::vector<std::optional<std::int32_t>>>(static_cast<std::size_t>(capacity));
    if(old) {
        if(copied<0 || static_cast<std::size_t>(copied)>old->size())
            fail("HITLAND index growth reached an invalid old physical capacity.");
        std::copy_n(old->begin(),copied,list->backing->begin());
    }
    list->capacity=capacity;list->count=count;
}
float add_single(float a,float b) {
    const float result=a+b;
    return std::abs(result)<std::numeric_limits<float>::min() ? std::copysign(0.0f,result):result;
}
void copy_paired_matrix(AnimationMatrix& to,const AnimationMatrix& from) {
    // 80128CF4 uses GQR0 paired stores, which flush subnormal words even
    // though this copy has no arithmetic. Scalar node translation differs.
    for(std::size_t i=0;i<to.size();++i) {
        auto word=std::bit_cast<std::uint32_t>(from[i]);
        if((word&0x7f800000u)==0)word&=0x80000000u;
        to[i]=std::bit_cast<float>(word);
    }
}
}
void hitland_clear_indices(const std::shared_ptr<HitLandIndexList>& list) {
    require(list,"HITLAND scratch has no reached integer list.");
    if(list->capacity>=0)list->count=0;else resize_indices(list,0);
}
void hitland_append_zero_index(const std::shared_ptr<HitLandIndexList>& list) {
    require(list,"HITLAND scratch has no reached integer list.");
    const auto index=list->count;
    const auto next=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(index)+1u);
    if(index<list->capacity)list->count=next;else resize_indices(list,next);
    if(index<0 || !list->backing || static_cast<std::size_t>(index)>=list->backing->size())
        fail("HITLAND index append reached no physical destination after size publication.");
    (*list->backing)[static_cast<std::size_t>(index)]=0;
}
std::int32_t hitland_physical_index(const std::shared_ptr<HitLandIndexList>& list,std::int32_t index) {
    require(list,"HITLAND index read reached a null list.");
    if(index<0 || !list->backing || static_cast<std::size_t>(index)>=list->backing->size())
        fail("HITLAND index read reached no physical word.");
    const auto value=(*list->backing)[static_cast<std::size_t>(index)];
    if(!value)fail("HITLAND index read reached an unspecified allocation word.");
    return *value;
}

struct HitLandTraversalInvocation::Impl {
    enum class Phase {prepare,info,child,visit,done};
    std::shared_ptr<HitLandTraversalManager> manager;
    std::shared_ptr<HitLandQuery> query;
    std::shared_ptr<ActorSceneNode> selected_switch;
    std::unique_ptr<HitLandIntersectionInvocation> visit;
    std::int32_t info_count{},info_index{},child_count{},child_index{};
    std::uint32_t kinds{};
    CollisionBox bounds;
    bool dynamic{},executing{},faulted{};
    Phase phase=Phase::prepare;
};
HitLandTraversalInvocation::HitLandTraversalInvocation(std::shared_ptr<HitLandTraversalManager> manager,
    std::shared_ptr<HitLandQuery> query):impl_(std::make_unique<Impl>()) {
    impl_->manager=std::move(manager);impl_->query=std::move(query);
}
HitLandTraversalInvocation::~HitLandTraversalInvocation()=default;
bool HitLandTraversalInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool HitLandTraversalInvocation::faulted() const noexcept {return impl_->faulted;}
MessengerCallResult HitLandTraversalInvocation::resume(std::shared_ptr<HitLandIntersectionServices> supplied) {
    auto& state=*impl_;
    if(state.executing){state.faulted=true;fail("HITLAND traversal cannot recursively resume its active invocation.");}
    if(state.faulted)fail("HITLAND traversal is permanently faulted.");
    if(completed())return {true,0,{}};
    state.executing=true;
    struct Guard {bool& executing;~Guard(){executing=false;}} guard{state.executing};
    auto check=[&] {if(state.faulted)fail("HITLAND traversal was faulted by a caught nested operation.");};
    try {
        const auto query=require(state.query,"HITLAND traversal reached a null passed query.");
        auto manager=[&] {return require(state.manager,"HITLAND traversal reached a null manager.");};
        auto visitor=[&] {return require(manager()->visitor,"HITLAND traversal reached a null intersection visitor.");};
        auto node_at=[&](std::int32_t index) {
            const auto children=require(state.selected_switch->child_list_owner(),"HITLAND selected switch lost its reached child list.");
            return require(children->backing_node(static_cast<std::size_t>(index)),"HITLAND switch selected a null physical child.");
        };
        while(!completed())switch(state.phase) {
        case Impl::Phase::prepare: {
            const auto count=query->segment_count;
            if(count<=0){state.phase=Impl::Phase::done;break;}
            const auto scratch=require(manager()->scratch,"HITLAND traversal reached a null scratch owner.");
            hitland_clear_objects(scratch->segments);check();
            hitland_clear_indices(scratch->indices);check();
            for(std::int32_t index=0;index<count;++index) {
                const auto segment_list=query->segments;
                const auto current=manager()->scratch;
                require(segment_list,"HITLAND scratch preparation reached a null query segment list.");
                // The source backing load precedes scratch+0C; the physical
                // source slot is consumed only after that destination lookup.
                const auto source_backing=segment_list->backing;
                static_cast<void>(source_backing);
                require(current,"HITLAND traversal lost its current scratch owner.");
                const auto destination=current->segments;
                const auto segment=hitland_physical_object(segment_list,index);
                hitland_append_object(destination,segment);check();
                hitland_append_zero_index(current->indices);check();
            }
            reset_hitland_intersection(visitor());check();
            visitor()->flags=0;
            visitor()->query=query;check();
            // The scratch load precedes the separate current visitor load.
            const auto current_scratch=manager()->scratch;
            visitor()->scratch=current_scratch;check();
            visitor()->query_candidate_callback=true;
            visitor()->flags|=4;
            state.bounds.maximum=query->maximum;state.bounds.minimum=query->minimum;
            const auto infos=require(manager()->infos,"HITLAND traversal reached a null manager info list.");
            state.kinds=query->flags&0x1ffu;
            state.info_count=infos->count;state.phase=Impl::Phase::info;break;
        }
        case Impl::Phase::info: {
            if(state.info_index>=state.info_count){state.phase=Impl::Phase::done;break;}
            const auto owner=require(hitland_physical_object(manager()->infos,state.info_index),"HITLAND info traversal reached a null occurrence.");
            auto name=owner->name.value_or(std::string{});
            if(name.size()>65535 || name.find('\0')!=std::string::npos)
                fail("Original HITLAND info-name copy exceeds its bounded C-string profile.");
            query->current_name=std::move(name);check();
            const auto info=std::dynamic_pointer_cast<ModelHitLandGeometryInfo>(owner);
            if(!info)fail("HITLAND info occurrence has no represented physical kind field.");
            const auto kind=static_cast<std::uint32_t>(info->kind);
            if((kind&state.kinds)==0 || ((kind&0x800u)!=0 && (query->flags&0x800u)!=0) ||
                ((query->flags&0x1000u)!=0 && (kind&0x1000u)==0)) {++state.info_index;break;}
            state.selected_switch=require(info->check_switch,"HITLAND info reached a null inner check switch.");
            const auto children=state.selected_switch->child_list_owner();
            state.child_count=children ? children->count():0;
            state.dynamic=(kind&0x400u)!=0;state.child_index=0;state.phase=Impl::Phase::child;break;
        }
        case Impl::Phase::child: {
            if(state.child_index>=state.child_count) {
                ++state.info_index;state.selected_switch.reset();check();state.phase=Impl::Phase::info;break;
            }
            const auto mask=require(state.selected_switch->switch_mask_owner(),"HITLAND child selection reached a null mask owner.");
            const auto index=static_cast<std::uint32_t>(state.child_index);
            if((mask->backing_word(index>>5)&(std::uint32_t{1}<<(index&31)))==0){++state.child_index;break;}
            query->current_index=state.child_index;
            auto node=node_at(state.child_index);
            if(state.dynamic) {
                const auto source=require(node->source_transform_owner(),"HITLAND dynamic child reached a null source transform.");
                const auto destination=node->matrix_owner();const auto matrix=source->matrix_owner();
                copy_paired_matrix(*destination,*matrix);
                const std::array<float,3> position{(*destination)[12],(*destination)[13],(*destination)[14]};
                const auto children=require(node->child_list_owner(),"HITLAND dynamic bound reached a null nested child list.");
                const auto first=require(children->backing_node(0),"HITLAND dynamic bound reached a null physical first child.");
                const auto box=require(first->box_owner(),"HITLAND dynamic bound reached a null first-child box.");
                CollisionBox translated;
                for(std::size_t axis=0;axis<3;++axis)translated.maximum[axis]=add_single(box->maximum[axis],position[axis]);
                for(std::size_t axis=0;axis<3;++axis)translated.minimum[axis]=add_single(box->minimum[axis],position[axis]);
                if(!original_hitland_xz_overlap(translated,state.bounds)){++state.child_index;break;}
            } else {
                const auto box=node->box_owner();
                if(box) {
                    if(!original_hitland_xz_overlap(*box,state.bounds)){++state.child_index;break;}
                    // 80300E28 reloads the selected physical child after broad phase.
                    node=node_at(state.child_index);
                }
            }
            state.visit=std::make_unique<HitLandIntersectionInvocation>(visitor(),node);
            state.phase=Impl::Phase::visit;break;
        }
        case Impl::Phase::visit: {
            const auto result=state.visit->resume(supplied);check();
            if(!result.completed)return result;
            state.visit.reset();check();
            ++state.child_index;state.phase=Impl::Phase::child;break;
        }
        case Impl::Phase::done:break;
        }
        return {true,0,{}};
    } catch(...) {state.faulted=true;throw;}
}
} // namespace dw4
