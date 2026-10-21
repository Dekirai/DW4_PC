#include "dw4/disc.hpp"
#include "dw4/disc_timing.hpp"

#include <algorithm>
#include <array>
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

std::vector<Event> replay(const Capture& c, dw4::DiscDrive::Rotation rotation, int ticks) {
    dw4::CriStreams cri;
    cri.drive.rotation = rotation;
    cri.drive.clock_offset = kCaptureClock;
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
    // (The ambience's next refill comes at 48 here, at 49 in the capture: the
    // DSP's progress is modelled to its AX frame on average only.)
    const auto bs02_exact = replay(bs02, R::dolphin, 48);
    check_events(bs02_exact, {{4, 1, 25, 2153}, {12, 0, 25, 34676}, {16, 1, 0, 0}, {18, 1, 28, 2178}, {22, 0, 0, 0},
                              {25, 0, 28, 34701}, {31, 1, 13, 2206}, {38, 2, 13, 132138}},
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
// The r22 captures (fix_r22load2, dngQ6B01 after the title): the descriptor
// read, the BGM refill, the loading card, the next refill and the first chunk
// of the preliminary member, each after a seek read. Dolphin's clock runs
// 23.8e6 ticks ahead of their time base * 12; with that offset its rotation
// formula gives every duration to 0.7 us, without it one read lands a half
// turn off, and the mean rotation misses all four by 7 to 17 ms.
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
} // namespace

int main() {
    try {
        test_parse();
        test_rejects();
        test_stream_reads();
        test_loader_reads();
        test_rotation_clock();
        test_fades();
    } catch (const std::exception& error) {
        std::cerr << "disc tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "disc tests passed (" << checks << " checks)\n";
    return 0;
}
