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

// An XZ rectangle the walking map keeps out of (stage change mats).
struct AvoidBox {
    float min_x, min_z, max_x, max_z;
};

// Everything reachable on foot within `range` of a start point: a flood of
// the grid (heights follow the walked floor, so ramps and stairs are
// climbed), then the cheapest routes, which keep away from walls (the
// original does not slide along walls: a player touching one stops).
class WalkMap {
public:
    WalkMap(const engine::CollisionWorld& world, const engine::Vec3f& from, float range, std::uint32_t wall_flags,
            const std::vector<AvoidBox>& avoid = {}, float width = 2.5f);
    // The reached floor point nearest to `p` (flat distance) if it lies
    // within `tolerance`.
    std::optional<engine::Vec3f> nearest(const engine::Vec3f& p, float tolerance) const;
    // Waypoints from the start to the reached point nearest to `p`.
    std::vector<engine::Vec3f> route_to(const engine::Vec3f& p) const;
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
    engine::Vec3f point(int cell) const;
    float x0_{}, z0_{};
    int w_{}, h_{};
    std::vector<float> floor_;
    std::vector<int> parent_; // -2 unreached, -1 start
    std::map<std::pair<int, int>, Blocker> blockers_;
};

// SEEK explore:<mat>: plays a dungeon like a player heading for an exit:
// fights live enemies, picks up items, strikes gimmicks that take hits
// (buttons, breakable walls, boxes), steps on the stage's other mats (they
// start the enemy waves whose defeat opens doors) and walks unvisited
// reachable ground until the route to the goal mat is free.
class ExploreBot {
public:
    struct Decision {
        float x{}, z{};     // where to walk
        float reach{4.0f};  // squared distance that counts as there
        bool attack{};      // press A (an enemy is in reach)
        bool confirm{};     // press A (standing on the goal mat)
        bool jump_attack{}; // the target floats above the player
        bool pick_up{};     // press A (standing at a pickup)
        bool press_a{};     // press A now (text windows, the answer to a prompt)
        bool press_down{};  // hold down now (prompt cursor to "No")
    };
    Decision update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);

private:
    void plan(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame);
    std::string stage_;
    std::set<std::pair<int, int>> visited_;      // 20-unit cells the player stood in
    std::set<std::string> mats_done_;            // mats the player stood on
    std::map<const void*, int> unreachable_;     // target -> frame it was found unreachable
    std::map<const void*, int> struck_;          // gimmick -> frames spent attacking it
    bool striking_{};                            // the target is a gimmick
    std::vector<engine::Vec3f> route_;
    std::size_t next_{};
    std::string what_;                           // the planned target, for traces
    const void* target_{};
    engine::Vec3f goal_point_{};
    int planned_ = -1000;
    bool to_goal_{};
    engine::Vec3f last_{}; // the player's position one step ago
    int stalled_{};        // steps without moving
    int escape_until_ = -1; // stuck: sidestep until this frame
    int escape_turn_{};    // which way the last sidestep went
    int no_control_{};     // steps the player has had no control
    bool declined_{};      // an unwanted stage change prompt was answered
};

} // namespace dw4::game
