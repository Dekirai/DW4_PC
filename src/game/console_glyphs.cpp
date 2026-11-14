#include "console_glyphs.hpp"

#include "button_art.hpp"
#include "console_menu.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <imgui.h>

namespace dw4::game {

namespace {
PromptFace face_only(Face face) {
    PromptFace f;
    f.face = face;
    return f;
}
PromptFace key_only(const char* key) {
    PromptFace f;
    f.key = key;
    return f;
}
PromptFace layout_button(PadFamily family, const Settings& settings, int button) {
    return face_only(face_of(family, settings.nintendo_layout, PadInput{PadInput::Kind::button, button}));
}
} // namespace

std::vector<PromptFace> console_faces(ConsoleGlyph glyph, PadFamily family, const Settings& settings, MenuPlatform platform) {
    if (glyph == ConsoleGlyph::lr || glyph == ConsoleGlyph::ud) {
        if (family == PadFamily::keyboard)
            return glyph == ConsoleGlyph::lr ? std::vector<PromptFace>{key_only("Left"), key_only("Right")}
                                             : std::vector<PromptFace>{key_only("Up"), key_only("Down")};
        return glyph == ConsoleGlyph::lr ? std::vector<PromptFace>{face_only(Face::dpad_left), face_only(Face::dpad_right)}
                                         : std::vector<PromptFace>{face_only(Face::dpad_up), face_only(Face::dpad_down)};
    }
    if (family == PadFamily::keyboard) {
        switch (glyph) {
        case ConsoleGlyph::ok: return {key_only("Return")};
        case ConsoleGlyph::back: return {key_only("Escape")};
        case ConsoleGlyph::del: return {key_only("Delete")};
        case ConsoleGlyph::prev: return {key_only("PageUp")};
        case ConsoleGlyph::next: return {key_only("PageDown")};
        case ConsoleGlyph::close: return {key_only("F1")};
        default: return {};
        }
    }
    if (glyph == ConsoleGlyph::close) {
        if (platform == MenuPlatform::android && family == PadFamily::thor) return {face_only(Face::back_key)};
        return {prompt_face(family, settings, PromptControl::menu)};
    }
    const bool layout = settings.menu_buttons == MenuButtons::layout;
    switch (glyph) {
    // (prompt_face's confirm / back / erase are the menus' own buttons with
    // menu_buttons=layout and the game's A / B / X bindings otherwise.)
    case ConsoleGlyph::ok: return {prompt_face(family, settings, PromptControl::confirm)};
    case ConsoleGlyph::back: return {prompt_face(family, settings, PromptControl::back)};
    case ConsoleGlyph::del: return {prompt_face(family, settings, PromptControl::erase)};
    case ConsoleGlyph::prev:
        return {layout ? layout_button(family, settings, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) : prompt_face(family, settings, PromptControl::l)};
    case ConsoleGlyph::next:
        return {layout ? layout_button(family, settings, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) : prompt_face(family, settings, PromptControl::r)};
    case ConsoleGlyph::l2:
    case ConsoleGlyph::r2:
        if (!layout) return {};
        return {face_only(face_of(family, settings.nintendo_layout,
                                  PadInput{PadInput::Kind::axis, glyph == ConsoleGlyph::l2 ? int(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                                                                                           : int(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)}))};
    case ConsoleGlyph::start:
        return {layout ? layout_button(family, settings, SDL_GAMEPAD_BUTTON_START) : prompt_face(family, settings, PromptControl::start)};
    default: return {};
    }
}

PromptFace console_face(ConsoleGlyph glyph, PadFamily family, const Settings& settings, MenuPlatform platform) {
    const auto faces = console_faces(glyph, family, settings, platform);
    return faces.empty() ? PromptFace{} : faces.front();
}

GlyphCache::~GlyphCache() {
    // (No context may be current at the process's end: the names are left.)
}

unsigned GlyphCache::texture(PadFamily family, const PromptFace& face) {
    if (face.face == Face::none && face.key.empty()) return 0;
    const auto key = std::make_tuple(int(family), int(face.face), face.key);
    if (const auto found = textures_.find(key); found != textures_.end()) return found->second;
    constexpr int kSide = 96;
    ArtImage image(kSide, kSide);
    draw_prompt(image, 0.0f, 0.0f, float(kSide), float(kSide), family, face, false);
    GLuint name = 0;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSide, kSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    textures_[key] = name;
    return name;
}

void GlyphCache::reset() { textures_.clear(); }

GlyphCache& console_glyphs() {
    static GlyphCache cache;
    return cache;
}

void draw_console_glyph(ImDrawList* draw, PadFamily family, const PromptFace& face, float x, float y, float size, std::uint32_t tint) {
    const unsigned texture = console_glyphs().texture(family, face);
    if (!texture || !draw) return;
    draw->AddImage(ImTextureRef(ImTextureID(static_cast<std::uintptr_t>(texture))), ImVec2(x, y), ImVec2(x + size, y + size), ImVec2(0, 0),
                   ImVec2(1, 1), tint);
}

} // namespace dw4::game

namespace dw4::game {

bool back_toggles_menu(MenuPlatform platform, PadFamily family) { return !(platform == MenuPlatform::android && family == PadFamily::thor); }

} // namespace dw4::game
