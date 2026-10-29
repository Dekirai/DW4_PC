#include "presence_art.hpp"

#include "presence_data.hpp"

#include "dw4/igb_document.hpp"
#include "dw4/resources.hpp"
#include "dw4/textures.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace dw4::game {
namespace {

// Bilinear resample of straight RGBA to size x size, the source rows taken
// bottom first (the cards are stored upside down; the renderer flips them).
std::vector<std::uint8_t> square_upright(const RgbaImage& image, int size) {
    std::vector<std::uint8_t> out(std::size_t(size) * std::size_t(size) * 4);
    const int w = int(image.width), h = int(image.height);
    for (int y = 0; y < size; ++y) {
        const float sy = (float(y) + 0.5f) * float(h) / float(size) - 0.5f;
        const int y0 = std::clamp(int(std::floor(sy)), 0, h - 1), y1 = std::min(y0 + 1, h - 1);
        const float fy = std::clamp(sy - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < size; ++x) {
            const float sx = (float(x) + 0.5f) * float(w) / float(size) - 0.5f;
            const int x0 = std::clamp(int(std::floor(sx)), 0, w - 1), x1 = std::min(x0 + 1, w - 1);
            const float fx = std::clamp(sx - float(x0), 0.0f, 1.0f);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int px, int py) { return float(image.rgba[(std::size_t(h - 1 - py) * std::size_t(w) + std::size_t(px)) * 4 + std::size_t(c)]); };
                const float top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
                const float bottom = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
                out[(std::size_t(y) * std::size_t(size) + std::size_t(x)) * 4 + std::size_t(c)] =
                    std::uint8_t(std::clamp(std::lround(top * (1 - fy) + bottom * fy), 0L, 255L));
            }
        }
    }
    return out;
}

} // namespace

int export_presence_art(const std::filesystem::path& data, const std::filesystem::path& folder) {
    const auto resources = MountedResources::open(data);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    int written = 0;
    for (const auto& image : area_images()) {
        const std::string path = std::string("loadingusa/") + image.card + ".igb";
        try {
            const auto document = read_igb_document(resources.read(path));
            const auto textures = list_igb_textures(document);
            const auto found = std::find_if(textures.begin(), textures.end(), [](const TextureInfo& t) { return t.supported && t.width >= 128; });
            if (found == textures.end()) {
                std::cerr << image.key << ": no picture in " << path << '\n';
                continue;
            }
            const auto rgba = decode_igb_texture(document, found->reference_index);
            const auto square = square_upright(rgba, 512);
            const auto file = folder / (std::string(image.key) + ".png");
            if (stbi_write_png(file.string().c_str(), 512, 512, 4, square.data(), 512 * 4)) {
                std::cout << "wrote " << file.string() << " (" << image.title << ", from " << path << ")\n";
                ++written;
            }
        } catch (const std::exception& problem) {
            std::cerr << image.key << ": " << problem.what() << '\n';
        }
    }
    std::cout << written << " area pictures in " << folder.string()
              << ". Upload them in the Discord Developer Portal (your application > Rich Presence > Art Assets) "
                 "under their file names; the form pictures (form_*) are yours to choose.\n";
    return written > 0 ? 0 : 1;
}

} // namespace dw4::game
