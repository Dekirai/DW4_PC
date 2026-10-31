#include "lobby_api.hpp"

#include "json.hpp"

#include <algorithm>
#include <set>

namespace dw4::game::online::api {

namespace {
HttpRequest request(std::string method, std::string path, std::string body = {}) {
    HttpRequest r;
    r.method = std::move(method);
    r.path = std::move(path);
    r.body = std::move(body);
    if (!r.body.empty()) r.content_type = "application/json";
    return r;
}
std::optional<json::Value> body_of(const HttpResponse& response) {
    if (!response.ok()) return std::nullopt;
    return json::parse(response.body);
}
std::string text_of(const json::Value& v) { return v.str().value_or(""); }
int int_of(const json::Value& v, int fallback = 0) { return int(v.i64().value_or(fallback)); }
bool flag_of(const json::Value& v) { return v.flag().value_or(false); }

RoomListing listing_of(const json::Value& r) {
    RoomListing l;
    l.code = text_of(r["code"]);
    l.name = text_of(r["name"]);
    l.owner = text_of(r["owner"]);
    l.owner_present = flag_of(r["ownerPresent"]);
    l.story_frozen = flag_of(r["storyFrozen"]);
    l.has_password = flag_of(r["hasPassword"]);
    l.players = int_of(r["players"]);
    l.max_players = int_of(r["maxPlayers"]);
    l.protocol = int_of(r["protocol"]);
    l.data_hash = r["dataHash"].u64().value_or(0);
    l.client_version = text_of(r["clientVersion"]);
    l.ranked = flag_of(r["ranked"]);
    l.randomizer = flag_of(r["randomizer"]);
    if (const auto d = r["difficulty"].i64()) l.difficulty = int(*d);
    l.instances = int_of(r["instances"]);
    if (const auto policy = text_of(r["storyPolicy"]); !policy.empty()) l.story_policy = policy;
    return l;
}

RoomMemberInfo member_of(const json::Value& m) {
    RoomMemberInfo info;
    info.account_id = m["accountId"].u64().value_or(0);
    info.name = text_of(m["username"]);
    info.slot = int_of(m["slot"]);
    info.owner = flag_of(m["owner"]);
    if (const auto inst = m["instance"].u64(); inst && *inst <= 0xFFFFFFFFull) info.instance = std::uint32_t(*inst);
    return info;
}

// The server's rules (DW4Online.Domain UsernamePolicy) for a typed name.
bool name_shape(const std::string& user) {
    if (user.size() < 3 || user.size() > 20) return false;
    return std::all_of(user.begin(), user.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
std::string lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}

struct Version {
    int part[3]{};
};
std::optional<Version> read_version(const std::string& text) {
    const auto core = text.substr(0, text.find_first_of("-+"));
    if (core.empty()) return std::nullopt;
    Version v;
    int index = 0;
    std::size_t at = 0;
    while (true) {
        const auto dot = core.find('.', at);
        const auto piece = core.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
        if (piece.empty() || piece.size() > 9 || !std::all_of(piece.begin(), piece.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        if (index > 2) return std::nullopt;
        v.part[index++] = std::stoi(piece);
        if (dot == std::string::npos) break;
        at = dot + 1;
    }
    if (index < 2) return std::nullopt;
    return v;
}
} // namespace

Problem problem_of(const HttpResponse& response) {
    Problem p;
    p.status = response.status;
    if (response.status == 0) {
        p.text = response.pin_mismatch ? "The server's certificate is not the expected one; nothing was sent."
                 : response.error.empty() ? std::string("The server does not answer.")
                                          : "The server does not answer (" + response.error + ").";
        return p;
    }
    if (const auto v = json::parse(response.body)) {
        p.code = text_of((*v)["code"]);
        for (const char* key : {"detail", "title"})
            if (const auto text = (*v)[key].str(); text && !text->empty()) {
                p.text = *text;
                break;
            }
    }
    if (p.text.empty()) p.text = response.ok() ? std::string() : "The server answered HTTP " + std::to_string(response.status) + ".";
    return p;
}

int unlocked_difficulty(const CharacterInfo& character) {
    if (!character.exists) return 0;
    if (character.clears & 2) return 2;
    if (character.clears & 1) return 1;
    return 0;
}

HttpRequest register_request(const std::string& user, const std::string& password, const std::string& email) {
    std::string body = "{\"username\":" + json::quote(user) + ",\"password\":" + json::quote(password);
    if (!email.empty()) body += ",\"email\":" + json::quote(email);
    body += ",\"deviceName\":\"DW4_PC\"}";
    auto r = request("POST", "/auth/register", std::move(body));
    r.authorized = false;
    return r;
}

HttpRequest recover_request(const std::string& user, const std::string& code, const std::string& new_password) {
    auto r = request("POST", "/auth/recover",
                     "{\"username\":" + json::quote(user) + ",\"recoveryCode\":" + json::quote(code) + ",\"newPassword\":" + json::quote(new_password) +
                         "}");
    r.authorized = false;
    return r;
}

HttpRequest logout_request() { return request("POST", "/auth/logout"); }
HttpRequest logout_all_request() { return request("POST", "/auth/logout-all"); }

HttpRequest change_password_request(const std::string& current, const std::string& next) {
    return request("POST", "/auth/password", "{\"currentPassword\":" + json::quote(current) + ",\"newPassword\":" + json::quote(next) + "}");
}

HttpRequest me_request() { return request("GET", "/me"); }
HttpRequest character_request() { return request("GET", "/online-character"); }

HttpRequest rooms_request() {
    // Every open room (the list shows the incompatible ones greyed out, so a
    // player sees why a friend's room cannot be joined).
    auto r = request("GET", "/rooms");
    r.authorized = false;
    return r;
}

HttpRequest room_state_request(const std::string& code) { return request("GET", "/rooms/" + code); }
HttpRequest kick_request(const std::string& code, const std::string& user) {
    return request("POST", "/rooms/" + code + "/kick", "{\"username\":" + json::quote(user) + "}");
}
HttpRequest close_room_request(const std::string& code) { return request("DELETE", "/rooms/" + code); }

std::optional<Registered> parse_registered(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    Registered r;
    r.token = text_of((*v)["token"]);
    r.recovery_code = text_of((*v)["recoveryCode"]);
    r.account = text_of((*v)["account"]["username"]);
    if (r.token.empty()) return std::nullopt;
    return r;
}

std::optional<std::string> parse_recovery_code(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    auto code = (*v)["recoveryCode"].str();
    if (!code || code->empty()) return std::nullopt;
    return code;
}

std::optional<AccountInfo> parse_account(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    AccountInfo a;
    a.id = (*v)["id"].u64().value_or(0);
    a.name = text_of((*v)["username"]);
    a.email = text_of((*v)["email"]);
    a.must_change_password = flag_of((*v)["mustChangePassword"]);
    a.created_at = text_of((*v)["createdAt"]);
    if (a.name.empty()) return std::nullopt;
    return a;
}

std::optional<CharacterInfo> parse_character(const HttpResponse& response) {
    if (response.status == 404) return CharacterInfo{};
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    CharacterInfo c;
    c.exists = true;
    c.revision = std::uint32_t((*v)["revision"].u64().value_or(0));
    c.play_seconds = (*v)["playSeconds"].i64().value_or(0);
    const auto& s = (*v)["summary"];
    c.name = text_of(s["playerName"]);
    c.digimon = text_of(s["digimon"]);
    c.level = int_of(s["level"]);
    c.clears = int_of(s["clears"]);
    return c;
}

std::optional<std::vector<RoomListing>> parse_rooms(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    const json::Value* list = v->kind == json::Value::Kind::array ? &*v : (*v).find("rooms");
    if (!list || list->kind != json::Value::Kind::array) return std::nullopt;
    std::vector<RoomListing> rooms;
    for (const auto& r : list->items) {
        auto l = listing_of(r);
        if (!l.code.empty()) rooms.push_back(std::move(l));
    }
    return rooms;
}

std::optional<RoomView> parse_room_view(const HttpResponse& response) {
    const auto v = body_of(response);
    if (!v) return std::nullopt;
    RoomView view;
    view.room = listing_of((*v)["room"]);
    if (view.room.code.empty()) return std::nullopt;
    const auto settings = settings_from_json((*v)["settings"]);
    if (!settings) return std::nullopt;
    view.settings = *settings;
    for (const auto& m : (*v)["members"].items) view.members.push_back(member_of(m));
    std::sort(view.members.begin(), view.members.end(), [](const auto& a, const auto& b) { return a.slot < b.slot; });
    for (const auto& i : (*v)["instances"].items) {
        InstanceInfo info;
        info.id = std::uint32_t(i["id"].u64().value_or(0));
        info.visit = std::uint32_t(i["visit"].u64().value_or(0));
        info.ticks = std::uint32_t(i["ticks"].u64().value_or(0));
        info.stage = text_of(i["stage"]);
        info.mode = text_of(i["mode"]);
        info.departing = flag_of(i["departing"]);
        info.boss_lock = flag_of(i["bossLock"]);
        info.joinable = flag_of(i["joinable"]);
        for (const auto& m : i["members"].items) {
            InstanceMemberInfo member;
            member.account_id = m["accountId"].u64().value_or(0);
            member.name = text_of(m["username"]);
            member.slot = int_of(m["slot"]);
            member.connected = flag_of(m["connected"]);
            member.lagging = flag_of(m["lagging"]);
            info.members.push_back(std::move(member));
        }
        view.instances.push_back(std::move(info));
    }
    view.you = member_of((*v)["you"]);
    return view;
}

std::string check_user_name(const std::string& user) {
    if (!name_shape(user)) return "User names are 3 to 20 characters: letters, digits, '_' and '-'.";
    static const std::set<std::string> reserved{"admin", "administrator", "root",     "system", "moderator", "mod",
                                                "support", "staff",       "server",   "dw4",    "dw4online", "dw4_pc",
                                                "official", "null",       "undefined", "anonymous", "guest", "host"};
    const auto name = lower(user);
    if (reserved.contains(name) || name.rfind("deleted_", 0) == 0) return "This user name is reserved.";
    return {};
}

std::string check_password(const std::string& password, const std::string& user) {
    if (password.size() < 10) return "Passwords need at least 10 characters.";
    if (password.size() > 128) return "Passwords have at most 128 characters.";
    if (!user.empty() && lower(password) == lower(user)) return "The password must not be the user name.";
    if (std::set<char>(password.begin(), password.end()).size() < 4) return "This password is too easy to guess.";
    return {};
}

std::string check_room_name(const std::string& name) {
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos) return "The room needs a name.";
    if (name.size() > 32) return "Room names have at most 32 characters.";
    return {};
}

std::string check_room_password(const std::string& password) {
    if (password.empty()) return {};
    if (password.size() < 4 || password.size() > 64) return "Room passwords are 4 to 64 characters (or none).";
    return {};
}

bool version_at_least(const std::string& version, const std::string& minimum) {
    const auto need = read_version(minimum);
    if (!need) return true;
    const auto own = read_version(version);
    if (!own) return false;
    for (int i = 0; i < 3; ++i)
        if (own->part[i] != need->part[i]) return own->part[i] > need->part[i];
    return true;
}

} // namespace dw4::game::online::api
