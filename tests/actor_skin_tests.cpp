#include "actor_skin_fixture.hpp"
#include "dw4/actions.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using actor_skin_fixture::Fixture;
using actor_skin_fixture::identity;
using actor_skin_fixture::translated;
int checks{};
void check(bool value,const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    bool caught=false; try { operation(); } catch (const ActorSkinError&) { caught=true; } check(caught,reason);
}
void capture_and_deformation() {
    Fixture f; auto sources=f.library(); auto group=f.group(); auto combiner=f.caches(sources,f.combiner);
    const auto binding=decode_actor_skin_binding(f.document(),group,0,f.geometry);
    check(binding.source()==group.source() && binding.ram_reference()==f.ram && binding.combiner_reference()==f.combiner &&
        binding.actor_reference()==f.actor && binding.appearance_reference()==f.appearance && binding.skin_reference()==f.skin &&
        binding.selector_reference()==f.selector && binding.database_reference()==f.database && binding.skeleton_reference()==f.skeleton &&
        binding.geometry_reference()==f.geometry,"Binding retains every explicit actor-to-geometry identity");
    rejects([&]{capture_actor_skin(binding,group,combiner);},"Unattached actors cannot capture shared caches");
    group.activate(f.combiner);
    const auto before_bones=combiner.bone_matrices(),before_joints=combiner.joint_matrices();
    const auto frame=capture_actor_skin(binding,group,combiner);
    check(frame.model_view==translated(10) && frame.palette_matrices==std::vector<AnimationMatrix>{translated(200),translated(100),translated(200)},
        "Capture retains root and copies permuted duplicate palette entries in exact authored order");
    check(frame.joint_positions.size()==2 && frame.joint_positions[0].x==30 && frame.joint_positions[0].y==31 &&
        frame.joint_positions[1].x==20 && frame.joint_positions[1].y==21,
        "Joint positions use owning bone caches even when joint output indices are permuted");
    const auto pose=evaluate_actor_skin(binding,frame);
    check(pose.positions.size()==3 && pose.positions[0].x==202 && pose.positions[1].x==104 && pose.positions[2].x==201 &&
        pose.positions[1].y==1 && pose.joint_positions[0].x==30,"CPU deformation selects each captured palette matrix and leaves root composition to drawing");
    check(combiner.bone_matrices()==before_bones && combiner.joint_matrices()==before_joints && !combiner.cache_valid() &&
        !combiner.hierarchy_valid() && combiner.states().empty(),"Capture and CPU deformation neither sample nor update hierarchy state");
    auto copy=binding; auto copied_frame=frame; copied_frame.palette_matrices[0][12]=7;
    check(evaluate_actor_skin(copy,copied_frame).positions[0].x==9 && evaluate_actor_skin(binding,frame).positions[0].x==202,
        "Binding copies share immutable identity while frame matrix copies own independent values");
    auto& actor=group.state(0); actor.cache_used=true;
    actor.bone_matrices={translated(77),translated(88,89),translated(99,100)};
    actor.joint_matrices={translated(-10),translated(-20)};
    const auto private_frame=capture_actor_skin(binding,group,combiner);
    check(private_frame.model_view==translated(77) && private_frame.palette_matrices[0]==translated(-20) && private_frame.joint_positions[0].x==99,
        "Actor cache_used selects both private matrix arrays");
    actor.joint_matrices[1][12]=-99;
    check(private_frame.palette_matrices[0][12]==-20 && frame.palette_matrices[0][12]==200,"Captured matrices survive later private cache mutations");
    group.deactivate();
    rejects([&]{capture_actor_skin(binding,group,combiner);},"Retained NDSP combiner links do not substitute for freed actor arrays");
    check(evaluate_actor_skin(binding,frame).positions[0].x==202,"A value-owned capture remains usable after actor deactivation");
}
void ownership_and_validation() {
    Fixture f; auto sources=f.library(); auto group=f.group(); group.activate(f.combiner); auto combiner=f.caches(sources,f.combiner);
    const auto binding=decode_actor_skin_binding(f.document(),group,0,f.geometry);
    auto alien_group=f.group("other/source.igb"); alien_group.activate(f.combiner);
    rejects([&]{capture_actor_skin(binding,alien_group,combiner);},"Actor groups require matching source qualification");
    auto alien_sources=f.library("other/source.igb"); auto alien_combiner=f.caches(alien_sources,f.combiner);
    rejects([&]{capture_actor_skin(binding,group,alien_combiner);},"Combiner sources cannot silently cross actor bindings");
    auto wrong_ref=f.caches(sources,f.combiner+1); auto unqualified=f.caches(sources,{});
    rejects([&]{capture_actor_skin(binding,group,wrong_ref);},"Different RAM combiners cannot share a numeric skeleton binding implicitly");
    rejects([&]{capture_actor_skin(binding,group,unqualified);},"Native caches require explicit combiner identity for actor capture");
    ActionActorGroup wrong_ram(group.source(),f.ram+1,f.combiner,group.definitions()); wrong_ram.activate(f.combiner);
    rejects([&]{capture_actor_skin(binding,wrong_ram,combiner);},"Actor capture requires the bound RAM identity");
    auto broken=group; broken.state(0).appearance_reference++;
    rejects([&]{capture_actor_skin(binding,broken,combiner);},"Mutable actor appearance corruption rejects");
    broken=group; broken.state(0).joint_matrices.clear();
    rejects([&]{capture_actor_skin(binding,broken,combiner);},"Missing private activation arrays reject even when shared arrays are selected");
    broken=group; broken.state(0).cache_used=true; broken.state(0).bone_matrices[1][3]=1;
    rejects([&]{capture_actor_skin(binding,broken,combiner);},"Selected private caches must remain finite affine matrices");
    auto hierarchy=decode_animation_hierarchy(f.document(),f.skeleton);
    const auto changed=[&](std::vector<AnimationHierarchyBone> bones,std::vector<AnimationMatrix> inverses) {
        return AnimationCombiner(sources,std::vector<AnimationMatrix>(3,identity()),
            AnimationHierarchyDefinition(f.skeleton,std::move(bones),std::move(inverses)),
            std::vector<AnimationMatrix>(3,identity()),std::vector<AnimationMatrix>(2,identity()),f.combiner);
    };
    auto bones=hierarchy.bones(); bones[2].parent=0; auto wrong_parent=changed(bones,hierarchy.inverse_joint_matrices());
    rejects([&]{capture_actor_skin(binding,group,wrong_parent);},"Equal skeleton references do not hide changed bone parents");
    bones=hierarchy.bones(); bones[1].joint_index=0; bones[2].joint_index=1; auto wrong_joints=changed(bones,hierarchy.inverse_joint_matrices());
    rejects([&]{capture_actor_skin(binding,group,wrong_joints);},"Complete joint mapping must match the combiner hierarchy");
    auto inverses=hierarchy.inverse_joint_matrices(); inverses[0][12]+=1; auto wrong_inverse=changed(hierarchy.bones(),inverses);
    rejects([&]{capture_actor_skin(binding,group,wrong_inverse);},"Inverse matrices compare against sequential serialized hierarchy input");
    const auto frame=capture_actor_skin(binding,group,combiner);
    const auto alien_binding=decode_actor_skin_binding(f.document(),group,0,f.geometry);
    rejects([&]{evaluate_actor_skin(alien_binding,frame);},"Frames cannot cross independently constructed bindings with coincident numeric identities");
    auto bad_frame=frame; bad_frame.palette_matrices.pop_back();
    rejects([&]{evaluate_actor_skin(binding,bad_frame);},"CPU deformation rejects truncated captured palettes");
    bad_frame=frame; bad_frame.palette_matrices[0][0]=std::numeric_limits<float>::infinity();
    rejects([&]{evaluate_actor_skin(binding,bad_frame);},"CPU deformation rejects unusable captured matrices");
    bad_frame=frame; bad_frame.palette_matrices[0][0]=std::numeric_limits<float>::max();
    rejects([&]{evaluate_actor_skin(binding,bad_frame);},"CPU deformation reports overflow without publishing a partial pose");
    check(evaluate_actor_skin(binding,frame).positions[0].x==202,"Rejected frame evaluations preserve the valid captured input");
    auto invalid=f.decoded_skin(); invalid.palette_to_joint[0]=2;
    rejects([&]{ActorSkinBinding(group.source(),f.ram,f.combiner,0,f.actor,f.appearance,invalid);},"Native bindings reject out-of-range palettes");
    invalid=f.decoded_skin(); invalid.influences[0].weight=0.5f;
    rejects([&]{ActorSkinBinding(group.source(),f.ram,f.combiner,0,f.actor,f.appearance,invalid);},"Native bindings retain the full-weight vertex profile");
    invalid=f.decoded_skin(); invalid.joints[0].inverse_bind[12]=0.1;
    rejects([&]{ActorSkinBinding(group.source(),f.ram,f.combiner,0,f.actor,f.appearance,invalid);},"Native inverses must retain exact serialized float components");
}
void malformed_graphs() {
    Fixture f; auto group=f.group();
    const auto reject=[&](const Fixture& value){rejects([&]{decode_actor_skin_binding(value.document(),group,0,value.geometry);},"Malformed explicit actor skin graph rejects");};
    for (std::size_t slot=0;slot<4;++slot) { auto bad=f; bad.contents(bad.extras[slot],{bad.skin}); reject(bad); }
    auto bad=f; bad.scalar(bad.appearance,3,animation_fixture::null_ref); reject(bad);
    bad=f; bad.contents(bad.actor_entries,{}); reject(bad);
    bad=f; bad.scalar(bad.combiner,3,bad.skin); reject(bad);
    bad=f; bad.scalar(bad.actor,11,bad.skin); reject(bad);
    rejects([&]{decode_actor_skin_binding(f.document(),group,1,f.geometry);},"Actor selection does not fall back to the first actor");
    rejects([&]{decode_actor_skin_binding(f.document(),group,0,f.skin);},"Geometry selection must be explicit and typed");
    auto doc=f.document(); auto& appearance=doc.objects[doc.references[f.appearance].value_index];
    const_cast<IgbFieldValue*>(doc.field(appearance,3))->type_index=1;
    rejects([&]{decode_actor_skin_binding(doc,group,0,f.geometry);},"Main-skin reference metadata is validated");
}
void original(const std::filesystem::path& directory) {
    const auto resources=MountedResources::open(directory); const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    const std::string source="_common/p_agumon.igb"; const auto program=decode_action_program(doc,546,source);
    const auto base_skin=decode_igb_skinning(doc,5665); std::vector<AnimationClip> clips;
    for (const auto& info:list_igb_animations(doc,base_skin)) clips.push_back(decode_igb_animation(doc,base_skin,info.reference_index));
    AnimationSourceRuntime sources(source,std::move(clips)); sources.initialize_action_offset();
    std::size_t captures=0,vertices=0;
    for (const auto& ram:program.rams()) {
        auto group=decode_action_actor_group(doc,ram.reference,ram.combiner_reference,source);
        auto combiner=decode_animation_actor_combiner(doc,sources,ram.combiner_reference); group.activate(ram.combiner_reference);
        combiner.add_base(sources,sources.clips().front().info.reference_index,100000000);
        for (std::size_t actor=0;actor<group.definitions().size();++actor) {
            const auto& appearance=doc.object(group.definitions()[actor].appearance_reference);
            const auto& skin=doc.object(*doc.field_reference(appearance,3));
            const auto& selector=doc.object(*doc.field_reference(skin,3));
            const auto children=doc.object_list(*doc.field_reference(selector,7));
            check(children.size()==1 && children[0].has_value(),"Original main skin selects one direct geometry node");
            const auto& owner=doc.object(*children[0]); const auto attrs=doc.object_list(*doc.field_reference(owner,8));
            std::vector<std::uint32_t> geometries;
            for (const auto attr:attrs) if (attr && doc.is_a(doc.object(*attr),"igGeometryAttr1_5")) geometries.push_back(*attr);
            check(geometries.size()==1,"Original explicit actor path has one geometry attribute");
            const auto binding=decode_actor_skin_binding(doc,group,actor,geometries[0]);
            for (std::uint64_t frame=0;frame<3;++frame) {
                group.traverse(combiner,sources,actor,translated(float(actor+10)),identity(),1000000000+frame*16666667);
                const auto captured=capture_actor_skin(binding,group,combiner); const auto pose=evaluate_actor_skin(binding,captured);
                check(captured.model_view==translated(float(actor+10)) && pose.positions.size()==binding.skin().source_positions.size(),
                    "Original actor captures retain root and deform every owned vertex");
                for (std::size_t p=0;p<captured.palette_matrices.size();++p)
                    check(captured.palette_matrices[p]==combiner.joint_matrices()[binding.skin().palette_to_joint[p]],"Original palette capture follows authored joint indices");
                for (const auto& point:pose.positions) check(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z),"Original CPU skin positions remain finite");
                ++captures; vertices+=pose.positions.size();
            }
        }
    }
    std::cout<<"Original actor skin captures="<<captures<<" vertices="<<vertices<<" players=4 appearances=3\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        capture_and_deformation(); ownership_and_validation(); malformed_graphs();
        if (args.size()==2) original(args[1]); else if (args.size()!=1) throw std::runtime_error("Usage: dw4_actor_skin_tests [EXTRACTED_ROOT]");
        std::cout<<checks<<" actor skin checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Actor skin failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
