#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace dw4::engine {
namespace {
// 804EAD24..804EAE10: level rows (upper level, origin, HP/MP base, HP/MP slope,
// attribute base, attribute slope). The first upper bound >= level applies.
struct LevelRow {
    int upper, origin, hp_base, hp_slope, attr_base, attr_slope;
};
constexpr LevelRow kLevelRows[] = {
    {5, 1, 0, 10, 0, 10},          {10, 5, 40, 20, 40, 9},        {20, 10, 140, 50, 85, 8},
    {30, 20, 640, 80, 165, 7},     {50, 30, 1440, 120, 235, 6},   {70, 50, 3840, 180, 355, 5},
    {110, 70, 7440, 260, 455, 4},  {150, 110, 17840, 180, 615, 3}, {230, 150, 25040, 120, 735, 2},
    {999, 230, 34640, 80, 895, 1},
};

const LevelRow& level_row(int level) {
    for (const auto& row : kLevelRows)
        if (level <= row.upper) return row;
    return kLevelRows[std::size(kLevelRows) - 1];
}

// 8036F808 / 8036FB80: ((level-origin)*slope + base) * grow / 100 + upcnt + initial.
int grown_value(int level, bool health, int grow, int initial, int upcnt, int cap) {
    const auto& row = level_row(level);
    const int base = (level - row.origin) * (health ? row.hp_slope : row.attr_slope) + (health ? row.hp_base : row.attr_base);
    return std::min(base * grow / 100 + upcnt + initial, cap);
}

// The one system generator (beSystem +0x30), seeded at boot.
SystemRandom& rng() {
    static SystemRandom generator;
    return generator;
}
} // namespace

void post_effect(Engine& engine, std::string category, std::optional<std::string> name, int index, std::string model,
                 std::string sequence) {
    Packet effect;
    effect.strings.push_back(std::move(model));
    effect.strings.push_back(std::move(sequence));
    effect.floats.push_back(1.0f);          // flt_804567AC / flt_80459744 / flt_8045C3E8
    effect.vectors.push_back(Vec3f{});      // flt_804F8AE0
    effect.bools = {0, 1};
    engine.messenger().post(engine.time() + kFrameNs, std::move(category), std::move(name), index, "SETEFFECT",
                            std::move(effect));
}

namespace fight {
// 8021EB40 (beSystem vtable +0x60, the only caller of genrand 8021B338).
// Test tool: DW4_FIXED_RANDOM=<hex> makes every draw return that constant
// without advancing the generator, like the Dolphin AR patch on 8021EB40
// (lis r3 / ori r3 / blr), so a fight runs the same in both whatever the
// clocks do.
std::uint32_t random() {
    static const char* const fixed_text = std::getenv("DW4_FIXED_RANDOM");
    static const std::uint32_t fixed = fixed_text ? std::uint32_t(std::strtoul(fixed_text, nullptr, 16)) : 0u;
    return fixed_text ? fixed : rng()();
}
SystemRandom& generator() { return rng(); }

namespace {
int database(Engine& engine, std::string_view table, int row, std::string_view column, int fallback = 0) {
    auto* db = engine.get<DatabaseManager>();
    return db ? db->number(table, row, column).value_or(fallback) : fallback;
}

int attribute_index(Engine& engine, int attack) {
    auto* db = engine.get<DatabaseManager>();
    const auto zokusei = db ? db->text("eneatk", attack, "ZOKUSEI") : std::string();
    static const char* names[] = {"fire", "ice", "thunder", "dark"};
    for (int i = 0; i < 4; ++i)
        if (zokusei == names[i]) return i;
    return -1;
}

// Shared tail of both damage paths: 95/97.5/100/102.5 percent plus up to 2.5.
int variance(int damage, int& flag) {
    damage = std::clamp(damage, 0, 99999);
    if (damage == 0) return 0;
    const std::uint32_t pick = random() & 3u;
    const std::uint32_t span = std::uint32_t(damage) * 25u;
    const int extra = span ? int(random() % span) : 0;
    static const int percent[] = {950, 975, 1000, 1025};
    damage = (damage * percent[pick] + extra) / 1000;
    if (pick == 3) flag = 1;
    return std::clamp(damage, 0, 99999);
}
} // namespace

// 804DD2A0: MDLgetBulletName selects by kind / 10000.
const char* bullet_name(int kind) {
    static const char* names[] = {"PCWA", "PCCA", "PCSA", "PCMA", "PCBA", "PCTK", "ENAT", "PCMK", "ENSP"};
    const int group = kind / 10000;
    return group >= 1 && group <= 9 ? names[group - 1] : "";
}

// P2EISHITCHECK (80372A44): magic (kind group 4) always hits; otherwise
// MEICHURITSU * (skill*20 + 6000 + SUBAYASA*30) / (enemy SUBAYASA * (100 - its
// KAIHIRITSU down) / 100) is compared with a random value below 10000.
bool player_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind, int evade_down) {
    if (kind / 10000 == 4) return true;
    auto* db = engine.get<DatabaseManager>();
    const int enemy_speed = db ? db->number("enemy", enemy_record, "SUBAYASA").value_or(1) : 1;
    const int skill = stats.skill[std::size_t(std::clamp(stats.weapon_skill_no, 0, 8))];
    const int divisor = enemy_speed * (100 - evade_down) / 100;
    const std::uint32_t roll = random() % 10000u;
    if (divisor == 0) return true;
    const int value = stats.meichuritsu * (skill * 20 + 6000 + stats.subayasa * 30) / divisor;
    return value > int(roll);
}

// 80371524: a technique bullet carries its family (kind % 1000, the TECHn
// base row); the player's WAZALIST entry of that family gives the rank row.
int technique_row(const GameSystem::PlayerSlot& player, int kind) {
    const int family = kind % 1000;
    for (const int row : player.wazalist)
        if (row >= family && row < family + 5) return row;
    return family;
}

// pcTech ZOKUSEI -> 0 fire, 1 ice, 2 thunder, 3 dark, 4 poison, -1 none.
int technique_element(DatabaseManager& db, int row) {
    static const char* names[] = {"fire", "ice", "thunder", "dark", "poison"};
    const auto element = db.text("pcTech", row, "ZOKUSEI");
    for (int i = 0; i < 5; ++i)
        if (element == names[i]) return i;
    return -1;
}

int player_attack_attribute(Engine& engine, const GameSystem::PlayerSlot& player, int kind) {
    if (kind / 10000 != 4) return player.stats.attack_zokusei;
    auto* db = engine.get<DatabaseManager>();
    if (!db) return -1;
    const int element = technique_element(*db, technique_row(player, kind));
    return element >= 0 && element < 4 ? element : -1;
}

const char* const kResistColumns[5] = {"RES_FIRE", "RES_ICE", "RES_THUNDER", "RES_DARK", "RES_POISON"};

// P2EISCRITICAL (80372C00): weapons CHIMEIRITSU > random % 200; techniques
// (kind group 4) scale the pcTech CHIMEIRITSU by the enemy's resistance to the
// element, or compare it with random % 100 without one.
bool player_critical(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind) {
    if (kind / 10000 != 4) return player.stats.chimeiritsu > int(random() % 200u);
    auto* db = engine.get<DatabaseManager>();
    if (!db) return false;
    const int row = technique_row(player, kind);
    const int element = technique_element(*db, row);
    const int critical = db->number("pcTech", row, "CHIMEIRITSU").value_or(0);
    if (element < 0) return critical > int(random() % 100u);
    const int resist = db->number("enemy", enemy_record, kResistColumns[element]).value_or(0);
    const int value = (critical + 100) * (10000 / (resist * 12 / 10 + 50)) / 100;
    return value > int(random() % 1000u);
}

