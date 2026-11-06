#pragma once

// SimUi (docs/SURVIVAL.md 9.1, 13.6): Data Overflow's screens as 2D items of
// the game's 640 x 480 layer, drawn by the game's own pipeline after the
// engine's picture (the TitleEntry technique): windows in the colours of the
// game's windows and text in the game font (bectext.igb's NORMAL font image
// and the engine's own `layout_text`). Presentation only: it reads the game
// data from a copy of the resources and never touches an engine.
//
// The layout uses the 2D layer's units with the origin top left and y down
// (the screens are easier to write that way); the items are made in the
// layer's own centred, y-up space. Widescreen keeps them 4:3 like the
// game's own layers (an orthographic projection: the renderer draws flat
// items in the 4:3 screen).
#include "dw4/engine/managers.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/resources.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

struct UiColor {
    float r = 1, g = 1, b = 1, a = 1;
};

// DW4's value colours (9.1) and the windows' own.
namespace ui_colors {
inline constexpr UiColor white{1, 1, 1, 1};
inline constexpr UiColor grey{0.55f, 0.55f, 0.6f, 1};
inline constexpr UiColor cyan{0.45f, 0.95f, 1, 1};
inline constexpr UiColor green{0.5f, 1, 0.45f, 1};
inline constexpr UiColor yellow{0.9f, 0.9f, 0.47f, 1}; // TALKFONT03's name tab
inline constexpr UiColor orange{1, 0.65f, 0.25f, 1};
inline constexpr UiColor magenta{1, 0.45f, 0.95f, 1};
inline constexpr UiColor purple{0.62f, 0.36f, 0.95f, 1}; // the HUD's line
inline constexpr UiColor red{1, 0.35f, 0.3f, 1};
// The talk / status windows: a deep blue body, darker at the bottom, and a
// light frame.
inline constexpr UiColor window_top{0.07f, 0.16f, 0.42f, 0.88f};
inline constexpr UiColor window_bottom{0.02f, 0.05f, 0.2f, 0.88f};
inline constexpr UiColor window_frame{0.62f, 0.78f, 1, 1};
inline constexpr UiColor cursor{0.3f, 0.55f, 1, 0.55f};
} // namespace ui_colors

class SimUi {
public:
    SimUi() = default;
    SimUi(const SimUi&) = delete;
    SimUi& operator=(const SimUi&) = delete;

    // The resources the font is read from (a copy; the first call wins).
    void set_resources(const MountedResources& resources);
    // The font is loaded (on first use; false if bectext.igb has none).
    bool ready();
    // The codes the game font has (T9 and the screens' check).
    const std::set<std::uint32_t>& font_codes();

    // A frame: begin, the pieces in drawing order, then `finish` appends
    // the items to the frame's list (the geometry lives until the next begin).
    void begin();
    void rect(float x, float y, float w, float h, UiColor color);
    void gradient(float x, float y, float w, float h, UiColor top, UiColor bottom);
    // A window: body, frame and (optional) a name tab at its top left.
    void window(float x, float y, float w, float h, float open = 1.0f);
    void name_tab(float x, float y, std::string_view name);
    // The selection bar of a list row.
    void cursor_bar(float x, float y, float w, float h);
    // A bar: a dark track and the filled part.
    void bar(float x, float y, float w, float h, float fraction, UiColor fill);
    // Text (UTF-8, `|` breaks a line): `size` the line height in layer units,
    // `align` 0 left, 1 right, 2 centred inside `width` from x.
    void text(float x, float y, std::string_view utf8, UiColor color = ui_colors::white, float size = 18.0f, int align = 0,
              float width = 0);
    float text_width(std::string_view utf8, float size = 18.0f);
    // The text with `|` breaks at spaces so no line is wider than `width`.
    std::string wrap(std::string_view utf8, float size, float width);
    // The ▼ prompt of a talk page (a small triangle, blinking).
    void prompt(float x, float y, std::uint64_t frame);
    void finish(std::vector<engine::DrawItem>& items, float alpha = 1.0f);
    // Items made by the last finish (tests, traces).
    std::size_t last_items() const { return last_items_; }

private:
    struct Piece {
        std::shared_ptr<engine::GeometryData> geometry;
        UiColor color;
        bool textured{};
    };
    bool load_font();
    engine::RenderState flat_state() const;

    std::unique_ptr<MountedResources> resources_;
    std::shared_ptr<const IgbDocument> font_document_;
    std::unique_ptr<engine::FontImage> font_;
    std::set<std::uint32_t> codes_;
    bool tried_ = false;
    std::vector<Piece> pieces_;
    std::vector<std::shared_ptr<engine::GeometryData>> drawn_; // the last frame's (the renderer reads them by address)
    std::size_t last_items_ = 0;
};

} // namespace dw4::game
