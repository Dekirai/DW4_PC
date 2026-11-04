#pragma once

// The byte stream under a DW4R2 session: a TCP connection to the relay
// (non-blocking after the connect, TCP_NODELAY) or, in tests, an in-memory
// pipe to a fake hub.
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dw4::game::online {

class Transport {
public:
    virtual ~Transport() = default;
    // Queues bytes; false once the connection is broken.
    virtual bool send(std::span<const std::uint8_t> bytes) = 0;
    // Appends what has arrived; false once the connection is closed (after
    // the remaining bytes were handed out).
    virtual bool receive(std::vector<std::uint8_t>& into) = 0;
    virtual void close() = 0;
    // The kernel's round-trip estimate of the TCP connection in ms (Windows
    // SIO_TCP_INFO; the network alone, without the relay's or this host's
    // processing), or a negative value where there is none (a pipe, an older
    // system). The network indicator shows it when the relay sends no
    // NETSTAT (a server before round 62).
    virtual double tcp_rtt_ms() const { return -1; }
    // TCP loss episodes on this connection so far (fast retransmissions and
    // retransmission timeouts; -1: unknown).
    virtual long long tcp_retransmits() const { return -1; }
};

// Connects to host:port (blocking up to `timeout_ms`), then switches to
// non-blocking. Nothing on failure (error says why).
std::unique_ptr<Transport> connect_tcp(const std::string& host, int port, int timeout_ms, std::string& error);

// A listening TCP socket on 127.0.0.1 for tests and tools (the first free
// port of [first, last]); accept() returns connected transports.
class LoopbackListener {
public:
    LoopbackListener(int first, int last);
    ~LoopbackListener();
    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;
    int port() const { return port_; }
    std::unique_ptr<Transport> accept();

private:
    std::intptr_t socket_ = -1;
    int port_ = 0;
};

// Two connected in-memory ends (tests).
struct MemoryPipe {
    std::unique_ptr<Transport> client, server;
};
MemoryPipe make_memory_pipe();

} // namespace dw4::game::online
