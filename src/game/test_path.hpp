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

// Everything reachable on foot within `range` of a start point: a flood of
// the grid (heights follow the walked floor, so ramps and stairs are
// climbed, ledges are walked off and gaps over pits are jumped), then the
// cheapest routes, which keep away from walls (the original does not slide
// along walls: a player touching one stops).
class WalkMap {
public:
    WalkMap(const engine::CollisionWorld& world, const engine::Vec3f& from, float range, std::uint32_t wall_flags,
            const std::vector<AvoidBox>& avoid = {}, float width = 2.5f);
    // The reached floor point nearest to `p` (flat distance) if it lies
    // within `tolerance`.
    std::optional<engine::Vec3f> nearest(const engine::Vec3f& p, float tolerance) const;
    // The reached point inside an XZ rectangle nearest to its middle (a mat
    // that reaches into a wall still has floor inside).
    std::optional<engine::Vec3f> reached_inside(float min_x, float min_z, float max_x, float max_z) const;
    // The route cost to the reached point nearest to `p` within `tolerance`
    // (infinity without one): walking distance in cells plus wall and jump
    // surcharges.
    float cost_to(const engine::Vec3f& p, float tolerance) const;
    // Waypoints from the start to the reached point nearest to `p`; `jump`
    // marks a waypoint reached by jumping from the one before.
    struct Waypoint {
        engine::Vec3f point;
        bool jump = false;
    };
    std::vector<Waypoint> route_to(const engine::Vec3f& p) const;
    // Waypoints to the cheapest reached point within `tolerance` of `p`
    // (the same point cost_to prices), else as route_to.
    std::vector<Waypoint> route_to(const engine::Vec3f& p, float tolerance) const;
    // Reached floor points (every third grid cell).
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
    std::vector<float> cost_;
    std::map<std::pair<int, int>, Blocker> blockers_;
};

// SEEK explore:<mat>: plays a dungeon like a player heading for an exit:
// fights live enemies, picks up items, works gimmicks (stands on floor
// switches, presses wall switches, strikes boxes, breakable walls and
// towers), steps on the stage's other mats (they start the enemy waves
// whose defeat opens doors) and walks unvisited reachable ground until the
// route to the goal mat is free.
class ExploreBot {
public:
    struct Decision {
        float x{}, z{};     // where to walk
        float reach{4.0f};  // squared distance that counts as there
        bool attack{};      // press A (an enemy is in reach)
        bool confirm{};     // press A (standing on the goal mat)
        bool jump_attack{}; // the target floats above the player
        bool pick_up{};     // press A (standing at a pickup)
        bool press_a{};     // press A now (text windows, wall switches)
        bool press_b{};     // press B now (cancels another exit's prompt)
        bool jump{};        // press X now (a gap in the route)
    };
    Decision update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);

private:
    enum class Gimmick { none, strike, talk, step };
    void plan(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);
    void give_up(int frame);
    // A z-mat that moves the player to another room of the same stage
    // (its script runs trans.lua instead of funcStageChange): a maze link.
    bool portal(engine::Engine& engine, const std::string& mat);
    // A mat whose script changes the stage (funcStageChange), whatever its
    // name: other exits are kept away from.
    bool exit_mat(engine::Engine& engine, const std::string& mat);
    // A mat that hurts or drops the player (quicksand, a pit that removes
    // the player): kept away from, never a place to stand on.
    bool hazard_mat(engine::Engine& engine, const std::string& mat);
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
    engine::Vec3f goal_point_{};
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
    std::map<std::string, bool> hazards_;        // mat -> quicksand or pit (cached per stage)
    std::map<const void*, int> switched_;        // switch -> frame it was seen switched on
    std::map<const void*, int> health_;          // strike gimmick -> HP when last seen
    std::set<std::string> portals_taken_;        // maze links already walked through
    std::string target_portal_;                  // the maze link being walked to
    std::set<std::string> cleared_generators_;   // generators seen cleared (progress)
    int waiting_since_ = -1;                     // waiting for defeated enemies since
    int on_goal_ = 0;                            // steps standing on the goal mat with control
    int goal_later_until_ = -1;                  // the goal did nothing yet (a gate not up): other targets first
    int wait_skip_until_ = -1;                   // a wait that never ended is skipped until
    // Endless waves: after a long fight without progress elsewhere, only
    // nearby enemies are fought for a while.
    int progress_at_{};      // the last new ground, mat or finished gimmick
    int defend_until_ = -1;
    std::optional<engine::Vec3f> leap_; // the landing of the jump in progress
    int leap_frame_{};
};

} // namespace dw4::game
