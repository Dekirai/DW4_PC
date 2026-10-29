// Overhead name tags: the projection onto the window (the game camera, the
// renderer's widescreen widening, the window's pixel scale), the head point,
// the distance fade, which labels show in which mode, and the smooth-motion
// blend with jumps and remote players.
#include "overhead_labels.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <cmath>
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
    check(near(table.x, 10) && near(table.z, -3) && near(table.y, 5 + form_label_height(0) + 3), "no box: the form's height");
    check(form_label_height(-1) == 30.0f && form_label_height(16) == 30.0f, "unknown form");
    dw4::engine::Node body;
    body.has_bound = true;
    body.bound_min = {-5, 0, -5};
    body.bound_max = {5, 26, 5};
    node.children.push_back(&body);
    const auto boxed = head_point(node, 0);
    check(near(boxed.y, 5 + 26 * 0.6f + 3), "a share of the bounding box's top");
    body.bound_max.y = 900; // a cull box of a whole level
    check(near(head_point(node, 11).y, 5 + form_label_height(11) + 3), "an absurd box gives way to the table");
    check(near(head_point(node, 11, 5.0f + 14.0f).y, 5 + 14 + 7), "the highest joint wins");
    check(near(head_point(node, 11, 5.0f + 1.0f).y, 5 + form_label_height(11) + 3), "a joint at the feet is no head");
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
    check(near(now[1].head.y, form_label_height(0) + 3), "a remote head from the form table");
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

int main() {
    try {
        projection();
        fade_and_modes();
        heads();
        blending();
        engine_capture();
        std::cout << checks << " name tag checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Name tag test failure: " << error.what() << '\n';
        return 1;
    }
}
