#include "dw4/disc.hpp"
#include "dw4/disc_timing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = std::uint8_t(v >> 24);
    d[at + 1] = std::uint8_t(v >> 16);
    d[at + 2] = std::uint8_t(v >> 8);
    d[at + 3] = std::uint8_t(v);
}

// A synthetic header and FST: root { ndmw.igb, ndmw.afs, sub { Area.afs } }.
std::vector<std::uint8_t> header() {
    std::vector<std::uint8_t> h(0x440);
    const std::string id = "GDJE52";
    for (std::size_t i = 0; i < id.size(); ++i) h[i] = std::uint8_t(id[i]);
    put32(h, 0x1C, 0xC2339F3D);
    put32(h, 0x420, 0x1000);
    return h;
}

std::vector<std::uint8_t> fst() {
    const std::vector<std::string> names{"ndmw.igb", "ndmw.afs", "sub", "Area.afs"};
    std::vector<std::uint8_t> d(5 * 12);
    std::vector<std::uint32_t> offsets;
    std::string table;
    for (const auto& name : names) {
        offsets.push_back(std::uint32_t(table.size()));
        table += name;
        table.push_back('\0');
    }
    put32(d, 0, 0x01000000);
    put32(d, 8, 5);
    put32(d, 12, offsets[0]);
    put32(d, 16, 0x2000);
    put32(d, 20, 0x100);
    put32(d, 24, offsets[1]);
    put32(d, 28, 0x3000);
    put32(d, 32, 0x800);
    put32(d, 36, 0x01000000 | offsets[2]);
    put32(d, 40, 0);
    put32(d, 44, 5);
    put32(d, 48, offsets[3]);
    put32(d, 52, 0x4000);
    put32(d, 56, 0x400);
    d.insert(d.end(), table.begin(), table.end());
    return d;
}

void test_parse() {
    const auto disc = dw4::DiscImage::parse(header(), fst(), 0x10000);
    check(disc.game_id() == "GDJE52", "The game id is read from the header");
    check(disc.dol_offset() == 0x1000, "The DOL offset is read from the header");
    check(disc.files().size() == 3, "Directories are not files");
    const auto* igb = disc.find("NDMW.IGB");
    check(igb && igb->offset == 0x2000 && igb->size == 0x100, "Files are found without case");
    const auto* area = disc.find("sub/area.afs");
    check(area && area->path == "sub/Area.afs" && area->offset == 0x4000, "Paths include their directories");
    check(!disc.find("area.afs"), "A file below a directory needs its path");
}

void test_rejects() {
    auto bad = header();
    bad[0x1C] = 0;
    bool thrown = false;
    try {
        dw4::DiscImage::parse(bad, fst(), 0x10000);
    } catch (const std::exception&) {
        thrown = true;
    }
    check(thrown, "A header without the disc magic is rejected");
    thrown = false;
    try {
        dw4::DiscImage::parse(header(), fst(), 0x3000);
    } catch (const std::exception&) {
        thrown = true;
    }
    check(thrown, "A file outside the image is rejected");
}

// ---------------------------------------------------------------------------
// Stream reads against Dolphin captures (work/dref/fix_r14stream, sndlog4 AR
// logger: every DVD read and its interrupt, ADXSTM reads, ADXT PLAYING,
// ADXT_Pause). Times are Dolphin's CPU ticks since boot.
// ---------------------------------------------------------------------------
constexpr std::uint64_t kNdmw = 0x2dbfb298; // ndmw.afs on the GDJEB2 disc
constexpr double kField = 486000000.0 / 59.94;

// The r14/r15 captures' times are their time base * 12, which their reads
// follow with Dolphin's rotation formula as it stands (the port's own clock
// has DiscDrive::kDolphinClockOffset added).
constexpr std::int64_t kCaptureClock = 0;
// The DSP's frame grid on that clock: the r29 captures' renderer reads put a
// frame boundary at time base 164817 (mod 202320), on Dolphin's clock (time
// base * 12 + 23.8e6, see test_rotation_clock) 1499404 ticks after a
// multiple of the frame. Every phase from 0.79e6 to 1.76e6 gives the reads
// below; the others put the ambience's refill after ISALLREADY in stagBS02 a
// field early.
constexpr std::int64_t kCaptureAxPhase = 1499404;

dw4::CriStreams::File adx(std::uint32_t sector, std::uint32_t sectors, std::uint32_t header, std::uint32_t channels,
                          std::uint32_t rate, std::uint32_t samples, std::uint32_t loop_start = 0, std::uint32_t loop_end = 0) {
    dw4::CriStreams::File f;
    f.disc_offset = kNdmw + std::uint64_t(sector) * 2048;
    f.sectors = sectors;
    f.header_bytes = header;
    f.channels = channels;
    f.sample_rate = rate;
    f.samples = samples;
    f.loop = loop_end != 0;
    f.loop_start_sector = loop_start;
    f.loop_end_sector = loop_end;
    return f;
}

struct Event {
    int tick, slot, sectors, sector; // a read; sectors 0: PLAYING
    bool operator==(const Event&) const = default;
};

