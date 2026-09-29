// Packed decoding and interpolation adapted from korenkonder/enbrip,
// commit 461b7818ec4b1b20ed68b4a7935250f3b4b4ce18 (MIT).
// Copyright (c) 2020-2025 korenkonder. See licenses/enbrip-MIT.txt.
// DW4_PC replaces guest pointers and incremental seek state with bounded
// little-endian cursors and an immutable, fully decoded sample sequence.
#include "dw4/enbaya.hpp"
#include "dw4/animation_math.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>

namespace dw4 {
namespace {
constexpr std::size_t header_size = 80;
constexpr std::size_t max_input = 16 * 1024 * 1024;
constexpr std::size_t max_transforms = 1024 * 1024;
constexpr std::uint32_t max_tracks = 1024;
constexpr std::uint32_t max_rate = 600;
constexpr float max_duration = 600;
enum Stream : std::size_t {
    init_bits, init_i8, init_i16, init_i32,
    delta_bits, delta_nibbles, delta_i8, delta_i16, delta_i32,
    state_bits, state_u8, state_u16, state_u32, flags, stream_count
};
constexpr std::array<std::size_t, stream_count> physical_order{
    init_i32, delta_i32, state_u32, init_i16, delta_i16, state_u16,
    init_bits, init_i8, delta_bits, delta_nibbles, delta_i8,
    state_bits, state_u8, flags
};
constexpr std::array<const char*, stream_count> stream_names{
    "initial selectors", "initial int8", "initial int16", "initial int32",
    "delta selectors", "delta nibbles", "delta int8", "delta int16", "delta int32",
    "state selectors", "state uint8", "state uint16", "state uint32", "track flags"
};

struct Cursor {
    std::span<const std::uint8_t> bytes;
    const char* name{"header"};
    std::size_t position{};
    unsigned remaining_bits{};
    std::uint8_t packed{};

