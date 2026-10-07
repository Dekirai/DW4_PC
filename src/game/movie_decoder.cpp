#include "movie_decoder.hpp"

#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#include "pl_mpeg.h"

namespace dw4::game {

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
}

MovieDecoder::~MovieDecoder() {
    if (state_ && state_->video) plm_video_destroy(state_->video);
}

bool MovieDecoder::seek(std::size_t picture) {
    plm_frame_t* last = nullptr;
    while (!ended_ && shown_ <= picture) {
        plm_frame_t* frame = state_->next();
        if (!frame) {
            ended_ = true;
            break;
        }
        ++shown_;
        last = frame;
    }
    if (last) {
        width_ = int(last->width);
        height_ = int(last->height);
        rgba_.resize(std::size_t(width_) * std::size_t(height_) * 4u, 0xFF);
        plm_frame_to_rgba(last, rgba_.data(), width_ * 4);
    }
    return !rgba_.empty();
}

} // namespace dw4::game
