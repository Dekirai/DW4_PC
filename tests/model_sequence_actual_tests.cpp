#include "dw4/model_scene.hpp"
#include "dw4/model_sequence.hpp"
#include "dw4/model_timer.hpp"
#include "dw4/resources.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& action) {
    ++checks;try {action();}catch(const std::exception&) {return;}
    throw std::runtime_error("An invalid actual sequence operation was accepted.");
}
std::shared_ptr<ModelSceneCommand> command(std::string type,std::optional<std::string> name={}) {
    auto value=std::make_shared<ModelSceneCommand>(std::move(type));value->name=std::move(name);return value;
}
std::shared_ptr<ModelSceneList> list(ModelSceneList::Storage entries) {
    auto value=std::make_shared<ModelSceneList>("igNamedObjectList");
    value->count=value->capacity=static_cast<std::int32_t>(entries.size());
    value->backing=std::make_shared<ModelSceneList::Storage>(std::move(entries));return value;
}
std::shared_ptr<ModelSceneSequence> sequence(ModelSceneList::Storage entries) {
    auto value=std::make_shared<ModelSceneSequence>();value->commands=list(std::move(entries));return value;
}
struct Fixture {
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSequenceFields> fields=std::make_shared<ModelSequenceFields>();
    std::shared_ptr<ModelMotionFields> motion=std::make_shared<ModelMotionFields>();
    std::shared_ptr<ModelControlFields> control=std::make_shared<ModelControlFields>();
    unsigned clocks{};
    std::function<void()> during_clock;
    std::shared_ptr<ModelRamTimerAccess> timer=std::make_shared<ModelRamTimerAccess>(ram,
        [this](const ModelSceneRam&){++clocks;if(during_clock)during_clock();return std::uint64_t{100};});
    ModelSequenceRuntime runtime{ModelControlState(ModelMotionPose::fields_only(motion),control,timer),fields};
    explicit Fixture(std::shared_ptr<ModelSceneRam> actual=std::make_shared<ModelSceneRam>()) :ram(std::move(actual)) {}
    std::shared_ptr<ModelSequenceActualFrame> enter(std::shared_ptr<ModelSceneSequence> data,
        std::optional<std::string> name="SQ_TEST") {
        auto frame=runtime.make_actual_frame();runtime.begin_actual_select(frame,std::move(name),std::move(data));
        runtime.finish_actual_select(frame);return frame;
    }
};
void entry_tests() {
    Fixture f;f.control->mode=8;f.control->phase=9;f.fields->wait={"old",0x80,-2,5};
    f.control->invulnerability_frames=std::numeric_limits<float>::quiet_NaN();
    const auto data=sequence({command("beModelCtrlCTRL","toWalk"),command("beModelCtrlWCHECK","end")});
    const auto pad=f.control->pad_motion;
    auto frame=f.runtime.make_actual_frame();f.runtime.begin_actual_select(frame,"SQ_FIRST",data);
    check(f.fields->name=="SQ_FIRST" && f.fields->command_data.actual_owner()==data && frame->selection_pending(),
        "Actual selection did not publish its exact data owner before cleanup.");
    check(f.control->mode==0 && f.control->phase==0 && f.control->pad_motion==pad &&
        f.fields->wait==ModelWaitState{std::nullopt,0x80,-2,5},"Actual early reset replaced owners or wait scalar fields.");
    check(f.clocks==0,"Actual sequence entry queried a timer before a consuming command.");
    rejects([&]{(void)f.runtime.model();});
    f.runtime.finish_actual_select(frame);
    check(std::isnan(f.control->invulnerability_frames),"Actual cleanup rejected or clamped unordered invulnerability.");
    const auto result=f.runtime.run_actual(frame);
    check(result.stop==ModelSequenceStop::waiting && frame->completed() && f.fields->command_index==2 &&
        f.control->mode==2 && f.clocks==0,"Fields-only CTRL/WCHECK failed or consumed an unrelated node/clock.");
    rejects([&]{(void)std::as_const(f.runtime).model().pose.matrix();});
    rejects([&]{(void)f.fields->command_data.prepared_owner();});
    rejects([&]{f.runtime.select("SQ_FIRST");});

    Fixture nullable;auto null_frame=nullable.enter(sequence({}),std::nullopt);
    check(!nullable.fields->name && nullable.runtime.run_actual(null_frame).stop==ModelSequenceStop::end,
        "A nullable sequence name was replaced by empty or rejected before consumption.");

    Fixture missing;missing.fields->command_index=19;auto absent=missing.runtime.make_actual_frame();
    missing.runtime.begin_actual_select(absent,"absent",{});missing.runtime.finish_actual_select(absent);
    rejects([&]{missing.runtime.run_actual(absent);});
    check(missing.fields->name=="absent" && !missing.fields->command_data && missing.fields->command_index==0,
        "Null selected data lost the already completed ordinary-entry prefix.");

    Fixture label;label.runtime.set_label("missing","target");label.fields->command_index=23;
    auto bad=label.runtime.make_actual_frame();
    rejects([&]{label.runtime.begin_actual_select(bad,"missing",{});});
    check(label.fields->name=="missing" && !label.fields->command_data && label.fields->command_index==23 && bad->faulted(),
        "Label lookup failure rolled back name/data or prematurely published the cursor.");

    Fixture cached;cached.runtime.set_label("bad-offset","absent");auto invalid=cached.runtime.make_actual_frame();
    cached.runtime.begin_actual_select(invalid,"bad-offset",sequence({command("beModelCtrlLABEL","other")}));
    check(cached.fields->command_index==-1,"Unresolved actual label was repaired or rejected before cursor publication.");
    cached.runtime.finish_actual_select(invalid);rejects([&]{cached.runtime.run_actual(invalid);});

    Fixture cleanup;auto moving=std::make_shared<MotionState>();moving->active=true;
    moving->mode=MotionMode::impulse_positive;moving->stop_on_sequence=true;
    cleanup.control->movement=std::make_shared<MotionChannelList>(MotionChannelList::Entries{moving,{}});
    cleanup.control->bullet_hit_script="old";cleanup.control->invulnerability_frames=-4;
    auto partial=cleanup.runtime.make_actual_frame();cleanup.runtime.begin_actual_select(partial,"cleanup",sequence({}));
    rejects([&]{cleanup.runtime.finish_actual_select(partial);});
    check(!moving->active && !cleanup.control->bullet_hit_script && cleanup.control->wall_check==1 &&
        cleanup.control->floor_check==1 && cleanup.control->check_fall==1 && cleanup.control->invulnerability_frames==0,
        "Later invalid movement entry rolled back earlier cleanup fields or stops.");
}
void capture_tests() {
    Fixture f;const auto first=command("beModelCtrlACTION","first");
    const auto tail=command("beModelCtrlWCHECK","old tail");auto original=sequence({first,tail});
    const auto replacement=sequence({command("beModelCtrlWCHECK","replacement")});
    auto frame=f.enter(original);unsigned identities=0,actions=0;std::string live_name="actor";
    ModelSequenceActualServices services;
    services.identity=[&]{++identities;return ModelSequenceIdentity{"source",live_name,static_cast<std::int32_t>(identities)};};
    services.action=[&](const ModelActionRequest& request) {
        ++actions;check(request.action=="first" && request.info_name=="actor" && request.ram_no==1,
            "A resumed command rebuilt its already consumed request from changed owners.");
        f.fields->command_data=replacement;f.fields->command_index=77;live_name="changed";first->name="mutated";
        if(actions==1)return false;
        original->commands=list({first,command("beModelCtrlWCHECK","new physical tail")});return true;
    };
    auto result=f.runtime.run_actual(frame,services);
    check(result.stop==ModelSequenceStop::action_required && !frame->completed() && f.fields->command_index==77,
        "A host suspension published the command loop's local cursor.");
    rejects([&]{(void)f.runtime.model();});
    rejects([&]{(void)f.runtime.make_actual_frame();});
    result=f.runtime.run_actual(frame,services);
    check(frame->completed() && result.stop==ModelSequenceStop::waiting && identities==1 && actions==2 &&
        f.fields->command_index==2 && f.fields->wait.name=="new physical tail" && f.fields->command_data.actual_owner()==replacement,
        "Command loop retargeted its captured data, cached a replaced command list, or lost local cursor writeback.");

    f.fields->wait.name.reset();f.fields->command_index=0;
    auto resumed=f.runtime.make_actual_resume_frame();result=f.runtime.run_actual(resumed);
    check(resumed->completed() && f.fields->wait.name=="replacement" && f.fields->command_index==1,
        "A later command-loop invocation failed to capture current work data and cursor.");

    Fixture logical;auto data=sequence({command("beModelCtrlWCHECK","capacity")});data->commands->count=0;
    auto empty=logical.enter(data);check(logical.runtime.run_actual(empty).stop==ModelSequenceStop::end,
        "An empty logical command list consumed its retained backing.");
    logical.fields->command_index=1;data->commands->count=0;data->commands->backing->push_back(command("beModelCtrlWCHECK","slot one"));
    auto physical=logical.runtime.make_actual_resume_frame();logical.runtime.run_actual(physical);
    check(logical.fields->wait.name=="slot one" && logical.fields->command_index==2,
        "Actual != count loop was incorrectly replaced by a logical prefix bounds check.");
}
void nested_tests() {
    Fixture f;auto outer=f.enter(sequence({command("beModelCtrlJUMP","child"),command("beModelCtrlWCHECK","wrong")}));
    const auto child_data=sequence({command("beModelCtrlACTION","child action"),command("beModelCtrlWCHECK","child wait")});
    std::shared_ptr<ModelSequenceActualFrame> child;unsigned jumps=0,actions=0;
    ModelSequenceActualServices child_services;
    child_services.identity=[] {return ModelSequenceIdentity{"source","actor",0};};
    child_services.action=[&](const ModelActionRequest&){return ++actions>1;};
    ModelSequenceActualServices outer_services;
    outer_services.jump=[&](std::string_view name) {
        ++jumps;check(name=="child","JUMP resumed a different command name.");
        if(!child) {
            child=f.runtime.make_actual_frame();f.runtime.begin_actual_select(child,"child",child_data);
            f.runtime.finish_actual_select(child);
        }
        f.runtime.run_actual(child,child_services);return child->completed();
    };
    auto result=f.runtime.run_actual(outer,outer_services);
    check(result.stop==ModelSequenceStop::controller_required && !outer->completed() && !child->completed() &&
        f.fields->name=="child" && f.fields->command_index==0,"Nested service suspension lost the selected child invocation.");
    result=f.runtime.run_actual(outer,outer_services);
    check(result.stop==ModelSequenceStop::jump_completed && outer->completed() && child->completed() &&
        jumps==2 && actions==2 && f.fields->command_index==2 && f.fields->wait.name=="child wait",
        "Completed direct JUMP overwrote child cursor, repeated selection, or skipped a pending child.");

    Fixture reentrant;auto frame=reentrant.enter(sequence({command("beModelCtrlACTION","reenter")}));
    ModelSequenceActualServices services;services.identity=child_services.identity;
    services.action=[&](const ModelActionRequest&){rejects([&]{(void)reentrant.runtime.make_actual_frame();});return true;};
    rejects([&]{reentrant.runtime.run_actual(frame,services);});
    check(frame->faulted() && reentrant.fields->command_index==0,"Caught invalid reentry allowed the original invocation to continue.");
}
void ownership_tests() {
    Fixture f;auto selected=sequence({command("beModelCtrlACTION","retain")});
    std::weak_ptr<ModelSceneSequence> weak=selected;auto frame=f.enter(selected);
    f.runtime.run_actual(frame);selected.reset();f.fields->command_data.reset();
    check(!weak.expired(),"Suspended command data borrowed a replaceable work reference.");
    frame.reset();check(weak.expired(),"An abandoned invocation leaked its selected command-data owner.");
    rejects([&]{(void)f.runtime.make_actual_frame();});

    std::shared_ptr<ModelSequenceFields> completed_fields;
    std::shared_ptr<ModelSequenceActualFrame> completed;
    {Fixture done;completed_fields=done.fields;completed=done.enter(sequence({}));done.runtime.run_actual(completed);}
    auto motion=std::make_shared<ModelMotionFields>();auto control=std::make_shared<ModelControlFields>();
    auto ram=std::make_shared<ModelSceneRam>();auto timer=std::make_shared<ModelRamTimerAccess>(ram,[](const ModelSceneRam&){return 0;});
    ModelSequenceRuntime rebound(ModelControlState(ModelMotionPose::fields_only(motion),control,timer),completed_fields);
    check(bool(rebound.make_actual_frame()),"A retained completed frame poisoned a healthy executor generation.");
}
void external_tests() {
    Fixture f;auto hit=command("beModelCtrlHITBOX","Set");hit->hitbox.length=7;
    auto lua=command("beModelCtrlLUACALL","original");lua->scope="event";
    const auto data=sequence({hit,lua,command("beModelCtrlLABEL","section")});
    auto frame=f.enter(data);unsigned hit_calls=0,lua_calls=0,identities=0;
    ModelSequenceActualServices services;
    services.identity=[&]{++identities;return ModelSequenceIdentity{"source","actor",9};};
    services.hitbox=[&](const ModelHitboxCommand& request) {
        check(request.parameters.length==7,"A suspended HITBOX reread changed command fields.");
        hit->hitbox.length=99;return ++hit_calls>1;
    };
    services.lua=[&](const ModelLuaRequest& request) {
        check(request.script_prefix=="original" && request.event=="event","A suspended Lua request reread changed command fields.");
        lua->name="changed";lua->scope.reset();return ++lua_calls>1;
    };
    check(f.runtime.run_actual(frame,services).stop==ModelSequenceStop::hitbox_required && f.fields->command_index==0,
        "Pending HITBOX consumed the command loop's cursor.");
    check(f.runtime.run_actual(frame,services).stop==ModelSequenceStop::lua_required && f.fields->command_index==0,
        "Completed HITBOX prematurely published the still-running loop's local cursor.");
    check(f.runtime.run_actual(frame,services).stop==ModelSequenceStop::end && frame->completed() &&
        f.fields->command_index==3 && f.fields->label_count==1 && hit_calls==2 && lua_calls==2 && identities==1,
        "Actual external service continuation replayed a completed prefix or lost the final cursor.");

    Fixture failure;auto prefix=command("beModelCtrlCTRL","toWalk");
    auto fault=failure.enter(sequence({prefix,command("beModelCtrlACTION","throw")}));
    ModelSequenceActualServices bad;bad.identity=services.identity;
    unsigned calls=0;bad.action=[&](const ModelActionRequest&) -> bool {++calls;throw std::runtime_error("service failure");};
    rejects([&]{failure.runtime.run_actual(fault,bad);});
    check(fault->faulted() && failure.control->mode==2 && failure.fields->command_index==0 && calls==1,
        "An uncertain external failure rolled back command effects or wrote the local cursor.");
    rejects([&]{failure.runtime.run_actual(fault,bad);});check(calls==1,"A faulted actual callback was replayed.");
}
void original_tests(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);
    ActorSceneResource scene(read_igb_document(mounted.read("_common/p_agumon.igb")),"_common/p_agumon.igb",29);
    const auto info=scene.models().info(1386);const auto seq=find_model_seq(*info);
    const std::array<std::string,4> labels{"dual","slash","arm","gun"};
    const std::array<std::string,4> waits{"WAITD1","WAITS1","WAITA1","WAITG1"};
    const std::array<std::string,4> runs{"RUNND1","RUNNS1","RUNNA1","RUNNG1"};
    for(std::size_t i=0;i<labels.size();++i) {
        auto ram=std::dynamic_pointer_cast<ModelSceneRam>(info->rams->values()[i]);Fixture f(ram);
        f.runtime.set_label("SQ_WAIT",labels[i]);f.runtime.set_label("SQ_WALK",labels[i]);
        std::vector<std::string> actions;ModelSequenceActualServices services;
        services.identity=[&]{return ModelSequenceIdentity{info->source(),info->name.value_or(""),ram->number};};
        // This unit service records exact dispatch payloads. Animation and +B8
        // execution remain the separately tested outer lifecycle's responsibility.
        services.action=[&](const ModelActionRequest& request){actions.push_back(request.action);return true;};
        for(const auto& name:{"SQ_WAIT","SQ_WALK","SQ_WAIT"}) {
            const auto data=find_model_sequence(*seq,name);auto frame=f.enter(data,name);
            const auto result=f.runtime.run_actual(frame,services);
            check(frame->completed() && (result.stop==ModelSequenceStop::label_boundary || result.stop==ModelSequenceStop::end) &&
                f.fields->command_data.actual_owner()==data && f.fields->command_index==static_cast<std::int32_t>((i+1)*4),
                "Original weapon branch lost its actual data owner, LABEL boundary or command cursor.");
        }
        check(actions==std::vector<std::string>{waits[i],runs[i],waits[i]} && f.clocks==1 &&
            bool(find_model_timer(*ram,"KEIKATIMER")),"Original actual WAIT/WALK branches lost their ACTION names or named timer owner.");
    }
}
void raw_control_tests() {
    Fixture invalid;auto move=command("beModelCtrlMOVEEX","bad");
    auto frame=invalid.enter(sequence({move}));
    rejects([&]{invalid.runtime.run_actual(frame);});
    check(invalid.clocks==1 && find_model_timer(*invalid.ram,"KEIKATIMER") && frame->faulted(),
        "Actual dispatcher decoded invalid MOVEEX before its original named-timer prefix.");

    Fixture live;auto raw=command("beModelCtrlMOVEEX","bad");auto data=sequence({raw});
    auto executing=live.enter(data);
    live.during_clock=[&] {
        raw->name="TZPA";raw->value=6;raw->frames=2;
        data->commands=list({command("beModelCtrlWCHECK","different owner")});
        live.fields->command_index=55;
    };
    const auto result=live.runtime.run_actual(executing);
    check(result.stop==ModelSequenceStop::end && executing->completed() && live.fields->command_index==1 &&
        live.control->movement->entry_owner(2)->value==6 && live.control->movement->entry_owner(2)->acceleration==3 &&
        !live.fields->wait.name,"Actual control lost its captured raw command or consumed its code/parameters before the clock site.");
}
}
int main(int argc,char** argv) {
    try {entry_tests();capture_tests();nested_tests();ownership_tests();external_tests();raw_control_tests();
        if(argc==2)original_tests(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" actual sequence checks passed.\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
