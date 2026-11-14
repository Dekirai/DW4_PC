#include "movie_decoder.hpp"

#include <atomic>
#include <chrono>

#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#include "pl_mpeg.h"

namespace dw4::game {

namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point from, Clock::time_point to) { return std::chrono::duration<double, std::milli>(to - from).count(); }
std::atomic<std::uint64_t> g_next_id{1};
} // namespace

struct MovieDecoder::State {
    plm_video_t* video{};
    bool drained{};

    // Next picture in display order, or null after the last one. Sofdec shows
    // every picture of the stream. pl_mpeg discards consumed bytes of a memory
    // buffer (plm_video_decode -> plm_buffer_discard_read_bytes), so its length
    // never reaches total_size and plm_buffer_has_ended stays false: the last
    // picture in decode order is refused, and the final reference picture is
    // flushed only after an I/P picture. When the stream runs dry, mark the
    // end, decode that last picture and then return the held reference.
    plm_frame_t* next() {
        if (drained) return flush();
        if (plm_frame_t* frame = plm_video_decode(video)) return frame;
        drained = true;
        video->buffer->has_ended = TRUE;
        if (plm_frame_t* frame = plm_video_decode(video)) return frame;
        return flush();
    }
    plm_frame_t* flush() {
        if (!video->has_reference_frame) return nullptr;
        video->has_reference_frame = FALSE;
        return &video->frame_backward;
    }
};

MovieDecoder::MovieDecoder(std::vector<std::uint8_t> video) : video_(std::move(video)), state_(std::make_unique<State>()) {
    if (video_.empty()) {
        ended_ = true;
        return;
    }
    plm_buffer_t* buffer = plm_buffer_create_with_memory(video_.data(), video_.size(), 0);
    state_->video = plm_video_create_with_buffer(buffer, 1);
    worker_ = std::thread([this] { work(); });
}

MovieDecoder::~MovieDecoder() {
    if (worker_.joinable()) {
        {
            std::lock_guard guard(lock_);
            stop_ = true;
        }
        changed_.notify_all();
        worker_.join();
    }
    if (state_ && state_->video) plm_video_destroy(state_->video);
}

void MovieDecoder::work() {
    std::unique_lock guard(lock_);
    for (;;) {
        changed_.wait(guard, [&] { return stop_ || decoded_ <= wanted_; });
        if (stop_) return;
        Picture picture;
        if (!spare_.empty()) {
            picture.rgba = std::move(spare_.back());
            spare_.pop_back();
        }
        guard.unlock();
        // (pl_mpeg's state and the picture's buffer are the worker's here.)
        const auto start = Clock::now();
        plm_frame_t* frame = state_->next();
        const auto decoded = Clock::now();
        if (frame) {
            picture.width = int(frame->width);
            picture.height = int(frame->height);
            // (plm_frame_to_rgba writes R, G and B; the alpha stays 0xFF.)
            picture.rgba.resize(std::size_t(picture.width) * std::size_t(picture.height) * 4u, 0xFF);
            plm_frame_to_rgba(frame, picture.rgba.data(), picture.width * 4);
            picture.costs.decode_ms = ms(start, decoded);
            picture.costs.convert_ms = ms(decoded, Clock::now());
        }
        guard.lock();
        if (!frame) {
            ended_ = true;
            changed_.notify_all();
            return;
        }
        picture.index = decoded_++;
        picture.id = g_next_id.fetch_add(1, std::memory_order_relaxed);
        ready_.push_back(std::move(picture));
        changed_.notify_all();
    }
}

bool MovieDecoder::seek(std::size_t picture) {
    // (The same picture again, or an earlier one: the current one.)
    if (taken_ && current_.index >= picture) return true;
    if (!worker_.joinable()) return taken_;
    const auto start = Clock::now();
    std::unique_lock guard(lock_);
    if (picture + kAhead > wanted_) {
        wanted_ = picture + kAhead;
        changed_.notify_all();
    }
    changed_.wait(guard, [&] { return ended_ || decoded_ > picture; });
    bool took = false;
    while (!ready_.empty() && ready_.front().index <= picture) {
        if (taken_ && spare_.size() < kAhead + 1) spare_.push_back(std::move(current_.rgba));
        current_ = std::move(ready_.front());
        ready_.pop_front();
        taken_ = took = true;
    }
    guard.unlock();
    if (took) current_.costs.waited_ms = ms(start, Clock::now());
    return taken_;
}

} // namespace dw4::game
