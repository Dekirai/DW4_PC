#include "actor_skin_fixture.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/number_control.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace actor_skin_fixture;
unsigned checks{};
void check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
template<class F>void rejects(F operation){++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error("Invalid NUMBERCTRL operation accepted.");}
bool equal(Vec2 a,Vec2 b){return std::bit_cast<std::uint32_t>(a.u)==std::bit_cast<std::uint32_t>(b.u) &&
    std::bit_cast<std::uint32_t>(a.v)==std::bit_cast<std::uint32_t>(b.v);}
IgbObject& object_at(IgbDocument& doc,std::uint32_t ref){return doc.objects.at(doc.references.at(ref).value_index);}

// All payload bytes are generated. Extra metadata fields refer to owned memory
// records so malformed tests can change both the schema and serialized values.
struct Fixture : Graph {
    struct Extra {std::uint32_t ref;std::string name,parent;std::map<std::uint16_t,std::pair<std::uint16_t,std::uint32_t>> fields;};
    std::vector<Extra> extra;
    std::uint32_t info,ram_list,exports,geometry,attribute,vertex,uv_memory,digit_list;
    std::vector<std::uint32_t> rams,digits,mappings;
    std::uint32_t custom(const char* name,const char* parent="igObject") {
        const auto ref=add(actor_skin_fixture::object);extra.push_back({ref,name,parent,{}});return ref;
    }
    void value(std::uint32_t ref,std::uint16_t slot,std::uint16_t type,Bytes bytes) {
        const auto mem=memory(std::move(bytes));
        for(auto& object:extra)if(object.ref==ref){object.fields[slot]={type,mem};return;}
        throw std::runtime_error("Fixture custom owner absent.");
    }
    void value(std::uint32_t ref,std::uint16_t slot,std::uint16_t type,std::uint32_t word_value) {
        value(ref,slot,type,words({word_value}));
    }
    void set(std::uint32_t ref,std::uint16_t slot,std::uint32_t word_value) {
        for(auto& object:extra)if(object.ref==ref){word(records[object.fields.at(slot).second].data,0,word_value);return;}
        throw std::runtime_error("Fixture set owner absent.");
    }
    std::uint32_t typed_list(const char* name,const std::vector<std::uint32_t>& refs) {
        const auto ref=list(object_list,refs);extra.push_back({ref,name,"igObjectList",{}});return ref;
    }
    std::uint32_t add_ram(std::uint8_t active=1) {
        const auto ram=custom("beNumberCtrlInfoRam");rams.push_back(ram);
        value(ram,2,1,123);value(ram,5,4,Bytes{active});
        for(std::uint16_t slot=6;slot<=9;++slot)value(ram,slot,1,77);
        value(ram,10,2,null_ref);value(ram,11,2,geometry);value(ram,12,8,std::bit_cast<std::uint32_t>(0.09375f));
        value(ram,13,1,999);value(ram,14,1,1);value(ram,15,2,digit_list);value(ram,16,2,null_ref);
        return ram;
    }
    Fixture() {
        geometry=add(geometry_node);attribute=add(geometry_attr);vertex=add(vertex_array);
        scalar(geometry,8,list(attr_list,{attribute}));scalar(attribute,4,vertex);
        scalar(attribute,6,3);scalar(attribute,7,6);scalar(vertex,3,18);
        Bytes table(80,255);word(table,0,memory(Bytes(18*12)));
        Bytes uv;for(unsigned i=0;i<18;++i){push32(uv,std::bit_cast<std::uint32_t>(0.875f));push32(uv,std::bit_cast<std::uint32_t>(0.625f));}
        uv_memory=memory(std::move(uv));word(table,44,uv_memory);scalar(vertex,2,memory(std::move(table)));
        for(unsigned n=0;n<3;++n) {
            std::vector<std::uint32_t> refs;
            const std::array<unsigned,6> corners{0,1,3,0,3,2};
            for(unsigned i=0;i<6;++i) {
                const auto mapping=custom("beNumVerInsideData");mappings.push_back(mapping);refs.push_back(mapping);
                value(mapping,2,1,n*6+i);value(mapping,3,1,corners[i]);
            }
            const auto digit=custom("beNumVerData");digits.push_back(digit);
            value(digit,2,1,0xffffffffu);value(digit,3,2,typed_list("beNumVerInsideDataList",refs));
        }
        digit_list=typed_list("beNumVerDataList",digits);add_ram();ram_list=typed_list("beBaseInfoRamList",rams);
        info=custom("beNumberCtrlInfo","igInfo");auto name=words({5});append(name,Bytes{'l','v','n','m',0});
        value(info,2,0,std::move(name));value(info,4,4,Bytes{255});value(info,5,2,null_ref);value(info,6,2,ram_list);
        exports=typed_list("igInfoList",{info});
    }
    IgbDocument document() const {
        auto doc=Graph::document();doc.metadata.field_types.push_back({"igFloatMetaField",1,0});
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
    NumberControlProgram program(std::uint32_t occurrence=7) const {
        return decode_number_controls(document(),"synthetic/number.igb",occurrence);
    }
};
MessengerDispatch send(ActionEventRuntime& events,std::string command,std::optional<std::int32_t> value={},
    std::optional<std::string> name="lvnm",std::int32_t index=0) {
    auto& messenger=events.messenger();messenger.select("NUMBERCTRL",std::move(name),index);
    if(value)messenger.buffer().add_int(*value);return messenger.begin_send(std::move(command));
}
void complete(ActionEventRuntime& events,MessengerDispatch& dispatch) {
    const auto result=events.resume(dispatch);check(result.stop==MessengerStop::completed,"NUMBERCTRL command did not complete.");
}
void command(ActionEventRuntime& events,std::string name,std::optional<std::int32_t> value={}) {
    auto dispatch=send(events,std::move(name),value);complete(events,dispatch);
}

void values_and_geometry() {
    Fixture fixture;const auto program=fixture.program();ActionEventRuntime events;NumberControlRuntime runtime(events);
    runtime.register_source(program);const auto& ram=runtime.ram({7,fixture.rams[0]});
    check(ram.index==0 && ram.words==std::array<std::int32_t,4>{} && ram.active==1 && !ram.work,
        "NUMBERCTRL generic initialization lost original ordinal/reset/null work.");
    command(events,"GETVALUE");check(events.messenger().buffer().integers().empty(),"Null work GETVALUE invented an integer.");
    command(events,"SETVALUE");check(ram.work && ram.work->id.is_native() && ram.uv->revision==0,
        "Empty SETVALUE did not allocate work before argument count, or changed geometry.");
    const auto work=ram.work;command(events,"SETVALUE",125);
    check(ram.work==work && work->current==125 && work->target==125 && work->step==0 && ram.uv->revision==1,
        "SETVALUE did not mutate the retained current/target/step owner.");
    check(ram.digits[0]->display==5 && ram.digits[1]->display==2 && ram.digits[2]->display==1,
        "Digit order is not least-significant first.");
    check(equal(ram.uv->texcoords[0],{0.46875f,0}) && equal(ram.uv->texcoords[1],{0.5625f,0}) &&
        equal(ram.uv->texcoords[2],{0.5625f,1}) && equal(ram.uv->texcoords[5],{0.46875f,1}),
        "The six original atlas-corner writes are wrong.");
    auto get=send(events,"GETVALUE",91);events.messenger().buffer().add_float(0.5f);complete(events,get);
    check(events.messenger().buffer().integers()==std::vector<std::int32_t>{91,125} &&
        events.messenger().buffer().floats()==std::vector<float>{0.5f},"GETVALUE did not append to the existing shared buffer.");
    complete(events,get);check(events.messenger().buffer().integers().size()==2,"Completed GETVALUE replayed its append.");
    command(events,"SETVALUE",-12);check(work->target==0 && ram.digits[0]->display==0 && ram.digits[1]->display==-1 &&
        equal(ram.uv->texcoords[6],{0,0}),"Negative clamp or blank leading-digit UV changed.");
    command(events,"SETVALUE",5000);check(work->target==999,"Upper bound clamp failed.");
    const auto revision=ram.uv->revision;
    for(const auto* name:{"SETVALUEDIRECT","setvalue","UNKNOWN",""})command(events,name,31);
    check(work->target==999 && ram.uv->revision==revision,"An originally declined command mutated NUMBERCTRL.");
    runtime.set_active(ram.id,0);command(events,"SETVALUE",12);
    check(work->target==12,"Direct SETVALUE incorrectly checks the RAM active byte.");
    auto filtered=send(events,"SETVALUE",30);events.messenger().buffer().set_enabled_only(1);complete(events,filtered);
    check(work->target==12,"Messenger enabled-only filtering did not use the live raw active byte.");
    const auto immutable=program.infos()[0].rams[0].uv;
    check(equal(immutable->texcoords[0],{0.875f,0.625f}),"Runtime mutated the immutable authored UV definition.");
}

void addition_and_update() {
    Fixture fixture;fixture.set(fixture.rams[0],13,0x7fffffff);ActionEventRuntime events;NumberControlRuntime runtime(events);
    runtime.register_source(fixture.program());const auto& ram=runtime.ram({7,fixture.rams[0]});
    command(events,"ADDVALUE");check(ram.work && ram.work->current==0 && ram.work->target==0,"Empty ADDVALUE allocation changed.");
    command(events,"ADDVALUE",1152);check(ram.work->target==1152 && ram.work->current==0 && ram.work->step==11 && ram.uv->revision==0,
        "ADDVALUE step must avoid ten and must not change displayed UVs immediately.");
    command(events,"GETVALUE");check(events.messenger().buffer().integers()==std::vector<std::int32_t>{1152},
        "GETVALUE returned display/current instead of real/target.");
    runtime.update(ram.id);check(ram.words[0]==1 && ram.work->current==11 && ram.work->target==1152 && ram.uv->revision==1,
        "First manager update did not initialize then advance the current display.");
    command(events,"SETVALUE",1200);command(events,"ADDVALUE",-1152);
    check(ram.work->step==-11 && ram.work->target==48,"Negative animation step or target changed.");
    for(unsigned i=0;i<110;++i)runtime.update(ram.id);
    check(ram.work->current==48 && ram.work->step==-11,"Manager update did not clamp an overshooting negative step.");
    const auto revision=ram.uv->revision;runtime.update(ram.id);check(ram.uv->revision==revision,"Equal current/target needlessly published UVs.");
    command(events,"SETVALUE",0x7ffffffe);command(events,"ADDVALUE",20);
    check(ram.work->target==0 && ram.work->step==-99,"ADDVALUE failed original signed wrapping and capped negative step.");
    command(events,"SETVALUE",0);command(events,"ADDVALUE",0x7fffffff);
    check(ram.work->step==99,"Large positive step was not capped at 99.");
    command(events,"SETVALUE",8);command(events,"ADDVALUE",2);runtime.update(ram.id);runtime.update(ram.id);
    check(ram.work->current==10 && ram.work->target==10,"Small positive step did not finish exactly.");
}

void aliases_names_and_lifetime() {
    Fixture fixture;fixture.add_ram(255);fixture.contents(fixture.ram_list,{fixture.rams[0],fixture.rams[1],fixture.rams[0]});
    fixture.contents(fixture.digit_list,{fixture.digits[0],fixture.digits[0],fixture.digits[2]});
    fixture.contents(fixture.exports,{fixture.info,fixture.info});
    const auto p=fixture.program();check(p.infos().size()==2 && p.infos()[0].rams.size()==3,"Source occurrence order was deduplicated.");
    ActionEventRuntime events;std::shared_ptr<const NumberWorkState> work;std::shared_ptr<const NumberUvState> uv;
    {
        NumberControlRuntime runtime(events);runtime.register_source(p);
        const auto& first=runtime.ram({7,fixture.rams[0]});const auto& second=runtime.ram({7,fixture.rams[1]});
        check(first.index==2 && first.uv==second.uv && first.digits[0]==first.digits[1] && first.digits[0]==second.digits[0],
            "Physical RAM, UV memory or repeated digit ownership was reconstructed independently.");
        auto setter=send(events,"SETVALUE",123);complete(events,setter);
        check(first.digits[0]->display==2 && equal(first.uv->texcoords[0],{0.1875f,0}),
            "Aliased digit display must be read after all digit assignments.");
        check(first.uv->revision==2 && events.messenger().registration_count()==6,"Repeated registration rows were deduplicated.");
        work=first.work;uv=first.uv;rejects([&]{runtime.register_source(p);});
        check(events.messenger().registration_count()==6,"Rejected duplicate registration partially installed owners.");
        runtime.register_source(fixture.program(8));check(runtime.ram({8,fixture.rams[0]}).uv!=uv,
            "Separate loaded source occurrences shared mutable UV storage.");
        rejects([&]{runtime.ram({9,fixture.rams[0]});});
    }
    command(events,"SETVALUE",321);check(work->target==321 && uv->revision==4,"Published provider lost owners when its facade was destroyed.");
    Fixture null_name;null_name.value(null_name.info,2,0,words({0}));const auto null_program=null_name.program();
    check(!null_program.infos()[0].name,"Authored null name became empty text.");
    ActionEventRuntime null_events;NumberControlRuntime null_runtime(null_events);null_runtime.register_source(null_program);
    auto empty=send(null_events,"SETVALUE",5,"");complete(null_events,empty);
    check(null_runtime.ram({7,null_name.rams[0]}).work->target==5,"Null info name did not register using the original empty literal.");
}

void nulls_negative_maximum_and_mapping_kinds() {
    Fixture fixture;fixture.set(fixture.rams[0],13,0xffffffffu);fixture.set(fixture.rams[0],14,0);
    ActionEventRuntime events;NumberControlRuntime runtime(events);runtime.register_source(fixture.program());
    command(events,"SETVALUE",100);const auto& ram=runtime.ram({7,fixture.rams[0]});
    check(ram.work->target==-1 && ram.digits[0]->display==0 && ram.digits[2]->display==0,
        "Sequential lower/upper clamp or negative-value zero-fill changed.");
    Fixture unknown;unknown.set(unknown.mappings[0],3,9);ActionEventRuntime unknown_events;NumberControlRuntime unknown_runtime(unknown_events);
    unknown_runtime.register_source(unknown.program());command(unknown_events,"SETVALUE",123);
    check(equal(unknown_runtime.ram({7,unknown.rams[0]}).uv->texcoords[0],{0.875f,0.625f}),
        "Unknown original corner kind must preserve the previous UV.");
    Fixture missing;missing.set(missing.rams[0],11,null_ref);const auto p=missing.program();
    check(!p.infos()[0].rams[0].geometry_reference && !p.infos()[0].rams[0].uv,"Null geometry was invented.");
    ActionEventRuntime missing_events;NumberControlRuntime missing_runtime(missing_events);missing_runtime.register_source(p);
    command(missing_events,"SETVALUE");auto failed=send(missing_events,"SETVALUE",7);
    check(missing_events.resume(failed).stop==MessengerStop::faulted && missing_runtime.ram({7,missing.rams[0]}).work->target==0,
        "Missing original geometry must not complete a scalar-only SETVALUE.");
}

void physical_aliases_and_authored_work() {
    Fixture fixture;fixture.add_ram();fixture.contents(fixture.ram_list,fixture.rams);
    const auto work=fixture.custom("beNumberCtrlInfoWork");
    fixture.value(work,2,1,7);fixture.value(work,3,1,19);fixture.value(work,4,1,3);
    fixture.set(fixture.rams[0],16,work);fixture.set(fixture.rams[1],16,work);
    const auto program=fixture.program();ActionEventRuntime events;NumberControlRuntime runtime(events);runtime.register_source(program);
    const auto& first=runtime.ram({7,fixture.rams[0]});const auto& second=runtime.ram({7,fixture.rams[1]});
    check(first.work==second.work && first.work->id==ControllerObjectId{7,work} && first.work->current==7 &&
        first.work->target==19 && first.work->step==3,"Authored NUMBERCTRL work alias or initial signed words were lost.");
    runtime.update(first.id);check(second.work->current==10 && second.work->target==19,
        "Another RAM did not observe its actual shared work owner update.");
    command(events,"SETVALUE",81);check(second.work->current==81 && second.work->step==0,
        "SETVALUE replaced the shared authored work instead of updating it.");
    auto document=fixture.document();document.references[fixture.rams[1]].value_index=document.references[fixture.rams[0]].value_index;
    const auto aliased=decode_number_controls(document,"synthetic/number.igb",8);
    check(aliased.infos()[0].rams[1].reference==fixture.rams[1] &&
        aliased.infos()[0].rams[1].owner_reference==fixture.rams[0],"Raw RAM reference alias lost its distinct source occurrence.");
    runtime.register_source(aliased);check(&runtime.ram({8,fixture.rams[0]})==&runtime.ram({8,fixture.rams[1]}) &&
        runtime.ram({8,fixture.rams[0]}).index==1,"Physical RAM alias lost final registration ordinal or live owner identity.");
    Fixture negative;negative.set(negative.rams[0],13,0xffffffffu);negative.set(negative.mappings[0],3,77);
    ActionEventRuntime negative_events;NumberControlRuntime negative_runtime(negative_events);negative_runtime.register_source(negative.program());
    command(negative_events,"SETVALUE",3);const auto& state=negative_runtime.ram({7,negative.rams[0]});
    check(state.digits[0]->display==-1 && equal(state.uv->texcoords[0],{0,0}),
        "Negative displayed digit must blank even an unknown corner kind.");
    Fixture null_digits;null_digits.set(null_digits.rams[0],15,null_ref);
    check(!null_digits.program().infos()[0].rams[0].digits_source,"Authored null digit list became an invented empty owner.");
    Fixture empty_name;empty_name.value(empty_name.info,2,0,words({4,0}));
    check(empty_name.program().infos()[0].name=="","Authored empty name was collapsed into null.");
    Fixture repeated_vertex;repeated_vertex.set(repeated_vertex.mappings[1],2,0);
    ActionEventRuntime repeated_events;NumberControlRuntime repeated_runtime(repeated_events);repeated_runtime.register_source(repeated_vertex.program());
    command(repeated_events,"SETVALUE",125);const auto& coordinates=repeated_runtime.ram({7,repeated_vertex.rams[0]}).uv->texcoords;
    check(equal(coordinates[0],{0.5625f,0}) && equal(coordinates[1],{0.875f,0.625f}),
        "Repeated mapped vertex index did not preserve the original last write and untouched vertex.");
    Fixture empty_digits;empty_digits.contents(empty_digits.digit_list,{});
    ActionEventRuntime empty_events;NumberControlRuntime empty_runtime(empty_events);empty_runtime.register_source(empty_digits.program());
    command(empty_events,"SETVALUE",2);const auto& empty_state=empty_runtime.ram({7,empty_digits.rams[0]});
    check(empty_state.digits.empty() && empty_state.uv->revision==1 && empty_state.work->current==2,
        "Non-null empty digit list skipped the original writable-stream publication.");
}

void malformed() {
    Fixture fixture;const auto doc=fixture.document();
    auto decode=[](const IgbDocument& value){return decode_number_controls(value,"synthetic/number.igb",7);};
    auto bad=doc;bad.info_list_reference.reset();rejects([&]{decode(bad);});
    bad=doc;bad.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{decode(bad);});
    rejects([&]{decode_number_controls(doc,"",7);});rejects([&]{decode_number_controls(doc,"source",0xffffffffu);});
    for(const auto [ref,slot]:std::array<std::pair<std::uint32_t,unsigned>,7>{{
        {fixture.info,4},{fixture.info,6},{fixture.rams[0],12},{fixture.rams[0],13},
        {fixture.digits[0],2},{fixture.mappings[0],2},{fixture.vertex,3}}}) {
        const auto field_slot=static_cast<std::uint16_t>(slot);
        bad=doc;auto* field=const_cast<IgbFieldValue*>(bad.field(object_at(bad,ref),field_slot));++field->size;
        rejects([&]{decode(bad);});
        bad=doc;field=const_cast<IgbFieldValue*>(bad.field(object_at(bad,ref),field_slot));field->type_index=0;
        rejects([&]{decode(bad);});
        bad=doc;object_at(bad,ref).fields.push_back(*bad.field(object_at(bad,ref),field_slot));rejects([&]{decode(bad);});
    }
    Fixture short_mapping;short_mapping.value(short_mapping.digits[0],3,2,short_mapping.typed_list("beNumVerInsideDataList",{short_mapping.mappings[0]}));
    rejects([&]{short_mapping.program();});
    Fixture outside;outside.set(outside.mappings[0],2,18);rejects([&]{outside.program();});
    Fixture negative;negative.set(negative.mappings[0],2,0xffffffffu);rejects([&]{negative.program();});
    Fixture nan;nan.set(nan.rams[0],12,0x7fc00000);rejects([&]{nan.program();});
    Fixture capacity;capacity.scalar(capacity.digit_list,3,257);rejects([&]{capacity.program();});
    Fixture null_digit;null_digit.contents(null_digit.digit_list,{null_ref});rejects([&]{null_digit.program();});
    bad=doc;object_at(bad,fixture.rams[0]).type_index=actor_skin_fixture::object;rejects([&]{decode(bad);});
    bad=doc;auto derived=bad.metadata.object_types.at(bad.object(fixture.info).type_index);
    derived.parent_index=static_cast<std::int32_t>(bad.object(fixture.info).type_index);derived.name="DerivedNumberInfo";
    object_at(bad,fixture.info).type_index=static_cast<std::uint32_t>(bad.metadata.object_types.size());bad.metadata.object_types.push_back(derived);
    check(decode(bad).infos().empty(),"Original exact exported info type match changed to inheritance.");
}

