// The Switch 2 Pro Controller reader (src/game/switch2_pro, docs/ROUND_SW2WIN_NOTES.md)
// without a controller or a window:
//
// - the start commands, the flash reads and the player lights as SDL 3.4.16's
//   driver sends them, and their replies;
// - the stick calibration from flash blocks built from the documented format
//   (with the values measured on a controller), SDL's stick arithmetic;
// - input reports 0x05 and 0x09 built byte by byte from the documented
//   layouts (and one written out as a USB capture shows it), and what the
//   SDL pad reports for them;
// - the state machine on a fake transport with the test's own clock: open,
//   start, read, the report rate, a stall, unplugging, another program on
//   interface 1, the driver states, a start command that fails;
// - the whole reader (its threads) on the fake transport with SDL's virtual
//   joysticks and every real backend off: the pad's family, its buttons by
//   label through the host's own pad code, unplugging and plugging in again,
//   two controllers, the player lights, and the game's window in the
//   background (a hidden window without the input focus: SDL ignores the
//   pad's presses as for every pad, the reader keeps reading and the pad).
#include "../src/game/pad_family.hpp"
#include "../src/game/pad_host.hpp"
#include "../src/game/switch2_pro.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using namespace dw4::game;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

bool has_line(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.find(part) != std::string::npos; });
}
std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& line : lines) out += line + "\n";
    return out;
}

// ---- The documented formats, written out independently of the reader

