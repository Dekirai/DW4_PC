// Overhead name tags: the projection onto the window (the game camera, the
// renderer's widescreen widening, the window's pixel scale), the head point
// and its follow of the head (traces of a jump, a run and a leap), the
// distance fade, which labels show in which mode, and the smooth-motion
// blend with jumps and remote players.
#include "overhead_labels.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

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

// Round 54's measurement of the 16 forms (world units above the model's
// origin; hidden 4-player runs with the initial weapons, DW4_RENDER=1, every
// second frame): the skinned figure as the renderer draws it (the vertices
// through the palette, as the shader does) and the pose's joints. "field" is
// the SQ_WAIT stance in dng101b1, "lobby" the one in stagML11 (another idle
// for some forms). head top: the highest vertex that moves with the head
// joint (Head / Head01 and below); near top: the highest vertex (weapons
// left out) within 2 units of the head joint horizontally, which also takes
// a collar or wings beside the head - the form's table height is the higher
// of the two stances. Highest joint: any joint, the stance (both), a run
// (SQ_WALK), the combo (SQ_ATCK1 V*), holding A to charge (SQ_ATCK1 K*), the
// charged attack's leap (SQ_ATCKTM; the model's origin stays on the ground)
// and a technique (SQ_WAZA). Round 50's rule put the tag a quarter of the
// highest joint above it (1.8 at least): its range in the stance, in a run
// and its highest anywhere in those actions. While digivolving no player
// model is drawn (the Digi-Lab window hides the tags; the evolution scene
// stagML14 shows its own panels).
struct MeasuredForm {
    const char* name;
    float head_top_field, head_top_lobby, near_top_field, near_top_lobby, head_joint_field, head_joint_lobby;
    float joint_stance, joint_run, joint_attack, joint_charge, joint_leap, joint_technique;
    float r50_stance_low, r50_stance_high, r50_run_low, r50_run_high, r50_actions_high;
};
constexpr MeasuredForm kMeasured[16] = {
    {"Agumon", 8.3f, 8.3f, 8.3f, 8.3f, 6.0f, 6.0f, 6.0f, 8.0f, 9.1f, 5.8f, 17.2f, 6.0f, 7.2f, 7.8f, 6.2f, 10.0f, 21.5f},
    {"Veemon", 9.7f, 8.7f, 9.2f, 8.7f, 6.1f, 6.0f, 7.8f, 8.0f, 7.3f, 5.8f, 17.2f, 7.1f, 9.0f, 9.7f, 6.2f, 10.0f, 21.5f},
    {"Guilmon", 9.6f, 9.9f, 8.7f, 9.6f, 5.1f, 6.0f, 7.8f, 8.0f, 6.1f, 5.8f, 17.2f, 6.0f, 6.2f, 9.7f, 6.2f, 10.0f, 21.5f},
    {"Dorumon", 8.4f, 8.2f, 8.2f, 8.2f, 5.1f, 6.0f, 7.8f, 8.0f, 7.1f, 5.8f, 17.2f, 6.0f, 6.2f, 9.7f, 6.2f, 10.0f, 21.5f},
    {"WereGarurumon", 11.3f, 13.8f, 11.9f, 13.8f, 8.6f, 11.2f, 11.2f, 10.6f, 11.2f, 11.9f, 24.0f, 9.2f, 11.0f, 14.0f, 11.1f, 13.2f, 30.0f},
    {"H-Kabuterimon", 16.4f, 16.0f, 13.9f, 14.0f, 11.2f, 11.2f, 11.2f, 11.2f, 14.2f, 11.9f, 24.0f, 10.7f, 14.0f, 14.1f, 11.7f, 14.0f, 30.0f},
    {"WarGreymon", 12.1f, 13.4f, 12.1f, 12.8f, 9.4f, 10.8f, 10.8f, 12.1f, 12.8f, 10.6f, 11.4f, 9.4f, 11.4f, 13.4f, 11.8f, 15.1f, 16.0f},
    {"Cl-Angemon", 12.6f, 12.7f, 12.6f, 12.7f, 11.1f, 11.3f, 11.3f, 11.1f, 11.1f, 11.9f, 24.0f, 16.1f, 13.9f, 14.1f, 11.7f, 13.9f, 30.0f},
    {"Beelzemon BM", 13.7f, 13.8f, 13.7f, 13.8f, 11.2f, 11.2f, 13.2f, 12.9f, 13.1f, 8.8f, 10.3f, 11.9f, 16.2f, 16.5f, 11.8f, 16.1f, 16.3f},
    {"Alphamon", 14.3f, 14.4f, 13.2f, 13.2f, 11.3f, 11.3f, 11.3f, 11.3f, 11.3f, 11.9f, 24.0f, 10.8f, 14.1f, 14.1f, 11.7f, 14.1f, 30.0f},
    {"BK WarGreymon", 12.1f, 13.4f, 12.1f, 12.8f, 9.4f, 10.8f, 10.8f, 12.1f, 12.8f, 10.6f, 11.4f, 16.1f, 11.4f, 13.4f, 11.8f, 15.1f, 20.1f},
    {"IPDramon FM", 13.7f, 13.5f, 13.7f, 13.5f, 11.2f, 11.2f, 13.5f, 11.2f, 11.2f, 8.8f, 10.3f, 16.1f, 14.0f, 16.9f, 11.8f, 14.1f, 20.1f},
    {"IPDramon PM", 14.0f, 13.7f, 14.0f, 13.7f, 11.2f, 11.2f, 13.6f, 11.2f, 11.2f, 9.4f, 24.0f, 16.1f, 14.0f, 17.0f, 11.7f, 14.1f, 30.0f},
    {"MetalGarurumon", 14.3f, 14.3f, 13.7f, 13.7f, 11.3f, 11.3f, 11.3f, 11.1f, 11.2f, 8.4f, 10.3f, 16.1f, 14.0f, 14.2f, 11.1f, 13.9f, 20.1f},
    {"Gallantmon CM", 13.3f, 13.4f, 13.3f, 13.3f, 11.2f, 11.2f, 11.2f, 11.2f, 11.2f, 9.4f, 24.0f, 16.1f, 14.0f, 14.1f, 11.7f, 14.1f, 30.0f},
    {"Susanomon", 13.6f, 13.6f, 13.6f, 13.6f, 11.2f, 11.2f, 11.7f, 11.2f, 14.2f, 9.4f, 24.0f, 16.1f, 14.0f, 14.7f, 11.7f, 14.1f, 30.0f},
};

