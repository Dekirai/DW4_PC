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
} // namespace

int main() {
    try {
        records();
        files();
        gci();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "save_data: " << checks << " checks passed\n";
    return 0;
}
