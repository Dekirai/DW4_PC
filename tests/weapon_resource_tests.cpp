#include "dw4/actor_scene.hpp"
#include "dw4/resources.hpp"
#include "dw4/weapon_resource.hpp"
#include "weapon_fixture.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace dw4::test;
using weapon_fixture::Fixture;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();} catch(const std::exception&) {return;}
    throw std::runtime_error("Invalid weapon resource was accepted.");
}
IgbObject& obj(IgbDocument& doc,std::uint32_t ref) {return doc.objects.at(doc.references.at(ref).value_index);}
IgbFieldValue& field(IgbDocument& doc,std::uint32_t ref,std::uint16_t slot) {
    auto& fields=obj(doc,ref).fields;
    const auto found=std::find_if(fields.begin(),fields.end(),[&](const auto& value){return value.slot==slot;});
    if(found==fields.end())throw std::runtime_error("Synthetic field is missing.");
    return *found;
}

std::shared_ptr<ActorSceneResource> resource(IgbDocument doc,std::uint32_t occurrence=9) {
    return std::make_shared<ActorSceneResource>(std::move(doc),"synthetic/weapon.igb",occurrence);
}
WeaponProgram program(IgbDocument doc) {return decode_weapons(resource(std::move(doc)));}

void ownership_and_aliases() {
    Fixture f;auto owner=resource(f.document());const auto p=decode_weapons(owner);const auto& info=p.infos().at(0);
    check(p.resource()==owner && p.source()=="synthetic/weapon.igb" && p.source_occurrence()==9,
        "Weapon lost its explicit retained source occurrence.");
    check(info.reference==f.info && info.owner_reference==f.info && info.rams.size()==2 && info.nodes.size()==2 &&
        info.bone_names.size()==2 && info.sequences.size()==2,"Weapon graph shape or ordered owners differ.");
    check(info.rams[0].authored_index==-7 && info.rams[1].authored_index==-6 && info.rams[0].active==255 &&
        info.rams[0].authored_words==std::array<std::int32_t,4>{11,-2,13,14},"Weapon decoder applied registration resets or normalized signed/raw fields.");
    check(info.node_list.reference==f.node_list && info.node_list.count==2 && info.node_list.capacity==2 &&
        info.node_list.memory_reference==info.node_list.memory_owner_reference,"Weapon list storage provenance differs.");
    check(info.nodes[0].node==owner->node(f.nodes[0]) && info.nodes[1].node->flags()==2 &&
        info.nodes[0].node->name()=="right","Weapon copied a shared node or lost authored flags/name.");
    check(info.sequences[0].label=="" && !info.sequences[1].label,"Weapon collapsed an empty and null label.");
    const auto copy=p;check(&copy.infos()==&p.infos() && copy.resource()==p.resource(),"Weapon program copy lost immutable owner identity.");
    const auto separate=decode_weapons(resource(f.document(),10));
    check(separate.infos()[0].nodes[0].node!=info.nodes[0].node,"Separately loaded source occurrences shared node owners.");

    f.contents(f.ram_list,{f.rams[0],f.rams[1],f.rams[0]});
    f.contents(f.node_list,{f.nodes[0],f.nodes[1],f.nodes[0]});
    f.contents(f.bone_list,{f.bones[0],f.bones[1],f.bones[0]});
    f.contents(f.sequence_list,{f.sequences[0],f.sequences[1],f.sequences[0]});
    f.contents(f.exported_info_list,{f.info,f.other_info,f.info});
    auto doc=f.document();
    for(const auto pair:{std::pair{f.rams[1],f.rams[0]},std::pair{f.nodes[1],f.nodes[0]},
        std::pair{f.bones[1],f.bones[0]},std::pair{f.sequences[1],f.sequences[0]}})
        doc.references[pair.first].value_index=doc.references[pair.second].value_index;
    const auto aliases=program(doc);
    check(aliases.infos().size()==3 && aliases.infos()[1].name=="other" && aliases.infos()[2].reference==f.info,
        "Weapon export duplicates/order were deduplicated.");
    const auto& a=aliases.infos()[0];
    check(a.rams.size()==3 && a.rams[1].reference==f.rams[1] && a.rams[1].owner_reference==f.rams[0] &&
        a.rams[2].reference==f.rams[0],"Weapon raw RAM occurrences were replaced by final registration state.");
    check(a.nodes[1].reference==f.nodes[1] && a.nodes[1].owner_reference==f.nodes[0] &&
        a.nodes[0].node==a.nodes[1].node && a.nodes[0].node==a.nodes[2].node &&
        a.nodes[0].node==aliases.infos()[1].nodes[0].node,"Weapon canonical node alias ownership differs.");
    check(a.bone_names[1].reference==f.bones[1] && a.bone_names[1].owner_reference==f.bones[0] &&
        a.sequences[1].reference==f.sequences[1] && a.sequences[1].owner_reference==f.sequences[0],
        "Weapon named/sequence row aliases lost provenance.");
    doc.references[f.other_info].value_index=doc.references[f.info].value_index;
    const auto info_alias=program(std::move(doc));
    check(info_alias.infos()[1].reference==f.other_info && info_alias.infos()[1].owner_reference==f.info,
        "Weapon export alias lost raw versus physical identity.");
    Fixture storage_alias;
    const auto alternate=storage_alias.memory(words({storage_alias.nodes[0],storage_alias.nodes[1]}));
    auto memory_doc=storage_alias.document();
    const auto raw_memory=*memory_doc.field_reference(memory_doc.object(storage_alias.node_list),4);
    memory_doc.references[raw_memory].value_index=memory_doc.references[alternate].value_index;
    const auto memory_alias=program(std::move(memory_doc));
    check(memory_alias.infos()[0].node_list.memory_reference==raw_memory &&
        memory_alias.infos()[0].node_list.memory_owner_reference==alternate,
        "Weapon list lost its distinct serialized/canonical memory identities.");
    const auto detached=[] {Fixture local;return program(local.document());}();
    check(detached.infos()[0].nodes[0].node->name()=="right" && detached.resource()->document().object(f.nodes[0]).reference_index==f.nodes[0],
        "Weapon node/document lifetime borrowed the decoder input.");
}

