// Controller families (src/game/pad_family, button_art; docs/ROUND_PADS_NOTES.md)
// without a device or window: the classification by SDL type, USB ids and
// name, the binding spaces (Nintendo pads by label or position, a GameCube
// pad 1:1), which picture a binding shows on each family, the prompt
// tracker (last device with real input on player 1's slot), the settings
// migration, and the pictures themselves (every family's atlas, the title's
// PRESS START variant). DW4_PADS_ART_DIR=<dir> writes the atlases as BMPs.
#include "../src/game/button_art.hpp"
#include "../src/game/controls.hpp"
#include "../src/game/pad_family.hpp"
#include "../src/game/settings.hpp"

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
    // SDL's Wii driver: type STANDARD, its own name.
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "Nintendo Wii U Pro Controller")) == PadFamily::wiiu_pro, "Wii U Pro by name");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0330, "")) == PadFamily::wiiu_pro, "Wii U Pro by id");
    check(classify(id(SDL_GAMEPAD_TYPE_STANDARD, 0x057e, 0x0306, "Nintendo Wii Remote")) == PadFamily::generic, "a Wii remote is no Wii U Pro");
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
    // The port's menu actions follow A / B / X.
    check(face(PadFamily::ps5, PromptControl::confirm, rebound) == Face::circle, "confirm follows A");
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
    auto s = Settings::parse(IniFile::parse("[gamepad]\na=b\nnintendo_layout=label\nprompts=dualsense\ngamecube_layout=bindings\nwii_driver=0\n"));
    check(s.nintendo_layout == NintendoLayout::by_label && s.prompts == "dualsense" && s.gamecube_layout == GameCubeLayout::bindings &&
              !s.wii_driver,
          "explicit settings");
    s.store();
    const auto again = Settings::parse(s.ini());
    check(again.nintendo_layout == NintendoLayout::by_label && again.prompts == "dualsense" && again.gamecube_layout == GameCubeLayout::bindings &&
              !again.wii_driver && again.pad[std::size_t(PadKey::a)][0] == PadInput{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_EAST},
          "settings survive a store");
    auto custom_stored = custom;
    custom_stored.store();
    check(Settings::parse(custom_stored.ini()).nintendo_layout == NintendoLayout::by_position, "the migrated layout is written");
    check(Settings().prompts == "auto" && Settings().wii_driver && Settings().gamecube_layout == GameCubeLayout::original, "defaults");
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
    // The title's PRESS START: only the word changes, the other rows (Game
    // Start / Option, read by TitleEntry, the copyright) stay the original's.
    ArtImage title(256, 128);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 256; ++x) {
            auto* p = &title.rgba[(std::size_t(y) * 256 + std::size_t(x)) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = std::uint8_t(((x / 3 + y / 2) % 3 == 0) ? 255 : 0);
        }
    ArtImage variant;
    check(title_press_start(title, 4, true, PadFamily::xbox_one, settings, variant), "title variant made");
    check(variant.width == 1024 && variant.height == 512, "title variant 4x");
    bool rows_kept = true, word_changed = false;
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 1024; ++x) {
            const auto* o = &title.rgba[(std::size_t(y / 4) * 256 + std::size_t(x / 4)) * 4];
            const auto* v = &variant.rgba[(std::size_t(y) * 1024 + std::size_t(x)) * 4];
            const bool in_word = x >= 84 * 4 && x < 171 * 4 && y >= 96 * 4;
            if (!in_word && std::memcmp(o, v, 4) != 0) rows_kept = false;
            if (in_word && o[3] != v[3]) word_changed = true;
        }
    check(rows_kept, "title: the rows outside START are the original's");
    check(word_changed, "title: START is drawn anew");
    check(!title_press_start(ArtImage(128, 128), 4, true, PadFamily::ps5, settings, variant), "title: another layout is left alone");
    if (dir) write_bmp(std::string(dir) + "/title_xbox_one.bmp", variant);
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
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " pad family checks passed\n";
    return 0;
}
