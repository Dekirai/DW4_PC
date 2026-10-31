// The online account's achievements (docs/SEAMLESS.md 11, src/game/
// online_achievements.hpp) against a fake request queue: the request bodies
// and the ranked context, events only (never stats or states), this machine's
// slot only, the owner's story challenges, the counter batches with their
// retries and refusals, the room check, the open reports kept for the next
// room, and the answer parsers of online/achievement_api.hpp.
#include "online_achievements.hpp"

#include "online/json.hpp"

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game;
namespace api = dw4::game::online::achievements;
namespace json = dw4::game::online::json;
using dw4::engine::PortEvent;
using Kind = PortEvent::Kind;
using dw4::game::online::HttpRequest;
using dw4::game::online::HttpResponse;

int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

// Requests are answered when the test says so.
struct FakeQueue final : api::RequestQueue {
    struct Sent {
        std::uint64_t id;
        HttpRequest request;
    };
    std::vector<Sent>* sent;
    std::deque<std::pair<std::uint64_t, HttpResponse>>* answers;
    std::uint64_t next = 1;
    std::uint64_t submit(HttpRequest request) override {
        sent->push_back({next, std::move(request)});
        return next++;
    }
    std::optional<std::pair<std::uint64_t, HttpResponse>> poll() override {
        if (answers->empty()) return std::nullopt;
        auto a = std::move(answers->front());
        answers->pop_front();
        return a;
    }
};

struct Harness {
    std::vector<FakeQueue::Sent> sent;
    std::deque<std::pair<std::uint64_t, HttpResponse>> answers;
    std::unique_ptr<OnlineAchievements> sync;
    explicit Harness(OnlineAchievementConfig config = {}) {
        if (config.room_code.empty()) config.room_code = "ABC123";
        config.protocol = 44;
        config.client_version = "0.9.0";
        auto queue = std::make_unique<FakeQueue>();
        queue->sent = &sent;
        queue->answers = &answers;
        sync = std::make_unique<OnlineAchievements>(std::move(queue), config);
    }
    void reply(std::uint64_t id, int status, std::string body) { answers.push_back({id, HttpResponse{status, std::move(body), {}, false, {}}}); }
    std::vector<FakeQueue::Sent> take(std::string_view path) {
        std::vector<FakeQueue::Sent> out, keep;
        for (auto& s : sent) (s.request.path == path ? out : keep).push_back(std::move(s));
        sent = std::move(keep);
        return out;
    }
};

// A room step: this machine's player in `own`, a partner in the other slot.
AchievementStep room_step(int own, bool owner, int members = 2) {
    AchievementStep step;
    step.stage = "stag0101";
    step.live = true;
    step.stage_serial = 1;
    step.online = true;
    step.hosting = owner;
    step.online_players = members;
    step.shared_story = true;
    step.own_slot = own;
    step.story_owner = owner;
    for (int s = 0; s < 2; ++s) {
        auto& p = step.players[std::size_t(s)];
        p.joined = true;
        p.local = s == own;
        p.unique = 500 + std::uint32_t(s);
        p.level = 1;
        p.hp = p.max_hp = 100;
    }
    return step;
}

json::Value body(const FakeQueue::Sent& s) {
    const auto v = json::parse(s.request.body);
    check(v.has_value(), "a JSON body: " + s.request.body);
    return *v;
}

