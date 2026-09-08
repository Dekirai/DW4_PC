#include "dw4/startup_loading.hpp"

#include <algorithm>
#include <stdexcept>

namespace dw4 {
namespace {
constexpr std::size_t max_files = 128;
constexpr std::size_t max_file_bytes = 64u*1024u*1024u;
constexpr std::size_t max_total_bytes = 256u*1024u*1024u;
constexpr std::size_t max_records = 1024u*1024u;
constexpr std::size_t max_fields = 4u*1024u*1024u;
constexpr std::size_t max_exports = 65536;
constexpr std::size_t max_text_bytes = 8u*1024u*1024u;
constexpr std::size_t max_metadata_bytes = 32u*1024u*1024u;
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Startup loading: "+message); }
void charge(std::size_t amount, std::size_t& used, std::size_t limit, const char* label) {
    if (amount > limit-used) fail(std::string(label)+" budget exceeded.");
    used += amount;
}
void field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot, const char* type) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != 4 || doc.metadata.field_types.at(value->type_index).name != type)
        fail("Unsupported scene-routing field at slot "+std::to_string(slot)+".");
}
void export_preflight(const IgbDocument& doc, std::size_t& text_bytes) {
    if (!doc.info_list_reference) fail("Document has no explicit info list.");
    const auto& list = doc.object(*doc.info_list_reference);
    if (doc.metadata.object_types.at(list.type_index).name != "igInfoList") fail("Expected an exact info list.");
    field(doc,list,2,"igIntMetaField"); field(doc,list,3,"igIntMetaField"); field(doc,list,4,"igMemoryRefMetaField");
    if (doc.field_u32(list,2) > 4096 || doc.field_u32(list,3) > 4096) fail("File info list exceeds 4,096 exports.");
    for (const auto reference : doc.object_list(*doc.info_list_reference)) {
        if (!reference) fail("Info list contains a null export.");
        const auto* name = doc.field(doc.object(*reference),2);
        if (!name || name->size < 4 || name->size > 4104 || doc.metadata.field_types.at(name->type_index).name != "igStringMetaField")
            fail("Export name has an unsupported schema or size.");
        // Charge padded serialized names before PackageExport trims/scans them.
        charge(name->size,text_bytes,max_text_bytes,"Export/routing text");
    }
}
bool late_scene(const IgbDocument& doc, const IgbObject& info, std::size_t& text_bytes, std::size_t& camera_references) {
    field(doc,info,7,"igObjectRefMetaField");
    if (const auto cameras = doc.field_reference(info,7)) {
        const auto& list = doc.object(*cameras);
        if (!doc.is_a(list,"igObjectList")) fail("Scene cameras are not an object list.");
        field(doc,list,2,"igIntMetaField"); field(doc,list,3,"igIntMetaField");
        field(doc,list,4,"igMemoryRefMetaField");
        if (doc.field_u32(list,2) > max_exports || doc.field_u32(list,3) > max_exports)
            fail("Scene camera list exceeds 65,536 entries.");
        charge(doc.field_u32(list,2),camera_references,max_records,"Combined camera-list traversal");
        if (!doc.object_list(*cameras).empty()) return true;
    }
    field(doc,info,5,"igObjectRefMetaField");
    const auto graph = doc.field_reference(info,5);
    if (!graph) fail("Scene without cameras has a null graph; original routing dereferences this graph.");
    const auto& root = doc.object(*graph);
    if (!doc.is_a(root,"igNode")) fail("Scene graph is not a node.");
    const auto* name = doc.field(root,2);
    if (!name || name->size < 4 || name->size > 4104 || doc.metadata.field_types.at(name->type_index).name != "igStringMetaField")
        fail("Scene graph name has an unsupported schema or size.");
    charge(name->size,text_bytes,max_text_bytes,"Export/routing text");
    const auto text = doc.field_string(root,2);
    if (text.size() > 4096 || text.find('\0') != std::string_view::npos) fail("Invalid scene graph name.");
    return text == "ROOTATTR";
}
std::vector<StartupSource> sources(const std::vector<StartupLoadedFile>& files) {
    std::vector<StartupSource> result; result.reserve(files.size());
    for (const auto& file : files) result.push_back({&file.document,file.requested_path});
    return result;
}
std::vector<std::uint8_t> read(const StartupAssetReader& reader, ResourceLocation location, std::size_t limit) {
    if (!limit) fail("No bytes remain in the loading budget.");
    auto bytes = reader(location,limit);
    if (bytes.size() > limit) fail("Asset reader exceeded its supplied byte limit.");
    return bytes;
}
} // namespace

StartupInfoRoutes route_startup_infos(std::span<const StartupSource> input) {
    if (input.size() > max_files) fail("Info routing exceeds 128 source files.");
    StartupInfoRoutes result;
    std::size_t export_count = 0, text_bytes = 0, camera_references = 0;
    for (std::uint32_t f = 0; f < input.size(); ++f) {
        if (!input[f].document) fail("Null source document.");
        if (input[f].label.size() > 4096) fail("Source label exceeds 4,096 bytes.");
        charge(input[f].label.size(),text_bytes,max_text_bytes,"Export/routing text");
        const auto& doc = *input[f].document;
        export_preflight(doc,text_bytes);
        const auto exports = list_package_exports(doc);
        charge(exports.size(),export_count,max_exports,"Info export");
        for (std::uint32_t e = 0; e < exports.size(); ++e) {
            const auto& item = exports[e];
            charge(item.name.size()+item.type.size(),text_bytes,max_text_bytes,"Export/routing text");
            const StartupInfoAddress address{f,e,item.reference_index};
            if (item.type == "beLuaInfo") result.lua_infos.push_back(address);
            else if (item.type == "igSceneInfo") {
                if (late_scene(doc,doc.object(item.reference_index),text_bytes,camera_references)) result.late_scenes.push_back(address);
                else result.held_scenes.push_back(address);
            } else result.deferred_infos.push_back(address);
        }
    }
    result.deferred_infos.insert(result.deferred_infos.end(),result.late_scenes.begin(),result.late_scenes.end());
    return result;
}