struct Capture {
    std::int64_t first_lua{};   // ticks of the first.lua record
    double phase{};             // the servers' phase in the field
    std::uint64_t last_offset{}, last_length{}; // the last read before first.lua
    std::int64_t last_done{};   // its interrupt
    int inflight_slot{-1};      // that read is still on the drive at first.lua
    struct Command {
        double at;              // fields after first.lua
        int slot;
        int kind;               // 0 start, 1 pause, 2 resume
        dw4::CriStreams::File file{};
    };
    std::vector<Command> commands;
};

std::vector<Event> replay(const Capture& c, dw4::DiscDrive::Rotation rotation, int ticks, bool instant = false) {
    dw4::CriStreams cri;
    cri.drive.rotation = rotation;
    cri.drive.instant = instant;
    cri.drive.clock_offset = kCaptureClock;
    cri.ax_phase = kCaptureAxPhase;
    const std::uint64_t ecc = 0x8000;
    const std::uint64_t first = c.last_offset - c.last_offset % ecc;
    const std::uint64_t last = (c.last_offset + c.last_length + ecc - 1) / ecc * ecc;
    cri.drive.buffer_start_offset = last;
    cri.drive.buffer_end_offset = std::max(last + 0x100000 - 2 * ecc, first + 0x100000);
    cri.drive.buffer_start_time = c.last_done;
    cri.drive.buffer_end_time = c.last_done + std::int64_t(486000000.0 * dw4::DiscDrive::raw_read_time(
                                                                cri.drive.buffer_start_offset,
                                                                cri.drive.buffer_end_offset - cri.drive.buffer_start_offset));
    if (c.inflight_slot >= 0) {
        cri.inflight.slot = c.inflight_slot;
        cri.inflight.done = c.last_done;
        cri.handles[std::size_t(c.inflight_slot)].busy = true;
    }
    std::vector<Event> events;
    std::size_t next = 0;
    std::array<bool, dw4::CriStreams::kSlots> was_playing{};
    for (int k = 1; k <= ticks; ++k) {
        const std::int64_t now = c.first_lua + std::int64_t((double(k) + c.phase) * kField);
        // The game update since the previous servers (the port runs it
        // before the driver tick of the same frame).
        while (next < c.commands.size() && c.commands[next].at < double(k) + c.phase) {
            const auto& cmd = c.commands[next++];
            if (cmd.kind == 0) cri.start(cmd.slot, cmd.file);
            else cri.pause(cmd.slot, cmd.kind == 1, now);
        }
        const int before = cri.inflight.slot;
        const std::int64_t before_done = cri.inflight.done;
        cri.tick(now);
        if (cri.inflight.slot >= 0 && (before < 0 || cri.inflight.done != before_done)) {
            const auto& h = cri.handles[std::size_t(cri.inflight.slot)];
            events.push_back({k, cri.inflight.slot, int(h.request), int((h.file.disc_offset - kNdmw) / 2048 + h.pos)});
        }
        for (int s = 0; s < dw4::CriStreams::kSlots; ++s) {
            if (cri.playing(s) && !was_playing[std::size_t(s)]) events.push_back({k, s, 0, 0});
            was_playing[std::size_t(s)] = cri.playing(s);
        }
    }
    return events;
}

int all_ready(const std::vector<Event>& events) {
    int tick = 0;
    for (const auto& e : events)
        if (e.sectors == 0) tick = std::max(tick, e.tick);
    return tick;
}

void check_events(const std::vector<Event>& got, const std::vector<Event>& want, const char* message) {
    bool same = got.size() >= want.size();
    for (std::size_t i = 0; same && i < want.size(); ++i) same = got[i] == want[i];
    if (!same) {
        for (const auto& e : got) std::cerr << "  got " << e.tick << " slot " << e.slot << " " << e.sectors << " @" << e.sector << '\n';
    }
    check(same, message);
}

// stagBS01 after the new game (r14 capture d_bs01): four streams at first.lua,
// BGM and two SE streams paused until the demo; ISALLREADY at field 53.
Capture stagbs01() {
    Capture c;
    c.first_lua = 38395942632;
    c.phase = 0.018;
    c.last_offset = 0x39733298;
    c.last_length = 16384;
    c.last_done = 38385115776;
    const auto bgm = adx(31987, 2689, 732, 2, 48000, 4892980, 384, 2686);
    const auto env = adx(4, 2149, 2048, 2, 48000, 3908784, 1, 2149);
    c.commands = {{0.135, 0, 1}, {0.138, 1, 1}, {0.139, 1, 0, env}, {0.140, 1, 2},
                  {0.141, 2, 1}, {0.142, 2, 0, adx(132044, 94, 40, 1, 24000, 340404)},
                  {0.144, 3, 1}, {0.145, 3, 0, adx(131947, 97, 40, 1, 48000, 351294)},
                  {0.146, 0, 1}, {0.147, 0, 0, bgm}, {53.020, 2, 2}, {53.021, 0, 2}};
    return c;
}

