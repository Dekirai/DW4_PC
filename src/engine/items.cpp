#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdlib>

namespace dw4::engine {
namespace {
// 803B0B9C: equipment rank from player 1's level (1 in 620 draws is rank 5 at
// low levels; higher levels shift every draw down).
int roll_rank(int level) {
    const int x = int(fight::random() % 620u) + 1 - (level / 10 + 10) / 10;
    if (x == 1) return 5;
    if (x <= 3) return 4;
    if (x <= 7) return 3;
    if (x <= 15) return 2;
    if (x <= 31) return 1;
    return 0;
}

// 803B0AB4: equipment value; one draw in 6000 beats the level, half of the
// draws give nothing.
int roll_value(int level) {
    const int a = int(fight::random() % 6000u) + 1;
    const int b = int(fight::random() % std::uint32_t(std::max(level, 1))) + 1;
    const int value = a == 1 ? level + 1 : a <= 3000 ? b - a / 10 : 0;
    return std::max(value, 0);
}

// 803B07E0: name colour by value; disks, chips and the "none" records stay grey.
Vec3f name_color(int table, const ItemDevice& device) {
    if (table < 3) return {0.9f, 0.9f, 0.9f};
    return device_color(device);
}

// DW4_TRACE_ITEM=1 logs the item name labels going on and off.
void trace_label(Engine& engine, const ModelCtrlManager::ModelRam& ram, int line, const char* what) {
    static const bool on = std::getenv("DW4_TRACE_ITEM") != nullptr;
    if (on) engine.log("item label " + ram.info_name + "#" + std::to_string(ram.index) + " " + what + " line " + std::to_string(line) + " in " + ram.seq_name);
}

Packet text_packet(int slot, std::string argument = {}) {
    static const char* lines[4] = {"font00", "font01", "font02", "font03"};
    Packet packet;
    packet.strings.emplace_back("ITNAMEFONT");
    packet.strings.emplace_back(lines[std::clamp(slot, 0, 3)]);
    if (!argument.empty()) packet.strings.emplace_back(std::move(argument));
    return packet;
}
} // namespace

Vec3f device_color(const ItemDevice& device) {
    const Vec3f grey{0.9f, 0.9f, 0.9f};
    if (device.type >= 3) return grey;
    if ((device.type == 0 && device.record == 276) || (device.type == 1 && device.record == 31) ||
        (device.type == 2 && device.record == 44))
        return grey;
    if (device.value >= 1000) return {1.0f, 0.0f, 0.9f};
    if (device.value >= 501) return {1.0f, 0.6f, 0.1f};
    if (device.value >= 101) return {0.9f, 0.8f, 0.1f};
    if (device.value >= 31) return {0.4f, 1.0f, 0.6f};
    if (device.value >= 1) return {0.1f, 0.9f, 1.0f};
    return grey;
}

const char* device_table(const ItemDevice& device) {
    static const char* tables[4] = {"weapon", "armor1", "armor2", "abChip"};
    return tables[std::clamp(device.type, 0, 3)];
}

void GameSystem::PlayerSlot::remove_device(int index) {
    if (index < 0 || index >= int(devices.size())) return;
    devices.erase(devices.begin() + index);
    for (auto* list : {&power_socket, &power_pocket, &main_slot, &main_pocket, &sub_slot})
        for (int& entry : *list) {
            if (entry == index) entry = -1;
            else if (entry > index) --entry;
        }
}

// Drop GENERATE (803AAE8C): weapons and armor1 roll rank and value, armor2 a
// value only, ability chips nothing; the level is player 1's LEVEL.
ItemDevice roll_item_device(int table, int record, int level) {
    ItemDevice device;
    device.type = std::clamp(table - 3, 0, 3);
    device.record = record;
    if (table == 3 || table == 4) device.rank = roll_rank(level);
    if (table >= 3 && table <= 5) device.value = roll_value(level);
    return device;
}

// KIND_ITEM (803AB5AC, vtable +A0): the class update the control step
// 80309168 runs only while the sequence has a CTRL mode and _isOnCtrl is set
// (0x8030918C; the drop items' SQ_ENT2, SQ_WAIT and SQ_BODING have "isLive",
// SQ_ENTR, SQ_EXIT and SQ_DDMG none). 8031C67C 'PLAYER' 'NEAR' picks the
// nearest player (smallest squared distance, the first of equals) and the
// item counts as near below 8 units (0x803AB6EC..0x803AB71C). Instant items
// (insItem: bits, X data; +F8 clear) are taken on contact; the others show
// their name for one near player and can then be picked up by that player.
void ModelCtrlManager::update_item(Engine& engine, ModelRam& ram) {
    if (!ram.node) return;
    const auto& m = ram.node->matrix;
    int nearest = -1;
    float best = 64.0f;
    for (const auto& other : rams_) {
        if (!other->is_player || !other->generated || !other->node) continue;
        const auto& o = other->node->matrix;
        const float dx = o(3, 0) - m(3, 0), dy = o(3, 1) - m(3, 1), dz = o(3, 2) - m(3, 2);
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best) { best = d2; nearest = other->index; }
    }
    if (ram.item_table != 1) {
        // The label state (RAM +0x18, 0x803AB81C): 0 shows a label for the
        // near player when that player's line is free (0x803AB838..0x803ABA44),
        // 1 keeps it on its owner and goes on in the same update.
        if (ram.item_label < 0 && nearest >= 0 && !item_labels_[std::size_t(std::clamp(nearest, 0, 3))])
            show_item_label(engine, ram, nearest);
        if (ram.item_label >= 0) update_item_label(engine, ram);
        return;
    }
    if (nearest < 0) return;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    // 803B13F8: record 0 bits, 1..3 X data, 4..25 status ups (UPCNT, then
    // RECALCPLAYER); UP is the record's amount.
    auto& slot = sys->players[std::size_t(std::clamp(nearest, 0, 3))];
    const int record = ram.item_record;
    int up = 0;
    if (auto* db = engine.get<DatabaseManager>()) up = db->number("insItem", record, "UP").value_or(0);
    ram.item_table = -1;
    select_sequence(engine, ram, "SQ_DDMG");
    if (record == 0) {
        // Bits by player 1's level band, scaled by player 1's BITRITSU.
        static const int bands[][3] = {{18, 10, 55}, {38, 55, 100}, {59, 87, 195}, {93, 162, 350},
                                       {107, 232, 520}, {143, 340, 700}, {202, 446, 860}, {9999, 566, 1160}};
        const auto& first = sys->players[0].stats;
        int lo = 566, hi = 1160;
        for (const auto& band : bands)
            if (first.level <= band[0]) { lo = band[1]; hi = band[2]; break; }
        const int amount = (lo + int(fight::random() % std::uint32_t(hi - lo))) * (first.bit_rate + 100) / 100;
        slot.bits = std::min(slot.bits + amount, 9999999);
        engine.log("player " + std::to_string(nearest) + " picked up " + std::to_string(amount) + " bits (" +
                   std::to_string(slot.bits) + ")");
        show_number(engine, 4, amount, Vec3f{m(3, 0), m(3, 1), m(3, 2)});
        return;
    }
    if (record <= 3) {
        slot.xdata += up;
    } else if (record <= 25) {
        // Record pairs map to UPCNT slots; the MMP ups land on slot 0 as well.
        static const int index[22] = {2, 2, 3, 3, 0, 0, 0, 0, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10};
        slot.upcnt[std::size_t(index[record - 4])] += up;
        sys->prepare_player(engine, nearest);
    } else {
        return;
    }
    engine.log("player " + std::to_string(nearest) + " picked up insItem " + std::to_string(record) + " (+" +
               std::to_string(up) + ")");
    // 803AA9A0: the matching panel (804EDAA0) over the player.
    static const char* panels[25][2] = {
        {"i_getitempnl02", "SQ_X5UP"},    {"i_getitempnl02", "SQ_X20UP"},    {"i_getitempnl02", "SQ_X100UP"},
        {"i_getitempnl01", "SQ_STRUP"},   {"i_getitempnl01", "SQ_STRUP2"},   {"i_getitempnl01", "SQ_DEFUP"},
        {"i_getitempnl01", "SQ_DEFUP2"},  {"i_getitempnl03", "SQ_MHPUP"},    {"i_getitempnl03", "SQ_MHPUP2"},
        {"i_getitempnl03", "SQ_MMPUP"},   {"i_getitempnl03", "SQ_MMPUP2"},   {"i_getitempnl01", "SQ_WISUP"},
        {"i_getitempnl01", "SQ_WISUP2"},  {"i_getitempnl01", "SQ_SPIUP"},    {"i_getitempnl01", "SQ_SPIUP2"},
        {"i_getitempnl01", "SQ_SPDUP"},   {"i_getitempnl01", "SQ_SPDUP2"},   {"i_getitempnl01", "SQ_FIREUP"},
        {"i_getitempnl01", "SQ_FIREUP2"}, {"i_getitempnl01", "SQ_ICEUP"},    {"i_getitempnl01", "SQ_ICEUP2"},
        {"i_getitempnl01", "SQ_THUNDUP"}, {"i_getitempnl01", "SQ_THUNDUP2"}, {"i_getitempnl01", "SQ_DARKUP"},
        {"i_getitempnl01", "SQ_DARKUP2"}};
    // Posted with 1 frame (0x803AAA6C).
    post_effect(engine, "PLAYER", std::nullopt, nearest, panels[record - 1][0], panels[record - 1][1]);
}

