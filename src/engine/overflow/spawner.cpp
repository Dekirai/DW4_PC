// Data Overflow (docs/SURVIVAL.md 4.2-4.4, 13.5): the director's spawner.
//
// Enemies come from the phase's pool (its own families weigh 3, the earlier
// phases' 0.5, so the pool always keeps RAMs in reserve; a new phase's
// families rise from 0 over its first 90 s) on a ring around the player:
// 32-46 units, 26-36 for the slow walkers, 20-25 for a family that never
// walks (SURVIVAL_O1A.md S1: from 70-95 most families idle; T6's close-in
// target moved the 40-60 of S1 closer), 70 % of them on the side the player
// walks towards, out of the picture where a point is (S3; one in the
// picture enters with SQ_ENTR at least visible_min away, never next to the
// player), on the arena's walkable cells. The director tops the living
// count up to A(t) (+ the surges) with at most two spawns every ten steps,
// makes one enemy a minute Corrupted (HP x 8; the run's damage-taken rule
// gives it x 1.5), and brings enemies back near the player that stay far,
// stand still or were left behind by a moving player (4.3: no closer for
// 4 s while farther than 40), with the HP they had; at 7:30, 12:30 and 17:30
// it encircles the player with the 16 farthest living enemies, generated
// again in their own RAMs (no new RAM is needed), four a step.
#include "dw4/engine/overflow_director.hpp"

#include "director_util.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::engine::overflow {

using detail::flat_distance;
using detail::living_enemy;
using detail::port_send;
using detail::ram_position;

namespace {
constexpr float kPi = 3.14159265358979f;
}

// A family's spawn ring (tables.inc, or a balance pass's own).
int OverflowDirector::family_ring(const FamilyRow& row) const {
    if (const auto found = config_.family_ring.find(row.family); found != config_.family_ring.end()) return std::clamp(found->second, 0, 2);
    return row.ring;
}
float OverflowDirector::ring_min(const FamilyRow& row) const {
    const auto& t = config_.tuning;
    const int ring = family_ring(row);
    return ring == 2 ? t.near_min : ring == 1 ? t.slow_min : t.ring_min;
}
float OverflowDirector::ring_max(const FamilyRow& row) const {
    const auto& t = config_.tuning;
    const int ring = family_ring(row);
    return ring == 2 ? t.near_max : ring == 1 ? t.slow_max : t.ring_max;
}

// The arena's walkable cells (4.3): a 5-unit grid of the collision floor
// within floor height +-9 of the player's start, flooded from the start so
// nothing outside the ring wall counts.
void OverflowDirector::build_walkable(Engine& engine) {
    grid_.assign(std::size_t(grid_n_ * grid_n_), Cell{});
    auto* lands = engine.get<HitLandManager>();
    const Vec3f start = player_position(engine);
    if (!lands) return;
    std::vector<Cell> raw(grid_.size());
    for (int iz = 0; iz < grid_n_; ++iz)
        for (int ix = 0; ix < grid_n_; ++ix) {
            const float x = grid_x0_ + grid_step_ * float(ix), z = grid_z0_ + grid_step_ * float(iz);
            const auto land = lands->world().check_land({x, start.y + 60.0f, z}, 1.0f, 400.0f, 400.0f);
            auto& cell = raw[std::size_t(iz * grid_n_ + ix)];
            cell.walkable = land.found && std::fabs(land.position.y - start.y) <= 9.0f;
            cell.y = land.found ? land.position.y : start.y;
        }
    const int sx = std::clamp(int(std::lround((start.x - grid_x0_) / grid_step_)), 0, grid_n_ - 1);
    const int sz = std::clamp(int(std::lround((start.z - grid_z0_) / grid_step_)), 0, grid_n_ - 1);
    std::vector<int> queue;
    if (raw[std::size_t(sz * grid_n_ + sx)].walkable) queue.push_back(sz * grid_n_ + sx);
    std::vector<bool> seen(raw.size());
    if (!queue.empty()) seen[std::size_t(queue[0])] = true;
    double cx = 0, cz = 0;
    int count = 0;
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const int at = queue[q];
        grid_[std::size_t(at)] = raw[std::size_t(at)];
        const int ix = at % grid_n_, iz = at / grid_n_;
        cx += grid_x0_ + grid_step_ * float(ix);
        cz += grid_z0_ + grid_step_ * float(iz);
        ++count;
        const int next[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : next) {
            const int nx = ix + d[0], nz = iz + d[1];
            if (nx < 0 || nz < 0 || nx >= grid_n_ || nz >= grid_n_) continue;
            const int n = nz * grid_n_ + nx;
            if (seen[std::size_t(n)] || !raw[std::size_t(n)].walkable) continue;
            seen[std::size_t(n)] = true;
            queue.push_back(n);
        }
    }
    if (count > 0) {
        centre_ = {float(cx / count), start.y, float(cz / count)};
        float far = 0;
        for (const int at : queue) {
            const float x = grid_x0_ + grid_step_ * float(at % grid_n_), z = grid_z0_ + grid_step_ * float(at / grid_n_);
            far = std::max(far, std::hypot(x - centre_.x, z - centre_.z));
        }
        radius_ = far;
    }
    engine.log("overflow: arena " + std::to_string(count) + " walkable cells, centre (" + std::to_string(int(centre_.x)) + ", " +
               std::to_string(int(centre_.z)) + "), radius " + std::to_string(int(radius_)));
}

