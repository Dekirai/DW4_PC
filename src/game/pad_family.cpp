#include "pad_family.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace dw4::game {
namespace {

struct FamilyInfo {
    const char* name;
    const char* id;
};
constexpr std::array<FamilyInfo, kPadFamilyCount> kFamilies{{
    {"Keyboard", "keyboard"},
    {"Xbox 360", "xbox360"},
    {"Xbox One / Series", "xbox_one"},
    {"DualShock 4", "dualshock4"},
    {"DualSense", "dualsense"},
    {"Switch Pro", "switch_pro"},
    {"Switch 2 Pro", "switch2_pro"},
    {"Wii U Pro", "wiiu_pro"},
    {"GameCube", "gamecube"},
    {"AYN Thor", "thor"},
    {"Other controller", "generic"},
}};

bool contains_nocase(std::string_view text, std::string_view part) {
    if (part.empty() || text.size() < part.size()) return part.empty();
    for (std::size_t i = 0; i + part.size() <= text.size(); ++i) {
        bool same = true;
        for (std::size_t k = 0; k < part.size() && same; ++k)
            same = std::tolower(static_cast<unsigned char>(text[i + k])) == std::tolower(static_cast<unsigned char>(part[k]));
        if (same) return true;
    }
    return false;
}

// GameCube adapters and controllers SDL's SDL_IsJoystickGameCube knows
// (SDL_joystick.c), for pads that reach SDL by another path than its
// GameCube driver (DirectInput's database mappings, virtual pads).
bool gamecube_ids(std::uint16_t vendor, std::uint16_t product) {
    if (vendor == kVendorNintendo) return product == kProductGameCubeAdapter || product == kProductSwitch2GameCube;
    if (vendor == kVendorDragonRise) return product == 0x1843 || product == 0x1844 || product == 0x1846; // Mayflash / EVORETRO PC mode
    if (vendor == 0x20d6) return product == 0xa711; // PowerA wired GameCube-style controller
    return false;
}

void swap_buttons(GamepadState& state, SDL_GamepadButton a, SDL_GamepadButton b) {
    std::swap(state.buttons[std::size_t(a)], state.buttons[std::size_t(b)]);
}

// The GameCube pad's triggers in the binding space: full at the click,
// released otherwise (the game reads only the digital L / R bits, which the
// GameCube sets at the click).
void gamecube_triggers(GamepadState& state, bool clicks) {
    auto trigger = [&](SDL_GamepadAxis axis, SDL_GamepadButton click) {
        auto& value = state.axes[std::size_t(axis)];
        const bool pressed = clicks ? state.buttons[std::size_t(click)] : value > kGameCubeTriggerFallback;
        value = pressed ? std::int16_t(32767) : std::int16_t(0);
        state.buttons[std::size_t(click)] = false;
    };
    trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_BUTTON_MISC3);
    trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_GAMEPAD_BUTTON_MISC4);
}

// The F-keys stay the host's (the F1 menu, save states, fullscreen) and
// modifiers alone are not play.
bool host_key(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) return true;
    if (code >= SDL_SCANCODE_F13 && code <= SDL_SCANCODE_F24) return true;
    switch (code) {
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI:
    case SDL_SCANCODE_PRINTSCREEN: case SDL_SCANCODE_CAPSLOCK: case SDL_SCANCODE_NUMLOCKCLEAR: case SDL_SCANCODE_SCROLLLOCK:
        return true;
    default: return false;
    }
}

constexpr std::array<const char*, kPromptControlCount> kTokens{
    "A", "B", "X", "Y", "START", "L", "R", "Z", "STICK", "CSTICK", "DPAD", "MENU", "OK", "BACK", "DEL", "OPT"};

} // namespace

const char* family_name(PadFamily family) { return family < PadFamily::count ? kFamilies[std::size_t(family)].name : ""; }
const char* family_id(PadFamily family) { return family < PadFamily::count ? kFamilies[std::size_t(family)].id : ""; }

