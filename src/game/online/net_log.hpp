#pragma once

// saves/online.log (round 62): what an online session did, for the player to
// send in when it lags (docs/SEAMLESS.md 6.4): versions and protocols, the
// relay connection, mode changes, the input delay and why it changed, the
// link every ten seconds (round trip, input delay, late and filled pads, the
// input latency measured here), resyncs, losses and the end. Never tickets,
// tokens, passwords or room codes. The file is appended to and rotated:
// online.log grows to about 1 MB, then becomes online.1.log (online.2.log,
// online.3.log are the older ones; the oldest goes).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace dw4::game::online {

class OnlineLog {
public:
    OnlineLog() = default;
    ~OnlineLog();
    OnlineLog(const OnlineLog&) = delete;
    OnlineLog& operator=(const OnlineLog&) = delete;

    // Opens `path` for appending (rotated first when it is over `max_bytes`).
    // False when the file cannot be written (the session goes on without it).
    bool open(const std::filesystem::path& path, std::uintmax_t max_bytes = 1u << 20, int keep = 3);
    bool is_open() const { return file_ != nullptr; }
    const std::filesystem::path& path() const { return path_; }
    // One line, prefixed with the local date and time (ms).
    void line(const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    void close();

private:
    void rotate();
    std::FILE* file_ = nullptr;
    std::filesystem::path path_;
    std::uintmax_t written_ = 0, max_bytes_ = 1u << 20;
    int keep_ = 3;
};

// The rotated name of `path` with number n (online.log -> online.1.log).
std::filesystem::path rotated_log_name(const std::filesystem::path& path, int n);

} // namespace dw4::game::online
