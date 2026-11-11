#include "settings.hpp"

#include "dw4/pad.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace dw4::game {
namespace {

enum class Channel { button, stick, c_stick };

struct KeyInfo {
    const char* name;
    const char* ini;
    PadGroup group;
    Channel channel;
    std::uint32_t bit; // pad_button bit or pad_direction bit of the channel
    std::array<SDL_Scancode, Settings::kKeysPerButton> keys;
};

// Default keyboard: WASD walk (main stick), the arrow keys are the D-pad and
// the keypad's 8 / 2 / 4 / 6 the C-stick (both move the quick panels'
// cursor), the right hand's I / J / K / L the face buttons.
const std::array<KeyInfo, kPadKeyCount> kKeys{{
    {"A", "a", PadGroup::buttons, Channel::button, pad_button::a, {SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE, SDL_SCANCODE_K}},
    {"B", "b", PadGroup::buttons, Channel::button, pad_button::b, {SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_L}},
    {"X", "x", PadGroup::buttons, Channel::button, pad_button::x, {SDL_SCANCODE_J}},
    {"Y", "y", PadGroup::buttons, Channel::button, pad_button::y, {SDL_SCANCODE_I}},
    {"Start", "start", PadGroup::buttons, Channel::button, pad_button::start, {SDL_SCANCODE_ESCAPE, SDL_SCANCODE_TAB}},
    {"L", "l", PadGroup::buttons, Channel::button, pad_button::l, {SDL_SCANCODE_Q}},
    {"R", "r", PadGroup::buttons, Channel::button, pad_button::r, {SDL_SCANCODE_E}},
    {"Z", "z", PadGroup::buttons, Channel::button, pad_button::z, {SDL_SCANCODE_Z}},
    {"Stick up", "stick_up", PadGroup::stick, Channel::stick, pad_direction::positive_y, {SDL_SCANCODE_W}},
    {"Stick down", "stick_down", PadGroup::stick, Channel::stick, pad_direction::negative_y, {SDL_SCANCODE_S}},
    {"Stick left", "stick_left", PadGroup::stick, Channel::stick, pad_direction::negative_x, {SDL_SCANCODE_A}},
    {"Stick right", "stick_right", PadGroup::stick, Channel::stick, pad_direction::positive_x, {SDL_SCANCODE_D}},
    {"D-pad up", "dpad_up", PadGroup::dpad, Channel::button, pad_button::up, {SDL_SCANCODE_UP}},
    {"D-pad down", "dpad_down", PadGroup::dpad, Channel::button, pad_button::down, {SDL_SCANCODE_DOWN}},
    {"D-pad left", "dpad_left", PadGroup::dpad, Channel::button, pad_button::left, {SDL_SCANCODE_LEFT}},
    {"D-pad right", "dpad_right", PadGroup::dpad, Channel::button, pad_button::right, {SDL_SCANCODE_RIGHT}},
    {"C-stick up", "cstick_up", PadGroup::c_stick, Channel::c_stick, pad_direction::positive_y, {SDL_SCANCODE_KP_8}},
    {"C-stick down", "cstick_down", PadGroup::c_stick, Channel::c_stick, pad_direction::negative_y, {SDL_SCANCODE_KP_2}},
    {"C-stick left", "cstick_left", PadGroup::c_stick, Channel::c_stick, pad_direction::negative_x, {SDL_SCANCODE_KP_4}},
    {"C-stick right", "cstick_right", PadGroup::c_stick, Channel::c_stick, pad_direction::positive_x, {SDL_SCANCODE_KP_6}},
}};

// Before the separate D-pad and C-stick inputs, [keyboard] / [gamepad] "up",
// "down", "left" and "right" were one direction input that set the D-pad bit
// and pushed the main stick (keyboard default "W, Up", controller "dpup").
constexpr const char* kLegacyDirections[4] = {"up", "down", "left", "right"};
constexpr PadKey kLegacyStick[4] = {PadKey::stick_up, PadKey::stick_down, PadKey::stick_left, PadKey::stick_right};
constexpr PadKey kLegacyDpad[4] = {PadKey::dpad_up, PadKey::dpad_down, PadKey::dpad_left, PadKey::dpad_right};

std::string key_text(SDL_Scancode code) {
    if (code == SDL_SCANCODE_UNKNOWN) return {};
    const char* name = SDL_GetScancodeName(code);
    return name ? name : std::string();
}

SDL_Scancode key_code(const std::string& text) {
    if (text.empty()) return SDL_SCANCODE_UNKNOWN;
    return SDL_GetScancodeFromName(text.c_str());
}

bool arrow_key(SDL_Scancode code) {
    return code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT;
}

// "Return, Space, K": up to three names.
std::vector<std::string> split_names(const std::string& value) {
    std::vector<std::string> names;
    std::size_t start = 0;
    for (std::size_t k = 0; k < Settings::kKeysPerButton && start <= value.size(); ++k) {
        const auto comma = std::min(value.find(',', start), value.size());
        std::string name = value.substr(start, comma - start);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        while (!name.empty() && name.back() == ' ') name.pop_back();
        names.push_back(std::move(name));
        start = comma + 1;
    }
    return names;
}

template <class T, class Parse>
std::array<T, Settings::kKeysPerButton> parse_list(const std::string& value, Parse parse) {
    std::array<T, Settings::kKeysPerButton> result{};
    const auto names = split_names(value);
    for (std::size_t k = 0; k < names.size() && k < result.size(); ++k) result[k] = parse(names[k]);
    return result;
}

// A default binding gives way to the bindings the file sets (a key or
// controller input stands for one control only): a migrated layout that
// keeps the arrow keys on the main stick leaves the D-pad without them.
template <class T>
void drop_defaults_in_use(std::array<std::array<T, Settings::kKeysPerButton>, kPadKeyCount>& bindings,
                          const std::array<bool, kPadKeyCount>& from_file) {
    for (std::size_t i = 0; i < kPadKeyCount; ++i) {
        if (from_file[i]) continue;
        for (auto& input : bindings[i]) {
            if (input == T{}) continue;
            for (std::size_t j = 0; j < kPadKeyCount; ++j)
                if (from_file[j] && std::find(bindings[j].begin(), bindings[j].end(), input) != bindings[j].end()) input = T{};
        }
    }
}

std::filesystem::path program_folder() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(base) : std::filesystem::path();
}

} // namespace

