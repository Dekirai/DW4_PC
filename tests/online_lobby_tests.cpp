// The account and room lobby without a server (docs/SEAMLESS.md 10): the
// REST calls' bodies and parsers (lobby_api.hpp), the local checks, the
// version gate, and the lobby's transitions over a scripted HTTP queue: the
// remembered login (DPAPI token in a temporary folder), register with the
// recovery code, recover, log out, the room list, create (difficulty up to
// the online character's clears), join with the password prompt, the
// confirmation that ends an offline game, the ready room the host starts,
// the room state, kick, close, and the answers that end a login (401) or
// close the gate (426).
#include "online/json.hpp"
#include "online/lobby.hpp"
#include "online/lobby_api.hpp"
#include "online/token_store.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game::online;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

HttpResponse answer(int status, std::string body = {}) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}
HttpResponse problem(int status, const std::string& code, const std::string& title) {
    return answer(status, "{\"type\":\"urn:dw4online:problem:" + code + "\",\"title\":" + json::quote(title) + ",\"status\":" +
                              std::to_string(status) + ",\"code\":\"" + code + "\"}");
}

// Answers each request at once from a handler; records what was sent.
class FakeQueue final : public HttpQueue {
public:
    std::function<HttpResponse(const HttpRequest&, const std::string& token)> handler;
    std::vector<std::pair<HttpRequest, std::string>> sent;
    std::string token;
    void set_token(std::string t) override { token = std::move(t); }
    std::uint64_t submit(HttpRequest request) override {
        sent.emplace_back(request, request.authorized ? token : std::string());
        done.emplace_back(++next, handler(request, request.authorized ? token : std::string()));
        return next;
    }
    std::optional<std::pair<std::uint64_t, HttpResponse>> poll() override {
        if (done.empty()) return std::nullopt;
        auto d = done.front();
        done.pop_front();
        return d;
    }
    bool sent_path(const std::string& method, const std::string& path) const {
        for (const auto& [r, t] : sent)
            if (r.method == method && r.path == path) return true;
        return false;
    }
    std::deque<std::pair<std::uint64_t, HttpResponse>> done;
    std::uint64_t next = 0;
};

const char* kStatus = R"({"service":"DW4Online","api":1,"maintenance":false,"motd":"","registration":"open",
    "client":{"minimumVersion":"","currentVersion":"","minimumProtocol":0,"currentProtocol":44},
    "relay":{"host":"127.0.0.1","port":54210,"protocol":"DW4R2"}})";

std::string rooms_json() {
    return R"([{"code":"ABCD12","name":"Kim's room","owner":"kim","ownerPresent":true,"storyFrozen":false,"hasPassword":true,
               "players":2,"maxPlayers":4,"protocol":44,"dataHash":777,"clientVersion":"0.1.0","ranked":true,"randomizer":false,
               "difficulty":0,"instances":1,"createdAt":"2026-10-30T10:00:00Z"},
              {"code":"OLD999","name":"Old game","owner":"lee","ownerPresent":false,"storyFrozen":true,"hasPassword":false,
               "players":1,"maxPlayers":2,"protocol":43,"dataHash":777,"clientVersion":"0.0.9","ranked":false,"randomizer":true,
               "difficulty":null,"instances":0,"createdAt":"2026-10-30T11:00:00Z"},
              {"code":"OPEN01","name":"Open room","owner":"max","ownerPresent":true,"storyFrozen":false,"hasPassword":false,
               "players":1,"maxPlayers":10,"protocol":44,"dataHash":777,"clientVersion":"0.1.0","ranked":true,"randomizer":false,
               "difficulty":1,"instances":1,"createdAt":"2026-10-30T09:00:00Z"}])";
}

