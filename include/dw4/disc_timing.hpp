#pragma once

// Disc read timing as the original's ADX streams observe it.
//
// The game streams its music and ambience (and the ADX sound effects) from
// the disc through CRI's ADX layer: ADXT 9.31b handles, each with an ADXSTM
// file reader that fills an SJ ring buffer through the GameCube file system
// "gcCi" (one DVD read at a time). A stream reports ready (ADXT status 3,
// PLAYING, which SOUND ISREADY / ISALLREADY test, 802F6FB0) only after its
// first block came off the disc and was decoded into the ARAM ring, so the
// demo after "DMchkSD SOUND ISALLREADY" waits for the drive.
//
// DiscDrive is the drive: Dolphin's model of the GameCube DVD drive
// (Source/Core/Core/HW/DVD/DVDMath.cpp and DVDInterface.cpp ScheduleReads),
// which the reference captures follow to the microsecond: constant angular
// velocity read speed by radius, seek time by head travel, rotational
// latency, and the 1 MiB the drive keeps reading ahead after a read.
// CriStreams is the CRI side of DW4's six 'beCri' handles (8031C99C: two
// stereo 48 kHz handles for the BGM and the ambience, four mono 24 kHz ones
// for ADX sound effects): which handle reads when and how much, the decoder
// and the renderer that turn the data into "ready" and consume it.
// Everything runs on the caller's clock (CPU ticks of 486 MHz), never on
// the host's; the driver ticks it once per VI field.
#include <array>
#include <cstdint>

namespace dw4 {

class DiscDrive {
public:
    static constexpr std::int64_t kTicksPerSecond = 486000000;
    // Rotational latency: the mean half turn (default), or Dolphin's formula,
    // whose disc angle follows the whole seconds of its emulated clock (test
    // tool: matches a Dolphin capture when `clock_offset` maps the caller's
    // clock to Dolphin's).
    enum class Rotation : std::uint8_t { mean, dolphin };

    // Duration (ticks) of a read of `length` bytes at disc offset `offset`
    // that starts at time `now`; updates the read-ahead buffer.
    std::int64_t read(std::uint64_t offset, std::uint64_t length, std::int64_t now);

    // Head travel time (s) between two disc offsets and the streaming time
    // (s) of a range, Dolphin's DVDMath.
    static double seek_time(std::uint64_t from, std::uint64_t to);
    static double raw_read_time(std::uint64_t offset, std::uint64_t length);
    static double rotational_latency(std::uint64_t offset, double time_seconds);

    Rotation rotation{Rotation::mean};
    std::int64_t clock_offset{}; // ticks added to the clock for Rotation::dolphin
    // The read-ahead buffer: [start offset, end offset) fills from start time
    // to end time (DVDInterface m_read_buffer_*).
    std::uint64_t buffer_start_offset{}, buffer_end_offset{};
    std::int64_t buffer_start_time{}, buffer_end_time{};
};

class CriStreams {
public:
    static constexpr int kSlots = 6;
    // One VI field in ticks (60000/1001 Hz, the world clock's field).
    static constexpr std::int64_t kFieldTicks = 8108100;

    // An ADX file as the stream reader sees it.
    struct File {
        std::uint64_t disc_offset{};    // first byte on the disc
        std::uint32_t sectors{};        // size in 2048-byte sectors
        std::uint32_t header_bytes{};   // ADX data offset, consumed at the header parse
        std::uint32_t channels{1};
        std::uint32_t sample_rate{48000};
        std::uint32_t samples{};        // per channel
        bool loop{};
        std::uint32_t loop_start_sector{}, loop_end_sector{};
    };

    // Game side (between driver ticks): ADXT_StartAfs, ADXT_Stop,
    // ADXT_Pause (8029A480: a playing handle starts or stops its renderer;
    // a paused handle still becomes PLAYING but nothing is played).
    void start(int slot, const File& file);
    void stop(int slot);
    void pause(int slot, bool paused, std::int64_t now);
    // One VI field of the CRI servers at time `now`.
    void tick(std::int64_t now);

    bool active(int slot) const { return valid(slot) && handles[std::size_t(slot)].active; }
    // ADXT status 3 (PLAYING).
    bool playing(int slot) const { return active(slot) && handles[std::size_t(slot)].state == 3; }
    bool reading() const { return inflight.slot >= 0; }

    // State (save states).
    struct Handle {
        File file;
        bool active{};     // a file is started (ADXT running)
        bool bound{};      // the ADXSTM was given the file (ADXT decinfo state 1)
        bool stopping{};   // the previous file's read is still on the drive
        bool busy{};       // this handle's read is on the drive
        bool done{};       // ... and the drive finished it
        bool paused{};
        bool first_block{}; // the first block arrived (the header parse follows)
        bool decoding{};
        std::uint8_t state{}; // ADXT status: 1 decinfo, 2 prep, 3 playing
        std::uint32_t limit{}, pos{}, request{}; // sectors: read limit, read so far, in flight
        std::uint32_t write_pos{};               // ring write position (bytes)
        std::uint64_t written{}, consumed{};     // ring bytes in / out
        std::uint64_t decoded{};                 // samples per channel
        std::uint32_t sj{};                      // decoded samples in the output SJ
        std::uint64_t moved{}, reclaimed{};      // samples into the ARAM ring / played back out
        std::int64_t dsp_start{-1};              // time the DSP started playing (-1: stopped)
        double played_before{};                  // samples played before a pause
    };
    std::array<Handle, kSlots> handles{};
    struct Inflight {
        int slot{-1};        // handle whose read is on the drive (-1: idle)
        std::int64_t done{}; // completion time
    } inflight;
    DiscDrive drive;

private:
    static bool valid(int slot) { return slot >= 0 && slot < kSlots; }
    double played(const Handle& h, std::int64_t now) const;
};

} // namespace dw4
