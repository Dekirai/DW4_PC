#include "dw4/engine/math.hpp"
#include "dw4/engine/actor.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/engine/gekko_math.hpp"
#include "dw4/engine/gx_color.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/particles.hpp"
#include "dw4/engine/random.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
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

NodeAnim loading_anim() {
    // The values of loadingUSA/ldag0101.igb.
    NodeAnim anim;
    anim.start = frame;
    anim.loop_start = 100 * frame;
    anim.loop_end = 280 * frame;
    anim.loading = true;
    anim.lock = 284 * frame;
    anim.play_limit = 100 * frame;
    return anim;
}

// The clock runs on the original coarse time (2^18 ns steps), so values are
// compared within one step.
bool near(std::int64_t a, std::int64_t b) { return (a > b ? a - b : b - a) <= (std::int64_t(1) << 19); }

void test_start_and_advance() {
    auto anim = loading_anim();
    const std::int64_t now = 1000 * frame;
    check(near(anim.time(now), frame), "The first visit starts at the start time");
    std::int64_t t = 0;
    for (int i = 1; i <= 10; ++i) t = anim.time(now + i * frame);
    check(near(t, 11 * frame), "The clock follows real time");
    check(anim.played == 10 * frame, "The play time counts real time");
    check(!anim.finished(), "The minimum play time is not reached yet");
}

void test_long_step() {
    auto anim = loading_anim();
    const std::int64_t now = 1000 * frame;
    anim.time(now);
    // A stall of 30 frames advances the clock by two frames only.
    check(near(anim.time(now + 30 * frame), 3 * frame), "A long step advances two frames");
    check(anim.played == 30 * frame, "The play time still counts the whole step");
}

void test_loop_and_lock() {
    auto anim = loading_anim();
    std::int64_t now = 1000 * frame;
    anim.time(now);
    std::int64_t t = 0;
    for (int i = 0; i < 300; ++i) t = anim.time(now += frame);
    // 1 + 300 frames passes the loop end (280) and wraps back to 100 + 21.
    check(near(t, 121 * frame), "The clock loops back to the loop start");
    check(anim.finished(), "The minimum play time is reached");
    anim.fixed = anim.lock;
    check(anim.time(now += frame) == 284 * frame, "A fixed time holds the clock");
}

// igTransformSequence1_5 evaluation (80209B74 / 80209578 / 80208084).
bool close(float a, float b) { return std::abs(a - b) < 1e-3f; }