std::string room_state_json(const std::string& code, const std::string& name, const std::string& you, bool owner, int max_players) {
    return R"({"room":{"code":")" + code + R"(","name":)" + json::quote(name) + R"(,"owner":"kim","ownerPresent":true,"storyFrozen":false,
        "hasPassword":false,"players":2,"maxPlayers":)" + std::to_string(max_players) + R"(,"protocol":44,"dataHash":777,
        "clientVersion":"0.1.0","ranked":true,"randomizer":false,"difficulty":0,"instances":1,"createdAt":"2026-10-30T10:00:00Z"},
      "settings":{"difficulty":0,"cameraMode":1,"lightMode":0,"fastLoading":true},
      "settingsHash":"0123456789abcdef",
      "members":[{"accountId":7,"username":"lee","slot":1,"owner":false,"instance":12,"joinedAt":"2026-10-30T10:01:00Z"},
                 {"accountId":5,"username":"kim","slot":0,"owner":true,"instance":12,"joinedAt":"2026-10-30T10:00:00Z"}],
      "instances":[{"id":12,"stage":"stag0101","visit":3,"mode":"sealed","members":[
          {"accountId":5,"username":"kim","slot":0,"connected":true,"lagging":false},
          {"accountId":7,"username":"lee","slot":1,"connected":true,"lagging":true}],
        "departing":false,"bossLock":false,"joinable":true,"ticks":1234,"parent":null}],
      "you":{"accountId":)" + std::string(owner ? "5" : "7") + R"(,"username":")" + you + R"(","slot":)" + (owner ? "0" : "1") +
           R"(,"owner":)" + (owner ? "true" : "false") + R"(,"instance":12,"joinedAt":"2026-10-30T10:00:00Z"},
      "heartbeatSeconds":10,"relay":{"host":"127.0.0.1","port":54210,"protocol":"DW4R2"},"relayTicket":"TICKET-1"})";
}

