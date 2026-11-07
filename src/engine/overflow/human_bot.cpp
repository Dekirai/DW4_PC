// Data Overflow: the human bot (bot=human; the balance round of 05.11).
//
// The kite bot (director.cpp) dodges as no newcomer does: every step it
// weighs 16 ways against every enemy within 40 units and takes the crowd's
// gap. The user's first run ended after two minutes (Agumon: "surrounded
// quickly, no gap to run; a few hits cost a lot"), so the balance needs a
// player who plays as he did:
//   - it sees late: what it acts on is 15-18 steps old (250-300 ms);
//   - it looks again only every 6-14 steps while it flees, every 20-40 while
//     it roams, and its thumb turns the stick in about five steps;
//   - it flees from the middle of the enemies near it (within 22 units,
//     weighted 1/d), its way off by up to 30 degrees (one flight in seven by
//     70 more: it is confused), at full stick, and holds that way for 0.4-0.9
//     s: no search for the gap, so it may run into the next group;
//   - it begins to flee when an enemy is 11-16 units away (as it sees them);
//   - otherwise it roams to points of the arena, one goal in four the middle
//     of a group (to let the Art work), stops for a moment now and then when
//     nothing is within 25 units, and keeps off the ring's wall;
//   - it takes an HP disk 0.75 s after its HP fell below a quarter, and the
//     offers as a newcomer (offers=newcomer, the director's step).
// Its draws come from its own generator, seeded from the run's seed: the
// same seed plays the same run.
#include "dw4/engine/overflow_director.hpp"

#include "director_util.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::engine::overflow {

using detail::flat_distance;
using detail::living_enemy;
using detail::ram_position;

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr std::size_t kSeen = 24; // steps of positions kept (more than the slowest reaction)
} // namespace

