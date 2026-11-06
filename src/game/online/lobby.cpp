#include "lobby.hpp"

#include "character.hpp"
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

bool is_root(Screen screen) { return screen == Screen::welcome || screen == Screen::home || screen == Screen::playing; }

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
    {Screen::account, "account"},   {Screen::change_password, "password"}, {Screen::reset_character, "reset"}, {Screen::home, "home"},
    {Screen::settings, "settings"}, {Screen::confirm, "confirm"},   {Screen::starting, "starting"}, {Screen::playing, "playing"},
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

std::string tidy_reason(const std::string& reason) {
    const auto open = reason.find(" (");
    if (open == std::string::npos || reason.size() < open + 4 || reason.compare(reason.size() - 2, 2, ").") != 0) return reason;
    auto head = reason.substr(0, open);
    auto inner = reason.substr(open + 2, reason.size() - open - 4);
    const auto bare = [](std::string text) {
        while (!text.empty() && (text.back() == '.' || text.back() == ' ')) text.pop_back();
        return text;
    };
    if (bare(head) == bare(inner)) return bare(head) + ".";
    return reason;
}

const char* gate_name(Gate gate) {
    switch (gate) {
    case Gate::unknown: return "unknown";
    case Gate::checking: return "checking";
    case Gate::open: return "open";
    case Gate::unreachable: return "unreachable";
    case Gate::maintenance: return "maintenance";
    case Gate::outdated: return "outdated";
    case Gate::not_set_up: return "not_set_up";
    }
    return "?";
}

std::optional<Screen> screen_from_name(const std::string& name) {
    for (const auto& s : kScreens)
        if (name == s.name) return s.screen;
    return std::nullopt;
}

OnlineLobby::OnlineLobby(std::unique_ptr<HttpQueue> http, LobbyConfig config) : http_(std::move(http)), config_(std::move(config)) {
    remembers_login_ = !read_account(config_.folder).empty();
}

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

Screen OnlineLobby::root() const { return in_world() ? Screen::playing : logged_in() ? Screen::home : Screen::welcome; }

void OnlineLobby::back() {
    error_.clear();
    info_text_.clear();
    if (screen_ == Screen::confirm) start_ = false;
    while (!history_.empty()) {
        const Screen previous = history_.back();
        history_.pop_back();
        // (Screens that no longer fit: the account ones after a logout, the
        // home screen once the world session runs.)
        const bool fits = logged_in() ? previous != Screen::welcome && previous != Screen::login && previous != Screen::register_account
                                      : previous == Screen::welcome || previous == Screen::login || previous == Screen::register_account ||
                                            previous == Screen::recover;
        if (fits && !(in_world() && previous == Screen::home)) {
            set_screen(previous);
            return;
        }
    }
    set_screen(root());
}

bool OnlineLobby::can_back() const { return screen_ != root() && screen_ != Screen::starting; }

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

