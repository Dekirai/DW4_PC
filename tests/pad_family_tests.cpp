// Controller families (src/game/pad_family, button_art; docs/ROUND_PADS_NOTES.md)
// without a device or window: the classification by SDL type, USB ids and
// name, the binding spaces (Nintendo pads by label or position, a GameCube
// pad 1:1), which picture a binding shows on each family, the prompt
// tracker (last device with real input on player 1's slot), the settings
// migration, and the pictures themselves (every family's atlas, the title's
// PRESS START variant). DW4_PADS_ART_DIR=<dir> writes the atlases as BMPs.
#include "../src/game/button_art.hpp"
#include "../src/game/controls.hpp"
#include "../src/game/gc_adapter.hpp"
#include "../src/game/pad_family.hpp"
#include "../src/game/prompt_layout.hpp"
#include "../src/game/settings.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/ini.hpp"
#include "dw4/pad.hpp"

#include <SDL3/SDL_events.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4;
using namespace dw4::game;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

PadIdentity id(SDL_GamepadType type, std::uint16_t vendor, std::uint16_t product, const char* name) { return {type, vendor, product, name}; }

void classification() {
    // The ids and names SDL 3.4.16 reports for the requested controllers
    // (usb_ids.h, controller_list.h, the HIDAPI drivers' names).
    check(classify(id(SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0ce6, "DualSense Wireless Controller")) == PadFamily::ps5, "DualSense");
    check(classify(id(SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0df2, "DualSense Edge Wireless Controller")) == PadFamily::ps5, "DualSense Edge");
    check(classify(id(SDL_GAMEPAD_TYPE_PS4, 0x054c, 0x05c4, "PS4 Controller")) == PadFamily::ps4, "DualShock 4 v1");
    check(classify(id(SDL_GAMEPAD_TYPE_PS4, 0x054c, 0x09cc, "PS4 Controller")) == PadFamily::ps4, "DualShock 4 v2");
    check(classify(id(SDL_GAMEPAD_TYPE_XBOX360, 0x045e, 0x028e, "Xbox 360 Controller")) == PadFamily::xbox360, "Xbox 360 wired");
    check(classify(id(SDL_GAMEPAD_TYPE_XBOX360, 0x045e, 0x02a1, "Xbox 360 Wireless Controller")) == PadFamily::xbox360, "Xbox 360 wireless");
    check(classify(id(SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x02ea, "Xbox One S Controller")) == PadFamily::xbox_one, "Xbox One S");
    check(classify(id(SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x0b12, "Xbox Series X Controller")) == PadFamily::xbox_one, "Xbox Series");
    check(classify(id(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2009, "Nintendo Switch Pro Controller")) == PadFamily::switch_pro,
          "Switch Pro");
    // The Switch 2 Pro has the Switch Pro's type and name in SDL: its id decides.
    check(classify(id(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2069, "Nintendo Switch Pro Controller")) == PadFamily::switch2_pro,
          "Switch 2 Pro by product id");
    // SDL's Wii driver: type STANDARD, its own name. 057e:0330 is also
    // every Wii Remote Plus: only the name SDL sets after reading the
    // extension tells the Wii U Pro Controller.
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii U Pro Controller")) == PadFamily::wiiu_pro, "Wii U Pro by name");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "")) == PadFamily::generic, "057e:0330 without the name is no Wii U Pro");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii Remote with Classic Controller")) == PadFamily::generic,
          "a Wii Remote Plus with a Classic Controller is no Wii U Pro");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii Remote with Nunchuk")) == PadFamily::generic,
          "a Wii Remote Plus with a Nunchuk is no Wii U Pro");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0306, "Nintendo Wii Remote")) == PadFamily::generic, "a Wii remote is no Wii U Pro");
    // Player slots: Wii remotes (a DolphinBar's slots, a Wii Remote Plus)
    // take none; every other pad does.
    check(player_pad(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii U Pro Controller")), "the Wii U Pro is a player's pad");
    check(!player_pad(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0306, "Nintendo Wii Remote")), "a Wii remote takes no slot");
    check(!player_pad(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii Remote with Nunchuk")), "a Wii Remote Plus takes no slot");
    check(!player_pad(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0306, "Nintendo Wii Remote with Classic Controller")),
          "a DolphinBar's remote with a Classic Controller takes no slot");
    // SDL's Wii driver's GUID tells the extension whatever the name says.
    PadIdentity driver = id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii U Pro Controller");
    driver.wii_extension = 129; // a Nunchuk
    check(classify(driver) == PadFamily::generic && !player_pad(driver), "Wii driver GUID: a Wii Remote Plus with a Nunchuk");
    driver.name.clear();
    driver.wii_extension = kWiiExtensionWiiUPro;
    check(classify(driver) == PadFamily::wiiu_pro && player_pad(driver), "Wii driver GUID: the Wii U Pro Controller");
    check(player_pad(id(SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0ce6, "DualSense Wireless Controller")) &&
              player_pad(id(SDL_GAMEPAD_TYPE_GAMECUBE, 0x057e, 0x0337, "Nintendo GameCube Controller")) &&
              player_pad(id(SDL_GAMEPAD_TYPE_UNKNOWN, 0x1234, 0x5678, "Some Gamepad")),
          "every other pad takes a slot");
    // SDL's Wii driver: automatic only while 057e:0330 is connected.
    check(!wii_driver_wanted(WiiDriver::automatic, false) && wii_driver_wanted(WiiDriver::automatic, true), "Wii driver automatic");
    check(wii_driver_wanted(WiiDriver::on, false) && !wii_driver_wanted(WiiDriver::off, true), "Wii driver on / off");
    // The character select's lines are the players' own.
    check(line_player("CHRSELFONT06", "font000") == 0 && line_player("CHRSELFONT06", "font103") == 1 &&
              line_player("CHRSELFONT16", "font201") == 2 && line_player("CHRSELFONT07", "font310") == 3,
          "character select lines by player");
    check(line_player("TITLEFONT01", "font105") == 0 && line_player("STWINFONT29", "font100") == 0 && line_player("CHRSELFONT06", "font03") == 0 &&
              line_player("CHRSELFONT06", "font9x1") == 0 && line_player("CHRSELFONT06", "font400") == 0,
          "other lines are player 1's");
    // GameCube adapters: the official / Wii U mode, Mayflash PC mode, the type alone.
    check(classify(id(SDL_GAMEPAD_TYPE_GAMECUBE, 0x057e, 0x0337, "Nintendo GameCube Controller")) == PadFamily::gamecube, "GC adapter");
    check(classify(id(SDL_GAMEPAD_TYPE_GAMECUBE, 0x0079, 0x1844, "Nintendo GameCube Controller")) == PadFamily::gamecube, "Mayflash PC mode");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x0079, 0x1843, "Mayflash GameCube Controller Adapter")) == PadFamily::gamecube,
          "Mayflash through DirectInput's database");
    check(classify(id(SDL_GAMEPAD_TYPE_UNKNOWN, 0x1234, 0x5678, "Some Gamepad")) == PadFamily::generic, "an unknown pad");
    // Every family has a name and an id that reads back.
    for (std::size_t f = 0; f < kPadFamilyCount; ++f) {
        check(family_name(PadFamily(f))[0] != 0, "family name");
        check(family_from_id(family_id(PadFamily(f))) == PadFamily(f), std::string("family id ") + family_id(PadFamily(f)));
    }
    check(!family_from_id("auto"), "auto is no family");
}