StartupSession load_startup_session(const ResourceCatalog& catalog, const StartupAssetReader& reader,
    const std::string& package_path, const BootInputs& inputs) {
    StartupSession session; session.package_path = package_path;
    session.next_barrier = "Original loading-screen state, runtime manager callbacks and layer/text initialization remain pending. start.lua and first.lua have not run.";
    std::string current = package_path;
    try {
        validate_boot_inputs(inputs); validate_resource_catalog(catalog);
        if (!reader) fail("No asset reader was supplied.");
        if (package_path.empty() || package_path.size() > 4096) fail("Invalid package path length.");
        const auto* descriptor = catalog.find(package_path);
        if (!descriptor || !descriptor->location) fail("Package has no exact available catalog entry.");
        auto package = read_package_descriptor(read_igb_document(read(reader,*descriptor->location,4u*1024u*1024u)),catalog);
        if (package.members.empty() || package.members.size() > 32) fail("Initial package must contain 1..32 members.");
        std::size_t references = 0, entries = 0, metadata_bytes = 0, export_count = 0, text_bytes = 0;
        const auto load = [&](StartupFileStage stage, std::uint32_t ordinal, std::string requested,
            std::string catalog_path, ResourceLocation location) {
            current = requested;
            if (session.files.size() >= max_files) fail("Loaded file count exceeds 128.");
            if (requested.size() > 4096 || catalog_path.size() > 4096) fail("Loaded path exceeds 4,096 bytes.");
            charge(requested.size()+catalog_path.size(),text_bytes,max_text_bytes,"Loaded metadata text");
            auto bytes = read(reader,location,std::min(max_file_bytes,max_total_bytes-session.total_bytes));
            // Inspect declared record counts before constructing another graph.
            // Metadata is temporary here; the owned document parses it independently.
            {
                const auto metadata = read_igb_metadata(bytes);
                charge(metadata.metadata_bytes,metadata_bytes,max_metadata_bytes,"Combined IGB metadata");
                charge(metadata.header.object_count,references,max_records,"Combined declared reference");
                charge(metadata.header.memory_reference_count,references,max_records,"Combined declared reference");
                charge(metadata.header.entry_count,entries,max_records,"Combined declared entry");
            }
            auto document = read_igb_document(std::move(bytes));
            std::size_t fields = 0;
            for (const auto& entry : document.entries) fields += entry.fields.size();
            for (const auto& object : document.objects) fields += object.fields.size();
            auto object_count = session.object_count, field_count = session.field_count, total_bytes = session.total_bytes;
            charge(document.objects.size(),object_count,max_records,"Combined object");
            charge(fields,field_count,max_fields,"Combined decoded field");
            export_preflight(document,text_bytes);
            auto exports = list_package_exports(document);
            charge(exports.size(),export_count,max_exports,"Combined info export");
            for (const auto& item : exports) charge(item.type.size()+item.name.size(),text_bytes,max_text_bytes,"Loaded metadata text");
            charge(document.bytes().size(),total_bytes,max_total_bytes,"Combined serialized file");
            session.files.push_back({stage,ordinal,std::move(requested),std::move(catalog_path),location,std::move(document),std::move(exports)});
            session.object_count = object_count; session.field_count = field_count; session.total_bytes = total_bytes;
        };
        for (const bool preliminary : {true,false}) for (const auto& member : package.members) {
            if ((member.suffix == "/s") != preliminary) continue;
            if (!member.supported || !member.location) fail("Unsupported package member: "+member.diagnostic);
            load(preliminary ? StartupFileStage::preliminary : StartupFileStage::package,member.index,
                package_path+" / member "+std::to_string(member.index)+" "+member.specifier,"",*member.location);
        }
        // Establish valid explicit exports before publishing script execution.
        const auto initial = sources(session.files);
        (void)route_startup_infos(initial);
        session.boot.execute(initial,package.name,inputs);
        session.boot.resolve_resources(catalog);
        const auto& boot = session.boot.report();
        if (!boot.boot_scripts_completed) fail(boot.diagnostic);
        if (!boot.resources_resolved) fail("Boot requested unresolved original resources.");
        if (boot.resources.size() > max_files-session.files.size()) fail("Initial and common sources exceed 128 files.");
        for (std::uint32_t i = 0; i < boot.resources.size(); ++i) {
            const auto& request = boot.resources[i];
            load(StartupFileStage::common,i,request.logical_path,request.catalog_path,*request.location);
        }
        session.files_loaded = true;
        session.infos = route_startup_infos(sources(session.files));
        session.info_routing_complete = true;
    } catch (const std::exception& error) { session.diagnostic = current+": "+error.what(); }
    return session;
}

StartupSession load_startup_session(const MountedResources& resources,
    const std::string& package_path, const BootInputs& inputs) {
    return load_startup_session(resources.catalog(),[&](ResourceLocation location, std::size_t limit) {
        return resources.archives().at(location.archive_index).read(location.entry_index,limit);
    },package_path,inputs);
}
} // namespace dw4
