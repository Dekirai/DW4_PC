#include "dw4/scripts.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace dw4 {
namespace {
constexpr std::uint32_t null_reference = 0xffffffffu;
constexpr std::uint32_t max_exports = 4096;
constexpr std::uint32_t max_scripts = 65536;
constexpr std::size_t max_name_bytes = 4u * 1024u * 1024u;
constexpr std::size_t max_script_bytes = 8u * 1024u * 1024u;
constexpr std::size_t max_total_script_bytes = 64u * 1024u * 1024u;
struct Budget { std::uint32_t exports{}, scripts{}; std::size_t names{}, bytes{}; };
[[noreturn]] void fail(const std::string& reason) { throw ScriptError("Embedded scripts: " + reason); }
const IgbObject& exact(const IgbDocument& doc, std::uint32_t ref, std::string_view expected) {
    const auto& object = doc.object(ref);
    if (doc.metadata.object_types.at(object.type_index).name != expected)
        fail("expected exact " + std::string(expected) + " at ref " + std::to_string(ref));
    return object;
}
void field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot, std::string_view expected) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != 4 || doc.metadata.field_types.at(value->type_index).name != expected)
        fail("missing or unsupported field at slot " + std::to_string(slot));
}
std::uint32_t reference(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, std::string_view type = "igObjectRefMetaField") {
    field(doc,object,slot,type);
    const auto ref = doc.field_reference(object,slot);
    if (!ref) fail("required reference is null");
    return *ref;
}
std::vector<std::optional<std::uint32_t>> list(const IgbDocument& doc,
    std::uint32_t ref, std::string_view expected, std::uint32_t limit) {
    const auto& object = exact(doc,ref,expected);
    field(doc,object,2,"igIntMetaField"); field(doc,object,3,"igIntMetaField");
    field(doc,object,4,"igMemoryRefMetaField");
    if (doc.field_u32(object,2) > limit || doc.field_u32(object,3) > limit)
        fail("list count or capacity exceeds its remaining expansion budget");
    return doc.object_list(ref);
}
std::string name(const IgbDocument& doc, const IgbObject& object, Budget& budget) {
    const auto* value = doc.field(object,2);
    if (!value || doc.metadata.field_types.at(value->type_index).name != "igStringMetaField")
        fail("name has an unsupported field type");
    // Charge serialized bytes before scanning/trimming, including repeated aliases.
    if (value->size < 4 || value->size > 4104 || value->size > max_name_bytes-budget.names)
        fail("serialized name exceeds its size or cumulative expansion budget");
    budget.names += value->size;
    const auto text = doc.field_string(object,2);
    if (text.size() > 4096 || text.find('\0') != std::string_view::npos)
        fail("name exceeds 4,096 bytes or contains an embedded NUL");
    return std::string(text);
}
std::string provenance(const EmbeddedScriptInfo& row) {
    return "info export " + std::to_string(row.info_export_index) + ", info ref " +
        std::to_string(row.info_reference) + ", data index " + std::to_string(row.data_index) +
        ", data ref " + std::to_string(row.data_reference) + ", object ref " +
        std::to_string(row.data_object_reference) + ", memory ref " + std::to_string(row.memory_reference);
}
std::vector<EmbeddedScriptInfo> validate(const IgbDocument& doc, Budget& budget) {
    try {
        std::vector<EmbeddedScriptInfo> results;
        if (doc.metadata.header.byte_order != IgbByteOrder::little)
            fail("big-endian script storage is outside the verified profile");
        if (!doc.info_list_reference) return results;
        const auto exports = list(doc,*doc.info_list_reference,"igInfoList",max_exports-budget.exports);
        budget.exports += static_cast<std::uint32_t>(exports.size());
        for (std::uint32_t e = 0; e < exports.size(); ++e) {
            if (!exports[e] || !doc.is_a(doc.object(*exports[e]),"igInfo"))
                fail("info list contains a null or non-info export");
            const auto& info = doc.object(*exports[e]);
            if (!doc.is_a(info,"beLuaInfo")) continue;
            exact(doc,*exports[e],"beLuaInfo");
            const auto info_name = name(doc,info,budget);
            const auto scripts = list(doc,reference(doc,info,7),"beLuaDataList",max_scripts-budget.scripts);
            budget.scripts += static_cast<std::uint32_t>(scripts.size());
            for (std::uint32_t i = 0; i < scripts.size(); ++i) {
                EmbeddedScriptInfo row;
                row.info_reference = *exports[e]; row.info_export_index = e; row.data_index = i;
                // Account for each owned metadata copy, not only its first decoding.
                if (info_name.size() > max_name_bytes-budget.names)
                    fail("expanded info names exceed their cumulative budget");
                budget.names += info_name.size(); row.info_name = info_name;
                row.data_reference = scripts[i].value_or(null_reference);
                try {
                    if (!scripts[i]) fail("script-list element is null");
                    const auto& data = exact(doc,*scripts[i],"beLuaData");
                    row.name = name(doc,data,budget); row.name_valid = true;
                    row.data_object_reference = reference(doc,data,3);
                    const auto& object = exact(doc,row.data_object_reference,"beLuaDataObject");
                    field(doc,object,2,"igIntMetaField");
                    row.byte_size = doc.field_u32(object,2);
                    if (row.byte_size > max_script_bytes) fail("script exceeds 8 MiB");
                    row.memory_reference = reference(doc,object,3,"igMemoryRefMetaField");
                    const auto bytes = doc.memory(row.memory_reference);
                    if (bytes.size() != row.byte_size) fail("declared script size differs from memory size");
                    if (bytes.size() > max_total_script_bytes-budget.bytes)
                        fail("expanded script bytes exceed 64 MiB");
                    budget.bytes += bytes.size();
                    row.supported = true;
                } catch (const std::exception& error) { row.diagnostic = error.what(); }
                row.provenance = provenance(row);
                results.push_back(std::move(row));
            }
        }
        return results;
    } catch (const ScriptError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}
StartupScriptPhase phase(StartupPhaseKind kind, std::string filename,
    std::vector<StartupNativeBarrier> prerequisites = {}) {
    StartupScriptPhase result; result.kind = kind; result.filename = std::move(filename);
    result.native_prerequisites = std::move(prerequisites); result.lookup_complete = true;
    return result;
}
} // namespace

