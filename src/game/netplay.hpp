#pragma once

// Online co-op: lockstep sessions over TCP. The host and up to three guests
// run the same deterministic simulation from the same start; every step
// waits until the controller input of all players for that step has
// arrived. Local input is sent `delay` steps ahead, the host relays every
// player's input to the others, guests report state fingerprints so a
// desync shows. The memory cards are shared: the host's Card A and the first
// guest's Card A (as Card B) are copied to every machine at the start and
// written back to their owners when the session ends.
#include "dw4/pad.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game {

struct CardFile {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

struct SessionStart {
    int players{};            // joined machines (host included)
    int delay{};              // input delay in steps
    std::string stage;        // the stage both start with
    std::uint8_t cheats{};    // bit 0 god, 1 infinite MP, 2 one hit
    int exp_rate = 100, damage_dealt = 100, damage_taken = 100; // rebalancing
    std::uint32_t drop_seed{};                                  // randomizer
    std::vector<CardFile> card_a, card_b;
};

class NetSession {
public:
    enum class Role { offline, host, guest };
    NetSession();
    ~NetSession();
    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Host: listen and wait for players - 1 guests. Guest: connect and say
    // hello with this machine's Card A files.
    // loopback: listen on 127.0.0.1 only (test runs; no firewall prompt).
    bool host(int port, int players, int delay, std::uint64_t data_hash, std::string& error, bool loopback = false);
    bool join(const std::string& address, int port, std::uint64_t data_hash, std::vector<CardFile> card_a, std::string& error);
    // Lobby: accept / handshake until the session starts. The host passes
    // what the start message carries (its stage, cheats and Card A).
    // Returns true once started; `status` describes the wait.
    bool lobby(const SessionStart& host_start, std::string& status, std::string& error);
    const SessionStart& start() const { return start_; }

    // Ends the session (or the lobby): sockets close, the game plays on alone.
    void close();
    bool in_lobby() const { return role_ != Role::offline && !started_; }
    Role role() const { return role_; }
    bool active() const { return role_ != Role::offline && started_; }
    int local_slot() const { return slot_; }
    bool slot_joined(int slot) const;

    // Input for a step (engine frame numbers, 1 = the first step).
    void submit_local(std::uint64_t frame, const PadSample& sample);
    bool ready(std::uint64_t frame) const;
    std::array<PadSample, 4> take(std::uint64_t frame);
    // Network I/O; call every loop and while waiting.
    void poll();
    // State checks every few steps; a mismatch is reported once.
    void report_fingerprint(std::uint64_t frame, std::uint64_t value);
    std::optional<std::uint64_t> desync() const { return desync_; }
    std::string take_message(); // joins, leaves, desyncs for the screen

private:
    struct Peer;
    void send(Peer& peer, std::uint8_t type, const std::vector<std::uint8_t>& payload);
    void broadcast(std::uint8_t type, const std::vector<std::uint8_t>& payload, int except_slot = -1);
    void handle(Peer& peer, std::uint8_t type, const std::vector<std::uint8_t>& payload);
    void store_input(int slot, std::uint64_t frame, const PadSample& sample);
    void drop_peer(Peer& peer);

    Role role_ = Role::offline;
    bool started_{};
    int slot_{};
    int players_wanted_{};
    int delay_{};
    std::uint64_t data_hash_{};
    std::vector<CardFile> card_a_;
    std::intptr_t listener_ = -1;
    std::vector<std::unique_ptr<Peer>> peers_;
    SessionStart start_;
    std::array<bool, 4> joined_{};
    std::array<std::uint64_t, 4> left_from_{}; // frame from which a slot is gone (0 none)
    std::map<std::uint64_t, std::array<std::optional<PadSample>, 4>> inputs_;
    std::map<std::uint64_t, std::uint64_t> fingerprints_; // host: own values
    std::map<std::uint64_t, std::uint64_t> pending_fingerprints_;
    std::optional<std::uint64_t> desync_;
    std::deque<std::string> messages_;
};

} // namespace dw4::game
