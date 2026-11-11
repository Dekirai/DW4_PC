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
    // The START picture inside start_word: the cell's rows, the columns its
    // ink takes and a little margin (a round button is as wide as it is
    // tall, a pill or key cap wider). The word START in a text becomes this
    // wide (prompt_layout.hpp).
    Cell start_word_ink{};
    // The square picture of a control's cell (the ImGui prompts use it).
    Cell icon(PromptControl control) const;
    // start_word_ink's width over the cell's height.
    float word_aspect() const { return start_word_ink.height > 0 ? float(start_word_ink.width) / float(start_word_ink.height) : 0.0f; }
};

// The atlas of a family with these bindings (the keyboard's key names, the
// pad's bound buttons). Same input, same pixels.
PromptAtlas build_prompt_atlas(PadFamily family, const Settings& settings);

// The families' atlases for one set of settings, drawn when first asked for:
// a change of the bindings or layouts marks every family stale, and only the
// families on screen are drawn again (about 7 ms each).
class PromptAtlasCache {
public:
    // The settings the atlases are drawn for (kept by reference: they must
    // outlive the cache); a change of their bindings or layouts (signature)
    // marks every family stale.
    void set_settings(const Settings& settings);
    // A family's atlas, drawn first when stale (the others stay as they are).
    const PromptAtlas& atlas(PadFamily family);
    // The family's atlas is drawn for the current settings.
    bool current(PadFamily family) const;
    // Changes whenever the family's atlas is drawn again.
    std::uint64_t generation(PadFamily family) const { return generation_[std::size_t(family)]; }
    // The picture of a drawn atlas is dropped (its cells stay; uploaded).
    void drop_image(PadFamily family) { atlases_[std::size_t(family)].image = {}; }
    // Atlases drawn so far.
    std::size_t draws() const { return draws_; }
    static std::uint64_t signature(const Settings& settings);

private:
    const Settings* settings_ = nullptr;
    std::uint64_t signature_ = 0;
    std::array<PromptAtlas, kPadFamilyCount> atlases_{};
    std::array<std::uint64_t, kPadFamilyCount> drawn_for_{}; // the signature each was drawn for (0: never)
    std::array<std::uint64_t, kPadFamilyCount> generation_{};
    std::size_t draws_ = 0;
};

// One prompt into a rectangle of an image (pixels, top-left origin). `wide`:
// a slot the width of a word (key caps and the DualShock 4's OPTIONS pill
// grow into it, round buttons stay round and centred).
void draw_prompt(ArtImage& image, float x0, float y0, float x1, float y1, PadFamily family, const PromptFace& face, bool wide = false);

// The title's "PRESS START" texture (mojigc.png: 256 x 128, stored upside
// down, or a texture pack's replacement of it in the same 2:1 layout) with
// the word START replaced by the family's START picture, at least
// `min_width` texels wide (the original's other rows scaled alike: nearest
// or bilinear, as the texture is sampled). False without the expected
// layout: a replacement whose START does not stand alone where the
// original's does (ink across the word's borders, none inside) stays as it is.
inline constexpr std::uint64_t kTitlePressStartHash = 0xac566f5a15c2a226ull; // TexturePack::hash of the USA mojigc.png
bool title_press_start(const ArtImage& original, int min_width, bool nearest, PadFamily family, const Settings& settings, ArtImage& out);

// A key's short cap label ("Return" -> "ENTER", "Keypad 8" -> "8",
// "Left Shift" -> "SHIFT"); arrows come back as "^", "v", "<", ">".
std::string key_cap_label(const std::string& key_name);

} // namespace dw4::game
