#include "settings.hpp"

#include "dw4/pad.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <string>

namespace dw4::game {
namespace {

struct KeyInfo {
    const char* name;
    const char* ini;
    std::uint32_t bit;
    std::array<SDL_Scancode, Settings::kKeysPerButton> keys;
};

const std::array<KeyInfo, kPadKeyCount> kKeys{{
    {"A", "a", pad_button::a, {SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE, SDL_SCANCODE_K}},
    {"B", "b", pad_button::b, {SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_L}},
    {"X", "x", pad_button::x, {SDL_SCANCODE_J}},
    {"Y", "y", pad_button::y, {SDL_SCANCODE_I}},
    {"Start", "start", pad_button::start, {SDL_SCANCODE_ESCAPE, SDL_SCANCODE_TAB}},
    {"Up", "up", pad_button::up, {SDL_SCANCODE_W, SDL_SCANCODE_UP}},
    {"Down", "down", pad_button::down, {SDL_SCANCODE_S, SDL_SCANCODE_DOWN}},
    {"Left", "left", pad_button::left, {SDL_SCANCODE_A, SDL_SCANCODE_LEFT}},
    {"Right", "right", pad_button::right, {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT}},
    {"L", "l", pad_button::l, {SDL_SCANCODE_Q}},
    {"R", "r", pad_button::r, {SDL_SCANCODE_E}},
    {"Z", "z", pad_button::z, {SDL_SCANCODE_Z}},
}};

std::string key_text(SDL_Scancode code) {
    if (code == SDL_SCANCODE_UNKNOWN) return {};
    const char* name = SDL_GetScancodeName(code);
    return name ? name : std::string();
}

SDL_Scancode key_code(const std::string& text) {
    if (text.empty()) return SDL_SCANCODE_UNKNOWN;
    return SDL_GetScancodeFromName(text.c_str());
}

std::filesystem::path program_folder() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(base) : std::filesystem::path();
}

} // namespace

// GameCube layout on a standard controller: A south, B east, X west, Y north,
// L the left trigger or shoulder, R the right trigger, Z the right shoulder.
using B = PadInput::Kind;
const std::array<std::array<PadInput, Settings::kKeysPerButton>, kPadKeyCount> kPadDefaults{{
    {{{B::button, SDL_GAMEPAD_BUTTON_SOUTH}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_EAST}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_WEST}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_NORTH}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_START}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_UP}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_DOWN}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_LEFT}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}}},
    {{{B::axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {B::button, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}}},
    {{{B::axis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}}},
}};

const char* pad_key_name(PadKey key) { return kKeys[std::size_t(key)].name; }

std::string pad_input_name(const PadInput& input) {
    const char* name = nullptr;
    if (input.kind == PadInput::Kind::button) name = SDL_GetGamepadStringForButton(SDL_GamepadButton(input.code));
    else if (input.kind == PadInput::Kind::axis) name = SDL_GetGamepadStringForAxis(SDL_GamepadAxis(input.code));
    return name ? name : std::string();
}

