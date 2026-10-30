#pragma once

// Room members outside the own instance (docs/SEAMLESS.md 12): their PRESENCE
// packets (five per second) become remote name tags, interpolated between
// packets: the tag is drawn one packet interval behind, between the two
// packets around that time, so it glides instead of jumping five times a
// second. A member whose packets stop for two seconds, or that is hidden
// (loading, a scene, a menu: an empty stage) or defeated (hp 0), has no tag. Presentation only: it
// never touches the scene graph.
#include "wire.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace dw4::game::online {

struct RemoteSample {
    std::uint32_t member{};
    std::string name, stage;
    float x{}, y{}, z{}, yaw{};
    int species{}, level{}, hp{}, max_hp{}, mp{}, max_mp{};
};

class PresenceTracks {
public:
    static constexpr double kInterval = 0.2;  // the sender's period
    static constexpr double kStale = 2.0;     // no packet for this long: no tag

    void add(const wire::Presence& presence, const std::string& name, double now);
    void forget(std::uint32_t member) { tracks_.erase(member); }
    void clear() { tracks_.clear(); }
    // The tags at `now` (any stage; the labels keep those of the local stage).
    std::vector<RemoteSample> sample(double now) const;

private:
    struct Point {
        double at{};
        wire::Presence presence;
    };
    struct Track {
        std::string name;
        std::deque<Point> points;
    };
    std::map<std::uint32_t, Track> tracks_;
};

} // namespace dw4::game::online
