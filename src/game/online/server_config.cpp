#include "server_config.hpp"

#include "server_pins.hpp"

#include <charconv>
#include <cstdlib>

namespace dw4::game::online {

std::string ServerEndpoint::url() const {
    std::string text = "https://" + host;
    if (port != 443) text += ":" + std::to_string(port);
    return text + base_path;
}

std::vector<std::string> split_pins(const std::string& list) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= list.size()) {
        const std::size_t comma = std::min(list.find(',', at), list.size());
        std::string item = list.substr(at, comma - at);
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (!item.empty()) out.push_back(item);
        at = comma + 1;
    }
    return out;
}

ServerEndpoint release_endpoint() {
    ServerEndpoint e;
#ifdef DW4_ONLINE_USE_DOMAIN
    e.host = "dekirai.crygod.de";
    e.base_path = "/dw4online/api/v1";
    e.pinned = false;
#else
    e.host = "88.198.52.221";
    e.base_path = "/api/v1";
    e.pinned = true;
    for (const char* pin : {kServerPinInUse, kServerPinSpare})
        if (*pin) e.pins.emplace_back(pin);
#endif
    return e;
}

std::optional<ServerEndpoint> development_endpoint(const std::string& server, const std::string& pins) {
    std::string host;
    std::string port_text;
    if (!server.empty() && server.front() == '[') { // [::1]:port
        const auto close = server.find(']');
        if (close == std::string::npos || close + 1 >= server.size() || server[close + 1] != ':') return std::nullopt;
        host = server.substr(1, close - 1);
        port_text = server.substr(close + 2);
    } else {
        const auto colon = server.rfind(':');
        if (colon == std::string::npos) return std::nullopt;
        host = server.substr(0, colon);
        port_text = server.substr(colon + 1);
    }
    int port = 0;
    const auto [end, error] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (error != std::errc{} || end != port_text.data() + port_text.size() || port <= 0 || port > 65535) return std::nullopt;
    const bool loopback = host == "localhost" || host == "::1" || host.rfind("127.", 0) == 0;
    if (!loopback) return std::nullopt;
    if (host.rfind("127.", 0) == 0) { // four decimal parts
        int parts = 0;
        std::size_t at = 0;
        while (at <= host.size()) {
            const std::size_t dot = std::min(host.find('.', at), host.size());
            int value = -1;
            const auto [e2, err2] = std::from_chars(host.data() + at, host.data() + dot, value);
            if (err2 != std::errc{} || e2 != host.data() + dot || value < 0 || value > 255) return std::nullopt;
            ++parts;
            at = dot + 1;
        }
        if (parts != 4) return std::nullopt;
    }
    ServerEndpoint e;
    e.host = host == "localhost" ? "127.0.0.1" : host;
    e.port = port;
    e.base_path = "/api/v1";
    e.pinned = true;
    e.pins = split_pins(pins);
    e.development = true;
    return e;
}

ServerEndpoint server_endpoint() {
#ifdef DW4_ONLINE_DEV
    const char* server = std::getenv("DW4_ONLINE_SERVER");
    const char* pins = std::getenv("DW4_ONLINE_PIN");
    if (server && *server)
        if (auto dev = development_endpoint(server, pins ? pins : "")) return *dev;
#endif
    return release_endpoint();
}

} // namespace dw4::game::online