void original(const char* root) {
    const auto resources=MountedResources::open(root);
    const auto document=read_igb_document(resources.archives().at(0).read(179));
    const auto program=decode_number_controls(document,"dng101b1.igb#[0.179]",179);
    check(program.infos().size()==7,"Original NUMBERCTRL export count changed.");
    ActionEventRuntime events;NumberControlRuntime runtime(events);runtime.register_source(program);
    check(events.messenger().registration_count()==43,"Original NUMBERCTRL RAM count changed.");
    unsigned hud_slots{};
    for(const auto& info:program.infos())for(std::size_t ordinal=0;ordinal<info.rams.size();++ordinal) {
        const auto& authored=info.rams[ordinal];const auto& ram=runtime.ram({179,authored.owner_reference});
        auto setter=send(events,"SETVALUE",125,info.name,static_cast<std::int32_t>(ordinal));complete(events,setter);
        check(ram.work && ram.work->target==125 && ram.uv->revision>0,"Original SETVALUE did not publish work and UV storage.");
        check(ram.digits.size()>=3 && ram.digits[0]->display==5 && ram.digits[1]->display==2 && ram.digits[2]->display==1,
            "Original digit list does not represent 125 in source order.");
        for(const auto& digit:ram.digits)for(std::size_t i=0;i<6;++i) {
            const auto& mapping=digit->mappings[i];const auto uv=ram.uv->texcoords.at(static_cast<std::size_t>(mapping.index));
            check(std::isfinite(uv.u) && std::isfinite(uv.v),"Original number update produced non-finite UV.");
        }
        if(info.name=="hpnm" || info.name=="lvnm" || info.name=="mpnm") {
            ++hud_slots;check(info.rams.size()==4 && ram.index==static_cast<std::int32_t>(ordinal),"HUD lost its four original player slots.");
            auto add=send(events,"ADDVALUE",100,info.name,static_cast<std::int32_t>(ordinal));complete(events,add);
            check(ram.work->current==125 && ram.work->target==225,"HUD ADDVALUE prematurely changed the display.");
            runtime.update(ram.id);check(ram.work->current==126,"HUD manager update did not advance its shared display owner.");
        }
    }
    check(hud_slots==12,"Original hpnm/lvnm/mpnm routing did not cover four players each.");
}
}
int main(int argc,char** argv) {
    try {
        values_and_geometry();addition_and_update();aliases_names_and_lifetime();nulls_negative_maximum_and_mapping_kinds();
        physical_aliases_and_authored_work();malformed();
        const auto synthetic_checks=checks;if(argc>1)original(argv[1]);
        std::cout<<"NUMBERCTRL tests passed: "<<checks<<" checks ("<<synthetic_checks<<" synthetic).\n";return 0;
    }catch(const std::exception& error){std::cerr<<"NUMBERCTRL test failure after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
