#include "dw4/walking.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}
void near(double value, double expected, const char* description, double tolerance = 0.0001) {
    check(std::abs(value - expected) <= tolerance, description);
}
void rejects(const std::function<void()>& operation) {
    ++checks;
    try { operation(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid walking operation was accepted");
}
MeshData rectangle(float x0, float x1, float z0, float z1, float y0 = 0, float y1 = 0) {
    MeshData mesh;
    mesh.positions = {{x0, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y0, z1}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}
MeshData wall(float x0, float z0, float x1, float z1, float bottom = 0, float top = 10) {
    MeshData mesh;
    mesh.positions = {{x0, bottom, z0}, {x1, bottom, z1}, {x1, top, z1}, {x0, top, z0}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}
WalkingWorld world(std::initializer_list<MeshData> meshes) {
    return WalkingWorld(std::span<const MeshData>(meshes.begin(), meshes.size()));
}
void geometry() {
    auto flat = rectangle(-100, 100, -100, 100, 7, 7);
    auto reversed = flat;
    std::reverse(reversed.indices.begin(), reversed.indices.end());
    for (const auto& mesh : {flat, reversed}) {
        const auto floor = world({mesh});
        near(*floor.find_floor(0, 0, 10, 0, 5), 7, "Triangle winding changed floor height");
        check(!floor.find_floor(0, 0, 6, 0, 50), "Floor above query was accepted with zero step up");
        check(!floor.find_floor(0, 0, 15, 0, 5), "Floor beneath allowed drop was accepted");
        check(!floor.find_floor(101, 0, 10, 10, 10), "Outside triangle was accepted");
        near(*floor.find_floor(100, 100, 10, 0, 5), 7, "Shared and outer triangle edges lost support");
    }
    const auto slope = world({rectangle(-100, 100, -100, 100, -50, 50)});
    near(*slope.find_floor(20, 13, 10, 1, 1), 10, "Slope barycentric interpolation is wrong");
    auto steep = rectangle(-1, 1, -1, 1, -10, 10);
    auto vertical = steep;
    for (auto& p : vertical.positions) p.x = 0;
    auto degenerate = flat;
    degenerate.indices = {0, 0, 0};
    const auto filtered = world({flat, steep, vertical, degenerate});
    check(filtered.triangle_count() == 2, "Steep, vertical or degenerate surface was retained");
    rejects([&] { world({steep}); });
    const auto levels = world({rectangle(-10, 10, -10, 10, -20, -20),
        rectangle(-10, 10, -10, 10, 0, 0), rectangle(-10, 10, -10, 10, 20, 20)});
    near(*levels.find_floor(0, 0, 1, 30, 30), 0, "Nearest level selection is wrong");
    near(*levels.find_floor(0, 0, 10, 30, 30), 0, "Equidistant level selection must prefer below");
    near(*levels.find_floor(0, 0, 19, 30, 30), 20, "Nearest upper level was ignored");
    near(*levels.find_floor(0, 0, 19, 0, 30), 0, "Downward query snapped onto the ceiling");
}
void movement() {
    const auto flat = world({rectangle(-1000, 1000, -1000, 1000)});
    for (int fps : {30, 60, 144}) {
        Walker walker;
        walker.reset({0, 40, 0}, flat);
        for (int frame = 0; frame < fps; ++frame) walker.advance(1, 0, 1.0 / fps, flat);
        near(walker.position().x, 80, "Frame rate changed one-second travel");
        near(walker.position().y, 0, "Moving player lost the floor");
        near(walker.facing(), std::numbers::pi / 2, "Positive X heading is wrong");
        check(walker.moving() && walker.grounded(), "Successful movement did not report grounded motion");
        walker.advance(0, 0, 1.0 / fps, flat);
        check(!walker.moving(), "Idle input retained the moving flag");
    }
    Walker diagonal;
    diagonal.reset({0, 0, 0}, flat);
    for (int i = 0; i < 60; ++i) diagonal.advance(1, 1, 1.0 / 60, flat);
    near(std::hypot(diagonal.position().x, diagonal.position().z), 80, "Diagonal input runs faster");
    Walker analog;
    analog.reset({0, 0, 0}, flat);
    for (int i = 0; i < 60; ++i) analog.advance(0, .25f, 1.0 / 60, flat);
    near(analog.position().z, 20, "Analog magnitude was not retained");
    analog.advance(0, 1, 60, flat);
    near(analog.position().z, 28, "Long frame teleported instead of clamping travel");
    analog.advance(0, 1, 0, flat);
    near(analog.position().z, 28, "Zero time changed player position");
    const auto slope = world({rectangle(-100, 100, -100, 100, -50, 50)});
    Walker uphill;
    uphill.reset({0, 30, 0}, slope);
    for (int i = 0; i < 30; ++i) uphill.advance(1, 0, 1.0 / 60, slope);
    near(uphill.position().x, 40, "Slope altered horizontal travel speed");
    near(uphill.position().y, 20, "Player did not follow the slope");
    const auto bridge = world({rectangle(-100, 100, -100, 100, -20, -20),
        rectangle(-100, 100, -100, 100, 0, 0), rectangle(-100, 100, -100, 100, 20, 20)});
    Walker lower;
    lower.reset({0, 1, 0}, bridge);
    for (int i = 0; i < 30; ++i) lower.advance(1, 0, 1.0 / 60, bridge);
    near(lower.position().y, 0, "Movement jumped between overlapping floors");
}
void edges_and_steps() {
    for (float gap : {.0001f, .01f, 2.0f}) {
        const auto split = world({rectangle(-100, 0, -100, 100), rectangle(gap, 100, -100, 100)});
        Walker walker;
        walker.reset({-.5f, 0, 0}, split);
        walker.advance(1, 0, 1.0 / 80, split);
        near(walker.position().x, -.5, "Swept path hopped a gap between endpoint samples");
        check(!walker.moving() && walker.grounded(), "Rejected path changed grounded motion state");
    }
    for (float destination_height : {14.0f, 16.0f, -49.0f, -51.0f}) {
        const auto stairs = world({rectangle(-100, 0, -100, 100),
            rectangle(0, 100, -100, 100, destination_height, destination_height)});
        Walker walker;
        walker.reset({-.5f, 0, 0}, stairs);
        walker.advance(1, 0, 1.0 / 80, stairs);
        const bool accepted = destination_height <= 15 && destination_height >= -50;
        near(walker.position().x, accepted ? .5 : -.5, "Step height limit was not enforced");
        near(walker.position().y, accepted ? destination_height : 0, "Step did not produce its floor height");
    }
    const auto finite_floor = world({rectangle(-2, 2, -2, 2)});
    Walker edge;
    edge.reset({0, 0, 0}, finite_floor);
    edge.advance(1, 0, .1, finite_floor);
    near(edge.position().x, 2, "Walker escaped the floor boundary or stopped before its edge");
    edge.advance(1, 0, .1, finite_floor);
    check(!edge.moving(), "Blocked edge motion played the moving state");
    edge.advance(-1, 0, 1.0 / 80, finite_floor);
    near(edge.position().x, 1, "Player could not walk away from a blocked boundary");
}
void hierarchy() {
    std::vector<MeshData> tiles;
    for (int x = -10; x < 10; ++x)
        for (int z = -10; z < 10; ++z)
            tiles.push_back(rectangle(float(x * 10), float((x + 1) * 10),
                float(z * 10), float((z + 1) * 10), float(x), float(x + 1)));
    const WalkingWorld tiled(tiles);
    check(tiled.triangle_count() == 800, "Floor hierarchy omitted tiles");
    for (int x = -99; x <= 99; x += 9)
        for (int z = -99; z <= 99; z += 9)
            near(*tiled.find_floor(float(x), float(z), 0, 10, 10), double(x) / 10,
                "Floor hierarchy returned an incorrect tile or height");
    Walker crossing;
    crossing.reset({-60, 0, -60}, tiled);
    for (int i = 0; i < 120; ++i) crossing.advance(1, 1, 1.0 / 60, tiled);
    const double expected = -60 + 160 / std::sqrt(2.0);
    near(crossing.position().x, expected, "Diagonal movement stopped at a shared tile corner");
    near(crossing.position().z, expected, "Diagonal tile crossing changed direction");
    near(crossing.position().y, expected / 10, "Tile crossing lost interpolated floor height");
}
void walls() {
    const std::vector<MeshData> floors{rectangle(-100, 100, -100, 100)};
    auto barrier = wall(0, -100, 0, 100);
    for (bool reversed : {false, true}) {
        if (reversed) std::reverse(barrier.indices.begin(), barrier.indices.end());
        const std::vector<MeshData> walls{barrier};
        const WalkingWorld blocked(floors, walls);
        check(blocked.wall_triangle_count() == 2, "Vertical wall triangles were filtered out");
        Walker walker;
        walker.reset({-2.5f, 0, 0}, blocked);
        walker.advance(1, 0, 1.0 / 80, blocked);
        near(walker.position().x, -1.5, "Body radius boundary stopped too early");
        walker.advance(1, 0, 1.0 / 80, blocked);
        near(walker.position().x, -1.5, "Player radius entered the wall");
        check(!walker.moving(), "Wall-blocked movement retained the running state");
        walker.advance(1, 1, 1.0 / 80, blocked);
        near(walker.position().x, -1.5, "Glancing movement entered the wall");
        near(walker.position().z, 1 / std::sqrt(2.0), "Glancing movement did not slide without acceleration");
        walker.advance(-1, 0, 1.0 / 80, blocked);
        near(walker.position().x, -2.5, "Player could not retreat from wall contact");
        walker.reset({-.5f, 0, 0}, blocked);
        walker.advance(-1, 0, 1.0 / 80, blocked);
        near(walker.position().x, -1.5, "An overlapping entry point could not escape outward");
        walker.reset({-.5f, 0, 0}, blocked);
        walker.advance(1, 0, 1.0 / 80, blocked);
        near(walker.position().x, -.5, "An overlapping entry point crossed through its wall");
        WalkingSettings settings; settings.max_substep_distance = 10;
        Walker fast(settings);
        fast.reset({-5, 0, 0}, blocked);
        fast.advance(1, 0, .1, blocked);
        near(fast.position().x, -5, "Continuous wall sweep tunneled between distant endpoints");
    }
    for (const auto height_range : {std::array<float, 2>{7, 20}, {-20, .5f}, {20, 30}}) {
        const std::vector<MeshData> walls{wall(0, -100, 0, 100, height_range[0], height_range[1])};
        const WalkingWorld clear(floors, walls);
        Walker walker;
        walker.reset({-5, 0, 0}, clear);
        walker.advance(1, 0, .1, clear);
        near(walker.position().x, 3, "An overhead or different-floor wall blocked the player's body");
    }
    const std::vector<MeshData> short_wall{wall(0, -1, 0, 1)};
    const WalkingWorld endpoint(floors, short_wall);
    WalkingSettings settings; settings.max_substep_distance = 10;
    for (float z : {2.4f, 2.6f}) {
        Walker walker(settings);
        walker.reset({-5, 0, z}, endpoint);
        walker.advance(1, 0, .1, endpoint);
        near(walker.position().x, z < 2.5f ? -5 : 3, "Wall endpoint did not preserve the body radius");
    }
    const std::vector<MeshData> angled_wall{wall(-20, -20, 20, 20)};
    const WalkingWorld angled(floors, angled_wall);
    Walker crossing(settings);
    crossing.reset({-5, 0, 0}, angled);
    crossing.advance(1, 0, .1, angled);
    near(crossing.position().x, -5, "A diagonal wall was crossed by a continuous sweep");
    const std::vector<MeshData> ceiling{rectangle(-100, 100, -100, 100, 7, 7)};
    const WalkingWorld under_ceiling(floors, ceiling);
    Walker beneath;
    beneath.reset({0, 0, 0}, under_ceiling);
    beneath.advance(1, 0, 1.0 / 80, under_ceiling);
    near(beneath.position().x, 1, "A ceiling above the body obstructed movement");
    auto invalid_wall = barrier;
    invalid_wall.indices[0] = 999;
    const std::vector<MeshData> malformed_walls{invalid_wall};
    rejects([&] { WalkingWorld invalid(floors, malformed_walls); });
    for (const auto radius : {0.0f, -1.0f, 101.0f, std::numeric_limits<float>::quiet_NaN()}) {
        WalkingSettings bad; bad.body_radius = radius;
        rejects([&] { Walker invalid(bad); });
    }
    WalkingSettings bad; bad.body_top = bad.body_bottom;
    rejects([&] { Walker invalid(bad); });
}
void validation() {
    const auto flat = rectangle(-100, 100, -100, 100);
    const auto floor = world({flat});
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    auto malformed = flat;
    malformed.indices.push_back(0);
    rejects([&] { world({malformed}); });
    malformed = flat; malformed.indices[0] = 999;
    rejects([&] { world({malformed}); });
    malformed = flat; malformed.positions[0].x = nan;
    rejects([&] { world({malformed}); });
    malformed = flat; malformed.positions[0].y = infinity;
    rejects([&] { world({malformed}); });
    malformed = flat; malformed.positions[0].z = 1000001;
    rejects([&] { world({malformed}); });
    rejects([&] { world({}); });
    for (const auto value : {nan, infinity}) {
        rejects([&] { floor.find_floor(value, 0, 0, 1, 1); });
        rejects([&] { floor.find_floor(0, value, 0, 1, 1); });
        rejects([&] { floor.find_floor(0, 0, value, 1, 1); });
        rejects([&] { floor.find_floor(0, 0, 0, value, 1); });
        rejects([&] { floor.find_floor(0, 0, 0, 1, value); });
    }
    rejects([&] { floor.find_floor(0, 0, 0, -1, 1); });
    rejects([&] { floor.find_floor(0, 0, 0, 1, -1); });
    Walker uninitialized;
    rejects([&] { uninitialized.advance(0, 0, 0, floor); });
    Walker walker;
    walker.reset({1, 2, 3}, floor, .5f);
    for (auto point : {Vec3{nan, 0, 0}, Vec3{0, infinity, 0}, Vec3{0, 0, nan},
        Vec3{1000, 0, 0}, Vec3{0, -10, 0}, Vec3{0, 201, 0}}) {
        rejects([&] { walker.reset(point, floor); });
        near(walker.position().x, 1, "Failed reset changed player X");
        near(walker.position().z, 3, "Failed reset changed player Z");
        near(walker.facing(), .5, "Failed reset changed facing");
    }
    rejects([&] { walker.reset({0, 0, 0}, floor, nan); });
    rejects([&] { walker.advance(nan, 0, .1, floor); });
    rejects([&] { walker.advance(0, infinity, .1, floor); });
    rejects([&] { walker.advance(0, 0, nan, floor); });
    rejects([&] { walker.advance(0, 0, infinity, floor); });
    rejects([&] { walker.advance(0, 0, -1, floor); });
    near(walker.position().x, 1, "Invalid advance changed position");
    for (const auto value : {0.0f, -1.0f, nan, infinity, 1001.0f}) {
        WalkingSettings settings; settings.speed = value;
        rejects([&] { Walker invalid(settings); });
    }
    WalkingSettings settings; settings.max_substep_distance = .001f;
    rejects([&] { Walker invalid(settings); });
    settings = {}; settings.max_frame_seconds = 1;
    rejects([&] { Walker invalid(settings); });
    std::vector<MeshData> too_many_meshes(4097);
    rejects([&] { WalkingWorld invalid(too_many_meshes); });
    malformed = flat; malformed.indices.resize(262145 * 3, 0);
    rejects([&] { world({malformed}); });
    malformed = flat; malformed.positions.resize(1048577);
    rejects([&] { world({malformed}); });
    malformed = flat; malformed.indices.clear();
    for (int i = 0; i < 8193; ++i) malformed.indices.insert(malformed.indices.end(), {0, 1, 2});
    const auto overlap = world({malformed});
    rejects([&] { overlap.find_floor(0, 0, 0, 1, 1); });
}
}

int main() {
    try {
        geometry(); movement(); edges_and_steps(); hierarchy(); walls(); validation();
        std::cout << "Walking floor, continuous path, locomotion and validation tests passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Walking test failure: " << error.what() << '\n';
        return 1;
    }
}
