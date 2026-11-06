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

int visibility_index(const std::string& name) {
    if (name == "friends") return 1;
    if (name == "private") return 2;
    return 0;
}
const char* visibility_key(int index) { return index == 1 ? "friends" : index == 2 ? "private" : "public"; }

namespace {
std::string entry_body(int box, int protocol, std::uint64_t data_hash, const std::string& client_version) {
    std::string body = "{";
    if (box > 0) body += "\"box\":" + std::to_string(box) + ",";
    return body + "\"protocol\":" + std::to_string(protocol) + ",\"dataHash\":" + std::to_string(data_hash) +
           ",\"clientVersion\":" + json::quote(client_version) + "}";
}
// The settings of the online preferences (no difficulty: the box's).
std::string prefs_settings_json(const RoomSettings& s) {
    const bool on = s.rand_seed != 0;
    return "\"cameraMode\":" + std::to_string(s.camera_mode) + ",\"lightMode\":" + std::to_string(s.light_mode) +
           ",\"fastLoading\":" + (s.fast_loading ? "true" : "false") + ",\"randomizer\":{\"enabled\":" + (on ? "true" : "false") +
           ",\"seed\":" + std::to_string(s.rand_seed) + ",\"drops\":" + (s.rand_drops ? "true" : "false") +
           ",\"enemies\":" + std::to_string(s.rand_enemies) + ",\"zones\":" + std::to_string(s.rand_zones) + "}";
}
OnlinePrefs prefs_of(const json::Value& prefs, const json::Value& settings) {
    OnlinePrefs p;
    if (const auto v = prefs["defaultVisibility"].str()) p.default_visibility = visibility_key(visibility_index(*v));
    if (const auto s = settings_from_json(settings)) p.settings = *s;
    p.settings.difficulty = 0;
    if (const auto box = prefs["lastBox"].i64(); box && *box >= 1 && *box <= 4) p.last_box = int(*box);
    return p;
}
} // namespace

HttpRequest world_enter_request(int box, int protocol, std::uint64_t data_hash, const std::string& client_version) {
    return post("/world/enter", entry_body(box, protocol, data_hash, client_version));
}
HttpRequest world_ticket_request(int protocol, std::uint64_t data_hash, const std::string& client_version) {
    return post("/world/ticket", entry_body(0, protocol, data_hash, client_version));
}
HttpRequest world_leave_request() { return post("/world/leave", ""); }
HttpRequest world_counts_request() {
    HttpRequest r;
    r.path = "/world";
    r.authorized = false;
    return r;
}
HttpRequest online_prefs_request() {
    HttpRequest r;
    r.path = "/me/online-prefs";
    return r;
}
HttpRequest put_online_prefs_request(const OnlinePrefs& p) {
    auto r = post("/me/online-prefs", "{\"defaultVisibility\":" + json::quote(visibility_key(visibility_index(p.default_visibility))) + "," +
                                          prefs_settings_json(p.settings) + "}");
    r.method = "PUT";
    return r;
}

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

std::optional<WorldEntry> parse_world_entry(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    WorldEntry e;
    e.ticket = (*v)["relayTicket"].str().value_or((*v)["ticket"].str().value_or(""));
    if (e.ticket.empty()) return std::nullopt;
    e.relay_host = (*v)["relay"]["host"].str().value_or("");
    if (const auto port = (*v)["relay"]["port"].i64(); port && *port > 0 && *port < 65536) e.relay_port = int(*port);
    e.protocol = int((*v)["protocol"].i64().value_or(0));
    e.role = (*v)["role"].str().value_or("player");
    e.relay_role = (*v)["relayRole"].str().value_or("member");
    if (const auto box = (*v)["box"].i64(); box && *box >= 1 && *box <= 4) e.box = int(*box);
    e.prefs = prefs_of((*v)["prefs"], (*v)["settings"]);
    return e;
}

std::optional<WorldCounts> parse_world_counts(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    WorldCounts c;
    c.players = int((*v)["players"].i64().value_or((*v)["playersOnline"].i64().value_or(0)));
    c.instances = int((*v)["instances"].i64().value_or(0));
    return c;
}

std::optional<OnlinePrefs> parse_online_prefs(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    return prefs_of(*v, *v);
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
