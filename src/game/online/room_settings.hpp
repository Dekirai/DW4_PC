#pragma once

// A room's simulation-relevant settings (docs/SEAMLESS.md 3.1, 6.2, 10):
// difficulty, camera mode and light mode (the title options; online they come
// from the room, so every instance uses the same camera data), the randomizer
// seed and modes (the host's for everyone) and fast loading. Cheats and
// rebalancing are always off online.
//
// JSON in the room's settings object (POST /rooms "settings"):
//   {"difficulty":0,"cameraMode":0,"lightMode":0,"fastLoading":false,
//    "randomizer":{"enabled":true,"seed":4242,"enemies":1,"zones":2,"drops":true}}
//
// The settings hash of the DW4R2 hello line (16 hex digits) is FNV-1a 64
// (offset basis 0xcbf29ce484222325, prime 0x100000001b3) over the relay's
// canonical ASCII text (DW4Online.Domain RoomSettings.Canonical)
//   DW4R2-settings/1;protocol=P;data=D;difficulty=N;camera=N;light=N;rand=SEED,DROPS,ENEMIES,ZONES;fast=N
// with decimal numbers, DROPS and fast 0 or 1, and rand=0,0,0,0 while the
// randomizer is not active.
#include "dw4/engine/engine.hpp"
#include "wire.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dw4::game::online {

namespace json {
struct Value;
}

struct RoomSettings {
    int difficulty = 0;   // 0 Normal, 1 Hard, 2 Very Hard
    int camera_mode = 0;  // title option camera_mode (aaaCamera / aaaCameraL)
    int light_mode = 1;   // title option light_mode (1: the original's new game default, as the server's)
    bool fast_loading = false;
    std::uint32_t rand_seed = 0;
    int rand_enemies = 0, rand_zones = 0;
    bool rand_drops = false;
    bool randomizer() const { return rand_seed != 0 && (rand_drops || rand_enemies != 0 || rand_zones != 0); }
    bool operator==(const RoomSettings&) const = default;
};

std::string settings_json(const RoomSettings& settings);
// Missing fields keep their defaults; nothing when a field is out of range.
std::optional<RoomSettings> settings_from_json(const json::Value& value);
std::string settings_hash_text(const RoomSettings& settings, std::uint32_t protocol, std::uint64_t data_hash);
std::uint64_t settings_hash(const RoomSettings& settings, std::uint32_t protocol, std::uint64_t data_hash);
// The engine's cheats for the room: no cheats, no rebalancing, the room's
// randomizer and fast loading (set after every online engine construction
// and restore).
void apply_room_cheats(const RoomSettings& settings, engine::Cheats& cheats);
// Round I1a (docs/SEAMLESS.md 19.8.7): the settings block of protocol 49 and
// back (out-of-range values clamped), and the modes (difficulty, camera,
// light) into the engine's GameSystem.
wire::Settings to_wire(const RoomSettings& settings);
RoomSettings from_wire(const wire::Settings& settings);
void apply_room_modes(const RoomSettings& settings, engine::Engine& engine);

} // namespace dw4::game::online