void api_calls() {
    using namespace api;
    const auto reg = register_request("Kim_1", "a long password", "");
    check(reg.method == "POST" && reg.path == "/auth/register" && !reg.authorized, "register request");
    check(reg.body == R"({"username":"Kim_1","password":"a long password","deviceName":"DW4_PC"})", "register body without e-mail: " + reg.body);
    check(register_request("a", "b", "k@x.de").body.find(R"("email":"k@x.de")") != std::string::npos, "register body with e-mail");
    check(recover_request("kim", "CODE", "new password!").body ==
              R"({"username":"kim","recoveryCode":"CODE","newPassword":"new password!"})" && !recover_request("k", "c", "p").authorized,
          "recover body");
    check(logout_request().path == "/auth/logout" && logout_all_request().path == "/auth/logout-all" && logout_request().authorized, "logout");
    check(change_password_request("old one", "new one").body == R"({"currentPassword":"old one","newPassword":"new one"})", "password body");
    check(me_request().method == "GET" && me_request().path == "/me" && character_request().path == "/online-character", "me, character");
    check(rooms_request().path == "/rooms" && !rooms_request().authorized, "the room list is public");
    check(room_state_request("ABCD12").path == "/rooms/ABCD12", "room state");
    check(kick_request("ABCD12", "lee").body == R"({"username":"lee"})" && kick_request("ABCD12", "lee").path == "/rooms/ABCD12/kick", "kick");
    check(close_room_request("ABCD12").method == "DELETE" && close_room_request("ABCD12").path == "/rooms/ABCD12", "close");

    const auto r = parse_registered(answer(201, R"({"account":{"id":3,"username":"kim_1"},"recoveryCode":"AAAA-BBBB","token":"T","expiresAt":"x"})"));
    check(r && r->token == "T" && r->account == "kim_1" && r->recovery_code == "AAAA-BBBB", "registered");
    check(!parse_registered(problem(422, "weak_password", "Passwords need at least 10 characters.")), "a refused registration");
    check(parse_recovery_code(answer(200, R"({"recoveryCode":"NEW"})")) == std::string("NEW"), "recovery code");
    const auto a = parse_account(answer(200, R"({"id":9,"username":"kim","email":null,"role":"player","status":"active",
        "mustChangePassword":true,"profilePublic":false,"createdAt":"2026-10-01T00:00:00Z","lastLoginAt":null})"));
    check(a && a->id == 9 && a->name == "kim" && a->email.empty() && a->must_change_password, "account");
    const auto none = parse_character(problem(404, "no_online_character", "There is no online character yet."));
    check(none && !none->exists && unlocked_difficulty(*none) == 0, "no online character: Normal only");
    const auto c = parse_character(answer(200, R"({"revision":4,"record":"AA==","summary":{"unique":1,"digimon":"Agumon","speciesRow":0,
        "playerName":"KIM","level":31,"highestLevel":31,"hp":1,"maxHp":1,"mp":1,"maxMp":1,"bits":0,"bankBits":0,"formsReached":3,
        "cardsCollected":0,"devicesHeld":0,"clears":1},"playSeconds":36000,"flagged":false})"));
    check(c && c->exists && c->level == 31 && c->name == "KIM" && c->digimon == "Agumon" && c->play_seconds == 36000 && c->revision == 4,
          "character summary");
    check(unlocked_difficulty(*c) == 1, "a Normal clear unlocks Hard");
    auto hard = *c;
    hard.clears = 3;
    check(unlocked_difficulty(hard) == 2, "a Hard clear unlocks Very Hard");

    const auto rooms = parse_rooms(answer(200, rooms_json()));
    check(rooms && rooms->size() == 3, "room list");
    check((*rooms)[0].has_password && (*rooms)[0].players == 2 && (*rooms)[0].max_players == 4 && (*rooms)[0].owner_present &&
              (*rooms)[0].difficulty == 0 && (*rooms)[0].data_hash == 777,
          "room fields");
    check(!(*rooms)[1].difficulty && (*rooms)[1].story_frozen && (*rooms)[1].randomizer, "null difficulty, story frozen");
    check((*rooms)[0].compatible(44, 777) && !(*rooms)[1].compatible(44, 777) && !(*rooms)[0].compatible(44, 778), "compatibility");
    check(parse_rooms(answer(200, R"({"rooms":[]})"))->empty(), "an object with rooms");
    check(!parse_rooms(answer(200, "{}")), "no list");

    const auto view = parse_room_view(answer(200, room_state_json("ABCD12", "Kim's room", "lee", false, 4)));
    check(view && view->room.code == "ABCD12" && view->settings.camera_mode == 1 && view->settings.light_mode == 0 && view->settings.fast_loading,
          "room view");
    check(view->members.size() == 2 && view->members[0].name == "kim" && view->members[0].owner && view->members[1].slot == 1 &&
              view->members[1].instance == 12u,
          "members sorted by room slot");
    check(view->instances.size() == 1 && view->instances[0].stage == "stag0101" && view->instances[0].mode == "sealed" &&
              view->instances[0].ticks == 1234 && view->instances[0].joinable && view->instances[0].members[1].lagging,
          "instances");
    check(view->you.name == "lee" && !view->you.owner, "you");

    const auto p = problem_of(problem(403, "wrong_room_password", "The room password is wrong."));
    check(p.status == 403 && p.code == "wrong_room_password" && p.text == "The room password is wrong.", "problem details");
    HttpResponse lost;
    lost.error = "WinHTTP error 12029";
    check(problem_of(lost).status == 0 && problem_of(lost).text.find("12029") != std::string::npos, "transport error");
    lost.pin_mismatch = true;
    check(problem_of(lost).text.find("certificate") != std::string::npos, "pin mismatch");
    check(problem_of(answer(500, "")).text == "The server answered HTTP 500.", "no body");

    check(check_user_name("kim").empty() && check_user_name("Kim-2_x").empty(), "good names");
    for (const char* bad : {"ki", "a very long user name here", "kim lee", "k\xC3\xA4m", "admin", "Deleted_5"})
        check(!check_user_name(bad).empty(), std::string("bad name: ") + bad);
    check(check_password("ten chars!", "kim").empty(), "good password");
    check(!check_password("short", "kim").empty() && !check_password("kimkimkimk", "kimkimkimk").empty() &&
              !check_password("aaaaaaaaaaaa", "kim").empty() && !check_password(std::string(129, 'x') + "abc", "kim").empty(),
          "bad passwords");
    check(check_room_name("Room").empty() && !check_room_name("   ").empty() && !check_room_name(std::string(33, 'r')).empty(), "room names");
    check(check_room_password("").empty() && check_room_password("abcd").empty() && !check_room_password("abc").empty(), "room passwords");
    check(version_at_least("0.2.0", "0.1.9") && version_at_least("0.1.0", "0.1") && !version_at_least("0.1.0", "0.1.1"), "versions");
    check(version_at_least("1.0.0-beta+abc", "1.0") && version_at_least("0.1.0", "") && version_at_least("0.1.0", "x"), "suffixes, no minimum");
    check(!version_at_least("dev", "0.1.0") && !version_at_least("1.2.3.4", "1.0.0"), "an unreadable own version fails");
}

