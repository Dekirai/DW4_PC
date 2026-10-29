#pragma once

// Pads on the wire (docs/SEAMLESS.md 6.4): online, a pad is quantized before
// it is sent. Each stick axis becomes the GameCube's step, round(v * 74) as an
// int8 in -74..74 (the values a GameCube stick gives the game are k / 74,
// pad.cpp gamecube_stick), and every machine executes k / 74.0f, so the
// sealed values are what every machine simulates. The buttons fit in 16 bits
// (every dw4::pad_button mask is below 0x10000; the test checks it).
//
// Wire layout, 6 bytes, little endian (the relay's Pad, SEAMLESS.md 6.4):
//   u16 buttons | i8 left.x | i8 left.y | i8 right.x | i8 right.y
// `connected` is not on the wire: it comes from the tick record's mask.
#include "dw4/pad.hpp"

#include <array>
#include <cstdint>

namespace dw4::game::online {

inline constexpr std::size_t kWirePadBytes = 6;
inline constexpr int kStickSteps = 74;

struct WirePad {
    std::uint16_t buttons{};
    std::int8_t lx{}, ly{}, rx{}, ry{};
    bool operator==(const WirePad&) const = default;
};

// One axis: round(v * 74) clamped to -74..74 (NaN -> 0).
std::int8_t quantize_axis(float value) noexcept;
// A host sample as it goes on the wire (connected is dropped, buttons above
// 16 bits are dropped).
WirePad quantize_pad(const PadSample& sample) noexcept;
// What every machine executes for a wire pad.
PadSample dequantize_pad(const WirePad& pad, bool connected) noexcept;
// A local sample as the simulation sees it online (quantize then dequantize).
inline PadSample online_pad(const PadSample& sample) noexcept { return dequantize_pad(quantize_pad(sample), sample.connected); }

void write_pad(std::uint8_t* out, const WirePad& pad) noexcept;
WirePad read_pad(const std::uint8_t* in) noexcept;

} // namespace dw4::game::online
