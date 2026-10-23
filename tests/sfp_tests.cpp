#include "dw4/axfx.hpp"
#include "dw4/axmix.hpp"
#include "dw4/sfp.hpp"
#include "dw4/sndf.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
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

// The SNDF driver and the AX voice pool (src/sndf.cpp).
std::shared_ptr<const sndf::BankData> driver_bank() {
    auto data = std::make_shared<sndf::BankData>();
    data->bank = std::make_unique<sfp::Bank>(bank());
    for (std::size_t i = 0; i < data->bank->sample_count(); ++i) {
        std::uint32_t loop = 0;
        data->pcm.push_back(data->bank->decode(i, &loop));
        data->loop_start.push_back(loop);
    }
    return data;
}

void pitch() {
    // 8024583C / 8023F53C against the DOL's table at 804C4A98.
    const std::pair<int, std::uint32_t> ratios[] = {{-498, 49153}, {-499, 49124}, {-501, 49068}, {701, 98249}, {0, 65536},
                                                    {2400, 262144}, {2500, 262144}, {-2400, 16384}, {-1, 65498}, {-62, 63230}};
    for (const auto& [cents, ratio] : ratios) check(sndf::src_ratio(sndf::cents_ratio(cents)) == ratio, "SRC ratio of cents");
    check(sndf::cents_ratio(-2400) == 0.25f && sndf::cents_ratio(1200) == 2.0f, "Octaves are exact");
    // Split tuning: -5 semitones + 1/64 (803B9F14: 100 * semitones + 1.5625 * fine).
    const auto b = sfp::Bank(bank());
    check(b.split_at(0)->semitones == -5 && b.split_at(0)->cents == 1 && b.split_at(2) == nullptr, "Split by index");
}

