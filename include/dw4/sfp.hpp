#pragma once

// CRI Sound Factory sound-effect banks (.sfp, beCriSfpData) as the GameCube
// SNDF driver (803B8700..803C5D40) reads them. A "gcaxDTPK" file holds a
// TBLD chunk (request table, drum program, DSP ADPCM sample records) and a
// PCMD chunk with the sample data.
//
// A sound effect is a request id 0xA9GGNN00: group GG of the request table,
// entry NN, a short sequence (803C49F4) whose notes play drum-program splits
// on the bank's channel. Every sequence in the game is a note or two.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace dw4::sfp {

// A 68-byte sample record of the table at TBLD +0x78.
struct Sample {
    std::uint32_t start{};        // byte offset in the PCMD chunk
    std::uint32_t loop_nibble{};  // loop start address, nibbles from start
    std::uint32_t end_nibble{};   // last nibble address
    std::array<std::int16_t, 16> coefs{};
    std::uint8_t ps{};            // first frame header
    bool loop{};                  // flags 0x0400
    std::uint32_t rate{};         // Hz
    std::uint32_t bytes{};
};

// A split of the drum program (TBLD +0x68): the sample a key plays. Its
// tuning (+0x0a semitones, +0x0b 64ths of a semitone) is relative to the
// 32 kHz DSP; every split in the game tunes its 24 kHz sample to its own
// rate (-5 st +1/64: -498 cents, an SRC ratio of 0.750015).
struct Split {
    std::uint8_t key{};
    std::uint8_t sample{};  // +2: 0x100 + index
    std::uint8_t pan{};     // 0x40 centre
    std::int8_t semitones{}, cents{};
    std::uint8_t detune{};  // +0x14: random pitch range, 64ths of a semitone (803B9E88)
    // The AUX sends of the drum voice (803BA5B4): +0x15 nonzero takes the
    // channel's sends instead; +0x16 bit 0 / bit 1: AUX A / B before the
    // fader; +0x17 / +0x18: the AUX A / B level 0..127 (0: off).
    bool channel_aux{};
    std::uint8_t aux_mode{}, aux_a{}, aux_b{};
    // +0x02: 0x100 + the sample (0: the split plays nothing, 803B9DD0).
    std::uint16_t sample_word{};
    // The voice's volume envelope (803B9BFC, 803C325C, 803BBA80; 0.1 dB per
    // AX frame): +0x0C attack (0: none, else from -96 dB at that rate),
    // +0x0E / +0x10 decay rate and sustain level, +0x12 release rate (0:
    // at once).
    std::int16_t attack{}, decay{}, sustain{}, release{};
    std::uint8_t priority{}; // +0x1D: the AX priority - 11
};

// A preset of a bank's effect table (TBLD +0x70, 803BF7E8): the effect types
// of AUX A and AUX B (0 ReverbStd, 1 ReverbHi, 2 Chorus, 3 Delay, 4 a fifth
// type, 0xFF none) and their parameter words (thousandths). Only allse.sfp
// has a table: five ReverbStd presets on AUX A.
struct EffectPreset {
    std::uint8_t type_a{0xff}, type_b{0xff};
    std::vector<std::uint32_t> a, b;
};

// One event of a sound-effect sequence.
struct Event {
    enum class Kind { note, other, end } kind{Kind::end};
    std::uint8_t status{};     // MIDI status after the 0xDn remap
    std::uint8_t key{}, velocity{};
    // The delta after the event: bit 7 of its last data byte clear (also a
    // delta of 0; 803C49F4 ends the track's pass after any event with one).
    bool has_wait{};
    std::uint32_t wait_ms{};   // delay before the next event (tempo 0x10000)
};

// Steps a sequence: its first byte is the track flags (5785), bit 7 a
// one-shot sound effect whose 0xDn statuses are notes with three data bytes.
class Sequence {
public:
    Sequence() = default;
    explicit Sequence(std::span<const std::uint8_t> data);
    // Reads the next event; Kind::end at 0xFF or the end of the data.
    Event next();
    bool finished() const { return finished_; }
    // The tick state (round J): the data as a place in `base` (its bank's
    // bytes), the cursor and flags.
    template <class Archive> void tick_io(Archive& a, std::span<const std::uint8_t> base) {
        std::uint64_t offset = data_.empty() ? 0 : std::uint64_t(data_.data() - base.data());
        std::uint64_t length = data_.size();
        a.io(offset);
        a.io(length);
        if (a.reading()) {
            if (offset > base.size() || length > base.size() - offset) throw std::runtime_error("a sequence outside its bank");
            data_ = length ? base.subspan(std::size_t(offset), std::size_t(length)) : std::span<const std::uint8_t>{};
        }
        a.io(position_);
        a.io(flags_);
        a.io(running_);
        a.io(finished_);
    }

private:
    std::span<const std::uint8_t> data_;
    std::size_t position_{};
    std::uint8_t flags_{}, running_{};
    bool finished_ = true;
};

class Bank {
public:
    // Validates the chunks and tables; throws std::runtime_error on bad data.
    explicit Bank(std::vector<std::uint8_t> data);
    std::span<const std::uint8_t> bytes() const { return data_; }
    // The sequence of request id 0xA9GGNN00 (group by the top 16 bits, entry
    // by bits 8..15, 803C5188), or an empty span.
    std::span<const std::uint8_t> sequence(std::uint32_t request) const;
    bool has_group(std::uint32_t request) const;
    // The split playing `key`: an exact key match, else the nearest lower.
    const Split* split(int key) const;
    // The drum note-on's split of a type 2 program (803B9D00): split
    // `index` of the layer (the note's key), or null past its last.
    const Split* split_at(int index) const {
        return index >= 0 && std::size_t(index) < splits_.size() ? &splits_[std::size_t(index)] : nullptr;
    }
    std::size_t sample_count() const { return samples_.size(); }
    const Sample& sample(std::size_t index) const { return samples_.at(index); }
    // The velocity curve (TBLD +0x60): 0..255 for velocity 0..127.
    std::uint8_t level(int velocity) const;
    // The bank's id in the SNDF bank list (DTPK +0x4D, stored by 803C2598):
    // 0 for allse.sfp, 0xFF for every other bank of the game.
    std::uint8_t id() const { return data_[0x4d]; }
    // The drum program's type (its first byte, 803B9C60; 2 in every bank:
    // the split is the key's).
    std::uint8_t program_type() const { return program_type_; }
    // Preset `preset` of the effect table; nothing without a table or past
    // its last preset.
    std::optional<EffectPreset> effect(int preset) const;
    // A whole sample as 16-bit PCM; the loop start in samples.
    std::vector<std::int16_t> decode(std::size_t index, std::uint32_t* loop_start = nullptr) const;

private:
    std::vector<std::uint8_t> data_;
    std::uint32_t requests_{}, curve_{}, pcm_offset_{}, pcm_size_{}, effects_{};
    std::uint8_t program_type_{};
    std::vector<Split> splits_;
    std::vector<Sample> samples_;
};

// Decodes DSP ADPCM nibbles [2, sample.end_nibble] of a sample starting at
// data[0]; loop_start receives the loop start in output samples.
std::vector<std::int16_t> decode_adpcm(std::span<const std::uint8_t> data, const Sample& sample,
                                       std::uint32_t* loop_start = nullptr);

} // namespace dw4::sfp
