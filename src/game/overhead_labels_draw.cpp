// The overhead name tags' ImGui drawing (overhead_labels.hpp).
#include "overhead_labels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace dw4::game {
namespace {

ImU32 colour(float r, float g, float b, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, std::clamp(a, 0.0f, 1.0f)));
}

// Text with a one-pixel dark outline, readable on any background.
void outlined_text(ImDrawList* list, ImFont* font, float size, ImVec2 at, ImU32 fill, float alpha, const char* text) {
    const ImU32 shadow = colour(0, 0, 0, 0.8f * alpha);
    for (const ImVec2 d : {ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1)})
        list->AddText(font, size, ImVec2(at.x + d.x, at.y + d.y), shadow, text);
    list->AddText(font, size, at, fill, text);
}

void bar(ImDrawList* list, ImVec2 at, float width, float height, float ratio, ImU32 fill, float alpha) {
    list->AddRectFilled(at, ImVec2(at.x + width, at.y + height), colour(0.05f, 0.05f, 0.08f, 0.75f * alpha), height * 0.4f);
    if (ratio > 0.0f)
        list->AddRectFilled(at, ImVec2(at.x + width * std::clamp(ratio, 0.0f, 1.0f), at.y + height), fill, height * 0.4f);
    list->AddRect(at, ImVec2(at.x + width, at.y + height), colour(0, 0, 0, 0.9f * alpha), height * 0.4f);
}

// The HUD rings' colours of the four local players.
ImVec4 slot_colour(int slot) {
    switch (slot) {
    case 0: return ImVec4(1.0f, 0.38f, 0.33f, 1);
    case 1: return ImVec4(0.40f, 0.62f, 1.0f, 1);
    case 2: return ImVec4(1.0f, 0.86f, 0.30f, 1);
    case 3: return ImVec4(0.40f, 0.92f, 0.45f, 1);
    default: return ImVec4(1, 1, 1, 1);
    }
}

} // namespace

void draw_overhead_labels(ImDrawList* list, const std::vector<OverheadLabel>& labels, const LabelView& view, LabelMode mode,
                          int players, float scale, bool numbers, ImFont* font) {
    if (!list || mode == LabelMode::off) return;
    if (!font) font = ImGui::GetFont();
    const float text_size = 17.0f * scale;
    const float small_size = 13.0f * scale;
    const float bar_width = 70.0f * scale;
    const float hp_height = std::max(3.0f, 6.0f * scale), mp_height = std::max(2.0f, 4.0f * scale);
    const float stripe = std::max(3.0f, 4.0f * scale); // the player colour's mark before the name
    auto texts = [&](const OverheadLabel& label, char (&level)[24]) {
        std::snprintf(level, sizeof level, "  Lv %d", label.level);
        return std::make_pair(font->CalcTextSizeA(text_size, 1e9f, 0.0f, label.name.c_str()), font->CalcTextSizeA(small_size, 1e9f, 0.0f, level));
    };
    // The box: the name line over the two bars (the numbers beside them
    // stick out to the right and are not counted).
    const auto boxes = layout_overhead_labels(labels, view, mode, players, [&](const OverheadLabel& label) {
        char level[24];
        const auto [name_size, level_size] = texts(label, level);
        const float name_width = name_size.x + level_size.x + (label.slot >= 0 ? stripe + 3.0f * scale : 0.0f);
        return std::make_pair(std::max(bar_width, name_width), name_size.y + 2.0f * scale + hp_height + 1.0f + mp_height);
    });
    // Farthest first, so the nearer tags are drawn over the farther ones.
    std::vector<std::size_t> order(labels.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return boxes[a].y < boxes[b].y; });
    for (const std::size_t i : order) {
        const auto& box = boxes[i];
        if (!box.shown) continue;
        const auto& label = labels[i];
        const float alpha = box.alpha;
        const float centre = box.x + box.width * 0.5f;
        // A tag moved up off its player: a thin line down to the head.
        const float bottom = box.y + box.height;
        if (box.anchor_y - bottom > 4.0f * scale)
            list->AddLine(ImVec2(centre, bottom + 1.0f), ImVec2(box.anchor_x, box.anchor_y), colour(1, 1, 1, 0.45f * alpha), std::max(1.0f, scale));
        const float x = centre - bar_width * 0.5f;
        float y = bottom - hp_height - 1.0f - mp_height;
        const float hp_ratio = label.max_hp > 0 ? float(label.hp) / float(label.max_hp) : 0.0f;
        const float mp_ratio = label.max_mp > 0 ? float(label.mp) / float(label.max_mp) : 0.0f;
        // HP green, yellow below half, red below a quarter.
        const ImU32 hp_fill = hp_ratio > 0.5f ? colour(0.30f, 0.90f, 0.35f, alpha)
                              : hp_ratio > 0.25f ? colour(0.95f, 0.85f, 0.25f, alpha)
                                                 : colour(0.95f, 0.30f, 0.25f, alpha);
        bar(list, ImVec2(x, y), bar_width, hp_height, hp_ratio, hp_fill, alpha);
        bar(list, ImVec2(x, y + hp_height + 1.0f), bar_width, mp_height, mp_ratio, colour(0.30f, 0.55f, 1.0f, alpha), alpha);
        if (numbers) {
            char text[48];
            std::snprintf(text, sizeof text, "%d/%d", label.hp, label.max_hp);
            outlined_text(list, font, small_size, ImVec2(x + bar_width + 4.0f * scale, y - 3.0f * scale), colour(1, 1, 1, alpha), alpha,
                          text);
            std::snprintf(text, sizeof text, "%d/%d", label.mp, label.max_mp);
            outlined_text(list, font, small_size, ImVec2(x + bar_width + 4.0f * scale, y + hp_height - 2.0f * scale),
                          colour(0.75f, 0.85f, 1.0f, alpha), alpha, text);
        }
        // "Name  Lv 23", centred over the bars, a local player's colour mark first.
        char level[24];
        const auto [name_size, level_size] = texts(label, level);
        const float mark = label.slot >= 0 ? stripe + 3.0f * scale : 0.0f;
        const float total = mark + name_size.x + level_size.x;
        y = box.y;
        float tx = centre - total * 0.5f;
        if (label.slot >= 0) {
            ImVec4 c = slot_colour(label.slot);
            c.w = alpha;
            const float top = y + name_size.y * 0.2f, low = y + name_size.y * 0.85f;
            list->AddRectFilled(ImVec2(tx - 1.0f, top - 1.0f), ImVec2(tx + stripe + 1.0f, low + 1.0f), colour(0, 0, 0, 0.8f * alpha), 1.0f);
            list->AddRectFilled(ImVec2(tx, top), ImVec2(tx + stripe, low), ImGui::ColorConvertFloat4ToU32(c), 1.0f);
            tx += mark;
        }
        const ImU32 name_fill = label.own ? colour(1.0f, 0.92f, 0.55f, alpha) : colour(1, 1, 1, alpha);
        outlined_text(list, font, text_size, ImVec2(tx, y), name_fill, alpha, label.name.c_str());
        outlined_text(list, font, small_size, ImVec2(tx + name_size.x, y + name_size.y - level_size.y), colour(0.8f, 0.9f, 1.0f, alpha), alpha,
                      level);
    }
}

} // namespace dw4::game