// P2EDAMAGE (80372EC4).
FightResult player_damage(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, bool critical,
                          int defense_down) {
    FightResult result;
    auto* db = engine.get<DatabaseManager>();
    if (!db) return result;
    const auto& stats = player.stats;
    if (kind / 10000 == 4) {
        // Techniques: (KASHIKOSA*10 + skill*5 - HP*20) / enemy SEISHIN, the
        // element resistance in thousandths, the skill critical bonus.
        const int row = technique_row(player, kind);
        const int power = -db->number("pcTech", row, "HP").value_or(0);
        const int skill = stats.skill[std::size_t(std::clamp(db->number("pcTech", row, "SKILL").value_or(0), 0, 8))];
        int damage = 0;
        if (power != 0) {
            const int spirit = db->number("enemy", enemy_record, "SEISHIN").value_or(1);
            damage = spirit != 0 ? (stats.kashikosa * 10 + skill * 5 + power * 20) / spirit : 0;
            damage = std::clamp(damage, 0, 99999);
        }
        if (damage != 0) {
            const int element = technique_element(*db, row);
            const int resist = element >= 0 ? db->number("enemy", enemy_record, kResistColumns[element]).value_or(0) : 0;
            damage = damage * (1000 - resist) / 1000;
            result.attribute = element;
        }
        damage = std::clamp(damage, 0, 99999);
        if (damage != 0 && critical) {
            int bonus;
            if (skill <= 1000) bonus = skill / 20 + 30;
            else if (skill <= 2000) bonus = (skill - 1000) / 50 + 80;
            else bonus = (skill - 2000) / 100 + 100;
            damage += damage * bonus / 100;
        }
        result.damage = variance(damage, result.flag);
        return result;
    }
    const int skill = stats.skill[std::size_t(std::clamp(stats.weapon_skill_no, 0, 8))];
    const int defense = db->number("enemy", enemy_record, "MAMORI").value_or(1) - defense_down;
    int damage = defense != 0 ? (stats.tsuyosa * 6 + skill * 4) / defense : 0;
    damage = std::clamp(damage, 0, 99999);
    // Attack attribute: the enemy's resistance may add up to the full damage again.
    if (damage != 0 && stats.attack_zokusei >= 0 && stats.attack_zokusei <= 4) {
        static const char* resist[] = {"RES_FIRE", "RES_ICE", "RES_THUNDER", "RES_DARK", "RES_POISON"};
        const int res = db->number("enemy", enemy_record, resist[stats.attack_zokusei]).value_or(1000);
        if (res < int(random() % 1000u)) damage += damage * (1000 - res) / 1000;
        result.attribute = stats.attack_zokusei;
    }
    damage = std::clamp(damage, 0, 99999);
    if (damage != 0) {
        if (critical) {
            int bonus;
            if (skill <= 1000) bonus = skill / 20 + 30;
            else if (skill <= 2000) bonus = (skill - 1000) / 50 + 80;
            else bonus = (skill - 2000) / 100 + 100;
            damage += damage * bonus / 100;
        }
    }
    result.damage = variance(damage, result.flag);
    return result;
}


bool enemy_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind) {
    const int attack = kind % 10000;
    if (database(engine, "eneatk", attack, "TYPE") != 0) return true;
    if (stats.bougyoritsu > int(random() % 100u)) return false;
    const int enemy_speed = database(engine, "enemy", enemy_record, "SUBAYASA");
    const int accuracy = database(engine, "eneatk", attack, "MEICHURITSU");
    const int divisor = stats.subayasa * (stats.kaihiritsu + 100) / 100;
    const int value = divisor != 0 ? accuracy * enemy_speed / divisor : 100;
    return value > int(random() % 100u);
}

FightResult enemy_damage(Engine& engine, const GameSystem::Stats& stats, int max_hp, int enemy_record, int kind,
                         int strength_down, int critical_down) {
    FightResult result;
    const int attack = kind % 10000;
    const int type = database(engine, "eneatk", attack, "TYPE");
    const int power = database(engine, "eneatk", attack, "TSUYOSA");
    const int attribute = attribute_index(engine, attack);
    const int resist = attribute >= 0 ? stats.resist[std::size_t(attribute)] : 0;
    result.attribute = attribute;
    int damage = 0;
    if (type == 0) {
        // Physical: enemy TSUYOSA (less its down value) * attack power / player MAMORI.
        const int strength = database(engine, "enemy", enemy_record, "TSUYOSA") - strength_down;
        damage = stats.mamori != 0 ? std::clamp(strength * power / stats.mamori, 0, 99999) : 0;
        if (resist != 0 && !(resist / 5 > int(random() % 1000u))) {
            if (resist <= 1000) damage += damage * ((1000 - resist) / 20 + 50) / 100;
            else if (resist <= 5000) damage += damage * ((5000 - resist) / 80) / 100;
        }
        if (damage != 0) {
            const int enemy_critical = database(engine, "enemy", enemy_record, "CHIMEIRITSU") - critical_down;
            const int attack_critical = database(engine, "eneatk", attack, "CHIMEIRITSU");
            if (!((100 - enemy_critical) * (100 - attack_critical) > int(random() % 10000u)))
                damage += damage * (attack_critical / 2 + 90) / 100;
        }
    } else if (type == 1) {
        // Magic: attack power * enemy KASHIKOSA / player SEISHIN, scaled by resistance.
        if (power == 0) return result;
        const int wisdom = database(engine, "enemy", enemy_record, "KASHIKOSA");
        damage = stats.seishin != 0 ? std::clamp(wisdom * power / stats.seishin, 0, 99999) : 0;
        if (resist != 0) {
            if (resist <= 1000) damage = damage * ((1000 - resist) / 20 + 50) / 100;
            else if (resist <= 4200) damage = damage * ((5000 - resist) / 80) / 100;
            else damage = damage / 10;
        }
        const int attack_critical = database(engine, "eneatk", attack, "CHIMEIRITSU");
        if (damage != 0 && attack_critical != 0) {
            const int chance = resist <= 1000 ? (1000 - resist) + attack_critical * 100 : attack_critical * 100 - (resist - 1000) / 10;
            if (!(chance > int(random() % 10000u))) damage += (attack_critical / 2 + damage + 90) / 100;
        }
    } else if (type == 2) {
        // Proportional: a percentage of the player's maximum HP.
        damage = std::clamp(max_hp * power / 100, 0, 99999);
        if (resist != 0) {
            if (resist <= 1000) damage += damage * ((1000 - resist) / 20 + 50) / 100;
            else if (resist <= 5000) damage += damage * ((5000 - resist) / 80) / 100;
        }
    }
    result.damage = variance(damage, result.flag);
    return result;
}

// E2PDAMAGERANK: the eneatk SEQUENCE column picks the reaction; -1 is none.
int enemy_damage_rank(Engine& engine, int kind, int hp, int damage, int max_hp, const FightResult& fight) {
    if (hp <= 0) return 3;
    const int attack = kind % 10000;
    int rank = 0;
    switch (database(engine, "eneatk", attack, "SEQUENCE")) {
    case 0: {
        if (database(engine, "eneatk", attack, "TSUYOSA") == 0) {
            rank = -1;
            break;
        }
        const int percent = max_hp != 0 ? damage * 100 / max_hp : 0;
        rank = percent < 10 ? 0 : percent < 40 ? 1 : 2;
        break;
    }
    case 1: rank = -1; break;
    case 3: rank = 2; break;
    case 4: rank = 1; break;
    default: rank = 0; break;
    }
    if (rank == 0 && fight.flag == 1) rank = 1;
    return rank;
}

// BLTgetDamageRank (802EE91C): 3 when dead, 2 above half, 1 above a quarter
// of the maximum; small hits grow one rank for the flag, a critical or charge
// and special attacks (kind groups 2 and 3).
int damage_rank(int hp, int max_hp, const FightResult& fight, bool critical, int kind) {
    if (hp <= 0) return 3;
    int rank = 0;
    const float ratio = max_hp != 0 ? float(fight.damage) / float(max_hp) : 0.0f;
    if (ratio > 0.5f) rank = 2;
    else if (ratio > 0.25f) rank = 1;
    if (rank == 0 && fight.flag == 1) return 1;
    if (rank == 0 || rank == 1) {
        if (critical) return rank + 1;
        const int group = kind / 10000;
        if (group == 2 || group == 3) return rank + 1;
    }
    return rank;
}

