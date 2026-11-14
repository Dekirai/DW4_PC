#pragma once

// Controller families (docs/ROUND_PADS_NOTES.md): which kind of controller a
// connected pad is, how its buttons reach the [gamepad] bindings, which
// physical button a binding names on it, and which device's pictures the
// prompts show (the last device with real input on player 1's slot).
//
// Presentation and host input only: nothing here reaches the engine, the
// online wire or a save. The game's logical pad (dw4::HostPadInput) is
// still made by gamepad_input from the bindings; this only decides, before
// that, which SDL button a binding name stands for on Nintendo and
// GameCube pads (a "binding space", below), and afterwards, what the
// prompts draw for it.
#include "controls.hpp"
#include "settings.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_joystick.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

// The families the prompts and the binding spaces know. `generic` is any
// other gamepad SDL knows (by SDL's own labels); `keyboard` is player 1's
// keyboard.
enum class PadFamily : std::uint8_t {
    keyboard,
    xbox360,
    xbox_one,    // Xbox One and Xbox Series
    ps4,         // DualShock 4
    ps5,         // DualSense (and DualSense Edge)
    switch_pro,  // Switch Pro Controller (and Joy-Con)
    switch2_pro, // Switch 2 Pro Controller
    wiiu_pro,    // Wii U Pro Controller
    gamecube,    // an original GameCube controller on an adapter
    thor,        // the AYN Thor's built-in controller ("Odin Controller", USB 2020:0111)
    generic,
    count
};
inline constexpr std::size_t kPadFamilyCount = std::size_t(PadFamily::count);

const char* family_name(PadFamily family); // "DualSense", "Xbox One / Series", ...
const char* family_id(PadFamily family);   // settings.ini: "dualsense", "xbox_one", ...
std::optional<PadFamily> family_from_id(std::string_view id);
// The pads whose face buttons are printed in the Nintendo layout (A right,
// B bottom, X top, Y left): Switch Pro, Switch 2 Pro, Wii U Pro and the AYN
// Thor; nintendo_layout reads them by label or by position.
bool nintendo_family(PadFamily family);

// ---- The AYN Thor
// Its built-in controller is a USB HID device "Odin Controller" 2020:0111
// (also the AYN Odin's; key layout Vendor_2020_Product_0111.kl): face
// buttons printed in the Nintendo layout, shoulders L1 / R1 and triggers
// L2 / R2, Start and Select above the lower screen (docs/ANDROID.md, The
// Thor's controller). Which key codes the face buttons send is the
// device's "Controller style" (settings.hpp ThorStyle): Standard sends the
// printed letters (SDL's SOUTH is the printed A, on the right), Xbox sends
// positions as SDL reads a Switch Pro Controller (SOUTH is the bottom
// button, printed B). The binding space reads the Thor like any Nintendo
// pad either way (by label unless nintendo_layout=position).
inline constexpr std::uint16_t kVendorAyn = 0x2020, kProductOdinController = 0x0111;
// The style the Thor's pads are read with now (standard or xbox; automatic
// counts as standard): the host sets it from settings.ini thor_style or
// the device's setting (thor_style_from_property). Standard until set.
void set_thor_codes(ThorStyle style);
ThorStyle thor_codes();
// The device's setting: Android's persist.sys.gamepad.type, "0" Standard
// (the Thor's quick settings tile said "Standard" with 0, 14.11.2026), "1"
// Xbox; anything else nullopt.
std::optional<ThorStyle> thor_style_from_property(std::string_view value);
// The Thor's L2 / R2 keys (its "L2/R2 mode" Digital or Both sends them,
// Analog or Both the GAS / BRAKE axes): SDL's Android backend keeps the keys
// as joystick buttons outside its gamepad mapping (kThorL2Button,
// kThorR2Button), so a key alone would do nothing; folded into the trigger
// axes, a pull counts once in every mode.
inline constexpr int kThorL2Button = 15, kThorR2Button = 16; // SDL_sysjoystick.c keycode_to_SDL: BUTTON_L2 -> MISC1 (15), BUTTON_R2 -> 16
GamepadState with_digital_triggers(GamepadState state, bool l2_key, bool r2_key);

