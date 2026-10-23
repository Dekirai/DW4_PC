#include "dw4/sfp.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dw4::sfp {
namespace {

std::uint16_t be16(std::span<const std::uint8_t> d, std::size_t at) {
    if (at + 2 > d.size()) throw std::runtime_error("SFP read past the end");
    return std::uint16_t((d[at] << 8) | d[at + 1]);
}
std::uint32_t be32(std::span<const std::uint8_t> d, std::size_t at) {
    return (std::uint32_t(be16(d, at)) << 16) | be16(d, at + 2);
}
std::uint8_t byte_at(std::span<const std::uint8_t> d, std::size_t at) {
    if (at >= d.size()) throw std::runtime_error("SFP read past the end");
    return d[at];
}
bool magic(std::span<const std::uint8_t> d, std::size_t at, const char* text) {
    return at + 8 <= d.size() && std::memcmp(&d[at], text, 8) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
Sequence::Sequence(std::span<const std::uint8_t> data) : data_(data) {
    if (data_.empty()) return;
    flags_ = data_[0];
    position_ = 1;
    finished_ = false;
}

Event Sequence::next() {
    Event event;
    if (finished_) return event;
    auto read = [&]() -> std::uint8_t { return position_ < data_.size() ? data_[position_++] : 0xff; };
    if (position_ >= data_.size() || data_[position_] == 0xff) {
        finished_ = true;
        return event;
    }
    if (data_[position_] & 0x80) running_ = data_[position_++];
    event.status = running_;
    const int high = running_ & 0xf0;
    const std::uint8_t first = read();
    bool no_wait = (first & 0x80) != 0;
    event.kind = Event::Kind::other;
    if (high == 0xd0 && (flags_ & 0x80)) {
        // 803C4DA0: key, velocity, a third byte whose bit 7 ends the event.
        event.kind = Event::Kind::note;
        event.status = std::uint8_t(running_ & ~0x40);
        event.key = first & 0x7f;
        event.velocity = read() & 0x7f;
        no_wait = (read() & 0x80) != 0;
    } else if (high == 0x90 || high == 0xa0 || high == 0xb0 || high == 0xf0) {
        const std::uint8_t second = read();
        no_wait = (second & 0x80) != 0;
        if (high == 0x90) {
            event.kind = Event::Kind::note;
            event.key = first & 0x7f;
            event.velocity = second & 0x7f;
        }
    }
    if (!no_wait) {
        event.has_wait = true;
        std::uint32_t wait = read();
        if (wait & 0x80) wait = (read() & 0x7fu) | ((wait << 7) & 0x3f80u);
        event.wait_ms = wait;
    }
    return event;
}

// ---------------------------------------------------------------------------
Bank::Bank(std::vector<std::uint8_t> data) : data_(std::move(data)) {
    const std::span<const std::uint8_t> d(data_);
    if (!magic(d, 0, "gcaxDTPK") || !magic(d, 0x20, "gcaxTBLD")) throw std::runtime_error("Not an SFP bank");
    pcm_size_ = be32(d, 0x18);
    pcm_offset_ = be32(d, 0x1c);
    if (!magic(d, pcm_offset_, "gcaxPCMD") || pcm_offset_ + std::uint64_t(pcm_size_) > d.size())
        throw std::runtime_error("SFP bank without its PCMD chunk");
    curve_ = be32(d, 0x60) + 4;
    requests_ = be32(d, 0x64);
    const std::uint32_t drum = be32(d, 0x68), sample_table = be32(d, 0x78);
    effects_ = be32(d, 0x70);
    if (curve_ + 128 > d.size() || !requests_ || !drum || !sample_table) throw std::runtime_error("SFP bank tables");
    byte_at(d, requests_);
    program_type_ = byte_at(d, drum);

    // The drum program's first layer: split count - 1, the splits from +0x20.
    const std::uint32_t layer = drum + be16(d, drum + 8);
    const int splits = be16(d, layer) + 1;
    for (int i = 0; i < splits; ++i) {
        const std::size_t at = layer + 0x20 + std::size_t(i) * 0x40;
        Split split;
        split.key = byte_at(d, at);
        split.sample = byte_at(d, at + 3);
        split.pan = byte_at(d, at + 6);
        split.semitones = std::int8_t(byte_at(d, at + 0x0a));
        split.cents = std::int8_t(byte_at(d, at + 0x0b));
        split.detune = byte_at(d, at + 0x14);
        split.channel_aux = byte_at(d, at + 0x15) != 0;
        split.aux_mode = byte_at(d, at + 0x16);
        split.aux_a = byte_at(d, at + 0x17);
        split.aux_b = byte_at(d, at + 0x18);
        split.sample_word = be16(d, at + 2);
        split.attack = std::int16_t(be16(d, at + 0x0c));
        split.decay = std::int16_t(be16(d, at + 0x0e));
        split.sustain = std::int16_t(be16(d, at + 0x10));
        split.release = std::int16_t(be16(d, at + 0x12));
        split.priority = byte_at(d, at + 0x1d);
        splits_.push_back(split);
    }

    const int samples = byte_at(d, sample_table + 3) + 1;
    for (int i = 0; i < samples; ++i) {
        const std::size_t at = sample_table + 4 + std::size_t(i) * 0x44;
        Sample sample;
        sample.start = be32(d, at);
        sample.loop_nibble = be32(d, at + 4);
        sample.end_nibble = be32(d, at + 8);
        for (std::size_t c = 0; c < 16; ++c) sample.coefs[c] = std::int16_t(be16(d, at + 0x0c + c * 2));
        sample.ps = std::uint8_t(be16(d, at + 0x2e));
        sample.loop = (be16(d, at + 0x3a) & 0x0400) != 0;
        sample.rate = be16(d, at + 0x3c);
        sample.bytes = be32(d, at + 0x40);
        if (sample.start + std::uint64_t(sample.bytes) > pcm_size_ || !sample.rate)
            throw std::runtime_error("SFP sample outside the PCMD chunk");
        samples_.push_back(sample);
    }
    for (const auto& split : splits_)
        if (split.sample >= samples_.size()) throw std::runtime_error("SFP split without its sample");
}

bool Bank::has_group(std::uint32_t request) const {
    const std::span<const std::uint8_t> d(data_);
    const int groups = d[requests_] + 1;
    for (int g = 0; g < groups; ++g)
        if (requests_ + 8u + 4u * unsigned(g) <= d.size() && (be32(d, requests_ + 4 + 4 * g) >> 16) == (request >> 16))
            return true;
    return false;
}

std::span<const std::uint8_t> Bank::sequence(std::uint32_t request) const {
    const std::span<const std::uint8_t> d(data_);
    const int groups = d[requests_] + 1;
    for (int g = 0; g < groups; ++g) {
        if (requests_ + 8u + 4u * unsigned(g) > d.size()) break;
        const std::uint32_t entry = be32(d, requests_ + 4 + 4 * g);
        if ((entry >> 16) != (request >> 16)) continue;
        const std::size_t group = requests_ + (entry & 0xffff);
        const unsigned index = (request >> 8) & 0xff;
        if (group >= d.size() || index > d[group] || group + 8 + 4 * index > d.size()) return {};
        const std::uint32_t offset = be32(d, group + 4 + 4 * index);
        if (!offset || requests_ + offset >= d.size()) return {};
        return d.subspan(requests_ + offset);
    }
    return {};
}

const Split* Bank::split(int key) const {
    const Split* best = nullptr;
    for (const auto& split : splits_) {
        if (split.key == key) return &split;
        if (split.key < key && (!best || split.key > best->key)) best = &split;
    }
    return best;
}

std::uint8_t Bank::level(int velocity) const { return data_[curve_ + std::size_t(std::clamp(velocity, 0, 127))]; }

std::optional<EffectPreset> Bank::effect(int preset) const {
    // 803BF7E8: the table's first word is the last preset's index, then the
    // presets' offsets from the table; a preset is the types word (AUX A in
    // bits 24..31, AUX B in 16..23), then AUX A's and AUX B's parameter
    // words, as many as the type takes (803BFAE8 and on).
    if (!effects_ || preset < 0) return std::nullopt;
    const std::span<const std::uint8_t> d(data_);
    try {
        if (be32(d, effects_) < std::uint32_t(preset)) return std::nullopt;
        std::size_t at = effects_ + std::size_t(be32(d, effects_ + 4 + 4 * std::size_t(preset)));
        const std::uint32_t types = be32(d, at);
        EffectPreset result;
        result.type_a = std::uint8_t(types >> 24);
        result.type_b = std::uint8_t(types >> 16);
        // Parameter words: ReverbStd 5, ReverbHi 6, Chorus 3, Delay 9, type 4: 5.
        auto words = [](std::uint8_t type) -> std::size_t {
            constexpr std::size_t kWords[5] = {5, 6, 3, 9, 5};
            return type < 5 ? kWords[type] : 0;
        };
        at += 4;
        for (std::size_t i = 0; i < words(result.type_a); ++i, at += 4) result.a.push_back(be32(d, at));
        for (std::size_t i = 0; i < words(result.type_b); ++i, at += 4) result.b.push_back(be32(d, at));
        return result;
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

std::vector<std::int16_t> Bank::decode(std::size_t index, std::uint32_t* loop_start) const {
    const auto& s = samples_.at(index);
    return decode_adpcm(std::span<const std::uint8_t>(data_).subspan(pcm_offset_ + s.start, s.bytes), s, loop_start);
}

// ---------------------------------------------------------------------------
std::vector<std::int16_t> decode_adpcm(std::span<const std::uint8_t> data, const Sample& sample, std::uint32_t* loop_start) {
    std::vector<std::int16_t> out;
    out.reserve(std::size_t(sample.end_nibble) * 7 / 8 + 16);
    if (loop_start) *loop_start = 0;
    int hist1 = 0, hist2 = 0, scale = 1, c1 = 0, c2 = 0;
    for (std::uint32_t nibble = 0; nibble <= sample.end_nibble; ++nibble) {
        const std::size_t at = nibble / 2;
        if (at >= data.size()) break;
        if (nibble % 16 == 0) {
            // Frame header: predictor index and scale.
            const int ps = data[at];
            scale = 1 << (ps & 15);
            c1 = sample.coefs[std::size_t((ps >> 4) & 7) * 2];
            c2 = sample.coefs[std::size_t((ps >> 4) & 7) * 2 + 1];
            continue;
        }
        if (nibble % 16 == 1) continue;
        if (loop_start && nibble == sample.loop_nibble) *loop_start = std::uint32_t(out.size());
        int value = (nibble & 1) ? data[at] & 15 : data[at] >> 4;
        if (value >= 8) value -= 16;
        int pcm = ((value * scale) << 11) + 1024 + c1 * hist1 + c2 * hist2;
        pcm = std::clamp(pcm >> 11, -32768, 32767);
        hist2 = hist1;
        hist1 = pcm;
        out.push_back(std::int16_t(pcm));
    }
    return out;
}

} // namespace dw4::sfp
