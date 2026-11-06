// Data Overflow's meta file (overflow_profile.hpp; docs/SURVIVAL.md 8).
#include "overflow_profile.hpp"

#include "overflow_run.hpp"

#include "dw4/card_store.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace dw4::game::overflow {

namespace {

constexpr std::string_view kHeader = "DW4OVERFLOW ";
// Guards against a damaged file that still has a header: at most this many
// unknown lines are kept, none longer than this.
constexpr std::size_t kMaxUnknownLines = 512;
constexpr std::size_t kMaxLine = 512;
constexpr std::int64_t kMaxCount = 999'999'999'999LL;

constexpr const char* kPlugKeys[7] = {"mhp", "mmp", "str", "def", "wis", "spi", "spd"};
constexpr const char* kPlugNames[7] = {"MaxHP", "MaxMP", "Strength", "Def.", "Wisdom", "Spirit", "Speed"};

// The 16 forms in the face sheet's order (SURVIVAL.md 9.1, charaicon) and
// their shinka rows (5.1's #).
struct FormDef {
    const char* model;
    const char* name;
    int shinka;
};
constexpr FormDef kForms[16] = {
    {"p_agumon", "Agumon", 0},          {"p_vmon", "Veemon", 1},           {"p_girumon", "Guilmon", 2},
    {"p_dorumon", "Dorumon", 3},        {"p_angelr", "Cl-Angemon", 7},     {"p_alpha", "Alphamon", 9},
    {"p_beelzeb", "Beelzemon BM", 8},   {"p_hekabut", "H-Kabuterimon", 5}, {"p_weregaru", "WereGarurumon", 4},
    {"p_wargrey", "WarGreymon", 6},     {"p_impdrafm", "IPDramonFM", 11},  {"p_impdrapm", "IPDramonPM", 12},
    {"p_dukecrim", "Gallantmon CM", 14}, {"p_susanoo", "Susanomon", 15},   {"p_metalgaru", "MetalGarurumon", 13},
    {"p_bwargrey", "BK WarGreymon", 10},
};
constexpr const char* kFormModels[16] = {"p_agumon", "p_vmon",    "p_girumon", "p_dorumon", "p_angelr",   "p_alpha",
                                         "p_beelzeb", "p_hekabut", "p_weregaru", "p_wargrey", "p_impdrafm", "p_impdrapm",
                                         "p_dukecrim", "p_susanoo", "p_metalgaru", "p_bwargrey"};
constexpr std::array<std::string_view, 4> kRookies = {"p_agumon", "p_vmon", "p_girumon", "p_dorumon"};

const FormDef* find_form(std::string_view model) {
    for (const auto& form : kForms)
        if (model == form.model) return &form;
    return nullptr;
}

const char* form_name(std::string_view model) {
    const auto* form = find_form(model);
    return form ? form->name : "?";
}

// The Data Records of 8.4 (the screen's order).
constexpr RecordDef kRecords[] = {
    {"gate_keeper", "Gate Keeper", "Defeat any Guardian.", "Cipher Core"},
    {"blossom_data", "Blossom Data", "Defeat Blossomon.", "Drain Ram"},
    {"mammoth_data", "Mammoth Data", "Defeat Mammothmon.", "Element Core"},
    {"skull_data", "Skull Data", "Defeat SkullGreymon.", "Assault Ram"},
    {"scorpio_data", "Scorpio Data", "Defeat Anomalocarimon.", "Defence ROM"},
    {"shogun_data", "Shogun Data", "Defeat TonosamaGekomon.", "the Resist ROMs, Resist Board"},
    {"diaboro_data", "Diaboro Data", "Defeat Diaboromon.", "Dread Ram"},
    {"ten_minutes", "Ten Minutes", "Survive 10:00 with any Digimon.", "Atlas Board"},
    {"fifteen_minutes", "Fifteen Minutes", "Survive 15:00 with any Digimon.", "the ?????? offer"},
    {"first_clear", "First Clear", "Defeat LordKnightmon (clear Normal).", "Hard, Extension I, Endless"},
    {"hard_clear", "Hard Clear", "Clear Hard.", "Very Hard, Extension II"},
    {"run_level_25", "Run Level 25", "Reach run level 25.", "EXP Board"},
    {"thousand", "Thousand", "Defeat 1,000 enemies in total.", "Achilles Board"},
    {"fire_adept", "Fire Adept", "Defeat 300 enemies with Fire techniques.", "the Fire PWR Medal"},
    {"ice_adept", "Ice Adept", "Defeat 300 enemies with Ice techniques.", "the Ice PWR Medal"},
    {"bolt_adept", "Bolt Adept", "Defeat 300 enemies with Bolt techniques.", "the Bolt PWR Medal"},
    {"dark_adept", "Dark Adept", "Defeat 300 enemies with Dark techniques.", "the Dark PWR Medal"},
    {"caster", "Caster", "Cast 500 techniques in total.", "Mana Board"},
    {"first_awakening", "First Awakening", "Awaken a signature weapon.", "Beast Board"},
    {"banker", "Banker", "Earn 5,000 Sim Bits in total.", "BIT Board"},
    {"crate_breaker", "Crate Breaker", "Break 100 crates in total.", "Rack Board"},
    {"rookie_mastery", "Rookie Mastery", "Clear Hard with a Rookie.", "Talis of Chaos among the ?????? results"},
    {"very_hard_clear", "Very Hard Clear", "Clear Very Hard.", "Talis of Order among the ?????? results"},
};

struct RecordGear {
    const char* record;
    const char* gear;
};
constexpr RecordGear kRecordGear[] = {
    {"gate_keeper", "cipher_core"},     {"blossom_data", "drain_ram"},       {"mammoth_data", "element_core"},
    {"skull_data", "assault_ram"},      {"scorpio_data", "defence_rom"},     {"shogun_data", "resist_rom_fire"},
    {"shogun_data", "resist_rom_ice"},  {"shogun_data", "resist_rom_bolt"},  {"shogun_data", "resist_rom_dark"},
    {"shogun_data", "resist_board"},    {"diaboro_data", "dread_ram"},       {"ten_minutes", "atlas_board"},
    {"run_level_25", "exp_board"},      {"thousand", "achilles_board"},      {"fire_adept", "pwr_medal_fire"},
    {"ice_adept", "pwr_medal_ice"},     {"bolt_adept", "pwr_medal_bolt"},    {"dark_adept", "pwr_medal_dark"},
    {"caster", "mana_board"},           {"first_awakening", "beast_board"},  {"banker", "bit_board"},
    {"crate_breaker", "rack_board"},
};

constexpr const char* kStartGear[] = {"brave_core",  "armor_board", "fire_board",  "ice_board",  "bolt_board",
                                      "dark_board",  "wisdom_chip", "spirit_chip", "speed_chip", "pwr_medal"};

// The Guardians (4.4) and the totals key of each one's defeats.
constexpr const char* kGuardians[] = {"e_blosso", "e_mammos", "e_skulgrey", "e_anomalo", "e_tonogeko", "e_diabolo", "e_ldknight"};

bool plain_text(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return c >= 0x20 && c < 0x7F; });
}

