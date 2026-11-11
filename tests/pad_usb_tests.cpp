// libusb-1.0.dll for SDL and the Switch 2 Pro Controller (src/game/pad_usb,
// docs/ROUND_LIBUSB_NOTES.md), without a controller or a window:
//
//   dw4_pad_usb_tests <libusb-1.0.dll>  the DLL this build made exports every
//       function SDL loads; SDL's HID layer loads it by name (beside the
//       program) and lets it go again; SDL's own device filter (the hints
//       the game sets) gives the Switch 2 Pro Controller to libusb, the
//       GameCube adapter in Wii U mode to nobody in SDL (the port's WinUSB
//       reader owns it) and every other controller to Windows' HID path.
//   dw4_pad_usb_tests --missing  the DLL search limited to System32 (the
//       DLL missing): SDL's HID layer still starts, SDL makes pads as before,
//       the notices say what is missing.
//   dw4_pad_usb_tests --not-built  a build without libusb.
//
// Every mode also checks the notices (which one, after which grace), the
// device list's Switch 2 part and the controller hints. SDL's libusb never
// enumerates or opens a device here (no SDL_hid_enumerate / SDL_hid_open).
#include "../src/game/pad_host.hpp"
#include "../src/game/pad_usb.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// SDL's own device filter (src/hidapi/SDL_hidapi.c, SDL 3.4.16): whether
// its libusb path (`libusb`) or its Windows HID path leaves a device alone.
// Internal to SDL but linked in from the static library; the test calls it
// to decide the adapter's owner from SDL's code rather than from a reading.
extern "C" bool SDL_HIDAPI_ShouldIgnoreDevice(int bus, Uint16 vendor_id, Uint16 product_id, Uint16 usage_page, Uint16 usage, bool libusb,
                                              bool libusb_xbox);

