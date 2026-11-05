// Data Overflow (docs/SURVIVAL.md 6.2-6.4): the director's Art, the weapon
// ladder and the techniques.
//
// The Art is the form's own first swing: the weapon hitboxes (kind 10000)
// of SQ_ATCK1's section for the weapon's label, taken from the player's
// sequences and attached as aimed port hitboxes (H6) without the attack
// animation, so the Digimon keeps walking. Each strike turns the pattern to
// the target's bearing and pushes it out until its tip reaches the target;
// the boxes of a strike share their struck list (a target is struck once).
// The swing's own effect action (VATCX1 / VATCS1) is broadcast for the
// picture and its effects turned the same way.
//
// The ladder (6.3): the socket holds the strongest weapon of the Art's
// class among the families the Forge has opened whose SKILLPOINT the class
// skill reaches (DW4's own rule), and the model in hand changes with the
// family (SURVIVAL_O1A.md S5: load the package, PLAYERARMS GENERATE).
//
// Techniques (6.4): the form's own TECH1-6 as DW4's recalc_player learns and
// ranks them (the run's level lock, H8), O1a's five only (Fire, Cutter, Bolt,
// Impact, Heal Lt); fired with the action broadcast the quick panel's SQ_WAZA
// would play, without the cast animation, on cooldowns. Only the heal costs
// MP (the user's answer 3). The shells start towards the nearest enemy.
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
constexpr float kAimRange = 40.0f;   // the shells' targets (6.4)
constexpr float kImpactRange = 20.0f;
constexpr int kTagArt = 1;

struct ArtDef {
    const char* form;
    const char* name;
    int ladder_base;     // the weapon row of the class's family 1 alpha
    const char* swing;   // the swing's effect action
    const char* label;   // the weapon label of SQ_ATCK1 (fallback)
    float factor;        // the class's own Art factor on top of 2.5 (14.3: two hits for every phase 1 family)
    std::array<const char*, 4> forge;
};
// SURVIVAL.md 6.3 (O1a: Cleaver and Blade Arc). The class factors make
// Kokatorimon's warm-up HP (54) a two-hit kill at level 1 (overflow_balance);
// Cleaver's is the larger: Veemon has Impact and Bolt's jumps for crowds,
// Agumon only its Art (the review round's bot runs: Agumon fell minutes
// behind Veemon with a walking player).
constexpr ArtDef kArts[] = {
    {"p_agumon", "Cleaver", 0, "VATCX1", "axe", 2.0f, {"a second line on the next target", "+20 % damage", "side lines", "a bigger fan"}},
    {"p_vmon", "Blade Arc", 25, "VATCS1", "slash", 1.4f, {"an arc on the next target", "+20 % damage", "the return arc", "+30 % size"}},
};

// O1a's techniques (pcTech base rows) with their cooldowns, cast sounds and
// damage factors. Impact's rings strike every enemy within 20 at once each
// time: x 0.5 (the review round's bot runs: with it Veemon outlasted Agumon
// by minutes).
struct TechDef {
    int family;
    int cooldown;
    const char* sound;
    bool aim, heal;
    float damage;
};
constexpr TechDef kTechniques[] = {
    {0, 150, "plMAGIC1", true, false, 1.0f},   // Fire, 2.5 s
    {30, 150, "plMAGIC1", true, false, 1.0f},  // Cutter, 2.5 s
    {10, 150, "plMAGIC1", true, false, 1.0f},  // Bolt, 2.5 s
    {25, 120, "plIMPACT", false, false, 0.5f}, // Impact, 2.0 s
    {40, 720, "plHEAL01", false, true, 1.0f},  // Heal Lt, 12 s
};
} // namespace

float technique_damage(int family) {
    for (const auto& def : kTechniques)
        if (def.family == family) return def.damage;
    return 1.0f;
}

float art_class_factor(std::string_view form) {
    for (const auto& def : kArts)
        if (form == def.form) return def.factor;
    return 1.0f;
}

