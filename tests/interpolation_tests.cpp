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
    // The texture matrix goes with its model: an item drawn as it is keeps
    // its step's texture matrix (the player's slash trail turns far in every
    // swing; a trailing UV on it showed a hard edge).
    after.state.texture_matrix = engine::translation(0.12f, 0.0f, 0.0f);
    after.model = turn_y(1.0f);
    game::BlendReport far_report;
    blended = step({before}, {after}, 0.5f, &far_report);
    check(near(blended[0].model(0, 0), after.model(0, 0)), "(the item turned far: drawn as it is)");
    check(near((*blended[0].state.texture_matrix)(3, 0), 0.12f), "A texture matrix stays with a model drawn as it is");
    check(far_report.far == 1 && far_report.textures == 1 && far_report.textures_blended == 0,
          "The texture matrix of an item drawn as it is is not blended");
    after.model = before.model;
    after.phase = 3; // a sequence above the item wrapped
    blended = step({before}, {after}, 0.5f);
    check(near((*blended[0].state.texture_matrix)(3, 0), 0.12f), "A wrapped item keeps its step's texture matrix");
    after.phase = 0;
    blended = step({before}, {after}, 0.5f);
    check(near((*blended[0].state.texture_matrix)(3, 0), 0.11f), "(with its model blended the scroll blends)");
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

// The step clock on a display with VSync (RefreshClock): passes measured
// with the jitter of when a pass starts after its vsync (0..3 ms, a fixed
// pseudo-random series) feed the host's accumulator (kFieldSeconds per
// step, at most 4 a pass); counted are the passes after 0 / 2+ steps and
// how often a 0 and a 2+ follow each other (judder).
struct PacingRun {
    int zeros = 0, doubles = 0, pairs = 0;
    double counted = 0, real = 0;
};
PacingRun pace(double display_hz, double reported_hz, int passes, bool snapped, double jitter = 0.003) {
    constexpr double kField = 1001.0 / 60000.0;
    dw4::game::RefreshClock clock;
    PacingRun run;
    std::uint32_t seed = 12345;
    const auto noise = [&] {
        seed = seed * 1664525u + 1013904223u;
        return double(seed >> 8) / double(1u << 24) * jitter;
    };
    double previous = noise(), accumulator = 0;
    int last_steps = 1;
    for (int k = 1; k <= passes; ++k) {
        const double stamp = double(k) / display_hz + noise();
        const double measured = stamp - previous;
        previous = stamp;
        const double dt = snapped ? clock.pass(measured, reported_hz) : measured;
        run.counted += dt;
        run.real += measured;
        accumulator += dt;
        int steps = 0;
        while (accumulator >= kField && steps < 4) {
            accumulator -= kField;
            ++steps;
        }
        if (steps == 4) accumulator = 0;
        if (k > 240) { // (after the clock settled)
            if (steps == 0) ++run.zeros;
            if (steps >= 2) ++run.doubles;
            if ((steps == 0 && last_steps >= 2) || (steps >= 2 && last_steps == 0)) ++run.pairs;
        }
        last_steps = steps;
    }
    return run;
}

void test_refresh_clock() {
    // 60 Hz with the jitter: measured, 0 / 2 pairs near the boundary; counted
    // in refreshes, one lone repeated picture every ~1000 passes.
    const auto raw = pace(60.0, 60.0, 6000, false);
    const auto clocked = pace(60.0, 60.0, 6000, true);
    check(raw.pairs > 20, "the measured clock judders on a 60 Hz display (the test sees it)");
    check(clocked.pairs == 0 && clocked.doubles == 0 && clocked.zeros >= 4 && clocked.zeros <= 6,
          "counted in refreshes: only the lone repeats of 60 against 59.94 Hz");
    check(std::abs(clocked.counted - clocked.real) < 1001.0 / 60000.0, "the counted clock keeps real time");
    // A 59.94 Hz display that reports 60: the measured period takes over, no judder at all.
    const auto ntsc = pace(60000.0 / 1001.0, 60.0, 6000, true);
    check(ntsc.pairs == 0 && ntsc.zeros <= 1 && ntsc.doubles <= 1, "a 59.94 Hz display: steady");
    check(std::abs(ntsc.counted - ntsc.real) < 1001.0 / 60000.0, "a 59.94 Hz display that says 60: real time kept");
    // 120 Hz: a step every other pass, never two.
    const auto fast = pace(120.0, 120.0, 6000, true, 0.0015);
    check(fast.doubles == 0, "120 Hz: never two steps in a pass");
    // A display that is not what it reports (a variable refresh at the game's
    // own pace under a 144 Hz mode): every pass as measured.
    dw4::game::RefreshClock clock;
    check(near(float(clock.pass(1001.0 / 60000.0, 144.0)), float(1001.0 / 60000.0)), "not a whole number of refreshes: as measured");
    check(clock.pass(0.2, 60.0) == 0.2, "a hitch of more than four refreshes: as measured");
    check(clock.pass(0.016, 0.0) == 0.016 && clock.period() == 0.0, "no known rate: as measured");
}

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
        test_refresh_clock();
    } catch (const std::exception& error) {
        std::cerr << "interpolation tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "interpolation tests passed (" << checks << " checks)\n";
    return 0;
}
