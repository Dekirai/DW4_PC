#include "texpack_export.hpp"

#include "../../src/game/texture_pack.hpp"
#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/packages.hpp"
#include "dw4/resources.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dw4::texpack {
namespace {

using engine::Attribute;
using engine::AttrKind;
using engine::GeometryData;
using engine::Mat4;
using engine::Node;
using engine::NodeKind;
using engine::ObjectView;
using engine::SceneCache;
using engine::TransformSequence;
using engine::Vec3f;
using game::TexturePack;

// UVs further outside [0, 1] than this (and than one texel, see place())
// leave the picture; rounding in the meshes stays below it.
constexpr float kTileEpsilon = 0.01f;
// Bounds for the scene walk of one document.
constexpr std::size_t kMaxTransforms = 64;
constexpr std::size_t kMaxVisits = 4'000'000;
constexpr std::uint32_t kMaxParticleRecords = 4096;
constexpr std::size_t kMaxCells = 4096;
constexpr const char* kMarker = ".dw4_texpack_export";

std::string hex16(std::uint64_t value) {
    char text[20];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// Text fields of the TSV files: no tabs, line breaks or control bytes.
std::string clean(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (static_cast<unsigned char>(c) < 32) c = '_';
    return out;
}

std::string join(const std::set<std::string>& values, const char* separator = ",") {
    std::string out;
    for (const auto& value : values) {
        if (!out.empty()) out += separator;
        out += value;
    }
    return out.empty() ? "-" : out;
}

std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// IG_GFX_ALPHA_FUNCTION (the renderer's alpha compare, gl_renderer.cpp).
const char* alpha_function_name(int value) {
    static const char* names[] = {"NEVER", "LESS", "EQUAL", "LEQUAL", "GREATER", "NOTEQUAL", "GEQUAL", "ALWAYS"};
    return value >= 0 && value < 8 ? names[value] : "UNKNOWN";
}

// IG_GFX_BLEND_FACTOR (gl_renderer.cpp blend_factor).
const char* blend_factor_name(int value) {
    static const char* names[] = {"ZERO",      "ONE",           "SRC_COLOR", "ONE_MINUS_SRC_COLOR",
                                  "SRC_ALPHA", "ONE_MINUS_SRC_ALPHA", "DST_COLOR", "ONE_MINUS_DST_COLOR",
                                  "DST_ALPHA", "ONE_MINUS_DST_ALPHA", "SRC_ALPHA_SATURATE"};
    return value >= 0 && value < 11 ? names[value] : "UNKNOWN";
}

// IG_GFX_TEXTURE_FILTER (engine::TextureSampler).
const char* filter_name(int value) {
    static const char* names[] = {"NEAREST",
                                  "LINEAR",
                                  "NEAREST_MIPMAP_NEAREST",
                                  "NEAREST_MIPMAP_LINEAR",
                                  "LINEAR_MIPMAP_NEAREST",
                                  "LINEAR_MIPMAP_LINEAR"};
    return value >= 0 && value < 6 ? names[value] : "UNKNOWN";
}

const char* wrap_name(int value) { return value == 0 ? "CLAMP" : "REPEAT"; }

bool stage_like(const std::string& name) {
    return name.rfind("stag", 0) == 0 || name.rfind("dng", 0) == 0;
}

struct Range {
    float u0{}, u1{}, v0{}, v1{};
    bool valid{};
    void add(float u, float v) {
        if (!valid) {
            u0 = u1 = u;
            v0 = v1 = v;
            valid = true;
            return;
        }
        u0 = std::min(u0, u);
        u1 = std::max(u1, u);
        v0 = std::min(v0, v);
        v1 = std::max(v1, v);
    }
    void add(const Range& other) {
        if (!other.valid) return;
        add(other.u0, other.v0);
        add(other.u1, other.v1);
    }
    bool outside_s() const { return u0 < -kTileEpsilon || u1 > 1.0f + kTileEpsilon; }
    bool outside_t() const { return v0 < -kTileEpsilon || v1 > 1.0f + kTileEpsilon; }
};

// Everything known about one unique texture (one pack hash).
struct Entry {
    ImageFacts facts;
    std::map<std::string, std::size_t> names;
    std::uint32_t palette_entries{};
    std::uint64_t palette_group{};
    bool has_palette_group{};
    std::set<std::string> languages, stages, wraps, filters, alpha_tests, blends, atlas, motion, pack_names;
    engine::TextureSampler sampler; // the first igTextureAttr's (beTextureCtrl bindings use it)
    bool has_sampler{};
    bool usa{};
    std::size_t instances{};
    std::set<std::pair<std::string, std::uint32_t>> documents;
    std::string first_source;
    std::size_t uses{};
    Range uv;
    bool tile_s{}, tile_t{}, edge_s{}, edge_t{}, mask_full{};
    std::set<std::array<int, 4>> quads;
    std::vector<AtlasCell> cells;
    std::set<std::string> cell_keys;
    std::vector<std::uint8_t> mask;
    std::filesystem::path staged;
    std::string file;

    void add_cell(AtlasCell cell) {
        if (cells.size() >= kMaxCells) return;
        const std::string key = cell.source + ':' + std::to_string(cell.x0) + ',' + std::to_string(cell.y0) + ',' +
                                std::to_string(cell.x1) + ',' + std::to_string(cell.y1);
        if (!cell_keys.insert(key).second) return;
        cells.push_back(std::move(cell));
    }
};

// Texel rectangle of a UV box, clamped to the image.
std::array<int, 4> texel_rect(const Range& r, std::uint32_t width, std::uint32_t height) {
    const auto clamp = [](float v, std::uint32_t size) {
        return int(std::clamp(v, 0.0f, float(size)));
    };
    return {clamp(std::floor(r.u0 * float(width) + 1e-3f), width), clamp(std::floor(r.v0 * float(height) + 1e-3f), height),
            clamp(std::ceil(r.u1 * float(width) - 1e-3f), width), clamp(std::ceil(r.v1 * float(height) - 1e-3f), height)};
}

} // namespace

const char* alpha_kind_name(AlphaKind kind) {
    switch (kind) {
    case AlphaKind::opaque: return "opaque";
    case AlphaKind::cutout: return "cutout";
    case AlphaKind::blend: return "blend";
    }
    return "unknown";
}

ImageFacts analyze(const RgbaImage& decoded) {
    ImageFacts facts;
    facts.hash = TexturePack::image_hash(decoded);
    facts.width = decoded.width;
    facts.height = decoded.height;
    facts.cmpr = decoded.block_compressed;
    for (std::size_t i = 3; i < decoded.rgba.size(); i += 4) {
        const auto a = decoded.rgba[i];
        if (a == 0) ++facts.transparent;
        else if (a != 255) ++facts.partial;
    }
    facts.alpha = facts.partial ? AlphaKind::blend : facts.transparent ? AlphaKind::cutout : AlphaKind::opaque;
    return facts;
}

std::vector<ImageRecord> scan_images(const IgbDocument& document, std::vector<std::string>* errors,
                                     std::vector<RgbaImage>* decoded) {
    std::vector<ImageRecord> records;
    for (const auto& info : list_igb_textures(document)) {
        if (!info.supported) {
            if (errors) errors->push_back("image " + std::to_string(info.reference_index) + " (" + info.name + "): " + info.diagnostic);
            continue;
        }
        RgbaImage image;
        try {
            image = decode_igb_texture(document, info.reference_index);
        } catch (const std::exception& error) {
            if (errors) errors->push_back("image " + std::to_string(info.reference_index) + " (" + info.name + "): " + error.what());
            continue;
        }
        ImageRecord record;
        record.reference = info.reference_index;
        record.name = info.name;
        record.facts = analyze(image);
        if (!image.block_compressed) {
            // The palette variants of one index image (the same index plane
            // under another igClut) share a group; the slots are the ones the
            // decoder reads (src/textures.cpp inspect_image).
            try {
                const auto& object = document.object(info.reference_index);
                if (const auto clut = document.field_reference(object, 17))
                    record.palette_entries = document.field_u32(document.object(*clut), 3);
                const auto pixels = document.memory(*document.field_reference(object, 13));
                const auto stride = std::size_t(document.field_i32(object, 19));
                const auto w = std::size_t(image.width), h = std::size_t(image.height);
                if (stride >= w && pixels.size() >= stride * h) {
                    std::vector<std::uint8_t> plane;
                    plane.reserve(w * h);
                    for (std::size_t y = 0; y < h; ++y)
                        plane.insert(plane.end(), pixels.begin() + std::ptrdiff_t(y * stride), pixels.begin() + std::ptrdiff_t(y * stride + w));
                    record.palette_group = TexturePack::hash(plane.data(), plane.size(), int(w), int(h)) ^ 0x5bd1e995ull;
                    record.has_palette_group = true;
                }
            } catch (const std::exception&) {
                // The decode above succeeded with the same fields; a group is optional.
            }
        }
        if (decoded) decoded->push_back(std::move(image));
        records.push_back(std::move(record));
    }
    return records;
}

std::string path_language(const std::string& logical_path) {
    static const std::set<std::string> packs{"usa", "eng", "fra", "ger", "ita", "spn", "jpn", "aus", "aeu", "ajp"};
    const std::string path = lower(logical_path);
    if (const auto at = path.find("/separate/"); at != std::string::npos) {
        const std::string tail = path.substr(at + 10);
        if (tail.size() == 12 && tail.substr(3) == "_pack.igb" && packs.contains(tail.substr(0, 3))) return tail.substr(0, 3);
    }
    if (path.rfind("loading", 0) == 0) {
        const auto slash = path.find('/');
        if (slash == 7) return "base";
        if (slash != std::string::npos) return path.substr(7, slash - 7);
    }
    return "all";
}

bool usa_language(const std::string& language) {
    return language == "all" || language == "usa" || language == "aus";
}

struct Exporter::State {
    Options options;
    std::filesystem::path textures, staging;
    std::unordered_map<std::uint64_t, Entry> entries;
    std::vector<std::uint64_t> order;
    std::set<std::string> stage_names;
    std::unordered_map<std::string, std::vector<std::string>> loading_stages; // "g101b1" -> stages
    std::vector<std::string> errors, warnings;
    std::size_t documents{}, images{};
    std::ofstream instances;
    // beTextureCtrl pack textures: stage ("common" for _common/) -> name -> hashes.
    std::map<std::string, std::map<std::string, std::set<std::uint64_t>>> packs;
    // Documents whose scene walk waits for every pack (add_bindings).
    struct Deferred {
        std::string label;
        std::set<std::string> stages;
        std::unordered_map<std::uint32_t, std::uint64_t> images;
    };
    std::map<std::pair<std::string, std::uint32_t>, Deferred> deferred;
    std::size_t named_bindings{}, fallback_bindings{}, unresolved_bindings{};
    std::set<std::string> fallback_names, unresolved_names;

