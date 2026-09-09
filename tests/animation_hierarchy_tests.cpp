#include "hierarchy_fixture.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using hierarchy_fixture::Fixture;
using hierarchy_fixture::identity;
using hierarchy_fixture::translation;
using hierarchy_fixture::null_ref;
int checks=0;
void check(bool value,const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& call,const char* reason) {
    bool rejected=false; try { call(); } catch (const std::runtime_error&) { rejected=true; } check(rejected,reason);
}

void composition() {
    Fixture fixture; const auto definition=fixture.definition();
    check(definition.skeleton_reference()==fixture.skeleton && definition.bones().size()==5 &&
        definition.inverse_joint_matrices()==fixture.inverses,"Retain explicit skeleton and serialized inverse order");
    check(definition.bones()[1].joint_index==2 && definition.bones()[3].joint_index==0 &&
        definition.bones()[4].joint_index==1,"Accept joint output order independently of inverse input order");
    std::vector<AnimationMatrix> local(5,identity()),bones(5,identity()),joints(3,identity());
    local[0]=translation(-900,-800,-700);
    local[1]={0,1,0,0,-1,0,0,0,0,0,1,0,3,4,5,1};
    local[2]=translation(2,0,0); local[3]=translation(0,3,0); local[3][0]=2;
    local[4]=translation(-2,8,1); local[4][3]=-0.0f;
    bones[0]=translation(500,600,700); bones[0][4]=4;
    const auto root=bones[0]; bones[2][12]=999;
    evaluate_animation_hierarchy(definition,local,bones,joints);
    const AnimationMatrix helper{0,1,0,0,-1,0,0,0,0,0,1,0,3,6,5,1};
    const AnimationMatrix descendant{0,2,0,0,-1,0,0,0,0,0,1,0,0,6,5,1};
    const AnimationMatrix joint0{0,6,0,0,-1,0,0,0,0,0,1,0,0,6,12,1};
    const AnimationMatrix joint2{0,1,0,0,-1,0,0,0,0,0,1,0,3,14,5,1};
    check(bones[0]==root,"Skipped root preserves the supplied traversal matrix");
    check(bones[1]==local[1] && bones[4]==local[4],"Parent zero copies local without the traversal root");
    check(std::bit_cast<std::uint32_t>(bones[4][3])==0x80000000u,"Parent-zero copy preserves the original matrix bits");
    check(bones[2]==helper && bones[3]==descendant,"A joint-free helper participates in noncommuting parent composition");
    check(joints[0]==joint0 && joints[1]==translation(7,7,1) && joints[2]==joint2,
        "Sequential inverse matrices feed scrambled joint outputs without reindexing inputs");

    auto aliased_bones=local; aliased_bones[0]=root; std::vector<AnimationMatrix> aliased_joints(3,identity());
    evaluate_animation_hierarchy(definition,aliased_bones,aliased_bones,aliased_joints);
    check(aliased_bones==bones && aliased_joints==joints,"Local input may alias the bone cache without affecting computation");
    auto missing=bones; missing.pop_back();
    rejects([&]{evaluate_animation_hierarchy(definition,local,missing,joints);},"Reject incomplete bone cache dimensions");
    rejects([&]{evaluate_animation_hierarchy(definition,std::span<const AnimationMatrix>(local).first(4),bones,joints);},"Reject incomplete local input");
    rejects([&]{evaluate_animation_hierarchy(definition,local,bones,bones);},"Reject aliased output arrays");
    const auto saved_bones=bones,saved_joints=joints;
    local[2][5]=std::numeric_limits<float>::infinity();
    rejects([&]{evaluate_animation_hierarchy(definition,local,bones,joints);},"Reject nonfinite local matrix");
    check(bones==saved_bones && joints==saved_joints,"Input rejection preserves both cache arrays");
    local.assign(5,identity()); local[1][0]=std::numeric_limits<float>::max(); local[2][0]=2;
    rejects([&]{evaluate_animation_hierarchy(definition,local,bones,joints);},"Reject finite inputs whose original matrix arithmetic overflows");
    check(bones==saved_bones && joints==saved_joints,"Arithmetic rejection preserves both cache arrays");
    bones[0][15]=0;
    rejects([&]{evaluate_animation_hierarchy(definition,local,bones,joints);},"Reject a non-affine preserved root matrix");

    AnimationHierarchyDefinition helper_only(7,{{8,"",-1,-1},{9,"",0,-1}},{});
    local={translation(100,100,100),translation(1,2,3)}; bones={root,identity()}; joints.clear();
    evaluate_animation_hierarchy(helper_only,local,bones,joints);
    check(bones[0]==root && bones[1]==local[1] && joints.empty(),"Zero-joint skeletons preserve root and evaluate helpers");
}

