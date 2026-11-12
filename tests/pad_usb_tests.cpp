// Who opens which USB controller, and what the game says about a Switch 2
// Pro Controller (src/game/pad_usb, docs/ROUND_SW2WIN_NOTES.md), without a
// controller or a window:
//
// - the hints the game sets before SDL starts: SDL never loads a
//   libusb-1.0.dll (not even one beside the program: a left-over file is
//   noticed, never loaded), and the Switch 2 Pro Controller is left to the
//   port's reader; SDL's own device filter then gives the GameCube adapter
//   and the Switch 2 Pro Controller to nobody in SDL and every other
//   controller to Windows' HID path as before;
// - Windows' device list as the GameCube adapter's reader reads it (the
//   Switch 2 Pro Controller's nodes for the log and Bluetooth);
// - the Switch 2 Pro Controller's notices by the reader's states, their
//   texts (none tells to install anything when Windows has what the
//   controller asks for), the grace, the F1 status lines;
// - SDL's virtual pads as before, the Switch 2 Pro's included, with the
//   game's hints.
#include "../src/game/pad_host.hpp"
#include "../src/game/pad_usb.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
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
// to decide the owners from SDL's code rather than from a reading.
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

// ---- Hints and owners

void hints_and_owner() {
    Settings settings;
    const auto hints = controller_hint_values(settings);
    check(std::string(hint_value(hints, SDL_HINT_HIDAPI_LIBUSB)) == "0", "SDL never loads libusb");
    check(std::string(hint_value(hints, SDL_HINT_JOYSTICK_BLACKLIST_DEVICES)) == "0x057e/0x2069", "SDL's other paths leave the Switch 2 Pro");
    check(std::string(hint_value(hints, SDL_HINT_JOYSTICK_HIDAPI_WII)) == "0", "Wii driver automatic: off at start");
    check(!hint_value(hints, SDL_HINT_HIDAPI_LIBUSB_WHITELIST) && !hint_value(hints, SDL_HINT_HIDAPI_LIBUSB_GAMECUBE), "no libusb hints left");
    settings.wii_driver = WiiDriver::on;
    check(std::string(hint_value(controller_hint_values(settings), SDL_HINT_JOYSTICK_HIDAPI_WII)) == "1", "wii_driver=on");
    check(gamecube_adapter_owner(true, false) == AdapterOwner::port_reader, "the port's reader owns the adapter");
    check(gamecube_adapter_owner(false, false) == AdapterOwner::nobody, "gc_adapter=false: nobody");
    check(gamecube_adapter_owner(true, true) == AdapterOwner::nobody, "test runs: nobody");
    check(switch2_pro_owner(false, false) == AdapterOwner::port_reader, "the port's reader owns the Switch 2 Pro");
    check(switch2_pro_owner(true, false) == AdapterOwner::nobody, "test runs: nobody");
    check(switch2_pro_owner(true, true) == AdapterOwner::port_reader, "DW4_TEST_SWITCH2: the reader in a test run");
}

