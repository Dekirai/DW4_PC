#include "dw4/hitland_geometry.hpp"

#include "dw4/actor_geometry.hpp"
#include "dw4/actor_scene.hpp"
#include "dw4/hitland_primitives.hpp"
#include "dw4/hitland_traversal.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw HitLandQueryError(reason);}
std::int32_t previous(std::int32_t count) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(count)-1u);
}
std::int32_t signed_index(std::uint32_t index) {return std::bit_cast<std::int32_t>(index);}
std::shared_ptr<HitLandSegment> segment(const std::shared_ptr<ModelSceneObject>& object) {
    const auto result=std::dynamic_pointer_cast<HitLandSegment>(object);
    if(!result)fail("Original HITLAND geometry requires its reached line segment owner.");
    return result;
}
}

struct HitLandGeometryInvocation::Impl {
    enum class Phase {capture,transform_bound,volume,segment_bound,geometry,inverse,capture_lists,
        segment,attribute,primitive,payload,callback,done};
    Phase phase=Phase::capture;
    std::shared_ptr<HitLandIntersectionVisitor> visitor;
    std::shared_ptr<const ActorSceneNode> node;
    std::shared_ptr<HitLandGeometryServices> services;
    std::shared_ptr<const CollisionBox> source_bound,bound;
    std::shared_ptr<std::vector<std::optional<AnimationMatrix>>> matrix_storage;
    std::int32_t matrix_index{};
    std::shared_ptr<AnimationMatrix> matrix;
    AnimationMatrix inverse{};
    bool inverse_written{},executing{},fault{};
    std::shared_ptr<ModelSceneList> attributes,segments;
    std::shared_ptr<ModelSceneObject> current_segment;
    std::shared_ptr<ActorGeometryAttribute> attribute;
    std::uint32_t attribute_count{},segment_count{},attribute_index{},segment_index{},result{};
    std::int32_t side{};
    HitLandVisitorPayload payload;
    std::function<MessengerCallResult(const std::shared_ptr<HitLandIntersectionVisitor>&,
        const std::shared_ptr<const ActorSceneNode>&,HitLandVisitorPayload&)> callback;
    bool callback_selected{},query_callback{};
};
HitLandGeometryInvocation::HitLandGeometryInvocation(std::shared_ptr<HitLandIntersectionVisitor> visitor,
    std::shared_ptr<const ActorSceneNode> node):impl_(std::make_unique<Impl>()) {
    impl_->visitor=std::move(visitor);impl_->node=std::move(node);
}
HitLandGeometryInvocation::~HitLandGeometryInvocation()=default;
bool HitLandGeometryInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool HitLandGeometryInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult HitLandGeometryInvocation::resume(std::shared_ptr<HitLandGeometryServices> services) {
    auto& state=*impl_;using Phase=Impl::Phase;
    if(state.fault || state.executing) {state.fault=true;fail("HITLAND geometry requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,state.result,{}};
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(state.executing);
    const auto check=[&] {if(state.fault)fail("A caught recursive failure invalidated HITLAND geometry.");};
    const auto need=[](const char* reason) {return MessengerCallResult{false,0,reason};};
    const auto finish=[&](std::uint32_t result) {state.result=result;state.phase=Phase::done;return MessengerCallResult{true,result,{}};};
    const auto available=[&] {return state.services ? state.services:services;};
    const auto matrix=[&] {
        // 801F62A8 captures an address, not its contents. Resolve only when
        // inverse/point arithmetic actually consumes that captured allocation.
        if(!state.matrix_storage || state.matrix_index<0 ||
            static_cast<std::size_t>(state.matrix_index)>=state.matrix_storage->size())
            fail("Original HITLAND geometry reads an unavailable captured matrix address.");
        auto& value=(*state.matrix_storage)[static_cast<std::size_t>(state.matrix_index)];
        if(!value)fail("Original HITLAND geometry reads an unwritten captured matrix element.");
        state.matrix=std::shared_ptr<AnimationMatrix>(state.matrix_storage,&*value);
        return state.matrix;
    };
    try {
        if(state.services && services && state.services!=services)fail("Retained HITLAND geometry requires its original service owner.");
        while(true)switch(state.phase) {
        case Phase::capture:
            if(!state.visitor || !state.node)fail("Original HITLAND geometry dereferences a null visitor or node.");
            state.source_bound=state.node->box_owner();state.bound=state.source_bound;
            if(!state.bound)return finish(0);
            if(!state.visitor->matrix_stack)fail("Original HITLAND geometry dereferences a null matrix stack after its bound read.");
            state.phase=state.visitor->matrix_stack->count>1 ? Phase::transform_bound:Phase::volume;break;
        case Phase::transform_bound: {
            const auto provider=available();
            if(!provider || !provider->transform_bound)return need("HITLAND geometry requires original bound scratch copy and transform dispatch 801283DC.");
            state.services=provider;
            const auto result=provider->transform_bound(state.visitor,state.source_bound,state.bound);check();
            if(!result.completed)return result;
            state.phase=Phase::volume;break;
        }
        case Phase::volume:
            if(state.visitor->flags&1u) {
                const auto provider=available();
                if(!provider || !provider->volume_test)return need("HITLAND geometry requires its original volume test and kind-1 callback.");
                state.services=provider;
                const auto result=provider->volume_test(state.visitor,state.node,state.bound);check();
                if(!result.completed)return result;
                if(result.value)return finish(result.value);
            }
            state.phase=Phase::segment_bound;break;
        case Phase::segment_bound:
            if(state.visitor->flags&2u) {
                const auto provider=available();
                if(!provider || !provider->segment_bound_test)return need("HITLAND geometry requires original typed segment-bound tests and kind-2 callbacks.");
                state.services=provider;
                const auto result=provider->segment_bound_test(state.visitor,state.node,state.bound);check();
                if(!result.completed)return result;
                if(result.value)return finish(result.value);
            }
            state.phase=Phase::geometry;break;
        case Phase::geometry:
            if(!(state.visitor->flags&4u))return finish(0);
            if(!state.node->document().is_a(state.node->document().object(state.node->reference()),"igGeometry"))return finish(0);
            if(!state.visitor->matrix_stack)fail("Original HITLAND geometry dereferences its current null matrix stack.");
            state.matrix_storage=state.visitor->matrix_stack->backing;
            state.matrix_index=previous(state.visitor->matrix_stack->count);
            state.phase=state.visitor->matrix_stack->count>1 ? Phase::inverse:Phase::capture_lists;break;
        case Phase::inverse: {
            const auto provider=available();
            if(!provider || !provider->inverse_matrix)return need("HITLAND geometry requires original matrix inverse 8012A92C.");
            state.services=provider;
            const auto result=provider->inverse_matrix(matrix(),state.inverse);check();
            if(!result.completed)return result;
            state.inverse_written=true;state.phase=Phase::capture_lists;break;
        }
        case Phase::capture_lists: {
            const auto scratch=state.visitor->scratch;
            state.attributes=state.node->attribute_list_owner();
            if(!scratch)fail("Original HITLAND geometry dereferences a null scratch owner.");
            state.segments=scratch->segments;
            if(!state.attributes)fail("Original HITLAND geometry dereferences a null attribute list.");
            state.attribute_count=std::bit_cast<std::uint32_t>(state.attributes->count);
            if(!state.segments)fail("Original HITLAND geometry dereferences a null segment list.");
            state.segment_count=std::bit_cast<std::uint32_t>(state.segments->count);
            state.segment_index=0;state.phase=Phase::segment;break;
        }
        case Phase::segment: {
            if(state.segment_index>=state.segment_count)return finish(0);
            const auto scratch=state.visitor->scratch;
            const auto stack=state.visitor->matrix_stack;
            if(!scratch || !stack)fail("Original HITLAND segment loop dereferences current scratch/stack owners.");
            const auto indices=scratch->indices;
            const auto original=hitland_physical_object(state.segments,signed_index(state.segment_index));
            const auto matrix_count=stack->count;
            state.side=hitland_physical_index(indices,signed_index(state.segment_index));
            state.current_segment=original;
            if(matrix_count>1) {
                auto destination=state.visitor->transformed_segment;
                if(!destination)fail("Original HITLAND segment copy dereferences null transformed scratch.");
                const auto source=segment(state.current_segment);
                destination->origin=source->origin;
                destination->direction=source->direction;
                destination->length=source->length;
                destination=state.visitor->transformed_segment;
                if(!state.inverse_written)fail("Original HITLAND segment transform reads an unwritten captured inverse matrix.");
                original_hitland_transform_segment(*destination,state.inverse);
                state.current_segment=state.visitor->transformed_segment;
            }
            state.attribute_index=0;state.phase=Phase::attribute;break;
        }
        case Phase::attribute: {
            if(state.attribute_index>=state.attribute_count) {++state.segment_index;state.phase=Phase::segment;break;}
            const auto object=hitland_physical_object(state.attributes,signed_index(state.attribute_index));
            if(!object)fail("Original HITLAND attribute type test dereferences a null object.");
            // The producer checks abstract igGeometryAttr; the enumerator
            // separately checks igGeometryAttr1_5. Fresh primary owners use
            // that original native ancestry without a serialized document.
            const bool geometry=object->is_a("igGeometryAttr") ||
                (!object->document_owner() && object->type()=="igGeometryAttr1_5");
            if(!geometry && !object->is_a("igGeometryAttr2")) {++state.attribute_index;break;}
            state.attribute=std::dynamic_pointer_cast<ActorGeometryAttribute>(object);
            if(!state.attribute)fail("Original HITLAND geometry requires a canonical eligible attribute owner.");
            state.payload.group_index=-1;state.payload.triangle_index=-1;
            state.phase=Phase::primitive;break;
        }
        case Phase::primitive: {
            const auto found=original_hitland_primitive(state.attribute,state.current_segment,state.payload.local_position,
                state.payload.normal,state.payload.group_index,state.payload.triangle_index,state.side,1,check);
            check();
            if(!found) {
                ++state.attribute_index;state.phase=Phase::attribute;break;
            }
            state.phase=Phase::payload;break;
        }
        case Phase::payload: {
            const auto original=hitland_physical_object(state.segments,signed_index(state.segment_index));
            state.payload.kind=4;state.payload.node_path=state.visitor->node_path;
            state.payload.attribute_index=signed_index(state.attribute_index);
            state.payload.segment=original;
            state.payload.scratch=state.visitor->scratch;
            state.payload.matrix=matrix();
            original_hitland_point(state.payload.position,state.payload.local_position,*state.payload.matrix);
            state.payload.distance=original_hitland_distance(segment(original)->origin,state.payload.position);
            state.callback_selected=false;state.callback={};state.phase=Phase::callback;break;
        }
        case Phase::callback: {
            if(!state.callback_selected) {
                state.query_callback=state.visitor->query_candidate_callback;
                if(!state.query_callback) {
                    const auto provider=available();
                    if(!provider || !provider->candidate)return need("HITLAND geometry requires its actual candidate callback.");
                    state.services=provider;
                    state.callback=provider->candidate;
                }
                state.callback_selected=true;
            }
            if(state.query_callback) {
                const auto query=state.visitor->query;
                if(!query)fail("Original 802FE1A4 callback dereferences the current null query.");
                query->append_candidate(state.payload);
            } else {
                const auto result=state.callback(state.visitor,state.node,state.payload);check();
                if(!result.completed)return result;
                if(result.value)return finish(result.value);
            }
            state.phase=Phase::primitive;break;
        }
        case Phase::done:return finish(state.result);
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
