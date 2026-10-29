// Seamless online co-op, the cameras-exits side (suite online_cameras) (docs/SEAMLESS.md 4, 5.2,
// 6.7): the union cull and the picture-only pass of per-slot cameras, the
// exit port's prompt owner and side rule, the rules, forks and intents of an
// engine without a stage.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/math.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4::engine;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

constexpr std::int64_t frame = 16666666;

// root -> seen (in the first camera's view) -> recorder, geometry;
//         aside (right of it) -> recorder, ParticleTimer, geometry.
struct Scene {
    Node root, seen, seen_recorder, seen_geometry, aside, aside_recorder, aside_timer, aside_geometry;
    std::shared_ptr<GeometryData> quad = std::make_shared<GeometryData>();
    Scene() {
        root.kind = NodeKind::group;
        seen.kind = aside.kind = NodeKind::group;
        seen_recorder.kind = aside_recorder.kind = NodeKind::transform_recorder;
        aside_timer.kind = NodeKind::particle_timer;
        aside_geometry.kind = seen_geometry.kind = NodeKind::geometry;
        quad->valid = true;
        aside_geometry.runtime_geometry = seen_geometry.runtime_geometry = quad;
        seen.has_bound = aside.has_bound = true;
        seen.bound_min = {-10, -10, -110};
        seen.bound_max = {10, 10, -90};
        aside.bound_min = {310, -10, -110};
        aside.bound_max = {320, 10, -90};
        root.children = {&seen, &aside};
        seen.children = {&seen_recorder, &seen_geometry};
        aside.children = {&aside_recorder, &aside_timer, &aside_geometry};
        aside_timer.timer.running = true;
        aside_timer.timer.time = -1;
    }
};

TraversalContext base_context() {
    TraversalContext context;
    context.cull = true;
    context.time = 5000 * frame;
    context.cull_camera.projection = perspective_horizontal(3.14159265f / 2.0f, 640.0f / 448.0f, 10.0f, 2000.0f);
    context.camera = context.cull_camera;
    return context;
}

// A camera that looks along -z from x = 315: it has `aside` in view.
FrameCamera camera_at_aside(const FrameCamera& first) {
    FrameCamera camera = first;
    camera.view = look_at({315, 0, 0}, {315, 0, -1}, {0, 1, 0});
    return camera;
}

void test_union_one_camera() {
    // The union code with one camera decides exactly as the original test.
    Scene original, unioned;
    auto context = base_context();
    traverse(&original.root, context);
    auto union_context = base_context();
    union_context.union_cull = true;
    traverse(&unioned.root, union_context);
    check(original.seen_recorder.recorded.has_value() && unioned.seen_recorder.recorded.has_value(),
          "One camera: the subtree in view records its matrices in both");
    check(!original.aside_recorder.recorded && !unioned.aside_recorder.recorded, "One camera: the culled subtree stays in both");
    check(original.aside_timer.timer.time == -1 && unioned.aside_timer.timer.time == -1, "One camera: the culled clock stays in both");
    check(original.seen_recorder.recorded->m == unioned.seen_recorder.recorded->m, "One camera: the same recorded matrix");
}

void test_union_two_cameras() {
    // A second member's camera keeps its surroundings simulated; the
    // recorders keep the first member's view.
    Scene scene;
    auto context = base_context();
    context.union_cull = true;
    context.extra_cull.push_back(camera_at_aside(context.cull_camera));
    traverse(&scene.root, context);
    check(scene.seen_recorder.recorded.has_value(), "The first camera's subtree is simulated");
    check(scene.aside_recorder.recorded.has_value(), "The second camera's subtree is simulated too");
    check(scene.aside_timer.timer.time != -1, "Its ParticleTimer advances");
    check(context.traversal_view.m == context.cull_camera.view.m, "The traversal's view (recorders, billboards) is the first camera's");
}

