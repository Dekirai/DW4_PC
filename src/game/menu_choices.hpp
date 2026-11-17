#pragma once

// The F1 menu's option lists (docs/CONSOLE_MENU.md): one table per setting
// with the labels both menus show, byte for byte. The PC's classic F1 menu
// (game_main.cpp's draw_f1_menu, host_view.cpp's settings sections) and
// Android's console menu (console_pages.cpp) read the same arrays, so a
// label or a value never drifts between the two presentations.
//
// No ImGui, no SDL: the tables and the arithmetic of the console menu's
// choice and stepper rows (a value settings.ini holds that is none of the
// listed ones) are unit-tested (tests/menu_ops_tests.cpp).
#include "pad_family.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace dw4::game::menu {

// ---- Settings tab (game_main.cpp)
inline constexpr const char* kPresetNames[] = {"Original", "Modernized", "Custom"};
inline constexpr const char* kLoadingNames[] = {"Original", "Fast"};

// ---- Display (host_view.cpp settings_display_section)
inline constexpr int kMsaaValues[] = {0, 2, 4, 8};
inline constexpr const char* kMsaaNames[] = {"Off", "2x", "4x", "8x"};
// The console menu's frame limits (a slider of 0..300 on the PC): off, 30,
// 60 and 120 fps, what the AYN Thor's display offers (60 / 120 Hz).
inline constexpr int kFrameLimitsAndroid[] = {0, 30, 60, 120};

// ---- Graphics
inline constexpr int kTextureMemoryValues[] = {512, 1024, 2048, 4096};
inline constexpr const char* kTextureMemoryNames[] = {"512 MiB", "1 GiB", "2 GiB", "4 GiB"};
inline constexpr float kLodBiasStep = 0.25f;

// ---- Name tags (LabelMode order: automatic, all, others, off)
inline constexpr const char* kLabelModeNames[] = {"Auto (two or more players)", "All players", "Other players", "Off"};

// ---- Controls (host_view.cpp settings_controllers_section)
inline constexpr PadFamily kPromptFamilies[] = {PadFamily::ps5,        PadFamily::ps4,         PadFamily::xbox360,  PadFamily::xbox_one,
                                                PadFamily::switch_pro, PadFamily::switch2_pro, PadFamily::wiiu_pro, PadFamily::gamecube,
                                                PadFamily::thor,       PadFamily::keyboard};
// "Automatic" first, then kPromptFamilies' names.
inline constexpr const char* kPromptNames[] = {"Automatic", "DualSense", "DualShock 4", "Xbox 360",  "Xbox One/Series", "Switch Pro",
                                               "Switch 2 Pro", "Wii U Pro", "GameCube", "AYN Thor", "Keyboard"};
inline constexpr const char* kLayoutNames[] = {"By label (the A button is A)", "By position (like an Xbox controller)"};
// (The first entry, "As the Thor's setting (...)", is made with the device's style.)
inline constexpr const char* kThorStyleNames[] = {"Standard", "Xbox"};
inline constexpr const char* kGameCubeNames[] = {"Original (every button is its own)", "Use the bindings below"};
inline constexpr const char* kMenuButtonNames[] = {"The controller's own A / B", "The game's A / B bindings"};
inline constexpr const char* kWiiNames[] = {"Automatic (while one is connected)", "On", "Off"};

// ---- Cheats
inline constexpr const char* kEnemyModeNames[] = {"Off", "Every enemy on its own", "One family per wave"};
inline constexpr const char* kZoneModeNames[] = {"Off", "Dungeon entrances within an area"};
// The console menu's rebalancing steps (sliders of 10..1000 % on the PC).
inline constexpr int kRateSteps[] = {10, 25, 50, 75, 100, 125, 150, 200, 300, 500, 750, 1000};

// ---- Second screen (screen2.cpp settings_screen2_section)
inline constexpr const char* kScreen2ModeNames[] = {"Automatic (a second screen shows the panels)", "Off"};
// (The console menu's short names of the same two values.)
inline constexpr const char* kScreen2ModeShort[] = {"Automatic", "Off"};

// ---- Grids of the console menu's steppers
// Volume 0..100 % in steps of 5, name tag size 50..200 % in steps of 10,
// the mip LOD bias kLodBiasMin..kLodBiasMax in steps of 0.25.
std::vector<double> grid(double first, double last, double step);

// ---- Choice and stepper arithmetic
// A row's values in order with `current` among them: the listed values, and
// a value none of them is (settings.ini's own) inserted at its sorted place
// (`values` ascending). `index` is current's place.
struct Placed {
    std::vector<double> values;
    std::size_t index = 0;
    bool foreign = false; // current is none of the listed values
};
Placed with_foreign(const std::vector<double>& values, double current);
// One step from `current` (-1 or +1, or a larger count) through the listed
// values, clamped at both ends: a foreign value snaps to its neighbour in
// that direction at the first step.
double step_value(const std::vector<double>& values, double current, int steps);
// The listed value nearest `current` (ties: the lower one).
std::size_t nearest_index(const std::vector<double>& values, double current);

} // namespace dw4::game::menu
