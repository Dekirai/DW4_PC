#include "pad_usb.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_loadso.h>

#include <array>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dw4::game {

namespace {
// SDL_libusb.c's LOAD_LIBUSB_SYMBOL list (SDL 3.4.16), in its order.
constexpr std::array<const char*, 35> kSdlLibusbFunctions{
    "init", "exit", "get_device_list", "free_device_list", "get_device_descriptor", "get_active_config_descriptor",
    "get_config_descriptor", "free_config_descriptor", "get_bus_number", "get_port_numbers", "get_device_address", "open", "close",
    "get_device", "claim_interface", "release_interface", "kernel_driver_active", "detach_kernel_driver", "attach_kernel_driver",
    "set_auto_detach_kernel_driver", "set_interface_alt_setting", "alloc_transfer", "submit_transfer", "cancel_transfer", "free_transfer",
    "control_transfer", "interrupt_transfer", "bulk_transfer", "handle_events", "handle_events_completed", "interrupt_event_handler",
    "has_capability", "hotplug_register_callback", "hotplug_deregister_callback", "error_name",
};

// libusb.h's struct libusb_version (libusb_get_version, since 1.0.10).
struct LibusbVersion {
    std::uint16_t major, minor, micro, nano;
    const char* rc;
    const char* describe;
};
#ifdef _WIN32
using GetVersion = const LibusbVersion*(__stdcall*)();
#else
using GetVersion = const LibusbVersion* (*)();
#endif

// (Driver service names are ASCII.)
std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) out += c < 0x80 ? char(c) : '?';
    return out;
}
} // namespace

bool sdl_has_libusb() {
#ifdef DW4_SDL_LIBUSB
    return true;
#else
    return false;
#endif
}

std::span<const char* const> sdl_libusb_functions() { return kSdlLibusbFunctions; }

LibusbInfo probe_libusb(bool built, bool enabled, const char* dll) {
    LibusbInfo info;
    // (SDL_LoadObject is SDL's own loader: LoadLibrary by name on Windows.
    // Looked for even when SDL may not use it: the log says what is there.)
    SDL_SharedObject* handle = SDL_LoadObject(dll);
    if (!handle) {
        info.state = !built ? LibusbState::not_built : !enabled ? LibusbState::off : LibusbState::missing;
        return info;
    }
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(handle), path.data(), DWORD(path.size()));
    path.resize(length < path.size() ? length : 0);
    if (!path.empty()) { // (UTF-8 for the log and ImGui)
        const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            info.path.resize(std::size_t(size));
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), int(path.size()), info.path.data(), size, nullptr, nullptr);
        }
    }
#endif
    for (const char* function : kSdlLibusbFunctions)
        if (!SDL_LoadFunction(handle, (std::string("libusb_") + function).c_str())) {
            info.missing = std::string("libusb_") + function;
            break;
        }
    if (const auto get_version = reinterpret_cast<GetVersion>(SDL_LoadFunction(handle, "libusb_get_version")))
        if (const LibusbVersion* version = get_version())
            info.version = std::to_string(version->major) + "." + std::to_string(version->minor) + "." + std::to_string(version->micro) + "." +
                           std::to_string(version->nano) + (version->rc ? version->rc : "");
    SDL_UnloadObject(handle);
    if (!built) info.state = LibusbState::not_built;
    else if (!info.missing.empty()) info.state = LibusbState::incompatible;
    else info.state = enabled ? LibusbState::loaded : LibusbState::off;
    return info;
}

bool libusb_in_process() {
#ifdef _WIN32
    return GetModuleHandleW(L"libusb-1.0.dll") != nullptr;
#else
    return false;
#endif
}

const char* libusb_state_name(LibusbState state) {
    switch (state) {
    case LibusbState::not_built: return "not built";
    case LibusbState::off: return "off";
    case LibusbState::missing: return "missing";
    case LibusbState::incompatible: return "incompatible";
    case LibusbState::loaded: return "loaded";
    }
    return "";
}

