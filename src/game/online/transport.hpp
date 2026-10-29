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