GamepadState pressed(std::initializer_list<SDL_GamepadButton> buttons) {
    GamepadState state;
    for (const auto b : buttons) state.buttons[std::size_t(b)] = true;
    return state;
}

void binding_spaces() {
    const Settings defaults;
    const auto game = [&](PadFamily family, NintendoLayout layout, const GamepadState& state) {
        return gamepad_input(defaults, binding_state(state, family, layout)).buttons;
    };
    // Xbox, PlayStation, other pads: by position exactly as before (every
    // single button and every pair of the face buttons).
    for (const auto family : {PadFamily::xbox360, PadFamily::xbox_one, PadFamily::ps4, PadFamily::ps5, PadFamily::generic})
        for (int a = 0; a < SDL_GAMEPAD_BUTTON_COUNT; ++a)
            for (int b = 0; b < 4; ++b) {
                const auto state = pressed({SDL_GamepadButton(a), SDL_GamepadButton(b)});
                check(binding_state(state, family, NintendoLayout::by_label).buttons == state.buttons, "Xbox / PlayStation states unchanged");
                check(gamepad_input(defaults, binding_state(state, family, NintendoLayout::by_label)).buttons ==
                          gamepad_input(defaults, state).buttons,
                      "Xbox / PlayStation input unchanged");
            }
    // Nintendo pads by label: the printed A (SDL's EAST) is the game's A.
    for (const auto family : {PadFamily::switch_pro, PadFamily::switch2_pro, PadFamily::wiiu_pro}) {
        check(game(family, NintendoLayout::by_label, pressed({SDL_GAMEPAD_BUTTON_EAST})) == pad_button::a, "Nintendo A by label");
        check(game(family, NintendoLayout::by_label, pressed({SDL_GAMEPAD_BUTTON_SOUTH})) == pad_button::b, "Nintendo B by label");
        check(game(family, NintendoLayout::by_label, pressed({SDL_GAMEPAD_BUTTON_NORTH})) == pad_button::x, "Nintendo X by label");
        check(game(family, NintendoLayout::by_label, pressed({SDL_GAMEPAD_BUTTON_WEST})) == pad_button::y, "Nintendo Y by label");
        // By position: like an Xbox pad.
        check(game(family, NintendoLayout::by_position, pressed({SDL_GAMEPAD_BUTTON_SOUTH})) == pad_button::a, "Nintendo A by position");
        check(game(family, NintendoLayout::by_position, pressed({SDL_GAMEPAD_BUTTON_EAST})) == pad_button::b, "Nintendo B by position");
        check(binding_button(SDL_GAMEPAD_BUTTON_EAST, family, NintendoLayout::by_label) == SDL_GAMEPAD_BUTTON_SOUTH, "capture by label");
        check(binding_button(SDL_GAMEPAD_BUTTON_WEST, family, NintendoLayout::by_label) == SDL_GAMEPAD_BUTTON_NORTH, "capture by label (Y)");
        check(binding_button(SDL_GAMEPAD_BUTTON_EAST, family, NintendoLayout::by_position) == SDL_GAMEPAD_BUTTON_EAST, "capture by position");
        check(game(family, NintendoLayout::by_label, pressed({SDL_GAMEPAD_BUTTON_START})) == pad_button::start, "Nintendo + is Start");
    }
    // The GameCube pad, 1:1 (SDL: A SOUTH, X EAST, B WEST, Y NORTH, Z the
    // right shoulder, L / R analog with their clicks on MISC3 / MISC4).
    const auto gc = [&](const GamepadState& state, bool clicks = true) { return gamecube_input(state, clicks).buttons; };
    check(gc(pressed({SDL_GAMEPAD_BUTTON_SOUTH})) == pad_button::a, "GC A");
    check(gc(pressed({SDL_GAMEPAD_BUTTON_WEST})) == pad_button::b, "GC B");
    check(gc(pressed({SDL_GAMEPAD_BUTTON_EAST})) == pad_button::x, "GC X");
    check(gc(pressed({SDL_GAMEPAD_BUTTON_NORTH})) == pad_button::y, "GC Y");
    check(gc(pressed({SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER})) == pad_button::z, "GC Z");
    check(gc(pressed({SDL_GAMEPAD_BUTTON_START})) == pad_button::start, "GC Start");
    check(gc(pressed({SDL_GAMEPAD_BUTTON_DPAD_UP})) == pad_button::up, "GC D-pad");
    GamepadState half;
    half.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 24000;
    check(gc(half) == 0, "GC L half way is no L (the click is)");
    half.buttons[SDL_GAMEPAD_BUTTON_MISC3] = true;
    check(gc(half) == pad_button::l, "GC L click");
    GamepadState right;
    right.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32000;
    right.buttons[SDL_GAMEPAD_BUTTON_MISC4] = true;
    check(gc(right) == pad_button::r, "GC R click");
    GamepadState no_clicks;
    no_clicks.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 30000;
    check(gc(no_clicks, false) == pad_button::l, "GC L without click buttons: deep press");
    no_clicks.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 17000;
    check(gc(no_clicks, false) == 0, "GC L without click buttons: half way is no L");
    GamepadState sticks;
    sticks.axes[SDL_GAMEPAD_AXIS_LEFTX] = 32767;
    sticks.axes[SDL_GAMEPAD_AXIS_RIGHTY] = -32767;
    const auto in = gamecube_input(sticks);
    check(in.stick.x > 0.99f && in.c_stick.y > 0.99f, "GC main stick and C-stick");
    // The GameCube pad on the bindings layout: its own labels in the binding space.
    check(game(PadFamily::gamecube, NintendoLayout::by_label, pressed({SDL_GAMEPAD_BUTTON_WEST})) == pad_button::b, "GC B through bindings");
    check(binding_button(SDL_GAMEPAD_BUTTON_WEST, PadFamily::gamecube, NintendoLayout::by_label) == SDL_GAMEPAD_BUTTON_EAST, "GC B capture");
}

