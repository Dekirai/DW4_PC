#include "switch2_pro.hpp"

// The protocol below follows SDL 3.4.16's SDL_hidapi_switch2.c:
//
//   Simple DirectMedia Layer
//   Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
//
//   This software is provided 'as-is', without any express or implied
//   warranty.  In no event will the authors be held liable for any damages
//   arising from the use of this software.
//
//   Permission is granted to anyone to use this software for any purpose,
//   including commercial applications, and to alter it and redistribute it
//   freely, subject to the following restrictions:
//
//   1. The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software
//      in a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//   3. This notice may not be removed or altered from any source distribution.
//
//   (SDL_hidapi_switch2.c: "Code and logic contributed by Valve Corporation
//   under the SDL zlib license.")
//
// Altered for this port (docs/ROUND_SW2WIN_NOTES.md): the start commands,
// the flash reads and the player lights as data with their replies checked
// and logged; the stick calibration with a plausibility check; report 0x09
// read as well (its layout from the procon2-mac project, MIT); Windows' HID
// and WinUSB in place of libusb; the reader's own state machine and threads.

#include <SDL3/SDL_hidapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <winusb.h>
#endif

namespace dw4::game {

// ---- Commands and replies

std::vector<Switch2Command> switch2_start_commands() {
    // SDL_hidapi_switch2.c HIDAPI_DriverSwitch2_InitUSB's init_sequence, in
    // its order and with its comments as names.
    return {
        {"07/01 (unknown purpose)", {0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}},
        {"0C/02 feature output mask 0x27", {0x0c, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00}},
        {"11/01 (unknown purpose)", {0x11, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}},
        {"0A/08 rumble data",
         {0x0a, 0x91, 0x00, 0x08, 0x00, 0x14, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {"0C/04 enable features 0x27", {0x0c, 0x91, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00}},
        {"01/0C (unknown purpose)", {0x01, 0x91, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00}},
        {"01/01 enable rumble", {0x01, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}},
        {"08/02 grip buttons", {0x08, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
        {"03/0A report format 0x05", {0x03, 0x91, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00}},
        {"03/0D start output", {0x03, 0x91, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    };
}

std::vector<std::uint8_t> switch2_flash_read_command(std::uint32_t address) {
    std::vector<std::uint8_t> command{0x02, 0x91, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    command[12] = std::uint8_t(address);
    command[13] = std::uint8_t(address >> 8);
    command[14] = std::uint8_t(address >> 16);
    command[15] = std::uint8_t(address >> 24);
    return command;
}

std::vector<std::uint8_t> switch2_player_led_command(std::uint8_t pattern) {
    std::vector<std::uint8_t> command{0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    command[8] = pattern;
    return command;
}

std::uint8_t switch2_player_pattern(int player) {
    static constexpr std::uint8_t kPatterns[] = {0x1, 0x3, 0x7, 0xf, 0x9, 0x5, 0xd, 0x6};
    return player < 0 ? 0 : kPatterns[player % 8];
}

std::optional<Switch2Reply> parse_switch2_reply(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 8) return std::nullopt;
    Switch2Reply reply;
    reply.command = data[0];
    reply.from_device = (data[1] & 0x80) == 0; // (0x91: the host's request on USB)
    reply.subcommand = data[3];
    reply.status = data[5];
    return reply;
}

bool switch2_reply_matches(const Switch2Reply& reply, const std::vector<std::uint8_t>& command) {
    return command.size() >= 8 && reply.from_device && reply.command == command[0] && reply.subcommand == command[3];
}

// ---- Calibration

Switch2StickCalibration parse_switch2_stick_calibration(const std::uint8_t* d) {
    // SDL's ParseStickCalibration.
    Switch2StickCalibration stick;
    stick.x.neutral = std::uint16_t(d[0] | ((d[1] & 0x0F) << 8));
    stick.y.neutral = std::uint16_t((d[1] >> 4) | (d[2] << 4));
    stick.x.max = std::uint16_t(d[3] | ((d[4] & 0x0F) << 8));
    stick.y.max = std::uint16_t((d[4] >> 4) | (d[5] << 4));
    stick.x.min = std::uint16_t(d[6] | ((d[7] & 0x0F) << 8));
    stick.y.min = std::uint16_t((d[7] >> 4) | (d[8] << 4));
    return stick;
}

bool switch2_calibration_usable(const Switch2StickCalibration& stick) {
    // (Measured on a controller: rest 1971..2144, travel 1521..1654.)
    auto axis = [](const Switch2AxisCalibration& a) {
        return a.neutral >= 1024 && a.neutral <= 3072 && a.max >= 500 && a.max <= 2047 && a.min >= 500 && a.min <= 2047;
    };
    return axis(stick.x) && axis(stick.y);
}

const char* switch2_calibration_name(Switch2CalibrationSource source) {
    switch (source) {
    case Switch2CalibrationSource::none: return "none";
    case Switch2CalibrationSource::factory: return "factory";
    case Switch2CalibrationSource::user: return "user";
    }
    return "";
}

bool apply_switch2_factory_stick(Switch2Calibration& calibration, bool right, const std::uint8_t* block) {
    if (!block) return false;
    const auto stick = parse_switch2_stick_calibration(block + 0x28);
    if (!switch2_calibration_usable(stick)) return false;
    auto& source = right ? calibration.right_source : calibration.left_source;
    if (source == Switch2CalibrationSource::user) return false; // (the user's stays)
    (right ? calibration.right : calibration.left) = stick;
    source = Switch2CalibrationSource::factory;
    return true;
}

bool apply_switch2_user_stick(Switch2Calibration& calibration, bool right, const std::uint8_t* block) {
    if (!block || block[0] != 0xb2 || block[1] != 0xa1) return false;
    const auto stick = parse_switch2_stick_calibration(block + 2);
    if (!switch2_calibration_usable(stick)) return false;
    (right ? calibration.right : calibration.left) = stick;
    (right ? calibration.right_source : calibration.left_source) = Switch2CalibrationSource::user;
    return true;
}

// ---- Reports

std::optional<Switch2Controls> parse_switch2_report(const std::uint8_t* d, std::size_t size) {
    if (!d || size < 1) return std::nullopt;
    namespace sb = switch2_button;
    Switch2Controls c;
    c.report_id = d[0];
    auto bit = [&](std::uint8_t byte, std::uint8_t mask, std::uint32_t button) {
        if (byte & mask) c.buttons |= button;
    };
    auto sticks = [&](std::size_t at) {
        c.left_x = std::uint16_t(d[at] | ((d[at + 1] & 0x0F) << 8));
        c.left_y = std::uint16_t((d[at + 1] >> 4) | (d[at + 2] << 4));
        c.right_x = std::uint16_t(d[at + 3] | ((d[at + 4] & 0x0F) << 8));
        c.right_y = std::uint16_t((d[at + 4] >> 4) | (d[at + 5] << 4));
    };
    if (d[0] == kSwitch2ReportFull) {
        // SDL's HandleSwitchProState: bytes 5..8, the sticks at 11 and 14.
        if (size < 17) return std::nullopt;
        bit(d[5], 0x01, sb::y), bit(d[5], 0x02, sb::x), bit(d[5], 0x04, sb::b), bit(d[5], 0x08, sb::a);
        bit(d[5], 0x40, sb::r), bit(d[5], 0x80, sb::zr);
        bit(d[6], 0x01, sb::minus), bit(d[6], 0x02, sb::plus), bit(d[6], 0x04, sb::right_stick), bit(d[6], 0x08, sb::left_stick);
        bit(d[6], 0x10, sb::home), bit(d[6], 0x20, sb::capture), bit(d[6], 0x40, sb::c);
        bit(d[7], 0x01, sb::down), bit(d[7], 0x02, sb::up), bit(d[7], 0x04, sb::right), bit(d[7], 0x08, sb::left);
        bit(d[7], 0x40, sb::l), bit(d[7], 0x80, sb::zl);
        bit(d[8], 0x01, sb::gr), bit(d[8], 0x02, sb::gl);
        sticks(11);
        return c;
    }
    if (d[0] == kSwitch2ReportPro) {
        // procon2-mac's measurement (after the id: a counter, one byte, the
        // buttons in three bytes, the sticks in six).
        if (size < 12) return std::nullopt;
        bit(d[3], 0x01, sb::b), bit(d[3], 0x02, sb::a), bit(d[3], 0x04, sb::y), bit(d[3], 0x08, sb::x);
        bit(d[3], 0x10, sb::r), bit(d[3], 0x20, sb::zr), bit(d[3], 0x40, sb::plus), bit(d[3], 0x80, sb::right_stick);
        bit(d[4], 0x01, sb::down), bit(d[4], 0x02, sb::right), bit(d[4], 0x04, sb::left), bit(d[4], 0x08, sb::up);
        bit(d[4], 0x10, sb::l), bit(d[4], 0x20, sb::zl), bit(d[4], 0x40, sb::minus), bit(d[4], 0x80, sb::left_stick);
        bit(d[5], 0x01, sb::home), bit(d[5], 0x02, sb::capture), bit(d[5], 0x04, sb::gr), bit(d[5], 0x08, sb::gl);
        bit(d[5], 0x10, sb::c);
        sticks(6);
        return c;
    }
    return std::nullopt;
}

// ---- As an SDL gamepad

std::int16_t switch2_stick_axis(std::uint16_t raw, const Switch2AxisCalibration& calibration, bool usable, bool invert) {
    // SDL's MapJoystickAxis.
    float value = float(raw);
    std::int16_t mapped = 0;
    if (usable && calibration.neutral && calibration.min && calibration.max) {
        value -= float(calibration.neutral);
        value /= value < 0 ? float(calibration.min) : float(calibration.max);
        mapped = std::int16_t(std::clamp(value * 32767.0f, -32768.0f, 32767.0f));
    } else {
        // HIDAPI_RemapVal(value, 0, 4096, SDL_MIN_SINT16, SDL_MAX_SINT16)
        mapped = std::int16_t(-32768.0f + 65535.0f * value / 4096.0f);
    }
    if (invert) mapped = std::int16_t(~mapped);
    return mapped;
}

Switch2PadState switch2_pad_state(const Switch2Controls& c, const Switch2Calibration& calibration) {
    namespace sb = switch2_button;
    Switch2PadState s;
    auto button = [&](SDL_GamepadButton b, std::uint32_t mask) { s.buttons[std::size_t(b)] = (c.buttons & mask) != 0; };
    button(SDL_GAMEPAD_BUTTON_SOUTH, sb::b);
    button(SDL_GAMEPAD_BUTTON_EAST, sb::a);
    button(SDL_GAMEPAD_BUTTON_WEST, sb::y);
    button(SDL_GAMEPAD_BUTTON_NORTH, sb::x);
    button(SDL_GAMEPAD_BUTTON_BACK, sb::minus);
    button(SDL_GAMEPAD_BUTTON_GUIDE, sb::home);
    button(SDL_GAMEPAD_BUTTON_START, sb::plus);
    button(SDL_GAMEPAD_BUTTON_LEFT_STICK, sb::left_stick);
    button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, sb::right_stick);
    button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, sb::l);
    button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, sb::r);
    button(SDL_GAMEPAD_BUTTON_DPAD_UP, sb::up);
    button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, sb::down);
    button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, sb::left);
    button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, sb::right);
    button(SDL_GAMEPAD_BUTTON_MISC1, sb::capture);
    button(SDL_GAMEPAD_BUTTON_MISC2, sb::c);
    button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, sb::gr);
    button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, sb::gl);
    s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = (c.buttons & sb::zl) ? 32767 : 0;
    s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = (c.buttons & sb::zr) ? 32767 : 0;
    const bool left = calibration.left_source != Switch2CalibrationSource::none;
    const bool right = calibration.right_source != Switch2CalibrationSource::none;
    s.axes[SDL_GAMEPAD_AXIS_LEFTX] = switch2_stick_axis(c.left_x, calibration.left.x, left, false);
    s.axes[SDL_GAMEPAD_AXIS_LEFTY] = switch2_stick_axis(c.left_y, calibration.left.y, left, true);
    s.axes[SDL_GAMEPAD_AXIS_RIGHTX] = switch2_stick_axis(c.right_x, calibration.right.x, right, false);
    s.axes[SDL_GAMEPAD_AXIS_RIGHTY] = switch2_stick_axis(c.right_y, calibration.right.y, right, true);
    return s;
}

