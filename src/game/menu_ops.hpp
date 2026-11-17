#pragma once

// The F1 menu's one apply layer (docs/CONSOLE_MENU.md): every setting both
// menus change goes through one of these, so the PC's classic F1 menu and
// Android's console menu apply and save a setting the same way. Each is the
// code the classic menu ran inline (the settings.ini field, the side effect
// it had: the swap interval, the display's rate, the renderer's graphics,
// the Thor's controller style, the texture pack's reload) plus
// settings_changed; the file is written when the menu closes, as always
// (host_view.cpp toggle_menu). Nothing here writes a log line or draws.
//
// The side effects outside Settings go through Effects (host_view.cpp gives
// the window's; tests/menu_ops_tests.cpp a recording fake).
#include "overhead_labels.hpp"
#include "pad_family.hpp"
#include "settings.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace dw4::game::ops {

struct Effects {
    virtual ~Effects() = default;
    virtual void swap_interval(bool vsync) { (void)vsync; }             // SDL_GL_SetSwapInterval
    virtual void frame_rate(const Settings& settings) { (void)settings; } // request_frame_rate (Android's 60 / 120 Hz)
    virtual void graphics(const GraphicsOptions& options) { (void)options; } // GlRenderer::set_graphics
    virtual void thor_style() {}                                         // the Thor's pads read with the new style
};

// Ops without side effects beyond Settings (the second screen's section).
Effects& no_effects();

// What an op changes: the settings, their "changed" flag (HostView's
// settings_changed) and the side effects.
struct Target {
    Settings& settings;
    bool& changed;
    Effects& effects;
};

// ---- Display
void set_ui_scale(Target t, int percent); // 0 automatic, else kUiScaleChoices' percent
void set_widescreen(Target t, bool on);
void set_vsync(Target t, bool on);
void set_frame_limit(Target t, int fps);  // 0 off
void set_smooth_motion(Target t, bool on);
void set_msaa(Target t, int samples);     // at the next start
// ---- Graphics: the options clamped, the renderer's, settings.ini's
void set_graphics(Target t, GraphicsOptions options);
// The pack folder (empty: mods/textures beside the program), then `reload`.
void set_texture_pack_folder(Target t, const std::filesystem::path& folder, const std::function<void()>& reload);
void set_dump_textures(Target t, bool on, const std::function<void()>& reload);
// ---- Sound, name tags, achievements, the network indicator, Discord
void set_volume(Target t, int percent);
void set_label_mode(Target t, LabelMode mode);
void set_label_scale(Target t, int percent);
void set_label_numbers(Target t, bool on);
void set_achievements(Target t, bool on);
void set_achievement_popups(Target t, bool on);
void set_net_indicator(Target t, bool on);
void set_discord(Target t, bool on);
// ---- Controls
void set_prompts(Target t, std::optional<PadFamily> family); // nullopt: automatic
void set_thor_style(Target t, ThorStyle style);
void set_nintendo_layout(Target t, NintendoLayout layout);
void set_menu_buttons(Target t, MenuButtons buttons);
void set_gamecube_layout(Target t, GameCubeLayout layout);
void set_wii_driver(Target t, WiiDriver driver);
void set_gc_adapter(Target t, bool on);
void clear_pad_binding(Target t, PadKey key, std::size_t slot);
void reset_pad_bindings(Target t);
// ---- Second screen
void set_screen2_mode(Target t, Screen2Mode mode);
void set_screen2_overlay(Target t, bool on);
// ---- The menu, the start, mods
void set_pause_in_menu(Target t, bool on);
void set_pause_unfocused(Target t, bool on);
void set_skip_intro(Target t, bool on);
void set_mod_enabled(Target t, const std::string& id, bool on); // at the next start
void set_overflow_npc(Target t, bool on);

} // namespace dw4::game::ops