// GameCube layout on a standard controller: A south, B east, X west, Y north,
// L the left trigger or shoulder, R the right trigger, Z the right shoulder,
// the D-pad the D-pad. The left stick is the main stick and the right stick
// the C-stick (game_main.cpp); stick directions take no button by default.
using B = PadInput::Kind;
const std::array<std::array<PadInput, Settings::kKeysPerButton>, kPadKeyCount> kPadDefaults{{
    {{{B::button, SDL_GAMEPAD_BUTTON_SOUTH}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_EAST}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_WEST}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_NORTH}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_START}}},
    {{{B::axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {B::button, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}}},
    {{{B::axis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}}},
    {}, {}, {}, {},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_UP}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_DOWN}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_LEFT}}},
    {{{B::button, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}}},
    {}, {}, {}, {},
}};

const char* pad_key_name(PadKey key) { return kKeys[std::size_t(key)].name; }
const char* pad_key_ini_name(PadKey key) { return kKeys[std::size_t(key)].ini; }
PadGroup pad_key_group(PadKey key) { return kKeys[std::size_t(key)].group; }

const char* pad_group_name(PadGroup group) {
    switch (group) {
    case PadGroup::buttons: return "Buttons";
    case PadGroup::stick: return "Main stick (walks)";
    case PadGroup::dpad: return "D-pad (menus, quick panel cursor)";
    case PadGroup::c_stick: return "C-stick (quick panel cursor)";
    }
    return "";
}

void press(PadKey key, HostPadInput& input) {
    const auto& info = kKeys[std::size_t(key)];
    switch (info.channel) {
    case Channel::button: input.buttons |= info.bit; break;
    case Channel::stick: input.stick_directions |= info.bit; break;
    case Channel::c_stick: input.c_stick_directions |= info.bit; break;
    }
}

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

Settings::Settings() {
    reset_keys();
    reset_pad();
}

void Settings::reset_pad() { pad = kPadDefaults; }

void Settings::reset_keys() {
    for (std::size_t i = 0; i < kPadKeyCount; ++i) keys[i] = kKeys[i].keys;
}

Settings Settings::load() {
    IniFile ini;
    std::filesystem::path file;
    const std::filesystem::path candidates[] = {"local/settings.ini", "settings.ini", program_folder() / "settings.ini"};
    for (const auto& candidate : candidates) {
        if (auto found = IniFile::load(candidate)) {
            ini = std::move(*found);
            file = candidate;
            break;
        }
    }
    Settings settings = parse(std::move(ini));
    settings.file = file.empty() ? program_folder() / "settings.ini" : file;
    return settings;
}

