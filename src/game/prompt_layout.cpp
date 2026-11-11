#include "prompt_layout.hpp"

#include "pad_family.hpp"

#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace dw4::game {
namespace {

bool same_cell(float u0, float v0, float u1, float v1, const PromptGlyphCell& cell) {
    constexpr float e = 1e-4f;
    return std::fabs(u0 - std::min(cell.u0, cell.u1)) < e && std::fabs(u1 - std::max(cell.u0, cell.u1)) < e &&
           std::fabs(v0 - std::min(cell.v0, cell.v1)) < e && std::fabs(v1 - std::max(cell.v0, cell.v1)) < e;
}

struct QuadInfo {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, z = 0;
    int control = -1; // a button glyph
    int letter = -1;  // S T A R
    bool start_glyph = false;
    bool space = false; // (the font's space has a quad of its own)
    bool word_char = false;
};

} // namespace

PromptLayout layout_prompts(const engine::GeometryData& g, const PromptGlyphs& glyphs, float word_aspect, int align) {
    PromptLayout out;
    const std::size_t quads = g.positions.size() / 4;
    bool layout = glyphs.valid && g.positions.size() % 4 == 0 && g.texcoords.size() == g.positions.size() * 2 && g.indices.size() == quads * 6;
    for (std::size_t q = 0; layout && q < quads; ++q) layout = g.indices[q * 6] == q * 4;
    if (!layout) return out;
    std::vector<QuadInfo> info(quads);
    for (std::size_t q = 0; q < quads; ++q) {
        auto& qi = info[q];
        float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
        for (std::size_t k = 0; k < 4; ++k) {
            const auto& p = g.positions[q * 4 + k];
            const float u = g.texcoords[(q * 4 + k) * 2], v = g.texcoords[(q * 4 + k) * 2 + 1];
            u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
            qi.x0 = std::min(qi.x0, p.x), qi.x1 = std::max(qi.x1, p.x), qi.y0 = std::min(qi.y0, p.y), qi.y1 = std::max(qi.y1, p.y);
        }
        qi.z = g.positions[q * 4].z;
        for (const auto& cell : glyphs.buttons)
            if (same_cell(u0, v0, u1, v1, cell)) qi.control = cell.control;
        for (int l = 0; l < 4; ++l)
            if (same_cell(u0, v0, u1, v1, glyphs.letters[std::size_t(l)])) qi.letter = l;
        qi.start_glyph = same_cell(u0, v0, u1, v1, glyphs.start_glyph);
        qi.space = same_cell(u0, v0, u1, v1, glyphs.space);
        for (const auto& cell : glyphs.word_chars)
            if (same_cell(u0, v0, u1, v1, cell)) qi.word_char = true;
        // (S T A R are letters too, whether or not the font listed them.)
        if (qi.letter >= 0) qi.word_char = true;
    }
    // START: S T A R T on one row, a word of its own (no letter or digit
    // touching it on either side: "STARTS", "RESTART" stay text) and no
    // START glyph right before or after it (a space between counts as
    // right: "START <glyph> button").
    constexpr int kWord[5] = {0, 1, 2, 3, 1};
    std::vector<int> word_at(quads, -1); // quad -> the word's first quad
    std::vector<float> shift(quads, 0.0f);
    std::vector<std::pair<std::size_t, float>> word_pictures; // first quad, picture width
    for (std::size_t q = 0; word_aspect > 0 && q + 5 <= quads; ++q) {
        const float height = info[q].y1 - info[q].y0;
        const auto same_row = [&](std::size_t other) { return std::fabs(info[other].y0 - info[q].y0) < height * 0.3f; };
        bool match = true;
        for (std::size_t k = 0; k < 5 && match; ++k) match = info[q + k].letter == kWord[k] && same_row(q + k);
        if (!match) continue;
        const float touch = height * 0.17f;
        if (q > 0 && same_row(q - 1) && info[q - 1].word_char && info[q].x0 - info[q - 1].x1 < touch) continue;
        if (q + 5 < quads && same_row(q + 5) && info[q + 5].word_char && info[q + 5].x0 - info[q + 4].x1 < touch) continue;
        // The quad beside the word, past one space (none: another row).
        const auto beside = [&](std::size_t from, int step) -> const QuadInfo* {
            for (std::ptrdiff_t i = std::ptrdiff_t(from) + step; i >= 0 && i < std::ptrdiff_t(quads); i += step) {
                if (!same_row(std::size_t(i))) return nullptr;
                if (!info[std::size_t(i)].space) return &info[std::size_t(i)];
                if (std::abs(i - std::ptrdiff_t(from)) > 1) return nullptr;
            }
            return nullptr;
        };
        const QuadInfo* before = beside(q, -1);
        const QuadInfo* after = beside(q + 4, 1);
        if ((before && before->start_glyph) || (after && after->start_glyph)) continue;
        for (std::size_t k = 0; k < 5; ++k) word_at[q + k] = int(q);
        // The picture takes the family's width; its row closes up (or makes
        // room) the way the line is aligned.
        const float width = height * word_aspect;
        const float delta = (info[q + 4].x1 - info[q].x0) - width; // > 0: narrower than the word
        std::size_t row_begin = q, row_end = q + 5;
        while (row_begin > 0 && same_row(row_begin - 1)) --row_begin;
        while (row_end < quads && same_row(row_end)) ++row_end;
        const float before_move = align == 1 ? delta : align == 2 ? delta * 0.5f : 0.0f;
        const float after_move = before_move - delta;
        for (std::size_t i = row_begin; i < row_end; ++i) shift[i] += i < q ? before_move : i > q + 4 ? after_move : before_move;
        word_pictures.emplace_back(q, width);
        q += 4;
    }
    for (std::size_t q = 0; q < quads; ++q) {
        const auto& qi = info[q];
        if (word_at[q] >= 0) {
            if (word_at[q] != int(q)) continue;
            float width = 0;
            for (const auto& [first, w] : word_pictures)
                if (first == q) width = w;
            const float x0 = qi.x0 + shift[q];
            out.pictures.push_back({x0, qi.y0, x0 + width, qi.y1, qi.z, -1});
            ++out.words;
            continue;
        }
        if (qi.control >= 0) {
            out.pictures.push_back({qi.x0 + shift[q], qi.y0, qi.x1 + shift[q], qi.y1, qi.z, qi.control});
            continue;
        }
        out.text.push_back({q, shift[q]});
    }
    return out;
}

