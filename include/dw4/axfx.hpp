#pragma once

// The AX effects library (AXFX) of the GameCube SDK as DW4 links it. The SNDF
// sets an effect up on an AX AUX bus from a bank's effect table (803BF698);
// AX hands the bus to the effect's callback once per 5 ms frame, the CPU
// processes it and the DSP mixes the result back into the main bus two
// frames later (axfx::AuxBus).
//
// ReverbStd (AXFXReverbStdInit 8024319C, its AUX callback 80243254, create
// 80242848, processing 80242CC4, shutdown 80243208) is the only effect type
// any DW4 bank uses: per channel a pre-delay, two parallel comb filters, an
// all-pass, a one-pole low-pass and a second all-pass, in single precision
// at the AX rate (the parameters assume 32000 Hz).
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dw4::axfx {

// One AX frame of one channel: 5 ms at 32 kHz.
inline constexpr std::size_t kFrameSamples = 160;
// An AUX bus buffer as the callback gets it: left, right and surround.
using AuxFrame = std::array<std::int32_t, 3 * kFrameSamples>;

class ReverbStd {
public:
    // AXFXReverbStdInit (the 80242848 create): coloration (the all-pass
    // coefficient) 0..1, time 0.01..10 s, mix 0..1, damping 0..1, pre-delay
    // 0..0.1 s. False when a parameter is out of range (the effect stays
    // off). A successful init starts with silent delay lines.
    bool init(float coloration, float time, float mix, float damping, float pre_delay);
    // AXFXReverbStdShutdown (80243208 -> 80243078): the delay lines go.
    void shutdown();
    bool active() const { return active_; }
    // The AUX callback (80243254 -> 80242CC4): the three channels in place.
    void process(AuxFrame& data);

private:
    // A delay line (DLcreate / DLsetdelay): `length` samples (the delay + 2),
    // written at `in`, then read at `out`, two samples ahead.
    struct DelayLine {
        std::size_t in{}, out{}, length{};
        std::vector<float> inputs;
        float last{};
        void create(int delay);
    };
    bool active_{};
    std::array<DelayLine, 6> all_pass_; // +0x00: two per channel
    std::array<DelayLine, 6> comb_;     // +0x78: two per channel
    float all_pass_coeff_{};            // +0xF0: coloration
    std::array<float, 6> comb_coef_{};  // +0xF4: 10^(-3 length / (32000 time))
    std::array<float, 3> lp_last_{};    // +0x10C
    float level_{};                     // +0x118: mix
    float damping_{};                   // +0x11C: 1 - (0.05 + 0.8 damping)
    int pre_delay_time_{};              // +0x120: 32000 * pre-delay samples
    std::array<std::vector<float>, 3> pre_delay_line_; // +0x124
    std::array<std::size_t, 3> pre_delay_pos_{};       // +0x130
};

// An AX AUX bus with the AX library's three rotating buffers (__AXAuxInit
// 8023CC00, __AXProcessAux 8023CE40): in frame k the DSP writes the bus mix
// of the frame's voices into buffer k % 3 and adds buffer (k + 1) % 3 to the
// main bus; at the next frame's interrupt the CPU runs the callback on the
// buffer the DSP filled in the frame before. A voice's send comes back two
// frames later. Without a callback the command list has no AUX command
// (8023D4E8): the DSP neither writes nor reads the bus, while the buffers
// rotate on and keep their contents.
class AuxBus {
public:
    // The frame interrupt: the callback (null: none registered) runs on the
    // buffer filled in the last frame, the buffers rotate. Returns the buffer
    // the DSP adds to the main bus in this frame, or null without a callback.
    const AuxFrame* begin_frame(ReverbStd* callback);
    // The end of the frame: with the AUX command, the DSP stores the frame's
    // sends (left and right interleaved; the surround send is 0).
    void end_frame(const std::array<std::int32_t, 2 * kFrameSamples>& sends);
    void reset();

private:
    std::array<AuxFrame, 3> buffers_{};
    int write_ = 0, read_ = 1, cpu_ = 2; // r13 +0xCE0 / +0xCE4 / +0xCF0
    int frame_write_ = -1;               // the running frame's write buffer (-1: no AUX command)
};

} // namespace dw4::axfx
