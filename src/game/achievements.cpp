#include "achievements.hpp"

#include "dw4/card_store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace dw4::game {
namespace {

using C = AchievementCategory;
using Kind = engine::PortEvent::Kind;

// The rule list. Ids never change once released (profiles and the online
// account key on them); titles and descriptions may.
constexpr AchievementDef kAchievements[] = {
    // Story (the boss stages' defeat flags and the clear flags 406..408).
    {"story_apocalymon", "Tower Toppler", "Defeat Apocalymon in Apokarimon Space.", C::story},
    {"story_belphemon", "Sloth Slayer", "Defeat Belphemon at Demon Shift.", C::story},
    {"story_lucemon", "Fallen Angel", "Defeat Lucemon at the Hades Obelisk.", C::story},
    {"story_lordknightmon", "Knight Fall", "Defeat LordKnightmon in the Electro Mine.", C::story},
    {"story_daemon", "Demon Lord", "Defeat Daemon in Zone X.", C::story},
    {"clear_normal", "Digital World Saved", "Finish the story on Normal.", C::story},
    {"clear_hard", "Hardened Tamer", "Finish the story on Hard.", C::story},
    {"clear_veryhard", "Legend of the Digital World", "Finish the story on Very Hard.", C::story},
    // Dungeon bosses (their ded_e_*.lua death scripts).
    {"boss_first", "Boss Breaker", "Defeat your first boss.", C::bosses},
    {"boss_blossomon", "Withered Bloom", "Defeat Blossomon.", C::bosses},
    {"boss_mammothmon", "Mammoth Task", "Defeat Mammothmon.", C::bosses},
    {"boss_skullgreymon", "Bone Breaker", "Defeat SkullGreymon.", C::bosses},
    {"boss_anomalocarimon", "Deep Trouble", "Defeat Anomalocarimon.", C::bosses},
    {"boss_tonosama", "Royal Flush", "Defeat Tonosama Gekomon.", C::bosses},
    {"boss_vine_tunnel", "Jungle Machine", "Defeat the boss of the Vine Tunnel.", C::bosses},
    {"boss_diaboromon", "Infinite Loop", "Defeat Diaboromon.", C::bosses},
    {"boss_mecha_nest", "Nest Cleaner", "Defeat the boss of the Mecha Nest.", C::bosses},
    {"boss_dungeon_all", "Key Keeper", "Defeat all nine dungeon bosses.", C::bosses, "dungeon_bosses", 9},
    // Challenges.
    {"nodmg_boss", "Untouchable", "Defeat a boss without losing HP in its stage.", C::challenge},
    {"nodmg_daemon_vh", "Flawless Demon Lord", "Defeat Daemon on Very Hard without losing HP in Zone X.", C::challenge},
    {"deathless_clear", "Never Fallen", "Finish the story with a new character that was never defeated.", C::challenge},
    {"solo_veryhard", "Lone Tamer", "Finish the story on Very Hard with a character that only ever played it alone.", C::challenge},
    {"lowlevel_apocalymon", "Underdog", "Defeat Apocalymon at level 12 or below.", C::challenge},
    {"speed_normal", "Swift Tamer", "Finish the story with a new character in under 8 hours of play.", C::challenge},
    {"speed_fast", "Speed Demon", "Finish the story with a new character in under 4 hours of play.", C::challenge},
    {"rookie_clear", "Rookie Pride", "Finish the story with a new character that never used a form past the rookies.", C::challenge},
    {"quest_veryhard", "Quest Veteran", "Reach a quest's goal in a Very Hard quest dungeon.", C::challenge},
    // Levels.
    {"lv_10", "Getting Stronger", "Reach level 10.", C::growth, "best_level", 10},
    {"lv_50", "Seasoned", "Reach level 50.", C::growth, "best_level", 50},
    {"lv_99", "Veteran", "Reach level 99.", C::growth, "best_level", 99},
    {"lv_200", "Elite", "Reach level 200.", C::growth, "best_level", 200},
    {"lv_500", "Unstoppable", "Reach level 500.", C::growth, "best_level", 500},
    {"lv_999", "Maximum Power", "Reach level 999.", C::growth, "best_level", 999},
    // Forms.
    {"form_evolve", "Digivolution!", "Reach a form past the rookies.", C::forms},
    {"form_rookies", "Rookie Squad", "Reach all four rookie forms with one character.", C::forms},
    {"form_eight", "Halfway Evolved", "Reach eight forms with one character.", C::forms, "best_forms", 8},
    {"form_all", "Complete Evolution", "Reach all sixteen forms with one character.", C::forms, "best_forms", 16},
    {"form_susanoomon", "Fusion of Legends", "Reach Susanoomon.", C::forms},
    {"form_master", "Form Master", "Raise all sixteen forms of one character to level 99.", C::forms, "best_forms_99", 16},
    // Techniques.
    {"tech_first", "Technique!", "Learn a new technique.", C::techniques},
    {"tech_full", "Full Repertoire", "Know all six techniques of a form.", C::techniques},
    {"tech_rank5", "Grandmaster Technique", "Raise a technique to its fifth rank.", C::techniques},
    // Collection.
    {"card_first", "Collector", "Get your first card.", C::collection},
    {"card_rare", "Lucky Draw", "Draw a rare card from a booster pack.", C::collection},
    {"card_25", "Half Album", "Own 25 cards.", C::collection, "best_cards", 25},
    {"card_50", "Complete Album", "Own all 50 cards.", C::collection, "best_cards", 50},
    {"bits_100k", "Saving Up", "Hold 100,000 bits (with the bank).", C::collection},
    {"bits_1m", "Millionaire", "Hold 1,000,000 bits (with the bank).", C::collection},
    {"bits_max", "Bit Tycoon", "Hold 9,999,999 bits in your wallet.", C::collection},
    {"hoarder", "Hoarder", "Fill all 36 places of your device folder.", C::collection},
    {"item_rank5", "Master Craft", "Hold a device of rank 5.", C::collection},
    {"item_pink", "Legendary Find", "Hold a device worth 1000 or more.", C::collection},
    {"numemon_free", "Numemon Liberator", "Free all ten Numemon.", C::collection, "numemon_freed", 10},
    // Quests.
    {"quest_first", "Side Job", "Reach the goal of a quest.", C::quests},
    {"quest_all", "Quest Master", "Reach the goals of all eight quests.", C::quests, "quests", 8},
    // Combat (counted for the players of this machine).
    {"kills_100", "Hunter", "Defeat 100 enemies.", C::combat, "kills", 100},
    {"kills_1000", "Slayer", "Defeat 1,000 enemies.", C::combat, "kills", 1000},
    {"kills_10000", "Exterminator", "Defeat 10,000 enemies.", C::combat, "kills", 10000},
    {"kills_100000", "Digital Legend", "Defeat 100,000 enemies.", C::combat, "kills", 100000},
    {"crit_1000", "Critical Mass", "Land 1,000 critical hits.", C::combat, "criticals", 1000},
    {"numemon_hunter", "Numemon Hunter", "Defeat 500 Numemon.", C::combat, "numemon_kills", 500},
    {"first_defeat", "Ouch", "Be defeated for the first time.", C::combat},
    {"raise_ally", "Second Chance", "See a defeated partner get back up in the same stage.", C::combat},
    // Time (in-game time without stage loads).
    {"time_1h", "Getting Started", "Play for 1 hour.", C::time, "play_minutes", 60},
    {"time_10h", "Dedicated", "Play for 10 hours.", C::time, "play_minutes", 600},
    {"time_100h", "Devoted", "Play for 100 hours.", C::time, "play_minutes", 6000},
    {"time_500h", "Lifer", "Play for 500 hours.", C::time, "play_minutes", 30000},
    // Together.
    {"mp_couch", "Couch Co-op", "Defeat a boss with two or more players.", C::multiplayer},
    {"mp_fullhouse", "Full House", "Defeat a boss with four players.", C::multiplayer},
    {"online_join", "Connected", "Play an online session.", C::multiplayer},
    {"online_host", "Room Host", "Host an online session with three or more players.", C::multiplayer},
    {"online_raid", "Raid Night", "Defeat a boss in an online session.", C::multiplayer},
};

// The online form of every achievement, in the order of kAchievements
// (docs/SEAMLESS.md 11). Events are the moments the game sees (story and
// boss credits go to every member of the instance it happened in); stats are
// the counters the server adds up; states are what the server reads from the
// uploaded online character. Goals are the server's units: speed limits in
// seconds of in-game time (loads excluded), lowlevel the highest level.
using OK = AchievementOnlineKind;
constexpr AchievementOnline kOnline[] = {
    {"story_apocalymon", OK::event},
    {"story_belphemon", OK::event},
    {"story_lucemon", OK::event},
    {"story_lordknightmon", OK::event},
    {"story_daemon", OK::event},
    {"clear_normal", OK::event},
    {"clear_hard", OK::event},
    {"clear_veryhard", OK::event},
    {"boss_first", OK::event},
    {"boss_blossomon", OK::event},
    {"boss_mammothmon", OK::event},
    {"boss_skullgreymon", OK::event},
    {"boss_anomalocarimon", OK::event},
    {"boss_tonosama", OK::event},
    {"boss_vine_tunnel", OK::event},
    {"boss_diaboromon", OK::event},
    {"boss_mecha_nest", OK::event},
    {"boss_dungeon_all", OK::event},
    {"nodmg_boss", OK::event},
    {"nodmg_daemon_vh", OK::event},
    {"deathless_clear", OK::event, 0, nullptr, true},
    {"solo_veryhard", OK::event, 0, nullptr, true},
    {"lowlevel_apocalymon", OK::event, 12},
    {"speed_normal", OK::event, 8 * 3600, nullptr, true},
    {"speed_fast", OK::event, 4 * 3600, nullptr, true},
    {"rookie_clear", OK::event, 0, nullptr, true},
    {"quest_veryhard", OK::event},
    {"lv_10", OK::state, 10},
    {"lv_50", OK::state, 50},
    {"lv_99", OK::state, 99},
    {"lv_200", OK::state, 200},
    {"lv_500", OK::state, 500},
    {"lv_999", OK::state, 999},
    {"form_evolve", OK::event},
    {"form_rookies", OK::state},
    {"form_eight", OK::state, 8},
    {"form_all", OK::state, 16},
    {"form_susanoomon", OK::state},
    {"form_master", OK::state, 16},
    {"tech_first", OK::event},
    {"tech_full", OK::event},
    {"tech_rank5", OK::event},
    {"card_first", OK::state, 1},
    {"card_rare", OK::event},
    {"card_25", OK::state, 25},
    {"card_50", OK::state, 50},
    {"bits_100k", OK::state, 100000},
    {"bits_1m", OK::state, 1000000},
    {"bits_max", OK::state, 9999999},
    {"hoarder", OK::state, 36},
    {"item_rank5", OK::state, 5},
    {"item_pink", OK::state, 1000},
    {"numemon_free", OK::event},
    {"quest_first", OK::event},
    {"quest_all", OK::event},
    {"kills_100", OK::stat, 100, "enemies_defeated"},
    {"kills_1000", OK::stat, 1000, "enemies_defeated"},
    {"kills_10000", OK::stat, 10000, "enemies_defeated"},
    {"kills_100000", OK::stat, 100000, "enemies_defeated"},
    {"crit_1000", OK::stat, 1000, "critical_hits"},
    {"numemon_hunter", OK::stat, 500, "numemon_defeated"},
    {"first_defeat", OK::event},
    {"raise_ally", OK::event},
    {"time_1h", OK::stat, 3600, "play_seconds"},
    {"time_10h", OK::stat, 36000, "play_seconds"},
    {"time_100h", OK::stat, 360000, "play_seconds"},
    {"time_500h", OK::stat, 1800000, "play_seconds"},
    {"mp_couch", OK::event},
    {"mp_fullhouse", OK::event},
    {"online_join", OK::event},
    {"online_host", OK::event},
    {"online_raid", OK::event},
};
static_assert(std::size(kOnline) == std::size(kAchievements), "one online form per achievement");

constexpr AchievementCounter kCounters[] = {
    {"kills", "enemies_defeated"},   {"criticals", "critical_hits"}, {"numemon_kills", "numemon_defeated"},
    {"bosses", "bosses_defeated"},   {"defeats", "defeats"},         {"play_fields", "play_seconds"},
};

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

// The dungeon bosses (death script keys) and their achievements.
struct DungeonBoss {
    const char* key;
    const char* id;
};
constexpr DungeonBoss kDungeonBosses[] = {
    {"blosso", "boss_blossomon"}, {"mammos", "boss_mammothmon"}, {"skulgrey", "boss_skullgreymon"},
    {"anomalo", "boss_anomalocarimon"}, {"tonogeko", "boss_tonosama"}, {"mecha4", "boss_vine_tunnel"},
    {"diabolo", "boss_diaboromon"}, {"mecha5", "boss_mecha_nest"}, {"ldknight", nullptr},
};

constexpr std::int64_t kFieldsPerHour = 215784; // 59.94 fields per second

bool starts_with(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
    return true;
}

} // namespace

