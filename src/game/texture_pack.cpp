#include "texture_pack.hpp"

#include "dw4/textures.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace dw4::game {
namespace {

// "<anything>_<16 hex digits>.png" -> the hash.
std::optional<std::uint64_t> hash_of(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".png") return std::nullopt;
    const std::string stem = path.stem().string();
    if (stem.size() < 16) return std::nullopt;
    const std::string digits = stem.substr(stem.size() - 16);
    std::uint64_t value = 0;
    for (const char c : digits) {
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                      : (c >= 'a' && c <= 'f')                  ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F')                  ? c - 'A' + 10
                                                                 : -1;
        if (d < 0) return std::nullopt;
        value = value << 4 | std::uint64_t(d);
    }
    return value;
}

std::string safe_name(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(std::isalnum(u) || c == '-' || c == '.' ? c : '_');
    }
    while (!out.empty() && out.back() == '.') out.pop_back();
    if (out.size() > 48) out.resize(48);
    return out.empty() ? "tex" : out;
}

} // namespace

void TexturePack::configure(const std::filesystem::path& pack, const std::filesystem::path& dump) {
    replacements_.clear();
    std::error_code error;
    if (!pack.empty() && std::filesystem::is_directory(pack, error))
        for (auto it = std::filesystem::recursive_directory_iterator(pack, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            if (it->is_regular_file(error))
                if (const auto h = hash_of(it->path())) replacements_[*h] = it->path();
    dump_ = dump;
    if (!dump_.empty()) std::filesystem::create_directories(dump_, error);
}

std::uint64_t TexturePack::hash(const std::uint8_t* rgba, std::size_t bytes, int width, int height) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint8_t b) {
        h ^= b;
        h *= 0x100000001b3ull;
    };
    for (int shift = 0; shift < 32; shift += 8) mix(std::uint8_t(width >> shift));
    for (int shift = 0; shift < 32; shift += 8) mix(std::uint8_t(height >> shift));
    for (std::size_t i = 0; i < bytes; ++i) mix(rgba[i]);
    return h;
}

std::uint64_t TexturePack::image_hash(const RgbaImage& image) {
    const int width = int(image.width), height = int(image.height);
    if (!image.block_compressed) return hash(image.rgba.data(), image.rgba.size(), width, height);
    auto earlier = image.rgba;
    for (std::size_t i = 0; i + 3 < earlier.size(); i += 4)
        if (earlier[i + 3] == 0) earlier[i] = earlier[i + 1] = earlier[i + 2] = 0;
    return hash(earlier.data(), earlier.size(), width, height);
}

bool TexturePack::write_png(const std::filesystem::path& path, const std::uint8_t* pixels, int width, int height,
                            int channels) {
    return stbi_write_png(path.string().c_str(), width, height, channels, pixels, width * channels) != 0;
}

std::string TexturePack::file_name(const std::string& image_name, int width, int height, std::uint64_t hash) {
    char tail[64];
    std::snprintf(tail, sizeof tail, "_%dx%d_%016llx.png", width, height, static_cast<unsigned long long>(hash));
    return safe_name(image_name) + tail;
}

std::optional<TextureImage> TexturePack::replacement(std::uint64_t hash) const {
    const auto found = replacements_.find(hash);
    if (found == replacements_.end()) return std::nullopt;
    std::ifstream in(found->second, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) return std::nullopt;
    TextureImage image;
    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + std::size_t(width) * std::size_t(height) * 4);
    stbi_image_free(pixels);
    return image;
}

void TexturePack::dump(const std::string& image_name, const std::uint8_t* rgba, int width, int height, std::uint64_t hash) {
    if (dump_.empty() || dumped_[hash]) return;
    dumped_[hash] = true;
    const auto path = dump_ / file_name(image_name, width, height, hash);
    std::error_code error;
    if (std::filesystem::exists(path, error)) return;
    write_png(path, rgba, width, height);
}

} // namespace dw4::game
