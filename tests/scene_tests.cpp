#include "dw4/engine/math.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

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
} // namespace

int main() {
    try {
        test_start_and_advance();
        test_long_step();
        test_loop_and_lock();
        test_sequence_rotation_layout();
        test_sequence_play_modes();
        test_sequence_interpolation_methods();
        test_compressed_sequence();
        test_horizontal_perspective();
    } catch (const std::exception& error) {
        std::cerr << "scene tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "scene tests passed (" << checks << " checks)\n";
    return 0;
}
