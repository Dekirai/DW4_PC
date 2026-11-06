// Data Overflow (docs/SURVIVAL.md 4, 13.5): the director's clock, phases,
// screens, end, run log and the kite bot. The spawner is in spawner.cpp,
// the Art, the ladder and the techniques in abilities.cpp, the offers and
// tables in offers.cpp.
#include "dw4/engine/overflow_director.hpp"

#include "director_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace dw4::engine::overflow {

using detail::flat_distance;
using detail::living_enemy;
using detail::port_send;
using detail::ram_position;

namespace {
constexpr int kTagArt = 1;
constexpr const char* kClassNames[6] = {"Blunt", "Slash", "Stab", "Bash", "Shot", "Crush"};
constexpr const char* kGroupNames[3] = {"Blast", "Heal", "Force"};
constexpr const char* kRankNames[5] = {"Petty", "Mega", "Giga", "Terra", "∞"};

std::string clock_text(double seconds) {
    const int whole = int(seconds);
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", whole / 60, whole % 60);
    return text;
}

double median(std::vector<int> values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 ? double(values[n / 2]) : 0.5 * double(values[n / 2 - 1] + values[n / 2]);
}
} // namespace

OverflowDirector::OverflowDirector(RunConfig config) : config_(std::move(config)) {
    random_ = 0x0F10F10F00000000ull ^ std::uint64_t(config_.seed) * 0x9E3779B97F4A7C15ull;
    next_corrupted_ = std::uint64_t(config_.tuning.corrupted_from);
    offer_state_.forge_sure = config_.forge_sure;
    // (The human bot's own stream: its draws leave the spawner's alone.)
    human_.random = 0x4855AA5500000000ull ^ std::uint64_t(config_.seed) * 0xD1B54A32D192ED03ull;
    // The run's Guardians (4.4): drawn from their own stream, so the spawner's draws stay as they were.
    if (config_.tuning.guardians) guardian_plan_ = draw_guardians(config_.seed, config_.guardians);
}

void OverflowDirector::attach(Engine& engine, OverflowRules& rules) {
    (void)engine;
    rules_ = &rules;
    rules.director = this;
    // H2: every family of the four phases on the arena (SURVIVAL_O1A.md S6:
    // 166 RAMs, 250 MB, a 0.75 s load), the phase music and the quests'
    // jingles.
    rules.donors.families.clear();
    for (const auto& row : family_table()) rules.donors.families.emplace_back(row.family);
    rules.donors.streams.clear();
    for (const auto& music : phase_music()) rules.donors.streams.push_back({music.stage, {music.name}});
    rules.donors.streams.push_back({"dngq1b1e", {"QS_START", "QS_DONOT"}});
    // The Guardians of this run from their stages (never their ded_*.lua),
    // BT_SBOSS from Blossomon's stage, the quest timer's sounds (4.4, 11;
    // BT_BOSS2 is the Violet Ring's own).
    rules.donors.guardians.clear();
    if (config_.tuning.guardians) {
        for (const auto& family : guardian_plan_)
            if (const auto* row = guardian_row(family)) rules.donors.guardians.push_back({row->family, row->donor});
        rules.donors.streams.push_back({"dng101b2", {"BT_SBOSS"}});
        rules.donors.streams.push_back({"dngq3b1e", {"QS_HURRY", "z3COUNT1", "z3COUNT2", "z3COUNT3"}});
    }
    // The form's element: +15 % for its techniques (5.1): Agumon fire, Veemon thunder.
    rules.damage.element_factor = {1, 1, 1, 1};
    if (config_.form == "p_agumon") rules.damage.element_factor[0] = 1.15f;
    if (config_.form == "p_vmon") rules.damage.element_factor[2] = 1.15f;
    if (config_.form == "p_girumon") rules.damage.element_factor[3] = 1.15f; // (Dark)
    if (config_.form == "p_dorumon") rules.damage.element_factor[1] = 1.15f; // (Ice)
    // The run's skill caps start at the first Train step (6.2); the class
    // cap follows the Forge at the field start.
    rules.skills.cap.fill(train_cap(0));
    rules.damage.tag_factor[kTagArt] = art_factor();
    for (const int family : {0, 5, 10, 15, 20, 25, 30, 35, 40})
        if (technique_damage(family) != 1.0f) rules.damage.technique_factor[family] = technique_damage(family);
    // A normal enemy's stun, sleep or bind is a hitch, not a stop (4.8; 14.2
    // allows 5 % of the frames without control).
    rules.player_hit.status_cap = config_.tuning.status_cap;
    rules.player_hit.muteki = config_.tuning.muteki;
    rules.levels.level_heal = config_.tuning.level_heal;
    rules.levels.exp_factor = config_.tuning.exp_factor;
    rules.levels.curve_base = config_.tuning.curve[0];
    rules.levels.curve_linear = config_.tuning.curve[1];
    rules.levels.curve_square = config_.tuning.curve[2];
    // The Guardians' own damage factors (round O1b, guardians.cpp) and their clamp.
    rules.damage.guardian_clamp = config_.tuning.guardian_clamp;
    for (const auto& row : guardian_table())
        if (row.damage != 1.0f) rules.damage.family_factor[row.family] = row.damage;
    // The families' own damage factors (14.3).
    for (const auto& row : family_table())
        if (family_damage(row) != 1.0f) rules.damage.family_factor[row.family] = family_damage(row);
}

Vec3f OverflowDirector::player_position(Engine& engine) const {
    auto* models = engine.get<ModelCtrlManager>();
    const auto* self = models ? models->player(0) : nullptr;
    return self ? ram_position(*self) : Vec3f{};
}

double OverflowDirector::run_seconds() const { return double(clock_) * 1001.0 / 60000.0; }

int OverflowDirector::phase() const { return std::clamp(int(clock_ / std::uint64_t(config_.tuning.phase_steps)) + 1, 1, 4); }

int OverflowDirector::level(const Engine& engine) const {
    const auto* sys = const_cast<Engine&>(engine).get<GameSystem>();
    return sys && sys->players[0].stats.ready ? sys->players[0].stats.level : 1;
}

void OverflowDirector::notice(std::string text) {
    notice_ = std::move(text);
    notice_step_ = steps_;
    event_log_.push_back({clock_, "notice " + notice_});
}

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------
void OverflowDirector::field_started(Engine& engine) {
    (void)engine;
    // (The run's own start comes in the director's next step, once the
    // player and the stage are up: start_running.)
    event_log_.push_back({clock_, "field " + engine.stage_name()});
}

void OverflowDirector::start_running(Engine& engine) {
    started_ = true;
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    build_walkable(engine);
    load_art_pattern(engine);
    if (sys && db && rules_) {
        // The class cap: the opened family's epsilon SKILLPOINT (Forge 1).
        rules_->skills.cap[std::size_t(art_skill_)] = db->number("weapon", ladder_base_ + 4, "SKILLPOINT").value_or(660);
        auto& player = sys->players[0];
        for (int c = 0; c < 9; ++c)
            if (c < 6 && c != art_skill_) rules_->skills.cap[std::size_t(c)] = player.stats.skill[std::size_t(c)];
        // Family 1 alpha of the class in the socket (5.3: a Rookie starts there).
        // The ladder's five weapon packages now, inside the start: a Forge
        // later changes the model in hand without reading a package (T8).
        if (auto* weapons = engine.get<WeaponManager>())
            for (int f = 0; f < 5; ++f) {
                const std::string model(db->text("weapon", ladder_base_ + f * 5, "MODEL"));
                if (!model.empty() && !weapons->labels(model)) engine.load_common_now("_common/" + model + ".igb");
            }
        equip_ladder(engine, true);
        // The Plug Lab's points of the form (8.2, lane F's meta file).
        for (std::size_t i = 0; i < config_.plug_lab.size(); ++i) player.upcnt[i] += std::max(0, config_.plug_lab[i]);
        sys->recalc_player(engine, 0);
        sys->store_species(0);
        // The form's element (5.1): its PWR Medal (round O1b, gear.cpp).
        element_ = config_.form == "p_vmon" ? 2 : config_.form == "p_girumon" ? 3 : config_.form == "p_dorumon" ? 1 : 0;
        apply_loadout(engine);
    }
    // QS_START, then the phase's music (11).
    detail::play_sound(engine, "QS_START");
    music_phase_ = 0;
    music_at_ = steps_ + 240;
    next_spawn_ = 0;
    trail_.clear();
    known_level_ = level(engine);
    event_log_.push_back({clock_, "start " + config_.form + " Lv " + std::to_string(level(engine))});
}

