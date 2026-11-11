#include "gc_adapter.hpp"

#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_joystick.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#endif

namespace dw4::game {

bool parse_gc_adapter_report(const std::uint8_t* data, std::size_t size, std::array<GcPortState, 4>& ports) {
    if (!data || size < 37 || data[0] != 0x21) return false;
    for (std::size_t p = 0; p < 4; ++p) {
        const std::uint8_t* slot = data + 1 + p * 9;
        auto& port = ports[p];
        // 0x10 a wired controller, 0x20 a wireless one (WaveBird).
        port.connected = (slot[0] & 0x30) != 0;
        port.buttons1 = slot[1];
        port.buttons2 = slot[2];
        port.stick_x = slot[3];
        port.stick_y = slot[4];
        port.c_x = slot[5];
        port.c_y = slot[6];
        port.l = slot[7];
        port.r = slot[8];
    }
    return true;
}

GcVirtualState gc_virtual_state(const GcPortState& port, const GcPortState& origin) {
    GcVirtualState s;
    auto button = [&](SDL_GamepadButton b, bool down) { s.buttons[std::size_t(b)] = down; };
    const std::uint8_t b1 = port.buttons1, b2 = port.buttons2;
    button(SDL_GAMEPAD_BUTTON_SOUTH, b1 & 0x01); // A
    button(SDL_GAMEPAD_BUTTON_WEST, b1 & 0x02);  // B
    button(SDL_GAMEPAD_BUTTON_EAST, b1 & 0x04);  // X
    button(SDL_GAMEPAD_BUTTON_NORTH, b1 & 0x08); // Y
    button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, b1 & 0x10);
    button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, b1 & 0x20);
    button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, b1 & 0x40);
    button(SDL_GAMEPAD_BUTTON_DPAD_UP, b1 & 0x80);
    button(SDL_GAMEPAD_BUTTON_START, b2 & 0x01);
    button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, b2 & 0x02); // Z
    button(SDL_GAMEPAD_BUTTON_MISC4, b2 & 0x04);          // R click
    button(SDL_GAMEPAD_BUTTON_MISC3, b2 & 0x08);          // L click
    // Sticks: about 80 steps from the origin are full deflection; SDL's y
    // points down, the GameCube's up.
    auto stick = [](int value, int rest, bool invert) {
        const int v = std::clamp((value - rest) * 32767 / 80, -32768, 32767);
        return std::int16_t(invert ? std::clamp(-v, -32768, 32767) : v);
    };
    s.axes[SDL_GAMEPAD_AXIS_LEFTX] = stick(port.stick_x, origin.stick_x, false);
    s.axes[SDL_GAMEPAD_AXIS_LEFTY] = stick(port.stick_y, origin.stick_y, true);
    s.axes[SDL_GAMEPAD_AXIS_RIGHTX] = stick(port.c_x, origin.c_x, false);
    s.axes[SDL_GAMEPAD_AXIS_RIGHTY] = stick(port.c_y, origin.c_y, true);
    // Triggers: 0 at rest .. 32767 about 150 steps in (as the gamepad sees them).
    auto trigger = [](int value, int rest) { return std::int16_t(std::clamp((value - rest) * 32767 / 150, 0, 32767)); };
    s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = trigger(port.l, origin.l);
    s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = trigger(port.r, origin.r);
    return s;
}

namespace {
constexpr std::uint16_t kNintendo = 0x057e, kAdapter = 0x0337, kSwitch2Pro = 0x2069;

std::optional<std::uint16_t> hex4(std::wstring_view text) {
    if (text.size() < 4) return std::nullopt;
    std::uint16_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const wchar_t c = text[i];
        int digit = -1;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        if (digit < 0) return std::nullopt;
        value = std::uint16_t(value * 16 + digit);
    }
    return value;
}

std::wstring upper(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) c = static_cast<wchar_t>(std::towupper(c));
    return out;
}
} // namespace

