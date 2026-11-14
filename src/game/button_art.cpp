#include "button_art.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string_view>

namespace dw4::game {
namespace {

// ---------------------------------------------------------------------------
// Signed distances (pixels; negative inside) and an anti-aliased canvas.

struct Vec2 {
    float x = 0, y = 0;
};

float length(float x, float y) { return std::sqrt(x * x + y * y); }

float sd_circle(float px, float py, float cx, float cy, float r) { return length(px - cx, py - cy) - r; }

// A rectangle of half size hw x hh with corner radius rad.
float sd_box(float px, float py, float cx, float cy, float hw, float hh, float rad) {
    rad = std::min({rad, hw, hh});
    const float qx = std::fabs(px - cx) - (hw - rad), qy = std::fabs(py - cy) - (hh - rad);
    return length(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rad;
}

float sd_segment(float px, float py, Vec2 a, Vec2 b) {
    const float bax = b.x - a.x, bay = b.y - a.y, pax = px - a.x, pay = py - a.y;
    const float den = bax * bax + bay * bay;
    const float h = den > 0 ? std::clamp((pax * bax + pay * bay) / den, 0.0f, 1.0f) : 0.0f;
    return length(pax - bax * h, pay - bay * h);
}

// Any simple polygon (the even-odd rule decides the sign).
float sd_polygon(float px, float py, const std::vector<Vec2>& v) {
    float d = 1e9f;
    bool inside = false;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
        d = std::min(d, sd_segment(px, py, v[j], v[i]));
        if (((v[i].y > py) != (v[j].y > py)) && (px < (v[j].x - v[i].x) * (py - v[i].y) / (v[j].y - v[i].y) + v[i].x)) inside = !inside;
    }
    return inside ? -d : d;
}

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};
constexpr Color hex(unsigned value, float alpha = 1.0f) {
    return {float((value >> 16) & 0xFF) / 255.0f, float((value >> 8) & 0xFF) / 255.0f, float(value & 0xFF) / 255.0f, alpha};
}

class Canvas {
public:
    Canvas(int width, int height) : width_(width), height_(height), px_(std::size_t(width) * std::size_t(height) * 4, 0.0f) {}
    int width() const { return width_; }
    int height() const { return height_; }

    // Fills where sdf(x, y) < 0 inside the box (pixel centres), the edge
    // anti-aliased over one pixel.
    template <class Sdf>
    void fill(float x0, float y0, float x1, float y1, Color color, Sdf sdf) {
        const int ix0 = std::max(0, int(std::floor(x0)) - 1), iy0 = std::max(0, int(std::floor(y0)) - 1);
        const int ix1 = std::min(width_ - 1, int(std::ceil(x1)) + 1), iy1 = std::min(height_ - 1, int(std::ceil(y1)) + 1);
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x) {
                const float d = sdf(float(x) + 0.5f, float(y) + 0.5f);
                const float coverage = std::clamp(0.5f - d, 0.0f, 1.0f);
                if (coverage > 0.0f) over(x, y, color, coverage);
            }
    }
    ArtImage image() const {
        ArtImage out(width_, height_);
        for (std::size_t i = 0; i < std::size_t(width_) * std::size_t(height_); ++i) {
            const float a = px_[i * 4 + 3];
            auto& o = out.rgba;
            if (a <= 0.0f) {
                // Transparent texels carry the outline's colour, so filtering
                // never pulls a light fringe in.
                o[i * 4] = 14, o[i * 4 + 1] = 14, o[i * 4 + 2] = 20, o[i * 4 + 3] = 0;
                continue;
            }
            for (int c = 0; c < 3; ++c) o[i * 4 + c] = std::uint8_t(std::lround(std::clamp(px_[i * 4 + c] / a, 0.0f, 1.0f) * 255.0f));
            o[i * 4 + 3] = std::uint8_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
        }
        return out;
    }

private:
    void over(int x, int y, Color c, float coverage) {
        float* p = &px_[(std::size_t(y) * std::size_t(width_) + std::size_t(x)) * 4];
        const float a = c.a * coverage;
        p[0] = c.r * a + p[0] * (1 - a);
        p[1] = c.g * a + p[1] * (1 - a);
        p[2] = c.b * a + p[2] * (1 - a);
        p[3] = a + p[3] * (1 - a);
    }
    int width_, height_;
    std::vector<float> px_; // premultiplied RGBA
};

// ---------------------------------------------------------------------------
// A stroke font of our own: capitals, digits and a few signs on a grid four
// units wide and six tall (y down), drawn as round-capped strokes.

struct StrokeGlyph {
    char c;
    float width;
    const char* strokes; // "x,y x,y ...|x,y ..." polylines
};
constexpr StrokeGlyph kStrokeFont[] = {
    {'A', 4, "0,6 2,0 4,6|0.7,4 3.3,4"},
    {'B', 4, "0,6 0,0 2.8,0 3.6,0.7 3.6,2.3 2.8,3 0,3|2.8,3 4,3.8 4,5.2 3.1,6 0,6"},
    {'C', 4, "4,0.9 3.2,0 0.8,0 0,0.8 0,5.2 0.8,6 3.2,6 4,5.1"},
    {'D', 4, "0,0 0,6 2.4,6 4,4.4 4,1.6 2.4,0 0,0"},
    {'E', 4, "4,0 0,0 0,6 4,6|0,3 3,3"},
    {'F', 4, "4,0 0,0 0,6|0,3 3,3"},
    {'G', 4, "4,0.9 3.2,0 0.8,0 0,0.8 0,5.2 0.8,6 3.2,6 4,5.2 4,3.2 2.2,3.2"},
    {'H', 4, "0,0 0,6|4,0 4,6|0,3 4,3"},
    {'I', 2, "0,0 2,0|1,0 1,6|0,6 2,6"},
    {'J', 4, "4,0 4,5.1 3.1,6 0.9,6 0,5.1"},
    {'K', 4, "0,0 0,6|4,0 0,3.6|1.4,2.7 4,6"},
    {'L', 4, "0,0 0,6 4,6"},
    {'M', 4.6f, "0,6 0,0 2.3,3.6 4.6,0 4.6,6"},
    {'N', 4, "0,6 0,0 4,6 4,0"},
    {'O', 4, "0.8,0 3.2,0 4,0.8 4,5.2 3.2,6 0.8,6 0,5.2 0,0.8 0.8,0"},
    {'P', 4, "0,6 0,0 3.1,0 4,0.8 4,2.4 3.1,3.2 0,3.2"},
    {'Q', 4, "0.8,0 3.2,0 4,0.8 4,5.2 3.2,6 0.8,6 0,5.2 0,0.8 0.8,0|2.5,4.5 4.2,6.2"},
    {'R', 4, "0,6 0,0 3.1,0 4,0.8 4,2.4 3.1,3.2 0,3.2|2,3.2 4,6"},
    {'S', 4, "4,0.9 3.2,0 0.8,0 0,0.8 0,2.2 0.8,3 3.2,3 4,3.8 4,5.2 3.2,6 0.8,6 0,5.1"},
    {'T', 4, "0,0 4,0|2,0 2,6"},
    {'U', 4, "0,0 0,5.2 0.8,6 3.2,6 4,5.2 4,0"},
    {'V', 4, "0,0 2,6 4,0"},
    {'W', 4.6f, "0,0 1.1,6 2.3,2.4 3.5,6 4.6,0"},
    {'X', 4, "0,0 4,6|4,0 0,6"},
    {'Y', 4, "0,0 2,3 4,0|2,3 2,6"},
    {'Z', 4, "0,0 4,0 0,6 4,6"},
    {'0', 4, "0.8,0 3.2,0 4,0.8 4,5.2 3.2,6 0.8,6 0,5.2 0,0.8 0.8,0|3.6,0.6 0.4,5.4"},
    {'1', 3, "0.4,1.2 1.6,0 1.6,6|0.2,6 3,6"},
    {'2', 4, "0,0.9 0.8,0 3.2,0 4,0.8 4,2.4 0,6 4,6"},
    {'3', 4, "0,0.9 0.8,0 3.2,0 4,0.8 4,2.2 3.2,3 1.4,3|3.2,3 4,3.8 4,5.2 3.2,6 0.8,6 0,5.1"},
    {'4', 4, "3,6 3,0 0,4.2 4,4.2"},
    {'5', 4, "4,0 0,0 0,2.6 3.2,2.6 4,3.4 4,5.2 3.2,6 0.8,6 0,5.1"},
    {'6', 4, "3.6,0 1.4,0 0,1.6 0,5.2 0.8,6 3.2,6 4,5.2 4,3.8 3.2,3 0.8,3 0,3.8"},
    {'7', 4, "0,0 4,0 1.4,6"},
    {'8', 4, "0.8,0 3.2,0 3.8,0.6 3.8,2.4 3.2,3 0.8,3 0.2,2.4 0.2,0.6 0.8,0|0.8,3 0,3.8 0,5.2 0.8,6 3.2,6 4,5.2 4,3.8 3.2,3"},
    {'9', 4, "0.4,6 2.6,6 4,4.4 4,0.8 3.2,0 0.8,0 0,0.8 0,2.2 0.8,3 3.2,3 4,2.2"},
    {'+', 4, "2,1 2,5|0,3 4,3"},
    {'-', 3, "0.2,3 2.8,3"},
    {'.', 1, "0.5,5.8 0.5,5.8"},
    {'/', 3, "3,0 0,6"},
    {'\'', 1, "0.5,0 0.5,1.8"},
    {'=', 4, "0,2 4,2|0,4 4,4"},
    {',', 1, "0.6,5.4 0.2,6.8"},
    {';', 1, "0.5,2.2 0.5,2.2|0.6,5.4 0.2,6.8"},
    {'[', 2, "2,0 0,0 0,6 2,6"},
    {']', 2, "0,0 2,0 2,6 0,6"},
    {'`', 1.4f, "0,0 1.4,1.4"},
    {'\\', 3, "0,0 3,6"},
    {'*', 4, "2,1 2,5|0.3,2 3.7,4|3.7,2 0.3,4"},
};

