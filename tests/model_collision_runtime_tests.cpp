#include "dw4/action_event_runtime.hpp"
#include "dw4/model_collision.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <cmath>
#include <iostream>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F operation,const char* message) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(message);
}
AnimationMatrix matrix(float x=0,float y=0,float z=0) {return {1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};}
ModelControlState model(float x=0) {ModelMotionPose pose(matrix(x));set_player_turn_rates(pose);return ModelControlState(pose);}
constexpr const char* source="synthetic/collision.igb";
constexpr const char* name="p_fixture";
constexpr CollisionBox local{{-1,-1,-1},{1,1,1}};
struct Fixture : pf::RuntimeFixture {
    std::uint32_t hit,hit_storage,box,box_storage,box_list,command,command_storage;
    std::vector<std::uint32_t> ram_storage,sequence_rows;
    std::optional<std::pair<std::uint32_t,std::uint32_t>> lua;
    explicit Fixture(std::array<std::uint8_t,3> flags={1,0,0}) {
        text(pf::RuntimeFixture::model,2,name);text(action,2,name);
        box=add(pf::object);pf::Bytes box_bytes;
        for(const auto point:{local.minimum,local.maximum})for(const auto v:point)pf::push32(box_bytes,std::bit_cast<std::uint32_t>(v));
        box_storage=memory(std::move(box_bytes));box_list=list(pf::object_list,{box});
        hit=add(pf::base_data_type);text(hit,2,"HIT");
        auto flags_bytes=pf::words({0,box_list});flags_bytes[0]=flags[0];flags_bytes[1]=flags[1];flags_bytes[2]=flags[2];
        hit_storage=memory(std::move(flags_bytes));contents(model_data,{sequence,hit});
        for(std::size_t i=0;i<model_rams.size();++i)ram_storage.push_back(memory(pf::words({0,0,0,0,0,0})));
        command=add(pf::node);text(command,2,"Set");
        command_storage=memory(pf::words({10000,0,std::bit_cast<std::uint32_t>(20.f),0,0,0,
            std::bit_cast<std::uint32_t>(12.f),std::bit_cast<std::uint32_t>(4.f),std::bit_cast<std::uint32_t>(4.f)}));
        for(const auto* row_name:{"SQ_BOX","SQ_IDLE","SQ_ERAS"}) {
            const auto row=add(pf::sequence_data_type);text(row,2,row_name);
            scalar(row,3,list(pf::named_list,std::string_view(row_name)=="SQ_BOX" ? std::vector<std::uint32_t>{command} : std::vector<std::uint32_t>{}));
            sequence_rows.push_back(row);
        }
        scalar(sequence,4,list(pf::named_list,sequence_rows));
    }
    void add_hit_sequence() {
        const auto ref=add(pf::node);text(ref,2,"target_nested");
        auto bytes=pf::words({9});pf::name(bytes,"HitReply");lua=std::pair{ref,memory(std::move(bytes))};
        const auto row=add(pf::sequence_data_type);text(row,2,"SQ_HIT");scalar(row,3,list(pf::named_list,{ref}));
        sequence_rows.push_back(row);scalar(sequence,4,list(pf::named_list,sequence_rows));
    }
    IgbDocument document() const {
        auto doc=pf::RuntimeFixture::document();
        auto vector=doc.metadata.field_types[8];vector.name="igVec3fMetaField";
        const auto vector_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());doc.metadata.field_types.push_back(std::move(vector));
        auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        auto offset=[&](std::uint32_t ref) {return doc.memories.at(doc.references.at(ref).value_index).offset;};
        auto type=[&](std::uint32_t ref,const char* type_name,std::int32_t parent) {
            auto descriptor=doc.metadata.object_types.at(object(ref).type_index);descriptor.name=type_name;descriptor.parent_index=parent;
            object(ref).type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(descriptor));
        };
        type(hit,"beModelCtrlInfoDataHit",pf::base_data_type);const auto h=offset(hit_storage);
        object(hit).fields.insert(object(hit).fields.end(),{{4,4,h,1},{5,4,h+1,1},{6,2,h+4,4},{7,4,h+2,1}});
        type(box,"igAABox",pf::object);const auto b=offset(box_storage);
        object(box).fields={{2,vector_type,b,12},{3,vector_type,b+12,12}};type(box_list,"igAABoxList",pf::object_list);
        object(pf::RuntimeFixture::model).fields.push_back({4,4,offset(ram_storage[0])+4,1});
        for(std::size_t i=0;i<model_rams.size();++i) {
            const auto at=offset(ram_storage[i]);auto& fields=object(model_rams[i]).fields;
            fields.insert(fields.end(),{{2,1,at,4},{5,4,at+4,1},{6,1,at+8,4},{7,1,at+12,4},{8,1,at+16,4},{9,1,at+20,4}});
        }
        type(command,"beModelCtrlHITBOX",pf::object);const auto c=offset(command_storage);
        object(command).fields.insert(object(command).fields.end(),{{3,1,c,4},{4,8,c+4,4},{5,8,c+8,4},{6,vector_type,c+12,12},{7,vector_type,c+24,12}});
        if(lua) {
            type(lua->first,"beModelCtrlLUACALL",pf::object);
            object(lua->first).fields.push_back({3,0,offset(lua->second),doc.memories.at(doc.references.at(lua->second).value_index).size});
        }
        return doc;
    }
    PlayerRuntimeBinding binding(std::uint32_t instance=0) const {
        return decode_player_runtime_binding(document(),runtime_request(instance),source,3);
    }
    ModelHitBodyDefinition definition() const {return decode_model_hit_body(document(),pf::RuntimeFixture::model,source,3);}
};
void enable_target(ActionEventRuntime& runtime,ControllerObjectId id) {
    check(runtime.select_sequence(id,"SQ_IDLE").complete(),"Prepared target selection did not finish.");
    runtime.prepare_wait_state(id,{std::nullopt,1,0,0});
}
void start(ActionEventRuntime& runtime,ControllerObjectId id) {
    check(runtime.select_sequence(id,"SQ_BOX").complete(),"Source HITBOX sequence selection did not finish.");
    check(runtime.run_sequence(id,17).stop==ModelSequenceStop::end && runtime.subcommands(id).entries().size()==1,
        "Source HITBOX command did not append exactly one owned entry.");
    auto state=runtime.sequence(id).model();state.bullet_hit_script="source_hit";runtime.prepare_model_state(id,std::move(state));
}
MessengerResult search(ActionEventRuntime& runtime,std::string category) {
    auto send=runtime.messenger().begin_send_to(std::move(category),std::nullopt,-1,"SEARCH");return runtime.resume(send);
}
void registration_ownership() {
    Fixture fixture({1,1,1});ActionEventRuntime runtime;
    const auto id=runtime.register_model(fixture.binding(1),model(),true);
    rejects([&]{runtime.collision_target(id);},"Unprepared collision target was fabricated.");
    const auto before=runtime.messenger().registration_count();
    auto wrong_source=decode_model_hit_body(fixture.document(),fixture.model,"different",3);
    rejects([&]{runtime.prepare_collision_target(id,wrong_source,true);},"Collision definition accepted a different source path.");
    auto wrong_occurrence=decode_model_hit_body(fixture.document(),fixture.model,source,4);
    rejects([&]{runtime.prepare_collision_target(id,wrong_occurrence,true);},"Collision definition accepted a different loaded occurrence.");
    auto swapped=fixture;swapped.contents(swapped.instances,{swapped.model_rams[1],swapped.model_rams[0]});
    rejects([&]{runtime.prepare_collision_target(id,swapped.definition(),true);},"Collision definition accepted another selected physical RAM.");
    check(runtime.messenger().registration_count()==before,"Rejected collision preparation published category rows.");
    runtime.prepare_collision_target(id,fixture.definition(),true);
    check(runtime.messenger().registration_count()==before+3 && runtime.collision_target(id).body().instance_index()==1 &&
        runtime.collision_target(id).body().ram_reference()==fixture.model_rams[1],"Prepared target lost authored categories or selected RAM ownership.");
    for(const auto* category:{"HITPLAYERBULLET","HITPLAYERTALK","HITENEMYBULLET"}) {
        check(search(runtime,category).stop==MessengerStop::completed && runtime.messenger().buffer().integers()==std::vector<std::int32_t>{1},
            "An authored HIT category was omitted or registered more than once.");
    }
    rejects([&]{runtime.prepare_collision_target(id,fixture.definition(),false);},"Prepared collision owner could be replaced.");
    check(runtime.messenger().registration_count()==before+3,"Repeated collision preparation appended duplicate registrations.");

    ActionEventRuntime aliases;const auto first=aliases.register_model(fixture.binding(),model(),true);
    aliases.prepare_collision_target(first,fixture.definition(),true);
    auto alias_fixture=fixture;
    alias_fixture.contents(alias_fixture.instances,{alias_fixture.model_rams[1]});
    alias_fixture.contents(alias_fixture.action_instances,{alias_fixture.action_rams[0]});
    alias_fixture.contents(alias_fixture.combiner_instances,{alias_fixture.combiners[0]});
    auto alias_document=alias_fixture.document();alias_document.references[alias_fixture.model_rams[1]]=alias_document.references[alias_fixture.model_rams[0]];
    const auto alias=aliases.register_model(decode_player_runtime_binding(alias_document,alias_fixture.runtime_request(),source,3),model(),true);
    check(alias.reference_index!=first.reference_index,"Synthetic alias did not retain a distinct serialized endpoint.");
    const auto count=aliases.messenger().registration_count();
    rejects([&]{aliases.prepare_collision_target(alias,decode_model_hit_body(alias_document,alias_fixture.model,source,3),true);},
        "A second raw endpoint installed another collision cache for the same canonical physical RAM.");
    check(aliases.messenger().registration_count()==count && !aliases.collision_target(first).body().initialized(),
        "Rejected physical collision alias changed category rows or the original cache.");
}
void miss_and_system_time_continuation() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto source_id=runtime.register_model(fixture.binding(),model(4),true);
    const auto target=runtime.register_model(fixture.binding(1),model(100),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);enable_target(runtime,target);start(runtime,source_id);
    unsigned scripts=0;ModelHitboxServices source_services;
    source_services.lua=[&](const ModelLuaRequest&) {++scripts;return true;};
    check(runtime.update_subcommands(source_id,1,{},source_services).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.collision_target(target).result().stop==ModelCollisionStop::system_time_required && runtime.collision_target(target).pending(),
        "Missing original timestamp silently bypassed target body evaluation.");
    const auto elapsed=runtime.subcommands(source_id).entries()[0].elapsed;
    rejects([&]{runtime.messenger();},"Suspended collision exposed its mutable source message.");
    rejects([&]{runtime.prepare_model_state(target,model());},"Suspended target accepted unrelated live work replacement.");
    rejects([&]{runtime.select_sequence(source_id,"SQ_IDLE");},"Suspended source collision could be interrupted by selection.");
    rejects([&]{runtime.update_subcommands(source_id,1);},"Pending collision accepted another elapsed delta.");
    runtime.set_collision_services({[]{return std::optional<std::uint64_t>{1};},{}});
    check(runtime.resume_subcommands(source_id,{},source_services).complete() && scripts==0 &&
        !runtime.collision_target(target).pending() && !runtime.collision_target(target).result().body_ordinal &&
        runtime.collision_target(target).body().boxes()==std::vector<CollisionBox>{{{99,-1,-1},{101,1,1}}} &&
        runtime.subcommands(source_id).entries()[0].elapsed==elapsed,"Timestamp retry replayed source time or treated a separated body as a hit.");
    check(runtime.messenger().buffer_depth()==0 && runtime.messenger().buffer().booleans().empty(),"Miss changed explicit buffer depth or published a hit result.");
    runtime.prepare_model_state(target,model());
    check(runtime.update_subcommands(source_id,0,{},source_services).complete() && scripts==0 &&
        runtime.collision_target(target).body().boxes()[0].minimum[0]==99,"Same system timestamp refreshed body geometry after a live pose change.");
}
void hit_script_then_source_script() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto source_id=runtime.register_model(fixture.binding(),model(4),true);
    const auto target=runtime.register_model(fixture.binding(1),model(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);enable_target(runtime,target);start(runtime,source_id);
    unsigned target_calls=0,target_effects=0,source_calls=0,source_effects=0;bool target_ready=false,source_ready=false;
    ModelTargetHitRequest captured;std::vector<std::string> order;
    runtime.set_collision_services({[]{return std::optional<std::uint64_t>{9};},[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
        ++target_calls;
        if(target_calls==1)captured=request;
        check(request.target==target && request.target_source==source && request.target_name==name && request.target_ram==1 &&
            request.kind==10000 && request.body_ordinal==0 && request.source_name==name && request.source_ram==0 && request.source_record==-1 &&
            request.script_prefix=="hit" && request.event=="Hit","Target hit service lost original script, packet or bound RAM fields.");
        if(!target_ready)return std::nullopt;
        auto live=runtime.sequence(target).model();live.pose.matrix=matrix(90,50,70);live.pose.matrix[0]=3;live.pose.matrix[5]=4;live.pose.matrix[10]=5;
        live.pose.position_locked=true;live.pose.translated=true;runtime.prepare_model_state(target,std::move(live));
        ++target_effects;order.push_back("target");return std::uint8_t{1};
    }});
    ModelHitboxServices source_services;source_services.lua=[&](const ModelLuaRequest& request) {
        ++source_calls;check(request.script_prefix=="source_hit" && request.event=="BHit","Source did not receive its own BHit callback after target effects.");
        const auto& live=runtime.sequence(target).model().pose;
        check(live.matrix[12]==90 && live.matrix[13]==50 && live.matrix[14]==70 && live.position_locked && live.translated &&
            std::abs(live.turn.desired[1]-1.5707963267948966f)<0.000001f && live.turn.applied==live.turn.desired &&
            std::abs(live.matrix[8]-1)<0.000001f && std::abs(live.matrix[2]+1)<0.000001f,
            "Hit response used stale post-Lua work or recomputed direction from the new target position.");
        if(!source_ready)return false;
        ++source_effects;order.push_back("source");return true;
    };
    check(runtime.update_subcommands(source_id,1,{},source_services).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.collision_target(target).result().stop==ModelCollisionStop::hit_script_required && target_calls==1 && source_calls==0,
        "Unavailable target Lua was accepted or source BHit ran before target completion.");
    rejects([&]{runtime.update_wait(target);},"Target wait ran through a suspended collision owner.");
    rejects([&]{runtime.run_sequence(target,18);},"Target sequence ran through a suspended collision owner.");
    target_ready=true;
    check(runtime.resume_subcommands(source_id,{},source_services).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.hitbox_result(source_id).stop==ModelHitboxStop::lua_required && !runtime.collision_target(target).pending() &&
        target_calls==2 && target_effects==1 && source_calls==1,"Target completion failed to restore the source buffer before BHit suspension.");
    source_ready=true;
    check(runtime.resume_subcommands(source_id,{},source_services).complete() && target_calls==2 && target_effects==1 && source_calls==2 && source_effects==1 &&
        order==std::vector<std::string>{"target","source"},"Resuming source BHit replayed a completed target effect.");
    const auto& buffer=runtime.messenger().buffer();
    check(runtime.messenger().buffer_depth()==0 && buffer.enabled_only()==1 && buffer.booleans()==std::vector<std::uint8_t>{1} &&
        buffer.integers()==std::vector<std::int32_t>{10000,0} && buffer.strings()==std::vector<std::optional<std::string>>{name} &&
        buffer.matrices()==std::vector<AnimationMatrix>{matrix(4)} && buffer.objects().size()==1 && std::holds_alternative<MessengerBox>(buffer.objects()[0]),
        "Target hit pop did not restore the complete original source HITBOX packet.");
    check(captured.target_ram==1 && runtime.collision_target(target).body().calc_time()==9 &&
        runtime.collision_target(target).body().boxes()==std::vector<CollisionBox>{local},"Target hit failed to retain its pre-script body cache and request.");
}
void target_sequence_cleanup_and_lua_retry() {
    Fixture fixture;fixture.add_hit_sequence();ActionEventRuntime runtime;
    const auto source_id=runtime.register_model(fixture.binding(),model(4),true);
    const auto target=runtime.register_model(fixture.binding(1),model(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);enable_target(runtime,target);
    auto sound=runtime.messenger().begin_send_to("MODELCTRL",name,1,"SETSOUND");
    runtime.messenger().buffer().add_float(0);runtime.messenger().buffer().add_string("target_loop");
    check(runtime.resume(sound).stop==MessengerStop::completed,"Synthetic target sound did not enter its real owned queue.");
    ModelSubcommandServices audio;unsigned started=0,cleanup_calls=0,cleaned=0;bool cleanup_ready=false;
    audio.play_sound=[&](const std::optional<std::string>& value) {check(value=="target_loop","Target sound identity changed.");++started;return ModelSoundPlayResult{true,17,4};};
    audio.sound_volume_down=[&](std::uint64_t handle) {++cleanup_calls;check(handle==17,"Target sequence cleanup lost its sound handle.");if(!cleanup_ready)return false;++cleaned;return true;};
    check(runtime.update_subcommands(target,0,audio).complete() && started==1 && runtime.subcommands(target).entries()[0].started,
        "Target loop was not retained for later sequence cleanup.");
    start(runtime,source_id);
    unsigned hit_calls=0,hit_effects=0,lua_calls=0,lua_effects=0,source_calls=0;bool selecting=false,lua_ready=false;
    ModelLuaDispatch lua=[&](const ModelLuaRequest& request) {
        ++lua_calls;check(request.ram_no==1 && request.script_prefix=="target_nested" && request.event=="HitReply",
            "Nested target sequence lost its immediate Lua identity.");
        if(!lua_ready)return false;++lua_effects;return true;
    };
    runtime.set_collision_services({[]{return std::optional<std::uint64_t>{1};},[&](const ModelTargetHitRequest&)->std::optional<std::uint8_t> {
        ++hit_calls;
        if(!selecting) {selecting=true;if(!runtime.select_sequence(target,"SQ_HIT",audio).complete())return std::nullopt;}
        else if(runtime.sequence(target).selection_pending() && !runtime.resume_sequence(target,audio).complete())return std::nullopt;
        const auto result=runtime.run_sequence(target,91,{},audio,{},lua);
        if(result.stop==ModelSequenceStop::lua_required)return std::nullopt;
        check(result.stop==ModelSequenceStop::end,"Nested target sequence did not finish after its actual callback.");
        ++hit_effects;return std::uint8_t{0};
    }});
    ModelHitboxServices source_services;source_services.lua=[&](const ModelLuaRequest&) {++source_calls;return true;};
    check(runtime.update_subcommands(source_id,1,{},source_services).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.sequence(target).selection_pending() && cleanup_calls==1 && lua_calls==0 && hit_calls==1,
        "Target hit script did not retain its nested sound-cleanup suspension.");
    cleanup_ready=true;
    check(runtime.resume_subcommands(source_id,{},source_services).stop==ModelSubcommandStop::hitbox_check_required &&
        !runtime.sequence(target).selection_pending() && runtime.sequence(target).lua_pending() && cleaned==1 && cleanup_calls==2 &&
        hit_calls==2 && lua_calls==1 && source_calls==0,
        "Owned collision retry could not resume target cleanup and suspend its later immediate Lua.");
    lua_ready=true;
    check(runtime.resume_subcommands(source_id,{},source_services).complete() && hit_calls==3 && hit_effects==1 && lua_calls==2 && lua_effects==1 &&
        cleanup_calls==2 && cleaned==1 && source_calls==1 && runtime.subcommands(target).entries().empty() &&
        runtime.subcommands(source_id).entries()[0].elapsed==1 && !runtime.sequence(target).lua_pending() &&
        runtime.messenger().buffer_depth()==0 && runtime.messenger().buffer().booleans()==std::vector<std::uint8_t>{1},
        "Nested target retry replayed cleanup/Lua, changed elapsed time or failed to restore the source packet.");
}
void source_record_query_and_service_replacement() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto source_id=runtime.register_model(fixture.binding(),model(4),true,ModelSubcommandOwner{"e_attached",6});
    const auto target=runtime.register_model(fixture.binding(1),model(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);enable_target(runtime,target);start(runtime,source_id);
    constexpr ControllerObjectId record_owner{90,17};runtime.register_recipient(record_owner,"MODELCTRL","e_attached",6);
    unsigned record_calls=0,record_effects=0,target_calls=0,source_calls=0;bool record_ready=false;
    runtime.set_external_services({{},[&](ControllerObjectId id)->MessengerManager {
        check(id==record_owner,"Source GETRECNO resolved an unrelated owner.");
        return {[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            ++record_calls;
            check(delivery.message.category=="MODELCTRL" && delivery.message.name=="e_attached" && delivery.message.index==6 &&
                delivery.message.command=="GETRECNO" && messenger.buffer_depth()==1 && messenger.buffer().objects().empty(),
                "GETRECNO did not select an independent child buffer with the exact source identity.");
            if(!record_ready)return MessengerCallResult{false,0,"Record owner unavailable"};
            messenger.buffer().add_int(321);messenger.buffer().add_string("child result");++record_effects;
            return MessengerCallResult{true,1,{}};
        },{}};
    }});
    runtime.set_collision_services({[]{return std::optional<std::uint64_t>{2};},[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
        ++target_calls;check(request.source_name=="e_attached" && request.source_ram==6 && request.source_record==321,
            "Target Lua did not retain the completed source record query.");return std::nullopt;
    }});
    ModelHitboxServices source_services;source_services.lua=[&](const ModelLuaRequest&) {++source_calls;return true;};
    check(runtime.update_subcommands(source_id,1,{},source_services).stop==ModelSubcommandStop::hitbox_check_required && record_calls==1 &&
        runtime.collision_target(target).result().stop==ModelCollisionStop::record_required && target_calls==0,
        "Incomplete GETRECNO was accepted or target Lua ran before source record resolution.");
    record_ready=true;
    check(runtime.resume_subcommands(source_id,{},source_services).stop==ModelSubcommandStop::hitbox_check_required && record_calls==2 && record_effects==1 &&
        target_calls==1 && source_calls==0 && runtime.collision_target(target).result().stop==ModelCollisionStop::hit_script_required,
        "Record retry did not preserve its completion before target Lua suspension.");
    runtime.set_collision_services({[]{return std::optional<std::uint64_t>{999};},[&](const ModelTargetHitRequest& request)->std::optional<std::uint8_t> {
        ++target_calls;check(request.source_record==321 && request.source_name=="e_attached","Replacing the unavailable hit service lost its owned request.");return std::uint8_t{0};
    }});
    check(runtime.resume_subcommands(source_id,{},source_services).complete() && record_calls==2 && record_effects==1 && target_calls==2 && source_calls==1 &&
        runtime.collision_target(target).body().calc_time()==2,"Service replacement replayed GETRECNO or refreshed a completed body calculation.");
    const auto& messenger=runtime.messenger();const auto& buffer=messenger.buffer();
    check(messenger.buffer_depth()==0 && buffer.integers()==std::vector<std::int32_t>{10000,6} &&
        buffer.strings()==std::vector<std::optional<std::string>>{"e_attached"} && buffer.booleans()==std::vector<std::uint8_t>{1} &&
        buffer.objects().size()==1 && std::holds_alternative<MessengerBox>(buffer.objects()[0]) &&
        messenger.selected().category=="MODELCTRL" && messenger.selected().name=="e_attached" && messenger.selected().command=="GETRECNO",
        "Target restoration retained child values or incorrectly restored the prior messenger selection.");
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);const std::string original_source="_common/p_agumon.igb";
    const auto document=read_igb_document(resources.read(original_source));const auto definition=decode_model_hit_body(document,1386,original_source,7);
    ActionEventRuntime runtime;std::vector<ControllerObjectId> players;
    for(std::uint32_t i=0;i<4;++i) {
        const auto id=runtime.register_model(decode_player_runtime_binding(document,{1386,546,i,1},original_source,7),model(float(i)*20),true);
        players.push_back(id);runtime.prepare_collision_target(id,definition,true);
        check(runtime.select_sequence(id,"SQ_WAIT").complete(),"Original target SQ_WAIT selection failed.");
        runtime.prepare_wait_state(id,{std::nullopt,1,0,0});
    }
    check(runtime.messenger().registration_count()==8,"Four original players did not register MODELCTRL and their one authored collision category.");
    for(const auto* category:{"HITPLAYERBULLET","HITPLAYERTALK","HITENEMYBULLET"}) {
        const int expected=std::string_view(category)=="HITENEMYBULLET" ? 4 : 0;
        check(search(runtime,category).stop==MessengerStop::completed && runtime.messenger().buffer().integers()==std::vector<std::int32_t>{expected},
            "Original Agumon registered an unauthored category or omitted a selected RAM.");
    }
    std::uint64_t time=100;unsigned scripts=0;
    runtime.set_collision_services({[&]{return std::optional<std::uint64_t>{++time};},[&](const ModelTargetHitRequest&)->std::optional<std::uint8_t>{++scripts;return std::uint8_t{0};}});
    auto send=runtime.messenger().begin_send_to("HITENEMYBULLET",std::nullopt,-1,"CHKBULLET");
    runtime.messenger().buffer().set_enabled_only(1);
    runtime.messenger().buffer().add_box(std::make_shared<const CollisionBox>(CollisionBox{{1000,0,1000},{1001,1,1001}}));
    check(runtime.resume(send).stop==MessengerStop::completed && scripts==0,"Separated original target bodies invoked hit effects.");
    for(std::size_t i=0;i<players.size();++i) {
        const auto& target=runtime.collision_target(players[i]);
        check(target.body().ram_reference()==definition.ram_references()[i] && target.body().calc_time()==101+i && !target.pending() &&
            target.body().boxes()==std::vector<CollisionBox>{{{float(i)*20-2.5f,0,-2.5f},{float(i)*20+2.5f,7,2.5f}}},
            "Original category delivery lost RAM order, per-target cache ownership or authored body bounds.");
    }
    std::cout<<"Original collision runtime: selected_targets=4 enemy_bullet_rows=4 other_collision_rows=0 misses=4\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        registration_ownership();miss_and_system_time_continuation();hit_script_then_source_script();target_sequence_cleanup_and_lua_retry();
        source_record_query_and_service_replacement();
        if(args.size()==2)original(args[1]);else if(args.size()!=1)throw std::runtime_error("Usage: dw4_model_collision_runtime_tests [EXTRACTED_ROOT]");
        std::cout<<"Model collision runtime checks: "<<checks<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<"Model collision runtime after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);
}
