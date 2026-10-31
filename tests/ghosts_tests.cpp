// Ghost figures of the hub stages (docs/SEAMLESS.md 12, round 60): the
// figure's placement and facing, where its items go in a frame's draw list,
// the camera fade, and with the game data (argument 1) a form's figure end to
// end: loaded in the background, faded in, posed (idle, then running),
// drawn with its shadow proxy, faded near the camera without showing its
// inside, a form change and a jump faded over, the cap of nine.
#include "ghost_figures.hpp"

#include "dw4/engine/scene_graph.hpp"
#include "dw4/resources.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace dw4::game;
using dw4::engine::DrawItem;
using dw4::engine::Mat4;
using dw4::engine::Vec3f;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }

void placement() {
    for (const float yaw : {0.0f, 0.5f, 1.5707963f, -2.0f, 3.0f}) {
        const Mat4 m = ghost_matrix({10, 20, 30}, yaw);
        // The sender takes its yaw as atan2 of the node's Z row (engine_adapter.cpp).
        check(near(std::atan2(m(2, 0), m(2, 2)), yaw), "the Z row gives back the yaw " + std::to_string(yaw));
        check(near(m(1, 1), 1) && near(m(0, 1), 0) && near(m(2, 1), 0), "upright");
        const float det = m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
                          m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
        check(near(det, 1), "a rotation (no mirror, no scale)");
        check(m(3, 0) == 10 && m(3, 1) == 20 && m(3, 2) == 30, "at the position");
    }
    // Facing +X at yaw pi/2: the model's forward (+Z) goes to world +X.
    const auto forward = dw4::engine::transform_point({0, 0, 1}, ghost_matrix({0, 0, 0}, std::numbers::pi_v<float> / 2));
    check(near(forward.x, 1) && near(forward.z, 0, 1e-4f), "yaw pi/2 faces +X");
    check(same_stage("stagML11", "stagml11") && !same_stage("stagml11", "stagml12") && !same_stage("stagml1", "stagml11"),
          "stage names compare without letter case");
}

DrawItem item(bool blended, bool layer_2d) {
    DrawItem i;
    i.state.blend_enabled = blended;
    if (layer_2d) i.state.projection = Mat4{};
    return i;
}

void insert_points() {
    // Opaque: 3D, 3D, 2D; blended: 3D, 2D (Engine::render's order).
    std::vector<DrawItem> items{item(false, false), item(false, false), item(false, true), item(true, false), item(true, true)};
    auto at = ghost_insert_points(items);
    check(at.opaque == 2 && at.blended == 4, "after the last opaque and the last blended 3D item");
    items = {item(false, false), item(false, true)};
    at = ghost_insert_points(items);
    check(at.opaque == 1 && at.blended == 2, "no blended item: the blended figures go last");
    items = {item(false, false), item(false, true), item(true, true)};
    at = ghost_insert_points(items);
    check(at.opaque == 1 && at.blended == 2, "only 2D blended items: before them");
    check(ghost_insert_points({}).opaque == 0 && ghost_insert_points({}).blended == 0, "an empty list");
}

void camera_fade() {
    const Vec3f eye{0, 50, 0};
    const Vec3f own{0, 10, 150};
    check(ghost_camera_alpha(eye, {80, 10, 60}, own, 10) == 1.0f, "away from the eye and the line of sight: no fade");
    check(near(ghost_camera_alpha(eye, {0, 45, 10}, own, 10), kCameraAlpha), "at the eye: faint");
    check(near(ghost_camera_alpha(eye, {0, 30, 75}, own, 10), kCameraAlpha), "between the eye and the own player: faint");
    check(ghost_camera_alpha(eye, {0, 5, 200}, own, 10) == 1.0f, "behind the own player: no fade");
    check(ghost_camera_alpha(eye, {30, 30, 75}, own, 10) == 1.0f, "beside the line of sight: no fade");
    const float edge = ghost_camera_alpha(eye, {0, 30 + 8.5f, 75}, own, 10);
    check(edge > kCameraAlpha && edge < 1.0f, "the line's edge fades part of the way");
    check(ghost_camera_alpha(eye, {0, 30, 75}, std::nullopt, 10) == 1.0f, "no own player: only the eye distance");
    check(near(ghost_camera_alpha(eye, {3, 12, 152}, own, 10), kCameraAlpha), "standing in the own player: faint");
    const float beside = ghost_camera_alpha(eye, {11, 12, 150}, own, 10);
    check(beside > kCameraAlpha && beside < 1.0f, "close beside it: part of the way");
    check(ghost_camera_alpha(eye, {2, 80, 150}, own, 10) == 1.0f, "another floor above: no fade");
}