struct ParsedGlyph {
    float width = 4;
    std::vector<std::vector<Vec2>> strokes;
};

const ParsedGlyph* stroke_glyph(char c) {
    static const auto table = [] {
        std::array<ParsedGlyph, 128> parsed{};
        for (const auto& g : kStrokeFont) {
            ParsedGlyph p;
            p.width = g.width;
            std::vector<Vec2> line;
            const char* s = g.strokes;
            while (*s) {
                if (*s == '|') {
                    p.strokes.push_back(line);
                    line.clear();
                    ++s;
                    continue;
                }
                if (*s == ' ') {
                    ++s;
                    continue;
                }
                char* end = nullptr;
                const float x = std::strtof(s, &end);
                s = end + 1; // ','
                const float y = std::strtof(s, &end);
                s = end;
                line.push_back({x, y});
            }
            if (!line.empty()) p.strokes.push_back(line);
            parsed[std::size_t(static_cast<unsigned char>(g.c))] = std::move(p);
        }
        return parsed;
    }();
    const auto index = std::size_t(static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(c))));
    if (index >= table.size() || table[index].strokes.empty()) return nullptr;
    return &table[index];
}

constexpr float kLetterGap = 1.3f; // grid units between letters
constexpr float kSpaceWidth = 2.6f;

// Width of a text in grid units.
float text_units(std::string_view text) {
    float units = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const ParsedGlyph* g = stroke_glyph(text[i]);
        units += g ? g->width : kSpaceWidth;
        if (i + 1 < text.size()) units += kLetterGap;
    }
    return units;
}

// Centred text of cap height `cap` px (shrunk to fit `max_width`), stroke
// width `weight` grid units.
void draw_text(Canvas& canvas, std::string_view text, float cx, float cy, float cap, float max_width, Color color, float weight = 1.15f) {
    if (text.empty()) return;
    float unit = cap / 6.0f;
    const float units = text_units(text) + weight; // (the caps reach half a stroke out)
    if (units * unit > max_width && max_width > 0) unit = max_width / units;
    const float radius = weight * unit * 0.5f;
    float x = cx - (units - weight) * unit * 0.5f;
    const float top = cy - 3.0f * unit;
    for (const char c : text) {
        const ParsedGlyph* g = stroke_glyph(c);
        if (!g) {
            x += (kSpaceWidth + kLetterGap) * unit;
            continue;
        }
        std::vector<std::pair<Vec2, Vec2>> segments;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& stroke : g->strokes)
            for (std::size_t i = 0; i < stroke.size(); ++i) {
                const Vec2 a{x + stroke[i].x * unit, top + stroke[i].y * unit};
                const Vec2 b = i + 1 < stroke.size() ? Vec2{x + stroke[i + 1].x * unit, top + stroke[i + 1].y * unit} : a;
                if (i + 1 < stroke.size() || stroke.size() == 1) segments.emplace_back(a, b);
                x0 = std::min({x0, a.x, b.x}), y0 = std::min({y0, a.y, b.y});
                x1 = std::max({x1, a.x, b.x}), y1 = std::max({y1, a.y, b.y});
            }
        canvas.fill(x0 - radius, y0 - radius, x1 + radius, y1 + radius, color, [&](float px, float py) {
            float d = 1e9f;
            for (const auto& [a, b] : segments) d = std::min(d, sd_segment(px, py, a, b));
            return d - radius;
        });
        x += (g->width + kLetterGap) * unit;
    }
}

// ---------------------------------------------------------------------------
// Shapes.

struct Box {
    float x0, y0, x1, y1;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
    float s() const { return std::min(w(), h()); }
};

void circle(Canvas& c, float cx, float cy, float r, Color color) {
    c.fill(cx - r, cy - r, cx + r, cy + r, color, [&](float x, float y) { return sd_circle(x, y, cx, cy, r); });
}
void ring(Canvas& c, float cx, float cy, float r, float half, Color color) {
    c.fill(cx - r - half, cy - r - half, cx + r + half, cy + r + half, color,
           [&](float x, float y) { return std::fabs(sd_circle(x, y, cx, cy, r)) - half; });
}
void box(Canvas& c, float cx, float cy, float hw, float hh, float rad, Color color) {
    c.fill(cx - hw, cy - hh, cx + hw, cy + hh, color, [&](float x, float y) { return sd_box(x, y, cx, cy, hw, hh, rad); });
}
void box_ring(Canvas& c, float cx, float cy, float hw, float hh, float rad, float half, Color color) {
    c.fill(cx - hw - half, cy - hh - half, cx + hw + half, cy + hh + half, color,
           [&](float x, float y) { return std::fabs(sd_box(x, y, cx, cy, hw, hh, rad)) - half; });
}
void line(Canvas& c, Vec2 a, Vec2 b, float radius, Color color) {
    c.fill(std::min(a.x, b.x) - radius, std::min(a.y, b.y) - radius, std::max(a.x, b.x) + radius, std::max(a.y, b.y) + radius, color,
           [&](float x, float y) { return sd_segment(x, y, a, b) - radius; });
}
void polygon(Canvas& c, const std::vector<Vec2>& points, Color color, float grow = 0.0f) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto& p : points) x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
    c.fill(x0 - grow, y0 - grow, x1 + grow, y1 + grow, color, [&](float x, float y) { return sd_polygon(x, y, points) - grow; });
}
void polygon_ring(Canvas& c, const std::vector<Vec2>& points, float half, Color color) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto& p : points) x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
    c.fill(x0 - half, y0 - half, x1 + half, y1 + half, color, [&](float x, float y) { return std::fabs(sd_polygon(x, y, points)) - half; });
}
// The D-pad's plus: two rounded arms.
float sd_cross(float x, float y, float cx, float cy, float arm, float half_width, float rad) {
    return std::min(sd_box(x, y, cx, cy, arm, half_width, rad), sd_box(x, y, cx, cy, half_width, arm, rad));
}

