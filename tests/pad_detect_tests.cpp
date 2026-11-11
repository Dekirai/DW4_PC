// Controller detection through SDL itself (docs/ROUND_PADS_NOTES.md 6):
// SDL virtual joysticks with each requested controller's USB ids and name
// (SDL derives the type and the button labels from them as for the real
// device), every real backend off so no controller of this PC takes part,
// no window. Checks the classification of what SDL reports, the prompt
// tracker driven by SDL's own events (last device with real input on
// player 1's slot wins, stick drift and trigger noise do not switch), the
// fallback when a pad is unplugged, the Wii remotes SDL's Wii driver makes
// pads of (no player slot), a pad's input and the menus' keys through the
// host's own code (pad_host.hpp, as host_view.cpp runs it), and the Switch 2
// Pro Controller end to end (docs/ROUND_LIBUSB_NOTES.md).
#include "../src/game/gc_adapter.hpp"
#include "../src/game/pad_family.hpp"
#include "../src/game/pad_host.hpp"

#include <SDL3/SDL.h>

#include <cmath>
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

// The host's controller part of host_event: the game's own pad_slots_event
// (pad_host.hpp) on SDL's events; returns how many events came.
struct Host {
    std::vector<SDL_Gamepad*> pads;
    PromptTracker tracker;
    Settings settings;
    int pump() {
        SDL_UpdateJoysticks();
        int count = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ++count;
            pad_slots_event(pads, tracker, event);
        }
        return count;
    }
    // A pad's input as read_live_input takes it.
    dw4::HostPadInput input(std::size_t slot) { return pad_input(settings, family_of(tracker, pads.at(slot)), pads.at(slot)); }
    MenuKeys menu(std::size_t slot) {
        const PadFamily family = family_of(tracker, pads.at(slot));
        return menu_keys(settings, family, read_gamepad(pads.at(slot)), family != PadFamily::gamecube || trigger_clicks(pads.at(slot)));
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
    // A trigger resting a little in (noise, a worn spring) does not switch;
    // a pulled one does. (SDL's virtual triggers run -32768..32767 raw.)
    SDL_PushEvent(&key);
    host.pump();
    SDL_SetJoystickVirtualAxis(host.joystick(ps5), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, std::int16_t(8000 * 2 - 32768));
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::keyboard, "trigger noise does not switch");
    SDL_SetJoystickVirtualAxis(host.joystick(ps5), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, std::int16_t(30000 * 2 - 32768));
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::ps5, "a pulled trigger switches");
    SDL_SetJoystickVirtualAxis(host.joystick(ps5), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
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

// The Wii U mode adapter (gc_adapter.cpp): a report as SDL's GameCube
// driver reads it, its ports as virtual GameCube controllers, and through
// the classification and the 1:1 layout the game's own buttons.
void gamecube_adapter() {
    std::uint8_t report[37]{};
    report[0] = 0x21;
    std::uint8_t* port1 = report + 1;
    port1[0] = 0x10;                     // wired controller
    port1[1] = 0x02 | 0x80;              // B, D-pad up
    port1[2] = 0x02 | 0x08;              // Z, L click
    port1[3] = 128 + 80, port1[4] = 128; // main stick right
    port1[5] = 128, port1[6] = 128 - 80; // C-stick down
    port1[7] = 200, port1[8] = 30;
    report[1 + 9 * 2] = 0x20; // port 3: a WaveBird, nothing pressed
    std::array<GcPortState, 4> ports{};
    check(parse_gc_adapter_report(report, sizeof report, ports), "adapter report parsed");
    check(ports[0].connected && !ports[1].connected && ports[2].connected && !ports[3].connected, "adapter ports connected");
    check(!parse_gc_adapter_report(report, 20, ports), "a short report is none");
    GcPortState rest;
    rest.l = rest.r = 30;
    const auto state = gc_virtual_state(ports[0], rest);
    check(state.buttons[SDL_GAMEPAD_BUTTON_WEST] && state.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP] && state.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] &&
              state.buttons[SDL_GAMEPAD_BUTTON_MISC3] && !state.buttons[SDL_GAMEPAD_BUTTON_SOUTH],
          "adapter buttons in SDL's GameCube layout");
    check(state.axes[SDL_GAMEPAD_AXIS_LEFTX] > 32000 && state.axes[SDL_GAMEPAD_AXIS_RIGHTY] > 32000, "adapter sticks (SDL's y down)");
    GcPortState up = ports[0];
    up.stick_x = 128, up.stick_y = 128 + 80; // main stick up (the GameCube's y points up)
    check(gc_virtual_state(up, rest).axes[SDL_GAMEPAD_AXIS_LEFTY] < -32000, "adapter main stick up is SDL's y up");
    check(state.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > 30000 && state.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] == 0, "adapter triggers");
    // As a virtual controller with the adapter's ids (what GcAdapter::update makes).
    Host host;
    const SDL_JoystickID id = attach({"Nintendo GameCube Controller", 0x057e, 0x0337, PadFamily::gamecube});
    host.pump();
    check(host.pads.size() == 1 && classify(pad_identity(host.pads.front())) == PadFamily::gamecube, "the adapter's port is a GameCube pad");
    SDL_Joystick* joystick = host.joystick(id);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) SDL_SetJoystickVirtualButton(joystick, b, state.buttons[std::size_t(b)]);
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
        int raw = state.axes[std::size_t(a)];
        if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) raw = raw * 2 - 32768;
        SDL_SetJoystickVirtualAxis(joystick, a, std::int16_t(raw));
    }
    host.pump();
    check(host.tracker.player_family(0) == PadFamily::gamecube, "the adapter's input switches the prompts");
    const auto in = host.input(0);
    check(in.buttons == (dw4::pad_button::b | dw4::pad_button::up | dw4::pad_button::z | dw4::pad_button::l), "adapter port 1:1 into the game");
    check(in.stick.x > 0.9f && in.c_stick.y < -0.9f, "adapter sticks into the game");
    // The main stick pushed up walks up in the game (the y the adapter
    // reports up, SDL down, the game up again).
    const auto up_state = gc_virtual_state(up, rest);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, up_state.axes[SDL_GAMEPAD_AXIS_LEFTX]);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, up_state.axes[SDL_GAMEPAD_AXIS_LEFTY]);
    host.pump();
    const auto walk = host.input(0);
    check(walk.stick.y > 0.9f && std::fabs(walk.stick.x) < 0.1f, "adapter main stick up: the game's stick up");
    // The GameCube's L at the click (MISC3), the menus' L2 and its A / B.
    const auto menu = host.menu(0);
    check(menu.face_right && !menu.face_down && menu.l2 > 0.9f, "adapter port in the menus: B goes back, L at its click");
    SDL_DetachVirtualJoystick(id);
    host.pump();
}