// The Art of the form, its pattern and the techniques (field start).
void OverflowDirector::load_art_pattern(Engine& engine) {
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    auto* models = engine.get<ModelCtrlManager>();
    if (!sys || !db || !models) return;
    const ArtDef* art = &kArts[0];
    for (const auto& def : kArts)
        if (config_.form == def.form) art = &def;
    art_name_ = art->name;
    art_swing_ = art->swing;
    ladder_base_ = art->ladder_base;
    art_skill_ = std::clamp(db->number("weapon", ladder_base_, "SKILL").value_or(0), 0, 5);
    for (int i = 0; i < 4; ++i) forge_steps_[std::size_t(i)] = art->forge[std::size_t(i)];
    family_names_.clear();
    // The Forge's families: the class's rows of the weapon table, five a
    // family (alpha to epsilon), as long as they are there and of the Art's
    // class (the form's data; a class with one family has no Forge).
    offer_state_.forge_families = 0;
    for (int f = 0; f < 5; ++f) {
        std::string name = detail::plain_name(db->text("weapon", ladder_base_ + f * 5, "NAME"));
        // "Battle Hawk α" -> "Battle Hawk" (the family; the grade is the ladder's rung)
        if (const auto space = name.find_last_of(' '); space != std::string::npos) name.erase(space);
        const bool there = !name.empty() && db->number("weapon", ladder_base_ + f * 5, "SKILL").value_or(-1) == art_skill_;
        if (there && offer_state_.forge_families == f) offer_state_.forge_families = f + 1;
        family_names_.push_back(name);
    }
    offer_state_.forge_families = std::max(1, offer_state_.forge_families);
    engine.log("overflow: Forge families " + std::to_string(offer_state_.forge_families));
    auto* self = models->find(sys->players[0].digimon, 0);
    art_label_ = art->label;
    if (self)
        if (const auto found = self->labels.find("SQ_ATCK1"); found != self->labels.end() && !found->second.empty()) art_label_ = found->second;
    pattern_.clear();
    if (self && self->sequences)
        for (const auto& seq : *self->sequences) {
            if (seq.name != "SQ_ATCK1") continue;
            bool in = false;
            for (const auto& command : seq.commands) {
                const auto type = command.type();
                if (type == "beModelCtrlLABEL") {
                    if (in) break;
                    in = command.str(2) == art_label_;
                    continue;
                }
                if (!in || type != "beModelCtrlHITBOX" || command.str(2) != "Set" || command.i32(3) / 10000 != 1) continue;
                Box box;
                box.delay = command.f32(4);
                box.end = box.delay + std::max(1.0f, command.f32(5));
                box.offset = command.vec3(6).value_or(Vec3f{});
                box.size = command.vec3(7).value_or(Vec3f{8, 8, 8});
                pattern_.push_back(box);
            }
        }
    if (pattern_.empty()) pattern_.push_back({{0, 4, 10}, {8, 8, 8}, 1, 6}); // (a form without the section: one box)
    // Without the swing's animation the boxes start at once (their own spacing kept).
    float first = pattern_[0].delay;
    pattern_tip_ = 0;
    for (const auto& box : pattern_) {
        first = std::min(first, box.delay);
        pattern_tip_ = std::max(pattern_tip_, box.offset.z);
    }
    for (auto& box : pattern_) {
        box.delay = box.delay - first + 1.0f;
        box.end = box.end - first + 1.0f;
    }
    // The techniques of the form that O1a plays, and the run level each
    // group's first one is learned at (the Train offers wait for it).
    techniques_.clear();
    offer_state_.unlock_level = {9999, 9999, 9999};
    const int species = sys->players[0].stats.species;
    for (int n = 1; n <= 6; ++n) {
        const int base = db->number("shinka", species, "TECH" + std::to_string(n)).value_or(-1);
        for (const auto& def : kTechniques) {
            if (def.family != base) continue;
            Technique t;
            t.family = base;
            t.cooldown = def.cooldown;
            t.sound = def.sound;
            t.aim = def.aim;
            t.heal = def.heal;
            std::string name = detail::plain_name(db->text("pcTech", base, "NAME"));
            if (const auto space = name.find(' '); space != std::string::npos) name.erase(0, space + 1); // "Petty Fire" -> "Fire"
            t.name = name;
            const int lock = db->number("shinka", species, "TECH" + std::to_string(n) + "_LEVEL").value_or(1);
            t.lock = std::max(1, rules_ ? rules_->technique_level(lock) : lock);
            const int group = db->number("pcTech", base, "SKILL").value_or(kBlastSkill) - kBlastSkill;
            t.group = std::clamp(group, 0, 2);
            techniques_.push_back(t);
            if (group >= 0 && group < 3) {
                offer_state_.groups[std::size_t(group)] = true;
                offer_state_.unlock_level[std::size_t(group)] = std::min(offer_state_.unlock_level[std::size_t(group)], t.lock);
            }
        }
    }
    for (auto& unlock : offer_state_.unlock_level)
        if (unlock == 9999) unlock = 0;
    engine.log("overflow: Art " + art_name_ + " (" + art_label_ + ", " + std::to_string(pattern_.size()) + " boxes, tip " +
               std::to_string(int(pattern_tip_)) + "), " + std::to_string(techniques_.size()) + " techniques");
}

