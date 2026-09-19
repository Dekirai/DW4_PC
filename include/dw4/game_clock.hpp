#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>

namespace dw4 {
class GameClockError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// igLongTimer metadata: three 64-bit values and a raw stopped byte. Native
// unsigned storage preserves original wraparound without signed-overflow UB.
struct GameClockState {
    std::uint64_t start_time{};      // +08: _startTime.
    std::uint64_t additional_time{}; // +10: _additionalTime.
    std::uint64_t reset_time{};      // +18: _resetTime.
    std::uint8_t stopped{1};        // +20: _isStopped, metadata default one.
    bool operator==(const GameClockState&) const = default;
};
using GameClockSource=std::function<std::uint64_t()>;

// The PC replacement for platform virtual +94. All returned sources share one
// process-local steady-clock epoch and report actual elapsed nanoseconds. This
// does not introduce a simulation tick, frame delta or a latched timestamp.
GameClockSource native_monotonic_clock_source();

// Authored equivalent of 8007734C, for original hardware conversion evidence:
// signed ((ticks*8000) modulo 2^64) / ((bus_clock>>2)/125000). The native host
// provider above does not execute guest code or depend on emulated registers.
std::uint64_t original_gamecube_clock_nanoseconds(std::uint64_t ticks,std::uint32_t bus_clock);

// Native owner for concrete timer 80093630..800939B8. The selected original
// singleton is igGamecubeLongTimer at global 805622FC, vtable 804703A4. Native
// startup must explicitly call start(); constructing defaults is not startup.
// Methods preserve field ordering across the sample, then read original live
// fields. Fields may be restored through state(), independently of sampling.
// Calls are synchronous and confined to the owning thread. Missing/throwing or
// recursive providers fault the owner without undoing earlier stores.
class NativeGameClock {
public:
    explicit NativeGameClock(GameClockSource source=native_monotonic_clock_source());
    NativeGameClock(const NativeGameClock&)=delete;
    NativeGameClock& operator=(const NativeGameClock&)=delete;
    GameClockState& state() noexcept {return state_;}
    const GameClockState& state() const noexcept {return state_;}

    std::uint64_t time();      // +68 / 80093630: stopped or live elapsed time.
    double time_seconds();    // +64 / 800936A0: one signed nanosecond sample, split into seconds and fraction.
    void start();              // +6C / 80093728: resume only if stopped.
    void pause();              // +70 / 8009377C: accumulate then stop.
    void reset();              // +74 / 800937F0: reset amount, preserve stopped.
    void stop();               // +78 / 8009384C: stop and reset amount.
    void restart();            // +7C / 80093868: reset amount and start.
    void set_reset_time(std::uint64_t); // +84 / 80093910: no current-time edit.
    std::uint64_t reset_time() const noexcept {return state_.reset_time;}
    std::uint8_t stopped() const noexcept {return state_.stopped;}
    bool faulted() const noexcept {return fault_;}
private:
    struct Operation;
    std::uint64_t sample();
    GameClockState state_;
    GameClockSource source_;
    bool executing_{},fault_{};
};
} // namespace dw4
