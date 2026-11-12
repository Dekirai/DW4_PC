#pragma once

// USB access for the controllers (docs/ROUND_SW2WIN_NOTES.md): the SDL hints
// that decide which code opens which USB device, what F1 > Controls and a
// notice say about a connected Switch 2 Pro Controller, and the controller
// log (saves/controllers.log).
//
// SDL 3.4.16 reaches a few devices only through libusb (SDL_hidapi.c,
// SDL_libusb_required: the GameCube adapter 057e:0337, the Switch 2 Pro
// Controller 057e:2069, the Switch 2 Joy-Con and GameCube controller); its
// Windows HID path skips them. The port reads two of them itself, with
// Windows' own drivers and no libusb: the GameCube adapter in Wii U mode
// (gc_adapter.hpp: WinUSB, which Zadig installs as for Dolphin) and the
// Switch 2 Pro Controller (switch2_pro.hpp: Windows' HID driver on its
// interface 0 for the input, the WinUSB driver Windows loads by itself on its
// interface 1 for the start commands). SDL is built without libusb and told
// never to load a libusb-1.0.dll (one left over from an earlier build included),
// so exactly one code path opens each device, and every other controller
// keeps its path (Windows HID, XInput, RawInput, ...).
//
// Test runs (--smoke-test) start neither reader (no controller of this PC
// takes part in them); DW4_TEST_SWITCH2=1 starts the Switch 2 Pro reader in a
// hidden test run (its look at Windows' device list, as in play).

#include "gc_adapter.hpp"
#include "pad_family.hpp"
#include "settings.hpp"
#include "switch2_pro.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_log.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

// ---- libusb (no longer used)
// Whether a libusb-1.0.dll is loaded in this process now (it must not be:
// SDL is built without libusb and told not to load one).
bool libusb_in_process();
// A libusb-1.0.dll beside the program (left over from an earlier build, which
// shipped one): its path, empty when there is none. Never loaded.
std::filesystem::path leftover_libusb_dll();

// The controller hints before SDL_Init (host_view.cpp controller_hints).
struct SdlHint {
    const char* name;
    const char* value;
};
std::vector<SdlHint> controller_hint_values(const Settings& settings);

// Who opens the GameCube adapter in Wii U mode (057e:0337): the port's
// WinUSB reader (gc_adapter.hpp) while the setting allows it outside test
// runs; never SDL (its Windows HID path skips the adapter, and it has no
// libusb).
enum class AdapterOwner : std::uint8_t { nobody, port_reader };
AdapterOwner gamecube_adapter_owner(bool gc_adapter_setting, bool test_run);
// Who opens the Switch 2 Pro Controller (057e:2069): the port's reader
// (switch2_pro.hpp) outside test runs (or with DW4_TEST_SWITCH2); never SDL.
AdapterOwner switch2_pro_owner(bool test_run, bool test_switch2);

// ---- The Switch 2 Pro Controller's notice
enum class Switch2Notice : std::uint8_t {
    none,              // none connected (or the reader does not run: a test run)
    playable,          // the reader plays it
    bluetooth,         // over Bluetooth only, as an input device (the reader reads it on a cable)
    no_input,          // on USB, but its HID input (interface 0) is not there or cannot be opened
    no_driver,         // its interface 1 has no driver (Windows did not load WinUSB)
    other_driver,      // its interface 1 has another driver than WinUSB (libusbK, libusb0, ...)
    no_interface_guid, // WinUSB on interface 1 without a device interface GUID: nothing to open
    no_interface,      // Windows lists no interface 1 of it (a driver on the whole controller?)
    busy,              // another program holds interface 1 (and no input report comes) or the HID input
    no_reports,        // started (or a start command could not be sent), but no input report came
};
const char* switch2_notice_name(Switch2Notice notice);
// One controller's notice by the reader's state (none while it opens or
// starts: the grace below covers that).
Switch2Notice switch2_device_notice(const Switch2Status& device);
// What applies now: `windows` the device list (gc_adapter.hpp: Bluetooth),
// `reader` every controller the reader has (switch2_pro.hpp), `reader_on`
// whether the reader runs. A controller that does not play wins over one
// that does (its notice says what to do).
Switch2Notice switch2_notice(const UsbFindings& windows, const std::vector<Switch2Status>& reader, bool reader_on);
// The controller a notice is about (null for none / playable / bluetooth).
const Switch2Status* switch2_notice_device(Switch2Notice notice, const std::vector<Switch2Status>& reader);
// F1 > Controls (what is wrong and what to do; empty for none / playable)
// and the short notice in play. `device`: the controller it is about.
std::string switch2_notice_text(Switch2Notice notice, const Switch2Status* device);
std::string switch2_toast_text(Switch2Notice notice);
// F1 > Controls: one line per connected controller ("Switch 2 Pro
// Controller (USB): playing, 250 reports/s, ...").
std::string switch2_status_line(const Switch2Status& device);

// The reader opens the controller a moment after Windows lists it: a notice
// other than playable counts only once it held kSwitch2Grace.
inline constexpr std::chrono::milliseconds kSwitch2Grace{3000};
class Switch2Watch {
public:
    Switch2Notice update(std::chrono::steady_clock::time_point now, Switch2Notice notice);
    Switch2Notice current() const { return shown_; }

private:
    Switch2Notice pending_ = Switch2Notice::none, shown_ = Switch2Notice::none;
    std::chrono::steady_clock::time_point since_{};
};

// ---- saves/controllers.log
// What to send in when a controller does not work: the program's version,
// SDL's, every pad that comes and goes (name, USB ids, family, SDL's
// backend, SDL's device path), what Windows lists of the Switch 2 Pro
// Controller and the GameCube adapter, every step of the Switch 2 Pro
// reader (switch2_pro.hpp), the notices, and SDL's own input messages.
// Written anew at every start, outside test runs.
class ControllerLog {
public:
    ControllerLog() = default;
    ~ControllerLog();
    ControllerLog(const ControllerLog&) = delete;
    ControllerLog& operator=(const ControllerLog&) = delete;
    bool open(const std::filesystem::path& file);
    bool is_open() const;
    const std::filesystem::path& path() const { return path_; }
    // One line with the seconds since the log opened.
    void write(std::string_view line);
    // SDL's input messages (all priorities) into this log too, from now on
    // until the log closes.
    void capture_sdl();

private:
    static void SDLCALL sdl_output(void* self, int category, SDL_LogPriority priority, const char* message);
    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::filesystem::path path_;
    std::chrono::steady_clock::time_point start_{};
    std::size_t lines_ = 0;
    bool capturing_ = false;
    SDL_LogOutputFunction previous_ = nullptr;
    void* previous_data_ = nullptr;
    SDL_LogPriority previous_priority_ = SDL_LOG_PRIORITY_ERROR;
};
// A pad's line: name, ids, family, SDL's backend and device path.
std::string pad_log_line(SDL_Gamepad* pad, PadFamily family);
// SDL's backend of a pad by its GUID's driver signature ("HIDAPI",
// "XInput", "RawInput", "Windows.Gaming.Input", "virtual", ...).
const char* sdl_backend_name(const SDL_GUID& guid);
// The device list's part for the log, and one line per Switch 2 Pro
// Controller node Windows lists ("windows node: <hardware id> [<service>]").
std::string usb_findings_line(const UsbFindings& findings);
std::vector<std::string> usb_findings_node_lines(const UsbFindings& findings);

} // namespace dw4::game