// A key or a form model: [a-z0-9_] only, 1-64 characters.
bool plain_key(std::string_view key) {
    if (key.empty() || key.size() > 64) return false;
    return std::all_of(key.begin(), key.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::optional<std::int64_t> number(std::string_view text) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::int64_t clamp_count(std::int64_t value) { return std::clamp<std::int64_t>(value, 0, kMaxCount); }

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        const std::size_t begin = i;
        while (i < line.size() && line[i] != ' ') ++i;
        if (i > begin) out.push_back(line.substr(begin, i - begin));
    }
    return out;
}

int plug_slot(std::string_view key) {
    for (int i = 0; i < 7; ++i)
        if (key == kPlugKeys[i]) return i;
    return -1;
}

bool known_gear(std::string_view key) {
    for (const char* start : kStartGear)
        if (key == start) return true;
    for (const auto& entry : kRecordGear)
        if (key == entry.gear) return true;
    return false;
}

std::int64_t clears_from(const FormStats& stats, int tier) {
    std::int64_t sum = 0;
    for (int t = std::clamp(tier, 0, 2); t < 3; ++t) sum += stats.clears[std::size_t(t)];
    return sum;
}

// Any form whose stats meet `test`.
template <typename Test>
bool any_form(const OverflowProfile& profile, Test test) {
    for (const auto& [model, stats] : profile.forms)
        if (test(model, stats)) return true;
    return false;
}

