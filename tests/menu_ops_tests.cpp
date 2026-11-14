// The F1 menu's one apply layer and its option tables (src/game/menu_ops,
// src/game/menu_choices; docs/CONSOLE_MENU.md): every op sets exactly its
// settings.ini field, the "changed" flag and its side effect (recorded by a
// fake), and Settings::store writes the same bytes as the direct field
// assignment the PC's F1 menu did before; the choice and stepper arithmetic
// with a value settings.ini holds that no list has.
#include "../src/game/menu_choices.hpp"
#include "../src/game/menu_ops.hpp"

#include <cmath>
#include <functional>
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

struct Recorder final : ops::Effects {
    std::vector<std::string> calls;
    void swap_interval(bool vsync) override { calls.push_back(vsync ? "swap 1" : "swap 0"); }
    void frame_rate(const Settings& settings) override { calls.push_back("rate " + std::to_string(settings.fps_limit)); }
    void graphics(const GraphicsOptions& options) override { calls.push_back("graphics " + std::to_string(options.texture_memory_mb)); }
    void thor_style() override { calls.push_back("thor"); }
};

// The settings.ini text of a Settings after store() (every key).
std::string stored(Settings settings) {
    settings.store();
    return settings.ini().text();
}

// One op against the same change made directly: the same fields, the flag
// set, the stored bytes equal, the side effect as expected.
void same(const char* name, const std::function<void(ops::Target)>& op, const std::function<void(Settings&)>& direct,
          const std::vector<std::string>& effects = {}) {
    Settings by_op, by_hand;
    bool changed = false;
    Recorder fx;
    op({by_op, changed, fx});
    direct(by_hand);
    check(changed, std::string(name) + ": settings_changed");
    check(stored(by_op) == stored(by_hand), std::string(name) + ": the stored settings differ from the direct assignment");
    check(fx.calls == effects, std::string(name) + ": side effects");
}

void ops_match_direct_assignment() {
    same("ui_scale", [](ops::Target t) { ops::set_ui_scale(t, 175); }, [](Settings& s) { s.ui_scale = 175; });
    same("widescreen", [](ops::Target t) { ops::set_widescreen(t, true); }, [](Settings& s) { s.widescreen = true; });
    same("vsync", [](ops::Target t) { ops::set_vsync(t, false); }, [](Settings& s) { s.vsync = false; }, {"swap 0"});
    same("frame limit", [](ops::Target t) { ops::set_frame_limit(t, 120); }, [](Settings& s) { s.fps_limit = 120; }, {"rate 120"});
    same("smooth motion", [](ops::Target t) { ops::set_smooth_motion(t, false); }, [](Settings& s) { s.smooth_motion = false; });
    same("msaa", [](ops::Target t) { ops::set_msaa(t, 2); }, [](Settings& s) { s.msaa = 2; });
    {
        GraphicsOptions g;
        g.texture_memory_mb = 2048;
        g.lod_bias = 9.0f; // (clamped)
        same("graphics", [g](ops::Target t) { ops::set_graphics(t, g); }, [g](Settings& s) { s.graphics = g.clamped(); }, {"graphics 2048"});
    }
    int reloads = 0;
    same("texture pack", [&](ops::Target t) { ops::set_texture_pack_folder(t, "packs/hd", [&] { ++reloads; }); },
         [](Settings& s) { s.texture_pack = "packs/hd"; });
    check(reloads == 1, "the pack folder reloads the pack");
    same("dump textures", [&](ops::Target t) { ops::set_dump_textures(t, true, [&] { ++reloads; }); }, [](Settings& s) { s.dump_textures = true; });
    check(reloads == 2, "dumping reloads the pack");
    same("volume", [](ops::Target t) { ops::set_volume(t, 35); }, [](Settings& s) { s.volume = 35; });
    same("label mode", [](ops::Target t) { ops::set_label_mode(t, LabelMode::others); }, [](Settings& s) { s.overhead_labels = "others"; });
    same("label scale", [](ops::Target t) { ops::set_label_scale(t, 150); }, [](Settings& s) { s.label_scale = 150; });
    same("label numbers", [](ops::Target t) { ops::set_label_numbers(t, true); }, [](Settings& s) { s.label_numbers = true; });
    same("achievements", [](ops::Target t) { ops::set_achievements(t, false); }, [](Settings& s) { s.achievements = false; });
    same("popups", [](ops::Target t) { ops::set_achievement_popups(t, false); }, [](Settings& s) { s.achievement_popups = false; });
    same("net indicator", [](ops::Target t) { ops::set_net_indicator(t, false); }, [](Settings& s) { s.net_indicator = false; });
    same("prompts", [](ops::Target t) { ops::set_prompts(t, PadFamily::thor); }, [](Settings& s) { s.prompts = family_id(PadFamily::thor); });
    same("prompts auto", [](ops::Target t) { ops::set_prompts(t, std::nullopt); }, [](Settings& s) { s.prompts = "auto"; });
    same("thor style", [](ops::Target t) { ops::set_thor_style(t, ThorStyle::xbox); }, [](Settings& s) { s.thor_style = ThorStyle::xbox; }, {"thor"});
    same("nintendo layout", [](ops::Target t) { ops::set_nintendo_layout(t, NintendoLayout::by_position); },
         [](Settings& s) { s.nintendo_layout = NintendoLayout::by_position; });
    same("menu buttons", [](ops::Target t) { ops::set_menu_buttons(t, MenuButtons::bindings); }, [](Settings& s) { s.menu_buttons = MenuButtons::bindings; });
    same("gamecube layout", [](ops::Target t) { ops::set_gamecube_layout(t, GameCubeLayout::bindings); },
         [](Settings& s) { s.gamecube_layout = GameCubeLayout::bindings; });
    same("clear binding", [](ops::Target t) { ops::clear_pad_binding(t, PadKey::a, 0); }, [](Settings& s) { s.pad[std::size_t(PadKey::a)][0] = {}; });
    same("reset bindings",
         [](ops::Target t) {
             t.settings.pad[0][0] = {};
             ops::reset_pad_bindings(t);
         },
         [](Settings& s) { s.reset_pad(); });
    same("screen2 mode", [](ops::Target t) { ops::set_screen2_mode(t, Screen2Mode::off); }, [](Settings& s) { s.screen2_mode = Screen2Mode::off; });
    same("screen2 overlay", [](ops::Target t) { ops::set_screen2_overlay(t, true); }, [](Settings& s) { s.screen2_overlay = true; });
    same("pause", [](ops::Target t) { ops::set_pause_in_menu(t, false); }, [](Settings& s) { s.pause_in_menu = false; });
    same("pause unfocused", [](ops::Target t) { ops::set_pause_unfocused(t, false); }, [](Settings& s) { s.pause_unfocused = false; });
    same("skip intro", [](ops::Target t) { ops::set_skip_intro(t, true); }, [](Settings& s) { s.skip_intro = true; });
    same("mod off", [](ops::Target t) { ops::set_mod_enabled(t, "hd", false); }, [](Settings& s) { s.disabled_mods.insert("hd"); });
    same("mod on",
         [](ops::Target t) {
             t.settings.disabled_mods.insert("hd");
             ops::set_mod_enabled(t, "hd", true);
         },
         [](Settings&) {});
    // An out-of-range binding is left alone.
    {
        Settings s;
        bool changed = false;
        Recorder fx;
        ops::clear_pad_binding({s, changed, fx}, PadKey::count, 0);
        check(!changed, "an unknown key changes nothing");
    }
}

