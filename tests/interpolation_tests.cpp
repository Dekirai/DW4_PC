#include "../src/game/interpolation.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 1e-4f; }

engine::DrawItem item_at(const engine::Node* node, const engine::GeometryData* geometry, float x, std::uint64_t instance = 0) {
    engine::DrawItem item;
    item.node = node;
    item.geometry = geometry;
    item.model = engine::translation(x, 0, 0);
    item.instance = instance;
    return item;
}

engine::Mat4 turn_y(float radians) {
    engine::Mat4 m;
    m(0, 0) = std::cos(radians);
    m(0, 2) = -std::sin(radians);
    m(2, 0) = std::sin(radians);
    m(2, 2) = std::cos(radians);
    return m;
}

// One step from `before` to `after`, blended at `alpha`.
std::vector<engine::DrawItem> step(const std::vector<engine::DrawItem>& before, const std::vector<engine::DrawItem>& after,
                                   float alpha, game::BlendReport* report = nullptr) {
    engine::FrameCamera camera;
    engine::ShadowFrame shadow;
    game::MotionInterpolator interpolator;
    interpolator.capture(before, camera, shadow);
    interpolator.advance();
    interpolator.capture(after, camera, shadow);
    auto blended = after;
    interpolator.blend(blended, camera, shadow, alpha, report);
    return blended;
}

void test_blend_between_steps() {
    engine::Node node;
    engine::GeometryData geometry;
    engine::FrameCamera camera;
    engine::ShadowFrame shadow;
    game::MotionInterpolator interpolator;
    std::vector<engine::DrawItem> items{item_at(&node, &geometry, 0.0f)};
    interpolator.capture(items, camera, shadow);
    interpolator.advance();
    items = {item_at(&node, &geometry, 2.0f)};
    interpolator.capture(items, camera, shadow);
    check(interpolator.captured(), "A frame is captured once per step");
    auto blended = items;
    interpolator.blend(blended, camera, shadow, 0.25f);
    check(near(blended[0].model(3, 0), 0.5f), "A quarter step blends a quarter of the way");
    blended = items;
    interpolator.blend(blended, camera, shadow, 1.0f);
    check(near(blended[0].model(3, 0), 2.0f), "A full step shows the current state");
}

void test_jumps_and_new_items() {
    engine::Node node, other;
    engine::GeometryData geometry;
    engine::FrameCamera camera;
    engine::ShadowFrame shadow;
    game::MotionInterpolator interpolator;
    std::vector<engine::DrawItem> items{item_at(&node, &geometry, 0.0f)};
    interpolator.capture(items, camera, shadow);
    interpolator.advance();
    items = {item_at(&node, &geometry, 500.0f), item_at(&other, &geometry, 3.0f)};
    interpolator.capture(items, camera, shadow);
    auto blended = items;
    interpolator.blend(blended, camera, shadow, 0.5f);
    check(near(blended[0].model(3, 0), 500.0f), "A warp is drawn where it ends");
    check(near(blended[1].model(3, 0), 3.0f), "A new item is drawn as it is");
}

void test_repeated_instances() {
    engine::Node node;
    engine::GeometryData geometry;
    engine::FrameCamera camera;
    engine::ShadowFrame shadow;
    game::MotionInterpolator interpolator;
    std::vector<engine::DrawItem> items{item_at(&node, &geometry, 0.0f), item_at(&node, &geometry, 10.0f)};
    interpolator.capture(items, camera, shadow);
    interpolator.advance();
    items = {item_at(&node, &geometry, 1.0f), item_at(&node, &geometry, 12.0f)};
    interpolator.capture(items, camera, shadow);
    auto blended = items;
    interpolator.blend(blended, camera, shadow, 0.5f);
    check(near(blended[0].model(3, 0), 0.5f) && near(blended[1].model(3, 0), 11.0f),
          "Instances of one node blend in drawing order");
}

// The placements of a shared node (DrawItem::instance) keep their own keys:
// when the first one is culled, the second one does not take its place
// (the stagML11 pillars and benches slid 20-40 units for a step).
void test_instances_keep_their_keys() {
    engine::Node node;
    engine::GeometryData geometry;
    const auto blended = step({item_at(&node, &geometry, 0.0f, 11), item_at(&node, &geometry, 20.0f, 22)},
                              {item_at(&node, &geometry, 21.0f, 22)}, 0.5f);
    check(blended.size() == 1 && near(blended[0].model(3, 0), 20.5f), "A placement blends from its own previous place");
    // A placement that appears in front of the others is new, not a neighbour.
    const auto added = step({item_at(&node, &geometry, 20.0f, 22)},
                            {item_at(&node, &geometry, 0.0f, 11), item_at(&node, &geometry, 20.0f, 22)}, 0.5f);
    check(near(added[0].model(3, 0), 0.0f) && near(added[1].model(3, 0), 20.0f), "A placement culled in is drawn as it is");
}

