#pragma once

// Data Overflow (docs/SURVIVAL.md 13.5, round O1a lane C): the director of a
// run's engine. It drives the run through the engine hooks of
// dw4/engine/overflow.hpp (Director) and the engine's own messenger
// commands, in a Messenger::PortOnly scope:
//   - the clock (the steps the run is not frozen) and the phases of 4.2 on
//     the Violet Ring (O1a: no Guardians; phase 4 goes on to 25:00, the
//     finale's five minutes without LordKnightmon, and the run completes);
//   - the spawner of 4.3: the phase pools with their reserves, the spawn
//     rings (closer than S1's 40-60, docs/OVERFLOW_DIRECTOR.md 3), surges,
//     Corrupted enemies, recycling and the encirclements by reuse;
//   - the Art (6.3: Cleaver for Agumon, Blade Arc for Veemon, aimed port
//     hitboxes from the form's own SQ_ATCK1 boxes) and the weapon ladder;
//   - the techniques Fire, Cutter, Bolt, Impact and Heal Lt (6.4) on
//     cooldowns, MP for the heal only;
//   - the run's skill caps (6.2), the level-up offers Forge / Train / Plug
//     (4.6) with the freeze of 13.5, the pause, Retire and the results;
//   - the drops of Corrupted enemies (a Plug, an HP Disk alpha);
//   - the run log (13.5, 14.2's numbers) and the kite bot (14.1).
//
// Everything is deterministic: the director has its own seeded generator
// (splitmix64) for its draws and reads no clock; the engine's own rolls use
// the system generator the host reseeded for the run. The host talks to it
// with commands (offers, pause, Retire) that take effect in the next step.

#include "dw4/engine/math.hpp"
#include "dw4/engine/overflow.hpp"
#include "dw4/pad.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::engine::overflow {

// ---------------------------------------------------------------------------
// The design tables (SURVIVAL.md 4-6 as code; src/engine/overflow/tables.inc)
// ---------------------------------------------------------------------------
struct FamilyRow {
    const char* family;   // the model (e_goburi)
    const char* name;     // the run's English name (9.7)
    int record;           // its Normal row in the enemy table (4.2)
    int phase;            // the phase whose families it belongs to (1-4)
    bool ranged;          // fights from range (S1): left out of the close-in target of 14.2
    int ring;             // where it spawns: 0 the 32-46 ring, 1 26-36 (slow walkers, S1), 2 20-25 (never walks: Kokatorimon; the slowest)
    float damage;         // its own damage factor (14.3)
    float weight;         // its spawn weight on top of the pool's (the ranged families 0.5)
};
const std::vector<FamilyRow>& family_table();
const FamilyRow* family_row(std::string_view family);
// The pool of a phase (1-4): the phase's families weigh `own`, every earlier
// phase's `earlier` (4.2).
struct PoolEntry {
    const FamilyRow* row;
    float weight;
};
std::vector<PoolEntry> phase_pool(int phase, float own = 3.0f, float earlier = 0.5f);
// The phase music (4.2, 11) and the stage whose sound list has it (H2).
struct PhaseMusic {
    const char* name;
    const char* stage;
};
const std::array<PhaseMusic, 4>& phase_music();

