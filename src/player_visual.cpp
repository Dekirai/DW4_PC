#include "dw4/player_visual.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <string_view>

namespace dw4 {
namespace {
constexpr std::uint32_t max_list = 4096;
constexpr std::uint32_t max_links = 4u * 1024u * 1024u;
[[noreturn]] void fail(const std::string& reason) { throw PlayerVisualError("Player visual: " + reason); }

class Reader {
public:
    explicit Reader(const IgbDocument& document) : doc(document) {}
    const IgbObject& exact(std::uint32_t ref, std::string_view type) const {
        const auto& object = doc.object(ref);
        if (doc.metadata.object_types.at(object.type_index).name != type)
            fail("expected exact " + std::string(type) + " at ref " + std::to_string(ref));
        return object;
    }
    const IgbFieldValue& field(const IgbObject& object, std::uint16_t slot, std::string_view type) const {
        const auto* value = doc.field(object, slot);
        if (!value || doc.metadata.field_types.at(value->type_index).name != type)
            fail("unsupported or missing field at slot " + std::to_string(slot));
        return *value;
    }
    std::string text(const IgbObject& object, std::uint16_t slot) {
        const auto& stored = field(object, slot, "igStringMetaField");
        if (stored.size > 4104 || stored.size > remaining_text)
            fail("serialized names exceed the per-field or aggregate 4 MiB byte budget");
        remaining_text -= static_cast<std::uint32_t>(stored.size);
        const auto value = doc.field_string(object, slot);
        if (value.size() > 4096) fail("name exceeds 4096 bytes");
        return std::string(value);
    }
    void node_flags(const IgbObject& object) const {
        field(object, 5, "igIntMetaField");
        if (doc.field_u32(object, 5) & ~0x1fu) fail("dynamic, invisible or unknown node flags are unsupported");
    }
    std::uint32_t ref(const IgbObject& object, std::uint16_t slot) const {
        field(object, slot, "igObjectRefMetaField");
        const auto value = doc.field_reference(object, slot);
        if (!value) fail("required object reference is null");
        return *value;
    }
    void null_ref(const IgbObject& object, std::uint16_t slot, bool memory = false) const {
        field(object, slot, memory ? "igMemoryRefMetaField" : "igObjectRefMetaField");
        if (doc.field_reference(object, slot)) fail("additional actor or node state is outside the visual profile");
    }
    std::vector<std::uint32_t> list(std::uint32_t reference, std::string_view type) {
        const auto& object = exact(reference, type);
        const auto count = list_storage(object);
        if (count > remaining) fail("aggregate list validation exceeds 4,194,304 links");
        remaining -= count;
        std::vector<std::uint32_t> result;
        for (const auto item : doc.object_list(reference)) {
            if (!item) fail("required list contains a null member");
            result.push_back(*item);
        }
        return result;
    }
    void empty(const IgbObject& object, std::uint16_t slot, std::string_view type) {
        const auto& value = exact(ref(object, slot), type);
        if (list_storage(value) != 0) fail("nonempty attachments, modifiers or extra children are unsupported");
    }
    std::vector<std::uint32_t> exports() {
        if (!doc.info_list_reference) return {};
        auto values = list(*doc.info_list_reference, "igInfoList");
        std::set<std::uint32_t> seen;
        for (const auto value : values) {
            if (!doc.is_a(doc.object(value), "igInfo")) fail("explicit export is not an igInfo");
            if (!seen.insert(value).second) fail("explicit info list repeats an export reference");
        }
        return values;
    }
    std::vector<std::uint32_t> instances(const IgbObject& model) {
        const auto values = list(ref(model, 6), "beBaseInfoRamList");
        if (values.empty()) fail("player model has no explicit instances");
        for (const auto value : values) exact(value, "beBaseInfoRam");
        return values;
    }
    void matrix(const IgbObject& object, std::uint16_t slot, bool require_identity) const {
        const auto bytes = doc.field_bytes(field(object, slot, "igMatrix44fMetaField"));
        if (bytes.size() != 64) fail("matrix must contain exactly 16 floats");
        for (std::size_t i = 0; i < 16; ++i) {
            std::uint32_t bits{};
            for (std::size_t b = 0; b < 4; ++b) bits |= std::uint32_t(bytes[i * 4 + b]) << (8 * b);
            const auto value = std::bit_cast<float>(bits);
            if (!std::isfinite(value)) fail("matrix contains a nonfinite component");
            const auto expected = i % 5 == 0 ? 1.f : 0.f;
            if ((require_identity || i == 3 || i == 7 || i == 11 || i == 15) && value != expected)
                fail(require_identity ? "actor or lower transform is not identity" : "authored model matrix is not affine");
        }
    }
    std::vector<std::uint32_t> attributes(const IgbObject& node) {
        const auto values = list(ref(node, 8), "igAttrList");
        for (const auto value : values) {
            const auto& object = doc.object(value);
            const auto& type = doc.metadata.object_types.at(object.type_index).name;
            if (!doc.is_a(object, "igAttr") ||
                (type != "igGeometryAttr1_5" && type != "igVertexBlendStateAttr" && type != "igTextureBindAttr" &&
                type != "igTextureStateAttr" && type != "igMaterialAttr" && type != "igShadeModelAttr" &&
                type != "igBlendFunctionAttr" && type != "igBlendStateAttr"))
                fail("attribute behavior is outside the visual position profile: " + type);
        }
        return values;
    }
private:
    const IgbDocument& doc;
    std::uint32_t remaining{max_links};
    std::uint32_t remaining_text{4u * 1024u * 1024u};
    std::uint32_t list_storage(const IgbObject& object) const {
        field(object, 2, "igIntMetaField"); field(object, 3, "igIntMetaField");
        field(object, 4, "igMemoryRefMetaField");
        const auto count = doc.field_u32(object, 2), capacity = doc.field_u32(object, 3);
        if (count > capacity || capacity > max_list) fail("list count or capacity exceeds the 4096-entry profile");
        const auto memory = doc.field_reference(object, 4);
        if (memory) {
            if (doc.memory(*memory).size() != std::uint64_t(capacity) * 4) fail("list memory differs from declared capacity");
        } else if (capacity) fail("nonempty list storage is null");
        return count;
    }
};

void graph(const IgbDocument& doc, PlayerVisualInfo& info) {
    if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("big-endian player visuals are unverified");
    Reader reader(doc);
    const auto exported = reader.exports();
    if (std::find(exported.begin(), exported.end(), info.request.model_reference) == exported.end())
        fail("selected player model is not an explicit info export");
    const auto& model = reader.exact(info.request.model_reference, "beModelCtrlInfo");
    if (reader.text(model, 7) != "KIND_PLAYER") fail("selected model is not KIND_PLAYER");
    info.model_name = reader.text(model, 2);
    const auto instances = reader.instances(model);
    if (info.request.instance_index >= instances.size()) fail("instance index is outside the original RAM list");
    const auto instance_ref = instances[info.request.instance_index];
    const auto& instance = doc.object(instance_ref);
    const auto data = reader.list(reader.ref(instance, 10), "beBaseInfoDataList");
    if (data.size() != 1) fail("instance requires exactly one ONE visual data record");
    const auto& one = reader.exact(data[0], "beModelCtrlInfoRamData");
    if (reader.text(one, 2) != "ONE") fail("instance visual data is not the exact ONE record");
    reader.null_ref(one, 5);
    info.model_node_reference = reader.ref(one, 4);
    const auto& node = reader.exact(info.model_node_reference, "beModelCtrlNode2");
    reader.node_flags(node);
    reader.matrix(node, 8, false);
    reader.field(node, 9, "igIntMetaField");
    if (doc.field_u32(node, 9) != 0) fail("additional model transform behavior is unverified");
    reader.null_ref(node, 11);
    reader.empty(node, 15, "igAttrList");
    const auto children = reader.list(reader.ref(node, 7), "igNodeList");
    if (children.size() != 1) fail("model node must contain exactly one igLod");
    info.lod_reference = children[0];
    const auto& lod = reader.exact(info.lod_reference, "igLod");
    reader.node_flags(lod);
    reader.null_ref(lod, 14);
    const auto branches = reader.list(reader.ref(lod, 7), "igNodeList");
    info.branch_count = static_cast<std::uint32_t>(branches.size());
    for (const auto branch : branches) reader.exact(branch, "igActor");
    if (info.request.branch_index >= branches.size()) fail("explicit LOD branch index is out of range");
    info.actor_reference = branches[info.request.branch_index];
    const auto& actor = reader.exact(info.actor_reference, "igActor");
    reader.node_flags(actor);
    reader.matrix(actor, 14, true);
    reader.empty(actor, 7, "igNodeList");
    reader.null_ref(actor, 8); reader.null_ref(actor, 9, true); reader.null_ref(actor, 10, true); reader.null_ref(actor, 12);
    reader.empty(actor, 13, "igAnimationModifierList");
    info.appearance_reference = reader.ref(actor, 11);
    const auto& appearance = reader.exact(info.appearance_reference, "igAppearance");
    reader.empty(appearance, 4, "igSkinList");
    reader.empty(appearance, 5, "igModelViewMatrixBoneSelectList");
    reader.empty(appearance, 6, "igStringObjList");
    reader.empty(appearance, 7, "igNodeList");
    info.skin_reference = reader.ref(appearance, 3);
    const auto& skin = reader.exact(info.skin_reference, "igSkin");
    const auto selector_ref = reader.ref(skin, 3);
    const auto& selector = reader.exact(selector_ref, "igBlendMatrixSelect");
    reader.node_flags(selector);
    reader.matrix(selector, 11, true); reader.matrix(selector, 12, true);
    for (const auto attribute : reader.attributes(selector))
        if (doc.metadata.object_types.at(doc.object(attribute).type_index).name == "igGeometryAttr1_5")
            fail("additional geometry on the selector scope is unsupported");
    const auto geometry_nodes = reader.list(reader.ref(selector, 7), "igNodeList");
    if (geometry_nodes.size() != 1) fail("skin selector must contain exactly one geometry node");
    const auto& geometry = reader.exact(geometry_nodes[0], "igGeometry");
    reader.node_flags(geometry);
    const auto trigger = doc.field_bytes(reader.field(geometry, 9, "igBoolMetaField"));
    if (trigger.size() != 1 || trigger[0] != 1) fail("geometry trigger must be explicitly enabled");
    reader.empty(geometry, 7, "igNodeList");
    const auto attributes = reader.attributes(geometry);
    std::vector<std::uint32_t> meshes;
    for (const auto attribute : attributes)
        if (doc.metadata.object_types.at(doc.object(attribute).type_index).name == "igGeometryAttr1_5") meshes.push_back(attribute);
    if (meshes.size() != 1) fail("geometry node must contain exactly one mesh attribute");
    info.geometry_reference = meshes[0];
    info.provenance = "Exported player model " + std::to_string(info.request.model_reference) + " -> instance " +
        std::to_string(info.request.instance_index) + " (RAM ref " + std::to_string(instance_ref) + ") -> ONE " +
        std::to_string(data[0]) + " -> model node " + std::to_string(info.model_node_reference) + " -> LOD " +
        std::to_string(info.lod_reference) + " branch " + std::to_string(info.request.branch_index) + " -> actor " +
        std::to_string(info.actor_reference) + " -> appearance " + std::to_string(info.appearance_reference) + " -> skin " +
        std::to_string(info.skin_reference) + " -> selector " + std::to_string(selector_ref) + " -> geometry " +
        std::to_string(info.geometry_reference) + "; authored model matrix excluded at the original player placement boundary";
}

void verify_skin(const PlayerVisualInfo& info, const SkinningInfo& skin) {
    if (!skin.supported || skin.geometry_reference != info.geometry_reference || skin.skin_reference != info.skin_reference ||
        std::find(skin.actor_references.begin(), skin.actor_references.end(), info.actor_reference) == skin.actor_references.end())
        fail("decoded skin provenance does not match the selected player visual chain");
}
} // namespace

std::vector<PlayerModelInfo> list_igb_player_models(const IgbDocument& document) {
    try {
        Reader reader(document);
        std::vector<PlayerModelInfo> result;
        for (const auto ref : reader.exports()) {
            const auto& object = document.object(ref);
            if (document.metadata.object_types.at(object.type_index).name != "beModelCtrlInfo" || reader.text(object, 7) != "KIND_PLAYER") continue;
            PlayerModelInfo info; info.reference_index = ref;
            try {
                info.name = reader.text(object, 2);
                info.instance_count = static_cast<std::uint32_t>(reader.instances(object).size());
                info.supported = true;
            } catch (const std::exception& error) { info.diagnostic = error.what(); }
            result.push_back(std::move(info));
        }
        return result;
    } catch (const PlayerVisualError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}

PlayerVisualInfo inspect_igb_player_visual(const IgbDocument& document, PlayerVisualRequest request) {
    PlayerVisualInfo info; info.request = request;
    try {
        graph(document, info);
        const auto skin = inspect_igb_skinning(document, info.geometry_reference);
        if (!skin.supported) fail(skin.diagnostic);
        verify_skin(info, skin); info.supported = true;
    } catch (const std::exception& error) { info.diagnostic = error.what(); }
    return info;
}

PlayerVisualData decode_igb_player_visual(const IgbDocument& document, PlayerVisualRequest request) {
    try {
        PlayerVisualData result; result.info.request = request;
        graph(document, result.info);
        result.skin = decode_igb_skinning(document, result.info.geometry_reference);
        verify_skin(result.info, result.skin.info);
        result.mesh = decode_igb_mesh(document, result.info.geometry_reference);
        result.info.supported = true;
        return result;
    } catch (const PlayerVisualError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}
} // namespace dw4
