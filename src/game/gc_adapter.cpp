#include "gc_adapter.hpp"

#include <SDL3/SDL_joystick.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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

struct GcAdapter::Impl {
    std::mutex mutex;
    std::array<GcPortState, 4> ports{}; // the latest report (reader thread)
    std::atomic<bool> stop{false};
    std::atomic<bool> is_open{false};
    std::thread reader;
    // Main thread: one virtual joystick per connected port.
    struct Port {
        SDL_JoystickID id = 0;
        SDL_Joystick* joystick = nullptr;
        GcPortState origin;
        GcVirtualState sent;
        bool origin_set = false;
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

    static std::wstring upper(std::wstring text) {
        for (auto& c : text) c = static_cast<wchar_t>(std::towupper(c));
        return text;
    }

    // The WinUSB device paths of 057e:0337: the device's DeviceInterfaceGUIDs
    // (the INF Zadig writes puts them in its registry key), then the
    // interfaces of that class whose path names the adapter.
    static std::vector<std::wstring> adapter_paths() {
        std::vector<std::wstring> paths;
        HDEVINFO all = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (all == INVALID_HANDLE_VALUE) return paths;
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(all, i, &info); ++i) {
            wchar_t ids[2048]{};
            if (!SetupDiGetDeviceRegistryPropertyW(all, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE*>(ids), sizeof(ids) - 4 * sizeof(wchar_t),
                                                   nullptr))
                continue;
            bool adapter = false;
            for (const wchar_t* id = ids; *id; id += std::wcslen(id) + 1)
                if (upper(id).find(L"VID_057E&PID_0337") != std::wstring::npos) adapter = true;
            if (!adapter) continue;
            HKEY key = SetupDiOpenDevRegKey(all, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (key == INVALID_HANDLE_VALUE) continue;
            wchar_t guids[1024]{};
            DWORD size = sizeof(guids) - 4 * sizeof(wchar_t);
            LSTATUS status = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", nullptr, nullptr, reinterpret_cast<BYTE*>(guids), &size);
            if (status != ERROR_SUCCESS) {
                size = sizeof(guids) - 4 * sizeof(wchar_t);
                status = RegQueryValueExW(key, L"DeviceInterfaceGUID", nullptr, nullptr, reinterpret_cast<BYTE*>(guids), &size);
            }
            RegCloseKey(key);
            GUID guid{};
            if (status != ERROR_SUCCESS || !parse_guid(guids, guid)) continue;
            HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (set == INVALID_HANDLE_VALUE) continue;
            SP_DEVICE_INTERFACE_DATA iface{};
            iface.cbSize = sizeof(iface);
            for (DWORD k = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, k, &iface); ++k) {
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
            ULONG timeout = 250; // ms: the reader looks at `stop` between reads
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
        auto next_scan = std::chrono::steady_clock::now();
        while (!stop) {
            if (!usb) {
                if (std::chrono::steady_clock::now() >= next_scan) {
                    next_scan = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                    if (open_adapter()) is_open = true;
                }
                if (!usb) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            UCHAR report[64]{};
            ULONG got = 0;
            if (WinUsb_ReadPipe(usb, in_pipe, report, sizeof(report), &got, nullptr)) {
                std::array<GcPortState, 4> parsed{};
                if (parse_gc_adapter_report(report, got, parsed)) {
                    std::lock_guard lock(mutex);
                    ports = parsed;
                }
                continue;
            }
            if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
            // Unplugged or taken: every port disconnects; look again later.
            close_adapter();
            is_open = false;
            std::lock_guard lock(mutex);
            ports = {};
        }
        close_adapter();
    }
#else
    void run() {}
#endif
};

GcAdapter::GcAdapter() : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    impl_->reader = std::thread([impl = impl_.get()] { impl->run(); });
#endif
}

GcAdapter::~GcAdapter() {
    impl_->stop = true;
    if (impl_->reader.joinable()) impl_->reader.join();
    for (auto& port : impl_->virtual_ports) {
        if (port.joystick) SDL_CloseJoystick(port.joystick);
        if (port.id) SDL_DetachVirtualJoystick(port.id);
    }
}

bool GcAdapter::open() const { return impl_->is_open; }

void GcAdapter::update() {
    std::array<GcPortState, 4> ports;
    {
        std::lock_guard lock(impl_->mutex);
        ports = impl_->ports;
    }
    for (std::size_t p = 0; p < 4; ++p) {
        auto& port = impl_->virtual_ports[p];
        if (ports[p].connected && !port.id) {
            // A controller on this port: a virtual GameCube controller as
            // SDL's own driver would make it.
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.vendor_id = 0x057e;
            desc.product_id = 0x0337;
            desc.name = "Nintendo GameCube Controller";
            port.id = SDL_AttachVirtualJoystick(&desc);
            port.joystick = port.id ? SDL_OpenJoystick(port.id) : nullptr;
            port.origin = ports[p]; // (the GameCube's own calibration: the sticks at connection)
            port.origin.buttons1 = port.origin.buttons2 = 0;
            port.sent = {};
            port.origin_set = true;
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
