#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace dw4::engine {
namespace {

std::optional<std::string> opt_string(const Lua4Value& value) {
    if (value.type() == Lua4Type::String) return std::string(value.as_string());
    if (value.type() == Lua4Type::Number) {
        const double n = value.as_number();
        if (std::floor(n) == n) return std::to_string(static_cast<long long>(n));
        return std::to_string(n);
    }
    return std::nullopt;
}

std::int32_t to_int(const Lua4Value& value, std::int32_t fallback = 0) {
    if (value.type() == Lua4Type::Number) return static_cast<std::int32_t>(value.as_number());
    if (value.type() == Lua4Type::String) {
        try { return std::stoi(std::string(value.as_string())); } catch (...) { return fallback; }
    }
    return fallback;
}

double to_number(const Lua4Value& value, double fallback = 0) {
    if (value.type() == Lua4Type::Number) return value.as_number();
    return fallback;
}

std::vector<Lua4Value> number(double value) { return {Lua4Value::number(value)}; }
std::vector<Lua4Value> string_result(std::string value) { return {Lua4Value::string(std::move(value))}; }

} // namespace

void Engine::install_lua() {
    lua_ = std::make_unique<Lua4Vm>();
    lua_->set_global("be", Lua4Value::host_object("be"));
    lua_->set_global("gsub", Lua4Value::host_function("gsub"));
    lua_->set_resumable_host_handler([this](const Lua4HostCall& call) {
        Lua4HostResult result;
        try {
            result.returned = lua_call(call);
        } catch (const std::exception& error) {
            note(std::string("script call failed: ") + call.name + ": " + error.what());
            result.returned.clear();
        }
        result.completed = true;
        return result;
    });
}

bool Engine::run_script(std::string_view name) {
    if (!lua_) install_lua();
    bool ran = false;
    for (const auto& info : lua_infos_) {
        for (const auto& data : info.list(7)) {
            if (!data || data.str(2) != name) continue;
            const auto object = data.ref(3);
            if (!object) break;
            const auto key = std::make_pair(static_cast<const void*>(object.document()), object.reference());
            auto found = lua_programs_.find(key);
            if (found == lua_programs_.end()) {
                try {
                    const auto bytes = object.memory(3);
                    found = lua_programs_.emplace(key, read_lua4_chunk(bytes, std::string(name))).first;
                } catch (const std::exception& error) {
                    log("script decode failed: " + std::string(name) + ": " + error.what());
                    break;
                }
            }
            ++script_depth_;
            Lua4RunResult run;
            if (script_depth_ > 1) {
                auto execution = lua_->begin_nested(found->second);
                run = lua_->resume(execution);
            } else {
                auto execution = lua_->begin_execution(found->second);
                run = lua_->resume(execution);
            }
            --script_depth_;
            ran = true;
            if (!run.completed) {
                log("script " + std::string(name) + " stopped: " + run.diagnostic);
                if (script_depth_ == 0) {
                    // Keep going with a fresh VM (restore shared helpers).
                    install_lua();
                    if (name != "func.lua") run_script("func.lua");
                }
                return ran;
            }
            break; // first match per info only
        }
    }
    if (!ran) note("script not found: " + std::string(name));
    return ran;
}

