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

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

// The text a token stands for without pictures (logs, no window).
const char* plain_name(PromptControl control) {
    switch (control) {
    case PromptControl::confirm: return "A";
    case PromptControl::back: return "B";
    case PromptControl::erase: return "X";
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
    auto* prompts = ButtonPrompts::active();
    if (!prompts || !prompts->ready()) return;
    const auto uv = prompts->icon_uv(control);
    draw->AddImage(ImTextureRef(texture_id(prompts->texture(prompts->family()))), ImVec2(x, y), ImVec2(x + size, y + size), ImVec2(uv[0], uv[1]),
                   ImVec2(uv[2], uv[3]), tint);
}

bool pictures() {
    auto* prompts = ButtonPrompts::active();
    return prompts && prompts->ready();
}

float icon_size() { return std::round(ImGui::GetFontSize() * 1.35f); }

} // namespace

ButtonPrompts::~ButtonPrompts() {
    for (auto& texture : textures_)
        if (texture) glDeleteTextures(1, &texture);
    if (g_active == this) g_active = nullptr;
}

ButtonPrompts* ButtonPrompts::active() { return g_active; }
void ButtonPrompts::set_active(ButtonPrompts* prompts) { g_active = prompts; }

std::uint64_t ButtonPrompts::signature(const Settings& settings) const {
    std::uint64_t hash = 0x5eed;
    for (const auto& inputs : settings.pad)
        for (const auto& input : inputs) hash = mix(hash, (std::uint64_t(input.kind) << 32) | std::uint32_t(input.code));
    for (const auto& keys : settings.keys)
        for (const auto code : keys) hash = mix(hash, std::uint64_t(code));
    hash = mix(hash, std::uint64_t(settings.nintendo_layout));
    hash = mix(hash, std::uint64_t(settings.gamecube_layout));
    return hash;
}

void ButtonPrompts::update(const Settings& settings) {
    settings_ = &settings;
    const std::uint64_t now = signature(settings);
    if (built_ && now == signature_) return;
    signature_ = now;
    ++generation_;
    using GenerateMipmap = void(APIENTRY*)(GLenum);
    static const auto generate_mipmap = reinterpret_cast<GenerateMipmap>(SDL_GL_GetProcAddress("glGenerateMipmap"));
    for (std::size_t f = 0; f < kPadFamilyCount; ++f) {
        const PromptAtlas atlas = build_prompt_atlas(PadFamily(f), settings);
        if (f == 0) layout_ = atlas;
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
    }
    layout_.image = {}; // (only the cells are kept)
    built_ = true;
}

std::array<float, 4> ButtonPrompts::icon_uv(PromptControl control) const {
    const auto cell = layout_.icon(control);
    const int w = PromptAtlas::kColumns * (PromptAtlas::kCellWidth + PromptAtlas::kGutter);
    const int h = ((int(kPromptControlCount) + PromptAtlas::kColumns - 1) / PromptAtlas::kColumns + 1) *
                  (PromptAtlas::kCellHeight + PromptAtlas::kGutter);
    return {float(cell.x) / float(w), float(cell.y) / float(h), float(cell.x + cell.width) / float(w), float(cell.y + cell.height) / float(h)};
}

unsigned ButtonPrompts::atlas_texture() const {
    if (!built_ || !settings_) return 0;
    // A GameCube pad on its original layout: the game's own pictures.
    if (family_ == PadFamily::gamecube && settings_->gamecube_layout == GameCubeLayout::original) return 0;
    return textures_[std::size_t(family_)];
}

std::array<float, 4> ButtonPrompts::atlas_cell(int control) const {
    const int w = PromptAtlas::kColumns * (PromptAtlas::kCellWidth + PromptAtlas::kGutter);
    const int h = ((int(kPromptControlCount) + PromptAtlas::kColumns - 1) / PromptAtlas::kColumns + 1) *
                  (PromptAtlas::kCellHeight + PromptAtlas::kGutter);
    if (control < 0 || control >= int(kPromptControlCount)) return {0, 0, 0, 0};
    const auto& cell = layout_.cells[std::size_t(control)];
    return {float(cell.x) / float(w), float(cell.y) / float(h), float(cell.x + cell.width) / float(w), float(cell.y + cell.height) / float(h)};
}

std::array<float, 4> ButtonPrompts::atlas_word() const {
    const int w = PromptAtlas::kColumns * (PromptAtlas::kCellWidth + PromptAtlas::kGutter);
    const int h = ((int(kPromptControlCount) + PromptAtlas::kColumns - 1) / PromptAtlas::kColumns + 1) *
                  (PromptAtlas::kCellHeight + PromptAtlas::kGutter);
    const auto& cell = layout_.start_word;
    return {float(cell.x) / float(w), float(cell.y) / float(h), float(cell.x + cell.width) / float(w), float(cell.y + cell.height) / float(h)};
}

std::uint64_t ButtonPrompts::art_key() const { return (generation_ << 8) | std::uint64_t(family_); }

bool ButtonPrompts::title_variant(const std::vector<std::uint8_t>& rgba, int width, int height, bool nearest, std::vector<std::uint8_t>& out,
                                  int& out_width, int& out_height) const {
    if (!settings_ || atlas_texture() == 0) return false;
    ArtImage original(width, height);
    if (rgba.size() != original.rgba.size()) return false;
    original.rgba = rgba;
    ArtImage variant;
    if (!title_press_start(original, 4, nearest, family_, *settings_, variant)) return false;
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

PromptGlyphs prompt_glyphs(const engine::FontImage& font) {
    PromptGlyphs glyphs;
    if (!font.binding || !font.binding->image) return glyphs;
    glyphs.document = font.binding->image.document();
    glyphs.reference = font.binding->image.reference();
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
    // (Without the letters no word is matched: a cell of zero size matches nothing.)
    if (!word) glyphs.letters = {};
    glyphs.valid = glyphs.buttons.size() == std::size(buttons);
    return glyphs;
}

} // namespace dw4::game