// ---------------------------------------------------------------------------
// The step (H9)
// ---------------------------------------------------------------------------
void OverflowDirector::step(Engine& engine) {
    ++steps_;
    if (!started_) {
        auto* models = engine.get<ModelCtrlManager>();
        const auto* self = models ? models->player(0) : nullptr;
        if (!engine.loading() && engine.stage_active() && self && self->generated && self->node) start_running(engine);
        return;
    }
    apply_commands(engine);
    if (weapon_dirty_) {
        // The model in hand follows the ladder (S5).
        weapon_dirty_ = false;
        auto* sys = engine.get<GameSystem>();
        auto* weapons = engine.get<WeaponManager>();
        if (sys && weapons) {
            const std::string model = sys->players[0].weapons[0];
            if (!model.empty() && model != weapon_model_) {
                if (!weapons->labels(model)) engine.load_common_now("_common/" + model + ".igb");
                port_send(engine, "PLAYERARMS", model, 0, "GENERATE");
                weapon_model_ = model;
            }
        }
    }
    // The skill-up panels' spacing (14.2: at most one every 30 frames).
    if (rules_ && rules_->tally.panels != last_panel_count_) {
        if (last_panel_frame_ >= 0) panels_min_gap_ = std::min<int>(panels_min_gap_, int(rules_->last_panel_frame - last_panel_frame_));
        last_panel_frame_ = rules_->last_panel_frame;
        last_panel_count_ = rules_->tally.panels;
    }
    if (outcome_ != Outcome::none) {
        step_ending(engine);
        return;
    }
    if (screen_ != Screen::none) {
        // A test policy takes the offers itself.
        if (screen_ == Screen::level_up && !config_.offers.empty() && !offers_.empty()) {
            std::size_t pick = 0;
            if (config_.offers == "random") {
                pick = std::size_t(next_random(random_) % offers_.size());
            } else if (config_.offers == "newcomer") {
                // (The human bot: the first line or, as often, any of them.)
                if (next_unit(human_.random) >= 0.5) pick = std::size_t(next_random(human_.random) % offers_.size());
            } else {
                for (std::size_t i = 1; i < offers_.size(); ++i)
                    if (offer_priority(offers_[i]) < offer_priority(offers_[pick])) pick = i;
            }
            // (forge_from: the measurement of a form without its early Forge
            // takes the next line instead; a window always has a Plug.)
            if (clock_ < std::uint64_t(std::max(0, config_.forge_from)) && offers_[pick].kind == OfferKind::forge)
                pick = (pick + 1) % offers_.size();
            command_log_.push_back({clock_, "policy take " + std::to_string(pick)});
            take_offer(engine, offers_[pick]);
        }
        return;
    }
    step_running(engine);
}

void OverflowDirector::step_running(Engine& engine) {
    ++clock_;
    const Vec3f me = player_position(engine);
    if (auto* models = engine.get<ModelCtrlManager>(); models && models->player(0) && models->player(0)->generated) {
        const auto* self = models->player(0);
        if (!control_seen_ && self->control_enabled) {
            control_seen_ = true;
            event_log_.push_back({clock_, "control"});
        }
        // (14.2 counts the run's own frames: a damage reaction, a control
        // status, no control; the defeat's knock-down belongs to the ending.)
        static constexpr std::string_view kReactions[] = {"SQ_SDMG", "SQ_MDMG", "SQ_LDMG", "SQ_DDMG", "SQ_DEAD",
                                                          "SQ_SLEP", "SQ_STUN", "SQ_GUARDR", "SQ_CMBK", "SQ_CMBK02"};
        if (!self->control_enabled || std::find(std::begin(kReactions), std::end(kReactions), self->seq_name) != std::end(kReactions))
            ++no_control_;
    }
    trail_.push_back(me);
    if (trail_.size() > 30) trail_.erase(trail_.begin());
    if (clock_ % 60 == 1) update_factors(engine);
    // The phase (4.2), the Guardians and the finale (4.4), the music (11).
    const int now_phase = phase();
    if (steps_ >= music_at_ && music_phase_ != now_phase) {
        music_phase_ = now_phase;
        if (now_phase > 1) event_log_.push_back({clock_, "phase " + std::to_string(now_phase)});
    }
    step_guardians(engine);
    if (outcome_ != Outcome::none) return; // (LordKnightmon fell)
    step_music(engine);
    // Phase 4 goes on past its five minutes until the run's end (the forge
    // round of 05.11: the finale's five minutes, 4.4, without LordKnightmon
    // in O1a): the HUD says how long.
    if (const auto& t = config_.tuning; !t.guardians && t.end_steps > 4 * t.phase_steps && clock_ == std::uint64_t(4 * t.phase_steps)) {
        const int end = t.end_steps / 60;
        notice("Hold out until " + std::to_string(end / 60) + ":" + (end % 60 < 10 ? "0" : "") + std::to_string(end % 60));
    }
    // MP comes back by 2 % of MaxMP a second (4.8).
    if (auto* sys = engine.get<GameSystem>(); sys && sys->players[0].hp > 0) {
        auto& player = sys->players[0];
        mp_rest_ += double(player.max_mp) * double(config_.tuning.mp_regen) * 1001.0 / 60000.0;
        if (mp_rest_ >= 1.0 && player.mp < player.max_mp) {
            const int add = int(mp_rest_);
            mp_rest_ -= add;
            sys->change_mp(engine, 0, add);
        } else if (player.mp >= player.max_mp) {
            mp_rest_ = 0;
        }
    }
    drop_items(engine);
    if (clock_ % 30 == 0) power_up_chips(engine);
    track_enemies(engine);
    {
        // The crowd round the player (the run log's minute lines): the living
        // enemies within 15 and 30 units, and whether a 90-degree way out is
        // left between the ones within 20 (eight sectors; one within 10 also
        // closes its neighbours: its body and its swing).
        int near15 = 0, near30 = 0;
        std::array<bool, 8> closed{};
        if (auto* models = engine.get<ModelCtrlManager>())
            for (const auto& ram : models->rams()) {
                if (!living_enemy(*ram)) continue;
                const Vec3f q = ram_position(*ram);
                const float d = flat_distance(q, me);
                near15 += d <= 15.0f ? 1 : 0;
                near30 += d <= 30.0f ? 1 : 0;
                if (d > 20.0f) continue;
                const float angle = std::atan2(q.x - me.x, q.z - me.z) + 3.14159265f;
                const int sector = std::clamp(int(angle / (3.14159265f / 4.0f)), 0, 7);
                closed[std::size_t(sector)] = true;
                if (d <= 10.0f) closed[std::size_t((sector + 1) % 8)] = closed[std::size_t((sector + 7) % 8)] = true;
            }
        bool gap = false;
        for (int k = 0; k < 8; ++k) gap = gap || (!closed[std::size_t(k)] && !closed[std::size_t((k + 1) % 8)]);
        minute_near15_ += near15;
        minute_near30_ += near30;
        minute_trapped_ += gap ? 0 : 1;
        ++minute_steps_;
    }
    // (The way out the spawn points keep, for the spawner and the recycling.)
    update_gap(engine);
    if (config_.tuning.spawning) spawner(engine);
    if (config_.tuning.abilities) {
        cast_art(engine);
        cast_techniques(engine);
    }
    if (clock_ % 3600 == 0) log_minute(engine);
    if (clock_ >= std::uint64_t(config_.tuning.end_steps)) {
        // The quest timer's 0:00 with LordKnightmon alive: TIME UP (4.4);
        // without Guardians O1a's clear.
        begin_end(engine, config_.tuning.guardians ? Outcome::time_up : Outcome::cleared);
        return;
    }
    // (Two steps after the level-up: its HUD values, posted a frame ahead,
    // arrive before the freeze holds the messenger.) At most one window every
    // level_window_gap run steps: the levels that come sooner wait and open
    // one after another in the next one (the first minute brought five
    // freezes; the review of O1a).
    if (pending_levels_ > 0 && !level_up_steps_.empty() && clock_ >= level_up_steps_.back() + 2 &&
        (window_steps_.empty() || clock_ >= window_steps_.back() + std::uint64_t(config_.tuning.level_window_gap)))
        open_level_up(engine);
}

