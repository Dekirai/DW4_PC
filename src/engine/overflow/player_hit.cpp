// Data Overflow (H10, SURVIVAL.md 4.8 and 13.4): the player hit script
// answered natively in a run engine.
//
// Every stage has hit_p_<form>.lua for all 16 forms, and apart from the five
// quest and training stages (dngq3* / dngq4*, stag0001, stag0103 / 0203 /
// 0303) it is one script (the work/port-research/dec decompilation, the
// common text of 1,152 of the 1,344 copies; stagBS02, staggf01 / 04 and every
// dng* field have it). The resource overrides cannot replace it (members of
// each stage's package) and the port runs only the original chunks, so the
// run engine answers it here. Every call goes through Engine::host_call, the
// code path the script's be: calls take, so RuleSet::original does exactly
// what the script does (T10 compares the two over recorded fights); numbers
// become text as the Lua VM's concatenation makes them (Lua4Value::describe).
//
// RuleSet::run changes the reaction, not the rules of the hit (4.8):
// - a normal or Corrupted enemy's hit: the damage, its number, the HUD and
//   its shake; no damage sequence and no turn towards the attacker, so the
//   Digimon walks on; the element's hit spark (the damage action's own
//   starter, SDMG<F/I/T/D>1) and PC_DMG01; stun, sleep and bind last at most
//   status_cap frames, the downs keep their times; poison ticks through the
//   damage-taken rule, at most poison_ticks times and poison_share of MaxHP
//   in all (OverflowRules::poison), and never takes the last HP (status.cpp);
// - a Guardian's hit: the original reaction by damage rank and statuses;
// - after every damaging hit player_hit.muteki frames of MUTEKI;
// - a miss shows the guard effect (the guard action's starter, GURD?2) and
//   EFX_GUARD without SQ_GUARDR; a status attack that did nothing the same;
// - classic buttons: a hit on the guard passes at guard_percent;
// - 0 HP: the director may revive (Raise); otherwise the run ends
//   (RunEnd::player_defeated) and the original knock-down plays.
#include "dw4/engine/overflow.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace dw4::engine::overflow {
namespace {

Lua4Value N(double value) { return Lua4Value::number(value); }
Lua4Value S(std::string value) { return Lua4Value::string(std::move(value)); }
// A value as the Lua VM's concatenation writes it.
std::string text(const Lua4Value& value) {
    if (value.type() == Lua4Type::String) return std::string(value.as_string());
    if (value.type() == Lua4Type::Number) return value.describe();
    return {};
}
std::string text(double value) { return Lua4Value::number(value).describe(); }
double number_of(const Lua4Value& value) { return value.type() == Lua4Type::Number ? value.as_number() : 0.0; }

// The script's be: calls.
class Calls {
public:
    explicit Calls(Engine& engine) : engine_(engine) {}
    Lua4Value call(std::string_view method, std::initializer_list<Lua4Value> args = {}) {
        const std::vector<Lua4Value> list(args);
        auto out = engine_.host_call(method, list);
        return out.empty() ? Lua4Value{} : out.front();
    }
    double num(std::string_view method, std::initializer_list<Lua4Value> args = {}) { return number_of(call(method, args)); }
    // be:sd(category, name, index, command, arguments [, delay])
    void sd(const char* category, const char* name, const Lua4Value& index, const char* command, std::string arguments) {
        call("sd", {S(category), S(name), index, S(command), S(std::move(arguments))});
    }
    void sd(const char* category, const char* name, const Lua4Value& index, const char* command, std::string arguments, double delay) {
        call("sd", {S(category), S(name), index, S(command), S(std::move(arguments)), N(delay)});
    }

private:
    Engine& engine_;
};

// L0 / L1 of the script: special enemy bullets meant for one player only.
bool foreign_bullet(double kind, double ram) {
    if ((kind == 70126 || kind == 70127) && ram != 0) return true;
    if ((kind == 70128 || kind == 70129) && ram != 1) return true;
    if ((kind == 70130 || kind == 70131) && ram != 2) return true;
    if ((kind == 70132 || kind == 70133) && ram != 3) return true;
    return false;
}

// L234..L321: the reaction by damage rank.
void reaction(Calls& be, double rank) {
    static const char* const sequences[4] = {"SQ_SDMG", "SQ_MDMG", "SQ_LDMG", "SQ_DDMG"};
    if (rank < 0 || rank > 3 || rank != std::floor(rank)) return;
    const char* sequence = sequences[int(rank)];
    be.call("MDLsetSD", {S("SETLABEL"), S(std::string(sequence) + "/" + text(be.call("BLTgetAttributeStr")))});
    be.call("MDLsetSeq", {S(sequence)});
}

// The run's poison rule for one hit (RuleSet::run): the rules, the hit's
// context and the hit report that learns the poison.
struct RunPoisonRule {
    const OverflowRules& rules;
    const ModelScriptContext& context;
    HitTaken& hit;
};

// L321..L391: the statuses of a damaging hit (none at rank 3). `cap` > 0
// limits stun and bind (the run's rule for normal enemies); `run` puts the
// poison through the run's poison rule (OverflowRules::poison: its ticks
// through the damage-taken rule, their count and total limited).
void damage_statuses(Calls& be, const Lua4Value& damage, double rank, int cap, const RunPoisonRule* run = nullptr) {
    if (rank == 3) return;
    auto capped = [&](double value) { return cap > 0 ? std::min(value, double(cap)) : value; };
    if (const double stun = be.num("BLPgetStunTime", {damage, N(rank)}); stun > 0) be.call("MDLsetSD", {S("SETSTUN"), S("I<" + text(capped(stun)) + ">")});
    if (const double bind = be.num("BLPgetBindTime"); bind > 0) be.call("MDLsetSD", {S("SETBIND"), S("I<" + text(capped(bind)) + ">")});
    if (const double poison = be.num("BLPgetPoisonTime", {S("TIME")}); poison > 0) {
        auto amount = be.call("BLPgetPoisonTime", {S("DAMAGE")});
        const auto gedoku = be.call("BLPgetPoisonTime", {S("GEDOKU")});
        std::string time = text(poison);
        if (run) {
            // (SETPOISON ticks time / 120 + 1 times.)
            const auto out = run->rules.poison(run->context, run->hit.max_hp, int(number_of(amount)), int(poison) / 120 + 1);
            amount = N(out.tick);
            time = text(double((out.ticks - 1) * 120));
            run->hit.poison_tick = out.tick;
            run->hit.poison_ticks = out.ticks;
        }
        be.call("MDLsetSD", {S("SETPOISON"), S("I<" + time + ">/I<" + text(amount) + ">/I<" + text(gedoku) + ">")});
    }
}

// L83..L232 with no damage: the status attacks. Answers the script's L3
// (0: nothing took, the guard reaction follows). `cap` > 0: the run's limit
// for stun, sleep and bind.
double status_attack(Calls& be, double kind, int cap) {
    auto capped = [&](double value) { return cap > 0 ? std::min(value, double(cap)) : value; };
    double l3 = 0;
    if (kind == 70002 || kind == 70046) {
        l3 = be.num("BLPgetStunTime", {N(0), N(0)});
        if (l3 > 0) {
            be.call("MDLsetSeq", {S("SQ_STUN")});
            be.call("MDLsetSD", {S("SETSTUN"), S("I<" + text(capped(l3)) + ">")});
        }
    }
    if (l3 == 0) {
        l3 = be.num("BLPgetSleepTime");
        if (l3 > 0) be.call("MDLsetSD", {S("SETSLEEP"), S("I<" + text(capped(l3)) + ">")});
    }
    if (l3 == 0) {
        l3 = be.num("BLPgetBindTime");
        if (l3 > 0) be.call("MDLsetSD", {S("SETBIND"), S("I<" + text(capped(l3)) + ">")});
    }
    if (l3 == 0) {
        l3 = be.num("BLPgetAttackDownTime", {S("TIME")});
        if (l3 > 0) {
            const auto speed = be.call("BLPgetAttackDownTime", {S("SUBAYASA")});
            const auto accuracy = be.call("BLPgetAttackDownTime", {S("MEICHURITSU")});
            be.call("MDLsetSD", {S("SETATTACKDOWN"), S("I<" + text(l3) + ">/I<" + text(speed) + ">/I<" + text(accuracy) + ">/I<0>/I<0>")});
        }
    }
    if (l3 == 0) {
        l3 = be.num("BLPgetPowerDownTime", {S("TIME")});
        if (l3 > 0) {
            const auto strength = be.call("BLPgetPowerDownTime", {S("TSUYOSA")});
            const auto defense = be.call("BLPgetPowerDownTime", {S("MAMORI")});
            be.call("MDLsetSD", {S("SETPOWERDOWN"), S("I<" + text(l3) + ">/I<" + text(strength) + ">/I<" + text(defense) + ">")});
        }
    }
    return l3;
}

// L391..L533: the HP meter and number, the panel's shake.
void hud(Calls& be) {
    const auto ram = be.call("MDLgetRamNo");
    be.sd("METERCTRL", "hpmt", ram, "SETPERCENT", "F<" + text(be.num("MDLgetHp") * 100 / be.num("MDLgetMHp")) + ">");
    be.sd("NUMBERCTRL", "hpnm", be.call("MDLgetRamNo"), "SETVALUE", "I<" + text(be.call("MDLgetHp")) + ">");
    static constexpr double kPanelX[4] = {-260, -110, 40, 190};
    auto panel_x = [&](double add) {
        const double index = be.num("MDLgetRamNo");
        const double base = index >= 0 && index < 4 && index == std::floor(index) ? kPanelX[int(index)] : 0;
        return text(base + add);
    };
    be.sd("MODELCTRL", "i_stpnl02", be.call("MDLgetRamNo"), "MOVETRANS", "V<" + panel_x(-2) + ",-158,0>/V<0,0,0>");
    be.sd("MODELCTRL", "i_stpnl02", be.call("MDLgetRamNo"), "MOVETRANS", "V<" + panel_x(-1) + ",-162,0>/V<0,0,0>", 4);
    be.sd("MODELCTRL", "i_stpnl02", be.call("MDLgetRamNo"), "MOVETRANS", "V<" + panel_x(1) + ",-159,0>/V<0,0,0>", 8);
    be.sd("MODELCTRL", "i_stpnl02", be.call("MDLgetRamNo"), "MOVETRANS", "V<" + panel_x(2) + ",-161,0>/V<0,0,0>", 12);
    be.sd("MODELCTRL", "i_stpnl02", be.call("MDLgetRamNo"), "MOVETRANS", "V<" + panel_x(0) + ",-160,0>/V<0,0,0>", 16);
}

// The script as it is (RuleSet::original).
void original_hit(Calls& be, double kind) {
    if (be.num("MDLisSeqName", {S("SQ_GUARD")}) == 1 || be.num("BLPisHitCheck") == 0) {
        be.call("MDLsetSeq", {S("SQ_GUARDR")});
        return;
    }
    const auto damage = be.call("BLPgetDamageValue");
    if (number_of(damage) == 0) {
        if (status_attack(be, kind, 0) == 0) be.call("MDLsetSeq", {S("SQ_GUARDR")});
        else be.call("MDLsetDMGTurn", {N(0)});
        return;
    }
    be.call("MDLaddHp", {N(-number_of(damage))});
    be.call("MDLdspDamageP", {damage});
    const auto hp = be.call("MDLgetHp");
    const double rank = be.num("BLPgetDamageRank", {hp, damage});
    reaction(be, rank);
    damage_statuses(be, damage, rank, 0);
    hud(be);
}

// A port-only effect of the player's own action starters (the action's
// starter without the action) and a sound.
void broadcast(Engine& engine, const std::string& model, int slot, const std::string& action) {
    const Messenger::PortOnly scope(engine.messenger());
    Packet packet;
    packet.strings.emplace_back(model);
    packet.ints.push_back(slot);
    engine.messenger().send(engine, "ACTION_BROADCAST", std::nullopt, -1, action, packet);
}
void play_sound(Engine& engine, const char* name) {
    const Messenger::PortOnly scope(engine.messenger());
    Packet packet;
    packet.strings.emplace_back(name);
    engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", packet);
}

// The guard effect of the weapon's guard reaction (GURDD2 / GURDS2 / GURDA2 /
// GURDG2 by the label of SQ_GUARDR: dual, slash, arm, gun) and EFX_GUARD.
void guard_effect(Engine& engine, const ModelCtrlManager::ModelRam& self) {
    std::string label = self.label;
    if (const auto found = self.labels.find("SQ_GUARDR"); found != self.labels.end()) label = found->second;
    const char letter = label == "slash" ? 'S' : label == "arm" ? 'A' : label == "gun" ? 'G' : 'D';
    broadcast(engine, self.info_name, self.index, std::string("GURD") + letter + "2");
    play_sound(engine, "EFX_GUARD");
}

// The run's rule (RuleSet::run).
void run_hit(Engine& engine, OverflowRules& rules, Calls& be, double kind) {
    auto* models = engine.get<ModelCtrlManager>();
    auto* context = engine.model_context();
    auto* self = models && context ? models->find(context->name, context->index) : nullptr;
    if (!context || !self) {
        original_hit(be, kind);
        return;
    }
    HitTaken hit;
    hit.slot = self->index;
    hit.shooter = context->shooter_name;
    hit.shooter_index = context->shooter;
    hit.kind = int(kind);
    hit.enemy = rules.enemy_class(context->shooter_name, context->shooter);
    const bool guardian = hit.enemy == EnemyClass::guardian;
    auto report = [&] {
        if (rules.director) rules.director->player_hit(engine, hit);
    };
    const bool guarding = be.num("MDLisSeqName", {S("SQ_GUARD")}) == 1;
    if (!guarding && be.num("BLPisHitCheck") == 0) {
        // A miss (BOUGYORITSU, evasion): the guard effect, no reaction.
        if (guardian) be.call("MDLsetSeq", {S("SQ_GUARDR")});
        else {
            be.call("MDLsetDMGTurn", {N(0)});
            guard_effect(engine, *self);
        }
        hit.missed = true;
        ++rules.tally.misses;
        report();
        return;
    }
    rules.last_taken = {};
    auto damage = be.call("BLPgetDamageValue");
    hit.raw = rules.last_taken.raw;
    hit.clamped = rules.last_taken.clamped;
    if (guarding && number_of(damage) > 0) {
        // Classic buttons: the guard lets the hit through at a share, no reaction.
        hit.guarded = true;
        damage = N(std::max(1.0, std::floor(number_of(damage) * rules.player_hit.guard_percent / 100.0)));
    }
    if (!guardian) be.call("MDLsetDMGTurn", {N(0)}); // the Digimon keeps its heading
    if (number_of(damage) == 0) {
        if (status_attack(be, kind, guardian ? 0 : rules.player_hit.status_cap) == 0) {
            if (guardian) be.call("MDLsetSeq", {S("SQ_GUARDR")});
            else guard_effect(engine, *self);
        } else if (guardian) {
            be.call("MDLsetDMGTurn", {N(0)});
        }
        report();
        return;
    }
    be.call("MDLaddHp", {N(-number_of(damage))});
    be.call("MDLdspDamageP", {damage});
    const auto hp = be.call("MDLgetHp");
    hit.damage = int(number_of(damage));
    hit.hp_after = int(number_of(hp));
    hit.max_hp = int(be.num("MDLgetMHp"));
    ++rules.tally.hits_taken;
    if (number_of(hp) <= 0) {
        // 4.8: Raise, or the end of the run (the defeat check comes before
        // any RECALCPLAYER, which would refill a player at 0 HP).
        if (rules.director && rules.director->revive(engine, self->index)) {
            hud(be);
            report();
            return;
        }
        rules.end_run(engine, {EndReason::player_defeated, {}});
        reaction(be, 3);
        hud(be);
        report();
        return;
    }
    const double rank = be.num("BLPgetDamageRank", {hp, damage});
    if (guardian && !hit.guarded) {
        reaction(be, rank);
        damage_statuses(be, damage, rank, 0);
    } else {
        if (!hit.guarded) {
            // The flash: the element's hit spark of the damage action, and PC_DMG01.
            const std::string attribute = text(be.call("BLTgetAttributeStr"));
            const char letter = attribute == "fire" ? 'F' : attribute == "ice" ? 'I' : attribute == "thunder" ? 'T' : attribute == "dark" ? 'D' : 0;
            if (letter) broadcast(engine, self->info_name, self->index, std::string("SDMG") + letter + "1");
            play_sound(engine, "PC_DMG01");
        }
        const RunPoisonRule poison{rules, *context, hit};
        damage_statuses(be, damage, std::min(rank, 2.0), rules.player_hit.status_cap, &poison);
    }
    if (rules.player_hit.muteki > 0) models->set_muteki(engine, *self, float(rules.player_hit.muteki));
    hud(be);
    report();
}

} // namespace

