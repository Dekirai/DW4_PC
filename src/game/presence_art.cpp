#include "presence_art.hpp"

#include "presence_data.hpp"

#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/packages.hpp"
#include "dw4/resources.hpp"
#include "dw4/textures.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>

namespace dw4::game {
namespace {

using engine::DrawItem;
using engine::Mat4;
using engine::Vec3f;

constexpr int kSize = 512;

// The picture of a loading card as the loading screen shows it: the card's
// one texture (loadingUSA/ld*.igb, ldimg01.png) holds the stage picture and,
// below it, the strip of name and "Now Loading" sprites the other shapes
// cut out of it. The picture is the quad M_imageShape: its corners' texture
// coordinates name the picture's part of the texture, its corners on the
// 4:3 screen the shape it is shown with.
struct CardPicture {
    std::uint32_t texture{}; // igImage reference in the card
    // Texture coordinates of the corners as seen on screen: top left, top
    // right, bottom left, bottom right.
    std::array<std::array<float, 2>, 4> uv{};
    float aspect = 4.0f / 3.0f; // width / height on the 4:3 screen
    float texels_wide{}, texels_high{};
};

// Applies the draw's model and projection to a position: x, y in normalised
// device coordinates.
std::array<float, 2> to_screen(const DrawItem& item, const Vec3f& p) {
    const Vec3f world = engine::transform_point(p, item.model);
    if (!item.state.projection) return {world.x, world.y};
    const Mat4& m = *item.state.projection;
    const float x = world.x * m(0, 0) + world.y * m(1, 0) + world.z * m(2, 0) + m(3, 0);
    const float y = world.x * m(0, 1) + world.y * m(1, 1) + world.z * m(2, 1) + m(3, 1);
    const float w = world.x * m(0, 3) + world.y * m(1, 3) + world.z * m(2, 3) + m(3, 3);
    return {x / (w != 0 ? w : 1.0f), y / (w != 0 ? w : 1.0f)};
}

// The card scene's draws once its intro has played: the beLoadingNode clock
// moves at most two frames per visit, so the scene is visited frame by frame.
std::vector<DrawItem> settled_draws(const IgbDocument& document, engine::SceneCache& cache) {
    std::vector<DrawItem> items;
    if (!document.info_list_reference) return items;
    engine::Node* root = nullptr;
    for (const auto& element : document.object_list(*document.info_list_reference)) {
        if (!element) continue;
        const engine::ObjectView info(&document, *element);
        if (info.type() == "igSceneInfo")
            if (const auto scene = info.ref(5)) root = cache.node(scene);
    }
    if (!root) return items;
    constexpr std::int64_t kFrame = 16'683'333; // one 59.94 Hz field in nanoseconds
    for (int frame = 0; frame <= 180; ++frame) {
        items.clear();
        engine::TraversalContext context;
        context.cache = &cache;
        context.time = std::int64_t(frame) * kFrame;
        context.output = &items;
        engine::traverse(root, context);
    }
    return items;
}

std::optional<CardPicture> find_card_picture(const IgbDocument& document, std::string& why) {
    engine::SceneCache cache;
    const auto items = settled_draws(document, cache);
    const DrawItem* picture = nullptr;
    for (const auto& item : items)
        if (item.node && item.node->name == "M_imageShape" && item.geometry && item.geometry->positions.size() == 4 &&
            item.geometry->texcoords.size() == 8 && item.state.texture && item.state.texture->image) {
            picture = &item;
            break;
        }
    if (!picture) {
        why = "no picture quad (M_imageShape) among " + std::to_string(items.size()) + " shapes";
        return std::nullopt;
    }
    const auto& geometry = *picture->geometry;
    std::array<std::array<float, 2>, 4> screen{};
    for (int i = 0; i < 4; ++i) screen[std::size_t(i)] = to_screen(*picture, geometry.positions[std::size_t(i)]);
    float left = screen[0][0], right = left, bottom = screen[0][1], top = bottom;
    for (const auto& s : screen) {
        left = std::min(left, s[0]);
        right = std::max(right, s[0]);
        bottom = std::min(bottom, s[1]);
        top = std::max(top, s[1]);
    }
    if (right - left <= 0 || top - bottom <= 0) {
        why = "the picture quad has no area on screen";
        return std::nullopt;
    }
    CardPicture card;
    card.texture = picture->state.texture->image.reference();
    // The corners by their place on screen (y up).
    const float mid_x = (left + right) * 0.5f, mid_y = (bottom + top) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const auto& s = screen[std::size_t(i)];
        const int corner = (s[1] >= mid_y ? 0 : 2) + (s[0] >= mid_x ? 1 : 0);
        card.uv[std::size_t(corner)] = {geometry.texcoords[std::size_t(i) * 2], geometry.texcoords[std::size_t(i) * 2 + 1]};
    }
    // Normalised device coordinates span the 4:3 picture.
    card.aspect = ((right - left) * 4.0f) / ((top - bottom) * 3.0f);
    return card;
}

// Bilinear sample of straight RGBA at texture coordinates (u, v); v counts
// from the texture's first stored row, as the renderer uploads it.
std::array<float, 4> sample(const RgbaImage& image, float u, float v) {
    const int w = int(image.width), h = int(image.height);
    const float sx = u * float(w) - 0.5f, sy = v * float(h) - 0.5f;
    const int x0 = std::clamp(int(std::floor(sx)), 0, w - 1), x1 = std::clamp(x0 + 1, 0, w - 1);
    const int y0 = std::clamp(int(std::floor(sy)), 0, h - 1), y1 = std::clamp(y0 + 1, 0, h - 1);
    const float fx = std::clamp(sx - std::floor(sx), 0.0f, 1.0f), fy = std::clamp(sy - std::floor(sy), 0.0f, 1.0f);
    std::array<float, 4> out{};
    for (int c = 0; c < 4; ++c) {
        auto at = [&](int x, int y) { return float(image.rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + std::size_t(c)]); };
        const float a = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
        const float b = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
        out[std::size_t(c)] = a * (1 - fy) + b * fy;
    }
    return out;
}