// The run's numbers (initial values; SURVIVAL.md 4.3-4.5, 14.2).
struct Tuning {
    // 4.3: A(t), the living count the spawner tops up to, by knots at 0:00,
    // 5:00, 10:00, 15:00 and 20:00 (linear between them), at most alive_cap.
    // (4.3 and the review round: 14 + 3.2 a minute; the balance round of
    // 05.11: 10 at the start, a newcomer was surrounded in two minutes.)
    std::array<float, 5> alive_at{10.0f, 24.0f, 40.0f, 54.0f, 60.0f};
    int alive_cap = 60;
    int spawn_every = 10, spawns_per_tick = 2;  // at most 2 spawns every 10 frames
    // ... and at most refill_at spawns a second (knots as alive_at): the
    // enemies a fast Art defeats come back no faster (a lull after a group).
    // (The review round: 12, unlimited; the balance round: 3 a second at the start.)
    std::array<float, 5> refill_at{3.0f, 5.0f, 8.0f, 12.0f, 12.0f};
    int surge_every = 90 * 60, surge_length = 20 * 60, surge_extra = 12;
    int surge_first = 180 * 60;                  // the first surge (run steps; the review round: 1:30)
    // A spawn never closes the player's last way out before this run step:
    // of the eight 45-degree sectors round the player, the ones with a living
    // enemy within gap_radius are closed, and a spawn point that would leave
    // no two neighbouring sectors open is drawn again (the user: "no gap to
    // run"). The encirclements keep their ring.
    int keep_gap_until = 600 * 60;               // (phases 1 and 2)
    float gap_radius = 40.0f;
    std::array<int, 3> encircle_at{450 * 60, 750 * 60, 1050 * 60}; // 7:30, 12:30, 17:30 (run steps)
    int encircle_count = 16;
    float encircle_ring = 45.0f;
    float ring_min = 32.0f, ring_max = 46.0f;    // S1 proposed 40-60; T6 wants them closer (14.2: within 30 in 8 s)
    float slow_min = 26.0f, slow_max = 36.0f;    // slow walkers (S1: under 0.1 units a frame from 50)
    float near_min = 20.0f, near_max = 25.0f;    // families that never walk
    float ahead_share = 0.7f;                    // spawns from the side the player walks towards
    // ... beside its way, not on it: between these angles off the heading
    // (degrees), so a walking player passes them within the Art's reach
    // instead of running into them.
    float ahead_angle_min = 40.0f, ahead_angle_max = 70.0f;
    float visible_min = 40.0f;                   // a spawn in the picture comes at least this far away (SQ_ENTR)
    float recycle_far = 140.0f;  int recycle_far_steps = 5 * 60;
    float idle_far = 30.0f;      int idle_steps = 6 * 60;
    // 4.3: an enemy that has not come closer for behind_steps while farther
    // than behind_far is left behind by a moving player: its RAM comes again
    // ahead of the player (the ranged families stay where they shoot from).
    float behind_far = 40.0f;
    int behind_steps = 4 * 60;
    int encircle_per_step = 4;                   // an encirclement places this many a step (T8)
    // The Art's reach standing (6.3: at least 20; the balance round of 05.11:
    // 14, so a Digimon that never walks is reached by the walkers again; 20
    // from 30 % of the stick, the review round's 22 standing let bot=stand
    // outlast the kite bot once the shots were softer)
    float art_range = 14.0f;
    // ... growing with the Digimon's walking speed to art_range_moving at
    // full stick (0.70 units a frame): the enemies close in 3 to 40 times
    // slower than the player walks (SURVIVAL_O1A.md S1), so a walking
    // Digimon passes them where a standing one waits for them.
    float art_range_moving = 34.0f;
    // HP = HPMAX x (1 + hp_per_minute x minutes), x warm_up in phase 1 (rising to 1 from 4:00 to 6:00)
    float hp_per_minute = 0.10f, warm_up = 0.6f;   // (4.3 had 0.10; the O1a balance pass 0.15)
    float hp_growth = 0.08f;                     // x e^(hp_growth x minutes) on top (the O1a review round: late crowds outlast the Art)
    // ... and from hp_late_minutes on by hp_growth_late a minute instead
    // (continuous at the knot; the forge round of 05.11: a good player's Art
    // keeps up with the crowd after 15:00 instead of being outgrown by it,
    // docs/OVERFLOW_DIRECTOR.md 11).
    float hp_late_minutes = 15.0f;
    float hp_growth_late = 0.03f;
    // damage taken x (1 + damage_per_minute x minutes) x e^(damage_growth x minutes) x the tier
    float damage_per_minute = 0.0f;              // (4.3 had 0.04, the O1a balance pass 0.14)
    float damage_growth = 0.13f;                 // (the O1a review round 0.18: no wall; the balance round of 05.11, docs/OVERFLOW_DIRECTOR.md 10)
    // ... and from damage_late_minutes on by damage_growth_late a minute
    // instead (the run's late growth; continuous at the knot; the forge
    // round of 05.11: the contact knots carry the late threat, so the time a
    // run lasts depends on how it is played, not on one wall).
    float damage_late_minutes = 12.0f;
    float damage_growth_late = 0.09f;
    std::array<int, 3> curve{70, 65, 16};        // the run's NEXTEXP terms (4.5 had 80 / 50 / 6, the O1a balance pass 70 / 65 / 13)
    float damage_warm_up = 0.2f;                 // the damage side of the warm-up: x 0.2 at 0:00, rising to x 1 at
    float damage_warm_minutes = 8.0f;            // ... this run minute (the review round: 0.4, 5:00)
    // The damage of a hit from an attacker within 15 units (its body, a
    // swing: a player who walks away is not hit) and from farther (a shot),
    // by knots at 0:00-20:00 (OverflowRules near_factor / far_factor; past
    // 20:00 the last knot): the first minutes' contact soft, later the
    // crowd's contact the threat a good player avoids, the shots softer.
    // (The balance round of 05.11: 0.7 / 1.0 / 1.3 / 1.5 / 1.6 and 0.8 /
    // 0.7 / 0.65 / 0.6 / 0.6; the forge round raised the late contact and
    // lowered the late shots: after 10:00 skill decides, docs/OVERFLOW_DIRECTOR.md 11.)
    // (Round O1b lane E measured 1.1 / 2.4 / 3.6 / 3.8 from 5:00 for the gear: Agumon's human bot
    // 22:16, every other form's 9-11 minutes; kept O1a's knots, docs/ROUND_O1B_E_NOTES.md 5.)
    std::array<float, 5> contact_at{0.7f, 1.0f, 1.3f, 2.2f, 2.4f};
    std::array<float, 5> shot_at{0.8f, 0.7f, 0.5f, 0.4f, 0.3f};
    int status_cap = 6;                          // frames of a normal enemy's stun / sleep / bind (4.8 had 30)
    double level_heal = 0.05;                    // a level-up adds this share of MaxHP (4.5: 3 %, at most 5 %)
    float exp_factor = 1.35f;                    // the EXP of a defeat (Normal; fewer enemies, the same level pace: Heal Lt at Lv 15)
    int corrupted_from = 150 * 60, corrupted_every = 60 * 60; // (the review round: from 1:30)
    float corrupted_hp = 8.0f;
    int disk_percent = 20;                       // a Corrupted enemy drops an HP Disk alpha
    float mp_regen = 0.02f;                      // of MaxMP a second
    float heal_below = 0.70f;                    // Heal Lt waits until HP is below this share
    int phase_steps = 300 * 60;                  // five minutes a phase
    int phase_ramp_steps = 90 * 60;              // a new phase's families come in over 90 s (their weight from 0)
    float phase_intro = 0.2f;                    // ... and their damage from this share of their factor
    int phase_intro_steps = 120 * 60;            //     to all of it over two minutes
    // Phase 4's families (the last phase's) over their own times (0: the
    // two above; the forge round of 05.11: 3:30 and 4:00, phase 4 was a wall
    // with 90 s and 120 s: 13-16 of 20 runs ended in its first two minutes).
    int last_ramp_steps = 210 * 60, last_intro_steps = 240 * 60;
    bool spawning = true, abilities = true;      // (the balance test measures with both off,
    bool time_scaling = true;                    //  and sets the damage-taken factor itself)
    // The run completes at 25:00 (the forge round of 05.11; O1a had 20:00):
    // phase 4 goes on for the finale's five minutes (4.4; O1a has no
    // LordKnightmon), so a run that goes well lasts 22-25 minutes (17.1).
    int end_steps = 1500 * 60;
    float bot_flee = 16.0f;                      // the kite bot walks away from an enemy this near (14.1)
    int level_window_gap = 20 * 60;              // at most one level-up window (freeze) in this many run steps
    int muteki = 30;                             // MUTEKI frames after a hit (4.8 had 50: a crowd round a standing Digimon must cost)
    // Guardians and the finale (4.4, round O1b lane E). A Guardian closes
    // phases 1-3 (it enters when the phase's five minutes end; the phases
    // stay on the clock, the balance of docs/OVERFLOW_DIRECTOR.md 10-11 is
    // built on it), LordKnightmon comes at 20:00 under the quest timer: its
    // defeat clears the run, the timer's 0:00 (end_steps) is TIME UP.
    // guardians=false: O1a's run (the clear at end_steps, no Guardian).
    bool guardians = true;
    // x the row's HPMAX x (1 + 0.10 a minute) (4.4: x 6, the finale x 8; round O1b: the finale x 14,
    // with x 8 the kite bot cleared 11 of 12 runs, LordKnightmon falling after 96-198 s)
    float guardian_hp = 6.0f, finale_hp = 14.0f;
    float elite_hp = 12.0f;                      // the elite fallback (4.4)
    float guardian_distance = 40.0f;             // it enters this far in front of the player
    int guardian_half_steps = 120 * 60;          // A(t) halves while a Guardian lives, at most this long
    int guardian_first = 0;                      // (tests, spikes: the first Guardian at this run step; 0: 5:00)
    int finale_hurry = 60 * 60;                  // QS_HURRY in the timer's last minute
    float guardian_clamp = 0.30f;                // a Guardian's hit takes at most this share of MaxHP (round O1b)
    // Pwr Up's cooldown from pwr_up_late_from on (0: its 15 s throughout; a
    // balance pass's key pwr_up_late=SECONDS,FROM). Round O1b lane E's finish
    // measured 60 s from the start (newcomers' runs ended at the phase 2
    // start), 60 s from 10:00 (still on: Giga to infinity last 42-54 s plus
    // INT) and 120 s from 10:00 (the human Agumon 1.5 minutes shorter, but
    // late hits over 35 % of MaxHP, the clamp, in 2 of 20 kite runs):
    // docs/ROUND_O1B_E_NOTES.md 12.1.
    int pwr_up_late_from = 600 * 60, pwr_up_late_cooldown = 0;
    // The Violet Ring's return gate after the clear (4.4): g_dreturngate comes
    // gate_wait steps after LordKnightmon's fall (the enemies leave first) and
    // walking into its mat (matz01) ends the run; elsewhere, or with
    // return_gate=false, the results come after 4 s. A test bot that has not
    // reached it after gate_bot_steps gets the results anyway.
    bool return_gate = true;
    int gate_wait = 120;
    int gate_bot_steps = 60 * 60;
};