struct Harness {
    std::filesystem::path folder;
    FakeQueue* queue = nullptr;
    std::unique_ptr<OnlineLobby> lobby;
    double now = 0;
    // Server state.
    std::string status = kStatus;
    std::string good_token = "TOKEN-KIM";
    int clears = 0;
    bool character = true;
    std::string room_password = "secret";

    explicit Harness(std::filesystem::path f, LobbyConfig extra = {}) : folder(std::move(f)) {
        auto q = std::make_unique<FakeQueue>();
        queue = q.get();
        q->handler = [this](const HttpRequest& r, const std::string& token) { return serve(r, token); };
        LobbyConfig config = extra;
        config.protocol = 44;
        config.data_hash = 777;
        config.client_version = "0.1.0";
        config.folder = folder;
        config.server_key = "127.0.0.1:54200";
        lobby = std::make_unique<OnlineLobby>(std::move(q), config);
    }
    void pass(int n = 3) {
        for (int i = 0; i < n; ++i) {
            now += 0.1;
            lobby->update(now, true);
        }
    }
    HttpResponse serve(const HttpRequest& r, const std::string& token) {
        if (r.path == "/status") return answer(200, status);
        if (r.path == "/auth/login") {
            const auto v = json::parse(r.body);
            if ((*v)["password"].str() != std::string("correct horse")) return problem(401, "invalid_credentials", "Wrong user name or password.");
            return answer(200, R"({"token":")" + good_token + R"(","expiresAt":"x","account":{"id":5,"username":"kim"}})");
        }
        if (r.path == "/auth/register") return answer(201, R"({"account":{"id":5,"username":"kim"},"recoveryCode":"RC-1234","token":")" + good_token + "\"}");
        if (r.path == "/auth/recover") return answer(200, R"({"recoveryCode":"RC-NEW"})");
        if (r.path == "/rooms" && r.method == "GET") return answer(200, rooms_json());
        if (r.path == "/world" && r.method == "GET") return answer(200, R"({"players":41,"playersOnline":41,"instances":12,"areas":{}})");
        if (token != good_token) return problem(401, "unauthorized", "Not logged in.");
        // Round I1a: the world (19.19 and the round's contract).
        if (r.path == "/friends" && r.method == "GET")
            return answer(200, R"({"friends":[{"username":"lee","presence":{"online":true}},{"username":"rin","presence":null}],"requestsIn":[]})");
        if (r.path == "/me/online-prefs" && r.method == "GET") return answer(200, prefs_json);
        if (r.path == "/me/online-prefs" && r.method == "PUT") {
            prefs_json = r.body;
            return answer(200, prefs_json);
        }
        if (r.path == "/world/enter") {
            ++enters;
            if (refuse_enter) return problem(403, "account_inactive", "The account can't play online.");
            return answer(200, R"({"relayTicket":"WT-1","ticket":"WT-1","relay":{"host":"127.0.0.1","port":54211,"protocol":"DW4R2"},"protocol":49,)"
                               R"("role":"player","relayRole":"member","box":1,"heldSeat":null,"prefs":{"defaultVisibility":"friends"},)"
                               R"("settings":{"cameraMode":1,"lightMode":0,"fastLoading":true,"randomizer":{"enabled":false,"seed":0}}})");
        }
        if (r.path == "/world/leave") {
            ++leaves;
            return answer(204);
        }
        if (r.path == "/me") return answer(200, R"({"id":5,"username":"kim","email":"","role":"player","status":"active","mustChangePassword":false})");
        if (r.path == "/online-character") {
            if (!character) return problem(404, "no_online_character", "There is no online character yet.");
            return answer(200, R"({"revision":2,"summary":{"playerName":"KIM","digimon":"Veemon","level":12,"clears":)" + std::to_string(clears) +
                                   R"(},"playSeconds":3600})");
        }
        if (r.path == "/auth/logout" || r.path == "/auth/password") return answer(204);
        if (r.path == "/online-character/reset") {
            const auto v = json::parse(r.body);
            if ((*v)["currentPassword"].str() != std::string("correct horse")) return problem(403, "wrong_password", "The password is wrong.");
            character = false;
            return answer(204);
        }
        if (r.path == "/auth/logout-all") return answer(200, R"({"revoked":3})");
        if (r.path == "/rooms" && r.method == "POST") {
            const auto v = json::parse(r.body);
            const auto name = (*v)["name"].str().value_or("");
            return answer(201, room_state_json("NEW001", name, "kim", true, int((*v)["maxPlayers"].i64().value_or(4))));
        }
        if (r.path == "/rooms/ABCD12/join") {
            const auto v = json::parse(r.body);
            if ((*v)["password"].str() != room_password) return problem(403, "wrong_room_password", "The room password is wrong.");
            return answer(200, room_state_json("ABCD12", "Kim's room", "kim", false, 4));
        }
        if (r.path == "/rooms/OPEN01/join") return answer(200, room_state_json("OPEN01", "Open room", "kim", false, 10));
        if (r.path == "/rooms/NEW001" || r.path == "/rooms/ABCD12" || r.path == "/rooms/OPEN01")
            return answer(200, room_state_json(r.path.substr(7), "Room", "kim", r.path == "/rooms/NEW001", 4));
        if (r.path.ends_with("/kick") || r.path.ends_with("/leave")) return answer(204);
        if (r.method == "DELETE") return answer(204);
        return problem(404, "not_found", "No such thing.");
    }
    std::string prefs_json = R"({"defaultVisibility":"public","cameraMode":0,"lightMode":1,"fastLoading":false,"randomizer":{"enabled":false},"lastBox":1})";
    int enters = 0, leaves = 0;
    bool refuse_enter = false;
};

