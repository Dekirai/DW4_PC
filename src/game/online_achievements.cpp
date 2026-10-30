#include "online_achievements.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

namespace dw4::game {

namespace api = online::achievements;

namespace {
constexpr const char* kSentPrefix = "sent."; // profile stats: what the server already has of each counter
constexpr double kSaveSeconds = 120.0;

std::filesystem::path pending_file(const std::filesystem::path& profile) {
    auto file = profile;
    file += ".pending";
    return file;
}

bool is_clear(std::string_view code) { return code == "clear_normal" || code == "clear_hard" || code == "clear_veryhard"; }
} // namespace

OnlineAchievements::OnlineAchievements(std::unique_ptr<api::RequestQueue> queue, OnlineAchievementConfig config)
    : queue_(std::move(queue)), config_(std::move(config)), owner_(config_.owner_hint) {
    if (!config_.profile_file.empty())
        if (auto profile = AchievementProfile::load(config_.profile_file)) tracker_ = AchievementTracker(std::move(*profile));
    load_pending();
}

api::ReportContext OnlineAchievements::context() const {
    api::ReportContext c;
    c.room_code = config_.room_code;
    c.players = std::clamp(instance_players_, 1, 4);
    c.room_players = std::clamp(std::max(room_members_, instance_players_), 1, 10);
    c.randomizer = config_.randomizer;
    c.difficulty = difficulty_;
    c.protocol = config_.protocol;
    c.client_version = config_.client_version;
    return c;
}

std::map<std::string, std::int64_t> OnlineAchievements::counters() const {
    std::map<std::string, std::int64_t> out;
    for (const auto& counter : achievement_online_counters()) {
        std::int64_t value = tracker_.profile().stat(counter.profile_stat);
        if (std::string_view(counter.profile_stat) == "play_fields") value = value * 1001 / 60000; // whole seconds at 59.94 Hz
        out[counter.server_stat] += value;
    }
    return out;
}

std::vector<AchievementUnlock> OnlineAchievements::observe(const AchievementStep& step, std::span<const engine::PortEvent> events,
                                                           AchievementGate gate, bool randomizer, std::int64_t now_unix, double now) {
    int joined = 0;
    for (const auto& p : step.players) joined += p.joined ? 1 : 0;
    instance_players_ = std::max(1, joined);
    difficulty_ = step.difficulty;
    auto unlocks = tracker_.observe(step, events, gate, randomizer, now_unix);
    const int own = std::clamp(step.own_slot, 0, 3);
    const auto& me = step.players[std::size_t(own)];
    for (const auto& unlock : unlocks) {
        if (!unlock.def) continue;
        const auto* online = find_achievement_online(unlock.def->id);
        if (!online || online->kind != AchievementOnlineKind::event) continue; // the server's own (counters, the character)
        Pending p;
        p.code = unlock.def->id;
        p.evidence.stage = step.stage;
        if (p.code == "lowlevel_apocalymon") p.evidence.level = me.level;
        // The story character's in-game time: speed goals, and the owner's clears for the speedrun boards (a guest's
        // clear is a "was there" credit without a time).
        const bool timed = p.code == "speed_normal" || p.code == "speed_fast" || (is_clear(p.code) && step.story_owner);
        if (timed) {
            const auto& characters = tracker_.profile().characters;
            if (const auto found = characters.find(me.unique);
                found != characters.end() && found->second.from_start && !found->second.assisted && found->second.play_frames > 0)
                p.evidence.igt_ms = found->second.play_frames * 1001 / 60;
        }
        p.not_before = now;
        pending_.push_back(std::move(p));
    }
    submit_unlocks(now);
    return unlocks;
}

void OnlineAchievements::submit_unlocks(double now) {
    for (auto it = pending_.begin(); it != pending_.end() && int(unlocks_in_flight_.size()) < config_.max_in_flight;) {
        if (it->not_before > now) {
            ++it;
            continue;
        }
        const auto id = queue_->submit(api::unlock_request(it->code, context(), it->evidence));
        unlocks_in_flight_[id] = std::move(*it);
        it = pending_.erase(it);
    }
}

void OnlineAchievements::send_stats(double) {
    if (stats_in_flight_) return;
    const auto now_counters = counters();
    std::map<std::string, std::int64_t> deltas;
    for (const auto& [key, value] : now_counters) {
        const std::int64_t delta = value - tracker_.profile().stat(std::string(kSentPrefix) + key);
        if (delta > 0) deltas[key] = delta;
    }
    if (deltas.empty()) return;
    stats_sending_ = now_counters;
    stats_in_flight_ = queue_->submit(api::stats_request(deltas, context()));
}

void OnlineAchievements::answer(std::uint64_t id, const online::HttpResponse& response, double now) {
    const auto outcome = api::outcome_of(response);
    if (room_in_flight_ && *room_in_flight_ == id) {
        room_in_flight_.reset();
        if (const auto facts = api::parse_room_facts(response)) {
            room_members_ = std::max(1, facts->members);
            owner_ = facts->owner;
        }
        return;
    }
    if (stats_in_flight_ && *stats_in_flight_ == id) {
        stats_in_flight_.reset();
        if (outcome == api::Outcome::retry) return; // the next batch carries these too
        // Taken, or refused for good (implausible, unranked): either way the server will not count them again.
        for (const auto& [key, value] : stats_sending_) tracker_.profile().stats[std::string(kSentPrefix) + key] = value;
        if (outcome == api::Outcome::refused)
            notices_.push_back("Online counters refused: " + api::problem_code(response) + " (HTTP " + std::to_string(response.status) + ")");
        return;
    }
    const auto found = unlocks_in_flight_.find(id);
    if (found == unlocks_in_flight_.end()) return;
    Pending p = std::move(found->second);
    unlocks_in_flight_.erase(found);
    const auto* def = find_achievement(p.code);
    const std::string title = def ? def->title : p.code;
    switch (outcome) {
    case api::Outcome::done:
        if (const auto a = api::parse_unlock(response); a && a->newly_unlocked)
            notices_.push_back("Online achievement: " + title + (a->ruleset == "randomizer" ? " (randomizer)" : ""));
        break;
    case api::Outcome::refused:
        notices_.push_back("Online achievement " + p.code + " refused: " + api::problem_code(response) + " (HTTP " +
                           std::to_string(response.status) + ")");
        break;
    case api::Outcome::retry:
        ++p.attempts;
        p.not_before = now + std::min(300.0, 5.0 * double(1 << std::min(p.attempts, 6)));
        pending_.push_back(std::move(p));
        break;
    }
}

void OnlineAchievements::pass(double now) {
    if (!started_) {
        started_ = true;
        next_room_ = now;
        next_stats_ = now + config_.stats_interval;
        next_save_ = now + kSaveSeconds;
    }
    while (auto done = queue_->poll()) answer(done->first, done->second, now);
    if (now >= next_room_ && !room_in_flight_) {
        room_in_flight_ = queue_->submit(api::room_request(config_.room_code));
        next_room_ = now + config_.room_interval;
    }
    if (now >= next_stats_) {
        send_stats(now);
        next_stats_ = now + config_.stats_interval;
    }
    submit_unlocks(now);
    if (now >= next_save_) {
        save();
        next_save_ = now + kSaveSeconds;
    }
}

void OnlineAchievements::flush(double now, double seconds) {
    while (auto done = queue_->poll()) answer(done->first, done->second, now);
    send_stats(now);
    // Every open unlock goes out once more; one the server does not answer now
    // waits (its retry time lies after `now`) for the next room.
    for (auto& p : pending_) p.not_before = std::min(p.not_before, now);
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        submit_unlocks(now);
        bool any = false;
        while (auto done = queue_->poll()) {
            any = true;
            answer(done->first, done->second, now);
        }
        const bool ready = std::any_of(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.not_before <= now; });
        if (!stats_in_flight_ && unlocks_in_flight_.empty() && !ready) break;
        if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Answers that did not come in time are never taken: those unlocks stay pending.
    for (auto& [id, p] : unlocks_in_flight_) pending_.push_back(std::move(p));
    unlocks_in_flight_.clear();
    stats_in_flight_.reset();
    room_in_flight_.reset();
    save();
}

