#include "dw4/meter_control.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/resources.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace dw4::test;
unsigned checks{};
void check(bool value,const char* text) {++checks;if(!value)throw std::runtime_error(text);}
template<class F>void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error("Invalid meter operation was accepted.");
}
struct Fixture:ScriptFixture {
    std::array<std::uint32_t,2> infos{},ram_lists{},one_lists{};
    std::vector<std::uint32_t> rams,ram_storage,ones,one_storage,nodes,node_storage,children,times,time_storage;
    Fixture() {
        for(unsigned i=0;i<4;++i) {
            rams.push_back(add(object));ram_storage.push_back(memory(words({77,255,4,5,6,7,null_ref})));
            nodes.push_back(add(node));children.push_back(list(object_list,{}));times.push_back(list(object_list,{}));
            node_storage.push_back(memory(words({2,children.back(),1,times.back(),0,0,0})));
            ones.push_back(add(named_object));text(ones.back(),2,"meter"+std::to_string(i));
            auto data=words({nodes.back()});for(const auto value:{100.0f,0.0f,1.0f,0.0f,0.0f})push32(data,std::bit_cast<std::uint32_t>(value));
            append(data,words({16000000,0,1980000000,0,0,0,1,0,null_ref,null_ref}));one_storage.push_back(memory(std::move(data)));
        }
        for(unsigned i=0;i<2;++i) {
            infos[i]=add(scene_info);text(infos[i],2,i==0 ? "hpmt" : "mpmt");scalar(infos[i],4,1);
            ram_lists[i]=list(object_list,{rams[i*2],rams[i*2+1]});one_lists[i]=list(named_list,{ones[i*2],ones[i*2+1]});
            scalar(infos[i],7,one_lists[i]);time_storage.push_back(memory(words({ram_lists[i]})));
        }
        exported_info_list=list(info_list,{infos[0],infos[1]});
    }
    void one_word(unsigned index,std::size_t offset,std::uint32_t value) {word(records[one_storage.at(index)].data,offset,value);}
    void node_word(unsigned index,std::size_t offset,std::uint32_t value) {word(records[node_storage.at(index)].data,offset,value);}
    IgbDocument document() const {
        auto doc=ScriptFixture::document();
        const auto type=[&](std::string name,std::int32_t parent) {
            IgbObjectType value;value.name=std::move(name);value.parent_index=parent;value.slots=17;
            doc.metadata.object_types.push_back(std::move(value));return static_cast<std::uint32_t>(doc.metadata.object_types.size()-1);
        };
        const auto float_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());doc.metadata.field_types.push_back({"igFloatMetaField",1,0});
        const auto long_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());doc.metadata.field_types.push_back({"igLongMetaField",1,0});
        const auto base_info=type("beBaseInfo",info_base),meter_info=type("beMeterCtrlInfo",static_cast<std::int32_t>(base_info));
        const auto ram_type=type("beBaseInfoRam",object),ram_list=type("beBaseInfoRamList",object_list);
        const auto group=type("igGroup",node),meter_node=type("beMeterCtrlNode",static_cast<std::int32_t>(group));
        const auto node_list=type("igNodeList",object_list),long_list=type("igLongList",object_list),one_type=type("beMeterCtrlOneData",named_object);
        const auto obj=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        const auto at=[&](std::uint32_t ref) {return doc.memories.at(doc.references.at(ref).value_index).offset;};
        for(unsigned i=0;i<4;++i) {
            auto& ram=obj(rams[i]);ram.type_index=ram_type;auto p=at(ram_storage[i]);
            ram.fields={{2,1,p,4},{5,4,p+4,1},{6,1,p+8,4},{7,1,p+12,4},{8,1,p+16,4},{9,1,p+20,4},{10,2,p+24,4}};
            auto& n=obj(nodes[i]);n.type_index=meter_node;p=at(node_storage[i]);
            n.fields.insert(n.fields.end(),{{5,1,p,4},{7,2,p+4,4},{8,4,p+8,1},{9,2,p+12,4},{10,8,p+16,12}});
            obj(children[i]).type_index=node_list;obj(times[i]).type_index=long_list;
            auto& one=obj(ones[i]);one.type_index=one_type;p=at(one_storage[i]);
            one.fields.insert(one.fields.end(),{{3,2,p,4},{4,float_type,p+4,4},{5,float_type,p+8,4},{6,float_type,p+12,4},
                {7,float_type,p+16,4},{8,float_type,p+20,4},{9,long_type,p+24,8},{10,long_type,p+32,8},
                {11,1,p+40,4},{12,4,p+44,1},{13,4,p+48,1},{14,4,p+52,1},{15,2,p+56,4},{16,2,p+60,4}});
        }
        for(unsigned i=0;i<2;++i) {
            auto& info=obj(infos[i]);info.type_index=meter_info;info.fields.push_back({6,2,at(time_storage[i]),4});obj(ram_lists[i]).type_index=ram_list;
        }
        return doc;
    }
    MeterProgram program(std::uint32_t occurrence=3) const {
        return decode_meter_controls(std::make_shared<ActorSceneResource>(document(),"synthetic/meter.igb",occurrence));
    }
};
struct Runtime {
    Fixture fixture;ActionEventRuntime events;MeterRuntime meter{events};
    explicit Runtime():Runtime(Fixture{}) {}
    explicit Runtime(Fixture f):fixture(std::move(f)){meter.register_source(fixture.program());}
    ControllerObjectId id(unsigned index=0) const {return {3,fixture.rams[index]};}
    MessengerResult command(std::string name,std::optional<float> value={},unsigned index=0) {
        auto& messenger=events.messenger();messenger.select("METERCTRL",index<2 ? "hpmt" : "mpmt",static_cast<std::int32_t>(index%2));
        if(value)messenger.buffer().add_float(*value);auto call=messenger.begin_send(std::move(name));return events.resume(call);
    }
};
void commands_and_update() {
    Runtime r;const auto id=r.id();
    check(r.meter.ram(id).index==0 && r.meter.ram(id).active==255 && r.meter.ram(id).words==std::array<std::int32_t,4>{},"Meter registration lost raw active or original RAM reset.");
    check(r.command("SETPERCENT",0.25f).stop==MessengerStop::completed && r.meter.selected_one(id).target==1.0f,
        "Nonzero sub-unit percent did not clamp to one.");
    check(r.command("SETPERCENT",-25.0f).stop==MessengerStop::completed && r.meter.selected_one(id).target==1.0f,
        "Negative nonzero SETPERCENT did not retain original minimum-one behavior.");
    r.command("SETPERCENT",-0.0f);check(std::signbit(r.meter.selected_one(id).target),"SETPERCENT lost its exact signed-zero result.");
    r.command("SETPERCENT",50.0f);check(r.meter.selected_one(id).current==0 && r.meter.selected_one(id).dirty==0,
        "SETPERCENT directly changed the smoothed current value or dirty flag.");
    r.command("GETPERCENT");check(r.events.messenger().buffer().floats()==std::vector<float>{0.5f},"GETPERCENT was multiplied by100.");
    r.meter.update(id);const auto& one=r.meter.selected_one(id);
    check(one.digit_count==3 && one.time_scale==20000000.0f && one.step==100.0f/60.0f && one.current==one.step &&
        one.dirty==0 && r.meter.ram(id).words[0]==1,"Original first update initialization or smoothing differs.");
    check(one.node->times->size()==1 && one.node->times->front()==1033333332,"Meter node did not receive its original timed sample.");
    r.command("SETVALUE",20.0f);check(one.target==20 && one.current==20 && one.dirty==1,"SETVALUE did not assign target/current and dirty.");
    r.command("ADDVALUE",200.0f);check(one.target==100 && one.current==20,"ADDVALUE did not clamp only the target.");
    r.command("ADDPERCENT",-250.0f);check(one.target==0,"ADDPERCENT did not use maximum scaling and target clamp.");
    r.command("SETMAXVALUE",999.0f);check(one.maximum==999 && one.digit_count==3 && one.step==999.0f/60 && one.dirty==1,"SETMAXVALUE did not rerun the actual initialization helper.");
    r.command("SETVALUE",1000.0f);r.meter.update(id);check(one.target==1000 && one.current==999 && one.node->times->front()==3000000000LL,
        "Full meter endpoint incorrectly clamped target or omitted the3-second sample.");
    r.command("SETVALUE",-2.0f);r.meter.update(id);check(one.target==-2 && one.current==0 && one.node->times->front()==1000000000LL,
        "Empty meter endpoint differs.");
    r.command("GETVALUE");check(r.events.messenger().buffer().floats()==std::vector<float>{-2.0f},"GETVALUE returned current instead of target.");
    const auto previous=one.target;r.command("UNIMPLEMENTED");check(one.target==previous,"Unknown command mutated meter state.");
    r.meter.set_active(id,0);auto& messenger=r.events.messenger();messenger.select("METERCTRL","hpmt",0);
    messenger.buffer().set_enabled_only(1);auto inactive=messenger.begin_send("SETVALUE");
    check(r.events.resume(inactive).stop==MessengerStop::completed && one.target==previous,"Inactive enabled-only meter parsed missing float.");
}
void digit_and_integer_modes() {
    Fixture f;f.node_word(0,8,0);f.one_word(0,48,0);Runtime r(f);const auto id=r.id();
    r.command("SETVALUE",42.0f);r.meter.update(id);
    check(*r.meter.selected_one(id).node->times==std::vector<std::int64_t>{3000000000,5000000000,1000000000},
        "Digit-mode long list did not preserve units-first order and zero padding.");
    f.one_word(0,48,255);Runtime trimmed(f);trimmed.command("SETVALUE",42.0f);trimmed.meter.update(trimmed.id());
    check(*trimmed.meter.selected_one(trimmed.id()).node->times==std::vector<std::int64_t>{3000000000,5000000000},"Raw nonzero trim flag failed.");
    f.one_word(0,52,255);Runtime integer_mode(f);integer_mode.command("SETPERCENT",50.0f);integer_mode.meter.update(integer_mode.id());
    check(integer_mode.meter.selected_one(integer_mode.id()).current==1,"Integer smoothing did not truncate after the float step.");
}
void aliases_and_lifetime() {
    Fixture f;auto doc=f.document();doc.references[f.rams[2]].value_index=doc.references[f.rams[0]].value_index;
    doc.references[f.ones[3]].value_index=doc.references[f.ones[2]].value_index;
    ActionEventRuntime events;MeterRuntime meter(events);auto resource=std::make_shared<ActorSceneResource>(doc,"synthetic/aliases.igb",7);
    const auto program=decode_meter_controls(resource);meter.register_source(program);
    check(meter.ram({7,f.rams[0]}).info==ControllerObjectId{7,f.infos[1]} && &meter.ram({7,f.rams[0]})==&meter.ram({7,f.rams[2]}),
        "Physical RAM aliases did not retain final original parent registration.");
    check(&meter.one({7,f.ones[2]})==&meter.one({7,f.ones[3]}),"Physical ONE aliases did not share mutable controller fields.");
    auto call=events.messenger().begin_send_to("METERCTRL","hpmt",0,"GETVALUE");events.resume(call);
    check(&meter.selected_one({7,f.rams[0]})==&meter.one({7,f.ones[2]}),"Old routing key bypassed the RAM's final actual parent.");
    rejects([&]{meter.register_source(program);});
    resource.reset();check(meter.one({7,f.ones[2]}).node->definition->node->source()=="synthetic/aliases.igb","Meter node lost its loaded source lifetime.");
    meter.set_index({7,f.rams[0]},1);check(&meter.selected_one({7,f.rams[0]})==&meter.one({7,f.ones[3]}),"Meter selection ignored the live RAM index.");
    ActionEventRuntime retained;
    {MeterRuntime facade(retained);facade.register_source(f.program());}
    auto get=retained.messenger().begin_send_to("METERCTRL","hpmt",0,"GETVALUE");
    check(retained.resume(get).stop==MessengerStop::completed,"Registered meter owners depended on facade lifetime.");
}
void model_hp_binding() {
    Runtime r;auto hp=std::make_shared<GameDataInt>(GameDataInt{"HP",7}),max=std::make_shared<GameDataInt>(GameDataInt{"MHP",13});
    const auto provider=ControllerObjectId::allocate_native();unsigned calls{};bool finish=false;
    const std::array providers{PreparedControllerRegistration{provider,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},[&]{return MessengerManager{
            [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,0,{}};},
            [&](NativeMessenger& messenger,const MessengerDelivery&,std::string_view command) {
                check(command=="GETHP","Meter binding requested an invented model command.");++calls;
                if(!finish)return MessengerCallResult{false,0,"Prepared HP is pending."};
                messenger.buffer().add_int(hp->data);messenger.buffer().add_game_object(hp);messenger.buffer().add_game_object(max);
                return MessengerCallResult{true,1,{}};
            }};}},std::nullopt}};
    const std::array rows{MessengerRegistration{provider,"MODELCTRL","player",2}};
    r.events.register_controllers(providers,rows);
    auto& messenger=r.events.messenger();messenger.select("METERCTRL","hpmt",0);messenger.buffer().add_string("player");messenger.buffer().add_int(2);
    const auto parent=messenger.buffer_owner();auto call=messenger.begin_send("SETMODELHP");
    check(r.events.resume(call).stop!=MessengerStop::completed && messenger.buffer_depth()==1 && !r.meter.selected_one(r.id()).bound_value,
        "Pending GETHP completed meter binding or popped its child packet.");
    r.meter.set_index(r.id(),1);finish=true;
    check(r.events.resume(call).stop==MessengerStop::completed && calls==2 && messenger.buffer_owner()==parent && messenger.buffer_depth()==0,
        "Meter binding did not restore its exact original parent packet once.");
    const auto& bound=r.meter.one({3,r.fixture.ones[0]});
    check(bound.bound_value==hp && bound.bound_maximum==max && !r.meter.selected_one(r.id()).bound_value,
        "Paused meter binding reselected ONE or copied HP wrappers.");
    r.meter.set_index(r.id(),0);r.command("SETPERCENT");check(bound.target==0,"Bound SETPERCENT read a missing float instead of ignoring it.");
    r.meter.update(r.id());check(bound.target==53.0f,"Live bound meter did not use original wrapped integer percent.");
    hp->data=13;r.meter.update(r.id());check(bound.target==100 && bound.bound_value==hp,"Bound meter did not observe the retained live HP owner.");
}
void malformed_and_faults() {
    rejects([]{decode_meter_controls({});});Fixture f;auto doc=f.document();
    auto& info=doc.objects.at(doc.references[f.infos[0]].value_index);info.fields.push_back(info.fields.front());
    rejects([&]{decode_meter_controls(std::make_shared<ActorSceneResource>(doc,"invalid",1));});
    doc=f.document();auto& one=doc.objects.at(doc.references[f.ones[0]].value_index);
    const_cast<IgbFieldValue*>(doc.field(one,4))->size=8;
    rejects([&]{decode_meter_controls(std::make_shared<ActorSceneResource>(doc,"invalid",1));});
    Fixture null_node;null_node.one_word(0,0,null_ref);rejects([&]{null_node.program();});
    Fixture nonfinite;nonfinite.one_word(0,4,0x7f800000);rejects([&]{nonfinite.program();});
    Fixture bad_list;bad_list.scalar(bad_list.ram_lists[0],3,3);rejects([&]{bad_list.program();});
    Runtime missing;check(missing.command("SETVALUE").stop==MessengerStop::faulted && missing.meter.faulted(),"Missing scalar was not an explicit checked meter fault.");
    Runtime zero;check(zero.command("SETMAXVALUE",0.0f).stop==MessengerStop::faulted && zero.meter.selected_one(zero.id()).maximum==0 &&
        zero.meter.selected_one(zero.id()).digit_count==1,"Invalid maximum rolled back the original committed field prefix.");
    Runtime index;index.meter.set_index(index.id(),-1);check(index.command("UNKNOWN").stop==MessengerStop::faulted,
        "Unknown command skipped the original pre-dispatch ONE selection.");
    Runtime partial;auto hp=std::make_shared<GameDataInt>(GameDataInt{"HP",17});
    const auto provider=ControllerObjectId::allocate_native();
    const std::array providers{PreparedControllerRegistration{provider,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},[hp]{return MessengerManager{
            [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,0,{}};},
            [hp](NativeMessenger& messenger,const MessengerDelivery&,std::string_view command) {
                if(command!="GETHP")throw std::runtime_error("Unexpected partial-binding query.");
                messenger.buffer().add_int(hp->data);messenger.buffer().add_game_object(hp);
                return MessengerCallResult{true,1,{}};
            }};}},std::nullopt}};
    const std::array rows{MessengerRegistration{provider,"MODELCTRL","partial",0}};
    partial.events.register_controllers(providers,rows);auto& messenger=partial.events.messenger();
    messenger.select("METERCTRL","hpmt",0);messenger.buffer().add_string("partial");messenger.buffer().add_int(0);
    const auto parent=messenger.buffer_owner();auto binding=messenger.begin_send("SETMODELHP");
    check(partial.events.resume(binding).stop==MessengerStop::faulted && partial.meter.faulted(),
        "Missing second HP binding object did not expose an explicit meter failure.");
    check(partial.meter.selected_one(partial.id()).bound_value==hp && !partial.meter.selected_one(partial.id()).bound_maximum &&
        messenger.buffer_depth()==1 && messenger.buffer_owner()!=parent,
        "Failed second binding access rolled back the committed HP owner or popped the unfinished packet.");
}
void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);auto resource=std::make_shared<ActorSceneResource>(
        read_igb_document(mounted.archives().at(0).read(179)),"ndmw.afs/179",12);
    const auto program=decode_meter_controls(resource);check(program.infos().size()==2,"Original HUD does not expose both meter infos.");
    ActionEventRuntime events;MeterRuntime runtime(events);runtime.register_source(program);
    const std::array<std::uint32_t,2> refs{31070,31212};
    for(const auto& info:program.infos()) {
        check((info.reference==refs[0] || info.reference==refs[1]) && info.rams.size()==4 && info.ones.size()==4,"Original meter resource shape differs.");
        for(std::uint32_t i=0;i<4;++i) {
            const ControllerObjectId id{12,info.rams[i].reference};const auto& one=runtime.selected_one(id);
            check(one.maximum==100 && one.current==0 && one.node->definition->meter_mode==1 && one.node->times->empty(),"Original meter authored startup fields differ.");
            auto& messenger=events.messenger();messenger.select("METERCTRL",info.name,static_cast<std::int32_t>(i));messenger.buffer().add_float(100);
            auto call=messenger.begin_send("SETPERCENT");check(events.resume(call).stop==MessengerStop::completed && one.target==100,"Original meter did not consume the player-start scalar.");
            runtime.update(id);check(one.current==(info.name=="hpmt" ? 1.0f : 100.0f/60.0f) && one.node->times->size()==1,
                "Original meter first update did not mutate its actual integer/fractional sample list.");
        }
    }
}
}
int main(int argc,char** argv) {
    try {commands_and_update();digit_and_integer_modes();aliases_and_lifetime();model_hp_binding();malformed_and_faults();const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Meter control checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Meter control after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
