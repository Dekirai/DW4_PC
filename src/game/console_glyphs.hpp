#pragma once

// The console F1 menu's button pictures (docs/CONSOLE_MENU.md): which
// picture each of its hints shows (console_face, a pure function the tests
// check for every family, both menu_buttons modes and both platforms) and a
// cache of the pictures as small textures of their own, drawn with
// button_art.hpp's draw_prompt. The game's prompt atlases (button_prompts.hpp,
// PromptControl) are not touched.
#include "pad_family.hpp"
#include "settings.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

struct ImDrawList;

namespace dw4::game {

enum class MenuPlatform : std::uint8_t;

enum class ConsoleGlyph : std::uint8_t {
    ok,    // {OK}: confirm (the menus' A)
    back,  // {BACK}
    del,   // {DEL}: a binding's clear
    prev,  // {PREV}: L1, the category before
    next,  // {NEXT}: R1
    l2,    // a page up (menu_buttons=layout only)
    r2,    // a page down
    start, // Start
    close, // {CLOSE}: what opens and closes F1 (the Thor on Android: its Back key)
    lr,    // {LR}: the D-pad's left and right
    ud,    // {UD}: the D-pad's up and down
};

// The picture(s) of a glyph for a family (two for LR / UD; none when the
// family has nothing for it, as L2 / R2 with menu_buttons=bindings).
std::vector<PromptFace> console_faces(ConsoleGlyph glyph, PadFamily family, const Settings& settings, MenuPlatform platform);
// The first of them (Face::none and no key when there is none).
PromptFace console_face(ConsoleGlyph glyph, PadFamily family, const Settings& settings, MenuPlatform platform);

// The pictures as OpenGL textures (a current context; the lower screen's
// shares the window's objects). A lost context drops them (reset).
class GlyphCache {
public:
    ~GlyphCache();
    // A picture's texture, drawn and uploaded at its first use.
    unsigned texture(PadFamily family, const PromptFace& face);
    void reset(); // (the GL context was made again: the old names are gone)
    std::size_t size() const { return textures_.size(); }

private:
    std::map<std::tuple<int, int, std::string>, unsigned> textures_;
};
GlyphCache& console_glyphs();
// One picture into a draw list, `size` px square.
void draw_console_glyph(ImDrawList* draw, PadFamily family, const PromptFace& face, float x, float y, float size, std::uint32_t tint = 0xFFFFFFFFu);

} // namespace dw4::game