// ---------------------------------------------------------------------------
// Family styles.

constexpr Color kOutline = hex(0x0c0c12, 0.92f);

struct Style {
    Color face;        // round face buttons
    Color face_rim;    // a thin ring inside the outline (none: alpha 0)
    Color letter;      // letters on face buttons (when not coloured per button)
    bool letters_coloured; // Xbox One: the letter in the button's colour
    bool fill_coloured;    // Xbox 360: the whole button in its colour
    bool snes_colours;     // the AYN Thor: the letters in the Super Famicom colours (A red, B yellow, X blue, Y green)
    Color shoulder, shoulder_label;
    Color middle, middle_symbol;
    Color stick, stick_cap, stick_label;
    Color dpad, dpad_mark;
};

Style style_of(PadFamily family) {
    Style s{};
    s.face_rim = hex(0, 0);
    // Sticks and the D-pad stand on the game's black text boxes: light on
    // every family (a dark stick or D-pad vanished there), as the
    // original's light grey stick picture is.
    const Color stick_base = hex(0x8e8e98), stick_cap = hex(0xdedee4), stick_label = hex(0x22222a);
    const Color dpad = hex(0xd2d2da), dpad_mark = hex(0x2a2a32);
    switch (family) {
    case PadFamily::xbox_one:
        s.face = hex(0x34343c); s.face_rim = hex(0xc0c0cc, 0.7f); s.letter = hex(0xf2f2f4); s.letters_coloured = true;
        s.shoulder = hex(0x2c2c33); s.shoulder_label = hex(0xf0f0f2);
        s.middle = hex(0x2c2c33); s.middle_symbol = hex(0xf0f0f2);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    case PadFamily::xbox360:
        s.face = hex(0x26262c); s.letter = hex(0xffffff); s.fill_coloured = true; s.face_rim = hex(0xffffff, 0.35f);
        s.shoulder = hex(0xc9c9cf); s.shoulder_label = hex(0x26262c);
        s.middle = hex(0xc9c9cf); s.middle_symbol = hex(0x26262c);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    case PadFamily::ps4:
        s.face = hex(0x2e2e38); s.face_rim = hex(0xb0b0bc, 0.6f); s.letter = hex(0xf0f0f4);
        s.shoulder = hex(0x2a2a32); s.shoulder_label = hex(0xf0f0f4);
        s.middle = hex(0x2a2a32); s.middle_symbol = hex(0xf0f0f4);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    case PadFamily::ps5:
        s.face = hex(0x24242c); s.face_rim = hex(0xe6e6ee, 0.9f); s.letter = hex(0xeeeef4);
        s.shoulder = hex(0xe9e9ef); s.shoulder_label = hex(0x24242c);
        s.middle = hex(0x24242c); s.middle_symbol = hex(0xeeeef4);
        s.stick = stick_base; s.stick_cap = hex(0xeeeef4); s.stick_label = stick_label;
        s.dpad = hex(0xe4e4ea); s.dpad_mark = hex(0x24242c);
        break;
    case PadFamily::switch_pro:
    case PadFamily::switch2_pro:
        s.face = family == PadFamily::switch2_pro ? hex(0x383842) : hex(0x30303a);
        s.face_rim = family == PadFamily::switch2_pro ? hex(0xd0d4e4, 0.9f) : hex(0xb0b0bc, 0.65f);
        s.letter = hex(0xf4f4f6);
        s.shoulder = hex(0x2c2c32); s.shoulder_label = hex(0xf4f4f6);
        s.middle = hex(0x2c2c32); s.middle_symbol = hex(0xf4f4f6);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    case PadFamily::wiiu_pro:
        s.face = hex(0xdcdce2); s.face_rim = hex(0xffffff, 0.6f); s.letter = hex(0x2a2a30);
        s.shoulder = hex(0xdcdce2); s.shoulder_label = hex(0x2a2a30);
        s.middle = hex(0xdcdce2); s.middle_symbol = hex(0x2a2a30);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    case PadFamily::thor:
        // Dark round buttons with coloured letters (A red, B yellow, X blue,
        // Y green), dark shoulders labelled L1 / R1 / L2 / R2, Start and
        // Select as small pills.
        s.face = hex(0x2c2c34); s.face_rim = hex(0xb4b4c0, 0.6f); s.letter = hex(0xf2f2f4); s.snes_colours = true;
        s.shoulder = hex(0x2a2a31); s.shoulder_label = hex(0xf0f0f2);
        s.middle = hex(0x2a2a31); s.middle_symbol = hex(0xf0f0f2);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    case PadFamily::gamecube:
        s.face = hex(0xbcbcc4); s.letter = hex(0x34343c);
        s.shoulder = hex(0xc4c4cc); s.shoulder_label = hex(0x34343c);
        s.middle = hex(0xb4b4bc); s.middle_symbol = hex(0x34343c);
        s.stick = hex(0x8e8e98); s.stick_cap = hex(0xd0d0d6); s.stick_label = hex(0x34343c);
        s.dpad = hex(0xc4c4cc); s.dpad_mark = hex(0x34343c);
        break;
    default: // generic
        s.face = hex(0x4a4a54); s.letter = hex(0xf4f4f6);
        s.shoulder = hex(0x4a4a54); s.shoulder_label = hex(0xf4f4f6);
        s.middle = hex(0x4a4a54); s.middle_symbol = hex(0xf4f4f6);
        s.stick = stick_base; s.stick_cap = stick_cap; s.stick_label = stick_label;
        s.dpad = dpad; s.dpad_mark = dpad_mark;
        break;
    }
    return s;
}

// The Xbox letters' colours (green A, red B, blue X, yellow Y) and the
// classic PlayStation symbol colours.
Color xbox_colour(Face face) {
    switch (face) {
    case Face::letter_a: return hex(0x5bbf3a);
    case Face::letter_b: return hex(0xe33b3b);
    case Face::letter_x: return hex(0x2f8be2);
    case Face::letter_y: return hex(0xf0c010);
    default: return hex(0xf0f0f2);
    }
}
// The Super Famicom's letter colours, as on the AYN Thor's face buttons.
Color snes_colour(Face face) {
    switch (face) {
    case Face::letter_a: return hex(0xe0383c);
    case Face::letter_b: return hex(0xf2c418);
    case Face::letter_x: return hex(0x3a78dc);
    case Face::letter_y: return hex(0x3cae4c);
    default: return hex(0xf0f0f2);
    }
}
Color ps_colour(Face face) {
    switch (face) {
    case Face::cross: return hex(0x7fb0ec);
    case Face::circle: return hex(0xff6a6a);
    case Face::square: return hex(0xec96cc);
    case Face::triangle: return hex(0x44d8a4);
    default: return hex(0xf0f0f4);
    }
}