Face face(PadFamily family, PromptControl control, const Settings& settings) { return prompt_face(family, settings, control).face; }

void pictures_follow_bindings() {
    Settings s;
    // Defaults.
    check(face(PadFamily::ps5, PromptControl::a, s) == Face::cross, "PS A is cross");
    check(face(PadFamily::ps4, PromptControl::b, s) == Face::circle, "PS B is circle");
    check(face(PadFamily::xbox_one, PromptControl::x, s) == Face::letter_x, "Xbox X");
    check(face(PadFamily::xbox_one, PromptControl::start, s) == Face::menu, "Xbox One Start is Menu");
    check(face(PadFamily::xbox360, PromptControl::start, s) == Face::start_360, "Xbox 360 Start");
    check(face(PadFamily::ps5, PromptControl::start, s) == Face::options, "PS Options");
    check(face(PadFamily::switch_pro, PromptControl::start, s) == Face::plus, "Switch +");
    check(face(PadFamily::switch_pro, PromptControl::a, s) == Face::letter_a, "Switch by label: A shows A");
    check(face(PadFamily::ps5, PromptControl::l, s) == Face::l2, "L is the left trigger first: L2");
    check(face(PadFamily::xbox_one, PromptControl::l, s) == Face::lt, "Xbox L: LT");
    check(face(PadFamily::switch_pro, PromptControl::l, s) == Face::zl, "Switch L: ZL");
    check(face(PadFamily::wiiu_pro, PromptControl::z, s) == Face::r, "Wii U Z: R");
    check(face(PadFamily::gamecube, PromptControl::z, s) == Face::z, "GC Z");
    check(face(PadFamily::xbox_one, PromptControl::menu, s) == Face::view, "Xbox One View opens the menu");
    check(face(PadFamily::ps4, PromptControl::menu, s) == Face::share, "DualShock 4 Share");
    check(face(PadFamily::switch2_pro, PromptControl::menu, s) == Face::minus, "Switch 2 minus");
    check(prompt_face(PadFamily::gamecube, s, PromptControl::menu).key == "F1", "GC pad: F1 opens the menu");
    check(face(PadFamily::xbox_one, PromptControl::stick, s) == Face::left_stick, "stick");
    check(face(PadFamily::gamecube, PromptControl::c_stick, s) == Face::c_stick, "GC C-stick");
    // A rebinding changes the picture: A on EAST (the user's own a=b).
    Settings rebound;
    rebound.pad[std::size_t(PadKey::a)] = {{{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST}}};
    check(face(PadFamily::ps5, PromptControl::a, rebound) == Face::circle, "rebound A on PS: circle");
    check(face(PadFamily::xbox_one, PromptControl::a, rebound) == Face::letter_b, "rebound A on Xbox: B");
    check(face(PadFamily::switch_pro, PromptControl::a, rebound) == Face::letter_b, "rebound A on Switch by label: B");
    rebound.nintendo_layout = NintendoLayout::by_position;
    check(face(PadFamily::switch_pro, PromptControl::a, rebound) == Face::letter_a, "rebound A on Switch by position: A");
    check(face(PadFamily::switch_pro, PromptControl::b, rebound) == Face::letter_a, "B (EAST) by position: A");
    // The port's menus (menu_buttons=layout, the default): the pad's own
    // A / Cross confirms, B / Circle goes back, whatever the bindings.
    check(face(PadFamily::ps5, PromptControl::confirm, rebound) == Face::cross, "menus: Cross confirms though A is rebound");
    check(face(PadFamily::ps5, PromptControl::back, rebound) == Face::circle, "menus: Circle goes back");
    check(face(PadFamily::xbox_one, PromptControl::erase, rebound) == Face::letter_x && face(PadFamily::xbox_one, PromptControl::option, rebound) == Face::letter_y,
          "menus: X deletes, Y capitals");
    check(face(PadFamily::switch_pro, PromptControl::confirm, s) == Face::letter_a && face(PadFamily::switch_pro, PromptControl::back, s) == Face::letter_b,
          "menus: a Switch pad by label confirms with A");
    check(face(PadFamily::switch_pro, PromptControl::confirm, rebound) == Face::letter_b, "menus: a Switch pad by position confirms with its bottom button");
    check(face(PadFamily::gamecube, PromptControl::confirm, s) == Face::letter_a && face(PadFamily::gamecube, PromptControl::back, s) == Face::letter_b &&
              face(PadFamily::gamecube, PromptControl::option, s) == Face::letter_y,
          "menus: the GameCube pad's A and B");
    // menu_buttons=bindings: they follow the game's A / B / X / Y.
    Settings follow = rebound;
    follow.menu_buttons = MenuButtons::bindings;
    follow.nintendo_layout = NintendoLayout::by_label;
    check(face(PadFamily::ps5, PromptControl::confirm, follow) == Face::circle, "menus by bindings: confirm follows A");
    check(prompt_face(PadFamily::keyboard, s, PromptControl::option).key == "Left Shift", "keyboard menus: Shift for capitals");
    // Keyboard: the bound keys.
    check(prompt_face(PadFamily::keyboard, s, PromptControl::a).key == "Return", "keyboard A: Return");
    check(prompt_face(PadFamily::keyboard, s, PromptControl::start).key == "Escape", "keyboard Start: Escape");
    check(prompt_face(PadFamily::keyboard, s, PromptControl::stick).keys[0] == "W", "keyboard stick: W");
    check(prompt_face(PadFamily::keyboard, s, PromptControl::back).key == "Escape", "keyboard menus: Esc goes back");
    Settings keys;
    keys.keys[std::size_t(PadKey::a)] = {SDL_SCANCODE_SPACE};
    check(prompt_face(PadFamily::keyboard, keys, PromptControl::a).key == "Space", "rebound key");
    check(key_cap_label("Return") == "ENTER" && key_cap_label("Keypad 8") == "8" && key_cap_label("Up") == "^" &&
              key_cap_label("Left Shift") == "SHIFT" && key_cap_label("Escape") == "ESC",
          "key cap labels");
    // F1 names.
    check(pad_input_label(PadFamily::ps5, s, {PadInput::Kind::axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER}) == "L2", "F1: L2");
    check(pad_input_label(PadFamily::switch_pro, s, {PadInput::Kind::button, SDL_GAMEPAD_BUTTON_SOUTH}) == "A", "F1: Switch by label");
    check(pad_input_label(PadFamily::xbox_one, s, {}).empty(), "F1: unbound");
}