bool OverflowDirector::spawn_point(Engine& engine, const Vec3f& centre, float lo, float hi, bool ahead, Vec3f& out, float visible_min) {
    // The walking direction: the player's way over the last half second.
    float heading = 0;
    bool walking = false;
    if (trail_.size() >= 2) {
        const Vec3f a = trail_.front(), b = trail_.back();
        if (flat_distance(a, b) > 3.0f) {
            heading = std::atan2(b.x - a.x, b.z - a.z);
            walking = true;
        }
    }
    // A point out of the picture wins; else one in the picture at least
    // visible_min away (its draw pushed out to it on the floor), else the
    // farthest one in the picture.
    std::optional<Vec3f> seen;
    float seen_distance = -1.0f;
    const auto& t = config_.tuning;
    const float side_min = t.ahead_angle_min * kPi / 180.0f, side_max = t.ahead_angle_max * kPi / 180.0f;
    for (int attempt = 0; attempt < 16; ++attempt) {
        // Ahead: beside the way (ahead_angle_min..max off the heading, left
        // or right), so a walking player passes it rather than runs into it.
        float angle;
        if (ahead && walking) {
            const float u = float(next_unit(random_) * 2.0 - 1.0);
            angle = heading + (u < 0.0f ? -1.0f : 1.0f) * (side_min + std::fabs(u) * (side_max - side_min));
        } else {
            angle = float(next_unit(random_)) * 2.0f * kPi;
        }
        const float r = lo + float(next_unit(random_)) * (hi - lo);
        const float x = centre.x + r * std::sin(angle), z = centre.z + r * std::cos(angle);
        const int ix = int(std::lround((x - grid_x0_) / grid_step_)), iz = int(std::lround((z - grid_z0_) / grid_step_));
        if (ix < 0 || iz < 0 || ix >= grid_n_ || iz >= grid_n_) continue;
        const auto& cell = grid_[std::size_t(iz * grid_n_ + ix)];
        if (!cell.walkable) continue;
        const Vec3f point{x, cell.y, z};
        if (!keeps_gap(point)) {
            ++gap_refused_;
            continue;
        }
        // Out of the picture where it can be (S3); a visible one enters with
        // SQ_ENTR, at least visible_min away (not next to the player).
        if (!on_screen(engine, {x, cell.y + 5.0f, z})) {
            out = point;
            return true;
        }
        if (seen_distance >= visible_min) continue;
        Vec3f candidate = point;
        float distance = r;
        if (r < visible_min) {
            const float vx = centre.x + visible_min * std::sin(angle), vz = centre.z + visible_min * std::cos(angle);
            const int jx = int(std::lround((vx - grid_x0_) / grid_step_)), jz = int(std::lround((vz - grid_z0_) / grid_step_));
            if (jx >= 0 && jz >= 0 && jx < grid_n_ && jz < grid_n_ && grid_[std::size_t(jz * grid_n_ + jx)].walkable) {
                candidate = Vec3f{vx, grid_[std::size_t(jz * grid_n_ + jx)].y, vz};
                distance = visible_min;
            }
        }
        if (distance > seen_distance) {
            seen = candidate;
            seen_distance = distance;
        }
    }
    if (!seen) return false;
    out = *seen;
    return true;
}

