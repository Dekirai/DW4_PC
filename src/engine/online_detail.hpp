#pragma once

// Internal parts of online.cpp that engine.cpp uses.

namespace dw4::engine::detail {

// The live engine count and the online engine guard (Engine::live_engines).
void engine_constructed(); // throws std::logic_error while an online engine runs
void engine_destroyed(bool online);

// 6.12: every simulation entry (Engine::step, the scene traversal) runs in
// the floating-point environment the simulation expects and puts back what
// it found; with the default environment it changes nothing. x86 / x86-64:
// MXCSR 0x1F80 (round to nearest, no FTZ / DAZ, exceptions masked). AArch64
// (the Android port, docs/ANDROID.md): FPCR with round to nearest (RMode 0),
// no flush to zero (FZ, FIZ), no default NaN (DN), no alternate handling
// (AH, NEP) and every exception trap off. Elsewhere it does nothing.
class FpEnvGuard {
public:
    FpEnvGuard();
    ~FpEnvGuard();
    FpEnvGuard(const FpEnvGuard&) = delete;
    FpEnvGuard& operator=(const FpEnvGuard&) = delete;

private:
    unsigned long long saved_;
};
// (The simulation entries' name for it from before the Android port.)
using MxcsrGuard = FpEnvGuard;

} // namespace dw4::engine::detail
