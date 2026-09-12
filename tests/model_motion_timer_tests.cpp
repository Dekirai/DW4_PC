#include "dw4/model_motion.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_timer.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
constexpr std::uint64_t world=std::uint64_t{1}<<24;
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
void near(float actual,float expected,const char* reason) {check(std::abs(actual-expected)<0.000002f,reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
std::shared_ptr<ModelSceneTimer> install(const std::shared_ptr<ModelSceneRam>& ram,float frames=0,std::uint64_t saved=0) {
    auto timer=std::make_shared<ModelSceneTimer>();timer->name="KEIKATIMER";
    timer->timer.saved_world_time=saved;timer->timer.cached_frames=frames;
    ensure_model_data(*ram)->append(timer);return timer;
}
MotionCommand impulse(std::uint32_t channel,float value,float duration=4) {
    MotionCommand result;result.type="beModelCtrlMOVEEX";result.channel=channel;
    result.setup={MotionMode::impulse_positive,value,4,duration};return result;
}
void turn_gates_and_independent_queries() {
    auto ram=std::make_shared<ModelSceneRam>();unsigned reads{};
    ModelRamTimerAccess access(ram,[&](const ModelSceneRam& actual){check(&actual==ram.get(),"Clock reader received another RAM.");++reads;return 42;});
    ModelMotionPose pose(identity);pose.turn().rate.fill(std::numeric_limits<float>::quiet_NaN());
    check(!turn_model(pose,access) && reads==0 && !ram->data,"Equal turn axes must skip all timer and rate reads.");
    pose.turn().desired={1,2,3};pose.turn().rate.fill(0);
    check(!turn_model(pose,access) && reads==0 && !ram->data,"Zero-rate axes must preserve applied angles without looking up a timer.");
    pose.turn().rate.fill(std::numeric_limits<float>::quiet_NaN());
    check(turn_model(pose,access,true) && reads==0 && !ram->data && pose.turn().applied==pose.turn().desired,
        "Forced turning must skip every timer/rate gate while publishing the desired basis.");
    auto timer=install(ram,0.25f,42);pose=ModelMotionPose(identity);pose.turn().desired={3,3,3};pose.turn().rate.fill(0.2f);
    std::vector<std::uint64_t> timestamps{42,world,world,world};std::size_t next{};
    ModelRamTimerAccess changing(ram,[&](const ModelSceneRam&){return timestamps.at(next++);});
    auto expected=timer->timer;const auto later_delta=motion_timer_frames(expected,world);
    check(turn_model(pose,changing) && next==4,"Each changed nonzero-rate axis must independently query frames, including a changed-time nested clock read.");
    near(pose.turn().applied[0],0.05f,"First axis did not use its own cached frame result.");
    near(pose.turn().applied[1],0.2f*later_delta,"Second axis reused the previous axis's frame result.");
    near(pose.turn().applied[2],0.2f*later_delta,"Third axis lost its independent same-time cached lookup.");
    check(timer->timer==expected,"Per-axis queries must publish to the actual named timer record.");
}
void live_ram_replacement() {
    auto ram=std::make_shared<ModelSceneRam>();auto old=install(ram,0.25f,42);unsigned reads{};
    ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return 42;});
    ModelMotionPose pose(identity);pose.turn().desired[1]=2;pose.turn().rate[1]=1;
    check(turn_model(pose,access) && reads==1,"Initial actual turn did not query its RAM timer.");
    near(pose.turn().applied[1],0.25f,"Initial actual turn used an incorrect cached result.");
    const auto old_data=ram->data;ram->data=std::make_shared<ModelSceneList>();auto replacement=install(ram,0.5f,42);
    check(turn_model(pose,access) && reads==2 && old_data!=ram->data,"A retained endpoint must perform a fresh lookup after actual RAM.data replacement.");
    near(pose.turn().applied[1],0.75f,"The second operation retained an obsolete timer from the previous RAM data list.");
    check(old->timer.cached_frames==0.25f && replacement->timer.cached_frames==0.5f,
        "Resolving another timer must not copy or overwrite the old actual timer record.");
    ram->data.reset();MotionChannels empty;pose.translated()=0xff;
    const auto result=empty.advance_model(pose,access);
    check(!result.turned && !pose.translated() && reads==2 && !ram->data,
        "Empty movement must clear translated before skipping timer lookup, even with pending turn angles.");
    MotionChannels null_list(std::make_shared<std::shared_ptr<MotionChannelList>>());pose.translated()=0x80;
    rejects([&]{null_list.advance_model(pose,access);},"A null movement owner must fail at its original count-read boundary.");
    check(!pose.translated() && reads==2 && !ram->data,"A null-list failure must preserve the prior translated-byte clear.");
}
void timer_prefix_before_invalid_motion() {
    {
        auto ram=std::make_shared<ModelSceneRam>();auto timer=install(ram,std::numeric_limits<float>::quiet_NaN(),42);unsigned reads{};
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return 42;});
        ModelMotionPose pose(identity);pose.turn().desired[1]=1;pose.turn().rate[1]=1;
        rejects([&]{turn_model(pose,access);},"An unsupported nonfinite cached frame must not silently snap to a finite target.");
        check(reads==1 && std::isnan(timer->timer.cached_frames) && pose.turn().applied[1]==0 && !access.faulted(),
            "The checked frame consumer must preserve its completed actual cached lookup and unmodified pose.");
    }
    {
        auto ram=std::make_shared<ModelSceneRam>();auto timer=install(ram);unsigned reads{};
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return world;});
        ModelMotionPose pose(identity);pose.turn().desired[1]=1;pose.turn().rate[1]=std::numeric_limits<float>::infinity();
        auto expected=timer->timer;motion_timer_frames(expected,world);
        rejects([&]{turn_model(pose,access);},"An invalid used turn rate must remain a checked boundary.");
        check(reads==2 && timer->timer==expected && pose.turn().applied==std::array<float,3>{} && !access.faulted(),
            "A later rate check must not roll back the completed actual timer helper or fault its valid endpoint.");
    }
    {
        auto ram=std::make_shared<ModelSceneRam>();auto timer=install(ram);unsigned reads{};
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return world;});
        ModelMotionPose pose(identity);pose.turn().desired[1]=1;pose.turn().rate[1]=1;
        pose.matrix()[12]=std::numeric_limits<float>::quiet_NaN();
        auto expected=timer->timer;motion_timer_frames(expected,world);
        rejects([&]{turn_model(pose,access);},"An invalid matrix publication must remain a checked boundary.");
        check(reads==2 && timer->timer==expected && pose.turn().applied[1]==0 && std::isnan(pose.matrix()[12]),
            "Timer writes must survive the subsequent rejected matrix rebuild while unapplied pose fields remain unchanged.");
    }
    {
        auto ram=std::make_shared<ModelSceneRam>();auto timer=install(ram);unsigned reads{};
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return world;});
        auto list=std::make_shared<MotionChannelList>(MotionChannelList::Entries{std::make_shared<MotionState>()});
        MotionChannels channels(list);ModelMotionPose pose(identity);pose.translated()=0xff;
        auto expected=timer->timer;motion_timer_frames(expected,world);
        rejects([&]{channels.advance_model(pose,access);},"Unsupported nonzero channel count must reject at the channel consumer.");
        check(reads==2 && !pose.translated() && timer->timer==expected,
            "Nonzero unsupported lists must still perform the original timer prefix after clearing translated.");
    }
    {
        auto ram=std::make_shared<ModelSceneRam>();unsigned reads{};
        ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return world;});
        MotionChannels channels;channels.apply(impulse(0,1));ModelMotionPose pose(identity);pose.translated()=1;
        rejects([&]{channels.advance_model(pose,access);},"Movement must not invent a missing named timer.");
        check(!pose.translated() && reads==0 && ram->data && ram->data->values().empty(),
            "Missing timer query must preserve its ensured actual RAM data list and translated clear.");
    }
}
void successful_movement_uses_current_named_timer() {
    auto ram=std::make_shared<ModelSceneRam>();const auto old=install(ram,0.5f,42);unsigned reads{};
    ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return 42;});
    MotionChannels channels;channels.apply(impulse(0,2));channels.apply(impulse(2,4));
    auto matrix=identity;matrix[0]=2;ModelMotionPose pose(matrix);
    const auto first=channels.advance_model(pose,access);
    check(reads==1 && first.channels.translation_updated && !first.turned && pose.translated()==1,
        "Successful translation with equal turn angles must perform only the initial movement timer query.");
    near(pose.matrix()[12],1.875f,"Actual movement lost the original scaled local X translation.");
    near(pose.matrix()[14],1.875f,"Actual movement lost the original local Z translation.");
    ram->data=std::make_shared<ModelSceneList>();const auto replacement=install(ram,0.25f,42);
    const auto second=channels.advance_model(pose,access);
    check(reads==2 && second.channels.translation_updated && old->timer.cached_frames==0.5f && replacement->timer.cached_frames==0.25f,
        "A later movement operation must resolve the new actual RAM list without altering the old timer.");
    near(pose.matrix()[12],2.78125f,"Movement reused the stale timer after RAM data replacement.");
    near(pose.matrix()[14],2.78125f,"Movement published a copied old timer after actual RAM data replacement.");
}
void committed_channel_prefixes() {
    auto ram=std::make_shared<ModelSceneRam>();auto timer=install(ram);unsigned reads{};
    ModelRamTimerAccess access(ram,[&](const ModelSceneRam&){++reads;return world;});
    MotionChannels channels;channels.apply(impulse(3,45));channels.apply(impulse(0,1));
    channels.apply(impulse(1,1));channels.apply(impulse(2,1));
    const auto list=channels.list_owner();list->entry_owner(1)->value=std::numeric_limits<float>::quiet_NaN();
    ModelMotionPose pose(identity);pose.translated()=0xff;
    auto expected=timer->timer;const auto delta=motion_timer_frames(expected,world);
    rejects([&]{channels.advance_model(pose,access);},"Invalid Y channel must stop at its actual scalar read.");
    check(reads==3 && timer->timer==expected && !pose.translated(),
        "Movement and turning must make separate actual timer calls and preserve their writes before a later channel failure.");
    near(list->entry_owner(3)->end_time,4-delta,"Completed rotation channel state was rolled back after a later failure.");
    near(list->entry_owner(0)->end_time,4-delta,"Completed X channel state was rolled back after invalid Y.");
    check(list->entry_owner(1)->end_time==4 && list->entry_owner(2)->end_time==4 && pose.turn().applied[1]!=0,
        "Later channels must remain unread while the already published rotation basis survives.");
    auto other_ram=std::make_shared<ModelSceneRam>();auto other_timer=install(other_ram);unsigned other_reads{};
    ModelRamTimerAccess other_access(other_ram,[&](const ModelSceneRam&){++other_reads;return world;});
    MotionChannels translate;translate.apply(impulse(0,1));ModelMotionPose bad_matrix(identity);
    bad_matrix.matrix()[0]=std::numeric_limits<float>::quiet_NaN();bad_matrix.translated()=1;
    rejects([&]{translate.advance_model(bad_matrix,other_access);},"Invalid translation matrix must remain a checked boundary.");
    near(translate.list_owner()->entry_owner(0)->end_time,4-delta,"Scalar translation prefix must survive a later matrix rejection.");
    check(other_reads==2 && other_timer->timer==expected && !bad_matrix.translated(),
        "Translation matrix failure must preserve its timer prefix and leave translated clear.");
}
}
int main() {
    try {
        turn_gates_and_independent_queries();live_ram_replacement();timer_prefix_before_invalid_motion();
        successful_movement_uses_current_named_timer();committed_channel_prefixes();
        std::cout<<"Actual model motion timer checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Actual model motion timer after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
