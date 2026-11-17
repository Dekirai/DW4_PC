#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dw4::game {

// The MPEG-1 video of a Sofdec movie (SoundManager::Movie), decoded with
// pl_mpeg in display order into RGBA pictures. A worker thread decodes and
// converts the pictures after the one the host asked for last (kAhead of
// them) while the host goes on, so a pass that shows a new picture only
// takes it (on the AYN Thor a picture's decoding inside the pass made every
// fourth pass take 36-46 ms: the opening and the attract movie juddered).
// The pictures stay exact: seek waits for the picture it asks for when the
// worker has not finished it yet.
class MovieDecoder {
public:
    explicit MovieDecoder(std::vector<std::uint8_t> video);
    ~MovieDecoder();
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;

    // Decodes forward until `picture` (0-based) is shown (the stream's last
    // picture when it ends before; an earlier `picture` keeps the current
    // one); false when no picture was decoded yet.
    bool seek(std::size_t picture);
    const std::vector<std::uint8_t>& rgba() const { return current_.rgba; }
    int width() const { return current_.width; }
    int height() const { return current_.height; }
    // A number of its own for every picture rgba() takes (0 before the
    // first): the renderer uploads each picture once.
    std::uint64_t picture_id() const { return current_.id; }
    // What the current picture cost (DW4_FRAME_STATS): its decoding and its
    // RGBA conversion on the worker, and how long seek waited for it.
    struct Costs {
        double decode_ms = 0, convert_ms = 0, waited_ms = 0;
    };
    const Costs& costs() const { return current_.costs; }

    static constexpr std::size_t kAhead = 2; // pictures decoded after the one asked for

private:
    struct State;
    struct Picture {
        std::size_t index = 0;
        std::vector<std::uint8_t> rgba;
        int width = 0, height = 0;
        std::uint64_t id = 0;
        Costs costs;
    };
    void work();

    std::vector<std::uint8_t> video_;
    std::unique_ptr<State> state_; // the worker's alone once it runs
    Picture current_;              // the host's (seek)
    bool taken_ = false;           // current_ holds a picture

    // Shared with the worker, under lock_.
    std::mutex lock_;
    std::condition_variable changed_;
    std::deque<Picture> ready_;                    // decoded pictures not taken yet, in order
    std::vector<std::vector<std::uint8_t>> spare_; // RGBA buffers to fill again
    std::size_t wanted_ = 0;                       // the worker decodes pictures up to this one
    std::size_t decoded_ = 0;                      // pictures decoded so far
    bool ended_ = false, stop_ = false;
    std::thread worker_;
};

} // namespace dw4::game