// The ladder: the socket weapon becomes the strongest opened rung the
// class skill reaches; the model in hand follows in the next step.
void OverflowDirector::equip_ladder(Engine& engine, bool force_model) {
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !db) return;
    auto& player = sys->players[0];
    const int skill = player.stats.skill[std::size_t(art_skill_)];
    int best = ladder_base_;
    for (int f = 0; f < offer_state_.forge; ++f)
        for (int g = 0; g < 5; ++g) {
            const int row = ladder_base_ + f * 5 + g;
            if (db->number("weapon", row, "SKILLPOINT").value_or(99999) <= skill) best = row;
        }
    int index = player.power_socket.empty() ? -1 : player.power_socket[0];
    if (index < 0 || index >= int(player.devices.size()) || player.devices[std::size_t(index)].type != 0) {
        ItemDevice weapon;
        weapon.type = 0;
        player.devices.push_back(weapon);
        index = int(player.devices.size()) - 1;
        if (!player.power_socket.empty()) player.power_socket[0] = index;
    }
    auto& device = player.devices[std::size_t(index)];
    const int before = device.record;
    device.type = 0;
    device.record = best;
    device.flag = true;
    if (before != best && started_) notice(detail::plain_name(db->text("weapon", best, "NAME")));
    if (force_model || std::string(db->text("weapon", best, "MODEL")) != weapon_model_) weapon_dirty_ = true;
}

void OverflowDirector::skill_changed(Engine& engine, int slot, int skill, int before, int after) {
    (void)before;
    (void)after;
    if (slot == 0 && skill == art_skill_) equip_ladder(engine, false);
}

// The broadcast's effects (queued on the player until its next update) turn
// to the world yaw `yaw` (atan2(dx, dz)), their offsets with them.
void OverflowDirector::aim_effects(Engine& engine, std::size_t first, float yaw) {
    auto* models = engine.get<ModelCtrlManager>();
    auto* sys = engine.get<GameSystem>();
    if (!models || !sys) return;
    auto* self = models->find(sys->players[0].digimon, 0);
    if (!self || !self->node) return;
    const auto& m = self->node->matrix;
    const float facing = std::atan2(m(2, 0), m(2, 2));
    float delta = yaw - facing;
    while (delta > kPi) delta -= 2.0f * kPi;
    while (delta < -kPi) delta += 2.0f * kPi;
    const float s = std::sin(delta), c = std::cos(delta);
    for (std::size_t i = first; i < self->effects.size(); ++i) {
        auto& effect = self->effects[i];
        if (effect.started) continue;
        // (The rotation of an effect's GENERATE is in radians on top of its
        // creator's turn; SURVIVAL_O1A.md lane C: the probe's aim test.)
        effect.rotation.y += delta;
        const Vec3f o = effect.offset;
        effect.offset = {o.x * c + o.z * s, o.y, -o.x * s + o.z * c};
    }
}

