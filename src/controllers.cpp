#include "dw4/controllers.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <stdexcept>

namespace dw4 {
namespace {
constexpr std::size_t max_sources = 128, max_rows = 65536, max_selected = 256;
constexpr std::size_t max_text = 8u * 1024u * 1024u;
constexpr const char* later_barrier = "Original loading-screen state, remaining controller callbacks, layer/text initialization and start/first execution are not established by this callback context.";
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Controller initialization: " + message); }
void charge(std::size_t amount, std::size_t& used, std::size_t limit, const char* label) {
    if (amount > limit - used) fail(std::string(label) + " budget exceeded.");
    used += amount;
}
template<class T> void reserve_extra(std::vector<T>& values, std::size_t count) {
    const auto required = values.size() + count;
    if (required > values.capacity()) values.reserve(std::min(max_rows, std::max(required, values.capacity() * 2)));
}
const IgbFieldValue& field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, const char* type, std::size_t size = 4) {
    const auto* value = doc.field(object, slot);
    if (!value || value->size != size || doc.metadata.field_types.at(value->type_index).name != type)
        fail("Unsupported serialized field at slot " + std::to_string(slot) + ".");
    return *value;
}
const IgbObject& exact(const IgbDocument& doc, std::uint32_t ref, const char* type) {
    const auto& object = doc.object(ref);
    if (doc.metadata.object_types.at(object.type_index).name != type) fail(std::string("Expected exact ") + type + ".");
    return object;
}
std::vector<std::optional<std::uint32_t>> list(const IgbDocument& doc, std::uint32_t ref,
    const char* type, std::size_t limit) {
    const auto& object = exact(doc, ref, type);
    field(doc, object, 2, "igIntMetaField"); field(doc, object, 3, "igIntMetaField");
    field(doc, object, 4, "igMemoryRefMetaField");
    if (doc.field_u32(object, 2) > limit || doc.field_u32(object, 3) > limit)
        fail("Object list exceeds its count/capacity limit.");
    return doc.object_list(ref);
}
std::string name(const IgbDocument& doc, const IgbObject& object, std::size_t& text) {
    const auto* value = doc.field(object, 2);
    if (!value || value->size < 4 || value->size > 4104 ||
        doc.metadata.field_types.at(value->type_index).name != "igStringMetaField") fail("Unsupported info name field.");
    charge(value->size, text, max_text, "Serialized name");
    const auto result = doc.field_string(object, 2);
    if (result.size() > 4096 || result.find('\0') != std::string_view::npos) fail("Invalid info name.");
    charge(result.size(), text, max_text, "Copied name");
    return std::string(result);
}
ControllerObjectId key(ControllerObjectId object) { return object; }
const char* manager_name(ControllerKind kind) { return kind == ControllerKind::static_info ? "beStaticInfoCtl" : "beSwitchCtrl"; }
const char* category_name(ControllerKind kind) { return kind == ControllerKind::static_info ? "STATICINFO" : "SWITCHCTRL"; }

ControllerInitialization initialize(std::span<const StartupSource> sources,
    std::span<const StartupInfoAddress> deferred, std::span<const std::uint32_t> selected,
    ControllerPrerequisites prerequisites, bool prefix) {
    ControllerInitialization result; result.ordered_prefix = prefix; result.next_barrier = later_barrier;
    std::size_t text = 0, visits = 0, exports = 0;
    std::map<ControllerObjectId, std::uint32_t> ram_indices;
    std::vector<std::optional<std::vector<std::optional<std::uint32_t>>>> source_exports;
    try {
        if (sources.size() > max_sources || deferred.size() > max_rows || (!prefix && selected.size() > max_selected))
            fail("Source, deferred-row or selection limit exceeded.");
        if (!prerequisites.system_prepared || !prerequisites.messenger_prepared)
            fail("A prepared native system and messenger context must be supplied explicitly.");
        for (const auto& source : sources) {
            if (!source.document) fail("Null source document.");
            if (source.label.size() > 4096) fail("Source label exceeds 4,096 bytes.");
            charge(source.label.size(), text, max_text, "Source label");
        }
        source_exports.resize(sources.size());
        result.managers = {{ControllerKind::static_info, {}, {}, false}, {ControllerKind::switch_control, {}, {}, false}};
        result.active_managers.reserve(3); result.categories.reserve(2);
        // Messenger's verified insertion initializer activates it before infos.
        result.active_managers.push_back("beMessenger");
        for (const auto ordinal : selected) {
            ControllerRegistrationStep step; step.deferred_index = ordinal;
            try {
                if (ordinal >= deferred.size()) fail("Selected deferred index is outside the supplied list.");
                step.source = deferred[ordinal];
                const auto address = step.source;
                if (address.file_index >= sources.size()) fail("Deferred source index is outside the loaded files.");
                const auto& doc = *sources[address.file_index].document;
                if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("Big-endian controller records are unverified.");
                auto& cached = source_exports[address.file_index];
                if (!cached) {
                    if (!doc.info_list_reference) fail("Source has no explicit info export list.");
                    auto rows = list(doc, *doc.info_list_reference, "igInfoList", 4096);
                    charge(rows.size(), exports, max_rows, "Combined export"); cached = std::move(rows);
                }
                if (address.export_index >= cached->size() || !(*cached)[address.export_index] ||
                    *(*cached)[address.export_index] != address.reference_index) fail("Deferred address does not identify its explicit export occurrence.");
                const auto& info = doc.object(address.reference_index);
                const auto& type = doc.metadata.object_types.at(info.type_index).name;
                if (type.size() > 256) fail("Controller type name exceeds its limit.");
                charge(type.size(), text, max_text, "Controller type"); step.type = type;
                step.name = name(doc, info, text);
                ControllerKind kind;
                if (type == "beStaticInfo") kind = ControllerKind::static_info;
                else if (type == "beSwitchCtrlInfo") kind = ControllerKind::switch_control;
                else fail("Unsupported native callback for " + type + "; no later deferred info was processed.");
                field(doc, info, 6, "igObjectRefMetaField");
                const auto ram_list = doc.field_reference(info, 6);
                if (!ram_list) fail("Null RAM list is not an empty accepted list.");
                const auto rows = list(doc, *ram_list, "beBaseInfoRamList", max_rows);
                charge(rows.size(), visits, max_rows, "Combined RAM occurrence");
                auto& manager = result.managers[kind == ControllerKind::static_info ? 0 : 1];
                std::vector<ControllerRamState> pending_rams;
                std::vector<ControllerRecipient> pending_recipients;
                std::map<ControllerObjectId, std::uint32_t> additions;
                pending_rams.reserve(rows.size()); pending_recipients.reserve(rows.size()); step.ram_indices.reserve(rows.size());
                const std::string category = category_name(kind);
                for (std::uint32_t r = 0; r < rows.size(); ++r) {
                    if (!rows[r]) fail("Null RAM list element cannot be initialized.");
                    const auto& stored = exact(doc, *rows[r], "beBaseInfoRam");
                    field(doc, stored, 2, "igIntMetaField");
                    const auto& is_run = field(doc, stored, 5, "igBoolMetaField", 1);
                    for (std::uint16_t slot = 6; slot <= 9; ++slot) field(doc, stored, slot, "igIntMetaField");
                    field(doc, stored, 10, "igObjectRefMetaField");
                    const auto flag = doc.field_bytes(is_run)[0];
                    if (flag > 1) fail("RAM _isRun has an unsupported Boolean representation.");
                    ControllerRamState ram{{address.file_index, *rows[r]}, static_cast<std::int32_t>(r),
                        {address.file_index, address.reference_index}, kind, flag != 0, {}, {}};
                    if (const auto data = doc.field_reference(stored, 10)) ram.data_list = ControllerObjectId{address.file_index, *data};
                    const auto identity = key(ram.object);
                    auto found = ram_indices.find(identity);
                    std::uint32_t index;
                    if (found != ram_indices.end()) index = found->second;
                    else {
                        const auto [added, inserted] = additions.try_emplace(identity,
                            static_cast<std::uint32_t>(result.rams.size() + additions.size()));
                        (void)inserted; index = added->second;
                    }
                    charge(step.name.size() + category.size(), text, max_text, "Recipient name");
                    pending_rams.push_back(ram); step.ram_indices.push_back(index);
                    pending_recipients.push_back({category, step.name, static_cast<std::int32_t>(r), index});
                }
                // Validate and allocate before publishing this callback's effects.
                // Source metadata errors cannot leave a half-accepted info behind.
                auto category_it = std::find_if(result.categories.begin(), result.categories.end(),
                    [&](const auto& item) { return item.name == category; });
                ControllerCategory pending_category{category, {}};
                if (!rows.empty()) {
                    if (category_it == result.categories.end()) pending_category.recipient_indices.reserve(rows.size());
                    else reserve_extra(category_it->recipient_indices, rows.size());
                }
                reserve_extra(result.rams, additions.size());
                reserve_extra(result.recipients, rows.size());
                reserve_extra(result.registered_infos, 1);
                reserve_extra(result.steps, 1);
                reserve_extra(manager.accepted_infos, 1);
                if (kind == ControllerKind::static_info) reserve_extra(manager.static_data_infos, rows.size());
                std::string activation = manager_name(kind);
                const auto new_size = result.rams.size() + additions.size();
                result.rams.resize(new_size); ram_indices.merge(additions);
                manager.accepted_infos.push_back(address);
                if (!rows.empty() && category_it == result.categories.end()) {
                    result.categories.push_back(std::move(pending_category)); category_it = result.categories.end() - 1;
                }
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    result.rams[step.ram_indices[r]] = pending_rams[r];
                    category_it->recipient_indices.push_back(static_cast<std::uint32_t>(result.recipients.size()));
                    result.recipients.push_back(std::move(pending_recipients[r]));
                    if (kind == ControllerKind::static_info) manager.static_data_infos.push_back({address.file_index, address.reference_index});
                }
                if (!manager.is_update_run) result.active_managers.push_back(std::move(activation));
                manager.is_update_run = true;
                result.registered_infos.push_back(address); step.completed = true;
                result.steps.push_back(std::move(step));
            } catch (const std::exception& error) {
                step.ram_indices.clear(); step.diagnostic = error.what();
                result.diagnostic = step.diagnostic; result.next_barrier = step.diagnostic + " " + later_barrier;
                result.stopped_deferred_index = ordinal; result.steps.push_back(std::move(step)); return result;
            }
        }
        result.completed = true;
    } catch (const std::exception& error) { result.diagnostic = error.what(); result.next_barrier = result.diagnostic + " " + later_barrier; }
    return result;
}
std::vector<StartupSource> session_sources(const StartupSession& session) {
    if (!session.files_loaded || !session.info_routing_complete) fail("Session files and info routing must be complete.");
    if (session.files.size() > max_sources) fail("Session exceeds the source limit.");
    std::vector<StartupSource> sources; sources.reserve(session.files.size());
    for (const auto& file : session.files) {
        if (file.requested_path.size() > 4096) fail("Session source label exceeds 4,096 bytes.");
        sources.push_back({&file.document, file.requested_path});
    }
    const auto routes = route_startup_infos(sources);
    if (routes.deferred_infos != session.infos.deferred_infos || routes.lua_infos != session.infos.lua_infos ||
        routes.held_scenes != session.infos.held_scenes || routes.late_scenes != session.infos.late_scenes)
        fail("Session routing does not match its owned source documents.");
    return sources;
}
ControllerInitialization session_failure(const std::exception& error, bool prefix) {
    ControllerInitialization result; result.ordered_prefix = prefix;
    result.diagnostic = error.what(); result.next_barrier = result.diagnostic + " " + later_barrier; return result;
}
} // namespace

