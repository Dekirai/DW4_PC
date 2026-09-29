#include "dw4/engine/managers.hpp"

#include <algorithm>
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

// P2EISCRITICAL (80372C00), weapon path: CHIMEIRITSU > random % 200.
bool player_critical(const GameSystem::Stats& stats, int kind) {
    if (kind / 10000 == 4) return false; // magic path (80371524 table lookup) not reconstructed
    return stats.chimeiritsu > int(random() % 200u);
}

// P2EDAMAGE (80372EC4), weapon path.
FightResult player_damage(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind, bool critical,
                          int defense_down) {
    FightResult result;
    if (kind / 10000 == 4) return result; // magic damage (80371524) not reconstructed
    auto* db = engine.get<DatabaseManager>();
    if (!db) return result;
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
        // Four-way variance: 95.0 / 97.5 / 100 / 102.5 percent plus up to 2.5 percent.
        const std::uint32_t pick = random() & 3u;
        const std::uint32_t span = std::uint32_t(damage) * 25u;
        const int extra = span ? int(random() % span) : 0;
        static const int percent[] = {950, 975, 1000, 1025};
        damage = (damage * percent[pick] + extra) / 1000;
        if (pick == 3) result.flag = 1;
    }
    result.damage = std::clamp(damage, 0, 99999);
    return result;
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
    auto& player = players[std::size_t(std::clamp(slot, 0, 3))];
    auto& stats = player.stats;
    auto* db = engine.get<DatabaseManager>();
    if (!db) return;
    stats.species = std::max(0, db->find_row("shinka", "MODEL_NAME", player.digimon));
    const int row = stats.species;
    const int level = stats.level;
    auto cell = [&](std::string_view table, int r, std::string_view column) { return db->number(table, r, column).value_or(0); };
    auto attribute = [&](const char* ini, const char* grow) {
        return grown_value(level, false, cell("shinka", row, grow), cell("shinka", row, ini), 0, 9999);
    };
    // First power-socket weapon: the species' INI_WEAPON1 (GAMESTART initial weapons).
    stats.weapon_record = cell("shinka", row, "INI_WEAPON1");
    // The power socket holds INI_WEAPON1..3; their models are the files the
    // next stage's boot.lua loads through SYSgetWeaponName.
    static const char* initial[] = {"INI_WEAPON1", "INI_WEAPON2", "INI_WEAPON3"};
    for (std::size_t i = 0; i < 3; ++i)
        if (player.weapons[i].empty()) player.weapons[i] = db->text("weapon", cell("shinka", row, initial[i]), "MODEL");
    const int weapon = stats.weapon_record;
    stats.weapon_model = db->text("weapon", weapon, "MODEL");
    stats.weapon_skill_no = cell("weapon", weapon, "SKILL");
    stats.meichuritsu = cell("weapon", weapon, "MEICHURITSU");
    stats.chimeiritsu = cell("weapon", weapon, "CHIMEIRITSU");
    stats.attack_delay = cell("weapon", weapon, "DELAY");
    auto clamp_stat = [](int v) { return std::clamp(v, 1, 9999); };
    stats.tsuyosa = clamp_stat(attribute("STR_INI", "STR_GROW") + cell("weapon", weapon, "TSUYOSA"));
    stats.mamori = clamp_stat(attribute("AC_INI", "AC_GROW"));
    stats.kashikosa = clamp_stat(attribute("INT_INI", "INT_GROW") + cell("weapon", weapon, "KASHIKOSA"));
    stats.seishin = clamp_stat(attribute("MND_INI", "MND_GROW") + cell("weapon", weapon, "SEISHIN"));
    stats.subayasa = clamp_stat(attribute("DEX_INI", "DEX_GROW") + cell("weapon", weapon, "SUBAYASA"));
    player.max_hp = grown_value(level, true, cell("shinka", row, "MHP_GROW"), cell("shinka", row, "MHP_INI"), 0, 99999);
    player.max_mp = grown_value(level, true, cell("shinka", row, "MMP_GROW"), cell("shinka", row, "MMP_INI"), 0, 99999);
    if (!stats.ready || player.hp > player.max_hp) player.hp = player.max_hp;
    if (!stats.ready || player.mp > player.max_mp) player.mp = player.max_mp;
    stats.ready = true;
}

} // namespace dw4::engine