// Two 12-bit values as the reports and the flash pack them.
void pack12(std::uint8_t* out, unsigned x, unsigned y) {
    out[0] = std::uint8_t(x & 0xff);
    out[1] = std::uint8_t(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
    out[2] = std::uint8_t(y >> 4);
}

// What a report carries, by name.
struct Held {
    bool a = false, b = false, x = false, y = false, l = false, r = false, zl = false, zr = false, minus = false, plus = false, home = false,
         capture = false, c = false, gl = false, gr = false, ls = false, rs = false, up = false, down = false, left = false, right = false;
    unsigned lx = 2048, ly = 2048, rx = 2048, ry = 2048;
};

// Report 0x05 (SDL_hidapi_switch2.c HandleSwitchProState; HIDMaestro's
// table): 05, a timer, buttons in bytes 5..8, the sticks at 11 and 14.
std::array<std::uint8_t, 64> report05(const Held& h, std::uint32_t timer = 0) {
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x05;
    r[1] = std::uint8_t(timer), r[2] = std::uint8_t(timer >> 8), r[3] = std::uint8_t(timer >> 16), r[4] = std::uint8_t(timer >> 24);
    r[5] = std::uint8_t((h.y ? 0x01 : 0) | (h.x ? 0x02 : 0) | (h.b ? 0x04 : 0) | (h.a ? 0x08 : 0) | (h.r ? 0x40 : 0) | (h.zr ? 0x80 : 0));
    r[6] = std::uint8_t((h.minus ? 0x01 : 0) | (h.plus ? 0x02 : 0) | (h.rs ? 0x04 : 0) | (h.ls ? 0x08 : 0) | (h.home ? 0x10 : 0) |
                        (h.capture ? 0x20 : 0) | (h.c ? 0x40 : 0));
    r[7] = std::uint8_t((h.down ? 0x01 : 0) | (h.up ? 0x02 : 0) | (h.right ? 0x04 : 0) | (h.left ? 0x08 : 0) | (h.l ? 0x40 : 0) | (h.zl ? 0x80 : 0));
    r[8] = std::uint8_t((h.gr ? 0x01 : 0) | (h.gl ? 0x02 : 0));
    pack12(&r[11], h.lx, h.ly);
    pack12(&r[14], h.rx, h.ry);
    r[0x20] = 0xd8, r[0x21] = 0x0e; // battery 3800 mV (HIDMaestro's example)
    return r;
}

// Report 0x09 (procon2-mac's measurement): 09, a counter, one byte, buttons
// in bytes 3..5, the sticks at 6 and 9.
std::array<std::uint8_t, 64> report09(const Held& h) {
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x09;
    r[2] = 0x25;
    r[3] = std::uint8_t((h.b ? 0x01 : 0) | (h.a ? 0x02 : 0) | (h.y ? 0x04 : 0) | (h.x ? 0x08 : 0) | (h.r ? 0x10 : 0) | (h.zr ? 0x20 : 0) |
                        (h.plus ? 0x40 : 0) | (h.rs ? 0x80 : 0));
    r[4] = std::uint8_t((h.down ? 0x01 : 0) | (h.right ? 0x02 : 0) | (h.left ? 0x04 : 0) | (h.up ? 0x08 : 0) | (h.l ? 0x10 : 0) | (h.zl ? 0x20 : 0) |
                        (h.minus ? 0x40 : 0) | (h.ls ? 0x80 : 0));
    r[5] = std::uint8_t((h.home ? 0x01 : 0) | (h.capture ? 0x02 : 0) | (h.gr ? 0x04 : 0) | (h.gl ? 0x08 : 0) | (h.c ? 0x10 : 0));
    pack12(&r[6], h.lx, h.ly);
    pack12(&r[9], h.rx, h.ry);
    return r;
}

// A stick calibration as nine bytes: rest, travel up / right, travel down / left.
std::array<std::uint8_t, 9> stick9(unsigned cx, unsigned cy, unsigned px, unsigned py, unsigned mx, unsigned my) {
    std::array<std::uint8_t, 9> s{};
    pack12(&s[0], cx, cy);
    pack12(&s[3], px, py);
    pack12(&s[6], mx, my);
    return s;
}

// The values measured on a controller (switch2bridge-macos issue 14).
const auto kLeftMeasured = stick9(1971, 2100, 1654, 1553, 1582, 1606);
const auto kRightMeasured = stick9(2144, 2126, 1521, 1631, 1617, 1631);

// ---- Commands and replies

void commands() {
    const auto start = switch2_start_commands();
    check(start.size() == 10, "SDL's ten start commands");
    for (const auto& command : start) {
        check(command.bytes.size() == std::size_t(command.bytes[5]) + 8u, std::string("header length + data: ") + command.name);
        check(command.bytes[1] == 0x91 && command.bytes[2] == 0x00, std::string("to the controller, over USB: ") + command.name);
    }
    check(start.front().bytes[0] == 0x07 && start.front().bytes[3] == 0x01, "first 07/01");
    const auto& format = start[8].bytes;
    check(format[0] == 0x03 && format[3] == 0x0a && format[8] == kSwitch2ReportFull, "03/0A selects report 0x05");
    const auto& output = start.back().bytes;
    check(output[0] == 0x03 && output[3] == 0x0d && output[8] == 0x01 && output.size() == 16, "the last one starts the output (03/0D)");
    check(start[1].bytes[8] == 0x27 && start[4].bytes[8] == 0x27, "features 0x27 (buttons, sticks, motion, rumble)");
    const auto flash = switch2_flash_read_command(0x1FC040);
    check(flash.size() == 16 && flash[0] == 0x02 && flash[3] == 0x01 && flash[5] == 0x08, "a flash read: 02/01 with 8 bytes");
    check(flash[12] == 0x40 && flash[13] == 0xC0 && flash[14] == 0x1F && flash[15] == 0x00, "the address little-endian");
    const auto led = switch2_player_led_command(switch2_player_pattern(1));
    check(led.size() == 16 && led[0] == 0x09 && led[3] == 0x07 && led[8] == 0x03, "player 2's lights: 09/07 with 0x3");
    check(switch2_player_pattern(0) == 0x1 && switch2_player_pattern(3) == 0xf && switch2_player_pattern(-1) == 0, "SDL's patterns");
}

void replies() {
    const std::uint8_t ok[] = {0x03, 0x01, 0x00, 0x0d, 0x00, 0xf8, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    const auto reply = parse_switch2_reply(ok, sizeof ok);
    check(reply && reply->from_device && reply->command == 0x03 && reply->subcommand == 0x0d && reply->status == 0xf8, "a reply");
    check(switch2_reply_matches(*reply, switch2_start_commands().back().bytes), "it answers 03/0D");
    check(!switch2_reply_matches(*reply, switch2_start_commands()[8].bytes), "not 03/0A");
    check(!parse_switch2_reply(ok, 7), "too short");
    const auto echoed = parse_switch2_reply(switch2_start_commands().back().bytes.data(), 16);
    check(echoed && !echoed->from_device && !switch2_reply_matches(*echoed, switch2_start_commands().back().bytes), "a command is no reply");
}

// ---- Calibration

void calibration() {
    const auto left = parse_switch2_stick_calibration(kLeftMeasured.data());
    check(left.x.neutral == 1971 && left.y.neutral == 2100 && left.x.max == 1654 && left.y.max == 1553 && left.x.min == 1582 && left.y.min == 1606,
          "nine bytes: rest, travel up, travel down");
    check(switch2_calibration_usable(left), "the measured one is usable");
    std::array<std::uint8_t, 9> erased;
    erased.fill(0xff);
    check(!switch2_calibration_usable(parse_switch2_stick_calibration(erased.data())), "an erased flash is none");
    check(!switch2_calibration_usable(Switch2StickCalibration{}), "zeros are none (SDL's test)");
    // The blocks: factory at 0x28, user after B2 A1.
    std::array<std::uint8_t, 0x40> factory_left{}, factory_right{}, user{}, user_empty{};
    std::copy(kLeftMeasured.begin(), kLeftMeasured.end(), factory_left.begin() + 0x28);
    std::copy(kRightMeasured.begin(), kRightMeasured.end(), factory_right.begin() + 0x28);
    const auto user_stick = stick9(2000, 2050, 1700, 1700, 1700, 1700);
    user[0] = 0xb2, user[1] = 0xa1;
    std::copy(user_stick.begin(), user_stick.end(), user.begin() + 2);
    user_empty.fill(0xff);
    Switch2Calibration c;
    check(apply_switch2_factory_stick(c, false, factory_left.data()) && apply_switch2_factory_stick(c, true, factory_right.data()), "factory");
    check(c.left_source == Switch2CalibrationSource::factory && c.right.x.neutral == 2144, "both sticks' factory calibration");
    check(!apply_switch2_user_stick(c, false, user_empty.data()) && c.left_source == Switch2CalibrationSource::factory, "no user one stored");
    check(apply_switch2_user_stick(c, false, user.data()) && c.left_source == Switch2CalibrationSource::user && c.left.x.neutral == 2000,
          "the user's own wins");
    check(!apply_switch2_factory_stick(c, false, factory_left.data()) && c.left.x.neutral == 2000, "and stays");
    std::array<std::uint8_t, 0x40> bad{};
    bad.fill(0xff);
    Switch2Calibration none;
    check(!apply_switch2_factory_stick(none, false, bad.data()) && none.left_source == Switch2CalibrationSource::none, "erased factory block");
}

void stick_arithmetic() {
    const auto s = parse_switch2_stick_calibration(kLeftMeasured.data());
    check(switch2_stick_axis(1971, s.x, true, false) == 0, "rest is 0");
    check(switch2_stick_axis(1971 + 1654, s.x, true, false) == 32767, "full travel right");
    check(switch2_stick_axis(1971 - 1582, s.x, true, false) == -32767, "full travel left");
    check(switch2_stick_axis(4095, s.x, true, false) == 32767 && switch2_stick_axis(0, s.x, true, false) == -32768, "clamped past the travel");
    // y: up is a larger raw value; SDL's y points down (inverted with ~).
    check(switch2_stick_axis(2100 + 1553, s.y, true, true) == -32768, "full up is SDL's -32768");
    check(switch2_stick_axis(2100 - 1606, s.y, true, true) == 32766, "full down");
    check(switch2_stick_axis(2100, s.y, true, true) == -1, "rest inverted is -1 (SDL's ~0)");
    // Without a calibration: the raw range (SDL's HIDAPI_RemapVal 0..4096).
    check(switch2_stick_axis(0, {}, false, false) == -32768 && switch2_stick_axis(2048, {}, false, false) == 0, "the raw range");
    check(switch2_stick_axis(4095, {}, false, false) > 32700, "the raw range's top");
    // Measured: about 0.8 of the way without the calibration (the reason to read it).
    check(switch2_stick_axis(1971 + 1654, {}, false, false) < 26500, "uncalibrated, a full push stays short");
}

// ---- Reports

void reports() {
    namespace sb = switch2_button;
    // Every button alone, through both report layouts.
    struct One {
        bool Held::*field;
        std::uint32_t bit;
        SDL_GamepadButton sdl; // SDL's own driver's button (SDL_GAMEPAD_BUTTON_INVALID: a trigger)
        SDL_GamepadAxis axis;
        const char* name;
    };
    const One buttons[] = {
        {&Held::a, sb::a, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_AXIS_INVALID, "A"},
        {&Held::b, sb::b, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_AXIS_INVALID, "B"},
        {&Held::x, sb::x, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_AXIS_INVALID, "X"},
        {&Held::y, sb::y, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_AXIS_INVALID, "Y"},
        {&Held::l, sb::l, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_AXIS_INVALID, "L"},
        {&Held::r, sb::r, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_AXIS_INVALID, "R"},
        {&Held::zl, sb::zl, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "ZL"},
        {&Held::zr, sb::zr, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "ZR"},
        {&Held::minus, sb::minus, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_AXIS_INVALID, "Minus"},
        {&Held::plus, sb::plus, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_AXIS_INVALID, "Plus"},
        {&Held::home, sb::home, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_AXIS_INVALID, "Home"},
        {&Held::capture, sb::capture, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_AXIS_INVALID, "Capture"},
        {&Held::c, sb::c, SDL_GAMEPAD_BUTTON_MISC2, SDL_GAMEPAD_AXIS_INVALID, "C"},
        {&Held::gl, sb::gl, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_AXIS_INVALID, "GL"},
        {&Held::gr, sb::gr, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_AXIS_INVALID, "GR"},
        {&Held::ls, sb::left_stick, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_AXIS_INVALID, "left stick"},
        {&Held::rs, sb::right_stick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_AXIS_INVALID, "right stick"},
        {&Held::up, sb::up, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_AXIS_INVALID, "up"},
        {&Held::down, sb::down, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_AXIS_INVALID, "down"},
        {&Held::left, sb::left, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_AXIS_INVALID, "left"},
        {&Held::right, sb::right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_AXIS_INVALID, "right"},
    };
    Switch2Calibration calibration;
    for (const auto& one : buttons) {
        Held held;
        held.*one.field = true;
        for (const auto& bytes : {report05(held), report09(held)}) {
            const auto parsed = parse_switch2_report(bytes.data(), bytes.size());
            check(parsed.has_value(), std::string("parsed: ") + one.name);
            check(parsed->buttons == one.bit, std::string("only ") + one.name + " (report 0x" + std::to_string(bytes[0]) + ")");
            const auto pad = switch2_pad_state(*parsed, calibration);
            int pressed = 0;
            for (bool b : pad.buttons) pressed += b ? 1 : 0;
            if (one.sdl != SDL_GAMEPAD_BUTTON_INVALID)
                check(pressed == 1 && pad.buttons[std::size_t(one.sdl)], std::string("the pad's button for ") + one.name);
            else
                check(pressed == 0 && pad.axes[std::size_t(one.axis)] == 32767, std::string("a full trigger for ") + one.name);
        }
    }
    // Sticks, packed as documented.
    Held sticks;
    sticks.lx = 1971, sticks.ly = 2100, sticks.rx = 0x123, sticks.ry = 0xfed;
    for (const auto& bytes : {report05(sticks), report09(sticks)}) {
        const auto parsed = parse_switch2_report(bytes.data(), bytes.size());
        check(parsed && parsed->left_x == 1971 && parsed->left_y == 2100 && parsed->right_x == 0x123 && parsed->right_y == 0xfed,
              "the sticks' 12-bit values");
    }
    // One report written out as a capture shows it: report 0x05, timer
    // 0x000061a8, A and ZR held, the left and right sticks at the measured rest.
    const std::uint8_t captured[64] = {0x05, 0xa8, 0x61, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb3, 0x47, 0x83, 0x60, 0xe8, 0x84};
    const auto parsed = parse_switch2_report(captured, sizeof captured);
    check(parsed && parsed->report_id == 0x05 && parsed->buttons == (sb::a | sb::zr), "the capture: A and ZR");
    check(parsed->left_x == 1971 && parsed->left_y == 2100 && parsed->right_x == 2144 && parsed->right_y == 2126, "the capture's sticks");
    // Not ours: another report, or one too short.
    std::uint8_t other[64]{0x30};
    check(!parse_switch2_report(other, 64), "report 0x30 is none of these");
    check(!parse_switch2_report(captured, 16) && parse_switch2_report(captured, 17), "0x05 needs its 17 bytes");
    const auto nine = report09({});
    check(!parse_switch2_report(nine.data(), 11) && parse_switch2_report(nine.data(), 12), "0x09 needs its 12 bytes");
    check(!parse_switch2_report(nullptr, 64) && !parse_switch2_report(captured, 0), "nothing");
}

// ---- A fake controller and platform

struct FakeController {
    std::mutex mutex;
    std::string id = "USB\\VID_057E&PID_2069\\FAKE1";
    Switch2IoResult input_open{Switch2Io::ok, 0, 0};
    Switch2IoResult command_open{Switch2Io::ok, 0, 0};
    UsbDriver command_driver = UsbDriver::winusb;
    bool command_guid = true;
    bool input_listed = true;
    bool replies = true;
    int fail_send = -1;                 // the n-th send (0-based) fails
    Switch2IoResult fail_result{Switch2Io::failed, 31, 0};
    std::optional<Switch2IoResult> read_error_once; // the next read fails so (an aborted read over standby)
    bool report_after_start = true;     // reports after 03/0D
    bool reports_by_themselves = false; // another program started it
    bool stall_until_restart = false;   // reports stop until the next 03/0D
    std::uint8_t report_id = 0x05;
    bool unplugged = false;
    bool pace = false; // (the reader's threads: a report every 2 ms, not as fast as a loop can ask)
    milliseconds slow_receive{0}; // each reply read takes this long (a controller slow to answer)
    // Closing the HID input waits while this holds (a device thread that has
    // not ended yet when the controller is listed again).
    bool hold_close = false;
    std::condition_variable gate;
    Held held;
    std::map<std::uint32_t, std::array<std::uint8_t, 0x40>> flash;
    // What happened.
    std::vector<std::vector<std::uint8_t>> sent;
    std::deque<std::uint8_t> reply_bytes;
    bool started = false;
    int reports = 0;
    std::uint32_t timer = 0;

    Switch2Found found() {
        std::lock_guard lock(mutex);
        Switch2Found f;
        f.id = id;
        f.input_node = true;
        f.input_service = "HidUsb";
        f.input_path = input_listed ? "\\\\?\\hid#vid_057e&pid_2069&mi_00#fake#{4d1e55b2-f16f-11cf-88cb-001111000030}" : "";
        f.command_node = command_driver != UsbDriver::unknown;
        f.command_driver = command_driver;
        f.command_service = command_driver == UsbDriver::winusb ? "WINUSB" : command_driver == UsbDriver::other ? "libusbK" : "";
        f.command_guid = command_guid;
        f.command_path = command_driver == UsbDriver::winusb && command_guid
                             ? "\\\\?\\usb#vid_057e&pid_2069&mi_01#fake#{6f13725e-ef0e-4fd3-ae5f-b2de989ec825}"
                             : "";
        return f;
    }
};

class FakeInput final : public Switch2Input {
public:
    explicit FakeInput(FakeController& c) : c_(c) {}
    ~FakeInput() override {
        std::unique_lock lock(c_.mutex);
        c_.gate.wait(lock, [&] { return !c_.hold_close; });
    }
    Switch2IoResult read(std::uint8_t* buffer, std::size_t size, milliseconds) override {
        bool pace = false;
        {
            std::lock_guard lock(c_.mutex);
            pace = c_.pace;
        }
        if (pace) std::this_thread::sleep_for(milliseconds(2));
        std::lock_guard lock(c_.mutex);
        if (c_.unplugged) return {Switch2Io::gone, 1167, 0};
        if (c_.read_error_once) return *std::exchange(c_.read_error_once, std::nullopt);
        const bool flowing = (c_.started && c_.report_after_start && !c_.stall_until_restart) || c_.reports_by_themselves;
        if (!flowing) return {Switch2Io::timeout, 0, 0};
        c_.timer += 4;
        auto bytes = c_.report_id == 0x09 ? report09(c_.held) : report05(c_.held, c_.timer);
        bytes[0] = c_.report_id;
        const std::size_t n = std::min(size, bytes.size());
        std::copy(bytes.begin(), bytes.begin() + n, buffer);
        ++c_.reports;
        return {Switch2Io::ok, 0, n};
    }
    std::string describe() const override { return "fake HID, input reports 64 bytes"; }

private:
    FakeController& c_;
};

class FakeCommands final : public Switch2Commands {
public:
    explicit FakeCommands(FakeController& c) : c_(c) {}
    Switch2IoResult send(const std::uint8_t* data, std::size_t size) override {
        std::lock_guard lock(c_.mutex);
        if (c_.unplugged) return {Switch2Io::gone, 22, 0};
        const int index = int(c_.sent.size());
        c_.sent.emplace_back(data, data + size);
        if (index == c_.fail_send) return c_.fail_result;
        if (size >= 8 && data[0] == 0x03 && data[3] == 0x0d) {
            c_.started = true;
            c_.stall_until_restart = false;
        }
        if (!c_.replies) return {Switch2Io::ok, 0, size};
        // The reply: the command and subcommand, 0x01, an acknowledgement.
        std::vector<std::uint8_t> reply{data[0], 0x01, 0x00, data[3], 0x00, 0xf8, 0x00, 0x00};
        if (data[0] == 0x02 && data[3] == 0x01) {
            const std::uint32_t address = std::uint32_t(data[12]) | std::uint32_t(data[13]) << 8 | std::uint32_t(data[14]) << 16 |
                                          std::uint32_t(data[15]) << 24;
            reply.resize(0x10, 0);
            const auto block = c_.flash.count(address) ? c_.flash[address] : std::array<std::uint8_t, 0x40>{};
            reply.insert(reply.end(), block.begin(), block.end());
        } else {
            reply.insert(reply.end(), {0x01, 0x00, 0x00, 0x00});
        }
        c_.reply_bytes.insert(c_.reply_bytes.end(), reply.begin(), reply.end());
        return {Switch2Io::ok, 0, size};
    }
    Switch2IoResult receive(std::uint8_t* buffer, std::size_t size, milliseconds) override {
        milliseconds slow{0};
        {
            std::lock_guard lock(c_.mutex);
            slow = c_.slow_receive;
        }
        if (slow.count()) std::this_thread::sleep_for(slow);
        std::lock_guard lock(c_.mutex);
        if (c_.unplugged) return {Switch2Io::gone, 22, 0};
        if (c_.reply_bytes.empty()) return {Switch2Io::timeout, 121, 0};
        std::size_t n = 0;
        while (n < size && !c_.reply_bytes.empty()) {
            buffer[n++] = c_.reply_bytes.front();
            c_.reply_bytes.pop_front();
        }
        return {Switch2Io::ok, 0, n};
    }
    void flush() override {
        std::lock_guard lock(c_.mutex);
        c_.reply_bytes.clear();
    }
    std::string describe() const override { return "fake WinUSB, bulk out 0x02, bulk in 0x82"; }

private:
    FakeController& c_;
};

class FakePlatform final : public Switch2Platform {
public:
    std::mutex mutex;
    std::vector<FakeController*> plugged;
    std::vector<Switch2Found> scan() override {
        std::vector<FakeController*> now;
        {
            std::lock_guard lock(mutex);
            now = plugged;
        }
        std::vector<Switch2Found> out;
        for (auto* c : now) out.push_back(c->found());
        return out;
    }
    FakeController* find(const Switch2Found& found) {
        std::lock_guard lock(mutex);
        for (auto* c : plugged)
            if (c->id == found.id) return c;
        return nullptr;
    }
    std::unique_ptr<Switch2Input> open_input(const Switch2Found& found, Switch2IoResult& result) override {
        FakeController* c = find(found);
        if (!c) {
            result = {Switch2Io::gone, 2, 0};
            return nullptr;
        }
        std::lock_guard lock(c->mutex);
        result = c->input_open;
        return result.io == Switch2Io::ok ? std::make_unique<FakeInput>(*c) : nullptr;
    }
    std::unique_ptr<Switch2Commands> open_commands(const Switch2Found& found, Switch2IoResult& result) override {
        FakeController* c = find(found);
        if (!c) {
            result = {Switch2Io::gone, 2, 0};
            return nullptr;
        }
        std::lock_guard lock(c->mutex);
        result = c->command_open;
        return result.io == Switch2Io::ok ? std::make_unique<FakeCommands>(*c) : nullptr;
    }
    void plug(FakeController& c) {
        std::lock_guard lock(mutex);
        plugged.push_back(&c);
    }
    // (Off Windows' list first, then its reads fail: a look the failure asks
    // for no longer finds it.)
    void unplug(FakeController& c) {
        {
            std::lock_guard lock(mutex);
            plugged.erase(std::remove(plugged.begin(), plugged.end(), &c), plugged.end());
        }
        std::lock_guard lock(c.mutex);
        c.unplugged = true;
    }
};

void fill_flash(FakeController& c) {
    std::array<std::uint8_t, 0x40> left{}, right{}, empty{};
    std::copy(kLeftMeasured.begin(), kLeftMeasured.end(), left.begin() + 0x28);
    std::copy(kRightMeasured.begin(), kRightMeasured.end(), right.begin() + 0x28);
    empty.fill(0xff);
    c.flash[kSwitch2FlashLeftFactory] = left;
    c.flash[kSwitch2FlashRightFactory] = right;
    c.flash[kSwitch2FlashLeftUser] = empty;
    c.flash[kSwitch2FlashRightUser] = empty;
}

// Steps a machine with the test's clock (4 ms a step: the controller's rate).
struct Driver {
    Switch2Machine& machine;
    Clock::time_point now = Clock::time_point{} + std::chrono::hours(1);
    std::vector<std::string> log;
    void steps(int count, milliseconds each = milliseconds(4)) {
        for (int i = 0; i < count; ++i) {
            machine.step(now);
            now += each;
            for (auto& line : machine.take_log()) log.push_back(std::move(line));
        }
    }
    void until(Switch2State state, int limit, milliseconds each = milliseconds(4)) {
        for (int i = 0; i < limit && machine.status().state != state; ++i) steps(1, each);
        check(machine.status().state == state, std::string("reached ") + switch2_state_name(state) + ", is " + switch2_state_name(machine.status().state) +
                                                   "\n" + joined(log));
    }
};

// ---- The state machine

void machine_start_and_read() {
    FakePlatform platform;
    FakeController c;
    fill_flash(c);
    c.held.a = true;
    c.held.lx = 1971 + 1654; // the left stick fully right
    c.held.ly = 2100;
    platform.plug(c);
    Switch2Machine machine(platform, c.found());
    Driver d{machine};
    d.until(Switch2State::running, 10);
    const auto& log = d.log;
    check(has_line(log, "switch 2 pro: input: opened \\\\?\\hid#vid_057e&pid_2069&mi_00"), "the log: the HID input opened\n" + joined(log));
    check(has_line(log, "switch 2 pro: interface 1: opened (fake WinUSB"), "the log: interface 1 opened");
    check(has_line(log, "flash 0x013080 (left stick, factory): ok, rest 1971,2100 travel +1654,+1553 -1582,-1606"), "the log: the left stick's flash");
    check(has_line(log, "flash 0x1fc040 (left stick, user): ok, none stored"), "the log: no user calibration");
    check(has_line(log, "sticks: left factory, right factory calibration"), "the log: the calibration used");
    check(has_line(log, "start 1/10 07/01 (unknown purpose): reply 07 01 00 01 00 f8 00 00"), "the log: each start command's reply");
    check(has_line(log, "start 10/10 03/0D start output: reply 03 01 00 0d 00 f8 00 00"), "the log: the last one");
    check(has_line(log, "first input report: id 0x05, 64 bytes, 4 ms after the start: 05 04 00 00 00 08"), "the log: the first report\n" + joined(log));
    // What went out: five flash reads, then SDL's ten commands in order.
    {
        std::lock_guard lock(c.mutex);
        check(c.sent.size() == 15, "15 commands: " + std::to_string(c.sent.size()));
        check(c.sent[0] == switch2_flash_read_command(kSwitch2FlashSerial) && c.sent[1] == switch2_flash_read_command(kSwitch2FlashLeftFactory) &&
                  c.sent[4] == switch2_flash_read_command(kSwitch2FlashRightUser),
              "the flash reads first");
        const auto start = switch2_start_commands();
        for (std::size_t i = 0; i < start.size(); ++i) check(c.sent[5 + i] == start[i].bytes, std::string("then ") + start[i].name);
    }
    // The controls with the calibration.
    check(machine.controls() && (machine.controls()->buttons & switch2_button::a), "A held");
    const auto pad = switch2_pad_state(*machine.controls(), machine.calibration());
    check(pad.buttons[SDL_GAMEPAD_BUTTON_EAST] && pad.axes[SDL_GAMEPAD_AXIS_LEFTX] == 32767 && pad.axes[SDL_GAMEPAD_AXIS_LEFTY] == -1,
          "A on EAST, the stick fully right with its calibration");
    // The rate: one report each 4 ms is 250 a second.
    d.steps(600);
    check(has_line(d.log, "report rate: 250 per second (id 0x05, 64 bytes)"), "the log: the report rate\n" + joined(d.log));
    check(machine.status().rate > 249 && machine.status().rate < 251, "the status' rate");
    check(machine.fresh(d.now), "fresh");
    // Player lights: once the host names the player.
    machine.set_player(1);
    d.steps(2);
    {
        std::lock_guard lock(c.mutex);
        check(c.sent.back() == switch2_player_led_command(0x3), "player 2's lights sent");
    }
    check(has_line(d.log, "player lights of player 2: reply 09 01 00 07"), "the log: the lights");
    const std::size_t sent = c.sent.size();
    d.steps(20);
    check(c.sent.size() == sent, "the lights once");
}

void machine_stall_unplug() {
    FakePlatform platform;
    FakeController c;
    fill_flash(c);
    platform.plug(c);
    Switch2Machine machine(platform, c.found());
    Driver d{machine};
    d.until(Switch2State::running, 10);
    // The reports stop until the controller is started again.
    {
        std::lock_guard lock(c.mutex);
        c.stall_until_restart = true;
    }
    d.steps(10, milliseconds(100));
    check(machine.status().state == Switch2State::running && !machine.fresh(d.now), "a stall: still running, but at rest");
    d.steps(1, milliseconds(100));
    check(machine.status().state == Switch2State::waiting, "a second without reports: started again, waiting");
    {
        std::lock_guard lock(c.mutex);
        check(c.sent.size() == 25 && c.sent.back() == switch2_start_commands().back().bytes, "the start commands went out again at once");
    }
    d.until(Switch2State::running, 20, milliseconds(100));
    check(has_line(d.log, "input reports stopped for 1"), "the log: the stall\n" + joined(d.log));
    check(has_line(d.log, "input reports again (id 0x05)"), "the log: back");
    {
        std::lock_guard lock(c.mutex);
        check(c.sent.size() == 25, "started again without the flash reads: " + std::to_string(c.sent.size()));
    }
    // Unplugged: the next read fails; Windows' list decides that it is gone.
    check(!machine.take_scan_request(), "no look at the list asked for while it plays");
    platform.unplug(c);
    d.steps(1);
    check(machine.status().state == Switch2State::opening && !machine.status().commands, "a lost read: both closed, opening again");
    check(machine.take_scan_request() && !machine.take_scan_request(), "a look at the list asked for, once");
    check(has_line(d.log, "input: lost the controller (gone (error 1167)); opening it again in a second unless Windows no longer lists it"),
          "the log: lost\n" + joined(d.log));
    d.steps(3);
    check(machine.status().state == Switch2State::opening, "a second's pause before opening again");
    machine.listed(std::nullopt);
    check(machine.status().state == Switch2State::gone, "no longer listed: gone");
    check(has_line(machine.take_log(), "unplugged (Windows no longer lists it)"), "the log: unplugged");
    d.steps(30, milliseconds(100));
    check(machine.status().state == Switch2State::gone, "stays gone");
    // Gone from Windows' list instead.
    FakeController c2;
    c2.id = "USB\\VID_057E&PID_2069\\SECOND";
    platform.plug(c2);
    Switch2Machine second(platform, c2.found());
    Driver d2{second};
    d2.until(Switch2State::running, 10);
    second.listed(std::nullopt);
    check(second.status().state == Switch2State::gone, "no longer listed: gone");
    check(has_line(second.take_log(), "unplugged (Windows no longer lists it)"), "the log: no longer listed");
}

std::size_t count_lines(const std::vector<std::string>& lines, const std::string& part) {
    return std::size_t(std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; }));
}