// A clock jump above the item (DrawItem::phase) is not blended: the ring
// walls turned back 11.25 degrees for a step at every wrap of their loop.
void test_phase_jumps() {
    engine::Node node;
    engine::GeometryData geometry;
    auto before = item_at(&node, &geometry, 0.0f);
    before.model = turn_y(0.1963f); // 11.25 degrees, the end of the pass
    auto after = item_at(&node, &geometry, 0.0f);
    after.model = turn_y(0.0009f); // the next pass's first step
    after.phase = 1;
    game::BlendReport report;
    auto blended = step({before}, {after}, 0.5f, &report);
    check(near(blended[0].model(0, 2), after.model(0, 2)), "A wrapped item is drawn as it is");
    check(report.wraps == 1 && report.blended == 0 && std::string(report.flagged[0].reason) == "wrap", "The wrap is reported");
    after.phase = 0;
    blended = step({before}, {after}, 0.5f);
    check(!near(blended[0].model(0, 2), after.model(0, 2)), "(without the phase it was blended backwards)");
    // A clock given an earlier origin takes the sequence time back.
    before.sequence_time = 5'000'000'000;
    after.sequence_time = 1'000'000'000;
    blended = step({before}, {after}, 0.5f);
    check(near(blended[0].model(0, 2), after.model(0, 2)), "A sequence whose clock went back is drawn as it is");
    after.sequence_time = 5'016'683'350;
    blended = step({before}, {after}, 0.5f);
    check(!near(blended[0].model(0, 2), after.model(0, 2)), "A clock running on blends");
}

// Texture matrices (UV scrolls) blend on their own clocks.
void test_texture_matrices() {
    engine::Node node;
    engine::GeometryData geometry;
    auto before = item_at(&node, &geometry, 0.0f);
    auto after = before;
    before.state.texture_matrix = engine::translation(0.10f, 0.0f, 0.0f);
    after.state.texture_matrix = engine::translation(0.12f, 0.0f, 0.0f);
    game::BlendReport report;
    auto blended = step({before}, {after}, 0.5f, &report);
    check(near((*blended[0].state.texture_matrix)(3, 0), 0.11f), "A UV scroll blends between the steps");
    check(report.textures == 1 && report.textures_blended == 1, "The texture blend is reported");
    after.texture_phase = 7; // the scroll's sequence wrapped
    blended = step({before}, {after}, 0.5f);
    check(near((*blended[0].state.texture_matrix)(3, 0), 0.12f), "A wrapped scroll is drawn as it is");
    after.texture_phase = 0;
    after.state.texture_matrix = engine::translation(0.75f, 0.0f, 0.0f); // an atlas cell, not a scroll
    blended = step({before}, {after}, 0.5f);
    check(near((*blended[0].state.texture_matrix)(3, 0), 0.75f), "A texture matrix jump is drawn as it is");
}

// A bone that jumps is drawn as it is; the others still blend.
void test_palette_bones() {
    engine::Node node;
    engine::GeometryData geometry;
    auto before = item_at(&node, &geometry, 0.0f);
    auto after = before;
    before.palette = std::make_shared<std::vector<engine::Mat4>>(
        std::vector<engine::Mat4>{engine::translation(0, 0, 0), engine::translation(0, 0, 0)});
    engine::Mat4 hidden;
    hidden(0, 0) = hidden(1, 1) = hidden(2, 2) = 0.0f; // a part scaled out
    after.palette = std::make_shared<std::vector<engine::Mat4>>(std::vector<engine::Mat4>{engine::translation(2, 0, 0), hidden});
    game::BlendReport report;
    auto blended = step({before}, {after}, 0.5f, &report);
    const auto& bones = *blended[0].palette;
    check(near(bones[0](3, 0), 1.0f), "The moving bone blends");
    check(near(bones[1](0, 0), 0.0f), "The bone that jumped is drawn as it is");
    check(report.palettes_blended == 1 && report.palettes_far == 1, "The partial palette is reported");
}