// Area-averaged sample of the picture over one output pixel: `ss` x `ss`
// bilinear taps, so a picture shown smaller than its texels stays smooth.
std::vector<std::uint8_t> render_square(const RgbaImage& image, const CardPicture& card) {
    // The centred square of the picture as shown: the shorter side whole.
    float x0 = 0, x1 = 1, y0 = 0, y1 = 1; // fractions of the shown picture, y down
    if (card.aspect > 1) {
        const float keep = 1.0f / card.aspect;
        x0 = (1 - keep) * 0.5f;
        x1 = x0 + keep;
    } else if (card.aspect < 1) {
        const float keep = card.aspect;
        y0 = (1 - keep) * 0.5f;
        y1 = y0 + keep;
    }
    // Half a texel inside the picture's edges, so no neighbouring sprite or
    // border texel bleeds into the edge pixels.
    const float inset_u = 0.5f / float(image.width), inset_v = 0.5f / float(image.height);
    const auto& tl = card.uv[0];
    const auto& tr = card.uv[1];
    const auto& bl = card.uv[2];
    const auto& br = card.uv[3];
    auto clamp_inside = [&](float value, float a, float b, float inset) {
        const float lo = std::min(a, b) + inset, hi = std::max(a, b) - inset;
        return lo < hi ? std::clamp(value, lo, hi) : value;
    };
    const float u_lo = std::min({tl[0], tr[0], bl[0], br[0]}), u_hi = std::max({tl[0], tr[0], bl[0], br[0]});
    const float v_lo = std::min({tl[1], tr[1], bl[1], br[1]}), v_hi = std::max({tl[1], tr[1], bl[1], br[1]});
    constexpr int ss = 4;
    std::vector<std::uint8_t> out(std::size_t(kSize) * kSize * 4);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            std::array<float, 4> sum{};
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    const float fx = x0 + (x1 - x0) * ((float(x) + (float(i) + 0.5f) / ss) / kSize);
                    const float fy = y0 + (y1 - y0) * ((float(y) + (float(j) + 0.5f) / ss) / kSize);
                    // Bilinear across the quad's corners.
                    float u = (tl[0] * (1 - fx) + tr[0] * fx) * (1 - fy) + (bl[0] * (1 - fx) + br[0] * fx) * fy;
                    float v = (tl[1] * (1 - fx) + tr[1] * fx) * (1 - fy) + (bl[1] * (1 - fx) + br[1] * fx) * fy;
                    u = clamp_inside(u, u_lo, u_hi, inset_u);
                    v = clamp_inside(v, v_lo, v_hi, inset_v);
                    const auto s = sample(image, u, v);
                    for (int c = 0; c < 4; ++c) sum[std::size_t(c)] += s[std::size_t(c)];
                }
            for (int c = 0; c < 4; ++c)
                out[(std::size_t(y) * kSize + std::size_t(x)) * 4 + std::size_t(c)] =
                    std::uint8_t(std::clamp(std::lround(sum[std::size_t(c)] / (ss * ss)), 0L, 255L));
            out[(std::size_t(y) * kSize + std::size_t(x)) * 4 + 3] = 255; // the picture is opaque on screen
        }
    return out;
}

