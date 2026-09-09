#include "dw4/animation_hierarchy.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <utility>

namespace dw4 {
namespace {
constexpr std::uint32_t null_reference=0xffffffffu;
constexpr std::size_t max_bones=1024, max_joints=256, max_name=4096, max_names=1024*1024;
[[noreturn]] void fail(const char* reason) { throw AnimationHierarchyError(reason); }
void validate_matrix(const AnimationMatrix& value) {
    for (const auto element:value) if (!std::isfinite(element)) fail("Animation hierarchy matrix is non-finite.");
    if (value[3]!=0 || value[7]!=0 || value[11]!=0 || value[15]!=1)
        fail("Animation hierarchy matrix is outside the verified affine profile.");
}
struct Reader {
    const IgbDocument& doc;
    const IgbObject& object(std::uint32_t reference,const char* type) const {
        const auto& result=doc.object(reference);
        if (!doc.is_a(result,type)) fail("Unexpected animation hierarchy object type.");
        return result;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,const char* type) const {
        const auto* value=doc.field(object,slot);
        if (!value || value->type_index>=doc.metadata.field_types.size() ||
            doc.metadata.field_types[value->type_index].name!=type)
            fail("Unexpected animation hierarchy field type.");
        return *value;
    }
    std::uint32_t word(const IgbObject& object,std::uint16_t slot,const char* type) const {
        if (field(object,slot,type).size!=4) fail("Animation hierarchy scalar field is not 32-bit.");
        return doc.field_u32(object,slot);
    }
    std::uint32_t object_ref(const IgbObject& object,std::uint16_t slot) const {
        const auto reference=word(object,slot,"igObjectRefMetaField");
        if (reference==null_reference) fail("Required animation hierarchy object reference is null.");
        return reference;
    }
};
}

AnimationHierarchyDefinition::AnimationHierarchyDefinition(std::uint32_t skeleton_reference,
    std::vector<AnimationHierarchyBone> bones,std::vector<AnimationMatrix> inverse_joint_matrices)
    : skeleton_reference_(skeleton_reference),bones_(std::move(bones)),
      inverse_joint_matrices_(std::move(inverse_joint_matrices)) {
    if (skeleton_reference_==null_reference || bones_.empty() || bones_.size()>max_bones ||
        inverse_joint_matrices_.size()>max_joints)
        fail("Animation hierarchy identity or counts are outside the bounded profile.");
    std::set<std::uint32_t> references;
    std::vector<bool> joints(inverse_joint_matrices_.size());
    std::size_t names=0;
    for (std::size_t index=0;index<bones_.size();++index) {
        const auto& bone=bones_[index];
        if (bone.reference==null_reference || !references.insert(bone.reference).second)
            fail("Animation hierarchy repeats a bone reference or contains a null reference.");
        if (bone.name.size()>max_name || bone.name.find('\0')!=std::string::npos || bone.name.size()>max_names-names)
            fail("Animation hierarchy bone names exceed the bounded string profile.");
        names+=bone.name.size();
        if (index==0) {
            if (bone.parent!=-1 || bone.joint_index!=-1) fail("Animation hierarchy bone zero must be an unparented helper.");
        } else if (bone.parent<0 || static_cast<std::size_t>(bone.parent)>=index)
            fail("Animation hierarchy parents must precede their children.");
        if (bone.joint_index < -1 || (bone.joint_index>=0 && static_cast<std::size_t>(bone.joint_index)>=joints.size()))
            fail("Animation hierarchy joint index is outside the output array.");
        if (bone.joint_index>=0) {
            const auto joint=static_cast<std::size_t>(bone.joint_index);
            if (joints[joint]) fail("Animation hierarchy joint has multiple owning bones.");
            joints[joint]=true;
        }
    }
    if (std::find(joints.begin(),joints.end(),false)!=joints.end()) fail("Animation hierarchy joint has no owning bone.");
    for (const auto& matrix:inverse_joint_matrices_) validate_matrix(matrix);
}

AnimationHierarchyDefinition decode_animation_hierarchy(const IgbDocument& doc,std::uint32_t skeleton_reference) {
    try {
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Animation hierarchy byte order is outside the verified profile.");
        Reader read{doc};
        const auto& skeleton=read.object(skeleton_reference,"igSkeleton");
        const auto list_reference=read.object_ref(skeleton,4);
        const auto& list=read.object(list_reference,"igSkeletonBoneInfoList");
        const auto count=read.word(list,2,"igIntMetaField"),capacity=read.word(list,3,"igIntMetaField");
        if (!count || count>max_bones || capacity<count || capacity>max_bones)
            fail("Animation hierarchy bone list exceeds the bounded profile.");
        const auto memory=read.word(list,4,"igMemoryRefMetaField");
        if (memory==null_reference || doc.memory(memory).size()!=std::size_t(capacity)*4)
            fail("Animation hierarchy bone list memory does not match capacity.");
        std::vector<AnimationHierarchyBone> bones;
        for (const auto reference:doc.object_list(list_reference)) {
            if (!reference) fail("Animation hierarchy bone list contains a null object.");
            const auto& bone=read.object(*reference,"igSkeletonBoneInfo");
            const auto& name_field=read.field(bone,2,"igStringMetaField");
            if (name_field.size<4 || name_field.size>max_name+8) fail("Animation hierarchy name field exceeds its bounded width.");
            const auto name=doc.field_string(bone,2);
            const auto parent=std::bit_cast<std::int32_t>(read.word(bone,3,"igIntMetaField"));
            const auto joint=std::bit_cast<std::int32_t>(read.word(bone,4,"igIntMetaField"));
            bones.push_back({*reference,std::string(name),parent,joint});
        }
        const auto joint_count=read.word(skeleton,6,"igIntMetaField");
        if (joint_count>max_joints) fail("Animation hierarchy joint count exceeds its bounded profile.");
        const auto inverse_reference=read.word(skeleton,5,"igMemoryRefMetaField");
        std::span<const std::uint8_t> bytes;
        if (inverse_reference==null_reference) {
            if (joint_count) fail("Animation hierarchy inverse-joint array is null.");
        } else bytes=doc.memory(inverse_reference);
        if (bytes.size()!=std::size_t(joint_count)*64) fail("Animation hierarchy inverse-joint byte count differs from joint count.");
        std::vector<AnimationMatrix> inverses(joint_count);
        for (std::size_t index=0;index<inverses.size();++index) for (std::size_t element=0;element<16;++element) {
            const auto at=index*64+element*4;
            std::uint32_t bits=0; for (std::size_t byte=0;byte<4;++byte) bits|=std::uint32_t(bytes[at+byte])<<(byte*8);
            inverses[index][element]=std::bit_cast<float>(bits);
        }
        return AnimationHierarchyDefinition(skeleton_reference,std::move(bones),std::move(inverses));
    } catch (const AnimationHierarchyError&) { throw; }
      catch (const std::exception& error) { throw AnimationHierarchyError(error.what()); }
}

void evaluate_animation_hierarchy(const AnimationHierarchyDefinition& definition,
    std::span<const AnimationMatrix> local,std::vector<AnimationMatrix>& bone_cache,
    std::vector<AnimationMatrix>& joint_cache) {
    const auto& bones=definition.bones(); const auto& inverses=definition.inverse_joint_matrices();
    if (bones.empty() || &bone_cache==&joint_cache || local.size()!=bones.size() ||
        bone_cache.size()!=bones.size() || joint_cache.size()!=inverses.size())
        fail("Animation hierarchy local or cache dimensions are inconsistent.");
    for (const auto& matrix:local) validate_matrix(matrix);
    validate_matrix(bone_cache.front());
    auto next_bones=bone_cache;
    auto next_joints=joint_cache;
    std::size_t inverse_index=0;
    try {
        // 80203024 starts at bone1. Parent0 copies local instead of applying the
        // traversal matrix in bone_cache[0]. Helpers still accumulate normally.
        for (std::size_t index=1;index<bones.size();++index) {
            const auto& bone=bones[index];
            next_bones[index]=bone.parent==0 ? local[index] :
                original_matrix_multiply(next_bones[static_cast<std::size_t>(bone.parent)],local[index]);
            validate_matrix(next_bones[index]);
            if (bone.joint_index!=-1) {
                // 8020307C..90 selects output using bmIdx but advances r29 by
                // 64 only after consuming the next serialized inverse matrix.
                auto& joint=next_joints[static_cast<std::size_t>(bone.joint_index)];
                joint=original_matrix_multiply(next_bones[index],inverses[inverse_index++]);
                validate_matrix(joint);
            }
        }
    } catch (const AnimationMathError& error) { throw AnimationHierarchyError(error.what()); }
    bone_cache.swap(next_bones); joint_cache.swap(next_joints);
}

} // namespace dw4
