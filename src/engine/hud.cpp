#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>

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
            ram.shown = std::clamp(ram.shown + (difference > 0 ? move : -move), 0.0f, ram.maximum);
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

} // namespace dw4::engine
