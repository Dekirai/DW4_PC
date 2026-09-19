#include "dw4/actor_scene.hpp"
#include "dw4/model_scene.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace dw4 {
namespace {
constexpr std::size_t max_objects=65536,max_links=262144,max_depth=128,max_text=16u*1024u*1024u;
[[noreturn]] void fail(const char* reason) { throw ActorSceneError(reason); }
struct Reader {
    const IgbDocument& doc;
    const IgbObject& object(std::uint32_t ref,std::string_view type) const {
        const auto& result=doc.object(ref);
        if(doc.metadata.object_types.at(result.type_index).name!=type) fail("Unexpected actor scene object type.");
        std::set<std::uint16_t> slots;
        for(const auto& field:result.fields)
            if(!slots.insert(field.slot).second) fail("Actor scene object repeats a field slot.");
        return result;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const auto* value=doc.field(object,slot);
        if(!value || doc.metadata.field_types.at(value->type_index).name!=type ||
            (width && value->size!=*width)) fail("Unexpected actor scene field type or width.");
        doc.field_bytes(*value);return *value;
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");
        const auto value=doc.field_reference(object,slot);
        if(!value) fail("Actor scene requires its actual authored object owner.");
        doc.object(*value);return *value;
    }
    std::vector<std::uint32_t> list(std::uint32_t ref,std::string_view type) const {
        const auto& value=object(ref,type);
        field(value,2,"igIntMetaField");field(value,3,"igIntMetaField");field(value,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if(count>capacity || capacity>max_objects) fail("Actor scene list exceeds its bounded capacity.");
        const auto memory=doc.field_reference(value,4);
        if((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("Actor scene list memory differs from its declared capacity.");
        std::vector<std::uint32_t> result;result.reserve(count);
        for(const auto item:doc.object_list(ref)) {
            if(!item) fail("Actor scene list contains a null object.");
            result.push_back(*item);
        }
        return result;
    }
    template<std::size_t N> std::array<float,N> floats(const IgbObject& value,std::uint16_t slot,std::string_view type) const {
        const auto bytes=doc.field_bytes(field(value,slot,type,N*4));
        std::array<float,N> result{};
        for(std::size_t i=0;i<N;++i) {
            std::uint32_t word{};for(unsigned j=0;j<4;++j)word|=std::uint32_t(bytes[i*4+j])<<(j*8);
            result[i]=std::bit_cast<float>(word);
        }
        return result;
    }
};
}
struct ActorSceneSource {
    IgbDocument document;
    std::string source;
    std::uint32_t occurrence{};
};
struct ActorSceneResource::Impl {
    std::shared_ptr<const ActorSceneSource> source;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneNode>> nodes;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneNodeList>> node_lists;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneNodeStorage>> node_memories;
    std::map<std::uint32_t,std::shared_ptr<CollisionBox>> boxes;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneBitMask>> masks;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneBitMask::Storage>> mask_memories;
    std::unique_ptr<ModelSceneObjects> models;
    std::map<std::uint32_t,std::shared_ptr<const ActorSceneSkeleton>> skeletons;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneAppearance>> appearances;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneAppearanceList>> lists;
    std::map<std::uint32_t,std::shared_ptr<ActorBoneSelectorList>> selector_lists;
    std::size_t links{},text{};
    std::size_t active_node_visits{};
    std::size_t owners() const noexcept {
        return nodes.size()+node_lists.size()+node_memories.size()+boxes.size()+masks.size()+mask_memories.size()+skeletons.size()+appearances.size()+lists.size()+selector_lists.size();
    }
    void require(std::size_t objects,std::size_t entries=0,std::size_t bytes=0) const {
        if(objects>max_objects-owners())fail("Actor scene exceeds its aggregate owner budget.");
        if(entries>max_links-links)fail("Actor scene exceeds its aggregate copied-entry budget.");
        if(bytes>max_text-text)fail("Actor scene exceeds its aggregate copied-text budget.");
    }
};
const IgbDocument& ActorSceneNode::document() const noexcept { return source_->document; }
bool ActorSceneNode::is_a(std::string_view type) const {
    return document().is_a(document().object(reference_),type);
}
std::shared_ptr<ModelSceneList> ActorSceneNode::attribute_list_owner() const {
    if(!attributes_available_)fail("This actor scene node has no represented attribute-list field.");
    return attributes_;
}
void ActorSceneNode::set_attribute_list_owner(std::shared_ptr<ModelSceneList> owner) {
    if(!attributes_available_)fail("This actor scene node has no represented attribute-list field.");
    attributes_=std::move(owner);
}
std::uint8_t ActorSceneNode::attributes_enabled() const {
    if(!attributes_available_)fail("This actor scene node has no represented attribute-enable field.");
    return attributes_enabled_;
}
void ActorSceneNode::set_attributes_enabled(std::uint8_t value) {
    if(!attributes_available_)fail("This actor scene node has no represented attribute-enable field.");
    attributes_enabled_=value;
}
std::shared_ptr<const IgbDocument> ActorSceneBitMask::document_owner() const noexcept {
    return source_ ? std::shared_ptr<const IgbDocument>(source_,&source_->document) : nullptr;
}
std::uint32_t ActorSceneBitMask::backing_word(std::size_t index) const {
    if(!backing || index>=backing->size())fail("Switch mask requires its reached physical word.");
    return (*backing)[index];
}
const std::string& ActorSceneNode::source() const noexcept { return source_->source; }
std::uint32_t ActorSceneNode::source_occurrence() const noexcept { return source_->occurrence; }
std::span<const std::shared_ptr<const ActorSceneNode>> ActorSceneNode::children() const {
    return children_ ? children_->values() : std::span<const std::shared_ptr<const ActorSceneNode>>{};
}
std::shared_ptr<AnimationMatrix> ActorSceneNode::matrix_owner() {
    if(!matrix_)fail("This actor scene node has no represented transform matrix field.");return matrix_;
}
std::shared_ptr<const AnimationMatrix> ActorSceneNode::matrix_owner() const {
    if(!matrix_)fail("This actor scene node has no represented transform matrix field.");return matrix_;
}
std::shared_ptr<CollisionBox> ActorSceneNode::box_owner() {
    if(!bound_available_)fail("This actor scene node has no represented bound field.");return box_;
}
std::shared_ptr<const CollisionBox> ActorSceneNode::box_owner() const {
    if(!bound_available_)fail("This actor scene node has no represented bound field.");return box_;
}
void ActorSceneNode::set_box_owner(std::shared_ptr<CollisionBox> value) {
    if(!bound_available_)fail("This actor scene node has no represented bound field.");box_=std::move(value);
}
std::shared_ptr<ActorSceneBitMask> ActorSceneNode::switch_mask_owner() {
    if(!mask_available_)fail("This actor scene node has no represented switch mask field.");return mask_;
}
std::shared_ptr<const ActorSceneBitMask> ActorSceneNode::switch_mask_owner() const {
    if(!mask_available_)fail("This actor scene node has no represented switch mask field.");return mask_;
}
void ActorSceneNode::set_switch_mask_owner(std::shared_ptr<ActorSceneBitMask> value) {
    if(!mask_available_)fail("This actor scene node has no represented switch mask field.");mask_=std::move(value);
}
std::shared_ptr<ActorSceneNode> ActorSceneNode::source_transform_owner() {
    if(!source_transform_available_)fail("This actor scene node has no represented source-transform field.");return source_transform_;
}
std::shared_ptr<const ActorSceneNode> ActorSceneNode::source_transform_owner() const {
    if(!source_transform_available_)fail("This actor scene node has no represented source-transform field.");return source_transform_;
}
std::shared_ptr<ModelSceneObject> ActorSceneNode::transform_input_owner() const {
    if(!transform_input_available_)fail("This actor scene node has no represented transform-input field.");
    return transform_input_;
}
void ActorSceneNode::set_transform_input_owner(std::shared_ptr<ModelSceneObject> value) {
    if(!transform_input_available_)fail("This actor scene node has no represented transform-input field.");
    if(value && !value->is_a("igTransformSource"))fail("Actor transform input owner is not an igTransformSource.");
    transform_input_=std::move(value);
}
void ActorSceneNode::set_source_transform_owner(std::shared_ptr<ActorSceneNode> value) {
    if(!source_transform_available_)fail("This actor scene node has no represented source-transform field.");
    if(value==source_transform_)return;
    const auto self=shared_from_this();
    ActorSceneNodeList::validate(nullptr,nullptr,nullptr,{}, {self},this,value);
    if(value) {
        std::erase_if(value->incoming_sources_,[](const auto& owner){return owner.expired();});
        if(value->incoming_sources_.size()>=max_objects)fail("Actor scene source exceeds its incoming-link budget.");
        value->incoming_sources_.push_back(self);
    }
    if(source_transform_)std::erase_if(source_transform_->incoming_sources_,
        [&](const auto& owner){const auto node=owner.lock();return !node || node.get()==this;});
    source_transform_=std::move(value);
}
std::shared_ptr<ActorSceneNodeList> ActorSceneNode::child_list_owner() {
    if(!children_available_)fail("This actor scene node has no represented child-list field.");return children_;
}
std::shared_ptr<const ActorSceneNodeList> ActorSceneNode::child_list_owner() const {
    if(!children_available_)fail("This actor scene node has no represented child-list field.");return children_;
}
std::span<const std::shared_ptr<const ActorSceneNode>> ActorSceneNodeList::values() const {
    if(count_<0 || capacity_<0 || count_>capacity_ || std::size_t(capacity_)>max_objects ||
        (capacity_ && !storage_) || (storage_ && std::size_t(capacity_)>storage_->values_.size()))
        fail("Actor scene child list has an unsupported logical count, capacity or backing range.");
    if(!count_)return {};
    return {storage_->values_.data(),static_cast<std::size_t>(count_)};
}
std::shared_ptr<const ActorSceneNode> ActorSceneNodeList::first_backing_node() const {
    // 8030DA7C..8030DA94 dereferences storage[0], without inspecting count.
    if(!storage_ || storage_->values_.empty())fail("Actor scene child list requires its addressable first backing slot.");
    return storage_->values_.front();
}
std::shared_ptr<ActorSceneNode> ActorSceneNodeList::backing_node(std::size_t index) const {
    if(!storage_ || index>=storage_->values_.size())fail("Actor scene child list requires its reached physical backing slot.");
    return std::const_pointer_cast<ActorSceneNode>(storage_->values_[index]);
}
void ActorSceneNodeList::validate(const ActorSceneNode* changed_node,const ActorSceneNodeList* changed_list,
    const ActorSceneNodeStorage* changed_storage,const Values& candidate,
    const std::vector<std::shared_ptr<const ActorSceneNode>>& initial_roots,
    const ActorSceneNode* changed_source,const std::shared_ptr<ActorSceneNode>& source_value) {
    if(candidate.size()>max_objects)fail("Actor scene child backing exceeds its allocation budget.");
    auto roots=initial_roots;
    std::set<const ActorSceneNode*> ancestors;
    std::size_t incoming_edges{};
    for(std::size_t index=0;index<roots.size();++index) {
        const auto node=roots[index];if(!ancestors.insert(node.get()).second)continue;
        if(ancestors.size()>max_objects)fail("Actor scene mutation exceeds its ancestor budget.");
        for(const auto& weak_source:node->incoming_sources_)if(auto parent=weak_source.lock()) {
            if(++incoming_edges>max_links || roots.size()>=max_links)fail("Actor scene mutation exceeds its source-parent budget.");
            roots.push_back(std::move(parent));
        }
        for(const auto& weak_storage:node->incoming_)if(const auto storage=weak_storage.lock()) {
            if(++incoming_edges>max_links)fail("Actor scene mutation exceeds its parent-link budget.");
            for(const auto& weak_list:storage->lists_)if(const auto list=weak_list.lock())
                for(const auto& weak_parent:list->holders_)if(auto parent=weak_parent.lock()) {
                    if(roots.size()>=max_links)fail("Actor scene mutation exceeds its retained-parent budget.");
                    roots.push_back(std::move(parent));
                }
        }
    }
    std::map<const ActorSceneNode*,std::size_t> heights;
    std::set<const ActorSceneNode*> visiting;
    std::size_t edges{};
    std::function<std::size_t(const ActorSceneNode*,std::size_t)> visit;
    visit=[&](const ActorSceneNode* node,std::size_t depth)->std::size_t {
        if(depth>max_depth || visiting.contains(node))fail("Actor scene child mutation creates a cycle or exceeds its path budget.");
        if(const auto it=heights.find(node);it!=heights.end()) {
            if(it->second>max_depth-depth)fail("Actor scene cached child path exceeds its depth budget.");return it->second;
        }
        if(heights.size()+visiting.size()>=max_objects)fail("Actor scene mutation exceeds its reachable-node budget.");
        visiting.insert(node);std::size_t height{};
        static const Values empty;
        const auto list=node->children_;
        const auto& values=node==changed_node || (changed_list && list.get()==changed_list) ||
            (changed_storage && list && list->storage_.get()==changed_storage) ? candidate :
            list && list->storage_ ? list->storage_->values_ : empty;
        if(values.size()>max_links-edges)fail("Actor scene mutation exceeds its reachable-edge budget.");edges+=values.size();
        for(const auto& child:values)if(child)height=std::max(height,visit(child.get(),depth+1)+1);
        const auto& source=node==changed_source ? source_value : node->source_transform_;
        if(source) {
            if(++edges>max_links)fail("Actor scene source graph exceeds its reachable-edge budget.");
            height=std::max(height,visit(source.get(),depth+1)+1);
        }
        visiting.erase(node);heights.emplace(node,height);return height;
    };
    for(const auto& root:roots)visit(root.get(),0);
    for(const auto& child:candidate)if(child)visit(child.get(),0);
}
void ActorSceneNodeList::attach(const std::shared_ptr<ActorSceneNode>& node) {
    std::erase_if(holders_,[](const auto& weak){return weak.expired();});
    if(std::none_of(holders_.begin(),holders_.end(),[&](const auto& weak){return weak.lock()==node;})) {
        if(holders_.size()>=max_objects)fail("Actor scene child list exceeds its attached-parent budget.");holders_.push_back(node);
    }
}
void ActorSceneNodeList::detach(const ActorSceneNode* node) {
    std::erase_if(holders_,[&](const auto& weak){const auto owner=weak.lock();return !owner || owner.get()==node;});
}
void ActorSceneNode::set_child_list_owner(std::shared_ptr<ActorSceneNodeList> value) {
    if(!children_available_)fail("This actor scene node has no represented child-list field.");
    if(value==children_)return;
    static const ActorSceneNodeList::Values empty;
    auto self=shared_from_this();
    ActorSceneNodeList::validate(this,nullptr,nullptr,value && value->storage_ ? value->storage_->values() : empty,{self});
    if(value)value->attach(self);
    if(children_)children_->detach(this);
    children_=std::move(value);
}
void ActorSceneNodeList::bind_storage(std::shared_ptr<ActorSceneNodeStorage> storage) {
    const auto self=weak_from_this().lock();
    if(!self)fail("Actor scene child list mutation requires its shared list owner.");
    if(storage==storage_)return;
    if(storage) {
        std::erase_if(storage->lists_,[](const auto& weak){return weak.expired();});
        if(storage->lists_.size()>=max_objects)fail("Actor scene backing exceeds its aliased-list budget.");
        storage->lists_.push_back(self);
    }
    if(storage_)std::erase_if(storage_->lists_,[&](const auto& weak){const auto list=weak.lock();return !list || list.get()==this;});
    storage_=std::move(storage);
}
void ActorSceneNodeList::write_storage(const std::shared_ptr<ActorSceneNodeStorage>& storage,Values values) {
    if(!storage->values_.empty() && storage->values_.size()!=values.size())
        fail("Actor scene backing growth must allocate a new backing owner.");
    // Weak incoming links cover every strong backing reference, regardless of count.
    for(const auto& node:values)if(node) {
        std::erase_if(node->incoming_,[](const auto& weak){return weak.expired();});
        if(node->incoming_.size()>=max_objects && std::none_of(node->incoming_.begin(),node->incoming_.end(),
            [&](const auto& weak){return weak.lock()==storage;}))fail("Actor scene node exceeds its incoming-backing budget.");
        node->incoming_.reserve(node->incoming_.size()+1);
    }
    for(const auto& node:storage->values_)if(node)std::erase_if(node->incoming_,[&](const auto& weak){const auto owner=weak.lock();return !owner || owner==storage;});
    for(const auto& node:values)if(node && std::none_of(node->incoming_.begin(),node->incoming_.end(),
        [&](const auto& weak){return weak.lock()==storage;}))node->incoming_.push_back(storage);
    // Existing capacity is fixed for this backing object, so retained spans and
    // original storage/end captures keep their addressed slots during writes.
    if(storage->values_.size()==values.size())std::copy(values.begin(),values.end(),storage->values_.begin());
    else if(storage->values_.empty())storage->values_=std::move(values);
    else fail("Actor scene backing growth must allocate a new backing owner.");
}
void ActorSceneNodeList::publish(Values values,std::int32_t count) {
    this->values();
    const auto self=weak_from_this().lock();if(!self)fail("Actor scene child list mutation requires its shared list owner.");
    const bool growing=count>capacity_;
    auto capacity=capacity_;
    if(growing) {
        // 80069128 -> 80041970 -> 80041660/8004155C: growth replaces only
        // this list's backing and copies its old capacity slots.
        capacity=std::max<std::int32_t>(capacity,4);
        while(capacity<count)capacity=capacity<1024 ? capacity*2 : capacity+1024;
        if(std::size_t(capacity)>max_objects)fail("Actor scene child growth exceeds its allocation budget.");
    }
    Values candidate=!growing && storage_ ? storage_->values_ : Values(static_cast<std::size_t>(capacity));
    if(growing && storage_)std::copy_n(storage_->values_.begin(),capacity_,candidate.begin());
    std::copy(values.begin(),values.end(),candidate.begin());
    std::vector<std::shared_ptr<const ActorSceneNode>> roots;
    const auto add_roots=[&](const ActorSceneNodeList& list) {
        for(const auto& weak:list.holders_)if(auto owner=weak.lock()) {
            if(roots.size()>=max_links)fail("Actor scene mutation exceeds its retained-parent budget.");roots.push_back(std::move(owner));
        }
    };
    if(!growing && storage_) {
        for(const auto& weak:storage_->lists_)if(const auto list=weak.lock())add_roots(*list);
    } else add_roots(*this);
    validate(nullptr,growing ? this : nullptr,growing ? nullptr : storage_.get(),candidate,roots);
    if(growing) {
        auto replacement=std::make_shared<ActorSceneNodeStorage>();write_storage(replacement,std::move(candidate));
        bind_storage(std::move(replacement));capacity_=capacity;
    } else if(storage_)write_storage(storage_,std::move(candidate));
    count_=count;
}
void ActorSceneNodeList::replace(Values values) {
    if(values.size()>max_objects || std::any_of(values.begin(),values.end(),[](const auto& node){return !node;}))
        fail("Actor scene child replacement requires bounded non-null active entries.");
    const auto count=static_cast<std::int32_t>(values.size());publish(std::move(values),count);
}
void ActorSceneNodeList::append(std::shared_ptr<const ActorSceneNode> value) {
    const auto active=values();
    if(active.size()>=max_objects || !value)fail("Actor scene child append requires a bounded non-null node.");
    Values replacement(active.begin(),active.end());replacement.push_back(std::move(value));replace(std::move(replacement));
}
std::shared_ptr<ModelPlacementNode> ActorSceneNode::placement_node() {
    if(type_!="beModelCtrlNode2")fail("This actor scene node requires its original concrete placement setter service.");
    return make_model_ctrl_node2_placement(matrix_owner());
}
ActorBoneSelector::ActorBoneSelector(std::int32_t index,std::vector<std::shared_ptr<const ActorSceneNode>> children)
    :matrix_index_(index),children_(std::move(children)) {
    if(children_.size()>max_objects || std::any_of(children_.begin(),children_.end(),[](const auto& node){return !node;}))
        fail("Bone selector requires bounded non-null child owners.");
}
ActorSceneSkeleton::ActorSceneSkeleton(std::shared_ptr<const ActorSceneSource> source,std::uint32_t ref,
    AnimationHierarchyDefinition hierarchy):source_(std::move(source)),reference_(ref),hierarchy_(std::move(hierarchy)) {}
const std::string& ActorSceneSkeleton::source() const noexcept { return source_->source; }
std::uint32_t ActorSceneSkeleton::source_occurrence() const noexcept { return source_->occurrence; }
std::int32_t ActorSceneSkeleton::find_bone(std::string_view name) const {
    if(name.size()>4096 || name.find('\0')!=std::string_view::npos) fail("Bone lookup name exceeds its bounded byte profile.");
    const auto& bones=hierarchy_.bones();
    // 8020323C -> 80077768: first match through the fixed ASCII uppercase table.
    const auto uppercase=[](unsigned char value) { return value>='a' && value<='z' ? value-('a'-'A') : value; };
    for(std::size_t i=0;i<bones.size();++i)
        if(bones[i].name.size()==name.size() && std::equal(bones[i].name.begin(),bones[i].name.end(),name.begin(),
            [&](unsigned char a,unsigned char b){return uppercase(a)==uppercase(b);})) return static_cast<std::int32_t>(i);
    return -1;
}
void ActorBoneSelectorList::append(std::shared_ptr<const ActorBoneSelector> selector) {
    if(!selector || values_.size()>=max_objects) fail("Bone selector list requires bounded non-null owners.");
    values_.push_back(std::move(selector));
}
std::size_t ActorBoneSelectorList::remove(const std::shared_ptr<const ActorBoneSelector>& selector) {
    const auto previous=values_.size();std::erase(values_,selector);return previous-values_.size();
}
const std::string& ActorSceneAppearance::source() const noexcept { return source_->source; }
std::uint32_t ActorSceneAppearance::source_occurrence() const noexcept { return source_->occurrence; }
const std::string& ActorSceneAppearanceList::source() const noexcept { return source_->source; }
std::uint32_t ActorSceneAppearanceList::source_occurrence() const noexcept { return source_->occurrence; }
bool ActorSceneAppearanceList::shares_source(const ActorSceneSkeleton& skeleton) const noexcept { return source_==skeleton.source_; }
ActorSceneResource::ActorSceneResource(IgbDocument document,std::string source,std::uint32_t occurrence)
    :impl_(std::make_unique<Impl>()) {
    if(source.empty() || source.size()>4096 || source.find('\0')!=std::string::npos ||
        occurrence==std::numeric_limits<std::uint32_t>::max()) fail("Actor scene requires a qualified loaded source.");
    if(document.metadata.header.byte_order!=IgbByteOrder::little) fail("Big-endian actor scene serialization is unverified.");
    impl_->source=std::make_shared<const ActorSceneSource>(ActorSceneSource{std::move(document),std::move(source),occurrence});
    impl_->text=impl_->source->source.size();
}
ActorSceneResource::~ActorSceneResource()=default;
const IgbDocument& ActorSceneResource::document() const noexcept { return impl_->source->document; }
std::shared_ptr<const IgbDocument> ActorSceneResource::document_owner() const noexcept {
    return {impl_->source,&impl_->source->document};
}
ModelSceneObjects& ActorSceneResource::models() {
    if(!impl_->models)impl_->models.reset(new ModelSceneObjects(*this));return *impl_->models;
}
const std::string& ActorSceneResource::source() const noexcept { return impl_->source->source; }
std::uint32_t ActorSceneResource::source_occurrence() const noexcept { return impl_->source->occurrence; }
std::uint32_t ActorSceneResource::canonical_reference(std::uint32_t ref) const {
    try {
        const auto& value=document().object(ref);
        if(&document().object(value.reference_index)!=&value) fail("Actor scene canonical reference does not identify its physical object.");
        return value.reference_index;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
std::shared_ptr<const ActorSceneNode> ActorSceneResource::node(std::uint32_t reference) {
    return mutable_node(reference);
}
std::shared_ptr<CollisionBox> ActorSceneResource::box(std::uint32_t reference) {
    try {
        reference=canonical_reference(reference);
        if(const auto it=impl_->boxes.find(reference);it!=impl_->boxes.end())return it->second;
        impl_->require(1);Reader read{document()};const auto& object=read.object(reference,"igAABox");
        auto owner=std::make_shared<CollisionBox>(CollisionBox{read.floats<3>(object,2,"igVec3fMetaField"),
            read.floats<3>(object,3,"igVec3fMetaField")});
        impl_->boxes.emplace(reference,owner);return owner;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
std::shared_ptr<ActorSceneBitMask> ActorSceneResource::bit_mask(std::uint32_t reference) {
    try {
        reference=canonical_reference(reference);
        if(const auto found=impl_->masks.find(reference);found!=impl_->masks.end())return found->second;
        impl_->require(1);Reader read{document()};const auto& object=read.object(reference,"igBitMask");
        read.field(object,2,"igIntMetaField");read.field(object,3,"igIntMetaField");
        read.field(object,4,"igMemoryRefMetaField");read.field(object,5,"igUnsignedIntMetaField");
        auto owner=std::make_shared<ActorSceneBitMask>();owner->source_=impl_->source;owner->reference_=reference;
        owner->count=document().field_i32(object,2);owner->capacity=document().field_i32(object,3);
        owner->bit_count=document().field_u32(object,5);
        std::optional<std::uint32_t> added_memory;
        if(const auto ref=document().field_reference(object,4)) {
            if(*ref>=document().references.size() || document().references[*ref].kind!=IgbReferenceKind::memory)
                fail("Switch mask backing is not a memory reference.");
            const auto& record=document().memories.at(document().references[*ref].value_index);
            const auto canonical=record.reference_index;
            if(canonical>=document().references.size() || document().references[canonical].kind!=IgbReferenceKind::memory ||
                document().references[canonical].value_index!=document().references[*ref].value_index)
                fail("Switch mask canonical memory does not identify its physical allocation.");
            if(const auto found=impl_->mask_memories.find(canonical);found!=impl_->mask_memories.end())owner->backing=found->second;
            else {
                const auto bytes=document().memory(*ref);
                if(bytes.size()%4 || bytes.size()/4>max_objects)fail("Switch mask backing exceeds its word allocation profile.");
                impl_->require(2,bytes.size()/4);
                owner->backing=std::make_shared<ActorSceneBitMask::Storage>();owner->backing->reserve(bytes.size()/4);
                for(std::size_t offset=0;offset<bytes.size();offset+=4) {
                    std::uint32_t word{};for(unsigned byte=0;byte<4;++byte)word|=std::uint32_t(bytes[offset+byte])<<(byte*8);
                    owner->backing->push_back(word);
                }
                added_memory=canonical;
            }
        }
        // Logical mask fields do not validate or truncate the physical words.
        // 80300CA0/DC4 reads backing[index/32] directly at the reached child.
        impl_->require(added_memory ? 2 : 1,added_memory ? owner->backing->size() : 0);
        if(added_memory) {
            impl_->mask_memories.emplace(*added_memory,owner->backing);impl_->links+=owner->backing->size();
        }
        impl_->masks.emplace(reference,owner);return owner;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
std::shared_ptr<ActorSceneNode> ActorSceneResource::mutable_node(std::uint32_t reference) {
    try {
        Reader read{document()};std::set<std::uint32_t> visiting,visiting_lists,visiting_memory;
        std::map<std::uint32_t,std::shared_ptr<ActorSceneNode>> added;
        std::map<std::uint32_t,std::shared_ptr<ActorSceneNodeList>> added_lists;
        std::map<std::uint32_t,std::shared_ptr<ActorSceneNodeStorage>> added_memory;
        std::map<std::uint32_t,std::shared_ptr<CollisionBox>> added_boxes;
        std::size_t links{},copied_text{};
        const auto require=[&](std::size_t extra=0) {
            impl_->require(added.size()+visiting.size()+added_lists.size()+added_memory.size()+visiting_memory.size()+added_boxes.size()+extra,links,copied_text);
        };
        const auto box=[&](std::uint32_t ref) {
            ref=canonical_reference(ref);
            if(const auto it=impl_->boxes.find(ref);it!=impl_->boxes.end())return it->second;
            if(const auto it=added_boxes.find(ref);it!=added_boxes.end())return it->second;
            require(1);const auto& object=read.object(ref,"igAABox");
            auto owner=std::make_shared<CollisionBox>(CollisionBox{read.floats<3>(object,2,"igVec3fMetaField"),
                read.floats<3>(object,3,"igVec3fMetaField")});
            added_boxes.emplace(ref,owner);return owner;
        };
        std::function<std::shared_ptr<ActorSceneNode>(std::uint32_t,std::size_t)> visit;
        std::function<std::shared_ptr<ActorSceneNodeList>(std::uint32_t,std::size_t)> child_list;
        std::function<std::shared_ptr<ActorSceneNodeStorage>(std::uint32_t,std::size_t)> memory;
        memory=[&](std::uint32_t ref,std::size_t depth) {
            if(ref>=document().references.size() || document().references[ref].kind!=IgbReferenceKind::memory)
                fail("Actor scene child backing is not a memory reference.");
            const auto& record=document().memories.at(document().references[ref].value_index);
            const auto canonical=record.reference_index;
            if(canonical>=document().references.size() || document().references[canonical].kind!=IgbReferenceKind::memory ||
                document().references[canonical].value_index!=document().references[ref].value_index)
                fail("Actor scene canonical memory does not identify its physical allocation.");
            if(visiting_memory.contains(canonical))fail("Actor scene physical backing graph contains a strong cycle.");
            if(const auto it=impl_->node_memories.find(canonical);it!=impl_->node_memories.end())return it->second;
            if(const auto it=added_memory.find(canonical);it!=added_memory.end())return it->second;
            const auto bytes=document().memory(ref);
            if(bytes.size()%4 || bytes.size()/4>max_objects)fail("Actor scene backing exceeds its pointer allocation profile.");
            const auto capacity=bytes.size()/4;
            if(capacity>max_links-links)fail("Actor scene backing exceeds its aggregate entry budget.");
            links+=capacity;require(1);visiting_memory.insert(canonical);
            auto owner=std::make_shared<ActorSceneNodeStorage>();ActorSceneNodeStorage::Values values;values.reserve(capacity);
            for(std::size_t i=0;i<capacity;++i) {
                std::uint32_t item{};for(unsigned byte=0;byte<4;++byte)item|=std::uint32_t(bytes[i*4+byte])<<(byte*8);
                values.push_back(item==0xffffffffu ? std::shared_ptr<const ActorSceneNode>{} : visit(item,depth));
            }
            ActorSceneNodeList::write_storage(owner,std::move(values));visiting_memory.erase(canonical);
            added_memory.emplace(canonical,owner);return owner;
        };
        child_list=[&](std::uint32_t ref,std::size_t depth) {
            ref=canonical_reference(ref);
            if(visiting_lists.contains(ref))fail("Actor scene child-list graph contains a cycle.");
            if(const auto it=impl_->node_lists.find(ref);it!=impl_->node_lists.end())return it->second;
            if(const auto it=added_lists.find(ref);it!=added_lists.end())return it->second;
            require(1);auto owner=std::make_shared<ActorSceneNodeList>();added_lists.emplace(ref,owner);
            const auto& object=read.object(ref,"igNodeList");
            read.field(object,2,"igIntMetaField");read.field(object,3,"igIntMetaField");read.field(object,4,"igMemoryRefMetaField");
            owner->count_=document().field_i32(object,2);owner->capacity_=document().field_i32(object,3);
            visiting_lists.insert(ref);
            if(const auto storage=document().field_reference(object,4))owner->bind_storage(memory(*storage,depth));
            visiting_lists.erase(ref);return owner;
        };
        visit=[&](std::uint32_t ref,std::size_t depth)->std::shared_ptr<ActorSceneNode> {
            ref=canonical_reference(ref);
            // A transform input can enter the model registry, which can call
            // mutable_node again. The local depth resets at that public entry;
            // retain the active path budget across those registry boundaries.
            // Preserve the existing root-at-zero limit of 128 edges.
            if(impl_->active_node_visits>max_depth)fail("Actor scene nested node loading exceeds its depth budget.");
            struct VisitGuard {
                std::size_t& active;
                explicit VisitGuard(std::size_t& value):active(value){++active;}
                ~VisitGuard(){--active;}
            } active_visit(impl_->active_node_visits);
            if(visiting.contains(ref))fail("Actor scene node graph contains a cycle.");
            if(depth>max_depth)fail("Actor scene node graph exceeds its depth budget.");
            if(const auto found=impl_->nodes.find(ref);found!=impl_->nodes.end())return found->second;
            if(const auto found=added.find(ref);found!=added.end())return found->second;
            require(1);const auto& value=document().object(ref);
            if(!document().is_a(value,"igNode"))fail("Actor scene child is not a node.");
            const auto& type=document().metadata.object_types.at(value.type_index).name;read.object(ref,type);
            const auto& name=read.field(value,2,"igStringMetaField",{});
            if(name.size>4104)fail("Actor scene node name exceeds its byte budget.");
            const auto text=document().field_string(value,2);
            if(text.size()>4096 || text.find('\0')!=std::string_view::npos)fail("Actor scene node name is invalid.");
            const auto bytes=text.size()+type.size();
            if(bytes>max_text-copied_text)fail("Actor scene node text exceeds its aggregate byte budget.");
            copied_text+=bytes;require(1);read.field(value,5,"igIntMetaField");
            auto result=std::shared_ptr<ActorSceneNode>(new ActorSceneNode);
            result->source_=impl_->source;result->reference_=ref;result->type_=type;
            result->name_=name.size==4 ? std::optional<std::string>{} : std::string(text);
            result->flags_=document().field_u32(value,5);visiting.insert(ref);
            if(document().is_a(value,"igTransform") || type=="beModelCtrlNode2")
                result->matrix_=std::make_shared<AnimationMatrix>(read.floats<16>(value,8,"igMatrix44fMetaField"));
            if(document().is_a(value,"igTransform") && document().field(value,11)) {
                read.field(value,11,"igObjectRefMetaField");result->transform_input_available_=true;
                // Use the public canonical registry entry point, as for node
                // attributes. Its nested checkpoint retains completed owners
                // that escape through this independently cached node graph.
                if(const auto input=document().field_reference(value,11)) {
                    // 801AC968 binds this object-reference field to the
                    // igTransformSource metadata class via 801AB2B4. Reject
                    // unrelated owners before their concrete loader can
                    // recursively enter the node registry.
                    if(!document().is_a(document().object(*input),"igTransformSource"))
                        fail("Actor transform input owner is not an igTransformSource.");
                    result->transform_input_=models().object(*input);
                }
            }
            if(document().field(value,3)) {
                read.field(value,3,"igObjectRefMetaField");result->bound_available_=true;
                if(const auto ref_box=document().field_reference(value,3))result->box_=box(*ref_box);
            }
            if(document().is_a(value,"igGroup")) {
                read.field(value,7,"igObjectRefMetaField");result->children_available_=true;
                if(const auto ref_list=document().field_reference(value,7)) {
                    result->children_=child_list(*ref_list,depth+1);result->children_->attach(result);
                }
            }
            if(document().is_a(value,"igAttrSet")) {
                read.field(value,8,"igObjectRefMetaField");
                const auto& enabled=read.field(value,9,"igBoolMetaField",1);
                result->attributes_available_=true;result->attributes_enabled_=document().field_bytes(enabled)[0];
                if(const auto attributes=document().field_reference(value,8))result->attributes_=models().list(*attributes);
            }
            if(type=="igSwitch") {
                read.field(value,8,"igObjectRefMetaField");result->mask_available_=true;
                if(const auto mask=document().field_reference(value,8))result->mask_=bit_mask(*mask);
            }
            if(type=="beCopyTransform") {
                read.field(value,12,"igObjectRefMetaField");result->source_transform_available_=true;
                if(const auto source=document().field_reference(value,12)) {
                    result->source_transform_=visit(*source,depth+1);
                    auto& incoming=result->source_transform_->incoming_sources_;
                    std::erase_if(incoming,[](const auto& owner){return owner.expired();});
                    if(incoming.size()>=max_objects)fail("Actor scene source exceeds its incoming-link budget.");
                    incoming.push_back(result);
                }
            }
            visiting.erase(ref);added.emplace(ref,result);return result;
        };
        auto result=visit(reference,0);
        // Mask owners are loaded through the shared resource registry during
        // this node transaction. Include them before publishing pending nodes.
        require();
        ActorSceneNodeList::validate(nullptr,nullptr,nullptr,{}, {result});
        impl_->nodes.merge(added);impl_->node_lists.merge(added_lists);impl_->node_memories.merge(added_memory);impl_->boxes.merge(added_boxes);
        impl_->links+=links;impl_->text+=copied_text;return result;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
std::shared_ptr<const ActorSceneSkeleton> ActorSceneResource::skeleton(std::uint32_t reference) {
    try {
        reference=canonical_reference(reference);
        if(const auto found=impl_->skeletons.find(reference);found!=impl_->skeletons.end()) return found->second;
        impl_->require(1);
        auto hierarchy=decode_animation_hierarchy(document(),reference);
        const auto entries=hierarchy.bones().size()+hierarchy.inverse_joint_matrices().size();
        std::size_t bytes{};for(const auto& bone:hierarchy.bones())bytes+=bone.name.size();
        impl_->require(1,entries,bytes);
        auto value=std::shared_ptr<const ActorSceneSkeleton>(new ActorSceneSkeleton(impl_->source,reference,std::move(hierarchy)));
        impl_->skeletons.emplace(reference,value);impl_->links+=entries;impl_->text+=bytes;return value;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
std::shared_ptr<ActorSceneAppearance> ActorSceneResource::appearance(std::uint32_t reference) {
    try {
        reference=canonical_reference(reference);
        if(const auto found=impl_->appearances.find(reference);found!=impl_->appearances.end()) return found->second;
        impl_->require(1);
        Reader read{document()};const auto& object=read.object(reference,"igAppearance");
        const auto skin=canonical_reference(read.ref(object,3));read.object(skin,"igSkin");
        const auto selectors=canonical_reference(read.ref(object,5));
        const auto authored=read.list(selectors,"igModelViewMatrixBoneSelectList");
        // Loaded player appearances in the verified profile start empty. General
        // serialized selectors require their inherited state and factory path.
        if(!authored.empty()) fail("Authored bone selectors require their original loaded-node preparation service.");
        const auto found=impl_->selector_lists.find(selectors);
        const bool fresh=found==impl_->selector_lists.end();
        impl_->require(fresh ? 2 : 1,2);
        auto value=std::shared_ptr<ActorSceneAppearance>(new ActorSceneAppearance);
        value->source_=impl_->source;value->reference_=reference;value->skin_reference_=skin;
        value->bone_select_list_reference_=selectors;
        value->selectors_=fresh ? std::make_shared<ActorBoneSelectorList>() : found->second;
        // Preallocate both map nodes before publishing either shared owner.
        decltype(impl_->selector_lists) added_selectors;
        decltype(impl_->appearances) added_appearances;
        if(fresh)added_selectors.emplace(selectors,value->selectors_);
        added_appearances.emplace(reference,value);
        impl_->selector_lists.merge(added_selectors);impl_->appearances.merge(added_appearances);
        impl_->links+=2;return value;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
std::shared_ptr<ActorSceneAppearanceList> ActorSceneResource::appearance_list(std::uint32_t reference) {
    try {
        reference=canonical_reference(reference);
        if(const auto found=impl_->lists.find(reference);found!=impl_->lists.end()) return found->second;
        impl_->require(1);
        Reader read{document()};const auto members=read.list(reference,"igAppearanceList");
        impl_->require(1,members.size());
        auto value=std::shared_ptr<ActorSceneAppearanceList>(new ActorSceneAppearanceList);
        value->source_=impl_->source;value->reference_=reference;
        for(const auto member:members) value->values_.push_back(appearance(member));
        // Preparing previously uncached appearances consumes its own budget.
        impl_->require(1,members.size());
        impl_->lists.emplace(reference,value);impl_->links+=members.size();return value;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
ActorSceneAction ActorSceneResource::action(std::uint32_t info_reference,std::uint32_t ram_reference) {
    try {
        Reader read{document()};info_reference=canonical_reference(info_reference);ram_reference=canonical_reference(ram_reference);
        const auto& info=read.object(info_reference,"beAction2Info");
        if(!document().info_list_reference) fail("Actor scene ACTION requires an exported info.");
        bool exported=false;
        for(const auto ref:read.list(*document().info_list_reference,"igInfoList")) exported|=canonical_reference(ref)==info_reference;
        if(!exported) fail("Actor scene ACTION info is not exported by this source.");
        bool member=false;
        for(const auto ref:read.list(read.ref(info,6),"beBaseInfoRamList")) member|=canonical_reference(ref)==ram_reference;
        if(!member) fail("Actor scene ACTION RAM is not owned by the selected info.");
        const auto& ram=read.object(ram_reference,"beAction2InfoRam");
        ActorSceneAction result;result.info_reference_=info_reference;result.ram_reference_=ram_reference;
        result.appearances_=appearance_list(read.ref(ram,12));
        result.actor_list_reference_=canonical_reference(read.ref(ram,13));
        for(const auto ref:read.list(result.actor_list_reference_,"igActorList")) {
            const auto& actor=read.object(ref,"igActor");
            result.actor_references_.push_back(canonical_reference(ref));
            result.actor_appearance_references_.push_back(canonical_reference(read.ref(actor,11)));
        }
        result.database_reference_=canonical_reference(read.ref(info,8));
        const auto& database=read.object(result.database_reference_,"igAnimationDatabase");
        const auto skeletons=read.list(read.ref(database,5),"igSkeletonList");
        if(skeletons.empty()) fail("Actor scene ACTION database has no first skeleton.");
        result.skeleton_=skeleton(skeletons.front());return result;
    } catch(const ActorSceneError&) {throw;}
      catch(const std::exception& error) {throw ActorSceneError(error.what());}
}
} // namespace dw4