const char* achievement_category_name(AchievementCategory category) {
    switch (category) {
    case C::story: return "Story";
    case C::bosses: return "Bosses";
    case C::challenge: return "Challenges";
    case C::growth: return "Levels";
    case C::forms: return "Forms";
    case C::techniques: return "Techniques";
    case C::collection: return "Collection";
    case C::quests: return "Quests";
    case C::combat: return "Combat";
    case C::time: return "Time";
    case C::multiplayer: return "Together";
    }
    return "";
}

std::span<const AchievementDef> achievement_list() { return kAchievements; }

const AchievementDef* find_achievement(std::string_view id) {
    for (const auto& def : kAchievements)
        if (id == def.id) return &def;
    return nullptr;
}

const char* achievement_online_kind_name(AchievementOnlineKind kind) {
    switch (kind) {
    case OK::event: return "event";
    case OK::stat: return "stat";
    case OK::state: return "state";
    }
    return "";
}

const char* achievement_category_key(AchievementCategory category) {
    switch (category) {
    case C::story: return "story";
    case C::bosses: return "boss";
    case C::challenge: return "challenge";
    case C::growth: return "level";
    case C::forms: return "digivolution";
    case C::techniques: return "technique";
    case C::collection: return "collection";
    case C::quests: return "quest";
    case C::combat: return "combat";
    case C::time: return "time";
    case C::multiplayer: return "multiplayer";
    }
    return "";
}

