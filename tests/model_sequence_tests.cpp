#include "dw4/model_sequence.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <utility>

namespace {
using namespace dw4;
namespace fixture=dw4::test;
int checks{};
constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {++checks;try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid sequence accepted.");}
ModelControlState model() {ModelMotionPose pose(identity);set_player_turn_rates(pose);return ModelControlState(pose);}
struct Fixture : fixture::ScriptFixture {
    struct Command {std::uint32_t ref;std::string type;std::optional<std::uint32_t> values,scope,hitbox_values;};
    std::vector<Command> commands;
    std::vector<std::uint32_t> sequences;
    std::uint32_t seq{},list_ref{},sequence_storage{};
    std::uint32_t command(std::string type,std::string name) {
        const auto ref=add(fixture::named_object);text(ref,2,name);commands.push_back({ref,std::move(type),{}, {},{}});return ref;
    }
    std::uint32_t wait(std::optional<std::string> name) {
        const auto ref=command("beModelCtrlWCHECK",name.value_or(""));
        if(!name)records.at(ref).values.at(2)=fixture::words({0});
        return ref;
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
    std::uint32_t muteki(std::optional<std::string> name,float frames) {
        const auto ref=command("beModelCtrlMUTEKI",name.value_or(""));
        if(!name)records.at(ref).values.at(2)=fixture::words({0});
        commands.back().values=memory(fixture::words({std::bit_cast<std::uint32_t>(frames)}));return ref;
    }
    std::uint32_t lua(std::optional<std::string> prefix,std::optional<std::string> mode) {
        const auto ref=label_control(prefix.value_or(""),std::move(mode));commands.back().type="beModelCtrlLUACALL";
        if(!prefix)records.at(ref).values.at(2)=fixture::words({0});return ref;
    }
    std::uint32_t hitbox(std::optional<std::string> name,const ModelHitboxSubcommand& value) {
        const auto ref=command("beModelCtrlHITBOX",name.value_or(""));
        if(!name)records.at(ref).values.at(2)=fixture::words({0});
        auto bytes=fixture::words({std::bit_cast<std::uint32_t>(value.kind),std::bit_cast<std::uint32_t>(value.delay),std::bit_cast<std::uint32_t>(value.length)});
        for(const auto vector:{value.offset,value.box_size})for(const auto component:vector)fixture::push32(bytes,std::bit_cast<std::uint32_t>(component));
        commands.back().hitbox_values=memory(std::move(bytes));return ref;
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
        auto vector_type=doc.metadata.field_types[8];vector_type.name="igVec3fMetaField";
        const auto vector_index=static_cast<std::uint16_t>(doc.metadata.field_types.size());doc.metadata.field_types.push_back(std::move(vector_type));
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
                obj.fields.push_back({3,8,memory.offset,4});
                if(command.type!="beModelCtrlMUTEKI")obj.fields.push_back({4,8,memory.offset+4,4});
            }
            if(command.scope) {
                const auto& memory=doc.memories.at(doc.references.at(*command.scope).value_index);
                obj.fields.push_back({3,0,memory.offset,memory.size});
            }
            if(command.hitbox_values) {
                const auto offset=doc.memories.at(doc.references.at(*command.hitbox_values).value_index).offset;
                obj.fields.push_back({3,1,offset,4});obj.fields.push_back({4,8,offset+4,4});obj.fields.push_back({5,8,offset+8,4});
                obj.fields.push_back({6,vector_index,offset+12,12});obj.fields.push_back({7,vector_index,offset+24,12});
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
    check(runtime.command_index()==3 && runtime.model().mode()==2 && runtime.model().elapsed_timer(),
        "Sequence prefix did not apply its original CTRL/MOVE effects.");
    const auto timer=runtime.model().elapsed_timer();
    result=runtime.run(99);
    check(result.commands_executed==0 && runtime.model().elapsed_timer()==timer,"Retry replayed the movement prefix or restarted its timer.");
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
        check(actions.size()==1 && actions[0].action==std::string("run_")+label && runtime.model().mode()==3,
            "Selected weapon section dispatched the wrong action/control mode.");
        check(runtime.command_index()==static_cast<std::int32_t>(4*(index+1)) && runtime.labels()[0].cached_offset==static_cast<std::int32_t>(4*index),
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
void nullable_label_storage_and_lookup() {
    Fixture f;
    const auto empty=f.command("beModelCtrlLABEL",""),chosen=f.command("beModelCtrlLABEL","chosen");
    f.sequence("SQ_A",{empty,chosen});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0);
    runtime.select("SQ_A");
    runtime.set_label(std::nullopt,std::nullopt);
    runtime.set_label(std::string{},std::string{});
    runtime.set_label("SQ_A",std::nullopt);
    check(runtime.labels().size()==3 && !runtime.labels()[0].sequence && !runtime.labels()[0].label &&
        runtime.labels()[1].sequence==std::optional<std::string>{""} && runtime.labels()[1].label==std::optional<std::string>{""},
        "SETLABEL merged null and empty sequence/name fields.");
    check(!runtime.labels()[2].label && runtime.labels()[2].cached_offset==-1,"SETLABEL did not store a null label with an invalidated cache.");
    const auto before=runtime.model().channels.states();
    rejects([&]{runtime.select("SQ_A");});
    check(runtime.current_sequence()=="SQ_A" && runtime.command_index()==0 && !runtime.selection_pending() &&
        runtime.model().channels.states()==before && !runtime.labels()[2].label,
        "Unsafe null-label lookup changed the prepared selection or normalized its stored label.");
    runtime.set_label("SQ_A",std::string{});runtime.select("SQ_A");
    check(runtime.command_index()==0 && runtime.labels()[2].cached_offset==0,"Non-null empty label failed to match the actual empty LABEL.");
    runtime.set_label("SQ_A","chosen");runtime.select("SQ_A");
    check(runtime.command_index()==1 && runtime.labels()[2].cached_offset==1,"Replacing a null/empty label lost original first-key/cache semantics.");
    auto copy=runtime;copy.set_label("SQ_A",std::nullopt);
    check(runtime.labels()[2].label=="chosen" && !copy.labels()[2].label && copy.labels()[2].cached_offset==-1,
        "Copied sequence work shared mutable nullable label state.");
    runtime.set_label(std::nullopt,"replacement");
    check(runtime.labels().size()==3 && runtime.labels()[0].label=="replacement","Null sequence-key replacement appended an unrelated row.");
}
void canonical_sequence_fields_and_command_owners() {
    const auto fixture=branches();auto program=std::make_shared<const ModelSequenceProgram>(fixture.program());
    auto fields=std::make_shared<ModelSequenceFields>();
    ModelSequenceRuntime runtime(program,model(),"actor",2,fields);
    check(runtime.fields()==fields && runtime.program_owner()==program && !fields->name && !fields->command_data &&
        fields->command_index==0 && fields->label_count==0 && !fields->labels->sequences &&
        !fields->labels->names && !fields->labels->offsets,"Fresh sequence fields fabricated selected data or label lists.");
    fields->wait={"old",0x80,-7,9};runtime.set_label("SQ_WALK","gun");runtime.select("SQ_WALK");
    const auto data=fields->command_data;const auto label_fields=fields->labels;const auto offsets=label_fields->offsets;
    check(fields->name=="SQ_WALK" && data.get()==&program->sequences()[1] &&
        !data.owner_before(program) && !program.owner_before(data.prepared_owner()) && fields->command_index==12 &&
        fields->label_count==0 && fields->wait==ModelWaitState{std::nullopt,0x80,-7,9},
        "Selection did not publish the exact source-owned data and live sequence/wait fields.");
    fields->name="independent name";fields->command_index=3;fields->label_count=-17;
    check(runtime.current_sequence()=="independent name" && runtime.command_index()==3 && runtime.label_count()==-17,
        "Sequence inspection derived competing values from a program index instead of canonical fields.");
    fields->command_data=std::shared_ptr<const ModelSequenceData>(program,&program->sequences()[0]);
    auto result=runtime.run(42);
    check(result.stop==ModelSequenceStop::action_required && result.name=="wait_dual" && fields->command_index==3,
        "The command runner ignored a live replacement of the actual command-data owner.");
    result=runtime.run(42,[](const auto&){});
    check(result.stop==ModelSequenceStop::label_boundary && fields->command_index==4 && fields->label_count==-17,
        "Signed nonzero label count was normalized or the live command index was not advanced.");
    const auto saved_data=fields->command_data;
    rejects([&]{runtime.select("SQ_ABSENT");});
    check(fields->name=="independent name" && fields->command_data==saved_data && fields->command_index==4 &&
        fields->label_count==-17 && fields->labels==label_fields && label_fields->offsets==offsets,
        "Rejected selection published a replacement name/data reference or canonical label owner.");
    auto copy=runtime;const auto copy_fields=copy.fields();
    check(copy_fields!=fields && copy_fields->command_data==fields->command_data &&
        copy_fields->labels!=label_fields && copy_fields->labels->offsets!=offsets &&
        &copy.wait_state()!=&runtime.wait_state(),"Prepared copy retained mutable canonical fields or copied immutable data ownership.");
    copy.wait_state().name="copy wait";copy_fields->name=std::string{};copy_fields->command_index=-5;
    check(!fields->wait.name && fields->name=="independent name" && fields->command_index==4,
        "Detached sequence/wait edits leaked to the original generation.");
    const auto stable_wait=&runtime.wait_state();runtime=copy;
    check(runtime.fields()==fields && fields->labels==label_fields && label_fields->offsets==offsets &&
        &runtime.wait_state()==stable_wait && fields->wait.name=="copy wait" && fields->name==std::optional<std::string>{""} &&
        fields->command_index==-5,"Snapshot publication replaced retained sequence, wait or label owners.");
    fields->name.reset();fields->command_data.reset();
    check(!runtime.selected() && runtime.current_sequence().empty() && fields->command_index==-5,
        "Null selected data implicitly reset other original sequence fields.");
    rejects([&]{runtime.run(42);});
    runtime.begin_select("SQ_WAIT");rejects([&]{runtime=copy;});
    check(runtime.selection_pending() && fields->name=="SQ_WAIT" && fields->command_index==0 &&
        &std::as_const(runtime).wait_state()==stable_wait,"Assignment abandoned an already-published selection prefix.");
    runtime.finish_select();

    std::weak_ptr<const ModelSequenceProgram> source_lifetime;
    std::shared_ptr<const ModelSequenceData> retained_data;
    {
        auto source=std::make_shared<const ModelSequenceProgram>(fixture.program());source_lifetime=source;
        auto retained_fields=std::make_shared<ModelSequenceFields>();
        ModelSequenceRuntime temporary(source,model(),"other",3,retained_fields);temporary.select("SQ_WAIT");
        retained_data=retained_fields->command_data;
    }
    check(!source_lifetime.expired() && retained_data->name=="SQ_WAIT" && retained_data->commands.size()==16,
        "Command-data ownership borrowed a destroyed facade or source program.");
    retained_data.reset();check(source_lifetime.expired(),"Immutable command-data ownership formed a back-reference cycle.");
}
void canonical_wait_survives_work_replacement_and_move() {
    Fixture fixture;fixture.sequence("SQ_A",{fixture.wait("Action"),fixture.command("beModelCtrlACTION","continued")});fixture.finish();
    auto program=std::make_shared<const ModelSequenceProgram>(fixture.program());
    struct Work {ModelSequenceFields sequence;};
    auto current_work=std::make_shared<Work>();std::weak_ptr<Work> old_work=current_work;
    auto fields=std::shared_ptr<ModelSequenceFields>(current_work,&current_work->sequence);
    ModelSequenceRuntime original(program,model(),"actor",0,fields);
    original.select("SQ_A");check(original.run(42).stop==ModelSequenceStop::waiting,"Prepared WCHECK did not establish the actual wait.");
    ModelWaitRuntime resolver;auto* stable=&original.wait_state();
    check(resolver.update(*stable,original.model().channels).stop==ModelWaitStop::action_completion_required && resolver.pending(),
        "The original wait did not pause at its missing action query.");
    current_work=std::make_shared<Work>();
    auto fresh_fields=std::shared_ptr<ModelSequenceFields>(current_work,&current_work->sequence);
    ModelSequenceRuntime fresh(program,model(),"actor",0,fresh_fields);fresh.wait_state().attack_mode=23;
    ModelSequenceRuntime moved(std::move(original));fields.reset();
    check(!old_work.expired() && &moved.wait_state()==stable && moved.fields()->command_index==1 &&
        !fresh_fields->command_data && !fresh_fields->wait.name,
        "Work replacement or facade move redirected/destroyed the pending wait and command cursor.");
    rejects([&]{resolver.resume(fresh.wait_state());});
    unsigned queries=0,actions=0;ModelWaitServices services;
    services.action_complete=[&] {++queries;return std::optional<bool>{true};};
    services.resume_commands=[&] {return moved.run(42,[&](const auto& request){check(request.action=="continued","Wrong retained command resumed.");++actions;}).stop==ModelSequenceStop::end;};
    check(resolver.resume(moved.wait_state(),services).complete() && !resolver.pending() && queries==1 && actions==1 &&
        !moved.wait_state().name && moved.command_index()==2 && fresh_fields->command_index==0 && fresh_fields->wait.attack_mode==23,
        "Wait continuation replayed an effect or wrote a replacement work generation.");
    check(resolver.resume(moved.wait_state(),services).complete() && queries==1 && actions==1,
        "Completed wait continuation replayed a query or command callback.");
}
void malformed_canonical_sequence_fields() {
    const auto fixture=branches();auto program=std::make_shared<const ModelSequenceProgram>(fixture.program());
    auto fields=std::make_shared<ModelSequenceFields>();
    rejects([&]{ModelSequenceRuntime invalid(nullptr,model(),"actor",0,fields);});
    rejects([&]{ModelSequenceRuntime invalid(program,model(),"actor",0,nullptr);});
    auto malformed=std::make_shared<ModelSequenceFields>();malformed->labels.reset();
    rejects([&]{ModelSequenceRuntime invalid(program,model(),"actor",0,malformed);});
    ModelSequenceRuntime runtime(program,model(),"actor",0,fields);runtime.select("SQ_WAIT");
    const auto selected=fields->command_data;
    fields->command_index=-1;rejects([&]{runtime.run(42);});
    check(fields->command_index==-1 && fields->command_data==selected,"A negative original command index was repaired or rebound.");
    fields->command_index=17;rejects([&]{runtime.run(42);});
    fields->command_index=16;check(runtime.run(42).stop==ModelSequenceStop::end,"The exact command-list end cursor was rejected.");
    fields->command_index=0;fields->command_data=std::make_shared<const ModelSequenceData>(*selected);
    rejects([&]{runtime.run(42);});
    auto foreign=std::make_shared<const ModelSequenceProgram>(fixture.program());
    fields->command_data=std::shared_ptr<const ModelSequenceData>(foreign,&foreign->sequences()[0]);
    rejects([&]{runtime.run(42);});
    fields->command_data=std::shared_ptr<const ModelSequenceData>(program,&foreign->sequences()[0]);
    rejects([&]{runtime.run(42);});
    check(fields->command_index==0 && fields->name=="SQ_WAIT","A copied or foreign data owner changed the live sequence before rejection.");
    fields->command_data=selected;fields->command_index=3;unsigned calls=0;
    rejects([&]{runtime.run(42,[&](const auto&){++calls;fields->command_index=std::numeric_limits<std::int32_t>::max();});});
    check(calls==1 && fields->command_index==std::numeric_limits<std::int32_t>::min(),
        "Original 32-bit cursor increment overflowed in host arithmetic or discarded a completed callback.");
}
void executor_lease_and_abandoned_services() {
    Fixture f;f.sequence("SQ_A",{f.command("beModelCtrlACTION","action")});f.finish();
    auto program=std::make_shared<const ModelSequenceProgram>(f.program());auto fields=std::make_shared<ModelSequenceFields>();
    {
        ModelSequenceRuntime runtime(program,model(),"actor",0,fields);runtime.select("SQ_A");
        rejects([&]{ModelSequenceRuntime second(program,model(),"actor",0,fields);});
        auto copy=runtime;check(copy.fields()!=fields,"A detached executor copy reused canonical fields.");
        rejects([&]{ModelSequenceRuntime second(program,model(),"actor",0,copy.fields());});
        unsigned calls=0;
        check(runtime.run(42,[&](const auto&){
            ++calls;rejects([&]{ModelSequenceRuntime moving(std::move(runtime));});
        }).stop==ModelSequenceStop::end && calls==1 && runtime.fields()==fields,
            "Moving an executing facade transferred its lease or changed its command owner.");
        runtime.wait_state().name="unqueried wait";
    }
    {
        ModelSequenceRuntime rebound(program,model(),"actor",0,fields);
        check(rebound.run(42).stop==ModelSequenceStop::waiting && rebound.wait_state().name=="unqueried wait",
            "A completed executor or an unqueried wait poisoned later binding.");
    }
    for(int kind=0;kind<8;++kind) {
        Fixture pending;
        const auto command=kind==1 || kind==7 ? pending.hitbox("Set",{}) :
            kind==2 || kind==5 ? pending.lua("prefix","event") :
            kind==4 ? pending.command("beModelCtrlJUMP","SQ_B") : pending.command("beModelCtrlACTION","action");
        pending.sequence("SQ_A",{command});pending.sequence("SQ_B",{});pending.finish();
        auto source=std::make_shared<const ModelSequenceProgram>(pending.program());
        auto owned=std::make_shared<ModelSequenceFields>();
        auto runtime=std::make_unique<ModelSequenceRuntime>(source,model(),"actor",0,owned);runtime->select("SQ_A");
        unsigned calls=0;
        const auto unavailable=[&] {++calls;return false;};
        const auto fault=[&]()->bool {++calls;throw std::runtime_error("Retained external service failure.");};
        if(kind==0)runtime->run(42,{},{},[&](const auto&){return unavailable();});
        else if(kind==1)runtime->run(42,{},{},{},[&](const auto&){return unavailable();});
        else if(kind==2)runtime->run(42,{},{},{},{},[&](const auto&){return unavailable();});
        else if(kind==3)runtime->begin_select("SQ_B");
        else if(kind==4)runtime->run(42,{},[&](auto){return unavailable();});
        else if(kind==5)rejects([&]{runtime->run(42,{},{},{},{},[&](const auto&){return fault();});});
        else if(kind==6)rejects([&]{runtime->run(42,{},{},[&](const auto&){return fault();});});
        else rejects([&]{runtime->run(42,{},{},{},[&](const auto&){return fault();});});
        const auto name=owned->name;const auto data=owned->command_data;const auto index=owned->command_index;
        rejects([&]{ModelSequenceRuntime second(source,model(),"actor",0,owned);});
        rejects([&]{runtime->select("SQ_A");});
        rejects([&]{runtime->fields();});
        auto copy=*runtime;
        check(std::as_const(copy).fields()!=owned && copy.command_index()==index,
            "A pending inspection copy shared canonical state or lost its saved cursor.");
        rejects([&]{copy.run(42);});rejects([&]{copy.finish_select();});
        const auto copied_fields=std::const_pointer_cast<ModelSequenceFields>(std::as_const(copy).fields());
        rejects([&]{ModelSequenceRuntime copied_executor(source,model(),"actor",0,copied_fields);});
        ModelSequenceRuntime replacement(source,model(),"actor",0,std::make_shared<ModelSequenceFields>());
        rejects([&]{*runtime=replacement;});
        replacement=*runtime;rejects([&]{replacement.run(42);});
        auto moved=std::make_unique<ModelSequenceRuntime>(std::move(*runtime));runtime.reset();
        rejects([&]{ModelSequenceRuntime second(source,model(),"actor",0,owned);});
        moved.reset();
        rejects([&]{ModelSequenceRuntime revived(source,model(),"actor",0,owned);});
        check(owned->name==name && owned->command_data==data && owned->command_index==index && calls==(kind==3 ? 0u : 1u),
            "Releasing an abandoned executor modified guest fields or repeated external service work.");
    }
    for(int kind=0;kind<4;++kind) {
        Fixture pending;
        const auto command=kind==1 ? pending.hitbox("Set",{}) : kind==2 ? pending.lua("prefix","event") :
            kind==3 ? pending.command("beModelCtrlJUMP","SQ_B") : pending.command("beModelCtrlACTION","action");
        pending.sequence("SQ_A",{command});pending.sequence("SQ_B",{});pending.finish();
        auto source=std::make_shared<const ModelSequenceProgram>(pending.program());auto owned=std::make_shared<ModelSequenceFields>();
        unsigned calls=0;
        {
            ModelSequenceRuntime runtime(source,model(),"actor",0,owned);runtime.select("SQ_A");
            const auto complete=[&] {return ++calls==2;};
            const auto run=[&] {
                if(kind==0)return runtime.run(42,{},{},[&](const auto&){return complete();});
                if(kind==1)return runtime.run(42,{},{},{},[&](const auto&){return complete();});
                if(kind==2)return runtime.run(42,{},{},{},{},[&](const auto&){return complete();});
                return runtime.run(42,{},[&](auto){return complete();});
            };
            run();check(run().stop==ModelSequenceStop::end && !runtime.external_pending() && !runtime.selection_pending(),
                "A completed retained external service did not release its execution guard.");
        }
        ModelSequenceRuntime rebound(source,model(),"actor",0,owned);
        check(rebound.run(42).stop==ModelSequenceStop::end && calls==2,
            "A completed pending service poisoned the generation or replayed after rebinding.");
    }
}
void label_list_owners_and_publication() {
    auto f=branches();auto fields=std::make_shared<ModelSequenceLabelFields>();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0,fields);
    check(runtime.label_fields()==fields && !fields->sequences && !fields->names && !fields->offsets,
        "Prepared sequence construction allocated the originally null label lists.");
    runtime.select("SQ_WAIT");
    check(!fields->sequences && !fields->names && !fields->offsets,
        "Unlabelled selection allocated label lists.");
    runtime.set_label("SQ_WALK","gun");
    const auto sequences=fields->sequences,names=fields->names;const auto offsets=fields->offsets;
    check(sequences && names && offsets && sequences!=names && sequences->size()==1 && names->size()==1 &&
        offsets->size()==1 && (*sequences)[0]=="SQ_WALK" && (*names)[0]=="gun" && (*offsets)[0]==-1,
        "SETLABEL failed to allocate and populate the three actual list owners.");
    runtime.select("SQ_WALK");
    check(runtime.label_fields()==fields && fields->sequences==sequences && fields->names==names && fields->offsets==offsets &&
        (*offsets)[0]==12 && runtime.command_index()==12,"Sequence selection replaced a retained label owner or missed its cache write.");
    auto snapshot=runtime.labels();snapshot[0].label="detached";snapshot[0].cached_offset=7;
    check((*names)[0]=="gun" && (*offsets)[0]==12,"Inspection snapshots changed canonical label fields.");
    (*names)[0]="slash";(*offsets)[0]=-1;runtime.select("SQ_WALK");
    check(runtime.command_index()==4 && runtime.labels()[0].label=="slash" && (*offsets)[0]==4,
        "Selection read stale zipped labels instead of the actual supplied lists.");
    auto copy=runtime;
    check(copy.label_fields()!=fields && copy.label_fields()->sequences!=sequences && copy.label_fields()->names!=names &&
        copy.label_fields()->offsets!=offsets && copy.labels()==runtime.labels(),"Sequence copy retained live mutable label allocations.");
    copy.set_label("SQ_WALK","dual");copy.select("SQ_WALK");
    check((*names)[0]=="slash" && (*offsets)[0]==4,"A detached sequence candidate modified the original labels.");
    runtime=copy;
    check(runtime.label_fields()==fields && fields->sequences==sequences && fields->names==names && fields->offsets==offsets &&
        (*names)[0]=="dual" && (*offsets)[0]==0,"Sequence assignment replaced published label-list owners.");
    ModelSequenceRuntime moved(std::move(runtime));
    check(moved.label_fields()==fields,"Sequence move construction detached its supplied field owner.");
    moved.set_label("SQ_WALK","absent");const auto before=moved.labels();
    rejects([&]{moved.select("SQ_WALK");});
    check(moved.labels()==before && moved.command_index()==0 && fields->offsets==offsets,
        "Failed selection published its candidate cache or replaced the actual offset list.");
    // Original searches the first matching key even when prepared lists contain
    // duplicate occurrences. Entries are not canonicalized by equal text.
    sequences->push_back("SQ_WALK");names->push_back("gun");offsets->push_back(12);
    moved.set_label("SQ_WALK","arm");
    check(names->size()==2 && (*names)[0]=="arm" && (*offsets)[0]==-1 && (*names)[1]=="gun" && (*offsets)[1]==12,
        "SETLABEL replaced a later equal key or merged ordered occurrences.");
    moved.begin_select("SQ_WALK");
    check((*offsets)[0]==8 && moved.selection_pending() && moved.command_index()==8,
        "Phased selection failed to publish its cache before cleanup.");
    rejects([&]{moved.label_fields();});moved.finish_select();
    check(fields->sequences==sequences && fields->names==names && fields->offsets==offsets,
        "Finishing selection replaced one of the canonical label allocations.");
    std::weak_ptr<ModelSequenceLabelFields> retained;
    std::shared_ptr<ModelSequenceLabelFields::StringList> retained_names;
    {
        ModelSequenceRuntime transient(f.program(),model(),"other",1);
        transient.set_label("SQ_WAIT","dual");retained=transient.label_fields();retained_names=transient.label_fields()->names;
    }
    check(retained.expired() && retained_names && (*retained_names)[0]=="dual",
        "A retained label list required the lifetime of the sequence facade.");
}
void malformed_label_lists() {
    auto f=branches();auto fields=std::make_shared<ModelSequenceLabelFields>();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0,fields);
    rejects([&]{ModelSequenceRuntime invalid(f.program(),model(),"actor",0,nullptr);});
    fields->sequences=std::make_shared<ModelSequenceLabelFields::StringList>();
    const auto sequences=fields->sequences;
    rejects([&]{runtime.set_label("SQ_WAIT","dual");});rejects([&]{runtime.select("SQ_WAIT");});
    check(fields->sequences==sequences && !fields->names && !fields->offsets,
        "A partial label owner was silently repaired or discarded.");
    fields->names=sequences;fields->offsets=std::make_shared<ModelSequenceLabelFields::OffsetList>();
    auto detached=runtime;
    check(detached.label_fields()!=fields && detached.label_fields()->sequences!=sequences &&
        detached.label_fields()->sequences==detached.label_fields()->names,
        "Copying a prepared label profile repaired aliases or retained live list owners.");
    rejects([&]{detached.labels();});
    rejects([&]{runtime.set_label("SQ_WAIT","dual");});
    fields->names=std::make_shared<ModelSequenceLabelFields::StringList>();
    runtime.select("SQ_WAIT");
    check(fields->sequences==sequences && sequences->empty() && fields->names->empty() && fields->offsets->empty(),
        "An allocated-empty label profile was conflated with null fields.");
    sequences->push_back("SQ_WAIT");
    rejects([&]{runtime.labels();});rejects([&]{runtime.select("SQ_WAIT");});
    check(sequences->size()==1 && fields->names->empty() && fields->offsets->empty(),
        "Mismatched label counts were padded to manufacture a valid profile.");
    fields->names->push_back("dual");fields->offsets->push_back(99);
    rejects([&]{runtime.select("SQ_WAIT");});
    check((*fields->offsets)[0]==99 && runtime.command_index()==0,"An invalid cached offset changed the current sequence.");
    (*fields->offsets)[0]=-1;(*fields->names)[0]=std::string(4097,'x');
    rejects([&]{runtime.select("SQ_WAIT");});
    check((*fields->offsets)[0]==-1,"An invalid stored label name was cached or normalized.");
}
void sequence_publishes_into_actual_model_lists() {
    auto f=branches();
    auto matrix=std::make_shared<ModelMotionPose::Matrix>(identity);
    auto pad=std::make_shared<ModelPadMotionList>();auto movement=std::make_shared<MotionChannelList>();
    auto labels=std::make_shared<ModelSequenceLabelFields>();
    ModelControlState state(ModelMotionPose(matrix),pad,movement);
    state.pad_motion.set_values({1,2,3,4});const auto backing=pad->backing_owner();
    MotionCommand move;move.type="beModelCtrlMOVE";move.channel=2;
    move.setup={MotionMode::accelerate_positive,2,4,0};move.stop=MotionStop{true,1,2};
    apply_model_command(state,move,42);const auto entries=movement->mutable_entries();
    ModelSequenceRuntime runtime(f.program(),std::move(state),"actor",0,labels);
    check(runtime.model().pose.matrix_owner()==matrix && runtime.model().pad_motion.list_owner()==pad &&
        runtime.model().channels.list_owner()==movement && movement->mutable_entries()==entries,
        "Sequence construction detached supplied node, pad or movement owners.");
    runtime.set_label("SQ_WALK","dual");const auto offsets=labels->offsets;
    runtime.begin_select("SQ_WALK");
    check(pad->size()==0 && pad->backing_owner()==backing && *backing==std::vector<float>({1,2,3,4}) &&
        entries[2]->mode==MotionMode::accelerate_positive && (*offsets)[0]==0,
        "Early sequence publication replaced or cleared backing storage, or prematurely stopped movement.");
    runtime.finish_select();
    check(runtime.model().channels.list_owner()==movement && movement->mutable_entries()==entries &&
        entries[2]->mode==MotionMode::stop_positive && !entries[2]->stop_on_sequence &&
        runtime.model().pose.matrix_owner()==matrix,"Post-cleanup stop missed the original retained movement entries.");
    const auto result=runtime.run(42,[](const auto&){});
    check(result.stop==ModelSequenceStop::label_boundary && movement->mutable_entries()==entries &&
        entries[2]->mode==MotionMode::accelerate_positive && entries[2]->value==0.7f && pad->backing_owner()==backing,
        "A live sequence MOVE wrote a detached control copy or lost the cleared PDMOVE backing.");
    auto retained=entries[2];auto copied=runtime;
    copied.select("SQ_WAIT");
    check(retained->active && retained->value==0.7f && runtime.command_index()==4,
        "Selecting a detached runtime modified the live original list or cursor.");
    // A failed sequence lookup must preserve the published graph, not merely
    // equal-looking copied field values.
    const auto before=*retained;const auto saved=*matrix;
    rejects([&]{runtime.select("SQ_ABSENT");});
    check(movement->mutable_entries()==entries && *retained==before && *matrix==saved &&
        pad->backing_owner()==backing && labels->offsets==offsets,"Rejected selection changed the live owner graph.");
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
    auto f=branches();auto state=model();state.channels=channels;state.mode()=4;state.phase()=8;state.check_fall()=false;
    state.pad_motion.set_values({1,2,3,4});state.pose.position_locked()=true;
    start_motion_timer(state.elapsed_timer().emplace(),42);
    const auto timer=state.elapsed_timer();ModelSequenceRuntime runtime(f.program(),state,"actor",0);runtime.select("SQ_WAIT");
    check(runtime.model().mode()==0 && runtime.model().phase()==0 && runtime.model().check_fall() && runtime.model().pad_motion.size()==0,
        "Owned sequence preparation did not reset original control fields.");
    check(runtime.model().elapsed_timer()==timer && runtime.model().pose.position_locked(),"Sequence preparation invented timer restart or position unlock.");
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
    runtime.select("SQ_CUSTOM");check(runtime.run(42).stop==ModelSequenceStop::end && runtime.current_sequence()=="custom" && !runtime.model().check_fall(),
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
void wait_assignment_and_resume() {
    for(const auto& name:std::array<std::optional<std::string>,5>{"Action","action","unknown",std::string{},std::nullopt}) {
        Fixture f;const auto wait=f.wait(name),action=f.command("beModelCtrlACTION","after_wait");
        f.sequence("SQ_WAIT",{wait,wait,action});f.finish();
        auto program=f.program();
        check(program.sequences()[0].commands[0].opcode==ModelSequenceOpcode::wait_check &&
            program.sequences()[0].commands[0].name==name.value_or("") &&
            program.sequences()[0].commands[1].reference_index==wait,"WCHECK changed exact bytes, null normalization or shared command occurrences.");
        ModelSequenceRuntime runtime(std::move(program),model(),"actor",0);runtime.select("SQ_WAIT");
        runtime.wait_state().control_enabled=0x7f;runtime.wait_state().attack_state=-4;runtime.wait_state().attack_mode=19;
        runtime.model().mode()=4;runtime.model().phase()=3;runtime.model().check_fall()=false;
        runtime.model().pad_motion.set_values({1,2,3,4});
        start_motion_timer(runtime.model().elapsed_timer().emplace(),17);
        const auto channels=runtime.model().channels.states();const auto timer=runtime.model().elapsed_timer();
        const auto pad_motion=runtime.model().pad_motion;
        std::uint32_t actions{};
        const ModelActionDispatch dispatch=[&](const auto&){++actions;};
        auto result=runtime.run(42,dispatch);
        check(result.stop==ModelSequenceStop::waiting && result.commands_executed==1 && result.command_reference==wait &&
            result.type=="beModelCtrlWCHECK" && result.name==name.value_or("") && runtime.command_index()==1 && actions==0,
            "WCHECK did not consume exactly once and yield before the next command.");
        check(runtime.wait_state()==ModelWaitState{name.value_or(""),0x7f,-4,19},
            "WCHECK cleared an empty wait or changed player control/attack fields.");
        check(runtime.model().channels.states()==channels && runtime.model().elapsed_timer()==timer &&
            runtime.model().pad_motion==pad_motion && runtime.model().mode()==4 && runtime.model().phase()==3 && !runtime.model().check_fall(),
            "WCHECK invented motion, timer or control effects.");
        result=runtime.run(99,dispatch);
        check(result.stop==ModelSequenceStop::waiting && result.commands_executed==0 && !result.command_reference &&
            result.name==name.value_or("") && runtime.command_index()==1 && actions==0,
            "Another run implicitly resolved or replayed an engaged wait.");
        auto copy=runtime;copy.wait_state().name="copy";
        check(runtime.wait_state().name==std::optional<std::string>{name.value_or("")},"Runtime copies share mutable wait names.");
        runtime.wait_state().name.reset();result=runtime.run(100,dispatch);
        check(result.stop==ModelSequenceStop::waiting && result.commands_executed==1 && runtime.command_index()==2 && actions==0 &&
            runtime.wait_state().name==std::optional<std::string>{name.value_or("")},"Shared WCHECK was skipped or resumed beyond its own wait.");
        runtime.wait_state().name.reset();result=runtime.run(101,dispatch);
        check(result.stop==ModelSequenceStop::end && result.commands_executed==1 && runtime.command_index()==3 && actions==1 &&
            runtime.wait_state()==ModelWaitState{std::nullopt,0x7f,-4,19},"Explicit wait release failed to resume the retained next command.");
        runtime.wait_state().name="external";result=runtime.run(102,dispatch);
        check(result.stop==ModelSequenceStop::waiting && result.commands_executed==0 && actions==1,
            "An externally owned wait was ignored merely because the sequence cursor reached its end.");
    }
}
void wait_selection_and_guards() {
    Fixture f;const auto wait=f.wait("Action");
    f.sequence("SQ_A",{wait});f.sequence("SQ_B",{f.command("beModelCtrlACTION","b")});
    const auto jump=f.command("beModelCtrlJUMP","SQ_B");f.sequence("SQ_JUMP",{jump});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0);runtime.select("SQ_A");runtime.run(42);
    runtime.wait_state().control_enabled=255;runtime.wait_state().attack_state=7;runtime.wait_state().attack_mode=-9;
    const auto before=runtime.wait_state();
    rejects([&]{runtime.begin_select("SQ_MISSING");});
    check(runtime.wait_state()==before && runtime.current_sequence()=="SQ_A" && runtime.command_index()==1,
        "Invalid sequence selection cleared the retained wait or changed its cursor.");
    runtime.set_label("SQ_B","missing");rejects([&]{runtime.select("SQ_B");});
    check(runtime.wait_state()==before && !runtime.selection_pending(),"Invalid selected label cleared the wait.");
    runtime.model().mode()=4;runtime.model().phase()=5;runtime.model().check_fall()=false;
    runtime.model().pad_motion.set_values({1,2,3,4});
    runtime.begin_select("SQ_A");const auto& view=std::as_const(runtime);
    check(view.wait_state()==ModelWaitState{std::nullopt,255,7,-9} && view.model().mode()==0 && view.model().phase()==0 &&
        view.model().pad_motion.size()==0 && !view.model().check_fall() && runtime.selection_pending() && runtime.command_index()==0,
        "Sequence begin failed to clear only wait name before external cleanup.");
    rejects([&]{runtime.wait_state();});rejects([&]{runtime.run(42);});rejects([&]{runtime.begin_select("SQ_A");});
    runtime.finish_select();check(runtime.wait_state()==ModelWaitState{std::nullopt,255,7,-9} && runtime.model().check_fall(),
        "Sequence finish reset player wait fields.");
    runtime.run(42);runtime.select("SQ_JUMP");
    std::uint32_t cleanups{},actions{};
    // Remove the deliberately invalid target label through a separate owned
    // runtime so this path tests the actual pending local jump boundary.
    ModelSequenceRuntime local(f.program(),model(),"actor",0);local.wait_state()=runtime.wait_state();local.select("SQ_JUMP");
    auto result=local.run(42,[&](const auto&){++actions;},[&](std::string_view destination) {
        ++cleanups;
        check(destination=="SQ_B" && std::as_const(local).wait_state()==ModelWaitState{std::nullopt,255,7,-9},
            "Local jump cleanup observed a retained old wait or reset attack fields.");
        rejects([&]{local.wait_state();});return false;
    });
    check(result.stop==ModelSequenceStop::sequence_cleanup_required && result.command_reference==jump && local.command_index()==0 &&
        cleanups==1 && actions==0,"Pending jump failed to preserve its cleanup boundary.");
    rejects([&]{local.wait_state();});
    result=local.run(43,[&](const auto&){rejects([&]{local.wait_state();});++actions;},[](auto){return true;});
    check(result.stop==ModelSequenceStop::end && actions==1 && local.wait_state()==ModelWaitState{std::nullopt,255,7,-9},
        "Completed local jump cleanup changed the retained player wait fields.");
}
void local_jump_cleanup_preflight() {
    Fixture f;
    f.sequence("SQ_A",{f.command("beModelCtrlACTION","replace cleanup owner"),f.command("beModelCtrlJUMP","SQ_B")});
    f.sequence("SQ_B",{f.command("beModelCtrlACTION","destination")});
    f.sequence("SQ_EXTERNAL",{f.command("beModelCtrlJUMP","SQ_MISSING")});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0);runtime.select("SQ_A");
    bool available=true,complete=false;
    unsigned actions{},preflights{},cleanups{};
    const auto preflight=[&] {
        ++preflights;
        check(runtime.current_sequence()=="SQ_A" && runtime.command_index()==1 && !runtime.selection_pending(),
            "Cleanup preflight observed an already-published local jump.");
        rejects([&]{runtime.run(42);});
        if(!available)throw std::runtime_error("The prior cleanup owner is still updating.");
    };
    const auto action=[&](const ModelActionRequest& request) {
        ++actions;if(request.action=="replace cleanup owner")available=false;
    };
    const auto cleanup=[&](auto) {++cleanups;return complete;};
    check(runtime.run(42,{},cleanup,{},{},{},preflight).stop==ModelSequenceStop::action_required && preflights==0,
        "Unvisited JUMP invoked cleanup preflight before its preceding ACTION.");
    rejects([&]{runtime.run(42,action,cleanup,{},{},{},preflight);});
    check(actions==1 && preflights==1 && cleanups==0 && runtime.current_sequence()=="SQ_A" &&
        runtime.command_index()==1 && !runtime.selection_pending() && !runtime.jump_pending() && !runtime.external_pending(),
        "Rejected cleanup preflight published a jump or lost the completed ACTION prefix.");
    available=true;
    check(runtime.run(43,action,cleanup,{},{},{},preflight).stop==ModelSequenceStop::sequence_cleanup_required &&
        actions==1 && preflights==2 && cleanups==1 && runtime.current_sequence()=="SQ_B" && runtime.jump_pending(),
        "Available cleanup preflight failed to publish and retain the real cleanup boundary.");
    complete=true;
    check(runtime.run(44,action,cleanup,{},{},{},[&]{throw std::runtime_error("Do not repeat an entered preflight.");}).stop==ModelSequenceStop::end &&
        actions==2 && preflights==2 && cleanups==2 && !runtime.jump_pending(),
        "Pending local JUMP repeated its preflight or its preceding ACTION.");
    runtime.select("SQ_EXTERNAL");
    check(runtime.run(45,action,cleanup,{},{},{},[&]{throw std::runtime_error("External JUMP is not a local cleanup.");}).stop==ModelSequenceStop::controller_required,
        "External JUMP invoked a local cleanup preflight.");
}
void malformed_waits() {
    Fixture f;const auto wait=f.wait("Action");f.sequence("SQ_A",{wait});f.finish();
    const auto decode=[&](const IgbDocument& doc){return decode_model_sequences(doc,f.seq,"source");};
    auto doc=f.document();auto& object=doc.objects.at(doc.references.at(wait).value_index);
    object.fields[0].type_index=1;rejects([&]{decode(doc);});
    doc=f.document();doc.objects.at(doc.references.at(wait).value_index).fields.clear();rejects([&]{decode(doc);});
    doc=f.document();doc.objects.at(doc.references.at(wait).value_index).fields[0].size=3;rejects([&]{decode(doc);});
    doc=f.document();doc.objects.at(doc.references.at(wait).value_index).fields[0].offset=doc.bytes().size();rejects([&]{decode(doc);});
    doc=f.document();doc.objects.at(doc.references.at(wait).value_index).fields[0].size=8;rejects([&]{decode(doc);});
    f.text(wait,2,std::string(4097,'x'));rejects([&]{f.program();});
    for(const auto type:{"beModelCtrlWAITCHECK","beAction2WAITCHECK","bemodelCtrlWCHECK","beModelCtrlWCheck","DerivedWCHECK"}) {
        Fixture wrong;const auto ref=wrong.command(type,"Action");wrong.sequence("SQ_A",{ref});wrong.finish();
        auto graph=wrong.document();
        if(std::string_view(type)=="DerivedWCHECK") {
            auto base=graph.metadata.object_types[fixture::named_object];base.name="beModelCtrlWCHECK";
            const auto base_index=static_cast<std::uint32_t>(graph.metadata.object_types.size());
            graph.metadata.object_types.push_back(std::move(base));
            graph.metadata.object_types.at(graph.object(ref).type_index).parent_index=base_index;
        }
        ModelSequenceRuntime runtime(decode_model_sequences(graph,wrong.seq,"source"),model(),"actor",0);runtime.select("SQ_A");
        const auto result=runtime.run(42);
        check(result.stop==ModelSequenceStop::unsupported_command && result.type==type && runtime.command_index()==0 && !runtime.wait_state().name,
            "WCHECK decoding accepted a case variant, different subsystem or derived type.");
    }
}
void resumable_actions() {
    Fixture f;const auto move=f.move("Move",0.7f,6),action=f.command("beModelCtrlACTION","first");
    const auto wait=f.wait("Action"),last=f.command("beModelCtrlACTION","last");
    f.sequence("SQ_A",{move,action,wait,last});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",3);runtime.select("SQ_A");
    std::uint32_t tries{},committed_prefixes{},legacy_calls{};bool external_pending{};
    const ModelActionDispatch legacy=[&](const auto&){++legacy_calls;};
    const ModelActionTryDispatch dispatch=[&](const ModelActionRequest& request) {
        check(request.command_reference==action && request.source=="synthetic/player.igb" && request.info_name=="actor" &&
            request.ram_no==3 && request.action=="first","Resumable ACTION lost its request identity on retry.");
        rejects([&]{runtime.wait_state();});
        if(!external_pending) {external_pending=true;++committed_prefixes;}
        return ++tries==3;
    };
    auto result=runtime.run(42,legacy,{},dispatch);
    check(result.stop==ModelSequenceStop::action_required && result.commands_executed==1 && result.command_reference==action &&
        runtime.command_index()==1 && tries==1 && committed_prefixes==1 && legacy_calls==0,
        "Incomplete ACTION consumed its cursor or invoked the lower-precedence void callback.");
    const auto timer=std::as_const(runtime).model().elapsed_timer();const auto channels=std::as_const(runtime).model().channels.states();
    result=runtime.run(99,legacy,{},dispatch);
    check(result.stop==ModelSequenceStop::action_required && result.commands_executed==0 && runtime.command_index()==1 &&
        std::as_const(runtime).model().elapsed_timer()==timer && std::as_const(runtime).model().channels.states()==channels && committed_prefixes==1 && tries==2,
        "Incomplete ACTION retry replayed movement or discarded the service continuation.");
    rejects([&]{runtime.run(100,{},{},[](const auto&)->bool{throw std::runtime_error("Temporary test failure.");});});
    check(runtime.command_index()==1 && std::as_const(runtime).model().elapsed_timer()==timer,"Throwing resumable ACTION consumed its command or prefix.");
    result=runtime.run(101,legacy,{},dispatch);
    check(result.stop==ModelSequenceStop::waiting && result.commands_executed==2 && runtime.command_index()==3 &&
        runtime.wait_state().name==std::optional<std::string>{"Action"} && tries==3 && committed_prefixes==1 && legacy_calls==0,
        "Completed ACTION did not continue once to the following WCHECK.");
    result=runtime.run(102,legacy,{},dispatch);
    check(result.stop==ModelSequenceStop::waiting && result.commands_executed==0 && tries==3,
        "Retained wait invoked a resumable ACTION service before resolution.");
    runtime.wait_state().name.reset();
    result=runtime.run(103,{},{},[&](const auto& request){check(request.command_reference==last,"Wait resumed the previous ACTION.");return true;});
    check(result.stop==ModelSequenceStop::end && result.commands_executed==1 && runtime.command_index()==4,
        "Resolved wait failed to consume its next resumable ACTION.");
}
void entry_fields_and_lua_setters() {
    Fixture f;
    f.sequence("SQ_A",{f.lua("bullet","BHit"),f.lua(std::nullopt,"WHit"),f.lua("floor","FHit"),
        f.muteki("OFFWALL",999),f.muteki("OFFFLOOR",777),f.muteki("ON",-1),f.wait("Action")});
    f.sequence("SQ_B",{f.lua("new","BHit"),f.command("beModelCtrlACTION","b")});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",2);runtime.select("SQ_A");
    auto result=runtime.run(123);
    check(result.stop==ModelSequenceStop::waiting && result.commands_executed==7 && runtime.model().bullet_hit_script()=="bullet" &&
        runtime.model().wall_hit_script()==std::optional<std::string>{""} && runtime.model().floor_hit_script()=="floor" &&
        runtime.model().wall_check()==0 && runtime.model().floor_check()==0 && runtime.model().invulnerability_frames()==-1,
        "Exact LUACALL/MUTEKI setters failed to populate owned entry fields.");
    const auto timer=runtime.model().elapsed_timer();
    check(timer && timer->saved_world_time==123 && !runtime.model().channels.allocated(),"MUTEKI did not share one lazily initialized KEIKATIMER.");
    auto copy=runtime;copy.model().bullet_hit_script()="other";
    check(runtime.model().bullet_hit_script()=="bullet","Sequence copies share mutable callback prefixes.");
    runtime.model().check_fall()=false;runtime.model().mode()=4;runtime.model().phase()=7;
    runtime.wait_state().attack_state=5;runtime.wait_state().attack_mode=-8;
    runtime.begin_select("SQ_B");const auto& during=std::as_const(runtime).model();
    check(runtime.current_sequence()=="SQ_B" && runtime.command_index()==0 && during.mode()==0 && during.phase()==0 &&
        !std::as_const(runtime).wait_state().name && during.bullet_hit_script()=="bullet" && during.wall_hit_script()=="" &&
        during.floor_hit_script()=="floor" && during.wall_check()==0 && during.floor_check()==0 && !during.check_fall() && during.invulnerability_frames()==-1,
        "Pre-cleanup selection cleared callback/check/counter fields before their original phase.");
    runtime.finish_select();
    check(!runtime.model().bullet_hit_script() && !runtime.model().wall_hit_script() && !runtime.model().floor_hit_script() &&
        runtime.model().wall_check()==1 && runtime.model().floor_check()==1 && runtime.model().check_fall() &&
        std::bit_cast<std::uint32_t>(runtime.model().invulnerability_frames())==0 && runtime.model().elapsed_timer()==timer &&
        runtime.wait_state().attack_state==5 && runtime.wait_state().attack_mode==-8,
        "Post-cleanup selection failed to clear callbacks, enable checks or clamp only negative invulnerability.");
    result=runtime.run(456);
    check(result.stop==ModelSequenceStop::action_required && result.commands_executed==1 && runtime.model().bullet_hit_script()=="new" &&
        !runtime.model().wall_hit_script(),"Destination commands ran before entry cleanup or their new callbacks were cleared afterward.");
    for(const float frames:{23.f,0.f,-0.0f}) {
        runtime.model().invulnerability_frames()=frames;runtime.select("SQ_B");
        check(std::bit_cast<std::uint32_t>(runtime.model().invulnerability_frames())==std::bit_cast<std::uint32_t>(frames),
            "Sequence entry reset positive invulnerability or normalized negative zero.");
    }
    runtime.model().bullet_hit_script()="retained";runtime.model().invulnerability_frames()=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{runtime.begin_select("SQ_A");});
    check(!runtime.selection_pending() && runtime.current_sequence()=="SQ_B" && runtime.model().bullet_hit_script()=="retained",
        "Invalid prepared entry fields partially published a new sequence.");
    auto invalid=model();invalid.floor_hit_script()=std::string("bad\0script",10);
    rejects([&]{ModelSequenceRuntime bad(f.program(),invalid,"actor",0);});
}
void hitbox_and_immediate_lua_boundaries() {
    Fixture f;const ModelHitboxSubcommand parameters{-12345,1.25f,-1,{2,-3,4},{5,6,7}};
    const auto set=f.hitbox("Set",parameters),clear=f.hitbox("AllClear",parameters);
    const auto immediate=f.lua("prefix","bhit"),null_mode=f.lua(std::nullopt,std::nullopt),empty_mode=f.lua("",std::string{});
    f.sequence("SQ_A",{f.muteki("ON",21),f.hitbox(std::nullopt,parameters),set,immediate,clear,null_mode,empty_mode,
        f.hitbox("set",parameters),f.command("beModelCtrlACTION","tail")});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",3);runtime.select("SQ_A");
    auto result=runtime.run(123);
    check(result.stop==ModelSequenceStop::hitbox_required && result.command_reference==set && result.commands_executed==2 &&
        runtime.command_index()==2 && std::as_const(runtime).model().invulnerability_frames()==21,"HITBOX Set failed to stop at its actual queue ownership boundary.");
    const auto timer=std::as_const(runtime).model().elapsed_timer();unsigned hitbox_calls{},lua_calls{},actions{};bool ready=false;
    std::vector<std::uint32_t> accepted;std::vector<ModelLuaRequest> requests;
    const ModelHitboxDispatch dispatch_hitbox=[&](const ModelHitboxCommand& command) {
        ++hitbox_calls;check(command.parameters.kind==-12345 && command.parameters.delay==1.25f && command.parameters.length==-1 &&
            command.parameters.offset==std::array<float,3>{2,-3,4} && command.parameters.box_size==std::array<float,3>{5,6,7},
            "HITBOX lost signed kind, frame units or complete vector payloads.");
        rejects([&]{runtime.model();});
        if(!ready)return false;accepted.push_back(command.reference_index);return true;
    };
    result=runtime.run(999,{},{},{},dispatch_hitbox);
    check(result.stop==ModelSequenceStop::hitbox_required && result.commands_executed==0 && runtime.command_index()==2 &&
        std::as_const(runtime).model().elapsed_timer()==timer && accepted.empty(),"Incomplete HITBOX replayed the sequence prefix or consumed its command.");
    ready=true;result=runtime.run(1000,{},{},{},dispatch_hitbox);
    check(result.stop==ModelSequenceStop::lua_required && result.command_reference==immediate && result.commands_executed==1 &&
        runtime.command_index()==3 && accepted==std::vector<std::uint32_t>{set} && !std::as_const(runtime).model().bullet_hit_script(),
        "Nonexact LUACALL mode silently assigned a callback or ran without its real Lua owner.");
    const ModelLuaDispatch dispatch_lua=[&](const ModelLuaRequest& request) {
        ++lua_calls;check(request.source=="synthetic/player.igb" && request.info_name=="actor" && request.ram_no==3,
            "Immediate LUACALL lost qualified source/model/RAM context.");
        rejects([&]{runtime.run(1000);});
        if(lua_calls==1)return false;requests.push_back(request);return true;
    };
    result=runtime.run(1001,{},{},{},dispatch_hitbox,dispatch_lua);
    check(result.stop==ModelSequenceStop::lua_required && result.commands_executed==0 && runtime.command_index()==3 && hitbox_calls==2,
        "Incomplete Lua invocation consumed its command or replayed the accepted hitbox.");
    result=runtime.run(1002,[&](const auto&){++actions;},{},{},dispatch_hitbox,dispatch_lua);
    check(result.stop==ModelSequenceStop::end && result.commands_executed==6 && actions==1 &&
        accepted==std::vector<std::uint32_t>({set,clear}) && hitbox_calls==3 && requests.size()==3,
        "Command resume skipped AllClear, replayed a committed prefix or dispatched unknown HITBOX names.");
    check(requests[0].command_reference==immediate && requests[0].script_prefix=="prefix" && requests[0].event=="bhit" &&
        requests[1].command_reference==null_mode && requests[1].script_prefix.empty() && !requests[1].event &&
        requests[2].command_reference==empty_mode && requests[2].script_prefix.empty() && requests[2].event==std::optional<std::string>{""},
        "Immediate LUACALL conflated null/empty modes or changed the original prefix fallback.");
}
void uncertain_lua_is_not_replayed() {
    Fixture f;const auto lua=f.lua("prefix","event");
    f.sequence("SQ_A",{f.muteki("ON",7),lua,f.command("beModelCtrlACTION","tail")});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0);
    rejects([&]{runtime.select("SQ_MISSING");});
    runtime.select("SQ_A");
    check(runtime.run(123).stop==ModelSequenceStop::lua_required && runtime.command_index()==1,
        "A validation failure or missing Lua service incorrectly faulted the sequence owner.");
    unsigned calls{},external_effects{};
    const ModelLuaDispatch callback=[&](const ModelLuaRequest&)->bool {
        ++calls;if(calls==1)return false;
        ++external_effects;throw std::runtime_error("Lua failed after a possible external effect.");
    };
    check(runtime.run(456,{},{},{},{},callback).stop==ModelSequenceStop::lua_required && calls==1 && external_effects==0,
        "A false Lua callback was treated as an uncertain exception.");
    const auto timer=std::as_const(runtime).model().elapsed_timer();
    rejects([&]{runtime.run(789,{},{},{},{},callback);});
    check(calls==2 && external_effects==1 && runtime.command_index()==1 &&
        std::as_const(runtime).model().elapsed_timer()==timer && std::as_const(runtime).model().invulnerability_frames()==7,
        "Throwing Lua lost its committed sequence prefix or consumed the uncertain command.");
    rejects([&]{runtime.run(999,{},{},{},{},callback);});
    rejects([&]{runtime.run(999);});
    rejects([&]{runtime.select("SQ_A");});
    rejects([&]{runtime.begin_select("SQ_A");});
    rejects([&]{runtime.finish_select();});
    rejects([&]{runtime.set_label("SQ_A","new");});
    rejects([&]{runtime.model();});
    rejects([&]{runtime.wait_state();});
    ModelSequenceRuntime replacement(f.program(),model(),"actor",0);
    rejects([&]{runtime=replacement;});rejects([&]{runtime=std::move(replacement);});
    auto copy=runtime;rejects([&]{copy.run(999,{},{},{},{},callback);});
    check(calls==2 && external_effects==1 && runtime.command_index()==1 && !runtime.selection_pending(),
        "A faulted Lua command was replayed or made mutable through a sequence copy.");
}
void incomplete_lua_retains_ownership() {
    Fixture f;const auto lua=f.lua("prefix","event");
    f.sequence("SQ_A",{f.muteki("ON",7),lua,f.wait("Action")});
    f.sequence("SQ_B",{f.muteki("ON",9)});f.finish();
    ModelSequenceRuntime runtime(f.program(),model(),"actor",0);runtime.select("SQ_A");
    check(runtime.run(123).stop==ModelSequenceStop::lua_required && !runtime.lua_pending(),
        "A missing, never-invoked Lua callback acquired external continuation ownership.");
    runtime.select("SQ_B");runtime.run(124);runtime.select("SQ_A");
    unsigned calls{},prefixes{};bool ready{};
    const ModelLuaDispatch callback=[&](const ModelLuaRequest& request) {
        check(request.command_reference==lua,"Lua retry changed its retained command identity.");
        if(calls++==0)++prefixes;
        return ready;
    };
    check(runtime.run(125,{},{},{},{},callback).stop==ModelSequenceStop::lua_required && runtime.lua_pending() &&
        calls==1 && prefixes==1 && runtime.command_index()==1,"Incomplete Lua lost its externally committed prefix or command cursor.");
    const auto timer=std::as_const(runtime).model().elapsed_timer();
    rejects([&]{runtime.select("SQ_B");});rejects([&]{runtime.begin_select("SQ_B");});rejects([&]{runtime.finish_select();});
    rejects([&]{runtime.model();});rejects([&]{runtime.wait_state();});rejects([&]{runtime.set_label("SQ_A","changed");});
    ModelSequenceRuntime replacement(f.program(),model(),"actor",0);
    rejects([&]{runtime=replacement;});rejects([&]{runtime=std::move(replacement);});
    check(runtime.run(456).stop==ModelSequenceStop::lua_required && runtime.lua_pending() && calls==1 &&
        runtime.command_index()==1 && std::as_const(runtime).model().elapsed_timer()==timer,
        "Omitting Lua on retry abandoned its pending prefix or restarted preceding MUTEKI timing.");
    ready=true;const auto result=runtime.run(789,{},{},{},{},callback);
    check(result.stop==ModelSequenceStop::waiting && result.commands_executed==2 && !runtime.lua_pending() &&
        calls==2 && prefixes==1 && runtime.command_index()==3 && runtime.wait_state().name=="Action",
        "Successful Lua retry duplicated its prefix or failed to release ownership before the next WCHECK.");
    runtime.select("SQ_B");
    check(runtime.run(999).stop==ModelSequenceStop::end && runtime.model().invulnerability_frames()==9,
        "Completed Lua permanently blocked later ordinary sequence selection.");
}
void malformed_hitbox_and_lua() {
    Fixture f;const auto hitbox=f.hitbox("AllClear",{10000,1,2,{3,4,5},{6,7,8}}),lua=f.lua("prefix","BHit");
    f.sequence("SQ_A",{hitbox,lua});f.finish();
    const auto decode=[&](const IgbDocument& doc){return decode_model_sequences(doc,f.seq,"source");};
    for(std::uint16_t slot=3;slot<=7;++slot) {
        auto doc=f.document();auto& object=doc.objects.at(doc.references.at(hitbox).value_index);
        const_cast<IgbFieldValue*>(doc.field(object,slot))->type_index=0;rejects([&]{decode(doc);});
        doc=f.document();auto& malformed=doc.objects.at(doc.references.at(hitbox).value_index);
        --const_cast<IgbFieldValue*>(doc.field(malformed,slot))->size;rejects([&]{decode(doc);});
    }
    auto doc=f.document();auto& object=doc.objects.at(doc.references.at(lua).value_index);
    const_cast<IgbFieldValue*>(doc.field(object,3))->type_index=1;rejects([&]{decode(doc);});
    doc=f.document();doc.objects.at(doc.references.at(lua).value_index).fields.pop_back();rejects([&]{decode(doc);});
    for(const std::size_t word:{1u,2u,3u,8u}) {
        auto bad=f;fixture::word(bad.records.at(*bad.commands[0].hitbox_values).data,word*4,0x7f800000u);
        rejects([&]{bad.program();});
    }
    Fixture long_mode;long_mode.sequence("SQ_A",{long_mode.lua("prefix",std::string(4097,'x'))});long_mode.finish();rejects([&]{long_mode.program();});
    for(const auto type:{"DerivedHITBOX","beModelCtrlHitBox","beModelCtrlLuaCall"}) {
        auto bad=f;bad.commands[0].type=type;
        ModelSequenceRuntime runtime(bad.program(),model(),"actor",0);runtime.select("SQ_A");
        check(runtime.run(0).stop==ModelSequenceStop::unsupported_command,"Command decoding accepted a derived or case variant type.");
    }
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
    const std::map<std::uint32_t,std::pair<std::string,std::uint32_t>> expected_waits{
        {516,{"Action",157}},{625,{"TurnY",32}},{160,{"Landing",36}},{76,{"Moving",10}},
        {1199,{"Atck01",8}},{2810,{"Atck02",8}},{665,{"Atck03",8}}};
    std::map<std::uint32_t,std::uint32_t> wait_counts;std::uint32_t total_waits{};
    for(const auto& sequence:program.sequences())for(const auto& command:sequence.commands) {
        if(command.type!="beModelCtrlWCHECK")continue;
        const auto found=expected_waits.find(command.reference_index);
        check(command.opcode==ModelSequenceOpcode::wait_check && found!=expected_waits.end() && command.name==found->second.first,
            "An original WCHECK occurrence lost its exact type, reference or wait name.");
        ++wait_counts[command.reference_index];++total_waits;
    }
    check(total_waits==259,"Original Agumon WCHECK occurrence count changed.");
    for(const auto& [reference,expected]:expected_waits)check(wait_counts[reference]==expected.second,"Original shared WCHECK counts changed.");
    std::map<std::uint32_t,std::uint32_t> hitbox_counts,muteki_counts;std::uint32_t set_count{},clear_count{},muteki_total{};
    const std::map<std::uint32_t,std::pair<float,std::uint32_t>> expected_muteki{{78,{-1.f,32u}},{1183,{30.f,13u}},{1905,{18.f,4u}},{86,{120.f,9u}},{1710,{180.f,4u}}};
    for(const auto& sequence:program.sequences())for(const auto& command:sequence.commands) {
        if(command.type=="beModelCtrlHITBOX") {
            const auto& raw=player.object(command.reference_index);
            check(command.opcode==ModelSequenceOpcode::hitbox && command.hitbox_command &&
                command.hitbox_command->reference_index==command.reference_index && command.hitbox_command->name==command.name &&
                command.hitbox_command->parameters.kind==player.field_i32(raw,3) &&
                std::bit_cast<std::uint32_t>(command.hitbox_command->parameters.delay)==std::bit_cast<std::uint32_t>(player.field_f32(raw,4)) &&
                std::bit_cast<std::uint32_t>(command.hitbox_command->parameters.length)==std::bit_cast<std::uint32_t>(player.field_f32(raw,5)),
                "Original HITBOX occurrence lost its exact identity, signed kind or frame words.");
            ++hitbox_counts[command.reference_index];
            if(command.name=="Set")++set_count;else if(command.name=="AllClear")++clear_count;
        } else if(command.type=="beModelCtrlMUTEKI") {
            const auto found=expected_muteki.find(command.reference_index);
            check(command.opcode==ModelSequenceOpcode::model && command.model_command && std::holds_alternative<ModelMutekiCommand>(*command.model_command) &&
                found!=expected_muteki.end() && std::get<ModelMutekiCommand>(*command.model_command).frames==found->second.first,
                "Original MUTEKI occurrence was lost or interpreted from its name instead of float payload.");
            ++muteki_counts[command.reference_index];++muteki_total;
        }
    }
    check(set_count==344 && clear_count==8 && hitbox_counts.size()==236,"Original HITBOX Set/AllClear occurrences or unique references changed.");
    check(muteki_total==62 && muteki_counts.size()==5,"Original MUTEKI occurrence count changed.");
    for(const auto& [reference,expected]:expected_muteki)check(muteki_counts[reference]==expected.second,"Original shared MUTEKI counts changed.");
    ModelSequenceRuntime attack(program,model(),"p_agumon",0);attack.select("SQ_ATCK1");
    auto boundary=attack.run(42);
    check(boundary.stop==ModelSequenceStop::hitbox_required && boundary.command_reference==129u && attack.command_index()==1,
        "Original SQ_ATCK1 failed to retain its first actual Set hitbox boundary.");
    boundary=attack.run(42,{},{},{},[&](const ModelHitboxCommand& command) {
        check(command.reference_index==129 && command.name=="Set" && command.parameters.kind==60000 && command.parameters.delay==1 &&
            command.parameters.length==1 && command.parameters.offset==std::array<float,3>{0,4,7} && command.parameters.box_size==std::array<float,3>{3,3,3},
            "Original first attack hitbox lost its authored talk category, offset or extents.");
        return true; // Explicit test service records the request; no collision claim.
    });
    check(boundary.stop==ModelSequenceStop::action_required && boundary.name=="TALKWF" && attack.command_index()==2,
        "Accepted first hitbox failed to continue at the actual TALKWF ACTION boundary.");
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
        for(std::size_t axis=0;axis<3;++axis)check(std::abs(runtime.model().pose.matrix()[12+axis]-(placement.matrix[12+axis]+placement.matrix[8+axis]*8.275f))<0.001f,
            "Sequence-driven movement lost its original scalar distance or entry basis.");
    }
}
} // namespace
int main(int argc,char** argv) {
    try {command_prefix_and_branches();nullable_label_storage_and_lookup();canonical_sequence_fields_and_command_owners();canonical_wait_survives_work_replacement_and_move();malformed_canonical_sequence_fields();executor_lease_and_abandoned_services();label_list_owners_and_publication();malformed_label_lists();sequence_publishes_into_actual_model_lists();motion_sequence_stop();labels_jumps_and_guards();wait_assignment_and_resume();wait_selection_and_guards();local_jump_cleanup_preflight();malformed_waits();resumable_actions();
        entry_fields_and_lua_setters();hitbox_and_immediate_lua_boundaries();uncertain_lua_is_not_replayed();incomplete_lua_retains_ownership();malformed_hitbox_and_lua();malformed();
        if(argc==2)original_data(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_sequence_tests [EXTRACTED_ROOT]");
        std::cout<<"Model sequence tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Model sequence failure after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
