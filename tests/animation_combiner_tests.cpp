#include "dw4/animation_combiner.hpp"
#include "dw4/assets.hpp"
#include "animation_fixture.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace animation_fixture;
int checks{};
void check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation, const char* reason) {
    bool caught=false; try { operation(); } catch (const AnimationError&) { caught=true; }
    check(caught,reason);
}
AnimationMatrix identity() { AnimationMatrix m{}; m[0]=m[5]=m[10]=m[15]=1; return m; }
constexpr std::uint64_t tick=262144, start=4*tick, duration=16*tick;
AnimationBlendWindow window() { return {1,duration,0,1}; }
std::vector<AnimationClip> clips() {
    Fixture f; auto a=decode_igb_animation(f.document(),f.data,f.clip);
    a.info.reference_index=100; a.use_sample_translation={false,false}; a.constant_translations={{},{2,0,0}};
    auto b=a; b.info.reference_index=200; b.info.source_reference+=1000; b.constant_translations[1].x=10;
    auto c=b; c.info.reference_index=300; c.info.source_reference+=1000; c.constant_translations[1].x=18;
    return {a,b,c};
}
AnimationSourceRuntime library() { return {"synthetic/actor.igb",clips()}; }
AnimationCombiner combiner(const AnimationSourceRuntime& sources) { return {sources,{identity(),identity()}}; }

void transitions() {
    auto sources=library(); auto c=combiner(sources);
    check(c.update(sources,start)==0 && !c.cache_valid(),"Empty update preserves uninitialized cache");
    check(c.evaluate(sources,start)==std::vector<AnimationMatrix>(2,identity()) && !c.cache_valid(),"Empty evaluation preserves prepared matrices");
    c.add_base(sources,100,7);
    check(c.states()[0].status==0 && c.states()[0].is_new && c.states()[0].transition_mode==4 &&
        c.states()[0].clock.time_scale==1 && c.states()[0].time_requested==7,"First activation defaults");
    check(c.evaluate(sources,start)[1][12]==2 && !c.states()[0].is_new,"Base evaluation samples and clears new flag");
    c.append_blend(sources,200,window(),9);
    check(c.states().size()==2 && c.states()[1].status==1 && !c.cache_valid(),"Incoming state is retained and invalidates matrices");
    check(c.evaluate(sources,start)[1][12]==2 && c.states()[1].weight==0,"Explicit blend starts at base translation");
    check(c.evaluate(sources,start+duration/2)[1][12]==6 && c.states()[1].weight==0.5f,"Half-time transition mixes the actual channels");
    const auto incoming=c.states()[1].id;
    check(c.update(sources,start+duration)==2 && c.states().size()==1,"Update returns pre-retirement count");
    check(c.states()[0].id==incoming && c.states()[0].combine_mode==0,"Completion preserves incoming state identity as base");
    check(c.evaluate(sources,start+duration)[1][12]==10,"Completed transition reaches incoming translation");

    auto pending=combiner(sources); pending.add_base(sources,100,0); pending.append_blend(sources,200,window(),0);
    pending.update(sources,start); pending.update(sources,start+duration/2);
    check(pending.states()[1].is_new && pending.states()[1].weight==0 &&
        pending.states()[1].blend.start_time==start+duration/2,"Repeated updates restart a still-new explicit blend");
    pending.evaluate(sources,start+duration/2); pending.evaluate(sources,start+duration);
    check(pending.states()[1].weight==0.5f,"Sampling clears the restart flag before the next update");
    const auto saved=pending.cached_matrices(); const auto local=pending.states()[1].clock.local_time;
    pending.set_state_speed(1,start+duration,0);
    check(pending.evaluate(sources,start+duration)==saved && pending.states()[1].clock.local_time==local,
        "Same-time cache does not reevaluate after a direct speed write");

    auto zero=combiner(sources); zero.add_base(sources,100,0); zero.append_blend(sources,200,window(),0);
    zero.update(sources,0); zero.update(sources,tick);
    check(zero.states()[1].blend.duration==100000001/3 && zero.states()[1].weight==0,
        "Zero start timestamp follows the original missing named-transition fallback");
    auto named=clips(); named[1].info.transition_count=1; AnimationSourceRuntime named_sources("named",named);
    auto unsupported=combiner(named_sources); unsupported.add_base(named_sources,100,0);
    unsupported.append_blend(named_sources,200,window(),0); unsupported.update(named_sources,0);
    rejects([&]{unsupported.update(named_sources,tick);},"Unknown named transition must remain explicit");
    check(unsupported.states()[1].blend.start_time==0,"Failed named transition does not publish partial clocks");
}