// The way out (Tuning::keep_gap_until): the eight sectors round the player
// with a living enemy within gap_radius, once a step before the spawner.
void OverflowDirector::update_gap(Engine& engine) {
    gap_sectors_ = 0;
    gap_active_ = clock_ < std::uint64_t(std::max(0, config_.tuning.keep_gap_until));
    if (!gap_active_) return;
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    gap_centre_ = player_position(engine);
    for (const auto& ram : models->rams()) {
        if (!living_enemy(*ram)) continue;
        const Vec3f q = ram_position(*ram);
        if (flat_distance(q, gap_centre_) > config_.tuning.gap_radius) continue;
        const float angle = std::atan2(q.x - gap_centre_.x, q.z - gap_centre_.z) + kPi;
        gap_sectors_ |= 1u << std::clamp(int(angle / (kPi / 4.0f)), 0, 7);
    }
}

bool OverflowDirector::keeps_gap(const Vec3f& point) const {
    if (!gap_active_ || flat_distance(point, gap_centre_) > config_.tuning.gap_radius) return true;
    const float angle = std::atan2(point.x - gap_centre_.x, point.z - gap_centre_.z) + kPi;
    const std::uint32_t closed = gap_sectors_ | (1u << std::clamp(int(angle / (kPi / 4.0f)), 0, 7));
    for (int k = 0; k < 8; ++k)
        if (!(closed & (1u << k)) && !(closed & (1u << ((k + 1) % 8)))) return true;
    return false;
}

// The spawn ring's least distance of a point in the picture: the families
// that never walk keep their near ring (they must be met where they stand).
float OverflowDirector::visible_min(const FamilyRow& row) const { return family_ring(row) == 2 ? 0.0f : config_.tuning.visible_min; }

// GENERATE one enemy of `row`: a free RAM (or `reuse_ram`, an encirclement or
// a recycle), its Normal record and the run's HP (4.3), SQ_ENTR, ONCTRL. An
// enemy that comes again (`reuse_ram`) keeps `hp_share` of its HP: a hurt
// one does not come back healed.
bool OverflowDirector::spawn_one(Engine& engine, const FamilyRow& row, bool corrupted, std::optional<Vec3f> at, int reuse_ram, double hp_share) {
    auto* db = engine.get<DatabaseManager>();
    if (!db) return false;
    int ram = reuse_ram;
    if (ram < 0) {
        Packet find;
        port_send(engine, "MODELCTRL", row.family, -1, "FIND_SPACE", find);
        if (find.ints.empty()) return false;
        ram = find.ints[0];
    }
    Vec3f point;
    if (at) {
        point = *at;
    } else {
        const auto& t = config_.tuning;
        const bool ahead = next_unit(random_) < double(t.ahead_share);
        if (!spawn_point(engine, player_position(engine), ring_min(row), ring_max(row), ahead, point, visible_min(row))) {
            ++spawn_failures_;
            return false;
        }
    }
    const auto& t = config_.tuning;
    double hp = double(db->number("enemy", row.record, "HPMAX").value_or(50)) * hp_factor(run_seconds() / 60.0);
    if (corrupted) hp *= double(t.corrupted_hp);
    Packet generate;
    generate.vectors = {point, Vec3f{0, 0, 0}};
    generate.strings.emplace_back(std::string("SQ_ENTR"));
    const int max_hp = std::max(1, int(std::lround(hp)));
    generate.ints = {row.record, max_hp};
    port_send(engine, "MODELCTRL", row.family, ram, "GENERATE", generate);
    port_send(engine, "MODELCTRL", row.family, ram, "ONCTRL");
    if (hp_share < 1.0) {
        Packet keep;
        keep.ints.push_back(std::clamp(int(std::lround(double(max_hp) * hp_share)), 1, max_hp));
        port_send(engine, "MODELCTRL", row.family, ram, "SETHP", keep);
    }
    if (rules_) rules_->set_enemy_class(row.family, ram, corrupted ? EnemyClass::corrupted : EnemyClass::normal);
    if (corrupted) {
        // A sign over it (the status line's rare icon; the metal look of 4.4
        // is O1b's check).
        Packet icon;
        icon.ints.push_back(1);
        port_send(engine, "MODELCTRL", row.family, ram, "SETRAREICON", icon);
    }
    // The tracking entry of that RAM starts over.
    Enemy* entry = nullptr;
    for (auto& e : enemies_)
        if (e.ram == ram && e.family == row.family) entry = &e;
    if (!entry) entry = &enemies_.emplace_back();
    *entry = Enemy{};
    entry->family = row.family;
    entry->ram = ram;
    entry->born = clock_;
    entry->closer_at = clock_;
    entry->corrupted = corrupted;
    entry->nearest = flat_distance(point, player_position(engine));
    entry->best = entry->nearest;
    entry->best_at = clock_;
    entry->anchor = point;
    if (gap_active_ && flat_distance(point, gap_centre_) <= config_.tuning.gap_radius) {
        const float angle = std::atan2(point.x - gap_centre_.x, point.z - gap_centre_.z) + kPi;
        gap_sectors_ |= 1u << std::clamp(int(angle / (kPi / 4.0f)), 0, 7);
    }
    ++spawned_;
    if (corrupted) ++corrupted_spawned_;
    return true;
}

