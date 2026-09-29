#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cstdio>

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

int font_line_index(std::string_view name) {
    // "font00" -> 0
    int value = 0;
    for (char c : name) if (c >= '0' && c <= '9') value = value * 10 + (c - '0');
    return value;
}

Node* find_geometry(Node* node, int depth = 0) {
    if (!node || depth > 8) return nullptr;
    if (node->kind == NodeKind::geometry) return node;
    for (auto* child : node->children) if (auto* found = find_geometry(child, depth + 1)) return found;
    return nullptr;
}

Node* find_parent_switch(Node* root, const std::string& child_name, int& index, int depth = 0) {
    if (!root || depth > 64) return nullptr;
    for (std::size_t i = 0; i < root->children.size(); ++i) {
        Node* child = root->children[i];
        if (root->kind == NodeKind::switch_node && child->name == child_name) {
            index = int(i);
            return root;
        }
        if (auto* found = find_parent_switch(child, child_name, index, depth + 1)) return found;
    }
    return nullptr;
}
} // namespace

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
                font->binding->sampler.mag_filter = font->binding->texture.i32(5, 1);
                font->binding->sampler.min_filter = font->binding->texture.i32(6, 1);
                font->binding->sampler.wrap_s = 0;
                font->binding->sampler.wrap_t = 0;
            }
        }
        if (const auto metrics = image.ref(7)) {
            const auto count = metrics.u32(2);
            const auto data = metrics.memory(4);
            for (std::uint32_t i = 0; i < count && (i + 1) * 40 <= data.size(); ++i) {
                const std::size_t o = std::size_t(i) * 40;
                Glyph g;
                const auto code = read_u32le(data, o);
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
    for (const auto& data : info.list(5)) {
        if (!data) continue;
        Line line;
        line.layer = std::string(data.str(2));
        line.node = engine.scenes().node(data.ref(4));
        line.line = data.i32(6);
        line.max_width = data.f32(7);
        line.spacing = data.f32(8);
        line.style = std::string(data.str(10));
        // Text lines only ever show generated glyphs; hide the authored placeholder.
        if (Node* geometry = find_geometry(line.node); geometry && !geometry->runtime_geometry)
            geometry->runtime_geometry = std::make_shared<GeometryData>();
        lines_.push_back(std::move(line));
    }
    strings_ = info.list(7);
}

void TextCtlManager::clear_stage(Engine&) {
    rams_.clear();
    lines_.clear();
    strings_.clear();
    geometry_.clear();
    colors_.clear();
    align_.clear();
}

std::u16string TextCtlManager::text(std::size_t index) const {
    std::u16string out;
    if (index >= strings_.size() || !strings_[index]) return out;
    const auto bytes = strings_[index].memory(3);
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const char16_t c = char16_t(bytes[i] | (bytes[i + 1] << 8));
        if (!c) break;
        out.push_back(c);
    }
    return out;
}

Node* TextCtlManager::layer_node(Engine& engine, const std::string& layer) {
    int index = -1;
    return find_parent_switch(engine.global_root(), layer, index);
}

void TextCtlManager::clear_layer(const std::string& layer) {
    for (auto& line : lines_) {
        if (line.layer != layer) continue;
        if (Node* geometry = find_geometry(line.node)) geometry->runtime_geometry = std::make_shared<GeometryData>();
    }
}

