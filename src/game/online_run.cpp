#include "online_run.hpp"

#include "online/host_priority.hpp"

#include "online/room_settings.hpp"
#include "online/token_store.hpp"
#include "online/transport.hpp"

#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <tuple>

namespace dw4::game {

std::optional<OnlineRoom> online_enter_room(online::HttpWorker& http, const OnlineOptions& options, std::uint64_t data_hash,
                                            const std::string& client_version, const std::filesystem::path& token_folder, std::string& error) {
    namespace api = online::api;
    OnlineRoom room;
    room.endpoint = http.endpoint();
    const auto server = room.endpoint.host + ":" + std::to_string(room.endpoint.port);
    auto status_response = http.wait(http.submit(api::status_request()));
    const auto status = api::parse_status(status_response);
    if (!status) {
        error = "The server does not answer (" + api::problem_text(status_response) + ").";
        return std::nullopt;
    }
    if (status->maintenance) {
        error = "The server is in maintenance." + (status->motd.empty() ? std::string() : " " + status->motd);
        return std::nullopt;
    }
    if (status->minimum_protocol > int(kOnlineProtocol)) {
        error = "Update required: the server needs a newer version of the game.";
        return std::nullopt;
    }
    room.relay_port = status->relay_port;
    // The remembered token first, else a password login.
    room.account = options.user;
    if (const auto token = online::load_token(token_folder, server, options.user)) {
        http.set_token(*token);
        online::HttpRequest me;
        me.path = "/me";
        const auto answer = http.wait(http.submit(me));
        if (answer.ok()) room.token = *token;
        // Only a refused token is forgotten; a network error keeps it for the next start.
        else if (answer.status == 401 || answer.status == 403) online::forget_token(token_folder, options.user);
        else {
            error = "The server did not answer the login check: " + api::problem_text(answer);
            return std::nullopt;
        }
    }
    if (room.token.empty()) {
        const char* password = std::getenv("DW4_ONLINE_PASSWORD");
        if (!password || !*password) {
            // (Round S1 has no login screen yet: the password comes from the environment in every build.)
            error = "No remembered login for " + options.user + " (set DW4_ONLINE_PASSWORD to log in).";
            return std::nullopt;
        }
        const auto response = http.wait(http.submit(api::login_request(options.user, password, client_version, int(kOnlineProtocol))));
        const auto login = api::parse_login(response);
        if (!login) {
            error = "Login failed: " + api::problem_text(response);
            return std::nullopt;
        }
        room.token = login->token;
        if (!login->account.empty()) room.account = login->account;
        http.set_token(room.token);
        if (!online::save_token(token_folder, server, options.user, room.token)) std::cout << "online: the login could not be remembered\n";
    }
    online::HttpResponse response;
    if (!options.create.empty()) {
        api::CreateRoom create;
        create.name = options.create;
        create.max_players = options.max_players;
        create.settings = options.settings;
        response = http.wait(http.submit(api::create_room_request(create, int(kOnlineProtocol), data_hash, client_version)));
    } else {
        response = http.wait(http.submit(api::join_room_request(options.join, "", int(kOnlineProtocol), data_hash, client_version)));
    }
    const auto joined = api::parse_room(response);
    if (!joined) {
        error = (options.create.empty() ? "Joining the room failed: " : "Opening the room failed: ") + api::problem_text(response);
        return std::nullopt;
    }
    room.code = joined->code;
    room.room_name = joined->name;
    room.settings = joined->settings;
    room.member_id = joined->member_id;
    room.max_players = joined->max_players;
    if (joined->relay_port) room.relay_port = joined->relay_port;
    room.ticket = joined->ticket;
    if (room.ticket.empty()) {
        const auto ticket_response = http.wait(http.submit(api::ticket_request(room.code)));
        const auto ticket = api::parse_ticket(ticket_response);
        if (!ticket) {
            error = "No relay ticket: " + api::problem_text(ticket_response);
            return std::nullopt;
        }
        room.ticket = *ticket;
    }
    return room;
}

OnlineRun::OnlineRun(std::unique_ptr<online::HttpWorker> http, OnlineRoom room, online::EngineHooks hooks, std::uint64_t data_hash,
                     std::uint32_t rate)
    : http_(std::move(http)), room_(std::move(room)), engine_(hooks.engine), link_(std::move(hooks), room_.settings) {
    online::SessionConfig config;
    config.ticket = room_.ticket;
    config.protocol = kOnlineProtocol;
    config.data_hash = data_hash;
    config.settings_hash = online::settings_hash(room_.settings, kOnlineProtocol, data_hash);
    config.own_name = room_.account;
    config.rate = rate;
    config.trace = std::getenv("DW4_ONLINE_TRACE") != nullptr;
    // DW4_ONLINE_RATE=N (test runs): this machine replays at most N steps per
    // second and reports that rate (a slower machine, SEAMLESS.md 6.6).
    if (const char* cap = std::getenv("DW4_ONLINE_RATE"); cap && std::atoi(cap) > 0) {
        config.max_replay_rate = std::uint32_t(std::atoi(cap));
        config.rate = std::min(config.rate, config.max_replay_rate);
    }
    session_ = std::make_unique<online::Session>(link_, config);
}

namespace {
// The relay port to connect to: the room's, or (development builds, the
// latency scenarios of tools/online_test) DW4_ONLINE_RELAY_PORT, a latency /
// jitter / loss proxy in front of the loopback relay.
int relay_port_for(int port) {
#ifdef DW4_ONLINE_DEV
    if (const char* text = std::getenv("DW4_ONLINE_RELAY_PORT"); text && std::atoi(text) > 0 && std::atoi(text) < 65536) return std::atoi(text);
#endif
    return port;
}
} // namespace

bool OnlineRun::connect(double now, std::string& error) {
    auto transport = online::connect_tcp(room_.endpoint.host, relay_port_for(room_.relay_port), 5000, error);
    if (!transport) return false;
    session_->attach(std::move(transport), now);
    // The relay's answer: by then it replaced any older connection of this account (a session another PC still holds in
    // the room ends without uploading). A refusal ends here; a slow relay is waited for in the main loop as before.
    if (!session_->await_hello(10.0) && session_->closed()) {
        error = session_->close_reason();
        return false;
    }
    return true;
}

void OnlineRun::pass(double now) {
    session_->poll(now);
    while (auto done = http_->poll()) dispatch(done->first, done->second, now);
    if (character_) character_->tick(now);
    if (left_ || session_->closed() || !session_->wants_transport()) return;
    // Reconnect: a new one-time ticket, then the relay again (6.10).
    if (ticket_request_ == 0 && now >= next_reconnect_) ticket_request_ = http_->submit(online::api::ticket_request(room_.code));
}

void OnlineRun::open_log(const std::filesystem::path& path, const std::string& client_version) {
    // A second game from the same folder (two games on one PC for one room)
    // has the file open already: it writes online-2.log (online-3.log, ...).
    bool opened = log_.open(path);
    for (int n = 2; !opened && n <= 4; ++n)
        opened = log_.open(path.parent_path() / (path.stem().string() + "-" + std::to_string(n) + path.extension().string()));
    if (!opened) {
        std::cout << "online: the online log " << path.string() << " cannot be written\n";
        return;
    }
    log_.line("---- online session: game %s, protocol %u, server %s:%d (%s), relay port %d, account %s, room '%s' (max %d players, "
              "difficulty %d)",
              client_version.c_str(), unsigned(kOnlineProtocol), room_.endpoint.host.c_str(), room_.endpoint.port,
              room_.endpoint.development ? "development" : "release", room_.relay_port, room_.account.c_str(), room_.room_name.c_str(),
              room_.max_players, room_.settings.difficulty);
    session_->set_log(&log_);
    status_request_ = http_->submit(online::api::status_request());
    // (A game in the background, such as the second of two on one PC, keeps its pace: online/host_priority.hpp.)
    log_.line("background throttling of this process: %s", online::keep_full_speed_in_background() ? "off" : "left to the system");
}

namespace {
const char* window_state(const OnlineRun::HostFacts& f) {
    if (!f.window) return "none";
    if (f.minimized) return "minimized";
    if (f.hidden) return "hidden";
    if (f.occluded) return "covered (occluded)";
    return f.focused ? "in front (focused)" : "in the background (not focused)";
}
} // namespace

void OnlineRun::host_pass(double now, const HostFacts& f) {
    auto& h = host_window_;
    if (h.since < 0) h.since = now;
    if (h.last >= 0) h.longest = std::max(h.longest, now - h.last);
    h.last = now;
    ++h.passes;
    if (f.presented) ++h.presents;
    const auto settings_of = [](const HostFacts& x) {
        return std::tuple(x.window, x.vsync, x.swap_interval, x.fps_limit, std::lround(x.refresh_hz * 100.0));
    };
    const auto window_of = [](const HostFacts& x) { return std::tuple(x.focused, x.minimized, x.occluded, x.hidden); };
    if (!host_logged_ || settings_of(*host_logged_) != settings_of(f)) {
        if (f.window)
            log_.line("host: vsync %s (swap interval %d), display %.3f Hz, frame limit %s", f.vsync ? "on" : "off", f.swap_interval, f.refresh_hz,
                      f.fps_limit > 0 ? (std::to_string(f.fps_limit) + " fps").c_str() : "off");
        else
            log_.line("host: no window (a run without pictures)");
    }
    if (f.window && (!host_logged_ || window_of(*host_logged_) != window_of(f))) log_.line("window: %s", window_state(f));
    host_logged_ = f;
    if (now - h.since >= 10.0) {
        // (A game at 60 pictures a second with vsync on a display of 60.000 Hz
        // sends its pads on a grid that beats against the relay's 59.94 Hz.)
        const double span = now - h.since;
        log_.line("host: %.2f passes/s, %.2f pictures/s, longest pass %.0f ms, window %s", double(h.passes) / span, double(h.presents) / span,
                  h.longest * 1000.0, window_state(f));
        h = HostWindow{now, now, 0, 0, 0};
    }
}

void OnlineRun::dispatch(std::uint64_t id, const online::HttpResponse& response, double now) {
    if (character_ && character_->on_response(id, response)) return;
    if (id == status_request_ && status_request_ != 0) {
        status_request_ = 0;
        if (const auto status = online::api::parse_status(response)) {
            log_.line("server: version %s, protocol %d (minimum %d), relay port %d%s", status->current_version.empty() ? "?" : status->current_version.c_str(),
                      status->current_protocol, status->minimum_protocol, status->relay_port,
                      status->current_protocol != 0 && status->current_protocol != int(kOnlineProtocol)
                          ? " - the server is set for another protocol than this game: update the server or the game"
                          : "");
        } else {
            log_.line("server: no status (%s)", online::api::problem_text(response).c_str());
        }
        return;
    }
    if (id != ticket_request_) return;
    ticket_request_ = 0;
    next_reconnect_ = now + 2.0;
    const auto ticket = online::api::parse_ticket(response);
    if (!ticket || left_ || session_->closed() || !session_->wants_transport()) return;
    std::string error;
    if (auto transport = online::connect_tcp(room_.endpoint.host, relay_port_for(room_.relay_port), 2000, error)) {
        log_.line("reconnected to the relay");
        session_->attach(std::move(transport), now, *ticket);
    } else {
        log_.line("reconnect failed: %s", error.c_str());
    }
}

// ---------------------------------------------------------------------------
// The online character (docs/SEAMLESS.md 9)
// ---------------------------------------------------------------------------
online::character_api::Context OnlineRun::character_context() const {
    online::character_api::Context c;
    c.room_code = room_.code;
    // (The same numbers as the online achievements' reports: the own stage
    // instance, and the room from the relay's ROSTER or else the presence.)
    c.players = std::clamp(std::popcount(unsigned(session_->present())), 1, 4);
    c.room_players = std::clamp(std::max(int(session_->roster().size()), int(session_->remote().size()) + 1), 1, 10);
    c.randomizer = room_.settings.randomizer();
    c.difficulty = room_.settings.difficulty;
    c.protocol = int(kOnlineProtocol);
    c.client_version = client_version_;
    return c;
}

bool OnlineRun::load_character(const std::filesystem::path& online_folder, const std::string& client_version, std::string& error) {
    client_version_ = client_version;
    online::CharacterSync::Config config;
    config.card = online::OnlineCard(online::online_card_folder(online_folder, room_.account));
    config.context = character_context();
    config.submit = [http = http_.get()](online::HttpRequest request) { return http->submit(std::move(request)); };
    config.trace = std::getenv("DW4_ONLINE_TRACE") != nullptr;
    character_ = std::make_unique<online::CharacterSync>(std::move(config));
    const auto response = http_->wait(http_->submit(online::character_api::get_request()));
    if (!character_->enter(response, error)) {
        if (response.status != 0 && response.status != 200 && response.status != 404) error += " (" + online::api::problem_text(response) + ")";
        character_.reset();
        return false;
    }
    return true;
}

save::CardStore OnlineRun::character_card() const {
    return character_ ? character_->card().store() : save::CardStore{};
}

void OnlineRun::save_character(online::CharacterSync::Why why) {
    if (!character_) return;
    character_->set_context(character_context());
    if (const auto record = link_.character_record(session_->own_slot())) character_->auto_save(*record, why);
}

void OnlineRun::after_live_step(double now) {
    // The title starts a game: the confirmed room story replaces the
    // character's FLAG / FLAGFOLDER / COUNTER before gameStart.lua runs
    // (GameEngineLink applies it with the room's modes on this very step).
    // Without a confirmed story yet the character's own stays until the
    // first barrier's merge.
    // (Every Game Start of the session: after "Continue? No" the title
    // starts the next one with the room story and modes again.)
    if (link_.game_start()) {
        const auto& story = session_->confirmed_story();
        link_.set_room_story(story);
        if (story) std::cout << "online: the game starts with the room story (revision " << story->revision << ")\n";
        else std::cout << "online: the game starts without a room story yet (the character's own until the first barrier)\n";
        if (character_) character_->game_started();
    }
    link_.after_live_step();
    if (!character_) return;
    auto& engine = engine_();
    // Play time without loads, as the server compares it with real time.
    if (link_.game_started() && engine.presenting() && !engine.loading()) character_->add_play_time(engine::kFieldSeconds);
    // The barrier tick (the step that executed NEXTSTAGE, SEAMLESS.md 6.7):
    // the state the member carries into the next stage is saved, so a crash
    // loses at most the stage it happens in.
    const bool pending = link_.pending_stage().has_value();
    if (pending && !pending_before_) save_character(online::CharacterSync::Why::stage_change);
    pending_before_ = pending;
    character_->tick(now);
}

void OnlineRun::port_events(const std::vector<engine::PortEvent>& events) {
    if (!character_) return;
    for (const auto& e : events) {
        if (e.kind == engine::PortEvent::Kind::card_written && e.slot == session_->own_slot() && e.value == 0 && e.extra >= 0) {
            character_->set_context(character_context());
            character_->card_written(e.extra);
        }
        // A character the character select made new or called from the card
        // (the title is the member's own solo instance: its slot is this
        // machine's); the Game Start decides from the last one.
        if (e.kind == engine::PortEvent::Kind::new_character && e.slot == session_->own_slot()) character_->new_game(std::uint32_t(e.value));
        if (e.kind == engine::PortEvent::Kind::character_called && e.slot == session_->own_slot())
            character_->character_called(e.extra, std::uint32_t(e.value));
    }
}

void OnlineRun::replaced() {
    if (character_) character_->replaced();
}

void OnlineRun::save_on_leave(double seconds) {
    if (!character_ || saved_on_leave_) return;
    saved_on_leave_ = true;
    save_character(online::CharacterSync::Why::leave);
    const auto clock = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const double until = clock() + seconds;
    while (!character_->idle() && clock() < until) {
        while (auto done = http_->poll()) dispatch(done->first, done->second, clock());
        character_->tick(clock());
        if (!character_->idle()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!character_->idle())
        std::cout << "online: the online character's last save did not reach the server; the next room entry uploads it\n";
}

void OnlineRun::leave(double save_seconds) {
    if (left_) return;
    save_on_leave(save_seconds);
    left_ = true;
    {
        // (The latency of the pads that waited for the network: solo ticks run in the step of their sample.)
        const auto& t = session_->stats().total();
        const auto& n = session_->stats().delayed_total();
        log_.line("leaving the room: %llu own pads ran (%llu with others), %llu not sealed as sent, latency with others avg %.0f ms p95 %.0f ms "
                  "max %.0f ms, input delay %d, reconnects %d, resyncs %d",
                  static_cast<unsigned long long>(t.samples), static_cast<unsigned long long>(n.samples), static_cast<unsigned long long>(t.dropped),
                  n.exec_avg() * 1000.0, n.exec_quantile(0.95) * 1000.0, n.exec_max * 1000.0, session_->delay(), session_->net().reconnects,
                  session_->net().resyncs);
    }
    session_->leave();
    http_->wait(http_->submit(online::api::leave_request(room_.code)));
}

} // namespace dw4::game
