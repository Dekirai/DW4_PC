#pragma once

// Round I1b: the arrival prompt drawn over the game (docs/SEAMLESS.md 19.5.3):
// the gate form where the lobby gate's list was (over the frozen game), the
// loading-screen form over the own loading screen. Presentation only: the
// session holds the prompt and its cursor; its keys reach it through
// Session::prompt_input. English UI, the texts of 19.5.6.
#include "online/session.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace dw4::game {

// The pad buttons the prompt reads from the own pad (the D-pad bits and the
// main stick's directions as up / down / left / right).
std::uint32_t prompt_buttons(std::uint32_t buttons, std::uint32_t stick_directions, float stick_x, float stick_y);

// The texts of a row (tested in tools-free form by online_session's checks
// of the session; here only the layout): "<host>'s instance", " (party)",
// " (friend)", "<players>/4", the host's place, "Here (n)", the story label,
// the ping, and a state ("Full", "Boss fight", "Story not here yet", ...).
std::string prompt_row_text(const online::wire::OptionRow& row, std::string_view arrival_stage);

// Draws the prompt (nothing when none is open). `keyboard`: the last input
// was the keyboard (the key names of 19.5.7 in the hint line).
void draw_arrival_prompt(const online::PromptView& prompt, bool keyboard);

} // namespace dw4::game
