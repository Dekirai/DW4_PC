#include "dw4/engine/randomizer.hpp"

#include "dw4/engine/engine.hpp"

#include <algorithm>
#include <map>
#include <cstdio>
#include <sstream>

namespace dw4::engine::randomizer {

std::uint64_t splitmix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::uint64_t sub_seed(std::uint32_t master, std::string_view salt) {
    std::uint64_t state = master;
    for (const char c : salt) state = state * 131 + std::uint8_t(c);
    splitmix64(state);
    return splitmix64(state);
}

std::uint64_t draw(std::uint64_t seed, std::string_view name, std::uint64_t counter) {
    // FNV-1a over the name, folded into the seed with the counter.
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const char c : name) h = (h ^ std::uint8_t(c)) * 0x100000001B3ull;
    std::uint64_t state = seed ^ h ^ (counter * 0xD6E8FEB86659FD93ull);
    return splitmix64(state);
}

namespace {
// The enemy families (model names of the 'enemy' table), their area tier and
// the smallest stage member holding their six infos (AFS sizes of the stage
// main members, round 48 survey).
constexpr Family kFamilies[] = {
    {"e_nume", 1, false, "dng101b1"},    {"e_goburi", 1, false, "stag0001"},  {"e_ogre", 1, false, "stag0001"},
    {"e_scum", 1, false, "dngq1b1e"},    {"e_cockatri", 1, false, "stag0001"}, {"e_mecha1", 1, false, "dngq4b1e"},
    {"e_mummy", 2, false, "dng204b1"},   {"e_minotaru", 2, false, "dng205b1"}, {"e_pharaoh", 2, false, "dngq4b1e"},
    {"e_mecha2", 2, false, "dngq2b2e"},  {"e_tyrano", 2, false, "stag0202"},  {"e_otama", 3, false, "dng307b1"},
    {"e_geko", 3, false, "dng308b1"},    {"e_mecha3", 3, false, "dng309b1"},  {"e_maridevi", 3, false, "dngq3b1e"},
    {"e_torta", 3, false, "stag0301"},   {"e_mecha4", 3, false, "dngq8b1e"},  {"e_infermo", 4, false, "dng411b2"},
    {"e_megadra", 4, false, "dng411b1"}, {"e_rare", 4, false, "dngq8b1e"},    {"e_mecha5", 4, false, "dng411b3"},
    {"e_impdra", 4, false, "stag0404"},
    // Bosses and the 1-RAM mid-bosses stay where they are.
    {"e_blosso", 0, true, nullptr},      {"e_mammos", 0, true, nullptr},      {"e_skulgrey", 0, true, nullptr},
    {"e_anomalo", 0, true, nullptr},     {"e_tonogeko", 0, true, nullptr},    {"e_diabolo", 0, true, nullptr},
    {"e_ldknight", 0, true, nullptr},    {"e_mecha4bs", 0, true, nullptr},    {"e_arachne", 0, true, nullptr},
};

// The shuffleable families of each enemy stage (its generators' and model
// infos' e_ families without bosses and 1-RAM mid-bosses, and without the
// dng308b3 / dng411b3 boss families), for the pools and the spoiler log.
struct StageFamilies {
    const char* stage;
    const char* families; // space-separated names without the e_ prefix
};
constexpr StageFamilies kStageFamilies[] = {
    {"dng101b1", "nume"}, {"dng101b2", "nume scum"}, {"dng102b1", "nume scum"}, {"dng102b2", "ogre scum"},
    {"dng102b3", "mecha1 nume ogre scum"}, {"dng103f1", "mecha1 ogre scum"}, {"dng103f2", "mecha1 ogre scum"},
    {"dng204b1", "mummy"}, {"dng204b2", "minotaru mummy"}, {"dng205b1", "minotaru mummy"},
    {"dng205b2", "mecha1 minotaru mummy pharaoh"}, {"dng205b3", "mecha1 mecha2 mummy pharaoh"},
    {"dng206f1", "mecha1 mecha2 minotaru pharaoh"}, {"dng206f2", "mecha1 mecha2 minotaru pharaoh"}, {"dng307b1", "otama"},
    {"dng307b2", "geko otama"}, {"dng308b1", "geko otama"}, {"dng308b2", "geko mecha2 mecha3 otama"},
    {"dng308b3", "geko mecha2 mecha3 otama"}, {"dng309b1", "geko mecha3 otama"},
    {"dng309b2", "geko maridevi mecha3 otama"}, {"dng309b3", "geko maridevi mecha3 otama"},
    {"dng309b4", "geko maridevi mecha3 otama"}, {"dng310f1", "geko maridevi mecha3 otama"},
    {"dng310f2", "geko maridevi mecha3 otama"}, {"dng310f3", "geko mecha3 otama"}, {"dng411b1", "megadra"},
    {"dng411b2", "infermo megadra"}, {"dng411b3", "infermo megadra rare"}, {"dng412b1", "infermo megadra rare"},
    {"dng412b2", "infermo mecha4 megadra rare"}, {"dng412b3", "infermo mecha4 megadra rare"},
    {"dng412b4", "infermo mecha5 megadra rare"}, {"dngq1b1e", "nume scum"}, {"dngq1b2e", "nume ogre scum"},
    {"dngq2b1e", "mecha1 minotaru mummy pharaoh"}, {"dngq2b2e", "mecha2 mummy pharaoh"}, {"dngq2b2h", "mecha2 mummy pharaoh"},
    {"dngq3b1e", "geko maridevi otama"}, {"dngq3b2e", "geko maridevi mecha3 otama"}, {"dngq4b1e", "mecha1 mummy pharaoh"},
    {"dngq4b1h", "mecha1 mummy pharaoh"}, {"dngq4b1v", "mecha1 mummy pharaoh"}, {"dngq4b2e", "mecha2 minotaru mummy pharaoh"},
    {"dngq4b2h", "mecha2 minotaru mummy pharaoh"}, {"dngq4b2v", "mecha2 minotaru mummy pharaoh"},
    {"dngq6b01", "infermo mecha4 megadra rare"}, {"dngq7b1e", "geko maridevi otama"}, {"dngq7b2e", "geko maridevi mecha3 otama"},
    {"dngq8b1e", "infermo mecha4 megadra rare"}, {"dngq8b2e", "infermo mecha4 megadra rare"},
    {"dngq8b3e", "infermo mecha4 mecha5 megadra rare"}, {"dngq8b3h", "infermo mecha4 mecha5 megadra rare"},
    {"dngq8b3v", "infermo mecha4 mecha5 megadra rare"}, {"stag0001", "cockatri goburi nume ogre"},
    {"stag0101", "goburi nume ogre"}, {"stag0102", "goburi ogre"}, {"stag0103", "nume"}, {"stag0104", "cockatri goburi ogre"},
    {"stag0201", "mecha2 mummy tyrano"}, {"stag0202", "mecha2 mummy tyrano"}, {"stag0203", "mecha1 mecha2"},
    {"stag0204", "mecha2"}, {"stag0301", "geko otama torta"}, {"stag0302", "geko otama"}, {"stag0303", "maridevi"},
    {"stag0304", "mecha1 mecha2"}, {"stag0401", "infermo mecha3 megadra"}, {"stag0403", "infermo"},
    {"stag0404", "impdra infermo mecha3"},    {"staggf01", "goburi"}, {"staggf02", "goburi"}, {"staggf04", "goburi"},
};

std::vector<std::string> split_families(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const auto space = text.find(' ', at);
        const auto word = text.substr(at, space == std::string_view::npos ? std::string_view::npos : space - at);
        if (!word.empty()) out.push_back("e_" + std::string(word));
        if (space == std::string_view::npos) break;
        at = space + 1;
    }
    return out;
}