// The damage taken at a run time (4.3): the tier, the time's growth (4.3's
// + 4 % a minute became e^(damage_growth x minutes): a run's levels raise
// DEF and MaxHP so fast that a linear factor let every phase's hits shrink
// to a third by its end, and each new phase came as a step; the review of
// O1a), and the warm-up rising from x damage_warm_up (0.4) at 0:00 to x 1 at
// 5:00 (no step at the phase change).
float OverflowDirector::damage_factor(double minutes) const {
    static constexpr float kTierDamage[3] = {1.0f, 1.2f, 1.4f};
    const auto& t = config_.tuning;
    const double warm_minutes = std::max(0.01, double(t.damage_warm_minutes));
    const double warm = std::min(1.0, double(t.damage_warm_up) + (1.0 - double(t.damage_warm_up)) * minutes / warm_minutes);
    // (From damage_late_minutes on the growth is damage_growth_late a minute.)
    const double early = std::min(minutes, double(t.damage_late_minutes)), late = std::max(0.0, minutes - double(t.damage_late_minutes));
    const double growth = (1.0 + double(t.damage_per_minute) * minutes) *
                          std::exp(double(t.damage_growth) * early + double(t.damage_growth_late) * late);
    return float(double(kTierDamage[std::clamp(config_.tier, 0, 2)]) * growth * warm);
}

// An enemy's HP at a run time (4.3): HPMAX x (1 + hp_per_minute x minutes)
// x e^(hp_growth x minutes), x warm_up in phase 1, rising to x 1 over the
// minute before and after its end (no step at 5:00).
double OverflowDirector::hp_factor(double minutes) const {
    const auto& t = config_.tuning;
    const double end = double(t.phase_steps) / 3600.0;
    const double warm = std::clamp(double(t.warm_up) + (1.0 - double(t.warm_up)) * (minutes - (end - 1.0)) / 2.0, double(t.warm_up), 1.0);
    const double early = std::min(minutes, double(t.hp_late_minutes)), late = std::max(0.0, minutes - double(t.hp_late_minutes));
    return (1.0 + double(t.hp_per_minute) * minutes) * std::exp(double(t.hp_growth) * early + double(t.hp_growth_late) * late) * warm;
}

// The time factor of the damage taken, every second.
void OverflowDirector::update_factors(Engine& engine) {
    (void)engine;
    if (!rules_ || !config_.tuning.time_scaling) return;
    rules_->damage.taken_factor = damage_factor(run_seconds() / 60.0);
    rules_->damage.near_factor = knots(config_.tuning.contact_at, run_seconds() / 60.0);
    rules_->damage.far_factor = knots(config_.tuning.shot_at, run_seconds() / 60.0);
    // A new phase's families also come in softly: their damage rises from
    // phase_intro of their own factor to all of it over phase_intro_steps,
    // as their spawn weight rises (no wall at a phase change).
    const auto& t = config_.tuning;
    const int now = phase();
    const std::uint64_t into = clock_ - std::uint64_t(now - 1) * std::uint64_t(t.phase_steps);
    const int intro_steps = now == 4 && t.last_intro_steps > 0 ? t.last_intro_steps : t.phase_intro_steps;
    const double intro = now == 1 ? 1.0
                                  : std::min(1.0, double(t.phase_intro) + (1.0 - double(t.phase_intro)) * double(into) /
                                                                                 double(std::max(1, intro_steps)));
    for (const auto& row : family_table())
        if (row.phase == now && now > 1) rules_->damage.family_factor[row.family] = float(double(family_damage(row)) * intro);
}

// A family's own damage factor (tables.inc, or a balance pass's override).
float OverflowDirector::family_damage(const FamilyRow& row) const {
    if (const auto found = config_.family_damage.find(row.family); found != config_.family_damage.end()) return found->second;
    return row.damage;
}

bool OverflowDirector::walkable(float x, float z) const {
    const int ix = int(std::lround((x - grid_x0_) / grid_step_)), iz = int(std::lround((z - grid_z0_) / grid_step_));
    return ix >= 0 && iz >= 0 && ix < grid_n_ && iz < grid_n_ && !grid_.empty() && grid_[std::size_t(iz * grid_n_ + ix)].walkable;
}

// ---------------------------------------------------------------------------
// Commands and screens
// ---------------------------------------------------------------------------
void OverflowDirector::command_move(int delta) { commands_.push_back({'m', delta}); }
void OverflowDirector::command_accept() { commands_.push_back({'a', 0}); }
void OverflowDirector::command_back() { commands_.push_back({'b', 0}); }
void OverflowDirector::command_pause() { commands_.push_back({'p', 0}); }
void OverflowDirector::command_retire() { commands_.push_back({'r', 0}); }

void OverflowDirector::apply_commands(Engine& engine) {
    auto lines = [&]() -> int {
        switch (screen_) {
        case Screen::level_up: return int(offers_.size());
        case Screen::pause: return 3;
        case Screen::retire_ask: return 2;
        default: return 1;
        }
    };
    std::vector<Command> queue;
    queue.swap(commands_);
    for (const auto& command : queue) {
        command_log_.push_back({clock_, std::string(1, command.kind) + (command.kind == 'm' ? " " + std::to_string(command.value) : "")});
        switch (command.kind) {
        case 'm': {
            const int n = std::max(1, lines());
            const int before = cursor_;
            cursor_ = ((cursor_ + command.value) % n + n) % n;
            if (cursor_ != before) detail::play_sound(engine, "CURSOR_MOVE");
            break;
        }
        case 'p':
            if (outcome_ != Outcome::none) break;
            if (screen_ == Screen::none) {
                screen_ = Screen::pause;
                cursor_ = 0;
                freeze(engine, true);
                detail::play_sound(engine, "WINDOW_OPEN");
            } else if (screen_ == Screen::pause || screen_ == Screen::build || screen_ == Screen::retire_ask) {
                screen_ = Screen::none;
                freeze(engine, false);
                detail::play_sound(engine, "WINDOW_CLOSE");
            }
            break;
        case 'b':
            if (screen_ == Screen::pause) {
                screen_ = Screen::none;
                freeze(engine, false);
                detail::play_sound(engine, "CURSOR_CANCEL");
            } else if (screen_ == Screen::build || screen_ == Screen::retire_ask) {
                cursor_ = screen_ == Screen::build ? 1 : 2;
                screen_ = Screen::pause;
                detail::play_sound(engine, "CURSOR_CANCEL");
            }
            break;
        case 'a':
            switch (screen_) {
            case Screen::level_up:
                if (cursor_ >= 0 && cursor_ < int(offers_.size())) take_offer(engine, offers_[std::size_t(cursor_)]);
                break;
            case Screen::pause:
                detail::play_sound(engine, "CURSOR_DECIDE");
                if (cursor_ == 0) {
                    screen_ = Screen::none;
                    freeze(engine, false);
                } else if (cursor_ == 1) {
                    screen_ = Screen::build;
                } else {
                    screen_ = Screen::retire_ask;
                    cursor_ = 1; // "No" first
                }
                break;
            case Screen::build:
                screen_ = Screen::pause;
                cursor_ = 1;
                break;
            case Screen::retire_ask:
                detail::play_sound(engine, "CURSOR_DECIDE");
                if (cursor_ == 0) {
                    screen_ = Screen::none;
                    begin_end(engine, Outcome::retired);
                } else {
                    screen_ = Screen::pause;
                    cursor_ = 2;
                }
                break;
            case Screen::results:
                detail::play_sound(engine, "CURSOR_DECIDE");
                over_ = true;
                break;
            case Screen::none: break;
            }
            break;
        case 'r':
            if (outcome_ == Outcome::none) {
                screen_ = Screen::none;
                begin_end(engine, Outcome::retired);
            } else if (screen_ != Screen::results) {
                // (Retire during the ending: the results at once.)
                ending_phase_ = 99;
            }
            break;
        default: break;
        }
    }
}

