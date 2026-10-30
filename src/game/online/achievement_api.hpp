#pragma once

// The DW4Online calls of the online achievements (docs/SEAMLESS.md 11):
//
//   POST /achievements/unlock  {code, context, evidence}  an event achievement
//   POST /stats                {deltas, context}           counters, about once a minute
//   GET  /rooms/{code}                                      the room's members and whether this account owns it
//
// The context is the ranked context of the report: the room, the players of
// the reporter's stage instance (players) and of the room (roomPlayers), the
// room's randomizer, difficulty, protocol and client version; cheats,
// rebalancing and save states are never on online. Request builders and
// answer parsers are separate from the transport (tested without a server);
// RequestQueue is the transport the achievement sync uses, an HttpWorker of
// its own in the game.
#include "http.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace dw4::game::online::achievements {

struct ReportContext {
    std::string room_code;
    int players = 1;       // in the reporter's stage instance (1-4)
    int room_players = 1;  // in the room (1-10)
    bool randomizer = false;
    int difficulty = 0;
    int protocol = 0;
    std::string client_version;
};

struct Evidence {
    std::optional<std::int64_t> igt_ms; // in-game time without loads (story clears, speed goals)
    std::optional<int> level;           // the reporter's level (low-level goals)
    std::string stage;                  // the stage folder (stored as evidence only)
};

HttpRequest unlock_request(const std::string& code, const ReportContext& context, const Evidence& evidence);
// Counter deltas by the server's keys (enemies_defeated, play_seconds, ...); zero entries are left out.
HttpRequest stats_request(const std::map<std::string, std::int64_t>& deltas, const ReportContext& context);
HttpRequest room_request(const std::string& code);

struct RoomFacts {
    int members = 0;
    bool owner = false;
};
std::optional<RoomFacts> parse_room_facts(const HttpResponse& response);

struct UnlockAnswer {
    bool newly_unlocked = false;
    std::string ruleset;
};
std::optional<UnlockAnswer> parse_unlock(const HttpResponse& response);

// The problem's "code" ("requirement_not_met", "not_in_room", ...); empty without one.
std::string problem_code(const HttpResponse& response);

// What to do with a report the server answered: done (2xx), refused for good
// (a 4xx other than 401 / 408 / 429: the server will never take it), or try
// again later (no answer, 401, 408, 429, 5xx).
enum class Outcome { done, refused, retry };
Outcome outcome_of(const HttpResponse& response);

// The transport of the reports: submit, then poll the answers.
class RequestQueue {
public:
    virtual ~RequestQueue() = default;
    virtual std::uint64_t submit(HttpRequest request) = 0;
    virtual std::optional<std::pair<std::uint64_t, HttpResponse>> poll() = 0;
};

// An HttpWorker of its own (its answers are never taken by another poller).
class HttpQueue final : public RequestQueue {
public:
    explicit HttpQueue(std::unique_ptr<HttpWorker> worker) : worker_(std::move(worker)) {}
    std::uint64_t submit(HttpRequest request) override { return worker_->submit(std::move(request)); }
    std::optional<std::pair<std::uint64_t, HttpResponse>> poll() override { return worker_->poll(); }

private:
    std::unique_ptr<HttpWorker> worker_;
};

} // namespace dw4::game::online::achievements
