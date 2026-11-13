#pragma once

// Texture modding: decoded game textures can be written out as PNG files
// (dump) and replaced by texture packs (docs/TEXTURES.md). A texture is
// named "<image name>_<width>x<height>_<hash>.png", the hash being a 64-bit
// FNV-1a of its decoded RGBA pixels.
//
// Two pack layouts are read:
// - format 1, loose files: any "..._<16 hex digits>.png" (or ".dds") below
//   the pack folder replaces the texture of that hash, at any size;
// - format 2, a manifest: "dw4pack.tsv" in the pack folder lists, per
//   texture hash, the files that hold it (each with its encoding: BC7, BC3,
//   BC1 or RGBA8 DDS files with their mip chains, or PNG), the scale of
//   each file's first level over the original and its level count. The
//   loader picks per texture the file and the first level that give the
//   chosen quality's scale in an encoding it can use; encodings it does not
//   know (ASTC for GLES devices, later) are skipped, so a pack can carry
//   them beside the PC files without breaking this contract.
#include "texture_codec.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

// One file of a pack texture: a manifest row, or a loose file of a format 1
// pack (`legacy`: encoding and scale are read from the file when it loads).
struct PackFile {
    TextureEncoding encoding = TextureEncoding::unknown;
    int scale = 0;                   // level 0 over the original texture (1, 2, 4, 8, 16); 0 unknown
    int levels = 1;                  // levels the file holds (a PNG: 1, the loader makes the rest)
    std::uint32_t width{}, height{}; // level 0, 0 when the manifest does not say
    std::filesystem::path path;
    bool legacy{};
    bool dds{};                      // a legacy file's container (.dds, else .png)
};

// A pack folder's contents (shared with the loading thread; never changed
// once made: TexturePack::configure makes a new one).
struct PackIndex {
    int format{};                    // 0 none, 1 loose files, 2 manifest
    std::string name;                // the manifest's #name
    std::filesystem::path folder;
    std::unordered_map<std::uint64_t, std::vector<PackFile>> textures;
    std::vector<std::string> warnings; // skipped manifest rows and files
    std::string error;                 // why a manifest was refused (the pack is empty then)
};

constexpr int kPackFormat = 2;
constexpr const char* kPackManifest = "dw4pack.tsv";

// Reads a manifest (the text of dw4pack.tsv; `folder` resolves its paths).
// A wrong first line, a newer format or missing columns refuse it (nullopt,
// `error` says why); malformed rows are skipped with a warning each.
std::optional<PackIndex> parse_pack_manifest(std::string_view text, const std::filesystem::path& folder, std::string& error);

// The encodings the GPU samples directly; the others are decoded to RGBA8 on
// the loading thread.
struct EncodingSupport {
    bool bc1 = true, bc3 = true, bc7 = true;
    bool native(TextureEncoding encoding) const {
        return encoding == TextureEncoding::rgba8 || encoding == TextureEncoding::png || (encoding == TextureEncoding::bc1 && bc1) ||
               (encoding == TextureEncoding::bc3 && bc3) || (encoding == TextureEncoding::bc7 && bc7);
    }
};

// The file and its first level for a quality's scale (2 High, 4 Ultra):
// the level whose scale equals it, else the closest below, else the
// smallest above; ties go to encodings the GPU reads, then BC7, BC3, BC1,
// RGBA8, PNG. Legacy files come back with first_level 0 (decided when the
// file is read). Null when no file has a usable encoding.
struct PackChoice {
    const PackFile* file{};
    int first_level{};
    int scale{}; // of the chosen first level (0 unknown)
};
PackChoice choose_pack_file(const std::vector<PackFile>& files, int target_scale, const EncodingSupport& gpu);

// A replacement ready for the GPU: its levels in one buffer, in `encoding`
// (rgba8, bc1, bc3 or bc7), level 0 first.
struct TexturePayload {
    std::uint64_t hash{};
    TextureEncoding encoding = TextureEncoding::unknown;
    std::vector<TextureLevel> levels; // offsets into data
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> rgba;   // level 0 as RGBA8, when asked for
    int scale{};                      // level 0 over the original (0 unknown)
    std::uint32_t generation{};       // the loader's configuration it was made for
    std::size_t bytes() const { return data.size(); }
};

// Reads a pack file into a payload for a texture of the given original
// size, from the chosen level on (the levels above it are not read). A PNG
// larger than the original gets a mip chain; one not larger keeps its one
// level (sampled like the original). Encodings the GPU does not read are
// decoded to RGBA8; `want_rgba` also keeps level 0 as RGBA8. Nullopt with
// `error` for a missing, truncated or mismatching file.
std::optional<TexturePayload> load_pack_file(const PackFile& file, int first_level, int target_scale, std::uint32_t original_width,
                                             std::uint32_t original_height, const EncodingSupport& gpu, bool want_rgba,
                                             std::string& error);

class TexturePack {
public:
    // pack: the pack folder (indexed once: its manifest, else its loose
    // files); dump: folder for dumped textures, empty for none.
    void configure(const std::filesystem::path& pack, const std::filesystem::path& dump);
    std::size_t replacement_count() const { return index_->textures.size(); }
    const PackIndex& index() const { return *index_; }
    std::shared_ptr<const PackIndex> shared_index() const { return index_; }
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
    // Decodes a PNG file's bytes to RGBA8 (thread safe).
    static std::optional<TextureImage> read_png(const std::vector<std::uint8_t>& bytes);

    // The replacement's largest level as RGBA8, loaded now (tools and tests;
    // the renderer streams through TextureStreamer).
    std::optional<TextureImage> replacement(std::uint64_t hash) const;
    // Writes the texture once per session when dumping.
    void dump(const std::string& image_name, const std::uint8_t* rgba, int width, int height, std::uint64_t hash);

private:
    std::shared_ptr<const PackIndex> index_ = std::make_shared<PackIndex>();
    std::filesystem::path dump_;
    std::unordered_map<std::uint64_t, bool> dumped_;
};

} // namespace dw4::game
