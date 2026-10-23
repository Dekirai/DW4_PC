#include "dw4/axfx.hpp"

#include <cmath>
#include <limits>

namespace dw4::axfx {
namespace {

// The PowerPC single-precision operations as Dolphin evaluates them: the
// product of two singles is exact in double, the sum is rounded to double
// and the result to single.
float madd(float a, float c, float b) { return float(double(a) * double(c) + double(b)); }  // fmadds a*c+b
float nmsub(float a, float c, float b) { return float(double(b) - double(a) * double(c)); } // fnmsubs -(a*c-b)
// fctiwz: truncation towards zero, saturated to the int32 range.
std::int32_t to_int(float value) {
    if (std::isnan(value)) return std::numeric_limits<std::int32_t>::min();
    if (value >= 2147483648.0f) return std::numeric_limits<std::int32_t>::max();
    if (value < -2147483648.0f) return std::numeric_limits<std::int32_t>::min();
    return std::int32_t(value);
}

// 804C41D0: the comb and all-pass lengths (samples at 32 kHz), the same for
// every channel.
constexpr int kCombLength[2] = {1789, 1999};
constexpr int kAllPassLength[2] = {433, 149};

} // namespace

void ReverbStd::DelayLine::create(int delay) {
    // DLcreate(delay + 2) leaves both points at 0; DLsetdelay moves the read
    // point `delay` samples behind the write point, wrapped into the line.
    length = std::size_t(delay) + 2;
    inputs.assign(length, 0.0f);
    last = 0.0f;
    in = 0;
    out = length - std::size_t(delay);
}

bool ReverbStd::init(float coloration, float time, float mix, float damping, float pre_delay) {
    // 8024319C clears tempDisableFX and creates under disabled interrupts.
    shutdown();
    // 80242848: the range checks (fcmpo; a NaN passes all but the last).
    if (coloration < 0.0f || coloration > 1.0f || time < 0.01f || time > 10.0f || mix < 0.0f || mix > 1.0f ||
        damping < 0.0f || damping > 1.0f || pre_delay < 0.0f || !(pre_delay <= 0.1f))
        return false;
    const float span = 32000.0f * time;
    for (std::size_t k = 0; k < 3; ++k) {
        for (std::size_t i = 0; i < 2; ++i) {
            comb_[k * 2 + i].create(kCombLength[i]);
            // powf (800A2F58): the double pow rounded to single.
            const float exponent = float(kCombLength[i] * -3) / span;
            comb_coef_[k * 2 + i] = float(std::pow(10.0, double(exponent)));
        }
        for (std::size_t i = 0; i < 2; ++i) all_pass_[k * 2 + i].create(kAllPassLength[i]);
        lp_last_[k] = 0.0f;
    }
    all_pass_coeff_ = coloration;
    level_ = mix;
    // 80242BA8..80242BB8: fcmpo + bge keeps only a damping of at least
    // 0.05; anything else, a NaN too, becomes 0.05.
    damping_ = !(damping >= 0.05f) ? 0.05f : damping;
    damping_ = 1.0f - (0.05f + 0.8f * damping_);
    if (pre_delay != 0.0f) {
        pre_delay_time_ = to_int(32000.0f * pre_delay);
        for (std::size_t k = 0; k < 3; ++k) {
            pre_delay_line_[k].assign(std::size_t(pre_delay_time_), 0.0f);
            pre_delay_pos_[k] = 0;
        }
    } else {
        pre_delay_time_ = 0;
    }
    active_ = true;
    return true;
}

void ReverbStd::shutdown() {
    active_ = false;
    for (auto& line : all_pass_) line = {};
    for (auto& line : comb_) line = {};
    for (auto& line : pre_delay_line_) line.clear();
    pre_delay_time_ = 0;
}

void ReverbStd::process(AuxFrame& data) {
    if (!active_) return;
    // 80242CC4. Per channel and sample: the input goes through the pre-delay
    // (a ring that wraps when its pointer reaches the last element, so it
    // delays by pre_delay_time - 1 samples), two parallel combs, an all-pass,
    // the low-pass and a second all-pass; the output is
    // 0.6 * (mix * wet + (1 - mix) * dry) of the undelayed input. The combs
    // use the sample they read in the same step (1789 / 1999 samples), the
    // all-passes keep theirs for the next step (434 / 150 samples).
    const float wet = level_ * 0.6f;
    const float dry = 0.6f - wet;
    const float coeff = all_pass_coeff_;
    for (std::size_t k = 0; k < 3; ++k) {
        auto& c0 = comb_[k * 2];
        auto& c1 = comb_[k * 2 + 1];
        auto& a0 = all_pass_[k * 2];
        auto& a1 = all_pass_[k * 2 + 1];
        const float coef0 = comb_coef_[k * 2], coef1 = comb_coef_[k * 2 + 1];
        float lp = lp_last_[k];
        auto& line = pre_delay_line_[k];
        auto& pos = pre_delay_pos_[k];
        const std::size_t wrap = pre_delay_time_ > 1 ? std::size_t(pre_delay_time_ - 1) : 1;
        std::int32_t* samples = data.data() + k * kFrameSamples;
        for (std::size_t i = 0; i < kFrameSamples; ++i) {
            const float x = float(samples[i]);
            float in = x;
            if (pre_delay_time_ != 0) {
                in = line[pos];
                line[pos] = x;
                if (++pos == wrap) pos = 0;
            }
            // The combs: write, then read the sample `delay` behind.
            const float f0 = madd(coef0, c0.last, in);
            const float f1 = madd(coef1, c1.last, in);
            c0.inputs[c0.in] = f0;
            c1.inputs[c1.in] = f1;
            c0.last = c0.inputs[c0.out];
            c1.last = c1.inputs[c1.out];
            if (++c0.in == c0.length) c0.in = 0;
            if (++c0.out == c0.length) c0.out = 0;
            if (++c1.in == c1.length) c1.in = 0;
            if (++c1.out == c1.length) c1.out = 0;
            float value = c0.last + c1.last;
            // All-pass 1.
            float w = madd(coeff, a0.last, value);
            a0.inputs[a0.in] = w;
            value = nmsub(coeff, w, a0.last);
            a0.last = a0.inputs[a0.out];
            if (++a0.in == a0.length) a0.in = 0;
            if (++a0.out == a0.length) a0.out = 0;
            // The low-pass.
            value = madd(damping_, lp, value * 0.3f);
            lp = value;
            // All-pass 2.
            w = madd(coeff, a1.last, value);
            a1.inputs[a1.in] = w;
            value = nmsub(coeff, w, a1.last);
            a1.last = a1.inputs[a1.out];
            if (++a1.in == a1.length) a1.in = 0;
            if (++a1.out == a1.length) a1.out = 0;
            samples[i] = to_int(madd(wet, value, dry * x));
        }
        lp_last_[k] = lp;
    }
}

const AuxFrame* AuxBus::begin_frame(ReverbStd* callback) {
    // The command list of this frame (built before __AXProcessAux) holds the
    // AUX command with the write and read buffers only while a callback is
    // registered.
    const int read = read_;
    frame_write_ = callback ? write_ : -1;
    if (callback) callback->process(buffers_[std::size_t(cpu_)]);
    write_ = (write_ + 1) % 3;
    read_ = (read_ + 1) % 3;
    cpu_ = (cpu_ + 1) % 3;
    return callback ? &buffers_[std::size_t(read)] : nullptr;
}

void AuxBus::end_frame(const std::array<std::int32_t, 2 * kFrameSamples>& sends) {
    if (frame_write_ < 0) return;
    auto& buffer = buffers_[std::size_t(frame_write_)];
    for (std::size_t i = 0; i < kFrameSamples; ++i) {
        buffer[i] = sends[i * 2];
        buffer[kFrameSamples + i] = sends[i * 2 + 1];
        buffer[2 * kFrameSamples + i] = 0;
    }
    frame_write_ = -1;
}

void AuxBus::reset() {
    buffers_ = {};
    write_ = 0;
    read_ = 1;
    cpu_ = 2;
    frame_write_ = -1;
}

} // namespace dw4::axfx
