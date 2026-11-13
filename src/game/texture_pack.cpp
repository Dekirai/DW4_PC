#include "texture_pack.hpp"

#include "dw4/textures.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>

namespace dw4::game {
namespace {

constexpr std::size_t kMaxManifestBytes = 64u << 20;
constexpr std::size_t kMaxManifestRows = 1'000'000;
constexpr std::size_t kMaxFilesPerTexture = 16;
constexpr std::uintmax_t kMaxPackFileBytes = 512u << 20;

std::optional<std::uint64_t> parse_hash(std::string_view digits) {
    if (digits.size() != 16) return std::nullopt;
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

// "<anything>_<16 hex digits>.png" / ".dds" -> the hash.
std::optional<std::uint64_t> hash_of(const std::filesystem::path& path, bool& dds) {
    std::string ext = path.extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".png" && ext != ".dds") return std::nullopt;
    dds = ext == ".dds";
    const std::string stem = path.stem().string();
    if (stem.size() < 16) return std::nullopt;
    return parse_hash(std::string_view(stem).substr(stem.size() - 16));
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

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const auto tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    for (auto& f : fields) {
        while (!f.empty() && (f.front() == ' ')) f.remove_prefix(1);
        while (!f.empty() && (f.back() == ' ')) f.remove_suffix(1);
    }
    return fields;
}

std::optional<long> parse_int(std::string_view text, long low, long high) {
    if (text.empty() || text.size() > 9) return std::nullopt;
    long value = 0;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
        value = value * 10 + (c - '0');
    }
    if (value < low || value > high) return std::nullopt;
    return value;
}

bool power_of_two(long v) { return v > 0 && (v & (v - 1)) == 0; }

// A manifest path: relative, below the pack folder (no root, no "..").
std::optional<std::filesystem::path> pack_path(std::string_view text, const std::filesystem::path& folder) {
    if (text.empty() || text.size() > 1024) return std::nullopt;
    for (const char c : text)
        if (static_cast<unsigned char>(c) < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return std::nullopt;
    const std::filesystem::path relative(std::u8string(text.begin(), text.end()));
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) return std::nullopt;
    for (const auto& part : relative)
        if (part == "..") return std::nullopt;
    return folder / relative;
}

bool read_range(const std::filesystem::path& path, std::uintmax_t offset, std::size_t size, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(std::streamoff(offset));
    out.resize(size);
    if (size && !in.read(reinterpret_cast<char*>(out.data()), std::streamsize(size))) return false;
    return true;
}

int encoding_rank(TextureEncoding e) {
    switch (e) {
    case TextureEncoding::bc7: return 0;
    case TextureEncoding::bc3: return 1;
    case TextureEncoding::bc1: return 2;
    case TextureEncoding::rgba8: return 3;
    case TextureEncoding::png: return 4;
    default: return 5;
    }
}

// The level of a chain starting at `scale` whose scale is the target, else
// the closest below it, else the last one (`levels` levels).
int level_for(int scale, int levels, int target) {
    if (scale <= 0 || target <= 0) return 0;
    int level = 0;
    while (level + 1 < levels && (scale >> level) > target) ++level;
    return level;
}

// The scale of a loaded picture over the original when it is an exact power
// of two of it in both directions (0 otherwise).
int exact_scale(std::uint32_t width, std::uint32_t height, std::uint32_t original_width, std::uint32_t original_height) {
    if (!original_width || !original_height || width % original_width || height % original_height) return 0;
    const std::uint32_t sx = width / original_width, sy = height / original_height;
    return sx == sy && power_of_two(long(sx)) ? int(sx) : 0;
}

} // namespace

