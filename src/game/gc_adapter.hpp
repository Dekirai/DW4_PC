#pragma once

// The GameCube controller adapter for Wii U / Switch on Windows
// (docs/ROUND_PADS_NOTES.md 2): Nintendo's WUP-028 and adapters in its mode
// (Mayflash's "Wii U" switch position), USB 057e:0337. SDL opens this adapter
// only through libusb, which this build does not include; with the WinUSB
// driver on it (Zadig installs it, as for Dolphin) this reader opens it
// through Windows' own WinUSB API and gives each connected port to SDL as a
// virtual GameCube controller with the ids, name and button layout of SDL's
// own GameCube driver. Detection, the GameCube binding space and the
// prompts then treat it like any other GameCube pad.
//
// An adapter in "PC" mode (Mayflash 0079:1843 / 1844) is an ordinary HID
// device that SDL's own driver reads; it needs none of this.
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace dw4::game {

// One port of an adapter report.
struct GcPortState {
    bool connected = false;
    std::uint8_t buttons1 = 0, buttons2 = 0; // as reported: A B X Y left right down up / Start Z R L
    std::uint8_t stick_x = 128, stick_y = 128, c_x = 128, c_y = 128, l = 0, r = 0;
};
// A 37-byte report (0x21, then nine bytes per port; the layout of SDL's
// SDL_hidapi_gamecube.c); false when it is none.
bool parse_gc_adapter_report(const std::uint8_t* data, std::size_t size, std::array<GcPortState, 4>& ports);

// What a port's virtual controller reports (buttons by SDL_GamepadButton,
// axes by SDL_GamepadAxis): A on SOUTH, X on EAST, B on WEST, Y on NORTH,
// Z on the right shoulder, the trigger clicks on MISC3 / MISC4 (SDL's own
// GameCube layout). `origin`: the port's sticks and triggers at rest (the
// GameCube calibrates them when a controller connects).
struct GcVirtualState {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
};
GcVirtualState gc_virtual_state(const GcPortState& port, const GcPortState& origin);

class GcAdapter {
public:
    GcAdapter();
    ~GcAdapter();
    GcAdapter(const GcAdapter&) = delete;
    GcAdapter& operator=(const GcAdapter&) = delete;
    // Once per frame on the main thread (after SDL's gamepad subsystem is
    // up): a background thread looks for the adapter every two seconds and
    // reads it; this keeps one SDL virtual joystick per connected port up
    // to date (their GAMEPAD_ADDED / REMOVED events follow as for any pad).
    void update();
    // The adapter is open (WinUSB) and its ports are read.
    bool open() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dw4::game
