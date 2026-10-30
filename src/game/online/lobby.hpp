#pragma once

// The account and room lobby behind the title's Online panel and the F1
// Online tab (docs/SEAMLESS.md 10): the version gate, log in (remember me),
// register with the one-time recovery code, recover, log out (here and
// everywhere), change the password, the account with the online
// character's summary, the room list, create (name, optional password, 2-10
// players, difficulty up to what the account unlocked, camera mode, light
// mode, randomizer, fast loading), join (password prompt), the confirmation
// that ends an offline game, and the room as its members see it (REST state
// every three seconds while shown; kick for the owner). It never blocks: every
// request goes through an HttpQueue (the WinHTTP worker in the game, a fake in
// the tests) and comes back in update().
//
// The lobby only prepares a room: once the server answered a create or join,
// take_ready() hands the host the joined room (ticket, settings, token); the
// host starts the online session (online_run.hpp) and tells the lobby how it
// went (session_started / session_failed / session_ended). Leaving goes the
// other way: request_leave() is taken by the host, which ends the session.
//
// No ImGui here (src/game/online_ui.cpp draws it); the screen is part of the
// model so the UI, the tests and the test-run script (online_ui_script.hpp)
// drive the same transitions.
#include "api.hpp"
#include "http.hpp"
#include "lobby_api.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dw4::game::online {

// The lobby's transport: the WinHTTP worker in the game, a scripted fake in
// tests.
class HttpQueue {
public:
    virtual ~HttpQueue() = default;
    virtual void set_token(std::string token) = 0;
    virtual std::uint64_t submit(HttpRequest request) = 0;
    virtual std::optional<std::pair<std::uint64_t, HttpResponse>> poll() = 0;
};
// The game's queue over an HttpWorker it owns.
std::unique_ptr<HttpQueue> worker_queue(ServerEndpoint endpoint, std::string user_agent);

enum class Screen {
    welcome,          // logged out: log in, register, recover
    login,
    register_account,
    registered,       // the recovery code, shown once
    recover,
    recovered,        // the new recovery code, shown once
    account,
    change_password,
    rooms,            // the room list (logged in)
    create_room,
    join_room,        // the password prompt
    confirm,          // "the offline game ends"
    starting,         // the request and the session start
    in_room,
};
const char* screen_name(Screen screen);
std::optional<Screen> screen_from_name(const std::string& name);

// Whether this machine may play online at all (shown above every screen).
enum class Gate {
    unknown,          // not asked yet
    checking,         // GET /status in flight
    open,
    unreachable,      // no answer (Try again)
    maintenance,
    outdated,         // the server needs a newer protocol or version
    not_set_up,       // this build has no pin for the server's certificate
};

struct LobbyConfig {
    int protocol = 0;                    // kOnlineProtocol
    std::uint64_t data_hash = 0;
    std::string client_version;          // DW4_PC_VERSION
    std::filesystem::path folder;        // saves/online: the remembered login (DPAPI token, account.txt)
    std::string server_key;              // host:port (the token's entropy)
    bool not_set_up = false;             // the endpoint is pinned but has no pin
    std::string refusal;                 // the CPU gate or the environment check (empty: fine)
    double rooms_interval = 10.0;        // the room list refreshes while shown
    double room_interval = 3.0;          // the room state refreshes while shown
};

// What the host needs to start the session (OnlineRoom in online_run.hpp).
struct LobbyRoom {
    api::RoomJoined joined;
    std::string account, token;
    int relay_port = 52041;
    bool created = false;                // this member opened the room
};

// The room create form's values (also the test script's).
struct CreateForm {
    std::string name, password;
    int max_players = 4;
    RoomSettings settings;
};

class OnlineLobby {
public:
    OnlineLobby(std::unique_ptr<HttpQueue> http, LobbyConfig config);

    // Network answers, timers. `shown`: the lobby is on screen (the room list
    // and the room state refresh only then).
    void update(double now, bool shown);
    // The first use: the version gate and the remembered login. Again after
    // "Try again". Nothing happens before (no network at all).
    void open();
    bool opened() const { return opened_; }

    // Navigation (clears the message lines).
    Screen screen() const { return screen_; }
    void go(Screen screen);
    void back();

