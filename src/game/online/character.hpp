#pragma once

// The online character (docs/SEAMLESS.md 9): one character per account,
// stored on the server, played from a virtual memory card.
//
//   GET  /online-character   -> revision, record (base64, 3316 bytes), playSeconds, flagged, summary; 404: none yet
//   PUT  /online-character   {record, playSeconds, context} with If-Match: "<revision>" (update) or
//                            If-None-Match: * (create) -> revision; 409 carries currentRevision
//   POST /online-character/reset {currentPassword} -> 204
//
// The virtual card. Entering a room, the client writes card A as a single
// save file in saves/online/<account>/online_card.dat (save::CardStore,
// never the player's own saves/savedata.dat) with the server's record in box
// 0; card B is absent. No online character (404) leaves the card empty: the
// original new game flow. The card file is a cache; the server is the truth.
// Beside it, online_card.state remembers the revision the cache was built
// from and whether it holds a save the server has not confirmed yet (a crash
// between a save and its upload): the next room entry uploads it when the
// server still has that revision.
//
// The uploads (CharacterSync). A save is a 3316-byte record:
//   - the save room: card_written for the own slot (the box the game wrote);
//   - automatically at every stage change (the barrier tick, from the state
//     the member carries into the next stage) and when the member leaves the
//     room (user decision, question 3 of SEAMLESS.md 18).
// Each save goes into the card cache first (pending), then PUT with
// If-Match. One upload at a time; a newer save replaces a waiting one.
// A 409 (the account saved from another PC meanwhile) never overwrites: the
// uploads stop for this room session, the server's character is loaded into
// the card cache again (the local cache is kept in backups/ first) and the
// player is told (a 409 that names the revision the save was based on is a
// race lost inside the server, a database deadlock, and goes again after 2 s,
// at most three times in a row). A record of another character (another UNIQUE) is not
// uploaded (the server would refuse it: reset the online character first), and
// neither is a character the title's character select made new while the
// account has an online character (new_game: its UNIQUE is the frame count of
// its creation and may even be the online character's); the player is told
// at the Game Start and the status line says so. Whether the game is a new
// character is decided at the Game Start from the title's last choice: Create
// Digimon reports new_character already at the status window, and B takes it
// back (round 59 review: a player who then called the online character from
// the card was never saved). A box the save room filled with another
// character is not the online character either when it is called later. A
// refused login (401 / 403, e.g. after "Log out everywhere" or a password
// change on another PC), an outdated version (426) or a newer connection of
// the account on another PC (the relay replaced this one) stops the uploads,
// but the saves still go into the card cache as pending: the next room entry
// uploads the newest when the server still has the revision it was based on.
// After a 409 the session's later saves go into the backup made then.
#include "http.hpp"

#include "dw4/card_store.hpp"
#include "dw4/save_data.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::game::online {

// RFC 4648 base64 (the record field of the API).
std::string base64_encode(std::span<const std::uint8_t> bytes);
std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text);

using CharacterRecord = std::array<std::uint8_t, save::kRecordSize>;

namespace character_api {

// The play context an upload reports (the server's PlayContext; online
// play has no cheats, no rebalancing and no save states).
struct Context {
    std::string room_code;
    int players = 1;          // members of this machine's stage instance (1-4)
    int room_players = 0;     // members of the room (1-10; 0: not sent), as the achievements report it
    bool randomizer = false;  // the room's randomizer setting
    bool cheats = false;      // the engine's cheat flags (God mode, Infinite MP, One-hit)
    bool rebalanced = false;  // the engine's rebalancing (EXP / damage rates not 100%)
    int difficulty = 0;       // the room's difficulty
    int protocol = 0;
    std::string client_version;
};

struct Character {
    int revision = 0;
    CharacterRecord record{};
    std::int64_t play_seconds = 0;
    bool flagged = false;
};

HttpRequest get_request();
// `revision` 0 creates the character (If-None-Match: *).
HttpRequest put_request(const CharacterRecord& record, int revision, std::int64_t play_seconds, const Context& context);
HttpRequest reset_request(const std::string& current_password);

// GET 200: the character; nothing for another answer or a malformed record.
std::optional<Character> parse_character(const HttpResponse& response);
// PUT 200 / 201: the new revision.
std::optional<int> parse_saved(const HttpResponse& response);
// The problem's "code" ("revision_conflict", "invalid_record", ...), "" when none.
std::string problem_code(const HttpResponse& response);
// 409: the server's current revision.
std::optional<int> current_revision(const HttpResponse& response);

} // namespace character_api