// ---- The transport

const char* switch2_io_name(Switch2Io io) {
    switch (io) {
    case Switch2Io::ok: return "ok";
    case Switch2Io::timeout: return "timeout";
    case Switch2Io::gone: return "gone";
    case Switch2Io::busy: return "busy";
    case Switch2Io::failed: return "failed";
    }
    return "";
}

std::string switch2_io_text(const Switch2IoResult& result) {
    std::string text = switch2_io_name(result.io);
    if (result.error) text += " (error " + std::to_string(result.error) + ")";
    return text;
}

namespace {
// The Windows system error codes the reader tells apart (winerror.h).
namespace win_error {
inline constexpr std::uint32_t file_not_found = 2, path_not_found = 3, access_denied = 5, bad_command = 22, sharing_violation = 32,
                               dev_not_exist = 55, sem_timeout = 121, busy = 170, wait_timeout = 258, no_such_device = 433,
                               operation_aborted = 995, device_not_connected = 1167, timeout = 1460, device_removed = 1617;
}
} // namespace

Switch2Io switch2_io_of_error(std::uint32_t error) {
    switch (error) {
    case win_error::sem_timeout: // (WinUSB's PIPE_TRANSFER_TIMEOUT)
    case win_error::wait_timeout:
    case win_error::timeout: return Switch2Io::timeout;
    case win_error::device_not_connected:
    case win_error::no_such_device:
    case win_error::file_not_found:
    case win_error::path_not_found:
    case win_error::dev_not_exist:
    case win_error::device_removed:
    case win_error::operation_aborted:
    case win_error::bad_command: return Switch2Io::gone;
    case win_error::access_denied:
    case win_error::sharing_violation:
    case win_error::busy: return Switch2Io::busy;
    default: return Switch2Io::failed;
    }
}

namespace {
std::string driver_text(UsbDriver driver, const std::string& service) {
    switch (driver) {
    case UsbDriver::unknown: return "not listed";
    case UsbDriver::none: return "no driver";
    case UsbDriver::winusb:
    case UsbDriver::other: return service;
    }
    return {};
}

std::string hex_bytes(const std::uint8_t* data, std::size_t size, std::size_t limit) {
    std::string out;
    char cell[4];
    for (std::size_t i = 0; i < size && i < limit; ++i) {
        std::snprintf(cell, sizeof cell, i ? " %02x" : "%02x", data[i]);
        out += cell;
    }
    if (size > limit) out += " ...";
    return out;
}

std::string hex8(std::uint32_t value, int digits) {
    char text[16];
    std::snprintf(text, sizeof text, "%0*x", digits, unsigned(value));
    return text;
}

std::string stick_text(const Switch2StickCalibration& s) {
    return "rest " + std::to_string(s.x.neutral) + "," + std::to_string(s.y.neutral) + " travel +" + std::to_string(s.x.max) + ",+" +
           std::to_string(s.y.max) + " -" + std::to_string(s.x.min) + ",-" + std::to_string(s.y.min);
}
} // namespace

std::string switch2_found_line(const Switch2Found& f) {
    std::string line = "on USB (" + f.id + "): interface 0 ";
    line += f.input_node ? "[" + f.input_service + "]" : "not listed";
    line += f.input_path.empty() ? ", no HID input device" : ", HID input " + f.input_path;
    line += "; interface 1 [" + driver_text(f.command_driver, f.command_service) + "]";
    if (f.command_node && (f.command_driver == UsbDriver::winusb || f.command_driver == UsbDriver::other))
        line += f.command_guid ? " with a device interface GUID" : " without a device interface GUID";
    if (!f.command_path.empty()) line += ", " + f.command_path;
    return line;
}

