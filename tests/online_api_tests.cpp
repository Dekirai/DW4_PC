// The online client's REST side without a server: the JSON reader, the room
// settings (JSON, the settings hash text, the engine cheats), the request
// bodies and answer parsers of the API calls, the hard-coded endpoint and the
// development override (loopback only), the DPAPI token file, the SPKI pin of
// a certificate made here (no certificate is stored in the repository).
#include "online/api.hpp"
#include "online/http.hpp"
#include "online/json.hpp"
#include "online/room_settings.hpp"
#include "online/server_config.hpp"
#include "online/token_store.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#endif

#include <filesystem>
#include <fstream>
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

void json_reader() {
    const auto v = json::parse(R"({"a":1,"b":[true,false,null],"c":{"d":"x\"y\\z\u00e9\ud83d\ude00"},"e":-12.5e1,"big":18446744073709551615})");
    check(v.has_value(), "parses");
    check((*v)["a"].i64() == 1 && (*v)["b"].items.size() == 3 && (*v)["b"].items[0].flag() == true, "numbers, arrays, booleans");
    check((*v)["c"]["d"].str() == std::string("x\"y\\z\xC3\xA9\xF0\x9F\x98\x80"), "escapes and surrogate pairs to UTF-8");
    check((*v)["e"].number == -125.0 && !(*v)["e"].i64(), "a fraction is no integer");
    check((*v)["big"].u64() == 18446744073709551615ull, "u64 range");
    check((*v)["missing"].is_null() && (*v)["a"]["deeper"].is_null(), "missing keys are null");
    for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\":1}x", "\"\x01\"", "tru", "{\"a\" 1}", "\"\\q\""})
        check(!json::parse(bad), std::string("malformed: ") + bad);
    std::string deep(40, '[');
    deep += std::string(40, ']');
    check(!json::parse(deep), "depth above 32");
    check(json::quote("a\"b\\c\n\x01") == "\"a\\\"b\\\\c\\n\\u0001\"", "quote");
    check(json::parse(json::quote("r\xC3\xA9sum\xC3\xA9"))->str() == "r\xC3\xA9sum\xC3\xA9", "UTF-8 passes through");
}

void settings() {
    RoomSettings s;
    s.difficulty = 1;
    s.camera_mode = 1;
    s.light_mode = 0;
    s.fast_loading = true;
    s.rand_seed = 4242;
    s.rand_enemies = 1;
    s.rand_zones = 2;
    s.rand_drops = true;
    const auto text = settings_json(s);
    const auto back = settings_from_json(*json::parse(text));
    check(back && *back == s, "settings JSON round trip");
    check(settings_hash_text(s, 44, 123) == "DW4R2-settings/1;protocol=44;data=123;difficulty=1;camera=1;light=0;rand=4242,1,1,2;fast=1",
          "settings hash text (the relay's RoomSettings.Canonical)");
    // Golden values shared with the server's RoomSettings tests (one hash on both sides).
    check(settings_hash(s, 44, 123) == 0x54c0f0b064c906baull, "settings hash");
    check(settings_hash(RoomSettings{}, 44, 123) == 0x3832ef80ddfb9134ull, "default settings hash (light mode 1 as the server's default)");
    RoomSettings off = s;
    off.rand_enemies = off.rand_zones = 0;
    off.rand_drops = false; // a seed without modes is no randomizer
    check(settings_hash_text(off, 44, 123).find(";rand=0,0,0,0;") != std::string::npos, "an inactive randomizer hashes as off");
    check(settings_hash(s, 44, 123) != settings_hash(off, 44, 123) && settings_hash(s, 44, 123) == settings_hash(s, 44, 123), "hash");
    check(settings_hash(s, 44, 123) != settings_hash(s, 45, 123) && settings_hash(s, 44, 123) != settings_hash(s, 44, 124),
          "protocol and data hash count");
    check(settings_from_json(*json::parse("null")) == RoomSettings{}, "no settings: defaults");
    for (const char* bad : {R"({"difficulty":3})", R"({"cameraMode":2})", R"({"randomizer":{"enabled":true,"seed":-1}})",
                            R"({"randomizer":{"enabled":true,"seed":1,"zones":5}})", "[1]"})
        check(!settings_from_json(*json::parse(bad)), std::string("out of range: ") + bad);
    dw4::engine::Cheats cheats;
    cheats.god = cheats.one_hit = cheats.infinite_mp = true;
    cheats.exp_rate = 200;
    cheats.damage_taken = 50;
    apply_room_cheats(s, cheats);
    check(!cheats.god && !cheats.one_hit && !cheats.infinite_mp && cheats.exp_rate == 100 && cheats.damage_taken == 100, "no cheats online");
    check(cheats.rand_seed == 4242 && cheats.rand_enemies == 1 && cheats.rand_zones == 2 && cheats.drop_seed == 4242 && cheats.fast_loading,
          "the room's randomizer and fast loading");
    apply_room_cheats(RoomSettings{}, cheats);
    check(cheats.original(), "default room settings: the original game");
}

