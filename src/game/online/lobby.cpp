#include "lobby.hpp"

#include "token_store.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace dw4::game::online {

namespace {

class WorkerQueue final : public HttpQueue {
public:
    WorkerQueue(ServerEndpoint endpoint, std::string user_agent) : worker_(std::move(endpoint), std::move(user_agent)) {}
    void set_token(std::string token) override { worker_.set_token(std::move(token)); }
    std::uint64_t submit(HttpRequest request) override { return worker_.submit(std::move(request)); }
    std::optional<std::pair<std::uint64_t, HttpResponse>> poll() override { return worker_.poll(); }

private:
    HttpWorker worker_;
};

constexpr const char* kAccountFile = "account.txt"; // the remembered account's name (the token beside it is DPAPI)

std::string read_account(const std::filesystem::path& folder) {
    std::ifstream in(folder / kAccountFile);
    std::string name;
    std::getline(in, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    return name;
}

void write_account(const std::filesystem::path& folder, const std::string& account) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const auto temp = folder / (std::string(kAccountFile) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << account << '\n';
        if (!out) return;
    }
    std::filesystem::rename(temp, folder / kAccountFile, error);
}

void remove_account(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::remove(folder / kAccountFile, error);
}

bool is_root(Screen screen) { return screen == Screen::welcome || screen == Screen::rooms || screen == Screen::in_room; }

std::string lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}

struct ScreenName {
    Screen screen;
    const char* name;
};
constexpr ScreenName kScreens[] = {
    {Screen::welcome, "welcome"},   {Screen::login, "login"},       {Screen::register_account, "register"},
    {Screen::registered, "registered"}, {Screen::recover, "recover"}, {Screen::recovered, "recovered"},
    {Screen::account, "account"},   {Screen::change_password, "password"}, {Screen::rooms, "rooms"},
    {Screen::create_room, "create"}, {Screen::join_room, "join"},   {Screen::confirm, "confirm"},
    {Screen::starting, "starting"}, {Screen::in_room, "room"},
};

} // namespace

std::unique_ptr<HttpQueue> worker_queue(ServerEndpoint endpoint, std::string user_agent) {
    return std::make_unique<WorkerQueue>(std::move(endpoint), std::move(user_agent));
}

const char* screen_name(Screen screen) {
    for (const auto& s : kScreens)
        if (s.screen == screen) return s.name;
    return "?";
}

std::optional<Screen> screen_from_name(const std::string& name) {
    for (const auto& s : kScreens)
        if (name == s.name) return s.screen;
    return std::nullopt;
}

OnlineLobby::OnlineLobby(std::unique_ptr<HttpQueue> http, LobbyConfig config) : http_(std::move(http)), config_(std::move(config)) {}

bool OnlineLobby::busy() const {
    return std::any_of(inflight_.begin(), inflight_.end(), [](const Pending& p) { return p.foreground; });
}

bool OnlineLobby::in_flight(Call call) const {
    return std::any_of(inflight_.begin(), inflight_.end(), [&](const Pending& p) { return p.call == call; });
}

void OnlineLobby::send(Call call, HttpRequest request, std::string busy_text) {
    Pending p;
    p.call = call;
    p.foreground = !busy_text.empty();
    if (p.foreground) busy_text_ = std::move(busy_text);
    p.id = http_->submit(std::move(request));
    inflight_.push_back(p);
}

void OnlineLobby::set_screen(Screen screen) {
    if (is_root(screen)) history_.clear();
    screen_ = screen;
}

void OnlineLobby::go(Screen screen) {
    if (screen == screen_) return;
    if (!is_root(screen) && screen_ != Screen::starting && screen_ != Screen::confirm) history_.push_back(screen_);
    set_screen(screen);
    error_.clear();
    info_text_.clear();
}