std::optional<PackIndex> parse_pack_manifest(std::string_view text, const std::filesystem::path& folder, std::string& error) {
    if (text.size() > kMaxManifestBytes) {
        error = "the manifest is larger than 64 MiB";
        return std::nullopt;
    }
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    PackIndex index;
    index.format = 0;
    index.folder = folder;
    std::vector<std::string> columns;
    int col_hash = -1, col_encoding = -1, col_scale = -1, col_levels = -1, col_file = -1, col_width = -1, col_height = -1;
    std::set<std::tuple<std::uint64_t, int, int>> seen;
    std::set<std::string> unknown_encodings;
    std::size_t rows = 0, line_number = 0, skipped = 0;
    auto warn = [&](const std::string& message) {
        ++skipped;
        if (index.warnings.size() < 50) index.warnings.push_back(message);
    };
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.find_first_not_of(" \t") == std::string_view::npos) continue;
        const std::string where = "line " + std::to_string(line_number) + ": ";
        if (index.format == 0) {
            // The first line names the format.
            std::string_view first = line;
            while (!first.empty() && (first.back() == ' ' || first.back() == '\t')) first.remove_suffix(1);
            const auto gap = first.find_first_of(" \t");
            const std::string_view tag = first.substr(0, gap);
            const auto value_at = gap == std::string_view::npos ? std::string_view::npos : first.find_first_not_of(" \t", gap);
            const std::string_view rest = value_at == std::string_view::npos ? std::string_view() : first.substr(value_at);
            if (tag != "#dw4pack") {
                error = "the first line is not \"#dw4pack<TAB>2\"";
                return std::nullopt;
            }
            const auto version = parse_int(rest, 0, 999999);
            if (!version || *version < kPackFormat) {
                error = "unknown pack format \"" + std::string(rest) + "\"";
                return std::nullopt;
            }
            if (*version > kPackFormat) {
                error = "pack format " + std::to_string(*version) + " is newer than this build reads (" + std::to_string(kPackFormat) + ")";
                return std::nullopt;
            }
            index.format = int(*version);
            continue;
        }
        if (line.front() == '#') {
            const auto fields = split_tabs(line);
            if (fields.size() >= 2 && fields[0] == "#name") index.name = std::string(fields[1].substr(0, 200));
            continue;
        }
        const auto fields = split_tabs(line);
        if (columns.empty()) {
            for (std::size_t i = 0; i < fields.size(); ++i) {
                std::string name(fields[i]);
                for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
                columns.push_back(name);
                const int at = int(i);
                if (name == "hash") col_hash = at;
                else if (name == "encoding") col_encoding = at;
                else if (name == "scale") col_scale = at;
                else if (name == "levels") col_levels = at;
                else if (name == "file") col_file = at;
                else if (name == "width") col_width = at;
                else if (name == "height") col_height = at;
            }
            std::string missing;
            if (col_hash < 0) missing += " hash";
            if (col_encoding < 0) missing += " encoding";
            if (col_scale < 0) missing += " scale";
            if (col_levels < 0) missing += " levels";
            if (col_file < 0) missing += " file";
            if (!missing.empty()) {
                error = "the column header lacks" + missing;
                return std::nullopt;
            }
            continue;
        }
        if (++rows > kMaxManifestRows) {
            error = "more than 1,000,000 rows";
            return std::nullopt;
        }
        if (fields.size() < columns.size()) {
            warn(where + std::to_string(fields.size()) + " fields, the header has " + std::to_string(columns.size()));
            continue;
        }
        const auto hash = parse_hash(fields[std::size_t(col_hash)]);
        if (!hash) {
            warn(where + "the hash is not 16 hex digits");
            continue;
        }
        PackFile file;
        file.encoding = parse_encoding(fields[std::size_t(col_encoding)]);
        if (file.encoding == TextureEncoding::unknown) {
            // A newer pack's encoding for another device: not an error.
            unknown_encodings.insert(std::string(fields[std::size_t(col_encoding)].substr(0, 32)));
            continue;
        }
        const auto scale = parse_int(fields[std::size_t(col_scale)], 1, 16);
        if (!scale || !power_of_two(*scale)) {
            warn(where + "the scale is not 1, 2, 4, 8 or 16");
            continue;
        }
        const auto levels = parse_int(fields[std::size_t(col_levels)], 1, 15);
        if (!levels) {
            warn(where + "the level count is not 1..15");
            continue;
        }
        file.scale = int(*scale);
        file.levels = int(*levels);
        if (col_width >= 0 && !fields[std::size_t(col_width)].empty() && fields[std::size_t(col_width)] != "0") {
            const auto w = parse_int(fields[std::size_t(col_width)], 1, 16384);
            if (!w) {
                warn(where + "the width is not 1..16384");
                continue;
            }
            file.width = std::uint32_t(*w);
        }
        if (col_height >= 0 && !fields[std::size_t(col_height)].empty() && fields[std::size_t(col_height)] != "0") {
            const auto h = parse_int(fields[std::size_t(col_height)], 1, 16384);
            if (!h) {
                warn(where + "the height is not 1..16384");
                continue;
            }
            file.height = std::uint32_t(*h);
        }
        const auto path = pack_path(fields[std::size_t(col_file)], folder);
        if (!path) {
            warn(where + "the file is not a relative path inside the pack");
            continue;
        }
        file.path = *path;
        if (!seen.insert({*hash, int(file.encoding), file.scale}).second) {
            warn(where + "the same hash, encoding and scale again");
            continue;
        }
        auto& files = index.textures[*hash];
        if (files.size() >= kMaxFilesPerTexture) {
            warn(where + "more than 16 files for one texture");
            continue;
        }
        files.push_back(std::move(file));
    }
    if (index.format == 0) {
        error = "the manifest is empty";
        return std::nullopt;
    }
    if (columns.empty()) {
        error = "the manifest has no column header";
        return std::nullopt;
    }
    for (const auto& name : unknown_encodings)
        index.warnings.push_back("encoding \"" + name + "\" is not read by this build (its rows are skipped)");
    if (skipped > 50) index.warnings.push_back(std::to_string(skipped - 50) + " more rows skipped");
    return index;
}

