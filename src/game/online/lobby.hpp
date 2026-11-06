#pragma once

// The account lobby and the home screen behind the title's Online panel and
// the F1 Online tab (docs/SEAMLESS.md 10; round I1a, 19.5.1: no rooms): the
// version gate, log in (remember me), register with the one-time recovery
// code, recover, log out (here and everywhere), change the password, the
// account with the online character's summary, the home screen (Play, the
// world's counts), Online > Settings (the online preferences: the new
// instance's visibility, camera and light mode, fast loading, randomizer),
// the confirmation that ends an offline game, and the screen while the world
// session runs. It never blocks: every request goes through an HttpQueue
// (the WinHTTP worker in the game, a fake in the tests) and comes back in
// update().
//
// The lobby only prepares the world session: once the server answered Play
// (POST /world/enter), take_ready() hands the host the entry (ticket,
// settings, token); the host starts the online session (online_run.hpp) and
// tells the lobby how it went (session_started / session_failed /
// session_ended). Leaving goes the other way: request_leave() is taken by
// the host, which ends the session.
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
    reset_character,  // the online character's reset (password, a warning)
    home,             // logged in: Play, Settings, Account, Log out, the world's counts
    settings,         // Online > Settings (the online preferences)
    confirm,          // "the offline game ends"
    starting,         // the request and the session start
    playing,          // the world session runs (the F1 Online tab's Instance page)
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
const char* gate_name(Gate gate); // "open", "outdated", ... (test runs)

// A session's close reason for people: "X (X.)" (the relay's CLOSE text that
// repeats the client's own sentence) becomes "X.".
std::string tidy_reason(const std::string& reason);

struct LobbyConfig {
    int protocol = 0;                    // kOnlineProtocol
    std::uint64_t data_hash = 0;
    std::string client_version;          // DW4_PC_VERSION
    std::filesystem::path folder;        // saves/online: the remembered login (DPAPI token, account.txt)
    std::string server_key;              // host:port (the token's entropy)
    bool not_set_up = false;             // the endpoint is pinned but has no pin
    std::string refusal;                 // the CPU gate or the environment check (empty: fine)
    double world_interval = 10.0;        // the world's counts refresh while the home screen shows
    int box = 1;                         // the box Play starts (1 until round D)
};

// What the host needs to start the session (OnlineRoom in online_run.hpp).
struct LobbyWorld {
    api::WorldEntry entry;
    std::string account, token;
    int relay_port = 52041;
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
    // back() leads somewhere: this is not the first screen (the home screen
    // logged in, the welcome logged out, the playing screen in the world).
    bool can_back() const;

    // Facts for the screens.
    Gate gate() const { return gate_; }
    std::string gate_text() const;       // the banner for a gate other than open
    const api::ServerStatus& status() const { return status_; }
    bool newer_version() const;          // the server knows a newer client
    bool logged_in() const { return !token_.empty(); }
    const std::string& account() const { return account_; }
    // The session token of the login (empty: logged out): other panels that
    // talk to the server for this account (the cloud backup) can share it.
    const std::string& token() const { return token_; }
    const std::optional<api::AccountInfo>& account_info() const { return info_; }
    const std::optional<api::CharacterInfo>& character() const { return character_; }
    int max_difficulty() const;          // what the character select offers (the box's clears)
    bool remembered() const { return remembered_; }
    bool busy() const;
    const std::string& busy_text() const { return busy_text_; }
    const std::string& error() const { return error_; }
    const std::string& info() const { return info_text_; }
    const std::string& recovery_code() const { return recovery_code_; }
    const std::string& recover_name() const { return recover_name_; }
    // GET /world: "Online now: <n> players, <m> instances" (nothing yet: none).
    const std::optional<api::WorldCounts>& world_counts() const { return counts_; }
    std::uint64_t counts_serial() const { return counts_serial_; } // counts the answers that arrived
    // The online preferences (GET /me/online-prefs; Online > Settings).
    const std::optional<api::OnlinePrefs>& prefs() const { return prefs_; }
    // The world session runs (Play started it).
    bool in_world() const { return playing_; }
    // "Play: <form> Lv <n> (Box <b>)" (19.5.6), "Play: new character (Box 1)".
    std::string play_label() const;
    // Play: the server is open, the account logged in, this PC may play online.
    bool can_play() const { return gate_ == Gate::open && logged_in() && config_.refusal.empty(); }
    const std::string& refusal() const { return config_.refusal; }
    int protocol() const { return config_.protocol; }
    std::uint64_t data_hash() const { return config_.data_hash; }
    // A login this PC remembers (read at construction): the host may open the lobby at the title.
    bool remembers_login() const { return remembers_login_; }

    // The host says whether an offline game would end (an engine past the
    // title): then Play asks first.
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
    // Ends the online character (and empties its online story): the next
    // online game starts a new one. Not while the world session runs.
    void reset_character(const std::string& current_password);
    void refresh_account();

    // The world (round I1a).
    void refresh_world();
    void refresh_prefs();
    // Play: POST /world/enter for the box (after "the offline game ends?").
    void play();
    // Online > Settings: PUT /me/online-prefs (they count from the next Play).
    void save_settings(const api::OnlinePrefs& prefs);
    void confirm_start();                // the "end the offline game" question: yes
    void cancel_start();

    // The host's side of the session.
    std::optional<LobbyWorld> take_ready();
    void session_started();
    void session_failed(const std::string& error);
    void session_ended(const std::string& reason);
    // A world session the command line started (--online-play): the lobby shows it.
    void adopt_world(const std::string& account, const std::string& token);
    void request_leave() { leave_requested_ = true; }
    bool take_leave_request() { return std::exchange(leave_requested_, false); }

private:
    enum class Call {
        status, me, character, login, register_account, recover, logout, logout_all, change_password, reset_character, world, prefs, put_prefs,
        enter, leave
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
    void start_request();                // Play's request after the confirmation
    void set_screen(Screen screen);
    Screen root() const;

    std::unique_ptr<HttpQueue> http_;
    LobbyConfig config_;
    bool opened_ = false;
    bool remembers_login_ = false;
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

    std::optional<api::WorldCounts> counts_;
    std::uint64_t counts_serial_ = 0;
    double counts_at_ = -1e9;
    std::optional<api::OnlinePrefs> prefs_;

    // Play on its way (the confirmation, then the request).
    bool start_ = false;
    bool offline_game_ = false;
    std::optional<LobbyWorld> ready_;
    bool playing_ = false;
    bool leave_requested_ = false;
};

} // namespace dw4::game::online