bool record_met(const OverflowProfile& p, std::string_view key) {
    auto guardian = [&](const char* family) { return p.total(std::string("defeat_") + family) > 0; };
    if (key == "gate_keeper") return p.total("guardians") > 0 || std::any_of(std::begin(kGuardians), std::end(kGuardians), guardian);
    if (key == "blossom_data") return guardian("e_blosso");
    if (key == "mammoth_data") return guardian("e_mammos");
    if (key == "skull_data") return guardian("e_skulgrey");
    if (key == "scorpio_data") return guardian("e_anomalo");
    if (key == "shogun_data") return guardian("e_tonogeko");
    if (key == "diaboro_data") return guardian("e_diabolo");
    if (key == "ten_minutes") return any_form(p, [](auto&, const FormStats& s) { return s.best_seconds >= 600.0 || s.best_endless >= 600.0; });
    if (key == "fifteen_minutes") return any_form(p, [](auto&, const FormStats& s) { return s.best_seconds >= 900.0 || s.best_endless >= 900.0; });
    if (key == "first_clear") return any_form(p, [](auto&, const FormStats& s) { return clears_from(s, 0) > 0; });
    if (key == "hard_clear") return any_form(p, [](auto&, const FormStats& s) { return clears_from(s, 1) > 0; });
    if (key == "very_hard_clear") return any_form(p, [](auto&, const FormStats& s) { return clears_from(s, 2) > 0; });
    if (key == "rookie_mastery") return any_form(p, [](const std::string& m, const FormStats& s) { return rookie_form(m) && clears_from(s, 1) > 0; });
    if (key == "run_level_25") return any_form(p, [](auto&, const FormStats& s) { return s.best_level >= 25; });
    if (key == "thousand") return p.total("defeats") >= 1000;
    if (key == "fire_adept") return p.total("tech_fire") >= 300;
    if (key == "ice_adept") return p.total("tech_ice") >= 300;
    if (key == "bolt_adept") return p.total("tech_bolt") >= 300;
    if (key == "dark_adept") return p.total("tech_dark") >= 300;
    if (key == "caster") return p.total("casts") >= 500;
    if (key == "first_awakening") return p.total("awakenings") > 0;
    if (key == "banker") return p.earned_total >= 5000;
    if (key == "crate_breaker") return p.total("crates") >= 100;
    return false;
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) out += ',';
        out += item;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Plug Lab
// ---------------------------------------------------------------------------
const char* plug_lab_key(int upcnt) { return upcnt >= 0 && upcnt < 7 ? kPlugKeys[upcnt] : nullptr; }
const char* plug_lab_name(int upcnt) { return upcnt >= 0 && upcnt < 7 ? kPlugNames[upcnt] : "?"; }
int plug_lab_price(int steps) { return steps >= kPlugLabSteps ? 0 : kPlugLabPrice * (std::max(0, steps) + 1); }

std::array<PlugLabRow, 7> plug_lab_rows(const OverflowProfile& profile, std::string_view model) {
    std::array<PlugLabRow, 7> rows{};
    for (std::size_t i = 0; i < 7; ++i) {
        const int upcnt = kPlugLabStats[i];
        const int steps = profile.plug_steps(model, upcnt);
        rows[i] = {upcnt, plug_lab_name(upcnt), steps, steps * kPlugLabPoints, plug_lab_price(steps)};
    }
    return rows;
}

const char* buy_result_text(BuyResult result) {
    switch (result) {
    case BuyResult::bought: return "Bought.";
    case BuyResult::not_enough: return "Not enough Sim Bits.";
    case BuyResult::maxed: return "This plug is at its last step.";
    case BuyResult::bad_stat: return "This stat has no plug.";
    }
    return "";
}

BuyResult buy_plug(OverflowProfile& profile, std::string_view model, int upcnt) {
    if (upcnt < 0 || upcnt >= 7 || !find_form(model)) return BuyResult::bad_stat;
    const int steps = profile.plug_steps(model, upcnt);
    if (steps >= kPlugLabSteps) return BuyResult::maxed;
    const int price = plug_lab_price(steps);
    if (profile.sim_bits < price) return BuyResult::not_enough;
    profile.sim_bits -= price;
    auto [it, added] = profile.plugs.try_emplace(std::string(model));
    if (added) it->second.fill(0);
    it->second[std::size_t(upcnt)] = steps + 1;
    return BuyResult::bought;
}

std::array<int, 11> plug_lab_points(const OverflowProfile& profile, std::string_view model) {
    std::array<int, 11> points{};
    for (int i = 0; i < 7; ++i) points[std::size_t(i)] = profile.plug_steps(model, i) * kPlugLabPoints;
    return points;
}

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------
const FormStats* OverflowProfile::form(std::string_view model) const {
    const auto found = forms.find(model);
    return found == forms.end() ? nullptr : &found->second;
}

Loadout OverflowProfile::loadout(std::string_view model) const {
    const auto found = loadouts.find(model);
    return found == loadouts.end() ? Loadout{} : found->second;
}

int OverflowProfile::plug_steps(std::string_view model, int upcnt) const {
    if (upcnt < 0 || upcnt >= 7) return 0;
    const auto found = plugs.find(model);
    return found == plugs.end() ? 0 : std::clamp(found->second[std::size_t(upcnt)], 0, kPlugLabSteps);
}

std::int64_t OverflowProfile::total(std::string_view key) const {
    const auto found = totals.find(key);
    return found == totals.end() ? 0 : found->second;
}

