#pragma once

#include "dw4/pad.hpp"

#include <array>
#include <cstdint>

namespace dw4 {

// The console reset path, one call per frame (59.94 Hz field).
//
// The game's own soft reset (STAGECTL 80347BB4: START pressed while B and X
// are held, kept for a second, outside the title stages) only returns to the
// title (NEXTSTAGE stagIS10 at 0x80347EDC). A reboot comes from the console's
// reset button: beSystem's update 80316258 reads OSGetResetButtonState
// (802601B8) into +0x60 (the previous value into +0x61) and latches the
// release (+0x62 = previous && !current, read only while unlatched); while the
// reset is enabled (+0x5F, set at start, cleared by a disc error 80316974 and
// around memory card work) it turns the picture black (VISetBlack(TRUE),
// VIFlush, VIWaitForRetrace) and restarts the disc (DVDCheckDisk, then
// OSResetSystem(OS_RESET_RESTART) at 0x80316304, OS_RESET_HOTRESET at
// 0x80316318 without a disc): the apploader loads the game again and it boots
// with the license screen, the memory card check, the logos and the opening.
//
// The controller presses that button too: B, X and START held together (A
// and Y released) for more than three seconds tap it for half a second
// (embedded in the controller hardware; Dolphin's CSIDevice_GCController::
// HandleButtonCombos and ProcessorInterface ResetButton_Tap, commit
// 4c22bea4). Holding the game's soft reset combination that long therefore
// reboots: the soft reset starts after a second, the reboot follows (Dolphin
// run work/dref/r29reset d_reset3s; held for 2 s the game only returns to the
// title, d_reset2s).
//
// Field counts (one call per field, the press seen by the call that taps):
//  - the combination's timer starts at the first field it is held and taps at
//    the first poll more than 3 s later: 180 fields (3.003 s), then starts
//    again if it is still held;
//  - the button is released 0.5 s (29.97 fields) after the tap: the call 30
//    fields later reads it released;
//  - OSGetResetButtonState reports a press only from its second sample (the
//    first sets Down and answers the hold, 0x80260210) and keeps reporting a
//    released button pressed for 40 ms (the time of the first released sample,
//    0x802602E8, then 40 * (bus clock / 4000) ticks): the third call after the
//    release reads it released, the latch closes 33 fields after the tap.
class ConsoleReset {
public:
    static constexpr int kComboFields = 180;
    static constexpr int kButtonFields = 30;
    static constexpr int kReleaseHoldFields = 3;
    // The controller's combination: these buttons held, A and Y released
    // (Dolphin compares the face and START bits, the D-pad, L, R and Z do
    // not matter).
    static constexpr std::uint32_t kComboMask = pad_button::a | pad_button::b | pad_button::x | pad_button::y | pad_button::start;
    static constexpr std::uint32_t kCombo = pad_button::b | pad_button::x | pad_button::start;

    // The frame's controller samples (before the game's input disable: the
    // combination is the controller's). True when beSystem reboots in this
    // frame.
    bool update(const std::array<PadSample, 4>& pads) {
        if (button_fields_ > 0 && --button_fields_ == 0) button_down_ = false;
        for (std::size_t i = 0; i < pads.size(); ++i) {
            const bool held = pads[i].connected && (pads[i].buttons & kComboMask) == kCombo;
            if (!held) {
                combo_fields_[i] = 0;
                continue;
            }
            if (++combo_fields_[i] > kComboFields) {
                combo_fields_[i] = 0;
                tap();
            }
        }
        // 80316258: OSGetResetButtonState only while the release is not latched.
        if (!latched_) {
            previous_ = current_;
            current_ = read_button();
            latched_ = previous_ && !current_;
        }
        return enabled_ && latched_;
    }
    // beSystem +0x5F.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool button_down() const { return button_down_; }
    bool latched() const { return latched_; }
    bool operator==(const ConsoleReset&) const = default;

    // Save state fields, in this order.
    template <class Archive> void serialize(Archive& a) {
        a.io(combo_fields_);
        a.io(button_down_);
        a.io(button_fields_);
        a.io(os_down_);
        a.io(os_hold_);
        a.io(os_last_);
        a.io(current_);
        a.io(previous_);
        a.io(latched_);
        a.io(enabled_);
    }

private:
    void tap() {
        // A second tap while the button is down keeps the first release.
        if (!button_down_) button_fields_ = kButtonFields;
        button_down_ = true;
    }
    // OSGetResetButtonState (802601B8) in fields.
    bool read_button() {
        bool state;
        if (button_down_) {
            if (os_down_) {
                state = true;
            } else {
                os_down_ = true;
                state = os_hold_ > 0;
            }
        } else if (os_down_) {
            os_down_ = false;
            state = os_last_;
            os_hold_ = state ? kReleaseHoldFields : 0;
        } else if (os_hold_ > 0 && --os_hold_ > 0) {
            state = true;
        } else {
            os_hold_ = 0;
            state = false;
        }
        os_last_ = state;
        return state;
    }
    std::array<std::int32_t, 4> combo_fields_{};
    bool button_down_{};
    std::int32_t button_fields_{};
    bool os_down_{};
    std::int32_t os_hold_{};
    bool os_last_{};
    bool current_{}, previous_{}, latched_{};
    bool enabled_ = true;
};

} // namespace dw4
