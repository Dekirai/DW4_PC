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
#include "discord_ipc.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace dw4::game {

class DiscordPresence {
public:
    struct Options {
        // Pipe names: \\?\pipe\<prefix>0 .. <prefix>9 (tests use their own).
        std::string pipe_prefix = "discord-ipc-";
        std::chrono::milliseconds retry_interval{15000}; // looking for the client again
        std::chrono::milliseconds send_interval{15000};  // between two SET_ACTIVITY
        std::chrono::milliseconds reply_timeout{3000};   // for the READY answer
    };
    enum class State { off, searching, connected, rejected };

    DiscordPresence();
    explicit DiscordPresence(Options options);
    ~DiscordPresence(); // sends CLOSE when connected, then joins the worker
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // On / off and the application id (the worker reconnects on a change).
    void configure(bool enabled, std::string client_id);
    // The activity to show (none: cleared).
    void set_activity(std::optional<discord::Activity> activity);

    State state() const;
    std::string status() const;        // a line for the F1 menu
    std::uint64_t sent_count() const;  // SET_ACTIVITY frames sent so far

private:
    struct Pipe;
    void run();
    bool connect(std::unique_lock<std::mutex>& lock);
    void disconnect(bool say_close);
    bool write_frame(discord::Opcode opcode, const std::string& json);
    // Reads waiting frames (answers PING); false when the pipe broke or the
    // client closed it.
    bool drain();

    Options options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_{};
    bool enabled_{};
    std::string client_id_;
    std::uint64_t config_serial_{};
    std::optional<discord::Activity> wanted_;
    std::uint64_t wanted_serial_{};
    State state_ = State::off;
    std::string status_ = "Off";
    std::uint64_t sent_{};
    // Worker only.
    std::unique_ptr<Pipe> pipe_;
    std::uint64_t connected_config_{};
    std::uint64_t sent_serial_{};
    bool sent_any_{};
    std::uint64_t nonce_{};
    std::chrono::steady_clock::time_point next_attempt_{}, last_send_{};
    std::thread worker_;
};

} // namespace dw4::game
