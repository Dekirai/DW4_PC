// Overhead name tags: the projection onto the window (the game camera, the
// renderer's widescreen widening, the window's pixel scale), the head point,
// the distance fade, which labels show in which mode, and the smooth-motion
// blend with jumps and remote players.
#include "overhead_labels.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <cmath>
#include <optional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game;
using dw4::engine::Vec3f;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float tolerance = 0.05f) { return std::abs(a - b) <= tolerance; }

LabelView view_4x3() {
    LabelView view;
    view.camera.view = dw4::engine::look_at({0, 0, 100}, {0, 0, 0}, {0, 1, 0});
    view.camera.projection = dw4::engine::perspective_horizontal(54.43f * 3.14159265f / 180.0f, 640.0f / 448.0f, 1.0f, 4000.0f);
    view.wide_x = 160;
    view.wide_y = 0;
    view.wide_width = 960;
    view.wide_height = 720;
    view.window_height = 720;
    return view;
}

void projection() {
    auto view = view_4x3();
    const auto centre = project_label({0, 0, 0}, view);
    check(centre && near(centre->x, 640) && near(centre->y, 360), "the look-at target is the picture's centre");
    check(near(centre->distance, 100, 0.01f), "distance from the eye");
    const auto above = project_label({0, 10, 0}, view);
    check(above && near(above->x, 640) && above->y < centre->y, "higher in the world is higher on the screen (y down)");
    const auto right = project_label({10, 0, 0}, view);
    check(right && right->x > centre->x, "x to the right");
    check(!project_label({0, 0, 200}, view), "behind the camera: none");
    check(!project_label({1000, 0, 0}, view), "far outside the picture: none");
    // Widescreen: the 3D view fills a 1280-wide window with column 0 of the
    // projection widened; a point inside the 4:3 part stays where the 4:3
    // picture had it.
    LabelView wide = view;
    wide.wide_x = 0;
    wide.wide_width = 1280;
    wide.widen = (960.0f / 720.0f) / (1280.0f / 720.0f);
    for (const Vec3f p : {Vec3f{0, 0, 0}, Vec3f{20, 5, 0}, Vec3f{-30, -8, 10}}) {
        const auto narrow_point = project_label(p, view);
        const auto wide_point = project_label(p, wide);
        check(narrow_point && wide_point && near(narrow_point->x, wide_point->x, 0.1f) && near(narrow_point->y, wide_point->y, 0.1f),
              "widescreen keeps the 4:3 positions");
    }
    // The window's pixels per ImGui unit (high DPI): ImGui units are halved.
    LabelView dpi = view;
    dpi.scale_x = dpi.scale_y = 2.0f;
    const auto scaled = project_label({0, 0, 0}, dpi);
    check(scaled && near(scaled->x, 320) && near(scaled->y, 180), "framebuffer scale");
    // A picture lower in the window (wide_y from the bottom, GL style).
    LabelView letterbox = view;
    letterbox.window_height = 800;
    letterbox.wide_y = 40;
    const auto boxed = project_label({0, 0, 0}, letterbox);
    check(boxed && near(boxed->y, 800 - 40 - 360), "viewport y counts from the bottom");
    const auto eye = camera_eye(view.camera.view);
    check(near(eye.x, 0) && near(eye.y, 0) && near(eye.z, 100), "camera eye");
}

void fade_and_modes() {
    check(label_fade(100) == 1.0f && label_fade(350) == 1.0f && label_fade(700) == 0.0f && near(label_fade(525), 0.5f), "fade");
    check(parse_label_mode("auto") == LabelMode::automatic && parse_label_mode("ALL") == LabelMode::all &&
              parse_label_mode("others") == LabelMode::others && parse_label_mode("off") == LabelMode::off &&
              parse_label_mode("bogus") == LabelMode::automatic,
          "modes");
    for (const auto mode : {LabelMode::automatic, LabelMode::all, LabelMode::others, LabelMode::off})
        check(parse_label_mode(label_mode_name(mode)) == mode, "mode names round trip");
    check(!label_visible(LabelMode::automatic, true, 1) && !label_visible(LabelMode::automatic, false, 1), "auto: alone, none");
    check(label_visible(LabelMode::automatic, true, 2) && label_visible(LabelMode::automatic, false, 4), "auto: everyone with two or more");
    check(label_visible(LabelMode::all, true, 1), "all: also alone");
    check(!label_visible(LabelMode::others, true, 3) && label_visible(LabelMode::others, false, 3), "others: not this machine's player");
    check(!label_visible(LabelMode::off, false, 4), "off");
}