std::span<const AchievementOnline> achievement_online_list() { return kOnline; }

const AchievementOnline* find_achievement_online(std::string_view id) {
    for (const auto& online : kOnline)
        if (id == online.id) return &online;
    return nullptr;
}

std::span<const AchievementCounter> achievement_online_counters() { return kCounters; }

std::string achievement_catalogue_json() {
    std::string out = "{\n  \"format\": \"dw4-achievements/1\",\n";
    out += "  \"source\": \"src/game/achievements.cpp (dw4_achievements_tests --export)\",\n";
    out += "  \"achievements\": [\n";
    for (std::size_t i = 0; i < std::size(kAchievements); ++i) {
        const auto& def = kAchievements[i];
        const auto& online = kOnline[i];
        out += "    {\"id\": " + json_text(def.id) + ", \"title\": " + json_text(def.title) + ", \"description\": " +
               json_text(def.description) + ", \"category\": " + json_text(achievement_category_key(def.category)) +
               ", \"kind\": " + json_text(achievement_online_kind_name(online.kind)) +
               ", \"goal\": " + (online.goal ? std::to_string(online.goal) : std::string("null")) +
               ", \"stat\": " + (online.server_stat ? json_text(online.server_stat) : std::string("null")) +
               ", \"ownerOnly\": " + (online.owner_only ? "true" : "false") + "}";
        out += i + 1 < std::size(kAchievements) ? ",\n" : "\n";
    }
    out += "  ]\n}\n";
    return out;
}

