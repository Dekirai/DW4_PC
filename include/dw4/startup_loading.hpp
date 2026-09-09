#pragma once

#include "dw4/packages.hpp"
#include "dw4/scripts.hpp"
#include "dw4/startup.hpp"

#include <functional>

namespace dw4 {
enum class StartupFileStage { preliminary, package, common };

struct StartupLoadedFile {
    StartupFileStage stage{};
    std::uint32_t ordinal{}; // Original member or common-queue ordinal.
    std::string requested_path, catalog_path;
    ResourceLocation location;
    IgbDocument document; // Owns bytes and local references for this occurrence.
    std::vector<PackageExport> exports;
};
struct StartupInfoAddress {
    std::uint32_t file_index{}, export_index{}, reference_index{};
    bool operator==(const StartupInfoAddress&) const = default;
};
struct StartupInfoRoutes {
    std::vector<StartupInfoAddress> lua_infos;
    std::vector<StartupInfoAddress> held_scenes;
    std::vector<StartupInfoAddress> deferred_infos;
    std::vector<StartupInfoAddress> late_scenes; // Also appended to deferred_infos, in this order.
};
struct StartupSession {
    std::string package_path;
    BootRuntime boot; // Retains the VM and globals after func/boot, without running start/first.
    std::vector<StartupLoadedFile> files;
    StartupInfoRoutes infos;
    std::size_t total_bytes{}, object_count{}, field_count{};
    bool files_loaded = false;
    bool info_routing_complete = false;
    std::string diagnostic, next_barrier;
};
struct StartupPreliminaryStage {
    std::string package_path;
    std::optional<PackageDescriptor> package; // Retained plan for a later ordinary-member stage.
    std::vector<StartupLoadedFile> files; // Only exact /s members, in descriptor order.
    StartupInfoRoutes infos;
    std::size_t total_bytes{}, object_count{}, field_count{};
    bool files_loaded = false; // Every preliminary member, possibly zero; never all game resources.
    bool info_routing_complete = false;
    std::string diagnostic, next_barrier;
};

// Decodes original loader routing only. Exact beLuaInfo is immediate; exact
// igSceneInfo with cameras or ROOTATTR goes last; other scenes are held apart.
// Every occurrence is preserved, including repeated references and file loads.
// This does not invoke controller factories, manager callbacks or layer setup.
StartupInfoRoutes route_startup_infos(std::span<const StartupSource> sources);

// The caller supplies a bounded byte reader, not decoded or trusted metadata.
// It must respect max_bytes before allocating its result. The loader also checks
// the returned size. The mounted implementation reads only checked AFS entries.
using StartupAssetReader = std::function<std::vector<std::uint8_t>(ResourceLocation, std::size_t max_bytes)>;
// Loads the descriptor and only its exact /s members. No ordinary/common reads,
// Lua execution or controller initialization occurs. Successful routing leaves
// the original preboot controller and SOUND transfer gates for the stage runner.
// Same bounded document loader as the aggregate inspector: descriptor 4 MiB,
// 32 original members, 64 MiB/file, 256 MiB combined and checked metadata/exports.
StartupPreliminaryStage load_startup_preliminary(const ResourceCatalog& catalog, const StartupAssetReader& reader,
    const std::string& package_path);
StartupPreliminaryStage load_startup_preliminary(const MountedResources& resources,
    const std::string& package_path);
// Aggregate script/resource inspection deliberately bypasses original preboot
// initialization and audio gates. Its combined routes are not the state-5 queue;
// use the staged preliminary loader for original-order startup reconstruction.
StartupSession load_startup_session(const ResourceCatalog& catalog, const StartupAssetReader& reader,
    const std::string& package_path, const BootInputs& inputs);
StartupSession load_startup_session(const MountedResources& resources,
    const std::string& package_path, const BootInputs& inputs);
} // namespace dw4
