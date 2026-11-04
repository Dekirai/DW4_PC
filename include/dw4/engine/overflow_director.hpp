#pragma once

// Data Overflow (docs/SURVIVAL.md 13.5, round O1a lane C): the director of a
// run's engine. It drives the run through the engine hooks of
// dw4/engine/overflow.hpp (Director) and the engine's own messenger
// commands, in a Messenger::PortOnly scope:
//   - the clock (the steps the run is not frozen) and the phases of 4.2 on
//     the Violet Ring (O1a: no Guardians; at 20:00 the run completes);
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
    int ring;             // where it spawns: 0 the 40-55 ring, 1 30-42 (slow walkers, S1), 2 20-25 (never walks: Kokatorimon)
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
    // 4.3: A(t) = min(cap, base + per_minute x minutes)
    float alive_base = 14.0f, alive_per_minute = 3.2f;
    int alive_cap = 60;
    int spawn_every = 10, spawns_per_tick = 2;  // at most 2 spawns every 10 frames
    int surge_every = 90 * 60, surge_length = 20 * 60, surge_extra = 12;
    std::array<int, 3> encircle_at{450 * 60, 750 * 60, 1050 * 60}; // 7:30, 12:30, 17:30 (run steps)
    int encircle_count = 16;
    float encircle_ring = 45.0f;
    float ring_min = 32.0f, ring_max = 46.0f;    // S1 proposed 40-60; T6 wants them closer (14.2: within 30 in 8 s)
    float slow_min = 26.0f, slow_max = 36.0f;    // slow walkers (S1: under 0.1 units a frame from 50)
    float near_min = 20.0f, near_max = 25.0f;    // families that never walk
    float ahead_share = 0.7f;                    // spawns from the side the player walks towards
    float recycle_far = 140.0f;  int recycle_far_steps = 5 * 60;
    float idle_far = 30.0f;      int idle_steps = 6 * 60;
    // HP = HPMAX x (1 + hp_per_minute x minutes), x warm_up in phase 1
    float hp_per_minute = 0.15f, warm_up = 0.6f;   // (4.3 had 0.10)
    // damage taken x (1 + damage_per_minute x minutes) x the tier
    float damage_per_minute = 0.14f;             // (4.3 had 0.04; the O1a balance pass, docs/OVERFLOW_DIRECTOR.md)
    std::array<int, 3> curve{70, 65, 13};        // the run's NEXTEXP terms (4.5 had 80 / 50 / 6)
    float damage_warm_up = 0.6f;                 // the damage side of the warm-up: x 0.6 at 0:00, rising to x 1 at 5:00
    int status_cap = 6;                          // frames of a normal enemy's stun / sleep / bind (4.8 had 30)
    double level_heal = 0.05;                    // a level-up adds this share of MaxHP (4.5: 3 %, at most 5 %)
    int corrupted_from = 90 * 60, corrupted_every = 60 * 60;
    float corrupted_hp = 8.0f;
    int disk_percent = 20;                       // a Corrupted enemy drops an HP Disk alpha
    float mp_regen = 0.02f;                      // of MaxMP a second
    float heal_below = 0.70f;                    // Heal Lt waits until HP is below this share
    int phase_steps = 300 * 60;                  // five minutes a phase
    int phase_ramp_steps = 90 * 60;              // a new phase's families come in over 90 s
    bool spawning = true, abilities = true;      // (the balance test measures with both off,
    bool time_scaling = true;                    //  and sets the damage-taken factor itself)
    int end_steps = 1200 * 60;                   // O1a: the run completes at 20:00
};

// ---------------------------------------------------------------------------
// Offers (4.6): Forge, Train and Plug in O1a
// ---------------------------------------------------------------------------
enum class OfferKind : std::uint8_t { forge, train, plug };
struct Offer {
    OfferKind kind{};
    int value{}; // forge: the family it opens (2-5); train: the skill column (6 Blast, 7 Heal, 8 Force); plug: the UPCNT index
    bool operator==(const Offer&) const = default;
};
struct OfferState {
    int forge = 1;                     // families opened (1-5)
    std::array<int, 3> train{};        // Train steps taken per group (Blast, Heal, Force; 0-4)
    std::array<bool, 3> groups{};      // the form has a run technique of that group
};
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
// over its groups, Plug 20 split over its stats; a Plug takes a line only
// by its weight or when fewer than three others are possible). `random`
// is the director's splitmix64 state.
std::vector<Offer> draw_offers(const OfferState& state, std::uint64_t& random);
// The priority list of the test bot (offers=priority): Forge, Train Blast,
// Train Heal, then the plugs in plug_stats order.
int offer_priority(const Offer& offer);

