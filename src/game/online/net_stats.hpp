#pragma once

// What a member's input costs online (docs/SEAMLESS.md 6.4): for every pad
// this machine samples, the time it was sampled, the tick it is sent for,
// when that tick ran here and when its picture was presented, and whether the
// relay sealed the pad as sent (a pad that came after its tick's due time is
// replaced by a fill: the press is lost). The session feeds it; the in-game
// network indicator, F1 > Online, saves/online.log and the latency scenarios
// of tools/online_test read it.
//
// All times are seconds of the host's steady clock.
#include "pad_wire.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace dw4::game::online {

// One window of samples (the indicator's last seconds, a log line's span).
struct LatencyWindow {
    std::uint64_t samples = 0;   // own pads that ran (sealed or solo)
    std::uint64_t dropped = 0;   // ... of which the relay sealed another pad (a fill)
    double exec_sum = 0, exec_max = 0;       // sampled -> its tick ran here (seconds)
    double present_sum = 0, present_max = 0; // sampled -> the first picture after its tick
    std::uint64_t presented = 0;
    double steps_sum = 0;        // host steps between sampling and running (the input delay in ticks)
    std::array<std::uint32_t, 32> exec_hist{}; // by 1/60 s buckets (the last one collects the rest)

    void add_exec(double seconds, double steps, bool dropped_pad);
    void add(const LatencyWindow& other);
    void add_present(double seconds);
    double exec_avg() const { return samples ? exec_sum / double(samples) : 0; }
    double present_avg() const { return presented ? present_sum / double(presented) : 0; }
    double steps_avg() const { return samples ? steps_sum / double(samples) : 0; }
    double drop_rate() const { return samples ? double(dropped) / double(samples) : 0; }
    // The exec latency below which `share` of the samples lie (from the histogram, in seconds).
    double exec_quantile(double share) const;
};

// How the link feels for the network indicator (round 62): good (green) up to
// 4 ticks of input delay (67 ms) with almost no lost inputs, fair (yellow) up
// to 7 ticks (117 ms) or a few lost inputs, poor (red) beyond, or while the
// game waits for the relay.
enum class LinkQuality : std::uint8_t { good, fair, poor };
// `delay` in ticks, `round_trip_ms` negative when not known, `drop_rate` the
// share of own pads not sealed as sent in the last seconds.
LinkQuality link_quality(int delay, double round_trip_ms, double drop_rate, bool waiting) noexcept;
const char* link_quality_name(LinkQuality quality) noexcept;

class NetStats {
public:
    // The pad sampled at `now` (host step counter `step`) goes out for `tick`
    // of the visit (sealed: tick + d; solo: the tick it runs in).
    void sampled(std::uint32_t visit, std::uint32_t tick, double now, std::uint64_t step, const WirePad& pad);
    // The tick ran with `sealed` as this member's pad; `delayed`: it waited
    // for the network (a sealed tick, or a solo leader's tick after JOIN_REQ),
    // not a solo tick that ran in the step of its sample.
    void executed(std::uint32_t visit, std::uint32_t tick, double now, std::uint64_t step, const WirePad& sealed, bool delayed = true);
    // A picture was presented: every tick that ran since the last one shows.
    void presented(double now);
    // A new visit, a resync, a mode change: the samples in flight are dropped
    // from the bookkeeping (not counted as lost).
    void forget_in_flight();

    // The windows: `current` since the last take, `recent` the last few
    // seconds (rolling), `total` the whole session, `delayed_total` its pads
    // that waited for the network (the session's latency with others: the
    // solo ticks' 0 ms left out, round 62 review).
    LatencyWindow take_window();
    const LatencyWindow& total() const { return total_; }
    const LatencyWindow& delayed_total() const { return delayed_total_; }
    const LatencyWindow& recent() const { return recent_view_; }
    void roll_recent(double now, double span = 3.0);

private:
    struct Sample {
        double at = 0;
        std::uint64_t step = 0;
        WirePad pad{};
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, Sample> in_flight_; // (visit, tick) -> sample
    std::deque<double> ran_; // sample times of ticks that ran but were not presented yet
    LatencyWindow current_, total_, delayed_total_;
    // The rolling window: per-second buckets of the last few seconds.
    struct Bucket {
        double start = 0;
        LatencyWindow w;
    };
    std::deque<Bucket> buckets_;
    LatencyWindow recent_view_;
    void add_bucket(double now);
};

} // namespace dw4::game::online
