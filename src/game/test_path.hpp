#pragma once

// Test-run helpers for the scripted SEEK bot (not part of the game): walking
// routes over the stage collision and an exploring player for dungeon runs.
// A grid of floor points, steps only between neighbours without a wall
// between them and without a large height step.
#include "dw4/engine/collision.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dw4::game {

// Waypoints from `from` to `to` (the last one is the goal); empty when no
// route was found within the searched area. A* over the area around both.
std::vector<engine::Vec3f> find_walk_path(const engine::CollisionWorld& world, const engine::Vec3f& from, const engine::Vec3f& to,
                                          std::uint32_t wall_flags);

// While alive, the players' limit circle (limit_Circle, a ring of walls
// round the players' centre that follows them) is left out of the stage
// collision: it never stops a single player, but a route planned ahead would
// end at it.
class WithoutLimitCircle {
public:
    explicit WithoutLimitCircle(engine::HitLandManager& hitland) : hitland_(hitland), saved_(hitland.child_hit("limit_Circle")) {
        if (saved_) hitland_.set_child_hit("limit_Circle", false);
    }
    ~WithoutLimitCircle() {
        if (saved_) hitland_.set_child_hit("limit_Circle", *saved_);
    }
    WithoutLimitCircle(const WithoutLimitCircle&) = delete;
    WithoutLimitCircle& operator=(const WithoutLimitCircle&) = delete;

private:
    engine::HitLandManager& hitland_;
    std::optional<bool> saved_;
};

// A box the walking map keeps out of (stage change and hazard mats): floor
// inside its XZ rectangle and height range.
struct AvoidBox {
    float min_x, min_z, max_x, max_z;
    float min_y = -1e30f, max_y = 1e30f;
};

// Enemy fire lanes for the walking map: grid cells (keys round(x / 5),
// round(z / 5)) with the floor heights at which a player standing there is
// in the fire. While a FireLanes guard is alive, every WalkMap built prices
// stepping into such a cell higher, so routes go round a lane where they can
// and cross it where they must.
using FireLaneCells = std::map<std::pair<int, int>, std::pair<float, float>>;
class FireLanes {
public:
    explicit FireLanes(const FireLaneCells& cells) { active_ = &cells; }
    ~FireLanes() { active_ = nullptr; }
    FireLanes(const FireLanes&) = delete;
    FireLanes& operator=(const FireLanes&) = delete;
    static const FireLaneCells* active() { return active_; }

private:
    static inline const FireLaneCells* active_ = nullptr;
};

// A moving platform for the walking map: the floor its top covers somewhere
// along its recorded path (a rectangle of the top's half extents round each
// recorded centre) counts as floor at its top height, reached and left only
// by a hop (a support change in the route). Its own collision (the floor
// check's info and child) is part of it wherever it stands now.
struct MovingFloor {
    std::vector<engine::Vec3f> centres; // recorded top centres (y: the top's height)
    float half_x{}, half_z{};
    std::uint16_t info = 0xFFFF, child{};
};

// Everything reachable on foot within `range` of a start point: a flood of
// the grid (heights follow the walked floor, so ramps and stairs are
// climbed, ledges are walked off and gaps over pits are jumped), then the
// cheapest routes, which keep away from walls (the original does not slide
// along walls: a player touching one stops).
class WalkMap {
public:
    WalkMap(const engine::CollisionWorld& world, const engine::Vec3f& from, float range, std::uint32_t wall_flags,
            const std::vector<AvoidBox>& avoid = {}, float width = 2.5f, const std::vector<MovingFloor>& moving = {});
    // The reached floor point nearest to `p` (flat distance) if it lies
    // within `tolerance`.
    std::optional<engine::Vec3f> nearest(const engine::Vec3f& p, float tolerance) const;
    // The reached point inside an XZ rectangle nearest to its middle (a mat
    // that reaches into a wall still has floor inside); the stage floor
    // first, or only.
    std::optional<engine::Vec3f> reached_inside(float min_x, float min_z, float max_x, float max_z, bool stage_floor_only = false) const;
    // Whether stage floor (not a moving platform's path) is reached within
    // `tolerance` of `p`.
    bool stage_floor_near(const engine::Vec3f& p, float tolerance) const;
    // The route cost to the reached point nearest to `p` within `tolerance`
    // (infinity without one): walking distance in cells plus wall and jump
    // surcharges.
    float cost_to(const engine::Vec3f& p, float tolerance) const;
    // Waypoints from the start to the reached point nearest to `p`; `jump`
    // marks a waypoint reached by jumping from the one before; `support` is
    // the moving floor (index into `moving`) the waypoint lies on, -1 the
    // stage floor (a change between two waypoints is a hop).
    struct Waypoint {
        engine::Vec3f point;
        bool jump = false;
        int support = -1;
    };
    std::vector<Waypoint> route_to(const engine::Vec3f& p) const;
    // Waypoints to the cheapest reached point within `tolerance` of `p`
    // (the same point cost_to prices), else as route_to.
    std::vector<Waypoint> route_to(const engine::Vec3f& p, float tolerance) const;
    // Reached stage floor points (every third grid cell; not the floor of
    // moving platforms, which is no ground to explore).
    std::vector<engine::Vec3f> samples() const;
    // The collision (info, child) pairs that stopped the walk, with counts.
    struct Blocker {
        int count{};
        engine::Vec3f first{}; // where it first stopped a step
        engine::Vec3f normal{};
    };
    const std::map<std::pair<int, int>, Blocker>& blockers() const { return blockers_; }

private:
    int nearest_cell(const engine::Vec3f& p) const;
    int nearest_reached(const engine::Vec3f& p, float tolerance) const;
    engine::Vec3f point(int cell) const;
    float x0_{}, z0_{};
    int w_{}, h_{};
    std::vector<float> floor_;
    std::vector<int> parent_;         // -2 unreached, -1 start
    std::vector<std::uint8_t> jumped_; // reached from the parent by a jump
    std::vector<std::int8_t> support_; // moving floor under the cell, -1 the stage floor
    std::vector<float> cost_;
    std::map<std::pair<int, int>, Blocker> blockers_;
};

