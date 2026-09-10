#include "dw4/actor_scene.hpp"

#include <algorithm>
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
};
}
struct ActorSceneSource {
    IgbDocument document;
    std::string source;
    std::uint32_t occurrence{};
};
struct ActorSceneResource::Impl {
    std::shared_ptr<const ActorSceneSource> source;
    std::map<std::uint32_t,std::shared_ptr<const ActorSceneNode>> nodes;
    std::map<std::uint32_t,std::shared_ptr<const ActorSceneSkeleton>> skeletons;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneAppearance>> appearances;
    std::map<std::uint32_t,std::shared_ptr<ActorSceneAppearanceList>> lists;
    std::map<std::uint32_t,std::shared_ptr<ActorBoneSelectorList>> selector_lists;
    std::size_t links{},text{};
    std::size_t owners() const noexcept {
        return nodes.size()+skeletons.size()+appearances.size()+lists.size()+selector_lists.size();
    }
    void require(std::size_t objects,std::size_t entries=0,std::size_t bytes=0) const {
        if(objects>max_objects-owners())fail("Actor scene exceeds its aggregate owner budget.");
        if(entries>max_links-links)fail("Actor scene exceeds its aggregate copied-entry budget.");
        if(bytes>max_text-text)fail("Actor scene exceeds its aggregate copied-text budget.");
    }
};
const IgbDocument& ActorSceneNode::document() const noexcept { return source_->document; }
const std::string& ActorSceneNode::source() const noexcept { return source_->source; }
std::uint32_t ActorSceneNode::source_occurrence() const noexcept { return source_->occurrence; }
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
    try {
        Reader read{document()};std::set<std::uint32_t> visiting;
        std::map<std::uint32_t,std::shared_ptr<const ActorSceneNode>> added;
        std::size_t links{},copied_text{};
        std::function<std::shared_ptr<const ActorSceneNode>(std::uint32_t,std::size_t)> visit;
        visit=[&](std::uint32_t ref,std::size_t depth)->std::shared_ptr<const ActorSceneNode> {
            ref=canonical_reference(ref);
            if(visiting.contains(ref)) fail("Actor scene node graph contains a cycle.");
            if(depth>max_depth) fail("Actor scene node graph exceeds its depth budget.");
            const auto cached=[&](const auto& owner) {
                if(owner->subtree_height_>max_depth-depth)fail("Cached actor scene subtree exceeds the node path budget.");
                return owner;
            };
            if(const auto found=impl_->nodes.find(ref);found!=impl_->nodes.end()) return cached(found->second);
            if(const auto found=added.find(ref);found!=added.end()) return cached(found->second);
            impl_->require(added.size()+visiting.size()+1,links,copied_text);
            const auto& value=document().object(ref);
            if(!document().is_a(value,"igNode")) fail("Actor scene child is not a node.");
            const auto& type=document().metadata.object_types.at(value.type_index).name;
            read.object(ref,type);
            const auto& name=read.field(value,2,"igStringMetaField",{});
            if(name.size>4104) fail("Actor scene node name exceeds its byte budget.");
            const auto text=document().field_string(value,2);
            if(text.size()>4096 || text.find('\0')!=std::string_view::npos) fail("Actor scene node name is invalid.");
            const auto bytes=text.size()+type.size();
            if(bytes>max_text-copied_text)fail("Actor scene node text exceeds its aggregate byte budget.");
            impl_->require(added.size()+visiting.size()+1,links,copied_text+bytes);
            copied_text+=bytes;
            read.field(value,5,"igIntMetaField");
            auto result=std::shared_ptr<ActorSceneNode>(new ActorSceneNode);
            result->source_=impl_->source;result->reference_=ref;result->type_=type;
            result->name_=name.size==4 ? std::optional<std::string>{} : std::string(text);
            result->flags_=document().field_u32(value,5);
            visiting.insert(ref);
            if(document().is_a(value,"igGroup")) {
                const auto children=read.list(read.ref(value,7),"igNodeList");
                if(children.size()>max_links-links) fail("Actor scene graph exceeds its edge budget.");
                impl_->require(added.size()+visiting.size(),links+children.size(),copied_text);
                links+=children.size();
                for(const auto child:children) {
                    auto owner=visit(child,depth+1);
                    result->subtree_height_=std::max(result->subtree_height_,owner->subtree_height_+1);
                    result->children_.push_back(std::move(owner));
                }
            }
            visiting.erase(ref);added.emplace(ref,result);return result;
        };
        auto result=visit(reference,0);impl_->nodes.merge(added);impl_->links+=links;impl_->text+=copied_text;return result;
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