// What SDL tells about a pad (a snapshot, so classification is testable
// without a device).
struct PadIdentity {
    SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN;
    std::uint16_t vendor = 0, product = 0;
    std::string name;
    // SDL's Wii driver (a HIDAPI GUID of 057e:0306 / 0330): the extension
    // type its GUID carries (kWiiExtensionWiiUPro the Wii U Pro Controller,
    // 128 none, 129 a Nunchuk, 130 a Classic Controller); -1 for other pads.
    int wii_extension = -1;
};
inline constexpr int kWiiExtensionWiiUPro = 131; // SDL_hidapi_nintendo.h k_eWiiExtensionControllerType_WiiUPro
PadIdentity pad_identity(SDL_Gamepad* pad);
PadFamily classify(const PadIdentity& identity);
// Whether a pad takes a player slot: everything SDL opens as a gamepad but
// the Wii remotes SDL's Wii driver makes gamepads of (a DolphinBar's slots,
// a Wii Remote Plus, also with a Nunchuk or Classic Controller): 057e:0306
// and 057e:0330 only when SDL names it the Wii U Pro Controller.
bool player_pad(const PadIdentity& identity);
// Whether SDL's Wii driver runs (settings.ini wii_driver; automatic: while
// a 057e:0330 HID device is connected).
bool wii_driver_wanted(WiiDriver setting, bool wii_u_ids_present);
// The player a text line's pictures belong to (0..3): the character
// select's lines (CHRSELFONTnn) are named font<player><line>; every other
// line is player 1's (0).
int line_player(std::string_view layer, std::string_view name);

// USB ids the classification names (SDL's usb_ids.h).
inline constexpr std::uint16_t kVendorNintendo = 0x057e, kVendorSony = 0x054c, kVendorMicrosoft = 0x045e, kVendorDragonRise = 0x0079;
inline constexpr std::uint16_t kProductSwitchPro = 0x2009, kProductSwitch2Pro = 0x2069, kProductWiiUPro = 0x0330, kProductWiiRemote = 0x0306,
                               kProductGameCubeAdapter = 0x0337, kProductSwitch2GameCube = 0x2073;

// ---- Binding spaces
// The [gamepad] bindings name SDL buttons ("a" = SOUTH, "b" = EAST, ...).
// Xbox, PlayStation and other pads are bound by position as SDL reports
// them. Nintendo pads by label (the default): the binding "a" is the button
// printed A (SDL's EAST on these pads), "b" the B button (SOUTH), "x" the X
// button (NORTH), "y" the Y button (WEST); by position they are bound like
// an Xbox pad. A GameCube pad on the "bindings" layout is bound by its own
// labels (SDL gives it GC A on SOUTH, X on EAST, B on WEST: "b" is GC B and
// "x" GC X) with L and R pressed at the trigger's click, so the default
// bindings are 1:1; on the "original" layout (the default) the bindings are
// not used at all and every GameCube button is the game's own (gamecube_input).
// (NintendoLayout and GameCubeLayout are settings: settings.hpp.)

// A pad's state as the bindings see it (SDL's position state in, the
// binding space's state out). `clicks`: the pad reports the GameCube
// triggers' digital clicks (SDL's MISC3 / MISC4); without them a trigger
// counts as pressed past kGameCubeTriggerFallback.
GamepadState binding_state(const GamepadState& state, PadFamily family, NintendoLayout nintendo, bool clicks = true);
// One SDL button of the pad (an event, the F1 capture) in the binding space.
int binding_button(int sdl_button, PadFamily family, NintendoLayout nintendo);
inline constexpr std::int16_t kGameCubeTriggerFallback = 24000;
// The GameCube pad on the original layout: A, B, X, Y, Z, Start, the D-pad,
// L and R at their click, the main stick and the C-stick, 1:1.
HostPadInput gamecube_input(const GamepadState& state, bool clicks = true);