ControllerInitialization initialize_controller_callbacks(std::span<const StartupSource> sources,
    std::span<const StartupInfoAddress> deferred, std::span<const std::uint32_t> selected, ControllerPrerequisites prerequisites) {
    return initialize(sources, deferred, selected, prerequisites, false);
}
ControllerInitialization initialize_controller_prefix(std::span<const StartupSource> sources,
    std::span<const StartupInfoAddress> deferred, ControllerPrerequisites prerequisites) {
    if (deferred.size() > max_rows) return initialize(sources, deferred, {}, prerequisites, true);
    std::vector<std::uint32_t> selected(deferred.size()); std::iota(selected.begin(), selected.end(), 0u);
    return initialize(sources, deferred, selected, prerequisites, true);
}
ControllerInitialization initialize_controller_callbacks(const StartupSession& session,
    std::span<const std::uint32_t> selected, ControllerPrerequisites prerequisites) {
    try { return initialize_controller_callbacks(session_sources(session), session.infos.deferred_infos, selected, prerequisites); }
    catch (const std::exception& error) { return session_failure(error, false); }
}
ControllerInitialization initialize_controller_prefix(const StartupSession& session, ControllerPrerequisites prerequisites) {
    try { return initialize_controller_prefix(session_sources(session), session.infos.deferred_infos, prerequisites); }
    catch (const std::exception& error) { return session_failure(error, true); }
}
} // namespace dw4
