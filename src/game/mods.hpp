#pragma once

// Content mods: mods/<id>/mod.ini with a versioned manifest and
// mods/<id>/files/<logical path> replacing (or adding) game files - stage
// packages, models, weapons, textures inside packages, scripts in them.
//
//   [mod]
//   name=Bigger Death Valley
//   version=1.2
//   author=someone
//   description=...
//   priority=10        ; higher wins when two mods replace the same file
//   game=GDJEB2        ; the disc the mod is made for
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace dw4::game {

struct ModInfo {
    std::string id; // the folder name
    std::string name, version, author, description;
    int priority{};
    bool enabled = true;
    std::filesystem::path files; // <folder>/files
    std::size_t file_count{};
    std::string problem; // why the mod cannot be used (empty when fine)
};

// Every folder below root with a mod.ini, highest priority first (then id).
std::vector<ModInfo> scan_mods(const std::filesystem::path& root, const std::set<std::string>& disabled);
// The enabled, usable mods' file folders in load order (first wins).
std::vector<std::filesystem::path> mod_overrides(const std::vector<ModInfo>& mods);
// Identity of the enabled mods (ids, versions, file names and sizes) for
// online sessions.
std::uint64_t mods_hash(const std::vector<ModInfo>& mods);

} // namespace dw4::game