    std::set<std::string> stages_of(const std::string& logical_path) const;
};

std::set<std::string> Exporter::State::stages_of(const std::string& logical_path) const {
    const std::string path = lower(logical_path);
    if (path.rfind("_common/", 0) == 0 || path == "bectext.igb") return {"common"};
    const auto slash = path.find('/');
    if (slash == std::string::npos) {
        // A package descriptor or other top-level document: "<stage>.igb".
        if (path.size() > 4 && path.substr(path.size() - 4) == ".igb") {
            const std::string stem = path.substr(0, path.size() - 4);
            if (stage_names.contains(stem)) return {stem};
        }
        return {};
    }
    const std::string top = path.substr(0, slash);
    if (stage_names.contains(top)) return {top};
    if (top.rfind("loading", 0) == 0) {
        // loading<lang>/ld<stage name from its third letter>.igb (engine.cpp
        // loader state 0).
        std::string file = path.substr(slash + 1);
        if (file.rfind("ld", 0) == 0 && file.size() > 6 && file.substr(file.size() - 4) == ".igb") {
            file = file.substr(2, file.size() - 6);
            if (const auto found = loading_stages.find(file); found != loading_stages.end())
                return std::set<std::string>(found->second.begin(), found->second.end());
        }
    }
    return {};
}

Exporter::Exporter(Options options) : state_(std::make_unique<State>()) {
    auto& s = *state_;
    s.options = std::move(options);
    namespace fs = std::filesystem;
    const fs::path& out = s.options.out;
    if (out.empty()) throw std::runtime_error("no output folder given");
    std::error_code error;
    if (fs::exists(out, error)) {
        if (!fs::is_directory(out, error)) throw std::runtime_error(out.string() + " is not a folder");
        const bool empty = fs::directory_iterator(out, error) == fs::directory_iterator();
        if (!empty && !fs::exists(out / kMarker, error))
            throw std::runtime_error(out.string() + " is neither empty nor an earlier dw4_texpack export (" + kMarker + ")");
        // An earlier export: its own files go, nothing else.
        for (const char* folder : {"textures", "atlas", "uvmask", "staging"}) fs::remove_all(out / folder, error);
        for (const char* file : {"manifest.tsv", "instances.tsv", "summary.txt"}) fs::remove(out / file, error);
    }
    fs::create_directories(out, error);
    {
        std::ofstream marker(out / kMarker, std::ios::binary);
        marker << "dw4_texpack export output (game-derived, keep local)\n";
    }
    s.textures = out / "textures";
    s.staging = out / "staging";
    fs::create_directories(s.textures, error);
    fs::create_directories(s.staging, error);
    if (s.options.masks) fs::create_directories(out / "uvmask", error);
    fs::create_directories(out / "atlas", error);
    s.instances.open(out / "instances.tsv", std::ios::binary);
    if (!s.instances) throw std::runtime_error("cannot write " + (out / "instances.tsv").string());
    s.instances << "archive\tentry\tsource\treference\tname\thash\twidth\theight\tformat\n";
}

Exporter::~Exporter() = default;

void Exporter::set_stage_names(std::set<std::string> names) {
    auto& s = *state_;
    s.stage_names.clear();
    s.loading_stages.clear();
    for (const auto& name : names) {
        const std::string stage = lower(name);
        s.stage_names.insert(stage);
        if (stage.size() > 2) s.loading_stages[stage.substr(2)].push_back(stage);
    }
}

void Exporter::note_error(const std::string& message) { state_->errors.push_back(message); }
const std::vector<std::string>& Exporter::errors() const { return state_->errors; }
const std::vector<std::string>& Exporter::warnings() const { return state_->warnings; }

namespace {

// The scene graph walk of one document: which material and which UVs draw
// each texture. It follows the engine's own nodes and attributes
// (engine::SceneCache) and keeps the attribute rules of the traversal's
// apply() (src/engine/scene_graph.cpp) for the texture of unit 0, its
// enable state, blending, the alpha compare and the texture coordinate
// source, but visits every child (all switch and LOD children, actor skins)
// at no particular time: a static picture of every possible use.
struct WalkState {
    const Attribute* texture{};
    // A beTextureCtrl binding instead: the pack textures of its name.
    const std::set<std::uint64_t>* named{};
    bool texture_enabled{};
    bool blend{};
    int blend_src = 4, blend_dst = 5;
    bool alpha_test = true;
    int alpha_func = 4;
    float alpha_ref = 0.1f;
    int texcoord_source{};
    std::shared_ptr<const std::vector<Mat4>> uv; // texture matrix alternatives; null: identity
    bool uv_scroll{}, uv_keys{};
};

// TextureCtrlManager::bind_all: an attribute set (by reference) whose texture
// binding the manager replaces by the pack texture of a name.
using NamedSets = std::unordered_map<std::uint32_t, const std::set<std::uint64_t>*>;

struct Target {
    Entry* entry{};
    engine::TextureSampler sampler;
};

class Walker {
public:
    Walker(const Exporter::Options& options, std::unordered_map<std::uint64_t, Entry>& entries, const IgbDocument& document,
           const std::unordered_map<std::uint32_t, std::uint64_t>& images, const NamedSets* named = nullptr)
        : options_(options), entries_(entries), document_(document), images_(images), named_(named) {}