// The original's freeze (subwnd.cpp's SUBWND GENERATE, 13.5): models and
// generators stop, posted messages wait, the sounds freeze; the run clock
// stops with it.
void OverflowDirector::freeze(Engine& engine, bool on) {
    if (on == frozen_) return;
    frozen_ = on;
    if (on) {
        port_send(engine, "MODELCTRL", std::nullopt, -1, "ONFREEZE");
        port_send(engine, "GENERATER", std::nullopt, -1, "ONFREEZE");
        engine.messenger().pause(engine, true);
        port_send(engine, "SOUND", std::nullopt, -1, "ONFREEZE");
    } else {
        for (const char* category : {"SOUND", "GENERATER", "MODELCTRL"}) port_send(engine, category, std::nullopt, -1, "OFFREEZE");
        engine.messenger().pause(engine, false);
    }
}

void OverflowDirector::level_up(Engine& engine, int slot, int level) {
    (void)engine;
    if (slot != 0) return;
    // (apply_level_ups reports once for several levels at once: one window each.)
    const int gained = std::max(1, level - known_level_);
    known_level_ = std::max(known_level_, level);
    pending_levels_ += gained;
    for (int i = 0; i < gained; ++i) level_up_steps_.push_back(clock_);
    event_log_.push_back({clock_, "level " + std::to_string(level)});
}

void OverflowDirector::open_level_up(Engine& engine) {
    window_steps_.push_back(clock_);
    offer_state_.level = level(engine);
    gear_offers(engine);
    offers_ = draw_offers(offer_state_, random_);
    ++offer_state_.drawn;
    screen_ = Screen::level_up;
    cursor_ = 0;
    // A test policy takes it in this step's director pass; the player gets the frozen window.
    if (config_.offers.empty()) {
        freeze(engine, true);
        detail::play_sound(engine, "WINDOW_OPEN");
    }
}

void OverflowDirector::take_offer(Engine& engine, const Offer& offer) {
    auto* sys = engine.get<GameSystem>();
    auto* db = engine.get<DatabaseManager>();
    if (!sys || !db || !rules_) return;
    auto& player = sys->players[0];
    const std::string text = offer_text(engine, offer);
    switch (offer.kind) {
    case OfferKind::forge: {
        offer_state_.forge = std::clamp(offer.value, 1, 5);
        rules_->skills.cap[std::size_t(art_skill_)] =
            db->number("weapon", ladder_base_ + (offer_state_.forge - 1) * 5 + 4, "SKILLPOINT").value_or(rules_->skills.cap[std::size_t(art_skill_)]);
        if (offer_state_.forge >= 3) rules_->damage.tag_factor[kTagArt] = 1.2f * art_factor();
        equip_ladder(engine, false);
        sys->recalc_player(engine, 0);
        sys->store_species(0);
        break;
    }
    case OfferKind::train: {
        const int group = std::clamp(offer.value - kBlastSkill, 0, 2);
        auto& steps = offer_state_.train[std::size_t(group)];
        steps = std::min(steps + 1, 4);
        const int cap = train_cap(steps);
        rules_->skills.cap[std::size_t(offer.value)] = cap;
        const auto before = player.wazalist;
        auto& skill = player.stats.skill[std::size_t(offer.value)];
        skill = std::min(cap, skill + 100);
        sys->recalc_player(engine, 0);
        sys->store_species(0);
        if (player.wazalist != before) post_effect(engine, "MODELCTRL", player.digimon, 0, "i_getitempnl01", "SQ_TECUP");
        break;
    }
    case OfferKind::core:
    case OfferKind::board:
    case OfferKind::chip: take_gear(engine, offer); break;
    case OfferKind::plug: {
        const int index = std::clamp(offer.value, 0, 10);
        const int points = plug_points(player.stats.level);
        player.upcnt[std::size_t(index)] += points;
        plugs_taken_.push_back({index, std::uint64_t(points)});
        // (A MaxHP plug raises the bar, not the HP: the run keeps HP apart.)
        sys->recalc_player(engine, 0);
        sys->store_species(0);
        break;
    }
    }
    event_log_.push_back({clock_, "take " + text});
    if (config_.offers.empty()) {
        detail::play_sound(engine, "CURSOR_DECIDE");
        detail::play_sound(engine, "plLVUP01");
    }
    pending_levels_ = std::max(0, pending_levels_ - 1);
    if (pending_levels_ > 0) {
        offer_state_.level = level(engine);
        gear_offers(engine);
        offers_ = draw_offers(offer_state_, random_);
        ++offer_state_.drawn;
        cursor_ = 0;
        return;
    }
    offers_.clear();
    screen_ = Screen::none;
    freeze(engine, false);
}

std::string OverflowDirector::offer_text(const Engine& engine, const Offer& offer) const {
    (void)engine;
    switch (offer.kind) {
    case OfferKind::forge:
        return "Forge: " + (offer.value >= 1 && offer.value <= int(family_names_.size()) ? family_names_[std::size_t(offer.value - 1)] : std::string("?"));
    case OfferKind::train: return std::string("Train ") + kGroupNames[std::clamp(offer.value - kBlastSkill, 0, 2)];
    case OfferKind::plug: return std::string("Plug: ") + plug_name(offer.value);
    case OfferKind::core:
    case OfferKind::board:
    case OfferKind::chip: return gear_text(engine, offer);
    }
    return {};
}

// "Fire", "Fire and Cutter", "Fire, Cutter and Bomb".
namespace {
std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return out;
}
} // namespace

std::string OverflowDirector::offer_detail(const Engine& engine, const Offer& offer) const {
    switch (offer.kind) {
    case OfferKind::forge: {
        const std::string step = offer.value >= 2 && offer.value <= 5 ? forge_steps_[std::size_t(offer.value - 2)] : std::string();
        return "Art: " + step;
    }
    case OfferKind::train: {
        const int group = std::clamp(offer.value - kBlastSkill, 0, 2);
        const int steps = offer_state_.train[std::size_t(group)];
        const int now = level(engine);
        int lock = 0;
        bool learned = false;
        for (const auto& t : techniques_)
            if (t.group == group) {
                if (t.lock <= now) learned = true;
                else if (lock == 0 || t.lock < lock) lock = t.lock;
            }
        if (!learned && lock > 0)
            for (const auto& t : techniques_)
                if (t.group == group && t.lock == lock) return t.name + " at Lv " + std::to_string(lock) + "; " + kGroupNames[group] + " +100";
        return std::string(kGroupNames[group]) + " +100, cap " + std::to_string(train_cap(steps)) + " > " + std::to_string(train_cap(steps + 1));
    }
    case OfferKind::plug: return std::string(plug_name(offer.value)) + " +" + std::to_string(plug_points(level(engine)));
    case OfferKind::core:
    case OfferKind::board:
    case OfferKind::chip: return gear_detail(engine, offer);
    }
    return {};
}

// One line about the offer under the cursor (9.4: "A Take" and a line of text).
std::string OverflowDirector::offer_about(const Engine& engine, const Offer& offer) const {
    switch (offer.kind) {
    case OfferKind::forge: {
        // One short line about what a Forge does (the forge round of 05.11:
        // a newcomer took it as often as a Plug); the detail column says
        // what this one adds to the Art.
        const std::string family =
            offer.value >= 1 && offer.value <= int(family_names_.size()) ? family_names_[std::size_t(offer.value - 1)] : std::string("?");
        return "A Forge makes " + art_name_ + " stronger at once and lets its weapon grow into " + family + ".";
    }
    case OfferKind::train: {
        const int group = std::clamp(offer.value - kBlastSkill, 0, 2);
        const int now = level(engine);
        std::vector<std::string> names, locked;
        int lock = 0;
        for (const auto& t : techniques_) {
            if (t.group != group) continue;
            if (t.lock <= now) names.push_back(t.name);
            else {
                locked.push_back(t.name);
                if (lock == 0 || t.lock < lock) lock = t.lock;
            }
        }
        const int steps = offer_state_.train[std::size_t(group)];
        if (names.empty() && lock > 0)
            return join_names(locked) + " is learned at Lv " + std::to_string(lock) + "; until then this only stores " + kGroupNames[group] + " skill.";
        return join_names(names) + " rank up with " + kGroupNames[group] + " skill: +100 now, and it may grow to " +
               std::to_string(train_cap(steps + 1)) + ".";
    }
    case OfferKind::plug: {
        static constexpr const char* kElements[4] = {"fire", "ice", "thunder", "dark"};
        const std::string plug = std::string(plug_name(offer.value)) + " +" + std::to_string(plug_points(level(engine))) + " for this run";
        if (offer.value >= 7 && offer.value <= 10) return plug + ": only " + kElements[offer.value - 7] + " attacks hit softer.";
        return plug + ".";
    }
    }
    if (offer.kind == OfferKind::core || offer.kind == OfferKind::board || offer.kind == OfferKind::chip)
        return "Gear for the equipment window: " + gear_detail(engine, offer) + ".";
    return {};
}

