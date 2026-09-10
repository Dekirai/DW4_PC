#include "dw4/action_event_runtime.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>
#include <map>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) { ++checks; if(!value)throw std::runtime_error(reason); }
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try { operation(); }catch(const std::exception&) { return; }throw std::runtime_error(reason);
}
AnimationMatrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
ModelControlState model() { ModelMotionPose pose(identity());set_player_turn_rates(pose);return ModelControlState(pose); }
struct Fixture : pf::RuntimeFixture {
    std::vector<std::uint32_t> definitions,sequences;
    std::map<std::uint32_t,std::string> types;
    std::map<std::uint32_t,std::vector<std::uint16_t>> float_slots;
    std::map<std::uint32_t,std::uint32_t> second_values;
    Fixture() {
        define("empty",{});define("tail",{});define("unused",{});
        define("frame",{command("beAction2WAITCHECK","FRAME",2)});
        define("random",{command("beAction2FADESET","IN",12,1),command("beAction2WAITCHECK","RANS",1),
            command("beAction2WAITCHECK","RAND",3)});
        scalar(action,9,list(pf::action_data_list,definitions));
        sequence_row("SQ_BOOT",{command("beModelCtrlACTION","empty")});
        sequence_row("SQ_FLOW",{command("beModelCtrlACTION","frame"),command("beModelCtrlWCHECK","Action"),
            command("beModelCtrlACTION","empty"),command("beModelCtrlWCHECK","Moving"),command("beModelCtrlACTION","tail")});
        sequence_row("SQ_RANDOM",{command("beModelCtrlMOVE","Move",0.7f,6),command("beModelCtrlACTION","random"),
            command("beModelCtrlWCHECK","Action"),command("beModelCtrlACTION","empty")});
        sequence_row("SQ_ATCK02",{command("beModelCtrlWCHECK","Atck02"),command("beModelCtrlACTION","unused")});
        sequence_row("SQ_WAIT",{command("beModelCtrlACTION","empty"),command("beModelCtrlWCHECK","Rotation"),command("beModelCtrlACTION","tail")});
        scalar(sequence,4,list(pf::named_list,sequences));
    }
    std::uint32_t command(std::string type,std::string name,float first=0,float second=0) {
        const auto ref=add(pf::sequence_type);text(ref,2,name);
        scalar(ref,3,std::bit_cast<std::uint32_t>(first));scalar(ref,4,0);
        if(type=="beAction2WAITCHECK")float_slots[ref]={3};
        if(type=="beAction2FADESET" || type=="beModelCtrlMOVE") {
            float_slots[ref]={3,4};second_values[ref]=memory(pf::floats({second}));
        }
        types[ref]=std::move(type);return ref;
    }
    void define(const std::string& name,const std::vector<std::uint32_t>& commands) {
        const auto ref=add(pf::sequence_data_type);text(ref,2,name);scalar(ref,3,list(pf::named_list,commands));
        types[ref]="beAction2Data";definitions.push_back(ref);
    }
    void sequence_row(const std::string& name,const std::vector<std::uint32_t>& commands) {
        const auto ref=add(pf::sequence_data_type);text(ref,2,name);scalar(ref,3,list(pf::named_list,commands));sequences.push_back(ref);
    }
    IgbDocument document() const {
        auto doc=pf::RuntimeFixture::document();
        for(const auto& [reference,type]:types) {
            auto& object=doc.objects.at(doc.references.at(reference).value_index);
            auto descriptor=doc.metadata.object_types.at(object.type_index);descriptor.name=type;descriptor.parent_index=pf::object;
            if(const auto found=float_slots.find(reference);found!=float_slots.end())for(const auto slot:found->second) {
                const_cast<IgbFieldValue*>(doc.field(object,slot))->type_index=8;
                for(auto& field:descriptor.fields)if(field.slot==slot)field.type_index=8;
            }
            if(const auto found=second_values.find(reference);found!=second_values.end())
                const_cast<IgbFieldValue*>(doc.field(object,4))->offset=doc.memories.at(doc.references.at(found->second).value_index).offset;
            object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(descriptor));
        }
        return doc;
    }
    PlayerRuntimeBinding binding(std::uint32_t instance=0,std::size_t occurrence=3) const {
        return decode_player_runtime_binding(document(),runtime_request(instance),"synthetic/player.igb",occurrence);
    }
    AnimationSourceRuntime library(const PlayerRuntimeBinding& binding) const {
        // Four identity tracks required by actual actor activation. These
        // generated streams contain no original game bytes.
        std::array<pf::Bytes,14> streams;streams[0].resize(7);streams[1]={1,1,1,1};
        streams[9]={64};streams[10]={100};streams[13]={0,0,0,0};
        for(const auto component:{3u,10u,17u,24u})streams[0][component/4]|=std::uint8_t(1u<<(6-2*(component%4)));
        pf::Bytes bytes(80);pf::word(bytes,0,0x002abf84);pf::word(bytes,4,4);
        pf::word(bytes,8,std::bit_cast<std::uint32_t>(1.f));pf::word(bytes,12,std::bit_cast<std::uint32_t>(1.f));pf::word(bytes,16,1);
        for(std::size_t i=0;i<streams.size();++i)pf::word(bytes,20+4*i,static_cast<std::uint32_t>(streams[i].size()));
        for(const auto i:{3,8,12,2,7,11,0,1,4,5,6,9,10,13})pf::append(bytes,streams[i]);
        AnimationClip clip;clip.info.supported=true;clip.info.reference_index=binding.action_program().animations().front().first;
        clip.info.name="waitS";clip.info.duration_seconds=1;clip.info.duration_ticks=1000000000;clip.info.track_count=4;
        clip.info.source_reference=9000;clip.info.skeleton_reference=skeleton;clip.channels=decode_enbaya(bytes);
        clip.bone_references=bones;clip.constant_translations.resize(4);clip.use_sample_translation.resize(4);
        AnimationSourceRuntime result(binding.source(),{std::move(clip)});result.initialize_action_offset();return result;
    }
    std::shared_ptr<ActionRuntime> runtime(const PlayerRuntimeBinding& binding,const AnimationSourceRuntime& sources) const {
        AnimationCombiner combiner(sources,std::vector<AnimationMatrix>(4,identity()),decode_animation_hierarchy(document(),skeleton),
            std::vector<AnimationMatrix>(4,identity()),std::vector<AnimationMatrix>(2,identity()),binding.combiner_reference());
        auto actors=decode_action_actor_group(document(),binding.action_ram_reference(),binding.combiner_reference(),binding.source());
        return std::make_shared<ActionRuntime>(binding.action_program(),sources,static_cast<std::int32_t>(binding.instance_index()),std::move(combiner),std::move(actors));
    }
};
bool pass_complete(const ModelSequenceResult& result) {
    return result.stop==ModelSequenceStop::end || result.stop==ModelSequenceStop::label_boundary || result.stop==ModelSequenceStop::waiting;
}
void live_ownership_and_query_prefix() {
    Fixture fixture;const auto binding=fixture.binding();auto sources=fixture.library(binding);
    auto action=fixture.runtime(binding,sources);ActionEventRuntime events;
    const auto first=events.register_model(binding,model(),true);
    const auto second=events.register_model(fixture.binding(1),model(),true);
    const auto reload=events.register_model(fixture.binding(0,4),model(),true);
    const ControllerObjectId prefix{30,1},tail{31,2};
    events.messenger().register_recipient(prefix,"ACTION",binding.model_name(),0);
    const auto action_id=events.register_action(first,action);
    check(action_id==ControllerObjectId{3,binding.action_ram_reference()},"Live ACTION registration lost qualified RAM identity.");
    const auto count=events.messenger().registration_count();
    rejects([&]{events.register_action(reload,action);},"One live ACTION was accepted for another loaded source occurrence.");
    rejects([&]{events.register_action(second,action);},"One live ACTION was accepted for another instance.");
    check(events.messenger().registration_count()==count,"Rejected live ownership partially registered a recipient.");
    events.register_action(second,fixture.runtime(fixture.binding(1),sources));
    events.register_action(reload,fixture.runtime(fixture.binding(0,4),sources));
    events.messenger().register_recipient(tail,"ACTION",binding.model_name(),0);
    // The repeated source registration also matches. Give it represented work
    // through the same public owner path, without a fabricated query answer.
    ActionServices services;events.select_sequence(first,"SQ_BOOT");events.run_action_sequence(first,sources,10,services);
    events.select_sequence(reload,"SQ_BOOT");events.run_action_sequence(reload,sources,10,services);
    unsigned prefix_calls{},tail_calls{},resolutions{};bool available=false;
    MessengerServices external;
    external.manager=[&](ControllerObjectId id) {
        ++resolutions;
        return MessengerManager{[&,id](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            check(delivery.message.command=="ISCOMEND","Prepared wait query changed the original command key.");
            if(id==prefix) {++prefix_calls;messenger.buffer().add_bool(0);return MessengerCallResult{true,1,{}};}
            check(id==tail,"Unexpected external query recipient.");++tail_calls;
            check(messenger.buffer().booleans()==std::vector<std::uint8_t>({0,1,1}),"Query suspension lost or replayed the first recipient prefix.");
            if(!available)return MessengerCallResult{false,0,"External query manager pending."};
            messenger.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
        },{}};
    };
    events.set_external_services(external);
    check(!events.query_action_complete(first).has_value() && prefix_calls==1 && tail_calls==1,"Missing external query recipient did not suspend after live work.");
    rejects([&]{events.messenger();},"Suspended query exposed the mutable shared buffer.");
    rejects([&]{events.select_sequence(second,"SQ_BOOT");},"Another player interrupted a suspended query.");
    available=true;
    check(events.query_action_complete(first)==false && prefix_calls==1 && tail_calls==2 && resolutions==2,
        "Query failed to consume boolean zero or repeated a completed/captured recipient.");
    check(events.messenger().buffer().booleans()==std::vector<std::uint8_t>({0,1,1,1}) && action->query_command_end()==true,
        "First boolean was replaced by the later completed live ACTION result.");
}
void retained_action_services_and_time() {
    Fixture fixture;const auto binding=fixture.binding();auto sources=fixture.library(binding);auto action=fixture.runtime(binding,sources);
    ActionEventRuntime events;const auto id=events.register_model(binding,model(),true);events.register_action(id,action);
    const ControllerObjectId prefix{40,1},tail{40,2};
    events.messenger().register_recipient(prefix,"ACTION_BROADCAST",binding.model_name(),0);
    events.messenger().register_recipient(tail,"ACTION_BROADCAST",binding.model_name(),0);
    bool available=false;unsigned prefix_calls{},tail_calls{},random_calls{},restored_calls{};
    MessengerServices external;external.manager=[&](ControllerObjectId recipient) {
        return MessengerManager{[&,recipient](NativeMessenger&,const MessengerDelivery& delivery) {
            check(delivery.message.command=="random","Suspended ACTION broadcast changed its action name.");
            if(recipient==prefix) {++prefix_calls;return MessengerCallResult{true,1,{}};}
            ++tail_calls;return MessengerCallResult{available,1,{}};
        },{}};
    };events.set_external_services(external);
    ActionServices services;services.try_broadcast=[&](const auto&){++restored_calls;return true;};
    events.select_sequence(id,"SQ_RANDOM");const std::uint64_t started=1000000000;
    auto result=events.run_action_sequence(id,sources,started,services);
    check(result.stop==ModelSequenceStop::action_required && events.last_action_result(id)->stop==ActionStop::random_required &&
        events.sequence(id).command_index()==1 && action->command_index()==2 && services.random_lower==1 && prefix_calls==0,
        "Random service boundary lost the name[3]=='S' lower-bound setter or replayed the sequence prefix.");
    const auto fade=action->timers().at("FADE");const auto motion=events.sequence(id).model().elapsed_timer;
    rejects([&]{events.run_sequence(id,started);},"Generic sequence execution bypassed a suspended live ACTION owner.");
    auto foreign_sources=sources;
    rejects([&]{events.run_action_sequence(id,foreign_sources,started,services);},"A copied source runtime replaced a retained invocation owner.");
    auto foreign_services=services;
    rejects([&]{events.run_action_sequence(id,sources,started,foreign_services);},"A copied random service owner replaced the retained mutable owner.");
    services.random_word=[&]{++random_calls;return 0u;};
    result=events.run_action_sequence(id,sources,started+9000000000,services);
    check(result.stop==ModelSequenceStop::action_required && events.last_action_result(id)->stop==ActionStop::broadcast_required &&
        action->command_index()==3 && random_calls==1 && prefix_calls==1 && tail_calls==1,
        "ACTION resume did not reach its broadcast once after the missing random source arrived.");
    check(action->timers().at("WAIT").saved_world_time==started && action->timers().at("WAIT").end_time==16666667 &&
        action->timers().at("FADE")==fade && events.sequence(id).model().elapsed_timer==motion,
        "Resumed ACTION used retry time, lost the shared random lower bound or restarted completed timers.");
    available=true;result=events.run_action_sequence(id,sources,started+19000000000,services);
    check(result.stop==ModelSequenceStop::waiting && events.sequence(id).command_index()==3 &&
        events.sequence(id).wait_state().name=="Action" && random_calls==1 && prefix_calls==1 && tail_calls==2,
        "Broadcast resume duplicated its prefix or consumed the next model wait.");
    check(services.try_broadcast({binding.source(),binding.model_name(),"caller",0}) && restored_calls==1,
        "The prepared bridge failed to restore the caller's broadcast service.");
}
void waits_query_and_independent_pads() {
    Fixture fixture;const auto first_binding=fixture.binding();auto sources=fixture.library(first_binding);ActionEventRuntime events;
    std::array<ControllerObjectId,2> ids;std::array<std::shared_ptr<ActionRuntime>,2> actions;
    ActionServices services;
    for(std::uint32_t i=0;i<2;++i) {
        const auto binding=fixture.binding(i);ids[i]=events.register_model(binding,model(),true);actions[i]=fixture.runtime(binding,sources);
        events.register_action(ids[i],actions[i]);events.select_sequence(ids[i],"SQ_FLOW");
        check(events.run_action_sequence(ids[i],sources,1000000000,services).stop==ModelSequenceStop::waiting &&
            events.sequence(ids[i]).command_index()==2 && actions[i]->query_command_end()==false,"Real ACTION frame work did not precede the model WCHECK boundary.");
    }
    unsigned resumed{};PlayerWaitServices common;
    common.resume_commands=[&]{++resumed;return pass_complete(events.run_action_sequence(ids[0],sources,2000000000,services));};
    check(events.update_wait(ids[0],common).complete() && resumed==0 && events.sequence(ids[0]).wait_state().name=="Action",
        "Incomplete real ACTION work was treated as ready by the model wait.");
    actions[0]->update(sources,services,2000000000);
    check(events.update_wait(ids[0],common).complete() && resumed==1 && events.sequence(ids[0]).command_index()==4 &&
        events.sequence(ids[0]).wait_state().name=="Moving" && actions[0]->current_action()=="empty",
        "Completed real ACTION did not resume the retained next command and preserve its newly assigned wait.");
    check(events.sequence(ids[1]).command_index()==2 && events.sequence(ids[1]).wait_state().name=="Action" &&
        actions[1]->query_command_end()==false,"One player's wait advanced another player's actual ACTION work.");
    // Independent original slots drive the Atck01 latch while both actual
    // ACTIONs are incomplete. No merged preview input or invented query value.
    for(std::uint32_t i=0;i<2;++i) {
        events.select_sequence(ids[i],"SQ_FLOW");events.run_action_sequence(ids[i],sources,3000000000,services);
        events.prepare_wait_state(ids[i],{"Atck01",1,0,9});
    }
    OriginalPads pads;OriginalPads::Samples samples{};
    samples[0]={true,{},{},pad_button::a};samples[1]={true,{},{},pad_button::b};pads.update(samples);
    PlayerWaitServices supplied;supplied.pads=&pads;
    check(events.update_wait(ids[0],supplied).complete() && events.update_wait(ids[1],supplied).complete(),"Ordinary incomplete attack queries unexpectedly suspended.");
    check(events.sequence(ids[0]).wait_state()==ModelWaitState{"Atck01",1,1,0} &&
        events.sequence(ids[1]).wait_state()==ModelWaitState{"Atck01",1,1,1},"Distinct OriginalPads slots shared their A-button latch.");
    actions[0]->update(sources,services,4000000000);actions[1]->update(sources,services,4000000000);
    supplied.resume_commands=[&]{return pass_complete(events.run_action_sequence(ids[0],sources,4000000000,services));};
    check(events.update_wait(ids[0],supplied).complete() && events.sequence(ids[0]).wait_state()==ModelWaitState{"Moving",1,0,0},
        "Held A failed to resume its own cursor after the real ACTION completed.");
    supplied.resume_commands={};
    check(events.update_wait(ids[1],supplied).stop==ModelWaitStop::sequence_required && events.wait_pending(ids[1]) &&
        events.sequence(ids[1]).wait_state()==ModelWaitState{std::nullopt,1,0,1},"Released A silently supplied the missing outer SQ_WAIT dispatcher.");
    rejects([&]{events.update_wait(ids[0],common);},"Another player bypassed a suspended outer dispatch boundary.");
    rejects([&]{events.prepare_wait_state(ids[1],{});},"Pending wait state could be replaced before its owner resumed.");
    check(events.resume_wait(ids[1],supplied).stop==ModelWaitStop::sequence_required,"Omitting outer dispatch on retry bypassed the retained boundary.");
    // This is explicitly a prepared test dispatch, not full original player
    // entry: select, cleanup and run only, with no claim about external callbacks.
    unsigned selections{};
    supplied.dispatch_sequence=[&](std::string_view name) {
        ++selections;return events.select_sequence(ids[1],name).complete() &&
            pass_complete(events.run_action_sequence(ids[1],sources,4000000000,services));
    };
    check(events.resume_wait(ids[1],supplied).complete() && selections==1 && !events.wait_pending(ids[1]) &&
        events.sequence(ids[1]).current_sequence()=="SQ_WAIT" && events.sequence(ids[1]).wait_state()==ModelWaitState{"Rotation",1,0,1},
        "Prepared outer service failed to preserve live destination wait and attack latch.");
}
void attack_two_live_destination() {
    Fixture fixture;const auto binding=fixture.binding();auto sources=fixture.library(binding);auto action=fixture.runtime(binding,sources);
    ActionEventRuntime events;const auto id=events.register_model(binding,model(),true);events.register_action(id,action);ActionServices services;
    events.select_sequence(id,"SQ_ATCK02");check(events.run_action_sequence(id,sources,100,services).stop==ModelSequenceStop::waiting,
        "Atck02 fixture did not establish its real WCHECK cursor.");
    events.prepare_wait_state(id,{"Atck02",1,-4,7});
    OriginalPads pads;OriginalPads::Samples samples{};samples[0]={true,{},{},pad_button::b};pads.update(samples);
    PlayerWaitServices supplied;supplied.pads=&pads;unsigned selected{},resumed{};
    supplied.dispatch_sequence=[&](std::string_view name) {
        ++selected;check(name=="SQ_WAIT","Atck02 dispatched a different outer sequence.");
        return events.select_sequence(id,name).complete() && pass_complete(events.run_action_sequence(id,sources,200,services));
    };
    supplied.resume_commands=[&] {
        ++resumed;check(events.sequence(id).current_sequence()=="SQ_WAIT" && events.sequence(id).command_index()==2 &&
            !events.sequence(id).wait_state().name,"Atck02 query failed to clear the newly installed live destination wait.");
        return pass_complete(events.run_action_sequence(id,sources,300,services));
    };
    check(events.update_wait(id,supplied).complete() && selected==1 && resumed==1 && action->current_action()=="tail" &&
        events.sequence(id).current_sequence()=="SQ_WAIT" && events.sequence(id).command_index()==3 &&
        events.sequence(id).wait_state()==ModelWaitState{std::nullopt,1,-4,7},
        "Atck02 restored its old command cursor, skipped the post-dispatch real query or reset attack fields.");
}
void failed_bridge_preflight() {
    Fixture fixture;const auto binding=fixture.binding();auto sources=fixture.library(binding);auto action=fixture.runtime(binding,sources);
    ActionEventRuntime events;const auto id=events.register_model(binding,model(),true);events.register_action(id,action);ActionServices services;
    rejects([&]{events.run_action_sequence(id,sources,10,services);},"Unselected MODELCTRL work entered the live ACTION bridge.");
    check(events.sequence(id).current_sequence().empty() && !events.last_action_result(id),"Unselected bridge attempt retained a phantom invocation.");
    check(events.select_sequence(id,"SQ_BOOT").complete() && events.run_action_sequence(id,sources,20,services).stop==ModelSequenceStop::end,
        "Unselected bridge rejection poisoned later preparation and execution.");
    ActionServices missing;
    check(action->dispatch(sources,missing,"tail",30).stop==ActionStop::broadcast_required,"Fixture failed to retain an older unrelated ACTION event.");
    events.select_sequence(id,"SQ_FLOW");
    rejects([&]{events.run_action_sequence(id,sources,40,services);},"An older unrelated ACTION broadcast was mistaken for the new model command.");
    check(events.sequence(id).command_index()==0 && action->current_action()=="tail","Bridge preflight consumed the new command before old delivery completed.");
    auto delivery=events.action_services(id);
    check(action->resume(sources,delivery,30).stop==ActionStop::end,"Explicit old ACTION delivery failed to complete.");
    check(events.run_action_sequence(id,sources,50,services).stop==ModelSequenceStop::waiting &&
        action->current_action()=="frame" && events.sequence(id).command_index()==2,
        "Rejected older broadcast left a poisoned invocation or skipped the new ACTION dispatch.");
}
void callback_owned_messenger_continuations() {
    for(const bool outer_sequence:{true,false}) {
        Fixture fixture;const auto binding=fixture.binding();auto sources=fixture.library(binding);auto action=fixture.runtime(binding,sources);
        ActionEventRuntime events;const auto id=events.register_model(binding,model(),true);events.register_action(id,action);ActionServices services;
        events.select_sequence(id,"SQ_BOOT");events.run_action_sequence(id,sources,10,services);
        events.prepare_wait_state(id,{outer_sequence ? "Atck02" : "Moving",0,5,-2});
        const ControllerObjectId prefix{50,1},tail{50,2};
        events.messenger().register_recipient(prefix,"EXTERNAL","target",0);
        events.messenger().register_recipient(tail,"EXTERNAL","target",0);
        unsigned prefix_calls{},tail_calls{},dispatch_calls{},command_calls{};bool ready=false;
        MessengerServices external;external.manager=[&](ControllerObjectId recipient) {
            return MessengerManager{[&,recipient](NativeMessenger&,const MessengerDelivery& delivery) {
                check(delivery.message.command=="DO_WORK","Custom wait service lost its retained command.");
                if(recipient==prefix) {++prefix_calls;return MessengerCallResult{true,1,{}};}
                ++tail_calls;return MessengerCallResult{ready,1,{}};
            },{}};
        };events.set_external_services(external);
        std::optional<MessengerDispatch> pending;
        const auto deliver=[&] {
            if(!pending)pending.emplace(events.messenger().begin_send_to("EXTERNAL","target",0,"DO_WORK"));
            return events.resume(*pending).stop==MessengerStop::completed;
        };
        PlayerWaitServices supplied;
        supplied.dispatch_sequence=[&](std::string_view name) {
            ++dispatch_calls;check(name=="SQ_WAIT","Atck02 changed its requested outer sequence.");return deliver();
        };
        supplied.resume_commands=[&] {
            ++command_calls;if(!outer_sequence && !deliver())return false;
            return pass_complete(events.run_action_sequence(id,sources,20,services));
        };
        const auto expected=outer_sequence ? ModelWaitStop::sequence_required : ModelWaitStop::commands_required;
        check(events.update_wait(id,supplied).stop==expected && events.wait_pending(id) && prefix_calls==1 && tail_calls==1,
            "Custom callback-owned send failed to retain its explicit wait boundary.");
        rejects([&]{events.messenger();},"A pending callback-owned wait exposed the shared message buffer.");
        ready=true;
        check(events.resume_wait(id,supplied).complete() && !events.wait_pending(id) && prefix_calls==1 && tail_calls==2 &&
            command_calls==(outer_sequence ? 1u : 2u) && dispatch_calls==(outer_sequence ? 2u : 0u),
            "Wait resume blocked its own custom send, repeated the completed recipient prefix or skipped the continuation.");
        check(events.sequence(id).wait_state()==ModelWaitState{std::nullopt,0,5,-2},"Callback-owned send restored a stale wait or reset attack fields.");
    }
}
void suspended_action_state_identity() {
    for(unsigned mutation=0;mutation<4;++mutation) {
        Fixture fixture;const auto binding=fixture.binding();auto sources=fixture.library(binding);auto action=fixture.runtime(binding,sources);
        ActionEventRuntime events;const auto id=events.register_model(binding,model(),true);events.register_action(id,action);ActionServices services;
        events.select_sequence(id,"SQ_RANDOM");
        check(events.run_action_sequence(id,sources,100,services).stop==ModelSequenceStop::action_required && action->command_index()==2,
            "State identity fixture failed to retain the partial real ACTION.");
        if(mutation==0) {
            // An exact copy contains the same retained command state, including
            // its opaque identity. Restoring that exact value is still safe.
            const auto exact=*action;*action=exact;services.random_word=[] {return 0u;};
            check(events.run_action_sequence(id,sources,900,services).stop==ModelSequenceStop::waiting &&
                action->timers().at("WAIT").saved_world_time==100,"An exact suspended-state value copy was mistaken for unrelated work.");
        } else {
            if(mutation==1)*action=*fixture.runtime(binding,sources);
            else {
                auto external=events.action_services(id);
                action->dispatch(sources,external,mutation==2 ? "tail" : "random",200);
            }
            const auto current=std::string(action->current_action());const auto cursor=action->command_index();
            services.random_word=[] {return 0u;};
            rejects([&]{events.run_action_sequence(id,sources,900,services);},"Replaced or externally dispatched live ACTION work resumed a stale model continuation.");
            check(events.sequence(id).command_index()==1 && action->current_action()==current && action->command_index()==cursor,
                "Rejected ACTION identity change mutated either the external work or retained model cursor.");
        }
    }
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);const std::string source="_common/p_agumon.igb";
    const auto doc=read_igb_document(resources.read(source));const auto skin=decode_igb_skinning(doc,5665);
    std::vector<AnimationClip> clips;for(const auto& info:list_igb_animations(doc,skin))clips.push_back(decode_igb_animation(doc,skin,info.reference_index));
    AnimationSourceRuntime sources(source,std::move(clips));sources.initialize_action_offset();ActionEventRuntime events;
    std::array<ControllerObjectId,4> ids;std::array<std::shared_ptr<ActionRuntime>,4> actions;
    const std::array<const char*,4> labels{"dual","slash","arm","gun"};ActionServices services;services.random_word=[] {return 0u;};
    OriginalPads pads;OriginalPads::Samples samples{};
    for(std::uint32_t i=0;i<4;++i) {
        auto binding=decode_player_runtime_binding(doc,{1386,546,i,1},source,7);
        actions[i]=std::make_shared<ActionRuntime>(decode_action_actor_runtime(doc,binding.action_program(),sources,static_cast<std::int32_t>(i)));
        ids[i]=events.register_model(binding,model(),true);events.register_action(ids[i],actions[i]);events.set_label(ids[i],"SQ_ATCK1",labels[i]);
        samples[i]={true,{},{},i%2==0 ? pad_button::a : pad_button::b};
    }
    // Recipient sets are captured by the original messenger cache. Register
    // all four loaded owners before selecting/sending the first query.
    for(std::uint32_t i=0;i<4;++i) {
        events.select_sequence(ids[i],"SQ_ATCK1");
        const auto result=events.run_sequence(ids[i],1000000000);
        check(result.stop==ModelSequenceStop::unsupported_command && result.type=="beModelCtrlHITBOX" && !actions[i]->active(),
            "Original attack sequence bypassed its unimplemented HITBOX controller boundary.");
        // Explicit prepared attack work only. The initial actual sequence
        // above cannot execute fully until its HITBOX owner is recovered.
        services.try_broadcast=events.action_services(ids[i]).try_broadcast;
        check(actions[i]->dispatch(sources,services,"KATCA3",1000000000).stop==ActionStop::waiting && actions[i]->work().wait_state==2,
            "Prepared original KATCA3 failed to start its actual motion wait.");
        events.prepare_wait_state(ids[i],{"Action",1,0,0});
        check(events.query_action_complete(ids[i])==actions[i]->query_command_end(),"Original player query did not read its corresponding live ACTION RAM.");
    }
    pads.update(samples);std::size_t passes{};
    for(std::uint32_t i=0;i<4;++i) {
        PlayerWaitServices supplied;supplied.pads=&pads;
        const auto result=events.update_wait(ids[i],supplied);
        check(result.complete() && events.sequence(ids[i]).wait_state().name=="Action" && actions[i]->query_command_end()==false,
            "Original attack wait silently resumed unsupported MODELCTRL commands or invented ACTION completion.");++passes;
        check(actions[i]->actors()->ram_reference()==events.binding(ids[i]).action_ram_reference(),"Original wait processing changed its actor RAM owner.");
    }
    std::cout<<"Original prepared player waits: players=4 HITBOX_boundaries=4 actual_KATCA3_starts=4 wait_passes="<<passes<<"\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        live_ownership_and_query_prefix();retained_action_services_and_time();waits_query_and_independent_pads();attack_two_live_destination();
        failed_bridge_preflight();callback_owned_messenger_continuations();
        suspended_action_state_identity();
        if(args.size()==2)original(args[1]);else if(args.size()!=1)throw std::runtime_error("Usage: dw4_player_waits_tests [EXTRACTED_ROOT]");
        std::cout<<"player_waits: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"player_waits after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);
}
