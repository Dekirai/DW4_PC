#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4::assets {

class AssetError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct GameCubeHeader {
    std::string game_id;
    std::string title;
    std::uint8_t disc_number{};
    std::uint8_t revision{};
    bool is_dw4_usa() const noexcept;
};

struct DolSection {
    bool executable{};
    std::size_t index{};
    std::uint32_t file_offset{};
    std::uint32_t address{};
    std::uint32_t size{};
};

struct DolInfo {
    std::uint64_t file_size{};
    std::uint32_t entry_point{};
    std::uint32_t bss_address{};
    std::uint32_t bss_size{};
    std::vector<DolSection> sections;
};

struct AfsEntry {
    std::uint32_t offset{};
    std::uint32_t size{};
    std::string name; // Empty if the archive has no valid optional filename table.
};

// Indexing reads the archive header and optional names, never the payloads.
// Read methods reopen the file and verify its size before reading.
class AfsArchive {
public:
    static AfsArchive open(const std::filesystem::path& path);
    // An archive stored at `base` (size bytes) inside a container file such
    // as a disc image.
    static AfsArchive open_region(const std::filesystem::path& container, std::uint64_t base, std::uint64_t size);
    const std::filesystem::path& path() const noexcept { return path_; }
    std::uint64_t file_size() const noexcept { return file_size_; }
    const std::vector<AfsEntry>& entries() const noexcept { return entries_; }
    bool has_names() const noexcept { return has_names_; }
    std::vector<std::uint8_t> read(std::size_t index,
        std::size_t max_bytes = 64u * 1024u * 1024u) const;
    std::vector<std::uint8_t> read_prefix(std::size_t index,
        std::size_t max_bytes = 256) const;

private:
    std::filesystem::path path_;
    std::uint64_t file_size_{};
    std::uint64_t base_{};           // archive start in the container file
    std::uint64_t container_size_{}; // container file size when indexed
    std::vector<AfsEntry> entries_;
    bool has_names_{};
};

struct BannerImage {
    std::uint32_t width{96};
    std::uint32_t height{32};
    std::vector<std::uint8_t> rgba;
};

struct ArchiveSummary {
    std::filesystem::path path;
    std::uint64_t file_size{};
    std::size_t entry_count{};
    bool has_names{};
};

struct DiscInspection {
    std::filesystem::path root;
    std::filesystem::path dol_path;
    std::filesystem::path banner_path; // Empty when opening.bnr is absent.
    GameCubeHeader header;
    DolInfo dol;
    std::vector<ArchiveSummary> archives;
    std::vector<std::string> warnings;
};

GameCubeHeader read_boot_header(const std::filesystem::path& path);
DolInfo inspect_dol(const std::filesystem::path& path);
BannerImage decode_banner(const std::filesystem::path& path);
// Supports Dolphin's sys/ + files/ layout and a flat extracted directory.
// Requires a GameCube boot.bin and main.dol; mismatched title/revision is warned.
DiscInspection inspect_extracted(const std::filesystem::path& root);

} // namespace dw4::assets
