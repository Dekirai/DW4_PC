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
#include <cstring>

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
    // Round O1b: Guilmon's dagger (two thrust boxes at z 11 and 16, Dyno Tooth
    // alpha TSUYOSA 48: the weakest start, 14.3's closest case) and Dorumon's
    // katana (the 5-box arc of x +-9, Kamikiri Maru alpha 54). Fang Thrust's
    // Forge steps are 6.3's (Forge 3 pierces: the thrust goes on through its
    // target for two more boxes; Forge 5 reaches 30 standing); Crescent Draw
    // aims at the densest group (6.3), its Forge 4 / 5 steps are Blade Arc's
    // (the return line, +30 % size) until O2. (Every Art's Forge 3 adds 20 %.)
    {"p_girumon", "Fang Thrust", 50, "VATCE1", "dagger", 2.2f, {"a thrust at the next target", "pierces 2, +20 % damage", "one more thrust", "reach 30"}},
    {"p_dorumon", "Crescent Draw", 75, "VATCS1", "slash", 1.5f, {"an arc on the next target", "+20 % damage", "the return arc", "+30 % size"}},
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
    // Round O1b: the rest of the four Rookies' lists (6.4).
    {5, 150, "plMAGIC1", true, false, 1.0f},   // Ice, 2.5 s
    {15, 150, "plMAGIC1", true, false, 1.0f},  // Dark, 2.5 s
    {20, 180, "plVENOM", true, false, 1.0f},   // Venom, 3.0 s (at the densest group)
    {35, 120, "plMAGIC1", true, false, 0.5f},  // Bomb, 2.0 s (at the densest group; x 0.5 as Impact: round O1b lane E)
    {75, 900, "plMIND01", false, false, 1.0f}, // Resist, 15 s (a buff on the player)
    {80, 900, "plMIND01", false, false, 1.0f}, // Pwr Up, 15 s (a buff on the player)
    {70, 480, "plMIND01", false, false, 1.0f}, // Freeze, 8 s (a pulse on the enemies)
    {85, 480, "plMIND01", false, false, 1.0f}, // Pwr Dwn, 8 s (a pulse on the enemies)
    {95, 600, "plMIND01", false, false, 1.0f}, // Bind, 10 s (a pulse on the enemies)
};
// The force group (6.4): a buff on the player is cast with an enemy within
// kForceRange, a pulse on the enemies with three of them within it.
constexpr float kForceRange = 30.0f;
// Fang Thrust (6.3): the reach from Forge 5 and the pierce's boxes from Forge 3.
constexpr float kThrustReach = 30.0f;
constexpr int kPierceBoxes = 2;
// The densest group (6.3 Crescent Draw, 6.4 Bomb and Venom): the enemy with
// the most others within this many units.
constexpr float kGroupRadius = 10.0f;
bool is_buff(int family) { return family == 75 || family == 80; }
bool is_pulse(int family) { return family == 70 || family == 85 || family == 95; }
bool at_group(int family) { return family == 20 || family == 35; }
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
    // The awakened Art's burst (6.5): the class's charge release of the same
    // section (KATCX2, KATCS2, KATCE2: x_100charge01 SQ_KIRIKAE and SQ_OWARI on
    // the player), the swing's class letter if the section has none.
    art_burst_ = std::string("KATC") + (std::strlen(art->swing) > 4 ? art->swing[4] : 'X') + "2";
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
                if (in && type == "beModelCtrlACTION") {
                    const std::string action(command.str(2));
                    if (action.size() == 6 && action.starts_with("KATC") && action[5] == '2') art_burst_ = action;
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
               std::to_string(int(pattern_tip_)) + ", burst " + art_burst_ + "), " + std::to_string(techniques_.size()) + " techniques");
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
    // Awakening (6.5): with the last family forged and the class skill at the
    // signature's SKILLPOINT (2500), the form's signature weapon (its SOUBI
    // bit is the form's; rows 250-265) takes the socket by DW4's rule and
    // stays there.
    if (offer_state_.forge >= std::max(1, offer_state_.forge_families) && offer_state_.forge_families >= 5)
        for (int row = 250; row < 266; ++row) {
            const int soubi = db->number("weapon", row, "SOUBI").value_or(0);
            if (!(soubi & (1 << player.stats.species)) || soubi == 0xFFFF) continue;
            if (db->number("weapon", row, "SKILL").value_or(-1) != art_skill_) continue;
            if (db->number("weapon", row, "SKILLPOINT").value_or(99999) > skill) break;
            best = row;
            if (!awakened_) {
                awakened_ = true;
                event_log_.push_back({clock_, "awakening " + std::to_string(row)});
                detail::play_sound(engine, "plLVUP01");
                if (started_) notice("AWAKENING: " + detail::plain_name(db->text("weapon", row, "NAME")));
                if (rules_) rules_->show_panel(engine, 0, player.digimon); // 6.5: the SQ_TECUP panel
            }
            break;
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
    if (before != best && started_ && best < 250) notice(detail::plain_name(db->text("weapon", best, "NAME")));
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
    auto* sys = engine.get<GameSystem>();
    auto* models = engine.get<ModelCtrlManager>();
    if (!sys || !models) return;
    auto& player = sys->players[0];
    auto* self = models->find(player.digimon, 0);
    // The awakened Art's burst (6.5): the class's charge release once for a
    // strike that hit (damage_dealt marks it; here, outside the hit scripts).
    if (burst_pending_) {
        burst_pending_ = false;
        burst_done_ = art_ready_;
        if (self && self->generated && !art_burst_.empty()) {
            Packet burst;
            burst.strings.emplace_back(player.digimon);
            burst.ints.push_back(0);
            port_send(engine, "ACTION_BROADCAST", std::nullopt, -1, art_burst_, burst);
            ++bursts_;
        }
    }
    if (clock_ < art_ready_) return;
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
    const int forge = offer_state_.forge;
    const bool cleaver = art_name_ == "Cleaver";
    const bool thrust = art_name_ == "Fang Thrust";
    const bool crescent = art_name_ == "Crescent Draw";
    float reach = t.art_range + (t.art_range_moving - t.art_range) * std::clamp(speed / 0.70f, 0.0f, 1.0f);
    if (thrust && forge >= 5) reach = std::max(reach, kThrustReach); // (6.3: Forge 5, reach 30)
    std::vector<Target> targets;
    std::vector<Vec3f> living;
    for (const auto& ram : models->rams()) {
        if (!living_enemy(*ram)) continue;
        const Vec3f q = ram_position(*ram);
        const float d = flat_distance(q, me);
        living.push_back(q);
        if (d <= reach) targets.push_back({d, q});
    }
    if (targets.empty()) return; // (the Art waits for a target)
    std::stable_sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.d < b.d; });
    if (crescent && targets.size() > 1) {
        // Crescent Draw's first arc goes to the densest group in its reach
        // (6.3): the target with the most living enemies within 10 units (the
        // nearest of equals); a Forge 2 arc then takes the nearest other one.
        std::size_t best = 0;
        int most = -1;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            int count = 0;
            for (const auto& q : living) count += flat_distance(q, targets[i].at) <= kGroupRadius ? 1 : 0;
            if (count > most) {
                most = count;
                best = i;
            }
        }
        std::rotate(targets.begin(), targets.begin() + std::ptrdiff_t(best), targets.begin() + std::ptrdiff_t(best) + 1);
    }
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
                if (!cleaver && !thrust && forge >= 5) hit.size = {box.size.x * 1.3f, box.size.y * 1.3f, box.size.z * 1.3f};
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
            // Fang Thrust pierces from Forge 3 (6.3): the line goes on through
            // its target, two more boxes of the tip's spacing beyond the tip
            // (the target and about two behind it in a line), one struck list.
            if (thrust && forge >= 3 && pattern_.size() >= 2) {
                const auto tip = std::max_element(pattern_.begin(), pattern_.end(), [](const Box& a, const Box& b) { return a.offset.z < b.offset.z; });
                float spacing = 5.0f;
                for (const auto& box : pattern_)
                    if (box.offset.z < tip->offset.z) spacing = std::min(spacing, tip->offset.z - box.offset.z);
                for (int k = 1; k <= kPierceBoxes; ++k) {
                    ModelCtrlManager::ModelRam::Hitbox hit;
                    hit.kind = 10000;
                    hit.delay = tip->delay + later;
                    hit.end = tip->end + later;
                    hit.offset = {mirror ? -tip->offset.x : tip->offset.x, tip->offset.y, tip->offset.z + spacing * float(k)};
                    hit.size = tip->size;
                    hit.tag = kTagArt;
                    hit.aimed = true;
                    hit.yaw = yaw + turn;
                    hit.shift = push;
                    hit.shared_struck = group;
                    models->port_hitbox(*self, hit);
                }
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
    std::vector<Vec3f> near; // (the enemies within the shells' range: the densest group, the force group's counts)
    int within_force = 0;
    for (const auto& ram : models->rams()) {
        if (!living_enemy(*ram)) continue;
        const Vec3f q = ram_position(*ram);
        const float d = flat_distance(q, me);
        if (d < nearest) {
            nearest = d;
            nearest_at = q;
        }
        if (d <= kAimRange) near.push_back(q);
        within_force += d <= kForceRange ? 1 : 0;
    }
    // The densest group within 40 (6.4: Bomb and Venom): the enemy with the
    // most others within 10 units.
    Vec3f group_at = nearest_at;
    {
        int best = -1;
        for (const auto& a : near) {
            int count = 0;
            for (const auto& b : near) count += flat_distance(a, b) <= kGroupRadius ? 1 : 0;
            if (count > best) {
                best = count;
                group_at = a;
            }
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
        } else if (is_buff(t.family)) {
            if (within_force < 1) continue;   // (a buff while enemies are near)
        } else if (is_pulse(t.family)) {
            if (within_force < 3) continue;   // (a pulse on a group)
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
        const Vec3f& aim_at = at_group(t.family) ? group_at : nearest_at;
        if (t.aim) aim_effects(engine, effects_before, std::atan2(aim_at.x - me.x, aim_at.z - me.z));
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