    // Returns a warning when the walk stopped early.
    std::string run() {
        std::vector<Node*> nodes;
        for (const auto& object : document_.objects) {
            const ObjectView view(&document_, object.reference_index);
            if (view.is_a("igNode")) nodes.push_back(cache_.node(&document_, object.reference_index));
        }
        std::unordered_set<const Node*> children;
        for (const Node* node : nodes) {
            for (const Node* child : node->children) children.insert(child);
            for (const Node* root : node->appearance_roots) children.insert(root);
        }
        for (Node* node : nodes)
            if (!children.contains(node)) visit(node, WalkState{}, 0);
        fonts();
        if (truncated_) return "scene walk stopped after " + std::to_string(kMaxVisits) + " node visits";
        return {};
    }

    void samplers() {
        for (const auto& object : document_.objects) {
            const ObjectView view(&document_, object.reference_index);
            if (!view.is_a("igTextureAttr")) continue;
            const auto image = view.ref(12);
            if (!image) continue;
            Entry* e = entry(image.reference());
            if (!e) continue;
            const auto sampler = engine::texture_sampler(view);
            if (!e->has_sampler) {
                e->sampler = sampler;
                e->has_sampler = true;
            }
            e->wraps.insert(std::string(wrap_name(sampler.wrap_s)) + '/' + wrap_name(sampler.wrap_t));
            e->filters.insert(std::string(filter_name(sampler.mag_filter)) + '/' + filter_name(sampler.min_filter));
        }
    }

private:
    const Exporter::Options& options_;
    std::unordered_map<std::uint64_t, Entry>& entries_;
    const IgbDocument& document_;
    const std::unordered_map<std::uint32_t, std::uint64_t>& images_;
    const NamedSets* named_{};
    SceneCache cache_;
    std::size_t visits_{};
    bool truncated_{};

    Entry* entry(std::uint32_t image_reference) {
        const auto found = images_.find(image_reference);
        if (found == images_.end()) return nullptr;
        const auto e = entries_.find(found->second);
        return e == entries_.end() ? nullptr : &e->second;
    }

    static void apply(const Attribute* a, WalkState& s) {
        switch (a->kind) {
        case AttrKind::texture_bind:
            if (a->unit == 0) {
                s.texture = a;
                s.named = nullptr;
            }
            break;
        case AttrKind::texture_state:
            if (a->unit == 0) s.texture_enabled = a->enabled;
            break;
        case AttrKind::blend_function:
            s.blend_src = a->e0;
            s.blend_dst = a->e1;
            break;
        case AttrKind::blend_state: s.blend = a->enabled; break;
        case AttrKind::alpha_function:
            s.alpha_func = a->e0;
            s.alpha_ref = a->f0;
            break;
        case AttrKind::alpha_state: s.alpha_test = a->enabled; break;
        case AttrKind::texcoord_source:
            if (a->unit == 0) s.texcoord_source = a->e0;
            break;
        default: break;
        }
    }

    // A node's attribute list; a beTextureCtrl set drops its own texture
    // bindings for the pack texture (bind_all appends the pack binding).
    void apply_attributes(const Node& n, WalkState& s) const {
        const NamedSets::value_type* bound = nullptr;
        if (named_)
            if (const auto found = named_->find(n.object.reference()); found != named_->end()) bound = &*found;
        for (const auto* a : n.attributes)
            if (!bound || a->kind != AttrKind::texture_bind) apply(a, s);
        if (bound) {
            s.texture = nullptr;
            s.named = bound->second;
        }
    }

