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
// desynchronize at the first menu), 47 rounds G-core and J, shipped together
// (G-core: flag 503 is story and merged at barriers, left alone at one into
// stag0001; the STORY_ACK reason queued; docs/SEAMLESS.md 8.8: a 46 client
// would not add 503 at a barrier where 47 clients do, and the instance would
// desynchronize. J: checkpoints - STATEHASH, CHECKPOINT, CHECKPOINT_BAD,
// CHECKPOINT_ASK, STATE_DATA codec 2, docs/SEAMLESS.md 6.13.7), 48 the READY
// check of a join from a checkpoint at its check tick (the donor's NETFP of
// T + 14 after an upload of T, wire::kReadyCheckTicks; a 47 joiner names the
// next multiple of 60 and a 47 donor never sends that NETFP, docs/SEAMLESS.md
// 6.13.5).
inline constexpr std::uint32_t kOnlineProtocol = 48;

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
    // The room's owner (room.owner) and whether this account is it (round
    // G-core, docs/SEAMLESS.md 8.8: a guest plays on the owner's story, its
    // card and uploads keep its own).
    // (Not the owner by default: both ways into a room set it with
    // set_room_owner. A guest taken for the owner put the room story into
    // its card, the leak round G-core closes; the other way round only its
    // own story would stand on the owner's card.)
    std::string owner_name;
    bool room_owner = false;
};
// The room's owner and whether `room.account` is it (api::owns_room): every
// way into a room calls this (--online-create / --online-join and the title's
// online screens, game_main's take_ready).
void set_room_owner(OnlineRoom& room, const online::api::RoomJoined& joined, bool created);
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
    // The card store of the online character (slot A the cache file). A
    // guest's store writes the account's own story into every used box (round
    // G-core: the save room's write never puts the owner's story on the card).
    save::CardStore character_card() const;
    // Called by pass() when a guest's own story changed since the last
    // character_card() (a 409's reload): the host gives the engine a new store.
    std::function<void()> card_story_changed;
    // Connects to the relay (blocking up to five seconds for the connection
    // and up to ten for the relay's answer: a newer connection of the same
    // account replaces one another PC holds in the room when the relay
    // answers, so the character loaded after it is the newest; round 59).
    bool connect(double now, std::string& error);
    // Every loop pass: network, catch-up, reconnects, the character's uploads.
    void pass(double now);
    // After every live step (instead of link().after_live_step()): the room's
    // modes and story at game_started (the session's confirmed room story,
    // handed to the link on the step that set game_started, SEAMLESS.md 8.5),
    // the character's play time, and its
    // automatic save at the barrier tick (the step that executed NEXTSTAGE:
    // the state the member carries into the next stage).
    // Round G-core (docs/SEAMLESS.md 8.8): no Game Start without the confirmed
    // room story. A Game Start (or Engine::common_rebuilt) that finds none holds
    // the next live step (Session::hold_for_story, "Loading the room story...")
    // until it comes; after 15 s without it the room is left ("The room story
    // did not arrive. Join the room again.").
    void after_live_step(double now);
    // Messages for the player (toasts): the guest's story at its Game Start, the
    // closed call shop.
    std::vector<std::string> take_notices();
    // "Story: yours" / "Story: <owner>'s, phase n, k key items" for the F1 Online
    // tab and the room view.
    std::string story_line() const;
    // The Game Start waits for the room story.
    bool story_held() const { return story_hold_since_ >= 0; }
    // The step's port events: card_written of the own slot on card A uploads
    // the box the save room wrote; new_character and character_called tell
    // the character the title's last choice before its Game Start.
    void port_events(const std::vector<engine::PortEvent>& events);
    // The relay closed the session because a newer connection of this account
    // (another PC) took its place: the leaving save stays pending in the card
    // cache instead of overwriting what the other PC goes on from.
    void replaced();
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
    void leave(double save_seconds = 8.0, const std::string& reason = {});
    // saves/online.log (online/net_log.hpp, round 62): opened for the
    // session, its first lines are the versions and protocols (the server's
    // come with its status); the session logs into it from here.
    void open_log(const std::filesystem::path& path, const std::string& client_version);
    const online::OnlineLog& log() const { return log_; }
    // What the host loop did in one pass, for online.log (round 62 review:
    // with two games on one PC the rates of passes and pictures, vsync, the
    // display's rate, the frame limit and which window was in front decide
    // what each game's pads cost; none of it was in the log).
    struct HostFacts {
        bool window = false;    // a window exists (test runs without pictures have none)
        bool presented = false; // this pass swapped a picture
        bool vsync = false;     // settings.ini vsync
        int swap_interval = 0;  // what the GL context reports (-1: adaptive)
        int fps_limit = 0;      // settings.ini fps_limit (0: off)
        double refresh_hz = 0;  // the window's display (0: unknown)
        bool focused = false, minimized = false, occluded = false, hidden = false;
    };
    // Once per pass of the host loop: a line when the settings or the window's
    // state change, and every ten seconds the rates of passes and pictures.
    void host_pass(double now, const HostFacts& facts);

private:
    void dispatch(std::uint64_t id, const online::HttpResponse& response, double now);
    // The Game Start with the confirmed story (or the hold without one).
    void begin_game(double now);
    std::optional<engine::RoomStory> story_for_start(double now);
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
    std::uint64_t status_request_ = 0; // the server's status for the log
    online::OnlineLog log_;
    struct HostWindow {
        double since = -1, last = -1, longest = 0;
        std::uint64_t passes = 0, presents = 0;
    } host_window_;
    std::optional<HostFacts> host_logged_; // the settings and window state of the last lines
    double next_reconnect_ = 0;
    double story_hold_since_ = -1;     // the Game Start waits for the room story since then (-1: no hold)
    double story_ignore_until_ = 0;    // DW4_ONLINE_TEST_STORY_DELAY (dev builds, G7): the story counts from then (set at the first Game Start)
    int callshop_seen_ = 0;            // Engine::callshop_refused already told
    mutable std::optional<save::StoryBytes> handed_story_; // the own story of the last character_card() (a guest's)
    std::vector<std::string> notices_;
    bool pending_before_ = false; // the last live step had a NEXTSTAGE pending
    bool saved_on_leave_ = false;
    bool left_ = false;
};

} // namespace dw4::game
