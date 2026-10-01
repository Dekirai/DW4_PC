#pragma once

// GameCube disc images (.iso / .gcm, uncompressed): the boot header and the
// file system table, so the game files can be read straight from the disc
// image instead of an extraction. Compressed images (RVZ, WIA, CISO, NKit)
// are not read; Dolphin converts them back to ISO.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {

struct DiscFile {
    std::string path; // "dir/name" below the disc root, as stored
    std::uint64_t offset{};
    std::uint64_t size{};
};

class DiscImage {
public:
    // Parses the header (game id at 0, FST offset / size at 0x424 / 0x428)
    // and the FST; throws for files that are not GameCube disc images.
    static DiscImage open(const std::filesystem::path& path);
    // Same, from the header and FST bytes (tests and tools).
    static DiscImage parse(std::span<const std::uint8_t> header, std::span<const std::uint8_t> fst,
                           std::uint64_t image_size);
    // True when the file starts like a GameCube disc (magic 0xC2339F3D at 0x1C).
    static bool looks_like_disc(const std::filesystem::path& path);

    const std::filesystem::path& path() const noexcept { return path_; }
    const std::string& game_id() const noexcept { return game_id_; } // e.g. "GDJEB2"
    int disc_number() const noexcept { return disc_number_; }
    int revision() const noexcept { return revision_; }
    std::uint64_t dol_offset() const noexcept { return dol_offset_; }
    const std::vector<DiscFile>& files() const noexcept { return files_; }
    // Case-insensitive path lookup ("ndmw.igb", "dir/file").
    const DiscFile* find(std::string_view path) const noexcept;
    std::vector<std::uint8_t> read(const DiscFile& file, std::size_t max_bytes = 64u * 1024u * 1024u) const;

private:
    std::filesystem::path path_;
    std::string game_id_;
    int disc_number_{}, revision_{};
    std::uint64_t dol_offset_{};
    std::uint64_t image_size_{};
    std::vector<DiscFile> files_;
};

} // namespace dw4