const StageFamilies* stage_families(std::string_view stage) {
    for (const auto& s : kStageFamilies)
        if (stage == s.stage) return &s;
    return nullptr;
}

// Coupled dungeon entrances per area (the mat scripts' funcStageChange
// calls: target, point 0, the STCHGFONT00 line; the dungeon b1 matz01 way
// back: the field point).
constexpr ZoneEntrance kZoneEntrances[] = {
    {"stag0101", "matz00", 32, "dng101b1", 2, 1}, {"stag0102", "matz00", 16, "dng102b1", 1, 1},
    {"stag0201", "matz01", 18, "dng204b1", 1, 2}, {"stag0202", "matz01", 14, "dng205b1", 1, 2},
    {"stag0301", "matz00", 17, "dng307b1", 5, 3}, {"stag0302", "matz00", 20, "dng308b1", 3, 3},
    {"stag0301", "matz01", 18, "dng309b1", 3, 3}, {"stag0401", "matz00", 21, "dng411b1", 4, 4},
    {"stag0401", "matz01", 22, "dng412b1", 5, 4},
};
// The dungeons' way back to their field: matz01 of b1, STCHGFONT00 line 10
// (the field's name).
constexpr int kWayBackText = 10;
} // namespace

std::span<const Family> families() { return kFamilies; }