bool OverflowProfile::operator==(const OverflowProfile& other) const { return serialize() == other.serialize(); }

std::string time_text(double seconds) {
    const long long tenths = std::llround(std::max(0.0, seconds) * 10.0);
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld.%lld", tenths / 600, (tenths / 10) % 60, tenths % 10);
    return text;
}

std::optional<double> parse_time(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0) return std::nullopt;
    const auto minutes = number(text.substr(0, colon));
    std::string_view rest = text.substr(colon + 1);
    const auto dot = rest.find('.');
    const auto secs = number(rest.substr(0, dot));
    std::int64_t tenths = 0;
    if (dot != std::string_view::npos) {
        const auto t = number(rest.substr(dot + 1));
        if (!t || rest.size() != dot + 2) return std::nullopt;
        tenths = *t;
    }
    if (!minutes || !secs || *minutes < 0 || *minutes > 100000 || *secs < 0 || *secs > 59) return std::nullopt;
    return double(*minutes) * 60.0 + double(*secs) + double(tenths) / 10.0;
}

std::string OverflowProfile::serialize() const {
    std::ostringstream out;
    out << kHeader << kProfileVersion << '\n';
    out << "seen_intro " << (seen_intro ? 1 : 0) << '\n';
    out << "sim_bits " << sim_bits << '\n';
    out << "earned_total " << earned_total << '\n';
    out << "last " << last_form << ' ' << last_tier << '\n';
    for (const auto& [model, steps] : plugs) {
        std::string line;
        for (std::size_t i = 0; i < 7; ++i)
            if (steps[i] > 0) line += std::string(" ") + kPlugKeys[i] + ' ' + std::to_string(std::clamp(steps[i], 0, kPlugLabSteps));
        if (!line.empty()) out << "plug " << model << line << '\n';
    }
    for (const auto& [model, loadout] : loadouts) {
        if (loadout.armor.empty() && loadout.chip.empty()) continue;
        out << "loadout " << model << " armor " << (loadout.armor.empty() ? "-" : loadout.armor) << " chip "
            << (loadout.chip.empty() ? "-" : loadout.chip) << '\n';
    }
    for (const auto& [model, stats] : forms) {
        out << "form " << model << " runs " << stats.runs;
        static constexpr const char* kClears[3] = {"clears_normal", "clears_hard", "clears_veryhard"};
        for (std::size_t t = 0; t < 3; ++t)
            if (stats.clears[t] > 0) out << ' ' << kClears[t] << ' ' << stats.clears[t];
        out << " best_time " << time_text(stats.best_seconds) << " best_level " << stats.best_level << " best_defeats " << stats.best_defeats;
        if (stats.best_endless > 0) out << " best_endless " << time_text(stats.best_endless);
        for (const auto& [key, value] : stats.extra) out << ' ' << key << ' ' << value;
        out << '\n';
    }
    for (const auto& [key, date] : records) out << "record " << key << ' ' << date << '\n';
    for (const auto& [key, value] : totals) out << "total " << key << ' ' << value << '\n';
    for (const auto& line : unknown) out << line << '\n';
    out << "end\n"; // a complete file (only a complete one becomes the .bak)
    return out.str();
}

