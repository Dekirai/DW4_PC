#include "achievement_api.hpp"

#include "json.hpp"

#include <algorithm>

namespace dw4::game::online::achievements {

std::string context_json(const ReportContext& c) {
    return "{\"roomCode\":" + json::quote(c.room_code) + ",\"players\":" + std::to_string(std::clamp(c.players, 1, 10)) +
           ",\"roomPlayers\":" + std::to_string(std::clamp(c.room_players, 1, 10)) +
           ",\"randomizer\":" + (c.randomizer ? "true" : "false") + ",\"cheats\":" + (c.cheats ? "true" : "false") +
           ",\"rebalanced\":" + (c.rebalanced ? "true" : "false") + ",\"saveStateLoaded\":false,\"difficulty\":" +
           std::to_string(std::clamp(c.difficulty, 0, 2)) +
           ",\"protocol\":" + std::to_string(c.protocol) + ",\"clientVersion\":" + json::quote(c.client_version) + "}";
}

namespace {
HttpRequest post(std::string path, std::string body) {
    HttpRequest r;
    r.method = "POST";
    r.path = std::move(path);
    r.body = std::move(body);
    r.content_type = "application/json";
    return r;
}
} // namespace

HttpRequest unlock_request(const std::string& code, const ReportContext& context, const Evidence& evidence) {
    std::string e = "{";
    auto add = [&](const std::string& field) {
        if (e.size() > 1) e += ',';
        e += field;
    };
    if (evidence.igt_ms) add("\"igtMs\":" + std::to_string(*evidence.igt_ms));
    if (evidence.level) add("\"level\":" + std::to_string(*evidence.level));
    if (!evidence.stage.empty()) add("\"stage\":" + json::quote(evidence.stage));
    e += '}';
    return post("/achievements/unlock", "{\"code\":" + json::quote(code) + ",\"context\":" + context_json(context) + ",\"evidence\":" + e + "}");
}

HttpRequest stats_request(const std::map<std::string, std::int64_t>& deltas, const ReportContext& context) {
    std::string d = "{";
    for (const auto& [key, value] : deltas) {
        if (value <= 0) continue;
        if (d.size() > 1) d += ',';
        d += json::quote(key) + ":" + std::to_string(value);
    }
    d += '}';
    return post("/stats", "{\"deltas\":" + d + ",\"context\":" + context_json(context) + "}");
}

HttpRequest room_request(const std::string& code) {
    HttpRequest r;
    r.path = "/rooms/" + code;
    return r;
}

std::optional<RoomFacts> parse_room_facts(const HttpResponse& response) {
    if (!response.ok()) return std::nullopt;
    const auto v = json::parse(response.body);
    if (!v) return std::nullopt;
    const auto players = (*v)["room"]["players"].i64();
    if (!players || *players < 0 || *players > 10) return std::nullopt;
    RoomFacts facts;
    facts.members = int(*players);
    // Only a member gets "you" (the public summary has none).
    facts.owner = (*v)["you"]["owner"].flag().value_or(false);
    return facts;
}

std::optional<UnlockAnswer> parse_unlock(const HttpResponse& response) {
    if (!response.ok()) return std::nullopt;
    const auto v = json::parse(response.body);
    if (!v) return std::nullopt;
    UnlockAnswer a;
    a.newly_unlocked = (*v)["newlyUnlocked"].flag().value_or(false);
    a.ruleset = (*v)["ruleset"].str().value_or("");
    return a;
}

std::string problem_code(const HttpResponse& response) {
    if (response.ok()) return {};
    if (const auto v = json::parse(response.body))
        if (const auto code = (*v)["code"].str()) return *code;
    return {};
}

Outcome outcome_of(const HttpResponse& response) {
    if (response.ok()) return Outcome::done;
    // (401: a session that ran out; the next room logs in again.)
    if (response.status == 0 || response.status == 401 || response.status == 408 || response.status == 429 || response.status >= 500)
        return Outcome::retry;
    return Outcome::refused;
}

} // namespace dw4::game::online::achievements
