#pragma once

// The Switch 2 Pro Controller (USB 057e:2069) on a cable, read by the port
// itself on Windows (docs/ROUND_SW2WIN_NOTES.md), with no driver to install.
//
// The controller is a composite USB device. Interface 0 is an HID game
// controller that Windows' own HID driver (HidUsb) runs; it carries the
// input reports. Interface 1 is a vendor interface with two bulk endpoints
// through which the controller is started; the controller's Microsoft OS
// descriptor asks Windows for WinUSB with a device interface GUID there, and
// Windows loads it by itself. The controller sends no input report until it
// is started.
//
// SDL 3.4.16 reaches this controller only through libusb (SDL_hidapi.c,
// SDL_libusb_required) and opens it once, when it appears; on Windows that
// first open came too early on the owner's PC and SDL never tried again
// (controllers.log of main 52d504d9). This reader does what Steam and
// Linux's driver do instead: input reports from the HID collection through
// Windows' HID API (hid.dll, ReadFile), the start commands through WinUSB on
// interface 1, each found and opened again whenever Windows' device list
// changes. Every connected controller becomes an SDL virtual gamepad with
// the controller's ids (057e:2069) and the button layout of SDL's own
// Switch 2 driver, so the controller families, the bindings and the prompts
// treat it like any other pad.
//
// The protocol (the start commands, the flash addresses of the calibration,
// the report layout and the stick arithmetic) is taken from SDL 3.4.16's
// src/joystick/hidapi/SDL_hidapi_switch2.c (zlib license, "Code and logic
// contributed by Valve Corporation"; THIRD_PARTY_NOTICES.md), the report
// 0x09 layout from the procon2-mac project's measurement (MIT). Changed for
// this port: commands and replies as data, a plausibility check of the
// factory calibration, the reader's own transport and state machine.
//
// Nothing here reaches the engine, a save or the online wire: the reader
// only makes an SDL pad.
#include "gc_adapter.hpp" // (UsbDriver)

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game {

inline constexpr std::uint16_t kSwitch2Vendor = 0x057e, kSwitch2ProProduct = 0x2069;
// What the virtual pad is called (SDL's own driver says "Nintendo Switch Pro
// Controller", the Switch Pro's name; the ids tell the two apart).
inline constexpr const char* kSwitch2PadName = "Nintendo Switch 2 Pro Controller";

// ---- Commands and replies (interface 1's bulk pipes)
// A command is an 8-byte header (command, 0x91, 0x00 for USB, subcommand,
// 0x00, data length, 0x00, 0x00) and its data. A reply echoes the command
// and subcommand with 0x01 in its second byte (the controller's direction)
// and an acknowledgement in its sixth (0xF8 on USB). (The owner's controller
// answered 01/01 and 08/02 with 0x04 in the second byte, 2026-11-12: a reply
// is any packet whose second byte lacks the host's request bit 0x80.)
struct Switch2Command {
    const char* name; // what the log calls it
    std::vector<std::uint8_t> bytes;
};
// The ten commands SDL's driver sends after reading the flash, in its order:
// the last one ("start output") makes the controller report; "report format
// 0x05" selects the report with every control.
std::vector<Switch2Command> switch2_start_commands();
// Reads 0x40 bytes of the controller's flash (the reply: 0x10 bytes, then the data).
std::vector<std::uint8_t> switch2_flash_read_command(std::uint32_t address);
// The player lights (a pattern of the four LEDs).
std::vector<std::uint8_t> switch2_player_led_command(std::uint8_t pattern);
// SDL's patterns for players 1..8 (`player` 0..7); 0 (all off) for none.
std::uint8_t switch2_player_pattern(int player);
inline constexpr std::size_t kSwitch2FlashReply = 0x50, kSwitch2FlashData = 0x10;

struct Switch2Reply {
    std::uint8_t command = 0, subcommand = 0, status = 0;
    bool from_device = false; // the second byte lacks the request bit 0x80 (0x01; 0x04 seen; a command has 0x91)
};
std::optional<Switch2Reply> parse_switch2_reply(const std::uint8_t* data, std::size_t size);
// Whether a reply answers a command (same command and subcommand, from the
// device). Replies are matched by these ids, not by their order: a reply that
// comes after its wait is a late reply of its own command, not the next one's.
bool switch2_reply_matches(const Switch2Reply& reply, const std::vector<std::uint8_t>& command);

// ---- Stick calibration (the flash)
// One axis: the 12-bit value at rest and the travel to either end.
struct Switch2AxisCalibration {
    std::uint16_t neutral = 0, max = 0, min = 0;
    bool operator==(const Switch2AxisCalibration&) const = default;
};
struct Switch2StickCalibration {
    Switch2AxisCalibration x, y;
    bool operator==(const Switch2StickCalibration&) const = default;
};
// Nine bytes: the rest position, the travel up / right, the travel down /
// left, each two 12-bit values packed as in the reports.
Switch2StickCalibration parse_switch2_stick_calibration(const std::uint8_t* nine);
// Whether a calibration can be used: SDL's test (nothing zero) and a
// plausible one (the rest within the middle of the range, a travel of a few
// hundred steps at least): an erased flash (0xFFF everywhere) is no
// calibration.
bool switch2_calibration_usable(const Switch2StickCalibration& stick);
enum class Switch2CalibrationSource : std::uint8_t { none, factory, user };
const char* switch2_calibration_name(Switch2CalibrationSource source); // "none", "factory", "user"
struct Switch2Calibration {
    Switch2StickCalibration left, right;
    Switch2CalibrationSource left_source = Switch2CalibrationSource::none, right_source = Switch2CalibrationSource::none;
};
// The flash blocks SDL reads: the factory calibration of the left stick in the
// block at 0x13080 and of the right one at 0x130C0 (at 0x28 of each block),
// the user's own (the console's stick calibration) at 0x1FC040 / 0x1FC080
// when the block starts with B2 A1 (at 2). The user's wins when it is usable.
inline constexpr std::uint32_t kSwitch2FlashSerial = 0x13000, kSwitch2FlashLeftFactory = 0x13080, kSwitch2FlashRightFactory = 0x130C0,
                               kSwitch2FlashLeftUser = 0x1FC040, kSwitch2FlashRightUser = 0x1FC080;
// `block`: the 0x40 data bytes. True when it was taken.
bool apply_switch2_factory_stick(Switch2Calibration& calibration, bool right, const std::uint8_t* block);
bool apply_switch2_user_stick(Switch2Calibration& calibration, bool right, const std::uint8_t* block);

// ---- Input reports (interface 0, the report id first as Windows returns it)
inline constexpr std::size_t kSwitch2ReportBytes = 64;
inline constexpr std::uint8_t kSwitch2ReportFull = 0x05; // every control and the motion sensors (the start commands select it)
inline constexpr std::uint8_t kSwitch2ReportPro = 0x09;  // the Pro Controller's default report (without the selection)
namespace switch2_button {
inline constexpr std::uint32_t a = 1u << 0, b = 1u << 1, x = 1u << 2, y = 1u << 3, l = 1u << 4, r = 1u << 5, zl = 1u << 6, zr = 1u << 7,
                               minus = 1u << 8, plus = 1u << 9, home = 1u << 10, capture = 1u << 11, c = 1u << 12, gl = 1u << 13,
                               gr = 1u << 14, left_stick = 1u << 15, right_stick = 1u << 16, up = 1u << 17, down = 1u << 18,
                               left = 1u << 19, right = 1u << 20;
}
// The controls of one report: buttons (switch2_button bits) and the sticks'
// raw 12-bit values (y grows upwards). ZL and ZR are buttons: the Switch 2
// Pro Controller has digital triggers only.
struct Switch2Controls {
    std::uint8_t report_id = 0;
    std::uint32_t buttons = 0;
    std::uint16_t left_x = 2048, left_y = 2048, right_x = 2048, right_y = 2048;
    bool operator==(const Switch2Controls&) const = default;
};
// Report 0x05 (SDL's layout) or 0x09 (procon2-mac's); nullopt for any other
// report or one too short.
std::optional<Switch2Controls> parse_switch2_report(const std::uint8_t* data, std::size_t size);

// ---- As an SDL gamepad
// What the virtual pad reports, as SDL's own Switch 2 driver does with SDL's
// HIDAPI mapping (positional face buttons): B on SOUTH, A on EAST, Y on
// WEST, X on NORTH, Minus BACK, Home GUIDE, Plus START, the stick clicks, L
// and R the shoulders, ZL and ZR the triggers (0 or full), the D-pad,
// Capture MISC1, C MISC2, GR RIGHT_PADDLE1, GL LEFT_PADDLE1. Triggers
// 0..32767 as the gamepad sees them; sticks through the calibration (SDL's
// MapJoystickAxis), y down as SDL's.
struct Switch2PadState {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
    bool operator==(const Switch2PadState&) const = default;
};
std::int16_t switch2_stick_axis(std::uint16_t raw, const Switch2AxisCalibration& calibration, bool usable, bool invert);
Switch2PadState switch2_pad_state(const Switch2Controls& controls, const Switch2Calibration& calibration);

// ---- The transport (Windows: hid.dll and WinUSB; the tests: a fake)
enum class Switch2Io : std::uint8_t {
    ok,
    timeout,
    gone,   // the device seems to have gone away (unplugged; Windows' list decides)
    busy,   // another program holds it (an open only)
    failed, // anything else (`error` says what)
};
const char* switch2_io_name(Switch2Io io);
// What a Windows system error code means here (ERROR_SEM_TIMEOUT, WinUSB's
// pipe timeout: timeout; ERROR_DEVICE_NOT_CONNECTED, ERROR_OPERATION_ABORTED
// ...: gone; ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION: busy; anything
// else: failed). By number, so it is the same on every platform.
Switch2Io switch2_io_of_error(std::uint32_t error);
struct Switch2IoResult {
    Switch2Io io = Switch2Io::failed;
    std::uint32_t error = 0; // the system's error code
    std::size_t bytes = 0;
};
std::string switch2_io_text(const Switch2IoResult& result); // "ok", "timeout", "busy (error 5)", ...

// Interface 0's HID collection: one input report per read.
class Switch2Input {
public:
    virtual ~Switch2Input() = default;
    virtual Switch2IoResult read(std::uint8_t* buffer, std::size_t size, std::chrono::milliseconds timeout) = 0;
    virtual std::string describe() const { return {}; }
};
// Interface 1's bulk pipes.
class Switch2Commands {
public:
    virtual ~Switch2Commands() = default;
    virtual Switch2IoResult send(const std::uint8_t* data, std::size_t size) = 0;
    virtual Switch2IoResult receive(std::uint8_t* buffer, std::size_t size, std::chrono::milliseconds timeout) = 0;
    // Drops what an earlier owner left unread.
    virtual void flush() {}
    virtual std::string describe() const { return {}; }
};

// What Windows lists of one controller on a cable.
struct Switch2Found {
    std::string id;            // the USB device's instance id
    bool input_node = false;   // interface 0 is listed
    std::string input_service; // its driver ("HidUsb")
    std::string input_path;    // its HID collection's device path (empty: none listed)
    bool command_node = false; // interface 1 is listed
    UsbDriver command_driver = UsbDriver::unknown;
    std::string command_service;
    bool command_guid = false; // its registry key names a device interface GUID
    std::string command_path;  // its WinUSB device path (empty: none listed)
    bool operator==(const Switch2Found&) const = default;
};
std::string switch2_found_line(const Switch2Found& found); // for the log

class Switch2Platform {
public:
    virtual ~Switch2Platform() = default;
    // Every Switch 2 Pro Controller on USB.
    virtual std::vector<Switch2Found> scan() = 0;
    virtual std::unique_ptr<Switch2Input> open_input(const Switch2Found& found, Switch2IoResult& result) = 0;
    virtual std::unique_ptr<Switch2Commands> open_commands(const Switch2Found& found, Switch2IoResult& result) = 0;
};
// Windows' HID and WinUSB (null elsewhere).
std::unique_ptr<Switch2Platform> make_windows_switch2_platform();

// ---- One controller
enum class Switch2State : std::uint8_t {
    opening,     // opening its HID input, then interface 1 (after a lost channel: a second later)
    no_input,    // its HID input is not listed or cannot be opened
    starting,    // reading the calibration, sending the start commands (one step)
    waiting,     // started (or another program holds interface 1): no input report yet
    running,     // input reports arrive: a pad
    no_commands, // interface 1 cannot be opened (its driver, or another program), and no report came by itself
    no_reports,  // started, but no input report came
    gone,        // unplugged: Windows no longer lists it
};
const char* switch2_state_name(Switch2State state);

struct Switch2Status {
    int number = 1;     // 1, 2, ... (the log's "switch 2 pro 2:")
    Switch2State state = Switch2State::opening;
    Switch2Found found; // what Windows lists of it
    Switch2IoResult input_open, command_open; // the last opens' results
    bool commands = false;   // interface 1 is open
    bool started = false;    // this reader's start commands went out
    int starts = 0;          // how often
    std::string failed_step; // a start command that could not be sent
    Switch2IoResult failed_result;
    std::uint64_t reports = 0;
    std::uint8_t report_id = 0;
    double rate = 0; // input reports per second (the last measure)
    Switch2CalibrationSource left_calibration = Switch2CalibrationSource::none, right_calibration = Switch2CalibrationSource::none;
};

// The state machine of one controller. step() does at most one wait on the
// device (the start sequence counts as one) and returns when to step next;
// it decides by `now` alone, so a test drives it with its own clock.
//
// Only Windows' list says that a controller is gone (listed(nullopt)). A
// read or a command that fails as if it were unplugged (an aborted read over
// standby, a reset port) closes both channels, asks for a look at the list
// (take_scan_request) and opens them again a second later while the list
// still has the controller: it plays on without being plugged in again.
class Switch2Machine {
public:
    using Clock = std::chrono::steady_clock;
    Switch2Machine(Switch2Platform& platform, Switch2Found found, int number = 1);
    ~Switch2Machine();
    Switch2Machine(const Switch2Machine&) = delete;
    Switch2Machine& operator=(const Switch2Machine&) = delete;
    Clock::time_point step(Clock::time_point now);
    // What Windows lists of it now (nullopt: no longer listed: gone).
    void listed(const std::optional<Switch2Found>& found);
    // The player lights for a player (0..3; -1 none): sent when interface 1 is open.
    void set_player(int player);
    const Switch2Status& status() const { return status_; }
    // The latest controls (none before the first report) and the calibration.
    const std::optional<Switch2Controls>& controls() const { return controls_; }
    const Switch2Calibration& calibration() const { return calibration_; }
    // Whether the last report is recent (a pad at rest otherwise), and when it came.
    bool fresh(Clock::time_point now) const;
    Clock::time_point last_report() const;
    // The log's lines since the last call ("switch 2 pro: ...").
    std::vector<std::string> take_log();
    // Whether a channel was lost since the last call: the owner then looks at
    // Windows' list (listed()) to tell an unplugged controller from a hiccup.
    bool take_scan_request();
    // Closes both channels (the reader's stop, the end of a test).
    void close();

    // Timing (also the tests').
    static constexpr std::chrono::milliseconds kReadWait{100};     // one report read
    static constexpr std::chrono::milliseconds kReplyWait{100};    // one reply read (SDL's)
    static constexpr std::chrono::milliseconds kFirstReport{1500}; // after the start
    static constexpr std::chrono::milliseconds kStall{1000};       // no report while running
    static constexpr std::chrono::milliseconds kRetry{2000};       // opening again
    static constexpr std::chrono::milliseconds kLostPause{1000};   // a lost channel: opening again (unless unplugged)
    static constexpr int kStartTries = 3;                          // start sequences before no_reports
    static constexpr int kLateReplies = 10;                        // late replies taken while one command waits for its own
    static constexpr std::chrono::seconds kRateWindow{2};

private:
    void log(const std::string& line);
    void open(Clock::time_point now);
    // `verbose`: every command's reply to the log (a retry every ten
    // seconds says only how many were answered).
    void start(Clock::time_point now, bool read_flash, bool verbose);
    void read(Clock::time_point now);
    void report(const std::uint8_t* data, std::size_t size, Clock::time_point now);
    void lost(const Switch2IoResult& result, const std::string& where, Clock::time_point now);
    // Sends a command and reads its reply (got 0: none within kReplyWait).
    // `late` (optional) takes a reply that is not this command's: true when it
    // was the late reply of an earlier command (the read then goes on for this
    // one's, at most kLateReplies times); false keeps it as the reply read.
    using LateReply = std::function<bool(const std::uint8_t*, std::size_t)>;
    bool exchange(const std::vector<std::uint8_t>& command, std::uint8_t* reply, std::size_t reply_size, std::size_t& got,
                  Switch2IoResult& sent, const LateReply& late = {});
    // Reads one reply (SDL's RecvBulkData: 64 bytes at a time until a short
    // packet); false when the controller is lost (`result` says).
    bool receive_reply(std::uint8_t* reply, std::size_t reply_size, std::size_t& got, Switch2IoResult& result);
    void send_led(Clock::time_point now);

    Switch2Platform& platform_;
    std::optional<Switch2Found> listed_; // the newest list entry
    Switch2Status status_;
    std::unique_ptr<Switch2Input> input_;
    std::unique_ptr<Switch2Commands> commands_;
    std::optional<Switch2Controls> controls_;
    Switch2Calibration calibration_;
    std::vector<std::string> log_;
    Clock::time_point deadline_{}, retry_at_{}, last_report_{}, rate_since_{}, started_at_{};
    std::uint64_t rate_reports_ = 0;
    std::uint8_t unknown_id_ = 0;
    bool first_logged_ = false, no_reports_logged_ = false;
    bool rate_logged_ = false;
    bool scan_request_ = false;
    double logged_rate_ = 0;
    int player_ = -1, led_sent_ = -2;
    std::array<std::uint8_t, kSwitch2ReportBytes> buffer_{};
};

// ---- Every controller, read in the background
// One thread looks at Windows' device list when asked (SDL's device change
// count: update()), when a controller lost a channel, and every two seconds
// while a controller does not play; one thread per controller runs its
// machine. A controller the list no longer has is gone; one listed again
// (also under the same instance id, at once) starts afresh. The main
// thread's update() keeps one SDL virtual gamepad per playing controller
// (attached at its first report, kept at rest through a stall, a restart or
// a lost channel, detached when it is unplugged) and hands the log's lines
// over. Nothing changes while the game's window is in the background: SDL
// ignores a pad's presses then, as for every other pad, and the controller
// stays started.
class Switch2Reader {
public:
    static constexpr std::chrono::milliseconds kRescan{2000};
    // `platform`: make_windows_switch2_platform() in the game. `sdl_changes`:
    // ask for a look at the device list when SDL counts a device change (off
    // in tests, which call request_scan()). `rescan`: how often the list is
    // looked at while a controller does not play (tests set it long to see
    // the other reasons for a look alone).
    explicit Switch2Reader(std::unique_ptr<Switch2Platform> platform, bool sdl_changes = true,
                           std::chrono::milliseconds rescan = kRescan);
    ~Switch2Reader();
    Switch2Reader(const Switch2Reader&) = delete;
    Switch2Reader& operator=(const Switch2Reader&) = delete;
    void request_scan();
    // Main thread, once per frame (after SDL's gamepad subsystem is up).
    void update();
    // Every connected controller's status.
    std::vector<Switch2Status> status() const;
    // How many looks at the device list there were (tests).
    std::uint32_t scans() const;
    // The log's lines since the last call.
    std::vector<std::string> take_log();
    // The virtual pad of a playing controller (0: none) by its status number.
    SDL_JoystickID joystick(int number) const;
    bool owns(SDL_JoystickID id) const;
    // The player lights of the controller behind a pad (player 0..3).
    void set_player(SDL_JoystickID id, int player);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dw4::game
