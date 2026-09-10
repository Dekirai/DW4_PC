#include "action_fixture.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/resources.hpp"

#include <filesystem>
#include <iostream>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* reason) { ++checks; if(!value) throw std::runtime_error(reason); }
void resumable_action() {
    action_fixture::Fixture fixture;
    fixture.action("RUN",{fixture.command(animation_fixture::action_motion,"Synthetic clip")});
    auto sources=fixture.library(); auto runtime=fixture.runtime(sources);
    ActionServices services;
    int attempts{},legacy{}; bool available=false;
    services.broadcast=[&](const auto&) { ++legacy; };
    services.try_broadcast=[&](const ActionBroadcast& event) {
        ++attempts; check(event.action=="RUN" && event.ram_no==0,"Pending delivery retains its exact ACTION identity");
        return available;
    };
    auto result=runtime.dispatch(sources,services,"RUN",1000000000);
    check(result.stop==ActionStop::broadcast_required && runtime.command_end(),"Resumable event suspends after the committed command prefix");
    const auto cursor=runtime.command_index(); const auto states=runtime.combiner().states().size();
    result=runtime.resume(sources,services,1000000000);
    check(result.stop==ActionStop::broadcast_required && runtime.command_index()==cursor && runtime.combiner().states().size()==states,
        "Missing recipient service cannot replay motion commands");
    available=true; result=runtime.resume(sources,services,1000000000);
    check(result.stop==ActionStop::end && attempts==3 && legacy==0,"Resumable callback owns delivery and takes precedence over legacy callback");
    runtime.resume(sources,services,1000000000);
    runtime.dispatch(sources,services,"RUN",1000000000);
    check(attempts==3,"Completed delivery and same-action suppression emit no duplicate event");
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto document=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto archive=assets::AfsArchive::open(root/"files"/"ndmw.afs");
    const auto sound_document=read_igb_document(archive.read(178));
    const auto sound=decode_action_event_tables(sound_document,232,"ndmw.afs/178",178);
    const auto starter_document=read_igb_document(archive.read(179));
    std::vector<ActionEventTableProgram> starters;
    for(const auto reference:starter_document.object_list(*starter_document.info_list_reference)) {
        if(reference && starter_document.metadata.object_types.at(starter_document.object(*reference).type_index).name=="beActionStarterInfo")
            starters.push_back(decode_action_event_tables(starter_document,*reference,"ndmw.afs/179",179));
    }
    check(starters.size()==58,"Original loaded map exposes all 58 Starter consumers");
    const auto skin=decode_igb_skinning(document,5665);
    std::vector<AnimationClip> clips;
    for(const auto& info:list_igb_animations(document,skin)) clips.push_back(decode_igb_animation(document,skin,info.reference_index));
    AnimationSourceRuntime sources("_common/p_agumon.igb",std::move(clips)); sources.initialize_action_offset();
    ActionEventRuntime events;
    std::vector<ActionRuntime> actions;
    std::vector<ControllerObjectId> ids;
    std::vector<ActionServices> action_services;
    const std::array<std::string,4> labels{"dual","slash","arm","gun"};
    const std::array<std::string,4> runs{"RUNND1","RUNNS1","RUNNA1","RUNNG1"};
    std::size_t registration_count=4;
    for(std::uint32_t i=0;i<4;++i) {
        auto binding=decode_player_runtime_binding(document,{1386,546,i,1},"_common/p_agumon.igb",7);
        actions.push_back(decode_action_actor_runtime(document,binding.action_program(),sources,static_cast<std::int32_t>(i)));
        ModelMotionPose pose({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}); set_player_turn_rates(pose);
        ids.push_back(events.register_model(std::move(binding),ModelControlState(pose),true));
        action_services.push_back(events.action_services(ids.back()));
        events.set_label(ids.back(),"SQ_WAIT",labels[i]); events.set_label(ids.back(),"SQ_WALK",labels[i]);
    }
    events.register_consumer(sound,true); registration_count+=sound.rams().size();
    for(const auto& starter:starters) { events.register_consumer(starter,true); registration_count+=starter.rams().size(); }
    check(events.messenger().registration_count()==registration_count,"All loaded consumer RAM occurrences enter the same ordered messenger");
    std::size_t pose_count{},vertex_count{},sound_starts{},sound_cleans{};
    for(std::size_t i=0;i<4;++i) {
        check(events.select_sequence(ids[i],"SQ_WALK").complete(),"Prepared sequence entry starts after empty queue cleanup");
        events.run_sequence(ids[i],1000000000,[&](const ModelActionRequest& request) {
            const auto result=actions[i].dispatch(sources,action_services[i],request,1000000000);
            check(result.stop==ActionStop::end || result.stop==ActionStop::waiting,"Real sequence ACTION synchronously delivers native consumers");
        });
        check(actions[i].current_action()==runs[i],"Weapon label selects its own original running action");
        const auto& entries=events.subcommands(ids[i]).entries();
        check(entries.size()==sound.rams().size(),"Only this original RAM receives its delayed footsteps");
        for(const auto& entry:entries) {
            const auto* value=std::get_if<ModelSoundSubcommandState>(&entry.value);
            check(value && value->name=="foot_lp1" && entry.delay==6 && entry.elapsed==0 && !entry.started,
                "Original Se row appends actual foot_lp1 with a six-frame delay");
        }
        for(std::size_t other=i+1;other<4;++other)
            check(events.subcommands(ids[other]).entries().empty(),"An unstarted player cannot receive another RAM's footstep queue");
        for(std::uint64_t frame=1;frame<=6;++frame) {
            const auto time=1000000000+frame*16666667;
            actions[i].update(sources,action_services[i],time);
            actions[i].traverse_actor(sources,events.binding(ids[i]).selected_actor_index(),
                AnimationMatrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},
                AnimationMatrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},time);
            const auto cache=capture_actor_skin(events.binding(ids[i]).skin_binding(),*actions[i].actors(),actions[i].combiner());
            const auto pose=evaluate_actor_skin(events.binding(ids[i]).skin_binding(),cache);
            ++pose_count; vertex_count+=pose.positions.size();
            const auto result=events.update_subcommands(ids[i],original_system_frame_delta(time,time-16666667));
            check(frame<6 ? result.complete() : result.stop==ModelSubcommandStop::sound_play_required,
                "Original delayed queue reaches an explicit audio-service boundary at frame six");
        }
        ModelSubcommandServices services;
        services.play_sound=[&](const std::optional<std::string>& name) {
            check(name=="foot_lp1","Audio boundary receives the real sound key");
            ++sound_starts; return ModelSoundPlayResult{true,static_cast<std::uint64_t>(i+1),4};
        };
        check(events.resume_subcommands(ids[i],services).complete(),"An explicitly supplied test audio manager completes the retained start");
        const auto starts=sound_starts;
        check(events.update_subcommands(ids[i],10,services).complete() && starts==sound_starts,
            "Started type-4 sound remains owned without replay after an uncapped frame delta");
        check(events.select_sequence(ids[i],"SQ_WAIT").stop==ModelSubcommandStop::sound_volume_down_required &&
            events.sequence(ids[i]).current_sequence()=="SQ_WAIT" && events.sequence(ids[i]).selection_pending(),
            "Sequence interruption exposes WAIT but requires sound cleanup before its commands");
        services.sound_volume_down=[&](std::uint64_t handle) {
            check(handle==i+1,"Each player's cleanup retains its own 64-bit sound handle"); ++sound_cleans; return true;
        };
        check(events.resume_sequence(ids[i],services).complete() && events.subcommands(ids[i]).entries().empty() &&
            events.sequence(ids[i]).current_sequence()=="SQ_WAIT" && !events.sequence(ids[i]).selection_pending(),
            "Successful cleanup releases the whole list before WAIT commands can execute");
        events.run_sequence(ids[i],1200000000,[&](const ModelActionRequest& request) {
            const auto result=actions[i].dispatch(sources,action_services[i],request,1200000000);
            check(result.stop==ActionStop::end || result.stop==ActionStop::waiting,"Original idle ACTION completes its consumer delivery");
        });
        check(events.subcommands(ids[i]).entries().empty(),"Idle sequence does not recreate the running sound");
    }
    check(sound_starts==4*sound.rams().size() && sound_cleans==sound_starts,"Four independent prepared player queues own matching starts and cleanup calls");
    const auto attack=actions[0].dispatch(sources,action_services[0],"KATCA3",1300000000);
    check(attack.stop==ActionStop::waiting && actions[0].work().wait_state==2,
        "Original KATCA3 starts katcA03 and yields at its motion wait");
    const auto& attack_entries=events.subcommands(ids[0]).entries();
    check(attack_entries.size()==2,"The actual loaded KATCA3 consumers append one sound then one effect");
    check(std::get<ModelSoundSubcommandState>(attack_entries[0].value).name=="PC_SUBURI02" && attack_entries[0].delay==0,
        "Original Se action row422 emits its exact immediate attack sound");
    const auto& effect=std::get<ModelEffectSubcommandState>(attack_entries[1].value).command;
    check(effect.model_name=="x_100armk01" && effect.sequence_name=="SQ_ATCK01" && effect.delay==0 &&
        effect.offset==std::array<float,3>{} && effect.rotation==std::array<float,3>{} && effect.sequence_dead && effect.position_lock,
        "Actual Starter8942 row8941 produces its original effect name, sequence, placement and lifetime flags");
    for(std::size_t i=1;i<4;++i) check(events.subcommands(ids[i]).entries().empty(),"Attack consumers do not append to another player RAM");
    ModelSubcommandServices attack_services;
    unsigned attack_sounds{},spaces{},generated{},erased{};
    attack_services.play_sound=[&](const std::optional<std::string>& name) {
        check(name=="PC_SUBURI02","Attack audio boundary receives the actual sound key");
        ++attack_sounds; return ModelSoundPlayResult{true,0,0}; // Explicit test manager: no retained sound handle.
    };
    check(events.update_subcommands(ids[0],0,attack_services).stop==ModelSubcommandStop::effect_space_required && attack_sounds==1,
        "Immediate sound completion precedes the missing original effect-space service");
    attack_services.find_effect_space=[&](const std::optional<std::string>& name) {
        check(name=="x_100armk01","FIND_SPACE retains the actual effect-info key");
        ++spaces; return ModelEffectSpaceResult{true,31};
    };
    check(events.resume_subcommands(ids[0],attack_services).stop==ModelSubcommandStop::effect_generate_required && spaces==1,
        "Effect selection is retained when the generation manager is missing");
    attack_services.generate_effect=[&](const ModelEffectGenerateRequest& request) {
        check(request.model_name=="x_100armk01" && request.sequence_name=="SQ_ATCK01" && request.ram_no==31 &&
            request.source==ModelSubcommandOwner{"p_agumon",0} && request.attachment==request.source && request.position_lock &&
            request.offset==std::array<float,3>{} && request.rotation==std::array<float,3>{},
            "Original effect GENERATE retains the owning and attachment RAM context");
        ++generated; return true; // Explicit test manager boundary; no rendered effect actor.
    };
    check(events.resume_subcommands(ids[0],attack_services).complete() && generated==1 && spaces==1 && attack_sounds==1 &&
        events.subcommands(ids[0]).entries().size()==1,"Resuming generation cannot replay sound or allocate a second effect RAM");
    check(events.select_sequence(ids[0],"SQ_WAIT").stop==ModelSubcommandStop::effect_erase_required,
        "Original sequence-dead attack effect requires cleanup on interruption");
    attack_services.erase_effect=[&](const std::optional<std::string>& name,std::int32_t ram) {
        check(name=="x_100armk01" && ram==31,"Effect cleanup retains its selected RAM and exact name"); ++erased; return true;
    };
    check(events.resume_sequence(ids[0],attack_services).complete() && erased==1 && events.subcommands(ids[0]).entries().empty(),
        "Attack effect cleanup completes before new sequence commands can execute");
    std::cout<<"Original player event integration: registrations="<<registration_count<<" players=4 poses="<<pose_count
        <<" vertices="<<vertex_count<<" sound_service_starts="<<sound_starts<<" sound_service_cleanups="<<sound_cleans
        <<" attack_sounds="<<attack_sounds<<" effects_generated="<<generated<<" effects_erased="<<erased<<"\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        resumable_action();
        if(args.size()==2) original(args[1]);
        else if(args.size()!=1) throw std::runtime_error("Usage: dw4_player_events_tests [EXTRACTED_ROOT]");
        std::cout<<"player_events: "<<checks<<" checks passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"player_events: "<<error.what()<<'\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for(int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