// ---------------------------------------------------------------------------
// The virtual card (the cache of the online character)
// ---------------------------------------------------------------------------
struct CardCacheState {
    int revision = 0;              // the server revision the cache was built from or last uploaded as (0: none)
    std::int64_t play_seconds = 0; // the character's play time at that revision (or the pending save's)
    bool pending = false;          // the cache holds a save the server has not confirmed
    int box = 0;                   // the box of the online character in the cache file
    std::uint32_t unique = 0;      // its UNIQUE (0: unknown)
    bool operator==(const CardCacheState&) const = default;
};

class OnlineCard {
public:
    OnlineCard() = default;
    // `folder`: saves/online/<account folder> (online_card_folder).
    explicit OnlineCard(std::filesystem::path folder) : folder_(std::move(folder)) {}
    const std::filesystem::path& folder() const { return folder_; }
    std::filesystem::path file() const { return folder_ / "online_card.dat"; }
    std::filesystem::path state_file() const { return folder_ / "online_card.state"; }
    std::filesystem::path backups() const { return folder_ / "backups"; }
    // The engine's card store: slot A = file(), slot B = a file that is never
    // written (the room's card B is absent; Engine::set_card_setting(1, none)
    // as well).
    save::CardStore store() const;

    std::optional<std::vector<std::uint8_t>> read() const;
    bool write(std::span<const std::uint8_t> gci, std::string* error = nullptr) const;
    // Drops the cache file (after a backup of it when it holds a save).
    void clear() const;
    // Copies the cache file to backups/online_card-YYYYMMDD-HHMMSS.dat; the
    // path, none when there was nothing to keep.
    std::optional<std::filesystem::path> backup() const;

    CardCacheState state() const; // defaults when the file is missing or damaged
    bool set_state(const CardCacheState& state) const;

private:
    std::filesystem::path folder_;
};

// saves/online/<account> (account folded like the token file's name).
std::filesystem::path online_card_folder(const std::filesystem::path& online_folder, const std::string& account);

// The card file (GCI bytes) with `record` in `box` of both data copies:
// `base` (the cache before, when it holds a valid save) gives the directory
// entry, the 8 KB header and the other boxes; without it the file is new
// (an empty header, the other boxes empty). `keep_others` false empties the
// other boxes even with a base (the card a room entry builds holds only the
// online character).
std::vector<std::uint8_t> card_with_record(const std::vector<std::uint8_t>* base, const CharacterRecord& record, int box, bool keep_others,
                                           std::uint32_t modified);
// The record bytes of `box` in the first data copy that passes its check.
std::optional<CharacterRecord> record_of_box(std::span<const std::uint8_t> gci, int box);
// A record's UNIQUE, ISUSE and the player name (ASCII) without a full decode.
std::uint32_t record_unique(const CharacterRecord& record);
bool record_used(const CharacterRecord& record);
std::string record_name(const CharacterRecord& record);

// ---------------------------------------------------------------------------
// The uploads
// ---------------------------------------------------------------------------
class CharacterSync {
public:
    enum class Why : std::uint8_t { save_room, stage_change, leave, recovered };
    struct Config {
        OnlineCard card;
        character_api::Context context;
        // Queues a request on the room's HTTP worker; the id comes back to
        // on_response.
        std::function<std::uint64_t(HttpRequest)> submit;
        bool trace = false; // "online-character: ..." lines on stdout (test runs)
        // In someone else's room (round G-core, 8.8): the engine's records hold
        // the room owner's story, so every cached or uploaded record gets the
        // own story. The room owner's engine story is his own (the room story):
        // his records keep it, and a save that only moved it on goes up.
        bool guest = false;
    };
    explicit CharacterSync(Config config);

    // Room entry with the answer of GET /online-character: builds the card
    // cache (or recovers an unsent save of a crash). False (with `error`)
    // when the server's answer is neither the character nor 404: the room is
    // not entered then (the card would be a guess).
    bool enter(const HttpResponse& get_response, std::string& error);
    bool entered() const { return entered_; }
    const OnlineCard& card() const { return config_.card; }