    std::uint32_t unsigned_value(unsigned width) {
        if (position > bytes.size() || width > bytes.size() - position)
            throw EnbayaError(std::string("Truncated Enbaya ") + name + " stream");
        std::uint32_t value{};
        for (unsigned i = 0; i < width; ++i)
            value |= static_cast<std::uint32_t>(bytes[position++]) << (i * 8);
        return value;
    }
    std::int32_t signed_value(unsigned width) {
        const auto value = unsigned_value(width);
        const auto bits = width * 8;
        const std::int64_t signed_number = (value & (std::uint32_t{1} << (bits - 1)))
            ? static_cast<std::int64_t>(value) - (std::int64_t{1} << bits)
            : static_cast<std::int64_t>(value);
        return static_cast<std::int32_t>(signed_number);
    }
    std::uint32_t packed_value(unsigned width) {
        if (!remaining_bits) {
            packed = static_cast<std::uint8_t>(unsigned_value(1));
            remaining_bits = 8;
        }
        remaining_bits -= width;
        return (packed >> remaining_bits) & ((std::uint32_t{1} << width) - 1);
    }
};

using Cursors = std::array<Cursor, stream_count>;
std::int32_t initial_value(Cursors& streams) {
    switch (streams[init_bits].packed_value(2)) {
    case 0: return 0;
    case 1: return streams[init_i8].signed_value(1);
    case 2: return streams[init_i16].signed_value(2);
    default: return streams[init_i32].signed_value(4);
    }
}
std::int32_t delta_value(Cursors& streams) {
    constexpr std::array<std::int32_t, 4> two_bit{0, 1, 0, -1};
    constexpr std::array<std::int32_t, 16> four_bit{
        0, 8, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -9
    };
    const auto selector = streams[delta_bits].packed_value(2);
    if (selector != 2) return two_bit[selector];
    const auto nibble = streams[delta_nibbles].packed_value(4);
    if (nibble) return four_bit[nibble];
    auto value = streams[delta_i8].signed_value(1);
    if (!value) {
        value = streams[delta_i16].signed_value(2);
        if (!value) value = streams[delta_i32].signed_value(4);
    } else if (value > 0 && value < 9) {
        value += 127;
    } else if (value < 0 && value > -9) {
        value -= 128;
    }
    return value;
}
std::uint32_t state_value(Cursors& streams) {
    switch (streams[state_bits].packed_value(2)) {
    case 0: return 0;
    case 1: return streams[state_u8].unsigned_value(1);
    case 2: return streams[state_u16].unsigned_value(2);
    default: return streams[state_u32].unsigned_value(4);
    }
}

float dot(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
}
std::array<float, 4> normalize(std::array<float, 4> q) {
    const auto length = std::sqrt(dot(q, q));
    if (!std::isfinite(length) || length <= 0)
        throw EnbayaError("Enbaya rotation has a zero or non-finite norm");
    const auto reciprocal = 1.0f / length;
    for (auto& component : q) component *= reciprocal;
    return q;
}
void validate(const EnbayaTransform& transform) {
    for (const auto component : transform.rotation)
        if (!std::isfinite(component)) throw EnbayaError("Non-finite Enbaya rotation");
    if (!std::isfinite(transform.translation.x) || !std::isfinite(transform.translation.y)
        || !std::isfinite(transform.translation.z))
        throw EnbayaError("Non-finite Enbaya translation");
}
std::array<float, 4> interpolate_rotation(
    std::array<float, 4> a, std::array<float, 4> b, float blend) {
    a = normalize(a);
    b = normalize(b);
    auto product = dot(a, b);
    if (product < 0) {
        for (auto& component : b) component = -component;
        product = -product;
    }
    std::array<float, 4> result{};
    if (1.0 - static_cast<double>(product) <= 0.08f) {
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = a[i] * (1.0f - blend) + b[i] * blend;
        return result;
    }
    product = std::min(product, 1.0f);
    const auto theta = std::acos(product);
    if (theta == 0) return a;
    const auto reciprocal_sine = 1.0f / std::sin(theta);
    const auto weight_a = std::sin((1.0f - blend) * theta) * reciprocal_sine;
    const auto weight_b = std::sin(theta * blend) * reciprocal_sine;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = weight_a * a[i] + weight_b * b[i];
    return normalize(result);
}
} // namespace

EnbayaClip decode_enbaya(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_size || bytes.size() > max_input)
        throw EnbayaError("Enbaya input size is outside the 80-byte to 16-MiB limits");
    Cursor header{bytes.first(header_size)};
    // The first word is a serialized guest value (observed 0x002ABF84 for the
    // player clips and 0x002ABF8C for NPC clips), not a format signature. The
    // stream layout that follows is identical; keep a coarse sanity range.
    if ((header.unsigned_value(4) & 0xffffff00u) != 0x002abf00u)
        throw EnbayaError("Unsupported Enbaya signature");
    EnbayaClip clip;
    clip.tracks_ = header.unsigned_value(4);
    const auto quantization = std::bit_cast<float>(header.unsigned_value(4));
    clip.duration_ = std::bit_cast<float>(header.unsigned_value(4));
    clip.rate_ = header.unsigned_value(4);
    if (!clip.tracks_ || clip.tracks_ > max_tracks)
        throw EnbayaError("Enbaya track count is outside the 1..1024 limit");
    if (!std::isfinite(quantization) || quantization <= 0 || quantization > 1)
        throw EnbayaError("Enbaya quantization error must be finite and in (0,1]");
    if (!std::isfinite(clip.duration_) || clip.duration_ <= 0 || clip.duration_ > max_duration)
        throw EnbayaError("Enbaya duration must be finite and in (0,600] seconds");
    if (!clip.rate_ || clip.rate_ > max_rate)
        throw EnbayaError("Enbaya sample rate is outside the 1..600 limit");
    std::array<std::uint32_t, stream_count> lengths{};
    for (auto& length : lengths) length = header.unsigned_value(4);
    // Header +76 is an opaque serialized guest value, never a host pointer.
    Cursors streams{};
    std::size_t offset = header_size;
    for (const auto index : physical_order) {
        if (lengths[index] > bytes.size() - offset)
            throw EnbayaError("Enbaya stream lengths exceed the input size");
        streams[index] = {bytes.subspan(offset, lengths[index]), stream_names[index]};
        offset += lengths[index];
    }
    if (offset != bytes.size()) throw EnbayaError("Enbaya input has trailing bytes outside its streams");
    for (const auto index : {init_i16, delta_i16, state_u16})
        if (lengths[index] % 2) throw EnbayaError("Enbaya 16-bit stream has a partial value");
    for (const auto index : {init_i32, delta_i32, state_u32})
        if (lengths[index] % 4) throw EnbayaError("Enbaya 32-bit stream has a partial value");
    if (lengths[flags] != clip.tracks_)
        throw EnbayaError("Enbaya track flag count does not match its tracks");

