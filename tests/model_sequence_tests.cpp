#include "dw4/model_sequence.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace fixture=dw4::test;
int checks{};
constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {++checks;try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid sequence accepted.");}
ModelControlState model() {ModelMotionPose pose(identity);set_player_turn_rates(pose);return ModelControlState(pose);}
struct Fixture : fixture::ScriptFixture {
    struct Command {std::uint32_t ref;std::string type;std::optional<std::uint32_t> values,scope;};
    std::vector<Command> commands;
    std::vector<std::uint32_t> sequences;
    std::uint32_t seq{},list_ref{},sequence_storage{};
    std::uint32_t command(std::string type,std::string name) {
        const auto ref=add(fixture::named_object);text(ref,2,name);commands.push_back({ref,std::move(type),{}, {}});return ref;
    }
    std::uint32_t move(std::string name,float value,float frames) {
        const auto ref=command("beModelCtrlMOVE",std::move(name));
        commands.back().values=memory(fixture::words({std::bit_cast<std::uint32_t>(value),std::bit_cast<std::uint32_t>(frames)}));return ref;
    }
    std::uint32_t label_control(std::string label,std::optional<std::string> scope) {
        const auto ref=command("beModelCtrlLABELCTRL",std::move(label));fixture::Bytes bytes;
        if(scope) {fixture::push32(bytes,static_cast<std::uint32_t>(scope->size()+1));fixture::name(bytes,*scope);}
        else fixture::push32(bytes,0);
        commands.back().scope=memory(std::move(bytes));return ref;
    }
    std::uint32_t sequence(std::string name,const std::vector<std::uint32_t>& contents) {
        const auto ref=add(fixture::lua_data);text(ref,2,name);scalar(ref,3,list(fixture::named_list,contents));
        sequences.push_back(ref);return ref;
    }
    void finish() {seq=add(fixture::derived_data);text(seq,2,"SEQ");list_ref=list(fixture::named_list,sequences);scalar(seq,3,list_ref);sequence_storage=memory(fixture::words({0}));}
    IgbDocument document() const {
        auto doc=ScriptFixture::document();
        doc.metadata.object_types[fixture::lua_data].name="beModelCtrlData";
        doc.metadata.object_types[fixture::derived_data].name="beModelCtrlInfoDataSeq";
        doc.metadata.field_types[8].name="igFloatMetaField";
        auto& root=doc.objects.at(doc.references.at(seq).value_index);
        root.fields.back().slot=4;
        root.fields.push_back({3,1,doc.memories.at(doc.references.at(sequence_storage).value_index).offset,4});
        for(const auto& command:commands) {
            auto descriptor=doc.metadata.object_types[fixture::named_object];descriptor.name=command.type;
            descriptor.parent_index=fixture::named_object;
            auto& obj=doc.objects.at(doc.references.at(command.ref).value_index);
            obj.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(descriptor));
            if(command.values) {
                const auto& memory=doc.memories.at(doc.references.at(*command.values).value_index);
                obj.fields.push_back({3,8,memory.offset,4});obj.fields.push_back({4,8,memory.offset+4,4});
            }
            if(command.scope) {
                const auto& memory=doc.memories.at(doc.references.at(*command.scope).value_index);
                obj.fields.push_back({3,0,memory.offset,memory.size});
            }
        }
        return doc;
    }
    ModelSequenceProgram program() const {return decode_model_sequences(document(),seq,"synthetic/player.igb");}
};
Fixture branches() {
    Fixture f;std::vector<std::uint32_t> idle,walk;
    const auto stop=f.move("Stop",1,4),move=f.move("Move",0.7f,6);
    const auto to_walk=f.command("beModelCtrlCTRL","toWalk"),to_wait=f.command("beModelCtrlCTRL","toWait");
    for(const auto name:{"dual","slash","arm","gun"}) {
        const auto label=f.command("beModelCtrlLABEL",name);
        idle.insert(idle.end(),{label,to_walk,stop,f.command("beModelCtrlACTION",std::string("wait_")+name)});
        walk.insert(walk.end(),{label,to_wait,move,f.command("beModelCtrlACTION",std::string("run_")+name)});
    }
    f.sequence("SQ_WAIT",idle);f.sequence("SQ_WALK",walk);f.finish();return f;
}
void command_prefix_and_branches() {
    auto f=branches();auto program=f.program();
    check(program.sequences().size()==2 && program.sequences()[0].commands.size()==16,"Command occurrences were deduplicated.");
    const auto action_ref=program.sequences()[0].commands[3].reference_index;
    ModelSequenceRuntime runtime(std::move(program),model(),"actor",3);
    rejects([&]{runtime.run(42);});runtime.select("SQ_WAIT");
    auto result=runtime.run(42);
    check(result.stop==ModelSequenceStop::action_required && result.commands_executed==3 && result.command_reference==action_ref,
        "Missing action handler did not preserve the executed prefix and exact boundary.");
    check(runtime.command_index()==3 && runtime.model().mode==2 && runtime.model().elapsed_timer,
        "Sequence prefix did not apply its original CTRL/MOVE effects.");
    const auto timer=runtime.model().elapsed_timer;
    result=runtime.run(99);
    check(result.commands_executed==0 && runtime.model().elapsed_timer==timer,"Retry replayed the movement prefix or restarted its timer.");
    rejects([&]{runtime.run(42,[](const auto&){throw std::runtime_error("Unavailable action manager.");});});
    check(runtime.command_index()==3,"Failed external action was consumed.");
    std::vector<ModelActionRequest> actions;
    result=runtime.run(42,[&](const auto& request){actions.push_back(request);});
    check(result.stop==ModelSequenceStop::label_boundary && result.commands_executed==1 && runtime.command_index()==4,
        "Command execution fell through into another weapon section.");
    check(actions.size()==1 && actions[0].action=="wait_dual" && actions[0].source=="synthetic/player.igb" &&
        actions[0].command_reference==action_ref && actions[0].info_name=="actor" && actions[0].ram_no==3,
        "ACTION request lost its source, recipient or payload.");
    check(runtime.run(42).commands_executed==0 && runtime.label_count()==1,"LABEL barrier reset on another run call.");
    std::uint32_t index=0;
    for(const auto label:{"dual","slash","arm","gun"}) {
        runtime.set_label("SQ_WALK",label);runtime.select("SQ_WALK");actions.clear();
        result=runtime.run(42,[&](const auto& request){actions.push_back(request);});
        check(actions.size()==1 && actions[0].action==std::string("run_")+label && runtime.model().mode==3,
            "Selected weapon section dispatched the wrong action/control mode.");
        check(runtime.command_index()==4*(index+1) && runtime.labels()[0].cached_offset==static_cast<std::int32_t>(4*index),
            "Label selection or cached ordinal changed.");
        check(result.stop==(index==3 ? ModelSequenceStop::end : ModelSequenceStop::label_boundary),"Sequence end and label boundary were conflated.");++index;
    }
    auto copy=runtime;copy.set_label("SQ_WALK","dual");copy.select("SQ_WALK");
    check(runtime.current_sequence()=="SQ_WALK" && runtime.command_index()==16 && runtime.labels()[0].label=="gun",
        "Copied players share mutable label/cursor state.");
    runtime.set_label("SQ_WALK","missing");const auto saved=runtime.model().channels.states();
    rejects([&]{runtime.select("SQ_WALK");});
    check(runtime.command_index()==16 && runtime.model().channels.states()==saved && runtime.labels()[0].cached_offset==-1,
        "Missing label partially selected or silently fell back to the first section.");
    rejects([&]{runtime.select("SQ_ABSENT");});rejects([&]{runtime.select("SQ_ERAS");});
}
void motion_sequence_stop() {
    MotionChannels channels;channels.on_sequence_change();check(!channels.allocated(),"Sequence change allocated unused channels.");
    for(std::uint32_t i=0;i<4;++i) {
        MotionCommand command;command.type="beModelCtrlMOVEEX";command.channel=i;
        command.setup={i==2 ? MotionMode::impulse_positive : MotionMode::accelerate_positive,2,4,12};
        command.stop=MotionStop{i!=1,1,2};channels.apply(command);
    }
    channels.advance(1);const auto before=channels.states();channels.on_sequence_change();const auto& after=channels.states();
    check(after[0].mode==MotionMode::stop_positive && after[0].active && after[0].speed==before[0].speed && !after[0].stop_on_sequence,
        "Flagged accelerating XYZ channel did not enter its stored stop mode.");
    check(after[1]==before[1],"Unflagged XYZ channel changed at sequence entry.");
    check(!after[2].active && after[2].speed==0,"Flagged impulse channel did not stop immediately.");
    auto rotation=before[3];rotation.active=false;rotation.speed=0;
    check(after[3]==rotation,"Rotation reset changed fields other than active and speed.");
    auto f=branches();auto state=model();state.channels=channels;state.mode=4;state.phase=8;state.check_fall=false;
    state.pad_motion=std::array<float,4>{1,2,3,4};state.pose.position_locked=true;
    start_motion_timer(state.elapsed_timer.emplace(),42);
    const auto timer=state.elapsed_timer;ModelSequenceRuntime runtime(f.program(),state,"actor",0);runtime.select("SQ_WAIT");
    check(runtime.model().mode==0 && runtime.model().phase==0 && runtime.model().check_fall && !runtime.model().pad_motion,
        "Owned sequence preparation did not reset original control fields.");
    check(runtime.model().elapsed_timer==timer && runtime.model().pose.position_locked,"Sequence preparation invented timer restart or position unlock.");
}
void labels_jumps_and_guards() {
    Fixture f;
    const auto label=f.command("beModelCtrlLABEL","chosen"),action=f.command("beModelCtrlACTION","selected");
    const auto selection=f.label_control("chosen","SQ_B"),null_selection=f.label_control("null-key",std::nullopt);
    const auto empty_selection=f.label_control("empty-key",std::string{});
    f.sequence("SQ_A",{selection,null_selection,empty_selection,f.command("beModelCtrlJUMP","SQ_B")});
    f.sequence("SQ_B",{f.command("UnknownCommand","unused"),label,action});
    f.sequence("SQ_ERASE_TEST",{f.command("beModelCtrlJUMP","SQ_ERAS")});
    f.sequence("SQ_MISSING",{f.command("beModelCtrlJUMP","SQ_ABSENT")});
    f.sequence("custom",{f.command("beModelCtrlCTRL","noFall")});
    f.sequence("SQ_CUSTOM",{f.command("beModelCtrlJUMP","custom"),f.command("UnknownCommand","unreachable")});
    f.sequence("ONCTRL",{});f.sequence("onctrl",{});
    f.sequence("SQ_NATIVE",{f.command("beModelCtrlJUMP","ONCTRL")});
    f.sequence("SQ_B",{});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",1);runtime.select("SQ_A");
    auto result=runtime.run(42);
    check(result.stop==ModelSequenceStop::action_required && runtime.current_sequence()=="SQ_B" && runtime.command_index()==2 &&
        runtime.label_count()==1,"Nested JUMP restored the old command cursor or selected the duplicate sequence.");
    check(runtime.labels().size()==3 && !runtime.labels()[1].sequence && runtime.labels()[2].sequence==std::optional<std::string>{""},
        "LABELCTRL conflated null and empty sequence keys.");
    const auto& labels=runtime.labels();check(labels[0].label=="chosen" && labels[0].cached_offset==1,"LABELCTRL failed to cache the selected LABEL ordinal.");
    rejects([&]{runtime.run(42,[&](const auto&){runtime.select("SQ_A");});});
    check(runtime.command_index()==2,"Reentrant action dispatch consumed the action.");
    rejects([&]{runtime.run(42,[&](const auto&){runtime.run(42);});});
    check(runtime.run(42,[](const auto&){}).stop==ModelSequenceStop::end,"Action failure left the runtime permanently reentrant.");
    runtime.select("SQ_ERASE_TEST");check(runtime.run(42).stop==ModelSequenceStop::controller_required && runtime.command_index()==0,
        "Erase command was mistaken for ordinary local sequence entry.");
    runtime.select("SQ_MISSING");check(runtime.run(42).stop==ModelSequenceStop::controller_required,"Missing jump destination was silently skipped.");
    runtime.select("SQ_CUSTOM");check(runtime.run(42).stop==ModelSequenceStop::end && runtime.current_sequence()=="custom" && !runtime.model().check_fall,
        "A valid named sequence was restricted to an invented SQ_ prefix.");
    rejects([&]{runtime.select("ONCTRL");});
    runtime.select("onctrl");check(runtime.run(42).stop==ModelSequenceStop::end,"Native command reservation lost exact case rules.");
    runtime.select("SQ_NATIVE");check(runtime.run(42).stop==ModelSequenceStop::controller_required && runtime.command_index()==0,
        "A same-named sequence bypassed the original native command table.");
    runtime.set_label("SQ_B","chosen");runtime.select("SQ_B");check(runtime.command_index()==1,"Label cache invalidation failed.");
    ModelSequenceRuntime unsupported(f.program(),model(),"actor",0);unsupported.select("SQ_B");
    result=unsupported.run(42);check(result.stop==ModelSequenceStop::unsupported_command && result.type=="UnknownCommand" && unsupported.command_index()==0,
        "Unsupported command did not stop at its actual occurrence.");
    Fixture loop;loop.sequence("SQ_LOOP",{loop.command("beModelCtrlJUMP","SQ_LOOP")});loop.finish();
    ModelSequenceRuntime cyclic(loop.program(),model(),"actor",0);cyclic.select("SQ_LOOP");
    rejects([&]{cyclic.run(42);});check(cyclic.current_sequence()=="SQ_LOOP" && cyclic.command_index()==0,"Budget failure lost the committed nested selection.");
}
void malformed() {
    auto f=branches();
    fixture::word(f.records.at(f.sequence_storage).data,0,0xffffffffu);
    check(f.program().sequences().size()==2,"SEQ inherited integer slot 3 was mistaken for the list reference at slot 4.");
    auto inherited=f.document();inherited.objects.at(inherited.references.at(f.seq).value_index).fields.back().type_index=2;
    rejects([&]{decode_model_sequences(inherited,f.seq,"source");});
    rejects([&]{decode_model_sequences(f.document(),f.seq,"");});
    rejects([&]{decode_model_sequences(f.document(),f.seq,std::string("bad\0source",10));});
    rejects([&]{decode_model_sequences(f.document(),f.sequences[0],"source");});
    auto doc=f.document();doc.metadata.header.byte_order=IgbByteOrder::big;rejects([&]{decode_model_sequences(doc,f.seq,"source");});
    doc=f.document();doc.objects.at(doc.references.at(f.list_ref).value_index).fields.back().size=3;
    rejects([&]{decode_model_sequences(doc,f.seq,"source");});
    f.contents(f.list_ref,{fixture::null_ref});rejects([&]{f.program();});
    f=branches();f.scalar(f.list_ref,3,4097);rejects([&]{f.program();});
    Fixture null_command;null_command.sequence("SQ_WAIT",{fixture::null_ref});null_command.finish();rejects([&]{null_command.program();});
    Fixture invalid;invalid.sequence("SQ_WAIT",{invalid.move("Move",std::numeric_limits<float>::infinity(),1)});invalid.finish();rejects([&]{invalid.program();});
    Fixture long_name;long_name.sequence("SQ_WAIT",{long_name.command("beModelCtrlLABEL",std::string(4097,'a'))});long_name.finish();rejects([&]{long_name.program();});
    f=branches();doc=f.document();doc.objects.at(doc.references.at(f.commands[0].ref).value_index).fields.back().offset=doc.bytes().size();
    rejects([&]{decode_model_sequences(doc,f.seq,"source");});
    Fixture aggregate;const auto label=aggregate.command("beModelCtrlLABEL","repeated");
    aggregate.sequence("SQ_A",std::vector<std::uint32_t>(32769,label));aggregate.sequence("SQ_B",std::vector<std::uint32_t>(32769,label));aggregate.finish();
    rejects([&]{aggregate.program();});
}
void original_data(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto player=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto program=decode_model_sequences(player,14,"_common/p_agumon.igb");
    check(program.sequences().size()==28,"Original Agumon sequence count changed.");
    const auto map=read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179));
    const std::array<const char*,4> labels{"dual","slash","arm","gun"};
    const std::array<const char*,4> waits{"WAITD1","WAITS1","WAITA1","WAITG1"},runs{"RUNND1","RUNNS1","RUNNA1","RUNNG1"};
    for(std::uint32_t group=0;group<2;++group)for(std::uint32_t slot=0;slot<4;++slot)for(std::size_t weapon=0;weapon<4;++weapon) {
        const auto placement=decode_igb_player_placement(map,115,group,slot);
        ModelMotionPose pose(placement.matrix);set_player_turn_rates(pose);synchronize_model_heading(pose);
        ModelSequenceRuntime runtime(program,ModelControlState(pose),"p_agumon",static_cast<std::int32_t>(slot));
        runtime.set_label("SQ_WAIT",labels[weapon]);runtime.set_label("SQ_WALK",labels[weapon]);
        std::vector<std::string> actions;
        // Supplied service boundary records requests only. This fixture does not
        // claim that ACTION manager animation or outer player callbacks ran.
        const auto dispatch=[&](const ModelActionRequest& request){actions.push_back(request.action);};
        runtime.select("SQ_WAIT");runtime.run(42,dispatch);
        const auto walk=update_model_control(runtime.model(),1,runtime.current_sequence(),"SQ_NONE");
        check(walk.dispatch_sequence && walk.requested_sequence=="SQ_WALK","Original WAIT control did not request WALK.");
        runtime.select(walk.requested_sequence);runtime.run(42,dispatch);
        for(int i=0;i<18;++i)advance_model_motion(runtime.model(),42);
        const auto idle=update_model_control(runtime.model(),0,runtime.current_sequence(),"SQ_NONE");
        check(idle.dispatch_sequence && idle.requested_sequence=="SQ_WAIT","Original WALK control did not request WAIT.");
        runtime.select(idle.requested_sequence);runtime.run(42,dispatch);
        for(int i=0;i<6;++i)advance_model_motion(runtime.model(),42);
        check(actions==std::vector<std::string>{waits[weapon],runs[weapon],waits[weapon]},"Original sequence crossed weapon/action branches.");
        for(std::size_t axis=0;axis<3;++axis)check(std::abs(runtime.model().pose.matrix[12+axis]-(placement.matrix[12+axis]+placement.matrix[8+axis]*8.275f))<0.001f,
            "Sequence-driven movement lost its original scalar distance or entry basis.");
    }
}
} // namespace
int main(int argc,char** argv) {
    try {command_prefix_and_branches();motion_sequence_stop();labels_jumps_and_guards();malformed();
        if(argc==2)original_data(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_sequence_tests [EXTRACTED_ROOT]");
        std::cout<<"Model sequence tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Model sequence failure after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