    // igTransform flag 8: the matrix (or every key of its sequence) moves
    // the texture coordinates below it (state.texture_matrix = local * outer).
    static void texture_transform(const Node& n, WalkState& s) {
        std::vector<Mat4> keys;
        if (n.sequence && n.sequence->valid) {
            const auto& q = *n.sequence;
            const std::size_t count = std::max({q.translations.size(), q.rotations.size(), q.scales.size(), std::size_t(1)});
            const auto same = [](const Mat4& a, const Mat4& b) {
                for (std::size_t i = 0; i < a.m.size(); ++i)
                    if (std::fabs(a.m[i] - b.m[i]) > 1e-6f) return false;
                return true;
            };
            for (std::size_t i = 0; i < count && keys.size() < kMaxTransforms; ++i) {
                const Mat4 key = q.key(i);
                if (std::none_of(keys.begin(), keys.end(), [&](const Mat4& k) { return same(k, key); })) keys.push_back(key);
            }
            // (A sequence of equal keys holds still.)
            if (keys.size() > 1) {
                const bool stepped = q.methods[0] == TransformSequence::none && q.methods[1] == TransformSequence::none;
                (stepped ? s.uv_keys : s.uv_scroll) = true;
            }
        } else {
            keys.push_back(n.matrix);
        }
        auto combined = std::make_shared<std::vector<Mat4>>();
        if (!s.uv) {
            *combined = keys;
        } else {
            for (const auto& key : keys)
                for (const auto& outer : *s.uv) {
                    if (combined->size() >= kMaxTransforms) break;
                    combined->push_back(key * outer);
                }
        }
        s.uv = std::move(combined);
    }

    void visit(Node* n, const WalkState& parent, int depth) {
        if (!n || depth > 256) return;
        if (++visits_ > kMaxVisits) {
            truncated_ = true;
            return;
        }
        WalkState s = parent;
        switch (n->kind) {
        case NodeKind::attr_set:
        case NodeKind::light_state_set:
        case NodeKind::blend_matrix_select:
            apply_attributes(*n, s);
            break;
        case NodeKind::light_set: return; // lights only, no children drawn
        case NodeKind::transform:
            if (n->texture_transform) texture_transform(*n, s);
            break;
        case NodeKind::geometry:
            apply_attributes(*n, s);
            for (const auto* g : n->geometries)
                if (const auto* data = cache_.geometry(g); data && data->valid) use_geometry(*data, s);
            break;
        case NodeKind::particle_timer:
            for (const auto& particle : n->particles) use_particles(particle, s);
            break;
        default: break;
        }
        for (Node* child : n->children) visit(child, s, depth + 1);
        if (n->kind == NodeKind::actor)
            for (Node* root : n->appearance_roots) visit(root, s, depth + 1);
    }

    // The textures a draw with this state samples (one, or every pack
    // texture of a beTextureCtrl name), each with the material recorded.
    std::vector<Target> targets(const WalkState& s) {
        std::vector<Target> out;
        if (!s.texture_enabled) return out;
        if (s.named) {
            for (const auto hash : *s.named)
                if (const auto found = entries_.find(hash); found != entries_.end()) out.push_back({&found->second, found->second.sampler});
        } else if (s.texture && s.texture->image) {
            if (Entry* e = entry(s.texture->image.reference())) out.push_back({e, s.texture->sampler});
        }
        for (auto& t : out) {
            Entry& e = *t.entry;
            ++e.uses;
            if (s.alpha_test) {
                char text[48];
                std::snprintf(text, sizeof text, "%s %.4g", alpha_function_name(s.alpha_func), double(s.alpha_ref));
                e.alpha_tests.insert(text);
            } else {
                e.alpha_tests.insert("off");
            }
            e.blends.insert(s.blend ? std::string(blend_factor_name(s.blend_src)) + '/' + blend_factor_name(s.blend_dst) : "off");
        }
        return out;
    }

    // A UV box of one use: the range, the wrap test and the mask.
    static void place(Entry& e, const Range& box, const engine::TextureSampler& sampler) {
        e.uv.add(box);
        // Outside by a texel at least (and kTileEpsilon): the picture
        // repeats there, more than its edge texels' filter footprint.
        const float eps_s = std::max(kTileEpsilon, 1.0f / float(e.facts.width));
        const float eps_t = std::max(kTileEpsilon, 1.0f / float(e.facts.height));
        const bool out_s = box.u0 < -eps_s || box.u1 > 1.0f + eps_s;
        const bool out_t = box.v0 < -eps_t || box.v1 > 1.0f + eps_t;
        if (out_s && sampler.wrap_s == 1) e.tile_s = true;
        if (out_t && sampler.wrap_t == 1) e.tile_t = true;
        if ((out_s && sampler.wrap_s == 1) || (out_t && sampler.wrap_t == 1)) e.mask_full = true;
        // UVs within a texel of a border (or past it): a REPEAT sampler
        // filters across the wrap there, so the opposite edge must match
        // (faces side by side with UVs from border to border tile it too).
        const float edge_s = 1.0f / float(e.facts.width), edge_t = 1.0f / float(e.facts.height);
        if (sampler.wrap_s == 1 && (box.u0 <= edge_s || box.u1 >= 1.0f - edge_s)) e.edge_s = true;
        if (sampler.wrap_t == 1 && (box.v0 <= edge_t || box.v1 >= 1.0f - edge_t)) e.edge_t = true;
    }