const char* switch2_state_name(Switch2State state) {
    switch (state) {
    case Switch2State::opening: return "opening";
    case Switch2State::no_input: return "no input device";
    case Switch2State::starting: return "starting";
    case Switch2State::waiting: return "waiting for input";
    case Switch2State::running: return "running";
    case Switch2State::no_commands: return "interface 1 unavailable";
    case Switch2State::no_reports: return "no input reports";
    case Switch2State::gone: return "unplugged";
    }
    return "";
}

// ---- One controller

Switch2Machine::Switch2Machine(Switch2Platform& platform, Switch2Found found, int number) : platform_(platform), listed_(found) {
    status_.number = number;
    status_.found = std::move(found);
}

Switch2Machine::~Switch2Machine() { close(); }

void Switch2Machine::log(const std::string& line) {
    log_.push_back((status_.number == 1 ? std::string("switch 2 pro: ") : "switch 2 pro " + std::to_string(status_.number) + ": ") + line);
}

std::vector<std::string> Switch2Machine::take_log() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

bool Switch2Machine::take_scan_request() { return std::exchange(scan_request_, false); }

void Switch2Machine::close() {
    input_.reset();
    commands_.reset();
    status_.commands = false;
}

bool Switch2Machine::fresh(Clock::time_point now) const {
    return controls_ && status_.state == Switch2State::running && now - last_report_ < kStall;
}

Switch2Machine::Clock::time_point Switch2Machine::last_report() const { return last_report_; }

void Switch2Machine::set_player(int player) { player_ = player; }

void Switch2Machine::listed(const std::optional<Switch2Found>& found) {
    if (status_.state == Switch2State::gone) return;
    if (!found) {
        log("unplugged (Windows no longer lists it)");
        close();
        status_.state = Switch2State::gone;
        return;
    }
    if (listed_ && *found == *listed_) return;
    listed_ = found;
    status_.found = *found;
    log("listed now " + switch2_found_line(*found));
    retry_at_ = {}; // (a waiting open tries at once)
}

void Switch2Machine::lost(const Switch2IoResult& result, const std::string& where, Clock::time_point now) {
    // Unplugged, or a hiccup (an aborted read over standby, a reset port):
    // Windows' list tells them apart (listed()); until it says the
    // controller is gone, both channels open again a second later.
    log(where + ": lost the controller (" + switch2_io_text(result) +
        "); opening it again in a second unless Windows no longer lists it");
    close();
    status_.started = false;
    status_.state = Switch2State::opening;
    retry_at_ = now + kLostPause;
    scan_request_ = true;
}

Switch2Machine::Clock::time_point Switch2Machine::step(Clock::time_point now) {
    switch (status_.state) {
    case Switch2State::gone: return now + std::chrono::hours(1);
    case Switch2State::opening:
        if (now < retry_at_) return retry_at_; // (after a lost channel)
        open(now);
        return now;
    case Switch2State::no_input:
        if (now < retry_at_) return retry_at_;
        status_.state = Switch2State::opening;
        return now;
    case Switch2State::starting: start(now, true, true); return now;
    case Switch2State::waiting:
    case Switch2State::running:
    case Switch2State::no_commands:
    case Switch2State::no_reports: read(now); return now;
    }
    return now;
}

void Switch2Machine::open(Clock::time_point now) {
    const Switch2Found found = listed_ ? *listed_ : status_.found;
    status_.found = found;
    if (!input_) {
        if (found.input_path.empty()) {
            if (status_.input_open.io != Switch2Io::failed || status_.input_open.error != 2)
                log(std::string("input: Windows lists no HID input device for interface 0 (") +
                    (found.input_node ? "its driver: " + (found.input_service.empty() ? std::string("none") : found.input_service)
                                      : std::string("interface 0 not listed")) +
                    "); looking again");
            status_.input_open = {Switch2Io::failed, 2, 0}; // (ERROR_FILE_NOT_FOUND: nothing to open)
            status_.state = Switch2State::no_input;
            retry_at_ = now + kRetry;
            return;
        }
        Switch2IoResult result;
        input_ = platform_.open_input(found, result);
        const bool repeated = status_.input_open.io == result.io && status_.input_open.error == result.error && status_.input_open.io != Switch2Io::ok;
        status_.input_open = result;
        if (!input_) {
            if (!repeated) log("input: cannot open " + found.input_path + ": " + switch2_io_text(result) + "; trying again every two seconds");
            status_.state = Switch2State::no_input;
            retry_at_ = now + kRetry;
            return;
        }
        status_.input_open.io = Switch2Io::ok;
        log("input: opened " + found.input_path + " (" + input_->describe() + ")");
    }
    if (!commands_ && !found.command_path.empty()) {
        Switch2IoResult result;
        commands_ = platform_.open_commands(found, result);
        const bool repeated =
            status_.command_open.io == result.io && status_.command_open.error == result.error && status_.command_open.io != Switch2Io::ok;
        status_.command_open = result;
        if (commands_) {
            status_.command_open.io = Switch2Io::ok;
            log("interface 1: opened (" + commands_->describe() + ")");
        } else if (!repeated) {
            log("interface 1: cannot open it: " + switch2_io_text(result) +
                (result.io == Switch2Io::busy ? " (another program holds it: Steam with its Switch controller support, an emulator, a "
                                                "second copy of this game?)"
                                              : ""));
        }
    } else if (!commands_) {
        if (status_.command_open.io != Switch2Io::failed || status_.command_open.error != 2)
            log("interface 1: nothing to open (" + driver_text(found.command_driver, found.command_service) +
                (found.command_node && !found.command_guid && found.command_driver == UsbDriver::winusb ? ", no device interface GUID" : "") +
                ")");
        status_.command_open = {Switch2Io::failed, 2, 0};
    }
    status_.commands = commands_ != nullptr;
    if (commands_) {
        status_.state = Switch2State::starting;
    } else {
        // Without interface 1: another program may have started it (its
        // reports arrive all the same).
        status_.state = Switch2State::waiting;
        deadline_ = now + kFirstReport;
        retry_at_ = now + kRetry;
    }
}

bool Switch2Machine::receive_reply(std::uint8_t* reply, std::size_t reply_size, std::size_t& got, Switch2IoResult& result) {
    got = 0;
    // SDL's RecvBulkData: 64 bytes at a time until a short packet.
    while (got < reply_size) {
        const std::size_t chunk = std::min<std::size_t>(64, reply_size - got);
        const Switch2IoResult r = commands_->receive(reply + got, chunk, kReplyWait);
        if (r.io == Switch2Io::gone) {
            result = r;
            return false;
        }
        if (r.io != Switch2Io::ok) break;
        got += r.bytes;
        if (r.bytes < chunk) break;
    }
    return true;
}

bool Switch2Machine::exchange(const std::vector<std::uint8_t>& command, std::uint8_t* reply, std::size_t reply_size, std::size_t& got,
                              Switch2IoResult& sent, const LateReply& late) {
    got = 0;
    sent = commands_->send(command.data(), command.size());
    if (sent.io != Switch2Io::ok) return false;
    for (int lates = 0;; ++lates) {
        std::fill(reply, reply + reply_size, std::uint8_t(0));
        if (!receive_reply(reply, reply_size, got, sent)) return false;
        if (!got || !late || lates >= kLateReplies) return true;
        const auto parsed = parse_switch2_reply(reply, got);
        if (parsed && switch2_reply_matches(*parsed, command)) return true;
        // (A reply an earlier command's wait missed: that command's, and this
        // one's may still come. Round I1b's integration, the owner's hardware
        // test: 0C/02 answered after its 100 ms, so 11/01 read 0C/02's reply
        // and 0A/08 read 11/01's, each "not its reply".)
        if (!late(reply, got)) return true;
    }
}

