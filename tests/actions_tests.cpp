#include "action_fixture.hpp"
#include "dw4/assets.hpp"
#include "dw4/model_sequence.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace animation_fixture;
using Fixture=action_fixture::Fixture;
int checks{};
constexpr std::uint64_t tick=262144, start=4*tick;
void check(bool value,const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    bool caught=false; try { operation(); } catch (const std::runtime_error&) { caught=true; }
    check(caught,reason);
}
struct Services : ActionServices {
    std::vector<ActionBroadcast> events;
    Services() { broadcast=[this](const auto& value){ events.push_back(value); }; }
};
void dispatch_and_waits() {
    Fixture f;
    const auto empty=f.action("empty",{});
    const auto motion=f.command(action_motion,"Synthetic clip"); f.action("idle",{motion});
    f.action("wait",{f.command(action_wait,"Frame"),f.command(action_fade,"HIDE")});
    f.action("motion",{motion,f.command(action_wait,"Motion"),f.command(action_flash,"Damage")});
    f.action("unknown-wait",{f.command(action_wait,"Other"),f.command(action_fade,"HIDE")});
    f.actions.push_back(empty); f.contents(f.actions_list,f.actions);
    auto sources=f.library(); Services service; auto runtime=f.runtime(sources);
    check(!runtime.active() && !runtime.visible() && !runtime.command_end(),"Prepared action is not already running");
    check(runtime.update(sources,service,start).stop==ActionStop::end,"Inactive update has no pending execution");
    rejects([&]{runtime.dispatch(sources,service,"IDLE",start);},"Action lookup preserves case");
    check(!runtime.active() && service.events.empty(),"Missing action cannot activate a prepared actor");
    auto result=runtime.dispatch(sources,service,"empty",start);
    check(result.stop==ActionStop::end && runtime.active() && runtime.visible() && runtime.command_end(),"Empty action activates and ends");
    check(runtime.combiner().states().size()==1 && runtime.combiner().states()[0].time_requested==100000000 &&
        runtime.work().motion.no_interpolation,"First activation uses the original request clock");
    runtime.dispatch(sources,service,"empty",start);
    check(service.events.size()==1 && !runtime.work().motion.no_interpolation,"Same identity does not restart but clears first-activation interpolation flag");
    runtime.dispatch(sources,service,"wait",start);
    check(runtime.command_index()==1 && runtime.work().wait_state==1 && !runtime.command_end(),"Wait consumes its command before yielding");
    check(runtime.update(sources,service,start).commands_executed==0 && runtime.visible(),"Zero-frame wait uses strict expiry");
    result=runtime.update(sources,service,start+tick);
    check(result.commands_executed==1 && runtime.command_end() && !runtime.visible() && runtime.active(),"Wait resumes into hide without deactivating");
    check(service.events.size()==2 && runtime.timers().contains("WAIT"),"Wait resume does not broadcast or remove its timer");
    runtime.dispatch(sources,service,"wait",start+tick);
    check(runtime.visible() && runtime.command_end() && service.events.size()==2,"Same-action dispatch restores switch visibility without restarting commands");
    runtime.dispatch(sources,service,"unknown-wait",start+tick);
    check(runtime.command_index()==1 && runtime.work().wait_state==0 && !runtime.command_end(),"Unrecognized wait character yields without a wait condition");
    check(runtime.update(sources,service,start+2*tick).commands_executed==0 && runtime.visible(),"Yield without a condition does not invent automatic resume");

    auto motion_runtime=f.runtime(sources);
    motion_runtime.dispatch(sources,service,"motion",start); motion_runtime.pose(sources,f.data,start);
    check(motion_runtime.work().wait_state==2 && motion_runtime.combiner().states().size()==1,"First motion enters its explicit duration wait");
    motion_runtime.update(sources,service,start);
    check(motion_runtime.work().wait_state==2,"Motion wait cannot finish before the duration");
    motion_runtime.update(sources,service,start+200000000);
    check(motion_runtime.command_end() && motion_runtime.work().flash_state==1,"Motion duration resumes following commands");
    check(service.events.back().source==action_fixture::source_name && service.events.back().info_name=="actor" &&
        service.events.back().ram_no==0 && service.events.back().action=="motion","Broadcast retains original recipient and source qualification");
    auto alien=f.library();
    rejects([&]{motion_runtime.dispatch(alien,service,"motion",start);},"Same names cannot bypass animation-library ownership");
    rejects([&]{motion_runtime.update(alien,service,start);},"Non-motion updates also validate library ownership");
    ModelActionRequest request{action_fixture::source_name,1,"actor",0,"empty"};
    motion_runtime.dispatch(sources,service,request,start);
    request.ram_no=1; rejects([&]{motion_runtime.dispatch(sources,service,request,start);},"Sequence request cannot select another RAM");
    request.ram_no=0; request.info_name="other"; rejects([&]{motion_runtime.dispatch(sources,service,request,start);},"Sequence request cannot select another info");
    request.info_name="actor"; request.source="other"; rejects([&]{motion_runtime.dispatch(sources,service,request,start);},"Sequence request cannot select another source");
}
void joint_and_freeze() {
    Fixture f;
    f.action("clamp",{f.command(action_joint,"span",0,0,"CL"),f.command(action_wait,"Joint"),f.command(action_fade,"HIDE")});
    f.action("loop",{f.command(action_joint,"span",0,2),f.command(action_wait,"Joint")});
    f.action("stagnant",{f.command(action_joint,"span",1,1),f.command(action_wait,"Joint")});
    f.action("frame",{f.command(action_wait,"Frame",10)});
    auto sources=f.library(); Services service; auto runtime=f.runtime(sources);
    runtime.dispatch(sources,service,"clamp",start);
    runtime.update(sources,service,start);
    check(runtime.node_time(start)==0 && !runtime.work().joint_ended && runtime.work().wait_state==3,"Joint endpoint equality is not completion");
    runtime.update(sources,service,start+tick);
    check(runtime.node_time(start+tick)==0 && runtime.work().joint_ended && runtime.command_end() && !runtime.visible(),"Clamped joint ends strictly after endpoint and resumes its wait");
    auto loop=f.runtime(sources); loop.dispatch(sources,service,"loop",start);
    const auto before=loop.node_time(start+400*tick); loop.update(sources,service,start+400*tick);
    check(loop.node_time(start+400*tick)==before%33333334 && loop.work().joint_state==1 && loop.work().joint_ended,
        "Joint loop retains phase and state through multiple wraps");
    auto stagnant=f.runtime(sources); stagnant.dispatch(sources,service,"stagnant",start);
    const auto bias=stagnant.node_clock().time_bias;
    rejects([&]{stagnant.update(sources,service,start+tick);},"Non-progressing original joint loop has a bounded failure");
    check(!stagnant.work().joint_ended && stagnant.node_clock().time_bias==bias,"Failed joint loop does not publish a partial clock");

    auto frozen=f.runtime(sources); frozen.dispatch(sources,service,"frame",start);
    frozen.freeze(true,start+tick); const auto saved_node=frozen.node_time(start+tick);
    const auto saved_timer=motion_timer_time(frozen.timers().at("WAIT"),start+tick);
    check(frozen.work().frozen && frozen.node_time(start+1000*tick)==saved_node &&
        motion_timer_time(frozen.timers().at("WAIT"),start+1000*tick)==saved_timer,"Freeze preserves node and timer phase");
    check(frozen.update(sources,service,start+1000*tick).commands_executed==0 && frozen.work().wait_state==1,"Frozen update cannot expire waits");
    frozen.freeze(false,start+1000*tick);
    check(!frozen.work().frozen && frozen.node_clock().time_scale==1 && frozen.node_time(start+1000*tick)==saved_node &&
        frozen.timers().at("WAIT").cached_frames==0,"Unfreeze resumes at scale one and clears elapsed-frame cache");
    frozen.update(sources,service,start+2000*tick); check(frozen.command_end(),"Unfrozen timer completes using resumed phase");
    MotionTimer timer; timer.time_scale=2; start_motion_timer(timer,start); motion_timer_frames(timer,start+tick);
    const auto phase=motion_timer_time(timer,start+2*tick), previous=timer.previous_time;
    const auto timestamp=timer.saved_world_time; change_motion_timer_speed(timer,start+2*tick,-1);
    check(motion_timer_time(timer,start+2*tick)==phase && timer.previous_time==previous && timer.saved_world_time==timestamp && timer.cached_frames==0,
        "Timer speed change preserves phase and original saved timestamps");
    const auto same=timer; change_motion_timer_speed(timer,start+999*tick,-1); check(timer==same,"Equal timer speed is a complete no-op");
    rejects([&]{change_motion_timer_speed(timer,start,std::numeric_limits<float>::infinity());},"Reject invalid timer speed");
    auto edge=f;
    edge.scalar(edge.nodes[0],13,std::bit_cast<std::uint32_t>(-1.0f));
    auto edge_runtime=edge.runtime(sources);
    check(edge_runtime.node_time(std::numeric_limits<std::uint64_t>::max())==std::bit_cast<std::uint64_t>(std::int64_t{-1125899906842624}),
        "Uncached node time cannot inherit the animation state cache's initial sentinel");
}
void appearance_and_order() {
    Fixture f;
    f.action("in",{f.command(action_fade,"IN",4,1)});
    f.action("out",{f.command(action_fade,"OUT",4,0.5f)});
    f.action("flash",{f.command(action_flash,"Damage")});
    f.action("combined",{f.command(action_flash,"Damage"),f.command(action_fade,"IN",1,1)});
    f.action("zero",{f.command(action_fade,"OUT",0,1)});
    f.action("nan",{f.command(action_fade,"IN",0,0)});
    auto sources=f.library(); Services service; auto runtime=f.runtime(sources);
    runtime.dispatch(sources,service,"in",start);
    check(runtime.command_end() && runtime.work().fade_state==1 && runtime.appearance().color->packed==0x00ffffff &&
        runtime.appearance().blend==true && runtime.appearance().lighting==false,"Fade starts independently of command completion");
    runtime.update(sources,service,start);
    check(runtime.appearance().color->channels[3]==0.25f && runtime.appearance().color->packed==0x40ffffff,"Fade uses rounded ABGR8 while keeping raw alpha");
    runtime.update(sources,service,start); runtime.update(sources,service,start); runtime.update(sources,service,start);
    check(runtime.work().fade_state==3 && !runtime.appearance().color && !runtime.appearance().blend && !runtime.appearance().lighting &&
        !runtime.timers().contains("FADE"),"Completed fade-in removes its unshared attributes");
    runtime.dispatch(sources,service,"out",start);
    check(runtime.appearance().color->packed==0x80ffffff,"Fade-out starts from authored alpha");
    runtime.dispatch(sources,service,"flash",start);
    check(runtime.appearance().color->channels[3]==128.0f/255 && runtime.appearance().texture==false,"Flash reads quantized color and preserves alpha");
    for (int i=0;i<4;++i) runtime.update(sources,service,start);
    check(runtime.work().fade_state==3 && runtime.appearance().color->packed==0x00ffffff && runtime.appearance().blend==true,
        "Fade-out completion retains color and blend");
    runtime.update(sources,service,start+300*tick);
    check(runtime.work().flash_state==0 && !runtime.appearance().texture && runtime.appearance().color && runtime.appearance().lighting==false,
        "Flash expiry preserves attributes still owned by blend presence");
    auto combined=f.runtime(sources); combined.dispatch(sources,service,"combined",start); combined.update(sources,service,start);
    check(combined.work().fade_state==3 && combined.appearance().color && combined.appearance().texture==false && !combined.appearance().blend,
        "Fade-in retains color when a false texture attribute is present");
    combined.update(sources,service,start+300*tick);
    check(!combined.appearance().color && !combined.appearance().texture && !combined.appearance().lighting,"Later flash expiry removes the remaining shared attributes");
    auto zero=f.runtime(sources); zero.dispatch(sources,service,"zero",start); zero.update(sources,service,start);
    check(zero.work().fade_state==3 && zero.work().fade_value==0,"Zero-frame fade-out preserves its usable infinity-to-zero path");
    zero.dispatch(sources,service,"nan",start);
    const auto color=zero.appearance().color->packed;
    rejects([&]{zero.update(sources,service,start);},"NaN fade reaches an explicit unusable-color boundary");
    check(zero.appearance().color->packed==color && zero.work().fade_value==0,"Invalid fade arithmetic preserves the last valid color");
}
void service_boundaries() {
    Fixture f;
    f.action("random",{f.command(action_wait,"RanSet",2),f.command(action_wait,"Random",3),f.command(action_flash,"Damage")});
    f.action("empty",{}); f.action("bad",{f.command(action_fade,"HIDE"),f.add(object),f.command(action_flash,"Damage")});
    f.action("negative",{f.command(action_wait,"RanSet",3),f.command(action_wait,"Random",2)});
    f.action("zero",{f.command(action_wait,"RanSet",2),f.command(action_wait,"Random",2)});
    auto sources=f.library(); ActionServices service; auto runtime=f.runtime(sources);
    auto result=runtime.dispatch(sources,service,"random",start);
    check(result.stop==ActionStop::random_required && result.commands_executed==1 && runtime.command_index()==1 && service.random_lower==2,
        "Missing random service preserves the successful command prefix");
    check(runtime.update(sources,service,start).stop==ActionStop::random_required && runtime.dispatch(sources,service,"random",start).stop==ActionStop::random_required,
        "Suspended execution remains explicit without restarting on the same action");
    int draws=0; service.random_word=[&]{++draws; return 150u;};
    result=runtime.resume(sources,service,start);
    check(result.stop==ActionStop::broadcast_required && runtime.work().wait_state==1 && runtime.command_index()==2 && draws==1,
        "Explicit retry starts a random wait once then requires its broadcast");
    check(runtime.timers().at("WAIT").end_time==41666668,"Random wait uses unsigned remainder and original float frame conversion");
    int events=0; service.broadcast=[&](const auto& event){++events; check(event.action=="random","Deferred event keeps the original action");};
    result=runtime.resume(sources,service,start);
    check(result.stop==ActionStop::waiting && events==1 && draws==1,"Broadcast retry neither consumes RNG again nor advances a wait");
    runtime.update(sources,service,start+300*tick);
    check(runtime.work().flash_state==1 && runtime.command_end() && events==1,"Wait continuation has no duplicate action broadcast");
    service.broadcast={};
    result=runtime.dispatch(sources,service,"bad",start);
    check(result.stop==ActionStop::unsupported_command && runtime.command_index()==1 && !runtime.visible(),"Unsupported command preserves ordered effects and unconsumed cursor");
    check(runtime.resume(sources,service,start).stop==ActionStop::unsupported_command,"Unsupported command cannot be skipped by retry");
    runtime.dispatch(sources,service,"empty",start);
    service.broadcast=[&](const auto&){runtime.freeze(true,start);};
    rejects([&]{runtime.resume(sources,service,start);},"Action callback cannot reenter runtime mutation");
    check(!runtime.work().frozen,"Rejected callback preserves the already-committed action state");
    service.broadcast=[&](const auto&){++events;};
    check(runtime.resume(sources,service,start).stop==ActionStop::end && events==2,"Failed callback stays pending for explicit retry");
    runtime.dispatch(sources,service,"negative",start);
    check(runtime.timers().at("WAIT").end_time==74999992,"Negative signed random width is interpreted as an unsigned divisor");
    rejects([&]{runtime.dispatch(sources,service,"zero",start);},"Zero random divisor fails explicitly");
    check(runtime.command_index()==1 && runtime.update(sources,service,start).stop==ActionStop::command_failed,"Failed command remains at its retry boundary");
}
ActionRuntime actor_runtime(const Fixture& f,const AnimationSourceRuntime& sources,std::int32_t ram=0) {
    auto program=f.program();
    const auto definition=program.rams().at(ram);
    ActionActorGroup actors(program.source(),definition.reference,definition.combiner_reference,
        {{900,950,2,1},{901,951,2,1},{902,952,2,1}});
    return {std::move(program),sources,ram,decode_animation_combiner(f.document(),sources,definition.combiner_reference),std::move(actors)};
}
void actor_lifecycle() {
    Fixture f;
    f.action("active",{f.command(action_motion,"Synthetic clip"),f.command(action_fade,"IN",4,1),
        f.command(action_flash,"Damage"),f.command(action_wait,"Frame",10)});
    f.action("empty",{});
    f.action("joint",{f.command(action_joint,"section",2,8)});
    f.action("unsupported",{f.command(action_fade,"HIDE"),f.add(object)});
    auto sources=f.library(); Services service;
    auto runtime=actor_runtime(f,sources);
    runtime.dispatch(sources,service,"NDSP",start);
    check(!runtime.active() && runtime.actors()->states().size()==3 && service.events.empty(),"Inactive NDSP owns cleanup without an action list or broadcast");
    runtime.dispatch(sources,service,"joint",start);
    runtime.dispatch(sources,service,"active",start);
    for (const auto& actor:runtime.actors()->states()) {
        check(actor.combiner_reference==f.combiners[0] && actor.bone_matrices.size()==2 && actor.joint_matrices.size()==1,
            "Activation owns all actor buffers, including unselected appearance branches");
        check(actor.bone_matrices[0][0]==1 && actor.bone_matrices[1][15]==1 && actor.joint_matrices[0][12]==0,
            "New actor buffers contain original identity matrices");
    }
    runtime.pose(sources,f.data,start);
    runtime.freeze(true,start+tick);
    const auto node=runtime.node_clock(); const auto cached=runtime.combiner().cached_matrices();
    const auto old_id=runtime.combiner().states().front().id;
    auto independent=runtime; const auto events=service.events.size();
    check(runtime.timers().size()==3 && runtime.work().joint_state!=0 && runtime.appearance().color && runtime.work().frozen,
        "Cleanup starts from populated work, named timers, node phase and shared appearance attributes");
    runtime.dispatch(sources,service,"NDSP",start+20*tick);
    check(!runtime.active() && !runtime.visible() && runtime.current_action().empty() && runtime.command_index()==0 && !runtime.command_end(),
        "NDSP releases command work and hides the instance");
    check(runtime.timers().empty() && !runtime.appearance().color && !runtime.appearance().blend && !runtime.appearance().texture &&
        !runtime.appearance().lighting && !runtime.work().frozen && runtime.work().joint_state==0 && runtime.work().flash_state==0,
        "NDSP releases all timers and attributes and discards the old work fields");
    check(runtime.node_clock().time_scale==node.time_scale && runtime.node_clock().time_bias==node.time_bias &&
        runtime.node_clock().local_time==node.local_time,"NDSP retains the frozen node clock exactly");
    check(runtime.combiner().states().empty() && !runtime.combiner().cache_valid() && runtime.combiner().cached_matrices()==cached &&
        runtime.combiner().request_time(55)==100000000,"NDSP clears combiner states while preserving its last matrices and request clock");
    for (const auto& actor:runtime.actors()->states())
        check(actor.bone_matrices.empty() && actor.joint_matrices.empty() && actor.combiner_reference==f.combiners[0],
            "NDSP releases both actor arrays while retaining the combiner link");
    check(independent.active() && independent.work().frozen && independent.actors()->states()[0].bone_matrices.size()==2,
        "Runtime copies own independent actor buffers and work");
    check(service.events.size()==events && runtime.update(sources,service,start+100*tick).stop==ActionStop::end,
        "NDSP and inactive update emit no action broadcast");
    runtime.dispatch(sources,service,"active",start+100*tick);
    check(runtime.active() && runtime.command_index()==4 && runtime.work().wait_state==1 && !runtime.work().frozen &&
        runtime.combiner().states().front().id>old_id && service.events.size()==events+1,
        "The same action reactivates with fresh work, cursor and owned animation state");
    check(runtime.node_clock().time_scale==0 && runtime.node_clock().time_bias==node.time_bias &&
        runtime.actors()->states()[2].joint_matrices[0][0]==1,"Reactivation allocates identity buffers without resetting node phase or speed");
    runtime.dispatch(sources,service,"OFFREEZE",start+100*tick);
    check(runtime.node_clock().time_scale==1,"Explicit unfreeze can resume a node retained through deactivation");
    runtime.dispatch(sources,service,"unsupported",start);
    runtime.dispatch(sources,service,"NDSP",start);
    check(runtime.resume(sources,service,start).stop==ActionStop::end && runtime.current_action().empty(),
        "NDSP discards a suspended command prefix");

    auto pending=actor_runtime(f,sources); ActionServices missing;
    pending.dispatch(sources,missing,"empty",start);
    check(pending.dispatch(sources,missing,"NDSP",start).stop==ActionStop::broadcast_required && pending.active(),
        "An unresolved earlier event blocks acceptance of a new native command");
    pending.dispatch(sources,service,"NDSP",start);
    check(!pending.active() && service.events.back().action=="empty","A retried event is delivered before accepting NDSP, which adds none");
    auto unowned=f.runtime(sources);
    rejects([&]{unowned.dispatch(sources,service,"NDSP",start);},"Command-only runtime cannot silently omit actor cleanup");
    for (int i=0;i<4;++i) {
        auto player=actor_runtime(f,sources,i); player.dispatch(sources,service,"active",start);
        auto sibling=player; player.dispatch(sources,service,"NDSP",start);
        check(!player.active() && sibling.active() && sibling.actors()->states()[0].combiner_reference==f.combiners[i],
            "Every prepared RAM binds its own combiner and preserves independent actor lifetime");
    }
    const auto program=f.program();
    auto reject_group=[&](ActionActorGroup group) {
        rejects([&]{ActionRuntime candidate(program,sources,0,decode_animation_combiner(f.document(),sources,f.combiners[0]),std::move(group));},
            "Actor runtime rejects mismatched or already attached prepared ownership");
    };
    reject_group(ActionActorGroup("other",f.rams[0],f.combiners[0],{{900,950,2,1}}));
    reject_group(ActionActorGroup(program.source(),f.rams[1],f.combiners[0],{{900,950,2,1}}));
    reject_group(ActionActorGroup(program.source(),f.rams[0],f.combiners[1],{{900,950,2,1}}));
    reject_group(ActionActorGroup(program.source(),f.rams[0],f.combiners[0],{{900,950,3,1}}));
    auto attached=ActionActorGroup(program.source(),f.rams[0],f.combiners[0],{{900,950,2,1}});
    attached.activate(f.combiners[0]); reject_group(std::move(attached));
}
void malformed_and_ownership() {
    Fixture f; const auto command=f.command(action_motion,"Synthetic clip"); f.action("idle",{command});
    auto sources=f.library(); Services service;
    std::vector<ActionRuntime> players;
    for (int i=0;i<4;++i) players.push_back(f.runtime(sources,i));
    players[2].dispatch(sources,service,"idle",start); players[2].freeze(true,start);
    check(!players[0].active() && !players[1].active() && players[2].work().frozen && !players[3].active() && service.events[0].ram_no==2,
        "Four prepared RAMs retain separate activation and freeze state");
    auto copy=players[2]; copy.freeze(false,start);
    check(players[2].work().frozen && !copy.work().frozen,"Runtime copies do not share mutable action work");
    rejects([&]{f.runtime(sources,-1);},"Reject negative action RAM number");
    rejects([&]{f.runtime(sources,4);},"Reject action RAM beyond prepared list");
    AnimationSourceRuntime unprepared(action_fixture::source_name,sources.clips());
    rejects([&]{f.runtime(unprepared);},"Action runtime requires original source-offset initialization");
    auto reject=[&](const Fixture& bad){ rejects([&]{bad.program();},"Reject malformed action graph"); };
    auto bad=f; bad.scalar(bad.info,8,bad.skin); reject(bad);
    bad=f; bad.contents(bad.rams_list,{bad.rams[0]}); reject(bad);
    bad=f; bad.contents(bad.rams_list,{bad.rams[0],bad.rams[0],bad.rams[2],bad.rams[3]}); reject(bad);
    bad=f; bad.scalar(bad.rams[1],11,bad.nodes[0]); reject(bad);
    bad=f; bad.contents(bad.combiners_list,{bad.combiners[0],bad.combiners[0],bad.combiners[2],bad.combiners[3]}); reject(bad);
    bad=f; bad.scalar(bad.rams[0],10,bad.info); reject(bad);
    bad=f; bad.scalar(bad.rams[0],14,bad.info); reject(bad);
    bad=f; bad.scalar(bad.rams[0],11,null_ref); reject(bad);
    bad=f; bad.scalar(bad.nodes[0],13,0x7fc00000); reject(bad);
    bad=f; bad.scalar(command,4,0x7f800000); reject(bad);
    bad=f; bad.text(command,3,"R"); reject(bad);
    bad=f; bad.text(command,5,""); reject(bad);
    bad=f; bad.text(command,2,std::string(4097,'a')); reject(bad);
    bad=f; bad.contents(bad.actions_list,{null_ref}); reject(bad);
    bad=f; bad.scalar(bad.actions_list,3,4097); reject(bad);
    bad=f; bad.scalar(bad.actions[0],3,bad.list(object_list,{})); reject(bad);
    bad=f; bad.scalar(bad.actions[0],3,bad.list(named_list,{null_ref})); reject(bad);
    bad=f; bad.scalar(bad.actions[0],3,null_ref);
    check(!bad.program().actions()[0].command_list_present,"Retain a null authored command-list placeholder");
    auto null_runtime=bad.runtime(sources);
    rejects([&]{null_runtime.dispatch(sources,service,"idle",start);},"Null ordinary action cannot masquerade as an empty action");
    bad=f; bad.action("huge",std::vector<std::uint32_t>(65536,command)); reject(bad);
    rejects([&]{decode_action_program(f.document(),f.info,"");},"Reject missing source qualification");
    auto doc=f.document(); doc.metadata.header.byte_order=IgbByteOrder::big;
    rejects([&]{decode_action_program(doc,f.info,"source");},"Reject unverified big-endian graph");
    doc=f.document(); auto& object=doc.objects.at(doc.references.at(f.rams[0]).value_index);
    for (auto& field:object.fields) if (field.slot==14) field.type_index=1;
    rejects([&]{decode_action_program(doc,f.info,"source");},"Nullable work reference still requires the correct field type");
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto doc=read_igb_document(resources.read("_common/p_agumon.igb"));
    const std::string source="_common/p_agumon.igb";
    const auto program=decode_action_program(doc,546,source);
    const auto skin=decode_igb_skinning(doc,5665);
    std::vector<AnimationClip> clips;
    for (const auto& info:list_igb_animations(doc,skin)) clips.push_back(decode_igb_animation(doc,skin,info.reference_index));
    AnimationSourceRuntime sources(source,std::move(clips)); sources.initialize_action_offset();
    check(program.actions().size()==183 && program.rams().size()==4 && program.animations().size()==82,"Original Agumon ACTION graph counts");
    const auto sequence=decode_model_sequences(doc,14,source);
    const std::array<std::string,4> labels{"dual","slash","arm","gun"};
    const std::array<std::string,4> waits{"WAITD1","WAITS1","WAITA1","WAITG1"}, runs{"RUNND1","RUNNS1","RUNNA1","RUNNG1"};
    const std::array<std::string,4> run_clips{"runD","runS","runA","runG"};
    Services service; std::vector<ActionRuntime> players; std::vector<ModelSequenceRuntime> models;
    for (int i=0;i<4;++i) {
        players.push_back(decode_action_actor_runtime(doc,program,sources,i));
        ModelMotionPose pose({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}); set_player_turn_rates(pose);
        models.emplace_back(sequence,ModelControlState(pose),program.name(),i);
        models.back().set_label("SQ_WAIT",labels[i]); models.back().set_label("SQ_WALK",labels[i]);
    }
    std::size_t poses=0, vertices=0;
    for (std::uint64_t frame=0;frame<60;++frame) for (std::size_t i=0;i<4;++i) {
        const auto time=1000000000+frame*16666667;
        if (frame==0 || frame==10+i || frame==13+i || frame==30+i) {
            const bool walking=frame==10+i || frame==30+i;
            models[i].select(walking ? "SQ_WALK" : "SQ_WAIT");
            models[i].run(time,[&](const ModelActionRequest& request) {
                check(request.source==source && request.info_name==program.name() && request.ram_no==static_cast<std::int32_t>(i),"Sequence ACTION resolves to its exact owning RAM");
                const auto result=players[i].dispatch(sources,service,request,time);
                check(result.stop==ActionStop::end || result.stop==ActionStop::waiting,"Original sequence applies its action effects");
                check(players[i].current_action()==(walking ? runs[i] : waits[i]),"Original weapon selection reaches its action program");
            });
        }
        players[i].update(sources,service,time);
        const auto pose=players[i].pose(sources,skin,time); ++poses; vertices+=pose.positions.size();
        check(pose.positions.size()==1371,"Original ACTION pose covers all selected vertices");
        for (const auto& p:pose.positions) check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),"Original ACTION vertex is finite");
    }
    for (std::size_t i=0;i<4;++i) check(players[i].combiner().states().size()==1 &&
        sources.clip(*players[i].combiner().states()[0].clip_reference).info.name==run_clips[i],"Each original ACTION RAM ends at its own weapon's running clip");
    std::size_t commands=0, action_poses=0, action_vertices=0;
    for (const auto& action:program.actions()) {
        auto local_sources=sources; auto runtime=decode_action_actor_runtime(doc,program,local_sources,0);
        if (action.name=="NDSP") {
            check(!action.command_list_present,"Original NDSP is a native-command placeholder");
            runtime.dispatch(local_sources,service,action.name,1000000000);
            check(!runtime.active() && runtime.actors()->states().size()==3,"Original NDSP bypasses its null command list and owns actor cleanup");
            continue;
        }
        const auto result=runtime.dispatch(local_sources,service,action.name,1000000000);
        check(result.stop==ActionStop::end || result.stop==ActionStop::waiting,"Every original Agumon action dispatch reaches its first yield/end");
        commands+=result.commands_executed;
        bool completed=false;
        for (std::uint64_t frame=0;frame<600;++frame) {
            const auto time=1000000000+frame*16666667;
            commands+=runtime.update(local_sources,service,time).commands_executed;
            const auto pose=runtime.pose(local_sources,skin,time); ++action_poses; action_vertices+=pose.positions.size();
            check(pose.positions.size()==1371,"Original action prefix evaluates its skin");
            for (const auto& p:pose.positions) check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),"Original action prefix vertex remains finite");
            const auto& work=runtime.work();
            if (frame>=3 && runtime.command_end() && work.motion.state==0 && work.fade_state!=1 && work.fade_state!=2 && work.flash_state==0) {
                completed=true; break;
            }
        }
        if (!completed) throw std::runtime_error("Original action did not finish its commands/effects: "+action.name);
        check(completed,"Original action wait/effect sequence completes within its reference-time budget");
        const auto event_count=service.events.size();
        runtime.dispatch(local_sources,service,"NDSP",11000000000);
        check(!runtime.active() && runtime.timers().empty() && runtime.combiner().states().empty() && service.events.size()==event_count,
            "Every original action deactivates without residual work or extra events");
        for (const auto& actor:runtime.actors()->states())
            check(actor.bone_matrices.empty() && actor.joint_matrices.empty(),"Every original action frees all LOD actor buffers");
        runtime.dispatch(local_sources,service,action.name,12000000000);
        check(runtime.active() && service.events.size()==event_count+1,"Every original action can reactivate after NDSP");
    }
    std::cout<<"Original ACTION2 actions="<<program.actions().size()<<" executed_commands="<<commands<<" action_poses="<<action_poses<<" action_vertices="<<action_vertices
        <<" sequence_players=4 sequence_poses="<<poses<<" sequence_vertices="<<vertices<<'\n';
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        dispatch_and_waits(); joint_and_freeze(); appearance_and_order(); service_boundaries(); actor_lifecycle(); malformed_and_ownership();
        if (args.size()==2) original(args[1]);
        else if (args.size()!=1) throw std::runtime_error("Usage: dw4_actions_tests [EXTRACTED_ROOT]");
        std::cout<<checks<<" action checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Action failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