void requests_and_parsers() {
    api::ReportContext c;
    c.room_code = "ROOM42";
    c.players = 2;
    c.room_players = 5;
    c.randomizer = true;
    c.difficulty = 2;
    c.protocol = 44;
    c.client_version = "1.2.3";
    api::Evidence e;
    e.igt_ms = 123456;
    e.level = 9;
    e.stage = "stagBS06";
    const auto r = api::unlock_request("story_daemon", c, e);
    check(r.method == "POST" && r.path == "/achievements/unlock" && r.content_type == "application/json" && r.authorized, "unlock request");
    const auto v = json::parse(r.body);
    check(v && (*v)["code"].str() == "story_daemon", "code");
    const auto& ctx = (*v)["context"];
    check(ctx["roomCode"].str() == "ROOM42" && ctx["players"].i64() == 2 && ctx["roomPlayers"].i64() == 5 && ctx["randomizer"].flag() == true &&
              ctx["cheats"].flag() == false && ctx["rebalanced"].flag() == false && ctx["saveStateLoaded"].flag() == false &&
              ctx["difficulty"].i64() == 2 && ctx["protocol"].i64() == 44 && ctx["clientVersion"].str() == "1.2.3",
          "the ranked context: " + r.body);
    check((*v)["evidence"]["igtMs"].i64() == 123456 && (*v)["evidence"]["level"].i64() == 9 && (*v)["evidence"]["stage"].str() == "stagBS06",
          "evidence");
    const auto bare = json::parse(api::unlock_request("online_join", c, {}).body);
    check(bare && (*bare)["evidence"].kind == json::Value::Kind::object && (*bare)["evidence"].members.empty(), "no evidence: {}");
    const auto stats = json::parse(api::stats_request({{"enemies_defeated", 3}, {"defeats", 0}, {"play_seconds", 60}}, c).body);
    check(stats && (*stats)["deltas"]["enemies_defeated"].i64() == 3 && (*stats)["deltas"]["play_seconds"].i64() == 60 &&
              !(*stats)["deltas"].find("defeats"),
          "stats: zero deltas left out");
    check(api::room_request("ROOM42").path == "/rooms/ROOM42" && api::room_request("ROOM42").method == "GET", "room request");

    const auto facts = api::parse_room_facts({200, R"({"room":{"code":"ROOM42","players":3},"you":{"owner":true}})", {}, false, {}});
    check(facts && facts->members == 3 && facts->owner, "room facts");
    const auto outsider = api::parse_room_facts({200, R"({"room":{"code":"ROOM42","players":3}})", {}, false, {}});
    check(outsider && !outsider->owner, "the public summary: not the owner");
    check(!api::parse_room_facts({404, R"({"code":"room_not_found"})", {}, false, {}}), "no room");
    check(!api::parse_room_facts({200, R"({"room":{"players":99}})", {}, false, {}}), "a member count out of range");
    const auto unlock = api::parse_unlock({200, R"({"code":"x","ruleset":"standard","newlyUnlocked":true})", {}, false, {}});
    check(unlock && unlock->newly_unlocked && unlock->ruleset == "standard", "unlock answer");
    check(api::problem_code({400, R"({"code":"requirement_not_met","title":"t"})", {}, false, {}}) == "requirement_not_met", "problem code");
    check(api::outcome_of({200, "", {}, false, {}}) == api::Outcome::done, "200 done");
    for (const int status : {0, 401, 408, 429, 500, 503}) check(api::outcome_of({status, "", {}, false, {}}) == api::Outcome::retry, "retry " + std::to_string(status));
    for (const int status : {400, 403, 404, 409, 426}) check(api::outcome_of({status, "", {}, false, {}}) == api::Outcome::refused, "refused " + std::to_string(status));
}

