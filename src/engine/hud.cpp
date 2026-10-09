#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dw4::engine {

// ===========================================================================
// NUMBERCTRL (beNumberCtrl, commands 8031B314, draw 8031AF04, update 8031AD7C)
// ===========================================================================
// RAM slots: [11] the digit geometry, [12] texture step per digit, [13] the
// maximum value, [14] blank leading digits, [15] one beNumVerData per digit
// place (units first) listing (vertex, corner) pairs.
void NumberCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<NumberRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        if (record) {
            ram->geometry = engine.scenes().node(record.ref(11));
            ram->step = record.f32(12);
            ram->maximum = record.i32(13);
            ram->blank_leading = record.i32(14) != 0;
            for (const auto& place : record.list(15)) {
                std::vector<std::pair<int, int>> vertices;
                if (place)
                    for (const auto& v : place.list(3))
                        if (v) vertices.emplace_back(v.i32(2), v.i32(3));
                ram->places.push_back(std::move(vertices));
            }
        }
        engine.messenger().add_row("NUMBERCTRL", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
    }
}

// 8031AF04: each digit place shows value % 10 as a texture column; digits above
// the value are blank (texcoord 0,0) or zero depending on [14].
void NumberCtrlManager::draw(Engine& engine, NumberRam& ram, int value) {
    if (!ram.geometry || ram.geometry->geometries.empty()) return;
    const auto* source = engine.scenes().geometry(ram.geometry->geometries.front());
    if (!source || !source->valid) return;
    auto geometry = std::make_shared<GeometryData>(*source);
    int rest = value;
    for (std::size_t place = 0; place < ram.places.size(); ++place) {
        int digit;
        if (value < 0) digit = ram.blank_leading ? -1 : 0;
        else if (place == 0 || rest != 0) digit = rest % 10;
        else digit = ram.blank_leading ? -1 : 0;
        rest /= 10;
        for (const auto& [vertex, corner] : ram.places[place]) {
            const auto at = std::size_t(vertex) * 2;
            if (at + 1 >= geometry->texcoords.size()) continue;
            float u = 0, v = 0;
            if (digit >= 0) {
                u = float(digit) * ram.step + ((corner == 1 || corner == 3) ? ram.step : 0.0f);
                v = corner >= 2 ? 1.0f : 0.0f;
            }
            geometry->texcoords[at] = u;
            geometry->texcoords[at + 1] = v;
        }
    }
    ram.geometry->runtime_geometry = std::move(geometry);
}

bool NumberCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<NumberRam&>(base);
    if (command == "GETVALUE") {
        packet.ints.push_back(ram.target);
        return true;
    }
    if (command == "SETVALUE") {
        if (packet.ints.empty()) return true;
        const int value = std::clamp(packet.ints[0], 0, ram.maximum);
        draw(engine, ram, value);
        ram.shown = ram.target = value;
        ram.rate = 0;
        return true;
    }
    if (command == "ADDVALUE") {
        if (packet.ints.empty()) return true;
        ram.target = std::clamp(ram.target + packet.ints[0], 0, ram.maximum);
        // 8031B234: count by (difference / 128) + 1 per frame, never a multiple of ten.
        const int difference = ram.target - ram.shown;
        int rate = 0;
        if (difference > 0) {
            rate = std::min(difference / 128 + 1, 99);
            if (rate % 10 == 0) ++rate;
        } else if (difference < 0) {
            rate = std::max(difference / 128 - 1, -99);
            if (rate % 10 == 0) --rate;
        }
        ram.rate = rate;
        return true;
    }
    return false;
}

void NumberCtrlManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.drawn) {
            ram.drawn = true;
            draw(engine, ram, ram.shown);
        }
        if (ram.shown == ram.target || ram.rate == 0) continue;
        int next = ram.shown + ram.rate;
        if ((ram.rate > 0 && next > ram.target) || (ram.rate < 0 && next < ram.target)) next = ram.target;
        ram.shown = next;
        draw(engine, ram, next);
    }
}

// ===========================================================================
// METERCTRL (beMeterCtrl, commands 803076A0, update 803071DC)
// ===========================================================================
// Each RAM owns a beMeterCtrlOneData ([3] node, [4] maximum, [9]/[10] time
// bounds). The node's children are animated clips whose 1 s .. 3 s range maps
// the displayed value from empty to full.
void MeterCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    const auto data = info.list(7);
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<MeterRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index;
        ram->active = record ? record.boolean(5) : false;
        if (std::size_t(index) < data.size() && data[std::size_t(index)]) {
            const auto& one = data[std::size_t(index)];
            ram->node = engine.scenes().node(one.ref(3));
            ram->maximum = std::max(1.0f, one.f32(4));
            ram->low_time = one.i64(9);
            ram->high_time = one.i64(10);
            ram->continuous = one.ref(3) ? one.ref(3).boolean(8) : true;
            ram->integer = one.boolean(14);
        }
        engine.messenger().add_row("METERCTRL", ram->info_name, ram->index, ram.get());
        rams_.push_back(std::move(ram));
        ++index;
    }
}

