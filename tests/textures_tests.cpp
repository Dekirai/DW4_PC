#include "dw4/textures.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace dw4;
using Bytes = std::vector<std::uint8_t>;
int checks = 0;

void check(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

void rejects(const std::function<void()>& operation, const std::string& reason) {
    bool rejected = false;
    try { operation(); } catch (const TextureError&) { rejected = true; }
    check(rejected, reason);
}

std::array<std::uint8_t, 4> pixel(const RgbaImage& image, std::uint32_t x, std::uint32_t y) {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.rgba.at(offset), image.rgba.at(offset + 1), image.rgba.at(offset + 2), image.rgba.at(offset + 3)};
}

void indexed_tests() {
    const Bytes palette{255, 0, 0, 255, 0, 128, 255, 127, 3, 7, 11, 0};
    const Bytes indices{0, 1, 2, 255, 2, 1, 0, 255}; // Unused row padding must not become a pixel.
    const auto image = decode_indexed8_rgba8(indices, 3, 2, 4, palette, 3);
    check(image.width == 3 && image.height == 2 && image.rgba.size() == 24, "Indexed output dimensions");
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255}, "Indexed first color");
    check(pixel(image, 1, 0) == std::array<std::uint8_t, 4>{0, 128, 255, 127}, "Indexed straight alpha");
    check(pixel(image, 0, 1) == std::array<std::uint8_t, 4>{3, 7, 11, 0}, "Indexed row pitch and transparent RGB");
    const Bytes wide_palette{11, 12, 13, 14, 99, 21, 22, 23, 24, 99};
    const auto wide = decode_indexed8_rgba8(Bytes{1}, 1, 1, 1, wide_palette, 2, 5);
    check(pixel(wide, 0, 0) == std::array<std::uint8_t, 4>{21, 22, 23, 24}, "Explicit palette stride");
    rejects([&] { decode_indexed8_rgba8(indices, 0, 1, 4, palette, 3); }, "Zero width");
    rejects([&] { decode_indexed8_rgba8(indices, 1, 0, 4, palette, 3); }, "Zero height");
    rejects([&] { decode_indexed8_rgba8(indices, 16385, 1, 16385, palette, 3); }, "Dimension safety limit");
    rejects([&] { decode_indexed8_rgba8(indices, 8192, 8192, 8192, palette, 3); }, "RGBA allocation safety limit");
    rejects([&] { decode_indexed8_rgba8(indices, 3, 2, 2, palette, 3); }, "Row stride below width");
    rejects([&] { decode_indexed8_rgba8(Bytes{0, 1}, 3, 1, 3, palette, 3); }, "Truncated index image");
    rejects([&] { decode_indexed8_rgba8(indices, 3, 2, 4, Bytes{0, 0, 0}, 1); }, "Truncated palette");
    rejects([&] { decode_indexed8_rgba8(Bytes{3}, 1, 1, 1, palette, 3); }, "Palette index out of range");
    rejects([&] { decode_indexed8_rgba8(indices, 3, 2, 4, palette, 0); }, "Empty palette");
    rejects([&] { decode_indexed8_rgba8(indices, 3, 2, 4, palette, 257); }, "Palette exceeds indexed8 address space");
    rejects([&] { decode_indexed8_rgba8(indices, 3, 2, 4, palette, 3, 3); }, "Palette stride below RGBA size");
    rejects([&] { decode_indexed8_rgba8(indices, 1, 2, 0xffffffff, palette, 3); }, "Huge row pitch cannot wrap");
}

void bc1_tests() {
    // Red > blue endpoints; selector byte 0xe4 is left-to-right 0,1,2,3.
    const Bytes four_color{0x00, 0xf8, 0x1f, 0x00, 0xe4, 0xe4, 0xe4, 0xe4};
    const auto image = decode_bc1(four_color, 4, 4);
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255}, "BC1 little-endian first RGB565 endpoint");
    check(pixel(image, 1, 0) == std::array<std::uint8_t, 4>{0, 0, 255, 255}, "BC1 selector bit order");
    check(pixel(image, 2, 0) == std::array<std::uint8_t, 4>{170, 0, 85, 255}, "BC1 first weighted color");
    check(pixel(image, 3, 0) == std::array<std::uint8_t, 4>{85, 0, 170, 255}, "BC1 second weighted color");
    const Bytes transparent{0x1f, 0x00, 0x00, 0xf8, 0xe4, 0xe4, 0xe4, 0xe4};
    const auto alpha = decode_bc1(transparent, 4, 4);
    check(pixel(alpha, 2, 0) == std::array<std::uint8_t, 4>{127, 0, 127, 255}, "BC1 three-color average");
    check(pixel(alpha, 3, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 0}, "BC1 transparent black, distinct from GX CMPR");
    const Bytes solid_red{0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0};
    const Bytes solid_green{0xe0, 0x07, 0x00, 0x00, 0, 0, 0, 0};
    const Bytes solid_blue{0x1f, 0x00, 0x00, 0x00, 0, 0, 0, 0};
    const Bytes solid_white{0xff, 0xff, 0x00, 0x00, 0, 0, 0, 0};
    Bytes blocks;
    for (const auto& block : {solid_red, solid_green, solid_blue, solid_white}) blocks.insert(blocks.end(), block.begin(), block.end());
    const auto cropped = decode_bc1(blocks, 5, 7);
    check(cropped.rgba.size() == 5 * 7 * 4, "BC1 partial blocks crop to dimensions");
    check(pixel(cropped, 4, 0) == std::array<std::uint8_t, 4>{0, 255, 0, 255}, "BC1 horizontal block order");
    check(pixel(cropped, 0, 4) == std::array<std::uint8_t, 4>{0, 0, 255, 255}, "BC1 vertical block order");
    check(pixel(cropped, 4, 6) == std::array<std::uint8_t, 4>{255, 255, 255, 255}, "BC1 lower-right cropped block");
    const auto tiny = decode_bc1(four_color, 1, 1);
    check(tiny.rgba.size() == 4 && tiny.rgba[0] == 255, "BC1 image smaller than one block");
    blocks.push_back(0x7f);
    check(decode_bc1(blocks, 5, 7).rgba == cropped.rgba, "BC1 trailing mip bytes do not affect base level");
    rejects([&] { decode_bc1(Bytes(7), 1, 1); }, "BC1 truncated block");
    rejects([&] { decode_bc1(Bytes(31), 5, 7); }, "BC1 truncated padded image");
    rejects([&] { decode_bc1(blocks, 0, 4); }, "BC1 zero dimension");
    rejects([&] { decode_bc1(blocks, 0xffffffff, 0xffffffff); }, "BC1 overflow dimensions");
    rejects([&] { decode_bc1(blocks, 8192, 8192); }, "BC1 output allocation safety limit");
}