void machine_lost_channel() {
    // A read fails as if the controller were unplugged (an aborted read over
    // standby: ERROR_OPERATION_ABORTED) while Windows still lists it: both
    // channels open again a second later and it plays on.
    FakePlatform platform;
    FakeController c;
    fill_flash(c);
    platform.plug(c);
    Switch2Machine machine(platform, c.found());
    Driver d{machine};
    d.until(Switch2State::running, 10);
    std::size_t sent = 0;
    {
        std::lock_guard lock(c.mutex);
        sent = c.sent.size();
        c.read_error_once = Switch2IoResult{Switch2Io::gone, 995, 0};
    }
    d.steps(1);
    check(machine.status().state == Switch2State::opening && !machine.status().commands && !machine.fresh(d.now),
          "lost: both channels closed, the pad at rest");
    check(machine.take_scan_request(), "a look at the list asked for");
    check(!machine.status().started, "not started (until its start commands go out again)");
    check(has_line(d.log, "input: lost the controller (gone (error 995))"), "the log: lost\n" + joined(d.log));
    d.steps(10, milliseconds(50));
    check(machine.status().state == Switch2State::opening && count_lines(d.log, "input: opened") == 1, "a second's pause");
    machine.listed(c.found()); // (the look: still listed, nothing new)
    d.until(Switch2State::running, 30, milliseconds(100));
    check(count_lines(d.log, "input: opened") == 2 && count_lines(d.log, "interface 1: opened") == 2, "both opened again\n" + joined(d.log));
    check(has_line(d.log, "input reports again (id 0x05)") && machine.status().started, "started again, playing");
    {
        std::lock_guard lock(c.mutex);
        check(c.sent.size() == sent + 10 && c.sent.back() == switch2_start_commands().back().bytes, "the ten start commands again, no flash read");
    }
    check(machine.calibration().left_source == Switch2CalibrationSource::factory, "the calibration kept");

    // A start command fails as if unplugged: the same.
    FakeController g;
    g.id = "USB\\VID_057E&PID_2069\\LOSTSTART";
    g.fail_send = 7;
    g.fail_result = {Switch2Io::gone, 22, 0};
    platform.plug(g);
    Switch2Machine starting(platform, g.found());
    Driver s{starting};
    s.steps(3);
    check(starting.status().state == Switch2State::opening && starting.take_scan_request(), "lost while starting: opening again");
    check(has_line(s.log, "start: start 3/10 11/01 (unknown purpose): lost the controller (gone (error 22))"), "the log: the step\n" + joined(s.log));
    s.until(Switch2State::running, 40, milliseconds(100));

    // Replies an earlier owner left unread in interface 1's pipe are dropped
    // before the start: every reply read is the command's own.
    FakeController stale;
    stale.id = "USB\\VID_057E&PID_2069\\STALE";
    fill_flash(stale);
    std::vector<std::uint8_t> left_over{0x03, 0x01, 0x00, 0x0d, 0x00, 0xf8, 0x00, 0x00};
    left_over.resize(64, 0x5a);
    stale.reply_bytes.assign(left_over.begin(), left_over.end());
    platform.plug(stale);
    Switch2Machine flushed(platform, stale.found());
    Driver f{flushed};
    f.until(Switch2State::running, 10);
    check(has_line(f.log, "sticks: left factory, right factory calibration"), "the calibration read right\n" + joined(f.log));
    check(!has_line(f.log, "not its reply") && !has_line(f.log, "unexpected reply"), "every reply the command's own\n" + joined(f.log));
}

