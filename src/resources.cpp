#include "dw4/resources.hpp"
#include "dw4/disc.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace dw4 {
namespace {
constexpr std::size_t max_logical_path = 16384;
constexpr std::size_t max_catalog_path_bytes = 64u * 1024u * 1024u;
[[noreturn]] void fail(const std::string& message) {
    throw assets::AssetError("Resource catalog: " + message);
}

void component(std::string_view value) {
    if (value.empty() || value == "." || value == ".." || value.size() > 4096)
        fail("invalid path component");
    for (const auto c : value) {
        if (static_cast<unsigned char>(c) < 32 || c == '/' || c == '\\' || c == ':')
            fail("path contains a separator, control character or drive prefix");
    }
}

void logical_path(std::string_view value) {
    if (value.empty() || value.size() > max_logical_path) fail("invalid logical path length");
    while (true) {
        const auto end = value.find('/');
        component(value.substr(0, end));
        if (end == std::string_view::npos) return;
        value.remove_prefix(end + 1);
    }
}

std::uint32_t required_reference(const IgbDocument& document, const IgbObject& object,
    std::uint16_t slot) {
    const auto ref = document.field_reference(object, slot);
    if (!ref) fail("required list reference is null");
    return *ref;
}

const IgbObject& require_type(const IgbDocument& document, std::uint32_t reference,
    std::string_view type) {
    const auto& object = document.object(reference);
    if (!document.is_a(object, type)) fail("expected object type " + std::string(type));
    return object;
}

std::vector<std::uint32_t> list(const IgbDocument& document, std::uint32_t reference) {
    std::vector<std::uint32_t> result;
    for (const auto item : document.object_list(reference)) {
        if (!item) fail("unexpected null element in catalog list");
        result.push_back(*item);
    }
    return result;
}

std::string file_path(const IgbDocument& document, const IgbObject& file) {
    std::string path;
    const auto directories = document.field_reference(file, 3);
    if (directories) {
        require_type(document, *directories, "igNamedObjectList");
        for (const auto reference : list(document, *directories)) {
            const auto& directory = require_type(document, reference, "igNamedObject");
            const auto name = document.field_string(directory, 2);
            component(name);
            if (name.size() + 1 > max_logical_path - path.size())
                fail("logical path exceeds its length limit");
            path.append(name);
            path.push_back('/');
        }
    }
    const auto basename = document.field_string(file, 2);
    component(basename);
    if (basename.size() > max_logical_path - path.size())
        fail("logical path exceeds its length limit");
    path.append(basename);
    return path;
}
} // namespace

const CatalogResource* ResourceCatalog::find(std::string_view path) const noexcept {
    const auto found = std::find_if(resources.begin(), resources.end(),
        [path](const CatalogResource& entry) { return entry.logical_path == path; });
    return found == resources.end() ? nullptr : &*found;
}

void validate_resource_catalog(const ResourceCatalog& catalog) {
    if (catalog.archives.empty() || catalog.archives.size() > 128 || catalog.resources.size() > 1048576)
        fail("invalid archive or resource count");
    std::set<std::string> archive_names;
    for (const auto& archive : catalog.archives) {
        component(archive.filename);
        if (!archive.filename.ends_with(".afs")) fail("archive filename must end in .afs");
        if (!archive_names.insert(archive.filename).second) fail("duplicate archive filename");
        if (archive.declared_entries > 1048576) fail("archive entry count exceeds the limit");
    }
    std::map<std::string, std::optional<ResourceLocation>> seen;
    std::size_t path_bytes = 0;
    for (const auto& resource : catalog.resources) {
        logical_path(resource.logical_path);
        if (resource.logical_path.size() > max_catalog_path_bytes - path_bytes)
            fail("combined logical paths exceed the 64 MiB catalog limit");
        path_bytes += resource.logical_path.size();
        if (resource.location) {
            const auto& location = *resource.location;
            if (location.archive_index >= catalog.archives.size()) fail("archive index is out of range");
            if (location.entry_index >= catalog.archives[location.archive_index].declared_entries)
                fail("entry index is out of range");
        }
        const auto [previous, inserted] = seen.emplace(resource.logical_path, resource.location);
        if (!inserted && previous->second != resource.location)
            fail("logical path has conflicting locations: " + resource.logical_path);
    }
}