void driver() {
    const auto data = driver_bank();
    const sndf::Driver::Banks banks{data};
    std::uint32_t random = 1;
    sndf::Driver d;
    d.ax().set_reserved(8);
    // SEQ_SET and SEQ_PLAY queue the request; GETSTATUS sees nothing until a tick runs it.
    check(d.seq_set(4, 0xa9050000u, 100) && d.seq_play(4, 100), "Request queued");
    check(d.status(4) == 0 && d.seqs()[4].state == 1, "Nothing runs before the tick");
    d.tick(0, 50, banks, random); // earlier than the request
    check(d.status(4) == 0, "A tick before the request's time leaves it queued");
    d.seq_volume(4, 127, 100);
    d.tick(1, 100, banks, random);
    // The one-shot sequence ended in the tick (state 3), its voice sounds.
    check(d.status(4) == 1 && d.seqs()[4].state == 3 && d.seqs()[4].voices == 1, "Voice of the sequence");
    const auto& v = d.voices()[0];
    check(v.used && v.seq == 4 && v.priority == 11 && v.ratio == 49153 && v.loop && v.start == 1,
          "Voice record: split 0 (sample 1, a loop), -498 cents");
    check(v.gain_from == v.gain_to && v.gain_to > 0.0f, "A new voice starts at its level");
    // A loop sounds until its sequence stops: 0xB035 keys it off, the release (0: at once) reaches -96 dB at the service,
    // the next tick frees it and its sequence (state 1).
    check(d.seq_stop(4, 200), "Stop queued");
    d.tick(2, 200, banks, random);
    check(d.voices()[0].used && d.voices()[0].key == 2 && d.voices()[0].gain_to == 0.0f && d.voices()[0].gain_from > 0.0f,
          "Key off ramps to silence across the next frame");
    check(d.voices()[0].priority == 10, "Key off lowers the AX priority");
    d.tick(3, 300, banks, random);
    check(!d.voices()[0].used && d.status(4) == 0 && d.seqs()[4].state == 1, "Released voice freed");

    // Request 1: key 1 (split 1: a one-shot with a random detune of +-40/64 semitone from rand()), then after 258 ms key 0.
    sndf::Driver e;
    std::uint32_t rnd = 1;
    e.seq_set(5, 0xa9050100u, 0);
    e.seq_play(5, 0);
    e.tick(0, 0, banks, rnd);
    check(rnd == 1103527590u, "rand() advanced once (8009C1CC)");
    // (16838 % 80) - 40 = -2: fine 1 - 2 = -1, -500 - 1.5625 = -501 cents.
    check(e.voices()[0].used && e.voices()[0].ratio == 49068 && !e.voices()[0].loop, "Detuned one-shot");
    check(e.status(5) == 3, "Track waiting and voice: both bits");
    // 28 samples at 0.749 run out in the first frame: freed at the next tick.
    check(e.voices()[0].end == 0, "One-shot runs out in its first frame");
    e.tick(1, 1, banks, rnd);
    check(!e.voices()[0].used && e.status(5) == 2, "Ran out: freed; the track still waits");
    for (int f = 2; f < 60 && e.status(5) == 2; ++f) e.tick(f, f, banks, rnd);
    check(e.voices()[0].used && e.voices()[0].loop && e.seqs()[5].state == 3, "Second note after the wait");

    // AX pool: two voices left; a key-on voice is never taken by an equal priority, a key-off one is (drop callback).
    sndf::Driver f;
    std::uint32_t r3 = 1;
    f.ax().set_reserved(62);
    for (int s = 1; s <= 3; ++s) {
        f.seq_set(s, 0xa9050000u, 0);
        f.seq_play(s, 0);
    }
    f.tick(0, 0, banks, r3);
    check(f.ax().in_use() == 64 && f.status(3) == 0 && f.seqs()[3].state == 3, "No voice for the third request");
    f.seq_stop(1, 1);
    f.tick(1, 1, banks, r3);
    f.seq_set(3, 0xa9050000u, 2);
    f.seq_play(3, 2);
    f.tick(2, 2, banks, r3); // the key-off voice of sequence 1 is freed first (released at once)
    check(f.status(3) == 1 && f.ax().in_use() == 64, "Freed voice reused");
    // A dropped record keeps its sequence for GETSTATUS (803C0B44).
    sndf::AxPool pool;
    pool.set_reserved(63);
    std::int64_t dropped = -1;
    const int a = pool.acquire(19, 7, dropped);
    check(a >= 0 && dropped == -1 && pool.acquire(19, 8, dropped) == -1, "Equal priority is not taken");
    check(pool.acquire(20, 9, dropped) == a && dropped == 7, "Lower priority taken, owner dropped");
}
// The AX DSP mix (axmix): the linear SRC's four-sample history, the
// compressor and its DOL table, the resampler.
void ax_mix() {
    // SRC type 1: the stateless voice sample equals the PB's running SRC.
    std::vector<std::int16_t> pcm(300);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = std::int16_t(int(i) * 97 - 14000);
    for (const std::uint32_t ratio : {49153u, 0x10000u, 98249u, 16384u}) {
        axmix::LinearSrc src;
        src.reset(ratio);
        std::size_t read = 0;
        bool same = true;
        for (std::uint64_t k = 0; k < 250; ++k) {
            const auto out = src.next([&](std::array<std::int16_t, 2>& frame) {
                if (read >= pcm.size()) return false;
                frame = {pcm[read], pcm[read]};
                ++read;
                return true;
            });
            same = same && out[0] == axmix::voice_sample(pcm.data(), pcm.size(), 0, false, k, ratio);
        }
        check(same, "Voice sample = running linear SRC");
    }
    // At ratio 1.0 the output is the input four samples back (three outputs of the zero history first).
    check(axmix::voice_sample(pcm.data(), pcm.size(), 0, false, 2, 0x10000) == 0 &&
              axmix::voice_sample(pcm.data(), pcm.size(), 0, false, 3, 0x10000) == pcm[0] &&
              axmix::voice_sample(pcm.data(), pcm.size(), 0, false, 10, 0x10000) == pcm[7],
          "Linear SRC lags its input by the history");
    // (s0 (65536 - f) + s1 f) >> 16: half way between two samples at ratio 0.5 (k = 8: 4.5 entered).
    check(axmix::voice_sample(pcm.data(), pcm.size(), 0, false, 8, 0x8000) == std::int16_t((pcm[0] + pcm[1]) >> 1),
          "Linear interpolation of the two oldest");
    // A loop goes on from its start; a one-shot ends in silence.
    check(axmix::voice_sample(pcm.data(), 10, 4, true, 12, 0x10000) == pcm[4 + (13 - 4 - 4) % 6] &&
              axmix::voice_sample(pcm.data(), 10, 4, false, 20, 0x10000) == 0,
          "Loop and end");

    // The compressor table: the DOL's 804C0820 values (FNV-1a of the 3360 words).
    const auto& table = axmix::Compressor::table();
    std::uint32_t hash = 2166136261u;
    for (const auto& ramp : table)
        for (const auto value : ramp) hash = (hash ^ value) * 16777619u;
    check(hash == 0xcffc9504u && table[0][0] == 32673 && table[0][159] == 20675 && table[10][0] == 20675 && table[11][0] == 31302 &&
              table[11][159] == 32768 && table[20][0] == 20681 && table[20][159] == 21649,
          "Compressor ramps as in the DOL");
    axmix::Compressor compressor;
    std::array<float, 320> frame{};
    frame.fill(1000.0f);
    compressor.process(frame.data());
    check(frame[0] == 1000.0f && frame[319] == 1000.0f && compressor.position() == 0, "No compression below the threshold");
    frame[100] = 32768.0f; // not above it
    compressor.process(frame.data());
    check(compressor.position() == 0 && frame[100] == 32768.0f, "The threshold itself does not trigger");
    frame[100] = 40000.0f;
    compressor.process(frame.data());
    check(compressor.position() == 10 && std::fabs(frame[319] - 1000.0f * 20675.0f / 32768.0f) < 0.01f &&
              std::fabs(frame[0] - 1000.0f * 32673.0f / 32768.0f) < 0.01f,
          "Attack to -4 dB within the frame");
    for (int f = 0; f < 10; ++f) {
        frame.fill(1000.0f);
        compressor.process(frame.data());
        check(compressor.position() == 9 - f &&
                  std::fabs(frame[319] - 1000.0f * float(table[std::size_t(11 + 9 - f)][159]) / 32768.0f) < 0.01f,
              "Release 0.4 dB per frame");
    }
    frame.fill(1000.0f);
    compressor.process(frame.data());
    check(frame[0] == 1000.0f && frame[319] == 1000.0f, "Released");
    // A trigger during the release attacks from the release's level.
    frame.fill(1000.0f);
    frame[5] = -50000.0f;
    compressor.process(frame.data());
    for (int f = 0; f < 3; ++f) {
        frame.fill(1000.0f);
        compressor.process(frame.data());
    }
    frame.fill(1000.0f);
    frame[7] = 33000.0f;
    compressor.process(frame.data());
    check(compressor.position() == 10 && std::fabs(frame[0] - 1000.0f * float(table[7][0]) / 32768.0f) < 0.01f,
          "Attack ramp of the release position");

    // The resampler: unity gain at DC in every phase.
    const auto& filter = axmix::Output::resampler();
    bool dc = true;
    for (int p = 0; p < axmix::Resampler::kPhases; ++p) {
        double sum = 0.0;
        for (int t = 0; t < 2 * axmix::Resampler::kHalfTaps; ++t) sum += filter.tap(p, t);
        dc = dc && std::fabs(sum - 1.0) < 1e-5;
    }
    check(dc, "Unity gain at DC");
}