// 0x803AB838..0x803ABA44: the label goes on the near player's own
// ITNAMEFONT line (font00..03 by player, 804EDB90) when no other item holds
// that line: ALIGN CENTER, TEXTSET with the item's name, the blink phase and
// its "COLOR" timer cleared (0x803AB8F4..0x803AB904), SWITCH SET; the item
// becomes the line's holder (manager +0x30) and the label's owner (+FC).
void ModelCtrlManager::show_item_label(Engine& engine, ModelRam& ram, int player) {
    auto& messenger = engine.messenger();
    player = std::clamp(player, 0, 3);
    std::string name;
    if (auto* db = engine.get<DatabaseManager>())
        name = db->text(GameSystem::item_tables[std::clamp(ram.item_table, 0, 6)], ram.item_record, "NAME");
    Packet align = text_packet(player, "CENTER");
    messenger.send(engine, "TEXTCTL", std::nullopt, -1, "ALIGN", align);
    Packet text = text_packet(player, name);
    messenger.send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", text);
    ram.item_lit = false;
    ram.item_color_timer.reset();
    Packet on = text_packet(player, "SET");
    messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", on);
    item_labels_[std::size_t(player)] = true;
    ram.item_label = player;
    trace_label(engine, ram, player, "on");
}

// 0x803ABA54..0x803ABDDC, label state 1: the named RAM timer "COLOR"
// (8031E238, a missing one counts as expired) switches the line between the
// name colour (803B07E0) and a third of it, then restarts with half a second
// (8031DE44, 500000000 ns); so the first update after SWITCH SET sends the
// full colour. The label follows the item 10 units above its origin while
// its owner (+FC, PLAYER GETMATRIX; no answer counts as 100) is closer than
// 8 units; otherwise the class's erase hook drops it (vtable +B4 at
// 0x803ABDC0) and the state goes back to 0.
void ModelCtrlManager::update_item_label(Engine& engine, ModelRam& ram) {
    auto& messenger = engine.messenger();
    const auto now = engine.time();
    if (!ram.item_color_timer || timer_expired(*ram.item_color_timer, now)) {
        Vec3f color = name_color(ram.item_table, ram.item_device);
        if (ram.item_lit) {
            ram.item_lit = false;
            color = {color.x / 3.0f, color.y / 3.0f, color.z / 3.0f};
        } else {
            ram.item_lit = true;
        }
        Packet tint = text_packet(ram.item_label);
        tint.vectors.push_back(color);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "COLOR", tint);
        timer_start(ram.item_color_timer.emplace(), now, 500000000);
    }
    if (!ram.node) return;
    const auto& m = ram.node->matrix;
    float d2 = 100.0f;
    for (const auto& other : rams_) {
        if (!other->is_player || !other->generated || !other->node || other->index != ram.item_label) continue;
        const auto& o = other->node->matrix;
        const float dx = m(3, 0) - o(3, 0), dy = m(3, 1) - o(3, 1), dz = m(3, 2) - o(3, 2);
        d2 = dz * dz + (dx * dx + dy * dy);
        break;
    }
    if (d2 >= 64.0f) {
        item_erase_hook(engine, ram);
        return;
    }
    auto* cameras = engine.get<CameraManager>();
    if (!cameras) return;
    FrameCamera camera;
    cameras->frame_camera(engine, camera);
    const Mat4 vp = camera.view * camera.projection;
    const Vec3f p{m(3, 0), m(3, 1) + 10.0f, m(3, 2)};
    const float x = p.x * vp(0, 0) + p.y * vp(1, 0) + p.z * vp(2, 0) + vp(3, 0);
    const float y = p.x * vp(0, 1) + p.y * vp(1, 1) + p.z * vp(2, 1) + vp(3, 1);
    const float w = p.x * vp(0, 3) + p.y * vp(1, 3) + p.z * vp(2, 3) + vp(3, 3);
    if (w <= 1e-4f) return;
    // Pixel offset from the 640x480 screen centre, in 1/100 units.
    Packet position = text_packet(ram.item_label);
    position.vectors.push_back({x / w * 320.0f / 100.0f - 0.7f, y / w * 240.0f / 100.0f, -5.0f});
    messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SETPOSITION", position);
}

