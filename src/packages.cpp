#include "dw4/packages.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <utility>

namespace dw4 {
namespace {
constexpr std::size_t max_descriptor_bytes = 4u * 1024u * 1024u;
constexpr std::size_t max_read_bytes = 256u * 1024u * 1024u;
constexpr std::size_t max_list_count = 4096;
constexpr std::size_t max_string_bytes = 4096;
constexpr std::size_t max_alias_bytes = 8u * 1024u * 1024u;
constexpr std::size_t max_alias_count = 65536;

[[noreturn]] void fail(const std::string& reason) { throw PackageError("Package: " + reason); }

void read_limit(std::size_t limit) {
    if (limit == 0 || limit > max_read_bytes) fail("read limit must be between 1 byte and 256 MiB");
}

const IgbFieldValue& field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot, std::string_view type) {
    const auto* value = doc.field(object, slot);
    if (!value || doc.metadata.field_types.at(value->type_index).name != type)
        fail("required field has an unsupported type at slot " + std::to_string(slot));
    return *value;
}

std::string text(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    field(doc, object, slot, "igStringMetaField");
    const auto value = doc.field_string(object, slot);
    if (value.size() > max_string_bytes) fail("metadata string exceeds 4096 bytes");
    if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        fail("metadata string contains a control character");
    return std::string(value);
}

std::vector<std::uint32_t> object_list(const IgbDocument& doc, std::uint32_t reference,
    std::string_view type) {
    const auto& list = doc.object(reference);
    if (doc.metadata.object_types.at(list.type_index).name != type)
        fail("expected exact list type " + std::string(type));
    field(doc, list, 2, "igIntMetaField");
    field(doc, list, 3, "igIntMetaField");
    field(doc, list, 4, "igMemoryRefMetaField");
    if (doc.field_u32(list, 2) > max_list_count || doc.field_u32(list, 3) > max_list_count)
        fail("list count or capacity exceeds 4096 entries");
    std::vector<std::uint32_t> result;
    for (const auto member : doc.object_list(reference)) {
        if (!member) fail("list contains a null object reference");
        result.push_back(*member);
    }
    return result;
}

std::vector<std::uint32_t> exports(const IgbDocument& doc) {
    if (!doc.info_list_reference) fail("document has no explicit info list");
    auto result = object_list(doc, *doc.info_list_reference, "igInfoList");
    for (const auto reference : result)
        if (!doc.is_a(doc.object(reference), "igInfo")) fail("info list contains a non-info object");
    return result;
}

struct Address {
    std::optional<ResourceLocation> location;
    std::string suffix;
    bool supported{};
    std::string diagnostic;
};

Address address(std::string_view specifier, const ResourceCatalog& catalog) {
    Address result;
    if (specifier.empty() || specifier.size() > max_string_bytes) {
        result.diagnostic = "Member specifier must contain 1..4096 bytes";
        return result;
    }
    const auto closing = specifier.find(']');
    const auto dot = specifier.find('.');
    if (specifier.front() != '[' || dot != 2 ||
        closing == std::string_view::npos || closing <= dot + 1) {
        result.diagnostic = "Only explicit [archive.entry] addresses with a single archive digit are verified";
        return result;
    }
    auto decimal = [](std::string_view value, std::uint32_t& number) {
        if (value.empty() || std::any_of(value.begin(), value.end(), [](char c) { return c < '0' || c > '9'; }))
            return false;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    };
    ResourceLocation location;
    if (!decimal(specifier.substr(1, dot - 1), location.archive_index) ||
        !decimal(specifier.substr(dot + 1, closing - dot - 1), location.entry_index)) {
        result.diagnostic = "Archive and entry indices must be unsigned decimal 32-bit integers";
        return result;
    }
    // The original AFS opening path sign-extends both serialized 16-bit indices.
    // Reject truncation instead of reproducing the guest parser's unsafe copy.
    if (location.entry_index > 32767 || closing - dot - 1 > 15) {
        result.diagnostic = "Entry index exceeds the verified signed-16-bit address profile";
        return result;
    }
    result.suffix = std::string(specifier.substr(closing + 1));
    if (location.archive_index >= catalog.archives.size() ||
        location.entry_index >= catalog.archives[location.archive_index].declared_entries) {
        result.diagnostic = "Member address is outside the validated catalog archive bounds";
        return result;
    }
    result.location = location;
    if (!result.suffix.empty() && result.suffix != "/s") {
        result.diagnostic = "Member suffix has unverified loading semantics: " + result.suffix;
        return result;
    }
    if (result.suffix == "/s")
        result.diagnostic = "Original preliminary loading stage /s retained; inspection reads bytes without executing staged initialization";
    result.supported = true;
    return result;
}

std::string location_text(const ResourceLocation& location, const ResourceCatalog& catalog) {
    return "catalog archive " + std::to_string(location.archive_index) + " (" +
        catalog.archives.at(location.archive_index).filename + "), entry " + std::to_string(location.entry_index);
}

PackageDescriptor descriptor(const IgbDocument& doc, const ResourceCatalog& catalog) {
    if (doc.bytes().size() > max_descriptor_bytes) fail("descriptor exceeds the 4 MiB limit");
    validate_resource_catalog(catalog);
    const IgbObject* selected = nullptr;
    for (const auto reference : exports(doc)) {
        const auto& object = doc.object(reference);
        if (!doc.is_a(object, "beFileListInfo")) continue;
        if (selected) fail("multiple exported beFileListInfo objects are ambiguous");
        if (doc.metadata.object_types.at(object.type_index).name != "beFileListInfo")
            fail("derived file-list descriptor types are not verified");
        selected = &object;
    }
    if (!selected) fail("explicit info list has no beFileListInfo descriptor");
    if (!doc.is_a(*selected, "igNamedObject")) fail("descriptor has no igNamedObject ancestry");
    PackageDescriptor output;
    output.reference_index = selected->reference_index;
    output.name = text(doc, *selected, 2);
    output.section = text(doc, *selected, 5);
    const auto state = doc.field_bytes(field(doc, *selected, 4, "igBoolMetaField"));
    if (state.size() != 1 || state[0] > 1) fail("descriptor resolve state is not a serialized boolean");
    output.resolve_state = state[0] != 0;
    field(doc, *selected, 6, "igObjectRefMetaField");
    const auto filenames = doc.field_reference(*selected, 6);
    if (!filenames) fail("descriptor filename list is null");
    for (const auto reference : object_list(doc, *filenames, "igNamedObjectList")) {
        const auto& object = doc.object(reference);
        if (doc.metadata.object_types.at(object.type_index).name != "igNamedObject")
            fail("filename list contains an unsupported object type");
        PackageMember member;
        member.index = static_cast<std::uint32_t>(output.members.size());
        member.file_reference = reference;
        member.specifier = text(doc, object, 2);
        const auto parsed = address(member.specifier, catalog);
        member.location = parsed.location;
        member.suffix = parsed.suffix;
        member.supported = parsed.supported;
        member.diagnostic = parsed.diagnostic;
        member.provenance = "beFileListInfo ref " + std::to_string(output.reference_index) +
            " slot 6 -> filename-list index " + std::to_string(member.index) + " -> igNamedObject ref " +
            std::to_string(reference) + " slot 2";
        if (member.location) member.provenance += " -> " + location_text(*member.location, catalog);
        if (member.suffix == "/s") member.provenance += "; original preliminary queue marker /s";
        output.members.push_back(std::move(member));
    }
    // Build only aliases needed by this descriptor. Preserve catalog order and
    // repeated filename-list entries without a members-times-catalog scan.
    using Key = std::pair<std::uint32_t, std::uint32_t>;
    std::map<Key, std::vector<std::size_t>> locations;
    for (std::size_t i = 0; i < output.members.size(); ++i)
        if (const auto& location = output.members[i].location)
            locations[{location->archive_index, location->entry_index}].push_back(i);
    std::size_t alias_bytes{}, alias_count{};
    for (const auto& resource : catalog.resources) {
        if (!resource.location) continue;
        const auto found = locations.find({resource.location->archive_index, resource.location->entry_index});
        if (found == locations.end()) continue;
        for (const auto i : found->second) {
            if (alias_count == max_alias_count || resource.logical_path.size() > max_alias_bytes - alias_bytes)
                fail("descriptor catalog aliases exceed 65536 entries or 8 MiB");
            ++alias_count;
            alias_bytes += resource.logical_path.size();
            output.members[i].catalog_paths.push_back(resource.logical_path);
        }
    }
    output.provenance = "Explicit igInfoList ref " + std::to_string(*doc.info_list_reference) +
        " -> beFileListInfo ref " + std::to_string(output.reference_index) + "; slot 5 _section, slot 6 _fnames";
    return output;
}
} // namespace