void Switch2Machine::start(Clock::time_point now, bool read_flash, bool verbose) {
    status_.state = Switch2State::starting;
    started_at_ = now;
    status_.failed_step.clear();
    status_.failed_result = {};
    commands_->flush();
    const bool first = status_.starts == 0;
    std::array<std::uint8_t, kSwitch2FlashReply> reply{};
    std::size_t got = 0;
    Switch2IoResult sent;
    auto abort = [&](const std::string& step) {
        if (sent.io == Switch2Io::gone) {
            lost(sent, "start: " + step, now);
            return;
        }
        status_.failed_step = step;
        status_.failed_result = sent;
        log("start: " + step + " could not be sent: " + switch2_io_text(sent) + "; interface 1 closed, opened again in two seconds");
        commands_.reset();
        status_.commands = false;
        status_.state = Switch2State::waiting; // (its reports may still come)
        deadline_ = now + kFirstReport;
        retry_at_ = now + kRetry;
    };
    if (read_flash && first) {
        // The serial number's block (read as SDL does; not kept) and the
        // stick calibration.
        struct Read {
            std::uint32_t address;
            const char* what;
        };
        static constexpr Read kReads[] = {{kSwitch2FlashSerial, "serial number"},
                                          {kSwitch2FlashLeftFactory, "left stick, factory"},
                                          {kSwitch2FlashRightFactory, "right stick, factory"},
                                          {kSwitch2FlashLeftUser, "left stick, user"},
                                          {kSwitch2FlashRightUser, "right stick, user"}};
        for (const auto& read : kReads) {
            const auto command = switch2_flash_read_command(read.address);
            const std::string step = "flash 0x" + hex8(read.address, 6) + " (" + read.what + ")";
            reply.fill(0);
            if (!exchange(command, reply.data(), reply.size(), got, sent)) return abort(step);
            const auto parsed = parse_switch2_reply(reply.data(), got);
            std::string line = step + ": ";
            if (got < kSwitch2FlashReply || !parsed || !switch2_reply_matches(*parsed, command)) {
                line += got ? "unexpected reply (" + std::to_string(got) + " bytes: " + hex_bytes(reply.data(), got, 8) + ")" : "no reply";
                log(line);
                continue;
            }
            const std::uint8_t* data = reply.data() + kSwitch2FlashData;
            line += "ok";
            if (read.address == kSwitch2FlashLeftFactory || read.address == kSwitch2FlashRightFactory) {
                const bool right = read.address == kSwitch2FlashRightFactory;
                const auto stick = parse_switch2_stick_calibration(data + 0x28);
                line += ", " + stick_text(stick) + (apply_switch2_factory_stick(calibration_, right, data) ? "" : " (not usable)");
            } else if (read.address == kSwitch2FlashLeftUser || read.address == kSwitch2FlashRightUser) {
                const bool right = read.address == kSwitch2FlashRightUser;
                if (data[0] != 0xb2 || data[1] != 0xa1) line += ", none stored";
                else
                    line += ", " + stick_text(parse_switch2_stick_calibration(data + 2)) +
                            (apply_switch2_user_stick(calibration_, right, data) ? "" : " (not usable)");
            }
            log(line);
        }
        status_.left_calibration = calibration_.left_source;
        status_.right_calibration = calibration_.right_source;
        log(std::string("sticks: left ") + switch2_calibration_name(calibration_.left_source) + ", right " +
            switch2_calibration_name(calibration_.right_source) + " calibration" +
            (calibration_.left_source == Switch2CalibrationSource::none || calibration_.right_source == Switch2CalibrationSource::none
                 ? " (none: the raw range)"
                 : ""));
    }
    const auto commands = switch2_start_commands();
    auto step_name = [&](std::size_t i) {
        return "start " + std::to_string(i + 1) + "/" + std::to_string(commands.size()) + " " + commands[i].name;
    };
    int answered = 0;
    // The commands still without their reply: a reply that comes after its
    // command's wait is matched to it by its ids when a later command reads it
    // (or at the end), never taken as the later command's.
    std::vector<std::size_t> unanswered;
    const LateReply late = [&](const std::uint8_t* data, std::size_t size) {
        const auto parsed = parse_switch2_reply(data, size);
        if (!parsed) return false;
        for (auto it = unanswered.begin(); it != unanswered.end(); ++it) {
            if (!switch2_reply_matches(*parsed, commands[*it].bytes)) continue;
            ++answered;
            if (verbose) log(step_name(*it) + ": its reply came late: " + hex_bytes(data, size, 12));
            unanswered.erase(it);
            return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto& command = commands[i];
        const std::string step = step_name(i);
        if (!exchange(command.bytes, reply.data(), 64, got, sent, late)) return abort(step);
        const auto parsed = parse_switch2_reply(reply.data(), got);
        const bool matches = got && parsed && switch2_reply_matches(*parsed, command.bytes);
        answered += matches ? 1 : 0;
        if (!matches) unanswered.push_back(i);
        if (!verbose) continue;
        std::string line = step + ": ";
        if (!got) line += "no reply within " + std::to_string(kReplyWait.count()) + " ms";
        else {
            line += "reply " + hex_bytes(reply.data(), got, 12);
            if (!matches) line += " (not its reply)";
        }
        log(line);
    }
    // The late replies still on their way (one wait each; the first without
    // one ends it), so none is read as the player lights' reply.
    for (std::size_t tries = unanswered.size(); tries > 0 && !unanswered.empty(); --tries) {
        if (!receive_reply(reply.data(), 64, got, sent)) return abort("the late replies");
        if (!got) break;
        if (!late(reply.data(), got)) {
            if (verbose) log("start: a reply of no start command: " + hex_bytes(reply.data(), got, 12));
            break;
        }
    }
    if (verbose)
        for (const std::size_t i : unanswered) log(step_name(i) + ": no reply");
    if (!verbose) log("start sequence sent again: " + std::to_string(answered) + " of " + std::to_string(commands.size()) + " commands answered");
    status_.started = true;
    ++status_.starts;
    led_sent_ = -2;
    if (player_ >= 0) send_led(now); // (the host names the player once the pad is there)
    if (status_.state != Switch2State::starting) return; // (lost while sending them)
    status_.state = Switch2State::waiting;
    deadline_ = now + kFirstReport;
}

void Switch2Machine::send_led(Clock::time_point now) {
    if (!commands_ || player_ < 0 || player_ == led_sent_) return;
    const auto command = switch2_player_led_command(switch2_player_pattern(player_));
    std::array<std::uint8_t, 64> reply{};
    std::size_t got = 0;
    Switch2IoResult sent;
    // (A start command's reply that came after the start's last wait is not
    // the lights' reply.)
    const auto start = switch2_start_commands();
    const LateReply late = [&](const std::uint8_t* data, std::size_t size) {
        const auto parsed = parse_switch2_reply(data, size);
        if (!parsed) return false;
        for (const auto& c : start)
            if (switch2_reply_matches(*parsed, c.bytes)) {
                log(std::string("start ") + c.name + ": its reply came late: " + hex_bytes(data, size, 12));
                return true;
            }
        return false;
    };
    if (!exchange(command, reply.data(), 16, got, sent, late)) {
        if (sent.io == Switch2Io::gone) return lost(sent, "player lights", now);
        log("player lights: could not be sent: " + switch2_io_text(sent));
        led_sent_ = player_; // (not again and again)
        return;
    }
    log("player lights " + (player_ < 0 ? std::string("off") : "of player " + std::to_string(player_ + 1)) + ": " +
        (got ? "reply " + hex_bytes(reply.data(), got, 8) : std::string("no reply")));
    led_sent_ = player_;
}

void Switch2Machine::report(const std::uint8_t* data, std::size_t size, Clock::time_point now) {
    ++status_.reports;
    const auto parsed = parse_switch2_report(data, size);
    const std::uint8_t id = size ? data[0] : 0;
    if (!parsed) {
        // (Not a report of the controls: it does not count as input.)
        if (id != unknown_id_)
            log("input report 0x" + hex8(id, 2) + " (" + std::to_string(size) + " bytes) is not one this reader knows: " + hex_bytes(data, size, 17));
        unknown_id_ = id;
        return;
    }
    ++rate_reports_;
    last_report_ = now;
    if (status_.state != Switch2State::running) {
        if (!first_logged_)
            log("first input report: id 0x" + hex8(id, 2) + ", " + std::to_string(size) + " bytes" +
                (status_.started ? ", " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at_).count()) +
                                       " ms after the start"
                                 : std::string(" (started by another program)")) +
                ": " + hex_bytes(data, size, 17));
        else
            log("input reports again (id 0x" + hex8(id, 2) + ")");
        first_logged_ = true;
        no_reports_logged_ = false;
        status_.state = Switch2State::running;
        rate_since_ = now;
        rate_reports_ = 0;
    }
    if (id != status_.report_id) {
        if (status_.report_id) log("input reports now with id 0x" + hex8(id, 2) + " (" + std::to_string(size) + " bytes)");
        status_.report_id = id;
    }
    controls_ = parsed;
    if (now - rate_since_ >= kRateWindow) {
        const double seconds = std::chrono::duration<double>(now - rate_since_).count();
        status_.rate = double(rate_reports_) / seconds;
        if (!rate_logged_ || std::abs(status_.rate - logged_rate_) > logged_rate_ * 0.25) {
            char text[96];
            std::snprintf(text, sizeof text, "report rate: %.0f per second (id 0x%02x, %zu bytes)", status_.rate, unsigned(id), size);
            log(text);
            rate_logged_ = true;
            logged_rate_ = status_.rate;
        }
        rate_since_ = now;
        rate_reports_ = 0;
    }
}

