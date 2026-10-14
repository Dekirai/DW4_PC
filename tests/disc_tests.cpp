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
    }
}
} // namespace

int main() {
    try {
        test_parse();
        test_rejects();
        test_stream_reads();
    } catch (const std::exception& error) {
        std::cerr << "disc tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "disc tests passed (" << checks << " checks)\n";
    return 0;
}