float OverflowDirector::knots(const std::array<float, 5>& at, double minutes) {
    const double k = std::clamp(minutes / 5.0, 0.0, 4.0);
    const int i = std::min(3, int(k));
    return float(double(at[std::size_t(i)]) + (double(at[std::size_t(i + 1)]) - double(at[std::size_t(i)])) * (k - double(i)));
}

void OverflowDirector::spawner(Engine& engine) {
    const auto& t = config_.tuning;
    // Corrupted: from 1:30 one a minute (the next spawn).
    bool corrupted = false;
    if (clock_ >= std::uint64_t(t.corrupted_from) && clock_ >= next_corrupted_) corrupted = true;
    // Encirclements (4.3) by reuse, a few enemies a step (T8: one step for
    // all 16 GENERATEs was the run's slowest).
    if (encircled_ < t.encircle_at.size() && clock_ >= std::uint64_t(t.encircle_at[encircled_])) {
        encircle(engine);
        ++encircled_;
    }
    place_encircled(engine);
    if (clock_ < next_spawn_) return;
    next_spawn_ = clock_ + std::uint64_t(t.spawn_every);
    const double minutes = run_seconds() / 60.0;
    int target = std::min(t.alive_cap, int(knots(t.alive_at, minutes)));
    // Surges: every 90 s, 12 more for 20 s (the first at surge_first).
    if (clock_ >= std::uint64_t(t.surge_first) &&
        ((clock_ - std::uint64_t(t.surge_first)) % std::uint64_t(t.surge_every)) < std::uint64_t(t.surge_length))
        target += t.surge_extra;
    // The refill rate: spawns a second, kept as a credit of whole spawns.
    refill_credit_ = std::min(refill_credit_ + double(knots(t.refill_at, minutes)) * double(t.spawn_every) / 60.0,
                              double(std::max(1, t.spawns_per_tick)));
    int want = std::min({t.spawns_per_tick, target - alive_, int(refill_credit_ + 1e-9)});
    if (want <= 0 && !corrupted) return;
    want = std::max(want, corrupted ? 1 : 0);
    // A new phase's families come in over its first 90 s (their weight rises
    // from 0 to 3, past the earlier phases' 0.5 after 15 s), so a phase
    // change is no wall.
    // (Phase 4's own time: last_ramp_steps; past its five minutes it stays in.)
    const std::uint64_t into = clock_ - std::uint64_t(phase() - 1) * std::uint64_t(t.phase_steps);
    const int ramp_steps = phase() == 4 && t.last_ramp_steps > 0 ? t.last_ramp_steps : t.phase_ramp_steps;
    const float ramp = phase() == 1 ? 1.0f : std::min(1.0f, float(into) / float(std::max(1, ramp_steps)));
    auto pool = phase_pool(phase(), 3.0f * ramp, 0.5f);
    std::erase_if(pool, [](const PoolEntry& entry) { return entry.weight <= 0.0f; });
    double total = 0;
    for (const auto& entry : pool) total += entry.weight;
    for (int n = 0; n < want; ++n) {
        bool made = false;
        for (int attempt = 0; attempt < 6 && !made; ++attempt) {
            double pick = next_unit(random_) * total;
            const FamilyRow* row = pool.back().row;
            for (const auto& entry : pool) {
                pick -= entry.weight;
                if (pick < 0) {
                    row = entry.row;
                    break;
                }
            }
            // (A family whose RAMs are all in use: FIND_SPACE finds none, another is drawn.)
            made = spawn_one(engine, *row, corrupted);
        }
        if (made) {
            ++alive_;
            refill_credit_ = std::max(0.0, refill_credit_ - 1.0);
            if (corrupted) {
                corrupted = false;
                next_corrupted_ = clock_ + std::uint64_t(t.corrupted_every);
                event_log_.push_back({clock_, "corrupted"});
            }
        }
    }
}