bool MeterCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<MeterRam&>(base);
    auto value_argument = [&]() {
        if (!packet.floats.empty()) return packet.floats[0];
        if (!packet.ints.empty()) return float(packet.ints[0]);
        return 0.0f;
    };
    if (command == "GETVALUE") {
        packet.floats.push_back(ram.target);
        return true;
    }
    if (command == "SETVALUE") {
        ram.target = ram.shown = value_argument();
        ram.dirty = true;
        return true;
    }
    if (command == "ADDVALUE") {
        ram.target = std::clamp(ram.target + value_argument(), 0.0f, ram.maximum);
        return true;
    }
    if (command == "GETPERCENT") {
        packet.floats.push_back(ram.target / ram.maximum);
        return true;
    }
    if (command == "SETPERCENT" || command == "ADDPERCENT") {
        // A nonzero percentage never shows as an empty meter.
        const float percent = value_argument();
        float value = percent * ram.maximum / 100.0f;
        if (command == "ADDPERCENT") value = std::clamp(value + ram.target, 0.0f, ram.maximum);
        else if (value < 1.0f && percent != 0.0f) value = 1.0f;
        ram.target = value;
        return true;
    }
    if (command == "SETMAXVALUE") {
        ram.maximum = std::max(1.0f, value_argument());
        ram.dirty = true;
        return true;
    }
    if (command == "SETMODELHP") {
        // Follow a model's HP: (name, RAM number).
        ram.model_name = std::string(packet.string(0));
        ram.model_index = packet.integer(0, 0);
        return true;
    }
    (void)engine;
    return false;
}

void MeterCtrlManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.model_name.empty()) {
            if (auto* models = engine.get<ModelCtrlManager>())
                if (const auto* model = models->find(ram.model_name, ram.model_index); model && model->max_hp > 0) {
                    const float percent = float(model->hp * 100 / model->max_hp);
                    float value = percent * ram.maximum / 100.0f;
                    if (value < 1.0f && percent != 0.0f) value = 1.0f;
                    ram.target = value;
                }
        }
        // 80307150: the displayed value moves by maximum / 60 per frame (at least 1).
        const float step = std::max(1.0f, ram.maximum / 60.0f);
        if (ram.target != ram.shown) {
            ram.dirty = true;
            const float difference = ram.target - ram.shown;
            const float move = std::min(std::abs(difference), step);
            float next = std::clamp(ram.shown + (difference > 0 ? move : -move), 0.0f, ram.maximum);
            // 80307358-803073BC: integer meters (OneData +0x3E, the HP meters) truncate the
            // shown value (fctiwz); a nonzero value that truncates to 0 shows as 1. With
            // maximum 100 the meter falls by 2 and rises by 1 per step and rests at
            // trunc(target), staying dirty while target != shown.
            if (ram.integer) {
                const bool nonzero = next != 0.0f;
                next = std::trunc(next);
                if (next == 0.0f && nonzero) next = 1.0f;
            }
            ram.shown = next;
        }
        if (!ram.dirty || !ram.node) continue;
        ram.dirty = false;
        std::int64_t time;
        if (ram.shown <= 0.0f) time = 1'000'000'000;
        else if (ram.shown >= ram.maximum) time = 3'000'000'000;
        else {
            auto offset = static_cast<std::int64_t>(ram.shown * (2.0e9f / ram.maximum));
            if (offset < ram.low_time) offset = ram.low_time;
            if (ram.high_time > 0 && offset > ram.high_time) offset = ram.high_time;
            time = offset + 1'000'000'000;
        }
        ram.node->fixed_time = time;
    }
}

// 802EDC54 / table 804DD2C4: FIND_SPACE the kind's number model, GENERATE it
// at the position with the sequence for the digit count (values capped at
// 99999) and set its NUMBERCTRL of the same RAM.
void show_number(Engine& engine, int kind, int value, const Vec3f& position) {
    struct Popup { const char* model; const char* number; const char* sequences[5]; };
    static const Popup popups[5] = {
        {"i_dmgnum", "dmgnm0", {"SQ_DMGNM01", "SQ_DMGNM02", "SQ_DMGNM03", "SQ_DMGNM04", "SQ_DMGNM05"}},
        {"i_expnum", "expnm0", {"SQ_EXPNM01", "SQ_EXPNM02", "SQ_EXPNM03", "SQ_EXPNM04", "SQ_EXPNM04"}},
        {"i_healnum", "healnm0", {"SQ_HEALNM01", "SQ_HEALNM02", "SQ_HEALNM03", "SQ_HEALNM04", "SQ_HEALNM05"}},
        {"i_pcdmgnum", "dmgnm1", {"SQ_DMGNM01", "SQ_DMGNM02", "SQ_DMGNM03", "SQ_DMGNM04", "SQ_DMGNM05"}},
        {"i_bitnum", "bitnum0", {"SQ_BITNM01", "SQ_BITNM02", "SQ_BITNM03", "SQ_BITNM04", "SQ_BITNM04"}}};
    if (kind < 0 || kind > 4) return;
    // DW4_TRACE_HIT also logs the numbers shown.
    if (static const bool trace = std::getenv("DW4_TRACE_HIT") != nullptr; trace)
        engine.log("number " + std::string(popups[kind].model) + " " + std::to_string(value));
    const auto& popup = popups[kind];
    auto& messenger = engine.messenger();
    Packet find;
    messenger.send(engine, "MODELCTRL", popup.model, -1, "FIND_SPACE", find);
    if (find.ints.empty()) return;
    const int index = find.ints[0];
    const int digits = value < 10 ? 0 : value < 100 ? 1 : value < 1000 ? 2 : value < 10000 ? 3 : 4;
    value = std::min(value, 99999);
    Packet generate;
    generate.strings.emplace_back(popup.sequences[digits]);
    generate.vectors.push_back(position);
    messenger.send(engine, "MODELCTRL", popup.model, index, "GENERATE", generate);
    Packet number;
    number.ints.push_back(value);
    messenger.send(engine, "NUMBERCTRL", popup.number, index, "SETVALUE", number);
}

} // namespace dw4::engine