PackChoice choose_pack_file(const std::vector<PackFile>& files, int target_scale, const EncodingSupport& gpu) {
    PackChoice best;
    std::tuple<int, int, int, int, int> best_key{};
    for (const auto& file : files) {
        if (file.legacy) {
            if (!best.file) best = {&file, 0, 0};
            continue;
        }
        if (file.encoding == TextureEncoding::unknown) continue;
        // A PNG's chain is made by the loader down to 1 x 1.
        int levels = file.levels;
        if (file.encoding == TextureEncoding::png)
            for (levels = 1; (file.scale >> levels) > 0; ++levels) {
            }
        const int level = level_for(file.scale, levels, target_scale);
        const int scale = file.scale >> level;
        const int distance = scale == target_scale ? 0 : scale < target_scale ? 1 : 2;
        const int gap = scale < target_scale ? target_scale - scale : scale - target_scale;
        const std::tuple<int, int, int, int, int> key{distance, gap, gpu.native(file.encoding) ? 0 : 1, encoding_rank(file.encoding), level};
        if (!best.file || key < best_key) {
            best = {&file, level, scale};
            best_key = key;
        }
    }
    return best;
}

std::optional<TexturePayload> load_pack_file(const PackFile& file, int first_level, int target_scale, std::uint32_t original_width,
                                             std::uint32_t original_height, const EncodingSupport& gpu, bool want_rgba,
                                             std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file.path, ec);
    if (ec) {
        error = "cannot read " + file.path.string();
        return std::nullopt;
    }
    if (size > kMaxPackFileBytes) {
        error = file.path.string() + " is larger than 512 MiB";
        return std::nullopt;
    }
    TexturePayload payload;
    const bool png = file.legacy ? !file.dds : file.encoding == TextureEncoding::png;
    if (png) {
        std::vector<std::uint8_t> bytes;
        if (!read_range(file.path, 0, std::size_t(size), bytes)) {
            error = "cannot read " + file.path.string();
            return std::nullopt;
        }
        auto image = TexturePack::read_png(bytes);
        if (!image) {
            error = file.path.string() + " is not a readable PNG";
            return std::nullopt;
        }
        const auto w = std::uint32_t(image->width), h = std::uint32_t(image->height);
        if ((file.width && file.width != w) || (file.height && file.height != h)) {
            error = file.path.string() + " is " + std::to_string(w) + "x" + std::to_string(h) + ", not the manifest's size";
            return std::nullopt;
        }
        const bool larger = original_width && original_height && (w > original_width || h > original_height);
        int scale = file.legacy ? exact_scale(w, h, original_width, original_height) : file.scale;
        payload.encoding = TextureEncoding::rgba8;
        if (!larger) {
            payload.levels.push_back({w, h, 0, image->rgba.size()});
            if (want_rgba) payload.rgba = image->rgba;
            payload.data = std::move(image->rgba);
            payload.scale = scale ? scale : (original_width ? 1 : 0);
            return payload;
        }
        const auto kind = classify_alpha(image->rgba);
        std::vector<TextureLevel> levels;
        auto chain = mip_chain(std::move(image->rgba), w, h, kind, levels);
        const int first = scale ? level_for(scale, int(levels.size()), target_scale) : 0;
        const std::size_t from = levels[std::size_t(first)].offset;
        payload.data.assign(chain.begin() + std::ptrdiff_t(from), chain.end());
        for (std::size_t i = std::size_t(first); i < levels.size(); ++i)
            payload.levels.push_back({levels[i].width, levels[i].height, levels[i].offset - from, levels[i].size});
        payload.scale = scale ? scale >> first : 0;
        if (want_rgba) payload.rgba.assign(payload.data.begin(), payload.data.begin() + std::ptrdiff_t(payload.levels[0].size));
        return payload;
    }
    std::vector<std::uint8_t> header;
    if (!read_range(file.path, 0, std::size_t(std::min<std::uintmax_t>(size, 148)), header)) {
        error = "cannot read " + file.path.string();
        return std::nullopt;
    }
    std::string why;
    const auto info = parse_dds_header(header, std::size_t(size), why);
    if (!info) {
        error = file.path.string() + ": " + why;
        return std::nullopt;
    }
    if (!file.legacy) {
        if (info->encoding != file.encoding) {
            error = file.path.string() + " holds " + encoding_name(info->encoding) + ", the manifest says " + encoding_name(file.encoding);
            return std::nullopt;
        }
        if ((file.width && file.width != info->width) || (file.height && file.height != info->height)) {
            error = file.path.string() + " is " + std::to_string(info->width) + "x" + std::to_string(info->height) + ", not the manifest's size";
            return std::nullopt;
        }
    }
    const int scale = file.legacy ? exact_scale(info->width, info->height, original_width, original_height) : file.scale;
    int first = file.legacy ? level_for(scale, int(info->levels.size()), target_scale) : first_level;
    first = std::clamp(first, 0, int(info->levels.size()) - 1);
    const auto& from = info->levels[std::size_t(first)];
    const auto& last = info->levels.back();
    std::vector<std::uint8_t> data;
    if (!read_range(file.path, from.offset, last.offset + last.size - from.offset, data)) {
        error = "cannot read " + file.path.string();
        return std::nullopt;
    }
    payload.scale = scale ? scale >> first : 0;
    const bool decode = !gpu.native(info->encoding) || info->bgra;
    payload.encoding = decode ? TextureEncoding::rgba8 : info->encoding;
    for (std::size_t i = std::size_t(first); i < info->levels.size(); ++i) {
        const auto& level = info->levels[i];
        const std::span<const std::uint8_t> bytes(data.data() + (level.offset - from.offset), level.size);
        if (decode) {
            std::vector<std::uint8_t> rgba;
            if (!decode_level(info->encoding, bytes, level.width, level.height, info->bgra, rgba)) {
                error = file.path.string() + ": level " + std::to_string(i) + " does not decode";
                return std::nullopt;
            }
            payload.levels.push_back({level.width, level.height, payload.data.size(), rgba.size()});
            payload.data.insert(payload.data.end(), rgba.begin(), rgba.end());
        } else {
            payload.levels.push_back({level.width, level.height, level.offset - from.offset, level.size});
        }
    }
    if (!decode) payload.data = std::move(data);
    if (want_rgba) {
        const auto& level = payload.levels[0];
        const std::span<const std::uint8_t> bytes(payload.data.data() + level.offset, level.size);
        if (!decode_level(payload.encoding, bytes, level.width, level.height, false, payload.rgba)) {
            error = file.path.string() + ": level 0 does not decode";
            return std::nullopt;
        }
    }
    return payload;
}

