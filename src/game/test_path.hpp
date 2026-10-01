#pragma once

// Test-run helper: walking routes for the scripted SEEK bot over the stage
// collision (not part of the game). A grid of floor points around the start
// and the goal, steps only between neighbours without a wall between them
// and without a large height step, searched with A*.
#include "dw4/engine/collision.hpp"

#include <vector>

namespace dw4::game {

// Waypoints from `from` to `to` (the last one is the goal); empty when no
// route was found within the searched area.
std::vector<engine::Vec3f> find_walk_path(const engine::CollisionWorld& world, const engine::Vec3f& from, const engine::Vec3f& to,
                                          std::uint32_t wall_flags);

} // namespace dw4::game