// ---------------------------------------------------------------------------
// Offers (4.6): Forge, Train and Plug in O1a
// ---------------------------------------------------------------------------
enum class OfferKind : std::uint8_t { forge, train, plug, core, board, chip };
struct Offer {
    OfferKind kind{};
    // forge: the family it opens (2-5); train: the skill column (6 Blast, 7
    // Heal, 8 Force); plug: the UPCNT index; core / board / chip: the index
    // into gear_table() (what it does follows from the build: equip it, or
    // its next step).
    int value{};
    bool operator==(const Offer&) const = default;
};
struct OfferState {
    int forge = 1;                     // families opened (1-5)
    // The families of the Art's weapon class in the weapon table (the
    // form's data, read at the field start): a Forge opens 2 up to this.
    // A form whose class has a single family has no Forge at all.
    int forge_families = 5;
    int drawn = 0;                     // the level-up offers drawn so far (the sure first Forge)
    int forge_sure = 3;                // ... in this many of the first offers (kForgeSure; 0: none, the balance round's draws)
    std::array<int, 3> train{};        // Train steps taken per group (Blast, Heal, Force; 0-4)
    std::array<bool, 3> groups{};      // the form has a run technique of that group
    // The run level at which the group's first run technique is learned (its
    // TECHn_LEVEL x 2/3; 0: from the start). Train shows from two levels
    // before it: a Train Heal at level 2 for a Heal Lt of level 15 would be
    // a choice that does nothing for minutes.
    std::array<int, 3> unlock_level{};
    int level = 1;                     // the run level of the window
    // The gear offers that may show (round O1b, 4.6: Core, Board, Chip; the
    // director fills them from the build and the opened gear).
    std::vector<Offer> gear;
};
// Train shows this many run levels before its group's first technique.
constexpr int kTrainLead = 2;
// The first Forge is sure (the forge round of 05.11, docs/OVERFLOW_DIRECTOR.md
// 11): a form with a Forge finds it on the first line (where the cursor
// starts, with the line about it) of each of its first kForgeSure level-up
// offers until it takes it. It stays a choice: the other two lines are drawn
// as always. (A newcomer's Agumon runs whose first Forge came after the
// third offer ended before 10:00 five times in twelve; never when it came
// sooner.)
constexpr int kForgeSure = 3;
static_assert(kForgeSure == OfferState{}.forge_sure);
// The group cap after `steps` Train offers: 300, 750, 1250, 1750, 2500.
int train_cap(int steps);
// The UPCNT points of a Plug at a run level: 10 + 2 x level.
int plug_points(int level);
// The UPCNT stats a Plug can raise (the plug stats of 7.4) and their names.
const std::array<int, 11>& plug_stats();
const char* plug_name(int upcnt_index);
// Every offer that may show (the plugs always).
std::vector<Offer> eligible_offers(const OfferState& state);
// Three different offers by the weights of 4.6 (Forge 25, Train 25 split
// over its groups, Plug 20 split over its stats, a resistance at a quarter
// of a stat's share: in O1a few attacks carry an element; a Plug takes a
// line only by its weight or when fewer than three others are possible).
// The first Forge takes the first line of the first kForgeSure offers.
// `random` is the director's splitmix64 state.
std::vector<Offer> draw_offers(const OfferState& state, std::uint64_t& random);
// The priority list of the test bot (offers=priority): Forge, Train Blast,
// Train Heal, then the plugs in plug_stats order.
int offer_priority(const Offer& offer);