void gates(const std::filesystem::path& root) {
    {
        LobbyConfig c;
        c.not_set_up = true;
        Harness h(root / "g0", c);
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::not_set_up && h.queue->sent.empty(), "a build without a pin sends nothing");
        check(h.lobby->gate_text().find("certificate") != std::string::npos, "not set up text");
    }
    {
        Harness h(root / "g1");
        check(h.lobby->gate() == Gate::unknown && !h.lobby->opened(), "nothing before open");
        h.pass();
        check(h.queue->sent.empty(), "no network before the lobby opens");
        h.lobby->open();
        check(h.lobby->gate() == Gate::checking, "checking");
        h.pass();
        check(h.lobby->gate() == Gate::open && h.lobby->screen() == Screen::welcome && !h.lobby->logged_in(), "open, logged out");
        check(h.lobby->status().relay_port == 54210 && h.lobby->status().registration_open, "status fields");
        check(!h.lobby->newer_version(), "no newer version");
    }
    {
        Harness h(root / "g2");
        h.status = R"({"maintenance":false,"client":{"minimumProtocol":45,"currentProtocol":45}})";
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::outdated && h.lobby->gate_text().find("Update required") != std::string::npos, "protocol gate");
        h.lobby->login("kim", "correct horse", false);
        check(!h.lobby->busy() && !h.lobby->error().empty(), "no login through a closed gate");
    }
    {
        Harness h(root / "g3");
        h.status = R"({"maintenance":false,"client":{"minimumVersion":"0.2.0","currentVersion":"0.3.1","minimumProtocol":0,"currentProtocol":44}})";
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::outdated && h.lobby->gate_text().find("0.2.0 or newer") != std::string::npos, "version gate");
        h.status = R"({"maintenance":false,"client":{"minimumVersion":"0.1.0","currentVersion":"0.3.1","minimumProtocol":0,"currentProtocol":44}})";
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::open && h.lobby->newer_version(), "a newer version is known");
    }
    {
        Harness h(root / "g4");
        h.status = R"({"maintenance":true,"motd":"Back at 18:00.","client":{}})";
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::maintenance && h.lobby->gate_text().find("Back at 18:00.") != std::string::npos, "maintenance");
    }
    {
        Harness h(root / "g5");
        h.status = "not json";
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::unreachable, "unreachable");
        h.status = kStatus;
        h.lobby->open();
        h.pass();
        check(h.lobby->gate() == Gate::open, "try again");
    }
}