void nullable_names_and_exact_types() {
    Fixture f;f.records[f.info].values[2]=words({0});f.records[f.bones[0]].values[2]=words({0});
    f.records[f.sequences[0]].values[2]=words({0});f.text(f.sequences[1],2,"");
    f.scalar(f.info,4,254);
    const auto p=program(f.document());
    check(!p.infos()[0].name && !p.infos()[0].bone_names[0].name && !p.infos()[0].sequences[0].name &&
        p.infos()[0].sequences[1].name=="" && p.infos()[0].active==254,"Nullable metadata or raw active bytes were normalized.");
    auto derived=f.document();auto type=derived.metadata.object_types.at(obj(derived,f.info).type_index);
    type.parent_index=static_cast<std::int32_t>(obj(derived,f.info).type_index);type.name="DerivedWeaponInfo";
    obj(derived,f.info).type_index=static_cast<std::uint32_t>(derived.metadata.object_types.size());
    derived.metadata.object_types.push_back(std::move(type));
    check(program(std::move(derived)).infos().empty(),"Weapon export matching incorrectly accepted inheritance.");
    f.contents(f.ram_list,{});f.contents(f.node_list,{});f.contents(f.bone_list,{});f.contents(f.sequence_list,{});
    const auto empty=program(f.document());
    check(empty.infos().size()==1 && empty.infos()[0].rams.empty() && empty.infos()[0].nodes.empty() &&
        empty.infos()[0].sequences.empty(),"Authored valid empty lists were rejected or synthesized.");
    f.contents(f.exported_info_list,{});check(program(f.document()).infos().empty(),"Valid empty export list changed.");
}