std::optional<OverflowProfile> OverflowProfile::parse(std::string_view text, bool* complete) {
    if (complete) *complete = false;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3); // a BOM (an editor's)
    OverflowProfile profile;
    bool header = false, ended = false;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        const bool last = end == std::string_view::npos;
        if (last) end = text.size();
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (line.ends_with('\r')) line.remove_suffix(1);
        // A last line without its line end was cut (the writer ends every
        // line): not trusted, and the file is not complete.
        if (last) break;
        if (!header) {
            // The first line is the header, or this is no profile.
            if (!line.starts_with(kHeader)) return std::nullopt;
            const auto version = number(line.substr(kHeader.size()));
            if (!version || *version < 1) return std::nullopt;
            profile.version = int(std::min<std::int64_t>(*version, 1'000'000));
            header = true;
            continue;
        }
        if (ended) {
            // Text after "end": the file is not what was written.
            if (!line.empty()) ended = false;
            else continue;
        }
        if (line.empty()) continue;
        if (line == "end") {
            ended = true;
            continue;
        }
        if (line.size() > kMaxLine || !plain_text(line)) continue; // damaged bytes: skipped
        const auto f = fields(line);
        if (f.empty()) continue;
        const std::string_view kind = f[0];
        bool known = true, good = false;
        if (kind == "seen_intro" && f.size() == 2) {
            if (const auto v = number(f[1])) profile.seen_intro = *v != 0, good = true;
        } else if (kind == "sim_bits" && f.size() == 2) {
            if (const auto v = number(f[1])) profile.sim_bits = clamp_count(*v), good = true;
        } else if (kind == "earned_total" && f.size() == 2) {
            if (const auto v = number(f[1])) profile.earned_total = clamp_count(*v), good = true;
        } else if (kind == "last" && f.size() == 3) {
            if (plain_key(f[1]) && plain_key(f[2])) profile.last_form = f[1], profile.last_tier = f[2], good = true;
        } else if (kind == "plug" && f.size() >= 2 && f.size() % 2 == 0) {
            if (plain_key(f[1])) {
                std::array<int, 7> steps{};
                if (const auto old = profile.plugs.find(f[1]); old != profile.plugs.end()) steps = old->second;
                good = true;
                for (std::size_t i = 2; i + 1 < f.size(); i += 2) {
                    const int slot = plug_slot(f[i]);
                    const auto v = number(f[i + 1]);
                    if (slot < 0 || !v) continue; // a later version's stat: not this one's
                    steps[std::size_t(slot)] = int(std::clamp<std::int64_t>(*v, 0, kPlugLabSteps));
                }
                profile.plugs[std::string(f[1])] = steps;
            }
        } else if (kind == "loadout" && f.size() == 6 && f[2] == "armor" && f[4] == "chip") {
            if (plain_key(f[1]) && (f[3] == "-" || plain_key(f[3])) && (f[5] == "-" || plain_key(f[5]))) {
                Loadout loadout;
                if (f[3] != "-") loadout.armor = f[3];
                if (f[5] != "-") loadout.chip = f[5];
                profile.loadouts[std::string(f[1])] = loadout;
                good = true;
            }
        } else if (kind == "form" && f.size() >= 2 && f.size() % 2 == 0) {
            if (plain_key(f[1])) {
                FormStats stats;
                good = true;
                for (std::size_t i = 2; i + 1 < f.size() && good; i += 2) {
                    const std::string_view key = f[i], value = f[i + 1];
                    auto count = [&](int& target) {
                        if (const auto v = number(value)) target = int(std::clamp<std::int64_t>(*v, 0, 1'000'000'000));
                        else good = false;
                    };
                    auto time = [&](double& target) {
                        if (const auto v = parse_time(value)) target = *v;
                        else good = false;
                    };
                    if (key == "runs") count(stats.runs);
                    else if (key == "clears_normal") count(stats.clears[0]);
                    else if (key == "clears_hard") count(stats.clears[1]);
                    else if (key == "clears_veryhard") count(stats.clears[2]);
                    else if (key == "best_time") time(stats.best_seconds);
                    else if (key == "best_level") count(stats.best_level);
                    else if (key == "best_defeats") count(stats.best_defeats);
                    else if (key == "best_endless") time(stats.best_endless);
                    else if (plain_key(key)) stats.extra.push_back({std::string(key), std::string(value)});
                }
                if (good) profile.forms[std::string(f[1])] = stats;
            }
        } else if (kind == "record" && f.size() == 3) {
            if (plain_key(f[1]))
                if (const auto v = number(f[2]); v && *v > 0) profile.records[std::string(f[1])] = *v, good = true;
        } else if (kind == "total" && f.size() == 3) {
            if (plain_key(f[1]))
                if (const auto v = number(f[2])) profile.totals[std::string(f[1])] = clamp_count(*v), good = true;
        } else if (kind == "seen_intro" || kind == "sim_bits" || kind == "earned_total" || kind == "last" || kind == "plug" ||
                   kind == "loadout" || kind == "form" || kind == "record" || kind == "total") {
            good = false; // a known line with the wrong shape: damaged, dropped
        } else {
            known = false;
        }
        (void)good;
        if (!known && plain_key(kind) && profile.unknown.size() < kMaxUnknownLines) profile.unknown.push_back(std::string(line));
    }
    if (!header) return std::nullopt;
    if (complete) *complete = ended;
    return profile;
}

bool complete_profile(std::span<const std::uint8_t> bytes) {
    bool complete = false;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return OverflowProfile::parse(text, &complete).has_value() && complete;
}

// ---------------------------------------------------------------------------
// Records and gear
// ---------------------------------------------------------------------------
std::span<const RecordDef> record_table() { return kRecords; }

const RecordDef* find_record(std::string_view key) {
    for (const auto& record : kRecords)
        if (key == record.key) return &record;
    return nullptr;
}

std::vector<std::string> record_gear(std::string_view key) {
    std::vector<std::string> out;
    for (const auto& entry : kRecordGear)
        if (key == entry.record) out.push_back(entry.gear);
    return out;
}

std::span<const char* const> start_gear() { return kStartGear; }