std::string libusb_summary(const LibusbInfo& info) {
    std::string found = info.path.empty() ? std::string(kLibusbDll) : info.path;
    if (!info.version.empty()) found += " " + info.version;
    switch (info.state) {
    case LibusbState::not_built: return "libusb: not in this build (the Switch 2 Pro Controller cannot be used)";
    case LibusbState::off: return "libusb: off in test runs (" + (info.path.empty() ? std::string(kLibusbDll) + " not found" : found) + ")";
    case LibusbState::missing:
        return std::string("libusb: ") + kLibusbDll + " not found: the Switch 2 Pro Controller cannot be used, every other controller works";
    case LibusbState::incompatible: return "libusb: " + found + " lacks " + info.missing + ": SDL cannot use it (Switch 2 Pro Controller unavailable)";
    case LibusbState::loaded: return "libusb: " + found + " (Switch 2 Pro Controller over USB)";
    }
    return {};
}

std::vector<SdlHint> controller_hint_values(const Settings& settings, bool test_run) {
    return {
        // The Wii U Pro Controller: SDL's Wii driver (SDL leaves it off: it
        // does not work behind a Mayflash DolphinBar). wii_driver=on starts
        // with it; automatic (the default) turns it on only while a Wii U
        // Pro Controller's ids are connected (update_controllers); off never.
        {SDL_HINT_JOYSTICK_HIDAPI_WII, settings.wii_driver == WiiDriver::on ? "1" : "0"},
        // PlayStation, Switch and Switch 2 pads: SDL's defaults (on).
        // Bluetooth PlayStation pads stay in their simple report mode until
        // a feature needs more (nothing here does).
        {SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "auto"},
        // libusb-1.0.dll for the devices SDL reaches only through it (the
        // Switch 2 Pro Controller); never in test runs.
        {SDL_HINT_HIDAPI_LIBUSB, test_run ? "0" : "1"},
        // Only those devices (SDL's default on Windows, stated): every other
        // controller keeps Windows' own drivers.
        {SDL_HINT_HIDAPI_LIBUSB_WHITELIST, "1"},
        // The GameCube adapter in Wii U mode stays the port's WinUSB
        // reader's (gc_adapter.hpp): one code path opens it.
        {SDL_HINT_HIDAPI_LIBUSB_GAMECUBE, "0"},
    };
}

AdapterOwner gamecube_adapter_owner(bool gc_adapter_setting, bool test_run) {
    return gc_adapter_setting && !test_run ? AdapterOwner::port_reader : AdapterOwner::nobody;
}

bool sdl_switch2_pad(const PadIdentity& identity, const SDL_GUID& guid) {
    // 'h': SDL's HIDAPI drivers (SDL_hidapijoystick.c). A RawInput or
    // DirectInput joystick with the same ids would get no input: nothing
    // started the controller.
    return identity.vendor == kVendorNintendo && identity.product == kProductSwitch2Pro && guid.data[14] == 'h';
}

bool sdl_switch2_pad(SDL_Gamepad* pad) {
    if (!pad) return false;
    SDL_Joystick* joystick = SDL_GetGamepadJoystick(pad);
    return joystick && sdl_switch2_pad(pad_identity(pad), SDL_GetJoystickGUID(joystick));
}

const char* switch2_notice_name(Switch2Notice notice) {
    switch (notice) {
    case Switch2Notice::none: return "none";
    case Switch2Notice::playable: return "playable";
    case Switch2Notice::not_built: return "no libusb in this build";
    case Switch2Notice::dll_missing: return "libusb-1.0.dll missing";
    case Switch2Notice::dll_incompatible: return "libusb-1.0.dll unusable";
    case Switch2Notice::bluetooth: return "Bluetooth only";
    case Switch2Notice::no_driver: return "no driver on interface 1";
    case Switch2Notice::other_driver: return "another driver on interface 1";
    case Switch2Notice::not_opened: return "not opened by SDL";
    }
    return "";
}

Switch2Notice switch2_notice(const UsbFindings& windows, bool sdl_pad, LibusbState libusb) {
    if (sdl_pad) return Switch2Notice::playable;
    if (!windows.switch2_usb && !windows.switch2_bluetooth) return Switch2Notice::none;
    if (libusb == LibusbState::off) return Switch2Notice::none; // (test runs)
    if (!windows.switch2_usb) return Switch2Notice::bluetooth;
    switch (libusb) {
    case LibusbState::not_built: return Switch2Notice::not_built;
    case LibusbState::missing: return Switch2Notice::dll_missing;
    case LibusbState::incompatible: return Switch2Notice::dll_incompatible;
    default: break;
    }
    switch (windows.switch2_vendor) {
    case UsbDriver::none: return Switch2Notice::no_driver;
    case UsbDriver::other: return Switch2Notice::other_driver;
    default: return Switch2Notice::not_opened;
    }
}