std::vector<EmbeddedScriptInfo> list_igb_scripts(const IgbDocument& document) {
    Budget budget; return validate(document,budget);
}
EmbeddedScript read_igb_script(const IgbDocument& document,
    std::uint32_t info_reference, std::uint32_t data_index) {
    Budget budget; auto rows = validate(document,budget);
    const auto found = std::find_if(rows.begin(),rows.end(),[&](const auto& row) {
        return row.info_reference == info_reference && row.data_index == data_index;
    });
    if (found == rows.end()) fail("requested script is not an explicit exported info/data-list selection");
    if (!found->supported) fail(found->diagnostic);
    try {
        const auto bytes = document.memory(found->memory_reference);
        return {std::move(*found),{bytes.begin(),bytes.end()}};
    } catch (const ScriptError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}
StartupPlan build_igb_startup_plan(std::span<const StartupSource> sources) {
    if (sources.size() > 64) fail("startup source count exceeds 64");
    StartupPlan plan; Budget budget;
    std::vector<std::vector<EmbeddedScriptInfo>> documents; documents.reserve(sources.size());
    for (const auto& source : sources) {
        if (!source.document) fail("startup source document is null");
        if (source.label.size() > 4096 || source.label.size() > max_name_bytes-budget.names)
            fail("startup source label exceeds its size or cumulative budget");
        budget.names += source.label.size(); plan.source_labels.push_back(source.label);
        documents.push_back(validate(*source.document,budget));
    }
    plan.phases.push_back(phase(StartupPhaseKind::functions,"func.lua",{
        {StartupBarrierKind::package_members_loaded,"Complete original package member loading."},
        {StartupBarrierKind::lua_infos_registered,"Register exported Lua infos in the supplied source order."},
        {StartupBarrierKind::lua_state_reset,"Reset the original Lua state before running func.lua."}}));
    plan.phases.push_back(phase(StartupPhaseKind::boot,"boot.lua"));
    plan.phases.push_back(phase(StartupPhaseKind::start,"start.lua",{
        {StartupBarrierKind::common_resources_loaded,"Complete boot-requested common resource loading and conditions."},
        {StartupBarrierKind::deferred_infos_registered,"Register deferred loaded info records."},
        {StartupBarrierKind::layers_ordered,"Perform the original layer ORDER and related setup."},
        {StartupBarrierKind::text_initialized,"Complete TEXTCTL INITIALIZE."}}));
    plan.phases.push_back(phase(StartupPhaseKind::first,"first.lua",{
        {StartupBarrierKind::stage_enabled,"Set the original stage-enabled system state."}}));
    plan.embedded_lookup_complete = true;
    for (auto& item : plan.phases) {
        for (std::uint32_t s = 0; s < documents.size(); ++s) {
            const auto& rows = documents[s];
            for (std::size_t begin = 0; begin < rows.size();) {
                std::size_t end = begin+1;
                while (end < rows.size() && rows[end].info_export_index == rows[begin].info_export_index) ++end;
                for (std::size_t i = begin; i < end; ++i) {
                    const auto& row = rows[i];
                    if (!row.name_valid) {
                        item.lookup_complete = false;
                        item.diagnostics.push_back("Source " + std::to_string(s) + ": cannot establish first name match; " +
                            row.provenance + ": " + row.diagnostic);
                        break;
                    }
                    if (row.name != item.filename) continue;
                    item.calls.push_back({s,row});
                    if (!row.supported) {
                        item.lookup_complete = false;
                        item.diagnostics.push_back("Source " + std::to_string(s) + ": first matching embedded script is unsupported; " +
                            row.provenance + ": " + row.diagnostic);
                    }
                    break; // Only the inner info-list search ends in the original.
                }
                begin = end;
            }
        }
        if (item.calls.empty()) {
            item.lookup_complete = false;
            item.diagnostics.push_back("No embedded match for " + item.filename + "; original filesystem fallback is unsupported.");
        }
        plan.embedded_lookup_complete &= item.lookup_complete;
    }
    return plan;
}
} // namespace dw4
