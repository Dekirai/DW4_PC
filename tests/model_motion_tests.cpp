#include "dw4/model_motion.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
using namespace dw4;
int checks{};
constexpr float pi=0x1.921fb6p+1f, tau=0x1.921fb6p+2f;
constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
void check(bool value,const char* message) { ++checks; if(!value) throw std::runtime_error(message); }
void near(float actual,float expected,const char* message) { check(std::abs(actual-expected)<0.000002f,message); }
template<class F> void rejects(F action) {
    ++checks; try { action(); } catch(const MotionError&) { return; }
    throw std::runtime_error("Invalid model motion was accepted.");
}
MotionTimer cached(float frames) { MotionTimer timer; timer.saved_world_time=42; timer.cached_frames=frames; return timer; }
MotionCommand command(std::uint32_t channel,MotionMode mode,float value,float frames=0) {
    MotionCommand result; result.type="beModelCtrlMOVEEX";result.channel=channel;result.setup={mode,value,frames,0};return result;
}
void shared_node_ownership() {
    static_assert(std::is_same_v<decltype(std::declval<const ModelMotionPose&>().matrix()),
        const ModelMotionPose::Matrix&>);
    static_assert(std::is_same_v<decltype(std::declval<const ModelMotionPose&>().matrix_owner()),
        std::shared_ptr<const ModelMotionPose::Matrix>>);
    rejects([]{ModelMotionPose missing(std::shared_ptr<ModelMotionPose::Matrix>{});});
    auto node=std::make_shared<ModelMotionPose::Matrix>(identity);
    ModelMotionPose old_work(node),fresh_work(node);
    old_work.turn.desired={1,2,3};old_work.position_locked=true;
    check(old_work.matrix_owner()==node && fresh_work.matrix_owner()==node &&
        fresh_work.turn==ModelTurn{} && !fresh_work.position_locked,
        "Fresh work did not share only the actual node matrix.");
    (*node)[12]=43;
    check(old_work.matrix()[12]==43 && fresh_work.matrix()[12]==43,
        "Actual node placement did not reach retained work aliases.");
    auto candidate=fresh_work;
    check(candidate.matrix_owner()!=node && candidate.matrix()==*node,
        "A transactional pose copy retained the mutable live matrix.");
    candidate.matrix()[12]=91;candidate.turn.desired[1]=0.4f;
    check((*node)[12]==43 && fresh_work.turn.desired[1]==0,
        "An unpublished candidate changed actual node or fresh work fields.");
    fresh_work=std::move(candidate);
    check(fresh_work.matrix_owner()==node && (*node)[12]==91 &&
        old_work.matrix()[12]==91 && old_work.turn.desired[1]==2 &&
        fresh_work.turn.desired[1]==0.4f,
        "Candidate commit replaced node identity or another work's turn fields.");
    ModelMotionPose moved(std::move(fresh_work));
    check(moved.matrix_owner()==node,"Moving a live pose detached its supplied node.");
    rejects([&]{(void)fresh_work.matrix();});
    fresh_work=old_work;
    check(fresh_work.matrix_owner()!=node && fresh_work==old_work,
        "Assignment could not restore independent value ownership after a move.");
    auto timer=cached(1);moved.turn.desired[1]=std::numeric_limits<float>::infinity();
    const auto before=*node;const auto before_timer=timer;
    rejects([&]{turn_model(moved,timer,42,true);});
    check(*node==before && timer==before_timer,
        "Failed turning partially changed a shared node matrix or timer.");
    moved.turn.desired={0,0.5f,0};force_model_turn(moved);
    check(old_work.matrix()==moved.matrix() && old_work.turn.desired[1]==2 &&
        moved.matrix()[12]==91,"Turning lost shared placement or changed old work fields.");
    std::weak_ptr<ModelMotionPose::Matrix> retained=node;
    node.reset();
    check(!retained.expired() && moved.matrix()[12]==91,
        "Work did not retain the actual node matrix after its caller released it.");
}
void angular_rules() {
    ModelMotionPose pose(identity); auto timer=cached(1);
    pose.turn.desired[1]=pi; pose.turn.rate[1]=0.25f;
    check(turn_model(pose,timer,42),"Half-turn did not run the model matrix path.");
    check(pose.turn.applied[1]==-0.25f,"Equal opposite arcs did not choose negative rotation without rewrapping.");
    check(pose.turn.desired[1]==pi && !pose.translated,"Turning changed the desired angle or translation flag.");
    near(pose.matrix()[8],std::sin(-0.25f),"Positive model Z does not follow the recovered Y rotation.");
    pose=ModelMotionPose(identity); pose.turn.desired[1]=0.125f;pose.turn.rate[1]=0.25f;
    turn_model(pose,timer,42); check(pose.turn.applied[1]==0.125f,"Short arc did not snap to desired angle.");
    pose=ModelMotionPose(identity);pose.turn.desired[1]=0.25f;pose.turn.rate[1]=0.25f;
    turn_model(pose,timer,42);check(pose.turn.applied[1]==0.25f,"Exact step-sized remaining angle did not snap.");
    pose=ModelMotionPose(identity);pose.turn.applied[1]=tau-0.125f;pose.turn.desired[1]=0.5f;pose.turn.rate[1]=0.25f;
    turn_model(pose,timer,42);check(pose.turn.applied[1]==tau+0.125f,"Cross-boundary positive result was normalized again.");
    pose=ModelMotionPose(identity);pose.turn.desired[1]=1;pose.turn.rate[1]=-0.25f;
    turn_model(pose,timer,42);check(pose.turn.applied[1]==-0.25f,"Signed original turn rate was clamped away.");
    pose.turn.applied[1]=0;pose.turn.rate[1]=0.25f;timer=cached(-1);
    turn_model(pose,timer,42);check(pose.turn.applied[1]==-0.25f,"Signed elapsed frame delta was clamped away.");
    pose=ModelMotionPose(identity);pose.turn.desired={0.1f,-0.2f,0.3f};pose.turn.rate={1,1,1};timer=cached(1);
    turn_model(pose,timer,42);
    near(pose.turn.applied[0],0.1f,"X-axis turn was skipped.");
    near(pose.turn.applied[1],tau-0.2f,"Negative target did not use original float wrapping.");
    near(pose.turn.applied[2],0.3f,"Z-axis turn was skipped.");
    // Repeated float subtraction differs from replacing the original loops by fmod.
    pose=ModelMotionPose(identity);pose.turn.applied[1]=100.0f;pose.turn.desired[1]=2;pose.turn.rate[1]=0.1f;
    float repeated=100.0f;while(repeated>=tau)repeated-=tau;
    turn_model(pose,timer,42);check(pose.turn.applied[1]==repeated+0.1f,"Original repeated-rounding angle reduction changed.");
    check(repeated!=std::fmod(100.0f,tau),"Wrapping fixture does not distinguish the original loop from fmod.");
}
void matrix_and_timer_paths() {
    ModelMotionPose pose({2,4,6,8, 3,5,7,9, 10,11,12,13, 15,16,17,18});
    auto timer=cached(1);const auto original=pose;const auto old_timer=timer;
    pose.turn.desired[1]=1;
    check(!turn_model(pose,timer,99) && pose.matrix()==original.matrix() && timer==old_timer,
        "Zero-rate axis touched its matrix or queried the timer.");
    pose.turn.desired[1]=0;pose.turn.rate[1]=std::numeric_limits<float>::infinity();
    check(!turn_model(pose,timer,99) && timer==old_timer,"An unchanged axis consumed its unused rate or timer.");
    pose.turn.desired[1]=tau;pose.turn.rate[1]=1;timer={};start_motion_timer(timer,0);
    check(!turn_model(pose,timer,1u<<18) && pose.matrix()==original.matrix() && timer.previous_time==(1u<<18),
        "Equivalent wrapped angle must query time but preserve the unchanged model basis.");
    pose.turn.desired={0,pi/2,0};pose.turn.rate[1]=0;pose.translated=true;
    timer.time_scale=std::numeric_limits<float>::infinity();const auto ignored_timer=timer;
    check(turn_model(pose,timer,77,true) && timer==ignored_timer,"Force read the timer or respected a zero turn rate.");
    check(pose.matrix()[12]==15 && pose.matrix()[13]==16 && pose.matrix()[14]==17 && pose.matrix()[15]==1 &&
        pose.matrix()[3]==0 && pose.matrix()[7]==0 && pose.matrix()[11]==0 && pose.translated,
        "Euler rebuild did not restore position/reset the bottom row, or altered translation bookkeeping.");
    near(pose.matrix()[0],0,"Forced Y rotation retained previous scale/shear.");
    near(pose.matrix()[2],-1,"Euler matrix has the wrong Y sine sign.");
    near(pose.matrix()[8],1,"Euler matrix has the wrong model-forward orientation.");
    near(pose.matrix()[5],1,"Euler rebuild did not replace previous Y scale.");
    pose.turn.desired={-0.0f,-0.0f,-0.0f};
    turn_model(pose,timer,77,true);
    check(std::signbit(pose.matrix()[2]) && pose.matrix()[0]==1 && pose.matrix()[5]==1 && pose.matrix()[10]==1,
        "Exact-zero Euler branches lost the original negated sine zero or identity basis.");
    pose.turn.desired[1]=-3*tau-0.2f;const auto raw=pose.turn.desired;
    turn_model(pose,timer,77,true);check(pose.turn.applied==raw,"Force unexpectedly wrapped desired angles.");
    // Non-position entries are replaced by the original matrix path, not read.
    pose.matrix()[0]=std::numeric_limits<float>::quiet_NaN();pose.turn.desired={0,0,0};
    turn_model(pose,timer,77,true);check(pose.matrix()[0]==1,"Euler rebuild rejected a basis element it completely overwrites.");
}
void model_order_and_ownership() {
    MotionChannels channels;ModelMotionPose pose(identity);auto timer=cached(0.5f);
    channels.apply(command(3,MotionMode::accelerate_positive,90));
    channels.apply(command(2,MotionMode::accelerate_positive,1));
    const auto result=channels.advance_model(pose,timer,42);
    check(result.turned && result.channels.translation_updated && pose.translated && result.channels.rotation==45 &&
        result.channels.translation[2]==0.5f,"Model tick lost original channel effects or update flags.");
    const float radians=0x1.1df46ap-6f*45;
    check(pose.turn.desired[1]==radians && pose.turn.rate[1]==1.25f*radians &&
        pose.turn.applied[1]==(1.25f*radians)*0.5f,"Rotation channel must set rate before turning multiplies by timer delta again.");
    near(pose.matrix()[12],std::sin(pose.turn.applied[1])*0.5f,"Translation used the old matrix before turning.");
    near(pose.matrix()[14],std::cos(pose.turn.applied[1])*0.5f,"Forward translation did not follow the updated basis.");
    const auto first_pose=pose;const auto first_timer=timer;const auto first_channels=channels.states();
    auto second_channels=channels;auto second_pose=pose;auto second_timer=timer;
    second_channels.apply(command(0,MotionMode::accelerate_negative,2));
    second_channels.advance_model(second_pose,second_timer,42);
    check(pose==first_pose && timer==first_timer && channels.states()==first_channels,
        "An independent model shares another player's mutable state.");
    check(second_pose.matrix()!=pose.matrix(),"Independent model did not advance its own matrix.");

    MotionChannels empty;pose.turn.desired[1]=3;pose.translated=true;timer.time_scale=std::numeric_limits<float>::infinity();
    const auto matrix=pose.matrix();const auto untouched_timer=timer;
    check(!empty.advance_model(pose,timer,99).turned && !pose.translated && pose.matrix()==matrix && timer==untouched_timer,
        "Empty channel list did more than clear the original translated flag.");
    MotionCommand no_op;no_op.type="beModelCtrlMOVE";empty.apply(no_op);timer=cached(1);pose=ModelMotionPose(identity);
    pose.turn.desired[1]=1;pose.turn.rate[1]=0.25f;
    const auto no_op_result=empty.advance_model(pose,timer,42);
    check(no_op_result.turned && !pose.translated && pose.turn.applied[1]==0.25f,
        "Allocated inactive channels skipped the original standalone turn path.");
    channels=MotionChannels{};channels.apply(command(3,MotionMode::impulse_positive,1));
    pose=ModelMotionPose(identity);pose.turn.rate[1]=5;
    const auto expired=channels.advance_model(pose,timer,42);
    check(!expired.turned && !channels.states()[3].active && pose.turn.rate[1]==0,
        "An initially active rotation channel that stops must still replace the Y rate with zero.");
    channels=MotionChannels{};channels.apply(command(2,MotionMode::stop_positive,1));pose=ModelMotionPose(identity);
    check(channels.advance_model(pose,timer,42).channels.translation_updated && pose.translated && pose.matrix()==identity,
        "Zero stopping displacement lost the original model-translation update path.");
    channels=MotionChannels{};channels.apply(command(3,MotionMode::impulse_negative,1,4));
    pose=ModelMotionPose(identity);timer=cached(0);
    channels.advance_model(pose,timer,42);
    check(std::bit_cast<std::uint32_t>(pose.turn.rate[1])==0x80000000u,
        "Ordered rotation magnitude was replaced by fabs, losing the original negative-zero rate.");
}
void guarded_failures() {
    ModelMotionPose pose(identity);pose.turn.desired[0]=1;pose.turn.rate[0]=0.25f;
    pose.turn.desired[1]=std::numeric_limits<float>::max();pose.turn.rate[1]=1;
    MotionTimer timer;start_motion_timer(timer,0);const auto old_pose=pose;const auto old_timer=timer;
    rejects([&]{turn_model(pose,timer,1u<<18);});
    check(pose==old_pose && timer==old_timer,"Stagnant original wrapping partially published another axis or timer.");
    pose.turn.desired[1]=std::numeric_limits<float>::infinity();
    rejects([&]{turn_model(pose,timer,1u<<18,true);});
    pose=ModelMotionPose(identity);pose.matrix()[12]=std::numeric_limits<float>::infinity();
    rejects([&]{turn_model(pose,timer,0,true);});
    MotionChannels channels;channels.apply(command(3,MotionMode::accelerate_positive,45));
    channels.apply(command(2,MotionMode::accelerate_positive,1));pose=ModelMotionPose(identity);
    timer=cached(0);const auto saved_channels=channels.states();const auto saved_pose=pose;const auto saved_timer=timer;
    rejects([&]{channels.advance_model(pose,timer,42);});
    check(channels.states()==saved_channels && pose==saved_pose && timer==saved_timer,
        "Non-finite zero-frame pose silently repaired motion or partially published state.");
    timer.cached_frames=1;
    check(channels.advance_model(pose,timer,42).turned,"Failed native boundary poisoned the unchanged state for a valid retry.");
}
void accumulated_angles() {
    auto timer=cached(1);
    // The independent literal original loop is deliberately kept in tests.
    // These include exponent boundaries, bin-3 ties and long-running rotations.
    for (const float magnitude : {8.0f,16.0f,31.999998f,32.0f,100.0f,100000.0f,1000000.0f,
        0x1p24f,0x1p26f,0x1p27f}) {
        for (const float sign : {-1.0f,1.0f}) {
            const float angle=sign*magnitude;
            float expected=angle;
            while(expected<0)expected+=tau;
            while(expected>=tau)expected-=tau;
            ModelMotionPose pose(identity);pose.turn.desired[1]=angle;pose.turn.rate[1]=10;
            turn_model(pose,timer,42);
            check(std::bit_cast<std::uint32_t>(pose.turn.applied[1])==std::bit_cast<std::uint32_t>(expected),
                "Batched wrapping differs from the literal original repeated-float loop.");
        }
    }
    ModelMotionPose pose(identity);pose.turn.desired[1]=std::nextafter(0x1p27f,std::numeric_limits<float>::infinity());pose.turn.rate[1]=1;
    rejects([&]{turn_model(pose,timer,42);});
}
void euler_ni_boundaries() {
    for(const auto sign:{0u,0x80000000u})for(const auto tiny:{1u,0x00400000u,0x007fffffu})for(std::size_t axis=0;axis<3;++axis) {
        ModelMotionPose pose(identity);pose.turn.desired[axis]=std::bit_cast<float>(sign|tiny);
        pose.matrix()[12]=std::bit_cast<float>(0x80000001u);pose.matrix()[13]=std::bit_cast<float>(1u);pose.matrix()[14]=-0.f;
        pose.translated=true;pose.position_locked=true;const auto desired=pose.turn.desired;
        force_model_turn(pose);
        const auto component=axis==0 ? 6u : axis==1 ? 2u : 1u;
        const auto expected=axis==1 ? sign^0x80000000u : sign;
        check(std::bit_cast<std::uint32_t>(pose.matrix()[component])==expected,
            "Euler trig frsp must flush tiny results with sign before products and sine negation");
        check(pose.turn.applied==desired && std::bit_cast<std::uint32_t>(pose.turn.applied[axis])==(sign|tiny) &&
            std::bit_cast<std::uint32_t>(pose.matrix()[12])==0x80000001u && std::bit_cast<std::uint32_t>(pose.matrix()[13])==1u &&
            std::bit_cast<std::uint32_t>(pose.matrix()[14])==0x80000000u && pose.translated && pose.position_locked,
            "NI Euler rebuild must preserve raw applied angles, scalar translation words and pose flags");
    }
    for(std::size_t axis=0;axis<3;++axis) {
        ModelMotionPose pose(identity);pose.turn.desired[axis]=std::numeric_limits<float>::min();force_model_turn(pose);
        const auto component=axis==0 ? 6u : axis==1 ? 2u : 1u;
        check(std::bit_cast<std::uint32_t>(pose.matrix()[component])==(axis==1 ? 0x80800000u : 0x00800000u),
            "Exactly minimum-normal sine results must survive Euler frsp");
    }
    const float almost_one=std::bit_cast<float>(0x3f7fffffu);
    const float angle=static_cast<float>(std::acos(static_cast<double>(almost_one)));
    check(std::bit_cast<std::uint32_t>(static_cast<float>(std::cos(static_cast<double>(angle))))==0x3f7fffffu,
        "Euler boundary fixture requires the immediately smaller float cosine than one");
    ModelMotionPose pose(identity);pose.turn.desired={std::numeric_limits<float>::min(),angle,0};
    force_model_turn(pose);
    check(std::bit_cast<std::uint32_t>(pose.matrix()[6])==0,
        "An exact product below minimum normal must flush even when ordinary float rounding would round up");
    pose.turn.desired[0]=std::bit_cast<float>(0x00800001u);force_model_turn(pose);
    check(std::bit_cast<std::uint32_t>(pose.matrix()[6])==0x00800000u,
        "A nearby product above the NI threshold must round normally rather than flush");
    pose=ModelMotionPose(identity);pose.turn.desired={0x1p-63f,0,0x1p-64f};force_model_turn(pose);
    check(std::bit_cast<std::uint32_t>(pose.matrix()[8])==0,
        "Tiny intermediate sine products must flush before entering a later fused basis term");
    const auto forced=pose;auto timer=cached(7);const auto saved_timer=timer;
    check(turn_model(pose,timer,99,true) && pose==forced && timer==saved_timer,
        "Standalone force and timer-bearing force must share Euler NI arithmetic without consuming time");
    pose.turn.desired[0]=std::numeric_limits<float>::infinity();const auto before=pose;
    rejects([&]{force_model_turn(pose);});
    check(pose.matrix()==before.matrix() && pose.turn.applied==before.turn.applied,
        "Failed standalone force must not publish a partial NI basis or applied angles");
}
void original_data(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root);
    const auto player=read_igb_document(resources.read("_common/p_agumon.igb"));
    const auto map=read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179));
    const auto move=decode_motion_command(player,130),stop=decode_motion_command(player,1540);
    check(move.name=="Move" && move.setup.value==0.7f && move.setup.frames==6,"Original Agumon SQ_WALK parameters differ.");
    check(stop.name=="Stop" && stop.setup.value==1 && stop.setup.frames==4,"Original Agumon SQ_WAIT parameters differ.");
    for(std::uint32_t slot=0;slot<4;++slot) {
        const auto placement=decode_igb_player_placement(map,115,0,slot);
        ModelMotionPose pose(placement.matrix);MotionChannels channels;auto timer=cached(1);
        // Explicit nominal-frame fixture: preserve this source basis, without
        // claiming the original spawn/sequence dispatcher or scheduler ran.
        channels.apply(move);for(int i=0;i<18;++i)channels.advance_model(pose,timer,42);
        channels.apply(stop);for(int i=0;i<6;++i)channels.advance_model(pose,timer,42);
        for(std::size_t axis=0;axis<3;++axis) {
            const float expected=placement.matrix[12+axis]+placement.matrix[8+axis]*8.275f;
            check(std::abs(pose.matrix()[12+axis]-expected)<0.001f,"Original model-basis travel differs from the recovered scalar trace.");
        }
        check(std::equal(pose.matrix().begin(),pose.matrix().begin()+12,placement.matrix.begin()),"Constant-heading original fixture changed its source basis.");
        check(!channels.states()[2].active && channels.states()[2].speed==0 && pose.translated,
            "Original Agumon fixture did not execute its final stopped translation path.");
        std::cout<<"Original point slot "<<slot<<": "<<pose.matrix()[12]<<','<<pose.matrix()[13]<<','<<pose.matrix()[14]<<'\n';
    }
}
} // namespace
int main(int argc,char** argv) {
    try { shared_node_ownership();angular_rules();matrix_and_timer_paths();model_order_and_ownership();guarded_failures();accumulated_angles();euler_ni_boundaries();
        if(argc==2)original_data(argv[1]);
        else if(argc!=1)throw std::runtime_error("Usage: dw4_model_motion_tests [EXTRACTED_ROOT]");
        std::cout<<"Model motion tests passed: "<<checks<<" checks\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"Model motion failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