void OverflowDirector::encircle(Engine& engine) {
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    const Vec3f me = player_position(engine);
    struct Far {
        float distance;
        ModelCtrlManager::ModelRam* ram;
    };
    std::vector<Far> living;
    for (const auto& ram : models->rams())
        if (living_enemy(*ram) && family_row(ram->info_name)) living.push_back({flat_distance(ram_position(*ram), me), ram.get()});
    std::stable_sort(living.begin(), living.end(), [](const Far& a, const Far& b) { return a.distance > b.distance; });
    const int count = std::min<int>(config_.tuning.encircle_count, int(living.size()));
    encircle_queue_.clear();
    encircle_placed_ = 0;
    for (int k = 0; k < count; ++k) {
        const auto* ram = living[std::size_t(k)].ram;
        Encircled entry;
        entry.family = ram->info_name;
        entry.ram = ram->index;
        entry.slot = k;
        entry.count = count;
        for (const auto& e : enemies_)
            if (e.ram == ram->index && e.family == ram->info_name) entry.corrupted = e.corrupted;
        entry.hp_share = ram->max_hp > 0 ? std::clamp(double(ram->hp) / double(ram->max_hp), 0.0, 1.0) : 1.0;
        encircle_queue_.push_back(entry);
    }
    event_log_.push_back({clock_, "encirclement " + std::to_string(count)});
    place_encircled(engine);
}

// The queued encirclement: encircle_per_step enemies a step, each evenly
// round the player on the 45-unit ring (a point off the floor turns a
// little further), with the HP it had.
void OverflowDirector::place_encircled(Engine& engine) {
    if (encircle_queue_.empty()) return;
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    const Vec3f me = player_position(engine);
    const int per_step = std::max(1, config_.tuning.encircle_per_step);
    int done = 0;
    while (!encircle_queue_.empty() && done < per_step) {
        const Encircled entry = encircle_queue_.front();
        encircle_queue_.erase(encircle_queue_.begin());
        ++done;
        const auto* row = family_row(entry.family);
        const auto* ram = models->find(entry.family, entry.ram);
        if (!row || !ram || !living_enemy(*ram)) continue; // (defeated meanwhile)
        for (int turn = 0; turn < 8; ++turn) {
            const float angle = 2.0f * kPi * (float(entry.slot) + 0.15f * float(turn)) / float(entry.count);
            const float x = me.x + config_.tuning.encircle_ring * std::sin(angle), z = me.z + config_.tuning.encircle_ring * std::cos(angle);
            const int ix = int(std::lround((x - grid_x0_) / grid_step_)), iz = int(std::lround((z - grid_z0_) / grid_step_));
            if (ix < 0 || iz < 0 || ix >= grid_n_ || iz >= grid_n_ || !grid_[std::size_t(iz * grid_n_ + ix)].walkable) continue;
            if (spawn_one(engine, *row, entry.corrupted, Vec3f{x, grid_[std::size_t(iz * grid_n_ + ix)].y, z}, entry.ram, entry.hp_share)) {
                ++encircle_placed_;
                ++encircled_total_;
            }
            break;
        }
    }
    if (encircle_queue_.empty()) event_log_.push_back({clock_, "encircled " + std::to_string(encircle_placed_)});
}

