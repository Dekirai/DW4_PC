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

float row_length(const engine::Mat4& m, int row) {
    return std::sqrt(m(row, 0) * m(row, 0) + m(row, 1) * m(row, 1) + m(row, 2) * m(row, 2));
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

// Two texture matrices one continuous scroll apart: no flip or rescale and
// less than half a texture of movement in a step.
bool texture_close(const engine::Mat4& a, const engine::Mat4& b) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::abs(a(i, j) - b(i, j)) > 0.5f) return false;
    return true;
}

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

engine::Mat4 MotionInterpolator::blend_matrix(const engine::Mat4& a, const engine::Mat4& b, float t) {
    engine::Mat4 r = lerp(a, b, t);
    for (int i = 0; i < 3; ++i) {
        // (An unchanged axis stays exactly as it is.)
        if (a(i, 0) == b(i, 0) && a(i, 1) == b(i, 1) && a(i, 2) == b(i, 2)) continue;
        const float la = row_length(a, i), lb = row_length(b, i), lr = row_length(r, i);
        if (!(lr > 1e-12f)) continue;
        const float k = (la + (lb - la) * t) / lr;
        for (int j = 0; j < 3; ++j) r(i, j) *= k;
    }
    return r;
}

engine::Mat4 MotionInterpolator::blend_view(const engine::Mat4& a, const engine::Mat4& b, float t) {
    if (a.m == b.m) return b;
    engine::Mat4 placed_a, placed_b, r;
    if (!engine::invert(a, placed_a) || !engine::invert(b, placed_b)) return lerp(a, b, t);
    if (!engine::invert(blend_matrix(placed_a, placed_b, t), r)) return lerp(a, b, t);
    return r;
}

