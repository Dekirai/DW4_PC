#include "sim_ui.hpp"

#include "overflow_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>

namespace dw4::game {

using engine::DrawItem;
using engine::GeometryData;
using engine::Mat4;
using engine::RenderState;
using engine::Vec3f;

namespace {
std::uint32_t u32le(std::span<const std::uint8_t> data, std::size_t at) {
    return std::uint32_t(data[at]) | (std::uint32_t(data[at + 1]) << 8) | (std::uint32_t(data[at + 2]) << 16) | (std::uint32_t(data[at + 3]) << 24);
}
float f32le(std::span<const std::uint8_t> data, std::size_t at) {
    const std::uint32_t bits = u32le(data, at);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
std::uint32_t pack(UiColor c) {
    auto byte = [](float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    // Packed RGBA8 bytes in memory order (GeometryData::colors).
    return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a) << 24);
}
// The layer's centred, y-up space from the screens' top-left, y-down one.
Vec3f layer(float x, float y) { return {x - 320.0f, 240.0f - y, 0.0f}; }

void add_quad(GeometryData& g, float x, float y, float w, float h, UiColor top, UiColor bottom) {
    const auto base = std::uint32_t(g.positions.size());
    g.positions.push_back(layer(x, y));
    g.positions.push_back(layer(x + w, y));
    g.positions.push_back(layer(x + w, y + h));
    g.positions.push_back(layer(x, y + h));
    g.colors.insert(g.colors.end(), {pack(top), pack(top), pack(bottom), pack(bottom)});
    g.indices.insert(g.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void finish_geometry(GeometryData& g) {
    g.valid = !g.positions.empty();
    if (!g.valid) return;
    Vec3f lo = g.positions.front(), hi = lo;
    for (const auto& p : g.positions) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    g.bounds_min = lo;
    g.bounds_max = hi;
}
} // namespace

void SimUi::set_resources(const MountedResources& resources) {
    if (!resources_) resources_ = std::make_unique<MountedResources>(resources);
}

bool SimUi::load_font() {
    if (tried_) return font_ != nullptr;
    tried_ = true;
    if (!resources_) {
        tried_ = false; // (not yet: no resources)
        return false;
    }
    try {
        font_document_ = std::make_shared<const IgbDocument>(read_igb_document(resources_->read("bectext.igb")));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "overflow: font bectext.igb: %s\n", error.what());
        return false;
    }
    // TextCtlManager::load_fonts' reading of the NORMAL font image (ui.cpp).
    const auto* doc = font_document_.get();
    for (const auto& object : doc->objects) {
        if (doc->metadata.object_types[object.type_index].name != "beFontImage") continue;
        engine::ObjectView image(doc, object.reference_index);
        if (image.str(2) != "NORMAL") continue;
        auto font = std::make_unique<engine::FontImage>();
        font->name = std::string(image.str(2));
        font->line_height = image.f32(3);
        if (const auto bind = image.ref(8)) {
            font->binding = std::make_unique<engine::Attribute>();
            font->binding->kind = engine::AttrKind::texture_bind;
            font->binding->object = bind;
            font->binding->texture = bind.ref(4);
            if (font->binding->texture) {
                font->binding->image = font->binding->texture.ref(12);
                font->binding->sampler = engine::texture_sampler(font->binding->texture);
            }
        }
        if (const auto metrics = image.ref(7)) {
            const auto count = metrics.u32(2);
            const auto data = metrics.memory(4);
            for (std::uint32_t i = 0; i < count && (i + 1) * 40 <= data.size(); ++i) {
                const std::size_t o = std::size_t(i) * 40;
                engine::Glyph g;
                const auto code = u32le(data, o) & 0xFFFFu;
                g.advance = f32le(data, o + 4);
                g.x_offset = f32le(data, o + 8);
                g.y_offset = f32le(data, o + 12);
                g.width = f32le(data, o + 16);
                g.height = f32le(data, o + 20);
                g.u0 = f32le(data, o + 24);
                g.v0 = f32le(data, o + 28);
                g.u1 = f32le(data, o + 32);
                g.v1 = f32le(data, o + 36);
                font->glyphs[code] = g;
                codes_.insert(code);
            }
        }
        font_ = std::move(font);
        break;
    }
    if (!font_) std::fprintf(stderr, "overflow: bectext.igb has no NORMAL font\n");
    return font_ != nullptr;
}

bool SimUi::ready() { return load_font(); }

const std::set<std::uint32_t>& SimUi::font_codes() {
    load_font();
    return codes_;
}

void SimUi::begin() { pieces_.clear(); }

void SimUi::rect(float x, float y, float w, float h, UiColor color) { gradient(x, y, w, h, color, color); }

void SimUi::gradient(float x, float y, float w, float h, UiColor top, UiColor bottom) {
    auto g = std::make_shared<GeometryData>();
    add_quad(*g, x, y, w, h, top, bottom);
    finish_geometry(*g);
    pieces_.push_back({std::move(g), ui_colors::white, false});
}

void SimUi::window(float x, float y, float w, float h, float open) {
    // The window grows from its middle line while it opens (the game's
    // SQ_OPEN look: the frame unfolds vertically).
    const float shown = std::clamp(open, 0.0f, 1.0f);
    if (shown <= 0.0f) return;
    const float mid = y + h * 0.5f, half = h * 0.5f * shown;
    y = mid - half;
    h = half * 2.0f;
    auto g = std::make_shared<GeometryData>();
    constexpr float kFrame = 2.0f;
    const UiColor shade{0, 0, 0, 0.35f};
    add_quad(*g, x + 4, y + 4, w, h, shade, shade);
    add_quad(*g, x, y, w, h, ui_colors::window_frame, ui_colors::window_frame);
    add_quad(*g, x + kFrame, y + kFrame, w - 2 * kFrame, h - 2 * kFrame, ui_colors::window_top, ui_colors::window_bottom);
    // The inner highlight line under the top edge.
    add_quad(*g, x + kFrame + 3, y + kFrame + 2, w - 2 * kFrame - 6, 1.0f, UiColor{0.6f, 0.75f, 1, 0.5f}, UiColor{0.6f, 0.75f, 1, 0.5f});
    finish_geometry(*g);
    pieces_.push_back({std::move(g), ui_colors::white, false});
}

void SimUi::name_tab(float x, float y, std::string_view name) {
    const float w = text_width(name, 18.0f) + 28.0f;
    auto g = std::make_shared<GeometryData>();
    add_quad(*g, x, y, w, 26, ui_colors::window_frame, ui_colors::window_frame);
    add_quad(*g, x + 2, y + 2, w - 4, 22, UiColor{0.1f, 0.2f, 0.5f, 0.95f}, UiColor{0.04f, 0.08f, 0.28f, 0.95f});
    finish_geometry(*g);
    pieces_.push_back({std::move(g), ui_colors::white, false});
    text(x + 14, y + 4, name, ui_colors::yellow, 18.0f);
}

void SimUi::polygon(const std::vector<std::pair<float, float>>& points, UiColor color) {
    if (points.size() < 3) return;
    auto g = std::make_shared<GeometryData>();
    for (const auto& [px, py] : points) {
        g->positions.push_back(layer(px, py));
        g->colors.push_back(pack(color));
    }
    for (std::uint32_t i = 1; i + 1 < std::uint32_t(points.size()); ++i) g->indices.insert(g->indices.end(), {0, i, i + 1});
    finish_geometry(*g);
    pieces_.push_back({std::move(g), ui_colors::white, false});
}

void SimUi::talk_frame(float y, float h, std::string_view name, float open) {
    const float shown = std::clamp(open, 0.0f, 1.0f);
    if (shown <= 0.0f) return;
    const float mid = y + h * 0.5f, half = h * 0.5f * shown;
    y = mid - half;
    h = half * 2.0f;
    // The body across the layer, darker at the bottom, then the edge lines.
    gradient(0, y, 640, h, UiColor{0.05f, 0.11f, 0.32f, 0.86f}, UiColor{0.02f, 0.05f, 0.18f, 0.9f});
    rect(0, y, 640, 2, ui_colors::window_line);
    rect(0, y + 4, 640, 1, ui_colors::window_line2);
    rect(0, y + h - 5, 640, 1, ui_colors::window_line2);
    rect(0, y + h - 2, 640, 2, ui_colors::window_line);
    if (shown < 1.0f || name.empty()) return;
    // The name tab: a slanted plate on the top line, cyan edged.
    const float w = text_width(name, 18.0f) + 36.0f, top = y - 26.0f, x = 30.0f;
    polygon({{x, top}, {x + w, top}, {x + w + 14, y}, {x - 6, y}}, ui_colors::window_line);
    polygon({{x + 2, top + 2}, {x + w - 1, top + 2}, {x + w + 10, y}, {x - 2, y}}, UiColor{0.04f, 0.1f, 0.3f, 0.95f});
    text(x + 16, top + 4, name, ui_colors::yellow, 18.0f);
}

void SimUi::cursor_bar(float x, float y, float w, float h) {
    gradient(x, y, w, h, UiColor{0.35f, 0.6f, 1, 0.55f}, UiColor{0.15f, 0.35f, 0.9f, 0.45f});
}

void SimUi::bar(float x, float y, float w, float h, float fraction, UiColor fill) {
    rect(x, y, w, h, UiColor{0, 0, 0, 0.6f});
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0) rect(x + 1, y + 1, (w - 2) * f, h - 2, fill);
}

float SimUi::text_width(std::string_view utf8, float size) {
    if (!load_font() || font_->line_height <= 0) return 0;
    const float scale = size / font_->line_height;
    float widest = 0, pen = 0;
    for (const char16_t c : overflow::to_game_text(utf8)) {
        if (c == u'\n') {
            widest = std::max(widest, pen);
            pen = 0;
            continue;
        }
        const auto found = font_->glyphs.find(std::uint32_t(c));
        pen += found == font_->glyphs.end() ? 0.06f : found->second.advance;
    }
    return std::max(widest, pen) * scale;
}

std::string SimUi::wrap(std::string_view utf8, float size, float width) {
    std::string out, line;
    std::size_t begin = 0;
    while (begin <= utf8.size()) {
        const std::size_t end = std::min(utf8.find_first_of(" |", begin), utf8.size());
        const std::string word(utf8.substr(begin, end - begin));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && text_width(candidate, size) > width) {
            out += line + "|";
            line = word;
        } else {
            line = candidate;
        }
        if (end < utf8.size() && utf8[end] == '|') {
            out += line + "|";
            line.clear();
        }
        if (end >= utf8.size()) break;
        begin = end + 1;
    }
    return out + line;
}

