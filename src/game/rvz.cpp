#include "rvz.hpp"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace dw4::game {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }

struct Reader {
    std::ifstream in;
    std::uint64_t size{};
    std::vector<std::uint8_t> read(std::uint64_t offset, std::size_t count) {
        if (offset + count > size) throw std::runtime_error("RVZ data lies outside the file");
        std::vector<std::uint8_t> bytes(count);
        in.seekg(std::streamoff(offset));
        in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(count));
        if (!in) throw std::runtime_error("RVZ file is truncated");
        return bytes;
    }
};

std::vector<std::uint8_t> zstd_decompress(const std::vector<std::uint8_t>& input, std::size_t expected) {
    std::vector<std::uint8_t> output(expected);
    const std::size_t done = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(done)) throw std::runtime_error(std::string("RVZ Zstandard data: ") + ZSTD_getErrorName(done));
    output.resize(done);
    return output;
}

// The padding generator state (lagged Fibonacci, f = xor, j = 32, k = 521).
class Junk {
public:
    explicit Junk(std::span<const std::uint8_t> seed) {
        for (std::size_t i = 0; i < 17; ++i) words_[i] = be32(seed.data() + i * 4);
        for (std::size_t i = 17; i < 521; ++i) words_[i] = (words_[i - 17] << 23) ^ (words_[i - 16] >> 9) ^ words_[i - 1];
        for (int i = 0; i < 4; ++i) advance();
    }
    void skip(std::uint64_t bytes) {
        while (bytes > 0) {
            const std::uint64_t left = 521 * 4 - position_;
            const std::uint64_t step = std::min(bytes, left);
            position_ += std::size_t(step);
            bytes -= step;
            if (position_ == 521 * 4) {
                advance();
                position_ = 0;
            }
        }
    }
    void output(std::span<std::uint8_t> out) {
        for (auto& byte : out) {
            const std::uint32_t word = words_[position_ / 4];
            // Bytes of a word: >> 24, >> 18 (not 16), >> 8, >> 0.
            static constexpr int shifts[4] = {24, 18, 8, 0};
            byte = std::uint8_t(word >> shifts[position_ % 4]);
            if (++position_ == 521 * 4) {
                advance();
                position_ = 0;
            }
        }
    }

private:
    void advance() {
        for (std::size_t i = 0; i < 32; ++i) words_[i] ^= words_[i + 521 - 32];
        for (std::size_t i = 32; i < 521; ++i) words_[i] ^= words_[i - 32];
    }
    std::array<std::uint32_t, 521> words_{};
    std::size_t position_{};
};

// RVZ packing: runs of literal bytes and generated padding, the padding
// aligned to the 32 KiB blocks of the disc.
void unpack(const std::vector<std::uint8_t>& packed, std::uint64_t disc_offset, std::vector<std::uint8_t>& out) {
    std::size_t at = 0;
    while (at + 4 <= packed.size()) {
        std::uint32_t size = be32(&packed[at]);
        at += 4;
        if (size & 0x80000000u) {
            size &= 0x7FFFFFFFu;
            if (at + 68 > packed.size()) throw std::runtime_error("RVZ padding seed is truncated");
            Junk junk(std::span(packed).subspan(at, 68));
            at += 68;
            junk.skip((disc_offset + out.size()) % 0x8000);
            const std::size_t start = out.size();
            out.resize(start + size);
            junk.output(std::span(out).subspan(start, size));
        } else {
            if (at + size > packed.size()) throw std::runtime_error("RVZ packed data is truncated");
            out.insert(out.end(), packed.begin() + std::ptrdiff_t(at), packed.begin() + std::ptrdiff_t(at + size));
            at += size;
        }
    }
}

} // namespace

void rvz_junk(std::span<const std::uint8_t> seed68, std::uint64_t skip, std::span<std::uint8_t> out) {
    Junk junk(seed68);
    junk.skip(skip);
    junk.output(out);
}

bool is_rvz(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[4]{};
    in.read(magic, 4);
    return in && magic[0] == 'R' && magic[1] == 'V' && magic[2] == 'Z' && magic[3] == 1;
}

