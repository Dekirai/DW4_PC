// Controller detection through SDL itself (docs/ROUND_PADS_NOTES.md 6):
// SDL virtual joysticks with each requested controller's USB ids and name
// (SDL derives the type and the button labels from them as for the real
// device), every real backend off so no controller of this PC takes part,
// no window. Checks the classification of what SDL reports, the prompt
// tracker driven by SDL's own events (last device with real input on
// player 1's slot wins, stick drift does not switch) and the fallback when
// a pad is unplugged.
#include "../src/game/pad_family.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4::game;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

struct Device {
    const char* name;
    std::uint16_t vendor, product;
    PadFamily family;
};
// The requested controllers (and variants) as SDL 3.4.16 names them.
const Device kDevices[] = {
    {"DualSense Wireless Controller", 0x054c, 0x0ce6, PadFamily::ps5},
    {"DualSense Edge Wireless Controller", 0x054c, 0x0df2, PadFamily::ps5},
    {"PS4 Controller", 0x054c, 0x05c4, PadFamily::ps4},
    {"PS4 Controller", 0x054c, 0x09cc, PadFamily::ps4},
    {"Xbox 360 Controller", 0x045e, 0x028e, PadFamily::xbox360},
    {"Xbox 360 Wireless Controller", 0x045e, 0x02a1, PadFamily::xbox360},
    {"Xbox One S Controller", 0x045e, 0x02ea, PadFamily::xbox_one},
    {"Xbox Series X Controller", 0x045e, 0x0b12, PadFamily::xbox_one},
    {"Nintendo Switch Pro Controller", 0x057e, 0x2009, PadFamily::switch_pro},
    {"Nintendo Switch Pro Controller", 0x057e, 0x2069, PadFamily::switch2_pro},
    {"Nintendo Wii U Pro Controller", 0x057e, 0x0330, PadFamily::wiiu_pro},
    {"Nintendo GameCube Controller", 0x057e, 0x0337, PadFamily::gamecube},
    {"Nintendo GameCube Controller", 0x0079, 0x1844, PadFamily::gamecube},
    {"Nintendo GameCube Controller", 0x0079, 0x1843, PadFamily::gamecube},
};

SDL_JoystickID attach(const Device& device) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.vendor_id = device.vendor;
    desc.product_id = device.product;
    desc.name = device.name;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    check(id != 0, std::string("attach ") + device.name + ": " + SDL_GetError());
    return id;
}

// The host's part of host_event (host_view.cpp): open added pads, classify
// them, follow the real input; returns how many events came.
struct Host {
    std::vector<SDL_Gamepad*> pads;
    PromptTracker tracker;
    int pump() {
        SDL_UpdateJoysticks();
        int count = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ++count;
            if (const auto kind = real_input(event); kind == InputKind::keyboard) tracker.key_input();
            else if (kind == InputKind::pad)
                tracker.pad_input(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event.gbutton.which : event.gaxis.which);
            if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                if (auto* pad = SDL_OpenGamepad(event.gdevice.which)) {
                    pads.push_back(pad);
                    const auto identity = pad_identity(pad);
                    tracker.pad_added(event.gdevice.which, classify(identity), identity.name);
                }
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                for (auto it = pads.begin(); it != pads.end(); ++it)
                    if (SDL_GetGamepadID(*it) == event.gdevice.which) {
                        SDL_CloseGamepad(*it);
                        pads.erase(it);
                        break;
                    }
                tracker.pad_removed(event.gdevice.which);
            }
        }
        return count;
    }
    SDL_Joystick* joystick(SDL_JoystickID id) {
        for (auto* pad : pads)
            if (SDL_GetGamepadID(pad) == id) return SDL_GetGamepadJoystick(pad);
        return nullptr;
    }
};

void press(Host& host, SDL_JoystickID id, SDL_GamepadButton button) {
    SDL_Joystick* joystick = host.joystick(id);
    check(joystick != nullptr, "the pad is open");
    SDL_SetJoystickVirtualButton(joystick, int(button), true);
    host.pump();
    SDL_SetJoystickVirtualButton(joystick, int(button), false);
    host.pump();
}