const char* achievement_gate_text(AchievementGate gate) {
    switch (gate) {
    case AchievementGate::open: return "Achievements count.";
    case AchievementGate::disabled: return "Achievements are switched off.";
    case AchievementGate::cheats: return "Paused: a cheat is on in this stage.";
    case AchievementGate::rebalancing: return "Paused: rebalancing is not at 100%.";
    case AchievementGate::save_state: return "Paused until the next stage: a save state was loaded.";
    case AchievementGate::replay: return "Paused while a save state replays.";
    case AchievementGate::test_run: return "Off in test runs.";
    case AchievementGate::bot: return "Off while a bot plays.";
    }
    return "";
}

std::int64_t AchievementProfile::stat(std::string_view key) const {
    const auto found = stats.find(std::string(key));
    return found == stats.end() ? 0 : found->second;
}

std::string AchievementProfile::serialize() const {
    std::ostringstream out;
    out << "DW4PROFILE 1\n";
    for (const auto& [id, time] : unlocked) out << "unlock " << id << ' ' << time << '\n';
    for (const auto& [id, time] : unlocked_randomizer) out << "unlock_randomizer " << id << ' ' << time << '\n';
    for (const auto& [key, value] : stats) out << "stat " << key << ' ' << value << '\n';
    for (const auto& [unique, c] : characters)
        out << "char " << unique << ' ' << c.play_frames << ' ' << c.defeats << ' ' << int(c.from_start) << ' ' << int(c.assisted)
            << ' ' << int(c.coop_very_hard) << ' ' << int(c.beyond_rookie) << '\n';
    out << "end\n"; // a complete file (save: only a complete one becomes the .bak)
    return out.str();
}