// The output timeline: a DSP signal that depends only on the absolute DSP
// sample, rendered in different splits, gives the same output samples, every
// frame mixed once and in order, and the output follows the DSP at 48 kHz.
void ax_output() {
    constexpr double kTwoPi = 6.283185307179586;
    auto run = [&](const std::vector<std::size_t>& chunks, std::vector<std::int64_t>& frames) {
        axmix::Output output;
        output.start(8108100, 1099812, 0);
        std::vector<std::int16_t> out;
        for (const auto n : chunks) {
            std::vector<std::int16_t> part(n * 2);
            output.render(part.data(), n, [&](std::int64_t frame, axmix::Output::Frame& samples, axmix::Output::Part*) {
                frames.push_back(frame);
                for (int j = 0; j < axmix::kFrameSamples; ++j) {
                    const double k = double(frame * axmix::kFrameSamples + j);
                    samples[std::size_t(j)] = {std::int16_t(std::lround(12000.0 * std::sin(kTwoPi * 1000.0 * k / axmix::kDspRate))),
                                               std::int16_t(j * 37 - 3000)};
                }
            });
            out.insert(out.end(), part.begin(), part.end());
        }
        return out;
    };
    std::vector<std::int64_t> frames_a, frames_b;
    const auto a = run({48000}, frames_a);
    std::vector<std::size_t> uneven;
    for (std::size_t total = 0, i = 0; total < 48000; ++i) {
        const std::size_t n = std::min<std::size_t>(48000 - total, i % 7 == 3 ? 2400 : i % 5 == 1 ? 1 : 800 + i % 3);
        uneven.push_back(n);
        total += n;
    }
    const auto b = run(uneven, frames_b);
    check(a == b, "Output independent of the call sizes");
    bool consecutive = frames_b.size() == frames_a.size();
    for (std::size_t i = 1; i < frames_b.size(); ++i) consecutive = consecutive && frames_b[i] == frames_b[i - 1] + 1;
    check(consecutive, "Every frame mixed once, in order");
    // Output sample s plays DSP time s * 10125 + one field - the delay (ticks): the tone at that phase.
    double worst = 0.0;
    for (std::size_t s = 2000; s < 46000; s += 97) {
        const double tick = double(s) * 10125.0 + 8108100.0 - double(axmix::Output::kDelay) - 1099812.0;
        const double expect = 12000.0 * std::sin(kTwoPi * 1000.0 * (tick / 15174.0) / axmix::kDspRate);
        worst = std::max(worst, std::fabs(double(a[2 * s]) - expect));
    }
    check(worst < 25.0, "Resampled tone on time and level");
}