std::string switch2_notice_text(Switch2Notice notice, const UsbFindings& windows) {
    const std::string zadig = "Zadig (zadig.akeo.ie): Options > List All Devices, pick the entry whose USB ID ends in 057E 2069 01 (the "
                              "controller's interface 1; never the one ending in 00), choose WinUSB and install it";
    switch (notice) {
    case Switch2Notice::none:
    case Switch2Notice::playable: return {};
    case Switch2Notice::not_built:
        return "Switch 2 Pro Controller found, but this build has no libusb support, which the controller needs. "
               "Use a build that comes with libusb-1.0.dll.";
    case Switch2Notice::dll_missing:
        return std::string("Switch 2 Pro Controller found, but ") + kLibusbDll +
               " is missing: the game starts this controller through it. Put the libusb-1.0.dll that comes with the game back next to "
               "the game's .exe and start the game again. Every other controller works without it.";
    case Switch2Notice::dll_incompatible:
        return std::string("Switch 2 Pro Controller found, but the ") + kLibusbDll +
               " next to the game is one SDL cannot use (too old, or another program's file). Replace it with the libusb-1.0.dll that "
               "comes with the game and start the game again.";
    case Switch2Notice::bluetooth:
        return "Switch 2 Pro Controller found over Bluetooth. It works over a USB cable only (SDL cannot read it over Bluetooth yet): "
               "connect it with a USB-C data cable.";
    case Switch2Notice::no_driver:
        return "Switch 2 Pro Controller found, but Windows blocks it: there is no driver on its interface 1, through which the game "
               "starts it (Windows normally puts WinUSB there by itself). Install WinUSB there with " +
               zadig + ", then unplug the controller and plug it in again.";
    case Switch2Notice::other_driver:
        return "Switch 2 Pro Controller found; its interface 1 has the " + narrow(windows.switch2_vendor_service) +
               " driver instead of WinUSB, and the controller did not start through it. Replace that driver with WinUSB in " + zadig +
               ", then unplug the controller and plug it in again.";
    case Switch2Notice::not_opened:
        return "Switch 2 Pro Controller found (WinUSB on its interface 1), but SDL could not open it. Another program may be using it: "
               "Steam with its Switch controller support, an emulator, or a second copy of this game. Close it, then unplug the "
               "controller and plug it in again. If it stays like this, send saves/controllers.log.";
    }
    return {};
}

std::string switch2_toast_text(Switch2Notice notice) {
    switch (notice) {
    case Switch2Notice::none:
    case Switch2Notice::playable: return {};
    case Switch2Notice::not_built: return "Switch 2 Pro Controller found: this build cannot read it (F1 > Controls).";
    case Switch2Notice::dll_missing: return "Switch 2 Pro Controller found: libusb-1.0.dll is missing (F1 > Controls).";
    case Switch2Notice::dll_incompatible: return "Switch 2 Pro Controller found: libusb-1.0.dll cannot be used (F1 > Controls).";
    case Switch2Notice::bluetooth: return "Switch 2 Pro Controller: connect it with a USB cable (F1 > Controls).";
    case Switch2Notice::no_driver:
    case Switch2Notice::other_driver: return "Switch 2 Pro Controller found: Windows blocks it, it needs WinUSB (F1 > Controls).";
    case Switch2Notice::not_opened: return "Switch 2 Pro Controller found, but it could not be opened (F1 > Controls).";
    }
    return {};
}

Switch2Notice Switch2Watch::update(std::chrono::steady_clock::time_point now, const UsbFindings& windows, bool sdl_pad, LibusbState libusb) {
    const Switch2Notice notice = switch2_notice(windows, sdl_pad, libusb);
    if (notice == Switch2Notice::none || notice == Switch2Notice::playable) {
        pending_ = shown_ = notice;
        return shown_;
    }
    if (notice != pending_) {
        pending_ = notice;
        since_ = now;
        shown_ = Switch2Notice::none;
    }
    if (now - since_ >= kSwitch2Grace) shown_ = pending_;
    return shown_;
}

// ---- saves/controllers.log

ControllerLog::~ControllerLog() {
    if (capturing_) {
        SDL_SetLogOutputFunction(previous_, previous_data_);
        SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, previous_priority_);
    }
    std::lock_guard lock(mutex_);
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