    // Facts for the screens.
    Gate gate() const { return gate_; }
    std::string gate_text() const;       // the banner for a gate other than open
    const api::ServerStatus& status() const { return status_; }
    bool newer_version() const;          // the server knows a newer client
    bool logged_in() const { return !token_.empty(); }
    const std::string& account() const { return account_; }
    const std::optional<api::AccountInfo>& account_info() const { return info_; }
    const std::optional<api::CharacterInfo>& character() const { return character_; }
    int max_difficulty() const;          // what a created room may use
    bool remembered() const { return remembered_; }
    bool busy() const;
    const std::string& busy_text() const { return busy_text_; }
    const std::string& error() const { return error_; }
    const std::string& info() const { return info_text_; }
    const std::string& recovery_code() const { return recovery_code_; }
    const std::string& recover_name() const { return recover_name_; }
    const std::vector<api::RoomListing>& rooms() const { return rooms_; }
    bool rooms_loaded() const { return rooms_loaded_; }
    const std::optional<api::RoomListing>& join_target() const { return join_target_; }
    const std::optional<api::RoomView>& room() const { return room_view_; }
    const std::string& room_code() const { return room_code_; }
    const std::string& room_name() const { return room_name_; }
    bool in_room() const { return !room_code_.empty(); }
    bool room_owner() const;
    // Create and join: the server is open, the account logged in, this PC may play online.
    bool can_play() const { return gate_ == Gate::open && logged_in() && config_.refusal.empty(); }
    const std::string& refusal() const { return config_.refusal; }

    // The host says whether an offline game would end (an engine past the
    // title): then create and join ask first.
    void set_offline_game(bool running) { offline_game_ = running; }

    // Account actions (a check that fails locally sets error() and sends
    // nothing).
    void login(const std::string& user, const std::string& password, bool remember);
    void register_account(const std::string& user, const std::string& password, const std::string& again, const std::string& email,
                          bool remember);
    void acknowledge_code();             // "I saved it"
    void recover(const std::string& user, const std::string& code, const std::string& password, const std::string& again);
    void logout();
    void logout_everywhere();
    void change_password(const std::string& current, const std::string& next, const std::string& again);
    void refresh_account();

    // Rooms.
    void refresh_rooms();
    void create_room(const CreateForm& form);
    // Join by code or name; a room with a password asks for it first
    // (screen join_room) unless one is given.
    void join_room(const std::string& code_or_name, const std::optional<std::string>& password = std::nullopt);
    void confirm_start();                // the "end the offline game" question: yes
    void cancel_start();
    void kick(const std::string& user);
    void close_room(const std::string& code);
    void refresh_room();

    // The host's side of the session.
    std::optional<LobbyRoom> take_ready();
    void session_started();
    void session_failed(const std::string& error);
    void session_ended(const std::string& reason);
    // A room the command line entered (--online-login): the lobby shows it.
    void adopt_room(const std::string& account, const std::string& token, const std::string& code, const std::string& name);
    void request_leave() { leave_requested_ = true; }
    bool take_leave_request() { return std::exchange(leave_requested_, false); }

private:
    enum class Call {
        status, me, character, login, register_account, recover, logout, logout_all, change_password, rooms, create, join, ticket,
        room_state, kick, close, leave
    };
    struct Pending {
        std::uint64_t id = 0;
        Call call{};
        bool foreground = false;         // the screens wait for it (busy)
    };
    // Foreground requests block the screen's buttons (busy_text); background
    // ones (refreshes) do not, and at most one of each kind is in flight.
    void send(Call call, HttpRequest request, std::string busy_text = {});
    bool in_flight(Call call) const;
    void handle(const Pending& call, const HttpResponse& response);
    bool handle_common(const HttpResponse& response, bool authorized);
    void become_logged_in(const std::string& account, const std::string& token, bool remember);
    void become_logged_out(const std::string& why);
    void start_request();                // the create or join after the confirmation
    void finish_join(const api::RoomJoined& joined);
    void set_screen(Screen screen);

    std::unique_ptr<HttpQueue> http_;
    LobbyConfig config_;
    bool opened_ = false;
    Screen screen_ = Screen::welcome;
    std::vector<Screen> history_;
    Gate gate_ = Gate::unknown;
    api::ServerStatus status_;
    std::vector<Pending> inflight_;
    std::string busy_text_, error_, info_text_;
    double now_ = 0;

    std::string account_, token_;
    bool remembered_ = false, remember_wanted_ = false;
    std::string login_name_;             // the name typed at login / register (before the server's spelling)
    std::optional<api::AccountInfo> info_;
    std::optional<api::CharacterInfo> character_;
    std::string recovery_code_, recover_name_;

    std::vector<api::RoomListing> rooms_;
    bool rooms_loaded_ = false;
    double rooms_at_ = -1e9;
    std::optional<api::RoomListing> join_target_;

    // A create or join on its way (the confirmation, then the request).
    struct Start {
        bool create = false;
        CreateForm form;
        std::string code, password;
    };
    std::optional<Start> start_;
    bool offline_game_ = false;
    std::optional<LobbyRoom> ready_;
    std::string room_code_, room_name_;
    std::optional<api::RoomView> room_view_;
    double room_at_ = -1e9;
    bool leave_requested_ = false;
};

} // namespace dw4::game::online