void api_calls() {
    const auto login = api::login_request("kim", "p\"w", "0.52.0", 44);
    check(login.method == "POST" && login.path == "/auth/login" && !login.authorized, "login request");
    const auto body = json::parse(login.body);
    check(body && (*body)["username"].str() == "kim" && (*body)["password"].str() == "p\"w" && (*body)["protocol"].i64() == 44, "login body");
    api::CreateRoom room;
    room.name = "Kim's room";
    room.max_players = 6;
    room.settings.camera_mode = 1;
    const auto create = api::create_room_request(room, 44, 18446744073709551615ull, "0.52.0");
    const auto cbody = json::parse(create.body);
    check(cbody && (*cbody)["maxPlayers"].i64() == 6 && (*cbody)["dataHash"].u64() == 18446744073709551615ull &&
              (*cbody)["settings"]["cameraMode"].i64() == 1 && (*cbody)["password"].is_null(),
          "create body");
    const auto join = api::join_room_request("ABCD", "secret", 44, 7, "0.52.0");
    check(join.path == "/rooms/ABCD/join" && json::parse(join.body).value()["password"].str() == "secret", "join body");
    check(api::ticket_request("ABCD").path == "/rooms/ABCD/ticket" && api::leave_request("ABCD").path == "/rooms/ABCD/leave", "paths");

    HttpResponse r;
    r.status = 200;
    r.body = R"({"service":"DW4Online","client":{"minimumProtocol":44,"currentProtocol":44},"maintenance":false,"relay":{"host":"x","port":52041}})";
    const auto status = api::parse_status(r);
    check(status && status->current_protocol == 44 && status->relay_port == 52041, "status");
    r.body = R"({"token":"abc.def","expiresAt":"2026-12-01T00:00:00Z","account":{"username":"kim","id":3}})";
    const auto l = api::parse_login(r);
    check(l && l->token == "abc.def" && l->account == "kim", "login answer");
    r.body = R"({"room":{"code":"QX7P","name":"Kim's room"},"settings":{"difficulty":2,"cameraMode":1},"members":[],"instances":[],)"
             R"("you":{"accountId":3,"username":"kim","slot":0},"heartbeatSeconds":10,"relay":{"host":"88.198.52.221","port":52041,"protocol":"DW4R2"},)"
             R"("settingsHash":"54c0f0b064c906ba","relayTicket":"T1"})";
    const auto joined = api::parse_room(r);
    check(joined && joined->code == "QX7P" && joined->settings.difficulty == 2 && joined->settings.camera_mode == 1 && joined->ticket == "T1" &&
              joined->member_id == 3 && joined->relay_port == 52041,
          "room answer with the relay ticket (relayTicket)");
    // Round G-core: who owns the room (both ways into a room decide with owns_room; the title's online screens
    // once left every guest the owner, and its card got the room story).
    r.body = R"({"room":{"code":"QX7P","name":"Kim's room","owner":"Kim"},"settings":{"difficulty":0},"relayTicket":"T1"})";
    const auto owned = api::parse_room(r);
    check(owned && owned->owner == "Kim", "the room's owner");
    check(api::owns_room(*owned, "kim", false) && api::owns_room(*owned, "KIM", false), "the owner (any case) owns the room");
    check(!api::owns_room(*owned, "lee", false) && !api::owns_room(*owned, "kimx", false) && !api::owns_room(*owned, "", false),
          "a guest does not own it");
    check(api::owns_room(*owned, "lee", true), "the account that created it does");
    api::RoomJoined ownerless = *owned;
    ownerless.owner.clear();
    check(!api::owns_room(ownerless, "kim", false), "no owner named: a guest");
    r.body = R"({"room":{"code":"QX7P"},"settings":null,"relay":{"port":52041,"ticket":"T2"}})";
    check(api::parse_room(r)->ticket == "T2", "or the ticket in relay");
    r.body = R"({"ticket":"T3"})";
    check(api::parse_ticket(r) == "T3", "ticket answer");
    r.body = R"({"room":{"code":"QX7P"},"relayTicket":"T4"})";
    check(api::parse_ticket(r) == "T4", "a fresh ticket from POST /rooms/{code}/ticket (relayTicket)");
    r.status = 409;
    r.body = R"({"type":"about:blank","title":"Conflict","status":409,"detail":"The room is full.","code":"room_full"})";
    check(!api::parse_room(r) && api::problem_text(r) == "The room is full.", "problem details");
    HttpResponse none;
    none.error = "WinHTTP error 12029";
    check(api::problem_text(none) == "WinHTTP error 12029", "transport errors");
}

