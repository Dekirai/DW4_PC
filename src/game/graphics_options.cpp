#include "graphics_options.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace dw4::game {
namespace {

std::string lower(std::string_view text) {
    std::string out;
    for (const char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// A small whole number written alone ("0".."9").
std::optional<int> small_number(const std::string& text) {
    if (text.size() != 1 || !std::isdigit(static_cast<unsigned char>(text[0]))) return std::nullopt;
    return text[0] - '0';
}

} // namespace

GraphicsOptions GraphicsOptions::clamped() const {
    GraphicsOptions out = *this;
    if (!std::isfinite(out.lod_bias)) out.lod_bias = 0.0f;
    out.lod_bias = std::clamp(out.lod_bias, kLodBiasMin, kLodBiasMax);
    out.texture_memory_mb = std::clamp(out.texture_memory_mb, kMemoryMinMb, kMemoryMaxMb);
    if (out.texture_quality > TextureQuality::ultra) out.texture_quality = TextureQuality::ultra;
    if (out.texture_filter > TextureFilter::aniso16) out.texture_filter = TextureFilter::aniso16;
    if (out.sharpening > Sharpening::high) out.sharpening = Sharpening::high;
    if (out.shadow_quality > ShadowQuality::ultra) out.shadow_quality = ShadowQuality::ultra;
    return out;
}

const char* texture_quality_name(TextureQuality value) {
    switch (value) {
    case TextureQuality::original: return "original";
    case TextureQuality::high: return "high";
    case TextureQuality::ultra: return "ultra";
    }
    return "ultra";
}

const char* texture_filter_name(TextureFilter value) {
    switch (value) {
    case TextureFilter::bilinear: return "bilinear";
    case TextureFilter::trilinear: return "trilinear";
    case TextureFilter::aniso2: return "anisotropic2";
    case TextureFilter::aniso4: return "anisotropic4";
    case TextureFilter::aniso8: return "anisotropic8";
    case TextureFilter::aniso16: return "anisotropic16";
    }
    return "bilinear";
}

const char* sharpening_name(Sharpening value) {
    switch (value) {
    case Sharpening::off: return "off";
    case Sharpening::low: return "low";
    case Sharpening::high: return "high";
    }
    return "off";
}

const char* shadow_quality_name(ShadowQuality value) {
    switch (value) {
    case ShadowQuality::original: return "original";
    case ShadowQuality::high: return "high";
    case ShadowQuality::ultra: return "ultra";
    }
    return "original";
}

std::optional<TextureQuality> parse_texture_quality(std::string_view text) {
    const auto t = lower(text);
    if (t == "original" || t == "off") return TextureQuality::original;
    if (t == "high" || t == "2x") return TextureQuality::high;
    if (t == "ultra" || t == "4x") return TextureQuality::ultra;
    if (const auto n = small_number(t); n && *n <= 2) return TextureQuality(*n);
    return std::nullopt;
}

std::optional<TextureFilter> parse_texture_filter(std::string_view text) {
    const auto t = lower(text);
    if (t == "bilinear" || t == "original") return TextureFilter::bilinear;
    if (t == "trilinear") return TextureFilter::trilinear;
    for (const auto f : {TextureFilter::aniso2, TextureFilter::aniso4, TextureFilter::aniso8, TextureFilter::aniso16}) {
        const std::string level = std::to_string(anisotropy(f));
        if (t == texture_filter_name(f) || t == "aniso" + level || t == level + "x") return f;
    }
    if (const auto n = small_number(t); n && *n <= 5) return TextureFilter(*n);
    return std::nullopt;
}

std::optional<Sharpening> parse_sharpening(std::string_view text) {
    const auto t = lower(text);
    if (t == "off" || t == "none") return Sharpening::off;
    if (t == "low") return Sharpening::low;
    if (t == "high") return Sharpening::high;
    if (const auto n = small_number(t); n && *n <= 2) return Sharpening(*n);
    return std::nullopt;
}

std::optional<ShadowQuality> parse_shadow_quality(std::string_view text) {
    const auto t = lower(text);
    if (t == "original") return ShadowQuality::original;
    if (t == "high") return ShadowQuality::high;
    if (t == "ultra") return ShadowQuality::ultra;
    if (const auto n = small_number(t); n && *n <= 2) return ShadowQuality(*n);
    return std::nullopt;
}

std::optional<float> parse_lod_bias(std::string_view text) {
    const std::string t = lower(text);
    if (t.empty() || t.size() > 16) return std::nullopt;
    char* end = nullptr;
    const float value = std::strtof(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(value)) return std::nullopt;
    return std::clamp(value, GraphicsOptions::kLodBiasMin, GraphicsOptions::kLodBiasMax);
}

const char* texture_quality_label(TextureQuality value) {
    switch (value) {
    case TextureQuality::original: return "Original";
    case TextureQuality::high: return "High (2x)";
    case TextureQuality::ultra: return "Ultra (4x)";
    }
    return "Ultra (4x)";
}

const char* texture_filter_label(TextureFilter value) {
    switch (value) {
    case TextureFilter::bilinear: return "Bilinear (original)";
    case TextureFilter::trilinear: return "Trilinear";
    case TextureFilter::aniso2: return "Anisotropic 2x";
    case TextureFilter::aniso4: return "Anisotropic 4x";
    case TextureFilter::aniso8: return "Anisotropic 8x";
    case TextureFilter::aniso16: return "Anisotropic 16x";
    }
    return "Bilinear (original)";
}

const char* sharpening_label(Sharpening value) {
    switch (value) {
    case Sharpening::off: return "Off";
    case Sharpening::low: return "Low";
    case Sharpening::high: return "High";
    }
    return "Off";
}

const char* shadow_quality_label(ShadowQuality value) {
    switch (value) {
    case ShadowQuality::original: return "Original";
    case ShadowQuality::high: return "High (4x)";
    case ShadowQuality::ultra: return "Ultra (8x)";
    }
    return "Original";
}

int target_scale(TextureQuality value) {
    return value == TextureQuality::ultra ? 4 : value == TextureQuality::high ? 2 : 0;
}

int anisotropy(TextureFilter value) {
    switch (value) {
    case TextureFilter::aniso2: return 2;
    case TextureFilter::aniso4: return 4;
    case TextureFilter::aniso8: return 8;
    case TextureFilter::aniso16: return 16;
    default: return 1;
    }
}

float sharpness(Sharpening value) {
    return value == Sharpening::high ? 1.0f : value == Sharpening::low ? 0.4f : 0.0f;
}

int shadow_scale(ShadowQuality value) {
    return value == ShadowQuality::ultra ? 8 : value == ShadowQuality::high ? 4 : 1;
}

GraphicsOptions graphics_from_env(GraphicsOptions base, const char* (*lookup)(const char*)) {
    auto get = [&](const char* name) -> std::optional<std::string_view> {
        const char* value = lookup ? lookup(name) : nullptr;
        if (!value || !*value) return std::nullopt;
        return std::string_view(value);
    };
    if (const auto v = get("DW4_TEXTURE_QUALITY"))
        if (const auto q = parse_texture_quality(*v)) base.texture_quality = *q;
    if (const auto v = get("DW4_TEXTURE_FILTER"))
        if (const auto f = parse_texture_filter(*v)) base.texture_filter = *f;
    if (const auto v = get("DW4_LOD_BIAS"))
        if (const auto b = parse_lod_bias(*v)) base.lod_bias = *b;
    if (const auto v = get("DW4_SHARPEN"))
        if (const auto s = parse_sharpening(*v)) base.sharpening = *s;
    if (const auto v = get("DW4_SHADOW_QUALITY"))
        if (const auto s = parse_shadow_quality(*v)) base.shadow_quality = *s;
    if (const auto v = get("DW4_TEXTURE_MEMORY")) {
        const std::string text(*v);
        char* end = nullptr;
        const long mb = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() + text.size() && mb > 0) base.texture_memory_mb = int(std::min<long>(mb, GraphicsOptions::kMemoryMaxMb));
    }
    return base.clamped();
}

} // namespace dw4::game
