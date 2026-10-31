#pragma once

// Room members outside the own instance (docs/SEAMLESS.md 12): their PRESENCE
// packets become remote name tags, and in the hub stages their PRESENCE2
// packets also ghost figures. Both are drawn a little behind the newest
// packet, between the two packets around that time, so a member glides
// instead of jumping five or ten times a second:
//
//  - PRESENCE2 carries the sender's clock: the packets keep its spacing (the
//    offset to this machine's clock is the smallest one seen, so a packet
//    the network held up does not shift the others) and are drawn kDelay2
//    behind the newest (one 100 ms interval and room for the network's
//    jitter); past the newest packet (a late one) the member goes on along
//    its last movement for at most kExtrapolate, then holds;
//  - PRESENCE has no clock: the packets are placed at their arrival and
//    drawn kInterval (its 200 ms interval) behind, holding the newest.
//
// A jump is never glided across: a new stage or instance starts the track
// over, and a change of the sender's warp counter or a move faster than any
// walk (kSnapSpeed, kSnapDistance) starts a new segment, shown at once. A
// member whose packets stop for kStale, or that is hidden (loading, a scene,
// a menu: an empty stage) or defeated (hp 0), has no sample. Presentation
// only: it never touches the engine.
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
    // PRESENCE2 (`v2`; from a PRESENCE the clip stays empty): the motion
    // clip at the sample time and whether it loops, a window open (busy: the
    // figure stands, the tag hides), the weapon package. `speed` (units per
    // second at the sample time) lets a figure guess its motion without a
    // clip; `segment` changes at every jump (nothing glides across it).
    bool v2{};
    std::string clip, weapon;
    bool loop = true, busy = false;
    float speed{};
    std::uint32_t segment{};
};

class PresenceTracks {
public:
    static constexpr double kInterval = 0.2;       // the PRESENCE sender's period
    static constexpr double kDelay2 = 0.16;        // PRESENCE2: drawn this far behind the sender's newest packet
    static constexpr double kStale = 2.0;          // no packet for this long: no sample
    static constexpr double kExtrapolate = 0.06;   // PRESENCE2: at most this far past the newest packet
    static constexpr double kOffsetRelax = 0.002;  // seconds the clock offset may rise per packet (clock drift, a slower route)
    static constexpr float kSnapSpeed = 900.0f;    // units per second faster than any walk: a warp
    static constexpr float kSnapDistance = 120.0f; // a step this far between two packets never glides
    static constexpr std::size_t kPoints = 8;      // packets kept per member

    void add(const wire::Presence& presence, const std::string& name, double now);
    void forget(std::uint32_t member) { tracks_.erase(member); }
    void clear() { tracks_.clear(); }
    // The samples at `now` (any stage; the labels and the figures keep those
    // of the local stage).
    std::vector<RemoteSample> sample(double now) const;

private:
    struct Point {
        double arrived{}; // this machine's clock
        double sent{};    // the sender's clock, unwrapped (PRESENCE: the arrival)
        std::uint32_t segment{};
        wire::Presence presence;
    };
    struct Track {
        std::string name;
        std::deque<Point> points;
        bool v2{};
        double offset{}; // this machine's clock minus the sender's: the smallest seen, relaxed per packet
        std::uint32_t segment{};
    };
    std::map<std::uint32_t, Track> tracks_;
};

} // namespace dw4::game::online
