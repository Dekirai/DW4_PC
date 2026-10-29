#pragma once

// The game's online room session (docs/SEAMLESS.md 10, round S1): the
// command-line development login and room create / join, the relay
// connection with reconnects, and the session the main loop drives. The
// account and room UI of the title and F1 menu come in round S2; this is the
// path both build on.
//
//   --online-login NAME        the account (password: the remembered DPAPI token,
//                              else DW4_ONLINE_PASSWORD)
//   --online-create "NAME"     open a room (--online-max N, --online-difficulty N,
//                              --online-camera N, --online-light N, --online-fast)
//   --online-join CODE         join a room
// Release builds talk to the hard-coded server only; DW4_ONLINE_DEV builds
// may point at a loopback test server (server_config.hpp).
#include "online/api.hpp"
#include "online/engine_adapter.hpp"
#include "online/http.hpp"
#include "online/leave_combo.hpp"
#include "online/session.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace dw4::game {

inline constexpr std::uint32_t kOnlineProtocol = 44; // DW4R2, the first seamless protocol (the REST "protocol" too)

struct OnlineOptions {
    std::string user, create, join;
    int max_players = 4;
    online::RoomSettings settings;
    bool wanted() const { return !user.empty() && (!create.empty() || !join.empty()); }
};

// The blocking part before the game loop: gates, status, login, room,
// ticket. Nothing on failure (error says why, for the player).
struct OnlineRoom {
    online::ServerEndpoint endpoint;
    std::string account, token, code, room_name, ticket;
    std::uint32_t member_id = 0;
    int relay_port = 52041;
    online::RoomSettings settings;
};
std::optional<OnlineRoom> online_enter_room(online::HttpWorker& http, const OnlineOptions& options, std::uint64_t data_hash,
                                            const std::string& client_version, const std::filesystem::path& token_folder, std::string& error);

class OnlineRun {
public:
    OnlineRun(std::unique_ptr<online::HttpWorker> http, OnlineRoom room, online::EngineHooks hooks, std::uint64_t data_hash,
              std::uint32_t rate);
    // Connects to the relay (blocking up to five seconds).
    bool connect(double now, std::string& error);
    // Every loop pass: network, catch-up, reconnects.
    void pass(double now);
    online::Session& session() { return *session_; }
    online::GameEngineLink& link() { return link_; }
    const OnlineRoom& room() const { return room_; }
    // The local pad of each live step: true when B + X + START completed its hold.
    bool leave_combo(std::uint32_t buttons) { return combo_.update(buttons); }
    bool leave_prompt = false;
    // Leaves the room (BYE, REST leave).
    void leave();

private:
    std::unique_ptr<online::HttpWorker> http_;
    OnlineRoom room_;
    online::GameEngineLink link_;
    std::unique_ptr<online::Session> session_;
    online::LeaveCombo combo_;
    std::uint64_t ticket_request_ = 0;
    double next_reconnect_ = 0;
    bool left_ = false;
};

} // namespace dw4::game