bool ControllerLog::open(const std::filesystem::path& file) {
    std::error_code error;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), error);
    std::lock_guard lock(mutex_);
    if (file_) std::fclose(file_);
#ifdef _WIN32
    file_ = _wfopen(file.c_str(), L"w");
#else
    file_ = std::fopen(file.c_str(), "w");
#endif
    path_ = file;
    start_ = std::chrono::steady_clock::now();
    lines_ = 0;
    if (!file_) return false;
    const std::time_t now = std::time(nullptr);
    char stamp[64]{};
    if (const std::tm* local = std::localtime(&now)) std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", local);
    std::fprintf(file_, "controllers log, started %s\n", stamp);
    std::fflush(file_);
    return true;
}

bool ControllerLog::is_open() const {
    std::lock_guard lock(mutex_);
    return file_ != nullptr;
}

void ControllerLog::write(std::string_view line) {
    std::lock_guard lock(mutex_);
    if (!file_) return;
    constexpr std::size_t kMaxLines = 5000; // (a log to send in, not a trace)
    if (lines_ > kMaxLines) return;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    if (++lines_ > kMaxLines) std::fprintf(file_, "%9.3f (log full)\n", seconds);
    else std::fprintf(file_, "%9.3f %.*s\n", seconds, int(line.size()), line.data());
    std::fflush(file_);
}

void ControllerLog::capture_sdl() {
    if (capturing_) return;
    SDL_GetLogOutputFunction(&previous_, &previous_data_);
    previous_priority_ = SDL_GetLogPriority(SDL_LOG_CATEGORY_INPUT);
    SDL_SetLogOutputFunction(&ControllerLog::sdl_output, this);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_DEBUG);
    capturing_ = true;
}

void SDLCALL ControllerLog::sdl_output(void* self, int category, SDL_LogPriority priority, const char* message) {
    auto* log = static_cast<ControllerLog*>(self);
    if (category == SDL_LOG_CATEGORY_INPUT) {
        log->write(std::string("sdl: ") + (message ? message : ""));
        // (Errors still reach the console as before; the rest only this log.)
        if (priority < log->previous_priority_) return;
    }
    if (log->previous_) log->previous_(log->previous_data_, category, priority, message);
}

const char* sdl_backend_name(const SDL_GUID& guid) {
    switch (guid.data[14]) {
    case 'h': return "HIDAPI";
    case 'x': return "XInput";
    case 'r': return "RawInput";
    case 'w': return "Windows.Gaming.Input";
    case 'g': return "GameInput";
    case 'v': return "virtual";
    case 0: return "DirectInput";
    default: return "other";
    }
}

std::string pad_log_line(SDL_Gamepad* pad, PadFamily family) {
    if (!pad) return "(no pad)";
    const PadIdentity identity = pad_identity(pad);
    char ids[16];
    std::snprintf(ids, sizeof ids, "%04x:%04x", identity.vendor, identity.product);
    std::string line = "\"" + identity.name + "\" " + ids + ", " + family_name(family);
    if (SDL_Joystick* joystick = SDL_GetGamepadJoystick(pad)) line += std::string(", SDL ") + sdl_backend_name(SDL_GetJoystickGUID(joystick));
    if (const char* type = SDL_GetGamepadStringForType(identity.type)) line += std::string(", type ") + type;
    if (const char* path = SDL_GetGamepadPath(pad)) line += std::string(", path ") + path;
    return line;
}

std::string usb_findings_line(const UsbFindings& found) {
    std::string line = "windows: ";
    if (!found.switch2_pro) {
        line += "no Switch 2 Pro Controller";
    } else {
        line += "Switch 2 Pro Controller";
        if (found.switch2_usb) line += " on USB";
        if (found.switch2_bluetooth) line += " over Bluetooth";
        if (found.switch2_usb) {
            line += ", interface 1 driver ";
            switch (found.switch2_vendor) {
            case UsbDriver::unknown: line += "(no interface 1 node)"; break;
            case UsbDriver::none: line += "none"; break;
            case UsbDriver::winusb:
            case UsbDriver::other: line += narrow(found.switch2_vendor_service); break;
            }
        }
    }
    line += !found.adapter ? "; no GameCube adapter in Wii U mode"
            : found.adapter_winusb ? "; GameCube adapter (Wii U mode) with WinUSB"
                                   : "; GameCube adapter (Wii U mode) without WinUSB";
    return line;
}

} // namespace dw4::game
