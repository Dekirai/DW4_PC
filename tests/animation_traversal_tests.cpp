#include "action_fixture.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using Fixture=action_fixture::Fixture;
int checks{};
void check(bool value,const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    bool caught=false; try { operation(); } catch (const std::runtime_error&) { caught=true; } check(caught,reason);
}
template<class F> void rejects_action(F&& operation,std::string_view message,const char* reason) {
    bool caught=false;
    try { operation(); } catch (const ActionError& error) { caught=std::string_view(error.what()).find(message)!=std::string_view::npos; }
    check(caught,reason);
}
AnimationMatrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
AnimationMatrix translated(float x,float y=0,float z=0) { auto result=identity(); result[12]=x; result[13]=y; result[14]=z; return result; }
std::uint32_t memory(Fixture& fixture,const std::vector<AnimationMatrix>& values) {
    animation_fixture::Bytes bytes;
    for (const auto& matrix:values) for (const auto value:matrix) animation_fixture::push32(bytes,std::bit_cast<std::uint32_t>(value));
    return fixture.memory(std::move(bytes));
}
Fixture fixture() {
    Fixture result; result.action("empty",{});
    for (const auto combiner:result.combiners) {
        result.scalar(combiner,12,memory(result,{translated(41),translated(73)}));
        result.scalar(combiner,13,memory(result,{translated(97)}));
    }
    return result;
}
ActionRuntime runtime(const Fixture& f,const AnimationSourceRuntime& sources,std::int32_t ram=0,AnimationMatrix transform=identity()) {
    auto program=f.program();
    ActionActorGroup actors(program.source(),f.rams[ram],f.combiners[ram],
        {{900,950,2,1,transform},{901,951,2,1},{902,952,2,1}});
    return {std::move(program),sources,ram,decode_animation_actor_combiner(f.document(),sources,f.combiners[ram]),std::move(actors)};
}
void caching() {
    auto f=fixture(); auto sources=f.library();
    auto combiner=decode_animation_actor_combiner(f.document(),sources,f.combiners[0]);
    check(combiner.hierarchy() && !combiner.hierarchy_valid() && combiner.bone_matrices()[1][12]==73 &&
        combiner.joint_matrices()[0][12]==97,"Decoder retains original shared matrices before any sampling");
    combiner.traverse(sources,translated(10),1000000000);
    check(combiner.hierarchy_valid() && !combiner.cache_valid() && combiner.bone_matrices()[0][12]==10 && combiner.bone_matrices()[1]==identity(),
        "Empty local evaluation still computes a missing hierarchy from retained local matrices");
    auto saved=combiner.bone_matrices(); auto joints=combiner.joint_matrices();
    combiner.traverse(sources,translated(20),2000000000);
    check(combiner.bone_matrices()[0][12]==20 && combiner.bone_matrices()[1]==saved[1] && combiner.joint_matrices()==joints && combiner.hierarchy_valid(),
        "No-state evaluation retains hierarchy validity while overwriting root at every traversal");
    combiner.add_base(sources,f.clip,100000000);
    check(combiner.hierarchy_valid(),"Adding an animation does not pretend that shared matrices have already changed");
    combiner.evaluate(sources,1000000000);
    check(!combiner.hierarchy_valid() && combiner.bone_matrices()[1]==saved[1],"New successful local sampling invalidates but does not overwrite hierarchy caches");
    combiner.traverse(sources,translated(30),1000000000);
    check(combiner.hierarchy_valid() && combiner.bone_matrices()[1]==combiner.cached_matrices()[1],"Parent-zero bone copies local and excludes traversal root");
    saved=combiner.bone_matrices(); joints=combiner.joint_matrices();
    combiner.pose(sources,f.data,1000000000);
    check(combiner.hierarchy_valid() && combiner.bone_matrices()==saved,"Same-time direct pose preserves an already evaluated hierarchy");
    combiner.pose(sources,f.data,1016666667);
    check(!combiner.hierarchy_valid() && combiner.bone_matrices()==saved,"Direct pose at new time invalidates shared hierarchy without recomputing it");
    combiner.traverse(sources,translated(40),1016666667);
    saved=combiner.bone_matrices(); joints=combiner.joint_matrices(); auto copy=combiner;
    combiner.clear();
    check(combiner.hierarchy_valid() && combiner.bone_matrices()==saved && combiner.joint_matrices()==joints,
        "NDSP combiner clear preserves shared hierarchy arrays and their validity");
    combiner.traverse(sources,translated(50),2000000000);
    check(combiner.hierarchy_valid() && combiner.bone_matrices()[1]==saved[1] && copy.bone_matrices()[0][12]==40,
        "Cleared traversal and independently copied hierarchy ownership preserve nonroot cache state");
    combiner.add_base(sources,f.clip,100000000); combiner.traverse(sources,translated(60),2000000000);
    check(combiner.cache_valid() && combiner.hierarchy_valid(),"Reactivation samples and recomputes the preserved shared hierarchy");
    auto non_affine=identity(); non_affine[3]=1; saved=combiner.bone_matrices();
    rejects([&]{combiner.traverse(sources,non_affine,2000000000);},"A same-time hierarchy cache hit still rejects a non-affine traversal root");
    check(combiner.bone_matrices()==saved && combiner.hierarchy_valid(),"Rejected cached root preserves hierarchy matrices and validity");
    auto bad=translated(std::numeric_limits<float>::infinity()); saved=combiner.bone_matrices();
    rejects([&]{combiner.traverse(sources,bad,3000000000);},"Reject a non-finite traversal root");
    check(combiner.bone_matrices()==saved && combiner.hierarchy_valid(),"Failed traversal preserves cached matrices and validity");
    auto local_only=decode_animation_combiner(f.document(),sources,f.combiners[0]);
    rejects([&]{local_only.traverse(sources,identity(),0);},"Missing shared caches cannot be manufactured by traversal");
    auto alien=f.library(); rejects([&]{combiner.traverse(alien,identity(),0);},"Traversal validates source library identity");
}
void actor_queries_and_time() {
    auto f=fixture(); f.scalar(f.nodes[0],13,std::bit_cast<std::uint32_t>(0.5f));
    f.records[f.nodes[0]].values[14]=animation_fixture::words({262144,0});
    auto sources=f.library(); ActionServices service; service.broadcast=[](const auto&){};
    auto actor_transform=identity(); actor_transform[0]=2; actor_transform[5]=3; actor_transform[12]=7;
    auto actor=runtime(f,sources,0,actor_transform);
    check(!actor.bone_matrix("missing"),"Inactive bone query returns no value before inspecting the requested name");
    rejects([&]{actor.traverse_actor(sources,0,identity(),identity(),1000000000);},"Actor traversal requires original activation");
    actor.dispatch(sources,service,"empty",1000000000);
    check((*actor.bone_matrix("bOnE 1"))[12]==73 && !actor.combiner().cache_valid(),"Bone query is ASCII case-insensitive and reads stored matrices without sampling");
    const auto first_query=actor.bone_matrix("Bone 0");
    const auto view=translated(11), inverse=translated(-5);
    const auto local_time=actor.node_time(1000000000);
    actor.traverse_actor(sources,0,view,inverse,1000000000);
    check(actor.node_clock().local_time==std::bit_cast<std::int64_t>(local_time) &&
        actor.combiner().states()[0].clock.local_time==std::bit_cast<std::int64_t>(local_time&~std::uint64_t(262143)),
        "Actor sampling uses node-adjusted traversal time once and publishes its local timestamp");
    check(actor.actors()->states()[0].model_view==view && actor.actors()->states()[0].inverse_view==inverse &&
        !actor.actors()->states()[0].cache_used && actor.actors()->states()[0].bone_matrices[0]==identity(),
        "Ordinary actor stores traversal attributes and leaves its private buffers unused");
    auto root=actor.bone_matrix("Bone 0");
    check((*root)[0]==2 && (*root)[5]==3 && (*root)[12]==18 && (*first_query)[12]==41,
        "Root composes model-view then authored actor transform and queries return owned copies");
    const auto body=actor.bone_matrix("Bone 1");
    actor.traverse_actor(sources,1,translated(29),inverse,1000000000);
    check((*actor.bone_matrix("Bone 0"))[12]==29 && actor.bone_matrix("Bone 1")==body && actor.combiner().hierarchy_valid(),
        "A second LOD actor changes shared root only and the first actor's query reads that shared cache");
    const auto prior_node=actor.node_clock().local_time;
    const auto prior_view=actor.actors()->states()[0].model_view;
    const auto prior_bones=actor.combiner().bone_matrices();
    auto overflow=identity(); overflow[0]=std::numeric_limits<float>::max();
    rejects([&]{actor.traverse_actor(sources,0,overflow,inverse,2000000000);},"Unusable actor multiplication is an explicit finite-result boundary");
    check(actor.node_clock().local_time==prior_node && actor.actors()->states()[0].model_view==prior_view &&
        actor.combiner().bone_matrices()==prior_bones && actor.combiner().hierarchy_valid(),"Failed native traversal does not publish partial node, actor or shared cache state");
    rejects([&]{actor.bone_matrix("no such bone");},"Unknown active bone names reject original out-of-bounds pointer arithmetic");
    rejects([&]{actor.traverse_actor(sources,3,view,inverse,0);},"Reject an actor outside the owning RAM");
    actor.freeze(true,1000000000); const auto frozen=actor.node_time(2000000000);
    actor.traverse_actor(sources,0,view,inverse,2000000000);
    check(actor.node_clock().local_time==std::bit_cast<std::int64_t>(frozen),"Frozen actor traversal retains its node phase");
    const auto matrices=actor.combiner().bone_matrices();
    actor.dispatch(sources,service,"NDSP",2000000000);
    check(!actor.bone_matrix("Bone 0") && actor.combiner().bone_matrices()==matrices && actor.combiner().hierarchy_valid(),
        "NDSP gates queries but retains shared traversal caches");
    actor.dispatch(sources,service,"empty",3000000000);
    check(actor.combiner().bone_matrices()==matrices && actor.node_clock().time_scale==0 && actor.bone_matrix("Bone 1"),
        "Reactivation preserves query caches and frozen node speed until a new traversal");

    auto duplicate=f; duplicate.text(duplicate.bones[0],2,"target"); duplicate.text(duplicate.bones[1],2,"TARGET");
    auto duplicates=runtime(duplicate,sources); duplicates.dispatch(sources,service,"empty",1);
    check((*duplicates.bone_matrix("TaRgEt"))[12]==41,"Bone lookup keeps the first ASCII-folded match");
}
void malformed_shared_graphs() {
    auto f=fixture(); auto sources=f.library();
    const auto original=f.document();
    const auto reject=[&](const Fixture& value){rejects([&]{decode_animation_actor_combiner(value.document(),sources,value.combiners[0]);},"Reject malformed prepared shared matrix graph");};
    auto bad=f; bad.scalar(bad.combiners[0],12,animation_fixture::null_ref); reject(bad);
    bad=f; bad.scalar(bad.combiners[0],13,animation_fixture::null_ref); reject(bad);
    bad=f; bad.scalar(bad.combiners[0],11,1); reject(bad);
    bad=f; bad.scalar(bad.combiners[0],12,memory(bad,{identity()})); reject(bad);
    bad=f; auto invalid=identity(); invalid[0]=std::numeric_limits<float>::quiet_NaN(); bad.scalar(bad.combiners[0],13,memory(bad,{invalid})); reject(bad);
    bad=f; bad.scalar(bad.combiners[0],12,*original.field_reference(original.object(f.combiners[0]),8)); reject(bad);
    bad=f; bad.scalar(bad.combiners[0],13,*original.field_reference(original.object(f.skeleton),5)); reject(bad);
    auto doc=f.document(); auto& object=doc.objects[doc.references[f.combiners[0]].value_index];
    const_cast<IgbFieldValue*>(doc.field(object,12))->type_index=2;
    rejects([&]{decode_animation_actor_combiner(doc,sources,f.combiners[0]);},"Shared cache references require memory metadata, not an object-reference type");
    doc=f.document();
    const auto bone_memory=*doc.field_reference(doc.object(f.combiners[0]),12);
    const auto local_memory=*doc.field_reference(doc.object(f.combiners[0]),8);
    doc.references[bone_memory].value_index=doc.references[local_memory].value_index;
    rejects([&]{decode_animation_actor_combiner(doc,sources,f.combiners[0]);},"Distinct references cannot hide aliased mutable cache ownership");
}
void actor_cache_ownership() {
    auto f=fixture(); auto sources=f.library(); const auto original=f.document();
    const auto make_actors=[&]{return ActionActorGroup(action_fixture::source_name,f.rams[0],f.combiners[0],{{900,950,2,2}});};
    rejects_action([&]{ActionRuntime(f.program(),sources,0,
        decode_animation_actor_combiner(original,sources,f.combiners[0]),make_actors());},"Actor joint allocation",
        "Native actor joint count must match an owned hierarchy");
    ActionRuntime local_only(f.program(),sources,0,decode_animation_combiner(original,sources,f.combiners[0]),make_actors());
    ActionServices service; service.broadcast=[](const auto&){};
    local_only.dispatch(sources,service,"empty",1000000000);
    check(local_only.actors()->states()[0].joint_matrices.size()==2,"Local-only lifecycle retains its explicit actor allocation counts");

    const auto reject=[&](const IgbDocument& doc,std::string_view message) {
        rejects_action([&]{decode_action_actor_runtime(doc,f.program(),sources,0);},message,
            "Full actor factory rejects aliased cache ownership before decoding incomplete actor fixtures");
    };
    // The fixture intentionally stops at actor deserialization. Passing this
    // ownership stage must reach that specific later boundary, even though all
    // four combiners share the same immutable inverse-joint input.
    bool actor_boundary=false;
    try { decode_action_actor_runtime(original,f.program(),sources,0); }
    catch (const ActionActorError&) { actor_boundary=true; }
    check(actor_boundary,"Distinct mutable caches with shared immutable inverses pass ownership validation");
    for (const auto slot:std::array<std::uint16_t,3>{8,12,13}) {
        auto bad=f;
        bad.scalar(bad.combiners[1],slot,*original.field_reference(original.object(f.combiners[0]),slot));
        reject(bad.document(),"Aliased mutable ACTION cache ownership");
    }
    auto bad=f;
    bad.scalar(bad.combiners[1],12,*original.field_reference(original.object(f.combiners[0]),8));
    reject(bad.document(),"Aliased mutable ACTION cache ownership");
    auto doc=original;
    const auto local=*doc.field_reference(doc.object(f.combiners[0]),8);
    const auto other_bones=*doc.field_reference(doc.object(f.combiners[1]),12);
    doc.references[other_bones].value_index=doc.references[local].value_index;
    reject(doc,"Aliased mutable ACTION cache ownership");

    bad=f;
    const auto other_skeleton=bad.add(animation_fixture::skeleton_type);
    bad.records[other_skeleton].values=bad.records[bad.skeleton].values;
    bad.scalar(other_skeleton,5,*original.field_reference(original.object(f.combiners[0]),13));
    bad.scalar(bad.combiners[1],3,other_skeleton);
    reject(bad.document(),"Mutable ACTION caches cannot alias immutable inverse-joint input");
}
void original(const std::filesystem::path& directory) {
    const auto resources=MountedResources::open(directory); const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    const std::string source="_common/p_agumon.igb"; const auto program=decode_action_program(doc,546,source);
    const auto skin=decode_igb_skinning(doc,5665); std::vector<AnimationClip> clips;
    for (const auto& info:list_igb_animations(doc,skin)) clips.push_back(decode_igb_animation(doc,skin,info.reference_index));
    AnimationSourceRuntime sources(source,std::move(clips)); sources.initialize_action_offset();
    ActionServices service; service.broadcast=[](const auto&){};
    const std::array<std::string,4> runs{"RUNND1","RUNNS1","RUNNA1","RUNNG1"}, waits{"WAITD1","WAITS1","WAITA1","WAITG1"};
    std::vector<ActionRuntime> players;
    for (int i=0;i<4;++i) players.push_back(decode_action_actor_runtime(doc,program,sources,i));
    std::size_t traversals=0, queries=0;
    for (std::uint64_t frame=0;frame<60;++frame) for (std::size_t i=0;i<4;++i) {
        auto& actor=players[i]; const auto time=1000000000+frame*16666667;
        if (!frame || frame==22 || frame==40) actor.dispatch(sources,service,runs[i],time);
        if (frame==20) actor.dispatch(sources,service,waits[i],time);
        if (frame==10 || frame==15) actor.freeze(frame==10,time);
        actor.update(sources,service,time);
        if (frame%3==0) actor.pose(sources,skin,time);
        std::vector<AnimationMatrix> first;
        for (std::size_t lod=0;lod<3;++lod) {
            actor.traverse_actor(sources,lod,translated(float(i*100+lod),float(frame),-5),identity(),time); ++traversals;
            check(actor.combiner().hierarchy_valid(),"Original actor traversal produces its shared hierarchy");
            if (!lod) first=actor.combiner().bone_matrices();
            else for (std::size_t bone=1;bone<first.size();++bone)
                check(actor.combiner().bone_matrices()[bone]==first[bone],"Original LODs share the same-time nonroot bone cache");
            for (const auto& bone:actor.combiner().hierarchy()->bones()) {
                const auto matrix=actor.bone_matrix(bone.name); ++queries;
                check(matrix.has_value(),"Original named bone query returns a cached copy");
                for (const auto value:*matrix) check(std::isfinite(value),"Original cached bone matrix remains finite");
            }
            for (const auto& matrix:actor.combiner().joint_matrices()) for (const auto value:matrix)
                check(std::isfinite(value),"Original shared joint matrix remains finite");
        }
        if (frame==39) {
            const auto saved=actor.combiner().bone_matrices(); actor.dispatch(sources,service,"NDSP",time);
            check(!actor.active() && actor.combiner().bone_matrices()==saved && actor.combiner().hierarchy_valid(),"Original NDSP retains all shared hierarchy values");
        }
    }
    std::cout<<"Original actor traversals="<<traversals<<" cached_bone_queries="<<queries<<" players=4 appearances=3\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        caching(); actor_queries_and_time(); malformed_shared_graphs(); actor_cache_ownership();
        if (args.size()==2) original(args[1]); else if (args.size()!=1) throw std::runtime_error("Usage: dw4_animation_traversal_tests [EXTRACTED_ROOT]");
        std::cout<<checks<<" animation traversal checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Traversal failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