void test_picture_only_pass() {
    // The picture of one member's camera draws what that camera sees and
    // changes nothing; the hidden subtrees (other members' prompts) are left out.
    Scene scene;
    auto context = base_context();
    context.union_cull = true;
    context.extra_cull.push_back(camera_at_aside(context.cull_camera));
    traverse(&scene.root, context);
    const auto recorded = *scene.aside_recorder.recorded;
    const auto timer = scene.aside_timer.timer;

    auto picture = base_context();
    picture.cull_camera = picture.camera = camera_at_aside(picture.cull_camera);
    picture.simulate = false;
    std::vector<DrawItem> items;
    picture.output = &items;
    scene.aside_recorder.recorded.reset();
    traverse(&scene.root, picture);
    check(items.size() == 1 && items[0].node == &scene.aside_geometry, "The picture draws the second member's view");
    check(!scene.aside_recorder.recorded.has_value(), "The picture records nothing");
    check(scene.aside_timer.timer.time == timer.time, "The picture advances no clock");
    scene.aside_recorder.recorded = recorded;

    const std::vector<const Node*> hidden{&scene.aside};
    picture.picture_hidden = &hidden;
    items.clear();
    traverse(&scene.root, picture);
    check(items.empty(), "A hidden subtree is not drawn");
    // A simulation pass ignores the list (it is presentation only).
    auto simulation = base_context();
    simulation.picture_hidden = &hidden;
    scene.seen_recorder.recorded.reset();
    traverse(&scene.root, simulation);
    check(scene.seen_recorder.recorded.has_value(), "A simulation pass never skips a subtree for the picture");
}

void test_union_lod_nearest() {
    // igLod in the union pass takes the nearest member camera.
    Node root, lod, near_child, far_child;
    root.kind = NodeKind::group;
    lod.kind = NodeKind::lod;
    near_child.kind = far_child.kind = NodeKind::group;
    lod.children = {&near_child, &far_child};
    lod.lod_ranges = {0.0f, 100.0f * 100.0f}; // child 0 within 100 units, child 1 beyond
    lod.lod_center = {0, 0, -500};
    root.children = {&lod};
    auto context = base_context();
    context.cull = false;
    context.union_cull = true;
    traverse(&root, context);
    check(lod.lod_child == 1, "Seen from the first camera alone the lod is far");
    FrameCamera close = context.cull_camera;
    close.view = look_at({0, 0, -450}, {0, 0, -451}, {0, 1, 0});
    context.extra_cull.push_back(close);
    traverse(&root, context);
    check(lod.lod_child == 0, "A second camera near it selects the near child");
}

void test_exit_owner_and_sides() {
    // The prompt owner: the lowest living present member that stepped on.
    check(exit_prompt_owner(0b11, 0b01, 0b11, 0b11) == 1, "The member who stepped on owns the prompt");
    check(exit_prompt_owner(0b11, 0b11, 0b11, 0b11) == -1, "Nobody stepped on: no prompt");
    check(exit_prompt_owner(0b01, 0b11, 0b11, 0b11) == -1, "A step off asks nobody");
    check(exit_prompt_owner(0b11, 0b11, 0b01, 0b11) == -1, "A death on the mat asks nobody (no slot-0 fallback)");
    check(exit_prompt_owner(0b10, 0b00, 0b00, 0b11) == -1, "A defeated member does not own a prompt");
    check(exit_prompt_owner(0b1100, 0b0000, 0b1111, 0b1111) == 2, "Two at once: the lowest");
    check(exit_prompt_owner(0b0100, 0b0000, 0b0100, 0b0011) == -1, "An absent slot owns nothing");
    // The side rule.
    check(exit_leavers(0b11, 0b11, 0b01) == 0b01, "One of two living members leaves alone (a fork)");
    check(exit_leavers(0b11, 0b11, 0b11) == 0b11, "Every living member goes: the group move");
    check(exit_leavers(0b11, 0b01, 0b01) == 0b11, "The only living member takes the defeated one along");
    check(exit_leavers(0b111, 0b101, 0b001) == 0b001, "A defeated member stays with the living one who stays");
    check(exit_leavers(0b111, 0b101, 0b011) == 0b001, "A defeated yes does not leave a living stayer");
    check(exit_leavers(0b11, 0b01, 0b10) == 0, "Only defeated members chose to go: nobody leaves");
    check(exit_leavers(0b1111, 0b0110, 0b0110) == 0b1111, "All living go: everyone, the defeated included");
}