void tracker() {
    PromptTracker t;
    check(t.player_family(0) == PadFamily::keyboard, "nothing connected: keyboard");
    t.pad_added(7, PadFamily::ps5, "DualSense Wireless Controller");
    check(t.player_family(0) == PadFamily::ps5, "a pad and no input yet: the pad");
    t.key_input();
    check(t.player_family(0) == PadFamily::keyboard, "a key: the keyboard");
    t.pad_input(7);
    check(t.player_family(0) == PadFamily::ps5, "the pad again");
    t.pad_added(9, PadFamily::xbox_one, "Xbox Series X Controller");
    t.pad_input(9);
    check(t.player_family(0) == PadFamily::ps5, "player 2's pad does not change player 1's prompts");
    check(t.player_family(1) == PadFamily::xbox_one, "player 2's family");
    t.pad_removed(7);
    check(t.player_family(0) == PadFamily::xbox_one, "player 1's pad gone: the new first pad");
    t.key_input();
    t.pad_added(11, PadFamily::switch_pro, "Nintendo Switch Pro Controller");
    check(t.player_family(0) == PadFamily::keyboard, "a new pad switches only on its input");
    t.pad_removed(9);
    check(t.player_family(0) == PadFamily::keyboard, "still the keyboard");
    t.pad_input(11);
    check(t.player_family(0) == PadFamily::switch_pro, "the Switch pad (now player 1's) pressed");
    t.pad_removed(11);
    check(t.player_family(0) == PadFamily::keyboard, "every pad gone: keyboard");
    check(t.player_family(3) == PadFamily::generic, "an empty slot");

    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_RETURN;
    check(real_input(e) == InputKind::keyboard, "a key is input");
    e.key.repeat = true;
    check(real_input(e) == InputKind::none, "a repeat is not");
    e.key.repeat = false;
    e.key.scancode = SDL_SCANCODE_F1;
    check(real_input(e) == InputKind::none, "F1 is the host's");
    e.key.scancode = SDL_SCANCODE_LSHIFT;
    check(real_input(e) == InputKind::none, "a modifier alone is not");
    SDL_Event a{};
    a.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    a.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    a.gaxis.value = 9000;
    check(real_input(a) == InputKind::none, "stick drift is not input");
    a.gaxis.value = -20000;
    check(real_input(a) == InputKind::pad, "a pushed stick is");
    a.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    a.gaxis.value = 12000;
    check(real_input(a) == InputKind::none, "a light trigger is not");
    a.gaxis.value = 20000;
    check(real_input(a) == InputKind::pad, "a pulled trigger is");
}