void OnlineLobby::back() {
    error_.clear();
    info_text_.clear();
    if (screen_ == Screen::confirm || screen_ == Screen::join_room) start_.reset();
    while (!history_.empty()) {
        const Screen previous = history_.back();
        history_.pop_back();
        // (Screens that no longer fit: the account ones after a logout, the
        // room list once inside a room.)
        const bool fits = logged_in() ? previous != Screen::welcome && previous != Screen::login && previous != Screen::register_account
                                      : previous == Screen::welcome || previous == Screen::login || previous == Screen::register_account ||
                                            previous == Screen::recover;
        if (fits && !(in_room() && previous == Screen::rooms)) {
            set_screen(previous);
            return;
        }
    }
    set_screen(in_room() ? Screen::in_room : logged_in() ? Screen::rooms : Screen::welcome);
}

void OnlineLobby::open() {
    opened_ = true;
    error_.clear();
    if (config_.not_set_up) {
        gate_ = Gate::not_set_up;
        return;
    }
    if (in_flight(Call::status)) return;
    gate_ = Gate::checking;
    send(Call::status, api::status_request());
}

std::string OnlineLobby::gate_text() const {
    switch (gate_) {
    case Gate::unknown:
    case Gate::open: return {};
    case Gate::checking: return "Connecting to the server...";
    case Gate::unreachable: return "The server does not answer. Check the connection and try again.";
    case Gate::maintenance: return "The server is in maintenance." + (status_.motd.empty() ? std::string() : " " + status_.motd);
    case Gate::outdated: {
        std::string text = "Update required: the server needs a newer version of the game";
        if (!status_.minimum_version.empty()) text += " (" + status_.minimum_version + " or newer)";
        return text + "; this is " + config_.client_version + ".";
    }
    case Gate::not_set_up: return "Online play is not available in this build yet: the server's certificate is not set up.";
    }
    return {};
}

bool OnlineLobby::newer_version() const {
    if (status_.current_protocol > config_.protocol) return true;
    return !status_.current_version.empty() && !api::version_at_least(config_.client_version, status_.current_version);
}

int OnlineLobby::max_difficulty() const { return character_ ? api::unlocked_difficulty(*character_) : 0; }

bool OnlineLobby::room_owner() const {
    if (room_view_) return room_view_->you.owner;
    return false;
}

void OnlineLobby::update(double now, bool shown) {
    now_ = now;
    while (auto done = http_->poll()) {
        const auto found = std::find_if(inflight_.begin(), inflight_.end(), [&](const Pending& p) { return p.id == done->first; });
        if (found == inflight_.end()) continue;
        const Pending call = *found;
        inflight_.erase(found);
        handle(call, done->second);
    }
    if (!busy()) busy_text_.clear();
    if (!shown || gate_ != Gate::open) return;
    if (screen_ == Screen::rooms && !in_flight(Call::rooms) && now - rooms_at_ >= config_.rooms_interval) refresh_rooms();
    if (screen_ == Screen::in_room && in_room() && logged_in() && !in_flight(Call::room_state) && now - room_at_ >= config_.room_interval)
        refresh_room();
}

bool OnlineLobby::handle_common(const HttpResponse& response, bool authorized) {
    if (response.status == 426) {
        gate_ = Gate::outdated;
        error_.clear();
        return true;
    }
    if (authorized && response.status == 401) {
        become_logged_out("Your login has ended. Log in again.");
        return true;
    }
    if (response.status == 403 && api::problem_of(response).code == "password_change_required") {
        set_screen(Screen::change_password);
        error_ = "Change the temporary password first.";
        return true;
    }
    return false;
}