void test_stream_reads() {
    using R = dw4::DiscDrive::Rotation;
    // The drive model reproduces Dolphin's read durations.
    {
        dw4::DiscDrive drive;
        drive.rotation = R::dolphin;
        drive.clock_offset = kCaptureClock;
        const auto c = stagbs01();
        const std::uint64_t ecc = 0x8000, first = c.last_offset - c.last_offset % ecc;
        const std::uint64_t last = (c.last_offset + c.last_length + ecc - 1) / ecc * ecc;
        drive.buffer_start_offset = last;
        drive.buffer_end_offset = std::max(last + 0x100000 - 2 * ecc, first + 0x100000);
        drive.buffer_start_time = c.last_done;
        drive.buffer_end_time = c.last_done + std::int64_t(486000000.0 * dw4::DiscDrive::raw_read_time(last, drive.buffer_end_offset - last));
        // The capture's first read: 51200 bytes at ndmw sector 31987 issued at
        // tick 38404059708, its interrupt 56740836 ticks (116.751 ms) later;
        // the head position comes from the buffer of the read before (whose
        // own start is not in the log), within 0.05 ms.
        const auto ticks = drive.read(kNdmw + 31987ull * 2048, 51200, 38404059708);
        if (std::llabs(ticks - 56740836) >= 24300) std::cerr << "  first read " << ticks << " ticks\n";
        check(std::llabs(ticks - 56740836) < 24300, "A seek read takes Dolphin's time");
    }
    const auto bs01 = stagbs01();
    const auto exact = replay(bs01, R::dolphin, 80);
    check_events(exact, {{1, 0, 25, 31987}, {8, 1, 25, 4}, {12, 0, 0, 0}, {15, 0, 28, 32012}, {19, 1, 0, 0},
                         {21, 1, 28, 29}, {27, 2, 13, 132044}, {34, 1, 13, 57}, {38, 2, 0, 0}, {42, 3, 13, 131947},
                         {49, 2, 2, 132057}, {53, 3, 0, 0}, {54, 1, 9, 70}, {60, 0, 9, 32040}, {65, 2, 2, 132059},
                         {72, 1, 8, 79}, {80, 0, 9, 32049}},
                 "stagBS01: every read and PLAYING of the capture");
    check(all_ready(exact) == 53, "stagBS01: ISALLREADY at field 53 as in Dolphin");
    check(all_ready(replay(bs01, R::mean, 80)) == 53, "stagBS01 with the mean rotation: field 53");

    Capture q1; // dngQ1B1e: the ambience at first.lua, QS_START after the old BGM's fade
    q1.first_lua = 36887902920;
    q1.phase = 0.003;
    q1.last_offset = 0x39709a98;
    q1.last_length = 16384;
    q1.last_done = 36847443336;
    q1.commands = {{0.046, 1, 1}, {0.048, 1, 0, adx(10746, 2533, 2048, 2, 48000, 4609239, 1, 2533)}, {0.0481, 1, 2},
                   {11.020, 0, 1}, {11.021, 0, 0, adx(85871, 134, 40, 2, 48000, 243645)}};
    const auto q1_exact = replay(q1, R::dolphin, 40);
    check_events(q1_exact, {{1, 1, 25, 10746}, {10, 1, 28, 10771}, {12, 0, 25, 85871}, {13, 1, 0, 0},
                            {18, 1, 8, 10799}, {22, 0, 0, 0}, {24, 0, 28, 85896}, {35, 1, 8, 10807}},
                 "dngQ1B1e: every read and PLAYING of the capture");
    check(all_ready(q1_exact) == 22, "dngQ1B1e: ISALLREADY at field 22 as in Dolphin");
    check(all_ready(replay(q1, R::mean, 40)) == 23, "dngQ1B1e with the mean rotation: field 23");

    Capture bs02; // stagBS02: the old BGM's read is still on the drive until field 4
    bs02.first_lua = 37236481524;
    bs02.phase = 0.020;
    bs02.last_offset = 0x39714a98;
    bs02.last_length = 18432;
    bs02.last_done = 37262004324;
    bs02.inflight_slot = 0;
    bs02.commands = {{0.123, 0, 1}, {0.126, 1, 1}, {0.127, 1, 0, adx(2153, 2149, 2048, 2, 48000, 3908769, 1, 2149)},
                     {0.1271, 1, 2}, {0.128, 2, 1}, {0.129, 2, 0, adx(132138, 36, 40, 1, 24000, 128969)},
                     {0.131, 0, 1}, {0.132, 0, 0, adx(34676, 2696, 1556, 2, 48000, 4904908, 655, 2615)}, {48.014, 0, 2}};
    // The ambience's refill after ISALLREADY comes with the DSP's frame
    // (49 as in the capture; a progress counted per tick had it at 48).
    const auto bs02_exact = replay(bs02, R::dolphin, 56);
    check_events(bs02_exact, {{4, 1, 25, 2153}, {12, 0, 25, 34676}, {16, 1, 0, 0}, {18, 1, 28, 2178}, {22, 0, 0, 0},
                              {25, 0, 28, 34701}, {31, 1, 13, 2206}, {38, 2, 13, 132138}, {48, 2, 0, 0},
                              {49, 1, 8, 2219}, {55, 0, 9, 34729}},
                 "stagBS02: every read and PLAYING of the capture");
    check(all_ready(bs02_exact) == 48, "stagBS02: ISALLREADY at field 48 as in Dolphin");
    check(all_ready(replay(bs02, R::mean, 60)) == 49, "stagBS02 with the mean rotation: field 49");

    // A one-shot shorter than two decode units still becomes PLAYING (the
    // decoder's end, 8029BED8), so ISREADY on it cannot hang a demo.
    {
        dw4::CriStreams cri;
        cri.start(2, adx(132138, 1, 40, 1, 24000, 500));
        bool playing = false;
        for (int k = 1; k <= 60 && !playing; ++k) {
            cri.tick(std::int64_t(k) * dw4::CriStreams::kFieldTicks);
            playing = cri.playing(2);
        }
        check(playing, "A very short one-shot becomes PLAYING");
        check(!cri.reading(), "Its single read is done");
        // ... then ends decoding (status 4) and plays out (status 5).
        int status = 3;
        for (int k = 61; k <= 200 && status != 5; ++k) {
            cri.tick(std::int64_t(k) * dw4::CriStreams::kFieldTicks);
            status = cri.status(2);
        }
        check(status == 5, "A one-shot plays out (ADXT status 5)");
    }
}

