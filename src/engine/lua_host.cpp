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
    if (name == "strfind") {
        // Lua 4 strfind(s, pattern [, init [, plain]]): the scripts only search
        // literal text, so patterns are matched literally (magic characters noted).
        if (call.arguments.size() < 2) return {Lua4Value{}};
        const std::string value(call.arguments[0].as_string());
        const std::string pattern(call.arguments[1].as_string());
        if (pattern.find_first_of("^$()%.[]*+-?") != std::string::npos) note("strfind pattern " + pattern);
        long long init = call.arguments.size() > 2 && call.arguments[2].type() == Lua4Type::Number
                             ? static_cast<long long>(call.arguments[2].as_number()) : 1;
        if (init < 0) init = std::max<long long>(1, static_cast<long long>(value.size()) + init + 1);
        const auto at = init <= 0 ? 0 : std::size_t(init - 1);
        const auto found = at <= value.size() ? value.find(pattern, at) : std::string::npos;
        if (found == std::string::npos) return {Lua4Value{}};
        return {Lua4Value::number(double(found + 1)), Lua4Value::number(double(found + pattern.size()))};
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
                std::string text = value.type() == Lua4Type::Number ? [&] {
                    char n[64];
                    std::snprintf(n, sizeof(n), "%.14g", value.as_number());
                    return std::string(n);
                }() : std::string(value.as_string());
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
        std::snprintf(buffer, sizeof buffer, "V<%g,%g,%g>", v.x, v.y, v.z);
        return std::string(buffer);
    };
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
        auto* model = context_model();
        Vec3f value{};
        if (model && model->node) {
            if (method == "MDLgetPosStr") {
                const auto& m = model->node->matrix;
                value = {m(3, 0), m(3, 1), m(3, 2)};
            } else {
                constexpr float to_degrees = 57.29577951308232f;
                value = {model->applied[0] * to_degrees, model->applied[1] * to_degrees, model->applied[2] * to_degrees};
            }
        }
        value.x += float(to_number(arg(0)));
        value.y += float(to_number(arg(1)));
        value.z += float(to_number(arg(2)));
        return string_result(vector_string(value));
    }
    if (method == "MDLgetHp" || method == "MDLaddHp") {
        auto* model = context_model();
        if (!model) return number(0);
        if (method == "MDLaddHp") {
            // DW4_GOD: automated test runs keep the players alive.
            static const bool god = std::getenv("DW4_GOD") != nullptr;
            if (god && model->is_player && to_int(arg(0)) < 0) return {};
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
        return number(model ? (model->record >= 0 ? model->record : model->index) : 0);
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
    if (method == "MDLisStatus") return number(0); // status effects are not reconstructed

    // --- Battle queries of hit scripts (FIGHTCALC P2E*, 802EE5C4..802EEA2C) ---
    auto shooter_stats = [&]() -> const GameSystem::Stats* {
        auto* sys = get<GameSystem>();
        if (!sys || !context || !context->shooter_name.starts_with('p') || context->shooter < 0 || context->shooter > 3) return nullptr;
        auto& slot = sys->players[std::size_t(context->shooter)];
        if (!slot.stats.ready) sys->prepare_player(*this, context->shooter);
        return &slot.stats;
    };
    const int bullet_kind = context ? context->bullet_kind : 0;
    if (method == "MDLgetBulletName") return string_result(fight::bullet_name(bullet_kind));
    if (method == "BLTisPlayerBullet") return number(context && context->shooter_name.starts_with('p') ? 1 : 0);
    if (method == "BLTisHitCheck") {
        const auto* stats = shooter_stats();
        auto* model = context_model();
        if (!stats || !model) return number(1);
        return number(fight::player_hits(*this, *stats, model->record, bullet_kind, 0) ? 1 : 0);
    }
    if (method == "BLTisCritical") {
        const auto* stats = shooter_stats();
        return number(stats && fight::player_critical(*stats, bullet_kind) ? 1 : 0);
    }
    if (method == "BLTgetDamageValue") {
        const auto* stats = shooter_stats();
        auto* model = context_model();
        if (!stats || !model || !context) return number(0);
        const auto result = fight::player_damage(*this, *stats, model->record, bullet_kind, to_int(arg(0)) != 0, 0);
        context->damage = result.damage;
        context->attribute = result.attribute;
        context->damage_flag = result.flag;
        return number(result.damage);
    }
    if (method == "BLTgetDamageRank") {
        auto* model = context_model();
        FightResult result;
        if (context) result = {context->damage, context->attribute, context->damage_flag};
        return number(fight::damage_rank(to_int(arg(0)), model ? model->max_hp : 0, result, to_int(arg(1)) != 0, bullet_kind));
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
    if (method == "BLTcalcSkillUp") return {}; // weapon skill growth not reconstructed
    if (method == "MDLdspDamage" || method == "MDLdspDamageP" || method == "MDLdspExp") {
        // 802EAE28..48: floating damage / player damage / EXP numbers at the model.
        const int value = to_int(arg(0));
        if (method != "MDLdspExp") log("damage " + std::to_string(value) + (context ? " -> " + context->name : std::string()));
        if (auto* model = context_model(); model && model->node) {
            const auto& m = model->node->matrix;
            show_number(*this, method == "MDLdspDamage" ? 0 : method == "MDLdspDamageP" ? 3 : 1, value, Vec3f{m(3, 0), m(3, 1), m(3, 2)});
        }
        return {};
    }
    if ((method.starts_with("BLTget") || method.starts_with("BLPget")) && method.ends_with("Time")) return number(0);
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
        return shooter ? (shooter->record >= 0 ? shooter->record : shooter->index) : -1;
    };
    if (method == "BLPisHitCheck") {
        const auto* stats = target_stats();
        return number(!stats || fight::enemy_hits(*this, *stats, shooter_record(), bullet_kind) ? 1 : 0);
    }
    if (method == "BLPcalcKoukaMagic") return number(0); // technique effects are not reconstructed
    if (method == "BLPgetDamageValue") {
        const auto* stats = target_stats();
        auto* model = context_model();
        if (!stats || !model || !context) return number(0);
        const auto result = fight::enemy_damage(*this, *stats, model->max_hp, shooter_record(), bullet_kind);
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

    // --- System queries ---
    if (method == "SYSgetArea") return number(area());
    if (method == "SYSgetCountry") return number(country());
    if (method == "SYSgetIsPackOnly") return number(1);
    if (method == "SYSgetRandom") {
        static std::mt19937 rng{0x4457344u};
        const auto limit = std::max(1, to_int(arg(0), 1));
        return number(double(rng() % std::uint32_t(limit)));
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