std::optional<AchievementProfile> AchievementProfile::parse(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line) || line.rfind("DW4PROFILE ", 0) != 0) return std::nullopt;
    AchievementProfile profile;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string kind, key;
        if (!(fields >> kind)) continue;
        if (kind == "unlock" || kind == "unlock_randomizer") {
            std::int64_t time = 0;
            if (fields >> key >> time && find_achievement(key))
                (kind == "unlock" ? profile.unlocked : profile.unlocked_randomizer)[key] = time;
        } else if (kind == "stat") {
            std::int64_t value = 0;
            if (fields >> key >> value && !key.empty() && key.size() < 64) profile.stats[key] = value;
        } else if (kind == "char") {
            std::uint32_t unique = 0;
            Character c;
            int from_start = 0, assisted = 0, coop = 0, beyond = 0;
            if (fields >> unique >> c.play_frames >> c.defeats >> from_start >> assisted >> coop >> beyond) {
                c.from_start = from_start != 0;
                c.assisted = assisted != 0;
                c.coop_very_hard = coop != 0;
                c.beyond_rookie = beyond != 0;
                profile.characters[unique] = c;
            }
        }
    }
    return profile;
}

std::optional<AchievementProfile> AchievementProfile::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str());
}

bool AchievementProfile::save(const std::filesystem::path& file) const {
    // The atomic save write (flushed, read back; the previous file becomes the
    // .bak only when it is a complete profile).
    const auto text = serialize();
    const auto complete = [](std::span<const std::uint8_t> bytes) {
        const std::string_view content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return content.starts_with("DW4PROFILE ") && content.ends_with("\nend\n");
    };
    return dw4::save::write_file_atomic(file, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), true, nullptr, complete);
}

bool AchievementTracker::unlocked(std::string_view id, bool randomizer) const {
    const auto& map = randomizer ? profile_.unlocked_randomizer : profile_.unlocked;
    return map.count(std::string(id)) != 0;
}

void AchievementTracker::unlock(const char* id, bool randomizer, std::int64_t now, std::vector<AchievementUnlock>& out) {
    auto& map = randomizer ? profile_.unlocked_randomizer : profile_.unlocked;
    if (!map.emplace(id, now).second) return;
    dirty_ = true;
    out.push_back({find_achievement(id), randomizer});
}

void AchievementTracker::add_stat(const char* key, std::int64_t amount) {
    profile_.stats[key] += amount;
    dirty_ = true;
}

void AchievementTracker::max_stat(const char* key, std::int64_t value) {
    auto& stat = profile_.stats[key];
    if (value > stat) {
        stat = value;
        dirty_ = true;
    }
}