void heads() {
    dw4::engine::Node node;
    node.matrix = dw4::engine::translation(10, 5, -3);
    const auto table = head_point(node, 0);
    check(near(table.x, 10) && near(table.z, -3) && near(table.y, 5 + form_label_height(0) + 1), "a form: its height, 1 above");
    check(form_label_height(-1) == 30.0f && form_label_height(16) == 30.0f, "unknown form");
    // A form's tag keeps its height whatever the pose does: a raised weapon
    // arm, an attack's leap, a run's bounce (round 50 followed the highest
    // joint: Agumon's tag hopped 6.2..10.0 in a run, up to 21.5 in a leap).
    for (const float joint : {5.6f, 9.1f, 17.2f, 1.0f, 40.0f})
        check(near(head_point(node, 0, 5.0f + joint).y, 5 + form_label_height(0) + 1), "the pose does not move a form's tag");
    // The head point stands over the head joint (the followed offset moves
    // it sideways) and rises with the head (the followed lift).
    const auto over = head_point(node, 3, std::nullopt, Vec3f{1.5f, 0.0f, -0.5f});
    check(near(over.x, 11.5f) && near(over.z, -3.5f) && near(over.y, 5 + form_head(3)->height + 1), "over the head joint at its height");
    check(near(head_point(node, 3, std::nullopt, Vec3f{1.5f, 2.5f, -0.5f}).y, 5 + form_head(3)->height + 1 + 2.5f), "lifted with the head");
    check(near(head_point(node, 3, std::nullopt, Vec3f{0.0f, -4.0f, 0.0f}).y, over.y), "never below its height");
    // Without a pose (no head joint): the higher stance's height.
    check(near(head_point(node, 4).y, 5 + form_label_height(4) + 1) && form_label_height(4) > form_head(4)->height + 1.5f,
          "no pose: the higher stance (WereGarurumon's upright lobby stance)");
    // A model of no known form: the bounding box, the highest joint, else 30.
    dw4::engine::Node body;
    body.has_bound = true;
    body.bound_min = {-5, 0, -5};
    body.bound_max = {5, 26, 5};
    node.children.push_back(&body);
    check(near(head_point(node, 0).y, 5 + form_label_height(0) + 1), "a form ignores its bounding box");
    check(near(head_point(node, -1).y, 5 + 26 * 0.45f + 1), "no form: a share of the bounding box's top");
    body.bound_max.y = 900; // a cull box of a whole level
    check(near(head_point(node, -1).y, 5 + 30 + 1), "an absurd box gives way to 30 units");
    check(near(head_point(node, -1, 5.0f + 14.0f).y, 5 + 14 + 14 * 0.25f), "no form: the highest joint, a share of its height above");
    check(near(head_point(node, 16, 5.0f + 5.6f).y, 5 + 5.6f + 1.8f), "no form: at least 1.8 above the joint");
    check(near(head_point(node, -1, 5.0f + 40.0f).y, 5 + 40 + 6), "no form, a tall figure: the margin stays small");
    check(near(head_point(node, -1, 5.0f + 1.0f).y, 5 + 30 + 1), "no form: a joint at the feet is no head");
}