// ---- What a binding is on the device
// The physical controls the prompts draw. Face buttons are named by what
// is printed on them on the family's pad (or their PlayStation symbol).
enum class Face : std::uint8_t {
    none,
    // face buttons
    letter_a, letter_b, letter_x, letter_y, cross, circle, square, triangle,
    // shoulders and triggers
    lb, rb, lt, rt, l1, r1, l2, r2, l, r, zl, zr, z,
    // middle buttons
    start_360, back_360, menu, view, options, share, create, plus, minus, start_gc, home, capture, touchpad, mic, c_button,
    start_arrow, select_square, // the AYN Thor's START (a right-pointing arrow) and SELECT (a square), as printed
    // sticks, their clicks, the D-pad (whole or one direction)
    left_stick, right_stick, left_stick_click, right_stick_click, c_stick, dpad, dpad_up, dpad_down, dpad_left, dpad_right,
    paddle_r1, paddle_l1, paddle_r2, paddle_l2, gl, gr,
    misc,
    // Android's Back key (the AYN Thor's key that opens the F1 menu on
    // Android; docs/CONSOLE_MENU.md): a dark round key with a left-pointing
    // triangle, a stand-in until the Thor's printing is confirmed.
    back_key,
};
const char* face_name(Face face); // "A", "Cross", "LB", "ZR", "Menu", "Left stick", ...

// The face a binding-space input shows on a family's pad.
Face face_of(PadFamily family, NintendoLayout nintendo, const PadInput& input);
// A binding's name as the F1 menu shows it for a family ("Cross", "ZL",
// "LB"; SDL's name where the family has none), "" when unbound.
std::string pad_input_label(PadFamily family, const Settings& settings, const PadInput& input);

// ---- The game's controls a prompt names
// The GameCube buttons of the game's own pictures and texts (the font's
// button glyphs, "START"), and the port's own menu actions.
enum class PromptControl : std::uint8_t {
    a, b, x, y, start, l, r, z, stick, c_stick, dpad,
    menu,    // the F1 menu: View / Back / Share / Minus, F1 on the keyboard
    // The port's menus (settings.ini menu_buttons: the pad's own A / B / X,
    // or the game's A / B / X bindings; Enter / Esc / Backspace on the
    // keyboard): activate, back, the on-screen keyboard's delete.
    confirm,
    back,
    erase,
    option, // the menus' top face button (Y / Triangle: the on-screen keyboard's capitals; Shift on the keyboard)
    count
};
inline constexpr std::size_t kPromptControlCount = std::size_t(PromptControl::count);
const char* prompt_control_token(PromptControl control); // "A", "B", ..., "START", "MENU", "OK", "BACK", "DEL", "OPT"
std::optional<PromptControl> prompt_control_from_token(std::string_view token);

// What a prompt for `control` draws for a pad family: the face of the first
// bound input of that game button (the bindings decide, so rebinding
// changes the picture). A GameCube pad on the original layout shows its own
// buttons.
struct PromptFace {
    Face face = Face::none;
    std::string key;  // keyboard: the bound key's name ("Return", "Keypad 8")
    std::array<std::string, 4> keys; // keyboard sticks and D-pad: up, down, left, right
};
PromptFace prompt_face(PadFamily family, const Settings& settings, PromptControl control);

// ---- Which device the prompts follow
// Player 1 is the keyboard and the first pad, players 2-4 the further pads
// (host_view.cpp read_live_input). The prompts follow player 1's last
// device with real input; until there is one, the first pad or the
// keyboard. Real input: a button pressed, a stick pushed past half way, a
// trigger pulled past kTriggerPressed, a key pressed (not repeated; the
// host's F-keys and modifiers do not count).
class PromptTracker {
public:
    struct Pad {
        SDL_JoystickID id{};
        PadFamily family = PadFamily::generic;
        std::string name;
    };
    void pad_added(SDL_JoystickID id, PadFamily family, std::string name);
    void pad_removed(SDL_JoystickID id);
    // A real input of a pad or the keyboard (the thresholds are the caller's:
    // real_input below).
    void pad_input(SDL_JoystickID id);
    void key_input();
    // The family of a player's slot (0..3): player 1's last device, a
    // further player's own pad; keyboard for player 1 without pads, generic
    // for an empty further slot.
    PadFamily player_family(std::size_t player) const;
    const std::vector<Pad>& pads() const { return pads_; }
    // Player 1's last input came from the keyboard.
    bool keyboard_last() const { return last_ == Last::keyboard; }

private:
    enum class Last : std::uint8_t { none, keyboard, pad };
    std::vector<Pad> pads_; // in the order of HostView::gamepads
    Last last_ = Last::none;
    SDL_JoystickID last_pad_{};
};

// Whether an SDL event is real input for PromptTracker (and of which kind).
enum class InputKind : std::uint8_t { none, keyboard, pad };
InputKind real_input(const SDL_Event& event);

} // namespace dw4::game