// ---------------------------------------------------------------------------
// Notifications (H5)
// ---------------------------------------------------------------------------
void OverflowDirector::enemy_defeated(Engine& engine, int killer, int record, std::string_view family, int index) {
    (void)killer;
    (void)record;
    if (guardian_ && guardian_->family == family && guardian_->ram == index) {
        ++defeats_;
        ++defeats_by_family_[std::string(family) + "!"];
        guardian_down(engine, true);
        return;
    }
    const auto* row = family_row(family);
    if (!row) return;
    ++defeats_;
    ++defeats_by_family_[std::string(family)];
    for (auto& e : enemies_) {
        if (e.ram != index || e.family != family) continue;
        ++(e.last_art ? minute_art_kills_ : minute_tech_kills_);
        if (e.last_source < 0)
            if (const int row = (-e.last_source) % 1000; row >= 0 && row < 20) ++tech_defeats_[std::size_t(row / 5)];
        if (phase() == 1 && !e.corrupted && !e.other_damage && e.art_hits > 0) art_hits_per_kill_.push_back(e.art_hits);
        if (e.corrupted) {
            ++corrupted_defeats_;
            // 4.4's drops (O1a: the Plug and the HP Disk alpha; the X Data
            // has no chip to power up before O1b).
            Vec3f at{};
            if (auto* models = engine.get<ModelCtrlManager>())
                if (const auto* ram = models->find(family, index)) at = ram_position(*ram);
            static constexpr int kPlugRecords[7] = {4, 6, 8, 10, 12, 14, 16}; // Strgth, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed (UP 1)
            drops_.push_back({"g_000xdata2", kPlugRecords[next_random(random_) % 7], 1, {at.x + 3.0f, at.y, at.z}});
            drops_.push_back({"g_000xdata1", 2, 1, {at.x, at.y, at.z + 3.0f}}); // ¶ Data UP 20 (4.4; round O1b: the chips use it)
            if (int(next_random(random_) % 100) < config_.tuning.disk_percent) drops_.push_back({"g_000item", 0, 0, {at.x - 3.0f, at.y, at.z}});
        }
        break;
    }
}

void OverflowDirector::damage_dealt(Engine& engine, std::string_view family, int index, std::int32_t source, int damage) {
    (void)engine;
    (void)damage;
    for (auto& e : enemies_) {
        if (e.ram != index || e.family != family) continue;
        if (source == kTagArt) ++e.art_hits;
        else e.other_damage = true;
        e.last_art = source == kTagArt;
        e.last_source = source;
        break;
    }
}

void OverflowDirector::item_taken(Engine& engine, int slot, int table, int record) {
    if (slot != 0) return;
    auto* sys = engine.get<GameSystem>();
    if (!sys) return;
    auto& player = sys->players[0];
    if (table == 1 && record >= 4 && record <= 25) {
        // A Plug pickup counts as the Plug offer (4.7): its UP is in, the rest comes here.
        static const int index[22] = {2, 2, 3, 3, 0, 0, 1, 1, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10};
        const int stat = index[record - 4];
        const int up = record % 2 == 0 ? 1 : 2;
        const int points = std::max(0, plug_points(player.stats.level) - up);
        player.upcnt[std::size_t(stat)] += points;
        sys->recalc_player(engine, 0);
        sys->store_species(0);
        ++plugs_picked_;
        notice(std::string(plug_name(stat)) + " Plug +" + std::to_string(points + up));
    } else if (table == 1 && record >= 1 && record <= 3) {
        // ¶ Data (4.7, 7.5): the run's own count, spent on the chips' power-ups.
        static constexpr int kUp[4] = {0, 5, 20, 100};
        x_data_ += kUp[record];
        x_data_total_ += kUp[record];
        notice("¶ Data +" + std::to_string(kUp[record]));
    } else if (table == 0) {
        ++disks_taken_;
        notice("HP Disk");
    }
}

void OverflowDirector::player_hit(Engine& engine, const HitTaken& hit) {
    if (hit.missed) {
        ++misses_;
        return;
    }
    if (hit.damage <= 0) return;
    ++hits_taken_;
    if (hit.clamped) ++clamps_;
    // A hit costs its damage and the whole of the poison it left (the run's
    // poison rule): 14.2's highest hit and the balance test see both.
    const int poison = hit.poison_tick * hit.poison_ticks;
    const double share = hit.max_hp > 0 ? double(hit.damage + poison) / double(hit.max_hp) : 0.0;
    if (poison > 0) {
        ++poisonings_;
        highest_poison_ = std::max(highest_poison_, double(poison) / double(std::max(1, hit.max_hp)));
    }
    taken_share_ += share;
    {
        // By source: an attacker within 15 units hit with its body or a
        // swing, a farther one with a shot; the poison apart.
        float distance = 0.0f;
        if (auto* models = engine.get<ModelCtrlManager>())
            if (const auto* shooter = models->find(hit.shooter, hit.shooter_index); shooter && shooter->node)
                distance = flat_distance(ram_position(*shooter), player_position(engine));
        const double part = hit.max_hp > 0 ? double(hit.damage) / double(hit.max_hp) : 0.0;
        (distance > 15.0f ? minute_shot_ : minute_contact_) += part;
        minute_poison_ += share - part;
    }
    // DW4_TRACE_OVERFLOW_HITS: every hit taken (the balance passes).
    static const bool trace = std::getenv("DW4_TRACE_OVERFLOW_HITS") != nullptr;
    if (trace)
        engine.log("overflow hit " + hit.shooter + "#" + std::to_string(hit.shooter_index) + " kind " + std::to_string(hit.kind) + " raw " +
                   std::to_string(hit.raw) + " damage " + std::to_string(hit.damage) + " of " + std::to_string(hit.max_hp) + " factor " +
                   std::to_string(rules_ ? rules_->damage.taken_factor : 0.0f) +
                   (poison > 0 ? " poison " + std::to_string(hit.poison_tick) + "x" + std::to_string(hit.poison_ticks) : std::string()));
    if (hit.enemy != EnemyClass::guardian) highest_hit_ = std::max(highest_hit_, share);
    auto& by = hits_by_family_[hit.shooter + (hit.enemy == EnemyClass::corrupted ? "*" : "")];
    taken_by_family_[{phase(), hit.shooter + (hit.enemy == EnemyClass::corrupted ? "*" : "")}] += share;
    ++by.first;
    by.second = std::max(by.second, share);
}

// ---------------------------------------------------------------------------
// The end
// ---------------------------------------------------------------------------
void OverflowDirector::run_end(Engine& engine, const RunEnd& end) {
    // H3: a defeat (or the original's game over) ends the run; a stage the
    // run did not ask for (O1a has no exit) counts as Retire.
    static const char* const reasons[] = {"stage_exit", "game_over", "player_defeated"};
    if (outcome_ == Outcome::none) end_reason_ = reasons[int(end.reason)];
    begin_end(engine, end.reason == EndReason::stage_exit ? Outcome::retired : Outcome::defeated);
}

void OverflowDirector::begin_end(Engine& engine, Outcome outcome) {
    if (outcome_ != Outcome::none) return;
    outcome_ = outcome;
    ending_since_ = steps_;
    ending_phase_ = 0;
    static const char* names[] = {"none", "retired", "defeated", "cleared", "time_up"};
    if (end_reason_ == "-") end_reason_ = names[int(outcome)];
    event_log_.push_back({clock_, std::string("end ") + names[int(outcome)] + " (" + end_reason_ + ")"});
    log_minute(engine);
    if (outcome == Outcome::retired) {
        ending_phase_ = 99;
    } else if (outcome == Outcome::cleared || outcome == Outcome::time_up) {
        // The simulation is complete (or its time is up, 4.4): the enemies
        // leave; the arena's victory music, or the timer's end and QS_DONOT.
        if (guardian_) guardian_down(engine, false);
        if (auto* models = engine.get<ModelCtrlManager>())
            for (const auto& ram : models->rams())
                if (living_enemy(*ram)) port_send(engine, "MODELCTRL", ram->info_name, ram->index, "SQ_ERAS");
        if (outcome == Outcome::cleared) {
            detail::play_sound(engine, "BT_WIN02");
        } else {
            detail::play_sound(engine, "z3COUNT3");
            detail::play_sound(engine, "QS_DONOT");
            notice("TIME UP");
        }
    }
}

