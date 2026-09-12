#include "dw4/model_scene.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

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
}
void timer_records_and_registration() {
    auto info=std::make_shared<ModelSceneInfo>();auto ram=std::make_shared<ModelSceneRam>();
    auto actual_manager=std::make_shared<int>(5);
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
    auto manager=std::make_shared<int>(1);
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
        ordered_identity_and_retention();live_lists_and_lookup();timer_records_and_registration();serialized_timer_and_hit();malformed();
        const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Model scene checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model scene after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