    static void mark(Entry& e, float u, float v) {
        const auto w = int(e.facts.width), h = int(e.facts.height);
        const int x = std::clamp(int(std::floor(u * float(w))), 0, w - 1);
        const int y = std::clamp(int(std::floor(v * float(h))), 0, h - 1);
        e.mask[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 255;
    }

    // Texels whose centre lies in the triangle (clamped UVs), plus the
    // texels under its corners, so thin triangles leave a trace.
    void rasterize(Entry& e, const float* a, const float* b, const float* c) const {
        if (!options_.masks || e.mask_full) return;
        const auto w = int(e.facts.width), h = int(e.facts.height);
        if (e.mask.empty()) e.mask.assign(std::size_t(w) * std::size_t(h), 0);
        const auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
        const float ax = clamp01(a[0]) * float(w), ay = clamp01(a[1]) * float(h);
        const float bx = clamp01(b[0]) * float(w), by = clamp01(b[1]) * float(h);
        const float cx = clamp01(c[0]) * float(w), cy = clamp01(c[1]) * float(h);
        mark(e, clamp01(a[0]), clamp01(a[1]));
        mark(e, clamp01(b[0]), clamp01(b[1]));
        mark(e, clamp01(c[0]), clamp01(c[1]));
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::fabs(area) < 1e-6f) return;
        const int x0 = std::clamp(int(std::floor(std::min({ax, bx, cx}))), 0, w - 1);
        const int x1 = std::clamp(int(std::ceil(std::max({ax, bx, cx}))), 0, w - 1);
        const int y0 = std::clamp(int(std::floor(std::min({ay, by, cy}))), 0, h - 1);
        const int y1 = std::clamp(int(std::ceil(std::max({ay, by, cy}))), 0, h - 1);
        const float sign = area > 0 ? 1.0f : -1.0f;
        const float slack = -1e-4f * std::fabs(area);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                const float e0 = sign * ((bx - ax) * (py - ay) - (by - ay) * (px - ax));
                const float e1 = sign * ((cx - bx) * (py - by) - (cy - by) * (px - bx));
                const float e2 = sign * ((ax - cx) * (py - cy) - (ay - cy) * (px - cx));
                if (e0 >= slack && e1 >= slack && e2 >= slack) e.mask[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 255;
            }
    }

    void rasterize_box(Entry& e, const Range& box) const {
        const float a[2]{box.u0, box.v0}, b[2]{box.u1, box.v0}, c[2]{box.u1, box.v1}, d[2]{box.u0, box.v1};
        rasterize(e, a, b, c);
        rasterize(e, a, c, d);
    }

    void use_geometry(const GeometryData& data, const WalkState& s) {
        const auto targets = this->targets(s);
        if (targets.empty()) return;
        if (s.texcoord_source == 2) {
            // A sphere map from the normals (water): no stored UVs apply.
            for (const auto& t : targets) {
                t.entry->motion.insert("texgen");
                t.entry->mask_full = true;
            }
            return;
        }
        const std::size_t vertices = data.texcoords.size() / 2;
        if (!vertices) return;
        for (const auto& t : targets) {
            if (s.uv_scroll) t.entry->motion.insert("scroll");
            if (s.uv_keys) t.entry->motion.insert("keys");
        }
        static const std::vector<Mat4> identity{Mat4{}};
        const auto& transforms = s.uv ? *s.uv : identity;
        std::vector<float> uv(data.texcoords.size());
        for (std::size_t k = 0; k < transforms.size(); ++k) {
            Range box;
            for (std::size_t i = 0; i < vertices; ++i) {
                float u = data.texcoords[2 * i], v = data.texcoords[2 * i + 1];
                if (s.uv) {
                    const Vec3f p = engine::transform_point(Vec3f{u, v, 0.0f}, transforms[k]);
                    u = p.x;
                    v = p.y;
                }
                uv[2 * i] = u;
                uv[2 * i + 1] = v;
                if (std::isfinite(u) && std::isfinite(v)) box.add(u, v);
            }
            if (!box.valid) continue;
            const bool inside = !box.outside_s() && !box.outside_t();
            // A sprite: one quad with an axis-aligned UV rectangle (HUD
            // pieces, billboards) is an atlas cell when it is not the whole
            // picture; so is every key of a stepped UV animation.
            bool sprite = inside && data.indices.size() == 6 && vertices == 4;
            const bool keyed = transforms.size() > 1;
            for (std::size_t i = 0; sprite && i < 4; ++i) {
                const float u = uv[2 * i], v = uv[2 * i + 1];
                const bool on_u = std::fabs(u - box.u0) < 1e-4f || std::fabs(u - box.u1) < 1e-4f;
                const bool on_v = std::fabs(v - box.v0) < 1e-4f || std::fabs(v - box.v1) < 1e-4f;
                sprite = on_u && on_v;
            }
            for (const auto& t : targets) {
                Entry& e = *t.entry;
                place(e, box, t.sampler);
                if (s.uv_scroll) e.mask_full = true;
                if (sprite) {
                    const auto rect = texel_rect(box, e.facts.width, e.facts.height);
                    e.quads.insert(rect);
                    e.add_cell({rect[0], rect[1], rect[2], rect[3], keyed ? "uvkey" : "quad", keyed ? std::to_string(k) : std::string("-")});
                }
                if (e.mask_full) continue;
                for (std::size_t i = 0; i + 2 < data.indices.size(); i += 3) {
                    const auto i0 = data.indices[i], i1 = data.indices[i + 1], i2 = data.indices[i + 2];
                    if (i0 >= vertices || i1 >= vertices || i2 >= vertices) continue;
                    rasterize(e, &uv[2 * i0], &uv[2 * i1], &uv[2 * i2]);
                }
            }
        }
    }

    // igParticleAttr (src/engine/particles.cpp build_particles): records of
    // the array's data with their UV rectangle at +0x6C..+0x78, moved by the
    // terms at +0x94..+0xB0 when the array is continuous or textured, and the
    // sprite sheet grid _xImages x _yImages ([24], [25]).
    void use_particles(const ObjectView& attr, const WalkState& s) {
        const auto targets = this->targets(s);
        if (targets.empty()) return;
        const auto array = attr.ref(5);
        if (!array) return;
        const float x_images = array.f32(24, 1.0f), y_images = array.f32(25, 1.0f);
        const bool moving = array.boolean(27) || array.boolean(29);
        if (x_images > 1.0f || y_images > 1.0f) {
            const int gx = std::max(1, int(std::lround(x_images))), gy = std::max(1, int(std::lround(y_images)));
            for (const auto& t : targets) {
                Entry& e = *t.entry;
                e.atlas.insert("grid:" + std::to_string(gx) + 'x' + std::to_string(gy));
                const auto w = int(e.facts.width), h = int(e.facts.height);
                for (int y = 0; y < gy; ++y)
                    for (int x = 0; x < gx; ++x)
                        e.add_cell({x * w / gx, y * h / gy, (x + 1) * w / gx, (y + 1) * h / gy, "grid", std::to_string(y * gx + x)});
            }
        }
        const std::uint32_t stride = array.u32(8) ? array.u32(8) : 180u;
        const auto data = array.memory(9);
        const std::uint32_t first = attr.u32(7), count = std::min(attr.u32(6), kMaxParticleRecords);
        for (std::uint32_t i = first; i < first + count; ++i) {
            const std::size_t base = std::size_t(i) * stride;
            if (base + 0x7C > data.size()) break;
            Range box;
            box.add(engine::read_f32le(data, base + 0x6C), engine::read_f32le(data, base + 0x70));
            box.add(engine::read_f32le(data, base + 0x74), engine::read_f32le(data, base + 0x78));
            if (!std::isfinite(box.u0) || !std::isfinite(box.u1) || !std::isfinite(box.v0) || !std::isfinite(box.v1)) continue;
            bool moves = false;
            if (moving && base + 0xB4 <= data.size())
                for (std::size_t o = 0x94; o <= 0xB0 && !moves; o += 4) moves = engine::read_f32le(data, base + o) != 0.0f;
            for (const auto& t : targets) {
                Entry& e = *t.entry;
                if (moves) {
                    e.motion.insert("particle");
                    e.mask_full = true;
                }
                place(e, box, t.sampler);
                rasterize_box(e, box);
            }
        }
    }

