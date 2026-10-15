// A character on the memory card (docs/SAVES.md): GameSystem players to and
// from the NDMWSaveData records of dw4::save.
#include "dw4/engine/managers.hpp"
#include "dw4/save_data.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace dw4::engine {
namespace {
// A disk word: record | kind 4 << 12 | count << 16.
constexpr std::uint32_t kDiskKind = 4;

template <std::size_t N> void copy_indices(std::array<std::int32_t, N>& out, const std::vector<int>& list) {
    out.fill(-1);
    for (std::size_t i = 0; i < N && i < list.size(); ++i) out[i] = list[i];
}
template <std::size_t N> std::vector<int> indices(const std::array<std::int32_t, N>& in) {
    return {in.begin(), in.end()};
}
} // namespace

void GameSystem::link_common(Engine& engine, int slot) {
    if (slot != 0) return;
    const auto& p = players[0];
    engine.flags() = p.base_flags;
    flag_folder = p.base_flag_folder;
    engine.counters() = p.base_counters;
}

// 80364AEC after GAMERAM CURRENT2BASE: the player's record with its
// BASE_FLAG, BASE_FLAGFOLDER and BASE_COUNTER (0x80365278 on): player 1's
// are the common flags, flag folder and counters.
save::Record GameSystem::make_record(Engine& engine, int slot) {
    store_species(slot);
    const auto& p = players[std::size_t(std::clamp(slot, 0, 3))];
    save::Record r;
    r.blank = false;
    r.used = true;
    r.unique = p.unique;
    r.digimon = p.digimon;
    r.name = p.name.substr(0, 8);
    r.level = p.stats.level;
    r.hp = p.hp;
    r.max_hp = p.max_hp;
    r.mp = p.mp;
    r.max_mp = p.max_mp;
    r.xdata = p.xdata;
    r.bits = p.bits;
    r.devices.fill(-1);
    for (std::size_t i = 0; i < r.devices.size() && i < p.devices.size(); ++i) r.devices[i] = std::int32_t(p.devices[i].pack());
    for (std::size_t i = 0; i < r.disks.size(); ++i) {
        const auto& disk = p.disks[i];
        r.disks[i] = std::int32_t((std::uint32_t(disk.record) & 0x3FF) | kDiskKind << 12 | (std::uint32_t(disk.count) & 0xF) << 16);
    }
    for (const auto& [card, count] : p.cards)
        if (card >= 0 && card < int(r.cards.size())) r.cards[std::size_t(card)] = count;
    r.bank.fill(-1);
    for (std::size_t i = 0; i < r.bank.size() && i < p.bank_devices.size(); ++i) r.bank[i] = std::int32_t(p.bank_devices[i].pack());
    r.bank_bits = p.bank_bits;
    copy_indices(r.power_socket, p.power_socket);
    copy_indices(r.power_pocket, p.power_pocket);
    r.main_slot = p.main_slot.empty() ? -1 : p.main_slot[0];
    copy_indices(r.main_pocket, p.main_pocket);
    r.sub_slot = p.sub_slot.empty() ? -1 : p.sub_slot[0];
    const bool host = slot == 0;
    for (const auto& [flag, value] : host ? engine.flags() : p.base_flags)
        if (flag >= 0 && flag < int(r.flags.size())) r.flags[std::size_t(flag)] = value;
    for (const auto& [index, value] : host ? flag_folder : p.base_flag_folder)
        if (index >= 0 && index < int(r.flag_folder.size())) r.flag_folder[std::size_t(index)] = value;
    for (const auto& [index, value] : host ? engine.counters() : p.base_counters)
        if (index >= 0 && index < int(r.counters.size())) r.counters[std::size_t(index)] = value;
    for (const auto& [species, used] : p.base_isuse)
        if (species >= 0 && species < 16) r.base_isuse[std::size_t(species)] = used;
    r.base_level = p.base_level;
    r.base_exp = p.base_exp;
    r.base_skill = p.base_skill;
    r.base_upcnt = p.base_upcnt;
    return r;
}

