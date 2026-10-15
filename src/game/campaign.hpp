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
// for survival (campaign.cpp "Survival"): guard, touch escape and techniques
// without DW4_ONE_HIT; heals, heal gates, the disk shop, the bit capsule and
// training below gate levels without DW4_GOD.
#include "test_path.hpp"

#include <cstdint>
#include <map>
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
    };
    // `player` is null outside the game stages (logos, title, loading).
    Output update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame);

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
                    const std::string& follow, float keep, int frame, Output& out);
    // A maze floor swept (ExploreBot exhausted): on to another exit.
    void maze_turn(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);
    // Survival without god mode (campaign.cpp kSupplies): the next disk shop
    // purchase, the bit capsule recovered with a Raise Disk, the emergency
    // heal and panel upkeep laid over the frame's output.
    struct Purchase {
        int record = -1, count = 0, held = 0;
    };
    Purchase next_purchase(engine::Engine& engine) const;
    bool raise_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out);
    void start_disk(const engine::GameSystem::PlayerSlot& slot, int record, int frame);
    bool heal_gate_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out);
    float blow_due(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float horizon, int frame);
    std::optional<engine::Vec3f> touch_escape(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float& gap);
    bool tech_target(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const engine::GameSystem::PlayerSlot& slot) const;
    void survive(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out);
    Output play(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame);

    std::string stage_;
    int entry_ = -1;             // the player point the current stage was entered at
    int stage_frame_{};          // the frame the current stage was requested
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
    int guard_traced_ = -100000; // the last traced guard
    std::string guard_what_;     // the box guarded against (traces)
    struct BoxSeen {
        int frame = -1, prev_frame = -1;
        engine::Vec3f at{}, prev{};
    };
    std::map<std::pair<const void*, int>, BoxSeen> boxes_seen_; // attack boxes' world centres (their motion)
    int tech_at_ = -1;           // a technique cast began (R), -1 none
    int touch_traced_ = -100000; // the last traced one
    int gate_disk_at_ = -100000; // the last Gate Disk use
    int last_attack_ = 0;        // the last frame player 1 attacked (SQ_ATCK*, SQ_WAZA)
    int aggressive_until_ = -1;  // no guards until
    int guard_held_ = -100000;   // the last frame a guard held an attack back
    int tech_end_ = -100000;     // the last cast's end
    // Training (campaign.cpp kTraining).
    std::string objective_now_;  // the objective worked on (defeats raise its gate)
    std::map<std::string, int> defeat_levels_; // objective -> highest level a defeat at it came at
    int train_exp_ = -1, train_exp_at_{}; // EXP seen while training, when it last grew
    bool train_leaving_ = false; // the training field is spent: leaving it
    int gate_since_ = -1;        // walking to a heal gate since, -1 none
    std::string gate_mat_;       // its mat
    engine::Vec3f gate_at_{};    // where it is
    std::set<std::string> gate_tried_; // "<stage> <mat>" heal gates given up
};

// One step of a ride's targets (campaign.cpp kRideTargets: the stag0103
// boat's factory towers, the stag0203 train's junction signs) on the way to
// `goal` (the exit mats, '|'-separated; empty: any): the player faces and
// strikes them, fights enemies boarding the ride and guards against shots.
// False when nothing on the ride needs the player (the ExploreBot plays on).
// Used by the campaign and by SEEK explore:<mat> (the exit survey).
bool ride_targets_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& stage,
                       const std::string& goal, int frame, CampaignBot::Output& out);

} // namespace dw4::game
