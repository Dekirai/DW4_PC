#include "dw4/sfd.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation) {
    ++checks;
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed SFD data was accepted");
}

using Bytes = std::vector<std::uint8_t>;
void append(Bytes& d, std::initializer_list<int> bytes) {
    for (int b : bytes) d.push_back(std::uint8_t(b));
}
void pack_header(Bytes& d) { append(d, {0, 0, 1, 0xBA, 0x21, 0, 1, 0, 1, 0x80, 0, 1}); }
// A packet: start code, id, length, body.
void packet(Bytes& d, int id, const Bytes& body) {
    append(d, {0, 0, 1, id, int(body.size() >> 8), int(body.size() & 0xFF)});
    d.insert(d.end(), body.begin(), body.end());
}

void streams() {
    Bytes file;
    pack_header(file);
    packet(file, 0xBB, {0x80, 0, 1, 0, 0x21, 0xFF});
    // Video: stuffing, STD buffer, PTS + DTS, then the payload.
    packet(file, 0xE0, {0xFF, 0xFF, 0x60, 0x2E, 0x31, 0, 1, 0, 1, 0x11, 0, 1, 0, 1, 0, 0, 1, 0xB3, 0x28, 0x01, 0xE0, 0xC4});
    // Audio: STD buffer and PTS, then the ADX file start.
    packet(file, 0xC0, {0x40, 0x04, 0x21, 0, 1, 0, 1, 0x80, 0x00});
    packet(file, 0xBE, Bytes(5, 0xFF)); // padding is skipped
    pack_header(file);
    packet(file, 0xE0, {0x0F, 0, 0, 1, 0, 0x12});      // no time stamps
    packet(file, 0xC0, {0x80, 0x00, 0x00, 0xAA, 0xBB}); // MPEG-2 header without data
    append(file, {0, 0, 1, 0xB9});
    append(file, {0, 0, 1, 0xE0, 0, 1, 0x0F}); // after the end code: ignored

    const auto s = sfd::demux(file);
    check(s.video == Bytes({0, 0, 1, 0xB3, 0x28, 0x01, 0xE0, 0xC4, 0, 0, 1, 0, 0x12}), "video payloads joined");
    check(s.audio == Bytes({0x80, 0x00, 0xAA, 0xBB}), "audio payloads joined");
    // The picture start code comes with the second video packet (ends at 102).
    check(s.picture_file_ends == std::vector<std::uint32_t>({102}), "where the demuxer has its picture");
}

void picture_ends() {
    // A picture start code split over two packets counts with the second.
    Bytes file;
    pack_header(file);
    packet(file, 0xE0, {0x0F, 0, 0, 1, 0x00, 0x11, 0, 0}); // a picture, then the next one's 00 00
    packet(file, 0xE0, {0x0F, 1, 0x00, 0x22});            // ... 01 00
    packet(file, 0xE0, {0x0F, 0, 0, 1, 0xB7});            // sequence end: no picture
    const auto s = sfd::demux(file);
    check(s.picture_file_ends == std::vector<std::uint32_t>({26, 36}), "pictures by the packet that completes them");
}

void video() {
    // 640x480 at 29.97 with three pictures.
    const Bytes v = {0, 0, 1, 0xB3, 0x28, 0x01, 0xE0, 0xC4, 0xFF, 0xFF, 0, 0, 1, 0xB8, 0, 0, 0, 0,
                     0, 0, 1, 0x00, 0x00, 0x0F, 0, 0, 1, 0x00, 0x00, 0x57, 0, 0, 1, 0x00, 0x01};
    const auto info = sfd::video_info(v);
    check(info.width == 640 && info.height == 480, "sequence size");
    check(info.frame_rate > 29.96 && info.frame_rate < 29.98, "frame rate code 4");
    check(info.pictures == 3, "picture count");
}

void malformed() {
    rejects([] {
        Bytes file;
        pack_header(file);
        append(file, {0, 0, 1, 0xE0, 0x10, 0x00, 0x0F}); // length past the data
        sfd::demux(file);
    });
    rejects([] {
        Bytes file = {0, 0, 1, 0xBA, 0x44, 0, 4, 0}; // MPEG-2 pack cut short
        sfd::demux(file);
    });
    rejects([] {
        Bytes file;
        packet(file, 0xC0, {0x55, 1, 2}); // not a PES header
        sfd::demux(file);
    });
    rejects([] {
        Bytes file;
        packet(file, 0xE0, {0x80, 0x80, 0x09, 0}); // MPEG-2 header longer than its packet
        sfd::demux(file);
    });
    rejects([] { sfd::video_info(Bytes{0, 0, 1, 0x00, 0, 0}); });
    rejects([] { sfd::video_info(Bytes{0, 0, 1, 0xB3, 0x28, 0x01}); });
}
} // namespace

int main() {
    try {
        streams();
        picture_ends();
        video();
        malformed();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " SFD checks passed\n";
}
