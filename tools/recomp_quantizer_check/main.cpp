// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.h"
#include <immintrin.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

extern "C" s64 baseline_quantize(f64, s64, s64, s32);
extern "C" s64 current_quantize(f64, s64, s64, s32);
extern "C" u32 baseline_factor(s32);
extern "C" u32 current_factor(s32);

namespace {
constexpr unsigned sticky_mask = 0x3f;
constexpr unsigned exception_masks = 0x1f80;
constexpr unsigned environment_mask = 0x6000 | 0x8000 | 0x40;
struct RestoreCsr {
    unsigned saved = _mm_getcsr();
    ~RestoreCsr() { _mm_setcsr(saved); }
};
struct Bounds { s64 low, high; };
constexpr Bounds bounds[] = {{0,255}, {0,65535}, {-128,127}, {-32768,32767}};

u64 random_bits(u64& state) {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545f4914f6cdd1dULL;
}

std::vector<u64> corpus(s32 scale, Bounds range) {
    std::vector<u64> result;
    // Raw payloads retain signaling NaNs and denormals without arithmetic.
    constexpr u64 raw[] = {
        0x0000000000000000ULL, 0x0000000000000001ULL,
        0x0000000000000002ULL, 0x000fffffffffffffULL,
        0x0010000000000000ULL, 0x0010000000000001ULL,
        0x7fefffffffffffffULL, 0x7ff0000000000000ULL,
        0x7ff8000000000000ULL, 0x7ff8000000000001ULL,
        0x7fffffffffffffffULL, 0x7ff0000000000001ULL,
        0x7ff4000000000000ULL, 0x7ff7ffffffffffffULL
    };
    for (u64 value : raw) {
        result.push_back(value);
        result.push_back(value | 0x8000000000000000ULL);
    }
    const auto add_neighbors = [&](f64 value) {
        result.push_back(f64_bits(value));
        result.push_back(f64_bits(std::nextafter(value, -std::numeric_limits<f64>::infinity())));
        result.push_back(f64_bits(std::nextafter(value, std::numeric_limits<f64>::infinity())));
    };
    // Include both double neighbors and the wider float rounding boundaries.
    constexpr u32 float_edges[] = {1, 0x007fffff, 0x00800000, 0x00800001,
                                  0x3f000000, 0x3f800000, 0x7f7fffff};
    for (u32 bits : float_edges) {
        const f64 value = static_cast<f64>(f32_value(bits));
        add_neighbors(value);
        add_neighbors(-value);
    }
    const f64 inverse_scale = std::ldexp(1.0, -scale);
    for (s64 limit : {range.low, range.high}) {
        const f32 boundary = static_cast<f32>(limit);
        const f32 below = std::nextafter(boundary, -std::numeric_limits<f32>::infinity());
        const f32 above = std::nextafter(boundary, std::numeric_limits<f32>::infinity());
        for (f64 value : {static_cast<f64>(below), static_cast<f64>(boundary),
                          static_cast<f64>(above),
                          (static_cast<f64>(below) + boundary) * 0.5,
                          (static_cast<f64>(above) + boundary) * 0.5}) {
            add_neighbors(value * inverse_scale);
        }
    }
    for (f64 value : {-1.5, -1.0, -0.5, 0.5, 1.0, 1.5})
        add_neighbors(value * inverse_scale);
    u64 state = 0xc89f143a72d8be65ULL ^ static_cast<u64>(scale + 32)
        ^ (static_cast<u64>(range.high) << 16);
    for (unsigned i = 0; i != 96; ++i) result.push_back(random_bits(state));
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: recomp-quantizer-check.exe report.json\n");
        return 2;
    }
    const RestoreCsr restore;
    const unsigned clean = (restore.saved | exception_masks) & ~(sticky_mask | environment_mask);
    _mm_setcsr(clean);
    unsigned long long comparisons = 0, factor_comparisons = 0, failures = 0;
    for (s32 scale = -32; scale <= 31; ++scale) {
        for (Bounds range : bounds) {
            _mm_setcsr(clean);
            const auto inputs = corpus(scale, range);
            for (unsigned rounding = 0; rounding < 4; ++rounding) {
                for (unsigned flushing = 0; flushing < 4; ++flushing) {
                    const unsigned csr = clean | (rounding << 13)
                        | ((flushing & 1) ? 0x8000 : 0) | ((flushing & 2) ? 0x40 : 0);
                    // Check every exact factor once per environment, independent of bounds.
                    if (range.high == 255) {
                        _mm_setcsr(csr);
                        const u32 before = baseline_factor(scale);
                        const unsigned before_flags = _mm_getcsr() & sticky_mask;
                        _mm_setcsr(csr);
                        const u32 after = current_factor(scale);
                        const unsigned after_flags = _mm_getcsr() & sticky_mask;
                        ++factor_comparisons;
                        if (before != after || before_flags != after_flags) {
                            ++failures;
                            if (failures <= 12)
                                std::fprintf(stderr, "Factor mismatch: scale=%d csr=%08x bits=%08x/%08x flags=%02x/%02x\n",
                                    scale, csr, before, after, before_flags, after_flags);
                        }
                    }
                    for (u64 bits : inputs) {
                        const f64 input = f64_value(bits);
                        _mm_setcsr(csr);
                        const s64 before = baseline_quantize(input, range.low, range.high, scale);
                        const unsigned before_flags = _mm_getcsr() & sticky_mask;
                        _mm_setcsr(csr);
                        const s64 after = current_quantize(input, range.low, range.high, scale);
                        const unsigned after_flags = _mm_getcsr() & sticky_mask;
                        ++comparisons;
                        if (before != after || before_flags != after_flags) {
                            ++failures;
                            if (failures <= 12)
                                std::fprintf(stderr, "Mismatch: input=%016llx scale=%d bounds=%lld:%lld csr=%08x result=%lld/%lld flags=%02x/%02x\n",
                                    bits, scale, range.low, range.high, csr, before, after, before_flags, after_flags);
                        }
                    }
                }
            }
        }
    }
    _mm_setcsr(restore.saved);
    FILE* report = nullptr;
    if (fopen_s(&report, argv[1], "wb") || !report) {
        std::fprintf(stderr, "Cannot write report: %s\n", argv[1]);
        return 2;
    }
    std::fprintf(report,
        "{\n  \"passed\": %s,\n  \"quantizer_comparisons\": %llu,\n  \"factor_comparisons\": %llu,\n"
        "  \"scales\": 64,\n  \"integer_bounds\": 4,\n  \"rounding_modes\": 4,\n"
        "  \"ftz_daz_combinations\": 4,\n  \"sticky_flags_compared\": 63,\n"
        "  \"failures\": %llu,\n  \"original_mxcsr\": %u,\n  \"restored_mxcsr\": %u,\n"
        "  \"game_code_executed\": false\n}\n",
        failures ? "false" : "true", comparisons, factor_comparisons, failures,
        restore.saved, _mm_getcsr());
    if (std::fclose(report)) return 2;
    std::printf("%s: %llu quantizer comparisons; %llu factor comparisons; %llu failures; MXCSR restored.\n",
        failures ? "FAIL" : "PASS", comparisons, factor_comparisons, failures);
    return failures ? 1 : 0;
}
