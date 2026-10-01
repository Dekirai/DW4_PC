#pragma once

// RVZ disc images (Dolphin's compressed format, docs/WiaAndRvz.md in the
// Dolphin repository) of GameCube discs, converted once into a plain disc
// image that the runtime reads. Groups stored uncompressed or with
// Zstandard (Dolphin's default) are supported, including RVZ packing of the
// pseudorandom padding; bzip2 / LZMA groups and Wii discs are not.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace dw4::game {

// True when the file starts with "RVZ\x01".
bool is_rvz(const std::filesystem::path& path);

// Writes the disc image; progress (0..1) is updated while it runs and
// cancel stops it. Returns an empty string on success, else the problem.
std::string convert_rvz(const std::filesystem::path& rvz, const std::filesystem::path& iso, std::atomic<double>* progress,
                        const std::atomic<bool>* cancel);

// The padding generator (lagged Fibonacci, j = 32, k = 521) seeded with 17
// big-endian words, its output started `skip` bytes in. Exposed for tests.
void rvz_junk(std::span<const std::uint8_t> seed68, std::uint64_t skip, std::span<std::uint8_t> out);

} // namespace dw4::game