// --- Status effect times (FIGHTCALC cases 7..12 E2P*, 14..19 P2E*) ---
// Times are frames (SETPOISON turns its time into 2 second ticks). A result
// of 0 means no attempt; an attempt that fails answers -1 (P2ESTUNTIME
// answers 0 either way).
namespace {
bool attack_effect(Engine& engine, int attack, std::string_view effect) {
    auto* db = engine.get<DatabaseManager>();
    return db && db->text("eneatk", attack, "KOUKA") == effect;
}

// E2P chance factor: 3 * enemy KASHIKOSA / SEISHIN + 100 - SEISHIN / 40.
int enemy_status_factor(Engine& engine, const GameSystem::Stats& stats, int enemy_record) {
    const int wisdom = database(engine, "enemy", enemy_record, "KASHIKOSA");
    const int spirit = std::max(stats.seishin, 1);
    return 3 * wisdom / spirit + 100 - spirit / 40;
}

int attempted(int time) { return time != 0 ? time : -1; }

// P2E sleep / bind / down chance per mille: skill tiers over enemy SEISHIN.
int technique_status_chance(Engine& engine, int skill, int enemy_record) {
    const int spirit = std::max(database(engine, "enemy", enemy_record, "SEISHIN"), 1);
    if (skill <= 1000) return 100 * (skill / 4 + 550) / spirit;
    if (skill <= 2000) return 100 * ((skill - 1000) / 5 + 800) / spirit;
    return 100 * ((skill - 2000) / 10 + 1000) / spirit;
}

struct TechniqueUse {
    int row{}, skill{};
};
TechniqueUse technique_use(Engine& engine, const GameSystem::PlayerSlot& player, int kind) {
    TechniqueUse use;
    use.row = technique_row(player, kind);
    const int skill_type = std::clamp(database(engine, "pcTech", use.row, "SKILL"), 0, 8);
    use.skill = player.stats.skill[std::size_t(skill_type)];
    return use;
}

// P2ESLEEPTIME / P2EBINDTIME and the TIME of both downs.
int technique_status_time(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind) {
    const auto use = technique_use(engine, player, kind);
    int time = 0;
    if (technique_status_chance(engine, use.skill, enemy_record) > int(random() % 1000u))
        time = player.stats.kashikosa / 2 + database(engine, "pcTech", use.row, "TIME");
    return attempted(time);
}

// P2E down value: the technique's (negative) value less skill / 20, at most
// 80 percent of the enemy's value, answered as a positive amount.
int technique_down(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, const char* column) {
    const auto use = technique_use(engine, player, kind);
    const int floor = -database(engine, "enemy", enemy_record, column) * 80 / 100;
    const int value = std::max(database(engine, "pcTech", use.row, column) - use.skill / 20, floor);
    return -value;
}
} // namespace

// E2PSTUNTIME (80375E28): magic attacks stun by their MAHIRITSU, physical
// ones by the enemy's MAHIRITSU (less its down value) on a medium or large
// hit, proportional ones on any such hit; plus the damage share of maximum HP
// and the player's MAHITEIKOU.
int enemy_stun_time(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, int damage, int rank,
                    int mahiritsu_down) {
    const int attack = kind % 10000;
    int from_attack = 0, from_enemy = 0;
    bool attempt = false;
    switch (database(engine, "eneatk", attack, "TYPE")) {
    case 0:
        if (rank >= 1 && rank < 3) {
            from_enemy = database(engine, "enemy", enemy_record, "MAHIRITSU") - mahiritsu_down;
            attempt = true;
        }
        break;
    case 1:
        from_attack = database(engine, "eneatk", attack, "MAHIRITSU");
        attempt = from_attack > 0;
        break;
    case 2: attempt = rank >= 1 && rank < 3; break;
    default: break;
    }
    if (!attempt) return 0;
    const int value = 50 * damage / std::max(player.max_hp, 1) + from_attack + from_enemy + player.stats.mahiteikou;
    int time = 0;
    if ((value + 100) / 2 > int(random() % 100u)) time = (value + 30) / 4;
    return attempted(time);
}

// E2PPOISONTIME (8037606C): "doku" attacks; TIME, DAMAGE (share of maximum
// HP) and GEDOKU.
int enemy_poison(Engine& engine, const GameSystem::PlayerSlot& player, int kind, std::string_view what) {
    const int attack = kind % 10000;
    if (what == "TIME") {
        if (!attack_effect(engine, attack, "doku")) return 0;
        const int rank = database(engine, "eneatk", attack, "KOUKARANK");
        int time = 0;
        if (15 * rank + player.stats.dokuteikou + 30 > int(random() % 100u))
            time = player.stats.dokuteikou / 2 + database(engine, "eneatk", attack, "KOUKATIME");
        return attempted(time);
    }
    if (what == "DAMAGE") return player.max_hp * database(engine, "eneatk", attack, "KOUKARANK") / 100;
    if (what == "GEDOKU") return database(engine, "eneatk", attack, "GEDOKU");
    return 0;
}

// E2PSLEEPTIME (803762A4): "sleep" attacks.
int enemy_sleep_time(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind) {
    const int attack = kind % 10000;
    if (!attack_effect(engine, attack, "sleep")) return 0;
    const int rank = database(engine, "eneatk", attack, "KOUKARANK");
    const int factor = enemy_status_factor(engine, stats, enemy_record);
    int time = 0;
    if (5 * rank * factor > int(random() % 10000u)) time = database(engine, "eneatk", attack, "KOUKATIME") * factor / 100;
    return attempted(time);
}

// E2PBINDTIME (8037643C): "ashidome" attacks; proportional ones always bind.
int enemy_bind_time(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind) {
    const int attack = kind % 10000;
    if (!attack_effect(engine, attack, "ashidome")) return 0;
    const int rank = database(engine, "eneatk", attack, "KOUKARANK");
    const int type = database(engine, "eneatk", attack, "TYPE");
    int time = 0;
    if (type == 2) {
        time = database(engine, "eneatk", attack, "KOUKATIME");
    } else {
        const int factor = enemy_status_factor(engine, stats, enemy_record);
        if (5 * rank * factor > int(random() % 10000u)) time = database(engine, "eneatk", attack, "KOUKATIME") * factor / 100;
    }
    return attempted(time);
}

namespace {
// E2PATKDWNTIME / E2PPOWDWNTIME: TIME for the effect; a value clears its
// DOWNDATA entry, recalculates the player and answers a percentage of the
// stat (KOUKARANK + 150 * enemy KASHIKOSA / SEISHIN / 100 - SEISHIN / 80,
// within 20..80).
int enemy_down(Engine& engine, int slot, int enemy_record, int kind, std::string_view what, const char* effect,
               std::initializer_list<std::pair<const char*, int>> values) {
    auto* sys = engine.get<GameSystem>();
    if (!sys) return 0;
    auto& player = sys->players[std::size_t(std::clamp(slot, 0, 3))];
    const int attack = kind % 10000;
    if (what == "TIME") {
        if (!attack_effect(engine, attack, effect)) return 0;
        const int rank = database(engine, "eneatk", attack, "KOUKARANK");
        const int factor = enemy_status_factor(engine, player.stats, enemy_record);
        int time = 0;
        if (160 * rank / 100 * factor > int(random() % 10000u))
            time = database(engine, "eneatk", attack, "KOUKATIME") * factor / 100;
        return attempted(time);
    }
    for (const auto& [name, index] : values) {
        if (what != name) continue;
        player.downdata[std::size_t(index)] = 0;
        sys->recalc_player(engine, slot);
        const int rank = database(engine, "eneatk", attack, "KOUKARANK");
        const int wisdom = database(engine, "enemy", enemy_record, "KASHIKOSA");
        const int spirit = std::max(player.stats.seishin, 1);
        const int percent = std::clamp(rank + 150 * wisdom / spirit / 100 - spirit / 80, 20, 80);
        int stat = 0;
        if (what == "SUBAYASA") stat = player.stats.subayasa;
        else if (what == "MEICHURITSU") stat = player.stats.meichuritsu;
        else if (what == "TSUYOSA") stat = player.stats.tsuyosa;
        else if (what == "MAMORI") stat = player.stats.mamori;
        return stat * percent / 100;
    }
    return 0;
}
} // namespace

// E2PATKDWNTIME (8037667C): "freeze"; SUBAYASA -> DOWNDATA 0, MEICHURITSU -> 1.
int enemy_attack_down(Engine& engine, int slot, int enemy_record, int kind, std::string_view what) {
    return enemy_down(engine, slot, enemy_record, kind, what, "freeze", {{"SUBAYASA", 0}, {"MEICHURITSU", 1}});
}

// E2PPOWDWNTIME (80376AB0): "powerdown"; TSUYOSA -> DOWNDATA 4, MAMORI -> 5.
int enemy_power_down(Engine& engine, int slot, int enemy_record, int kind, std::string_view what) {
    return enemy_down(engine, slot, enemy_record, kind, what, "powerdown", {{"TSUYOSA", 4}, {"MAMORI", 5}});
}

// P2ESTUNTIME (80373FA0): the player's MAHIRITSU and the damage share of the
// enemy's maximum HP against its RES_STUN.
int player_stun_time(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int max_hp, int damage) {
    const int resist = database(engine, "enemy", enemy_record, "RES_STUN");
    const int hp = std::max(max_hp, 1);
    const int chance = stats.mahiritsu + (50 * damage / hp + 100) - resist;
    if (!(chance / 2 > int(random() % 100u))) return 0;
    return (200 - resist) * (30 * damage / hp + stats.mahiritsu * 3) / 400;
}

