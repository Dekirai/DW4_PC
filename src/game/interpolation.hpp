#pragma once

// Presentation above 60 frames per second: the game always advances in fixed
// 60 Hz steps, so between two steps the draw list repeats. The interpolator
// keeps the model, skinning and camera matrices of the last two steps and
// blends the drawn frame between them by the time since the last step (one
// step of latency). Items that appear, vanish or jump far (warps, camera
// cuts, stage changes) are drawn as they are.
#include "dw4/engine/scene_graph.hpp"

#include <cstdint>
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
    int jumps{};      // blended across a change far larger than the step before it (a loop wrapping)
    int palettes{}, palettes_blended{}, palettes_far{}, palettes_shared{}, palettes_resized{};
    bool camera{}, light{}; // the view / light view were blended
    struct Flag {
        const engine::DrawItem* item; // into the blended list (valid until it changes)
        const char* reason;           // "far", "swap", "jump", "palette_far", "palette_resized", "palette_shared"
        float distance;               // from the previous step's matrix (translation)
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

private:
    struct Key {
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