PromptGlyphs prompt_glyphs(const engine::FontImage& font) {
    PromptGlyphs glyphs;
    // (The font texture the renderer matches the lines by; the glyph cells
    // do without it.)
    if (font.binding && font.binding->image) {
        glyphs.document = font.binding->image.document();
        glyphs.reference = font.binding->image.reference();
    }
    auto cell = [&](std::uint32_t code, int control) -> std::optional<PromptGlyphCell> {
        const auto found = font.glyphs.find(code);
        if (found == font.glyphs.end()) return std::nullopt;
        const auto& g = found->second;
        return PromptGlyphCell{g.u0, g.v0, g.u1, g.v1, control};
    };
    // The button glyphs (SJIS 81DA..81E6, 889F, 88A1 through the 802FB59C
    // table, ui.cpp): A, B small and large, X, Y, START, L, R, Z, the D-pad,
    // the control stick and the C-stick.
    const std::pair<std::uint32_t, PromptControl> buttons[] = {
        {0x226A, PromptControl::a},     {0x226B, PromptControl::b},     {0x2220, PromptControl::b},
        {0x2252, PromptControl::x},     {0x2312, PromptControl::y},     {0x22A5, PromptControl::start},
        {0x221A, PromptControl::l},     {0x4E9C, PromptControl::r},     {0x2235, PromptControl::z},
        {0x2261, PromptControl::dpad},  {0x2207, PromptControl::stick}, {0x5A03, PromptControl::c_stick},
    };
    for (const auto& [code, control] : buttons)
        if (const auto c = cell(code, int(control))) glyphs.buttons.push_back(*c);
    const char letters[4] = {'S', 'T', 'A', 'R'};
    bool word = true;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto c = cell(std::uint32_t(letters[i]), -1);
        word = word && c.has_value();
        if (c) glyphs.letters[i] = *c;
    }
    if (const auto c = cell(0x22A5, int(PromptControl::start))) glyphs.start_glyph = *c;
    if (const auto c = cell(0x20, -1)) glyphs.space = *c;
    // Letters and digits, ASCII and full width (a START touching one is
    // part of a longer word); a cell once (full width shares ASCII's).
    const auto add_word_char = [&](std::uint32_t code) {
        const auto c = cell(code, -1);
        if (!c) return;
        for (const auto& known : glyphs.word_chars)
            if (known.u0 == c->u0 && known.v0 == c->v0 && known.u1 == c->u1 && known.v1 == c->v1) return;
        glyphs.word_chars.push_back(*c);
    };
    for (const auto& [first, last] : {std::pair<std::uint32_t, std::uint32_t>{'0', '9'}, {'A', 'Z'}, {'a', 'z'}, {0xFF10, 0xFF19}, {0xFF21, 0xFF3A},
                                      {0xFF41, 0xFF5A}})
        for (std::uint32_t code = first; code <= last; ++code) add_word_char(code);
    // (Without the letters no word is matched: a cell of zero size matches nothing.)
    if (!word) glyphs.letters = {};
    glyphs.valid = glyphs.buttons.size() == std::size(buttons);
    return glyphs;
}

} // namespace dw4::game