void events_only_for_this_slot() {
    Harness h;
    h.sync->pass(0);
    auto rooms = h.take("/rooms/ABC123");
    check(rooms.size() == 1 && rooms[0].request.method == "GET", "the room check at once");
    h.reply(rooms[0].id, 200, R"({"room":{"players":3},"you":{"owner":false}})");
    h.sync->pass(1);
    check(h.sync->room_members() == 3 && !h.sync->owner(), "room facts taken");

    auto step = room_step(1, false, 3);
    // The partner's kill and a level 10 state: nothing to report (stats batch later, states are the server's).
    step.players[1].level = 10;
    const PortEvent partner_kill[] = {{Kind::enemy_defeated, 0, 1, 0, "e_goburi"}};
    auto unlocks = h.sync->observe(step, partner_kill, AchievementGate::open, false, 100, 2);
    bool lv10 = false, join = false;
    for (const auto& u : unlocks) {
        lv10 |= std::string(u.def->id) == "lv_10";
        join |= std::string(u.def->id) == "online_join";
    }
    check(lv10 && join, "the online tracker unlocks both");
    auto posts = h.take("/achievements/unlock");
    check(posts.size() == 1 && body(posts[0])["code"].str() == "online_join", "only the event goes out (not lv_10)");
    const auto ctx = body(posts[0])["context"];
    check(ctx["roomCode"].str() == "ABC123" && ctx["players"].i64() == 2 && ctx["roomPlayers"].i64() == 3, "instance 2, room 3");
    check(h.sync->tracker().profile().stat("kills") == 0, "the partner's kill is not this machine's");
    h.reply(posts[0].id, 200, R"({"code":"online_join","ruleset":"standard","newlyUnlocked":true})");
    h.sync->pass(3);
    const auto notices = h.sync->take_notices();
    check(notices.size() == 1 && notices[0] == "Online achievement: Connected", "a notice for the new unlock");

    // A boss: was there (the guest gets the boss and the clear, not the owner's challenges).
    const PortEvent daemon[] = {{Kind::boss_defeated, -1, 0, 0, "devl3"}, {Kind::story_flag, -1, 406, 1, {}}};
    h.sync->observe(step, daemon, AchievementGate::open, false, 101, 4);
    posts = h.take("/achievements/unlock");
    std::set<std::string> codes;
    for (const auto& p : posts) codes.insert(*body(p)["code"].str());
    for (const char* id : {"boss_first", "story_daemon", "clear_normal", "nodmg_boss"}) check(codes.count(id), std::string("reported: ") + id);
    std::set<std::string> waiting;
    for (const auto& p : h.sync->pending()) waiting.insert(p.code);
    check(waiting.count("mp_couch") && waiting.count("online_raid"), "the rest waits for a free place");
    check(!codes.count("speed_fast") && !codes.count("deathless_clear"), "a guest: no story challenges");
    for (const auto& p : posts) {
        if (body(p)["code"].str() == "clear_normal")
            check(!body(p)["evidence"].find("igtMs"), "a guest's clear has no speedrun time");
        check(body(p)["evidence"]["stage"].str() == "stag0101", "the stage as evidence");
    }
    check(posts.size() == 4, "at most four in flight");
    // A closed gate: nothing.
    const PortEvent boss2[] = {{Kind::boss_defeated, -1, 0, 0, "blosso"}};
    h.sync->observe(step, boss2, AchievementGate::cheats, false, 102, 5);
    check(h.take("/achievements/unlock").empty(), "cheats: nothing reported");
}

