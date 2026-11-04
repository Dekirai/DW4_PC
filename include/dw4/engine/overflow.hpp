#pragma once

// Data Overflow (docs/SURVIVAL.md): the rules of a survival run's engine.
//
// A run lives in an Engine of its own. The host makes that engine and hands
// it an OverflowRules (Engine::set_overflow_rules) before its first
// request_stage; the director drives the run through the Director interface
// below. Every engine hook of SURVIVAL.md 13.4 acts only while the engine
// has rules: with the pointer null (every story, test, replay and online
// engine) each hook is one untaken branch, so det.sh, the r25c campaign
// reference and every save state stay as they are. The rules are no state of
// the original game: they are in no snapshot and not in the fingerprint.
//
// The hooks (SURVIVAL.md 13.4) and where they act:
//   H2  donors          Engine loader state 3: the run's explicit family list
//                       (Engine::load_overflow_donors, DonorRequest)
//   H3  exits           Engine::request_stage (only the director's own field
//                       change passes), the soft reset (soft_reset_allowed),
//                       the console reset (set_reset_enabled) and the game
//                       over script end_g_000bcaps.lua: the director gets
//                       Director::run_end instead
//   H4  rule values     lua_host BLTisHitCheck / BLTgetDamageValue /
//                       BLPgetDamageValue; combat skill_up, recalc_player
//                       (NEXTEXP), experience_up, apply_level_ups; items
//                       pickup radius; model_ctrl MOVE (the walk speed)
//   H5  notifications   next to the observe-only port events: enemy
//                       defeated, level up, player defeated, item taken
//   H6  port hitbox     ModelCtrlManager::port_hitbox: an Art's boxes on the
//                       player for their frames, aimed, without a sequence
//   H8  run player      recalc_player's technique level lock; digivolve()
//   H9  director step   the end of every simulated Engine::step
//   H10 native scripts  Engine::run_script asks script() first: the player
//                       hit script hit_p_<form>.lua (the run's player hit
//                       rule, 4.8), the Violet Ring's first.lua (the native
//                       field start) and end_g_000bcaps.lua
//
// Everything here is deterministic: no host clock, no draw of its own (the
// engine's hit, crit and damage rolls still use the system generator).

#include "dw4/engine/math.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dw4::engine {

class Engine;
struct ModelScriptContext;

