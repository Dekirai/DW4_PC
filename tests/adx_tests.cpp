#include "dw4/adx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation) {
    ++checks;
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed ADX data was accepted");
}

void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = std::uint8_t(v >> 8);
    d[at + 1] = std::uint8_t(v);
}
void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    put16(d, at, v >> 16);
    put16(d, at + 2, v & 0xffff);
}

// A version 4 mono stream of `blocks` blocks (32 samples each), header 0x40.
std::vector<std::uint8_t> stream(std::uint32_t blocks, bool loop, std::uint32_t loop_begin, std::uint32_t loop_end) {
    const std::uint32_t copyright = 0x3c;
    std::vector<std::uint8_t> d(copyright + 4 + blocks * 18, 0);
    put16(d, 0, 0x8000);
    put16(d, 2, copyright);
    d[4] = 3;
    d[5] = 18;
    d[6] = 4;
    d[7] = 1;
    put32(d, 8, 48000);
    put32(d, 12, blocks * 32);
    put16(d, 16, 500);
    d[18] = 4;
    if (loop) {
        put32(d, 0x24, 1);
        put32(d, 0x28, loop_begin);
        put32(d, 0x30, loop_end);
    }
    std::memcpy(&d[copyright - 2], "(c)CRI", 6);
    return d;
}

void header() {
    const auto h = adx::read_header(stream(2, true, 32, 64));
    check(h.data_offset == 0x40 && h.block_size == 18 && h.channels == 1, "Header layout");
    check(h.sample_rate == 48000 && h.samples == 64, "Header rate and length");
    check(h.loop && h.loop_begin == 32 && h.loop_end == 64, "Version 4 loop points");
    const double z = std::cos(2.0 * 3.14159265358979323846 * 500.0 / 48000.0);
    const double a = std::sqrt(2.0) - z, b = std::sqrt(2.0) - 1.0;
    const double c = (a - std::sqrt((a + b) * (a - b))) / b;
    check(h.coef1 == std::int16_t(c * 8192.0) && h.coef2 == std::int16_t(c * c * -4096.0), "Predictor coefficients");
    auto bad = stream(1, false, 0, 0);
    bad[0] = 0;
    rejects([&] { adx::read_header(bad); });
    auto encrypted = stream(1, false, 0, 0);
    encrypted[19] = 8;
    rejects([&] { adx::read_header(encrypted); });
}

void decoding() {
    // First block: scale 0x0fff + 1 = 4096; nibbles 7, -1, then zeros.
    auto d = stream(1, false, 0, 0);
    put16(d, 0x40, 0x0fff);
    d[0x42] = 0x7f;
    adx::Decoder decoder(d);
    const auto h = decoder.header();
    std::vector<std::int16_t> pcm(40, 1);
    const auto got = decoder.read(pcm.data(), 40);
    check(got == 32 && decoder.finished(), "One block decodes 32 frames, then the stream ends");
    check(pcm[0] == 28672, "Sample without history is nibble * scale");
    const std::int32_t second = std::clamp(-4096 + ((h.coef1 * 28672) >> 12), -32768, 32767);
    check(pcm[1] == second, "Second sample adds the predicted history");
    check(decoder.read(pcm.data(), 8) == 0 && decoder.finished(), "Non-looping stream ends");
}

void looping() {
    // Two blocks, loop over the second: output keeps coming and repeats.
    auto d = stream(2, true, 32, 64);
    put16(d, 0x40 + 18, 0x0000);
    d[0x40 + 18 + 2] = 0x10; // second block: nibble 1 * scale 1
    adx::Decoder decoder(d);
    std::vector<std::int16_t> first(64), again(32);
    check(decoder.read(first.data(), 64) == 64, "Loop stream fills the first pass");
    check(decoder.read(again.data(), 32) == 32 && !decoder.finished(), "Loop keeps playing");
    check(again[0] == first[32], "Loop restarts at the loop begin with its saved history");
    decoder.set_looping(false);
    check(decoder.read(again.data(), 32) == 0 && decoder.finished(), "Looping off ends at the stream end");
}
} // namespace

int main() {
    try {
        header();
        decoding();
        looping();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "adx: " << checks << " checks passed\n";
    return 0;
}