// SDL's Wii driver makes gamepads of Wii remotes too (a DolphinBar's four
// slots, a Wii Remote Plus with its extensions): they take no player slot
// and never push the real pads back; the Wii U Pro Controller does.
void wii_remotes() {
    Host host;
    // (A DolphinBar's remotes: 057e:0306. SDL names a virtual 057e:0330 by
    // its own table, the Wii U Pro Controller; the Wii Remote Plus's names
    // and GUIDs are pad_family_tests' part.)
    const SDL_JoystickID remote = attach({"Nintendo Wii Remote", 0x057e, 0x0306, PadFamily::generic});
    const SDL_JoystickID plus = attach({"Nintendo Wii Remote with Nunchuk", 0x057e, 0x0306, PadFamily::generic});
    host.pump();
    check(host.pads.empty(), "Wii remotes take no slot");
    const SDL_JoystickID dualsense = attach(kDevices[0]);
    const SDL_JoystickID wiiu = attach({"Nintendo Wii U Pro Controller", 0x057e, 0x0330, PadFamily::wiiu_pro});
    host.pump();
    check(host.pads.size() == 2 && host.tracker.player_family(0) == PadFamily::ps5 && host.tracker.player_family(1) == PadFamily::wiiu_pro,
          "the DualSense is player 1 and the Wii U Pro player 2, the remotes nobody");
    for (const auto id : {remote, plus, dualsense, wiiu}) SDL_DetachVirtualJoystick(id);
    host.pump();
    check(host.pads.empty(), "all gone");
}

