#include "dw4/actor_transform.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/assets.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
struct Fixture {
    pf::RuntimeFixture source;
    std::uint32_t input=source.add(pf::object),other_input=source.add(pf::object);
    std::uint32_t words=source.memory(pf::words({input,other_input,pf::null_ref,source.nodes[0],0}));
    IgbDocument document(bool null_input=false,bool copy=false,bool derived=false) const {
        auto doc=source.document();
        auto type=[&](std::string name,std::int32_t parent,std::uint16_t slots) {
            IgbObjectType value;value.name=std::move(name);value.parent_index=parent;value.slots=slots;
            const auto index=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(value));return index;
        };
        const auto input_type=type("igTransformSource",pf::object,2),transform_type=type("igTransform",pf::group,12);
        const auto selected_type=derived ? type("UnverifiedTransform",static_cast<std::int32_t>(transform_type),12):transform_type;
        auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        object(input).type_index=input_type;object(other_input).type_index=input_type;
        const auto offset=doc.memories.at(doc.references.at(words).value_index).offset;
        for(const auto ref:source.nodes) {
            auto& node=object(ref);node.type_index=selected_type;
            std::erase_if(node.fields,[](const auto& field){return field.slot>8;});
            node.fields.push_back({9,1,offset+16,4});node.fields.push_back({11,2,offset+(null_input ? 8:0),4});
        }
        if(copy) {
            auto& node=object(source.nodes[1]);node.type_index=type("beCopyTransform",static_cast<std::int32_t>(transform_type),13);
            node.fields.push_back({12,2,offset+12,4});
        }
        return doc;
    }
    std::shared_ptr<ActorSceneResource> scene(bool null_input=false,bool copy=false,bool derived=false) const {
        return std::make_shared<ActorSceneResource>(document(null_input,copy,derived),"synthetic/transform-input",61);
    }
};
void canonical_input_owners() {
    Fixture fixture;auto doc=fixture.document();const auto alias=static_cast<std::uint32_t>(doc.references.size());
    doc.references.push_back(doc.references.at(fixture.input));
    auto scene=std::make_shared<ActorSceneResource>(std::move(doc),"synthetic/transform-alias",62);
    const auto input=scene->models().object(alias);
    const auto first=scene->mutable_node(fixture.source.nodes[0]),second=scene->mutable_node(fixture.source.nodes[1]);
    check(first->transform_input_field_available() && first->transform_input_owner()==input && second->transform_input_owner()==input &&
        input==scene->models().object(fixture.input),"Transform input did not use the canonical shared model-object registry.");
    const auto replacement=scene->models().object(fixture.other_input);first->set_transform_input_owner(replacement);
    check(first->transform_input_owner()==replacement && second->transform_input_owner()==input,"Transform input slot mutation changed another node's owner.");
    const auto wrong_type=scene->models().object(fixture.source.ones[1]);
    rejects([&]{first->set_transform_input_owner(wrong_type);},"Runtime transform input accepted an unrelated model owner.");
    check(first->transform_input_owner()==replacement,"Rejected input type mutation changed the prior owner.");
    input->name="retained input";second->set_transform_input_owner({});
    check(!second->transform_input_owner() && first->transform_input_owner()==replacement,"Explicit null input did not preserve other node ownership.");
    const auto original_document=input->document_owner();scene.reset();
    check(first->transform_input_owner()==replacement && input->document_owner()==original_document && input->name=="retained input",
        "Captured input or node owner required the resource facade to survive.");
    auto null_scene=fixture.scene(true,true);const auto copy=null_scene->mutable_node(fixture.source.nodes[1]);
    check(copy->transform_input_field_available() && !copy->transform_input_owner() && copy->source_transform_owner() &&
        copy->source_transform_owner()==null_scene->mutable_node(fixture.source.nodes[0]),
        "Copy transform confused inherited null slot11 with the distinct source link at slot12.");
}
void schema_boundaries() {
    Fixture fixture;
    auto missing=fixture.document();auto& node=missing.objects.at(missing.references.at(fixture.source.nodes[0]).value_index);
    std::erase_if(node.fields,[](const auto& field){return field.slot==11;});
    auto absent=std::make_shared<ActorSceneResource>(std::move(missing),"synthetic/transform-missing",1);
    const auto owner=absent->mutable_node(fixture.source.nodes[0]);
    check(!owner->transform_input_field_available(),"Missing transform slot was replaced with an invented null input.");
    rejects([&]{owner->transform_input_owner();},"Missing transform input field was read as null.");
    rejects([&]{owner->set_transform_input_owner({});},"Missing transform input field was writable.");
    ActorTransformUpdateInvocation call(owner,17);rejects([&]{call.resume();},"Exact transform update skipped its missing physical input field.");
    check(call.faulted(),"Missing input field failed without poisoning its invocation.");
    for(unsigned mode=0;mode<3;++mode) {
        auto bad=fixture.document();auto& value=bad.objects.at(bad.references.at(fixture.source.nodes[0]).value_index);
        for(auto& field:value.fields)if(field.slot==11) {
            if(mode==0)field.type_index=1;
            if(mode==1)field.size=3;
            if(mode==2)field.offset=bad.memories.at(bad.references.at(fixture.words).value_index).offset+1;
        }
        ActorSceneResource malformed(std::move(bad),"synthetic/transform-malformed",2);
        rejects([&]{malformed.mutable_node(fixture.source.nodes[0]);},"Malformed transform-input type, width or reference was accepted.");
    }
    const auto ordinary=std::make_shared<ActorSceneResource>(fixture.source.document(),"synthetic/non-transform",3);
    const auto other=ordinary->mutable_node(fixture.source.nodes[0]);
    check(other->type()=="beModelCtrlNode2" && !other->transform_input_field_available(),
        "A different class's serialized slot11 was mistaken for igTransform input.");
    ActorTransformUpdateInvocation unknown(other,123);
    check(!unknown.resume().completed && !unknown.faulted(),"Unverified concrete node update was assumed to inherit the transform leaf.");
    const auto derived=fixture.scene(true,false,true);ActorTransformUpdateInvocation subclass(derived->node(fixture.source.nodes[0]),1);
    check(!subclass.resume().completed && !subclass.faulted(),"Unverified transform subclass was completed as a generic null-input no-op.");

    // A MODEL ONE owner could load a node which leads back to that model.
    // The original field's igTransformSource class constraint must reject
    // this malformed edge before entering the unrelated model loader.
    Fixture cyclic;const auto loop=cyclic.source.memory(pf::words({cyclic.source.ones[0]}));
    auto cycle_document=cyclic.document();
    auto& cycle_node=cycle_document.objects.at(cycle_document.references.at(cyclic.source.nodes[0]).value_index);
    for(auto& field:cycle_node.fields)if(field.slot==11)
        field.offset=cycle_document.memories.at(cycle_document.references.at(loop).value_index).offset;
    auto cycle=std::make_shared<ActorSceneResource>(std::move(cycle_document),"synthetic/transform-model-cycle",4);
    rejects([&]{cycle->mutable_node(cyclic.source.nodes[0]);},"Transform input recursion across model and actor registries was accepted.");
    rejects([&]{cycle->mutable_node(cyclic.source.nodes[0]);},"Failed cross-registry cycle published an incomplete reusable node.");
    rejects([&]{cycle->models().object(cyclic.source.ones[0]);},"Cross-registry cycle was accepted when entered through the model registry.");
    const auto unrelated=cycle->mutable_node(cyclic.source.nodes[1]);
    check(unrelated->transform_input_owner()==cycle->models().object(cyclic.input),
        "Failed cross-registry cycle corrupted later unrelated canonical input loading.");

    auto subclass_document=fixture.document();
    auto& input_object=subclass_document.objects.at(subclass_document.references.at(fixture.input).value_index);
    IgbObjectType input_subclass;input_subclass.name="UnverifiedTransformInput";
    input_subclass.parent_index=static_cast<std::int32_t>(input_object.type_index);input_subclass.slots=2;
    input_object.type_index=static_cast<std::uint32_t>(subclass_document.metadata.object_types.size());
    subclass_document.metadata.object_types.push_back(std::move(input_subclass));
    ActorSceneResource input_scene(std::move(subclass_document),"synthetic/transform-input-subclass",6);
    const auto input_node=input_scene.node(fixture.source.nodes[0]);
    check(input_node->transform_input_owner()->is_a("igTransformSource") &&
        input_node->transform_input_owner()->type()=="UnverifiedTransformInput",
        "A valid unknown input subclass lost its canonical generic owner.");
    ActorTransformUpdateInvocation input_call(input_node,55);
    check(!input_call.resume().completed && !input_call.faulted(),"A valid unknown input subclass received an invented virtual+5C implementation.");
}
void null_leaf_preserves_matrix_and_time() {
    Fixture fixture;auto scene=fixture.scene(true,true);
    for(const auto ref:fixture.source.nodes) {
        const auto node=scene->mutable_node(ref);
        const auto matrix=node->matrix_owner();
        (*matrix)[0]=std::numeric_limits<float>::quiet_NaN();(*matrix)[12]=std::bit_cast<float>(std::uint32_t{1});(*matrix)[13]=-0.f;
        const auto before=*matrix;unsigned calls{};auto services=std::make_shared<ActorTransformUpdateServices>();
        services->input_update=[&](const auto&,const auto&,std::uint64_t) {++calls;return MessengerCallResult{true,9,{}};};
        ActorTransformUpdateInvocation call(node,0xfedcba9876543210ull);
        check(call.resume(services).completed && call.completed() && !call.faulted() && calls==0,
            "Null transform input consumed a callback or failed either verified concrete binding.");
        for(std::size_t index=0;index<matrix->size();++index)
            check(std::bit_cast<std::uint32_t>((*matrix)[index])==std::bit_cast<std::uint32_t>(before[index]),
                "Null transform update changed or validated matrix words.");
        check(call.resume(services).completed && calls==0,"Completed null transform update replayed execution.");
    }
    ActorTransformUpdateInvocation missing({},0);rejects([&]{missing.resume();},"Transform update accepted a null node.");
}
void wrong_type_input_chains() {
    // Each malformed input points at MODEL ONE, which points at the next
    // transform. Reject the field's wrong class before recursive loading;
    // do not turn invalid schema into an excessive parser call stack.
    pf::Graph graph;std::vector<std::uint32_t> nodes,inputs;
    for(unsigned index=0;index<130;++index) {
        nodes.push_back(graph.add(pf::model_node));inputs.push_back(graph.add(pf::one_type));
    }
    for(std::size_t index=0;index+1<nodes.size();++index) {
        graph.scalar(nodes[index],11,inputs[index]);graph.scalar(inputs[index],4,nodes[index+1]);
    }
    auto doc=graph.document();IgbObjectType type;type.name="igTransform";type.parent_index=pf::group;type.slots=12;
    const auto transform_type=static_cast<std::uint32_t>(doc.metadata.object_types.size());
    doc.metadata.object_types.push_back(std::move(type));
    for(const auto ref:nodes) {
        auto& object=doc.objects.at(doc.references.at(ref).value_index);object.type_index=transform_type;
        const auto input=*doc.field(object,11);
        std::erase_if(object.fields,[](const auto& field){return field.slot>8;});object.fields.push_back(input);
    }
    ActorSceneResource scene(std::move(doc),"synthetic/transform-model-depth",5);
    rejects([&]{scene.mutable_node(nodes.front());},"Alternating wrong-type transform/model input chain was accepted.");
    rejects([&]{scene.mutable_node(nodes.front());},"Failed excessive alternating chain retained incomplete owners or active-depth state.");
    rejects([&]{scene.mutable_node(nodes[1]);},"A shorter chain of wrong-type transform inputs was accepted as valid schema.");
    const auto leaf=scene.mutable_node(nodes.back());
    check(leaf->transform_input_field_available() && !leaf->transform_input_owner() && leaf==scene.mutable_node(nodes.back()),
        "Rejected wrong-type input chain corrupted subsequent valid null-input node caching.");
}
void retained_input_and_partial_matrix() {
    Fixture fixture;auto scene=fixture.scene();const auto node=scene->mutable_node(fixture.source.nodes[0]);
    const auto input=node->transform_input_owner();
    const auto matrix=node->matrix_owner();
    const auto replacement=scene->models().object(fixture.other_input);
    constexpr std::uint64_t time=0x123456789abcdef0ull;ActorTransformUpdateInvocation call(node,time);
    check(!call.resume().completed && !call.faulted(),"Nonnull transform input was silently treated as an update-free path.");
    node->set_transform_input_owner(replacement);auto services=std::make_shared<ActorTransformUpdateServices>();unsigned calls{};
    services->input_update=[&](const auto& actual,const auto& output,std::uint64_t actual_time) {
        ++calls;check(actual==input && output==matrix && actual_time==time,"Input continuation recaptured changed input/matrix/time owners.");
        if(calls==1){(*output)[12]=21;return MessengerCallResult{false,0,"Partial input operation"};}
        check((*output)[12]==21,"Pending input operation lost its partial matrix writes.");
        (*output)[13]=34;return MessengerCallResult{true,93,{}};
    };
    check(!call.resume(services).completed && calls==1 && (*matrix)[12]==21,"Input callback suspension discarded its matrix prefix.");
    services->input_update=[](const auto&,const auto&,auto)->MessengerCallResult {throw std::runtime_error("Unreached replacement callback");};
    node->set_transform_input_owner({});scene.reset();
    check(call.resume(services).completed && calls==2 && (*matrix)[12]==21 && (*matrix)[13]==34 &&
        node->transform_input_owner()==nullptr,"Input retry did not retain its entered callback after field/provider changes and resource release.");
    check(call.resume(services).completed && calls==2,"Completed input callback was invoked again.");
}
void fault_and_checkpoint_prefixes() {
    Fixture fixture;auto scene=fixture.scene();const auto node=scene->mutable_node(fixture.source.nodes[0]);
    ActorTransformUpdateInvocation call(node,31);auto services=std::make_shared<ActorTransformUpdateServices>();
    services->input_update=[&](const auto&,const auto& matrix,auto) {
        (*matrix)[12]=75;try {call.resume(services);}catch(const std::exception&) {}return MessengerCallResult{true,0,{}};
    };
    rejects([&]{call.resume(services);},"Caught recursive input update allowed the outer call to complete.");
    check(call.faulted() && node->matrix_owner()->at(12)==75,"Recursive update fault discarded its callback's prior matrix write.");
    rejects([&]{call.resume(services);},"Faulted input update replayed callback effects.");
    ActorTransformUpdateInvocation owner_change(node,42);auto pending=std::make_shared<ActorTransformUpdateServices>();
    pending->input_update=[](const auto&,const auto&,auto){return MessengerCallResult{false,0,"Pending input"};};
    check(!owner_change.resume(pending).completed,"Pending input provider unexpectedly completed.");
    rejects([&]{owner_change.resume(std::make_shared<ActorTransformUpdateServices>());},"Entered input callback accepted a different service owner.");
    check(owner_change.faulted(),"Service replacement failed without poisoning the entered update.");

    // MODEL ONE loading enters node loading, which publishes its input through
    // a nested canonical registry call. A later unsupported work reference must
    // not roll back that completed owner already retained by the node cache.
    Fixture nested;nested.source.scalar(nested.source.ones[0],5,nested.input);
    auto rollback=nested.scene();
    rejects([&]{rollback->models().data(nested.source.ones[0]);},"Unsupported MODEL ONE work field unexpectedly loaded.");
    const auto captured=rollback->mutable_node(nested.source.nodes[0])->transform_input_owner();
    check(captured==rollback->models().object(nested.input),"Outer registry rollback replaced a completed input retained by the node graph.");
    captured->name="surviving mutation";
    rejects([&]{rollback->models().data(nested.source.ones[0]);},"Repeated unsupported MODEL ONE work field unexpectedly loaded.");
    check(rollback->mutable_node(nested.source.nodes[0])->transform_input_owner()==captured &&
        rollback->models().object(nested.input)==captured && captured->name=="surviving mutation",
        "Retry replaced escaped transform input identity or its live mutation after an outer registry failure.");
}
void original(const std::filesystem::path& root) {
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179)),"ndmw.afs[179]",179);
    const auto animated=scene->node(1873);
    const auto input=animated->transform_input_owner();
    check(animated->type()=="igTransform" && input && input->reference()==1874 &&
        input->type()=="igTransformSequence1_5" && input->is_a("igTransformSequence") && input->is_a("igTransformSource") &&
        input==scene->models().object(1874),"Original nonnull transform input lost its concrete type, permitted source base or canonical owner.");
    const auto before_input=std::bit_cast<std::array<std::uint32_t,16>>(*animated->matrix_owner());
    ActorTransformUpdateInvocation input_call(animated,0x123456789abcdef0ull);
    const auto input_result=input_call.resume();
    check(!input_result.completed && !input_call.faulted() && input_result.diagnostic.find("virtual+5C")!=std::string::npos,
        "Original exact igTransform with a real sequence input did not stop at its unresolved input virtual+5C operation.");
    check(std::bit_cast<std::array<std::uint32_t,16>>(*animated->matrix_owner())==before_input,
        "Pending original sequence input changed the node matrix without performing its actual operation.");
    const auto info=scene->models().hit_land_geometry_info(24723);
    const auto list=info->check_switch->child_list_owner();
    check(list && list->count()==56,"Original dynamic collision group no longer has its expected56 child occurrences.");
    unsigned copies{};
    for(std::int32_t index=0;index<list->count();++index) {
        const auto node=list->backing_node(static_cast<std::size_t>(index));
        check(node && node->type()=="beCopyTransform" && node->transform_input_field_available() && !node->transform_input_owner(),
            "Original copy transform did not retain its verified authored null input field.");
        const auto source=node->source_transform_owner();check(bool(source),"Original copy transform lost its separate source-transform owner.");
        const auto before=*node->matrix_owner();ActorTransformUpdateInvocation call(node,0x1234000056780000ull+index);
        check(call.resume().completed,"Actual original null-input copy transform required an invented update provider.");
        for(std::size_t word=0;word<before.size();++word)
            check(std::bit_cast<std::uint32_t>(node->matrix_owner()->at(word))==std::bit_cast<std::uint32_t>(before[word]),
                "Original null-input update copied source-transform data or changed authored matrix words.");
        ++copies;
    }
    const auto first=list->backing_node(0);
    check(first->reference()==21503 && first->source_transform_owner()==scene->mutable_node(24992),
        "Original first copy/source node binding differs from the independently inspected graph.");
    std::cout<<"Original stage179: "<<copies<<" beCopyTransform null-input updates; source links remain distinct.\n";
}
}
int main(int argc,char** argv) {
    try {
        canonical_input_owners();schema_boundaries();null_leaf_preserves_matrix_and_time();wrong_type_input_chains();retained_input_and_partial_matrix();fault_and_checkpoint_prefixes();
        const auto synthetic=checks;if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" actor transform checks passed (synthetic "<<synthetic<<").\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Actor transform after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