namespace {
using namespace dw4;
using namespace dw4::game;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

constexpr int kBusUsb = 1; // hidapi.h HID_API_BUS_USB

const char* hint_value(const std::vector<SdlHint>& hints, const char* name) {
    for (const auto& hint : hints)
        if (std::strcmp(hint.name, name) == 0) return hint.value;
    return nullptr;
}

void apply(const std::vector<SdlHint>& hints) {
    for (const auto& hint : hints) SDL_SetHint(hint.name, hint.value);
}

#ifdef _WIN32
bool libusb_module_loaded() { return GetModuleHandleW(L"libusb-1.0.dll") != nullptr; }
#else
bool libusb_module_loaded() { return false; }
#endif

// ---- Every mode

void hints_and_owner() {
    Settings settings;
    const auto play = controller_hint_values(settings, false);
    const auto test = controller_hint_values(settings, true);
    check(std::string(hint_value(play, SDL_HINT_HIDAPI_LIBUSB)) == "1", "libusb on in play");
    check(std::string(hint_value(test, SDL_HINT_HIDAPI_LIBUSB)) == "0", "libusb off in test runs");
    for (const auto* hints : {&play, &test}) {
        check(std::string(hint_value(*hints, SDL_HINT_HIDAPI_LIBUSB_WHITELIST)) == "1", "libusb only for the devices that need it");
        check(std::string(hint_value(*hints, SDL_HINT_HIDAPI_LIBUSB_GAMECUBE)) == "0", "SDL leaves the GameCube adapter");
        check(std::string(hint_value(*hints, SDL_HINT_JOYSTICK_HIDAPI_WII)) == "0", "Wii driver automatic: off at start");
    }
    settings.wii_driver = WiiDriver::on;
    check(std::string(hint_value(controller_hint_values(settings, false), SDL_HINT_JOYSTICK_HIDAPI_WII)) == "1", "wii_driver=on");
    check(gamecube_adapter_owner(true, false) == AdapterOwner::port_reader, "the port's reader owns the adapter");
    check(gamecube_adapter_owner(false, false) == AdapterOwner::nobody, "gc_adapter=false: nobody");
    check(gamecube_adapter_owner(true, true) == AdapterOwner::nobody, "test runs: nobody");
}

void device_list() {
    check(hardware_id_interface(L"USB\\VID_057E&PID_2069&REV_0100&MI_01") == 1, "MI_01");
    check(hardware_id_interface(L"usb\\vid_057e&pid_2069&mi_00") == 0, "mi_00");
    check(!hardware_id_interface(L"USB\\VID_057E&PID_2069&REV_0100"), "no interface");
    check(hardware_id_usb(L"USB\\VID_057E&PID_2069") && hardware_id_usb(L"HID\\VID_057E&PID_2069&MI_00"), "USB ids");
    check(!hardware_id_usb(L"HID\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002057e_PID&2069"), "a Bluetooth id");
    check(usb_driver(L"") == UsbDriver::none && usb_driver(L"WinUSB") == UsbDriver::winusb && usb_driver(L"WINUSB") == UsbDriver::winusb &&
              usb_driver(L"libusbK") == UsbDriver::other,
          "driver services");
    // A Switch 2 Pro Controller on a cable as Windows lists it: the
    // composite parent, interface 0 (HID) with its HID child, interface 1
    // (the vendor interface: WinUSB by the controller's own descriptor).
    std::vector<DeviceRecord> devices{
        {{L"USB\\VID_057E&PID_2069&REV_0100", L"USB\\VID_057E&PID_2069"}, false, L"usbccgp"},
        {{L"USB\\VID_057E&PID_2069&REV_0100&MI_00", L"USB\\VID_057E&PID_2069&MI_00"}, false, L"HidUsb"},
        {{L"HID\\VID_057E&PID_2069&REV_0100&MI_00", L"HID\\VID_057E&PID_2069&MI_00"}, false, L""},
        {{L"USB\\VID_057E&PID_2069&REV_0100&MI_01", L"USB\\VID_057E&PID_2069&MI_01"}, true, L"WinUSB"},
    };
    auto found = usb_findings(devices);
    check(found.switch2_pro && found.switch2_usb && !found.switch2_bluetooth, "on USB");
    check(found.switch2_vendor == UsbDriver::winusb && found.switch2_vendor_service == L"WinUSB", "WinUSB on interface 1");
    check(!found.adapter, "no adapter");
    devices.back().service.clear();
    found = usb_findings(devices);
    check(found.switch2_vendor == UsbDriver::none, "interface 1 without a driver");
    devices.back().service = L"libusbK";
    check(usb_findings(devices).switch2_vendor == UsbDriver::other, "interface 1 with another driver");
    // Interface 1 listed first, then interface 0 (HidUsb): interface 0's
    // driver says nothing about interface 1.
    std::reverse(devices.begin(), devices.end());
    devices.front().service = L"WinUSB";
    check(usb_findings(devices).switch2_vendor == UsbDriver::winusb, "interface 1's driver whatever the order");
    // No interface 1 node (WinUSB put on the whole device): unknown.
    devices.erase(devices.begin());
    found = usb_findings(devices);
    check(found.switch2_usb && found.switch2_vendor == UsbDriver::unknown, "no interface 1 node: its driver unknown");
    // Over Bluetooth only.
    const std::vector<DeviceRecord> bluetooth{{{L"HID\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002057e_PID&2069"}, false, L""}};
    found = usb_findings(bluetooth);
    check(found.switch2_pro && found.switch2_bluetooth && !found.switch2_usb && found.switch2_vendor == UsbDriver::unknown, "Bluetooth only");
    check(usb_findings_line(found).find("over Bluetooth") != std::string::npos, "the log's line");
}

UsbFindings usb_switch2(UsbDriver vendor, const wchar_t* service = L"") {
    UsbFindings found;
    found.switch2_pro = found.switch2_usb = true;
    found.switch2_vendor = vendor;
    found.switch2_vendor_service = service;
    return found;
}

void notices() {
    const UsbFindings nothing;
    const UsbFindings winusb = usb_switch2(UsbDriver::winusb, L"WinUSB");
    const UsbFindings none = usb_switch2(UsbDriver::none);
    const UsbFindings other = usb_switch2(UsbDriver::other, L"libusbK");
    UsbFindings bluetooth;
    bluetooth.switch2_pro = bluetooth.switch2_bluetooth = true;
    using N = Switch2Notice;
    using L = LibusbState;
    check(switch2_notice(nothing, false, L::loaded) == N::none, "nothing connected");
    for (const L state : {L::not_built, L::off, L::missing, L::incompatible, L::loaded})
        check(switch2_notice(winusb, true, state) == N::playable, "SDL's pad plays whatever else");
    check(switch2_notice(winusb, false, L::off) == N::none, "test runs say nothing");
    check(switch2_notice(bluetooth, false, L::loaded) == N::bluetooth, "Bluetooth");
    check(switch2_notice(winusb, false, L::not_built) == N::not_built, "a build without libusb");
    check(switch2_notice(winusb, false, L::missing) == N::dll_missing, "the DLL missing");
    check(switch2_notice(none, false, L::missing) == N::dll_missing, "the DLL first, then the driver");
    check(switch2_notice(winusb, false, L::incompatible) == N::dll_incompatible, "an unusable DLL");
    check(switch2_notice(none, false, L::loaded) == N::no_driver, "Windows without a driver on interface 1");
    check(switch2_notice(other, false, L::loaded) == N::other_driver, "another driver on interface 1");
    check(switch2_notice(winusb, false, L::loaded) == N::not_opened, "WinUSB, but not opened");
    check(switch2_notice(usb_switch2(UsbDriver::unknown), false, L::loaded) == N::not_opened, "no interface 1 node seen");
    // The texts: what is wrong and what to do.
    check(switch2_notice_text(N::none, nothing).empty() && switch2_notice_text(N::playable, winusb).empty(), "no text when fine");
    const std::string missing = switch2_notice_text(N::dll_missing, winusb);
    check(missing.find("libusb-1.0.dll is missing") != std::string::npos && missing.find("next to the game's .exe") != std::string::npos &&
              missing.find("Every other controller works") != std::string::npos,
          "the DLL-missing text: what, where, the rest works: " + missing);
    const std::string driver = switch2_notice_text(N::no_driver, none);
    check(driver.find("Windows blocks it") != std::string::npos && driver.find("057E 2069 01") != std::string::npos &&
              driver.find("WinUSB") != std::string::npos && driver.find("never the one ending in 00") != std::string::npos,
          "the no-driver text: Zadig on interface 1 only: " + driver);
    check(switch2_notice_text(N::other_driver, other).find("libusbK") != std::string::npos, "the other driver by name");
    check(switch2_notice_text(N::bluetooth, bluetooth).find("USB") != std::string::npos, "Bluetooth: use the cable");
    check(switch2_notice_text(N::not_opened, winusb).find("controllers.log") != std::string::npos, "not opened: send the log");
    for (const N notice : {N::not_built, N::dll_missing, N::dll_incompatible, N::bluetooth, N::no_driver, N::other_driver, N::not_opened}) {
        check(!switch2_notice_text(notice, other).empty(), std::string("F1 text for ") + switch2_notice_name(notice));
        check(switch2_toast_text(notice).find("F1 > Controls") != std::string::npos, std::string("toast for ") + switch2_notice_name(notice));
    }
    check(switch2_toast_text(N::dll_missing).find("libusb-1.0.dll is missing") != std::string::npos, "the DLL-missing toast");
    // The grace: SDL opens the controller a moment after Windows lists it.
    Switch2Watch watch;
    const auto t0 = std::chrono::steady_clock::now();
    check(watch.update(t0, winusb, false, L::loaded) == N::none, "just plugged: nothing yet");
    check(watch.update(t0 + std::chrono::milliseconds(2900), winusb, false, L::loaded) == N::none, "still within the grace");
    check(watch.update(t0 + kSwitch2Grace, winusb, false, L::loaded) == N::not_opened, "after the grace: not opened");
    check(watch.update(t0 + std::chrono::seconds(4), winusb, true, L::loaded) == N::playable, "SDL's pad: playable at once");
    check(watch.update(t0 + std::chrono::seconds(5), nothing, false, L::loaded) == N::none, "unplugged: nothing");
    check(watch.update(t0 + std::chrono::seconds(6), none, false, L::loaded) == N::none, "plugged without a driver: within the grace");
    check(watch.update(t0 + std::chrono::seconds(9), none, false, L::loaded) == N::no_driver && watch.current() == N::no_driver, "then no driver");
    // SDL's own Switch 2 driver opened it: 057e:2069 with SDL's HIDAPI GUID.
    SDL_GUID guid{};
    guid.data[14] = 'h';
    PadIdentity identity{SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2069, "Nintendo Switch Pro Controller"};
    check(sdl_switch2_pad(identity, guid), "SDL's HIDAPI Switch 2 pad");
    guid.data[14] = 'r';
    check(!sdl_switch2_pad(identity, guid), "a RawInput joystick of the same ids is no started controller");
    guid.data[14] = 'h';
    identity.product = 0x2009;
    check(!sdl_switch2_pad(identity, guid), "a Switch Pro is not a Switch 2 Pro");
}

// ---- With the DLL beside the test

void with_dll(const std::filesystem::path& built) {
    check(sdl_has_libusb(), "this build's SDL has libusb");
    check(sdl_libusb_functions().size() == 35, "SDL 3.4.16 loads 35 functions");
    // The DLL this build made: every function SDL loads, version 1.0.30.
    const LibusbInfo direct = probe_libusb(true, true, built.string().c_str());
    check(direct.state == LibusbState::loaded, "the built DLL has what SDL needs: " + direct.missing);
    check(direct.version.rfind("1.0.30.", 0) == 0, "libusb 1.0.30: " + direct.version);
    // By name, as SDL looks for it: the one beside the program.
    const LibusbInfo by_name = probe_libusb(true, true);
    check(by_name.state == LibusbState::loaded, std::string("found by name: ") + libusb_state_name(by_name.state));
    check(std::filesystem::equivalent(std::filesystem::u8path(by_name.path), built), "the one beside the program: " + by_name.path);
    check(libusb_summary(by_name).find("Switch 2 Pro Controller over USB") != std::string::npos, "summary: " + libusb_summary(by_name));
    check(probe_libusb(true, false).state == LibusbState::off, "test runs: off (still found)");
    check(probe_libusb(true, true, "dw4-no-such-library.dll").state == LibusbState::missing, "another name: missing");
    check(probe_libusb(false, true, "dw4-no-such-library.dll").state == LibusbState::not_built, "a build without libusb");
    check(!libusb_module_loaded(), "the probe let the DLL go");
    // SDL's HID layer with the game's hints: SDL loads the DLL by name.
    apply(controller_hint_values(Settings{}, false));
    check(SDL_hid_init() == 0, std::string("SDL_hid_init: ") + SDL_GetError());
    check(libusb_module_loaded(), "SDL loaded libusb-1.0.dll");
    // SDL's own filter decides the owners: the Switch 2 Pro Controller
    // through libusb only; the GameCube adapter in Wii U mode neither
    // through libusb nor through Windows' HID (the port's WinUSB reader
    // owns it); a DualSense through Windows' HID as before, never libusb.
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x2069, 0, 0, true, false), "SDL takes the Switch 2 Pro through libusb");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x2069, 0, 0, false, false), "... and not through Windows' HID");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x0337, 0, 0, true, false), "SDL's libusb leaves the GameCube adapter");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x0337, 0, 0, false, false), "SDL's Windows HID leaves the GameCube adapter");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x054c, 0x0ce6, 0, 0, true, false), "a DualSense never through libusb");
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x054c, 0x0ce6, 0, 0, false, false), "a DualSense through Windows' HID");
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x2009, 0, 0, false, false), "a Switch Pro through Windows' HID");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x2009, 0, 0, true, false), "a Switch Pro never through libusb");
    // SDL's enumeration as the game runs it on a device change, through
    // Windows' HID and libusb (libusb lists every USB device; only Nintendo's
    // ids are looked at here, and nothing is opened for input): it stays short.
    const auto start = std::chrono::steady_clock::now();
    SDL_hid_device_info* list = SDL_hid_enumerate(0x057e, 0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    int listed = 0;
    for (const SDL_hid_device_info* device = list; device; device = device->next) ++listed;
    SDL_hid_free_enumeration(list);
    std::cout << "SDL_hid_enumerate(057e) with libusb: " << listed << " devices in " << ms << " ms\n";
    check(ms < 3000, "an enumeration through libusb stays short");
    SDL_hid_exit();
    check(!libusb_module_loaded(), "SDL let the DLL go");
    // Without the game's adapter hint SDL's libusb path would take the
    // adapter as well: the two readers the hint keeps apart.
    SDL_ResetHint(SDL_HINT_HIDAPI_LIBUSB_GAMECUBE);
    check(SDL_hid_init() == 0, "SDL_hid_init, SDL's defaults");
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x0337, 0, 0, true, false), "SDL's default: its libusb path takes the adapter");
    SDL_hid_exit();
    // Test runs: SDL never loads it.
    apply(controller_hint_values(Settings{}, true));
    check(SDL_hid_init() == 0, "SDL_hid_init in a test run");
    check(!libusb_module_loaded(), "test runs: SDL leaves libusb alone");
    SDL_hid_exit();
}