void TextCtlManager::set_text(Engine& engine, const std::string& layer, int first_line, const std::u16string& value) {
    const Line* target = nullptr;
    for (const auto& line : lines_)
        if (line.layer == layer && line.line == first_line) { target = &line; break; }
    if (!target) {
        engine.note("TEXTCTL line " + layer + "/" + std::to_string(first_line));
        return;
    }
    Node* geometry_node = find_geometry(target->node);
    if (!geometry_node) return;
    auto font_it = fonts_.find(target->style.empty() ? "NORMAL" : target->style);
    if (font_it == fonts_.end()) font_it = fonts_.find("NORMAL");
    if (font_it == fonts_.end()) return;
    const FontImage& font = *font_it->second;
    const float line_height = font.line_height > 0 ? font.line_height : 0.28f;
    auto geometry = std::make_shared<GeometryData>();
    float x = 0, y = 0;
    std::vector<std::pair<std::size_t, float>> line_ranges; // first vertex, width
    std::size_t line_start = 0;
    auto finish_line = [&]() { line_ranges.emplace_back(line_start, x); line_start = geometry->positions.size(); };
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char16_t c = value[i];
        if (c == u'\r') continue;
        if (c == u'\n') {
            finish_line();
            x = 0;
            y -= line_height;
            continue;
        }
        auto found = font.glyphs.find(std::uint32_t(c));
        if (found == font.glyphs.end()) {
            x += 0.06f;
            continue;
        }
        const Glyph& g = found->second;
        if (g.width > 0 && g.height > 0) {
            const float x0 = x + g.x_offset, x1 = x0 + g.width;
            const float y1 = y - g.y_offset, y0 = y1 - g.height;
            const auto base = std::uint32_t(geometry->positions.size());
            geometry->positions.push_back({x0, y1, 0});
            geometry->positions.push_back({x1, y1, 0});
            geometry->positions.push_back({x1, y0, 0});
            geometry->positions.push_back({x0, y0, 0});
            geometry->texcoords.insert(geometry->texcoords.end(), {g.u0, g.v1, g.u1, g.v1, g.u1, g.v0, g.u0, g.v0});
            geometry->indices.insert(geometry->indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        }
        x += g.advance;
    }
    finish_line();
    // Alignment within max width.
    const auto align = align_.count(layer) ? align_[layer] : std::string("LEFT");
    if (align != "LEFT" && target->max_width > 0) {
        for (std::size_t l = 0; l < line_ranges.size(); ++l) {
            const auto begin = line_ranges[l].first;
            const auto end = l + 1 < line_ranges.size() ? line_ranges[l + 1].first : geometry->positions.size();
            const float shift = align == "CENTER" ? (target->max_width - line_ranges[l].second) * 0.5f
                                                  : target->max_width - line_ranges[l].second;
            for (auto v = begin; v < end; ++v) geometry->positions[v].x += shift;
        }
    }
    geometry->valid = !geometry->indices.empty();
    geometry_node->runtime_geometry = geometry;
    geometry_node->runtime_texture = font.binding.get();
    geometry_.push_back(std::move(geometry));
    if (auto c = colors_.find(layer); c != colors_.end()) target->node->color_override = c->second;
}

bool TextCtlManager::command(Engine& engine, Ram&, std::string_view command, Packet& packet) {
    if (command == "INITIALIZE") {
        for (auto& line : lines_) if (Node* g = find_geometry(line.node)) g->runtime_geometry = std::make_shared<GeometryData>();
        return true;
    }
    const std::string layer(packet.string(0));
    const int line = font_line_index(packet.string(1));
    if (command == "TEXTSET") {
        if (!packet.ints.empty()) set_text(engine, layer, line, text(std::size_t(packet.ints[0])));
        else if (packet.strings.size() > 2) {
            std::u16string literal;
            for (char c : packet.string(2)) literal.push_back(char16_t(static_cast<unsigned char>(c)));
            set_text(engine, layer, line, literal);
        }
        return true;
    }
    if (command == "SWITCH") {
        const auto mode = packet.string(2);
        int index = -1;
        Node* category = find_parent_switch(engine.global_root(), layer, index);
        if (mode == "SET") {
            if (category && index >= 0) category->set_child_enabled(std::size_t(index), true);
            // Style switches below the layer (e.g. NORMAL) show all their lines.
            if (category && index >= 0)
                for (auto* child : category->children[std::size_t(index)]->children)
                    if (child->kind == NodeKind::switch_node) child->set_all_children(true);
        } else if (mode == "CLR") {
            clear_layer(layer);
            if (category && index >= 0) category->set_child_enabled(std::size_t(index), false);
        }
        return true;
    }
    if (command == "COLOR") {
        if (!packet.vectors.empty()) {
            const auto v = packet.vectors[0];
            colors_[layer] = Vec4f{v.x, v.y, v.z, 1.0f};
            for (auto& l : lines_) if (l.layer == layer && l.node) l.node->color_override = colors_[layer];
        }
        return true;
    }
    if (command == "ALIGN") {
        align_[layer] = std::string(packet.string(2));
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

bool SelectCtrlManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    (void)engine;
    auto& ram = static_cast<SelectRam&>(base);
    if (command == "GENERATE") {
        ram.running = true;
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
    if (command == "FO2TOP") { packet.bools.push_back(0); return true; }
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
    for (auto& owned : rams_) {
        auto& ram = *owned;
        if (!ram.running) continue;
        const auto& pad = engine.pads()[std::size_t(std::clamp(ram.index, 0, 3))];
        const std::uint32_t sticks = pad.left_directions.repeat;
        const std::uint32_t dpad = pad.buttons.repeat & 0xF0u;
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
