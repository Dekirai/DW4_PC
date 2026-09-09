#pragma once

#include "dw4/meshes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dw4 {

// Provisional native point-grounding and locomotion for the playable preview.
// This is host behavior, not a reconstruction of the original movement engine.
// Inputs must be world-space floor meshes, preferably the original hit-land
// geometry. Optional wall meshes obstruct a swept horizontal body circle.
// Dynamic obstacles and gravity are absent.
// Both triangle windings are accepted; surfaces steeper than 55 degrees are
// excluded. A bounded X/Z hierarchy accelerates floor and swept-path queries.
// Per floor/wall set: 4,096 meshes, 1,048,576 vertices, 262,144 input triangles, coordinates
// within +/-1,000,000; 8,192 candidate overlaps and 256 transitions per query.
class WalkingWorld {
public:
    explicit WalkingWorld(std::span<const MeshData> meshes, std::span<const MeshData> walls = {});
    std::optional<float> find_floor(float x, float z, float reference_y,
        float max_up, float max_down) const;
    std::size_t triangle_count() const noexcept { return triangles_.size(); }
    std::size_t wall_triangle_count() const noexcept { return wall_triangles_.size(); }

private:
    friend class Walker;
    struct Triangle {
        std::array<double, 3> x, y, z;
        double denominator{};
        double min_x{}, max_x{}, min_z{}, max_z{};
    };
    struct Node {
        double min_x{}, max_x{}, min_z{}, max_z{};
        std::uint32_t begin{}, end{}, left{}, right{};
    };
    static std::vector<Triangle> read_triangles(std::span<const MeshData> meshes, bool walls);
    std::uint32_t build_node(std::uint32_t begin, std::uint32_t end, bool walls = false);
    std::vector<std::uint32_t> candidates(double min_x, double max_x,
        double min_z, double max_z, bool walls = false) const;
    std::optional<double> floor(double x, double z, double reference_y,
        double max_up, double max_down) const;
    bool blocked_by_walls(const std::array<double, 3>& from, const std::array<double, 3>& to,
        double radius, double bottom, double top) const;
    std::optional<double> follow_floor(double start_x, double start_z,
        double start_y, double end_x, double end_z,
        double max_up, double max_down) const;
    std::vector<Triangle> triangles_;
    std::vector<std::uint32_t> order_;
    std::vector<Node> nodes_;
    std::vector<Triangle> wall_triangles_;
    std::vector<std::uint32_t> wall_order_;
    std::vector<Node> wall_nodes_;
};

struct WalkingSettings {
    float speed = 80; // Original source-coordinate units per second.
    float step_up = 15;
    float step_down = 50;
    float spawn_snap_down = 200;
    float max_frame_seconds = 0.1f; // Discard long pauses instead of teleporting.
    float max_substep_distance = 1;
    float body_radius = 1.5f;
    float body_bottom = 1;
    float body_top = 6;
};

class Walker {
public:
    explicit Walker(WalkingSettings settings = {});
    // Reset snaps down to the nearest floor beneath the original entry point.
    // A missing floor throws; a failed reset preserves the previous state.
    void reset(Vec3 position, const WalkingWorld& world, float facing = 0);
    // Input is world X/Z, with radial length capped to one. Analog magnitudes
    // below one preserve their speed; callers supply any device dead zone.
    // Finite nonnegative dt is clamped, then movement uses bounded substeps.
    // Each substep requires continuous supporting triangles across its entire
    // segment, so a gap cannot be crossed by sampling only its far side.
    // Wall triangles are clipped to the swept body's vertical range. A blocked
    // diagonal can slide along one world axis; touching walls allow retreat.
    void advance(float input_x, float input_z, double dt, const WalkingWorld& world);
    Vec3 position() const noexcept;
    float facing() const noexcept { return facing_; } // Radians; zero faces +Z.
    bool moving() const noexcept { return moving_; }
    bool grounded() const noexcept { return grounded_; }

private:
    WalkingSettings settings_;
    std::array<double, 3> position_{};
    float facing_{};
    bool moving_{};
    bool grounded_{};
};

} // namespace dw4