void SimUi::text(float x, float y, std::string_view utf8, UiColor color, float size, int align, float width) {
    if (!load_font() || font_->line_height <= 0 || utf8.empty()) return;
    const float scale = size / font_->line_height;
    // One item per line, so each line can be aligned inside `width`.
    const auto all = overflow::to_game_text(utf8);
    std::size_t begin = 0;
    float line_y = y;
    for (;;) {
        const auto end = std::min(all.find(u'\n', begin), all.size());
        const std::u16string_view line(all.data() + begin, end - begin);
        if (!line.empty()) {
            auto g = std::make_shared<GeometryData>(engine::layout_text(*font_, line, 0, 0));
            if (!g->positions.empty()) {
                float pen = 0;
                for (const char16_t c : line) {
                    const auto found = font_->glyphs.find(std::uint32_t(c));
                    pen += found == font_->glyphs.end() ? 0.06f : found->second.advance;
                }
                float left = x;
                if (align == 1) left = x + width - pen * scale;
                else if (align == 2) left = x + (width - pen * scale) * 0.5f;
                // The glyphs stand on the pen: the baseline about 0.78 of the
                // line height below its top.
                const Vec3f at = layer(left, line_y + size * 0.78f);
                for (auto& p : g->positions) p = {at.x + p.x * scale, at.y + p.y * scale, 0.0f};
                finish_geometry(*g);
                pieces_.push_back({std::move(g), color, true});
            }
        }
        if (end >= all.size()) break;
        begin = end + 1;
        line_y += size * 1.12f;
    }
}

