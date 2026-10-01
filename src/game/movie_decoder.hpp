#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dw4::game {

// The MPEG-1 video of a Sofdec movie (SoundManager::Movie), decoded with
// pl_mpeg in display order into RGBA pictures.
class MovieDecoder {
public:
    explicit MovieDecoder(std::vector<std::uint8_t> video);
    ~MovieDecoder();
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;

    // Decodes forward until `picture` (0-based) is shown; false when no
    // picture was decoded yet.
    bool seek(std::size_t picture);
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct State;
    std::vector<std::uint8_t> video_;
    std::unique_ptr<State> state_;
    std::vector<std::uint8_t> rgba_;
    int width_{}, height_{};
    std::size_t shown_{};  // pictures decoded so far
    bool ended_{};
};

} // namespace dw4::game