void measured_forms() {
    for (int species = 0; species < 16; ++species) {
        const auto& m = kMeasured[species];
        const std::string name = m.name;
        const auto* head = form_head(species);
        check(head != nullptr, name + ": in the table");
        // The joint is the measured standing height (the higher stance).
        check(near(head->joint, std::max(m.head_joint_field, m.head_joint_lobby), 0.01f), name + ": the head joint's standing height");
        // The tag in each stance (the head joint at its standing height
        // there): the bars clear the head and what is close to it, and do
        // not float above it by more than a unit (round 54 put WereGarurumon's
        // 2.9 above its field stance's head).
        for (const auto& [joint, top, stance] : {std::tuple{m.head_joint_field, m.near_top_field, "field"},
                                                 std::tuple{m.head_joint_lobby, m.near_top_lobby, "lobby"}}) {
            const float rest = head->height + form_head_rise(species, joint);
            check(rest >= top - 0.01f, name + ": the bars clear the head in the " + stance + " stance");
            check(rest - top <= 1.0f, name + ": not far above the head in the " + stance + " stance");
        }
        // Without a pose the higher stance, both covered.
        check(form_label_height(species) >= std::max(m.near_top_field, m.near_top_lobby) - 0.01f &&
                  form_label_height(species) <= std::max(m.near_top_field, m.near_top_lobby) + 1.0f,
              name + ": no pose: the higher stance");
        // The head joint (the follow's reference) sits inside the figure.
        check(m.head_joint_field < m.head_top_field && m.head_joint_lobby < m.head_top_lobby, name + ": the head joint below its top");
    }
    // Left out on purpose: weapons held up beside the head (Beelzemon BM's
    // gun at 25.5) and horns further out ahead (H-Kabuterimon's at 16.4).
    check(form_label_height(8) + 1.0f < 25.5f && form_label_height(5) < kMeasured[5].head_top_field, "left out: weapons, far horns");
    check(!form_head(-1) && !form_head(16) && form_head_rise(16, 50.0f) == 0.0f, "another species: no head");
    // WereGarurumon rests on its field stance (the reviewed 3.6 to 3.9 above
    // the head in every dungeon), its upright lobby stance lifts the tag.
    check(form_head(4)->height + 1.0f - kMeasured[4].head_top_field < 1.7f, "WereGarurumon's field tag at its head");
    check(form_head_rise(4, kMeasured[4].head_joint_lobby) > 1.5f && form_head_rise(4, kMeasured[4].head_joint_field) == 0.0f,
          "WereGarurumon's lobby stance lifts it");
}

