#pragma once

// Where the online client talks to (docs/SEAMLESS.md 1.1, 6.2, 10). The
// address is a build constant and not user-changeable:
//   now:   https://88.198.52.221:8443, API at /api/v1, the self-signed certificate
//          checked by its SPKI pin (server_pins.hpp);
//   later: https://dekirai.crygod.de/dw4online (build with
//          DW4_ONLINE_USE_DOMAIN), API at /dw4online/api/v1, the ordinary
//          certificate chain.
// The relay is reached at the same host on the port GET /status announces; a
// host name from the server is never used.
//
// Development builds only (DW4_ONLINE_DEV): DW4_ONLINE_SERVER=127.0.0.1:<port>
// (a loopback address only, else ignored) and DW4_ONLINE_PIN=<base64[,base64]>
// point the client at a local test stack; the pinning path stays in use, so a
// test server runs HTTPS with a throwaway certificate.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game::online {

struct ServerEndpoint {
    std::string host;                // "88.198.52.221"
    int port = 443;
    std::string base_path;           // "/api/v1"
    bool pinned = true;              // SPKI pin instead of the chain
    std::vector<std::string> pins;   // base64 SHA-256 SPKI, in use first
    bool development = false;        // a DW4_ONLINE_DEV loopback override
    std::string url() const;         // https://host[:port] + base path (for messages)
};

// The build's endpoint (with the development override where allowed).
ServerEndpoint server_endpoint();
// The release endpoint, ignoring any override (tests).
ServerEndpoint release_endpoint();
// A development override "host:port" accepted only for loopback hosts
// (127.0.0.0/8, ::1, localhost); nothing otherwise.
std::optional<ServerEndpoint> development_endpoint(const std::string& server, const std::string& pins);

// "a,b" -> {"a", "b"} (empty entries dropped).
std::vector<std::string> split_pins(const std::string& list);

} // namespace dw4::game::online
