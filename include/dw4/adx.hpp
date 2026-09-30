#pragma once

// CRI ADX streams (the game's BGM and ambience in ndmw.afs, played by ADXT):
// 4-bit ADPCM blocks with a fixed-coefficient predictor from the header's
// high-pass cutoff, optional loop points (header versions 3 and 4).
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dw4::adx {

struct Header {
    std::uint32_t data_offset{};   // first block (copyright offset + 4)
    std::uint32_t block_size{};    // bytes per channel block (18)
    std::uint32_t channels{};
    std::uint32_t sample_rate{};
    std::uint32_t samples{};       // per channel
    std::int16_t coef1{}, coef2{}; // predictor from the high-pass cutoff
    bool loop{};
    std::uint32_t loop_begin{}, loop_end{}; // samples
};

// Validates and reads a header; throws std::runtime_error on bad data.
Header read_header(std::span<const std::uint8_t> data);

// Streaming decoder over a whole file in memory. Output is interleaved
// signed 16-bit PCM at the stream's own rate and channel count.
class Decoder {
public:
    explicit Decoder(std::vector<std::uint8_t> data);
    const Header& header() const { return header_; }
    // Decodes up to frames sample frames; fewer at the end of a non-looping
    // stream (0 once finished).
    std::size_t read(std::int16_t* out, std::size_t frames);
    bool finished() const { return finished_; }
    void set_looping(bool looping) { looping_ = looping; }

private:
    void decode_block();
    std::vector<std::uint8_t> data_;
    Header header_;
    bool looping_{};
    bool finished_{};
    std::uint32_t block_{};    // next block to decode
    std::uint32_t position_{}; // sample index of the next output frame
    std::array<std::int32_t, 8> hist1_{}, hist2_{};
    // History at the start of the block holding the loop start.
    bool loop_saved_{};
    std::array<std::int32_t, 8> loop_hist1_{}, loop_hist2_{};
    std::vector<std::int16_t> block_pcm_; // 32 interleaved frames
    std::uint32_t block_first_{}, block_frames_{}, block_read_{};
};

} // namespace dw4::adx
