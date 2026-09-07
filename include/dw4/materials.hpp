#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;

enum class MaterialWrap { repeat, clamp_to_edge, mirrored_repeat };
enum class MaterialFilter { nearest, linear };

struct MaterialInfo {
    bool supported{};
    std::string diagnostic;
    std::uint32_t geometry_reference{};
    std::optional<std::uint32_t> geometry_node_reference;
    std::optional<std::uint32_t> scope_node_reference;
    std::optional<std::uint32_t> attribute_list_reference;
    std::optional<std::uint32_t> texture_bind_reference;
    std::optional<std::uint32_t> texture_reference;
    std::optional<std::uint32_t> image_reference;
    std::string geometry_node_name;
    std::string image_name;
    std::string provenance;
    bool inherited{};
    // Owner-to-binding-scope references. The caller can display the exact path.
    std::vector<std::uint32_t> inheritance_path;
    MaterialWrap wrap_s{MaterialWrap::repeat};
    MaterialWrap wrap_t{MaterialWrap::repeat};
    MaterialFilter min_filter{MaterialFilter::linear};
    MaterialFilter mag_filter{MaterialFilter::linear};
    std::uint32_t raw_wrap_s{};
    std::uint32_t raw_wrap_t{};
    std::uint32_t raw_min_filter{};
    std::uint32_t raw_mag_filter{};
};

// Resolves one verified local stage-zero base-color texture through explicit
// scene attribute references. No first-image or filename-based fallback.
// Unsupported/ambiguous state returns supported=false with a diagnostic.
// The result does not decode UVs/pixels or reconstruct lighting and animation.
MaterialInfo resolve_igb_material(const IgbDocument& document, std::uint32_t geometry_reference);

} // namespace dw4