void snapshots_and_order() {
    auto sources=library(); auto c=combiner(sources); c.add_base(sources,100,0); c.evaluate(sources,start);
    c.append_blend(sources,200,window(),0); c.evaluate(sources,start); c.evaluate(sources,start+duration/2);
    const auto saved=c.cached_matrices(); c.update(sources,start+duration*3/4); c.snapshot();
    check(c.states().size()==1 && !c.states()[0].clip_reference && c.states()[0].status==1 && c.states()[0].is_new,
        "Snapshot replaces live states with one fixed state");
    check(c.states()[0].channels[1].translation.x==6 && c.cached_matrices()==saved,
        "Snapshot uses the last matrix even when state time has advanced");
    check(c.evaluate(sources,start+duration)[1][12]==6,"Fixed snapshot remains fixed as time advances");
    c.append_blend(sources,300,window(),0); c.evaluate(sources,start+duration);
    check(c.evaluate(sources,start+duration*3/2)[1][12]==12,"Interrupted transition starts from the saved intermediate pose");

    auto ordered=combiner(sources); ordered.add_base(sources,100,0);
    ordered.append_blend(sources,200,window(),0); ordered.append_blend(sources,300,window(),0);
    ordered.evaluate(sources,start);
    check(ordered.evaluate(sources,start+duration/2)[1][12]==12,"Equal priorities preserve insertion order for all layers");
    check(ordered.update(sources,start+duration)==3 && ordered.states().size()==1 &&
        ordered.states()[0].clip_reference==300,"Several completions retire preceding tracks in one update");

    auto values=clips(); values[0].info.priority=10; values[1].info.priority=-10;
    AnimationSourceRuntime priority("priority",values); auto p=combiner(priority); p.add_base(priority,100,0);
    p.append_blend(priority,200,window(),0); p.evaluate(priority,start);
    check(p.evaluate(priority,start+duration/2)[1][12]==2,"Tracks below the last replace priority do not contribute");
    p.update(priority,start+duration);
    check(p.states().size()==2 && p.states()[0].clip_reference==100,"Low-priority completion does not retire a higher-priority base");

    auto matrices=std::vector<AnimationMatrix>(2,identity()); matrices[1][0]=matrices[1][5]=matrices[1][10]=3;
    matrices[1][3]=7; matrices[1][12]=42;
    AnimationCombiner nonunit(sources,matrices); nonunit.snapshot();
    check(nonunit.states()[0].channels[1].rotation[3]>1.5f,"Snapshot conversion does not normalize a nonunit matrix");
    const auto reconstructed=nonunit.evaluate(sources,start);
    check(reconstructed[1][0]==1 && reconstructed[1][3]==7 && reconstructed[1][12]==42,
        "Fixed-state evaluation rebuilds only rotation and translation fields");
}

