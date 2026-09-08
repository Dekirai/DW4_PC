#pragma once

#include "dw4/igb_document.hpp"
#include "dw4/resources.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {
class PackageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PackageMember {
    std::uint32_t index{}; // Original filename-list ordinal, never a scene index.
    std::uint32_t file_reference{}; // igNamedObject in the descriptor document.
    std::string specifier;
    std::optional<ResourceLocation> location;
    std::string suffix; // Exact /s marks the original preliminary loading stage.
    bool supported{}; // Bounded byte inspection, not original staged initialization.
    std::string diagnostic;
    std::vector<std::string> catalog_paths; // Exact aliases; empty is valid for packed entries.
    std::string provenance;
};

struct PackageDescriptor {
    std::uint32_t reference_index{}; // Exported beFileListInfo reference.
    std::string name;
    std::string section;
    bool resolve_state{}; // Stored igInfo field; not an executed loading state.
    std::vector<PackageMember> members;
    std::string logical_path; // Filled by load_package_descriptor.
    std::optional<ResourceLocation> location; // Descriptor source archive entry.
    std::string provenance;
};

struct PackageExport {
    std::uint32_t reference_index{}; // In the member document, never the descriptor.
    std::string type;
    std::string name;
};

// Requires exactly one beFileListInfo on the explicit igInfoList. Member order
// is preserved. Unsupported filename forms remain visible with diagnostics.
PackageDescriptor read_package_descriptor(const IgbDocument& document, const ResourceCatalog& catalog);

// Reads the exact catalog resource. Descriptor input has an independent 4-MiB
// cap. The caller limit can lower it; no directory or extension is guessed.
PackageDescriptor load_package_descriptor(const MountedResources& resources, std::string_view logical_path,
    std::size_t max_bytes = 64u * 1024u * 1024u);

// Revalidates the specifier/location against the mounted catalog before an
// explicit member read. Caller limits are bounded to at most 256 MiB.
IgbDocument read_package_member(const MountedResources& resources, const PackageMember& member,
    std::size_t max_bytes = 64u * 1024u * 1024u);

// Follows only the explicit igInfoList, preserving its order and reference
// identity. This never scans objects for a guessed scene or chooses a root.
std::vector<PackageExport> list_package_exports(const IgbDocument& document);
} // namespace dw4