namespace overflow {

// What the native scripts do (T10): `original` answers hit_p_<form>.lua
// exactly as the script does and leaves every other hook inert (the T10
// comparison against the original scripts); `run` is the run's rules.
enum class RuleSet : std::uint8_t { original, run };

// What an enemy is to the run (the director marks it when it generates it).
enum class EnemyClass : std::uint8_t { normal, corrupted, guardian };

// Why a run engine wants to end (H3); the director decides what follows.
enum class EndReason : std::uint8_t {
    stage_exit,      // a stage change the director did not ask for (a gate, the return gate matz01, a gate disc)
    game_over,       // end_g_000bcaps.lua: no live player left (the original's game over)
    player_defeated, // the player hit rule saw 0 HP and the director did not revive the Digimon
};
struct RunEnd {
    EndReason reason{};
    std::string target; // stage_exit: the stage that was asked for
};

// One damaging (or missed) enemy hit on the player under the run's rules
// (Director::player_hit; the run log of 13.5).
struct HitTaken {
    int slot{};
    std::string shooter;  // the attacking model (its family)
    int shooter_index = -1;
    int kind{};           // the bullet kind (7xxxx: the eneatk row is kind % 10000)
    EnemyClass enemy{};
    int raw{};            // the damage before the run's damage-taken rule
    int damage{};         // what the player lost
    bool clamped{};       // the 35 % safety clamp cut it (logged; Normal must never)
    bool guarded{};       // classic buttons: a guarded hit at half damage
    bool missed{};        // a core's BOUGYORITSU or the evasion: no damage
    int hp_after{}, max_hp{};
};

// H2: what the run engine loads beside its arena (after boot.lua, before the
// infos register). All of it may come from any stage, the boss arenas
// included; there is no family limit.
struct DonorRequest {
    // Regular enemy families (the randomizer's family table names each
    // family's donor stage member): their six infos, the action starters and
    // effects aimed at them, their scripts without ded_*.lua, their SE banks.
    std::vector<std::string> families;
    // Further families from a named donor: {family, donor}, the donor a
    // stage ("dng101b2": its main member) or a separate package of a stage
    // ("stag0202/separate/e_skulgrey.igb", read like boot.lua's common
    // files). Never with their ded_*.lua (no story flag, scene or door).
    std::vector<std::pair<std::string, std::string>> guardians;
    // Stream entries (BGM, jingles) of other stages, by name: {stage, names}
    // from that stage's sound list (its /s member).
    std::vector<std::pair<std::string, std::vector<std::string>>> streams;
    bool empty() const { return families.empty() && guardians.empty() && streams.empty(); }
};

class OverflowRules;

// The director's side of the hooks (lane C: OverflowDirector). Every call
// comes from inside a step of the run engine, so a director may send and
// post messages (in a Messenger::PortOnly scope) and change the rules'
// values. Default implementations do nothing.
class Director {
public:
    virtual ~Director() = default;
    // H9: once per simulated step, after the managers.
    virtual void step(Engine& engine) { (void)engine; }
    // H10: the native field start ran the arena's own lines (the stage setup
    // of its first.lua without the boss); the director adds the run's own:
    // the phase music, its spawns, paused mats, stopped generators.
    virtual void field_started(Engine& engine) { (void)engine; }
    // H3: the engine wanted to end the run (once per run; see
    // OverflowRules::ended). Nothing has left the stage: the director shows
    // its Game Over picture, the results, or moves on.
    virtual void run_end(Engine& engine, const RunEnd& end) { (void)engine; (void)end; }
    // 4.8 defeat: the player reached 0 HP. True when the director revived it
    // (Raise: it set the HP and the MUTEKI itself); false ends the run.
    virtual bool revive(Engine& engine, int slot) { (void)engine; (void)slot; return false; }
    // H4 skill rule: a skill changed by a hit (before RECALCPLAYER). The
    // ladder re-equips here (equip_strongest_weapon among the forged
    // families); the rules then recalc, store and decide the panel.
    virtual void skill_changed(Engine& engine, int slot, int skill, int before, int after) {
        (void)engine; (void)slot; (void)skill; (void)before; (void)after;
    }
    // H5 notifications (deterministic, independent of the port events).
    virtual void enemy_defeated(Engine& engine, int killer, int record, std::string_view family, int index) {
        (void)engine; (void)killer; (void)record; (void)family; (void)index;
    }
    virtual void level_up(Engine& engine, int slot, int level) { (void)engine; (void)slot; (void)level; }
    virtual void item_taken(Engine& engine, int slot, int table, int record) { (void)engine; (void)slot; (void)table; (void)record; }
    virtual void player_hit(Engine& engine, const HitTaken& hit) { (void)engine; (void)hit; }
};

// Skill columns of GameSystem::Stats::skill (shinka BASE_SKILL, pcTech SKILL):
// 0 Blunt, 1 Slash, 2 Stab, 3 Bash, 4 Shot, 5 Crush, then the technique groups.
constexpr int kBlastSkill = 6, kHealSkill = 7, kForceSkill = 8;

// The run's NEXTEXP (4.5): the EXP that reaches level + 1, the sum over the
// levels l = 1..level of base + linear (l - 1) + square (l - 1)^2 (initial
// 80 / 50 / 6: 80 for level 2, about 1,000 for level 10 to 11).
int run_next_exp(int level, int base, int linear, int square);
// The run's technique level lock (H8, 6.4): ceil(lock * num / den).
int run_technique_level(int lock, int num, int den);
// The level-up rule (4.5): the new maximum times the old fraction plus
// `heal` of the new maximum (a full bar stays full), at most the maximum.
int run_level_hp(int old_value, int old_max, int new_max, double heal);

class OverflowRules {
public:
    explicit OverflowRules(RuleSet set = RuleSet::run) : rule_set(set) {}
    virtual ~OverflowRules() = default;
    OverflowRules(const OverflowRules&) = delete;
    OverflowRules& operator=(const OverflowRules&) = delete;

