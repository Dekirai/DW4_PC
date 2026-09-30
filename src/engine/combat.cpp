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

std::mt19937& rng() {
    static std::mt19937 generator{0x44573446u};
    return generator;
}
} // namespace

namespace fight {
std::uint32_t random() { return rng()(); }

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
// MEICHURITSU * (skill*20 + 6000 + SUBAYASA*30) / (enemy SUBAYASA * (100-down) / 100)
// is compared with a random value below 10000.
bool player_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind, int speed_down) {
    if (kind / 10000 == 4) return true;
    auto* db = engine.get<DatabaseManager>();
    const int enemy_speed = db ? db->number("enemy", enemy_record, "SUBAYASA").value_or(1) : 1;
    const int skill = stats.skill[std::size_t(std::clamp(stats.weapon_skill_no, 0, 8))];
    const int divisor = enemy_speed * (100 - speed_down) / 100;
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

FightResult enemy_damage(Engine& engine, const GameSystem::Stats& stats, int max_hp, int enemy_record, int kind) {
    FightResult result;
    const int attack = kind % 10000;
    const int type = database(engine, "eneatk", attack, "TYPE");
    const int power = database(engine, "eneatk", attack, "TSUYOSA");
    const int attribute = attribute_index(engine, attack);
    const int resist = attribute >= 0 ? stats.resist[std::size_t(attribute)] : 0;
    result.attribute = attribute;
    int damage = 0;
    if (type == 0) {
        // Physical: enemy TSUYOSA * attack power / player MAMORI.
        const int strength = database(engine, "enemy", enemy_record, "TSUYOSA");
        damage = stats.mamori != 0 ? std::clamp(strength * power / stats.mamori, 0, 99999) : 0;
        if (resist != 0 && !(resist / 5 > int(random() % 1000u))) {
            if (resist <= 1000) damage += damage * ((1000 - resist) / 20 + 50) / 100;
            else if (resist <= 5000) damage += damage * ((5000 - resist) / 80) / 100;
        }
        if (damage != 0) {
            const int enemy_critical = database(engine, "enemy", enemy_record, "CHIMEIRITSU");
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
} // namespace fight

void GameSystem::prepare_player(Engine& engine, int slot) {
    if (!players[std::size_t(std::clamp(slot, 0, 3))].stats.ready) create_player(engine, slot);
    else recalc_player(engine, slot);
}

void GameSystem::create_player(Engine& engine, int slot) {
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
    player.hp = 0; // RECALCPLAYER refills HP and MP from 0
    recalc_player(engine, slot);
    stats.ready = true;
}

void GameSystem::recalc_player(Engine& engine, int slot) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto& stats = player.stats;
    auto* db = engine.get<DatabaseManager>();
    if (!db) return;
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
}

void GameSystem::change_hp(Engine& engine, int slot, int delta) {
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    if (player.hp == 0) return;
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
        if (slot == killer) killer_gain = exp;
        stats.exp += exp;
        bool leveled = false;
        while (stats.level < 999 && stats.next_exp <= stats.exp) {
            ++stats.level;
            prepare_player(engine, slot);
            leveled = true;
        }
        if (!leveled) continue;
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
        auto send = [&](const char* category, const char* target, const char* command, Packet p) {
            engine.messenger().send(engine, category, target, slot, command, p);
        };
        Packet effect;
        effect.strings.emplace_back("i_getitempnl01");
        effect.strings.emplace_back("SQ_LVUP");
        send("MODELCTRL", name.c_str(), "SETEFFECT", effect);
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
    }
    return killer_gain;
}

} // namespace dw4::engine
