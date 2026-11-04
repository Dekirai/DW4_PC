#pragma once

// Pacing of a sealed instance (docs/SEAMLESS.md 6.4). The server seals tick t
// at S(t) = S0 + t * kFieldSeconds on its clock; a client executes sealed
// ticks at its own 59.94 Hz pace with a playout buffer of one tick and follows
// the server's pace by at most +-0.1 % (crystal drift is far below that). The
// same factor goes to the audio device (SDL_SetAudioStreamFrequencyRatio), so
// the sound manager's AX frames keep their mapping to the steps. A backlog of
// more than three ticks (a stall on the member's downlink) runs at once
// without pictures, which moves the audio once, as an offline hitch does.
// A smaller excess that lasts (the buffer 0.75 ticks or more above its target
// for half a second: a join's or a stage event's long step, a hitch of the
// host)
// runs one extra step at once (round 62): at +0.1 % it took 17 s per tick, and
// every tick of it made this member's pads later at the relay, which answered
// with a higher input delay.
#include <cstddef>

namespace dw4::game::online {

inline constexpr double kFieldSeconds = 1001.0 / 60000.0; // one NTSC field (engine kFieldSeconds)
inline constexpr double kMaxPaceDeviation = 0.001;         // +-0.1 %
inline constexpr std::size_t kPlayoutTicks = 1;
inline constexpr std::size_t kBacklogTicks = 3;

class PaceController {
public:
    void reset();
    // One pass of the host loop: `elapsed` real seconds since the last pass,
    // `waiting` sealed ticks received and not executed yet. Returns how many
    // steps to run now (never more than are waiting).
    int due_steps(double elapsed, std::size_t waiting);
    // The step rate factor (1 +- kMaxPaceDeviation); also the audio stream's
    // frequency ratio.
    double ratio() const { return ratio_; }
    // The last due_steps ran a backlog at once: no pictures for those steps.
    bool catching_up() const { return catching_up_; }
    // The smoothed number of waiting ticks (diagnostics, tests).
    double level() const { return level_; }
    // Steps that found no sealed tick when they were due (diagnostics).
    std::size_t starved() const { return starved_; }
    // Extra steps run to drain a lasting excess (diagnostics).
    std::size_t drained() const { return drained_; }

private:
    double accumulator_ = 0;
    double ratio_ = 1.0;
    double level_ = double(kPlayoutTicks);
    bool catching_up_ = false;
    bool primed_ = false;
    std::size_t starved_ = 0;
    std::size_t drained_ = 0;
    double excess_for_ = 0; // seconds the level stayed 0.75 ticks or more above its target
};

} // namespace dw4::game::online
