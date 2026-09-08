#include "dw4/scene_controls.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <string_view>

namespace dw4 {
namespace {
constexpr std::uint32_t max_exports = 4096;
constexpr std::uint32_t max_elements = 4u * 1024u * 1024u;
[[noreturn]] void fail(const std::string& reason) { throw SceneControlError("Scene control: " + reason); }
const IgbObject& exact(const IgbDocument& doc, std::uint32_t ref, std::string_view name) {
    const auto& object = doc.object(ref);
    if (doc.metadata.object_types.at(object.type_index).name != name)
        fail("expected exact " + std::string(name) + " at ref " + std::to_string(ref));
    return object;
}
void field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot,
    std::string_view type, std::uint32_t size = 4) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != size || doc.metadata.field_types.at(value->type_index).name != type)
        fail("missing or unsupported serialized field at slot " + std::to_string(slot));
}
std::vector<std::optional<std::uint32_t>> list(const IgbDocument& doc, std::uint32_t ref,
    std::string_view type, std::uint32_t limit, std::uint32_t* remaining_links = nullptr) {
    const auto& object = exact(doc,ref,type);
    field(doc,object,2,"igIntMetaField"); field(doc,object,3,"igIntMetaField");
    field(doc,object,4,"igMemoryRefMetaField");
    if (doc.field_u32(object,2) > limit || doc.field_u32(object,3) > limit)
        fail("list count or capacity exceeds its limit");
    if (remaining_links) {
        const auto count = doc.field_u32(object,2);
        if (count > *remaining_links) fail("controller enumeration exceeds the aggregate 4,194,304-child-link budget");
        *remaining_links -= count;
    }
    return doc.object_list(ref);
}
SwitchControlInfo descriptor(const IgbDocument& doc, std::uint32_t ref, std::uint32_t& remaining_links) {
    SwitchControlInfo result; result.reference_index = ref;
    try {
        if (doc.metadata.header.byte_order != IgbByteOrder::little)
            fail("big-endian controller storage is unverified");
        const auto& info = exact(doc,ref,"beSwitchCtrlInfo");
        // String payload length varies; field_string validates its stored size.
        const auto* name = doc.field(info,2);
        if (!name || doc.metadata.field_types.at(name->type_index).name != "igStringMetaField")
            fail("controller name has an unsupported field type");
        const auto text = doc.field_string(info,2);
        if (text.size() > 4096) fail("controller name exceeds 4096 bytes");
        result.name = std::string(text);
        field(doc,info,7,"igObjectRefMetaField");
        const auto target = doc.field_reference(info,7);
        if (!target) fail("controller target is null");
        result.target_reference = *target;
        const auto& node = exact(doc,*target,"igSwitch");
        field(doc,node,7,"igObjectRefMetaField"); field(doc,node,8,"igObjectRefMetaField");
        if (const auto children = doc.field_reference(node,7)) {
            const auto values = list(doc,*children,"igNodeList",max_elements,&remaining_links);
            result.child_count = static_cast<std::uint32_t>(values.size());
            for (const auto child : values)
                if (child && !doc.is_a(doc.object(*child),"igNode")) fail("switch child is not an igNode");
        }
        const auto mask_ref = doc.field_reference(node,8);
        if (!mask_ref) fail("controller target has no bit mask");
        const auto& mask = exact(doc,*mask_ref,"igBitMask");
        field(doc,mask,2,"igIntMetaField"); field(doc,mask,3,"igIntMetaField");
        field(doc,mask,4,"igMemoryRefMetaField"); field(doc,mask,5,"igUnsignedIntMetaField");
        const auto count = doc.field_i32(mask,2), capacity = doc.field_i32(mask,3);
        result.mask_bit_count = doc.field_u32(mask,5);
        const auto words = (std::uint64_t(result.mask_bit_count)+31)/32;
        if (result.mask_bit_count < result.child_count || result.mask_bit_count > max_elements ||
            count < 0 || capacity < count || static_cast<std::uint64_t>(count) != words ||
            static_cast<std::uint64_t>(capacity) > (max_elements+31)/32)
            fail("switch mask counts/capacity are inconsistent or exceed limits");
        if (const auto memory = doc.field_reference(mask,4)) {
            if (doc.memory(*memory).size() != static_cast<std::uint64_t>(capacity)*4)
                fail("switch mask memory size differs from capacity");
        } else if (capacity) fail("switch mask memory is null");
        result.supported = true;
    } catch (const std::exception& error) { result.diagnostic = error.what(); }
    return result;
}
} // namespace

std::vector<SwitchControlInfo> list_igb_switch_controls(const IgbDocument& document) {
    try {
        std::vector<SwitchControlInfo> result;
        if (!document.info_list_reference) return result;
        const auto exported = list(document,*document.info_list_reference,"igInfoList",max_exports);
        auto remaining_links = max_elements;
        for (const auto ref : exported) {
            if (!ref || !document.is_a(document.object(*ref),"igInfo"))
                fail("explicit info list contains a null or non-info object");
            if (!document.is_a(document.object(*ref),"beSwitchCtrlInfo")) continue;
            if (std::any_of(result.begin(),result.end(),[&](const auto& item) { return item.reference_index == *ref; }))
                fail("explicit info list repeats a switch controller");
            result.push_back(descriptor(document,*ref,remaining_links));
        }
        return result;
    } catch (const SceneControlError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}
SwitchControlState evaluate_igb_switch_control(const IgbDocument& document, SwitchControlRequest request) {
    if (request.command != SwitchControlCommand::all_on && request.command != SwitchControlCommand::all_off)
        fail("unknown switch command");
    const auto controls = list_igb_switch_controls(document);
    const auto found = std::find_if(controls.begin(),controls.end(),[&](const auto& info) {
        return info.reference_index == request.controller_reference;
    });
    if (found == controls.end()) fail("requested controller is not on the explicit info list");
    if (!found->supported) fail(found->diagnostic);
    return {*found,request.command,std::vector<bool>(found->child_count,request.command == SwitchControlCommand::all_on)};
}
} // namespace dw4