std::optional<UsbIds> hardware_id_usb_ids(std::wstring_view hardware_id) {
    const std::wstring id = upper(hardware_id);
    // USB and HID: VID_xxxx&PID_xxxx. Bluetooth HID: VID&0002xxxx_PID&xxxx
    // (the 0002 is the Bluetooth vendor id source).
    if (const auto at = id.find(L"VID_"); at != std::wstring::npos) {
        const auto vendor = hex4(std::wstring_view(id).substr(at + 4));
        const auto pid = id.find(L"PID_", at);
        if (vendor && pid != std::wstring::npos)
            if (const auto product = hex4(std::wstring_view(id).substr(pid + 4))) return UsbIds{*vendor, *product};
    }
    if (const auto at = id.find(L"VID&"); at != std::wstring::npos && at + 12 <= id.size()) {
        const auto vendor = hex4(std::wstring_view(id).substr(at + 8));
        const auto pid = id.find(L"PID&", at);
        if (vendor && pid != std::wstring::npos)
            if (const auto product = hex4(std::wstring_view(id).substr(pid + 4))) return UsbIds{*vendor, *product};
    }
    return std::nullopt;
}

std::optional<int> hardware_id_interface(std::wstring_view hardware_id) {
    const std::wstring id = upper(hardware_id);
    const auto at = id.find(L"&MI_");
    if (at == std::wstring::npos || at + 6 > id.size()) return std::nullopt;
    int value = 0;
    for (std::size_t i = at + 4; i < at + 6; ++i) {
        const wchar_t c = id[i];
        if (c >= L'0' && c <= L'9') value = value * 16 + (c - L'0');
        else if (c >= L'A' && c <= L'F') value = value * 16 + (c - L'A' + 10);
        else return std::nullopt;
    }
    return value;
}

bool hardware_id_usb(std::wstring_view hardware_id) {
    const std::wstring id = upper(hardware_id);
    return id.rfind(L"USB\\", 0) == 0 || id.rfind(L"HID\\VID_", 0) == 0;
}

UsbDriver usb_driver(std::wstring_view service) {
    if (service.empty()) return UsbDriver::none;
    return upper(service) == L"WINUSB" ? UsbDriver::winusb : UsbDriver::other;
}

UsbFindings usb_findings(const std::vector<DeviceRecord>& devices) {
    UsbFindings found;
    for (const auto& device : devices) {
        bool adapter = false, switch2 = false, usb = false, vendor_interface = false;
        for (const auto& hardware_id : device.hardware_ids) {
            const auto ids = hardware_id_usb_ids(hardware_id);
            if (!ids || ids->vendor != kNintendo) continue;
            adapter = adapter || ids->product == kAdapter;
            if (ids->product != kSwitch2Pro) continue;
            switch2 = true;
            usb = usb || hardware_id_usb(hardware_id);
            // (The USB node USB\VID_057E&PID_2069&...&MI_01, not a HID child.)
            vendor_interface = vendor_interface || (upper(hardware_id).rfind(L"USB\\", 0) == 0 && hardware_id_interface(hardware_id) == 1);
        }
        if (adapter) {
            found.adapter = true;
            found.adapter_winusb = found.adapter_winusb || device.interface_guid;
        }
        if (switch2) {
            found.switch2_pro = true;
            (usb ? found.switch2_usb : found.switch2_bluetooth) = true;
            if (vendor_interface) {
                found.switch2_vendor = usb_driver(device.service);
                found.switch2_vendor_service = device.service;
            }
        }
    }
    return found;
}

struct GcAdapter::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    // Shared with the reader thread (under `mutex`).
    std::array<GcPortState, 4> ports{}; // the latest report
    bool holding = false;               // closed for now: the ports stay as idle pads
    bool enabled = true, keep_ports = false;
    bool scan_requested = true;
    bool settings_changed = false; // set_enabled: look at `enabled` / `keep_ports` again
    bool stop = false;
    State state = State::none;
    UsbFindings findings;
    std::uint32_t scans = 0; // (every look at the device list)
    std::thread reader;
    // Main thread.
    std::uint32_t hid_changes = 0;
    bool enabled_main = true, keep_main = false;
    // One virtual joystick per connected port.
    struct Port {
        SDL_JoystickID id = 0;
        SDL_Joystick* joystick = nullptr;
        GcPortState origin;
        GcVirtualState sent;
    };
    std::array<Port, 4> virtual_ports{};