void motion_and_ownership() {
    auto sources=library(); sources.initialize_action_offset(); auto c=combiner(sources); c.add_base(sources,100,0);
    AnimationMotionWork work{true,false,0};
    apply_animation_motion(c,sources,work,{200,8,"RR","LP"},start,start);
    check(c.states().size()==1 && c.states()[0].clip_reference==200 && work.state==1 && work.is_motion && !work.no_interpolation,
        "First MOTIONSET consumes no-interpolation and immediately retires the base");
    advance_animation_motion(c,sources,work,start,0.5f);
    check(work.state==2 && c.states()[0].clock.time_scale==1,"Motion state 1 only advances to state 2");
    advance_animation_motion(c,sources,work,start,0.5f);
    check(work.state==0 && work.is_motion && c.states()[0].clock.time_scale==0.5f,"One remaining state aligns node speed on the next update");

    auto paused=combiner(sources); paused.add_base(sources,100,0); AnimationMotionWork paused_work;
    apply_animation_motion(paused,sources,paused_work,{200,8,"SS","NP"},4*tick,12*tick);
    check(paused.states()[0].clock.time_bias==4*static_cast<std::int64_t>(tick) &&
        paused.states()[1].clock.time_bias==-8*static_cast<std::int64_t>(tick),"S mode preserves phase at SYSTEM time after node-time blend setup");
    check(paused.states()[1].time_requested==4*tick && paused.states()[1].blend.start_time==12*tick &&
        sources.mode(200)==AnimationPlayMode::offset_clamp,"Request time, node blend time and source loop selection remain distinct");
    const auto before=paused.states()[1].clock.time_bias;
    rejects([&]{apply_animation_motion(paused,sources,paused_work,{999,8,"RR","LP"},start,start);},"Reject unresolved clip before publishing motion effects");
    check(paused.states().size()==2 && paused.states()[1].clock.time_bias==before && paused_work.state==1 &&
        sources.mode(200)==AnimationPlayMode::offset_clamp,"Failed interrupted motion is atomic across combiner/source/work");
    rejects([&]{apply_animation_motion(paused,sources,paused_work,{100,8,"S","LP"},start,start);},"Reject incomplete authored interpolation characters");

    auto blend=combiner(sources); blend.add_base(sources,100,0); blend.evaluate(sources,start); AnimationMotionWork phase;
    apply_animation_motion(blend,sources,phase,{200,8,"RR","LP"},start,start);
    advance_animation_motion(blend,sources,phase,start,1); advance_animation_motion(blend,sources,phase,start,1);
    check(!blend.states()[0].is_new && !blend.states()[1].is_new,"Motion update clears all new flags after advancing a multi-state combiner");
    advance_animation_motion(blend,sources,phase,start+200000000,1);
    check(phase.state==0 && blend.states().size()==1,"Motion update exits after transition retirement");

    std::vector<AnimationCombiner> players(4,combiner(sources));
    for (std::size_t i=0;i<players.size();++i) { players[i].add_base(sources,100,0); players[i].set_state_speed(0,start,static_cast<float>(i)); }
    for (std::size_t i=0;i<players.size();++i) {
        players[i].evaluate(sources,start+tick);
        check(players[i].states()[0].clock.local_time==static_cast<std::int64_t>((4+i)*tick),"Four combiners retain independent state clocks with shared sources");
    }
    players[0].snapshot(); check(players[1].states()[0].clip_reference==100,"One player's interruption does not mutate another combiner");
    auto independent=sources; independent.set_mode(100,AnimationPlayMode::clamp);
    check(sources.mode(100)==AnimationPlayMode::repeat,"Copied sessions own independent source modes");
    auto alien=library(); rejects([&]{players[1].evaluate(alien,start);},"Same path/reference IDs do not authorize a different owned library");
    auto shared=clips(); shared[1].info.source_reference=shared[0].info.source_reference;
    AnimationSourceRuntime aliases("shared",shared); aliases.set_mode(100,AnimationPlayMode::bounce);
    check(aliases.mode(200)==AnimationPlayMode::bounce && aliases.mode(300)==AnimationPlayMode::repeat,"Mode changes follow source identity, not clip identity");
}

