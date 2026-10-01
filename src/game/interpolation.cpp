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
    if (captured_) previous_ = std::move(current_);
    current_ = {};
    captured_ = false;
}

void MotionInterpolator::reset() {
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

void MotionInterpolator::apply(const Map& previous, std::vector<engine::DrawItem>& items, float alpha) {
    std::unordered_map<Key, int, KeyHash> seen;
    seen.reserve(items.size());
    for (auto& item : items) {
        Key key{item.node, item.geometry, 0};
        key.occurrence = std::uint32_t(seen[key]++);
        const auto found = previous.find(key);
        if (found == previous.end()) continue;
        const auto& before = found->second;
        if (!close(before.model, item.model, 40.0f)) continue;
        item.model = lerp(before.model, item.model, alpha);
        if (item.palette && before.palette && before.palette != item.palette &&
            before.palette->size() == item.palette->size()) {
            auto blended = std::make_shared<std::vector<engine::Mat4>>(*item.palette);
            bool ok = true;
            for (std::size_t i = 0; i < blended->size() && ok; ++i) {
                if (!close((*before.palette)[i], (*blended)[i], 40.0f)) ok = false;
                else (*blended)[i] = lerp((*before.palette)[i], (*blended)[i], alpha);
            }
            if (ok) item.palette = std::move(blended);
        }
    }
}

void MotionInterpolator::blend(std::vector<engine::DrawItem>& items, engine::FrameCamera& camera,
                               engine::ShadowFrame& shadow, float alpha) const {
    if (!previous_.valid || !current_.valid) return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha >= 1.0f) return;
    apply(previous_.items, items, alpha);
    apply(previous_.casters, shadow.casters, alpha);
    if (close(previous_.view, camera.view, 60.0f)) camera.view = lerp(previous_.view, camera.view, alpha);
    if (close(previous_.light_view, shadow.light_view, 60.0f))
        shadow.light_view = lerp(previous_.light_view, shadow.light_view, alpha);
}

} // namespace dw4::game
