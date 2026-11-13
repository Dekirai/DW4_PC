// dw4_texpack export (tools/dw4_texpack): its pack hashes are the renderer's.
// Synthetic decoded images (C8 and CMPR, with and without transparent CMPR
// texels) against the specification of the pack hash and fixed values, the
// exported file name read back as a replacement by TexturePack, the output
// folder rules and the language rules. With game data (argument 1) the
// export's records of original documents equal what GlRenderer::texture
// hashes for the same images through the engine's texture bindings, and the
// hashes the renderer dumped on the dng101b1 route.
#include "../src/game/texture_pack.hpp"
#include "../tools/dw4_texpack/texpack_export.hpp"

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/packages.hpp"
#include "dw4/resources.hpp"
#include "dw4/textures.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
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

// The pack hash as docs/TEXTURES.md specifies it: 64-bit FNV-1a over the
// width and height (little-endian 32 bits each) and the RGBA8 rows, a CMPR
// texel with alpha 0 counted as 0, 0, 0, 0.
std::uint64_t reference_hash(const RgbaImage& image) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint8_t b) {
        h ^= b;
        h *= 0x100000001b3ull;
    };
    for (int shift = 0; shift < 32; shift += 8) mix(std::uint8_t(image.width >> shift));
    for (int shift = 0; shift < 32; shift += 8) mix(std::uint8_t(image.height >> shift));
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const bool black = image.block_compressed && image.rgba[i + 3] == 0;
        for (std::size_t c = 0; c < 4; ++c) mix(black ? 0 : image.rgba[i + c]);
    }
    return h;
}

std::uint64_t raw_hash(const RgbaImage& image) {
    return game::TexturePack::hash(image.rgba.data(), image.rgba.size(), int(image.width), int(image.height));
}

void synthetic_hashes() {
    // C8: a transparent palette entry keeps its colour in the hash.
    const Bytes palette{255, 0, 0, 255, 0, 128, 255, 127, 3, 7, 11, 0};
    const Bytes indices{0, 1, 2, 9, 2, 1, 0, 9};
    const auto c8 = decode_indexed8_rgba8(indices, 3, 2, 4, palette, 3);
    const auto c8_facts = texpack::analyze(c8);
    check(c8_facts.hash == game::TexturePack::image_hash(c8), "C8: export hash is the renderer's");
    check(c8_facts.hash == reference_hash(c8) && c8_facts.hash == raw_hash(c8), "C8: hash of the decoded pixels as they are");
    check(c8_facts.hash == 0xa90da98eaee43b6cull, "C8: fixed hash value (a decoder or hash change invalidates every pack)");
    check(!c8_facts.cmpr && c8_facts.alpha == texpack::AlphaKind::blend && c8_facts.transparent == 2 && c8_facts.partial == 2,
          "C8: format and alpha kind");

    // CMPR three-colour block: selector 3 is transparent and keeps GX's
    // average colour in the decode, the hash sees it black.
    const Bytes transparent{0x1f, 0x00, 0x00, 0xf8, 0xe4, 0xe4, 0xe4, 0xe4};
    const auto cutout = decode_bc1(transparent, 4, 4);
    check(cutout.rgba[3 * 4 + 0] == 127 && cutout.rgba[3 * 4 + 3] == 0, "CMPR: the decode keeps the average colour");
    const auto cutout_facts = texpack::analyze(cutout);
    check(cutout_facts.hash == game::TexturePack::image_hash(cutout), "CMPR cut-out: export hash is the renderer's");
    check(cutout_facts.hash == reference_hash(cutout), "CMPR cut-out: transparent texels hash black");
    check(cutout_facts.hash != raw_hash(cutout), "CMPR cut-out: the black rule changes the hash");
    check(cutout_facts.hash == 0x10801f20ee7e4905ull, "CMPR cut-out: fixed hash value");
    check(cutout_facts.cmpr && cutout_facts.alpha == texpack::AlphaKind::cutout && cutout_facts.transparent == 4,
          "CMPR cut-out: format and alpha kind");

    // CMPR four-colour block: opaque, the hash is the plain one.
    const Bytes four_color{0x00, 0xf8, 0x1f, 0x00, 0xe4, 0xe4, 0xe4, 0xe4};
    const auto opaque = decode_bc1(four_color, 4, 4);
    const auto opaque_facts = texpack::analyze(opaque);
    check(opaque_facts.hash == game::TexturePack::image_hash(opaque) && opaque_facts.hash == raw_hash(opaque),
          "CMPR opaque: export hash is the renderer's plain hash");
    check(opaque_facts.hash == 0xffcccabb06e14065ull, "CMPR opaque: fixed hash value");
    check(opaque_facts.alpha == texpack::AlphaKind::opaque, "CMPR opaque: alpha kind");
}