const Family* family(std::string_view name) {
    for (const auto& f : kFamilies)
        if (name == f.name) return &f;
    return nullptr;
}

bool boss_family(std::string_view name) {
    if (name.starts_with("f_")) return true;
    const auto* f = family(name);
    return f && f->boss;
}

bool fixed_stage(std::string_view stage) {
    return stage.starts_with("stagbs") || stage == "staggf03" || stage.starts_with("stagis") || stage.starts_with("stagml");
}

int stage_tier(std::string_view stage) {
    if (stage.size() >= 6 && stage.starts_with("dngq")) {
        // Quests by the area they belong to (dngQ1 area 1 ... dngQ8 area 4).
        static constexpr int kQuest[] = {0, 1, 2, 3, 2, 3, 4, 3, 4};
        const int n = stage[4] - '0';
        return n >= 1 && n <= 8 ? kQuest[n] : 1;
    }
    if (stage.starts_with("dng") && stage.size() > 3) return std::clamp(stage[3] - '0', 1, 4);
    if (stage.starts_with("stag0") && stage.size() > 5) return std::clamp(stage[5] - '0', 1, 4);
    return 1;
}

int tier_weight(int stage_tier, int family_tier) {
    const int distance = std::abs(stage_tier - family_tier);
    return distance == 0 ? 8 : distance == 1 ? 3 : 1;
}

std::vector<std::string> stage_pool(std::uint64_t enemy_seed, std::string_view stage, const std::vector<std::string>& present, int extra) {
    constexpr std::size_t kMaxPool = 6;
    std::vector<std::string> pool;
    // The authored list where known (the same pool in every run and in the
    // spoiler log), else what the stage loaded.
    std::vector<std::string> base = present;
    if (const auto* known = stage_families(stage)) base = split_families(known->families);
    for (const auto& name : base)
        if (!boss_family(name) && std::find(pool.begin(), pool.end(), name) == pool.end() && pool.size() < kMaxPool) pool.push_back(name);
    const int tier = stage_tier(stage);
    for (int n = 0; n < extra && pool.size() < kMaxPool; ++n) {
        int total = 0;
        for (const auto& f : kFamilies)
            if (!f.boss && f.donor && std::find(pool.begin(), pool.end(), f.name) == pool.end()) total += tier_weight(tier, f.tier);
        if (total == 0) break;
        int roll = int(draw(enemy_seed, std::string(stage) + "/pool", std::uint64_t(n)) % std::uint64_t(total));
        for (const auto& f : kFamilies) {
            if (f.boss || !f.donor || std::find(pool.begin(), pool.end(), f.name) != pool.end()) continue;
            roll -= tier_weight(tier, f.tier);
            if (roll < 0) {
                pool.emplace_back(f.name);
                break;
            }
        }
    }
    return pool;
}

std::span<const ZoneEntrance> zone_entrances() { return kZoneEntrances; }

std::vector<int> zone_permutation(std::uint64_t zone_seed) {
    const int count = int(std::size(kZoneEntrances));
    std::vector<int> perm(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) perm[std::size_t(i)] = i;
    for (int area = 1; area <= 4; ++area) {
        std::vector<int> members;
        for (int i = 0; i < count; ++i)
            if (kZoneEntrances[i].area == area) members.push_back(i);
        if (members.size() < 2) continue;
        // A seeded shuffle without fixed points (every entrance leads
        // somewhere new): the first derangement of the seed's shuffles.
        std::uint64_t state = zone_seed ^ (std::uint64_t(area) * 0x9E3779B97F4A7C15ull);
        std::vector<int> order = members;
        for (int attempt = 0; attempt < 64; ++attempt) {
            order = members;
            for (std::size_t i = order.size() - 1; i > 0; --i) std::swap(order[i], order[std::size_t(splitmix64(state) % (i + 1))]);
            bool deranged = true;
            for (std::size_t i = 0; i < order.size(); ++i) deranged &= order[i] != members[i];
            if (deranged) break;
        }
        for (std::size_t i = 0; i < members.size(); ++i) perm[std::size_t(members[i])] = order[i];
    }
    return perm;
}

