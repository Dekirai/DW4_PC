#include "dw4/model_player_work.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_timer.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F call,const char* message) {
    ++checks;try{call();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Clock {std::uint64_t now=100;unsigned reads{};};
struct Fixture {
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<Clock> clock=std::make_shared<Clock>();
    std::shared_ptr<ModelRamTimerAccess> access=std::make_shared<ModelRamTimerAccess>(ram,
        [clock=clock](const ModelSceneRam&) {++clock->reads;return clock->now;});
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    std::shared_ptr<AnimationMatrix> matrix=std::make_shared<AnimationMatrix>(identity());
    ModelControlState state=bind_model_player_control(work,matrix,access);
};
MotionCommand unknown_move() {MotionCommand command;command.type="beModelCtrlMOVE";command.name="unknown";return command;}
void work_only_operations() {
    Fixture f;
    f.work->control.mode=9;f.work->control.phase=7;
    apply_model_command(f.state,ModelCtrlCommand{0,"isLive"});
    check(f.state.mode()==4 && f.state.phase()==0 && !f.ram->data && f.clock->reads==0,
        "CTRL changed timing or lost its original mode/phase stores.");
    apply_model_command(f.state,PadMotionCommand{0,"Move",2,3});
    check(f.state.pad_motion.size()==4 && !f.ram->data && f.clock->reads==0,"PDMOVE performed an implicit timer operation.");
    auto snapshot=f.state.snapshot_work();snapshot.mode()=2;
    rejects([&]{snapshot.elapsed_timer();},"A work-only snapshot acquired a prepared timer slot.");
    check(!snapshot.timer_access_owner(),"A work-only snapshot retained actual timer authority.");
    rejects([&]{auto copy=f.state;(void)copy;},"Full copy of an actual timer binding silently detached its timer.");
    rejects([&]{f.state=snapshot;},"General assignment published a work snapshot into an actual timer binding.");
    f.state.publish_work(snapshot);
    check(f.state.mode()==2 && f.state.timer_access_owner()==f.access && !f.ram->data && f.clock->reads==0,
        "Work publication lost the endpoint or touched the named list.");
    const auto decision=update_model_control(f.state,1,"SQ_WAIT","SQ_NONE");
    check(decision.dispatch_sequence && decision.requested_sequence=="SQ_WALK" && !f.ram->data && f.clock->reads==0,
        "Control-state staging queried or created a timer.");
    const auto prepared_slot=std::make_shared<ModelRamTimerSlot>();
    auto prepared=bind_model_player_control(make_model_player_work(),std::make_shared<AnimationMatrix>(identity()),prepared_slot);
    prepared_slot->value.emplace();prepared_slot->value->time_bias=31;
    const auto saved=prepared.snapshot_work();prepared_slot->value->time_bias=47;prepared.publish_work(saved);
    check(prepared_slot->value->time_bias==47,"Unrelated work publication restored a stale prepared timer.");
}
void timer_prefix_and_fresh_heading() {
    Fixture f;
    const auto wrong=std::make_shared<ModelSceneData>();wrong->name="KEIKATIMER";
    f.ram->data=std::make_shared<ModelSceneList>();f.ram->data->append(wrong);
    apply_model_command(f.state,ModelMutekiCommand{0,"OFFWALL",std::numeric_limits<float>::quiet_NaN()});
    check(f.state.wall_check()==0 && f.ram->data->count==1 && f.clock->reads==0,
        "Flag-only MUTEKI classified or read an existing named record's unused timer/frames fields.");
    f.ram->data.reset();
    apply_model_command(f.state,ModelMutekiCommand{0,"ONWALL",0});
    const auto timer=find_model_timer(*f.ram,"KEIKATIMER");
    check(timer && timer->timer.cached_frames==1 && timer->timer.saved_world_time==100 && f.clock->reads==1,
        "MUTEKI did not allocate its actual named record at the original start site.");
    const auto initial=timer->timer;f.clock->now=1000000;
    apply_model_command(f.state,ModelMutekiCommand{0,"ONFLOOR",0},9999999);
    check(timer->timer==initial && f.clock->reads==1,"Existing timer was restarted or command-entry time overrode context.");
    player_runtime_fixture::RuntimeFixture source;
    auto resource=std::make_shared<ActorSceneResource>(source.document(),"synthetic/timer-node.igb",3);
    auto one=std::make_shared<ModelSceneOne>();one->name="ONE";one->node=resource->mutable_node(source.nodes[1]);
    auto heading=identity();heading[0]=0;heading[2]=-1;heading[8]=1;heading[10]=0;
    *one->node->matrix_owner()=heading;f.ram->data->append(one);f.state.pose.position_locked()=255;
    apply_model_command(f.state,unknown_move());
    check(f.state.pose.position_locked()==0 && std::abs(f.work->motion.turn.desired[1]-1.57079637f)<0.000001f &&
        f.work->motion.turn.applied[1]==f.work->motion.turn.desired[1] && *f.matrix==identity() && f.clock->reads==1,
        "MOVE did not use its fresh RAM ONE node while retaining the captured work fields.");
    Fixture missing;missing.state.pose.position_locked()=1;
    rejects([&]{apply_model_command(missing.state,unknown_move());},"MOVE ignored its missing fresh ONE heading owner.");
    check(find_model_timer(*missing.ram,"KEIKATIMER") && missing.state.pose.position_locked()==0 && missing.clock->reads==1,
        "Later heading failure rolled back the committed timer/unlock prefix.");
}
void retained_generations_and_countdown() {
    Fixture f;apply_model_command(f.state,ModelMutekiCommand{0,"TIME",4});
    const auto old_timer=find_model_timer(*f.ram,"KEIKATIMER");
    const auto old_value=old_timer->timer;
    const auto fresh=make_model_player_work();auto newer=bind_model_player_control(fresh,f.matrix,f.access);
    f.ram->data=std::make_shared<ModelSceneList>();
    auto replacement=std::make_shared<ModelSceneTimer>();replacement->name="KEIKATIMER";
    replacement->timer.saved_world_time=f.clock->now;replacement->timer.cached_frames=2;
    f.ram->data->append(replacement);newer.invulnerability_frames()=1;
    advance_model_invulnerability(f.state);advance_model_invulnerability(newer);
    check(f.work->control.invulnerability_frames==2 && fresh->control.invulnerability_frames==0 && old_timer->timer==old_value,
        "Old/new work failed to share the RAM's current timer or overwrote its replaced record.");
    const auto reads=f.clock->reads;
    for(const auto value:{0.0f,-0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN()}) {
        f.state.invulnerability_frames()=value;advance_model_invulnerability(f.state);
        check(std::bit_cast<std::uint32_t>(f.state.invulnerability_frames())==std::bit_cast<std::uint32_t>(value),
            "Nonpositive or unordered countdown changed its original field.");
    }
    check(f.clock->reads==reads,"Skipped countdown branches accessed the timer.");
    f.state.invulnerability_frames()=2;replacement->timer.cached_frames=std::numeric_limits<float>::quiet_NaN();
    advance_model_invulnerability(f.state);
    check(std::isnan(f.state.invulnerability_frames()),"An unordered countdown result was incorrectly clamped.");
    f.state.pose.translated()=255;f.ram->data.reset();const auto before=f.clock->reads;
    advance_model_motion(f.state);
    check(f.state.pose.translated()==0 && !f.ram->data && f.clock->reads==before,
        "Empty model movement accessed the timer or skipped the translated-byte clear.");
}
void sequence_stages_without_timer_authority() {
    Fixture f;player_runtime_fixture::RuntimeFixture source;
    const auto binding=source.binding();
    const auto program=std::make_shared<const ModelSequenceProgram>(binding.sequence_program());
    auto sequence=bind_model_player_sequence(f.work,program,binding.model_name(),0,f.matrix,f.access);
    sequence.begin_select("SQ_WAIT");
    check(!f.ram->data && f.clock->reads==0 && sequence.selection_pending(),"Sequence begin staged a timer.");
    const auto timer=f.access->start("KEIKATIMER");timer->timer.time_bias=89;
    const auto retained=timer->timer;f.ram->data.reset();const auto reads=f.clock->reads;
    sequence.finish_select();sequence.select("SQ_WAIT");
    check(!f.ram->data && f.clock->reads==reads && timer->timer==retained &&
        std::as_const(sequence).model().timer_access_owner()==f.access,"Sequence cleanup/selection touched timer state or lost the actual endpoint.");
    rejects([&]{auto copy=sequence;(void)copy;},"Sequence copy duplicated actual timer authority.");
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto document=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto map=read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179));
    const std::array<const char*,4> labels{"dual","slash","arm","gun"};
    const std::array<const char*,4> waits{"WAITD1","WAITS1","WAITA1","WAITG1"},runs{"RUNND1","RUNNS1","RUNNA1","RUNNG1"};
    for(std::uint32_t slot=0;slot<4;++slot) for(std::size_t weapon=0;weapon<labels.size();++weapon) {
        auto scene=std::make_shared<ActorSceneResource>(document,"_common/p_agumon.igb",17);
        const auto binding=decode_player_runtime_binding(document,{1386,546,slot,0},scene->source(),17);
        const auto ram=scene->models().ram(binding.model_ram_reference());
        const auto one=find_model_one(*ram);
        check(one && one->node==scene->mutable_node(binding.node_reference()),"Original timer integration lost its actual RAM ONE node.");
        one->work=make_model_player_work();
        const auto placement=decode_igb_player_placement(map,115,0,slot);
        *one->node->matrix_owner()=placement.matrix;
        unsigned clock_reads{};
        auto access=std::make_shared<ModelRamTimerAccess>(ram,[&](const ModelSceneRam& owner) {
            check(&owner==ram.get(),"Actual timer clock was queried for another RAM.");++clock_reads;return std::uint64_t{42};
        });
        auto runtime=bind_model_player_sequence(one->work,std::make_shared<const ModelSequenceProgram>(binding.sequence_program()),
            binding.model_name(),static_cast<std::int32_t>(slot),one->node->matrix_owner(),access);
        set_player_turn_rates(runtime.model().pose);synchronize_model_heading(runtime.model().pose);
        runtime.set_label("SQ_WAIT",labels[weapon]);runtime.set_label("SQ_WALK",labels[weapon]);
        std::vector<std::string> actions;
        // Records the real ACTION request boundary only; this does not execute
        // the original outer player update or complete animation dispatch.
        const auto dispatch=[&](const ModelActionRequest& request){actions.push_back(request.action);};
        runtime.select("SQ_WAIT");runtime.run(999999,dispatch);
        const auto walk=update_model_control(runtime.model(),1,runtime.current_sequence(),"SQ_NONE");
        check(walk.dispatch_sequence && walk.requested_sequence=="SQ_WALK","Original control did not request WALK with actual timer binding.");
        runtime.select(walk.requested_sequence);runtime.run(999999,dispatch);
        for(int i=0;i<18;++i) advance_model_motion(runtime.model());
        const auto idle=update_model_control(runtime.model(),0,runtime.current_sequence(),"SQ_NONE");
        check(idle.dispatch_sequence && idle.requested_sequence=="SQ_WAIT","Original control did not request WAIT with actual timer binding.");
        runtime.select(idle.requested_sequence);runtime.run(999999,dispatch);
        for(int i=0;i<6;++i) advance_model_motion(runtime.model());
        check(actions==std::vector<std::string>{waits[weapon],runs[weapon],waits[weapon]},"Actual timer integration changed original weapon/action branches.");
        for(std::size_t axis=0;axis<3;++axis) check(std::abs(runtime.model().pose.matrix()[12+axis]-(placement.matrix[12+axis]+placement.matrix[8+axis]*8.275f))<0.001f,
            "Actual named timer movement changed the original sequence distance or basis.");
        const auto timer=find_model_timer(*ram,"KEIKATIMER");
        check(timer && timer->timer.saved_world_time==42 && timer->timer.cached_frames==1 && clock_reads>=25,
            "Original command/movement consumers used a prepared clock or omitted actual helper reads.");
    }
}
}
int main(int argc,char** argv) {
    try {work_only_operations();timer_prefix_and_fresh_heading();retained_generations_and_countdown();sequence_stages_without_timer_authority();
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_control_timer_tests [extracted-game-root]");
        std::cout<<"Actual model control/timer checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Actual model control/timer after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