// The item class's erase hook (vtable +B4, 803ABE0C): with a label shown
// (+100) it clears that flag, frees the owner's line (manager +0x30), sets
// +FC = -1 and sends TEXTCTL SWITCH ITNAMEFONT <line> CLR. It runs when the
// owner walks away (0x803ABDC0), after a pickup (0x803AC408) and when SQ_ERAS
// starts on the generated item (command handler 0x8030E884, select_sequence),
// so a vanishing item's name goes with its SQ_ERAS, a frame before the
// removal.
void ModelCtrlManager::item_erase_hook(Engine& engine, ModelRam& ram) {
    if (ram.item_label < 0) return;
    const int line = std::clamp(ram.item_label, 0, 3);
    item_labels_[std::size_t(line)] = false;
    ram.item_label = -1;
    Packet off = text_packet(line, "CLR");
    engine.messenger().send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", off);
    trace_label(engine, ram, line, "off");
}

// 803AC0E8 (vtable +BC, DROPITEM ACTION1 <player>): only a non-instant item
// whose label is shown and owned by the asking player is picked up
// (0x803AC1D8..0x803AC208). Disks stack to nine per kind (DISKFOLDER),
// equipment fills up to 36 DEVICEFOLDER slots, key items set their common
// FLAGFOLDER entry; a full folder leaves the item lying. A pickup sends
// DROPITEM SQ_DDMG to the item and then runs its erase hook (0x803AC3F0,
// 0x803AC408).
bool ModelCtrlManager::pick_item(Engine& engine, ModelRam& ram, int player) {
    if (!ram.generated || ram.item_table < 0 || ram.item_table == 1 || player < 0 || ram.item_label < 0 || player != ram.item_label)
        return false;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return false;
    auto& slot = sys->players[std::size_t(std::clamp(player, 0, 3))];
    if (ram.item_table == 0) {
        auto& count = slot.disk_count(ram.item_record);
        if (count >= 9) return false;
        ++count;
    } else if (ram.item_table == 2) {
        sys->flag_folder[ram.item_record] = 1;
    } else {
        if (slot.devices.size() >= 36) return false;
        slot.devices.push_back(ram.item_device);
    }
    std::string name;
    if (auto* db = engine.get<DatabaseManager>()) name = db->text(GameSystem::item_tables[ram.item_table], ram.item_record, "NAME");
    engine.log("player " + std::to_string(player) + " picked up " + game_text_utf8(name));
    ram.item_table = -1;
    select_sequence(engine, ram, "SQ_DDMG");
    item_erase_hook(engine, ram);
    return true;
}