void OverflowDirector::step_ending(Engine& engine) {
    const std::uint64_t age = steps_ - ending_since_;
    if (screen_ == Screen::results) {
        if (config_.results_hold > 0 && steps_ - results_since_ >= std::uint64_t(config_.results_hold)) over_ = true;
        return;
    }
    if (outcome_ == Outcome::defeated && ending_phase_ == 0 && age >= 120) {
        // The original's Game Over picture (4.8).
        port_send(engine, "MODELCTRL", "i_fadewn01", 0, "SQ_GAMEOVER");
        detail::play_sound(engine, "QS_DONOT");
        ending_phase_ = 1;
    }
    const std::uint64_t wait = outcome_ == Outcome::defeated ? 330 : outcome_ == Outcome::cleared || outcome_ == Outcome::time_up ? 240 : 0;
    if (ending_phase_ == 99 || age >= wait) {
        screen_ = Screen::results;
        cursor_ = 0;
        results_since_ = steps_;
        freeze(engine, true);
    }
}

// ---------------------------------------------------------------------------
// Pads: move only (10) and the kite bot (14.1)
// ---------------------------------------------------------------------------
PadSample OverflowDirector::filter_pad(Engine& engine, const PadSample& live) {
    PadSample out;
    out.connected = live.connected;
    if (screen_ != Screen::none || outcome_ != Outcome::none || !started_) return out;
    out.left = live.left;
    std::uint32_t keep = pad_button::up | pad_button::down | pad_button::left | pad_button::right | pad_button::l;
    auto* models = engine.get<ModelCtrlManager>();
    auto* sys = engine.get<GameSystem>();
    bool label = false, panel = false;
    if (models && sys) {
        if (const auto* self = models->find(sys->players[0].digimon, 0)) panel = self->panel_open;
        for (const auto& ram : models->rams())
            if (ram->generated && ram->category == "ITEM" && ram->item_label == 0) label = true;
    }
    // A takes a labelled item (disks) or chooses in the item panel; with the
    // panel open, B closes it.
    if (label || panel) keep |= pad_button::a;
    if (panel) keep |= pad_button::b;
    out.buttons = live.buttons & keep;
    return out;
}

PadSample OverflowDirector::bot_pad(Engine& engine) {
    HostPadInput input;
    if (screen_ != Screen::none || outcome_ != Outcome::none || !started_) return host_pad_sample(input);
    auto* models = engine.get<ModelCtrlManager>();
    auto* sys = engine.get<GameSystem>();
    if (!models || !sys || !engine.get<CameraManager>()) return host_pad_sample(input);
    const auto* self = models->find(sys->players[0].digimon, 0);
    if (!self || !self->node) return host_pad_sample(input);
    const Vec3f me = ram_position(*self);
    // 14.1, as a player of the move-only mode plays it (10): it never stands.
    // With an enemy within bot_flee (16) units it walks away from the
    // enemies within 40, weighted 1/d^2 (of 16 ways the one whose point 25
    // units ahead has the least sum of 1/d^2 to them, so a crowd's gap wins
    // over its middle; 0.7 of the stick, full stick within half of bot_flee).
    // Else it walks at half the stick towards the nearest enemy within 60
    // (into the Art's reach), else on its last way. Back towards the centre
    // past 70 % of the arena's radius; a small cost for turning keeps it from
    // dithering between two ways. (Lane C's bot stood until an enemy came
    // within 20, so the balance was made for a player who waits.)
    struct Threat {
        Vec3f at;
    };
    std::vector<Threat> threats;
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
        if (d <= 40.0f) threats.push_back({q});
    }
    const float flee_at = config_.tuning.bot_flee;
    const bool flee = nearest < flee_at, approach = !flee && nearest < 60.0f;
    const float r = flat_distance(me, centre_);
    float magnitude = flee ? (nearest < 0.5f * flee_at ? 1.0f : 0.7f) : 0.5f;
    if (r > 0.7f * radius_) magnitude = std::max(magnitude, std::min(1.0f, (r - 0.7f * radius_) / (0.2f * radius_)));
    float dx = 0, dz = 0;
    {
        float best = 1e30f;
        for (int k = 0; k < 16; ++k) {
            const float angle = 6.2831853f * float(k) / 16.0f;
            const float sx = std::sin(angle), sz = std::cos(angle);
            const Vec3f p{me.x + 25.0f * sx, me.y, me.z + 25.0f * sz};
            float cost = 0;
            if (flee)
                for (const auto& t : threats) {
                    const float d = std::max(3.0f, flat_distance(p, t.at));
                    cost += 1.0f / (d * d);
                }
            else if (approach)
                cost += 0.0001f * flat_distance(p, nearest_at);
            const float pr = flat_distance(p, centre_);
            if (pr > 0.7f * radius_) cost += 0.02f * (pr - 0.7f * radius_) * (pr - 0.7f * radius_) / (radius_ * radius_) * 100.0f;
            cost += 0.0004f * (1.0f - (sx * bot_dx_ + sz * bot_dz_));
            if (cost < best) {
                best = cost;
                dx = sx;
                dz = sz;
            }
        }
        bot_dx_ = dx;
        bot_dz_ = dz;
    }
    if (magnitude > 0.05f && (dx != 0 || dz != 0)) input.stick = stick_for(engine, me, dx, dz, magnitude);
    bot_buttons(engine, input, 1, 3, 0);
    return host_pad_sample(input);
}

// World direction to the stick: up is away from the camera on the ground,
// right is the picture's right.
PadStick OverflowDirector::stick_for(Engine& engine, const Vec3f& me, float dx, float dz, float magnitude) const {
    PadStick stick{};
    auto* cameras = engine.get<CameraManager>();
    if (!cameras) return stick;
    FrameCamera camera;
    cameras->frame_camera(engine, camera);
    Mat4 inverse;
    invert(camera.view, inverse);
    const Vec3f eye{inverse(3, 0), inverse(3, 1), inverse(3, 2)};
    float fx = me.x - eye.x, fz = me.z - eye.z;
    const float fl = std::hypot(fx, fz);
    if (fl <= 1e-3f) return stick;
    fx /= fl;
    fz /= fl;
    // The right of the picture: the one of the two side directions that projects to +x.
    const Mat4 vp = camera.view * camera.projection;
    auto clip_x = [&](float x, float z) {
        const float cx = x * vp(0, 0) + me.y * vp(1, 0) + z * vp(2, 0) + vp(3, 0);
        const float w = x * vp(0, 3) + me.y * vp(1, 3) + z * vp(2, 3) + vp(3, 3);
        return w > 1e-4f ? cx / w : 0.0f;
    };
    float rx = fz, rz = -fx;
    if (clip_x(me.x + rx * 10.0f, me.z + rz * 10.0f) < clip_x(me.x - rx * 10.0f, me.z - rz * 10.0f)) {
        rx = -rx;
        rz = -rz;
    }
    const float up = dx * fx + dz * fz, right = dx * rx + dz * rz;
    stick = {right * magnitude, up * magnitude};
    return stick;
}

