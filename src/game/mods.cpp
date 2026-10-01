#include "mods.hpp"

#include "dw4/ini.hpp"

#include <algorithm>

namespace dw4::game {

std::vector<ModInfo> scan_mods(const std::filesystem::path& root, const std::set<std::string>& disabled) {
    std::vector<ModInfo> mods;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) return mods;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (!entry.is_directory(error)) continue;
        const auto ini = IniFile::load(entry.path() / "mod.ini");
        if (!ini) continue;
        ModInfo mod;
        mod.id = entry.path().filename().string();
        mod.name = ini->get_or("mod", "name", mod.id);
        mod.version = ini->get_or("mod", "version", "");
        mod.author = ini->get_or("mod", "author", "");
        mod.description = ini->get_or("mod", "description", "");
        mod.priority = ini->get_int("mod", "priority", 0);
        mod.enabled = !disabled.count(mod.id);
        mod.files = entry.path() / "files";
        const auto game = ini->get_or("mod", "game", "GDJEB2");
        if (game != "GDJEB2") mod.problem = "made for " + game;
        if (std::filesystem::is_directory(mod.files, error))
            for (auto it = std::filesystem::recursive_directory_iterator(mod.files, error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                if (it->is_regular_file(error)) ++mod.file_count;
        if (!mod.file_count && mod.problem.empty()) mod.problem = "no files/ folder";
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.id < b.id;
    });
    return mods;
}

std::vector<std::filesystem::path> mod_overrides(const std::vector<ModInfo>& mods) {
    std::vector<std::filesystem::path> roots;
    for (const auto& mod : mods)
        if (mod.enabled && mod.problem.empty()) roots.push_back(mod.files);
    return roots;
}

std::uint64_t mods_hash(const std::vector<ModInfo>& mods) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](const std::string& text) {
        for (const char c : text) {
            h ^= std::uint8_t(c);
            h *= 0x100000001b3ull;
        }
        h ^= 0xFF;
        h *= 0x100000001b3ull;
    };
    for (const auto& mod : mods) {
        if (!mod.enabled || !mod.problem.empty()) continue;
        mix(mod.id);
        mix(mod.version);
        std::vector<std::string> files;
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(mod.files, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            if (it->is_regular_file(error))
                files.push_back(std::filesystem::relative(it->path(), mod.files, error).generic_string() + ":" +
                                std::to_string(it->file_size(error)));
        std::sort(files.begin(), files.end());
        for (const auto& file : files) mix(file);
    }
    return h;
}

} // namespace dw4::game