std::string convert_rvz(const std::filesystem::path& rvz, const std::filesystem::path& iso, std::atomic<double>* progress,
                        const std::atomic<bool>* cancel) {
    try {
        Reader file;
        file.in.open(rvz, std::ios::binary);
        if (!file.in) return "Cannot open " + rvz.string();
        file.size = std::filesystem::file_size(rvz);
        const auto head = file.read(0, 0x48);
        if (head[0] != 'R' || head[1] != 'V' || head[2] != 'Z' || head[3] != 1) return "Not an RVZ file";
        const std::uint32_t disc_size = be32(&head[0x0C]);
        const std::uint64_t iso_size = be64(&head[0x24]);
        const auto disc = file.read(0x48, std::max<std::uint32_t>(disc_size, 0xDC));
        const std::uint32_t disc_type = be32(&disc[0]), compression = be32(&disc[4]), chunk_size = be32(&disc[0x0C]);
        if (disc_type != 1) return "Only GameCube RVZ images are supported";
        if (compression != 0 && compression != 5)
            return "This RVZ uses bzip2 or LZMA compression; convert it again in Dolphin with Zstandard (the default) or to ISO";
        if (chunk_size < 0x8000 || chunk_size > 0x10000000) return "RVZ chunk size is invalid";
        const std::uint8_t* dhead = &disc[0x10];
        // wia_disc_t: dhead at 0x10, n_part 0x90, part_t_size 0x94, part_off
        // 0x98, part_hash 0xA0, then the raw data and group tables.
        const std::uint32_t n_raw = be32(&disc[0xB4]);
        const std::uint64_t raw_off = be64(&disc[0xB8]);
        const std::uint32_t raw_size = be32(&disc[0xC0]);
        const std::uint32_t n_groups = be32(&disc[0xC4]);
        const std::uint64_t group_off = be64(&disc[0xC8]);
        const std::uint32_t group_size = be32(&disc[0xD0]);
        auto table = [&](std::uint64_t offset, std::uint32_t stored, std::size_t expected) {
            auto bytes = file.read(offset, stored);
            if (compression == 5) bytes = zstd_decompress(bytes, expected);
            if (bytes.size() < expected) throw std::runtime_error("RVZ table is truncated");
            return bytes;
        };
        const auto raws = table(raw_off, raw_size, std::size_t(n_raw) * 0x18);
        const auto groups = table(group_off, group_size, std::size_t(n_groups) * 12);

        std::error_code error;
        if (iso.has_parent_path()) std::filesystem::create_directories(iso.parent_path(), error);
        const auto partial = std::filesystem::path(iso.string() + ".part");
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) return "Cannot write " + partial.string();
        auto write_at = [&](std::uint64_t offset, const std::uint8_t* data, std::size_t count) {
            out.seekp(std::streamoff(offset));
            out.write(reinterpret_cast<const char*>(data), std::streamsize(count));
            if (!out) throw std::runtime_error("Cannot write the disc image (disk full?)");
        };
        // Full size first, zeroes where no data is stored.
        if (iso_size) {
            const std::uint8_t zero = 0;
            write_at(iso_size - 1, &zero, 1);
        }
        std::uint64_t written = 0;
        for (std::uint32_t r = 0; r < n_raw; ++r) {
            const std::uint8_t* e = &raws[std::size_t(r) * 0x18];
            std::uint64_t start = be64(e), size = be64(e + 8);
            const std::uint32_t first_group = be32(e + 16), count = be32(e + 20);
            // The first entry starts at 0x80 but holds data from 0 (rounded
            // down to the 32 KiB block).
            const std::uint64_t aligned = start / 0x8000 * 0x8000;
            size += start - aligned;
            start = aligned;
            for (std::uint32_t g = 0; g < count; ++g) {
                if (cancel && cancel->load()) {
                    out.close();
                    std::filesystem::remove(partial, error);
                    return "Cancelled";
                }
                const std::uint32_t index = first_group + g;
                if (index >= n_groups) throw std::runtime_error("RVZ group index is out of range");
                const std::uint8_t* ge = &groups[std::size_t(index) * 12];
                const std::uint64_t data_off = std::uint64_t(be32(ge)) * 4;
                const std::uint32_t data_size = be32(ge + 4), packed_size = be32(ge + 8);
                const std::uint64_t group_start = start + std::uint64_t(g) * chunk_size;
                if (group_start >= start + size) break;
                const std::size_t length = std::size_t(std::min<std::uint64_t>(chunk_size, start + size - group_start));
                std::vector<std::uint8_t> data;
                const std::uint32_t stored = data_size & 0x7FFFFFFFu;
                if (stored == 0) {
                    data.assign(length, 0);
                } else {
                    data = file.read(data_off, stored);
                    if ((data_size & 0x80000000u) && compression == 5) data = zstd_decompress(data, packed_size ? packed_size : length);
                    if (packed_size) {
                        std::vector<std::uint8_t> unpacked;
                        unpacked.reserve(length);
                        unpack(data, group_start, unpacked);
                        data = std::move(unpacked);
                    }
                }
                data.resize(length);
                write_at(group_start, data.data(), data.size());
                written += length;
                if (progress && iso_size) progress->store(double(written) / double(iso_size));
            }
        }
        write_at(0, dhead, 0x80);
        out.close();
        if (!out) return "Cannot finish the disc image";
        std::filesystem::rename(partial, iso, error);
        if (error) return "Cannot rename the finished disc image: " + error.message();
        if (progress) progress->store(1.0);
        return {};
    } catch (const std::exception& problem) {
        return problem.what();
    }
}

} // namespace dw4::game