    // The save room wrote the card (card_written for the own slot, card A):
    // the record of `box` in the cache file goes up.
    void card_written(int box);
    // An automatic save (stage change, leaving the room): the record goes
    // into the cache's character box, then up.
    void auto_save(const CharacterRecord& record, Why why);
    // The title's character select made a new character (port event
    // new_character of the own slot, already at the status window), called
    // one from a card box (character_called), and the title started a game
    // (TITLE2 GAMESTART). The Game Start decides from the last of the two: a
    // new character while the account has an online character, or a box the
    // save room filled with another character, is another character whose
    // saves are not uploaded until a game starts with the card's online
    // character again (round 56 and 59 reviews).
    void new_game(std::uint32_t unique);
    void character_called(int box, std::uint32_t unique);
    void game_started();
    // A newer connection of this account (another PC) replaced this one in
    // the room: the uploads stop, the saves stay pending in the card cache
    // (the other PC goes on from the server's revision; this PC's last
    // progress goes up at its next room entry if nobody saved since).
    void replaced();
    bool foreign() const { return foreign_; }
    // Play time without loads (live steps in a started game).
    void add_play_time(double seconds) { session_play_ += seconds; }
    // The room's context changes (members, ...).
    void set_context(const character_api::Context& context) { config_.context = context; }
    // Round G-core (docs/SEAMLESS.md 8.8): the account's own story bytes
    // (record 792..1891) from the record GET /online-character returned at the
    // room entry, from the record of a 409 reload, zeros without a character on
    // the server (404). The room entry puts them into every used box of the
    // card cache; a guest's records carry them in the cache and in every
    // upload (Config::guest; the server composes the own story anyway; the
    // cache and its backups so never hold a borrowed story), and a guest's
    // online card store writes them (save::CardStore::set_story_override).
    const save::StoryBytes& own_story() const { return own_story_; }
    void put_own_story(CharacterRecord& record) const;

    // An answer of the HTTP worker; false when the id is not this sync's.
    bool on_response(std::uint64_t id, const HttpResponse& response);
    // Retries after a network problem or a rate limit.
    void tick(double now);

    // Nothing waiting and nothing in flight.
    bool idle() const { return in_flight_ == 0 && !waiting_ && !reload_; }
    // The uploads stopped for this room session (409 or a refused account).
    bool blocked() const { return blocked_ || auth_blocked_; }
    int revision() const { return revision_; }
    int box() const { return box_; } // the online character's box in the card cache (0-3)
    std::uint32_t unique() const { return unique_; }
    std::int64_t play_seconds() const;
    std::uint64_t uploads() const { return uploads_; }
    // One line for the Online tab ("Saved (revision 7)", "Saving...", ...).
    std::string status_text() const;
    // Messages for the player (toasts), oldest first.
    std::vector<std::string> take_notices();

    static const char* why_name(Why why);

private:
    struct Save {
        CharacterRecord record{};
        Why why = Why::save_room;
    };
    void request(const Save& save, bool write_cache);
    void send(const Save& save);
    void notice(std::string text);
    void trace(const std::string& line) const;
    void reload();
    void after_reload(const HttpResponse& response);
    void skipped(Why why); // a save of another character than the online one

    Config config_;
    save::StoryBytes own_story_{};    // the account's own story bytes (round G-core)
    bool entered_ = false;
    int revision_ = 0;                // the server's revision of the online character (0: none yet)
    std::uint32_t unique_ = 0;        // the online character's UNIQUE (0: none yet)
    double base_play_ = 0;            // play seconds at revision_
    double session_play_ = 0;         // play seconds of this session since entering
    double sent_play_ = 0;            // session_play_ when the upload in flight left
    int box_ = 0;                     // the character's box in the card cache
    std::uint64_t in_flight_ = 0;     // the PUT in flight (0: none)
    Save flying_{};
    std::optional<Save> waiting_;     // the next save while one is in flight (the newest wins)
    std::optional<CharacterRecord> confirmed_; // the last record the server confirmed (identical saves are skipped)
    std::uint64_t reload_ = 0;        // the GET after a conflict
    bool blocked_ = false;            // 409: the server's character changed; nothing goes up or into the cache
    bool auth_blocked_ = false;       // 401 / 403 / 426: nothing goes up, the saves stay pending in the cache
    bool foreign_ = false;            // the game is a character made new while the online character exists
    enum class TitleChoice : std::uint8_t { none, made_new, called };
    TitleChoice title_choice_ = TitleChoice::none; // the character select's last choice since the last game_started
    int called_box_ = -1;
    std::set<int> foreign_boxes_;     // card boxes the save room filled with another character this session
    std::optional<std::filesystem::path> blocked_backup_; // the backup a 409 made: later saves of the session go there
    bool skip_told_ = false;          // the player was told that this game is not saved online
    double retry_at_ = -1;            // a retry is due then (-1: none)
    int races_ = 0;                   // 409s at the revision the save was based on (lost server races) in a row
    double now_ = 0;
    std::uint64_t uploads_ = 0;
    std::string last_problem_;
    std::vector<std::string> notices_;
};

} // namespace dw4::game::online