std::size_t MotionInterpolator::KeyHash::operator()(const Key& key) const {
    const std::size_t a = std::hash<const void*>()(key.node);
    const std::size_t b = std::hash<const void*>()(key.geometry);
    return a ^ (b * 0x9E3779B97F4A7C15ull) ^ (std::size_t(key.occurrence) << 1) ^ std::size_t(key.instance * 0xC2B2AE3D27D4EB4Full);
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

std::optional<engine::Mat4> MotionInterpolator::previous_view() const {
    if (!previous_.valid) return std::nullopt;
    return previous_.view;
}

void MotionInterpolator::record(Map& map, const std::vector<engine::DrawItem>& items) {
    map.clear();
    map.reserve(items.size());
    for (const auto& item : items) {
        Key key{item.instance, item.node, item.geometry, 0};
        while (map.count(key)) ++key.occurrence;
        map.emplace(key, State{item.model, item.palette, item.state.texture_matrix, item.phase, item.texture_phase,
                               item.sequence_time, item.texture_sequence_time});
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

void MotionInterpolator::apply(const Map& previous, std::vector<engine::DrawItem>& items, float alpha, BlendReport* report,
                               const Map* older) {
    std::unordered_map<Key, int, KeyHash> seen;
    seen.reserve(items.size());
    for (auto& item : items) {
        Key key{item.instance, item.node, item.geometry, 0};
        key.occurrence = std::uint32_t(seen[key]++);
        if (report) ++report->items;
        const auto found = previous.find(key);
        if (found == previous.end()) {
            if (report) ++report->unmatched;
            continue;
        }
        const auto& before = found->second;
        const bool textured = item.state.texture_matrix && before.texture_matrix;
        if (report && textured) ++report->textures;
        if (report) {
            // (Diagnostics: another occurrence of the placement, node and
            // geometry was nearer in the previous step than the one this item
            // blends from.)
            const float own = distance(before.model, item.model);
            for (const std::uint32_t other : {key.occurrence - 1, key.occurrence + 1}) {
                if (other == std::uint32_t(-1)) continue;
                const auto alt = previous.find(Key{key.instance, key.node, key.geometry, other});
                if (alt != previous.end() && distance(alt->second.model, item.model) + 0.01f < own) {
                    ++report->swapped;
                    report->flagged.push_back({&item, "swap", own});
                    break;
                }
            }
        }
        if (before.phase != item.phase || item.sequence_time < before.sequence_time) {
            // A sequence above the item wrapped or stepped (the stagML11 ring
            // walls turn 11.25 degrees in 240 steps and start again: blended,
            // they turned back for a step), or its clock went back (the
            // stagis10 menu background's JOINTSET loop every 358 steps).
            if (report) {
                ++report->wraps;
                report->flagged.push_back({&item, "wrap", distance(before.model, item.model), turn(before.model, item.model)});
            }
            continue;
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
            // before that no clock marked, blended like motion.)
            if (const auto old = older->find(key);
                old != older->end() && old->second.phase == before.phase && old->second.sequence_time <= before.sequence_time) {
                const float move_now = distance(before.model, item.model), move_before = distance(old->second.model, before.model);
                const float turn_now = turn(before.model, item.model), turn_before = turn(old->second.model, before.model);
                if (turn_now > 8.0f * turn_before + 0.01f || move_now > 8.0f * move_before + 0.5f) {
                    ++report->jumps;
                    report->flagged.push_back({&item, "jump", move_now, turn_now, turn_before});
                }
            }
        }
        // The texture matrix (UV scrolls, on its own clocks) only blends with
        // its model: an item drawn as it is (the wrap and far cases above)
        // keeps its step's texture matrix too, so the picture is that step's
        // (the player's slash trail CF_pCylinderShape1 turns far in every
        // swing; with a trailing UV it showed a hard edge for a present or two).
        if (textured) {
            if (before.texture_phase == item.texture_phase && item.texture_sequence_time >= before.texture_sequence_time &&
                texture_close(*before.texture_matrix, *item.state.texture_matrix)) {
                item.state.texture_matrix = lerp(*before.texture_matrix, *item.state.texture_matrix, alpha);
                if (report) ++report->textures_blended;
            } else if (report && before.texture_matrix->m != item.state.texture_matrix->m) {
                report->flagged.push_back({&item, "texture_jump", 0.0f});
            }
        }
        item.model = blend_matrix(before.model, item.model, alpha);
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
            // Bone by bone: one that jumped (a part scaled in or out, a snap)
            // is drawn as it is, the others with the blended placement.
            auto blended = std::make_shared<std::vector<engine::Mat4>>(*item.palette);
            int kept = 0;
            for (std::size_t i = 0; i < blended->size(); ++i) {
                if (close((*before.palette)[i], (*blended)[i], 40.0f))
                    (*blended)[i] = blend_matrix((*before.palette)[i], (*blended)[i], alpha);
                else
                    ++kept;
            }
            item.palette = std::move(blended);
            if (report) {
                ++report->palettes_blended;
                if (kept) {
                    ++report->palettes_far;
                    report->flagged.push_back({&item, "palette_far", float(kept)});
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
    if (report) {
        // (Diagnostics: how the camera's placement changed in this step and
        // in the one before.)
        engine::Mat4 now, before, older;
        if (engine::invert(camera.view, now) && engine::invert(previous_.view, before)) {
            report->camera_move = distance(before, now);
            report->camera_turn = turn(before, now);
            if (older_.valid && engine::invert(older_.view, older)) {
                report->camera_move_before = distance(older, before);
                report->camera_turn_before = turn(older, before);
            }
        }
    }
    if (close(previous_.view, camera.view, 60.0f)) {
        camera.view = blend_view(previous_.view, camera.view, alpha);
        if (report) report->camera = true;
    }
    if (close(previous_.light_view, shadow.light_view, 60.0f)) {
        shadow.light_view = blend_view(previous_.light_view, shadow.light_view, alpha);
        if (report) report->light = true;
    }
}

} // namespace dw4::game