// GAMESTART with a loaded record: the player takes the character over, the
// current form comes from its BASE_* entry (BASE2CURRENT); the common flags,
// flag folder and counters come from the host's record.
void GameSystem::apply_record(Engine& engine, int slot, const save::Record& r, bool common) {
    auto& p = players[std::size_t(std::clamp(slot, 0, 3))];
    const bool joined = p.joined;
    p = PlayerSlot{};
    p.joined = joined;
    p.unique = r.unique;
    p.digimon = r.digimon;
    p.name = r.name;
    p.xdata = r.xdata;
    p.bits = r.bits;
    for (const auto word : r.devices)
        if (word != -1) p.devices.push_back(ItemDevice::unpack(std::uint32_t(word)));
    for (std::size_t i = 0; i < r.disks.size(); ++i)
        if (r.disks[i] != -1)
            p.disks[i] = {int(std::uint32_t(r.disks[i]) & 0x3FF), int((std::uint32_t(r.disks[i]) >> 16) & 0xF)};
    for (std::size_t i = 0; i < r.cards.size(); ++i)
        if (r.cards[i]) p.cards[int(i)] = r.cards[i];
    for (const auto word : r.bank)
        if (word != -1) p.bank_devices.push_back(ItemDevice::unpack(std::uint32_t(word)));
    p.bank_bits = r.bank_bits;
    p.power_socket = indices(r.power_socket);
    p.power_pocket = indices(r.power_pocket);
    p.main_slot = {r.main_slot};
    p.main_pocket = indices(r.main_pocket);
    p.sub_slot = {r.sub_slot};
    for (std::size_t s = 0; s < 16; ++s)
        if (r.base_isuse[s]) p.base_isuse[int(s)] = r.base_isuse[s];
    p.base_level = r.base_level;
    p.base_exp = r.base_exp;
    p.base_skill = r.base_skill;
    p.base_upcnt = r.base_upcnt;
    // BASE2CURRENT: the current form is the "shinka" row of the record's
    // model name. The character select has the database and sets it at once.
    // A direct stage start reads a DW4_CARD_RECORD while boot.lua runs,
    // before the stage's usa_pack.igb is loaded: then the form stays open
    // (species -1) and the record's values stay as they are until the first
    // recalc_player with the tables (the player's GENERATE), as if the record
    // had been applied in the character select.
    p.stats.species = -1;
    if (auto* db = engine.get<DatabaseManager>())
        if (const int row = db->find_row("shinka", "MODEL_NAME", r.digimon); row >= 0) load_species(engine, slot, row);
    p.stats.level = r.level;
    p.hp = r.hp;
    p.max_hp = r.max_hp;
    p.mp = r.mp;
    p.max_mp = r.max_mp;
    p.stats.ready = true;
    // 803657D8: the record's BASE_FLAG, BASE_FLAGFOLDER and BASE_COUNTER
    // replace the new player data's (BASE_FLAG[960] included).
    p.base_flags.clear();
    for (std::size_t i = 0; i < r.flags.size(); ++i)
        if (r.flags[i]) p.base_flags[std::int32_t(i)] = r.flags[i];
    p.base_flag_folder.clear();
    for (std::size_t i = 0; i < r.flag_folder.size(); ++i)
        if (r.flag_folder[i]) p.base_flag_folder[int(i)] = r.flag_folder[i];
    p.base_counters.clear();
    for (std::size_t i = 0; i < r.counters.size(); ++i)
        if (r.counters[i]) p.base_counters[std::int32_t(i)] = r.counters[i];
    if (common) link_common(engine, slot);
    recalc_player(engine, slot);
}

// DW4_CARD_RECORD=FILE.gci:N (test runs): player 1 starts as record N of a
// GCI save instead of a new character.
bool GameSystem::load_test_record(Engine& engine, int slot) {
    const char* spec = std::getenv("DW4_CARD_RECORD");
    if (!spec || slot != 0) return false;
    std::string path(spec);
    int index = 0;
    if (const auto colon = path.rfind(':'); colon != std::string::npos && colon > 1) {
        index = std::atoi(path.c_str() + colon + 1);
        path.resize(colon);
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() < save::kGciEntrySize + save::kFileSize) return false;
    const auto records = save::read_file(std::span(bytes).subspan(save::kGciEntrySize));
    if (!records || index < 0 || index >= int(records->size()) || !(*records)[std::size_t(index)].used) return false;
    apply_record(engine, slot, (*records)[std::size_t(index)], true);
    engine.log("player 1 loaded card record " + std::to_string(index) + " (" + players[0].digimon + " level " +
               std::to_string(players[0].stats.level) + ")");
    return true;
}

} // namespace dw4::engine
