#pragma once

#include "dw4/engine/math.hpp"

#include <cmath>
#include <cstdint>

namespace dw4::engine {

// GX holds every colour in 8 bits per channel: the channel material and
// ambient registers, the light colours, the fog and copy clear colours and the
// vertex colours. The original converts its float colours with one of two
// rules, depending on where the colour comes from; every constant colour the
// port hands to the GPU goes through the rule of its original source, so a
// fade at 127.54 / 255 is drawn at 128 like the original, not at 127.5.

// 8012E1D8(colour, 1): each channel clamped to the igVec4f bounds 0 and 1
// (0x804F8BE0 / 0x804F8BF0, set at 8012F804..8012F820), then
// (int)(c * 255 + 0.5): fmadds (one rounding of the fused product) and
// fctiwz, i.e. round half up. igColorAttr::setColor (800C6A70 -> 800BDAB8)
// packs its colour this way for its apply (800BDA4C -> the visual
// context's +0x444, the material or ambient register of geometry without
// vertex colours, 800FA274): the IGB colour attributes, the FADESET /
// FLASHSET overrides, TEXTCTL line colours and the projective shadow's
// _shadowColor. igClearAttr packs the copy clear colour the same way
// (0x800BD8DC, apply 800BD7D0 -> 800EC01C), the particle arrays their
// vertex colours (800E0684, 800E0B5C, 800E0E60).
// A NaN is not clamped: both bounds are fcmpo tests whose branch skips the
// clamp when the compare is unordered (bc 4,1 at 0x8012E1FC, bc 4,0 at
// 0x8012E258), and fctiwz turns the NaN into 0x80000000, a 0 byte. (The
// packing ORs the unshifted channel unmasked, so a NaN there would also set
// the top bit of the highest channel, the alpha of setColor's format 1 at
// 0x8012E428; the port's colours are finite, the particles' read_f32 drops
// non-finite values, so that spill is not modelled.)
inline std::uint8_t gx_round_byte(float c) {
    if (c != c) return 0;
    if (c > 1.0f) c = 1.0f;
    if (c < 0.0f) c = 0.0f;
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(std::fma(c, 255.0f, 0.5f)));
}

// Truncation, (int)(255 * c), in the lighting flush 800F6DC0 and the fog
// colour (800F9604, fctiwz of 255 * c). Nothing is clamped below 0 there: a
// channel below 0 would wrap its byte, the port clamps it instead. The upper
// clamps differ in how they treat a NaN:
// - gx_trunc_byte: the ambient register (0x800F7604..0x800F76CC) and the
//   material colour channels (0x800F76DC..0x800F77C4) clamp with fcmpo +
//   bge (bc 4,0), which takes an unordered compare to 1 as well: NaN -> 255.
// - gx_trunc_light_byte: the light colours compare 255 * c with 255 and
//   branch with ble (bc 4,1 at 0x800F7244..0x800F7274), the fog colour is
//   not clamped at all: a NaN reaches fctiwz, 0x80000000, a 0 byte.
inline std::uint8_t gx_trunc_byte(float c) {
    if (!(c < 1.0f)) c = 1.0f;
    if (c < 0.0f) c = 0.0f;
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(c * 255.0f));
}
inline std::uint8_t gx_trunc_light_byte(float c) {
    if (c != c) return 0;
    return gx_trunc_byte(c);
}

// The same values as the GPU sees them: the 8-bit value / 255.
inline float gx_round(float c) { return float(gx_round_byte(c)) / 255.0f; }
inline float gx_trunc(float c) { return float(gx_trunc_byte(c)) / 255.0f; }
inline float gx_trunc_light(float c) { return float(gx_trunc_light_byte(c)) / 255.0f; }
inline Vec4f gx_round(const Vec4f& c) { return {gx_round(c.x), gx_round(c.y), gx_round(c.z), gx_round(c.w)}; }
inline Vec4f gx_trunc(const Vec4f& c) { return {gx_trunc(c.x), gx_trunc(c.y), gx_trunc(c.z), gx_trunc(c.w)}; }

} // namespace dw4::engine