// The DSP's progress as the renderer reads it (r29 captures: renderer reads
// of the char-select streams, 802A733C). A voice plays from the first AX
// frame boundary after the renderer switched it on; the CPU copy of its
// address shows the frames finished before the last boundary, 240 samples
// each for 48 kHz.
void test_dsp_frames() {
    constexpr std::int64_t P = dw4::CriStreams::kAxFrameTicks, F = dw4::CriStreams::kFieldTicks;
    const auto ambience = adx(4, 2149, 2048, 2, 48000, 3908784, 1, 2149);
    // Switched on by the ADXT server (status 3) 0.469 frame after a boundary:
    // the next servers read 480, 1440, 2160 and 2880 samples.
    {
        dw4::CriStreams cri;
        cri.start(1, ambience);
        std::int64_t t0 = 0;
        for (int k = 1; k <= 200 && !t0; ++k) {
            cri.tick(std::int64_t(k) * F);
            if (cri.playing(1)) t0 = std::int64_t(k) * F;
        }
        check(t0 != 0, "The ambience becomes PLAYING");
        cri.ax_phase = t0 - P * 469 / 1000;
        const std::uint64_t want[] = {0, 480, 1440, 2160, 2880};
        bool same = true;
        for (int k = 0; k < 5; ++k) same = same && cri.ring_position(1, t0 + k * F) == want[k];
        check(same, "A voice started by the server shows whole AX frames, one behind");
    }
    // Resumed by the game update (ADXT_Pause 0) before a server 0.528 frame
    // after a boundary: 480, 1200, 2160. The pause before dropped what the
    // ARAM ring held (802A7534 resets its SJ).
    {
        dw4::CriStreams cri;
        cri.pause(0, true, 0);
        cri.start(0, ambience);
        std::int64_t t = 0;
        for (int k = 1; k <= 200 && !t; ++k) {
            cri.tick(std::int64_t(k) * F);
            if (cri.playing(0)) t = std::int64_t(k) * F;
        }
        check(t != 0, "A paused stream still becomes PLAYING");
        for (int k = 0; k < 10; ++k) cri.tick(t += F);
        check(cri.ring_position(0, t) == 0, "Nothing plays while paused");
        t += F;
        cri.ax_phase = t - P * 528 / 1000;
        cri.pause(0, false, t);
        const std::uint64_t want[] = {480, 1200, 2160};
        bool same = true;
        for (int k = 0; k < 3; ++k) same = same && cri.ring_position(0, t + k * F) == want[k];
        check(same, "A resumed voice shows the frames since the game update's switch-on");
        cri.tick(t);
        cri.pause(0, true, t + F);
        const auto& h = cri.handles[0];
        check(h.moved == h.reclaimed && h.dsp_start < 0, "A pause drops the ARAM ring's unplayed samples");
    }
}

