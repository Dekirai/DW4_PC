#pragma once

// The title menu's third entry "Online" in the picture (docs/SEAMLESS.md 10).
// The engine's TITLE2 menu (title.cpp) has the cursor and the choice; its
// model j_startmenu02 knows only "Game Start" and "Option", so this draws the
// third row from the model's own pieces, in the game's own pipeline:
//
// - "Online" is a copy of the "Option" draw item whose quads are glyphs cut
//   from the same texture row (O and n of "Option", i of "Option", e of "Game
//   Start", the l from the i's stem drawn up to the dot's top), one row
//   (the distance from "Game Start" to "Option") below "Option";
// - the band behind the rows reaches one row further down;
// - the arrows (the model's own, with their idle animation) slide a row down
//   while the cursor is on the third entry, as the model slides them between
//   the first two (linear, 2.4 units a frame);
// - the logo moves up and the copyright down while the menu is open (eased
//   in and out with it), so the longer band fits between them; the press
//   start screen keeps the original layout.
//
// Everything else follows the model: the new pieces copy the render state and
// the matrices of the model's items, so the open and close animations, the
// colours and the fades apply to them too.
//
// Presentation only: it edits the frame's draw list after Engine::render and
// never touches the engine. Only while the engine's title_online_entry is set.
#include "dw4/engine/scene_graph.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dw4::game {

class TitleEntry {
public:
    // `menu_open`: TITLE2 shows the Game Start / Option menu (state 2);
    // `cursor_online`: its cursor is on the third entry; `frame`: the engine's
    // frame (the slides and the layout move per frame, as the model's own
    // animations do). Returns whether the third row was drawn.
    bool apply(std::vector<engine::DrawItem>& items, bool menu_open, bool cursor_online, std::uint64_t frame);
    // The texture had no usable glyphs (other data, a different layout):
    // the host leaves the entry out then, so the cursor never reaches an
    // entry that cannot be drawn.
    bool failed() const { return text_failed_; }
    // DW4_TRACE_TITLE_ENTRY=1 (test runs): the 2D items (texture, place, vertices).
    static void trace(const std::vector<engine::DrawItem>& items, std::uint64_t frame);

    // The layout in the 2D layer's units (640 x 480, y up; one row is 50):
    // the logo's lift, the copyright's drop and the menu block's lift while
    // the menu is open.
    static constexpr float kLogoLift = 30.0f, kCopyrightDrop = 16.0f, kMenuLift = 33.0f;

private:
    std::shared_ptr<engine::GeometryData> text_;        // the "Online" quads
    const engine::GeometryData* text_from_ = nullptr;   // the "Option" geometry they were cut for
    std::uint64_t text_serial_ = 0;
    bool text_failed_ = false;                          // the texture has no usable glyphs
    std::unordered_map<std::uint64_t, std::shared_ptr<engine::GeometryData>> band_; // the band one row longer (by source serial)
    float layout_ = 0;                                  // 0 press start .. 1 menu (eased)
    float arrows_ = 0;                                  // 0 at Option .. 1 at Online
    std::uint64_t last_ = 0;                            // the last frame
};

} // namespace dw4::game