// ---- with the game data
struct Clock {
    double now = 100.0;
};

std::vector<DrawItem> draw(GhostFigures& ghosts, const dw4::engine::FrameCamera& camera, double now, dw4::engine::ShadowFrame* shadow = nullptr,
                           std::size_t scene = 0) {
    std::vector<DrawItem> items(scene, item(false, false));
    ghosts.draw(items, shadow, camera, std::nullopt, now);
    return items;
}

void figures(const std::string& data) {
    const auto resources = dw4::MountedResources::open(data);
    GhostFigures ghosts;
    ghosts.set_resources(resources);
    Clock clock;
    GhostTarget agumon;
    agumon.key = 3;
    agumon.position = {100, 0, 50};
    agumon.yaw = 0.7f;
    agumon.species = 0;
    dw4::engine::FrameCamera camera;
    camera.view = dw4::engine::look_at({100, 80, 250}, {100, 10, 50}, {0, 1, 0});
    camera.projection = dw4::engine::perspective(0.8f, 4.0f / 3.0f, 1.0f, 4000.0f);
    // Loading runs in the background; until it is done the figure waits unseen.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (ghosts.loaded_forms() == 0 && std::chrono::steady_clock::now() < deadline) {
        ghosts.update({agumon}, std::nullopt, clock.now);
        check(draw(ghosts, camera, clock.now).empty() || ghosts.loaded_forms() == 1, "nothing drawn before the form is loaded");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(ghosts.loaded_forms() == 1, "Agumon's package loaded");
    // Fade in over kFadeSeconds.
    ghosts.update({agumon}, std::nullopt, clock.now += 0.1);
    auto items = draw(ghosts, camera, clock.now);
    check(!items.empty() && ghosts.drawn() == 1, "the figure is drawn while it fades in");
    bool faded = false, depth_first = false;
    for (const auto& i : items) {
        faded |= i.state.blend_enabled && i.state.modulate && i.state.modulate->w < 0.5f;
        depth_first |= !i.state.color_write;
    }
    check(faded && depth_first, "fading in: the depth first, then the colour at a fraction");
    for (int k = 0; k < 6; ++k) ghosts.update({agumon}, std::nullopt, clock.now += 0.1);
    dw4::engine::ShadowFrame shadow;
    shadow.active = true;
    items = draw(ghosts, camera, clock.now, &shadow, 2);
    std::size_t skinned = 0, blended = 0;
    for (std::size_t k = 2; k < items.size(); ++k) {
        skinned += items[k].palette && items[k].palette->size() > 1 ? 1 : 0;
        blended += items[k].state.blend_enabled ? 1 : 0;
    }
    check(items.size() > 2 && skinned > 0 && blended == 0, "faded in: opaque skinned items after the scene's opaque ones");
    check(!shadow.casters.empty(), "its shadow proxy joins the shadow casters");
    check(ghosts.describe().size() == 1 && ghosts.describe()[0].find("clip=waitD") != std::string::npos, "standing: the wait clip (" +
                                                                                                         ghosts.describe()[0] + ")");
    // Running: from its speed (no clip sent), then the clip the member plays.
    agumon.speed = 120;
    ghosts.update({agumon}, std::nullopt, clock.now += 0.05);
    draw(ghosts, camera, clock.now);
    check(ghosts.describe()[0].find("clip=runD") != std::string::npos, "moving without a clip: the run clip (" + ghosts.describe()[0] + ")");
    agumon.clip = "waitS";
    ghosts.update({agumon}, std::nullopt, clock.now += 0.05);
    draw(ghosts, camera, clock.now);
    check(ghosts.describe()[0].find("clip=waitS") != std::string::npos, "the member's clip wins");
    agumon.clip = "noSuchClip";
    agumon.speed = 0;
    ghosts.update({agumon}, std::nullopt, clock.now += 0.05);
    draw(ghosts, camera, clock.now);
    check(ghosts.describe()[0].find("clip=waitD") != std::string::npos, "an unknown clip: the wait clip");
    // The camera right at the figure: it turns faint (eased), depth first.
    dw4::engine::FrameCamera close = camera;
    close.view = dw4::engine::look_at({100, 15, 62}, {100, 10, 0}, {0, 1, 0});
    for (int k = 0; k < 10; ++k) {
        ghosts.update({agumon}, std::nullopt, clock.now += 0.05);
        items = draw(ghosts, close, clock.now);
    }
    float least = 1;
    for (const auto& i : items)
        if (i.state.modulate) least = std::min(least, i.state.modulate->w);
    check(near(least, kCameraAlpha, 0.02f), "at the camera: faint (" + std::to_string(least) + ")");
    // A jump: the old figure fades out where it was, a new one in.
    agumon.segment = 1;
    agumon.position = {400, 0, 50};
    ghosts.update({agumon}, std::nullopt, clock.now += 0.1);
    auto lines = ghosts.describe();
    check(lines.size() == 2 && lines[0].find("leaving") != std::string::npos && lines[1].find("segment=1") != std::string::npos,
          "a jump fades over instead of gliding");
    for (int k = 0; k < 6; ++k) ghosts.update({agumon}, std::nullopt, clock.now += 0.1);
    check(ghosts.describe().size() == 1, "the old figure is gone after its fade");
    // Another form: a crossfade once Veemon's package is in.
    agumon.species = 1;
    for (int k = 0; k < 400 && ghosts.loaded_forms() < 2; ++k) {
        ghosts.update({agumon}, std::nullopt, clock.now += 0.01);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(ghosts.loaded_forms() == 2, "Veemon's package loaded");
    for (int k = 0; k < 6; ++k) ghosts.update({agumon}, std::nullopt, clock.now += 0.1);
    lines = ghosts.describe();
    check(lines.size() == 1 && lines[0].find("form=1") != std::string::npos, "the new form stands alone after the crossfade");
    // Leaving: fades out, then gone.
    ghosts.update({}, std::nullopt, clock.now += 0.1);
    check(ghosts.active() && ghosts.describe()[0].find("leaving") != std::string::npos, "a member that left fades out");
    for (int k = 0; k < 5; ++k) ghosts.update({}, std::nullopt, clock.now += 0.1);
    check(!ghosts.active(), "and is gone");
    // The cap: the nearest nine of twelve.
    std::vector<GhostTarget> crowd;
    for (int k = 0; k < 12; ++k) {
        GhostTarget t;
        t.key = std::uint64_t(10 + k);
        t.species = 0;
        t.position = {float(k * 30), 0, 0};
        crowd.push_back(t);
    }
    ghosts.update(crowd, Vec3f{0, 0, 0}, clock.now += 0.1);
    lines = ghosts.describe();
    check(lines.size() == GhostFigures::kMaxFigures, "at most nine figures");
    for (const auto& line : lines) check(line.find("member=19") == std::string::npos && line.find("member=21") == std::string::npos, "the nearest ones");
    ghosts.clear();
    check(!ghosts.active(), "clear drops every figure at once");
}
} // namespace

int main(int argc, char** argv) {
    try {
        placement();
        insert_points();
        camera_fade();
        if (argc > 1 && argv[1] && *argv[1]) figures(argv[1]);
        else std::cout << "(no game data: the figure checks are skipped)\n";
        std::cout << checks << " ghost figure checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ghost figure test failure: " << error.what() << '\n';
        return 1;
    }
}
