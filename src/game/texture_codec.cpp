#include "texture_codec.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace dw4::game {
namespace {

constexpr std::uint32_t kMaxDimension = 16384;
constexpr std::uint32_t kMaxLevels = 15;

std::uint32_t u32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v);
    p[1] = std::uint8_t(v >> 8);
    p[2] = std::uint8_t(v >> 16);
    p[3] = std::uint8_t(v >> 24);
}
constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
    return std::uint32_t(std::uint8_t(a)) | std::uint32_t(std::uint8_t(b)) << 8 | std::uint32_t(std::uint8_t(c)) << 16 |
           std::uint32_t(std::uint8_t(d)) << 24;
}

// ---------------------------------------------------------------- BC7 tables
// Khronos Data Format Specification 1.3, section 21 (BPTC): the subset of
// each texel for the 64 two-subset and 64 three-subset partitions, the
// anchor texels of the second and third subsets, and the interpolation
// weights.
constexpr std::uint8_t kPartition2[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1}, {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1}, {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}, {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0}, {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}, {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0}, {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0}, {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1}, {0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0}, {0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0}, {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1}, {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0}, {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0}, {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1},
};

constexpr std::uint8_t kPartition3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2}, {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2}, {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2}, {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0}, {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0}, {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2}, {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1}, {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2}, {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0}, {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0}, {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1}, {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1}, {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1}, {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1}, {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2}, {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2}, {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2}, {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1}, {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0},
};

constexpr std::uint8_t kAnchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,  8, 2,  2, 8,  8,  15, 2,  8,  2,  2,  8,  8, 2, 2,
    15, 15, 6,  8,  2,  8,  15, 15, 2,  8,  2,  2,  2,  15, 15, 6,  6,  2,  6, 8,  15, 15, 2, 2,  15, 15, 15, 15, 15, 2, 2, 15,
};
constexpr std::uint8_t kAnchor3a[64] = {
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
};
constexpr std::uint8_t kAnchor3b[64] = {
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
};

constexpr std::uint8_t kWeights2[4] = {0, 21, 43, 64};
constexpr std::uint8_t kWeights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::uint8_t kWeights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

const std::uint8_t* weights(int bits) { return bits == 2 ? kWeights2 : bits == 3 ? kWeights3 : kWeights4; }

