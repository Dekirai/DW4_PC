#pragma once

// The F1 menu's Graphics options (settings.ini [graphics], docs/TEXTURES.md):
// how textures are chosen and sampled, the sharpening pass and the shadow
// map size. Presentation only: none of them reaches the simulation, the tick
// state, saves, snapshots or the online wire, so online partners can use
// different values. GL-free (the renderer applies them, settings.cpp stores
// them, the tests check both).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dw4::game {

// Which texture pack level is drawn: none (the game's own textures), the
// pack's 2x level or its 4x level (a pack's mip chain or its variants).
enum class TextureQuality : std::uint8_t { original, high, ultra };
// Bilinear: the game's own textures as the GameCube samples them (their
// full-size image only); pack textures still blend their mipmaps. Trilinear
// gives the game's textures mipmaps too; the anisotropic levels add
// anisotropic filtering on top of trilinear.
enum class TextureFilter : std::uint8_t { bilinear, trilinear, aniso2, aniso4, aniso8, aniso16 };
// A contrast adaptive sharpening pass over the game picture.
enum class Sharpening : std::uint8_t { off, low, high };
// The projected shadow's map: the game's own size (128 x 128 for every
// stage), 4x or 8x as many texels per side.
enum class ShadowQuality : std::uint8_t { original, high, ultra };

struct GraphicsOptions {
    TextureQuality texture_quality = TextureQuality::ultra;
    TextureFilter texture_filter = TextureFilter::bilinear;
    float lod_bias = 0.0f;             // added to the mip level choice, kLodBiasMin..kLodBiasMax
    Sharpening sharpening = Sharpening::off;
    ShadowQuality shadow_quality = ShadowQuality::original;
    int texture_memory_mb = 1024;      // the texture cache's budget, kMemoryMinMb..kMemoryMaxMb

    static constexpr float kLodBiasMin = -2.0f, kLodBiasMax = 2.0f;
    static constexpr int kMemoryMinMb = 32, kMemoryMaxMb = 8192;
    bool operator==(const GraphicsOptions&) const = default;
    // Values outside their ranges brought into them.
    GraphicsOptions clamped() const;
};

// The settings.ini / environment spelling of each value ("ultra",
// "anisotropic16", "low" ...) and its reading (case-insensitive; the
// numbers "0".."2" / "0".."5" too); nullopt for anything else.
const char* texture_quality_name(TextureQuality value);
const char* texture_filter_name(TextureFilter value);
const char* sharpening_name(Sharpening value);
const char* shadow_quality_name(ShadowQuality value);
std::optional<TextureQuality> parse_texture_quality(std::string_view text);
std::optional<TextureFilter> parse_texture_filter(std::string_view text);
std::optional<Sharpening> parse_sharpening(std::string_view text);
std::optional<ShadowQuality> parse_shadow_quality(std::string_view text);
std::optional<float> parse_lod_bias(std::string_view text);

// The F1 menu's labels.
const char* texture_quality_label(TextureQuality value); // "Ultra (4x)"
const char* texture_filter_label(TextureFilter value);   // "Anisotropic 16x"
const char* sharpening_label(Sharpening value);
const char* shadow_quality_label(ShadowQuality value);   // "High (512 x 512)"

// The pack level a quality draws (its scale over the original texture):
// 0 for none, 2 or 4.
int target_scale(TextureQuality value);
// The anisotropy a filter asks for (1: none).
int anisotropy(TextureFilter value);
// Whether the game's own textures get mipmaps.
inline bool original_mipmaps(TextureFilter value) { return value != TextureFilter::bilinear; }
// The CAS sharpness of a level (0: no pass).
float sharpness(Sharpening value);
// Shadow map texels per side over the game's own size.
int shadow_scale(ShadowQuality value);

// Test runs read no settings.ini: their options are `base` with
// DW4_TEXTURE_QUALITY, DW4_TEXTURE_FILTER, DW4_LOD_BIAS, DW4_SHARPEN,
// DW4_SHADOW_QUALITY and DW4_TEXTURE_MEMORY (MiB) applied, each where it is
// set and valid. `lookup` stands for std::getenv.
GraphicsOptions graphics_from_env(GraphicsOptions base, const char* (*lookup)(const char*));

} // namespace dw4::game
