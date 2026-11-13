#pragma once

#include "prompt_layout.hpp"
#include "texture_pack.hpp"

#include "dw4/engine/scene_graph.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dw4::engine {
struct FontImage;
}

namespace dw4::game {

struct Viewport {
    int x{}, y{}, width{}, height{};
};

// OpenGL 3.3 renderer for engine draw lists. Requires a current context.
class TexturePack;

// The controller prompts (button_prompts.hpp, docs/ROUND_PADS_NOTES.md):
// the game font's button glyphs, the word START in its texts and the
// title's PRESS START drawn with each player's controller pictures
// (prompt_layout.hpp). Only the presentation changes: the engine's quads
// and texts stay what it made; a START picture narrower than the word
// closes its row up as the line is aligned.
class PromptSource {
public:
    virtual ~PromptSource() = default;
    // Any player's pictures differ from the game's own (else nothing is split).
    virtual bool active() const = 0;
    // A player's (0..3) picture atlas, 0 for the game's own pictures.
    virtual unsigned atlas_texture(int player) const = 0;
    // A control's cell in the atlas (the same in every family): u0, v top, u1, v bottom.
    virtual std::array<float, 4> atlas_cell(int control) const = 0;
    // A player's START picture for the word START: its cell trimmed to the
    // picture (u0, v top, u1, v bottom) and its width over the cell's height.
    struct Word {
        std::array<float, 4> uv{};
        float aspect = 0;
    };
    virtual Word atlas_word(int player) const = 0;
    // Changes with a player's family and its bindings (the title's variants are kept by player 1's).
    virtual std::uint64_t art_key(int player) const = 0;
    // A text line's player (0..3) and alignment (0 left, 1 right, 2 center).
    struct Line {
        int player = 0;
        int align = 0;
    };
    virtual Line line_info(const engine::GeometryData* geometry) const = 0;
    // The title's PRESS START texture for player 1's family (rgba top row
    // first, as decoded or replaced); false: keep it as it is.
    virtual bool title_variant(const std::vector<std::uint8_t>& rgba, int width, int height, bool nearest, std::vector<std::uint8_t>& out,
                               int& out_width, int& out_height) const = 0;
};

class GlRenderer {
public:
    GlRenderer();
    ~GlRenderer();
    // Texture dumps and replacements (null for none); clear_cache() reloads.
    void set_texture_pack(TexturePack* pack) { pack_ = pack; }
    // The controller prompts (null: the game's own pictures everywhere).
    void set_prompts(PromptSource* source) { prompts_ = source; }
    // The game font's glyph cells (once the engine loaded its font).
    void set_prompt_glyphs(const PromptGlyphs& glyphs);
    bool has_prompt_glyphs() const { return glyphs_.valid; }
    const PromptGlyphs& prompt_glyphs() const { return glyphs_; }
    // Translations (docs/L10N.md 2): the game font whose atlas gets the
    // composed glyph cells (engine::paint_composed_glyphs) when it uploads,
    // also into a texture pack's larger font00. Its glyphs are copied; the
    // atlas uploads again when another font image arrives.
    void set_font_atlas(const engine::FontImage& font);
    // The display language's pictures (docs/L10N.md 3), taken instead of the
    // USA textures of these hashes (a texture pack's replacement of a
    // language picture by its own hash still applies). The textures of the
    // old and the new set upload again; empty: the game's own pictures.
    void set_language_textures(LanguageTextures textures);
    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    // `screen` is the 4:3 game area inside the window (GL window coordinates).
    // `screen` is the original 4:3 picture; a wider `wide` area (widescreen
    // option) shows more of the 3D scene at the same vertical field of view
    // while the 2D layers stay on the 4:3 picture, except full-screen layers
    // (fades, backdrops), which stretch over it.
    void render(const std::vector<engine::DrawItem>& items, const engine::FrameCamera& camera,
                const engine::Vec4f& clear_color, const Viewport& screen, const Viewport& wide, int window_width,
                int window_height);
    // A movie picture (RGBA rows, top first) over the whole 4:3 screen.
    void draw_movie(const std::uint8_t* rgba, int width, int height, const Viewport& screen, int window_width,
                    int window_height);
    // The picture drawn so far this frame, kept for frames that present
    // nothing new (a stage switch), and that picture drawn again (false when
    // none was kept at this window size).
    void keep_picture(int window_width, int window_height);
    bool draw_kept_picture(int window_width, int window_height);
    // No picture is kept any more (another engine drew the last one: the
    // Data Overflow switches).
    void forget_kept_picture() { kept_width_ = kept_height_ = 0; }
    // The window depth (0..1) the last render left at a window pixel (GL
    // coordinates, origin bottom left); nullopt outside the window or when
    // the depth cannot be read. It waits for the picture to be drawn (the
    // overhead name tags' occlusion reads a few pixels per frame).
    std::optional<float> read_depth(int x, int y, int window_width, int window_height);
    // The projected shadow drawn with the next render (null: none).
    void set_shadow(const engine::ShadowFrame* shadow) { shadow_ = shadow; }
    // Drop all GPU resources (call when the engine changed stage).
    void clear_cache();
    // Dev tool (DW4_DEV_IDS, dev_present.hpp): render() draws every item in
    // a flat colour that encodes its position in the draw list (index + 1 in
    // R, G, B), without blending, multisampling or shadow receivers; the
    // alpha test still drops what the picture does not show.
    void set_id_pass(bool on) { id_pass_ = on; }
    std::size_t texture_count() const { return textures_.size(); }
    std::size_t mesh_count() const { return meshes_.size(); }
    struct Impl;

private:
    struct Mesh {
        unsigned vao{}, vbo{}, ibo{};
        int index_count{};
        bool has_colors{}, has_uvs{}, has_normals{};
        std::uint64_t serial{};       // GeometryData instance the buffers hold
        std::uint64_t last_frame{};   // eviction of unused runtime geometry
    };
    void release(Mesh& mesh);
    std::uint64_t frame_{};
    struct Texture {
        unsigned id{};
        bool alpha{};
        // The title's PRESS START texture (its decoded pixels for the
        // prompt variants, and whether it is magnified nearest).
        bool title_press = false;
        bool nearest = false;
        int width = 0, height = 0;
        std::vector<std::uint8_t> original;
        // The decoded texture's hash (TexturePack::hash; 0 when none was
        // needed): a language change uploads the ones it swaps again.
        std::uint64_t hash = 0;
    };
    // A text line's quads split for the prompts (prompt_layout.hpp): the
    // text without the button glyphs and words (moved where a word picture
    // closed its row up), and the pictures' quads in the atlas; its line's
    // player and alignment, and the START picture's width it was laid out
    // for (another family's: laid out again).
    struct PromptSplit {
        std::uint64_t serial{};
        unsigned text_vao{}, text_vbo{}, text_ibo{};
        int text_count{};
        unsigned icon_vao{}, icon_vbo{}, icon_ibo{};
        int icon_count{};
        int player = 0, align = 0;
        float word_aspect = 0;
        bool words = false;
        bool none{}; // no prompt quads: drawn as it is
    };
    const PromptSplit* prompt_split(const engine::GeometryData* geometry);
    void release(PromptSplit& split);
    unsigned title_texture(const Texture& texture);
    const Mesh* mesh(const engine::GeometryData* geometry);
    const Texture* texture(const engine::Attribute* binding);
    void render_shadow_map();
    void render_shadow_receivers(const engine::FrameCamera& camera);