    // beFontImage (TEXTCTL fonts, src/engine/ui.cpp load_fonts): the glyph
    // cells of the font picture ([7] metrics, 40 bytes per glyph: code,
    // advance, offsets, size, u0 v0 u1 v1; [8] the texture binding).
    void fonts() {
        for (const auto& object : document_.objects) {
            const ObjectView font(&document_, object.reference_index);
            if (font.type() != "beFontImage") continue;
            const auto bind = font.ref(8);
            const auto texture = bind ? bind.ref(4) : ObjectView{};
            const auto image = texture ? texture.ref(12) : ObjectView{};
            if (!image) continue;
            Entry* e = entry(image.reference());
            if (!e) continue;
            const auto metrics = font.ref(7);
            if (!metrics) continue;
            const auto count = metrics.u32(2);
            const auto data = metrics.memory(4);
            std::uint32_t glyphs = 0;
            for (std::uint32_t i = 0; i < count && (std::size_t(i) + 1) * 40 <= data.size(); ++i) {
                const std::size_t o = std::size_t(i) * 40;
                const auto code = engine::read_u32le(data, o) & 0xFFFFu;
                Range box;
                box.add(engine::read_f32le(data, o + 24), engine::read_f32le(data, o + 28));
                box.add(engine::read_f32le(data, o + 32), engine::read_f32le(data, o + 36));
                if (!std::isfinite(box.u0) || !std::isfinite(box.u1) || !std::isfinite(box.v0) || !std::isfinite(box.v1)) continue;
                ++glyphs;
                e->uv.add(box);
                const auto rect = texel_rect(box, e->facts.width, e->facts.height);
                char label[16];
                std::snprintf(label, sizeof label, "U+%04X", code);
                e->add_cell({rect[0], rect[1], rect[2], rect[3], "font", label});
                rasterize_box(*e, box);
            }
            e->atlas.insert("font:" + clean(font.str(2)) + ':' + std::to_string(glyphs));
        }
    }
};

} // namespace

bool Exporter::add_document(const IgbDocument& document, const DocumentSource& source) {
    auto& s = *state_;
    ++s.documents;
    std::vector<std::string> labels = source.aliases;
    for (const auto& package : source.packages) labels.push_back("pkg:" + package);
    const std::string label = labels.empty() ? source.archive + ':' + std::to_string(source.entry) : labels.front();
    std::set<std::string> languages, stages;
    for (const auto& alias : source.aliases) {
        languages.insert(path_language(alias));
        for (const auto& stage : s.stages_of(alias)) stages.insert(stage);
    }
    for (const auto& package : source.packages) {
        languages.insert("all");
        for (const auto& stage : s.stages_of(package)) stages.insert(stage);
    }
    if (languages.empty()) languages.insert("all"); // an entry no catalog path names
    const bool usa = std::any_of(languages.begin(), languages.end(), usa_language);

    std::vector<std::string> problems;
    std::vector<RgbaImage> decoded;
    const auto records = scan_images(document, &problems, &decoded);
    for (const auto& problem : problems) s.errors.push_back(label + ": " + problem);
    std::unordered_map<std::uint32_t, std::uint64_t> by_reference;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        ++s.images;
        by_reference[r.reference] = r.facts.hash;
        auto [found, fresh] = s.entries.try_emplace(r.facts.hash);
        Entry& e = found->second;
        if (fresh) {
            e.facts = r.facts;
            e.palette_entries = r.palette_entries;
            e.palette_group = r.palette_group;
            e.has_palette_group = r.has_palette_group;
            e.first_source = label;
            e.staged = s.staging / (hex16(r.facts.hash) + ".png");
            const auto& image = decoded[i];
            if (!TexturePack::write_png(e.staged, image.rgba.data(), int(image.width), int(image.height)))
                s.errors.push_back(label + ": cannot write " + e.staged.string());
        } else if (e.facts.width != r.facts.width || e.facts.height != r.facts.height || e.facts.cmpr != r.facts.cmpr) {
            s.errors.push_back(label + ": hash " + hex16(r.facts.hash) + " names two different images");
        }
        ++e.names[clean(r.name)];
        e.languages.insert(languages.begin(), languages.end());
        e.stages.insert(stages.begin(), stages.end());
        e.usa = e.usa || usa;
        ++e.instances;
        e.documents.insert({source.archive, source.entry});
        s.instances << source.archive << '\t' << source.entry << '\t' << clean(label) << '\t' << r.reference << '\t'
                    << (r.name.empty() ? "-" : clean(r.name)) << '\t' << hex16(r.facts.hash) << '\t' << r.facts.width << '\t'
                    << r.facts.height << '\t' << (r.facts.cmpr ? "CMPR" : "C8") << '\n';
    }
    // beTextureCtrl (TextureCtrlManager): a pack info names its textures; a
    // binding info lists attribute sets that take the pack texture of a name.
    bool binds = false;
    try {
        for (const auto& object : document.objects) {
            const ObjectView info(&document, object.reference_index);
            if (info.type() != "beTextureCtrlInfo") continue;
            const bool pack = info.boolean(7);
            for (const auto& item : info.list(8)) {
                if (!item) continue;
                if (!pack) {
                    binds = true;
                    continue;
                }
                const auto texture = item.ref(3);
                const auto image = texture ? texture.ref(12) : ObjectView{};
                if (!image) continue;
                const auto found = by_reference.find(image.reference());
                if (found == by_reference.end()) continue;
                const std::string name(item.str(2));
                if (stages.empty()) s.packs["*"][name].insert(found->second);
                for (const auto& stage : stages) s.packs[stage][name].insert(found->second);
                s.entries[found->second].pack_names.insert(clean(name));
            }
        }
    } catch (const std::exception& error) {
        s.warnings.push_back(label + ": texture control: " + error.what());
    }
    try {
        Walker walker(s.options, s.entries, document, by_reference);
        walker.samplers();
        if (binds) {
            s.deferred[{source.archive, source.entry}] = {label, stages, by_reference};
            return true;
        }
        if (by_reference.empty()) return false;
        if (auto warning = walker.run(); !warning.empty()) s.warnings.push_back(label + ": " + warning);
    } catch (const std::exception& error) {
        s.warnings.push_back(label + ": scene walk: " + error.what());
    }
    return false;
}

