#pragma once

// Memory card access as the original's SAVEUTILITY observes it.
//
// SAVEUTILITY (beSaveUtil, commands 80323D20, per-frame update 80323538)
// runs one request at a time as a beSv*Api object (vtables 804E0BEC ..
// 804E0F1C: +0x5C start, +0x60 one step per frame until it reports done,
// +0x64 finish into the beSvDeliver record). Every request mounts the card
// first (8031FC40 / 8031FCF0, one step per frame: CARDProbeEx,
// CARDMountAsync, CARDGetResultCode until the mount ends, CARDCheck), works
// on the file (CARDOpen, CARDFreeBlocks, CARDCreateAsync, CARDDeleteAsync,
// CARDReadAsync, CARDWriteAsync, CARDGetStatus / CARDSetStatus, CARDClose)
// and unmounts (8031FF98).
//
// The asynchronous CARD calls end after the EXI transfers they need, which
// Dolphin times (Source/Core/Core/HW/EXI/EXI_DeviceMemoryCard.cpp): a DMA
// read takes size * (486 MHz / 512 KiB/s) cycles, a page program the DMA of
// its 128 bytes at 96.125 KiB/s plus 5000 cycles until the card reports the
// command done, a sector erase 5000 cycles; immediate transfers (commands,
// status) take no time. The CARD library reads 512-byte segments
// (__CARDReadSegment), erases a sector before it programs its 64 pages
// (CARDWrite, __CARDUpdateDir, __CARDUpdateFatBlock) and mounts by reading
// the five 8 KiB system blocks.
//
// CardChannel is the CARD control block of a slot, CardRequest one
// beSv*Api object. Both run on the caller's clock (CPU ticks of 486 MHz,
// see DiscDrive); the caller steps a request once per frame.
#include <cstdint>

namespace dw4 {

namespace card {
constexpr std::int64_t kTicksPerSecond = 486000000;
constexpr std::int64_t kReadByteTicks = kTicksPerSecond / (512 * 1024);                     // 926
constexpr std::int64_t kWriteByteTicks = kTicksPerSecond / static_cast<std::int64_t>(96.125 * 1024); // 4937
constexpr std::int64_t kCommandTicks = 5000; // CmdDoneLater after an erase or a page program
constexpr std::uint32_t kSegment = 512;      // CARD_SEG_SIZE
constexpr std::uint32_t kPage = 128;         // CARD_PAGE_SIZE
constexpr std::uint32_t kSectorSize = 8192;  // card ID 0xC221
constexpr std::uint32_t kSystemBlocks = 5;   // header, 2 directories, 2 block maps
constexpr std::uint32_t kMaxFiles = 127;     // CARD_MAX_FILE
// Erase one sector and program its 64 pages.
constexpr std::int64_t kSectorTicks = kCommandTicks + (kSectorSize / kPage) * (kPage * kWriteByteTicks + kCommandTicks);

// CARD result codes.
enum Result : int {
    kReady = 0,
    kBusy = -1,
    kWrongDevice = -2,
    kNoCard = -3,
    kNoFile = -4,
    kIoError = -5,
    kBroken = -6,
    kExist = -7,
    kNoEntry = -8,
    kInsSpace = -9,
    kEncoding = -13,
    kFatal = -128,
};

// Transfer time of a read of `bytes` (whole 512-byte segments).
constexpr std::int64_t read_ticks(std::uint32_t bytes) { return std::int64_t((bytes + kSegment - 1) / kSegment) * kSegment * kReadByteTicks; }
// Sectors programmed for a write of `bytes`, then the directory (WriteCallback
// ends in __CARDUpdateDir).
constexpr std::int64_t write_ticks(std::uint32_t bytes) { return std::int64_t((bytes + kSectorSize - 1) / kSectorSize + 1) * kSectorTicks; }
constexpr std::int64_t kMountTicks = read_ticks(kSystemBlocks * kSectorSize);
constexpr std::int64_t kCreateTicks = 2 * kSectorTicks; // block map, then the directory
constexpr std::int64_t kDeleteTicks = 2 * kSectorTicks; // directory, then the block map
constexpr std::int64_t kStatusTicks = kSectorTicks;     // __CARDUpdateDir
constexpr std::int64_t kFormatTicks = kSystemBlocks * kSectorTicks;
} // namespace card

// The card in a slot as a request sees it when it starts.
struct CardSlot {
    bool present{};                  // a memory card in the slot
    bool formatted = true;           // false: the mount and CARDCheck report it broken
    bool file{};                     // the game's file exists
    bool data_valid = true;          // a data copy passes its check (READ_MEDIA case 4)
    std::uint32_t image_offset = 64; // CARDStat iconAddr of the file (GCI entry +0x2C)
    std::uint32_t free_blocks{};     // CARDFreeBlocks
    std::uint32_t free_files{};
};

// The CARD library's control block of one channel.
struct CardChannel {
    bool attached{};       // card->attached: mounted (CARDUnmount detaches)
    int result{};          // card->result
    std::int64_t end{};    // when the running asynchronous call ends
    int final_result{};    // its result then

