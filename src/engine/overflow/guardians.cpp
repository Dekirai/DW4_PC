// Data Overflow (docs/SURVIVAL.md 4.4, round O1b lane E): the Guardians and
// the finale.
//
// A Guardian is a DW4 mid-boss family loaded from its own stage (H2's
// guardians list, never with its ded_*.lua: no story flag, scene or door).
// One closes each of phases 1-3: it enters when the phase's five minutes
// end, 40 units in front of the player, with BT_SBOSS and the arena's boss
// bar (i_bosshp02, METERCTRL bshp02 SETMODELHP); while it lives A(t) halves
// for at most two minutes. The phases stay on the run clock (the balance of
// docs/OVERFLOW_DIRECTOR.md 10 and 11 is built on it): the next phase's
// families come in over their ramp beside it. Its hits use the original's
// full reactions (the player hit rule, 4.8). Its defeat gives a Recovery
// disk and ¶ Data UP 100 and brings the phase music back; the next
// Guardian waits for it.
//
// The finale: LordKnightmon at 20:00 with BT_BOSS2 and the quest timer
// (5:00, to the run's end at 25:00; QS_HURRY in its last minute, z3COUNT2 at
// ten seconds, z3COUNT1 on the last three). Its defeat clears the
// simulation; the timer's 0:00 is TIME UP (Outcome::time_up). A phase
// Guardian still alive at 20:00 leaves (SQ_ERAS) for it.
//
// The elite fallback (4.4): a Guardian named in RunConfig::elite comes as the
// largest family of its area with its Normal row and HP x 12 (the spike of
// round O1b found no Guardian that needs it on the Violet Ring:
// docs/ROUND_O1B_E_NOTES.md).
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
// 4.4: family, name, Normal record (HPMAX), donor, phase (4: the finale),
// elite, damage factor. The factors (round O1b, kite runs of the four
// Rookies with factor 1): the worst hits were Mammothmon 36-42 % of MaxHP at
// 5:00, Blossomon 8-17 %, Anomalocarimon 5-15 % and SkullGreymon 5 % at
// 10:00, TonosamaGekomon 8 % and Diaboromon 4-45 % at 15:00, LordKnightmon
// 33-70 % at 20:00 (427 % with a special): about 15-20 % now, and every
// Guardian's hit at most Tuning::guardian_clamp (30 %).
const std::vector<GuardianRow> kGuardians = {
    {"e_blosso", "Blossomon", 76, "dng101b2", 1, "e_ogre", 1.0f},
    {"e_mammos", "Mammothmon", 97, "dng102b3", 1, "e_ogre", 0.15f}, // (0.25 in the measurements: Guilmon fell to it at 5-6 minutes)
    {"e_skulgrey", "SkullGreymon", 187, "stag0202/separate/e_skulgrey.igb", 2, "e_tyrano", 1.0f},
    {"e_anomalo", "Anomalocarimon", 196, "dng205b3", 2, "e_tyrano", 1.0f},
    {"e_tonogeko", "TonosamaGekomon", 280, "stag0302/separate/e_tonogeko.igb", 3, "e_torta", 1.0f},
    {"e_diabolo", "Diaboromon", 289, "dng309b4", 3, "e_torta", 0.45f},
    {"e_ldknight", "LordKnightmon", 348, "dng412b4", 4, "e_megadra", 0.4f},
};
constexpr int kFinale = 4;
constexpr int kRecoveryDisk = 9; // conItem 9 Recovery
} // namespace

const std::vector<GuardianRow>& guardian_table() { return kGuardians; }

const GuardianRow* guardian_row(std::string_view family) {
    for (const auto& row : kGuardians)
        if (family == row.family) return &row;
    return nullptr;
}

bool playable_form(std::string_view form) { return std::find(kRookies.begin(), kRookies.end(), form) != kRookies.end(); }

std::array<std::string, 4> draw_guardians(std::uint32_t seed, const std::array<std::string, 3>& forced) {
    // (Its own stream: the run's other draws stay as they were.)
    std::uint64_t random = 0x6A09E667F3BCC908ull ^ std::uint64_t(seed) * 0xBF58476D1CE4E5B9ull;
    std::array<std::string, 4> plan{};
    for (int phase = 1; phase <= 3; ++phase) {
        std::vector<const GuardianRow*> pair;
        for (const auto& row : kGuardians)
            if (row.phase == phase) pair.push_back(&row);
        const auto draw = next_random(random) % pair.size();
        plan[std::size_t(phase - 1)] = pair[draw]->family;
        if (const auto& f = forced[std::size_t(phase - 1)]; !f.empty() && guardian_row(f)) plan[std::size_t(phase - 1)] = f;
    }
    plan[3] = "e_ldknight";
    return plan;
}

