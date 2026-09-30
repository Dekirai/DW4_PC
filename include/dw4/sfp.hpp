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
#include <span>
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
// tuning (+0x0a semitones, +0x0b cents) is relative to the 32 kHz DSP; every
// split in the game tunes its 24 kHz sample to its own rate (-5 st +1 ct).
struct Split {
    std::uint8_t key{};
    std::uint8_t sample{};  // +2: 0x100 + index
    std::uint8_t pan{};     // 0x40 centre
    std::int8_t semitones{}, cents{};
    std::uint8_t detune{};  // +0x14: random pitch range in cents (803B9E88)
};

// One event of a sound-effect sequence.
struct Event {
    enum class Kind { note, other, end } kind{Kind::end};
    std::uint8_t status{};     // MIDI status after the 0xDn remap
    std::uint8_t key{}, velocity{};
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
    // The sequence of request id 0xA9GGNN00 (group by the top 16 bits, entry
    // by bits 8..15, 803C5188), or an empty span.
    std::span<const std::uint8_t> sequence(std::uint32_t request) const;
    bool has_group(std::uint32_t request) const;
    // The split playing `key`: an exact key match, else the nearest lower.
    const Split* split(int key) const;
    std::size_t sample_count() const { return samples_.size(); }
    const Sample& sample(std::size_t index) const { return samples_.at(index); }
    // The velocity curve (TBLD +0x60): 0..255 for velocity 0..127.
    std::uint8_t level(int velocity) const;
    // A whole sample as 16-bit PCM; the loop start in samples.
    std::vector<std::int16_t> decode(std::size_t index, std::uint32_t* loop_start = nullptr) const;

private:
    std::vector<std::uint8_t> data_;
    std::uint32_t requests_{}, curve_{}, pcm_offset_{}, pcm_size_{};
    std::vector<Split> splits_;
    std::vector<Sample> samples_;
};

// Decodes DSP ADPCM nibbles [2, sample.end_nibble] of a sample starting at
// data[0]; loop_start receives the loop start in output samples.
std::vector<std::int16_t> decode_adpcm(std::span<const std::uint8_t> data, const Sample& sample,
                                       std::uint32_t* loop_start = nullptr);

} // namespace dw4::sfp