// A round button with its outline.
void round_button(Canvas& c, float cx, float cy, float r, Color fill, Color rim) {
    circle(c, cx, cy, r + r * 0.08f, kOutline);
    circle(c, cx, cy, r, fill);
    if (rim.a > 0) ring(c, cx, cy, r - r * 0.07f, r * 0.045f, rim);
}

void ps_symbol(Canvas& c, Face face, float cx, float cy, float r, Color color) {
    const float half = r * 0.105f;
    switch (face) {
    case Face::cross: {
        const float k = r * 0.4f;
        line(c, {cx - k, cy - k}, {cx + k, cy + k}, half, color);
        line(c, {cx + k, cy - k}, {cx - k, cy + k}, half, color);
        break;
    }
    case Face::circle: ring(c, cx, cy, r * 0.44f, half, color); break;
    case Face::square: box_ring(c, cx, cy, r * 0.4f, r * 0.4f, r * 0.04f, half, color); break;
    case Face::triangle: {
        const float k = r * 0.5f;
        polygon_ring(c, {{cx, cy - k}, {cx + k * 0.96f, cy + k * 0.62f}, {cx - k * 0.96f, cy + k * 0.62f}}, half, color);
        break;
    }
    default: break;
    }
}

void lines3(Canvas& c, float cx, float cy, float w, float gap, float half, Color color) {
    for (int i = -1; i <= 1; ++i) line(c, {cx - w * 0.5f, cy + gap * float(i)}, {cx + w * 0.5f, cy + gap * float(i)}, half, color);
}

void triangle_fill(Canvas& c, float cx, float cy, float k, bool right, Color color) {
    const float d = right ? 1.0f : -1.0f;
    polygon(c, {{cx - d * k * 0.6f, cy - k}, {cx + d * k * 0.9f, cy}, {cx - d * k * 0.6f, cy + k}}, color);
}

// A pill (horizontal rounded bar) with its outline.
void pill(Canvas& c, float cx, float cy, float hw, float hh, Color fill, Color rim = hex(0, 0)) {
    const float o = hh * 0.22f;
    box(c, cx, cy, hw + o, hh + o, hh + o, kOutline);
    box(c, cx, cy, hw, hh, hh, fill);
    if (rim.a > 0) box_ring(c, cx, cy, hw - hh * 0.12f, hh - hh * 0.12f, hh, hh * 0.08f, rim);
}

// Shoulders (bumper: a wide bar) and triggers (a taller rounded tab).
void shoulder(Canvas& c, const Box& b, const std::string& label, bool trigger, Color fill, Color text) {
    const float s = b.s();
    const float hw = trigger ? s * 0.4f : s * 0.47f, hh = trigger ? s * 0.38f : s * 0.27f;
    const float rad = trigger ? s * 0.22f : s * 0.15f;
    const float o = s * 0.05f;
    box(c, b.cx(), b.cy(), hw + o, hh + o, rad + o, kOutline);
    box(c, b.cx(), b.cy(), hw, hh, rad, fill);
    draw_text(c, label, b.cx(), b.cy(), s * (trigger ? 0.34f : 0.3f), hw * 1.7f, text);
}

// A stick from above: its light base (round, or the GameCube's octagonal
// gate) and a lighter cap as large as a face button, the letter dark on it.
void stick_icon(Canvas& c, const Box& b, const Style& st, const std::string& label, bool octagon, Color cap) {
    const float s = b.s(), cx = b.cx(), cy = b.cy();
    const float r = s * 0.47f;
    if (octagon) {
        std::vector<Vec2> gate;
        for (int i = 0; i < 8; ++i) {
            const float a = (float(i) + 0.5f) * 3.14159265f / 4.0f;
            gate.push_back({cx + std::cos(a) * r * 1.04f, cy + std::sin(a) * r * 1.04f});
        }
        polygon(c, gate, kOutline, s * 0.04f);
        polygon(c, gate, st.stick);
    } else {
        circle(c, cx, cy, r + s * 0.04f, kOutline);
        circle(c, cx, cy, r, st.stick);
    }
    circle(c, cx, cy, r * 0.8f, hex(0x2a2a32, 0.85f));
    circle(c, cx, cy, r * 0.74f, cap);
    ring(c, cx, cy, r * 0.6f, s * 0.016f, hex(0x000000, 0.18f));
    if (!label.empty()) draw_text(c, label, cx, cy, r * 0.82f, r * 1.25f, st.stick_label, 1.35f);
}

