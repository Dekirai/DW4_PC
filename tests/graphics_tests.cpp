// Texture pack format 2 and the Graphics options (docs/TEXTURES.md), without
// GL: the manifest (valid, malformed, newer, unsafe paths), DDS headers
// (valid and malformed), the CPU decoders (BC7 bit exact against
// DirectXTex's decode of blocks of every mode and partition; BC1 / BC3),
// mip chains, the file choice per quality, the pack file loader, the
// streaming thread (requests, deliveries, reconfiguration, documents; with
// the game data also a real document's scan and read-ahead), the memory
// budget's evictions and the options' names, environment and settings.ini
// round trip. Run one part: dw4_graphics_tests PART [DATA].
#include "../src/game/graphics_options.hpp"
#include "../src/game/settings.hpp"
#include "../src/game/texture_codec.hpp"
#include "../src/game/texture_pack.hpp"
#include "../src/game/texture_stream.hpp"

#include "dw4/igb_document.hpp"
#include "dw4/resources.hpp"
#include "dw4/textures.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace dw4;
using namespace dw4::game;
using Bytes = std::vector<std::uint8_t>;
int checks = 0;

void check(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

std::uint64_t fnv(const Bytes& bytes) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const auto b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

struct Rng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

Bytes random_bytes(std::size_t size, std::uint64_t seed) {
    Rng r{seed};
    Bytes out(size);
    for (std::size_t i = 0; i < size; i += 8) {
        const auto v = r.next();
        std::memcpy(out.data() + i, &v, std::min<std::size_t>(8, size - i));
    }
    return out;
}

// A chain of random blocks for every level of a texture (any 16-byte block
// with a mode bit is a valid BC7 block).
std::vector<Bytes> random_chain(TextureEncoding encoding, std::uint32_t width, std::uint32_t height, int levels, std::uint64_t seed) {
    std::vector<Bytes> out;
    for (int i = 0; i < levels; ++i) {
        const auto w = std::max<std::uint32_t>(width >> i, 1), h = std::max<std::uint32_t>(height >> i, 1);
        out.push_back(random_bytes(level_bytes(encoding, w, h), seed + std::uint64_t(i)));
        if (encoding == TextureEncoding::bc7)
            for (std::size_t b = 0; b < out.back().size(); b += 16)
                if (out.back()[b] == 0) out.back()[b] = 0x40; // (mode 6, not the reserved one)
    }
    return out;
}

void write_file(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::filesystem::path temp_root(const char* part) {
    const auto root = std::filesystem::temp_directory_path() / ("dw4_graphics_tests_" + std::string(part) + "_" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

// ------------------------------------------------------------- manifest

void manifest_tests() {
    const std::filesystem::path folder = "C:/packs/remaster";
    std::string error;
    // A valid manifest: BOM, CRLF, a name, an extra column, an encoding of a
    // later build (ASTC for GLES devices) that is skipped, not refused.
    const std::string text = "\xEF\xBB\xBF#dw4pack\t2\r\n#name\tPilot pack\r\n# a comment\r\n\r\n"
                             "hash\tencoding\tscale\tlevels\twidth\theight\tfile\tmodel\r\n"
                             "05f8453aab0ffa8d\tbc7\t4\t11\t1024\t512\ttex/05f8453aab0ffa8d.dds\tpbrify\r\n"
                             "05f8453aab0ffa8d\tastc6x6\t4\t11\t1024\t512\ttex/05f8453aab0ffa8d.ktx2\tpbrify\r\n"
                             "05F8453AAB0FFA8D\tbc1\t2\t10\t512\t256\ttex/05f8_2x.dds\tpbrify\r\n"
                             "ac566f5a15c2a226\trgba8\t4\t1\t\t\tui/mojigc.dds\tus2\r\n"
                             "faccbeda501eefc1\tpng\t4\t1\t2048\t2048\tfont/font00.png\tpathb\r\n";
    auto index = parse_pack_manifest(text, folder, error);
    check(index.has_value(), "valid manifest read: " + error);
    check(index->format == 2 && index->name == "Pilot pack", "format and name");
    check(index->textures.size() == 3, "three textures (the ASTC row skipped)");
    const auto& rows = index->textures.at(0x05f8453aab0ffa8dull);
    check(rows.size() == 2 && rows[0].encoding == TextureEncoding::bc7 && rows[0].scale == 4 && rows[0].levels == 11 &&
              rows[0].width == 1024 && rows[0].height == 512,
          "bc7 row fields");
    check(rows[0].path == folder / "tex/05f8453aab0ffa8d.dds", "the file below the pack folder");
    check(rows[1].encoding == TextureEncoding::bc1 && rows[1].scale == 2, "upper-case hash, second row of the hash");
    check(index->textures.at(0xac566f5a15c2a226ull)[0].width == 0, "empty width: unknown");
    check(index->other_encodings == std::vector<std::string>{"astc6x6"} && index->warnings.empty(),
          "the skipped encoding is named as another device's, not as a warning");
    // Refused manifests.
    auto refused = [&](const std::string& t, const std::string& why) {
        std::string e;
        check(!parse_pack_manifest(t, folder, e) && !e.empty(), why);
    };
    refused("", "empty");
    refused("hash\tencoding\n", "no format line");
    refused("#dw4pack\t1\nhash\tencoding\tscale\tlevels\tfile\n", "format 1 has no manifest");
    refused("#dw4pack\t3\nhash\tencoding\tscale\tlevels\tfile\n", "a newer format");
    refused("#dw4pack\tx\nhash\tencoding\tscale\tlevels\tfile\n", "a format that is no number");
    refused("#dw4pack\t2\nhash\tencoding\tscale\tfile\n", "no levels column");
    refused("#dw4pack\t2\n#name\tx\n", "no column header");
    // Malformed rows are skipped, each with a warning; good rows stay.
    const std::string rows_text = "#dw4pack 2\nhash\tencoding\tscale\tlevels\tfile\n"
                                  "05f8453aab0ffa8\tbc7\t4\t1\ta.dds\n"      // 15 digits
                                  "05f8453aab0ffa8g\tbc7\t4\t1\ta.dds\n"     // not hex
                                  "05f8453aab0ffa8d\tbc7\t3\t1\ta.dds\n"     // scale 3
                                  "05f8453aab0ffa8d\tbc7\t0\t1\ta.dds\n"     // scale 0
                                  "05f8453aab0ffa8d\tbc7\t32\t1\ta.dds\n"    // scale 32
                                  "05f8453aab0ffa8d\tbc7\t4\t0\ta.dds\n"     // levels 0
                                  "05f8453aab0ffa8d\tbc7\t4\t16\ta.dds\n"    // levels 16
                                  "05f8453aab0ffa8d\tbc7\t4\t1\tC:/a.dds\n"  // absolute
                                  "05f8453aab0ffa8d\tbc7\t4\t1\t/a.dds\n"    // rooted
                                  "05f8453aab0ffa8d\tbc7\t4\t1\t../a.dds\n"  // outside
                                  "05f8453aab0ffa8d\tbc7\t4\t1\tx/../../a.dds\n"
                                  "05f8453aab0ffa8d\tbc7\t4\t1\ta|b.dds\n"
                                  "05f8453aab0ffa8d\tbc7\t4\n"               // too few fields
                                  "05f8453aab0ffa8d\tbc7\t4\t1\tgood.dds\n"
                                  "05f8453aab0ffa8d\tbc7\t4\t2\tagain.dds\n"; // same hash, encoding and scale
    index = parse_pack_manifest(rows_text, folder, error);
    check(index.has_value(), "rows manifest read (spaces after #dw4pack): " + error);
    check(index->textures.size() == 1 && index->textures.begin()->second.size() == 1 &&
              index->textures.begin()->second[0].path == folder / "good.dds",
          "only the good row");
    check(index->warnings.size() == 14, "a warning per bad row (" + std::to_string(index->warnings.size()) + ")");
    // A width that is no number is a bad row too.
    index = parse_pack_manifest("#dw4pack\t2\nhash\tencoding\tscale\tlevels\twidth\tfile\n05f8453aab0ffa8d\tbc7\t4\t1\tabc\ta.dds\n", folder, error);
    check(index && index->textures.empty() && index->warnings.size() == 1, "bad width");
}

// ------------------------------------------------------------------ DDS

void dds_tests() {
    std::string error;
    for (const auto encoding : {TextureEncoding::bc1, TextureEncoding::bc3, TextureEncoding::bc7, TextureEncoding::rgba8})
        for (const bool dx10 : {false, true}) {
            const auto chain = random_chain(encoding, 20, 12, 5, 7);
            const auto file = make_dds(encoding, 20, 12, chain, dx10);
            const auto info = parse_dds_header(file, file.size(), error);
            const std::string what = std::string(encoding_name(encoding)) + (dx10 ? " dx10" : " legacy");
            check(info.has_value(), what + ": read (" + error + ")");
            check(info->encoding == encoding && info->width == 20 && info->height == 12 && !info->bgra, what + ": header");
            check(info->levels.size() == 5 && info->levels[4].width == 1 && info->levels[4].height == 1, what + ": levels");
            std::size_t offset = info->data_offset;
            for (std::size_t i = 0; i < 5; ++i) {
                check(info->levels[i].offset == offset && info->levels[i].size == chain[i].size(), what + ": level place");
                check(std::equal(chain[i].begin(), chain[i].end(), file.begin() + std::ptrdiff_t(offset)), what + ": level bytes");
                offset += chain[i].size();
            }
            check(offset == file.size(), what + ": nothing after the last level");
        }
    // Partial blocks: a 5x3 BC1 level is 2x1 blocks.
    check(level_bytes(TextureEncoding::bc1, 5, 3) == 16 && level_bytes(TextureEncoding::bc7, 1, 1) == 16 &&
              level_bytes(TextureEncoding::rgba8, 5, 3) == 60,
          "level sizes");
    const auto good = make_dds(TextureEncoding::bc7, 8, 8, random_chain(TextureEncoding::bc7, 8, 8, 4, 1), true);
    auto bad = [&](Bytes file, std::size_t size, const std::string& why) {
        std::string e;
        check(!parse_dds_header(file, size, e) && !e.empty(), why);
    };
    auto put = [](Bytes file, std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) file[at + std::size_t(i)] = std::uint8_t(v >> (8 * i));
        return file;
    };
    bad(Bytes(good.begin(), good.begin() + 100), 100, "shorter than a header");
    bad(put(good, 0, 0x20534444u ^ 1u), good.size(), "magic");
    bad(put(good, 4, 100), good.size(), "header size");
    bad(put(good, 76, 30), good.size(), "pixel format size");
    bad(Bytes(good.begin(), good.begin() + 140), 140, "DX10 header cut");
    bad(put(good, 112, 0x200), good.size(), "cube map");
    bad(put(good, 24, 4), good.size(), "volume");
    bad(put(good, 140, 6), good.size(), "texture array");
    bad(put(good, 136, 4), good.size(), "DX10 cube flag");
    bad(put(good, 132, 2), good.size(), "1D texture");
    bad(put(good, 128, 95), good.size(), "BC6H (unsupported DXGI format)");
    bad(put(good, 16, 0), good.size(), "zero width");
    bad(put(good, 16, 16385), good.size(), "width above 16384");
    bad(put(good, 28, 5), good.size(), "more levels than 8x8 has");
    bad(put(good, 28, 20), good.size(), "20 levels");
    bad(good, good.size() - 1, "the last level runs past the end");
    {
        const auto legacy = make_dds(TextureEncoding::bc1, 8, 8, random_chain(TextureEncoding::bc1, 8, 8, 1, 2), false);
        bad(put(legacy, 84, 0x33545844u), legacy.size(), "DXT3 FourCC");
    }
    {
        // 32-bit masks: BGRA is read with the swap flag; no alpha mask is refused.
        auto rgba = make_dds(TextureEncoding::rgba8, 2, 2, random_chain(TextureEncoding::rgba8, 2, 2, 1, 3), false);
        auto bgra = put(put(rgba, 92, 0xff0000u), 100, 0xffu);
        const auto info = parse_dds_header(bgra, bgra.size(), error);
        check(info && info->encoding == TextureEncoding::rgba8 && info->bgra, "BGRA masks");
        bad(put(rgba, 104, 0), rgba.size(), "no alpha mask");
        auto dx10_bgra = put(make_dds(TextureEncoding::rgba8, 2, 2, random_chain(TextureEncoding::rgba8, 2, 2, 1, 3), true), 128, 87);
        const auto info2 = parse_dds_header(dx10_bgra, dx10_bgra.size(), error);
        check(info2 && info2->bgra, "DXGI B8G8R8A8");
        auto srgb = put(good, 128, 99);
        const auto info3 = parse_dds_header(srgb, srgb.size(), error);
        check(info3 && info3->encoding == TextureEncoding::bc7, "BC7_UNORM_SRGB reads as BC7");
    }
}

// ---------------------------------------------------------------- codec

void codec_tests() {
    // BC7: 256 random blocks over every mode (the partition, rotation, index
    // selection, endpoints and indices random) against DirectXTex's decoder
    // (texconv 2024.10.29 -f R8G8B8A8_UNORM of the same 64x64 file: the FNV-1a
    // of its RGBA8 rows). BC7 decoding is exact integer arithmetic, so the
    // whole picture must match bit for bit.
    {
        Rng r{0x9e3779b97f4a7c15ull};
        Bytes blocks(256 * 16);
        for (int i = 0; i < 256; ++i) {
            std::uint64_t lo = r.next();
            const std::uint64_t hi = r.next();
            const int mode = i % 8;
            lo &= ~((1ull << (mode + 1)) - 1);
            lo |= 1ull << mode;
            std::memcpy(&blocks[std::size_t(i) * 16], &lo, 8);
            std::memcpy(&blocks[std::size_t(i) * 16 + 8], &hi, 8);
        }
        Bytes rgba;
        check(decode_bc7(blocks, 64, 64, rgba), "BC7 fixture decodes");
        check(fnv(rgba) == 0x0595996f1b4ffe53ull, "BC7 matches DirectXTex for every mode and partition");
        Bytes cut;
        check(decode_bc7(blocks, 61, 62, cut) && cut.size() == 61 * 62 * 4, "partial blocks cropped");
        for (int y = 0; y < 62; ++y) check(std::equal(cut.begin() + y * 61 * 4, cut.begin() + (y + 1) * 61 * 4, rgba.begin() + y * 64 * 4), "crop rows");
        check(!decode_bc7(Bytes(blocks.begin(), blocks.end() - 1), 64, 64, rgba), "BC7: too few blocks");
        std::uint8_t reserved[16]{}, out[64];
        std::memset(out, 7, sizeof out);
        decode_bc7_block(reserved, out);
        check(std::all_of(out, out + 64, [](std::uint8_t v) { return v == 0; }), "BC7 reserved mode: transparent black");
    }
    // BC1: four colours (c0 > c1), three colours plus transparent black.
    {
        const Bytes four{0x00, 0xf8, 0x1f, 0x00, 0xe4, 0xe4, 0xe4, 0xe4}; // red, blue; rows 0,1,2,3 selectors
        Bytes rgba;
        check(decode_bc1(four, 4, 4, rgba), "BC1 decodes");
        // 0xe4 = selectors 0, 1, 2, 3 for the row's texels: red, blue, 2/3 red, 1/3 red.
        const Bytes row{255, 0, 0, 255, 0, 0, 255, 255, 170, 0, 85, 255, 85, 0, 170, 255};
        check(std::equal(row.begin(), row.end(), rgba.begin()), "BC1 four-colour row");
    }
    {
        const Bytes solid_red{0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0};
        Bytes rgba;
        decode_bc1(solid_red, 4, 4, rgba);
        check(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255, "BC1 solid red");
        const Bytes transparent{0x00, 0x00, 0x1f, 0x00, 0xff, 0xff, 0xff, 0xff}; // c0 < c1, selector 3
        decode_bc1(transparent, 4, 4, rgba);
        check(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 0, "BC1 three-colour block: selector 3 transparent black");
        const Bytes half{0x00, 0x00, 0x1f, 0x00, 0xaa, 0xaa, 0xaa, 0xaa}; // selector 2: the midpoint
        decode_bc1(half, 4, 4, rgba);
        check(rgba[0] == 0 && rgba[2] == 128 && rgba[3] == 255, "BC1 three-colour midpoint");
        check(!decode_bc1(Bytes(7), 4, 4, rgba), "BC1: a cut block");
    }
    // BC3: the alpha block's eight and six step ramps; the colour block is
    // always read with four colours.
    {
        Bytes block(16, 0);
        block[0] = 255;
        block[1] = 0;
        // selectors: texel 0 -> 1 (a1 = 0), texel 1 -> 2 ((6*255 + 0 + 3) / 7 = 219), texel 2 -> 7 ((255 + 6*0 + 3) / 7 = 36)
        const std::uint64_t sel = 1ull | 2ull << 3 | 7ull << 6;
        for (int i = 0; i < 6; ++i) block[2 + std::size_t(i)] = std::uint8_t(sel >> (8 * i));
        block[8] = 0x00; block[9] = 0x00; block[10] = 0x1f; block[11] = 0x00; // c0 < c1, still four colours
        block[12] = 0xff;                                                       // texel 0..3: selector 3
        Bytes rgba;
        check(decode_bc3(block, 4, 4, rgba), "BC3 decodes");
        check(rgba[3] == 0 && rgba[7] == 219 && rgba[11] == 36, "BC3 eight-step alpha");
        check(rgba[0] == 0 && rgba[2] == 170 && rgba[1] == 0, "BC3 colour selector 3 = (c0 + 2 c1) / 3, not transparent");
        block[0] = 0;
        block[1] = 255;
        for (int i = 0; i < 6; ++i) block[2 + std::size_t(i)] = 0;
        const std::uint64_t six = 6ull | 7ull << 3 | 2ull << 6;
        for (int i = 0; i < 6; ++i) block[2 + std::size_t(i)] = std::uint8_t(six >> (8 * i));
        decode_bc3(block, 4, 4, rgba);
        check(rgba[3] == 0 && rgba[7] == 255 && rgba[11] == 51, "BC3 six-step alpha with 0 and 255");
    }
    {
        Bytes bgra{1, 2, 3, 4, 5, 6, 7, 8}, out;
        check(decode_level(TextureEncoding::rgba8, bgra, 2, 1, true, out) && out == Bytes{3, 2, 1, 4, 7, 6, 5, 8}, "BGRA swapped");
        check(!decode_level(TextureEncoding::rgba8, bgra, 3, 1, false, out), "RGBA8 too short");
        check(!decode_level(TextureEncoding::png, bgra, 2, 1, false, out), "PNG is no level encoding");
    }
    check(parse_encoding("BC7") == TextureEncoding::bc7 && parse_encoding("astc4x4") == TextureEncoding::unknown, "encoding names");
}

// ----------------------------------------------------------------- mips

void mip_tests() {
    check(classify_alpha(Bytes{1, 2, 3, 255, 4, 5, 6, 255}) == AlphaKind::opaque, "opaque");
    check(classify_alpha(Bytes{1, 2, 3, 0, 4, 5, 6, 255}) == AlphaKind::cutout, "cutout");
    check(classify_alpha(Bytes{1, 2, 3, 128, 4, 5, 6, 255}) == AlphaKind::blend, "blend");
    // A transparent black texel does not darken the colour.
    const Bytes quad{200, 100, 50, 255, 0, 0, 0, 0, 200, 100, 50, 255, 200, 100, 50, 255};
    auto half = half_level(quad, 2, 2, AlphaKind::cutout);
    check(half.size() == 4 && half[0] == 200 && half[1] == 100 && half[2] == 50 && half[3] == 255, "cutout: colour by alpha, three of four opaque");
    const Bytes thin{9, 9, 9, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    half = half_level(thin, 2, 2, AlphaKind::cutout);
    check(half[0] == 9 && half[3] == 0, "cutout: one of four stays transparent (binary alpha)");
    half = half_level(thin, 2, 2, AlphaKind::blend);
    check(half[3] == 64, "blend: averaged alpha");
    half = half_level(Bytes(3 * 4, 255), 3, 1, AlphaKind::opaque);
    check(half.size() == 4, "3x1 -> 1x1");
    std::vector<TextureLevel> levels;
    const auto chain = mip_chain(Bytes(8 * 4 * 4, 77), 8, 4, AlphaKind::opaque, levels);
    check(levels.size() == 4 && levels[3].width == 1 && levels[3].height == 1, "8x4 has four levels");
    std::size_t at = 0;
    for (const auto& l : levels) {
        check(l.offset == at && l.size == std::size_t(l.width) * l.height * 4, "levels follow each other");
        at += l.size;
    }
    check(chain.size() == at && std::all_of(chain.begin(), chain.end(), [](std::uint8_t v) { return v == 77; }), "flat colour stays");
    check(mip_chain(Bytes(3), 8, 4, AlphaKind::opaque, levels).empty() && levels.empty(), "short picture: no chain");
}

// --------------------------------------------------------------- choice

void choice_tests() {
    std::vector<PackFile> files(4);
    files[0].encoding = TextureEncoding::bc7, files[0].scale = 4, files[0].levels = 9;
    files[1].encoding = TextureEncoding::png, files[1].scale = 4, files[1].levels = 1;
    files[2].encoding = TextureEncoding::bc1, files[2].scale = 2, files[2].levels = 8;
    files[3].encoding = TextureEncoding::unknown, files[3].scale = 4, files[3].levels = 9;
    const EncodingSupport all;
    auto choice = choose_pack_file(files, 4, all);
    check(choice.file == &files[0] && choice.first_level == 0 && choice.scale == 4, "Ultra: the BC7 file's level 0");
    choice = choose_pack_file(files, 2, all);
    check(choice.file == &files[0] && choice.first_level == 1 && choice.scale == 2, "High: the BC7 file's level 1 (BC7 before BC1)");
    const EncodingSupport no_bc7{true, true, false};
    choice = choose_pack_file(files, 2, no_bc7);
    check(choice.file == &files[2] && choice.first_level == 0, "High without BC7 on the GPU: the BC1 2x file");
    choice = choose_pack_file(files, 4, no_bc7);
    check(choice.file == &files[1] && choice.scale == 4, "Ultra without BC7: the 4x PNG (the GPU reads RGBA8)");
    files.erase(files.begin() + 1);
    choice = choose_pack_file(files, 4, no_bc7);
    check(choice.file == &files[0] && choice.scale == 4, "Ultra without BC7 and PNG: the 4x BC7 level still (decoded on the CPU)");
    choice = choose_pack_file(files, 8, all);
    check(choice.scale == 4, "8x asked: the largest there is");
    std::vector<PackFile> small(1);
    small[0].encoding = TextureEncoding::bc7, small[0].scale = 2, small[0].levels = 1;
    choice = choose_pack_file(small, 4, all);
    check(choice.file && choice.scale == 2, "Ultra with a 2x pack: 2x");
    small[0].scale = 8;
    choice = choose_pack_file(small, 2, all);
    check(choice.file && choice.first_level == 0 && choice.scale == 8, "a one-level 8x file: level 0 (nothing smaller)");
    check(!choose_pack_file({files[3]}, 4, all).file, "only unknown encodings: none");
    check(!choose_pack_file({}, 4, all).file, "no files: none");
    PackFile legacy;
    legacy.legacy = true;
    std::vector<PackFile> loose{legacy};
    check(choose_pack_file(loose, 2, all).file == &loose[0], "a loose file");
}

// --------------------------------------------------------------- loader

void loader_tests() {
    const auto root = temp_root("loader");
    std::string error;
    // A 16x16 original's 4x BC7 file with its whole chain (64 .. 1: 7 levels).
    const auto chain = random_chain(TextureEncoding::bc7, 64, 64, 7, 11);
    const auto dds = make_dds(TextureEncoding::bc7, 64, 64, chain, true);
    write_file(root / "tex" / "a.dds", dds);
    PackFile file;
    file.encoding = TextureEncoding::bc7;
    file.scale = 4;
    file.levels = 7;
    file.width = 64;
    file.height = 64;
    file.path = root / "tex" / "a.dds";
    const EncodingSupport all;
    auto payload = load_pack_file(file, 0, 4, 16, 16, all, false, error);
    check(payload && payload->encoding == TextureEncoding::bc7 && payload->levels.size() == 7 && payload->scale == 4, "Ultra: all levels");
    Bytes joined;
    for (const auto& l : chain) joined.insert(joined.end(), l.begin(), l.end());
    check(payload->data == joined && payload->levels[1].offset == chain[0].size(), "the levels' bytes as stored");
    payload = load_pack_file(file, 1, 2, 16, 16, all, false, error);
    check(payload && payload->levels.size() == 6 && payload->levels[0].width == 32 && payload->scale == 2 &&
              payload->data.size() == joined.size() - chain[0].size(),
          "High: level 0 is not read");
    // Without BC7 on the GPU: RGBA8 levels decoded here.
    const EncodingSupport no_bc7{true, true, false};
    payload = load_pack_file(file, 0, 4, 16, 16, no_bc7, true, error);
    Bytes expected;
    decode_bc7(chain[0], 64, 64, expected);
    check(payload && payload->encoding == TextureEncoding::rgba8 && payload->levels.size() == 7 &&
              payload->levels[0].size == 64 * 64 * 4 && std::equal(expected.begin(), expected.end(), payload->data.begin()),
          "CPU decode of BC7");
    check(payload->rgba == expected, "level 0 as RGBA8 when asked for");
    // Mismatches and broken files.
    PackFile wrong = file;
    wrong.encoding = TextureEncoding::bc3;
    check(!load_pack_file(wrong, 0, 4, 16, 16, all, false, error) && error.find("manifest") != std::string::npos, "encoding mismatch");
    wrong = file;
    wrong.width = 128;
    check(!load_pack_file(wrong, 0, 4, 16, 16, all, false, error), "size mismatch");
    write_file(root / "tex" / "cut.dds", Bytes(dds.begin(), dds.end() - 5));
    wrong = file;
    wrong.path = root / "tex" / "cut.dds";
    check(!load_pack_file(wrong, 0, 4, 16, 16, all, false, error), "truncated file");
    wrong.path = root / "tex" / "missing.dds";
    check(!load_pack_file(wrong, 0, 4, 16, 16, all, false, error), "missing file");
    write_file(root / "tex" / "junk.png", Bytes{1, 2, 3, 4, 5});
    wrong = file;
    wrong.encoding = TextureEncoding::png;
    wrong.path = root / "tex" / "junk.png";
    check(!load_pack_file(wrong, 0, 4, 16, 16, all, false, error), "unreadable PNG");
    // A PNG 4x: a mip chain made here, High from its level 1.
    Bytes pixels(64 * 64 * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = std::uint8_t(i * 7 + (i >> 8));
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    std::filesystem::create_directories(root / "p");
    check(TexturePack::write_png(root / "p" / "x_16x16_0123456789abcdef.png", pixels.data(), 64, 64), "PNG written");
    PackFile png;
    png.encoding = TextureEncoding::png;
    png.scale = 4;
    png.path = root / "p" / "x_16x16_0123456789abcdef.png";
    payload = load_pack_file(png, 0, 4, 16, 16, all, false, error);
    check(payload && payload->encoding == TextureEncoding::rgba8 && payload->levels.size() == 7 &&
              std::equal(pixels.begin(), pixels.end(), payload->data.begin()),
          "PNG Ultra: level 0 as stored, mips made");
    payload = load_pack_file(png, 0, 2, 16, 16, all, false, error);
    check(payload && payload->levels.size() == 6 && payload->levels[0].width == 32 && payload->scale == 2, "PNG High: from level 1");
    // Loose files (format 1): the scale comes from the sizes.
    PackFile loose;
    loose.legacy = true;
    loose.path = png.path;
    payload = load_pack_file(loose, 0, 2, 16, 16, all, false, error);
    check(payload && payload->levels[0].width == 32 && payload->scale == 2, "loose PNG at High");
    payload = load_pack_file(loose, 0, 4, 64, 64, all, false, error);
    check(payload && payload->levels.size() == 1, "a loose PNG not larger than the original: one level");
    loose.dds = true;
    loose.path = file.path;
    payload = load_pack_file(loose, 0, 2, 32, 32, all, false, error);
    check(payload && payload->levels[0].width == 64 && payload->scale == 2, "loose 2x DDS at High: level 0");
    payload = load_pack_file(loose, 0, 2, 16, 16, all, false, error);
    check(payload && payload->levels[0].width == 32, "loose 4x DDS at High: level 1");
    // TexturePack over both layouts.
    write_text(root / "pack2" / kPackManifest, "#dw4pack\t2\nhash\tencoding\tscale\tlevels\tfile\n0123456789abcdef\tbc7\t4\t7\ta.dds\n");
    write_file(root / "pack2" / "a.dds", dds);
    TexturePack pack;
    pack.configure(root / "pack2", {});
    check(pack.index().format == 2 && pack.replacement_count() == 1 && pack.index().error.empty(), "format 2 folder");
    const auto image = pack.replacement(0x0123456789abcdefull);
    check(image && image->width == 64 && image->rgba == expected, "replacement(): level 0 decoded");
    pack.configure(root / "p", {});
    check(pack.index().format == 1 && pack.replacement_count() == 1, "format 1 folder (loose PNG)");
    write_text(root / "bad" / kPackManifest, "#dw4pack\t9\n");
    pack.configure(root / "bad", {});
    check(pack.replacement_count() == 0 && pack.index().error.find("newer") != std::string::npos, "a refused manifest: no textures, the reason kept");
    // A folder of loose files is walked up to a number of entries (a drive's
    // root given by mistake is not read) and folder levels.
    for (int i = 0; i < 12; ++i) write_text(root / "many" / ("x" + std::to_string(i) + "_000000000000000" + std::to_string(i % 10) + ".png"), "");
    auto many = read_pack_folder(root / "many", 20);
    check(many.format == 1 && many.textures.size() == 10 && many.error.empty(), "12 entries within a limit of 20");
    many = read_pack_folder(root / "many", 8);
    check(many.textures.empty() && many.error.find("more than 8") != std::string::npos, "past the limit: not read, the reason said");
    std::filesystem::path deep = root / "deep";
    for (int level = 0; level < kMaxLooseDepth + 2; ++level) {
        deep /= "d";
        char name[64];
        std::snprintf(name, sizeof name, "t_%016x.png", level + 1);
        write_text(deep / name, "");
    }
    check(read_pack_folder(root / "deep").textures.size() == std::size_t(kMaxLooseDepth),
          "files below kMaxLooseDepth folder levels are not looked for");
    check(read_pack_folder(root / "nothing").textures.empty() && read_pack_folder(root / "nothing").error.empty(),
          "a folder that does not exist: an empty pack, no error");
    // The F1 menu's reload on a thread: handed over once, the latest of two
    // reloads asked for at once wins.
    {
        PackLoader loader;
        check(!loader.take(), "nothing read before a start");
        loader.start(root / "pack2", {});
        loader.start(root / "p", {});
        std::optional<TexturePack> read;
        for (int i = 0; i < 1000 && (loader.busy() || !read); ++i) {
            if (auto r = loader.take()) read = std::move(r);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (auto r = loader.take()) read = std::move(r);
        check(!loader.busy() && read && read->index().format == 1 && read->replacement_count() == 1, "the second folder read last");
        check(!loader.take(), "handed over once");
        loader.start(root / "pack2", {});
    } // (the destructor waits for that read)
    std::filesystem::remove_all(root);
}

// --------------------------------------------------------------- stream

TexturePayload take_one(TextureStreamer& streamer) {
    for (int i = 0; i < 500; ++i) {
        if (auto p = streamer.take()) return std::move(*p);
        streamer.wait_idle(10);
    }
    throw std::runtime_error("no payload arrived");
}

void stream_tests(const std::string& data) {
    const auto root = temp_root("stream");
    const auto dds = make_dds(TextureEncoding::bc7, 64, 64, random_chain(TextureEncoding::bc7, 64, 64, 7, 5), true);
    write_file(root / "a.dds", dds);
    write_text(root / kPackManifest, "#dw4pack\t2\nhash\tencoding\tscale\tlevels\tfile\n"
                                     "0000000000000001\tbc7\t4\t7\ta.dds\n"
                                     "0000000000000002\tbc7\t4\t7\tmissing.dds\n");
    TexturePack pack;
    pack.configure(root, {});
    TextureStreamer streamer;
    check(!streamer.active(), "not active before a pack");
    TextureStreamer::Config config;
    config.pack = pack.shared_index();
    config.target_scale = 4;
    streamer.configure(config);
    check(streamer.active(), "active with a pack");
    streamer.request(1, 16, 16);
    auto payload = take_one(streamer);
    check(payload.hash == 1 && payload.levels.size() == 7 && payload.generation == streamer.generation(), "requested payload");
    streamer.request(1, 16, 16);
    check(streamer.wait_idle(2000) && !streamer.take(), "delivered: asked again, nothing new");
    streamer.forget(1);
    streamer.request(1, 16, 16);
    payload = take_one(streamer);
    check(payload.hash == 1, "forgotten (evicted): read again");
    streamer.request(2, 16, 16);
    check(streamer.wait_idle(2000) && !streamer.take() && streamer.stats().failures == 1 && !streamer.stats().last_error.empty(),
          "a missing file fails once, with its reason");
    streamer.request(2, 16, 16);
    check(streamer.wait_idle(2000) && streamer.stats().failures == 1, "a failed texture is not tried again");
    streamer.request(99, 16, 16);
    check(streamer.wait_idle(2000) && !streamer.take(), "a hash outside the pack: nothing");
    // High: a new generation, level 1 first.
    const auto before = streamer.generation();
    config.target_scale = 2;
    streamer.configure(config);
    check(streamer.generation() == before + 1, "configure starts a generation");
    streamer.request(1, 16, 16);
    payload = take_one(streamer);
    check(payload.levels.size() == 6 && payload.levels[0].width == 32 && payload.generation == before + 1, "High payload");
    // The title's pixels.
    config.rgba_hashes = {1};
    streamer.configure(config);
    streamer.request(1, 16, 16);
    payload = take_one(streamer);
    check(payload.rgba.size() == 32 * 32 * 4, "level 0 as RGBA8 for the hashes that need it");
    // Documents: an empty one is scanned (no images), dropped when no longer listed.
    auto empty = std::make_shared<const IgbDocument>();
    streamer.set_documents({empty});
    check(streamer.wait_idle(2000) && streamer.stats().documents_scanned == 1, "a document scanned");
    check(!streamer.lookup(empty.get(), 1), "no image in it");
    streamer.set_documents({});
    streamer.set_documents({empty});
    check(streamer.wait_idle(2000) && streamer.stats().documents_scanned == 2, "listed again after it was dropped: scanned again");
    // Off: no pack.
    streamer.configure({});
    streamer.request(1, 16, 16);
    check(!streamer.active() && streamer.wait_idle(2000) && !streamer.take(), "off: no work");
    // A document listed while nothing streams (Original) is scanned once a
    // pack streams again (the renderer keys its records from that scan:
    // pending() says whether to wait for it).
    auto later = std::make_shared<const IgbDocument>();
    streamer.set_documents({empty, later});
    check(streamer.wait_idle(2000) && !streamer.pending(later.get()) && !streamer.lookup(later.get(), 1),
          "off: listed, not scanned, nothing pending");
    const auto scanned_before = streamer.stats().documents_scanned;
    config.rgba_hashes.clear();
    streamer.configure(config);
    const bool was_pending = streamer.pending(later.get());
    check(streamer.wait_idle(2000) && !streamer.pending(later.get()) && streamer.stats().documents_scanned == scanned_before + 1 &&
              (was_pending || streamer.stats().documents_scanned > scanned_before),
          "on again: the listed document scanned");
    check(!streamer.pending(reinterpret_cast<const IgbDocument*>(&scanned_before)), "a document not listed is never pending");
    streamer.configure({});
    // With the game data: a real document's images are scanned and the pack's
    // texture among them is read ahead without a request.
    if (!data.empty()) {
        const auto resources = MountedResources::open(data);
        auto document = std::make_shared<const IgbDocument>(read_igb_document(resources.read("_common/w_common07.igb")));
        const std::uint64_t w_common07 = 0x635f3332c7e4a816ull; // w_common07.dds, 64x64 (texpack_tests' dumped hashes)
        const auto big = make_dds(TextureEncoding::bc7, 256, 256, random_chain(TextureEncoding::bc7, 256, 256, 9, 21), true);
        write_file(root / "w.dds", big);
        write_text(root / kPackManifest, "#dw4pack\t2\nhash\tencoding\tscale\tlevels\tfile\n635f3332c7e4a816\tbc7\t4\t9\tw.dds\n");
        pack.configure(root, {});
        config = {};
        config.pack = pack.shared_index();
        config.target_scale = 4;
        streamer.configure(config);
        streamer.set_documents({document});
        payload = take_one(streamer);
        check(payload.hash == w_common07 && payload.levels.size() == 9 && payload.levels[0].width == 256, "read ahead from the scan");
        bool found = false;
        for (const auto& info : list_igb_textures(*document)) {
            const auto key = streamer.lookup(document.get(), info.reference_index);
            if (!info.supported) continue;
            check(key.has_value(), "every image of the document scanned");
            const auto image = decode_igb_texture(*document, info.reference_index);
            check(key->pack_hash == TexturePack::image_hash(image) && key->width == image.width, "the scan's hash is the renderer's");
            check(key->pixel_hash == TexturePack::hash(image.rgba.data(), image.rgba.size(), int(image.width), int(image.height)),
                  "the pixel hash is of the pixels drawn");
            if (key->pack_hash == w_common07) found = key->in_pack;
        }
        check(found, "w_common07 found in the pack");
        std::cout << "(game data: w_common07.igb scanned and read ahead)\n";
    }
    std::filesystem::remove_all(root);
}

// --------------------------------------------------------------- budget

void budget_tests() {
    const std::vector<BudgetEntry> entries{{1, 100, 10}, {2, 100, 5}, {3, 100, 20}, {4, 100, 5}, {5, 100, 30}};
    check(choose_evictions(entries, 500, 100).empty(), "within the budget: nothing");
    check(choose_evictions(entries, 300, 100) == std::vector<std::uint64_t>{2, 4}, "least recently drawn first, ties by id");
    check(choose_evictions(entries, 250, 100) == std::vector<std::uint64_t>{2, 4, 1}, "until it fits");
    check(choose_evictions(entries, 0, 20) == std::vector<std::uint64_t>{2, 4, 1}, "never what was drawn from frame 20 on");
    check(choose_evictions({}, 0, 0).empty(), "no entries");
}

// -------------------------------------------------------------- options

const char* fake(const char* name) {
    static const std::map<std::string, std::string> env{{"DW4_TEXTURE_QUALITY", "high"}, {"DW4_TEXTURE_FILTER", "8x"},
                                                        {"DW4_LOD_BIAS", "-0.5"},       {"DW4_SHARPEN", "low"},
                                                        {"DW4_SHADOW_QUALITY", "ultra"}, {"DW4_TEXTURE_MEMORY", "2048"}};
    const auto it = env.find(name);
    return it == env.end() ? nullptr : it->second.c_str();
}
const char* junk(const char* name) {
    static const std::map<std::string, std::string> env{{"DW4_TEXTURE_QUALITY", "extreme"}, {"DW4_TEXTURE_FILTER", "32x"},
                                                        {"DW4_LOD_BIAS", "sharp"},          {"DW4_SHARPEN", "3"},
                                                        {"DW4_SHADOW_QUALITY", ""},         {"DW4_TEXTURE_MEMORY", "-5"}};
    const auto it = env.find(name);
    return it == env.end() ? nullptr : it->second.c_str();
}

void option_tests() {
    for (int i = 0; i < 3; ++i) {
        check(parse_texture_quality(texture_quality_name(TextureQuality(i))) == TextureQuality(i), "quality name round trip");
        check(parse_sharpening(sharpening_name(Sharpening(i))) == Sharpening(i), "sharpening name round trip");
        check(parse_shadow_quality(shadow_quality_name(ShadowQuality(i))) == ShadowQuality(i), "shadow name round trip");
    }
    for (int i = 0; i < 6; ++i) check(parse_texture_filter(texture_filter_name(TextureFilter(i))) == TextureFilter(i), "filter name round trip");
    check(parse_texture_filter("ANISO16") == TextureFilter::aniso16 && parse_texture_filter("2x") == TextureFilter::aniso2 &&
              parse_texture_filter("1") == TextureFilter::trilinear && !parse_texture_filter("32x") && !parse_texture_filter(""),
          "filter spellings");
    check(parse_texture_quality("4x") == TextureQuality::ultra && !parse_texture_quality("8x") && !parse_texture_quality("3"), "quality spellings");
    check(parse_lod_bias("-0.5") == -0.5f && parse_lod_bias("9") == 2.0f && !parse_lod_bias("abc") && !parse_lod_bias("nan") &&
              !parse_lod_bias("1.5x"),
          "LOD bias");
    check(target_scale(TextureQuality::original) == 0 && target_scale(TextureQuality::high) == 2 && target_scale(TextureQuality::ultra) == 4,
          "target scales");
    check(anisotropy(TextureFilter::trilinear) == 1 && anisotropy(TextureFilter::aniso16) == 16 && shadow_scale(ShadowQuality::high) == 4 &&
              shadow_scale(ShadowQuality::ultra) == 8 && sharpness(Sharpening::off) == 0.0f,
          "derived values");
    GraphicsOptions wild;
    wild.lod_bias = 7.0f;
    wild.texture_memory_mb = 1;
    wild.texture_filter = TextureFilter(42);
    const auto tame = wild.clamped();
    check(tame.lod_bias == 2.0f && tame.texture_memory_mb == GraphicsOptions::kMemoryMinMb && tame.texture_filter == TextureFilter::aniso16, "clamped");
    // Test runs: the environment over the defaults; junk keeps them.
    const auto env = graphics_from_env({}, fake);
    check(env.texture_quality == TextureQuality::high && env.texture_filter == TextureFilter::aniso8 && env.lod_bias == -0.5f &&
              env.sharpening == Sharpening::low && env.shadow_quality == ShadowQuality::ultra && env.texture_memory_mb == 2048,
          "environment read");
    check(graphics_from_env({}, junk) == GraphicsOptions{}, "malformed environment values keep the defaults");
    check(graphics_from_env({}, nullptr) == GraphicsOptions{}, "no environment");
    // settings.ini [graphics]: defaults, a round trip, malformed values.
    auto settings = Settings::parse(IniFile::parse(""));
    check(settings.graphics == GraphicsOptions{}, "defaults without [graphics]");
    settings.graphics = env;
    settings.store();
    const std::string text = settings.ini().text();
    check(text.find("[graphics]") != std::string::npos && text.find("texture_filter=anisotropic8") != std::string::npos &&
              text.find("lod_bias=-0.50") != std::string::npos,
          "stored as names");
    const auto again = Settings::parse(IniFile::parse(text));
    check(again.graphics == env, "settings.ini round trip");
    const auto broken = Settings::parse(IniFile::parse("[graphics]\ntexture_quality=extreme\ntexture_filter=bilinear\nlod_bias=x\n"
                                                       "sharpening=high\nshadow_quality=9\ntexture_memory=99999\n"));
    GraphicsOptions expected;
    expected.sharpening = Sharpening::high;
    expected.texture_memory_mb = GraphicsOptions::kMemoryMaxMb;
    check(broken.graphics == expected, "malformed values keep their defaults, memory clamped");
    // The presets.
    GraphicsOptions mine;
    mine.sharpening = Sharpening::low;
    mine.lod_bias = -1.0f;
    mine.texture_memory_mb = 2048;
    const auto modern = preset_graphics(true, mine);
    check(modern.texture_quality == TextureQuality::ultra && modern.texture_filter == TextureFilter::aniso16 &&
              modern.shadow_quality == ShadowQuality::high && modern.lod_bias == 0.0f && modern.sharpening == Sharpening::low &&
              modern.texture_memory_mb == 2048 && is_modernized_graphics(modern) && !is_original_graphics(modern),
          "Modernized: 4x, 16x, the 4x shadow map; sharpening and memory kept");
    const auto old = preset_graphics(false, mine);
    check(old.texture_quality == TextureQuality::original && old.texture_filter == TextureFilter::bilinear &&
              old.sharpening == Sharpening::off && old.texture_memory_mb == 2048 && is_original_graphics(old) && !is_modernized_graphics(old),
          "Original: the GameCube's look");
    check(!is_original_graphics(GraphicsOptions{}) && !is_modernized_graphics(GraphicsOptions{}), "the defaults are neither");
    // A settings.ini written before the Graphics options keeps showing its
    // preset; a new file (or one with [graphics]) keeps the defaults.
    const auto from_original = Settings::parse(IniFile::parse("[window]\nwidth=1280\nwidescreen=false\nsmooth_motion=false\nmsaa=0\n"));
    check(is_original_graphics(from_original.graphics), "an Original settings.ini: the game's own textures");
    const auto from_modern = Settings::parse(IniFile::parse(
        "[window]\nwidth=1920\nwidescreen=true\nsmooth_motion=true\nmsaa=8\n[preferences]\nskip_intro=true\n[gameplay]\nloading=fast\n"));
    check(is_modernized_graphics(from_modern.graphics), "a Modernized settings.ini: the modern graphics");
    const auto custom = Settings::parse(IniFile::parse("[window]\nwidth=1280\nwidescreen=true\nsmooth_motion=false\nmsaa=0\n"));
    check(custom.graphics == GraphicsOptions{}, "a Custom settings.ini keeps the defaults");
    const auto chosen = Settings::parse(IniFile::parse("[window]\nwidth=1280\nwidescreen=false\nsmooth_motion=false\nmsaa=0\n"
                                                       "[graphics]\ntexture_quality=ultra\n"));
    check(chosen.graphics.texture_quality == TextureQuality::ultra, "[graphics] stored: as it says");
}

} // namespace

int main(int argc, char** argv) {
    const std::string part = argc > 1 ? argv[1] : "all";
    const std::string data = argc > 2 && argv[2] ? argv[2] : "";
    const std::map<std::string, std::function<void()>> parts{
        {"manifest", manifest_tests}, {"dds", dds_tests},       {"codec", codec_tests},    {"mips", mip_tests},
        {"choice", choice_tests},     {"loader", loader_tests}, {"stream", [&] { stream_tests(data); }},
        {"budget", budget_tests},     {"options", option_tests}};
    try {
        if (part == "all") {
            for (const auto& [name, run] : parts) run();
        } else {
            const auto found = parts.find(part);
            if (found == parts.end()) throw std::runtime_error("unknown part " + part);
            found->second();
        }
        std::cout << "Passed " << checks << " graphics checks (" << part << ").\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Graphics tests failed (" << part << "): " << error.what() << '\n';
        return 1;
    }
}
