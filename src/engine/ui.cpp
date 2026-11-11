#include "dw4/engine/managers.hpp"
#include "dw4/engine/randomizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace dw4::engine {
namespace {
template <class RamT>
void register_rams(Engine& engine, Manager* manager, const ObjectView& info, std::vector<std::unique_ptr<RamT>>& out,
                   const std::string& category) {
    std::int32_t index = 0;
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<RamT>();
        ram->manager = manager;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = index++;
        ram->active = record ? record.boolean(5) : false;
        engine.messenger().add_row(category, ram->info_name, ram->index, ram.get());
        out.push_back(std::move(ram));
    }
}

Node* find_geometry(Node* node, int depth = 0) {
    if (!node || depth > 8) return nullptr;
    if (node->kind == NodeKind::geometry) return node;
    for (auto* child : node->children) if (auto* found = find_geometry(child, depth + 1)) return found;
    return nullptr;
}

// Shift JIS runs of the 802FB59C conversion table: (code, UTF-16, count).
struct SjisRun { std::uint16_t sjis, unicode, count; };
constexpr SjisRun kSjisRuns[] = {
    {0x8140, 0x3000, 1}, {0x8141, 0x3001, 2}, {0x8143, 0xFF0C, 1}, {0x8144, 0xFF0E, 1}, {0x8145, 0x30FB, 1},
    {0x8146, 0xFF1A, 1}, {0x8148, 0xFF1F, 1}, {0x8149, 0xFF01, 1}, {0x814F, 0xFF3E, 1}, {0x8151, 0xFF3F, 1},
    {0x815B, 0x30FC, 1}, {0x8160, 0xFF5E, 1}, {0x8162, 0xFF5C, 1}, {0x8163, 0x2026, 1}, {0x8166, 0x2019, 1},
    {0x8168, 0x201D, 1}, {0x8169, 0xFF08, 2}, {0x8171, 0x3008, 2}, {0x8175, 0x300C, 4}, {0x817B, 0xFF0B, 1},
    {0x817C, 0xFF0D, 1}, {0x817E, 0x00D7, 1}, {0x8187, 0x221E, 1}, {0x8195, 0xFF06, 1}, {0x819B, 0x25CB, 1},
    {0x81A0, 0x25A1, 1}, {0x81A2, 0x25B3, 1}, {0x81AA, 0x2191, 1}, {0x81AB, 0x2193, 1}, {0x81B8, 0x2208, 1},
    {0x81B9, 0x220B, 1}, {0x81BA, 0x2286, 1}, {0x81BC, 0x2282, 2}, {0x81BE, 0x222A, 1}, {0x81BF, 0x2229, 1},
    {0x81C8, 0x2227, 1}, {0x81C9, 0x2228, 1}, {0x81DA, 0x2220, 1}, {0x81DB, 0x22A5, 1}, {0x81DC, 0x2312, 1},
    {0x81DE, 0x2207, 1}, {0x81DF, 0x2261, 1}, {0x81E0, 0x2252, 1}, {0x81E1, 0x226A, 2}, {0x81E3, 0x221A, 1},
    {0x81E6, 0x2235, 1}, {0x81F7, 0x00B6, 1}, {0x824F, 0xFF10, 10}, {0x8260, 0xFF21, 26}, {0x8281, 0xFF41, 26},
    {0x829F, 0x3041, 79}, {0x82F0, 0x3092, 2}, {0x8340, 0x30A1, 63}, {0x8380, 0x30E0, 16}, {0x8392, 0x30F2, 3},
    {0x83BF, 0x03B1, 5}, {0x889F, 0x4E9C, 1}, {0x88A1, 0x5A03, 1}, {0x88A2, 0x00B5, 1}, {0x88A3, 0x00BF, 1},
    {0x88A4, 0x00C3, 5}, {0x88A9, 0x00D1, 1}, {0x88AA, 0x00D5, 2}, {0x88AC, 0x00D8, 1}, {0x88AD, 0x00DC, 1},
    {0x88AE, 0x00DF, 2}, {0x88B0, 0x00E2, 3}, {0x88B3, 0x00E7, 6}, {0x88B9, 0x00EE, 2}, {0x88BB, 0x00F1, 2},
    {0x88BD, 0x00F4, 3}, {0x88C0, 0x00F9, 1}, {0x88C1, 0x00FB, 2}, {0x88C3, 0x00E1, 1}, {0x88C4, 0x00ED, 1},
    {0x88C5, 0x00F3, 1}, {0x88C6, 0x00FA, 1}, {0x88C7, 0x00C2, 1}, {0x88C8, 0x00CA, 1}, {0x88C9, 0x00D4, 1},
    {0x88CA, 0x00CB, 1}, {0x88CB, 0x00CF, 1}, {0x88CC, 0x00C1, 1}, {0x88CD, 0x00C9, 1}, {0x88CE, 0x00CD, 1},
    {0x88CF, 0x00D3, 1}, {0x88D0, 0x00DA, 1}, {0x88D1, 0x00C0, 1}, {0x88D2, 0x00C8, 1}, {0x88D3, 0x00CC, 1},
    {0x88D4, 0x00D2, 1}, {0x88D5, 0x00D9, 1}, {0x88D6, 0x00B0, 1}, {0x88D7, 0x00A1, 1}, {0x88D8, 0x00A3, 1},
    {0x88D9, 0x00A7, 1}, {0x8A71, 0x8D6B, 1}
};

} // namespace

