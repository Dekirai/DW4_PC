#include "dw4/sfp.hpp"

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
    throw std::runtime_error("Malformed SFP data was accepted");
}

void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = std::uint8_t(v >> 8);
    d[at + 1] = std::uint8_t(v);
}
void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    put16(d, at, v >> 16);
    put16(d, at + 2, v & 0xffff);
}
void put(std::vector<std::uint8_t>& d, std::size_t at, std::initializer_list<int> bytes) {
    for (int b : bytes) d[at++] = std::uint8_t(b);
}

// Tables: curve 0x100, requests 0x200, drum 0x280, samples 0x400, PCMD 0x800.
std::vector<std::uint8_t> bank() {
    std::vector<std::uint8_t> d(0x900, 0);
    std::memcpy(&d[0], "gcaxDTPK", 8);
    std::memcpy(&d[0x20], "gcaxTBLD", 8);
    put32(d, 0x18, 0x100);
    put32(d, 0x1c, 0x800);
    std::memcpy(&d[0x800], "gcaxPCMD", 8);
    put32(d, 0x60, 0xfc);
    put32(d, 0x64, 0x200);
    put32(d, 0x68, 0x280);
    put32(d, 0x78, 0x400);
    for (int i = 0; i < 128; ++i) d[0x100 + std::size_t(i)] = std::uint8_t(i * 2 + 1);
    // Request group A905 with two sequences.
    d[0x200] = 0;
    put32(d, 0x204, 0xa9050010);
    d[0x210] = 1;
    put32(d, 0x214, 0x20);
    put32(d, 0x218, 0x28);
    put(d, 0x220, {0xc0, 0xdf, 0x00, 0x7f, 0x80, 0xff});
    put(d, 0x228, {0xc0, 0xdf, 0x01, 0x64, 0x00, 0x82, 0x02, 0xdf, 0x00, 0x7f, 0x80, 0xff});
    // Drum program: layer at +0x10, two splits.
    put16(d, 0x288, 0x10);
    put16(d, 0x290, 1);
    put(d, 0x2b0, {0x00, 0x80, 0x01, 0x01, 0, 0, 0x40, 0x80, 0x0f, 0xff, 0xfb, 0x01});
    put(d, 0x2f0, {0x01, 0x80, 0x01, 0x00, 0, 0, 0x30, 0x80, 0x0f, 0xff, 0xfb, 0x01});
    d[0x2f0 + 0x14] = 0x28;
    // Samples: two frames each, zero coefficients (output = nibble * scale).
    d[0x403] = 1;
    for (int i = 0; i < 2; ++i) {
        const std::size_t at = 0x404 + std::size_t(i) * 0x44;
        put32(d, at, i == 0 ? 0x20 : 0x30);
        put32(d, at + 4, i == 0 ? 2 : 18);
        put32(d, at + 8, 31);
        put16(d, at + 0x3a, i == 0 ? 0x0200 : 0x0600);
        put16(d, at + 0x3c, 24000);
        put32(d, at + 0x40, 16);
    }
    put(d, 0x820, {0x00, 0x12, 0x34, 0x56, 0x7f, 0x80, 0x00, 0x00, 0x02, 0x1f, 0, 0, 0, 0, 0, 0});
    return d;
}

void tables() {
    const sfp::Bank b(bank());
    check(b.sample_count() == 2 && b.sample(1).loop && !b.sample(0).loop, "Sample records");
    check(b.sample(0).rate == 24000 && b.sample(0).bytes == 16 && b.sample(1).start == 0x30, "Sample fields");
    check(b.split(0)->sample == 1 && b.split(1)->sample == 0 && b.split(1)->pan == 0x30, "Splits");
    check(b.split(0)->semitones == -5 && b.split(0)->cents == 1 && b.split(1)->detune == 40 && !b.split(0)->detune,
          "Split tuning");
    check(b.split(9) && b.split(9)->key == 1, "Nearest lower split");
    check(b.level(0) == 1 && b.level(127) == 255, "Velocity curve");
    check(b.has_group(0xa9050100) && !b.has_group(0xa9060000), "Request groups");
    check(b.sequence(0xa9060000).empty() && b.sequence(0xa9050200).empty(), "Missing requests");
}

void sequences() {
    const sfp::Bank b(bank());
    sfp::Sequence one(b.sequence(0xa9050000));
    auto e = one.next();
    check(e.kind == sfp::Event::Kind::note && e.status == 0x9f && e.key == 0 && e.velocity == 0x7f && e.wait_ms == 0,
          "One-shot note");
    check(one.next().kind == sfp::Event::Kind::end && one.finished(), "Sequence end");
    sfp::Sequence two(b.sequence(0xa9050100));
    e = two.next();
    check(e.key == 1 && e.velocity == 0x64 && e.wait_ms == 258, "Delta time");
    e = two.next();
    check(e.kind == sfp::Event::Kind::note && e.key == 0 && e.wait_ms == 0, "Second note");
    check(two.next().kind == sfp::Event::Kind::end, "Two-note end");
}

void adpcm() {
    const sfp::Bank b(bank());
    std::uint32_t loop = 99;
    const auto pcm = b.decode(0, &loop);
    check(pcm.size() == 28 && loop == 0, "Decoded length");
    check(pcm[0] == 1 && pcm[1] == 2 && pcm[6] == 7 && pcm[7] == -1 && pcm[8] == -8, "First frame nibbles");
    check(pcm[14] == 4 && pcm[15] == -4, "Second frame scale");
    b.decode(1, &loop);
    check(loop == 14, "Loop start in samples");
}

void malformed() {
    rejects([] {
        auto d = bank();
        d[0] = 'x';
        sfp::Bank b(std::move(d));
    });
    rejects([] {
        auto d = bank();
        std::memset(&d[0x800], 0, 8);
        sfp::Bank b(std::move(d));
    });
    rejects([] {
        auto d = bank();
        d[0x2b3] = 5; // split names a missing sample
        sfp::Bank b(std::move(d));
    });
    rejects([] {
        auto d = bank();
        put32(d, 0x404 + 0x40, 0x1000); // sample past PCMD
        sfp::Bank b(std::move(d));
    });
}
} // namespace

int main() {
    try {
        tables();
        sequences();
        adpcm();
        malformed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " SFP checks passed\n";
}