#ifndef DW4_EXPECTED_PINS
#define DW4_EXPECTED_PINS ""
#endif

void endpoints() {
    const auto release = release_endpoint();
#ifndef DW4_ONLINE_USE_DOMAIN
    check(release.host == "88.198.52.221" && release.port == 8443 && release.base_path == "/api/v1" && release.pinned && !release.development,
          "the hard-coded IP endpoint, pinned");
    check(release.url() == "https://88.198.52.221:8443/api/v1", "url");
    // The pins come from the build step (CMake DW4_ONLINE_PINS or local/online_pins.txt, DEPLOY.md 6.1).
    check(release.pins == split_pins(DW4_EXPECTED_PINS), "the build's pins");
    for (const auto& pin : release.pins) check(pin.size() == 44 && pin.back() == '=', "a pin is a base64 SHA-256");
    std::cout << "server pins in this build: " << release.pins.size() << '\n';
#endif
    const auto dev = development_endpoint("127.0.0.1:53210", "AAA=, BBB=");
    check(dev && dev->host == "127.0.0.1" && dev->port == 53210 && dev->pins.size() == 2 && dev->pins[1] == "BBB=" && dev->development,
          "a loopback development server");
    check(development_endpoint("localhost:53210", "")->host == "127.0.0.1", "localhost is loopback");
    check(development_endpoint("[::1]:53210", "")->host == "::1", "IPv6 loopback");
    for (const char* bad : {"88.198.52.221:443", "10.0.0.1:53210", "127.0.0.1", "127.0.0.1:0", "127.0.0.1:99999", "127.0.0:1", "127.1.2.3.4:80",
                            "example.org:443", "127.0.0.1.example.org:443", "[::2]:1"})
        check(!development_endpoint(bad, "x"), std::string("only loopback: ") + bad);
#ifndef DW4_ONLINE_DEV
    check(!server_endpoint().development, "a release build never takes the override");
#endif
    check(split_pins(" a ,, b,") == std::vector<std::string>{"a", "b"}, "pin lists");
}

