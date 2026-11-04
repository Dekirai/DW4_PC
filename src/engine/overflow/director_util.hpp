#pragma once

// Data Overflow: small helpers of the director's sources (director.cpp,
// spawner.cpp, abilities.cpp). Every message goes out in a
// Messenger::PortOnly scope (SURVIVAL.md 13.5): the original's message
// objects never see the director.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace dw4::engine::overflow::detail {

inline Vec3f ram_position(const ModelCtrlManager::ModelRam& ram) {
    return ram.node ? Vec3f{ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)} : Vec3f{};
}

inline float flat_distance(const Vec3f& a, const Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }

inline std::size_t port_send(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index,
                             std::string_view command, Packet& packet) {
    const Messenger::PortOnly scope(engine.messenger());
    return engine.messenger().send(engine, category, name, index, command, packet);
}

inline void port_send(Engine& engine, std::string_view category, std::optional<std::string_view> name, int index, std::string_view command) {
    Packet packet;
    port_send(engine, category, name, index, command, packet);
}

inline void play_sound(Engine& engine, std::string_view sound) {
    Packet packet;
    packet.strings.emplace_back(std::string(sound));
    port_send(engine, "SOUND", std::nullopt, -1, "PLAY", packet);
}

// A living enemy of the run: generated, not erased or dying, HP left.
inline bool living_enemy(const ModelCtrlManager::ModelRam& ram) {
    return ram.generated && ram.category == "ENEMY" && ram.hp > 0 && ram.node && ram.seq_name != "SQ_ERAS" &&
           ram.seq_name != "SQ_DDMG" && ram.seq_name != "SQ_DEAD" && ram.info_name.starts_with("e_");
}

// The game's item and technique names without their glyph prefix (∈, ∋, ...)
// as UTF-8 ("Battle Hawk α").
inline std::string plain_name(std::string_view game_text) {
    std::string text = game_text_utf8(game_text);
    // The item glyphs of the font are mathematical symbols (U+2200..U+22FF:
    // E2 88 xx / E2 8B xx) in front of the name.
    while (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xE2 &&
           (static_cast<unsigned char>(text[1]) == 0x88 || static_cast<unsigned char>(text[1]) == 0x8A || static_cast<unsigned char>(text[1]) == 0x8B))
        text.erase(0, 3);
    return text;
}

} // namespace dw4::engine::overflow::detail
