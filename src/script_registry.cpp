#include "dw4/script_registry.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t max_registrations = 4096;
constexpr std::size_t max_sources = 64;
constexpr std::size_t max_rows = 65536;
constexpr std::size_t max_metadata_bytes = 16u * 1024u * 1024u;
constexpr std::size_t max_payload_bytes = 64u * 1024u * 1024u;
[[noreturn]] void fail(const std::string& reason) {
    throw ScriptError("Script registry: " + reason);
}
void text(std::string_view value, std::string_view kind) {
    if (value.size() > 4096 || value.find('\0') != std::string_view::npos)
        fail(std::string(kind) + " exceeds 4,096 bytes or contains an embedded NUL");
}
void charge(std::size_t amount, std::size_t& used, std::size_t limit, std::string_view kind) {
    if (amount > limit - used) fail(std::string(kind) + " exceeds its aggregate bound");
    used += amount;
}
void charge_row(const EmbeddedScriptInfo& row, std::size_t& used) {
    for (const auto* value : {&row.info_name,&row.name,&row.diagnostic,&row.provenance})
        charge(value->size(),used,max_metadata_bytes,"owned metadata");
}
struct Catalog {
    std::vector<std::optional<std::uint32_t>> exports;
    std::vector<EmbeddedScriptInfo> rows;
};
} // namespace

ScriptRegistry::ScriptRegistry(std::span<const ScriptRegistration> registrations) {
    if (registrations.size() > max_registrations) fail("registration count exceeds 4,096");
    std::map<std::uint32_t,std::pair<const IgbDocument*,std::string>> sources;
    std::map<const IgbDocument*,Catalog> catalogs;
    std::size_t catalog_rows{}, owned_rows{}, metadata_bytes{}, payload_bytes{};
    registrations_.reserve(registrations.size());
    scripts_.reserve(registrations.size());
    try {
        for (const auto& request : registrations) {
            if (!request.document) fail("registration has no source document");
            text(request.source_label,"source label");
            const auto source = sources.find(request.source_occurrence);
            if (source != sources.end()) {
                if (source->second.first != request.document || source->second.second != request.source_label)
                    fail("one source occurrence identifies different documents or labels");
            } else {
                if (sources.size() == max_sources) fail("source occurrence count exceeds 64");
                sources.emplace(request.source_occurrence,std::pair{request.document,request.source_label});
                charge(request.source_label.size(),metadata_bytes,max_metadata_bytes,"owned metadata");
            }
            auto catalog = catalogs.find(request.document);
            if (catalog == catalogs.end()) {
                Catalog value;
                // The existing reader validates all explicit ownership lists and
                // retains malformed individual rows as ordered barriers.
                value.rows = list_igb_scripts(*request.document);
                charge(value.rows.size(),catalog_rows,max_rows,"validated catalog rows");
                for (const auto& row : value.rows) charge_row(row,metadata_bytes);
                if (request.document->info_list_reference)
                    value.exports = request.document->object_list(*request.document->info_list_reference);
                catalog = catalogs.emplace(request.document,std::move(value)).first;
            }
            const auto& exports = catalog->second.exports;
            if (request.info_export_index >= exports.size() ||
                exports[request.info_export_index] != request.info_reference)
                fail("selected reference does not match its explicit info-export ordinal");
            const auto& info = request.document->object(request.info_reference);
            if (request.document->metadata.object_types.at(info.type_index).name != "beLuaInfo")
                fail("selected export is not an exact beLuaInfo");

            charge(request.source_label.size(),metadata_bytes,max_metadata_bytes,"owned metadata");
            registrations_.push_back({static_cast<std::uint32_t>(registrations_.size()),
                request.source_label,request.source_occurrence,request.info_export_index,request.info_reference});
            auto& owned = scripts_.emplace_back();
            const auto& rows = catalog->second.rows;
            auto row = std::lower_bound(rows.begin(),rows.end(),request.info_export_index,
                [](const EmbeddedScriptInfo& item, std::uint32_t index) { return item.info_export_index < index; });
            for (; row != rows.end() && row->info_export_index == request.info_export_index; ++row) {
                charge(1,owned_rows,max_rows,"registered script rows");
                charge_row(*row,metadata_bytes);
                EmbeddedScript script; script.info = *row;
                if (row->supported) {
                    // list_igb_scripts has checked the precise payload reference,
                    // size and profile. Copy that validated buffer once; calling
                    // read_igb_script per row would rescan the complete catalog.
                    const auto bytes = request.document->memory(row->memory_reference);
                    charge(bytes.size(),payload_bytes,max_payload_bytes,"owned script bytes");
                    script.bytes.assign(bytes.begin(),bytes.end());
                }
                owned.push_back(std::make_shared<const EmbeddedScript>(std::move(script)));
            }
        }
    } catch (const ScriptError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}

ScriptLookupPlan ScriptRegistry::lookup(std::string_view filename) const {
    text(filename,"filename");
    ScriptLookupPlan result; result.filename = filename;
    result.entries.reserve(registrations_.size());
    for (std::size_t i = 0; i < registrations_.size(); ++i) {
        ScriptLookupEntry entry; entry.registration = registrations_[i];
        for (const auto& script : scripts_[i]) {
            if (!script->info.name_valid) {
                entry.state = ScriptLookupState::name_barrier; entry.script = script; break;
            }
            if (script->info.name != filename) continue;
            entry.state = script->info.supported ? ScriptLookupState::match : ScriptLookupState::unsupported_match;
            entry.script = script; break;
        }
        result.entries.push_back(std::move(entry));
    }
    return result;
}
} // namespace dw4