Settings Settings::parse(IniFile document) {
    Settings settings;
    settings.ini_ = std::move(document);
    const auto& ini = settings.ini_;
    settings.width = std::clamp(ini.get_int("window", "width", settings.width), 320, 7680);
    settings.height = std::clamp(ini.get_int("window", "height", settings.height), 240, 4320);
    settings.fullscreen = ini.get_bool("window", "fullscreen", ini.get_bool("window", "borderless", settings.fullscreen));
    settings.vsync = ini.get_bool("window", "vsync", settings.vsync);
    settings.overflow_npc = ini.get_bool("overflow", "npc", true);
    settings.widescreen = ini.get_bool("window", "widescreen", ini.get_or("preferences", "aspect", "") == "16:9");
    settings.fps_limit = std::clamp(ini.get_int("window", "fps_limit", settings.fps_limit), 0, 1000);
    settings.smooth_motion = ini.get_bool("window", "smooth_motion", settings.smooth_motion);
    settings.msaa = std::clamp(ini.get_int("window", "msaa", settings.msaa), 0, 16);
    settings.volume = std::clamp(ini.get_int("audio", "volume", settings.volume), 0, 100);
    settings.pause_in_menu = ini.get_bool("menu", "pause", settings.pause_in_menu);
    settings.skip_intro = ini.get_bool("preferences", "skip_intro", settings.skip_intro);
    settings.exp_rate = std::clamp(ini.get_int("gameplay", "exp_rate", settings.exp_rate), 10, 1000);
    settings.damage_dealt = std::clamp(ini.get_int("gameplay", "damage_dealt", settings.damage_dealt), 10, 1000);
    settings.damage_taken = std::clamp(ini.get_int("gameplay", "damage_taken", settings.damage_taken), 10, 1000);
    if (ini.get("randomizer", "seed")) {
        settings.rand_seed = std::uint32_t(std::max(0, ini.get_int("randomizer", "seed", 0)));
        settings.rand_drops = ini.get_bool("randomizer", "drops", false);
        const std::string enemies = ini.get_or("randomizer", "enemies", "off");
        settings.rand_enemies = enemies == "wave" ? 2 : enemies == "each" ? 1 : 0;
        settings.rand_zones = ini.get_or("randomizer", "zones", "off") == "dungeons" ? 1 : 0;
    } else if (const int drop_seed = ini.get_int("gameplay", "drop_seed", 0); drop_seed > 0) {
        // The item drop seed of earlier versions: the same drops.
        settings.rand_seed = std::uint32_t(drop_seed);
        settings.rand_drops = true;
    }
    {
        std::string loading = ini.get_or("gameplay", "loading", "original");
        for (auto& c : loading) c = char(std::tolower(static_cast<unsigned char>(c)));
        settings.fast_loading = loading == "fast";
    }
    // [keyboard]: the controls the file names, then the old direction
    // inputs: their keys go to the main stick, except the arrow keys, which
    // stay the D-pad's (the new default) unless a direction had only arrow
    // keys - then they keep walking and the D-pad keys stay unbound.
    std::array<bool, kPadKeyCount> keys_from_file{};
    for (std::size_t i = 0; i < kPadKeyCount; ++i)
        if (const auto value = ini.get("keyboard", kKeys[i].ini)) {
            settings.keys[i] = parse_list<SDL_Scancode>(*value, key_code);
            keys_from_file[i] = true;
        }
    for (int d = 0; d < 4; ++d) {
        const auto value = ini.get("keyboard", kLegacyDirections[d]);
        const auto stick = std::size_t(kLegacyStick[d]);
        if (!value || keys_from_file[stick]) continue;
        const auto old = parse_list<SDL_Scancode>(*value, key_code);
        std::array<SDL_Scancode, kKeysPerButton> walk{};
        std::size_t count = 0;
        for (const auto code : old)
            if (code != SDL_SCANCODE_UNKNOWN && !arrow_key(code)) walk[count++] = code;
        settings.keys[stick] = count ? walk : old;
        keys_from_file[stick] = true;
    }
    drop_defaults_in_use(settings.keys, keys_from_file);
    // [gamepad]: the old direction inputs were the D-pad buttons by default
    // and become the D-pad (which no longer walks, like on the GameCube).
    std::array<bool, kPadKeyCount> pad_from_file{};
    for (std::size_t i = 0; i < kPadKeyCount; ++i)
        if (const auto value = ini.get("gamepad", kKeys[i].ini)) {
            settings.pad[i] = parse_list<PadInput>(*value, [](const std::string& name) { return pad_input_from_name(name); });
            pad_from_file[i] = true;
        }
    for (int d = 0; d < 4; ++d) {
        const auto value = ini.get("gamepad", kLegacyDirections[d]);
        const auto dpad = std::size_t(kLegacyDpad[d]);
        if (!value || pad_from_file[dpad]) continue;
        settings.pad[dpad] = parse_list<PadInput>(*value, [](const std::string& name) { return pad_input_from_name(name); });
        pad_from_file[dpad] = true;
    }
    drop_defaults_in_use(settings.pad, pad_from_file);
    // The controller families (pad_family.hpp). A file from before the
    // Nintendo layout that binds the face buttons its own way keeps reading
    // every pad by position, as those bindings were made for (a Switch pad's
    // A that a player put on "b" stays where he put it); untouched face
    // buttons read Nintendo pads by their letters.
    if (const auto layout = ini.get("gamepad", "nintendo_layout")) {
        settings.nintendo_layout = *layout == "position" ? NintendoLayout::by_position : NintendoLayout::by_label;
    } else {
        bool custom_faces = false;
        for (const auto key : {PadKey::a, PadKey::b, PadKey::x, PadKey::y})
            if (pad_from_file[std::size_t(key)] && settings.pad[std::size_t(key)] != kPadDefaults[std::size_t(key)]) custom_faces = true;
        settings.nintendo_layout = custom_faces ? NintendoLayout::by_position : NintendoLayout::by_label;
    }
    settings.gamecube_layout =
        ini.get_or("gamepad", "gamecube_layout", "original") == "bindings" ? GameCubeLayout::bindings : GameCubeLayout::original;
    settings.prompts = ini.get_or("gamepad", "prompts", settings.prompts);
    {
        // ("true" / "false" from an earlier build of this round read as on / off.)
        std::string wii = ini.get_or("gamepad", "wii_driver", "auto");
        for (auto& c : wii) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (wii == "on" || wii == "1" || wii == "true" || wii == "yes") settings.wii_driver = WiiDriver::on;
        else if (wii == "off" || wii == "0" || wii == "false" || wii == "no") settings.wii_driver = WiiDriver::off;
        else settings.wii_driver = WiiDriver::automatic;
    }
    settings.menu_buttons =
        ini.get_or("gamepad", "menu_buttons", "layout") == "bindings" ? MenuButtons::bindings : MenuButtons::layout;
    settings.gc_adapter = ini.get_bool("gamepad", "gc_adapter", settings.gc_adapter);
    settings.texture_pack = ini.get_or("mods", "textures", "");
    settings.dump_textures = ini.get_bool("mods", "dump_textures", settings.dump_textures);
    {
        const std::string list = ini.get_or("mods", "disabled", "");
        std::size_t start = 0;
        while (start < list.size()) {
            const auto comma = std::min(list.find(',', start), list.size());
            std::string id = list.substr(start, comma - start);
            while (!id.empty() && id.front() == ' ') id.erase(id.begin());
            while (!id.empty() && id.back() == ' ') id.pop_back();
            if (!id.empty()) settings.disabled_mods.insert(id);
            start = comma + 1;
        }
    }
    settings.overhead_labels = ini.get_or("hud", "overhead_labels", settings.overhead_labels);
    settings.label_scale = std::clamp(ini.get_int("hud", "label_scale", settings.label_scale), 50, 200);
    settings.label_numbers = ini.get_bool("hud", "label_numbers", settings.label_numbers);
    settings.discord = ini.get_bool("discord", "enabled", settings.discord);
    settings.discord_client_id = ini.get_or("discord", "client_id", settings.discord_client_id);
    // (An id is digits only; anything else falls back to the default.)
    if (settings.discord_client_id.empty() || settings.discord_client_id.size() > 24 ||
        !std::all_of(settings.discord_client_id.begin(), settings.discord_client_id.end(), [](char c) { return c >= '0' && c <= '9'; }))
        settings.discord_client_id = kDiscordClientId;
    settings.achievements = ini.get_bool("achievements", "enabled", settings.achievements);
    settings.achievement_popups = ini.get_bool("achievements", "popups", settings.achievement_popups);
    settings.net_indicator = ini.get_bool("online", "net_indicator", settings.net_indicator);
    settings.data = ini.get_or("data", "path", "");
    settings.save_file = ini.get_or("data", "save", settings.save_file.string());
    settings.save_file_b = ini.get_or("data", "save_b", settings.save_file_b.string());
    settings.save_migrated = ini.get_bool("data", "save_migrated", settings.save_migrated);
    settings.cards = ini.get_or("data", "cards", "");
    settings.card_a = ini.get_or("data", "card_a", "");
    settings.card_b = ini.get_or("data", "card_b", "");
    return settings;
}

