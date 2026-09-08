#pragma once

#include "dw4/resources.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {
struct StartupSource;

// Explicit host inspection state. These are not recovered save-game defaults.
struct BootPlayerInput {
    bool joined = false;
    std::string model_name;
    std::array<std::optional<std::string>,3> weapon_names;
};
struct BootInputs {
    std::array<BootPlayerInput,4> players;
    int difficulty = 0;
    int camera_mode = 0;
};
struct BootScriptResult {
    std::string name, source, diagnostic;
    std::uint32_t info_reference{}, data_index{};
    std::uint64_t instructions{}, host_calls{};
    bool completed = false;
};
struct BootResourceRequest {
    std::string logical_path, source;
    std::string prototype;
    std::size_t pc{};
    std::optional<ResourceLocation> location;
    std::string diagnostic;
    std::string catalog_path; // Original resolver lowercases ASCII A..Z in the request only.
};
struct BootReport {
    std::string package_path, folder;
    BootInputs inputs;
    std::vector<BootScriptResult> scripts;
    std::vector<BootResourceRequest> resources;
    std::uint64_t instructions{}, host_calls{};
    std::size_t registered_sources{}, registered_scripts{};
    bool boot_scripts_completed = false;
    bool resources_resolved = false;
    std::string diagnostic;
    std::string next_barrier;
};

void validate_boot_inputs(const BootInputs& inputs);
// Shared app/CLI parsing for the explicitly named --boot-* state options.
// Returns false for an unknown option; a recognized malformed value throws.
bool set_boot_input_option(BootInputs& inputs, std::string_view option, std::string_view value);
// Executes only func.lua and boot.lua, with strict CFL/SYS inspection bindings.
// Unknown native effects stop execution; no staged loading or start/first calls
// are skipped, mocked or marked complete. Failed partial queues remain diagnostic.
BootReport execute_boot_scripts(std::span<const StartupSource> sources,
    const std::string& folder, const BootInputs& inputs);
void resolve_boot_requests(BootReport& report, const ResourceCatalog& catalog);
// Reads initial package members, registering preliminary /s members before the
// remaining members while preserving order within each stage. Reads at most
// 32 members, 64 MiB per member and 256 MiB of combined member bytes.
BootReport inspect_package_boot(const MountedResources& resources,
    const std::string& package_path, const BootInputs& inputs);
} // namespace dw4