std::optional<PadFamily> family_from_id(std::string_view id) {
    for (std::size_t i = 0; i < kPadFamilyCount; ++i)
        if (id == kFamilies[i].id) return PadFamily(i);
    // (Short spellings a player may type into settings.ini.)
    if (id == "ps5") return PadFamily::ps5;
    if (id == "ps4") return PadFamily::ps4;
    if (id == "xbox" || id == "xboxone" || id == "xbox_series") return PadFamily::xbox_one;
    if (id == "switch" || id == "nintendo") return PadFamily::switch_pro;
    if (id == "switch2") return PadFamily::switch2_pro;
    if (id == "wiiu") return PadFamily::wiiu_pro;
    if (id == "gc") return PadFamily::gamecube;
    if (id == "ayn" || id == "odin") return PadFamily::thor;
    return std::nullopt;
}

bool nintendo_family(PadFamily family) {
    return family == PadFamily::switch_pro || family == PadFamily::switch2_pro || family == PadFamily::wiiu_pro || family == PadFamily::thor;
}

namespace {
ThorStyle g_thor_codes = ThorStyle::standard;

// Whether the binding space swaps a Nintendo-layout pad's face buttons
// (SOUTH with EAST, WEST with NORTH): SDL reads Nintendo pads by position
// (SOUTH is the printed B), the Thor in its Standard style by label (SOUTH
// is the printed A); the binding space reads them by label
// (nintendo_layout=label, the default) or by position.
bool nintendo_swap(PadFamily family, NintendoLayout nintendo) {
    if (!nintendo_family(family)) return false;
    const bool sdl_by_label = family == PadFamily::thor && g_thor_codes != ThorStyle::xbox;
    return (nintendo == NintendoLayout::by_label) != sdl_by_label;
}
} // namespace

void set_thor_codes(ThorStyle style) { g_thor_codes = style == ThorStyle::xbox ? ThorStyle::xbox : ThorStyle::standard; }
ThorStyle thor_codes() { return g_thor_codes; }

std::optional<ThorStyle> thor_style_from_property(std::string_view value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r')) value.remove_suffix(1);
    if (value == "0") return ThorStyle::standard;
    if (value == "1") return ThorStyle::xbox;
    return std::nullopt;
}

GamepadState with_digital_triggers(GamepadState state, bool l2_key, bool r2_key) {
    auto& left = state.axes[std::size_t(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)];
    auto& right = state.axes[std::size_t(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)];
    if (l2_key) left = std::int16_t(32767);
    if (r2_key) right = std::int16_t(32767);
    return state;
}

PadIdentity pad_identity(SDL_Gamepad* pad) {
    PadIdentity identity;
    if (!pad) return identity;
    identity.type = SDL_GetGamepadType(pad);
    identity.vendor = SDL_GetGamepadVendor(pad);
    identity.product = SDL_GetGamepadProduct(pad);
    if (const char* name = SDL_GetGamepadName(pad)) identity.name = name;
    // SDL's Wii driver keeps the extension it read in its GUID's last byte
    // (UpdateDeviceIdentity in SDL_hidapi_wii.c; 'h' marks a HIDAPI GUID).
    if (identity.vendor == kVendorNintendo && (identity.product == kProductWiiRemote || identity.product == kProductWiiUPro))
        if (SDL_Joystick* joystick = SDL_GetGamepadJoystick(pad)) {
            const SDL_GUID guid = SDL_GetJoystickGUID(joystick);
            if (guid.data[14] == 'h') identity.wii_extension = guid.data[15];
        }
    return identity;
}

PadFamily classify(const PadIdentity& id) {
    // The Switch 2 Pro Controller has the Switch Pro's type and name in SDL;
    // only its product id tells them apart.
    if (id.vendor == kVendorNintendo && id.product == kProductSwitch2Pro) return PadFamily::switch2_pro;
    if (contains_nocase(id.name, "Switch 2 Pro")) return PadFamily::switch2_pro;
    // SDL's Wii driver gives the Wii U Pro Controller no type (STANDARD) and
    // Xbox labels; only the extension it reads (its GUID) and the name it
    // sets after that tell it (057e:0330 is also every Wii Remote Plus, with
    // or without a Nunchuk or Classic Controller).
    if (id.wii_extension >= 0) return id.wii_extension == kWiiExtensionWiiUPro ? PadFamily::wiiu_pro : PadFamily::generic;
    if (contains_nocase(id.name, "Wii U Pro")) return PadFamily::wiiu_pro;
    // The AYN Thor's built-in controller (SDL's Android backend: no type).
    if ((id.vendor == kVendorAyn && id.product == kProductOdinController) || contains_nocase(id.name, "Odin Controller"))
        return PadFamily::thor;
    if (id.type == SDL_GAMEPAD_TYPE_GAMECUBE || gamecube_ids(id.vendor, id.product) || contains_nocase(id.name, "GameCube"))
        return PadFamily::gamecube;
    switch (id.type) {
    case SDL_GAMEPAD_TYPE_PS5: return PadFamily::ps5;
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS3: return PadFamily::ps4; // (the DualShock 3 has the same symbols)
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return PadFamily::switch_pro;
    case SDL_GAMEPAD_TYPE_XBOX360: return PadFamily::xbox360;
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadFamily::xbox_one;
    default: break;
    }
    // Typeless pads by their maker.
    if (id.vendor == kVendorSony) return PadFamily::ps4;
    if (id.vendor == kVendorMicrosoft) return PadFamily::xbox_one;
    return PadFamily::generic;
}

