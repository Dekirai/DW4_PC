#include "dw4/application_frame.hpp"
#include "dw4/game_clock.hpp"
#include "dw4/motion_timer.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
std::shared_ptr<NativeGameClock> running(GameClockSource source) {
    auto clock=std::make_shared<NativeGameClock>(std::move(source));
    clock->state().stopped=0;return clock;
}
MessengerCallResult done() {return {true,17,{}};}
void defaults_and_arithmetic() {
    unsigned samples{};
    const auto clock=std::make_shared<NativeGameClock>([&] {++samples;return std::uint64_t{100};});
    ApplicationFrameState state(clock);
    check(state.world_time==0 && state.current_time==0 && state.frame_time==0 &&
        state.current_to_world_factor==0 && samples==0 && clock->stopped()==1,
        "Application state construction invented initialization effects or sampled time.");
    initialize_application_frame_factor(state);
    check(state.current_to_world_factor==0x40000 && samples==0 && clock->stopped()==1,
        "Factor initialization performed additional startup effects.");
    // Expected words independently obtained by executing8028AF84 in the
    // supplied original DOL with explicit prepared virtual service results.
    struct Golden {std::uint64_t delta;std::uint32_t factor;std::uint64_t increment;};
    constexpr std::array goldens{
        Golden{0xffffffffffffffffull,0x7fffffff,0xffffffffffffe000ull},
        Golden{0xffffffffffffffffull,0x80000000,0x2000},
        Golden{0xffffffffffffffffull,0xffffffff,0},
        Golden{0x7ffffffffffffffeull,0x40000,0xfffffffffffffffeull},
        Golden{0x7ffffffffffffffeull,0x7fffffff,0x1fffffffc000ull},
        Golden{0x7ffffffffffffffeull,0x80000000,0x4000},
        Golden{0x7ffffffffffffffeull,0xffffffff,0xffffe00000000000ull},
        Golden{0xfe502a,0x7fffffff,0x1fca053fc0ull},
        Golden{0xfe502a,0x80000000,0xffffffe035fac000ull},
        Golden{0xfe502a,0xffffffff,0xffffffffffffffc0ull}
    };
    for(const auto& row:goldens)
        check(original_application_world_increment(row.delta,std::bit_cast<std::int32_t>(row.factor))==row.increment,
            "Application fixed-point product changed signed shift or wrapped intermediate words.");
}
void ordered_frame_and_system() {
    std::uint64_t now=125;
    std::vector<char> trace;
    auto state=std::make_shared<ApplicationFrameState>(running([&] {trace.push_back('T');return now;}));
    initialize_application_frame_factor(*state);state->current_time=100;state->world_time=7;
    ModelSceneSystem system;system.context=state;
    check(system.frame_time_f==1 && system.saved_world_time==0 && system.frame_time==0,
        "Fresh beSystem timing fields do not match metadata defaults.");
    state->dispatcher=[&] {
        trace.push_back('D');
        check(state->current_time==125 && state->frame_time==25 && state->world_time==32,
            "Dispatcher ran before the application time prefix.");
        // A dispatcher can change the scheduler before its original pointer load.
        state->scheduler=[&] {trace.push_back('S');publish_system_frame_time(system);return done();};
        return done();
    };
    ApplicationFrameInvocation frame(state);
    const auto result=frame.resume();
    check(result.completed && result.value==0 && trace==std::vector<char>{'T','D','S'},
        "Application clock/dispatcher/scheduler ordering changed.");
    check(system.frame_time==32 && system.saved_world_time==32 && system.frame_time_f==original_elapsed_frames(32),
        "beSystem did not consume the current shared application context.");
    check(frame.resume().completed && trace.size()==3,"Completed application frame replayed its clock or services.");
    now=150;state->dispatcher=[&] {trace.push_back('D');return done();};
    ApplicationFrameInvocation next(state);check(next.resume().completed,"Next completed application update could not enter.");
    check(state->world_time==57 && system.frame_time==25 && system.saved_world_time==57 && trace.size()==6,
        "A subsequent application update failed to advance from its prior published time.");
}
void pending_services_and_owner_gate() {
    unsigned samples{},dispatches{},schedules{},replacement_calls{};
    bool ready{};
    auto state=std::make_shared<ApplicationFrameState>(running([&] {++samples;return std::uint64_t{100};}));
    initialize_application_frame_factor(*state);
    ApplicationFrameInvocation frame(state);
    check(!frame.resume().completed && samples==1 && state->world_time==100,
        "Missing dispatcher fabricated success or lost the completed time prefix.");
    state->dispatcher=[&] {++dispatches;return MessengerCallResult{ready,4,"Prepared dispatcher is pending."};};
    check(!frame.resume().completed && dispatches==1 && samples==1,"Dispatcher suspension sampled the clock again.");
    state->dispatcher=[&] {++replacement_calls;return done();};
    ApplicationFrameInvocation overlap(state);
    rejects([&] {overlap.resume();},"A second application frame sampled while the first frame was pending.");
    check(overlap.faulted() && !state->faulted() && samples==1,"Rejected competing frame poisoned or restarted its actual owner.");
    ready=true;
    check(!frame.resume().completed && dispatches==2 && replacement_calls==0 && samples==1,
        "Pending dispatcher selection was replaced or absent scheduler was accepted.");
    state->scheduler=[&] {++schedules;return MessengerCallResult{schedules==2,8,"Prepared scheduler is pending."};};
    check(!frame.resume().completed && schedules==1,"Scheduler did not preserve its explicit pending result.");
    state->scheduler=[&] {++replacement_calls;return done();};
    check(frame.resume().completed && schedules==2 && replacement_calls==0 && samples==1,
        "Pending scheduler restarted time or changed its selected continuation.");
}
void live_timer_read_fields() {
    std::shared_ptr<ApplicationFrameState> state;
    unsigned samples{};
    const auto replacement=std::make_shared<NativeGameClock>();
    std::weak_ptr<NativeGameClock> captured;
    auto timer=running([&] {
        ++samples;state->timer=replacement;
        state->current_time=20;state->current_to_world_factor=-0x40000;state->world_time=500;
        return std::uint64_t{100};
    });
    captured=timer;state=std::make_shared<ApplicationFrameState>(timer);timer.reset();
    state->current_time=999;state->world_time=777;state->current_to_world_factor=0;
    state->dispatcher=done;state->scheduler=done;
    ApplicationFrameInvocation frame(state);
    check(frame.resume().completed && samples==1 && !captured.expired(),"Active application timer owner was destroyed or sampled twice.");
    check(state->timer==replacement && state->current_time==100 && state->frame_time==80 && state->world_time==420,
        "Application producer used pre-sample fields instead of the original live reloads.");
}
void failure_prefixes() {
    auto missing=std::make_shared<ApplicationFrameState>(nullptr);
    missing->current_time=10;missing->frame_time=20;missing->world_time=30;
    ApplicationFrameInvocation null_timer(missing);
    rejects([&] {null_timer.resume();},"Missing application timer fabricated a time value.");
    check(missing->faulted() && missing->current_time==10 && missing->frame_time==20 && missing->world_time==30,
        "Missing timer altered the original unpublished time prefix.");
    auto failing=std::make_shared<ApplicationFrameState>(running([]() -> std::uint64_t {throw GameClockError("Prepared timer failure.");}));
    failing->current_time=10;failing->frame_time=20;failing->world_time=30;
    ApplicationFrameInvocation failed_sample(failing);
    rejects([&] {failed_sample.resume();},"Throwing application timer was treated as success.");
    check(failing->faulted() && failing->current_time==10 && failing->frame_time==20 && failing->world_time==30,
        "Throwing timer published invented time stores.");

    auto state=std::make_shared<ApplicationFrameState>(running([] {return std::uint64_t{100};}));
    initialize_application_frame_factor(*state);unsigned schedules{};
    state->dispatcher=[]() -> MessengerCallResult {throw ApplicationFrameError("Prepared dispatcher failure.");};
    state->scheduler=[&] {++schedules;return done();};
    ApplicationFrameInvocation failed_dispatch(state);
    rejects([&] {failed_dispatch.resume();},"Dispatcher failure was treated as a completed frame.");
    check(state->faulted() && state->world_time==100 && state->current_time==100 && state->frame_time==100 && schedules==0,
        "Dispatcher failure rolled back time or reached the later scheduler.");
    ApplicationFrameInvocation retry(state);
    rejects([&] {retry.resume();},"New invocation restarted a faulted application's visible prefix.");

    auto abandoned=std::make_shared<ApplicationFrameState>(running([] {return std::uint64_t{5};}));
    {ApplicationFrameInvocation pending(abandoned);check(!pending.resume().completed,"Absent dispatcher completed during abandonment fixture.");}
    check(abandoned->faulted(),"Abandoning a started frame permitted a later update to skip its dispatcher.");
    auto unused=std::make_shared<ApplicationFrameState>(nullptr);
    {ApplicationFrameInvocation never_entered(unused);}
    check(!unused->faulted(),"Destroying an unentered invocation faulted an unused application.");

    auto recursive_state=std::make_shared<ApplicationFrameState>(running([] {return std::uint64_t{7};}));
    initialize_application_frame_factor(*recursive_state);
    ApplicationFrameInvocation recursive(recursive_state);
    recursive_state->dispatcher=[&] {try {recursive.resume();}catch(const ApplicationFrameError&) {}return done();};
    rejects([&] {recursive.resume();},"Caught recursive dispatcher allowed outer completion.");
    check(recursive_state->faulted() && recursive_state->world_time==7,"Recursive dispatcher rolled back the original time prefix.");
}
void system_leaf() {
    ModelSceneSystem system;system.saved_world_time=9;system.frame_time=8;system.frame_time_f=7;
    rejects([&] {publish_system_frame_time(system);},"Missing system context invented a frame delta.");
    check(system.saved_world_time==9 && system.frame_time==8 && system.frame_time_f==7,"Missing context changed system timing fields.");
    system.context=std::make_shared<ModelSceneContext>();system.context->world_time=100000002;system.saved_world_time=0;
    publish_system_frame_time(system);
    check(system.frame_time_f==6 && system.frame_time==100000002 && system.saved_world_time==100000002,
        "System timing was capped or lost its exact saved timestamp.");
    system.context=std::make_shared<ModelSceneContext>();system.context->world_time=2;
    system.saved_world_time=std::numeric_limits<std::uint64_t>::max()-2;
    publish_system_frame_time(system);
    check(system.frame_time==5 && system.saved_world_time==2 && system.frame_time_f==original_elapsed_frames(5),
        "System world difference did not preserve carry at wraparound.");
    system.context->world_time=1;publish_system_frame_time(system);
    check(system.frame_time==std::numeric_limits<std::uint64_t>::max() && system.frame_time_f==original_elapsed_frames(-1),
        "System timing clamped a negative signed elapsed value.");
}
int compare_original_prefix() {
    std::uint64_t sample{},previous{},world{};std::uint32_t factor{};
    while(std::cin>>std::hex>>sample>>previous>>world>>factor) {
        auto state=std::make_shared<ApplicationFrameState>(running([&] {return sample;}));
        state->current_time=previous;state->world_time=world;state->current_to_world_factor=std::bit_cast<std::int32_t>(factor);
        state->dispatcher=done;state->scheduler=done;
        ApplicationFrameInvocation frame(state);
        if(!frame.resume().completed)return 2;
        std::cout<<std::hex<<state->current_time<<' '<<state->frame_time<<' '<<state->world_time<<'\n';
    }
    return 0;
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--original-prefix")return compare_original_prefix();
        defaults_and_arithmetic();ordered_frame_and_system();pending_services_and_owner_gate();
        live_timer_read_fields();failure_prefixes();system_leaf();
        std::cout<<"Passed "<<checks<<" native application frame checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Application frame test failed: "<<error.what()<<'\n';return 1;}
}
