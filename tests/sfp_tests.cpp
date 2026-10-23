#include "dw4/axfx.hpp"
#include "dw4/sfp.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
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

// The bank above as allse.sfp has it: bank id 0, an effect table and AUX
// sends on the splits.
std::vector<std::uint8_t> effect_bank() {
    auto d = bank();
    d[0x4c] = 0xff;
    d[0x4d] = 0;
    put32(d, 0x70, 0x600);
    put32(d, 0x600, 1); // last preset
    put32(d, 0x604, 0x0c);
    put32(d, 0x608, 0x24);
    put32(d, 0x60c, 0x00ff0000); // ReverbStd on AUX A
    for (std::size_t i = 0; i < 5; ++i) put32(d, 0x610 + i * 4, std::uint32_t(i == 0 ? 1400 : i == 1 ? 20 : i == 4 ? 1000 : 400));
    put32(d, 0x624, 0x02000000); // Chorus on A (3 words), ReverbStd on B
    for (std::size_t i = 0; i < 8; ++i) put32(d, 0x628 + i * 4, std::uint32_t(100 + i));
    put(d, 0x2b0 + 0x15, {0x00, 0x02, 0x40, 0x7f});
    d[0x2f0 + 0x15] = 1;
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

void effects() {
    const sfp::Bank plain(bank());
    check(plain.id() == 0 && !plain.effect(0) && plain.program_type() == 0, "Bank without an effect table");
    auto d = bank();
    d[0x4d] = 0xff;
    check(sfp::Bank(d).id() == 0xff, "Bank id");
    const sfp::Bank b(effect_bank());
    const auto first = b.effect(0);
    check(first && first->type_a == 0 && first->type_b == 0xff && first->b.empty(), "ReverbStd preset types");
    check(first->a == std::vector<std::uint32_t>{1400, 20, 400, 400, 1000}, "ReverbStd preset parameters");
    const auto second = b.effect(1);
    check(second && second->type_a == 2 && second->a == std::vector<std::uint32_t>{100, 101, 102}, "AUX A words by type");
    check(second->type_b == 0 && second->b == std::vector<std::uint32_t>{103, 104, 105, 106, 107}, "AUX B after AUX A");
    check(!b.effect(2) && !b.effect(-1), "Past the last preset");
    check(b.split(0)->aux_a == 0x40 && b.split(0)->aux_mode == 2 && b.split(0)->aux_b == 0x7f && !b.split(0)->channel_aux,
          "Split AUX sends");
    check(b.split(1)->channel_aux && !b.split(1)->aux_a, "Channel AUX sends");
}

// AXFX ReverbStd (80242848 / 80242CC4) with allse.sfp preset 1.
void reverb() {
    axfx::ReverbStd r;
    check(!r.init(0.4f, 0.005f, 1.0f, 0.4f, 0.02f) && !r.active(), "Reverb time below 10 ms");
    check(!r.init(0.4f, 1.4f, 1.0f, 0.4f, 0.2f) && !r.init(1.5f, 1.4f, 1.0f, 0.4f, 0.0f), "Reverb ranges");
    check(r.init(0.4f, 1.4f, 1.0f, 0.4f, 20.0f / 1000.0f) && r.active(), "Reverb init");
    // An impulse: 640 pre-delay samples delay by 639 (the ring wraps at its
    // last element), the 1789-sample comb answers first; then all-pass
    // -0.4, low-pass 0.3, all-pass -0.4 again and the 0.6 output level:
    // 10000 -> -4000 -> -1200 -> 480 -> 288.
    std::vector<std::int32_t> left;
    for (int frame = 0; frame < 20; ++frame) {
        axfx::AuxFrame data{};
        if (frame == 0) data[0] = 10000;
        r.process(data);
        for (std::size_t i = 0; i < axfx::kFrameSamples; ++i) {
            left.push_back(data[i]);
            check(data[axfx::kFrameSamples + i] == 0 && data[2 * axfx::kFrameSamples + i] == 0, "Channels stay apart");
        }
    }
    bool silent = true;
    for (std::size_t i = 0; i < 2428; ++i) silent = silent && left[i] == 0;
    check(silent && left[2428] == 288, "Reverb impulse response");
    // The low-pass state, the second all-pass (its read comes a step after
    // the write: 150 samples) and the second comb (1999).
    check(left[2429] == 181 && left[2577] == 0 && left[2578] == -604 && left[2638] == 288, "Reverb tail");
    long long total = 0;
    for (const auto value : left) total += value < 0 ? -value : value;
    check(total == 14775, "Reverb impulse energy");
    // Mix 0 without pre-delay: 0.6 of the input, truncated.
    check(r.init(0.4f, 1.4f, 0.0f, 0.4f, 0.0f), "Dry init");
    axfx::AuxFrame dry{};
    dry[3] = 10000;
    dry[4] = -10001;
    r.process(dry);
    check(dry[3] == 6000 && dry[4] == -6000, "Dry level");
    r.shutdown();
    dry[3] = 1234;
    r.process(dry);
    check(!r.active() && dry[3] == 1234, "Shut down");
    // The damping below 0.05 becomes 0.05 (80242BA8..80242BB8: fcmpo + bge),
    // a NaN too: it passes the range checks and then answers like 0.0.
    auto impulse = [](float damping) {
        axfx::ReverbStd effect;
        std::vector<std::int32_t> out;
        if (!effect.init(0.4f, 1.4f, 1.0f, damping, 0.0f)) return out;
        for (int frame = 0; frame < 40; ++frame) {
            axfx::AuxFrame data{};
            if (frame == 0) data[0] = 10000;
            effect.process(data);
            out.insert(out.end(), data.begin(), data.begin() + axfx::kFrameSamples);
        }
        return out;
    };
    const auto low = impulse(0.0f), clamped = impulse(0.05f), nan = impulse(std::numeric_limits<float>::quiet_NaN());
    check(!low.empty() && low == clamped && low != impulse(0.4f), "Damping below 0.05 takes 0.05");
    check(nan == low && std::none_of(nan.begin(), nan.end(), [](std::int32_t v) { return v == std::numeric_limits<std::int32_t>::min(); }),
          "A NaN damping takes 0.05");
}

// The AUX bus: a send comes back two frames later, after the callback.
void aux_bus() {
    axfx::ReverbStd dry;
    dry.init(0.4f, 1.4f, 0.0f, 0.4f, 0.0f);
    axfx::AuxBus bus;
    std::array<std::int32_t, 2 * axfx::kFrameSamples> sends{};
    check(bus.begin_frame(nullptr) == nullptr, "No callback, no AUX command");
    bus.end_frame(sends);
    std::vector<std::int32_t> back;
    for (int frame = 0; frame < 4; ++frame) {
        const auto* returned = bus.begin_frame(&dry);
        check(returned != nullptr, "AUX command with a callback");
        back.push_back((*returned)[5]);
        back.push_back((*returned)[axfx::kFrameSamples + 5]);
        sends.fill(0);
        if (frame == 0) {
            sends[10] = 10000; // left sample 5
            sends[11] = 5000;  // right sample 5
        }
        bus.end_frame(sends);
    }
    check(back == std::vector<std::int32_t>{0, 0, 0, 0, 6000, 3000, 0, 0}, "Two frames of AUX latency");
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
        effects();
        reverb();
        aux_bus();
        sequences();
        adpcm();
        malformed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " SFP checks passed\n";
}