void accounts(const std::filesystem::path& root) {
    const auto folder = root / "acc";
    {
        Harness h(folder);
        h.lobby->open();
        h.pass();
        h.lobby->go(Screen::login);
        h.lobby->login("kim", "wrong one", true);
        check(h.lobby->busy() && h.lobby->busy_text() == "Logging in...", "busy while logging in");
        h.pass();
        check(!h.lobby->logged_in() && h.lobby->error() == "Wrong user name or password." && h.lobby->screen() == Screen::login, "wrong password");
        h.lobby->login("Kim", "correct horse", true);
        h.pass();
        check(h.lobby->logged_in() && h.lobby->account() == "kim" && h.lobby->screen() == Screen::home, "logged in, the server's spelling");
        check(h.lobby->remembered() && std::filesystem::exists(folder / "account.txt") && load_token(folder, "127.0.0.1:54200", "kim"),
              "remember me keeps the token (DPAPI)");
        check(h.queue->sent_path("GET", "/me") && h.queue->sent_path("GET", "/online-character") && h.queue->token == "TOKEN-KIM",
              "account and character after the login");
        check(h.lobby->character() && h.lobby->character()->level == 12 && h.lobby->max_difficulty() == 0, "character summary, Normal only");
        check(h.lobby->world_counts() && h.lobby->world_counts()->players == 41 && h.lobby->world_counts()->instances == 12,
              "the world's counts load on the home screen");
        check(h.lobby->prefs() && h.lobby->prefs()->default_visibility == "public", "the online preferences after the login");
        check(h.lobby->play_label() == "Play: Veemon Lv 12 (Box 1)", "the Play button names the character: " + h.lobby->play_label());
        // The lobby's welcome after Play's drive (19.5.1) counts the friends online.
        check(!h.lobby->friends_online(), "no friend count before it is asked");
        h.lobby->refresh_friends();
        h.pass();
        check(h.queue->sent_path("GET", "/friends") && h.lobby->friends_online() == 1, "GET /friends: one friend online");
    }
    {
        // A second start: the remembered login comes back without a password.
        Harness h(folder);
        h.lobby->open();
        h.pass();
        check(h.lobby->logged_in() && h.lobby->account() == "kim" && h.lobby->screen() == Screen::home, "remembered login");
        h.lobby->go(Screen::account);
        h.lobby->logout();
        h.pass();
        check(!h.lobby->logged_in() && h.lobby->screen() == Screen::welcome && h.lobby->info() == "Logged out.", "logout");
        check(!std::filesystem::exists(folder / "account.txt") && !load_token(folder, "127.0.0.1:54200", "kim"), "logout forgets the token");
    }
    {
        // A remembered token the server refuses is forgotten.
        Harness h(folder);
        save_token(folder, "127.0.0.1:54200", "kim", "OLD-TOKEN");
        std::ofstream(folder / "account.txt") << "kim\n";
        h.lobby->open();
        h.pass();
        check(!h.lobby->logged_in() && h.lobby->screen() == Screen::welcome && !load_token(folder, "127.0.0.1:54200", "kim"),
              "a refused token is forgotten");
    }
    {
        // Without "remember me" nothing stays on disk.
        Harness h(folder);
        h.lobby->open();
        h.pass();
        h.lobby->login("kim", "correct horse", false);
        h.pass();
        check(h.lobby->logged_in() && !h.lobby->remembered() && !std::filesystem::exists(folder / "account.txt") &&
                  !load_token(folder, "127.0.0.1:54200", "kim"),
              "a login that is not remembered");
        h.lobby->logout_everywhere();
        h.pass();
        check(!h.lobby->logged_in() && h.lobby->info() == "Logged out on every device.", "log out everywhere");
    }
    {
        Harness h(root / "reg");
        h.lobby->open();
        h.pass();
        h.lobby->go(Screen::register_account);
        h.lobby->register_account("k", "long enough!", "long enough!", "", true);
        check(!h.lobby->busy() && h.lobby->error().find("3 to 20") != std::string::npos, "name checked locally");
        h.lobby->register_account("kim", "short", "short", "", true);
        check(h.lobby->error().find("10 characters") != std::string::npos, "password checked locally");
        h.lobby->register_account("kim", "long enough!", "long enough?", "", true);
        check(h.lobby->error() == "The two passwords differ.", "the two passwords");
        h.lobby->register_account("kim", "long enough!", "long enough!", "nomail", true);
        check(h.lobby->error().find("e-mail") != std::string::npos, "e-mail shape");
        const auto before = h.queue->sent.size();
        h.lobby->register_account("kim", "long enough!", "long enough!", "", true);
        h.pass();
        check(h.queue->sent.size() > before && h.lobby->logged_in() && h.lobby->screen() == Screen::registered && h.lobby->recovery_code() == "RC-1234",
              "registered: the recovery code is shown");
        h.lobby->acknowledge_code();
        h.pass();
        check(h.lobby->screen() == Screen::home && h.lobby->recovery_code().empty(), "I saved it");
        h.lobby->go(Screen::account);
        h.lobby->go(Screen::change_password);
        h.lobby->change_password("old", "new password 1", "new password 2");
        check(h.lobby->error() == "The two passwords differ.", "change password: the two passwords");
        h.lobby->change_password("old", "new password 1", "new password 1");
        h.pass();
        check(h.lobby->screen() == Screen::account && h.lobby->info().find("Password changed") != std::string::npos, "password changed");
        // The online character's reset (round 56): the password, the server's answer, the account without a character.
        check(h.lobby->character() && h.lobby->character()->exists, "the account has an online character");
        h.lobby->go(Screen::reset_character);
        h.lobby->reset_character("");
        check(h.lobby->error().find("password") != std::string::npos, "reset: the password is needed");
        h.lobby->reset_character("a wrong one");
        h.pass();
        check(h.lobby->screen() == Screen::reset_character && h.lobby->error() == "The password is wrong.", "reset: a wrong password");
        h.lobby->reset_character("correct horse");
        h.pass();
        check(h.lobby->screen() == Screen::account && h.lobby->info().find("was reset") != std::string::npos, "reset: done");
        check(h.lobby->character() && !h.lobby->character()->exists, "reset: the account has no online character now");
        check(std::string(screen_name(Screen::reset_character)) == "reset" && screen_from_name("reset") == Screen::reset_character, "reset: its name");
        h.lobby->back();
        check(h.lobby->screen() == Screen::home, "back to the home screen");
    }
    {
        Harness h(root / "rec");
        h.lobby->open();
        h.pass();
        h.lobby->go(Screen::recover);
        h.lobby->recover("kim", "RC-1234", "new password 1", "new password 1");
        h.pass();
        check(h.lobby->screen() == Screen::recovered && h.lobby->recovery_code() == "RC-NEW", "recovered: a new code");
        h.lobby->acknowledge_code();
        check(h.lobby->screen() == Screen::login && h.lobby->recover_name() == "kim", "then the login with the name");
        h.lobby->back();
        check(h.lobby->screen() == Screen::welcome, "back from there");
    }
    {
        // 426 on a login closes the gate.
        Harness h(root / "426");
        h.lobby->open();
        h.pass();
        h.queue->handler = [](const HttpRequest& r, const std::string&) {
            return r.path == "/auth/login" ? problem(426, "client_outdated", "This game version is too old for the server. Please update.")
                                           : answer(200, kStatus);
        };
        h.lobby->login("kim", "correct horse", false);
        h.pass();
        check(h.lobby->gate() == Gate::outdated && !h.lobby->logged_in(), "426 closes the gate");
    }
}

