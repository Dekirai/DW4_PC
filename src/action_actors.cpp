#include "dw4/action_actors.hpp"

#include <algorithm>
#include <bit>
#include <set>

namespace dw4 {
namespace {
constexpr std::uint32_t null_reference=0xffffffffu;
constexpr std::size_t max_actors=64, max_bones=1024, max_joints=256;
[[noreturn]] void fail(const char* reason) { throw ActionActorError(reason); }
AnimationMatrix identity() {
    AnimationMatrix value{}; value[0]=value[5]=value[10]=value[15]=1; return value;
}
void valid_reference(std::uint32_t value) {
    if (value==null_reference) fail("Action actor ownership contains a null reference.");
}
struct Reader {
    const IgbDocument& doc;
    const IgbObject& object(std::uint32_t ref,const char* type) const {
        const auto& value=doc.object(ref);
        if (!doc.is_a(value,type)) fail("Unexpected action actor graph object type.");
        return value;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,const char* type,std::size_t size) const {
        const auto* value=doc.field(object,slot);
        if (!value || value->size!=size || value->type_index>=doc.metadata.field_types.size() ||
            doc.metadata.field_types[value->type_index].name!=type)
            fail("Unexpected action actor field type or width.");
        return *value;
    }
    std::uint32_t word(const IgbObject& object,std::uint16_t slot,const char* type) const {
        field(object,slot,type,4); return doc.field_u32(object,slot);
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        const auto value=word(object,slot,"igObjectRefMetaField"); valid_reference(value); return value;
    }
    std::vector<std::uint32_t> list(std::uint32_t ref,const char* type,std::size_t limit) const {
        const auto& value=object(ref,type);
        const auto count=word(value,2,"igIntMetaField"), capacity=word(value,3,"igIntMetaField");
        const auto memory=word(value,4,"igMemoryRefMetaField");
        if (count>limit || capacity>limit || count>capacity) fail("Action actor list exceeds its bounded profile.");
        if (memory==null_reference) {
            if (capacity) fail("Action actor list with capacity has no memory.");
        } else if (doc.memory(memory).size()!=std::size_t(capacity)*4)
            fail("Action actor list memory does not match capacity.");
        std::vector<std::uint32_t> result;
        for (const auto entry:doc.object_list(ref)) {
            if (!entry) fail("Action actor list contains a null object.");
            result.push_back(*entry);
        }
        return result;
    }
    void default_bool(const IgbObject& value,std::uint16_t slot,bool expected) const {
        if (!doc.field(value,slot)) return;
        const auto bytes=doc.field_bytes(field(value,slot,"igBoolMetaField",1));
        if (bytes[0]!=static_cast<std::uint8_t>(expected))
            fail("Serialized action actor traversal state requires restoration.");
    }
};
}

ActionActorGroup::ActionActorGroup(std::string source,std::uint32_t ram_reference,
    std::uint32_t combiner_reference,std::vector<ActionActorDefinition> definitions)
    : source_(std::move(source)),ram_reference_(ram_reference),combiner_reference_(combiner_reference),
      definitions_(std::move(definitions)) {
    if (source_.empty() || source_.size()>4096 || source_.find('\0')!=std::string::npos)
        fail("Action actors require a bounded explicit source without embedded nulls.");
    valid_reference(ram_reference_); valid_reference(combiner_reference_);
    if (definitions_.empty() || definitions_.size()>max_actors) fail("Action actor count is outside the verified profile.");
    std::set<std::uint32_t> actors,appearances;
    for (const auto& value:definitions_) {
        valid_reference(value.reference); valid_reference(value.appearance_reference);
        if (!actors.insert(value.reference).second || !appearances.insert(value.appearance_reference).second)
            fail("Shared action actor or appearance identities require additional alias ownership.");
        if (!value.bone_count || value.bone_count>max_bones || !value.joint_count || value.joint_count>max_joints ||
            value.joint_count>value.bone_count || value.bone_count!=definitions_.front().bone_count ||
            value.joint_count!=definitions_.front().joint_count)
            fail("Action actors require one bounded common skeleton.");
        states_.push_back({value.reference,value.appearance_reference,{},{},{}});
    }
}

std::size_t ActionActorGroup::activate(std::uint32_t combiner_reference) {
    if (combiner_reference!=combiner_reference_) fail("Action actor combiner belongs to another prepared group.");
    // Validate caller-owned mutations before publishing an allocation prefix.
    for (std::size_t i=0;i<states_.size();++i) {
        const auto& state=states_[i]; const auto& definition=definitions_[i];
        if (state.reference!=definition.reference || state.appearance_reference!=definition.appearance_reference ||
            (state.combiner_reference && *state.combiner_reference!=combiner_reference_) ||
            (!state.bone_matrices.empty() && state.bone_matrices.size()!=definition.bone_count) ||
            (!state.joint_matrices.empty() && state.joint_matrices.size()!=definition.joint_count))
            fail("Action actor cache ownership or allocation size is inconsistent.");
    }
    std::size_t allocated=0;
    for (std::size_t i=0;i<states_.size();++i) {
        auto& state=states_[i];
        if (!state.bone_matrices.empty()) break;
        auto bones=std::vector<AnimationMatrix>(definitions_[i].bone_count,identity());
        auto joints=std::vector<AnimationMatrix>(definitions_[i].joint_count,identity());
        state.combiner_reference=combiner_reference;
        state.bone_matrices=std::move(bones); state.joint_matrices=std::move(joints); ++allocated;
    }
    return allocated;
}

void ActionActorGroup::deactivate() noexcept {
    for (auto& state:states_) {
        std::vector<AnimationMatrix>().swap(state.bone_matrices);
        std::vector<AnimationMatrix>().swap(state.joint_matrices);
    }
}

ActionActorGroup decode_action_actor_group(const IgbDocument& doc,std::uint32_t ram_reference,
    std::uint32_t combiner_reference,std::string source) {
    try {
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Action actor byte order is outside the verified profile.");
        Reader read{doc};
        const auto& ram=read.object(ram_reference,"beAction2InfoRam");
        const auto& combiner=read.object(combiner_reference,"igAnimationCombiner");
        const auto& skeleton=read.object(read.ref(combiner,3),"igSkeleton");
        const auto bones=read.list(read.ref(skeleton,4),"igSkeletonBoneInfoList",max_bones);
        const auto joints=read.word(skeleton,6,"igIntMetaField");
        if (bones.empty() || !joints || joints>max_joints || joints>bones.size()) fail("Action actor skeleton counts are invalid.");
        std::set<std::uint32_t> bone_refs,joint_indices;
        for (const auto ref:bones) {
            if (!bone_refs.insert(ref).second) fail("Action actor skeleton repeats a bone.");
            const auto& bone=read.object(ref,"igSkeletonBoneInfo");
            const auto joint=read.word(bone,4,"igIntMetaField");
            if (joint!=null_reference && (joint>=joints || !joint_indices.insert(joint).second))
                fail("Action actor skeleton joint ownership is invalid.");
        }
        if (joint_indices.size()!=joints) fail("Action actor skeleton has an unowned joint.");
        const auto& node=read.object(read.ref(ram,11),"beModelCtrlNode2");
        if (!read.list(read.ref(node,15),"igAttrList",max_actors).empty())
            fail("Serialized action node attributes require restoration before NDSP can own their removal.");
        const auto actors=read.list(read.ref(ram,13),"igActorList",max_actors);
        const auto appearances=read.list(read.ref(ram,12),"igAppearanceList",max_actors);
        std::set<std::uint32_t> appearance_refs(appearances.begin(),appearances.end());
        if (actors.empty() || actors.size()!=appearances.size() || appearance_refs.size()!=appearances.size())
            fail("Action actor and appearance lists require unique corresponding entries.");
        for (const auto ref:appearances) {
            const auto& appearance=read.object(ref,"igAppearance");
            read.object(read.ref(appearance,3),"igSkin");
        }
        std::vector<ActionActorDefinition> definitions;
        for (const auto ref:actors) {
            const auto& actor=read.object(ref,"igActor");
            if (read.word(actor,8,"igObjectRefMetaField")!=null_reference ||
                read.word(actor,9,"igMemoryRefMetaField")!=null_reference ||
                read.word(actor,10,"igMemoryRefMetaField")!=null_reference ||
                read.word(actor,12,"igObjectRefMetaField")!=null_reference)
                fail("Serialized action actor combiner, database or matrix caches require restoration.");
            const auto appearance=read.ref(actor,11);
            if (!appearance_refs.erase(appearance)) fail("Action actor appearance is absent or shared in its RAM.");
            if (!read.list(read.ref(actor,13),"igAnimationModifierList",max_bones).empty())
                fail("Action actor modifiers require traversal integration.");
            const auto bytes=doc.field_bytes(read.field(actor,14,"igMatrix44fMetaField",64));
            for (std::size_t at=0;at<16;++at) {
                std::uint32_t bits=0; for (std::size_t byte=0;byte<4;++byte) bits|=std::uint32_t(bytes[at*4+byte])<<(byte*8);
                if (std::bit_cast<float>(bits)!=(at%5==0 ? 1.0f : 0.0f)) fail("Nonidentity action actor transforms require traversal integration.");
            }
            read.default_bool(actor,15,false); read.default_bool(actor,16,false);
            read.default_bool(actor,19,true); read.default_bool(actor,20,true);
            definitions.push_back({ref,appearance,bones.size(),joints});
        }
        return ActionActorGroup(std::move(source),ram_reference,combiner_reference,std::move(definitions));
    } catch (const ActionActorError&) { throw; }
      catch (const std::exception& error) { throw ActionActorError(error.what()); }
}

} // namespace dw4