void save_ppm(const std::filesystem::path& path, const RgbaImage& image) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.rgba.size(); i += 4)
        output.write(reinterpret_cast<const char*>(image.rgba.data() + i), 3);
    if (!output) throw std::runtime_error("Could not save optional local preview");
}

void original_sample(const std::filesystem::path& archive_path, std::size_t entry_index,
    const std::filesystem::path& output_path) {
    const auto archive = assets::AfsArchive::open(archive_path);
    const auto document = read_igb_document(archive.read(entry_index));
    const auto images = list_igb_textures(document);
    std::size_t supported = 0;
    bool saved = false;
    for (const auto& info : images) {
        std::cout << "Image ref=" << info.reference_index << " name=" << info.name << " " << info.width << 'x' << info.height
            << " " << info.format_name << " bytes=" << info.encoded_bytes << " " << info.diagnostic << '\n';
        if (!info.supported) continue;
        const auto decoded = decode_igb_texture(document, info.reference_index);
        check(decoded.rgba.size() == static_cast<std::size_t>(info.width) * info.height * 4, "Original texture output size");
        ++supported;
        if (!output_path.empty() && !saved) { save_ppm(output_path, decoded); saved = true; }
    }
    check(supported > 0, "Optional local IGB sample must contain at least one decodable texture");
    std::cout << "Decoded " << supported << '/' << images.size() << " original igImage objects natively.\n";
}

void original_archives(const std::filesystem::path& directory) {
    for (const auto& name : {"area.afs", "ndmw.afs"}) {
        const auto archive = assets::AfsArchive::open(directory / name);
        std::size_t document_count = 0;
        std::size_t image_count = 0;
        std::map<std::string, std::size_t> formats;
        for (std::size_t entry = 0; entry < archive.entries().size(); ++entry) {
            const auto header = archive.read_prefix(entry, 48);
            if (header.size() < 48) continue;
            const bool little_cookie = header[40] == 0xda && header[41] == 0xfa && header[42] == 0 && header[43] == 0;
            const bool big_cookie = header[40] == 0 && header[41] == 0 && header[42] == 0xfa && header[43] == 0xda;
            if (!little_cookie && !big_cookie) continue;
            try {
                const auto document = read_igb_document(archive.read(entry));
                ++document_count;
                for (const auto& info : list_igb_textures(document)) {
                    if (!info.supported) throw TextureError(info.diagnostic);
                    const auto decoded = decode_igb_texture(document, info.reference_index);
                    check(decoded.rgba.size() == static_cast<std::size_t>(info.width) * info.height * 4,
                        "Original corpus texture output size");
                    ++image_count;
                    ++formats[info.format_name];
                }
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string(name) + " entry " + std::to_string(entry) + ": " + error.what());
            }
        }
        check(document_count > 0 && image_count > 0, "Original archive corpus contains no IGB images");
        std::cout << name << ": " << document_count << " IGB documents, " << image_count << " decoded images\n";
        for (const auto& [format, count] : formats) std::cout << "  " << format << ": " << count << '\n';
    }
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        indexed_tests();
        bc1_tests();
        std::cout << "Passed " << checks << " synthetic texture checks.\n";
        if (argc > 1) {
            if (std::filesystem::path(argv[1]) == "--archives") {
                if (argc != 3) throw std::runtime_error("Usage: dw4_textures_tests --archives path/to/extracted/files");
                original_archives(argv[2]);
                return 0;
            }
            if (argc < 3 || argc > 4) throw std::runtime_error("Optional usage: dw4_textures_tests archive.afs entry_index [local_preview.ppm]");
            const auto entry = std::stoull(std::filesystem::path(argv[2]).string());
            original_sample(argv[1], static_cast<std::size_t>(entry), argc == 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Texture tests failed: " << error.what() << '\n';
        return 1;
    }
}