// The stage loader's reads (r15 captures with the loader states, fix_r15sound
// runs d_bs02 / d_bs03 / d_bs01): the last queued file (LH_pack) is read right
// after the old BGM's refill, and the refill after it seeks back; the drive
// model reproduces both durations with Dolphin's rotation.
void test_loader_reads() {
    using R = dw4::DiscDrive::Rotation;
    struct Read {
        double issue, done;          // seconds (TB) of the read and its interrupt
        std::uint64_t offset, length;
    };
    struct Case {
        const char* name;
        Read before, loader, refill;
    };
    const Case cases[] = {
        {"stagBS02", {76.218064, 76.308324, 0x39710298, 18432}, {76.318190, 76.420706, 0x52afda98, 22528},
         {76.568418, 76.670791, 0x39714a98, 18432}},
        {"stagBS03", {76.568418, 76.669083, 0x39714a98, 18432}, {76.685140, 76.809237, 0x52e6d298, 34816},
         {76.902084, 77.007638, 0x39719298, 18432}},
        {"stagBS01", {78.570419, 78.677078, 0x3972f298, 16384}, {78.687133, 78.810628, 0x52766298, 28672},
         {78.870716, 78.981720, 0x39733298, 16384}},
    };
    auto ticks = [](double seconds) { return std::int64_t(seconds * 486000000.0); };
    // The drive after a seek read (DVDInterface: it reads ahead 1 MiB).
    auto after = [&](dw4::DiscDrive& drive, const Read& r) {
        const std::uint64_t ecc = 0x8000, first = r.offset - r.offset % ecc;
        const std::uint64_t last = (r.offset + r.length + ecc - 1) / ecc * ecc;
        drive.buffer_start_offset = last;
        drive.buffer_end_offset = std::max(last + 0x100000 - 2 * ecc, first + 0x100000);
        drive.buffer_start_time = ticks(r.done);
        drive.buffer_end_time = ticks(r.done) + std::int64_t(486000000.0 * dw4::DiscDrive::raw_read_time(last, drive.buffer_end_offset - last));
    };
    for (const auto& c : cases) {
        dw4::DiscDrive drive;
        drive.rotation = R::dolphin;
        drive.clock_offset = kCaptureClock;
        after(drive, c.before);
        const auto loader = drive.read(c.loader.offset, c.loader.length, ticks(c.loader.issue));
        if (std::llabs(loader - (ticks(c.loader.done) - ticks(c.loader.issue))) >= 24300)
            std::cerr << "  " << c.name << " loader read " << loader << " ticks\n";
        check(std::llabs(loader - (ticks(c.loader.done) - ticks(c.loader.issue))) < 24300, "The loader's last read takes Dolphin's time");
        after(drive, c.loader);
        const auto refill = drive.read(c.refill.offset, c.refill.length, ticks(c.refill.issue));
        if (std::llabs(refill - (ticks(c.refill.done) - ticks(c.refill.issue))) >= 24300)
            std::cerr << "  " << c.name << " refill " << refill << " ticks\n";
        check(std::llabs(refill - (ticks(c.refill.done) - ticks(c.refill.issue))) < 24300, "The BGM refill after it seeks back in Dolphin's time");
    }

    // A file is read in requests of at most 64 sectors; the next request
    // follows one field after a booking.
    {
        dw4::CriStreams cri;
        cri.load(kNdmw + 300000ull * 2048, 173);
        std::vector<std::pair<int, std::uint32_t>> issued;
        for (int k = 1; k <= 40 && (cri.loader.active || cri.loader.busy); ++k) {
            const int before = cri.inflight.slot;
            cri.tick(std::int64_t(k) * dw4::CriStreams::kFieldTicks);
            if (cri.inflight.slot == dw4::CriStreams::kLoaderSlot && before != dw4::CriStreams::kLoaderSlot)
                issued.push_back({k, cri.loader.request});
            if (cri.loader.booked) issued.push_back({k, 0});
        }
        // 1: a seek read (several fields), then reads out of the drive's
        // read-ahead buffer, each requested in the field after its booking.
        bool ok = issued.size() == 6 && issued[0].second == 64 && issued[2].second == 64 && issued[4].second == 45;
        for (std::size_t i = 1; ok && i + 1 < issued.size(); i += 2)
            ok = issued[i].second == 0 && issued[i + 1].first == issued[i].first + 1;
        if (!ok)
            for (const auto& [k, n] : issued) std::cerr << "  loader tick " << k << ": " << (n ? "read " : "booked ") << n << '\n';
        check(ok, "The loader reads 64 + 64 + 45 sectors, one field after each booking");
        check(!cri.loader.active && !cri.loader.busy, "The loader's file is read");
    }
    // The stream readers come first: the loader waits for a stream's read and
    // starts in the field that books it.
    {
        dw4::CriStreams cri;
        cri.start(0, adx(95786, 2689, 2048, 2, 48000, 4892980, 1, 2686));
        cri.tick(dw4::CriStreams::kFieldTicks);      // binds the stream
        cri.tick(2 * dw4::CriStreams::kFieldTicks);  // its first read
        check(cri.inflight.slot == 0, "The stream reads first");
        cri.load(kNdmw + 302597ull * 2048, 11);
        int loader_tick = 0, stream_booked = 0;
        for (int k = 3; k <= 40 && !loader_tick; ++k) {
            const bool stream_busy = cri.handles[0].busy;
            cri.tick(std::int64_t(k) * dw4::CriStreams::kFieldTicks);
            if (stream_busy && !cri.handles[0].busy && !stream_booked) stream_booked = k;
            if (cri.inflight.slot == dw4::CriStreams::kLoaderSlot) loader_tick = k;
        }
        check(stream_booked != 0 && loader_tick == stream_booked, "The loader starts in the field the stream's read is booked");
    }
}

