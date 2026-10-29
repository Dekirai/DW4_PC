#include "interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace dw4::game {
namespace {

engine::Mat4 lerp(const engine::Mat4& a, const engine::Mat4& b, float t) {
    engine::Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r(i, j) = a(i, j) + (b(i, j) - a(i, j)) * t;
    return r;
}

// Two placements close enough to blend: no warp (translation) and no sudden
// turn or rescale (the 3x3 part).
bool close(const engine::Mat4& a, const engine::Mat4& b, float max_move) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::abs(a(i, j) - b(i, j)) > 0.5f) return false;
    const float dx = a(3, 0) - b(3, 0), dy = a(3, 1) - b(3, 1), dz = a(3, 2) - b(3, 2);
    return dx * dx + dy * dy + dz * dz <= max_move * max_move;
}

} // namespace

std::size_t MotionInterpolator::KeyHash::operator()(const Key& key) const {
    const std::size_t a = std::hash<const void*>()(key.node);
    const std::size_t b = std::hash<const void*>()(key.geometry);
    return a ^ (b * 0x9E3779B97F4A7C15ull) ^ (std::size_t(key.occurrence) << 1);
}

void MotionInterpolator::advance() {
    if (captured_) {
        older_ = std::move(previous_);
        previous_ = std::move(current_);
    }
    current_ = {};
    captured_ = false;
}

void MotionInterpolator::reset() {
    older_ = {};
    previous_ = {};
    current_ = {};
    captured_ = false;
}

void MotionInterpolator::record(Map& map, const std::vector<engine::DrawItem>& items) {
    map.clear();
    map.reserve(items.size());
    for (const auto& item : items) {
        Key key{item.node, item.geometry, 0};
        while (map.count(key)) ++key.occurrence;
        map.emplace(key, State{item.model, item.palette});
    }
}

void MotionInterpolator::capture(const std::vector<engine::DrawItem>& items, const engine::FrameCamera& camera,
                                 const engine::ShadowFrame& shadow) {
    record(current_.items, items);
    record(current_.casters, shadow.casters);
    current_.view = camera.view;
    current_.light_view = shadow.light_view;
    current_.valid = true;
    captured_ = true;
}

namespace {
float distance(const engine::Mat4& a, const engine::Mat4& b) {
    const float dx = a(3, 0) - b(3, 0), dy = a(3, 1) - b(3, 1), dz = a(3, 2) - b(3, 2);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
float turn(const engine::Mat4& a, const engine::Mat4& b) {
    float most = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) most = std::max(most, std::abs(a(i, j) - b(i, j)));
    return most;
}
} // namespace

void MotionInterpolator::apply(const Map& previous, std::vector<engine::DrawItem>& items, float alpha, BlendReport* report,
                               const Map* older) {
    std::unordered_map<Key, int, KeyHash> seen;
    seen.reserve(items.size());
    for (auto& item : items) {
        Key key{item.node, item.geometry, 0};
        key.occurrence = std::uint32_t(seen[key]++);
        if (report) ++report->items;
        const auto found = previous.find(key);
        if (found == previous.end()) {
            if (report) ++report->unmatched;
            continue;
        }
        const auto& before = found->second;
        if (report) {
            // (Diagnostics: another occurrence of the node and geometry was
            // nearer in the previous step than the one this item blends from.)
            const float own = distance(before.model, item.model);
            for (const std::uint32_t other : {key.occurrence - 1, key.occurrence + 1}) {
                if (other == std::uint32_t(-1)) continue;
                const auto alt = previous.find(Key{key.node, key.geometry, other});
                if (alt != previous.end() && distance(alt->second.model, item.model) + 0.01f < own) {
                    ++report->swapped;
                    report->flagged.push_back({&item, "swap", own});
                    break;
                }
            }
        }
        if (!close(before.model, item.model, 40.0f)) {
            if (report) {
                ++report->far;
                report->flagged.push_back({&item, "far", distance(before.model, item.model)});
            }
            continue;
        }
        if (report && older) {
            // (Diagnostics: a change far larger than the one of the step
            // before, e.g. a looping transform sequence wrapping to its
            // start, is blended like motion.)
            if (const auto old = older->find(key); old != older->end()) {
                const float move_now = distance(before.model, item.model), move_before = distance(old->second.model, before.model);
                const float turn_now = turn(before.model, item.model), turn_before = turn(old->second.model, before.model);
                if (turn_now > 8.0f * turn_before + 0.01f || move_now > 8.0f * move_before + 0.5f) {
                    ++report->jumps;
                    report->flagged.push_back({&item, "jump", move_now, turn_now, turn_before});
                }
            }
        }
        item.model = lerp(before.model, item.model, alpha);
        if (report) ++report->blended;
        if (report && item.palette) {
            ++report->palettes;
            if (before.palette == item.palette) {
                ++report->palettes_shared;
                report->flagged.push_back({&item, "palette_shared", 0.0f});
            } else if (before.palette && before.palette->size() != item.palette->size()) {
                ++report->palettes_resized;
                report->flagged.push_back({&item, "palette_resized", 0.0f});
            }
        }
        if (item.palette && before.palette && before.palette != item.palette &&
            before.palette->size() == item.palette->size()) {
            auto blended = std::make_shared<std::vector<engine::Mat4>>(*item.palette);
            bool ok = true;
            for (std::size_t i = 0; i < blended->size() && ok; ++i) {
                if (!close((*before.palette)[i], (*blended)[i], 40.0f)) ok = false;
                else (*blended)[i] = lerp((*before.palette)[i], (*blended)[i], alpha);
            }
            if (ok) item.palette = std::move(blended);
            if (report) {
                if (ok) ++report->palettes_blended;
                else {
                    ++report->palettes_far;
                    report->flagged.push_back({&item, "palette_far", 0.0f});
                }
            }
        }
    }
}

void MotionInterpolator::blend(std::vector<engine::DrawItem>& items, engine::FrameCamera& camera,
                               engine::ShadowFrame& shadow, float alpha, BlendReport* report) const {
    if (!previous_.valid || !current_.valid) return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha >= 1.0f) return;
    apply(previous_.items, items, alpha, report, older_.valid ? &older_.items : nullptr);
    apply(previous_.casters, shadow.casters, alpha, nullptr);
    if (close(previous_.view, camera.view, 60.0f)) {
        camera.view = lerp(previous_.view, camera.view, alpha);
        if (report) report->camera = true;
    }
    if (close(previous_.light_view, shadow.light_view, 60.0f)) {
        shadow.light_view = lerp(previous_.light_view, shadow.light_view, alpha);
        if (report) report->light = true;
    }
}

} // namespace dw4::game
