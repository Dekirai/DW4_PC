#include "dw4/textures.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace dw4 {
namespace {

constexpr std::uint64_t max_rgba_bytes = 64ull * 1024 * 1024;

[[noreturn]] void fail(const std::string& message) {
    throw TextureError("Texture: " + message);
}

std::size_t output_size(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 16384 || height > 16384)
        fail("dimensions must be between 1 and 16,384");
    const auto bytes = std::uint64_t(width) * height * 4;
    if (bytes > max_rgba_bytes) fail("decoded image exceeds the 64 MiB limit");
    return static_cast<std::size_t>(bytes);
}

std::size_t bc1_size(std::uint32_t width, std::uint32_t height) {
    (void)output_size(width, height);
    return static_cast<std::size_t>((std::uint64_t(width) + 3) / 4 * ((std::uint64_t(height) + 3) / 4) * 8);
}

void require_bytes(std::span<const std::uint8_t> bytes, std::uint64_t required, const char* purpose) {
    if (required > bytes.size()) fail(std::string(purpose) + " is truncated");
}

using Pixel = std::array<std::uint8_t, 4>;

Pixel rgb565(std::uint16_t value) {
    const auto r = (value >> 11) & 31;
    const auto g = (value >> 5) & 63;
    const auto b = value & 31;
    return {static_cast<std::uint8_t>((r << 3) | (r >> 2)),
        static_cast<std::uint8_t>((g << 2) | (g >> 4)),
        static_cast<std::uint8_t>((b << 3) | (b >> 2)), 255};
}

bool boolean_field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto field = doc.field(object, slot);
    if (!field) fail("igImage is missing a required boolean field");
    const auto bytes = doc.field_bytes(*field);
    if (bytes.size() != 1 || bytes[0] > 1) fail("igImage has an invalid serialized boolean");
    return bytes[0] != 0;
}

std::uint32_t required_reference(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto reference = doc.field_reference(object, slot);
    if (!reference) fail("image or palette data reference is null");
    return *reference;
}

struct ImageSource {
    TextureInfo info;
    std::span<const std::uint8_t> pixels;
    std::span<const std::uint8_t> palette;
    std::uint32_t row_stride{};
    std::uint32_t palette_entries{};
    std::uint32_t palette_stride{};
    bool compressed{};
};

ImageSource inspect_image(const IgbDocument& doc, const IgbObject& object) {
    if (!doc.is_a(object, "igImage")) fail("selected object is not an igImage");
    ImageSource result;
    result.info.reference_index = object.reference_index;
    result.info.width = doc.field_u32(object, 2);
    result.info.height = doc.field_u32(object, 3);
    result.info.name = std::string(doc.field_string(object, 22));
    (void)output_size(result.info.width, result.info.height);
    // Slot 4 is 1 for indexed images and 4 for BC1 in the observed files.
    // Its semantic name is not established; do not treat it as 3D image depth.
    const auto storage_scalar = doc.field_u32(object, 4);
    if (doc.field_u32(object, 5) != 1 || doc.field_u32(object, 6) != 100 || doc.field_u32(object, 21) != 0)
        fail("igImage storage/order descriptor differs from the verified layout");
    if (doc.metadata.header.byte_order != IgbByteOrder::little)
        fail("the big-endian igImage payload layout has not been verified");
    const auto format = doc.field_u32(object, 11);
    const auto declared_size = doc.field_i32(object, 12);
    if (declared_size <= 0) fail("image declares no positive pixel-data size");
    result.pixels = doc.memory(required_reference(doc, object, 13));
    require_bytes(result.pixels, static_cast<std::uint32_t>(declared_size), "declared image data");
    result.pixels = result.pixels.first(static_cast<std::size_t>(declared_size));
    result.info.encoded_bytes = result.pixels.size();
    result.compressed = boolean_field(doc, object, 20);
    const auto index_bits = doc.field_u32(object, 18);
    const auto intensity_bits = doc.field_u32(object, 16);
    if (result.compressed) {
        if (format != 14 || storage_scalar != 4 || index_bits != 0 || intensity_bits != 0)
            fail("compressed igImage format " + std::to_string(format) + " is not the supported linear BC1 layout");
        for (std::uint16_t slot = 7; slot <= 10; ++slot)
            if (doc.field_u32(object, slot) != 1)
                fail("compressed igImage channel descriptor differs from verified BC1 storage");
        require_bytes(result.pixels, bc1_size(result.info.width, result.info.height), "BC1 base image");
        result.info.format_name = "BC1 / DXT1 (linear)";
    } else {
        if (format != 65536 || storage_scalar != 1 || index_bits != 8 || intensity_bits != 0)
            fail("uncompressed igImage format " + std::to_string(format) + " is not the supported indexed8 layout");
        for (std::uint16_t slot = 7; slot <= 10; ++slot)
            if (doc.field_u32(object, slot) != 0)
                fail("indexed igImage has unexpected direct-color channel widths");
        const auto row_stride = doc.field_i32(object, 19);
        if (row_stride <= 0 || static_cast<std::uint32_t>(row_stride) < result.info.width)
            fail("indexed image row stride is smaller than its width");
        result.row_stride = static_cast<std::uint32_t>(row_stride);
        require_bytes(result.pixels, std::uint64_t(result.row_stride) * result.info.height, "indexed base image");
        const auto& clut = doc.object(required_reference(doc, object, 17));
        if (!doc.is_a(clut, "igClut")) fail("image palette reference is not an igClut");
        if (doc.field_u32(clut, 2) != 7) fail("palette format is not the verified RGBA8 format (7)");
        result.palette_entries = doc.field_u32(clut, 3);
        const auto palette_stride = doc.field_i32(clut, 4);
        const auto palette_size = doc.field_i32(clut, 6);
        if (result.palette_entries == 0 || result.palette_entries > 256 || palette_stride != 4 || palette_size <= 0)
            fail("palette is not the verified 1..256-entry, four-byte RGBA layout");
        result.palette_stride = 4;
        result.palette = doc.memory(required_reference(doc, clut, 5));
        require_bytes(result.palette, static_cast<std::uint32_t>(palette_size), "declared palette data");
        result.palette = result.palette.first(static_cast<std::size_t>(palette_size));
        require_bytes(result.palette, std::uint64_t(result.palette_entries) * 4, "RGBA palette");
        result.info.format_name = "Indexed8 + RGBA8 palette (linear)";
    }
    result.info.supported = true;
    return result;
}

} // namespace