void malformed() {
    const auto rejects_fixture=[](auto mutate,const char* reason) { Fixture fixture; mutate(fixture); rejects([&]{fixture.definition();},reason); };
    rejects_fixture([](auto& f){f.scalar(f.skeleton,4,null_ref);},"Reject absent bone list");
    rejects_fixture([](auto& f){f.scalar(f.bones_list,2,0);},"Reject empty bone list");
    rejects_fixture([](auto& f){f.scalar(f.bones_list,2,1025);},"Bound bone allocation");
    rejects_fixture([](auto& f){f.scalar(f.bones_list,3,6);},"Require list memory matching capacity");
    rejects_fixture([](auto& f){f.contents(f.bones_list,{f.bones[0],f.bones[1],null_ref,f.bones[3],f.bones[4]});},"Reject null bone entries");
    rejects_fixture([](auto& f){f.contents(f.bones_list,{f.bones[0],f.bones[1],f.bones[1],f.bones[3],f.bones[4]});},"Reject repeated bone identities");
    rejects_fixture([](auto& f){f.scalar(f.bones[0],3,0);},"Require an unparented skipped root");
    rejects_fixture([](auto& f){f.scalar(f.bones[0],4,1);},"Do not silently skip a joint-bearing root");
    rejects_fixture([](auto& f){f.scalar(f.bones[2],3,2);},"Reject self-parent cycles");
    rejects_fixture([](auto& f){f.scalar(f.bones[2],3,3);},"Reject parents after children");
    rejects_fixture([](auto& f){f.scalar(f.bones[2],3,null_ref);},"Reject an additional unparented bone");
    rejects_fixture([](auto& f){f.scalar(f.bones[4],4,0);},"Reject duplicate joint ownership");
    rejects_fixture([](auto& f){f.scalar(f.bones[4],4,null_ref);},"Reject unowned joint entries");
    rejects_fixture([](auto& f){f.scalar(f.bones[4],4,3);},"Reject out-of-range joint output");
    rejects_fixture([](auto& f){f.scalar(f.bones[4],4,0xfffffffeu);},"Reject negative joint values other than the helper sentinel");
    rejects_fixture([](auto& f){f.scalar(f.skeleton,6,257);},"Bound inverse-joint allocation");
    rejects_fixture([](auto& f){f.scalar(f.skeleton,5,null_ref);},"Reject missing inverse-joint memory");
    rejects_fixture([](auto& f){f.scalar(f.skeleton,5,f.bones[0]);},"Reject an object in the inverse-memory field");
    rejects_fixture([](auto& f){f.records[f.inverse_memory].data.resize(128);},"Reject truncated inverse-joint matrix arrays");
    rejects_fixture([](auto& f){hierarchy_fixture::word(f.records[f.inverse_memory].data,0,0x7f800000);},"Reject nonfinite inverse matrix");
    rejects_fixture([](auto& f){hierarchy_fixture::word(f.records[f.inverse_memory].data,12,0x3f800000);},"Reject non-affine inverse matrix");
    rejects_fixture([](auto& f){f.text(f.bones[1],2,std::string("a\0b",3));},"Reject embedded null bone-name bytes");
    rejects_fixture([](auto& f){f.text(f.bones[1],2,std::string(4097,'x'));},"Bound bone names");
    Fixture fixture; auto doc=fixture.document();
    auto& bone=doc.objects[doc.references[fixture.bones[1]].value_index];
    const_cast<IgbFieldValue*>(doc.field(bone,3))->type_index=2;
    rejects([&]{decode_animation_hierarchy(doc,fixture.skeleton);},"Check parent field type separately from width");
    doc=fixture.document(); auto& skeleton=doc.objects[doc.references[fixture.skeleton].value_index];
    const_cast<IgbFieldValue*>(doc.field(skeleton,6))->size=2;
    rejects([&]{decode_animation_hierarchy(doc,fixture.skeleton);},"Check joint-count field width");
    doc=fixture.document(); doc.metadata.header.byte_order=IgbByteOrder::big;
    rejects([&]{decode_animation_hierarchy(doc,fixture.skeleton);},"Reject unverified byte order");
    fixture.text(fixture.bones[1],2,"same"); fixture.text(fixture.bones[4],2,"same");
    const auto repeated=fixture.definition();
    check(repeated.bones()[1].name=="same" && repeated.bones()[4].name=="same","Preserve duplicate names for separate original lookup semantics");
    const auto valid=fixture.definition();
    rejects([&]{AnimationHierarchyDefinition(null_ref,valid.bones(),valid.inverse_joint_matrices());},"Reject null skeleton identity");
    rejects([&]{AnimationHierarchyDefinition(7,{},{});},"Reject empty explicit skeleton definitions");
}