PadInput pad_input_from_name(std::string_view text) {
    if (text.empty()) return {};
    const std::string name(text);
    if (const auto button = SDL_GetGamepadButtonFromString(name.c_str()); button != SDL_GAMEPAD_BUTTON_INVALID)
        return {PadInput::Kind::button, int(button)};
    if (const auto axis = SDL_GetGamepadAxisFromString(name.c_str());
        axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
        return {PadInput::Kind::axis, int(axis)};
    return {};
}
const char* pad_key_ini_name(PadKey key) { return kKeys[std::size_t(key)].ini; }
std::uint32_t pad_key_bit(PadKey key) { return kKeys[std::size_t(key)].bit; }

Settings::Settings() {
    reset_keys();
    reset_pad();
}

void Settings::reset_pad() { pad = kPadDefaults; }

void Settings::reset_keys() {
    for (std::size_t i = 0; i < kPadKeyCount; ++i) keys[i] = kKeys[i].keys;
}

Settings Settings::load() {
    Settings settings;
    const std::filesystem::path candidates[] = {"local/settings.ini", "settings.ini", program_folder() / "settings.ini"};
    for (const auto& candidate : candidates) {
        if (auto ini = IniFile::load(candidate)) {
            settings.ini_ = std::move(*ini);
            settings.file = candidate;
            break;
        }
    }
    if (settings.file.empty()) settings.file = program_folder() / "settings.ini";
    const auto& ini = settings.ini_;
    settings.width = std::clamp(ini.get_int("window", "width", settings.width), 320, 7680);
    settings.height = std::clamp(ini.get_int("window", "height", settings.height), 240, 4320);
    settings.fullscreen = ini.get_bool("window", "fullscreen", ini.get_bool("window", "borderless", settings.fullscreen));
    settings.vsync = ini.get_bool("window", "vsync", settings.vsync);
    settings.widescreen = ini.get_bool("window", "widescreen", ini.get_or("preferences", "aspect", "") == "16:9");
    settings.fps_limit = std::clamp(ini.get_int("window", "fps_limit", settings.fps_limit), 0, 1000);
    settings.smooth_motion = ini.get_bool("window", "smooth_motion", settings.smooth_motion);
    settings.msaa = std::clamp(ini.get_int("window", "msaa", settings.msaa), 0, 16);
    settings.volume = std::clamp(ini.get_int("audio", "volume", settings.volume), 0, 100);
    settings.pause_in_menu = ini.get_bool("menu", "pause", settings.pause_in_menu);
    for (std::size_t i = 0; i < kPadKeyCount; ++i) {
        const auto value = ini.get("keyboard", kKeys[i].ini);
        if (!value) continue;
        // "Return, Space, K": up to three key names.
        settings.keys[i] = {};
        std::size_t start = 0;
        for (std::size_t k = 0; k < kKeysPerButton && start <= value->size(); ++k) {
            const auto comma = std::min(value->find(',', start), value->size());
            std::string name = value->substr(start, comma - start);
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!name.empty() && name.back() == ' ') name.pop_back();
            settings.keys[i][k] = key_code(name);
            start = comma + 1;
        }
    }
    for (std::size_t i = 0; i < kPadKeyCount; ++i) {
        const auto value = ini.get("gamepad", kKeys[i].ini);
        if (!value) continue;
        settings.pad[i] = {};
        std::size_t start = 0;
        for (std::size_t k = 0; k < kKeysPerButton && start <= value->size(); ++k) {
            const auto comma = std::min(value->find(',', start), value->size());
            std::string name = value->substr(start, comma - start);
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!name.empty() && name.back() == ' ') name.pop_back();
            settings.pad[i][k] = pad_input_from_name(name);
            start = comma + 1;
        }
    }
    settings.texture_pack = ini.get_or("mods", "textures", "");
    settings.dump_textures = ini.get_bool("mods", "dump_textures", settings.dump_textures);
    settings.data = ini.get_or("data", "path", "");
    return settings;
}

bool Settings::save() {
    ini_.set_int("window", "width", width);
    ini_.set_int("window", "height", height);
    ini_.set_bool("window", "fullscreen", fullscreen);
    ini_.set_bool("window", "vsync", vsync);
    ini_.set_bool("window", "widescreen", widescreen);
    ini_.set_int("window", "fps_limit", fps_limit);
    ini_.set_bool("window", "smooth_motion", smooth_motion);
    ini_.set_int("window", "msaa", msaa);
    ini_.set_int("audio", "volume", volume);
    ini_.set_bool("menu", "pause", pause_in_menu);
    for (std::size_t i = 0; i < kPadKeyCount; ++i) {
        std::string value;
        for (const auto code : keys[i]) {
            const auto name = key_text(code);
            if (name.empty()) continue;
            if (!value.empty()) value += ", ";
            value += name;
        }
        ini_.set("keyboard", kKeys[i].ini, value);
    }
    for (std::size_t i = 0; i < kPadKeyCount; ++i) {
        std::string value;
        for (const auto& input : pad[i]) {
            const auto name = pad_input_name(input);
            if (name.empty()) continue;
            if (!value.empty()) value += ", ";
            value += name;
        }
        ini_.set("gamepad", kKeys[i].ini, value);
    }
    if (!texture_pack.empty()) ini_.set("mods", "textures", texture_pack.string());
    ini_.set_bool("mods", "dump_textures", dump_textures);
    if (!data.empty()) ini_.set("data", "path", data.string());
    return ini_.save(file);
}

} // namespace dw4::game