// P2EPOISONTIME (803740C8): TIME from the technique skill against RES_POISON;
// DAMAGE is a fifth of the technique's HP.
int player_poison(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, std::string_view what) {
    const auto use = technique_use(engine, player, kind);
    if (what != "TIME") return -(database(engine, "pcTech", use.row, "HP") / 5);
    const int keep = 1000 - database(engine, "enemy", enemy_record, "RES_POISON");
    int threshold;
    if (use.skill <= 1000) threshold = (use.skill / 4 + 750) * keep / 1000;
    else if (use.skill <= 2000) threshold = ((use.skill - 1000) / 5 + 1000) * keep / 1000;
    else threshold = ((use.skill - 2000) / 10 + 1200) * keep / 1000;
    int time = 0;
    if (threshold > int(random() % 1000u)) time = player.stats.kashikosa / 2 + database(engine, "pcTech", use.row, "TIME");
    return attempted(time);
}

// P2ESLEEPTIME (803743C0) / P2EBINDTIME (803745B8).
int player_sleep_time(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind) {
    return technique_status_time(engine, player, enemy_record, kind);
}

// P2EATKDWNTIME (803747B0): SUBAYASA limited by the enemy's; CHIMEIRITSU,
// MAHIRITSU (pcTech MHIRITSU) and KAIHIRITSU straight from the technique.
int player_attack_down(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, std::string_view what) {
    if (what == "TIME") return technique_status_time(engine, player, enemy_record, kind);
    if (what == "SUBAYASA") return technique_down(engine, player, enemy_record, kind, "SUBAYASA");
    const auto use = technique_use(engine, player, kind);
    if (what == "CHIMEIRITSU") return -database(engine, "pcTech", use.row, "CHIMEIRITSU");
    if (what == "MAHIRITSU") return -database(engine, "pcTech", use.row, "MHIRITSU");
    if (what == "KAIHIRITSU") return -database(engine, "pcTech", use.row, "KAIHIRITSU");
    return 0;
}

// P2EPOWDWNTIME (80374C60): TSUYOSA / MAMORI limited by the enemy's.
int player_power_down(Engine& engine, const GameSystem::PlayerSlot& player, int enemy_record, int kind, std::string_view what) {
    if (what == "TIME") return technique_status_time(engine, player, enemy_record, kind);
    if (what == "TSUYOSA" || what == "MAMORI")
        return technique_down(engine, player, enemy_record, kind, what == "TSUYOSA" ? "TSUYOSA" : "MAMORI");
    return 0;
}

namespace {
// 803715AC: RAM numbers of the players (or bit capsules) within 30 units of
// a point that still have HP, one RAM number excluded.
std::vector<int> models_around(Engine& engine, bool players, const Vec3f& point, int exclude) {
    std::vector<int> found;
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return found;
    for (const auto& ram : models->rams()) {
        if (!ram->generated || !ram->node || ram->index == exclude) continue;
        if (players ? !ram->is_player : ram->category != "PLAYER2") continue;
        const auto& m = ram->node->matrix;
        const float dx = m(3, 0) - point.x, dy = m(3, 1) - point.y, dz = m(3, 2) - point.z;
        if (dz * dz + (dx * dx + dy * dy) < 900.0f && ram->hp != 0) found.push_back(ram->index);
    }
    return found;
}
} // namespace

// P2PKOUKAMAGIC (80376EE4): a support technique acts once, when its bullet
// reaches the caster itself, on the players around the caster.
int support_magic(Engine& engine, int target, int shooter, int kind) {
    if (target != shooter) return 0;
    auto* sys = engine.get<GameSystem>();
    auto* models = engine.get<ModelCtrlManager>();
    if (!sys || !models) return 0;
    const int slot = std::clamp(target, 0, 3);
    auto& caster = sys->players[std::size_t(slot)];
    const int family = kind % 10000;
    Vec3f point{};
    if (const auto* model = models->player(slot); model && model->node) {
        const auto& m = model->node->matrix;
        point = {m(3, 0), m(3, 1), m(3, 2)};
    }
    // RAISE to the bit capsules around: EXP percent and HP of the rank
    // (804EB164 / 804EB178).
    auto raise = [&](int rank) {
        static const int exp_percent[5] = {59, 68, 77, 86, 95};
        static const int raise_hp[5] = {100, 400, 1600, 3200, 6400};
        rank = std::clamp(rank, 0, 4);
        Mat4 matrix;
        if (const auto* model = models->player(slot); model && model->node) matrix = model->node->matrix;
        for (const int capsule : models_around(engine, false, point, -1)) {
            Packet packet;
            packet.matrices.push_back(matrix);
            packet.ints = {exp_percent[rank], raise_hp[rank]};
            engine.messenger().send(engine, "PLAYER2", std::nullopt, capsule, "RAISE", packet);
        }
    };
    // 8037754C: a negative kind is a disk's raise with the first entry.
    if (kind < 0) {
        if (kind % 10000 != -50) return 0;
        raise(0);
        return 1;
    }
    const auto use = technique_use(engine, caster, kind);
    auto column = [&](const char* name) { return database(engine, "pcTech", use.row, name); };
    auto send = [&](int player, const char* command, Packet& packet) {
        engine.messenger().send(engine, "PLAYER", sys->players[std::size_t(std::clamp(player, 0, 3))].digimon, player, command,
                                packet);
    };
    // The hit effect on the other players, posted (803771A4 heal all,
    // 8037751C cure all, 803779A0 / 80377C78 / 80377F24 buffs).
    auto effect = [&](int player, const char* model, const char* sequence) {
        post_effect(engine, "PLAYER", sys->players[std::size_t(std::clamp(player, 0, 3))].digimon, player, model, sequence);
    };
    // Buffs reach the players around and the caster (without its effect).
    auto buff = [&](const char* command, std::initializer_list<int> values) {
        auto players = models_around(engine, true, point, slot);
        players.push_back(slot);
        for (const int player : players) {
            Packet packet;
            for (const int value : values) packet.ints.push_back(value);
            send(player, command, packet);
            if (player != slot) effect(player, "x_250uphit00", "SQ_UPEFONLY");
        }
    };
    switch (family) {
    case 40: // heal: technique HP + skill + KASHIKOSA
    case 45: // heal all
    {
        const int amount = column("HP") + use.skill + caster.stats.kashikosa;
        sys->change_hp(engine, slot, amount);
        if (family == 45)
            for (const int player : models_around(engine, true, point, slot)) {
                sys->change_hp(engine, player, amount);
                effect(player, "x_250healhit00", "SQ_HEALEFONLY");
            }
        break;
    }
    case 55: // cure: ends poison when rank + 5 - GEDOKU reaches random % 10
    case 60: // cure all
    {
        auto cure = [&](int player) {
            auto* ram = models->find(sys->players[std::size_t(std::clamp(player, 0, 3))].digimon, player);
            if (!ram || !ram->status || !ram->status->timer[0]) return;
            if (use.row - family + 5 - ram->status->gedoku >= int(random() % 10u)) models->clear_poison(*ram);
        };
        cure(slot);
        if (family == 60)
            for (const int player : models_around(engine, true, point, slot)) {
                cure(player);
                effect(player, "x_250curehit00", "SQ_CUREEFONLY");
            }
        break;
    }
    case 50: // raise: bit capsules around get RAISE by the technique's rank
        raise(use.row - 50);
        break;
    case 65: // speed up: SUBAYASA + skill / 5 and the hit values
        buff("SETSPEEDUP", {column("TIME") + caster.stats.kashikosa, use.skill / 5 + column("SUBAYASA"), column("CHIMEIRITSU"),
                            column("MHIRITSU"), column("KAIHIRITSU")});
        break;
    case 80: { // power up: TSUYOSA, MAMORI and MAXHP + skill / 5
        const int bonus = use.skill / 5;
        buff("SETPOWERUP", {column("TIME") + caster.stats.kashikosa, bonus + column("TSUYOSA"), bonus + column("MAMORI"),
                            bonus + column("MAXHP")});
        break;
    }
    case 75: // refract: the element resistances
        buff("SETREFRACT", {column("TIME"), column("FIRE"), column("ICE"), column("THUNDER"), column("DARK")});
        break;
    default: break;
    }
    return 1;
}