void native_player_hit(Engine& engine, OverflowRules& rules) {
    Calls be(engine);
    const double kind = be.num("MDLgetBulletKind");
    const double ram = be.num("MDLgetRamNo");
    if (foreign_bullet(kind, ram)) return;
    // 80000..89999: support techniques (heal, force) reach the player.
    if (!(80000 > kind) && !(kind >= 90000)) {
        be.call("MDLsetDMGTurn", {N(0)});
        // The run's heal cap (6.4): a heal restores at most heal_cap of MaxHP.
        auto* sys = rules.run() ? engine.get<GameSystem>() : nullptr;
        const int slot = std::clamp(int(ram), 0, 3);
        const int hp_before = sys ? sys->players[std::size_t(slot)].hp : 0;
        const auto effect = be.call("BLPcalcKoukaMagic");
        if (sys) {
            const auto& player = sys->players[std::size_t(slot)];
            const int cap = std::max(1, int(double(player.max_hp) * double(rules.player_hit.heal_cap)));
            if (player.hp - hp_before > cap) sys->change_hp(engine, slot, hp_before + cap - player.hp);
        }
        be.call("BLTcalcSkillUp", {effect, N(0)});
        return;
    }
    if (rules.run()) run_hit(engine, rules, be, kind);
    else original_hit(be, kind);
}

} // namespace dw4::engine::overflow
