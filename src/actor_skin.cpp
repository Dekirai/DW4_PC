#include "dw4/actor_skin.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr std::uint32_t null_reference=0xffffffffu;
constexpr std::size_t max_vertices=4u*1024u*1024u;
[[noreturn]] void fail(const char* reason) { throw ActorSkinError(reason); }
void reference(std::uint32_t value) { if (value==null_reference) fail("Actor skin identity contains a null reference."); }
template<class Matrix> void affine(const Matrix& matrix) {
    for (const auto value:matrix) if (!std::isfinite(value)) fail("Actor skin matrix is non-finite.");
    if (matrix[3]!=0 || matrix[7]!=0 || matrix[11]!=0 || matrix[15]!=1)
        fail("Actor skin matrix is outside the affine profile.");
}
AnimationHierarchyDefinition validate_skin(const SkinningData& skin) {
    const auto& info=skin.info;
    if (!info.supported || !info.geometry_node_reference || !info.selector_reference || !info.skin_reference ||
        !info.database_reference || !info.skeleton_reference) fail("Actor skin requires a complete explicit decoded geometry binding.");
    for (const auto ref:{info.geometry_reference,*info.geometry_node_reference,*info.selector_reference,
        *info.skin_reference,*info.database_reference,*info.skeleton_reference}) reference(ref);
    if (skin.bones.empty() || skin.bones.size()>1024 || skin.joints.empty() || skin.joints.size()>256 ||
        info.bone_count!=skin.bones.size() || info.joint_count!=skin.joints.size() ||
        info.vertex_count!=skin.source_positions.size() || skin.source_positions.empty() ||
        skin.source_positions.size()>max_vertices || skin.influences.size()!=skin.source_positions.size() ||
        skin.palette_to_joint.empty() || skin.palette_to_joint.size()>256)
        fail("Actor skin bone, joint, palette or vertex dimensions are inconsistent.");
    std::vector<AnimationHierarchyBone> bones;
    for (const auto& bone:skin.bones) bones.push_back({bone.reference_index,bone.name,bone.parent,bone.joint_index});
    std::vector<AnimationMatrix> inverses;
    for (const auto& joint:skin.joints) {
        affine(joint.inverse_bind);
        AnimationMatrix matrix{};
        for (std::size_t i=0;i<16;++i) {
            const auto value=joint.inverse_bind[i];
            if (std::abs(value)>std::numeric_limits<float>::max() || static_cast<double>(static_cast<float>(value))!=value)
                fail("Actor skin inverse matrix must preserve serialized float components.");
            matrix[i]=static_cast<float>(value);
        }
        inverses.push_back(matrix);
    }
    AnimationHierarchyDefinition hierarchy(*info.skeleton_reference,std::move(bones),std::move(inverses));
    for (std::size_t joint=0;joint<skin.joints.size();++joint) {
        const auto& value=skin.joints[joint];
        if (value.bone_index>=skin.bones.size() || skin.bones[value.bone_index].joint_index!=static_cast<std::int32_t>(joint))
            fail("Actor skin joint does not select its owning skeleton bone.");
        auto parent=skin.bones[value.bone_index].parent;
        while (parent>=0 && skin.bones[static_cast<std::size_t>(parent)].joint_index<0)
            parent=skin.bones[static_cast<std::size_t>(parent)].parent;
        const auto parent_joint=parent<0 ? -1 : skin.bones[static_cast<std::size_t>(parent)].joint_index;
        if (value.parent_joint!=parent_joint) fail("Actor skin joint parent differs from its skeleton ancestry.");
    }
    for (const auto joint:skin.palette_to_joint) if (joint>=skin.joints.size()) fail("Actor skin palette selects an absent joint.");
    for (std::size_t vertex=0;vertex<skin.source_positions.size();++vertex) {
        const auto& point=skin.source_positions[vertex];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) fail("Actor skin source position is non-finite.");
        const auto& influence=skin.influences[vertex];
        if (influence.weight!=1 || influence.palette_index>=skin.palette_to_joint.size() ||
            skin.palette_to_joint[influence.palette_index]!=influence.joint_index)
            fail("Actor skin requires one full-weight palette influence per vertex.");
    }
    return hierarchy;
}
void match_hierarchy(const SkinningData& skin,const AnimationHierarchyDefinition& hierarchy) {
    if (skin.info.skeleton_reference!=hierarchy.skeleton_reference() || skin.bones.size()!=hierarchy.bones().size() ||
        skin.joints.size()!=hierarchy.inverse_joint_matrices().size()) fail("Actor skin differs from the combiner skeleton dimensions.");
    for (std::size_t i=0;i<skin.bones.size();++i) {
        const auto& bone=skin.bones[i]; const auto& expected=hierarchy.bones()[i];
        if (bone.reference_index!=expected.reference || bone.name!=expected.name || bone.parent!=expected.parent || bone.joint_index!=expected.joint_index)
            fail("Actor skin differs from the complete combiner bone binding.");
    }
    // SkinningData retains inverse_bind arrays in serialized order. Original
    // hierarchy input advances sequentially, independently of output bmIdx.
    for (std::size_t joint=0;joint<skin.joints.size();++joint) for (std::size_t i=0;i<16;++i)
        if (std::bit_cast<std::uint32_t>(static_cast<float>(skin.joints[joint].inverse_bind[i]))!=
            std::bit_cast<std::uint32_t>(hierarchy.inverse_joint_matrices()[joint][i]))
            fail("Actor skin inverse matrices differ from serialized combiner hierarchy input.");
}
struct Reader {
    const IgbDocument& doc;
    const IgbObject& object(std::uint32_t ref,const char* type) const {
        const auto& value=doc.object(ref); if (!doc.is_a(value,type)) fail("Unexpected actor skin graph object type."); return value;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,const char* type,std::size_t width) const {
        const auto* value=doc.field(object,slot);
        if (!value || value->size!=width || doc.metadata.field_types.at(value->type_index).name!=type)
            fail("Unexpected actor skin graph field type or width.");
        return *value;
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField",4);
        const auto value=doc.field_reference(object,slot); if (!value) fail("Required actor skin graph reference is null."); return *value;
    }
    std::vector<std::optional<std::uint32_t>> list(std::uint32_t ref,const char* type) const {
        const auto& value=object(ref,type);
        field(value,2,"igIntMetaField",4); field(value,3,"igIntMetaField",4); field(value,4,"igMemoryRefMetaField",4);
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if (count>64 || capacity>64 || count>capacity) fail("Actor skin list exceeds its bounded ownership profile.");
        const auto memory=doc.field_reference(value,4);
        if ((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("Actor skin list memory differs from its capacity.");
        return doc.object_list(ref);
    }
};
}

ActorSkinBinding::ActorSkinBinding(std::string source,std::uint32_t ram_reference,std::uint32_t combiner_reference,
    std::size_t actor_index,std::uint32_t actor_reference,std::uint32_t appearance_reference,SkinningData skin)
    :source_(std::move(source)),ram_reference_(ram_reference),combiner_reference_(combiner_reference),
      actor_reference_(actor_reference),appearance_reference_(appearance_reference),actor_index_(actor_index),skin_(std::move(skin)) {
    try {
        if (source_.empty() || source_.size()>4096 || source_.find('\0')!=std::string::npos || actor_index_>=64)
            fail("Actor skin requires a bounded explicit source and actor index.");
        for (const auto ref:{ram_reference_,combiner_reference_,actor_reference_,appearance_reference_}) reference(ref);
        validate_skin(skin_);
        if (std::find(skin_.info.actor_references.begin(),skin_.info.actor_references.end(),actor_reference_)==skin_.info.actor_references.end())
            fail("Actor skin provenance does not contain its selected actor.");
    } catch (const ActorSkinError&) { throw; }
      catch (const std::exception& error) { throw ActorSkinError(error.what()); }
}

ActorSkinBinding decode_actor_skin_binding(const IgbDocument& doc,const ActionActorGroup& actors,
    std::size_t actor_index,std::uint32_t geometry_reference) {
    try {
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Actor skin byte order is outside the verified profile.");
        if (actor_index>=actors.definitions().size() || actor_index>=actors.states().size()) fail("Actor skin selection is outside the owning RAM.");
        Reader read{doc}; const auto& definition=actors.definitions()[actor_index];
        const auto& ram=read.object(actors.ram_reference(),"beAction2InfoRam");
        const auto entries=read.list(read.ref(ram,13),"igActorList");
        if (entries.size()!=actors.definitions().size()) fail("Actor skin RAM actor count differs from its native group.");
        for (std::size_t i=0;i<entries.size();++i)
            if (entries[i]!=actors.definitions()[i].reference) fail("Actor skin RAM actor identity or order differs from its native group.");
        const auto& actor=read.object(definition.reference,"igActor");
        if (read.ref(actor,11)!=definition.appearance_reference) fail("Actor skin appearance differs from its owning actor.");
        const auto& appearance=read.object(definition.appearance_reference,"igAppearance");
        const auto main_skin=read.ref(appearance,3); read.object(main_skin,"igSkin");
        const std::array<const char*,4> extras{"igSkinList","igModelViewMatrixBoneSelectList","igStringObjList","igNodeList"};
        for (std::uint16_t i=0;i<extras.size();++i)
            if (!read.list(read.ref(appearance,static_cast<std::uint16_t>(4+i)),extras[i]).empty())
                fail("Actor appearance attachments require additional traversal effects.");
        auto skin=decode_igb_skinning(doc,geometry_reference);
        if (skin.info.skin_reference!=main_skin || skin.info.selector_reference!=read.ref(doc.object(main_skin),3))
            fail("Selected geometry does not belong to the actor's explicit main skin.");
        const auto& combiner=read.object(actors.combiner_reference(),"igAnimationCombiner");
        if (skin.info.skeleton_reference!=read.ref(combiner,3) || definition.bone_count!=skin.bones.size() || definition.joint_count!=skin.joints.size())
            fail("Actor skin skeleton differs from its owning combiner or activation counts.");
        match_hierarchy(skin,decode_animation_hierarchy(doc,*skin.info.skeleton_reference));
        return {actors.source(),actors.ram_reference(),actors.combiner_reference(),actor_index,
            definition.reference,definition.appearance_reference,std::move(skin)};
    } catch (const ActorSkinError&) { throw; }
      catch (const std::exception& error) { throw ActorSkinError(error.what()); }
}

ActorSkinFrame capture_actor_skin(const ActorSkinBinding& binding,const ActionActorGroup& actors,const AnimationCombiner& combiner) {
    try {
        if (binding.source()!=actors.source() || binding.source()!=combiner.source() || binding.ram_reference()!=actors.ram_reference() ||
            binding.combiner_reference()!=actors.combiner_reference() || combiner.reference()!=binding.combiner_reference() ||
            binding.actor_index()>=actors.definitions().size() || binding.actor_index()>=actors.states().size())
            fail("Actor skin capture source, RAM or combiner ownership differs from its binding.");
        const auto& definition=actors.definitions()[binding.actor_index()]; const auto& actor=actors.states()[binding.actor_index()];
        if (definition.reference!=binding.actor_reference() || definition.appearance_reference!=binding.appearance_reference() ||
            actor.reference!=definition.reference || actor.appearance_reference!=definition.appearance_reference ||
            actor.combiner_reference!=binding.combiner_reference()) fail("Actor skin capture requires its attached original actor and appearance.");
        const auto& skin=binding.skin();
        if (!combiner.hierarchy()) fail("Actor skin capture requires a decoded combiner hierarchy.");
        match_hierarchy(skin,*combiner.hierarchy());
        if (definition.bone_count!=skin.bones.size() || definition.joint_count!=skin.joints.size() ||
            actor.bone_matrices.size()!=definition.bone_count || actor.joint_matrices.size()!=definition.joint_count)
            fail("Actor skin capture requires both original activation arrays.");
        const auto& bones=actor.cache_used ? actor.bone_matrices : combiner.bone_matrices();
        const auto& joints=actor.cache_used ? actor.joint_matrices : combiner.joint_matrices();
        if (bones.size()!=skin.bones.size() || joints.size()!=skin.joints.size()) fail("Actor skin selected cache dimensions differ from its skeleton.");
        for (const auto& matrix:bones) affine(matrix);
        for (const auto& matrix:joints) affine(matrix);
        ActorSkinFrame result; result.binding_identity_=binding.identity_; result.model_view=bones[0];
        result.palette_matrices.reserve(skin.palette_to_joint.size()); result.joint_positions.reserve(skin.joints.size());
        for (const auto joint:skin.palette_to_joint) result.palette_matrices.push_back(joints[joint]);
        for (const auto& joint:skin.joints) {
            const auto& bone=bones[joint.bone_index]; result.joint_positions.push_back({bone[12],bone[13],bone[14]});
        }
        return result;
    } catch (const ActorSkinError&) { throw; }
      catch (const std::exception& error) { throw ActorSkinError(error.what()); }
}

SkinPose evaluate_actor_skin(const ActorSkinBinding& binding,const ActorSkinFrame& frame) {
    try {
        if (!binding.identity_ || frame.binding_identity_!=binding.identity_)
            fail("Actor skin frame belongs to another immutable binding.");
        const auto& skin=binding.skin();
        if (frame.palette_matrices.size()!=skin.palette_to_joint.size() || frame.joint_positions.size()!=skin.joints.size())
            fail("Actor skin frame palette or joint dimensions differ from its binding.");
        affine(frame.model_view);
        for (const auto& matrix:frame.palette_matrices) affine(matrix);
        for (const auto& point:frame.joint_positions)
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) fail("Actor skin frame joint position is non-finite.");
        SkinPose result; result.positions.reserve(skin.source_positions.size()); result.joint_positions=frame.joint_positions;
        for (std::size_t i=0;i<skin.source_positions.size();++i) {
            const auto& point=skin.source_positions[i];
            const auto transformed=original_skin_point(frame.palette_matrices[skin.influences[i].palette_index],{point.x,point.y,point.z});
            result.positions.push_back({transformed[0],transformed[1],transformed[2]});
        }
        return result;
    } catch (const ActorSkinError&) { throw; }
      catch (const std::exception& error) { throw ActorSkinError(error.what()); }
}

} // namespace dw4