void sdl_filter() {
    // A left-over libusb-1.0.dll beside the program (earlier builds shipped
    // one): noticed, never loaded (a stand-in file: loading it would fail
    // loudly).
    std::filesystem::path exe;
#ifdef _WIN32
    std::wstring name(MAX_PATH, L'\0');
    name.resize(GetModuleFileNameW(nullptr, name.data(), DWORD(name.size())));
    exe = name;
#endif
    const auto dll = exe.parent_path() / "libusb-1.0.dll";
    std::error_code error;
    const bool had = std::filesystem::exists(dll, error);
    if (!had) std::ofstream(dll, std::ios::binary) << "not a DLL";
    struct Remove { // (also when a check fails)
        std::filesystem::path file;
        ~Remove() {
            std::error_code ignored;
            if (!file.empty()) std::filesystem::remove(file, ignored);
        }
    } remove_after{had ? std::filesystem::path() : dll};
    check(leftover_libusb_dll() == dll, "the left-over DLL is noticed: " + leftover_libusb_dll().string());
    apply(controller_hint_values(Settings{}));
    check(SDL_hid_init() == 0, std::string("SDL_hid_init: ") + SDL_GetError());
    check(!libusb_in_process(), "SDL loaded no libusb-1.0.dll");
    // SDL's own filter: the Switch 2 Pro Controller and the GameCube adapter
    // not through Windows' HID (the port's readers own them; SDL has no
    // libusb path at all); a DualSense and a Switch Pro through Windows' HID
    // as before.
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x2069, 0, 0, false, false), "SDL's Windows HID leaves the Switch 2 Pro");
    check(SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x0337, 0, 0, false, false), "SDL's Windows HID leaves the GameCube adapter");
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x054c, 0x0ce6, 0, 0, false, false), "a DualSense through Windows' HID");
    check(!SDL_HIDAPI_ShouldIgnoreDevice(kBusUsb, 0x057e, 0x2009, 0, 0, false, false), "a Switch Pro through Windows' HID");
    // SDL's enumeration of Nintendo's ids (as the game's Wii U Pro check
    // runs it on a device change) stays short without libusb.
    const auto start = std::chrono::steady_clock::now();
    SDL_hid_device_info* list = SDL_hid_enumerate(0x057e, 0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    SDL_hid_free_enumeration(list);
    std::cout << "SDL_hid_enumerate(057e): " << ms << " ms\n";
    check(ms < 3000, "an enumeration stays short");
    check(!libusb_in_process(), "still no libusb");
    SDL_hid_exit();
    if (!had) {
        std::filesystem::remove(dll, error);
        check(leftover_libusb_dll().empty(), "none without the file");
    }
}

// ---- Windows' device list (the adapter reader's look)

void device_list() {
    check(hardware_id_interface(L"USB\\VID_057E&PID_2069&REV_0100&MI_01") == 1, "MI_01");
    check(hardware_id_interface(L"usb\\vid_057e&pid_2069&mi_00") == 0, "mi_00");
    check(!hardware_id_interface(L"USB\\VID_057E&PID_2069&REV_0100"), "no interface");
    check(hardware_id_usb(L"USB\\VID_057E&PID_2069") && hardware_id_usb(L"HID\\VID_057E&PID_2069&MI_00"), "USB ids");
    check(!hardware_id_usb(L"HID\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002057e_PID&2069"), "a Bluetooth id");
    check(usb_driver(L"") == UsbDriver::none && usb_driver(L"WinUSB") == UsbDriver::winusb && usb_driver(L"WINUSB") == UsbDriver::winusb &&
              usb_driver(L"libusbK") == UsbDriver::other,
          "driver services");
    // The controller on a cable exactly as the owner's PC listed it
    // (controllers.log of main 52d504d9).
    const std::vector<DeviceRecord> devices{
        {{L"USB\\VID_057E&PID_2069&REV_0201&MI_00"}, false, L"HidUsb"},
        {{L"USB\\VID_057E&PID_2069&REV_0201&MI_01"}, true, L"WINUSB"},
        {{L"USB\\VID_057E&PID_2069&REV_0201&MI_02"}, false, L"usbaudio"},
        {{L"USB\\VID_057E&PID_2069&REV_0201"}, false, L"usbccgp"},
        {{L"HID\\VID_057E&PID_2069&REV_0201&MI_00"}, false, L""},
    };
    const auto found = usb_findings(devices);
    check(found.switch2_pro && found.switch2_usb && !found.switch2_bluetooth && !found.switch2_paired, "on USB");
    check(found.switch2_vendor == UsbDriver::winusb && found.switch2_vendor_guid, "WinUSB with its GUID on interface 1");
    check(usb_findings_line(found) ==
              "windows: Switch 2 Pro Controller on USB, interface 1 driver WINUSB with a device interface GUID; no GameCube adapter in Wii U mode",
          "the log's line as on the owner's PC: " + usb_findings_line(found));
    const auto node_lines = usb_findings_node_lines(found);
    check(node_lines.size() == 5 && node_lines[4] == "windows node: HID\\VID_057E&PID_2069&REV_0201&MI_00 []", "the log's node lines");
    // Bluetooth: a connected input device (its HID node) or only a pairing.
    const std::vector<DeviceRecord> bluetooth{{{L"HID\\{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02057e_PID&2069_REV&0001&Col01"}, false, L""}};
    check(usb_findings(bluetooth).switch2_bluetooth && !usb_findings(bluetooth).switch2_usb, "Bluetooth as an input device");
    const std::vector<DeviceRecord> paired{{{L"BTHLEDevice\\{00001800-0000-1000-8000-00805f9b34fb}_Dev_VID&02057e_PID&2069_REV&0001"}, false, L"UmPass"}};
    check(usb_findings(paired).switch2_paired && !usb_findings(paired).switch2_pro, "a pairing only");
}

