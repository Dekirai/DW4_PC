#include "../src/game/interpolation.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 1e-4f; }

engine::DrawItem item_at(const engine::Node* node, const engine::GeometryData* geometry, float x) {
    engine::DrawItem item;
    item.node = node;
    item.geometry = geometry;
    item.model = engine::translation(x, 0, 0);
    return item;
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
} // namespace

int main() {
    try {
        test_blend_between_steps();
        test_jumps_and_new_items();
        test_repeated_instances();
    } catch (const std::exception& error) {
        std::cerr << "interpolation tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "interpolation tests passed (" << checks << " checks)\n";
    return 0;
}
