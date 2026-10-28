#include "dw4/disc_timing.hpp"

#include <algorithm>
#include <cmath>

namespace dw4 {
namespace {
// Dolphin DVDMath.cpp: GameCube discs have the Wii data density; the radius
// follows the spiral's area.
constexpr std::uint64_t kWiiLayerSize = 0x118240000ull;
constexpr double kInnerRadius = 0.024;
constexpr double kWiiOuterRadius = 0.058;
constexpr double kGcOuterRadius = 0.038;
constexpr double kTrackPitch = 0.00000074;
constexpr double kGcInnerSpeed = 1024 * 1024 * 2.1;   // bytes/s
constexpr double kGcOuterSpeed = 1024 * 1024 * 3.325; // bytes/s
constexpr double kGcRotationsPerSecond = 28.5;
constexpr double kShortSeekMaxDistance = 0.001;
constexpr double kShortSeekConstant = 0.035;
constexpr double kShortSeekVelocityInverse = 50;
constexpr double kLongSeekConstant = 0.075;
constexpr double kLongSeekVelocityInverse = 4.5;
// Dolphin DVDInterface.cpp
constexpr std::uint64_t kEccBlock = 16 * 0x800;
constexpr std::uint64_t kStreamingBuffer = 1024 * 1024;
constexpr std::uint64_t kBackwardSeekLimit = 2 * kEccBlock;
constexpr std::int64_t kBufferTransferRate = 32 * 1024 * 1024;
constexpr std::int64_t kReadCommandLatencyUs = 600;

double position(std::uint64_t offset) {
    offset %= kWiiLayerSize * 2;
    if (offset > kWiiLayerSize) offset = kWiiLayerSize * 2 - offset;
    return std::sqrt(double(offset) / double(kWiiLayerSize) * (kWiiOuterRadius * kWiiOuterRadius - kInnerRadius * kInnerRadius) +
                     kInnerRadius * kInnerRadius);
}

// DW4's CRI setup.
constexpr std::uint32_t kSector = 2048;
// ADXT_Create (8029B820) input ring: (work - 24768 * channels - 292) / 2048
// sectors of the work size 802A878C computes for the handle (8031C99C:
// slots 0-1 stereo at 48 kHz, 2-5 mono at 24 kHz).
constexpr std::array<std::uint32_t, CriStreams::kSlots> kRingSectors{53, 53, 13, 13, 13, 13};
constexpr std::uint32_t kStartLimit = 25;          // 804CD3A8: sectors read before the header is parsed
constexpr std::uint32_t kMaxRequest = 512;         // ADXSTM +0x2C
constexpr std::uint32_t kLoaderRequest = 64;       // the loader's reads (every capture: 64-sector requests)
constexpr std::uint32_t kOutputSamples = 0x2000;   // output SJ per channel (8029B820: +0x30)
// The output SJ's extra area (8029B820: +0x34 8288 - +0x30): a chunk may run
// past the ring's end by this much (SJRBF 802A372C), so the renderer reads
// up to 96 samples beyond it in one piece.
constexpr std::uint32_t kOutputExtra = 8288 - kOutputSamples;
// The input SJ's extra area (8029B820: +0x28 = 36 bytes): the decoder's
// chunk of the input may run 36 bytes past the ring's end.
constexpr std::uint32_t kInputExtra = 36;
constexpr std::uint32_t kAramSamples = 4096;       // RNARES ring per channel (802A8590)
constexpr std::uint32_t kAramStep = 2048;          // played samples are given back in halves (802A72F0)
// A command of the game update runs 0.02 field after the servers of its field.
constexpr std::int64_t kGamePhase = CriStreams::kFieldTicks * 2 / 100;
// AX output samples per frame (5 ms at the nominal 32 kHz).
constexpr std::int64_t kAxFrameSamples = 160;

// The voice's sample-rate converter step (16.16) the renderer gives AX: the
// stream's rate over the nominal 32000 Hz (the captures: a 48 kHz voice moves
// exactly 240 samples per frame, a 24 kHz one 120).
std::int64_t src_ratio(const CriStreams::File& file) { return (std::int64_t(file.sample_rate) << 16) / 32000; }

// 8029C1E0: samples per channel the decoder may produce per server call:
// three fields of a looping stream, half that for one-shots, rounded to a
// multiple of 64 above.
std::uint32_t decode_unit(const CriStreams::File& file) {
    std::uint32_t unit = 3 * (file.sample_rate / 60);
    if (!file.loop) unit /= 2;
    return 64 * ((unit + 64) / 64);
}

// The fade tables (804E05C8 down, 804E04D8 up; 60 levels each).
constexpr float kFadeDown[60] = {
    0.959999979f, 0.930000007f, 0.899999976f, 0.870000005f, 0.829999983f, 0.810000002f, 0.779999971f, 0.75f,
    0.720000029f, 0.689999998f, 0.660000026f, 0.639999986f, 0.610000014f, 0.579999983f, 0.560000002f, 0.529999971f,
    0.50999999f,  0.49000001f,  0.460000008f, 0.439999998f, 0.419999987f, 0.400000006f, 0.379999995f, 0.360000014f,
    0.330000013f, 0.319999993f, 0.300000012f, 0.280000001f, 0.25999999f,  0.25f,        0.230000004f, 0.209999993f,
    0.200000003f, 0.180000007f, 0.170000002f, 0.150000006f, 0.140000001f, 0.129999995f, 0.119999997f, 0.109999999f,
    0.100000001f, 0.0799999982f, 0.0799999982f, 0.0700000003f, 0.0599999987f, 0.0500000007f, 0.0399999991f, 0.0299999993f,
    0.0299999993f, 0.0199999996f, 0.0199999996f, 0.00999999978f, 0.00999999978f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
constexpr float kFadeUp[60] = {
    0.0399999991f, 0.0700000003f, 0.100000001f, 0.129999995f, 0.170000002f, 0.189999998f, 0.219999999f, 0.25f,
    0.280000001f,  0.310000002f,  0.340000004f, 0.360000014f, 0.389999986f, 0.419999987f, 0.439999998f, 0.469999999f,
    0.49000001f,   0.50999999f,   0.540000021f, 0.560000002f, 0.579999983f, 0.600000024f, 0.620000005f, 0.639999986f,
    0.670000017f,  0.680000007f,  0.699999988f, 0.720000029f, 0.74000001f,  0.75f,        0.769999981f, 0.790000021f,
    0.800000012f,  0.819999993f,  0.829999983f, 0.850000024f, 0.860000014f, 0.870000005f, 0.879999995f, 0.889999986f,
    0.899999976f,  0.920000017f,  0.920000017f, 0.930000007f, 0.939999998f, 0.949999988f, 0.959999979f, 0.970000029f,
    0.970000029f,  0.980000019f,  0.980000019f, 0.99000001f,  0.99000001f,  1.0f,         1.0f,         1.0f,
    1.0f,          1.0f,          1.0f,         1.0f};
// (int)|-210 * level| (fctiwz after clearing the sign, 8031DD04..8031DD30).
int fade_db(float level) { return int(std::fabs(-210.0f * level)); }
} // namespace

// 8031D5D0: the first index is the first table entry the start level
// reaches, the last index the last entry the end level reaches (scanned from
// the table's end; -1 when none does); the index advances by their distance
// over the frames.
void CriFade::start(float from_level, float to_level, float frames) {
    running = true;
    from = from_level;
    to = to_level;
    int first = -1, last = -1;
    if (from >= to) {
        for (int i = 0; i < 60 && first < 0; ++i)
            if (from >= kFadeDown[i]) first = i;
        for (int i = 59; i >= 0 && last < 0; --i)
            if (to <= kFadeDown[i]) last = i;
    } else {
        for (int i = 0; i < 60 && first < 0; ++i)
            if (from <= kFadeUp[i]) first = i;
        for (int i = 59; i >= 0 && last < 0; --i)
            if (to >= kFadeUp[i]) last = i;
    }
    position = float(first);
    // (No script fades over zero frames; that would divide by zero.)
    increment = float(last - first) / std::max(frames, 1.0f);
}

// 8031DCB0 (with 8031DC38): the index advances by the step, then the level
// is read from the table (the down table while from >= to); the fade ends
// once the volume reaches the target's.
int CriFade::step(int volume, int base) {
    position += increment;
    // fctiwz; the original indexes without a bound (every fade ends before
    // its index leaves the table).
    const int index = std::clamp(int(position), 0, 59);
    const float level = from >= to ? kFadeDown[index] : kFadeUp[index];
    const int result = fade_db(level) + volume + base - 210;
    const int target = fade_db(to) + volume + base - 210;
    if (from <= to) {
        if (result >= target) running = false;
    } else if (result <= target) {
        running = false;
        if (to == 0.0f) return -999;
    }
    return result;
}

std::uint32_t rna_src_ratio(std::uint32_t rate, bool sofdec) {
    // 802A6CFC..802A6D1C: mode 1 through the multiply by 0x7482296B (/ 1125).
    const std::uint32_t f = sofdec ? std::uint32_t(1124ull * (std::uint64_t(rate) + 1) / 1125) : rate;
    // mulhw 0x10624DD3 >> 11 (/ 32000) and (f << 8) / 125, its low half.
    return ((f / 32000u) << 16) | (((f << 8) / 125u) & 0xFFFFu);
}

double rna_rate(std::uint32_t rate, bool sofdec) {
    return double(rna_src_ratio(rate, sofdec)) / 65536.0 * 54.0e6 / 1686.0;
}

// 8031DDB8: the table step of 8031DC38, then 127 * level and 127 * to
// truncated (fctiwz); the fade ends when the volume is at or below the
// target's (in either direction: only fades down reach sfp entries).
int CriFade::step_seq() {
    position += increment;
    const int index = std::clamp(int(position), 0, 59);
    const float level = from >= to ? kFadeDown[index] : kFadeUp[index];
    int result = int(127.0f * level);
    if (result <= int(127.0f * to)) {
        running = false;
        if (to == 0.0f) result = 0;
    }
    return result;
}

double DiscDrive::seek_time(std::uint64_t from, std::uint64_t to) {
    const double distance = std::fabs(position(from) - position(to));
    if (distance < kShortSeekMaxDistance) return distance * kShortSeekVelocityInverse + kShortSeekConstant;
    return distance * kLongSeekVelocityInverse + kLongSeekConstant;
}

double DiscDrive::raw_read_time(std::uint64_t offset, std::uint64_t length) {
    const double radius = position(offset + length / 2);
    const double speed = (radius - kInnerRadius) / (kGcOuterRadius - kInnerRadius) * (kGcOuterSpeed - kGcInnerSpeed) + kGcInnerSpeed;
    return double(length) / speed;
}

double DiscDrive::rotational_latency(std::uint64_t offset, double time_seconds) {
    const double target = std::fmod(position(offset) / kTrackPitch, 1.0);
    const double start = std::fmod(time_seconds * kGcRotationsPerSecond, 1.0);
    return std::fmod(target + 1.0 - start, 1.0) / kGcRotationsPerSecond;
}

// ScheduleReads: per ECC block either a transfer out of the read-ahead buffer,
// a streaming read right behind the head, or a seek (head travel plus the
// rotation to the block). Afterwards the drive keeps reading ahead.
std::int64_t DiscDrive::read(std::uint64_t offset, std::uint64_t length, std::int64_t now) {
    if (instant) return 0;
    std::uint64_t dvd = offset - offset % kEccBlock;
    const std::uint64_t first_block = dvd;
    std::uint64_t buffer_start = 0, buffer_end = 0, head = 0;
    if (buffer_start_time != buffer_end_time) {
        buffer_start = buffer_end_offset > kStreamingBuffer ? buffer_end_offset - kStreamingBuffer : 0;
        if (now >= buffer_end_time) {
            buffer_end = buffer_end_offset;
        } else {
            const std::uint64_t part = std::uint64_t(now - buffer_start_time) * (buffer_end_offset - buffer_start_offset) /
                                       std::uint64_t(buffer_end_time - buffer_start_time);
            buffer_end = buffer_start_offset + (part - part % kEccBlock);
        }
        head = buffer_end;
        // Reading before the buffer drops it (the head stays).
        if (dvd < buffer_start) buffer_start = buffer_end = 0;
    }
    std::int64_t ticks = kReadCommandLatencyUs * (kTicksPerSecond / 1000000);
    bool seek = false;
    std::uint64_t left = length;
    do {
        const std::uint64_t chunk = std::min((offset + kEccBlock) / kEccBlock * kEccBlock - offset, left);
        if (dvd >= buffer_start && dvd < buffer_end) {
            ticks += std::int64_t(chunk) * kTicksPerSecond / kBufferTransferRate;
        } else {
            if (dvd != head) {
                seek = true;
                ticks += std::int64_t(double(kTicksPerSecond) * seek_time(head, dvd));
                double latency = 0.5 / kGcRotationsPerSecond;
                if (rotation == Rotation::dolphin) {
                    // Dolphin divides integer ticks: whole seconds.
                    const std::int64_t seconds = (now + clock_offset + ticks) / kTicksPerSecond;
                    latency = rotational_latency(dvd, double(seconds));
                }
                ticks = std::int64_t(double(ticks) + double(kTicksPerSecond) * latency);
            } else {
                ticks += std::int64_t(double(kTicksPerSecond) * raw_read_time(dvd, kEccBlock));
            }
            head = dvd + kEccBlock;
        }
        offset += chunk;
        left -= chunk;
        dvd += kEccBlock;
    } while (left > 0);
    const std::uint64_t last_block = dvd;
    if (!(last_block - buffer_start <= kBackwardSeekLimit && buffer_start != buffer_end)) {
        buffer_start_offset = last_block >= buffer_end ? last_block : buffer_end;
        buffer_end_offset = last_block + kStreamingBuffer - kBackwardSeekLimit;
        if (seek) buffer_end_offset = std::max(buffer_end_offset, first_block + kStreamingBuffer);
        buffer_start_time = now + ticks;
        buffer_end_time = buffer_start_time + std::int64_t(double(kTicksPerSecond) *
                                                           raw_read_time(buffer_start_offset, buffer_end_offset - buffer_start_offset));
    }
    return ticks;
}

void CriStreams::start(int slot, const File& file) {
    if (!valid(slot)) return;
    auto& h = handles[std::size_t(slot)];
    const bool busy = h.busy, paused = h.paused;
    // ADXT_StartAfs stops the handle first (8029AF34): with a read on the
    // drive the ADXSTM stop waits for it (80299A30).
    h = Handle{};
    h.busy = busy;
    h.stopping = busy;
    h.paused = paused;
    h.file = file;
    h.active = true;
    h.state = 1;
    h.limit = kStartLimit;
}

void CriStreams::stop(int slot) {
    if (!valid(slot)) return;
    auto& h = handles[std::size_t(slot)];
    const bool busy = h.busy, paused = h.paused;
    h = Handle{};
    h.busy = busy;
    h.stopping = busy;
    h.paused = paused;
}

void CriStreams::load(std::uint64_t disc_offset, std::uint32_t sectors) {
    loader.active = sectors > 0;
    loader.disc_offset = disc_offset;
    loader.sectors = sectors;
    loader.pos = 0;
}

void CriStreams::pause(int slot, bool paused, std::int64_t now) {
    if (!valid(slot)) return;
    auto& h = handles[std::size_t(slot)];
    if (h.paused == paused) return;
    h.paused = paused;
    // 8029A4E0: the renderer is switched in status 3 and 4 only.
    if (!h.active || (h.state != 3 && h.state != 4)) return;
    // The game update runs after this field's servers, i.e. one field
    // before the driver tick that follows it.
    const std::int64_t at = now - kFieldTicks + kGamePhase;
    if (paused) {
        // 802A7534 (switch off): the voices stop and the ARAM ring is reset
        // (its SJ's vtable +0x14: empty, both positions at the start); what
        // it held unplayed is dropped (the transfer refills it).
        h.reclaimed = h.moved;
        h.aram_base = h.moved;
        h.dsp_start = -1;
    } else {
        // 802A7534 (switch on): the voices start again at the ring's start,
        // where the data written after the reset begins.
        h.played_base = h.reclaimed;
        h.dsp_start = at;
    }
}

// The AX frame boundary at or before `t` (its index on the frame grid).
std::int64_t CriStreams::ax_frame(std::int64_t t) const {
    const std::int64_t d = t - ax_phase;
    return d >= 0 ? d / kAxFrameTicks : -((-d + kAxFrameTicks - 1) / kAxFrameTicks);
}

// The renderer reads the voice's current address from the CPU copy of its
// parameter block (VPB +0x1B2, 802A733C). AX copies the DSP's address there
// once per frame (__AXSyncPBs -> 8023E4A8, at the frame's AI interrupt),
// before it sends the frame's command list: the copy shows the frames the
// DSP finished before the last boundary. A voice switched on at `dsp_start`
// plays from the first boundary after it, so after n boundaries the renderer
// sees n - 1 frames (the r29 captures: every renderer read of a 48 kHz
// stream within 0 samples).
std::uint64_t CriStreams::played(const Handle& h, std::int64_t now) const {
    if (h.dsp_start < 0) return h.played_base;
    const std::int64_t frames = ax_frame(now) - ax_frame(h.dsp_start) - 1;
    if (frames <= 0) return h.played_base;
    return h.played_base + std::uint64_t(frames * kAxFrameSamples * src_ratio(h.file) >> 16);
}

void CriStreams::tick(std::int64_t now) {
    // ADXT server (8029A60C). The decoder server first (80290018 -> 80290074
    // -> 80290520): a decoding handle whose output SJ has room (lane 0's
    // count / 2 >= ADXB +0x10, which the ADX header parse sets to 1,
    // 0x802936F8) takes the input SJ's data chunk - up to the ring's end and
    // its 36-byte extra area (SJRBF 802A372C) - and decodes in one call
    // (80293AEC, the block count of 80293E5C) the least of its whole ADX
    // frames, the output's free chunk (lane 0 up to its end and extra area,
    // capped at the unit: the output callback 80290838 with ADXSJD +0x38,
    // 80290C78) and the frames to the output ring's end; the consumed bytes
    // go back to the input's free lane (0x80290258).
    for (std::size_t slot = 0; slot < handles.size(); ++slot) {
        auto& h = handles[slot];
        if (!h.active || !h.decoding) continue;
        if (h.sj >= kOutputSamples) continue;
        if (!h.file.loop && h.decoded >= h.file.samples) continue;
        const std::uint32_t frame_bytes = 18 * h.file.channels;
        const std::uint64_t ring = std::uint64_t(kRingSectors[slot]) * kSector;
        const std::uint64_t in_chunk = std::min<std::uint64_t>(h.written - h.consumed, ring + kInputExtra - h.consumed % ring);
        const std::uint64_t out_pos = h.decoded % kOutputSamples;
        const std::uint64_t out_chunk =
            std::min<std::uint64_t>({kOutputSamples - h.sj, kOutputSamples + kOutputExtra - out_pos, decode_unit(h.file)});
        std::uint64_t frames = std::min<std::uint64_t>({in_chunk / frame_bytes, out_chunk / 32, (kOutputSamples - out_pos) / 32});
        std::uint64_t n = frames * 32;
        if (!h.file.loop) {
            // a one-shot ends with its last ADX frame, whose samples go out
            // only up to the file's end (80290074: min(decoded, total -
            // decoded so far) into the output SJ, all its bytes consumed)
            const std::uint64_t left = h.file.samples - h.decoded;
            frames = std::min<std::uint64_t>(frames, (left + 31) / 32);
            n = std::min<std::uint64_t>(frames * 32, left);
        }
        if (!frames) continue;
        h.decoded += n;
        h.sj += std::uint32_t(n);
        h.consumed += frames * frame_bytes;
    }
    // ADXT_ExecHndl (8029BED8) per handle.
    for (auto& h : handles) {
        if (!h.active) continue;
        if (!h.bound) {
            // decinfo (8029C1E0): the stream is bound (8029B2D0) once its
            // ADXSTM stopped; the reader opens the file in the next file server.
            if (!h.busy && !h.stopping) h.bound = true;
            continue;
        }
        if (h.state == 1 && h.first_block) {
            // Header parse: the reader may go on to the loop end (8029963C),
            // the decoder starts.
            h.consumed += h.file.header_bytes;
            h.limit = h.file.loop ? h.file.loop_end_sector : 0x7FFFFFFFu;
            h.decoding = true;
            h.state = 2;
        } else if (h.state == 2) {
            // PLAYING (8029C03C) once the ARAM ring holds two units or is full,
            // or a one-shot is decoded to its end (a sound shorter than that).
            const std::uint64_t aram = h.moved - h.reclaimed;
            const bool decoded_all = !h.file.loop && h.decoded >= h.file.samples;
            if (aram >= 2ull * decode_unit(h.file) || kAramSamples - aram <= 32 || decoded_all) {
                h.state = 3;
                // 8029D9A0 -> 802A7534: the voices start at the ring's start.
                if (!h.paused) {
                    h.played_base = h.reclaimed;
                    h.dsp_start = now;
                }
            }
        } else if (h.state == 3) {
            // 8029BF14: a one-shot decoded to its end whose output SJ holds
            // fewer than 64 samples switches the renderer's transfer off
            // (8029D9C0) and ends decoding (status 4, DECEND).
            if (!h.file.loop && h.decoded >= h.file.samples && h.sj < 64) h.state = 4;
        } else if (h.state == 4) {
            // 8029C0F0: once the ARAM ring holds no unplayed data (802A74C0:
            // 4096 - free - flushed silence, i.e. moved - reclaimed) the
            // renderer stops (8029D9A0) and the stream has ended (status 5).
            if (h.moved <= h.reclaimed) {
                h.state = 5;
                h.played_base = h.reclaimed;
                h.dsp_start = -1;
            }
        }
    }
    // Renderer server (802A6DF8): the DSP's progress comes back in halves
    // (802A72F0: at most one per call), then one DMA per channel refills the
    // ARAM ring (also before playback): the least of the ring's free chunk
    // (lane 0 up to the ring's end, no extra area: 802A7C64) and the output
    // SJ's data chunk (up to its end and extra area), whole 16-sample units
    // (0x802A6EE4).
    for (auto& h : handles) {
        if (!h.active || h.state < 2 || h.state > 4) continue;
        if (played(h, now) >= h.reclaimed + kAramStep) h.reclaimed += kAramStep;
        // After DECEND the transfer is off: the ring only gets silence
        // (802A6FF0), the DSP plays the rest out.
        if (h.state == 4) continue;
        const std::uint64_t aram_pos = (h.moved - h.aram_base) % kAramSamples;
        const std::uint64_t room = std::min<std::uint64_t>(kAramSamples - (h.moved - h.reclaimed), kAramSamples - aram_pos);
        std::uint64_t m = std::min<std::uint64_t>({room, h.sj, kOutputSamples + kOutputExtra - h.moved % kOutputSamples});
        m -= m % 16;
        h.moved += m;
        h.sj -= std::uint32_t(m);
    }
    // File server (80296904): the gcCi poll, then two passes over the readers
    // (80298FC8 twice, 80299248 each).
    if (inflight.slot >= 0 && now >= inflight.done) {
        if (inflight.slot == kLoaderSlot) loader.done = true;
        else if (inflight.slot == kMovieSlot) movie.done = true;
        else if (inflight.slot == kMovieHeaderSlot) movie.header_done = true;
        else handles[std::size_t(inflight.slot)].done = true;
        inflight.slot = -1;
    }
    loader.booked = false;
    for (int pass = 0; pass < 2; ++pass) {
        for (int slot = 0; slot < kSlots; ++slot) {
            auto& h = handles[std::size_t(slot)];
            if (h.busy) {
                // A finished read is only booked in this call.
                if (!h.done) continue;
                h.busy = h.done = false;
                if (h.stopping) continue; // the previous file's data
                h.pos += h.request;
                h.written += std::uint64_t(h.request) * kSector;
                const std::uint32_t ring = kRingSectors[std::size_t(slot)] * kSector;
                h.write_pos = std::uint32_t((h.write_pos + std::uint64_t(h.request) * kSector) % ring);
                h.first_block = true;
                // 8029C8E4 (set as the reader's end callback at the header
                // parse): at the loop end the reader seeks back to the loop start.
                if (h.file.loop && h.decoding && h.pos == h.limit) h.pos = h.file.loop_start_sector;
                continue;
            }
            if (h.stopping) {
                h.stopping = false; // the stop takes effect
                continue;
            }
            if (!h.active || !h.bound) continue;
            const std::uint64_t ring = std::uint64_t(kRingSectors[std::size_t(slot)]) * kSector;
            // 8029B2D0: ask while the ring holds less than 85 %.
            const std::uint64_t threshold = std::uint64_t(kRingSectors[std::size_t(slot)] * 85 / 100) * kSector;
            const std::uint64_t used = h.written - h.consumed;
            if (used >= threshold || inflight.slot >= 0) continue;
            const std::uint64_t contiguous = std::min<std::uint64_t>(ring - used, ring - h.write_pos);
            std::int64_t n = std::int64_t(contiguous / kSector);
            n = std::min<std::int64_t>(n, std::int64_t(h.limit) - std::int64_t(h.pos));
            n = std::min<std::int64_t>(n, std::int64_t(h.file.sectors) - std::int64_t(h.pos));
            n = std::min<std::int64_t>(n, kMaxRequest);
            if (n <= 0) continue;
            const std::int64_t duration = drive.read(h.file.disc_offset + std::uint64_t(h.pos) * kSector, std::uint64_t(n) * kSector, now);
            h.busy = true;
            h.request = std::uint32_t(n);
            inflight.slot = slot;
            inflight.done = now + duration;
        }
        // Sofdec's stream (8051AE70) after them, the loader's (8051B1D0, also
        // ADXF's: the movie's header) last.
        movie_reader(now);
        movie_header(now);
        // The loader's stream comes after the ADXT readers.
        if (loader.busy) {
            if (!loader.done) continue;
            loader.busy = loader.done = false;
            loader.pos += loader.request;
            loader.booked = true;
            if (loader.pos >= loader.sectors) loader.active = false;
            continue;
        }
        if (!loader.active || loader.booked || inflight.slot >= 0) continue;
        const std::uint32_t n = std::min(kLoaderRequest, loader.sectors - loader.pos);
        inflight.done = now + drive.read(loader.disc_offset + std::uint64_t(loader.pos) * kSector, std::uint64_t(n) * kSector, now);
        inflight.slot = kLoaderSlot;
        loader.busy = true;
        loader.request = n;
    }
}

void CriStreams::movie_open(std::uint64_t disc_offset, std::uint32_t sectors) {
    movie_close();
    movie.header = true;
    movie.disc_offset = disc_offset;
    movie.sectors = sectors;
}

// A read still on the drive ends on the reader that asked for it: the
// stream's (kMovieSlot: done, then booked as stopped by movie_reader) or the
// open's on the loader's stream (kMovieHeaderSlot: header_done, then dropped
// by movie_header); neither books its data, and a later open's header read
// waits for the drive like any read.
void CriStreams::movie_close() {
    const bool busy = movie.busy, header_busy = movie.header_busy;
    movie = Movie{};
    movie.busy = movie.stopping = busy;
    movie.header_busy = movie.header_stopping = header_busy;
}

void CriStreams::movie_stream(std::int64_t now) {
    if (movie.sectors == 0 || movie.active) return;
    movie.active = true;
    movie_reader(now);
}

// ADXSTM_ExecHndl (80299248) for the movie's stream: a finished read is
// booked in its pass; then, with the drive free, it asks for the free part
// of its ring up to the ring's end while the ring holds less than 85 %.
void CriStreams::movie_reader(std::int64_t now) {
    if (movie.busy) {
        if (!movie.done) return;
        movie.busy = movie.done = false;
        if (movie.stopping) {
            movie.stopping = false;
            return;
        }
        movie.pos += movie.request;
        movie.written += std::uint64_t(movie.request) * kSector;
        movie.write_pos = std::uint32_t((movie.write_pos + std::uint64_t(movie.request) * kSector) % (kMovieRingSectors * kSector));
        return;
    }
    if (!movie.active || inflight.slot >= 0) return;
    const std::uint64_t ring = std::uint64_t(kMovieRingSectors) * kSector;
    const std::uint64_t used = movie.written - movie.consumed;
    if (used >= std::uint64_t(kMovieRingSectors * 85 / 100) * kSector) return;
    const std::uint64_t contiguous = std::min<std::uint64_t>(ring - used, ring - movie.write_pos);
    std::int64_t n = std::int64_t(contiguous / kSector);
    n = std::min<std::int64_t>(n, std::int64_t(movie.sectors) - std::int64_t(movie.pos));
    n = std::min<std::int64_t>(n, kMaxRequest);
    if (n <= 0) return;
    inflight.done = now + drive.read(movie.disc_offset + std::uint64_t(movie.pos) * kSector, std::uint64_t(n) * kSector, now);
    inflight.slot = kMovieSlot;
    movie.busy = true;
    movie.request = std::uint32_t(n);
}

// The open's ADXF_ReadNw of the file's first ten sectors (20480 bytes): read
// by the loader's stream; ADXF_GetStat answers 3 once the pass booked it.
void CriStreams::movie_header(std::int64_t now) {
    if (movie.header_busy) {
        if (!movie.header_done) return;
        movie.header_busy = movie.header_done = false;
        if (movie.header_stopping) {
            movie.header_stopping = false; // a closed open's read
            return;
        }
        movie.header_booked = true;
        return;
    }
    if (!movie.header || movie.header_booked || inflight.slot >= 0) return;
    inflight.done = now + drive.read(movie.disc_offset, std::uint64_t(kMovieHeaderSectors) * kSector, now);
    inflight.slot = kMovieHeaderSlot;
    movie.header_busy = true;
}

} // namespace dw4