    const auto period = 1.0f / static_cast<float>(clip.rate_);
    clip.frame_times_.push_back(0);
    // This retains the source codec's epsilon at the end of a quantized clip.
    while (clip.duration_ - clip.frame_times_.back() > 0.00001f) {
        if (clip.frame_times_.size() >= max_transforms / clip.tracks_)
            throw EnbayaError("Enbaya decoded transform count exceeds 1048576");
        const auto frame = static_cast<std::uint32_t>(clip.frame_times_.size());
        clip.frame_times_.push_back(static_cast<float>(frame) * period);
    }
    const auto total = clip.frame_times_.size() * clip.tracks_;
    clip.samples_.reserve(total);
    auto next_toggle = state_value(streams);
    std::vector<std::uint8_t> track_flags(clip.tracks_);
    std::vector<std::array<float, 7>> deltas(clip.tracks_);
    for (std::uint32_t track = 0; track < clip.tracks_; ++track) {
        EnbayaTransform transform;
        for (auto& component : transform.rotation)
            component = static_cast<float>(initial_value(streams)) * quantization;
        transform.translation = {
            static_cast<float>(initial_value(streams)) * quantization,
            static_cast<float>(initial_value(streams)) * quantization,
            static_cast<float>(initial_value(streams)) * quantization
        };
        transform.rotation = normalize(transform.rotation);
        validate(transform);
        clip.samples_.push_back(transform);
        track_flags[track] = static_cast<std::uint8_t>(streams[flags].unsigned_value(1));
        if (track_flags[track] & 0x80) throw EnbayaError("Unsupported Enbaya track flag bit 7");
    }
    for (std::size_t frame = 1; frame < clip.frame_times_.size(); ++frame) {
        if (frame > 1) {
            const auto components = clip.tracks_ * 7;
            std::uint32_t component{};
            while (component < components) {
                if (!next_toggle) {
                    track_flags[component / 7] ^= static_cast<std::uint8_t>(1u << (component % 7));
                    next_toggle = state_value(streams);
                    ++component;
                } else {
                    const auto skipped = std::min(next_toggle, components - component);
                    component += skipped;
                    next_toggle -= skipped;
                }
            }
        }
        for (std::uint32_t track = 0; track < clip.tracks_; ++track) {
            auto& delta = deltas[track];
            for (unsigned component = 0; component < delta.size(); ++component) {
                if (track_flags[track] & (1u << component)) {
                    delta[component] += static_cast<float>(delta_value(streams));
                    if (!std::isfinite(delta[component])) throw EnbayaError("Enbaya delta accumulation overflow");
                }
            }
            auto transform = clip.samples_[(frame - 1) * clip.tracks_ + track];
            for (std::size_t component = 0; component < transform.rotation.size(); ++component)
                transform.rotation[component] += delta[component] * quantization;
            transform.translation.x += delta[4] * quantization;
            transform.translation.y += delta[5] * quantization;
            transform.translation.z += delta[6] * quantization;
            transform.rotation = normalize(transform.rotation);
            validate(transform);
            clip.samples_.push_back(transform);
        }
    }
    return clip;
}

std::vector<EnbayaTransform> EnbayaClip::sample(float time_seconds, bool original_interpolation) const {
    const auto& clip = *this;
    if (!std::isfinite(time_seconds)) throw EnbayaError("Enbaya sample time must be finite");
    if (!clip.tracks_ || clip.frame_times_.empty()) throw EnbayaError("Cannot sample an empty Enbaya clip");
    const auto time = std::clamp(time_seconds, 0.0f, clip.duration_);
    if (time < 0.000001f || clip.frame_times_.size() == 1) {
        std::vector<EnbayaTransform> result(clip.samples_.begin(), clip.samples_.begin() + clip.tracks_);
        if (!original_interpolation)
            for (auto& transform : result)
                transform.rotation = interpolate_rotation(transform.rotation, transform.rotation, 0);
        return result;
    }
    auto found = std::lower_bound(clip.frame_times_.begin(), clip.frame_times_.end(), time);
    if (found == clip.frame_times_.end()) --found;
    const auto next_frame = static_cast<std::size_t>(found - clip.frame_times_.begin());
    const auto previous_frame = next_frame ? next_frame - 1 : 0;
    const auto previous_time = std::min(clip.frame_times_[previous_frame], clip.duration_);
    const auto period = 1.0f / static_cast<float>(clip.rate_);
    const auto blend = std::clamp((time - previous_time) / period, 0.0f, 1.0f);
    std::vector<EnbayaTransform> result(clip.tracks_);
    for (std::size_t track = 0; track < clip.tracks_; ++track) {
        const auto& a = clip.samples_[previous_frame * clip.tracks_ + track];
        const auto& b = clip.samples_[next_frame * clip.tracks_ + track];
        auto& transform = result[track];
        transform.rotation = original_interpolation
            ? original_interpolate_rotation(a.rotation, b.rotation, blend)
            : interpolate_rotation(a.rotation, b.rotation, blend);
        transform.translation = {
            a.translation.x * (1.0f - blend) + b.translation.x * blend,
            a.translation.y * (1.0f - blend) + b.translation.y * blend,
            a.translation.z * (1.0f - blend) + b.translation.z * blend
        };
        validate(transform);
    }
    return result;
}

std::vector<EnbayaTransform> sample_enbaya(const EnbayaClip& clip, float time_seconds) {
    return clip.sample(time_seconds, false);
}

std::vector<EnbayaTransform> sample_enbaya_original(const EnbayaClip& clip, float time_seconds) {
    try { return clip.sample(time_seconds, true); }
    catch (const AnimationMathError& error) { throw EnbayaError(error.what()); }
}
} // namespace dw4