void malformed_graphs() {
    Fixture f;
    rejects([]{decode_weapons({});});
    auto bad=f.document();bad.info_list_reference.reset();rejects([&]{program(bad);});
    bad=f.document();bad.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{program(bad);});
    for(const auto target:{f.info,f.rams[0],f.node_list,f.bone_list,f.sequence_list,f.bones[0],f.sequences[0]}) {
        bad=f.document();const auto duplicate=obj(bad,target).fields.front();obj(bad,target).fields.push_back(duplicate);
        rejects([&]{program(bad);});
    }
    for(const auto target:{std::pair{f.info,std::uint16_t{4}},std::pair{f.rams[0],std::uint16_t{2}},
        std::pair{f.node_list,std::uint16_t{2}},std::pair{f.sequence_list,std::uint16_t{4}},
        std::pair{f.sequences[0],std::uint16_t{3}}}) {
        bad=f.document();auto& wrong=field(bad,target.first,target.second);
        wrong.type_index=wrong.type_index==0 ? 1 : 0;rejects([&]{program(bad);});
        bad=f.document();field(bad,target.first,target.second).size=2;rejects([&]{program(bad);});
    }
    for(const auto slot:{std::uint16_t{6},std::uint16_t{8},std::uint16_t{9}}) {
        bad=f.document();field(bad,f.info,slot).offset=field(bad,f.info,5).offset;rejects([&]{program(bad);});
    }
    Fixture missing_nodes;missing_nodes.scalar(missing_nodes.info,7,null_ref);rejects([&]{program(missing_nodes.document());});
    bad=f.document();obj(bad,f.sequences[0]).fields.pop_back();rejects([&]{program(bad);});
    bad=f.document();field(bad,f.info,5).offset=field(bad,f.info,7).offset;rejects([&]{program(bad);});
    bad=f.document();obj(bad,f.rams[0]).fields.push_back({3,2,field(bad,f.info,5).offset,4});rejects([&]{program(bad);});
    bad=f.document();auto derived=bad.metadata.object_types.at(obj(bad,f.rams[0]).type_index);
    derived.parent_index=static_cast<std::int32_t>(obj(bad,f.rams[0]).type_index);derived.name="DerivedWeaponInfoRam";
    obj(bad,f.rams[0]).type_index=static_cast<std::uint32_t>(bad.metadata.object_types.size());bad.metadata.object_types.push_back(std::move(derived));
    rejects([&]{program(bad);});
    bad=f.document();obj(bad,f.bones[0]).type_index=node;rejects([&]{program(bad);});
    bad=f.document();obj(bad,f.sequences[0]).type_index=named_object;rejects([&]{program(bad);});
    bad=f.document();bad.references[f.rams[0]].kind=IgbReferenceKind::memory;rejects([&]{program(bad);});
    Fixture null_row;null_row.contents(null_row.sequence_list,{null_ref});rejects([&]{program(null_row.document());});
    Fixture length;length.contents(length.bone_list,{length.bones[0]});rejects([&]{program(length.document());});
    Fixture negative;negative.scalar(negative.node_list,2,0xffffffffu);rejects([&]{program(negative.document());});
    Fixture capacity;capacity.scalar(capacity.ram_list,3,4097);rejects([&]{program(capacity.document());});
    Fixture storage;storage.scalar(storage.node_list,4,storage.memory(words({storage.nodes[0]})));rejects([&]{program(storage.document());});
    Fixture no_storage;no_storage.scalar(no_storage.bone_list,4,null_ref);rejects([&]{program(no_storage.document());});
    Fixture export_row;export_row.contents(export_row.exported_info_list,{export_row.bones[0]});rejects([&]{program(export_row.document());});
    Fixture names;names.text(names.sequences[0],2,std::string(4097,'x'));rejects([&]{program(names.document());});
    Fixture aggregate;aggregate.contents(aggregate.exported_info_list,std::vector<std::uint32_t>(2049,aggregate.info));
    rejects([&]{program(aggregate.document());}); // 4,098 actual RAM occurrences, not deduplicated.
    Fixture links;links.contents(links.node_list,std::vector<std::uint32_t>(4096,links.nodes[0]));
    links.contents(links.bone_list,std::vector<std::uint32_t>(4096,links.bones[0]));
    links.contents(links.sequence_list,std::vector<std::uint32_t>(4096,links.sequences[0]));
    links.contents(links.exported_info_list,std::vector<std::uint32_t>(11,links.info));rejects([&]{program(links.document());});
    Fixture text_budget;text_budget.text(text_budget.sequences[0],2,std::string(4096,'x'));
    text_budget.contents(text_budget.sequence_list,std::vector<std::uint32_t>(4096,text_budget.sequences[0]));
    rejects([&]{program(text_budget.document());});
}