std::u16string decode_game_text(std::string_view text) {
    std::u16string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (!(c & 0x80) || i + 1 >= text.size()) {
            out.push_back(char16_t(c));
            continue;
        }
        const auto code = std::uint16_t((c << 8) | static_cast<unsigned char>(text[++i]));
        for (const auto& run : kSjisRuns)
            if (code >= run.sjis && code < run.sjis + run.count) {
                out.push_back(char16_t(run.unicode + (code - run.sjis)));
                break;
            }
    }
    return out;
}

std::string game_text_utf8(std::string_view text) {
    std::string out;
    for (const char16_t c : decode_game_text(text)) {
        if (c < 0x80) {
            out.push_back(char(c));
        } else if (c < 0x800) {
            out.push_back(char(0xC0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else {
            out.push_back(char(0xE0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// ===========================================================================
// TEXTCTL
// ===========================================================================
void TextCtlManager::load_fonts(Engine& engine) {
    if (!fonts_.empty()) return;
    try {
        font_document_ = std::make_shared<const IgbDocument>(read_igb_document(engine.resources().read("bectext.igb")));
    } catch (const std::exception& error) {
        engine.note(std::string("font bectext.igb: ") + error.what());
        return;
    }
    const auto* doc = font_document_.get();
    for (const auto& object : doc->objects) {
        if (doc->metadata.object_types[object.type_index].name != "beFontImage") continue;
        ObjectView image(doc, object.reference_index);
        auto font = std::make_unique<FontImage>();
        font->name = std::string(image.str(2));
        font->line_height = image.f32(3);
        if (const auto bind = image.ref(8)) {
            font->binding = std::make_unique<Attribute>();
            font->binding->kind = AttrKind::texture_bind;
            font->binding->object = bind;
            font->binding->texture = bind.ref(4);
            if (font->binding->texture) {
                font->binding->image = font->binding->texture.ref(12);
                // font00.png is palette-indexed, so the glyphs sample bilinearly
                // although its igTextureAttr says NEAREST (see texture_sampler;
                // measured in Dolphin: a line centred on x.5 px is the 7/12 :
                // 5/12 mix of neighbouring texels, an x.0 line keeps a 1/12
                // ghost). The wraps stay the attribute's REPEAT.
                font->binding->sampler = texture_sampler(font->binding->texture);
            }
        }
        if (const auto metrics = image.ref(7)) {
            const auto count = metrics.u32(2);
            const auto data = metrics.memory(4);
            for (std::uint32_t i = 0; i < count && (i + 1) * 40 <= data.size(); ++i) {
                const std::size_t o = std::size_t(i) * 40;
                Glyph g;
                // The character code is 16 bits; the rest of the word is padding.
                const auto code = read_u32le(data, o) & 0xFFFFu;
                g.advance = read_f32le(data, o + 4);
                g.x_offset = read_f32le(data, o + 8);
                g.y_offset = read_f32le(data, o + 12);
                g.width = read_f32le(data, o + 16);
                g.height = read_f32le(data, o + 20);
                g.u0 = read_f32le(data, o + 24);
                g.v0 = read_f32le(data, o + 28);
                g.u1 = read_f32le(data, o + 32);
                g.v1 = read_f32le(data, o + 36);
                font->glyphs[code] = g;
            }
        }
        fonts_[font->name] = std::move(font);
    }
}

void TextCtlManager::add_info(Engine& engine, const ObjectView& info) {
    load_fonts(engine);
    register_rams(engine, this, info, rams_, "TEXTCTL");
    auto& strings = strings_[{info.document(), info.reference()}];
    strings = info.list(7);
    for (const auto& data : info.list(5)) {
        if (!data) continue;
        Line line;
        line.document = info.document();
        line.info = info.reference();
        line.layer = std::string(data.str(2));
        line.node = engine.scenes().node(data.ref(4));
        line.name = line.node ? line.node->name : std::string();
        line.category = data.i32(5);
        line.index = data.i32(6);
        line.max_width = data.f32(7);
        line.spacing = data.f32(8);
        line.align = data.i32(9);
        line.style = std::string(data.str(10));
        line.strings = &strings;
        // Text lines only ever show generated glyphs; hide the authored placeholder.
        if (Node* geometry = find_geometry(line.node); geometry && !geometry->runtime_geometry)
            geometry->runtime_geometry = std::make_shared<GeometryData>();
        lines_.push_back(std::move(line));
    }
}

const Node* TextCtlManager::line_node(std::string_view layer, std::string_view name) const {
    for (const auto& line : lines_)
        if (line.layer == layer && line.name == name) return line.node;
    return nullptr;
}

const FontImage* TextCtlManager::font(std::string_view name) const {
    const auto found = fonts_.find(std::string(name));
    return found == fonts_.end() ? nullptr : found->second.get();
}

void TextCtlManager::clear_stage(Engine&) {
    rams_.clear();
    lines_.clear();
    strings_.clear();
    geometry_.clear();
}

// 802FA900 INITIALIZE: the FONT layer's first child switch holds one child per
// category; each category's first child switch holds its lines.
Node* TextCtlManager::root_switch(Engine& engine) {
    // The host layer group wraps the FONT scene root; descend to the switch.
    Node* font = engine.find_named_node(engine.global_root(), "FONT");
    for (int depth = 0; font && !font->children.empty() && depth < 4; ++depth) {
        if (font->children[0]->kind == NodeKind::switch_node) return font->children[0];
        font = font->children[0];
    }
    return nullptr;
}

void TextCtlManager::switch_line(Engine& engine, const Line& line, bool on) {
    Node* root = root_switch(engine);
    if (!root || line.category < 0 || std::size_t(line.category) >= root->children.size()) return;
    Node* category = root->children[std::size_t(line.category)];
    if (on) root->set_child_enabled(std::size_t(line.category), true);
    if (category->children.empty()) return;
    Node* lines = category->children[0];
    if (lines->kind == NodeKind::switch_node && line.index >= 0) lines->set_child_enabled(std::size_t(line.index), on);
}

// A category stays visible exactly while one of its lines is enabled.
void TextCtlManager::refresh_categories(Engine& engine) {
    Node* root = root_switch(engine);
    if (!root) return;
    for (std::size_t i = 0; i < root->children.size(); ++i) {
        Node* category = root->children[i];
        if (category->children.empty() || category->children[0]->kind != NodeKind::switch_node) continue;
        const Node* lines = category->children[0];
        bool any = false;
        for (std::size_t j = 0; j < lines->children.size() && !any; ++j) any = lines->child_enabled(j);
        root->set_child_enabled(i, any);
    }
}

void TextCtlManager::set_text(Engine& engine, Line& target, const std::u16string& value) {
    // DW4_TRACE_TEXT logs every text assignment (non-ASCII as <hex>) for smoke tests.
    static const bool trace = std::getenv("DW4_TRACE_TEXT") != nullptr;
    if (trace) {
        std::string ascii;
        char hex[8];
        for (char16_t c : value) {
            if (c >= 32 && c < 127) ascii.push_back(char(c));
            else { std::snprintf(hex, sizeof hex, "<%04x>", unsigned(c)); ascii += hex; }
        }
        engine.log("text " + target.layer + "/" + target.name + ": " + ascii);
    }
    target.text = value;
    Node* geometry_node = find_geometry(target.node);
    if (!geometry_node) return;
    auto font_it = fonts_.find(target.style.empty() ? "NORMAL" : target.style);
    if (font_it == fonts_.end()) font_it = fonts_.find("NORMAL");
    if (font_it == fonts_.end()) return;
    const FontImage& font = *font_it->second;
    auto geometry = std::make_shared<GeometryData>(layout_text(font, value, target.align, target.max_width));
    geometry_node->runtime_geometry = geometry;
    geometry_node->runtime_texture = font.binding.get();
    geometry_.push_back(std::move(geometry));
}

GeometryData layout_text(const FontImage& font, std::u16string_view text, int align, float max_width) {
    const float line_height = font.line_height > 0 ? font.line_height : 0.28f;
    GeometryData geometry;
    // 80116960 fills the quads on one pen that runs through the whole string
    // (no font has '\r' or '\n', so nothing steps back); 802FBF98 (called at
    // 802FBC78 for every alignment) then moves each line onto its own row.
    float pen = 0;          // 80116960: the pen starts at the font's +16 (0)
    float shift = 0;        // 802FBF98 f31: x move of the current line
    float row = 0;          // 802FBF98 f30: y move of the current line
    bool laid_out = false;  // 802FBF98 r29 != 0
    int breaks = 0;         // line breaks still waiting for the next quad's v0.x
    float last_right = 0;   // v5.x of the last quad, already moved
    std::vector<std::pair<std::size_t, float>> lines; // end vertex, width (lists +0x30 / +0x34)
    // 0x802FC010-0x802FC0CC: at a line break the line's width is
    // vertex[r29].x - |f31| and the next line moves by f31 = -vertex[r29].x,
    // vertex[r29] being the not yet moved v0 of the next quad (the pen of the
    // line end plus that glyph's x offset). Its first glyph lands on x 0.
    auto break_lines = [&](float next_x0) {
        for (; breaks > 0; --breaks) {
            lines.emplace_back(geometry.positions.size(), next_x0 - std::fabs(shift));
            shift = -next_x0;
        }
    };
    for (const char16_t c : text) {
        if (c == u'\r') continue; // 0x802FBFF4
        if (c == u'\n') {
            if (!laid_out) continue; // 0x802FC008: breaks before the first character are dropped
            ++breaks;
            row -= line_height; // 0x802FC0D0: the font's +12
            continue;
        }
        laid_out = true;
        auto found = font.glyphs.find(std::uint32_t(c));
        if (found == font.glyphs.end()) {
            // Port fallback: 80116960 leaves the character out, but 802FBF98
            // still counts 6 vertices for it (0x802FC100) and so moves the
            // following quads' slots; scripts swap such characters out first
            // with REPLACEUNKNOWN (0x802FB428).
            pen += 0.06f;
            continue;
        }
        const Glyph& g = found->second;
        // 80116A1C: x0 = pen + x offset, x1 = x0 + width (no width test), the
        // glyph stands on the pen (bottom y + y offset, top one height above).
        const float x0 = pen + g.x_offset, x1 = x0 + g.width;
        break_lines(x0);
        last_right = x1 + shift;
        if (g.width > 0 && g.height > 0) {
            const float y0 = g.y_offset + row, y1 = (g.y_offset + g.height) + row;
            const auto base = std::uint32_t(geometry.positions.size());
            geometry.positions.push_back({x0 + shift, y1, 0});
            geometry.positions.push_back({x1 + shift, y1, 0});
            geometry.positions.push_back({x1 + shift, y0, 0});
            geometry.positions.push_back({x0 + shift, y0, 0});
            geometry.texcoords.insert(geometry.texcoords.end(), {g.u0, g.v1, g.u1, g.v1, g.u1, g.v0, g.u0, g.v0});
            geometry.indices.insert(geometry.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        }
        pen += g.advance; // 0x801169E8
    }
    // A string that ends in a line break reads the stale vertex after the last
    // quad in the original; the port measures that line to its pen instead.
    break_lines(pen);
    // 0x802FC29C-0x802FC2A8: the last line's width is the moved v5.x of its
    // last quad, the right edge of the glyph and not the pen after it.
    lines.emplace_back(geometry.positions.size(), last_right);
    // 802FBC7C / 802FC2D0: beFontInfoData [9] (enum 804D2048: LEFT 0, RIGHT 1,
    // CENTER 2) moves each line by (max width - line width) * 0.5 when centred
    // or the whole rest when right-aligned, unrounded and whatever the max width.
    if (align == 1 || align == 2) {
        std::size_t begin = 0;
        for (const auto& [end, width] : lines) {
            const float move = align == 2 ? (max_width - width) * 0.5f : max_width - width;
            for (auto v = begin; v < end; ++v) geometry.positions[v].x += move;
            begin = end;
        }
    }
    geometry.valid = !geometry.indices.empty();
    return geometry;
}

void TextCtlManager::make_copies(Engine& engine, int slot, const Ram& ram, std::string_view layer, std::string_view name) {
    Node* root = root_switch(engine);
    if (!root) return;
    const std::size_t count = lines_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Line original = lines_[i];
        if (original.slot >= 0 || original.document != ram.info.document() || original.info != ram.info.reference()) continue;
        if (!(layer == "ALL" || original.layer == layer) || original.name != name || !original.node) continue;
        const bool made = std::any_of(lines_.begin(), lines_.end(), [&](const Line& line) {
            return line.slot == slot && line.document == original.document && line.info == original.info && line.layer == original.layer &&
                   line.name == original.name;
        });
        if (made || original.category < 0 || std::size_t(original.category) >= root->children.size()) continue;
        Node* category = root->children[std::size_t(original.category)];
        if (category->children.empty() || original.index < 0) continue;
        Node* parent = category->children[0];
        if (std::size_t(original.index) >= parent->children.size()) continue;
        std::size_t index = 0;
        Node* node = engine.scene_copy_line(parent->children[std::size_t(original.index)], original.node, parent, index);
        if (!node) continue;
        Line copy = original;
        copy.slot = slot;
        copy.node = node;
        copy.index = int(index);
        lines_.push_back(std::move(copy));
    }
    // Round J: the topology journal (the RAM by its place in this manager).
    if (lines_.size() != count)
        for (std::size_t r = 0; r < rams_.size(); ++r)
            if (rams_[r].get() == &ram) {
                engine.journal({0, Engine::TopologyEntry::line_copies, std::string(layer), std::string(name), std::int32_t(r), slot});
                break;
            }
}

void TextCtlManager::replay_copies(Engine& engine, int slot, int ram, std::string_view layer, std::string_view name) {
    if (ram < 0 || std::size_t(ram) >= rams_.size()) throw TickStateError("topology", "a text copy of an unknown RAM");
    const Engine::SceneScope scope(engine, slot);
    make_copies(engine, slot, *rams_[std::size_t(ram)], layer, name);
}

bool TextCtlManager::command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
    // Each TEXTCTL RAM answers for the lines of its own beFontInfo.
    auto owned = [&](const Line& line) {
        return line.document == ram.info.document() && line.info == ram.info.reference();
    };
    // Seamless co-op (SEAMLESS.md 5.3): inside a member's own window or
    // scene the lines are its own copies; outside, the originals (always so
    // offline: no scope).
    const int scope = engine.scene_scope();
    if (command == "INITIALIZE") {
        for (auto& line : lines_)
            if (owned(line))
                if (Node* g = find_geometry(line.node)) g->runtime_geometry = std::make_shared<GeometryData>();
        return true;
    }
    // 802FC5AC / 802FC6F4: select lines by layer ("ALL" = any) and line name.
    const std::string layer(packet.string(0));
    const std::string name(packet.string(1));
    if (scope >= 0 && (command == "TEXTSET" || command == "SWITCH" || command == "ALIGN" || command == "COLOR" || command == "DEFAULTCOLOR" ||
                       command == "CANCELCOLOR" || command == "SETPOSITION"))
        make_copies(engine, scope, ram, layer, name);
    auto matches = [&](const Line& line) {
        return owned(line) && line.slot == scope && (layer == "ALL" || line.layer == layer) && line.name == name;
    };
    if (command == "TEXTSET") {
        std::optional<std::u16string> value;
        if (packet.strings.size() > 2) value = decode_game_text(packet.string(2));
        else if (!packet.objects.empty()) value = *static_cast<const std::u16string*>(packet.objects[0]); // string object
        // Randomizer: the exit dialog of a shuffled dungeon entrance names
        // its new target (the line the target's own entrance shows).
        if (!value && layer == "STCHGFONT00" && !packet.ints.empty() && engine.cheats().zone_shuffle())
            value = randomizer::zone_dialog_text(engine, packet.ints[0]);
        for (auto& line : lines_) {
            if (!matches(line)) continue;
            if (!value && !packet.ints.empty()) {
                const auto index = std::size_t(packet.ints[0]);
                std::u16string text;
                if (line.strings && index < line.strings->size() && (*line.strings)[index]) {
                    const auto bytes = (*line.strings)[index].memory(3);
                    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
                        const char16_t c = char16_t(bytes[i] | (bytes[i + 1] << 8));
                        if (!c) break;
                        text.push_back(c);
                    }
                }
                set_text(engine, line, text);
            } else if (value) {
                set_text(engine, line, *value);
            }
        }
        return true;
    }
    if (command == "SWITCH") {
        bool on = false;
        if (packet.strings.size() > 2) {
            const auto mode = packet.string(2);
            on = mode == "SET" || mode == "DISPON";
        } else if (!packet.ints.empty()) {
            on = packet.ints[0] != 0;
        }
        for (auto& line : lines_) if (matches(line)) switch_line(engine, line, on);
        refresh_categories(engine);
        return true;
    }
    if (command == "REPLACEUNKNOWN") return true; // missing glyphs already fall back
    if (command == "ALIGN") {
        // 802FAF38: the mode string through the alignment enum (804D2048) into [9].
        const auto mode = packet.string(2);
        const int align = mode == "CENTER" ? 2 : mode == "RIGHT" ? 1 : 0;
        for (auto& line : lines_) if (matches(line)) line.align = align;
        return true;
    }
    if (command == "COLOR" || command == "DEFAULTCOLOR" || command == "CANCELCOLOR") {
        // 0x802FB2F0-0x802FB3E8: DEFAULTCOLOR 0.9 (0x80425768), CANCELCOLOR
        // 0.5 (0x8042576C), COLOR the packet vector. Every igColorAttr in a
        // matching line node's own attribute list gets that rgb through its
        // setColor (vtable +0x80), its alpha kept. The colour replaces the
        // IGB's (the font lines carry 0.9, alpha 0.8), it does not multiply
        // it: DEFAULTCOLOR leaves a line as it was loaded.
        Vec3f rgb{0.9f, 0.9f, 0.9f};
        if (command == "CANCELCOLOR") rgb = {0.5f, 0.5f, 0.5f};
        else if (command == "COLOR") {
            if (packet.vectors.empty()) return true;
            rgb = packet.vectors[0];
        }
        for (auto& line : lines_) {
            if (!matches(line) || !line.node) continue;
            for (const Attribute* attribute : line.node->attributes) {
                if (attribute->kind != AttrKind::color) continue;
                // (A member's own copy has its own colour attributes.)
                Attribute* color = line.slot >= 0 ? const_cast<Attribute*>(attribute) : engine.scenes().edit_attribute(attribute->object);
                if (!color) continue;
                color->color = Vec4f{rgb.x, rgb.y, rgb.z, color->color.w};
            }
        }
        return true;
    }
    if (command == "SETPOSITION") {
        // 802FB230: moves the first child transform of each matching line.
        if (packet.vectors.empty()) return true;
        const Vec3f v = packet.vectors[0];
        for (auto& line : lines_) {
            if (!matches(line) || !line.node || line.node->children.empty()) continue;
            Node* transform = line.node->children[0];
            if (transform->kind != NodeKind::transform) continue;
            transform->matrix(3, 0) = v.x;
            transform->matrix(3, 1) = v.y;
            transform->matrix(3, 2) = v.z;
        }
        return true;
    }
    return false;
}

// ===========================================================================
// SELECTCTRL
// ===========================================================================
void SelectCtrlManager::add_info(Engine& engine, const ObjectView& info) {
    register_rams(engine, this, info, rams_, "SELECTCTRL");
    for (auto& ram : rams_) {
        if (ram->info.reference() != info.reference() || ram->info.document() != info.document()) continue;
        if (!ram->ram) continue;
        for (const auto& data : ram->ram.list(10)) {
            if (data && data.str(2) == "ONE") {
                ram->vertical = data.boolean(4, true);
                ram->items = std::max(1, data.i32(5, 1));
                break;
            }
        }
    }
}

void SelectCtrlManager::clear_stage(Engine&) { rams_.clear(); }

bool SelectCtrlManager::has(std::string_view name, int index) const {
    for (const auto& ram : rams_)
        if (ram->info_name == name && ram->index == index) return true;
    return false;
}

void SelectCtrlManager::add_copy(Engine& engine, std::string_view name, int index, const std::string& copy) {
    const SelectRam* original = nullptr;
    for (const auto& ram : rams_)
        if (ram->info_name == name && ram->index == index) original = ram.get();
    if (!original) return;
    auto ram = std::make_unique<SelectRam>();
    ram->manager = this;
    ram->info = original->info;
    ram->ram = original->ram;
    ram->info_name = copy;
    ram->index = original->index;
    ram->active = ram->ram ? ram->ram.boolean(5) : false;
    // (The fresh cursor's line layout, as add_info reads it.)
    if (ram->ram)
        for (const auto& data : ram->ram.list(10))
            if (data && data.str(2) == "ONE") {
                ram->vertical = data.boolean(4, true);
                ram->items = std::max(1, data.i32(5, 1));
                break;
            }
    engine.messenger().add_row("SELECTCTRL", ram->info_name, ram->index, ram.get());
    rams_.push_back(std::move(ram));
}

std::vector<SelectCtrlManager::OpenSelect> SelectCtrlManager::open_selects() const {
    std::vector<OpenSelect> open;
    for (const auto& ram : rams_)
        if (ram->running)
            open.push_back({ram->info_name, ram->index, ram->cursor, ram->items, ram->vertical, ram->cancel, ram->state == 1 && ram->sub == 0});
    return open;
}

bool SelectCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    (void)engine;
    auto& ram = static_cast<SelectRam&>(base);
    if (command == "GENERATE") {
        ram.running = true;
        ram.trigger = engine.trigger_slot(); // online pad redirect (7.4)
        ram.state = ram.sub = ram.counter = ram.busy = 0;
        ram.cursor = 0;
        if (!packet.ints.empty()) {
            ram.items = packet.ints[0];
            if (packet.ints.size() == 2) ram.cursor = packet.ints[1];
        } else if (ram.ram) {
            for (const auto& data : ram.ram.list(10))
                if (data && data.str(2) == "ONE") ram.items = std::max(1, data.i32(5, 1));
        }
        ram.position = packet.vectors.empty() ? Vec3f{} : packet.vectors[0];
        ram.cancel = !(packet.has_string(0) && packet.string(0) == "NOCANCEL");
        ram.enabled.clear();
        if (!packet.bools.empty() && int(packet.bools.size()) == ram.items) ram.enabled = packet.bools;
        return true;
    }
    if (command == "ISEND") { packet.bools.push_back(ram.running ? 0 : 1); return true; }
    if (command == "GETSELECT") { packet.ints.push_back(ram.cursor); return true; }
    if (command == "ISSELECTNOW") {
        packet.bools.push_back(ram.running && ram.state != 2 && ram.state != 3 ? 1 : 0);
        return true;
    }
    if (command == "REMOVE") {
        ram.cursor = -1;
        ram.state = 2;
        ram.sub = ram.counter = ram.busy = 0;
        return true;
    }
    if (command == "FO2TOP") {
        // A new page: an idle cursor returns to the top with the new count
        // and enables (state 10 plays SQ_FO2TOP).
        if (ram.state != 1 || ram.sub) {
            packet.bools.push_back(0);
            return true;
        }
        ram.cursor = 0;
        if (!packet.ints.empty()) {
            ram.items = packet.ints[0];
            if (int(packet.bools.size()) == ram.items) ram.enabled = packet.bools;
        }
        ram.state = 10;
        ram.sub = ram.counter = ram.busy = 0;
        packet.bools.push_back(1);
        return true;
    }
    if (command == "ISFO2TOP") { packet.bools.push_back(ram.state != 1 || ram.sub ? 1 : 0); return true; }
    return false;
}

void SelectCtrlManager::update(Engine& engine) {
    auto seq = [&](SelectRam& ram, const std::string& name) {
        Packet p;
        ram.sequence = name;
        engine.messenger().send(engine, "MODELCTRL", ram.info_name, ram.index, name, p);
    };
    auto is_seq = [&](SelectRam& ram, const std::string& name) {
        Packet p;
        p.strings.emplace_back(name);
        engine.messenger().send(engine, "MODELCTRL", ram.info_name, ram.index, "ISSEQNAME", p);
        return !p.bools.empty() && p.bools[0];
    };
    char buffer[32];
    static const bool trace = std::getenv("DW4_TRACE_SELECT") != nullptr;
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.running) continue;
        if (trace && (ram.state != ram.traced_state || ram.sub != ram.traced_sub)) {
            engine.log("select " + ram.info_name + " state " + std::to_string(ram.state) + "/" + std::to_string(ram.sub) +
                       " frame " + std::to_string(engine.frame()));
            ram.traced_state = ram.state;
            ram.traced_sub = ram.sub;
        }
        // (Online: the trigger's or the leader's pad for index 0 and absent
        // slots, Engine::pad_reader_slot.)
        const auto& pad = engine.pads()[std::size_t(engine.pad_reader_slot(ram.index, ram.trigger))];
        // 803132C0: held stick / d-pad directions move the cursor once the
        // previous line sequence has ended.
        const std::uint32_t sticks = pad.left_directions.held;
        const std::uint32_t dpad = pad.buttons.held & 0xF0u;
        const std::uint32_t trigger = pad.buttons.trigger;
        if (ram.state == 0) {
            if (ram.sub == 0) {
                Packet p;
                p.vectors.push_back(ram.position);
                p.strings.emplace_back("SQ_OPEN");
                engine.messenger().send(engine, "MODELCTRL", ram.info_name, ram.index, "GENERATE", p);
                ram.sequence = "SQ_OPEN";
                ram.sub = 1;
            } else if (ram.sub == 1 && !is_seq(ram, "SQ_OPEN")) {
                std::snprintf(buffer, sizeof buffer, "SQ_LINESL%02d", ram.cursor);
                seq(ram, buffer);
                ram.state = 1;
                ram.sub = ram.counter = ram.busy = 0;
            }
        } else if (ram.state == 1) {
            if (ram.sub == 0) {
                if (sticks || dpad) {
                    if (ram.busy) {
                        if (!is_seq(ram, ram.sequence)) ram.busy = 0;
                    } else {
                        const bool up = (sticks & 1) || (dpad & 0x10);
                        const bool right = (sticks & 2) || (dpad & 0x20);
                        const bool down = (sticks & 4) || (dpad & 0x40);
                        const bool left = (sticks & 8) || (dpad & 0x80);
                        int next = ram.cursor;
                        bool moved = false;
                        if ((ram.vertical && up) || (!ram.vertical && left)) {
                            if (ram.cursor) {
                                next = ram.cursor - 1;
                                std::snprintf(buffer, sizeof buffer, "SQ_LINEUP%02d", next);
                                moved = true;
                            }
                        } else if ((ram.vertical && down) || (!ram.vertical && right)) {
                            if (ram.cursor != ram.items - 1) {
                                next = ram.cursor + 1;
                                std::snprintf(buffer, sizeof buffer, "SQ_LINEDW%02d", next);
                                moved = true;
                            }
                        }
                        if (moved) {
                            ram.cursor = next;
                            seq(ram, buffer);
                            ++ram.busy;
                        }
                    }
                }
                if (trigger & dw4::pad_button::b) {
                    if (ram.cancel) {
                        ram.cursor = -1;
                        ram.state = 2;
                        ram.sub = ram.counter = ram.busy = 0;
                        continue;
                    }
                    Packet p;
                    p.strings.emplace_back("CURSOR_NG");
                    engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", p);
                    ram.sub = 2;
                    ram.counter = ram.busy = 0;
                    continue;
                }
                if (trigger & dw4::pad_button::a) {
                    if (!ram.enabled.empty() && ram.cursor >= 0 && ram.cursor < int(ram.enabled.size()) && !ram.enabled[std::size_t(ram.cursor)]) {
                        Packet p;
                        p.strings.emplace_back("CURSOR_NG");
                        engine.messenger().send(engine, "SOUND", std::nullopt, -1, "PLAY", p);
                        ram.sub = 2;
                        ram.counter = ram.busy = 0;
                        continue;
                    }
                    std::snprintf(buffer, sizeof buffer, "SQ_LINEON%02d", ram.cursor);
                    seq(ram, buffer);
                    ram.sub = 1;
                    ram.counter = ram.busy = 0;
                }
            } else if (ram.sub == 1) {
                if (!is_seq(ram, ram.sequence)) {
                    ram.state = 2;
                    ram.sub = ram.counter = ram.busy = 0;
                }
            } else if (ram.sub == 2) {
                if (ram.counter >= 16) ram.sub = ram.counter = ram.busy = 0;
                else ++ram.counter;
            }
        } else if (ram.state == 10) {
            if (ram.sub == 0) {
                seq(ram, "SQ_FO2TOP");
                ram.sub = 1;
            } else if (!is_seq(ram, "SQ_FO2TOP")) {
                ram.state = 1;
                ram.sub = ram.counter = ram.busy = 0;
            }
        } else if (ram.state == 2) {
            if (ram.sub == 0) {
                seq(ram, ram.cursor == -1 ? "SQ_CANCEL" : "SQ_CLOSE");
                ram.sub = 1;
            } else if (ram.sub == 1 && !is_seq(ram, ram.sequence)) {
                ram.state = 3;
                ram.sub = ram.counter = ram.busy = 0;
            }
        } else {
            ram.running = false;
        }
    }
}

} // namespace dw4::engine
