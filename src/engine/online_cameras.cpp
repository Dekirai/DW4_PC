// Seamless online co-op, the camera side (docs/SEAMLESS.md 4): per-slot box
// cameras at membership steps, the leash rule, the picture's camera and the
// picture filter of a multi-member instance. Every path here is inert while
// Engine::online() is all off (the original game).
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace dw4::engine {

// The rules change between steps, at a tick every member of the instance
// applies (the membership lane stamps JOIN / LEAVE / forks); the camera mask
// and the leash follow at once.
void Engine::set_online(const OnlineRules& rules) {
    const OnlineRules before = online_;
    online_ = rules;
    apply_online_cameras(before);
    apply_leash_rule();
    if (before.enabled != online_.enabled || std::popcount(before.present) >= 2 != std::popcount(online_.present) >= 2)
        install_online_lua();
}

bool Engine::online_shared() const { return online_.enabled && std::popcount(online_.present) >= 2; }

// SEAMLESS.md 4.1: when the mask changes, the slots that leave it lose their
// box; when an instance goes from the shared camera to per-slot cameras, the
// members present so far take over the shared box state (no cut), every
// other new slot starts with SETBOXCAMERA's first run (a direct cut).
void Engine::apply_online_cameras(const OnlineRules& before) {
    if (before.player_cameras == online_.player_cameras) return;
    if (auto* cameras = get<CameraManager>()) cameras->set_player_cameras(*this, before.player_cameras, online_.player_cameras, before.present);
    union_frame_ = ~0ull; // the next picture runs the union pass with the new mask
}

// SEAMLESS.md 4.6: no leash online. While OnlineRules::leash_off is set the
// players' ring (limit_Circle, a child hit under beChangePosTransform) stays
// off; a stage load brings it back from the stage data, so the step after
// the load (once the HitLand infos are registered) switches it off again.
void Engine::apply_leash_rule() {
    if (!online_.leash_off) return;
    if (auto* hitland = get<HitLandManager>(); hitland && hitland->child_hit("limit_Circle").value_or(false))
        hitland->set_child_hit("limit_Circle", false);
}

// SEAMLESS.md 4.1: the picture follows the local member's camera; while its
// player is defeated (no generated player: the capsule is out) the nearest
// living member's camera, measured from where the defeated member's box
// camera stands. Presentation only: the simulation never reads it.
int Engine::picture_slot() const {
    const int view = view_slot_;
    const auto mask = online_.player_cameras;
    if (!(mask & (1u << view))) return view;
    const auto* models = get<ModelCtrlManager>();
    if (!models) return view;
    if (models->player(view)) return view;
    const auto* cameras = get<CameraManager>();
    const Vec3f* from = cameras ? cameras->player_box_centre(view) : nullptr;
    int best = view;
    float best_d2 = std::numeric_limits<float>::infinity();
    for (int slot = 0; slot < 4; ++slot) {
        if (slot == view || !(mask & (1u << slot))) continue;
        const auto* other = models->player(slot);
        if (!other || !other->node) continue;
        const auto& m = other->node->matrix;
        const float dx = from ? m(3, 0) - from->x : 0.0f, dz = from ? m(3, 2) - from->z : 0.0f;
        const float d2 = dx * dx + dz * dz;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = slot;
        }
    }
    return best;
}

// SEAMLESS.md 4.3 / 5.2: what this machine's picture leaves out in a
// multi-member instance. Every member simulates the same scene; these parts
// are another member's: the item name line of another owner (positioned for
// that owner's camera), another member's exit prompt window and, while an
// exit prompt runs, its half fade and prompt text unless this member sees
// the prompt.
void Engine::picture_hidden_roots(std::vector<const Node*>& out) {
    out.clear();
    if (!online_shared()) return;
    const int picture = picture_slot();
    const int view = view_slot_;
    if (auto* text = get<TextCtlManager>()) {
        static constexpr const char* kItemLines[4] = {"font00", "font01", "font02", "font03"};
        for (int line = 0; line < 4; ++line)
            if (line != picture)
                if (const Node* node = text->line_node("ITNAMEFONT", kItemLines[line])) out.push_back(node);
        if (exit_view_.active && exit_view_.asking != view)
            if (const Node* node = text->line_node("STCHGFONT00", "font00")) out.push_back(node);
    }
    if (auto* models = get<ModelCtrlManager>()) {
        for (int slot = 0; slot < 4; ++slot) {
            if (slot == view) continue;
            for (const char* name : {"i_zzstchgwb", "i_zzstchgwn"})
                if (const auto* ram = models->find(name, slot); ram && ram->node) out.push_back(ram->node);
        }
        if (exit_view_.active && !(exit_view_.sees_fade & (1u << view)))
            if (const auto* ram = models->find("i_fadewn01", 0); ram && ram->node) out.push_back(ram->node);
    }
}

} // namespace dw4::engine