void machine_other_program() {
    // Another program holds interface 1, and nothing comes by itself.
    FakePlatform platform;
    FakeController c;
    c.command_open = {Switch2Io::busy, 5, 0};
    platform.plug(c);
    Switch2Machine machine(platform, c.found());
    Driver d{machine};
    d.until(Switch2State::no_commands, 30, milliseconds(100));
    check(has_line(d.log, "interface 1: cannot open it: busy (error 5) (another program holds it"), "the log: busy\n" + joined(d.log));
    check(has_line(d.log, "no input report came by itself"), "the log: nothing came");
    check(machine.status().command_open.io == Switch2Io::busy && !machine.status().commands, "the status: busy");
    // It tries again every two seconds and logs a repeat once only.
    d.steps(50, milliseconds(100));
    check(std::count_if(d.log.begin(), d.log.end(), [](const std::string& l) { return l.find("cannot open it") != std::string::npos; }) == 1,
          "the same failure logged once");
    // That program started it: its reports arrive all the same.
    {
        std::lock_guard lock(c.mutex);
        c.reports_by_themselves = true;
    }
    d.until(Switch2State::running, 5);
    check(has_line(d.log, "(started by another program)"), "the log: started by another program");
    check(!machine.status().started, "not started by this reader");
    // The program lets interface 1 go and the reports stop: started here.
    {
        std::lock_guard lock(c.mutex);
        c.reports_by_themselves = false;
        c.command_open = {Switch2Io::ok, 0, 0};
    }
    d.until(Switch2State::waiting, 30, milliseconds(100));
    d.until(Switch2State::no_commands, 30, milliseconds(100));
    d.until(Switch2State::running, 40, milliseconds(100));
    check(machine.status().started && machine.status().commands, "started by this reader once interface 1 was free");
}

