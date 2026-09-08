#pragma once

#include <cstdint>
#include <optional>
#include <span>
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
    std::optional<std::uint32_t> state_scope_node_reference;
    std::optional<std::uint32_t> attribute_list_reference;
    std::optional<std::uint32_t> texture_bind_reference;
    std::optional<std::uint32_t> texture_reference;
    std::optional<std::uint32_t> image_reference;
    std::string geometry_node_name;
    std::string image_name;
    std::string provenance;
    bool inherited{};
    // Owner through the farthest selected binding/state scope. Both scopes are
    // identical for the legacy resolver; explicit scene paths can inherit them
    // independently. attribute_list_reference is the binding scope's list.
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

// Resolve the same narrow profile on an explicit root-to-geometry-node path.
// Every edge and geometry ownership is checked. Other parents of shared nodes
// do not affect this instance; state above the supplied root is not inferred.
// All supplied ancestor scopes are checked for unhandled texture modifiers and
// other texture units, even above a complete local unit-zero bind/state pair.
// The nearest binding and nearest enable state are selected independently;
// scope_node_reference identifies the binding, state_scope_node_reference its
// enabling state. Duplicate attributes within one scope remain unsupported.
MaterialInfo resolve_igb_material_on_path(const IgbDocument& document,
    std::uint32_t geometry_reference, std::span<const std::uint32_t> root_to_owner);

} // namespace dw4