void migration() {
    // No [gamepad] faces in the file: Nintendo pads by label.
    check(Settings::parse(IniFile::parse("[window]\nwidth=1280\n")).nintendo_layout == NintendoLayout::by_label, "new file: by label");
    // The default faces written out by an earlier version: by label too.
    check(Settings::parse(IniFile::parse("[gamepad]\na=a\nb=b\nx=x\ny=y\n")).nintendo_layout == NintendoLayout::by_label, "default faces: by label");
    // A file whose faces a player bound his own way (this owner's a=b, b=a,
    // x=y, y=x) keeps them by position: every binding stays what it was.
    const auto custom = Settings::parse(IniFile::parse("[gamepad]\na=b\nb=a\nx=y\ny=x\nstart=start\nl=lefttrigger, leftshoulder\n"));
    check(custom.nintendo_layout == NintendoLayout::by_position, "custom faces: by position");
    check(custom.pad[std::size_t(PadKey::a)][0] == PadInput{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST}, "custom A kept");
    check(custom.pad[std::size_t(PadKey::l)][1] == PadInput{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, "L's second binding kept");
    // A Switch pad with the custom file reads as before (by position).
    check(gamepad_input(custom, binding_state(pressed({SDL_GAMEPAD_BUTTON_EAST}), PadFamily::switch_pro, custom.nintendo_layout)).buttons ==
              pad_button::a,
          "custom file on a Switch pad: A where it was");
    // Explicit values win and survive a store.
    auto s = Settings::parse(IniFile::parse(
        "[gamepad]\na=b\nnintendo_layout=label\nprompts=dualsense\ngamecube_layout=bindings\nwii_driver=0\nmenu_buttons=bindings\ngc_adapter=false\n"));
    check(s.nintendo_layout == NintendoLayout::by_label && s.prompts == "dualsense" && s.gamecube_layout == GameCubeLayout::bindings &&
              s.wii_driver == WiiDriver::off && s.menu_buttons == MenuButtons::bindings && !s.gc_adapter,
          "explicit settings");
    s.store();
    const auto again = Settings::parse(s.ini());
    check(again.nintendo_layout == NintendoLayout::by_label && again.prompts == "dualsense" && again.gamecube_layout == GameCubeLayout::bindings &&
              again.wii_driver == WiiDriver::off && again.menu_buttons == MenuButtons::bindings && !again.gc_adapter &&
              again.pad[std::size_t(PadKey::a)][0] == PadInput{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST},
          "settings survive a store");
    auto custom_stored = custom;
    custom_stored.store();
    check(Settings::parse(custom_stored.ini()).nintendo_layout == NintendoLayout::by_position, "the migrated layout is written");
    check(Settings().prompts == "auto" && Settings().wii_driver == WiiDriver::automatic && Settings().gamecube_layout == GameCubeLayout::original &&
              Settings().menu_buttons == MenuButtons::layout && Settings().gc_adapter,
          "defaults");
    // wii_driver from an earlier build (true / false) and its own words.
    for (const auto& [text, value] : {std::pair<const char*, WiiDriver>{"true", WiiDriver::on}, {"1", WiiDriver::on}, {"on", WiiDriver::on},
                                      {"false", WiiDriver::off}, {"off", WiiDriver::off}, {"auto", WiiDriver::automatic}, {"", WiiDriver::automatic}})
        check(Settings::parse(IniFile::parse(std::string("[gamepad]\nwii_driver=") + text + "\n")).wii_driver == value,
              std::string("wii_driver=") + text);
    auto automatic = Settings::parse(IniFile::parse("[gamepad]\nwii_driver=1\n"));
    automatic.wii_driver = WiiDriver::automatic;
    automatic.store();
    check(Settings::parse(automatic.ini()).wii_driver == WiiDriver::automatic, "wii_driver=auto is written");
}

void write_bmp(const std::string& path, const ArtImage& image) {
    std::ofstream out(path, std::ios::binary);
    const std::uint32_t row = std::uint32_t(image.width) * 4, size = 54 + row * std::uint32_t(image.height);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("BM", 2);
    u32(size), u32(0), u32(54), u32(40), u32(std::uint32_t(image.width)), u32(std::uint32_t(image.height)), u16(1), u16(32), u32(0),
        u32(size - 54), u32(2835), u32(2835), u32(0), u32(0);
    for (int y = image.height - 1; y >= 0; --y)
        for (int x = 0; x < image.width; ++x) {
            const auto* p = &image.rgba[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4];
            const char bgra[4] = {char(p[2]), char(p[1]), char(p[0]), char(p[3])};
            out.write(bgra, 4);
        }
}

std::size_t ink(const ArtImage& image, const PromptAtlas::Cell& cell) {
    std::size_t count = 0;
    for (int y = cell.y; y < cell.y + cell.height; ++y)
        for (int x = cell.x; x < cell.x + cell.width; ++x) count += image.rgba[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4 + 3] > 128;
    return count;
}

// The box of a cell's opaque pixels (x0, y0, x1, y1; empty: x1 < x0).
std::array<int, 4> ink_box(const ArtImage& image, const PromptAtlas::Cell& cell) {
    std::array<int, 4> box{cell.x + cell.width, cell.y + cell.height, cell.x - 1, cell.y - 1};
    for (int y = cell.y; y < cell.y + cell.height; ++y)
        for (int x = cell.x; x < cell.x + cell.width; ++x)
            if (image.rgba[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4 + 3] > 128)
                box = {std::min(box[0], x), std::min(box[1], y), std::max(box[2], x), std::max(box[3], y)};
    return box;
}

// The mean lightness (0..1) of a cell's opaque pixels: what a picture shows
// on the game's black text boxes.
float lightness(const ArtImage& image, const PromptAtlas::Cell& cell) {
    double sum = 0, weight = 0;
    for (int y = cell.y; y < cell.y + cell.height; ++y)
        for (int x = cell.x; x < cell.x + cell.width; ++x) {
            const auto* p = &image.rgba[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4];
            if (p[3] < 200) continue;
            sum += (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
            weight += 1;
        }
    return weight > 0 ? float(sum / weight) : 0.0f;
}

// The share of the middle of a cell (its inner half, inside a key cap's
// outline) that is dark ink: the letters and signs on a light key cap.
float dark_share(const ArtImage& image, const PromptAtlas::Cell& cell) {
    std::size_t dark = 0, all = 0;
    for (int y = cell.y + cell.height / 4; y < cell.y + cell.height * 3 / 4; ++y)
        for (int x = cell.x + cell.width / 4; x < cell.x + cell.width * 3 / 4; ++x) {
            const auto* p = &image.rgba[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4];
            dark += p[3] > 200 && p[0] < 80 && p[1] < 80 && p[2] < 80;
            ++all;
        }
    return float(dark) / float(all);
}

void pictures() {
    const char* dir = std::getenv("DW4_PADS_ART_DIR");
    const Settings settings;
    for (std::size_t f = 0; f < kPadFamilyCount; ++f) {
        const auto family = PadFamily(f);
        const auto atlas = build_prompt_atlas(family, settings);
        check(atlas.image.width > 0 && atlas.image.rgba.size() == std::size_t(atlas.image.width) * std::size_t(atlas.image.height) * 4,
              "atlas size");
        // Every control a prompt can name has a picture, in its cell's
        // picture rows (the original glyphs' rows 5..25 of 27); the GameCube
        // pad's menu button is the keyboard's F1.
        for (std::size_t c = 0; c < kPromptControlCount; ++c) {
            const auto icon = atlas.icon(PromptControl(c));
            check(ink(atlas.image, icon) > std::size_t(icon.width * icon.height / 12),
                  std::string(family_id(family)) + ": a picture for " + prompt_control_token(PromptControl(c)));
            const auto& cell = atlas.cells[c];
            check(ink(atlas.image, cell) == ink(atlas.image, icon), std::string(family_id(family)) + ": nothing outside the picture rows");
        }
        check(ink(atlas.image, atlas.start_word) > 300, std::string(family_id(family)) + ": the word START's picture");
        // Same input, same pixels.
        check(build_prompt_atlas(family, settings).image.rgba == atlas.image.rgba, "the atlas is deterministic");
        if (dir) write_bmp(std::string(dir) + "/atlas_" + family_id(family) + ".bmp", atlas.image);
    }
    // A rebinding redraws: A on EAST is a circle on PlayStation.
    Settings rebound;
    rebound.pad[std::size_t(PadKey::a)] = {{{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST}}};
    check(build_prompt_atlas(PadFamily::ps4, rebound).image.rgba != build_prompt_atlas(PadFamily::ps4, settings).image.rgba, "rebinding redraws");

    // Readable on the game's black text boxes: the sticks and the D-pad are
    // light on every pad family (a dark grey disc vanished there).
    for (std::size_t f = 0; f < kPadFamilyCount; ++f) {
        const auto family = PadFamily(f);
        if (family == PadFamily::keyboard) continue;
        const auto atlas = build_prompt_atlas(family, settings);
        for (const auto control : {PromptControl::stick, PromptControl::c_stick, PromptControl::dpad})
            check(lightness(atlas.image, atlas.icon(control)) > 0.5f,
                  std::string(family_id(family)) + ": a light " + prompt_control_token(control) + " picture (" +
                      std::to_string(lightness(atlas.image, atlas.icon(control))) + ")");
        // START (and the menu button) as large as the face buttons: no small
        // dot beside A and B, in its glyph cell and in the word's place.
        const auto a = ink_box(atlas.image, atlas.icon(PromptControl::a));
        const auto start = ink_box(atlas.image, atlas.icon(PromptControl::start));
        const auto word = ink_box(atlas.image, atlas.start_word);
        const int a_height = a[3] - a[1];
        check(start[3] - start[1] >= a_height * 0.62f, std::string(family_id(family)) + ": START's picture as tall as A's");
        if (family != PadFamily::ps4 && family != PadFamily::ps5 && family != PadFamily::gamecube)
            check(std::abs((start[3] - start[1]) - a_height) <= 4 && std::abs((word[3] - word[1]) - a_height) <= 4,
                  std::string(family_id(family)) + ": a round START as large as A (" + std::to_string(start[3] - start[1]) + " / " +
                      std::to_string(word[3] - word[1]) + " vs " + std::to_string(a_height) + ")");
        // The word's picture: its ink and two pixels on each side.
        check(atlas.start_word_ink.x >= atlas.start_word.x && atlas.start_word_ink.x + atlas.start_word_ink.width <= atlas.start_word.x + atlas.start_word.width &&
                  word[0] - atlas.start_word_ink.x <= 3 && atlas.start_word_ink.x + atlas.start_word_ink.width - 1 - word[2] <= 3,
              std::string(family_id(family)) + ": the word's picture trimmed to its ink");
        check(atlas.word_aspect() > 0.5f && atlas.word_aspect() < 2.9f, std::string(family_id(family)) + ": the word's picture's width");
    }
    // The PlayStation START: the DualShock 4's pill carries the word OPTIONS
    // (as printed by it), the DualSense's the three lines, in the glyph
    // cell, the word's place and the title alike (one drawing).
    {
        const auto ds4 = build_prompt_atlas(PadFamily::ps4, settings), ds5 = build_prompt_atlas(PadFamily::ps5, settings);
        check(ds4.word_aspect() > ds5.word_aspect() * 1.3f, "DualShock 4: the OPTIONS pill is wider than the DualSense's");
        ArtImage word4(376, 96), word5(376, 96);
        draw_prompt(word4, 0, 0, 376, 96, PadFamily::ps4, prompt_face(PadFamily::ps4, settings, PromptControl::start), true);
        draw_prompt(word5, 0, 0, 376, 96, PadFamily::ps5, prompt_face(PadFamily::ps5, settings, PromptControl::start), true);
        check(word4.rgba != word5.rgba, "DualShock 4 and DualSense START differ");
        check(prompt_face(PadFamily::ps4, settings, PromptControl::menu).face == Face::share, "DualShock 4 menu: Share");
    }
    // Keyboard: the stick, D-pad and C-stick are one key cap with the keys'
    // labels (four small caps were dots in a text line); Backspace is the
    // erase sign, not an arrow like the Left key's.
    {
        const auto kb = build_prompt_atlas(PadFamily::keyboard, settings);
        for (const auto control : {PromptControl::stick, PromptControl::dpad, PromptControl::c_stick}) {
            const auto box = ink_box(kb.image, kb.icon(control));
            check(box[2] - box[0] >= PromptAtlas::kCellWidth * 0.85f && box[3] - box[1] >= PromptAtlas::kIconSize * 0.8f,
                  std::string("keyboard ") + prompt_control_token(control) + ": one cap over the whole picture");
            check(dark_share(kb.image, kb.icon(control)) > 0.05f, std::string("keyboard ") + prompt_control_token(control) + ": large labels");
        }
        Settings keys;
        keys.keys[std::size_t(PadKey::b)] = {SDL_SCANCODE_BACKSPACE};
        keys.keys[std::size_t(PadKey::x)] = {SDL_SCANCODE_LEFT};
        const auto signs = build_prompt_atlas(PadFamily::keyboard, keys);
        const float erase = dark_share(signs.image, signs.icon(PromptControl::b)), left = dark_share(signs.image, signs.icon(PromptControl::x));
        check(erase > left * 2.0f, "keyboard: Backspace's sign is no arrow (" + std::to_string(erase) + " vs " + std::to_string(left) + ")");
    }
    // Only the families on screen are drawn, once per binding change.
    {
        PromptAtlasCache cache;
        Settings live;
        cache.set_settings(live);
        cache.atlas(PadFamily::ps5);
        cache.atlas(PadFamily::ps5);
        check(cache.draws() == 1 && cache.current(PadFamily::ps5) && !cache.current(PadFamily::xbox_one), "atlas cache: one family drawn once");
        const auto generation = cache.generation(PadFamily::ps5);
        live.pad[std::size_t(PadKey::a)] = {{{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST}}};
        cache.set_settings(live);
        check(!cache.current(PadFamily::ps5), "atlas cache: a rebinding marks it stale");
        const auto& redrawn = cache.atlas(PadFamily::ps5);
        check(cache.draws() == 2 && cache.generation(PadFamily::ps5) != generation, "atlas cache: drawn again when asked for");
        check(redrawn.image.rgba == build_prompt_atlas(PadFamily::ps5, live).image.rgba, "atlas cache: the rebinding's pictures");
        cache.set_settings(live);
        cache.atlas(PadFamily::ps5);
        check(cache.draws() == 2, "atlas cache: the same bindings draw nothing");
        live.menu_buttons = MenuButtons::bindings;
        cache.set_settings(live);
        check(!cache.current(PadFamily::ps5), "atlas cache: the menu buttons' setting is part of the pictures");
    }

    // The title's PRESS START: only the word changes, the other rows (Game
    // Start / Option, read by TitleEntry, the copyright) stay the original's.
    // A stand-in with the original's layout: ink rows above, PRESS and
    // START apart in the stored rows 99..124.
    const auto title_like = [](int scale) {
        ArtImage title(256 * scale, 128 * scale);
        for (int y = 0; y < title.height; ++y)
            for (int x = 0; x < title.width; ++x) {
                auto* p = &title.rgba[(std::size_t(y) * std::size_t(title.width) + std::size_t(x)) * 4];
                const int ox = x / scale, oy = y / scale;
                const bool rows_above = oy >= 60 && oy < 90 && ox >= 20 && ox < 230;
                const bool press = oy >= 99 && oy < 125 && ox >= 12 && ox < 78, start = oy >= 99 && oy < 125 && ox >= 88 && ox < 166;
                p[0] = p[1] = p[2] = 255;
                p[3] = std::uint8_t((rows_above || press || start) && ((ox / 3 + oy / 2) % 3 != 0) ? 255 : 0);
            }
        return title;
    };
    const ArtImage title = title_like(1);
    ArtImage variant;
    check(title_press_start(title, 2048, true, PadFamily::xbox_one, settings, variant), "title variant made");
    check(variant.width == 2048 && variant.height == 1024, "title variant 8x (sharp up to 4K)");
    bool rows_kept = true, word_changed = false;
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 2048; ++x) {
            const auto* o = &title.rgba[(std::size_t(y / 8) * 256 + std::size_t(x / 8)) * 4];
            const auto* v = &variant.rgba[(std::size_t(y) * 2048 + std::size_t(x)) * 4];
            const bool in_word = x >= 84 * 8 && x < 171 * 8 && y >= 96 * 8;
            if (!in_word && std::memcmp(o, v, 4) != 0) rows_kept = false;
            if (in_word && o[3] != v[3]) word_changed = true;
        }
    check(rows_kept, "title: the rows outside START are the original's");
    check(word_changed, "title: START is drawn anew");
    check(!title_press_start(ArtImage(128, 128), 2048, true, PadFamily::ps5, settings, variant), "title: another layout is left alone");
    if (dir) write_bmp(std::string(dir) + "/title_xbox_one.bmp", variant);
    // A texture pack's replacement in the same layout (4x): its START is
    // replaced too, at 2048 texels; one whose ink runs across the word's
    // borders (moved or restyled) stays as it is.
    ArtImage pack = title_like(4);
    check(title_press_start(pack, 2048, false, PadFamily::ps5, settings, variant) && variant.width == 2048, "title: a replacement's START");
    check(variant.rgba[(std::size_t(563) * 2048 + 803) * 4 + 3] == pack.rgba[(std::size_t(281) * 1024 + 401) * 4 + 3] &&
              variant.rgba[(std::size_t(563) * 2048 + 803) * 4 + 3] != 0,
          "title: a replacement's other rows kept");
    for (int y = 99 * 4; y < 125 * 4; ++y)
        for (int x = 80 * 4; x < 90 * 4; ++x) pack.rgba[(std::size_t(y) * 1024 + std::size_t(x)) * 4 + 3] = 255;
    check(!title_press_start(pack, 2048, false, PadFamily::ps5, settings, variant), "title: a replacement with another layout is left alone");
}

// ---- The prompts in the game's text lines (prompt_layout.hpp), laid out
// by the engine's own layout_text with a stand-in font: every character a
// cell of its own (full width START letters share the ASCII cells, as in
// bectext.igb), the space with a quad.
engine::FontImage test_font() {
    engine::FontImage font;
    font.name = "NORMAL";
    font.line_height = 0.28f;
    auto add = [&](std::uint32_t code, std::uint32_t cell, float width) {
        engine::Glyph g;
        g.advance = width + 0.02f;
        g.width = width;
        g.height = 0.27f;
        g.u0 = float(cell % 32) / 32.0f, g.u1 = g.u0 + 1.0f / 32.0f;
        g.v0 = float(cell / 32) / 32.0f, g.v1 = g.v0 + 1.0f / 32.0f;
        font.glyphs[code] = g;
    };
    for (std::uint32_t c = 0x21; c < 0x7f; ++c) add(c, c, 0.12f);
    add(0x20, 0x20, 0.05f);
    for (std::uint32_t c = 0; c < 26; ++c) add(0xFF21 + c, 'A' + c, 0.12f); // full width capitals: the ASCII cells
    // The button glyphs.
    std::uint32_t cell = 0x80;
    for (const std::uint32_t code : {0x226Au, 0x226Bu, 0x2220u, 0x2252u, 0x2312u, 0x22A5u, 0x221Au, 0x4E9Cu, 0x2235u, 0x2261u, 0x2207u, 0x5A03u})
        add(code, cell++, 0.2f);
    return font;
}

struct Laid {
    engine::GeometryData geometry;
    PromptLayout layout;
};
Laid lay(const engine::FontImage& font, std::u16string_view text, float aspect, int align = 0, float max_width = 6.0f) {
    Laid out;
    out.geometry = engine::layout_text(font, text, align, max_width);
    out.layout = layout_prompts(out.geometry, prompt_glyphs(font), aspect, align);
    return out;
}
float quad_x0(const engine::GeometryData& g, std::size_t quad) {
    return std::min({g.positions[quad * 4].x, g.positions[quad * 4 + 1].x, g.positions[quad * 4 + 2].x, g.positions[quad * 4 + 3].x});
}
float shift_of(const PromptLayout& layout, std::size_t quad) {
    for (const auto& t : layout.text)
        if (t.quad == quad) return t.shift;
    throw std::runtime_error("quad " + std::to_string(quad) + " is not text");
}

void text_lines() {
    const auto font = test_font();
    const auto glyphs = prompt_glyphs(font);
    check(glyphs.buttons.size() == 12 && glyphs.word_chars.size() >= 62, "the font's button glyphs and letters");
    constexpr float h = 0.27f, aspect = 0.7f;
    // "Press START when finished.": the word becomes one picture the
    // family's width at the word's start; the rest of the row closes up
    // (left aligned: the start stays).
    {
        const auto t = lay(font, u"Press START when finished.", aspect);
        check(t.layout.words == 1 && t.layout.pictures.size() == 1 && t.layout.pictures[0].control == -1, "START: one word picture");
        const auto& p = t.layout.pictures[0];
        const float word = (quad_x0(t.geometry, 10) + 0.12f) - quad_x0(t.geometry, 6); // S .. T's right edge
        check(std::fabs(p.x0 - quad_x0(t.geometry, 6)) < 1e-4f && std::fabs((p.x1 - p.x0) - h * aspect) < 1e-4f, "START: picture width");
        const float delta = word - h * aspect;
        check(std::fabs(shift_of(t.layout, 0)) < 1e-6f && std::fabs(shift_of(t.layout, 11) + delta) < 1e-4f &&
                  std::fabs(shift_of(t.layout, 25) + delta) < 1e-4f,
              "START: the row closes up after the word, the start stays");
        check(t.layout.text.size() == t.geometry.positions.size() / 4 - 5, "START: every other quad stays text");
    }
    // Centred and right-aligned lines keep their middle and their end.
    {
        const auto left = lay(font, u"Press START", aspect, 0, 4.0f);
        const auto centre = lay(font, u"Press START", aspect, 2, 4.0f);
        const auto right = lay(font, u"Press START", aspect, 1, 4.0f);
        const float delta = (quad_x0(left.geometry, 10) + 0.12f - quad_x0(left.geometry, 6)) - h * aspect;
        check(std::fabs(shift_of(centre.layout, 0) - delta * 0.5f) < 1e-4f, "centred: the start moves half way");
        check(std::fabs(shift_of(right.layout, 0) - delta) < 1e-4f, "right aligned: the start moves all the way");
        check(std::fabs(right.layout.pictures[0].x1 - (quad_x0(right.geometry, 10) + 0.12f)) < 1e-4f, "right aligned: the end stays");
    }
    // A wider picture (the DualShock 4's OPTIONS pill) makes room.
    {
        const auto t = lay(font, u"Press START now", 3.2f);
        check(t.layout.words == 1 && shift_of(t.layout, 12) > 0, "a wide picture: the rest moves right");
    }
    // Other rows of the line stay where they are.
    {
        const auto t = lay(font, u"Press START\nwhen finished.", aspect);
        check(t.layout.words == 1 && std::fabs(shift_of(t.layout, 11)) < 1e-6f && std::fabs(shift_of(t.layout, 0)) < 1e-6f, "another row stays");
    }
    // Words that only contain START stay text; punctuation does not count.
    for (const auto* text : {u"STARTS here", u"Press STARTED", u"RESTART now", u"xSTART", u"START1"}) {
        const auto t = lay(font, text, aspect);
        check(t.layout.words == 0 && t.layout.pictures.empty(), "a longer word stays text");
    }
    for (const auto* text : {u"Press START.", u"(START)", u"START, then A"}) check(lay(font, text, aspect).layout.words == 1, "punctuation beside START");
    check(lay(font, u"Press ＳＴＡＲＴ when finished.", aspect).layout.words == 1, "full width START");
    // START next to the START glyph is the word that names it.
    check(lay(font, u"START ⊥ button", aspect).layout.words == 0, "START beside its glyph stays a word");
    // No word pictures without a width (the GameCube's own pictures).
    check(lay(font, u"Press START", 0.0f).layout.words == 0, "no word picture without a width");
    // The button glyphs: a picture over each glyph's place, the rest text.
    {
        const auto t = lay(font, u"≡∇Select  ≪Confirm  ≫Cancel", aspect);
        check(t.layout.pictures.size() == 4 && t.layout.words == 0, "button glyphs become pictures");
        check(t.layout.pictures[0].control == int(PromptControl::dpad) && t.layout.pictures[1].control == int(PromptControl::stick) &&
                  t.layout.pictures[2].control == int(PromptControl::a) && t.layout.pictures[3].control == int(PromptControl::b),
              "button glyphs: their controls");
        check(std::fabs(t.layout.pictures[2].x0 - quad_x0(t.geometry, 10)) < 1e-4f, "button glyphs keep their places");
    }
    // Not a text line: nothing.
    engine::GeometryData odd;
    odd.positions.resize(3);
    check(layout_prompts(odd, glyphs, aspect, 0).pictures.empty(), "not a text line");
}

// The device list's ids (gc_adapter.hpp): USB, HID and Bluetooth spellings.
void device_ids() {
    const auto ids = [](const wchar_t* text) { return hardware_id_usb_ids(text); };
    check(ids(L"USB\\VID_057E&PID_0337&REV_0100") && ids(L"USB\\VID_057E&PID_0337&REV_0100")->product == 0x0337, "USB id");
    check(ids(L"HID\\VID_057e&PID_2069&MI_00")->vendor == 0x057e && ids(L"HID\\VID_057e&PID_2069&MI_00")->product == 0x2069, "HID id");
    const auto bt = ids(L"HID\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002057e_PID&0330");
    check(bt && bt->vendor == 0x057e && bt->product == 0x0330, "Bluetooth id");
    check(!ids(L"ACPI\\PNP0A08") && !ids(L"USB\\VID_12&PID_34"), "no ids");
    std::vector<DeviceRecord> devices{{{L"USB\\VID_046D&PID_C52B"}, false}};
    auto found = usb_findings(devices);
    check(!found.adapter && !found.switch2_pro, "nothing of ours");
    devices.push_back({{L"USB\\VID_057E&PID_0337&REV_0100", L"USB\\VID_057E&PID_0337"}, false});
    found = usb_findings(devices);
    check(found.adapter && !found.adapter_winusb, "an adapter without WinUSB");
    devices.back().interface_guid = true;
    devices.push_back({{L"USB\\VID_057E&PID_2069&MI_00"}, false});
    found = usb_findings(devices);
    check(found.adapter && found.adapter_winusb && found.switch2_pro, "an adapter with WinUSB and a Switch 2 Pro");
}

} // namespace

int main() {
    try {
        classification();
        binding_spaces();
        pictures_follow_bindings();
        tracker();
        migration();
        pictures();
        text_lines();
        device_ids();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " pad family checks passed\n";
    return 0;
}