#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    UCHAR in_pipe = 0, out_pipe = 0;

    static bool parse_guid(const wchar_t* text, GUID& guid) {
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

    // A device's interface GUID (the INF Zadig writes puts it in its
    // registry key as DeviceInterfaceGUIDs).
    static std::optional<GUID> interface_guid(HDEVINFO set, SP_DEVINFO_DATA& info) {
        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) return std::nullopt;
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

    // The driver service Windows runs on a device ("WinUSB", "HidUsb"),
    // empty when it has none.
    static std::wstring service(HDEVINFO set, SP_DEVINFO_DATA& info) {
        wchar_t name[256]{};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_SERVICE, nullptr, reinterpret_cast<BYTE*>(name), sizeof(name) - sizeof(wchar_t),
                                               nullptr))
            return {};
        return name;
    }

    static std::vector<std::wstring> hardware_ids(HDEVINFO set, SP_DEVINFO_DATA& info) {
        std::vector<std::wstring> out;
        wchar_t ids[2048]{};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE*>(ids), sizeof(ids) - 4 * sizeof(wchar_t),
                                               nullptr))
            return out;
        for (const wchar_t* id = ids; *id; id += std::wcslen(id) + 1) out.emplace_back(id);
        return out;
    }

    // Every present device that names a Nintendo id we look for (USB and
    // Bluetooth alike).
    static UsbFindings scan() {
        std::vector<DeviceRecord> records;
        HDEVINFO all = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (all == INVALID_HANDLE_VALUE) return {};
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(all, i, &info); ++i) {
            DeviceRecord record;
            record.hardware_ids = hardware_ids(all, info);
            bool ours = false;
            for (const auto& id : record.hardware_ids)
                if (const auto ids = hardware_id_usb_ids(id); ids && ids->vendor == kNintendo && (ids->product == kAdapter || ids->product == kSwitch2Pro))
                    ours = true;
            if (!ours) continue;
            record.interface_guid = interface_guid(all, info).has_value();
            record.service = service(all, info);
            records.push_back(std::move(record));
        }
        SetupDiDestroyDeviceInfoList(all);
        return usb_findings(records);
    }

    // The WinUSB device paths of 057e:0337: the device's interface GUID,
    // then the interfaces of that class whose path names the adapter.
    static std::vector<std::wstring> adapter_paths() {
        std::vector<std::wstring> paths;
        HDEVINFO all = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (all == INVALID_HANDLE_VALUE) return paths;
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(all, i, &info); ++i) {
            bool adapter = false;
            for (const auto& id : hardware_ids(all, info))
                if (const auto ids = hardware_id_usb_ids(id); ids && ids->vendor == kNintendo && ids->product == kAdapter) adapter = true;
            if (!adapter) continue;
            const auto guid = interface_guid(all, info);
            if (!guid) continue;
            HDEVINFO set = SetupDiGetClassDevsW(&*guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (set == INVALID_HANDLE_VALUE) continue;
            SP_DEVICE_INTERFACE_DATA iface{};
            iface.cbSize = sizeof(iface);
            for (DWORD k = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &*guid, k, &iface); ++k) {
                DWORD need = 0;
                SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &need, nullptr);
                if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || need > 4096) continue;
                std::vector<std::uint8_t> buffer(need);
                auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                if (SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, need, nullptr, nullptr) &&
                    upper(detail->DevicePath).find(L"VID_057E&PID_0337") != std::wstring::npos)
                    paths.emplace_back(detail->DevicePath);
            }
            SetupDiDestroyDeviceInfoList(set);
        }
        SetupDiDestroyDeviceInfoList(all);
        return paths;
    }

    bool open_adapter() {
        for (const auto& path : adapter_paths()) {
            // (No sharing: WinUSB opens a device once; another program's
            // handle fails this, and ours fails Dolphin's.)
            file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
            if (file == INVALID_HANDLE_VALUE) continue;
            if (!WinUsb_Initialize(file, &usb)) {
                CloseHandle(file);
                file = INVALID_HANDLE_VALUE;
                usb = nullptr;
                continue;
            }
            USB_INTERFACE_DESCRIPTOR descriptor{};
            in_pipe = out_pipe = 0;
            if (WinUsb_QueryInterfaceSettings(usb, 0, &descriptor))
                for (UCHAR e = 0; e < descriptor.bNumEndpoints; ++e) {
                    WINUSB_PIPE_INFORMATION pipe{};
                    if (!WinUsb_QueryPipe(usb, 0, e, &pipe) || pipe.PipeType != UsbdPipeTypeInterrupt) continue;
                    if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) in_pipe = pipe.PipeId;
                    else out_pipe = pipe.PipeId;
                }
            if (!in_pipe || !out_pipe) {
                close_adapter();
                continue;
            }
            ULONG timeout = 250; // ms: the reader looks at its requests between reads
            WinUsb_SetPipePolicy(usb, in_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
            ULONG write_timeout = 500;
            WinUsb_SetPipePolicy(usb, out_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(write_timeout), &write_timeout);
            // The adapter starts reporting after this command (SDL's
            // HIDAPI_DriverGameCube_EnableAdapter sends the same byte).
            UCHAR init = 0x13;
            ULONG written = 0;
            if (!WinUsb_WritePipe(usb, out_pipe, &init, 1, &written, nullptr) || written != 1) {
                close_adapter();
                continue;
            }
            return true;
        }
        return false;
    }

    void close_adapter() {
        if (usb) WinUsb_Free(usb);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        usb = nullptr;
        file = INVALID_HANDLE_VALUE;
    }

    void run() {
        std::unique_lock lock(mutex);
        auto next_retry = std::chrono::steady_clock::time_point{};
        while (!stop) {
            settings_changed = false;
            if (scan_requested) {
                scan_requested = false;
                lock.unlock();
                const UsbFindings found = scan();
                lock.lock();
                findings = found;
                ++scans;
                next_retry = {}; // (try to open at once)
            }
            if (!enabled) {
                if (usb) {
                    lock.unlock();
                    close_adapter();
                    lock.lock();
                }
                // In the background the ports stay as idle pads; off, they go.
                if (keep_ports && std::any_of(ports.begin(), ports.end(), [](const GcPortState& p) { return p.connected; })) {
                    holding = true;
                } else {
                    ports = {};
                    holding = false;
                }
                if (!keep_ports) state = State::off;
                wake.wait(lock, [&] { return stop || enabled || scan_requested || settings_changed; });
                next_retry = {}; // (back in front: try to open at once)
                continue;
            }
            if (!usb) {
                if (!findings.adapter) state = State::none;
                else if (!findings.adapter_winusb) state = State::no_driver;
                else if (std::chrono::steady_clock::now() >= next_retry) {
                    lock.unlock();
                    const bool opened = open_adapter();
                    lock.lock();
                    if (opened) {
                        state = State::open;
                        continue;
                    }
                    // Held by another program: tried again every two
                    // seconds while this game is in front.
                    state = State::busy;
                    next_retry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                }
                // Not (or no longer) open: idle ports go.
                if (holding || std::any_of(ports.begin(), ports.end(), [](const GcPortState& p) { return p.connected; })) {
                    ports = {};
                    holding = false;
                }
                if (state == State::busy) wake.wait_until(lock, next_retry, [&] { return stop || scan_requested || !enabled; });
                else wake.wait(lock, [&] { return stop || scan_requested || !enabled; });
                continue;
            }
            // Open: read one report (the pipe's 250 ms timeout keeps the
            // requests above in reach).
            lock.unlock();
            UCHAR report[64]{};
            ULONG got = 0;
            const bool read = WinUsb_ReadPipe(usb, in_pipe, report, sizeof(report), &got, nullptr);
            const DWORD error = read ? 0 : GetLastError();
            lock.lock();
            if (read) {
                std::array<GcPortState, 4> parsed{};
                if (parse_gc_adapter_report(report, got, parsed)) {
                    ports = parsed;
                    holding = false;
                }
                continue;
            }
            if (error == ERROR_SEM_TIMEOUT) continue;
            // Unplugged or taken: every port disconnects; the next device
            // change looks again.
            lock.unlock();
            close_adapter();
            lock.lock();
            ports = {};
            holding = false;
            state = State::none;
            findings.adapter = false;
        }
        lock.unlock();
        close_adapter();
    }