void OverflowDirector::cast_art(Engine& engine) {
    if (clock_ < art_ready_) return;
    auto* sys = engine.get<GameSystem>();
    auto* models = engine.get<ModelCtrlManager>();
    if (!sys || !models) return;
    auto& player = sys->players[0];
    auto* self = models->find(player.digimon, 0);
    if (!self || !self->generated || !self->node || player.hp <= 0) return;
    const Vec3f me = ram_position(*self);
    struct Target {
        float d;
        Vec3f at;
    };
    // The reach grows with the walking speed of the last half second.
    const auto& t = config_.tuning;
    float speed = 0.0f;
    if (trail_.size() >= 2) speed = flat_distance(trail_.front(), trail_.back()) / float(trail_.size() - 1);
    const float reach = t.art_range + (t.art_range_moving - t.art_range) * std::clamp(speed / 0.70f, 0.0f, 1.0f);
    std::vector<Target> targets;
    for (const auto& ram : models->rams()) {
        if (!living_enemy(*ram)) continue;
        const Vec3f q = ram_position(*ram);
        const float d = flat_distance(q, me);
        if (d <= reach) targets.push_back({d, q});
    }
    if (targets.empty()) return; // (the Art waits for a target)
    std::stable_sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.d < b.d; });
    const int forge = offer_state_.forge;
    const bool cleaver = art_name_ == "Cleaver";
    const int strikes = std::min<int>(forge >= 2 ? 2 : 1, int(targets.size()));
    float first_yaw = 0;
    for (int k = 0; k < strikes; ++k) {
        const auto& target = targets[std::size_t(k)];
        const float yaw = std::atan2(target.at.x - me.x, target.at.z - me.z);
        if (k == 0) first_yaw = yaw;
        const float shift = std::max(0.0f, target.d - pattern_tip_);
        auto shared = std::make_shared<std::vector<std::pair<std::string, int>>>();
        auto lay = [&](float turn, float push, bool mirror, float later, const std::shared_ptr<std::vector<std::pair<std::string, int>>>& group) {
            for (const auto& box : pattern_) {
                ModelCtrlManager::ModelRam::Hitbox hit;
                hit.kind = 10000;
                hit.delay = box.delay + later;
                hit.end = box.end + later;
                hit.offset = {mirror ? -box.offset.x : box.offset.x, box.offset.y, box.offset.z};
                hit.size = box.size;
                if (!cleaver && forge >= 5) hit.size = {box.size.x * 1.3f, box.size.y * 1.3f, box.size.z * 1.3f};
                // Cleaver's fan: the boxes off the line and the tip.
                if (cleaver && forge >= 5 && (box.offset.x != 0.0f || box.offset.z >= pattern_tip_))
                    hit.size = {box.size.x * 1.5f, box.size.y, box.size.z * 1.5f};
                hit.tag = kTagArt;
                hit.aimed = true;
                hit.yaw = yaw + turn;
                hit.shift = push;
                hit.shared_struck = group;
                models->port_hitbox(*self, hit);
            }
        };
        lay(0.0f, shift, false, 0.0f, shared);
        if (cleaver && forge >= 4) {
            // Side lines: the line again at +-45 degrees (one strike: each target once).
            lay(kPi / 4.0f, 0.0f, false, 0.0f, shared);
            lay(-kPi / 4.0f, 0.0f, false, 0.0f, shared);
        }
        if (!cleaver && forge >= 4) lay(0.0f, shift, true, 6.0f, std::make_shared<std::vector<std::pair<std::string, int>>>());
    }
    // The swing's effect, turned to the first target.
    const std::size_t effects_before = self->effects.size();
    Packet swing;
    swing.strings.emplace_back(player.digimon);
    swing.ints.push_back(0);
    port_send(engine, "ACTION_BROADCAST", std::nullopt, -1, art_swing_, swing);
    aim_effects(engine, effects_before, first_yaw);
    // The cooldown: the weapon's DELAY x 2.2 frames, -5 % per Forge (6.3).
    const int delay = std::max(10, player.stats.attack_delay);
    art_cooldown_ = std::max(20, int(std::lround(double(delay) * 2.2 * (1.0 - 0.05 * double(forge - 1)))));
    art_ready_ = clock_ + std::uint64_t(art_cooldown_);
}