struct Prepared : Fixture {
    std::uint32_t combiner_ref, states_ref, lists_ref, bases_ref, matrices_ref, inner_ref;
    Prepared() {
        combiner_ref=add(combiner_type); scalar(combiner_ref,3,skeleton);
        states_ref=list(state_list,{}); scalar(combiner_ref,6,states_ref);
        inner_ref=list(combiner_tracks,{}); lists_ref=list(combiner_lists,{inner_ref,list(combiner_tracks,{})}); scalar(combiner_ref,4,lists_ref);
        bases_ref=list(int_list,{0,0}); scalar(combiner_ref,5,bases_ref);
        Bytes bytes; for (int i=0;i<2;++i) for (float value:identity()) push32(bytes,std::bit_cast<std::uint32_t>(value));
        matrices_ref=memory(bytes); scalar(combiner_ref,8,matrices_ref);
        records[combiner_ref].values[14]=words({null_ref,null_ref});
    }
};
void parsing_and_guards() {
    Prepared f; AnimationSourceRuntime sources("fixture",{decode_igb_animation(f.document(),f.data,f.clip)});
    auto c=decode_animation_combiner(f.document(),sources,f.combiner_ref);
    check(c.states().empty() && c.cached_matrices()==std::vector<AnimationMatrix>(2,identity()) && !c.cache_valid(),"Decode prepared empty combiner matrices");
    check(c.request_time(123456)==123456,"Unset serialized request clock uses system time");
    auto valid=f; valid.records[valid.combiner_ref].values[14]=words({123,4}); valid.scalar(valid.combiner_ref,10,1);
    c=decode_animation_combiner(valid.document(),sources,valid.combiner_ref);
    check(c.cache_valid() && c.request_time(0)==(4ull<<32)+123,"Preserve serialized full-width clock override and cache flag");
    auto reject=[&](const Prepared& bad,const char* reason) {
        rejects([&]{decode_animation_combiner(bad.document(),sources,bad.combiner_ref);},reason);
    };
    auto bad=f; bad.scalar(bad.combiner_ref,3,bad.skin); reject(bad,"Reject wrong combiner skeleton");
    bad=f; bad.scalar(bad.combiner_ref,6,null_ref); reject(bad,"Reject missing state list");
    bad=f; bad.scalar(bad.combiner_ref,6,bad.list(object_list,{})); reject(bad,"Reject wrong state-list subtype");
    bad=f; bad.scalar(bad.combiner_ref,4,bad.list(object_list,{bad.inner_ref,bad.inner_ref})); reject(bad,"Reject wrong track-list-list subtype");
    bad=f; bad.contents(bad.lists_ref,{bad.list(object_list,{}),bad.inner_ref}); reject(bad,"Reject wrong bone-track subtype");
    bad=f; bad.scalar(bad.combiner_ref,5,bad.list(object_list,{0,0})); reject(bad,"Reject wrong base-index list subtype");
    bad=f; bad.contents(bad.states_ref,{bad.clip}); reject(bad,"Reject active serialized states needing full restoration");
    bad=f; bad.contents(bad.lists_ref,{bad.inner_ref}); reject(bad,"Reject missing bone track list");
    bad=f; bad.contents(bad.lists_ref,{null_ref,bad.inner_ref}); reject(bad,"Reject null bone track list");
    bad=f; bad.contents(bad.inner_ref,{bad.clip}); reject(bad,"Reject serialized live tracks");
    bad=f; bad.scalar(bad.bases_ref,2,1); reject(bad,"Reject inconsistent base-index count");
    bad=f; bad.scalar(bad.bases_ref,3,1); reject(bad,"Reject base-index capacity below count");
    bad=f; bad.contents(bad.bases_ref,{0,1}); reject(bad,"Reject nonzero base for empty tracks");
    bad=f; bad.scalar(bad.bases_ref,4,bad.memory(words({0}))); reject(bad,"Reject truncated base-index memory");
    bad=f; bad.records[bad.matrices_ref].data.resize(127); reject(bad,"Reject partial cached matrix");
    bad=f; word(bad.records[bad.matrices_ref].data,0,0x7fc00000); reject(bad,"Reject non-finite cached matrix");
    bad=f; bad.scalar(bad.combiner_ref,10,2); reject(bad,"Reject invalid cache-valid byte");
    auto doc=f.document(); auto& object=const_cast<IgbObject&>(doc.object(f.combiner_ref));
    for (auto& field:object.fields) if (field.slot==14) field.size=4;
    rejects([&]{decode_animation_combiner(doc,sources,f.combiner_ref);},"Reject short clock field in an owned graph");

    auto values=clips(); values[1].info.reference_index=100;
    rejects([&]{AnimationSourceRuntime runtime("duplicate",values);},"Reject duplicate clips");
    values=clips(); values[1].bone_references[0]+=100;
    rejects([&]{AnimationSourceRuntime runtime("mixed",values);},"Reject mixed skeleton bindings");
    values=clips(); ++values[0].info.duration_ticks;
    rejects([&]{AnimationSourceRuntime runtime("duration",values);},"Reject inconsistent duration metadata");
    rejects([&]{AnimationCombiner invalid(sources,{identity()});},"Reject incomplete prepared matrices");
    auto matrices=std::vector<AnimationMatrix>(2,identity()); matrices[0][0]=std::numeric_limits<float>::infinity();
    rejects([&]{AnimationCombiner invalid(sources,matrices);},"Reject invalid prepared matrix arithmetic");
    auto good=library(); auto atomic=combiner(good); atomic.add_base(good,100,0); atomic.evaluate(good,start);
    atomic.append_blend(good,200,{1,0,0,1},0);
    rejects([&]{atomic.evaluate(good,start);},"Reject unusable zero-duration NaN weight at pose boundary");
    check(atomic.states()[1].is_new && atomic.states()[1].status==1 && atomic.cached_matrices()[1][12]==2,
        "Failed pose leaves live states and the last valid matrices unchanged");
    auto data=Fixture{}.data; data.joints.push_back({1,-1,{}, {}, {}});
    rejects([&]{atomic.pose(good,data,start);},"Invalid pose cannot publish a partially advanced combiner");
}

