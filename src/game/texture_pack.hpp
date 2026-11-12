#pragma once

// Texture modding: decoded game textures can be written out as PNG files
// (dump) and replaced by PNG files of any size (texture packs). A texture is
// named "<image name>_<width>x<height>_<hash>.png", the hash being a 64-bit
// FNV-1a of its decoded RGBA pixels; a replacement only needs the hash part
// ("..._<16 hex digits>.png") anywhere below the pack folder.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw4 {
struct RgbaImage;
}

namespace dw4::game {

struct TextureImage {
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
};

class TexturePack {
public:
    // pack: folder of replacement PNGs (indexed once); dump: folder for
    // dumped textures, empty for none.
    void configure(const std::filesystem::path& pack, const std::filesystem::path& dump);
    std::size_t replacement_count() const { return replacements_.size(); }
    bool dumping() const { return !dump_.empty(); }

    static std::uint64_t hash(const std::uint8_t* rgba, std::size_t bytes, int width, int height);
    // The pack hash of a decoded game texture, the one the renderer looks
    // replacements up by and dumps under. CMPR (BC1) images hash the earlier
    // decode with black transparent texels (decode_bc1 now keeps GX's average
    // colour there), so packs made before that change still match them.
    // Every tool that names textures for packs (tools/dw4_texpack) calls this
    // function; a copy of the rule would drift from the renderer.
    static std::uint64_t image_hash(const RgbaImage& image);
    static std::string file_name(const std::string& image_name, int width, int height, std::uint64_t hash);
    // Writes straight RGBA8 rows (or `channels` 8-bit channels per pixel) as
    // a PNG file (dumps and exports).
    static bool write_png(const std::filesystem::path& path, const std::uint8_t* pixels, int width, int height,
                          int channels = 4);

    // The replacement for a texture, if the pack has one (loaded from disk).
    std::optional<TextureImage> replacement(std::uint64_t hash) const;
    // Writes the texture once per session when dumping.
    void dump(const std::string& image_name, const std::uint8_t* rgba, int width, int height, std::uint64_t hash);

private:
    std::unordered_map<std::uint64_t, std::filesystem::path> replacements_;
    std::filesystem::path dump_;
    std::unordered_map<std::uint64_t, bool> dumped_;
};

} // namespace dw4::game