    // CARDProbeEx: a present card is ready (EXIInit probed both slots at boot,
    // long before the first request, so the 300 ms presence wait is over).
    int probe(const CardSlot& card) const { return card.present ? card::kReady : card::kNoCard; }
    // An asynchronous call that takes `ticks` and then reports `outcome`.
    int start(std::int64_t now, std::int64_t ticks, int outcome);
    // CARDGetResultCode.
    int result_code(std::int64_t now);
};

// One beSv*Api object (the SAVEUTILITY REQUEST_* order of 80367B18).
class CardRequest {
public:
    enum Kind : int { kFileMake, kFileFind, kFormat, kUseCheck, kReadMedia, kWriteMedia, kGetFreeSize };
    // beSv codes (deliver +24).
    enum Code : int { kRunning = 1, kDone = 2, kError = 3, kMissing = 4, kMountError = 5, kBroken = 7, kBadData = 8,
                      kNoSpace = 9, kNoEntry = 10, kWrongDevice = 11, kNoCard = 12 };
    static constexpr std::uint32_t kCopyBytes = 40960;  // beSvPlatDataGC +16: a data copy
    static constexpr std::uint32_t kHeaderBytes = 8192; // +108: comment, banner and icon
    static constexpr std::uint32_t kFileBytes = 90112;  // +20: 11 blocks
    static constexpr std::uint32_t kImageOffset = 64;   // the banner follows the comment

    // +0x5C start. A negative slot means slot A, then B (FILEFIND only).
    CardRequest(Kind kind, int slot);
    // +0x60: one step; true once done (the step that ends counts).
    bool update(CardChannel& channel, CardSlot& card, std::int64_t now);

    Kind kind() const { return kind_; }
    int slot() const { return slot_; }
    int result() const { return result_; } // +24
    bool done() const { return done_; }    // +28
    // GETFREESIZE: CARDFreeBlocks (+216 bytes, +220 files).
    std::uint32_t free_bytes() const { return free_bytes_; }
    std::uint32_t free_files() const { return free_files_; }
    // Ticks a synchronous CARDSetStatus kept the frame waiting (the header
    // write of WRITE_MEDIA / FILEMAKE); the original's game loop stalls.
    std::int64_t stalled() const { return stalled_; }

private:
    int mount(CardChannel& channel, const CardSlot& card, std::int64_t now); // 8031FCF0
    int header(CardChannel& channel, CardSlot& card, std::int64_t now);      // 8032029C
    void finish(CardChannel& channel);                                       // 8031FF98
    // Steps 0-1 (+32 / +36): the mount, mapped to the beSv codes; true
    // when the request may go on.
    bool mounted(CardChannel& channel, const CardSlot& card, std::int64_t now, bool format = false);
    void next(int step) { step_ = step; sub_ = 0; }
    void fail(int code, int step) { result_ = code; next(step); }

    Kind kind_;
    int slot_{};
    bool any_slot_{};          // +16: try slot B when slot A fails (FILEFIND)
    bool tried_b_{};           // +20 reached slot B
    int result_ = kRunning;
    bool done_{};
    int step_{}, sub_{};       // +32 / +36
    int mount_state_{};        // +192
    int mount_retries_{};      // +188
    bool mount_flag_{};        // +196: unmount when done
    int header_state_{};       // +200
    int copy_{};               // +220 / +232: the data copy being moved
    std::uint32_t free_bytes_{}, free_files_{};
    std::int64_t stalled_{};
};

} // namespace dw4