std::string OnlineLobby::play_label() const {
    const std::string box = " (Box " + std::to_string(config_.box) + ")";
    if (!character_) return "Play" + box;
    if (!character_->exists) return "Play: new character" + box;
    std::string form = character_->digimon;
    if (form.rfind("p_", 0) == 0) form = form.substr(2);
    if (!form.empty() && form[0] >= 'a' && form[0] <= 'z') form[0] = char(form[0] - 'a' + 'A');
    return "Play: " + (form.empty() ? std::string("your character") : form) + " Lv " + std::to_string(character_->level) + box;
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
    if ((screen_ == Screen::home || screen_ == Screen::playing) && !in_flight(Call::world) && now - counts_at_ >= config_.world_interval) refresh_world();
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
            if (screen_ == Screen::welcome) set_screen(Screen::home);
            if (info->must_change_password) {
                set_screen(Screen::change_password);
                error_ = "Change the temporary password first.";
            }
            if (!in_flight(Call::character)) send(Call::character, api::character_request());
            refresh_prefs();
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
        set_screen(Screen::home);
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
    case Call::reset_character:
        if (handle_common(response, true)) return;
        if (!response.ok() && response.status != 404) {
            error_ = problem();
            return;
        }
        set_screen(Screen::account);
        history_.clear();
        info_text_ = "The online character was reset. Your next online game starts a new one at level 1, with a new story.";
        character_.reset();
        if (!in_flight(Call::character)) send(Call::character, api::character_request());
        return;
    case Call::world: {
        counts_at_ = now_;
        if (const auto counts = api::parse_world_counts(response)) {
            counts_ = counts;
            ++counts_serial_;
        }
        return;
    }
    case Call::prefs:
        if (handle_common(response, true)) return;
        if (const auto prefs = api::parse_online_prefs(response)) prefs_ = prefs;
        return;
    case Call::put_prefs: {
        if (handle_common(response, true)) return;
        const auto prefs = api::parse_online_prefs(response);
        if (!response.ok()) {
            error_ = problem();
            return;
        }
        if (prefs) prefs_ = prefs;
        set_screen(root());
        history_.clear();
        info_text_ = "Settings saved. They count from your next Play.";
        return;
    }
    case Call::enter: {
        if (handle_common(response, true)) {
            start_ = false;
            return;
        }
        const auto entry = api::parse_world_entry(response);
        start_ = false;
        if (!entry) {
            const auto p = api::problem_of(response);
            set_screen(Screen::home);
            error_ = p.code == "account_inactive" ? "Your account can't play online right now." : p.text;
            return;
        }
        ready_ = LobbyWorld{*entry, account_, token_, entry->relay_port ? entry->relay_port : status_.relay_port};
        prefs_ = entry->prefs;
        busy_text_ = "Entering the server...";
        return;
    }
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
    remembers_login_ = remembered_;
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
    remembers_login_ = !read_account(config_.folder).empty();
    info_.reset();
    character_.reset();
    prefs_.reset();
    start_ = false;
    ready_.reset();
    if (!in_world()) {
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
        set_screen(logged_in() ? Screen::home : Screen::welcome);
        if (logged_in()) refresh_world();
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
    if (busy() || !logged_in() || in_world()) return;
    send(Call::logout, api::logout_request(), "Logging out...");
}

void OnlineLobby::logout_everywhere() {
    if (busy() || !logged_in() || in_world()) return;
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

void OnlineLobby::reset_character(const std::string& current_password) {
    error_.clear();
    if (busy() || !logged_in()) return;
    if (in_world()) {
        error_ = "Go offline first.";
        return;
    }
    if (current_password.empty()) {
        error_ = "Enter your password to confirm.";
        return;
    }
    send(Call::reset_character, character_api::reset_request(current_password), "Resetting the online character...");
}

void OnlineLobby::refresh_account() {
    if (!logged_in()) return;
    if (!in_flight(Call::me)) send(Call::me, api::me_request());
}

void OnlineLobby::refresh_world() {
    if (gate_ != Gate::open || in_flight(Call::world)) return;
    counts_at_ = now_;
    send(Call::world, api::world_counts_request());
}

void OnlineLobby::refresh_prefs() {
    if (!logged_in() || in_flight(Call::prefs)) return;
    send(Call::prefs, api::online_prefs_request());
}

void OnlineLobby::play() {
    error_.clear();
    if (busy()) return;
    if (!can_play()) {
        error_ = !config_.refusal.empty() ? config_.refusal : gate_ != Gate::open ? gate_text() : "Log in first.";
        return;
    }
    if (in_world()) return;
    start_ = true;
    if (offline_game_) go(Screen::confirm);
    else start_request();
}

void OnlineLobby::save_settings(const api::OnlinePrefs& prefs) {
    error_.clear();
    if (busy() || !logged_in()) return;
    const auto& s = prefs.settings;
    if (s.camera_mode < 0 || s.camera_mode > 1 || s.light_mode < 0 || s.light_mode > 1 || s.rand_enemies < 0 || s.rand_enemies > 2 ||
        s.rand_zones < 0 || s.rand_zones > 2) {
        error_ = "A setting is out of range.";
        return;
    }
    send(Call::put_prefs, api::put_online_prefs_request(prefs), "Saving the settings...");
}

void OnlineLobby::confirm_start() {
    if (screen_ != Screen::confirm || !start_) return;
    start_request();
}

void OnlineLobby::cancel_start() {
    start_ = false;
    back();
}

void OnlineLobby::start_request() {
    if (!start_) return;
    set_screen(Screen::starting);
    error_.clear();
    info_text_.clear();
    send(Call::enter, api::world_enter_request(config_.box, config_.protocol, config_.data_hash, config_.client_version), "Entering the server...");
}

std::optional<LobbyWorld> OnlineLobby::take_ready() {
    if (!ready_ || ready_->entry.ticket.empty()) return std::nullopt;
    auto world = std::move(ready_);
    ready_.reset();
    start_ = false;
    return world;
}

void OnlineLobby::session_started() {
    busy_text_.clear();
    playing_ = true;
    set_screen(Screen::playing);
    error_.clear();
    info_text_.clear();
    refresh_world();
}

void OnlineLobby::session_failed(const std::string& error) {
    // The server keeps the world session until it hears of the leave.
    if (logged_in()) send(Call::leave, api::world_leave_request());
    playing_ = false;
    ready_.reset();
    start_ = false;
    set_screen(logged_in() ? Screen::home : Screen::welcome);
    error_ = error;
    refresh_world();
}

void OnlineLobby::session_ended(const std::string& reason) {
    playing_ = false;
    leave_requested_ = false;
    set_screen(logged_in() ? Screen::home : Screen::welcome);
    error_.clear();
    info_text_ = tidy_reason(reason);
    refresh_world();
    // (The online character changed while playing.)
    if (logged_in() && !in_flight(Call::character)) send(Call::character, api::character_request());
}

void OnlineLobby::adopt_world(const std::string& account, const std::string& token) {
    if (token_.empty()) {
        account_ = account;
        token_ = token;
        http_->set_token(token_);
    }
    session_started();
}

} // namespace dw4::game::online
