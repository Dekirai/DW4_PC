#include "button_prompts.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace dw4::game {
namespace {

ButtonPrompts* g_active = nullptr;

// The text a token stands for without pictures (logs, no window).
const char* plain_name(PromptControl control) {
    switch (control) {
    case PromptControl::confirm: return "A";
    case PromptControl::back: return "B";
    case PromptControl::erase: return "X";
    case PromptControl::option: return "Y";
    case PromptControl::menu: return "View";
    case PromptControl::stick: return "Stick";
    case PromptControl::c_stick: return "C-stick";
    case PromptControl::dpad: return "D-pad";
    default: return prompt_control_token(control);
    }
}

struct Segment {
    std::string_view text;            // a run of text (words with their spaces)
    std::optional<PromptControl> icon; // or a picture
};

// The text cut into text runs and tokens; `words`: runs end after each space
// (for wrapping).
std::vector<Segment> segments(std::string_view text, bool words) {
    std::vector<Segment> out;
    std::size_t run = 0;
    auto flush = [&](std::size_t end) {
        if (end <= run) return;
        const std::string_view part = text.substr(run, end - run);
        if (!words) {
            out.push_back({part, std::nullopt});
            return;
        }
        std::size_t start = 0;
        for (std::size_t i = 0; i < part.size(); ++i)
            if (part[i] == ' ' && (i + 1 >= part.size() || part[i + 1] != ' ')) {
                out.push_back({part.substr(start, i + 1 - start), std::nullopt});
                start = i + 1;
            }
        if (start < part.size()) out.push_back({part.substr(start), std::nullopt});
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        const auto close = text.find('}', i);
        if (close == std::string_view::npos) break;
        const auto control = prompt_control_from_token(text.substr(i + 1, close - i - 1));
        if (!control) continue;
        flush(i);
        out.push_back({{}, control});
        run = close + 1;
        i = close;
    }
    flush(text.size());
    return out;
}

ImTextureID texture_id(unsigned texture) { return ImTextureID(static_cast<std::uintptr_t>(texture)); }

void draw_icon(ImDrawList* draw, PromptControl control, float x, float y, float size, ImU32 tint) {
    auto* prompts = ButtonPrompts::active_prompts();
    if (!prompts || !prompts->ready()) return;
    const auto uv = prompts->icon_uv(control);
    draw->AddImage(ImTextureRef(texture_id(prompts->texture(prompts->family()))), ImVec2(x, y), ImVec2(x + size, y + size), ImVec2(uv[0], uv[1]),
                   ImVec2(uv[2], uv[3]), tint);
}

bool pictures() {
    auto* prompts = ButtonPrompts::active_prompts();
    return prompts && prompts->ready();
}

float icon_size() { return std::round(ImGui::GetFontSize() * 1.35f); }

} // namespace

ButtonPrompts::~ButtonPrompts() {
    for (auto& texture : textures_)
        if (texture) glDeleteTextures(1, &texture);
    if (g_active == this) g_active = nullptr;
}

ButtonPrompts* ButtonPrompts::active_prompts() { return g_active; }
void ButtonPrompts::set_active(ButtonPrompts* prompts) { g_active = prompts; }

void ButtonPrompts::update(const Settings& settings) {
    settings_ = &settings;
    cache_.set_settings(settings);
}

void ButtonPrompts::set_families(const std::array<PadFamily, 4>& families) {
    families_ = families;
    if (!settings_) return;
    using GenerateMipmap = void(APIENTRY*)(GLenum);
    static const auto generate_mipmap = reinterpret_cast<GenerateMipmap>(SDL_GL_GetProcAddress("glGenerateMipmap"));
    for (const PadFamily family : families_) {
        const auto f = std::size_t(family);
        // Drawn for these bindings and uploaded: nothing to do (the usual frame).
        if (cache_.current(family) && uploaded_[f] == cache_.generation(family)) continue;
        const PromptAtlas& atlas = cache_.atlas(family);
        if (!textures_[f]) glGenTextures(1, &textures_[f]);
        glBindTexture(GL_TEXTURE_2D, textures_[f]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas.image.width, atlas.image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.image.rgba.data());
        if (generate_mipmap) generate_mipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, generate_mipmap ? 4 : 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generate_mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        uploaded_[f] = cache_.generation(family);
        cells_[f] = atlas;
        cells_[f].image = {}; // (only the cells are kept)
        cache_.drop_image(family);
    }
}

namespace {
// The atlas's size in texels (the same for every family).
std::array<float, 2> atlas_size() {
    const int w = PromptAtlas::kColumns * (PromptAtlas::kCellWidth + PromptAtlas::kGutter);
    const int h = ((int(kPromptControlCount) + PromptAtlas::kColumns - 1) / PromptAtlas::kColumns + 1) *
                  (PromptAtlas::kCellHeight + PromptAtlas::kGutter);
    return {float(w), float(h)};
}
std::array<float, 4> uv_of(const PromptAtlas::Cell& cell) {
    const auto [w, h] = atlas_size();
    return {float(cell.x) / w, float(cell.y) / h, float(cell.x + cell.width) / w, float(cell.y + cell.height) / h};
}
} // namespace

std::array<float, 4> ButtonPrompts::icon_uv(PromptControl control) const { return uv_of(cells_[std::size_t(families_[0])].icon(control)); }

bool ButtonPrompts::active() const {
    for (int player = 0; player < 4; ++player)
        if (atlas_texture(player)) return true;
    return false;
}

