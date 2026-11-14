#include "menu_ops.hpp"

namespace dw4::game::ops {

Effects& no_effects() {
    static Effects none;
    return none;
}

void set_ui_scale(Target t, int percent) {
    t.settings.ui_scale = percent;
    t.changed = true;
}

void set_widescreen(Target t, bool on) {
    t.settings.widescreen = on;
    t.changed = true;
}

void set_vsync(Target t, bool on) {
    t.settings.vsync = on;
    t.effects.swap_interval(on);
    t.changed = true;
}

void set_frame_limit(Target t, int fps) {
    t.settings.fps_limit = fps;
    t.changed = true;
    t.effects.frame_rate(t.settings);
}

void set_smooth_motion(Target t, bool on) {
    t.settings.smooth_motion = on;
    t.changed = true;
}

void set_msaa(Target t, int samples) {
    t.settings.msaa = samples;
    t.changed = true;
}

void set_graphics(Target t, GraphicsOptions options) {
    t.settings.graphics = options.clamped();
    t.effects.graphics(t.settings.graphics);
    t.changed = true;
}

void set_texture_pack_folder(Target t, const std::filesystem::path& folder, const std::function<void()>& reload) {
    t.settings.texture_pack = folder;
    t.changed = true;
    if (reload) reload();
}

void set_dump_textures(Target t, bool on, const std::function<void()>& reload) {
    t.settings.dump_textures = on;
    t.changed = true;
    if (reload) reload();
}

void set_volume(Target t, int percent) {
    t.settings.volume = percent;
    t.changed = true;
}

void set_label_mode(Target t, LabelMode mode) {
    t.settings.overhead_labels = label_mode_name(mode);
    t.changed = true;
}

void set_label_scale(Target t, int percent) {
    t.settings.label_scale = percent;
    t.changed = true;
}

void set_label_numbers(Target t, bool on) {
    t.settings.label_numbers = on;
    t.changed = true;
}

void set_achievements(Target t, bool on) {
    t.settings.achievements = on;
    t.changed = true;
}

void set_achievement_popups(Target t, bool on) {
    t.settings.achievement_popups = on;
    t.changed = true;
}

void set_net_indicator(Target t, bool on) {
    t.settings.net_indicator = on;
    t.changed = true;
}

void set_discord(Target t, bool on) {
    t.settings.discord = on;
    t.changed = true;
}

void set_prompts(Target t, std::optional<PadFamily> family) {
    t.settings.prompts = family ? family_id(*family) : "auto";
    t.changed = true;
}

void set_thor_style(Target t, ThorStyle style) {
    t.settings.thor_style = style;
    t.changed = true;
    t.effects.thor_style();
}

void set_nintendo_layout(Target t, NintendoLayout layout) {
    t.settings.nintendo_layout = layout;
    t.changed = true;
}

void set_menu_buttons(Target t, MenuButtons buttons) {
    t.settings.menu_buttons = buttons;
    t.changed = true;
}

void set_gamecube_layout(Target t, GameCubeLayout layout) {
    t.settings.gamecube_layout = layout;
    t.changed = true;
}

void set_wii_driver(Target t, WiiDriver driver) {
    t.settings.wii_driver = driver;
    t.changed = true;
}

void set_gc_adapter(Target t, bool on) {
    t.settings.gc_adapter = on;
    t.changed = true;
}

void clear_pad_binding(Target t, PadKey key, std::size_t slot) {
    if (std::size_t(key) >= t.settings.pad.size() || slot >= t.settings.pad[std::size_t(key)].size()) return;
    t.settings.pad[std::size_t(key)][slot] = {};
    t.changed = true;
}

void reset_pad_bindings(Target t) {
    t.settings.reset_pad();
    t.changed = true;
}

void set_screen2_mode(Target t, Screen2Mode mode) {
    t.settings.screen2_mode = mode;
    t.changed = true;
}

void set_screen2_overlay(Target t, bool on) {
    t.settings.screen2_overlay = on;
    t.changed = true;
}

void set_pause_in_menu(Target t, bool on) {
    t.settings.pause_in_menu = on;
    t.changed = true;
}

void set_pause_unfocused(Target t, bool on) {
    t.settings.pause_unfocused = on;
    t.changed = true;
}

void set_skip_intro(Target t, bool on) {
    t.settings.skip_intro = on;
    t.changed = true;
}

void set_mod_enabled(Target t, const std::string& id, bool on) {
    if (on) t.settings.disabled_mods.erase(id);
    else t.settings.disabled_mods.insert(id);
    t.changed = true;
}

void set_overflow_npc(Target t, bool on) {
    t.settings.overflow_npc = on;
    t.changed = true;
}

} // namespace dw4::game::ops
