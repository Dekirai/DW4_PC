#pragma once

// dw4_texpack export (docs/TEXTURES.md, "Exporting every texture"): reads the
// supplied game data (read-only) and writes each unique texture once as a
// PNG named by the texture pack hash the renderer uses
// (TexturePack::image_hash), with a manifest of what the data says about it:
// format, palette variants, alpha, the materials that draw it, samplers,
// languages, stages, the UV range its meshes use and atlas cells where the
// data defines them. A development tool; its output is derived from the
// game's pictures and stays on the user's machine.
#include "dw4/textures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;
class MountedResources;
} // namespace dw4

namespace dw4::texpack {

enum class AlphaKind { opaque, cutout, blend };
const char* alpha_kind_name(AlphaKind kind);

// What the decoded pixels alone say about a texture.
struct ImageFacts {
    std::uint64_t hash{}; // TexturePack::image_hash: the renderer's pack hash
    std::uint32_t width{}, height{};
    bool cmpr{};          // GX CMPR (BC1) source; otherwise C8 with an RGBA8 palette
    AlphaKind alpha = AlphaKind::opaque;
    std::size_t transparent{}; // texels with alpha 0
    std::size_t partial{};     // texels with alpha 1..254
};
ImageFacts analyze(const RgbaImage& decoded);

// One igImage of a document with its pack hash.
struct ImageRecord {
    std::uint32_t reference{};
    std::string name;
    ImageFacts facts;
    std::uint32_t palette_entries{};   // C8 only
    std::uint64_t palette_group{};     // C8 only: hash of the index plane and size
    bool has_palette_group{};
};
// Decodes every supported igImage of a document. Unsupported or undecodable
// images are reported through `errors` (one line each) and left out.
std::vector<ImageRecord> scan_images(const IgbDocument& document, std::vector<std::string>* errors = nullptr,
                                     std::vector<RgbaImage>* decoded = nullptr);

// Where a document comes from: its archive entry, the catalog paths naming
// that entry and the package descriptors listing it as a member.
struct DocumentSource {
    std::string archive;
    std::uint32_t entry{};
    std::vector<std::string> aliases;
    std::vector<std::string> packages; // descriptor logical paths ("dng101b1.igb")
};

// The language a logical path belongs to: "usa", "eng", "fra", "ger", "ita",
// "spn", "jpn" (<stage>/separate/<lang>_pack.igb, loading<lang>/), the region
// packs "aus", "aeu", "ajp", "base" for loading/, otherwise "all".
std::string path_language(const std::string& logical_path);
// The languages the USA release loads: language-neutral data, usa and aus.
bool usa_language(const std::string& language);

// Texel rectangle [x0, x1) x [y0, y1) of an atlas cell.
struct AtlasCell {
    int x0{}, y0{}, x1{}, y1{};
    std::string source; // "font", "grid", "uvkey"
    std::string label;  // glyph code, grid cell index, key index
};

class Exporter {
public:
    struct Options {
        std::filesystem::path out;
        bool masks = true; // uvmask/<hash>.png: texels the meshes' UVs cover
    };
    struct Summary {
        std::size_t documents{}, images{}, unique{}, usa_unique{}, errors{}, warnings{};
        std::size_t used_unique{}, tiling_unique{}, atlas_unique{};
        // beTextureCtrl bindings: all, resolved only through another
        // stage's packs, not resolved.
        std::size_t named_bindings{}, fallback_bindings{}, unresolved_bindings{};
    };
    // Prepares the output folder: it must be empty, new, or an earlier
    // export (marked by .dw4_texpack_export), whose files are replaced.
    explicit Exporter(Options options);
    ~Exporter();
    Exporter(const Exporter&) = delete;
    Exporter& operator=(const Exporter&) = delete;

    // The catalog's stage names (for the stages column).
    void set_stage_names(std::set<std::string> names);
    // Images, samplers, pack textures and the scene walk of one document.
    // Returns true when the document binds pack textures by name
    // (beTextureCtrl): its scene walk waits for add_bindings, called once
    // every document was added.
    bool add_document(const IgbDocument& document, const DocumentSource& source);
    void add_bindings(const IgbDocument& document, const DocumentSource& source);
    void note_error(const std::string& message);
    // Names the PNGs and writes manifest.tsv, instances.tsv, atlas/ and uvmask/.
    Summary finish(std::ostream& log);
    const std::vector<std::string>& errors() const;
    const std::vector<std::string>& warnings() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// The whole export over mounted game data (an extracted root or a disc image).
Exporter::Summary export_all(const MountedResources& resources, const Exporter::Options& options, std::ostream& log);

} // namespace dw4::texpack
