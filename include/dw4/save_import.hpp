#pragma once

// Importing GameCube saves of Digimon World 4 (docs/SAVES.md "Importing GC
// saves"): the containers other tools write, detected by their magic and
// size, are unpacked to GCI bytes (the 64-byte directory entry and the
// 90112-byte NDMWSaveData file):
// - .gci: Dolphin / GCMM / Swiss export, the directory entry then the blocks;
// - .gcs: GameShark / PSO save ("GCSAVE"), a 0x110-byte header, the entry at
//   0x110 (its block count is not reliable), the blocks from 0x150;
// - .sav: MaxDrive / Action Replay ("DATELGC_SAVE"), a 0x80-byte header, the
//   entry at 0x80 with the bytes of each 16-bit pair at entry +0x06 and
//   +0x2C..+0x3F swapped, the blocks from 0xC0;
// - raw memory card images (.raw / .gcp: Dolphin, Nintendont, card dumps):
//   header block 0, directories in blocks 1 / 2 and block maps in 3 / 4 (the
//   checked copy with the higher update counter wins), the file's blocks
//   follow the block map from the entry's first block;
// - a GCI folder (Dolphin's Card A / Card B or the GC/USA root above them).
// Boxes then go into the current save as byte copies of their 8 KB box
// buffers, both data copies rewritten with a new checksum.
#include "dw4/save_data.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::save {

// A save found in an import source.
struct ImportSource {
    std::string container;          // "GCI", "GCS", "SAV", "memory card image", "GCI folder"
    std::string detail;             // e.g. the card size and directory used
    std::vector<std::uint8_t> gci;  // entry (as written by the source) + the 90112-byte file
    std::filesystem::path file;     // the file it came from (a GCI folder: the .gci)
};

// The save in `bytes`, whatever the container (magic, then the size: a
// multiple of 8 KB is a card image, else a GCI). On failure `error` says why.
std::optional<ImportSource> parse_import(std::span<const std::uint8_t> bytes, std::string& error);
// A file of any container above, or a GCI folder.
std::optional<ImportSource> load_import(const std::filesystem::path& path, std::string& error);

// What a save box shows in the import preview.
struct BoxSummary {
    bool used{};
    std::uint32_t unique{};
    std::string name;    // the player name (ASCII, '?' for other characters)
    std::string digimon; // DIGIMONNAME, e.g. p_dorumon
    std::int32_t level{}, hp{}, max_hp{}, bits{};
    int phase{}; // story phase 1-7 (flags 2-6, 86 as work/campaign/tools/gcisum.py)
    int forms{}; // forms unlocked (BASE_ISUSE)
};
// The four boxes of the first data copy that passes its check; none when
// neither does. `gci`: entry + file.
std::optional<std::array<BoxSummary, kRecordsPerCopy>> summarize_save(std::span<const std::uint8_t> gci);
// "Agumon", "Imperialdramon FM", ... for a DIGIMONNAME (else the name itself).
std::string form_label(std::string_view model);
// "Tamer - Dorumon Lv 88, 2059734 bits, story phase 2, 3 forms" / "(empty)".
std::string describe_box(const BoxSummary& box);
// The save's time from its directory entry (+0x28, the console clock's
// seconds since 2000-01-01; the records keep no time of their own) as
// "YYYY-MM-DD HH:MM", "" for 0.
std::string save_time_text(std::span<const std::uint8_t> gci);

// One box going from the source into the target (boxes 0..3).
struct BoxMove {
    int from{}, to{};
    bool operator==(const BoxMove&) const = default;
};
// The boxes as players count them (1..4, as the data select shows them):
// "all" or "" (the whole file: no moves), "1,3" (boxes 1 and 3 into the
// same boxes), "1>2,4>1" (box 1 into box 2, box 4 into box 1). A target box
// may appear once.
std::optional<std::vector<BoxMove>> parse_box_moves(std::string_view text);

// The save after the import, as GCI bytes with a fresh directory entry (the
// one the port's save room writes, time `modified`): `moves` empty takes the
// source's file unchanged; else the target's file (or, without a target or
// with a broken one, the source's header and empty boxes) with each moved
// box's 8 KB buffer copied from the source's checked data copy, then both
// copies written with the new checksum. Fails when the source has no data
// copy that passes its check or a box number is out of range.
std::optional<std::vector<std::uint8_t>> merge_import(const std::vector<std::uint8_t>* target, std::span<const std::uint8_t> source,
                                                      std::span<const BoxMove> moves, std::uint32_t modified, std::string& error);

// GameCube memory card checksum pair (sum of the big-endian 16-bit words and
// of their complements; 0xFFFF counts as 0), for card images.
std::array<std::uint16_t, 2> card_checksum(std::span<const std::uint8_t> bytes);

} // namespace dw4::save