bool player_pad(const PadIdentity& id) {
    // SDL's Wii driver opens every Wii remote it finds (a DolphinBar's four
    // slots, a paired remote, a Wii Remote Plus with its extensions) as a
    // gamepad; of them only the Wii U Pro Controller is a player's pad.
    if (id.wii_extension >= 0) return id.wii_extension == kWiiExtensionWiiUPro;
    if (id.vendor == kVendorNintendo && (id.product == kProductWiiRemote || id.product == kProductWiiUPro))
        return contains_nocase(id.name, "Wii U Pro");
    return true;
}

bool wii_driver_wanted(WiiDriver setting, bool wii_u_ids_present) {
    switch (setting) {
    case WiiDriver::on: return true;
    case WiiDriver::off: return false;
    case WiiDriver::automatic: return wii_u_ids_present;
    }
    return false;
}

int line_player(std::string_view layer, std::string_view name) {
    // The character select's lines (CHRSELFONT01 ... 16) are named
    // font<player><line>: font000 player 1's, font103 player 2's ...
    if (layer.rfind("CHRSELFONT", 0) != 0 || name.size() != 7 || name.rfind("font", 0) != 0) return 0;
    for (const char c : name.substr(4))
        if (c < '0' || c > '9') return 0;
    const int player = name[4] - '0';
    return player < 4 ? player : 0;
}

GamepadState binding_state(const GamepadState& state, PadFamily family, NintendoLayout nintendo, bool clicks) {
    GamepadState out = state;
    if (nintendo_swap(family, nintendo)) {
        // (By label: printed A is on SDL's EAST, B on SOUTH, X on NORTH, Y on
        // WEST; the Thor's Standard style the other way round.)
        swap_buttons(out, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST);
        swap_buttons(out, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH);
    } else if (family == PadFamily::gamecube) {
        // SDL: GC A on SOUTH, GC X on EAST, GC B on WEST, GC Y on NORTH.
        swap_buttons(out, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST);
        gamecube_triggers(out, clicks);
    }
    return out;
}

int binding_button(int sdl_button, PadFamily family, NintendoLayout nintendo) {
    auto swap = [](int value, int a, int b) { return value == a ? b : value == b ? a : value; };
    if (nintendo_swap(family, nintendo))
        return swap(swap(sdl_button, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST), SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH);
    if (family == PadFamily::gamecube) return swap(sdl_button, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST);
    return sdl_button;
}

HostPadInput gamecube_input(const GamepadState& raw, bool clicks) {
    const GamepadState state = binding_state(raw, PadFamily::gamecube, NintendoLayout::by_label, clicks);
    static const Settings defaults; // the default bindings are 1:1 in the GameCube binding space
    return gamepad_input(defaults, state);
}

