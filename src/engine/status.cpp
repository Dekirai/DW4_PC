// Status effects of players and enemies: the beNDMWMdlPEBase work shared by
// beNDMWMdlPlayer and beNDMWMdlEnemy (803ACB30..803B02E0). Status commands
// (804E28E0) fill per-slot timers; the per-model update after control
// (vtable +C0, 803ACD88) runs them with named model timers, and the
// sequence-change hook (vtable +B8, 803AE1A4) breaks sleep and stun.
#include "dw4/engine/managers.hpp"
#include "dw4/engine/overflow.hpp"

#include <algorithm>
#include <cstdlib>

namespace dw4::engine {
namespace {
using ModelRam = ModelCtrlManager::ModelRam;

// 804EDD28: the icon model of each status slot.
constexpr const char* kStatusIcons[12] = {"i_stdownic01", "i_stdownic02", "i_stdownic03", "i_stdownic04",
                                          "i_stdownic05", "i_stdownic09", "i_stdownic10", "i_stdownic06",
                                          "i_stdownic07", "i_stdownic08", "i_rareicon",   "i_stdownic00"};
// 804EDD58 / 804EDD70: icon sequences per position.
constexpr const char* kIconOpen[6] = {"SQ_OPEN01", "SQ_OPEN02", "SQ_OPEN03", "SQ_OPEN04", "SQ_OPEN05", "SQ_OPEN06"};
constexpr const char* kIconLoop[6] = {"SQ_LOOP01", "SQ_LOOP02", "SQ_LOOP03", "SQ_LOOP04", "SQ_LOOP05", "SQ_LOOP06"};
// 804E2888 StatusName: CHECKSTATUS names of slots 0..10.
constexpr const char* kStatusNames[11] = {"POISON", "ATTACKDOWN", "POWERDOWN", "BIND", "REFRACT", "SPEEDUP",
                                          "POWERUP", "METAL", "SLEEP", "STUN", "RARE"};
// 804E2838 DamageSeq: a bound model still moves in these.
constexpr const char* kDamageSequences[10] = {"SQ_SDMG", "SQ_MDMG", "SQ_LDMG", "SQ_DDMG", "SQ_DEAD",
                                              "SQ_SDMGC", "SQ_SDMGF", "SQ_SDMGI", "SQ_SDMGT", "SQ_SDMGD"};
// 804E28E0: the status commands in handler order.
constexpr const char* kStatusCommands[16] = {"SETPOISON", "SETSLEEP", "SETSTUN", "SETBIND", "SETATTACKDOWN",
                                             "SETPOWERDOWN", "SETRAREICON", "SETSPEEDUP", "SETPOWERUP", "SETREFRACT",
                                             "SETMETAL", "CLEARMETAL", "CHECKSTATUS", "CLEARSTATUS", "ALLCLEARSTATUS",
                                             "GETPEBASEWORK"};
constexpr std::int64_t kPoisonTick = 2000000000; // a poison tick every 2 s

std::int64_t frames_ns(int frames) { return std::int64_t{1000000000} * frames / 60; }

// Named model timers on the world clock (8031DE44 start, 8031E238 run out:
// also when missing, otherwise end < the 2^18 ns quantised clock since the
// start; 8031E8BC remove). They run on even while the update does not
// (frozen, no control).
void start_timer(ModelRam& ram, std::int64_t world, std::string_view name, std::int64_t ns) {
    timer_start(ram.named_timers[std::string(name)], world, ns);
}
bool timer_done(const ModelRam& ram, std::int64_t world, std::string_view name) {
    const auto found = ram.named_timers.find(name);
    return found == ram.named_timers.end() || timer_expired(found->second, world);
}
void remove_timer(ModelRam& ram, std::string_view name) {
    if (const auto found = ram.named_timers.find(name); found != ram.named_timers.end()) ram.named_timers.erase(found);
}

bool damage_sequence(std::string_view name) {
    return std::find(std::begin(kDamageSequences), std::end(kDamageSequences), name) != std::end(kDamageSequences);
}

Vec3f model_position(const ModelRam& ram) {
    if (!ram.node) return {};
    const auto& m = ram.node->matrix;
    return {m(3, 0), m(3, 1), m(3, 2)};
}

void erase_icon(Engine& engine, int slot, int icon) {
    Packet erase;
    engine.messenger().send(engine, "MODELCTRL", kStatusIcons[slot], icon, "SQ_ERAS", erase);
}
} // namespace

int ModelCtrlManager::down_value(Engine& engine, const ModelRam& ram, int index) const {
    if (index < 0) return 0;
    if (ram.is_player) {
        auto* sys = engine.get<GameSystem>();
        return sys && index < 11 ? sys->players[std::size_t(std::clamp(ram.index, 0, 3))].downdata[std::size_t(index)] : 0;
    }
    return ram.status && index < 6 ? ram.status->down[std::size_t(index)] : 0;
}

void ModelCtrlManager::clear_poison(ModelRam& ram) {
    remove_timer(ram, "POISON");
    if (ram.status) ram.status->timer[0] = 0;
}

// A player's [66] / [67] are its GAMERAM DOWNDATA / UPDATA lists (803AE5F8 /
// 803AE87C); an enemy's own lists hold 6 and 7 values.
void ModelCtrlManager::set_down(Engine& engine, ModelRam& ram, int index, int value) {
    if (ram.is_player) {
        if (auto* sys = engine.get<GameSystem>(); sys && index >= 0 && index < 11)
            sys->players[std::size_t(std::clamp(ram.index, 0, 3))].downdata[std::size_t(index)] = value;
    } else if (ram.status && index >= 0 && index < 6) {
        ram.status->down[std::size_t(index)] = value;
    }
}

void ModelCtrlManager::set_up(Engine& engine, ModelRam& ram, int index, int value) {
    if (ram.is_player) {
        if (auto* sys = engine.get<GameSystem>(); sys && index >= 0 && index < 11)
            sys->players[std::size_t(std::clamp(ram.index, 0, 3))].updata[std::size_t(index)] = value;
    } else if (ram.status && index >= 0 && index < 7) {
        ram.status->up[std::size_t(index)] = value;
    }
}

// 803AEB00: a player recalculates (GAMERAM RECALCPLAYER) and refreshes its
// HP display (ADDHP 0).
void ModelCtrlManager::apply_status_values(Engine& engine, ModelRam& ram) {
    if (!ram.is_player) return;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    const int slot = std::clamp(ram.index, 0, 3);
    sys->recalc_player(engine, slot);
    sys->change_hp(engine, slot, 0);
}

// 803AEBB4: the status commands. All but CHECKSTATUS and CLEARSTATUS make
// the work first (12 slots, icons and positions -1).
bool ModelCtrlManager::status_command(Engine& engine, ModelRam& ram, std::string_view command, Packet& packet) {
    const auto* found = std::find(std::begin(kStatusCommands), std::end(kStatusCommands), command);
    if (found == std::end(kStatusCommands)) return false;
    const int index = int(found - std::begin(kStatusCommands));
    if (index != 12 && index != 13 && !ram.status) {
        ModelRam::Status status;
        status.icon.fill(-1);
        status.place.fill(-1);
        ram.status = status;
    }
    auto arg = [&](int i) { return packet.integer(i, 0); };
    auto set = [&](int slot, int time) {
        ram.status->start[std::size_t(slot)] = true;
        ram.status->timer[std::size_t(slot)] = time;
    };
    auto clear = [&](int slot, const char* timer) {
        remove_timer(ram, timer);
        ram.status->start[std::size_t(slot)] = false;
        ram.status->timer[std::size_t(slot)] = 0;
    };
    switch (index) {
    case 0: // SETPOISON I<time>/I<damage>[/I<gedoku>]: 2 s ticks
        set(0, arg(0) / 120 + 1);
        ram.status->poison_damage = arg(1);
        if (packet.ints.size() == 3) ram.status->gedoku = arg(2);
        break;
    case 1: set(8, arg(0)); break; // SETSLEEP
    case 2: set(9, arg(0)); break; // SETSTUN
    case 3: set(3, arg(0)); break; // SETBIND
    case 4:                        // SETATTACKDOWN time / SUBAYASA / hit / MAHIRITSU / KAIHIRITSU
        set(1, arg(0));
        ram.status->down_list = true;
        for (int i = 0; i < 4; ++i) set_down(engine, ram, i, arg(i + 1));
        apply_status_values(engine, ram);
        break;
    case 5: // SETPOWERDOWN time / TSUYOSA / MAMORI
        set(2, arg(0));
        ram.status->down_list = true;
        set_down(engine, ram, 4, arg(1));
        set_down(engine, ram, 5, arg(2));
        apply_status_values(engine, ram);
        break;
    case 6: set(10, arg(0)); break; // SETRAREICON
    case 7:                         // SETSPEEDUP
        set(5, arg(0));
        ram.status->up_list = true;
        for (int i = 0; i < 4; ++i) set_up(engine, ram, i, arg(i + 1));
        apply_status_values(engine, ram);
        break;
    case 8: // SETPOWERUP
        set(6, arg(0));
        ram.status->up_list = true;
        for (int i = 0; i < 3; ++i) set_up(engine, ram, 4 + i, arg(i + 1));
        apply_status_values(engine, ram);
        break;
    case 9: // SETREFRACT
        set(4, arg(0));
        ram.status->up_list = true;
        for (int i = 0; i < 4; ++i) set_up(engine, ram, 7 + i, arg(i + 1));
        apply_status_values(engine, ram);
        break;
    case 10: // SETMETAL: runs until CLEARMETAL
        set(7, 1);
        ram.status->down_list = true;
        break;
    case 11: // CLEARMETAL
        ram.status->start[7] = false;
        ram.status->timer[7] = 0;
        if (ram.status->down_list) set_down(engine, ram, 6, 0);
        break;
    case 12: { // CHECKSTATUS <name>: whether that slot runs
        bool on = false;
        if (ram.status && !packet.strings.empty()) {
            const auto name = packet.string(0);
            for (int slot = 0; slot < 11; ++slot)
                if (name == kStatusNames[slot]) on = ram.status->timer[std::size_t(slot)] != 0;
        }
        packet.bools.push_back(on);
        break;
    }
    case 14: // ALLCLEARSTATUS: every timed effect, all UP / DOWN values, then CLEARSTATUS
        if (ram.status) {
            clear(0, "POISON");
            clear(1, "ATKDOWN");
            clear(2, "POWDOWN");
            clear(5, "SPEEDUP");
            clear(6, "POWERUP");
            clear(4, "REFRACT");
            for (int i = 0; i < 11; ++i) {
                if (ram.status->up_list) set_up(engine, ram, i, 0);
                if (ram.status->down_list) set_down(engine, ram, i, 0);
            }
            if (ram.status->up_list || ram.status->down_list) apply_status_values(engine, ram);
        }
        [[fallthrough]];
    case 13: // CLEARSTATUS: sleep, stun and bind end, the model waits
        if (ram.status) {
            clear(8, "SLEEP");
            clear(9, "STUN");
            clear(3, "BIND");
            Packet wait;
            this->command(engine, ram, "SQ_WAIT", wait);
        }
        break;
    default: break; // GETPEBASEWORK: the lists are read directly (down_value)
    }
    return true;
}

// 803ACB30: shows the icon of a slot above the model. A new icon takes a
// free RAM and opens at its position; a shown one follows the model and
// replays its loop when its position changed.
int ModelCtrlManager::status_icon(Engine& engine, ModelRam& ram, int slot, int icon, int place, bool moved) {
    auto& messenger = engine.messenger();
    const char* name = kStatusIcons[slot];
    const Vec3f position = model_position(ram);
    place = std::clamp(place, 0, 5);
    if (icon == -1) {
        Packet space;
        messenger.send(engine, "MODELCTRL", name, -1, "FIND_SPACE", space);
        if (space.ints.empty()) return -1;
        const int free_ram = space.ints[0];
        Packet generate;
        generate.strings.emplace_back(std::string(kIconOpen[place]));
        generate.vectors.push_back(position);
        messenger.send(engine, "MODELCTRL", name, free_ram, "GENERATE", generate);
        static const bool trace = std::getenv("DW4_TRACE_STATUS") != nullptr;
        if (trace)
            engine.log("status icon " + std::string(name) + "#" + std::to_string(free_ram) + " " + kIconOpen[place] + " for " +
                       ram.info_name + "#" + std::to_string(ram.index));
        return free_ram;
    }
    if (moved) {
        Packet loop;
        messenger.send(engine, "MODELCTRL", name, icon, kIconLoop[place], loop);
    }
    Packet move;
    move.vectors.push_back(position);
    move.vectors.push_back(Vec3f{});
    messenger.send(engine, "MODELCTRL", name, icon, "MOVETRANS", move);
    return icon;
}

// 803ACD88: the per-frame status update. Effects run only while control is
// on (ONCTRL); the icons follow in either case. At 0 HP every icon goes and
// all timers stop.
void ModelCtrlManager::update_status(Engine& engine, ModelRam& ram) {
    const std::int64_t now = engine.time();
    if (!ram.status) return;
    auto& st = *ram.status;
    auto play = [&](const char* sequence) {
        Packet p;
        command(engine, ram, sequence, p);
    };
    if (ram.hp == 0) {
        for (int slot = 0; slot < 12; ++slot)
            if (st.icon[std::size_t(slot)] != -1) erase_icon(engine, slot, st.icon[std::size_t(slot)]);
        st.timer.fill(0);
        st.icon.fill(-1);
        return;
    }
    const bool on = ram.control_enabled;
    const int slot_index = std::clamp(ram.index, 0, 3);
    auto* sys = engine.get<GameSystem>();

    // Poison: each 2 s tick takes its damage. A player at 0 HP dies; an enemy
    // keeps 1 HP.
    if (on && st.timer[0]) {
        if (st.start[0]) {
            start_timer(ram, now, "POISON", kPoisonTick);
            --st.timer[0];
            st.start[0] = false;
        }
        if (timer_done(ram, now, "POISON")) {
            if (!(engine.cheats().god && ram.is_player)) {
                int lost = st.poison_damage;
                // Data Overflow (SURVIVAL.md 4.8, a run engine only): poison
                // never takes the player's last HP; the run's defeat is its
                // player hit rule's (OverflowRules::PlayerHit).
                if (ram.is_player && engine.overflow_run() && engine.overflow()->player_hit.poison_keeps_one)
                    lost = std::min(lost, std::max(ram.hp - 1, 0));
                ram.hp = std::max(ram.hp - lost, 0);
            }
            if (ram.is_player) {
                if (sys) sys->players[std::size_t(slot_index)].hp = ram.hp;
                Packet percent, value;
                percent.floats.push_back(100.0f * float(ram.hp) / float(std::max(ram.max_hp, 1)));
                value.ints.push_back(ram.hp);
                engine.messenger().send(engine, "METERCTRL", "hpmt", ram.index, "SETPERCENT", percent);
                engine.messenger().send(engine, "NUMBERCTRL", "hpnm", ram.index, "SETVALUE", value);
            }
            if (ram.hp > 0) {
                if (--st.timer[0] > 0) {
                    start_timer(ram, now, "POISON", kPoisonTick);
                } else {
                    remove_timer(ram, "POISON");
                    st.timer[0] = 0;
                }
            } else if (ram.is_player) {
                play("SQ_DEAD");
                remove_timer(ram, "POISON");
                st.timer[0] = 0;
            } else {
                ram.hp = 1;
                remove_timer(ram, "POISON");
                st.timer[0] = 0;
            }
        }
    }
    // Sleep: SQ_SLEP for the time, then SQ_WAIT.
    if (on && st.timer[8]) {
        if (st.start[8]) {
            start_timer(ram, now, "SLEEP", frames_ns(st.timer[8]));
            if (ram.seq_name != "SQ_SLEP") play("SQ_SLEP");
            st.start[8] = false;
        }
        if (timer_done(ram, now, "SLEEP")) {
            play("SQ_WAIT");
            remove_timer(ram, "SLEEP");
            st.timer[8] = 0;
        }
    }
    // Stun counts only while SQ_STUN plays.
    if (on && st.timer[9] && ram.seq_name == "SQ_STUN") {
        if (st.start[9]) {
            start_timer(ram, now, "STUN", frames_ns(st.timer[9]));
            st.start[9] = false;
        }
        if (timer_done(ram, now, "STUN")) {
            st.timer[9] = 0;
            remove_timer(ram, "STUN");
            play("SQ_WAIT");
        }
    }
    // Bind: outside a damage reaction every move channel but Y stops.
    if (on && st.timer[3]) {
        if (st.start[3]) {
            start_timer(ram, now, "BIND", frames_ns(st.timer[3]));
            st.start[3] = false;
        }
        if (timer_done(ram, now, "BIND")) {
            remove_timer(ram, "BIND");
            st.timer[3] = 0;
            play("SQ_WAIT");
        } else if (!damage_sequence(ram.seq_name)) {
            try {
                if (ram.channels.allocated()) {
                    auto& entries = ram.channels.list_owner()->mutable_entries();
                    for (std::size_t i = 0; i < entries.size(); ++i)
                        if (i != 1 && entries[i]) {
                            entries[i]->active = false;
                            entries[i]->speed = 0.0f;
                        }
                }
            } catch (const std::exception&) {
            }
        }
    }
    // Timed values: at the end the values return to 0 and apply.
    auto timed = [&](int slot, const char* timer, auto reset) {
        if (!on || !st.timer[std::size_t(slot)]) return;
        if (st.start[std::size_t(slot)]) {
            start_timer(ram, now, timer, frames_ns(st.timer[std::size_t(slot)]));
            st.start[std::size_t(slot)] = false;
        }
        if (!timer_done(ram, now, timer)) return;
        remove_timer(ram, timer);
        st.timer[std::size_t(slot)] = 0;
        reset();
        apply_status_values(engine, ram);
    };
    timed(1, "ATKDOWN", [&] {
        for (int i = 0; i < 4; ++i) set_down(engine, ram, i, 0);
    });
    timed(2, "POWDOWN", [&] {
        set_down(engine, ram, 4, 0);
        set_down(engine, ram, 5, 0);
    });
    timed(5, "SPEEDUP", [&] {
        for (int i = 0; i < 4; ++i) set_up(engine, ram, i, 0);
    });
    timed(6, "POWERUP", [&] {
        for (int i = 4; i < 7; ++i) set_up(engine, ram, i, 0);
    });
    timed(4, "REFRACT", [&] {
        for (int i = 7; i < 11; ++i) set_up(engine, ram, i, 0);
    });
    // Metal: DOWNDATA DELAY grows by 4 every ATTACKDELAY * 2 seconds.
    if (on && st.timer[7]) {
        if (st.start[7]) {
            set_down(engine, ram, 6, 0);
            start_timer(ram, now, "METAL", 0);
            st.start[7] = false;
        }
        if (timer_done(ram, now, "METAL")) {
            set_down(engine, ram, 6, down_value(engine, ram, 6) + 4);
            const int delay = sys ? sys->players[std::size_t(slot_index)].stats.attack_delay : 0;
            start_timer(ram, now, "METAL", std::int64_t{120000000000} * delay / 60);
            apply_status_values(engine, ram);
        }
    }
    // Icons: slots 0..7 line up in order, the frame (11) shows behind them,
    // sleep / stun / rare icon keep their own place.
    bool any = false;
    int place = 0;
    for (int slot = 0; slot < 12; ++slot) {
        const auto s = std::size_t(slot);
        if (slot == 11) st.timer[11] = any ? 1 : 0;
        if (st.timer[s]) {
            if (slot >= 8) {
                st.icon[s] = status_icon(engine, ram, slot, st.icon[s], slot == 10 ? st.timer[10] - 1 : 0, false);
            } else {
                bool moved = false;
                if (st.place[s] != place) {
                    st.place[s] = place;
                    moved = true;
                }
                st.icon[s] = status_icon(engine, ram, slot, st.icon[s], place++, moved);
                any = true;
            }
        } else if (st.icon[s] != -1) {
            erase_icon(engine, slot, st.icon[s]);
            st.icon[s] = -1;
        }
    }
}

// 803AE1A4 (sequence-change hook): leaving SQ_SLEP ends sleep; a stunned
// model back in SQ_WAIT stuns again, any other sequence ends the stun.
void ModelCtrlManager::status_sequence_check(Engine& engine, ModelRam& ram) {
    if (!ram.status) return;
    auto& st = *ram.status;
    if (ram.hp == 0) {
        for (int slot = 0; slot < 12; ++slot)
            if (st.icon[std::size_t(slot)] != -1) erase_icon(engine, slot, st.icon[std::size_t(slot)]);
        st.timer.fill(0);
        st.icon.fill(-1);
        return;
    }
    if (st.timer[8] && ram.seq_name != "SQ_SLEP") {
        remove_timer(ram, "SLEEP");
        st.start[8] = false;
        st.timer[8] = 0;
    }
    if (st.timer[9]) {
        if (ram.seq_name == "SQ_WAIT") {
            Packet p;
            command(engine, ram, "SQ_STUN", p);
        } else if (ram.seq_name != "SQ_STUN") {
            remove_timer(ram, "STUN");
            st.start[9] = false;
            st.timer[9] = 0;
            erase_icon(engine, 9, st.icon[9]);
        }
    }
}

} // namespace dw4::engine
