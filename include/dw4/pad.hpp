#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dw4 {

// Original logical button bits produced by GDJEB2's GameCube device adapter.
// These differ from the raw PADStatus bits and from SDL button indices.
namespace pad_button {
inline constexpr std::uint32_t start = 0x0008;
inline constexpr std::uint32_t up = 0x0010;
inline constexpr std::uint32_t right = 0x0020;
inline constexpr std::uint32_t down = 0x0040;
inline constexpr std::uint32_t left = 0x0080;
inline constexpr std::uint32_t z = 0x0200;
inline constexpr std::uint32_t l = 0x0400;
inline constexpr std::uint32_t r = 0x0800;
inline constexpr std::uint32_t y = 0x1000;
inline constexpr std::uint32_t x = 0x2000;
inline constexpr std::uint32_t a = 0x4000;
inline constexpr std::uint32_t b = 0x8000;
} // namespace pad_button

namespace pad_direction {
inline constexpr std::uint32_t positive_y = 1;
inline constexpr std::uint32_t positive_x = 2;
inline constexpr std::uint32_t negative_y = 4;
inline constexpr std::uint32_t negative_x = 8;
} // namespace pad_direction

struct PadStick {
    float x{}, y{};
    bool operator==(const PadStick&) const = default;
};

// A sampled logical device after any platform conversion. Original analog
// values use positive Y upward; they are retained without radial rescaling.
struct PadSample {
    bool connected{};
    PadStick left{}, right{};
    std::uint32_t buttons{};
};

struct PadMaskState {
    std::uint32_t held{}, previous{}, trigger{}, repeat{};
    // Guest decrement wraps at 32 bits, including when idle at reset.
    std::uint32_t repeat_countdown{};
    bool operator==(const PadMaskState&) const = default;
};

struct PadState {
    bool connected{};
    PadStick left{}, right{};
    PadMaskState left_directions{}, right_directions{}, buttons{};
    bool operator==(const PadState&) const = default;
};

// GDJEB2 800CCF5C: ignores raw PADStatus bits that have no logical mapping.
std::uint32_t gamecube_buttons(std::uint16_t raw_buttons) noexcept;

// One pad's host controls before the device conversion: keyboard keys and
// controller inputs bound to GameCube controls, and a controller's analog
// sticks. The main stick, the D-pad and the C-stick are separate channels
// like on a GameCube pad: the original reads them in different places (the
// player walks with the main stick only, the quick panels' cursor takes the
// C-stick and the D-pad, menus the main stick and the D-pad).
struct HostPadInput {
    // pad_button bits: A, B, X, Y, START, L, R, Z and the four D-pad bits.
    std::uint32_t buttons{};
    // pad_direction bits of digital main stick / C-stick inputs (keys).
    std::uint32_t stick_directions{}, c_stick_directions{};
    // Analog sticks in host units ([-1,1] per axis, y up, deadzone applied).
    PadStick stick{}, c_stick{};
};

// Adds `other`'s inputs (a second device of the same player): buttons and
// digital directions join, a moved analog stick replaces a centred one.
void merge(HostPadInput& into, const HostPadInput& other) noexcept;

// The logical sample of a connected pad. A stick takes its analog value when
// it is moved, else its digital directions at full deflection; each axis is
// limited to [-1,1] and scaled to the original device's full deflection
// after the SDK clamp (axis limit / 74: 72/74 main stick, 59/74 C-stick).
// D-pad bits never move a stick and stick keys never set D-pad bits.
PadSample host_pad_sample(const HostPadInput& input) noexcept;

// SDK ClampStick/PADClamp followed by the original device's division by 74.
// Pass raw signed PADStatus axes, not already normalized host input. The
// left stick uses (deadzone, axis, diagonal) = (15,72,40); C-stick = (15,59,31).
// Hardware calibration, PADRead errors, analog triggers and rumble are outside
// this helper. It does not scale the octagonal result to a unit circle.
PadStick gamecube_stick(std::int8_t x, std::int8_t y, bool c_stick = false) noexcept;

// Recovered bePadData profile: 0.4 activation radius, pi/4 angular overlap,
// and 10/6 update-count repeats. Direction masks accept either diagonal bit
// pair; button and each stick channel have separate timers. Native sqrt/atan2
// reproduce the recovered formulas, without a claim of bitwise PPC libm parity
// at floating-point angular boundaries. See docs/research/PLAYER_MOVEMENT_RESEARCH.md.
class OriginalPads {
public:
    static constexpr std::size_t slot_count = 4;
    using Samples = std::array<PadSample, slot_count>;
    using States = std::array<PadState, slot_count>;

    // Call once per original pad-manager update, not per presentation frame.
    // A frozen update retains every field, including trigger/repeat outputs.
    // A disconnected sample supplies zero input through the usual release path.
    // Connected axes must be finite and within [-2,2], covering original signed
    // byte conversion even before SDK clamp. Invalid input leaves all slots
    // unchanged. Disconnected/frozen samples' unused axes are not inspected.
    // This models four physical slots; the original fifth special/redirected
    // pad, device enumeration, and slot ownership remain caller responsibilities.
    void update(const Samples& samples, bool frozen = false);
    void reset() noexcept { states_ = {}; }
    // Save states: the slots as they were.
    void restore(const States& states) noexcept { states_ = states; }
    const States& states() const noexcept { return states_; }

private:
    States states_{};
};

} // namespace dw4
