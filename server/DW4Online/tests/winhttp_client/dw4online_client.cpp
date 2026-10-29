// dw4online_client: an end-to-end test tool for the DW4Online API over WinHTTP (the HTTP stack the game client will
// use; nothing here is part of the game yet). It logs in, uploads and downloads the cloud save, writes and reads the
// online character, opens a room, enters a stage and checks the relay handshake, unlocks an achievement in the room,
// leaves and logs out. Every step prints one line; the exit code is 0 only when every step passed.
//
//   dw4online_client --base http://127.0.0.1:5280 --user NAME --password PW --save savedata.dat --record record.bin
//                    [--stage stagD101] [--achievement online_join] [--protocol 42] [--data-hash 1] [--pin BASE64]
//
// --base may carry a path (https://example.org/dw4online): every API path goes below it.
// --pin: the server's certificate is self-signed (https://88.198.52.221): instead of the Windows root store, the
// SHA-256 of its SubjectPublicKeyInfo (base64, "DW4Online.Server tls-pin") must match. It is checked in WinHTTP's
// SENDING_REQUEST callback, after the TLS handshake and before any request byte (token, password) leaves the machine;
// a mismatch closes the request. Only an unknown CA and a name mismatch are ignored then, never an expired certificate.
//
// Build: build.cmd (MSVC, C++20; links winhttp, bcrypt, crypt32, ws2_32). https URLs use Schannel (TLS 1.2+).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::wstring base = L"http://127.0.0.1:5280";
    std::string user, password, save_path, record_path;
    std::string stage = "stagD101";
    std::string achievement = "online_join";
    int protocol = 42;
    unsigned long long data_hash = 1;
    std::string client_version = "0.48.0";
    std::string pin; // base64 SHA-256 SPKI of a pinned (self-signed) server certificate
};

struct Response {
    DWORD status = 0;
    std::string body;
    std::map<std::string, std::string> headers; // the few we ask for
};

int g_failures = 0;
int g_pin_failures = 0;

void step(bool ok, const char* name, const std::string& detail = {}) {
    std::printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : ": ", detail.c_str());
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

std::string escape_json(std::string_view s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// Minimal JSON reading for a test tool: the first string / number value of a key at or after `from`.
std::optional<std::string> json_string(const std::string& body, std::string_view key, size_t from = 0) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const size_t at = body.find(needle, from);
    if (at == std::string::npos) return std::nullopt;
    std::string out;
    for (size_t i = at + needle.size(); i < body.size(); ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) {
            out += body[++i] == 'n' ? '\n' : body[i];
        } else if (body[i] == '"') {
            return out;
        } else {
            out += body[i];
        }
    }
    return std::nullopt;
}

std::optional<long long> json_number(const std::string& body, std::string_view key, size_t from = 0) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const size_t at = body.find(needle, from);
    if (at == std::string::npos) return std::nullopt;
    char* end = nullptr;
    const long long v = std::strtoll(body.c_str() + at + needle.size(), &end, 10);
    if (end == body.c_str() + at + needle.size()) return std::nullopt;
    return v;
}

bool json_true(const std::string& body, std::string_view key) {
    return body.find("\"" + std::string(key) + "\":true") != std::string::npos;
}

std::vector<unsigned char> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string sha256_hex(const std::vector<unsigned char>& data) {
    unsigned char digest[32] = {};
    if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data.data()), ULONG(data.size()), digest, 32)))
        return {};
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

std::string base64(const std::vector<unsigned char>& data) {
    DWORD size = 0;
    const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    CryptBinaryToStringA(data.data(), DWORD(data.size()), flags, nullptr, &size);
    std::string out(size, '\0');
    CryptBinaryToStringA(data.data(), DWORD(data.size()), flags, out.data(), &size);
    out.resize(size);
    return out;
}

// The SHA-256 of a certificate's DER SubjectPublicKeyInfo, base64: the same value as "DW4Online.Server tls-pin".
std::string spki_pin(PCCERT_CONTEXT cert) {
    BYTE* der = nullptr;
    DWORD length = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &cert->pCertInfo->SubjectPublicKeyInfo, CRYPT_ENCODE_ALLOC_FLAG,
                             nullptr, &der, &length))
        return {};
    std::vector<unsigned char> digest(32);
    const bool hashed = BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, der, length, digest.data(), 32));
    LocalFree(der);
    return hashed ? base64(digest) : std::string{};
}

