#include "dw4/scenes.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <span>

namespace dw4 {
namespace {
constexpr std::size_t max_depth = 64;
constexpr std::size_t max_nodes = 65536;
constexpr std::size_t max_instances = 4096;
constexpr std::size_t max_elements = 4u * 1024u * 1024u;
constexpr SceneMatrix identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
[[noreturn]] void fail(const std::string& reason) { throw SceneError("Scene: " + reason); }

std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size()-at < 4) fail("truncated little-endian word");
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
        (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
}
std::span<const std::uint8_t> field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, std::size_t size) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != size) fail("required serialized field has an unexpected size or is absent");
    return doc.field_bytes(*value);
}
bool boolean(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    const auto value = field(doc,object,slot,1)[0];
    if (value > 1) fail("invalid serialized boolean");
    return value != 0;
}
const IgbObject& typed(const IgbDocument& doc, std::uint32_t ref, const char* name) {
    const auto& object = doc.object(ref);
    if (!doc.is_a(object,name)) fail(std::string("expected ") + name + " at referenced object");
    return object;
}
std::string type_name(const IgbDocument& doc, const IgbObject& object) {
    return doc.metadata.object_types.at(object.type_index).name;
}
SceneMatrix multiply(const SceneMatrix& parent, const SceneMatrix& local) {
    SceneMatrix result{};
    for (std::size_t col = 0; col < 4; ++col)
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t k = 0; k < 4; ++k) result[col*4+row] += parent[k*4+row]*local[col*4+k];
            if (!std::isfinite(result[col*4+row])) fail("composed transform overflow");
        }
    return result;
}
SceneMatrix matrix(const IgbDocument& doc, const IgbObject& node) {
    const auto bytes = field(doc,node,8,64);
    SceneMatrix result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = std::bit_cast<float>(word(bytes,i*4));
        if (!std::isfinite(result[i])) fail("transform matrix contains a non-finite value");
    }
    if (result[3] != 0 || result[7] != 0 || result[11] != 0 || result[15] != 1)
        fail("transform is outside the affine matrix profile");
    return result;
}
void extend(MeshBounds& bounds, Vec3 point) {
    bounds.min.x = std::min(bounds.min.x,point.x); bounds.max.x = std::max(bounds.max.x,point.x);
    bounds.min.y = std::min(bounds.min.y,point.y); bounds.max.y = std::max(bounds.max.y,point.y);
    bounds.min.z = std::min(bounds.min.z,point.z); bounds.max.z = std::max(bounds.max.z,point.z);
}
Vec3 transform(const SceneMatrix& matrix, Vec3 point) {
    const double input[]{point.x,point.y,point.z};
    float output[3]{};
    for (std::size_t row = 0; row < 3; ++row) {
        double value = matrix[12+row];
        for (std::size_t col = 0; col < 3; ++col) value += matrix[col*4+row]*input[col];
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            fail("world position exceeds finite float range");
        output[row] = static_cast<float>(value);
    }
    return {output[0],output[1],output[2]};
}
SceneInfo descriptor(const IgbDocument& doc, std::uint32_t reference) {
    SceneInfo info; info.info_reference = reference;
    try {
        const auto& object = typed(doc,reference,"igSceneInfo");
        info.name = std::string(doc.field_string(object,2));
        if (type_name(doc,object) != "igSceneInfo") fail("derived scene descriptors are unverified");
        if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("big-endian scene storage is unverified");
        const auto root = doc.field_reference(object,5);
        if (!root) fail("scene descriptor has no explicit root");
        info.root_reference = *root;
        const auto& node = typed(doc,*root,"igNode");
        info.root_type = type_name(doc,node);
        const auto up = field(doc,object,10,12);
        info.up_direction = {std::bit_cast<float>(word(up,0)),std::bit_cast<float>(word(up,4)),
            std::bit_cast<float>(word(up,8))};
        if (!std::isfinite(info.up_direction.x) || !std::isfinite(info.up_direction.y) || !std::isfinite(info.up_direction.z))
            fail("scene up direction is non-finite");
        info.supported = true;
    } catch (const std::exception& error) { info.diagnostic = error.what(); }
    return info;
}

class Decoder {
public:
    Decoder(const IgbDocument& document, SceneInfo info, SceneOptions options) : doc(document) {
        result.info = std::move(info); result.inspected_root_child = options.inspect_root_child;
        result.root_control = options.root_control;
    }
    SceneData decode() {
        if (!result.info.supported) fail(result.info.diagnostic);
        if (result.inspected_root_child && result.info.root_type != "igSwitch")
            fail("root-child inspection requires an exact igSwitch root");
        if (result.root_control) {
            if (result.inspected_root_child) fail("controller command and root-child inspection are mutually exclusive");
            control = evaluate_igb_switch_control(doc,*result.root_control);
            if (control->info.target_reference != result.info.root_reference)
                fail("controller target differs from the selected scene root");
        }
        visit(result.info.root_reference,identity);
        result.complete = result.diagnostics.empty();
        return std::move(result);
    }
private:
    const IgbDocument& doc;
    SceneData result;
    std::optional<SwitchControlState> control;
    std::vector<std::uint32_t> path;
    std::size_t links{}, cached_vertices{}, cached_triangles{};
    std::map<std::uint32_t,MeshData> meshes;

