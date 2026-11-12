#include "pad_usb.hpp"

#include <SDL3/SDL_hints.h>

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
// (Driver service names and hardware ids are ASCII.)
std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) out += c < 0x80 ? char(c) : '?';
    return out;
}
} // namespace

bool libusb_in_process() {
#ifdef _WIN32
    return GetModuleHandleW(L"libusb-1.0.dll") != nullptr;
#else
    return false;
#endif
}

std::filesystem::path leftover_libusb_dll() {
#ifdef _WIN32
    std::wstring exe(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
    if (!length || length >= exe.size()) return {};
    exe.resize(length);
    const auto dll = std::filesystem::path(exe).parent_path() / L"libusb-1.0.dll";
    std::error_code error;
    return std::filesystem::is_regular_file(dll, error) ? dll : std::filesystem::path();
#else
    return {};
#endif
}

std::vector<SdlHint> controller_hint_values(const Settings& settings) {
    return {
        // The Wii U Pro Controller: SDL's Wii driver (SDL leaves it off: it
        // does not work behind a Mayflash DolphinBar). wii_driver=on starts
        // with it; automatic (the default) turns it on only while a Wii U
        // Pro Controller's ids are connected (update_controllers); off never.
        {SDL_HINT_JOYSTICK_HIDAPI_WII, settings.wii_driver == WiiDriver::on ? "1" : "0"},
        // PlayStation and Switch pads: SDL's defaults (on). Bluetooth
        // PlayStation pads stay in their simple report mode until a feature
        // needs more (nothing here does).
        {SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "auto"},
        // SDL never loads a libusb-1.0.dll (this build's SDL has no libusb;
        // also one left over beside the game from an earlier build stays
        // unloaded): the devices SDL reaches only through libusb are the
        // port's own readers' (gc_adapter.hpp, switch2_pro.hpp).
        {SDL_HINT_HIDAPI_LIBUSB, "0"},
        // The Switch 2 Pro Controller's HID input is a game controller to
        // Windows: SDL's DirectInput, RawInput, Windows.Gaming.Input and
        // GameInput paths would list it as a joystick that never reports
        // (nothing of theirs starts it). They leave it to the port's reader,
        // whose SDL virtual pad (an SDL virtual joystick is never filtered
        // by this list) is the controller.
        {SDL_HINT_JOYSTICK_BLACKLIST_DEVICES, "0x057e/0x2069"},
    };
}

AdapterOwner gamecube_adapter_owner(bool gc_adapter_setting, bool test_run) {
    return gc_adapter_setting && !test_run ? AdapterOwner::port_reader : AdapterOwner::nobody;
}

AdapterOwner switch2_pro_owner(bool test_run, bool test_switch2) {
    return !test_run || test_switch2 ? AdapterOwner::port_reader : AdapterOwner::nobody;
}

const char* switch2_notice_name(Switch2Notice notice) {
    switch (notice) {
    case Switch2Notice::none: return "none";
    case Switch2Notice::playable: return "playable";
    case Switch2Notice::bluetooth: return "Bluetooth only";
    case Switch2Notice::no_input: return "its HID input cannot be opened";
    case Switch2Notice::no_driver: return "no driver on interface 1";
    case Switch2Notice::other_driver: return "another driver on interface 1";
    case Switch2Notice::no_interface_guid: return "WinUSB on interface 1 without a device interface GUID";
    case Switch2Notice::no_interface: return "no interface 1 listed";
    case Switch2Notice::busy: return "interface 1 held by another program";
    case Switch2Notice::no_reports: return "no input reports";
    }
    return "";
}

Switch2Notice switch2_device_notice(const Switch2Status& d) {
    switch (d.state) {
    case Switch2State::opening:
    case Switch2State::starting:
    case Switch2State::waiting:
    case Switch2State::gone: return Switch2Notice::none;
    case Switch2State::running: return Switch2Notice::playable;
    case Switch2State::no_input: return Switch2Notice::no_input;
    case Switch2State::no_reports: return Switch2Notice::no_reports;
    case Switch2State::no_commands: {
        const Switch2Found& f = d.found;
        if (!f.command_node) return Switch2Notice::no_interface;
        switch (f.command_driver) {
        case UsbDriver::unknown: return Switch2Notice::no_interface;
        case UsbDriver::none: return Switch2Notice::no_driver;
        case UsbDriver::other: return Switch2Notice::other_driver;
        case UsbDriver::winusb:
            if (!f.command_guid || f.command_path.empty()) return Switch2Notice::no_interface_guid;
            return d.command_open.io == Switch2Io::busy ? Switch2Notice::busy : Switch2Notice::no_reports;
        }
        return Switch2Notice::no_reports;
    }
    }
    return Switch2Notice::none;
}

Switch2Notice switch2_notice(const UsbFindings& windows, const std::vector<Switch2Status>& reader, bool reader_on) {
    if (!reader_on) return Switch2Notice::none; // (test runs)
    bool playing = false;
    for (const auto& device : reader) {
        const Switch2Notice notice = switch2_device_notice(device);
        if (notice == Switch2Notice::playable) playing = true;
        else if (notice != Switch2Notice::none) return notice;
    }
    if (playing) return Switch2Notice::playable;
    if (windows.switch2_bluetooth && !windows.switch2_usb) return Switch2Notice::bluetooth;
    return Switch2Notice::none;
}

const Switch2Status* switch2_notice_device(Switch2Notice notice, const std::vector<Switch2Status>& reader) {
    if (notice == Switch2Notice::none || notice == Switch2Notice::playable || notice == Switch2Notice::bluetooth) return nullptr;
    for (const auto& device : reader)
        if (switch2_device_notice(device) == notice) return &device;
    return nullptr;
}

std::string switch2_notice_text(Switch2Notice notice, const Switch2Status* device) {
    const std::string send_log = " If it stays like this, send saves/controllers.log.";
    const std::string uninstall = "Device Manager > View > Devices by connection, find the controller, right-click that entry, Uninstall "
                                  "device with \"Attempt to remove the driver for this device\" ticked, then plug the controller in again";
    switch (notice) {
    case Switch2Notice::none:
    case Switch2Notice::playable: return {};
    case Switch2Notice::bluetooth:
        return "Switch 2 Pro Controller found over Bluetooth. The game reads it over a USB cable only: connect it with a USB-C data cable.";
    case Switch2Notice::no_input: {
        std::string text = "Switch 2 Pro Controller found on USB, but the game cannot open its input device";
        if (device && device->input_open.error && !device->found.input_path.empty()) text += " (error " + std::to_string(device->input_open.error) + ")";
        text += ". Windows reads the controller's buttons through its own HID driver on the controller's interface 0";
        if (device && device->found.input_node && !device->found.input_service.empty() && device->found.input_service != "HidUsb")
            text += ", which has the " + device->found.input_service + " driver instead";
        text += ". If another driver was installed there (Zadig: the entry ending in 057E 2069 00), remove it: " + uninstall + "." + send_log;
        return text;
    }
    case Switch2Notice::no_driver:
        return "Switch 2 Pro Controller found, but Windows has no driver on its interface 1, through which the game starts it. Windows "
               "normally puts WinUSB there by itself (the controller asks for it): unplug the controller, wait a few seconds and plug it in "
               "again, or use another USB port. Only if it stays like this: Zadig (zadig.akeo.ie), Options > List All Devices, the entry "
               "whose USB ID ends in 057E 2069 01 (never the one ending in 00), WinUSB, Install Driver.";
    case Switch2Notice::other_driver:
        return "Switch 2 Pro Controller found, but its interface 1 has the " +
               (device && !device->found.command_service.empty() ? device->found.command_service : std::string("another")) +
               " driver (from Zadig or another program) instead of Windows' own WinUSB, and the game opens it only through WinUSB. Remove "
               "that driver: " +
               uninstall + "; Windows puts WinUSB back by itself.";
    case Switch2Notice::no_interface_guid:
        return "Switch 2 Pro Controller found; its interface 1 has WinUSB, but without the device interface the game opens it through (a "
               "driver installed by hand?). Remove it: " +
               uninstall + "; Windows sets WinUSB up again by itself." + send_log;
    case Switch2Notice::no_interface:
        return "Switch 2 Pro Controller found on USB, but Windows lists no interface 1 of it (the part ending in 057E 2069 01 through "
               "which the game starts it). A driver installed on the whole controller instead of on its parts does this: in Device "
               "Manager, uninstall that device with its driver and plug the controller in again." +
               send_log;
    case Switch2Notice::busy:
        return "Switch 2 Pro Controller found, but another program holds its interface 1 (Steam with its Switch controller support, an "
               "emulator, or a second copy of this game), and the controller sends nothing until it is started. Close that program or "
               "turn its Switch controller support off; the game tries again every two seconds.";
    case Switch2Notice::no_reports: {
        std::string text = "Switch 2 Pro Controller found";
        if (device && !device->failed_step.empty())
            text += ", but the start command \"" + device->failed_step + "\" could not be sent (" + switch2_io_text(device->failed_result) + ")";
        else if (device && device->state == Switch2State::no_commands)
            text += ", but its interface 1 could not be opened (" + switch2_io_text(device->command_open) + ")";
        else
            text += " and started, but it sends no input";
        return text + ". Unplug it and plug it in again." + send_log;
    }
    }
    return {};
}

std::string switch2_toast_text(Switch2Notice notice) {
    switch (notice) {
    case Switch2Notice::none:
    case Switch2Notice::playable: return {};
    case Switch2Notice::bluetooth: return "Switch 2 Pro Controller: connect it with a USB cable (F1 > Controls).";
    case Switch2Notice::no_input: return "Switch 2 Pro Controller found, but its input device cannot be opened (F1 > Controls).";
    case Switch2Notice::no_driver: return "Switch 2 Pro Controller found: Windows has no driver on its interface 1 yet (F1 > Controls).";
    case Switch2Notice::other_driver:
    case Switch2Notice::no_interface_guid: return "Switch 2 Pro Controller found: a driver installed on it is in the way (F1 > Controls).";
    case Switch2Notice::no_interface: return "Switch 2 Pro Controller found, but it did not start (F1 > Controls).";
    case Switch2Notice::busy: return "Switch 2 Pro Controller found: another program is using it (F1 > Controls).";
    case Switch2Notice::no_reports: return "Switch 2 Pro Controller found, but it did not start (F1 > Controls).";
    }
    return {};
}

std::string switch2_status_line(const Switch2Status& d) {
    std::string line = "Switch 2 Pro Controller";
    if (d.number > 1) line += " " + std::to_string(d.number);
    line += " (USB): ";
    switch (d.state) {
    case Switch2State::opening: line += "opening..."; break;
    case Switch2State::starting: line += "starting..."; break;
    case Switch2State::waiting: line += d.started ? "started, waiting for its first input report..." : "waiting for its input..."; break;
    case Switch2State::running: {
        char rate[64];
        std::snprintf(rate, sizeof rate, "playing, %.0f reports/s", d.rate);
        line += d.rate > 0 ? std::string(rate) : std::string("playing");
        if (!d.started) line += " (started by another program)";
        const auto left = d.left_calibration, right = d.right_calibration;
        line += left == right ? std::string(", sticks: ") + switch2_calibration_name(left) + " calibration"
                              : std::string(", sticks: left ") + switch2_calibration_name(left) + ", right " + switch2_calibration_name(right) +
                                    " calibration";
        line += "; ZL / ZR are digital";
        break;
    }
    case Switch2State::no_input: line += "its input device cannot be opened"; break;
    case Switch2State::no_commands: line += "interface 1 unavailable (" + switch2_io_text(d.command_open) + ")"; break;
    case Switch2State::no_reports: line += "no input reports"; break;
    case Switch2State::gone: line += "unplugged"; break;
    }
    return line;
}

Switch2Notice Switch2Watch::update(std::chrono::steady_clock::time_point now, Switch2Notice notice) {
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
            if (found.switch2_vendor == UsbDriver::winusb || found.switch2_vendor == UsbDriver::other)
                line += found.switch2_vendor_guid ? " with a device interface GUID" : " without a device interface GUID";
        }
    }
    if (found.switch2_paired) line += "; a Bluetooth pairing of a Switch 2 Pro Controller";
    line += !found.adapter ? "; no GameCube adapter in Wii U mode"
            : found.adapter_winusb ? "; GameCube adapter (Wii U mode) with WinUSB"
                                   : "; GameCube adapter (Wii U mode) without WinUSB";
    return line;
}

std::vector<std::string> usb_findings_node_lines(const UsbFindings& found) {
    std::vector<std::string> lines;
    for (const auto& node : found.switch2_nodes) lines.push_back("windows node: " + narrow(node));
    return lines;
}

} // namespace dw4::game