// ---------------------------------------------------------------------------
// Gear (7, round O1b): DW4's own rows, equipped by DW4's rules
// ---------------------------------------------------------------------------
enum class GearSlot : std::uint8_t { core, board, chip };
struct GearRow {
    const char* key;      // the gear key (the loadout, the records: docs/ROUND_O1B_E_NOTES.md)
    const char* name;     // English (the table's own name without its grade)
    GearSlot slot;
    int first;            // its alpha row (armor1, armor2, abChip; the PWR Medal: the fire one, + 15 per element)
    bool weapon_pocket;   // a chip the weapon pocket takes (WU)
    bool core_pocket;     // ... the core pocket (BLUEAB)
    bool x_data;          // it powers up with ¶ Data (7.5)
    bool start;           // open from the start (8.4); else a record opens it (RunConfig::gear_open)
};
const std::vector<GearRow>& gear_table();
int gear_index(std::string_view key); // -1: none
// The run's ¶ Data cost of a chip's next step: its NEXTX / 20 (7.5).
constexpr int kXDataDivisor = 20;

// The Art's class factor of a form (on top of OverflowRules' 2.5; 14.3).
float art_class_factor(std::string_view form);
// A run technique's own damage factor (pcTech base row; 1 for most).
float technique_damage(int family);

// splitmix64 draws (the randomizer's pattern).
std::uint64_t next_random(std::uint64_t& state);
double next_unit(std::uint64_t& state); // [0, 1)

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------
struct RunConfig {
    std::string form = "p_agumon";
    std::string field = "stagbs02";
    std::uint32_t seed = 1;
    int tier = 0;            // 0 Normal (O1a plays Normal only)
    std::string bot;         // "kite": the test bot drives player 1
    std::string offers;      // "priority" / "random": a test policy takes the offers; empty: the player
    int results_hold = 0;    // test runs: the results close after this many steps (0: they wait for A)
    Tuning tuning;
    std::map<std::string, float> family_damage; // a balance pass's own family damage factors (tables.inc otherwise)
    std::map<std::string, int> family_ring;     // ... and spawn rings (0-2, FamilyRow::ring)
    // A test policy takes no Forge before this run step (the forge round's
    // measurement of a form without its early Forge): it takes the next line.
    int forge_from = 0;
    int forge_sure = kForgeSure; // (forge_sure=0: the balance round's offers, for its before measurement)
    float art_class = 0;     // a balance pass's own Art class factor (0: art_class_factor)
    // Round O1b (the lobby's character select and the meta file, lanes D
    // and F; docs/ROUND_O1B_E_NOTES.md): the starting loadout (8.3, gear keys
    // of gear_table(); "" none), the gear the Data Records opened beyond the
    // start set, and the Plug Lab's points of the form (8.2) by UPCNT index
    // (GameSystem::PlayerSlot::upcnt: 0 MaxHP, 1 MaxMP, 2 Strength, 3 Def.,
    // 4 Wisdom, 5 Spirit, 6 Speed, 7-10 Fire / Ice / Bolt / Dark RES).
    std::string loadout_armor, loadout_chip;
    std::vector<std::string> gear_open;
    std::array<int, 11> plug_lab{};
    // Guardians (tests, spikes): the families of phases 1-3 ("" drawn by the
    // seed) and the families that come as the elite fallback (4.4).
    std::array<std::string, 3> guardians{};
    std::vector<std::string> elite;
    // (Tests, pictures: the Forge families open at the start, 1-5; the Awakening's test.)
    int forge_start = 1;
    int art_skill_start = 0; // (... and the Art's class skill at the start: forge_start=5 art_skill=2500 awakens)
    // (A balance pass's own technique cooldowns by pcTech base row, in steps; 0: the technique is never cast.)
    std::map<int, int> technique_cooldown;
};

// The four Rookies a run starts with in O1b (5.3).
constexpr std::array<std::string_view, 4> kRookies{"p_agumon", "p_vmon", "p_girumon", "p_dorumon"};
bool playable_form(std::string_view form);

// ---------------------------------------------------------------------------
// Guardians (4.4)
// ---------------------------------------------------------------------------
struct GuardianRow {
    const char* family;  // e_blosso
    const char* name;    // Blossomon
    int record;          // its Normal row in the enemy table
    const char* donor;   // the stage (main member) or separate package it loads from (H2)
    int phase;           // 1-3, 4 the finale
    const char* elite;   // the elite fallback's family (4.4)
    float damage;        // its own damage factor (round O1b: its worst hits about 15-20 % of a Rookie's MaxHP at its time)
};
const std::vector<GuardianRow>& guardian_table();
const GuardianRow* guardian_row(std::string_view family);
// The run's Guardians: phases 1-3 drawn from their pairs by the seed (or the
// forced ones), then LordKnightmon.
std::array<std::string, 4> draw_guardians(std::uint32_t seed, const std::array<std::string, 3>& forced = {});
struct GuardianBar {
    std::string name;
    int hp{}, max_hp{};
};