// Each step: the living count, each enemy's approach (14.2's close-in
// target: within 30 units in 8 s), recycling (4.3).
void OverflowDirector::track_enemies(Engine& engine) {
    auto* models = engine.get<ModelCtrlManager>();
    if (!models) return;
    const auto& t = config_.tuning;
    const Vec3f me = player_position(engine);
    alive_ = 0;
    int recycles = 0;
    for (auto& e : enemies_) {
        if (!e.alive) continue;
        auto* ram = models->find(e.family, e.ram);
        if (!ram || !living_enemy(*ram)) {
            // Gone (defeated, erased): one defeated before its 8 s counts only
            // when it had come close (14.2 measures the approach, not the kill).
            e.alive = false;
            if (!e.counted) {
                e.counted = true;
                const auto* row = family_row(e.family);
                if (e.closed && row && !row->ranged) {
                    ++closed_by_family_[e.family].first;
                    ++closed_by_family_[e.family].second;
                }
            }
            continue;
        }
        ++alive_;
        const Vec3f at = ram_position(*ram);
        const float d = flat_distance(at, me);
        e.nearest = std::min(e.nearest, d);
        // (An idler stands: it has not moved 4 units in 6 s. Measured on the
        // enemy, not on the distance, so a player who keeps away does not
        // make every follower an idler.)
        if (flat_distance(at, e.anchor) > 4.0f) {
            e.anchor = at;
            e.closer_at = clock_;
        }
        const auto* row = family_row(e.family);
        if (!e.counted) {
            if (d <= 30.0f) e.closed = true;
            if (e.closed || clock_ - e.born >= 480) {
                e.counted = true;
                if (row && !row->ranged) {
                    auto& tally = closed_by_family_[e.family];
                    tally.first += e.closed ? 1 : 0;
                    ++tally.second;
                }
            }
        }
        // (Left behind: it has not come 2 units closer than its best for
        // behind_steps. A standing player's followers keep coming closer;
        // a moving player's fall behind and come again on its way.)
        if (d < e.best - 2.0f) {
            e.best = d;
            e.best_at = clock_;
        }
        e.far_since = d > t.recycle_far ? (e.far_since ? e.far_since : clock_) : 0;
        const bool far = e.far_since && clock_ - e.far_since >= std::uint64_t(t.recycle_far_steps);
        // (The ranged families keep their distance on purpose: only the far rule moves them.)
        const bool idle = d > t.idle_far && clock_ - e.closer_at >= std::uint64_t(t.idle_steps) && row && !row->ranged;
        // (The ranged families keep their distance on purpose and are not brought
        // back on the way: a walking player leaves their shots behind.)
        const bool behind = row && !row->ranged && d > t.behind_far && clock_ - e.best_at >= std::uint64_t(t.behind_steps);
        if ((far || idle || behind) && row && recycles < 2) {
            // Its RAM comes back near the player (ahead of it) with the HP it had.
            Vec3f point;
            if (spawn_point(engine, me, ring_min(*row), ring_max(*row), true, point, visible_min(*row))) {
                const bool corrupted = e.corrupted;
                const double share = ram->max_hp > 0 ? std::clamp(double(ram->hp) / double(ram->max_hp), 0.0, 1.0) : 1.0;
                spawn_one(engine, *row, corrupted, point, e.ram, share);
                ++recycled_;
                ++recycles;
            }
        }
    }
    // Forget the entries of RAMs that are gone and not reused (keeps the list short).
    std::erase_if(enemies_, [](const Enemy& e) { return !e.alive && e.counted; });
}

// Drops of the step's defeats (Corrupted enemies: a Plug and at 20 % an HP
// Disk alpha, 4.4), made outside the hit scripts.
void OverflowDirector::drop_items(Engine& engine) {
    for (const auto& drop : drops_) {
        Packet find;
        port_send(engine, "MODELCTRL", drop.model, -1, "FIND_SPACE", find);
        if (find.ints.empty()) continue; // (8 RAMs per pickup model: a drop that finds none is lost in O1a)
        Packet generate;
        generate.vectors.push_back(drop.at);
        generate.ints = {drop.record, drop.table};
        port_send(engine, "MODELCTRL", drop.model, find.ints[0], "GENERATE", generate);
    }
    drops_.clear();
}

} // namespace dw4::engine::overflow