namespace {
// 804EAF18: skill up chances per level bracket (to 36, 107, 227, above), skill
// rank (804EB110 limits) and column (0 hit, 1 unused, 2 critical).
constexpr std::int16_t kSkillUpTable[4][21][3] = {
    {{10, 5, 12}, {13, 7, 16}, {16, 9, 21}, {21, 12, 27}, {28, 15, 35}, {37, 20, 46}, {48, 26, 60}, {62, 34, 78}, {81, 44, 101}, {106, 58, 132}, {132, 72, 165}, {165, 91, 207}, {207, 113, 258}, {258, 142, 323}, {323, 177, 404}, {404, 222, 505}, {505, 278, 632}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{5, 2, 6}, {6, 3, 8}, {8, 4, 10}, {10, 6, 13}, {14, 7, 17}, {18, 10, 23}, {24, 13, 30}, {31, 17, 39}, {40, 22, 50}, {53, 29, 66}, {66, 36, 82}, {82, 45, 103}, {103, 56, 129}, {129, 71, 161}, {161, 88, 202}, {202, 111, 252}, {252, 139, 316}, {316, 173, 395}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{2, 1, 3}, {3, 1, 4}, {4, 2, 5}, {5, 3, 6}, {7, 4, 8}, {9, 5, 11}, {12, 7, 15}, {15, 9, 19}, {20, 12, 25}, {26, 15, 33}, {33, 19, 41}, {41, 24, 51}, {51, 31, 64}, {64, 38, 80}, {80, 48, 101}, {101, 60, 126}, {126, 75, 158}, {158, 94, 197}, {205, 123, 256}, {0, 0, 0}, {0, 0, 0}},
    {{1, 0, 1}, {1, 0, 2}, {2, 1, 2}, {2, 1, 3}, {3, 2, 4}, {4, 2, 5}, {6, 3, 7}, {7, 4, 9}, {10, 6, 12}, {13, 7, 16}, {16, 9, 20}, {20, 12, 25}, {25, 15, 32}, {32, 19, 40}, {40, 24, 50}, {50, 30, 63}, {63, 37, 79}, {79, 47, 98}, {102, 61, 128}, {128, 77, 160}, {160, 96, 200}},
};
constexpr std::int16_t kSkillRanks[21] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 30, 100, 300, 500, 1000, 2000, 3000, 4000, 6000, 10000};
// 804EB13C: the shinka growth column of each weapon skill.
constexpr const char* kSkillGrow[6] = {"BLUNT_GROW", "SLASH_GROW", "STAB_GROW", "BASH_GROW", "SHOT_GROW", "CRUSH_GROW"};
} // namespace

// P2ESKILLUP (803736C8): below the level's skill limit the weapon skill (or a
// technique's skill) may grow by one; weapons scale the chance by the form's
// growth for that skill. The hit flag folds away in the original: only a
// critical changes the column.
void skill_up(Engine& engine, int shooter, int kind, int /*hit*/, int critical) {
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    const int slot = std::clamp(shooter, 0, 3);
    auto& player = sys->players[std::size_t(slot)];
    auto& stats = player.stats;
    const int level = stats.level;
    int limit;
    if (level <= 36) limit = level * 30;
    else if (level <= 107) limit = level * 20 + 360;
    else if (level <= 227) limit = level * 10 + 1430;
    else limit = level * 8 + 1884;
    const int group = kind / 10000;
    const bool technique = group == 4 || group == 8 || group == 9;
    const int skill_type = std::clamp(technique ? database(engine, "pcTech", technique_row(player, kind), "SKILL") : stats.weapon_skill_no, 0, 8);
    const int skill = stats.skill[std::size_t(skill_type)];
    if (skill >= limit) return;
    const int bracket = level <= 36 ? 0 : level <= 107 ? 1 : level <= 227 ? 2 : 3;
    int rank = 20;
    for (int i = 0; i < 21; ++i)
        if (skill <= kSkillRanks[i]) {
            rank = i;
            break;
        }
    const int value = kSkillUpTable[bracket][rank][critical == 1 ? 2 : 0];
    int chance;
    if (technique) {
        chance = value * 100;
    } else {
        const int grow = skill_type < 6 ? database(engine, "shinka", stats.species, kSkillGrow[skill_type]) : 0;
        chance = value * grow * 500 / 1000;
    }
    static const bool trace = std::getenv("DW4_TRACE_SKILL") != nullptr;
    if (trace)
        engine.log("skill up chance " + std::to_string(chance) + "/100000 for skill " + std::to_string(skill_type) + " = " +
                   std::to_string(skill));
    if (!(chance > int(random() % 100000u))) return;
    stats.skill[std::size_t(skill_type)] = skill + 1;
    engine.log("player " + std::to_string(slot) + " skill " + std::to_string(skill_type) + " -> " + std::to_string(skill + 1));
    sys->recalc_player(engine, slot);
    sys->store_species(slot);
    // The skill up panel over the player (i_getitempnl01 SQ_TECUP), posted (80373B40).
    post_effect(engine, "MODELCTRL", player.digimon, slot, "i_getitempnl01", "SQ_TECUP");
}
} // namespace fight

void GameSystem::prepare_player(Engine& engine, int slot) {
    if (!players[std::size_t(std::clamp(slot, 0, 3))].stats.ready) create_player(engine, slot);
    else recalc_player(engine, slot);
}

void GameSystem::create_player(Engine& engine, int slot) {
    if (load_test_record(engine, slot)) return;
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto& stats = player.stats;
    auto* db = engine.get<DatabaseManager>();
    if (!db) return;
    stats.species = std::max(0, db->find_row("shinka", "MODEL_NAME", player.digimon));
    stats.level = 1;
    stats.exp = 0;
    stats.skill.fill(1);
    player.upcnt.fill(0);
    player.base_isuse[stats.species] = 1;
    // 8036E124 / 8036E3B8: the three initial weapons fill DEVICEFOLDER and the socket.
    player.devices.clear();
    player.power_socket.assign(3, -1);
    player.power_pocket.assign(5, -1);
    player.main_slot.assign(1, -1);
    player.main_pocket.assign(5, -1);
    player.sub_slot.assign(1, -1);
    static const char* initial[] = {"INI_WEAPON1", "INI_WEAPON2", "INI_WEAPON3"};
    for (int i = 0; i < 3; ++i) {
        ItemDevice weapon;
        weapon.record = db->number("shinka", stats.species, initial[i]).value_or(0);
        weapon.flag = true;
        player.power_socket[std::size_t(i)] = int(player.devices.size());
        player.devices.push_back(weapon);
    }
    // DW4_DEVICES=type:record:rank:value,...: test runs start with more devices.
    if (const char* extra = std::getenv("DW4_DEVICES")) {
        for (const char* at = extra; *at;) {
            ItemDevice device;
            if (std::sscanf(at, "%d:%d:%d:%d", &device.type, &device.record, &device.rank, &device.value) == 4 &&
                player.devices.size() < 36)
                player.devices.push_back(device);
            while (*at && *at != ',') ++at;
            if (*at == ',') ++at;
        }
    }
    if (const char* bits = std::getenv("DW4_BITS")) player.bits = std::clamp(std::atoi(bits), 0, 9999999);
    if (const char* xdata = std::getenv("DW4_XDATA")) player.xdata = std::clamp(std::atoi(xdata), 0, 9999);
    // DW4_DISKS=record:count,...: disks for test runs.
    if (const char* extra = std::getenv("DW4_DISKS")) {
        for (const char* at = extra; *at;) {
            int record = 0, count = 0;
            if (std::sscanf(at, "%d:%d", &record, &count) == 2) player.disk_count(record) = std::clamp(count, 0, 9);
            while (*at && *at != ',') ++at;
            if (*at == ',') ++at;
        }
    }
    // (DW4_DIFFICULTY is set before the first stage loads, game_main.cpp.)
    // DW4_FLAG_FOLDER=item,...: key items (FLAG folder) a test run owns.
    if (const char* items = std::getenv("DW4_FLAG_FOLDER")) {
        for (const char* at = items; *at;) {
            flag_folder[std::atoi(at)] = 1;
            while (*at && *at != ',') ++at;
            if (*at == ',') ++at;
        }
    }
    // DW4_FORMS=species,...: forms a test run has reached (BASE_ISUSE).
    if (const char* forms = std::getenv("DW4_FORMS")) {
        for (const char* at = forms; *at;) {
            const int species = std::atoi(at);
            if (species >= 0 && species < 16) player.base_isuse[species] = 1;
            while (*at && *at != ',') ++at;
            if (*at == ',') ++at;
        }
    }
    // DW4_LEVEL=n: the level a test run starts at.
    if (const char* level = std::getenv("DW4_LEVEL")) {
        stats.level = std::clamp(std::atoi(level), 1, 999);
        player.base_level[std::size_t(std::clamp(stats.species, 0, 15))] = stats.level;
        // The EXP that reaches the level (NEXTEXP of the level below).
        const int below = stats.level - 1;
        stats.exp = (below + 140) * below * below + below * 200;
        player.base_exp[std::size_t(std::clamp(stats.species, 0, 15))] = stats.exp;
    }
    player.hp = 0; // RECALCPLAYER refills HP and MP from 0
    recalc_player(engine, slot);
    stats.ready = true;
}

