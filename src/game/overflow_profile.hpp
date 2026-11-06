#pragma once

// Data Overflow's meta file (docs/SURVIVAL.md 8, round O1b lane F):
// saves/overflow.dat beside the program, never the story save.
//
// What it keeps: whether the NPC's intro was seen, the Sim Bits (8.1), the
// Plug Lab's steps per form (8.2), the starting loadout per form (8.3), the
// Data Records (8.4) with their dates, per-form bests and the totals the
// records are checked against. What a record opens is derived from the
// records, never stored twice.
//
// The file is text (8.5), one fact per line, "end" as its last line. Unknown
// lines (a later version's) are kept and written back as they were, so an
// older program never drops a newer one's data. A file that is not complete
// (no header, no "end": a damaged or cut file) is never trusted over the
// backup: load() takes overflow.dat, else overflow.dat.bak, else what the
// damaged file still holds, else a new profile; a damaged file is kept as
// overflow.dat.damaged before the next write replaces it. Writes go through
// dw4::save::write_file_atomic (temporary file, flushed, read back, renamed;
// the previous file becomes the .bak only when it is complete).
//
// Nothing here needs a window or an engine: the lobby screens (lane D) read
// the profile and call the store's writes, the run driver turns a run into
// RunConfig values (apply_meta) and records its end (record_run).

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game::overflow {

struct RunRequest; // overflow_run.hpp

// The file's version (the header "DW4OVERFLOW <n>"). A file of a later
// version is read as far as this one understands it; its unknown lines stay.
inline constexpr int kProfileVersion = 1;

// ---------------------------------------------------------------------------
// The Plug Lab (8.2)
// ---------------------------------------------------------------------------
// Seven stats per form, 10 steps of +5 UPCNT each; step n costs 100 x n Sim
// Bits; no refund.
inline constexpr int kPlugLabSteps = 10;
inline constexpr int kPlugLabPoints = 5;
inline constexpr int kPlugLabPrice = 100;
// The Plug Lab's stats as UPCNT indices (GameSystem's player.upcnt: 0 MHP,
// 1 MMP, 2 STR, 3 DEF, 4 WIS, 5 SPI, 6 SPD) in the screen's order: Strength,
// Def., MaxHP, MaxMP, Wisdom, Spirit, Speed.
inline constexpr std::array<int, 7> kPlugLabStats = {2, 3, 0, 1, 4, 5, 6};
// The file's key of an UPCNT index ("str", "def", "mhp", "mmp", "wis", "spi",
// "spd"); nullptr for another index.
const char* plug_lab_key(int upcnt);
const char* plug_lab_name(int upcnt); // "Strength", "Def.", ...
// The price of the next step after `steps` (0 when the stat is full).
int plug_lab_price(int steps);

// ---------------------------------------------------------------------------
// The profile
// ---------------------------------------------------------------------------
struct FormStats {
    int runs = 0;
    std::array<int, 3> clears{}; // Normal, Hard, Very Hard
    double best_seconds = 0;     // the longest run (Endless has its own)
    int best_level = 0;
    int best_defeats = 0;
    double best_endless = 0;
    // A later version's keys on this form's line (kept for the write).
    std::vector<std::pair<std::string, std::string>> extra;
};

struct Loadout {
    std::string armor; // a gear key of a Core or a Board (lane E's gear_table), "" none
    std::string chip;  // a gear key of a chip, "" none
    bool operator==(const Loadout&) const = default;
};

struct OverflowProfile {
    int version = kProfileVersion; // the version the file was written with
    bool seen_intro = false;
    std::int64_t sim_bits = 0;     // the balance (the Plug Lab spends it)
    std::int64_t earned_total = 0; // every Sim Bit ever earned (the Banker record)
    // Plug Lab steps per form and UPCNT index (0-6), 0-10 each.
    std::map<std::string, std::array<int, 7>, std::less<>> plugs;
    std::map<std::string, Loadout, std::less<>> loadouts;
    std::map<std::string, FormStats, std::less<>> forms;
    // Data Records done: key -> the date it was met (yyyymmdd; 1 when unknown).
    std::map<std::string, std::int64_t, std::less<>> records;
    // Totals over every run: defeats, runs, casts, crates, tech_fire,
    // tech_ice, tech_bolt, tech_dark, guardians, clears, seconds.
    std::map<std::string, std::int64_t, std::less<>> totals;
    // The last form and tier picked (the character select starts there).
    std::string last_form = "p_agumon";
    std::string last_tier = "normal";
    // Lines this version does not know, in their order (kept for the write).
    std::vector<std::string> unknown;

    const FormStats* form(std::string_view model) const;
    Loadout loadout(std::string_view model) const;
    int plug_steps(std::string_view model, int upcnt) const; // 0 for an unknown form or stat
    std::int64_t total(std::string_view key) const;
    bool record(std::string_view key) const { return records.count(key) != 0; }

    std::string serialize() const;
    // Empty when the text is not a profile at all (no header). `complete`
    // says whether it ended with "end" (a cut file parses as far as it goes).
    static std::optional<OverflowProfile> parse(std::string_view text, bool* complete = nullptr);
    bool operator==(const OverflowProfile&) const;
};

// A complete profile file: the header and "end" as the last line.
bool complete_profile(std::span<const std::uint8_t> bytes);

// "21:12.4" from seconds and back (the file's best times).
std::string time_text(double seconds);
std::optional<double> parse_time(std::string_view text);

// ---------------------------------------------------------------------------
// Data Records (8.4) and what they open
// ---------------------------------------------------------------------------
struct RecordDef {
    const char* key;       // the file's key ("gate_keeper")
    const char* title;     // "Gate Keeper"
    const char* condition; // "Defeat any Guardian."
    const char* opens;     // "Cipher Core" (the screen's text)
};
std::span<const RecordDef> record_table();
const RecordDef* find_record(std::string_view key);
// The gear keys a record opens (lane E's gear_table keys; empty for records
// that open tiers or offers).
std::vector<std::string> record_gear(std::string_view key);
// The starting set of gear keys every profile has (7.1-7.3 "start"): the
// form's own PWR Medal is "pwr_medal".
std::span<const char* const> start_gear();
// The start set plus everything the records opened (start set first, no key
// twice), and only the record-opened ones (RunConfig::gear_open).
std::vector<std::string> opened_gear(const OverflowProfile& profile);
std::vector<std::string> gear_open(const OverflowProfile& profile);
// Whether a gear key is a Core / a chip that goes only into the core pocket
// (8.3: such a chip needs a Core as the armour). Keys of lane E's start set
// and of record_gear.
bool gear_is_core(std::string_view key);
bool gear_is_board(std::string_view key);
bool gear_is_chip(std::string_view key);
bool gear_core_pocket_only(std::string_view key);
// Whether a loadout may start a run with this profile (opened keys, the
// right slots, a core-pocket chip only with a Core).
bool loadout_allowed(const OverflowProfile& profile, const Loadout& loadout);

// The four Rookies (start forms) and the 16 forms in the face sheet's order.
std::span<const char* const> overflow_forms();
bool rookie_form(std::string_view model);
// A start form is open (5.3): the Rookies always, a form the story save has
// reached (17.1 answer 7: bit n of `story_forms` = the shinka row n; read
// only), or its unlock condition met in the mode.
bool form_unlocked(const OverflowProfile& profile, std::string_view model, std::uint32_t story_forms = 0);
// The unlock condition's text for a locked face ("Survive 10:00 with Agumon.").
std::string form_unlock_text(std::string_view model);

// ---------------------------------------------------------------------------
// A run's end (4.9)
// ---------------------------------------------------------------------------
struct RunReport {
    std::string form = "p_agumon";
    int tier = 0;            // 0 Normal, 1 Hard, 2 Very Hard
    bool endless = false;
    double seconds = 0;      // the run clock
    int level = 1;
    int defeats = 0;
    std::string outcome = "retired"; // cleared, defeated, retired, time_up, window_closed
    std::int64_t bits = 0;   // the run player's BIT at the end (the BIT Board raised it already)
    std::vector<std::string> guardians; // Guardian families defeated ("e_blosso", ...)
    bool awakened = false;   // a signature weapon awakened
    int casts = 0;           // techniques cast
    std::array<int, 4> tech_defeats{}; // technique defeats with Fire, Ice, Bolt, Dark
    int crates = 0;
    bool cleared() const { return outcome == "cleared"; }
};

struct RunReward {
    std::int64_t sim_bits = 0;
    std::vector<std::string> new_records; // keys, in record_table order
};

// The run's bits of a defeat (4.7: 5 % of the defeats leave bits worth the
// original's band for player 1's level, 10-55 up to level 18 ... 566-1160,
// x (100 + BITRITSU) / 100). Until the run drops g_000bit itself (O2) the
// driver credits them at the defeat, with its own generator (`random`, a
// splitmix64 state seeded from the run's seed), so the run's engine and the
// director's draws stay as they are.
inline constexpr int kDefeatBitsPercent = 5;
int defeat_bits(int level, int bit_rate, std::uint64_t& random);
std::uint64_t bits_seed(std::uint32_t run_seed);

// The tier's Sim Bits factor (4.3: 1, 1.5, 2).
double tier_factor(int tier);
// Sim Bits of a run (4.9): its bits x the tier's factor, +500 x the factor
// for a clear.
std::int64_t run_sim_bits(const RunReport& report);
// Adds a run to a profile: the Sim Bits, the form's line, the totals and the
// records it meets (dated `date`, yyyymmdd). Pure; MetaStore::record_run
// saves after it.
RunReward add_run(OverflowProfile& profile, const RunReport& report, std::int64_t date);
// Records whose conditions the profile's totals and bests already meet
// (a record a later version adds is checked against the file's totals; also
// after add_run). Returns the keys newly set.
std::vector<std::string> check_records(OverflowProfile& profile, std::int64_t date);

// ---------------------------------------------------------------------------
// The Plug Lab's screen rows and purchase
// ---------------------------------------------------------------------------
struct PlugLabRow {
    int upcnt{};
    const char* name{};
    int steps{};      // 0-10
    int points{};     // 5 x steps (UPCNT)
    int next_price{}; // 0 when full
};
std::array<PlugLabRow, 7> plug_lab_rows(const OverflowProfile& profile, std::string_view model);
enum class BuyResult { bought, not_enough, maxed, bad_stat };
const char* buy_result_text(BuyResult result);
BuyResult buy_plug(OverflowProfile& profile, std::string_view model, int upcnt);
// A run's Plug Lab points by UPCNT index (0-10; 7-10, the resistances, stay 0).
std::array<int, 11> plug_lab_points(const OverflowProfile& profile, std::string_view model);

// ---------------------------------------------------------------------------
// The run's request
// ---------------------------------------------------------------------------
// The meta of a run in RunRequest::options, for the keys the request does
// not have yet (a test run's own values win): armor=<key>, chip=<key> (the
// form's loadout), plug_lab=n0,...,n10 (by UPCNT index), gear_open=k1,k2.
void apply_meta(const OverflowProfile& profile, RunRequest& request);
// plug_lab=... back into points (absent or malformed: none).
std::array<int, 11> parse_plug_lab(std::string_view text);

// ---------------------------------------------------------------------------
// The store: the profile, its file and the writes of 8.5
// ---------------------------------------------------------------------------
enum class LoadSource { none, file, backup, salvaged, fresh };

class MetaStore {
public:
    // Reads `path` (empty: a profile in memory only, never written; test runs
    // without DW4_OVERFLOW_PROFILE).
    void open(const std::filesystem::path& path);
    const std::filesystem::path& path() const { return path_; }
    const OverflowProfile& profile() const { return profile_; }
    LoadSource source() const { return source_; }
    // What the load had to do (empty for a good file or none): for the log.
    const std::string& load_note() const { return note_; }
    const std::string& last_error() const { return error_; }

    // The writes of 8.5 (each saves at once; false when nothing changed or
    // the step is not allowed; a failed write keeps the change in memory and
    // says why in last_error()).
    bool mark_intro_seen();
    bool set_loadout(std::string_view model, const Loadout& loadout);
    BuyResult buy_plug(std::string_view model, int upcnt);
    bool set_last(std::string_view model, std::string_view tier);
    // A run's results opened: Sim Bits, records, bests (`date` yyyymmdd; 0:
    // today). Saves.
    RunReward record_run(const RunReport& report, std::int64_t date = 0);
    const std::optional<RunReport>& last_report() const { return last_report_; }
    const RunReward& last_reward() const { return last_reward_; }

    // A whole profile (tests, a reset in the F1 panel): replaces and saves.
    bool replace(OverflowProfile profile);
    bool save();

private:
    std::filesystem::path path_;
    OverflowProfile profile_;
    LoadSource source_ = LoadSource::none;
    std::string note_, error_;
    std::optional<RunReport> last_report_;
    RunReward last_reward_;
};

// The process-wide store (game_main opens it; the lobby screens and the run
// driver use it).
MetaStore& meta();
// The file a program uses: DW4_OVERFLOW_PROFILE when set; none in test runs
// (`test_run`); else <program folder>/saves/overflow.dat.
std::filesystem::path profile_path(const std::filesystem::path& program_folder, bool test_run);

// Today as yyyymmdd (local time).
std::int64_t today();

} // namespace dw4::game::overflow