// Why the run ended (the host maps it to its RunEnd).
enum class Outcome : std::uint8_t { none, retired, defeated, cleared, time_up };

// What the overlay shows (O1a: plain boxes, docs/SURVIVAL.md 9.4-9.6).
enum class Screen : std::uint8_t { none, level_up, pause, build, retire_ask, results };

// One minute of the run log (13.5).
struct MinuteLog {
    int minute{}, level{}, defeats{}, hits_taken{}, clamps{}, alive{};
    std::uint64_t no_control{}, steps{};
    double highest_hit{}; // of MaxHP
    double taken{};       // the damage taken in this minute (hits and their poison), in MaxHPs
    // ... by source (the balance round of 05.11): a hit from an attacker
    // within 15 units (a melee swing, a body attack), one from farther (a
    // shot), and the poison the hits left; in MaxHPs.
    double contact{}, shot{}, poison{};
    // The defeats of the minute whose last hit was the Art's, and the
    // others' (the techniques).
    int art_kills{}, tech_kills{};
    // The living enemies within 15 and 30 units (the minute's mean) and the
    // share of its steps the player was trapped: no 90-degree way out
    // between the enemies within 20 units (the user: "no gap to run").
    double near15{}, near30{}, trapped{};
};

class OverflowDirector final : public Director {
public:
    explicit OverflowDirector(RunConfig config);

    // The host's attach: the rules' values (the donors, the caps) before
    // the arena's request. The rules must outlive nothing: the host keeps
    // the director alive while the engine lives.
    void attach(Engine& engine, OverflowRules& rules);

    // ---- Director (dw4/engine/overflow.hpp)
    void step(Engine& engine) override;
    void field_started(Engine& engine) override;
    void run_end(Engine& engine, const RunEnd& end) override;
    void skill_changed(Engine& engine, int slot, int skill, int before, int after) override;
    void enemy_defeated(Engine& engine, int killer, int record, std::string_view family, int index) override;
    void level_up(Engine& engine, int slot, int level) override;
    void item_taken(Engine& engine, int slot, int table, int record) override;
    void player_hit(Engine& engine, const HitTaken& hit) override;
    void damage_dealt(Engine& engine, std::string_view family, int index, std::int32_t source, int damage) override;

    // ---- Host commands (they act in the next step; the run log keeps them)
    void command_move(int delta);      // the cursor of the open screen
    void command_accept();             // A on the open screen
    void command_back();               // B on the open screen
    void command_pause();              // START
    void command_retire();             // F1 > Retire, a test run's limit: the results follow
    // The pads the run's engine gets this step (move only, 10): the stick
    // and the D-pad; A only while an item's name is up for player 1 (a
    // pickup), or with the item panel open; L opens the item panel; START,
    // B, X, Y, R and Z never reach it. The kite bot's pad replaces player 1's.
    PadSample filter_pad(Engine& engine, const PadSample& live);
    PadSample bot_pad(Engine& engine);
    // The human bot (bot=human, the balance round of 05.11): a newcomer as
    // the user played his first run. It sees the enemies 250-300 ms late,
    // looks again only every 6-14 steps (while it roams every 20-40), flees
    // from the middle of the near ones (no search for the gap, its way off by
    // up to 30 degrees, at times by 70), so it may run into the next group;
    // it roams to points of the arena and at times towards a group (to let
    // the Art work), stops now and then when nothing is near, and takes the
    // offers as a newcomer (offers=newcomer: the first line or a random one).
    // Seeded from the run's seed, its own generator: deterministic.
    PadSample human_pad(Engine& engine);
    // The run log's pad line for this step (after filter_pad).
    void record_pad(const PadSample& pad);