// The exported file name works as a replacement: TexturePack finds it by
// the hash and loads the same pixels.
void file_round_trip(const std::filesystem::path& root) {
    const Bytes transparent{0x1f, 0x00, 0x00, 0xf8, 0xe4, 0xe4, 0xe4, 0xe4};
    const auto image = decode_bc1(transparent, 4, 4);
    const auto facts = texpack::analyze(image);
    const auto folder = root / "pack";
    std::filesystem::create_directories(folder);
    const auto file = folder / game::TexturePack::file_name("cell tex.dds", 4, 4, facts.hash);
    check(game::TexturePack::write_png(file, image.rgba.data(), 4, 4), "PNG written");
    game::TexturePack pack;
    pack.configure(folder, {});
    check(pack.replacement_count() == 1, "the exported name is a replacement name");
    const auto loaded = pack.replacement(facts.hash);
    check(loaded && loaded->width == 4 && loaded->height == 4 && loaded->rgba == image.rgba,
          "the replacement loads the exported pixels (the decode, average colour included)");
}

void output_folder(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    const auto foreign = root / "foreign";
    fs::create_directories(foreign);
    std::ofstream(foreign / "keep.png") << "x";
    bool refused = false;
    try {
        texpack::Exporter exporter({foreign, true});
    } catch (const std::runtime_error&) {
        refused = true;
    }
    check(refused && fs::exists(foreign / "keep.png"), "a folder that is no export is left alone");
    const auto out = root / "export";
    {
        texpack::Exporter exporter({out, true});
        std::ostringstream log;
        const auto summary = exporter.finish(log);
        check(summary.unique == 0 && summary.errors == 0, "empty export");
    }
    std::ifstream manifest(out / "manifest.tsv");
    std::string header;
    std::getline(manifest, header);
    check(header.rfind("hash\tfile\tnames\t", 0) == 0, "manifest header");
    std::ofstream(out / "textures" / "stale_4x4_0000000000000000.png") << "x";
    {
        texpack::Exporter again({out, true});
    }
    check(!fs::exists(out / "textures" / "stale_4x4_0000000000000000.png"), "an earlier export is replaced");
}

void languages() {
    check(texpack::path_language("dng101b1/separate/usa_pack.igb") == "usa", "usa pack");
    check(texpack::path_language("stagis08/separate/aus_pack.igb") == "aus", "aus pack");
    check(texpack::path_language("stagis11/separate/aeu_pack.igb") == "aeu", "aeu pack");
    check(texpack::path_language("dng101b1/separate/lh_pack.igb") == "all", "lh pack is no language");
    check(texpack::path_language("loadingusa/ldg101b1.igb") == "usa", "loadingusa");
    check(texpack::path_language("loading/ldag0000.igb") == "base", "loading");
    check(texpack::path_language("_common/w_common07.igb") == "all", "common");
    check(texpack::usa_language("all") && texpack::usa_language("usa") && texpack::usa_language("aus"), "USA languages");
    check(!texpack::usa_language("eng") && !texpack::usa_language("aeu") && !texpack::usa_language("base"), "other languages");
}

// GlRenderer::texture's hash of every texture binding of a document: the
// engine's binding (SceneCache attribute) -> decode_igb_texture of its image
// -> TexturePack::image_hash, without GL.
std::map<std::uint32_t, std::uint64_t> renderer_hashes(const IgbDocument& document) {
    std::map<std::uint32_t, std::uint64_t> out;
    engine::SceneCache cache;
    for (const auto& object : document.objects) {
        const engine::ObjectView view(&document, object.reference_index);
        if (view.type() != "igTextureBindAttr") continue;
        const auto* binding = cache.attribute_at(&document, object.reference_index);
        if (!binding || !binding->image) continue;
        const auto image = decode_igb_texture(*binding->image.document(), binding->image.reference());
        out[binding->image.reference()] = game::TexturePack::image_hash(image);
    }
    return out;
}