void OnlineLobby::handle(const Pending& pending, const HttpResponse& response) {
    const auto problem = [&] { return api::problem_of(response).text; };
    switch (pending.call) {
    case Call::status: {
        const auto status = api::parse_status(response);
        if (!status) {
            gate_ = Gate::unreachable;
            return;
        }
        status_ = *status;
        if (status_.maintenance) gate_ = Gate::maintenance;
        else if (status_.minimum_protocol > config_.protocol || !api::version_at_least(config_.client_version, status_.minimum_version))
            gate_ = Gate::outdated;
        else gate_ = Gate::open;
        if (gate_ != Gate::open) return;
        if (logged_in()) {
            refresh_account();
            return;
        }
        // The remembered login: the token of the last account, checked with GET /me.
        const auto account = read_account(config_.folder);
        if (account.empty()) return;
        if (const auto token = load_token(config_.folder, config_.server_key, account)) {
            token_ = *token;
            account_ = account;
            remembered_ = true;
            http_->set_token(token_);
            send(Call::me, api::me_request(), "Logging in as " + account + "...");
        } else {
            remove_account(config_.folder);
        }
        return;
    }
    case Call::me: {
        if (const auto info = api::parse_account(response)) {
            info_ = info;
            account_ = info->name;
            if (screen_ == Screen::welcome) set_screen(Screen::rooms);
            if (info->must_change_password) {
                set_screen(Screen::change_password);
                error_ = "Change the temporary password first.";
            }
            if (!in_flight(Call::character)) send(Call::character, api::character_request());
            return;
        }
        if (response.status == 401 || response.status == 403) {
            // A refused token is forgotten; a network error keeps it.
            become_logged_out(remembered_ ? "The remembered login has ended. Log in again." : "Your login has ended. Log in again.");
            return;
        }
        if (response.status == 0) gate_ = Gate::unreachable;
        return;
    }
    case Call::character:
        if (handle_common(response, true)) return;
        if (const auto c = api::parse_character(response)) character_ = c;
        return;
    case Call::login: {
        if (handle_common(response, false)) return;
        const auto login = api::parse_login(response);
        if (!login) {
            error_ = problem();
            return;
        }
        become_logged_in(login->account.empty() ? login_name_ : login->account, login->token, remember_wanted_);
        set_screen(Screen::rooms);
        return;
    }
    case Call::register_account: {
        if (handle_common(response, false)) return;
        const auto r = api::parse_registered(response);
        if (!r) {
            error_ = problem();
            return;
        }
        become_logged_in(r->account.empty() ? login_name_ : r->account, r->token, remember_wanted_);
        recovery_code_ = r->recovery_code;
        set_screen(Screen::registered);
        return;
    }
    case Call::recover: {
        const auto code = api::parse_recovery_code(response);
        if (!code) {
            error_ = problem();
            return;
        }
        recovery_code_ = *code;
        set_screen(Screen::recovered);
        return;
    }
    case Call::logout:
    case Call::logout_all:
        // (Whatever the answer: the token is gone here.)
        become_logged_out(pending.call == Call::logout_all && response.ok() ? "Logged out on every device." : "Logged out.");
        return;
    case Call::change_password:
        if (handle_common(response, true)) return;
        if (!response.ok()) {
            error_ = problem();
            return;
        }
        if (info_) info_->must_change_password = false;
        set_screen(Screen::account);
        history_.clear();
        info_text_ = "Password changed. Your other devices are logged out.";
        return;
    case Call::rooms: {
        rooms_at_ = now_;
        const auto rooms = api::parse_rooms(response);
        if (!rooms) {
            if (screen_ == Screen::rooms && error_.empty()) error_ = "The room list could not be loaded: " + problem();
            return;
        }
        rooms_ = *rooms;
        // This game's rooms first (in the server's order: newest first).
        std::stable_partition(rooms_.begin(), rooms_.end(),
                              [&](const api::RoomListing& r) { return r.compatible(config_.protocol, config_.data_hash); });
        rooms_loaded_ = true;
        return;
    }
    case Call::create:
    case Call::join: {
        if (handle_common(response, true)) {
            start_.reset();
            return;
        }
        const auto joined = api::parse_room(response);
        if (!joined) {
            const auto p = api::problem_of(response);
            if (p.code == "wrong_room_password" && start_ && !start_->create) {
                set_screen(Screen::join_room);
                error_ = p.text;
                return;
            }
            start_.reset();
            set_screen(Screen::rooms);
            error_ = p.code == "room_owned" ? p.text + " (your room is in the list: Rejoin or Close it)" : p.text;
            refresh_rooms();
            return;
        }
        if (joined->ticket.empty()) {
            // (An older server: the ticket comes from its own call.)
            room_code_ = joined->code;
            room_name_ = joined->name;
            ready_ = LobbyRoom{*joined, account_, token_, joined->relay_port ? joined->relay_port : status_.relay_port, pending.call == Call::create};
            send(Call::ticket, api::ticket_request(joined->code), "Starting the online game...");
            return;
        }
        finish_join(*joined);
        ready_->created = pending.call == Call::create;
        return;
    }
    case Call::ticket: {
        const auto ticket = api::parse_ticket(response);
        if (!ticket || !ready_) {
            ready_.reset();
            room_code_.clear();
            set_screen(Screen::rooms);
            error_ = "No relay ticket: " + problem();
            return;
        }
        ready_->joined.ticket = *ticket;
        busy_text_ = "Starting the online game...";
        return;
    }
    case Call::room_state: {
        room_at_ = now_;
        if (handle_common(response, true)) return;
        if (auto view = api::parse_room_view(response)) room_view_ = std::move(view);
        return;
    }
    case Call::kick:
        if (handle_common(response, true)) return;
        if (!response.ok()) error_ = problem();
        else info_text_ = "Removed from the room.";
        refresh_room();
        return;
    case Call::close:
        if (handle_common(response, true)) return;
        if (!response.ok()) error_ = problem();
        else info_text_ = "Your room is closed.";
        refresh_rooms();
        return;
    case Call::leave: return;
    }
}

