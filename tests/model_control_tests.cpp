#include "dw4/model_control.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"
#include "script_fixtures.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
int checks{};
constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    ++checks;try {action();} catch(const std::exception&) {return;}
    throw std::runtime_error("Invalid model control was accepted.");
}
bool same(const ModelControlState& a,const ModelControlState& b) {
    return a.pose==b.pose && a.channels.states()==b.channels.states() && a.channels.allocated()==b.channels.allocated() &&
        a.elapsed_timer==b.elapsed_timer && a.mode==b.mode && a.phase==b.phase && a.pad_motion==b.pad_motion && a.check_fall==b.check_fall &&
        a.bullet_hit_script==b.bullet_hit_script && a.wall_hit_script==b.wall_hit_script && a.floor_hit_script==b.floor_hit_script &&
        a.wall_check==b.wall_check && a.floor_check==b.floor_check &&
        std::bit_cast<std::uint32_t>(a.invulnerability_frames)==std::bit_cast<std::uint32_t>(b.invulnerability_frames);
}
MotionCommand movement(float value=1,float frames=2) {
    MotionCommand command;command.type="beModelCtrlMOVE";command.name="Move";command.channel=2;
    command.setup={MotionMode::accelerate_positive,value,frames,0};return command;
}
struct Fixture : dw4::test::ScriptFixture {
    std::uint32_t command,storage;
    std::string type;
    Fixture(std::string command_type,std::string command_name,float value=1,float frames=4):type(std::move(command_type)) {
        command=add(dw4::test::named_object);text(command,2,command_name);
        storage=memory(dw4::test::words({std::bit_cast<std::uint32_t>(value),std::bit_cast<std::uint32_t>(frames)}));
    }
    IgbDocument document() const {
        auto doc=ScriptFixture::document();doc.metadata.object_types[dw4::test::named_object].name=type;
        doc.metadata.field_types[8].name="igFloatMetaField";
        auto& object=doc.objects.at(doc.references.at(command).value_index);
        const auto offset=doc.memories.at(doc.references.at(storage).value_index).offset;
        if(type!="beModelCtrlCTRL") {
            object.fields.push_back({3,8,offset,4});
            if(type!="beModelCtrlMUTEKI")object.fields.push_back({4,8,offset+4,4});
        }
        return doc;
    }
    ModelCommand decode() const {return decode_model_command(document(),command);}
};
void heading_and_player_rates() {
    ModelMotionPose pose({-1,0,0,0, 0,2,0,0, -0.0f,0,-1,0, 15,16,17,1});
    pose.turn.desired={0.2f,1,0.3f};pose.turn.applied={0.4f,2,0.5f};pose.translated=true;pose.position_locked=true;
    const auto original=pose;
    set_player_turn_rates(pose);
    check(pose.turn.rate==std::array<float,3>{0,7,0},"Original player turn rates are not (0,7,0) radians/frame.");
    synchronize_model_heading(pose);
    check(pose.turn.desired[1]==0x1.921fb6p+1f && pose.turn.applied[1]==pose.turn.desired[1],
        "Model heading must preserve FMA zero rules before atan2; copying negative-zero Z-column X would give -pi.");
    check(pose.matrix==original.matrix && pose.translated && pose.position_locked &&
        pose.turn.desired[0]==0.2f && pose.turn.applied[2]==0.5f && pose.turn.rate[1]==7,
        "Heading synchronization modified the model matrix, rates, X/Z angles or flags.");
    pose.matrix[5]=std::numeric_limits<float>::quiet_NaN();
    synchronize_model_heading(pose);check(pose.turn.desired[1]==0x1.921fb6p+1f,"Unused transformed Y poisoned heading.");
    pose.matrix=identity;pose.matrix[0]=std::numeric_limits<float>::infinity();const auto old=pose.turn;
    rejects([&]{synchronize_model_heading(pose);});check(pose.turn==old,"Non-finite heading was partially published.");
    pose=ModelMotionPose(identity);set_player_turn_rates(pose);pose.turn.desired[1]=2;
    MotionTimer timer;timer.saved_world_time=42;timer.cached_frames=1;
    turn_model(pose,timer,42);check(pose.turn.applied[1]==2,"Player rate was converted from degrees or otherwise changed.");
    pose.turn.applied[1]=0;timer.cached_frames=0.125f;
    turn_model(pose,timer,42);check(pose.turn.applied[1]==0.875f,"Player turn rate did not use the actual cached frame fraction.");
}
void preparation_and_timer() {
    ModelControlState state{ModelMotionPose(identity)};state.pose.matrix[8]=1;state.pose.matrix[10]=0;
    state.pose.position_locked=true;state.pose.turn.desired[0]=0.3f;state.pose.turn.applied[2]=0.4f;set_player_turn_rates(state.pose);
    apply_model_command(state,movement(),123456789);
    check(state.elapsed_timer && state.elapsed_timer->saved_world_time==123456789 && state.elapsed_timer->cached_frames==1,
        "MOVE did not create and start its owned elapsed timer.");
    check(!state.pose.position_locked && state.pose.turn.desired[1]==0x1.921fb6p+0f &&
        state.pose.turn.applied[1]==state.pose.turn.desired[1] && state.pose.turn.desired[0]==0.3f && state.pose.turn.applied[2]==0.4f,
        "MOVE did not release position lock and synchronize only Y.");
    const auto timer=state.elapsed_timer;
    state.pose.turn.desired[1]=0.25f;state.pose.turn.applied[1]=0.5f;
    apply_model_command(state,movement(),234567890);
    check(state.elapsed_timer==timer && state.pose.turn.desired[1]==0.25f && state.pose.turn.applied[1]==0.5f,
        "Repeated MOVE restarted an existing timer or overwrote unlocked heading.");
    ModelControlState copy=state;copy.pose.position_locked=true;
    apply_model_command(copy,movement(2),345678901);
    check(state.pose.turn.desired[1]==0.25f && state.channels.states()[2].value==1 && state.elapsed_timer==timer,
        "Independent models share mutable movement state.");
    MotionCommand unknown;unknown.type="beModelCtrlMOVE";unknown.name="unrecognized";
    ModelControlState no_op{ModelMotionPose(identity)};no_op.pose.position_locked=true;
    apply_model_command(no_op,unknown,42);
    check(no_op.channels.allocated() && no_op.elapsed_timer && !no_op.pose.position_locked,
        "Unknown MOVE skipped its original preparation and allocation effects.");
    const auto saved=no_op;auto bad=movement();bad.channel=4;
    rejects([&]{apply_model_command(no_op,bad,99);});check(same(no_op,saved),"Invalid channel partially modified model control.");
    ModelControlState malformed{ModelMotionPose(identity)};malformed.pose.position_locked=true;
    malformed.pose.matrix[8]=std::numeric_limits<float>::infinity();const auto before=malformed;
    rejects([&]{apply_model_command(malformed,movement(),99);});check(same(malformed,before),"Failed unlock published a timer or unlocked pose.");
    ModelControlState empty{ModelMotionPose(identity)};empty.pose.translated=true;
    check(!advance_model_motion(empty,99).turned && !empty.pose.translated && !empty.elapsed_timer,
        "Unallocated movement created a timer or failed to clear the original flag.");
    empty.channels.apply(unknown);const auto missing=empty;
    rejects([&]{advance_model_motion(empty,99);});check(same(empty,missing),"Missing KEIKATIMER failure changed state.");
}
void controls_and_parameters() {
    ModelControlState state{ModelMotionPose(identity)};
    for(const auto& [text,mode]:std::array<std::pair<const char*,int>,4>{{{"onWalk",1},{"toWalk",2},{"toWait",3},{"isLive",4}}}) {
        state.phase=99;apply_model_command(state,Fixture("beModelCtrlCTRL",text).decode(),42);
        check(state.mode==mode && state.phase==0,"CTRL name selected the wrong mode or retained its substate.");
    }
    state.phase=2;apply_model_command(state,Fixture("beModelCtrlCTRL","noFall").decode(),42);
    check(!state.check_fall && state.phase==2 && state.mode==4,"noFall changed control mode or failed to change the flag.");
    apply_model_command(state,Fixture("beModelCtrlCTRL","onFall").decode(),42);check(state.check_fall,"onFall did not restore the flag.");
    const auto original=state;
    for(const auto text:{"ToWalk","unknown",""}) {
        apply_model_command(state,Fixture("beModelCtrlCTRL",text).decode(),42);
        check(same(state,original),"Unknown exact CTRL name is not a successful no-op.");
    }
    apply_model_command(state,Fixture("beModelCtrlPDMOVE","Move",0.7f,6).decode(),42);
    check(state.pad_motion==std::optional{std::array<float,4>{0,0,0.7f,6}},"First PDMOVE did not initialize the other pair to zero.");
    apply_model_command(state,Fixture("beModelCtrlPDMOVE","Stop",1,4).decode(),42);
    check(state.pad_motion==std::optional{std::array<float,4>{1,4,0.7f,6}},"PDMOVE pair order differs from the original.");
    apply_model_command(state,Fixture("beModelCtrlPDMOVE","move",2,0).decode(),42);
    check(state.pad_motion==std::optional{std::array<float,4>{2,0,0.7f,6}},"Nonexact PDMOVE name must select the Stop pair, including zero frames.");
    check(!state.elapsed_timer && !state.channels.allocated(),"CTRL/PDMOVE unexpectedly allocated motion or time.");
}
void muteki_fields_and_shared_timer() {
    ModelControlState state{ModelMotionPose(identity)};
    check(state.wall_check==1 && state.floor_check==1 && state.invulnerability_frames==0 && !state.bullet_hit_script &&
        !state.wall_hit_script && !state.floor_hit_script,"Prepared entry field defaults changed.");
    state.pose.position_locked=true;state.mode=3;state.phase=9;state.check_fall=false;
    state.bullet_hit_script="bullet";state.wall_hit_script="";state.floor_hit_script="floor";
    state.wall_check=255;state.floor_check=128;state.invulnerability_frames=42;
    apply_model_command(state,Fixture("beModelCtrlMUTEKI","OFFWALL",-1).decode(),123);
    check(state.wall_check==0 && state.floor_check==128 && state.invulnerability_frames==42 &&
        state.elapsed_timer && state.elapsed_timer->saved_world_time==123 && state.elapsed_timer->cached_frames==1,
        "OFFWALL failed to ensure KEIKATIMER or overwrote unrelated counter/check fields.");
    const auto timer=state.elapsed_timer;
    apply_model_command(state,Fixture("beModelCtrlMUTEKI","ONWALL",999).decode(),456);
    apply_model_command(state,Fixture("beModelCtrlMUTEKI","OFFFLOOR",999).decode(),456);
    check(state.wall_check==1 && state.floor_check==0 && state.invulnerability_frames==42 && state.elapsed_timer==timer,
        "Exact MUTEKI flag names assigned frame values or restarted an existing timer.");
    apply_model_command(state,Fixture("beModelCtrlMUTEKI","ONFLOOR",999).decode(),456);
    check(state.floor_check==1 && !state.check_fall && state.pose.position_locked && state.mode==3 && state.phase==9 &&
        !state.channels.allocated() && state.bullet_hit_script=="bullet" && state.wall_hit_script=="" && state.floor_hit_script=="floor",
        "MUTEKI flag commands changed pose, fall, channels, callbacks or control state.");
    for(const auto& [name,value]:std::array<std::pair<const char*,float>,5>{{{"ON",-1.f},{"OFF",0.f},{"offwall",2.f},{"",-0.0f},{"3.0",17.f}}}) {
        const auto command=Fixture("beModelCtrlMUTEKI",name,value).decode();
        check(std::holds_alternative<ModelMutekiCommand>(command) && std::get<ModelMutekiCommand>(command).name==name,
            "MUTEKI did not decode as its exact owned command.");
        apply_model_command(state,command,789);
        check(std::bit_cast<std::uint32_t>(state.invulnerability_frames)==std::bit_cast<std::uint32_t>(value) &&
            state.wall_check==1 && state.floor_check==1 && state.elapsed_timer==timer,"MUTEKI parsed its name or normalized the authored float.");
    }
    Fixture nullable("beModelCtrlMUTEKI","unused",23);nullable.records[nullable.command].values[2]=dw4::test::words({0});
    apply_model_command(state,nullable.decode(),789);check(state.invulnerability_frames==23,"Null MUTEKI name failed to take the frame assignment path.");
    apply_model_command(state,movement(),900);
    check(state.elapsed_timer==timer && !state.pose.position_locked && state.channels.allocated(),"MOVE did not reuse MUTEKI's existing KEIKATIMER.");
    const auto saved=state;
    rejects([&]{apply_model_command(state,ModelMutekiCommand{0,"OFFWALL",std::numeric_limits<float>::infinity()},901);});
    check(same(state,saved),"Invalid native MUTEKI partially changed the shared timer or flags.");
    auto doc=Fixture("beModelCtrlMUTEKI","ON",-1).document();doc.objects[0].fields.back().type_index=1;
    rejects([&]{decode_model_command(doc,0);});
    doc=Fixture("beModelCtrlMUTEKI","ON",-1).document();doc.objects[0].fields.back().size=3;rejects([&]{decode_model_command(doc,0);});
    rejects([&]{Fixture("beModelCtrlMUTEKI","ONWALL",std::numeric_limits<float>::quiet_NaN()).decode();});
    rejects([&]{Fixture("beModelCtrlMuteki","ON",-1).decode();});
}
void post_input_transitions() {
    ModelControlState state{ModelMotionPose(identity)};
    state.mode=2;
    auto decision=update_model_control(state,1,"SQ_WAIT","SQ_NONE");
    check(decision.requested_sequence=="SQ_WALK" && decision.dispatch_sequence && state.mode==2,
        "toWalk should request the sequence without applying its commands or changing control mode.");
    check(!update_model_control(state,2,"SQ_WAIT","SQ_NONE").dispatch_sequence,"toWalk treats a nonzero byte other than 1 as exactly 1.");
    check(!update_model_control(state,1,"SQ_WALK","SQ_NONE").dispatch_sequence,"An already-current sequence was dispatched again.");
    decision=update_model_control(state,1,"SQ_WAIT","SQ_JUMP");
    check(decision.requested_sequence=="SQ_JUMP" && decision.dispatch_sequence,"Mode logic overwrote the input callback's higher-priority request.");
    state.mode=3;
    decision=update_model_control(state,0,"SQ_WALK","SQ_NONE");
    check(decision.requested_sequence=="SQ_WAIT" && decision.dispatch_sequence,"toWait did not request SQ_WAIT after release.");
    check(!update_model_control(state,1,"SQ_WALK","SQ_NONE").dispatch_sequence,"toWait requested idle while input was active.");
    state.mode=4;check(!update_model_control(state,0,"SQ_WALK","SQ_NONE").dispatch_sequence,"isLive invented an input transition.");
    state.mode=1;state.phase=7;const auto saved=state;
    rejects([&]{update_model_control(state,1,"SQ_WAIT","SQ_NONE");});check(same(state,saved),"Missing channel dependency changed state.");
    decision=update_model_control(state,1,"SQ_WAIT","SQ_JUMP");
    check(decision.dispatch_sequence,"An existing request incorrectly consumed unused onWalk dependencies.");
    state.channels.apply(movement());state.phase=1;
    check(!update_model_control(state,1,"SQ_WAIT","SQ_NONE").dispatch_sequence,"Stable onWalk state read absent unused PDMOVE data.");
    const auto absent=state;rejects([&]{update_model_control(state,0,"SQ_WAIT","SQ_NONE");});
    check(same(state,absent),"Missing PDMOVE pair changed channel state.");
    apply_model_command(state,PadMotionCommand{0,"Move",0.7f,6},0);
    apply_model_command(state,PadMotionCommand{0,"Stop",1,4},0);
    state.phase=0;state.pose.position_locked=true;
    update_model_control(state,2,"SQ_WAIT","SQ_NONE");
    check(state.phase==1 && state.channels.states()[2].value==0.7f && state.pose.position_locked && !state.elapsed_timer,
        "onWalk initial nonzero input did not directly configure Move, or incorrectly ran MOVE preparation.");
    update_model_control(state,0,"SQ_WAIT","SQ_NONE");
    check(state.phase==2 && state.channels.states()[2].mode==MotionMode::stop_positive,"onWalk release did not configure Stop.");
    update_model_control(state,2,"SQ_WAIT","SQ_NONE");check(state.phase==2,"onWalk phase 2 must require exact input byte 1.");
    update_model_control(state,1,"SQ_WAIT","SQ_NONE");check(state.phase==1,"onWalk phase 2 did not return to movement.");
    state.phase=0;update_model_control(state,0,"SQ_WAIT","SQ_NONE");check(state.phase==2,"Initial onWalk idle input did not enter Stop.");
}
void malformed_records() {
    for(const auto type:{"DerivedCTRL","beModelCtrlMOVERL","beModelCtrlACTION"})
        rejects([&]{Fixture(type,"Move").decode();});
    rejects([&]{Fixture("beModelCtrlPDMOVE","Move",std::numeric_limits<float>::infinity()).decode();});
    rejects([&]{Fixture("beModelCtrlCTRL",std::string(4097,'x')).decode();});
    auto doc=Fixture("beModelCtrlPDMOVE","Move").document();
    doc.objects[0].fields.back().size=3;rejects([&]{decode_model_command(doc,0);});
    doc=Fixture("beModelCtrlCTRL","toWalk").document();doc.objects[0].fields.clear();rejects([&]{decode_model_command(doc,0);});
    doc=Fixture("beModelCtrlCTRL","toWalk").document();doc.metadata.header.byte_order=IgbByteOrder::big;
    rejects([&]{decode_model_command(doc,0);});
    doc=Fixture("beModelCtrlPDMOVE","Move").document();doc.objects[0].fields.back().offset=doc.bytes().size();
    rejects([&]{decode_model_command(doc,0);});
    ModelControlState state{ModelMotionPose(identity)};const auto saved=state;
    rejects([&]{apply_model_command(state,PadMotionCommand{0,"Stop",1,std::numeric_limits<float>::quiet_NaN()},0);});
    check(same(state,saved),"Invalid PDMOVE published a parameter list.");
    rejects([&]{update_model_control(state,1,"SQ_WAIT",std::string_view("SQ\0X",4));});
    check(same(state,saved),"Invalid sequence input changed state.");
}
void original_data(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto player=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto map=read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179));
    const auto move=decode_model_command(player,130),stop=decode_model_command(player,1540);
    const auto to_walk=decode_model_command(player,6499),to_wait=decode_model_command(player,622);
    check(std::get<ModelCtrlCommand>(to_walk).name=="toWalk" && std::get<ModelCtrlCommand>(to_wait).name=="toWait",
        "Original WAIT/WALK control records have changed.");
    for(const auto& [reference,value]:std::array<std::pair<std::uint32_t,float>,5>{{{78u,-1.f},{1183u,30.f},{1905u,18.f},{86u,120.f},{1710u,180.f}}}) {
        const auto command=decode_model_command(player,reference);
        check(std::get<ModelMutekiCommand>(command).reference_index==reference && std::get<ModelMutekiCommand>(command).frames==value,
            "Original shared MUTEKI float/reference changed.");
    }
    for(std::uint32_t group=0;group<2;++group)for(std::uint32_t slot=0;slot<4;++slot) {
        const auto placement=decode_igb_player_placement(map,115,group,slot);
        ModelMotionPose pose(placement.matrix);set_player_turn_rates(pose);synchronize_model_heading(pose);
        ModelControlState state(pose);
        apply_model_command(state,stop,42);apply_model_command(state,to_walk,42);
        check(update_model_control(state,1,"SQ_WAIT","SQ_NONE").requested_sequence=="SQ_WALK","Original WAIT did not request WALK.");
        // Explicit command effects, not execution of the intervening original
        // weapon/action sequence. Cached frame 1 is a scalar integration fixture.
        apply_model_command(state,move,42);apply_model_command(state,to_wait,42);
        for(int i=0;i<18;++i)advance_model_motion(state,42);
        check(update_model_control(state,0,"SQ_WALK","SQ_NONE").requested_sequence=="SQ_WAIT","Original WALK did not request WAIT.");
        apply_model_command(state,stop,42);apply_model_command(state,to_walk,42);
        for(int i=0;i<6;++i)advance_model_motion(state,42);
        for(std::size_t axis=0;axis<3;++axis)check(std::abs(state.pose.matrix[12+axis]-(placement.matrix[12+axis]+placement.matrix[8+axis]*8.275f))<0.001f,
            "Original prepared model movement lost its entry basis.");
        check(state.elapsed_timer && state.elapsed_timer->saved_world_time==42 && state.elapsed_timer->cached_frames==1 &&
            state.pose.turn.rate[1]==7 && !state.channels.states()[2].active,"Original command chain lost its timer, rate or stopped state.");
    }
}
} // namespace
int main(int argc,char** argv) {
    try {heading_and_player_rates();preparation_and_timer();controls_and_parameters();muteki_fields_and_shared_timer();post_input_transitions();malformed_records();
        if(argc==2)original_data(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: dw4_model_control_tests [EXTRACTED_ROOT]");
        std::cout<<"Model control tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model control failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
