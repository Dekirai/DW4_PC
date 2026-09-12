#include "dw4/action_event_runtime.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <iostream>
#include <type_traits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    ++checks;try{action();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid HITLANDDELIVER owner operation completed.");
}
struct Fixture {
    pf::RuntimeFixture source;
    std::uint32_t info{},instances{},numbers{},vectors{};
    std::array<std::uint32_t,2> rams{};
    Fixture() {
        info=source.add(pf::model_type);source.records[info]=source.records[source.model];
        for(auto& ram:rams)ram=source.add(pf::ram_type);
        instances=source.list(pf::ram_list,{rams[1],pf::null_ref,rams[0],rams[1]});
        source.scalar(info,5,pf::null_ref);source.scalar(info,6,instances);
        numbers=source.memory(pf::words({7,11,12,13,14,55}));
        vectors=source.memory(pf::floats({3,4,4,1,2,3,4,5,6,7,8,9}));
    }
    IgbDocument document() const {
        auto doc=source.document();
        const auto zero=*doc.field(doc.object(source.ones[0]),3);
        const auto flag=*doc.field(doc.object(source.state),4);
        const auto integer_offset=doc.memories.at(doc.references.at(numbers).value_index).offset;
        const auto vector_offset=doc.memories.at(doc.references.at(vectors).value_index).offset;
        const auto vector_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());
        doc.metadata.field_types.push_back({"igVec3fMetaField",1,0});
        const auto info_type=static_cast<std::uint32_t>(doc.metadata.object_types.size());
        auto info_schema=doc.metadata.object_types.at(pf::model_type);info_schema.name="beHitLandDelivInfo";
        info_schema.slots=7;std::erase_if(info_schema.fields,[](const auto& field){return field.slot==7;});
        doc.metadata.object_types.push_back(std::move(info_schema));
        const auto ram_type=static_cast<std::uint32_t>(doc.metadata.object_types.size());
        auto ram_schema=doc.metadata.object_types.at(pf::ram_type);ram_schema.name="beHitLandDelivInfoRam";
        ram_schema.parent_index=pf::ram_type;ram_schema.slots=20;doc.metadata.object_types.push_back(std::move(ram_schema));
        auto& model=doc.objects.at(doc.references.at(source.model).value_index);
        auto active=flag;active.slot=4;model.fields.push_back(active);
        auto& land_info=doc.objects.at(doc.references.at(info).value_index);
        land_info.type_index=info_type;std::erase_if(land_info.fields,[](const auto& field){return field.slot==7;});
        land_info.fields.push_back(active);
        for(std::size_t i=0;i<rams.size();++i) {
            auto& model_ram=doc.objects.at(doc.references.at(source.model_rams[i]).value_index);
            for(const auto slot:{2u,6u,7u,8u,9u}) {auto field=zero;field.slot=static_cast<std::uint16_t>(slot);model_ram.fields.push_back(field);}
            active.slot=5;model_ram.fields.push_back(active);
            auto& ram=doc.objects.at(doc.references.at(rams[i]).value_index);ram.type_index=ram_type;
            ram.fields.push_back({2,1,integer_offset+20,4});ram.fields.push_back(active);
            for(std::uint16_t slot=6;slot<=9;++slot)ram.fields.push_back({slot,1,integer_offset+4*(slot-5),4});
            auto node=*doc.field(doc.object(source.ones[i]),4);node.slot=11;ram.fields.push_back(node);
            ram.fields.push_back({12,1,integer_offset,4});
            for(std::uint16_t slot=13;slot<=15;++slot)ram.fields.push_back({slot,8,vector_offset+4*(slot-13),4});
            for(std::uint16_t slot=16;slot<=18;++slot)ram.fields.push_back({slot,vector_type,vector_offset+12*(slot-15),12});
            auto land_ok=flag;land_ok.slot=19;ram.fields.push_back(land_ok);
        }
        return doc;
    }
    std::shared_ptr<ActorSceneResource> scene(std::uint32_t occurrence=5) const {
        return std::make_shared<ActorSceneResource>(document(),"synthetic/hit-land.igb",occurrence);
    }
};
void defaults_and_canonical_graph() {
    static_assert(!std::is_copy_constructible_v<ModelHitLandRam>);
    ModelHitLandInfo initial_info;ModelHitLandRam initial;
    check(initial_info.active==1 && initial.active==1 && !initial.node && initial.check_flags==0 &&
        initial.half_width==3 && initial.up_range==4 && initial.down_range==4 &&
        initial.previous_position==std::array<float,3>{} && initial.land_position_ok==0,
        "Fresh reflected defaults must match the original metadata, not a node-derived prior position.");
    Fixture f;auto doc=f.document();
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[f.rams[1]]);
    auto scene=std::make_shared<ActorSceneResource>(std::move(doc),"synthetic/hit-land.igb",5);
    const auto info=scene->models().hit_land_info(f.info);const auto first=scene->models().hit_land_ram(f.rams[0]);
    const auto second=scene->models().hit_land_ram(f.rams[1]);
    check(info->name=="Synthetic player" && info->active==1 && !info->data && info->rams->count==4 && info->rams->capacity==4,
        "HITLANDDELIVER info fields or ordered list shape changed during load.");
    check(info->rams->values()[0]==second && !info->rams->values()[1] && info->rams->values()[2]==first &&
        info->rams->values()[3]==second && scene->models().hit_land_ram(alias)==second,
        "Null slots, repeated entries and physical reference aliases must retain their canonical owners.");
    check(first->number==55 && first->active==1 && first->words==std::array<std::uint32_t,4>{11,12,13,14} &&
        !first->data && first->check_flags==7 && first->half_width==3 && first->up_range==4 && first->down_range==4,
        "Authored HITLANDDELIVER scalar payload was replaced by registration or constructor defaults.");
    check(first->position==std::array<float,3>{1,2,3} && first->previous_position==std::array<float,3>{4,5,6} &&
        first->land_position==std::array<float,3>{7,8,9} && first->land_position_ok==1 &&
        first->parent.expired() && first->manager.expired(),"Collision vectors and unpublished backlinks must remain distinct.");
    const auto model=scene->models().ram(f.source.model_rams[0]);const auto one=find_model_one(*model);
    check(first->node==one->node && first->node==scene->mutable_node(f.source.nodes[0]),
        "HITLANDDELIVER transform must be the exact canonical MODELCTRL ONE node.");
    (*one->node->matrix_owner())[12]=81;
    check((*first->node->matrix_owner())[12]==81 && first->previous_position==std::array<float,3>{4,5,6},
        "Shared node writes must not silently synchronize separate collision history.");
    auto copied=f.scene();check(copied->models().hit_land_ram(f.rams[0])!=first &&
        copied->models().hit_land_ram(f.rams[0])->node!=first->node,"Separately loaded scenes cannot share physical owners by path/ref alone.");
    const auto document=first->document_owner();scene.reset();
    check(first->document_owner()==document && first->source()=="synthetic/hit-land.igb" && first->source_occurrence()==5 &&
        first->reference()==f.rams[0] && (*first->node->matrix_owner())[12]==81,
        "Retained collision owners and source identity must outlive the resource facade.");
}
void malformed_and_transactional_loading() {
    Fixture f;
    auto malformed=f.document();auto& ram=malformed.objects.at(malformed.references.at(f.rams[0]).value_index);
    const auto duplicate=*malformed.field(ram,19);ram.fields.push_back(duplicate);
    ActorSceneResource duplicate_scene(std::move(malformed),"duplicate",1);
    rejects([&]{duplicate_scene.models().hit_land_info(f.info);});
    auto backlink=f.document();auto& linked=backlink.objects.at(backlink.references.at(f.rams[0]).value_index);
    auto parent=*backlink.field(linked,11);parent.slot=3;linked.fields.push_back(parent);
    ActorSceneResource linked_scene(std::move(backlink),"linked",1);
    rejects([&]{linked_scene.models().hit_land_ram(f.rams[0]);});
    auto type=f.document();auto& wrong=type.objects.at(type.references.at(f.rams[0]).value_index);
    for(auto& field:wrong.fields)if(field.slot==16)field.type_index=8;
    ActorSceneResource wrong_scene(std::move(type),"wrong",1);
    const auto retained=wrong_scene.models().ram(f.source.model_rams[0]);const auto retained_one=find_model_one(*retained);
    rejects([&]{wrong_scene.models().hit_land_info(f.info);});
    check(wrong_scene.models().ram(f.source.model_rams[0])==retained && find_model_one(*retained)==retained_one,
        "A failed collision load must not replace previously exposed canonical MODELCTRL owners.");
    rejects([&]{wrong_scene.models().hit_land_ram(f.source.model_rams[0]);});
    rejects([&]{wrong_scene.models().ram(f.rams[1]);});
}
void registration_and_real_dispatch() {
    Fixture f;auto scene=f.scene();const auto info=scene->models().hit_land_info(f.info);
    const auto ram=scene->models().hit_land_ram(f.rams[0]);
    std::weak_ptr<const ModelSceneManager> weak_manager;
    {
        ActionEventRuntime events;
        const auto model=events.register_model_scene_ram(scene,f.source.model,f.source.model_rams[0],0);
        const auto id=events.register_hit_land_scene_ram(scene,f.info,f.rams[0],4);
        weak_manager=ram->manager;
        check(events.hit_land_ram(id)==ram && ram->manager.lock()!=events.model_manager() &&
            ram->manager.lock()!=events.model_ram(model)->manager.lock(),"HITLANDDELIVER must have a distinct actual manager allocation.");
        check(ram->parent.lock()==info && ram->number==4 && ram->active==1 && ram->words==std::array<std::uint32_t,4>{},
            "Original per-RAM registration must reset words, then publish manager/parent/index without changing activity.");
        check(ram->previous_position==std::array<float,3>{4,5,6} && ram->position==std::array<float,3>{1,2,3} &&
            ram->land_position==std::array<float,3>{7,8,9} && ram->land_position_ok==1,
            "Registration must not initialize previous position or other collision results from the node.");
        info->name="renamed";
        check(events.register_hit_land_scene_ram(scene,f.info,f.rams[0],8)==id,
            "Repeated registration must append another row for the same canonical RAM owner.");
        auto old_row=events.messenger().begin_send_to("HITLANDDELIVER","Synthetic player",4,"UNKNOWN");
        check(events.resume(old_row).stop==MessengerStop::completed,
            "Old registration keys must route their original row despite live parent/RAM changes.");
        auto new_row=events.messenger().begin_send_to("HITLANDDELIVER","renamed",8,"UNKNOWN");
        check(events.resume(new_row).recipients_completed==1,
            "The later registration occurrence must preserve its independently captured keys.");
        info->name.reset();events.register_hit_land_scene_ram(scene,f.info,f.rams[0],10);
        auto empty_name=events.messenger().begin_send_to("HITLANDDELIVER","",10,"UNKNOWN");
        check(events.resume(empty_name).recipients_completed==1,"Null parent name must register the original non-null empty fallback.");
        events.set_active(id,false);auto& messenger=events.messenger();messenger.select("HITLANDDELIVER","",10);
        messenger.buffer().set_enabled_only(1);auto inactive=messenger.begin_send("UNKNOWN");
        check(events.resume(inactive).recipients_completed==0 && ram->active==0,"Activity gates must read the actual live recipient RAM.");
        events.set_active(id,true);
        auto expected_manager=ram->manager.lock();ram->manager.reset();
        auto missing=events.messenger().begin_send_to("HITLANDDELIVER","",10,"UNKNOWN");
        check(events.resume(missing).stop==MessengerStop::service_required,"Missing actual manager must not fabricate successful delivery.");
        ram->manager=expected_manager;
        check(events.resume(missing).stop==MessengerStop::completed,"Installing the original manager must resume the same retained row.");
        auto duplicate=f.scene();rejects([&]{events.register_hit_land_scene_ram(duplicate,f.info,f.rams[0],0);});
        rejects([&]{events.hit_land_ram(model);});
    }
    check(weak_manager.expired() && ram->manager.expired() && ram->node,
        "RAM backlinks must not retain the event runtime or conflate node lifetime with manager lifetime.");
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(resources.read("_common/p_agumon.igb")),
        "_common/p_agumon.igb",31);
    const auto info=scene->models().hit_land_info(1712);const auto model=scene->models().info(1386);
    const auto physical=scene->document_owner()->object_list(2);
    check(physical==std::vector<std::optional<std::uint32_t>>{235,236,237,238},
        "Original HITLANDDELIVER RAM order must come from memory247, not sorting references.");
    check(info->name=="p_agumon" && info->active==1 && !info->data && info->rams->reference()==2u &&
        info->rams->count==4 && info->rams->capacity==4,"Original Agumon collision info differs.");
    ActionEventRuntime events;
    const std::array<std::uint32_t,4> nodes{6390,6457,6524,6590};
    for(std::size_t index=0;index<physical.size();++index) {
        const auto ram=std::dynamic_pointer_cast<ModelHitLandRam>(info->rams->values()[index]);
        const auto player=std::dynamic_pointer_cast<ModelSceneRam>(model->rams->values()[index]);
        const auto one=find_model_one(*player);
        check(ram && ram->reference()==physical[index] && ram==scene->models().hit_land_ram(*physical[index]) &&
            ram->node==one->node && ram->node==scene->mutable_node(nodes[index]),"Original collision and player graphs must share exact nodes in physical list order.");
        check(ram->number==0 && ram->active==1 && ram->words==std::array<std::uint32_t,4>{} && !ram->data &&
            ram->check_flags==7 && ram->half_width==3 && ram->up_range==4 && ram->down_range==4 &&
            ram->position==std::array<float,3>{} && ram->previous_position==std::array<float,3>{} &&
            ram->land_position==std::array<float,3>{} && ram->land_position_ok==0,
            "Original authored collision payload must survive loading exactly.");
        const auto before=ram->node->matrix_owner();const auto matrix=*before;
        const auto id=events.register_hit_land_scene_ram(scene,1712,*physical[index],static_cast<std::int32_t>(index));
        check(events.hit_land_ram(id)==ram && ram->number==static_cast<std::int32_t>(index) && ram->node->matrix_owner()==before &&
            *before==matrix && ram->previous_position==std::array<float,3>{},
            "Original registration must retain authored zero previous position and the shared mutable transform.");
    }
    check(events.messenger().registration_count()==4,"Original four physical collision RAMs must append four recipient rows.");
}
}
int main(int argc,char** argv) {
    try {
        defaults_and_canonical_graph();malformed_and_transactional_loading();registration_and_real_dispatch();
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" HITLANDDELIVER scene checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
