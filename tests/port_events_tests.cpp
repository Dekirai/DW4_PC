// The engine's observe-only port events (engine/port_events.hpp): nothing is
// collected while the host asks for none, the hooks report boss defeats,
// quests, cards and story flags, and collecting changes neither the
// fingerprint nor any engine state the simulation reads.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/lua4.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::engine;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void off_by_default() {
    Engine engine{dw4::MountedResources{}};
    check(!engine.port_events_enabled(), "a new engine collects nothing");
    engine.port_event({PortEvent::Kind::level_up, 0, 5, 0, {}});
    port_flag_changed(engine, 67, 0, 1);
    check(!port_script_started(engine, "ded_e_blosso.lua"), "no boss script watched while off");
    check(engine.take_port_events().empty() && engine.port_boss_scripts().empty(), "no events while off");
}

void flags_and_scripts() {
    Engine engine{dw4::MountedResources{}};
    engine.set_port_events(true);
    port_flag_changed(engine, 67, 0, 1);
    auto events = engine.take_port_events();
    check(events.size() == 2, "a story flag and a boss");
    check(events[0].kind == PortEvent::Kind::story_flag && events[0].value == 67 && events[0].extra == 1, "the flag event");
    check(events[1].kind == PortEvent::Kind::boss_defeated && events[1].text == "apocaly", "flag 67 is Apocalymon");
    port_flag_changed(engine, 67, 1, 1);
    check(engine.take_port_events().empty(), "no change, no event");
    port_flag_changed(engine, 70, 1, 0);
    events = engine.take_port_events();
    check(events.size() == 1 && events[0].kind == PortEvent::Kind::story_flag && events[0].extra == 0, "a falling edge is only a flag");
    // The lobby's treasure scripts set the quest goals too: only quest dungeons count.
    port_flag_changed(engine, 384, 0, 1);
    events = engine.take_port_events();
    check(events.size() == 1 && events[0].kind == PortEvent::Kind::story_flag, "384 outside a quest dungeon");
    port_flag_changed(engine, 447, 0, 1);
    events = engine.take_port_events();
    check(events.size() == 2 && events[1].kind == PortEvent::Kind::quest_done && events[1].value == 7 && events[1].extra == 0,
          "447: quest 7 on Normal");
    port_flag_changed(engine, 452, 0, 1);
    events = engine.take_port_events();
    check(events.size() == 2 && events[1].value == 8 && events[1].extra == 2, "452: quest 8 on Very Hard");
    // Dungeon boss death scripts: the boss falls when the run sets a flag
    // (ded_e_blosso.lua: FLGset(119, 1) after the victory demo).
    check(port_script_started(engine, "ded_e_blosso.lua"), "ded_e_blosso is watched");
    check(engine.take_port_events().empty(), "nothing at the start");
    port_flag_changed(engine, 119, 0, 1);
    port_flag_changed(engine, 508, 1, 0);
    port_script_finished(engine);
    events = engine.take_port_events();
    check(events.size() == 3 && events[0].kind == PortEvent::Kind::boss_defeated && events[0].text == "blosso", "ded_e_blosso: " +
          std::to_string(events.size()));
    check(events[1].kind == PortEvent::Kind::story_flag && events[1].value == 119 && events[2].value == 508, "its flags");
    check(engine.port_boss_scripts().empty(), "closed");
    // A group boss's death script runs once per part; only the last sets flags.
    check(port_script_started(engine, "ded_e_mecha4.lua"), "mecha4");
    port_script_finished(engine);
    check(engine.take_port_events().empty(), "a part of the group: no defeat");
    // A flag already set still ends the boss (FLGset of an unchanged flag).
    port_script_started(engine, "ded_e_mecha4.lua");
    port_flag_changed(engine, 157, 1, 1);
    port_flag_changed(engine, 605, 0, 1);
    port_script_finished(engine);
    events = engine.take_port_events();
    check(events.size() == 2 && events[0].text == "mecha4" && events[1].kind == PortEvent::Kind::story_flag, "reported once per run");
    // Nested scripts: the inner one's flags belong to the boss script.
    port_script_started(engine, "ded_e_diabolo.lua");
    check(!port_script_started(engine, "hit_e_nume.lua"), "other scripts are not watched");
    port_flag_changed(engine, 1, 0, 1);
    port_script_finished(engine);
    events = engine.take_port_events();
    check(events.size() == 2 && events[0].text == "diabolo", "nested flag");
    check(!port_script_started(engine, "ded_g_d1bkwall.lua"), "wall death scripts are no boss");
    // Everyone down: the script returns before its flags.
    port_script_started(engine, "ded_e_mammos.lua");
    port_script_finished(engine);
    check(engine.take_port_events().empty(), "no flag, no defeat");
}

