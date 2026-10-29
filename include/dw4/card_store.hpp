#pragma once

// Where the memory card slots keep the Digimon World 4 save (docs/SAVES.md
// "Where the save lives"). The game's card requests (SAVEUTILITY) only see a
// slot's DW4 file (GCI bytes: the 64-byte directory entry and the 90112-byte
// NDMWSaveData file) and the space the card's files take. Two backends:
// - folder: Dolphin's GCI folder cards, <root>/Card A and <root>/Card B, each
//   *.gci file one card file (--card-dir, DW4_CARD_DIR, test runs, the
//   campaign tools, a Dolphin GC/USA folder, the online session copy);
// - single file: one file per slot holding the GCI bytes unchanged
//   (saves/savedata.dat, saves/savedata_b.dat; the players' default). Every
//   write keeps the previous file as <file>.bak.
// Writes go to <file>.tmp first and replace the file by a rename.
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::save {

// Dolphin's name of the save in a GCI folder (maker, game, file name).
inline constexpr std::string_view kSaveFileName = "B2-GDJE-NDMWSaveData.gci";

// The blocks and directory entries the files of a card take.
struct CardUsage {
    std::uint32_t blocks{};
    std::uint32_t files{};
};

class CardStore {
public:
    enum class Kind : std::uint8_t { folder, single_file };
    CardStore() = default;
    static CardStore folder(std::filesystem::path root);
    static CardStore single_file(std::filesystem::path slot_a, std::filesystem::path slot_b);

    Kind kind() const { return kind_; }
    // Folder: the GCI root holding Card A / Card B.
    const std::filesystem::path& root() const { return root_; }
    // Folder: <root>/Card A|B; single file: the folder of the slot's file.
    std::filesystem::path slot_folder(int slot) const;
    // The slot's DW4 save file.
    std::filesystem::path save_path(int slot) const;
    // Folder: the slot's folder exists; single file: the slot's file exists.
    bool slot_exists(int slot) const;
    // The slot's save as GCI bytes; none when it is missing or shorter than
    // a directory entry and the 90112-byte file.
    std::optional<std::vector<std::uint8_t>> read(int slot) const;
    // Replaces the slot's save (folder: creates the slot's folder).
    bool write(int slot, std::span<const std::uint8_t> gci, std::string* error = nullptr) const;
    // Folder: the block counts (GCI entry +0x38) and number of every *.gci in
    // the slot's folder; single file: the save's blocks and one file.
    CardUsage usage(int slot) const;
    // Where the slot's save lives, for the menus and logs.
    std::string describe(int slot) const;
    bool operator==(const CardStore&) const = default;

private:
    Kind kind_ = Kind::folder;
    std::filesystem::path root_ = "saves/GC/USA";
    std::array<std::filesystem::path, 2> files_{};
};

// A whole file; none when it cannot be read.
std::optional<std::vector<std::uint8_t>> read_whole_file(const std::filesystem::path& path);
// Writes <path>.tmp (flushed to the disk, closed, then read back and compared)
// and renames it over `path`; with `keep_backup` an existing file is first
// copied to <path>.bak, unless `backup_check` rejects it (a damaged file
// never replaces the last good backup). Creates the parent folder. A failed
// write leaves `path` and the backup as they were.
using FileCheck = bool (*)(std::span<const std::uint8_t> bytes);
bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, bool keep_backup,
                       std::string* error = nullptr, FileCheck backup_check = nullptr);
// GCI bytes of a DW4 save: a directory entry and the 90112-byte file with a
// data copy that passes its checksum and version check (the entry's names
// are not checked).
bool valid_save_gci(std::span<const std::uint8_t> gci);

// Moving an old GCI folder save into the single-file store: the GCI is
// copied to `target` when `target` does not exist yet and the GCI holds a
// valid save. The old file always stays where it is.
enum class Migration { copied, target_exists, no_source, invalid_source, failed };
Migration migrate_gci(const std::filesystem::path& legacy_gci, const std::filesystem::path& target, std::string* error = nullptr);
const char* migration_text(Migration result);

// The folder for backups made before an import: <folder of the save>/backups
// (single file), <root>/backups (folder).
std::filesystem::path backup_folder(const CardStore& store);
// Copies the slot's save into `folder` as <name>-YYYYMMDD-HHMMSS<extension>
// (local time, -2, -3 ... when taken); none when the slot has no save or the
// copy failed (`error` tells which).
std::optional<std::filesystem::path> backup_save(const CardStore& store, int slot, const std::filesystem::path& folder,
                                                 std::string* error = nullptr);

} // namespace dw4::save
