#include "dw4/action_event_runtime.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) { ++checks;if(!value)throw std::runtime_error(reason); }
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
AnimationMatrix matrix(float x=10,float y=20,float z=30) {
    return {1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};
}
ModelControlState model() {
    ModelMotionPose pose(matrix());set_player_turn_rates(pose);return ModelControlState(pose);
}
ModelHitboxSubcommand command(std::int32_t kind=10000,float delay=0,float length=3) {
    return {kind,delay,length,{1,2,3},{2,4,6}};
}
struct Fixture : pf::RuntimeFixture {
    struct Command {std::uint32_t reference,storage;};
    std::vector<Command> commands;
    std::vector<std::uint32_t> sequences;
    explicit Fixture(ModelHitboxSubcommand value=command()) {
        row("SQ_BOX",{hitbox("Set",value)});
        row("SQ_CLEAR",{hitbox("AllClear",value)});
        row("SQ_EMPTY",{});
        scalar(sequence,4,list(pf::named_list,sequences));
    }
    std::uint32_t hitbox(const std::string& name,const ModelHitboxSubcommand& value) {
        const auto ref=add(pf::node);text(ref,2,name);
        auto bytes=pf::words({std::bit_cast<std::uint32_t>(value.kind),std::bit_cast<std::uint32_t>(value.delay),
            std::bit_cast<std::uint32_t>(value.length)});
        for(const auto vector:{value.offset,value.box_size})for(const auto component:vector)
            pf::push32(bytes,std::bit_cast<std::uint32_t>(component));
        commands.push_back({ref,memory(std::move(bytes))});return ref;
    }
    void row(const std::string& name,const std::vector<std::uint32_t>& values) {
        const auto ref=add(pf::sequence_data_type);text(ref,2,name);scalar(ref,3,list(pf::named_list,values));sequences.push_back(ref);
    }
    IgbDocument document() const {
        auto doc=pf::RuntimeFixture::document();
        auto vector=doc.metadata.field_types[8];vector.name="igVec3fMetaField";
        const auto vector_index=static_cast<std::uint16_t>(doc.metadata.field_types.size());
        doc.metadata.field_types.push_back(std::move(vector));
        for(const auto& entry:commands) {
            auto& object=doc.objects.at(doc.references.at(entry.reference).value_index);
            auto descriptor=doc.metadata.object_types.at(object.type_index);
            descriptor.name="beModelCtrlHITBOX";descriptor.parent_index=pf::object;
            object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back(std::move(descriptor));
            const auto offset=doc.memories.at(doc.references.at(entry.storage).value_index).offset;
            object.fields.push_back({3,1,offset,4});object.fields.push_back({4,8,offset+4,4});
            object.fields.push_back({5,8,offset+8,4});object.fields.push_back({6,vector_index,offset+12,12});
            object.fields.push_back({7,vector_index,offset+24,12});
        }
        return doc;
    }
    PlayerRuntimeBinding binding(std::uint32_t instance=0,std::size_t occurrence=3) const {
        return decode_player_runtime_binding(document(),runtime_request(instance),"synthetic/player.igb",occurrence);
    }
};
void start(ActionEventRuntime& runtime,ControllerObjectId id) {
    check(runtime.select_sequence(id,"SQ_BOX").complete(),"HITBOX sequence selection unexpectedly suspended.");
    check(runtime.run_sequence(id,17).stop==ModelSequenceStop::end,"HITBOX Set did not complete its queue append.");
    check(runtime.subcommands(id).entries().size()==1 && !runtime.subcommands(id).entries()[0].started,
        "Sequence dispatch must append the HITBOX without starting collision delivery.");
}
void prepare(ActionEventRuntime& runtime,ControllerObjectId id,AnimationMatrix value,
    std::optional<std::string> prefix={}) {
    auto state=runtime.sequence(id).model();state.pose.matrix()=value;state.bullet_hit_script=std::move(prefix);
    runtime.prepare_model_state(id,std::move(state));
}
MessengerBox box(const MessengerBuffer& buffer) {
    check(buffer.objects().size()==1 && std::holds_alternative<MessengerBox>(buffer.objects()[0]),
        "HITBOX must send one retained geometric object, not a controller RAM identity.");
    auto result=std::get<MessengerBox>(buffer.objects()[0]);check(bool(result),"HITBOX sent a null geometric object.");return result;
}
void packet_modes_and_lifetime() {
    const std::vector<std::pair<std::int32_t,std::string>> rows{{10000,"HITPLAYERBULLET"},
        {60000,"HITPLAYERTALK"},{70000,"HITENEMYBULLET"},{1000,"HITENEMYBULLET"},{-10000,"HITPLAYERBULLET"}};
    for(const auto& [kind,category]:rows) {
        Fixture fixture(command(kind,0.5f));ActionEventRuntime runtime;
        const auto id=runtime.register_model(fixture.binding(1),model(),true,ModelSubcommandOwner{"attached source",7});
        const ControllerObjectId target{91,123};runtime.register_recipient(target,category,"different file target",52);
        unsigned deliveries{},active_reads{};MessengerBox retained;
        runtime.set_external_services({[&](ControllerObjectId recipient)->std::optional<std::uint8_t> {
            ++active_reads;check(recipient==target,"Activity gate used a different qualified recipient.");return std::uint8_t{2};
        },[&](ControllerObjectId recipient)->MessengerManager {
            check(recipient==target,"Collision manager used a different qualified recipient.");
            return {[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
                ++deliveries;const auto& buffer=messenger.buffer();retained=box(buffer);
                check(delivery.message.category==category && !delivery.message.name && delivery.message.index==-1 &&
                    delivery.message.command=="CHKBULLET","HITBOX selection lost category, wildcard name/index or command.");
                check(buffer.enabled_only()==1 && buffer.integers()==std::vector<std::int32_t>{kind,7} &&
                    buffer.strings()==std::vector<std::optional<std::string>>{"attached source"},
                    "HITBOX packet lost signed kind, enabled gate or attachment identity.");
                check(buffer.matrices()==std::vector<AnimationMatrix>{matrix()} && buffer.booleans().empty(),
                    "HITBOX packet matrix or initial result list changed.");
                check(*retained==CollisionBox{{10,20,30},{12,24,36}},"HITBOX did not transform offset and halve world-axis extent.");
                return MessengerCallResult{true,1,{}};
            },{}};
        }});
        start(runtime,id);check(deliveries==0,"HITBOX append performed collision work.");
        check(runtime.update_subcommands(id,0.25f).complete() && deliveries==0,"HITBOX ignored its fractional delay.");
        check(runtime.update_subcommands(id,0.25f).complete() && deliveries==1 && active_reads==1,
            "A named/indexed target in another source must receive wildcard HITBOX delivery.");
        check(runtime.hitbox_result(id).stop==ModelHitboxStop::complete &&
            runtime.hitbox_result(id).entry_id==runtime.subcommands(id).entries()[0].id,
            "Successful HITBOX result must identify its queue entry.");
        const std::weak_ptr<const CollisionBox> lifetime=retained;
        check(runtime.select_sequence(id,"SQ_CLEAR").complete() && runtime.run_sequence(id,18).stop==ModelSequenceStop::end &&
            runtime.subcommands(id).entries().empty(),"Sequence cleanup and AllClear must remove completed HITBOX work.");
        runtime.messenger().select("unrelated",std::nullopt,-1);
        check(!lifetime.expired() && *retained==CollisionBox{{10,20,30},{12,24,36}},
            "Recipient retention must outlive the temporary check, queue and shared argument buffer.");
        retained.reset();check(lifetime.expired(),"HITBOX retained an unnecessary geometric owner after buffer cleanup.");
    }
}
void registration_retry_and_ownership() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto id=runtime.register_model(fixture.binding(),model(),true);
    const auto other=runtime.register_model(fixture.binding(1),model(),true);
    start(runtime,id);start(runtime,other);
    const auto result=runtime.update_subcommands(id,1);
    check(result.stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.hitbox_result(id).stop==ModelHitboxStop::collision_registration_required &&
        result.entry_id==runtime.subcommands(id).entries()[0].id,"Missing collision registration was silently treated as an empty scene.");
    rejects([&]{runtime.messenger();},"Suspended collision must retain the shared messenger buffer.");
    rejects([&]{prepare(runtime,id,matrix(90));},"Paused HITBOX accepted external work replacement.");
    rejects([&]{runtime.set_hitbox_debug(id,true);},"Paused HITBOX accepted a fresh debug mode.");
    rejects([&]{runtime.select_sequence(other,"SQ_EMPTY");},"Another model selected during a suspended HITBOX.");
    rejects([&]{runtime.run_sequence(other,18);},"Another model ran commands during a suspended HITBOX.");
    rejects([&]{runtime.update_subcommands(other,1);},"Another model advanced subcommands during a suspended HITBOX.");
    rejects([&]{runtime.update_subcommands(id,1);},"Paused HITBOX accepted a new elapsed delta.");
    rejects([&]{runtime.update_wait(other);},"Another player wait ran during a suspended HITBOX.");
    rejects([&]{runtime.broadcast(other,{"synthetic/player.igb","Synthetic player","test",1});},
        "Another ACTION broadcast replaced suspended HITBOX arguments.");
    const ControllerObjectId target{8,901};runtime.register_recipient(target,"HITPLAYERBULLET",std::nullopt,-1);
    std::vector<MessengerBox> boxes;unsigned deliveries{};
    runtime.set_external_services({[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&](ControllerObjectId)->MessengerManager {return {[&](NativeMessenger& messenger,const MessengerDelivery&) {
            ++deliveries;boxes.push_back(box(messenger.buffer()));
            check(messenger.buffer().strings()==std::vector<std::optional<std::string>>{"Synthetic player"} &&
                messenger.buffer().integers()==std::vector<std::int32_t>{10000,deliveries==3 ? 1 : 0},"Unattached HITBOX lost source model identity.");
            return MessengerCallResult{true,1,{}};
        },{}};}});
    check(runtime.resume_subcommands(id).complete() && deliveries==1 && runtime.subcommands(id).entries()[0].elapsed==1,
        "Late registration must resume the same check without replaying its elapsed delta.");
    prepare(runtime,id,matrix(40));
    check(runtime.update_subcommands(id,1).complete() && deliveries==2 && boxes[0]!=boxes[1] &&
        boxes[0]->minimum[0]==10 && boxes[1]->minimum[0]==40,"Next frame must allocate geometry from the new live matrix.");
    check(runtime.update_subcommands(id,1).complete() && deliveries==2 && runtime.subcommands(id).entries().empty(),
        "End equality must expire started HITBOX before another collision query.");
    check(runtime.update_subcommands(other,0).complete() && deliveries==3,"Completed HITBOX did not release global operation ownership.");
}
void partial_delivery_and_lua_continuation() {
    Fixture fixture;ActionEventRuntime runtime;const auto id=runtime.register_model(fixture.binding(),model(),true);
    start(runtime,id);prepare(runtime,id,matrix(),"initial");
    const ControllerObjectId first{9,1},second{10,2};
    runtime.register_recipient(first,"HITPLAYERBULLET",std::nullopt,-1);
    runtime.register_recipient(second,"HITPLAYERBULLET",std::nullopt,-1);
    runtime.register_recipient(first,"HITPLAYERBULLET",std::nullopt,-1);
    unsigned first_calls{},second_calls{},second_effects{},lua_calls{},lua_effects{},resolutions{};
    std::vector<MessengerBox> observed;
    runtime.set_external_services({[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&](ControllerObjectId recipient)->MessengerManager {
            ++resolutions;
            return {[&,recipient](NativeMessenger& messenger,const MessengerDelivery&) {
                observed.push_back(box(messenger.buffer()));
                if(recipient==first) {++first_calls;messenger.buffer().add_bool(0);return MessengerCallResult{true,1,{}};}
                ++second_calls;
                if(second_calls==1)return MessengerCallResult{false,0,"Collision target is waiting for its real effect."};
                ++second_effects;prepare(runtime,id,matrix(45),"live");return MessengerCallResult{true,1,{}};
            },{}};
        }});
    check(runtime.update_subcommands(id,1).stop==ModelSubcommandStop::hitbox_check_required && first_calls==1 && second_calls==1 &&
        runtime.hitbox_result(id).stop==ModelHitboxStop::collision_delivery_required,"Partial target delivery failed to suspend at its recipient.");
    check(runtime.resume_subcommands(id).stop==ModelSubcommandStop::hitbox_check_required && first_calls==2 && second_calls==2 &&
        second_effects==1 && resolutions==3 && runtime.hitbox_result(id).stop==ModelHitboxStop::lua_required,
        "Resume replayed a completed recipient, resolved a captured manager again or skipped BHit for false results.");
    check(observed.size()==4 && observed[0]==observed[1] && observed[1]==observed[2] && observed[2]==observed[3],
        "Partial recipients did not share the retained temporary box.");
    ModelHitboxServices services;services.lua=[&](const ModelLuaRequest& request) {
        ++lua_calls;check(request.source=="synthetic/player.igb" && !request.command_reference &&
            request.info_name=="Synthetic player" && request.ram_no==0 && request.script_prefix=="live" && request.event=="BHit",
            "BHit must capture the live prefix after delivery and retain source identity across retries.");
        if(lua_calls==1) {prepare(runtime,id,matrix(90),"later");return false;}
        ++lua_effects;return true;
    };
    check(runtime.resume_subcommands(id,{},services).stop==ModelSubcommandStop::hitbox_check_required && lua_calls==1,
        "Incomplete Lua service must retain the BHit continuation.");
    rejects([&]{prepare(runtime,id,matrix());},"Suspended Lua accepted external source-state replacement.");
    check(runtime.resume_subcommands(id,{},services).complete() && lua_calls==2 && lua_effects==1 && first_calls==2 && second_calls==2 &&
        runtime.subcommands(id).entries()[0].elapsed==1,"Lua retry replayed collision delivery or elapsed time.");
}
void debug_geometry_then_live_packet() {
    Fixture fixture;ActionEventRuntime runtime;const auto id=runtime.register_model(fixture.binding(),model(),true);
    const auto other=runtime.register_model(fixture.binding(1),model(),true);start(runtime,other);
    start(runtime,id);runtime.set_hitbox_debug(id,true);
    runtime.register_recipient({7,8},"HITPLAYERBULLET",std::nullopt,-1);
    unsigned debug_calls{},debug_effects{},deliveries{};const CollisionBox* debug_box{};std::vector<MessengerBox> boxes;
    runtime.set_external_services({[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&](ControllerObjectId)->MessengerManager {return {[&](NativeMessenger& messenger,const MessengerDelivery&) {
            ++deliveries;boxes.push_back(box(messenger.buffer()));
            check(boxes.back().get()==debug_box && messenger.buffer().matrices()[0]==matrix(50),
                "Debug must retain earlier geometry but packet construction must read its later live matrix.");
            prepare(runtime,id,matrix(80));return MessengerCallResult{true,1,{}};
        },{}};}});
    check(runtime.update_subcommands(id,0.5f).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.hitbox_result(id).stop==ModelHitboxStop::debug_required && deliveries==0,
        "Enabled debug without a service must stop before collision selection.");
    ModelHitboxServices services;services.debug_box=[&](ControllerObjectId source,const ModelHitboxCheckRequest& request,const CollisionBox& value) {
        ++debug_calls;check(source==id && request.bullet_mode==0 && request.command.kind==10000 &&
            request.entry_id==runtime.subcommands(id).entries()[0].id,"Debug callback lost source RAM, entry identity or original color mode.");
        const auto cursor=runtime.sequence(id).command_index();const auto name=std::string(runtime.sequence(id).current_sequence());
        rejects([&]{runtime.select_sequence(id,"SQ_EMPTY");},"Debug callback recursively selected its owning sequence.");
        rejects([&]{runtime.run_sequence(id,90);},"Debug callback recursively ran its owning sequence.");
        rejects([&]{runtime.update_subcommands(other,1);},"Debug callback nested a second model's subcommand pass.");
        check(runtime.sequence(id).command_index()==cursor && runtime.sequence(id).current_sequence()==name &&
            !runtime.sequence(id).selection_pending() && runtime.subcommands(other).entries()[0].elapsed==0,
            "Caught callback reentry rejection partially changed sequence or other-model work.");
        if(debug_calls==1) {debug_box=&value;return false;}
        if(debug_calls==2)check(debug_box==&value && value.minimum[0]==10,"Debug retry regenerated the earlier geometry.");
        else {debug_box=&value;check(value.minimum[0]==80,"Next active pass reused stale geometry.");}
        ++debug_effects;prepare(runtime,id,matrix(50));return true;
    };
    check(runtime.resume_subcommands(id,{},services).stop==ModelSubcommandStop::hitbox_check_required && deliveries==0,
        "Incomplete debug callback advanced to collision delivery.");
    check(runtime.resume_subcommands(id,{},services).complete() && deliveries==1 && debug_calls==2 && debug_effects==1,
        "Debug resume duplicated a committed display or failed to complete collision delivery.");
    check(runtime.update_subcommands(id,0.5f,{},services).complete() && deliveries==2 && boxes[0]!=boxes[1] &&
        boxes[0]->minimum[0]==10 && boxes[1]->minimum[0]==80,"Fresh geometry did not retain immutable earlier-frame boxes.");
}
void lua_owned_messenger_dispatch() {
    Fixture fixture;ActionEventRuntime runtime;const auto id=runtime.register_model(fixture.binding(),model(),true);
    start(runtime,id);prepare(runtime,id,matrix(),"");
    const ControllerObjectId collision{22,4},script{23,5};
    runtime.register_recipient(collision,"HITPLAYERBULLET",std::nullopt,-1);
    runtime.register_recipient(script,"SCRIPT_EFFECT",std::nullopt,-1);
    unsigned collision_calls{},script_calls{},script_effects{},lua_calls{};std::optional<MessengerDispatch> dispatch;
    runtime.set_external_services({[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&](ControllerObjectId recipient)->MessengerManager {return {[&,recipient](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            if(recipient==collision) {++collision_calls;messenger.buffer().add_bool(0);return MessengerCallResult{true,1,{}};}
            ++script_calls;check(delivery.message.category=="SCRIPT_EFFECT" && delivery.message.command=="APPLY" &&
                messenger.buffer().integers()==std::vector<std::int32_t>{73},"Suspended Lua dispatch lost its live argument buffer.");
            if(script_calls==1)return MessengerCallResult{false,0,"Script effect is unavailable."};
            ++script_effects;return MessengerCallResult{true,1,{}};
        },{}};}});
    ModelHitboxServices services;services.lua=[&](const ModelLuaRequest& request) {
        ++lua_calls;check(request.script_prefix.empty() && request.event=="BHit","Empty engaged BHit prefix was treated as absent.");
        if(!dispatch) {
            auto& messenger=runtime.messenger();messenger.select("SCRIPT_EFFECT",std::nullopt,-1);
            messenger.buffer().add_int(73);dispatch.emplace(messenger.begin_send("APPLY"));
        }
        // Retrying a suspended callback resumes its retained operation directly;
        // obtaining mutable messenger access here would replace its arguments.
        const auto result=runtime.resume(*dispatch);
        if(result.stop!=MessengerStop::completed)return false;
        dispatch.reset();return true;
    };
    check(runtime.update_subcommands(id,1,{},services).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.hitbox_result(id).stop==ModelHitboxStop::lua_required && dispatch && script_calls==1,
        "BHit did not retain a script-owned messenger continuation.");
    rejects([&]{runtime.messenger();},"Paused BHit messenger buffer was externally accessible.");
    check(runtime.resume_subcommands(id,{},services).complete() && collision_calls==1 && lua_calls==2 && script_calls==2 &&
        script_effects==1 && !dispatch,"Lua-owned messenger retry replayed the HITBOX or committed effect.");
}
void lua_owned_broadcast() {
    Fixture fixture;ActionEventRuntime runtime;const auto id=runtime.register_model(fixture.binding(),model(),true);
    start(runtime,id);prepare(runtime,id,matrix(),"script");
    const ControllerObjectId collision{31,1},listener{32,2};
    runtime.register_recipient(collision,"HITPLAYERBULLET",std::nullopt,-1);
    runtime.register_recipient(listener,"ACTION_BROADCAST",std::nullopt,-1);
    const ActionBroadcast event{"synthetic/player.igb","Synthetic player","script-event",0};
    unsigned collision_calls{},broadcast_calls{},effects{},lua_calls{};
    runtime.set_external_services({[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [&](ControllerObjectId recipient)->MessengerManager {return {[&,recipient](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            if(recipient==collision) {++collision_calls;messenger.buffer().add_bool(0);return MessengerCallResult{true,1,{}};}
            ++broadcast_calls;check(delivery.message.category=="ACTION_BROADCAST" && delivery.message.name==event.info_name &&
                delivery.message.index==0 && delivery.message.command==event.action,"BHit broadcast lost its retained event identity.");
            if(broadcast_calls==1)return MessengerCallResult{false,0,"Broadcast listener is unavailable."};
            ++effects;return MessengerCallResult{true,1,{}};
        },{}};}});
    ModelHitboxServices services;services.lua=[&](const ModelLuaRequest&) {
        ++lua_calls;return runtime.broadcast(id,event).stop==MessengerStop::completed;
    };
    check(runtime.update_subcommands(id,1,{},services).stop==ModelSubcommandStop::hitbox_check_required &&
        runtime.hitbox_result(id).stop==ModelHitboxStop::lua_required && broadcast_calls==1,
        "BHit wrapper broadcast did not retain its pending delivery.");
    check(runtime.resume_subcommands(id,{},services).complete() && collision_calls==1 && broadcast_calls==2 &&
        lua_calls==2 && effects==1,"BHit could not resume its wrapper-owned broadcast without replaying collision work.");
}
void empty_results_and_validation() {
    for(const bool active:{false,true})for(const bool prefix:{false,true}) {
        Fixture fixture;ActionEventRuntime runtime;const auto id=runtime.register_model(fixture.binding(),model(),true);
        start(runtime,id);prepare(runtime,id,matrix(),prefix ? std::optional<std::string>{"hit"} : std::nullopt);
        runtime.register_recipient({8,2},"HITPLAYERBULLET",std::nullopt,-1);unsigned calls{},lua_calls{};
        runtime.set_external_services({[&](ControllerObjectId)->std::optional<std::uint8_t>{return active ? std::uint8_t{1} : std::uint8_t{0};},
            [&](ControllerObjectId)->MessengerManager {return {[&](NativeMessenger& messenger,const MessengerDelivery&) {
                ++calls;if(!prefix)messenger.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
            },{}};}});
        ModelHitboxServices services;services.lua=[&](const ModelLuaRequest&){++lua_calls;return true;};
        check(runtime.update_subcommands(id,1,{},services).complete() && calls==(active ? 1u : 0u) && lua_calls==0,
            "Inactive targets, absent script or empty result list must not produce BHit.");
        const auto before=runtime.sequence(id).model().pose.matrix();
        auto invalid=runtime.sequence(id).model();invalid.pose.matrix()[12]=std::numeric_limits<float>::infinity();
        rejects([&]{runtime.prepare_model_state(id,invalid);},"Prepared HITBOX accepted a nonfinite live matrix.");
        invalid=runtime.sequence(id).model();invalid.bullet_hit_script=std::string("bad\0prefix",10);
        rejects([&]{runtime.prepare_model_state(id,invalid);},"Prepared HITBOX accepted an embedded-NUL script prefix.");
        check(runtime.sequence(id).model().pose.matrix()==before,"Rejected model preparation partially replaced live state.");
    }
}
} // namespace
int main() {
    try {
        packet_modes_and_lifetime();registration_retry_and_ownership();partial_delivery_and_lua_continuation();
        debug_geometry_then_live_packet();lua_owned_messenger_dispatch();lua_owned_broadcast();empty_results_and_validation();
        std::cout<<"Model HITBOX runtime checks: "<<checks<<"\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Model HITBOX runtime test failed: "<<error.what()<<"\n";return 1;}
}