// The fade of a Cri sound entry (8031D5D0 / 8031DCB0).
void test_fades() {
    // A replaced BGM (1.0 -> 0.0 in 10 frames) of volume -10 under the base
    // volume -30: 59 table steps over 10 fields; the driver (802F53B8) stops
    // it once the volume is -210 or less, after 6 fields (ST_CHARA in the
    // r14 captures).
    {
        dw4::CriFade fade;
        fade.start(1.0f, 0.0f, 10.0f);
        check(fade.running && fade.position == 0.0f && std::fabs(fade.increment - 5.9f) < 1e-6f, "A fade-out runs over the whole table");
        const int want[] = {-80, -116, -148, -175, -198, -219};
        bool same = true;
        for (const int v : want) same = same && fade.step(-10, -30) == v;
        check(same, "The fade-out's volumes follow the table 804E05C8");
        int steps = 6, v = 0;
        while (fade.running && steps < 20) {
            v = fade.step(-10, -30);
            ++steps;
        }
        check(steps == 9 && v == -999, "A fade to silence ends with -999 once the table reaches 0");
    }
    // A fade-in (cmQUAKE 0.0 -> 1.0 in 150 frames): the up table from its
    // first entry; it ends at full volume.
    {
        dw4::CriFade fade;
        fade.start(0.0f, 1.0f, 150.0f);
        check(fade.position == 0.0f && std::fabs(fade.increment - 59.0f / 150.0f) < 1e-6f, "A fade-in runs over the up table");
        int v = 0, steps = 0;
        while (fade.running && steps < 200) {
            v = fade.step(-40, -30);
            ++steps;
        }
        check(v == -70 && !fade.running && steps < 150, "The fade-in ends at the sound's volume");
    }
    // A partial fade (VOLUMECTL2 1.0 -> 0.5): it ends on the first level at
    // or below the target, the volume then stays.
    {
        dw4::CriFade fade;
        fade.start(1.0f, 0.5f, 30.0f);
        int v = 0, steps = 0;
        while (fade.running && steps < 100) {
            v = fade.step(0, -30);
            ++steps;
        }
        check(!fade.running && v == int(std::fabs(-210.0f * 0.49000001f)) - 240, "A partial fade ends at the target's level");
    }
}
// The fade of an sfp entry (8031DDB8, VOLUMECTL DOWN of a sound
// subcommand's end call): the down table's level as the SEQ volume
// int(127 * level), the fade ends once that is at or below int(127 * to).
void test_seq_fades() {
    // 1.0 -> 0.0 over 10 frames (803106C0): table indices 5, 11, 17, ...
    // 53; a fade to 0.0 ends with 0. The driver (802F53B8) stops the entry
    // at 12 or less, after the seventh field.
    {
        dw4::CriFade fade;
        fade.start(1.0f, 0.0f, 10.0f);
        const int want[] = {102, 81, 62, 45, 31, 19, 10, 3};
        bool same = true;
        for (const int v : want) same = same && fade.step_seq() == v && fade.running;
        check(same, "The sfp fade's SEQ volumes follow the table 804E05C8");
        check(fade.step_seq() == 0 && !fade.running, "An sfp fade to 0.0 ends with 0");
        dw4::CriFade again;
        again.start(1.0f, 0.0f, 10.0f);
        int fields = 0;
        while (again.step_seq() > 12) ++fields;
        check(fields + 1 == 7, "The entry stops in the seventh field");
    }
    // 1.0 -> 0.5: it ends on the first SEQ volume at or below int(127 *
    // 0.5) = 63 and keeps it (0.49 of the table: 62).
    {
        dw4::CriFade fade;
        fade.start(1.0f, 0.5f, 10.0f);
        int v = 0, steps = 0, before = 127;
        while (fade.running && steps < 100) {
            before = v ? v : before;
            v = fade.step_seq();
            ++steps;
        }
        check(!fade.running && v == 62 && before > 63, "A partial sfp fade ends at the first volume at or below the target's");
    }
    // 8031DDB8 compares only downwards: a fade up ends at its first step.
    {
        dw4::CriFade fade;
        fade.start(0.0f, 1.0f, 10.0f);
        check(fade.step_seq() == 24 && !fade.running, "An sfp fade up ends at once (0.19 of the up table)");
    }
}

// The rate the AX voice plays a stream at (802A6C38): the 16.16 ratio of
// rate / 32000 (mode 0) or 1124 (rate + 1) / 1125 / 32000 (mode 1, Sofdec)
// at the DSP's 54 MHz / 1686 output rate.
void test_rna_rate() {
    check(dw4::rna_src_ratio(48000, false) == 0x18000u && std::fabs(dw4::rna_rate(48000, false) - 48042.70) < 0.01,
          "A 48 kHz beCri stream plays at 48042.7 Hz");
    check(dw4::rna_src_ratio(24000, false) == 0xC000u && std::fabs(dw4::rna_rate(24000, false) - 24021.35) < 0.01,
          "A 24 kHz beCri stream plays at 24021.35 Hz");
    check(dw4::rna_src_ratio(48000, true) == 0x17FA9u && std::fabs(dw4::rna_rate(48000, true) - 48000.18) < 0.01,
          "The movie's 48 kHz sound plays at 48000.18 Hz");
    check(dw4::rna_src_ratio(32000, false) == 0x10000u && dw4::rna_src_ratio(44100, false) == 0x160CCu,
          "The ratio's integer part and its fraction (f << 8) / 125");
}