// Round I1a (19.5.1): the home screen. Play enters the world (POST
// /world/enter) and hands the host the entry; an offline game asks first;
// Online > Settings saves the preferences; the playing screen; a failed
// start leaves the world; the command line's session is adopted.
void world(const std::filesystem::path& root) {
    Harness h(root / "world");
    h.lobby->open();
    h.pass();
    h.lobby->login("kim", "correct horse", false);
    h.pass();
    check(h.lobby->screen() == Screen::home && h.lobby->can_play(), "the home screen");
    h.lobby->play();
    check(h.lobby->screen() == Screen::starting && h.lobby->busy_text() == "Entering the server...", "entering");
    h.pass();
    auto ready = h.lobby->take_ready();
    check(ready && ready->entry.ticket == "WT-1" && ready->token == "TOKEN-KIM" && ready->account == "kim" && ready->relay_port == 54211 &&
              ready->entry.prefs.settings.camera_mode == 1 && ready->entry.prefs.settings.fast_loading && ready->entry.box == 1,
          "the ready world entry");
    {
        const auto& body = std::find_if(h.queue->sent.rbegin(), h.queue->sent.rend(), [](const auto& s) { return s.first.path == "/world/enter"; })->first.body;
        check(body.find(R"("protocol":44)") != std::string::npos && body.find(R"("dataHash":777)") != std::string::npos &&
                  body.find(R"("box":1)") != std::string::npos,
              "enter body: " + body);
    }
    check(!h.lobby->take_ready(), "taken once");
    h.lobby->session_started();
    h.pass();
    check(h.lobby->screen() == Screen::playing && h.lobby->in_world() && !h.lobby->can_back(), "playing");
    h.lobby->play();
    check(h.enters == 1, "one world session at a time");
    h.lobby->logout();
    check(h.lobby->logged_in(), "no logout while playing");
    h.lobby->request_leave();
    check(h.lobby->take_leave_request() && !h.lobby->take_leave_request(), "leave request taken once");
    h.lobby->session_ended("You went offline.");
    h.pass();
    check(h.lobby->screen() == Screen::home && !h.lobby->in_world() && h.lobby->info() == "You went offline.", "back to the home screen");
    // An offline game in progress: Play asks first.
    h.lobby->set_offline_game(true);
    h.lobby->play();
    check(h.lobby->screen() == Screen::confirm, "the offline game question");
    h.lobby->cancel_start();
    check(h.lobby->screen() == Screen::home && !h.lobby->busy() && h.enters == 1, "cancelled");
    h.lobby->play();
    h.lobby->confirm_start();
    h.pass();
    check(h.lobby->take_ready() && h.enters == 2, "confirmed");
    h.lobby->session_failed("The relay does not answer.");
    h.pass();
    check(h.lobby->screen() == Screen::home && h.lobby->error() == "The relay does not answer." && h.leaves == 1, "a failed start leaves the world");
    // A refused entry stays home with the reason.
    h.lobby->set_offline_game(false);
    h.refuse_enter = true;
    h.lobby->play();
    h.pass();
    check(h.lobby->screen() == Screen::home && !h.lobby->take_ready() && h.lobby->error() == "Your account can't play online right now.",
          "a refused entry");
    h.refuse_enter = false;
    // Online > Settings.
    h.lobby->go(Screen::settings);
    api::OnlinePrefs prefs;
    prefs.default_visibility = "private";
    prefs.settings.camera_mode = 1;
    prefs.settings.light_mode = 0;
    prefs.settings.rand_seed = 99;
    prefs.settings.rand_enemies = 1;
    h.lobby->save_settings(prefs);
    h.pass();
    check(h.queue->sent_path("PUT", "/me/online-prefs") && h.lobby->prefs() && h.lobby->prefs()->default_visibility == "private" &&
              h.lobby->prefs()->settings.rand_seed == 99 && h.lobby->screen() == Screen::home && h.lobby->info().find("Settings saved") != std::string::npos,
          "the settings saved");
    prefs.settings.camera_mode = 3;
    h.lobby->save_settings(prefs);
    check(h.lobby->error() == "A setting is out of range.", "a setting out of range");
    // The command line's session shows in the lobby too.
    h.lobby->adopt_world("kim", "TOKEN-KIM");
    h.pass();
    check(h.lobby->in_world() && h.lobby->screen() == Screen::playing, "adopted world session");
    check(tidy_reason("Signed in on another PC (Signed in on another PC.).") == "Signed in on another PC." &&
              tidy_reason("You were removed (spam).") == "You were removed (spam)." && tidy_reason("You went offline.") == "You went offline.",
          "close reasons");
    check(screen_from_name("home") == Screen::home && std::string(screen_name(Screen::playing)) == "playing" &&
              screen_from_name("settings") == Screen::settings && !screen_from_name("rooms"),
          "screen names");
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("dw4_lobby_tests_" + std::to_string(std::rand()));
    std::filesystem::remove_all(root);
    int failed = 0;
    for (const auto& [name, test] : std::initializer_list<std::pair<const char*, std::function<void()>>>{
             {"api calls", api_calls}, {"gates", [&] { gates(root); }}, {"accounts", [&] { accounts(root); }}, {"world", [&] { world(root); }}}) {
        try {
            test();
            std::cout << "ok   " << name << '\n';
        } catch (const std::exception& e) {
            std::cout << "FAIL " << name << ": " << e.what() << '\n';
            ++failed;
        }
    }
    std::filesystem::remove_all(root);
    std::cout << checks << " checks, " << failed << " failed\n";
    return failed ? 1 : 0;
}