void Switch2Machine::read(Clock::time_point now) {
    if (!input_) {
        status_.state = Switch2State::opening;
        return;
    }
    const Switch2IoResult r = input_->read(buffer_.data(), buffer_.size(), kReadWait);
    if (r.io == Switch2Io::ok) {
        report(buffer_.data(), std::min(r.bytes, buffer_.size()), now);
    } else if (r.io == Switch2Io::gone) {
        return lost(r, "input", now);
    } else if (r.io != Switch2Io::timeout) {
        log("input: read failed: " + switch2_io_text(r) + "; opening it again");
        input_.reset();
        status_.state = Switch2State::no_input;
        retry_at_ = now + std::chrono::milliseconds(500);
        return;
    }
    switch (status_.state) {
    case Switch2State::waiting:
        if (now < deadline_) break;
        if (commands_ && status_.starts < kStartTries) {
            log("no input report " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(kFirstReport).count()) +
                " ms after the start: starting it again (" + std::to_string(status_.starts + 1) + "/" + std::to_string(kStartTries) + ")");
            start(now, false, true);
        } else if (commands_) {
            if (!no_reports_logged_) log("no input report after " + std::to_string(status_.starts) + " starts; trying once more every ten seconds");
            no_reports_logged_ = true;
            status_.state = Switch2State::no_reports;
            retry_at_ = now + std::chrono::seconds(10);
        } else {
            if (status_.state != Switch2State::no_commands)
                log("no input report came by itself: the game needs interface 1 to start it; trying again every two seconds");
            status_.state = Switch2State::no_commands;
            retry_at_ = now + kRetry;
        }
        break;
    case Switch2State::running:
        if (now - last_report_ < kStall) {
            if (commands_ && player_ >= 0 && player_ != led_sent_) send_led(now);
            break;
        }
        if (commands_) {
            log("input reports stopped for " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report_).count()) +
                " ms: starting it again");
            status_.starts = std::min(status_.starts, 1); // (a fresh round of tries)
            start(now, false, true);
        } else {
            log("input reports stopped; waiting for them");
            status_.state = Switch2State::waiting;
            deadline_ = now + kFirstReport;
        }
        break;
    case Switch2State::no_commands:
        if (now < retry_at_) break;
        retry_at_ = now + kRetry;
        if (const Switch2Found found = listed_ ? *listed_ : status_.found; !found.command_path.empty()) {
            Switch2IoResult result;
            commands_ = platform_.open_commands(found, result);
            const bool repeated = status_.command_open.io == result.io && status_.command_open.error == result.error;
            status_.command_open = result;
            if (commands_) {
                status_.command_open.io = Switch2Io::ok;
                status_.commands = true;
                log("interface 1: opened (" + commands_->describe() + ")");
                start(now, true, true);
            } else if (!repeated) {
                log("interface 1: cannot open it: " + switch2_io_text(result));
            }
        }
        break;
    case Switch2State::no_reports:
        if (now < retry_at_ || !commands_) break;
        status_.starts = kStartTries - 1;
        start(now, false, false);
        break;
    default: break;
    }
}

// ---- Windows

