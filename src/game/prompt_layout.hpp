#pragma once

// The controller prompts inside the game's own text lines
// (docs/ROUND_PADS_NOTES.md): which quads of a line layout_text (ui.cpp)
// made are the font's GameCube button glyphs and which five spell the word
// START, and where the pictures and the remaining letters go. Plain CPU code
// without OpenGL (testable); GlRenderer draws the result at draw time only,
// nothing reaches the engine's geometry.
#include "dw4/engine/scene_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dw4::engine {
struct FontImage;
}

namespace dw4::game {

struct PromptGlyphCell {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // the glyph's texel rectangle in the font texture
    int control = -1;                     // PromptControl
};
struct PromptGlyphs {
    const void* document = nullptr; // the font image (its texture cache key)
    std::uint32_t reference = 0;
    std::vector<PromptGlyphCell> buttons;     // the button glyphs
    std::array<PromptGlyphCell, 4> letters{}; // S, T, A, R (the word START)
    PromptGlyphCell start_glyph;              // the START button glyph (a word next to it stays a word)
    PromptGlyphCell space;                    // the space (it has a quad: words end at it)
    std::vector<PromptGlyphCell> word_chars;  // letters and digits: a START touching one is part of a longer word
    bool valid = false;
};

struct PromptLayout {
    // A text quad that stays text, and its move along x (the row of a word
    // picture narrower or wider than the word).
    struct Text {
        std::size_t quad = 0;
        float shift = 0;
    };
    // A picture over a quad's place (control: a PromptControl) or the
    // word START's (control -1), in the text's coordinates (y up).
    struct Picture {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0, z = 0;
        int control = -1;
    };
    std::vector<Text> text;
    std::vector<Picture> pictures;
    int words = 0; // START words made pictures
};

// The prompts of one laid-out line. `word_aspect`: the width of the active
// family's START picture over its glyph cell's height (the word's picture
// takes that width and the rest of its row closes up or makes room as the
// line's `align` says: 0 left keeps the start, 1 right keeps the end, 2
// center keeps the middle); 0 leaves the word START as text. A line that is
// not one of layout_text's (four vertices and six indices per quad) has no
// prompts.
PromptLayout layout_prompts(const engine::GeometryData& geometry, const PromptGlyphs& glyphs, float word_aspect, int align);

// The game font's button glyphs (bectext.igb's font00.png: the GameCube
// button pictures the texts address with these characters), its letters
// S T A R, the START glyph, the space and the letters and digits that make
// a word longer (a START touching one stays text), for
// GlRenderer::set_prompt_glyphs.
PromptGlyphs prompt_glyphs(const engine::FontImage& font);

} // namespace dw4::game