// ---- The notices

Switch2Status device(Switch2State state) {
    Switch2Status d;
    d.state = state;
    d.found.id = "USB\\VID_057E&PID_2069\\X";
    d.found.input_node = true;
    d.found.input_service = "HidUsb";
    d.found.input_path = "\\\\?\\hid#vid_057e&pid_2069&mi_00#x";
    d.found.command_node = true;
    d.found.command_driver = UsbDriver::winusb;
    d.found.command_service = "WINUSB";
    d.found.command_guid = true;
    d.found.command_path = "\\\\?\\usb#vid_057e&pid_2069&mi_01#x";
    return d;
}

void notices() {
    using N = Switch2Notice;
    using S = Switch2State;
    const UsbFindings nothing;
    UsbFindings bluetooth;
    bluetooth.switch2_pro = bluetooth.switch2_bluetooth = true;
    check(switch2_notice(nothing, {}, true) == N::none, "nothing connected");
    check(switch2_notice(bluetooth, {}, true) == N::bluetooth, "Bluetooth only");
    check(switch2_notice(bluetooth, {device(S::running)}, false) == N::none, "the reader off (a test run): nothing");
    for (const S pending : {S::opening, S::starting, S::waiting, S::gone})
        check(switch2_notice(nothing, {device(pending)}, true) == N::none, std::string("pending: ") + switch2_state_name(pending));
    check(switch2_notice(bluetooth, {device(S::running)}, true) == N::playable, "the reader plays it");
    check(switch2_notice(nothing, {device(S::no_input)}, true) == N::no_input, "no HID input");
    check(switch2_notice(nothing, {device(S::no_reports)}, true) == N::no_reports, "no reports");
    // Interface 1 unavailable, by what Windows has there.
    auto commands = [&](UsbDriver driver, bool guid, Switch2Io open) {
        auto d = device(S::no_commands);
        d.found.command_driver = driver;
        d.found.command_node = driver != UsbDriver::unknown;
        d.found.command_service = driver == UsbDriver::other ? "libusbK" : driver == UsbDriver::winusb ? "WINUSB" : "";
        d.found.command_guid = guid;
        if (!guid || driver != UsbDriver::winusb) d.found.command_path.clear();
        d.command_open = {open, open == Switch2Io::busy ? 5u : 0u, 0};
        return d;
    };
    check(switch2_device_notice(commands(UsbDriver::winusb, true, Switch2Io::busy)) == N::busy, "another program holds it");
    check(switch2_device_notice(commands(UsbDriver::winusb, true, Switch2Io::failed)) == N::no_reports, "it could not be opened");
    check(switch2_device_notice(commands(UsbDriver::winusb, false, Switch2Io::failed)) == N::no_interface_guid, "WinUSB without its GUID");
    check(switch2_device_notice(commands(UsbDriver::none, false, Switch2Io::failed)) == N::no_driver, "no driver");
    check(switch2_device_notice(commands(UsbDriver::other, true, Switch2Io::failed)) == N::other_driver, "another driver");
    check(switch2_device_notice(commands(UsbDriver::unknown, false, Switch2Io::failed)) == N::no_interface, "no interface 1 node");
    // One that does not play wins over one that does.
    const std::vector<Switch2Status> two{device(S::running), commands(UsbDriver::winusb, true, Switch2Io::busy)};
    check(switch2_notice(nothing, two, true) == N::busy, "the second one's trouble shows");
    check(switch2_notice_device(N::busy, two) == &two[1] && !switch2_notice_device(N::playable, two), "the notice's controller");
    // The texts: what is wrong and what to do. With what Windows had on the
    // owner's PC (HidUsb on 0, WinUSB with its GUID on 1) none tells to
    // install anything.
    check(switch2_notice_text(N::none, nullptr).empty() && switch2_notice_text(N::playable, nullptr).empty(), "no text when fine");
    const auto busy = commands(UsbDriver::winusb, true, Switch2Io::busy);
    const auto busy_text = switch2_notice_text(N::busy, &busy);
    check(busy_text.find("Steam") != std::string::npos && busy_text.find("Zadig") == std::string::npos && busy_text.find("install") == std::string::npos,
          "busy: close the other program, nothing to install: " + busy_text);
    auto failed = device(S::no_reports);
    failed.failed_step = "start 3/10 11/01 (unknown purpose)";
    failed.failed_result = {Switch2Io::failed, 31, 0};
    const auto failed_text = switch2_notice_text(N::no_reports, &failed);
    check(failed_text.find("start 3/10") != std::string::npos && failed_text.find("error 31") != std::string::npos &&
              failed_text.find("controllers.log") != std::string::npos && failed_text.find("Zadig") == std::string::npos,
          "a failed start: the step, the log, nothing to install: " + failed_text);
    const auto quiet_text = switch2_notice_text(N::no_reports, &two[0]);
    check(quiet_text.find("sends no input") != std::string::npos && quiet_text.find("Zadig") == std::string::npos, "no reports: " + quiet_text);
    auto hid = device(S::no_input);
    hid.input_open = {Switch2Io::failed, 87, 0};
    const auto hid_text = switch2_notice_text(N::no_input, &hid);
    check(hid_text.find("error 87") != std::string::npos && hid_text.find("057E 2069 00") != std::string::npos &&
              hid_text.find("remove it") != std::string::npos && hid_text.find("Install") == std::string::npos,
          "no HID input: remove a driver on interface 0, install nothing: " + hid_text);
    hid.found.input_service = "WinUSB";
    check(switch2_notice_text(N::no_input, &hid).find("which has the WinUSB driver instead") != std::string::npos, "interface 0's wrong driver named");
    // Another program holds the HID input for itself (access denied, a
    // sharing violation): close it; nothing to remove or install.
    for (const std::uint32_t error : {5u, 32u}) {
        auto held = device(S::no_input);
        held.input_open = {Switch2Io::busy, error, 0};
        check(switch2_device_notice(held) == N::busy && switch2_notice(nothing, {held}, true) == N::busy, "the HID input held: busy");
        const std::vector<Switch2Status> one{held};
        check(switch2_notice_device(N::busy, one) == &one[0], "the busy notice's controller");
        const auto held_text = switch2_notice_text(N::busy, &held);
        check(held_text.find("input device (busy (error " + std::to_string(error) + "))") != std::string::npos &&
                  held_text.find("Steam") != std::string::npos && held_text.find("Close that program") != std::string::npos &&
                  held_text.find("Uninstall") == std::string::npos && held_text.find("remove") == std::string::npos &&
                  held_text.find("Zadig") == std::string::npos && held_text.find("install") == std::string::npos &&
                  held_text.find("interface 1") == std::string::npos,
              "the HID input held: close the other program, nothing to remove: " + held_text);
        check(switch2_status_line(held) == "Switch 2 Pro Controller (USB): its input device cannot be opened (busy (error " + std::to_string(error) + "))",
              "F1: the HID input held: " + switch2_status_line(held));
    }
    check(busy_text.find("interface 1") != std::string::npos, "interface 1 held: its own text");
    check(std::string(switch2_notice_name(N::busy)) == "held by another program", "the log's name for both");
    const auto other = commands(UsbDriver::other, true, Switch2Io::failed);
    const auto other_text = switch2_notice_text(N::other_driver, &other);
    check(other_text.find("libusbK") != std::string::npos && other_text.find("Uninstall device") != std::string::npos &&
              other_text.find("puts WinUSB back by itself") != std::string::npos && other_text.find("Zadig (") == std::string::npos,
          "another driver: remove it, Windows puts WinUSB back: " + other_text);
    const auto no_guid = commands(UsbDriver::winusb, false, Switch2Io::failed);
    check(switch2_notice_text(N::no_interface_guid, &no_guid).find("Uninstall device") != std::string::npos, "no GUID: remove that driver");
    const auto none = commands(UsbDriver::none, false, Switch2Io::failed);
    const auto none_text = switch2_notice_text(N::no_driver, &none);
    check(none_text.find("plug it in again") != std::string::npos && none_text.find("Only if it stays like this: Zadig") != std::string::npos &&
              none_text.find("never the one ending in 00") != std::string::npos,
          "no driver: plug in again first, Zadig only then and on interface 1: " + none_text);
    check(switch2_notice_text(N::bluetooth, nullptr).find("USB") != std::string::npos, "Bluetooth: use the cable");
    for (const N notice : {N::bluetooth, N::no_input, N::no_driver, N::other_driver, N::no_interface_guid, N::no_interface, N::busy, N::no_reports}) {
        check(!switch2_notice_text(notice, nullptr).empty(), std::string("F1 text for ") + switch2_notice_name(notice));
        check(switch2_toast_text(notice).find("F1 > Controls") != std::string::npos, std::string("toast for ") + switch2_notice_name(notice));
        check(switch2_toast_text(notice).find("WinUSB") == std::string::npos && switch2_toast_text(notice).find("blocks") == std::string::npos,
              std::string("no toast asks for a driver: ") + switch2_toast_text(notice));
    }
    // The grace: the reader opens the controller a moment after Windows lists it.
    Switch2Watch watch;
    const auto t0 = std::chrono::steady_clock::now();
    check(watch.update(t0, N::no_input) == N::none, "just plugged: nothing yet");
    check(watch.update(t0 + std::chrono::milliseconds(2900), N::no_input) == N::none, "still within the grace");
    check(watch.update(t0 + kSwitch2Grace, N::no_input) == N::no_input, "after the grace");
    check(watch.update(t0 + std::chrono::seconds(4), N::playable) == N::playable, "playable at once");
    check(watch.update(t0 + std::chrono::seconds(5), N::busy) == N::none, "a new trouble: the grace again");
    check(watch.update(t0 + std::chrono::seconds(8), N::busy) == N::busy && watch.current() == N::busy, "then shown");
    // F1's status lines.
    auto playing = device(S::running);
    playing.started = true;
    playing.rate = 250;
    playing.left_calibration = playing.right_calibration = Switch2CalibrationSource::factory;
    check(switch2_status_line(playing) == "Switch 2 Pro Controller (USB): playing, 250 reports/s, sticks: factory calibration; ZL / ZR are digital",
          "F1: playing: " + switch2_status_line(playing));
    playing.number = 2;
    playing.started = false;
    playing.right_calibration = Switch2CalibrationSource::user;
    check(switch2_status_line(playing).find("Switch 2 Pro Controller 2 (USB): playing, 250 reports/s (started by another program), sticks: left "
                                            "factory, right user calibration") == 0,
          "F1: the second one: " + switch2_status_line(playing));
    check(switch2_status_line(busy) == "Switch 2 Pro Controller (USB): interface 1 unavailable (busy (error 5))", "F1: busy: " + switch2_status_line(busy));
}

