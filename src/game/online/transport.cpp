#include "transport.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace dw4::game::online {

namespace {
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalid = INVALID_SOCKET;
void close_socket(socket_t s) { closesocket(s); }
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
struct WinsockInit {
    WinsockInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
};
void ensure_winsock() { static WinsockInit init; }
#else
using socket_t = int;
constexpr socket_t kInvalid = -1;
void close_socket(socket_t s) { ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
void ensure_winsock() {}
#endif

void set_non_blocking(socket_t s, bool on) {
#ifdef _WIN32
    u_long value = on ? 1 : 0;
    ioctlsocket(s, FIONBIO, &value);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

class TcpTransport final : public Transport {
public:
    explicit TcpTransport(socket_t s) : socket_(s) {
        set_non_blocking(socket_, true);
        int one = 1;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
    }
    ~TcpTransport() override { close(); }
    bool send(std::span<const std::uint8_t> bytes) override {
        if (socket_ == kInvalid) return false;
        pending_.insert(pending_.end(), bytes.begin(), bytes.end());
        return flush();
    }
    bool receive(std::vector<std::uint8_t>& into) override {
        if (socket_ == kInvalid) return false;
        if (!flush()) return false;
        std::uint8_t buffer[16384];
        for (;;) {
            const int n = int(::recv(socket_, reinterpret_cast<char*>(buffer), int(sizeof buffer), 0));
            if (n > 0) {
                into.insert(into.end(), buffer, buffer + n);
                continue;
            }
            if (n == 0) { // orderly close
                close();
                return false;
            }
            if (would_block()) return true;
            close();
            return false;
        }
    }
    void close() override {
        if (socket_ != kInvalid) close_socket(socket_);
        socket_ = kInvalid;
    }
    double tcp_rtt_ms() const override {
        const auto info = tcp_info();
        return info ? info->first : -1.0;
    }
    long long tcp_retransmits() const override {
        const auto info = tcp_info();
        return info ? info->second : -1;
    }

private:
    // (rtt ms, loss episodes) from SIO_TCP_INFO (Windows 10 1703 and later).
    std::optional<std::pair<double, long long>> tcp_info() const {
#if defined(_WIN32) && defined(SIO_TCP_INFO)
        if (socket_ == kInvalid) return std::nullopt;
        DWORD version = 0, returned = 0;
        TCP_INFO_v0 info{};
        if (WSAIoctl(socket_, SIO_TCP_INFO, &version, sizeof version, &info, sizeof info, &returned, nullptr, nullptr) != 0 ||
            returned < sizeof info)
            return std::nullopt;
        // (Loss episodes: fast retransmissions and retransmission timeouts.)
        return std::pair{double(info.RttUs) / 1000.0, (long long)info.FastRetrans + (long long)info.TimeoutEpisodes};
#else
        return std::nullopt;
#endif
    }
    bool flush() {
        while (!pending_.empty()) {
            const int n = int(::send(socket_, reinterpret_cast<const char*>(pending_.data()), int(pending_.size()), 0));
            if (n > 0) {
                pending_.erase(pending_.begin(), pending_.begin() + n);
                continue;
            }
            if (n < 0 && would_block()) return true;
            close();
            return false;
        }
        return true;
    }
    socket_t socket_;
    std::vector<std::uint8_t> pending_;
};

// In-memory pipe: two queues guarded by one mutex (the fake hub may run on
// the test's thread or another).
struct PipeState {
    std::mutex mutex;
    std::deque<std::uint8_t> to_server, to_client;
    bool closed = false;
};

class PipeEnd final : public Transport {
public:
    PipeEnd(std::shared_ptr<PipeState> state, bool client) : state_(std::move(state)), client_(client) {}
    ~PipeEnd() override { close(); }
    bool send(std::span<const std::uint8_t> bytes) override {
        std::lock_guard lock(state_->mutex);
        if (state_->closed) return false;
        auto& queue = client_ ? state_->to_server : state_->to_client;
        queue.insert(queue.end(), bytes.begin(), bytes.end());
        return true;
    }
    bool receive(std::vector<std::uint8_t>& into) override {
        std::lock_guard lock(state_->mutex);
        auto& queue = client_ ? state_->to_client : state_->to_server;
        into.insert(into.end(), queue.begin(), queue.end());
        queue.clear();
        return !state_->closed;
    }
    void close() override {
        std::lock_guard lock(state_->mutex);
        state_->closed = true;
    }

private:
    std::shared_ptr<PipeState> state_;
    bool client_;
};
} // namespace

std::unique_ptr<Transport> connect_tcp(const std::string& host, int port, int timeout_ms, std::string& error) {
    ensure_winsock();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST; // the address is a build constant or a loopback literal, never a name from a server
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "bad relay address " + host;
        return nullptr;
    }
    socket_t s = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (s == kInvalid) {
        freeaddrinfo(found);
        error = "no socket";
        return nullptr;
    }
    set_non_blocking(s, true);
    const int rc = ::connect(s, found->ai_addr, int(found->ai_addrlen));
    freeaddrinfo(found);
    if (rc != 0 && !would_block()) {
        close_socket(s);
        error = "the relay refused the connection";
        return nullptr;
    }
    if (rc != 0) {
        fd_set writable, failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(s, &writable);
        FD_SET(s, &failed);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        const int ready = ::select(int(s) + 1, nullptr, &writable, &failed, &tv);
        int so_error = 0;
        socklen_t length = sizeof so_error;
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &length);
        if (ready <= 0 || FD_ISSET(s, &failed) || so_error != 0) {
            close_socket(s);
            error = ready == 0 ? "the relay did not answer" : "the relay refused the connection";
            return nullptr;
        }
    }
    return std::make_unique<TcpTransport>(s);
}

LoopbackListener::LoopbackListener(int first, int last) {
    ensure_winsock();
    for (int port = first; port <= last; ++port) {
        socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kInvalid) return;
#ifdef _WIN32
        BOOL exclusive = TRUE; // never share a port another process holds
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(std::uint16_t(port));
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 && ::listen(s, 8) == 0) {
            set_non_blocking(s, true);
            socket_ = std::intptr_t(s);
            port_ = port;
            return;
        }
        close_socket(s);
    }
}

LoopbackListener::~LoopbackListener() {
    if (socket_ != -1) close_socket(socket_t(socket_));
}

std::unique_ptr<Transport> LoopbackListener::accept() {
    if (socket_ == -1) return nullptr;
    const socket_t s = ::accept(socket_t(socket_), nullptr, nullptr);
    if (s == kInvalid) return nullptr;
    return std::make_unique<TcpTransport>(s);
}

MemoryPipe make_memory_pipe() {
    auto state = std::make_shared<PipeState>();
    return {std::make_unique<PipeEnd>(state, true), std::make_unique<PipeEnd>(state, false)};
}

} // namespace dw4::game::online
