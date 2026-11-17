#pragma once

// The online client's HTTPS transport (docs/SEAMLESS.md 10): one WinHTTP
// worker thread with a queue. Requests are synchronous on that thread, the
// game loop only submits and polls, so it never blocks on the network.
//
// Pinning (the IP phase): the server's certificate is self-signed, so the
// Windows root store does not vouch for it. Each request ignores an unknown CA
// and a name mismatch (never an expired or revoked certificate), and the
// SENDING_REQUEST callback - after the TLS handshake, before any byte of the
// request (token, password) leaves the machine - compares the SHA-256 of the
// certificate's SubjectPublicKeyInfo with the pins; a mismatch closes the
// request (WinHTTP error 12017 for the caller), so nothing is sent. Without
// pins in a pinned build every request fails closed. The domain phase
// (pinned == false) validates the chain normally.
#include "server_config.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dw4::game::online {

// Whether HttpWorker has a transport on this platform: WinHTTP on Windows;
// elsewhere (Android) every request ends at once with "no HTTPS transport on
// this platform", so nothing that needs the server may be offered there
// (online_cpu_refusal, cloud_refusal: online/cpu_gate.hpp).
#ifdef _WIN32
inline constexpr bool kHttpsTransport = true;
#else
inline constexpr bool kHttpsTransport = false;
#endif

struct HttpRequest {
    std::string method = "GET";
    std::string path;                 // below the endpoint's base path, e.g. "/rooms"
    std::string body;
    std::string content_type;         // "application/json" when body is JSON
    std::vector<std::pair<std::string, std::string>> headers;
    bool authorized = true;           // sends the bearer token when one is set
};

struct HttpResponse {
    int status = 0;                   // 0: no answer (see error)
    std::string body;
    std::string error;                // transport error text
    bool pin_mismatch = false;        // the server's key matched no pin: nothing was sent
    std::string server_pin;           // the key the server showed (diagnostics)
    bool ok() const { return status >= 200 && status < 300; }
};

// The base64 SHA-256 SPKI pin of a DER certificate (the same value as
// "DW4Online.Server tls-pin"); empty on malformed input.
std::string spki_pin_of_certificate(const std::vector<std::uint8_t>& der);

class HttpWorker {
public:
    explicit HttpWorker(ServerEndpoint endpoint, std::string user_agent = "DW4_PC");
    ~HttpWorker();
    HttpWorker(const HttpWorker&) = delete;
    HttpWorker& operator=(const HttpWorker&) = delete;

    const ServerEndpoint& endpoint() const { return endpoint_; }
    void set_token(std::string token);
    // Queues a request; the id comes back with its response.
    std::uint64_t submit(HttpRequest request);
    // A finished request, oldest first.
    std::optional<std::pair<std::uint64_t, HttpResponse>> poll();
    // Waits for one id (tools and tests; never the game loop).
    HttpResponse wait(std::uint64_t id);

private:
    void run();
    HttpResponse perform(const HttpRequest& request);

    ServerEndpoint endpoint_;
    std::string user_agent_;
    std::mutex mutex_;
    std::condition_variable wake_, done_wake_;
    std::deque<std::pair<std::uint64_t, HttpRequest>> queue_;
    std::deque<std::pair<std::uint64_t, HttpResponse>> done_;
    std::string token_;
    std::uint64_t next_id_ = 1;
    bool stop_ = false;
    void* session_ = nullptr;
    void* connect_ = nullptr;
    std::thread thread_;
};

} // namespace dw4::game::online