unsigned ButtonPrompts::atlas_texture(int player) const {
    if (!settings_) return 0;
    const PadFamily family = families_[std::size_t(player & 3)];
    // A GameCube pad on its original layout: the game's own pictures.
    if (family == PadFamily::gamecube && settings_->gamecube_layout == GameCubeLayout::original) return 0;
    return textures_[std::size_t(family)];
}

std::array<float, 4> ButtonPrompts::atlas_cell(int control) const {
    if (control < 0 || control >= int(kPromptControlCount)) return {0, 0, 0, 0};
    // (Every family's atlas has the same cells.)
    return uv_of(cells_[std::size_t(families_[0])].cells[std::size_t(control)]);
}

PromptSource::Word ButtonPrompts::atlas_word(int player) const {
    const auto& atlas = cells_[std::size_t(families_[std::size_t(player & 3)])];
    return {uv_of(atlas.start_word_ink), atlas.word_aspect()};
}

std::uint64_t ButtonPrompts::art_key(int player) const {
    const PadFamily family = families_[std::size_t(player & 3)];
    return (uploaded_[std::size_t(family)] << 8) | std::uint64_t(family);
}

PromptSource::Line ButtonPrompts::line_info(const engine::GeometryData* geometry) const {
    return line_lookup_ ? line_lookup_(geometry) : Line{};
}

bool ButtonPrompts::title_variant(const std::vector<std::uint8_t>& rgba, int width, int height, bool nearest, std::vector<std::uint8_t>& out,
                                  int& out_width, int& out_height) const {
    if (!settings_ || atlas_texture(0) == 0) return false;
    ArtImage original(width, height);
    if (rgba.size() != original.rgba.size()) return false;
    original.rgba = rgba;
    ArtImage variant;
    // (2048 texels wide: sharp up to 4K.)
    if (!title_press_start(original, 2048, nearest, families_[0], *settings_, variant)) return false;
    out = std::move(variant.rgba);
    out_width = variant.width;
    out_height = variant.height;
    return true;
}

void prompt_text(std::string_view text, bool wrap, std::uint32_t color) {
    const ImU32 ink = color ? ImU32(color) : ImGui::GetColorU32(ImGuiCol_Text);
    if (!pictures()) {
        const std::string plain = prompt_plain(text);
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
        if (wrap) ImGui::TextWrapped("%s", plain.c_str());
        else ImGui::TextUnformatted(plain.c_str());
        ImGui::PopStyleColor();
        return;
    }
    const auto parts = segments(text, wrap);
    const float font = ImGui::GetFontSize(), icon = icon_size();
    const float line = std::max(icon, ImGui::GetTextLineHeight());
    const float gap = std::round(font * 0.2f);
    const float limit = wrap ? std::max(ImGui::GetContentRegionAvail().x, icon * 2) : FLT_MAX;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 tint = ImGui::GetColorU32(ImVec4(1, 1, 1, ImGui::GetStyle().Alpha));
    float x = 0, y = 0, widest = 0;
    for (const auto& part : parts) {
        const float w = part.icon ? icon + gap * 2 : ImGui::CalcTextSize(part.text.data(), part.text.data() + part.text.size()).x;
        if (wrap && x > 0 && x + w > limit) {
            x = 0;
            y += line + ImGui::GetStyle().ItemSpacing.y * 0.5f;
        }
        if (part.icon) draw_icon(draw, *part.icon, at.x + x + gap, at.y + y + (line - icon) * 0.5f, icon, tint);
        else draw->AddText(ImVec2(at.x + x, at.y + y + (line - ImGui::GetTextLineHeight()) * 0.5f), ink, part.text.data(),
                           part.text.data() + part.text.size());
        x += w;
        widest = std::max(widest, x);
    }
    ImGui::Dummy(ImVec2(widest, y + line));
}

void prompt_text_disabled(std::string_view text, bool wrap) { prompt_text(text, wrap, ImGui::GetColorU32(ImGuiCol_TextDisabled)); }

float prompt_text_width(std::string_view text) {
    if (!pictures()) {
        const std::string plain = prompt_plain(text);
        return ImGui::CalcTextSize(plain.c_str()).x;
    }
    const float icon = icon_size(), gap = std::round(ImGui::GetFontSize() * 0.2f);
    float width = 0;
    for (const auto& part : segments(text, false))
        width += part.icon ? icon + gap * 2 : ImGui::CalcTextSize(part.text.data(), part.text.data() + part.text.size()).x;
    return width;
}

std::string prompt_plain(std::string_view text) {
    std::string out;
    for (const auto& part : segments(text, false)) out += part.icon ? std::string(plain_name(*part.icon)) : std::string(part.text);
    return out;
}

void prompt_icon(PromptControl control, float size) {
    if (!pictures()) {
        ImGui::TextUnformatted(plain_name(control));
        return;
    }
    const ImVec2 at = ImGui::GetCursorScreenPos();
    draw_icon(ImGui::GetWindowDrawList(), control, at.x, at.y, size, ImGui::GetColorU32(ImVec4(1, 1, 1, ImGui::GetStyle().Alpha)));
    ImGui::Dummy(ImVec2(size, size));
}

void prompt_icon(ImDrawList* draw, PromptControl control, float x, float y, float size) {
    draw_icon(draw, control, x, y, size, IM_COL32_WHITE);
}

} // namespace dw4::game
