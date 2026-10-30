#include "title_entry.hpp"

#include "dw4/textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace dw4::game {

namespace {

std::string texture_name(const engine::RenderState& state) {
    if (!state.texture_enabled || !state.texture || !state.texture->image) return "-";
    return std::string(state.texture->image.str(22));
}

bool flat(const engine::DrawItem& item) { return item.state.projection && (*item.state.projection)(2, 3) == 0.0f; }

// A quad's place and its texture rectangle (u and v at its left / right and
// bottom / top edges, in whichever direction the data runs).
struct Quad {
    float x0{}, x1{}, y0{}, y1{};
    float u_left{}, u_right{}, v_bottom{}, v_top{};
};
std::optional<Quad> quad_of(const engine::GeometryData& g) {
    if (g.positions.size() != 4 || g.texcoords.size() != 8) return std::nullopt;
    Quad q;
    q.x0 = q.x1 = g.positions[0].x;
    q.y0 = q.y1 = g.positions[0].y;
    for (const auto& p : g.positions) {
        q.x0 = std::min(q.x0, p.x), q.x1 = std::max(q.x1, p.x);
        q.y0 = std::min(q.y0, p.y), q.y1 = std::max(q.y1, p.y);
    }
    if (q.x1 - q.x0 < 1 || q.y1 - q.y0 < 1) return std::nullopt;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& p = g.positions[i];
        (p.x < (q.x0 + q.x1) / 2 ? q.u_left : q.u_right) = g.texcoords[2 * i];
        (p.y < (q.y0 + q.y1) / 2 ? q.v_bottom : q.v_top) = g.texcoords[2 * i + 1];
    }
    return q;
}

float lo(float a, float b) { return std::min(a, b); }
float hi(float a, float b) { return std::max(a, b); }

// The rows "Game Start" and "Option" of the title textures (mojips / mojigc,
// 256 x 128): both quads sample v 0.51..0.75, "Game Start" from u 0.004,
// "Option" from u 0.59 (measured on the USA data).
bool menu_row(const Quad& q) { return lo(q.v_bottom, q.v_top) > 0.45f && hi(q.v_bottom, q.v_top) < 0.8f; }
bool game_start(const Quad& q) { return menu_row(q) && lo(q.u_left, q.u_right) < 0.05f && hi(q.u_left, q.u_right) < 0.62f; }
bool option(const Quad& q) { return menu_row(q) && lo(q.u_left, q.u_right) > 0.55f && lo(q.u_left, q.u_right) < 0.62f; }
// The copyright lines (v below 0.45, nearly the texture's whole width).
bool copyright(const Quad& q) { return hi(q.v_bottom, q.v_top) < 0.45f && std::fabs(q.u_right - q.u_left) > 0.9f; }

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// Ink columns of a texel rectangle: runs of columns whose strongest alpha is
// above a threshold.
struct Run {
    int c0 = 0, c1 = 0; // inclusive texel columns
};
std::vector<Run> ink_runs(const RgbaImage& image, int cx0, int cx1, int ry0, int ry1) {
    std::vector<Run> runs;
    int start = -1;
    for (int x = cx0; x <= cx1 + 1; ++x) {
        int strongest = 0;
        if (x <= cx1)
            for (int y = ry0; y <= ry1; ++y) strongest = std::max(strongest, int(image.rgba[(std::size_t(y) * image.width + std::size_t(x)) * 4 + 3]));
        if (strongest > 40 && start < 0) start = x;
        if (strongest <= 40 && start >= 0) {
            runs.push_back({start, x - 1});
            start = -1;
        }
    }
    return runs;
}

int alpha(const RgbaImage& image, int x, int y) { return image.rgba[(std::size_t(y) * image.width + std::size_t(x)) * 4 + 3]; }

} // namespace

void TitleEntry::trace(const std::vector<engine::DrawItem>& items, std::uint64_t frame) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (!flat(item) || !item.geometry) continue;
        const auto& g = *item.geometry;
        const auto& m = item.model;
        std::printf("title-entry %llu item %zu %s node %s t %.2f %.2f %.2f s %.4f %.4f color %.3f %.3f %.3f %.3f verts %zu\n",
                    (unsigned long long)frame, i, texture_name(item.state).c_str(), item.node ? item.node->name.c_str() : "-", m(3, 0), m(3, 1),
                    m(3, 2), m(0, 0), m(1, 1), item.state.color.x, item.state.color.y, item.state.color.z, item.state.color.w, g.positions.size());
    }
    std::fflush(stdout);
}

