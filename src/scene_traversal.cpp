#include "dw4/scene_traversal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t max_attributes=65536,max_visits=262144,max_depth=130;
[[noreturn]] void fail(const char* reason) { throw SceneTraversalError(reason); }
const IgbFieldValue& field(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot,
    std::string_view type,std::size_t width) {
    const auto* value=doc.field(object,slot);
    if(!value || doc.metadata.field_types.at(value->type_index).name!=type || value->size!=width)
        fail("Common scene field has an unsupported type or width.");
    doc.field_bytes(*value);return *value;
}
void unique_fields(const IgbObject& object) {
    std::set<std::uint16_t> seen;
    for(const auto& value:object.fields)if(!seen.insert(value.slot).second)fail("Common scene object repeats a field slot.");
}
std::uint32_t canonical(const IgbDocument& doc,std::uint32_t ref) {
    const auto& object=doc.object(ref);
    if(&doc.object(object.reference_index)!=&object)fail("Common scene reference does not identify its physical owner.");
    return object.reference_index;
}
void finite(const AnimationMatrix& matrix) {
    for(const auto value:matrix)if(!std::isfinite(value))fail("Common scene model-view contains non-finite values.");
}
using Attribute=std::shared_ptr<const SceneAttributeOwner>;
using Key=std::pair<std::string,std::int16_t>;
Key key(const Attribute& value) { return {value->type(),value->unit()}; }
enum class Stage { previsit, enter, transform, flush, children, geometry, external, leave };
}

SceneAttributeOwner::SceneAttributeOwner(std::shared_ptr<const ActorSceneNode> owner,std::uint32_t reference)
    :owner_(std::move(owner)) {
    try {
        if(!owner_)fail("A common scene attribute requires an actual loaded document owner.");
        const auto& doc=owner_->document();reference_=canonical(doc,reference);
        const auto& object=doc.object(reference_);unique_fields(object);
        if(!doc.is_a(object,"igAttr"))fail("A common scene attribute list contains a non-attribute.");
        type_=doc.metadata.object_types.at(object.type_index).name;
        field(doc,object,2,"igShortMetaField",2);unit_=doc.field_i16(object,2);
        if(unit_<0 || unit_>255)fail("Common scene attribute unit exceeds its checked registry profile.");
    } catch(const SceneTraversalError&) {throw;}
      catch(const std::exception& error) {throw SceneTraversalError(error.what());}
}

