#pragma once

// Data Overflow's run screens in the game's look (docs/SURVIVAL.md 9.3-9.6,
// 4.4; round O1b lane D): the HUD overlay (the run timer, the EXP bar,
// Defeated and Bits, the build row, the Guardian bar), the level-up window,
// pause with the build page and Retire, the results and the finale's quest
// timer. They replace O1a's plain overlay boxes. The driver fills a
// RunScreenData from the director every pass (fill in overflow_driver.cpp);
// the screens draw it into SimUi's 2D items.
#include "sim_ui.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game::overflow {

enum class RunScreen { none, level_up, pause, build, retire_ask, results };

struct RunOffer {
    std::string text, detail, about;
};

struct RunScreenData {
    // HUD (9.3).
    double seconds = 0;          // the run clock
    int level = 1;
    float exp_fraction = 0;      // to the next level
    int defeats = 0;
    int bits = 0;                // Sim Bits of the run so far
    std::string weapon;          // the Art's weapon (its value colour by its grade)
    float weapon_fraction = 0;   // to the next rung
    std::vector<std::string> techniques; // six glyphs with their rank ("⊂P", "∩M", "-" locked)
    std::vector<bool> learned;
    std::string gear;            // the ∋ / ∨ / ∧ glyphs of the gear
    std::string notice;          // the last visible change (3 s)
    // The finale (4.4): LordKnightmon is in, the quest timer counts down.
    bool finale = false;
    double finale_left = 0;      // seconds
    bool time_up = false;
    // A Guardian's bar where the arena has none.
    std::optional<std::string> guardian;
    int guardian_hp = 0, guardian_max = 1;
    // Menus.
    RunScreen screen = RunScreen::none;
    int cursor = 0;
    std::vector<RunOffer> offers;
    std::vector<std::string> build_lines, result_lines;
    bool cleared = false;
    std::uint64_t screen_frames = 0; // frames the screen is open (the results' lines appear one by one)
};

// The HUD's build row from the director's build page (until lane E's
// getters): the weapon after "Art: <Art> with ", the skill / cap of the
// class line as the weapon bar, each technique line ("  Fire: Petty (...)",
// "  Heal Lt: not learned (...)") as its element glyph or initial with its
// rank letter (P, M, G, T, ∞).
void build_row(const std::vector<std::string>& build_lines, RunScreenData& data);

// The run's 2D items of one frame (`frame`: the presented step, blinking).
void draw_run_screens(SimUi& ui, const RunScreenData& data, std::uint64_t frame);
// The countdown in the quest timer's look: "m:ss" (finale_left).
std::string quest_timer_text(double seconds_left);
// The value colour of a weapon by its grade glyph (α white .. ε magenta).
UiColor grade_color(std::string_view name);

} // namespace dw4::game::overflow