void machine_driver_states() {
    struct Case {
        UsbDriver driver;
        bool guid;
        const char* expect;
    };
    const Case cases[] = {{UsbDriver::none, true, "interface 1: nothing to open (no driver)"},
                          {UsbDriver::other, true, "interface 1: nothing to open (libusbK)"},
                          {UsbDriver::winusb, false, "interface 1: nothing to open (WINUSB, no device interface GUID)"},
                          {UsbDriver::unknown, false, "interface 1: nothing to open (not listed)"}};
    for (const auto& one : cases) {
        FakePlatform platform;
        FakeController c;
        c.command_driver = one.driver;
        c.command_guid = one.guid;
        platform.plug(c);
        Switch2Machine machine(platform, c.found());
        Driver d{machine};
        d.until(Switch2State::no_commands, 30, milliseconds(100));
        check(has_line(d.log, one.expect), std::string("the log: ") + one.expect + "\n" + joined(d.log));
        check(machine.status().found.command_driver == one.driver, "the status keeps the driver state");
    }
    // No HID input listed yet (Windows registers it a moment later): it
    // looks again and plays once it is there.
    FakePlatform platform;
    FakeController c;
    c.input_listed = false;
    platform.plug(c);
    Switch2Machine machine(platform, c.found());
    Driver d{machine};
    d.steps(3);
    check(machine.status().state == Switch2State::no_input, "no HID input listed: no_input");
    check(has_line(d.log, "input: Windows lists no HID input device for interface 0 (its driver: HidUsb); looking again"), "the log: no HID yet");
    {
        std::lock_guard lock(c.mutex);
        c.input_listed = true;
    }
    machine.listed(c.found());
    d.until(Switch2State::running, 10);
    check(has_line(d.log, "listed now on USB"), "the log: listed again");
    // The HID input listed but not to be opened.
    FakeController e;
    e.id = "USB\\VID_057E&PID_2069\\FAILING";
    e.input_open = {Switch2Io::failed, 87, 0};
    platform.plug(e);
    Switch2Machine failing(platform, e.found(), 2);
    Driver f{failing};
    f.steps(2);
    check(failing.status().state == Switch2State::no_input && failing.status().input_open.error == 87, "the open's error");
    check(has_line(f.log, "switch 2 pro 2: input: cannot open"), "the second controller's log prefix\n" + joined(f.log));
}