void owner_and_evidence() {
    OnlineAchievementConfig config;
    config.owner_hint = true;
    Harness h(config);
    auto step = room_step(1, true);
    h.sync->observe(step, {}, AchievementGate::open, false, 1, 0); // tracked from the start (level 1)
    h.take("/achievements/unlock");
    for (int i = 0; i < 1000; ++i) h.sync->observe(step, {}, AchievementGate::open, false, 2, 0);
    const PortEvent clear[] = {{Kind::story_flag, -1, 406, 1, {}}};
    h.sync->observe(step, clear, AchievementGate::open, false, 3, 0);
    // Answer the first batch so the rest goes out.
    for (int round = 0; round < 3; ++round) {
        for (const auto& p : h.sent) h.reply(p.id, 200, R"({"newlyUnlocked":true,"ruleset":"standard"})");
        h.sent.clear();
        h.sync->pass(1 + round);
    }
    // (All went out by now: collect from the notices instead of the requests.)
    const auto notices = h.sync->take_notices();
    std::set<std::string> seen(notices.begin(), notices.end());
    for (const char* title : {"Online achievement: Digital World Saved", "Online achievement: Speed Demon", "Online achievement: Never Fallen",
                              "Online achievement: Rookie Pride", "Online achievement: Swift Tamer"})
        check(seen.count(title), std::string("the owner: ") + title);

    // The evidence of the owner's clear and speed goal: the character's in-game time.
    Harness t(config);
    auto s = room_step(0, true);
    t.sync->observe(s, {}, AchievementGate::open, false, 1, 0);
    for (int i = 0; i < 599; ++i) t.sync->observe(s, {}, AchievementGate::open, false, 1, 0);
    t.sent.clear();
    const PortEvent vh[] = {{Kind::story_flag, -1, 406, 1, {}}};
    t.sync->observe(s, vh, AchievementGate::open, false, 2, 0);
    bool timed_clear = false, timed_speed = false;
    for (const auto& p : t.sent) {
        const auto b = body(p);
        if (b["code"].str() == "clear_normal") timed_clear = b["evidence"]["igtMs"].i64() == std::int64_t(601) * 1001 / 60;
        if (b["code"].str() == "speed_fast") timed_speed = b["evidence"]["igtMs"].i64() == std::int64_t(601) * 1001 / 60;
    }
    check(timed_clear, "the owner's clear carries the in-game time (601 live fields, the clear's own included: 10,026 ms)");
    // speed_fast may still wait (four in flight): the deque holds it with its time.
    bool queued_speed = false;
    for (const auto& p : t.sync->pending())
        if (p.code == "speed_fast") queued_speed = p.evidence.igt_ms == std::int64_t(601) * 1001 / 60;
    check(timed_speed || queued_speed, "the speed goal carries the in-game time");

    // Underdog: the own slot's level.
    Harness u;
    auto low = room_step(1, false);
    low.players[1].level = 11;
    low.players[0].level = 80;
    const PortEvent apocalymon[] = {{Kind::boss_defeated, -1, 0, 0, "apocaly"}};
    u.sync->observe(low, apocalymon, AchievementGate::open, false, 1, 0);
    bool level = false;
    auto look = [&](const std::string& code, const api::Evidence& e) {
        if (code == "lowlevel_apocalymon") level = e.level == 11;
    };
    for (const auto& p : u.sent) {
        const auto b = body(p);
        if (b["code"].str() == "lowlevel_apocalymon") level = b["evidence"]["level"].i64() == 11;
    }
    for (const auto& p : u.sync->pending()) look(p.code, p.evidence);
    check(level, "lowlevel_apocalymon with this machine's level 11");
}

void counters_batches() {
    OnlineAchievementConfig config;
    config.stats_interval = 60;
    Harness h(config);
    h.sync->pass(0);
    h.take("/rooms/ABC123");
    auto step = room_step(0, false);
    std::vector<PortEvent> kills(30, PortEvent{Kind::enemy_defeated, 0, 1, 0, "e_numemon"});
    kills.push_back({Kind::critical_hit, 0, 0, 0, {}});
    kills.push_back({Kind::enemy_defeated, 1, 1, 0, "e_goburi"}); // the partner's
    h.sync->observe(step, kills, AchievementGate::open, false, 1, 1);
    for (int i = 0; i < 3596; ++i) h.sync->observe(step, {}, AchievementGate::open, false, 1, 1); // 3597 live fields
    h.take("/achievements/unlock");
    h.sync->pass(30);
    check(h.take("/stats").empty(), "no batch before a minute");
    h.sync->pass(60);
    auto stats = h.take("/stats");
    check(stats.size() == 1, "a batch after a minute");
    auto d = body(stats[0])["deltas"];
    check(d["enemies_defeated"].i64() == 30 && d["numemon_defeated"].i64() == 30 && d["critical_hits"].i64() == 1 &&
              d["play_seconds"].i64() == 3597 * 1001 / 60000 && !d.find("defeats") && !d.find("bosses_defeated"),
          "the deltas by the server's keys: " + stats[0].request.body);
    // No answer (a network error): the next batch carries them again, plus the new ones.
    h.reply(stats[0].id, 0, "");
    h.sync->pass(61);
    const PortEvent more[] = {{Kind::enemy_defeated, 0, 1, 0, "e_goburi"}};
    h.sync->observe(step, more, AchievementGate::open, false, 2, 62);
    h.sync->pass(121);
    stats = h.take("/stats");
    check(stats.size() == 1 && body(stats[0])["deltas"]["enemies_defeated"].i64() == 31, "a retry carries the lost deltas");
    h.reply(stats[0].id, 200, R"({"totals":{},"unlocked":[]})");
    h.sync->pass(122);
    check(h.sync->tracker().profile().stat("sent.enemies_defeated") == 31, "what the server has");
    h.sync->pass(182);
    check(h.take("/stats").empty(), "nothing new: no batch");
    // A refusal (implausible) is not sent again.
    h.sync->observe(step, more, AchievementGate::open, false, 3, 183);
    h.sync->pass(242);
    stats = h.take("/stats");
    check(stats.size() == 1 && body(stats[0])["deltas"]["enemies_defeated"].i64() == 1, "one more kill");
    h.reply(stats[0].id, 400, R"({"code":"stats_implausible"})");
    h.sync->pass(243);
    check(h.sync->tracker().profile().stat("sent.enemies_defeated") == 32, "a refused batch is dropped");
    const auto notices = h.sync->take_notices();
    check(!notices.empty() && notices.back().find("stats_implausible") != std::string::npos, "the refusal is told");
}

