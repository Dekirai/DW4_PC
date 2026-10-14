#pragma once

#include "dw4/assets.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {

class IgbDocument;

struct CatalogArchive {
    std::string filename;
    std::uint32_t declared_entries{};
};

struct ResourceLocation {
    std::uint32_t archive_index{};
    std::uint32_t entry_index{};
    bool operator==(const ResourceLocation&) const = default;
};

struct CatalogResource {
    std::string logical_path;
    std::uint32_t object_reference{};
    // A signed -1 archive entry in the source catalog has no readable payload.
    std::optional<ResourceLocation> location;
};

struct ResourceCatalog {
    std::string root_label; // Original engine label, never used as a host path.
    std::vector<CatalogArchive> archives;
    std::vector<CatalogResource> resources;

    // Exact, case-sensitive logical lookup. Null means no catalog record.
    const CatalogResource* find(std::string_view logical_path) const noexcept;
};

// Reconstructs beNDMWAfsSetupInfo and its referenced catalog lists.
// Validates schema, paths and archive indices; does not open external resources.
ResourceCatalog read_resource_catalog(const IgbDocument& document);
// Also usable for checking a catalog assembled by future import/mod tools.
void validate_resource_catalog(const ResourceCatalog& catalog);

class MountedResources {
public:
    // Accepts Dolphin's extraction root or its files/ directory, or an
    // uncompressed disc image (.iso / .gcm). Reads only.
    static MountedResources open(const std::filesystem::path& root);
    const ResourceCatalog& catalog() const noexcept { return catalog_; }
    const std::vector<assets::AfsArchive>& archives() const noexcept { return archives_; }
    std::vector<std::uint8_t> read(std::string_view logical_path,
        std::size_t max_bytes = 64u * 1024u * 1024u) const;
    // Mods: folders whose files replace (or add) logical paths, the first
    // folder winning. Only validated logical paths are looked up there.
    void set_overrides(std::vector<std::filesystem::path> roots) { overrides_ = std::move(roots); }
    const std::vector<std::filesystem::path>& overrides() const noexcept { return overrides_; }
    // The override file for a logical path, if a mod has one.
    std::optional<std::filesystem::path> override_for(std::string_view logical_path) const;
    // Where an archive lies on the game disc (its FST offset): from the disc
    // image, or from the extraction's sys/boot.bin + sys/fst.bin. The stream
    // reads' simulated disc timing needs it.
    std::optional<std::uint64_t> archive_disc_offset(std::size_t archive) const {
        return archive < disc_offsets_.size() ? disc_offsets_[archive] : std::nullopt;
    }

private:
    std::vector<std::filesystem::path> overrides_;
    ResourceCatalog catalog_;
    std::vector<assets::AfsArchive> archives_;
    std::vector<std::optional<std::uint64_t>> disc_offsets_;
};

} // namespace dw4