void GameSystem::recalc_player(Engine& engine, int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto& stats = player.stats;
    auto* db = engine.get<DatabaseManager>();
    if (!db) return;
    // A card record's form is still open (apply_record): BASE2CURRENT of the
    // "shinka" row of its model name (803657D8), the record's level kept.
    // Without the table yet (a direct stage start before usa_pack.igb) the
    // record's HP, MP and level stay untouched until a later call.
    if (stats.species < 0) {
        if (db->rows("shinka") == 0) return;
        const int record_level = stats.level;
        load_species(engine, slot, std::max(0, db->find_row("shinka", "MODEL_NAME", player.digimon)));
        stats.species = std::max(stats.species, 0); // a row past the 16 BASE_* forms: form 0
        stats.level = record_level;
    }
    const int row = stats.species;
    const int level = stats.level;
    auto cell = [&](std::string_view table, int r, std::string_view column) { return db->number(table, r, column).value_or(0); };
    static const char* device_tables[4] = {"weapon", "armor1", "armor2", "abChip"};
    auto column = [&](const ItemDevice& d, std::string_view c) { return cell(device_tables[std::clamp(d.type, 0, 3)], d.record, c); };
    // 8036F484 socket weapon (+ value), 8036F534 weapon chips, 8036F5DC armor1
    // (+ value), 8036F68C armor chips, 8036F734 armor2 (+ value on its main stat).
    const ItemDevice* weapon = player.device(player.power_socket[0]);
    const ItemDevice* armor1 = player.device(player.main_slot[0]);
    const ItemDevice* armor2 = player.device(player.sub_slot[0]);
    auto socket = [&](int& v, const char* c, bool value) {
        if (!weapon) return;
        v += column(*weapon, c);
        if (value) v += weapon->value;
    };
    auto chips = [&](int& v, const char* c, const std::vector<int>& list) {
        for (const int index : list)
            if (const auto* d = player.device(index)) v += column(*d, c);
    };
    auto power_pocket = [&](int& v, const char* c) { chips(v, c, player.power_pocket); };
    auto main_slot = [&](int& v, const char* c, bool value) {
        if (!armor1) return;
        v += column(*armor1, c);
        if (value) v += armor1->value;
    };
    auto main_pocket = [&](int& v, const char* c) { chips(v, c, player.main_pocket); };
    auto sub_slot = [&](int& v, const char* c, bool value) {
        if (!armor2) return;
        v += column(*armor2, c);
        if (value && db->text("armor2", armor2->record, "SHUSEINOURYOKU") == c) v += armor2->value;
    };
    const auto& up = player.upcnt;
    const auto& buff = player.updata;
    const auto& down = player.downdata;
    auto grown = [&](const char* ini, const char* grow, int upcnt) {
        return grown_value(level, false, cell("shinka", row, grow), cell("shinka", row, ini), upcnt, 9999);
    };
    auto limit = [](int v) { return std::clamp(v, 1, 9999); };

    int mhp = grown_value(level, true, cell("shinka", row, "MHP_GROW"), cell("shinka", row, "MHP_INI"), up[0], 99999) + buff[6];
    main_slot(mhp, "MAXHP", false);
    sub_slot(mhp, "MAXHP", false);
    player.max_hp = std::min(mhp, 99999);
    const bool refill = player.hp == 0;
    if (refill || player.hp > player.max_hp) player.hp = player.max_hp;
    int mmp = grown_value(level, true, cell("shinka", row, "MMP_GROW"), cell("shinka", row, "MMP_INI"), up[1], 99999);
    main_slot(mmp, "MAXMP", false);
    sub_slot(mmp, "MAXMP", false);
    player.max_mp = std::min(mmp, 99999);
    if (refill || player.mp > player.max_mp) player.mp = player.max_mp;

    int tsuyosa = grown("STR_INI", "STR_GROW", up[2]) + buff[4] - down[4];
    socket(tsuyosa, "TSUYOSA", true);
    power_pocket(tsuyosa, "TSUYOSA");
    sub_slot(tsuyosa, "TSUYOSA", true);
    stats.tsuyosa = limit(tsuyosa);
    int mamori = grown("AC_INI", "AC_GROW", up[3]) + buff[5] - down[5];
    main_slot(mamori, "MAMORI", true);
    sub_slot(mamori, "MAMORI", true);
    stats.mamori = limit(mamori);
    auto mind = [&](const char* ini, const char* grow, int upcnt, const char* c, bool sub_value, int extra) {
        int v = grown(ini, grow, upcnt) + extra;
        socket(v, c, false);
        power_pocket(v, c);
        main_slot(v, c, false);
        main_pocket(v, c);
        sub_slot(v, c, sub_value);
        return limit(v);
    };
    stats.kashikosa = mind("INT_INI", "INT_GROW", up[4], "KASHIKOSA", true, 0);
    stats.seishin = mind("MND_INI", "MND_GROW", up[5], "SEISHIN", false, 0);
    stats.subayasa = mind("DEX_INI", "DEX_GROW", up[6], "SUBAYASA", true, buff[0] - down[0]);
    static const char* resists[] = {"RES_FIRE", "RES_ICE", "RES_THUNDER", "RES_DARK"};
    for (std::size_t i = 0; i < 4; ++i) {
        int v = cell("shinka", row, resists[i]) + up[7 + i] + buff[7 + i];
        main_slot(v, resists[i], false);
        main_pocket(v, resists[i]);
        sub_slot(v, resists[i], true);
        stats.resist[i] = limit(v);
    }

    // WAZALIST: each TECHn reached by LEVEL, at the highest rank whose
    // TECHn_SKILLk the technique's skill points meet.
    std::vector<int> learned_before; // (observe-only, port_events.hpp)
    if (engine.port_events_enabled()) learned_before = player.wazalist;
    player.wazalist.clear();
    for (int n = 1; n <= 6; ++n) {
        const std::string tech = "TECH" + std::to_string(n);
        const int base = cell("shinka", row, tech);
        const int skill_type = std::clamp(cell("pcTech", base, "SKILL"), 0, 8);
        const int points = stats.skill[std::size_t(skill_type)];
        if (cell("shinka", row, tech + "_LEVEL") > level) continue;
        for (int rank = 4; rank >= 0; --rank)
            if (cell("shinka", row, tech + "_SKILL" + std::to_string(rank + 1)) <= points) {
                player.wazalist.push_back(base + rank);
                break;
            }
    }
    if (engine.port_events_enabled() && !learned_before.empty()) {
        // A technique the form had not (or a higher rank of one), when the
        // list before was this form's too (not a load or a form change).
        std::array<int, 6> bases{};
        for (int n = 1; n <= 6; ++n) bases[std::size_t(n - 1)] = cell("shinka", row, "TECH" + std::to_string(n));
        auto base_of = [&](int tech) {
            for (const int base : bases)
                if (tech >= base && tech <= base + 4) return base;
            return -1;
        };
        if (std::all_of(learned_before.begin(), learned_before.end(), [&](int tech) { return base_of(tech) >= 0; }))
            for (const int tech : player.wazalist)
                if (std::find(learned_before.begin(), learned_before.end(), tech) == learned_before.end())
                    engine.port_event({PortEvent::Kind::technique, slot, tech, tech - base_of(tech), {}});
    }
    // NEXTEXP (80370DBC): (level + 140) * level^2 + level * 200; none at 999.
    stats.next_exp = level >= 999 ? 0 : (level + 140) * level * level + level * 200;

    int delay = down[6];
    socket(delay, "DELAY", false);
    stats.attack_delay = std::min(delay, 70);
    stats.meichuritsu = 0;
    socket(stats.meichuritsu, "MEICHURITSU", false);
    int chimei = buff[1] - down[1];
    socket(chimei, "CHIMEIRITSU", false);
    power_pocket(chimei, "CHIMEIRITSU");
    stats.chimeiritsu = std::max(chimei, 1);
    stats.mahiritsu = 0;
    socket(stats.mahiritsu, "MAHIRITSU", false);
    int kaihi = buff[3] - down[3];
    main_pocket(kaihi, "CHOHATURITSU");
    stats.kaihiritsu = std::max(kaihi, 1);
    stats.bougyoritsu = 0;
    main_slot(stats.bougyoritsu, "BOUGYORITSU", false);
    int mahiteikou = buff[2] - down[2];
    main_slot(mahiteikou, "MAHITEIKOU", false);
    stats.mahiteikou = std::max(mahiteikou, 1);
    stats.dokuteikou = 0;
    main_slot(stats.dokuteikou, "DOKUTEIKOU", false);
    stats.bit_rate = stats.exp_rate = stats.luck_rate = 0;
    sub_slot(stats.bit_rate, "BITRITSU", false);
    sub_slot(stats.exp_rate, "EXPRITSU", false);
    sub_slot(stats.luck_rate, "LUCKRITSU", false);
    stats.attack_zokusei = -1;
    static const char* elements[] = {"fire", "ice", "thunder", "dark"};
    for (const int index : player.power_pocket) {
        const auto* chip = player.device(index);
        if (!chip) continue;
        const auto element = db->text("abChip", chip->record, "ZOKUSEI");
        for (int e = 0; e < 4 && stats.attack_zokusei < 0; ++e)
            if (element == elements[e]) stats.attack_zokusei = e;
        if (stats.attack_zokusei >= 0) break;
    }
    stats.hp_drain = stats.mp_drain = 0;
    power_pocket(stats.hp_drain, "HPDRAIN");
    power_pocket(stats.mp_drain, "MPDRAIN");
    // WEAPONRECNO / WEAPONSKILLNO and POWERSOCKETNAME (the models boot.lua loads).
    stats.weapon_record = weapon ? weapon->record : 0;
    stats.weapon_skill_no = weapon ? cell("weapon", stats.weapon_record, "SKILL") : 0;
    player.weapons = {};
    for (std::size_t i = 0; i < player.power_socket.size() && i < 3; ++i)
        if (const auto* d = player.device(player.power_socket[i])) player.weapons[i] = db->text("weapon", d->record, "MODEL");
    stats.weapon_model = player.weapons[0];
    // DW4_TRACE_STATS: every status calculation (compare a direct start with
    // a character select load).
    if (static const bool trace = std::getenv("DW4_TRACE_STATS") != nullptr; trace) {
        std::string line = "player " + std::to_string(slot) + " stats " + player.digimon + " form " + std::to_string(row) + " level " +
                           std::to_string(level) + " hp " + std::to_string(player.hp) + "/" + std::to_string(player.max_hp) + " mp " +
                           std::to_string(player.mp) + "/" + std::to_string(player.max_mp);
        for (const int v : {stats.tsuyosa, stats.mamori, stats.kashikosa, stats.seishin, stats.subayasa, stats.resist[0], stats.resist[1],
                            stats.resist[2], stats.resist[3], stats.exp, stats.next_exp, stats.attack_delay, stats.chimeiritsu,
                            stats.kaihiritsu})
            line += " " + std::to_string(v);
        for (const auto& weapon : player.weapons) line += " " + (weapon.empty() ? std::string("-") : weapon);
        engine.log(line);
    }
}

