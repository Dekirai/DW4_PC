#include "pad_wire.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::game::online {

std::int8_t quantize_axis(float value) noexcept {
    if (!std::isfinite(value)) return 0;
    const float scaled = std::round(std::clamp(value, -1.0f, 1.0f) * float(kStickSteps));
    return std::int8_t(std::clamp(int(scaled), -kStickSteps, kStickSteps));
}

WirePad quantize_pad(const PadSample& sample) noexcept {
    WirePad pad;
    pad.buttons = std::uint16_t(sample.buttons & 0xFFFFu);
    pad.lx = quantize_axis(sample.left.x);
    pad.ly = quantize_axis(sample.left.y);
    pad.rx = quantize_axis(sample.right.x);
    pad.ry = quantize_axis(sample.right.y);
    return pad;
}

PadSample dequantize_pad(const WirePad& pad, bool connected) noexcept {
    PadSample sample;
    sample.connected = connected;
    if (!connected) return sample; // an absent slot plays the neutral pad (6.5)
    sample.buttons = pad.buttons;
    sample.left = {float(pad.lx) / float(kStickSteps), float(pad.ly) / float(kStickSteps)};
    sample.right = {float(pad.rx) / float(kStickSteps), float(pad.ry) / float(kStickSteps)};
    return sample;
}

void write_pad(std::uint8_t* out, const WirePad& pad) noexcept {
    out[0] = std::uint8_t(pad.buttons);
    out[1] = std::uint8_t(pad.buttons >> 8);
    out[2] = std::uint8_t(pad.lx);
    out[3] = std::uint8_t(pad.ly);
    out[4] = std::uint8_t(pad.rx);
    out[5] = std::uint8_t(pad.ry);
    out[6] = pad.flags;
}

WirePad read_pad(const std::uint8_t* in) noexcept {
    WirePad pad;
    pad.buttons = std::uint16_t(in[0] | (in[1] << 8));
    pad.lx = std::int8_t(in[2]);
    pad.ly = std::int8_t(in[3]);
    pad.rx = std::int8_t(in[4]);
    pad.ry = std::int8_t(in[5]);
    pad.flags = in[6];
    return pad;
}

} // namespace dw4::game::online
