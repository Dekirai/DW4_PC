#pragma once

// The DW4Online REST calls the S1 client needs (docs/SEAMLESS.md 6.2, 10):
// status, login, rooms create / join, a relay ticket for a reconnect, leave.
// Request builders and answer parsers are separate from the transport, so
// they are tested without a server.
//
//   GET  /status                       -> client.currentProtocol, client.minimumProtocol, maintenance, relay.port
//   POST /auth/login                   {username, password, deviceName, clientVersion, protocol} -> token, account
//   POST /rooms                        {name, password?, maxPlayers, protocol, dataHash, clientVersion, settings}
//   POST /rooms/{code}/join            {password?, protocol, dataHash, clientVersion}
//        both -> room.code, settings, you, and the room-scoped relay ticket: "ticket" at the top level
//                (round 52 relay lane), else relay.ticket
//   POST /rooms/{code}/ticket          -> ticket (a new one, for a reconnect)
//   POST /rooms/{code}/leave           -> 204
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
struct RoomJoined {
    std::string code;
    std::string name;
    RoomSettings settings;
    std::string ticket;  // empty: ask POST /rooms/{code}/ticket
    std::uint32_t member_id = 0;
    int relay_port = 0;  // 0: from GET /status
    int max_players = 0; // room.maxPlayers (the Discord party size)
    std::string owner;   // room.owner
};
struct CreateRoom {
    std::string name, password;
    int max_players = 4;
    RoomSettings settings;
};

HttpRequest status_request();
HttpRequest login_request(const std::string& user, const std::string& password, const std::string& client_version, int protocol);
HttpRequest create_room_request(const CreateRoom& room, int protocol, std::uint64_t data_hash, const std::string& client_version);
HttpRequest join_room_request(const std::string& code, const std::string& password, int protocol, std::uint64_t data_hash,
                              const std::string& client_version);
HttpRequest ticket_request(const std::string& code);
HttpRequest leave_request(const std::string& code);

std::optional<ServerStatus> parse_status(const HttpResponse& response);
std::optional<Login> parse_login(const HttpResponse& response);
std::optional<RoomJoined> parse_room(const HttpResponse& response);
std::optional<std::string> parse_ticket(const HttpResponse& response);
// The server's problem text (RFC 7807 "detail" / "title" / "code"), or the
// transport error, for messages.
std::string problem_text(const HttpResponse& response);

} // namespace dw4::game::online::api