void GameSystem::store_bit_chip(int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto& chip = bit_chips[std::size_t(std::clamp(slot, 0, 3))];
    chip.digimon = player.digimon;
    chip.bits = player.bits;
    player.bits = 0;
    const int level = player.stats.level;
    if (level == 999 || level == 1) return;
    // The EXP that reaches this level and the one before (NEXTEXP of the
    // levels below): a fifth of the step is lost, never below this level.
    const int reach = (level - 1) * 200 + (level + 139) * (level - 1) * (level - 1);
    const int before = (level - 2) * 200 + (level + 138) * (level - 2) * (level - 2);
    const int exp = player.stats.exp;
    const int kept = std::max(exp - 2 * (reach - before) / 10, reach);
    chip.exp = exp - kept;
    player.stats.exp = kept;
}

void GameSystem::change_hp(Engine& engine, int slot, int delta) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    if (player.hp == 0) return;
    if (delta < 0 && engine.cheats().god) return;
    player.hp = std::clamp(player.hp + delta, 0, player.max_hp);
    if (auto* models = engine.get<ModelCtrlManager>())
        if (auto* model = models->find(player.digimon, slot)) model->hp = player.hp;
    Packet percent, value;
    percent.floats.push_back(100.0f * float(player.hp) / float(std::max(player.max_hp, 1)));
    value.ints.push_back(player.hp);
    engine.messenger().send(engine, "METERCTRL", "hpmt", slot, "SETPERCENT", percent);
    engine.messenger().send(engine, "NUMBERCTRL", "hpnm", slot, "SETVALUE", value);
}

void GameSystem::change_mp(Engine& engine, int slot, int delta) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    if (delta < 0 && engine.cheats().infinite_mp) return;
    player.mp = std::clamp(player.mp + delta, 0, player.max_mp);
    Packet percent, value;
    percent.floats.push_back(100.0f * float(player.mp) / float(std::max(player.max_mp, 1)));
    value.ints.push_back(player.mp);
    engine.messenger().send(engine, "METERCTRL", "mpmt", slot, "SETPERCENT", percent);
    engine.messenger().send(engine, "NUMBERCTRL", "mpnm", slot, "SETVALUE", value);
}

int GameSystem::experience_up(Engine& engine, int killer, int enemy_record) {
    auto* db = engine.get<DatabaseManager>();
    const int enemy_exp = db ? db->number("enemy", enemy_record, "EXP").value_or(0) : 0;
    int joined = 0;
    for (const auto& p : players) joined += p.joined ? 1 : 0;
    int killer_gain = 0;
    for (int slot = 0; slot < 4; ++slot) {
        auto& player = players[std::size_t(slot)];
        if (!player.joined || player.hp == 0) continue;
        if (!player.stats.ready) prepare_player(engine, slot);
        auto& stats = player.stats;
        if (stats.level >= 999) continue;
        int exp = enemy_exp;
        if (stats.exp_rate != 0) exp = exp * (stats.exp_rate + 100) / 100;
        // Party size scaling, then the defeating player keeps twice the share.
        switch (joined) {
        case 2: exp = exp * 140 / 3; break;
        case 3: exp = exp * 160 / 4; break;
        case 4: exp = exp * 200 / 5; break;
        default: exp = exp * 100 / 2; break;
        }
        exp = slot == killer ? exp / 50 : exp / 100;
        // Rebalancing option: the EXP rate.
        if (engine.cheats().exp_rate != 100) exp = int(std::int64_t(exp) * engine.cheats().exp_rate / 100);
        if (slot == killer) killer_gain = exp;
        stats.exp += exp;
        apply_level_ups(engine, slot);
    }
    return killer_gain;
}

bool GameSystem::apply_level_ups(Engine& engine, int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto& stats = player.stats;
    bool leveled = false;
    while (stats.level < 999 && stats.next_exp <= stats.exp) {
        ++stats.level;
        prepare_player(engine, slot);
        leveled = true;
    }
    if (!leveled) return false;
    engine.port_event({PortEvent::Kind::level_up, slot, stats.level, 0, {}}); // observe-only
    store_species(slot);
    player.hp = player.max_hp;
    player.mp = player.max_mp;
    auto* models = engine.get<ModelCtrlManager>();
    std::string name = player.digimon;
    if (models)
        if (auto* model = models->find(name, slot)) {
            model->max_hp = player.max_hp;
            model->hp = player.hp;
        }
    engine.log("player " + std::to_string(slot) + " reached level " + std::to_string(stats.level));
    // 80373DD4..80373F78: the level up panel over the player (locked to it)
    // and the HUD values, all posted with 1 frame.
    post_effect(engine, "MODELCTRL", name, slot, "i_getitempnl01", "SQ_LVUP");
    auto send = [&](const char* category, const char* target, const char* command, Packet p) {
        engine.messenger().post(engine.time() + kFrameNs, category, std::string(target), slot, command, std::move(p));
    };
    Packet level, full, hp, mp;
    level.ints.push_back(stats.level);
    full.floats.push_back(100.0f);
    hp.ints.push_back(player.hp);
    mp.ints.push_back(player.mp);
    send("NUMBERCTRL", "lvnm", "SETVALUE", level);
    send("METERCTRL", "hpmt", "SETPERCENT", full);
    send("NUMBERCTRL", "hpnm", "SETVALUE", hp);
    send("METERCTRL", "mpmt", "SETPERCENT", full);
    send("NUMBERCTRL", "mpnm", "SETVALUE", mp);
    return true;
}