// The title logo (logo.png of the title's j_startlogo model in the USA
// pack of stagIS10, stored bottom row first) on a transparent square, its
// own shape kept.
std::optional<std::vector<std::uint8_t>> render_logo(const MountedResources& resources, std::string& why) {
    try {
        std::vector<IgbDocument> documents;
        if (const auto* pack = resources.catalog().find("stagis10/separate/usa_pack.igb"); pack && pack->location)
            documents.push_back(read_igb_document(resources.read(pack->logical_path)));
        const auto descriptor = load_package_descriptor(resources, "stagis10.igb");
        for (const auto& member : descriptor.members)
            if (member.location) documents.push_back(read_package_member(resources, member));
        for (const auto& document : documents) {
            for (const auto& texture : list_igb_textures(document)) {
                if (texture.name != "logo.png" || !texture.supported) continue;
                const auto image = decode_igb_texture(document, texture.reference_index);
                // The logo's own bounds (alpha above a trace), a margin around it.
                int lo_x = int(image.width), hi_x = -1, lo_y = int(image.height), hi_y = -1;
                for (int y = 0; y < int(image.height); ++y)
                    for (int x = 0; x < int(image.width); ++x)
                        if (image.rgba[(std::size_t(y) * image.width + std::size_t(x)) * 4 + 3] > 8) {
                            lo_x = std::min(lo_x, x), hi_x = std::max(hi_x, x);
                            lo_y = std::min(lo_y, y), hi_y = std::max(hi_y, y);
                        }
                if (hi_x < lo_x || hi_y < lo_y) break;
                const float w = float(hi_x - lo_x + 1), h = float(hi_y - lo_y + 1);
                const float scale = (kSize * 0.9f) / std::max(w, h);
                const float ox = (kSize - w * scale) * 0.5f, oy = (kSize - h * scale) * 0.5f;
                std::vector<std::uint8_t> out(std::size_t(kSize) * kSize * 4, 0);
                constexpr int ss = 4;
                for (int y = 0; y < kSize; ++y)
                    for (int x = 0; x < kSize; ++x) {
                        float sum[4]{};
                        for (int j = 0; j < ss; ++j)
                            for (int i = 0; i < ss; ++i) {
                                const float px = (float(x) + (float(i) + 0.5f) / ss - ox) / scale;
                                const float py = (float(y) + (float(j) + 0.5f) / ss - oy) / scale;
                                if (px < 0 || py < 0 || px >= w || py >= h) continue;
                                // Upright: the last stored row is the top.
                                const float u = (float(lo_x) + px) / float(image.width);
                                const float v = (float(hi_y) + 1.0f - py) / float(image.height);
                                const auto s = sample(image, u, v);
                                // Premultiplied while averaging, so the transparent
                                // texels' colour does not fringe the edges.
                                for (int c = 0; c < 3; ++c) sum[c] += s[std::size_t(c)] * s[3] / 255.0f;
                                sum[3] += s[3];
                            }
                        const std::size_t at = (std::size_t(y) * kSize + std::size_t(x)) * 4;
                        const float alpha = sum[3] / (ss * ss);
                        for (int c = 0; c < 3; ++c)
                            out[at + std::size_t(c)] = std::uint8_t(
                                alpha > 0 ? std::clamp(std::lround(sum[c] / (ss * ss) * 255.0f / alpha), 0L, 255L) : 0L);
                        out[at + 3] = std::uint8_t(std::clamp(std::lround(alpha), 0L, 255L));
                    }
                return out;
            }
        }
        why = "no logo.png in stagIS10's files";
    } catch (const std::exception& problem) {
        why = problem.what();
    }
    return std::nullopt;
}

