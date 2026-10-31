#pragma once

// The DW4Online REST calls of the account and room UI (docs/SEAMLESS.md 10):
// registration, recovery, logout (here and everywhere), password change, the
// account, the online character's summary, the room list, a room's state,
// kick and close. Request builders and answer parsers only (no transport), so
// they are tested without a server; api.hpp has the S1 calls (status, login,
// create, join, ticket, leave) the lobby uses too.
//
//   POST   /auth/register        {username, password, email?, deviceName} -> 201 {account, recoveryCode, token, expiresAt}
//   POST   /auth/recover         {username, recoveryCode, newPassword}    -> {recoveryCode} (a new one)
//   POST   /auth/logout                                                   -> 204
//   POST   /auth/logout-all                                               -> {revoked}
//   POST   /auth/password        {currentPassword, newPassword}           -> 204 (every other session ends)
//   GET    /me                                                            -> the account
//   GET    /online-character                                              -> {revision, summary, playSeconds, ...} or 404
//   GET    /rooms                                                         -> [RoomSummary] (every open room, public)
//   GET    /rooms/{code}                                                  -> {room, settings, members, instances, you, ...}
//   POST   /rooms/{code}/kick    {username}                               -> 204 (the owner)
//   DELETE /rooms/{code}                                                  -> 204 (the owner closes it for everyone)
#include "http.hpp"
#include "room_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game::online::api {

// The server's problem details (RFC 9457): the stable code ("wrong_room_password",
// "client_outdated", ...) and the English title for people.
struct Problem {
    int status = 0;     // 0: no answer (transport error)
    std::string code;   // empty when the body had none
    std::string text;   // for the player
};
Problem problem_of(const HttpResponse& response);

struct Registered {
    std::string token;
    std::string account;       // the server's spelling of the name
    std::string recovery_code; // shown once
};
struct AccountInfo {
    std::uint64_t id = 0;
    std::string name, email;
    bool must_change_password = false;
    std::string created_at;    // ISO 8601 as the server sends it
};
// The online character's summary (what the account screen shows; the
// character lane builds the virtual card from the same answer's record).
struct CharacterInfo {
    bool exists = false;       // false: 404, a new game starts from zero
    std::string name, digimon;
    int level = 0, clears = 0; // clears: bit 0 Normal, 1 Hard, 2 Very Hard (flags 406-408)
    std::int64_t play_seconds = 0;
    std::uint32_t revision = 0;
};
// The highest difficulty a room of this character's owner may use: Hard
// after a Normal clear, Very Hard after a Hard clear (as the title's select).
int unlocked_difficulty(const CharacterInfo& character);

// One open room of GET /rooms.
struct RoomListing {
    std::string code, name, owner;
    bool owner_present = false, story_frozen = false, has_password = false, ranked = false, randomizer = false;
    int players = 0, max_players = 0;
    int protocol = 0;
    std::uint64_t data_hash = 0;
    std::string client_version;
    std::optional<int> difficulty;
    int instances = 0;
    // Rooms:StoryWhileOwnerAway of the server: "drop", "hold" or "apply"
    // (what happens to story progress while the owner is away; SEAMLESS.md 8.6).
    std::string story_policy = "drop";
    // Same protocol and game data as this machine: only these can be joined.
    bool compatible(int own_protocol, std::uint64_t own_data_hash) const {
        return protocol == own_protocol && data_hash == own_data_hash;
    }
};

struct RoomMemberInfo {
    std::uint64_t account_id = 0;
    std::string name;
    int slot = 0;                 // the room slot 0-9 (the relay's member index)
    bool owner = false;
    std::optional<std::uint32_t> instance; // none while not connected to the relay
};
struct InstanceMemberInfo {
    std::uint64_t account_id = 0;
    std::string name;
    int slot = 0;                 // the engine slot 0-3 in that instance
    bool connected = false, lagging = false;
};
struct InstanceInfo {
    std::uint32_t id = 0, visit = 0, ticks = 0;
    std::string stage;
    std::string mode;             // hub, solo, sealed
    std::vector<InstanceMemberInfo> members;
    bool departing = false, boss_lock = false, joinable = false;
};
// GET /rooms/{code} for a member (and the create / join answers).
struct RoomView {
    RoomListing room;
    RoomSettings settings;
    std::vector<RoomMemberInfo> members;
    std::vector<InstanceInfo> instances;
    RoomMemberInfo you;
};

HttpRequest register_request(const std::string& user, const std::string& password, const std::string& email);
HttpRequest recover_request(const std::string& user, const std::string& code, const std::string& new_password);
HttpRequest logout_request();
HttpRequest logout_all_request();
HttpRequest change_password_request(const std::string& current, const std::string& next);
HttpRequest me_request();
HttpRequest character_request();
HttpRequest rooms_request();
HttpRequest room_state_request(const std::string& code);
HttpRequest kick_request(const std::string& code, const std::string& user);
HttpRequest close_room_request(const std::string& code);

std::optional<Registered> parse_registered(const HttpResponse& response);
std::optional<std::string> parse_recovery_code(const HttpResponse& response);
std::optional<AccountInfo> parse_account(const HttpResponse& response);
// 404 gives a CharacterInfo without `exists`; other failures nothing.
std::optional<CharacterInfo> parse_character(const HttpResponse& response);
std::optional<std::vector<RoomListing>> parse_rooms(const HttpResponse& response);
std::optional<RoomView> parse_room_view(const HttpResponse& response);

// Local checks before a request (the server's rules, so the player hears of a
// mistake at once): user names 3-20 of A-Z a-z 0-9 _ -; passwords 10-128 and
// not the user name; room names 1-32; room passwords empty or 4-64. Empty
// text: fine.
std::string check_user_name(const std::string& user);
std::string check_password(const std::string& password, const std::string& user);
std::string check_room_name(const std::string& name);
std::string check_room_password(const std::string& password);

// "1.2.3" at least "1.2.0"? Versions as the server's ClientVersion reads them
// (two or three numbers, a "-suffix" or "+build" ignored); an unreadable
// minimum passes, an unreadable own version fails a readable minimum.
bool version_at_least(const std::string& version, const std::string& minimum);

} // namespace dw4::game::online::api
