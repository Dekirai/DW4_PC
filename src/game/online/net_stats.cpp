#include "net_stats.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dw4::game::online {

namespace {
constexpr double kField = 1001.0 / 60000.0;
constexpr std::size_t kMaxInFlight = 512; // far above any delay: a tick that never runs is forgotten

} // namespace

void LatencyWindow::add(const LatencyWindow& w) {
    LatencyWindow& into = *this;
    into.samples += w.samples;
    into.dropped += w.dropped;
    into.exec_sum += w.exec_sum;
    into.exec_max = std::max(into.exec_max, w.exec_max);
    into.present_sum += w.present_sum;
    into.present_max = std::max(into.present_max, w.present_max);
    into.presented += w.presented;
    into.steps_sum += w.steps_sum;
    for (std::size_t i = 0; i < into.exec_hist.size(); ++i) into.exec_hist[i] += w.exec_hist[i];
}

void LatencyWindow::add_exec(double seconds, double steps, bool dropped_pad) {
    ++samples;
    if (dropped_pad) ++dropped;
    exec_sum += seconds;
    exec_max = std::max(exec_max, seconds);
    steps_sum += steps;
    const auto bucket = std::size_t(std::clamp(std::floor(seconds / kField + 0.5), 0.0, double(exec_hist.size() - 1)));
    ++exec_hist[bucket];
}

void LatencyWindow::add_present(double seconds) {
    ++presented;
    present_sum += seconds;
    present_max = std::max(present_max, seconds);
}

double LatencyWindow::exec_quantile(double share) const {
    if (!samples) return 0;
    const auto want = std::uint64_t(std::ceil(double(samples) * std::clamp(share, 0.0, 1.0)));
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < exec_hist.size(); ++i) {
        seen += exec_hist[i];
        if (seen >= std::max<std::uint64_t>(1, want)) return double(i) * kField;
    }
    return double(exec_hist.size() - 1) * kField;
}

LinkQuality link_quality(int delay, double round_trip_ms, double drop_rate, bool waiting) noexcept {
    if (waiting || delay > 7 || drop_rate > 0.05 || round_trip_ms > 150) return LinkQuality::poor;
    if (delay > 4 || drop_rate > 0.01 || round_trip_ms > 80) return LinkQuality::fair;
    return LinkQuality::good;
}

const char* link_quality_name(LinkQuality quality) noexcept {
    switch (quality) {
    case LinkQuality::good: return "good";
    case LinkQuality::fair: return "fair";
    case LinkQuality::poor: return "poor";
    }
    return "?";
}

void NetStats::sampled(std::uint32_t visit, std::uint32_t tick, double now, std::uint64_t step, const WirePad& pad) {
    // (A tick sent twice keeps its first sample: the pad that is on the wire for it.)
    in_flight_.try_emplace({visit, tick}, Sample{now, step, pad});
    while (in_flight_.size() > kMaxInFlight) in_flight_.erase(in_flight_.begin());
}

void NetStats::executed(std::uint32_t visit, std::uint32_t tick, double now, std::uint64_t step, const WirePad& sealed) {
    const auto it = in_flight_.find({visit, tick});
    if (it == in_flight_.end()) return;
    const Sample s = it->second;
    // Everything older than this tick will not run any more.
    in_flight_.erase(in_flight_.begin(), std::next(it));
    const double exec = std::max(0.0, now - s.at);
    const double steps = double(step - std::min(step, s.step));
    // (Lost: the relay sealed another pad. A sealed pad with the sent one's
    // buttons and more, a press the relay carried from a late pad, is not.)
    const bool dropped = (sealed.buttons & s.pad.buttons) != s.pad.buttons || sealed.lx != s.pad.lx || sealed.ly != s.pad.ly ||
                         sealed.rx != s.pad.rx || sealed.ry != s.pad.ry;
    current_.add_exec(exec, steps, dropped);
    total_.add_exec(exec, steps, dropped);
    add_bucket(now);
    buckets_.back().w.add_exec(exec, steps, dropped);
    ran_.push_back(s.at);
    if (ran_.size() > kMaxInFlight) ran_.pop_front();
}

void NetStats::presented(double now) {
    if (ran_.empty()) return;
    add_bucket(now);
    for (const double at : ran_) {
        const double seconds = std::max(0.0, now - at);
        current_.add_present(seconds);
        total_.add_present(seconds);
        buckets_.back().w.add_present(seconds);
    }
    ran_.clear();
}

void NetStats::forget_in_flight() {
    in_flight_.clear();
    ran_.clear();
}

LatencyWindow NetStats::take_window() { return std::exchange(current_, {}); }

void NetStats::add_bucket(double now) {
    if (buckets_.empty() || now - buckets_.back().start >= 1.0) buckets_.push_back({now, {}});
}

void NetStats::roll_recent(double now, double span) {
    while (!buckets_.empty() && now - buckets_.front().start > span) buckets_.pop_front();
    recent_view_ = {};
    for (const auto& b : buckets_) recent_view_.add(b.w);
}

} // namespace dw4::game::online
