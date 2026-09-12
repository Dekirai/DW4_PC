#include "dw4/model_control.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_timer.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
constexpr std::array<float,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
struct Fixture {
    player_runtime_fixture::RuntimeFixture authored;
    ActorSceneResource resource{authored.document(),"synthetic/raw-control.igb",1};
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<ModelControlFields> fields=std::make_shared<ModelControlFields>();
    std::shared_ptr<ModelMotionFields> motion=std::make_shared<ModelMotionFields>();
    unsigned clocks{};
    std::function<void()> clock_hook;
    std::shared_ptr<ModelRamTimerAccess> timer=std::make_shared<ModelRamTimerAccess>(ram,[this](const ModelSceneRam&) {
        ++clocks;if(clock_hook)clock_hook();return std::uint64_t{123456789};
    });
    ModelControlState state{ModelMotionPose::fields_only(motion),fields,timer};
    Fixture() {
        one->name="ONE";one->node=resource.mutable_node(authored.nodes[0]);*one->node->matrix_owner()=identity;
        ram->data=std::make_shared<ModelSceneList>();ram->data->append(one);
    }
    void wrong_timer() {auto wrong=std::make_shared<ModelSceneData>();wrong->name="KEIKATIMER";ram->data->append(wrong);}
};

void mutable_code_and_parameters_after_clock() {
    Fixture f;ModelSceneCommand command("beModelCtrlMOVEEX");command.name.reset();
    command.value=command.frames=command.end_time=std::numeric_limits<float>::quiet_NaN();
    f.motion->position_locked=1;
    const auto replacement=std::make_shared<ModelSceneOne>();replacement->name="ONE";
    replacement->node=f.resource.mutable_node(f.authored.nodes[1]);*replacement->node->matrix_owner()=identity;
    (*replacement->node->matrix_owner())[8]=1;(*replacement->node->matrix_owner())[10]=0;
    std::shared_ptr<ModelSceneTimer> started;
    f.clock_hook=[&] {
        check(f.motion->position_locked==1 && f.fields->movement->size()==0,"MOVE read/unlocked work before timer start.");
        started=std::dynamic_pointer_cast<ModelSceneTimer>(find_model_named(f.ram->data,"KEIKATIMER"));
        check(started && started->timer.cached_frames==0,"Clock did not observe the published timer defaults.");
        auto data=std::make_shared<ModelSceneList>();data->append(replacement);data->append(started);f.ram->data=data;
        command.name="TXPA";command.value=12;command.frames=4;command.end_time=9;
        command.stop_on_sequence=2;command.stop_value=3;command.stop_frames=5;
    };
    apply_model_command(f.state,command);
    const auto channel=f.fields->movement->entry_owner(0);
    check(f.clocks==1 && started->timer.saved_world_time==123456789 && f.timer->exists("KEIKATIMER"),
        "Raw MOVE duplicated or omitted its actual timer prefix.");
    check(!f.motion->position_locked && f.motion->turn.desired[1]==0x1.921fb6p+0f &&
        f.motion->turn.applied[1]==f.motion->turn.desired[1],"MOVE did not resolve the fresh ONE node after clock mutation.");
    check(channel->active && channel->mode==MotionMode::accelerate_positive && channel->value==12 &&
        channel->acceleration==1.5f && channel->end_time==9 && channel->stop_on_sequence &&
        channel->stop_value==3 && channel->stop_frames==5,"MOVEEX used an eagerly copied code or parameter snapshot.");
}

void invalid_codes_preserve_move_prefix() {
    for(const auto name:std::array<std::optional<std::string>,4>{std::nullopt,"T","TXPQ","TQPA"}) {
        Fixture f;f.motion->position_locked=1;ModelSceneCommand command("beModelCtrlMOVEEX");command.name=name;
        command.value=2;command.frames=3;
        rejects([&]{apply_model_command(f.state,command);},"Invalid raw MOVEEX code was accepted.");
        const auto timer=find_model_timer(*f.ram,"KEIKATIMER");
        check(timer && timer->timer.saved_world_time==123456789 && f.clocks==1 && !f.motion->position_locked &&
            f.fields->movement->size()==4,"MOVEEX rejected its code before timer/unlock/channel-allocation publication.");
        for(const auto& channel:f.fields->movement->mutable_entries())check(channel && !channel->active,
            "Invalid MOVEEX partially configured a channel or omitted its default allocation.");
    }
    Fixture f;ModelSceneCommand command("beModelCtrlMOVEEX");command.name="TXPA";
    f.clock_hook=[&]{command.name="invalid";};
    rejects([&]{apply_model_command(f.state,command);},"MOVEEX reused a valid code copied before the clock callback.");
    check(f.fields->movement->size()==4 && f.clocks==1,"Mutated code rejection lost the original allocation prefix.");
}

