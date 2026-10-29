#pragma once

// Discord Rich Presence over the desktop client's local pipes (discord_ipc.hpp
// has the framing). A worker thread owns the connection: it looks for the
// client on discord-ipc-0..9 (again every retry interval, so a Discord
// started later is found), says HANDSHAKE with the application id, sends
// SET_ACTIVITY with this process's id when the wanted activity changed (at
// most once per send interval; the newest one wins) and CLOSE when the game
// ends or the setting goes off. Without a running Discord, or with the
// setting off, nothing is sent and nothing waits: the game never blocks on
// it. No authorization is asked for (Rich Presence needs none).
//
// Every pipe transfer is overlapped and bounded: a read or write that does
// not finish within io_timeout (a hung client, a client that sends a header
// but not its payload, one that stops reading) is cancelled and the
// connection dropped; a client that does not answer the handshake within
// reply_timeout is dropped too. Such a misbehaving client is tried again
// after retry_interval, doubling for each further failure up to
// max_retry_interval (a client that is simply not running is looked for
// every retry_interval). Quitting never waits on Discord: the destructor
// wakes the worker out of any transfer, gives the CLOSE frame close_timeout
// and waits at most shutdown_timeout on top for the worker; a worker still
// busy then is left to finish alone (it owns everything it uses).
#include "discord_ipc.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace dw4::game {

class DiscordPresence {
public:
    struct Options {
        // Pipe names: \\?\pipe\<prefix>0 .. <prefix>9 (tests use their own).
        std::string pipe_prefix = "discord-ipc-";
        std::chrono::milliseconds retry_interval{15000};      // looking for the client again
        std::chrono::milliseconds max_retry_interval{240000}; // backoff cap after failures
        std::chrono::milliseconds send_interval{15000};       // between two SET_ACTIVITY
        std::chrono::milliseconds reply_timeout{3000};        // for the READY answer
        std::chrono::milliseconds io_timeout{2000};           // one frame's read or write
        std::chrono::milliseconds close_timeout{250};         // the CLOSE frame when quitting
        std::chrono::milliseconds shutdown_timeout{1000};     // the destructor's wait for the worker on top
    };
    enum class State { off, searching, connected, rejected };

    DiscordPresence();
    explicit DiscordPresence(Options options);
    ~DiscordPresence(); // sends CLOSE when connected; waits at most close_timeout + shutdown_timeout
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // On / off and the application id (the worker reconnects on a change).
    void configure(bool enabled, std::string client_id);
    // The activity to show (none: cleared).
    void set_activity(std::optional<discord::Activity> activity);

    State state() const;
    std::string status() const;        // a line for the F1 menu
    std::uint64_t sent_count() const;  // SET_ACTIVITY frames sent so far
    // Connection attempts in a row that ended in a failure (0 after a READY
    // or while no client runs); the wait before the next attempt doubles
    // with each.
    int failures() const;
    // Connections that reached READY so far.
    std::uint64_t connections() const;

    struct Shared; // the worker's state, owned together with the worker thread

private:
    std::shared_ptr<Shared> shared_;
    std::thread worker_;
};

} // namespace dw4::game