RgbaImage decode_indexed8_rgba8(std::span<const std::uint8_t> indices,
    std::uint32_t width, std::uint32_t height, std::uint32_t row_stride,
    std::span<const std::uint8_t> palette, std::uint32_t palette_entries,
    std::uint32_t palette_stride) {
    const auto bytes = output_size(width, height);
    if (row_stride < width) fail("indexed row stride is smaller than the image width");
    if (!palette_entries || palette_entries > 256 || palette_stride < 4 || palette_stride > 1024)
        fail("invalid RGBA palette size or stride");
    require_bytes(indices, std::uint64_t(row_stride) * height, "indexed image");
    require_bytes(palette, std::uint64_t(palette_stride) * palette_entries, "RGBA palette");
    RgbaImage result{width, height, std::vector<std::uint8_t>(bytes)};
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto index = indices[static_cast<std::size_t>(y) * row_stride + x];
            if (index >= palette_entries) fail("pixel index refers beyond its palette");
            const auto source = static_cast<std::size_t>(index) * palette_stride;
            const auto dest = (static_cast<std::size_t>(y) * width + x) * 4;
            std::copy_n(palette.data() + source, 4, result.rgba.data() + dest);
        }
    return result;
}

RgbaImage decode_bc1(std::span<const std::uint8_t> blocks, std::uint32_t width, std::uint32_t height) {
    const auto required = bc1_size(width, height);
    require_bytes(blocks, required, "BC1 image");
    RgbaImage result{width, height, std::vector<std::uint8_t>(output_size(width, height)), true};
    std::size_t block_offset = 0;
    for (std::uint32_t block_y = 0; block_y < height; block_y += 4)
        for (std::uint32_t block_x = 0; block_x < width; block_x += 4) {
            const auto* data = blocks.data() + block_offset;
            block_offset += 8;
            const auto first = static_cast<std::uint16_t>(data[0] | (std::uint16_t(data[1]) << 8));
            const auto second = static_cast<std::uint16_t>(data[2] | (std::uint16_t(data[3]) << 8));
            std::array<Pixel, 4> colors{rgb565(first), rgb565(second), Pixel{}, Pixel{}};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                if (first > second) {
                    colors[2][channel] = static_cast<std::uint8_t>((2 * unsigned(colors[0][channel]) + colors[1][channel]) / 3);
                    colors[3][channel] = static_cast<std::uint8_t>((colors[0][channel] + 2 * unsigned(colors[1][channel])) / 3);
                } else colors[2][channel] = static_cast<std::uint8_t>((unsigned(colors[0][channel]) + colors[1][channel]) / 2);
            }
            colors[2][3] = 255;
            colors[3][3] = first > second ? 255 : 0;
            // GX CMPR's three-colour mode keeps the average (c0 + c1) / 2 in the
            // transparent texel where BC1 has black. Bilinear filtering mixes
            // that colour into the edge pixels that pass an alpha test
            // (stagBS05 floor Bs0423_n: Dolphin's texture dump has c2 there).
            if (first <= second)
                for (std::size_t channel = 0; channel < 3; ++channel) colors[3][channel] = colors[2][channel];
            // BC1 uses little-endian endpoint words and least-significant selector
            // bits first.
            for (std::uint32_t y = 0; y < 4 && block_y + y < height; ++y)
                for (std::uint32_t x = 0; x < 4 && block_x + x < width; ++x) {
                    const auto selector = (data[4 + y] >> (x * 2)) & 3;
                    const auto dest = (static_cast<std::size_t>(block_y + y) * width + block_x + x) * 4;
                    std::copy(colors[selector].begin(), colors[selector].end(), result.rgba.begin() + static_cast<std::ptrdiff_t>(dest));
                }
        }
    return result;
}

std::vector<TextureInfo> list_igb_textures(const IgbDocument& document) {
    std::vector<TextureInfo> result;
    for (const auto& object : document.objects) {
        if (!document.is_a(object, "igImage")) continue;
        TextureInfo info;
        info.reference_index = object.reference_index;
        info.format_name = "Unsupported IGB image";
        try {
            info.width = document.field_u32(object, 2);
            info.height = document.field_u32(object, 3);
            info.name = std::string(document.field_string(object, 22));
            info = inspect_image(document, object).info;
        } catch (const std::exception& error) {
            info.diagnostic = error.what();
        }
        result.push_back(std::move(info));
    }
    return result;
}

RgbaImage decode_igb_texture(const IgbDocument& document, std::uint32_t reference_index) {
    try {
        const auto source = inspect_image(document, document.object(reference_index));
        if (source.compressed) return decode_bc1(source.pixels, source.info.width, source.info.height);
        return decode_indexed8_rgba8(source.pixels, source.info.width, source.info.height,
            source.row_stride, source.palette, source.palette_entries, source.palette_stride);
    } catch (const IgbError& error) {
        throw TextureError(error.what());
    }
}

} // namespace dw4