void clock_and_heading_failure_prefixes() {
    Fixture clock;clock.motion->position_locked=1;ModelSceneCommand move("beModelCtrlMOVE");move.name="Move";
    clock.clock_hook=[] {throw MotionError("Synthetic actual clock failure.");};
    rejects([&]{apply_model_command(clock.state,move);},"Clock failure was hidden by raw MOVE.");
    const auto timer=find_model_timer(*clock.ram,"KEIKATIMER");
    check(timer && timer->timer.saved_world_time==0 && timer->timer.cached_frames==0 &&
        clock.motion->position_locked==1 && clock.fields->movement->size()==0,"Clock failure changed later MOVE fields or discarded timer append.");
    Fixture heading;heading.motion->position_locked=1;heading.one->node.reset();
    rejects([&]{apply_model_command(heading.state,move);},"MOVE accepted a missing fresh node.");
    check(heading.clocks==1 && !heading.motion->position_locked && heading.fields->movement->size()==0 &&
        find_model_timer(*heading.ram,"KEIKATIMER")->timer.cached_frames==1,"Heading failure lost timer/unlock or allocated channels too early.");
    Fixture list;list.fields->movement.reset();list.motion->position_locked=1;
    rejects([&]{apply_model_command(list.state,move);},"MOVE fabricated an absent actual movement-list owner.");
    check(list.clocks==1 && !list.motion->position_locked && !list.fields->movement,
        "Movement-list failure ran before the original timer/unlock prefix.");
}

void muteki_live_flags_and_ignored_fields() {
    Fixture f;ModelSceneCommand command("beModelCtrlMUTEKI");command.name="numeric";
    command.frames=std::numeric_limits<float>::quiet_NaN();f.motion->position_locked=1;
    f.clock_hook=[&]{command.name="OFFWALL";};
    apply_model_command(f.state,command);
    check(f.clocks==1 && f.fields->wall_check==0 && f.motion->position_locked==1 && f.fields->movement->size()==0,
        "MUTEKI read its old name/NaN frames or ran MOVE's extra prefix.");
    command.name="unknown";command.frames=17;apply_model_command(f.state,command);
    check(f.clocks==1 && f.fields->invulnerability_frames==17,"MUTEKI restarted an existing actual timer or retained old numeric fields.");
    Fixture mutated;ModelSceneCommand number("beModelCtrlMUTEKI");number.name="OFFFLOOR";number.frames=1;
    mutated.clock_hook=[&]{number.name.reset();number.frames=27;};apply_model_command(mutated.state,number);
    check(mutated.fields->floor_check==1 && mutated.fields->invulnerability_frames==27,
        "MUTEKI did not use its post-clock null-name fallback and fresh numeric field.");
    Fixture invalid;ModelSceneCommand bad("beModelCtrlMUTEKI");bad.name="numeric";bad.frames=std::numeric_limits<float>::infinity();
    rejects([&]{apply_model_command(invalid.state,bad);},"Numeric MUTEKI accepted unsupported nonfinite frames.");
    check(invalid.clocks==1 && find_model_timer(*invalid.ram,"KEIKATIMER")->timer.cached_frames==1 &&
        invalid.fields->invulnerability_frames==0,"Checked MUTEKI frames failure preceded or rolled back timer start.");
    for(const auto* flag:{"OFFWALL","ONWALL","OFFFLOOR","ONFLOOR"}) {
        Fixture wrong;wrong.wrong_timer();ModelSceneCommand set("beModelCtrlMUTEKI");set.name=flag;set.frames=bad.frames;
        apply_model_command(wrong.state,set);check(wrong.clocks==0,"Flag-only MUTEKI consumed a wrong-type existing timer payload.");
    }
}

void ignored_move_numbers_and_shared_variant_prefix() {
    Fixture f;f.wrong_timer();ModelSceneCommand command("beModelCtrlMOVE");command.name="unrecognized";
    command.value=command.frames=std::numeric_limits<float>::quiet_NaN();
    apply_model_command(f.state,command);check(f.clocks==0 && f.fields->movement->size()==4,
        "Unknown MOVE validated unused parameters or typed an existing timer.");
    command.name.reset();apply_model_command(f.state,command);
    check(f.clocks==0 && !f.fields->movement->entry_owner(2)->active,"Null MOVE fallback configured a channel or queried time.");
    Fixture standard;MotionCommand move;move.type="beModelCtrlMOVE";move.name="Move";move.channel=2;
    move.setup={MotionMode::accelerate_positive,2,4,0};apply_model_command(standard.state,ModelCommand{move});
    check(standard.clocks==1 && standard.fields->movement->size()==4 && standard.fields->movement->entry_owner(2)->value==2,
        "Existing actual ModelCommand path duplicated its shared timer/allocation prefix.");
}

