#include "dw4/materials.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace dw4 {
namespace {
constexpr std::size_t max_scene_links = 4u * 1024u * 1024u;
constexpr std::size_t max_inheritance_depth = 64;

[[noreturn]] void unsupported(const std::string& reason) {
    throw std::runtime_error("Material: " + reason);
}

bool bool_field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto* field = doc.field(object, slot);
    if (!field) unsupported("required boolean state is missing");
    const auto bytes = doc.field_bytes(*field);
    if (bytes.size() != 1 || bytes[0] > 1) unsupported("invalid serialized boolean state");
    return bytes[0] != 0;
}

std::uint32_t required_ref(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto ref = doc.field_reference(object, slot);
    if (!ref) unsupported("required local object reference is null");
    return *ref;
}

class Resolver {
public:
    Resolver(const IgbDocument& document, MaterialInfo& output) : doc(document), result(output) {}

    void resolve() {
        const auto& geometry = doc.object(result.geometry_reference);
        if (!doc.is_a(geometry, "igGeometryAttr1_5")) unsupported("selected reference is not an igGeometryAttr1_5");
        std::vector<std::uint32_t> owners;
        for (const auto& object : doc.objects) {
            if (!doc.is_a(object, "igGeometry")) continue;
            const auto attributes = doc.field_reference(object, 8);
            if (!attributes) continue;
            const auto contents = list(*attributes, "igAttrList");
            if (std::find(contents.begin(), contents.end(), result.geometry_reference) != contents.end())
                owners.push_back(object.reference_index);
        }
        if (owners.empty()) unsupported("no scene geometry node explicitly owns the selected geometry");
        if (owners.size() != 1) unsupported("selected geometry has multiple scene owners; material context is ambiguous");
        result.geometry_node_reference = owners[0];
        const auto& owner = doc.object(owners[0]);
        result.geometry_node_name = std::string(doc.field_string(owner, 2));
        auto current = owners[0];
        for (std::size_t depth = 0; depth < max_inheritance_depth; ++depth) {
            if (std::find(result.inheritance_path.begin(), result.inheritance_path.end(), current) != result.inheritance_path.end())
                unsupported("scene-parent cycle prevents material resolution");
            result.inheritance_path.push_back(current);
            const auto& node = doc.object(current);
            if (doc.is_a(node, "igAttrSet")) {
                const auto attributes = doc.field_reference(node, 8);
                if (attributes && resolve_scope(node, *attributes)) return;
            }
            const auto parents = parents_of(current);
            if (parents.empty()) unsupported("no complete local texture binding exists on the unique ancestor path");
            if (parents.size() != 1) unsupported("scene node has multiple parents; inherited material context is ambiguous");
            current = parents[0];
        }
        unsupported("material inheritance exceeds the 64-node limit");
    }

    void resolve_path(std::span<const std::uint32_t> path) {
        if (path.empty() || path.size() > max_inheritance_depth)
            unsupported("explicit material path must contain 1..64 nodes");
        if (!doc.is_a(doc.object(result.geometry_reference),"igGeometryAttr1_5"))
            unsupported("selected reference is not an igGeometryAttr1_5");
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (std::find(path.begin(),path.begin()+i,path[i]) != path.begin()+i)
                unsupported("explicit material path contains a cycle");
            const auto& node = doc.object(path[i]);
            if (!doc.is_a(node,"igNode")) unsupported("explicit material path contains a non-node");
            if (i + 1 == path.size()) continue;
            if (!doc.is_a(node,"igGroup")) unsupported("material path edge has no group parent");
            const auto children = list(required_ref(doc,node,7),"igNodeList");
            if (std::find(children.begin(),children.end(),path[i+1]) == children.end())
                unsupported("explicit material path contains a disconnected edge");
            for (const auto child : children)
                if (child && !doc.is_a(doc.object(*child),"igNode")) unsupported("child list contains a non-node");
        }
        const auto& owner = doc.object(path.back());
        if (!doc.is_a(owner,"igGeometry")) unsupported("explicit path does not end at an igGeometry");
        const auto attributes = list(required_ref(doc,owner,8),"igAttrList");
        if (std::count(attributes.begin(),attributes.end(),result.geometry_reference) != 1)
            unsupported("path owner does not uniquely contain the selected geometry");
        result.geometry_node_reference = owner.reference_index;
        result.geometry_node_name = std::string(doc.field_string(owner,2));
        // Original attributes are pushed independently by type/unit. A nearer
        // binding does not reset inherited enable state, UV matrices or texgen.
        // Validate all scopes, then retain the nearest binding and enable state.
        std::optional<std::uint32_t> bind_reference, state_reference, bind_scope, state_scope, bind_attributes;
        std::size_t farthest_scope = path.size();
        for (const auto ref : path) {
            const auto& node = doc.object(ref);
            if (!doc.is_a(node,"igAttrSet")) continue;
            if (const auto attrs = doc.field_reference(node,8)) {
                const auto selected = validate_path_texture_state(*attrs);
                if (selected.bind) {bind_reference=selected.bind;bind_scope=ref;bind_attributes=attrs;}
                if (selected.state) {state_reference=selected.state;state_scope=ref;}
            }
        }
        if (!bind_reference || !state_reference)
            unsupported("explicit path requires a local or inherited texture binding and enable state");
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (path[i] == *bind_scope || path[i] == *state_scope) farthest_scope=std::min(farthest_scope,i);
        }
        for (std::size_t i=path.size();i>farthest_scope;--i) result.inheritance_path.push_back(path[i-1]);
        resolve_binding(doc.object(*bind_scope),*bind_attributes,*bind_reference,doc.object(*state_scope),*state_reference);
    }

