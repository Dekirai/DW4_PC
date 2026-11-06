#pragma once

// The DW4Online REST calls of the world session (docs/SEAMLESS.md 6.2, 10;
// round I1a, 19.19 and the round's contract): status, login, the world's
// entry, a relay ticket for a reconnect, leave, the world's counts and the
// online preferences. Request builders and answer parsers are separate from
// the transport, so they are tested without a server.
//
//   GET  /status                       -> client.currentProtocol, client.minimumProtocol, maintenance, relay.port
//   POST /auth/login                   {username, password, deviceName, clientVersion, protocol} -> token, account
//   POST /world/enter                  {box, protocol, dataHash, clientVersion}
//        -> relayTicket (or ticket), relay {host, port}, protocol, role, relayRole, box, prefs, settings
//   POST /world/ticket                 the same body and answer (a reconnect)
//   POST /world/leave                  -> 204
//   GET  /world                        -> players (playersOnline), instances, areas
//   GET  /me/online-prefs              -> defaultVisibility, invites, whereabouts, friendToasts, cameraMode, lightMode,
//                                         fastLoading, randomizer {enabled, seed, drops, enemies, zones}, lastBox
//   PUT  /me/online-prefs              any subset of the same -> the stored object
#include "http.hpp"
#include "room_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dw4::game::online::api {

struct ServerStatus {
    int current_protocol = 0, minimum_protocol = 0;
    std::string current_version, minimum_version; // client.currentVersion / minimumVersion ("" = none)
    bool maintenance = false;
    bool registration_open = true;                // "registration": "open"
    int relay_port = 52041;
    std::string motd;
};
struct Login {
    std::string token;
    std::string account; // the server's spelling of the account name
};
// The online preferences (19.17.1's online_prefs): the default visibility of
// a new instance (Q9: the last choice) and the host settings of 19.8.7
// (camera, light, fast loading, randomizer; the difficulty is the box's,
// chosen at the Game Start).
struct OnlinePrefs {
    std::string default_visibility = "public"; // public | friends | private
    RoomSettings settings;                    // difficulty unused
    int last_box = 1;
};
// POST /world/enter (and /world/ticket).
struct WorldEntry {
    std::string ticket;
    std::string relay_host;  // empty: the server's host
    int relay_port = 0;      // 0: from GET /status
    int protocol = 0;
    std::string role = "player", relay_role = "member";
    int box = 1;
    OnlinePrefs prefs;
};
// GET /world (public): the home screen's line.
struct WorldCounts {
    int players = 0, instances = 0;
};
// "Public" / "Friends" / "Private" and back (0-2, the wire's Visibility).
int visibility_index(const std::string& name);
const char* visibility_key(int index);

HttpRequest status_request();
HttpRequest login_request(const std::string& user, const std::string& password, const std::string& client_version, int protocol);
HttpRequest world_enter_request(int box, int protocol, std::uint64_t data_hash, const std::string& client_version);
HttpRequest world_ticket_request(int protocol, std::uint64_t data_hash, const std::string& client_version);
HttpRequest world_leave_request();
HttpRequest world_counts_request();
// GET /friends (round I2-social; the friends' presence since I1a's web lane): the lobby's welcome counts
// the friends online.
HttpRequest friends_request();
HttpRequest online_prefs_request();
HttpRequest put_online_prefs_request(const OnlinePrefs& prefs);

std::optional<ServerStatus> parse_status(const HttpResponse& response);
std::optional<Login> parse_login(const HttpResponse& response);
std::optional<WorldEntry> parse_world_entry(const HttpResponse& response);
std::optional<std::string> parse_ticket(const HttpResponse& response);
std::optional<WorldCounts> parse_world_counts(const HttpResponse& response);
// The friends whose presence says online (a friend who shows nothing has presence null).
std::optional<int> parse_friends_online(const HttpResponse& response);
// The lobby's welcome after Play (19.5.1): "Welcome back, Kim. 14 players online, 2 friends."; the
// players' part only with GET /world's count, the friends' only with one or more online.
std::string welcome_text(const std::string& name, std::optional<int> players, std::optional<int> friends_online);
std::optional<OnlinePrefs> parse_online_prefs(const HttpResponse& response);
// The server's problem text (RFC 7807 "detail" / "title" / "code"), or the
// transport error, for messages.
std::string problem_text(const HttpResponse& response);

} // namespace dw4::game::online::api
