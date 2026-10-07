#include "dw4/engine/engine.hpp"
#include "dw4/engine/random.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <iostream>
#include <sstream>
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

int main() {
    try {
        test_start_and_advance();
        test_long_step();
        test_loop_and_lock();
        test_system_random();
        test_field_time();
    } catch (const std::exception& error) {
        std::cerr << "scene tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "scene tests passed (" << checks << " checks)\n";
    return 0;
}