int OverflowDirector::finale_left_steps() const {
    if (!finale_) return 0;
    const auto end = std::uint64_t(std::max(0, config_.tuning.end_steps));
    return clock_ >= end ? 0 : int(end - clock_);
}

std::optional<GuardianBar> OverflowDirector::guardian_bar(Engine& engine) const {
    if (!guardian_) return std::nullopt;
    auto* models = engine.get<ModelCtrlManager>();
    const auto* ram = models ? models->find(guardian_->family, guardian_->ram) : nullptr;
    if (!ram || !ram->generated) return std::nullopt;
    return GuardianBar{guardian_->name, std::max(0, ram->hp), std::max(1, ram->max_hp)};
}

bool OverflowDirector::spawn_guardian(Engine& engine, int stage) {
    auto* db = engine.get<DatabaseManager>();
    if (!db) return false;
    const auto* row = guardian_row(guardian_plan_[std::size_t(stage - 1)]);
    if (!row) return false;
    const bool elite = std::find(config_.elite.begin(), config_.elite.end(), row->family) != config_.elite.end();
    const auto* elite_row = family_row(row->elite);
    if (elite && !elite_row) return false;
    const std::string family = elite ? elite_row->family : row->family;
    const int record = elite ? elite_row->record : row->record;
    Packet find;
    port_send(engine, "MODELCTRL", family, -1, "FIND_SPACE", find);
    if (find.ints.empty()) return false; // (an elite of a regular family: its RAMs may all be in use; the next step tries again)
    const int ram = find.ints[0];
    const auto& t = config_.tuning;
    const Vec3f me = player_position(engine);
    Vec3f point;
    if (!spawn_point(engine, me, t.guardian_distance - 6.0f, t.guardian_distance + 6.0f, true, point, 0.0f) &&
        !spawn_point(engine, me, 20.0f, t.guardian_distance + 20.0f, false, point, 0.0f))
        return false;
    const float factor = elite ? t.elite_hp : stage == kFinale ? t.finale_hp : t.guardian_hp;
    // (The time's linear growth only, 1 + 0.10 a minute: with the crowd's
    // e^(0.08 x minutes) on top TonosamaGekomon had 75,878 HP at 15:00 and
    // LordKnightmon 203,930 at 20:00, and neither fell in five minutes of
    // the kite bot's runs; one target takes only the Art strikes that find
    // it nearest.)
    const double minutes = run_seconds() / 60.0;
    const double hp = double(db->number("enemy", record, "HPMAX").value_or(500)) * double(factor) * (1.0 + double(t.hp_per_minute) * minutes);
    const int max_hp = std::max(1, int(std::lround(hp)));
    Packet generate;
    // (It faces the player.)
    generate.vectors = {point, Vec3f{0, std::atan2(me.x - point.x, me.z - point.z) * 180.0f / 3.14159265f, 0}};
    generate.strings.emplace_back(std::string("SQ_ENTR"));
    generate.ints = {record, max_hp};
    port_send(engine, "MODELCTRL", family, ram, "GENERATE", generate);
    port_send(engine, "MODELCTRL", family, ram, "ONCTRL");
    if (rules_) rules_->set_enemy_class(family, ram, EnemyClass::guardian);
    // The arena's boss bar (stagBS02's first.lua does the same for its boss).
    {
        Packet bar;
        bar.vectors.push_back(Vec3f{0, 205, 0});
        bar.strings.emplace_back(std::string("SQ_OPEN"));
        port_send(engine, "MODELCTRL", "i_bosshp02", 0, "GENERATE", bar);
        Packet meter;
        meter.strings.push_back(family);
        meter.ints.push_back(ram);
        port_send(engine, "METERCTRL", "bshp02", 0, "SETMODELHP", meter);
    }
    guardian_ = Guardian{family, elite ? std::string(elite_row->name) : std::string(row->name), ram, stage, clock_, elite};
    if (stage == kFinale) {
        finale_ = true;
        finale_at_ = clock_;
    }
    notice(guardian_->name + (stage == kFinale ? ": the finale" : " appears"));
    event_log_.push_back({clock_, "guardian " + std::string(row->family) + (elite ? " elite " + family : std::string()) + " in hp " +
                                      std::to_string(max_hp) + " at " + std::to_string(int(point.x)) + "," + std::to_string(int(point.z))});
    return true;
}