void OnlineLobby::become_logged_in(const std::string& account, const std::string& token, bool remember) {
    account_ = account;
    token_ = token;
    http_->set_token(token_);
    info_.reset();
    character_.reset();
    if (remember && save_token(config_.folder, config_.server_key, account_, token_)) {
        write_account(config_.folder, account_);
        remembered_ = true;
    } else {
        // Not remembered: no token of this account stays on disk.
        forget_token(config_.folder, account_);
        if (lower(read_account(config_.folder)) == lower(account_)) remove_account(config_.folder);
        remembered_ = false;
    }
    send(Call::me, api::me_request());
}

void OnlineLobby::become_logged_out(const std::string& why) {
    if (!account_.empty() && remembered_) {
        forget_token(config_.folder, account_);
        remove_account(config_.folder);
    }
    token_.clear();
    http_->set_token({});
    remembered_ = false;
    info_.reset();
    character_.reset();
    start_.reset();
    ready_.reset();
    if (!in_room()) {
        set_screen(Screen::welcome);
        info_text_ = why;
    } else {
        error_ = why;
    }
    account_.clear();
}

void OnlineLobby::login(const std::string& user, const std::string& password, bool remember) {
    error_.clear();
    if (busy()) return;
    if (gate_ != Gate::open) {
        error_ = gate_text();
        return;
    }
    if (user.empty() || password.empty()) {
        error_ = "Enter your user name and password.";
        return;
    }
    login_name_ = user;
    remember_wanted_ = remember;
    send(Call::login, api::login_request(user, password, config_.client_version, config_.protocol), "Logging in...");
}

void OnlineLobby::register_account(const std::string& user, const std::string& password, const std::string& again, const std::string& email,
                                   bool remember) {
    error_.clear();
    if (busy()) return;
    if (gate_ != Gate::open) {
        error_ = gate_text();
        return;
    }
    if (!status_.registration_open) {
        error_ = "Registration is closed on this server.";
        return;
    }
    if (auto problem = api::check_user_name(user); !problem.empty()) {
        error_ = problem;
        return;
    }
    if (auto problem = api::check_password(password, user); !problem.empty()) {
        error_ = problem;
        return;
    }
    if (password != again) {
        error_ = "The two passwords differ.";
        return;
    }
    if (!email.empty() && (email.find('@') == std::string::npos || email.find('.') == std::string::npos)) {
        error_ = "That does not look like an e-mail address (it is optional).";
        return;
    }
    login_name_ = user;
    remember_wanted_ = remember;
    send(Call::register_account, api::register_request(user, password, email), "Creating the account...");
}

void OnlineLobby::acknowledge_code() {
    recovery_code_.clear();
    error_.clear();
    info_text_.clear();
    if (screen_ == Screen::registered) {
        set_screen(logged_in() ? Screen::rooms : Screen::welcome);
        if (logged_in()) refresh_rooms();
    } else if (screen_ == Screen::recovered) {
        set_screen(Screen::welcome);
        history_.push_back(Screen::welcome);
        screen_ = Screen::login;
        info_text_ = "Log in with the new password.";
    }
}