ResourceCatalog read_resource_catalog(const IgbDocument& document) {
    if (!document.info_list_reference) fail("IGB has no info list");
    const IgbObject* setup = nullptr;
    for (const auto reference : list(document, *document.info_list_reference)) {
        const auto& candidate = document.object(reference);
        if (document.is_a(candidate, "beNDMWAfsSetupInfo")) {
            if (setup) fail("multiple AFS setup objects");
            setup = &candidate;
        }
    }
    if (!setup) fail("IGB has no beNDMWAfsSetupInfo object");
    ResourceCatalog catalog;
    catalog.root_label = document.field_string(*setup, 2);
    const auto archive_list = required_reference(document, *setup, 6);
    require_type(document, archive_list, "beNDMWAfsSetupDataList");
    for (const auto reference : list(document, archive_list)) {
        if (catalog.archives.size() == 128) fail("archive count exceeds the limit");
        const auto& archive = require_type(document, reference, "beNDMWAfsSetupData");
        const auto count = document.field_i32(archive, 3);
        if (count < 0) fail("negative archive entry count");
        const auto archive_name = document.field_string(archive, 2);
        component(archive_name);
        catalog.archives.push_back({std::string(archive_name),
            static_cast<std::uint32_t>(count)});
    }
    const auto file_list = required_reference(document, *setup, 5);
    require_type(document, file_list, "igAdxAfsFileList");
    std::size_t path_bytes = 0;
    for (const auto reference : list(document, file_list)) {
        const auto& file = require_type(document, reference, "igAdxAfsFile");
        const auto archive_index = document.field_i16(file, 4);
        const auto entry_index = document.field_i16(file, 5);
        CatalogResource resource{file_path(document, file), reference, std::nullopt};
        if (resource.logical_path.size() > max_catalog_path_bytes - path_bytes)
            fail("combined logical paths exceed the 64 MiB catalog limit");
        path_bytes += resource.logical_path.size();
        if (entry_index != -1) {
            if (archive_index < 0 || entry_index < 0) fail("invalid negative archive/entry index");
            resource.location = ResourceLocation{static_cast<std::uint32_t>(archive_index),
                static_cast<std::uint32_t>(entry_index)};
        }
        catalog.resources.push_back(std::move(resource));
    }
    validate_resource_catalog(catalog);
    return catalog;
}

MountedResources MountedResources::open(const std::filesystem::path& root) {
    MountedResources result;
    if (std::filesystem::is_regular_file(root)) {
        // A disc image: the catalog and the AFS archives are files of its FST.
        const auto disc = DiscImage::open(root);
        const auto* catalog = disc.find("ndmw.igb");
        if (!catalog) fail("disc image has no ndmw.igb: " + root.string());
        result.catalog_ = read_resource_catalog(read_igb_document(disc.read(*catalog)));
        for (const auto& archive : result.catalog_.archives) {
            const auto* file = disc.find(archive.filename);
            if (!file) fail("disc image has no archive " + archive.filename);
            auto opened = assets::AfsArchive::open_region(root, file->offset, file->size);
            if (opened.entries().size() != archive.declared_entries)
                fail("archive entry count differs from catalog: " + archive.filename);
            result.archives_.push_back(std::move(opened));
        }
        return result;
    }
    const auto data = std::filesystem::is_directory(root / "files") ? root / "files" : root;
    result.catalog_ = read_resource_catalog(read_igb_document(data / "ndmw.igb"));
    for (const auto& archive : result.catalog_.archives) {
        // Only validated archive basenames become host paths. Logical resource
        // paths are identifiers and are never opened on the host filesystem.
        auto opened = assets::AfsArchive::open(data / std::filesystem::path(archive.filename));
        if (opened.entries().size() != archive.declared_entries)
            fail("archive entry count differs from catalog: " + archive.filename);
        result.archives_.push_back(std::move(opened));
    }
    return result;
}

std::vector<std::uint8_t> MountedResources::read(std::string_view path, std::size_t limit) const {
    const auto* resource = catalog_.find(path);
    if (!resource) fail("logical path is not present: " + std::string(path));
    if (!resource->location) fail("resource has no archive entry: " + std::string(path));
    const auto& location = *resource->location;
    return archives_.at(location.archive_index).read(location.entry_index, limit);
}
} // namespace dw4