// One request's pin check, filled in by the callback.
struct PinCheck {
    std::string expected;
    std::string seen;
    bool checked = false;
    bool mismatch = false;
};

// Called after the TLS handshake and before the request is sent: a certificate with another key never gets the
// request (with its token or password); closing the handle makes WinHttpSendRequest fail.
void CALLBACK pin_callback(HINTERNET request, DWORD_PTR context, DWORD status, LPVOID, DWORD) {
    if (status != WINHTTP_CALLBACK_STATUS_SENDING_REQUEST || context == 0) return;
    auto* check = reinterpret_cast<PinCheck*>(context);
    PCCERT_CONTEXT cert = nullptr;
    DWORD size = sizeof cert;
    if (WinHttpQueryOption(request, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &size) && cert) {
        check->seen = spki_pin(cert);
        CertFreeCertificateContext(cert);
    }
    check->checked = true;
    if (check->seen.empty() || check->seen != check->expected) {
        check->mismatch = true;
        WinHttpCloseHandle(request);
    }
}

class Api {
public:
    Api(const std::wstring& base, std::string pin) : pin_(std::move(pin)) {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        wchar_t host[256] = {};
        wchar_t path[512] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = 255;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 511;
        parts.dwSchemeLength = DWORD(-1);
        if (!WinHttpCrackUrl(base.c_str(), 0, 0, &parts)) return;
        host_ = host;
        port_ = parts.nPort;
        secure_ = parts.nScheme == INTERNET_SCHEME_HTTPS;
        for (const wchar_t* p = path; *p; ++p) prefix_ += char(*p); // "/dw4online" below a path base, "" at the root
        while (!prefix_.empty() && prefix_.back() == '/') prefix_.pop_back();
        session_ = WinHttpOpen(L"DW4_PC-test/dw4online_client", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_) {
            WinHttpSetTimeouts(session_, 5000, 5000, 15000, 15000);
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
            WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
            connect_ = WinHttpConnect(session_, host_.c_str(), port_, 0);
        }
    }
    ~Api() {
        if (connect_) WinHttpCloseHandle(connect_);
        if (session_) WinHttpCloseHandle(session_);
    }
    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;

    bool ready() const { return connect_ != nullptr; }
    void set_token(std::string token) { token_ = std::move(token); }

    Response send(const wchar_t* method, const std::string& path, const std::vector<unsigned char>& body,
                  const std::wstring& content_type, const std::wstring& extra = {},
                  std::initializer_list<const wchar_t*> want_headers = {}) {
        Response out;
        HINTERNET req = WinHttpOpenRequest(connect_, method, widen(prefix_ + path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, secure_ ? WINHTTP_FLAG_SECURE : 0);
        if (!req) return out;
        PinCheck check{pin_};
        const bool pinned = secure_ && !pin_.empty();
        if (pinned) {
            // A self-signed certificate: its key is checked by the pin, not by a CA or the name.
            DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags);
            // FLAG_SEND_REQUEST covers SENDING_REQUEST (checked) and REQUEST_SENT (ignored by the callback).
            WinHttpSetStatusCallback(req, pin_callback, WINHTTP_CALLBACK_FLAG_SEND_REQUEST, 0);
        }
        std::wstring headers = extra;
        if (!content_type.empty()) headers += L"Content-Type: " + content_type + L"\r\n";
        if (!token_.empty()) headers += L"Authorization: Bearer " + widen(token_) + L"\r\n";
        const BOOL sent = WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), DWORD(-1),
                                             body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<unsigned char*>(body.data()),
                                             DWORD(body.size()), DWORD(body.size()), pinned ? reinterpret_cast<DWORD_PTR>(&check) : 0);
        if (pinned && check.mismatch) { // the callback closed the request before anything was sent
            out.body = "pin mismatch (server key " + (check.seen.empty() ? std::string("unknown") : check.seen) + ")";
            ++g_pin_failures;
            return out;
        }
        if (pinned && sent && !check.checked) {
            out.body = "pin not checked";
            ++g_pin_failures;
            WinHttpCloseHandle(req);
            return out;
        }
        if (sent && WinHttpReceiveResponse(req, nullptr)) {
            DWORD size = sizeof out.status;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &out.status, &size, WINHTTP_NO_HEADER_INDEX);
            for (const wchar_t* name : want_headers) {
                wchar_t value[256] = {};
                DWORD len = sizeof value;
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, value, &len, WINHTTP_NO_HEADER_INDEX)) {
                    std::string narrow;
                    for (const wchar_t* p = value; *p; ++p) narrow += char(*p);
                    std::string key;
                    for (const wchar_t* p = name; *p; ++p) key += char(*p);
                    out.headers[key] = narrow;
                }
            }
            for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail;) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
                out.body.append(chunk, 0, read);
            }
        } else {
            out.body = "WinHTTP error " + std::to_string(GetLastError());
        }
        WinHttpCloseHandle(req);
        return out;
    }

    Response json(const wchar_t* method, const std::string& path, const std::string& body = {}, const std::wstring& extra = {}) {
        return send(method, path, std::vector<unsigned char>(body.begin(), body.end()), body.empty() ? L"" : L"application/json", extra);
    }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;
    std::wstring host_;
    std::string prefix_;
    std::string pin_;
    INTERNET_PORT port_ = 0;
    bool secure_ = false;
    std::string token_;
};

