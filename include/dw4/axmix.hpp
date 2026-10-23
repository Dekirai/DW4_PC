#pragma once

// The AX DSP mix as DW4's AX library drives it, and the step from the DSP's
// output to the host's audio device.
//
// AIInit (8023C0D4) sets the DSP sample rate to 32 kHz (AISetDSPSampleRate(0)
// 8023BEA4: AICR bit 0x40, the only call), which the GameCube's AI clocks at
// 54 MHz / 1686 = 32028.5 Hz; AX renders 160 samples per frame (5 ms) at that
// rate. Each frame the AX command list (__AXNextFrame 8023D0D8) has the DSP
// process the voices (each through its sample-rate converter, volume and mixer
// levels into the main and AUX buses), mix the AUX returns into the main bus,
// run the compressor (command 0x12, on from __AXClInit 8023D788) and output
// the main bus saturated to 16 bits. The arithmetic follows Dolphin's AX ucode
// emulation (DSPHLE AX), which the reference captures ran.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace dw4::axmix {

// One AX frame at the DSP rate.
inline constexpr int kFrameSamples = 160;
inline constexpr double kDspRate = 54.0e6 / 1686.0;

// AX sample-rate conversion type 1, linear (every SFP split's +0x2F, 803BAA94 /
// 803BB7F4, and the RNA's stream voices, 804CD6C8 = 1 through 802A6D4C /
// 802A7EF8): per output sample the 16.16 position advances by the ratio, the
// input samples it passes enter a four-sample history (the PB's last_samples,
// zero for a voice set up fresh), and the output interpolates the two oldest
// history samples at the position's fraction, (s0 (65536 - f) + s1 f) >> 16
// (s0 alone at fraction 0). A voice's output so lags its input by about four
// input samples.
struct LinearSrc {
    std::uint32_t ratio{};                              // 16.16 (AXPBSRC ratio)
    std::uint32_t position{};                           // the fraction (cur_addr_frac)
    std::array<std::array<std::int16_t, 2>, 4> history{}; // last_samples, two channels
    unsigned write{};                                   // the next history slot
    // A fresh voice: zero history and fraction.
    void reset(std::uint32_t new_ratio) {
        *this = LinearSrc{};
        ratio = new_ratio;
    }
    // One output sample (both channels); fetch(frame) supplies the next input
    // frame and returns false once there is none (the input reads as zeros).
    template <class Fetch> std::array<std::int16_t, 2> next(Fetch&& fetch) {
        std::uint64_t pos = std::uint64_t(position) + ratio;
        while (pos >= 0x10000u) {
            std::array<std::int16_t, 2> frame{};
            if (!fetch(frame)) frame = {};
            history[write & 3u] = frame;
            ++write;
            pos -= 0x10000u;
        }
        position = std::uint32_t(pos);
        const auto& s0 = history[write & 3u];
        const auto& s1 = history[(write + 1) & 3u];
        if (!position) return s0;
        const std::int64_t f = position, inv = 0x10000 - f;
        return {std::int16_t((s0[0] * inv + s1[0] * f) >> 16), std::int16_t((s0[1] * inv + s1[1] * f) >> 16)};
    }
};

// The k-th output sample (k >= 0 from the voice's first frame) of a voice
// playing `pcm` (`length` samples; from `loop` on again when `loops`, else
// silence after the end) through that SRC at the 16.16 `ratio` from a fresh
// start: (k + 1) * ratio has passed n input samples, and the output
// interpolates inputs n - 4 and n - 3 (zeros before the start).
std::int16_t voice_sample(const std::int16_t* pcm, std::size_t length, std::size_t loop, bool loops, std::uint64_t k,
                          std::uint32_t ratio);

// The AX ucode's compressor (command 0x12 with the threshold 0x8000, ten
// release frames and the ramp table at 804C0820, which __AXNextFrame 8023D0D8
// sends every frame while r13 +0xD04 is set: __AXClInit 8023D788 sets it and
// nothing in the DOL clears it; DSPHLE AX RunCompressor). A frame whose main
// left or right bus (after the AUX returns) exceeds the threshold anywhere
// (|s| > 32768) is scaled by the attack ramp of the current release position
// (down to -4 dB across the frame) and the position becomes 10; each later
// frame below the threshold steps the position down and is scaled by its
// release ramp (0.4 dB per frame back up to 0 dB).
class Compressor {
public:
    static constexpr int kThreshold = 0x8000, kReleaseFrames = 10, kAttackRamps = 11, kRamps = 21;
    using Ramp = std::array<std::uint16_t, kFrameSamples>;
    // 804C0820: ramp r < 11 runs from -0.4 r dB to -4 dB, ramp 11 + q from
    // -0.4 (q + 1) dB to -0.4 q dB, linear in dB per sample, sample j at
    // (j + 1) / 160 of the way, as floor(32768 * 10^(dB / 20)) (the DOL's 3360
    // values exactly).
    static const std::array<Ramp, kRamps>& table();
    // The frame's main bus (left / right interleaved, 160 frames) in place.
    void process(float* main);
    int position() const { return position_; }

private:
    int position_{};
};

