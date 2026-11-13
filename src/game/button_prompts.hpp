#pragma once

// The controller prompts on screen (docs/ROUND_PADS_NOTES.md): the picture
// atlases (button_art.hpp) of the families on screen as OpenGL textures, each
// player's family for the game's own pictures (GlRenderer's PromptSource:
// the font's button glyphs, the word START, the title's PRESS START) and
// player 1's for the port's ImGui texts, where "{A}", "{START}", "{MENU}"
// ... stand for the pictures.
//
// Presentation only: switching a family swaps a texture binding in the
// next frame; nothing reaches the engine.
#include "button_art.hpp"
#include "gl_renderer.hpp"
#include "pad_family.hpp"

#include "dw4/engine/managers.hpp"

#include <array>
#include <cstdint>
#include <functional>
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

    // The settings the pictures are drawn for (a binding or layout change
    // marks every family's atlas stale; nothing is drawn here).
    void update(const Settings& settings);
    // The families the players' pictures show (players 0..3; the game's own
    // GameCube pictures stay for a GameCube pad on its original layout), and
    // the atlases they need drawn and uploaded (a current OpenGL context):
    // only the families on screen are drawn, once per binding change.
    void set_families(const std::array<PadFamily, 4>& families);
    // The GL context was lost (Android): the pictures are uploaded again.
    void reset_context() {
        textures_ = {};
        uploaded_ = {};
    }
    PadFamily family(int player = 0) const { return families_[std::size_t(player & 3)]; }
    const Settings* settings() const { return settings_; }
    // A family's atlas texture (0 until it was on screen) and a control's
    // picture in it (u0, v0, u1, v1, rows top first).
    unsigned texture(PadFamily family) const { return textures_[std::size_t(family)]; }
    std::array<float, 4> icon_uv(PromptControl control) const;
    bool ready() const { return texture(families_[0]) != 0; }
    // How a text line finds its line (TextCtlManager::line_of: its player
    // and alignment); none: every line is player 1's and left aligned.
    void set_line_lookup(std::function<PromptSource::Line(const engine::GeometryData*)> lookup) { line_lookup_ = std::move(lookup); }
    // Atlases drawn so far (the F1 menu's rebinding draws only those on screen).
    std::size_t draws() const { return cache_.draws(); }

    // PromptSource (GlRenderer).
    bool active() const override;
    unsigned atlas_texture(int player) const override;
    std::array<float, 4> atlas_cell(int control) const override;
    Word atlas_word(int player) const override;
    std::uint64_t art_key(int player) const override;
    Line line_info(const engine::GeometryData* geometry) const override;
    bool title_variant(const std::vector<std::uint8_t>& rgba, int width, int height, bool nearest, std::vector<std::uint8_t>& out,
                       int& out_width, int& out_height) const override;

    // The instance the ImGui helpers below draw with (one per process).
    static ButtonPrompts* active_prompts();
    static void set_active(ButtonPrompts* prompts);

private:
    PromptAtlasCache cache_;
    std::array<unsigned, kPadFamilyCount> textures_{};
    std::array<std::uint64_t, kPadFamilyCount> uploaded_{}; // the cache generation each texture holds
    std::array<PromptAtlas, kPadFamilyCount> cells_{};      // the cells (no picture) of each uploaded atlas
    std::array<PadFamily, 4> families_{PadFamily::gamecube, PadFamily::gamecube, PadFamily::gamecube, PadFamily::gamecube};
    const Settings* settings_ = nullptr;
    std::function<PromptSource::Line(const engine::GeometryData*)> line_lookup_;
};

// ---- ImGui: texts with prompt pictures
// "{A} Select   {B} Back": the tokens are pad_family.hpp's prompt control
// tokens (A B X Y START L R Z STICK CSTICK DPAD MENU OK BACK DEL) in braces;
// each is drawn as player 1's family's picture of that control, a little
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

// (prompt_glyphs: prompt_layout.hpp.)

} // namespace dw4::game