void machine_start_failures() {
    // A start command cannot be sent: interface 1 closed, opened again.
    FakePlatform platform;
    FakeController c;
    c.fail_send = 7; // (the third start command)
    platform.plug(c);
    Switch2Machine machine(platform, c.found());
    Driver d{machine};
    d.steps(3);
    check(machine.status().failed_step == "start 3/10 11/01 (unknown purpose)" && machine.status().failed_result.error == 31, "the failed step");
    check(has_line(d.log, "start 3/10 11/01 (unknown purpose) could not be sent: failed (error 31)"), "the log: the failed step\n" + joined(d.log));
    d.until(Switch2State::running, 60, milliseconds(100));
    check(machine.status().failed_step.empty(), "started on the next try");
    // No report after the start: three starts, then no_reports, then a try
    // every ten seconds that logs one line.
    FakeController quiet;
    quiet.id = "USB\\VID_057E&PID_2069\\QUIET";
    quiet.report_after_start = false;
    platform.plug(quiet);
    Switch2Machine silent(platform, quiet.found());
    Driver s{silent};
    s.until(Switch2State::no_reports, 100, milliseconds(100));
    check(silent.status().starts == Switch2Machine::kStartTries, "three starts");
    check(has_line(s.log, "starting it again (2/3)") && has_line(s.log, "starting it again (3/3)") && has_line(s.log, "no input report after 3 starts"),
          "the log: the tries\n" + joined(s.log));
    const std::size_t lines = s.log.size();
    s.steps(110, milliseconds(100));
    check(has_line(s.log, "start sequence sent again: 10 of 10 commands answered"), "a later try: one line");
    check(s.log.size() - lines <= 3, "the later tries stay short: " + std::to_string(s.log.size() - lines));
    // Reports of another kind only: not playing.
    FakeController odd;
    odd.id = "USB\\VID_057E&PID_2069\\ODD";
    odd.report_id = 0x30;
    platform.plug(odd);
    Switch2Machine other(platform, odd.found());
    Driver o{other};
    o.steps(30);
    check(other.status().state == Switch2State::waiting && !other.controls(), "report 0x30 does not play");
    check(has_line(o.log, "input report 0x30 (64 bytes) is not one this reader knows"), "the log: an unknown report\n" + joined(o.log));
    // Report 0x09 (another program's format): plays.
    FakeController pro;
    pro.id = "USB\\VID_057E&PID_2069\\PRO09";
    pro.report_id = 0x09;
    pro.held.b = true;
    platform.plug(pro);
    Switch2Machine nine(platform, pro.found());
    Driver n{nine};
    n.until(Switch2State::running, 10);
    check(nine.controls() && nine.controls()->buttons == switch2_button::b, "report 0x09 plays");
}

// ---- The whole reader with SDL's virtual pads

struct Host {
    std::vector<SDL_Gamepad*> pads;
    PromptTracker tracker;
    Settings settings;
    void pump() {
        SDL_UpdateJoysticks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) pad_slots_event(pads, tracker, event);
    }
    dw4::HostPadInput input(std::size_t slot) { return pad_input(settings, family_of(tracker, pads.at(slot)), pads.at(slot)); }
};

// Updates the reader and the host until `done` (5 s at most).
template <typename Done>
void settle(Switch2Reader& reader, Host& host, Done done, const std::string& what, milliseconds limit = milliseconds(5000)) {
    const auto until = Clock::now() + limit;
    while (Clock::now() < until) {
        reader.update();
        host.pump();
        if (done()) return;
        std::this_thread::sleep_for(milliseconds(5));
    }
    throw std::runtime_error("timed out: " + what);
}

void set_held(FakeController& c, const Held& held) {
    std::lock_guard lock(c.mutex);
    c.held = held;
}

void reader_end_to_end() {
    auto owned = std::make_unique<FakePlatform>();
    FakePlatform& platform = *owned;
    // (Every fake controller outlives the reader: its threads.)
    FakeController c, again, second;
    fill_flash(c);
    c.pace = true;
    platform.plug(c);
    Switch2Reader reader(std::move(owned), false);
    Host host;
    settle(reader, host, [&] { return host.pads.size() == 1; }, "a pad");
    SDL_Gamepad* pad = host.pads.front();
    const auto identity = pad_identity(pad);
    check(identity.vendor == 0x057e && identity.product == 0x2069 && identity.name == kSwitch2PadName, "the pad's ids and name");
    check(classify(identity) == PadFamily::switch2_pro && host.tracker.player_family(0) == PadFamily::switch2_pro, "family Switch 2 Pro");
    check(SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_A &&
              SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_B,
          "SDL's labels: A on EAST, B on SOUTH");
    check(reader.owns(SDL_GetGamepadID(pad)) && reader.joystick(1) == SDL_GetGamepadID(pad), "the reader's pad");
    check(host.settings.nintendo_layout == NintendoLayout::by_label, "by label by default");
    namespace pb = dw4::pad_button;
    auto held_input = [&](Held held) {
        set_held(c, held);
        dw4::HostPadInput in{};
        settle(reader, host, [&] {
            in = host.input(0);
            return in.buttons != 0 || in.stick.x != 0 || in.c_stick.x != 0;
        }, "the input arrives");
        set_held(c, {});
        settle(reader, host, [&] {
            const auto rest = host.input(0);
            return rest.buttons == 0 && std::abs(rest.stick.x) < 0.01f;
        }, "released");
        return in;
    };
    Held h;
    h.a = true;
    check(held_input(h).buttons == pb::a, "A is the game's A (by label)");
    h = {}, h.b = true;
    check(held_input(h).buttons == pb::b, "B is the game's B");
    h = {}, h.x = true;
    check(held_input(h).buttons == pb::x, "X is the game's X");
    h = {}, h.y = true;
    check(held_input(h).buttons == pb::y, "Y is the game's Y");
    h = {}, h.plus = true;
    check(held_input(h).buttons == pb::start, "Plus is START");
    h = {}, h.zr = true;
    check(held_input(h).buttons == pb::r, "ZR is the game's R");
    h = {}, h.zl = true;
    check(held_input(h).buttons == pb::l, "ZL is the game's L");
    h = {}, h.r = true;
    check(held_input(h).buttons == pb::z, "R is the game's Z");
    h = {}, h.lx = 1971 + 1654, h.ly = 2100;
    check(held_input(h).stick.x > 0.95f, "the left stick fully right (its calibration)");
    // F1 > Controls > Nintendo layout by position: the bottom button (B) is
    // the game's A, as on an Xbox pad; with the face bindings swapped
    // (a=b, b=a, x=y, y=x) by position plays by label again.
    host.settings.nintendo_layout = NintendoLayout::by_position;
    h = {}, h.b = true;
    check(held_input(h).buttons == pb::a, "by position: B (bottom) is the game's A");
    h = {}, h.a = true;
    check(held_input(h).buttons == pb::b, "by position: A (right) is the game's B");
    h = {}, h.y = true;
    check(held_input(h).buttons == pb::x, "by position: Y (left) is the game's X");
    auto bind = [&](PadKey key, SDL_GamepadButton button) {
        host.settings.pad[std::size_t(key)] = {};
        host.settings.pad[std::size_t(key)][0] = PadInput{PadInput::Kind::button, int(button)};
    };
    bind(PadKey::a, SDL_GAMEPAD_BUTTON_EAST), bind(PadKey::b, SDL_GAMEPAD_BUTTON_SOUTH);
    bind(PadKey::x, SDL_GAMEPAD_BUTTON_NORTH), bind(PadKey::y, SDL_GAMEPAD_BUTTON_WEST);
    for (const auto& [field, game, name] : {std::tuple{&Held::a, pb::a, "A"}, std::tuple{&Held::b, pb::b, "B"}, std::tuple{&Held::x, pb::x, "X"},
                                            std::tuple{&Held::y, pb::y, "Y"}}) {
        h = {};
        h.*field = true;
        check(held_input(h).buttons == game, std::string("by position, face bindings swapped: ") + name + " is the game's " + name);
    }
    host.settings = Settings{};
    // Reports stall (and do not come back after a restart): the pad goes
    // to rest rather than keep the held button; they come back: it plays.
    h = {}, h.a = true;
    set_held(c, h);
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "A held");
    {
        std::lock_guard lock(c.mutex);
        c.stall_until_restart = true;
        c.report_after_start = false;
    }
    settle(reader, host, [&] { return !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "stalled: the pad at rest");
    check(host.pads.size() == 1, "the pad stays through the stall");
    {
        std::lock_guard lock(c.mutex);
        c.report_after_start = true;
    }
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "started again: A held again");
    set_held(c, {});
    settle(reader, host, [&] { return !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "A let go");
    // C, GL, GR: SDL reports them; nothing by default.
    check(SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_MISC2) && SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1) &&
              SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1),
          "C, GL, GR are buttons of the pad");
    h = {}, h.c = true;
    set_held(c, h);
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_MISC2); }, "C");
    check(host.input(0).buttons == 0, "C plays nothing until bound");
    set_held(c, {});
    settle(reader, host, [&] { return !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_MISC2); }, "C released");
    // The player lights follow the pad's slot.
    reader.set_player(SDL_GetGamepadID(pad), 0);
    settle(reader, host, [&] {
        std::lock_guard lock(c.mutex);
        return !c.sent.empty() && c.sent.back() == switch2_player_led_command(0x1);
    }, "player 1's lights");
    // Unplugged: the pad goes; plugged in again: a new pad.
    platform.unplug(c);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.empty(); }, "the pad goes");
    check(reader.status().empty() || reader.status().front().state == Switch2State::gone, "the reader lets it go");
    fill_flash(again);
    again.pace = true;
    platform.plug(again);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.size() == 1; }, "plugged in again");
    check(reader.status().size() == 1 && reader.status().front().number == 1, "number 1 again");
    // A second controller: a second pad, the second player.
    second.id = "USB\\VID_057E&PID_2069\\FAKE2";
    second.pace = true;
    platform.plug(second);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.size() == 2; }, "two pads");
    const auto states = reader.status();
    check(states.size() == 2 && (states[0].number + states[1].number) == 3, "numbers 1 and 2");
    check(host.tracker.pads()[1].family == PadFamily::switch2_pro, "the second pad's family");
    auto lines = reader.take_log();
    check(has_line(lines, "switch 2 pro 2: found on USB (USB\\VID_057E&PID_2069\\FAKE2)"), "the log: the second one\n" + joined(lines));
    check(has_line(lines, "switch 2 pro: found on USB"), "the log: the first one");
    platform.unplug(second);
    platform.unplug(again);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.empty(); }, "both go");
    settle(reader, host, [&] { return has_line(reader.take_log(), "switch 2 pro: none on USB"); }, "the log: none on USB");
}