void OnlineLobby::recover(const std::string& user, const std::string& code, const std::string& password, const std::string& again) {
    error_.clear();
    if (busy()) return;
    if (gate_ != Gate::open) {
        error_ = gate_text();
        return;
    }
    if (user.empty() || code.empty()) {
        error_ = "Enter the user name and the recovery code.";
        return;
    }
    if (auto problem = api::check_password(password, user); !problem.empty()) {
        error_ = problem;
        return;
    }
    if (password != again) {
        error_ = "The two passwords differ.";
        return;
    }
    recover_name_ = user;
    send(Call::recover, api::recover_request(user, code, password), "Setting the new password...");
}

void OnlineLobby::logout() {
    if (busy() || !logged_in() || in_room()) return;
    send(Call::logout, api::logout_request(), "Logging out...");
}

void OnlineLobby::logout_everywhere() {
    if (busy() || !logged_in() || in_room()) return;
    send(Call::logout_all, api::logout_all_request(), "Logging out everywhere...");
}

void OnlineLobby::change_password(const std::string& current, const std::string& next, const std::string& again) {
    error_.clear();
    if (busy() || !logged_in()) return;
    if (current.empty()) {
        error_ = "Enter your current password.";
        return;
    }
    if (auto problem = api::check_password(next, account_); !problem.empty()) {
        error_ = problem;
        return;
    }
    if (next != again) {
        error_ = "The two passwords differ.";
        return;
    }
    send(Call::change_password, api::change_password_request(current, next), "Changing the password...");
}

void OnlineLobby::refresh_account() {
    if (!logged_in()) return;
    if (!in_flight(Call::me)) send(Call::me, api::me_request());
}

void OnlineLobby::refresh_rooms() {
    if (gate_ != Gate::open || in_flight(Call::rooms)) return;
    rooms_at_ = now_;
    send(Call::rooms, api::rooms_request());
}

void OnlineLobby::create_room(const CreateForm& form) {
    error_.clear();
    if (busy()) return;
    if (!can_play()) {
        error_ = !config_.refusal.empty() ? config_.refusal : gate_ != Gate::open ? gate_text() : "Log in first.";
        return;
    }
    if (in_room()) {
        error_ = "Leave the room you are in first.";
        return;
    }
    if (auto problem = api::check_room_name(form.name); !problem.empty()) {
        error_ = problem;
        return;
    }
    if (auto problem = api::check_room_password(form.password); !problem.empty()) {
        error_ = problem;
        return;
    }
    if (form.max_players < 2 || form.max_players > 10) {
        error_ = "A room has 2 to 10 players.";
        return;
    }
    if (form.settings.difficulty < 0 || form.settings.difficulty > max_difficulty()) {
        error_ = "Your online character has not unlocked that difficulty yet.";
        return;
    }
    start_ = Start{true, form, {}, {}};
    if (offline_game_) go(Screen::confirm);
    else start_request();
}

void OnlineLobby::join_room(const std::string& code_or_name, const std::optional<std::string>& password) {
    error_.clear();
    if (busy()) return;
    if (!can_play()) {
        error_ = !config_.refusal.empty() ? config_.refusal : gate_ != Gate::open ? gate_text() : "Log in first.";
        return;
    }
    if (in_room()) {
        error_ = "Leave the room you are in first.";
        return;
    }
    std::optional<api::RoomListing> target;
    for (const auto& r : rooms_)
        if (r.code == code_or_name || r.name == code_or_name) {
            target = r;
            break;
        }
    if (target && !target->compatible(config_.protocol, config_.data_hash)) {
        error_ = "This room plays another version of the game or other game data.";
        return;
    }
    if (target && target->players >= target->max_players && target->max_players > 0 && lower(target->owner) != lower(account_)) {
        error_ = "The room is full.";
        return;
    }
    const std::string code = target ? target->code : code_or_name;
    if (target && target->has_password && !password) {
        join_target_ = target;
        start_ = Start{false, {}, code, {}};
        go(Screen::join_room);
        return;
    }
    if (password) {
        if (auto problem = api::check_room_password(*password); !problem.empty() || (target && target->has_password && password->empty())) {
            error_ = problem.empty() ? "Enter the room's password." : problem;
            if (screen_ != Screen::join_room) {
                join_target_ = target;
                go(Screen::join_room);
                error_ = problem.empty() ? "Enter the room's password." : problem;
            }
            return;
        }
    }
    join_target_ = target;
    start_ = Start{false, {}, code, password.value_or("")};
    if (offline_game_) go(Screen::confirm);
    else start_request();
}