struct SceneTraversal::Impl {
    struct Frame {
        std::shared_ptr<const ActorSceneNode> node;
        std::vector<std::shared_ptr<const ActorSceneNode>> children;
        AnimationMatrix view{};
        Stage stage=Stage::previsit;
        std::size_t cursor{},pushed{};
        std::int32_t value{};
        std::optional<std::uint32_t> attribute_list;
        std::vector<Attribute> attributes;
        std::shared_ptr<const SceneSubmissionRequest> request;
        std::function<bool(const SceneSubmissionRequest&)> submit,flush;
        std::function<std::optional<std::int32_t>(const SceneSubmissionRequest&)> previsit,handler;
        std::function<std::optional<std::uint64_t>(const SceneSubmissionRequest&)> clock;
    };
    std::weak_ptr<SceneTraversalContext::Impl> owner;
    std::shared_ptr<const ActorBoneSelector> selector;
    std::shared_ptr<const BoneSelectModelView> selected_view;
    std::vector<Frame> frames;
    std::uint64_t generation{};
    std::size_t visits{};
    bool done{},failed{};
    std::int32_t value{};
    std::string diagnostic;
};
struct SceneTraversalContext::Impl {
    SceneGraphicsScope scope;
    std::vector<Attribute> attributes;
    std::map<std::pair<const IgbDocument*,std::uint32_t>,Attribute> owners;
    std::shared_ptr<SceneTraversal::Impl> current;
    std::uint64_t generation{};
    bool executing{},releasing{},failed{};
    explicit Impl(SceneGraphicsScope value):scope(std::move(value)) {}
    ~Impl() {releasing=true;}
    void readable() const {
        if(releasing)fail("Common scene ownership cannot be reentered during release.");
    }
    void idle() const {
        readable();
        if(executing || current || failed)fail("The retained common scene traversal owns this context.");
    }
    Attribute attribute(const std::shared_ptr<const ActorSceneNode>& node,std::uint32_t ref) {
        const auto index=std::pair{&node->document(),canonical(node->document(),ref)};
        if(const auto found=owners.find(index);found!=owners.end())return found->second;
        if(owners.size()>=max_attributes)fail("Common scene exceeds its attribute owner budget.");
        auto value=std::make_shared<const SceneAttributeOwner>(node,ref);owners.emplace(index,value);return value;
    }
    std::vector<Attribute> effective() const {
        std::vector<Attribute> result;std::map<Key,std::size_t> indices;std::set<Key> pinned;
        for(const auto& inherited:scope.inherited_attributes) {
            indices.emplace(key(inherited.attribute),result.size());result.push_back(inherited.attribute);
            if(inherited.override_selected)pinned.insert(key(inherited.attribute));
        }
        for(const auto& value:attributes) {
            const auto k=key(value);if(pinned.contains(k))continue;
            const auto found=indices.find(k);
            if(found!=indices.end())result[found->second]=value;
            else {indices.emplace(k,result.size());result.push_back(value);}
        }
        return result;
    }
    void read_attributes(SceneTraversal::Impl::Frame& frame) {
        const auto& doc=frame.node->document();const auto& object=doc.object(frame.node->reference());
        field(doc,object,8,"igObjectRefMetaField",4);
        const auto ref=doc.field_reference(object,8);
        // The original normal attribute handler dereferences its list; null is
        // an unchecked guest boundary, not a silently empty native list.
        if(!ref)fail("Common attribute set requires its actual attribute list.");
        frame.attribute_list=canonical(doc,*ref);const auto& list=doc.object(*ref);unique_fields(list);
        if(doc.metadata.object_types.at(list.type_index).name!="igAttrList")fail("Common scene requires an exact attribute list.");
        field(doc,list,2,"igIntMetaField",4);field(doc,list,3,"igIntMetaField",4);field(doc,list,4,"igMemoryRefMetaField",4);
        const auto count=doc.field_u32(list,2),capacity=doc.field_u32(list,3);
        if(count>capacity || capacity>max_attributes)fail("Common scene attribute list exceeds its capacity budget.");
        const auto memory=doc.field_reference(list,4);
        if((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("Common scene attribute list storage differs from its capacity.");
        for(const auto item:doc.object_list(*ref)) {
            if(!item)fail("Common scene attribute list contains a null owner.");
            frame.attributes.push_back(attribute(frame.node,*item));
        }
    }
    bool trigger(const SceneTraversal::Impl::Frame& frame) const {
        const auto& doc=frame.node->document();const auto& object=doc.object(frame.node->reference());
        const auto& value=field(doc,object,9,"igBoolMetaField",1);return doc.field_bytes(value)[0]!=0;
    }
    std::shared_ptr<const SceneSubmissionRequest> request(SceneTraversal::Impl& operation,
        const AnimationMatrix& view) const {
        const auto& frame=operation.frames.back();auto result=std::make_shared<SceneSubmissionRequest>();
        result->node=frame.node;result->model_view=view;result->attribute_list_reference=frame.attribute_list;
        result->attributes=frame.attributes;result->effective_attributes=effective();
        result->geometry_flags=frame.node->flags();
        for(const auto& path:operation.frames)if(path.node) {
            result->path.push_back(path.node);result->root_to_owner.push_back(path.node->reference());
        }
        return result;
    }
    AnimationMatrix geometry_view(const SceneTraversal::Impl::Frame& frame) const {
        auto list=scope.inherited_vertex_blend_list_present;
        // 801DDBFC tests stack count. A pushed list attribute takes this branch
        // even when an outer override selects some other payload owner.
        for(const auto& value:attributes)if(value->type()=="igVertexBlendMatrixListAttr" && value->unit()==0)list=true;
        if(!list)fail("Common geometry requires the inherited vertex-blend matrix-list stack count.");
        if(*list)fail("Common geometry requires the original nonempty vertex-blend matrix-list service.");
        auto enabled=scope.inherited_vertex_blend_enabled;
        for(const auto& value:effective())if(value->type()=="igVertexBlendStateAttr" && value->unit()==0) {
            const auto& object=value->document().object(value->reference());
            const auto& flag=field(value->document(),object,4,"igBoolMetaField",1);
            enabled=value->document().field_bytes(flag)[0]!=0;
        }
        if(!enabled)fail("Common geometry requires the effective inherited vertex-blend state.");
        if(!*enabled)return frame.view;
        if(!scope.bone_root_known)fail("Enabled vertex blending requires the actual TOP/F8 bone-array pointer.");
        return scope.bone_root ? *scope.bone_root : frame.view;
    }
    void finish(SceneTraversal::Impl& operation,std::int32_t value) {
        auto& frame=operation.frames.back();
        // Reverse source order models the original stack pops. Optional engine
        // pop callbacks belong to the supplied prepared attribute-manager scope.
        for(std::size_t i=0;i<frame.pushed;++i)attributes.pop_back();
        operation.frames.pop_back();
        if(operation.frames.empty()) {operation.done=true;operation.value=value;return;}
        auto& parent=operation.frames.back();
        if(value==1 || value==2) {parent.value=value==2 ? 2 : 0;parent.stage=Stage::leave;}
    }
};

SceneTraversal::SceneTraversal(std::shared_ptr<Impl> impl):impl_(std::move(impl)) {}
SceneTraversal::SceneTraversal(SceneTraversal&&) noexcept=default;
SceneTraversal& SceneTraversal::operator=(SceneTraversal&&) noexcept=default;
SceneTraversal::~SceneTraversal()=default;
bool SceneTraversal::completed() const noexcept {return impl_ && impl_->done;}
bool SceneTraversal::faulted() const noexcept {return !impl_ || impl_->failed;}
SceneTraversalContext::SceneTraversalContext(SceneGraphicsScope scope):impl_(std::make_shared<Impl>(std::move(scope))) {
    if(impl_->scope.inherited_attributes.size()>max_attributes)fail("Inherited scene attributes exceed their owner budget.");
    std::set<Key> keys;
    for(const auto& value:impl_->scope.inherited_attributes) {
        if(!value.attribute || !keys.insert(key(value.attribute)).second)fail("Inherited scene state requires one effective owner per type and unit.");
        impl_->owners.emplace(std::pair{&value.attribute->document(),value.attribute->reference()},value.attribute);
    }
    if(impl_->scope.bone_root)finite(*impl_->scope.bone_root);
    if(!impl_->scope.bone_root_known && impl_->scope.bone_root)fail("An inherited bone root must identify a known pointer state.");
}
SceneTraversalContext::~SceneTraversalContext()=default;
SceneTraversal SceneTraversalContext::begin(BoneSelectContext& bones,std::shared_ptr<const ActorBoneSelector> selector) {
    impl_->idle();
    if(!selector)fail("Common scene traversal requires the actual selector owner.");
    const auto selected=bones.model_view();if(!selected)fail("Common scene child entry requires the selector's active model-view owner.");
    finite(selected->matrix());auto operation=std::make_shared<SceneTraversal::Impl>();
    operation->owner=impl_;operation->generation=impl_->generation;operation->selector=std::move(selector);operation->selected_view=selected;
    SceneTraversal::Impl::Frame root;root.children=operation->selector->children();root.view=selected->matrix();root.stage=Stage::children;
    operation->frames.reserve(max_depth);operation->frames.push_back(std::move(root));impl_->current=operation;
    return SceneTraversal(std::move(operation));
}
SceneTraversalResult SceneTraversalContext::resume(SceneTraversal& traversal,const SceneTraversalServices& services) {
    if(!traversal.impl_ || traversal.impl_->owner.lock()!=impl_)fail("Common scene traversal requires the same live context owner.");
    auto state=impl_;auto operation=traversal.impl_;state->readable();
    if(operation->generation!=state->generation) {
        operation->failed=true;operation->done=false;operation->diagnostic="Explicit reset invalidated the common scene traversal.";
    }
    if(operation->failed)return {SceneTraversalStop::faulted,std::nullopt,operation->diagnostic};
    if(operation->done)return {SceneTraversalStop::completed,operation->value,{}};
    if(state->executing)fail("Common scene service cannot reenter its traversal or context.");
    if(state->current!=operation || state->failed)fail("Common scene traversal lost its retained context ownership.");
    struct Guard {Impl& state;explicit Guard(Impl& value):state(value){state.executing=true;}~Guard(){state.executing=false;}} guard(*state);
    const auto required=[](const char* text) {return SceneTraversalResult{SceneTraversalStop::service_required,std::nullopt,text};};
    try {
        while(!operation->done) {
            auto& frame=operation->frames.back();
            if(frame.stage==Stage::previsit) {
                if(state->scope.previsit_enabled) {
                    if(!frame.previsit) {
                        if(!services.previsit)return required("Common scene requires its enabled context previsit callback.");
                        frame.previsit=services.previsit;
                    }
                    if(!frame.request)frame.request=state->request(*operation,frame.view);
                    const auto result=frame.previsit(*frame.request);
                    if(!result)return required("Common scene previsit callback is incomplete.");
                    frame.request.reset();
                    if(*result!=0) {
                        if(*result==2 && frame.node->type()!="igNode")frame.stage=Stage::children;
                        else if(*result!=2)frame.stage=Stage::leave;
                        else frame.stage=Stage::enter;
                    } else frame.stage=Stage::enter;
                } else frame.stage=Stage::enter;
            } else if(frame.stage==Stage::enter) {
                const auto& type=frame.node->type();
                if(type=="igGroup")frame.stage=Stage::children;
                else if(type=="igNode")frame.stage=Stage::leave;
                else if(type=="igAttrSet") {
                    state->read_attributes(frame);
                    for(const auto& attribute:frame.attributes) {
                        const auto& attr_type=attribute->type();
                        if(attr_type!="igShadeModelAttr" && attr_type!="igTextureStateAttr" &&
                            attr_type!="igMaterialAttr" && attr_type!="igTextureBindAttr" &&
                            attr_type!="igVertexBlendStateAttr" && attr_type!="igVertexBlendMatrixListAttr")
                            fail("Normal common attribute requires its original stack/effect implementation.");
                    }
                    if(frame.attributes.size()>max_attributes-state->attributes.size())fail("Common scene attribute stack exceeds its entry budget.");
                    state->attributes.reserve(state->attributes.size()+frame.attributes.size());
                    for(const auto& attribute:frame.attributes) {state->attributes.push_back(attribute);++frame.pushed;}
                    frame.stage=state->trigger(frame) ? Stage::flush : Stage::children;
                } else if(type=="igTransform")frame.stage=Stage::transform;
                else if(type=="igGeometry") {
                    // Normal geometry does not push its own attrs or visit children.
                    if(!state->trigger(frame) || (frame.node->flags()&0x20u))frame.stage=Stage::leave;
                    else {state->read_attributes(frame);frame.stage=Stage::geometry;}
                } else frame.stage=Stage::external;
            } else if(frame.stage==Stage::transform) {
                    if(!state->scope.prepared_transform_clock) {
                        if(!frame.clock) {
                            if(!services.sample_transform_clock)return required("Common transform requires the actual context clock query.");
                            frame.clock=services.sample_transform_clock;
                        }
                        if(!frame.request)frame.request=state->request(*operation,frame.view);
                        if(!frame.clock(*frame.request))return required("The retained common transform clock query is incomplete.");
                        frame.request.reset();
                    }
                    const auto& doc=frame.node->document();const auto& object=doc.object(frame.node->reference());
                    field(doc,object,9,"igIntMetaField",4);field(doc,object,11,"igObjectRefMetaField",4);
                    if(doc.field_i32(object,9)!=0 || doc.field_reference(object,11))
                        fail("Common transform requires its original nonzero-target or input-update service.");
                    const auto bytes=doc.field_bytes(field(doc,object,8,"igMatrix44fMetaField",64));
                    AnimationMatrix local{};
                    for(std::size_t i=0;i<local.size();++i) {
                        std::uint32_t word{};for(unsigned b=0;b<4;++b)word|=std::uint32_t(bytes[i*4+b])<<(b*8);
                        local[i]=std::bit_cast<float>(word);
                    }
                    frame.view=original_matrix_multiply(frame.view,local);frame.stage=Stage::children;
            } else if(frame.stage==Stage::flush || frame.stage==Stage::geometry) {
                const bool geometry=frame.stage==Stage::geometry;
                auto& callback=geometry ? frame.submit : frame.flush;
                if(!callback) {
                    const auto& supplied=geometry ? services.submit_geometry : services.flush_attributes;
                    if(!supplied)return required(geometry ? "Common geometry requires its actual render submission service." :
                        "Triggered normal attribute set requires its changed-state-list service.");
                    callback=supplied;
                }
                if(!frame.request)frame.request=state->request(*operation,geometry ? state->geometry_view(frame) : frame.view);
                if(!callback(*frame.request))return required(geometry ? "Common geometry submission is incomplete." :
                    "Common changed-state-list submission is incomplete.");
                frame.request.reset();frame.stage=geometry ? Stage::leave : Stage::children;
            } else if(frame.stage==Stage::external) {
                if(!frame.handler) {
                    if(!services.node_handler)return required("Common scene requires this node's original registered handler.");
                    frame.handler=services.node_handler;
                }
                if(!frame.request)frame.request=state->request(*operation,frame.view);
                const auto result=frame.handler(*frame.request);
                if(!result)return required("The retained common scene node handler is incomplete.");
                frame.request.reset();state->finish(*operation,*result);
            } else if(frame.stage==Stage::children) {
                if(frame.cursor==frame.children.size()) {frame.stage=Stage::leave;continue;}
                if(operation->frames.size()>=max_depth || ++operation->visits>max_visits)
                    fail("Common scene traversal exceeds its path or occurrence budget.");
                SceneTraversal::Impl::Frame child;child.node=frame.children[frame.cursor++];
                child.children=child.node->children();child.view=frame.view;operation->frames.push_back(std::move(child));
            } else state->finish(*operation,frame.value==2 ? 2 : 0);
        }
        state->current.reset();return {SceneTraversalStop::completed,operation->value,{}};
    } catch(const std::exception& error) {
        operation->failed=state->failed=true;operation->diagnostic=error.what();
    } catch(...) {
        operation->failed=state->failed=true;operation->diagnostic="Common scene service threw an unknown exception.";
    }
    return {SceneTraversalStop::faulted,std::nullopt,operation->diagnostic};
}
std::size_t SceneTraversalContext::attribute_depth() const noexcept {return impl_->attributes.size();}
std::size_t SceneTraversalContext::node_depth() const noexcept {return impl_->current ? impl_->current->frames.size()-1 : 0;}
bool SceneTraversalContext::pending() const noexcept {return bool(impl_->current);}
bool SceneTraversalContext::faulted() const noexcept {return impl_->failed;}
std::optional<AnimationMatrix> SceneTraversalContext::model_view() const {
    impl_->readable();if(!impl_->current)return std::nullopt;
    const auto& frame=impl_->current->frames.back();
    if(frame.stage==Stage::geometry && frame.request)return frame.request->model_view;
    return frame.view;
}
void SceneTraversalContext::reset() {
    impl_->readable();if(impl_->executing)fail("Cannot reset common scene state from its executing service.");
    ++impl_->generation;impl_->releasing=true;impl_->current.reset();impl_->attributes.clear();impl_->releasing=false;impl_->failed=false;
}
} // namespace dw4
