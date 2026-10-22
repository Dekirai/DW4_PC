#include "dw4/console_reset.hpp"
#include "dw4/pad.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation) {
    ++checks;
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Malformed pad input was accepted");
}

std::uint32_t direction(PadStick stick) {
    OriginalPads pads;
    OriginalPads::Samples samples{};
    samples[0] = {true, stick};
    pads.update(samples);
    return pads.states()[0].left_directions.held;
}

void analog_directions() {
    check(direction({}) == 0, "Centered stick activated directions");
    check(direction({0.3999f, 0}) == 0, "Subthreshold stick activated directions");
    check(direction({0.4f, 0}) == 2, "Activation threshold must be inclusive");
    check(direction({std::nextafter(0.4f, 0.0f), 0}) == 0, "Adjacent lower radius crossed threshold");
    check(direction({0.28f, 0.28f}) == 0, "Diagonal radius test became an axis test");
    check(direction({0.3f, 0.3f}) == 3, "Diagonal activation did not use magnitude");
    const std::array<PadStick, 8> points{{
        {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    }};
    const std::array<std::uint32_t, 8> expected{2, 3, 1, 9, 8, 12, 4, 6};
    for (std::size_t i = 0; i < points.size(); ++i) {
        check(direction(points[i]) == expected[i], "Original direction bit assignment changed");
    }
    check(direction({-1, -0.0f}) == 8 && direction({-1, 0}) == 8,
        "Signed zero at the angle branch cut changed direction");

    constexpr float q = 0x1.921fb6p-1f;
    constexpr float tq = 0x1.2d97c8p+1f;
    constexpr float h = q * 0.5f;
    struct Boundary { float angle; std::uint32_t below, exact, above; };
    // Literal expectations come from 80311BC0-80311C88 branch comparisons.
    const std::array<Boundary, 8> boundaries{{
        {-tq - h, 8, 12, 12}, {-tq + h, 12, 4, 4},
        {-q - h, 4, 4, 6}, {-q + h, 6, 6, 2},
        {q - h, 2, 3, 3}, {q + h, 3, 1, 1},
        {tq - h, 1, 1, 9}, {tq + h, 9, 9, 8}
    }};
    for (const auto& boundary : boundaries) {
        const auto point = [](float angle) {
            return PadStick{static_cast<float>(std::cos(static_cast<double>(angle))),
                static_cast<float>(std::sin(static_cast<double>(angle)))};
        };
        auto at = point(boundary.angle);
        const auto angle_of = [](PadStick value) {
            return static_cast<float>(std::atan2(static_cast<double>(value.y), static_cast<double>(value.x)));
        };
        // Rounded sin/cos need not round-trip exactly. Select an adjacent
        // representable input whose angle does, so this checks equality rather
        // than accidentally checking one side of an intended boundary.
        for (int offset = -4; offset <= 4 && angle_of(at) != boundary.angle; ++offset) {
            auto candidate = point(boundary.angle);
            for (int step = 0; step < std::abs(offset); ++step) {
                candidate.x = std::nextafter(candidate.x, offset < 0 ? -2.0f : 2.0f);
            }
            if (angle_of(candidate) == boundary.angle) at = candidate;
        }
        const auto round_trip = angle_of(at);
        check(round_trip == boundary.angle, "Angular fixture does not lie on the intended float boundary");
        check(direction(point(boundary.angle - 0.00001f)) == boundary.below,
            "Direction below an angular boundary is wrong");
        check(direction(at) == boundary.exact, "Angular boundary inclusivity changed");
        check(direction(point(boundary.angle + 0.00001f)) == boundary.above,
            "Direction above an angular boundary is wrong");
    }
}

void repeats_and_independence() {
    OriginalPads pads;
    OriginalPads::Samples samples{};
    pads.update(samples);
    check(pads.states()[0].buttons.repeat_countdown == 0xffffffffu,
        "Initial idle decrement must wrap, not start a repeat delay");
    samples[0] = {true, {0.2f, -0.1f}, {0, 1}, pad_button::a};
    samples[1] = {true, {-1, 0}, {}, pad_button::b};
    samples[2] = {true, {}, {1, 0}, pad_button::start};
    samples[3] = {true, {1, 1}, {-1, -1}, pad_button::l | pad_button::r};
    pads.update(samples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& state = pads.states()[i];
        check(state.connected && state.buttons.held == samples[i].buttons,
            "A physical pad sample leaked into another slot");
        check(state.buttons.trigger == samples[i].buttons && state.buttons.repeat == samples[i].buttons,
            "Initial press did not produce both trigger and repeat");
        check(state.buttons.repeat_countdown == 10, "Initial repeat delay is not ten updates");
        check(state.left == samples[i].left && state.right == samples[i].right,
            "Analog storage applied an unrequested deadzone or normalization");
    }
    for (int update = 1; update <= 22; ++update) {
        if (update == 2) samples[1].buttons |= pad_button::x;
        if (update == 5) samples[0].left = {1, 0};
        pads.update(samples);
        const auto& first = pads.states()[0];
        check(first.buttons.trigger == 0, "Held button emitted another trigger");
        check(first.buttons.repeat == ((update == 10 || update == 16 || update == 22) ? pad_button::a : 0),
            "Repeat did not occur after exactly ten, then six, updates");
        check(first.right_directions.repeat == ((update == 10 || update == 16 || update == 22) ? 1u : 0u),
            "Right stick timer was affected by another channel");
        if (update >= 5) {
            check(first.left_directions.repeat == ((update == 5 || update == 15 || update == 21) ? 2u : 0u),
                "Left stick did not keep an independent repeat timer");
        }
        if (update == 2) {
            check(pads.states()[1].buttons.trigger == pad_button::x &&
                pads.states()[1].buttons.repeat == pad_button::x,
                "Chord addition retriggered already held buttons");
        }
        if (update == 12 || update == 18) {
            check(pads.states()[1].buttons.repeat == (pad_button::b | pad_button::x),
                "Chord repeat did not emit the entire held mask");
        }
    }
    samples[1].buttons = pad_button::b;
    pads.update(samples);
    check(pads.states()[1].buttons.trigger == 0 && pads.states()[1].buttons.repeat == 0 &&
        pads.states()[1].buttons.repeat_countdown == 10,
        "Chord release must restart delay without triggering the remaining button");

    samples[2].buttons |= pad_button::x;
    pads.update(samples); // Freeze while trigger/repeat outputs are nonzero.
    const auto before_freeze = pads.states();
    samples = {};
    for (int i = 0; i < 20; ++i) pads.update(samples, true);
    check(pads.states() == before_freeze, "Frozen manager update changed pad state or timers");
    pads.update(samples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& state = pads.states()[i];
        check(!state.connected && state.left == PadStick{} && state.right == PadStick{},
            "Disconnect retained connected state or analog input");
        check(state.buttons.previous == before_freeze[i].buttons.held && state.buttons.held == 0 &&
            state.buttons.trigger == 0 && state.buttons.repeat == 0,
            "Disconnect did not follow the original release transition");
    }
    samples[3] = {true, {}, {}, pad_button::a};
    pads.update(samples);
    check(pads.states()[3].buttons.trigger == pad_button::a && pads.states()[0].buttons.held == 0,
        "Reconnect did not start a fresh edge in its own slot");
    pads.reset();
    check(pads.states() == OriginalPads::States{}, "Reset retained controller history");
}

void conversions() {
    constexpr std::array<std::uint32_t, 16> expected{
        0x80, 0x20, 0x40, 0x10, 0x200, 0x800, 0x400, 0,
        0x4000, 0x8000, 0x2000, 0x1000, 8, 0, 0, 0
    };
    for (std::size_t bit = 0; bit < expected.size(); ++bit) {
        check(gamecube_buttons(static_cast<std::uint16_t>(1u << bit)) == expected[bit],
            "GameCube raw bit did not map to the original logical bit");
    }
    check(gamecube_buttons(0xffff) == 0xfef8, "Mapped GameCube button union is wrong");
    check(gamecube_buttons(0xe080) == 0, "Unmapped hardware bits leaked into logical buttons");
    for (const bool c_stick : {false, true}) {
        for (int y = -15; y <= 15; ++y) {
            for (int x = -15; x <= 15; ++x) {
                check(gamecube_stick(static_cast<std::int8_t>(x), static_cast<std::int8_t>(y), c_stick) == PadStick{},
                    "SDK component deadzone is not inclusive");
            }
        }
        const float maximum = static_cast<float>(c_stick ? 59 : 72) / 74.0f;
        const float diagonal = static_cast<float>(c_stick ? 31 : 40) / 74.0f;
        check(gamecube_stick(127, 0, c_stick) == PadStick{maximum, 0},
            "Positive stick axis clamp or division by 74 changed");
        check(gamecube_stick(-128, 0, c_stick) == PadStick{-maximum, 0},
            "Minimum signed byte overflowed when computing its magnitude");
        check(gamecube_stick(127, -128, c_stick) ==
            PadStick{static_cast<float>(c_stick ? 30 : 39) / 74.0f, -diagonal},
            "Unequal diagonal did not preserve original truncating integer division");
        check(gamecube_stick(127, 127, c_stick) == PadStick{diagonal, diagonal},
            "SDK octagonal diagonal was replaced with a circular clamp");
        check(gamecube_stick(16, -16, c_stick) == PadStick{1.0f / 74.0f, -1.0f / 74.0f},
            "SDK component deadzone was not subtracted before scaling");
    }
    // The walk test (8034C4D4) is the held direction mask. Measured in Dolphin
    // (dng101b1): raw x 34/40/44 (0.257/0.338/0.392) neither walk nor turn,
    // 45 (0.405) walks.
    for (const int x : {34, 40, 44})
        check(direction(gamecube_stick(static_cast<std::int8_t>(x), 0)) == 0 &&
                  direction(gamecube_stick(static_cast<std::int8_t>(-x), 0)) == 0,
            "A stick below the activation radius set a direction (the player would walk)");
    check(direction(gamecube_stick(45, 0)) == 2 && direction(gamecube_stick(-45, 0)) == 8,
        "A stick just above the activation radius set no direction");
    check(direction(gamecube_stick(32, 32)) == 0 && direction(gamecube_stick(36, 36)) == 3,
        "The diagonal activation radius changed");
}

void invalid_inputs() {
    OriginalPads pads;
    OriginalPads::Samples samples{};
    samples[0] = {true, {1, 0}, {}, pad_button::a};
    pads.update(samples);
    const auto before = pads.states();
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 2.01f, -2.01f}) {
        samples[3] = {true, {}, {invalid, 0}, 0};
        rejects([&] { pads.update(samples); });
        check(pads.states() == before, "Malformed later slot partially advanced earlier slot timers");
        pads.update(samples, true);
        check(pads.states() == before, "Frozen input validated or consumed unused samples");
    }
    samples[3].connected = false;
    pads.update(samples);
    check(!pads.states()[3].connected, "Disconnected unused axes were treated as input");
    samples[3] = {true, {2, -2}, {}, 0};
    pads.update(samples);
    check(pads.states()[3].left == samples[3].left, "Valid bounded analog endpoints were changed");
}
// The controller's B+X+START combination presses the reset button
// (ConsoleReset): 180 fields to the tap, 33 more to the latched release.
void console_reset() {
    constexpr std::uint32_t combo = pad_button::b | pad_button::x | pad_button::start;
    auto run = [](ConsoleReset& reset, std::uint32_t buttons, int fields, int pad = 0, bool connected = true) {
        std::array<PadSample, 4> samples{};
        samples[std::size_t(pad)] = {connected, {}, {}, buttons};
        int first = -1;
        for (int i = 0; i < fields; ++i)
            if (reset.update(samples) && first < 0) first = i;
        return first;
    };
    {
        // Held from field 0: the tap at field 180, the reboot at 213.
        ConsoleReset reset;
        check(run(reset, combo, 300) == 213, "Reset combination did not reboot 213 fields after it started");
        check(reset.latched(), "Reset release was not latched");
    }
    {
        // Released after 180 fields (just before the tap): nothing.
        ConsoleReset reset;
        check(run(reset, combo, 180) < 0, "Combination shorter than three seconds pressed the reset button");
        check(run(reset, 0, 100) < 0 && !reset.button_down(), "Released combination reset later");
    }
    {
        // Released right after the tap: the button still goes down for half
        // a second, so the reboot comes at the same field.
        ConsoleReset reset;
        check(run(reset, combo, 181) < 0 && reset.button_down(), "Tap did not press the reset button");
        check(run(reset, 0, 100) == 213 - 181, "Reset after the tap did not follow the button's release");
    }
    {
        // A, Y or the D-pad: Dolphin compares the face buttons only.
        ConsoleReset reset;
        check(run(reset, combo | pad_button::a, 400) < 0, "Combination with A pressed the reset button");
        check(run(reset, combo | pad_button::y, 400) < 0, "Combination with Y pressed the reset button");
        ConsoleReset dpad;
        check(run(dpad, combo | pad_button::up | pad_button::r | pad_button::z, 300) == 213, "D-pad, R or Z blocked the combination");
    }
    {
        // Every controller has the combination; a disconnected one none.
        ConsoleReset reset;
        check(run(reset, combo, 300, 3) == 213, "Controller 4 combination did not reboot");
        ConsoleReset off;
        check(run(off, combo, 300, 2, false) < 0, "Disconnected controller pressed the reset button");
    }
    {
        // A latched release waits while the reset is disabled (+0x5F).
        ConsoleReset reset;
        reset.set_enabled(false);
        check(run(reset, combo, 300) < 0 && reset.latched(), "Disabled reset lost the latched release");
        reset.set_enabled(true);
        check(run(reset, 0, 1) == 0, "Enabled reset did not reboot on the latched release");
    }
    {
        // An interrupted combination starts its three seconds again.
        ConsoleReset reset;
        check(run(reset, combo, 150) < 0 && run(reset, 0, 1) < 0, "Interrupted combination reset");
        check(run(reset, combo, 300) == 213, "Combination timer did not restart");
    }
}