void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);
    const std::array<std::string,3> paths{"_common/w_common05.igb","_common/w_common04.igb","_common/w_common03.igb"};
    const std::array<std::uint32_t,3> info_refs{36,17,53},node_refs{3,31,5},entries{916,915,914};
    const std::array<std::array<std::uint32_t,4>,3> ram_refs{{{37,38,39,40},{18,0,1,2},{54,55,56,57}}};
    const std::array<std::string_view,21> sequence_names{"SQ_ENTR","SQ_EXIT","SQ_WAIT","SQ_WALK","SQ_JUMP","SQ_FALL",
        "SQ_GUARD","SQ_GUARD2","SQ_GUARDR","SQ_ATCK1","SQ_ATCKTM","SQ_ATCK2","SQ_ATCK3","SQ_DEAD","SQ_CMBK",
        "SQ_CMBK02","SQ_ENJOY","SQ_SLEP","SQ_STUN","SQ_DEMOWAIT","SQ_DEMOWALK"};
    for(std::size_t i=0;i<paths.size();++i) {
        auto owner=std::make_shared<ActorSceneResource>(read_igb_document(mounted.read(paths[i])),paths[i],static_cast<std::uint32_t>(20+i));
        const auto p=decode_weapons(owner);const auto& info=p.infos().at(0);
        check(mounted.catalog().find(paths[i])->location->entry_index==entries[i] && p.infos().size()==1 &&
            info.reference==info_refs[i],"Original weapon export/resource provenance differs.");
        check(info.rams.size()==4 && info.nodes.size()==(i==0?1u:2u) && info.bone_names.size()==info.nodes.size() &&
            info.sequences.size()==21,"Original weapon lists have different dimensions.");
        check(info.nodes[0].reference==node_refs[i] && info.nodes[0].node==owner->node(node_refs[i]) &&
            info.bone_names[0].name=="RArm04","Original weapon node/bone owner differs.");
        if(i!=0)check(info.bone_names[1].name=="LArm04" && info.nodes[1].node->flags()==2,"Original left attachment flags/bone differ.");
        for(std::size_t ram=0;ram<4;++ram)
            check(info.rams[ram].reference==ram_refs[i][ram] && info.rams[ram].authored_index==0 && info.rams[ram].active==0 &&
                info.rams[ram].authored_words==std::array<std::int32_t,4>{},"Original serialized RAM was mistaken for runtime registration state.");
        for(std::size_t row=0;row<21;++row) {
            const auto label=i==0 ? (row==9 || row==11?"axe":"slash") : (i==1?"gun":"arm");
            check(info.sequences[row].name==sequence_names[row] && info.sequences[row].label==label,
                "Original ordered weapon sequence overrides differ.");
        }
        check(info.nodes[0].node->children().size()==1 && info.nodes[0].node->children()[0]->type()=="igGeometry",
            "Original weapon geometry child was replaced or discarded.");
    }
    std::cout<<"Original weapon resources: 3 infos, 12 RAM occurrences, 5 nodes, 63 sequence rows\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        ownership_and_aliases();nullable_names_and_exact_types();malformed_graphs();
        if(args.size()==2)original(args[1]);else if(args.size()!=1)throw std::runtime_error("Usage: dw4_weapon_resource_tests [EXTRACTED_ROOT]");
        std::cout<<"weapon_resource: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"weapon_resource after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);
}
