#include "presence.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dw4::game::online {

namespace {
float distance(const wire::Presence& a, const wire::Presence& b) {
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
// The shorter way from yaw a to yaw b, `t` of it.
float lerp_yaw(float a, float b, double t) {
    constexpr double two_pi = 2.0 * std::numbers::pi;
    double d = std::fmod(double(b) - double(a), two_pi);
    if (d > std::numbers::pi) d -= two_pi;
    if (d < -std::numbers::pi) d += two_pi;
    return float(double(a) + d * t);
}
} // namespace

void PresenceTracks::add(const wire::Presence& presence, const std::string& name, double now) {
    auto& track = tracks_[presence.member];
    track.name = name;
    const bool v2 = presence.version != 0;
    // A new stage or instance, or another kind of packet, starts the track
    // over (no glide across a load).
    if (!track.points.empty() && (track.points.back().presence.stage != presence.stage || track.points.back().presence.inst != presence.inst ||
                                  track.v2 != v2)) {
        track.points.clear();
        ++track.segment;
    }
    track.v2 = v2;
    Point point;
    point.arrived = now;
    point.sent = now;
    if (v2) {
        // The sender's clock in seconds, unwrapped against the last packet
        // (its milliseconds wrap after 49 days).
        bool restart = track.points.empty();
        if (!restart) {
            const auto& last = track.points.back();
            const auto step = std::int32_t(presence.sent_ms - last.presence.sent_ms);
            point.sent = last.sent + double(step) / 1000.0;
            // Out of order or a restarted sender clock: start over.
            if (step <= 0 || step > 10000) {
                track.points.clear();
                ++track.segment;
                restart = true;
            }
        }
        if (restart) {
            point.sent = double(presence.sent_ms) / 1000.0;
            track.offset = track.target = now - point.sent;
            track.offset_time = now;
            track.lags.assign(1, now - point.sent);
        } else {
            // The drawn offset goes on from where it is now towards the new
            // target (no jump: it only changes its direction here).
            track.offset = offset_at(track, now);
            track.offset_time = now;
            track.lags.push_back(now - point.sent);
            while (track.lags.size() > kLagWindow) track.lags.pop_front();
            track.target = *std::min_element(track.lags.begin(), track.lags.end());
        }
    }
    if (!track.points.empty()) {
        const auto& last = track.points.back();
        const double dt = std::max(point.sent - last.sent, 1e-3);
        const float moved = distance(last.presence, presence);
        if ((v2 && presence.warp != last.presence.warp) || moved > kSnapDistance || moved > kSnapSpeed * dt) ++track.segment;
    }
    point.segment = track.segment;
    point.presence = presence;
    track.points.push_back(std::move(point));
    while (track.points.size() > kPoints) track.points.pop_front();
}

double PresenceTracks::offset_at(const Track& track, double now) {
    const double room = kOffsetSlew * std::max(0.0, now - track.offset_time);
    return track.offset + std::clamp(track.target - track.offset, -room, room);
}

std::vector<RemoteSample> PresenceTracks::sample(double now) const {
    std::vector<RemoteSample> out;
    for (const auto& [member, track] : tracks_) {
        if (track.points.empty()) continue;
        const auto& last = track.points.back();
        if (now - last.arrived > kStale) continue;
        if (last.presence.stage.empty() || last.presence.hp == 0) continue; // hidden (empty stage) or defeated
        // The time drawn, on the sender's clock (PRESENCE: the arrivals).
        const double at = track.v2 ? now - offset_at(track, now) - kDelay2 : now - kInterval;
        const Point* a = &last;
        const Point* b = &last;
        double t = 1.0;
        if (at < track.points.front().sent) {
            a = b = &track.points.front();
        } else if (at < last.sent) {
            for (std::size_t i = 0; i + 1 < track.points.size(); ++i)
                if (track.points[i + 1].sent >= at) {
                    a = &track.points[i];
                    b = &track.points[i + 1];
                    break;
                }
            if (a->segment != b->segment) {
                // A jump between them: the earlier place until the later packet's time.
                b = a;
            } else {
                t = b->sent > a->sent ? std::clamp((at - a->sent) / (b->sent - a->sent), 0.0, 1.0) : 1.0;
            }
        } else if (track.v2 && track.points.size() >= 2) {
            // Past the newest packet: on along the last movement for a moment.
            const auto& before = track.points[track.points.size() - 2];
            if (before.segment == last.segment && last.sent > before.sent) {
                a = &before;
                t = 1.0 + std::min(at - last.sent, kExtrapolate) / (last.sent - before.sent);
            }
        }
        const auto lerp = [t](float p, float q) { return float(double(p) + (double(q) - double(p)) * t); };
        RemoteSample s;
        s.member = member;
        s.name = track.name;
        s.stage = last.presence.stage;
        s.x = lerp(a->presence.x, b->presence.x);
        s.y = lerp(a->presence.y, b->presence.y);
        s.z = lerp(a->presence.z, b->presence.z);
        s.yaw = lerp_yaw(a->presence.yaw, b->presence.yaw, std::min(t, 1.0));
        s.species = last.presence.species;
        s.level = last.presence.level;
        s.hp = last.presence.hp;
        s.max_hp = last.presence.max_hp;
        s.mp = last.presence.mp;
        s.max_mp = last.presence.max_mp;
        s.v2 = track.v2;
        // The motion of the packet the drawn time has reached; the rest is the newest.
        const auto& motion = t >= 1.0 ? b->presence : a->presence;
        s.clip = motion.clip;
        s.loop = (motion.flags & wire::Presence::loop) != 0;
        s.busy = (last.presence.flags & wire::Presence::busy) != 0;
        s.weapon = last.presence.weapon;
        s.speed = a != b && b->sent > a->sent ? distance(a->presence, b->presence) / float(b->sent - a->sent) : 0.0f;
        s.segment = (t >= 1.0 ? b : a)->segment;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace dw4::game::online