std::vector<std::string> gear_open(const OverflowProfile& profile) {
    std::vector<std::string> out;
    for (const auto& record : kRecords) {
        if (!profile.record(record.key)) continue;
        for (auto& gear : record_gear(record.key))
            if (std::find(out.begin(), out.end(), gear) == out.end()) out.push_back(std::move(gear));
    }
    return out;
}

std::vector<std::string> opened_gear(const OverflowProfile& profile) {
    std::vector<std::string> out(std::begin(kStartGear), std::end(kStartGear));
    for (auto& gear : gear_open(profile))
        if (std::find(out.begin(), out.end(), gear) == out.end()) out.push_back(std::move(gear));
    return out;
}

bool gear_is_core(std::string_view key) { return known_gear(key) && key.ends_with("_core"); }
bool gear_is_board(std::string_view key) { return known_gear(key) && key.ends_with("_board"); }
bool gear_is_chip(std::string_view key) { return known_gear(key) && !gear_is_core(key) && !gear_is_board(key); }
// (abChip WU 0, BLUEAB 1: the Resist ROMs and the Defence ROM.)
bool gear_core_pocket_only(std::string_view key) { return gear_is_chip(key) && (key.starts_with("resist_rom") || key == "defence_rom"); }

bool loadout_allowed(const OverflowProfile& profile, const Loadout& loadout) {
    const auto opened = opened_gear(profile);
    auto is_open = [&](const std::string& key) { return std::find(opened.begin(), opened.end(), key) != opened.end(); };
    if (!loadout.armor.empty() && (!is_open(loadout.armor) || !(gear_is_core(loadout.armor) || gear_is_board(loadout.armor)))) return false;
    if (!loadout.chip.empty() && (!is_open(loadout.chip) || !gear_is_chip(loadout.chip))) return false;
    if (!loadout.chip.empty() && gear_core_pocket_only(loadout.chip) && !gear_is_core(loadout.armor)) return false;
    return true;
}

std::span<const char* const> overflow_forms() { return kFormModels; }

bool rookie_form(std::string_view model) { return std::find(kRookies.begin(), kRookies.end(), model) != kRookies.end(); }

bool form_unlocked(const OverflowProfile& p, std::string_view model, std::uint32_t story_forms) {
    const auto* form = find_form(model);
    if (!form) return false;
    if (rookie_form(model)) return true;
    if (story_forms & (1u << form->shinka)) return true;
    auto stats = [&](std::string_view m) -> FormStats {
        const auto* s = p.form(m);
        return s ? *s : FormStats{};
    };
    auto survived = [&](std::string_view m) { return stats(m).best_seconds >= 600.0 || stats(m).best_endless >= 600.0; };
    auto cleared = [&](std::string_view m, int tier) { return clears_from(stats(m), tier) > 0; };
    if (model == "p_hekabut") return survived("p_agumon");
    if (model == "p_weregaru") return survived("p_vmon");
    if (model == "p_angelr") return survived("p_girumon");
    if (model == "p_wargrey") return std::any_of(kRookies.begin(), kRookies.end(), survived);
    if (model == "p_alpha") return cleared("p_dorumon", 0);
    if (model == "p_susanoo") return std::all_of(kRookies.begin(), kRookies.end(), [&](std::string_view m) { return cleared(m, 0); });
    if (model == "p_impdrapm") return cleared("p_vmon", 0) || cleared("p_weregaru", 0);
    if (model == "p_dukecrim") return cleared("p_girumon", 0) || cleared("p_angelr", 0);
    if (model == "p_bwargrey") return cleared("p_wargrey", 0);
    if (model == "p_beelzeb") return any_form(p, [](auto&, const FormStats& s) { return s.best_defeats >= 1500; });
    if (model == "p_impdrafm") return any_form(p, [](auto&, const FormStats& s) { return clears_from(s, 1) > 0; });
    if (model == "p_metalgaru") return any_form(p, [](auto&, const FormStats& s) { return s.best_level >= 40; });
    return false;
}

std::string form_unlock_text(std::string_view model) {
    if (rookie_form(model)) return "";
    if (model == "p_hekabut") return "Survive 10:00 with Agumon.";
    if (model == "p_weregaru") return "Survive 10:00 with Veemon.";
    if (model == "p_angelr") return "Survive 10:00 with Guilmon.";
    if (model == "p_wargrey") return "Survive 10:00 with any Rookie.";
    if (model == "p_alpha") return "Clear Normal with Dorumon.";
    if (model == "p_susanoo") return "Clear Normal with all four Rookies.";
    if (model == "p_impdrapm") return "Clear Normal with Veemon or WereGarurumon.";
    if (model == "p_dukecrim") return "Clear Normal with Guilmon or Cl-Angemon.";
    if (model == "p_bwargrey") return "Clear Normal with WarGreymon.";
    if (model == "p_beelzeb") return "Defeat 1,500 enemies in one run.";
    if (model == "p_impdrafm") return "Clear Hard with any Digimon.";
    if (model == "p_metalgaru") return "Reach run level 40.";
    return std::string(form_name(model)) + " is not in the simulator.";
}

