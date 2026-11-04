// Data Overflow (H10, SURVIVAL.md 4.1 and 13.4): the native field start.
//
// A run engine never runs an arena's first.lua: it plays the boss intro and
// generates the boss (stagBS02: Belphemon, GENERATER START GENE-00). The
// native start is that first.lua line for line without the boss lines, in
// the same order and through the same be: calls (Engine::host_call):
//
//   stagBS02 first.lua                                   native start
//   DMsetSD SOUND BGMSTOP                                kept
//   sd NDMWPANEL NOGATEDISK I<1>                         kept
//   DMsetSD SOUND PLAY AM_BOS01                          boss ambience: left out
//   DMsetSD SOUND PLAYatPAUSE bvTRNS01 / BT_BOSS2        boss voice and BGM: left out
//   DMchkSD SOUND ISALLREADY != 1                        (waits for those two): left out
//   DMsetSD SOUND PAUSEOFF BT_BOSS2                      boss BGM: left out
//   DMsetSD CAMERA SETDEMOCAMERA entry                   boss intro camera: left out
//   DMsetSD LAYERCTL SWITCH WN00/CLR (10)                hides the windows for the intro: left out
//   DMchkSD CAMERA ISDEMOCAMERA == 0                     (waits for the intro camera): left out
//   DMsetSD MODELCTRL i_fadewn01 0 SQ_BLIN60             kept
//   DMsetSD GENERATER START GENE-00 (150)                the boss: left out
//   DMsetSD SOUND PAUSEOFF bvTRNS01 (150)                boss voice: left out
//   DMchkSD CAMERA ISDEMOCAMERA != 0                     (waits for the intro's end): left out
//   DMsetSD CAMERA SETBOXCAMERA                          kept
//   DMsetSD LAYERCTL SWITCH WN00/SET                     kept
//   DMsetSD PLAYER SQ_WAIT                               kept
//   DMsetSD STAGECTL PADINPUT ENABLE                     kept
//   DMsetSD ENEMY f_belvam 0 SQ_WAIT                     the boss: left out
//   DMrun                                                kept
//   g_delayTime = 520 (the intro's length)               OverflowRules::hud_delay
//   HUD panels i_stpnl00..05 per joined player           kept (delay hud_delay, +5 per slot)
//   i_bosshp02 GENERATE, bshp02 SETMODELHP f_belvam      the boss bar: left out (O1b: Guardians)
//   sd SWITCHCTRL ALLON                                  kept
//   funcFirstLayerSet(): LAYERCTL REMOVE LOADING,        kept (func.lua's two lines)
//                        LAYERCTL SWITCH ALL/DISPON
//
// A demo record's delay posts its message and goes on (DemoManager), so
// without the intro's waits WN00/CLR (posted 10 frames ahead) would come
// after WN00/SET and ALL/DISPON and hide the HUD for good: it belongs to the
// intro (T10 found it). Then the director adds the run's own lines
// (Director::field_started): the phase music, its spawns. T10 compares the
// stage state after both starts.
#include "dw4/engine/overflow.hpp"

#include "dw4/engine/engine.hpp"

#include <initializer_list>
#include <string>
#include <vector>

namespace dw4::engine::overflow {
namespace {

Lua4Value N(double value) { return Lua4Value::number(value); }
Lua4Value S(std::string value) { return Lua4Value::string(std::move(value)); }
const Lua4Value kNil{};

void be(Engine& engine, std::string_view method, std::initializer_list<Lua4Value> args) {
    const std::vector<Lua4Value> list(args);
    engine.host_call(method, list);
}

void violet_ring(Engine& engine, const OverflowRules& rules) {
    be(engine, "DMsetSD", {S("SOUND"), kNil, N(-1), S("BGMSTOP")});
    be(engine, "sd", {S("NDMWPANEL"), kNil, N(-1), S("NOGATEDISK"), S("I<1>")});
    be(engine, "DMsetSD", {S("MODELCTRL"), S("i_fadewn01"), N(0), S("SQ_BLIN60")});
    be(engine, "DMsetSD", {S("CAMERA"), kNil, N(-1), S("SETBOXCAMERA")});
    be(engine, "DMsetSD", {S("LAYERCTL"), kNil, N(-1), S("SWITCH"), S("WN00/SET")});
    be(engine, "DMsetSD", {S("PLAYER"), kNil, N(-1), S("SQ_WAIT")});
    be(engine, "DMsetSD", {S("STAGECTL"), kNil, N(-1), S("PADINPUT"), S("ENABLE")});
    be(engine, "DMrun", {});
    const double delay = rules.hud_delay;
    be(engine, "sd", {S("MODELCTRL"), S("i_stpnl00"), N(0), S("SQ_OPEN"), N(delay)});
    for (int slot = 0; slot < 4; ++slot) {
        const auto joined = engine.host_call("SYSisJoinPlayer", std::vector<Lua4Value>{N(slot)});
        if (joined.empty() || joined[0].type() != Lua4Type::Number || joined[0].as_number() != 1) continue;
        const double at = delay + 5.0 * slot;
        const auto name = engine.host_call("SYSgetPlayerName", std::vector<Lua4Value>{N(slot)});
        be(engine, "sd", {S("MODELCTRL"), S("i_stpnl01"), N(slot), S("SQ_OPEN"), N(at)});
        be(engine, "sd", {S("MODELCTRL"), S("i_stpnl02"), N(slot), name.empty() ? kNil : name[0], N(at)});
        be(engine, "sd", {S("MODELCTRL"), S("i_stpnl03"), N(slot), S("SQ_OPENWN"), N(at)});
        be(engine, "sd", {S("MODELCTRL"), S("i_stpnl04"), N(slot), S("SQ_OPENWN"), N(at)});
        be(engine, "sd", {S("MODELCTRL"), S("i_stpnl05"), N(slot), S("SQ_WAIT"), N(at)});
    }
    be(engine, "sd", {S("SWITCHCTRL"), kNil, N(-1), S("ALLON")});
    // funcFirstLayerSet (func.lua)
    be(engine, "sd", {S("LAYERCTL"), kNil, N(-1), S("REMOVE"), S("LOADING")});
    be(engine, "sd", {S("LAYERCTL"), kNil, N(-1), S("SWITCH"), S("ALL/DISPON")});
}

} // namespace

bool has_native_field_start(std::string_view stage) { return stage == "stagbs02"; }

bool native_field_start(Engine& engine, OverflowRules& rules) {
    if (engine.stage_name() != "stagbs02") return false;
    violet_ring(engine, rules);
    engine.log("overflow: native field start of " + engine.stage_name());
    if (rules.director) rules.director->field_started(engine);
    return true;
}

} // namespace dw4::engine::overflow