void heads() {
    dw4::engine::Node node;
    node.matrix = dw4::engine::translation(10, 5, -3);
    const auto table = head_point(node, 0);
    check(near(table.x, 10) && near(table.z, -3) && near(table.y, 5 + form_label_height(0) + 1), "no box: the form's height");
    check(form_label_height(0) < 10.0f, "a rookie's table height is about its figure (Agumon 7.3)");
    check(form_label_height(-1) == 30.0f && form_label_height(16) == 30.0f, "unknown form");
    dw4::engine::Node body;
    body.has_bound = true;
    body.bound_min = {-5, 0, -5};
    body.bound_max = {5, 26, 5};
    node.children.push_back(&body);
    const auto boxed = head_point(node, 0);
    check(near(boxed.y, 5 + 26 * 0.45f + 1), "a share of the bounding box's top");
    body.bound_max.y = 900; // a cull box of a whole level
    check(near(head_point(node, 11).y, 5 + form_label_height(11) + 1), "an absurd box gives way to the table");
    check(near(head_point(node, 11, 5.0f + 14.0f).y, 5 + 14 + 14 * 0.25f), "the highest joint wins, a share of its height above");
    check(near(head_point(node, 0, 5.0f + 5.6f).y, 5 + 5.6f + 1.8f), "Agumon: just above its head (7.3), not a body above");
    check(near(head_point(node, 1, 5.0f + 7.7f).y, 5 + 7.7f + 7.7f * 0.25f), "Veemon: just above its ears' roots (its head's top)");
    check(near(head_point(node, 11, 5.0f + 40.0f).y, 5 + 40 + 6), "a tall figure: the margin stays small");
    check(near(head_point(node, 11, 5.0f + 1.0f).y, 5 + form_label_height(11) + 1), "a joint at the feet is no head");
}

OverheadLabel label(std::uint64_t key, Vec3f head) {
    OverheadLabel l;
    l.key = key;
    l.head = head;
    l.hp = l.max_hp = 100;
    return l;
}

void blending() {
    OverheadLabels labels;
    labels.capture({label(0, {0, 30, 0}), label(1, {100, 30, 0})}, 2, "stag0101");
    labels.capture({label(0, {10, 30, 0}), label(1, {200, 30, 0}), label(2, {0, 0, 0})}, 3, "stag0101");
    const auto half = labels.blended(0.5f);
    check(half.size() == 3 && labels.players() == 3, "three labels");
    check(near(half[0].head.x, 5), "blended halfway");
    check(near(half[1].head.x, 200), "a jump is drawn where it is now");
    check(near(half[2].head.x, 0), "a new label is drawn where it is");
    check(near(labels.blended(0.0f)[0].head.x, 0) && near(labels.blended(1.0f)[0].head.x, 10), "alpha 0 previous, 1 current");
    check(near(labels.blended(-3.0f)[0].head.x, 0) && near(labels.blended(7.0f)[0].head.x, 10), "alpha clamped");
    // Remote players: only those in this stage, keyed apart from the slots.
    OverheadLabels::RemotePlayer here{7, "Taiki", "STAG0101", {50, 0, 50}, 0, 12, 80, 100, 10, 20};
    OverheadLabels::RemotePlayer away{8, "Kira", "dng101b1", {0, 0, 0}, 0, 30, 1, 1, 1, 1};
    labels.set_remote_players({here, away});
    labels.capture({label(0, {10, 30, 0})}, 1, "stag0101");
    const auto& now = labels.current();
    check(now.size() == 2 && labels.players() == 2, "one remote player in this stage");
    check(now[1].key == 0x100 + 7 && now[1].slot == -1 && now[1].name == "Taiki" && now[1].level == 12, "the remote label");
    check(near(now[1].head.y, form_label_height(0) + 1), "a remote head from the form table");
    labels.reset();
    check(labels.current().empty() && labels.players() == 0, "reset");
}

void engine_capture() {
    // An engine without game data: the joined player has no model yet, so
    // no label, but it counts as a player.
    dw4::engine::Engine engine{dw4::MountedResources{}};
    OverheadLabels labels;
    labels.capture(engine, 0);
    check(labels.current().empty() && labels.players() == 1, "player 1 joined, no model");
    check(OverheadLabels::hidden(engine), "no stage: hidden");
}

} // namespace

