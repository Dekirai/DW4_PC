// Font additions for translations (docs/L10N.md 2): composed glyph cells
// and text drawn into pictures with the game's font. Presentation only.
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::engine {

// The cells lie in the atlas's free area (font00: x 256-511 and rows
// 256-511 are empty), 16 texels apart, row 260 (in 512ths). „ is ” moved
// 18 rows down (the comma and ’ share that shape 17 rows apart); “ and ‘
// are ” and ’ turned 180 degrees and moved back up to the top of the cell.
const std::vector<ComposedGlyph>& composed_glyphs() {
    static const std::vector<ComposedGlyph> glyphs = {
        {u'„', u'”', ComposedGlyph::Op::shift_down, 18, 258.0f / 512.0f, 260.0f / 512.0f},
        {u'“', u'”', ComposedGlyph::Op::rotate_up, 18, 274.0f / 512.0f, 260.0f / 512.0f},
        {u'‘', u'’', ComposedGlyph::Op::rotate_up, 17, 290.0f / 512.0f, 260.0f / 512.0f},
        {u'‚', u',', ComposedGlyph::Op::alias, 0, 0, 0},
        {u'–', u'-', ComposedGlyph::Op::alias, 0, 0, 0},
        {u'—', u'ー', ComposedGlyph::Op::alias, 0, 0, 0},
    };
    return glyphs;
}

void add_composed_glyphs(FontImage& font) {
    for (const auto& composed : composed_glyphs()) {
        if (font.glyphs.contains(std::uint32_t(composed.code))) continue; // a font that has it keeps its own
        const auto source = font.glyphs.find(std::uint32_t(composed.source));
        if (source == font.glyphs.end()) continue;
        Glyph glyph = source->second;
        if (composed.op != ComposedGlyph::Op::alias) {
            const float du = glyph.u1 - glyph.u0, dv = glyph.v1 - glyph.v0;
            glyph.u0 = composed.u;
            glyph.v0 = composed.v;
            glyph.u1 = composed.u + du;
            glyph.v1 = composed.v + dv;
        }
        font.glyphs[std::uint32_t(composed.code)] = glyph;
    }
}

std::unique_ptr<FontImage> make_display_font(const FontImage& font) {
    auto display = std::make_unique<FontImage>();
    display->name = font.name;
    display->line_height = font.line_height;
    display->glyphs = font.glyphs;
    add_composed_glyphs(*display);
    // Ä Ö Ü ä ö ü ß: their cells (font00 rows 175-255) are 26 texels high,
    // their ink one texel lower than the base letters' on the same pen. One
    // texel is a 27th of a glyph's height (0.01 NORMAL, 0.0066 MINIMUM).
    for (const char16_t code : {u'Ä', u'Ö', u'Ü', u'ä', u'ö', u'ü', u'ß'})
        if (const auto found = display->glyphs.find(std::uint32_t(code)); found != display->glyphs.end())
            found->second.y_offset += found->second.height / 27.0f;
    return display;
}

void paint_composed_glyphs(std::vector<std::uint8_t>& rgba, int width, int height, const FontImage& font) {
    if (width <= 0 || height <= 0 || rgba.size() < std::size_t(width) * std::size_t(height) * 4) return;
    auto pixel = [&](int x, int y) { return rgba.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4; };
    for (const auto& composed : composed_glyphs()) {
        if (composed.op == ComposedGlyph::Op::alias) continue;
        const auto source = font.glyphs.find(std::uint32_t(composed.source));
        if (source == font.glyphs.end()) continue;
        const Glyph& g = source->second;
        const int sx = int(std::lround(g.u0 * float(width))), sy = int(std::lround(g.v0 * float(height)));
        const int w = int(std::lround((g.u1 - g.u0) * float(width))), h = int(std::lround((g.v1 - g.v0) * float(height)));
        const int dx = int(std::lround(composed.u * float(width))), dy = int(std::lround(composed.v * float(height)));
        const int shift = int(std::lround(float(composed.rows) * float(height) / 512.0f));
        if (w <= 0 || h <= 0 || shift >= h || sx < 0 || sy < 0 || sx + w > width || sy + h > height || dx + w > width || dy + h > height)
            continue;
        std::vector<std::uint8_t> cell(std::size_t(w) * std::size_t(h) * 4);
        for (int r = 0; r < h; ++r)
            std::copy_n(pixel(sx, sy + r), std::size_t(w) * 4, cell.data() + std::size_t(r) * std::size_t(w) * 4);
        auto at = [&](int c, int r) { return cell.data() + (std::size_t(r) * std::size_t(w) + std::size_t(c)) * 4; };
        // Rows run with v: row 0 of a cell is the glyph's bottom.
        for (int r = 0; r < h; ++r) {
            int from_row = -1;
            bool turned = false;
            if (composed.op == ComposedGlyph::Op::shift_down) {
                from_row = r + shift; // the ink moves to lower rows
            } else if (r >= shift) {
                from_row = h - 1 - (r - shift); // turned 180 degrees, moved back up
                turned = true;
            }
            for (int c = 0; c < w; ++c) {
                std::uint8_t* out = pixel(dx + c, dy + r);
                if (from_row < 0 || from_row >= h) {
                    // Outside the moved ink: the cell's own empty texel.
                    std::copy_n(at(0, 0), 4, out);
                    continue;
                }
                std::copy_n(at(turned ? w - 1 - c : c, from_row), 4, out);
            }
        }
    }
}

void render_text_rgba(std::vector<std::uint8_t>& rgba, int width, int height, const std::vector<std::uint8_t>& atlas, int atlas_width,
                      int atlas_height, const FontImage& font, std::u16string_view text, int center_x, int row0, int row1) {
    if (width <= 0 || height <= 0 || rgba.size() < std::size_t(width) * std::size_t(height) * 4) return;
    if (atlas_width <= 0 || atlas_height <= 0 || atlas.size() < std::size_t(atlas_width) * std::size_t(atlas_height) * 4) return;
    // Texels per font unit, from the atlas cells (font00 NORMAL: 100).
    float scale = 0;
    for (const auto& [code, g] : font.glyphs)
        if (g.width > 0 && g.u1 > g.u0) {
            scale = (g.u1 - g.u0) * float(atlas_width) / g.width;
            break;
        }
    if (scale <= 0) return;
    struct Placed {
        const Glyph* glyph;
        float x;
    };
    std::vector<Placed> placed;
    float pen = 0, right = 0;
    for (const char16_t c : text) {
        if (c == u'\r' || c == u'\n') continue;
        const auto found = font.glyphs.find(std::uint32_t(c));
        if (found == font.glyphs.end()) {
            pen += 0.06f; // layout_text's step over a missing glyph
            continue;
        }
        const Glyph& g = found->second;
        placed.push_back({&g, pen + g.x_offset});
        right = std::max(right, pen + g.x_offset + g.width);
        pen += g.advance;
    }
    // The ink coverage (alpha) and colour of the glyphs on the picture.
    std::vector<std::uint8_t> ink(std::size_t(width) * std::size_t(height), 0);
    std::vector<std::uint8_t> shade(std::size_t(width) * std::size_t(height), 0);
    const int left = center_x - int(std::lround(right * scale * 0.5f));
    const int middle = (row0 + row1) / 2;
    for (const auto& [g, x] : placed) {
        const int sx = int(std::lround(g->u0 * float(atlas_width))), sy = int(std::lround(g->v0 * float(atlas_height)));
        const int w = int(std::lround((g->u1 - g->u0) * float(atlas_width))), h = int(std::lround((g->v1 - g->v0) * float(atlas_height)));
        const int dx = left + int(std::lround(x * scale));
        const int dy = middle - h / 2 + int(std::lround(g->y_offset * scale));
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c) {
                const int ax = sx + c, ay = sy + r, tx = dx + c, ty = dy + r;
                if (ax < 0 || ay < 0 || ax >= atlas_width || ay >= atlas_height || tx < 0 || ty < 0 || tx >= width || ty >= height) continue;
                const std::uint8_t* a = atlas.data() + (std::size_t(ay) * std::size_t(atlas_width) + std::size_t(ax)) * 4;
                const std::size_t t = std::size_t(ty) * std::size_t(width) + std::size_t(tx);
                if (a[3] > ink[t]) {
                    ink[t] = a[3];
                    shade[t] = std::uint8_t((unsigned(a[0]) + unsigned(a[1]) + unsigned(a[2])) / 3);
                }
            }
    }
    auto coverage = [&](int x, int y) -> unsigned {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return ink[std::size_t(y) * std::size_t(width) + std::size_t(x)];
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            // The original's look: a dark shadow down-right (rows run up, so
            // down is one row lower), a black outline, white glyphs.
            unsigned outline = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) outline = std::max(outline, coverage(x + ox, y + oy));
            const unsigned shadow = std::max(coverage(x - 2, y + 2), coverage(x - 1, y + 1));
            const unsigned glyph = coverage(x, y);
            if (!outline && !shadow) continue;
            float r = 0, gg = 0, b = 0, alpha = 0;
            auto over = [&](float cr, float cg, float cb, float ca) {
                const float out = ca + alpha * (1 - ca);
                if (out <= 0) return;
                r = (cr * ca + r * alpha * (1 - ca)) / out;
                gg = (cg * ca + gg * alpha * (1 - ca)) / out;
                b = (cb * ca + b * alpha * (1 - ca)) / out;
                alpha = out;
            };
            over(68, 68, 68, float(shadow) / 255.0f);
            over(0, 0, 0, float(outline) / 255.0f);
            const float level = float(shade[std::size_t(y) * std::size_t(width) + std::size_t(x)]);
            over(level, level, level, float(glyph) / 255.0f);
            std::uint8_t* out = rgba.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
            const float under = float(out[3]) / 255.0f;
            const float total = alpha + under * (1 - alpha);
            if (total <= 0) continue;
            out[0] = std::uint8_t(std::lround((r * alpha + float(out[0]) * under * (1 - alpha)) / total));
            out[1] = std::uint8_t(std::lround((gg * alpha + float(out[1]) * under * (1 - alpha)) / total));
            out[2] = std::uint8_t(std::lround((b * alpha + float(out[2]) * under * (1 - alpha)) / total));
            out[3] = std::uint8_t(std::lround(total * 255.0f));
        }
}

} // namespace dw4::engine
