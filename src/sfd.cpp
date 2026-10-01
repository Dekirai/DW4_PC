#include "dw4/sfd.hpp"

#include <stdexcept>
#include <string>

namespace dw4::sfd {
namespace {

bool start_code(std::span<const std::uint8_t> data, std::size_t at) {
    return at + 4 <= data.size() && data[at] == 0 && data[at + 1] == 0 && data[at + 2] == 1;
}

// The payload of an MPEG-1 (or MPEG-2) PES packet body: stuffing, the STD
// buffer size and the time stamps come first.
std::span<const std::uint8_t> pes_payload(std::span<const std::uint8_t> body) {
    std::size_t at = 0;
    if (!body.empty() && (body[0] & 0xC0) == 0x80) {
        // MPEG-2: flags, then the header data length.
        if (body.size() < 3) throw std::runtime_error("sfd: short MPEG-2 PES header");
        at = 3u + body[2];
    } else {
        while (at < body.size() && body[at] == 0xFF && at < 16) ++at;
        if (at < body.size() && (body[at] & 0xC0) == 0x40) at += 2;
        if (at >= body.size()) return {};
        const std::uint8_t flags = body[at] & 0xF0;
        if (flags == 0x20) at += 5;
        else if (flags == 0x30) at += 10;
        else if (body[at] == 0x0F) at += 1;
        else throw std::runtime_error("sfd: bad PES header");
    }
    if (at > body.size()) throw std::runtime_error("sfd: PES header past its packet");
    return body.subspan(at);
}

} // namespace

Streams demux(std::span<const std::uint8_t> data) {
    Streams out;
    std::size_t at = 0;
    while (at + 4 <= data.size()) {
        if (!start_code(data, at)) {
            ++at; // padding between packs
            continue;
        }
        const std::uint8_t id = data[at + 3];
        if (id == 0xB9) break; // program end
        if (id == 0xBA) {
            // Pack header: 12 bytes (MPEG-1) or 14 plus stuffing (MPEG-2).
            if (at + 5 > data.size()) throw std::runtime_error("sfd: short pack header");
            std::size_t size = 12;
            if ((data[at + 4] & 0xC0) == 0x40) {
                if (at + 14 > data.size()) throw std::runtime_error("sfd: short pack header");
                size = 14u + (data[at + 13] & 7u);
            }
            if (at + size > data.size()) throw std::runtime_error("sfd: pack header past the data");
            at += size;
            continue;
        }
        if (id < 0xBB) {
            ++at; // not a packet start code
            continue;
        }
        if (at + 6 > data.size()) throw std::runtime_error("sfd: short packet header");
        const std::size_t length = std::size_t(data[at + 4]) << 8 | data[at + 5];
        if (at + 6 + length > data.size())
            throw std::runtime_error("sfd: packet " + std::to_string(id) + " past the data");
        const auto body = data.subspan(at + 6, length);
        if (id == 0xE0 || id == 0xC0) {
            const auto payload = pes_payload(body);
            auto& stream = id == 0xE0 ? out.video : out.audio;
            stream.insert(stream.end(), payload.begin(), payload.end());
        }
        at += 6 + length;
    }
    return out;
}

VideoInfo video_info(std::span<const std::uint8_t> video) {
    static constexpr double rates[16] = {0.0, 24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0, 50.0, 60000.0 / 1001.0, 60.0};
    VideoInfo info;
    bool header = false;
    for (std::size_t at = 0; at + 4 <= video.size(); ++at) {
        if (!start_code(video, at)) continue;
        const std::uint8_t code = video[at + 3];
        if (code == 0x00) {
            ++info.pictures;
        } else if (code == 0xB3 && !header) {
            if (at + 8 > video.size()) throw std::runtime_error("sfd: short sequence header");
            info.width = int(video[at + 4]) << 4 | video[at + 5] >> 4;
            info.height = int(video[at + 5] & 0x0F) << 8 | video[at + 6];
            info.frame_rate = rates[video[at + 7] & 0x0F];
            header = true;
        }
    }
    if (!header) throw std::runtime_error("sfd: no sequence header");
    return info;
}

} // namespace dw4::sfd