// The audio side of the SNDF under a host's top-ups and hitches: the game's
// driver ticks per field, the audio's mirror per mixed frame (its notes sound
// a frame after their callback, as on the DSP), and the mapping moves by whole
// frames when the audio runs half a field off the game's clock, as
// SoundManager does. Every voice is mixed from its first sample on in
// consecutive frames, exactly once, also when the audio ran ahead.
void audio_timeline() {
    auto data = std::make_shared<sndf::BankData>();
    data->bank = std::make_unique<sfp::Bank>(bank());
    for (std::size_t i = 0; i < data->bank->sample_count(); ++i) {
        std::uint32_t loop = 0;
        data->pcm.push_back(data->bank->decode(i, &loop));
        data->loop_start.push_back(loop);
    }
    data->pcm[0].assign(3000, 0); // the detuned one-shot of request 1: 3000 samples (about 26 AX frames)
    for (std::size_t i = 0; i < 3000; ++i) data->pcm[0][i] = std::int16_t(int(i) - 1500);
    const std::shared_ptr<const sndf::BankData> shared = data;
    const sndf::Driver::Banks banks{shared};
    constexpr std::int64_t kField = 8108100, kFrame = 2427840, kPhase = 1099812, kCommand = 291600;
    auto frame_time = [](std::int64_t g) { return kPhase + g * kFrame; };
    struct Result {
        std::vector<std::int64_t> starts; // audio frame of each voice's first sample
        bool contiguous = true;
        std::uint32_t game_random{}, audio_random{};
    };
    auto simulate = [&](bool stressed) {
        Result result;
        sndf::Driver game;
        std::uint32_t random = 1;
        std::int64_t game_frame = -1;
        sndf::Mirror mirror;
        axmix::Output output;
        std::int64_t delta = 0;
        std::map<std::uint32_t, std::int64_t> next_k; // voice id -> the sample index its next frame must start at
        auto mix = [&](std::int64_t frame, axmix::Output::Frame&, axmix::Output::Part*) {
            if (!mirror.live()) mirror.start(game, random, game_frame + delta);
            if (mirror.ticked() < frame - 1) mirror.advance(frame - 1, delta, banks, frame_time);
            for (const auto& v : mirror.driver().voices()) {
                if (!v.used) continue;
                const std::int64_t first = (frame - (v.start + 1)) * axmix::kFrameSamples;
                if (first < 0) continue;
                const auto it = next_k.find(v.id);
                if (it == next_k.end()) {
                    result.contiguous = result.contiguous && first == 0;
                    result.starts.push_back(frame);
                } else {
                    result.contiguous = result.contiguous && first == it->second;
                }
                next_k[v.id] = first + axmix::kFrameSamples;
            }
        };
        std::uint64_t rendered_steps = 0;
        std::int64_t queued_steps = 0;
        int seq = 4;
        auto call = [&](sndf::Mirror::Call c) {
            c.after = game_frame;
            mirror.record(std::move(c));
        };
        for (int step = 1; step <= 600; ++step) {
            const std::int64_t now = std::int64_t(step) * kField;
            // The game's driver: the frames that began before now, then the
            // update's requests (every 40 steps, alternating two sequences)
            // and stops.
            const std::int64_t last = (now - kPhase - 1) / kFrame;
            while (game_frame < last) {
                ++game_frame;
                game.tick(game_frame, frame_time(game_frame), banks, random);
            }
            if (output.started()) {
                const std::int64_t lead = std::int64_t(output.sample()) * 10125 + kField - delta * kFrame - now;
                if (std::llabs(lead) > kField / 2) delta += (lead >= 0 ? lead + kFrame / 2 : lead - kFrame / 2) / kFrame;
            }
            if (step % 40 == 10) {
                call({sndf::Mirror::Call::Kind::set, 0, seq, 0, 0xa9050100u, now + kCommand});
                game.seq_set(seq, 0xa9050100u, now + kCommand);
                call({sndf::Mirror::Call::Kind::play, 0, seq, 0, 0, now + kCommand});
                game.seq_play(seq, now + kCommand);
                seq = seq == 4 ? 5 : 4;
            }
            if (step % 40 == 30)
                for (int s : {4, 5}) {
                    call({sndf::Mirror::Call::Kind::stop, 0, s, 0, 0, now + kCommand});
                    game.seq_stop(s, now + kCommand);
                }
            // The host: one field of samples per step; stressed, a hitch now
            // and then (steps that come in a burst and are rendered together)
            // and top-ups of 1600..2400 samples.
            ++queued_steps;
            if (stressed && step % 23 > 20) continue;
            if (!output.started()) output.start(kField, kPhase, 0);
            for (; queued_steps > 0; --queued_steps) {
                ++rendered_steps;
                const auto upto = [](std::uint64_t f) { return f * 48000 * 1001 / 60000; };
                const std::size_t n = std::size_t(upto(rendered_steps) - upto(rendered_steps - 1));
                std::vector<std::int16_t> out(n * 2);
                output.render(out.data(), n, mix);
            }
            if (stressed && step % 17 == 5) {
                std::vector<std::int16_t> out(2400 * 2);
                output.render(out.data(), 1600 + std::size_t(step % 5) * 200, mix);
            }
        }
        result.game_random = random;
        result.audio_random = mirror.random();
        return result;
    };
    const auto clean = simulate(false);
    const auto stressed = simulate(true);
    check(clean.contiguous && stressed.contiguous, "Voices mixed from their first sample on, frame after frame, never again");
    check(clean.starts.size() == stressed.starts.size() && clean.starts.size() >= 25, "Every note sounds once");
    check(clean.audio_random == clean.game_random && stressed.audio_random == stressed.game_random,
          "The mirror draws the same rand() values");
    bool later = true, moved = false;
    for (std::size_t i = 0; i < clean.starts.size() && i < stressed.starts.size(); ++i) {
        later = later && stressed.starts[i] >= clean.starts[i];
        moved = moved || stressed.starts[i] > clean.starts[i];
    }
    check(later && moved, "A top-up delays later notes, never plays them early");
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
        pitch();
        driver();
        ax_mix();
        ax_output();
        audio_timeline();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " SFP checks passed\n";
}