// The relay handshake of an instance host: "DW4R1 HOST <ticket>\n" -> "OK <hostKey>\n".
std::string relay_host_handshake(const std::string& host, int port, const std::string& ticket) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return "WSAStartup failed";
    std::string result;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) == 0 && found) {
        SOCKET s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
        DWORD timeout = 10000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        if (s != INVALID_SOCKET && connect(s, found->ai_addr, int(found->ai_addrlen)) == 0) {
            const std::string line = "DW4R1 HOST " + ticket + "\n";
            send(s, line.data(), int(line.size()), 0);
            char c = 0;
            while (recv(s, &c, 1, 0) == 1 && c != '\n') result += c;
        } else {
            result = "connect failed";
        }
        if (s != INVALID_SOCKET) closesocket(s);
        freeaddrinfo(found);
    } else {
        result = "getaddrinfo failed";
    }
    WSACleanup();
    return result;
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view key = argv[i];
        const std::string value = argv[i + 1];
        if (key == "--base") o.base = widen(value);
        else if (key == "--user") o.user = value;
        else if (key == "--password") o.password = value;
        else if (key == "--save") o.save_path = value;
        else if (key == "--record") o.record_path = value;
        else if (key == "--stage") o.stage = value;
        else if (key == "--achievement") o.achievement = value;
        else if (key == "--protocol") o.protocol = std::atoi(value.c_str());
        else if (key == "--data-hash") o.data_hash = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "--pin") o.pin = value;
        else return false;
    }
    return !o.user.empty() && !o.password.empty() && !o.save_path.empty() && !o.record_path.empty();
}

} // namespace

