#pragma once

// Internal parts of online.cpp that engine.cpp uses.

namespace dw4::engine::detail {

// The live engine count and the online engine guard (Engine::live_engines).
void engine_constructed(); // throws std::logic_error while an online engine runs
void engine_destroyed(bool online);

// 6.12: every simulation entry (Engine::step, the scene traversal) runs with
// MXCSR 0x1F80 (round to nearest, no FTZ / DAZ, exceptions masked) and puts
// back what it found; with the default MXCSR it changes nothing.
class MxcsrGuard {
public:
    MxcsrGuard();
    ~MxcsrGuard();
    MxcsrGuard(const MxcsrGuard&) = delete;
    MxcsrGuard& operator=(const MxcsrGuard&) = delete;

private:
    unsigned saved_;
};

} // namespace dw4::engine::detail