#ifdef _WIN32
namespace {
std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(std::size_t(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
    std::wstring out(std::size_t(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size);
    return out;
}

std::wstring upper(std::wstring text) {
    for (auto& c : text) c = wchar_t(std::towupper(c));
    return text;
}

static_assert(win_error::file_not_found == ERROR_FILE_NOT_FOUND && win_error::path_not_found == ERROR_PATH_NOT_FOUND &&
              win_error::access_denied == ERROR_ACCESS_DENIED && win_error::bad_command == ERROR_BAD_COMMAND &&
              win_error::sharing_violation == ERROR_SHARING_VIOLATION && win_error::dev_not_exist == ERROR_DEV_NOT_EXIST &&
              win_error::sem_timeout == ERROR_SEM_TIMEOUT && win_error::busy == ERROR_BUSY && win_error::wait_timeout == WAIT_TIMEOUT &&
              win_error::no_such_device == ERROR_NO_SUCH_DEVICE && win_error::operation_aborted == ERROR_OPERATION_ABORTED &&
              win_error::device_not_connected == ERROR_DEVICE_NOT_CONNECTED && win_error::timeout == ERROR_TIMEOUT &&
              win_error::device_removed == ERROR_DEVICE_REMOVED);

Switch2IoResult io_error(DWORD error) { return {switch2_io_of_error(std::uint32_t(error)), std::uint32_t(error), 0}; }

std::wstring device_id(DEVINST node) {
    wchar_t id[MAX_DEVICE_ID_LEN + 1]{};
    if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};
    return id;
}

std::wstring registry_text(DEVINST node, ULONG property) {
    wchar_t text[512]{};
    ULONG size = sizeof(text) - 2 * sizeof(wchar_t);
    ULONG type = 0;
    if (CM_Get_DevNode_Registry_PropertyW(node, property, &type, text, &size, 0) != CR_SUCCESS) return {};
    return text;
}

bool parse_guid(const wchar_t* text, GUID& guid) {
    unsigned long d1 = 0;
    unsigned int d2 = 0, d3 = 0, b[8]{};
    if (std::swscanf(text, L"{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11)
        return false;
    guid.Data1 = d1;
    guid.Data2 = static_cast<unsigned short>(d2);
    guid.Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; ++i) guid.Data4[i] = static_cast<unsigned char>(b[i]);
    return true;
}

// The device interface GUID a node's hardware key names (the controller's
// Microsoft OS descriptor puts it there as DeviceInterfaceGUID; an INF as
// DeviceInterfaceGUIDs).
std::optional<GUID> node_interface_guid(DEVINST node) {
    HKEY key = nullptr;
    if (CM_Open_DevNode_Key(node, KEY_READ, 0, RegDisposition_OpenExisting, &key, CM_REGISTRY_HARDWARE) != CR_SUCCESS) return std::nullopt;
    wchar_t guids[1024]{};
    DWORD size = sizeof(guids) - 4 * sizeof(wchar_t);
    LSTATUS status = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", nullptr, nullptr, reinterpret_cast<BYTE*>(guids), &size);
    if (status != ERROR_SUCCESS) {
        size = sizeof(guids) - 4 * sizeof(wchar_t);
        status = RegQueryValueExW(key, L"DeviceInterfaceGUID", nullptr, nullptr, reinterpret_cast<BYTE*>(guids), &size);
    }
    RegCloseKey(key);
    GUID guid{};
    if (status != ERROR_SUCCESS || !parse_guid(guids, guid)) return std::nullopt;
    return guid;
}

// The first present device interface of a class on a node.
std::wstring node_interface_path(const GUID& guid, const std::wstring& id) {
    ULONG size = 0;
    if (CM_Get_Device_Interface_List_SizeW(&size, const_cast<GUID*>(&guid), const_cast<wchar_t*>(id.c_str()), CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
            CR_SUCCESS ||
        size <= 1 || size > 65536)
        return {};
    std::vector<wchar_t> list(size + 1, L'\0');
    if (CM_Get_Device_Interface_ListW(const_cast<GUID*>(&guid), const_cast<wchar_t*>(id.c_str()), list.data(), size,
                                      CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
        return {};
    return list.data(); // (the first of the double-terminated list)
}

std::optional<int> interface_of(const std::wstring& id) {
    const std::wstring text = upper(id);
    const auto at = text.find(L"&MI_");
    if (at == std::wstring::npos || at + 6 > text.size()) return std::nullopt;
    int value = 0;
    for (std::size_t i = at + 4; i < at + 6; ++i) {
        const wchar_t c = text[i];
        if (c >= L'0' && c <= L'9') value = value * 16 + (c - L'0');
        else if (c >= L'A' && c <= L'F') value = value * 16 + (c - L'A' + 10);
        else return std::nullopt;
    }
    return value;
}

class WindowsInput final : public Switch2Input {
public:
    WindowsInput(HANDLE file, std::size_t report_size, std::string description)
        : file_(file), buffer_(std::max<std::size_t>(report_size, kSwitch2ReportBytes)), description_(std::move(description)) {
        overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~WindowsInput() override {
        if (pending_) {
            CancelIoEx(file_, &overlapped_);
            DWORD got = 0;
            GetOverlappedResult(file_, &overlapped_, &got, TRUE);
        }
        if (overlapped_.hEvent) CloseHandle(overlapped_.hEvent);
        CloseHandle(file_);
    }
    Switch2IoResult read(std::uint8_t* out, std::size_t size, std::chrono::milliseconds timeout) override {
        if (!overlapped_.hEvent) return {Switch2Io::failed, ERROR_INVALID_HANDLE, 0};
        DWORD got = 0;
        if (!pending_) {
            ResetEvent(overlapped_.hEvent);
            if (ReadFile(file_, buffer_.data(), DWORD(buffer_.size()), &got, &overlapped_)) return deliver(out, size, got);
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) return io_error(error);
            pending_ = true;
        }
        if (WaitForSingleObject(overlapped_.hEvent, DWORD(timeout.count())) == WAIT_TIMEOUT) return {Switch2Io::timeout, 0, 0};
        pending_ = false;
        if (!GetOverlappedResult(file_, &overlapped_, &got, FALSE)) return io_error(GetLastError());
        return deliver(out, size, got);
    }
    std::string describe() const override { return description_; }

private:
    Switch2IoResult deliver(std::uint8_t* out, std::size_t size, DWORD got) {
        const std::size_t n = std::min<std::size_t>({std::size_t(got), size, buffer_.size()});
        std::memcpy(out, buffer_.data(), n);
        return {Switch2Io::ok, 0, n};
    }
    HANDLE file_;
    OVERLAPPED overlapped_{};
    bool pending_ = false;
    std::vector<std::uint8_t> buffer_;
    std::string description_;
};

class WindowsCommands final : public Switch2Commands {
public:
    WindowsCommands(HANDLE file, WINUSB_INTERFACE_HANDLE usb, UCHAR out_pipe, UCHAR in_pipe, std::string description)
        : file_(file), usb_(usb), out_(out_pipe), in_(in_pipe), description_(std::move(description)) {}
    ~WindowsCommands() override {
        WinUsb_Free(usb_);
        CloseHandle(file_);
    }
    Switch2IoResult send(const std::uint8_t* data, std::size_t size) override {
        ULONG written = 0;
        if (!WinUsb_WritePipe(usb_, out_, const_cast<UCHAR*>(data), ULONG(size), &written, nullptr)) return io_error(GetLastError());
        if (written != size) return {Switch2Io::failed, 0, written};
        return {Switch2Io::ok, 0, written};
    }
    Switch2IoResult receive(std::uint8_t* buffer, std::size_t size, std::chrono::milliseconds timeout) override {
        const ULONG wanted = ULONG(std::max<long long>(1, timeout.count()));
        if (wanted != in_timeout_) {
            WinUsb_SetPipePolicy(usb_, in_, PIPE_TRANSFER_TIMEOUT, sizeof(wanted), const_cast<ULONG*>(&wanted));
            in_timeout_ = wanted;
        }
        ULONG got = 0;
        if (!WinUsb_ReadPipe(usb_, in_, buffer, ULONG(size), &got, nullptr)) return io_error(GetLastError());
        return {Switch2Io::ok, 0, got};
    }
    void flush() override { WinUsb_FlushPipe(usb_, in_); }
    std::string describe() const override { return description_; }

private:
    HANDLE file_;
    WINUSB_INTERFACE_HANDLE usb_;
    UCHAR out_, in_;
    ULONG in_timeout_ = 0;
    std::string description_;
};

class WindowsPlatform final : public Switch2Platform {
public:
    std::vector<Switch2Found> scan() override {
        std::vector<Switch2Found> out;
        // Every present USB device's instance id; the controller's own node
        // (its composite parent) is USB\VID_057E&PID_2069\<instance>.
        ULONG size = 0;
        if (CM_Get_Device_ID_List_SizeW(&size, L"USB", CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS || size <= 1)
            return out;
        std::vector<wchar_t> ids(size + 1, L'\0');
        if (CM_Get_Device_ID_ListW(L"USB", ids.data(), size, CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS)
            return out;
        GUID hid{};
        HidD_GetHidGuid(&hid);
        for (const wchar_t* id = ids.data(); *id; id += std::wcslen(id) + 1) {
            const std::wstring text = upper(id);
            if (text.rfind(L"USB\\VID_057E&PID_2069", 0) != 0 || text.find(L"&MI_") != std::wstring::npos) continue;
            DEVINST node = 0;
            if (CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(id), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;
            Switch2Found found;
            found.id = narrow(id);
            // Its interfaces (MI_xx, usbccgp's children).
            DEVINST child = 0;
            for (CONFIGRET cr = CM_Get_Child(&child, node, 0); cr == CR_SUCCESS; cr = CM_Get_Sibling(&child, child, 0)) {
                const std::wstring child_id = device_id(child);
                const auto number = interface_of(child_id);
                if (!number) continue;
                const std::wstring service = registry_text(child, CM_DRP_SERVICE);
                if (*number == 0) {
                    found.input_node = true;
                    found.input_service = narrow(service);
                    // Its HID collection (HID\VID_057E&PID_2069&...&MI_00\...).
                    DEVINST collection = 0;
                    for (CONFIGRET c2 = CM_Get_Child(&collection, child, 0); c2 == CR_SUCCESS && found.input_path.empty();
                         c2 = CM_Get_Sibling(&collection, collection, 0))
                        found.input_path = narrow(node_interface_path(hid, device_id(collection)));
                } else if (*number == 1) {
                    found.command_node = true;
                    found.command_service = narrow(service);
                    found.command_driver = usb_driver(service);
                    if (const auto guid = node_interface_guid(child)) {
                        found.command_guid = true;
                        if (found.command_driver == UsbDriver::winusb) found.command_path = narrow(node_interface_path(*guid, child_id));
                    }
                }
            }
            out.push_back(std::move(found));
        }
        return out;
    }

    std::unique_ptr<Switch2Input> open_input(const Switch2Found& found, Switch2IoResult& result) override {
        const std::wstring path = widen(found.input_path);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE) // (reading needs no write access)
            file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            result = io_error(GetLastError());
            return nullptr;
        }
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(file, &attributes) || attributes.VendorID != kSwitch2Vendor || attributes.ProductID != kSwitch2ProProduct) {
            CloseHandle(file);
            result = {Switch2Io::failed, std::uint32_t(ERROR_BAD_DEVICE), 0};
            return nullptr;
        }
        std::size_t report_size = kSwitch2ReportBytes;
        char text[160];
        std::snprintf(text, sizeof text, "HID %04x:%04x version %04x", unsigned(attributes.VendorID), unsigned(attributes.ProductID),
                      unsigned(attributes.VersionNumber));
        std::string description = text;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(file, &preparsed) && preparsed) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                report_size = std::max<std::size_t>(caps.InputReportByteLength, 1);
                std::snprintf(text, sizeof text, ", usage page 0x%04x usage 0x%04x, input reports %u bytes, output %u", unsigned(caps.UsagePage),
                              unsigned(caps.Usage), unsigned(caps.InputReportByteLength), unsigned(caps.OutputReportByteLength));
                description += text;
            }
            HidD_FreePreparsedData(preparsed);
        }
        HidD_SetNumInputBuffers(file, 64);
        HidD_FlushQueue(file);
        result = {Switch2Io::ok, 0, 0};
        return std::make_unique<WindowsInput>(file, report_size, description);
    }

    std::unique_ptr<Switch2Commands> open_commands(const Switch2Found& found, Switch2IoResult& result) override {
        const std::wstring path = widen(found.command_path);
        // (WinUSB opens a device once: another program's handle fails this.)
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            result = io_error(GetLastError());
            return nullptr;
        }
        WINUSB_INTERFACE_HANDLE usb = nullptr;
        if (!WinUsb_Initialize(file, &usb)) {
            result = io_error(GetLastError());
            CloseHandle(file);
            return nullptr;
        }
        USB_INTERFACE_DESCRIPTOR descriptor{};
        UCHAR out_pipe = 0, in_pipe = 0;
        USHORT out_size = 0, in_size = 0;
        if (WinUsb_QueryInterfaceSettings(usb, 0, &descriptor))
            for (UCHAR e = 0; e < descriptor.bNumEndpoints; ++e) {
                WINUSB_PIPE_INFORMATION pipe{};
                if (!WinUsb_QueryPipe(usb, 0, e, &pipe) || pipe.PipeType != UsbdPipeTypeBulk) continue;
                if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
                    if (!in_pipe) in_pipe = pipe.PipeId, in_size = pipe.MaximumPacketSize;
                } else if (!out_pipe) {
                    out_pipe = pipe.PipeId, out_size = pipe.MaximumPacketSize;
                }
            }
        if (!in_pipe || !out_pipe) {
            WinUsb_Free(usb);
            CloseHandle(file);
            result = {Switch2Io::failed, std::uint32_t(ERROR_BAD_DEVICE), 0};
            return nullptr;
        }
        ULONG write_timeout = 1000; // ms (SDL's)
        WinUsb_SetPipePolicy(usb, out_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(write_timeout), &write_timeout);
        char text[160];
        std::snprintf(text, sizeof text, "WinUSB, interface %u class 0x%02x, bulk out 0x%02x (%u bytes), bulk in 0x%02x (%u bytes)",
                      unsigned(descriptor.bInterfaceNumber), unsigned(descriptor.bInterfaceClass), unsigned(out_pipe), unsigned(out_size),
                      unsigned(in_pipe), unsigned(in_size));
        result = {Switch2Io::ok, 0, 0};
        return std::make_unique<WindowsCommands>(file, usb, out_pipe, in_pipe, text);
    }
};
} // namespace