// The occlusion from the picture's depth buffer: the probes above the head
// point, the projection's depth turned back into a distance, the verdict.
void occlusion() {
    const auto view = view_4x3();
    const Vec3f head{0, 0, 0};
    const auto probes = occlusion_probes(head, view);
    check(probes.size() == 3, "three probes");
    check(probes[0].y < 360 && probes[0].y > 340 && near(probes[0].x, 640, 0.5f), "the centre probe stands just above the head point");
    check(near(probes[1].x, 640 - 720 * 0.012f, 0.5f) && near(probes[2].x, 640 + 720 * 0.012f, 0.5f) && probes[1].y == probes[0].y,
          "the side probes beside it");
    check(near(probes[0].depth, 100, 0.01f), "a probe's depth is its distance along the view axis");
    // The depth the projection writes for a point at distance d, read back.
    const auto& projection = view.camera.projection;
    auto window_depth = [&](float distance) {
        const float z = -distance; // eye space
        const float clip_z = z * projection(2, 2) + projection(3, 2), clip_w = z * projection(2, 3) + projection(3, 3);
        return (clip_z / clip_w) * 0.5f + 0.5f;
    };
    for (const float d : {2.0f, 50.0f, 100.0f, 1000.0f})
        check(view_distance(window_depth(d), projection) && near(*view_distance(window_depth(d), projection), d, d * 0.002f),
              "a window depth reads back as its distance, " + std::to_string(d));
    check(!view_distance(1.0f, projection), "the cleared far plane is no distance");
    dw4::engine::Mat4 ortho; // w = 1 everywhere
    check(!view_distance(0.5f, ortho), "an orthographic projection gives none");
    auto wall_at = [&](float distance) { return [=](float, float) -> std::optional<float> { return window_depth(distance); }; };
    check(occluded_in_picture(probes, wall_at(50), projection), "a wall halfway to the camera hides the player");
    check(!occluded_in_picture(probes, wall_at(99), projection), "within the margin: the player's own surroundings");
    check(!occluded_in_picture(probes, wall_at(300), projection), "the scene behind the player");
    check(!occluded_in_picture(probes, [](float, float) -> std::optional<float> { return 1.0f; }, projection), "nothing drawn");
    check(!occluded_in_picture(probes, [](float, float) -> std::optional<float> { return std::nullopt; }, projection), "no depth to read");
    check(!occluded_in_picture(probes, [&](float x, float) -> std::optional<float> { return window_depth(x > 645 ? 300 : 50); }, projection),
          "a probe beside a pillar sees the player");
    check(!occluded_in_picture({}, wall_at(50), projection), "no probes (behind the camera)");
    LabelView behind = view;
    behind.camera.view = dw4::engine::look_at({0, 0, -100}, {0, 0, -200}, {0, 1, 0});
    check(occlusion_probes(head, behind).empty(), "behind the camera: no probes");
    // Eased over eight presented frames, a new label at once.
    OverheadLabels labels;
    check(labels.ease_occlusion(1, true) == 1.0f && labels.ease_occlusion(2, false) == 0.0f, "a new label starts as it is");
    float value = 0;
    for (int i = 0; i < 4; ++i) value = labels.ease_occlusion(1, false);
    check(near(value, 0.5f, 0.001f), "half way back after four frames");
    for (int i = 0; i < 8; ++i) value = labels.ease_occlusion(1, false);
    check(value == 0.0f, "seen again after eight");
    labels.ease_occlusion(2, true);
    check(near(labels.ease_occlusion(2, true), 0.25f, 0.001f), "hiding eases in too");
    labels.reset();
    check(labels.ease_occlusion(2, false) == 0.0f, "reset forgets the eased values");
}

// The layout: the nearer tag keeps its place, one behind it stacks above,
// a hidden player's tag is faint, no HP or the mode hides it.
void layout() {
    const auto view = view_4x3();
    auto size = [](const OverheadLabel&) { return std::make_pair(80.0f, 30.0f); };
    std::vector<OverheadLabel> tags = {label(0, {0, 0, 0}), label(1, {0, 0, -20}), label(2, {30, 0, 0})};
    tags[0].slot = 0;
    tags[1].slot = 1;
    tags[2].slot = 2;
    auto boxes = layout_overhead_labels(tags, view, LabelMode::all, 3, size);
    check(boxes[0].shown && near(boxes[0].x, 640 - 40) && near(boxes[0].y, 360 - 30), "the nearest tag right above its head point");
    check(boxes[1].shown && boxes[1].y + boxes[1].height + 2.0f <= boxes[0].y + 0.01f, "the one behind it moves up above it");
    check(near(boxes[1].anchor_x, 640) && boxes[1].anchor_y > boxes[1].y + boxes[1].height, "and keeps its head point (the line down)");
    check(boxes[2].shown && near(boxes[2].y, boxes[0].y, 0.5f), "a tag beside them stays where it is");
    tags[0].occlusion = 1.0f;
    tags[2].occlusion = 0.5f;
    boxes = layout_overhead_labels(tags, view, LabelMode::all, 3, size);
    check(near(boxes[0].alpha, 0.35f, 0.001f) && near(boxes[2].alpha, 1.0f - 0.65f * 0.5f, 0.001f) && near(boxes[1].alpha, 1.0f),
          "a hidden player's tag is faint, eased in between");
    tags[1].hp = 0;
    tags[2].own = true;
    boxes = layout_overhead_labels(tags, view, LabelMode::others, 3, size);
    check(boxes[0].shown && !boxes[1].shown && !boxes[2].shown, "no HP and the own tag in 'others' do not show");
    check(!layout_overhead_labels(tags, view, LabelMode::off, 3, size)[0].shown, "off shows none");
}

int main() {
    try {
        projection();
        fade_and_modes();
        heads();
        blending();
        engine_capture();
        occlusion();
        layout();
        std::cout << checks << " name tag checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Name tag test failure: " << error.what() << '\n';
        return 1;
    }
}