bool write_png(const std::filesystem::path& file, const std::vector<std::uint8_t>& rgba) {
    return stbi_write_png(file.string().c_str(), kSize, kSize, 4, rgba.data(), kSize * 4) != 0;
}

std::string describe(const CardPicture& card, const RgbaImage& image) {
    const float du = std::abs(card.uv[1][0] - card.uv[0][0]), dv = std::abs(card.uv[2][1] - card.uv[0][1]);
    std::ostringstream text;
    text.precision(3);
    text << "picture " << std::lround(du * float(image.width)) << "x" << std::lround(dv * float(image.height)) << " of "
         << image.width << "x" << image.height << " texels, shown " << card.aspect << ":1";
    return text.str();
}

} // namespace

int export_presence_art(const std::filesystem::path& data, const std::filesystem::path& folder) {
    const auto resources = MountedResources::open(data);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    int written = 0;
    for (const auto& image : area_images()) {
        const auto file = folder / (std::string(image.key) + ".png");
        if (std::string_view(image.key) == kLogoImageKey) {
            std::string why;
            if (const auto logo = render_logo(resources, why)) {
                if (write_png(file, *logo)) {
                    std::cout << "wrote " << file.string() << " (" << image.title << ", the title logo)\n";
                    ++written;
                }
                continue;
            }
            std::cerr << image.key << ": " << why << "; using the card " << image.card << '\n';
        }
        const std::string path = std::string("loadingusa/") + image.card + ".igb";
        try {
            const auto document = read_igb_document(resources.read(path));
            std::string why;
            const auto card = find_card_picture(document, why);
            if (!card) {
                std::cerr << image.key << ": " << path << ": " << why << '\n';
                continue;
            }
            const auto rgba = decode_igb_texture(document, card->texture);
            if (write_png(file, render_square(rgba, *card))) {
                std::cout << "wrote " << file.string() << " (" << image.title << ", from " << path << ": " << describe(*card, rgba)
                          << ")\n";
                ++written;
            }
        } catch (const std::exception& problem) {
            std::cerr << image.key << ": " << problem.what() << '\n';
        }
    }
    // Test aid: DW4_PRESENCE_ART_ALL=1 also writes every loading card's
    // picture to <folder>/cards (choosing an area's card).
    if (const char* all = std::getenv("DW4_PRESENCE_ART_ALL"); all && *all && *all != '0') {
        std::filesystem::create_directories(folder / "cards", error);
        for (const auto& record : resources.catalog().resources) {
            if (!record.location) continue;
            const std::string& path = record.logical_path;
            if (path.rfind("loadingusa/", 0) != 0 || path.size() < 16 || path.substr(path.size() - 4) != ".igb") continue;
            try {
                const auto document = read_igb_document(resources.read(path));
                std::string why;
                const auto card = find_card_picture(document, why);
                const std::string name = path.substr(11, path.size() - 15);
                if (!card) {
                    std::cout << "card " << name << ": " << why << '\n';
                    continue;
                }
                const auto rgba = decode_igb_texture(document, card->texture);
                write_png(folder / "cards" / (name + ".png"), render_square(rgba, *card));
                std::cout << "card " << name << ": " << describe(*card, rgba) << '\n';
            } catch (const std::exception& problem) {
                std::cout << "card " << path << ": " << problem.what() << '\n';
            }
        }
    }
    std::cout << written << " pictures in " << folder.string()
              << ". Upload them in the Discord Developer Portal (your application > Rich Presence > Art Assets) "
                 "under their file names; the form pictures (form_*) are yours to choose.\n";
    return written > 0 ? 0 : 1;
}

} // namespace dw4::game