void tables() {
    check(std::string(menu::kPresetNames[2]) == "Custom" && std::string(menu::kLoadingNames[1]) == "Fast", "preset and loading labels");
    check(std::size(menu::kMsaaValues) == std::size(menu::kMsaaNames), "MSAA values and names");
    check(std::size(menu::kPromptNames) == std::size(menu::kPromptFamilies) + 1, "prompt names: Automatic and every family");
    check(std::size(menu::kTextureMemoryValues) == std::size(menu::kTextureMemoryNames), "texture memory values and names");
    check(std::string(menu::kLabelModeNames[0]) == "Auto (two or more players)", "the PC's label");
    check(std::string(menu::kScreen2ModeNames[0]).rfind(menu::kScreen2ModeShort[0], 0) == 0, "the short screen2 name starts the long one");
}

void choice_arithmetic() {
    const std::vector<double> rates(std::begin(menu::kRateSteps), std::end(menu::kRateSteps));
    check(menu::step_value(rates, 100, 1) == 125 && menu::step_value(rates, 100, -1) == 75, "a step through the list");
    check(menu::step_value(rates, 1000, 1) == 1000 && menu::step_value(rates, 10, -1) == 10, "clamped at both ends");
    check(menu::step_value(rates, 110, 1) == 125 && menu::step_value(rates, 110, -1) == 100, "a foreign value snaps to its neighbours");
    check(menu::step_value(rates, 2000, 1) == 2000 && menu::step_value(rates, 2000, -1) == 1000, "a foreign value past the end");
    check(menu::step_value(rates, 5, -1) == 5 && menu::step_value(rates, 5, 1) == 10, "a foreign value before the start");
    check(menu::step_value(rates, 100, 5) == 500, "five steps");
    const auto placed = menu::with_foreign(rates, 110);
    check(placed.foreign && placed.index == 5 && placed.values.size() == rates.size() + 1 && placed.values[5] == 110, "the foreign value's place");
    const auto listed = menu::with_foreign(rates, 150);
    check(!listed.foreign && listed.index == 6 && listed.values.size() == rates.size(), "a listed value");
    // LOD bias: a float grid in 0.25 steps, exact.
    const auto lod = menu::grid(GraphicsOptions::kLodBiasMin, GraphicsOptions::kLodBiasMax, menu::kLodBiasStep);
    check(lod.size() == 17 && lod.front() == -2.0 && lod.back() == 2.0 && lod[8] == 0.0, "the LOD grid");
    check(menu::step_value(lod, 0.1, 1) == 0.25 && menu::step_value(lod, 0.1, -1) == 0.0, "a float off the grid snaps");
    check(std::abs(menu::step_value(lod, -0.25, 1)) < 1e-9, "0.25 steps stay exact");
    const auto volume = menu::grid(0, 100, 5);
    check(volume.size() == 21 && menu::step_value(volume, 33, 1) == 35, "volume steps of 5");
    check(menu::nearest_index(volume, 33) == 7 && menu::nearest_index(volume, 32.5) == 6, "the nearest grid value (ties: the lower)");
}
} // namespace

int main() {
    try {
        ops_match_direct_assignment();
        tables();
        choice_arithmetic();
    } catch (const std::exception& error) {
        std::cerr << "menu ops tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "menu ops tests passed (" << checks << " checks)\n";
    return 0;
}