// The Guardian fell (defeated) or left (the finale took its place).
void OverflowDirector::guardian_down(Engine& engine, bool defeated) {
    if (!guardian_) return;
    const Guardian g = *guardian_;
    guardian_.reset();
    port_send(engine, "MODELCTRL", "i_bosshp02", 0, "SQ_CLOSE");
    const auto* row = guardian_row(guardian_plan_[std::size_t(g.stage - 1)]);
    if (!defeated) {
        std::string hp;
        if (auto* models = engine.get<ModelCtrlManager>())
            if (const auto* ram = models->find(g.family, g.ram)) hp = " with " + std::to_string(std::max(0, ram->hp)) + " of " + std::to_string(ram->max_hp) + " HP";
        event_log_.push_back({clock_, "guardian " + g.family + " left" + hp});
        return;
    }
    // (The stand-in of an elite fallback counts for its Guardian's record.)
    guardians_defeated_.push_back(row ? row->family : g.family);
    event_log_.push_back({clock_, "guardian " + g.family + " defeated after " + std::to_string((clock_ - g.at) / 60) + " s"});
    notice(g.name + " defeated");
    if (g.stage == kFinale) {
        begin_end(engine, Outcome::cleared);
        return;
    }
    // Its drops (4.4): a Recovery disk and ¶ Data UP 100.
    Vec3f at = player_position(engine);
    if (auto* models = engine.get<ModelCtrlManager>())
        if (const auto* ram = models->find(g.family, g.ram); ram && ram->node) at = ram_position(*ram);
    drops_.push_back({"g_000xdata1", 3, 1, {at.x + 3.0f, at.y, at.z}});
    drops_.push_back({"g_000item", kRecoveryDisk, 0, {at.x - 3.0f, at.y, at.z}});
}

void OverflowDirector::step_guardians(Engine& engine) {
    const auto& t = config_.tuning;
    if (!t.guardians) return;
    // A Guardian that is gone without its defeat (erased, a RAM taken over).
    if (guardian_) {
        auto* models = engine.get<ModelCtrlManager>();
        const auto* ram = models ? models->find(guardian_->family, guardian_->ram) : nullptr;
        if (!ram || !ram->generated) guardian_down(engine, false);
    }
    const auto finale_step = std::uint64_t(4) * std::uint64_t(t.phase_steps);
    if (!finale_ && t.end_steps > int(finale_step) && clock_ >= finale_step) {
        // The finale (4.4): a phase Guardian still there leaves for it.
        if (guardian_ && guardian_->stage != kFinale) {
            port_send(engine, "MODELCTRL", guardian_->family, guardian_->ram, "SQ_ERAS");
            guardian_down(engine, false);
        }
        if (spawn_guardian(engine, kFinale)) guardian_next_ = kFinale + 1;
    } else if (!guardian_ && guardian_next_ <= 3 && clock_ < finale_step) {
        const std::uint64_t due = guardian_next_ == 1 && t.guardian_first > 0 ? std::uint64_t(t.guardian_first)
                                                                               : std::uint64_t(guardian_next_) * std::uint64_t(t.phase_steps);
        if (clock_ >= due && spawn_guardian(engine, guardian_next_)) ++guardian_next_;
    }
    // The quest timer's sounds (dngq3b1e's chk_i_timenm0: z3COUNT2 every ten
    // seconds and z3COUNT1 on the others; here at ten and on the last three).
    if (finale_) {
        const int left = int(std::ceil(double(finale_left_steps()) * 1001.0 / 60000.0 - 1e-9));
        if (left != count_said_ && left > 0) {
            count_said_ = left;
            if (left == 10) detail::play_sound(engine, "z3COUNT2");
            else if (left <= 3) detail::play_sound(engine, "z3COUNT1");
        }
    }
}

// The music (4.2, 11): the phase's, BT_SBOSS while a Guardian lives, BT_BOSS2
// for the finale and QS_HURRY in its last minute.
void OverflowDirector::step_music(Engine& engine) {
    if (steps_ < music_at_) return;
    std::string want;
    if (finale_) want = finale_left_steps() <= config_.tuning.finale_hurry ? "QS_HURRY" : "BT_BOSS2";
    else if (guardian_) want = "BT_SBOSS";
    else want = phase_music()[std::size_t(phase() - 1)].name;
    if (want == music_now_) return;
    music_now_ = want;
    detail::play_sound(engine, want);
}

} // namespace dw4::engine::overflow