// ---- SDL's pads as before (virtual joysticks, every real backend off, the game's hints)

void pads_as_before() {
    apply(controller_hint_values(Settings{}));
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                             SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT})
        SDL_SetHint(hint, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    check(SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS), std::string("SDL_Init: ") + SDL_GetError());
    struct Device {
        const char* name;
        std::uint16_t vendor, product;
        PadFamily family;
    };
    // (The Switch 2 Pro as the port's reader makes it: the blacklist the
    // game sets for 057e:2069 does not touch SDL's virtual joysticks.)
    const Device devices[] = {{"DualSense Wireless Controller", 0x054c, 0x0ce6, PadFamily::ps5},
                              {"PS4 Controller", 0x054c, 0x09cc, PadFamily::ps4},
                              {"Nintendo Switch Pro Controller", 0x057e, 0x2009, PadFamily::switch_pro},
                              {kSwitch2PadName, 0x057e, 0x2069, PadFamily::switch2_pro}};
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
        check(pad_log_line(pads[i], devices[i].family).find("SDL virtual") != std::string::npos,
              "the log's pad line: " + pad_log_line(pads[i], devices[i].family));
    }
    check(!libusb_in_process(), "no libusb with SDL's gamepads up");
    for (auto* pad : pads) SDL_CloseGamepad(pad);
    for (const auto id : ids) SDL_DetachVirtualJoystick(id);
    SDL_Quit();
}

} // namespace

int main() {
    try {
        hints_and_owner();
        sdl_filter();
        device_list();
        notices();
        pads_as_before();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " pad USB checks passed\n";
    return 0;
}