std::unique_ptr<Switch2Platform> make_windows_switch2_platform() { return std::make_unique<WindowsPlatform>(); }
#else
std::unique_ptr<Switch2Platform> make_windows_switch2_platform() { return nullptr; }
#endif

// ---- Every controller

namespace {
using Clock = std::chrono::steady_clock;

struct LogSink {
    std::mutex mutex;
    std::vector<std::string> lines;
    void add(std::vector<std::string> more) {
        if (more.empty()) return;
        std::lock_guard lock(mutex);
        constexpr std::size_t kMax = 2000; // (nobody takes them: a test, or a run without a log)
        for (auto& line : more)
            if (lines.size() < kMax) lines.push_back(std::move(line));
    }
};

// One controller: its machine on its own thread.
struct Device {
    std::string id;
    int number = 1;
    std::shared_ptr<LogSink> sink;
    std::function<void()> request_scan; // (a lost channel: is it still listed?)
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    std::optional<std::optional<Switch2Found>> listed; // (a new list entry for the machine)
    bool unlisted = false; // the manager told it Windows no longer lists it: no entry reaches it again
    int player = -1;
    bool player_changed = false;
    // Published after every step.
    Switch2Status status;
    std::optional<Switch2Controls> controls;
    Switch2Calibration calibration;
    Clock::time_point last_report{};
    bool finished = false;
    // Main thread.
    SDL_JoystickID joystick_id = 0;
    SDL_Joystick* joystick = nullptr;
    Switch2PadState sent;
    std::atomic<bool> released{false}; // the pad is gone: the reader may drop it
    std::thread thread;

    void run(Switch2Platform& platform, Switch2Found found) {
        Switch2Machine machine(platform, std::move(found), number);
        sink->add({(number == 1 ? std::string("switch 2 pro: ") : "switch 2 pro " + std::to_string(number) + ": ") + "found " +
                   switch2_found_line(machine.status().found)});
        for (;;) {
            {
                std::lock_guard lock(mutex);
                if (stop) break;
                if (listed) {
                    machine.listed(*listed);
                    listed.reset();
                }
                if (player_changed) {
                    machine.set_player(player);
                    player_changed = false;
                }
            }
            const auto wake_at = machine.step(Clock::now());
            sink->add(machine.take_log());
            {
                std::lock_guard lock(mutex);
                status = machine.status();
                controls = machine.controls();
                calibration = machine.calibration();
                last_report = machine.last_report();
            }
            if (machine.status().state == Switch2State::gone) break;
            if (machine.take_scan_request() && request_scan) request_scan();
            std::unique_lock lock(mutex);
            wake.wait_until(lock, wake_at, [&] { return stop || listed.has_value() || player_changed; });
        }
        machine.close();
        sink->add(machine.take_log());
        std::lock_guard lock(mutex);
        status = machine.status();
        if (status.state != Switch2State::gone) status.state = Switch2State::gone;
        finished = true;
    }

    void request_stop() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        wake.notify_all();
    }

    ~Device() {
        request_stop();
        if (thread.joinable()) thread.join();
    }
};
} // namespace

struct Switch2Reader::Impl {
    std::unique_ptr<Switch2Platform> platform;
    bool sdl_changes = true;
    std::chrono::milliseconds rescan = Switch2Reader::kRescan;
    std::shared_ptr<LogSink> sink = std::make_shared<LogSink>();
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    bool scan_requested = true;
    std::uint32_t scans = 0;
    long long listed_count = -1; // (how many the last look found: the log's "none on USB")
    std::vector<std::shared_ptr<Device>> devices; // (under `mutex`)
    std::thread manager;
    // Main thread.
    std::uint32_t hid_changes = 0;
    bool hid_changes_known = false;

    void request() {
        {
            std::lock_guard lock(mutex);
            scan_requested = true;
        }
        wake.notify_all();
    }

    // A controller that still counts (not unplugged): under `mutex`.
    static bool live(Device& d) {
        std::lock_guard device_lock(d.mutex);
        return !d.released && !d.finished && !d.unlisted;
    }

    int free_number() const {
        for (int n = 1;; ++n) {
            bool used = false;
            for (const auto& d : devices) used = used || (d->number == n && live(*d));
            if (!used) return n;
        }
    }

    // Whether a controller does not play (a look at the list may help it).
    bool any_not_playing() const {
        bool waiting = false;
        for (const auto& d : devices) {
            std::lock_guard device_lock(d->mutex);
            waiting = waiting || (!d->finished && !d->unlisted && d->status.state != Switch2State::running);
        }
        return waiting;
    }