void cards_and_cap() {
    Engine engine{dw4::MountedResources{}};
    auto* sys = engine.get<GameSystem>();
    sys->update(engine);
    engine.set_port_events(true);
    auto call = [&](const char* op, int card) {
        std::vector<dw4::Lua4Value> args{dw4::Lua4Value::string(op), dw4::Lua4Value::number(card)};
        std::vector<dw4::Lua4Value> out;
        check(sys->script_call(engine, "SYSctlCardFolder", args, out) && out.size() == 1, "SYSctlCardFolder answers");
        return int(out[0].as_number());
    };
    check(call("SET", 12) == 1, "a new card");
    auto events = engine.take_port_events();
    check(events.size() == 1 && events[0].kind == PortEvent::Kind::card && events[0].value == 12, "the card event");
    check(call("SET", 12) == 0 && engine.take_port_events().empty(), "an owned card is no new event");
    check(call("CLR", 12) == 1 && engine.take_port_events().empty(), "a cleared card is no event");
    for (std::size_t i = 0; i < Engine::kMaxPortEvents + 10; ++i) engine.port_event({PortEvent::Kind::critical_hit, 0, 0, 0, {}});
    check(engine.take_port_events().size() == Engine::kMaxPortEvents, "at most kMaxPortEvents wait");
    engine.port_event({PortEvent::Kind::critical_hit, 0, 0, 0, {}});
    engine.set_port_events(false);
    check(engine.take_port_events().empty(), "switching off drops what waits");
}

void observe_only() {
    // The same calls on two engines, one collecting: the same fingerprint
    // and the same game state.
    Engine quiet{dw4::MountedResources{}};
    Engine collecting{dw4::MountedResources{}};
    collecting.set_port_events(true);
    for (Engine* engine : {&quiet, &collecting}) {
        auto* sys = engine->get<GameSystem>();
        sys->update(*engine);
        std::vector<dw4::Lua4Value> args{dw4::Lua4Value::string("SET"), dw4::Lua4Value::number(3)};
        std::vector<dw4::Lua4Value> out;
        sys->script_call(*engine, "SYSctlCardFolder", args, out);
        port_flag_changed(*engine, 68, 0, 1);
        const bool watched = port_script_started(*engine, "ded_e_skulgrey.lua");
        port_flag_changed(*engine, 120, 0, 1);
        if (watched) port_script_finished(*engine);
    }
    check(!collecting.take_port_events().empty(), "the collecting engine has events");
    check(quiet.fingerprint() == collecting.fingerprint(), "collecting leaves the fingerprint unchanged");
    check(quiet.get<GameSystem>()->players[0].cards == collecting.get<GameSystem>()->players[0].cards, "the same card list");
    check(quiet.flags() == collecting.flags(), "the same flags");
    check(std::string(port_event_name(PortEvent::Kind::quest_done)) == "quest_done", "event names");
}

} // namespace

int main() {
    try {
        off_by_default();
        flags_and_scripts();
        cards_and_cap();
        observe_only();
        std::cout << checks << " port event checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Port event test failure: " << error.what() << '\n';
        return 1;
    }
}
