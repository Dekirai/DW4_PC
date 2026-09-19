#include "dw4/game_clock.hpp"

#include <bit>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw GameClockError(reason);}
}
GameClockSource native_monotonic_clock_source() {
    using Clock=std::chrono::steady_clock;
    static_assert(Clock::is_steady);
    static const auto epoch=Clock::now();
    return [] {
        const auto elapsed=Clock::now()-epoch;
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    };
}
std::uint64_t original_gamecube_clock_nanoseconds(std::uint64_t ticks,std::uint32_t bus_clock) {
    const auto divisor=(bus_clock>>2)/125000u;
    if(divisor==0)fail("Original GameCube timer conversion reaches a zero frequency divisor.");
    const auto numerator=std::bit_cast<std::int64_t>(ticks*std::uint64_t{8000});
    return std::bit_cast<std::uint64_t>(numerator/static_cast<std::int64_t>(divisor));
}
struct NativeGameClock::Operation {
    NativeGameClock& owner;
    int exceptions=std::uncaught_exceptions();
    explicit Operation(NativeGameClock& value):owner(value) {
        if(owner.fault_ || owner.executing_) {
            owner.fault_=true;
            fail("Native game clock requires an unfaulted nonrecursive owner.");
        }
        owner.executing_=true;
    }
    ~Operation() {
        if(std::uncaught_exceptions()>exceptions)owner.fault_=true;
        owner.executing_=false;
    }
};
NativeGameClock::NativeGameClock(GameClockSource source):source_(std::move(source)) {}
std::uint64_t NativeGameClock::sample() {
    if(!source_)fail("Native game clock requires its actual platform nanosecond source.");
    const auto result=source_();
    if(fault_)fail("A caught recursive provider invalidated the native game clock.");
    return result;
}
std::uint64_t NativeGameClock::time() {
    Operation operation(*this);
    if(state_.stopped!=0)return state_.additional_time;
    const auto now=sample();
    // 8009366C..688 reload both fields after the platform call. A getter does
    // not cache the result or modify start/additional time.
    return state_.additional_time+now-state_.start_time;
}
double NativeGameClock::time_seconds() {
    static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits==53);
    const auto nanoseconds=std::bit_cast<std::int64_t>(time());
    // 800936A0 converts signed remainder and quotient separately. Both fit
    // exactly in binary64. Preserve the division rounding before the addition;
    // converting the full nanosecond word first can produce a different result.
    constexpr std::int64_t scale=1000000000;
    const auto fraction=static_cast<double>(nanoseconds%scale)/1000000000.0;
    const auto seconds=static_cast<double>(nanoseconds/scale);
    return seconds+fraction;
}
void NativeGameClock::start() {
    Operation operation(*this);
    if(state_.stopped!=0) {
        state_.stopped=0; // 8009374C precedes the potentially failing sample.
        const auto now=sample();
        state_.start_time=now;
    }
}
void NativeGameClock::pause() {
    Operation operation(*this);
    if(state_.stopped==0) {
        const auto now=sample();
        const auto elapsed=now-state_.start_time;
        state_.additional_time+=elapsed;
        state_.stopped=1;
    }
}
void NativeGameClock::reset() {
    Operation operation(*this);
    state_.additional_time=state_.reset_time; // Published even if a later sample fails.
    if(state_.stopped==0) {
        const auto now=sample();
        state_.start_time=now;
    }
}
void NativeGameClock::stop() {
    Operation operation(*this);
    state_.stopped=1;
    state_.additional_time=state_.reset_time;
}
void NativeGameClock::restart() {
    Operation operation(*this);
    state_.stopped=0;
    state_.additional_time=state_.reset_time;
    const auto now=sample();
    state_.start_time=now;
}
void NativeGameClock::set_reset_time(std::uint64_t value) {
    Operation operation(*this);
    state_.reset_time=value;
}
} // namespace dw4