void reader_background() {
    // The game's window in the background: a hidden window without the
    // input focus (SDL then ignores every pad's presses and takes its
    // releases, as for SDL's own drivers' pads). The reader keeps reading and
    // keeps the pad; nothing sticks; back in front (no window without the
    // focus any more), the next press counts.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    check(SDL_InitSubSystem(SDL_INIT_VIDEO), std::string("video: ") + SDL_GetError());
    auto owned = std::make_unique<FakePlatform>();
    FakePlatform& platform = *owned;
    FakeController c;
    c.pace = true;
    platform.plug(c);
    Switch2Reader reader(std::move(owned), false);
    Host host;
    settle(reader, host, [&] { return host.pads.size() == 1; }, "a pad");
    SDL_Gamepad* pad = host.pads.front();
    // In front (no window: nothing without the focus): A held counts.
    Held h;
    h.a = true;
    set_held(c, h);
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "A in front");
    // The window goes to the background, A is let go there: SDL takes the release.
    SDL_Window* window = SDL_CreateWindow("dw4 switch2 test", 64, 64, SDL_WINDOW_HIDDEN);
    check(window != nullptr, std::string("a hidden window: ") + SDL_GetError());
    check(SDL_GetKeyboardFocus() == nullptr, "the hidden window has no focus: the game in the background");
    set_held(c, {});
    settle(reader, host, [&] { return !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "A let go in the background: nothing sticks");
    // B pressed in the background: ignored, while the reader goes on.
    h = {};
    h.b = true;
    set_held(c, h);
    int before = 0;
    {
        std::lock_guard lock(c.mutex);
        before = c.reports;
    }
    const auto until = Clock::now() + milliseconds(400);
    while (Clock::now() < until) {
        reader.update();
        host.pump();
        std::this_thread::sleep_for(milliseconds(5));
    }
    {
        std::lock_guard lock(c.mutex);
        check(c.reports > before + 10, "still reading in the background");
    }
    check(host.pads.size() == 1 && reader.status().front().state == Switch2State::running, "the pad stays, the controller started");
    check(!SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH), "SDL ignores the press in the background (as for every pad)");
    set_held(c, {});
    for (int i = 0; i < 20; ++i) {
        reader.update();
        host.pump();
        std::this_thread::sleep_for(milliseconds(5));
    }
    // Back in front: the next press counts.
    SDL_DestroyWindow(window);
    h = {};
    h.a = true;
    set_held(c, h);
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "back in front: A counts");
    platform.unplug(c);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.empty(); }, "unplugged");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
}

// The reader's lines so far (kept: settle() looks at them more than once).
struct Lines {
    std::vector<std::string> all;
    bool has(Switch2Reader& reader, const std::string& part) {
        for (auto& line : reader.take_log()) all.push_back(std::move(line));
        return has_line(all, part);
    }
};

constexpr milliseconds kNever{std::chrono::hours(1)}; // (no look at the list by the clock)

void reader_lost_channel() {
    // A read fails as if unplugged while Windows still lists the controller,
    // and nothing else happens (no device change, no look by the clock): the
    // reader looks at the list itself, the controller plays on, the pad and
    // its player slot stay.
    auto owned = std::make_unique<FakePlatform>();
    FakePlatform& platform = *owned;
    FakeController c;
    fill_flash(c);
    c.pace = true;
    platform.plug(c);
    Switch2Reader reader(std::move(owned), false, kNever);
    Host host;
    Lines lines;
    settle(reader, host, [&] { return host.pads.size() == 1; }, "a pad");
    const SDL_JoystickID id = SDL_GetGamepadID(host.pads.front());
    Held h;
    h.a = true;
    set_held(c, h);
    settle(reader, host, [&] { return SDL_GetGamepadButton(host.pads.front(), SDL_GAMEPAD_BUTTON_EAST); }, "A held");
    const std::uint32_t scans = reader.scans();
    {
        std::lock_guard lock(c.mutex);
        c.read_error_once = Switch2IoResult{Switch2Io::gone, 995, 0};
    }
    settle(reader, host, [&] { return lines.has(reader, "input: lost the controller (gone (error 995))"); }, "the read lost");
    check(host.pads.size() == 1 && SDL_GetGamepadID(host.pads.front()) == id, "the pad stays while it opens again");
    settle(reader, host, [&] { return lines.has(reader, "input reports again"); }, "playing again without being plugged in again");
    settle(reader, host, [&] { return SDL_GetGamepadButton(host.pads.front(), SDL_GAMEPAD_BUTTON_EAST); }, "A held again");
    check(host.pads.size() == 1 && SDL_GetGamepadID(host.pads.front()) == id && reader.joystick(1) == id, "the same pad, the same player slot");
    check(reader.scans() > scans, "the lost read asked for a look at the list");
    check(reader.status().size() == 1 && reader.status().front().state == Switch2State::running, "running");
    // Unplugged for real (off the list, its reads fail), and again no device
    // change: the look the failed read asks for finds it gone.
    platform.unplug(c);
    settle(reader, host, [&] { return host.pads.empty(); }, "unplugged: the pad goes");
    settle(reader, host, [&] { return lines.has(reader, "unplugged (Windows no longer lists it)"); }, "the log: unplugged");
}