// Round 54's trace of the figure (world units above the model's origin): the
// head joint and the head's top (its highest vertex), every frame unless
// noted.
// Guilmon's jump in the lobby (J4, frames 612..680: the ears flap up to
// 12.6, 2.0 over the bars of a steady tag).
constexpr float kGuilmonJumpJoint[] = {6.0f, 5.9f, 5.9f, 5.9f, 5.9f, 5.8f, 5.8f, 5.8f, 5.8f, 5.8f, 6.0f, 6.3f, 6.5f, 6.8f, 7.1f, 7.3f, 7.6f, 7.9f,
                                       7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.9f, 7.8f,
                                       7.8f, 7.8f, 7.8f, 7.8f, 7.7f, 7.7f, 7.8f, 7.8f, 7.8f, 7.8f, 7.8f, 7.8f, 7.8f, 7.7f, 7.2f, 6.7f, 6.1f, 5.6f,
                                       5.1f, 4.6f, 4.1f, 4.4f, 4.6f, 4.9f, 5.1f, 5.3f, 5.5f, 5.7f, 5.8f, 5.8f, 5.8f, 5.8f, 5.8f};
constexpr float kGuilmonJumpTop[] = {9.8f,  9.8f,  9.8f,  9.7f,  9.7f,  9.7f,  9.6f,  9.6f,  9.5f,  9.5f,  9.5f,  9.7f,  9.9f,  10.1f,
                                     10.2f, 10.3f, 10.4f, 10.5f, 10.6f, 10.7f, 10.8f, 10.9f, 10.9f, 10.8f, 10.8f, 10.7f, 10.7f, 10.6f,
                                     10.6f, 10.8f, 11.0f, 11.2f, 11.4f, 11.8f, 12.1f, 12.4f, 12.6f, 12.5f, 12.4f, 12.2f, 12.1f, 12.1f,
                                     12.2f, 12.3f, 12.3f, 12.4f, 12.5f, 12.5f, 12.5f, 12.3f, 11.8f, 11.2f, 10.4f, 9.8f,  9.3f,  8.8f,
                                     8.2f,  8.5f,  8.8f,  9.0f,  9.2f,  9.4f,  9.5f,  9.6f,  9.6f,  9.6f,  9.5f,  9.5f,  9.4f};
// Agumon's run in dng101b1 (h1, every second frame 600..760; a short stop
// near the end): each stride bounces the head 3.6 units.
constexpr float kAgumonRunJoint[] = {5.9f, 6.1f, 6.4f, 6.1f, 5.3f, 4.6f, 4.6f, 5.3f, 6.3f, 7.3f, 7.9f, 7.9f, 7.3f, 6.3f, 5.3f, 4.6f, 4.7f,
                                     5.4f, 6.5f, 7.4f, 7.9f, 8.0f, 7.6f, 6.7f, 5.6f, 4.8f, 4.4f, 4.9f, 5.8f, 6.9f, 7.7f, 8.0f, 7.7f, 6.8f,
                                     5.8f, 4.8f, 4.5f, 4.9f, 5.9f, 7.0f, 7.8f, 8.0f, 7.8f, 7.1f, 6.1f, 5.2f, 4.6f, 4.7f, 5.4f, 6.4f, 7.3f,
                                     7.9f, 7.9f, 7.2f, 6.3f, 5.3f, 4.6f, 4.7f, 5.5f, 6.5f, 7.5f, 7.7f, 7.3f, 6.6f, 6.0f, 5.9f, 5.8f, 5.8f,
                                     5.7f, 5.7f, 5.6f, 5.9f, 6.2f, 6.0f, 5.3f, 4.6f, 4.6f, 5.3f, 6.3f, 7.3f, 7.9f};
constexpr float kAgumonRunTop[] = {8.2f, 8.5f,  8.8f,  8.5f, 7.7f, 6.9f, 6.9f, 7.6f,  8.7f,  9.6f,  10.2f, 10.2f, 9.6f, 8.6f, 7.6f, 6.9f, 7.0f,
                                   7.7f, 8.8f,  9.7f,  10.2f, 10.3f, 9.9f, 9.0f, 8.0f,  7.1f,  6.7f,  7.2f, 8.1f, 9.2f, 10.0f, 10.3f, 10.0f, 9.1f,
                                   8.1f, 7.1f,  6.7f,  7.2f, 8.2f, 9.3f, 10.1f, 10.3f, 10.1f, 9.4f,  8.5f,  7.5f, 6.9f, 6.9f, 7.7f, 8.7f, 9.6f,
                                   10.2f, 10.2f, 9.5f, 8.6f, 7.6f, 6.9f, 7.0f, 7.8f,  8.8f,  9.8f,  10.0f, 9.6f, 8.9f, 8.4f, 8.2f, 8.1f, 8.1f,
                                   8.0f, 8.0f,  7.9f,  8.2f, 8.6f, 8.4f, 7.6f, 6.9f,  6.9f,  7.6f,  8.7f,  9.6f, 10.2f};