void SimUi::image(float x, float y, float w, float h, const engine::Attribute* texture) {
    if (!texture) return;
    auto g = std::make_shared<GeometryData>();
    g->positions = {layer(x, y), layer(x + w, y), layer(x + w, y + h), layer(x, y + h)};
    g->texcoords = {0, 1, 1, 1, 1, 0, 0, 0};
    g->indices = {0, 1, 2, 0, 2, 3};
    finish_geometry(*g);
    pieces_.push_back({std::move(g), ui_colors::white, true, texture});
}

void SimUi::prompt(float x, float y, std::uint64_t frame) {
    if ((frame / 20) % 2) return;
    auto g = std::make_shared<GeometryData>();
    const auto base = std::uint32_t(g->positions.size());
    g->positions.push_back(layer(x - 6, y));
    g->positions.push_back(layer(x + 6, y));
    g->positions.push_back(layer(x, y + 8));
    const auto c = pack(ui_colors::yellow);
    g->colors.insert(g->colors.end(), {c, c, c});
    g->indices.insert(g->indices.end(), {base, base + 1, base + 2});
    finish_geometry(*g);
    pieces_.push_back({std::move(g), ui_colors::white, false});
}

RenderState SimUi::flat_state() const {
    RenderState s;
    Mat4 projection;
    projection(0, 0) = 1.0f / 320.0f;
    projection(1, 1) = 1.0f / 240.0f;
    projection(2, 2) = -0.001f;
    projection(2, 3) = 0.0f;
    projection(3, 3) = 1.0f;
    s.projection = projection;
    s.view = Mat4{};
    s.lighting = false;
    s.depth_test = false;
    s.depth_write = false;
    s.cull_enabled = false;
    s.blend_enabled = true;
    s.blend_src = 4;
    s.blend_dst = 5;
    s.alpha_test = true;
    s.alpha_ref = 0.02f;
    return s;
}

void SimUi::finish(std::vector<DrawItem>& items, float alpha) {
    drawn_.clear();
    last_items_ = 0;
    if (pieces_.empty() || alpha <= 0.0f) return;
    const RenderState base = flat_state();
    std::uint64_t index = 0;
    for (auto& piece : pieces_) {
        DrawItem item;
        item.geometry = piece.geometry.get();
        item.state = base;
        if (piece.textured && piece.texture) {
            item.state.texture = piece.texture;
            item.state.texture_enabled = true;
        } else if (piece.textured && font_ && font_->binding) {
            item.state.texture = font_->binding.get();
            item.state.texture_enabled = true;
            item.state.color = {piece.color.r, piece.color.g, piece.color.b, piece.color.a};
        }
        if (alpha < 1.0f) item.state.modulate = engine::Vec4f{1, 1, 1, alpha};
        item.instance = 0x51A1u ^ (++index * 0x9E3779B97F4A7C15ull);
        items.push_back(std::move(item));
        drawn_.push_back(std::move(piece.geometry));
        ++last_items_;
    }
    pieces_.clear();
}

} // namespace dw4::game