void OnlineLobby::confirm_start() {
    if (screen_ != Screen::confirm || !start_) return;
    start_request();
}

void OnlineLobby::cancel_start() {
    start_.reset();
    back();
}

void OnlineLobby::start_request() {
    if (!start_) return;
    set_screen(Screen::starting);
    error_.clear();
    info_text_.clear();
    if (start_->create) {
        api::CreateRoom create;
        create.name = start_->form.name;
        create.password = start_->form.password;
        create.max_players = start_->form.max_players;
        create.settings = start_->form.settings;
        send(Call::create, api::create_room_request(create, config_.protocol, config_.data_hash, config_.client_version), "Opening the room...");
    } else {
        send(Call::join, api::join_room_request(start_->code, start_->password, config_.protocol, config_.data_hash, config_.client_version),
             "Joining the room...");
    }
}

void OnlineLobby::finish_join(const api::RoomJoined& joined) {
    room_code_ = joined.code;
    room_name_ = joined.name;
    ready_ = LobbyRoom{joined, account_, token_, joined.relay_port ? joined.relay_port : status_.relay_port, false};
    busy_text_ = "Starting the online game...";
}

std::optional<LobbyRoom> OnlineLobby::take_ready() {
    if (!ready_ || ready_->joined.ticket.empty() || in_flight(Call::ticket)) return std::nullopt;
    auto room = std::move(ready_);
    ready_.reset();
    start_.reset();
    return room;
}

void OnlineLobby::session_started() {
    busy_text_.clear();
    room_view_.reset();
    room_at_ = -1e9;
    set_screen(Screen::in_room);
    error_.clear();
    info_text_.clear();
    refresh_room();
}

void OnlineLobby::session_failed(const std::string& error) {
    // The server keeps the membership until it hears of the leave.
    if (!room_code_.empty() && logged_in()) send(Call::leave, api::leave_request(room_code_));
    room_code_.clear();
    room_name_.clear();
    room_view_.reset();
    ready_.reset();
    start_.reset();
    set_screen(logged_in() ? Screen::rooms : Screen::welcome);
    error_ = error;
    refresh_rooms();
}

void OnlineLobby::session_ended(const std::string& reason) {
    room_code_.clear();
    room_name_.clear();
    room_view_.reset();
    leave_requested_ = false;
    set_screen(logged_in() ? Screen::rooms : Screen::welcome);
    error_.clear();
    info_text_ = reason;
    refresh_rooms();
    // (The online character changed while playing.)
    if (logged_in() && !in_flight(Call::character)) send(Call::character, api::character_request());
}

void OnlineLobby::adopt_room(const std::string& account, const std::string& token, const std::string& code, const std::string& name) {
    if (token_.empty()) {
        account_ = account;
        token_ = token;
        http_->set_token(token_);
    }
    room_code_ = code;
    room_name_ = name;
    session_started();
}

void OnlineLobby::refresh_room() {
    if (!in_room() || !logged_in() || in_flight(Call::room_state)) return;
    room_at_ = now_;
    send(Call::room_state, api::room_state_request(room_code_));
}

void OnlineLobby::kick(const std::string& user) {
    error_.clear();
    if (busy() || !in_room() || !room_owner()) return;
    if (lower(user) == lower(account_)) return;
    send(Call::kick, api::kick_request(room_code_, user), "Removing " + user + "...");
}

void OnlineLobby::close_room(const std::string& code) {
    error_.clear();
    if (busy() || !logged_in() || code == room_code_) return;
    send(Call::close, api::close_room_request(code), "Closing the room...");
}

} // namespace dw4::game::online