TransformSequence line_sequence(std::int64_t duration) {
    TransformSequence seq;
    seq.translations = {{0, 0, 0}, {1, 0, 0}};
    seq.times = {0, 1'000'000'000};
    seq.driven_channels = TransformSequence::translation;
    seq.duration = duration;
    seq.valid = true;
    return seq;
}

void test_sequence_rotation_layout() {
    // 8012BBF4: 90 degrees about Z turns +X to -Y (the transpose of
    // quaternion_matrix in math.cpp).
    TransformSequence seq;
    const float h = std::sqrt(0.5f);
    seq.rotations = {{0, 0, h, h}, {0, 0, h, h}};
    seq.times = {0, 1'000'000'000};
    seq.driven_channels = TransformSequence::quaternion;
    seq.duration = 1'000'000'000;
    seq.valid = true;
    const Mat4 m = seq.evaluate(500'000'000);
    check(close(m(0, 1), -1) && close(m(1, 0), 1), "Quaternion keys build the 8012BBF4 matrix");
    // Euler degrees through 801292D4: +90 about Z turns +X to +Y.
    seq.driven_channels = TransformSequence::euler;
    seq.rotations = {{0, 0, 90, 0}, {0, 0, 90, 0}};
    const Mat4 e = seq.evaluate(500'000'000);
    check(close(e(0, 1), 1) && close(e(1, 0), -1) && close(e(2, 2), 1), "Euler keys are degrees, not a quaternion");
    // A rotation turns about the centre: translation = centre - centre * R.
    seq.rotations = {{0, 0, 180, 0}, {0, 0, 180, 0}};
    seq.center = {1, 0, 0};
    const Mat4 c = seq.evaluate(0);
    check(close(c(3, 0), 2) && close(c(3, 1), 0), "The centre correction uses the rotation");
    // Scale applies before the rotation and does not scale the centre term.
    seq.driven_channels = TransformSequence::euler | TransformSequence::scale;
    seq.rotations = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    seq.scales = {{2, 2, 2}, {2, 2, 2}};
    const Mat4 s = seq.evaluate(0);
    check(close(s(0, 0), 2) && close(s(3, 0), 0), "Scale rows, unscaled centre term");
    // Undriven channels are ignored.
    seq.driven_channels = TransformSequence::translation;
    seq.translations = {{0, 0, 0}, {0, 0, 0}};
    const Mat4 t = seq.evaluate(0);
    check(close(t(0, 0), 1) && close(t(0, 1), 0) && close(t(3, 0), 0), "Only driven channels apply");
}

void test_sequence_play_modes() {
    auto seq = line_sequence(1'000'000'000);
    check(close(seq.evaluate(500'000'000)(3, 0), 0.5f), "Linear translation between keys");
    check(close(seq.evaluate(1'000'000'000)(3, 0), 1), "The end of the duration shows the last key");
    check(close(seq.evaluate(1'250'000'000)(3, 0), 0.25f), "REPEAT wraps only beyond the end");
    check(close(seq.evaluate(-5)(3, 0), 0), "Before the first key: key 0");
    seq.play_mode = TransformSequence::clamp;
    check(close(seq.evaluate(5'250'000'000)(3, 0), 1), "CLAMP holds the last key");
    seq.play_mode = TransformSequence::bounce;
    check(close(seq.evaluate(1'250'000'000)(3, 0), 0.75f), "BOUNCE runs back on odd passes");
    check(close(seq.evaluate(2'250'000'000)(3, 0), 0.25f), "BOUNCE runs forward on even passes");
    auto loop = line_sequence(2'000'000'000);
    loop.play_mode = TransformSequence::loop;
    check(close(loop.evaluate(1'500'000'000)(3, 0), 0.5f), "LOOP blends the last key into key 0");
    auto hold = line_sequence(2'000'000'000);
    check(close(hold.evaluate(1'500'000'000)(3, 0), 1), "Other modes hold the last key until the end");
    // The HUD meter (hpmetr): duration = last key, evaluated at the end.
    auto meter = line_sequence(1'000'000'000);
    meter.duration = 1'000'000'000;
    check(close(meter.evaluate(meter.duration)(3, 0), 1), "Meter time at the duration is the full key");
}

void test_sequence_interpolation_methods() {
    auto seq = line_sequence(1'000'000'000);
    seq.translations = {{0, 0, 0}, {3, 0, 0}};
    seq.translation_tangents = {{0, 3, 0}, {3, 3, 0}, {0, 0, 0}, {0, 0, 0}};
    seq.methods[0] = TransformSequence::bezier;
    const Mat4 b = seq.evaluate(500'000'000);
    check(close(b(3, 0), 1.5f) && close(b(3, 1), 2.25f), "BEZIER uses the control points 2i and 2i+1");
    seq.methods[0] = TransformSequence::none;
    check(close(seq.evaluate(900'000'000)(3, 0), 0), "NONE holds key i");
    // Quaternion LINEAR is the normalized blend 8012C15C, not a slerp.
    TransformSequence q;
    q.rotations = {{0, 0, 0, 1}, {0, 0, 1, 0}};
    q.times = {0, 1'000'000'000};
    q.driven_channels = TransformSequence::quaternion;
    q.duration = 1'000'000'000;
    q.valid = true;
    const Mat4 n = q.evaluate(250'000'000); // nlerp angle 2*atan(0.25/0.75)
    const float angle = 2 * std::atan(0.25f / 0.75f);
    check(close(n(0, 0), std::cos(angle)), "Quaternion LINEAR blends linearly and normalizes");
}

// TransformSequence::phase (presentation only): it changes exactly where
// evaluate() jumps, so smooth motion does not blend across the jump.
void test_sequence_phase() {
    constexpr std::int64_t second = 1'000'000'000;
    auto seq = line_sequence(second); // REPEAT, 0 -> 1 in one second
    check(seq.phase(100'000'000) == seq.phase(900'000'000), "A linear pass keeps its phase");
    check(seq.phase(second) == seq.phase(900'000'000), "The end itself still belongs to the pass");
    check(seq.phase(1'100'000'000) != seq.phase(900'000'000), "REPEAT passing the end changes the phase");
    check(seq.phase(1'100'000'000) == seq.phase(1'900'000'000), "The next pass keeps its own phase");
    check(seq.phase(2'100'000'000) != seq.phase(1'900'000'000), "Every wrap changes the phase");
    seq.play_mode = TransformSequence::bounce;
    check(seq.phase(1'100'000'000) == seq.phase(900'000'000), "BOUNCE runs back continuously");
    seq.play_mode = TransformSequence::clamp;
    check(seq.phase(5 * second) == seq.phase(6 * second), "CLAMP holds one key");
    check(seq.phase(1'100'000'000) == seq.phase(900'000'000), "CLAMP's end continues the pass (the last key held)");
    auto late = line_sequence(second);
    late.time_offset = 500'000'000;
    check(late.phase(100'000'000) == late.phase(700'000'000), "Before the first key key 0 is held: no jump at the start");
    auto loop = line_sequence(2 * second);
    loop.play_mode = TransformSequence::loop;
    check(loop.phase(1'500'000'000) == loop.phase(500'000'000), "LOOP's blend into key 0 is continuous");
    check(loop.phase(2'100'000'000) == loop.phase(1'900'000'000), "LOOP passing the end ends in key 0: no phase change");
    check(std::abs(loop.evaluate(2 * second)(3, 0) - loop.evaluate(0)(3, 0)) < 1e-6f, "(LOOP's end evaluates key 0)");
    auto tight = line_sequence(second); // LOOP without time after the last key: the end is the last key
    tight.play_mode = TransformSequence::loop;
    check(!tight.wrap_continuous() && tight.phase(1'100'000'000) != tight.phase(900'000'000),
          "A LOOP that ends at its last key jumps to key 0");
    // REPEAT whose last key places as its first one (a full turn, a path
    // back to its start) wraps continuously.
    auto round = line_sequence(2 * second);
    round.translations = {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}};
    round.times = {0, second, 2 * second};
    check(round.wrap_continuous(), "Equal end keys: a continuous wrap");
    check(round.phase(2'100'000'000) == round.phase(1'900'000'000), "REPEAT back to its start keeps the phase");
    check(!line_sequence(second).wrap_continuous(), "A REPEAT line's ends differ: its wrap jumps");
    // Uniform LOOP keys blend by the key spacing, not up to the end: only
    // equal end keys make that wrap continuous.
    auto uniform = loop;
    uniform.uniform = true;
    uniform.times = {second, second};
    check(!uniform.wrap_continuous(), "A uniform LOOP with different end keys jumps at the wrap");
    // A stepped channel jumps at every key.
    auto stepped = line_sequence(2 * second);
    stepped.translations = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    stepped.times = {0, second, 2 * second};
    stepped.methods[0] = TransformSequence::none;
    check(stepped.phase(100'000'000) == stepped.phase(900'000'000), "A held key keeps its phase");
    check(stepped.phase(1'100'000'000) != stepped.phase(900'000'000), "A stepped key change changes the phase");
    check(std::abs(stepped.evaluate(1'100'000'000)(3, 0) - stepped.evaluate(900'000'000)(3, 0)) > 0.5f,
          "(the stepped channel jumps there)");
    // The ring walls of stagML11 (QS, Y rotation 0 -> 11.25 degrees in 240
    // frames, REPEAT): continuous for 240 frames, then a phase change.
    TransformSequence ring;
    ring.compressed = true;
    const float s = std::sin(11.25f * 3.14159265f / 360.0f), c = std::cos(11.25f * 3.14159265f / 360.0f);
    ring.rotations = {{0, 0, 0, 1}, {0, s, 0, c}};
    ring.times = {0, 240 * frame};
    ring.driven_channels = TransformSequence::quaternion;
    ring.duration = 240 * frame;
    ring.valid = true;
    const std::int64_t start = 7 * frame;
    int changes = 0;
    for (int f = 1; f <= 720; ++f)
        if (ring.phase(start + f * frame) != ring.phase(start + (f - 1) * frame)) ++changes;
    check(changes == 3, "The ring's sequence changes its phase once every 240 frames");
}

void test_compressed_sequence() {
    TransformSequence seq;
    seq.compressed = true;
    const float h = std::sqrt(0.5f);
    seq.rotations = {{0, 0, h, h}, {0, 0, h, h}};
    seq.translations = {{0, 0, 0}, {2, 0, 0}};
    seq.times = {0, 1'000'000'000};
    seq.driven_channels = TransformSequence::quaternion | TransformSequence::translation;
    seq.duration = 2'000'000'000;
    seq.play_mode = TransformSequence::loop;
    seq.time_offset = 123; // QS has no time offset
    seq.valid = true;
    const Mat4 m = seq.evaluate(500'000'000);
    check(close(m(0, 1), -1) && close(m(3, 0), 1), "QS keys use 8012BBF4 and set the translation");
    check(close(seq.evaluate(1'500'000'000)(3, 0), 1), "QS LOOP blends the last key into key 0");
}

void test_horizontal_perspective() {
    // A stage camera's _horizontalFieldOfView (54.4322, dng101b1 cameraShape00)
    // on the 640x448 viewer window: the width keeps tan(fov / 2) (801DB6E4).
    const float fov = 54.4322f * 3.14159265f / 180.0f;
    const float half = std::tan(fov * 0.5f);
    const Mat4 p = perspective_horizontal(fov, 640.0f / 448.0f, 10.0f, 2000.0f);
    check(std::fabs(p(0, 0) - 1.0f / half) < 1e-4f, "The horizontal field of view spans the width");
    check(std::fabs(p(1, 1) - (640.0f / 448.0f) / half) < 1e-4f, "The height follows from the 640x448 aspect");
    // Against the field of view taken as a vertical one at 4:3, the picture
    // is 4/3 wider and 10/7 taller, as measured against Dolphin.
    const Mat4 old = perspective(fov, 4.0f / 3.0f, 10.0f, 2000.0f);
    check(std::fabs(p(0, 0) / old(0, 0) - 4.0f / 3.0f) < 1e-4f, "4/3 horizontal zoom");
    check(std::fabs(p(1, 1) / old(1, 1) - 10.0f / 7.0f) < 1e-4f, "10/7 vertical zoom");
    check(p(2, 2) == old(2, 2) && p(3, 2) == old(3, 2), "The depth range is unchanged");
}

bool row_is(const Mat4& m, int row, float x, float y, float z) {
    return close(m(row, 0), x) && close(m(row, 1), y) && close(m(row, 2), z);
}

void test_billboard_matrix() {
    // A camera at (0, 50, 100) looking at the origin: 26.6 degrees down.
    const Mat4 view = look_at({0, 50, 100}, {0, 0, 0}, {0, 1, 0});
    Mat4 inverse_view;
    check(invert(view, inverse_view), "The test view inverts");
    // i_pcdmgnum: dmgnmtrans scales its digits (1, 2, 1) for 10 frames, under
    // a mode 3 igBillboard. 801D53EC keeps no scale of the parent chain.
    Mat4 world = scaling(1, 2, 1);
    const Mat4 m3 = billboard_matrix(3, {0, 0, 1}, {}, world, inverse_view);
    check(row_is(m3, 0, 1, 0, 0), "Mode 3 at the screen centre: X is the camera's X");
    check(row_is(m3, 1, inverse_view(1, 0), inverse_view(1, 1), inverse_view(1, 2)),
          "Mode 3 at the screen centre: Y is the camera's Y, unscaled");
    check(row_is(m3, 2, 0, 0.4472136f, 0.8944272f), "Mode 3: Z points at the eye");
    check(m3(3, 0) == 0 && m3(3, 1) == 0 && m3(3, 2) == 0 && m3(3, 3) == 1, "The position stays");
    // Off the screen centre mode 3 faces the eye, not the picture plane.
    world = translation(60, 0, 0);
    const Mat4 off = billboard_matrix(3, {0, 0, 1}, {}, world, inverse_view);
    const float d = std::sqrt(60.0f * 60.0f + 50.0f * 50.0f + 100.0f * 100.0f);
    check(row_is(off, 2, -60.0f / d, 50.0f / d, 100.0f / d), "Mode 3 off centre: Z = normalized(eye - position)");
    check(close(off(1, 0) * off(2, 0) + off(1, 1) * off(2, 1) + off(1, 2) * off(2, 2), 0), "Y is orthogonal to Z");
    check(close(off(3, 0), 60), "The world translation is kept");
    // Mode 0: an upright billboard turning about world Y only.
    const Mat4 m0 = billboard_matrix(0, {0, 0, 1}, {}, scaling(3, 3, 3), inverse_view);
    check(row_is(m0, 1, 0, 1, 0) && row_is(m0, 2, 0, 0, 1) && row_is(m0, 0, 1, 0, 0),
          "Mode 0 stays upright and drops the scale");
    check(row_is(billboard_matrix(7, {0, 0, 1}, {}, Mat4{}, inverse_view), 1, 0, 1, 0), "Other modes act like mode 0");
    // Mode 1: _axis through the parent's rotation is the up vector.
    const Mat4 turned = quaternion_matrix(0.7071068f, 0, 0, 0.7071068f); // 90 degrees about X
    const Mat4 m1 = billboard_matrix(1, {0, 0, 1}, {}, turned, inverse_view);
    const Vec3f axis{turned(2, 0), turned(2, 1), turned(2, 2)};
    check(row_is(m1, 1, axis.x, axis.y, axis.z), "Mode 1: Y is the rotated axis");
    // Mode 2: world Y made orthogonal to the eye direction; Z is the eye direction.
    const Mat4 m2 = billboard_matrix(2, {0, 0, 1}, {}, translation(60, 0, 0), inverse_view);
    check(row_is(m2, 2, -60.0f / d, 50.0f / d, 100.0f / d), "Mode 2: Z = normalized(eye - position)");
    check(m2(1, 1) > 0.8f, "Mode 2: Y leans towards world Y");
    // The centre goes through the world rotation into the position.
    const Mat4 centred = billboard_matrix(3, {0, 0, 1}, {0, 0, 5}, turned * translation(1, 2, 3), inverse_view);
    check(close(centred(3, 0), 1 + 5 * axis.x) && close(centred(3, 1), 2 + 5 * axis.y) && close(centred(3, 2), 3 + 5 * axis.z),
          "The centre through the world rotation is added to the position");
    // Up parallel to the eye direction: the world X fallback of 801D53EC.
    const Mat4 above = look_at({0, 100, 0}, {0, 0, 0}, {0, 0, -1});
    Mat4 inverse_above;
    check(invert(above, inverse_above), "The top view inverts");
    const Mat4 top = billboard_matrix(0, {0, 0, 1}, {}, Mat4{}, inverse_above);
    check(row_is(top, 0, 1, 0, 0) && row_is(top, 1, 0, 1, 0) && row_is(top, 2, 0, 0, 1),
          "Up along the eye direction falls back to world X");
}
} // namespace

// The system generator: genrand 8021B338 after sgenrand 8021B2A0 with the
// boot seed 5150 (values of an independent model of the two routines).
void test_system_random() {
    SystemRandom random;
    check(random() == 0xCDB83DD3u, "First draw after the boot seed");
    check(random() == 0xD10604D7u, "Second draw");
    for (int i = 2; i < 623; ++i) random();
    check(random() == 0x1BCA2800u, "Last word of the first state");
    check(random() == 0xAFB44022u, "First word after the refill");
    std::stringstream saved;
    saved << random;
    SystemRandom copy;
    saved >> copy;
    check(copy == random, "A save state keeps the generator");
    for (int i = 625; i < 999; ++i) random();
    check(random() == 0xD4A34A9Cu, "Draw 1000");
}

// World time: one NTSC field per frame, 50,050,000 ns every three.
void test_field_time() {
    check(field_time(0) == 0 && field_time(1) == 16683333 && field_time(2) == 33366666, "Field times");
    check(field_time(3) == 50050000 && field_time(3000) == 50050000000, "Three fields");
}

// MOTIONSET cross-fade (801D0C14, several states): the quaternions blend
// through 8012BE90 and the translations linearly; the local matrices are
// never mixed element by element, which would shrink a turning bone.
void test_motion_blend_tracks() {
    const float h = std::sqrt(0.5f);
    BoneTrack rest;                       // identity, at the origin
    BoneTrack turned;
    turned.rotation = {0, h, 0, h};       // 90 degrees about Y
    turned.translation = {4, 0, -2};
    const BoneTrack half = blend_bone_track(rest, turned, 0.5f);
    const Mat4 m = bone_track_matrix(half);
    for (int row = 0; row < 3; ++row) {
        const float length = std::sqrt(m(row, 0) * m(row, 0) + m(row, 1) * m(row, 1) + m(row, 2) * m(row, 2));
        check(std::abs(length - 1.0f) < 0.01f, "A half-way blend keeps the bone's axes unit length");
    }
    // An element-wise lerp of the two matrices would give cos = 0.5 here.
    check(std::abs(m(0, 0) - std::cos(0.785398163f)) < 0.01f && std::abs(m(1, 1) - 1.0f) < 1e-6f,
          "Half of a 90 degree turn is 45 degrees");
    check(m(3, 0) == 2.0f && m(3, 1) == 0.0f && m(3, 2) == -1.0f, "The translation mixes linearly");

    // The shorter way: the negated quaternion is the same turn.
    BoneTrack negated = turned;
    for (auto& c : negated.rotation) c = -c;
    const BoneTrack other = blend_bone_track(rest, negated, 0.5f);
    for (std::size_t i = 0; i < 4; ++i)
        check(std::abs(std::abs(other.rotation[i]) - std::abs(half.rotation[i])) < 1e-6f, "8012BE90 takes the shorter way");

    // Close rotations (1 - dot <= 0.08) take the unnormalized linear branch;
    // the translation is acc * (1 - w) + w * t, fused.
    BoneTrack near_turn;
    near_turn.rotation = {0, 0.2f, 0, std::sqrt(1.0f - 0.04f)};
    near_turn.translation = {3, 2, -1};
    BoneTrack start;
    start.translation = {1, 2, 3};
    const BoneTrack q = blend_bone_track(start, near_turn, 0.25f);
    check(q.rotation[0] == 0 && q.rotation[1] == 0.25f * 0.2f && q.rotation[2] == 0 &&
              q.rotation[3] == std::fma(0.75f, 1.0f, 0.25f * near_turn.rotation[3]),
          "Close rotations blend linearly without normalizing");
    check(q.translation.x == 1.5f && q.translation.y == 2.0f && q.translation.z == 2.0f, "Translation blend at a quarter");

    // A fixed snapshot rebuilds its track from the cached local (8012B8A8).
    const BoneTrack back = matrix_bone_track(bone_track_matrix(turned));
    for (std::size_t i = 0; i < 4; ++i)
        check(std::abs(back.rotation[i] - turned.rotation[i]) < 1e-6f, "The snapshot keeps the rotation");
    check(back.translation.x == 4.0f && back.translation.y == 0.0f && back.translation.z == -2.0f, "The snapshot keeps the translation");
    const BoneTrack identity = matrix_bone_track(Mat4{});
    check(identity.rotation == std::array<float, 4>{0, 0, 0, 1}, "An identity local is the identity quaternion");
}

// igLod selection (801F799C): squared distances against the ranges, the
// cached band and the hysteresis widening.
void test_lod_selection() {
    Node kids[3];
    Node lod;
    lod.kind = NodeKind::lod;
    lod.children = {&kids[0], &kids[1], &kids[2]};
    lod.lod_ranges = {0.0f, 0.0f, 10000.0f}; // the players' Kage / High / Low
    check(lod.select_lod(50.0f * 50.0f) == &kids[1], "High within 100 units (squared range 10000)");
    check(lod.select_lod(99.0f * 99.0f) == &kids[1], "High just inside 100 units");
    check(lod.select_lod(150.0f * 150.0f) == &kids[2], "Low beyond 100 units, not beyond 10000");
    check(lod.lod_upper == std::numeric_limits<float>::max(), "The last band ends at FLT_MAX");
    check(lod.select_lod(1.0f) == &kids[1], "Back down to High; Kage's empty band [0, 0) never shows");
    check(lod.select_lod(0.0f) == &kids[1], "Zero distance is High as well");
    lod.lod_ranges[2] = 1.0e8f; // SETLODHIGH
    lod.reset_lod();
    check(lod.lod_index == -1 && lod.lod_child == -1 && lod.lod_lower == 0.0f && lod.lod_upper == 0.0f, "801F7CB8 reset");
    check(lod.select_lod(5000.0f * 5000.0f) == &kids[1], "SETLODHIGH: High up to 10000 units");
    check(lod.select_lod(20000.0f * 20000.0f) == &kids[2], "Low beyond 10000 units");

    Node two[2];
    Node h;
    h.kind = NodeKind::lod;
    h.children = {&two[0], &two[1]};
    h.lod_ranges = {0.0f, 100.0f};
    h.lod_hysteresis = {0.0f, 2.0f};
    check(h.select_lod(50.0f) == &two[0] && h.lod_upper == 144.0f, "Upper bound (sqrt(100) + 2)^2");
    check(h.select_lod(120.0f) == &two[0], "Inside the widened band the child stays");
    check(h.select_lod(150.0f) == &two[1] && h.lod_lower == 64.0f, "Lower bound (sqrt(100) - 2)^2");
    check(h.select_lod(80.0f) == &two[1], "Above the lowered bound the far child stays");
    check(h.select_lod(60.0f) == &two[0], "Below it the search runs down");

    Node one;
    one.kind = NodeKind::lod;
    one.children = {&two[0]};
    one.lod_ranges = {10.0f, 20.0f};
    check(one.select_lod(5.0f) == &two[0], "The first search assumes range[0] is reached (from _lastIndex + 2)");
    check(one.select_lod(25.0f) == nullptr, "An index past the children selects nothing");
    check(one.select_lod(5.0f) == nullptr && one.lod_index == -1 && one.lod_upper == 10.0f, "Below range[0] going down: no child");
}

// DW4_CARD_A / DW4_CARD_B and settings.ini [data] card_a / card_b.
void test_card_setting() {
    using Mode = dw4::engine::CardSetting::Mode;
    const auto none = dw4::engine::parse_card_setting("none");
    check(none && none->mode == Mode::none && none->blocks == 2043, "An empty slot");
    const auto small = dw4::engine::parse_card_setting(" Unformatted , 251 ");
    check(small && small->mode == Mode::unformatted && small->blocks == 251, "An unformatted 251-block card");
    const auto sized = dw4::engine::parse_card_setting("59");
    check(sized && sized->mode == Mode::automatic && sized->blocks == 59, "A block count alone keeps the slot's default");
    check(!dw4::engine::parse_card_setting("full") && !dw4::engine::parse_card_setting("4096"), "Unknown settings are refused");
}

// GAMERAM GATESTAGENAME: a null string object until set (SYSgetGateStageName
// nil), kept apart from the empty string in save states.
void test_gate_stage_name() {
    using dw4::engine::GameSystem;
    auto round_trip = [](const GameSystem& system) {
        dw4::engine::StageSnapshot snapshot;
        snapshot.stage = "stagml11";
        snapshot.system = std::make_shared<GameSystem>(system);
        const auto bytes = dw4::engine::encode_snapshot(snapshot);
        const auto back = dw4::engine::decode_snapshot(bytes);
        return *static_cast<const GameSystem*>(back.system.get());
    };
    GameSystem system;
    check(!system.gate_stage_name && system.gate_stage_no == 0, "The gate stage is unset after boot");
    check(!round_trip(system).gate_stage_name, "An unset gate stage stays unset in a save state");
    system.gate_stage_name = std::string();
    check(round_trip(system).gate_stage_name == std::string(), "The empty name (SYSsetGateStage(nil)) is not unset");
    system.gate_stage_name = "GateOff";
    system.gate_stage_no = -1;
    const auto copy = round_trip(system);
    check(copy.gate_stage_name == std::string("GateOff") && copy.gate_stage_no == -1, "A set gate stage survives a save state");
    system.reset_gate_stage();
    check(!system.gate_stage_name && system.gate_stage_no == 0, "GAMEDATAINIT of slot 0 unsets the gate stage");
}

// The PC fast loading option changes the frames of a load, so a save state
// keeps it with the other gameplay options; off is the original game.
void test_fast_loading_snapshot() {
    dw4::engine::Cheats cheats;
    check(!cheats.fast_loading && cheats.original(), "Fast loading is off by default (the original timing)");
    cheats.fast_loading = true;
    check(!cheats.original(), "Fast loading is not the original game");
    for (const bool fast : {false, true}) {
        dw4::engine::StageSnapshot snapshot;
        snapshot.stage = "dng101b1";
        snapshot.cheats.fast_loading = fast;
        snapshot.cheats.exp_rate = 150;
        const auto back = dw4::engine::decode_snapshot(dw4::engine::encode_snapshot(snapshot));
        check(back.cheats.fast_loading == fast && back.cheats.exp_rate == 150 && back.stage == "dng101b1",
              "A save state keeps the load timing it was made with");
    }
    // A save state of the version before the option is refused. (Since round
    // I1b's integration version 44 still reads 43, which lacks only the random
    // WAITCHECK minimum; 42 and older are refused.)
    dw4::engine::StageSnapshot snapshot;
    auto bytes = dw4::engine::encode_snapshot(snapshot);
    check(bytes.size() >= 4, "A snapshot starts with its version");
    std::uint32_t version = 0;
    std::memcpy(&version, bytes.data(), sizeof version);
    check(version == dw4::engine::snapshot_version(), "A snapshot starts with its version number");
    version -= 2;
    std::memcpy(bytes.data(), &version, sizeof version);
    bool refused = false;
    try {
        (void)dw4::engine::decode_snapshot(bytes);
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "A save state of an older version is refused");
}

void test_text_layout() {
    // NORMAL metrics of bectext.igb: digits advance past their right edge,
    // '!' has an x offset.
    FontImage font;
    font.line_height = 0.28f;
    font.glyphs[u'1'] = Glyph{0.16f, 0.0f, 0.0f, 0.15f, 0.27f};
    font.glyphs[u'!'] = Glyph{0.07f, 0.02f, 0.0f, 0.05f, 0.27f};
    // Quad k holds positions 4k..4k+3: (x0,y1) (x1,y1) (x1,y0) (x0,y0).
    auto right = layout_text(font, u"11", 1, 1.0f);
    check(right.positions.size() == 8 && close(right.positions[5].x, 1.0f),
          "RIGHT ends at the last glyph's right edge (802FBF98 v5.x), not the pen");
    auto center = layout_text(font, u"11", 2, 1.0f);
    check(close(center.positions[0].x, 1.0f - center.positions[5].x), "CENTER leaves equal gaps around the ink");
    auto two = layout_text(font, u"11\n1", 1, 1.0f);
    check(close(two.positions[5].x, 0.99f), "A broken line measures to the next quad's v0.x (the pen)");
    check(close(two.positions[8].x, 0.85f) && close(two.positions[9].x, 1.0f) && close(two.positions[11].y, -0.28f),
          "The last line measures to its right edge, one font height lower");
    auto lead = layout_text(font, u"\n\r\n1", 0, 1.0f);
    check(lead.positions.size() == 4 && close(lead.positions[3].y, 0.0f) && close(lead.positions[0].x, 0.0f),
          "Line breaks before the first character are dropped (0x802FC008)");
    auto left = layout_text(font, u"1\n!1", 0, 1.0f);
    check(close(left.positions[4].x, 0.0f) && close(left.positions[5].x, 0.05f) && close(left.positions[8].x, 0.05f),
          "A later line moves its first quad's v0.x to 0, x offset included");
    auto trailing = layout_text(font, u"11\n", 1, 1.0f);
    check(trailing.positions.size() == 8 && close(trailing.positions[5].x, 0.99f),
          "A line before a final break measures to the pen");
}

void test_frustum_cull_code() {
    // 90 degrees across the 640x448 window: at depth d the frustum spans
    // x in (-d, d) and y in (-d * 448 / 640, d * 448 / 640).
    const Mat4 projection = perspective_horizontal(3.14159265f / 2.0f, 640.0f / 448.0f, 10.0f, 2000.0f);
    const Mat4 view{};
    check(frustum_cull_code(view, projection, {-10, -10, -110}, {10, 10, -90}) == 1, "A box in the frustum is inside (1)");
    check(frustum_cull_code(view, projection, {150, -10, -110}, {170, 10, -90}) == 2, "No corner inside x < w: outside (2)");
    check(frustum_cull_code(view, projection, {-10, -10, 50}, {10, 10, 60}) == 2, "Every corner behind the eye: outside (2)");
    check(frustum_cull_code(view, projection, {90, -10, -110}, {130, 10, -90}) == 3, "Across the right plane: partial (3)");
    check(frustum_cull_code(view, projection, {95, -1, -101}, {105, 1, -99}) == 5,
          "Over the edge but inside the 1.1 times wider frustum (5)");
    check(frustum_cull_code(view, projection, {-10, -10, -20}, {10, 10, 5}) == 3, "Across the near plane: partial (3)");
    // The bound moves with the modelview (row vectors: translation in row 3).
    check(frustum_cull_code(translation(160, 0, 0), projection, {-10, -10, -110}, {10, 10, -90}) == 2,
          "The modelview places the box before the test");
    // The 2D layers' orthographic projection: z in the near/far range too.
    Mat4 ortho;
    ortho(0, 0) = 2.0f / 640.0f;
    ortho(1, 1) = 2.0f / 480.0f;
    ortho(2, 2) = -1.0f;
    check(frustum_cull_code(view, ortho, {-10, -10, 0}, {10, 10, 0}) == 1, "A 2D quad on the screen is inside");
    check(frustum_cull_code(view, ortho, {330, -10, 0}, {350, 10, 0}) == 2, "A 2D quad beyond the right edge is outside");
}

void test_particle_timer_state() {
    ParticleTimerState timer;
    timer.running = true;
    timer.time = -1; // a start (0x80312334)
    const std::int64_t t0 = 1000 * frame;
    check(timer.visit(t0) == 0, "The first visit after a start is the effect's time 0");
    check(timer.visit(t0 + 10) == 10, "A running timer follows the traversal time");
    check(timer.peek(t0 + 20) == 20 && timer.time == t0 + 10, "peek changes nothing");
    check(timer.visit(t0 + 50) == 50, "Frames without a visit still count while running");
    timer.running = false; // ONFREEZE
    check(timer.visit(t0 + 60) == 50, "A stopped timer keeps the time of its last visit");
    check(timer.visit(t0 + 70) == 50, "and keeps it on every visit");
    timer.running = true; // OFFREEZE
    check(timer.visit(t0 + 80) == 60, "Running again: the step since the last visit counts");
    ParticleTimerState late;
    late.running = true;
    late.bias = t0; // its last visit
    late.time = -1; // a start out of view (0x80312334 leaves _timeBias)
    check(late.bias == t0, "A start keeps the last visit's _timeBias");
    // 80312268 state 1 tests now - _timeBias against the lifetime, so until
    // the first visit such an effect looks as old as that visit and is
    // usually stopped before it is drawn.
    check(late.visit(t0 + 500) == 0 && late.bias == t0 + 500, "Its effect time and the lifetime test count from its first visit");
    ParticleTimerState frozen;
    frozen.running = true;
    frozen.time = -1;
    frozen.visit(t0);
    frozen.running = false; // ONFREEZE, then culled: no visit moves the bias
    check(frozen.bias == t0, "A frozen timer out of view keeps its bias (the effect ages on)");
    check(frozen.visit(t0 + 300) == 0 && frozen.bias == t0 + 300, "A visit while stopped moves the bias along");
}

void test_frustum_cull_traversal() {
    // root -> seen (bound in view) -> recorder, geometry; root -> aside
    // (bound right of the 4:3 frustum, inside a 16:9 picture) -> recorder,
    // ParticleTimer, geometry.
    Node root, seen, seen_recorder, aside, aside_recorder, aside_timer, aside_geometry, seen_geometry;
    root.kind = NodeKind::group;
    seen.kind = aside.kind = NodeKind::group;
    seen_recorder.kind = aside_recorder.kind = NodeKind::transform_recorder;
    aside_timer.kind = NodeKind::particle_timer;
    aside_geometry.kind = seen_geometry.kind = NodeKind::geometry;
    auto quad = std::make_shared<GeometryData>();
    quad->valid = true;
    aside_geometry.runtime_geometry = seen_geometry.runtime_geometry = quad;
    seen.has_bound = aside.has_bound = true;
    seen.bound_min = {-10, -10, -110};
    seen.bound_max = {10, 10, -90};
    aside.bound_min = {110, -10, -110};
    aside.bound_max = {120, 10, -90};
    root.children = {&seen, &aside};
    seen.children = {&seen_recorder, &seen_geometry};
    aside.children = {&aside_recorder, &aside_timer, &aside_geometry};
    aside_timer.timer.running = true;
    aside_timer.timer.time = -1;

    TraversalContext context;
    context.cull = true;
    context.time = 5000 * frame;
    context.cull_camera.projection = perspective_horizontal(3.14159265f / 2.0f, 640.0f / 448.0f, 10.0f, 2000.0f);
    context.camera = context.cull_camera;
    traverse(&root, context);
    check(seen_recorder.recorded.has_value(), "A subtree in view records its matrices");
    check(!aside_recorder.recorded.has_value(), "A culled subtree's recorder keeps its matrix (801E994C code 2)");
    check(aside_timer.timer.time == -1, "A culled ParticleTimer's clock does not advance");

    std::vector<DrawItem> items;
    context.output = &items;
    traverse(&root, context);
    check(items.size() == 1 && items[0].node == &seen_geometry, "The picture draws only the subtree in view");

    // A 16:9 picture (the renderer's x scale 0.75) reaches the culled box:
    // drawn, but its recorder and clock stay as the original leaves them.
    items.clear();
    context.picture_widen = 0.75f;
    traverse(&root, context);
    check(items.size() == 2, "The widened picture draws the subtree the original culls (not its unstarted timer)");
    check(!aside_recorder.recorded.has_value() && aside_timer.timer.time == -1,
          "The widened picture's extra subtree changes no state");

    // A debug camera draws everything; the cull stays with the game camera.
    items.clear();
    context.picture_widen = 1.0f;
    context.picture_cull = false;
    traverse(&root, context);
    check(items.size() == 2 && !aside_recorder.recorded.has_value(),
          "Without the picture's cull the original's cull still decides the updates");
}

// What smooth motion reads from the traversal (DrawItem::instance / phase)
// and the picture's cull with the previous step's camera.
void test_presentation_trail() {
    // root -> place_a (x -20) -> shared; root -> place_b (x +20) -> spin -> shared:
    // one geometry node placed twice (a DAG, as the stagML11 pillars).
    Node root, place_a, place_b, spin, shared;
    root.kind = NodeKind::group;
    place_a.kind = place_b.kind = spin.kind = NodeKind::transform;
    place_a.matrix = translation(-20, 0, -100);
    place_b.matrix = translation(20, 0, -100);
    auto seq = std::make_shared<TransformSequence>(line_sequence(1'000'000'000));
    seq->translations = {{0, 0, 0}, {0, 1, 0}}; // REPEAT up by one: the wrap jumps back down
    spin.sequence = seq;
    shared.kind = NodeKind::geometry;
    auto quad = std::make_shared<GeometryData>();
    quad->valid = true;
    shared.runtime_geometry = quad;
    shared.has_bound = true;
    shared.bound_min = {-1, -1, -1};
    shared.bound_max = {1, 1, 1};
    root.children = {&place_a, &place_b};
    place_a.children = {&shared};
    place_b.children = {&spin};
    spin.children = {&shared};

    TraversalContext context;
    context.cull = true;
    context.time = 500'000'000;
    context.cull_camera.projection = perspective_horizontal(3.14159265f / 2.0f, 640.0f / 448.0f, 10.0f, 2000.0f);
    context.camera = context.cull_camera;
    std::vector<DrawItem> items;
    context.output = &items;
    traverse(&root, context);
    check(items.size() == 2 && items[0].node == items[1].node && items[0].geometry == items[1].geometry,
          "One node and geometry drawn at two placements");
    check(items[0].instance != items[1].instance, "Each placement has its own instance");
    const auto a = items[0].instance, b = items[1].instance;
    const auto phase_a = items[0].phase, phase_b = items[1].phase;

    // Looking right: placement a leaves the picture, b keeps its instance.
    const Mat4 turned = look_at({0, 0, 0}, {0.85f, 0, -1}, {0, 1, 0}); // 40 degrees to the right
    context.camera.view = context.cull_camera.view = turned;
    items.clear();
    traverse(&root, context);
    check(items.size() == 1 && items[0].instance == b, "A placement keeps its instance when another one is culled");

    // The sequence above placement b wraps: its phase changes, a's does not.
    context.camera.view = context.cull_camera.view = Mat4{};
    context.time = 1'500'000'000;
    items.clear();
    traverse(&root, context);
    check(items.size() == 2 && items[0].instance == a && items[1].instance == b, "The instances are stable");
    check(items[1].phase != phase_b, "A wrap above the item changes its phase");
    check(items[0].phase == phase_a, "A placement without the sequence keeps its phase");

    // Smooth motion: the previous step looked straight ahead, so placement a
    // (outside this step's frustum) is still drawn, without updates below.
    context.time = 500'000'000;
    context.camera.view = context.cull_camera.view = turned;
    context.picture_previous_view = Mat4{};
    items.clear();
    traverse(&root, context);
    check(items.size() == 2 && items[0].instance == a, "The previous step's frustum keeps placement a in the picture");
    context.picture_previous_view.reset();
    items.clear();
    traverse(&root, context);
    check(items.size() == 1, "Without smooth motion only this step's frustum draws");
}

// A beModelCtrlNode2's node clock given an earlier origin (ACTION's
// JOINTSET, a joint loop: the stagis10 menu background's turning parts
// every 358 steps) takes back the sequence time of what its sequences
// place, nothing else.
void test_node_clock_phase() {
    Node model, spin, turned, still;
    model.kind = NodeKind::model_node;
    spin.kind = NodeKind::transform;
    spin.sequence = std::make_shared<TransformSequence>(line_sequence(4'000'000'000));
    turned.kind = still.kind = NodeKind::geometry;
    auto quad = std::make_shared<GeometryData>();
    quad->valid = true;
    turned.runtime_geometry = still.runtime_geometry = quad;
    model.children = {&spin, &still};
    spin.children = {&turned};
    TraversalContext context;
    context.time = 1'000'000'000;
    std::vector<DrawItem> items;
    context.output = &items;
    traverse(&model, context);
    check(items.size() == 2, "Both items are drawn");
    const auto turned_time = items[0].sequence_time;
    check(turned_time > 0 && items[1].sequence_time == 0, "Only the sequence's item has a sequence time");
    context.time += frame;
    items.clear();
    traverse(&model, context);
    check(items[0].sequence_time > turned_time, "A running node clock moves the sequence time on");
    model.time_bias = -500'000'000; // a new origin: the sequence runs again from an earlier time
    items.clear();
    traverse(&model, context);
    check(items[0].sequence_time < turned_time, "An earlier node clock origin takes the sequence time back");
    check(items[1].sequence_time == 0, "Items no sequence places have none");
}

// A beMeterCtrlNode's children run at the meter's value (Node::fixed_time):
// the meter falling is no clock going back, but a jump of its sequence
// still changes the phase.
void test_meter_phase() {
    Node meter, bar, shape;
    meter.kind = NodeKind::group;
    bar.kind = NodeKind::transform;
    auto seq = std::make_shared<TransformSequence>(line_sequence(4'000'000'000));
    seq->translations = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    seq->times = {1'000'000'000, 2'000'000'000, 3'000'000'000};
    bar.sequence = seq;
    shape.kind = NodeKind::geometry;
    auto quad = std::make_shared<GeometryData>();
    quad->valid = true;
    shape.runtime_geometry = quad;
    meter.children = {&bar};
    bar.children = {&shape};
    TraversalContext context;
    context.time = 10'000'000'000;
    std::vector<DrawItem> items;
    context.output = &items;
    meter.fixed_time = 2'500'000'000;
    traverse(&meter, context);
    check(items.size() == 1, "The meter's bar is drawn");
    const auto phase = items[0].phase;
    const auto time = items[0].sequence_time;
    meter.fixed_time = 2'450'000'000; // the meter falls
    items.clear();
    traverse(&meter, context);
    check(items[0].sequence_time >= time, "A falling meter takes no sequence time back");
    check(items[0].phase == phase, "A falling meter keeps the phase");
    check(items[0].model(3, 0) < 1.5f, "(the bar moved with the meter)");
    seq->methods[0] = TransformSequence::none; // a stepped meter
    items.clear();
    traverse(&meter, context);
    const auto stepped = items[0].phase;
    meter.fixed_time = 1'900'000'000; // into the key before
    items.clear();
    traverse(&meter, context);
    check(items[0].phase != stepped, "A stepped meter's key change still changes the phase");
    // A clock-driven placement of the same sequence going back is a jump.
    meter.fixed_time.reset();
    context.time = 2'500'000'000;
    items.clear();
    traverse(&meter, context);
    const auto clocked = items[0].sequence_time;
    context.time = 1'500'000'000;
    items.clear();
    traverse(&meter, context);
    check(items[0].sequence_time < clocked, "Without the meter a clock going back takes the sequence time back");
}

void test_previous_camera_cull_state() {
    // As test_frustum_cull_traversal: the subtree the previous camera saw is
    // drawn, but its recorder and ParticleTimer stay as the original leaves them.
    Node root, aside, aside_recorder, aside_timer, aside_geometry;
    root.kind = NodeKind::group;
    aside.kind = NodeKind::group;
    aside_recorder.kind = NodeKind::transform_recorder;
    aside_timer.kind = NodeKind::particle_timer;
    aside_geometry.kind = NodeKind::geometry;
    auto quad = std::make_shared<GeometryData>();
    quad->valid = true;
    aside_geometry.runtime_geometry = quad;
    aside.has_bound = true;
    aside.bound_min = {110, -10, -110};
    aside.bound_max = {120, 10, -90};
    root.children = {&aside};
    aside.children = {&aside_recorder, &aside_timer, &aside_geometry};
    aside_timer.timer.running = true;
    aside_timer.timer.time = -1;
    TraversalContext context;
    context.cull = true;
    context.time = 5000 * frame;
    context.cull_camera.projection = perspective_horizontal(3.14159265f / 2.0f, 640.0f / 448.0f, 10.0f, 2000.0f);
    context.camera = context.cull_camera;
    context.picture_previous_view = look_at({0, 0, 0}, {0.85f, 0, -1}, {0, 1, 0}); // turned right: the box is ahead
    std::vector<DrawItem> items;
    context.output = &items;
    traverse(&root, context);
    check(items.size() == 1 && items[0].node == &aside_geometry, "The previous camera's frustum draws the subtree");
    check(!aside_recorder.recorded.has_value() && aside_timer.timer.time == -1,
          "The previous camera's extra subtree changes no state");
    // The shadow casters (light view as the modelview base) ignore it.
    context.modelview_base = Mat4{};
    items.clear();
    traverse(&root, context);
    check(items.empty(), "Under a modelview base the previous camera does not apply");
}

void test_particle_records() {
    // y_messcr (the menu cursor ping): one 160-byte record, no Bec terms.
    std::vector<std::uint8_t> one(160, 1);
    check(particle_record(one, 0, 160).size() == 160, "A 160-byte record is complete without the 0xB4 Bec tail");
    check(particle_record(one, 1, 160).empty(), "There is no record past the data");
    // y_dbreak02: 15 records of 160 bytes; the last one is drawn too.
    std::vector<std::uint8_t> fifteen(15 * 160);
    fifteen[14 * 160] = 7;
    const auto last = particle_record(fifteen, 14, 160);
    check(last.size() == 160 && last[0] == 7, "The last of 15 records starts at 14 * stride");
    const auto inner = particle_record(fifteen, 3, 160);
    check(inner.size() == 0xB4 && inner.data() == fifteen.data() + 3 * 160,
          "Inner records read 0xB4 bytes into the next record like the original");
    std::vector<std::uint8_t> bec(2 * 180);
    check(particle_record(bec, 1, 180).size() == 0xB4, "180-byte records are read whole");
}

void test_gx_colors() {
    // The evolution demo's i_evowipe fade step at 127.54 / 255: igColorAttr
    // rounds (8012E1D8) to 128, the lighting / fog / clear rule truncates.
    const float fade = 127.54f / 255.0f;
    check(gx_round_byte(fade) == 128 && gx_trunc_byte(fade) == 127, "A fade at 127.54 / 255 is 128 rounded, 127 truncated");
    check(gx_round_byte(0.5f) == 128 && gx_trunc_byte(0.5f) == 127, "Half rounds up, truncation drops it");
    check(gx_round_byte(1.0f) == 255 && gx_trunc_byte(1.0f) == 255 && gx_round_byte(0.0f) == 0, "The bounds stay 0 and 255");
    check(gx_round_byte(1.1f) == 255 && gx_trunc_byte(1.1f) == 255, "Above 1 clamps (the lobby's 1.1 light)");
    check(gx_round_byte(-0.25f) == 0 && gx_trunc_byte(-0.25f) == 0, "Below 0 clamps");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(gx_round_byte(nan) == 0, "8012E1D8 does not clamp a NaN: fctiwz gives 0x80000000, a 0 byte");
    check(gx_trunc_byte(nan) == 255, "The ambient / material clamp (fcmpo + bge) takes a NaN to 1");
    check(gx_trunc_light_byte(nan) == 0 && gx_trunc_light_byte(1.1f) == 255 && gx_trunc_light_byte(fade) == 127,
          "The light colour clamp (fcmpo + ble) lets a NaN through to fctiwz: 0");
    check(gx_round(0.9f) == 230.0f / 255.0f && gx_trunc(0.9f) == 229.0f / 255.0f, "The TEXTCTL default 0.9 is 230 rounded");
    const Vec4f c = gx_round(Vec4f{1.0f, fade, 0.1f, 0.0f});
    check(c.x == 1.0f && c.y == 128.0f / 255.0f && c.z == 26.0f / 255.0f && c.w == 0.0f, "Each channel rounds on its own");
}

// A matrix from its twelve float words (rows 0..3, columns 0..2), the
// fourth column (0, 0, 0, 1).
Mat4 words12(const std::uint32_t (&w)[12]) {
    Mat4 m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) m(r, c) = std::bit_cast<float>(w[r * 3 + c]);
    return m;
}

bool same_words(const Mat4& m, const std::uint32_t (&w)[12]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            if (std::bit_cast<std::uint32_t>(m(r, c)) != w[r * 3 + c]) return false;
    return true;
}

void test_recorder_modelview() {
    // The stag0203 train route (Dolphin n 4585, hooks P 0x801DC978, R
    // 0x801DEA4C, V 0x801DEA54): model node (identity) -> route00 (turned
    // pi about Y, sin(pi) = 8.74e-8) -> route01 (a translation of 1e-14) ->
    // the recorder. The original pushes local * top (80128690) from the
    // view, and _m = top * inverse view (801DE9C0): rounding noise of
    // 1e-9..1e-7 in the rotation and up to 0.00005 in the translation that
    // the world matrix product does not have.
    static constexpr std::uint32_t view[12] = {0xbf000001, 0x3ebae377, 0xbf490c34, 0xb33bbd2f, 0x3f68265d, 0x3ed7cce2,
                                               0x3f5db3d7, 0x3e57cce6, 0xbee8265e, 0xc50d4ad8, 0x440c05ed, 0xc4a39fd0};
    static constexpr std::uint32_t route00[12] = {0xbf800000, 0x00000000, 0x33bbbd2e, 0x00000000, 0x3f800000, 0x00000000,
                                                  0xb3bbbd2e, 0x00000000, 0xbf800000, 0xc50fc000, 0x41200000, 0x44a28000};
    static constexpr std::uint32_t route01[12] = {0x3f800000, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0, 0x28800000, 0xa8cc2100};
    static constexpr std::uint32_t top[12] = {0x3f000002, 0xbebae376, 0x3f490c33, 0xb33bbd2f, 0x3f68265d, 0x3ed7cce2,
                                              0xbf5db3d6, 0xbe57cce8, 0x3ee82660, 0x41727c00, 0x4065a100, 0xc2afde90};
    static constexpr std::uint32_t recorded[12] = {0xbf800000, 0x30a69060, 0x33c97cd2, 0x316e50c0, 0x3f800000, 0x32093904,
                                                   0xb39512e0, 0x31198300, 0xbf800000, 0xc50fc001, 0x41200030, 0x44a27fff};
    static constexpr std::uint32_t inverse[12] = {0xbf000001, 0xb3713414, 0x3f5db3d7, 0x3ebae378, 0x3f68265d, 0x3e57cce6,
                                                  0xbf490c34, 0x3ed7cce2, 0xbee8265e, 0xc513aca8, 0x422f3cf3, 0x449bc7ee};
    Node root, model, first, second, recorder;
    root.kind = NodeKind::group;
    model.kind = NodeKind::model_node;
    first.kind = second.kind = NodeKind::transform;
    recorder.kind = NodeKind::transform_recorder;
    first.matrix = words12(route00);
    second.matrix = words12(route01);
    root.children = {&model};
    model.children = {&first};
    first.children = {&second};
    second.children = {&recorder};
    TraversalContext context;
    context.cull = true;
    context.cull_camera.view = words12(view);
    context.camera = context.cull_camera;
    traverse(&root, context);
    check(same_words(context.traversal_inverse_view, inverse), "The traversal's inverse view is 8012A92C of the view");
    check(same_words(gekko::multiply(second.matrix, gekko::multiply(first.matrix, context.cull_camera.view)), top),
          "The modelview pushes round like 80128690 (Dolphin R n 4585)");
    check(recorder.recorded && same_words(*recorder.recorded, recorded),
          "The recorder's _m is the stack top times the inverse view (Dolphin V n 4585)");
    check(!same_words(second.matrix * first.matrix, recorded), "The world matrix product lacks the original's noise");
}

int main() {
    try {
        test_gx_colors();
        test_lod_selection();
        test_start_and_advance();
        test_long_step();
        test_loop_and_lock();
        test_sequence_rotation_layout();
        test_sequence_play_modes();
        test_sequence_interpolation_methods();
        test_compressed_sequence();
        test_sequence_phase();
        test_horizontal_perspective();
        test_billboard_matrix();
        test_system_random();
        test_field_time();
        test_motion_blend_tracks();
        test_text_layout();
        test_frustum_cull_code();
        test_particle_timer_state();
        test_frustum_cull_traversal();
        test_presentation_trail();
        test_node_clock_phase();
        test_meter_phase();
        test_previous_camera_cull_state();
        test_recorder_modelview();
        test_particle_records();
        test_card_setting();
        test_gate_stage_name();
        test_fast_loading_snapshot();
    } catch (const std::exception& error) {
        std::cerr << "scene tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "scene tests passed (" << checks << " checks)\n";
    return 0;
}
