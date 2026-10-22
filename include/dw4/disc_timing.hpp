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
    // Rotational latency: Dolphin's formula (default), whose disc angle
    // follows the whole seconds of its emulated clock (so a seek costs one of
    // two latencies by the parity of the second), or the mean half turn.
    enum class Rotation : std::uint8_t { mean, dolphin };
    // Dolphin's clock (CoreTiming ticks since boot) at the caller's clock 0,
    // modulo the 2 s its rotation formula repeats in. The captures' DVD
    // interrupts follow Dolphin's clock = TB * 12 + 23.8e6 ticks (all 394
    // reads of a stage load within 0.006 field), and their world clock starts
    // 6903.2e6 ticks after boot; the port's driver tick at world time t stands
    // for the servers of the next field (t + 8108108 ticks).
    static constexpr std::int64_t kDolphinClockOffset = 107330000;

    // Duration (ticks) of a read of `length` bytes at disc offset `offset`
    // that starts at time `now`; updates the read-ahead buffer.
    std::int64_t read(std::uint64_t offset, std::uint64_t length, std::int64_t now);

    // Head travel time (s) between two disc offsets and the streaming time
    // (s) of a range, Dolphin's DVDMath.
    static double seek_time(std::uint64_t from, std::uint64_t to);
    static double raw_read_time(std::uint64_t offset, std::uint64_t length);
    static double rotational_latency(std::uint64_t offset, double time_seconds);

    Rotation rotation{Rotation::dolphin};
    std::int64_t clock_offset{kDolphinClockOffset}; // ticks added to the clock for Rotation::dolphin
    // The read-ahead buffer: [start offset, end offset) fills from start time
    // to end time (DVDInterface m_read_buffer_*).
    std::uint64_t buffer_start_offset{}, buffer_end_offset{};
    std::int64_t buffer_start_time{}, buffer_end_time{};
    // PC fast loading (not the original): every read takes no time and
    // leaves the read-ahead buffer alone; a request ends at the next server
    // tick. The sound driver sets it each tick from the engine.
    bool instant{};
};

// A Cri sound entry's volume fade (the fade object at entry +16: set up by
// 8031D5D0, stepped once per field by 8031DCB0 / 8031DC38 while the entry's
// stream runs unpaused, 802F53B8). The level runs through the fade table
// 804E05C8 (down, from >= to) or 804E04D8 (up) from the first entry the
// start level reaches to the last entry the end level reaches, in `frames`
// steps; the output volume (0.1 dB) is (int)|-210 * level| - 210 + the
// sound's volume + the Cri's base volume.
struct CriFade {
    bool running{};            // +8 == 3
    float from{}, to{};        // +20 / +24
    float position{};          // +16: table index
    float increment{};         // +28: its step
    void start(float from_level, float to_level, float frames);
    // One step: the output volume; once the target is reached the fade ends,
    // a fade to silence then returns -999.
    int step(int volume, int base);
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
    // ADXT_Pause (8029A480: a playing or decoded handle starts or stops its
    // renderer; a paused handle still becomes PLAYING but nothing is played).
    void start(int slot, const File& file);
    void stop(int slot);
    void pause(int slot, bool paused, std::int64_t now);
    // The stage loader's file stream (its own ADXSTM 8051B1D0, after the six
    // ADXT readers 8051ABD0.. in each file server pass, so a stream refill
    // wins a tick both want): a file is read in requests of at most 64
    // sectors, one at a time; the loader consumes a booked chunk in the game
    // update that follows, so the next request comes one field later (the
    // engine asks for one chunk at a time and adds the loading thread's work,
    // Engine::process_stage_loader).
    void load(std::uint64_t disc_offset, std::uint32_t sectors);
    // One VI field of the CRI servers at time `now`.
    void tick(std::int64_t now);

    bool active(int slot) const { return valid(slot) && handles[std::size_t(slot)].active; }
    // ADXT status (802A8C18): 0 stopped, 1 decinfo, 2 prep, 3 playing,
    // 4 decoding ended, 5 play ended (a one-shot that has been played out).
    int status(int slot) const { return active(slot) ? handles[std::size_t(slot)].state : 0; }
    // ADXT status 3 (PLAYING).
    bool playing(int slot) const { return status(slot) == 3; }
    bool reading() const { return inflight.slot >= 0; }
    // Samples the renderer sees played on the voices since the ring's last
    // reset (its read of the voice address, 802A733C).
    std::uint64_t ring_position(int slot, std::int64_t now) const {
        if (!active(slot)) return 0;
        const auto& h = handles[std::size_t(slot)];
        return played(h, now) - h.played_base;
    }

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
        std::uint8_t state{}; // ADXT status: 1 decinfo, 2 prep, 3 playing, 4 decoding ended, 5 play ended
        std::uint32_t limit{}, pos{}, request{}; // sectors: read limit, read so far, in flight
        std::uint32_t write_pos{};               // ring write position (bytes)
        std::uint64_t written{}, consumed{};     // ring bytes in / out
        std::uint64_t decoded{};                 // samples per channel
        std::uint32_t sj{};                      // decoded samples in the output SJ
        std::uint64_t moved{}, reclaimed{};      // samples into the ARAM ring / played back out
        std::int64_t dsp_start{-1};              // time the renderer switched the voices on (-1: off)
        std::uint64_t played_base{};             // samples moved into the ARAM ring before that start
    };
    std::array<Handle, kSlots> handles{};
    // The inflight slot of the loader's reads.
    static constexpr int kLoaderSlot = kSlots;
    struct Loader {
        bool active{};               // a file is read (until its last chunk is booked)
        bool busy{}, done{};         // a chunk is on the drive / the drive finished it
        bool booked{};               // a chunk was booked in this tick: no request before the next
        std::uint64_t disc_offset{};
        std::uint32_t sectors{}, pos{}, request{};
    } loader;
    struct Inflight {
        int slot{-1};        // handle whose read is on the drive (-1: idle, kLoaderSlot: the loader)
        std::int64_t done{}; // completion time
    } inflight;
    DiscDrive drive;

    // The DSP's frames: AX mixes 160 samples per frame at the AI's 32 kHz
    // rate, which is 54 MHz / 1686 (32028.5 Hz; the r29 captures: one frame
    // every 2427840 ticks over 1400 fields), and copies the voices' progress
    // back to the CPU at each frame boundary. ax_phase is a boundary on the
    // caller's clock: the one of the r29 captures' boot, whose frames start
    // kAxPhase ticks after a multiple of the frame from the port's world
    // clock 0.
    static constexpr std::int64_t kAxFrameTicks = 2427840;
    static constexpr std::int64_t kAxPhase = 1099812;
    std::int64_t ax_phase{kAxPhase};

private:
    static bool valid(int slot) { return slot >= 0 && slot < kSlots; }
    std::int64_t ax_frame(std::int64_t t) const;
    std::uint64_t played(const Handle& h, std::int64_t now) const;
};

} // namespace dw4
