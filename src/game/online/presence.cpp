#include "presence.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::game::online {

void PresenceTracks::add(const wire::Presence& presence, const std::string& name, double now) {
    auto& track = tracks_[presence.member];
    track.name = name;
    // A new stage or instance starts the track over (no glide across a load).
    if (!track.points.empty() && (track.points.back().presence.stage != presence.stage || track.points.back().presence.inst != presence.inst))
        track.points.clear();
    track.points.push_back({now, presence});
    while (track.points.size() > 4) track.points.pop_front();
}

std::vector<RemoteSample> PresenceTracks::sample(double now) const {
    std::vector<RemoteSample> out;
    for (const auto& [member, track] : tracks_) {
        if (track.points.empty()) continue;
        const auto& last = track.points.back();
        if (now - last.at > kStale) continue;
        if (last.presence.flags & (wire::presence_flag::hidden | wire::presence_flag::defeated)) continue;
        // One interval behind: between the two packets around that time.
        const double at = now - kInterval;
        const Point* a = &track.points.front();
        const Point* b = &last;
        for (std::size_t i = 0; i + 1 < track.points.size(); ++i)
            if (track.points[i].at <= at && track.points[i + 1].at >= at) {
                a = &track.points[i];
                b = &track.points[i + 1];
                break;
            }
        double t = b->at > a->at ? (at - a->at) / (b->at - a->at) : 1.0;
        t = std::clamp(t, 0.0, 1.0);
        if (at >= last.at) {
            a = b = &last;
            t = 1.0;
        }
        const auto lerp = [t](float p, float q) { return float(double(p) + (double(q) - double(p)) * t); };
        RemoteSample s;
        s.member = member;
        s.name = track.name;
        s.stage = last.presence.stage;
        s.x = lerp(a->presence.x, b->presence.x);
        s.y = lerp(a->presence.y, b->presence.y);
        s.z = lerp(a->presence.z, b->presence.z);
        s.yaw = b->presence.yaw;
        s.species = last.presence.species;
        s.level = last.presence.level;
        s.hp = last.presence.hp;
        s.max_hp = last.presence.max_hp;
        s.mp = last.presence.mp;
        s.max_mp = last.presence.max_mp;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace dw4::game::online