void classification_through_sdl() {
    for (const auto& device : kDevices) {
        Host host;
        const SDL_JoystickID id = attach(device);
        host.pump();
        check(host.pads.size() == 1, std::string("opened as a gamepad: ") + device.name);
        const auto identity = pad_identity(host.pads.front());
        check(identity.vendor == device.vendor && identity.product == device.product, "ids as given");
        const PadFamily family = classify(identity);
        check(family == device.family, std::string("family of ") + device.name + " " + std::to_string(device.product) + ": got " +
                                           family_id(family) + " (SDL type " + std::to_string(int(identity.type)) + ")");
        // SDL's own labels agree with the families' (the Wii U Pro is the
        // one SDL labels like an Xbox pad: the family table decides there).
        const auto south = SDL_GetGamepadButtonLabel(host.pads.front(), SDL_GAMEPAD_BUTTON_SOUTH);
        if (family == PadFamily::ps4 || family == PadFamily::ps5) check(south == SDL_GAMEPAD_BUTTON_LABEL_CROSS, "PlayStation label");
        if (family == PadFamily::switch_pro || family == PadFamily::switch2_pro) check(south == SDL_GAMEPAD_BUTTON_LABEL_B, "Switch: SOUTH is B");
        if (family == PadFamily::gamecube) {
            check(south == SDL_GAMEPAD_BUTTON_LABEL_A, "GameCube: SOUTH is A");
            check(SDL_GetGamepadButtonLabel(host.pads.front(), SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_X, "GameCube: EAST is X");
        }
        check(host.tracker.player_family(0) == device.family, "the only pad sets the prompts before any input");
        SDL_CloseGamepad(host.pads.front());
        SDL_DetachVirtualJoystick(id);
        host.pads.clear();
        host.pump();
    }
}

void last_input_wins() {
    Host host;
    const SDL_JoystickID ps5 = attach(kDevices[0]);
    host.pump();
    const SDL_JoystickID xbox = attach(kDevices[7]);
    host.pump();
    check(host.pads.size() == 2, "two pads");
    check(host.tracker.player_family(0) == PadFamily::ps5 && host.tracker.player_family(1) == PadFamily::xbox_one, "slots in plug order");
    // Player 1 types: keyboard prompts; presses his pad: the pad's.
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_K;
    key.key.down = true;
    SDL_PushEvent(&key);
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::keyboard, "a key switches to the keyboard");
    press(host, ps5, SDL_GAMEPAD_BUTTON_SOUTH);
    check(host.tracker.player_family(0) == PadFamily::ps5, "player 1's pad switches back");
    // Player 2's pad does not take player 1's prompts.
    SDL_PushEvent(&key);
    host.pump();
    press(host, xbox, SDL_GAMEPAD_BUTTON_EAST);
    check(host.tracker.player_family(0) == PadFamily::keyboard, "player 2's input leaves player 1's prompts");
    // A drifting stick does not switch; a pushed one does.
    SDL_SetJoystickVirtualAxis(host.joystick(ps5), SDL_GAMEPAD_AXIS_LEFTX, 6000);
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::keyboard, "drift does not switch");
    SDL_SetJoystickVirtualAxis(host.joystick(ps5), SDL_GAMEPAD_AXIS_LEFTX, 30000);
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::ps5, "a pushed stick switches");
    SDL_SetJoystickVirtualAxis(host.joystick(ps5), SDL_GAMEPAD_AXIS_LEFTX, 0);
    host.pump();
    // Unplugging player 1's pad: the next pad becomes player 1 and the
    // prompts follow it.
    SDL_DetachVirtualJoystick(ps5);
    host.pump();
    check(host.pads.size() == 1 && host.tracker.player_family(0) == PadFamily::xbox_one, "unplugged: the next pad's prompts");
    // A pad plugged in later switches only on its input.
    SDL_PushEvent(&key);
    host.pump();
    const SDL_JoystickID gc = attach(kDevices[12]);
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::keyboard, "a new pad switches nothing yet");
    SDL_DetachVirtualJoystick(xbox);
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::keyboard, "the keyboard stays");
    press(host, gc, SDL_GAMEPAD_BUTTON_SOUTH);
    check(host.tracker.player_family(0) == PadFamily::gamecube, "the GameCube pad (now player 1's) pressed");
    SDL_DetachVirtualJoystick(gc);
    host.pump();
    check(host.pads.empty() && host.tracker.player_family(0) == PadFamily::keyboard, "nothing left: keyboard");
}

} // namespace

int main() {
    // No real controller of this PC takes part (every backend but the
    // virtual one off), and no window: events arrive without focus.
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                             SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT})
        SDL_SetHint(hint, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        std::cerr << "FAIL: SDL_Init: " << SDL_GetError() << '\n';
        return 1;
    }
    int result = 0;
    try {
        int connected = 0;
        SDL_free(SDL_GetJoysticks(&connected));
        check(connected == 0, "no real controller reaches the test");
        classification_through_sdl();
        last_input_wins();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    if (!result) std::cout << checks << " pad detection checks passed\n";
    return result;
}
