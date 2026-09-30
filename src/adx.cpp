#include "dw4/adx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace dw4::adx {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("ADX: " + message); }

std::uint32_t be16(std::span<const std::uint8_t> d, std::size_t at) {
    return (std::uint32_t(d[at]) << 8) | d[at + 1];
}
std::uint32_t be32(std::span<const std::uint8_t> d, std::size_t at) {
    return (be16(d, at) << 16) | be16(d, at + 2);
}
} // namespace

Header read_header(std::span<const std::uint8_t> data) {
    if (data.size() < 0x14 || be16(data, 0) != 0x8000) fail("missing 0x8000 signature");
    const std::uint32_t copyright = be16(data, 2);
    Header h;
    h.data_offset = copyright + 4;
    if (copyright < 6 || data.size() < h.data_offset || std::memcmp(&data[copyright - 2], "(c)CRI", 6) != 0)
        fail("missing (c)CRI marker");
    if (data[4] != 3) fail("unsupported encoding " + std::to_string(data[4]));
    h.block_size = data[5];
    if (data[6] != 4 || h.block_size < 3) fail("unsupported block layout");
    h.channels = data[7];
    if (h.channels == 0 || h.channels > 8) fail("unsupported channel count");
    h.sample_rate = be32(data, 8);
    h.samples = be32(data, 12);
    if (h.sample_rate == 0) fail("zero sample rate");
    const std::uint32_t cutoff = be16(data, 16);
    const unsigned version = data[18];
    if (data[19] != 0) fail("encrypted streams are not supported");
    // Predictor: c = (a - sqrt((a + b)(a - b))) / b with a = sqrt(2) - cos(2 pi
    // cutoff / rate), b = sqrt(2) - 1; coefficients in 4.12 fixed point.
    const double z = std::cos(2.0 * 3.14159265358979323846 * double(cutoff) / double(h.sample_rate));
    const double a = std::sqrt(2.0) - z;
    const double b = std::sqrt(2.0) - 1.0;
    const double c = (a - std::sqrt((a + b) * (a - b))) / b;
    h.coef1 = static_cast<std::int16_t>(c * 8192.0);
    h.coef2 = static_cast<std::int16_t>(c * c * -4096.0);
    // Loop points: version 3 at 0x18, version 4 twelve bytes later.
    const std::size_t base = version == 4 ? 0x24 : version == 3 ? 0x18 : 0;
    if (base && base + 0x10 <= copyright && be32(data, base) == 1) {
        h.loop = true;
        h.loop_begin = be32(data, base + 4);
        h.loop_end = std::min(be32(data, base + 0x0c), h.samples);
        if (h.loop_begin >= h.loop_end) h.loop = false;
    }
    return h;
}

Decoder::Decoder(std::vector<std::uint8_t> data) : data_(std::move(data)), header_(read_header(data_)) {
    looping_ = header_.loop;
    block_pcm_.resize(std::size_t((header_.block_size - 2) * 2) * header_.channels);
}

void Decoder::decode_block() {
    const std::uint32_t channels = header_.channels;
    const std::uint32_t per_block = (header_.block_size - 2) * 2;
    const std::size_t offset = header_.data_offset + std::size_t(block_) * header_.block_size * channels;
    if (offset + std::size_t(header_.block_size) * channels > data_.size()) {
        finished_ = true;
        block_frames_ = block_read_ = 0;
        return;
    }
    if (header_.loop && !loop_saved_ && block_ == header_.loop_begin / per_block) {
        loop_hist1_ = hist1_;
        loop_hist2_ = hist2_;
        loop_saved_ = true;
    }
    for (std::uint32_t c = 0; c < channels; ++c) {
        const std::uint8_t* frame = &data_[offset + std::size_t(c) * header_.block_size];
        const std::int32_t scale = static_cast<std::int16_t>((frame[0] << 8) | frame[1]) + 1;
        std::int32_t h1 = hist1_[c], h2 = hist2_[c];
        for (std::uint32_t i = 0; i < per_block; ++i) {
            const std::uint8_t nibbles = frame[2 + i / 2];
            std::int32_t nibble = (i & 1) ? (nibbles & 0x0f) : (nibbles >> 4);
            if (nibble & 8) nibble -= 16;
            std::int32_t sample = nibble * scale + ((header_.coef1 * h1 + header_.coef2 * h2) >> 12);
            sample = std::clamp(sample, -32768, 32767);
            block_pcm_[std::size_t(i) * channels + c] = static_cast<std::int16_t>(sample);
            h2 = h1;
            h1 = sample;
        }
        hist1_[c] = h1;
        hist2_[c] = h2;
    }
    block_first_ = block_ * per_block;
    block_frames_ = per_block;
    block_read_ = position_ > block_first_ ? position_ - block_first_ : 0;
    ++block_;
}

std::size_t Decoder::read(std::int16_t* out, std::size_t frames) {
    const std::uint32_t channels = header_.channels;
    const std::uint32_t per_block = (header_.block_size - 2) * 2;
    std::size_t done = 0;
    while (done < frames && !finished_) {
        const bool loop = looping_ && header_.loop;
        const std::uint32_t end = loop ? header_.loop_end : header_.samples;
        if (position_ >= end) {
            if (!loop) {
                finished_ = true;
                break;
            }
            // Back to the loop start with the history it was first decoded with.
            block_ = header_.loop_begin / per_block;
            if (loop_saved_) {
                hist1_ = loop_hist1_;
                hist2_ = loop_hist2_;
            }
            position_ = header_.loop_begin;
            block_frames_ = block_read_ = 0;
            continue;
        }
        if (block_read_ >= block_frames_) {
            decode_block();
            if (finished_) break;
        }
        const std::uint32_t available = std::min(block_frames_ - block_read_, end - position_);
        const std::size_t count = std::min<std::size_t>(available, frames - done);
        std::copy_n(&block_pcm_[std::size_t(block_read_) * channels], count * channels, out + done * channels);
        block_read_ += std::uint32_t(count);
        position_ += std::uint32_t(count);
        done += count;
    }
    return done;
}

} // namespace dw4::adx