std::vector<Lua4Value> Engine::lua_call(const Lua4HostCall& call) {
    const auto& name = call.name;
    if (name == "gsub") {
        if (call.arguments.size() < 3) return {};
        std::string value(call.arguments[0].as_string());
        const std::string pattern(call.arguments[1].as_string());
        const std::string replacement(call.arguments[2].as_string());
        std::size_t count = 0, at = 0;
        while (!pattern.empty() && (at = value.find(pattern, at)) != std::string::npos) {
            value.replace(at, pattern.size(), replacement);
            at += replacement.size();
            ++count;
        }
        return {Lua4Value::string(value), Lua4Value::number(double(count))};
    }
    if (!name.starts_with("be.")) {
        note("script function " + name);
        return {};
    }
    const std::string method = name.substr(3);
    std::span<const Lua4Value> args = call.arguments;
    if (!args.empty() && args[0].type() == Lua4Type::HostObject) args = args.subspan(1);
    auto arg = [&](std::size_t i) -> const Lua4Value& {
        static const Lua4Value nil;
        return i < args.size() ? args[i] : nil;
    };

    // --- Common file list (beFileListInfoManager) ---
    if (method == "CFLclear") { common_file_queue().clear(); return {}; }
    if (method == "CFLsetName") {
        if (auto path = opt_string(arg(0))) {
            std::string lower = *path;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            common_file_queue().push_back(lower);
        }
        return {};
    }
    if (method == "CFLgetFolderName") return string_result(folder_name());

    // --- Messenger ---
    if (method == "sd") {
        const auto category = opt_string(arg(0)).value_or("");
        const auto target = opt_string(arg(1));
        const auto index = to_int(arg(2), -1);
        const auto command = opt_string(arg(3)).value_or("");
        Packet packet;
        std::int64_t delay = 0;
        if (args.size() > 4) {
            if (arg(4).type() == Lua4Type::String) parse_packet_arguments(arg(4).as_string(), packet);
            else if (arg(4).type() == Lua4Type::Number && args.size() == 5) delay = static_cast<std::int64_t>(arg(4).as_number());
        }
        if (args.size() > 5 && arg(5).type() == Lua4Type::Number) delay = static_cast<std::int64_t>(arg(5).as_number());
        if (delay > 0) {
            messenger().post(time() + kFrameNs * delay, category, target, index, command, std::move(packet));
            return {};
        }
        std::optional<std::string_view> target_view;
        if (target) target_view = *target;
        messenger().send(*this, category, target_view, index, command, packet);
        if (!packet.bools.empty()) return number(packet.bools[0]);
        if (!packet.ints.empty()) return number(packet.ints[0]);
        return {};
    }
    if (method == "DMsetSD" || method == "DMchkSD" || method == "DMswtSD") {
        DemoRecord record;
        record.category = opt_string(arg(0)).value_or("");
        record.name = opt_string(arg(1));
        record.index = to_int(arg(2), -1);
        record.command = opt_string(arg(3)).value_or("");
        if (args.size() > 4 && arg(4).type() == Lua4Type::String) record.argument = std::string(arg(4).as_string());
        if (method == "DMchkSD") {
            const auto condition = opt_string(arg(5)).value_or("");
            record.type = (condition == "!=0" || condition == "==1") ? 2 : 1;
            demo().add(std::move(record));
        } else if (method == "DMswtSD") {
            demo().begin_switch(std::move(record));
        } else {
            if (args.size() > 5 && arg(5).type() == Lua4Type::Number) record.delay_frames = static_cast<std::int64_t>(arg(5).as_number());
            else if (args.size() == 5 && arg(4).type() == Lua4Type::Number) record.delay_frames = static_cast<std::int64_t>(arg(4).as_number());
            record.type = 0;
            demo().add(std::move(record));
        }
        return {};
    }
    if (method == "DMcase") { demo().add_case(to_int(arg(0))); return {}; }
    if (method == "DMswtEnd") { demo().end_switch(); return {}; }
    if (method == "DMrun") { demo().run(); return {}; }
    if (method == "DMsetLua") {
        DemoRecord record;
        record.type = 7;
        record.argument = opt_string(arg(0));
        demo().add(std::move(record));
        return {};
    }
    if (method == "DMgetCount") return number(double(demo().running()));
    if (method == "MsgBox" || method == "DMmsgBox") {
        log("MsgBox: " + opt_string(arg(0)).value_or(""));
        return {};
    }

    // --- Flags / counters (game RAM) ---
    if (method == "FLGisOn") return number(flags()[to_int(arg(0))] ? 1 : 0);
    if (method == "FLGset") { flags()[to_int(arg(0))] = to_int(arg(1)) ? 1 : 0; return {}; }
    if (method == "FLGflip") { auto& f = flags()[to_int(arg(0))]; f = f ? 0 : 1; return {}; }
    if (method == "COUNTERset") { counters()[to_int(arg(0))] = to_int(arg(1)); return {}; }
    if (method == "COUNTERget") return number(counters()[to_int(arg(0))]);

    // --- System queries ---
    if (method == "SYSgetArea") return number(area());
    if (method == "SYSgetCountry") return number(country());
    if (method == "SYSgetIsPackOnly") return number(1);
    if (method == "SYSgetRandom") {
        static std::mt19937 rng{0x4457344u};
        const auto limit = std::max(1, to_int(arg(0), 1));
        return number(double(rng() % std::uint32_t(limit)));
    }
    if (auto* sys = get<GameSystem>()) {
        std::vector<Lua4Value> out;
        if (sys->script_call(*this, method, args, out)) return out;
    }
    note("script method be:" + method);
    // Unknown queries answer 0 so numeric comparisons in the original
    // scripts keep running; the call is reported as unimplemented.
    return number(0);
}

} // namespace dw4::engine
