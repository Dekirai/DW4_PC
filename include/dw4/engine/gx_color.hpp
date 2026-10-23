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
// (0x804F8BE0 / 0x804F8BF0, set at 8012F804..8012F820; first the upper bound,
// so a NaN becomes 1), then (int)(c * 255 + 0.5): fmadds (one rounding of
// the fused product) and fctiwz, i.e. round half up. igColorAttr::setColor
// (800C6A70 -> 800BDAB8) packs its colour this way for its apply (800BDA4C
// -> the visual context's +0x444, the material or ambient register of
// geometry without vertex colours, 800FA274): the IGB colour attributes,
// the FADESET / FLASHSET overrides, TEXTCTL line colours and the projective
// shadow's _shadowColor. igClearAttr packs the copy clear colour the same
// way (0x800BD8DC, apply 800BD7D0 -> 800EC01C), the particle arrays their
// vertex colours (800E0684, 800E0B5C, 800E0E60).
inline std::uint8_t gx_round_byte(float c) {
    if (!(c <= 1.0f)) c = 1.0f;
    if (!(c >= 0.0f)) c = 0.0f;
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(std::fma(c, 255.0f, 0.5f)));
}

// Truncation, (int)(255 * c): the lighting flush 800F6DC0 (every register
// channel clamped to 1, fctiwz at 0x800F7228..0x800F727C and
// 0x800F7604..0x800F77C4) and the fog colour (800F9604, fctiwz of 255 * c).
// The fog colour is not clamped there, nothing is clamped below 0: a channel
// outside [0, 1] would wrap its byte, the port clamps it instead.
inline std::uint8_t gx_trunc_byte(float c) {
    if (!(c <= 1.0f)) c = 1.0f;
    if (!(c >= 0.0f)) c = 0.0f;
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(c * 255.0f));
}

// The same values as the GPU sees them: the 8-bit value / 255.
inline float gx_round(float c) { return float(gx_round_byte(c)) / 255.0f; }
inline float gx_trunc(float c) { return float(gx_trunc_byte(c)) / 255.0f; }
inline Vec4f gx_round(const Vec4f& c) { return {gx_round(c.x), gx_round(c.y), gx_round(c.z), gx_round(c.w)}; }
inline Vec4f gx_trunc(const Vec4f& c) { return {gx_trunc(c.x), gx_trunc(c.y), gx_trunc(c.z), gx_trunc(c.w)}; }

} // namespace dw4::engine