    RuleSet rule_set;
    bool run() const { return rule_set == RuleSet::run; }
    // Not owned: the host keeps the director alive while the engine lives.
    Director* director = nullptr;

    // ---- Values (SURVIVAL.md's initial values; the director changes them
    // as the run goes: the time factor every minute, caps by its offers).
    struct Damage {
        // Dealt (player to enemy, BLTgetDamageValue after the original formula).
        float art_factor = 2.5f;                    // every port hitbox (an Art, 6.3)
        std::map<std::int32_t, float> tag_factor;   // per port hitbox tag, on top of art_factor
        std::map<std::int32_t, float> technique_factor; // per pcTech family (bullet kind % 1000)
        std::array<float, 4> element_factor{1, 1, 1, 1}; // fire, ice, thunder, dark (the form's element: 1.15)
        float other_factor = 1.0f;                  // every other player hit (the classic A combo)
        bool arts_always_hit = true;                // BLTisHitCheck for port boxes
        // Taken (enemy to player, BLPgetDamageValue).
        float taken_factor = 1.0f;                  // time x tier (1 + 0.04 x minutes, Hard 1.2, ...)
        float corrupted_factor = 1.5f;
        float hit_clamp = 0.35f;                    // a non-Guardian hit takes at most this share of MaxHP
    } damage;
    struct Levels {
        bool run_curve = true;                      // NEXTEXP of run_next_exp instead of DW4's
        int curve_base = 80, curve_linear = 50, curve_square = 6;
        float exp_factor = 1.0f;                    // the tier's EXP factor
        bool keep_fractions = true;                 // no refill on a level-up (4.5)
        double level_heal = 0.03;                   // ... but this share of the new MaxHP
        int tech_lock_num = 2, tech_lock_den = 3;   // TECHn_LEVEL x 2/3, rounded up
    } levels;
    struct Skills {
        bool run_rule = true;                       // the skill rule of 6.2 instead of P2ESKILLUP's rolls
        int art_gain = 1, crit_gain = 1;            // per weapon (Art) hit, x the form's GROW / 100
        int blast_gain = 2;                         // per blast technique hit
        int support_gain = 12;                      // per heal or force cast (its hit on the player)
        // The run's caps (the class cap from Forge, the group caps from
        // Train); a skill at its cap stops.
        std::array<int, 9> cap{300, 300, 300, 300, 300, 300, 300, 300, 300};
        int panel_gap = 30;                         // at most one SQ_TECUP panel in this many frames
    } skills;
    struct PlayerHit {
        int muteki = 50;                            // frames after every damaging hit (4.8)
        int status_cap = 30;                        // stun / sleep / bind of normal enemies, frames
        int guard_percent = 50;                     // classic buttons: a guarded hit's share
    } player_hit;
    float pickup_radius = 8.0f;                     // instant pickups (items.cpp: the original's 8)
    float walk_factor = 1.0f;                       // the player's walk speed (Boost, traits)
    // The native field start opens the HUD after this many frames (the
    // original's first.lua waits 520 for the boss intro).
    int hud_delay = 30;
    DonorRequest donors;                            // H2, read at every stage load

    // ---- Enemy classes: the director marks Corrupted enemies and Guardians
    // when it generates them (normal is the default; a GENERATE of the same
    // RAM again should mark it again).
    void set_enemy_class(std::string_view family, int index, EnemyClass value);
    EnemyClass enemy_class(std::string_view family, int index) const;

    // ---- H3. The director's own stage change (the field change of 4.1,
    // O3): with the host's first request (the arena) the only
    // request_stage a run engine takes.
    void field_change(Engine& engine, std::string stage);
    // Engine::request_stage: true when the change may go on.
    bool stage_request(Engine& engine, std::string_view stage);
    // Reports the end to the director once; later ends are dropped until
    // reset_end() (a revived or continued run).
    void end_run(Engine& engine, RunEnd end);
    const std::optional<RunEnd>& ended() const { return ended_; }
    void reset_end() { ended_.reset(); }

