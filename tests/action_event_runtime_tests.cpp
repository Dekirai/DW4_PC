#include "dw4/action_event_runtime.hpp"
#include "action_event_tables_fixture.hpp"
#include "player_runtime_binding_fixture.hpp"
#include "action_fixture.hpp"

#include <iostream>
#include <type_traits>
#include <utility>

namespace {
using namespace dw4;
namespace ef=action_event_fixture;
namespace pf=player_runtime_fixture;
int checks{};
constexpr ControllerObjectId probe{90,123};
constexpr const char* player_name="Synthetic player";
void check(bool condition,const char* reason) { ++checks; if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    bool caught=false; try { operation(); } catch (const std::exception&) { caught=true; } check(caught,reason);
}
ModelControlState prepared_model() { return ModelControlState(ModelMotionPose({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1})); }
struct Fixture {
    pf::RuntimeFixture player;
    ef::Fixture events;
    std::uint32_t wait_sequence{},walk_sequence{};
    std::vector<std::pair<std::uint32_t,std::string>> command_types;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> lua_modes;
    Fixture() {
        for (const auto index:{0u,2u}) {
            events.text(events.se_rows[index],2,player_name);
            events.text(events.starter_rows[index],2,player_name);
        }
        const auto walk=player.add(pf::sequence_data_type); player.text(walk,2,"SQ_WALK");
        player.scalar(walk,3,player.list(pf::named_list,{}));
        const auto doc=player.document();
        const auto sequences=*doc.field_reference(doc.object(player.sequence),4);
        std::vector<std::uint32_t> rows;
        for (const auto ref:doc.object_list(sequences)) rows.push_back(*ref);
        wait_sequence=rows[0]; walk_sequence=walk;
        rows.push_back(walk); player.contents(sequences,rows);
    }
    PlayerRuntimeBinding binding(std::uint32_t instance=0,std::size_t occurrence=3) const {
        auto doc=player.document();
        for (const auto& [ref,type]:command_types) {
            auto& object=doc.objects[doc.references[ref].value_index];
            auto descriptor=doc.metadata.object_types[object.type_index]; descriptor.name=type; descriptor.parent_index=pf::object;
            object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size()); doc.metadata.object_types.push_back(std::move(descriptor));
        }
        for(const auto& [ref,storage]:lua_modes) {
            auto& object=doc.objects.at(doc.references.at(ref).value_index);
            const auto& memory=doc.memories.at(doc.references.at(storage).value_index);
            object.fields.push_back({3,0,memory.offset,memory.size});
        }
        return decode_player_runtime_binding(doc,player.runtime_request(instance),"synthetic/player.igb",occurrence);
    }
    ActionEventTableProgram sounds() const { return decode_action_event_tables(events.document(),events.se,"synthetic/sounds.igb",7); }
    ActionEventTableProgram effects() const { return decode_action_event_tables(events.document(),events.starter,"synthetic/effects.igb",8); }
    ActionBroadcast event(std::int32_t instance=0,std::string action="RUN") const {
        return {"synthetic/player.igb",player_name,std::move(action),instance};
    }
    void one_consumer_ram() { events.contents(events.rams,{events.ram_refs[0]}); }
    void one_sound_and_effect() {
        one_consumer_ram();
        events.contents(events.se_last_actions,{events.sounds[2]}); events.scalar(events.sounds[2],4,0);
        events.contents(events.starter_first_actions,{events.effects[1]}); events.scalar(events.effects[1],4,0);
    }
    std::uint32_t command(std::string type,std::string name) {
        const auto ref=player.add(pf::unknown_node); player.text(ref,2,name);
        command_types.emplace_back(ref,std::move(type)); return ref;
    }
    std::uint32_t lua(std::string prefix,std::optional<std::string> mode) {
        const auto ref=command("beModelCtrlLUACALL",std::move(prefix));
        auto bytes=pf::words({mode ? static_cast<std::uint32_t>(mode->size()+1) : 0});
        if(mode)pf::name(bytes,*mode);
        lua_modes.emplace_back(ref,player.memory(std::move(bytes)));return ref;
    }
};
struct Delivered {
    std::string command;
    std::uint8_t enabled{};
    std::int32_t index{};
    std::vector<std::optional<std::string>> strings;
    std::vector<float> floats;
    std::vector<std::array<float,3>> vectors;
    std::vector<std::uint8_t> booleans;
};
Delivered snapshot(NativeMessenger& messenger,const MessengerDelivery& delivery) {
    const auto& buffer=messenger.buffer();
    return {*delivery.message.command,buffer.enabled_only(),delivery.message.index,
        buffer.strings(),buffer.floats(),buffer.vectors(),buffer.booleans()};
}
void cross_source_and_duplicates() {
    static_assert(!std::is_copy_constructible_v<ActionEventRuntime> && !std::is_move_constructible_v<ActionEventRuntime>);
    Fixture f; ActionEventRuntime runtime;
    const auto first=runtime.register_model(f.binding(),prepared_model(),true);
    const auto repeated=runtime.register_model(f.binding(0,4),prepared_model(),true);
    const auto second=runtime.register_model(f.binding(1),prepared_model(),true);
    std::vector<Delivered> deliveries;
    runtime.messenger().register_recipient(probe,"MODELCTRL",player_name,-1);
    MessengerServices external;
    external.active=[](auto id) { check(id==probe,"External activity only resolves unowned recipients"); return std::uint8_t{1}; };
    external.manager=[&](auto id) {
        check(id==probe,"External manager retains its qualified recipient");
        return MessengerManager{[&](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            deliveries.push_back(snapshot(messenger,delivery)); return MessengerCallResult{true,1,{}};
        },{}};
    };
    runtime.set_external_services(external);
    runtime.register_consumer(f.sounds(),true); runtime.register_consumer(f.effects(),true);
    check(runtime.messenger().registration_count()==10,"Registration retains three models, one external recipient and six consumer occurrences");
    check(runtime.broadcast(first,f.event()).stop==MessengerStop::completed,"Cross-source consumer delivery completes its real queue appends");
    const auto& queue=runtime.subcommands(first).entries();
    check(queue.size()==24 && runtime.subcommands(repeated).entries().size()==24 && runtime.subcommands(second).entries().empty(),
        "Names and RAM numbers route across sources without confusing a different instance");
    check(deliveries.size()==24,"Every duplicate consumer RAM and matching action produces a distinct nested delivery");
    for (std::size_t i=0;i<queue.size();++i) {
        check(queue[i].id==i && queue[i].elapsed==0 && !queue[i].started,"Delivery appends ordered fresh work without pretending to execute it");
        const auto& message=deliveries[i];
        check(message.index==0,"Consumer sends retain the triggering RAM number");
        if (i<12) {
            const auto& sound=std::get<ModelSoundSubcommandState>(queue[i].value);
            check(message.command=="SETSOUND" && message.enabled==0 && message.strings.size()==1 && message.vectors.empty() &&
                sound.name==message.strings[0] && queue[i].delay==message.floats[0],"Se sends its exact nullable sound and leaves enabled-only reset");
            if (i%4==0) check(!sound.name,"Null sound survives table, messenger and queue");
            if (i%4==1) check(sound.name=="","Empty sound remains distinct from null through delivery");
        } else {
            const auto& effect=std::get<ModelEffectSubcommandState>(queue[i].value).command;
            const auto row=(i-12)%4;
            check(message.command=="SETEFFECT" && message.enabled==1 && message.strings.size()==2 &&
                message.vectors.size()==(row==1 ? 2u : 1u) && message.booleans==std::vector<std::uint8_t>{1,static_cast<std::uint8_t>(row==1)},
                "Starter preserves enabled-only, flags and exact Data2 vector arity");
            check(effect.model_name=="" && effect.offset==std::array<float,3>{1,2,3} && effect.sequence_dead &&
                effect.position_lock==(row==1) && effect.rotation==(row==1 ? std::array<float,3>{4,5,6} : std::array<float,3>{}),
                "MODELCTRL appends the translated effect payload with original base rotation fallback");
            if (row==0 || row==3) check(!effect.sequence_name,"Null effect sequence payload survives dispatch");
            if (row==1) check(effect.sequence_name=="","Empty effect sequence payload survives dispatch");
        }
    }
    check(runtime.select_sequence(first,"SQ_WAIT").complete() && runtime.subcommands(first).entries().empty() &&
        runtime.subcommands(repeated).entries().size()==24,"Unstarted cleanup removes only its owning model's work");
    const auto before=runtime.messenger().registration_count();
    rejects([&]{runtime.register_model(f.binding(),prepared_model(),true);},"Duplicate qualified model owner rejects");
    check(runtime.messenger().registration_count()==before,"Rejected duplicate owners do not append registration rows");
    auto foreign=f.event(); foreign.source="synthetic/alien.igb";
    rejects([&]{runtime.broadcast(first,foreign);},"Emitter source is checked independently of cross-file destination matching");
    rejects([&]{runtime.broadcast(first,f.event(1));},"Emitter RAM cannot be silently redirected");
    rejects([&]{runtime.set_active({99,999},true);},"Unowned activity cannot be invented");
}
void inactive_targets() {
    Fixture f; f.one_consumer_ram(); ActionEventRuntime runtime;
    const auto model=runtime.register_model(f.binding(),prepared_model(),false);
    runtime.register_consumer(f.sounds(),true); runtime.register_consumer(f.effects(),true);
    unsigned deliveries=0,activity_checks=0;
    runtime.messenger().register_recipient(probe,"MODELCTRL",player_name,0);
    runtime.set_external_services({[&](auto id) { check(id==probe,"Inactive external probe has its own RAM"); ++activity_checks; return std::uint8_t{0}; },
        [&](auto) { return MessengerManager{[&](NativeMessenger& messenger,const MessengerDelivery& message) {
            check(message.message.command=="SETSOUND" && messenger.buffer().enabled_only()==0,"Only Se reaches an inactive external target");
            ++deliveries; return MessengerCallResult{true,1,{}};
        },{}}; }});
    check(runtime.broadcast(model,f.event()).stop==MessengerStop::completed && runtime.subcommands(model).entries().empty(),
        "Inactive MODELCTRL handles Se without append and is skipped for Starter");
    check(deliveries==4 && activity_checks==4,"Starter's enabled-only flag controls filtering while Se preserves ordinary delivery");
    runtime.set_active(model,true);
    check(runtime.broadcast(model,f.event()).stop==MessengerStop::completed && runtime.subcommands(model).entries().size()==8,
        "Explicit model activation allows the next event's real sound/effect appends");
}
void nested_resume_and_snapshot() {
    Fixture f; f.one_consumer_ram();
    const auto control=f.command("beModelCtrlCTRL","toWalk"),action=f.command("beModelCtrlACTION","RUN");
    f.player.scalar(f.wait_sequence,3,f.player.list(pf::named_list,{control,action}));
    ActionEventRuntime runtime;
    const auto model=runtime.register_model(f.binding(),prepared_model(),true);
    const auto second=runtime.register_model(f.binding(1),prepared_model(),true);
    runtime.select_sequence(model,"SQ_WAIT"); runtime.select_sequence(second,"SQ_WAIT");
    runtime.register_consumer(f.sounds(),true);
    runtime.messenger().register_recipient(probe,"MODELCTRL",player_name,0);
    bool ready=false,changed=false; unsigned attempts=0;
    std::vector<std::optional<std::string>> completed;
    runtime.set_external_services({[](auto) { return std::uint8_t{1}; },[&](auto) {
        return MessengerManager{[&](NativeMessenger& messenger,const MessengerDelivery&) {
            ++attempts;
            if (!ready) return MessengerCallResult{false,0,"Original external MODELCTRL service remains unavailable"};
            completed.push_back(messenger.buffer().strings()[0]);
            if (!changed) {
                changed=true;
                // Mutate the actual outer cache record while this consumer is
                // in a nested send. Its remaining rows must use its saved keys.
                messenger.select("ACTION_BROADCAST",player_name,1);
                auto changed_message=messenger.begin_send("CHANGED");
                (void)changed_message;
            }
            return MessengerCallResult{true,1,{}};
        },{}};
    }});
    auto services=runtime.action_services(model);
    const ModelActionDispatch dispatch=[&](const ModelActionRequest& request) {
        if(!services.try_broadcast({request.source,request.info_name,request.action,request.ram_no}))
            throw std::runtime_error("Retain the sequence ACTION cursor while its real delivery is unavailable");
    };
    rejects([&]{runtime.run_sequence(model,0,dispatch);},"Sequence ACTION retains a suspended native delivery");
    check(runtime.subcommands(model).entries().size()==1 && runtime.sequence(model).model().mode==2 && runtime.sequence(model).command_index()==1,
        "Missing nested service suspends after the first real model append");
    rejects([&]{runtime.run_sequence(model,1,dispatch);},"Owning sequence may retry its pending ACTION delivery");
    check(runtime.subcommands(model).entries().size()==1 && attempts==2 && runtime.sequence(model).command_index()==1,
        "Retry keeps the committed append and retries only the unfinished external recipient");
    rejects([&]{runtime.broadcast(model,f.event(0,"OTHER"));},"A new ACTION cannot replace a suspended event");
    rejects([&]{runtime.update_subcommands(model,1);},"Pending delivery cannot be advanced as a completed frame");
    rejects([&]{runtime.select_sequence(model,"SQ_WAIT");},"Sequence cleanup cannot discard a suspended broadcast prefix");
    rejects([&]{runtime.broadcast(second,f.event(1));},"Another player cannot overwrite shared messenger state during suspended delivery");
    rejects([&]{runtime.messenger();},"Top-level mutable messenger access cannot overwrite a suspended message");
    rejects([&]{runtime.run_sequence(second,1);},"Another player's sequence must stop before applying CTRL during suspended delivery");
    check(runtime.sequence(second).model().mode==0 && runtime.sequence(second).command_index()==0,
        "Rejected second-player sequence execution preserves its control fields and command cursor");
    ready=true;
    check(runtime.run_sequence(model,2,dispatch).stop==ModelSequenceStop::end && runtime.subcommands(model).entries().size()==4 && runtime.subcommands(second).entries().empty(),
        "Resume completes remaining matches against the original captured RAM despite nested cache mutation");
    check(completed==std::vector<std::optional<std::string>>{std::nullopt,std::string{},std::string{"foot_lp1"},std::string{"foot_lp1"}} && attempts==6,
        "External completion and model queues each observe every ordered match exactly once");
    for (std::size_t i=0;i<4;++i) check(runtime.subcommands(model).entries()[i].id==i,"Suspension does not consume extra model work IDs");
    check(runtime.select_sequence(model,"SQ_WAIT").complete(),"Completed delivery releases the model's pending-event barrier");
}
void source_occurrence_consistency() {
    Fixture f; ActionEventRuntime runtime;
    runtime.register_model(f.binding(),prepared_model(),true);
    runtime.register_consumer(f.sounds(),true);
    const auto before=runtime.messenger().registration_count();
    const auto player_doc=f.player.document(),event_doc=f.events.document();
    const auto alien_model=decode_player_runtime_binding(player_doc,f.player.runtime_request(1),"synthetic/alien-player.igb",3);
    rejects([&]{runtime.register_model(alien_model,prepared_model(),true);},"A new model RAM cannot reinterpret an existing source occurrence as another path");
    const auto alien_consumer=decode_action_event_tables(event_doc,f.events.starter,"synthetic/alien-events.igb",7);
    rejects([&]{runtime.register_consumer(alien_consumer,true);},"Another consumer class cannot reuse one loaded occurrence for a different file");
    const auto same_kind=decode_action_event_tables(event_doc,f.events.se,"synthetic/renamed-sounds.igb",7);
    rejects([&]{runtime.register_consumer(same_kind,true);},"Same-kind RAM re-registration does not permit changing its source path");
    check(runtime.messenger().registration_count()==before,"Rejected source collisions publish no registrations");
    const auto second=runtime.register_model(f.binding(1),prepared_model(),true);
    check(runtime.binding(second).source_occurrence()==3 && runtime.binding(second).source()=="synthetic/player.igb",
        "A consistent same-source instance remains registerable after rejected collisions");
    const auto separate=decode_action_event_tables(event_doc,f.events.starter,"synthetic/alien-events.igb",9);
    runtime.register_consumer(separate,true);
    check(runtime.messenger().registration_count()==before+4,"A different explicitly loaded occurrence owns its separate source and RAM registrations");
}
void atomic_registration_limit() {
    Fixture f; ActionEventRuntime runtime;
    runtime.register_model(f.binding(),prepared_model(),true);
    std::vector<std::uint32_t> rams;
    for (std::size_t i=0;i<4096;++i) rams.push_back(f.events.add(ef::ram_type));
    f.events.contents(f.events.rams,rams);
    const auto oversized=decode_action_event_tables(f.events.document(),f.events.se,"synthetic/over-limit.igb",12);
    const auto count=runtime.messenger().registration_count(),bytes=runtime.messenger().registration_text_bytes();
    rejects([&]{runtime.register_consumer(oversized,true);},"A valid 4096-RAM table cannot exceed the owner limit after a model already owns one RAM");
    check(runtime.messenger().registration_count()==count && runtime.messenger().registration_text_bytes()==bytes,
        "Aggregate owner rejection occurs before any registration or text-budget mutation");
    rejects([&]{runtime.set_active({12,rams.front()},false);},"An over-limit registration cannot leave the early consumer owner prefix installed");
    f.events.contents(f.events.rams,{rams.front()});
    const auto recovered=decode_action_event_tables(f.events.document(),f.events.se,"synthetic/recovered.igb",12);
    runtime.register_consumer(recovered,true);
    check(runtime.messenger().registration_count()==count+1,
        "Over-limit rejection does not reserve the source occurrence or prevent a later bounded registration");
}
void sequence_cleanup_before_selection() {
    Fixture f; f.one_sound_and_effect(); ActionEventRuntime runtime;
    const ModelSubcommandOwner attachment{"attached model",11};
    const auto model=runtime.register_model(f.binding(),prepared_model(),true,attachment);
    runtime.register_consumer(f.sounds(),true); runtime.register_consumer(f.effects(),true);
    check(runtime.select_sequence(model,"SQ_WAIT").complete() && runtime.sequence(model).current_sequence()=="SQ_WAIT",
        "Initial prepared sequence selection completes with an empty queue");
    check(runtime.broadcast(model,f.event()).stop==MessengerStop::completed && runtime.subcommands(model).entries().size()==2,
        "Event appends one sound and one sequence-dead effect into the same ordered owner");
    unsigned plays=0,finds=0,generates=0,downs=0,erases=0;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto& name) { ++plays; check(name=="foot_lp1","Playback receives the actual translated sound"); return ModelSoundPlayResult{true,101,4}; };
    services.find_effect_space=[&](const auto& name) { ++finds; check(name=="","Null Starter info became the original empty effect key"); return ModelEffectSpaceResult{true,23}; };
    services.generate_effect=[&](const ModelEffectGenerateRequest& request) {
        ++generates;
        check(request.source==ModelSubcommandOwner{player_name,0} && request.attachment==attachment && request.ram_no==23 &&
            request.sequence_name=="" && request.rotation==std::array<float,3>{4,5,6} && request.position_lock,
            "Generated effect retains model owner, explicit attachment and Data2 payload");
        return true;
    };
    check(runtime.update_subcommands(model,0,services).complete() && plays==1 && finds==1 && generates==1,
        "The separate subcommand update executes each registered service exactly once");
    rejects([&]{runtime.select_sequence(model,"MISSING",services);},"A nonexistent sequence rejects before external cleanup");
    check(runtime.sequence(model).current_sequence()=="SQ_WAIT" && runtime.subcommands(model).entries().size()==2 && downs==0,
        "Invalid selection keeps the existing sequence and started work");
    services.sound_volume_down=[&](std::uint64_t handle) { ++downs; check(handle==101,"Cleanup owns the started sound handle"); return true; };
    const auto pending=runtime.select_sequence(model,"SQ_WALK",services);
    check(pending.stop==ModelSubcommandStop::effect_erase_required && downs==1 && erases==0 &&
        runtime.sequence(model).current_sequence()=="SQ_WALK" && runtime.sequence(model).selection_pending() &&
        runtime.subcommands(model).entries().size()==2,
        "New selection is visible while ordered cleanup retains its completed sound prefix");
    rejects([&]{runtime.run_sequence(model,1);},"No new sequence command may run before required cleanup completes");
    rejects([&]{runtime.set_label(model,{},"slash");},"Pending selection cannot receive unrelated label mutations");
    rejects([&]{runtime.broadcast(model,f.event());},"Pending sequence cleanup blocks a new ACTION append");
    check(runtime.resume_sequence(model,services).stop==ModelSubcommandStop::effect_erase_required && downs==1,
        "Missing cleanup retries do not repeat successful volume-down");
    services.erase_effect=[&](const auto& name,std::int32_t ram) { ++erases; check(name=="" && ram==23,"Cleanup targets the generated effect RAM"); return true; };
    check(runtime.resume_sequence(model,services).complete() && downs==1 && erases==1 &&
        runtime.subcommands(model).entries().empty() && runtime.sequence(model).current_sequence()=="SQ_WALK",
        "Cleanup releases the whole queue and finishes the already visible selection");
    check(runtime.run_sequence(model,2).stop==ModelSequenceStop::end && plays==1 && generates==1,
        "Completed selection does not replay prior playback or generation");
}
void local_jump_cleanup() {
    Fixture f; f.one_sound_and_effect();
    const auto jump=f.command("beModelCtrlJUMP","SQ_WALK");
    const auto arrived=f.command("beModelCtrlACTION","ARRIVED");
    f.player.scalar(f.wait_sequence,3,f.player.list(pf::named_list,{jump}));
    f.player.scalar(f.walk_sequence,3,f.player.list(pf::named_list,{arrived}));
    ActionEventRuntime runtime;
    const auto model=runtime.register_model(f.binding(),prepared_model(),true);
    runtime.register_consumer(f.sounds(),true);
    runtime.select_sequence(model,"SQ_WAIT");
    check(runtime.broadcast(model,f.event()).stop==MessengerStop::completed,"Local-jump test owns a real queued sound");
    unsigned plays=0,downs=0,actions=0;
    ModelSubcommandServices services;
    services.play_sound=[&](const auto&) { ++plays; return ModelSoundPlayResult{true,501,4}; };
    runtime.update_subcommands(model,0,services);
    const ModelActionDispatch execute=[&](const ModelActionRequest& request) {
        ++actions;
        check(request.command_reference==arrived && request.action=="ARRIVED","Only the post-cleanup target ACTION executes");
        check(runtime.broadcast(model,{request.source,request.info_name,request.action,request.ram_no}).stop==MessengerStop::completed,
            "Post-jump ACTION executes its real empty-match broadcast");
    };
    const auto pending=runtime.run_sequence(model,100,execute,services);
    check(pending.stop==ModelSequenceStop::sequence_cleanup_required && pending.command_reference==jump &&
        runtime.sequence(model).current_sequence()=="SQ_WALK" && runtime.sequence(model).command_index()==0 &&
        runtime.sequence(model).jump_pending() && actions==0,
        "An ordinary local JUMP publishes its target but cannot run commands before sound cleanup");
    check(runtime.run_sequence(model,200,execute,services).stop==ModelSequenceStop::sequence_cleanup_required && plays==1 && actions==0,
        "Retry retains pending jump metadata without replaying playback or executing destination commands");
    rejects([&]{runtime.resume_subcommands(model,services);},"Local jump cleanup must resume through its owning sequence operation");
    services.sound_volume_down=[&](std::uint64_t handle) {
        ++downs; check(handle==501 && runtime.sequence(model).current_sequence()=="SQ_WALK" && runtime.sequence(model).selection_pending(),
            "Jump cleanup sees the published destination and actual sound handle"); return true;
    };
    check(runtime.run_sequence(model,300,execute,services).stop==ModelSequenceStop::end && downs==1 && actions==1 &&
        runtime.sequence(model).current_sequence()=="SQ_WALK" && runtime.subcommands(model).entries().empty(),
        "Completed jump cleanup permits destination commands exactly once");
}
void visible_selection_phases() {
    Fixture f; f.one_sound_and_effect();
    auto state=prepared_model(); state.mode=7; state.phase=8; state.check_fall=false;
    state.pad_motion.set_values({1,2,3,4});
    state.channels.apply({1,"beModelCtrlMOVEEX","X",0,MotionSetup{MotionMode::accelerate_positive,4,2,0},MotionStop{true,1,2}});
    state.channels.apply({2,"beModelCtrlMOVEEX","R",3,MotionSetup{MotionMode::accelerate_positive,3,1,0},{}});
    state.channels.advance(1);
    const auto before=state.channels.states(); auto stopped=state.channels; stopped.on_sequence_change();
    ActionEventRuntime runtime; const auto id=runtime.register_model(f.binding(),state,true);
    const auto pad=runtime.sequence(id).model().pad_motion.list_owner();const auto backing=pad->backing_owner();
    const auto movement=runtime.sequence(id).model().channels.list_owner();const auto first_channel=movement->entry_owner(0);
    const auto wrapper=runtime.subcommands(id).wrapper_owner();const auto list=runtime.subcommands(id).list_owner();
    runtime.register_consumer(f.sounds(),true);
    runtime.broadcast(id,f.event());
    ModelSubcommandServices services;
    services.play_sound=[](const auto&) { return ModelSoundPlayResult{true,123,4}; };
    runtime.update_subcommands(id,0,services);
    bool ready=false; unsigned cleanup_calls{};
    services.sound_volume_down=[&](std::uint64_t handle) {
        ++cleanup_calls; const auto& sequence=runtime.sequence(id); const auto& model=sequence.model();
        check(handle==123 && sequence.current_sequence()=="SQ_WALK" && sequence.command_index()==0 && sequence.selection_pending(),
            "External cleanup sees the original pre-cleanup sequence and cursor publication");
        check(model.mode==0 && model.phase==0 && model.pad_motion.size()==0 && !model.check_fall && model.channels.states()==before,
            "CTRL/PDMOVE reset precedes cleanup while fall/channel changes follow it");
        check(model.pad_motion.list_owner()==pad && pad->backing_owner()==backing && *backing==std::vector<float>({1,2,3,4}) &&
            model.channels.list_owner()==movement && movement->entry_owner(0)==first_channel &&
            runtime.subcommands(id).wrapper_owner()==wrapper && runtime.subcommands(id).list_owner()==list,
            "Paused cleanup observes the actual retained model and subcommand list owners");
        return ready;
    };
    check(runtime.select_sequence(id,"SQ_WALK",services).stop==ModelSubcommandStop::sound_volume_down_required && cleanup_calls==1,
        "A suspended cleanup retains the visible intermediate selection phase");
    ready=true;
    check(runtime.resume_sequence(id,services).complete() && cleanup_calls==2 && !runtime.sequence(id).selection_pending() &&
        runtime.sequence(id).model().check_fall && runtime.sequence(id).model().channels.states()==stopped.states(),
        "Only completed cleanup applies the original fall and channel-stop effects");
    check(movement->entry_owner(0)==first_channel && first_channel->mode==MotionMode::stop_positive && list->empty() &&
        pad->backing_owner()==backing && runtime.subcommands(id).list_owner()==list,
        "Completed selection preserves list allocations and mutates the retained original movement entry");

    ModelSequenceRuntime direct(f.binding().sequence_program(),state,player_name,0);
    direct.begin_select("SQ_WALK");
    const auto& visible=std::as_const(direct);
    check(visible.current_sequence()=="SQ_WALK" && visible.model().channels.states()==before,"Direct phase API preserves the same intermediate state");
    rejects([&]{direct.model();},"Mutable model access cannot change an unfinished selection");
    rejects([&]{direct.run(1);},"External selection must finish before direct command execution");
    rejects([&]{direct.select("SQ_WAIT");},"Another selection cannot replace unfinished cleanup");
    rejects([&]{direct.set_label({},"dual");},"Label mutation cannot invalidate an unfinished selection");
    direct.finish_select();
    check(direct.run(1).stop==ModelSequenceStop::end && !direct.selection_pending(),"Explicit finish permits ordinary commands");
    rejects([&]{direct.finish_select();},"Finishing an already completed selection is rejected");

    const auto jump=f.command("beModelCtrlJUMP","SQ_WALK");
    f.player.scalar(f.wait_sequence,3,f.player.list(pf::named_list,{jump}));
    ModelSequenceRuntime jumping(f.binding().sequence_program(),prepared_model(),player_name,0);
    jumping.select("SQ_WAIT");
    check(jumping.run(1,{},[](auto){return false;}).stop==ModelSequenceStop::sequence_cleanup_required &&
        jumping.current_sequence()=="SQ_WALK" && jumping.jump_pending(),"Local jump retains a required cleanup continuation");
    check(jumping.run(2).stop==ModelSequenceStop::sequence_cleanup_required && jumping.jump_pending(),
        "Omitting the callback cannot bypass already required jump cleanup");
    const auto completed=jumping.run(3,{},[](auto){return true;});
    check(completed.stop==ModelSequenceStop::end && !jumping.jump_pending() && !jumping.selection_pending(),
        "Resumed jump finishes its selection before entering destination commands");
}
void repeated_consumer_owners() {
    Fixture f; f.events.contents(f.events.starter_first_actions,{f.events.effects[1]});
    ActionEventRuntime runtime;
    const auto model=runtime.register_model(f.binding(),prepared_model(),true);
    auto doc=f.events.document();
    doc.references[f.events.ram_refs[1]].value_index=doc.references[f.events.ram_refs[0]].value_index;
    const auto original=decode_action_event_tables(doc,f.events.starter,"synthetic/effects.igb",8);
    runtime.register_consumer(original,true);
    check(runtime.messenger().registration_count()==4,"Aliased raw RAM references retain every registration occurrence");
    runtime.set_active({8,f.events.ram_refs[0]},false);
    runtime.messenger().select("ACTION_BROADCAST",player_name,0); runtime.messenger().buffer().set_enabled_only(1);
    auto inactive=runtime.messenger().begin_send("RUN");
    check(runtime.resume(inactive).stop==MessengerStop::completed && runtime.subcommands(model).entries().empty(),
        "One physical activity change applies to every alias registration row");
    runtime.set_active({8,f.events.ram_refs[0]},true);
    check(runtime.broadcast(model,f.event()).stop==MessengerStop::completed && runtime.subcommands(model).entries().size()==3,
        "All three alias rows invoke the shared consumer owner when active");
    runtime.select_sequence(model,"SQ_WAIT");
    const auto next_info=f.events.add(ef::starter_info); f.events.records[next_info]=f.events.records[f.events.starter];
    f.events.text(next_info,2,"replacement effect"); f.events.contents(f.events.info_reference,{f.events.se,f.events.starter,next_info});
    auto replacement_doc=f.events.document();
    replacement_doc.references[f.events.ram_refs[1]].value_index=replacement_doc.references[f.events.ram_refs[0]].value_index;
    const auto replacement=decode_action_event_tables(replacement_doc,next_info,"synthetic/effects.igb",8);
    runtime.register_consumer(replacement,true);
    check(runtime.messenger().registration_count()==7,"Repeated same-kind export rebinds its shared RAM and appends all new rows");
    check(runtime.broadcast(model,f.event()).stop==MessengerStop::completed && runtime.subcommands(model).entries().size()==3,
        "Warm original recipient cache preserves old row count after later registration");
    for (const auto& entry:runtime.subcommands(model).entries())
        check(std::get<ModelEffectSubcommandState>(entry.value).command.model_name=="replacement effect",
            "Cached recipient rows consult the RAM's newly assigned parent consumer program");
    const auto before=runtime.messenger().registration_count();
    const auto different_kind=decode_action_event_tables(replacement_doc,f.events.se,"synthetic/effects.igb",8);
    rejects([&]{runtime.register_consumer(different_kind,true);},"A physical RAM cannot silently change consumer manager kinds");
    check(runtime.messenger().registration_count()==before,"Rejected cross-kind owner changes do not append registration rows");
}
void immediate_lua_owner_and_nested_guards() {
    Fixture f;f.one_sound_and_effect();
    const auto lua=f.lua("script",std::nullopt),tail=f.command("beModelCtrlCTRL","toWalk");
    f.player.scalar(f.wait_sequence,3,f.player.list(pf::named_list,{lua,tail}));
    ActionEventRuntime runtime;const auto id=runtime.register_model(f.binding(),prepared_model(),true);
    const auto other=runtime.register_model(f.binding(1),prepared_model(),true);
    runtime.select_sequence(id,"SQ_WAIT");runtime.select_sequence(other,"SQ_WALK");
    runtime.register_consumer(f.sounds(),true);runtime.broadcast(id,f.event());
    check(runtime.subcommands(id).entries().size()==1,"Lua ownership fixture requires one unstarted sound.");
    action_fixture::Fixture animation;auto sources=animation.library();ActionServices action;
    unsigned calls{},effects{},plays{};
    const ModelLuaDispatch dispatch=[&](const ModelLuaRequest& request) {
        ++calls;
        check(request.source=="synthetic/player.igb" && request.command_reference==lua && request.info_name==player_name &&
            request.ram_no==0 && request.script_prefix=="script" && !request.event,
            "Immediate Lua retry must retain its exact command identity and nullable event.");
        const auto nested_reject=[&](auto operation,std::string_view expected) {
            std::string diagnostic;
            try {operation();}catch(const std::exception& error) {diagnostic=error.what();}
            check(diagnostic.find(expected)!=std::string::npos,"Nested sequence entry did not reject at its wrapper preflight.");
        };
        nested_reject([&]{runtime.run_sequence(other,44);},"Nested prepared sequence");
        nested_reject([&]{runtime.run_action_sequence(other,sources,44,action);},"Nested prepared ACTION sequence");
        check(runtime.sequence(other).command_index()==0 && runtime.sequence(other).current_sequence()=="SQ_WALK" &&
            !runtime.sequence(other).selection_pending() && !runtime.last_action_result(other),
            "Rejected nested entry latched or changed the other model's sequence/action work.");
        if(calls==1) {++effects;return false;}
        return true;
    };
    auto result=runtime.run_sequence(id,17,{},{},{},dispatch);
    check(result.stop==ModelSequenceStop::lua_required && result.command_reference==lua && runtime.sequence(id).lua_pending() &&
        runtime.sequence(id).command_index()==0 && runtime.sequence(id).model().mode==0,
        "Incomplete immediate Lua must retain its cursor and stop before the next command.");
    ModelSubcommandServices services;services.play_sound=[&](const auto&) {++plays;return ModelSoundPlayResult{true,0,0};};
    rejects([&]{runtime.update_subcommands(id,3,services);},"A pending Lua call allowed a later model frame.");
    rejects([&]{runtime.select_sequence(id,"SQ_WALK");},"Selection discarded an invoked Lua continuation without a messenger handle.");
    check(plays==0 && runtime.subcommands(id).entries().size()==1 && runtime.subcommands(id).entries()[0].elapsed==0 &&
        runtime.sequence(id).command_index()==0 && runtime.sequence(id).current_sequence()=="SQ_WAIT" &&
        !runtime.sequence(id).selection_pending(),"Rejected Lua interruption changed queue timing or published sequence entry.");
    result=runtime.run_sequence(id,17,{},{},{},dispatch);
    check(result.stop==ModelSequenceStop::end && calls==2 && effects==1 && !runtime.sequence(id).lua_pending() &&
        runtime.sequence(id).model().mode==2,"Lua retry replayed a completed prefix or failed to execute its later command.");
    check(runtime.run_sequence(other,44).stop==ModelSequenceStop::end &&
        runtime.select_sequence(other,"SQ_WAIT").complete(),"Nested preflight left a hidden sequence-time or selection latch.");
    check(runtime.update_subcommands(id,3,services).complete() && plays==1 && runtime.subcommands(id).entries().empty(),
        "Completed Lua failed to release its frame-processing boundary.");
}
void immediate_lua_fault() {
    Fixture f;f.one_sound_and_effect();const auto lua=f.lua("uncertain",std::string{});
    f.player.scalar(f.wait_sequence,3,f.player.list(pf::named_list,{lua}));
    ActionEventRuntime runtime;const auto id=runtime.register_model(f.binding(),prepared_model(),true);
    runtime.select_sequence(id,"SQ_WAIT");runtime.register_consumer(f.sounds(),true);runtime.broadcast(id,f.event());
    unsigned calls{},plays{};
    const ModelLuaDispatch dispatch=[&](const ModelLuaRequest& request)->bool {
        ++calls;check(request.event==std::optional<std::string>{""},"Empty Lua event was collapsed to null.");
        throw std::runtime_error("Script may already have committed its effect.");
    };
    rejects([&]{runtime.run_sequence(id,1,{},{},{},dispatch);},"Throwing Lua callback did not report failure.");
    rejects([&]{runtime.run_sequence(id,1,{},{},{},dispatch);},"Faulted Lua was replayed.");
    rejects([&]{runtime.select_sequence(id,"SQ_WALK");},"Selection discarded uncertain Lua ownership.");
    ModelSubcommandServices services;services.play_sound=[&](const auto&){++plays;return ModelSoundPlayResult{true,0,0};};
    rejects([&]{runtime.update_subcommands(id,1,services);},"Uncertain Lua allowed another frame's manager effects.");
    check(calls==1 && runtime.sequence(id).command_index()==0 && runtime.sequence(id).current_sequence()=="SQ_WAIT" &&
        !runtime.sequence(id).selection_pending() && runtime.sequence(id).lua_faulted() && plays==0 &&
        runtime.subcommands(id).entries().size()==1 && runtime.subcommands(id).entries()[0].elapsed==0,
        "Uncertain Lua service was replayed or changed the selected command/queued frame.");
}
void immediate_lua_owned_dispatch() {
    Fixture f;const auto lua=f.lua("nested_effect","Step"),tail=f.command("beModelCtrlCTRL","toWalk");
    f.player.scalar(f.wait_sequence,3,f.player.list(pf::named_list,{lua,tail}));
    ActionEventRuntime runtime;const auto id=runtime.register_model(f.binding(),prepared_model(),true);
    const auto other=runtime.register_model(f.binding(1),prepared_model(),true);
    runtime.select_sequence(id,"SQ_WAIT");runtime.select_sequence(other,"SQ_WALK");
    const ControllerObjectId first{91,1},second{92,2};
    runtime.register_recipient(first,"LUA_EFFECT",std::nullopt,-1);runtime.register_recipient(second,"LUA_EFFECT",std::nullopt,-1);
    unsigned first_calls{},second_calls{},second_effects{},lua_calls{};std::optional<MessengerDispatch> pending;
    runtime.set_external_services({{},[&](ControllerObjectId recipient)->MessengerManager {
        return {[&,recipient](NativeMessenger& messenger,const MessengerDelivery& delivery) {
            check(delivery.message.command=="APPLY" && messenger.buffer().integers()==std::vector<std::int32_t>{73},
                "Immediate Lua nested delivery lost its captured message payload.");
            if(recipient==first) {++first_calls;return MessengerCallResult{true,1,{}};}
            ++second_calls;if(second_calls==1)return MessengerCallResult{false,0,"Awaiting script effect."};
            ++second_effects;return MessengerCallResult{true,1,{}};
        },{}};
    }});
    const ModelLuaDispatch dispatch=[&](const ModelLuaRequest& request) {
        ++lua_calls;check(request.command_reference==lua && request.event=="Step","Nested Lua retry changed its original command.");
        if(!pending) {
            auto& messenger=runtime.messenger();messenger.select("LUA_EFFECT",std::nullopt,-1);
            messenger.buffer().add_int(73);pending.emplace(messenger.begin_send("APPLY"));
        }
        const auto result=runtime.resume(*pending);
        if(result.stop!=MessengerStop::completed)return false;
        pending.reset();return true;
    };
    check(runtime.run_sequence(id,18,{},{},{},dispatch).stop==ModelSequenceStop::lua_required &&
        first_calls==1 && second_calls==1 && pending,"Immediate Lua did not retain its partial synchronous delivery.");
    rejects([&]{runtime.messenger();},"Suspended Lua delivery exposed mutable shared arguments.");
    rejects([&]{runtime.run_sequence(other,19);},"Another sequence bypassed a suspended Lua-owned delivery.");
    check(runtime.run_sequence(id,18,{},{},{},dispatch).stop==ModelSequenceStop::end && lua_calls==2 &&
        first_calls==1 && second_calls==2 && second_effects==1 && !pending && runtime.sequence(id).model().mode==2,
        "Owning Lua retry replayed a completed recipient or could not finish its retained dispatch.");
    check(runtime.run_sequence(id,20,{},{},{},dispatch).stop==ModelSequenceStop::end && lua_calls==2 &&
        runtime.run_sequence(other,19).stop==ModelSequenceStop::end,"Completed Lua retained a stale dispatch/sequence ownership gate.");
}
}

int main() {
    try {
        cross_source_and_duplicates(); inactive_targets(); nested_resume_and_snapshot(); sequence_cleanup_before_selection();
        local_jump_cleanup(); visible_selection_phases(); repeated_consumer_owners(); source_occurrence_consistency(); atomic_registration_limit();
        immediate_lua_owner_and_nested_guards();immediate_lua_fault();immediate_lua_owned_dispatch();
        std::cout<<checks<<" action event runtime checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Action event runtime failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