    // ---- What the host reads
    Outcome outcome() const { return outcome_; }
    bool over() const { return over_; }             // the results closed
    Screen screen() const { return screen_; }
    int cursor() const { return cursor_; }
    const std::vector<Offer>& offers() const { return offers_; }
    std::string offer_text(const Engine& engine, const Offer& offer) const;
    std::string offer_detail(const Engine& engine, const Offer& offer) const;
    std::string offer_about(const Engine& engine, const Offer& offer) const; // one line about the cursor's offer (9.4)
    std::uint64_t run_steps() const { return clock_; }
    std::uint64_t steps() const { return steps_; }    // every director step (frozen ones too)
    double run_seconds() const;
    int phase() const;
    int level(const Engine& engine) const;
    int defeats() const { return defeats_; }
    int alive() const { return alive_; }
    bool frozen() const { return frozen_; }
    const RunConfig& config() const { return config_; }
    const std::string& art_name() const { return art_name_; }
    // The build page and the results (9.5, 9.6): lines of text.
    std::vector<std::string> build_lines(const Engine& engine) const;
    std::vector<std::string> result_lines(const Engine& engine) const;
    // A short line about the last visible change (a weapon rung, a new
    // technique) for the overlay, and the step it happened in.
    const std::string& notice() const { return notice_; }
    std::uint64_t notice_step() const { return notice_step_; }
    // The run log (13.5) and the summary of 14.2's numbers (T6).
    std::string log_text() const;
    std::string summary_text(const Engine& engine) const;
    // For the tests (T7): the hits taken per family ("e_ogre", a Corrupted
    // one "e_ogre*"): count and the highest share of MaxHP; the arena's
    // walkable cells; a spawn point as the spawner draws it; an
    // encirclement now; the damage-taken factor at a run time.
    const std::map<std::string, std::pair<int, double>>& hits_by_family() const { return hits_by_family_; }
    void reset_hits_by_family() { hits_by_family_.clear(); }
    bool walkable(float x, float z) const;
    bool sample_spawn_point(Engine& engine, float lo, float hi, bool ahead, Vec3f& out, float visible_min = 0.0f) {
        return spawn_point(engine, player_position(engine), lo, hi, ahead, out, visible_min);
    }
    void encircle_now(Engine& engine) { encircle(engine); }
    // The enemies the spawner has brought back (stayed far, stood, left behind).
    std::uint64_t recycled() const { return recycled_; }
    // The poisonings of the run's poison rule and the largest one's total (of MaxHP).
    int poisonings() const { return poisonings_; }
    double highest_poison() const { return highest_poison_; }
    float damage_factor(double minutes) const;
    float family_damage(const FamilyRow& row) const; // a family's own damage factor
    // A(t) and the refill rate at a run time (Tuning's knots).
    static float knots(const std::array<float, 5>& at, double minutes);
    int alive_target() const; // A(t) now: the knots, a surge, halved while a Guardian lives (4.4)
    float art_factor() const { return config_.art_class > 0 ? config_.art_class : art_class_factor(config_.form); }
    double hp_factor(double minutes) const;   // an enemy's HPMAX factor at a run time
    // Guardians and the finale (4.4; the HUD of lane D, the records of lane F).
    bool finale() const { return finale_; }                  // LordKnightmon is in, the quest timer runs
    int finale_left_steps() const;                           // the timer (0..end_steps - 20:00), 1 step = 1001/60000 s
    bool time_up() const { return outcome_ == Outcome::time_up; }
    std::optional<GuardianBar> guardian_bar(Engine& engine) const;
    const std::vector<std::string>& guardians_defeated() const { return guardians_defeated_; }
    const std::array<std::string, 4>& guardian_plan() const { return guardian_plan_; }
    bool awakened() const { return awakened_; }
    int awakening_bursts() const { return bursts_; }        // the x_100charge01 bursts of the awakened Art (6.5)
    // The Violet Ring's return gate (4.4): it is up after the clear and the
    // player may walk into it (the results follow); its mat's box.
    bool return_gate_open() const { return gate_ == 2; }
    bool gate_entered() const { return gate_entered_; }
    int casts() const;
    // The defeats whose last hit was a technique of Fire, Ice, Bolt or Dark
    // (pcTech rows 0-19; lane F's Adept records), and the crates broken
    // (none on O1b's arena yet).
    const std::array<int, 4>& tech_defeats() const { return tech_defeats_; }
    int crates() const { return 0; }
    // The family whose hit took the last HP (lane D's "Welcome back", 3.4:
    // the Gekomon joke); empty unless the run ended in a defeat. (A poison
    // tick never takes the last HP, so the last damaging hit is the one.)
    std::string defeated_by() const { return outcome_ == Outcome::defeated ? last_hit_family_ : std::string(); }
    const Vec3f& arena_centre() const { return centre_; }
    float arena_radius() const { return radius_; }

private:
    struct Enemy;
    struct Strike;
    void start_running(Engine& engine);
    void apply_commands(Engine& engine);
    void freeze(Engine& engine, bool on);
    void open_level_up(Engine& engine);
    void take_offer(Engine& engine, const Offer& offer);
    void begin_end(Engine& engine, Outcome outcome);
    void step_ending(Engine& engine);
    void step_running(Engine& engine);
    void update_factors(Engine& engine);
    void build_walkable(Engine& engine);
    bool spawn_point(Engine& engine, const Vec3f& centre, float lo, float hi, bool ahead, Vec3f& out, float visible_min = 0.0f);
    // The sectors round the player with a living enemy within gap_radius
    // (bit k: sector k), and whether one more at `point` leaves a way out.
    void update_gap(Engine& engine);
    bool keeps_gap(const Vec3f& point) const;
    int family_ring(const FamilyRow& row) const; // its spawn ring (tables.inc or RunConfig::family_ring)
    float ring_min(const FamilyRow& row) const;
    float ring_max(const FamilyRow& row) const;
    float visible_min(const FamilyRow& row) const;
    bool spawn_one(Engine& engine, const FamilyRow& row, bool corrupted, std::optional<Vec3f> at = std::nullopt, int reuse_ram = -1,
                   double hp_share = 1.0);
    void spawner(Engine& engine);
    void encircle(Engine& engine);
    void place_encircled(Engine& engine);
    void track_enemies(Engine& engine);
    void cast_art(Engine& engine);
    void cast_techniques(Engine& engine);
    void equip_ladder(Engine& engine, bool force_model);
    void load_art_pattern(Engine& engine);
    void drop_items(Engine& engine);
    void log_minute(Engine& engine);
    void step_guardians(Engine& engine);
    bool spawn_guardian(Engine& engine, int stage);
    void guardian_down(Engine& engine, bool defeated);
    void step_music(Engine& engine);
    // The return gate (4.4): its pad for the test bots while it is up.
    bool gate_walk() const { return gate_ > 0 && outcome_ == Outcome::cleared && screen_ == Screen::none; }
    PadSample gate_pad(Engine& engine);
    void step_gate(Engine& engine);
    bool gate_inside(const Vec3f& at) const;
    // Gear (gear.cpp)
    void gear_offers(Engine& engine);
    void take_gear(Engine& engine, const Offer& offer);
    bool equip_gear(Engine& engine, int gear, int record);
    void apply_loadout(Engine& engine);
    void power_up_chips(Engine& engine);
    int gear_row(int gear) const;          // its alpha row for this form
    bool gear_open(int gear) const;
    std::string gear_text(const Engine& engine, const Offer& offer) const;
    std::string gear_detail(const Engine& engine, const Offer& offer) const;
    void notice(std::string text);
    Vec3f player_position(Engine& engine) const;
    // The stick that walks the player on the ground way (dx, dz) at this
    // magnitude, through the box camera (the bots).
    PadStick stick_for(Engine& engine, const Vec3f& me, float dx, float dz, float magnitude) const;
    // The bots' item handling: A on a labelled item, the HP disk from the
    // item panel (L, then A) once the HP is below num / den for more than
    // `wait` steps.
    void bot_buttons(Engine& engine, HostPadInput& input, int num, int den, int wait);

