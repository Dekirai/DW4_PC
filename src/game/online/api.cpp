#include "api.hpp"

#include "json.hpp"

#include <cctype>

namespace dw4::game::online::api {

namespace {
HttpRequest post(std::string path, std::string body) {
    HttpRequest r;
    r.method = "POST";
    r.path = std::move(path);
    r.body = std::move(body);
    if (!r.body.empty()) r.content_type = "application/json";
    return r;
}
std::optional<json::Value> body_of(const HttpResponse& response) {
    if (!response.ok()) return std::nullopt;
    return json::parse(response.body);
}
} // namespace

HttpRequest status_request() {
    HttpRequest r;
    r.path = "/status";
    r.authorized = false;
    return r;
}

HttpRequest login_request(const std::string& user, const std::string& password, const std::string& client_version, int protocol) {
    auto r = post("/auth/login", "{\"username\":" + json::quote(user) + ",\"password\":" + json::quote(password) +
                                     ",\"deviceName\":\"DW4_PC\",\"clientVersion\":" + json::quote(client_version) +
                                     ",\"protocol\":" + std::to_string(protocol) + "}");
    r.authorized = false;
    return r;
}

HttpRequest create_room_request(const CreateRoom& room, int protocol, std::uint64_t data_hash, const std::string& client_version) {
    std::string body = "{\"name\":" + json::quote(room.name);
    if (!room.password.empty()) body += ",\"password\":" + json::quote(room.password);
    body += ",\"maxPlayers\":" + std::to_string(room.max_players) + ",\"protocol\":" + std::to_string(protocol) +
            ",\"dataHash\":" + std::to_string(data_hash) + ",\"clientVersion\":" + json::quote(client_version) +
            ",\"settings\":" + settings_json(room.settings) + "}";
    return post("/rooms", std::move(body));
}

HttpRequest join_room_request(const std::string& code, const std::string& password, int protocol, std::uint64_t data_hash,
                              const std::string& client_version) {
    std::string body = "{";
    if (!password.empty()) body += "\"password\":" + json::quote(password) + ",";
    body += "\"protocol\":" + std::to_string(protocol) + ",\"dataHash\":" + std::to_string(data_hash) +
            ",\"clientVersion\":" + json::quote(client_version) + "}";
    return post("/rooms/" + code + "/join", std::move(body));
}

HttpRequest ticket_request(const std::string& code) { return post("/rooms/" + code + "/ticket", ""); }
HttpRequest leave_request(const std::string& code) { return post("/rooms/" + code + "/leave", ""); }

std::optional<ServerStatus> parse_status(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    ServerStatus s;
    const auto& client = (*v)["client"];
    s.current_protocol = int(client["currentProtocol"].i64().value_or(0));
    s.minimum_protocol = int(client["minimumProtocol"].i64().value_or(0));
    s.current_version = client["currentVersion"].str().value_or("");
    s.minimum_version = client["minimumVersion"].str().value_or("");
    if (const auto registration = (*v)["registration"].str()) s.registration_open = *registration == "open";
    s.maintenance = (*v)["maintenance"].flag().value_or(false);
    s.motd = (*v)["motd"].str().value_or("");
    if (const auto port = (*v)["relay"]["port"].i64(); port && *port > 0 && *port < 65536) s.relay_port = int(*port);
    return s;
}

std::optional<Login> parse_login(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    const auto token = (*v)["token"].str();
    if (!token || token->empty()) return std::nullopt;
    Login l;
    l.token = *token;
    const auto& account = (*v)["account"];
    l.account = account["username"].str().value_or(account.str().value_or(""));
    return l;
}

std::optional<RoomJoined> parse_room(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    RoomJoined r;
    const auto& room = (*v)["room"];
    const auto code = room["code"].str();
    if (!code) return std::nullopt;
    r.code = *code;
    r.name = room["name"].str().value_or("");
    r.max_players = int(room["maxPlayers"].i64().value_or(0));
    r.owner = room["owner"].str().value_or("");
    const auto settings = settings_from_json((*v)["settings"]);
    if (!settings) return std::nullopt;
    r.settings = *settings;
    r.ticket = (*v)["relayTicket"].str().value_or((*v)["ticket"].str().value_or((*v)["relay"]["ticket"].str().value_or("")));
    r.member_id = std::uint32_t((*v)["you"]["accountId"].u64().value_or(0));
    if (const auto port = (*v)["relay"]["port"].i64(); port && *port > 0 && *port < 65536) r.relay_port = int(*port);
    return r;
}

bool owns_room(const RoomJoined& joined, const std::string& account, bool created) {
    if (created) return true;
    if (joined.owner.empty() || joined.owner.size() != account.size()) return false;
    for (std::size_t i = 0; i < account.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(joined.owner[i])) != std::tolower(static_cast<unsigned char>(account[i]))) return false;
    return true;
}

std::optional<std::string> parse_ticket(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    auto t = (*v)["relayTicket"].str();
    if (!t || t->empty()) t = (*v)["ticket"].str();
    if (!t || t->empty()) return std::nullopt;
    return t;
}

std::string problem_text(const HttpResponse& response) {
    if (response.status == 0) return response.error.empty() ? std::string("no answer from the server") : response.error;
    if (const auto v = json::parse(response.body)) {
        for (const char* key : {"detail", "title", "code"})
            if (const auto text = (*v)[key].str(); text && !text->empty()) return *text;
    }
    return "HTTP " + std::to_string(response.status);
}

} // namespace dw4::game::online::api