// The host's output: the DSP's 32028.5 Hz samples resampled to 48000 Hz
// (48000 / 32028.5 = 562 / 375 exactly) through a Kaiser-windowed sinc
// low-pass of 2 * kHalfTaps taps (beta 6.76, about 70 dB; cut off at 15.27
// kHz, so the band up to about 14.5 kHz passes and the DSP's images, from 16.0
// kHz on, are suppressed), kept as 562 phases.
class Resampler {
public:
    static constexpr int kHalfTaps = 48, kPhases = 562;
    // One output frame at DSP position base + phase / 562 from the DSP samples
    // base - kHalfTaps + 1 .. base + kHalfTaps (`at(i)` gives sample i).
    template <class At> std::array<float, 2> sample(std::int64_t base, int phase, At&& at) const {
        const float* w = taps_.data() + std::size_t(phase) * (2 * kHalfTaps);
        float left = 0.0f, right = 0.0f;
        for (int t = 0; t < 2 * kHalfTaps; ++t) {
            const auto& s = at(base - kHalfTaps + 1 + t);
            left += w[t] * float(s[0]);
            right += w[t] * float(s[1]);
        }
        return {left, right};
    }
    Resampler();
    // Tap t of phase p (the filter at offset t - kHalfTaps + 1 - p / 562).
    float tap(int phase, int t) const { return taps_[std::size_t(phase) * (2 * kHalfTaps) + std::size_t(t)]; }

private:
    std::vector<float> taps_;
};

// The output side of the audio timeline: the DSP's AX frames, each mixed once
// and in order when the output first reaches it, resampled to the output rate.
// Output sample s plays the DSP at CPU tick s * kSampleTicks + origin - kDelay
// (kDelay: the resampler's half length); DSP sample k (of frame k / 160)
// stands at tick ax_phase + k * kDspSampleTicks. However the output is split
// into calls, the samples are the same and every frame is mixed exactly once.
class Output {
public:
    static constexpr std::int64_t kSampleTicks = 10125;    // 486 MHz / 48 kHz
    static constexpr std::int64_t kDspSampleTicks = 15174; // 486 MHz / 32028.5 Hz (27 * 562)
    static constexpr std::int64_t kDelay = Resampler::kHalfTaps * kDspSampleTicks;
    using Frame = std::array<std::array<std::int16_t, 2>, kFrameSamples>;
    using Part = std::array<std::array<float, 2>, kFrameSamples>;
    // The output continues at sample `sample`, whose tick before the delay is
    // sample * kSampleTicks + origin; the first frame mixed is the first one
    // that sample reaches.
    void start(std::int64_t origin, std::int64_t ax_phase, std::uint64_t sample);
    bool started() const { return started_; }
    std::uint64_t sample() const { return sample_; }
    std::int64_t next_frame() const { return next_frame_; }
    // The output sample (fractional) at which DSP frame `frame` begins.
    double frame_sample(std::int64_t frame) const {
        return double(ax_phase_ + frame * kFrameSamples * kDspSampleTicks + kDelay - origin_) / double(kSampleTicks);
    }
    // `count` stereo samples into `out`; mix(frame, Frame& samples, Part* part)
    // mixes DSP frame `frame` (part: a component of the mix to resample
    // alongside into `part_out`, a test tool; null without).
    template <class Mix> void render(std::int16_t* out, std::size_t count, Mix&& mix, std::vector<std::int16_t>* part_out = nullptr) {
        const Resampler& filter = resampler();
        constexpr int kHalf = Resampler::kHalfTaps;
        for (std::size_t i = 0; i < count; ++i) {
            std::int64_t base = 0;
            int phase = 0;
            position(std::int64_t(sample_), base, phase);
            while (first_ + std::int64_t(dsp_.size()) <= base + kHalf) {
                Frame frame{};
                Part part{};
                mix(next_frame_, frame, part_out ? &part : nullptr);
                ++next_frame_;
                for (int j = 0; j < kFrameSamples; ++j) {
                    dsp_.push_back(frame[std::size_t(j)]);
                    if (part_out) part_.push_back(part[std::size_t(j)]);
                }
            }
            while (first_ < base - kHalf + 1 && !dsp_.empty()) {
                dsp_.pop_front();
                if (!part_.empty()) part_.pop_front();
                ++first_;
            }
            const auto value = filter.sample(base, phase, [&](std::int64_t k) -> const std::array<std::int16_t, 2>& {
                return dsp_[std::size_t(k - first_)];
            });
            out[2 * i] = saturate(value[0]);
            out[2 * i + 1] = saturate(value[1]);
            if (part_out && part_.size() == dsp_.size()) {
                const auto value_part = filter.sample(base, phase, [&](std::int64_t k) -> const std::array<float, 2>& {
                    return part_[std::size_t(k - first_)];
                });
                part_out->push_back(saturate(value_part[0]));
                part_out->push_back(saturate(value_part[1]));
            }
            ++sample_;
        }
    }
    // DSP position of output sample s: the DSP sample `base` and 562ths.
    void position(std::int64_t s, std::int64_t& base, int& phase) const;
    static std::int16_t saturate(float value);
    static const Resampler& resampler();

private:
    bool started_{};
    std::int64_t origin_{}, ax_phase_{};
    std::uint64_t sample_{};
    std::int64_t next_frame_{}, first_{};
    std::deque<std::array<std::int16_t, 2>> dsp_;
    std::deque<std::array<float, 2>> part_;
};

} // namespace dw4::axmix
