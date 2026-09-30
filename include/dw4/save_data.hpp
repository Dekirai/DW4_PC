#pragma once

// Digimon World 4 memory card data (docs/SAVES.md): the NDMWSaveData file is
// an 8 KB header (comment, banner, icon) and two redundant 40960-byte copies
// of the game data; each copy holds a checksum, the word 4 and the 3316-byte
// character records (80364AEC) back to back. Big-endian throughout.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::save {

constexpr std::size_t kRecordSize = 3316;
constexpr std::size_t kCopySize = 40960;
constexpr std::size_t kHeaderSize = 0x2000;
constexpr std::size_t kFileSize = 90112; // 11 blocks of 8192
constexpr std::array<std::size_t, 2> kCopyOffsets{0x2000, 0xC000};
constexpr std::size_t kRecordsPerCopy = (kCopySize - 8) / kRecordSize;
constexpr std::uint32_t kCopyVersion = 4;
constexpr std::size_t kGciEntrySize = 64;

// One character (the GAMERAM player fields the save writes).
struct Record {
    bool blank = true;                // never written: stays all zero on the card
    bool used{};                      // ISUSE
    std::uint32_t unique{};           // UNIQUE
    std::string digimon;              // DIGIMONNAME, e.g. p_agumon
    std::u16string name;              // PLAYERNAME as stored (full-width), at most 8
    std::int32_t level{}, hp{}, max_hp{}, mp{}, max_mp{}, xdata{}, bits{};
    std::array<std::int32_t, 36> devices{};  // DEVICEFOLDER packed words, -1 empty
    std::array<std::int32_t, 12> disks{};    // DISKFOLDER packed words, -1 empty
    std::array<std::uint8_t, 52> cards{};    // CARDLIST
    std::array<std::int32_t, 96> bank{};     // BANKDEVICE packed words, -1 empty
    std::int32_t bank_bits{};                // BANKBIT
    std::array<std::int32_t, 3> power_socket{}; // DEVICEFOLDER indices, -1 none
    std::array<std::int32_t, 5> power_pocket{};
    std::int32_t main_slot{};
    std::array<std::int32_t, 5> main_pocket{};
    std::int32_t sub_slot{};
    std::array<std::uint8_t, 1024> flags{};      // BASE_FLAG
    std::array<std::uint8_t, 12> flag_folder{};  // BASE_FLAGFOLDER
    std::array<std::int32_t, 16> counters{};     // BASE_COUNTER
    std::array<std::uint8_t, 16> base_isuse{};
    std::array<std::int32_t, 16> base_level{}, base_exp{};
    std::array<std::array<std::int32_t, 9>, 16> base_skill{};
    std::array<std::array<std::int32_t, 11>, 16> base_upcnt{};
};

std::array<std::uint8_t, kRecordSize> encode(const Record& record);
// Throws std::runtime_error when fewer than kRecordSize bytes are given.
Record decode(std::span<const std::uint8_t> bytes);

// PLAYERNAME uses the full-width forms (U+FF01..U+FF5E for '!'..'~').
std::u16string name_to_card(std::string_view ascii);
std::string name_from_card(std::u16string_view card);

// Sum of the big-endian words after the first (8031FB98).
std::uint32_t checksum(std::span<const std::uint8_t> copy);

// The whole file from its 8 KB header and up to kRecordsPerCopy records.
std::vector<std::uint8_t> write_file(std::span<const std::uint8_t> header, std::span<const Record> records);
// The records of the first copy whose checksum and version hold.
std::optional<std::vector<Record>> read_file(std::span<const std::uint8_t> file);

// Dolphin's GCI folder format: a 64-byte directory entry before the file.
struct GciEntry {
    std::array<char, 4> game{'G', 'D', 'J', 'E'};
    std::array<char, 2> maker{'B', '2'};
    std::uint8_t banner_format = 1;
    std::string file_name = "NDMWSaveData";
    std::uint32_t modified{}; // seconds since 2000-01-01
    std::uint32_t image_offset = 0x40;
    std::uint16_t icon_format = 1, icon_speed = 2;
    std::uint8_t permissions = 4, copies{};
    std::uint16_t first_block = 5, blocks = 11;
    std::uint32_t comment_offset{};
};
std::array<std::uint8_t, kGciEntrySize> encode_gci_entry(const GciEntry& entry);
GciEntry decode_gci_entry(std::span<const std::uint8_t> bytes);

} // namespace dw4::save
