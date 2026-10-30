#pragma once

// The online account's achievements in a room (docs/SEAMLESS.md 11): a
// second AchievementTracker, with its own profile per account, counts this
// machine's player with the rules of the local tracker and reports to the
// server:
//
//   unlocks   an event achievement (story, bosses, challenges, techniques,
//             quests, together) goes out at once with POST
//             /achievements/unlock and the ranked context; stat and state
//             achievements are the server's (counters, the uploaded online
//             character) and are never reported;
//   stats     the profile's counters (kills, critical hits, Numemon, bosses,
//             defeats, play time) as deltas every 60 s with POST /stats;
//   room      GET /rooms/{code} every 30 s: the room's members (Room Host,
//             Raid Night) and whether this account owns the room (the story
//             is the owner's: its challenges go only to the owner's machine).
//
// Only this machine's slot counts (never p1), and only live ticks the member
// plays itself: a catch-up replays without port events (Engine::set_replay),
// and the host calls observe() only while the session runs the member's own
// live ticks (solo or sealed), so story credits are "was there" credits of
// the instance it happened in. A report the server refuses for good is
// dropped; one it did not answer is tried again, and the open ones are kept
// in <profile>.pending for the next room.
#include "achievements.hpp"
#include "online/achievement_api.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dw4::game {

struct OnlineAchievementConfig {
    std::string room_code;
    bool owner_hint = false;            // this account opened the room (until GET /rooms answers)
    bool randomizer = false;            // the room's randomizer (its own ruleset on the server)
    int protocol = 0;
    std::string client_version;
    std::filesystem::path profile_file; // the account's cache (saves/online/<account>/achievements.dat); empty: memory only
    double stats_interval = 60.0;       // seconds between counter batches
    double room_interval = 30.0;        // seconds between room checks
    int max_in_flight = 4;              // unlock reports waiting for an answer at once
};

class OnlineAchievements {
public:
    OnlineAchievements(std::unique_ptr<online::achievements::RequestQueue> queue, OnlineAchievementConfig config);

    // The facts the host puts into the step (AchievementStep.story_owner, hosting, online_players).
    bool owner() const { return owner_; }
    int room_members() const { return room_members_; }
    // The ranked context of a report now (the room, the instance's and the room's players, the room's
    // randomizer, the difficulty of the last step): also the context of an online character upload.
    online::achievements::ReportContext report_context() const;

    // One live step of this machine's member (after the local tracker, with
    // the same events). Returns the unlocks of the online tracker (all kinds;
    // only events are reported).
    std::vector<AchievementUnlock> observe(const AchievementStep& step, std::span<const engine::PortEvent> events, AchievementGate gate,
                                           bool randomizer, std::int64_t now_unix, double now);
    // Answers, the room check, the counter batch, retries. Every loop pass.
    void pass(double now);
    // Leaving the room (or the game): the last counter batch and the open
    // unlocks, waiting up to `seconds` for the answers; whatever is left
    // stays in <profile>.pending. Saves the profile.
    void flush(double now, double seconds);
    bool save();

    // Lines for the player and the test traces ("Online achievement: <title>", refusals).
    std::vector<std::string> take_notices();
    const AchievementTracker& tracker() const { return tracker_; }
    struct Pending {
        std::string code;
        online::achievements::Evidence evidence;
        double not_before = 0;
        int attempts = 0;
    };
    const std::deque<Pending>& pending() const { return pending_; }
    std::size_t in_flight() const { return unlocks_in_flight_.size() + (stats_in_flight_ ? 1 : 0) + (room_in_flight_ ? 1 : 0); }

private:
    std::map<std::string, std::int64_t> counters() const; // the server's keys, cumulative
    void send_stats(double now);
    void submit_unlocks(double now);
    void answer(std::uint64_t id, const online::HttpResponse& response, double now);
    void load_pending();
    bool save_pending() const;

    std::unique_ptr<online::achievements::RequestQueue> queue_;
    OnlineAchievementConfig config_;
    AchievementTracker tracker_;
    bool owner_;
    int room_members_ = 1;
    int instance_players_ = 1;
    int difficulty_ = 0;

    std::deque<Pending> pending_;
    std::map<std::uint64_t, Pending> unlocks_in_flight_;
    std::optional<std::uint64_t> stats_in_flight_;
    std::map<std::string, std::int64_t> stats_sending_;
    std::optional<std::uint64_t> room_in_flight_;
    double next_stats_ = 0, next_room_ = 0, next_save_ = 0;
    bool started_ = false;
    std::vector<std::string> notices_;
};

} // namespace dw4::game