const char* face_name(Face face) {
    switch (face) {
    case Face::none: return "";
    case Face::letter_a: return "A";
    case Face::letter_b: return "B";
    case Face::letter_x: return "X";
    case Face::letter_y: return "Y";
    case Face::cross: return "Cross";
    case Face::circle: return "Circle";
    case Face::square: return "Square";
    case Face::triangle: return "Triangle";
    case Face::lb: return "LB";
    case Face::rb: return "RB";
    case Face::lt: return "LT";
    case Face::rt: return "RT";
    case Face::l1: return "L1";
    case Face::r1: return "R1";
    case Face::l2: return "L2";
    case Face::r2: return "R2";
    case Face::l: return "L";
    case Face::r: return "R";
    case Face::zl: return "ZL";
    case Face::zr: return "ZR";
    case Face::z: return "Z";
    case Face::start_360: return "Start";
    case Face::back_360: return "Back";
    case Face::menu: return "Menu";
    case Face::view: return "View";
    case Face::options: return "Options";
    case Face::share: return "Share";
    case Face::create: return "Create";
    case Face::plus: return "+";
    case Face::minus: return "-";
    case Face::start_gc: return "Start";
    case Face::home: return "Home";
    case Face::capture: return "Capture";
    case Face::touchpad: return "Touchpad";
    case Face::mic: return "Mute";
    case Face::c_button: return "C";
    case Face::start_word: return "Start";
    case Face::select_word: return "Select";
    case Face::left_stick: return "Left stick";
    case Face::right_stick: return "Right stick";
    case Face::left_stick_click: return "Left stick press";
    case Face::right_stick_click: return "Right stick press";
    case Face::c_stick: return "C-stick";
    case Face::dpad: return "D-pad";
    case Face::dpad_up: return "D-pad up";
    case Face::dpad_down: return "D-pad down";
    case Face::dpad_left: return "D-pad left";
    case Face::dpad_right: return "D-pad right";
    case Face::paddle_r1: return "P1";
    case Face::paddle_l1: return "P3";
    case Face::paddle_r2: return "P2";
    case Face::paddle_l2: return "P4";
    case Face::gl: return "GL";
    case Face::gr: return "GR";
    case Face::misc: return "Extra";
    }
    return "";
}

Face face_of(PadFamily family, NintendoLayout nintendo, const PadInput& input) {
    const bool sony = family == PadFamily::ps4 || family == PadFamily::ps5;
    const bool nintendo_pad = nintendo_family(family);
    const bool gc = family == PadFamily::gamecube;
    const bool thor = family == PadFamily::thor;
    if (input.kind == PadInput::Kind::axis) {
        const bool left = input.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        if (input.code != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && input.code != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return Face::none;
        if (sony || thor) return left ? Face::l2 : Face::r2;
        if (nintendo_pad) return left ? Face::zl : Face::zr;
        if (gc) return left ? Face::l : Face::r;
        return left ? Face::lt : Face::rt;
    }
    if (input.kind != PadInput::Kind::button) return Face::none;
    switch (input.code) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        if (sony) return Face::cross;
        if (nintendo_pad && nintendo == NintendoLayout::by_position) return Face::letter_b;
        return Face::letter_a;
    case SDL_GAMEPAD_BUTTON_EAST:
        if (sony) return Face::circle;
        if (nintendo_pad && nintendo == NintendoLayout::by_position) return Face::letter_a;
        return Face::letter_b;
    case SDL_GAMEPAD_BUTTON_WEST:
        if (sony) return Face::square;
        if (nintendo_pad && nintendo == NintendoLayout::by_position) return Face::letter_y;
        return Face::letter_x;
    case SDL_GAMEPAD_BUTTON_NORTH:
        if (sony) return Face::triangle;
        if (nintendo_pad && nintendo == NintendoLayout::by_position) return Face::letter_x;
        return Face::letter_y;
    case SDL_GAMEPAD_BUTTON_BACK:
        if (thor) return Face::select_word;
        if (family == PadFamily::xbox360) return Face::back_360;
        if (family == PadFamily::ps4) return Face::share;
        if (family == PadFamily::ps5) return Face::create;
        if (nintendo_pad) return Face::minus;
        if (gc) return Face::none;
        return Face::view;
    case SDL_GAMEPAD_BUTTON_START:
        if (thor) return Face::start_word;
        if (family == PadFamily::xbox360) return Face::start_360;
        if (sony) return Face::options;
        if (nintendo_pad) return Face::plus;
        if (gc) return Face::start_gc;
        return Face::menu;
    case SDL_GAMEPAD_BUTTON_GUIDE: return Face::home;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return Face::left_stick_click;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return Face::right_stick_click;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        if (sony || thor) return Face::l1;
        if (nintendo_pad) return Face::l;
        if (gc) return Face::none;
        return Face::lb;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        if (sony || thor) return Face::r1;
        if (nintendo_pad) return Face::r;
        if (gc) return Face::z;
        return Face::rb;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return Face::dpad_up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return Face::dpad_down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return Face::dpad_left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Face::dpad_right;
    case SDL_GAMEPAD_BUTTON_MISC1:
        if (family == PadFamily::ps5) return Face::mic;
        if (thor) return Face::misc;
        if (nintendo_pad) return Face::capture;
        if (family == PadFamily::xbox_one) return Face::share;
        return Face::misc;
    case SDL_GAMEPAD_BUTTON_MISC2:
        if (family == PadFamily::switch2_pro) return Face::c_button;
        return Face::misc;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return Face::touchpad;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return family == PadFamily::switch2_pro ? Face::gr : Face::paddle_r1;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return family == PadFamily::switch2_pro ? Face::gl : Face::paddle_l1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return Face::paddle_r2;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return Face::paddle_l2;
    default: return Face::misc;
    }
}

