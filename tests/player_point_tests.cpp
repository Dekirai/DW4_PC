#include "actor_skin_fixture.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/player_point.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace actor_skin_fixture;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F>void rejects(F operation) {++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error("Invalid PLAYERPOINT operation accepted.");}
IgbObject& object_at(IgbDocument& doc,std::uint32_t ref) {return doc.objects.at(doc.references.at(ref).value_index);}
struct Fixture : Graph {
    struct Extra {std::uint32_t ref;std::string name,parent;std::map<std::uint16_t,std::pair<std::uint16_t,std::uint32_t>> fields;};
    std::vector<Extra> extra;
    std::uint32_t info,ram,ram_list,exports,group_list,polygon;
    std::vector<std::uint32_t> groups,points,children;
    std::uint32_t custom(const char* name,const char* parent="igObject") {
        const auto ref=add(actor_skin_fixture::object);extra.push_back({ref,name,parent,{}});return ref;
    }
    void value(std::uint32_t ref,std::uint16_t slot,std::uint16_t type,Bytes bytes) {
        const auto mem=memory(std::move(bytes));
        for(auto& object:extra)if(object.ref==ref){object.fields[slot]={type,mem};return;}
        throw std::runtime_error("Fixture custom owner absent.");
    }
    void value(std::uint32_t ref,std::uint16_t slot,std::uint16_t type,std::uint32_t n) {value(ref,slot,type,words({n}));}
    void set(std::uint32_t ref,std::uint16_t slot,std::uint32_t n) {
        for(auto& object:extra)if(object.ref==ref){word(records[object.fields.at(slot).second].data,0,n);return;}
        throw std::runtime_error("Fixture set owner absent.");
    }
    void label(std::uint32_t ref,std::optional<std::string> text) {
        auto bytes=words({text?static_cast<std::uint32_t>(text->size()+1):0});
        if(text){bytes.insert(bytes.end(),text->begin(),text->end());bytes.push_back(0);}value(ref,2,0,std::move(bytes));
    }
    std::uint32_t typed_list(const char* name,const std::vector<std::uint32_t>& refs) {
        const auto ref=list(object_list,refs);extra.push_back({ref,name,"igObjectList",{}});return ref;
    }
    std::uint32_t add_info(std::optional<std::string> name,std::uint32_t rams,std::uint32_t list) {
        const auto added=custom("bePoint01Info","igInfo");label(added,name);value(added,4,4,Bytes{255});
        value(added,5,2,null_ref);value(added,6,2,rams);value(added,7,2,polygon);value(added,8,2,list);return added;
    }
    Fixture() {
        polygon=custom("igSwitch","igGroup");
        for(unsigned g=0;g<2;++g) {
            std::vector<std::uint32_t> members;
            for(unsigned p=0;p<4;++p) {
                const auto point=custom("igTransform","igGroup");points.push_back(point);members.push_back(point);
                label(point,"Point");value(point,5,1,0);value(point,7,2,list(node_list,{}));
                value(point,8,7,matrix_bytes(translated(static_cast<float>(10*g+p),static_cast<float>(g),2)));
                value(point,9,1,0);value(point,11,2,null_ref);
            }
            const auto group=custom("beGroupKeep","igGroup");groups.push_back(group);label(group,g?std::nullopt:std::optional<std::string>{"Entry"});
            children.push_back(list(node_list,members));value(group,7,2,children.back());
        }
        group_list=typed_list("beGroupKeepList",groups);ram=custom("beBaseInfoRam");value(ram,2,1,91);value(ram,5,4,Bytes{0});
        for(std::uint16_t slot=6;slot<=9;++slot)value(ram,slot,1,99);
        value(ram,10,2,null_ref);ram_list=typed_list("beBaseInfoRamList",{ram});
        info=add_info("",ram_list,group_list);exports=typed_list("igInfoList",{info});
    }
    IgbDocument document() const {
        auto doc=Graph::document();
        const auto type=[&](const std::string& name,const std::string& parent) {
            for(std::size_t i=0;i<doc.metadata.object_types.size();++i)if(doc.metadata.object_types[i].name==name)return static_cast<std::uint32_t>(i);
            std::int32_t parent_index=-1;
            for(std::size_t i=0;i<doc.metadata.object_types.size();++i)if(doc.metadata.object_types[i].name==parent)parent_index=static_cast<std::int32_t>(i);
            const auto index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back({name,1,0,parent_index,32,{}});return index;
        };
        type("igInfo","igObject");
        for(const auto& entry:extra) {
            auto& object=object_at(doc,entry.ref);object.type_index=type(entry.name,entry.parent);
            for(const auto& [slot,source]:entry.fields) {
                const auto& memory=doc.memories.at(doc.references.at(source.second).value_index);
                object.fields.push_back({slot,source.first,memory.offset,memory.size});
            }
        }
        doc.info_list_reference=exports;return doc;
    }
    PlayerPointProgram program(std::uint32_t occurrence=7) const {
        return decode_player_points(std::make_shared<ActorSceneResource>(document(),"synthetic/points.igb",occurrence));
    }
};
MessengerDispatch send(ActionEventRuntime& events,std::string command,std::initializer_list<std::int32_t> integers={},
    std::optional<std::string> name={},std::int32_t index=-1) {
    auto& messenger=events.messenger();messenger.select("PLAYERPOINT",std::move(name),index);
    for(const auto value:integers)messenger.buffer().add_int(value);
    return messenger.begin_send(std::move(command));
}
void complete(ActionEventRuntime& events,MessengerDispatch& dispatch) {
    const auto result=events.resume(dispatch);check(result.stop==MessengerStop::completed,"PLAYERPOINT delivery did not complete.");
}
void command(ActionEventRuntime& events,std::string name,std::initializer_list<std::int32_t> values={}) {
    auto dispatch=send(events,std::move(name),values);complete(events,dispatch);
}
void routing_and_matrices() {
    Fixture fixture;const auto program=fixture.program();ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(program);
    const auto& ram=runtime.ram({7,fixture.ram});
    check(ram.manager==runtime.manager_id() && ram.info==ControllerObjectId{7,fixture.info} && ram.index==0 &&
        ram.active==0 && ram.words==std::array<std::int32_t,4>{},"PLAYERPOINT generic RAM initialization changed.");
    check(runtime.saved_group()==0,"Original manager integer default changed.");
    for(std::int32_t group=0;group<2;++group)for(std::int32_t player=0;player<4;++player) {
        command(events,"SETGENERATEPOS",{group});command(events,"GETMATRIX",{99,player});
        check(events.messenger().buffer().matrices()==std::vector<AnimationMatrix>{translated(float(group*10+player),float(group),2)},
            "GETMATRIX used integer0 instead of its manager group and integer1.");
        command(events,"GETMATRIXDIRECT",{1-group,player});
        check(events.messenger().buffer().matrices()[0]==translated(float((1-group)*10+player),float(1-group),2) && runtime.saved_group()==group,
            "GETMATRIXDIRECT changed manager state or chose the wrong point.");
    }
    command(events,"SETGENERATEPOS",{0x7fffffff});command(events,"GETMATRIX",{0,2});
    check(events.messenger().buffer().matrices()[0]==translated(2,0,2) && runtime.saved_group()==0x7fffffff,
        "Excessively positive group did not fall back locally to zero.");
    command(events,"GETGENERATEPOS",{77});check(events.messenger().buffer().integers()==std::vector<std::int32_t>{77,0x7fffffff},
        "GETGENERATEPOS did not append to the actual argument owner.");
    command(events,"GETPOINTNAME");check(events.messenger().buffer().strings()==std::vector<std::optional<std::string>>{"Entry",""},
        "GETPOINTNAME lost order or the original null-to-empty conversion.");
    auto dispatch=send(events,"GETMATRIXDIRECT",{0,0});auto matrix=identity();matrix[0]=std::numeric_limits<float>::denorm_min();
    matrix[1]=-std::numeric_limits<float>::denorm_min();runtime.set_matrix({7,fixture.points[0]},matrix);
    events.messenger().buffer().add_matrix(translated(50));events.messenger().buffer().add_string("kept");complete(events,dispatch);
    const auto& result=events.messenger().buffer().matrices();
    check(result.size()==2 && result[0]==translated(50) && result[1][0]==0 && std::signbit(result[1][1]),
        "GETMATRIX did not preserve existing matrices or original PSQ tiny-value copies.");
    complete(events,dispatch);check(result.size()==2,"Completed matrix delivery replayed its append.");
    check(runtime.point({7,fixture.points[0]}).matrix[0]==std::numeric_limits<float>::denorm_min(),"Sending mutated the point owner.");
    auto invalid=matrix;invalid[0]=std::numeric_limits<float>::infinity();rejects([&]{runtime.set_matrix({7,fixture.points[0]},invalid);});
    check(runtime.point({7,fixture.points[0]}).revision==1,"Rejected point mutation changed the owner revision.");
    for(const auto* unknown:{"getmatrix","UNKNOWN",""})command(events,unknown);
    auto filtered=send(events,"GETGENERATEPOS");events.messenger().buffer().set_enabled_only(1);complete(events,filtered);
    check(events.messenger().buffer().integers().empty(),"Enabled-only delivery ignored raw activity.");
    runtime.set_active(ram.id,255);filtered=send(events,"GETGENERATEPOS");events.messenger().buffer().set_enabled_only(255);complete(events,filtered);
    check(events.messenger().buffer().integers()==std::vector<std::int32_t>{0x7fffffff},"Nonzero raw activity was converted to an exact-one gate.");
}
void nulls_and_failures() {
    Fixture null_group;null_group.contents(null_group.group_list,{null_ref,null_group.groups[1]});
    {ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(null_group.program());
        command(events,"GETMATRIX",{0,-999});check(events.messenger().buffer().matrices()==std::vector<AnimationMatrix>{identity()},
            "Explicit null group did not return original initialized library identity independently of player index.");}
    const auto fault=[&](Fixture& fixture,const char* name,std::initializer_list<std::int32_t> values) {
        ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(fixture.program());auto dispatch=send(events,name,values);
        check(events.resume(dispatch).stop==MessengerStop::faulted,"Original unsafe PLAYERPOINT access did not become a checked fault.");
    };
    Fixture fixture;fault(fixture,"GETMATRIX",{0});fault(fixture,"SETGENERATEPOS",{});fault(fixture,"GETMATRIXDIRECT",{-1,0});
    fault(fixture,"GETMATRIXDIRECT",{0,-1});fault(fixture,"GETMATRIXDIRECT",{0,4});fault(null_group,"GETPOINTNAME",{});
    Fixture child;child.contents(child.children[0],{null_ref});fault(child,"GETMATRIX",{0,0});
    Fixture no_children;no_children.set(no_children.groups[0],7,null_ref);fault(no_children,"GETMATRIX",{0,0});
    Fixture empty;empty.contents(empty.group_list,{});fault(empty,"GETMATRIX",{0,0});
    Fixture absent;absent.set(absent.info,8,null_ref);fault(absent,"GETMATRIX",{0,0});
    {ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(fixture.program());
        command(events,"SETGENERATEPOS",{std::numeric_limits<std::int32_t>::min()});
        check(runtime.saved_group()==std::numeric_limits<std::int32_t>::min(),"SETGENERATEPOS clamped the saved signed group.");
        auto dispatch=send(events,"GETMATRIX",{0,0});check(events.resume(dispatch).stop==MessengerStop::faulted,"Negative saved group was clamped.");}
}
void aliases_and_lifetime() {
    Fixture fixture;fixture.contents(fixture.children[1],{fixture.points[0],fixture.points[0]});
    const auto second=fixture.add_info("second",fixture.typed_list("beBaseInfoRamList",{fixture.ram,fixture.ram}),fixture.group_list);
    fixture.contents(fixture.exports,{fixture.info,second,fixture.info});auto doc=fixture.document();
    const auto raw_alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[fixture.points[0]]);
    auto resource=std::make_shared<ActorSceneResource>(std::move(doc),"aliases",8);const auto program=decode_player_points(resource);
    ActionEventRuntime events;const auto id=ControllerObjectId{8,fixture.ram};
    {
        PlayerPointRuntime runtime(events);runtime.register_source(program);
        check(events.messenger().registration_count()==4 && runtime.ram(id).info==ControllerObjectId{8,fixture.info} && runtime.ram(id).index==0,
            "Ordered shared RAM registration did not preserve every row and final parent/ordinal.");
        const auto& first=runtime.point({8,fixture.points[0]});const auto& alias=runtime.point({8,raw_alias});
        check(&first==&alias && first.definition->node==resource->node(raw_alias),"Physical node alias lost its actual scene owner.");
        runtime.set_matrix({8,raw_alias},translated(432));
        const auto& groups=runtime.ram(id).parent->groups;
        check(groups[0]->points[0]==groups[1]->points[0] && groups[1]->points[0]==groups[1]->points[1] && first.matrix==translated(432),
            "Aliased point entries did not share a single mutable matrix.");
        auto dispatch=send(events,"GETMATRIXDIRECT",{1,1},"second",1);complete(events,dispatch);
        check(events.messenger().buffer().matrices()==std::vector<AnimationMatrix>{translated(432)},"Cached registration row did not resolve the live shared parent.");
        rejects([&]{runtime.register_source(program);});check(events.messenger().registration_count()==4,"Rejected duplicate source changed registration.");
    }
    auto dispatch=send(events,"GETMATRIXDIRECT",{1,0},"second",0);complete(events,dispatch);
    check(events.messenger().buffer().matrices()[0]==translated(432),"Registered point manager did not retain owners beyond its facade.");
    Fixture other;PlayerPointRuntime second_manager(events);second_manager.register_source(other.program(9));
    auto set=send(events,"SETGENERATEPOS",{1});complete(events,set);
    check(second_manager.saved_group()==1,"Manager group update did not reach repeated loaded occurrences.");
}
void switch_service() {
    Fixture fixture;ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(fixture.program());
    auto dispatch=send(events,"SHOWPOLYGON");check(events.resume(dispatch).stop==MessengerStop::service_required,
        "Unimplemented switch mutation was falsely completed.");
    unsigned calls{};bool accept{};std::uint32_t mask{};
    runtime.set_services({[&](const auto& resource,ControllerObjectId id,bool show) {
        ++calls;check(resource==runtime.sources()[0].resource() && id==ControllerObjectId{7,fixture.polygon} && show,
            "Suspended switch operation lost its original resource or target.");
        if(!accept)return false;mask=0xffffffffu;return true;
    }});
    check(events.resume(dispatch).stop==MessengerStop::service_required && calls==1 && mask==0,"Declined switch service was marked complete.");
    accept=true;complete(events,dispatch);check(mask==0xffffffffu && calls==2,"Accepted switch effect did not complete.");
    complete(events,dispatch);check(calls==2,"Completed switch operation replayed.");
    runtime.set_services({[&](const auto&,ControllerObjectId,bool show){mask=show?0xffffffffu:0;return true;}});
    command(events,"HIDEPOLYGON");check(mask==0,"HIDEPOLYGON lost the original clear operation.");
}
void sources_and_buffer_owners() {
    Fixture first,second;first.label(first.info,std::nullopt);second.label(second.info,"Elsewhere");
    const auto initial=first.program();check(!initial.infos()[0].name,"Authored null info name became an empty string.");
    ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(initial);
    command(events,"SETGENERATEPOS",{1});runtime.register_source(second.program(8));
    check(runtime.saved_group()==1 && runtime.sources().size()==2,"Loading another source reset manager-wide point state.");
    runtime.set_matrix({7,first.points[4]},translated(99));
    check(runtime.point({8,second.points[4]}).matrix==translated(10,1,2),"Repeated source loads shared physical matrices across occurrences.");
    auto absent=send(events,"GETMATRIX",{0,0},"elsewhere",0);complete(events,absent);
    check(events.messenger().buffer().matrices().empty(),"Point routing lost case-sensitive exact names.");
    auto& messenger=events.messenger();messenger.select("caller",{},-1);messenger.buffer().add_int(123);
    const auto parent=messenger.buffer_owner();messenger.push_buffer();
    auto child=send(events,"GETMATRIX",{0,0},"",0);const auto child_owner=messenger.buffer_owner();complete(events,child);
    check(parent!=child_owner && parent->integers()==std::vector<std::int32_t>{123} && child_owner->matrices()==std::vector<AnimationMatrix>{translated(99)},
        "PLAYERPOINT append changed the caller's retained parent buffer or replaced the selected child owner.");
    const auto result=child_owner->matrices()[0];messenger.pop_buffer();messenger.buffer().add_matrix(result);
    check(messenger.buffer_owner()==parent && parent->matrices()==std::vector<AnimationMatrix>{translated(99)},
        "Placement caller could not restore the exact buffer owner and append the original response.");
    ActionEventRuntime conflict;PlayerPointRuntime owner(conflict),other(conflict);owner.register_source(initial);
    rejects([&]{other.register_source(initial);});
    check(other.sources().empty() && conflict.messenger().registration_count()==1,"Conflicting provider publication left partial registration state.");
}
void malformed() {
    Fixture fixture;const auto doc=fixture.document();
    const auto decode=[](IgbDocument document){return decode_player_points(std::make_shared<ActorSceneResource>(std::move(document),"malformed",7));};
    rejects([&]{decode_player_points(nullptr);});auto bad=doc;bad.info_list_reference.reset();rejects([&]{decode(bad);});
    bad=doc;bad.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{decode(bad);});
    for(const auto [ref,raw_slot]:std::array<std::pair<std::uint32_t,unsigned>,7>{{{fixture.info,4},{fixture.info,6},
        {fixture.ram,2},{fixture.groups[0],7},{fixture.points[0],8},{fixture.points[0],9},{fixture.points[0],11}}}) {
        const auto slot=static_cast<std::uint16_t>(raw_slot);
        bad=doc;auto* field=const_cast<IgbFieldValue*>(bad.field(object_at(bad,ref),slot));++field->size;rejects([&]{decode(bad);});
        bad=doc;field=const_cast<IgbFieldValue*>(bad.field(object_at(bad,ref),slot));field->type_index=0;rejects([&]{decode(bad);});
        bad=doc;object_at(bad,ref).fields.push_back(*bad.field(object_at(bad,ref),slot));rejects([&]{decode(bad);});
    }
    Fixture dynamic;dynamic.set(dynamic.points[0],9,1);rejects([&]{dynamic.program();});
    Fixture nan;nan.set(nan.points[0],8,0x7fc00000);rejects([&]{nan.program();});
    Fixture capacity;capacity.scalar(capacity.group_list,3,0x7fffffff);rejects([&]{capacity.program();});
    Fixture short_storage;short_storage.scalar(short_storage.group_list,3,3);rejects([&]{short_storage.program();});
    Fixture null_ram;null_ram.contents(null_ram.ram_list,{null_ref});rejects([&]{null_ram.program();});
    bad=doc;object_at(bad,fixture.points[0]).type_index=actor_skin_fixture::object;rejects([&]{decode(bad);});
    bad=doc;auto derived=bad.metadata.object_types.at(bad.object(fixture.info).type_index);
    derived.parent_index=static_cast<std::int32_t>(bad.object(fixture.info).type_index);derived.name="DerivedPointInfo";
    object_at(bad,fixture.info).type_index=static_cast<std::uint32_t>(bad.metadata.object_types.size());bad.metadata.object_types.push_back(derived);
    check(decode(bad).infos().empty(),"Exact info type acceptance incorrectly broadened to inheritance.");
}
void original(const char* root) {
    const auto resources=MountedResources::open(root);auto document=read_igb_document(resources.archives().at(0).read(179));
    auto resource=std::make_shared<ActorSceneResource>(std::move(document),"dng101b1.igb#[0.179]",179);
    const auto program=decode_player_points(resource);check(program.infos().size()==1,"Original PLAYERPOINT export count changed.");
    const auto& info=program.infos()[0];check(info.reference==115 && info.groups.size()==2,"Original source-qualified point graph changed.");
    ActionEventRuntime events;PlayerPointRuntime runtime(events);runtime.register_source(program);
    check(events.messenger().registration_count()==info.rams.size(),"Original point RAM registrations differ.");
    for(std::int32_t group=0;group<2;++group)for(std::int32_t player=0;player<4;++player) {
        command(events,"SETGENERATEPOS",{group});command(events,"GETMATRIX",{77,player});
        const auto expected=decode_igb_player_placement(resource->document(),115,static_cast<std::uint32_t>(group),static_cast<std::uint32_t>(player));
        check(events.messenger().buffer().matrices()[0]==expected.matrix,"Original matrix query differs from the authored point transform.");
        const auto& point=runtime.point({179,expected.transform_reference});
        check(point.definition->node==resource->node(expected.transform_reference),"Original point lost actual scene-node identity.");
    }
    command(events,"SETGENERATEPOS",{999});command(events,"GETMATRIX",{0,3});
    check(events.messenger().buffer().matrices()[0]==decode_igb_player_placement(resource->document(),115,0,3).matrix && runtime.saved_group()==999,
        "Original out-of-range group fallback changed saved manager state.");
}
}
int main(int argc,char** argv) {
    try {
        routing_and_matrices();nulls_and_failures();aliases_and_lifetime();switch_service();sources_and_buffer_owners();malformed();
        const auto synthetic=checks;if(argc>1)original(argv[1]);
        std::cout<<"PLAYERPOINT tests passed: "<<checks<<" checks ("<<synthetic<<" synthetic).\n";return 0;
    }catch(const std::exception& error){std::cerr<<"PLAYERPOINT test failure after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