struct Bc7Mode {
    int subsets, partition_bits, rotation_bits, selection_bits, color_bits, alpha_bits, endpoint_pbits, shared_pbits, index_bits,
        index2_bits;
};
constexpr Bc7Mode kModes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, {2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, {3, 6, 0, 0, 5, 0, 0, 0, 2, 0}, {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, {1, 0, 2, 0, 7, 8, 0, 0, 2, 2}, {1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
};

// The 128 block bits, least significant first.
struct BlockBits {
    std::uint64_t lo{}, hi{};
    unsigned pos{};
    explicit BlockBits(const std::uint8_t* block) {
        for (int i = 7; i >= 0; --i) lo = lo << 8 | block[i];
        for (int i = 15; i >= 8; --i) hi = hi << 8 | block[i];
    }
    unsigned get(unsigned count) {
        if (count == 0) return 0;
        const std::uint64_t mask = (std::uint64_t(1) << count) - 1;
        std::uint64_t value;
        if (pos >= 64) value = hi >> (pos - 64);
        else if (pos + count <= 64) value = lo >> pos;
        else value = (lo >> pos) | (hi << (64 - pos));
        pos += count;
        return unsigned(value & mask);
    }
};

// A value of `bits` bits widened to 8 by repeating its top bits.
std::uint8_t widen(unsigned value, int bits) {
    unsigned v = value << (8 - bits);
    v |= v >> bits;
    return std::uint8_t(v);
}

std::uint8_t interpolate(std::uint8_t e0, std::uint8_t e1, unsigned weight) {
    return std::uint8_t(((64 - weight) * e0 + weight * e1 + 32) >> 6);
}

// Writes a decoded 4x4 block (64 bytes) into the picture, cropped.
void store_block(const std::uint8_t* block, std::uint32_t bx, std::uint32_t by, std::uint32_t width, std::uint32_t height,
                 std::uint8_t* rgba) {
    for (std::uint32_t y = 0; y < 4; ++y) {
        const std::uint32_t py = by * 4 + y;
        if (py >= height) break;
        for (std::uint32_t x = 0; x < 4; ++x) {
            const std::uint32_t px = bx * 4 + x;
            if (px >= width) break;
            std::memcpy(rgba + (std::size_t(py) * width + px) * 4, block + (y * 4 + x) * 4, 4);
        }
    }
}

// BC1 colour block (8 bytes) into 16 RGBA texels; `four_colour` forces the
// four-colour mode (BC2 / BC3 colour blocks).
void decode_colour_block(const std::uint8_t* p, std::uint8_t* out, bool four_colour) {
    const unsigned c0 = unsigned(p[0]) | unsigned(p[1]) << 8;
    const unsigned c1 = unsigned(p[2]) | unsigned(p[3]) << 8;
    std::uint8_t palette[4][4];
    auto expand = [](unsigned c, std::uint8_t* rgb) {
        const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        rgb[0] = std::uint8_t(r << 3 | r >> 2);
        rgb[1] = std::uint8_t(g << 2 | g >> 4);
        rgb[2] = std::uint8_t(b << 3 | b >> 2);
    };
    expand(c0, palette[0]);
    expand(c1, palette[1]);
    palette[0][3] = palette[1][3] = 255;
    if (four_colour || c0 > c1) {
        for (int c = 0; c < 3; ++c) {
            palette[2][c] = std::uint8_t((2 * palette[0][c] + palette[1][c] + 1) / 3);
            palette[3][c] = std::uint8_t((palette[0][c] + 2 * palette[1][c] + 1) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int c = 0; c < 3; ++c) palette[2][c] = std::uint8_t((palette[0][c] + palette[1][c] + 1) / 2);
        palette[2][3] = 255;
        palette[3][0] = palette[3][1] = palette[3][2] = palette[3][3] = 0;
    }
    const std::uint32_t selectors = u32(p + 4);
    for (int i = 0; i < 16; ++i) std::memcpy(out + i * 4, palette[(selectors >> (2 * i)) & 3], 4);
}

// BC3 alpha block (8 bytes) into the alpha of 16 texels.
void decode_alpha_block(const std::uint8_t* p, std::uint8_t* out) {
    const unsigned a0 = p[0], a1 = p[1];
    std::uint8_t values[8];
    values[0] = std::uint8_t(a0);
    values[1] = std::uint8_t(a1);
    if (a0 > a1) {
        for (unsigned i = 1; i < 7; ++i) values[i + 1] = std::uint8_t(((7 - i) * a0 + i * a1 + 3) / 7);
    } else {
        for (unsigned i = 1; i < 5; ++i) values[i + 1] = std::uint8_t(((5 - i) * a0 + i * a1 + 2) / 5);
        values[6] = 0;
        values[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 7; i >= 2; --i) bits = bits << 8 | p[i];
    for (int i = 0; i < 16; ++i) out[i * 4 + 3] = values[(bits >> (3 * i)) & 7];
}

template <class Block>
bool decode_blocks(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::size_t block_size,
                   std::vector<std::uint8_t>& rgba, Block&& block) {
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) return false;
    const std::uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    if (blocks.size() < std::size_t(bw) * bh * block_size) return false;
    rgba.assign(std::size_t(width) * height * 4, 0);
    std::uint8_t texels[64];
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            block(blocks.data() + (std::size_t(by) * bw + bx) * block_size, texels);
            store_block(texels, bx, by, width, height, rgba.data());
        }
    return true;
}

} // namespace

const char* encoding_name(TextureEncoding encoding) {
    switch (encoding) {
    case TextureEncoding::rgba8: return "rgba8";
    case TextureEncoding::bc1: return "bc1";
    case TextureEncoding::bc3: return "bc3";
    case TextureEncoding::bc7: return "bc7";
    case TextureEncoding::png: return "png";
    default: return "unknown";
    }
}

TextureEncoding parse_encoding(std::string_view text) {
    std::string t;
    for (const char c : text) t.push_back(char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    if (t == "rgba8") return TextureEncoding::rgba8;
    if (t == "bc1") return TextureEncoding::bc1;
    if (t == "bc3") return TextureEncoding::bc3;
    if (t == "bc7") return TextureEncoding::bc7;
    if (t == "png") return TextureEncoding::png;
    return TextureEncoding::unknown;
}

bool block_compressed(TextureEncoding encoding) {
    return encoding == TextureEncoding::bc1 || encoding == TextureEncoding::bc3 || encoding == TextureEncoding::bc7;
}

std::size_t level_bytes(TextureEncoding encoding, std::uint32_t width, std::uint32_t height) {
    const std::size_t blocks = std::size_t((width + 3) / 4) * ((height + 3) / 4);
    switch (encoding) {
    case TextureEncoding::rgba8: return std::size_t(width) * height * 4;
    case TextureEncoding::bc1: return blocks * 8;
    case TextureEncoding::bc3:
    case TextureEncoding::bc7: return blocks * 16;
    default: return 0;
    }
}

std::optional<DdsInfo> parse_dds_header(std::span<const std::uint8_t> header, std::size_t file_size, std::string& error) {
    if (header.size() < 128 || file_size < 128) {
        error = "shorter than a DDS header";
        return std::nullopt;
    }
    const std::uint8_t* p = header.data();
    if (u32(p) != fourcc('D', 'D', 'S', ' ') || u32(p + 4) != 124 || u32(p + 76) != 32) {
        error = "not a DDS file";
        return std::nullopt;
    }
    DdsInfo info;
    info.height = u32(p + 12);
    info.width = u32(p + 16);
    const std::uint32_t depth = u32(p + 24);
    std::uint32_t level_count = std::max<std::uint32_t>(u32(p + 28), 1);
    const std::uint32_t pf_flags = u32(p + 80), four = u32(p + 84), bit_count = u32(p + 88);
    const std::uint32_t caps2 = u32(p + 112);
    if (info.width == 0 || info.height == 0 || info.width > kMaxDimension || info.height > kMaxDimension) {
        error = "dimensions outside 1..16384";
        return std::nullopt;
    }
    if ((caps2 & 0x200u) || (caps2 & 0x200000u) || depth > 1) {
        error = "not a 2D texture (cube map or volume)";
        return std::nullopt;
    }
    info.data_offset = 128;
    if ((pf_flags & 0x4u) && four == fourcc('D', 'X', '1', '0')) {
        if (header.size() < 148 || file_size < 148) {
            error = "DX10 header missing";
            return std::nullopt;
        }
        const std::uint32_t format = u32(p + 128), dimension = u32(p + 132), misc = u32(p + 136), array_size = u32(p + 140);
        if (dimension != 3 || (misc & 0x4u) || array_size > 1) {
            error = "not a single 2D texture (DX10 dimension, cube or array)";
            return std::nullopt;
        }
        switch (format) {
        case 27: case 28: case 29: info.encoding = TextureEncoding::rgba8; break;           // R8G8B8A8 typeless / unorm / srgb
        case 87: case 90: case 91: info.encoding = TextureEncoding::rgba8; info.bgra = true; break; // B8G8R8A8
        case 70: case 71: case 72: info.encoding = TextureEncoding::bc1; break;
        case 76: case 77: case 78: info.encoding = TextureEncoding::bc3; break;
        case 97: case 98: case 99: info.encoding = TextureEncoding::bc7; break;
        default:
            error = "unsupported DXGI format " + std::to_string(format);
            return std::nullopt;
        }
        info.data_offset = 148;
    } else if (pf_flags & 0x4u) {
        if (four == fourcc('D', 'X', 'T', '1')) info.encoding = TextureEncoding::bc1;
        else if (four == fourcc('D', 'X', 'T', '5')) info.encoding = TextureEncoding::bc3;
        else {
            error = "unsupported FourCC";
            return std::nullopt;
        }
    } else if ((pf_flags & 0x40u) && bit_count == 32 && (pf_flags & 0x1u)) {
        const std::uint32_t r = u32(p + 92), g = u32(p + 96), b = u32(p + 100), a = u32(p + 104);
        if (r == 0xffu && g == 0xff00u && b == 0xff0000u && a == 0xff000000u) info.encoding = TextureEncoding::rgba8;
        else if (r == 0xff0000u && g == 0xff00u && b == 0xffu && a == 0xff000000u) {
            info.encoding = TextureEncoding::rgba8;
            info.bgra = true;
        } else {
            error = "unsupported 32-bit channel masks";
            return std::nullopt;
        }
    } else {
        error = "unsupported pixel format";
        return std::nullopt;
    }
    const std::uint32_t largest = std::max(info.width, info.height);
    std::uint32_t possible = 1;
    while ((largest >> possible) > 0) ++possible;
    if (level_count > kMaxLevels || level_count > possible) {
        error = "more mip levels than the size allows";
        return std::nullopt;
    }
    std::size_t offset = info.data_offset;
    for (std::uint32_t i = 0; i < level_count; ++i) {
        TextureLevel level;
        level.width = std::max<std::uint32_t>(info.width >> i, 1);
        level.height = std::max<std::uint32_t>(info.height >> i, 1);
        level.offset = offset;
        level.size = level_bytes(info.encoding, level.width, level.height);
        if (level.size > file_size || offset > file_size - level.size) {
            error = "level " + std::to_string(i) + " runs past the end of the file";
            return std::nullopt;
        }
        offset += level.size;
        info.levels.push_back(level);
    }
    return info;
}

std::vector<std::uint8_t> make_dds(TextureEncoding encoding, std::uint32_t width, std::uint32_t height,
                                   const std::vector<std::vector<std::uint8_t>>& levels, bool dx10_header) {
    const bool dx10 = dx10_header || encoding == TextureEncoding::bc7;
    std::vector<std::uint8_t> out(dx10 ? 148 : 128, 0);
    std::uint8_t* p = out.data();
    put32(p, fourcc('D', 'D', 'S', ' '));
    put32(p + 4, 124);
    const bool compressed = block_compressed(encoding);
    put32(p + 8, 0x1u | 0x2u | 0x4u | 0x1000u | (levels.size() > 1 ? 0x20000u : 0u) | (compressed ? 0x80000u : 0x8u));
    put32(p + 12, height);
    put32(p + 16, width);
    put32(p + 20, compressed ? std::uint32_t(level_bytes(encoding, width, height)) : width * 4);
    put32(p + 28, std::uint32_t(levels.size()));
    put32(p + 76, 32);
    if (dx10) {
        put32(p + 80, 0x4u);
        put32(p + 84, fourcc('D', 'X', '1', '0'));
        const std::uint32_t format = encoding == TextureEncoding::bc7   ? 98
                                     : encoding == TextureEncoding::bc3 ? 77
                                     : encoding == TextureEncoding::bc1 ? 71
                                                                        : 28;
        put32(p + 128, format);
        put32(p + 132, 3);
        put32(p + 140, 1);
    } else if (compressed) {
        put32(p + 80, 0x4u);
        put32(p + 84, encoding == TextureEncoding::bc3 ? fourcc('D', 'X', 'T', '5') : fourcc('D', 'X', 'T', '1'));
    } else {
        put32(p + 80, 0x40u | 0x1u);
        put32(p + 88, 32);
        put32(p + 92, 0xffu);
        put32(p + 96, 0xff00u);
        put32(p + 100, 0xff0000u);
        put32(p + 104, 0xff000000u);
    }
    put32(p + 108, 0x1000u | (levels.size() > 1 ? 0x400008u : 0u));
    for (const auto& level : levels) out.insert(out.end(), level.begin(), level.end());
    return out;
}

void decode_bc7_block(const std::uint8_t* block, std::uint8_t* out) {
    int mode = 0;
    while (mode < 8 && !(block[0] & (1u << mode))) ++mode;
    if (mode == 8) {
        // Reserved mode: transparent black (the specification's choice).
        std::memset(out, 0, 64);
        return;
    }
    const Bc7Mode& m = kModes[mode];
    BlockBits bits(block);
    bits.pos = unsigned(mode + 1);
    const unsigned partition = bits.get(unsigned(m.partition_bits));
    const unsigned rotation = bits.get(unsigned(m.rotation_bits));
    const unsigned selection = bits.get(unsigned(m.selection_bits));
    unsigned raw[3][2][4]{}; // subset, endpoint, channel
    for (int c = 0; c < 3; ++c)
        for (int s = 0; s < m.subsets; ++s)
            for (int e = 0; e < 2; ++e) raw[s][e][c] = bits.get(unsigned(m.color_bits));
    if (m.alpha_bits)
        for (int s = 0; s < m.subsets; ++s)
            for (int e = 0; e < 2; ++e) raw[s][e][3] = bits.get(unsigned(m.alpha_bits));
    unsigned pbit[3][2]{};
    if (m.endpoint_pbits)
        for (int s = 0; s < m.subsets; ++s)
            for (int e = 0; e < 2; ++e) pbit[s][e] = bits.get(1);
    if (m.shared_pbits)
        for (int s = 0; s < m.subsets; ++s) pbit[s][0] = pbit[s][1] = bits.get(1);
    const bool has_pbits = m.endpoint_pbits || m.shared_pbits;
    std::uint8_t endpoint[3][2][4]{};
    for (int s = 0; s < m.subsets; ++s)
        for (int e = 0; e < 2; ++e) {
            for (int c = 0; c < 3; ++c) {
                const unsigned v = has_pbits ? (raw[s][e][c] << 1 | pbit[s][e]) : raw[s][e][c];
                endpoint[s][e][c] = widen(v, m.color_bits + (has_pbits ? 1 : 0));
            }
            if (m.alpha_bits) {
                const unsigned v = has_pbits ? (raw[s][e][3] << 1 | pbit[s][e]) : raw[s][e][3];
                endpoint[s][e][3] = widen(v, m.alpha_bits + (has_pbits ? 1 : 0));
            } else {
                endpoint[s][e][3] = 255;
            }
        }
    auto subset_of = [&](int texel) -> int {
        if (m.subsets == 2) return kPartition2[partition][texel];
        if (m.subsets == 3) return kPartition3[partition][texel];
        return 0;
    };
    auto anchor = [&](int texel) {
        if (texel == 0) return true;
        if (m.subsets == 2) return texel == kAnchor2[partition];
        if (m.subsets == 3) return texel == kAnchor3a[partition] || texel == kAnchor3b[partition];
        return false;
    };
    unsigned index[16]{}, index2[16]{};
    for (int i = 0; i < 16; ++i) index[i] = bits.get(unsigned(m.index_bits - (anchor(i) ? 1 : 0)));
    if (m.index2_bits)
        for (int i = 0; i < 16; ++i) index2[i] = bits.get(unsigned(m.index2_bits - (i == 0 ? 1 : 0)));
    for (int i = 0; i < 16; ++i) {
        const int s = subset_of(i);
        unsigned colour_weight, alpha_weight;
        if (!m.index2_bits) {
            colour_weight = alpha_weight = weights(m.index_bits)[index[i]];
        } else if (selection == 0) {
            colour_weight = weights(m.index_bits)[index[i]];
            alpha_weight = weights(m.index2_bits)[index2[i]];
        } else {
            colour_weight = weights(m.index2_bits)[index2[i]];
            alpha_weight = weights(m.index_bits)[index[i]];
        }
        std::uint8_t* texel = out + i * 4;
        for (int c = 0; c < 3; ++c) texel[c] = interpolate(endpoint[s][0][c], endpoint[s][1][c], colour_weight);
        texel[3] = interpolate(endpoint[s][0][3], endpoint[s][1][3], alpha_weight);
        if (rotation) std::swap(texel[3], texel[rotation - 1]);
    }
}

bool decode_bc1(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba) {
    return decode_blocks(blocks, width, height, 8, rgba, [](const std::uint8_t* b, std::uint8_t* out) { decode_colour_block(b, out, false); });
}

bool decode_bc3(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba) {
    return decode_blocks(blocks, width, height, 16, rgba, [](const std::uint8_t* b, std::uint8_t* out) {
        decode_colour_block(b + 8, out, true);
        decode_alpha_block(b, out);
    });
}

bool decode_bc7(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba) {
    return decode_blocks(blocks, width, height, 16, rgba, [](const std::uint8_t* b, std::uint8_t* out) { decode_bc7_block(b, out); });
}

bool decode_level(TextureEncoding encoding, std::span<const std::uint8_t> data, std::uint32_t width, std::uint32_t height, bool bgra,
                  std::vector<std::uint8_t>& rgba) {
    switch (encoding) {
    case TextureEncoding::bc1: return decode_bc1(data, width, height, rgba);
    case TextureEncoding::bc3: return decode_bc3(data, width, height, rgba);
    case TextureEncoding::bc7: return decode_bc7(data, width, height, rgba);
    case TextureEncoding::rgba8: {
        const std::size_t bytes = std::size_t(width) * height * 4;
        if (width == 0 || height == 0 || data.size() < bytes) return false;
        rgba.assign(data.begin(), data.begin() + std::ptrdiff_t(bytes));
        if (bgra)
            for (std::size_t i = 0; i < bytes; i += 4) std::swap(rgba[i], rgba[i + 2]);
        return true;
    }
    default: return false;
    }
}

AlphaKind classify_alpha(std::span<const std::uint8_t> rgba) {
    bool transparent = false;
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        const std::uint8_t a = rgba[i];
        if (a != 0 && a != 255) return AlphaKind::blend;
        if (a == 0) transparent = true;
    }
    return transparent ? AlphaKind::cutout : AlphaKind::opaque;
}

std::vector<std::uint8_t> half_level(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, AlphaKind kind) {
    const std::uint32_t w = std::max<std::uint32_t>(width / 2, 1), h = std::max<std::uint32_t>(height / 2, 1);
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4);
    if (rgba.size() < std::size_t(width) * height * 4) return out;
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
            const std::uint8_t* t[4] = {&rgba[(std::size_t(y0) * width + x0) * 4], &rgba[(std::size_t(y0) * width + x1) * 4],
                                        &rgba[(std::size_t(y1) * width + x0) * 4], &rgba[(std::size_t(y1) * width + x1) * 4]};
            unsigned alpha = 0;
            for (const auto* texel : t) alpha += texel[3];
            std::uint8_t* o = &out[(std::size_t(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c) {
                unsigned sum = 0;
                if (alpha > 0) {
                    for (const auto* texel : t) sum += unsigned(texel[c]) * texel[3];
                    o[c] = std::uint8_t((sum + alpha / 2) / alpha);
                } else {
                    for (const auto* texel : t) sum += texel[c];
                    o[c] = std::uint8_t((sum + 2) / 4);
                }
            }
            if (kind == AlphaKind::cutout) o[3] = alpha >= 510 ? 255 : 0;
            else o[3] = std::uint8_t((alpha + 2) / 4);
        }
    }
    return out;
}

std::vector<std::uint8_t> mip_chain(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, AlphaKind kind,
                                    std::vector<TextureLevel>& levels) {
    levels.clear();
    std::vector<std::uint8_t> out;
    if (width == 0 || height == 0 || rgba.size() < std::size_t(width) * height * 4) return out;
    std::size_t total = 0;
    for (std::uint32_t w = width, h = height;; w = std::max<std::uint32_t>(w / 2, 1), h = std::max<std::uint32_t>(h / 2, 1)) {
        total += std::size_t(w) * h * 4;
        if (w == 1 && h == 1) break;
    }
    out.reserve(total);
    std::uint32_t w = width, h = height;
    std::vector<std::uint8_t> level = std::move(rgba);
    level.resize(std::size_t(w) * h * 4);
    for (;;) {
        levels.push_back({w, h, out.size(), level.size()});
        out.insert(out.end(), level.begin(), level.end());
        if (w == 1 && h == 1) break;
        level = half_level(level, w, h, kind);
        w = std::max<std::uint32_t>(w / 2, 1);
        h = std::max<std::uint32_t>(h / 2, 1);
    }
    return out;
}

} // namespace dw4::game