// A pad's input and the menus' keys through the host's own code: a Switch
// pad by label plays its A as the game's A; the menus take the pad's own A
// (menu_buttons=layout) whatever the game's bindings, or the bindings.
void host_input() {
    Host host;
    const SDL_JoystickID sw = attach(kDevices[8]); // Switch Pro
    const SDL_JoystickID xbox = attach(kDevices[7]);
    host.pump();
    check(host.pads.size() == 2, "two pads");
    SDL_SetJoystickVirtualButton(host.joystick(sw), SDL_GAMEPAD_BUTTON_EAST, true); // the button printed A
    host.pump();
    check(host.input(0).buttons == dw4::pad_button::a, "Switch by label: A is the game's A");
    check(host.menu(0).face_down && !host.menu(0).face_right, "Switch by label: A confirms in the menus");
    SDL_SetJoystickVirtualButton(host.joystick(sw), SDL_GAMEPAD_BUTTON_EAST, false);
    // The owner's a=b binding on an Xbox pad: the game's A on B (EAST); the
    // menus keep A (SOUTH) confirming unless they follow the bindings.
    host.settings.pad[std::size_t(dw4::game::PadKey::a)] = {{{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST}}};
    host.settings.pad[std::size_t(dw4::game::PadKey::b)] = {{{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_SOUTH}}};
    SDL_SetJoystickVirtualButton(host.joystick(xbox), SDL_GAMEPAD_BUTTON_SOUTH, true);
    host.pump();
    check(host.input(1).buttons == dw4::pad_button::b, "a=b: Xbox A is the game's B");
    check(host.menu(1).face_down, "menus by layout: Xbox A confirms");
    host.settings.menu_buttons = MenuButtons::bindings;
    check(host.menu(1).face_right && !host.menu(1).face_down, "menus by bindings: Xbox A (the game's B) goes back");
    SDL_SetJoystickVirtualButton(host.joystick(xbox), SDL_GAMEPAD_BUTTON_SOUTH, false);
    // The stick: SDL's y down is the menus' and the game's up.
    SDL_SetJoystickVirtualAxis(host.joystick(xbox), SDL_GAMEPAD_AXIS_LEFTY, -32000);
    host.pump();
    host.settings.menu_buttons = MenuButtons::layout;
    check(host.menu(1).left_y > 0.9f && host.input(1).stick.y > 0.9f, "stick up in the menus and the game");
    for (const auto id : {sw, xbox}) SDL_DetachVirtualJoystick(id);
    host.pump();
}