// ---- The DLL missing

void missing_dll() {
    const LibusbInfo info = probe_libusb(true, true);
    check(info.state == LibusbState::missing, std::string("the DLL is missing (System32 only): ") + libusb_state_name(info.state) + " " + info.path);
    check(libusb_summary(info).find("every other controller works") != std::string::npos, "summary: " + libusb_summary(info));
    apply(controller_hint_values(Settings{}, false));
    // SDL's HID layer starts without it (Windows' own HID path).
    check(SDL_hid_init() == 0, std::string("SDL_hid_init without the DLL: ") + SDL_GetError());
    check(!libusb_module_loaded(), "nothing loaded");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x0337, 0, 0, false, false), "the adapter still the port reader's");
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x054c, 0x0ce6, 0, 0, false, false), "a DualSense through Windows' HID as before");
    SDL_hid_exit();
    // What a connected Switch 2 Pro Controller then shows.
    const auto notice = switch2_notice(usb_switch2(UsbDriver::winusb, L"WinUSB"), false, info.state);
    check(notice == Switch2Notice::dll_missing, "the notice: the DLL is missing");
}

void not_built() {
    check(!sdl_has_libusb(), "this build's SDL has no libusb");
    const LibusbInfo info = probe_libusb(sdl_has_libusb(), true);
    check(info.state == LibusbState::not_built, "not built");
    check(switch2_notice(usb_switch2(UsbDriver::winusb, L"WinUSB"), false, info.state) == Switch2Notice::not_built, "the notice: not built");
}