    void run() {
        std::unique_lock lock(mutex);
        while (!stop) {
            // A look at the device list when asked (a device change, a lost
            // channel), and every `rescan` while a controller does not play
            // (a driver or a path that comes later, another program that
            // lets interface 1 go, a channel lost without a device change).
            if (!scan_requested) {
                wake.wait_for(lock, rescan, [&] { return stop || scan_requested; });
                if (stop) break;
                if (!scan_requested && !any_not_playing()) continue;
            }
            scan_requested = false;
            lock.unlock();
            const auto found = platform->scan();
            lock.lock();
            ++scans;
            if (found.empty() && listed_count != 0) sink->add({"switch 2 pro: none on USB"});
            listed_count = static_cast<long long>(found.size());
            // Drop the controllers the main thread let go.
            devices.erase(std::remove_if(devices.begin(), devices.end(), [](const std::shared_ptr<Device>& d) { return d->released.load(); }), devices.end());
            for (const auto& entry : found) {
                // (One the list lost, also when its thread has not ended yet,
                // takes no entry: a controller listed again starts afresh.)
                std::shared_ptr<Device> device;
                for (const auto& d : devices) {
                    std::lock_guard device_lock(d->mutex);
                    if (d->id == entry.id && !d->finished && !d->unlisted) device = d;
                }
                if (device) {
                    {
                        std::lock_guard device_lock(device->mutex);
                        device->listed = entry;
                    }
                    device->wake.notify_all();
                    continue;
                }
                auto fresh = std::make_shared<Device>();
                fresh->id = entry.id;
                fresh->number = free_number();
                fresh->sink = sink;
                fresh->request_scan = [this] { request(); };
                fresh->status.number = fresh->number;
                fresh->status.found = entry;
                Switch2Platform* p = platform.get();
                Device* raw = fresh.get();
                fresh->thread = std::thread([raw, p, entry] { raw->run(*p, entry); });
                devices.push_back(fresh);
            }
            for (const auto& d : devices) {
                bool listed = false;
                for (const auto& entry : found) listed = listed || entry.id == d->id;
                if (listed) continue;
                {
                    std::lock_guard device_lock(d->mutex);
                    if (d->finished || d->unlisted) continue;
                    d->unlisted = true;
                    d->listed = std::optional<Switch2Found>{};
                }
                d->wake.notify_all();
            }
        }
    }
};

Switch2Reader::Switch2Reader(std::unique_ptr<Switch2Platform> platform, bool sdl_changes, std::chrono::milliseconds rescan)
    : impl_(std::make_unique<Impl>()) {
    impl_->platform = std::move(platform);
    impl_->sdl_changes = sdl_changes;
    impl_->rescan = rescan;
    if (impl_->platform) impl_->manager = std::thread([impl = impl_.get()] { impl->run(); });
}

Switch2Reader::~Switch2Reader() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wake.notify_all();
    if (impl_->manager.joinable()) impl_->manager.join();
    for (const auto& d : impl_->devices) d->request_stop();
    for (const auto& d : impl_->devices) {
        if (d->thread.joinable()) d->thread.join();
        if (d->joystick) SDL_CloseJoystick(d->joystick);
        if (d->joystick_id) SDL_DetachVirtualJoystick(d->joystick_id);
        d->joystick = nullptr;
        d->joystick_id = 0;
    }
    impl_->devices.clear();
}

void Switch2Reader::request_scan() { impl_->request(); }

std::uint32_t Switch2Reader::scans() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->scans;
}

std::vector<std::string> Switch2Reader::take_log() {
    std::lock_guard lock(impl_->sink->mutex);
    std::vector<std::string> out;
    out.swap(impl_->sink->lines);
    return out;
}

std::vector<Switch2Status> Switch2Reader::status() const {
    std::vector<Switch2Status> out;
    std::lock_guard lock(impl_->mutex);
    for (const auto& d : impl_->devices) {
        std::lock_guard device_lock(d->mutex);
        if (d->released || d->unlisted) continue; // (unplugged: its pad goes)
        out.push_back(d->status);
    }
    return out;
}

SDL_JoystickID Switch2Reader::joystick(int number) const {
    std::lock_guard lock(impl_->mutex);
    for (const auto& d : impl_->devices)
        if (d->number == number && Impl::live(*d)) return d->joystick_id;
    return 0;
}

bool Switch2Reader::owns(SDL_JoystickID id) const {
    if (!id) return false;
    std::lock_guard lock(impl_->mutex);
    for (const auto& d : impl_->devices)
        if (d->joystick_id == id) return true;
    return false;
}

void Switch2Reader::set_player(SDL_JoystickID id, int player) {
    std::lock_guard lock(impl_->mutex);
    for (const auto& d : impl_->devices) {
        if (d->joystick_id != id) continue;
        {
            std::lock_guard device_lock(d->mutex);
            if (d->player == player) return;
            d->player = player;
            d->player_changed = true;
        }
        d->wake.notify_all();
    }
}

void Switch2Reader::update() {
    if (impl_->sdl_changes) {
        // SDL counts every device interface that comes or goes.
        const std::uint32_t count = SDL_hid_device_change_count();
        if (!impl_->hid_changes_known || count != impl_->hid_changes) {
            impl_->hid_changes_known = true;
            if (count != impl_->hid_changes) request_scan();
            impl_->hid_changes = count;
        }
    }
    std::vector<std::shared_ptr<Device>> devices;
    {
        std::lock_guard lock(impl_->mutex);
        devices = impl_->devices;
    }
    for (const auto& d : devices) {
        Switch2State state;
        std::optional<Switch2Controls> controls;
        Switch2Calibration calibration;
        bool finished = false, unlisted = false;
        Clock::time_point last_report{};
        {
            std::lock_guard lock(d->mutex);
            state = d->status.state;
            controls = d->controls;
            calibration = d->calibration;
            last_report = d->last_report;
            finished = d->finished;
            unlisted = d->unlisted;
        }
        // (At rest while its reports stall, also while its thread is busy
        // starting it again and still says running.)
        const bool fresh = state == Switch2State::running && controls && Clock::now() - last_report < Switch2Machine::kStall;
        if (d->released) continue;
        const bool gone = finished || unlisted || state == Switch2State::gone;
        if (gone) {
            if (d->joystick) SDL_CloseJoystick(d->joystick);
            if (d->joystick_id) SDL_DetachVirtualJoystick(d->joystick_id);
            d->joystick = nullptr;
            d->joystick_id = 0;
            if (finished) d->released = true;
            continue;
        }
        // A pad from the first report on, kept through a stall or a restart
        // (no player slot moves) until the controller is unplugged.
        if (!d->joystick_id && state == Switch2State::running && controls) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.vendor_id = kSwitch2Vendor;
            desc.product_id = kSwitch2ProProduct;
            desc.name = kSwitch2PadName;
            d->joystick_id = SDL_AttachVirtualJoystick(&desc);
            d->joystick = d->joystick_id ? SDL_OpenJoystick(d->joystick_id) : nullptr;
            d->sent = {};
            if (d->joystick) {
                // Triggers rest at the axis minimum (SDL's virtual triggers).
                SDL_SetJoystickVirtualAxis(d->joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
                SDL_SetJoystickVirtualAxis(d->joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
            }
        }
        if (!d->joystick) continue;
        const Switch2PadState target = fresh && controls ? switch2_pad_state(*controls, calibration) : Switch2PadState{};
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (target.buttons[std::size_t(b)] != d->sent.buttons[std::size_t(b)])
                SDL_SetJoystickVirtualButton(d->joystick, b, target.buttons[std::size_t(b)]);
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            if (target.axes[std::size_t(a)] == d->sent.axes[std::size_t(a)]) continue;
            std::int16_t raw = target.axes[std::size_t(a)];
            if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) raw = std::int16_t(int(raw) * 2 - 32768);
            SDL_SetJoystickVirtualAxis(d->joystick, a, raw);
        }
        d->sent = target;
    }
}

} // namespace dw4::game