void OverflowDirector::cast_techniques(Engine& engine) {
    auto* sys = engine.get<GameSystem>();
    auto* models = engine.get<ModelCtrlManager>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !models || !db) return;
    auto& player = sys->players[0];
    auto* self = models->find(player.digimon, 0);
    if (!self || !self->generated || !self->node || player.hp <= 0) return;
    const Vec3f me = ram_position(*self);
    float nearest = 1e9f;
    Vec3f nearest_at{};
    for (const auto& ram : models->rams()) {
        if (!living_enemy(*ram)) continue;
        const Vec3f q = ram_position(*ram);
        const float d = flat_distance(q, me);
        if (d < nearest) {
            nearest = d;
            nearest_at = q;
        }
    }
    for (auto& t : techniques_) {
        if (clock_ < t.ready) continue;
        int row = -1;
        for (const int learned : player.wazalist)
            if (learned >= t.family && learned < t.family + 5) row = learned;
        if (row < 0) continue;
        if (t.heal) {
            if (double(player.hp) >= double(player.max_hp) * double(config_.tuning.heal_below)) continue;
            const int cost = technique_mp_cost(engine, row);
            if (player.mp < cost) continue; // (a heal that lacks MP waits)
            sys->change_mp(engine, 0, -cost);
        } else if (t.aim) {
            if (nearest > kAimRange) continue;
        } else if (nearest > kImpactRange) {
            continue;
        }
        // The action of the rank: SQ_WAZA's section for the pcTech LABEL.
        auto action = actions_.find(row);
        if (action == actions_.end()) {
            std::string name;
            const std::string label(db->text("pcTech", row, "LABEL"));
            if (self->sequences)
                for (const auto& seq : *self->sequences) {
                    if (seq.name != "SQ_WAZA") continue;
                    bool in = false;
                    for (const auto& command : seq.commands) {
                        if (command.type() == "beModelCtrlLABEL") {
                            if (in) break;
                            in = command.str(2) == label;
                        } else if (in && command.type() == "beModelCtrlACTION" && name.empty()) {
                            name = std::string(command.str(2));
                        }
                    }
                }
            action = actions_.emplace(row, name).first;
        }
        if (action->second.empty()) continue;
        const std::size_t effects_before = self->effects.size();
        Packet cast;
        cast.strings.emplace_back(player.digimon);
        cast.ints.push_back(0);
        port_send(engine, "ACTION_BROADCAST", std::nullopt, -1, action->second, cast);
        if (t.aim) aim_effects(engine, effects_before, std::atan2(nearest_at.x - me.x, nearest_at.z - me.z));
        // The cast sound the quick panel would start, at most every 4 steps.
        if (auto& at = sound_at_[t.sound]; steps_ >= at) {
            detail::play_sound(engine, t.sound);
            at = steps_ + 4;
        }
        t.ready = clock_ + std::uint64_t(t.cooldown);
        ++t.casts;
    }
}

std::string OverflowDirector::technique_name(int source) const {
    if (source == kTagArt) return art_name_;
    const int family = (-source) % 1000;
    for (const auto& t : techniques_)
        if (family >= t.family && family < t.family + 5) return t.name;
    return "other " + std::to_string(-source);
}

} // namespace dw4::engine::overflow
