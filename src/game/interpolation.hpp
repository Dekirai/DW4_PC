#pragma once

// Presentation above 60 frames per second: the game always advances in fixed
// 60 Hz steps, so between two steps the draw list repeats. The interpolator
// keeps the model, skinning, texture and camera matrices of the last two
// steps and blends the drawn frame between them by the time since the last
// step (one step of latency). Items that appear, vanish or jump far (warps,
// camera cuts, stage changes) are drawn as they are.
//
// An item is the same item in both steps when its key matches: its placement
// (DrawItem::instance, the node path from the traversal's root, so the
// placements of a shared node never trade places when culling changes which
// of them are drawn), node and geometry, and its rank among the items with
// the same placement, node and geometry. Items whose sequences jumped
// between the steps (DrawItem::phase / texture_phase: a looping sequence
// wrapping, a stepped key; DrawItem::sequence_time / texture_sequence_time
// going back: a clock given an earlier origin) are not blended across that
// step.
#include "dw4/engine/scene_graph.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dw4::game {

// What one blend did (dev diagnostics, DW4_DEV_DUMP's presents.csv and
// blend_NNNNNN.txt; the blend itself does not depend on it).
struct BlendReport {
    int items{};      // draw items of the frame
    int blended{};    // drawn between the two steps
    int unmatched{};  // no item with the key in the previous step: drawn as they are
    int far{};        // a key match too far away (warp, sudden turn): drawn as they are
    int swapped{};    // blended from another occurrence's previous place (a nearer one exists)
    int jumps{};      // blended across a change far larger than the step before it
    int wraps{};      // a sequence above the item jumped (DrawItem::phase, sequence_time): drawn as it is
    int palettes{}, palettes_blended{}, palettes_far{}, palettes_shared{}, palettes_resized{};
    int textures{}, textures_blended{}; // texture matrices in both steps / blended
    bool camera{}, light{}; // the view / light view were blended
    // The camera's change (eye distance, largest axis change) from the
    // previous step and the one before it (-1: no step before).
    float camera_move{}, camera_turn{}, camera_move_before = -1, camera_turn_before = -1;
    struct Flag {
        const engine::DrawItem* item; // into the blended list (valid until it changes)
        const char* reason;           // "far", "swap", "jump", "wrap", "texture_jump", "palette_far", "palette_resized", "palette_shared"
        float distance;               // from the previous step's matrix (translation; palette_far: the bones drawn as they are)
        float turn{};                 // largest change of the 3x3 part from the previous step
        float turn_before{};          // the same from the step before ("jump")
    };
    std::vector<Flag> flagged;
};

class MotionInterpolator {
public:
    // The simulation advanced: the state captured last becomes the previous one.
    void advance();
    // Captures the current state from a freshly built frame (once per step).
    void capture(const std::vector<engine::DrawItem>& items, const engine::FrameCamera& camera,
                 const engine::ShadowFrame& shadow);
    bool captured() const { return captured_; }
    // Blends the frame from the previous toward the current state; alpha 0 is
    // the previous step, 1 the current one.
    void blend(std::vector<engine::DrawItem>& items, engine::FrameCamera& camera, engine::ShadowFrame& shadow,
               float alpha, BlendReport* report = nullptr) const;
    void reset();
    // The previous step's camera view, while there is one to blend from: the
    // picture's cull also draws what lies inside that frustum
    // (Engine::picture_previous_view), so nothing at the trailing edge of a
    // turning camera leaves the blended picture early.
    std::optional<engine::Mat4> previous_view() const;

    // The blend of two placements (public for the tests): element by element,
    // with each axis (row of the 3x3 part) kept at the length between the two
    // axes' lengths, so a turn does not shrink the item halfway.
    static engine::Mat4 blend_matrix(const engine::Mat4& a, const engine::Mat4& b, float t);
    // The blend of two camera views: the camera's placement (the inverse
    // view) blended as above and inverted back, so the eye moves on the line
    // between its two positions and the picture does not shrink in a turn.
    static engine::Mat4 blend_view(const engine::Mat4& a, const engine::Mat4& b, float t);

private:
    struct Key {
        std::uint64_t instance{};
        const void* node{};
        const void* geometry{};
        std::uint32_t occurrence{};
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };
    struct State {
        engine::Mat4 model;
        std::shared_ptr<const std::vector<engine::Mat4>> palette;
        std::optional<engine::Mat4> texture_matrix;
        std::uint32_t phase{}, texture_phase{};
        std::int64_t sequence_time{}, texture_sequence_time{};
    };
    using Map = std::unordered_map<Key, State, KeyHash>;
    struct Snapshot {
        Map items, casters;
        engine::Mat4 view, light_view;
        bool valid{};
    };
    static void record(Map& map, const std::vector<engine::DrawItem>& items);
    static void apply(const Map& previous, std::vector<engine::DrawItem>& items, float alpha, BlendReport* report,
                      const Map* older = nullptr);

    // (older_: the step before previous_, only for BlendReport's jumps.)
    Snapshot older_, previous_, current_;
    bool captured_{};
};

} // namespace dw4::game
