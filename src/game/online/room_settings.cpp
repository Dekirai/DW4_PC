#include "room_settings.hpp"

#include "json.hpp"
#include "dw4/engine/managers.hpp"
#include "wire.hpp"

#include <algorithm>
#include <span>

namespace dw4::game::online {

std::string settings_json(const RoomSettings& s) {
    std::string out = "{\"difficulty\":" + std::to_string(s.difficulty) + ",\"cameraMode\":" + std::to_string(s.camera_mode) +
                      ",\"lightMode\":" + std::to_string(s.light_mode) + ",\"fastLoading\":" + (s.fast_loading ? "true" : "false");
    if (s.randomizer())
        out += ",\"randomizer\":{\"enabled\":true,\"seed\":" + std::to_string(s.rand_seed) + ",\"enemies\":" + std::to_string(s.rand_enemies) +
               ",\"zones\":" + std::to_string(s.rand_zones) + ",\"drops\":" + (s.rand_drops ? "true" : "false") + "}";
    return out + "}";
}

std::optional<RoomSettings> settings_from_json(const json::Value& v) {
    RoomSettings s;
    if (v.is_null()) return s;
    if (v.kind != json::Value::Kind::object) return std::nullopt;
    auto small = [&](std::string_view key, int& into, int max) {
        const auto& f = v[key];
        if (f.is_null()) return true;
        const auto n = f.i64();
        if (!n || *n < 0 || *n > max) return false;
        into = int(*n);
        return true;
    };
    if (!small("difficulty", s.difficulty, 2) || !small("cameraMode", s.camera_mode, 1) || !small("lightMode", s.light_mode, 1)) return std::nullopt;
    if (const auto fast = v["fastLoading"].flag()) s.fast_loading = *fast;
    const auto& r = v["randomizer"];
    if (r.kind == json::Value::Kind::object && r["enabled"].flag().value_or(false)) {
        const auto seed = r["seed"].u64();
        if (!seed || *seed > 0xFFFFFFFFull) return std::nullopt;
        s.rand_seed = std::uint32_t(*seed);
        if (const auto e = r["enemies"].i64()) {
            if (*e < 0 || *e > 2) return std::nullopt;
            s.rand_enemies = int(*e);
        }
        if (const auto z = r["zones"].i64()) {
            if (*z < 0 || *z > 2) return std::nullopt;
            s.rand_zones = int(*z);
        }
        s.rand_drops = r["drops"].flag().value_or(false);
    }
    return s;
}

std::string settings_hash_text(const RoomSettings& s, std::uint32_t protocol, std::uint64_t data_hash) {
    // The relay's canonical text (RoomSettings.Canonical in DW4Online.Domain).
    const bool on = s.randomizer();
    return "DW4R2-settings/1;protocol=" + std::to_string(protocol) + ";data=" + std::to_string(data_hash) + ";difficulty=" +
           std::to_string(s.difficulty) + ";camera=" + std::to_string(s.camera_mode) + ";light=" + std::to_string(s.light_mode) + ";rand=" +
           std::to_string(on ? s.rand_seed : 0) + "," + (on && s.rand_drops ? "1" : "0") + "," + std::to_string(on ? s.rand_enemies : 0) + "," +
           std::to_string(on ? s.rand_zones : 0) + ";fast=" + (s.fast_loading ? "1" : "0");
}

std::uint64_t settings_hash(const RoomSettings& s, std::uint32_t protocol, std::uint64_t data_hash) {
    const auto text = settings_hash_text(s, protocol, data_hash);
    return wire::fnv1a64(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

wire::Settings to_wire(const RoomSettings& s) {
    wire::Settings w;
    w.difficulty = std::uint8_t(std::clamp(s.difficulty, 0, 2));
    w.camera = std::uint8_t(std::clamp(s.camera_mode, 0, 1));
    w.light = std::uint8_t(std::clamp(s.light_mode, 0, 1));
    w.fast = s.fast_loading ? 1 : 0;
    w.seed = s.rand_seed;
    w.drops = s.rand_drops ? 1 : 0;
    w.enemies = std::uint8_t(std::clamp(s.rand_enemies, 0, 2));
    w.zones = std::uint8_t(std::clamp(s.rand_zones, 0, 2));
    return w;
}

RoomSettings from_wire(const wire::Settings& w) {
    RoomSettings s;
    s.difficulty = std::clamp(int(w.difficulty), 0, 2);
    s.camera_mode = std::clamp(int(w.camera), 0, 1);
    s.light_mode = std::clamp(int(w.light), 0, 1);
    s.fast_loading = w.fast != 0;
    s.rand_seed = w.seed;
    s.rand_drops = w.drops != 0;
    s.rand_enemies = std::clamp(int(w.enemies), 0, 2);
    s.rand_zones = std::clamp(int(w.zones), 0, 2);
    return s;
}

void apply_room_modes(const RoomSettings& s, engine::Engine& engine) {
    if (auto* sys = engine.get<engine::GameSystem>()) {
        sys->difficulty = std::clamp(s.difficulty, 0, 2);
        sys->camera_mode = s.camera_mode;
        sys->light_mode = s.light_mode;
    }
}

void apply_room_cheats(const RoomSettings& s, engine::Cheats& cheats) {
    cheats.god = cheats.infinite_mp = cheats.one_hit = false;
    cheats.exp_rate = cheats.damage_dealt = cheats.damage_taken = 100;
    const bool on = s.randomizer();
    cheats.rand_seed = on ? s.rand_seed : 0;
    cheats.rand_enemies = std::uint8_t(on ? s.rand_enemies : 0);
    cheats.rand_zones = std::uint8_t(on ? s.rand_zones : 0);
    cheats.drop_seed = on && s.rand_drops ? s.rand_seed : 0;
    cheats.fast_loading = s.fast_loading;
}

} // namespace dw4::game::online