// ---------------------------------------------------------------------------
// A run's end
// ---------------------------------------------------------------------------
int defeat_bits(int level, int bit_rate, std::uint64_t& random) {
    auto next = [&random] {
        // splitmix64 (the randomizer's and the director's pattern)
        std::uint64_t z = (random += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    if (int(next() % 100) >= kDefeatBitsPercent) return 0;
    // The original's bands (items.cpp, 803B13F8): up to level, low, high.
    static constexpr int kBands[][3] = {{18, 10, 55},   {38, 55, 100},  {59, 87, 195},  {93, 162, 350},
                                        {107, 232, 520}, {143, 340, 700}, {202, 446, 860}, {9999, 566, 1160}};
    int lo = 566, hi = 1160;
    for (const auto& band : kBands)
        if (level <= band[0]) {
            lo = band[1];
            hi = band[2];
            break;
        }
    const int amount = lo + int(next() % std::uint64_t(hi - lo));
    return std::max(0, amount * (std::max(-100, bit_rate) + 100) / 100);
}

std::uint64_t bits_seed(std::uint32_t run_seed) { return 0xB17B175EED000000ull ^ (std::uint64_t(run_seed) * 0x9E3779B97F4A7C15ull); }

double tier_factor(int tier) { return tier >= 2 ? 2.0 : tier == 1 ? 1.5 : 1.0; }

std::int64_t run_sim_bits(const RunReport& report) {
    const double factor = tier_factor(report.tier);
    const double bits = double(std::max<std::int64_t>(0, report.bits)) * factor + (report.cleared() ? 500.0 * factor : 0.0);
    return clamp_count(std::llround(bits));
}

std::vector<std::string> check_records(OverflowProfile& profile, std::int64_t date) {
    std::vector<std::string> added;
    for (const auto& record : kRecords) {
        if (profile.record(record.key) || !record_met(profile, record.key)) continue;
        profile.records[record.key] = date > 0 ? date : 1;
        added.push_back(record.key);
    }
    return added;
}

RunReward add_run(OverflowProfile& profile, const RunReport& report, std::int64_t date) {
    RunReward reward;
    reward.sim_bits = run_sim_bits(report);
    profile.sim_bits = clamp_count(profile.sim_bits + reward.sim_bits);
    profile.earned_total = clamp_count(profile.earned_total + reward.sim_bits);
    auto& stats = profile.forms[report.form];
    ++stats.runs;
    const double seconds = std::max(0.0, report.seconds);
    if (report.endless) stats.best_endless = std::max(stats.best_endless, seconds);
    else stats.best_seconds = std::max(stats.best_seconds, seconds);
    stats.best_level = std::max(stats.best_level, report.level);
    stats.best_defeats = std::max(stats.best_defeats, report.defeats);
    if (report.cleared() && !report.endless) ++stats.clears[std::size_t(std::clamp(report.tier, 0, 2))];
    auto add = [&](const std::string& key, std::int64_t amount) {
        if (amount > 0) profile.totals[key] = clamp_count(profile.total(key) + amount);
    };
    add("runs", 1);
    add("defeats", report.defeats);
    add("casts", report.casts);
    add("crates", report.crates);
    add("seconds", std::llround(seconds));
    add("clears", report.cleared() ? 1 : 0);
    add("awakenings", report.awakened ? 1 : 0);
    static constexpr const char* kTech[4] = {"tech_fire", "tech_ice", "tech_bolt", "tech_dark"};
    for (std::size_t e = 0; e < 4; ++e) add(kTech[e], report.tech_defeats[e]);
    for (const auto& family : report.guardians) {
        if (std::find(std::begin(kGuardians), std::end(kGuardians), family) == std::end(kGuardians)) continue;
        add("guardians", 1);
        add("defeat_" + family, 1);
    }
    reward.new_records = check_records(profile, date);
    return reward;
}

// ---------------------------------------------------------------------------
// The run's request
// ---------------------------------------------------------------------------
std::array<int, 11> parse_plug_lab(std::string_view text) {
    std::array<int, 11> points{};
    std::size_t i = 0, at = 0;
    while (at <= text.size() && i < 11) {
        const std::size_t end = std::min(text.find(',', at), text.size());
        const auto v = number(text.substr(at, end - at));
        if (!v) return {};
        points[i++] = int(std::clamp<std::int64_t>(*v, 0, 10'000));
        at = end + 1;
    }
    return i == 11 ? points : std::array<int, 11>{};
}

void apply_meta(const OverflowProfile& profile, RunRequest& request) {
    const Loadout loadout = profile.loadout(request.form);
    if (loadout_allowed(profile, loadout)) {
        if (!loadout.armor.empty()) request.options.try_emplace("armor", loadout.armor);
        if (!loadout.chip.empty()) request.options.try_emplace("chip", loadout.chip);
    }
    const auto points = plug_lab_points(profile, request.form);
    if (std::any_of(points.begin(), points.end(), [](int p) { return p != 0; })) {
        std::string text;
        for (std::size_t i = 0; i < points.size(); ++i) text += (i ? "," : "") + std::to_string(points[i]);
        request.options.try_emplace("plug_lab", text);
    }
    if (const auto open = gear_open(profile); !open.empty()) request.options.try_emplace("gear_open", join(open));
}

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------
void MetaStore::open(const std::filesystem::path& path) {
    path_ = path;
    profile_ = {};
    source_ = LoadSource::none;
    note_.clear();
    error_.clear();
    last_report_.reset();
    last_reward_ = {};
    if (path_.empty()) return;
    const auto main = save::read_whole_file(path_);
    bool complete = false;
    std::optional<OverflowProfile> parsed;
    if (main) {
        parsed = OverflowProfile::parse(std::string_view(reinterpret_cast<const char*>(main->data()), main->size()), &complete);
        if (parsed && complete) {
            profile_ = std::move(*parsed);
            source_ = LoadSource::file;
            return;
        }
    }
    // Missing or damaged: the last complete file, else what is left.
    auto backup = path_;
    backup += ".bak";
    const auto name = path_.filename().string();
    if (main) {
        // The damaged file stays beside it until the next damaged one.
        auto damaged = path_;
        damaged += ".damaged";
        std::error_code ec;
        std::filesystem::copy_file(path_, damaged, std::filesystem::copy_options::overwrite_existing, ec);
        note_ = name + " was damaged (kept as " + damaged.filename().string() + ")";
    }
    if (const auto bak = save::read_whole_file(backup)) {
        bool bak_complete = false;
        if (auto from_backup = OverflowProfile::parse(std::string_view(reinterpret_cast<const char*>(bak->data()), bak->size()), &bak_complete);
            from_backup && bak_complete) {
            profile_ = std::move(*from_backup);
            source_ = LoadSource::backup;
            note_ += (note_.empty() ? name + " was missing" : std::string()) + "; restored from " + backup.filename().string();
            return;
        }
    }
    if (parsed) {
        profile_ = std::move(*parsed);
        source_ = LoadSource::salvaged;
        note_ += "; kept what it still holds";
        return;
    }
    source_ = main ? LoadSource::fresh : LoadSource::none;
    if (main) note_ += "; a new profile starts";
}

bool MetaStore::save() {
    if (path_.empty()) return true; // a test run's profile: memory only
    const auto text = profile_.serialize();
    error_.clear();
    return save::write_file_atomic(path_, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), true, &error_,
                                   complete_profile);
}

bool MetaStore::mark_intro_seen() {
    if (profile_.seen_intro) return false;
    profile_.seen_intro = true;
    return save();
}

bool MetaStore::set_loadout(std::string_view model, const Loadout& loadout) {
    if (!find_form(model) || !loadout_allowed(profile_, loadout)) return false;
    if (profile_.loadout(model) == loadout) return false;
    profile_.loadouts[std::string(model)] = loadout;
    return save();
}

BuyResult MetaStore::buy_plug(std::string_view model, int upcnt) {
    const auto result = overflow::buy_plug(profile_, model, upcnt);
    if (result == BuyResult::bought) save();
    return result;
}

bool MetaStore::set_last(std::string_view model, std::string_view tier) {
    if (!plain_key(model) || !plain_key(tier) || (profile_.last_form == model && profile_.last_tier == tier)) return false;
    profile_.last_form = model;
    profile_.last_tier = tier;
    return save();
}

RunReward MetaStore::record_run(const RunReport& report, std::int64_t date) {
    last_report_ = report;
    last_reward_ = add_run(profile_, report, date > 0 ? date : today());
    save();
    return last_reward_;
}

bool MetaStore::replace(OverflowProfile profile) {
    profile_ = std::move(profile);
    return save();
}

MetaStore& meta() {
    static MetaStore store;
    return store;
}

std::filesystem::path profile_path(const std::filesystem::path& program_folder, bool test_run) {
    if (const char* file = std::getenv("DW4_OVERFLOW_PROFILE"); file && *file) return std::filesystem::path(file);
    if (test_run) return {};
    return program_folder / "saves" / "overflow.dat";
}

std::int64_t today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return std::int64_t(local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

} // namespace dw4::game::overflow