std::string pad_input_label(PadFamily family, const Settings& settings, const PadInput& input) {
    if (input.kind == PadInput::Kind::none) return {};
    const Face face = face_of(family, settings.nintendo_layout, input);
    if (face != Face::none && face != Face::misc) return face_name(face);
    return pad_input_name(input);
}

const char* prompt_control_token(PromptControl control) {
    return control < PromptControl::count ? kTokens[std::size_t(control)] : "";
}

std::optional<PromptControl> prompt_control_from_token(std::string_view token) {
    for (std::size_t i = 0; i < kPromptControlCount; ++i)
        if (token == kTokens[i]) return PromptControl(i);
    return std::nullopt;
}

PromptFace prompt_face(PadFamily family, const Settings& settings, PromptControl control) {
    PromptFace result;
    // The game button a port action stands for (with menu_buttons=bindings
    // host_view.cpp feeds ImGui from these bindings).
    PadKey key = PadKey::a;
    switch (control) {
    case PromptControl::a: case PromptControl::confirm: key = PadKey::a; break;
    case PromptControl::b: case PromptControl::back: key = PadKey::b; break;
    case PromptControl::x: case PromptControl::erase: key = PadKey::x; break;
    case PromptControl::y: case PromptControl::option: key = PadKey::y; break;
    case PromptControl::start: key = PadKey::start; break;
    case PromptControl::l: key = PadKey::l; break;
    case PromptControl::r: key = PadKey::r; break;
    case PromptControl::z: key = PadKey::z; break;
    case PromptControl::stick: key = PadKey::stick_up; break;
    case PromptControl::c_stick: key = PadKey::cstick_up; break;
    case PromptControl::dpad: key = PadKey::dpad_up; break;
    case PromptControl::menu: break;
    case PromptControl::count: break;
    }
    if (family == PadFamily::keyboard) {
        auto first_key = [&](PadKey k) -> std::string {
            for (const auto code : settings.keys[std::size_t(k)])
                if (code != SDL_SCANCODE_UNKNOWN)
                    if (const char* name = SDL_GetScancodeName(code); name && *name) return name;
            return {};
        };
        switch (control) {
        case PromptControl::menu: result.key = "F1"; return result;
        case PromptControl::confirm: result.key = "Return"; return result;
        case PromptControl::back: result.key = "Escape"; return result;
        case PromptControl::erase: result.key = "Backspace"; return result;
        case PromptControl::option: result.key = "Left Shift"; return result;
        case PromptControl::stick:
        case PromptControl::c_stick:
        case PromptControl::dpad: {
            const auto base = std::size_t(key);
            for (std::size_t d = 0; d < 4; ++d) result.keys[d] = first_key(PadKey(base + d));
            result.face = control == PromptControl::dpad ? Face::dpad : control == PromptControl::stick ? Face::left_stick : Face::right_stick;
            return result;
        }
        default: result.key = first_key(key); return result;
        }
    }
    if (family == PadFamily::gamecube && settings.gamecube_layout == GameCubeLayout::original) {
        switch (control) {
        case PromptControl::a: case PromptControl::confirm: result.face = Face::letter_a; break;
        case PromptControl::b: case PromptControl::back: result.face = Face::letter_b; break;
        case PromptControl::x: case PromptControl::erase: result.face = Face::letter_x; break;
        case PromptControl::y: case PromptControl::option: result.face = Face::letter_y; break;
        case PromptControl::start: result.face = Face::start_gc; break;
        case PromptControl::l: result.face = Face::l; break;
        case PromptControl::r: result.face = Face::r; break;
        case PromptControl::z: result.face = Face::z; break;
        case PromptControl::stick: result.face = Face::left_stick; break;
        case PromptControl::c_stick: result.face = Face::c_stick; break;
        case PromptControl::dpad: result.face = Face::dpad; break;
        case PromptControl::menu: result.key = "F1"; break; // (no View button: the keyboard's F1)
        case PromptControl::count: break;
        }
        return result;
    }
    if (settings.menu_buttons == MenuButtons::layout && (control == PromptControl::confirm || control == PromptControl::back ||
                                                         control == PromptControl::erase || control == PromptControl::option)) {
        // The menus' own buttons (menu_keys): the binding space's bottom,
        // right, left and top face buttons whatever the game's bindings.
        const int button = control == PromptControl::confirm ? SDL_GAMEPAD_BUTTON_SOUTH
                           : control == PromptControl::back  ? SDL_GAMEPAD_BUTTON_EAST
                           : control == PromptControl::erase ? SDL_GAMEPAD_BUTTON_WEST
                                                             : SDL_GAMEPAD_BUTTON_NORTH;
        result.face = face_of(family, settings.nintendo_layout, PadInput{PadInput::Kind::button, button});
        return result;
    }
    switch (control) {
    case PromptControl::stick: result.face = Face::left_stick; return result;
    case PromptControl::c_stick: result.face = family == PadFamily::gamecube ? Face::c_stick : Face::right_stick; return result;
    case PromptControl::dpad: result.face = Face::dpad; return result;
    case PromptControl::menu:
        result.face = face_of(family, settings.nintendo_layout, PadInput{PadInput::Kind::button, SDL_GAMEPAD_BUTTON_BACK});
        if (result.face == Face::none) result.key = "F1";
        return result;
    default: break;
    }
    for (const auto& input : settings.pad[std::size_t(key)]) {
        if (input.kind == PadInput::Kind::none) continue;
        result.face = face_of(family, settings.nintendo_layout, input);
        if (result.face != Face::none) return result;
    }
    return result; // unbound: nothing to draw
}