void Exporter::add_bindings(const IgbDocument& document, const DocumentSource& source) {
    auto& s = *state_;
    const auto found = s.deferred.find({source.archive, source.entry});
    if (found == s.deferred.end()) return;
    const auto& deferred = found->second;
    // The pack textures a name can take in this document's stages (the
    // manager binds from the packs its stage loaded: lh_pack or ls_pack,
    // the language pack); a name no such pack has is looked up in every
    // stage's packs.
    std::vector<std::unique_ptr<std::set<std::uint64_t>>> owned;
    NamedSets named;
    try {
        for (const auto& object : document.objects) {
            const ObjectView info(&document, object.reference_index);
            if (info.type() != "beTextureCtrlInfo" || info.boolean(7)) continue;
            for (const auto& item : info.list(8)) {
                if (!item) continue;
                const auto set = item.ref(3);
                if (!set) continue;
                const std::string name(item.str(2));
                ++s.named_bindings;
                auto hashes = std::make_unique<std::set<std::uint64_t>>();
                const auto collect = [&](const std::string& stage) {
                    if (const auto p = s.packs.find(stage); p != s.packs.end())
                        if (const auto n = p->second.find(name); n != p->second.end()) hashes->insert(n->second.begin(), n->second.end());
                };
                for (const auto& stage : deferred.stages) collect(stage);
                collect("common");
                if (hashes->empty()) {
                    for (const auto& [stage, names] : s.packs) collect(stage);
                    if (!hashes->empty()) {
                        ++s.fallback_bindings;
                        s.fallback_names.insert(clean(name));
                    }
                }
                if (hashes->empty()) {
                    // bind_all leaves the set's own binding.
                    ++s.unresolved_bindings;
                    s.unresolved_names.insert(clean(name));
                    continue;
                }
                named[set.reference()] = hashes.get();
                owned.push_back(std::move(hashes));
            }
        }
        Walker walker(s.options, s.entries, document, deferred.images, &named);
        if (auto warning = walker.run(); !warning.empty()) s.warnings.push_back(deferred.label + ": " + warning);
    } catch (const std::exception& error) {
        s.warnings.push_back(deferred.label + ": scene walk: " + error.what());
    }
}

Exporter::Summary Exporter::finish(std::ostream& log) {
    namespace fs = std::filesystem;
    auto& s = *state_;
    s.instances.close();
    Summary summary;
    summary.documents = s.documents;
    summary.images = s.images;
    summary.unique = s.entries.size();
    // Palette variants per index image.
    std::unordered_map<std::uint64_t, std::size_t> variants;
    for (const auto& [hash, e] : s.entries)
        if (e.has_palette_group) ++variants[e.palette_group];
    std::vector<std::uint64_t> hashes;
    hashes.reserve(s.entries.size());
    for (const auto& [hash, e] : s.entries) hashes.push_back(hash);
    std::sort(hashes.begin(), hashes.end());
    std::error_code error;
    for (const auto hash : hashes) {
        Entry& e = s.entries[hash];
        // The most frequent image name (the renderer names a dump after
        // the use it meets first; any name works, only the hash counts).
        std::string name;
        std::size_t best = 0;
        for (const auto& [candidate, count] : e.names)
            if (!candidate.empty() && count > best) name = candidate, best = count;
        e.file = TexturePack::file_name(name, int(e.facts.width), int(e.facts.height), hash);
        if (!e.staged.empty()) {
            fs::rename(e.staged, s.textures / e.file, error);
            if (error) s.errors.push_back("cannot move " + e.staged.string() + ": " + error.message());
        }
    }
    fs::remove(s.staging, error);

    std::ofstream manifest(s.options.out / "manifest.tsv", std::ios::binary);
    manifest << "hash\tfile\tnames\twidth\theight\tformat\tpalette_entries\tpalette_group\tpalette_variants\talpha\t"
                "alpha0_texels\talpha_partial_texels\talpha_test\tblend\twrap\tfilter\tlanguages\tusa\tstages\tinstances\t"
                "documents\tuses\tpack_names\tuv_range\ttiles\tuv_edges\tuv_motion\tatlas\tuv_mask\tsource\n";
    for (const auto hash : hashes) {
        Entry& e = s.entries[hash];
        std::set<std::string> names;
        for (const auto& [name, count] : e.names)
            if (!name.empty()) names.insert(name);
        if (e.quads.size() >= 2 || (e.quads.size() == 1 && *e.quads.begin() != std::array<int, 4>{0, 0, int(e.facts.width), int(e.facts.height)}))
            e.atlas.insert("quads:" + std::to_string(e.quads.size()));
        char range[96] = "-";
        if (e.uv.valid)
            std::snprintf(range, sizeof range, "%.3f..%.3f,%.3f..%.3f", double(e.uv.u0), double(e.uv.u1), double(e.uv.v0), double(e.uv.v1));
        const std::string tiles = !e.uv.valid && e.motion.empty() ? "-"
                                  : e.tile_s && e.tile_t          ? "st"
                                  : e.tile_s                      ? "s"
                                  : e.tile_t                      ? "t"
                                                                  : "no";
        const std::string edges = tiles == "-" ? "-" : e.edge_s && e.edge_t ? "st" : e.edge_s ? "s" : e.edge_t ? "t" : "no";
        std::string mask = "-";
        if (e.mask_full) mask = "full";
        else if (!e.mask.empty() && s.options.masks) mask = "uvmask/" + hex16(hash) + ".png";
        std::string stages;
        if (e.stages.contains("common")) stages = "common";
        for (const auto& stage : e.stages)
            if (stage != "common") stages += (stages.empty() ? "" : ",") + stage;
        if (stages.empty()) stages = "-";
        if (e.usa) ++summary.usa_unique;
        if (e.uses) ++summary.used_unique;
        if (e.tile_s || e.tile_t) ++summary.tiling_unique;
        if (!e.atlas.empty()) ++summary.atlas_unique;
        manifest << hex16(hash) << '\t' << e.file << '\t' << join(names) << '\t' << e.facts.width << '\t' << e.facts.height << '\t'
                 << (e.facts.cmpr ? "CMPR" : "C8") << '\t' << (e.facts.cmpr ? std::string("-") : std::to_string(e.palette_entries)) << '\t'
                 << (e.has_palette_group ? hex16(e.palette_group) : std::string("-")) << '\t'
                 << (e.has_palette_group ? variants[e.palette_group] : 1) << '\t' << alpha_kind_name(e.facts.alpha) << '\t'
                 << e.facts.transparent << '\t' << e.facts.partial << '\t' << join(e.alpha_tests) << '\t' << join(e.blends) << '\t'
                 << join(e.wraps) << '\t' << join(e.filters) << '\t' << join(e.languages) << '\t' << (e.usa ? 1 : 0) << '\t' << stages
                 << '\t' << e.instances << '\t' << e.documents.size() << '\t' << e.uses << '\t' << join(e.pack_names) << '\t'
                 << range << '\t' << tiles << '\t' << edges << '\t' << join(e.motion) << '\t' << join(e.atlas, ";") << '\t' << mask
                 << '\t' << clean(e.first_source) << '\n';
        if (!e.cells.empty() && !e.atlas.empty()) {
            std::ofstream cells(s.options.out / "atlas" / (hex16(hash) + ".tsv"), std::ios::binary);
            cells << "source\tlabel\tx0\ty0\tx1\ty1\n";
            for (const auto& c : e.cells) cells << c.source << '\t' << c.label << '\t' << c.x0 << '\t' << c.y0 << '\t' << c.x1 << '\t' << c.y1 << '\n';
        }
        if (mask.rfind("uvmask/", 0) == 0) {
            // One texel of margin: bilinear sampling reads the neighbours.
            const int w = int(e.facts.width), h = int(e.facts.height);
            std::vector<std::uint8_t> grown(e.mask.size(), 0);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    if (!e.mask[std::size_t(y) * std::size_t(w) + std::size_t(x)]) continue;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if (nx >= 0 && ny >= 0 && nx < w && ny < h) grown[std::size_t(ny) * std::size_t(w) + std::size_t(nx)] = 255;
                        }
                }
            if (!TexturePack::write_png(s.options.out / mask, grown.data(), w, h, 1)) s.errors.push_back("cannot write " + mask);
        }
    }
    if (!manifest) s.errors.push_back("cannot write manifest.tsv");
    manifest.close();
    summary.errors = s.errors.size();
    summary.warnings = s.warnings.size();
    summary.named_bindings = s.named_bindings;
    summary.fallback_bindings = s.fallback_bindings;
    summary.unresolved_bindings = s.unresolved_bindings;

    std::ofstream text(s.options.out / "summary.txt", std::ios::binary);
    const auto report = [&](std::ostream& o) {
        o << "documents " << summary.documents << "\nimages " << summary.images << "\nunique " << summary.unique << "\nusa_unique "
          << summary.usa_unique << "\ndrawn_unique " << summary.used_unique << "\ntiling_unique " << summary.tiling_unique
          << "\natlas_unique " << summary.atlas_unique << "\nnamed_bindings " << summary.named_bindings << "\nfallback_bindings "
          << summary.fallback_bindings << " (" << join(s.fallback_names) << ")\nunresolved_bindings " << summary.unresolved_bindings
          << " (" << join(s.unresolved_names) << ")\nerrors " << summary.errors << "\nwarnings " << summary.warnings << '\n';
        for (const auto& line : s.errors) o << "error: " << line << '\n';
        for (const auto& line : s.warnings) o << "warning: " << line << '\n';
    };
    report(text);
    report(log);
    return summary;
}

