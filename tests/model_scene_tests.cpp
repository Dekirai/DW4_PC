#include "dw4/model_scene.hpp"
#include "dw4/actor_geometry.hpp"
#include "dw4/resources.hpp"
#include "hitland_scene_fixture.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& call,const char* reason) {
    ++checks;try {call();}catch(const ModelSceneError&){return;}throw std::runtime_error(reason);
}

// Extend the shared synthetic visual fixture with the actual serialized MODEL
// scalar fields. Values refer only to generated zero/one storage in that fixture.
IgbDocument document(const pf::RuntimeFixture& fixture) {
    auto doc=fixture.document();
    const auto zero=*doc.field(doc.object(fixture.ones[0]),3);
    const auto flag=*doc.field(doc.object(fixture.state),4);
    auto& info=doc.objects.at(doc.references.at(fixture.model).value_index);
    auto active=flag;active.slot=4;info.fields.push_back(active);
    for(const auto ref:fixture.model_rams) {
        auto& ram=doc.objects.at(doc.references.at(ref).value_index);
        for(auto slot:{2u,6u,7u,8u,9u}) {auto value=zero;value.slot=static_cast<std::uint16_t>(slot);ram.fields.push_back(value);}
        active.slot=5;ram.fields.push_back(active);
    }
    return doc;
}
std::unique_ptr<ActorSceneResource> resource(const pf::RuntimeFixture& fixture) {
    return std::make_unique<ActorSceneResource>(document(fixture),"synthetic/model.igb",7);
}
std::shared_ptr<ModelSceneRam> ram_at(const std::shared_ptr<ModelSceneInfo>& info,std::size_t index) {
    return std::dynamic_pointer_cast<ModelSceneRam>(info->rams->values()[index]);
}
void ordered_identity_and_retention() {
    static_assert(!std::is_copy_constructible_v<ModelSceneObject> && !std::is_copy_assignable_v<ModelSceneRam>);
    static_assert(!std::is_constructible_v<ModelSceneObjects,ActorSceneResource&>);
    pf::RuntimeFixture fixture;
    fixture.contents(fixture.instances,{fixture.model_rams[1],pf::null_ref,fixture.model_rams[0],fixture.model_rams[1]});
    auto doc=document(fixture);
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[fixture.model_rams[1]]);
    auto scene=std::make_unique<ActorSceneResource>(doc,"synthetic/model.igb",7);
    auto info=scene->models().info(fixture.model);
    check(info->name=="Synthetic player" && info->kind=="KIND_PLAYER" && info->active==1,
        "Actual info fields must survive loading.");
    check(info->rams->count==4 && ram_at(info,0)==ram_at(info,3) && !info->rams->values()[1],
        "RAM list order, null slots and repeated owners must survive.");
    auto ram=ram_at(info,0);
    check(scene->models().ram(alias)==ram && ram->reference()==fixture.model_rams[1],
        "Reference aliases must return one physical RAM owner.");
    check(ram->number==0 && ram->words==std::array<std::uint32_t,4>{} && ram->parent.expired() && ram->manager.expired(),
        "Loading must not infer registered ordinals or backlinks.");
    const auto one=find_model_one(*ram);const auto node=one->node;
    check(one==scene->models().data(fixture.ones[1]) && !one->work && node==scene->mutable_node(fixture.nodes[1]),
        "ONE must retain the actual scene node and serialized null work.");
    auto old_work=make_model_player_work();one->work=old_work;
    auto old_script=bind_model_player_script(old_work,{7,1},
        "synthetic/model.igb",std::string("Synthetic player"),0,node->matrix_owner());
    old_script->hp()->data=17;
    one->work=make_model_player_work();one->work->script.hp->data=23;
    check(old_script->hp()->data==17 && one->work->script.hp->data==23,
        "Replacing ONE.work must not redirect an already retained script view.");
    const auto original_data=ram->data;
    ram->data=std::make_shared<ModelSceneList>();
    check(!find_model_one(*ram) && original_data->values().front()==one,
        "Replacing RAM.data must preserve captured old list and ONE owners.");
    ram->data=original_data;
    ActorSceneResource other(doc,"synthetic/model.igb",7);
    check(other.models().ram(fixture.model_rams[1])!=ram &&
        other.models().ram(fixture.model_rams[1])->document_owner()!=ram->document_owner(),
        "Equal-looking source metadata must not merge separate loaded owners.");
    const auto retained_document=ram->document_owner();scene.reset();
    check(ram->source()=="synthetic/model.igb" && ram->source_occurrence()==7 &&
        retained_document->object(*ram->reference()).reference_index==*ram->reference() &&
        find_model_one(*ram)==one && one->node==node,
        "Retained graph owners must remain usable after the resource facade is destroyed.");
}
void live_lists_and_lookup() {
    pf::RuntimeFixture fixture;
    const auto extra=fixture.add(pf::data_list);fixture.records[extra].values=fixture.records[fixture.instances].values;
    fixture.scalar(extra,2,1);
    auto scene=resource(fixture);auto first=scene->models().list(fixture.instances),second=scene->models().list(extra);
    check(first!=second && first->count==2 && second->count==1 && first->backing==second->backing,
        "Distinct compatible list objects must share one physical backing allocation.");
    std::swap((*first->backing)[0],(*first->backing)[1]);
    check(second->values()[0]==first->values()[0],"Backing mutation must be visible through all aliased lists.");
    first->count=0;
    check(first->values().empty() && second->count==1 && second->values().size()==1,
        "Logical counts belong to list owners, not their shared backing.");
    first->append(nullptr);
    check(second->values()[0]==nullptr && first->backing==second->backing,
        "Append into retained capacity must update the shared slot without replacing backing.");
    first->count=-1;rejects([&]{first->values();},"Negative count must remain an explicit checked boundary.");
    first->count=1;first->capacity=0;rejects([&]{first->values();},"Count beyond capacity must reject.");
    first->capacity=2;first->count=2;const auto old_backing=first->backing;
    const auto appended=std::make_shared<ModelSceneData>();first->append(appended);
    check(first->count==3 && first->capacity==4 && first->backing!=old_backing && second->backing==old_backing &&
        first->values()[2]==appended && second->capacity==2,
        "Capacity growth must replace only the growing list's backing, preserving other physical memory aliases.");
    first->append(nullptr);first->append(appended);
    check(first->capacity==8 && first->count==5,"Original list growth must double capacities below 1024.");
    auto large=std::make_shared<ModelSceneList>();large->backing=std::make_shared<ModelSceneList::Storage>(1024);
    large->count=large->capacity=1024;large->append(nullptr);
    check(large->count==1025 && large->capacity==2048,"Original list growth must add 1024 at larger capacities.");

    ModelSceneRam ram;
    const auto empty=ensure_model_data(ram);
    check(empty && empty->count==0 && !empty->backing && !find_model_one(ram),
        "Ensure must create only one empty data list, never a fabricated ONE.");
    const auto null_name=std::make_shared<ModelSceneData>();
    const auto empty_name=std::make_shared<ModelSceneData>();empty_name->name="";
    const auto wrong=std::make_shared<ModelSceneData>();wrong->name="ONE";
    const auto right=std::make_shared<ModelSceneOne>();right->name="ONE";
    empty->append(nullptr);empty->append(null_name);empty->append(empty_name);empty->append(wrong);empty->append(right);empty->append(right);
    rejects([&]{find_model_one(ram);},"Original named lookup must reject a reached null entry rather than silently skipping it.");
    (*empty->backing)[0]=null_name;
    check(find_model_data(empty,"")==empty_name && !find_model_named(empty,"one"),
        "Lookup must distinguish null/empty names and retain exact case.");
    check(find_model_data(empty,"ONE")==wrong,
        "Lookup must return the first name match before requesting a concrete ONE type.");
    rejects([&]{find_model_one(ram);},"A wrong-type ONE must shadow a later correct ONE.");
    (*empty->backing)[3]=null_name;
    check(find_model_one(ram)==right && empty->values()[4]==empty->values()[5],
        "Fresh lookup must observe mutable order and preserve repeated ONE entries.");
    rejects([&]{find_model_data(empty,std::string("ONE\0",4));},"Embedded-null lookup names must reject.");
    rejects([&]{find_model_named({},"ONE");},"Original named lookup must reject a null list.");
    auto raw=std::make_shared<ModelSceneList>();raw->count=-7;raw->capacity=-19;
    check(!find_model_named(raw,"ONE"),"Signed nonpositive lookup count must return no match without validating backing or capacity.");
    raw->count=1;raw->backing=std::make_shared<ModelSceneList::Storage>(ModelSceneList::Storage{right});
    check(find_model_named(raw,"ONE")==right,"Named lookup must not consult the capacity field.");
    raw->count=std::numeric_limits<std::int32_t>::max();
    check(find_model_named(raw,"ONE")==right,"An early name match must not validate unreached backing slots.");
    rejects([&]{find_model_named(raw,"MISSING");},"Lookup must check the first actually reached unavailable slot.");
    raw->count=-1;raw->backing.reset();
    check(!find_model_named_nullable(raw,std::nullopt),"Nonpositive lookup count must not consume a null requested name.");
    raw->count=1;raw->backing=std::make_shared<ModelSceneList::Storage>(ModelSceneList::Storage{null_name});
    check(!find_model_named_nullable(raw,std::nullopt),"A null entry name must skip strcmp without consuming the requested name.");
    (*raw->backing)[0]=empty_name;
    rejects([&]{find_model_named_nullable(raw,std::nullopt);},"Reached non-null entry name must fault when the requested name is null.");
}
void timer_records_and_registration() {
    auto info=std::make_shared<ModelSceneInfo>();auto ram=std::make_shared<ModelSceneRam>();
    auto actual_manager=std::make_shared<ModelSceneManager>();
    register_model_ram(info,ram,-7,actual_manager);
    check(ram->parent.lock()==info && ram->manager.lock()==actual_manager && ram->number==-7 && !ram->active && !info->rams,
        "Explicit registration must bind supplied owners/index without adding activation or list effects.");
    actual_manager.reset();info.reset();
    check(ram->parent.expired() && ram->manager.expired(),"Backlinks must not create info/manager retention cycles.");
    rejects([&]{model_timer_frames(*ram,"KEIKATIMER",8);},"Missing timer query must reject after ensuring RAM data.");
    check(ram->data && ram->data->count==0,"Missing timer query must retain the original data-list ensure effect.");
    auto timer=start_model_timer(*ram,"KEIKATIMER",123456,50);
    check(ram->data->count==1 && timer==find_model_timer(*ram,"KEIKATIMER") && timer->name=="KEIKATIMER" &&
        !timer->reference() && timer->value==0 && timer->timer.end_time==50 && timer->timer.cached_frames==1,
        "Timer start must name and append one actual fresh record before restarting it.");
    timer->timer.time_scale=0.5f;timer->timer.previous_time=-19;
    auto duplicate=std::make_shared<ModelSceneTimer>();duplicate->name="KEIKATIMER";
    ram->data->append(duplicate);
    check(start_model_timer(*ram,"KEIKATIMER",999999,75)==timer && ram->data->count==2 &&
        timer->timer.time_scale==0.5f && timer->timer.previous_time==-19 && timer->timer.end_time==75 &&
        duplicate->timer==MotionTimer{},"Restart must preserve the first timer's scale/previous time and ignore later duplicates.");
    check(model_timer_frames(*ram,"KEIKATIMER",999999)==1,"Repeated timestamp query must use the actual timer cache.");
    const auto old_data=ram->data;ram->data.reset();
    const auto new_timer=start_model_timer(*ram,"KEIKATIMER",4);
    check(new_timer!=timer && timer->timer.end_time==75 && old_data->values()[0]==timer,
        "Replacing the named-data list must not redirect captured old timers.");
    const auto wrong=std::make_shared<ModelSceneData>();wrong->name="BAD";
    ram->data->append(wrong);const auto before=ram->data->count;
    rejects([&]{start_model_timer(*ram,"BAD",8);},"Wrong-type named timer must reject without appending a replacement.");
    check(ram->data->count==before,"Failed timer type check must preserve the actual named list.");
}
void sequence_owners_and_live_fields() {
    pf::RuntimeFixture fixture;
    const auto move=fixture.add(pf::base_data_type);fixture.text(move,2,"Move");
    const auto label=fixture.add(pf::base_data_type);fixture.text(label,2,"dual");
    const auto commands=fixture.list(pf::named_list,{label,move,move,pf::null_ref});
    const auto alias_list=fixture.add(pf::named_list);
    fixture.records[alias_list].values=fixture.records[commands].values;
    fixture.scalar(alias_list,2,2);
    const auto selected=fixture.add(pf::sequence_data_type);fixture.text(selected,2,"SQ_LIVE");
    fixture.scalar(selected,3,commands);
    const auto sequences=fixture.list(pf::named_list,{selected,pf::null_ref,selected});
    fixture.scalar(fixture.sequence,4,sequences);
    const auto scalar_storage=fixture.memory(pf::floats({0.75f,6.0f}));
    auto doc=document(fixture);
    for(const auto entry:{std::pair{move,"beModelCtrlMOVE"},std::pair{label,"beModelCtrlLABEL"}}) {
        auto& object=doc.objects[doc.references[entry.first].value_index];
        object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
        doc.metadata.object_types.push_back({entry.second,1,0,pf::object,5,{}});
        object.fields.erase(std::remove_if(object.fields.begin(),object.fields.end(),[](const auto& field){return field.slot==3;}),object.fields.end());
    }
    auto& move_record=doc.objects[doc.references[move].value_index];
    const auto offset=doc.memories[doc.references[scalar_storage].value_index].offset;
    move_record.fields.push_back({3,8,offset,4});move_record.fields.push_back({4,8,offset+4,4});
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[move]);
    auto scene=std::make_unique<ActorSceneResource>(doc,"synthetic/sequence.igb",19);
    const auto info=scene->models().info(fixture.model);const auto seq=find_model_seq(*info);
    const auto data=find_model_sequence(*seq,"SQ_LIVE");
    check(seq && seq->reference()==fixture.sequence && data && data->reference()==selected,
        "Named SEQ and selected data must be canonical actual graph owners.");
    check(data->commands->count==4 && !(*data->commands->backing)[3] &&
        (*seq->sequences->backing)[0]==(*seq->sequences->backing)[2],
        "Sequence and command lists must retain nullable/repeated physical entries.");
    const auto command=std::dynamic_pointer_cast<ModelSceneCommand>((*data->commands->backing)[1]);
    check(command && scene->models().object(alias)==command && (*data->commands->backing)[2]==command,
        "Repeated and aliased command references must share one mutable command allocation.");
    const auto other=scene->models().list(alias_list);
    check(other!=data->commands && other->backing==data->commands->backing && other->count==2,
        "Distinct command lists must retain one shared physical backing and independent counts.");
    auto consumed=model_scene_command(*command);const auto motion=std::get<MotionCommand>(*consumed.model_command);
    check(motion.name=="Move" && motion.channel==2u && motion.setup.value==0.75f && motion.setup.frames==6,
        "Reached MOVE request must derive from its raw authored fields.");
    command->name="Jump";command->value=2;command->frames=-3;
    consumed=model_scene_command(*std::dynamic_pointer_cast<ModelSceneCommand>((*other->backing)[1]));
    const auto changed=std::get<MotionCommand>(*consumed.model_command);
    check(changed.channel==1u && changed.setup.mode==MotionMode::impulse_positive && changed.setup.value==2 && changed.setup.frames==-3,
        "Command name and numeric mutation must be observed through all command aliases.");
    command->name.reset();command->value=std::numeric_limits<float>::quiet_NaN();
    consumed=model_scene_command(*command);
    check(!command->name && consumed.name.empty() && std::isnan(std::get<MotionCommand>(*consumed.model_command).setup.value),
        "Owner null name and raw nonfinite numeric fields must survive until the actual consumer.");
    const auto wrong=std::make_shared<ModelSceneData>();wrong->name="SQ_LIVE";
    (*seq->sequences->backing)[0]=wrong;
    rejects([&]{find_model_sequence(*seq,"SQ_LIVE");},"First wrong-type sequence name must shadow the later correct allocation.");
    (*seq->sequences->backing)[0]=data;seq->sequences->count=1;
    auto replacement=std::make_shared<ModelSceneSeq>();replacement->name="SEQ";
    replacement->sequences=std::make_shared<ModelSceneList>("igNamedObjectList");
    info->data=std::make_shared<ModelSceneList>();info->data->append(replacement);
    check(find_model_seq(*info)==replacement && !find_model_sequence(*replacement,"SQ_LIVE") && data->commands->backing==other->backing,
        "Fresh parent SEQ selection must observe replacement while captured data/commands remain alive.");
    const auto retained=command->document_owner();scene.reset();
    check(command->source_occurrence()==19 && retained && data->reference()==selected && model_scene_command(*command).name.empty(),
        "Captured selected data and command owners must outlive the source facade.");

    fixture.scalar(sequences,2,0xffffffffu);fixture.scalar(sequences,3,0x80000000u);
    auto raw_doc=document(fixture);
    // Only load the named list; the command records are ordinary named base
    // data here, sufficient to check untouched independent list scalars.
    ActorSceneResource raw(std::move(raw_doc),"synthetic/raw-list.igb",20);
    const auto raw_list=raw.models().list(sequences);
    check(raw_list->count==-1 && raw_list->capacity==std::numeric_limits<std::int32_t>::min() &&
        raw_list->backing->size()==3 && !find_model_named(raw_list,"anything"),
        "Serialized named-list count/capacity must not be normalized or eagerly used as an allocation size.");
}
void serialized_timer_and_hit() {
    pf::RuntimeFixture fixture;
    const auto timer_ref=fixture.add(pf::base_data_type);fixture.text(timer_ref,2,"KEIKATIMER");
    auto timer_bytes=pf::words({std::bit_cast<std::uint32_t>(0.5f),0,0x89abcdefu,0xfedcba98u,
        0x01234567u,0x89abcdefu,0x76543210u,0xfedcba98u,0x55555555u,0xaaaaaaaau,
        std::bit_cast<std::uint32_t>(-2.5f),0});
    const auto timer_storage=fixture.memory(timer_bytes);
    const auto hit_ref=fixture.add(pf::base_data_type);fixture.text(hit_ref,2,"HIT");
    const auto box_ref=fixture.add(pf::object);
    const auto box_storage=fixture.memory(pf::floats({-1,-2,-3,4,5,6}));
    const auto box_list=fixture.list(pf::object_list,{box_ref,pf::null_ref,box_ref});
    const auto hit_storage=fixture.memory(pf::words({255,2,box_list,128}));
    auto doc=document(fixture);
    const auto field_type=[&](std::string name) {
        for(std::size_t i=0;i<doc.metadata.field_types.size();++i)
            if(doc.metadata.field_types[i].name==name)return static_cast<std::uint16_t>(i);
        const auto index=static_cast<std::uint16_t>(doc.metadata.field_types.size());
        doc.metadata.field_types.push_back({std::move(name),1,0});return index;
    };
    const auto type=[&](std::uint32_t ref,std::string name,std::int32_t parent) -> IgbObject& {
        auto& value=doc.objects[doc.references[ref].value_index];
        value.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
        doc.metadata.object_types.push_back({std::move(name),1,0,parent,16,{}});return value;
    };
    const auto field=[&](std::uint32_t memory,std::size_t offset,std::uint16_t slot,std::string name,std::size_t size) {
        return IgbFieldValue{slot,field_type(std::move(name)),doc.memories[doc.references[memory].value_index].offset+offset,size};
    };
    auto& timer=type(timer_ref,"beBaseInfoRamTimer",pf::base_data_type);
    timer.fields.push_back(field(timer_storage,0,4,"igFloatMetaField",4));
    for(std::uint16_t slot=5;slot<=8;++slot)timer.fields.push_back(field(timer_storage,std::size_t(slot-4)*8,slot,"igLongMetaField",8));
    timer.fields.push_back(field(timer_storage,40,9,"igFloatMetaField",4));
    auto& box=type(box_ref,"igAABox",pf::object);
    box.fields={field(box_storage,0,2,"igVec3fMetaField",12),field(box_storage,12,3,"igVec3fMetaField",12)};
    type(box_list,"igAABoxList",pf::object_list);
    auto& hit=type(hit_ref,"beModelCtrlInfoDataHit",pf::base_data_type);
    hit.fields.push_back(field(hit_storage,0,4,"igBoolMetaField",1));
    hit.fields.push_back(field(hit_storage,4,5,"igBoolMetaField",1));
    hit.fields.push_back(field(hit_storage,8,6,"igObjectRefMetaField",4));
    hit.fields.push_back(field(hit_storage,12,7,"igBoolMetaField",1));
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[box_ref]);
    ActorSceneResource scene(doc,"synthetic/model.igb",7);
    const auto actual_timer=std::dynamic_pointer_cast<ModelSceneTimer>(scene.models().data(timer_ref));
    check(actual_timer && actual_timer->name=="KEIKATIMER" && actual_timer->timer.time_scale==0.5f &&
        std::bit_cast<std::uint64_t>(actual_timer->timer.time_bias)==0xfedcba9889abcdefull &&
        std::bit_cast<std::uint64_t>(actual_timer->timer.end_time)==0x89abcdef01234567ull &&
        actual_timer->timer.saved_world_time==0xfedcba9876543210ull &&
        std::bit_cast<std::uint64_t>(actual_timer->timer.previous_time)==0xaaaaaaaa55555555ull &&
        actual_timer->timer.cached_frames==-2.5f,"Serialized timer fields must preserve exact signed and unsigned 64-bit values.");
    const auto actual_hit=std::dynamic_pointer_cast<ModelSceneHit>(scene.models().data(hit_ref));
    check(actual_hit && actual_hit->hit_player_bullet==255 && actual_hit->hit_enemy_bullet==2 && actual_hit->hit_player_talk==128,
        "HIT registration bytes must remain raw values, not canonicalized booleans.");
    const auto actual_box=std::dynamic_pointer_cast<ModelSceneBox>(actual_hit->boxes->values()[0]);
    check(actual_hit->boxes->count==3 && !actual_hit->boxes->values()[1] &&
        actual_hit->boxes->values()[2]==actual_box && actual_box==scene.models().object(alias) && actual_box->box==scene.box(alias),
        "HIT box list must preserve null/repeated slots and share canonical node-bound box ownership.");
    actual_box->box->minimum[0]=-19;
    check(scene.box(box_ref)->minimum[0]==-19,"Graph and node box APIs must observe the same mutable bound allocation.");
    actual_hit->hit_player_bullet=0;
    check(std::dynamic_pointer_cast<ModelSceneHit>(scene.models().data(hit_ref))->hit_player_bullet==0,
        "HIT registration must read current owner fields instead of decoding original immutable bytes again.");
}
void nested_publication_survives_outer_failure() {
    hitland_scene_fixture::Fixture fixture;
    auto doc=fixture.document();
    auto& info=doc.objects.at(doc.references.at(fixture.info).value_index);
    for(auto& field:info.fields)if(field.slot==10)field.type_index=8; // Float instead of the required integer.
    const auto alias=static_cast<std::uint32_t>(doc.references.size());
    doc.references.push_back(doc.references.at(fixture.source.geom));
    ActorSceneResource scene(std::move(doc),"synthetic/nested-load-failure",9);
    bool failed_at_kind{};
    try {scene.models().hit_land_geometry_info(fixture.info);}
    catch(const ModelSceneError& error) {
        failed_at_kind=std::string_view(error.what()).find("slot 10 requires igIntMetaField")!=std::string_view::npos;
    }
    check(failed_at_kind,"The malformed final kind field must fail after its node graph has loaded.");
    const auto node=scene.mutable_node(fixture.source.owner);
    const auto attributes=node->attribute_list_owner();
    const auto geometry=std::dynamic_pointer_cast<ActorGeometryAttribute>(attributes->backing->at(0));
    check(geometry && attributes==scene.models().list(fixture.source.owner_attrs) &&
        geometry==scene.models().geometry_attribute(fixture.source.geom) && geometry==scene.models().object(alias),
        "Outer rollback discarded a successful nested load already retained by a canonical node.");
    const auto vertices=geometry->vertices;
    const auto streams=vertices->streams;
    const auto memory=streams->slots.at(0);
    check(vertices==scene.models().vertex_array(fixture.source.vertices) && memory && !memory->bytes.empty(),
        "Nested publication did not preserve canonical vertex and physical backing owners.");
    geometry->primitive_count=73;memory->bytes[0]^=1;
    const auto changed=memory->bytes[0];
    for(unsigned i=0;i<3;++i) {
        rejects([&]{scene.models().hit_land_geometry_info(fixture.info);},
            "A repeated outer failure exposed an unfinished info owner.");
        const auto current=scene.models().geometry_attribute(alias);
        check(current==geometry && current->primitive_count==73 && current->vertices==vertices &&
            current->vertices->streams==streams && streams->slots.at(0)==memory && memory->bytes[0]==changed &&
            node->attribute_list_owner()==attributes && attributes==scene.models().list(fixture.source.owner_attrs),
            "Retry replaced an exposed nested owner, allocation or its live mutations.");
    }
}
void malformed() {
    pf::RuntimeFixture fixture;
    fixture.scalar(fixture.instances,2,3);
    auto scene=resource(fixture);
    rejects([&]{scene->models().info(fixture.model);},"Serialized count beyond capacity must reject.");
    fixture.scalar(fixture.instances,2,2);fixture.scalar(fixture.instances,3,0x7fffffffu);scene=resource(fixture);
    rejects([&]{scene->models().info(fixture.model);},"Serialized capacity beyond the bounded backing must reject.");
    fixture.scalar(fixture.instances,3,2);fixture.scalar(fixture.ones[0],5,fixture.model);scene=resource(fixture);
    rejects([&]{scene->models().info(fixture.model);},"Serialized non-null work must not become a prepared default work.");
    fixture.scalar(fixture.ones[0],5,pf::null_ref);
    auto doc=document(fixture);
    auto& ram=doc.objects.at(doc.references[fixture.model_rams[0]].value_index);
    auto duplicate=*doc.field(ram,2);ram.fields.push_back(duplicate);
    scene=std::make_unique<ActorSceneResource>(doc,"synthetic/model.igb",7);
    rejects([&]{scene->models().ram(fixture.model_rams[0]);},"Repeated metadata slots must reject.");
    const auto cycle=fixture.list(pf::data_list,{});fixture.contents(cycle,{cycle});scene=resource(fixture);
    rejects([&]{scene->models().list(cycle);},"Owning graph cycles must reject without publishing partial caches.");
    const auto valid=scene->models().data(fixture.sequence);
    for(unsigned i=0;i<8;++i)rejects([&]{scene->models().list(cycle);},"Repeated failed loads must stay checked.");
    check(scene->models().data(fixture.sequence)==valid,"Failed loads must preserve previously published owner identity.");
    rejects([&]{scene->models().ram(fixture.sequence);},"Wrong concrete RAM type must reject.");
    fixture.scalar(fixture.ones[0],4,pf::null_ref);fixture.scalar(fixture.model,5,pf::null_ref);
    fixture.scalar(fixture.model,6,pf::null_ref);scene=resource(fixture);
    check(!scene->models().info(fixture.model)->rams && !scene->models().info(fixture.model)->data &&
        !std::dynamic_pointer_cast<ModelSceneOne>(scene->models().data(fixture.ones[0]))->node,
        "Null authored info lists and ONE node slots must remain null.");
}
void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);
    ActorSceneResource scene(read_igb_document(mounted.read("_common/p_agumon.igb")),"_common/p_agumon.igb",17);
    const auto info=scene.models().info(1386);
    check(info->name=="p_agumon" && info->kind=="KIND_PLAYER" && info->active==1 && info->rams->count==4,
        "Original Agumon model info fields differ.");
    const auto seq=find_model_seq(*info);const auto selected=find_model_sequence(*seq,"SQ_WAIT");
    check(seq->reference()==14u && seq->sequences->count==28 && selected && selected->commands,
        "Original Agumon SEQ and SQ_WAIT must be actual named list/data owners.");
    const auto decoded=decode_model_sequences(*scene.document_owner(),14,"_common/p_agumon.igb");
    std::size_t occurrences{};
    for(const auto& expected:decoded.sequences()) {
        const auto actual=find_model_sequence(*seq,expected.name);
        check(actual && actual->reference()==expected.reference_index && actual->commands->count==static_cast<std::int32_t>(expected.commands.size()),
            "Original sequence identity/order/count differs from independently decoded prepared evidence.");
        for(std::size_t i=0;i<expected.commands.size();++i) {
            const auto owner=std::dynamic_pointer_cast<ModelSceneCommand>((*actual->commands->backing)[i]);
            check(owner && scene.models().object(expected.commands[i].reference_index)==owner &&
                owner->type()==expected.commands[i].type && owner->name.value_or("")==expected.commands[i].name,
                "Original ordered command occurrence must resolve its one actual physical owner.");
            ++occurrences;
        }
    }
    check(occurrences>100,"Original sequence owner validation must cover the complete authored command occurrence set.");
    const std::array<std::uint32_t,4> rams{1725,6417,6420,6622},data{6435,51,52,53},
        ones{1724,121,1594,4783},nodes{6390,6457,6524,6590};
    std::array<std::shared_ptr<ModelSceneRam>,4> owners;
    for(std::size_t i=0;i<4;++i) {
        owners[i]=ram_at(info,i);const auto one=find_model_one(*owners[i]);
        check(owners[i]==scene.models().ram(rams[i]) && owners[i]->data->reference()==data[i] &&
            owners[i]->number==0 && !owners[i]->active && owners[i]->words==std::array<std::uint32_t,4>{} &&
            owners[i]->parent.expired() && owners[i]->manager.expired(),"Original RAM fields, order or initial backlinks differ.");
        check(one->reference()==ones[i] && !one->work && one->node==scene.mutable_node(nodes[i]) && one->node->matrix_owner(),
            "Original ONE work/node identity differs.");
        const auto x=(*one->node->matrix_owner())[12];
        check(std::fabs(x-(-26.5f+13.25f*static_cast<float>(i)))<0.03f,
            "Original authored node placement was replaced by an invented identity matrix.");
    }
    auto manager=std::make_shared<ModelSceneManager>();
    for(std::size_t i=0;i<4;++i)register_model_ram(info,owners[i],static_cast<std::int32_t>(i),manager);
    const auto timer=start_model_timer(*owners[0],"KEIKATIMER",1234);
    const auto one=find_model_one(*owners[0]);one->work=make_model_player_work();const auto old=one->work;
    one->work=make_model_player_work();
    check(find_model_timer(*owners[0],"KEIKATIMER")==timer && old!=one->work && owners[0]->number==0 &&
        owners[3]->number==3 && owners[3]->parent.lock()==info,"Original RAM timer and registration must survive fresh work replacement.");
}
}
int main(int argc,char** argv) {
    try {
        ordered_identity_and_retention();live_lists_and_lookup();timer_records_and_registration();sequence_owners_and_live_fields();serialized_timer_and_hit();
        nested_publication_survives_outer_failure();malformed();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Model scene checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model scene after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
