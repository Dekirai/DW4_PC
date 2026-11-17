#pragma once

#include "graphics_options.hpp"
#include "prompt_layout.hpp"
#include "texture_pack.hpp"

#include "dw4/engine/scene_graph.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw4 {
class IgbDocument;
}

namespace dw4::engine {
struct FontImage;
}

namespace dw4::game {

class TextureStreamer;
struct TexturePayload;

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

// The current context is OpenGL ES (the Android port) rather than desktop
// OpenGL (its GL_VERSION string).
bool gl_is_es();

class GlRenderer {
public:
    GlRenderer();
    ~GlRenderer();
    // Texture dumps and replacements (null for none). Setting it again
    // (after TexturePack::configure) drops the pack's textures and streams
    // from the pack's new index.
    void set_texture_pack(TexturePack* pack);
    // The Graphics options (graphics_options.hpp), applied from the next
    // frame: samplers, the game's own mipmaps, the pack level, the memory
    // budget, sharpening and the shadow map size.
    void set_graphics(const GraphicsOptions& options);
    const GraphicsOptions& graphics() const { return graphics_; }
    // Clears the GL error flags; with DW4_GL_CHECK=1 an error raised since
    // the last check is logged ("renderer: GL error 0x... <where> (frame
    // N)", the first 20) and counted (the total when the renderer ends).
    // Called before texture uploads and, while checking_gl(), at the end of
    // every presented frame (no glGetError per frame otherwise).
    void check_gl_errors(const char* where);
    bool checking_gl() const;
    // Once per pass, before drawing (note_stage): the documents the engine
    // holds (null: the same as last time; new ones are scanned and their
    // pack textures read ahead, while a stage loads), the uploads of
    // replacements read meanwhile within the pass's time budget, and the
    // texture memory budget's evictions.
    void stream_textures(const std::vector<std::shared_ptr<const IgbDocument>>* documents);
    // What the GPU offers for packs and filtering.
    struct Caps {
        bool bc1{}, bc3{}, bc7{}, anisotropic{};
        bool memory_info{}; // GL_NVX_gpu_memory_info (the free GPU memory in the texture figures)
        float max_anisotropy = 1.0f;
        int max_texture_size = 2048;
    };
    const Caps& caps() const { return caps_; }
    // Texture memory and streaming figures (F1 menu, DW4_TRACE_TEXTURES);
    // the peak and the per-stage counts restart with reset_texture_stats().
    struct TextureStats {
        std::size_t originals{}, replacements{};
        std::size_t original_bytes{}, replacement_bytes{}, peak_bytes{};
        std::size_t uploads{}, upload_bytes{};
        double upload_ms{}, longest_upload_pass_ms{};
        std::size_t missing_draws{}; // draws that showed the original while the pack's texture was not there yet
        std::size_t evictions{}, failed{};
        std::size_t pending{};       // scans and loads queued
        std::size_t images_scanned{}, loads{};
        double scan_ms{}, load_ms{};
        long long gpu_available_kb = -1; // GL_NVX_gpu_memory_info, -1 without it
        std::string last_error;
    };
    TextureStats texture_stats() const;
    void reset_texture_stats();
    // The main thread's texture work since the last call (decoding and
    // uploading the game's own textures, the pack's uploads), in ms: the
    // stage start window of DW4_TRACE_LOADTIME shows it for its longest pass.
    double take_pass_texture_ms() {
        const double ms = pass_texture_ms_;
        pass_texture_ms_ = 0;
        return ms;
    }
    // The controller prompts (null: the game's own pictures everywhere).
    void set_prompts(PromptSource* source) { prompts_ = source; }
    // The game font's glyph cells (once the engine loaded its font).
    void set_prompt_glyphs(const PromptGlyphs& glyphs);
    bool has_prompt_glyphs() const { return glyphs_.valid; }
    const PromptGlyphs& prompt_glyphs() const { return glyphs_; }
    // Translations (docs/L10N.md 2): the game font whose atlas gets the
    // composed glyph cells (engine::paint_composed_glyphs) when it uploads,
    // also into every level of a texture pack's larger font00 as it streams
    // in. Its glyphs are copied; the atlas (the original and the pack's)
    // uploads again when another font image arrives.
    void set_font_atlas(const engine::FontImage& font);
    // The display language's pictures (docs/L10N.md 3), taken instead of the
    // USA textures of these pack hashes: the records show the picture by its
    // own hash (a texture pack's replacement of it by that hash applies, the
    // pack's one of the USA texture does not). Hashed uploads are shared by
    // hash, so a switch only changes which hash a record draws; the game's
    // own textures are hashed while a set with pictures is active. Empty:
    // the game's own pictures.
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
    // A movie picture (RGBA rows, top first) over the whole 4:3 screen;
    // `picture` its number (MovieDecoder::picture_id: uploaded once; 0:
    // uploaded every time).
    void draw_movie(const std::uint8_t* rgba, int width, int height, std::uint64_t picture, const Viewport& screen, int window_width,
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
    // The same for several pixels, in their order (the name tags' probes of
    // a frame in one go). OpenGL ES 3.0 has no depth read (its glReadPixels
    // takes no GL_DEPTH_COMPONENT): there the window's depth around the
    // pixels is blitted into a depth texture, one pass of points fetches it
    // at the pixels and one glReadPixels of a small integer row takes the
    // values back (the same bits a depth read gives); the PC reads each
    // pixel with glReadPixels. A context whose depth cannot be read this way
    // (no depth buffer, a failed step: logged once) gives nullopt.
    std::vector<std::optional<float>> read_depths(const std::vector<std::array<int, 2>>& pixels, int window_width, int window_height);
    // The projected shadow drawn with the next render (null: none).
    void set_shadow(const engine::ShadowFrame* shadow) { shadow_ = shadow; }
    // Drop the stage's GPU resources (call when the engine changed stage):
    // geometry and the textures' records by document. Textures known by
    // their pixel hash (the pack's and the game's own, when a pack streams)
    // stay until the memory budget evicts them.
    void clear_cache();
    // Android's low memory (SDL_EVENT_LOW_MEMORY): the textures known by
    // hash (a texture pack's, and the game's own while a pack streams) that
    // no picture drew in the last two frames go at once, as the memory
    // budget lets them go when it is full; they come again when drawn.
    struct Trimmed {
        std::size_t textures{}, bytes{};
    };
    Trimmed trim_textures();
    // The context was lost and a new one is current (Android's
    // SDL_EVENT_RENDER_DEVICE_RESET): every object is made again, the old
    // names are forgotten without deleting them.
    void reset_context();
    // Dev tool (DW4_DEV_IDS, dev_present.hpp): render() draws every item in
    // a flat colour that encodes its position in the draw list (index + 1 in
    // R, G, B), without blending, multisampling or shadow receivers; the
    // alpha test still drops what the picture does not show.
    void set_id_pass(bool on) { id_pass_ = on; }
    std::size_t texture_count() const { return textures_.size(); }
    std::size_t mesh_count() const { return meshes_.size(); }
    struct Impl;

private:
    void create_objects();
    // check_gl_errors' counting and logging of an error already taken.
    void note_gl_error(unsigned error, const char* where);
    struct Mesh {
        unsigned vao{}, vbo{}, ibo{};
        int index_count{};
        bool has_colors{}, has_uvs{}, has_normals{};
        std::uint64_t serial{};       // GeometryData instance the buffers hold
        std::uint64_t last_frame{};   // eviction of unused runtime geometry
    };
    void release(Mesh& mesh);
    std::uint64_t frame_{};
    // A GL texture object: a game texture (by its pixel hash when a pack
    // streams, else owned by its binding's record) or a pack's replacement
    // (by the pack hash).
    struct GpuTexture {
        unsigned id{};
        std::size_t bytes{};
        std::uint64_t last_frame{};
        int levels = 1;                 // 1: the full-size image only (sampled as GX does)
        std::uint64_t slot{};           // the budget's id
        int width{}, height{};
        std::vector<std::uint8_t> rgba; // level 0 (the title's PRESS START replacement)
    };
    // A texture binding's record (by document and reference, until the
    // stage changes): its sampler, its hashes and its texture.
    struct Texture {
        const engine::Attribute* binding{};
        int wrap_s{}, wrap_t{}, min_filter{};
        bool mag_nearest{};
        bool keyed{};                         // the hashes are known
        bool scanned{};                       // ... from the loading thread's scan
        // The pack or the texture quality changed while this record had no
        // hashes (the game's own textures are hashed only while a pack
        // streams or dumps): keyed when its document's scan is there.
        bool rekey{};
        // The hashes and the size of the picture it shows: the original's,
        // or the display language's picture for it (apply_language), whose
        // own hash then keys the original's upload and the pack's
        // replacement. usa_*: the game's own texture's.
        std::uint64_t pack_hash{}, pixel_hash{};
        std::uint32_t width{}, height{};
        std::uint64_t usa_pack_hash{}, usa_pixel_hash{};
        std::uint32_t usa_width{}, usa_height{};
        bool language{};                      // shows the language's picture
        bool in_pack{};
        bool authored_mips{};                 // IGB mip images (never in this game's data): not shared
        bool failed{};
        std::uint64_t asked_frame{};
        GpuTexture owned;                     // the original when not shared by hash
        // The title's PRESS START texture (its decoded pixels for the
        // prompt variants, and whether it is magnified nearest).
        bool title_press = false;
        bool nearest = false;
        int width_px = 0, height_px = 0;
        std::vector<std::uint8_t> original;
    };
    // The texture a draw shows: the pack's replacement once it is uploaded,
    // else the game's own (decoded now if no record holds it); `replaced`
    // says which. Null when the texture cannot be decoded.
    const GpuTexture* resolve(Texture& texture, bool& replaced);
    const GpuTexture* upload_original(Texture& texture);
    // The record's hashes from the loading thread's scan of its document
    // (false before the scan).
    bool key_from_scan(Texture& texture);
    // The keyed record's shown picture (usa_* or the display language's
    // picture for it) and whether the pack has it (docs/L10N.md 3).
    void apply_language(Texture& texture);
    // Whether a record's texture is the game font's atlas, which gets the
    // composed glyph cells (set_font_atlas).
    bool font_atlas(const Texture& texture) const;
    // The composed glyph cells painted into every level of a pack's font
    // atlas (block encodings are decoded to RGBA8 first).
    void paint_font_payload(TexturePayload& payload) const;
    // After a pack or quality change: which records' textures the pack has,
    // and which are keyed again (Texture::rekey).
    void refresh_pack_flags();
    // A replacement going up in bands of rows over one or more passes
    // (stream_textures): its payload, its texture, the level and row next.
    // `moved` says whether the pass uploaded anything yet: its first piece
    // always goes, later ones only while the measured upload rate says
    // they end within the pass's budget.
    struct Upload;
    void begin_upload(TexturePayload payload);
    bool continue_upload(std::chrono::steady_clock::time_point deadline, bool& moved);
    void finish_upload();
    // A replacement the GPU refused (a GL error while it went up): never
    // asked for again in this configuration, the game's own is drawn.
    void refuse_upload(std::uint64_t hash, unsigned error);
    unsigned sampler(const Texture& texture, const GpuTexture& gpu);
    void drop_samplers();
    void drop_originals();
    void drop_replacements();
    void configure_streamer();
    bool streaming() const;
    void delete_texture(GpuTexture& texture);
    // The textures known by hash, least recently drawn first, until they fit
    // `shared_budget` (never one drawn in the last two frames).
    Trimmed evict_hashed(std::size_t shared_budget);
    void note_bytes();
    void sharpen(const Viewport& area);
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
    unsigned title_texture(const Texture& texture, const GpuTexture* replacement);
    const Mesh* mesh(const engine::GeometryData* geometry);
    Texture* texture(const engine::Attribute* binding);
    void render_shadow_map();
    void render_shadow_receivers(const engine::FrameCamera& camera);

    std::unique_ptr<Impl> gl_;
    unsigned program_{};
    std::unordered_map<const engine::GeometryData*, Mesh> meshes_;
    std::map<std::pair<const void*, std::uint32_t>, Texture> textures_;
    std::unordered_map<std::uint64_t, GpuTexture> originals_;    // the game's textures by pixel hash
    std::unordered_map<std::uint64_t, GpuTexture> replacements_; // the pack's by pack hash
    std::unordered_map<std::uint64_t, bool> failed_uploads_;     // pack hashes the GPU refused
    std::unordered_map<unsigned, unsigned> samplers_;            // by sampler key
    std::unique_ptr<TextureStreamer> streamer_;
    std::unique_ptr<Upload> upload_;
    GraphicsOptions graphics_;
    Caps caps_;
    bool cpu_decode_{};               // DW4_TEXTURE_CPU_DECODE: block formats decoded on the CPU (test of that path)
    double upload_budget_ms_ = 1.5;   // uploads per pass (DW4_TEXTURE_UPLOAD_MS)
    double upload_ns_per_byte_ = 1.0; // the measured upload rate (a running average)
    double begin_upload_ms_ = 0.05;   // ... and the time a texture's storage takes
    std::uint64_t refuse_hash_{};     // DW4_TEXTURE_TEST_REFUSE: this pack hash's upload raises a GL error (test of the fallback)
    std::uint64_t next_slot_ = 1;
    double pass_texture_ms_{};
    int trace_{}; // DW4_TRACE_TEXTURES (2: every texture drawn before its pack texture was there)
    std::size_t owned_bytes_{};
    TextureStats stats_;
    unsigned sharpen_program_{}, sharpen_texture_{};
    int sharpen_width_{}, sharpen_height_{};
    unsigned white_texture_{};
    const engine::ShadowFrame* shadow_{};
    unsigned movie_program_{}, movie_vao_{};
    std::array<unsigned, 3> movie_textures_{};       // a ring: the pictures' uploads (draw_movie)
    std::array<std::uint64_t, 3> movie_pictures_{};  // the picture each one holds
    std::size_t movie_slot_{};                       // the last one uploaded
    int movie_width_{}, movie_height_{};
    unsigned kept_program_{}, kept_texture_{};
    int kept_width_{}, kept_height_{};
    unsigned shadow_fbo_{}, shadow_texture_{};
    int shadow_width_{}, shadow_height_{}, shadow_border_{};
    bool shadow_ready_{};
    TexturePack* pack_{};
    PromptSource* prompts_{};
    PromptGlyphs glyphs_;
    // Translations: the font atlas's image (document, reference), its
    // hashes (any texture of these pixels, and the pack's replacement, get
    // the cells) and the font's glyphs the composed cells are painted from;
    // the language's pictures by USA pack hash.
    std::pair<const void*, std::uint32_t> atlas_key_{};
    std::uint64_t atlas_pack_hash_{}, atlas_pixel_hash_{};
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