void PromptTracker::pad_added(SDL_JoystickID id, PadFamily family, std::string name) {
    for (auto& pad : pads_)
        if (pad.id == id) {
            pad.family = family;
            pad.name = std::move(name);
            return;
        }
    pads_.push_back({id, family, std::move(name)});
}

void PromptTracker::pad_removed(SDL_JoystickID id) {
    pads_.erase(std::remove_if(pads_.begin(), pads_.end(), [&](const Pad& pad) { return pad.id == id; }), pads_.end());
    // Player 1's pad went away: the prompts fall back to the new first pad
    // (or the keyboard) until the next real input.
    if (last_ == Last::pad && last_pad_ == id) last_ = Last::none;
}

void PromptTracker::pad_input(SDL_JoystickID id) {
    // Only player 1's pad decides the prompts (a further player's pad is
    // his own slot).
    if (!pads_.empty() && pads_.front().id == id) {
        last_ = Last::pad;
        last_pad_ = id;
    }
}

void PromptTracker::key_input() { last_ = Last::keyboard; }

PadFamily PromptTracker::player_family(std::size_t player) const {
    if (player == 0) {
        if (last_ == Last::keyboard) return PadFamily::keyboard;
        if (last_ == Last::pad && !pads_.empty() && pads_.front().id == last_pad_) return pads_.front().family;
        return pads_.empty() ? PadFamily::keyboard : pads_.front().family;
    }
    return player < pads_.size() ? pads_[player].family : PadFamily::generic;
}

InputKind real_input(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        return !event.key.repeat && !host_key(event.key.scancode) ? InputKind::keyboard : InputKind::none;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        return InputKind::pad;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const int value = event.gaxis.value;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            return value > kTriggerPressed ? InputKind::pad : InputKind::none;
        // Sticks past half way (a drifting stick does not switch the prompts).
        return std::abs(value) > 16384 ? InputKind::pad : InputKind::none;
    }
    default: return InputKind::none;
    }
}

} // namespace dw4::game
