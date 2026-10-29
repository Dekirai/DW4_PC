#include "pacing.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::game::online {

namespace {
// The smoothing time of the buffer level and the level the controller aims
// at: sampled between the arrivals and the steps, a buffer of one tick reads
// between one and two.
constexpr double kLevelSeconds = 2.0;
constexpr double kTargetLevel = double(kPlayoutTicks) + 0.5;
} // namespace

void PaceController::reset() { *this = PaceController{}; }

int PaceController::due_steps(double elapsed, std::size_t waiting) {
    catching_up_ = false;
    elapsed = std::clamp(elapsed, 0.0, 1.0);
    if (!primed_) {
        // Start once the playout buffer holds its tick.
        if (waiting < kPlayoutTicks + 1) return 0;
        primed_ = true;
        accumulator_ = kFieldSeconds; // the first step at once
    }
    level_ += (double(waiting) - level_) * std::min(1.0, elapsed / kLevelSeconds);
    ratio_ = 1.0 + std::clamp((level_ - kTargetLevel) * kMaxPaceDeviation, -kMaxPaceDeviation, kMaxPaceDeviation);
    if (waiting > kBacklogTicks + kPlayoutTicks) {
        // A stall on the downlink: run the backlog now, keep the buffer.
        catching_up_ = true;
        accumulator_ = 0;
        level_ = kTargetLevel;
        return int(waiting - kPlayoutTicks);
    }
    accumulator_ += elapsed * ratio_;
    int due = int(std::floor(accumulator_ / kFieldSeconds));
    if (due <= 0) return 0;
    if (std::size_t(due) > waiting) {
        starved_ += std::size_t(due) - waiting;
        due = int(waiting);
        // Keep at most one field of debt: the next arrival runs at once,
        // nothing more piles up.
        accumulator_ = std::min(accumulator_ - double(due) * kFieldSeconds, kFieldSeconds);
        return due;
    }
    accumulator_ -= double(due) * kFieldSeconds;
    return due;
}

} // namespace dw4::game::online
