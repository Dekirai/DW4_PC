#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    lua_->set_global("strfind", Lua4Value::host_function("strfind"));
    lua_->set_global("format", Lua4Value::host_function("format"));
    for (const char* name : {"floor", "ceil", "getn"}) lua_->set_global(name, Lua4Value::host_function(name));
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

bool Engine::has_script(std::string_view name) const {
    for (const auto& info : lua_infos_)
        for (const auto& data : info.list(7))
            if (data && data.str(2) == name) return true;
    return false;
}

bool Engine::script_mentions(std::string_view name, std::string_view text) const {
    for (const auto& info : lua_infos_)
        for (const auto& data : info.list(7)) {
            if (!data || data.str(2) != name) continue;
            const auto object = data.ref(3);
            if (!object) continue;
            const auto bytes = object.memory(3);
            const std::string_view chunk(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (chunk.find(text) != std::string_view::npos) return true;
        }
    return false;
}

bool Engine::run_script(std::string_view name) {
    if (!lua_) install_lua();
    // DW4_TRACE_SCRIPT=1 logs every script run (=name: only that one).
    if (static const char* trace = std::getenv("DW4_TRACE_SCRIPT"); trace && (std::string_view(trace) == "1" || name == trace))
        log("script " + std::string(name));
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
            try {
                if (script_depth_ > 1) {
                    auto execution = lua_->begin_nested(found->second);
                    run = lua_->resume(execution);
                } else {
                    auto execution = lua_->begin_execution(found->second);
                    run = lua_->resume(execution);
                }
            } catch (const std::exception& error) {
                run.diagnostic = error.what(); // A stopped VM refuses to begin.
            }
            --script_depth_;
            ran = true;
            // DW4_TRACE_LUA_STACK=1 logs each new high of the Lua stack (the
            // deepest luaD_checkstack top + reserve, 1023 overflows) and the
            // slot where a nested chunk started.
            if (static const bool trace_stack = std::getenv("DW4_TRACE_LUA_STACK") != nullptr; trace_stack) {
                static std::size_t logged = 0;
                if (script_depth_ > 0) log("lua nested run " + std::string(name) + " at slot " + std::to_string(run.stack_base));
                else if (const auto peak = lua_->heap_stats().stack_peak; peak > logged) {
                    logged = peak;
                    log("lua stack peak " + std::to_string(peak) + " of 1023 after " + std::string(name));
                }
            }
            if (!run.completed) {
                // igLuaState::runBuffer (80272768) only logs "runBuffer(%s)
                // failed - reason %s" after the script's _ERRORMESSAGE ran
                // (be:MsgBox): the half-run chunk's frames are gone
                // (luaD_runprotected restored the stack top), the lua_State
                // and all stage globals stay, and a nested run's caller goes
                // on. Only a host fault stops the port's VM for good; then a
                // fresh VM gets func.lua's helpers back.
                log("script " + std::string(name) + " stopped: " + run.diagnostic);
                if (script_depth_ == 0 && lua_->stopped()) {
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
    // Lua 4's string library takes numbers as strings: str_find / str_gsub
    // (0x80280594) read their arguments through luaL_check_lstr 0x80274120
    // -> lua_tostring 0x80273120, which converts a number with luaV_tostring
    // 0x80282818 (sprintf "%.16g" on the double, 0x805612F0), as the VM's
    // own concatenation does (describe). stagML11 mate00.lua runs strfind
    // over its area list, whose player points are numbers.
    const auto text_of = [](const Lua4Value& value) {
        if (value.type() != Lua4Type::Number) return std::string(value.as_string());
        return value.describe();
    };
    if (name == "gsub") {
        if (call.arguments.size() < 3) return {};
        std::string value = text_of(call.arguments[0]);
        const std::string pattern = text_of(call.arguments[1]);
        const std::string replacement = text_of(call.arguments[2]);
        std::size_t count = 0, at = 0;
        while (!pattern.empty() && (at = value.find(pattern, at)) != std::string::npos) {
            value.replace(at, pattern.size(), replacement);
            at += replacement.size();
            ++count;
        }
        return {Lua4Value::string(value), Lua4Value::number(double(count))};
    }
    if (name == "strfind") {
        // Lua 4 strfind(s, pattern [, init [, plain]]): the scripts only search
        // literal text, so patterns are matched literally (magic characters noted).
        if (call.arguments.size() < 2) return {Lua4Value{}};
        const std::string value = text_of(call.arguments[0]);
        const std::string pattern = text_of(call.arguments[1]);
        if (pattern.find_first_of("^$()%.[]*+-?") != std::string::npos) note("strfind pattern " + pattern);
        long long init = call.arguments.size() > 2 && call.arguments[2].type() == Lua4Type::Number
                             ? static_cast<long long>(call.arguments[2].as_number()) : 1;
        if (init < 0) init = std::max<long long>(1, static_cast<long long>(value.size()) + init + 1);
        const auto at = init <= 0 ? 0 : std::size_t(init - 1);
        const auto found = at <= value.size() ? value.find(pattern, at) : std::string::npos;
        if (found == std::string::npos) return {Lua4Value{}};
        return {Lua4Value::number(double(found + 1)), Lua4Value::number(double(found + pattern.size()))};
    }
    if (name == "floor" || name == "ceil") {
        const double value = call.arguments.empty() ? 0.0 : to_number(call.arguments[0]);
        return {Lua4Value::number(name == "floor" ? std::floor(value) : std::ceil(value))};
    }
    if (name == "getn") {
        // Lua 4 getn(t): the "n" field, else the count of consecutive integer keys from 1.
        if (call.arguments.empty() || call.arguments[0].type() != Lua4Type::Table || !lua_) return {Lua4Value::number(0)};
        const auto& table = call.arguments[0];
        const auto n = lua_->table_get(table, Lua4Value::string("n"));
        if (n.type() == Lua4Type::Number) return {n};
        double count = 0;
        while (lua_->table_get(table, Lua4Value::number(count + 1)).truthy()) ++count;
        return {Lua4Value::number(count)};
    }
    if (name == "format") {
        // Lua 4 format(fmt, ...): printf conversions d i c x X o u e E f g G s q %.
        if (call.arguments.empty()) return {Lua4Value::string({})};
        const std::string fmt(call.arguments[0].as_string());
        std::string out;
        std::size_t next = 1;
        for (std::size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%') { out.push_back(fmt[i]); continue; }
            if (i + 1 < fmt.size() && fmt[i + 1] == '%') { out.push_back('%'); ++i; continue; }
            std::size_t j = i + 1;
            while (j < fmt.size() && std::strchr("-+ #0123456789.", fmt[j])) ++j;
            if (j >= fmt.size()) break;
            const char conversion = fmt[j];
            const std::string spec = fmt.substr(i, j - i);
            const Lua4Value none;
            const Lua4Value& value = next < call.arguments.size() ? call.arguments[next] : none;
            ++next;
            char buffer[512];
            if (std::strchr("dic", conversion)) {
                std::snprintf(buffer, sizeof(buffer), (spec + conversion).c_str(), int(to_number(value)));
            } else if (std::strchr("xXou", conversion)) {
                std::snprintf(buffer, sizeof(buffer), (spec + conversion).c_str(), static_cast<unsigned>(static_cast<long long>(to_number(value))));
            } else if (std::strchr("eEfgG", conversion)) {
                std::snprintf(buffer, sizeof(buffer), (spec + conversion).c_str(), to_number(value));
            } else if (conversion == 's' || conversion == 'q') {
                std::string text = value.type() == Lua4Type::Number ? value.describe() : std::string(value.as_string());
                if (conversion == 'q') text = "\"" + text + "\"";
                std::snprintf(buffer, sizeof(buffer), (spec + 's').c_str(), text.c_str());
            } else {
                buffer[0] = 0;
            }
            out += buffer;
            i = j;
        }
        return {Lua4Value::string(out)};
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
            // 802EC8B8: a name already in the list is not added again.
            auto& queue = common_file_queue();
            if (std::find(queue.begin(), queue.end(), lower) == queue.end()) queue.push_back(lower);
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
            // Overloads: (cat, name, idx, cmd, cond) and (cat, name, idx, cmd, arg, cond).
            std::string condition;
            if (args.size() >= 6) condition = opt_string(arg(5)).value_or("");
            else {
                condition = opt_string(arg(4)).value_or("");
                record.argument.reset();
            }
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

    // --- Model queries ---
    // Explicit form (name, index): routed to whichever model category owns it.
    auto explicit_model = [&](std::string_view command, std::optional<std::string> argument) -> std::optional<bool> {
        const auto target = opt_string(arg(0)).value_or("");
        const auto index = to_int(arg(1), 0);
        for (const char* category : {"MODELCTRL", "PLAYER", "ENEMY", "PLAYER2", "EFFECT", "ITEM"}) {
            if (!messenger().count(category, target, index)) continue;
            Packet packet;
            if (argument) packet.strings.emplace_back(*argument);
            messenger().send(*this, category, target, index, command, packet);
            return !packet.bools.empty() && packet.bools[0];
        }
        return std::nullopt;
    };
    // Context form: the model the running hit/LUACALL script belongs to (8030B6EC).
    auto* context = model_context();
    auto context_model = [&]() -> ModelCtrlManager::ModelRam* {
        auto* models = get<ModelCtrlManager>();
        return models && context ? models->find(context->name, context->index) : nullptr;
    };
    auto vector_string = [](Vec3f v) {
        char buffer[96];
        std::snprintf(buffer, sizeof buffer, "V<%f,%f,%f>", v.x, v.y, v.z);
        return std::string(buffer);
    };
    if (method == "MDLsetWorldPosTarget") {
        // 802EDF20: only while an AIMAP WPOS call waits for the target.
        if (context && context->world_target)
            *context->world_target = {float(to_number(arg(0))), float(to_number(arg(1))), float(to_number(arg(2)))};
        return {};
    }
    if (method == "MDLgetKyoriSquare") {
        // 802EDF84: squared distance from the model to the point.
        auto* model = context_model();
        if (!model || !model->node) return number(0);
        const auto& m = model->node->matrix;
        const float dx = m(3, 0) - float(to_number(arg(0))), dy = m(3, 1) - float(to_number(arg(1))),
                    dz = m(3, 2) - float(to_number(arg(2)));
        return number(dx * dx + dy * dy + dz * dz);
    }
    if (method == "MDLgetBulletBoxNo") return number(context ? context->bullet_box : 0); // 802EDC44
    if (method == "MDLisLive") {
        if (args.size() >= 2) return number(explicit_model("ISRUN", std::nullopt).value_or(false) ? 1 : 0);
        auto* model = context_model();
        return number(model && model->generated ? 1 : 0);
    }
    if (method == "MDLisSeqName") {
        if (args.size() >= 3) return number(explicit_model("ISSEQNAME", opt_string(arg(2)).value_or("")).value_or(false) ? 1 : 0);
        auto* model = context_model();
        return number(model && model->generated && model->seq_name == opt_string(arg(0)).value_or("") ? 1 : 0);
    }
    if (method == "MDLsetSeq") {
        if (args.size() >= 3) {
            explicit_model(opt_string(arg(2)).value_or(""), std::nullopt);
            return {};
        }
        if (context) {
            Packet packet;
            messenger().send(*this, "MODELCTRL", context->name, context->index, opt_string(arg(0)).value_or(""), packet);
        }
        return {};
    }
    if (method == "MDLgetRamNo") {
        if (args.size() >= 2) return number(to_int(arg(1), 0));
        return number(context ? context->index : 0);
    }
    if (method == "MDLsetDMGTurn") {
        if (context) context->turn = to_int(arg(0)) != 0;
        return {};
    }
    if (method == "MDLisCallName") return number(context && context->call_name == opt_string(arg(0)).value_or("") ? 1 : 0);
    if (method == "MDLgetBulletKind") return number(context ? context->bullet_kind : 0);
    if (method == "BLTgetShootRamNo") return number(context ? context->shooter : 0);
    if (method == "MDLgetPosStr" || method == "MDLgetRotStr") {
        // 802ED8E0 / 802ED9A4: from the script model's node matrix (the
        // "ONE" node, 8030B81C). The position adds the arguments along the
        // model's own axes (80128DC8); the rotation is the yaw of its +Z
        // axis (8012D434) plus the Y argument, X and Z pass through.
        auto* model = context_model();
        const float a = float(to_number(arg(0))), b = float(to_number(arg(1))), c = float(to_number(arg(2)));
        Vec3f value{a, b, c};
        if (model && model->node) {
            const auto& m = model->node->matrix;
            if (method == "MDLgetPosStr") {
                // 80128DC8 per column: b*row1, + a*row0, + c*row2 (fused),
                // then the translation.
                auto column = [&](int j) { return m(3, j) + std::fma(m(2, j), c, std::fma(m(0, j), a, m(1, j) * b)); };
                value = {column(0), column(1), column(2)};
            } else {
                constexpr float to_degrees = 57.29577951308232f; // 80424B38 (0x42652EE1)
                value = {a, std::atan2(m(2, 0), m(2, 2)) * to_degrees + b, c};
            }
        }
        return string_result(vector_string(value));
    }
    if (method == "MDLgetHp" || method == "MDLaddHp") {
        auto* model = context_model();
        if (!model) return number(0);
        if (method == "MDLaddHp") {
            // God mode cheat (DW4_GOD in test runs) keeps the players alive.
            if (cheats().god && model->is_player && to_int(arg(0)) < 0) return {};
            model->hp = std::clamp(model->hp + to_int(arg(0)), 0, std::max(model->max_hp, model->hp + to_int(arg(0))));
            // 8030B6EC publishes a player's HP to its game RAM row.
            if (model->is_player)
                if (auto* sys = get<GameSystem>()) sys->players[std::size_t(std::clamp(model->index, 0, 3))].hp = model->hp;
            return {};
        }
        return number(model->hp);
    }
    if (method == "MDLgetMHp") {
        auto* model = context_model();
        return number(model ? model->max_hp : 0);
    }
    if (method == "MDLgetRecNo") {
        auto* model = context_model();
        return number(model ? std::max(model->record, 0) : 0); // work +10, zero unless GENERATE set it
    }
    if (method == "MDLgetInfoName") return string_result(context ? context->name : std::string());
    if (method == "MDLsetSD") {
        // Context form (command, "a/b/I<n>"): message to the script's own model.
        if (context) {
            Packet packet;
            if (args.size() > 1 && arg(1).type() == Lua4Type::String) parse_packet_arguments(arg(1).as_string(), packet);
            messenger().send(*this, context->category, context->name, context->index, opt_string(arg(0)).value_or(""), packet);
        }
        return {};
    }
    if (method == "MDLisStatus") {
        // 802EE144: MODELCTRL <name> <ram> CHECKSTATUS <status> answers one flag.
        Packet packet;
        packet.strings.emplace_back(opt_string(arg(2)).value_or(""));
        messenger().send(*this, "MODELCTRL", opt_string(arg(0)).value_or(""), to_int(arg(1)), "CHECKSTATUS", packet);
        return number(packet.bools.size() == 1 && packet.bools[0] ? 1 : 0);
    }

    // --- Battle queries of hit scripts (FIGHTCALC P2E*, 802EE5C4..802EEA2C) ---
    auto shooter_slot = [&]() -> const GameSystem::PlayerSlot* {
        auto* sys = get<GameSystem>();
        if (!sys || !context || !context->shooter_name.starts_with('p') || context->shooter < 0 || context->shooter > 3) return nullptr;
        auto& slot = sys->players[std::size_t(context->shooter)];
        if (!slot.stats.ready) sys->prepare_player(*this, context->shooter);
        return &slot;
    };
    auto shooter_stats = [&]() -> const GameSystem::Stats* {
        const auto* slot = shooter_slot();
        return slot ? &slot->stats : nullptr;
    };
    const int bullet_kind = context ? context->bullet_kind : 0;
    // GETPEBASEWORK: a model's DOWN value for the FIGHTCALC paths.
    auto down_of = [&](ModelCtrlManager::ModelRam* model, int index) {
        auto* models = get<ModelCtrlManager>();
        return models && model ? models->down_value(*this, *model, index) : 0;
    };
    if (method == "MDLgetBulletName") return string_result(fight::bullet_name(bullet_kind));
    if (method == "BLTisPlayerBullet") return number(context && context->shooter_name.starts_with('p') ? 1 : 0);
    if (method == "BLTisHitCheck") {
        const auto* stats = shooter_stats();
        auto* model = context_model();
        if (!stats || !model) return number(1);
        return number(fight::player_hits(*this, *stats, model->record, bullet_kind, down_of(model, 3)) ? 1 : 0);
    }
    if (method == "BLTisCritical") {
        const auto* slot = shooter_slot();
        auto* model = context_model();
        return number(slot && model && fight::player_critical(*this, *slot, model->record, bullet_kind) ? 1 : 0);
    }
    if (method == "BLTgetDamageValue") {
        const auto* slot = shooter_slot();
        auto* model = context_model();
        if (!slot || !model || !context) return number(0);
        auto result =
            fight::player_damage(*this, *slot, model->record, bullet_kind, to_int(arg(0)) != 0, down_of(model, 5));
        // Rebalancing option, then the one-hit cheat (all the target's HP).
        if (cheats().damage_dealt != 100 && result.damage > 0)
            result.damage = std::max(1, int(std::int64_t(result.damage) * cheats().damage_dealt / 100));
        if (cheats().one_hit && result.damage > 0) result.damage = std::max(result.damage, model->hp);
        context->damage = result.damage;
        context->attribute = result.attribute;
        context->damage_flag = result.flag;
        // HPDRAIN / MPDRAIN of the weapon chips return a share of weapon damage.
        if (bullet_kind / 10000 != 4 && result.damage > 0) {
            auto* sys = get<GameSystem>();
            if (slot->stats.hp_drain) sys->change_hp(*this, context->shooter, result.damage * slot->stats.hp_drain / 100);
            if (slot->stats.mp_drain) sys->change_mp(*this, context->shooter, result.damage * slot->stats.mp_drain / 100);
        }
        return number(result.damage);
    }
    if (method == "BLTgetDamageRank") {
        auto* model = context_model();
        FightResult result;
        if (context) result = {context->damage, context->attribute, context->damage_flag};
        return number(fight::damage_rank(to_int(arg(0)), model ? model->max_hp : 0, result, to_int(arg(1)) != 0, bullet_kind));
    }
    if (method == "BLPgetAttackZokusei") {
        // 802EF518: FIGHTCALC GETPATKZOKUSEI of the shooter and bullet.
        static const char* names[] = {"fire", "ice", "thunder", "dark"};
        const auto* slot = shooter_slot();
        const int attribute = slot ? fight::player_attack_attribute(*this, *slot, bullet_kind) : -1;
        return string_result(attribute >= 0 && attribute < 4 ? names[attribute] : "common");
    }
    if (method == "BLTgetAttributeStr") {
        static const char* names[] = {"fire", "ice", "thunder", "dark"};
        const int attribute = context ? context->attribute : -1;
        return string_result(attribute >= 0 && attribute < 4 ? names[attribute] : "common");
    }
    if (method == "BLTcalcEXPUp") {
        auto* model = context_model();
        auto* sys = get<GameSystem>();
        if (!model || !sys || !context) return number(0);
        const int killer = context->shooter_name.starts_with('p') ? context->shooter : -1;
        return number(sys->experience_up(*this, killer, model->record));
    }
    if (method == "BLTcalcSkillUp") {
        // 802EF198: P2ESKILLUP for a player shooter (hit flag, critical).
        if (shooter_slot()) fight::skill_up(*this, context->shooter, bullet_kind, to_int(arg(0)), to_int(arg(1)));
        return {};
    }
    if (method == "MDLdspDamage" || method == "MDLdspDamageP" || method == "MDLdspExp") {
        // 802EAE28..48: floating damage / player damage / EXP numbers at the model.
        const int value = to_int(arg(0));
        if (method != "MDLdspExp") {
            // (DW4_TRACE_DAMAGE: the shooter and the bullet kind too, a test aid.)
            static const bool by = std::getenv("DW4_TRACE_DAMAGE") != nullptr;
            log("damage " + std::to_string(value) + (context ? " -> " + context->name : std::string()) +
                (by && context ? " by " + context->shooter_name + "#" + std::to_string(context->shooter) + " kind " + std::to_string(context->bullet_kind)
                               : std::string()));
        }
        if (auto* model = context_model(); model && model->node) {
            const auto& m = model->node->matrix;
            show_number(*this, method == "MDLdspDamage" ? 0 : method == "MDLdspDamageP" ? 3 : 1, value, Vec3f{m(3, 0), m(3, 1), m(3, 2)});
        }
        return {};
    }
    if (method.starts_with("BLTget") && method.ends_with("Time")) {
        // P2E* status times (802EEA64..802EF050): stun after any hit; the
        // others only for their technique (kind 4xxxx with its family).
        const auto* slot = shooter_slot();
        auto* model = context_model();
        if (!slot || !model) return number(0);
        const std::string what = opt_string(arg(0)).value_or("");
        auto technique = [&](int family) { return bullet_kind / 10000 == 4 && bullet_kind % 10000 == family; };
        if (method == "BLTgetStunTime")
            return number(fight::player_stun_time(*this, slot->stats, model->record, model->max_hp, to_int(arg(0))));
        if (method == "BLTgetPoisonTime")
            return number(technique(20) ? fight::player_poison(*this, *slot, model->record, bullet_kind, what) : 0);
        if (method == "BLTgetSleepTime")
            return number(technique(90) ? fight::player_sleep_time(*this, *slot, model->record, bullet_kind) : 0);
        if (method == "BLTgetBindTime")
            return number(technique(95) ? fight::player_sleep_time(*this, *slot, model->record, bullet_kind) : 0);
        if (method == "BLTgetAttackDownTime")
            return number(technique(70) ? fight::player_attack_down(*this, *slot, model->record, bullet_kind, what) : 0);
        if (method == "BLTgetPowerDownTime")
            return number(technique(85) ? fight::player_power_down(*this, *slot, model->record, bullet_kind, what) : 0);
        return number(0);
    }
    // Enemy-to-player hits (E2P*): the target is the player RAM, the shooter's
    // GETRECNO is its enemy record.
    auto target_stats = [&]() -> const GameSystem::Stats* {
        auto* sys = get<GameSystem>();
        auto* model = context_model();
        if (!sys || !model || !model->is_player) return nullptr;
        auto& slot = sys->players[std::size_t(std::clamp(model->index, 0, 3))];
        if (!slot.stats.ready) sys->prepare_player(*this, model->index);
        return &slot.stats;
    };
    auto shooter_record = [&]() {
        auto* models = get<ModelCtrlManager>();
        const auto* shooter = models && context ? models->find(context->shooter_name, context->shooter) : nullptr;
        return shooter ? std::max(shooter->record, 0) : -1;
    };
    if (method == "BLPisHitCheck") {
        const auto* stats = target_stats();
        return number(!stats || fight::enemy_hits(*this, *stats, shooter_record(), bullet_kind) ? 1 : 0);
    }
    if (method == "BLPcalcKoukaMagic") {
        // 802EFC64: P2PKOUKAMAGIC of the support bullet that reached a player.
        auto* model = context_model();
        if (!model || !model->is_player || !context) return number(0);
        return number(fight::support_magic(*this, model->index, context->shooter, bullet_kind));
    }
    if (method == "BLPgetDamageValue") {
        const auto* stats = target_stats();
        auto* model = context_model();
        if (!stats || !model || !context) return number(0);
        auto* models = get<ModelCtrlManager>();
        auto* shooter = models && context ? models->find(context->shooter_name, context->shooter) : nullptr;
        auto result = fight::enemy_damage(*this, *stats, model->max_hp, shooter_record(), bullet_kind, down_of(shooter, 4),
                                          down_of(shooter, 1));
        if (cheats().damage_taken != 100 && result.damage > 0)
            result.damage = std::max(1, int(std::int64_t(result.damage) * cheats().damage_taken / 100));
        context->damage = result.damage;
        context->attribute = result.attribute;
        context->damage_flag = result.flag;
        return number(result.damage);
    }
    if (method == "BLPgetDamageRank") {
        auto* model = context_model();
        FightResult result;
        if (context) result = {context->damage, context->attribute, context->damage_flag};
        return number(fight::enemy_damage_rank(*this, bullet_kind, to_int(arg(0)), to_int(arg(1)), model ? model->max_hp : 0, result));
    }
    if (method.starts_with("BLPget") && method.ends_with("Time")) {
        // E2P* status times (802EF68C..802EFB78) of the attacking enemy.
        auto* sys = get<GameSystem>();
        auto* model = context_model();
        if (!sys || !model || !model->is_player || !target_stats()) return number(0);
        const int slot = std::clamp(model->index, 0, 3);
        const auto& player = sys->players[std::size_t(slot)];
        const int record = shooter_record();
        const std::string what = opt_string(arg(0)).value_or("");
        if (method == "BLPgetStunTime") {
            auto* models = get<ModelCtrlManager>();
            auto* shooter = models && context ? models->find(context->shooter_name, context->shooter) : nullptr;
            return number(fight::enemy_stun_time(*this, player, record, bullet_kind, to_int(arg(0)), to_int(arg(1)), down_of(shooter, 2)));
        }
        if (method == "BLPgetPoisonTime") return number(fight::enemy_poison(*this, player, bullet_kind, what));
        if (method == "BLPgetSleepTime") return number(fight::enemy_sleep_time(*this, player.stats, record, bullet_kind));
        if (method == "BLPgetBindTime") return number(fight::enemy_bind_time(*this, player.stats, record, bullet_kind));
        if (method == "BLPgetAttackDownTime") return number(fight::enemy_attack_down(*this, slot, record, bullet_kind, what));
        if (method == "BLPgetPowerDownTime") return number(fight::enemy_power_down(*this, slot, record, bullet_kind, what));
        return number(0);
    }

    // --- System queries ---
    if (method == "SYSgetArea") return number(area());
    if (method == "SYSgetCountry") return number(country());
    if (method == "SYSgetIsPackOnly") return number(1);
    if (method == "SYSgetTarget") return number(2); // 802EBC98
    if (method == "SYSctlShinka") {
        // 802EC590: GETEXP / GETLEVEL of a reached species (-1 otherwise);
        // SETSHINKA marks one reached (1 when it was new).
        auto* sys = get<GameSystem>();
        const std::string op = opt_string(arg(0)).value_or("");
        const int species = to_int(arg(2));
        if (!sys || species < 0 || species >= 16) return number(0);
        auto& player = sys->players[std::size_t(std::clamp(to_int(arg(1)), 0, 3))];
        const auto found = player.base_isuse.find(species);
        const bool reached = found != player.base_isuse.end() && found->second != 0;
        if (op == "GETEXP") return number(reached ? player.base_exp[std::size_t(species)] : -1);
        if (op == "GETLEVEL") return number(reached ? player.base_level[std::size_t(species)] : -1);
        if (op == "SETSHINKA") {
            if (reached) return number(0);
            player.base_isuse[species] = 1;
            return number(1);
        }
        return number(0);
    }
    if (method == "PLAYERgetPosStr") {
        // 802EE36C: the player's position moved by a local offset; nil
        // without the player.
        Packet packet;
        messenger().send(*this, "PLAYER", std::nullopt, to_int(arg(0)), "GETMATRIX", packet);
        if (packet.matrices.empty()) return {Lua4Value{}};
        const auto& m = packet.matrices[0];
        const float x = float(to_number(arg(1))), y = float(to_number(arg(2))), z = float(to_number(arg(3)));
        char buffer[96];
        std::snprintf(buffer, sizeof buffer, "V<%f,%f,%f>", m(3, 0) + x * m(0, 0) + y * m(1, 0) + z * m(2, 0),
                      m(3, 1) + x * m(0, 1) + y * m(1, 1) + z * m(2, 1), m(3, 2) + x * m(0, 2) + y * m(1, 2) + z * m(2, 2));
        return string_result(buffer);
    }
    if (method == "PLAYERgetDataStr") {
        // 802EE240: PLAYERPOINT GETMATRIXDIRECT as a placement, the heading
        // in degrees plus the given angle.
        Packet packet;
        packet.ints = {to_int(arg(0)), to_int(arg(1))};
        messenger().send(*this, "PLAYERPOINT", std::nullopt, -1, "GETMATRIXDIRECT", packet);
        const Mat4 m = packet.matrices.empty() ? Mat4{} : packet.matrices[0];
        const float yaw = std::atan2(m(2, 0), m(2, 2)) * 57.29578f + float(to_number(arg(2)));
        char buffer[128];
        std::snprintf(buffer, sizeof buffer, "V<%f,%f,%f>/V<0,%f,0>", m(3, 0), m(3, 1), m(3, 2), yaw);
        return string_result(buffer);
    }
    if (method == "SYSgetRandom") {
        // 802EBCA8: the system generator modulo the limit.
        const auto limit = std::max(1, to_int(arg(0), 1));
        return number(double(fight::random() % std::uint32_t(limit)));
    }
    if (method.starts_with("MAT")) {
        if (auto* mats = get<MatCtrlManager>()) {
            std::vector<Lua4Value> out;
            if (mats->script_call(*this, method, args, out)) return out;
        }
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
