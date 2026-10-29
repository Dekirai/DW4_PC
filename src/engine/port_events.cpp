// Observe-only port events (port_events.hpp): the hooks' shared helpers.
// Nothing here writes engine state; every function returns at once while
// the host collects no events.
#include "dw4/engine/port_events.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cctype>

namespace dw4::engine {
namespace {

int difficulty(const Engine& engine) {
    const auto* sys = engine.get<GameSystem>();
    return sys ? sys->difficulty : 0;
}

bool starts_with_folded(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
    return true;
}

} // namespace

const char* port_event_name(PortEvent::Kind kind) {
    switch (kind) {
    case PortEvent::Kind::boss_defeated: return "boss_defeated";
    case PortEvent::Kind::enemy_defeated: return "enemy_defeated";
    case PortEvent::Kind::level_up: return "level_up";
    case PortEvent::Kind::digivolution: return "digivolution";
    case PortEvent::Kind::form_reached: return "form_reached";
    case PortEvent::Kind::technique: return "technique";
    case PortEvent::Kind::card: return "card";
    case PortEvent::Kind::quest_done: return "quest_done";
    case PortEvent::Kind::player_defeated: return "player_defeated";
    case PortEvent::Kind::critical_hit: return "critical_hit";
    case PortEvent::Kind::story_flag: return "story_flag";
    }
    return "?";
}

void port_flag_changed(Engine& engine, std::int32_t flag, int before, int after) {
    if (!engine.port_events_enabled()) return;
    // A dungeon boss's death script sets its flags only on the run that ends
    // the fight: not while the group of a multi-part boss still stands
    // (dng308b3 ded_e_mecha4.lua counts g_bossRest down first) and not when
    // every joined player is down (the HP sum test at its start returns).
    if (auto& scripts = engine.port_boss_scripts(); after != 0 && !scripts.empty() && !scripts.back().empty()) {
        engine.port_event({PortEvent::Kind::boss_defeated, -1, difficulty(engine), 0, scripts.back()});
        scripts.back().clear();
    }
    if (before == after) return;
    engine.port_event({PortEvent::Kind::story_flag, -1, flag, after, {}});
    if (before != 0 || after == 0) return;
    // The boss stages' defeat flags, set by the boss's hit script when its
    // HP reaches 0 (stagBS01 hit_f_apocaly.lua 67, stagBS02 hit_f_belvam.lua
    // 68, stagBS03 hit_f_luce.lua 69, stagBS06 hit_f_devl3.lua 70; the story
    // flags of a difficulty run, so they rise once per run).
    static constexpr std::pair<std::int32_t, const char*> kBossFlags[] = {
        {67, "apocaly"}, {68, "belvam"}, {69, "luce"}, {70, "devl3"}};
    for (const auto& [boss_flag, key] : kBossFlags)
        if (flag == boss_flag) engine.port_event({PortEvent::Kind::boss_defeated, -1, difficulty(engine), 0, key});
    // Quest goals: the lobby's questNN.lua clears 384..389 when quest 1..6
    // is taken (and sets 376..381 for it); the quest dungeon sets its own
    // flag when the goal is reached (dngq1b2e ded_e_mammos 384, dngq2b1e
    // first.lua 385, dngq3b1e matz01 386, dngq4b2e/h/v ded_e_anomalo 387,
    // dngq5b1* itemchange*.lua 388, dngq6b01 matz15/16 389). The lobby's
    // treasureNN.lua sets them too, so only the dungeons count. Quests 7
    // and 8 have one flag per difficulty (dngq7b2e ded_e_tonogeko 447 / 449
    // / 451, dngq8b3e/h/v ded_e_ldknight 448 / 450 / 452).
    if (flag >= 384 && flag <= 389 && starts_with_folded(engine.stage_name(), "dngq"))
        engine.port_event({PortEvent::Kind::quest_done, -1, flag - 383, difficulty(engine), {}});
    if (flag >= 447 && flag <= 452) {
        const int quest = (flag - 447) % 2 == 0 ? 7 : 8;
        engine.port_event({PortEvent::Kind::quest_done, -1, quest, (flag - 447) / 2, {}});
    }
}

bool port_script_started(Engine& engine, std::string_view name) {
    if (!engine.port_events_enabled() || !starts_with_folded(name, "ded_e_")) return false;
    // The dungeon bosses' death scripts (ded_e_<key>.lua): the boss counts
    // once the run sets a flag (port_flag_changed).
    std::string key(name.substr(6));
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".lua") == 0) key.resize(key.size() - 4);
    engine.port_boss_scripts().push_back(std::move(key));
    return true;
}

void port_script_finished(Engine& engine) {
    if (auto& scripts = engine.port_boss_scripts(); !scripts.empty()) scripts.pop_back();
}

} // namespace dw4::engine