void TexturePack::configure(const std::filesystem::path& pack, const std::filesystem::path& dump) {
    auto index = std::make_shared<PackIndex>();
    index->folder = pack;
    std::error_code error;
    if (!pack.empty() && std::filesystem::is_directory(pack, error)) {
        const auto manifest = pack / kPackManifest;
        if (std::filesystem::is_regular_file(manifest, error)) {
            std::ifstream in(manifest, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), {});
            std::string why;
            if (auto parsed = parse_pack_manifest(text, pack, why)) *index = std::move(*parsed);
            else index->error = std::string(kPackManifest) + ": " + why;
            index->folder = pack;
        } else {
            index->format = 1;
            for (auto it = std::filesystem::recursive_directory_iterator(pack, error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                if (it->is_regular_file(error)) {
                    bool dds = false;
                    if (const auto h = hash_of(it->path(), dds)) {
                        PackFile file;
                        file.legacy = true;
                        file.dds = dds;
                        file.encoding = dds ? TextureEncoding::unknown : TextureEncoding::png;
                        file.path = it->path();
                        // (One file per hash; a later one of the same hash replaces it, as before.)
                        index->textures[*h] = {std::move(file)};
                    }
                }
        }
    }
    index_ = std::move(index);
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

std::optional<TextureImage> TexturePack::read_png(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > std::size_t(kMaxPackFileBytes)) return std::nullopt;
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels) || width <= 0 || height <= 0 ||
        width > 16384 || height > 16384)
        return std::nullopt;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) return std::nullopt;
    TextureImage image;
    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + std::size_t(width) * std::size_t(height) * 4);
    stbi_image_free(pixels);
    return image;
}

std::string TexturePack::file_name(const std::string& image_name, int width, int height, std::uint64_t hash) {
    char tail[64];
    std::snprintf(tail, sizeof tail, "_%dx%d_%016llx.png", width, height, static_cast<unsigned long long>(hash));
    return safe_name(image_name) + tail;
}

std::optional<TextureImage> TexturePack::replacement(std::uint64_t hash) const {
    const auto found = index_->textures.find(hash);
    if (found == index_->textures.end()) return std::nullopt;
    // The largest level the pack has, decoded to RGBA8 on the CPU.
    const EncodingSupport cpu{false, false, false};
    const auto choice = choose_pack_file(found->second, 1 << 16, cpu);
    if (!choice.file) return std::nullopt;
    std::string error;
    auto payload = load_pack_file(*choice.file, choice.first_level, 1 << 16, 0, 0, cpu, true, error);
    if (!payload || payload->levels.empty()) return std::nullopt;
    TextureImage image;
    image.width = int(payload->levels[0].width);
    image.height = int(payload->levels[0].height);
    image.rgba = std::move(payload->rgba);
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