// The r22 captures (fix_r22load2, dngQ6B01 after the title): the descriptor
// read, the BGM refill, the loading card, the next refill and the first chunk
// of the preliminary member, each after a seek read. Dolphin's clock runs
// 23.8e6 ticks ahead of their time base * 12; with that offset its rotation
// formula gives every duration to 0.7 us, without it one read lands a half
// turn off, and the mean rotation misses all four by 7 to 17 ms.
// PC fast loading: the drive takes no time (DiscDrive::instant), so a read
// ends at the next server tick and the streams of a stage start are ready
// within a few fields instead of the original's 22-53.
void test_instant_drive() {
    using R = dw4::DiscDrive::Rotation;
    dw4::DiscDrive drive;
    drive.instant = true;
    const auto before = drive;
    check(drive.read(kNdmw + 31987ull * 2048, 51200, 38404059708) == 0, "An instant read takes no time");
    check(drive.buffer_start_offset == before.buffer_start_offset && drive.buffer_end_offset == before.buffer_end_offset &&
              drive.buffer_start_time == before.buffer_start_time && drive.buffer_end_time == before.buffer_end_time,
          "An instant read leaves the read-ahead buffer alone");
    const auto bs01 = stagbs01();
    const auto fast = replay(bs01, R::dolphin, 80, true);
    const int ready = all_ready(fast);
    if (ready > 12) std::cerr << "  instant stagBS01 ISALLREADY at field " << ready << '\n';
    check(ready > 0 && ready <= 12, "stagBS01 with an instant drive: ISALLREADY within 12 fields (53 with the drive)");
    // One read per tick: every event is a tick of its own and in the original order of the streams.
    bool one_per_tick = true;
    for (std::size_t i = 1; i < fast.size(); ++i)
        if (fast[i].sectors && fast[i - 1].sectors && fast[i].tick == fast[i - 1].tick) one_per_tick = false;
    check(one_per_tick, "The instant drive still serves one read at a time");
    // The loader's 64-sector requests: one tick each, the next in the tick after the booking.
    dw4::CriStreams cri;
    cri.drive.instant = true;
    cri.load(kNdmw + 300000ull * 2048, 173);
    int k = 1;
    for (; k <= 40 && (cri.loader.active || cri.loader.busy); ++k) cri.tick(std::int64_t(k) * dw4::CriStreams::kFieldTicks);
    check(!cri.loader.active && !cri.loader.busy && k - 1 == 6, "An instant drive reads 64 + 64 + 45 sectors in six ticks");
}