private:
    const IgbDocument& doc;
    MaterialInfo& result;
    std::size_t links_read{};

    struct ScopeAttributes { std::optional<std::uint32_t> bind, state; };
    ScopeAttributes validate_path_texture_state(std::uint32_t attribute_list) {
        ScopeAttributes selected;
        for (const auto ref : list(attribute_list,"igAttrList")) {
            if (!ref) continue;
            const auto& attribute = doc.object(*ref);
            if (!doc.is_a(attribute,"igAttr")) unsupported("attribute list contains an object that is not an igAttr");
            if (doc.is_a(attribute,"igTextureBindAttr")) {
                if (selected.bind) unsupported("explicit path has duplicate texture bindings in one scope");
                selected.bind=*ref;
                if (doc.field_i32(attribute,5) != 0)
                    unsupported("explicit path contains an unsupported texture binding unit");
                if (!doc.is_a(doc.object(required_ref(doc,attribute,4)),"igTextureAttr"))
                    unsupported("path texture bind does not reference an igTextureAttr");
            } else if (doc.is_a(attribute,"igTextureStateAttr")) {
                if (selected.state) unsupported("explicit path has duplicate texture enable states in one scope");
                selected.state=*ref;
                bool_field(doc,attribute,4); // A valid disabled ancestor can be overridden locally.
                if (doc.field_i32(attribute,5) != 0)
                    unsupported("explicit path contains an unsupported texture state unit");
            } else {
                const auto& name = doc.metadata.object_types.at(attribute.type_index).name;
                if (name.starts_with("igTexture") || name.starts_with("igMultiTexture") || name.starts_with("igTexGen"))
                    unsupported("inherited texture state is outside the single-stage path profile: " + name);
            }
        }
        return selected;
    }

    std::vector<std::optional<std::uint32_t>> list(std::uint32_t reference, std::string_view expected_type) {
        const auto& object = doc.object(reference);
        if (!doc.is_a(object, expected_type)) unsupported("scene list has an unexpected type");
        const auto count = doc.field_i32(object, 2);
        if (count < 0 || static_cast<std::size_t>(count) > max_scene_links - links_read)
            unsupported("scene traversal exceeds the 4,194,304-link limit");
        links_read += static_cast<std::size_t>(count);
        return doc.object_list(reference);
    }

    std::vector<std::uint32_t> parents_of(std::uint32_t child) {
        std::vector<std::uint32_t> parents;
        for (const auto& object : doc.objects) {
            if (!doc.is_a(object, "igGroup")) continue;
            const auto children = doc.field_reference(object, 7);
            if (!children) continue;
            const auto contents = list(*children, "igNodeList");
            for (const auto item : contents)
                if (item && !doc.is_a(doc.object(*item), "igNode")) unsupported("node list contains an object that is not an igNode");
            if (std::find(contents.begin(), contents.end(), child) != contents.end()) parents.push_back(object.reference_index);
        }
        return parents;
    }

    bool resolve_scope(const IgbObject& node, std::uint32_t attribute_list) {
        std::vector<std::uint32_t> binds, states;
        for (const auto ref : list(attribute_list, "igAttrList")) {
            if (!ref) continue;
            const auto& attribute = doc.object(*ref);
            if (!doc.is_a(attribute, "igAttr")) unsupported("attribute list contains an object that is not an igAttr");
            if (doc.is_a(attribute, "igTextureBindAttr")) binds.push_back(*ref);
            else if (doc.is_a(attribute, "igTextureStateAttr")) states.push_back(*ref);
            else {
                const auto& name = doc.metadata.object_types.at(attribute.type_index).name;
                if (name.starts_with("igTexture") || name.starts_with("igMultiTexture") || name.starts_with("igTexGen"))
                    unsupported("additional texture state is outside the single-stage profile: " + name);
            }
        }
        if (binds.empty() && states.empty()) return false;
        if (binds.size() != 1 || states.size() != 1)
            unsupported("texture binding/state is incomplete or ambiguous in one attribute scope");
        resolve_binding(node,attribute_list,binds[0],node,states[0]);
        return true;
    }

    void resolve_binding(const IgbObject& node, std::uint32_t attribute_list, std::uint32_t binding_reference,
        const IgbObject& state_node, std::uint32_t state_reference) {
        const auto& state = doc.object(state_reference);
        if (!bool_field(doc, state, 4)) unsupported("texturing is disabled in the selected attribute scope");
        if (doc.field_i32(state, 5) != 0) unsupported("texture state selects an unsupported texture unit");
        const auto& bind = doc.object(binding_reference);
        if (doc.field_i32(bind, 5) != 0) unsupported("texture binding selects an unsupported texture unit");
        result.scope_node_reference = node.reference_index;
        result.state_scope_node_reference = state_node.reference_index;
        result.attribute_list_reference = attribute_list;
        result.texture_bind_reference = bind.reference_index;
        result.inherited = node.reference_index != *result.geometry_node_reference
            || state_node.reference_index != *result.geometry_node_reference;
        result.texture_reference = required_ref(doc, bind, 4);
        const auto& texture = doc.object(*result.texture_reference);
        if (!doc.is_a(texture, "igTextureAttr")) unsupported("texture bind does not reference an igTextureAttr");
        result.raw_mag_filter = doc.field_u32(texture, 5);
        result.raw_min_filter = doc.field_u32(texture, 6);
        result.raw_wrap_s = doc.field_u32(texture, 7);
        result.raw_wrap_t = doc.field_u32(texture, 8);
        if (result.raw_mag_filter != 1 || result.raw_min_filter != 1 || result.raw_wrap_s != 1 || result.raw_wrap_t != 1)
            unsupported("sampler differs from the verified linear/repeat profile");
        if (doc.field_u32(texture, 4) != 0 || doc.field_u32(texture, 10) != 0 || doc.field_u32(texture, 11) != 0)
            unsupported("texture color/mipmap/source state differs from the verified local-image profile");
        if (bool_field(doc, texture, 13) || doc.field_reference(texture, 14) || doc.field_i32(texture, 15) != 1)
            unsupported("paged, texture-unit, or multi-image texture state is unsupported");
        const auto mipmaps = required_ref(doc, texture, 16);
        if (!list(mipmaps, "igImageMipMapList").empty()) unsupported("texture has an explicit mipmap list outside the base-image profile");
        result.image_reference = required_ref(doc, texture, 12);
        const auto& image = doc.object(*result.image_reference);
        if (!doc.is_a(image, "igImage")) unsupported("texture image is not a local igImage object");
        result.image_name = std::string(doc.field_string(image, 22));
        result.mag_filter = MaterialFilter::linear;
        result.min_filter = MaterialFilter::linear;
        result.wrap_s = MaterialWrap::repeat;
        result.wrap_t = MaterialWrap::repeat;
        result.provenance = "Geometry " + std::to_string(result.geometry_reference) + " -> node " +
            std::to_string(*result.geometry_node_reference) + " -> scope " + std::to_string(*result.scope_node_reference) +
            " -> attr list " + std::to_string(attribute_list) + " -> bind " + std::to_string(bind.reference_index) +
            " -> texture " + std::to_string(*result.texture_reference) + " -> image " + std::to_string(*result.image_reference) +
            " / enabled by state " + std::to_string(state.reference_index) + " at node " + std::to_string(state_node.reference_index);
        result.supported = true;
    }
};
} // namespace

MaterialInfo resolve_igb_material(const IgbDocument& document, std::uint32_t geometry_reference) {
    MaterialInfo result;
    result.geometry_reference = geometry_reference;
    try { Resolver(document, result).resolve(); }
    catch (const std::exception& error) { result.diagnostic = error.what(); }
    return result;
}

MaterialInfo resolve_igb_material_on_path(const IgbDocument& document,
    std::uint32_t geometry_reference, std::span<const std::uint32_t> root_to_owner) {
    MaterialInfo result; result.geometry_reference = geometry_reference;
    try { Resolver(document,result).resolve_path(root_to_owner); }
    catch (const std::exception& error) { result.diagnostic = error.what(); }
    return result;
}

} // namespace dw4
