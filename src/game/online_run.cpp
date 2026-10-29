#include "online_run.hpp"

#include "online/room_settings.hpp"
#include "online/token_store.hpp"
#include "online/transport.hpp"

#include <cstdlib>
#include <iostream>

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
        if (http.wait(http.submit(me)).ok()) room.token = *token;
        else online::forget_token(token_folder, options.user);
    }
    if (room.token.empty()) {
        const char* password = std::getenv("DW4_ONLINE_PASSWORD");
        if (!password || !*password) {
            error = "No remembered login for " + options.user + " (set DW4_ONLINE_PASSWORD for a development login).";
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
    : http_(std::move(http)), room_(std::move(room)), link_(std::move(hooks), room_.settings) {
    online::SessionConfig config;
    config.ticket = room_.ticket;
    config.protocol = kOnlineProtocol;
    config.data_hash = data_hash;
    config.settings_hash = online::settings_hash(room_.settings, kOnlineProtocol, data_hash);
    config.own_name = room_.account;
    config.rate = rate;
    config.trace = std::getenv("DW4_ONLINE_TRACE") != nullptr;
    session_ = std::make_unique<online::Session>(link_, config);
}

bool OnlineRun::connect(double now, std::string& error) {
    auto transport = online::connect_tcp(room_.endpoint.host, room_.relay_port, 5000, error);
    if (!transport) return false;
    session_->attach(std::move(transport), now);
    return true;
}

void OnlineRun::pass(double now) {
    session_->poll(now);
    if (left_ || session_->closed() || !session_->wants_transport()) return;
    // Reconnect: a new one-time ticket, then the relay again (6.10).
    if (ticket_request_ == 0 && now >= next_reconnect_) ticket_request_ = http_->submit(online::api::ticket_request(room_.code));
    while (auto done = http_->poll()) {
        if (done->first != ticket_request_) continue;
        ticket_request_ = 0;
        next_reconnect_ = now + 2.0;
        const auto ticket = online::api::parse_ticket(done->second);
        if (!ticket) continue;
        std::string error;
        if (auto transport = online::connect_tcp(room_.endpoint.host, room_.relay_port, 2000, error))
            session_->attach(std::move(transport), now, *ticket);
    }
}

void OnlineRun::leave() {
    if (left_) return;
    left_ = true;
    session_->leave();
    http_->wait(http_->submit(online::api::leave_request(room_.code)));
}

} // namespace dw4::game
