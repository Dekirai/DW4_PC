// Data Overflow (docs/SURVIVAL.md 13.4): the rules behind the run engine's
// hooks (dw4/engine/overflow.hpp).
#include "dw4/engine/overflow.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>

namespace dw4::engine::overflow {
namespace {

// 804EB13C: the shinka growth column of each weapon skill.
constexpr const char* kSkillGrow[6] = {"BLUNT_GROW", "SLASH_GROW", "STAB_GROW", "BASH_GROW", "SHOT_GROW", "CRUSH_GROW"};

// The WAZALIST row of a technique bullet's family (kind % 1000, its TECHn
// base row), as combat.cpp's technique_row.
int technique_row(const GameSystem::PlayerSlot& player, int kind) {
    const int family = kind % 1000;
    for (const int row : player.wazalist)
        if (row >= family && row < family + 5) return row;
    return family;
}

// Sequences in which the player has no control (4.8, the frames the bot runs
// count): the damage reactions, the knock-down and the control statuses.
bool reaction_sequence(std::string_view name) {
    static constexpr std::string_view kNames[] = {"SQ_SDMG", "SQ_MDMG", "SQ_LDMG", "SQ_DDMG", "SQ_DEAD",
                                                  "SQ_SLEP", "SQ_STUN", "SQ_GUARDR", "SQ_CMBK", "SQ_CMBK02"};
    return std::find(std::begin(kNames), std::end(kNames), name) != std::end(kNames);
}

} // namespace

int run_next_exp(int level, int base, int linear, int square) {
    if (level <= 0) return 0;
    // sum over k = 0 .. level - 1 of base + linear k + square k^2
    const std::int64_t n = level;
    const std::int64_t total = std::int64_t(base) * n + std::int64_t(linear) * n * (n - 1) / 2 +
                               std::int64_t(square) * (n - 1) * n * (2 * n - 1) / 6;
    return int(std::clamp<std::int64_t>(total, 0, INT_MAX));
}

int run_technique_level(int lock, int num, int den) {
    if (lock <= 0 || num <= 0 || den <= 0) return lock;
    return int((std::int64_t(lock) * num + den - 1) / den);
}

int run_level_hp(int old_value, int old_max, int new_max, double heal) {
    if (new_max <= 0) return 0;
    std::int64_t value = old_max > 0 ? std::int64_t(new_max) * std::clamp(old_value, 0, old_max) / old_max : new_max;
    if (heal > 0) value += std::int64_t(std::floor(heal * double(new_max)));
    value = std::min<std::int64_t>(value, new_max);
    if (old_value > 0) value = std::max<std::int64_t>(value, 1);
    return int(value);
}

RunPoison run_poison(int tick, int ticks, int max_hp, double factor, int max_ticks, double share) {
    RunPoison out;
    if (tick <= 0 || ticks <= 0) return out;
    const int total = std::max(1, int(std::floor(double(std::max(max_hp, 1)) * share)));
    out.tick = std::clamp(int(double(tick) * factor), 1, total);
    out.ticks = max_ticks > 0 ? std::min(ticks, max_ticks) : ticks;
    out.ticks = std::max(1, std::min(out.ticks, total / out.tick));
    return out;
}

// ---------------------------------------------------------------------------
// Enemy classes
// ---------------------------------------------------------------------------
void OverflowRules::set_enemy_class(std::string_view family, int index, EnemyClass value) {
    if (value == EnemyClass::normal) classes_.erase({std::string(family), index});
    else classes_[{std::string(family), index}] = value;
}

EnemyClass OverflowRules::enemy_class(std::string_view family, int index) const {
    if (classes_.empty()) return EnemyClass::normal;
    const auto found = classes_.find({std::string(family), index});
    return found == classes_.end() ? EnemyClass::normal : found->second;
}

// ---------------------------------------------------------------------------
// H3: exits
// ---------------------------------------------------------------------------
void OverflowRules::field_change(Engine& engine, std::string stage) {
    std::transform(stage.begin(), stage.end(), stage.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    allowed_stage_ = stage;
    engine.request_stage(std::move(stage));
}

bool OverflowRules::stage_request(Engine& engine, std::string_view stage) {
    // The host's first request (the arena) and the director's own change.
    if (!first_taken_ || (!allowed_stage_.empty() && stage == allowed_stage_)) {
        first_taken_ = true;
        allowed_stage_.clear();
        return true;
    }
    engine.log("overflow: stage change to " + std::string(stage) + " refused (run end)");
    end_run(engine, {EndReason::stage_exit, std::string(stage)});
    return false;
}

void OverflowRules::end_run(Engine& engine, RunEnd end) {
    if (ended_) return;
    static const char* const names[] = {"stage exit", "game over", "player defeated"};
    engine.log(std::string("overflow: run end (") + names[int(end.reason)] + (end.target.empty() ? "" : " " + end.target) + ")");
    ended_ = std::move(end);
    if (director) director->run_end(engine, *ended_);
}

// ---------------------------------------------------------------------------
// H4: rule values
// ---------------------------------------------------------------------------
bool OverflowRules::always_hits(const ModelScriptContext& context) const { return damage.arts_always_hit && context.port_tag != 0; }

int OverflowRules::dealt(Engine& engine, const ModelScriptContext& context, int value, int attribute) {
    double factor = 1.0;
    std::int32_t source;
    if (context.port_tag != 0) {
        factor = damage.art_factor;
        if (const auto found = damage.tag_factor.find(context.port_tag); found != damage.tag_factor.end()) factor *= found->second;
        source = context.port_tag;
    } else if (context.bullet_kind / 10000 == 4) {
        if (const auto found = damage.technique_factor.find(context.bullet_kind % 1000); found != damage.technique_factor.end())
            factor = found->second;
        source = -context.bullet_kind;
    } else {
        factor = damage.other_factor;
        source = -context.bullet_kind;
    }
    if (attribute >= 0 && attribute < 4) factor *= damage.element_factor[std::size_t(attribute)];
    const int out = std::clamp(int(double(value) * factor), 1, 99999);
    tally.dealt_by[source] += out;
    if (director) director->damage_dealt(engine, context.name, context.index, source, out);
    return out;
}

int OverflowRules::taken(Engine& engine, const ModelScriptContext& context, int slot, int max_hp, int value) {
    (void)slot;
    const auto kind = enemy_class(context.shooter_name, context.shooter);
    double factor = damage.taken_factor;
    if (kind == EnemyClass::corrupted) factor *= damage.corrupted_factor;
    if (const auto found = damage.family_factor.find(context.shooter_name); found != damage.family_factor.end()) factor *= found->second;
    // Near or far: the attacker's distance on the ground when the hit lands.
    last_taken.range = 1.0;
    if (damage.near_factor != 1.0f || damage.far_factor != 1.0f)
        if (auto* models = engine.get<ModelCtrlManager>()) {
            const auto* shooter = models->find(context.shooter_name, context.shooter);
            const auto* self = models->find(context.name, context.index);
            if (shooter && self && shooter->node && self->node) {
                const float dx = shooter->node->matrix(3, 0) - self->node->matrix(3, 0);
                const float dz = shooter->node->matrix(3, 2) - self->node->matrix(3, 2);
                last_taken.range = std::hypot(dx, dz) <= damage.near_distance ? damage.near_factor : damage.far_factor;
            }
        }
    factor *= last_taken.range;
    int out = std::clamp(int(double(value) * factor), 1, 99999);
    last_taken.raw = out;
    last_taken.clamped = false;
    if (kind != EnemyClass::guardian && max_hp > 0) {
        const int cap = std::max(1, int(std::lround(double(max_hp) * double(damage.hit_clamp))));
        if (out > cap) {
            engine.log("overflow: a hit of " + std::to_string(out) + " by " + context.shooter_name + " clamped to " + std::to_string(cap));
            out = cap;
            last_taken.clamped = true;
            ++tally.clamps;
        }
        tally.highest_hit_share = std::max(tally.highest_hit_share, double(out) / double(max_hp));
    }
    return out;
}

RunPoison OverflowRules::poison(const ModelScriptContext& context, int max_hp, int tick, int ticks) const {
    const auto kind = enemy_class(context.shooter_name, context.shooter);
    if (kind == EnemyClass::guardian) return {tick, ticks};
    double factor = damage.taken_factor;
    if (kind == EnemyClass::corrupted) factor *= damage.corrupted_factor;
    if (const auto found = damage.family_factor.find(context.shooter_name); found != damage.family_factor.end()) factor *= found->second;
    factor *= last_taken.range; // (the hit's near or far factor: taken() came first)
    return run_poison(tick, ticks, max_hp, factor, player_hit.poison_ticks, player_hit.poison_share);
}

int OverflowRules::next_exp(int level) const {
    if (!levels.run_curve) return (level + 140) * level * level + level * 200;
    return run_next_exp(level, levels.curve_base, levels.curve_linear, levels.curve_square);
}

int OverflowRules::exp_gain(int exp) const {
    if (exp <= 0 || levels.exp_factor == 1.0f) return exp;
    return std::max(1, int(double(exp) * levels.exp_factor));
}

void OverflowRules::level_up(Engine& engine, int slot, int old_hp, int old_max_hp, int old_mp, int old_max_mp) {
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    auto& player = sys->players[std::size_t(std::clamp(slot, 0, 3))];
    if (!levels.keep_fractions) {
        player.hp = player.max_hp;
        player.mp = player.max_mp;
        return;
    }
    player.hp = run_level_hp(old_hp, old_max_hp, player.max_hp, levels.level_heal);
    player.mp = run_level_hp(old_mp, old_max_mp, player.max_mp, 0.0);
}

bool OverflowRules::skill_up(Engine& engine, int slot, int kind, int critical) {
    if (!skills.run_rule) return false;
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !db) return true;
    auto& player = sys->players[std::size_t(std::clamp(slot, 0, 3))];
    auto& stats = player.stats;
    const int group = kind / 10000;
    const bool technique = group == 4 || group == 8 || group == 9;
    int type;
    double gain;
    if (technique) {
        type = std::clamp(db->number("pcTech", technique_row(player, kind), "SKILL").value_or(0), 0, 8);
        // A technique's hit on an enemy (kind 4xxxx) or a support cast on
        // the player (8xxxx / 9xxxx, its hit script's BLTcalcSkillUp).
        gain = group == 4 ? skills.blast_gain : skills.support_gain;
    } else {
        type = std::clamp(stats.weapon_skill_no, 0, 8);
        const int grow = type < 6 ? db->number("shinka", stats.species, kSkillGrow[type]).value_or(100) : 100;
        gain = double(skills.art_gain + (critical == 1 ? skills.crit_gain : 0)) * double(grow) / 100.0;
    }
    const int cap = skills.cap[std::size_t(type)];
    const int before = stats.skill[std::size_t(type)];
    if (before >= cap) return true; // (at its cap the skill stops; nothing else happens)
    auto& rest = tally.skill_rest[std::size_t(type)];
    const double total = rest + gain;
    const int whole = int(std::floor(total));
    rest = total - double(whole);
    if (whole <= 0) return true;
    const int after = std::min(cap, before + whole);
    if (after >= cap) rest = 0;
    // What the player sees: the techniques and their ranks, the weapon.
    const auto techniques_before = player.wazalist;
    const int weapon_before = player.power_socket.empty() ? -1 : player.power_socket[0];
    const int record_before = stats.weapon_record;
    stats.skill[std::size_t(type)] = after;
    if (director) director->skill_changed(engine, slot, type, before, after);
    sys->recalc_player(engine, slot);
    sys->store_species(slot);
    const bool visible = player.wazalist != techniques_before || stats.weapon_record != record_before ||
                         (!player.power_socket.empty() && player.power_socket[0] != weapon_before);
    if (!visible) return true;
    const auto frame = std::int64_t(engine.frame());
    if (last_panel_frame >= 0 && frame - last_panel_frame < skills.panel_gap) {
        ++tally.panels_skipped;
        return true;
    }
    last_panel_frame = frame;
    ++tally.panels;
    // The skill up panel over the player (i_getitempnl01 SQ_TECUP), posted as the original's (80373B40).
    post_effect(engine, "MODELCTRL", player.digimon, slot, "i_getitempnl01", "SQ_TECUP");
    return true;
}

int OverflowRules::technique_level(int lock) const { return run_technique_level(lock, levels.tech_lock_num, levels.tech_lock_den); }

// ---------------------------------------------------------------------------
// H5
// ---------------------------------------------------------------------------
void OverflowRules::enemy_defeated(Engine& engine, int killer, int record, std::string_view family, int index) {
    if (director) director->enemy_defeated(engine, killer, record, family, index);
}
void OverflowRules::level_reached(Engine& engine, int slot, int level) {
    if (director) director->level_up(engine, slot, level);
}
void OverflowRules::item_taken(Engine& engine, int slot, int table, int record) {
    if (director) director->item_taken(engine, slot, table, record);
}

// ---------------------------------------------------------------------------
// H9 / H10
// ---------------------------------------------------------------------------
void OverflowRules::step(Engine& engine) {
    ++tally.steps;
    if (auto* models = engine.get<ModelCtrlManager>())
        if (const auto* player = models->player(0); player && player->generated)
            if (!player->control_enabled || reaction_sequence(player->seq_name)) ++tally.no_control_steps;
    if (director) director->step(engine);
}

bool OverflowRules::script(Engine& engine, std::string_view name) {
    // The player hit script of every form (one script in every stage).
    if (name.starts_with("hit_p_") && name.ends_with(".lua")) {
        native_player_hit(engine, *this);
        return true;
    }
    if (!run()) return false;
    if (name == "first.lua" && has_native_field_start(engine.stage_name())) return native_field_start(engine, *this);
    // H2 never loads a donor family's ded_*.lua (no story flag, scene or
    // door); MRδ04 and MRε05 still call theirs (their donor stages are boss
    // stages): answered with nothing.
    if (name.starts_with("ded_e_") && name.ends_with(".lua")) return true;
    if (name == "end_g_000bcaps.lua") {
        // The original's game over (no live player left): the run ends.
        end_run(engine, {EndReason::game_over, {}});
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// H8: digivolution (4.1, O3)
// ---------------------------------------------------------------------------
void digivolve(Engine& engine, int slot, int species) {
    auto* sys = engine.get<GameSystem>();
    if (!sys || species < 0 || species >= 16) return;
    auto& player = sys->players[std::size_t(std::clamp(slot, 0, 3))];
    const int old_hp = player.hp, old_max_hp = player.max_hp, old_mp = player.mp, old_max_mp = player.max_mp;
    const auto s = std::size_t(species);
    player.base_level[s] = player.stats.level;
    player.base_exp[s] = player.stats.exp;
    player.base_skill[s] = player.stats.skill;
    player.base_upcnt[s] = player.upcnt;
    player.base_isuse[species] = 1;
    sys->load_species(engine, slot, species);
    sys->recalc_player(engine, slot);
    player.hp = run_level_hp(old_hp, old_max_hp, player.max_hp, 0.0);
    player.mp = run_level_hp(old_mp, old_max_mp, player.max_mp, 0.0);
    sys->store_species(slot);
}

// ---------------------------------------------------------------------------
// Director helpers
// ---------------------------------------------------------------------------
bool on_screen(Engine& engine, const Vec3f& point, float widen) {
    auto* cameras = engine.get<CameraManager>();
    if (!cameras) return false;
    FrameCamera camera;
    cameras->frame_camera(engine, camera);
    const Mat4 vp = camera.view * camera.projection;
    const float x = point.x * vp(0, 0) + point.y * vp(1, 0) + point.z * vp(2, 0) + vp(3, 0);
    const float y = point.x * vp(0, 1) + point.y * vp(1, 1) + point.z * vp(2, 1) + vp(3, 1);
    const float w = point.x * vp(0, 3) + point.y * vp(1, 3) + point.z * vp(2, 3) + vp(3, 3);
    if (w <= 1e-4f) return false;
    return std::fabs(x / w) <= widen && std::fabs(y / w) <= 1.0f;
}

int technique_mp_cost(Engine& engine, int technique_row) {
    auto* db = engine.get<DatabaseManager>();
    if (!db || db->number("pcTech", technique_row, "SKILL").value_or(-1) != kHealSkill) return 0;
    return std::max(0, db->number("pcTech", technique_row, "MP").value_or(0)) / 8;
}

} // namespace dw4::engine::overflow
