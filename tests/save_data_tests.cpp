#include "dw4/card_timing.hpp"
#include "dw4/save_data.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

save::Record sample() {
    save::Record r;
    r.blank = false;
    r.used = true;
    r.unique = 0x0C0D42D7;
    r.digimon = "p_dorumon";
    r.name = save::name_to_card("Tamer");
    r.level = 88;
    r.hp = r.max_hp = 13532;
    r.mp = r.max_mp = 12270;
    r.xdata = 530;
    r.bits = 2059734;
    r.devices.fill(-1);
    r.devices[0] = 0x10D;
    r.devices[1] = 0x141140A;
    r.disks.fill(-1);
    r.disks[0] = 0x24000;
    r.cards[5] = 1;
    r.bank.fill(-1);
    r.bank_bits = 7;
    r.power_socket = {1, 0, -1};
    r.power_pocket = {-1, -1, -1, -1, -1};
    r.main_slot = -1;
    r.main_pocket = {-1, -1, -1, -1, -1};
    r.sub_slot = -1;
    r.flags[0] = r.flags[1023] = 1;
    r.flag_folder[0] = 1;
    r.counters[1] = 6237962;
    r.base_isuse[3] = 1;
    r.base_level.fill(1);
    r.base_level[3] = 88;
    r.base_exp[3] = 1745778;
    for (auto& row : r.base_skill) row.fill(1);
    r.base_skill[3][3] = 1815;
    r.base_upcnt[3][2] = 2;
    return r;
}

bool same(const save::Record& a, const save::Record& b) { return save::encode(a) == save::encode(b); }

void records() {
    const auto r = sample();
    const auto bytes = save::encode(r);
    check(bytes[0] == 1 && bytes[4] == 0x0C && bytes[7] == 0xD7, "ISUSE and UNIQUE at 0 / 4");
    check(bytes[40] == 0xFF && bytes[41] == 0xFF && bytes[42] == 0xFF && bytes[43] == 0x34, "Name marker then full-width 'T'");
    check(bytes[75] == 88 && bytes[100 + 2] == 0x01 && bytes[100 + 3] == 0x0D, "LEVEL at 72, first device at 100");
    check(bytes[792] == 1 && bytes[1815] == 1 && bytes[1816] == 1, "BASE_FLAG spans 792..1815, folder at 1816");
    const auto back = save::decode(bytes);
    check(same(r, back) && back.digimon == "p_dorumon" && save::name_from_card(back.name) == "Tamer", "Record round trip");
    const auto zero = save::encode(save::Record{});
    check(std::all_of(zero.begin(), zero.end(), [](std::uint8_t b) { return b == 0; }) && save::decode(zero).blank,
          "A blank record stays all zero");
    bool threw = false;
    try { save::decode(std::span(bytes).first(100)); }
    catch (const std::runtime_error&) { threw = true; }
    check(threw, "Short record rejected");
}

void files() {
    std::vector<std::uint8_t> header(save::kHeaderSize, 0);
    header[0] = 'D';
    std::vector<save::Record> records(save::kRecordsPerCopy);
    records[0] = sample();
    auto file = save::write_file(header, records);
    check(file.size() == save::kFileSize && file[0] == 'D', "File size and header");
    check(file[0x2004 + 3] == 4 && file[0xC004 + 3] == 4, "Both copies carry the version word");
    const auto read = save::read_file(file);
    check(read && read->size() == save::kRecordsPerCopy && same((*read)[0], records[0]) && !(*read)[1].used, "File round trip");
    file[0x2000 + 100] ^= 0x55; // spoil the first copy: the second one is read
    const auto fallback = save::read_file(file);
    check(fallback && same((*fallback)[0], records[0]), "A failed copy falls back to the other");
    file[0xC000 + 100] ^= 0x55;
    check(!save::read_file(file), "Both copies failing reads nothing");
}

void gci() {
    save::GciEntry entry;
    entry.modified = 0x324C22A2;
    const auto bytes = save::encode_gci_entry(entry);
    check(bytes[0] == 'G' && bytes[6] == 0xFF && bytes[45] == 0 && bytes[47] == 0x40 && bytes[57] == 11, "GCI entry layout");
    const auto back = save::decode_gci_entry(bytes);
    check(back.file_name == "NDMWSaveData" && back.modified == entry.modified && back.blocks == 11, "GCI entry round trip");
}

