#pragma once

// CRI Sofdec movies (.sfd, the opening in area.afs): an MPEG-1 program stream
// whose video stream 0xE0 is MPEG-1 video and whose audio stream 0xC0 carries
// a CRI ADX file instead of MPEG audio.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dw4::sfd {

struct Streams {
    std::vector<std::uint8_t> video; // MPEG-1 video elementary stream
    std::vector<std::uint8_t> audio; // the ADX file
    // For each picture start code of the video stream, the file offset just
    // past the packet that completes it: what a demuxer has read from the
    // file when it reaches that picture.
    std::vector<std::uint32_t> picture_file_ends;
};

// Splits the program stream into its video and audio payloads. Throws
// std::runtime_error on a pack or packet that runs past the data.
Streams demux(std::span<const std::uint8_t> data);

struct VideoInfo {
    int width{}, height{};
    double frame_rate{};    // pictures per second (0 when unknown)
    std::size_t pictures{}; // picture start codes in the stream
};

// The first sequence header and the picture count of a video stream. Throws
// std::runtime_error when there is no complete sequence header.
VideoInfo video_info(std::span<const std::uint8_t> video);

} // namespace dw4::sfd
