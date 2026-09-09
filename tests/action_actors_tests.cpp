#include "action_actor_fixture.hpp"
#include "dw4/resources.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
using action_actor_fixture::Fixture;
using action_actor_fixture::Bytes;
using action_actor_fixture::null_ref;
int checks=0;
void check(bool value,const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& call,const char* reason) {
    bool rejected=false; try { call(); } catch (const std::runtime_error&) { rejected=true; } check(rejected,reason);
}
bool identity(const AnimationMatrix& value) {
    for (std::size_t i=0;i<16;++i) if (value[i]!=(i%5==0 ? 1.0f : 0.0f)) return false;
    return true;
}
void lifecycle() {
    Fixture fixture; auto group=fixture.group();
    check(group.source()=="synthetic/action-actors.igb" && group.ram_reference()==fixture.ram &&
        group.combiner_reference()==fixture.combiner,"Retain qualified actor ownership");
    check(group.definitions().size()==3 && group.states().size()==3,"All three appearances retain ordered actors");
    for (std::size_t i=0;i<3;++i) {
        const auto& state=group.states()[i];
        check(state.reference==fixture.actors[i] && state.appearance_reference==fixture.appearances[i] &&
            !state.combiner_reference && state.bone_matrices.empty() && state.joint_matrices.empty(),"Prepared actor caches are unallocated");
    }
    rejects([&]{group.activate(fixture.combiner+1);},"Activation rejects a foreign combiner");
    check(group.activate(fixture.combiner)==3,"Activation allocates the complete initial actor prefix");
    for (const auto& state:group.states()) {
        check(state.combiner_reference==fixture.combiner && state.bone_matrices.size()==3 && state.joint_matrices.size()==1,
            "Use bone and joint counts independently");
        for (const auto& matrix:state.bone_matrices) check(identity(matrix),"Bone caches begin as identities");
        for (const auto& matrix:state.joint_matrices) check(identity(matrix),"Blend caches begin as identities");
    }
    group.state(0).bone_matrices[1][12]=7;
    check(group.activate(fixture.combiner)==0 && group.states()[0].bone_matrices[1][12]==7,"Already allocated first actor breaks the whole activation loop");
    auto copy=group; copy.state(0).bone_matrices[1][12]=9; copy.deactivate();
    check(group.states()[0].bone_matrices[1][12]==7 && group.states()[2].bone_matrices.size()==3,"Copies own independent cache memory");
    for (const auto& state:copy.states()) check(state.bone_matrices.empty() && state.joint_matrices.empty() &&
        state.bone_matrices.capacity()==0 && state.joint_matrices.capacity()==0 && state.combiner_reference==fixture.combiner,
        "NDSP releases every cache allocation and retains combiner links");
    check(copy.activate(fixture.combiner)==3 && identity(copy.states()[0].bone_matrices[1]),"Reactivation resets released caches");

    auto prefix=fixture.group();
    prefix.state(1).bone_matrices.resize(3); prefix.state(1).bone_matrices[0][12]=11;
    check(prefix.activate(fixture.combiner)==1,"A later allocated actor stops the complete remaining prefix");
    check(!prefix.states()[1].combiner_reference && prefix.states()[1].bone_matrices[0][12]==11 &&
        prefix.states()[2].bone_matrices.empty() && !prefix.states()[2].combiner_reference,
        "Loop break occurs before combiner assignment and leaves later actors unchanged");
    prefix.deactivate(); check(prefix.states()[1].bone_matrices.empty(),"NDSP visits caches beyond an activation barrier");

    auto partial=fixture.group(); partial.state(0).joint_matrices.resize(1); partial.state(0).joint_matrices[0][12]=5;
    check(partial.activate(fixture.combiner)==3 && identity(partial.states()[0].joint_matrices[0]),
        "A joint-only allocation is replaced when its bone cache is absent");
    auto invalid=fixture.group(); invalid.state(1).bone_matrices.resize(2);
    rejects([&]{invalid.activate(fixture.combiner);},"Reject inconsistent native cache dimensions");
    check(invalid.states()[0].bone_matrices.empty(),"Invalid native ownership is rejected before allocation");
    auto definitions=group.definitions(); definitions[1].reference=definitions[0].reference;
    rejects([&]{ActionActorGroup("source",fixture.ram,fixture.combiner,definitions);},"Reject aliased actor identity");
    rejects([&]{ActionActorGroup(std::string("a\0b",3),fixture.ram,fixture.combiner,group.definitions());},"Reject an embedded null in source ownership");
}
void malformed_graphs() {
    const auto rejects_fixture=[](auto mutate,const char* reason) { Fixture fixture; mutate(fixture); rejects([&]{fixture.group();},reason); };
    rejects_fixture([](auto& f){f.scalar(f.ram,13,null_ref);},"Reject missing actor list");
    rejects_fixture([](auto& f){f.scalar(f.actors_list,2,65);},"Bound actor counts before reading memory");
    rejects_fixture([](auto& f){f.scalar(f.actors_list,3,4);},"Require exact actor list capacity memory");
    rejects_fixture([](auto& f){f.contents(f.actors_list,{f.actors[0],null_ref,f.actors[2]});},"Reject null actor entries");
    rejects_fixture([](auto& f){f.contents(f.actors_list,{f.actors[0],f.actors[0],f.actors[2]});},"Reject aliased actor list entries");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],11,f.appearances[1]);},"Reject shared or mismatched appearances");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],8,f.combiner);},"Reject serialized active actor combiners");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],9,f.memory(Bytes(64)));},"Reject restored bone cache memory");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],10,f.memory(Bytes(64)));},"Reject restored blend cache memory");
    rejects_fixture([](auto& f){f.contents(f.modifiers[0],{f.actors[0]});},"Reject nonempty modifiers");
    rejects_fixture([](auto& f){f.contents(f.attributes,{f.actors[0]});},"Reject unowned serialized node attributes");
    rejects_fixture([](auto& f){f.scalar(f.skeleton,6,0);},"Reject zero-joint actor skeletons");
    rejects_fixture([](auto& f){f.scalar(f.skeleton,6,257);},"Bound skeleton joint allocation");
    rejects_fixture([](auto& f){f.scalar(f.bones_list,2,1025);},"Bound skeleton bone allocation");
    rejects_fixture([](auto& f){f.scalar(f.bones[1],4,0);},"Reject duplicate joint ownership");
    rejects_fixture([](auto& f){f.scalar(f.bones[2],4,null_ref);},"Reject a joint count without an owning bone");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],15,1);},"Reject force-cache traversal restoration");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],16,1);},"Reject a serialized used-cache flag");
    rejects_fixture([](auto& f){f.scalar(f.actors[0],19,0);},"Reject changed serialized update state");
    rejects_fixture([](auto& f){action_actor_fixture::word(f.records[f.actors[0]].values[14],48,0x7f800000u);},
        "Reject non-finite actor transforms");
    Fixture fixture; auto doc=fixture.document();
    auto& actor=doc.objects[doc.references[fixture.actors[0]].value_index];
    auto* matrix=const_cast<IgbFieldValue*>(doc.field(actor,14)); matrix->type_index=1;
    rejects([&]{decode_action_actor_group(doc,fixture.ram,fixture.combiner,"source");},"Validate matrix field type independently of its width");
    doc=fixture.document(); auto& ram=doc.objects[doc.references[fixture.ram].value_index];
    auto* field=const_cast<IgbFieldValue*>(doc.field(ram,13)); field->size=2;
    rejects([&]{decode_action_actor_group(doc,fixture.ram,fixture.combiner,"source");},"Validate reference field widths");
    doc=fixture.document(); doc.metadata.header.byte_order=IgbByteOrder::big;
    rejects([&]{decode_action_actor_group(doc,fixture.ram,fixture.combiner,"source");},"Reject unverified byte order");
    auto reordered=fixture; std::reverse(reordered.appearances.begin(),reordered.appearances.end());
    reordered.contents(reordered.appearances_list,reordered.appearances);
    check(reordered.group().definitions()[0].appearance_reference==fixture.appearances[0],"Appearance association follows explicit actor references");
    auto translated=fixture;
    action_actor_fixture::word(translated.records[fixture.actors[0]].values[14],48,std::bit_cast<std::uint32_t>(2.0f));
    check(translated.group().definitions()[0].transform[12]==2,"Actor traversal retains the authored finite transform");
}
void original(const char* directory) {
    const auto resources=MountedResources::open(directory);
    const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    const std::uint32_t combiners[]{6396,6463,6530,6596};
    for (std::size_t i=0;i<4;++i) {
        auto group=decode_action_actor_group(doc,250+static_cast<std::uint32_t>(i),combiners[i],"_common/p_agumon.igb");
        check(group.definitions().size()==3,"Original RAM retains all three LOD actors");
        check(group.activate(combiners[i])==3,"Original actor group allocates all appearances");
        for (const auto& state:group.states()) check(state.bone_matrices.size()==25 && state.joint_matrices.size()==20,
            "Original Agumon actor allocation uses 25 bones and 20 joints");
        group.deactivate(); check(group.activate(combiners[i])==3,"Original group supports NDSP then reactivation");
    }
}
}
int main(int argc,char** argv) {
    try {
        lifecycle(); malformed_graphs(); if (argc>1) original(argv[1]);
        std::cout<<"Action actor checks passed: "<<checks<<'\n'; return 0;
    } catch (const std::exception& error) { std::cerr<<"Action actor test failure: "<<error.what()<<'\n'; return 1; }
}