void retries_and_pending() {
    const auto folder = std::filesystem::temp_directory_path() / ("dw4_online_ach_" + std::to_string(std::rand()));
    OnlineAchievementConfig config;
    config.profile_file = folder / "account" / "achievements.dat";
    {
        Harness h(config);
        auto step = room_step(0, false);
        h.sync->observe(step, {}, AchievementGate::open, false, 1, 0); // online_join
        auto posts = h.take("/achievements/unlock");
        check(posts.size() == 1, "sent");
        h.reply(posts[0].id, 503, "");
        h.sync->pass(1);
        check(h.sync->pending().size() == 1 && h.sync->pending().front().attempts == 1, "a 503 waits for a retry");
        h.sync->pass(5);
        check(h.take("/achievements/unlock").empty(), "not before its time");
        h.sync->pass(11);
        posts = h.take("/achievements/unlock");
        check(posts.size() == 1, "tried again after 10 s");
        // Leaving with no answer: kept for the next room.
        h.sync->flush(12, 0.05);
        check(std::filesystem::exists(config.profile_file), "the profile is saved");
        auto pending = config.profile_file;
        pending += ".pending";
        check(std::filesystem::exists(pending), "the open report is kept");
    }
    {
        OnlineAchievementConfig next = config;
        next.room_code = "NEXT01";
        Harness h(next);
        check(h.sync->pending().size() == 1 && h.sync->pending().front().code == "online_join", "loaded in the next room");
        check(h.sync->tracker().unlocked("online_join"), "the tracker remembers the unlock");
        h.sync->pass(0);
        auto posts = h.take("/achievements/unlock");
        check(posts.size() == 1 && body(posts[0])["context"]["roomCode"].str() == "NEXT01", "sent with the new room");
        h.reply(posts[0].id, 400, R"({"code":"requirement_not_met"})");
        h.sync->flush(1, 1.0);
        auto pending = next.profile_file;
        pending += ".pending";
        check(!std::filesystem::exists(pending) && h.sync->pending().empty(), "a refusal is dropped, nothing left");
        const auto notices = h.sync->take_notices();
        check(!notices.empty() && notices.back().find("requirement_not_met") != std::string::npos, "the refusal is told");
    }
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

} // namespace

int main() {
    try {
        requests_and_parsers();
        events_only_for_this_slot();
        owner_and_evidence();
        counters_batches();
        retries_and_pending();
        std::cout << checks << " online achievement checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Online achievement test failure: " << error.what() << '\n';
        return 1;
    }
}