std::optional<StageTarget> zone_remap(std::uint64_t zone_seed, std::string_view from, std::string_view target, int point) {
    const auto perm = zone_permutation(zone_seed);
    const int count = int(std::size(kZoneEntrances));
    for (int i = 0; i < count; ++i) {
        const auto& e = kZoneEntrances[i];
        // An entrance: the field's mat leads to the dungeon of entrance perm[i].
        if (from == e.field && target == e.dungeon && point == 0) return StageTarget{kZoneEntrances[perm[std::size_t(i)]].dungeon, 0};
        // A way back: the dungeon now behind entrance j leads back to j.
        if (from == e.dungeon && target == e.field && point == e.back_point)
            for (int j = 0; j < count; ++j)
                if (perm[std::size_t(j)] == i) return StageTarget{kZoneEntrances[j].field, kZoneEntrances[j].back_point};
    }
    return std::nullopt;
}

std::optional<StageTarget> zone_dialog_source(std::uint64_t zone_seed, std::string_view stage, int text) {
    const auto perm = zone_permutation(zone_seed);
    const int count = int(std::size(kZoneEntrances));
    for (int i = 0; i < count; ++i) {
        const auto& e = kZoneEntrances[i];
        // The entrance names its new dungeon: that dungeon's own field line.
        if (stage == e.field && text == e.text) {
            const auto& to = kZoneEntrances[perm[std::size_t(i)]];
            return StageTarget{to.field, to.text};
        }
        // The way back names the field it now leads to: the line of the
        // dungeon that field's entrance had.
        if (stage == e.dungeon && text == kWayBackText)
            for (int j = 0; j < count; ++j)
                if (perm[std::size_t(j)] == i) return StageTarget{kZoneEntrances[j].dungeon, kWayBackText};
    }
    return std::nullopt;
}

std::optional<std::u16string> zone_dialog_text(Engine& engine, int text) {
    const auto source = zone_dialog_source(sub_seed(engine.cheats().rand_seed, "zones"), engine.stage_name(), text);
    if (!source) return std::nullopt;
    static std::map<std::pair<std::string, int>, std::u16string> cache;
    const auto key = std::make_pair(source->stage, source->point);
    if (const auto found = cache.find(key); found != cache.end()) return found->second;
    std::u16string name;
    try {
        const auto document = read_igb_document(engine.resources().read(source->stage + "/separate/usa_pack.igb"));
        for (const auto& object : document.objects) {
            if (document.metadata.object_types[object.type_index].name != "beFontInfo") continue;
            const auto strings = ObjectView(&document, object.reference_index).list(7);
            if (source->point >= 0 && std::size_t(source->point) < strings.size() && strings[std::size_t(source->point)]) {
                const auto bytes = strings[std::size_t(source->point)].memory(3);
                for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
                    const char16_t c = char16_t(bytes[i] | (bytes[i + 1] << 8));
                    if (!c) break;
                    name.push_back(c);
                }
            }
            break;
        }
    } catch (const std::exception& error) {
        engine.log(std::string("randomizer: exit name not read: ") + error.what());
        return std::nullopt;
    }
    if (name.empty()) return std::nullopt;
    cache[key] = name;
    return name;
}

std::string spoiler(const Cheats& cheats) {
    std::ostringstream out;
    out << "DW4_PC randomizer spoiler log\n";
    out << "seed " << cheats.rand_seed << "\n";
    out << "item drops: " << (cheats.drop_seed ? "shuffled within their kind (key items stay)" : "original") << "\n";
    out << "enemies: "
        << (!cheats.enemy_shuffle() ? "original" : cheats.rand_enemies == 2 ? "one family per wave" : "every enemy drawn on its own")
        << " (stats, EXP and drops of the original enemy; bosses, mid-bosses and story fights stay)\n";
    out << "loading zones: " << (cheats.zone_shuffle() ? "dungeon entrances within each area (coupled)" : "original") << "\n";
    if (cheats.zone_shuffle()) {
        out << "\n[loading zones]\n";
        const auto perm = zone_permutation(sub_seed(cheats.rand_seed, "zones"));
        for (std::size_t i = 0; i < perm.size(); ++i) {
            const auto& e = kZoneEntrances[i];
            const auto& to = kZoneEntrances[std::size_t(perm[i])];
            out << "area " << e.area << ": " << e.field << " " << e.mat << " (to " << e.dungeon << ") -> " << to.dungeon
                << ", its way back -> " << e.field << " point " << e.back_point << "\n";
        }
    }
    if (cheats.enemy_shuffle()) {
        out << "\n[enemy pools] (the families each stage's shuffled waves draw from)\n";
        const auto seed = sub_seed(cheats.rand_seed, "enemies");
        for (const auto& s : kStageFamilies) {
            out << s.stage << ":";
            for (const auto& name : stage_pool(seed, s.stage, {}, kPoolExtra)) out << " " << name;
            out << "\n";
        }
    }
    return out.str();
}

} // namespace dw4::engine::randomizer