bool TitleEntry::apply(std::vector<engine::DrawItem>& items, bool menu_open, bool cursor_online, std::uint64_t frame) {
    // The layout eases in and out over 15 frames; the arrows slide as the
    // model's LINEDW01 / LINEUP00 slide them (linear, 2.4 units a frame:
    // a row of 50 in about 21 frames).
    const float frames = last_ == 0 || frame < last_ ? 60.0f : float(std::min<std::uint64_t>(frame - last_, 60));
    last_ = frame;
    auto move = [&](float& value, bool on, float per_frame) {
        value = on ? std::min(1.0f, value + frames * per_frame) : std::max(0.0f, value - frames * per_frame);
    };
    move(layout_, menu_open, 1.0f / 15.0f);
    move(arrows_, menu_open && cursor_online, 2.4f / 50.0f);
    if (!menu_open) arrows_ = 0;
    const auto smooth = [](float t) { return t * t * (3 - 2 * t); };
    const float layout = smooth(layout_);

    // The pieces: the two rows' text quads, the band, the arrows, the logo
    // and the copyright.
    int game = -1, opt = -1, logo = -1, copy = -1;
    std::vector<int> cursor_items;
    for (int i = 0; i < int(items.size()); ++i) {
        const auto& item = items[std::size_t(i)];
        if (!flat(item) || !item.geometry) continue;
        const auto name = texture_name(item.state);
        if (contains(name, "moji")) {
            const auto q = quad_of(*item.geometry);
            if (!q) continue;
            if (game_start(*q)) game = i;
            else if (option(*q)) opt = i;
            else if (copyright(*q)) copy = i;
        } else if (name == "logo.png" && item.geometry->positions.size() == 4) {
            logo = i;
        } else if (contains(name, "cursor.png")) {
            cursor_items.push_back(i);
        }
    }
    if (logo >= 0) items[std::size_t(logo)].model(3, 1) += kLogoLift * layout;
    if (copy >= 0) items[std::size_t(copy)].model(3, 1) -= kCopyrightDrop * layout;
    if (game < 0 || opt < 0) return false;

    const float game_y = items[std::size_t(game)].model(3, 1), option_y = items[std::size_t(opt)].model(3, 1);
    const float row = game_y - option_y; // 50 at rest
    if (row < 10 || row > 200) return false;
    const float lift = kMenuLift * layout;

    // The band (between the two rows, 12 vertices: the band and its two
    // lines) reaches a row further down; the arrows (8 vertices) slide a row
    // down with the cursor.
    for (const int i : cursor_items) {
        auto& item = items[std::size_t(i)];
        const float y = item.model(3, 1);
        const auto& g = *item.geometry;
        if (g.positions.size() == 12 && y < game_y && y > option_y) {
            if (band_.size() > 8 && !band_.contains(g.serial.value)) band_.clear(); // (stages loaded again)
            auto& band = band_[g.serial.value];
            if (!band) {
                band = std::make_shared<engine::GeometryData>(g);
                float y0 = g.positions[0].y, y1 = y0;
                for (const auto& p : g.positions) y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
                const float middle = (y0 + y1) / 2;
                // (At rest the band's matrix has no scale: a row is the same in its own units.)
                for (auto& p : band->positions)
                    if (p.y < middle) p.y -= row;
                band->bounds_min.y -= row;
            }
            item.geometry = band.get();
            item.model(3, 1) += lift;
        } else if (g.positions.size() == 8 && y < game_y + row / 2 && y > option_y - row / 2) {
            item.model(3, 1) += lift - row * arrows_;
        }
    }

    // "Online": glyph quads cut once from the "Option" item's texture.
    const auto& source = items[std::size_t(opt)];
    if (source.geometry != text_from_ || source.geometry->serial.value != text_serial_) {
        text_from_ = source.geometry;
        text_serial_ = source.geometry->serial.value;
        text_.reset();
        text_failed_ = false;
    }
    if (!text_ && !text_failed_) {
        text_failed_ = true;
        const auto option_quad = quad_of(*source.geometry);
        const auto game_quad = quad_of(*items[std::size_t(game)].geometry);
        if (option_quad && game_quad && source.state.texture && source.state.texture->image) {
            try {
                const auto image = decode_igb_texture(*source.state.texture->image.document(), source.state.texture->image.reference());
                if (image.width >= 16 && image.height >= 16 && image.rgba.size() >= std::size_t(image.width) * image.height * 4) {
                    const int W = int(image.width), H = int(image.height);
                    // Texel rectangles of the two quads (rows from the bottom
                    // edge's v: the picture's up is the texture's rows up).
                    auto columns = [&](const Quad& q, int& c0, int& c1) {
                        c0 = std::clamp(int(std::lround(lo(q.u_left, q.u_right) * W)), 0, W - 1);
                        c1 = std::clamp(int(std::lround(hi(q.u_left, q.u_right) * W)) - 1, 0, W - 1);
                    };
                    int oc0, oc1, gc0, gc1;
                    columns(*option_quad, oc0, oc1);
                    columns(*game_quad, gc0, gc1);
                    const bool rows_up = option_quad->v_top > option_quad->v_bottom;
                    const int r0 = std::clamp(int(std::lround(lo(option_quad->v_bottom, option_quad->v_top) * H)), 0, H - 1);
                    const int r1 = std::clamp(int(std::lround(hi(option_quad->v_bottom, option_quad->v_top) * H)) - 1, 0, H - 1);
                    const auto o_runs = ink_runs(image, oc0, oc1, r0, r1); // O p ti o n
                    const auto g_runs = ink_runs(image, gc0, gc1, r0, r1); // G a m e (Sta) (rt)
                    if (o_runs.size() == 5 && g_runs.size() >= 4) {
                        // The "ti" run splits at its weakest column.
                        const Run ti = o_runs[2];
                        int split = -1, weakest = 1 << 30;
                        for (int x = ti.c0 + 2; x <= ti.c1 - 2; ++x) {
                            int strong = 0;
                            for (int y = r0; y <= r1; ++y) strong += alpha(image, x, y) > 128;
                            if (strong < weakest) weakest = strong, split = x;
                        }
                        const Run i_run{split + 1, ti.c1};
                        // The i's rows (bottom up): the stem, the gap, the dot.
                        auto row_strong = [&](int y) {
                            for (int x = i_run.c0; x <= i_run.c1; ++x)
                                if (alpha(image, x, y) > 128) return true;
                            return false;
                        };
                        const int step = rows_up ? 1 : -1;
                        const int bottom_row = rows_up ? r0 : r1, top_row = rows_up ? r1 : r0;
                        int stem0 = -1, stem1 = -1, dot0 = -1, dot1 = -1;
                        for (int y = bottom_row; y != top_row + step; y += step) {
                            const bool s = row_strong(y);
                            if (stem0 < 0) {
                                if (s) stem0 = stem1 = y;
                            } else if (dot0 < 0) {
                                if (s && stem1 == y - step) stem1 = y;
                                else if (s) dot0 = dot1 = y;
                            } else if (s && dot1 == y - step) {
                                dot1 = y;
                            }
                        }
                        if (split > 0 && i_run.c1 - i_run.c0 >= 2 && stem0 >= 0 && dot0 >= 0) {
                            // Texel -> the "Option" quad's units.
                            const float kx = (option_quad->x1 - option_quad->x0) / (std::fabs(option_quad->u_right - option_quad->u_left) * W);
                            // A texture row's v (its lower edge) -> the quad's y.
                            auto y_of_v = [&](float v) {
                                const float t = (v - option_quad->v_bottom) / (option_quad->v_top - option_quad->v_bottom);
                                return option_quad->y0 + t * (option_quad->y1 - option_quad->y0);
                            };
                            auto v_edge = [&](int texel_row, bool upper) { // the edge of a row on the picture's lower / upper side
                                return float(texel_row + ((upper == rows_up) ? 1 : 0)) / float(H);
                            };
                            struct Piece {
                                float x0, x1, y0, y1;    // the quad's units
                                float u0, u1, vb, vt;    // texture
                            };
                            std::vector<Piece> pieces;
                            float cursor = 0;
                            // A whole-height glyph: its ink columns with one column of margin.
                            auto glyph = [&](Run r) {
                                const float u0 = float(r.c0 - 1) / W, u1 = float(r.c1 + 2) / W;
                                const float x = cursor - kx;
                                pieces.push_back({x, x + (u1 - u0) * W * kx, option_quad->y0, option_quad->y1, u0, u1, option_quad->v_bottom,
                                                  option_quad->v_top});
                                cursor += float(r.c1 - r.c0 + 1) * kx + 1.5f * kx;
                            };
                            // The l: the i's stem, drawn on up to the top of its dot
                            // (the stem's lower half, one inner row stretched, the
                            // dot's upper half).
                            auto ell = [&] {
                                const float u0 = float(i_run.c0 - 1) / W, u1 = float(i_run.c1 + 2) / W;
                                const float x = cursor - kx, x1 = x + (u1 - u0) * W * kx;
                                const int stem_mid = stem0 + (stem1 - stem0) / 2, dot_mid = dot0 + (dot1 - dot0) / 2;
                                const float vb = v_edge(stem0 - step, false), vs = (float(stem_mid) + 0.5f) / H;
                                const float vd = (float(dot_mid) + 0.5f) / H, vt = v_edge(dot1 + step, true);
                                pieces.push_back({x, x1, y_of_v(vb), y_of_v(vs), u0, u1, vb, vs});
                                pieces.push_back({x, x1, y_of_v(vs), y_of_v(vd), u0, u1, vs, vs});
                                pieces.push_back({x, x1, y_of_v(vd), y_of_v(vt), u0, u1, vd, vt});
                                cursor += float(i_run.c1 - i_run.c0 + 1) * kx + 1.5f * kx;
                            };
                            glyph(o_runs[0]); // O
                            glyph(o_runs[4]); // n
                            ell();            // l
                            glyph(i_run);     // i
                            glyph(o_runs[4]); // n
                            glyph(g_runs[3]); // e
                            const float width = cursor - 1.5f * kx;
                            // Centred like "Option" on its quad's centre.
                            const float shift = (option_quad->x0 + option_quad->x1) / 2 - width / 2;
                            auto text = std::make_shared<engine::GeometryData>();
                            const auto& src = *source.geometry;
                            text->weight_count = 0;
                            for (std::size_t p = 0; p < pieces.size(); ++p) {
                                const auto& piece = pieces[p];
                                for (std::size_t k = 0; k < 4; ++k) {
                                    // The source quad's vertex order and winding.
                                    const auto& sp = src.positions[k];
                                    const bool right = sp.x > (option_quad->x0 + option_quad->x1) / 2;
                                    const bool top = sp.y > (option_quad->y0 + option_quad->y1) / 2;
                                    text->positions.push_back({(right ? piece.x1 : piece.x0) + shift, top ? piece.y1 : piece.y0, sp.z});
                                    text->texcoords.push_back(right ? piece.u1 : piece.u0);
                                    text->texcoords.push_back(top ? piece.vt : piece.vb);
                                    if (k < src.normals.size()) text->normals.push_back(src.normals[k]);
                                    if (k < src.colors.size()) text->colors.push_back(src.colors[k]);
                                }
                                for (const auto index : src.indices) text->indices.push_back(index + std::uint32_t(4 * p));
                            }
                            text->bounds_min = {shift - kx, option_quad->y0, src.bounds_min.z};
                            text->bounds_max = {shift + width + kx, option_quad->y1, src.bounds_max.z};
                            for (const auto& p : text->positions) {
                                text->bounds_min.y = std::min(text->bounds_min.y, p.y);
                                text->bounds_max.y = std::max(text->bounds_max.y, p.y);
                            }
                            text->valid = true;
                            text_ = std::move(text);
                            text_failed_ = false;
                        }
                    }
                }
            } catch (const std::exception&) {
            }
        }
    }

    // Lift the two rows; the third goes in after "Option", a row lower.
    items[std::size_t(game)].model(3, 1) += lift;
    items[std::size_t(opt)].model(3, 1) += lift;
    if (!text_) return false;
    engine::DrawItem online = items[std::size_t(opt)];
    online.geometry = text_.get();
    online.model(3, 1) -= row;
    online.instance ^= 0x4f6e6c696e65ull; // "Online": its own placement for smooth motion
    items.insert(items.begin() + opt + 1, std::move(online));
    return true;
}

} // namespace dw4::game