// The Switch 2 Pro Controller end to end as SDL's Switch 2 driver reports it
// (SDL_hidapi_switch2.c with SDL_gamepad.c's HIDAPI mapping: positional
// face buttons, A on EAST; Minus BACK, Plus START, Home GUIDE, Capture
// MISC1, C MISC2, GR RIGHT_PADDLE1, GL LEFT_PADDLE1): its family, the
// labels, by label (the default) the printed A is the game's A, the
// shoulders and triggers as on the Switch Pro; C, GL and GR play nothing
// until bound in F1 (they still count as the player's input for the
// prompts), then they play the bound button and are drawn and named C, GL,
// GR; Minus opens the menus' back / F1 key, A confirms.
void switch2_pro() {
    Host host;
    check(host.settings.nintendo_layout == NintendoLayout::by_label, "Nintendo pads by label by default");
    const SDL_JoystickID id = attach(kDevices[9]); // 057e:2069
    host.pump();
    check(host.pads.size() == 1 && host.tracker.player_family(0) == PadFamily::switch2_pro, "a Switch 2 Pro Controller");
    SDL_Gamepad* pad = host.pads.front();
    check(SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_A, "SDL labels EAST A");
    check(SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_NORTH) == SDL_GAMEPAD_BUTTON_LABEL_X, "SDL labels NORTH X");
    SDL_Joystick* joystick = host.joystick(id);
    auto held = [&](SDL_GamepadButton button) {
        SDL_SetJoystickVirtualButton(joystick, int(button), true);
        host.pump();
        const auto in = host.input(0);
        SDL_SetJoystickVirtualButton(joystick, int(button), false);
        host.pump();
        return in.buttons;
    };
    namespace pb = dw4::pad_button;
    check(held(SDL_GAMEPAD_BUTTON_EAST) == pb::a, "A is the game's A");
    check(held(SDL_GAMEPAD_BUTTON_SOUTH) == pb::b, "B is the game's B");
    check(held(SDL_GAMEPAD_BUTTON_NORTH) == pb::x, "X is the game's X");
    check(held(SDL_GAMEPAD_BUTTON_WEST) == pb::y, "Y is the game's Y");
    check(held(SDL_GAMEPAD_BUTTON_START) == pb::start, "Plus is START");
    check(held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == pb::l, "L is the game's L");
    check(held(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == pb::z, "R is the game's Z");
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    host.pump();
    check(host.input(0).buttons == (pb::l | pb::r), "ZL is the game's L, ZR its R");
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    host.pump();
    // The extra buttons: nothing by default, but the player's input.
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_K;
    key.key.down = true;
    for (const auto extra : {SDL_GAMEPAD_BUTTON_MISC2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_MISC1,
                             SDL_GAMEPAD_BUTTON_GUIDE}) {
        check(SDL_GamepadHasButton(pad, extra), "SDL reports the button");
        SDL_PushEvent(&key);
        host.pump();
        check(host.tracker.player_family(0) == PadFamily::keyboard, "typed: keyboard prompts");
        check(held(extra) == 0, std::string("unbound by default: ") + SDL_GetGamepadStringForButton(extra));
        check(host.tracker.player_family(0) == PadFamily::switch2_pro, "its press brings the Switch 2 Pro prompts back");
    }
    // Bound in F1 (the capture keeps these buttons as they are), they play
    // and are named and drawn as on the controller.
    check(binding_button(SDL_GAMEPAD_BUTTON_MISC2, PadFamily::switch2_pro, NintendoLayout::by_label) == SDL_GAMEPAD_BUTTON_MISC2 &&
              binding_button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, PadFamily::switch2_pro, NintendoLayout::by_label) == SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
          "the capture keeps C and GL");
    auto& bindings = host.settings.pad;
    bindings[std::size_t(PadKey::z)][1] = {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1};
    bindings[std::size_t(PadKey::l)][2] = {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1};
    bindings[std::size_t(PadKey::y)][1] = {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_MISC2};
    check(held(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1) == pb::z, "GR bound to Z");
    check(held(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1) == pb::l, "GL bound to L");
    check(held(SDL_GAMEPAD_BUTTON_MISC2) == pb::y, "C bound to Y");
    check(pad_input_label(PadFamily::switch2_pro, host.settings, {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1}) == "GR" &&
              pad_input_label(PadFamily::switch2_pro, host.settings, {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1}) == "GL" &&
              pad_input_label(PadFamily::switch2_pro, host.settings, {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_MISC2}) == "C",
          "F1 names GR, GL, C");
    check(pad_input_label(PadFamily::switch_pro, host.settings, {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_MISC2}) != "C",
          "a Switch Pro has no C");
    check(prompt_face(PadFamily::switch2_pro, host.settings, PromptControl::a).face == Face::letter_a, "the A prompt");
    check(prompt_face(PadFamily::switch2_pro, host.settings, PromptControl::start).face == Face::plus, "the START prompt is Plus");
    check(prompt_face(PadFamily::switch2_pro, host.settings, PromptControl::z).face == Face::r, "Z is R (the first binding)");
    bindings[std::size_t(PadKey::z)] = {{{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1}}};
    check(prompt_face(PadFamily::switch2_pro, host.settings, PromptControl::z).face == Face::gr, "Z on GR alone: the GR picture");
    // The menus: A confirms, B goes back, Minus is the menus' back / F1 key.
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_BACK, true);
    host.pump();
    const auto menu = host.menu(0);
    check(menu.face_down && !menu.face_right && menu.back, "A confirms, Minus is the back key");
    SDL_DetachVirtualJoystick(id);
    host.pump();
    check(host.pads.empty(), "unplugged");
}

} // namespace

int main() {
    // No real controller of this PC takes part (every backend but the
    // virtual one off), and no window: events arrive without focus.
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                             SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_HIDAPI_LIBUSB})
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
        gamecube_adapter();
        wii_remotes();
        host_input();
        switch2_pro();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    if (!result) std::cout << checks << " pad detection checks passed\n";
    return result;
}
