#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;

class TextureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RgbaImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba; // Top-to-bottom rows, straight RGBA8.
    bool block_compressed{};        // Decoded from BC1 / GX CMPR blocks.
};

struct TextureInfo {
    std::uint32_t reference_index{}; // Serialized IGB global reference, not list position.
    std::uint32_t width{};
    std::uint32_t height{};
    std::string name;
    std::string format_name;
    bool supported{};
    std::string diagnostic;
    std::size_t encoded_bytes{};
};

// Native decoders for the linear storage observed in this game's IGB files.
// They do not imply that an arbitrary GX tiled texture uses the same layout.
// Limits: dimensions <= 16,384 and decoded RGBA <= 64 MiB. Only the base image
// is decoded; trailing mip data is accepted but never interpreted implicitly.
RgbaImage decode_indexed8_rgba8(std::span<const std::uint8_t> indices,
    std::uint32_t width, std::uint32_t height, std::uint32_t row_stride,
    std::span<const std::uint8_t> palette, std::uint32_t palette_entries,
    std::uint32_t palette_stride = 4);
RgbaImage decode_bc1(std::span<const std::uint8_t> blocks,
    std::uint32_t width, std::uint32_t height);

// Lists igImage objects without decoding pixels. Unsupported or malformed
// layouts stay visible with diagnostics. References are local to this document;
// callers retain the archive and entry identity alongside the document.
std::vector<TextureInfo> list_igb_textures(const IgbDocument& document);
RgbaImage decode_igb_texture(const IgbDocument& document, std::uint32_t reference_index);

} // namespace dw4