// Agumon's charged attack in dng101b1 (F4, frames 955..1000): the leap
// carries the head 8.3 over the bars of a steady tag.
constexpr float kAgumonLeapJoint[] = {5.1f, 5.0f,  4.9f,  4.9f,  4.8f, 4.8f, 4.8f,  6.9f,  8.5f, 8.6f, 9.9f, 11.5f, 13.6f, 15.1f, 15.3f, 14.3f,
                                      12.5f, 10.7f, 9.9f, 9.6f, 10.3f, 10.4f, 9.8f, 7.8f, 4.8f, 4.6f, 4.5f, 4.6f,  4.6f,  4.6f,  4.6f,  4.9f,
                                      4.8f, 4.6f,  4.4f,  4.3f,  4.2f, 4.1f, 4.1f,  4.3f,  4.6f, 4.9f, 5.1f, 5.3f,  5.6f,  5.7f};
constexpr float kAgumonLeapTop[] = {7.3f,  7.2f,  7.2f,  7.1f, 7.0f, 7.0f, 7.0f, 9.2f, 10.5f, 10.0f, 11.1f, 13.9f, 16.4f, 17.4f, 17.6f, 16.6f,
                                    14.4f, 12.0f, 10.8f, 11.3f, 12.9f, 13.3f, 12.0f, 10.1f, 6.9f, 6.7f, 6.5f, 6.7f, 6.7f, 6.7f, 6.7f, 7.0f,
                                    6.9f,  6.8f,  6.6f,  6.5f, 6.5f, 6.4f, 6.3f, 6.6f, 6.9f, 7.1f, 7.3f, 7.6f, 7.8f, 8.0f};

// The tag's bars (above the origin) along a trace, `steps` simulation steps
// per sample; `worst` the most the head's top reached over them.
std::vector<float> follow_trace(int species, const float* joint, const float* top, std::size_t count, int steps, float& worst) {
    std::vector<float> bars;
    std::optional<HeadFollow> follow;
    worst = -100.0f;
    for (std::size_t i = 0; i < count; ++i) {
        follow = advance_head_follow(follow, Vec3f{}, form_head_rise(species, joint[i]), steps);
        bars.push_back(form_head(species)->height + 1.0f + follow->lift);
        worst = std::max(worst, top[i] - bars.back());
    }
    return bars;
}

