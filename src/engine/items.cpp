#include "dw4/engine/managers.hpp"

#include <algorithm>

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
    const Vec3f grey{0.9f, 0.9f, 0.9f};
    if (table < 3 || device.type >= 3) return grey;
    if ((device.type == 0 && device.record == 0x114) || (device.type == 1 && device.record == 0x1F) ||
        (device.type == 2 && device.record == 0x2C))
        return grey;
    if (device.value >= 1000) return {1.0f, 0.0f, 0.9f};
    if (device.value >= 501) return {1.0f, 0.6f, 0.1f};
    if (device.value >= 101) return {0.9f, 0.8f, 0.1f};
    if (device.value >= 31) return {0.4f, 1.0f, 0.6f};
    if (device.value >= 1) return {0.1f, 0.9f, 1.0f};
    return grey;
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

// KIND_ITEM (803AB5AC): the nearest player within 8 units may pick the item
// up; instant items (insItem: bits, X data) are taken on contact. Other items
// show their name over the model while that player stays close.
void ModelCtrlManager::update_item(Engine& engine, ModelRam& ram) {
    if (!ram.node || ram.seq_name == "SQ_DDMG") return;
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
    ram.item_player = nearest;
    if (ram.item_table != 1) {
        item_label(engine, ram, nearest >= 0);
        return;
    }
    if (nearest < 0) return;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    if (ram.item_record == 0) {
        // 803B1408: bits by player 1's level band.
        static const int bands[][3] = {{18, 10, 55}, {38, 55, 100}, {59, 87, 195}, {93, 162, 350},
                                       {107, 232, 520}, {143, 340, 700}, {202, 446, 860}, {9999, 566, 1160}};
        const int level = sys->players[0].stats.level;
        int lo = 566, hi = 1160;
        for (const auto& band : bands)
            if (level <= band[0]) { lo = band[1]; hi = band[2]; break; }
        const int amount = lo + int(fight::random() % std::uint32_t(hi - lo));
        sys->bits += amount;
        engine.log("picked up " + std::to_string(amount) + " bits (" + std::to_string(sys->bits) + ")");
        Packet value;
        value.ints.push_back(sys->bits);
        if (engine.messenger().count("NUMBERCTRL", "bitnum0", -1)) engine.messenger().send(engine, "NUMBERCTRL", "bitnum0", -1, "SETVALUE", value);
    } else {
        engine.note("instant item " + std::to_string(ram.item_record));
    }
    ram.item_table = -1;
    select_sequence(engine, ram, "SQ_DDMG");
}

// 803AB864..803ABDB0: one of four ITNAMEFONT lines (font00..03) carries the
// name, blinking between its colour and a third of it every half second, and
// follows the item 10 units above its origin in screen space.
void ModelCtrlManager::item_label(Engine& engine, ModelRam& ram, bool show) {
    auto& messenger = engine.messenger();
    if (!show) {
        if (ram.item_label < 0) return;
        Packet off = text_packet(ram.item_label, "CLR");
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", off);
        item_labels_[std::size_t(ram.item_label)] = false;
        ram.item_label = -1;
        return;
    }
    const Vec3f color = name_color(ram.item_table, ram.item_device);
    if (ram.item_label < 0) {
        const auto free = std::find(item_labels_.begin(), item_labels_.end(), false);
        if (free == item_labels_.end()) return;
        ram.item_label = int(free - item_labels_.begin());
        *free = true;
        std::string name;
        if (auto* db = engine.get<DatabaseManager>())
            name = db->text(GameSystem::item_tables[std::clamp(ram.item_table, 0, 6)], ram.item_record, "NAME");
        Packet align = text_packet(ram.item_label, "CENTER");
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "ALIGN", align);
        Packet text = text_packet(ram.item_label, name);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "TEXTSET", text);
        Packet tint = text_packet(ram.item_label);
        tint.vectors.push_back(color);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "COLOR", tint);
        Packet on = text_packet(ram.item_label, "SET");
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "SWITCH", on);
        ram.item_dim = false;
        ram.item_blink = engine.time() + 500'000'000;
    } else if (engine.time() >= ram.item_blink) {
        ram.item_dim = !ram.item_dim;
        ram.item_blink = engine.time() + 500'000'000;
        Packet tint = text_packet(ram.item_label);
        tint.vectors.push_back(ram.item_dim ? Vec3f{color.x / 3.0f, color.y / 3.0f, color.z / 3.0f} : color);
        messenger.send(engine, "TEXTCTL", std::nullopt, -1, "COLOR", tint);
    }
    auto* cameras = engine.get<CameraManager>();
    if (!cameras || !ram.node) return;
    FrameCamera camera;
    cameras->frame_camera(engine, camera);
    const Mat4 vp = camera.view * camera.projection;
    const auto& m = ram.node->matrix;
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

// 803AC0E8: the player next to the item picks it up. Disks stack to nine per
// kind (DISKFOLDER), equipment fills up to 36 DEVICEFOLDER slots, key items set
// their common FLAGFOLDER entry; a full folder leaves the item lying.
bool ModelCtrlManager::pick_item(Engine& engine, ModelRam& ram, int player) {
    if (!ram.generated || ram.item_table < 0 || ram.item_table == 1 || player < 0 || player != ram.item_player)
        return false;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return false;
    auto& slot = sys->players[std::size_t(std::clamp(player, 0, 3))];
    if (ram.item_table == 0) {
        auto& count = slot.disks[ram.item_record];
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
    item_label(engine, ram, false);
    ram.item_table = -1;
    select_sequence(engine, ram, "SQ_DDMG");
    return true;
}

} // namespace dw4::engine