// ---- SDL's pads as before (virtual joysticks, every real backend off)

void pads_as_before() {
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                             SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_HIDAPI_LIBUSB})
        SDL_SetHint(hint, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    check(SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS), std::string("SDL_Init: ") + SDL_GetError());
    struct Device {
        const char* name;
        std::uint16_t vendor, product;
        PadFamily family;
    };
    const Device devices[] = {{"DualSense Wireless Controller", 0x054c, 0x0ce6, PadFamily::ps5},
                              {"PS4 Controller", 0x054c, 0x09cc, PadFamily::ps4},
                              {"Nintendo Switch Pro Controller", 0x057e, 0x2009, PadFamily::switch_pro},
                              {"Nintendo Switch Pro Controller", 0x057e, 0x2069, PadFamily::switch2_pro}};
    std::vector<SDL_Gamepad*> pads;
    PromptTracker tracker;
    std::vector<SDL_JoystickID> ids;
    for (const auto& device : devices) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.vendor_id = device.vendor;
        desc.product_id = device.product;
        desc.name = device.name;
        ids.push_back(SDL_AttachVirtualJoystick(&desc));
        check(ids.back() != 0, std::string("attach ") + device.name);
    }
    SDL_UpdateJoysticks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) pad_slots_event(pads, tracker, event);
    check(pads.size() == std::size(devices), "every pad opened");
    for (std::size_t i = 0; i < pads.size(); ++i) {
        check(family_of(tracker, pads[i]) == devices[i].family, std::string("family of ") + devices[i].name);
        // (A virtual pad is no started Switch 2 Pro Controller.)
        check(!sdl_switch2_pad(pads[i]), "not SDL's Switch 2 driver");
        check(pad_log_line(pads[i], devices[i].family).find("SDL virtual") != std::string::npos, "the log's pad line: " +
                                                                                                    pad_log_line(pads[i], devices[i].family));
    }
    for (auto* pad : pads) SDL_CloseGamepad(pad);
    for (const auto id : ids) SDL_DetachVirtualJoystick(id);
    SDL_Quit();
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
#ifdef _WIN32
    // The DLL missing: Windows looks for DLLs loaded by name in System32
    // only (SDL's LoadLibrary included), so the one beside the test is not
    // found.
    if (mode == "--missing") SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
    try {
        hints_and_owner();
        device_list();
        notices();
        if (mode == "--missing") missing_dll();
        else if (mode == "--not-built") not_built();
        else if (!mode.empty()) with_dll(std::filesystem::u8path(mode));
        else throw std::runtime_error("usage: dw4_pad_usb_tests <libusb-1.0.dll> | --missing | --not-built");
        pads_as_before();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " pad USB checks passed (" << (mode.empty() ? "?" : mode) << ")\n";
    return 0;
}