    void unsupported(const IgbObject& node, const std::string& reason) {
        result.diagnostics.push_back({node.reference_index,path,reason});
    }
    std::vector<std::optional<std::uint32_t>> list(std::uint32_t ref, const char* name) {
        const auto& object = typed(doc,ref,name);
        const auto count = doc.field_i32(object,2);
        if (count < 0 || static_cast<std::size_t>(count) > max_elements-links) fail("scene exceeds the 4,194,304-link limit");
        links += static_cast<std::size_t>(count);
        return doc.object_list(ref);
    }
    std::vector<std::optional<std::uint32_t>> children(const IgbObject& node) {
        const auto ref = doc.field_reference(node,7);
        if (!ref) return {};
        auto values = list(*ref,"igNodeList");
        for (const auto child : values) if (child) typed(doc,*child,"igNode");
        return values;
    }
    std::vector<std::optional<std::uint32_t>> attributes(const IgbObject& node) {
        const auto ref = doc.field_reference(node,8);
        if (!ref) return {};
        auto values = list(*ref,"igAttrList");
        for (const auto attribute : values) if (attribute) typed(doc,*attribute,"igAttr");
        return values;
    }
    bool static_attributes(const IgbObject& node, const std::vector<std::optional<std::uint32_t>>& attrs) {
        for (const auto ref : attrs) if (ref) {
            const auto type = type_name(doc,doc.object(*ref));
            // These verified states do not change vertex positions. Their full
            // lighting, blending and texture behavior remains a renderer concern.
            if (type != "igGeometryAttr1_5" && type != "igTextureBindAttr" && type != "igTextureStateAttr" &&
                type != "igMaterialAttr" && type != "igShadeModelAttr" && type != "igBlendFunctionAttr" &&
                type != "igBlendStateAttr") {
                unsupported(node,"attribute behavior is outside the static position profile: " + type); return false;
            }
        }
        return true;
    }
    std::vector<bool> switch_mask(const IgbObject& node, std::size_t child_count) {
        const auto ref = doc.field_reference(node,8);
        if (!ref) {
            if (child_count) fail("nonempty switch has no child mask");
            return {};
        }
        const auto& mask = typed(doc,*ref,"igBitMask");
        const auto count = doc.field_i32(mask,2), capacity = doc.field_i32(mask,3);
        const auto bits = doc.field_u32(mask,5);
        const auto words = (std::uint64_t(bits)+31)/32;
        if (bits < child_count || bits > max_elements || count < 0 || capacity < count ||
            static_cast<std::uint64_t>(count) != words || static_cast<std::uint64_t>(capacity) > (max_elements+31)/32)
            fail("switch mask counts/capacity are inconsistent or exceed limits");
        const auto memory = doc.field_reference(mask,4);
        if (!memory) {
            if (count || capacity) fail("switch mask memory is null");
            return {};
        }
        const auto bytes = doc.memory(*memory);
        if (bytes.size() != static_cast<std::uint64_t>(capacity)*4) fail("switch mask memory size differs from capacity");
        std::vector<bool> selected(child_count);
        for (std::size_t i = 0; i < child_count; ++i) selected[i] = (word(bytes,(i/32)*4) & (1u << (i%32))) != 0;
        return selected;
    }
    void emit(const IgbObject& node, std::uint32_t geometry_ref, const SceneMatrix& world) {
        if (result.instances.size() == max_instances) fail("scene exceeds the 4,096-instance limit");
        const auto& geometry = typed(doc,geometry_ref,"igGeometryAttr1_5");
        const auto vertex_ref = doc.field_reference(geometry,4);
        if (!vertex_ref) fail("geometry has no vertex array");
        const auto& vertices = typed(doc,*vertex_ref,"igVertexArray1_1");
        if (doc.field_reference(vertices,7) || doc.field_reference(vertices,8) || doc.field_reference(vertices,10)) {
            unsupported(node,"skinned or additional vertex state requires the actor pipeline"); return;
        }
        const auto vertex_count = doc.field_u32(vertices,3);
        if (vertex_count > max_elements-result.vertex_count) fail("scene exceeds the 4,194,304-vertex limit");
        auto cached = meshes.find(geometry_ref);
        if (cached == meshes.end()) {
            if (vertex_count > max_elements-cached_vertices) fail("mesh cache exceeds the vertex limit");
            auto mesh = decode_igb_mesh(doc,geometry_ref);
            if (mesh.indices.size()/3 > max_elements-cached_triangles) fail("mesh cache exceeds the triangle limit");
            cached_vertices += mesh.positions.size(); cached_triangles += mesh.indices.size()/3;
            cached = meshes.emplace(geometry_ref,std::move(mesh)).first;
        }
        const auto& mesh = cached->second;
        if (mesh.indices.size()/3 > max_elements-result.triangle_count) fail("scene exceeds the 4,194,304-triangle limit");
        SceneInstance instance;
        instance.node_reference = node.reference_index; instance.geometry_reference = geometry_ref;
        instance.name = std::string(doc.field_string(node,2)); instance.path = path; instance.world_transform = world;
        instance.mesh.indices = mesh.indices; instance.mesh.positions.reserve(mesh.positions.size());
        for (const auto point : mesh.positions) {
            const auto world_point = transform(world,point);
            if (instance.mesh.positions.empty()) instance.mesh.bounds = {world_point,world_point};
            else extend(instance.mesh.bounds,world_point);
            instance.mesh.positions.push_back(world_point);
        }
        if (result.instances.empty()) result.bounds = instance.mesh.bounds;
        else { extend(result.bounds,instance.mesh.bounds.min); extend(result.bounds,instance.mesh.bounds.max); }
        result.vertex_count += mesh.positions.size(); result.triangle_count += mesh.indices.size()/3;
        result.instances.push_back(std::move(instance));
    }
    void visit(std::uint32_t reference, const SceneMatrix& parent) {
        if (path.size() == max_depth) fail("scene exceeds the 64-node path limit");
        if (std::find(path.begin(),path.end(),reference) != path.end()) fail("cycle in selected scene path");
        if (result.visited_nodes == max_nodes) fail("scene exceeds the 65,536-visit limit");
        ++result.visited_nodes; path.push_back(reference);
        visit_node(typed(doc,reference,"igNode"),parent);
        path.pop_back();
    }
    void visit_node(const IgbObject& node, const SceneMatrix& parent) {
        const auto type = type_name(doc,node);
        const auto flags = doc.field_u32(node,5);
        // The original geometry handler returns before state dispatch/children.
        if (type == "igGeometry" && (!boolean(doc,node,9) || (flags & 0x20u))) return;
        if (flags & ~0x1fu) { unsupported(node,"dynamic, invisible non-geometry, or unknown node flags"); return; }
        if (type == "igNode") return; // The base node handler has no traversal.
        if (type != "igGroup" && type != "beGroupKeep" && type != "igTransform" && type != "igAttrSet" &&
            type != "igGeometry" && type != "igSwitch") {
            unsupported(node,"unverified node behavior: " + type); return;
        }
        auto world = parent;
        if (type == "igTransform") {
            if (doc.field_u32(node,9) != 0 || doc.field_reference(node,11)) {
                unsupported(node,"nonzero transform target or dynamic transform input"); return;
            }
            world = multiply(parent,matrix(doc,node)); ++result.transform_nodes;
        }
        if (type == "igAttrSet" || type == "igGeometry") {
            boolean(doc,node,9); // Validate; AttrSet trigger does not suppress children.
            const auto attrs = attributes(node);
            if (!static_attributes(node,attrs)) return;
            std::vector<std::uint32_t> geometry;
            for (const auto ref : attrs) if (ref && doc.is_a(doc.object(*ref),"igGeometryAttr1_5")) geometry.push_back(*ref);
            if (type == "igGeometry") {
                if (geometry.empty()) { unsupported(node,"geometry owner has no geometry attributes"); return; }
                for (const auto ref : geometry) emit(node,ref,world);
                return;
            }
            if (!geometry.empty()) { unsupported(node,"geometry attributes on a non-geometry scope are unverified"); return; }
        }
        const auto descendants = children(node);
        auto selected = type == "igSwitch" ? switch_mask(node,descendants.size()) : std::vector<bool>{};
        if (path.size() == 1 && control) selected = control->selected_children;
        if (path.size() == 1 && result.inspected_root_child) {
            const auto child = *result.inspected_root_child;
            if (child >= descendants.size() || !descendants[child]) fail("inspection child is outside the root list or null");
            std::fill(selected.begin(),selected.end(),false); selected[child] = true;
        }
        for (std::size_t i = 0; i < descendants.size(); ++i)
            if (descendants[i] && (type != "igSwitch" || selected[i])) visit(*descendants[i],world);
    }
};
} // namespace

std::vector<SceneInfo> list_igb_scenes(const IgbDocument& document) {
    std::vector<SceneInfo> result;
    for (const auto& object : document.objects) {
        if (!document.is_a(object,"igSceneInfo")) continue;
        if (result.size() == max_instances) fail("scene descriptor list exceeds the 4,096 limit");
        result.push_back(descriptor(document,object.reference_index));
    }
    return result;
}
SceneData decode_igb_scene(const IgbDocument& document, std::uint32_t info_reference, SceneOptions options) {
    try { return Decoder(document,descriptor(document,info_reference),options).decode(); }
    catch (const IgbError& error) { throw SceneError(error.what()); }
    catch (const MeshError& error) { throw SceneError(error.what()); }
    catch (const SceneControlError& error) { throw SceneError(error.what()); }
}
} // namespace dw4