void original(const std::vector<std::filesystem::path>& args) {
    const auto doc=read_igb_document(assets::AfsArchive::open(args[1]).read(std::stoul(args[2].string())));
    const auto skin=decode_igb_skinning(doc,static_cast<std::uint32_t>(std::stoul(args[3].string())));
    std::vector<AnimationClip> decoded;
    for (const auto& info:list_igb_animations(doc,skin)) decoded.push_back(decode_igb_animation(doc,skin,info.reference_index));
    AnimationSourceRuntime sources(args[1].string()+":"+args[2].string(),std::move(decoded)); sources.initialize_action_offset();
    std::vector<AnimationCombiner> players;
    for (const auto& object:doc.objects) if (doc.is_a(object,"igAnimationCombiner") && doc.field_reference(object,3)==skin.info.skeleton_reference)
        players.push_back(decode_animation_combiner(doc,sources,object.reference_index));
    check(players.size()==4,"Original selected skeleton must have four prepared combiners");
    const std::array<std::string,4> waits{"waitD","waitS","waitA","waitG"}, runs{"runD","runS","runA","runG"};
    auto ref=[&](const std::string& name) {
        for (const auto& clip:sources.clips()) if (clip.info.name==name) return clip.info.reference_index;
        throw std::runtime_error("Required original clip not found: "+name);
    };
    std::array<AnimationMotionCommand,4> wait_commands, run_commands;
    const std::array<std::uint32_t,4> wait_records{12,1242,3701,724}, run_records{3702,726,496,497};
    auto command=[&](std::uint32_t reference,const std::string& expected_name) {
        const auto& object=doc.object(reference);
        check(doc.is_a(object,"beAction2MOTIONSET") && doc.field_string(object,2)==expected_name,
            "Explicit original motion record must select its weapon-specific clip");
        return AnimationMotionCommand{ref(std::string(doc.field_string(object,2))),doc.field_f32(object,4),
            std::string(doc.field_string(object,3)),std::string(doc.field_string(object,5))};
    };
    for (std::size_t i=0;i<players.size();++i) {
        wait_commands[i]=command(wait_records[i],waits[i]); run_commands[i]=command(run_records[i],runs[i]);
    }
    std::array<AnimationMotionWork,4> work{}; std::size_t poses{}, interruptions{};
    for (std::size_t i=0;i<players.size();++i) {
        players[i].add_base(sources,ref("waitD"),0); work[i].no_interpolation=true;
        apply_animation_motion(players[i],sources,work[i],wait_commands[i],1000000000,1000000000);
    }
    for (std::uint64_t frame=0;frame<60;++frame) for (std::size_t i=0;i<players.size();++i) {
        const auto time=1000000000+frame*16666667;
        if (frame==10+i || frame==13+i || frame==30+i) {
            if (players[i].states().size()>1) ++interruptions;
            apply_animation_motion(players[i],sources,work[i],frame==13+i ? wait_commands[i] : run_commands[i],time,time);
        }
        advance_animation_motion(players[i],sources,work[i],time,1);
        const auto pose=players[i].pose(sources,skin,time);
        check(pose.positions.size()==skin.source_positions.size() && pose.joint_positions.size()==skin.joints.size(),"Original combined pose reaches the complete selected skin");
        for (const auto& p:pose.positions) check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),"Original combined vertex is finite");
        ++poses;
    }
    check(interruptions==4,"Every original prepared combiner exercises an interrupted motion");
    for (std::size_t i=0;i<players.size();++i) check(players[i].states().size()==1 && players[i].states()[0].clip_reference==ref(runs[i]),
        "Each original player retains its own final weapon-specific running state");
    std::cout<<"Original library clips="<<sources.clips().size()<<" combiners="<<players.size()<<" poses="<<poses<<" interruptions="<<interruptions<<'\n';
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        transitions(); snapshots_and_order(); motion_and_ownership(); parsing_and_guards();
        if (args.size()==4) original(args);
        else if (args.size()!=1) throw std::runtime_error("Usage: dw4_animation_combiner_tests [archive.afs entry geometry-reference]");
        std::cout<<checks<<" animation combiner checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Animation combiner test failure: "<<error.what()<<'\n'; return 1; }
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
