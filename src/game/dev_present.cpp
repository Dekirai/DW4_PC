#include "dev_present.hpp"

#include "settings.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace dw4::game {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::optional<int> env_int(const char* name) {
    if (const char* text = std::getenv(name); text && *text) return std::atoi(text);
    return std::nullopt;
}

const char* kind_name(engine::NodeKind kind) {
    using K = engine::NodeKind;
    switch (kind) {
    case K::group: return "group";
    case K::attr_set: return "attr_set";
    case K::switch_node: return "switch";
    case K::transform: return "transform";
    case K::geometry: return "geometry";
    case K::actor: return "actor";
    case K::billboard: return "billboard";
    case K::blend_matrix_select: return "blend_matrix_select";
    case K::light_set: return "light_set";
    case K::light_state_set: return "light_state_set";
    case K::camera: return "camera";
    case K::model_node: return "model_node";
    case K::lod: return "lod";
    case K::shadow_shader: return "shadow_shader";
    case K::bone_select: return "bone_select";
    case K::copy_model: return "copy_model";
    case K::particle_timer: return "particle_timer";
    case K::transform_recorder: return "transform_recorder";
    case K::node_anim: return "node_anim";
    case K::reset_node: return "reset_node";
    default: return "unknown";
    }
}

std::string texture_name(const engine::RenderState& state) {
    if (!state.texture_enabled || !state.texture || !state.texture->image) return "-";
    return std::string(state.texture->image.str(22));
}

// An orthographic (2D layer) item.
bool flat(const engine::DrawItem& item) { return item.state.projection && (*item.state.projection)(2, 3) == 0.0f; }

bool particle(const engine::DrawItem& item) { return item.node && item.node->kind == engine::NodeKind::particle_timer; }

bool matches(const engine::DrawItem& item, const std::string& pattern) {
    if (pattern == ":blend") return item.state.blend_enabled;
    if (pattern == ":opaque") return !item.state.blend_enabled;
    if (pattern == ":2d") return flat(item);
    if (pattern == ":3d") return !flat(item);
    if (pattern == ":skinned") return item.palette != nullptr;
    if (pattern == ":particles") return particle(item);
    if (pattern == ":texmatrix") return item.state.texture_matrix.has_value();
    if (pattern.empty() || pattern[0] == ':') return false;
    if (item.node && lower(item.node->name).find(pattern) != std::string::npos) return true;
    return lower(texture_name(item.state)).find(pattern) != std::string::npos;
}

// glReadPixels rows (bottom first) as top-first RGB.
std::vector<std::uint8_t> top_down_rgb(int width, int height, const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> rgb(std::size_t(width) * std::size_t(height) * 3);
    for (int y = 0; y < height; ++y) {
        const auto* from = rgba.data() + std::size_t(height - 1 - y) * std::size_t(width) * 4;
        auto* to = rgb.data() + std::size_t(y) * std::size_t(width) * 3;
        for (int x = 0; x < width; ++x) {
            to[x * 3] = from[x * 4];
            to[x * 3 + 1] = from[x * 4 + 1];
            to[x * 3 + 2] = from[x * 4 + 2];
        }
    }
    return rgb;
}

std::filesystem::path numbered(const std::filesystem::path& dir, const char* prefix, int present, const char* extension) {
    char name[64];
    std::snprintf(name, sizeof name, "%s_%06d.%s", prefix, present, extension);
    return dir / name;
}

} // namespace