// One original document: the export's records against the renderer's hashes.
std::size_t compare(const IgbDocument& document, const std::string& label, std::map<std::uint64_t, std::string>& found) {
    std::vector<std::string> errors;
    const auto records = texpack::scan_images(document, &errors);
    check(errors.empty(), label + ": images the export cannot decode");
    const auto rendered = renderer_hashes(document);
    std::size_t matched = 0;
    for (const auto& record : records) {
        found[record.facts.hash] = record.name;
        if (const auto it = rendered.find(record.reference); it != rendered.end()) {
            check(it->second == record.facts.hash, label + ": " + record.name + " export hash differs from the renderer's");
            ++matched;
        }
    }
    check(matched > 0, label + ": no texture binding to compare");
    return matched;
}

void original_data(const std::filesystem::path& data) {
    const auto resources = MountedResources::open(data);
    std::map<std::uint64_t, std::string> found;
    std::size_t matched = 0;
    for (const char* path : {"bectext.igb", "stagis10/separate/usa_pack.igb", "_common/w_common07.igb", "loadingusa/ldg101b1.igb"})
        matched += compare(read_igb_document(resources.read(path)), path, found);
    const auto package = load_package_descriptor(resources, "dng101b1.igb");
    for (const auto& member : package.members) {
        if (!member.location) continue;
        IgbDocument document;
        try {
            document = read_package_member(resources, member);
        } catch (const std::exception&) {
            continue; // not an IGB member
        }
        if (list_igb_textures(document).empty()) continue;
        std::vector<std::string> errors;
        for (const auto& record : texpack::scan_images(document, &errors)) found[record.facts.hash] = record.name;
        check(errors.empty(), "dng101b1 member: images the export cannot decode");
        const auto rendered = renderer_hashes(document);
        for (const auto& record : texpack::scan_images(document))
            if (const auto it = rendered.find(record.reference); it != rendered.end()) {
                check(it->second == record.facts.hash, "dng101b1 member: export hash differs from the renderer's");
                ++matched;
            }
    }
    // Hashes the renderer dumped (DW4_DUMP_TEXTURES) on the scripted route
    // to dng101b1 before the hash rule moved into TexturePack::image_hash.
    const std::pair<std::uint64_t, const char*> dumped[] = {
        {0xfaccbeda501eefc1ull, "font00.png"},     {0xac566f5a15c2a226ull, "mojigc.png"},      {0x269fee9e73fa226bull, "logo.png"},
        {0x1474c94558854a81ull, "mojips.png"},     {0x635f3332c7e4a816ull, "w_common07.dds"},  {0xa587cce7c63abf90ull, "ldimg01.png"},
        {0xc902e5e08e419118ull, "(dng101b1 CMPR)"}, {0x7f95c2d9ffd0a206ull, "charaicon.png"},
    };
    for (const auto& [hash, name] : dumped) check(found.contains(hash), std::string("renderer dump hash of ") + name + " not exported");
    // A CMPR cut-out of the dng101b1 package: the black rule applies.
    check(found.contains(0xa68e0d725bd7953dull), "fsbktip0.dds (CMPR with transparent texels) under its pack hash");
    std::cout << "Compared " << matched << " texture bindings of original documents with the renderer's hashes.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto root = std::filesystem::temp_directory_path() / ("dw4_texpack_tests_" + std::to_string(std::random_device{}()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        synthetic_hashes();
        file_round_trip(root);
        output_folder(root);
        languages();
        std::filesystem::remove_all(root);
        std::cout << "Passed " << checks << " synthetic texture pack export checks.\n";
        if (argc > 1 && argv[1] && *argv[1]) {
            original_data(argv[1]);
            std::cout << "Passed " << checks << " checks with the game data.\n";
        } else {
            std::cout << "(no game data: the original document checks are skipped)\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Texture pack export tests failed: " << error.what() << '\n';
        return 1;
    }
}