void reader_rescan() {
    // Windows lists the HID input a moment after the controller and SDL
    // counts no device change for it: the look every two seconds while a
    // controller does not play finds it.
    auto owned = std::make_unique<FakePlatform>();
    FakePlatform& platform = *owned;
    FakeController c;
    c.pace = true;
    c.input_listed = false;
    platform.plug(c);
    Switch2Reader reader(std::move(owned), false);
    Host host;
    settle(reader, host, [&] { return reader.status().size() == 1 && reader.status().front().state == Switch2State::no_input; }, "no input yet");
    {
        std::lock_guard lock(c.mutex);
        c.input_listed = true;
    }
    settle(reader, host, [&] { return host.pads.size() == 1; }, "found by the look every two seconds", milliseconds(8000));
    platform.unplug(c);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.empty(); }, "unplugged");
}

void reader_replug_same_id() {
    // Unplugged and listed again under the same instance id before the old
    // controller's thread has ended (and before the main thread let it go):
    // a fresh controller, number 1, a new pad.
    auto owned = std::make_unique<FakePlatform>();
    FakePlatform& platform = *owned;
    FakeController c, again; // (outlive the reader: its threads)
    c.pace = true;
    again.id = c.id;
    again.pace = true;
    platform.plug(c);
    Switch2Reader reader(std::move(owned), false, kNever);
    // (Lets the old thread end before the reader joins it, also when a check fails.)
    struct Release {
        FakeController& c;
        void operator()() {
            {
                std::lock_guard lock(c.mutex);
                c.hold_close = false;
            }
            c.gate.notify_all();
        }
        ~Release() { (*this)(); }
    } release{c};
    Host host;
    settle(reader, host, [&] { return host.pads.size() == 1; }, "a pad");
    const SDL_JoystickID first = SDL_GetGamepadID(host.pads.front());
    {
        std::lock_guard lock(c.mutex);
        c.hold_close = true;
    }
    auto look = [&](const char* what) {
        const std::uint32_t before = reader.scans();
        reader.request_scan();
        const auto until = Clock::now() + std::chrono::seconds(5);
        while (reader.scans() == before && Clock::now() < until) std::this_thread::sleep_for(milliseconds(2));
        check(reader.scans() > before, std::string("a look at the list: ") + what);
    };
    platform.unplug(c);
    look("unplugged");
    check(reader.status().empty() && !reader.joystick(1), "no longer listed: no controller (its thread still ending)");
    platform.plug(again);
    look("listed again");
    const auto states = reader.status();
    check(states.size() == 1 && states.front().number == 1, "listed again at once: a fresh controller, number 1");
    // The old pad goes at once, also while the old thread still hangs.
    settle(reader, host, [&] {
        return std::none_of(host.pads.begin(), host.pads.end(), [&](SDL_Gamepad* p) { return SDL_GetGamepadID(p) == first; });
    }, "the old pad goes while its thread still ends");
    release();
    settle(reader, host, [&] { return host.pads.size() == 1 && SDL_GetGamepadID(host.pads.front()) != first; }, "a new pad");
    check(reader.joystick(1) == SDL_GetGamepadID(host.pads.front()), "number 1 is the new pad");
    platform.unplug(again);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.empty(); }, "unplugged");
}

void reader_slow_restart() {
    // The reports stall and the restart takes long (a controller slow to
    // answer: its thread is busy and still says running): the pad goes to
    // rest a second after the last report all the same.
    auto owned = std::make_unique<FakePlatform>();
    FakePlatform& platform = *owned;
    FakeController c;
    c.pace = true;
    platform.plug(c);
    Switch2Reader reader(std::move(owned), false, kNever);
    Host host;
    settle(reader, host, [&] { return host.pads.size() == 1; }, "a pad");
    SDL_Gamepad* pad = host.pads.front();
    Held h;
    h.a = true;
    set_held(c, h);
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "A held");
    {
        std::lock_guard lock(c.mutex);
        c.slow_receive = milliseconds(300); // (ten start commands: three seconds)
        c.stall_until_restart = true;
    }
    const auto stalled = Clock::now();
    settle(reader, host, [&] { return !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "at rest");
    const auto rest = std::chrono::duration_cast<milliseconds>(Clock::now() - stalled);
    check(rest >= Switch2Machine::kStall - milliseconds(50) && rest < Switch2Machine::kStall + milliseconds(1200),
          "at rest a second after the last report, not when the slow restart ends: " + std::to_string(rest.count()) + " ms");
    {
        std::lock_guard lock(c.mutex);
        c.slow_receive = milliseconds(0);
    }
    settle(reader, host, [&] { return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST); }, "started again: A held again", milliseconds(8000));
    set_held(c, {});
    platform.unplug(c);
    reader.request_scan();
    settle(reader, host, [&] { return host.pads.empty(); }, "unplugged");
}

void error_codes() {
    // Windows' error codes (winerror.h) as the reader takes them.
    for (const std::uint32_t timeout : {121u, 258u, 1460u})
        check(switch2_io_of_error(timeout) == Switch2Io::timeout, "a timeout: " + std::to_string(timeout) + " (121: WinUSB's pipe timeout)");
    for (const std::uint32_t busy : {5u, 32u, 170u})
        check(switch2_io_of_error(busy) == Switch2Io::busy, "another program: " + std::to_string(busy) + " (access denied, sharing violation)");
    for (const std::uint32_t gone : {2u, 3u, 22u, 55u, 433u, 995u, 1167u, 1617u})
        check(switch2_io_of_error(gone) == Switch2Io::gone, "as if unplugged: " + std::to_string(gone));
    for (const std::uint32_t failed : {0u, 1u, 6u, 31u, 87u, 1784u})
        check(switch2_io_of_error(failed) == Switch2Io::failed, "anything else: " + std::to_string(failed));
}

void windows_platform() {
    // Windows' own platform on this PC: its look at the device list opens
    // nothing (a controller that happens to be plugged in stays untouched),
    // and opening paths that are not there says so.
    auto platform = make_windows_switch2_platform();
#ifdef _WIN32
    check(platform != nullptr, "the Windows platform");
    for (const auto& found : platform->scan()) {
        std::string id = found.id;
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        check(id.rfind("USB\\VID_057E&PID_2069\\", 0) == 0, "only Switch 2 Pro Controllers: " + found.id);
        std::cout << "(a Switch 2 Pro Controller is connected: " << switch2_found_line(found) << ")\n";
    }
    Switch2Found missing;
    missing.id = "USB\\VID_057E&PID_2069\\MISSING";
    missing.input_path = "\\\\?\\hid#vid_057e&pid_2069&mi_00#dw4-test-missing#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    missing.command_path = "\\\\?\\usb#vid_057e&pid_2069&mi_01#dw4-test-missing#{6f13725e-ef0e-4fd3-ae5f-b2de989ec825}";
    Switch2IoResult result;
    check(!platform->open_input(missing, result) && result.io != Switch2Io::ok && result.error != 0,
          "a HID path that is not there: " + switch2_io_text(result));
    check(!platform->open_commands(missing, result) && result.io != Switch2Io::ok && result.error != 0,
          "a WinUSB path that is not there: " + switch2_io_text(result));
#else
    check(platform == nullptr, "no platform elsewhere");
#endif
}

void reader_without_platform() {
    // Off Windows (or without a platform): nothing happens, nothing breaks.
    Switch2Reader reader(nullptr, false);
    reader.request_scan();
    reader.update();
    check(reader.status().empty() && reader.take_log().empty() && reader.scans() == 0, "no platform: idle");
}

} // namespace

int main() {
    int result = 0;
    try {
        commands();
        replies();
        calibration();
        stick_arithmetic();
        reports();
        error_codes();
        machine_start_and_read();
        machine_stall_unplug();
        machine_lost_channel();
        machine_other_program();
        machine_driver_states();
        machine_start_failures();
        windows_platform();
        // No real controller of this PC takes part (every backend but the
        // virtual one off), and no visible window.
        for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                                 SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_HIDAPI_LIBUSB})
            SDL_SetHint(hint, "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        check(SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS), std::string("SDL_Init: ") + SDL_GetError());
        int connected = 0;
        SDL_free(SDL_GetJoysticks(&connected));
        check(connected == 0, "no real controller reaches the test");
        reader_without_platform();
        reader_end_to_end();
        reader_background();
        reader_lost_channel();
        reader_rescan();
        reader_replug_same_id();
        reader_slow_restart();
        SDL_Quit();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    if (!result) std::cout << checks << " Switch 2 Pro reader checks passed\n";
    return result;
}