// One request stepped once per NTSC field; the updates until it is done.
int run(CardRequest& request, CardChannel& channel, CardSlot& card, std::int64_t& now) {
    for (int updates = 1; updates < 1000; ++updates) {
        if (request.update(channel, card, now)) return updates;
        now += 8108100;
    }
    return -1;
}

void card_timing() {
    check(card::kReadByteTicks == 926 && card::kWriteByteTicks == 4937, "Dolphin's memory card byte times");
    check(card::kSectorTicks == 40768904 && card::kMountTicks == 37928960, "Sector program and mount times");
    CardSlot empty;
    empty.present = true;
    empty.free_blocks = 2043;
    empty.free_files = 127;
    CardSlot saved = empty;
    saved.file = true;
    saved.free_blocks -= 11;
    saved.free_files -= 1;
    CardChannel channel;
    std::int64_t now = 0;
    // The empty card at the boot check: USE_CHECK, FILEFIND, GETFREESIZE end
    // in their 9th / 11th / 12th update (Dolphin: the window +32 fields
    // over one-update requests).
    auto measure = [&](CardRequest::Kind kind, CardSlot card, int& result) {
        CardRequest request(kind, 0);
        const int updates = run(request, channel, card, now);
        result = request.result();
        return updates;
    };
    int result = 0;
    check(measure(CardRequest::kUseCheck, empty, result) == 9 && result == 2, "USE_CHECK mounts in 9 updates");
    check(measure(CardRequest::kFileFind, empty, result) == 11 && result == 4, "FILEFIND without the file");
    check(measure(CardRequest::kFileFind, saved, result) == 12 && result == 2, "FILEFIND with the file unmounts");
    {
        CardRequest request(CardRequest::kGetFreeSize, 0);
        CardSlot card = empty;
        check(run(request, channel, card, now) == 12 && request.result() == 2 && request.free_bytes() == 2043u * 8192 &&
                  request.free_files() == 127,
              "GETFREESIZE");
    }
    // READ_MEDIA: the mount and both 40 KiB copies (Dolphin: +29 over one
    // update); a file whose copies fail their check reports 8.
    check(measure(CardRequest::kReadMedia, saved, result) == 29 && result == 2, "READ_MEDIA reads both copies");
    CardSlot broken_data = saved;
    broken_data.data_valid = false;
    check(measure(CardRequest::kReadMedia, broken_data, result) == 29 && result == 8, "READ_MEDIA of broken copies");
    // WRITE_MEDIA / FILEMAKE stay inside the menus' 3 s minimum.
    {
        CardRequest request(CardRequest::kWriteMedia, 0);
        CardSlot card = saved;
        const int updates = run(request, channel, card, now);
        check(updates > 80 && updates < 180 && request.result() == 2 && request.stalled() == card::kStatusTicks,
              "WRITE_MEDIA: copies, header and the synchronous status");
    }
    {
        CardRequest request(CardRequest::kFileMake, 0);
        CardSlot card = empty;
        const int updates = run(request, channel, card, now);
        check(updates > 90 && updates < 180 && request.result() == 2 && card.file && card.free_blocks == 2032, "FILEMAKE");
    }
    {
        CardRequest request(CardRequest::kFileMake, 0);
        CardSlot card = empty;
        card.free_blocks = 10;
        check(run(request, channel, card, now) > 0 && request.result() == 3 && !card.file, "FILEMAKE without room");
    }
    // No card: three probes; an unformatted card mounts broken (7) until a
    // format.
    CardSlot none;
    check(measure(CardRequest::kUseCheck, none, result) == 4 && result == 12, "USE_CHECK without a card");
    check(measure(CardRequest::kFileFind, none, result) == 5 && result == 12, "FILEFIND without a card");
    CardSlot unformatted = empty;
    unformatted.formatted = false;
    check(measure(CardRequest::kUseCheck, unformatted, result) == 9 && result == 7, "An unformatted card is broken");
    {
        CardRequest request(CardRequest::kFormat, 0);
        CardSlot card = unformatted;
        check(run(request, channel, card, now) > 0 && request.result() == 2 && card.formatted, "FORMAT");
    }
    check(!channel.attached, "Requests unmount");
}
} // namespace

int main() {
    try {
        records();
        files();
        gci();
        card_timing();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "save_data: " << checks << " checks passed\n";
    return 0;
}