void test_rotation_clock() {
    using R = dw4::DiscDrive::Rotation;
    struct Read {
        std::int64_t issue;     // ticks (time base * 12)
        std::uint64_t offset, length;
        std::int64_t duration;  // to the DVD interrupt
    };
    const Read reads[] = {{36140296644, 0x3e0eea98, 4096, 35612556},   {36189013152, 0x39640a98, 16384, 49658580},
                          {36245807256, 0x2daeaa98, 131072, 62489004}, {36334937364, 0x39644a98, 16384, 61566744},
                          {36399746616, 0x4a5a2298, 131072, 66177060}};
    auto run = [&](R rotation, std::int64_t clock) {
        int matched = 0;
        for (std::size_t i = 0; i + 1 < std::size(reads); ++i) {
            const auto& r = reads[i];
            dw4::DiscDrive drive;
            drive.rotation = rotation;
            drive.clock_offset = clock;
            const std::uint64_t ecc = 0x8000, first = r.offset - r.offset % ecc;
            const std::uint64_t last = (r.offset + r.length + ecc - 1) / ecc * ecc;
            drive.buffer_start_offset = last;
            drive.buffer_end_offset = std::max(last + 0x100000 - 2 * ecc, first + 0x100000);
            drive.buffer_start_time = r.issue + r.duration;
            drive.buffer_end_time = drive.buffer_start_time +
                                    std::int64_t(486000000.0 * dw4::DiscDrive::raw_read_time(last, drive.buffer_end_offset - last));
            const auto& next = reads[i + 1];
            if (std::llabs(drive.read(next.offset, next.length, next.issue) - next.duration) < 1000) ++matched;
        }
        return matched;
    };
    check(run(R::dolphin, 23800000) == 4, "Dolphin's rotation with its clock gives the capture's four seek reads");
    check(run(R::dolphin, 0) == 3, "One of them is near a whole second: the clock matters");
    check(run(R::mean, 0) == 0, "The mean rotation misses them");
}
// The movie's reads (Sofdec, MOVIE PLAY): the open's ten header sectors are
// read from the next server pass and booked in the pass after the drive
// finished; the stream asks for its whole 365-sector ring at once, then
// refills what the demuxer freed once the ring holds less than 85 %, up to
// the ring's end; a close leaves a read on the drive to finish.
void test_movie_reads() {
    constexpr std::int64_t field = dw4::CriStreams::kFieldTicks;
    constexpr std::uint64_t sfd = 0x14010298; // area.afs entry 2 (AUS_opening.sfd)
    constexpr std::uint32_t sectors = 19684;
    dw4::CriStreams cri;
    cri.drive.rotation = dw4::DiscDrive::Rotation::mean;
    std::int64_t now = 100 * field;
    cri.movie_open(sfd, sectors);
    check(!cri.movie_header_read(), "The header is not read at the open");
    cri.tick(now);
    check(cri.inflight.slot == dw4::CriStreams::kMovieHeaderSlot, "The next pass reads the header");
    const std::int64_t done = cri.inflight.done;
    int fields = 0;
    while (!cri.movie_header_read() && fields < 30) {
        now += field;
        cri.tick(now);
        ++fields;
    }
    check(cri.movie_header_read() && now >= done && now - field < done, "The pass after the read's end books the header");
    check(cri.movie_written() == 0, "The header is not the stream's data");
    cri.movie_stream(now);
    check(cri.inflight.slot == dw4::CriStreams::kMovieSlot && cri.movie.request == dw4::CriStreams::kMovieRingSectors,
          "The stream's first read fills its ring in the same pass");
    cri.movie_stream(now);
    check(cri.movie.request == dw4::CriStreams::kMovieRingSectors && cri.movie.busy, "A second start changes nothing");
    while (cri.movie_written() == 0 && fields < 200) {
        now += field;
        cri.tick(now);
        ++fields;
    }
    check(cri.movie_written() == 365ull * 2048 && cri.movie.pos == 365, "The first read is booked");
    // Full ring: nothing asked until the demuxer frees more than 15 %.
    cri.movie_consume(50ull * 2048);
    now += field;
    cri.tick(now);
    check(cri.inflight.slot < 0, "A ring at 86 % waits");
    cri.movie_consume(60ull * 2048);
    now += field;
    cri.tick(now);
    check(cri.inflight.slot == dw4::CriStreams::kMovieSlot && cri.movie.request == 60, "The freed 60 sectors up to the ring's end");
    cri.movie_consume(1ull << 40);
    check(cri.movie.consumed == cri.movie.written, "The demuxer takes no more than arrived");
    cri.movie_close();
    check(cri.movie.busy && cri.movie.stopping && cri.inflight.slot == dw4::CriStreams::kMovieSlot, "A close keeps the read on the drive");
    for (int i = 0; i < 4; ++i) {
        now += field;
        cri.tick(now);
    }
    check(cri.inflight.slot < 0 && !cri.movie.busy && cri.movie_written() == 0, "The read ends without data and nothing follows");
    // A close, and a new open after it, while the open's header read is on
    // the drive: that read ends on its own reader (the loader's stream) and
    // books nothing, the movie stream is free at once, and the new open's
    // header is read after it and booked only once its own read ended.
    cri.movie_open(sfd, sectors);
    now += field;
    cri.tick(now);
    check(cri.inflight.slot == dw4::CriStreams::kMovieHeaderSlot, "The open reads its header");
    cri.movie_close();
    check(cri.movie.header_busy && cri.movie.header_stopping && !cri.movie.busy && !cri.movie.stopping,
          "A close leaves the header read to the header's reader, not the stream's");
    cri.movie_open(sfd, sectors);
    check(cri.movie.header_busy && cri.movie.header_stopping && !cri.movie_header_read(), "So does an open right after it");
    std::int64_t own_done = -1;
    for (fields = 0; !cri.movie_header_read() && fields < 60; ++fields) {
        now += field;
        cri.tick(now);
        if (own_done < 0 && !cri.movie.header_stopping && cri.inflight.slot == dw4::CriStreams::kMovieHeaderSlot) own_done = cri.inflight.done;
    }
    check(own_done >= 0 && cri.movie_header_read() && now >= own_done && now - field < own_done,
          "The new open's header is booked in the pass after its own read ended");
    check(!cri.movie.busy && cri.movie_written() == 0, "The stream has nothing to wait for");
    cri.movie_stream(now);
    check(cri.inflight.slot == dw4::CriStreams::kMovieSlot && cri.movie.request == dw4::CriStreams::kMovieRingSectors,
          "and its first read goes on the drive at once");
}
} // namespace

int main() {
    try {
        test_parse();
        test_rejects();
        test_stream_reads();
        test_dsp_frames();
        test_loader_reads();
        test_rotation_clock();
        test_instant_drive();
        test_fades();
        test_seq_fades();
        test_rna_rate();
        test_movie_reads();
    } catch (const std::exception& error) {
        std::cerr << "disc tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "disc tests passed (" << checks << " checks)\n";
    return 0;
}
