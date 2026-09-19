#include "dw4/game_clock.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
void lifecycle() {
    std::uint64_t now=100;
    unsigned samples{};
    NativeGameClock clock([&] {++samples;return now;});
    check(clock.state()==GameClockState{0,0,0,1},"Clock metadata defaults changed.");
    check(clock.time()==0 && samples==0,"Stopped clock sampled its platform source.");
    clock.pause();clock.reset();clock.stop();
    check(samples==0,"Stopped lifecycle operation sampled its platform source.");
    clock.start();
    check(clock.state()==GameClockState{100,0,0,0} && samples==1,"Initial start did not sample one baseline.");
    clock.start();
    check(samples==1,"Starting an already running clock sampled again.");
    now=125;check(clock.time()==25,"Running clock elapsed time changed.");
    now=150;const auto before=clock.state();
    check(clock.time()==50 && clock.state()==before,"Getter latched a timestamp or changed fields.");
    now=175;clock.pause();
    check(clock.state()==GameClockState{100,75,0,1},"Pause failed to accumulate elapsed time.");
    const auto paused_samples=samples;
    now=190;clock.pause();check(clock.time()==75 && samples==paused_samples,"Paused clock advanced or sampled again.");
    now=200;clock.start();now=210;
    check(clock.time()==85,"Resume discarded the accumulated time.");
    clock.set_reset_time(1000);
    check(clock.state()==GameClockState{200,75,1000,0} && clock.reset_time()==1000,"Reset-time setter changed current time.");
    now=300;clock.reset();now=320;
    check(clock.state()==GameClockState{300,1000,1000,0} && clock.time()==1020,"Running reset did not rebase its elapsed time.");
    const auto before_stop=samples;clock.stop();
    check(clock.state()==GameClockState{300,1000,1000,1} && samples==before_stop,"Stop sampled or preserved elapsed time instead of resetting.");
    now=400;clock.restart();
    check(clock.state()==GameClockState{400,1000,1000,0},"Restart did not reset and start.");
    now=425;clock.restart();
    check(clock.state()==GameClockState{425,1000,1000,0},"Restarting a running clock failed to resample.");

    clock.state()={50,75,25,255};const auto stopped_samples=samples;
    check(clock.stopped()==255 && clock.time()==75 && samples==stopped_samples,"Nonzero stopped byte was normalized or treated as running.");
    clock.reset();
    check(clock.state()==GameClockState{50,25,25,255} && samples==stopped_samples,"Stopped reset changed baseline or raw stopped byte.");
    now=500;clock.start();
    check(clock.state()==GameClockState{500,25,25,0},"Start did not accept a noncanonical stopped byte.");
}
void modulo_arithmetic() {
    constexpr auto max=std::numeric_limits<std::uint64_t>::max();
    NativeGameClock clock([] {return std::uint64_t{5};});
    clock.state()={max-2,max-1,7,0};
    check(clock.time()==6,"Getter lost original unsigned wraparound.");
    clock.pause();
    check(clock.state()==GameClockState{max-2,6,7,1},"Pause lost original unsigned wraparound.");
    NativeGameClock backward([] {return std::uint64_t{3};});
    backward.state()={9,2,0,0};
    check(backward.time()==max-3,"Negative elapsed difference was clamped instead of preserving words.");
    check(original_gamecube_clock_nanoseconds(0,162000000)==0,"Zero original timer conversion changed.");
    check(original_gamecube_clock_nanoseconds(40500000,162000000)==1000000000,"Original bus frequency conversion changed units.");
    check(original_gamecube_clock_nanoseconds(1,162000000)==24,"Original timer conversion rounded instead of truncating.");
    check(original_gamecube_clock_nanoseconds(max,162000000)==max-23,"Signed wrapped conversion did not truncate toward zero.");
    check(original_gamecube_clock_nanoseconds(max,500000)==max-7999,"Original conversion changed wrapped numerator words.");
    rejects([] {original_gamecube_clock_nanoseconds(1,499999);},"Zero-divisor hardware conversion was accepted.");
}
void signed_seconds() {
    // Original800936A0/800A3D14/800A3AF8/800A3E90 instruction-derived
    // binary64 results. The final rows distinguish split conversion from a
    // single double(nanoseconds)/1e9 expression in both rounding directions.
    struct Case {std::uint64_t nanoseconds,bits;};
    constexpr std::array cases{
        Case{0x0000000000000000,0x0000000000000000},
        Case{0x0000000000000001,0x3e112e0be826d695},
        Case{0x000000003b9ac9ff,0x3fefffffff768fa1},
        Case{0x000000003b9aca00,0x3ff0000000000000},
        Case{0x000000003b9aca01,0x3ff000000044b830},
        Case{0x00000000ffffffff,0x40112e0be815a889},
        Case{0x0000000100000000,0x40112e0be826d695},
        Case{0x001fffffffffffff,0x41612e0be826d694},
        Case{0x0020000000000001,0x41612e0be826d695},
        Case{0x7fffffffffffffff,0x42012e0be826d695},
        Case{0x8000000000000000,0xc2012e0be826d695},
        Case{0xffffffffffffffff,0xbe112e0be826d695},
        Case{0xffffffffc4653601,0xbfefffffff768fa1},
        Case{0xffffffffc4653600,0xbff0000000000000},
        Case{0xffffffffc46535ff,0xbff000000044b830},
        Case{0x49f4eabe5edac255,0x41f3da4425a71355},
        Case{0x8bcb42de1f726300,0xc1ff319dc3863360},
        Case{0x010bac9562730e76,0x4191f69b6188c33b},
        Case{0x70f279ae289545ab,0x41fe51ab9d186282}
    };
    for(const auto& item:cases) {
        unsigned samples{};
        NativeGameClock clock([&] {++samples;return item.nanoseconds-2;});
        clock.state()={5,item.nanoseconds,11,255};const auto stopped=clock.state();
        check(std::bit_cast<std::uint64_t>(clock.time_seconds())==item.bits && samples==0 && clock.state()==stopped,
            "Stopped seconds getter changed original signed conversion, state or sample count.");
        clock.state()={5,7,11,0};const auto running=clock.state();
        check(std::bit_cast<std::uint64_t>(clock.time_seconds())==item.bits && samples==1 && clock.state()==running,
            "Running seconds getter changed wrapped timer words or sampled more than once.");
    }
    NativeGameClock* active{};unsigned samples{};
    NativeGameClock mutated([&] {
        ++samples;active->state()={30,0x49f4eabe5edac25f,17,255};return std::uint64_t{20};
    });
    active=&mutated;mutated.state()={1,2,3,0};
    check(std::bit_cast<std::uint64_t>(mutated.time_seconds())==0x41f3da4425a71355 && samples==1 &&
        mutated.state()==GameClockState{30,0x49f4eabe5edac25f,17,255},
        "Seconds getter failed to use the integer getter's live fields after its one sample.");

    NativeGameClock absent(GameClockSource{});
    check(absent.time_seconds()==0 && !absent.faulted(),"Stopped seconds getter required an unused source.");
    absent.state()={10,20,30,0};
    rejects([&] {absent.time_seconds();},"Running seconds getter fabricated a missing source.");
    check(absent.faulted() && absent.state()==GameClockState{10,20,30,0},"Seconds source failure changed the timer state.");
    bool recursive_rejected{};
    NativeGameClock recursive([&] {
        try {active->time_seconds();}catch(const GameClockError&) {recursive_rejected=true;}
        return std::uint64_t{100};
    });
    active=&recursive;recursive.state()={10,20,30,0};
    rejects([&] {recursive.time_seconds();},"Caught recursive seconds sampling completed the outer getter.");
    check(recursive_rejected && recursive.faulted() && recursive.state()==GameClockState{10,20,30,0},
        "Seconds recursion did not preserve the integer getter's fault contract.");
}
void live_sample_fields() {
    // These are provider contract fixtures. A host callback can alter restored
    // owner fields at the actual virtual +94 entry; real hardware does not.
    NativeGameClock* active{};
    unsigned samples{};
    GameClockState observed;
    GameClockState replacement{40,80,120,255};
    NativeGameClock clock([&] {++samples;observed=active->state();active->state()=replacement;return std::uint64_t{100};});
    active=&clock;
    clock.state()={10,20,30,0};
    check(clock.time()==140 && clock.state()==replacement,"Getter failed to reload fields after the platform call.");
    clock.state()={10,20,30,0};clock.pause();
    check(clock.state()==GameClockState{40,140,120,1},"Pause failed to reload baseline/accumulator after sampling.");
    clock.state()={10,20,30,255};clock.start();
    check(observed==GameClockState{10,20,30,0},"Start did not publish its stopped-byte prefix before sampling.");
    check(clock.state()==GameClockState{100,80,120,255},"Start overwrote callback fields beyond its final baseline.");
    clock.state()={10,20,30,0};clock.reset();
    check(observed==GameClockState{10,30,30,0},"Reset did not publish its reset accumulator before sampling.");
    check(clock.state()==GameClockState{100,80,120,255},"Reset overwrote callback fields beyond its final baseline.");
    clock.state()={10,20,30,255};clock.restart();
    check(observed==GameClockState{10,30,30,0},"Restart did not publish its reset and running prefixes.");
    check(clock.state()==GameClockState{100,80,120,255} && samples==5,"Restart lost callback mutations or sampled more than once.");
}
void provider_failure_prefixes() {
    using Method=void(NativeGameClock::*)();
    struct Case {Method method;GameClockState before;GameClockState after;};
    const std::array cases{
        Case{&NativeGameClock::start,{10,20,30,1},{10,20,30,0}},
        Case{&NativeGameClock::pause,{10,20,30,0},{10,20,30,0}},
        Case{&NativeGameClock::reset,{10,20,30,0},{10,30,30,0}},
        Case{&NativeGameClock::restart,{10,20,30,255},{10,30,30,0}}
    };
    for(const auto& item:cases) {
        unsigned calls{};
        NativeGameClock clock([&]() -> std::uint64_t {++calls;throw GameClockError("Synthetic provider failure.");});
        clock.state()=item.before;
        rejects([&] {(clock.*item.method)();},"Throwing clock provider was swallowed.");
        check(clock.state()==item.after && clock.faulted() && calls==1,"Failed clock operation rolled back or fabricated later stores.");
        rejects([&] {clock.start();},"Faulted clock allowed a retry past an incomplete sample.");
        check(clock.state()==item.after && calls==1,"Faulted retry changed fields or repeated a sample.");
    }
    NativeGameClock absent(GameClockSource{});
    check(absent.time()==0 && !absent.faulted(),"Stopped clock unnecessarily required a platform sample.");
    absent.state()={10,20,30,0};
    rejects([&] {absent.time();},"Missing running-clock source returned a fabricated timestamp.");
    check(absent.state()==GameClockState{10,20,30,0} && absent.faulted(),"Missing-source getter changed fields or did not fault.");

    NativeGameClock* active{};
    bool inner_rejected{};
    NativeGameClock recursive([&] {
        try {active->time();}catch(const GameClockError&) {inner_rejected=true;}
        return std::uint64_t{100};
    });
    active=&recursive;recursive.state()={10,20,30,1};
    rejects([&] {recursive.start();},"Caught recursive sample incorrectly completed the outer operation.");
    check(inner_rejected && recursive.faulted() && recursive.state()==GameClockState{10,20,30,0},
        "Recursive sample lost the visible start prefix or wrote a fabricated baseline.");
}
void real_monotonic_source() {
    auto first=native_monotonic_clock_source();
    const auto a=first();
    auto second=native_monotonic_clock_source();
    const auto b=second();const auto c=first();
    check(a<=b && b<=c,"Native clock sources did not share one monotonic epoch.");
    NativeGameClock clock;
    check(clock.stopped()==1 && clock.time()==0,"Native host clock silently started during construction.");
    clock.start();const auto initial=clock.time();const auto next=clock.time();
    check(next>=initial,"Native running clock moved backwards.");
    clock.pause();const auto paused=clock.time();
    check(clock.time()==paused && clock.stopped()==1,"Paused native clock did not hold its accumulated value.");
}
} // namespace
int main() {
    try {
        lifecycle();modulo_arithmetic();signed_seconds();live_sample_fields();provider_failure_prefixes();real_monotonic_source();
        std::cout<<"Passed "<<checks<<" native game clock checks.\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"Native game clock test failed: "<<error.what()<<'\n';return 1;
    }
}