// The last lines of every run, also when a failed login or room ends it early.
int finish() {
    if (g_pin_failures > 0) std::printf("PIN FAILURES %d: nothing was sent to that server\n", g_pin_failures);
    std::printf("%s (%d failed)\n", g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    Options o;
    if (!parse_args(argc, argv, o)) {
        std::fprintf(stderr, "usage: dw4online_client --base URL --user NAME --password PW --save FILE --record FILE "
                             "[--stage NAME] [--achievement CODE] [--protocol N] [--data-hash N] [--pin BASE64]\n");
        return 2;
    }
    const auto save = read_file(o.save_path);
    const auto record = read_file(o.record_path);
    step(!save.empty() && record.size() == 3316, "input files", std::to_string(save.size()) + " + " + std::to_string(record.size()) + " bytes");

    Api api(o.base, o.pin);
    step(api.ready(), "winhttp session");
    if (!api.ready()) return finish();
    const std::string context = "\"context\":{\"protocol\":" + std::to_string(o.protocol) + ",\"clientVersion\":\"" + o.client_version + "\"";

    auto r = api.json(L"GET", "/api/v1/status");
    step(r.status == 200, "status", json_string(r.body, "version").value_or(r.body));

    r = api.json(L"POST", "/api/v1/auth/login",
                 "{\"username\":\"" + escape_json(o.user) + "\",\"password\":\"" + escape_json(o.password) +
                     "\",\"deviceName\":\"dw4online_client\",\"clientVersion\":\"" + o.client_version + "\",\"protocol\":" +
                     std::to_string(o.protocol) + "}");
    const auto token = json_string(r.body, "token");
    step(r.status == 200 && token.has_value(), "login", std::to_string(r.status));
    if (!token) return finish();
    api.set_token(*token);

    r = api.json(L"GET", "/api/v1/me");
    step(r.status == 200 && json_string(r.body, "username") == o.user, "me", json_string(r.body, "username").value_or(r.body));

    // Cloud save: create, then download and compare bytes and hash.
    const std::string hash = sha256_hex(save);
    r = api.send(L"PUT", "/api/v1/cloud-save", save, L"application/octet-stream",
                 L"If-None-Match: *\r\nX-Sha256: " + widen(hash) + L"\r\nX-Device-Name: dw4online_client\r\nX-Client-Version: " +
                     widen(o.client_version) + L"\r\n");
    step(r.status == 201, "cloud save upload", std::to_string(r.status) + " revision " + std::to_string(json_number(r.body, "revision").value_or(0)));
    r = api.send(L"GET", "/api/v1/cloud-save", {}, L"", L"", {L"X-Sha256", L"ETag"});
    const std::vector<unsigned char> downloaded(r.body.begin(), r.body.end());
    step(r.status == 200 && downloaded == save && r.headers["X-Sha256"] == hash, "cloud save download",
         std::to_string(downloaded.size()) + " bytes, sha " + r.headers["X-Sha256"].substr(0, 12));

    // Online character: create with If-None-Match: *, read back.
    const std::string record64 = base64(record);
    r = api.json(L"PUT", "/api/v1/online-character", "{\"record\":\"" + record64 + "\",\"playSeconds\":3600," + context + "}}",
                 L"If-None-Match: *\r\n");
    step(r.status == 201, "online character write", std::to_string(r.status) + " revision " + std::to_string(json_number(r.body, "revision").value_or(0)));
    r = api.json(L"GET", "/api/v1/online-character");
    step(r.status == 200 && json_string(r.body, "record") == record64, "online character read",
         json_string(r.body, "digimon").value_or("?") + " level " + std::to_string(json_number(r.body, "level").value_or(0)));

    // A room, a stage instance and the relay handshake as its host.
    r = api.json(L"POST", "/api/v1/rooms",
                 "{\"name\":\"WinHTTP room\",\"maxPlayers\":4,\"protocol\":" + std::to_string(o.protocol) + ",\"dataHash\":" +
                     std::to_string(o.data_hash) + ",\"clientVersion\":\"" + o.client_version + "\"}");
    const size_t room_at = r.body.find("\"room\"");
    const auto code = room_at == std::string::npos ? std::nullopt : json_string(r.body, "code", room_at);
    step(r.status == 201 && code.has_value(), "room create", code.value_or(r.body));
    if (!code) return finish();
    r = api.json(L"POST", "/api/v1/rooms/" + *code + "/stage", "{\"stage\":\"" + escape_json(o.stage) + "\"}");
    const auto ticket = json_string(r.body, "ticket");
    const size_t relay_at = r.body.find("\"relay\"");
    const auto relay_host = relay_at == std::string::npos ? std::nullopt : json_string(r.body, "host", relay_at);
    const auto relay_port = relay_at == std::string::npos ? std::nullopt : json_number(r.body, "port", relay_at);
    step(r.status == 200 && ticket && json_string(r.body, "role") == "host", "stage enter",
         json_string(r.body, "channel").value_or(r.body));
    if (ticket && relay_host && relay_port) {
        const std::string answer = relay_host_handshake(*relay_host, int(*relay_port), *ticket);
        step(answer.rfind("OK ", 0) == 0, "relay host handshake", answer.substr(0, 3) + "<hostKey>");
    } else {
        step(false, "relay host handshake", "no ticket or relay address");
    }

    // An achievement in the room (online play only).
    r = api.json(L"POST", "/api/v1/achievements/unlock",
                 "{\"code\":\"" + escape_json(o.achievement) + "\"," + context + ",\"roomCode\":\"" + *code + "\",\"players\":1}}");
    step(r.status == 200 && json_true(r.body, "newlyUnlocked"), "achievement unlock", json_string(r.body, "ruleset").value_or(r.body));

    r = api.json(L"POST", "/api/v1/rooms/" + *code + "/leave");
    step(r.status == 204, "room leave", std::to_string(r.status));
    r = api.json(L"POST", "/api/v1/auth/logout");
    step(r.status == 204, "logout", std::to_string(r.status));
    r = api.json(L"GET", "/api/v1/me");
    step(r.status == 401, "token revoked", std::to_string(r.status));

    return finish();
}