void GameSystem::cheat_levels(Engine& engine, int slot, int levels) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    if (!player.joined || player.hp == 0) return;
    if (!player.stats.ready) prepare_player(engine, slot);
    auto& stats = player.stats;
    const int target = std::clamp(stats.level + levels, 1, 999);
    // The EXP that reaches the level (NEXTEXP of the level below).
    const int below = target - 1;
    stats.exp = std::max(stats.exp, (below + 140) * below * below + below * 200);
    apply_level_ups(engine, slot);
}

void GameSystem::cheat_bits(int slot, int amount) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    player.bits = std::clamp(player.bits + amount, 0, 9999999);
}

void GameSystem::cheat_refill(Engine& engine, int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    if (!player.joined || player.hp == 0) return;
    change_hp(engine, slot, player.max_hp);
    change_mp(engine, slot, player.max_mp);
}

void GameSystem::store_species(int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    const int species = player.stats.species;
    if (species < 0 || species >= 16) return;
    const auto s = std::size_t(species);
    player.base_level[s] = player.stats.level;
    player.base_exp[s] = player.stats.exp;
    player.base_skill[s] = player.stats.skill;
    player.base_upcnt[s] = player.upcnt;
}

void GameSystem::load_species(Engine& engine, int slot, int species) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    if (species < 0 || species >= 16) return;
    const auto s = std::size_t(species);
    player.stats.species = species;
    if (auto* db = engine.get<DatabaseManager>()) {
        const auto model = db->text("shinka", species, "MODEL_NAME");
        if (!model.empty()) player.digimon = model;
    }
    player.stats.level = player.base_level[s];
    player.stats.exp = player.base_exp[s];
    player.stats.skill = player.base_skill[s];
    player.upcnt = player.base_upcnt[s];
}

bool GameSystem::weapon_usable(Engine& engine, int slot, int species) const {
    const auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto* db = engine.get<DatabaseManager>();
    if (!db || species < 0 || species >= 16) return false;
    const auto& skills = player.stats.species == species ? player.stats.skill : player.base_skill[std::size_t(species)];
    for (const auto& d : player.devices) {
        if (d.type != 0) continue;
        const auto soubi = std::uint32_t(db->number("weapon", d.record, "SOUBI").value_or(0));
        const int skill = db->number("weapon", d.record, "SKILL").value_or(0);
        const int need = db->number("weapon", d.record, "SKILLPOINT").value_or(0);
        if ((soubi & (1u << species)) && skill >= 0 && skill < 9 && skills[std::size_t(skill)] >= need) return true;
    }
    return false;
}

void GameSystem::equip_strongest_weapon(Engine& engine, int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto* db = engine.get<DatabaseManager>();
    if (!db) return;
    for (auto& d : player.devices)
        if (d.type == 0) d.flag = false;
    std::fill(player.power_socket.begin(), player.power_socket.end(), -1);
    player.weapons = {};
    const int species = player.stats.species;
    int best = -1, strongest = 0;
    for (std::size_t i = 0; i < player.devices.size(); ++i) {
        const auto& d = player.devices[i];
        if (d.type != 0 || species < 0 || species >= 32) continue;
        const auto soubi = std::uint32_t(db->number("weapon", d.record, "SOUBI").value_or(0));
        const int skill = db->number("weapon", d.record, "SKILL").value_or(0);
        const int need = db->number("weapon", d.record, "SKILLPOINT").value_or(0);
        if (!(soubi & (1u << species)) || skill < 0 || skill >= 9 || player.stats.skill[std::size_t(skill)] < need) continue;
        const int power = db->number("weapon", d.record, "TSUYOSA").value_or(0);
        if (best < 0 || strongest < power) {
            best = int(i);
            strongest = power;
        }
    }
    if (best < 0) return;
    if (!player.power_socket.empty()) player.power_socket[0] = best;
    player.weapons[0] = db->text("weapon", player.devices[std::size_t(best)].record, "MODEL");
    player.devices[std::size_t(best)].flag = true;
}

std::array<int, 7> GameSystem::form_stats(Engine& engine, int slot, int species) const {
    std::array<int, 7> out{};
    const auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto* db = engine.get<DatabaseManager>();
    if (!db || species < 0 || species >= 16) return out;
    const auto s = std::size_t(species);
    const int level = player.base_level[s];
    const auto& up = player.base_upcnt[s];
    auto cell = [&](const char* column) { return db->number("shinka", species, column).value_or(0); };
    out[0] = grown_value(level, true, cell("MHP_GROW"), cell("MHP_INI"), up[0], 99999);
    out[1] = grown_value(level, true, cell("MMP_GROW"), cell("MMP_INI"), up[1], 99999);
    static const char* ini[5] = {"STR_INI", "AC_INI", "INT_INI", "MND_INI", "DEX_INI"};
    static const char* grow[5] = {"STR_GROW", "AC_GROW", "INT_GROW", "MND_GROW", "DEX_GROW"};
    for (std::size_t i = 0; i < 5; ++i) out[i + 2] = grown_value(level, false, cell(grow[i]), cell(ini[i]), up[i + 2], 9999);
    return out;
}

void GameSystem::update(Engine& engine) {
    if (registered_) return;
    ram_.manager = this;
    engine.messenger().add_row("GAMERAM", std::nullopt, -1, &ram_);
    registered_ = true;
}

bool GameSystem::command(Engine& engine, Ram&, std::string_view command, Packet& packet) {
    // ADDHP / ADDMP (GAMERAM cases 9 / 10, 80371BCC / 80371D08):
    // I<player>/I<amount>[/I<effect>[/I<absolute>]]. ADDHP (8036F220) adds the
    // amount within the maximum, nothing at 0 HP; an absolute value other
    // than -1 sets the HP within 0..maximum even at 0 HP (the pit respawn sets
    // 1). The effect flag plays the heal effect on the player whenever
    // 8036F220 did not refuse (relative at 0 HP); ADDMP plays it only with
    // exactly three ints.
    if (command == "ADDHP" || command == "ADDMP") {
        const int slot = packet.integer(0, 0);
        const int amount = packet.integer(1, 0);
        const bool effect = packet.ints.size() >= 3 && packet.integer(2, 0) != 0;
        bool applied = true;
        if (command == "ADDHP") {
            const int absolute = packet.ints.size() >= 4 ? packet.integer(3, -1) : -1;
            auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
            if (absolute != -1) {
                player.hp = std::clamp(absolute, 0, player.max_hp);
                if (auto* models = engine.get<ModelCtrlManager>())
                    if (auto* model = models->find(player.digimon, slot)) model->hp = player.hp;
                Packet percent, value;
                percent.floats.push_back(100.0f * float(player.hp) / float(std::max(player.max_hp, 1)));
                value.ints.push_back(player.hp);
                engine.messenger().send(engine, "METERCTRL", "hpmt", slot, "SETPERCENT", percent);
                engine.messenger().send(engine, "NUMBERCTRL", "hpnm", slot, "SETVALUE", value);
            } else {
                applied = player.hp != 0;
                change_hp(engine, slot, amount);
            }
        } else {
            change_mp(engine, slot, amount);
        }
        // The heal effect is posted (0x80371CFC / 0x80371E04): sent at once it
        // would be lost to a sequence the player selects in the same frame.
        if (applied && effect && (command == "ADDHP" || packet.ints.size() == 3))
            post_effect(engine, "PLAYER", players[std::size_t(std::clamp(slot, 0, 3))].digimon, slot, "x_250healhit00",
                        "SQ_HEALEFONLY");
        return true;
    }
    if (command == "RECALCPLAYER") {
        recalc_player(engine, packet.integer(0, 0));
        return true;
    }
    if (command == "CTRLITEM") {
        // 80372054: S<folder>/S<command>/S<table>, I<player>/I<a>/I<b>/I<c>.
        // DEVICE, DISK and FLAG answer a bool (80306964), BIT GET clears the
        // ints and answers the value (80306AB8, 80306A40 at 0x80372950), BIT
        // SET / ADD answer nothing.
        auto text = [&](std::size_t i) { return packet.string(i); };
        const int result = control_item(text(0), text(1), packet.integer(0, 0), text(2), packet.integer(1, 0), packet.integer(2, 0),
                                        packet.integer(3, 0));
        if (text(0) == "BIT") {
            if (text(1) == "GET") {
                packet.ints.clear();
                packet.ints.push_back(result);
            }
        } else {
            packet.bools.push_back(std::uint8_t(result));
        }
        return true;
    }
    return false;
}

} // namespace dw4::engine
