#pragma once

// The controller pictures of the prompts (docs/ROUND_PADS_NOTES.md 3): our
// own vector drawings (circles, rounded rectangles, strokes and a small
// stroke font of our own), rasterized with signed distances into RGBA
// pictures, one style per family. No console or company logos: the
// pictures show the functional symbols printed on the buttons (letters,
// the PlayStation shapes, +/-, sticks, the D-pad) and key caps.
//
// Plain CPU code without SDL devices, OpenGL or ImGui (testable): the
// host's button_prompts makes textures of what this draws.
#include "pad_family.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dw4::game {

// An RGBA8 picture, rows top first, straight alpha.
struct ArtImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    ArtImage() = default;
    ArtImage(int w, int h) : width(w), height(h), rgba(std::size_t(w) * std::size_t(h) * 4, 0) {}
};

// One family's prompt pictures: a cell per PromptControl in the shape of
// the game font's glyph cells (20 x 27 texels; the button sits in rows
// 5..25 like the original glyphs, here kCellWidth x kCellHeight pixels), and
// a wide cell for "START" written as a word (the game's texts, the title).
struct PromptAtlas {
    static constexpr int kCellWidth = 96, kCellHeight = 130, kGutter = 8, kColumns = 5;
    // The square picture inside a cell (the original glyphs' ink rows 5..25
    // of 27), in cell pixels.
    static constexpr int kIconTop = 24, kIconSize = 96;
    static constexpr int kWideCells = 4; // the word "START": four cells wide
    struct Cell {
        int x = 0, y = 0, width = 0, height = 0;
    };
    ArtImage image;
    std::array<Cell, kPromptControlCount> cells{};
    Cell start_word{};
    // The square picture of a control's cell (the ImGui prompts use it).
    Cell icon(PromptControl control) const;
};

// The atlas of a family with these bindings (the keyboard's key names, the
// pad's bound buttons). Same input, same pixels.
PromptAtlas build_prompt_atlas(PadFamily family, const Settings& settings);

// One prompt into a rectangle of an image (pixels, top-left origin). `wide`:
// a slot the width of a word (key caps grow into it, round buttons stay
// round and centred); `words`: large enough for the names printed on a pill
// (OPTIONS, START), else the pill's symbol.
void draw_prompt(ArtImage& image, float x0, float y0, float x1, float y1, PadFamily family, const PromptFace& face, bool wide = false,
                 bool words = false);

// The title's "PRESS START" texture (mojigc.png, 256 x 128, stored upside
// down) with the word START replaced by the family's START picture, `scale`
// times the size (the original's other rows scaled alike: nearest or
// bilinear, as the texture is sampled). Null without the expected layout.
inline constexpr std::uint64_t kTitlePressStartHash = 0xac566f5a15c2a226ull; // TexturePack::hash of the USA mojigc.png
bool title_press_start(const ArtImage& original, int scale, bool nearest, PadFamily family, const Settings& settings, ArtImage& out);

// A key's short cap label ("Return" -> "ENTER", "Keypad 8" -> "8",
// "Left Shift" -> "SHIFT"); arrows come back as "^", "v", "<", ">".
std::string key_cap_label(const std::string& key_name);

} // namespace dw4::game