    std::unique_ptr<Impl> gl_;
    unsigned program_{};
    std::unordered_map<const engine::GeometryData*, Mesh> meshes_;
    std::map<std::pair<const void*, std::uint32_t>, Texture> textures_;
    unsigned white_texture_{};
    const engine::ShadowFrame* shadow_{};
    unsigned movie_program_{}, movie_texture_{}, movie_vao_{};
    int movie_width_{}, movie_height_{};
    unsigned kept_program_{}, kept_texture_{};
    int kept_width_{}, kept_height_{};
    unsigned shadow_fbo_{}, shadow_texture_{};
    int shadow_width_{}, shadow_height_{};
    bool shadow_ready_{};
    TexturePack* pack_{};
    PromptSource* prompts_{};
    PromptGlyphs glyphs_;
    // Translations: the font atlas's image (document, reference) and the
    // font's glyphs the composed cells are painted from; the language's
    // pictures by USA hash.
    std::pair<const void*, std::uint32_t> atlas_key_{};
    std::unique_ptr<engine::FontImage> atlas_font_;
    LanguageTextures language_;
    bool language_hashing_ = false; // (uploads hash while a set with pictures is active)
    std::unordered_map<const engine::GeometryData*, PromptSplit> splits_;
    // The title's PRESS START variants by PromptSource::art_key, the latest
    // first (a few kept: switching families back and forth draws none again).
    std::vector<std::pair<std::uint64_t, unsigned>> title_variants_;
    bool id_pass_{};
};

} // namespace dw4::game