void dpad_icon(Canvas& c, const Box& b, const Style& st, Face face) {
    const float s = b.s(), cx = b.cx(), cy = b.cy();
    const float arm = s * 0.47f, hw = s * 0.19f, rad = s * 0.06f, o = s * 0.045f;
    c.fill(cx - arm - o, cy - arm - o, cx + arm + o, cy + arm + o, kOutline,
           [&](float x, float y) { return sd_cross(x, y, cx, cy, arm, hw, rad) - o; });
    c.fill(cx - arm, cy - arm, cx + arm, cy + arm, st.dpad, [&](float x, float y) { return sd_cross(x, y, cx, cy, arm, hw, rad); });
    // One direction: that arm lit; the whole pad: small arrows.
    auto arrow = [&](float dx, float dy, Color color, float size) {
        const float tx = cx + dx * arm * 0.62f, ty = cy + dy * arm * 0.62f, k = size;
        const float px = -dy, py = dx; // perpendicular
        polygon(c, {{tx + dx * k, ty + dy * k}, {tx - dx * k * 0.6f + px * k, ty - dy * k * 0.6f + py * k},
                    {tx - dx * k * 0.6f - px * k, ty - dy * k * 0.6f - py * k}},
                color);
    };
    const std::array<std::pair<float, float>, 4> dirs{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
    const std::array<Face, 4> faces{Face::dpad_up, Face::dpad_down, Face::dpad_left, Face::dpad_right};
    for (std::size_t i = 0; i < 4; ++i) {
        const bool lit = face == faces[i];
        if (face == Face::dpad || lit) arrow(dirs[i].first, dirs[i].second, lit ? hex(0xe0a000) : st.dpad_mark, s * (lit ? 0.12f : 0.08f));
    }
}

// Keys drawn as a sign instead of their name: Enter, Backspace, Space.
bool sign_label(const std::string& label) { return label == "ENTER" || label == "BKSP" || label == "SPACE"; }

// A key cap: a light top over a darker skirt.
void key_cap(Canvas& c, const Box& b, const std::string& label, bool arrow_glyph) {
    const float s = b.s();
    const float hh = s * 0.4f;
    const float text_cap = s * 0.36f;
    float hw = hh;
    if (label == "SPACE") hw = std::max(hh, std::min(b.w() * 0.5f, s * 0.9f)); // (a space bar is wide where it can be)
    else if (!arrow_glyph && !sign_label(label)) hw = std::max(hh, (text_units(label) + 1.15f) * text_cap / 6.0f * 0.5f + s * 0.12f);
    hw = std::min(hw, b.w() * 0.5f - s * 0.06f);
    const float cx = b.cx(), cy = b.cy() - s * 0.03f;
    const float rad = s * 0.13f, o = s * 0.045f, drop = s * 0.07f;
    box(c, cx, cy + drop * 0.5f, hw + o, hh + drop * 0.5f + o, rad + o, kOutline);
    box(c, cx, cy + drop, hw, hh, rad, hex(0x8c8c98));
    box(c, cx, cy, hw, hh, rad, hex(0xeeeef3));
    const Color ink = hex(0x22222a);
    const float stroke = s * 0.045f;
    if (label == "ENTER") {
        // The return arrow.
        const float k = s * 0.19f;
        line(c, {cx + k, cy - k}, {cx + k, cy + k * 0.35f}, stroke, ink);
        line(c, {cx + k, cy + k * 0.35f}, {cx - k * 0.55f, cy + k * 0.35f}, stroke, ink);
        polygon(c, {{cx - k * 1.05f, cy + k * 0.35f}, {cx - k * 0.45f, cy - k * 0.2f}, {cx - k * 0.45f, cy + k * 0.9f}}, ink);
    } else if (label == "BKSP") {
        // The erase-to-the-left sign: a dark tag pointing left with a light
        // cross in it (an arrow alone reads like the arrow key).
        const float k = s * 0.24f, h = s * 0.19f;
        polygon(c, {{cx - k * 1.15f, cy}, {cx - k * 0.45f, cy - h}, {cx + k, cy - h}, {cx + k, cy + h}, {cx - k * 0.45f, cy + h}}, ink, s * 0.02f);
        const float x = cx + k * 0.22f, d = h * 0.48f;
        line(c, {x - d, cy - d}, {x + d, cy + d}, stroke * 0.85f, hex(0xeeeef3));
        line(c, {x + d, cy - d}, {x - d, cy + d}, stroke * 0.85f, hex(0xeeeef3));
    } else if (label == "SPACE") {
        // The space bracket.
        const float k = std::min(hw * 0.6f, s * 0.3f);
        line(c, {cx - k, cy - s * 0.04f}, {cx - k, cy + s * 0.1f}, stroke, ink);
        line(c, {cx - k, cy + s * 0.1f}, {cx + k, cy + s * 0.1f}, stroke, ink);
        line(c, {cx + k, cy + s * 0.1f}, {cx + k, cy - s * 0.04f}, stroke, ink);
    } else if (arrow_glyph && !label.empty()) {
        const float k = s * 0.17f;
        const char a = label[0];
        const float dx = a == '<' ? -1.0f : a == '>' ? 1.0f : 0.0f, dy = a == '^' ? -1.0f : a == 'v' ? 1.0f : 0.0f;
        const float px = -dy, py = dx;
        polygon(c, {{cx + dx * k, cy + dy * k}, {cx - dx * k * 0.7f + px * k, cy - dy * k * 0.7f + py * k},
                    {cx - dx * k * 0.7f - px * k, cy - dy * k * 0.7f - py * k}},
                ink);
    } else {
        draw_text(c, label, cx, cy, text_cap, hw * 2.0f - s * 0.14f, ink, 1.25f);
    }
}

bool arrow_label(const std::string& label) { return label == "^" || label == "v" || label == "<" || label == ">"; }

// The keyboard's stick, D-pad and C-stick: one key cap as large as a single
// key's with the four keys' labels on it, up above left, down and right
// (W over A S D, the arrows, 8 over 4 2 6), big enough to read in a line of
// text; separate small caps were dots there.
void key_cluster(Canvas& c, const Box& b, const std::array<std::string, 4>& keys) {
    const float s = b.s(), cx = b.cx(), cy = b.cy() - s * 0.03f;
    const float hw = s * 0.47f, hh = s * 0.43f;
    const float rad = s * 0.13f, o = s * 0.045f, drop = s * 0.07f;
    box(c, cx, cy + drop * 0.5f, hw + o, hh + drop * 0.5f + o, rad + o, kOutline);
    box(c, cx, cy + drop, hw, hh, rad, hex(0x8c8c98));
    box(c, cx, cy, hw, hh, rad, hex(0xeeeef3));
    const Color ink = hex(0x22222a);
    const float row_up = cy - hh * 0.46f, row_down = cy + hh * 0.42f, step = hw * 0.62f;
    // (up, down, left, right)
    const std::array<Vec2, 4> at{{{cx, row_up}, {cx, row_down}, {cx - step, row_down}, {cx + step, row_down}}};
    const std::array<Vec2, 4> dir{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
    for (std::size_t i = 0; i < 4; ++i) {
        if (keys[i].empty()) continue; // (an unbound direction: nothing there)
        std::string label = key_cap_label(keys[i]);
        if (arrow_label(label)) {
            // The arrow keys: filled triangles pointing their way.
            const float k = s * 0.12f, dx = dir[i].x, dy = dir[i].y, px = -dy, py = dx;
            const float x = at[i].x, y = at[i].y;
            polygon(c, {{x + dx * k, y + dy * k}, {x - dx * k * 0.7f + px * k, y - dy * k * 0.7f + py * k}, {x - dx * k * 0.7f - px * k, y - dy * k * 0.7f - py * k}},
                    ink);
            continue;
        }
        if (label.size() > 1 && !sign_label(label)) label = label.substr(0, 1);
        if (sign_label(label)) label = label.substr(0, 1); // (a sign key in a cluster: its first letter)
        draw_text(c, label, at[i].x, at[i].y, s * 0.33f, step * 0.95f, ink, 1.5f);
    }
}

// `wide`: a slot the width of a word (key caps and the DualShock 4's
// OPTIONS pill grow into it).
void draw_face(Canvas& c, Box b, PadFamily family, const PromptFace& pf, bool wide) {
    if (family == PadFamily::keyboard) {
        if (pf.face == Face::dpad || pf.face == Face::left_stick || pf.face == Face::right_stick) {
            const float s = b.s();
            key_cluster(c, Box{b.cx() - s * 0.5f, b.cy() - s * 0.5f, b.cx() + s * 0.5f, b.cy() + s * 0.5f}, pf.keys);
            return;
        }
        if (pf.key.empty()) return;
        const std::string label = key_cap_label(pf.key);
        Box cap = b;
        if (!wide) {
            // (Square slots: a long name gets a wider cap only as far as the slot.)
        }
        key_cap(c, cap, label, arrow_label(label));
        return;
    }
    const Style st = style_of(family);
    const float s = b.s();
    const float cx = b.cx(), cy = b.cy();
    const float r = s * 0.47f;
    switch (pf.face) {
    case Face::none:
        if (!pf.key.empty()) key_cap(c, b, key_cap_label(pf.key), false); // (the GameCube pad's F1)
        return;
    case Face::letter_a: case Face::letter_b: case Face::letter_x: case Face::letter_y: {
        const char* text = pf.face == Face::letter_a ? "A" : pf.face == Face::letter_b ? "B" : pf.face == Face::letter_x ? "X" : "Y";
        if (family == PadFamily::gamecube) {
            // The GameCube's own: a big green A, a small red B, kidney-shaped
            // grey X and Y.
            if (pf.face == Face::letter_a) {
                round_button(c, cx, cy, r, hex(0x35a84a), hex(0xffffff, 0.25f));
                draw_text(c, text, cx, cy, r * 1.0f, r * 1.4f, hex(0x0f4a1d));
            } else if (pf.face == Face::letter_b) {
                round_button(c, cx, cy, r * 0.82f, hex(0xd8343a), hex(0xffffff, 0.25f));
                draw_text(c, text, cx, cy, r * 0.86f, r * 1.2f, hex(0x5a0c10));
            } else {
                const bool x = pf.face == Face::letter_x;
                const float hw = x ? r * 0.62f : r * 1.0f, hh = x ? r * 1.0f : r * 0.62f;
                box(c, cx, cy, hw + s * 0.045f, hh + s * 0.045f, std::min(hw, hh) + s * 0.045f, kOutline);
                box(c, cx, cy, hw, hh, std::min(hw, hh), st.face);
                draw_text(c, text, cx, cy, r * 0.86f, hw * 1.6f, st.letter);
            }
            return;
        }
        Color fill = st.face, letter = st.letter;
        if (st.fill_coloured) fill = xbox_colour(pf.face);
        if (st.letters_coloured) letter = xbox_colour(pf.face);
        if (st.snes_colours) letter = snes_colour(pf.face);
        round_button(c, cx, cy, r, fill, st.face_rim);
        draw_text(c, text, cx, cy, r * 1.02f, r * 1.4f, letter);
        return;
    }
    case Face::cross: case Face::circle: case Face::square: case Face::triangle:
        round_button(c, cx, cy, r, st.face, st.face_rim);
        ps_symbol(c, pf.face, cx, cy, r, family == PadFamily::ps5 ? st.letter : ps_colour(pf.face));
        return;
    case Face::lb: shoulder(c, b, "LB", false, st.shoulder, st.shoulder_label); return;
    case Face::rb: shoulder(c, b, "RB", false, st.shoulder, st.shoulder_label); return;
    case Face::lt: shoulder(c, b, "LT", true, st.shoulder, st.shoulder_label); return;
    case Face::rt: shoulder(c, b, "RT", true, st.shoulder, st.shoulder_label); return;
    case Face::l1: shoulder(c, b, "L1", false, st.shoulder, st.shoulder_label); return;
    case Face::r1: shoulder(c, b, "R1", false, st.shoulder, st.shoulder_label); return;
    case Face::l2: shoulder(c, b, "L2", true, st.shoulder, st.shoulder_label); return;
    case Face::r2: shoulder(c, b, "R2", true, st.shoulder, st.shoulder_label); return;
    case Face::l: shoulder(c, b, "L", family == PadFamily::gamecube, st.shoulder, st.shoulder_label); return;
    case Face::r: shoulder(c, b, "R", family == PadFamily::gamecube, st.shoulder, st.shoulder_label); return;
    case Face::zl: shoulder(c, b, "ZL", true, st.shoulder, st.shoulder_label); return;
    case Face::zr: shoulder(c, b, "ZR", true, st.shoulder, st.shoulder_label); return;
    case Face::z: shoulder(c, b, "Z", false, hex(0x5a5ed0), hex(0xf4f4ff)); return;
    case Face::gl: shoulder(c, b, "GL", false, st.shoulder, st.shoulder_label); return;
    case Face::gr: shoulder(c, b, "GR", false, st.shoulder, st.shoulder_label); return;
    case Face::paddle_r1: shoulder(c, b, "P1", false, st.shoulder, st.shoulder_label); return;
    case Face::paddle_r2: shoulder(c, b, "P2", false, st.shoulder, st.shoulder_label); return;
    case Face::paddle_l1: shoulder(c, b, "P3", false, st.shoulder, st.shoulder_label); return;
    case Face::paddle_l2: shoulder(c, b, "P4", false, st.shoulder, st.shoulder_label); return;
    case Face::menu: case Face::view: case Face::start_360: case Face::back_360: case Face::plus: case Face::minus:
    case Face::home: case Face::misc: case Face::c_button: case Face::start_arrow: case Face::select_square: case Face::back_key: {
        // As large as a face button (smaller ones read as a dot beside A / B).
        const float rm = r;
        round_button(c, cx, cy, rm, pf.face == Face::back_key ? hex(0x23262e) : st.middle, st.face_rim);
        const Color sym = st.middle_symbol;
        const float half = rm * 0.1f;
        switch (pf.face) {
        case Face::menu: lines3(c, cx, cy, rm * 0.95f, rm * 0.34f, half, sym); break;
        case Face::view: {
            const float k = rm * 0.3f;
            box_ring(c, cx - k * 0.35f, cy - k * 0.35f, k, k * 0.8f, k * 0.2f, half * 0.8f, sym);
            box(c, cx + k * 0.35f, cy + k * 0.35f, k, k * 0.8f, k * 0.2f, st.middle);
            box_ring(c, cx + k * 0.35f, cy + k * 0.35f, k, k * 0.8f, k * 0.2f, half * 0.8f, sym);
            break;
        }
        case Face::start_360: triangle_fill(c, cx + rm * 0.06f, cy, rm * 0.5f, true, sym); break;
        // The AYN Thor's printing (the PlayStation 2's signs): START a
        // right-pointing arrow, SELECT a square.
        case Face::start_arrow: triangle_fill(c, cx + rm * 0.06f, cy, rm * 0.5f, true, sym); break;
        case Face::select_square: box(c, cx, cy, rm * 0.36f, rm * 0.36f, rm * 0.05f, sym); break;
        // Android's Back key: a left-pointing triangle (white on a dark key).
        case Face::back_key: triangle_fill(c, cx - rm * 0.08f, cy, rm * 0.52f, false, hex(0xf2f3f7)); break;
        case Face::back_360: triangle_fill(c, cx - rm * 0.06f, cy, rm * 0.5f, false, sym); break;
        case Face::plus:
            line(c, {cx - rm * 0.5f, cy}, {cx + rm * 0.5f, cy}, half * 1.2f, sym);
            line(c, {cx, cy - rm * 0.5f}, {cx, cy + rm * 0.5f}, half * 1.2f, sym);
            break;
        case Face::minus: line(c, {cx - rm * 0.5f, cy}, {cx + rm * 0.5f, cy}, half * 1.2f, sym); break;
        case Face::home: {
            const float k = rm * 0.48f;
            polygon(c, {{cx, cy - k}, {cx + k, cy - k * 0.05f}, {cx + k * 0.66f, cy - k * 0.05f}, {cx + k * 0.66f, cy + k * 0.8f},
                        {cx - k * 0.66f, cy + k * 0.8f}, {cx - k * 0.66f, cy - k * 0.05f}, {cx - k, cy - k * 0.05f}},
                    sym);
            break;
        }
        case Face::c_button: draw_text(c, "C", cx, cy, rm * 1.0f, rm * 1.4f, sym); break;
        default: circle(c, cx, cy, rm * 0.16f, sym); break;
        }
        return;
    }
    case Face::options: case Face::share: case Face::create: case Face::start_gc: case Face::touchpad: case Face::mic: case Face::capture: {
        const Color sym = st.middle_symbol;
        if (pf.face == Face::start_gc) {
            const float hw = wide ? std::min(b.w() * 0.46f, s * 1.3f) : s * 0.47f, hh = wide ? s * 0.36f : s * 0.27f;
            pill(c, cx, cy, hw, hh, st.middle, hex(0, 0));
            draw_text(c, "START", cx, cy, hh * 0.9f, hw * 1.7f, sym);
            return;
        }
        if (pf.face == Face::touchpad) {
            box(c, cx, cy, s * 0.47f, s * 0.3f, s * 0.08f, kOutline);
            box(c, cx, cy, s * 0.43f, s * 0.26f, s * 0.06f, st.middle);
            return;
        }
        if (pf.face == Face::capture) {
            box(c, cx, cy, s * 0.34f, s * 0.34f, s * 0.08f, kOutline);
            box(c, cx, cy, s * 0.3f, s * 0.3f, s * 0.06f, st.middle);
            ring(c, cx, cy, s * 0.15f, s * 0.035f, sym);
            return;
        }
        // The DualShock 4's Options and Share buttons are told apart by the
        // words printed by them (its pills carry the words); the DualSense's
        // by their signs (Options three lines, Create a burst of three
        // strokes). The same pill in a word's slot, a glyph's and the title.
        const bool ds4_words = family == PadFamily::ps4 && (pf.face == Face::options || pf.face == Face::share);
        const char* word = pf.face == Face::options ? "OPTIONS" : "SHARE";
        float hw = s * 0.44f, hh = s * 0.26f;
        if (ds4_words) {
            hh = wide ? s * 0.36f : s * 0.3f;
            const float text = hh * 0.82f;
            hw = std::min((text_units(word) + 1.2f) * text / 6.0f * 0.5f + hh * 0.75f, (wide ? b.w() * 0.49f : s * 0.49f));
        } else if (wide) {
            hw = s * 0.62f, hh = s * 0.36f;
        }
        pill(c, cx, cy, hw, hh, st.middle, st.face_rim);
        if (ds4_words) {
            draw_text(c, word, cx, cy, hh * 0.82f, (hw - hh * 0.45f) * 2.0f, sym, 1.25f);
        } else if (pf.face == Face::options) {
            lines3(c, cx, cy, hh * 1.3f, hh * 0.46f, hh * 0.11f, sym);
        } else if (pf.face == Face::share) {
            line(c, {cx, cy + hh * 0.5f}, {cx, cy - hh * 0.5f}, hh * 0.1f, sym);
            line(c, {cx - hh * 0.4f, cy - hh * 0.1f}, {cx, cy - hh * 0.5f}, hh * 0.1f, sym);
            line(c, {cx + hh * 0.4f, cy - hh * 0.1f}, {cx, cy - hh * 0.5f}, hh * 0.1f, sym);
        } else if (pf.face == Face::create) {
            // A burst: three strokes fanning up from below the middle.
            const float k = hh * 0.62f;
            line(c, {cx, cy + k * 0.15f}, {cx, cy - k}, hh * 0.1f, sym);
            line(c, {cx - k * 0.32f, cy + k * 0.3f}, {cx - k * 0.95f, cy - k * 0.6f}, hh * 0.1f, sym);
            line(c, {cx + k * 0.32f, cy + k * 0.3f}, {cx + k * 0.95f, cy - k * 0.6f}, hh * 0.1f, sym);
        } else {
            circle(c, cx, cy, hh * 0.3f, sym); // mute
        }
        return;
    }
    case Face::left_stick: case Face::right_stick: case Face::left_stick_click: case Face::right_stick_click: {
        const bool left = pf.face == Face::left_stick || pf.face == Face::left_stick_click;
        const bool click = pf.face == Face::left_stick_click || pf.face == Face::right_stick_click;
        std::string label = left ? "L" : "R";
        if (click) label = family == PadFamily::ps4 || family == PadFamily::ps5 ? (left ? "L3" : "R3") : (left ? "LS" : "RS");
        if (family == PadFamily::gamecube) label.clear();
        stick_icon(c, b, st, label, family == PadFamily::gamecube, st.stick_cap);
        return;
    }
    case Face::c_stick: {
        // The GameCube's yellow C-stick in its yellow gate.
        Style yellow = st;
        yellow.stick = hex(0xc9a21a);
        yellow.stick_label = hex(0x5a4300);
        stick_icon(c, b, yellow, "C", true, hex(0xf6cc2a));
        return;
    }
    case Face::dpad: case Face::dpad_up: case Face::dpad_down: case Face::dpad_left: case Face::dpad_right:
        dpad_icon(c, b, st, pf.face);
        return;
    }
}

constexpr int kPitchX = PromptAtlas::kCellWidth + PromptAtlas::kGutter;
constexpr int kPitchY = PromptAtlas::kCellHeight + PromptAtlas::kGutter;
// The word START spans 0.78 x 0.27 font units (four letters of 0.16 and one
// of 0.14): its cell keeps that shape.
constexpr int kWordWidth = 376;

Box icon_box(const PromptAtlas::Cell& cell, int margin) {
    return Box{float(cell.x + margin), float(cell.y + PromptAtlas::kIconTop + margin), float(cell.x + cell.width - margin),
               float(cell.y + PromptAtlas::kIconTop + PromptAtlas::kIconSize - margin)};
}

} // namespace

PromptAtlas::Cell PromptAtlas::icon(PromptControl control) const {
    const auto& cell = cells[std::size_t(control)];
    return Cell{cell.x, cell.y + kIconTop, cell.width, kIconSize};
}

std::string key_cap_label(const std::string& key_name) {
    std::string name = key_name;
    if (name == "Up") return "^";
    if (name == "Down") return "v";
    if (name == "Left") return "<";
    if (name == "Right") return ">";
    if (name.rfind("Keypad ", 0) == 0) name = name.substr(7);
    if (name == "Return" || name == "Enter") return "ENTER";
    if (name == "Escape") return "ESC";
    if (name == "Backspace") return "BKSP";
    if (name == "Space") return "SPACE";
    if (name == "Delete") return "DEL";
    if (name == "Insert") return "INS";
    if (name == "PageUp") return "PGUP";
    if (name == "PageDown") return "PGDN";
    for (const char* side : {"Left ", "Right "})
        if (name.rfind(side, 0) == 0) name = name.substr(std::strlen(side));
    if (name == "Shift" || name == "Ctrl" || name == "Alt" || name == "Tab" || name == "Home" || name == "End") {
        for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        return name;
    }
    for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    // Unknown long names: their first letters (the cap shrinks its text too).
    if (name.size() > 5) name = name.substr(0, 4);
    return name;
}

void draw_prompt(ArtImage& image, float x0, float y0, float x1, float y1, PadFamily family, const PromptFace& face, bool wide) {
    Canvas canvas(image.width, image.height);
    draw_face(canvas, Box{x0, y0, x1, y1}, family, face, wide);
    const ArtImage drawn = canvas.image();
    // Over what the image holds.
    for (std::size_t i = 0; i < drawn.rgba.size(); i += 4) {
        const float a = float(drawn.rgba[i + 3]) / 255.0f;
        if (a <= 0) continue;
        const float da = float(image.rgba[i + 3]) / 255.0f;
        const float oa = a + da * (1 - a);
        for (int c = 0; c < 3; ++c)
            image.rgba[i + std::size_t(c)] = std::uint8_t(std::lround(
                (float(drawn.rgba[i + std::size_t(c)]) * a + float(image.rgba[i + std::size_t(c)]) * da * (1 - a)) / std::max(oa, 1e-6f)));
        image.rgba[i + 3] = std::uint8_t(std::lround(oa * 255.0f));
    }
}

PromptAtlas build_prompt_atlas(PadFamily family, const Settings& settings) {
    PromptAtlas atlas;
    constexpr int rows = (int(kPromptControlCount) + PromptAtlas::kColumns - 1) / PromptAtlas::kColumns;
    const int width = PromptAtlas::kColumns * kPitchX;
    const int height = (rows + 1) * kPitchY;
    Canvas canvas(width, height);
    for (std::size_t i = 0; i < kPromptControlCount; ++i) {
        auto& cell = atlas.cells[i];
        cell.x = int(i % PromptAtlas::kColumns) * kPitchX + PromptAtlas::kGutter / 2;
        cell.y = int(i / PromptAtlas::kColumns) * kPitchY + PromptAtlas::kGutter / 2;
        cell.width = PromptAtlas::kCellWidth;
        cell.height = PromptAtlas::kCellHeight;
        draw_face(canvas, icon_box(cell, 1), family, prompt_face(family, settings, PromptControl(i)), false);
    }
    atlas.start_word = {PromptAtlas::kGutter / 2, rows * kPitchY + PromptAtlas::kGutter / 2, kWordWidth, PromptAtlas::kCellHeight};
    draw_face(canvas, icon_box(atlas.start_word, 1), family, prompt_face(family, settings, PromptControl::start), true);
    atlas.image = canvas.image();
    // The columns the START picture takes (and two pixels on each side).
    const auto& word = atlas.start_word;
    int ink0 = word.x + word.width, ink1 = word.x - 1;
    for (int y = word.y; y < word.y + word.height; ++y)
        for (int x = word.x; x < word.x + word.width; ++x)
            if (atlas.image.rgba[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4 + 3] > 8) ink0 = std::min(ink0, x), ink1 = std::max(ink1, x);
    if (ink1 < ink0) ink0 = ink1 = word.x + word.width / 2; // (nothing drawn: a sliver)
    ink0 = std::max(word.x, ink0 - 2);
    ink1 = std::min(word.x + word.width - 1, ink1 + 2);
    atlas.start_word_ink = {ink0, word.y, ink1 - ink0 + 1, word.height};
    return atlas;
}

std::uint64_t PromptAtlasCache::signature(const Settings& settings) {
    std::uint64_t hash = 0x5eed;
    const auto mix = [&](std::uint64_t value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
    for (const auto& inputs : settings.pad)
        for (const auto& input : inputs) mix((std::uint64_t(input.kind) << 32) | std::uint32_t(input.code));
    for (const auto& keys : settings.keys)
        for (const auto code : keys) mix(std::uint64_t(code));
    mix(std::uint64_t(settings.nintendo_layout));
    mix(std::uint64_t(settings.gamecube_layout));
    mix(std::uint64_t(settings.menu_buttons));
    return hash ? hash : 1;
}

void PromptAtlasCache::set_settings(const Settings& settings) {
    settings_ = &settings;
    signature_ = signature(settings);
}

bool PromptAtlasCache::current(PadFamily family) const {
    return family < PadFamily::count && signature_ != 0 && drawn_for_[std::size_t(family)] == signature_;
}

const PromptAtlas& PromptAtlasCache::atlas(PadFamily family) {
    const auto f = std::size_t(family < PadFamily::count ? family : PadFamily::generic);
    if (!current(PadFamily(f))) {
        static const Settings defaults;
        atlases_[f] = build_prompt_atlas(PadFamily(f), settings_ ? *settings_ : defaults);
        drawn_for_[f] = signature_;
        ++generation_[f];
        ++draws_;
    }
    return atlases_[f];
}

bool title_press_start(const ArtImage& original, int min_width, bool nearest, PadFamily family, const Settings& settings, ArtImage& out) {
    const int in_w = original.width, in_h = original.height;
    if (in_w < 256 || in_h * 2 != in_w || original.rgba.size() != std::size_t(in_w) * std::size_t(in_h) * 4u) return false;
    // The word START in the original's texels (stored rows 99..124, columns
    // 88..166; the rows above are empty up to "Game Start / Option", which
    // TitleEntry reads and this leaves alone), in this texture's.
    constexpr int kX0 = 84, kX1 = 171, kY0 = 96, kY1 = 128;
    const float in_scale = float(in_w) / 256.0f;
    const auto alpha_at = [&](int x, int y) { return original.rgba[(std::size_t(y) * std::size_t(in_w) + std::size_t(x)) * 4 + 3]; };
    {
        // The word stands alone there: ink inside, none across its left and
        // right borders (a texture pack that moved or restyled it is left
        // as it is).
        const int x0 = int(std::lround(kX0 * in_scale)), x1 = int(std::lround(kX1 * in_scale));
        const int y0 = int(std::lround(kY0 * in_scale)), y1 = std::min(in_h, int(std::lround(kY1 * in_scale)));
        const int border = std::max(1, int(std::lround(2 * in_scale)));
        std::size_t inside = 0, edge = 0, edge_ink = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) inside += alpha_at(x, y) > 128;
            for (int k = 0; k < border; ++k)
                for (const int x : {x0 - 1 - k, x1 + k}) {
                    ++edge;
                    edge_ink += alpha_at(std::clamp(x, 0, in_w - 1), y) > 64;
                }
        }
        if (inside < std::size_t((x1 - x0) * (y1 - y0)) / 20 || edge_ink * 10 > edge) return false;
    }
    // The texture, a whole number of times its size up to `min_width` (as it
    // would be magnified).
    const int scale = std::max(1, (std::max(min_width, in_w) + in_w - 1) / in_w);
    out = ArtImage(in_w * scale, in_h * scale);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            std::uint8_t* o = &out.rgba[(std::size_t(y) * std::size_t(out.width) + std::size_t(x)) * 4];
            if (nearest || scale == 1) {
                const std::uint8_t* p = &original.rgba[(std::size_t(y / scale) * std::size_t(in_w) + std::size_t(x / scale)) * 4];
                std::memcpy(o, p, 4);
                continue;
            }
            const float sx = (float(x) + 0.5f) / float(scale) - 0.5f, sy = (float(y) + 0.5f) / float(scale) - 0.5f;
            const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
            const float fx = sx - float(ix), fy = sy - float(iy);
            float acc[4]{};
            float weight_alpha = 0;
            for (int k = 0; k < 4; ++k) {
                const int px = std::clamp(ix + (k & 1), 0, in_w - 1), py = std::clamp(iy + (k >> 1), 0, in_h - 1);
                const float w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                const std::uint8_t* p = &original.rgba[(std::size_t(py) * std::size_t(in_w) + std::size_t(px)) * 4];
                const float a = float(p[3]) * w;
                for (int c = 0; c < 3; ++c) acc[c] += float(p[c]) * a;
                acc[3] += a;
                weight_alpha += a;
            }
            for (int c = 0; c < 3; ++c) o[c] = std::uint8_t(std::lround(weight_alpha > 0 ? acc[c] / weight_alpha : 0.0f));
            o[3] = std::uint8_t(std::lround(acc[3]));
        }
    // The word cleared, the START picture drawn upright into a copy and
    // stored upside down like the texture.
    const float out_scale = float(out.width) / 256.0f;
    const int ox0 = int(std::lround(kX0 * out_scale)), ox1 = int(std::lround(kX1 * out_scale));
    const int oy0 = int(std::lround(kY0 * out_scale)), oy1 = std::min(out.height, int(std::lround(kY1 * out_scale)));
    for (int y = oy0; y < oy1; ++y)
        for (int x = ox0; x < ox1; ++x) out.rgba[(std::size_t(y) * std::size_t(out.width) + std::size_t(x)) * 4 + 3] = 0;
    const int w = ox1 - ox0, h = oy1 - oy0;
    ArtImage word(w, h);
    // (The word's ink is 26 rows: the picture keeps that height.)
    const float margin = float(h) * 0.06f;
    draw_prompt(word, 0, margin, float(w), float(h) - margin, family, prompt_face(family, settings, PromptControl::start), true);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = &word.rgba[(std::size_t(h - 1 - y) * std::size_t(w) + std::size_t(x)) * 4];
            std::memcpy(&out.rgba[(std::size_t(oy0 + y) * std::size_t(out.width) + std::size_t(ox0 + x)) * 4], p, 4);
        }
    return true;
}

} // namespace dw4::game