void Settings::store() {
    ini_.set_int("window", "width", width);
    ini_.set_int("window", "height", height);
    ini_.set_bool("window", "fullscreen", fullscreen);
    ini_.set_bool("window", "vsync", vsync);
    ini_.set_bool("window", "widescreen", widescreen);
    ini_.set_bool("overflow", "npc", overflow_npc);
    ini_.set_int("window", "fps_limit", fps_limit);
    ini_.set_bool("window", "smooth_motion", smooth_motion);
    ini_.set_int("window", "msaa", msaa);
    ini_.set_int("audio", "volume", volume);
    ini_.set_bool("menu", "pause", pause_in_menu);
    ini_.set_bool("preferences", "skip_intro", skip_intro);
    ini_.set_int("gameplay", "exp_rate", exp_rate);
    ini_.set_int("gameplay", "damage_dealt", damage_dealt);
    ini_.set_int("gameplay", "damage_taken", damage_taken);
    ini_.remove("gameplay", "drop_seed");
    ini_.set_int("randomizer", "seed", int(rand_seed & 0x7FFFFFFF));
    ini_.set_bool("randomizer", "drops", rand_drops);
    ini_.set("randomizer", "enemies", rand_enemies == 2 ? "wave" : rand_enemies == 1 ? "each" : "off");
    ini_.set("randomizer", "zones", rand_zones ? "dungeons" : "off");
    ini_.set("gameplay", "loading", fast_loading ? "fast" : "original");
    // The old combined direction inputs are migrated (parse) and dropped.
    for (const char* legacy : kLegacyDirections) {
        ini_.remove("keyboard", legacy);
        ini_.remove("gamepad", legacy);
    }
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
    ini_.set("gamepad", "nintendo_layout", nintendo_layout == NintendoLayout::by_position ? "position" : "label");
    ini_.set("gamepad", "gamecube_layout", gamecube_layout == GameCubeLayout::bindings ? "bindings" : "original");
    ini_.set("gamepad", "prompts", prompts);
    ini_.set("gamepad", "wii_driver", wii_driver == WiiDriver::on ? "on" : wii_driver == WiiDriver::off ? "off" : "auto");
    ini_.set("gamepad", "menu_buttons", menu_buttons == MenuButtons::bindings ? "bindings" : "layout");
    ini_.set_bool("gamepad", "gc_adapter", gc_adapter);
    if (!texture_pack.empty()) ini_.set("mods", "textures", texture_pack.string());
    ini_.set_bool("mods", "dump_textures", dump_textures);
    {
        std::string list;
        for (const auto& id : disabled_mods) list += (list.empty() ? "" : ",") + id;
        ini_.set("mods", "disabled", list);
    }
    ini_.set("hud", "overhead_labels", overhead_labels);
    ini_.set_int("hud", "label_scale", label_scale);
    ini_.set_bool("hud", "label_numbers", label_numbers);
    ini_.set_bool("discord", "enabled", discord);
    ini_.set("discord", "client_id", discord_client_id);
    ini_.set_bool("achievements", "enabled", achievements);
    ini_.set_bool("achievements", "popups", achievement_popups);
    ini_.set_bool("online", "net_indicator", net_indicator);
    if (!data.empty()) ini_.set("data", "path", data.string());
    // Where the saves live, so settings.ini shows it.
    ini_.set("data", "save", save_file.generic_string());
    ini_.set("data", "save_b", save_file_b.generic_string());
    ini_.set_bool("data", "save_migrated", save_migrated);
}

bool Settings::save() {
    store();
    return ini_.save(file);
}

} // namespace dw4::game