bool OnlineAchievements::save() {
    if (config_.profile_file.empty()) return true;
    std::error_code error;
    std::filesystem::create_directories(config_.profile_file.parent_path(), error);
    const bool profile = tracker_.profile().save(config_.profile_file);
    return save_pending() && profile;
}

void OnlineAchievements::load_pending() {
    if (config_.profile_file.empty()) return;
    std::ifstream in(pending_file(config_.profile_file), std::ios::binary);
    std::string line;
    if (!in || !std::getline(in, line) || line.rfind("DW4PENDING 1", 0) != 0) return;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string kind, code, stage;
        std::int64_t igt = -1;
        int level = -1;
        if (!(fields >> kind >> code >> igt >> level >> stage) || kind != "unlock") continue;
        const auto* online = find_achievement_online(code);
        if (!online || online->kind != AchievementOnlineKind::event || pending_.size() >= 256) continue;
        Pending p;
        p.code = code;
        if (igt >= 0) p.evidence.igt_ms = igt;
        if (level >= 0) p.evidence.level = level;
        if (stage != "-" && stage.size() <= 16) p.evidence.stage = stage;
        pending_.push_back(std::move(p));
    }
}

bool OnlineAchievements::save_pending() const {
    const auto file = pending_file(config_.profile_file);
    std::error_code error;
    if (pending_.empty() && unlocks_in_flight_.empty()) {
        std::filesystem::remove(file, error);
        return true;
    }
    std::ostringstream out;
    out << "DW4PENDING 1\n";
    auto line = [&](const Pending& p) {
        out << "unlock " << p.code << ' ' << p.evidence.igt_ms.value_or(-1) << ' ' << p.evidence.level.value_or(-1) << ' '
            << (p.evidence.stage.empty() ? std::string("-") : p.evidence.stage) << '\n';
    };
    for (const auto& p : pending_) line(p);
    for (const auto& [id, p] : unlocks_in_flight_) line(p);
    auto temporary = file;
    temporary += ".tmp";
    {
        std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
        if (!(f << out.str())) return false;
    }
    std::filesystem::rename(temporary, file, error);
    return !error;
}

std::vector<std::string> OnlineAchievements::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

} // namespace dw4::game
