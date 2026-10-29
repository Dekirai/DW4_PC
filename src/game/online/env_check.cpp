#include "env_check.hpp"

#include "dw4/engine/online.hpp"

namespace dw4::game::online {

// One list for the whole program: the engine's (src/engine/online.cpp,
// docs/SEAMLESS.md 6.12). Simulation variables lock the online start;
// presentation, diagnostics, the host around the engine, the test bots
// (their pads travel as input) and the online client's own switches do not.
// An entry ending in '*' is a prefix (DW4_TRACE_*).
std::span<const std::string_view> online_forbidden_env() { return engine::online_forbidden_env(); }

std::span<const std::string_view> online_allowed_env() { return engine::online_presentation_env(); }

bool env_listed(std::string_view name, std::span<const std::string_view> list) {
    for (const auto entry : list) {
        if (!entry.empty() && entry.back() == '*') {
            if (name.substr(0, entry.size() - 1) == entry.substr(0, entry.size() - 1)) return true;
        } else if (name == entry) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> first_forbidden_env(const char* (*lookup)(const char*)) {
    for (const auto name : online_forbidden_env()) {
        const std::string key(name);
        if (const char* value = lookup(key.c_str()); value && *value) return key;
    }
    return std::nullopt;
}

std::string env_refusal(std::string_view variable) {
    return "Online play is locked while the test variable " + std::string(variable) +
           " is set (it changes the game and would break the session). Unset it and start the game again.";
}

} // namespace dw4::game::online