// A takes a labelled item; the HP disks wait in the folder for the panel:
// L, then A on its first line, once the HP is below num / den for more than
// `wait` steps.
void OverflowDirector::bot_buttons(Engine& engine, HostPadInput& input, int num, int den, int wait) {
    auto* models = engine.get<ModelCtrlManager>();
    auto* sys = engine.get<GameSystem>();
    if (!models || !sys) return;
    bool label = false;
    for (const auto& ram : models->rams())
        if (ram->generated && ram->category == "ITEM" && ram->item_label == 0) label = true;
    auto& player = sys->players[0];
    const int disks = player.disk_count(0);
    const bool low = player.hp > 0 && player.hp * den < player.max_hp * num;
    disk_wait_ = low ? disk_wait_ + 1 : 0;
    if (bot_l_ == 0 && disks > 0 && low && disk_wait_ > wait) bot_l_ = 1;
    if (bot_l_ > 0) {
        if (bot_l_ == 1) input.buttons |= pad_button::l;
        if (bot_l_ == 20) input.buttons |= pad_button::a;
        if (++bot_l_ > 60) bot_l_ = 0;
    } else if (label) {
        bot_a_ = (bot_a_ + 1) % 2;
        if (bot_a_ == 1) input.buttons |= pad_button::a;
    }
    if (disks < last_disks_) ++disks_used_;
    last_disks_ = disks;
}

void OverflowDirector::record_pad(const PadSample& pad) {
    // Run-length lines "count buttons x y" (13.5: the pads of every step).
    if (last_pad_count_ > 0 && pad.buttons == last_pad_.buttons && pad.left == last_pad_.left) {
        ++last_pad_count_;
        return;
    }
    if (last_pad_count_ > 0) {
        char line[96];
        std::snprintf(line, sizeof line, "%llu %x %.4f %.4f\n", (unsigned long long)last_pad_count_, last_pad_.buttons, last_pad_.left.x,
                      last_pad_.left.y);
        pad_log_ += line;
    }
    last_pad_ = pad;
    last_pad_count_ = 1;
}

// ---------------------------------------------------------------------------
// The run log (13.5) and the summary (T6, 14.2)
// ---------------------------------------------------------------------------
void OverflowDirector::log_minute(Engine& engine) {
    MinuteLog line;
    line.minute = int(clock_ / 3600);
    line.level = level(engine);
    line.defeats = defeats_;
    line.hits_taken = hits_taken_;
    line.clamps = clamps_;
    line.alive = alive_;
    line.highest_hit = highest_hit_;
    line.taken = taken_share_ - minute_taken_mark_;
    minute_taken_mark_ = taken_share_;
    line.contact = minute_contact_;
    line.shot = minute_shot_;
    line.poison = minute_poison_;
    if (minute_steps_ > 0) {
        line.near15 = minute_near15_ / double(minute_steps_);
        line.near30 = minute_near30_ / double(minute_steps_);
        line.trapped = double(minute_trapped_) / double(minute_steps_);
    }
    line.art_kills = minute_art_kills_;
    line.tech_kills = minute_tech_kills_;
    minute_art_kills_ = minute_tech_kills_ = 0;
    minute_contact_ = minute_shot_ = minute_poison_ = minute_near15_ = minute_near30_ = 0;
    minute_trapped_ = minute_steps_ = 0;
    if (rules_) {
        line.no_control = rules_->tally.no_control_steps;
        line.steps = rules_->tally.steps;
    }
    minutes_.push_back(line);
}

std::vector<std::string> OverflowDirector::build_lines(const Engine& engine) const {
    std::vector<std::string> out;
    auto* sys = const_cast<Engine&>(engine).get<GameSystem>();
    auto* db = const_cast<Engine&>(engine).get<DatabaseManager>();
    if (!sys || !db) return out;
    const auto& player = sys->players[0];
    const auto* weapon = player.device(player.power_socket.empty() ? -1 : player.power_socket[0]);
    const int skill = player.stats.skill[std::size_t(art_skill_)];
    out.push_back("Art: " + art_name_ + " with " + (weapon ? detail::plain_name(db->text("weapon", weapon->record, "NAME")) : std::string("-")));
    out.push_back(std::string("  ") + kClassNames[std::clamp(art_skill_, 0, 5)] + " skill " + std::to_string(skill) + " / cap " +
                  std::to_string(rules_ ? rules_->skills.cap[std::size_t(art_skill_)] : 0) + ", Forge " + std::to_string(offer_state_.forge) +
                  " of " + std::to_string(std::min(5, offer_state_.forge_families)));
    for (const auto& t : techniques_) {
        int row = -1;
        for (const int learned : player.wazalist)
            if (learned >= t.family && learned < t.family + 5) row = learned;
        const int group = std::clamp(db->number("pcTech", t.family, "SKILL").value_or(kBlastSkill), 0, 8);
        std::string line = "  " + t.name + ": ";
        if (row >= 0) line += kRankNames[std::clamp(row - t.family, 0, 4)];
        else line += "not learned";
        line += " (" + std::string(kGroupNames[std::clamp(group - kBlastSkill, 0, 2)]) + " " + std::to_string(player.stats.skill[std::size_t(group)]) +
                " / cap " + std::to_string(rules_ ? rules_->skills.cap[std::size_t(group)] : 0) + ")";
        out.push_back(line);
    }
    // The equipment window's other parts (7): the core, the board, the pockets.
    {
        auto name = [&](const char* table, int index) {
            const auto* device = player.device(index);
            if (!device) return std::string("-");
            std::string text = detail::plain_name(db->text(table, device->record, "NAME"));
            if (device->value > 0) text += " +" + std::to_string(device->value);
            return text;
        };
        out.push_back("Core: " + name("armor1", player.main_slot[0]) + "   Board: " + name("armor2", player.sub_slot[0]));
        out.push_back("Chips: " + name("abChip", player.power_pocket[0]) + " / " + name("abChip", player.main_pocket[0]) + "   ¶ Data " +
                      std::to_string(x_data_));
    }
    std::map<int, int> plugs;
    for (const auto& [stat, points] : plugs_taken_) plugs[stat] += int(points);
    if (!plugs.empty()) {
        std::string line = "Plugs:";
        for (const auto& [stat, points] : plugs) line += std::string(" ") + plug_name(stat) + " +" + std::to_string(points);
        out.push_back(line);
    }
    out.push_back("Time " + clock_text(run_seconds()) + "   Level " + std::to_string(player.stats.level) + "   Defeated " + std::to_string(defeats_));
    return out;
}

