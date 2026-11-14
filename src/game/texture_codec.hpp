#pragma once

// Texture pack payloads (docs/TEXTURES.md, pack format 2): the encodings a
// pack may store, DDS headers, CPU decoders for the block formats (for GPUs
// without them and for pictures the host needs as pixels) and mip chains.
// GL-free and SDL-free; every function checks its input ranges and never
// reads outside the given bytes.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

// Pixel encodings of pack files. rgba8, bc1, bc3 and bc7 come in DDS files,
// png in PNG files (one level; the loader makes the mip chain). Further
// encodings (ASTC for GLES devices) are new names in the manifest that
// earlier loaders skip.
enum class TextureEncoding : std::uint8_t { unknown, rgba8, bc1, bc3, bc7, png };
const char* encoding_name(TextureEncoding encoding); // "bc7", "rgba8", ...
TextureEncoding parse_encoding(std::string_view text); // unknown for names this loader does not read
bool block_compressed(TextureEncoding encoding);
// Bytes of one level (4x4 blocks for the block formats, partial blocks
// rounded up); 0 for png / unknown.
std::size_t level_bytes(TextureEncoding encoding, std::uint32_t width, std::uint32_t height);

// A level's size and its bytes inside a payload.
struct TextureLevel {
    std::uint32_t width{}, height{};
    std::size_t offset{}, size{};
};

// A DDS file's header as far as packs use it: a 2D texture (no cube, array
// or volume) in one of the encodings above, its levels and where they are.
// sRGB variants are read as their UNORM formats (the port samples values as
// stored, as GX does). Uncompressed 32-bit files with BGRA masks are
// reported as rgba8 with `bgra` set (the loader swaps the channels).
struct DdsInfo {
    TextureEncoding encoding = TextureEncoding::unknown;
    std::uint32_t width{}, height{};
    bool bgra{};
    std::size_t data_offset{}; // first level's byte (128, or 148 with the DX10 header)
    std::vector<TextureLevel> levels; // offsets from the start of the file
};
// Reads the header and checks that `file_size` holds every declared level
// (dimensions up to 16384, at most 15 levels). `header` needs the first 148
// bytes (or the whole file when it is shorter). On failure `error` says why.
std::optional<DdsInfo> parse_dds_header(std::span<const std::uint8_t> header, std::size_t file_size, std::string& error);

// A DDS file in memory (tests and tools): header and levels as given.
// `levels` hold the encoded bytes of level 0, 1, ... (sizes must match).
std::vector<std::uint8_t> make_dds(TextureEncoding encoding, std::uint32_t width, std::uint32_t height,
                                   const std::vector<std::vector<std::uint8_t>>& levels, bool dx10_header);

// CPU decoders to straight RGBA8, top row first. BC1 follows Direct3D
// (selector 3 of a three-colour block is transparent black); BC7 follows the
// Khronos Data Format Specification (BPTC), bit exact. Blocks are read in
// row order, partial edge blocks cropped. False when `blocks` is too short.
bool decode_bc1(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba);
bool decode_bc3(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba);
bool decode_bc7(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba);
// One 16-byte BC7 block into 64 bytes (4x4 RGBA8, rows top first).
void decode_bc7_block(const std::uint8_t* block, std::uint8_t* out);
// Any encoding's level to RGBA8 (rgba8 passes through, `bgra` swaps).
bool decode_level(TextureEncoding encoding, std::span<const std::uint8_t> data, std::uint32_t width, std::uint32_t height, bool bgra,
                  std::vector<std::uint8_t>& rgba);

// How a picture's alpha is used, for its mip chain.
enum class AlphaKind : std::uint8_t { opaque, cutout, blend };
// opaque: every alpha 255; cutout: only 0 and 255; blend otherwise.
AlphaKind classify_alpha(std::span<const std::uint8_t> rgba);
// The next smaller level of a straight RGBA8 picture (half size, at least
// 1): a 2x2 box with colours weighted by their alpha (no dark fringes from
// transparent texels). A cutout keeps a binary alpha (the box's alpha
// thresholded at half), so its coverage holds at a distance for any alpha
// test reference; opaque and blend pictures average the alpha.
std::vector<std::uint8_t> half_level(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, AlphaKind kind);
// Level 0 followed by every smaller level down to 1x1, as one buffer with
// the levels' places (used by PNG packs and by the game's own textures with
// mipmapped filtering).
std::vector<std::uint8_t> mip_chain(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, AlphaKind kind,
                                    std::vector<TextureLevel>& levels);

} // namespace dw4::game
