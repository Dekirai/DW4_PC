#pragma once

#include "dw4/meshes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace dw4 {

class EnbayaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct EnbayaTransform {
    std::array<float, 4> rotation{0, 0, 0, 1}; // Quaternion XYZW.
    Vec3 translation{};
};

// Owns decoded samples; compressed input need not outlive this value. Only the
// checked decoder can populate frames, so sampling cannot follow byte pointers.
class EnbayaClip {
public:
    std::uint32_t track_count() const noexcept { return tracks_; }
    std::uint32_t sample_rate() const noexcept { return rate_; }
    float duration() const noexcept { return duration_; }
    std::size_t frame_count() const noexcept { return frame_times_.size(); }

private:
    std::uint32_t tracks_{};
    std::uint32_t rate_{};
    float duration_{};
    std::vector<float> frame_times_;
    std::vector<EnbayaTransform> samples_; // Frame-major, then track ordinal.
    std::vector<EnbayaTransform> sample(float time_seconds, bool original_interpolation) const;
    friend EnbayaClip decode_enbaya(std::span<const std::uint8_t>);
    friend std::vector<EnbayaTransform> sample_enbaya(const EnbayaClip&, float);
    friend std::vector<EnbayaTransform> sample_enbaya_original(const EnbayaClip&, float);
};

// Verified little-endian signature 0x002ABF84 profile. Limits: 16 MiB input,
// 1,024 tracks, 600 seconds, 600 samples/second, 1,048,576 decoded transforms.
// Malformed or unsupported data throws EnbayaError before producing a clip.
EnbayaClip decode_enbaya(std::span<const std::uint8_t> bytes);

// Finite time is clamped to [0, duration]; looping belongs to the caller.
// Translation uses linear interpolation. Quaternion interpolation preserves
// the reference codec's SLERP, including its unnormalized near-angle LERP.
// The final partial sample uses the original full sample-period denominator.
std::vector<EnbayaTransform> sample_enbaya(const EnbayaClip& clip, float time_seconds);

// The same checked frame sequence and timing, using the recovered original USA
// engine's quaternion interpolation. No extra endpoint/output normalization is
// applied. This is the runtime path; sample_enbaya is the reference comparison.
std::vector<EnbayaTransform> sample_enbaya_original(const EnbayaClip& clip, float time_seconds);

} // namespace dw4
