#include "dw4/model_collision.hpp"
#include "script_fixtures.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
namespace fixture=dw4::test;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation,const char* message) {
    ++checks;try {operation();}catch(const MessengerError&) {return;}throw std::runtime_error(message);
}
AnimationMatrix matrix(float x=0,float y=0,float z=0) {return {1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};}
ModelControlState model() {ModelMotionPose pose(matrix());set_player_turn_rates(pose);return ModelControlState(pose);}
constexpr CollisionBox local{{-1,0,-3},{3,6,5}},second{{-4,-2,-6},{2,4,0}};

// Minimal serialized MODELCTRL/HIT graph with duplicate box occurrences and
// one actual RAM. Every byte belongs to this generated fixture.
struct Fixture : fixture::ScriptFixture {
    struct Box {std::uint32_t reference,storage;};
    std::uint32_t model,ram,ram_storage,hit,hit_storage,data_list,ram_list,box_list;
    std::optional<std::uint32_t> alias;
    std::vector<Box> boxes;
    explicit Fixture(const std::string& name,bool aliased=false) {
        for(const auto box:{local,second}) {
            fixture::Bytes bytes;for(const auto point:{box.minimum,box.maximum})for(const auto value:point)
                fixture::push32(bytes,std::bit_cast<std::uint32_t>(value));
            boxes.push_back({add(fixture::object),memory(std::move(bytes))});
        }
        box_list=list(fixture::object_list,{boxes[0].reference,boxes[1].reference,boxes[0].reference});
        hit=add(fixture::named_object);text(hit,2,"HIT");hit_storage=memory(fixture::words({0,0x100,box_list,0}));
        data_list=list(fixture::object_list,{hit});ram=add(fixture::object);
        ram_storage=memory(fixture::words({0,0,0,0,0,0,fixture::null_ref}));
        if(aliased)alias=add(fixture::object);
        ram_list=list(fixture::object_list,alias ? std::vector<std::uint32_t>{ram,*alias} : std::vector<std::uint32_t>{ram});
        model=add(fixture::scene_info);text(model,2,name);scalar(model,4,1);scalar(model,5,data_list);scalar(model,7,ram_list);
        exported_info_list=list(fixture::info_list,{model});
    }
    ModelHitBodyDefinition definition() const {
        auto doc=fixture::ScriptFixture::document();
        const auto type=[&](std::string name,std::int32_t parent) {
            const auto index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back({std::move(name),1,0,parent,16,{}});return index;
        };
        const auto data_type=type("beBaseInfoData",fixture::named_object);
        const auto hit_type=type("beModelCtrlInfoDataHit",static_cast<std::int32_t>(data_type));
        const auto model_type=type("beModelCtrlInfo",fixture::info_base),ram_type=type("beBaseInfoRam",fixture::object);
        const auto box_type=type("igAABox",fixture::object),data_list_type=type("beBaseInfoDataList",fixture::object_list);
        const auto ram_list_type=type("beBaseInfoRamList",fixture::object_list),box_list_type=type("igAABoxList",fixture::object_list);
        const auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        const auto offset=[&](std::uint32_t ref){return doc.memories.at(doc.references.at(ref).value_index).offset;};
        object(model).type_index=model_type;for(auto& field:object(model).fields)if(field.slot==7)field.slot=6;
        object(data_list).type_index=data_list_type;object(ram_list).type_index=ram_list_type;object(box_list).type_index=box_list_type;
        auto& data=object(hit);data.type_index=hit_type;const auto at=offset(hit_storage);
        data.fields.insert(data.fields.end(),{{3,1,at,4},{4,4,at+4,1},{5,4,at+5,1},{6,2,at+8,4},{7,4,at+12,1}});
        for(const auto& box:boxes) {auto& value=object(box.reference);value.type_index=box_type;
            value.fields={{2,8,offset(box.storage),12},{3,8,offset(box.storage)+12,12}};}
        auto& value=object(ram);value.type_index=ram_type;const auto r=offset(ram_storage);
        value.fields={{2,1,r,4},{5,4,r+4,1},{6,1,r+8,4},{7,1,r+12,4},{8,1,r+16,4},{9,1,r+20,4},{10,2,r+24,4}};
        if(alias)doc.references[*alias]=doc.references[ram];
        return decode_model_hit_body(doc,model,"synthetic/body.igb",7);
    }
};
struct Target {
    Fixture fixture;
    ModelHitBodyDefinition definition;
    ModelControlState state=model();
    ControllerObjectId id;
    ModelCollisionRuntime runtime;
    NativeMessenger messenger;
    MessengerMessage message{"HITPLAYERBULLET",std::nullopt,-1,"CHKBULLET"};
    ModelCollisionWork work{1,true,"SQ_WAIT"};
    MessengerServices recipients;
    ModelCollisionServices services;
    explicit Target(const std::string& name="p_target",bool alias=false,std::uint32_t instance=0)
        :fixture(name,alias),definition(fixture.definition()),
        id{7,definition.ram_references().at(instance)},runtime(ModelHitBodyCache(definition,instance)) {
        messenger.select("PREPARED_BUFFER",std::nullopt,-1);
        services.system_time=[]()->std::optional<std::uint64_t>{return 1;};
    }
    MessengerDelivery delivery(std::uint64_t dispatch=123,std::size_t row=0) const {return {dispatch,row,id,message};}
    ModelCollisionResult handle() {return runtime.handle(messenger,delivery(),state,work,recipients,services);}
    void packet(CollisionBox box=local,std::optional<std::string> source="p_source",std::int32_t kind=60000) {
        messenger.select("HITPLAYERBULLET",std::nullopt,-1);auto& buffer=messenger.buffer();
        buffer.add_box(std::make_shared<const CollisionBox>(box));buffer.add_int(kind);buffer.add_int(2);
        buffer.add_matrix(matrix(10));buffer.add_string(std::move(source));buffer.set_enabled_only(1);
    }
};
void lazy_gates() {
    for(const auto name:{"enemy","P_target",""}) {
        Target target(name);target.state.invulnerability_frames()=1;
        check(target.handle().complete() && !target.runtime.body().initialized() && target.messenger.buffer().booleans().empty(),
            "Non-player invulnerable target must return before reading missing source or incoming arguments");
    }
    for(const auto source:{"enemy","P_source",""})for(const float frames:{1.f,-1.f}) {
        Target target;target.state.invulnerability_frames()=frames;target.messenger.buffer().add_string(source);
        check(target.handle().complete() && !target.runtime.body().initialized(),"Invulnerability exception requires exact lowercase p on both names");
    }
    Target missing_hp;missing_hp.state.invulnerability_frames()=2;missing_hp.work.hp_reference_present=false;
    missing_hp.messenger.buffer().add_string("p_source");
    check(missing_hp.handle().complete() && !missing_hp.runtime.body().initialized(),"Invulnerability exception requires the actual HP reference");
    for(const float frames:{0.f,-0.f})for(const std::uint8_t enabled:{std::uint8_t{0},std::uint8_t{2}}) {
        Target target;target.state.invulnerability_frames()=frames;target.work.control_enabled=enabled;
        target.work.current_sequence=enabled ? std::optional<std::string_view>{"SQ_ERAS"} : std::nullopt;
        check(target.handle().complete() && !target.runtime.body().initialized(),"Zero signs bypass invulnerability, then control/erase gate before arguments");
    }
    Target exception;exception.packet();exception.state.invulnerability_frames()=3;exception.work.control_enabled=2;
    exception.work.current_sequence="SQ_ERAS2";exception.services.system_time={};
    check(exception.handle().stop==ModelCollisionStop::system_time_required && exception.runtime.pending() && !exception.runtime.body().initialized(),
        "Player/HP exception or SQ_ERAS2 was incorrectly treated as an early return");
    Target null_source;null_source.state.invulnerability_frames()=1;null_source.messenger.buffer().add_string(std::nullopt);
    rejects([&]{null_source.handle();},"A read null source name must stop at the checked packet boundary");
    check(null_source.runtime.faulted(),"A fault after gate evaluation must prohibit uncertain replay");
    Target null_sequence;null_sequence.work.current_sequence={};
    rejects([&]{null_sequence.handle();},"Enabled work requires its real current sequence name");
    Target invalid;invalid.state.invulnerability_frames()=std::numeric_limits<float>::quiet_NaN();invalid.work.control_enabled=0;
    rejects([&]{invalid.handle();},"Nonfinite invulnerability must reject before the control gate");
}
void retained_time_gate_and_first_body() {
    Target target;target.packet();target.services.system_time=[]()->std::optional<std::uint64_t>{return std::nullopt;};
    const auto incoming=std::get<MessengerBox>(target.messenger.buffer().objects()[0]);
    check(target.handle().stop==ModelCollisionStop::system_time_required && target.runtime.owns_delivery(target.delivery()),
        "Missing timestamp failed to retain this delivery's completed gate prefix");
    target.state.invulnerability_frames()=99;target.work.control_enabled=0;target.work.current_sequence="SQ_ERAS";
    target.state.pose.matrix()=matrix(1);target.packet({{100,100,100},{101,101,101}},"p_later",70000);
    target.services.system_time=[]()->std::optional<std::uint64_t>{return 2;};unsigned scripts{};
    target.services.hit_script=[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
        ++scripts;check(request.kind==70000 && request.source_name=="p_later" && request.body_ordinal==0,
            "Arguments after timestamp must be live, while the earlier incoming object and gates stay retained");return std::uint8_t{0};
    };
    check(target.handle().complete() && scripts==1 && target.runtime.body().boxes()[0].minimum[0]==0 &&
        target.messenger.buffer().booleans()==std::vector<std::uint8_t>{1} && incoming->minimum[0]==-1,
        "Timestamp retry re-evaluated gates, replaced captured input geometry or ignored the live target matrix");
    for(const auto& row:std::vector<std::pair<CollisionBox,std::size_t>>{{{{3,6,5},{3,6,5}},0},{{{-3,0,-1},{-3,0,-1}},1}}) {
        Target current;current.packet(row.first);current.services.hit_script=[](const auto&)->std::optional<std::uint8_t>{return std::uint8_t{0};};
        check(current.handle().body_ordinal==row.second && current.messenger.buffer().booleans()==std::vector<std::uint8_t>{1},
            "Inclusive collision must retain the first overlapping body ordinal, including duplicates");
    }
    for(const std::uint64_t time:{0ull,1ull}) {
        Target miss;miss.messenger.buffer().add_box(std::make_shared<const CollisionBox>(CollisionBox{{100,100,100},{101,101,101}}));
        miss.services.system_time=[time]()->std::optional<std::uint64_t>{return time;};
        check(miss.handle().complete() && !miss.runtime.result().body_ordinal && miss.messenger.buffer().booleans().empty(),
            "No overlap must finish without reading later source arguments or appending a result boolean");
    }
    Target zero;const float largest=std::numeric_limits<float>::max();zero.packet({{-largest,-largest,-largest},{largest,largest,largest}});
    zero.services.system_time=[]()->std::optional<std::uint64_t>{return 0;};
    zero.services.hit_script=[](const auto&)->std::optional<std::uint8_t>{return std::uint8_t{0};};
    check(zero.handle().body_ordinal==0,"First time-zero factory bounds still require the actual six overlap comparisons");
}
void record_counts_and_parent_buffer() {
    for(const auto& records:std::vector<std::vector<std::int32_t>>{{},{17},{17,18},{-9},{-1}})for(const bool existing_false:{false,true}) {
        Target target;target.packet(local,"enemy_source",-12345);auto& parent=target.messenger.buffer();
        if(existing_false)parent.add_bool(0);
        parent.add_float(std::bit_cast<float>(0x80000001u));parent.add_int64(-1234567890123ll);
        parent.add_vector({1,-0.f,3});parent.add_object(ControllerObjectId{91,81});parent.add_object(std::nullopt);
        parent.add_string(std::nullopt);parent.add_string("extra");const auto before=parent;
        const ControllerObjectId source{90,7};target.messenger.register_recipient(source,"MODELCTRL","enemy_source",2);
        unsigned queries{},scripts{};MessengerBuffer* child{};
        target.recipients.manager=[&](ControllerObjectId recipient)->MessengerManager {
            check(recipient==source,"GETRECNO manager lost cross-file source identity");
            return {[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
                ++queries;child=&messenger.buffer();check(child!=&parent && messenger.buffer_depth()==1 &&
                    delivery.message.category=="MODELCTRL" && delivery.message.name=="enemy_source" && delivery.message.index==2 &&
                    delivery.message.command=="GETRECNO" && messenger.buffer().integers().empty(),
                    "GETRECNO must execute in its real child buffer with exact source selection");
                for(const auto record:records)messenger.buffer().add_int(record);
                messenger.buffer().add_bool(0);messenger.buffer().add_string("child effect");
                return MessengerCallResult{true,1,{}};
            },{}};
        };
        target.services.hit_script=[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
            ++scripts;check(request.target==target.id && request.target_source=="synthetic/body.igb" && request.target_name=="p_target" &&
                request.target_ram==0 && request.kind==-12345 && request.body_ordinal==0 && request.source_name=="enemy_source" &&
                request.source_ram==2 && request.source_record==records[0] && request.script_prefix=="hit" && request.event=="Hit",
                "Target Lua lost complete captured kind/body/source/record/target context");return std::uint8_t{0};
        };
        const bool script_expected=records.size()==1 && records[0]!=-9;
        check(target.handle().complete() && queries==1 && scripts==(script_expected ? 1u : 0u),
            "GETRECNO accepts exactly one integer; sentinel -9 skips target Lua even when it is the actual returned integer");
        check(&target.messenger.buffer()==&parent && target.messenger.buffer_depth()==0 && child && child->integers().empty() && child->strings().empty(),
            "Hit completion did not clear the actual child and restore its original parent object");
        check(parent.integers()==before.integers() && parent.strings()==before.strings() && parent.objects()==before.objects() &&
            parent.matrices()==before.matrices() && parent.vectors()==before.vectors() && parent.integers64()==before.integers64() &&
            std::bit_cast<std::uint32_t>(parent.floats()[0])==0x80000001u && parent.enabled_only()==1,
            "Real buffer restoration changed parked parent argument ownership or scalar words");
        check(parent.booleans()==std::vector<std::uint8_t>{existing_false ? std::uint8_t{0} : std::uint8_t{1}},
            "Existing false parent result suppresses the appended true; child booleans do not replace parent results");
        check(target.messenger.selected().command=="GETRECNO","Buffer pop must not restore the earlier selection/cache record");
    }
}
void player_script_resume_and_heading() {
    Target target;target.packet();unsigned scripts{},effects{};std::vector<ModelTargetHitRequest> requests;
    target.recipients.manager=[](ControllerObjectId)->MessengerManager {throw std::runtime_error("Player source must skip GETRECNO");};
    target.services.hit_script=[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
        ++scripts;requests.push_back(request);
        check(target.messenger.buffer_depth()==1 && request.source_record==-1 && request.source_name=="p_source",
            "Player source must enter target Lua with sentinel -1 in the pushed child");
        if(scripts==1) {
            target.state.pose.matrix()=matrix(200,20,-400);target.state.pose.turn().desired={0.25f,-0.5f,-0.125f};
            target.state.pose.turn().applied={9,8,7};target.state.pose.turn().rate={0,0,0};
            target.state.pose.translated()=true;target.state.pose.position_locked()=true;
            target.messenger.select("SCRIPT_CHANGED_SELECTION",std::nullopt,-1);target.messenger.buffer().add_int(99);
            return std::nullopt;
        }
        ++effects;return std::uint8_t{2};
    };
    check(target.handle().stop==ModelCollisionStop::hit_script_required && target.runtime.pending() && scripts==1,
        "Incomplete target Lua did not retain its request and buffer ownership");
    const auto captured=target.runtime.body().boxes();
    target.services.system_time=[]()->std::optional<std::uint64_t>{throw std::runtime_error("A script retry must not refresh bodies");};
    target.work.control_enabled=0;target.work.current_sequence={};target.state.invulnerability_frames()=90;
    check(target.handle().complete() && effects==1 && scripts==2 && target.runtime.body().boxes()==captured,
        "Lua retry replayed gates/body calculation or failed to complete its live effect");
    check(requests.size()==2 && requests[0].target==requests[1].target && requests[0].kind==requests[1].kind &&
        requests[0].source_record==requests[1].source_record && requests[0].source_name==requests[1].source_name &&
        requests[0].source_ram==requests[1].source_ram && requests[1].target_name=="p_target" && requests[1].body_ordinal==0,
        "Suspended target Lua request was rebuilt from changed shared state");
    const float heading=static_cast<float>(std::atan2(10.0,0.0));
    check(target.state.pose.turn().desired==std::array<float,3>{0.25f,heading,-0.125f} &&
        target.state.pose.turn().applied==target.state.pose.turn().desired && target.state.pose.turn().rate==std::array<float,3>{0,0,0},
        "Forced hit heading must use captured old source/target positions and live post-Lua pitch/roll");
    check(target.state.pose.matrix()[12]==200 && target.state.pose.matrix()[13]==20 && target.state.pose.matrix()[14]==-400 &&
        target.state.pose.matrix()[3]==0 && target.state.pose.matrix()[7]==0 && target.state.pose.matrix()[11]==0 && target.state.pose.matrix()[15]==1 &&
        target.state.pose.translated() && target.state.pose.position_locked() && target.state.invulnerability_frames()==90,
        "Forced hit orientation lost live translation, pose flags or unrelated target work");
    check(target.messenger.buffer_depth()==0 && target.messenger.buffer().booleans()==std::vector<std::uint8_t>{1},
        "Heading completion failed to restore and append to the parent result buffer");
}
void nested_record_and_outer_delivery() {
    Target target;
    const ControllerObjectId first{11,1},last{11,2},source_a{12,1},source_b{12,2};
    target.messenger.register_recipient(first,"HITPLAYERBULLET",std::nullopt,-1);
    target.messenger.register_recipient(target.id,"HITPLAYERBULLET",std::nullopt,-1);
    target.messenger.register_recipient(last,"HITPLAYERBULLET",std::nullopt,-1);
    target.messenger.register_recipient(source_a,"MODELCTRL","enemy_source",2);
    target.messenger.register_recipient(source_b,"MODELCTRL","enemy_source",2);
    target.packet(local,"enemy_source");
    unsigned first_calls{},last_calls{},a_calls{},b_calls{},record_effects{},scripts{};
    target.recipients.active=[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};};
    target.recipients.manager=[&](ControllerObjectId id)->MessengerManager {
        return {[&,id](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            if(id==first) {++first_calls;return MessengerCallResult{true,1,{}};}
            if(id==last) {++last_calls;check(messenger.buffer_depth()==0 && messenger.buffer().booleans()==std::vector<std::uint8_t>{1},
                "Later outer recipient did not receive restored collision results");return MessengerCallResult{true,1,{}};}
            if(id==target.id) {const auto result=target.runtime.handle(messenger,delivery,target.state,target.work,target.recipients,target.services);
                return MessengerCallResult{result.complete(),result.complete() ? 1u : 0u,result.diagnostic};}
            if(id==source_a) {++a_calls;return MessengerCallResult{true,1,{}};}
            ++b_calls;if(b_calls==1)return MessengerCallResult{false,0,"Source record wrapper is unavailable"};
            ++record_effects;messenger.buffer().add_int(42);return MessengerCallResult{true,1,{}};
        },{}};
    };
    target.services.hit_script=[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
        ++scripts;check(request.source_record==42,"Nested GETRECNO did not preserve its completed recipient prefix");return std::uint8_t{0};
    };
    auto outer=target.messenger.begin_send("CHKBULLET");
    const auto paused=target.messenger.resume(outer,target.recipients);
    if(paused.stop==MessengerStop::faulted)throw std::runtime_error("Nested collision delivery faulted: "+paused.diagnostic);
    check(paused.stop==MessengerStop::service_required &&
        target.runtime.result().stop==ModelCollisionStop::record_required && first_calls==1 && a_calls==1 && b_calls==1 && last_calls==0 &&
        target.messenger.buffer_depth()==1,"Nested record query did not pause both inner and outer deliveries at the right rows");
    check(target.messenger.resume(outer,target.recipients).stop==MessengerStop::completed && first_calls==1 && a_calls==1 && b_calls==2 &&
        record_effects==1 && scripts==1 && last_calls==1 && target.messenger.buffer_depth()==0,
        "Nested record retry replayed a completed inner or outer recipient");
}
void script_owned_nested_delivery() {
    Target target;target.packet();const ControllerObjectId effect{22,1};
    target.messenger.register_recipient(effect,"HIT_SCRIPT_EFFECT",std::nullopt,-1);
    unsigned script_calls{},effect_calls{},effects{};std::optional<MessengerDispatch> child;
    target.recipients.manager=[&](ControllerObjectId id)->MessengerManager {
        check(id==effect,"Script effect selected an unrelated recipient");return {[&](NativeMessenger& messenger,const MessengerDelivery&) {
            ++effect_calls;check(messenger.buffer_depth()==2 && messenger.buffer().integers()==std::vector<std::int32_t>{61},
                "Target Lua continuation lost its nested buffer or argument");
            if(effect_calls==1)return MessengerCallResult{false,0,"Effect needs its external owner"};
            ++effects;return MessengerCallResult{true,1,{}};
        },{}};
    };
    target.services.hit_script=[&](const ModelTargetHitRequest&)->std::optional<std::uint8_t> {
        ++script_calls;
        if(!child) {target.messenger.push_buffer();target.messenger.select("HIT_SCRIPT_EFFECT",std::nullopt,-1);
            target.messenger.buffer().add_int(61);child.emplace(target.messenger.begin_send("APPLY"));}
        if(target.messenger.resume(*child,target.recipients).stop!=MessengerStop::completed)return std::nullopt;
        child.reset();target.messenger.pop_buffer();return std::uint8_t{0};
    };
    check(target.handle().stop==ModelCollisionStop::hit_script_required && target.messenger.buffer_depth()==2 && child,
        "Target Lua must own and retain incomplete nested messenger work");
    check(target.handle().complete() && script_calls==2 && effect_calls==2 && effects==1 && !child && target.messenger.buffer_depth()==0 &&
        target.messenger.buffer().booleans()==std::vector<std::uint8_t>{1},"Nested target Lua retry replayed effects or restored the wrong buffer");
}
void owner_guards_and_faults() {
    Target target;target.packet();target.services.system_time={};target.handle();
    auto other_model=target.state;NativeMessenger other_messenger;other_messenger.select("OTHER",std::nullopt,-1);
    rejects([&]{target.runtime.handle(target.messenger,target.delivery(124),target.state,target.work,target.recipients,target.services);},
        "Pending collision accepted a different dispatch");
    rejects([&]{target.runtime.handle(target.messenger,target.delivery(123,1),target.state,target.work,target.recipients,target.services);},
        "Pending collision accepted a different registration row");
    rejects([&]{target.runtime.handle(target.messenger,target.delivery(),other_model,target.work,target.recipients,target.services);},
        "Pending collision accepted a reassigned live model owner");
    rejects([&]{target.runtime.handle(other_messenger,target.delivery(),target.state,target.work,target.recipients,target.services);},
        "Pending collision accepted a different messenger owner");
    check(!target.runtime.faulted() && target.runtime.pending(),"Rejected ownership changes must preserve the valid pending continuation");
    target.services.system_time=[]()->std::optional<std::uint64_t>{return 1;};
    target.services.hit_script=[&](const ModelTargetHitRequest&)->std::optional<std::uint8_t> {
        rejects([&]{target.handle();},"Recursive collision callback was accepted");return std::uint8_t{0};
    };
    check(target.handle().complete() && !target.runtime.faulted(),"Caught recursive rejection poisoned the valid target operation");
    Target thrown;thrown.packet();unsigned effects{};
    thrown.services.hit_script=[&](const ModelTargetHitRequest&)->std::optional<std::uint8_t> {++effects;throw std::runtime_error("Uncertain Lua effect");};
    bool caught=false;try {thrown.handle();}catch(const std::runtime_error&) {caught=true;}
    check(caught && thrown.runtime.faulted() && thrown.messenger.buffer_depth()==1,"Throwing target Lua must fault without automatically popping uncertain child work");
    rejects([&]{thrown.handle();},"Faulted target Lua was replayed");check(effects==1,"Faulted target callback executed twice");
    Target wrong_depth;wrong_depth.packet();wrong_depth.services.hit_script=[&](const auto&)->std::optional<std::uint8_t> {
        wrong_depth.messenger.push_buffer();return std::uint8_t{0};
    };
    rejects([&]{wrong_depth.handle();},"A completed Lua service left an unowned nested buffer");
    check(wrong_depth.runtime.faulted() && wrong_depth.messenger.buffer_depth()==2,"Nested buffer failure silently restored parent state");
    Target full_stack;for(unsigned depth=0;depth<4;++depth)full_stack.messenger.push_buffer();full_stack.packet();
    rejects([&]{full_stack.handle();},"Target collision exceeded the five original prepared buffers");
    check(full_stack.runtime.faulted() && full_stack.messenger.buffer_depth()==4,"Stack overflow changed active buffer depth before rejection");
    Target unsupported;unsupported.message.command="OTHER";
    rejects([&]{unsupported.handle();},"Target collision accepted an unsupported command");
    check(!unsupported.runtime.pending() && !unsupported.runtime.faulted(),"Unsupported command rejection created or faulted a delivery owner");
}
void qualified_aliases_and_packet_boundaries() {
    Target unrelated;unrelated.packet();
    for(const auto recipient:{ControllerObjectId{8,unrelated.id.reference_index},ControllerObjectId{7,999999}}) {
        const MessengerDelivery delivery{123,0,recipient,unrelated.message};
        rejects([&]{unrelated.runtime.handle(unrelated.messenger,delivery,unrelated.state,unrelated.work,unrelated.recipients,unrelated.services);},
            "Fresh collision accepted an unrelated source occurrence or RAM");
        check(!unrelated.runtime.pending() && !unrelated.runtime.faulted(),"Unrelated delivery rejection must precede mutable ownership publication");
    }
    for(const bool canonical:{false,true}) {
        Target alias("p_alias",true,1);alias.packet();
        const ControllerObjectId raw=alias.id,owner{7,alias.fixture.ram};
        check(raw!=owner && alias.definition.ram_owners()[1]==owner.reference_index,"Synthetic alias must retain distinct serialized and physical identities");
        if(canonical)alias.id=owner;
        alias.services.system_time={};check(alias.handle().stop==ModelCollisionStop::system_time_required,
            "Verified raw or canonical RAM endpoint must be accepted for its selected alias instance");
        const MessengerDelivery changed{123,0,canonical ? raw : owner,alias.message};
        check(!alias.runtime.owns_delivery(changed),"Alias substitution must not match an already retained endpoint");
        rejects([&]{alias.runtime.handle(alias.messenger,changed,alias.state,alias.work,alias.recipients,alias.services);},
            "Pending alias delivery accepted a different endpoint despite matching its physical RAM");
        alias.services.system_time=[]()->std::optional<std::uint64_t>{return 1;};
        alias.services.hit_script=[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
            check(request.target==alias.id && request.target_ram==1,"Target request must retain the actual endpoint and original selected RAM number");
            return std::uint8_t{0};
        };
        check(alias.handle().complete() && !alias.runtime.faulted(),"Caught alias substitution rejection poisoned the legitimate retained delivery");
    }
    for(const unsigned kind:{0u,1u,2u}) {
        Target invalid;if(kind==1)invalid.messenger.buffer().add_object(std::nullopt);
        if(kind==2)invalid.messenger.buffer().add_object(ControllerObjectId{7,99});
        rejects([&]{invalid.handle();},"CHKBULLET object zero must be an actual retained AABox");
        check(invalid.runtime.faulted() && !invalid.runtime.body().initialized(),"Invalid object should stop before target body calculation");
    }
    for(const unsigned missing:{0u,1u,2u}) {
        Target invalid;auto& buffer=invalid.messenger.buffer();buffer.add_box(std::make_shared<const CollisionBox>(local));
        if(missing!=0) {buffer.add_int(10000);buffer.add_int(0);}
        if(missing!=1)buffer.add_matrix(matrix());
        if(missing!=2)buffer.add_string("p_source");else buffer.add_string(std::nullopt);
        rejects([&]{invalid.handle();},"Overlapping collision must require each later argument when read");
        check(invalid.runtime.body().initialized() && invalid.messenger.buffer_depth()==0 && invalid.runtime.faulted(),
            "Later malformed arguments must fail after body calculation and before buffer push");
    }
}
} // namespace
int main() {
    try {
        lazy_gates();retained_time_gate_and_first_body();record_counts_and_parent_buffer();player_script_resume_and_heading();
        nested_record_and_outer_delivery();script_owned_nested_delivery();owner_guards_and_faults();qualified_aliases_and_packet_boundaries();
        std::cout<<checks<<" model collision checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Model collision failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