// (One engine at a time: an online engine must be the only one of the
// process, SEAMLESS.md 6.12.)
void test_engine_rules() {
    OnlineRules rules;
    {
        Engine engine{dw4::MountedResources{}};
        check(engine.online() == OnlineRules{}, "A new engine plays the original (every rule off)");
        check(engine.player_cameras() == 0 && !engine.online_shared(), "No per-slot cameras, no shared instance");
        engine.set_view_slot(2);
        check(engine.picture_slot() == 2, "Without per-slot cameras the picture slot is the view slot");
        rules.player_cameras = 0b11;
        engine.set_online(rules);
        check(engine.player_cameras() == 0b11 && !engine.online_shared(), "Cameras alone are not an online instance");
        engine.online_intent(1, IntentKind::give_up, 0);
        check(!engine.take_split(), "Intents act only online");
        rules.enabled = true;
        rules.present = 0b11;
        engine.set_online(rules);
        check(engine.online_shared(), "Two present members share the instance");
        // Forks of an engine without a stage (nobody lives): the side rule
        // takes every member along on an exit (no fork). The online suite's
        // real-stage scenarios cover forks of living members from both copies.
        engine.set_split_identity(1);
        check(engine.online_split(0b01, ForkKind::exit), "Nobody lives: everyone goes with the exit, this copy too");
        check(!engine.take_split() && engine.online().present == 0b11, "No fork without a living stayer");
    }
    {
        // Give up seen from the partner's copy: the member who gave up is gone.
        Engine down{dw4::MountedResources{}};
        rules.present = rules.player_cameras = 0b11;
        down.set_online(rules);
        down.set_split_identity(0);
        down.online_intent(1, IntentKind::give_up, 0);
        const auto gave_up = down.take_split();
        check(gave_up && gave_up->leavers == 0b10 && !gave_up->local_leaves, "Give up forks the defeated member off");
        check(!down.take_split(), "take_split hands it out once");
        check(down.online().present == 0b01 && down.player_cameras() == 0b01, "The member who gave up is gone, its camera too");
        check(!down.online_shared(), "One member left: no shared instance");
        down.online_intent(0, IntentKind::give_up, 0);
        check(!down.take_split(), "A member alone gives up into the original game over, no fork");
    }
    {
        // Give up seen from the copy of the member who gives up: the others go.
        Engine own{dw4::MountedResources{}};
        rules.present = rules.player_cameras = 0b111;
        own.set_online(rules);
        own.set_split_identity(0);
        own.online_intent(0, IntentKind::give_up, 0);
        const auto split = own.take_split();
        check(split && split->leavers == 0b001 && split->local_leaves, "The copy of the member who gives up takes its side");
        check(own.online().present == 0b001 && own.player_cameras() == 0b001, "The others are gone from that copy");
    }
}

void test_camera_hash() {
    Engine a{dw4::MountedResources{}}, b{dw4::MountedResources{}};
    auto* cameras_a = a.get<CameraManager>();
    auto* cameras_b = b.get<CameraManager>();
    check(cameras_a && cameras_b, "Every engine has the camera manager");
    check(cameras_a->player_box_hash() == cameras_b->player_box_hash(), "Equal engines have equal camera states");
    OnlineRules rules;
    rules.player_cameras = 0b101;
    a.set_online(rules);
    check(!cameras_a->player_box_centre(0) && !cameras_a->player_box_centre(1), "Boxes start without a stage");
}

} // namespace

int main() {
    try {
        test_union_one_camera();
        test_union_two_cameras();
        test_picture_only_pass();
        test_union_lod_nearest();
        test_exit_owner_and_sides();
        test_engine_rules();
        test_camera_hash();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " online checks passed\n";
    return 0;
}