// The Art's class factor of a form (on top of OverflowRules' 2.5; 14.3).
float art_class_factor(std::string_view form);

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
};

// Why the run ended (the host maps it to its RunEnd).
enum class Outcome : std::uint8_t { none, retired, defeated, cleared };

// What the overlay shows (O1a: plain boxes, docs/SURVIVAL.md 9.4-9.6).
enum class Screen : std::uint8_t { none, level_up, pause, build, retire_ask, results };

// One minute of the run log (13.5).
struct MinuteLog {
    int minute{}, level{}, defeats{}, hits_taken{}, clamps{}, alive{};
    std::uint64_t no_control{}, steps{};
    double highest_hit{}; // of MaxHP
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
    bool sample_spawn_point(Engine& engine, float lo, float hi, bool ahead, Vec3f& out) {
        return spawn_point(engine, player_position(engine), lo, hi, ahead, out);
    }
    void encircle_now(Engine& engine) { encircle(engine); }
    float damage_factor(double minutes) const;
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
    bool spawn_point(Engine& engine, const Vec3f& centre, float lo, float hi, bool ahead, Vec3f& out);
    float ring_min(const FamilyRow& row) const;
    float ring_max(const FamilyRow& row) const;
    bool spawn_one(Engine& engine, const FamilyRow& row, bool corrupted, std::optional<Vec3f> at = std::nullopt, int reuse_ram = -1);
    void spawner(Engine& engine);
    void encircle(Engine& engine);
    void track_enemies(Engine& engine);
    void cast_art(Engine& engine);
    void cast_techniques(Engine& engine);
    void equip_ladder(Engine& engine, bool force_model);
    void load_art_pattern(Engine& engine);
    void drop_items(Engine& engine);
    void log_minute(Engine& engine);
    void notice(std::string text);
    Vec3f player_position(Engine& engine) const;

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
        std::uint64_t far_since = 0;     // the run step it went past recycle_far (0: not)
        bool corrupted = false;
        bool closed = false;             // came within 30 units in 8 s
        bool counted = false;            // its close-in result is in the tally
        int art_hits = 0;
        bool other_damage = false;       // a technique hit it too
        bool alive = true;
    };
    std::vector<Enemy> enemies_;
    std::uint64_t next_spawn_ = 0;
    std::uint64_t next_corrupted_ = 0;
    std::size_t encircled_ = 0;
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
    std::array<std::string, 4> forge_steps_;     // what Forge 2-5 add to the Art
    std::vector<std::string> family_names_;      // the class's five weapon families (Forge's names)
    int ladder_base_ = 0, art_skill_ = 0;
    float art_range_ = 22.0f;
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
    std::map<std::string, std::pair<int, int>> closed_by_family_; // closed, spawned (measured families)
    std::vector<int> art_hits_per_kill_;                          // phase 1, Art-only defeats
    std::vector<std::uint64_t> level_up_steps_;
    std::vector<std::pair<std::uint64_t, std::string>> command_log_;
    std::vector<std::pair<std::uint64_t, std::string>> event_log_;
    std::string pad_log_;                                         // run-length "count:buttons/x/y" lines
    PadSample last_pad_{};
    std::uint64_t last_pad_count_ = 0;
    std::vector<MinuteLog> minutes_;
    int hits_taken_ = 0, misses_ = 0, clamps_ = 0;
    std::uint64_t no_control_ = 0;   // run steps (not the ending's) without control (14.2)
    double highest_hit_ = 0;
    int panels_min_gap_ = 1 << 30;
    std::int64_t last_panel_frame_ = -1;
    int last_panel_count_ = 0;
    std::uint64_t spawned_ = 0, corrupted_spawned_ = 0, recycled_ = 0, encircled_total_ = 0, spawn_failures_ = 0;
    int disks_taken_ = 0, disks_used_ = 0, plugs_picked_ = 0;
    std::string notice_;
    std::uint64_t notice_step_ = 0;
    // the kite bot
    int bot_a_ = 0, bot_l_ = 0;
    bool control_seen_ = false;
    int last_disks_ = 0;
};

} // namespace dw4::engine::overflow