    RunConfig config_;
    OverflowRules* rules_ = nullptr;
    std::uint64_t random_ = 0;

    // State
    bool started_ = false, frozen_ = false, over_ = false;
    Outcome outcome_ = Outcome::none;
    Screen screen_ = Screen::none;
    int cursor_ = 0;
    std::vector<Offer> offers_;
    int pending_levels_ = 0;
    int known_level_ = 1;
    std::uint64_t clock_ = 0;      // run steps (not frozen)
    std::uint64_t steps_ = 0;      // every director step
    std::uint64_t ending_since_ = 0;
    int ending_phase_ = 0;
    std::uint64_t results_since_ = 0;
    int music_phase_ = 0;
    std::uint64_t music_at_ = 0;

    // Commands queued by the host for the next step.
    struct Command {
        char kind; // 'm' move, 'a' accept, 'b' back, 'p' pause, 'r' retire
        int value;
    };
    std::vector<Command> commands_;

    // The arena
    struct Cell {
        bool walkable{};
        float y{};
    };
    std::vector<Cell> grid_;
    float grid_x0_ = -150.0f, grid_z0_ = -150.0f, grid_step_ = 5.0f;
    int grid_n_ = 61;
    Vec3f centre_{};
    float radius_ = 110.0f;

    // The spawner
    struct Enemy {
        std::string family;
        int ram = -1;
        std::uint64_t born = 0;          // run step of its GENERATE
        float nearest = 1e9f;            // its closest distance to the player so far
        Vec3f anchor{};                  // where it stood when it last moved (4 units)
        std::uint64_t closer_at = 0;     // the run step it last moved
        float best = 1e9f;               // its closest distance since it last came closer (2 units)
        std::uint64_t best_at = 0;       // the run step it last came closer
        std::uint64_t far_since = 0;     // the run step it went past recycle_far (0: not)
        bool corrupted = false;
        bool closed = false;             // came within 30 units in 8 s
        bool counted = false;            // its close-in result is in the tally
        int art_hits = 0;
        bool other_damage = false;       // a technique hit it too
        bool last_art = false;           // the last hit was the Art's
        std::int32_t last_source = 0;    // the last hit's source (damage_dealt)
        bool alive = true;
    };
    std::vector<Enemy> enemies_;
    std::uint64_t next_spawn_ = 0;
    double refill_credit_ = 0;       // spawns the refill rate allows (Tuning::refill_at)
    std::uint32_t gap_sectors_ = 0;  // the closed sectors (update_gap)
    Vec3f gap_centre_{};
    bool gap_active_ = false;
    std::uint64_t gap_refused_ = 0;  // spawn points drawn again for the gap
    std::uint64_t next_corrupted_ = 0;
    std::size_t encircled_ = 0;
    // An encirclement's enemies waiting for their place (encircle_per_step a step).
    struct Encircled {
        std::string family;
        int ram = -1;
        int slot = 0, count = 1;   // its place on the ring
        bool corrupted = false;
        double hp_share = 1.0;     // the HP it keeps
    };
    std::vector<Encircled> encircle_queue_;
    int encircle_placed_ = 0;
    int alive_ = 0;
    std::vector<Vec3f> trail_;           // the player's recent positions (its walking direction)

    // The Art
    struct Box {
        Vec3f offset{}, size{};
        float delay{}, end{};
    };
    std::vector<Box> pattern_;
    float pattern_tip_ = 17.0f;
    std::string art_name_, art_swing_, art_label_;
    // The class's charge release (KATC?2 of the Art's SQ_ATCK1 section:
    // x_100charge01 SQ_KIRIKAE / SQ_OWARI), the awakened Art's burst (6.5):
    // once for every strike that hits.
    std::string art_burst_;
    bool burst_pending_ = false;
    std::uint64_t burst_done_ = ~std::uint64_t(0); // the art_ready_ of the strike that burst last
    int bursts_ = 0;
    std::array<std::string, 4> forge_steps_;     // what Forge 2-5 add to the Art
    std::vector<std::string> family_names_;      // the class's five weapon families (Forge's names)
    int ladder_base_ = 0, art_skill_ = 0;
    int art_cooldown_ = 75;
    std::uint64_t art_ready_ = 0;
    std::string weapon_model_;
    bool weapon_dirty_ = false;

    // The techniques
    struct Technique {
        int family = 0;           // pcTech base row (TECHn)
        int cooldown = 150;       // steps
        const char* sound = "plMAGIC1";
        bool aim = false;
        bool heal = false;
        std::uint64_t ready = 0;
        int casts = 0;
        std::string name;         // "Fire"
        int group = 0;            // 0 Blast, 1 Heal, 2 Force
        int lock = 1;             // the run level it is learned at (TECHn_LEVEL x 2/3)
    };
    std::map<int, std::string> actions_;         // pcTech row -> the SQ_WAZA action (HFIRE1)
    std::map<std::string, std::uint64_t> sound_at_; // a cast sound at most every 4 steps
    void aim_effects(Engine& engine, std::size_t first, float yaw);
    std::string technique_name(int source) const;
    std::vector<Technique> techniques_;
    double mp_rest_ = 0;