#else
    void run() {}
#endif
};

GcAdapter::GcAdapter(bool enabled) : impl_(std::make_unique<Impl>()) {
    impl_->enabled = impl_->enabled_main = enabled;
    if (!enabled) impl_->state = State::off;
    // (The first look at the device list is the reader's own.)
    impl_->hid_changes = SDL_hid_device_change_count();
#ifdef _WIN32
    impl_->reader = std::thread([impl = impl_.get()] { impl->run(); });
#endif
}

GcAdapter::~GcAdapter() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wake.notify_all();
    if (impl_->reader.joinable()) impl_->reader.join();
    for (auto& port : impl_->virtual_ports) {
        if (port.joystick) SDL_CloseJoystick(port.joystick);
        if (port.id) SDL_DetachVirtualJoystick(port.id);
    }
}

void GcAdapter::set_enabled(bool enabled, bool keep_ports) {
    if (enabled == impl_->enabled_main && (enabled || keep_ports == impl_->keep_main)) return;
    impl_->enabled_main = enabled;
    impl_->keep_main = keep_ports;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->enabled = enabled;
        impl_->keep_ports = keep_ports;
        impl_->settings_changed = true;
    }
    impl_->wake.notify_all();
}

GcAdapter::State GcAdapter::state() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->state;
}

