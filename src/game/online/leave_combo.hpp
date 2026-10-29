#pragma once

// The local leave combo (docs/SEAMLESS.md 7.4). Offline, B + X + START held
// for 180 fields on any connected pad presses the console's reset button.
// Online every engine runs with the console reset off (one member would
// reboot every machine), and the client watches only its own pad for the
// combo: held for the same 180 fields it opens "Leave room?" (presentation;
// nothing reaches the simulation). The combo must be released before it can
// fire again.
#include "dw4/pad.hpp"

#include <cstdint>

namespace dw4::game::online {

class LeaveCombo {
public:
    static constexpr int kHoldFields = 180;
    static constexpr std::uint32_t kButtons = pad_button::b | pad_button::x | pad_button::start;

    // One field of the local pad; true in the field the hold completes.
    bool update(std::uint32_t buttons) {
        if ((buttons & kButtons) != kButtons) {
            held_ = 0;
            fired_ = false;
            return false;
        }
        if (fired_) return false;
        if (++held_ < kHoldFields) return false;
        fired_ = true;
        return true;
    }
    int held() const { return held_; }
    void reset() { held_ = 0, fired_ = false; }

private:
    int held_ = 0;
    bool fired_ = false;
};

} // namespace dw4::game::online