// SEEK explore:<mat>: plays a dungeon like a player heading for an exit:
// fights live enemies, picks up items, works gimmicks (stands on floor
// switches, presses wall switches, strikes boxes, breakable walls and
// towers), steps on the stage's other mats (they start the enemy waves
// whose defeat opens doors) and walks unvisited reachable ground until the
// route to the goal mat is free. Moving platforms over pits are ridden:
// it waits for one to come by, hops on, rides it (out of trap lines) and
// hops off or on to the next where its route goes on.
class ExploreBot {
public:
    struct Decision {
        float x{}, z{};     // where to walk
        float reach{4.0f};  // squared distance that counts as there
        bool attack{};      // press A (an enemy is in reach)
        bool confirm{};     // press A (standing on the goal mat)
        bool jump_attack{}; // the target floats above the player
        bool fast_attack{}; // press A at every chance (the attack delay)
        bool pick_up{};     // press A (standing at a pickup)
        bool press_a{};     // press A now (text windows, wall switches)
        bool press_b{};     // press B now (cancels another exit's prompt)
        bool jump{};        // press X now (a gap in the route)
        bool press_r{};     // press R now (the technique panel)
    };
    Decision update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);
    // Nothing left to explore and the goal out of reach: waiting at the
    // nearest point (the campaign driver then tries another exit of a maze).
    bool exhausted() const { return closest_; }