Exporter::Summary export_all(const MountedResources& resources, const Exporter::Options& options, std::ostream& log) {
    Exporter exporter(options);
    const auto& catalog = resources.catalog();
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::string>> aliases, owners;
    std::set<std::string> stages;
    for (const auto& r : catalog.resources) {
        if (r.location) aliases[{r.location->archive_index, r.location->entry_index}].push_back(r.logical_path);
        const std::string path = lower(r.logical_path);
        const auto slash = path.find('/');
        if (slash != std::string::npos) {
            if (stage_like(path.substr(0, slash))) stages.insert(path.substr(0, slash));
        } else if (path.size() > 4 && path.substr(path.size() - 4) == ".igb" && stage_like(path)) {
            stages.insert(path.substr(0, path.size() - 4));
        }
    }
    exporter.set_stage_names(stages);
    for (const auto& r : catalog.resources) {
        if (!r.location || r.logical_path.size() < 4 || lower(r.logical_path.substr(r.logical_path.size() - 4)) != ".igb") continue;
        try {
            const auto package = load_package_descriptor(resources, r.logical_path);
            for (const auto& member : package.members)
                if (member.location) owners[{member.location->archive_index, member.location->entry_index}].push_back(r.logical_path);
        } catch (const std::exception&) {
            // Not a package descriptor.
        }
    }
    const auto& archives = resources.archives();
    std::vector<std::pair<std::uint32_t, DocumentSource>> deferred;
    for (std::uint32_t a = 0; a < archives.size(); ++a) {
        const auto& archive = archives[a];
        for (std::uint32_t e = 0; e < archive.entries().size(); ++e) {
            const auto header = archive.read_prefix(e, 48);
            if (header.size() < 48) continue;
            // The IGB cookie at +40, either byte order (as the catalog reader).
            const bool little = header[40] == 0xda && header[41] == 0xfa && header[42] == 0 && header[43] == 0;
            const bool big = header[40] == 0 && header[41] == 0 && header[42] == 0xfa && header[43] == 0xda;
            if (!little && !big) continue;
            DocumentSource source;
            source.archive = archive.path().filename().string();
            source.entry = e;
            if (const auto found = aliases.find({a, e}); found != aliases.end()) source.aliases = found->second;
            if (const auto found = owners.find({a, e}); found != owners.end()) source.packages = found->second;
            IgbDocument document;
            try {
                document = read_igb_document(archive.read(e));
            } catch (const std::exception& error) {
                exporter.note_error(source.archive + ':' + std::to_string(e) + ": " + error.what());
                continue;
            }
            if (exporter.add_document(document, source)) deferred.emplace_back(a, std::move(source));
        }
    }
    // Second pass: the documents that bind pack textures by name, now that
    // every pack is known.
    for (const auto& [a, source] : deferred) {
        try {
            exporter.add_bindings(read_igb_document(archives[a].read(source.entry)), source);
        } catch (const std::exception& error) {
            exporter.note_error(source.archive + ':' + std::to_string(source.entry) + ": " + error.what());
        }
    }
    return exporter.finish(log);
}

} // namespace dw4::texpack