// A turn blended element by element shrinks the item by cos(angle / 2)
// halfway; the blend keeps each axis's length.
void test_turns_keep_their_size() {
    const engine::Mat4 a = turn_y(0.0f), b = turn_y(0.5f);
    const auto m = game::MotionInterpolator::blend_matrix(a, b, 0.5f);
    const float length = std::sqrt(m(0, 0) * m(0, 0) + m(0, 1) * m(0, 1) + m(0, 2) * m(0, 2));
    check(near(length, 1.0f), "A blended turn keeps the axis length");
    check(near(std::atan2(-m(0, 2), m(0, 0)), 0.25f), "A blended turn is halfway");
    engine::Mat4 scaled = turn_y(0.5f);
    for (int j = 0; j < 3; ++j) scaled(0, j) *= 3.0f;
    const auto s = game::MotionInterpolator::blend_matrix(a, scaled, 0.5f);
    check(near(std::sqrt(s(0, 0) * s(0, 0) + s(0, 1) * s(0, 1) + s(0, 2) * s(0, 2)), 2.0f), "A scale blends linearly");
    check(game::MotionInterpolator::blend_matrix(b, b, 0.3f).m == b.m, "A placement that did not change stays exact");
    // The camera: the eye on the line between its places, the view rigid.
    const engine::Mat4 v0 = engine::look_at({0, 0, 50}, {0, 0, 0}, {0, 1, 0});
    const engine::Mat4 v1 = engine::look_at({50 * std::sin(0.4f), 0, 50 * std::cos(0.4f)}, {0, 0, 0}, {0, 1, 0});
    const auto v = game::MotionInterpolator::blend_view(v0, v1, 0.5f);
    engine::Mat4 placed;
    check(engine::invert(v, placed), "The blended view is invertible");
    check(near(placed(3, 0), 25 * std::sin(0.4f)) && near(placed(3, 2), 25 * (1 + std::cos(0.4f))), "The eye moves on the line");
    check(near(std::sqrt(v(0, 0) * v(0, 0) + v(0, 1) * v(0, 1) + v(0, 2) * v(0, 2)), 1.0f), "The blended view does not shrink");
    check(game::MotionInterpolator::blend_view(v0, v0, 0.5f).m == v0.m, "A camera that did not move stays exact");
}

void test_previous_view() {
    engine::FrameCamera camera;
    engine::ShadowFrame shadow;
    game::MotionInterpolator interpolator;
    check(!interpolator.previous_view(), "No previous step: no previous view");
    camera.view = engine::translation(1, 2, 3);
    interpolator.capture({}, camera, shadow);
    check(!interpolator.previous_view(), "The current step is not the previous one");
    interpolator.advance();
    check(interpolator.previous_view() && (*interpolator.previous_view())(3, 1) == 2.0f, "After a step its view is the previous one");
    interpolator.reset();
    check(!interpolator.previous_view(), "A reset forgets it");
}

// The dev diagnostics (BlendReport, DW4_DEV_DUMP) name the two cases the
// key-and-distance blend cannot tell from motion.
void test_blend_report() {
    engine::Node node;
    engine::GeometryData geometry;
    engine::FrameCamera camera;
    engine::ShadowFrame shadow;
    {
        // A looping transform: two small steps, then back to the start.
        game::MotionInterpolator interpolator;
        for (const float x : {0.0f, 0.1f, 0.2f}) {
            std::vector<engine::DrawItem> items{item_at(&node, &geometry, x)};
            interpolator.capture(items, camera, shadow);
            interpolator.advance();
        }
        std::vector<engine::DrawItem> items{item_at(&node, &geometry, -5.0f)};
        interpolator.capture(items, camera, shadow);
        game::BlendReport report;
        auto blended = items;
        interpolator.blend(blended, camera, shadow, 0.5f, &report);
        check(report.items == 1 && report.jumps == 1 && report.flagged.size() == 1 &&
                  std::string(report.flagged[0].reason) == "jump",
              "A wrap far larger than the step before is reported as a jump");
    }
    {
        // Two instances; the first one is culled in the next step, so the
        // second becomes occurrence 0 and meets the first one's place.
        game::MotionInterpolator interpolator;
        std::vector<engine::DrawItem> items{item_at(&node, &geometry, 0.0f), item_at(&node, &geometry, 20.0f)};
        interpolator.capture(items, camera, shadow);
        interpolator.advance();
        items = {item_at(&node, &geometry, 20.0f)};
        interpolator.capture(items, camera, shadow);
        game::BlendReport report;
        auto blended = items;
        interpolator.blend(blended, camera, shadow, 0.5f, &report);
        check(report.swapped == 1 && report.blended == 1, "A culled instance shifting the occurrences is reported as a swap");
    }
    {
        // Plain motion reports nothing.
        game::MotionInterpolator interpolator;
        for (const float x : {0.0f, 1.0f, 2.0f}) {
            std::vector<engine::DrawItem> items{item_at(&node, &geometry, x)};
            interpolator.capture(items, camera, shadow);
            interpolator.advance();
        }
        std::vector<engine::DrawItem> items{item_at(&node, &geometry, 3.0f)};
        interpolator.capture(items, camera, shadow);
        game::BlendReport report;
        auto blended = items;
        interpolator.blend(blended, camera, shadow, 0.5f, &report);
        check(report.blended == 1 && report.flagged.empty() && report.camera, "Steady motion is blended without flags");
        check(near(blended[0].model(3, 0), 2.5f), "The report does not change the blend");
    }
}
} // namespace

int main() {
    try {
        test_blend_between_steps();
        test_jumps_and_new_items();
        test_repeated_instances();
        test_instances_keep_their_keys();
        test_phase_jumps();
        test_texture_matrices();
        test_palette_bones();
        test_turns_keep_their_size();
        test_previous_view();
        test_blend_report();
    } catch (const std::exception& error) {
        std::cerr << "interpolation tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "interpolation tests passed (" << checks << " checks)\n";
    return 0;
}
