#pragma once

// USB access for the controllers (docs/ROUND_LIBUSB_NOTES.md): the
// libusb-1.0.dll SDL loads at run time for the Switch 2 Pro Controller, the
// SDL hints that decide which code opens which USB device, what F1 >
// Controls and a notice say about a connected Switch 2 Pro Controller, and
// the controller log (saves/controllers.log).
//
// SDL 3.4.16 reaches a few devices only through libusb (SDL_hidapi.c,
// SDL_libusb_required: the GameCube adapter 057e:0337, the Switch 2 Pro
// Controller 057e:2069, the Switch 2 Joy-Con and GameCube controller); its
// Windows HID path skips them. Every other controller keeps its path
// (Windows HID, XInput, RawInput, ...): SDL_HIDAPI_LIBUSB_WHITELIST stays
// on. The Switch 2 Pro Controller is a composite USB device: interface 0 is
// HID (Windows' own HID driver; libusb reads it through its HID backend),
// interface 1 is a vendor interface with two bulk endpoints that SDL's
// Switch 2 driver claims to start the controller (SDL_hidapi_switch2.c
// HIDAPI_DriverSwitch2_InitUSB). libusb can claim that interface only with
// the WinUSB driver on it and a device interface GUID in its registry key
// (libusb's Windows backend finds composite interfaces through that GUID).
// A descriptor dump of the controller (VIIPER) carries a Microsoft OS
// descriptor that asks Windows for both; not yet checked with the device.
// Otherwise Zadig installs WinUSB there (its INF writes the GUID).
//
// Without the DLL SDL notes "Couldn't load libusb" and goes on with Windows'
// HID: the game starts and every other controller works as before.
//
// Ownership: SDL's GameCube driver would open the Wii U mode adapter
// through libusb as well; the port's WinUSB reader (gc_adapter.hpp) keeps
// it (SDL_HIDAPI_LIBUSB_GAMECUBE=0), so exactly one code path opens it, with
// or without the DLL, and it is still let go in the background for Dolphin.
//
// Test runs (--smoke-test) never let SDL use libusb (no controller of this
// PC takes part in them); they still look for the DLL and print it.

#include "gc_adapter.hpp"
#include "pad_family.hpp"
#include "settings.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_log.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

// The DLL SDL loads (SDL_LIBUSB_DYNAMIC of this build's SDL).
inline constexpr const char* kLibusbDll = "libusb-1.0.dll";
// This build's SDL was configured with libusb (CMake DW4_LIBUSB).
bool sdl_has_libusb();
// The functions SDL 3.4.16 loads from the DLL (SDL_libusb.c), without their
// "libusb_" prefix; SDL uses the DLL only when every one is there.
std::span<const char* const> sdl_libusb_functions();

enum class LibusbState : std::uint8_t {
    not_built,    // this build's SDL has no libusb
    off,          // SDL's libusb is off (test runs)
    missing,      // no libusb-1.0.dll where Windows looks (the game's folder first)
    incompatible, // a libusb-1.0.dll without a function SDL needs (too old, or not libusb)
    loaded,       // SDL can use it
};
struct LibusbInfo {
    LibusbState state = LibusbState::not_built;
    std::string path;    // where Windows found it
    std::string version; // "1.0.30.12037"
    std::string missing; // the first function it lacks (incompatible)
};
// Looks for `dll` as SDL_LoadObject does (Windows' LoadLibrary search: the
// program's folder first), checks every function SDL loads and reads its
// version, then lets it go again. `built`: sdl_has_libusb(); `enabled`:
// SDL's libusb hint (off in test runs: the DLL is still looked for).
LibusbInfo probe_libusb(bool built, bool enabled, const char* dll = kLibusbDll);
const char* libusb_state_name(LibusbState state); // "loaded", "missing", ...
// Whether a libusb-1.0.dll is loaded in this process now (SDL keeps it
// while its HID layer runs).
bool libusb_in_process();
// One line for the console, the log and F1 > Controls.
std::string libusb_summary(const LibusbInfo& info);

// The controller hints before SDL_Init (host_view.cpp controller_hints).
struct SdlHint {
    const char* name;
    const char* value;
};
std::vector<SdlHint> controller_hint_values(const Settings& settings, bool test_run);

// Who opens the GameCube adapter in Wii U mode (057e:0337): the port's
// WinUSB reader (gc_adapter.hpp) while the setting allows it outside test
// runs; never SDL (its libusb path is told to leave the adapter and its
// Windows HID path skips it), with or without libusb-1.0.dll.
enum class AdapterOwner : std::uint8_t { nobody, port_reader };
AdapterOwner gamecube_adapter_owner(bool gc_adapter_setting, bool test_run);

// ---- The Switch 2 Pro Controller
// A pad SDL's own Switch 2 driver opened (057e:2069 with SDL's HIDAPI
// GUID): the controller plays.
bool sdl_switch2_pad(const PadIdentity& identity, const SDL_GUID& guid);
bool sdl_switch2_pad(SDL_Gamepad* pad);

enum class Switch2Notice : std::uint8_t {
    none,             // none connected (or a test run)
    playable,         // SDL drives it: a player's pad
    not_built,        // this build's SDL has no libusb
    dll_missing,      // libusb-1.0.dll is missing
    dll_incompatible, // the libusb-1.0.dll found lacks what SDL needs
    bluetooth,        // over Bluetooth only, as an input device (SDL drives it on a cable only)
    no_driver,        // its interface 1 has no driver (Windows did not load WinUSB)
    no_interface_guid, // WinUSB on interface 1 without a device interface GUID: libusb cannot reach it
    other_driver,     // its interface 1 has another driver than WinUSB, and it does not play
    no_interface,     // on USB, but Windows lists no interface 1 of it (a driver on the whole device?)
    not_opened,       // WinUSB with its GUID is there, yet SDL made no pad of it (another program holds it, or its start failed)
};
const char* switch2_notice_name(Switch2Notice notice);
// What applies now: `windows` the device list (gc_adapter.hpp), `sdl_pad`
// whether SDL opened it (sdl_switch2_pad), `libusb` the DLL's state.
Switch2Notice switch2_notice(const UsbFindings& windows, bool sdl_pad, LibusbState libusb);
// F1 > Controls (what is wrong and what to do; empty for none / playable)
// and the short notice in play.
std::string switch2_notice_text(Switch2Notice notice, const UsbFindings& windows);
std::string switch2_toast_text(Switch2Notice notice);

// SDL opens the controller a moment after Windows lists it: a notice other
// than playable counts only once it held kSwitch2Grace.
inline constexpr std::chrono::milliseconds kSwitch2Grace{3000};
class Switch2Watch {
public:
    Switch2Notice update(std::chrono::steady_clock::time_point now, const UsbFindings& windows, bool sdl_pad, LibusbState libusb);
    Switch2Notice current() const { return shown_; }

private:
    Switch2Notice pending_ = Switch2Notice::none, shown_ = Switch2Notice::none;
    std::chrono::steady_clock::time_point since_{};
};

// ---- saves/controllers.log
// What to send in when a controller does not work: the program's version,
// SDL's, libusb-1.0.dll's state, every pad that comes and goes (name, USB
// ids, family, SDL's backend, SDL's device path), what Windows lists of the
// Switch 2 Pro Controller and the GameCube adapter, the notices, and SDL's
// own input messages (devices it adds, libusb loading, the Switch 2
// driver's warnings). Written anew at every start, outside test runs.
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
