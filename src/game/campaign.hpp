#pragma once

// Test driver (not part of the game): "frame SEEK duration campaign" plays
// the story from a new game the way a player would. It boots through the
// logos and the title, creates a character, then works through the story
// objectives (src/game/campaign.cpp, kObjectives) in order: each objective
// names the stage where it is done, the action that does it there and the
// game state (story flags, key items) that says it is done. Between stages
// it follows the stage exit graph (kExits): lobby gates are walked to and
// their menus answered, fields and dungeons are handed to the ExploreBot
// with the exit mat as its goal. Nothing in the game is changed: the story
// flags are set by the game's own scripts. Without the test cheats it plays
// for survival (campaign.cpp "Survival"): techniques and MP disks without
// DW4_ONE_HIT; guards, heals, heal gates, Gate Disks, the lobby shops, the
// bit capsule and training below gate levels without DW4_GOD. With
// DW4_CAMPAIGN_CHECKPOINTS it saves the game at checkpoints of the story,
// with DW4_CAMPAIGN_LOAD it calls such a save at the title (segment runs,
// campaign.cpp "Checkpoints").
#include "test_path.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game {

class CampaignBot {
public:
    struct Output {
        std::uint32_t buttons{}; // pad_button bits for player 1
        bool walk{};             // steer the stick toward (x, z)
        float x{}, z{};
        float reach{4.0f};       // squared distance that counts as there
        bool ride_strike{};      // A is for a ride's target passing by (survive does not hold it back)
    };
    // `player` is null outside the game stages (logos, title, loading).
    Output update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame);
    // Segment runs (campaign.cpp "Checkpoints"): true once the run has done
    // what DW4_CAMPAIGN_STOP asks for (the caller ends the test run).
    bool finished() const { return segment_.finished; }
    // The driver's memory that outlives a stage (defeat levels, shop and lab
    // errands done, heal gates given up, visits, cooldowns) as text lines, so
    // a run resumed from a stage state goes on as the run that saved it
    // (test tooling, see campaign_memory_save).
    void save_memory(std::ostream& out) const;
    void load_memory(std::istream& in);