private:
    enum class Gimmick { none, strike, talk, step, tech };
    void plan(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);
    void give_up(int frame);
    // A z-mat that moves the player to another room of the same stage
    // (its script runs trans.lua instead of funcStageChange): a maze link.
    bool portal(engine::Engine& engine, const std::string& mat);
    // A mat whose script changes the stage (funcStageChange or NEXTSTAGE), whatever its
    // name: other exits are kept away from.
    bool exit_mat(engine::Engine& engine, const std::string& mat);
    // A mat that drops the player into a pit (ADDHP and REMOVE): kept away
    // from, never a place to stand on.
    bool hazard_mat(engine::Engine& engine, const std::string& mat);
    // A quicksand mat (QuickSandOn/Off): walked over, but nothing to visit.
    bool quicksand_mat(engine::Engine& engine, const std::string& mat);
    // A trap mat: it switches a model's label while a player stands in it
    // (MATisPlayer, SETLABEL), e.g. a laser gun that fires across a pit or a
    // damage floor.
    bool trap_mat(engine::Engine& engine, const std::string& mat);
    // The line a trap mat's model fires along (its +Z, like the cannons the
    // flank approach keeps off), within the mat; the whole mat when the
    // model stands outside it. Kept out of while hopping and riding.
    struct Beam {
        engine::Vec3f origin{}, axis{}; // axis zero: the whole mat
        engine::Vec3f lo{}, hi{};       // the mat's box
    };
    const std::vector<Beam>& beams(engine::Engine& engine, int frame);
    // Moving platforms: models whose own collision is a floor (the floor
    // check under them answers with their name and index) that keep moving
    // sideways. Their path is recorded step by step: it repeats, so where
    // one will be is read from the same moment of an earlier round.
    struct Platform {
        const void* model{};
        std::uint16_t info = 0xFFFF, child{}; // its collision
        float top{};                          // the top's height above the origin
        engine::Vec3f offset{};               // the top's centre from the origin (X/Z)
        float half_x{}, half_z{};             // the top's half extents
        std::vector<engine::Vec3f> trail;     // the origin each step, oldest first
        int last_frame = -1;
        int moving_steps{};                   // steps it moved of the recorded ones
        bool hop_on{};                        // moves sideways and keeps moving (set by each plan)
        bool sideways() const;
    };
    void track_platforms(engine::Engine& engine, const engine::Vec3f& me, int frame);
    // The platform's top centre k steps on; `known` tells whether its path
    // showed it (else it is its present speed carried on).
    engine::Vec3f predict(const Platform& p, int k, bool* known = nullptr) const;
    // The platform (index) whose top is under p, -1 for the stage floor.
    int platform_under(engine::Engine& engine, const engine::Vec3f& p) const;
    // Hops between supports (the stage floor and moving platforms) along the
    // route: wait for the platform, run up, jump and steer at where the
    // landing will be. True while it steers the player.
    bool hop_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const engine::Vec3f& me, bool airborne,
                  int frame, Decision& decision);
    // The route waypoint a hop leads to from the support the player stands
    // on (or route_.size()).
    std::size_t pending_hop(int standing_on, const engine::Vec3f& me) const;
    std::string stage_;
    std::set<std::pair<int, int>> visited_;      // 20-unit cells the player stood in
    std::set<std::string> mats_done_;            // mats the player stood on
    std::map<const void*, int> unreachable_;     // target -> frame until which it is left alone
    std::map<const void*, int> struck_;          // gimmick -> frames spent attacking it, pressing it or standing on it
    std::map<const void*, int> struck_at_;       // gimmick -> frame its budget ran out (switches are tried again later)
    std::map<std::string, int> mat_frames_;      // mat -> steps the player has been inside it
    std::optional<engine::Vec3f> frontier_;      // the explored point being walked to
    int last_reset_ = -100000;                   // when the visited ground and mats were last forgotten
    std::vector<WalkMap::Waypoint> route_;
    std::size_t next_{};
    std::string what_;                           // the planned target, for traces and the watchdog
    const void* target_{};
    Gimmick gimmick_{};                          // how the target gimmick is worked
    std::string target_mat_;                     // the mat being walked to
    std::string target_radar_;                   // the radar generator whose base is walked to
    engine::Vec3f goal_point_{};
    // A boss whose body starts above the player's swing (Apocalymon over the
    // stagBS01 dais): the point next to its body on that floor the route
    // leads to (no swing reaches it from below, none is taken in the air).
    std::optional<engine::Vec3f> boss_stand_;
    // The post an enemy is fought from: for one hovering out of the swing
    // from its own floor (stag0404 e_impdra: body 13..28 above its root, the
    // swing reaches 0..8) reached floor near it high enough for the swing to
    // meet the body (no swing is taken in the air); for a turret a place next
    // to its body, behind it where that can be reached.
    std::optional<engine::Vec3f> perch_;
    std::map<std::pair<int, int>, int> perch_skip_; // post cell -> frame until which it is not used (nothing came in reach)
    int perch_since_ = -1;                         // standing on the post since
    int perch_hit_at_ = -1;                        // the last new HP low of the enemy fought from it
    // Enemy fire from turrets (stag0304 cannons): the cells their bullets
    // crossed lately with the floor heights at which a player there is hit,
    // and each turret's shot times (its volleys repeat). Routes avoid the
    // lanes where they can; where they must cross one, the bot waits out of
    // the fire for the long gap between two volleys.
    void track_fire(engine::Engine& engine, int frame);
    bool in_fire(const engine::Vec3f& p, int frame) const;
    struct FireCell {
        float lo{}, hi{};
        int frame{};
        const void* shooter{};
    };
    std::map<std::pair<int, int>, FireCell> fire_cells_;
    std::map<const void*, std::vector<int>> shots_;                       // turret -> frames of its last bullets' starts
    std::map<const void*, std::pair<engine::Vec3f, const void*>> bullets_; // live enemy bullet -> position, turret
    std::map<const void*, std::pair<engine::Vec3f, int>> standing_;      // enemy -> where it stands and since when
    std::map<const void*, float> bullet_speed_;                          // turret -> how far its bullets fly in a step
    std::map<const void*, std::pair<float, float>> turret_band_;         // turret -> floor heights its fire meets
    // The same for a crossing that starts with a jump up into a turret's
    // lane: a bullet keeps counting while its box list is empty for a step
    // (stag0304's x_bltcannon02 re-arms its box 10 frames after the shot,
    // which bullets_ takes for a bullet gone and then for a new shot), and a
    // shot is a bullet not seen the step before (the volleys repeat cleanly:
    // 3 shots 30 frames apart every 152 frames).
    std::map<const void*, std::pair<engine::Vec3f, const void*>> steady_bullets_; // live enemy bullet -> position, turret
    std::map<const void*, std::vector<int>> volleys_;                              // turret -> frames of its last shots
    // Gimmicks that stopped a turret's shot away from the player (the shot
    // ended at their body), with that turret: walls between its fire and
    // the way, which a no-cheat run leaves standing while the turret lives.
    std::map<const void*, const void*> shields_;
    std::map<const void*, int> shot_born_;  // enemy bullet of the steady view -> the frame its shot was first seen
    std::map<const void*, int> shot_life_;  // turret -> the longest flight of its shots so far (frames)
    // Each turret's firing sequence, as seen: the sequence its shots start
    // in, the shots' frames after that sequence's start and its longest run.
    // A turret hit by the player leaves it (stag0304's g_cannon02: SQ_SDMG,
    // then SQ_FIRE again with a shot at once), so the volleys' gaps alone
    // mislead right after a hit; the phase in the sequence does not.
    struct FirePhase {
        std::uint32_t serial{};  // the turret's sequence start being watched
        std::string seq;         // its sequence then
        int start = -1;          // the frame that sequence started
        std::string fire_seq;    // the sequence its shots start in
        std::vector<int> offsets; // shot frames after that sequence's start
        int length{};            // its longest run (frames until the next sequence)
    };
    std::map<const void*, FirePhase> phases_;
    // Posts given up for the stage (the third time a turret's fire kept the
    // player off): forgiven once the stage is explored to the end, since the
    // turret's body may fill the only way on (stag0304's climb to matz00);
    // a forgiven turret's walk to its post then counts only a quarter.
    std::set<const void*> unreached_;
    std::set<const void*> forgiven_;
    int fire_wait_since_ = -1;                                           // waiting for a gap in the fire since
    int fire_go_until_ = -1;                                             // crossing (or a wait that ran out): no waiting until
    bool fire_leap_to_post_{};                                           // the crossing's jump steers on to the turret's post
    int planned_ = -1000;
    bool to_goal_{};
    engine::Vec3f last_{};   // the player's position one step ago
    int stalled_{};          // steps without moving
    int escape_until_ = -1;  // stuck: sidestep until this frame
    int escape_turn_{};      // which way the last sidestep went
    int no_control_{};       // steps the player has had no control
    // Watchdog: a target that brings no progress for a while is given up
    // (kept per target, so two targets taking turns both run out).
    struct Watch {
        float best{};
        int since{}, seen{};
    };
    std::map<std::string, Watch> watch_;
    std::map<std::string, bool> portals_;        // mat -> is a maze link (cached per stage)
    std::map<std::string, bool> exits_;          // mat -> changes the stage (cached per stage)
    std::map<std::string, bool> hazards_;        // mat -> pit (cached per stage)
    std::map<std::string, bool> quicksands_;     // mat -> quicksand (cached per stage)
    std::map<std::string, bool> traps_;          // mat -> trap trigger (cached per stage)
    std::vector<Beam> beams_;                    // the trap lines (found again every 600 frames)
    int beams_at_ = -100000;
    std::map<std::string, int> clears_seen_;     // generator -> clears seen (restarting waves)
    engine::Vec3f wait_point_{};                 // where the wait for the defeated began
    std::string hold_mat_;                       // the mat whose wave is held
    int hold_since_ = -1;                        // the last plan that held it
    std::map<std::string, int> hold_left_;       // mat -> frames it may still be held
    std::string rejoin_gen_;                     // the stopped wave whose mat is walked back to
    int rejoin_since_ = -1;                      // the last plan that did
    std::map<std::string, int> rejoin_left_;     // generator -> frames its mat may still be walked back to
    std::map<std::string, int> mat_skip_;        // given-up mat -> frame until which it is skipped
    std::set<std::string> mats_ever_;            // mats ever stood on in this stage (forget keeps them)
    std::map<std::string, int> mat_done_at_;     // mat -> frame it was stood on
    int world_change_ = -1;                      // the last switch turned on or generator first cleared
    std::map<std::string, bool> gen_started_;    // generator -> started one step ago
    std::map<std::string, std::set<std::string>> gen_mats_; // generator -> mats the player stood in when it started
    bool goal_first_failed_{};                   // the goal was tried first and did nothing
    // DW4_MAT_SEQUENCE=m1,m2,...: mats stood on once each, in this order,
    // before anything but the goal (a switch puzzle's solution, given like a
    // preset); DW4_AVOID_MATS=<prefix>: the other mats of that prefix are kept
    // away from.
    std::vector<std::string> mat_sequence_;
    std::size_t mat_sequence_next_{};
    std::string avoid_mats_;

    std::map<const void*, int> switched_;        // switch -> frame it was seen switched on
    std::map<const void*, int> health_;          // strike gimmick -> lowest HP seen
    std::map<const void*, int> lowest_hp_;       // attacked enemy -> lowest HP seen
    std::map<const void*, int> fruitless_;       // attacked enemy -> attack steps since its last new low
    std::map<const void*, int> fruitless_runs_;  // enemy -> times it was left alone for taking no damage
    std::map<const void*, int> approach_;        // structure -> steps targeted without getting within reach
    std::set<const void*> unkillable_;           // enemies that heal themselves or took no damage thrice: left alone for the stage
    std::set<const void*> regenerating_;         // enemies seen healing back above their lowest HP
    std::set<const void*> vanished_;             // enemy RAMs seen free or defeated (a spawn into one is a new enemy)
    std::map<const void*, engine::Vec3f> enemy_last_; // attacked enemy -> position one step ago (moving ones are no vents)
    std::map<const void*, int> enemy_hp_;        // enemies' HP one step ago (kills count as progress)
    int kill_credits_{};                         // kills counted as progress since the last newly cleared generator
    // Walls a mat switches with the player's presence: XZ/Y bounds of each
    // HITLANDMODEL child (found by switching it once), cached per stage.
    struct ChildBox {
        bool found{};
        engine::Vec3f lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    };
    std::map<std::string, ChildBox> child_boxes_;
    std::set<std::string> portals_taken_;        // maze links already walked through
    std::string target_portal_;                  // the maze link being walked to
    std::set<std::string> cleared_generators_;   // generators seen cleared (progress)
    int waiting_since_ = -1;                     // waiting for defeated enemies since
    int on_goal_ = 0;                            // steps standing on the goal mat with control
    bool tech_used_{};                           // the technique of this panel round was cast
    std::set<const void*> switches_done_;        // switches seen on that no longer react: done for the stage
    std::map<std::string, int> radar_skip_;      // radar generator -> frame until which its base is left alone
    std::map<const void*, engine::Vec3f> model_last_; // nearby models' positions one step ago (platform speeds)
    int goal_later_until_ = -1;                  // the goal did nothing yet (a gate not up): other targets first
    int wait_skip_until_ = -1;                   // a wait that never ended is skipped until
    // Endless waves: after a long fight without progress elsewhere, only
    // nearby enemies are fought for a while.
    int progress_at_{};      // the last new ground, mat or finished gimmick
    int defend_until_ = -1;
    std::optional<engine::Vec3f> leap_; // the landing of the jump in progress
    int leap_frame_{};
    bool runup_ready_{};     // at the run-up point of a long jump: run and jump
    int platform_wait_since_ = -1; // waiting at a take-off for a moving platform since
    int jump_hold_since_ = -1;     // a row of jumps waits for the enemies by its landings since
    bool closest_{};         // the goal cannot be reached: wait at the nearest point
    std::vector<Platform> platforms_;                  // in the order first seen (the walking map's moving floors)
    std::map<const void*, engine::Vec3f> stirring_;    // other models' positions one step ago (platform candidates)
    std::set<const void*> not_platforms_;              // moved, but their floor is not their own
    // A hop under way: the run-up until `jump_at` (or the edge), then the
    // jump with the stick on the landing until touch-down at `land_at`.
    struct Hop {
        int from = -1, to = -1; // supports: platform index, -1 the stage floor
        int started{}, jump_at{}, land_at{};
        engine::Vec3f dir{};     // the run-up
        engine::Vec3f landing{}; // on the stage floor
        bool jumped{};
    };
    std::optional<Hop> hop_;
    int hop_wait_since_ = -1;                // waiting for a platform since
    std::vector<engine::Vec3f> hop_spots_;   // safe landings on the stage floor round the route's landing
    engine::Vec3f hop_spots_for_{1e30f, 0.0f, 1e30f};
    engine::Vec3f hop_wait_{};               // where to wait on the stage floor for a platform
    engine::Vec3f hop_wait_for_{1e30f, 0.0f, 1e30f};
    int hop_wait_to_ = -1;
};

} // namespace dw4::game
