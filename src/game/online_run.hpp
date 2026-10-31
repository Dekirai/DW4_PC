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
#include "online/character.hpp"
#include "online/engine_adapter.hpp"
#include "online/http.hpp"
#include "online/leave_combo.hpp"
#include "online/session.hpp"

#include "dw4/engine/port_events.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game {

// DW4R2, the seamless protocol (the REST "protocol" too): 44 round S1, 45
// round S2 (the room story in BARRIER_OK, STORY / STORY_ACK, the story at
// game start; an S1 client in an S2 room would desynchronize at its first
// barrier, so the rooms of the two never mix), 46 round 63 (a member's
// status window, NPC talks and map recorders are its own and pause nothing,
// docs/SEAMLESS.md 5.3; a 45 client would pause its instance for them and
// desynchronize at the first menu).
inline constexpr std::uint32_t kOnlineProtocol = 46;

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
    int max_players = 0; // the room's size (the Discord party)
};
std::optional<OnlineRoom> online_enter_room(online::HttpWorker& http, const OnlineOptions& options, std::uint64_t data_hash,
                                            const std::string& client_version, const std::filesystem::path& token_folder, std::string& error);

class OnlineRun {
public:
    OnlineRun(std::unique_ptr<online::HttpWorker> http, OnlineRoom room, online::EngineHooks hooks, std::uint64_t data_hash,
              std::uint32_t rate);
    // The online character (docs/SEAMLESS.md 9), blocking, before the
    // engine starts: GET /online-character into the virtual card in
    // `online_folder`/<account> (online_card.dat). The engine plays from
    // character_card() (card B absent). False with `error` when the server
    // gave neither the character nor 404.
    bool load_character(const std::filesystem::path& online_folder, const std::string& client_version, std::string& error);
    online::CharacterSync* character() { return character_.get(); }
    // The card store of the online character (slot A the cache file).
    save::CardStore character_card() const;
    // Connects to the relay (blocking up to five seconds).
    bool connect(double now, std::string& error);
    // Every loop pass: network, catch-up, reconnects, the character's uploads.
    void pass(double now);
    // After every live step (instead of link().after_live_step()): the room's
    // modes and story at game_started (the session's confirmed room story,
    // handed to the link on the step that set game_started, SEAMLESS.md 8.5),
    // the character's play time, and its
    // automatic save at the barrier tick (the step that executed NEXTSTAGE:
    // the state the member carries into the next stage).
    void after_live_step(double now);
    // The step's port events: card_written of the own slot on card A uploads
    // the box the save room wrote.
    void port_events(const std::vector<engine::PortEvent>& events);
    // The automatic save when the member leaves the room (also when the
    // server closed the session): the current state's record, then up to
    // `seconds` for the uploads to finish (what does not arrive stays pending
    // in the card cache for the next room entry). Once.
    void save_on_leave(double seconds = 8.0);
    online::Session& session() { return *session_; }
    online::GameEngineLink& link() { return link_; }
    const OnlineRoom& room() const { return room_; }
    // The local pad of each live step: true when B + X + START completed its hold.
    bool leave_combo(std::uint32_t buttons) { return combo_.update(buttons); }
    bool leave_prompt = false;
    // Leaves the room: the character's automatic save (waiting up to
    // `save_seconds` for the uploads), then BYE and the REST leave.
    void leave(double save_seconds = 8.0);

private:
    void dispatch(std::uint64_t id, const online::HttpResponse& response, double now);
    void save_character(online::CharacterSync::Why why);
    online::character_api::Context character_context() const;

    std::unique_ptr<online::HttpWorker> http_;
    OnlineRoom room_;
    std::function<engine::Engine&()> engine_;
    online::GameEngineLink link_;
    std::unique_ptr<online::Session> session_;
    std::unique_ptr<online::CharacterSync> character_;
    std::string client_version_;
    online::LeaveCombo combo_;
    std::uint64_t ticket_request_ = 0;
    double next_reconnect_ = 0;
    bool pending_before_ = false; // the last live step had a NEXTSTAGE pending
    bool saved_on_leave_ = false;
    bool left_ = false;
};

} // namespace dw4::game
