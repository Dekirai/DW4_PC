#pragma once

// The controller prompts on screen (docs/ROUND_PADS_NOTES.md): one picture
// atlas per family (button_art.hpp) as OpenGL textures, the active family
// for the game's own pictures (GlRenderer's PromptSource: the font's button
// glyphs, the word START, the title's PRESS START) and the port's ImGui
// texts, where "{A}", "{START}", "{MENU}" ... stand for the pictures.
//
// Presentation only: switching the family swaps a texture binding in the
// next frame; nothing reaches the engine.
#include "button_art.hpp"
#include "gl_renderer.hpp"
#include "pad_family.hpp"

#include "dw4/engine/managers.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct ImDrawList;

namespace dw4::game {

class ButtonPrompts final : public PromptSource {
public:
    ButtonPrompts() = default;
    ~ButtonPrompts() override;
    ButtonPrompts(const ButtonPrompts&) = delete;
    ButtonPrompts& operator=(const ButtonPrompts&) = delete;

    // Every family's atlas for these settings (a current OpenGL context;
    // again when the bindings or layouts change: update() notices).
    void update(const Settings& settings);
    // The family the pictures show (the game's own GameCube pictures stay
    // for a GameCube pad on its original layout).
    void set_family(PadFamily family) { family_ = family; }
    PadFamily family() const { return family_; }
    const Settings* settings() const { return settings_; }
    // The family's atlas texture and a control's picture in it (u0, v0,
    // u1, v1, rows top first).
    unsigned texture(PadFamily family) const { return textures_[std::size_t(family)]; }
    std::array<float, 4> icon_uv(PromptControl control) const;
    bool ready() const { return built_; }

    // PromptSource (GlRenderer).
    unsigned atlas_texture() const override;
    std::array<float, 4> atlas_cell(int control) const override;
    std::array<float, 4> atlas_word() const override;
    std::uint64_t art_key() const override;
    bool title_variant(const std::vector<std::uint8_t>& rgba, int width, int height, bool nearest, std::vector<std::uint8_t>& out,
                       int& out_width, int& out_height) const override;

    // The instance the ImGui helpers below draw with (one per process).
    static ButtonPrompts* active();
    static void set_active(ButtonPrompts* prompts);

private:
    std::uint64_t signature(const Settings& settings) const;
    std::array<unsigned, kPadFamilyCount> textures_{};
    PromptAtlas layout_; // (the cells; the same for every family)
    PadFamily family_ = PadFamily::gamecube;
    const Settings* settings_ = nullptr;
    std::uint64_t signature_ = 0;
    std::uint64_t generation_ = 0;
    bool built_ = false;
};

// ---- ImGui: texts with prompt pictures
// "{A} Select   {B} Back": the tokens are pad_family.hpp's prompt control
// tokens (A B X Y START L R Z STICK CSTICK DPAD MENU OK BACK DEL) in braces;
// each is drawn as the active family's picture of that control, a little
// taller than the text. Without prompts (no window) the token's name shows.
// `wrap`: at the window's content width. `color`: 0 for the text colour.
void prompt_text(std::string_view text, bool wrap = false, std::uint32_t color = 0);
void prompt_text_disabled(std::string_view text, bool wrap = false);
// A text's width as prompt_text draws it on one line.
float prompt_text_width(std::string_view text);
// The text with the tokens as plain names ("{START}" -> "START"), for logs.
std::string prompt_plain(std::string_view text);
// One picture at the cursor (`size` px square), on the line like an item.
void prompt_icon(PromptControl control, float size);
// One picture into a draw list (for custom drawn rows).
void prompt_icon(ImDrawList* draw, PromptControl control, float x, float y, float size);

// The game font's button glyphs (bectext.igb's font00.png: the GameCube
// button pictures the texts address with these characters), its letters
// S T A R and the START glyph, for GlRenderer::set_prompt_glyphs.
PromptGlyphs prompt_glyphs(const engine::FontImage& font);

} // namespace dw4::game
