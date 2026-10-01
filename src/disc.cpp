#include "dw4/disc.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace dw4 {
namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Disc image: " + message); }

std::uint32_t be32(std::span<const std::uint8_t> d, std::size_t at) {
    if (at + 4 > d.size()) fail("read past the end of the header or file table");
    return (std::uint32_t(d[at]) << 24) | (std::uint32_t(d[at + 1]) << 16) | (std::uint32_t(d[at + 2]) << 8) | d[at + 3];
}

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::vector<std::uint8_t> read_range(const std::filesystem::path& path, std::uint64_t offset, std::size_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path.string());
    in.seekg(std::streamoff(offset));
    std::vector<std::uint8_t> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
    if (!in || std::size_t(in.gcount()) != size) fail("truncated image");
    return bytes;
}

constexpr std::uint32_t kMagic = 0xC2339F3D; // GameCube disc magic at 0x1C
constexpr std::size_t kHeaderSize = 0x440;

} // namespace

DiscImage DiscImage::parse(std::span<const std::uint8_t> header, std::span<const std::uint8_t> fst,
                           std::uint64_t image_size) {
    if (header.size() < kHeaderSize || be32(header, 0x1C) != kMagic) fail("not a GameCube disc image");
    DiscImage disc;
    disc.image_size_ = image_size;
    disc.game_id_.assign(reinterpret_cast<const char*>(header.data()), 6);
    disc.disc_number_ = header[6];
    disc.revision_ = header[7];
    disc.dol_offset_ = be32(header, 0x420);
    // FST: 12-byte entries (flags | 24-bit name offset, offset / parent,
    // size / next); the root's last field is the entry count; names follow.
    if (fst.size() < 12) fail("file table is truncated");
    const std::uint32_t count = be32(fst, 8);
    if (count == 0 || std::uint64_t(count) * 12 > fst.size()) fail("file table entry count is invalid");
    const std::size_t names = std::size_t(count) * 12;
    auto name_at = [&](std::uint32_t offset) {
        std::string name;
        for (std::size_t i = names + offset; i < fst.size() && fst[i]; ++i) name.push_back(char(fst[i]));
        if (names + offset >= fst.size()) fail("file name outside the file table");
        return name;
    };
    // Directory ends (exclusive) with their path prefixes.
    std::vector<std::pair<std::uint32_t, std::string>> stack{{count, ""}};
    for (std::uint32_t i = 1; i < count; ++i) {
        while (stack.size() > 1 && i >= stack.back().first) stack.pop_back();
        const std::uint32_t word = be32(fst, std::size_t(i) * 12);
        const std::string name = name_at(word & 0xFFFFFF);
        const std::uint32_t a = be32(fst, std::size_t(i) * 12 + 4), b = be32(fst, std::size_t(i) * 12 + 8);
        const std::string full = stack.back().second + name;
        if (word >> 24) {
            if (b <= i || b > count) fail("directory end is invalid: " + full);
            stack.push_back({b, full + "/"});
        } else {
            if (std::uint64_t(a) + b > image_size) fail("file lies outside the image: " + full);
            disc.files_.push_back({full, a, b});
        }
    }
    return disc;
}

DiscImage DiscImage::open(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < kHeaderSize) fail("cannot read " + path.string());
    const auto header = read_range(path, 0, kHeaderSize);
    if (be32(header, 0x1C) != kMagic) fail(path.filename().string() + " is not an uncompressed GameCube disc image");
    const std::uint32_t fst_offset = be32(header, 0x424), fst_size = be32(header, 0x428);
    if (fst_size == 0 || fst_size > 16u * 1024u * 1024u || std::uint64_t(fst_offset) + fst_size > size)
        fail("file table lies outside the image");
    const auto fst = read_range(path, fst_offset, fst_size);
    auto disc = parse(header, fst, size);
    disc.path_ = path;
    return disc;
}

bool DiscImage::looks_like_disc(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || std::filesystem::file_size(path, error) < kHeaderSize) return false;
    try {
        return be32(read_range(path, 0, 0x20), 0x1C) == kMagic;
    } catch (const std::exception&) {
        return false;
    }
}

const DiscFile* DiscImage::find(std::string_view path) const noexcept {
    for (const auto& file : files_)
        if (same(file.path, path)) return &file;
    return nullptr;
}

std::vector<std::uint8_t> DiscImage::read(const DiscFile& file, std::size_t max_bytes) const {
    if (file.size > max_bytes) fail("file exceeds the read limit: " + file.path);
    return read_range(path_, file.offset, std::size_t(file.size));
}

} // namespace dw4
