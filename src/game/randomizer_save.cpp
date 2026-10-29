#include "randomizer_save.hpp"

#include "dw4/engine/randomizer.hpp"
#include "dw4/ini.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace dw4::game {

void apply_randomizer(const RandomizerConfig& config, engine::Cheats& cheats) {
    const bool on = config.seed != 0;
    cheats.rand_seed = on ? config.seed : 0;
    cheats.drop_seed = on && config.drops ? config.seed : 0;
    cheats.rand_enemies = std::uint8_t(on ? std::clamp(config.enemies, 0, 2) : 0);
    cheats.rand_zones = std::uint8_t(on ? std::clamp(config.zones, 0, engine::randomizer::kZoneRuleLast) : 0);
}

RandomizerConfig randomizer_from_cheats(const engine::Cheats& cheats) {
    RandomizerConfig config;
    config.seed = cheats.rand_seed ? cheats.rand_seed : cheats.drop_seed;
    config.drops = cheats.drop_seed != 0;
    config.enemies = cheats.rand_seed ? cheats.rand_enemies : 0;
    config.zones = cheats.rand_seed ? cheats.rand_zones : 0;
    return config;
}

std::optional<RandomizerConfig> parse_randomizer(std::string_view text) {
    unsigned seed = 0;
    int enemies = 1, zones = engine::randomizer::kZoneRuleCurrent, drops = 1;
    const std::string copy(text);
    if (std::sscanf(copy.c_str(), "%u,%d,%d,%d", &seed, &enemies, &zones, &drops) < 1) return std::nullopt;
    RandomizerConfig config;
    config.seed = seed;
    config.enemies = std::clamp(enemies, 0, 2);
    config.zones = std::clamp(zones, 0, engine::randomizer::kZoneRuleLast);
    config.drops = drops != 0;
    return config;
}

std::string randomizer_unique_key(std::uint32_t unique) {
    char text[16];
    std::snprintf(text, sizeof text, "%08X", unique);
    return text;
}

std::string describe_randomizer(const RandomizerConfig& config) {
    if (!config.active()) return "the original game";
    std::string text = "seed " + std::to_string(config.seed) + " - enemies: ";
    text += config.enemies == 2 ? "one family per wave" : config.enemies == 1 ? "each on its own" : "original";
    text += ", loading zones: ";
    if (config.zones == 0) text += "original";
    else if (config.zones == engine::randomizer::kZoneRuleCurrent) text += "dungeon entrances";
    else text += "dungeon entrances (" + std::string(engine::randomizer::zone_rule_text(config.zones)) + ")";
    text += ", item drops: ";
    text += config.drops ? "shuffled" : "original";
    return text;
}

BondChoice choose_randomizer_bond(bool has_bond, bool new_game, bool settings_active, bool bind_called) {
    if (has_bond) return BondChoice::use_bond;
    if (settings_active && (new_game || bind_called)) return BondChoice::bind_settings;
    return BondChoice::original;
}

namespace {
const char* enemy_mode_name(int mode) { return mode == 2 ? "wave" : mode == 1 ? "each" : "off"; }
int enemy_mode(std::string_view text) { return text == "wave" ? 2 : text == "each" || text == "1" ? 1 : 0; }
} // namespace

std::optional<RandomizerConfig> load_randomizer_binding(const std::filesystem::path& folder, std::uint32_t unique) {
    const auto ini = IniFile::load(folder / "randomizer.ini");
    if (!ini) return std::nullopt;
    const std::string section = "character_" + randomizer_unique_key(unique);
    if (!ini->get(section, "seed")) return std::nullopt;
    RandomizerConfig config;
    config.seed = std::uint32_t(std::max(0, ini->get_int(section, "seed", 0)));
    config.drops = ini->get_bool(section, "drops", false);
    config.enemies = enemy_mode(ini->get_or(section, "enemies", "off"));
    // A bond from before the rules were numbered (round 48) has no
    // zone_rule: rule 1, the table it was played with.
    config.zones = ini->get_or(section, "zones", "off") == "dungeons"
                       ? std::clamp(ini->get_int(section, "zone_rule", engine::randomizer::kZoneRuleDerangement), 1,
                                    engine::randomizer::kZoneRuleLast)
                       : 0;
    return config;
}

bool store_randomizer_binding(const std::filesystem::path& folder, std::uint32_t unique, std::string_view name,
                              const RandomizerConfig& config) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    auto ini = IniFile::load(folder / "randomizer.ini").value_or(IniFile::parse(
        "; DW4_PC randomizer: the seed and modes each saved character plays with (by its UNIQUE).\n"
        "; Delete a section to let that character follow the settings again.\n"));
    const std::string section = "character_" + randomizer_unique_key(unique);
    if (!name.empty() || !ini.get(section, "name")) ini.set(section, "name", name); // (an empty name keeps the stored one)
    ini.set_int(section, "seed", int(config.seed & 0x7FFFFFFF));
    ini.set_bool(section, "drops", config.drops);
    ini.set(section, "enemies", enemy_mode_name(config.enemies));
    ini.set(section, "zones", config.zones ? "dungeons" : "off");
    if (config.zones) ini.set_int(section, "zone_rule", config.zones);
    else ini.remove(section, "zone_rule");
    return ini.save(folder / "randomizer.ini");
}

bool write_randomizer_spoiler(const std::filesystem::path& folder, std::uint32_t unique, const RandomizerConfig& config) {
    engine::Cheats cheats;
    apply_randomizer(config, cheats);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::ofstream out(folder / ("randomizer_" + randomizer_unique_key(unique) + "_spoiler.txt"), std::ios::binary);
    if (!out) return false;
    out << "character " << randomizer_unique_key(unique) << "\n" << engine::randomizer::spoiler(cheats);
    return bool(out);
}

} // namespace dw4::game