// CTRLITEM (GAMERAM 80372054). DEVICE: SET adds (weapon/armor1 record, value
// b, rank c; armor2 value b; abChip record) unless 36 are held, CLR drops the
// first unequipped match, IS / ISEQUIP / ISMAX query. DISK: SET / CLR add or
// remove c (0..9), IS / ISMAX query. FLAG: SET / CLR succeed on a change, IS
// reads. BIT: SET, GET, ADD (0..9999999). Answers 0 when nothing changes.
int GameSystem::control_item(std::string_view folder, std::string_view command, int player, std::string_view table,
                             int a, int b, int c) {
    auto& slot = players[std::size_t(std::clamp(player, 0, 3))];
    if (folder == "DEVICE") {
        auto& devices = slot.devices;
        if (command == "ISMAX") return devices.size() >= 36 ? 1 : 0;
        const int type = table == "weapon" ? 0 : table == "armor1" ? 1 : table == "armor2" ? 2 : table == "abChip" ? 3 : -1;
        if (type < 0) return 0;
        auto match = [&](const ItemDevice& d) { return d.type == type && d.record == a; };
        if (command == "SET") {
            if (devices.size() >= 36) return 0;
            ItemDevice device;
            device.type = type;
            device.record = a;
            if (type <= 2) device.value = b;
            if (type <= 1) device.rank = c;
            devices.push_back(device);
            return 1;
        }
        if (command == "CLR") {
            const auto found = std::find_if(devices.begin(), devices.end(), [&](const ItemDevice& d) { return match(d) && !d.flag; });
            if (found == devices.end()) return 0;
            slot.remove_device(int(found - devices.begin()));
            return 1;
        }
        if (command == "IS") return std::any_of(devices.begin(), devices.end(), match) ? 1 : 0;
        if (command == "ISEQUIP")
            return std::any_of(devices.begin(), devices.end(), [&](const ItemDevice& d) { return match(d) && d.flag; }) ? 1 : 0;
        return 0;
    }
    if (folder == "DISK") {
        auto& count = slot.disk_count(a);
        if (command == "SET") {
            if (count == 9) return 0;
            count = std::min(count + c, 9);
            return 1;
        }
        if (command == "CLR") {
            if (count == 0) return 0;
            count = std::max(count - c, 0);
            return 1;
        }
        if (command == "IS") return count != 0 ? 1 : 0;
        if (command == "ISMAX") return count == 9 ? 1 : 0;
        return 0;
    }
    if (folder == "FLAG") {
        auto& flag = flag_folder[a];
        if (command == "SET") {
            if (flag) return 0;
            flag = 1;
            return 1;
        }
        if (command == "CLR") {
            if (!flag) return 0;
            flag = 0;
            return 1;
        }
        if (command == "IS") return flag;
        return 0;
    }
    if (folder == "BIT") {
        if (command == "SET") slot.bits = a;
        else if (command == "GET") return slot.bits;
        else if (command == "ADD") slot.bits = int(std::clamp<long long>(static_cast<long long>(slot.bits) + a, 0, 9999999));
        return 0;
    }
    return 0;
}

} // namespace dw4::engine