void raw_work_only_commands() {
    Fixture f;f.one->node.reset();f.fields->movement.reset();f.motion->position_locked=1;
    f.clock_hook=[] {throw MotionError("CTRL/PDMOVE must not read the clock.");};
    ModelSceneCommand control("beModelCtrlCTRL");control.name="onWalk";control.frames=std::numeric_limits<float>::quiet_NaN();
    apply_model_command(f.state,control);check(f.fields->mode==1 && f.fields->phase==0 && f.clocks==0,
        "CTRL inspected unrelated node/movement/timer fields.");
    ModelSceneCommand pad("beModelCtrlPDMOVE");pad.name="Move";pad.value=6;pad.frames=4;apply_model_command(f.state,pad);
    check(f.state.pad_motion.values()==std::optional{std::array<float,4>{0,0,6,4}} && f.clocks==0 &&
        f.motion->position_locked==1 && !f.fields->movement,"PDMOVE lost direct field ownership or accessed unrelated motion.");
    Fixture partial;ModelSceneCommand broken("beModelCtrlPDMOVE");broken.name="Move";broken.value=8;
    broken.frames=std::numeric_limits<float>::infinity();
    rejects([&]{apply_model_command(partial.state,broken);},"Raw PDMOVE accepted invalid consumed frames.");
    check(partial.state.pad_motion.size()==4 && partial.state.pad_motion.values()==std::optional{std::array<float,4>{0,0,8,0}} &&
        partial.clocks==0,"Raw PDMOVE prevalidated frames before initialization/value store or touched time.");
}
void actual_post_input_reads_and_publication() {
    auto work=make_model_player_work();work->control.movement.reset();work->control.pad_motion.reset();
    work->key_event_name="SQ_NONE";work->control.mode=4;
    check(!update_model_control(*work),"Idle actual control consumed unused current-name or movement fields.");
    work->control.mode=2;work->key_input=2;
    check(!update_model_control(*work) && work->key_event_name=="SQ_NONE",
        "Actual toWalk lost its exact key-input==1 gate.");
    work->key_input=1;
    rejects([&]{update_model_control(*work);},"Actual dispatch accepted a reached null current-sequence name.");
    check(work->key_event_name=="SQ_WALK","A later dispatch comparison rolled back the original request publication.");
    work->sequence.name="SQ_WALK";work->control.mode=1;
    check(!update_model_control(*work),"A preexisting request read unused onWalk fields or dispatched the current sequence.");
    work->sequence.name="SQ_WAIT";
    check(update_model_control(*work)=="SQ_WALK","Actual direct dispatch lost the current requested name.");
    work->key_event_name="SQ_NONE";work->sequence.name.reset();work->control.phase=99;
    work->control.movement=std::make_shared<MotionChannelList>();
    work->control.movement->mutable_entries().resize(3);
    check(!update_model_control(*work),"An unused null channel was dereferenced after an inactive onWalk phase.");
    work->control.movement->mutable_entries().resize(2);
    rejects([&]{update_model_control(*work);},"Actual onWalk skipped its physical channel-2 pointer read before the phase gate.");
    auto channel=std::make_shared<MotionState>();channel->speed=3;
    work->control.movement->mutable_entries()={channel,{},channel};
    work->control.pad_motion=std::make_shared<ModelPadMotionList>();
    PadMotionParameters parameters(work->control.pad_motion);parameters.set_values({2,4,8,6});parameters.clear();
    work->control.phase=0;work->key_input=255;
    check(!update_model_control(*work) && work->control.phase==1 && channel->active && channel->value==8 &&
        channel->acceleration==8.0f/18.0f && channel->speed==3 && work->control.pad_motion->size()==0,
        "Actual onWalk failed aliased channel identity, nonzero-key gate or count-independent PDMOVE backing.");
    work->control.phase=2;work->key_input=255;work->control.pad_motion.reset();
    check(!update_model_control(*work) && work->control.phase==2,
        "Actual phase-2 control consumed parameters without exact key-input one.");
    work->key_event_name.reset();
    rejects([&]{update_model_control(*work);},"A null actual request was substituted with an empty or idle name.");
}
}
int main() {
    try {
        mutable_code_and_parameters_after_clock();invalid_codes_preserve_move_prefix();clock_and_heading_failure_prefixes();
        muteki_live_flags_and_ignored_fields();ignored_move_numbers_and_shared_variant_prefix();raw_work_only_commands();
        actual_post_input_reads_and_publication();
        std::cout<<"Actual model control tests passed: "<<checks<<" checks.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