std::vector<std::string> OverflowDirector::result_lines(const Engine& engine) const {
    std::vector<std::string> out;
    auto* sys = const_cast<Engine&>(engine).get<GameSystem>();
    auto* db = const_cast<Engine&>(engine).get<DatabaseManager>();
    out.push_back("Time " + clock_text(run_seconds()));
    out.push_back("Run level " + std::to_string(level(engine)));
    out.push_back("Digimon " + std::string(config_.form == "p_vmon"      ? "Veemon"
                                           : config_.form == "p_agumon"  ? "Agumon"
                                           : config_.form == "p_girumon" ? "Guilmon"
                                           : config_.form == "p_dorumon" ? "Dorumon"
                                                                         : config_.form.c_str()));
    if (!guardians_defeated_.empty()) {
        std::string names;
        for (const auto& family : guardians_defeated_)
            if (const auto* row = guardian_row(family)) names += (names.empty() ? "" : ", ") + std::string(row->name);
        out.push_back("Guardians " + names);
    }
    out.push_back("Defeated " + std::to_string(defeats_));
    std::vector<std::pair<int, std::string>> families;
    for (const auto& [family, count] : defeats_by_family_) {
        const auto* row = family_row(family);
        families.push_back({count, row ? row->name : family});
    }
    std::stable_sort(families.begin(), families.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 0; i < families.size() && i < 5; ++i) out.push_back("   " + families[i].second + " " + std::to_string(families[i].first));
    if (rules_) {
        std::vector<std::pair<std::int64_t, std::string>> damage;
        for (const auto& [source, amount] : rules_->tally.dealt_by) damage.push_back({amount, technique_name(source)});
        std::stable_sort(damage.begin(), damage.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        out.push_back("Damage dealt");
        for (std::size_t i = 0; i < damage.size() && i < 6; ++i) out.push_back("   " + damage[i].second + " " + std::to_string(damage[i].first));
    }
    char hit[64];
    std::snprintf(hit, sizeof hit, "Highest hit taken %.0f %% of MaxHP", highest_hit_ * 100.0);
    out.push_back(hit);
    if (sys && db) {
        const auto& player = sys->players[0];
        if (const auto* weapon = player.device(player.power_socket.empty() ? -1 : player.power_socket[0]))
            out.push_back("Weapon " + detail::plain_name(db->text("weapon", weapon->record, "NAME")) + ", Forge " + std::to_string(offer_state_.forge));
    }
    return out;
}

std::string OverflowDirector::log_text() const {
    std::ostringstream out;
    out << "DW4OVERFLOWLOG 1\n";
    out << "form " << config_.form << "\nfield " << config_.field << "\nseed " << config_.seed << "\ntier " << config_.tier << "\nbot "
        << (config_.bot.empty() ? "-" : config_.bot) << "\noffers " << (config_.offers.empty() ? "player" : config_.offers) << "\n";
    out << "loadout none\nmeta none\n"; // (O1b: the loadout and the Plug Lab)
    for (const auto& [step, text] : command_log_) out << "command " << step << ' ' << text << '\n';
    for (const auto& [step, text] : event_log_) out << "event " << step << ' ' << text << '\n';
    for (const auto& m : minutes_)
        out << "minute " << m.minute << " level " << m.level << " defeats " << m.defeats << " hits " << m.hits_taken << " clamps " << m.clamps
            << " alive " << m.alive << " no_control " << m.no_control << '/' << m.steps << " highest " << m.highest_hit << " taken " << m.taken
            << " contact " << m.contact << " shot " << m.shot << " poison " << m.poison << " near15 " << m.near15 << " near30 " << m.near30
            << " trapped " << m.trapped << " art_kills " << m.art_kills << " tech_kills " << m.tech_kills << '\n';
    out << "pads\n" << pad_log_;
    if (last_pad_count_ > 0) {
        char line[96];
        std::snprintf(line, sizeof line, "%llu %x %.4f %.4f\n", (unsigned long long)last_pad_count_, last_pad_.buttons, last_pad_.left.x,
                      last_pad_.left.y);
        out << line;
    }
    out << "end\n";
    return out.str();
}

int OverflowDirector::casts() const {
    int total = 0;
    for (const auto& t : techniques_) total += t.casts;
    return total;
}

std::string OverflowDirector::summary_text(const Engine& engine) const {
    std::ostringstream out;
    static const char* names[] = {"none", "retired", "defeated", "cleared", "time_up"};
    out << "form " << config_.form << "\nseed " << config_.seed << "\nbot " << (config_.bot.empty() ? "-" : config_.bot) << "\nend "
        << names[int(outcome_)] << "\nend_reason " << end_reason_ << "\n";
    out << "run_steps " << clock_ << "\nrun_seconds " << run_seconds() << "\nlevel " << level(engine) << "\ndefeats " << defeats_
        << "\ncorrupted_defeats " << corrupted_defeats_ << "\n";
    const double first = level_up_steps_.empty() ? -1.0 : double(level_up_steps_.front()) * 1001.0 / 60000.0;
    out << "first_level_up_s " << first << "\n";
    int phase1 = 0;
    std::string gaps;
    std::uint64_t last = 0;
    for (const auto step : level_up_steps_) {
        if (step > std::uint64_t(config_.tuning.phase_steps)) break;
        ++phase1;
        char gap[32];
        std::snprintf(gap, sizeof gap, "%s%.1f", gaps.empty() ? "" : ",", double(step - last) * 1001.0 / 60000.0);
        gaps += gap;
        last = step;
    }
    out << "phase1_level_ups " << phase1 << "\nphase1_level_up_gaps_s " << (gaps.empty() ? "-" : gaps) << "\n";
    out << "level_ups " << level_up_steps_.size() << "\n";
    {
        // The level-up windows (freezes) of phase 1: how many in the first
        // minute, and their gaps.
        int first_minute = 0;
        std::string windows;
        std::uint64_t last_window = 0;
        for (const auto step : window_steps_) {
            if (step > std::uint64_t(config_.tuning.phase_steps)) break;
            if (double(step) * 1001.0 / 60000.0 <= 60.0) ++first_minute;
            char gap[32];
            std::snprintf(gap, sizeof gap, "%s%.1f", windows.empty() ? "" : ",", double(step - last_window) * 1001.0 / 60000.0);
            windows += gap;
            last_window = step;
        }
        out << "windows_first_minute " << first_minute << "\nphase1_window_gaps_s " << (windows.empty() ? "-" : windows) << "\n";
    }
    out << "art_hits_per_kill_median " << median(art_hits_per_kill_) << "\nart_kills_phase1 " << art_hits_per_kill_.size() << "\n";
    out << "hits_taken " << hits_taken_ << "\nmisses " << misses_ << "\nclamps " << clamps_ << "\nhighest_hit_share " << highest_hit_ << "\n";
    out << "poisonings " << poisonings_ << "\nhighest_poison_share " << highest_poison_ << "\ntaken_share " << taken_share_ << "\n";
    if (rules_) {
        const auto& tally = rules_->tally;
        out << "no_control_share " << (clock_ ? double(no_control_) / double(clock_) : 0.0) << "\n";
        out << "rules_no_control_share " << (tally.steps ? double(tally.no_control_steps) / double(tally.steps) : 0.0) << "\n";
        out << "rules_clamps " << tally.clamps << "\npanels " << tally.panels << "\npanels_skipped " << tally.panels_skipped << "\n";
        for (const auto& [source, amount] : tally.dealt_by) out << "dealt " << technique_name(source) << ' ' << amount << '\n';
    }
    for (const auto& [family, by] : hits_by_family_) out << "hits_by " << family << ' ' << by.first << ' ' << by.second << '\n';
    // (The damage taken per phase and family: who the balance has to answer.)
    for (const auto& [key, total] : taken_by_family_) out << "taken_by " << key.first << ' ' << key.second << ' ' << total << '\n';
    out << "panels_min_gap " << (panels_min_gap_ == (1 << 30) ? -1 : panels_min_gap_) << "\n";
    double worst = 1.0;
    std::string worst_family = "-";
    for (const auto& [family, tally] : closed_by_family_) {
        const double share = tally.second ? double(tally.first) / double(tally.second) : 1.0;
        out << "closed_in " << family << ' ' << share << ' ' << tally.first << '/' << tally.second << '\n';
        if (tally.second >= 5 && share < worst) {
            worst = share;
            worst_family = family;
        }
    }
    out << "closed_in_worst " << worst_family << ' ' << worst << "\n";
    out << "spawned " << spawned_ << "\ncorrupted_spawned " << corrupted_spawned_ << "\nrecycled " << recycled_ << "\nencircled " << encircled_total_
        << "\nspawn_failures " << spawn_failures_ << "\ngap_refused " << gap_refused_ << "\n";
    out << "disks_taken " << disks_taken_ << "\ndisks_used " << disks_used_ << "\nplugs_picked " << plugs_picked_ << "\n";
    out << "forge " << offer_state_.forge << "\ntrain " << offer_state_.train[0] << ' ' << offer_state_.train[1] << ' ' << offer_state_.train[2]
        << "\n";
    for (const auto& t : techniques_) out << "casts " << t.name << ' ' << t.casts << '\n';
    // The Guardians (4.4, round O1b): the plan, the defeated, the finale.
    out << "guardians";
    for (const auto& family : guardian_plan_) out << ' ' << (family.empty() ? "-" : family);
    out << "\nguardians_defeated " << guardians_defeated_.size();
    for (const auto& family : guardians_defeated_) out << ' ' << family;
    out << "\nfinale " << (finale_ ? 1 : 0) << "\nawakened " << (awakened_ ? 1 : 0) << '\n';
    for (const auto& [step, text] : event_log_)
        if (text.starts_with("guardian ")) out << "guardian_event " << step << ' ' << text.substr(9) << '\n';
    int lua_errors = 0;
    for (const auto& note : engine.notes()) {
        if (note.text.find("script call failed") != std::string::npos || note.text.find("script not found") != std::string::npos)
            lua_errors += int(note.count);
        out << "note " << note.count << ' ' << note.text << '\n';
    }
    out << "lua_errors " << lua_errors << "\n";
    for (const auto& m : minutes_)
        out << "minute " << m.minute << " level " << m.level << " defeats " << m.defeats << " hits " << m.hits_taken << " alive " << m.alive
            << " highest " << m.highest_hit << " taken " << m.taken << " contact " << m.contact << " shot " << m.shot << " poison " << m.poison
            << " near15 " << m.near15 << " near30 " << m.near30 << " trapped " << m.trapped << " art_kills " << m.art_kills << " tech_kills "
            << m.tech_kills << '\n';
    return out.str();
}

} // namespace dw4::engine::overflow