bool GcAdapter::switch2_pro() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->findings.switch2_pro;
}

UsbFindings GcAdapter::findings() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->findings;
}

std::uint32_t GcAdapter::scans() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->scans;
}

void GcAdapter::update() {
    // SDL counts every device interface that comes or goes (its HID
    // discovery's notification window): only then is the list looked at.
    if (const std::uint32_t count = SDL_hid_device_change_count(); count != impl_->hid_changes) {
        impl_->hid_changes = count;
        {
            std::lock_guard lock(impl_->mutex);
            impl_->scan_requested = true;
        }
        impl_->wake.notify_all();
    }
    std::array<GcPortState, 4> ports;
    bool holding = false;
    {
        std::lock_guard lock(impl_->mutex);
        ports = impl_->ports;
        holding = impl_->holding;
    }
    for (std::size_t p = 0; p < 4; ++p) {
        auto& port = impl_->virtual_ports[p];
        if (holding) {
            // Closed for now (the window in the background): the port's pad
            // stays, at rest.
            if (!port.joystick) continue;
            const GcVirtualState rest{};
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
                if (port.sent.buttons[std::size_t(b)]) SDL_SetJoystickVirtualButton(port.joystick, b, false);
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
                if (port.sent.axes[std::size_t(a)] == 0) continue;
                const bool trigger = a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                SDL_SetJoystickVirtualAxis(port.joystick, a, trigger ? std::int16_t(-32768) : std::int16_t(0));
            }
            port.sent = rest;
            continue;
        }
        if (ports[p].connected && !port.id) {
            // A controller on this port: a virtual GameCube controller as
            // SDL's own driver would make it.
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.vendor_id = kNintendo;
            desc.product_id = kAdapter;
            desc.name = "Nintendo GameCube Controller";
            port.id = SDL_AttachVirtualJoystick(&desc);
            port.joystick = port.id ? SDL_OpenJoystick(port.id) : nullptr;
            port.origin = ports[p]; // (the GameCube's own calibration: the sticks at connection)
            port.origin.buttons1 = port.origin.buttons2 = 0;
            port.sent = {};
            if (port.joystick) {
                // Triggers rest at the axis minimum (SDL's virtual triggers).
                SDL_SetJoystickVirtualAxis(port.joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
                SDL_SetJoystickVirtualAxis(port.joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
            }
        } else if (!ports[p].connected && port.id) {
            if (port.joystick) SDL_CloseJoystick(port.joystick);
            SDL_DetachVirtualJoystick(port.id);
            port = {};
        }
        if (!port.joystick) continue;
        const auto state = gc_virtual_state(ports[p], port.origin);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (state.buttons[std::size_t(b)] != port.sent.buttons[std::size_t(b)])
                SDL_SetJoystickVirtualButton(port.joystick, b, state.buttons[std::size_t(b)]);
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            if (state.axes[std::size_t(a)] == port.sent.axes[std::size_t(a)]) continue;
            std::int16_t raw = state.axes[std::size_t(a)];
            if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) raw = std::int16_t(int(raw) * 2 - 32768);
            SDL_SetJoystickVirtualAxis(port.joystick, a, raw);
        }
        port.sent = state;
    }
}

} // namespace dw4::game