PadSample OverflowDirector::human_pad(Engine& engine) {
    HostPadInput input;
    auto& h = human_;
    if (gate_walk() && started_) return gate_pad(engine);
    if (screen_ != Screen::none || outcome_ != Outcome::none || !started_) return host_pad_sample(input);
    auto* models = engine.get<ModelCtrlManager>();
    auto* sys = engine.get<GameSystem>();
    if (!models || !sys || !engine.get<CameraManager>()) return host_pad_sample(input);
    const auto* self = models->find(sys->players[0].digimon, 0);
    if (!self || !self->node) return host_pad_sample(input);
    const Vec3f me = ram_position(*self);
    auto unit = [&] { return float(next_unit(h.random)); };
    // What it sees now; it acts on what it saw `reaction` steps ago.
    {
        std::vector<Vec3f> now;
        for (const auto& ram : models->rams())
            if (living_enemy(*ram)) now.push_back(ram_position(*ram));
        h.seen.push_back(std::move(now));
        if (h.seen.size() > kSeen) h.seen.erase(h.seen.begin());
    }
    if (h.mode_left > 0) --h.mode_left;
    if (h.goal_left > 0) --h.goal_left;
    if (--h.look_in <= 0) {
        const std::size_t lag = std::min<std::size_t>(h.seen.size() - 1, std::size_t(std::max(0, h.reaction)));
        const auto& view = h.seen[h.seen.size() - 1 - lag];
        float nearest = 1e9f, ax = 0, az = 0;
        for (const auto& q : view) {
            const float d = flat_distance(q, me);
            nearest = std::min(nearest, d);
            if (d < 22.0f && d > 1e-3f) {
                // Away from the middle of the near ones, the nearest weigh most.
                const float w = 1.0f / std::max(d, 2.0f);
                ax += (me.x - q.x) / d * w;
                az += (me.z - q.z) / d * w;
            }
        }
        const float panic = 11.0f + 5.0f * unit();
        const bool dashing = h.mode == Human::Mode::escape && h.mode_left > 0;
        if (nearest < panic && !dashing && (ax != 0.0f || az != 0.0f)) {
            // A flight: its way off by up to 30 degrees, one in seven by 70 more.
            float angle = std::atan2(ax, az) + (unit() + unit() - 1.0f) * (30.0f * kPi / 180.0f);
            if (unit() < 1.0f / 7.0f) angle += (unit() < 0.5f ? -1.0f : 1.0f) * (70.0f * kPi / 180.0f);
            h.want_x = std::sin(angle);
            h.want_z = std::cos(angle);
            h.want_magnitude = 1.0f;
            h.mode = Human::Mode::escape;
            h.mode_left = 25 + int(30.0f * unit());
        } else if (!dashing && !(h.mode == Human::Mode::stop && h.mode_left > 0)) {
            // Roaming: a goal of the arena, one in four the middle of a group.
            if (h.mode != Human::Mode::roam || h.goal_left <= 0 || flat_distance(me, h.goal) < 8.0f) {
                bool picked = false;
                if (unit() < 0.25f) {
                    int best = 0;
                    for (const auto& q : view) {
                        if (flat_distance(q, me) > 50.0f) continue;
                        int around = 0;
                        for (const auto& o : view) around += flat_distance(q, o) < 12.0f ? 1 : 0;
                        if (around > best) {
                            best = around;
                            h.goal = q;
                            picked = true;
                        }
                    }
                }
                for (int attempt = 0; attempt < 12 && !picked; ++attempt) {
                    const float r = 0.6f * radius_ * std::sqrt(unit()), a = 2.0f * kPi * unit();
                    const Vec3f g{centre_.x + r * std::sin(a), me.y, centre_.z + r * std::cos(a)};
                    if (walkable(g.x, g.z)) {
                        h.goal = g;
                        picked = true;
                    }
                }
                if (!picked) h.goal = centre_;
                h.goal_left = 180 + int(240.0f * unit());
            }
            h.mode = Human::Mode::roam;
            if (nearest > 25.0f && unit() < 0.03f) {
                // A moment's stop (it looks at the screen).
                h.mode = Human::Mode::stop;
                h.mode_left = 20 + int(50.0f * unit());
                h.want_magnitude = 0.0f;
            } else {
                const float gx = h.goal.x - me.x, gz = h.goal.z - me.z;
                const float angle = std::atan2(gx, gz) + (unit() + unit() - 1.0f) * (15.0f * kPi / 180.0f);
                h.want_x = std::sin(angle);
                h.want_z = std::cos(angle);
                h.want_magnitude = 0.75f + 0.25f * unit();
            }
        }
        // The ring's wall: a way that leads past 85 % of the radius bends inwards.
        if (h.want_magnitude > 0.0f) {
            const Vec3f ahead{me.x + 20.0f * h.want_x, me.y, me.z + 20.0f * h.want_z};
            const float out = flat_distance(ahead, centre_);
            const float r = flat_distance(me, centre_);
            if (out > 0.85f * radius_ && r > 1e-3f) {
                const float k = std::clamp((out - 0.8f * radius_) / (0.15f * radius_), 0.0f, 1.0f) * 1.5f;
                float wx = h.want_x + k * (centre_.x - me.x) / r, wz = h.want_z + k * (centre_.z - me.z) / r;
                const float l = std::hypot(wx, wz);
                if (l > 1e-3f) {
                    h.want_x = wx / l;
                    h.want_z = wz / l;
                }
            }
        }
        const bool fleeing = h.mode == Human::Mode::escape;
        h.look_in = fleeing ? 6 + int(9.0f * unit()) : 20 + int(21.0f * unit());
        h.reaction = 15 + int(4.0f * unit());
    }
    // The thumb: the stick turns to the wanted way in about five steps.
    {
        const float k = 0.35f;
        float x = h.dx + (h.want_x - h.dx) * k, z = h.dz + (h.want_z - h.dz) * k;
        const float l = std::hypot(x, z);
        if (l < 0.2f) {
            x = h.want_x;
            z = h.want_z;
        } else {
            x /= l;
            z /= l;
        }
        h.dx = x;
        h.dz = z;
        h.magnitude += (h.want_magnitude - h.magnitude) * 0.3f;
    }
    if (h.magnitude > 0.05f && (h.dx != 0.0f || h.dz != 0.0f)) input.stick = stick_for(engine, me, h.dx, h.dz, std::min(1.0f, h.magnitude));
    bot_buttons(engine, input, 1, 4, 45);
    return host_pad_sample(input);
}

} // namespace dw4::engine::overflow