private:
    struct Step {
        std::string stage;   // where the step is taken
        std::string action;  // explore:<mats> | mat:<mat> | talk:/strike:/step:<model>#<index> | wait
        int choice = -1;     // the entry of a lobby gate's area list, -1 = none
        std::string why;     // the objective it serves (traces)
    };
    std::optional<Step> next_step(engine::Engine& engine) const;
    Output boot(engine::Engine& engine, int frame);
    // Answers an open cursor menu of player 1; false when none takes input.
    bool answer_menu(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const Step& step, int frame,
                     Output& out);
    // Walks over the stage collision to `goal` (route replanned every
    // second); true once within `close`.
    bool walk_to(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const engine::Vec3f& goal, float close,
                 int frame, Output& out);
    const engine::ModelCtrlManager::ModelRam* find_model(engine::Engine& engine, const std::string& spec, const engine::Vec3f& near,
                                                         int frame = 0) const;
    // One frame of a boss fight (kBossFights); false when the boss is down.
    bool boss_fight(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& strike,
                    const std::string& follow, float keep, bool dodge, float arena_x, float arena_z, float arena_r, bool clear_touch,
                    const char* omen, const char* homing, bool lead, const char* rings, bool shaped, int frame, Output& out);
    // A maze floor swept (ExploreBot exhausted): on to another exit.
    void maze_turn(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);
    // Survival without god mode (campaign.cpp kSupplies): the next disk shop
    // purchase, the bit capsule recovered with a Raise Disk, the emergency
    // heal and panel upkeep laid over the frame's output.
    struct Purchase {
        int record = -1, count = 0, held = 0;
    };
    Purchase next_purchase(engine::Engine& engine) const;
    int planned_spend(engine::Engine& engine) const;
    // The next device to equip at the Digi-Lab (campaign.cpp kEquipGain):
    // kind 0 the weapon in use (POWERSOCKET 0), 1 the board (MAINSLOT), 2 the
    // accessory (SUBSLOT), 3 a PWR Medal into power pocket `pocket` of the
    // weapon in use; device -1 when the equipment is the best held.
    struct EquipPick {
        int kind = -1, device = -1, pocket = -1;
    };
    EquipPick next_equip(engine::Engine& engine) const;
    double equip_value(engine::Engine& engine, const engine::ItemDevice& d) const;
    // The stock line of a device shop (6 weapons, 7 armor, 8 chips) to buy, -1 none.
    int next_gear(engine::Engine& engine, int program) const;
    // The next device to sell at the disk shop (campaign.cpp kSellFrom), -1 none.
    int next_sale(engine::Engine& engine) const;
    bool raise_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out);
    void start_disk(const engine::GameSystem::PlayerSlot& slot, int record, int frame);
    bool heal_gate_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out);
    float blow_due(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float horizon, int frame);
    std::optional<engine::Vec3f> touch_escape(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float& gap, int frame);
    void track_step(const engine::ModelCtrlManager::ModelRam& player, int frame);
    bool guard_pulse(int frame);
    bool guard_footing(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, bool deep = true) const;
    // Whether player 1 stands on a floor that gave way before (fading_floors_).
    bool on_fading_floor(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player) const;
    bool tech_target(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const engine::GameSystem::PlayerSlot& slot) const;
    void survive(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out);
    Output play(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame);

    std::string stage_;
    int entry_ = -1;             // the player point the current stage was entered at
    int stage_frame_{};          // the frame the current stage was requested
    int player_traced_visit_ = -1; // the stage visit (its stage_frame_) the character was traced in
    std::string traced_;         // the last traced step
    int traced_at_{};            // the frame it began
    ExploreBot explorer_;
    std::vector<WalkMap::Waypoint> route_;
    std::size_t route_next_{};
    engine::Vec3f route_goal_{1e9f, 0, 1e9f};
    int route_frame_ = -100000;
    engine::Vec3f last_{};
    int stalled_{}, escape_until_ = -1, escape_turn_{};
    int no_control_{};           // steps player 1 has had no control
    int menu_pressed_ = -100;    // the last frame a menu button went down
    int waited_{};               // steps spent on a `wait` step
    int stuck_since_ = -1;       // the step was found impossible since (traces)
    int key_since_ = -1;         // walking to a key item since
    int key_skip_until_ = -1;    // a key item out of reach is left to the ExploreBot until
    int lab_target_ = -1;        // the form the open Digi-Lab is asked for (evolve:<n>)
    bool lab_leaving_ = false;   // Exit was chosen in the lab menu
    const void* foe_ = nullptr;  // the enemy fought outside the ExploreBot
    int foe_since_{}, foe_hp_{}; // since when without losing HP, its HP then
    int foe_seen_ = -1000;       // the last frame a foe was fought
    std::map<const void*, int> foe_left_; // enemies left alone until
    std::map<const void*, std::pair<std::uint32_t, int>> omen_seen_; // a boss fight's omen markers: sequence serial, first frame
    const void* target_ = nullptr; // the model a talk/strike step walks to
    int target_since_{};          // since when (not reached)
    std::map<const void*, int> target_skip_; // "#*" targets not reached, left alone until
    std::map<std::pair<std::string, int>, int> place_visits_; // arrivals per (stage, player point)
    std::string maze_goal_;      // the exit a swept maze floor is left by
    std::map<std::pair<std::string, int>, std::size_t> swept_; // maze places swept, with the flags then
    int exhausted_since_ = -1;   // the ExploreBot has had nothing left to explore since
    int maze_since_ = -1;        // in this maze place (or since its last turn) since
    std::string region_now_;     // the maze region the player was last found in (kMazeRegions)
    int region_check_{};         // when it was looked for
    // Survival without god mode.
    std::map<int, int> prices_;  // conItem record -> HANBAI price (read where the table is loaded)
    bool shop_open_ = false;     // a shop errand's program (SUBWND DISK) may be running
    bool heal_open_ = false;     // the X-Data shop (a full heal on leaving) may be running
    int heal_at_ = -100000;      // the last emergency heal (Y)
    int panel_close_at_ = -100000; // the last R that closes a panel left open
    int disk_use_ = -1;          // the disk being used from the L panel (conItem record), -1 none
    int disk_use_at_{};          // its use began (L)
    int disk_line_{};            // its line in the panel
    int disk_ups_{};             // UP presses that reach the panel's top
    bool disk_quick_{};          // used with Y (the emergency heal) instead
    int disk_held_{};            // its count when the use began
    int raise_tries_{};          // Raise Disk uses tried in this stage
    int raise_walk_since_ = -1;  // walking to the bit capsule since
    bool alive_ = false;         // player 1 was alive in the last game frame
    int deaths_{};               // defeats so far
    int fallen_at_ = -1;         // player 1's HP reached 0 here, not yet known as a defeat (a pit respawns the player)
    int fallen_level_{};         // its level then
    std::string fallen_objective_; // the objective then
    std::string fallen_stage_;   // the stage then
    int guard_traced_ = -100000; // the last traced guard
    std::string guard_what_;     // the box guarded against (traces)
    struct BoxSeen {
        int frame = -1, prev_frame = -1;
        engine::Vec3f at{}, prev{};
    };
    std::map<std::pair<const void*, int>, BoxSeen> boxes_seen_; // attack boxes' world centres (their motion)
    bool blow_shot_ = false;     // the blow blow_due found last is a shot in flight
    bool blow_effect_ = false;   // ... or a box of an effect (no enemy's own body: a turret's shell or beam)
    std::set<std::string> hurting_effects_; // effect models seen with an enemy attack box (blow_due: shots still queued)
    std::string guard_seq_;      // player 1's sequence in the last frame (survive: idle guards)
    int idle_guards_ = 0;        // guards in a row that blocked nothing while touched
    bool blow_queued_ = false;   // the blow blow_due found last is a shot still queued on a shooter within a swing
    engine::Vec3f me_seen_{}, me_step_{}; // player 1's position when blow_due last ran, its step in the frame before
    int me_seen_frame_ = -1;
    int guard_pulse_from_ = -100, guard_pulse_last_ = -100; // the B pulse of survive's guards (guard_pulse)
    engine::Vec3f guard_from_{}; // where the blow last found by blow_due / touch_escape comes from (its box centre)
    int tech_at_ = -1;           // a technique cast began (R), -1 none
    int touch_traced_ = -100000; // the last traced one
    int gate_disk_at_ = -100000; // the last Gate Disk use
    int gate_refused_at_ = -1;   // the stage visit (its stage_frame_) whose panel refused the Gate Disk
    int last_attack_ = 0;        // the last frame player 1 attacked (SQ_ATCK*, SQ_WAZA)
    int aggressive_until_ = -1;  // no guards until
    int held_presses_ = 0;       // presses of A the guard took back since the last attack
    int guard_held_ = -100000;   // the last frame a guard held an attack back
    int tech_end_ = -100000;     // the last cast's end
    int blade_frame_ = -1;       // the last frame of a boss fight that takes swings only (BossFight::blade)
    int panel_ready_at_ = -100000; // the panel's FIRE wait after the last pick runs out (survive)
    int last_hp_ = -1;           // player 1's HP in the last frame (survive)
    int big_hit_ = 0, big_hit_at_ = -100000; // the biggest blow taken lately, when
    const void* focus_seen_ = nullptr; // a boss fight's focus in the last frame (BossFight::lead) ...
    engine::Vec3f focus_at_{};   // ... where it stood ...
    int focus_frame_ = -1;       // ... and when
    int dodge_frame_ = -1;       // the last frame a boss fight's dodge saw a box about to meet the player (survive: no slow heal)
    std::string exit_to_;        // the stage the current step's exit mat leads to (kExits; survive tops the HP up before a stage without heals)
    std::set<std::string> no_panel_stages_; // stages seen without the quick panels (survive), beside kNoHealStages
    std::set<int> flags_seen_;   // story flags seen on so far (a visit that sets a new one made progress)
    bool flags_init_ = false;    // flags_seen_ filled with the flags on when the driver started
    int new_flag_visit_ = -1;    // the stage visit (its stage_frame_) that last set a flag never seen before
    int new_flag_ = -1;          // that flag
    std::map<std::string, int> progress_trips_; // objective -> Gate Disk trips out of it with progress (no gate raised)
    std::map<std::string, int> restock_trips_;  // objective -> Gate Disk trips back to restock on the way to it (kRestockTrips)
    std::string restock_pending_;               // the objective of a restocking trip started, counted back in the lobby
    int heal_stock_percent(engine::Engine& engine) const; // the heals held against a full stock (kRestockBelow)
    int stock_at_entry_ = -1;                   // heal_stock_percent when the stage visit stock_visit_ began
    int stock_visit_ = -1;
    engine::Vec3f grounded_at_{};               // where the player last stood with control (a fall's start)
    int grounded_frame_ = -1;
    std::string grounded_floor_;                // the HitLand info of that floor (empty: the stage's own)
    std::set<std::string> fading_floors_;       // HitLand infos of floors a fall began from: crossed without a stop
    // The equipment errand at the Digi-Lab (next_equip).
    bool equip_open_ = false;    // its lab program may be running
    int equip_kind_ = -1;        // the slot kind chosen in the lab's equipment menu
    int list_page_ = 0;          // a device list's page (lab or sale list, counted from its opening)
    int list_turn_ = 0, list_turn_at_ = -100000; // a page turn pressed (+1 / -1), when
    bool equip_folder_ = false;  // the device list was open in the last frame
    bool equip_pockets_ = false; // Chips was chosen for the weapon in use (the pocket cursor and its menus follow)
    EquipPick equip_tried_{};    // the pick the errand began with
    int equip_since_ = -1;       // the errand's step since
    std::set<std::pair<int, int>> equip_failed_; // (kind, device record) picks the lab did not take
    // Device sales at the disk shop (next_sale).
    bool shop_selling_ = false;  // Sell was chosen in the shop menu
    bool sale_confirm_ = false;  // a device was picked from the sale list (its yes follows)
    int sale_target_ = -1;       // that device
    int sale_at_ = -1;           // a device sold (the list's page follows the folder)
    // The bank (campaign.cpp kBankDust).
    bool bank_open_ = false;     // a bank errand's program may be running
    bool bank_deposit_ = false;  // it deposits (else withdraws)
    bool bank_ran_ = false;      // its program was seen running
    bool bank_withdrawn_ = false; // the bits came out in this stay in the lobby
    int bank_tries_ = 0;         // bank visits in this stay
    int bank_ups_ = 0;           // UP presses on the amount's top digit
    int bank_digits_at_ = -100000; // the amount's digit cursor last seen
    int bank_lists_ = 0;           // lists backed out of in this bank visit (two: no bank, see answer_menu)
    // Lobby errands that stalled (kErrandStall): kinds left until the next
    // lobby room, and the stalls per kind in this stay.
    std::set<std::string> errands_paused_;
    std::map<std::string, int> errand_stalls_;
    bool errand_off(const std::string& kind) const;
    int bank_list_at_ = -100000;
    // Gear from the weapon, armor and chip shops (next_gear).
    bool gear_open_ = false;     // a device shop program of the errand may be running
    int gear_program_ = 6;       // its program (6 weapons, 7 armor, 8 chips, 9 the junk shop)
    bool gear_buying_ = false;   // Buy was chosen: the stock list
    bool gear_ran_ = false;      // its program was seen running
    std::array<int, 4> gear_checked_{-1, -1, -1, -1}; // the level each shop was last looked at
    std::array<int, 4> gear_visit_{-1, -1, -1, -1};   // the stage visit (its stage_frame_) each shop last ran and closed in
    std::map<std::string, std::pair<int, int>> gear_restocks_; // gear objective -> (level, fresh-stock trips at it), kGearRestocks
    // The junk shop (campaign.cpp kProtections).
    int junk_visits_ = 0;          // its visits in this stay in the lobby
    int junk_ups_ = 0;             // UP presses on the donation's thousands digit
    int junk_digits_at_ = -100000; // the donation's digit cursor last seen
    // Training (campaign.cpp kTraining).
    std::string objective_now_;  // the objective worked on (defeats raise its gate)
    std::map<std::string, int> defeat_levels_; // objective -> highest level a defeat (or a Gate Disk escape) at it came at
    std::map<std::string, int> train_falls_;   // training field -> highest level a defeat there came at (train_for)
    int escape_stage_frame_ = -1; // the stage visit a Gate Disk escape raised the gate in
    int train_exp_ = -1, train_exp_at_{}; // EXP seen while training, when it last grew
    bool train_leaving_ = false; // the training field is spent: leaving it
    std::string stall_leave_;    // a field step stalled: the exit mat it is left by for this visit (kStallLeave)
    int gate_since_ = -1;        // walking to a heal gate since, -1 none
    std::string gate_mat_;       // its mat
    engine::Vec3f gate_at_{};    // where it is
    std::set<std::string> gate_tried_; // "<stage> <mat>" heal gates given up

    // Checkpoints and segment runs (campaign.cpp "Checkpoints"): saves of
    // the game on the memory card at points of the story, and runs that call
    // such a save at the title and play on to the next one. Kept apart from
    // the members above: after a checkpoint save the driver forgets them all
    // (checkpoint_forget), only this survives.
    struct Segment {
        bool began = false;            // the game (new or loaded) has begun
        int began_at = -1;             // in that frame
        std::set<std::string> held;    // checkpoints already behind the game when it began
        std::set<std::string> saved;   // checkpoints saved in this run
        std::string due;               // the checkpoint being saved now, empty none
        int due_since = -1;            // when it came due
        int tries = 0;                 // save room visits for it
        std::string card_before;       // Card A's save file before the save room (unchanged = not saved)
        std::string last_stage;        // the stage of the last frame
        bool story_done = false;       // flag 86 reported
        bool finished = false;         // DW4_CAMPAIGN_STOP: the run's work is done
    };
    Segment segment_;
    void checkpoint_update(engine::Engine& engine, int frame);
    std::optional<Output> checkpoint_boot(engine::Engine& engine, int frame);
    void checkpoint_saved(engine::Engine& engine, int frame);
    void checkpoint_forget();
};

// Test tooling for resumed runs: the live campaign driver's memory written
// next to a stage state (DW4_STAGE_STATES: <frame>_<stage>.mem beside the
// .dw4state), and read back with --load-state (that state's .mem, when there
// is one) before the driver's next update. No-ops without a driver.
void campaign_memory_save(const std::filesystem::path& path);
void campaign_memory_load(const std::filesystem::path& path);

// One step of a ride's targets (campaign.cpp kRideTargets: the stag0103
// boat's factory towers, the stag0203 train's junction signs) on the way to
// `goal` (the exit mats, '|'-separated; empty: any): the player faces and
// strikes them, fights enemies boarding the ride and guards against shots.
// False when nothing on the ride needs the player (the ExploreBot plays on).
// Used by the campaign and by SEEK explore:<mat> (the exit survey).
bool ride_targets_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& stage,
                       const std::string& goal, int frame, CampaignBot::Output& out);

} // namespace dw4::game
