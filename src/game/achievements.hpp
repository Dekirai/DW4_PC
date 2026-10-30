#pragma once

// Local achievements: a fixed list (stable ids, English titles) unlocked
// from the engine's observe-only events (engine/port_events.hpp) and the
// players' state after each live step, kept in saves/profile.dat next to the
// program. Offline unlocks stay on this machine (the F1 menu's Achievements
// tab); in an online room a second tracker counts this machine's player for
// the online account (online_achievements.hpp) with the same rules. Nothing
// unlocks or counts while a cheat or a rebalancing value is
// in use, after a save state was loaded in the running stage, during a save
// state's replay, in test runs and while the campaign bot plays; a run with
// the randomizer (item drops, enemy or loading zone shuffle) unlocks into a
// category of its own.
#include "dw4/engine/port_events.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game {

enum class AchievementCategory { story, bosses, challenge, growth, forms, techniques, collection, quests, combat, time, multiplayer };
const char* achievement_category_name(AchievementCategory category);

struct AchievementDef {
    const char* id;          // stable: profiles and the server key on it
    const char* title;
    const char* description;
    AchievementCategory category;
    const char* stat = nullptr; // the profile counter its progress shows (with goal)
    std::int64_t goal = 0;
};
std::span<const AchievementDef> achievement_list();
const AchievementDef* find_achievement(std::string_view id);

// The online account's catalogue (docs/SEAMLESS.md 11). The server adopts
// this client's ids, titles, descriptions and thresholds; the checked-in
// data/online/achievements.json (achievement_catalogue_json) is compared by
// this client's test and by the server's tests, so a change on one side
// fails both until the other follows.
//
// Who unlocks an achievement for the online account: the game reports an
// event when it happens (POST /achievements/unlock), the server unlocks a
// stat from the counters the game batches every minute (POST /stats), or a
// state from its own decode of the uploaded online character.
enum class AchievementOnlineKind { event, stat, state };
const char* achievement_online_kind_name(AchievementOnlineKind kind);
// The server's key of a category (the achievements.category column).
const char* achievement_category_key(AchievementCategory category);

// One per AchievementDef, in the same order.
struct AchievementOnline {
    const char* id;
    AchievementOnlineKind kind;
    std::int64_t goal = 0;             // the server's goal or limit (0: none): a count, a level, seconds of in-game time
    const char* server_stat = nullptr; // the server's counter of a stat achievement
    bool owner_only = false;           // a story challenge: only the room owner's machine reports it
};
std::span<const AchievementOnline> achievement_online_list();
const AchievementOnline* find_achievement_online(std::string_view id);
// The catalogue as data/online/achievements.json holds it (LF line ends).
std::string achievement_catalogue_json();

// The profile counters an online client batches (POST /stats) and the
// server's key of each. play_fields goes out as whole seconds of in-game
// time (59.94 fields per second).
struct AchievementCounter {
    const char* profile_stat;
    const char* server_stat;
};
std::span<const AchievementCounter> achievement_online_counters();

// One player as the host saw it after a step.
struct AchievementPlayer {
    bool joined{};
    bool local{};               // plays on this machine
    std::uint32_t unique{};     // UNIQUE, the character's id
    int level = 1, exp{}, species{}, hp{}, max_hp{};
    int bits{}, bank_bits{};
    int devices{};              // DEVICEFOLDER entries
    int best_rank{}, best_value{}; // the best device rank / value it holds
    std::array<bool, 16> forms{};  // BASE_ISUSE
    std::array<int, 16> form_levels{};
    int techniques{};           // WAZALIST entries of the current form
};

// The state after a step.
struct AchievementStep {
    std::string stage;
    bool live{};            // gameplay: no load, a picture, not a menu stage
    int difficulty{};
    bool online{};          // a lockstep / room session
    bool hosting{};
    int online_players{};   // machines in the session
    std::array<AchievementPlayer, 4> players{};
    int cards{};            // owned cards (the story character's CARDLIST)
    int numemon_freed{};    // story flags 89..98 set
    std::uint64_t stage_serial{}; // changes with every stage load
    // The story character: player 1 offline, this machine's player in an
    // online room (docs/SEAMLESS.md 11: unlocks by the local slot, never p1).
    int own_slot = 0;
    // An online room: the story is the room owner's. Its challenges (speed,
    // deathless, rookie, solo) count only on the owner's machine, and a
    // story collection counts only when its flag rises in a live step here
    // ("was there"), never from the room story an engine received.
    bool shared_story = false;
    bool story_owner = true;
};

// Why nothing counts right now (open: everything counts).
enum class AchievementGate { open, disabled, cheats, rebalancing, save_state, replay, test_run, bot };
const char* achievement_gate_text(AchievementGate gate);

struct AchievementProfile {
    struct Character {
        std::int64_t play_frames{};  // in-game fields without stage loads
        std::int64_t defeats{};
        bool from_start{};           // tracked since a new game (level 1, no EXP)
        bool assisted{};             // a gate closed while it played (cheats, states)
        bool coop_very_hard{};       // played Very Hard with others
        bool beyond_rookie{};        // used a form past the rookies
    };
    std::map<std::string, std::int64_t> unlocked;            // id -> unix time
    std::map<std::string, std::int64_t> unlocked_randomizer; // the same in randomizer runs
    std::map<std::string, std::int64_t> stats;
    std::map<std::uint32_t, Character> characters;

    std::int64_t stat(std::string_view key) const;
    // "DW4PROFILE 1" then one line per value; unknown lines are skipped.
    std::string serialize() const;
    static std::optional<AchievementProfile> parse(std::string_view text);
    // The file: written to a temporary file and moved over, the previous
    // one kept as <file>.bak.
    static std::optional<AchievementProfile> load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
};

struct AchievementUnlock {
    const AchievementDef* def{};
    bool randomizer{};
};

class AchievementTracker {
public:
    explicit AchievementTracker(AchievementProfile profile = {}) : profile_(std::move(profile)) {}
    // One step: its engine events and the state after it. `randomizer`: the
    // randomizer is on (its own category). Returns what unlocked.
    std::vector<AchievementUnlock> observe(const AchievementStep& step, std::span<const engine::PortEvent> events,
                                           AchievementGate gate, bool randomizer, std::int64_t now_unix);
    const AchievementProfile& profile() const { return profile_; }
    AchievementProfile& profile() { return profile_; }
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }
    bool unlocked(std::string_view id, bool randomizer = false) const;

private:
    void unlock(const char* id, bool randomizer, std::int64_t now, std::vector<AchievementUnlock>& out);
    void add_stat(const char* key, std::int64_t amount);
    void max_stat(const char* key, std::int64_t value);

    AchievementProfile profile_;
    bool dirty_{};
    // The running stage: its serial and whether a local player lost HP in it.
    std::uint64_t stage_serial_{};
    bool stage_damage_{};
    std::array<int, 4> last_hp_{};
    bool have_last_{};
};

} // namespace dw4::game
