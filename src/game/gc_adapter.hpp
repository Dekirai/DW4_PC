#pragma once

// The GameCube controller adapter for Wii U / Switch on Windows
// (docs/ROUND_PADS_NOTES.md 2): Nintendo's WUP-028 and adapters in its mode
// (Mayflash's "Wii U" switch position), USB 057e:0337. SDL opens this adapter
// only through libusb, which this build's SDL does not have (pad_usb.hpp):
// this reader owns it. With the WinUSB driver on it (Zadig installs it, as for
// Dolphin) it opens the adapter through Windows' own WinUSB API and gives
// each connected port to SDL as a virtual GameCube controller with the ids,
// name and button layout of SDL's own GameCube driver. Detection, the
// GameCube binding space and the prompts then treat it like any other
// GameCube pad.
//
// Windows' device list is looked at when SDL counts a device change (and
// every two seconds only while a found adapter is held by another program),
// never on a timer otherwise. The adapter is released while the game's
// window is in the background, so Dolphin can take it; its ports stay as
// idle pads meanwhile and come back to life when it is opened again.
//
// The same look at the device list notices a Switch 2 Pro Controller
// (057e:2069): on a cable or over Bluetooth (an HID node of it), and which
// driver Windows has on its interface 1 (the vendor interface whose bulk
// endpoints start it) with or without a device interface GUID: the log's
// lines, and the Bluetooth notice (pad_usb.hpp switch2_notice). The
// controller itself is switch2_pro.hpp's.
//
// An adapter in "PC" mode (Mayflash 0079:1843 / 1844) is an ordinary HID
// device that SDL's own driver reads; it needs none of this.
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

// One port of an adapter report.
struct GcPortState {
    bool connected = false;
    std::uint8_t buttons1 = 0, buttons2 = 0; // as reported: A B X Y left right down up / Start Z R L
    std::uint8_t stick_x = 128, stick_y = 128, c_x = 128, c_y = 128, l = 0, r = 0;
};
// A 37-byte report (0x21, then nine bytes per port; the layout of SDL's
// SDL_hidapi_gamecube.c); false when it is none.
bool parse_gc_adapter_report(const std::uint8_t* data, std::size_t size, std::array<GcPortState, 4>& ports);

// What a port's virtual controller reports (buttons by SDL_GamepadButton,
// axes by SDL_GamepadAxis): A on SOUTH, X on EAST, B on WEST, Y on NORTH,
// Z on the right shoulder, the trigger clicks on MISC3 / MISC4 (SDL's own
// GameCube layout). `origin`: the port's sticks and triggers at rest (the
// GameCube calibrates them when a controller connects).
struct GcVirtualState {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
};
GcVirtualState gc_virtual_state(const GcPortState& port, const GcPortState& origin);

// ---- Windows' device list
// The USB vendor and product of a Windows hardware id: "USB\VID_057E&PID_0337&REV_0100",
// "HID\VID_057E&PID_2069&MI_00", classic Bluetooth's "HID\{00001124-...}_VID&0002057E_PID&0330"
// (a four-digit vendor id source before the vendor) and Bluetooth LE's
// "BTHLEDevice\{...}_Dev_VID&02057E_PID&2069_REV&0001" (a two-digit source).
struct UsbIds {
    std::uint16_t vendor = 0, product = 0;
};
std::optional<UsbIds> hardware_id_usb_ids(std::wstring_view hardware_id);
// The interface of a composite USB device's node ("...&MI_01" is 1).
std::optional<int> hardware_id_interface(std::wstring_view hardware_id);
// Whether a hardware id is a USB one ("USB\..." and a USB HID's
// "HID\VID_..."); Bluetooth spells its ids "VID&0002..." or "VID&02...".
bool hardware_id_usb(std::wstring_view hardware_id);
// Whether a hardware id is a Bluetooth device's HID node ("HID\{...}_VID&..."):
// Windows has those while the device is connected as an input device. Its
// pairing nodes ("BTHENUM\...", "BTHLE\...", "BTHLEDevice\...") stay listed
// while it is paired, also when it is off.
bool hardware_id_bluetooth_hid(std::wstring_view hardware_id);
// One device of the list: its hardware ids, whether its registry key names
// a device interface GUID (DeviceInterfaceGUID(s): the INF Zadig writes for
// WinUSB does, as does a device's own Microsoft OS descriptor; a WinUSB
// device is opened only through it) and the driver service Windows runs on
// it ("WinUSB", "HidUsb"; empty: none).
struct DeviceRecord {
    std::vector<std::wstring> hardware_ids;
    bool interface_guid = false;
    std::wstring service;
};
// The driver on a device node.
enum class UsbDriver : std::uint8_t {
    unknown, // the node was not seen
    none,    // no driver (Device Manager: "Other devices", a yellow mark)
    winusb,  // Microsoft's WinUSB (by the device's own descriptor or Zadig)
    other,   // another one (libusbK, libusb0, a vendor's)
};
UsbDriver usb_driver(std::wstring_view service);
struct UsbFindings {
    bool adapter = false;        // a GameCube adapter in Wii U mode (057e:0337)
    bool adapter_winusb = false; // ... with the WinUSB driver
    bool switch2_pro = false;    // a Switch 2 Pro Controller (057e:2069) connected, USB or Bluetooth
    bool switch2_usb = false;       // ... on a USB cable
    bool switch2_bluetooth = false; // ... over Bluetooth as an input device (a Bluetooth HID node of it)
    bool switch2_paired = false;    // a Bluetooth pairing of it is listed (also while it is off; for the log)
    // Its interface 1 (MI_01: the bulk endpoints it is started through):
    // that node's driver service, and whether the node names a device
    // interface GUID (WinUSB's device is opened only through it).
    UsbDriver switch2_vendor = UsbDriver::unknown;
    std::wstring switch2_vendor_service;
    bool switch2_vendor_guid = false;
    // Every node of it Windows lists, "<first hardware id> [<service>]" (the
    // log: what a Bluetooth pairing looks like on the owner's PC).
    std::vector<std::wstring> switch2_nodes;
    bool operator==(const UsbFindings&) const = default;
};
UsbFindings usb_findings(const std::vector<DeviceRecord>& devices);

class GcAdapter {
public:
    enum class State : std::uint8_t {
        off,       // settings.ini gc_adapter=false
        none,      // no adapter in Wii U mode connected
        no_driver, // connected without the WinUSB driver (Zadig)
        busy,      // connected, but another program (Dolphin) has it open
        open,      // read: its ports are SDL pads
    };
    explicit GcAdapter(bool enabled = true);
    ~GcAdapter();
    GcAdapter(const GcAdapter&) = delete;
    GcAdapter& operator=(const GcAdapter&) = delete;
    // Whether the adapter may be opened (the setting and the window's input
    // focus). `keep_ports`: when it is closed for now (the window in the
    // background) its ports stay as idle pads, so no player slot moves.
    void set_enabled(bool enabled, bool keep_ports);
    // Once per frame on the main thread (after SDL's gamepad subsystem is
    // up): asks for a look at the device list when SDL counts a device
    // change, and keeps one SDL virtual joystick per connected port up to
    // date (their GAMEPAD_ADDED / REMOVED events follow as for any pad).
    void update();
    State state() const;
    // The adapter is open (WinUSB) and its ports are read.
    bool open() const { return state() == State::open; }
    // A Switch 2 Pro Controller is connected (USB or Bluetooth).
    bool switch2_pro() const;
    // The latest look at the device list (the Switch 2 Pro Controller's
    // part: pad_usb.hpp) and how many looks there were.
    UsbFindings findings() const;
    std::uint32_t scans() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dw4::game