// The head's rise: up at once, held, eased back down; the run steady.
void rising_heads() {
    float worst = 0;
    static_assert(std::size(kGuilmonJumpJoint) == std::size(kGuilmonJumpTop) && std::size(kAgumonRunJoint) == std::size(kAgumonRunTop) &&
                  std::size(kAgumonLeapJoint) == std::size(kAgumonLeapTop));
    // A steady tag (round 54) had the jump's ears 2.0 over the bars, the
    // leap's head 8.3, the run's 1.0.
    const float steady_guilmon = form_head(2)->height + 1.0f, steady_agumon = form_head(0)->height + 1.0f;
    check(*std::max_element(std::begin(kGuilmonJumpTop), std::end(kGuilmonJumpTop)) - steady_guilmon > 1.9f &&
              *std::max_element(std::begin(kAgumonLeapTop), std::end(kAgumonLeapTop)) - steady_agumon > 8.0f &&
              *std::max_element(std::begin(kAgumonRunTop), std::end(kAgumonRunTop)) - steady_agumon > 0.9f,
          "a steady tag: the head through the bars");
    auto bars = follow_trace(2, kGuilmonJumpJoint, kGuilmonJumpTop, std::size(kGuilmonJumpJoint), 1, worst);
    check(worst <= 0.15f, "Guilmon's jump: the ears at most graze the bars, " + std::to_string(worst));
    check(near(bars.front(), steady_guilmon) && *std::max_element(bars.begin(), bars.end()) > steady_guilmon + 1.8f, "the jump lifts the tag");
    bars = follow_trace(0, kAgumonLeapJoint, kAgumonLeapTop, std::size(kAgumonLeapJoint), 1, worst);
    check(worst <= 0.0f, "Agumon's leap: the head below the bars, " + std::to_string(worst));
    bars = follow_trace(0, kAgumonRunJoint, kAgumonRunTop, std::size(kAgumonRunJoint), 2, worst);
    check(worst <= 0.0f, "Agumon's run: the head below the bars, " + std::to_string(worst));
    // From the first stride's top to the stop the tag keeps still.
    const auto [low, high] = std::minmax_element(bars.begin() + 10, bars.begin() + 63);
    check(*high - *low <= 0.11f, "the run's tag steady, " + std::to_string(*high - *low));
    // Held, then eased back down: half a second after a landing.
    HeadFollow follow = advance_head_follow(std::nullopt, Vec3f{}, 2.0f);
    for (int i = 0; i < 30; ++i) follow = advance_head_follow(follow, Vec3f{}, 0.0f);
    check(near(follow.lift, 2.0f, 0.001f), "held for 30 steps");
    follow = advance_head_follow(follow, Vec3f{}, 0.0f);
    check(near(follow.lift, 1.8f, 0.001f), "then a tenth of the way down per step");
    for (int i = 0; i < 30; ++i) follow = advance_head_follow(follow, Vec3f{}, 0.0f);
    check(follow.lift < 0.1f, "down after another half second");
    // A rise back to three quarters of the held one renews the hold (a
    // stride's slightly lower top), a smaller one does not.
    follow = advance_head_follow(std::nullopt, Vec3f{}, 2.0f);
    for (int i = 0; i < 25; ++i) follow = advance_head_follow(follow, Vec3f{}, 0.0f);
    follow = advance_head_follow(follow, Vec3f{}, 1.6f);
    check(follow.hold == 30 && near(follow.lift, 2.0f, 0.001f), "a stride's lower top renews the hold, the lift kept");
    for (int i = 0; i < 30; ++i) follow = advance_head_follow(follow, Vec3f{}, 1.4f);
    check(follow.hold == 0 && near(follow.lift, 2.0f, 0.001f), "a smaller rise lets the hold run out");
    // The horizontal follow: cut to 3.5 units, a twentieth of the way per
    // step, the height never followed.
    const auto start = advance_head_follow(std::nullopt, Vec3f{6.0f, 9.0f, 8.0f}, 0.0f);
    check(near(start.offset.x, 2.1f, 0.001f) && near(start.offset.z, 2.8f, 0.001f) && start.offset.y == 0.0f && start.lift == 0.0f,
          "a new label: the joint's offset, at most 3.5 long");
    const auto step = advance_head_follow(HeadFollow{Vec3f{0.0f, 3.0f, 0.0f}}, Vec3f{1.0f, 0.0f, 0.0f}, 0.0f);
    check(near(step.offset.x, 0.05f, 0.001f) && step.offset.y == 0.0f && near(step.offset.z, 0.0f), "a twentieth of the way");
    HeadFollow side{};
    for (int i = 0; i < 20; ++i) side = advance_head_follow(side, Vec3f{5.0f, 0.0f, 0.0f}, 0.0f);
    check(side.offset.x > 2.1f && side.offset.x < 2.4f, "some 20 steps for most of the way");
    for (int i = 0; i < 200; ++i) side = advance_head_follow(side, Vec3f{5.0f, 0.0f, 0.0f}, 0.0f);
    check(near(side.offset.x, 3.5f, 0.01f), "never past 3.5 units");
    // A catch-up update of several steps moves as far as single steps.
    HeadFollow one = advance_head_follow(std::nullopt, Vec3f{1.0f, 0.0f, 0.0f}, 2.0f), four = one;
    for (int round = 0; round < 12; ++round) {
        for (int i = 0; i < 4; ++i) one = advance_head_follow(one, Vec3f{-2.0f, 0.0f, 1.0f}, 0.2f);
        four = advance_head_follow(four, Vec3f{-2.0f, 0.0f, 1.0f}, 0.2f, 4);
        check(near(one.offset.x, four.offset.x, 1e-4f) && near(one.offset.z, four.offset.z, 1e-4f) && near(one.lift, four.lift, 1e-4f) &&
                  one.hold == four.hold,
              "four steps at once as four single steps");
    }
    check(four.lift < 1.0f, "and the lift eased in them");
}