// Host controls keep the GameCube pad's three direction channels apart: the
// player walks on the main stick's held mask (8034C4D4), the quick panels'
// cursor reads the C-stick's and the D-pad's trigger/repeat (8034A24C..60).
void host_inputs() {
    const auto states = [](const HostPadInput& input) {
        OriginalPads pads;
        OriginalPads::Samples samples{};
        samples[0] = host_pad_sample(input);
        pads.update(samples);
        return pads.states()[0];
    };
    HostPadInput walk;
    walk.stick_directions = pad_direction::positive_y;
    auto state = states(walk);
    check(state.buttons.held == 0, "Movement keys set D-pad bits");
    check(state.left_directions.held == pad_direction::positive_y, "Movement keys did not push the main stick");
    check(state.right_directions.held == 0, "Movement keys moved the C-stick");
    check(state.left.y == 72.0f / 74.0f && state.left.x == 0.0f, "Main stick full deflection is not 72/74");

    HostPadInput dpad;
    dpad.buttons = pad_button::down;
    state = states(dpad);
    check(state.buttons.held == pad_button::down, "D-pad bit lost");
    check(state.left_directions.held == 0 && state.right_directions.held == 0, "The D-pad moved a stick");

    HostPadInput c_stick;
    c_stick.c_stick_directions = pad_direction::negative_y;
    state = states(c_stick);
    check(state.right_directions.held == pad_direction::negative_y && state.left_directions.held == 0 && state.buttons.held == 0,
          "C-stick keys left their channel");
    check(state.right.y == -59.0f / 74.0f, "C-stick full deflection is not 59/74");

    // Analog sticks win over keys while moved; keys push when centred.
    HostPadInput both;
    both.stick_directions = pad_direction::positive_x;
    both.stick = {0.0f, -0.5f};
    check(host_pad_sample(both).left.x == 0.0f && host_pad_sample(both).left.y == -0.5f * (72.0f / 74.0f),
          "A moved analog stick did not replace the keys");
    both.stick = {};
    check(host_pad_sample(both).left.x == 72.0f / 74.0f, "Keys did not push a centred stick");
    both.stick_directions = pad_direction::positive_x | pad_direction::negative_x | pad_direction::positive_y | pad_direction::negative_y;
    check(host_pad_sample(both).left.x > 0.0f && host_pad_sample(both).left.y < 0.0f, "Opposite keys changed priority");
    both.stick = {3.0f, -7.0f};
    check(host_pad_sample(both).left.x == 72.0f / 74.0f && host_pad_sample(both).left.y == -72.0f / 74.0f,
          "Analog input beyond the host range was not limited");

    // A second device of the same player (keyboard + first controller).
    HostPadInput keyboard, controller;
    keyboard.buttons = pad_button::a;
    keyboard.stick_directions = pad_direction::negative_x;
    controller.buttons = pad_button::up;
    controller.c_stick = {0.0f, 1.0f};
    merge(keyboard, controller);
    const auto merged = host_pad_sample(keyboard);
    check(merged.buttons == (pad_button::a | pad_button::up), "Merged buttons lost");
    check(merged.left.x == -72.0f / 74.0f && merged.right.y == 59.0f / 74.0f, "Merged sticks lost");
    HostPadInput idle;
    merge(keyboard, idle);
    check(keyboard.c_stick.y == 1.0f, "A centred stick replaced a moved one");
}
} // namespace

int main() {
    try {
        analog_directions();
        repeats_and_independence();
        conversions();
        invalid_inputs();
        console_reset();
        host_inputs();
        std::cout << checks << " original pad checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Pad test failure: " << error.what() << '\n';
        return 1;
    }
}
