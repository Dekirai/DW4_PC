#include "dw4/axmix.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dw4::axmix {

std::int16_t voice_sample(const std::int16_t* pcm, std::size_t length, std::size_t loop, bool loops, std::uint64_t k,
                          std::uint32_t ratio) {
    const std::uint64_t pos = (k + 1) * std::uint64_t(ratio);
    const std::uint64_t n = pos >> 16;
    const std::uint32_t f = std::uint32_t(pos & 0xFFFFu);
    const bool repeat = loops && loop < length;
    auto input = [&](std::uint64_t entered, std::uint64_t back) -> std::int64_t {
        if (entered < back) return 0; // the zero history before the start
        std::uint64_t i = entered - back;
        if (i >= length) {
            if (!repeat) return 0;
            i = loop + (i - loop) % (length - loop);
        }
        return pcm[i];
    };
    const std::int64_t s0 = input(n, 4);
    if (!f) return std::int16_t(s0);
    const std::int64_t s1 = input(n, 3);
    return std::int16_t((s0 * (0x10000 - std::int64_t(f)) + s1 * std::int64_t(f)) >> 16);
}

const std::array<Compressor::Ramp, Compressor::kRamps>& Compressor::table() {
    static const std::array<Ramp, kRamps> ramps = [] {
        std::array<Ramp, kRamps> out{};
        for (int r = 0; r < kRamps; ++r) {
            const double from = r < kAttackRamps ? -0.4 * r : -0.4 * (r - kAttackRamps + 1);
            const double to = r < kAttackRamps ? -4.0 : -0.4 * (r - kAttackRamps);
            for (int j = 0; j < kFrameSamples; ++j) {
                const double db = from + (to - from) * double(j + 1) / double(kFrameSamples);
                out[std::size_t(r)][std::size_t(j)] = std::uint16_t(std::floor(32768.0 * std::pow(10.0, db / 20.0)));
            }
        }
        return out;
    }();
    return ramps;
}

void Compressor::process(float* main) {
    bool triggered = false;
    for (int i = 0; i < 2 * kFrameSamples && !triggered; ++i) triggered = std::fabs(main[i]) > float(kThreshold);
    int ramp = 0;
    if (triggered) {
        ramp = position_;
        position_ = kReleaseFrames;
    } else if (position_) {
        --position_;
        ramp = kAttackRamps + position_;
    } else {
        return;
    }
    const auto& gains = table()[std::size_t(ramp)];
    for (int i = 0; i < kFrameSamples; ++i) {
        const float g = float(gains[std::size_t(i)]) / 32768.0f;
        main[2 * i] *= g;
        main[2 * i + 1] *= g;
    }
}

namespace {
// The modified Bessel function I0 (its power series).
double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}
} // namespace

Resampler::Resampler() : taps_(std::size_t(kPhases) * (2 * kHalfTaps)) {
    constexpr double kCutoff = 15270.0 / kDspRate; // cycles per DSP sample
    constexpr double kBeta = 6.755;
    const double i0_beta = bessel_i0(kBeta);
    for (int p = 0; p < kPhases; ++p) {
        double sum = 0.0;
        float* row = taps_.data() + std::size_t(p) * (2 * kHalfTaps);
        std::array<double, 2 * kHalfTaps> w{};
        for (int t = 0; t < 2 * kHalfTaps; ++t) {
            const double u = double(t - kHalfTaps + 1) - double(p) / double(kPhases);
            const double r = u / double(kHalfTaps);
            const double window = std::fabs(r) >= 1.0 ? 0.0 : bessel_i0(kBeta * std::sqrt(1.0 - r * r)) / i0_beta;
            const double x = 2.0 * kCutoff * u;
            const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            w[std::size_t(t)] = 2.0 * kCutoff * sinc * window;
            sum += w[std::size_t(t)];
        }
        // Unity gain at DC for every phase.
        for (int t = 0; t < 2 * kHalfTaps; ++t) row[t] = float(w[std::size_t(t)] / sum);
    }
}

// ---------------------------------------------------------------------------
namespace {
std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
} // namespace

void Output::start(std::int64_t origin, std::int64_t ax_phase, std::uint64_t sample) {
    started_ = true;
    origin_ = origin;
    ax_phase_ = ax_phase;
    sample_ = sample;
    std::int64_t base = 0;
    int phase = 0;
    position(std::int64_t(sample), base, phase);
    next_frame_ = floor_div(base - Resampler::kHalfTaps + 1, kFrameSamples);
    first_ = next_frame_ * kFrameSamples;
    dsp_.clear();
    part_.clear();
}

void Output::position(std::int64_t s, std::int64_t& base, int& phase) const {
    // (tick - ax_phase) / kDspSampleTicks DSP samples, in 562ths: the ticks / 27
    // (15174 = 27 * 562), to the nearest.
    const std::int64_t num = s * kSampleTicks + origin_ - kDelay - ax_phase_;
    const std::int64_t x = floor_div(2 * num + 27, 54);
    base = floor_div(x, Resampler::kPhases);
    phase = int(x - base * Resampler::kPhases);
}

std::int16_t Output::saturate(float value) {
    return std::int16_t(std::clamp(std::lrint(value), -32768L, 32767L));
}

const Resampler& Output::resampler() {
    static const Resampler filter;
    return filter;
}

} // namespace dw4::axmix