std::vector<AchievementUnlock> AchievementTracker::observe(const AchievementStep& step, std::span<const engine::PortEvent> events,
                                                           AchievementGate gate, bool randomizer, std::int64_t now) {
    std::vector<AchievementUnlock> out;
    // A new stage: its damage record starts over.
    if (step.stage_serial != stage_serial_) {
        stage_serial_ = step.stage_serial;
        stage_damage_ = false;
        have_last_ = false;
    }
    // The story character: player 1 offline, this machine's player online.
    const auto& p1 = step.players[std::size_t(std::clamp(step.own_slot, 0, 3))];
    bool local_alive_hp_loss = false;
    for (int slot = 0; slot < 4; ++slot) {
        const auto& p = step.players[std::size_t(slot)];
        if (have_last_ && p.joined && p.local && p.hp < last_hp_[std::size_t(slot)]) local_alive_hp_loss = true;
    }
    if (local_alive_hp_loss) stage_damage_ = true;
    // A partner defeated in this stage got back up.
    bool partner_raised = false;
    int joined = 0;
    for (int slot = 0; slot < 4; ++slot) {
        const auto& p = step.players[std::size_t(slot)];
        joined += p.joined ? 1 : 0;
        if (have_last_ && p.joined && last_hp_[std::size_t(slot)] == 0 && p.hp > 0) partner_raised = true;
    }
    for (int slot = 0; slot < 4; ++slot) last_hp_[std::size_t(slot)] = step.players[std::size_t(slot)].hp;
    have_last_ = true;

    // The characters of this machine: a closed gate marks them assisted.
    if (gate == AchievementGate::disabled || gate == AchievementGate::test_run || gate == AchievementGate::replay ||
        gate == AchievementGate::bot)
        return out;
    for (const auto& p : step.players) {
        if (!p.joined || !p.local || !step.live) continue;
        auto [it, fresh] = profile_.characters.try_emplace(p.unique);
        auto& c = it->second;
        if (fresh) {
            c.from_start = p.level <= 1 && p.exp == 0;
            dirty_ = true;
        }
        if (gate != AchievementGate::open && !c.assisted) {
            c.assisted = true;
            dirty_ = true;
        }
        ++c.play_frames;
        dirty_ = true;
        if (p.species >= 4 && !c.beyond_rookie) {
            c.beyond_rookie = true;
            dirty_ = true;
        }
        if (step.difficulty == 2 && (joined > 1 || step.online_players > 1) && !c.coop_very_hard) {
            c.coop_very_hard = true;
            dirty_ = true;
        }
    }
    if (gate != AchievementGate::open) return out;
    auto give = [&](const char* id) { unlock(id, randomizer, now, out); };

    // In-game time: the profile's minutes.
    if (step.live) {
        add_stat("play_fields", 1);
        const std::int64_t minutes = profile_.stat("play_fields") * 60 / kFieldsPerHour;
        max_stat("play_minutes", minutes);
    }

    bool local_player = false;
    for (const auto& p : step.players) local_player |= p.joined && p.local;
    auto local_slot = [&](int slot) { return slot >= 0 && slot < 4 && step.players[std::size_t(slot)].local; };
    const AchievementProfile::Character* story_character = nullptr;
    if (p1.joined && p1.local)
        if (const auto found = profile_.characters.find(p1.unique); found != profile_.characters.end()) story_character = &found->second;
    // The story finished on a difficulty (Daemon's fall sets 406 + it; a
    // flag set before this profile existed does not rise again, the boss
    // event still comes).
    auto story_clear = [&](int difficulty) {
        give(difficulty == 0 ? "clear_normal" : difficulty == 1 ? "clear_hard" : "clear_veryhard");
        // (Online the story is the room owner's: a guest gets the clear, never its challenges.)
        if (!step.story_owner || !story_character || !story_character->from_start || story_character->assisted) return;
        if (story_character->defeats == 0) give("deathless_clear");
        if (!story_character->beyond_rookie) give("rookie_clear");
        if (story_character->play_frames < 8 * kFieldsPerHour) give("speed_normal");
        if (story_character->play_frames < 4 * kFieldsPerHour) give("speed_fast");
        if (difficulty == 2 && !story_character->coop_very_hard) give("solo_veryhard");
    };

    bool numemon_rose = false; // a Numemon freed in this step (flags 89..98)
    for (const auto& e : events) {
        switch (e.kind) {
        case Kind::boss_defeated: {
            if (!local_player) break;
            add_stat("bosses", 1);
            give("boss_first");
            if (e.text == "apocaly") {
                give("story_apocalymon");
                if (p1.joined && p1.local && p1.level <= 12) give("lowlevel_apocalymon");
            } else if (e.text == "belvam") {
                give("story_belphemon");
            } else if (e.text == "luce") {
                give("story_lucemon");
            } else if (e.text == "devl3") {
                give("story_daemon");
                if (step.difficulty == 2 && !stage_damage_) give("nodmg_daemon_vh");
                story_clear(std::clamp(e.value, 0, 2));
            }
            for (std::size_t i = 0; i < std::size(kDungeonBosses); ++i) {
                if (e.text != kDungeonBosses[i].key) continue;
                if (kDungeonBosses[i].id) give(kDungeonBosses[i].id);
                if (e.text == "ldknight" && starts_with(step.stage, "dng412")) give("story_lordknightmon");
                const std::int64_t bit = std::int64_t(1) << i;
                const std::int64_t mask = profile_.stat("dungeon_boss_mask") | bit;
                profile_.stats["dungeon_boss_mask"] = mask;
                std::int64_t count = 0;
                for (std::size_t b = 0; b < std::size(kDungeonBosses); ++b) count += (mask >> b) & 1;
                max_stat("dungeon_bosses", count);
                if (count >= 9) give("boss_dungeon_all");
                dirty_ = true;
            }
            if (!stage_damage_) give("nodmg_boss");
            if (joined >= 2) give("mp_couch");
            if (joined >= 4) give("mp_fullhouse");
            if (step.online && step.online_players >= 2) give("online_raid");
            break;
        }
        case Kind::story_flag:
            if (local_player && e.extra != 0 && e.value >= 406 && e.value <= 408) story_clear(e.value - 406);
            if (e.extra != 0 && e.value >= 89 && e.value <= 98) numemon_rose = true;
            break;
        case Kind::enemy_defeated:
            if (!local_slot(e.slot)) break;
            add_stat("kills", 1);
            if (starts_with(e.text, "e_nume")) add_stat("numemon_kills", 1);
            break;
        case Kind::critical_hit:
            if (local_slot(e.slot)) add_stat("criticals", 1);
            break;
        case Kind::player_defeated:
            if (!local_slot(e.slot)) break;
            give("first_defeat");
            add_stat("defeats", 1);
            ++profile_.characters[step.players[std::size_t(e.slot)].unique].defeats;
            break;
        case Kind::digivolution:
        case Kind::form_reached:
            if (local_slot(e.slot) && e.value >= 4) give("form_evolve");
            if (local_slot(e.slot) && e.value == 15) give("form_susanoomon");
            break;
        case Kind::technique:
            if (!local_slot(e.slot)) break;
            give("tech_first");
            if (e.extra >= 4) give("tech_rank5");
            break;
        case Kind::card:
            // (Cards are kept in player 1's record; online the shops are solo hub stages.)
            if (!local_player || step.own_slot != 0) break;
            give("card_first");
            if (e.extra >= 2) give("card_rare");
            break;
        case Kind::quest_done: {
            if (!local_player || e.value < 1 || e.value > 8) break;
            give("quest_first");
            const std::int64_t mask = profile_.stat("quest_mask") | (std::int64_t(1) << (e.value - 1));
            profile_.stats["quest_mask"] = mask;
            std::int64_t count = 0;
            for (int q = 0; q < 8; ++q) count += (mask >> q) & 1;
            max_stat("quests", count);
            if (count >= 8) give("quest_all");
            if (step.stage.size() >= 8 && (step.stage.back() == 'v' || step.stage.back() == 'V') && starts_with(step.stage, "dngq"))
                give("quest_veryhard");
            dirty_ = true;
            break;
        }
        case Kind::level_up:
            break; // levels are read from the state below
        case Kind::new_character:
            break; // (the randomizer's bond, game_main)
        }
    }

    // State rules for this machine's players.
    for (const auto& p : step.players) {
        if (!p.joined || !p.local) continue;
        max_stat("best_level", p.level);
        int forms = 0, forms_99 = 0;
        for (int f = 0; f < 16; ++f) {
            forms += p.forms[std::size_t(f)] ? 1 : 0;
            forms_99 += p.forms[std::size_t(f)] && p.form_levels[std::size_t(f)] >= 99 ? 1 : 0;
        }
        max_stat("best_forms", forms);
        max_stat("best_forms_99", forms_99);
        if (p.forms[0] && p.forms[1] && p.forms[2] && p.forms[3]) give("form_rookies");
        if (p.techniques >= 6) give("tech_full");
        const std::int64_t bits = std::int64_t(p.bits) + p.bank_bits;
        if (bits >= 100000) give("bits_100k");
        if (bits >= 1000000) give("bits_1m");
        if (p.bits >= 9999999) give("bits_max");
        if (p.devices >= 36) give("hoarder");
        if (p.best_rank >= 5) give("item_rank5");
        if (p.best_value >= 1000) give("item_pink");
    }
    if (local_player) {
        max_stat("best_cards", step.cards);
        // (Online the flags come with the room story: they count when one rises here.)
        if (!step.shared_story || numemon_rose) max_stat("numemon_freed", step.numemon_freed);
    }
    if (partner_raised && joined >= 2 && local_player) give("raise_ally");
    if (step.online && local_player) give("online_join");
    if (step.online && step.hosting && step.online_players >= 3) give("online_host");
    // Counter goals.
    for (const auto& def : kAchievements)
        if (def.stat && def.goal > 0 && profile_.stat(def.stat) >= def.goal) give(def.id);
    return out;
}

} // namespace dw4::game