void tokens() {
#ifdef _WIN32
    const auto folder = std::filesystem::temp_directory_path() / ("dw4_online_tokens_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(folder);
    check(save_token(folder, "88.198.52.221", "kim", "secret-token-123"), "saved");
    check(load_token(folder, "88.198.52.221", "kim") == "secret-token-123", "read back");
    std::ifstream raw(token_file(folder, "kim"), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(raw), std::istreambuf_iterator<char>()};
    raw.close();
    check(bytes.rfind("DW4TOKEN", 0) == 0 && bytes.find("secret-token-123") == std::string::npos, "the file holds no plain token");
    check(!load_token(folder, "127.0.0.1", "kim"), "another server's entropy does not decrypt");
    {
        std::filesystem::copy_file(token_file(folder, "kim"), token_file(folder, "lee"));
        check(!load_token(folder, "88.198.52.221", "lee"), "another account's entropy does not decrypt");
    }
    {
        std::string damaged = bytes;
        damaged[damaged.size() - 3] ^= 0x55;
        std::ofstream out(token_file(folder, "kim"), std::ios::binary | std::ios::trunc);
        out << damaged;
    }
    check(!load_token(folder, "88.198.52.221", "kim"), "a damaged file is refused");
    forget_token(folder, "kim");
    check(!std::filesystem::exists(token_file(folder, "kim")), "forgotten");
    check(token_file(folder, "Kim/../x").filename() == "_im____x.token", "account names fold to a safe file name");
    std::filesystem::remove_all(folder);
#endif
}

#ifdef _WIN32
// A throwaway self-signed certificate made in memory (ephemeral key container).
std::vector<std::uint8_t> self_signed(const wchar_t* container) {
    HCRYPTPROV provider = 0;
    CryptAcquireContextW(&provider, container, nullptr, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    if (!CryptAcquireContextW(&provider, container, nullptr, PROV_RSA_AES, CRYPT_NEWKEYSET)) return {};
    HCRYPTKEY key = 0;
    CryptGenKey(provider, AT_SIGNATURE, (2048u << 16) | CRYPT_EXPORTABLE, &key);
    const wchar_t* subject = L"CN=dw4-test";
    BYTE name[256];
    DWORD name_size = sizeof name;
    CertStrToNameW(X509_ASN_ENCODING, subject, CERT_X500_NAME_STR, nullptr, name, &name_size, nullptr);
    CERT_NAME_BLOB blob{name_size, name};
    CRYPT_KEY_PROV_INFO info{};
    info.pwszContainerName = const_cast<wchar_t*>(container);
    info.dwProvType = PROV_RSA_AES;
    info.dwKeySpec = AT_SIGNATURE;
    PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(provider, &blob, 0, &info, nullptr, nullptr, nullptr, nullptr);
    std::vector<std::uint8_t> der;
    if (cert) {
        der.assign(cert->pbCertEncoded, cert->pbCertEncoded + cert->cbCertEncoded);
        CertFreeCertificateContext(cert);
    }
    if (key) CryptDestroyKey(key);
    CryptReleaseContext(provider, 0);
    HCRYPTPROV gone = 0;
    CryptAcquireContextW(&gone, container, nullptr, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    return der;
}
#endif

void pins() {
#ifdef _WIN32
    const auto a = self_signed(L"dw4-online-test-a");
    const auto b = self_signed(L"dw4-online-test-b");
    if (a.empty() || b.empty()) {
        std::cout << "(no CryptoAPI key container: pin checks skipped)\n";
        return;
    }
    const auto pin_a = spki_pin_of_certificate(a);
    const auto pin_b = spki_pin_of_certificate(b);
    check(pin_a.size() == 44 && pin_a.back() == '=', "a base64 SHA-256 (44 characters)");
    check(pin_a != pin_b, "another key, another pin");
    check(spki_pin_of_certificate(a) == pin_a, "stable");
    auto damaged = a;
    damaged.resize(damaged.size() / 2);
    check(spki_pin_of_certificate(damaged).empty() && spki_pin_of_certificate({}).empty(), "malformed DER: no pin");
    // A pinned build without pins refuses every request before it opens one.
    ServerEndpoint no_pins = release_endpoint();
    no_pins.host = "127.0.0.1";
    no_pins.port = 9; // nothing is contacted
    no_pins.pins.clear();
    no_pins.pinned = true;
    HttpWorker worker(no_pins);
    const auto response = worker.wait(worker.submit(api::status_request()));
    check(response.status == 0 && response.error.find("no pin") != std::string::npos, "fail closed without pins");
#endif
}
} // namespace

int main() {
    try {
        json_reader();
        settings();
        api_calls();
        endpoints();
        tokens();
        pins();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
