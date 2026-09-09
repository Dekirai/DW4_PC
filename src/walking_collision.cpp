#include "dw4/walking_collision.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace dw4 {
namespace {
constexpr std::size_t max_exports = 4096;
constexpr std::size_t max_shapes = 256;
constexpr std::size_t max_elements = 1048576;
[[noreturn]] void fail(std::string_view message) {
    throw WalkingCollisionError("Walking collision: " + std::string(message));
}
const IgbObject& exact(const IgbDocument& doc, std::uint32_t ref, std::string_view name) {
    const auto& object = doc.object(ref);
    if (doc.metadata.object_types.at(object.type_index).name != name)
        fail("unsupported object type in the static collision profile");
    return object;
}
void field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot,
    std::string_view name, std::size_t size = 4) {
    const auto* value = doc.field(object, slot);
    if (!value || value->size != size || doc.metadata.field_types.at(value->type_index).name != name)
        fail("missing or incorrectly typed serialized field");
}
std::uint32_t reference(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot,
    std::string_view name = "igObjectRefMetaField") {
    field(doc, object, slot, name);
    const auto ref = doc.field_reference(object, slot);
    if (!ref) fail("required reference is null");
    return *ref;
}
std::vector<std::optional<std::uint32_t>> list(const IgbDocument& doc, std::uint32_t ref,
    std::string_view name, std::size_t limit) {
    const auto& object = exact(doc, ref, name);
    field(doc, object, 2, "igIntMetaField");
    field(doc, object, 3, "igIntMetaField");
    field(doc, object, 4, "igMemoryRefMetaField");
    const auto count = doc.field_i32(object, 2);
    const auto capacity = doc.field_i32(object, 3);
    if (count < 0 || capacity < count || static_cast<std::size_t>(capacity) > limit)
        fail("list exceeds the static collision profile's limits");
    auto values = doc.object_list(ref);
    for (const auto value : values) if (!value) fail("null object in a required list");
    return values;
}
std::vector<bool> mask(const IgbDocument& doc, const IgbObject& node, std::size_t children) {
    const auto& object = exact(doc, reference(doc, node, 8), "igBitMask");
    field(doc, object, 2, "igIntMetaField");
    field(doc, object, 3, "igIntMetaField");
    field(doc, object, 5, "igUnsignedIntMetaField");
    const auto count = doc.field_i32(object, 2);
    const auto capacity = doc.field_i32(object, 3);
    const auto bits = doc.field_u32(object, 5);
    const auto words = (bits + 31u) / 32u;
    if (bits > max_shapes || bits < children || count < 0 || capacity < count ||
        capacity > static_cast<std::int32_t>((max_shapes + 31) / 32) ||
        static_cast<std::uint32_t>(count) != words)
        fail("invalid switch mask layout");
    const auto bytes = doc.memory(reference(doc, object, 4, "igMemoryRefMetaField"));
    if (bytes.size() != static_cast<std::size_t>(capacity) * 4)
        fail("switch mask storage differs from its capacity");
    std::vector<bool> selected(children);
    for (std::size_t i = 0; i < children; ++i)
        selected[i] = (bytes[i / 8] & (1u << (i % 8))) != 0;
    return selected;
}
void flags(const IgbDocument& doc, const IgbObject& object, std::uint32_t expected) {
    field(doc, object, 5, "igIntMetaField");
    if (doc.field_u32(object, 5) != expected)
        fail("unverified node flags in the static collision profile");
}
std::vector<MeshData> decode_category(const IgbDocument& doc, std::uint32_t info_reference,
    std::int32_t kind) {
    try {
        if (doc.metadata.header.byte_order != IgbByteOrder::little)
            fail("big-endian collision geometry is unverified");
        if (!doc.info_list_reference) fail("document has no explicit exports");
        const auto exports = list(doc, *doc.info_list_reference, "igInfoList", max_exports);
        if (std::find(exports.begin(), exports.end(), info_reference) == exports.end())
            fail("selected collision info is not exported");
        const auto& info = exact(doc, info_reference, "beHitLandModelInfo");
        field(doc, info, 10, "igIntMetaField");
        if (doc.field_i32(info, 10) != kind)
            fail(kind == 1 ? "selected info is not a category-1 collision model" :
                "selected info is not a category-2 wall model");

        const auto& outer = exact(doc, reference(doc, info, 7), "igSwitch");
        const auto inner_ref = reference(doc, info, 8);
        const auto& inner = exact(doc, inner_ref, "igSwitch");
        flags(doc, outer, 0); flags(doc, inner, 0);
        const auto outer_children = list(doc, reference(doc, outer, 7), "igNodeList", 1);
        if (outer_children.size() != 1 || *outer_children.front() != inner_ref)
            fail("outer collision switch does not contain exactly the declared inner switch");
        // The original CHECKLAND query uses _checkSwitch directly; the outer
        // _switchgraph's disabled mask does not disable these collision queries.
        // Validate the wrapper relationship without modifying either mask.
        static_cast<void>(mask(doc, outer, 1));
        const auto children = list(doc, reference(doc, inner, 7), "igNodeList", max_shapes);
        if (children.empty()) fail("collision model has no geometry children");
        const auto selected = mask(doc, inner, children.size());
        std::vector<MeshData> result;
        std::size_t vertices{}, triangles{};
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto& geometry = exact(doc, *children[i], "igGeometry");
            flags(doc, geometry, 16);
            if (!list(doc, reference(doc, geometry, 7), "igNodeList", max_shapes).empty())
                fail("nested geometry children are outside the static collision profile");
            field(doc, geometry, 9, "igBoolMetaField", 1);
            if (doc.field_bytes(*doc.field(geometry, 9))[0] != 1)
                fail("disabled geometry attributes are outside the static collision profile");
            const auto attrs = list(doc, reference(doc, geometry, 8), "igAttrList", 1);
            if (attrs.size() != 1) fail("collision shape must have exactly one geometry attribute");
            exact(doc, *attrs.front(), "igGeometryAttr1_5");
            if (!selected[i]) continue;
            auto mesh = decode_igb_mesh(doc, *attrs.front());
            if (mesh.positions.size() > max_elements - vertices ||
                mesh.indices.size() / 3 > max_elements - triangles)
                fail("combined collision geometry exceeds the element limit");
            if (mesh.positions.empty() || mesh.indices.empty()) fail("selected collision shape has no triangles");
            vertices += mesh.positions.size(); triangles += mesh.indices.size() / 3;
            result.push_back(std::move(mesh));
        }
        if (result.empty() && kind == 1) fail("inner collision switch selects no geometry");
        return result;
    } catch (const WalkingCollisionError&) { throw; }
    catch (const std::exception& error) { throw WalkingCollisionError("Walking collision: " + std::string(error.what())); }
}
} // namespace

std::vector<MeshData> decode_walking_collision(const IgbDocument& doc, std::uint32_t info_reference) {
    return decode_category(doc, info_reference, 1);
}
std::vector<MeshData> decode_walking_walls(const IgbDocument& doc, std::uint32_t info_reference) {
    return decode_category(doc, info_reference, 2);
}
} // namespace dw4