    // ---- H4 (called by the engine; inert under the original rule set).
    bool always_hits(const ModelScriptContext& context) const;
    int dealt(Engine& engine, const ModelScriptContext& context, int damage, int attribute);
    int taken(Engine& engine, const ModelScriptContext& context, int slot, int max_hp, int damage);
    int next_exp(int level) const;
    int exp_gain(int exp) const;
    // apply_level_ups after the levels: the run's HP / MP instead of the refill.
    void level_up(Engine& engine, int slot, int old_hp, int old_max_hp, int old_mp, int old_max_mp);
    // P2ESKILLUP replaced (true: handled).
    bool skill_up(Engine& engine, int slot, int kind, int critical);
    int technique_level(int lock) const;

    // ---- H5 (forwarded to the director).
    void enemy_defeated(Engine& engine, int killer, int record, std::string_view family, int index);
    void level_reached(Engine& engine, int slot, int level);
    void item_taken(Engine& engine, int slot, int table, int record);

    // ---- H9.
    void step(Engine& engine);
    // ---- H10: true when the script was answered natively.
    bool script(Engine& engine, std::string_view name);

    // ---- The run's tallies (the run log of 13.5; never read by the engine).
    struct Tally {
        std::uint64_t steps{};             // run engine steps seen by step()
        std::uint64_t no_control_steps{};  // steps without control (4.8: <= 5 %)
        int hits_taken{}, misses{}, clamps{};
        double highest_hit_share{};        // of MaxHP, non-Guardians
        int panels{};                      // SQ_TECUP panels shown
        int panels_skipped{};              // panels the 30-frame gap or "nothing visible" left out
        // Damage dealt by source: a port hitbox tag (> 0) or -(bullet kind)
        // of every other player hit (techniques: -(4xxxx)).
        std::map<std::int32_t, std::int64_t> dealt_by;
        // The skill points kept below one (the GROW fractions of 6.2).
        std::array<double, 9> skill_rest{};
    } tally;

    // The frame the last SQ_TECUP panel showed (-1 none).
    std::int64_t last_panel_frame = -1;
    // The last damage-taken rule's result (taken()): the player hit handler
    // reports it with the hit (HitTaken).
    struct LastTaken {
        int raw{};
        bool clamped{};
    } last_taken;

private:
    std::map<std::pair<std::string, int>, EnemyClass, std::less<>> classes_;
    std::optional<RunEnd> ended_;
    std::string allowed_stage_;
    bool first_taken_{};
};

// H8 / 4.1 (O3): the run's digivolution. The run's level, EXP, skills and
// UPCNT go into the new form's BASE row, then BASE2CURRENT and RECALCPLAYER;
// HP and MP keep their fractions. The model change itself is a stage load
// (the field change), so call it before OverflowRules::field_change.
void digivolve(Engine& engine, int slot, int species);

// H10 natives (player_hit.cpp, field_start.cpp): exposed for the T10 tests.
// The player hit script of every form (one script in every stage, SURVIVAL.md
// 4.8); with `rules` under RuleSet::original it does exactly what the
// script does.
void native_player_hit(Engine& engine, OverflowRules& rules);
// The native field start of an arena (its first.lua without the boss lines);
// false when the stage has none.
bool native_field_start(Engine& engine, OverflowRules& rules);
// The stages with a native field start.
bool has_native_field_start(std::string_view stage);

// Helpers for the director (lane C).
// Whether a world point is in the picture of the frame's game camera (the
// box camera; docs/SURVIVAL_O1A.md S3): inside the projection's x range
// times `widen` (1 for 4:3, 4/3 for the 16:9 picture) and its y range, in
// front of the camera. Reads the camera only (no state changes).
bool on_screen(Engine& engine, const Vec3f& point, float widen = 4.0f / 3.0f);
// The MP a technique costs in a run (4.8, the user's answer 3): the heal
// group (pcTech SKILL 7: Heal Lt, Aura, Cure Lt / Cure, Raise) pcTech MP / 8,
// every other technique nothing.
int technique_mp_cost(Engine& engine, int technique_row);

} // namespace overflow
} // namespace dw4::engine