DevPresent DevPresent::from_environment() {
    DevPresent dev;
    if (const char* text = std::getenv("DW4_DEV_PRESENT"); text && *text) dev.hz = std::max(0.0, std::atof(text));
    if (const char* path = std::getenv("DW4_DEV_SETTINGS"); path && *path) dev.settings_file = path;
    if (const auto v = env_int("DW4_DEV_SMOOTH")) dev.smooth = *v != 0;
    if (const auto v = env_int("DW4_DEV_WIDESCREEN")) dev.widescreen = *v != 0;
    if (const auto v = env_int("DW4_DEV_MSAA")) dev.msaa = std::clamp(*v, 0, 16);
    if (const char* size = std::getenv("DW4_DEV_WINDOW"); size && *size) {
        int w = 0, h = 0;
        if (std::sscanf(size, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            dev.width = w;
            dev.height = h;
        }
    }
    if (const char* list = std::getenv("DW4_DEV_DUMP"); list && *list) {
        for (const char* at = list; at && *at;) {
            int from = 0, to = -1;
            if (std::sscanf(at, "%d-%d", &from, &to) == 2) dev.dump_ranges.emplace_back(from, to);
            at = std::strchr(at, ',');
            if (at) ++at;
        }
        if (const char* dir = std::getenv("DW4_DEV_DUMP_DIR"); dir && *dir) dev.dump_dir = dir;
    }
    if (const auto v = env_int("DW4_DEV_IDS")) dev.ids = *v != 0;
    if (const auto v = env_int("DW4_DEV_PICTURES")) dev.pictures = *v != 0;
    if (const char* list = std::getenv("DW4_DEV_HIDE"); list && *list) {
        std::string text = lower(list);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto comma = text.find(',', start);
            auto part = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!part.empty()) dev.hide.push_back(part);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return dev;
}

void DevPresent::apply(Settings& settings) const {
    if (!settings_file.empty()) {
        auto ini = IniFile::load(settings_file);
        if (!ini) throw std::runtime_error("DW4_DEV_SETTINGS: cannot read " + settings_file.string());
        const Settings file = Settings::parse(std::move(*ini));
        settings.width = file.width;
        settings.height = file.height;
        settings.widescreen = file.widescreen;
        settings.msaa = file.msaa;
        settings.smooth_motion = file.smooth_motion;
        settings.fast_loading = file.fast_loading;
        settings.skip_intro = file.skip_intro;
    }
    if (smooth) settings.smooth_motion = *smooth;
    if (widescreen) settings.widescreen = *widescreen;
    if (msaa) settings.msaa = *msaa;
    if (width) {
        settings.width = *width;
        settings.height = *height;
    }
}

void dev_hide(const DevPresent& dev, std::vector<engine::DrawItem>& items, engine::ShadowFrame& shadow) {
    if (dev.hide.empty()) return;
    for (const auto& pattern : dev.hide)
        if (pattern == ":shadow") shadow.active = false;
    std::erase_if(items, [&](const engine::DrawItem& item) {
        return std::any_of(dev.hide.begin(), dev.hide.end(), [&](const std::string& pattern) { return matches(item, pattern); });
    });
}

DevDumper::DevDumper(const DevPresent& dev) : dir_(dev.dump_dir) {
    if (dir_.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(dir_, error);
    csv_.open(dir_ / "presents.csv", std::ios::trunc);
    csv_ << "present,frame,steps,alpha,blended,stage,items,items_blended,unmatched,far,swapped,palettes,palettes_blended,"
            "palettes_far,palettes_shared,palettes_resized,camera,light,jumps\n";
}

void DevDumper::picture(int present, int frame, int steps, float alpha, const BlendReport* report, const std::string& stage,
                        int width, int height, const std::vector<std::uint8_t>& rgba) {
    if (dir_.empty()) return;
    if (!rgba.empty()) {
        const auto rgb = top_down_rgb(width, height, rgba);
        stbi_write_png_compression_level = 1;
        stbi_write_png(numbered(dir_, "present", present, "png").string().c_str(), width, height, 3, rgb.data(), width * 3);
    }
    char line[320];
    std::snprintf(line, sizeof line, "%d,%d,%d,%.4f,%d,", present, frame, steps, alpha, report ? 1 : 0);
    csv_ << line << stage;
    const BlendReport r = report ? *report : BlendReport{};
    std::snprintf(line, sizeof line, ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", r.items, r.blended, r.unmatched, r.far,
                  r.swapped, r.palettes, r.palettes_blended, r.palettes_far, r.palettes_shared, r.palettes_resized,
                  r.camera ? 1 : 0, r.light ? 1 : 0, r.jumps);
    csv_ << line;
    csv_.flush();
    if (report && !report->flagged.empty()) {
        std::ofstream out(numbered(dir_, "blend", present, "txt"), std::ios::trunc);
        out << "# reason\tdistance\tnode\tkind\ttexture\tx\ty\tz\tnode_addr\tgeometry_addr\tturn\tturn_before\n";
        for (const auto& flag : report->flagged) {
            const auto& item = *flag.item;
            char text[512];
            std::snprintf(text, sizeof text, "%s\t%.2f\t%s\t%s\t%s\t%.2f\t%.2f\t%.2f\t%p\t%p\t%.4f\t%.4f\n", flag.reason, flag.distance,
                          item.node && !item.node->name.empty() ? item.node->name.c_str() : "-",
                          item.node ? kind_name(item.node->kind) : "-", texture_name(item.state).c_str(), item.model(3, 0),
                          item.model(3, 1), item.model(3, 2), static_cast<const void*>(item.node),
                          static_cast<const void*>(item.geometry), flag.turn, flag.turn_before);
            out << text;
        }
    }
}

void DevDumper::ids(int present, int width, int height, const std::vector<std::uint8_t>& rgba,
                    const std::vector<engine::DrawItem>& items, const std::unordered_map<const void*, std::string>& files) {
    if (dir_.empty()) return;
    const auto rgb = top_down_rgb(width, height, rgba);
    stbi_write_png_compression_level = 1;
    stbi_write_png(numbered(dir_, "ids", present, "png").string().c_str(), width, height, 3, rgb.data(), width * 3);
    std::ofstream out(numbered(dir_, "items", present, "txt"), std::ios::trunc);
    out << "# index\tnode\tkind\tfile#ref\ttexture\tflags\tx\ty\tz\tnode_addr\tgeometry_addr\tmodel 3x3 rows\t[uv translation]\n";
    std::size_t index = 0;
    for (const auto& item : items) {
        ++index;
        const auto& s = item.state;
        std::string flags;
        flags += s.blend_enabled ? 'B' : '-';
        flags += s.depth_test ? 'D' : '-';
        flags += s.depth_write ? 'W' : '-';
        flags += s.alpha_test ? 'A' : '-';
        flags += flat(item) ? '2' : '3';
        flags += item.palette ? 'S' : '-';
        flags += particle(item) ? 'P' : '-';
        flags += s.texture_matrix ? 'T' : '-';
        flags += s.viewport ? 'V' : '-';
        std::string origin = "-";
        if (item.node && item.node->object) {
            const auto found = files.find(item.node->object.document());
            origin = (found != files.end() ? found->second : std::string("?")) + "#" + std::to_string(item.node->object.reference());
        }
        char line[768];
        std::snprintf(line, sizeof line, "%zu\t%s\t%s\t%s\t%s\t%s\t%.2f\t%.2f\t%.2f\t%p\t%p", index,
                      item.node && !item.node->name.empty() ? item.node->name.c_str() : "-",
                      item.node ? kind_name(item.node->kind) : "-", origin.c_str(), texture_name(s).c_str(), flags.c_str(),
                      item.model(3, 0),
                      item.model(3, 1), item.model(3, 2), static_cast<const void*>(item.node),
                      static_cast<const void*>(item.geometry));
        out << line;
        // The model matrix's 3x3 part (rows) and the texture matrix's
        // translation (UV scroll), for comparing presents.
        std::snprintf(line, sizeof line, "\t%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f", item.model(0, 0), item.model(0, 1),
                      item.model(0, 2), item.model(1, 0), item.model(1, 1), item.model(1, 2), item.model(2, 0), item.model(2, 1),
                      item.model(2, 2));
        out << line;
        if (s.texture_matrix) {
            std::snprintf(line, sizeof line, "\tuv %.4f %.4f", (*s.texture_matrix)(3, 0), (*s.texture_matrix)(3, 1));
            out << line;
        }
        out << '\n';
    }
}

} // namespace dw4::game