PackageDescriptor read_package_descriptor(const IgbDocument& document, const ResourceCatalog& catalog) {
    try { return descriptor(document, catalog); }
    catch (const PackageError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}

PackageDescriptor load_package_descriptor(const MountedResources& resources, std::string_view logical_path,
    std::size_t max_bytes) {
    try {
        read_limit(max_bytes);
        if (logical_path.empty() || logical_path.size() > 16384) fail("invalid descriptor logical-path length");
        const auto* resource = resources.catalog().find(logical_path);
        if (!resource || !resource->location) fail("descriptor has no exact available catalog entry");
        auto output = descriptor(read_igb_document(resources.read(logical_path,
            std::min(max_bytes, max_descriptor_bytes))), resources.catalog());
        output.logical_path = std::string(logical_path);
        output.location = resource->location;
        output.provenance = output.logical_path + " -> " + location_text(*output.location, resources.catalog()) +
            " -> " + output.provenance;
        return output;
    } catch (const PackageError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}

IgbDocument read_package_member(const MountedResources& resources, const PackageMember& member, std::size_t max_bytes) {
    try {
        read_limit(max_bytes);
        const auto parsed = address(member.specifier, resources.catalog());
        if (!parsed.supported) fail(parsed.diagnostic);
        if (!member.supported || member.location != parsed.location || member.suffix != parsed.suffix)
            fail("member address metadata differs from its validated specifier");
        const auto& location = *parsed.location;
        return read_igb_document(resources.archives().at(location.archive_index).read(location.entry_index, max_bytes));
    } catch (const PackageError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}

std::vector<PackageExport> list_package_exports(const IgbDocument& document) {
    try {
        std::vector<PackageExport> result;
        std::size_t string_bytes{};
        for (const auto reference : exports(document)) {
            const auto& object = document.object(reference);
            auto type = document.metadata.object_types.at(object.type_index).name;
            auto name = text(document, object, 2);
            if (type.size() > max_string_bytes || type.size() + name.size() > max_descriptor_bytes - string_bytes)
                fail("combined export names exceed the 4 MiB limit");
            string_bytes += type.size() + name.size();
            result.push_back({reference, std::move(type), std::move(name)});
        }
        return result;
    } catch (const PackageError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}
} // namespace dw4
