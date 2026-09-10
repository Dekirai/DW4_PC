#include "dw4/model_hit_body.hpp"
#include "dw4/player_runtime_binding.hpp"
#include "dw4/resources.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace fixture=dw4::test;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const ModelHitBodyError&){return;}
    throw std::runtime_error("Invalid model HIT ownership or state was accepted.");
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
constexpr CollisionBox local{{-1,0,-3},{3,6,5}};
constexpr CollisionBox second{{-4,-2,-6},{2,4,0}};
struct Fixture : fixture::ScriptFixture {
    struct Hit {std::uint32_t reference,storage;};
    struct Box {std::uint32_t reference,storage;};
    std::uint32_t model,data_list,ram_list,box_list;
    std::vector<std::uint32_t> rams,ram_storage;
    std::vector<Hit> hits;
    std::vector<Box> boxes;
    Fixture() {
        add_box(local);add_box(second);
        box_list=list(fixture::object_list,{boxes[0].reference,boxes[1].reference,boxes[0].reference});
        add_hit("HIT",box_list);
        data_list=list(fixture::object_list,{hits[0].reference});
        for(unsigned i=0;i<4;++i) {
            rams.push_back(add(fixture::object));
            ram_storage.push_back(memory(fixture::words({0,0,0,0,0,0,fixture::null_ref})));
        }
        ram_list=list(fixture::object_list,rams);
        model=add(fixture::scene_info);text(model,2,"p_fixture");scalar(model,4,1);
        scalar(model,5,data_list);scalar(model,7,ram_list);
        exported_info_list=list(fixture::info_list,{model});
    }
    void add_box(const CollisionBox& box) {
        fixture::Bytes bytes;
        for(const auto point:{box.minimum,box.maximum})for(const auto value:point)fixture::push32(bytes,std::bit_cast<std::uint32_t>(value));
        boxes.push_back({add(fixture::object),memory(std::move(bytes))});
    }
    void add_hit(const std::string& name,std::uint32_t list_ref) {
        const auto ref=add(fixture::named_object);text(ref,2,name);
        hits.push_back({ref,memory(fixture::words({0,0x100,list_ref,0}))});
    }
    IgbDocument document() const {
        auto doc=fixture::ScriptFixture::document();
        const auto type=[&](std::string name,std::int32_t parent) {
            const auto index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back({std::move(name),1,0,parent,16,{}});return index;
        };
        const auto data_type=type("beBaseInfoData",fixture::named_object);
        const auto hit_type=type("beModelCtrlInfoDataHit",static_cast<std::int32_t>(data_type));
        const auto model_type=type("beModelCtrlInfo",fixture::info_base);
        const auto ram_type=type("beBaseInfoRam",fixture::object);
        const auto box_type=type("igAABox",fixture::object);
        const auto list_type=type("beBaseInfoDataList",fixture::object_list);
        const auto ram_list_type=type("beBaseInfoRamList",fixture::object_list);
        const auto box_list_type=type("igAABoxList",fixture::object_list);
        const auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        const auto offset=[&](std::uint32_t ref){return doc.memories.at(doc.references.at(ref).value_index).offset;};
        // Extend the parsed synthetic graph with exact typed fields whose bytes
        // are serialized in fixture-owned memory. No original bytes are used.
        object(model).type_index=model_type;
        for(auto& field:object(model).fields)if(field.slot==7)field.slot=6;
        object(data_list).type_index=list_type;object(ram_list).type_index=ram_list_type;object(box_list).type_index=box_list_type;
        for(const auto& hit:hits) {
            auto& value=object(hit.reference);value.type_index=hit_type;const auto at=offset(hit.storage);
            value.fields.insert(value.fields.end(),{{3,1,at,4},{4,4,at+4,1},{5,4,at+5,1},{6,2,at+8,4},{7,4,at+12,1}});
        }
        for(const auto& box:boxes) {
            auto& value=object(box.reference);value.type_index=box_type;const auto at=offset(box.storage);
            value.fields={{2,8,at,12},{3,8,at+12,12}};
        }
        for(std::size_t i=0;i<rams.size();++i) {
            auto& value=object(rams[i]);value.type_index=ram_type;const auto at=offset(ram_storage[i]);
            value.fields={{2,1,at,4},{5,4,at+4,1},{6,1,at+8,4},{7,1,at+12,4},{8,1,at+16,4},{9,1,at+20,4},{10,2,at+24,4}};
        }
        return doc;
    }
    ModelHitBodyDefinition definition() const {return decode_model_hit_body(document(),model,"synthetic/body.igb",7);}
};
void definition_ownership() {
    Fixture f;auto definition=f.definition();
    check(definition.source()=="synthetic/body.igb" && definition.source_occurrence()==7 && definition.model_reference()==f.model &&
        definition.model_name()=="p_fixture" && definition.data_list_reference()==f.data_list && definition.ram_list_reference()==f.ram_list,
        "HIT definition lost source/info/list provenance.");
    check(definition.ram_references()==f.rams && definition.ram_owners()==f.rams && definition.hit_reference()==f.hits[0].reference &&
        definition.box_list_reference()==f.box_list && definition.box_references()==std::vector<std::uint32_t>{f.boxes[0].reference,f.boxes[1].reference,f.boxes[0].reference},
        "HIT definition changed ordered references or duplicate box occurrences.");
    check(definition.local_boxes()==std::vector<CollisionBox>{local,second,local},"Local boxes did not survive their document's destruction.");
    const auto copy=definition;
    check(&copy.local_boxes()==&definition.local_boxes(),"Definition copies duplicate mutable local geometry rather than share immutable values.");
    auto doc=f.document();doc.references[f.rams[1]]=doc.references[f.rams[0]];doc.references[f.boxes[1].reference]=doc.references[f.boxes[0].reference];
    const auto aliased=decode_model_hit_body(doc,f.model,"aliased",8);
    check(aliased.ram_references()==f.rams && aliased.ram_owners()[1]==f.rams[0] && aliased.box_references()[1]==f.boxes[1].reference &&
        aliased.local_boxes()==std::vector<CollisionBox>(3,local),"Distinct serialized aliases lost their canonical RAM or ordered box identity.");
    ModelHitBodyCache alias_cache(aliased,1);
    check(alias_cache.ram_reference()==f.rams[1] && alias_cache.ram_owner_reference()==f.rams[0] && alias_cache.instance_index()==1,
        "Cache confused serialized instance provenance with canonical physical RAM identity.");
    const auto other=decode_model_hit_body(f.document(),f.model,"other/source",9);
    check(other.source()!=definition.source() && other.source_occurrence()!=definition.source_occurrence(),"Loaded source occurrences were conflated.");
    check(definition.hit_player_bullet()==0 && definition.hit_enemy_bullet()==1 && definition.hit_player_talk()==0,
        "HIT category flags lost their authored byte order.");
    f.records[f.hits[0].storage].data[4]=2;
    f.records[f.hits[0].storage].data[5]=255;
    f.records[f.hits[0].storage].data[12]=128;
    const auto flags=f.definition();
    check(flags.hit_player_bullet()==2 && flags.hit_enemy_bullet()==255 && flags.hit_player_talk()==128,
        "HIT registration flags normalized or rejected original nonzero bytes.");
    rejects([&]{ModelHitBodyCache bad(definition,4);});
}
void first_named_hit_and_empty() {
    Fixture f;f.add_hit("HIT",fixture::null_ref);f.contents(f.data_list,{f.hits[0].reference,f.hits[1].reference,f.hits[0].reference});
    check(f.definition().hit_reference()==f.hits[0].reference,"Later duplicate HIT name replaced the original first match.");
    f.contents(f.data_list,{f.hits[1].reference,f.hits[0].reference});rejects([&]{f.definition();});
    f.contents(f.data_list,{f.hits[0].reference});f.text(f.hits[0].reference,2,"hit");rejects([&]{f.definition();});
    f.text(f.hits[0].reference,2,"HIT");f.contents(f.box_list,{});
    auto empty=f.definition();ModelHitBodyCache cache(empty,0);
    check(empty.local_boxes().empty() && cache.update(identity(),0).empty() && cache.initialized() && cache.calc_time()==0,
        "An authored empty HIT list was mistaken for a missing graph.");
    check(cache.update(identity(),123).empty() && cache.calc_time()==123,"Empty authored cache failed to retain changed system time.");
    f.contents(f.ram_list,{});const auto no_instances=f.definition();rejects([&]{ModelHitBodyCache bad(no_instances,0);});
}
void cache_lifecycle() {
    Fixture f;const auto definition=f.definition();ModelHitBodyCache cache(definition,0),other(definition,1);
    check(!cache.initialized() && cache.calc_time()==0 && cache.boxes().empty() && cache.minimum_distances().empty(),
        "Fresh cache performed original first-use allocations before its first query.");
    cache.update(identity(),0);
    check(cache.initialized() && cache.boxes().size()==3 && cache.minimum_distances()[0]==std::array<float,3>{2,3,4} &&
        cache.maximum_distances()[0]==std::array<float,3>{2,3,4},"First use did not capture separate local distances in parallel lists.");
    for(const auto& box:cache.boxes())for(std::size_t axis=0;axis<3;++axis)check(
        std::bit_cast<std::uint32_t>(box.minimum[axis])==0x7f7fffffu && std::bit_cast<std::uint32_t>(box.maximum[axis])==0xff7fffffu,
        "Time-zero query replaced exact finite inverted AABox defaults.");
    auto transform=identity();transform[0]=0;transform[2]=-2;transform[5]=3;transform[8]=4;transform[10]=0;
    transform[12]=10;transform[13]=20;transform[14]=30;
    const auto* output=cache.boxes().data();cache.update(transform,0);
    check(cache.boxes().data()==output && std::bit_cast<std::uint32_t>(cache.boxes()[0].minimum[0])==0x7f7fffffu,
        "First-use state or changed matrix incorrectly forced a same-time refresh.");
    cache.update(transform,1);
    check(cache.boxes()[0]==CollisionBox{{12,26,24},{16,32,32}} && cache.boxes()[2]==cache.boxes()[0] && cache.calc_time()==1,
        "Body transform rotated/scaled authored extents instead of restoring local distances around transformed center.");
    const auto saved=cache.boxes();auto translated=transform;translated[12]+=100;
    check(cache.update(translated,1)==saved,"Same timestamp refreshed boxes after a matrix change.");
    cache.update(translated,0x100000001ull);
    check(cache.calc_time()==0x100000001ull && cache.boxes()[0].minimum[0]==112,"Cache ignored the timestamp's high word.");
    cache.update(transform,0x100000002ull);
    check(cache.boxes()[0].minimum[0]==12,"Cache ignored the timestamp's low word.");
    cache.update(identity(),0);
    check(cache.boxes()==definition.local_boxes() && cache.calc_time()==0,"Returning to time zero restored factory defaults instead of refreshing existing work.");
    auto copy=cache;copy.update(translated,2);
    check(cache.boxes()==definition.local_boxes() && copy.boxes()!=cache.boxes() && copy.minimum_distances()==cache.minimum_distances(),
        "Cache copies shared mutable output lists or lost captured distances.");
    check(!other.initialized() && other.boxes().empty() && other.ram_reference()!=cache.ram_reference(),"One target initialized another target's body cache.");
    other.update(translated,4);
    check(cache.calc_time()==0 && cache.boxes()==definition.local_boxes(),"Other target refresh changed the original owner's timestamp or boxes.");
}
void checked_cache_failures() {
    Fixture f;ModelHitBodyCache fresh(f.definition(),0);
    auto invalid=identity();invalid[3]=std::numeric_limits<float>::infinity();
    rejects([&]{fresh.update(invalid,0);});
    check(!fresh.initialized() && fresh.boxes().empty() && fresh.minimum_distances().empty(),"Invalid first query published partial list allocation.");
    auto overflow=identity();overflow[0]=std::numeric_limits<float>::max();
    rejects([&]{fresh.update(overflow,1);});
    check(!fresh.initialized() && fresh.calc_time()==0 && fresh.maximum_distances().empty(),"Failed first computation published its timestamp or distances.");
    fresh.update(identity(),1);const auto saved=fresh.boxes();const auto minima=fresh.minimum_distances();
    rejects([&]{fresh.update(invalid,1);});rejects([&]{fresh.update(overflow,2);});
    check(fresh.boxes()==saved && fresh.minimum_distances()==minima && fresh.calc_time()==1,
        "Checked cache failure partially changed an already initialized owner.");
    check(fresh.update(overflow,1)==saved,"Finite same-time matrix was evaluated despite the original timestamp cache hit.");
}
void asymmetric_and_inverted_distances() {
    Fixture f;
    fixture::word(f.records[f.boxes[0].storage].data,0,0x3f800000u);
    fixture::word(f.records[f.boxes[0].storage].data,12,0x3f800001u);
    ModelHitBodyCache narrow(f.definition(),0);narrow.update(identity(),0);
    check(narrow.minimum_distances()[0][0]==0 && narrow.maximum_distances()[0][0]==0x1p-23f,
        "Cache collapsed separately rounded local distances into a symmetric radius.");
    narrow.update(identity(),1);
    check(std::bit_cast<std::uint32_t>(narrow.boxes()[0].minimum[0])==0x3f800000u &&
        std::bit_cast<std::uint32_t>(narrow.boxes()[0].maximum[0])==0x3f800001u,"Body refresh discarded the distinct maximum distance.");
    fixture::word(f.records[f.boxes[0].storage].data,0,std::bit_cast<std::uint32_t>(2.f));
    fixture::word(f.records[f.boxes[0].storage].data,12,std::bit_cast<std::uint32_t>(-2.f));
    ModelHitBodyCache inverted(f.definition(),0);auto translated=identity();translated[12]=100;
    inverted.update(translated,1);
    check(inverted.boxes()[0].minimum[0]==-2 && inverted.boxes()[0].maximum[0]==2,
        "Finite inverted local X bounds bypassed original transform/absolute-distance rules.");
}
void malformed_graphs() {
    Fixture f;const auto decode=[&](const IgbDocument& doc){return decode_model_hit_body(doc,f.model,"source",0);};
    rejects([&]{decode_model_hit_body(f.document(),f.model,"",0);});
    rejects([&]{decode_model_hit_body(f.document(),f.model,std::string("bad\0source",10),0);});
    rejects([&]{decode_model_hit_body(f.document(),f.model,"source",std::numeric_limits<std::size_t>::max());});
    auto doc=f.document();doc.info_list_reference.reset();rejects([&]{decode(doc);});
    doc=f.document();doc.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{decode(doc);});
    const auto mutate_type=[&](std::uint32_t ref) {auto graph=f.document();graph.metadata.object_types[graph.object(ref).type_index].name+="Derived";rejects([&]{decode(graph);});};
    for(const auto ref:{f.model,f.ram_list,f.rams[0],f.data_list,f.hits[0].reference,f.box_list,f.boxes[0].reference})mutate_type(ref);
    for(const auto ref:{f.model,f.rams[0],f.hits[0].reference,f.boxes[0].reference}) {
        auto graph=f.document();auto& field=graph.objects[graph.references[ref].value_index].fields.back();
        field.size+=1;rejects([&]{decode(graph);});
        graph=f.document();graph.objects[graph.references[ref].value_index].fields.back().type_index=0;rejects([&]{decode(graph);});
        graph=f.document();graph.objects[graph.references[ref].value_index].fields.back().offset=graph.bytes().size();rejects([&]{decode(graph);});
    }
    // All authored category flags remain byte fields even though any nonzero
    // value enables registration. Slot7 is covered by the range checks above.
    for(const auto slot:{4,5}) {
        for(unsigned mutation=0;mutation<3;++mutation) {
            auto graph=f.document();auto& fields=graph.objects[graph.references[f.hits[0].reference].value_index].fields;
            for(auto& field:fields)if(field.slot==slot) {
                if(mutation==0)field.size=2;
                else if(mutation==1)field.type_index=1;
                else field.offset=graph.bytes().size();
            }
            rejects([&]{decode(graph);});
        }
    }
    auto bad=f;bad.contents(bad.box_list,{fixture::null_ref});rejects([&]{bad.definition();});
    bad=f;bad.scalar(bad.box_list,2,4097);bad.scalar(bad.box_list,3,4097);rejects([&]{bad.definition();});
    bad=f;bad.scalar(bad.ram_list,3,1025);rejects([&]{bad.definition();});
    bad=f;bad.scalar(bad.box_list,3,2);rejects([&]{bad.definition();});
    bad=f;bad.scalar(bad.box_list,4,fixture::null_ref);rejects([&]{bad.definition();});
    bad=f;bad.scalar(bad.model,5,fixture::null_ref);rejects([&]{bad.definition();});
    bad=f;fixture::word(bad.records[bad.hits[0].storage].data,8,fixture::null_ref);rejects([&]{bad.definition();});
    bad=f;bad.scalar(bad.model,4,2);rejects([&]{bad.definition();});
    bad=f;fixture::word(bad.records[bad.boxes[0].storage].data,0,0x7fc00000u);rejects([&]{bad.definition();});
    bad=f;bad.text(bad.hits[0].reference,2,std::string(4096,'x'));bad.contents(bad.data_list,std::vector<std::uint32_t>(300,bad.hits[0].reference));
    rejects([&]{bad.definition();});
    bad=f;bad.text(bad.hits[0].reference,2,"different");rejects([&]{bad.definition();});
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);const std::string source="_common/p_agumon.igb";
    const auto document=read_igb_document(resources.read(source));const auto definition=decode_model_hit_body(document,1386,source,7);
    check(definition.hit_reference()==1997 && definition.box_list_reference()==6395 && definition.box_references()==std::vector<std::uint32_t>{1316} &&
        definition.local_boxes()==std::vector<CollisionBox>{{{-2.5f,0,-2.5f},{2.5f,7,2.5f}}} &&
        definition.hit_player_bullet()==0 && definition.hit_enemy_bullet()==1 && definition.hit_player_talk()==0,
        "Original Agumon HIT graph, flags or authored endpoints changed.");
    check(definition.ram_references()==std::vector<std::uint32_t>{1725,6417,6420,6622},"Original ordered MODELCTRL RAM refs changed.");
    std::vector<ModelHitBodyCache> caches;
    for(std::uint32_t i=0;i<4;++i) {
        const auto binding=decode_player_runtime_binding(document,{1386,546,i,1},source,7);caches.emplace_back(definition,i);
        check(caches.back().ram_reference()==binding.model_ram_reference() && caches.back().definition().source_occurrence()==binding.source_occurrence() &&
            caches.back().definition().model_reference()==binding.model_reference() && caches.back().definition().model_name()==binding.model_name(),
            "Original target cache did not match its actual player graph ownership.");
        const auto& defaults=caches.back().update(identity(),0);
        check(defaults.size()==1 && std::bit_cast<std::uint32_t>(defaults[0].minimum[0])==0x7f7fffffu &&
            std::bit_cast<std::uint32_t>(defaults[0].maximum[0])==0xff7fffffu,"Original target lost first-query time-zero defaults.");
        auto translated=identity();translated[12]=float(10*i);translated[13]=2;translated[14]=-4;
        const auto& boxes=caches.back().update(translated,1);
        check(boxes[0]==CollisionBox{{float(10*i)-2.5f,2,-6.5f},{float(10*i)+2.5f,9,-1.5f}},"Original body cache did not retain authored extents with its live target translation.");
        translated[12]+=100;const auto saved=boxes;
        check(caches.back().update(translated,1)==saved,"Original target refreshed on a same-time matrix change.");
    }
    caches[0].update(identity(),2);
    for(std::size_t i=1;i<caches.size();++i)check(caches[i].calc_time()==1 && caches[i].boxes()[0].minimum[0]==float(10*i)-2.5f,
        "One original target's timestamp update changed another target's cache.");
    std::cout<<"Original model HIT bodies: targets=4 local_boxes=1 time_zero_defaults=4\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        definition_ownership();first_named_hit_and_empty();cache_lifecycle();checked_cache_failures();asymmetric_and_inverted_distances();malformed_graphs();
        if(args.size()==2)original(args[1]);else if(args.size()!=1)throw std::runtime_error("Usage: dw4_model_hit_body_tests [EXTRACTED_ROOT]");
        std::cout<<"model_hit_body: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"model_hit_body after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);
}