void original(const char* directory) {
    const auto resources=MountedResources::open(directory);
    const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto definition=decode_animation_hierarchy(doc,701);
    check(definition.bones().size()==25 && definition.inverse_joint_matrices().size()==20,"Decode original Agumon skeleton counts");
    std::vector<AnimationMatrix> local(25,identity()),bones(25,identity()),joints(20,identity());
    bones[0]=translation(123,234,345); const auto root=bones[0];
    evaluate_animation_hierarchy(definition,local,bones,joints);
    check(bones[0]==root,"Original root cache retains the supplied traversal matrix");
    for (std::size_t index=1;index<bones.size();++index) check(bones[index]==identity(),"Original identity locals produce identity bone caches");
    std::size_t inverse=0;
    for (const auto& bone:definition.bones()) if (bone.joint_index>=0)
        check(joints[static_cast<std::size_t>(bone.joint_index)]==definition.inverse_joint_matrices()[inverse++],
            "Original identity pose preserves serialized inverse matrices through explicit joint outputs");
    std::vector<std::array<float,3>> expected(25);
    for (std::size_t index=1;index<local.size();++index) {
        local[index]=translation(static_cast<float>(index),static_cast<float>(index%3),-static_cast<float>(index%5));
        for (std::size_t component=0;component<3;++component) {
            expected[index][component]=local[index][12+component];
            const auto parent=definition.bones()[index].parent;
            if (parent>0) expected[index][component]+=expected[static_cast<std::size_t>(parent)][component];
        }
    }
    evaluate_animation_hierarchy(definition,local,bones,joints);
    for (std::size_t index=1;index<bones.size();++index) for (std::size_t component=0;component<3;++component)
        check(bones[index][12+component]==expected[index][component],"Original helper hierarchy matches independent integer translation accumulation");
    for (const auto& matrix:joints) for (const auto element:matrix) check(std::isfinite(element),"Original translated joint cache remains finite");
}
}

int main(int argc,char** argv) {
    try {
        composition(); malformed(); if (argc>1) original(argv[1]);
        std::cout<<"Animation hierarchy checks passed: "<<checks<<'\n'; return 0;
    } catch (const std::exception& error) { std::cerr<<"Animation hierarchy test failure: "<<error.what()<<'\n'; return 1; }
}