    OfferState offer_state_;
    std::vector<std::pair<int, std::uint64_t>> plugs_taken_;

    // Drops waiting for the next step (outside the hit scripts).
    struct Drop {
        std::string model;
        int record{}, table{};
        Vec3f at{};
    };
    std::vector<Drop> drops_;

    // The run log
    int defeats_ = 0, corrupted_defeats_ = 0;
    std::map<std::string, int> defeats_by_family_;
    std::map<std::string, std::pair<int, double>> hits_by_family_; // hits taken, the highest share of MaxHP
    std::map<std::pair<int, std::string>, double> taken_by_family_; // the damage taken per phase and family (MaxHPs, poison included)
    std::map<std::string, std::pair<int, int>> closed_by_family_; // closed, spawned (measured families)
    std::vector<int> art_hits_per_kill_;                          // phase 1, Art-only defeats
    std::vector<std::uint64_t> level_up_steps_;
    std::vector<std::uint64_t> window_steps_;                     // the run steps the level-up windows opened
    std::vector<std::pair<std::uint64_t, std::string>> command_log_;
    std::vector<std::pair<std::uint64_t, std::string>> event_log_;
    std::string pad_log_;                                         // run-length "count:buttons/x/y" lines
    PadSample last_pad_{};
    std::uint64_t last_pad_count_ = 0;
    std::vector<MinuteLog> minutes_;
    int hits_taken_ = 0, misses_ = 0, clamps_ = 0;
    int poisonings_ = 0;
    double highest_poison_ = 0;      // the largest poisoning's total, of MaxHP
    double taken_share_ = 0;         // the damage taken (hits and their poison), in MaxHPs
    double minute_taken_mark_ = 0;   // taken_share_ at the last minute line
    // This minute's damage by source and the crowd round the player (MinuteLog).
    double minute_contact_ = 0, minute_shot_ = 0, minute_poison_ = 0;
    double minute_near15_ = 0, minute_near30_ = 0;
    int minute_art_kills_ = 0, minute_tech_kills_ = 0;
    std::uint64_t minute_trapped_ = 0, minute_steps_ = 0;
    std::string end_reason_ = "-";   // what ended the run (RunEnd's reason, retire, clear)
    std::string last_hit_family_;    // the last damaging hit's shooter (defeated_by)
    std::uint64_t no_control_ = 0;   // run steps (not the ending's) without control (14.2)
    double highest_hit_ = 0;
    int panels_min_gap_ = 1 << 30;
    std::int64_t last_panel_frame_ = -1;
    int last_panel_count_ = 0;
    std::uint64_t spawned_ = 0, corrupted_spawned_ = 0, recycled_ = 0, encircled_total_ = 0, spawn_failures_ = 0;
    int disks_taken_ = 0, disks_used_ = 0, plugs_picked_ = 0;
    std::string notice_;
    std::uint64_t notice_step_ = 0;
    // Guardians and the finale
    struct Guardian {
        std::string family, name;
        int ram = -1;
        int stage = 0;               // 1-3 the phases, 4 the finale
        std::uint64_t at = 0;        // the run step it entered
        bool elite = false;
    };
    std::array<std::string, 4> guardian_plan_{};
    int guardian_next_ = 1;          // the stage of the next Guardian (5: none left)
    std::optional<Guardian> guardian_;
    std::vector<std::string> guardians_defeated_;
    bool finale_ = false;
    std::uint64_t finale_at_ = 0;
    std::string music_now_;
    int count_said_ = -1;            // the timer's last counted second
    bool awakened_ = false;
    // The return gate: 0 none, 1 coming (gate_since_ the clear's step), 2 up
    // (gate_open_at_); the box of its mat.
    int gate_ = 0;
    std::uint64_t gate_since_ = 0, gate_open_at_ = 0;
    bool gate_entered_ = false;
    bool gate_out_ = false;          // the player stood outside its box since it came
    Vec3f gate_min_{-6.0f, -1e9f, -66.0f}, gate_max_{6.0f, 1e9f, -54.0f};
    // Gear (7): the equipped gear (gear_table() indices, -1 none), their
    // value steps, the run's ¶ Data and the form's element (0 fire .. 3 dark).
    int core_gear_ = -1, board_gear_ = -1;
    int core_steps_ = 0, board_steps_ = 0;
    std::array<int, 2> pocket_gear_{-1, -1}; // the weapon pocket, the core pocket
    int x_data_ = 0, x_data_total_ = 0, chip_steps_ = 0;
    int element_ = 0;
    std::array<int, 4> tech_defeats_{};
    // the kite bot
    int bot_a_ = 0, bot_l_ = 0;
    float bot_dx_ = 0, bot_dz_ = 0;  // its last way (it keeps walking)
    bool control_seen_ = false;
    int last_disks_ = 0;
    int disk_wait_ = 0;              // steps the bot has been below its disk share
    // the human bot (human_pad)
    struct Human {
        std::uint64_t random = 0;
        std::vector<std::vector<Vec3f>> seen; // the enemies' positions of the last steps (seen[0] the oldest)
        int reaction = 16;                    // what it acts on is this many steps old
        int look_in = 0;                      // steps until it looks again
        enum class Mode : std::uint8_t { roam, escape, stop } mode = Mode::roam;
        int mode_left = 0;                    // steps the mode holds at least
        Vec3f goal{};
        int goal_left = 0;                    // steps until it picks another goal
        float want_x = 0, want_z = 0, want_magnitude = 0; // where it means to go
        float dx = 0, dz = 0, magnitude = 0;              // where the stick points (it turns there quickly, not at once)
    } human_;
};

} // namespace dw4::engine::overflow
