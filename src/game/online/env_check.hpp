#pragma once

// The environment check of the online session start (docs/SEAMLESS.md 6.12):
// every DW4_* environment variable the program reads is classified. A
// simulation variable (cheats, test starts, clocks, injected records, the
// randomizer, mods) would desync silently online or ride along in a new
// instance's snapshot, so the session start refuses while one is set and
// names it. Presentation and harness variables (traces, dumps, windows,
// pictures, the test bots, which only produce pads that travel as input)
// are allowed. The ctest `online_env` greps every "DW4_..." literal in src
// and include and fails for a name that is on neither list.
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dw4::game::online {

// Variables that change the simulation (or what an engine is built from).
std::span<const std::string_view> online_forbidden_env();
// Variables that only change presentation, diagnostics or the host around
// the engine, plus the online client's own development switches.
std::span<const std::string_view> online_allowed_env();

// The first forbidden variable that is set (non-empty) in this process.
// `lookup` reads one variable (std::getenv in the game, a map in tests).
std::optional<std::string> first_forbidden_env(const char* (*lookup)(const char*));

// The refusal text for that variable.
std::string env_refusal(std::string_view variable);

} // namespace dw4::game::online
