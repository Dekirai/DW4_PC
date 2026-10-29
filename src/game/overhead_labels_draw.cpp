// The overhead name tags' ImGui drawing (overhead_labels.hpp).
#include "overhead_labels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

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

} // namespace

void draw_overhead_labels(ImDrawList* list, const std::vector<OverheadLabel>& labels, const LabelView& view, LabelMode mode,
                          int players, float scale, bool numbers) {
    if (!list || mode == LabelMode::off) return;
    ImFont* font = ImGui::GetFont();
    const float text_size = 15.0f * scale;
    const float small_size = 12.0f * scale;
    const float bar_width = 70.0f * scale;
    const float hp_height = std::max(3.0f, 6.0f * scale), mp_height = std::max(2.0f, 4.0f * scale);
    for (const auto& label : labels) {
        if (label.hp <= 0 || !label_visible(mode, label.own, players)) continue;
        const auto point = project_label(label.head, view);
        if (!point) continue;
        const float alpha = label_fade(point->distance);
        if (alpha <= 0.01f) continue;
        // Bars right above the head point, the name above them.
        const float x = point->x - bar_width * 0.5f;
        float y = point->y - hp_height - mp_height - 2.0f * scale;
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
        // "Name  Lv 23", centred over the bars.
        char level[24];
        std::snprintf(level, sizeof level, "  Lv %d", label.level);
        const ImVec2 name_size = font->CalcTextSizeA(text_size, 1e9f, 0.0f, label.name.c_str());
        const ImVec2 level_size = font->CalcTextSizeA(small_size, 1e9f, 0.0f, level);
        const float total = name_size.x + level_size.x;
        y -= name_size.y + 2.0f * scale;
        const ImU32 name_fill = label.own ? colour(1.0f, 0.92f, 0.55f, alpha) : colour(1, 1, 1, alpha);
        outlined_text(list, font, text_size, ImVec2(point->x - total * 0.5f, y), name_fill, alpha, label.name.c_str());
        outlined_text(list, font, small_size, ImVec2(point->x - total * 0.5f + name_size.x, y + name_size.y - level_size.y), 
                      colour(0.8f, 0.9f, 1.0f, alpha), alpha, level);
    }
}

} // namespace dw4::game
