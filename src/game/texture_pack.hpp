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
    static std::string file_name(const std::string& image_name, int width, int height, std::uint64_t hash);

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