// OverheadLabels::follow_head: per label key, forgotten without a joint.
void label_follow() {
    OverheadLabels labels;
    dw4::engine::Node node;
    node.matrix = dw4::engine::translation(10, 5, -3);
    const auto first = labels.follow_head(1, node, 0, Vec3f{12.0f, 5.0f + 6.0f, -3.0f});
    check(first && near(first->x, 2.0f) && near(first->y, 0.0f) && near(first->z, 0.0f), "a new label: the joint's offset, standing");
    const auto jump = labels.follow_head(1, node, 0, Vec3f{13.0f, 5.0f + 8.0f, -3.0f});
    check(jump && near(jump->x, 2.05f, 0.001f) && near(jump->y, 2.0f, 0.001f), "the head's rise at once, the offset smoothly");
    check(near(head_point(node, 0, std::nullopt, jump).y, 5 + form_head(0)->height + 1 + 2.0f), "the head point lifted");
    const auto other = labels.follow_head(2, node, 0, Vec3f{10.0f, 5.0f + 6.0f, -1.0f});
    check(other && near(other->z, 2.0f) && near(other->y, 0.0f), "another key follows on its own");
    check(!labels.follow_head(1, node, 0, std::nullopt), "no joint: none");
    check(near(head_point(node, 0, std::nullopt, std::nullopt).y, 5 + form_label_height(0) + 1), "and the tag at the form's height");
    const auto again = labels.follow_head(1, node, 0, Vec3f{7.0f, 5.0f + 6.0f, -3.0f});
    check(again && near(again->x, -3.0f) && near(again->y, 0.0f), "the joint back: started afresh, not eased from before");
    const auto kept = labels.follow_head(2, node, 0, Vec3f{10.0f, 5.0f + 6.0f, -1.0f}, 3);
    check(kept && near(kept->z, 2.0f), "the other key kept");
    labels.reset();
    const auto reset = labels.follow_head(1, node, 0, Vec3f{12.0f, 5.0f + 6.0f, -3.0f});
    check(reset && near(reset->x, 2.0f), "reset forgets the follows");
    // The form a tag measures: the drawn model first (a Digi-Lab
    // digivolution changes the player's form at once, the model only with
    // the next stage load), the stats' species, the player's model name.
    check(label_species("p_agumon", "p_wargrey", 6) == 0, "the drawn model's form first");
    check(label_species("P_WEREGARU", "", -1) == 4, "model names fold case");
    check(label_species("e_unknown", "p_wargrey", 3) == 3 && label_species("e_unknown", "p_wargrey", -1) == 6, "then the stats, then the name");
    check(label_species("e_unknown", "", -1) == -1, "none");
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
    // A hide counts only after kHideAfterFrames frames in a row: the review's
    // 1-3 frame hides (the player's own weapon, damage numbers, effects) and
    // even 8-frame ones leave the tag as it is; real cover then eases in.
    for (int i = 0; i < 3; ++i) value = labels.ease_occlusion(2, true);
    check(value == 0.0f, "a 3 frame hide does not dim the tag");
    value = labels.ease_occlusion(2, false);
    for (int i = 0; i < OverheadLabels::kHideAfterFrames - 1; ++i) value = labels.ease_occlusion(2, true);
    check(value == 0.0f, "nor one a frame short of the limit");
    value = labels.ease_occlusion(2, false);
    check(value == 0.0f, "seen again: counting starts over");
    for (int i = 0; i < OverheadLabels::kHideAfterFrames; ++i) value = labels.ease_occlusion(2, true);
    check(near(value, 0.125f, 0.001f), "a lasting hide starts to ease in");
    value = labels.ease_occlusion(2, true);
    check(near(value, 0.25f, 0.001f), "hiding eases in too");
    for (int i = 0; i < 20; ++i) value = labels.ease_occlusion(2, true);
    check(value == 1.0f, "fully hidden behind lasting cover");
    value = labels.ease_occlusion(2, false);
    check(near(value, 0.875f, 0.001f), "seen again: eases out at once");
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
        measured_forms();
        rising_heads();
        label_follow();
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
