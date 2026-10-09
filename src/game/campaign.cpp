#include "campaign.hpp"

#include "dw4/pad.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace dw4::game {
namespace {

// ---------------------------------------------------------------------------
// Story data
// ---------------------------------------------------------------------------
// Conditions over the game state, as the objectives and exits below use
// them: atoms F<n> (story flag n set), K<n> (key item n in the common flag
// folder), P<n> (the story phase is at least n, see phase()), D<model>#<n>
// (that model of the running stage is used up: HP 0, e.g. a console that
// took its one hit), E<n> (the stage holding the exit was entered at player
// point n, see Exit::entry), S<n> (player 1 has reached form n, shinka row:
// BASE_ISUSE), C<n> (player 1 is form n now), L<n> (player 1 is level n or
// more); '!' negates an atom, '&' binds tighter than '|'; no parentheses.
// Empty = always.

// The story phase the lobby scripts branch on (stagML11 start.lua and
// mate00.lua, stagML02 matf00/matf02/first.lua): flags 1..6 are set and
// cleared in turn, 86 marks the cleared game (stagBS06 hit_f_devl3.lua,
// stagML11 start.lua turns 70 into 86).
//   1 new game: the lobby gate is off       (stagML11 start.lua)
//   2 Ophanimon's first briefing done        (stagML02 matf00.lua)
//   3 Apocalymon defeated and reported       (stagML02 matf00.lua, hurry)
//   4 the lobby system restored              (stagML02 first.lua, Seraphimon)
//   5 Belphemon defeated and reported        (stagML02 matf02.lua)
//   6 Lucemon defeated and reported          (stagML02 matf00.lua)
//   7 Daemon defeated (flag 86)
int phase(engine::Engine& engine) {
    auto& flags = engine.flags();
    auto on = [&](int n) {
        const auto it = flags.find(n);
        return it != flags.end() && it->second;
    };
    if (on(86)) return 7;
    for (int p = 6; p >= 2; --p)
        if (on(p)) return p;
    return 1;
}

// `entry`: the player point the stage holding the condition was (or will be)
// entered at, -1 unknown (E atoms are false then).
bool atom(engine::Engine& engine, std::string_view text, int entry) {
    bool negate = false;
    while (!text.empty() && text.front() == '!') {
        negate = !negate;
        text.remove_prefix(1);
    }
    if (text.size() < 2) return negate;
    const int n = std::atoi(std::string(text.substr(1)).c_str());
    bool value = false;
    if (text.front() == 'D') {
        const std::string spec(text.substr(1));
        const auto hash = spec.find('#');
        const std::string name = spec.substr(0, hash);
        const int index = hash == std::string::npos ? 0 : std::atoi(spec.c_str() + hash + 1);
        if (auto* models = engine.get<engine::ModelCtrlManager>())
            for (const auto& ram : models->rams())
                if (ram->generated && ram->info_name == name && ram->index == index && ram->hp <= 0) value = true;
    } else if (text.front() == 'F') {
        const auto it = engine.flags().find(n);
        value = it != engine.flags().end() && it->second;
    } else if (text.front() == 'K') {
        if (auto* sys = engine.get<engine::GameSystem>()) {
            const auto it = sys->flag_folder.find(n);
            value = it != sys->flag_folder.end() && it->second;
        }
    } else if (text.front() == 'P') {
        value = phase(engine) >= n;
    } else if (text.front() == 'E') {
        value = entry >= 0 && entry == n;
    } else if (text.front() == 'S' || text.front() == 'C' || text.front() == 'L') {
        if (auto* sys = engine.get<engine::GameSystem>()) {
            const auto& slot = sys->players[0];
            if (text.front() == 'S') {
                const auto it = slot.base_isuse.find(n);
                value = it != slot.base_isuse.end() && it->second;
            } else {
                value = text.front() == 'C' ? slot.stats.species == n : slot.stats.level >= n;
            }
        }
    }
    return value != negate;
}

bool holds(engine::Engine& engine, std::string_view condition, int entry = -1) {
    if (condition.empty()) return true;
    for (std::size_t start = 0;;) {
        const auto bar = condition.find('|', start);
        const auto alternative = condition.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start);
        bool all = true;
        for (std::size_t at = 0; all;) {
            const auto amp = alternative.find('&', at);
            all = atom(engine, alternative.substr(at, amp == std::string_view::npos ? std::string_view::npos : amp - at), entry);
            if (amp == std::string_view::npos) break;
            at = amp + 1;
        }
        if (all) return true;
        if (bar == std::string_view::npos) return false;
        start = bar + 1;
    }
}

// The story objectives in the order a player meets them. `done` says the
// objective is behind the player; the first one not done is worked on: the
// player travels to `stage` and does `action` there:
//   explore:<mats>   play through the stage to one of the mats (ExploreBot:
//                    enemies, pickups, switches, waves on the way)
//   mat:<mat>        walk onto a mat (lobby events)
//   talk:<model>#<n> walk up to a model and press A (talks; in the lobby
//   strike:<model>#<n> A also checks consoles, elsewhere it attacks)
//   step:<model>#<n> stand on a model (floor switches)
//   evolve:<n>       digivolve player 1 to form n at the Digi-Lab (the
//                    stagML12 Digi-Elf n_digilff#1, bML01004.lua)
//   wait             let the stage scripts run (demos on arrival)
struct Objective {
    const char* name;
    const char* done;
    const char* stage;
    const char* action;
    // >= 0: the objective's stage counts only when entered at that player
    // point (a part of the stage reached only from that side; the exits
    // leading there name their point, Exit::entry).
    int entry = -1;
};
constexpr Objective kObjectives[] = {
    // Phase 1. The Digi-Elf sends the new member to the Commander Room;
    // walking up to Ophanimon (matf00) plays her briefing and sets phase 2.
    {"Ophanimon's first briefing", "P2", "stagml02", "mat:matf00"},
    // Phase 2. The lobby gate (stagML11 mate00) first offers the training
    // field stag0001 (until flag 503), whose exit leads on to Death Valley.
    // The gob gate in stag0101 opens with the security switches, which are
    // powered only with key items 0 and 1 (stag0101 start.lua: flag 900),
    // dropped by the bosses of dng101b2 (Blossomon) and dng102b3
    // (Mammothmon).
    {"key item 0 (dng101b2 boss)", "K0|P3", "dng101b2", "explore:matz02"},
    {"key item 1 (dng102b3 boss)", "K1|P3", "dng102b3", "explore:matz02"},
    // (The ExploreBot fights the cannons guarding the switch and steps on
    // it as a floor switch while heading for the gate's exit.)
    {"security switch at the gob gate (stag0101)", "F37|F20|P3", "stag0101", "explore:matz01"},
    // Behind the gate the gate fields lead to Leomon (stagGF03 math00 after
    // the e_mecha4 fight): flag 20, then the stage goes on to the Commander
    // Room, where Ophanimon hands over key item 2 (stagML02 first.lua).
    // (The cover gate opens to its console, which works only before the
    // e_mecha4 fight: stagGF03 hit_g_covercnsz05.lua, flag 383.)
    {"cover gate console (staggf03)", "F383|F20|P3|Dg_covercnsz05#0", "staggf03", "talk:g_covercnsz05#0"},
    {"Leomon in the gate field (staggf03)", "F20|P3", "staggf03", "explore:math00"},
    {"key item 2 from Ophanimon", "K2|P3", "stagml02", "wait"},
    // Key item 2 opens stag0102 matg00 (the boat to stag0103), on to
    // stag0104 and the dng103 tower with Apocalymon at its top.
    {"Apocalymon (stagBS01)", "F67|P3", "stagbs01", "explore:matz01"},
    {"report to Ophanimon (phase 3)", "P3", "stagml02", "mat:matf00"},
    // Phase 3 (the lobby alarm, the gate is off): the four lobby consoles in
    // stagML12 (stagML12 hit_g_evenconsz05.lua) in the order 0, junk
    // console in stagML13 (needs flag 329), 2, 3, 1; the last one sends the
    // player to the Commander Room, where Seraphimon restores the system
    // (stagML02 first.lua: phase 4).
    {"lobby console 0 (stagML12)", "F329|P4", "stagml12", "strike:g_evenconsz05#0"},
    {"junk console (stagML13)", "F321|P4", "stagml13", "strike:g_junkconsz05#0"},
    // (Consoles 1-3 lie behind the hiding box: the side reached from the
    // stagML13 gate mate00 (player point 5), open once the junk console is
    // off.)
    {"lobby console 2 (stagML12)", "F331|P4", "stagml12", "strike:g_evenconsz05#2", 5},
    {"lobby console 3 (stagML12)", "F332|P4", "stagml12", "strike:g_evenconsz05#3", 5},
    {"lobby console 1 (stagML12)", "F324|P4", "stagml12", "strike:g_evenconsz05#1", 5},
    {"Seraphimon restores the lobby", "P4", "stagml02", "wait"},
    // Phase 4. Area 2: key items 4 (dng204b2) and 5 (dng205b3) open the way
    // from stag0202 to stag0204 (stag0202 start.lua), the energy plug in
    // stag0205 (reached through the gate fields, flag 49) the way into the
    // dng206 tower with Belphemon (all 36 plugs broken, brk_g_eneplugz02.lua).
    {"key item 4 (dng204b2 boss)", "K4|P5", "dng204b2", "explore:matz02"},
    {"key item 5 (dng205b3 boss)", "K5|P5", "dng205b3", "explore:matz02"},
    // (The plugs lie by the train's arrival, player point 0; the gate field
    // side, point 1, is a plateau walled in by kind 0x802 walls, which stop
    // the player: no way down.)
    {"energy plugs (stag0205)", "F49|P5", "stag0205", "strike:g_eneplugz02#*", 0},
    {"Belphemon (stagBS02)", "F68|P5", "stagbs02", "explore:matz01"},
    // After Belphemon: Ophanimon (flag 334), Seraphimon (82), Leomon in
    // stagML12 (335), then Seraphimon's briefing on matf02 (phase 5).
    {"talk to Ophanimon (flag 334)", "F334|P5", "stagml02", "talk:n_ofanim#0"},
    {"talk to Seraphimon (flag 82)", "F82|P5", "stagml02", "talk:n_seraph#0"},
    {"talk to Leomon (flag 335)", "F335|P5", "stagml12", "talk:n_leomon#0"},
    {"Seraphimon's briefing (phase 5)", "P5", "stagml02", "mat:matf02"},
    // Phase 5. Area 3: key items 6, 7 and 8 (dng307b2, dng308b3, dng309b4;
    // dng309 lies behind stag0301 matz01, opened by the stag0302 console
    // (flag 57) after the stag0301 lever (flag 56)), then Gekomon in stag0302
    // (flag 333) clears the way to stag0303/0304 and the dng310 fortress
    // with Lucemon.
    {"key item 6 (dng307b2 boss)", "K6|P6", "dng307b2", "explore:matz02"},
    // The cell walls of dng308b1-b3 and dng309b2/b3 (hit_g_dcellwall.lua)
    // break only to a fire attack, the scrap walls of dng412b1-b4
    // (hit_g_dscrap.lua) only to thunder: a power pocket chip of that element
    // (in a weapon of rank 1 or more, rare drops) or a technique. The partner
    // is Veemon (Petty Bolt, thunder); from level 16 the Digi-Elf n_digilff#1
    // in stagML12 adds a second rookie form (stagML12 n_digilff_01.lua ->
    // sin_digi.lua: Veemon -> Guilmon, shinka row 2, Petty Fire), and the
    // Digi-Lab behind her (bML01004.lua, LABO) digivolves between the two.
    {"Guilmon form from the Digi-Elf (level 16)", "S2|K7&K8|P6|!L16", "stagml12", "talk:n_digilff#1"},
    {"digivolve to Guilmon (fire for the cell walls)", "C2|K7&K8|P6|!S2", "stagml12", "evolve:2"},
    {"key item 7 (dng308b3 boss)", "K7|P6", "dng308b3", "explore:matz02"},
    {"lever (stag0301)", "F56|P6", "stag0301", "strike:g_lever01z03#0"},
    {"console (stag0302)", "F57|P6", "stag0302", "strike:g_consolez03#0"},
    {"key item 8 (dng309b4 boss)", "K8|P6", "dng309b4", "explore:matz02"},
    {"Gekomon (stag0302, flag 333)", "F333|P6", "stag0302", "talk:n_gekoa#0"},
    {"Lucemon (stagBS03)", "F69|P6", "stagbs03", "explore:matz01"},
    {"report to Ophanimon (phase 6)", "P6", "stagml02", "mat:matf00"},
    // Phase 6. Area 4: key items 9 (dng411b3) and 10 (dng412b4) let the
    // starship in stag0401 (mate00) fly to stag0403; stag0404 leads to the
    // last three boss stages and the staff roll (stagBS06 matz01).
    {"digivolve back to Veemon (thunder for the scrap walls)", "C1|K10|P7|!S1", "stagml12", "evolve:1"},
    {"key item 9 (dng411b3 boss)", "K9|P7", "dng411b3", "explore:matz02"},
    {"key item 10 (dng412b4 boss)", "K10|P7", "dng412b4", "explore:matz02"},
    {"Daemon (stagBS06)", "F86|F70", "stagbs06", "explore:matz01"},
    {"staff roll", "", "stagis20", "wait"},
};

// The stage exits the story uses (the exit survey's list without the
// quest dungeons and the stag0401 quest entrances) and the game state each
// needs to be usable. "transporter" marks the lobby gate's area list.
// `entry` is the target's player point the exit leads to (funcStageChange's
// second argument, SETGENERATEPOS), given where an objective or an exit
// depends on the side a stage was entered from; -1 = not needed.
struct Exit {
    const char* stage;
    const char* mat;
    const char* target;
    const char* when;
    int entry = -1;
};
constexpr Exit kExits[] = {
    // Lobby (walked to; gate prompts answered).
    {"stagml11", "mate01", "stagml02", "!F424"},
    {"stagml11", "matz00", "stagml12", ""},     {"stagml11", "matz01", "stagml13", ""},
    {"stagml11", "mate00", "stag0001", "transporter"}, {"stagml11", "mate00", "stag0101", "transporter"},
    {"stagml11", "mate00", "stag0201", "transporter"}, {"stagml11", "mate00", "stag0301", "transporter"},
    {"stagml11", "mate00", "stag0401", "transporter"},
    {"stagml02", "mate00", "stagml11", ""},
    {"stagml12", "matz00", "stagml11", ""},     {"stagml12", "matz01", "stagml13", ""},
    {"stagml13", "matz00", "stagml11", ""},     {"stagml13", "matz01", "stagml12", ""},
    {"stagml13", "mate00", "stagml12", "F321", 5},
    // Training field (stag0001 matz00.lua: both answers lead to stag0101).
    {"stag0001", "matz00", "stag0101", ""},
    // Area 1.
    {"stag0101", "matz00", "dng101b1", ""},     {"stag0101", "matz01", "staggf01", "F37|F20"},
    {"stag0101", "matz02", "stag0102", ""},
    {"stag0102", "matg00", "stag0103", "K2"},   {"stag0102", "matz00", "dng102b1", ""},
    {"stag0102", "matz01", "stag0101", ""},
    {"stag0103", "matz00", "stag0104", ""},
    {"stag0104", "matz00", "dng103f1", ""},     {"stag0104", "matz01", "stag0102", ""},
    {"dng101b1", "matz01", "stag0101", ""},     {"dng101b1", "matz02", "dng101b2", ""},
    {"dng101b2", "matz01", "dng101b1", ""},     {"dng101b2", "matz02", "stagml11", ""},
    {"dng102b1", "matz01", "stag0102", ""},     {"dng102b1", "matz02", "dng102b2", ""},
    {"dng102b2", "matz01", "dng102b1", ""},     {"dng102b2", "matz02", "dng102b3", ""},
    {"dng102b3", "matz01", "dng102b2", ""},     {"dng102b3", "matz02", "stagml11", ""},
    {"dng103f1", "matz01", "stag0104", ""},     {"dng103f1", "matz02", "dng103f2", ""},
    {"dng103f2", "matz01", "dng103f1", ""},     {"dng103f2", "matz02", "stagbs01", ""},
    {"stagbs01", "matz01", "stagml11", ""},
    // Gate fields (staggf03 math00 goes on to the Commander Room).
    {"staggf01", "matz00", "stag0101", ""},     {"staggf01", "matz01", "staggf02", ""},
    {"staggf02", "matz00", "staggf01", ""},     {"staggf02", "matz01", "staggf03", ""},
    {"staggf02", "matz02", "staggf04", ""},
    {"staggf03", "matz00", "staggf02", ""},     {"staggf03", "math00", "stagml02", "!F20"},
    {"staggf04", "matz00", "stag0205", "", 1}, {"staggf04", "matz01", "staggf02", ""},
    // Area 2.
    {"stag0201", "matz00", "stag0202", ""},     {"stag0201", "matz01", "dng204b1", ""},
    {"stag0202", "matz00", "stag0201", ""},     {"stag0202", "matz01", "dng205b1", ""},
    {"stag0202", "matz02", "stag0204", "K4&K5"},
    {"stag0204", "matz00", "stag0202", ""},     {"stag0204", "matz01", "dng206f1", "F49"},
    // stag0205: the train's arrival (point 0) with the plugs and the
    // teleporter (mate00) and the gate field plateau (point 1) are not joined.
    {"stag0205", "mate00", "stag0201", "E0"},   {"stag0205", "matz00", "staggf04", "E1"},
    // The train (stag0201 matz02, its prompt's first answer) and its track
    // ends (stag0203 matz00-03 back to stag0201, matz04 to stag0205).
    {"stag0201", "matz02", "stag0203", ""},
    {"stag0203", "matz00", "stag0201", ""},     {"stag0203", "matz01", "stag0201", ""},
    {"stag0203", "matz02", "stag0201", ""},     {"stag0203", "matz03", "stag0201", ""},
    {"stag0203", "matz04", "stag0205", "", 0},
    {"dng204b1", "matz01", "stag0201", ""},     {"dng204b1", "matz02", "dng204b2", ""},
    {"dng204b2", "matz01", "dng204b1", ""},     {"dng204b2", "matz02", "stagml11", ""},
    {"dng205b1", "matz01", "stag0202", ""},     {"dng205b1", "matz02", "dng205b2", ""},
    {"dng205b2", "matz01", "dng205b1", ""},     {"dng205b2", "matz02", "dng205b3", ""},
    {"dng205b3", "matz01", "dng205b2", ""},     {"dng205b3", "matz02", "stagml11", ""},
    {"dng206f1", "matz01", "stag0204", ""},     {"dng206f1", "matz02", "dng206f2", ""},
    {"dng206f2", "matz01", "dng206f1", ""},     {"dng206f2", "matz02", "stagbs02", ""},
    {"stagbs02", "matz01", "stagml11", ""},
    // Area 3.
    {"stag0301", "mate00", "stag0302", ""},     {"stag0301", "matz00", "dng307b1", ""},
    {"stag0301", "matz01", "dng309b1", "F57"},
    {"stag0302", "matf00", "stag0301", ""},     {"stag0302", "matz00", "dng308b1", ""},
    {"stag0302", "matz01", "stag0303", "F333"},
    {"stag0303", "matg00", "stag0304", ""},     {"stag0303", "matz00", "stag0302", ""},
    {"stag0304", "mate00", "stag0303", ""},     {"stag0304", "matz00", "dng310f1", ""},
    {"dng307b1", "matz01", "stag0301", ""},     {"dng307b1", "matz02", "dng307b2", ""},
    {"dng307b2", "matz01", "dng307b1", ""},     {"dng307b2", "matz02", "stagml11", ""},
    {"dng308b1", "matz01", "stag0302", ""},     {"dng308b1", "matz02", "dng308b2", ""},
    {"dng308b2", "matz01", "dng308b1", ""},     {"dng308b2", "matz02", "dng308b3", ""},
    {"dng308b3", "matz01", "dng308b2", ""},     {"dng308b3", "matz02", "stagml11", ""},
    {"dng309b1", "matz01", "stag0301", ""},     {"dng309b1", "matz02", "dng309b2", ""},
    {"dng309b2", "matz01", "dng309b1", ""},     {"dng309b2", "matz02", "dng309b3", ""},
    {"dng309b3", "matz01", "dng309b2", ""},     {"dng309b3", "matz02", "dng309b4", ""},
    {"dng309b4", "matz01", "dng309b3", ""},     {"dng309b4", "matz02", "stagml11", ""},
    {"dng310f1", "matz01", "stag0304", ""},
    {"dng310f1", "matz02", "dng310f2", ""},     {"dng310f1", "matz03", "dng310f2", ""},
    {"dng310f1", "matz04", "dng310f2", ""},     {"dng310f1", "matz05", "dng310f2", ""},
    {"dng310f1", "matz06", "dng310f2", ""},     {"dng310f1", "matz07", "dng310f2", ""},
    {"dng310f1", "matz08", "dng310f2", ""},
    {"dng310f2", "matz01", "dng310f1", ""},     {"dng310f2", "matz02", "dng310f1", ""},
    {"dng310f2", "matz04", "dng310f1", ""},     {"dng310f2", "matz05", "dng310f1", ""},
    {"dng310f2", "matz07", "dng310f1", ""},     {"dng310f2", "matz08", "dng310f1", ""},
    {"dng310f2", "matz09", "dng310f1", ""},
    {"dng310f2", "matz03", "dng310f3", ""},     {"dng310f2", "matz06", "dng310f3", ""},
    {"dng310f2", "matz10", "dng310f3", ""},     {"dng310f2", "matz11", "dng310f3", ""},
    {"dng310f2", "matz12", "dng310f3", ""},
    {"dng310f3", "matz01", "dng310f2", ""},     {"dng310f3", "matz02", "dng310f2", ""},
    {"dng310f3", "matz03", "dng310f2", ""},     {"dng310f3", "matz04", "dng310f2", ""},
    {"dng310f3", "matz05", "dng310f2", ""},     {"dng310f3", "matz06", "stagbs03", ""},
    {"stagbs03", "matz01", "stagml11", ""},
    // Area 4 (stag0401 mate00 = the starship, stag0401 mate00.lua: key
    // items 9 and 10).
    {"stag0401", "matz00", "dng411b1", ""},     {"stag0401", "matz01", "dng412b1", ""},
    {"stag0401", "mate00", "stag0403", "K9&K10"},
    {"dng411b1", "matz01", "stag0401", ""},     {"dng411b1", "matz02", "dng411b2", ""},
    {"dng411b2", "matz01", "dng411b1", ""},     {"dng411b2", "matz02", "dng411b3", ""},
    {"dng411b3", "matz01", "dng411b2", ""},     {"dng411b3", "matz02", "stagml11", ""},
    {"dng412b1", "matz01", "stag0401", ""},     {"dng412b1", "matz02", "dng412b2", ""},
    {"dng412b2", "matz01", "dng412b1", ""},     {"dng412b2", "matz02", "dng412b3", ""},
    {"dng412b3", "matz01", "dng412b2", ""},     {"dng412b3", "matz02", "dng412b4", ""},
    {"dng412b4", "matz01", "dng412b3", ""},     {"dng412b4", "matz02", "stagml11", ""},
    {"stag0403", "matz00", "stag0404", ""},     {"stag0404", "matf00", "stagbs04", ""},
    {"stagbs04", "matz01", "stagbs05", ""},     {"stagbs05", "matz01", "stagbs06", ""},
    {"stagbs06", "matz01", "stagis20", ""},
};

// The lobby gate's area list (stagML11 mate00.lua) by phase: none while it
// is off (phases 1 and 3), the training field until flag 503.
std::vector<std::string> transporter_areas(engine::Engine& engine) {
    switch (phase(engine)) {
    case 2: return {holds(engine, "F503") ? "stag0101" : "stag0001"};
    case 4: return {"stag0101", "stag0201"};
    case 5: return {"stag0101", "stag0201", "stag0301"};
    case 6:
    case 7: return {"stag0101", "stag0201", "stag0301", "stag0401"};
    default: return {};
    }
}

// Per-stage settings the ExploreBot reads from the environment at a stage
// start (a switch puzzle's solution, given like the survey presets).
struct StageSetting {
    const char* stage;
    const char* name;
    const char* value;
};
constexpr StageSetting kStageSettings[] = {
    // stag0403: the floor switch order that lowers the bridge to matz00.
    {"stag0403", "DW4_MAT_SEQUENCE", "maty07,maty00,maty04,maty03,maty02"},
    {"stag0403", "DW4_AVOID_MATS", "maty"},
};

// Targets struck in passing from a ride (the stag0103 boat: the six factory
// towers and the two shelves must all break before the boat reaches the
// bomb, stag0103 hit_g_facttowerz06.lua / hit_g_shelfz06.lua: flags 481-489,
// else matf00 blows it up and sends the player back to stag0102). Each model
// prefix listed (or "<model>#<n>", that RAM only) is attacked whenever one with
// HP that is not switched or powered off is within `range`, enemies within
// `fight`; shots within `guard` are guarded against.
// The stag0203 train runs straight at every junction unless the junction's
// target is struck while the train is on its mat (stag0203 mate00..mate10:
// g_IFLG00 = track, g_IFLG01 = 1 straight / 2 turn, applied when the train
// leaves the mat). The way to matz04 (stag0205, where the energy plugs are):
// track 1 turns at mate00 (g_turnright#0) to 4, straight at mate03 to 5,
// turns at mate04 (g_turnright#4) to 9, at mate08 (g_turnleft#2) to 20, and
// the gate signs of mate09/mate10 (g_gatesign#0/#1, else they turn back to
// track 10) lead on to 21 and 22. The signs stand beside the track and the
// stage's weapon w_sp0203 is a slash (every sequence label "slash") whose
// box reaches 14-18 ahead of the player, while the sign boxes begin 30 from
// the track (the train's edge is 16 from it): the player moves to the edge
// nearest the sign early and swings once its hit box is within `strike`.
// A sign of `avoid` that is not wanted (the other way at a junction) within
// reach holds the swing back, also at enemies.
struct RideTargets {
    const char* stage;
    const char* prefixes; // '|'-separated model name prefixes
    float range, fight, guard;
    float strike = 0.0f;    // > 0: A only with the target's hit box this close
    const char* avoid = ""; // '|'-separated prefixes of models not to hit
};
constexpr RideTargets kRideTargets[] = {
    {"stag0103", "g_facttowerz06|g_shelfz06", 70.0f, 20.0f, 40.0f},
    {"stag0203", "g_turnright#0|g_turnright#4|g_turnleft#2|g_gatesign#0|g_gatesign#1", 220.0f, 20.0f, 0.0f, 16.0f,
     "g_turn|g_gatesign"},
};

// Whether a model is named by a '|'-separated list ("<model>#<n>" names that
// RAM, otherwise a name prefix); the list starts and ends with '|'.
bool listed_in(const std::string& list, const engine::ModelCtrlManager::ModelRam& ram) {
    const std::string exact = ram.info_name + "#" + std::to_string(ram.index);
    for (std::size_t at = 1; at < list.size();) {
        const auto bar = list.find('|', at);
        if (bar == std::string::npos) break;
        const std::string token = list.substr(at, bar - at);
        if (!token.empty() && (token.find('#') != std::string::npos ? token == exact : ram.info_name.rfind(token, 0) == 0)) return true;
        at = bar + 1;
    }
    return false;
}

// The point of a model's hit boxes (world X/Z, from the local boxes and the
// node matrix) nearest to `p`; the model's origin without boxes.
engine::Vec3f nearest_hit_point(const engine::ModelCtrlManager::ModelRam& ram, const engine::Vec3f& p) {
    const auto& m = ram.node->matrix;
    engine::Vec3f best{m(3, 0), m(3, 1), m(3, 2)};
    if (!ram.hit || ram.hit->boxes.empty()) return best;
    float best_d = 1e30f;
    for (const auto& [lo, hi] : ram.hit->boxes) {
        float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
        for (int corner = 0; corner < 4; ++corner) {
            const float x = corner & 1 ? hi.x : lo.x, z = corner & 2 ? hi.z : lo.z, y = (lo.y + hi.y) * 0.5f;
            const float wx = x * m(0, 0) + y * m(1, 0) + z * m(2, 0) + m(3, 0), wz = x * m(0, 2) + y * m(1, 2) + z * m(2, 2) + m(3, 2);
            x0 = std::min(x0, wx);
            x1 = std::max(x1, wx);
            z0 = std::min(z0, wz);
            z1 = std::max(z1, wz);
        }
        const engine::Vec3f q{std::clamp(p.x, x0, x1), m(3, 1), std::clamp(p.z, z0, z1)};
        const float d = std::hypot(q.x - p.x, q.z - p.z);
        if (d < best_d) {
            best_d = d;
            best = q;
        }
    }
    return best;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

bool lobby(const std::string& stage) { return stage == "stagml02" || stage == "stagml11" || stage == "stagml12" || stage == "stagml13"; }

float flat(const engine::Vec3f& a, const engine::Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }

engine::Vec3f position_of(const engine::ModelCtrlManager::ModelRam& ram) {
    return {ram.node->matrix(3, 0), ram.node->matrix(3, 1), ram.node->matrix(3, 2)};
}

bool trace() {
    static const bool on = std::getenv("DW4_TRACE_CAMPAIGN") != nullptr;
    return on;
}

// The new game from the title (relative to the frame the title stage was
// requested; from the scripted new-game flow): START opens Game Start /
// Option, A goes through the card check to the partner selection (the fourth
// A shows "Select a Digimon" with Dorumon, LOADINTF2 state 4: RIGHT steps
// Dorumon -> Agumon -> Veemon -> Guilmon), the name keyboard (eight 'a'),
// "assign this name" and the difficulty, START logs in. The partner is
// Veemon (kPartnerSteps RIGHT presses; see the element walls of areas 3 and
// 4 in kObjectives).
constexpr int kPartnerSteps = 2;
struct TitlePress {
    int at;
    std::uint32_t button;
};
std::vector<TitlePress> title_presses() {
    std::vector<TitlePress> presses{{647, pad_button::start}};
    // (Each RIGHT waits for the face window's change before the next.)
    constexpr int kStepGap = 60;
    const int shift = (kPartnerSteps + 1) * kStepGap;
    for (int k = 0; k < 34; ++k) {
        if (k == 4)
            for (int step = 0; step < kPartnerSteps; ++step)
                presses.push_back({1047 + 90 * 3 + kStepGap * (step + 1), pad_button::right});
        presses.push_back({1047 + 90 * k + (k >= 4 ? shift : 0), pad_button::a});
    }
    presses.push_back({3947 + shift, pad_button::start});
    return presses;
}

} // namespace

// "<model>#<n>" names one RAM, "<model>#*" the nearest of that model that
// still has HP.
const engine::ModelCtrlManager::ModelRam* CampaignBot::find_model(engine::Engine& engine, const std::string& spec,
                                                                  const engine::Vec3f& near) const {
    const auto hash = spec.find('#');
    const std::string name = spec.substr(0, hash);
    const bool any = hash != std::string::npos && spec.compare(hash + 1, std::string::npos, "*") == 0;
    const int index = hash == std::string::npos || any ? 0 : std::atoi(spec.c_str() + hash + 1);
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!models) return nullptr;
    const engine::ModelCtrlManager::ModelRam* best = nullptr;
    for (const auto& ram : models->rams()) {
        if (!ram->generated || !ram->node || ram->info_name != name) continue;
        if (!any && ram->index == index) return ram.get();
        if (any && ram->hp > 0 && (!best || flat(position_of(*ram), near) < flat(position_of(*best), near))) best = ram.get();
    }
    return best;
}

std::optional<CampaignBot::Step> CampaignBot::next_step(engine::Engine& engine) const {
    const Objective* objective = nullptr;
    for (const auto& o : kObjectives)
        if (!holds(engine, o.done)) {
            objective = &o;
            break;
        }
    if (!objective) return std::nullopt;
    const std::string here = lower(engine.stage_name());
    if (here == objective->stage && (objective->entry < 0 || objective->entry == entry_))
        return Step{here, objective->action, -1, objective->name};
    // The cheapest way over the usable exits from here to the objective's
    // stage (entered at its point, if it names one): a stage change costs 1,
    // one into the lobby less (a cleared dungeon is left by its return gate
    // rather than walked back through). A place is a stage and the point it
    // was entered at (-1 where no exit names one).
    const auto areas = transporter_areas(engine);
    auto usable = [&](const Exit& exit, int entry) {
        if (std::string_view(exit.when) == "transporter")
            return std::find(areas.begin(), areas.end(), exit.target) != areas.end();
        return holds(engine, exit.when, entry);
    };
    using Place = std::pair<std::string, int>;
    const Place start{here, entry_};
    std::map<Place, Place> parent;
    std::map<Place, float> cost{{start, 0.0f}};
    std::set<Place> done;
    std::optional<Place> found;
    for (;;) {
        std::optional<Place> at;
        float best = 1e30f;
        for (const auto& [place, c] : cost)
            if (!done.count(place) && c < best) {
                best = c;
                at = place;
            }
        if (!at) break;
        if (at->first == objective->stage && (objective->entry < 0 || objective->entry == at->second)) {
            found = at;
            break;
        }
        done.insert(*at);
        for (const auto& exit : kExits) {
            if (at->first != exit.stage || !usable(exit, at->second)) continue;
            const Place next{exit.target, exit.entry};
            const float c = best + (std::string_view(exit.target) == "stagml11" ? 0.6f : 1.0f);
            if (const auto it = cost.find(next); it == cost.end() || c < it->second) {
                cost[next] = c;
                parent[next] = *at;
            }
        }
    }
    if (!found || *found == start) return std::nullopt;
    Place next = *found;
    while (parent.at(next) != start) next = parent.at(next);
    // Every mat of this stage leading there is a goal.
    Step step{here, "", -1, objective->name};
    std::string mats;
    for (const auto& exit : kExits)
        if (here == exit.stage && next.first == exit.target && next.second == exit.entry && usable(exit, entry_)) {
            if (!mats.empty()) mats += '|';
            mats += exit.mat;
            if (std::string_view(exit.when) == "transporter")
                step.choice = int(std::find(areas.begin(), areas.end(), exit.target) - areas.begin());
        }
    step.action = (lobby(here) ? "mat:" : "explore:") + mats;
    return step;
}

CampaignBot::Output CampaignBot::boot(engine::Engine& engine, int frame) {
    Output out;
    const int t = frame - stage_frame_;
    if (stage_ == "stagis09") {
        // The opening movie: START skips it once pad input is on (~140 frames).
        if (t >= 160 && t % 40 < 3) out.buttons |= pad_button::start;
    } else if (stage_ == "stagis10") {
        static const auto presses = title_presses();
        for (const auto& press : presses)
            if (t >= press.at && t < press.at + 3) out.buttons |= press.button;
    }
    (void)engine;
    return out;
}

bool CampaignBot::answer_menu(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const Step& step, int frame,
                              Output& out) {
    auto* selects = engine.get<engine::SelectCtrlManager>();
    if (!selects) return false;
    for (const auto& menu : selects->open_selects()) {
        if (menu.index != 0 && menu.index != 4) continue;
        if (!menu.taking_input) return true; // opening or closing: hands off
        // What this menu should answer: the area list the gate's entry, a
        // stage change prompt yes on the step's mat and no elsewhere, other
        // menus (talk choices) their first entry.
        int want = 0;
        bool cancel = false;
        const bool on_step_mat = [&] {
            if (step.action.rfind("mat:", 0) != 0) return false;
            auto* mats = engine.get<engine::MatCtrlManager>();
            if (!mats) return false;
            const auto me = position_of(player);
            const std::string wanted = "|" + step.action.substr(4) + "|";
            for (const auto& box : mats->mat_boxes())
                if (wanted.find("|" + box.name + "|") != std::string::npos && me.x >= box.min.x - 3 && me.x <= box.max.x + 3 &&
                    me.z >= box.min.z - 3 && me.z <= box.max.z + 3)
                    return true;
            return false;
        }();
        if (menu.name == "i_zzstchgcur") {
            if (step.choice >= 0 && on_step_mat) want = step.choice;
            else cancel = true;
        } else if (menu.name == "i_zzstchgwn") {
            cancel = !on_step_mat;
        } else if (step.action.rfind("evolve:", 0) == 0) {
            // The Digi-Lab (bML01004.lua, beNDMWShopCtrl20 / ShopCtrlEvolve):
            // "use the lab?" and later "equip now?" (2 lines, i_messcr), the
            // lab menu Digivolve / Equip / Exit (3 lines), the forms reached
            // (i_stcursor01: shinka row 3 first, then ascending), the two
            // confirmations (i_stmesscr, 0 = yes); once the form is the
            // wanted one, no / Exit.
            const int target = std::atoi(step.action.c_str() + 7);
            auto* sys = engine.get<engine::GameSystem>();
            const bool done = sys && sys->players[0].stats.species == target;
            if (menu.name == "i_messcr") {
                want = done ? menu.items - 1 : 0;
            } else if (menu.name == "i_stcursor01" && sys) {
                std::vector<int> forms;
                for (const auto& [species, reached] : sys->players[0].base_isuse)
                    if (reached && species >= 0 && species < 16) forms.push_back(species);
                std::stable_partition(forms.begin(), forms.end(), [](int f) { return f == 3; });
                const auto at = std::find(forms.begin(), forms.end(), target);
                want = at == forms.end() ? 0 : int(at - forms.begin());
            }
        }
        if (cancel && !menu.cancel) cancel = false; // NOCANCEL: take the first entry
        if (frame - menu_pressed_ < 14) return true;
        if (cancel) {
            out.buttons |= pad_button::b;
        } else if (menu.cursor != want) {
            const bool forward = menu.cursor < want;
            out.buttons |= menu.vertical ? (forward ? pad_button::down : pad_button::up) : (forward ? pad_button::right : pad_button::left);
        } else {
            out.buttons |= pad_button::a;
        }
        menu_pressed_ = frame;
        if (trace())
            std::printf("[%d] campaign menu %s cursor %d/%d -> %s\n", frame, menu.name.c_str(), menu.cursor, menu.items,
                        cancel ? "B" : menu.cursor != want ? "move" : "A");
        return true;
    }
    return false;
}

bool CampaignBot::walk_to(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const engine::Vec3f& goal, float close,
                          int frame, Output& out) {
    const auto me = position_of(player);
    if (flat(me, goal) <= close) return true;
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (hitland && (flat(goal, route_goal_) > 8.0f || frame - route_frame_ > 60 || route_next_ >= route_.size())) {
        const WithoutLimitCircle without_limit(*hitland);
        // Other stage change mats would open their prompts: kept away from.
        std::vector<AvoidBox> avoid;
        if (mats)
            for (const auto& box : mats->mat_boxes()) {
                const std::string script = box.name + ".lua";
                if (!engine.script_mentions(script, "funcStageChange") && !engine.script_mentions(script, "NEXTSTAGE") &&
                    !engine.script_mentions(script, "funcHealGate"))
                    continue;
                if (goal.x >= box.min.x - 4 && goal.x <= box.max.x + 4 && goal.z >= box.min.z - 4 && goal.z <= box.max.z + 4) continue;
                if (me.x >= box.min.x - 4 && me.x <= box.max.x + 4 && me.z >= box.min.z - 4 && me.z <= box.max.z + 4) continue;
                avoid.push_back({box.min.x - 4, box.min.z - 4, box.max.x + 4, box.max.z + 4, box.min.y - 5, box.max.y + 5});
            }
        const WalkMap map(hitland->world(), me, 700.0f, hitland->delivery_flags(player.info_name, player.index), avoid,
                          hitland->delivery_width(player.info_name, player.index));
        route_ = map.route_to(goal, std::max(close, 2.0f));
        route_next_ = 0;
        route_goal_ = goal;
        route_frame_ = frame;
        if (std::getenv("DW4_TRACE_CAMPAIGN") && std::string_view(std::getenv("DW4_TRACE_CAMPAIGN")) == "2")
            std::printf("[%d] campaign route (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f): %zu points, ends (%.0f,%.0f,%.0f)\n", frame, me.x, me.y,
                        me.z, goal.x, goal.y, goal.z, route_.size(), route_.empty() ? me.x : route_.back().point.x,
                        route_.empty() ? me.y : route_.back().point.y, route_.empty() ? me.z : route_.back().point.z);
    }
    while (route_next_ < route_.size() && flat(route_[route_next_].point, me) < 4.0f) ++route_next_;
    engine::Vec3f aim = goal;
    if (route_next_ < route_.size()) {
        aim = route_[route_next_].point;
        if (route_[route_next_].jump && flat(aim, me) < 14.0f && frame % 30 < 3) out.buttons |= pad_button::x;
    }
    // Stopped by a corner (walls stop, they do not slide): sidestep.
    stalled_ = flat(me, last_) < 0.05f ? stalled_ + 1 : 0;
    if (stalled_ >= 25 && frame >= escape_until_) {
        escape_until_ = frame + 20;
        escape_turn_ = (escape_turn_ + 1) % 3;
        route_frame_ = -100000;
    }
    if (frame < escape_until_) {
        constexpr float turns[3] = {1.5708f, -1.5708f, 3.1416f};
        const float heading = std::atan2(aim.x - me.x, aim.z - me.z) + turns[escape_turn_];
        aim = {me.x + std::sin(heading) * 10.0f, me.y, me.z + std::cos(heading) * 10.0f};
    }
    out.walk = true;
    out.x = aim.x;
    out.z = aim.z;
    out.reach = 1.0f;
    return false;
}

CampaignBot::Output CampaignBot::update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame) {
    Output out;
    const std::string stage = lower(engine.stage_name());
    if (stage != stage_) {
        stage_ = stage;
        entry_ = -1;
        stage_frame_ = frame;
        route_.clear();
        route_frame_ = -100000;
        waited_ = 0;
        no_control_ = 0;
        stuck_since_ = -1;
        // Settings the ExploreBot reads at its stage start.
        for (const auto& setting : kStageSettings) {
            const bool here = stage == setting.stage;
#ifdef _WIN32
            _putenv_s(setting.name, here ? setting.value : "");
#else
            if (here) setenv(setting.name, setting.value, 1);
            else unsetenv(setting.name);
#endif
        }
    }
    if (entry_ < 0) {
        // The player point this stage was entered at (PLAYERPOINT
        // GETGENERATEPOS, what first.lua's PLAYERgetPos reads), asked once
        // the stage's PLAYERPOINT row answers (not yet on the frame the
        // stage changes); an exit's SETGENERATEPOS later is for the next.
        engine::Packet packet;
        engine.messenger().send(engine, "PLAYERPOINT", std::nullopt, -1, "GETGENERATEPOS", packet);
        if (!packet.ints.empty()) entry_ = packet.ints[0];
    }
    if (stage.rfind("stagis", 0) == 0 || !player || !player->node) return boot(engine, frame);
    // The character on arriving in a stage (level, techniques' elements,
    // the attack's element: what element-locked walls need).
    if (frame == stage_frame_ + 104 && trace())
        if (auto* sys = engine.get<engine::GameSystem>()) {
            const auto& slot = sys->players[0];
            std::string techs;
            if (auto* db = engine.get<engine::DatabaseManager>())
                for (const int row : slot.wazalist) techs += " " + db->text("pcTech", row, "ZOKUSEI");
            std::printf("[%d] campaign player %s level %d hp %d bits %d attack element %d techniques:%s\n", frame, slot.digimon.c_str(),
                        slot.stats.level, slot.max_hp, slot.bits, slot.stats.attack_zokusei, techs.c_str());
            if (auto* db = engine.get<engine::DatabaseManager>(); db && std::getenv("DW4_TRACE_CAMPAIGN") &&
                                                                  std::string_view(std::getenv("DW4_TRACE_CAMPAIGN")) == "2")
                for (int n = 1; n <= 6; ++n) {
                    const std::string tech = "TECH" + std::to_string(n);
                    const int base = db->number("shinka", slot.stats.species, tech).value_or(-1);
                    std::printf("      %s %s level %d element %s\n", tech.c_str(), db->text("pcTech", base, "NAME").c_str(),
                                db->number("shinka", slot.stats.species, tech + "_LEVEL").value_or(-1),
                                db->text("pcTech", base, "ZOKUSEI").c_str());
                }
            // (and the device folder: what could be equipped for an element)
            if (auto* db = engine.get<engine::DatabaseManager>(); db && std::getenv("DW4_TRACE_CAMPAIGN") &&
                                                                  std::string_view(std::getenv("DW4_TRACE_CAMPAIGN")) == "2") {
                static const char* tables[4] = {"weapon", "armor1", "armor2", "abChip"};
                for (std::size_t i = 0; i < slot.devices.size(); ++i) {
                    const auto& d = slot.devices[i];
                    const char* table = tables[std::clamp(d.type, 0, 3)];
                    std::printf("      device %zu %s #%d %s rank %d%s%s\n", i, table, d.record, db->text(table, d.record, "NAME").c_str(), d.rank,
                                d.type == 3 ? (" element " + db->text("abChip", d.record, "ZOKUSEI")).c_str() : "", d.flag ? " equipped" : "");
                }
            }
        }
    const auto step = next_step(engine);
    const std::string key = step ? step->stage + " " + step->action + " (" + step->why + ")" : "no step";
    if (key != traced_) {
        std::printf("[%d] campaign: %s\n", frame, key.c_str());
        traced_ = key;
        traced_at_ = frame;
    } else if ((frame - traced_at_) % 10000 == 0) {
        // A long step: the place in the log where a run stalls.
        std::printf("[%d] campaign: still %s for %d frames at (%.0f,%.0f,%.0f)\n", frame, key.c_str(), frame - traced_at_,
                    player->node->matrix(3, 0), player->node->matrix(3, 1), player->node->matrix(3, 2));
    }
    const auto me = position_of(*player);
    const bool controllable = player->control_enabled && player->seq_name.rfind("SQ_DEMO", 0) != 0;
    // DW4_TRACE_CAMPAIGN=2: the models around the player every 300 frames.
    if (frame % 300 == 0 && std::getenv("DW4_TRACE_CAMPAIGN") && std::string_view(std::getenv("DW4_TRACE_CAMPAIGN")) == "2")
        if (auto* models = engine.get<engine::ModelCtrlManager>()) {
            std::printf("[%d] campaign at (%.0f,%.0f,%.0f) %s ctrl=%d\n", frame, me.x, me.y, me.z, player->seq_name.c_str(), controllable);
            for (const auto& e : models->rams())
                if (e->generated && e->node && !e->is_player && flat(position_of(*e), me) < 150.0f && e->info_name.rfind("i_", 0) != 0)
                    std::printf("      %s#%d %s %s hp=%d ctrl=%d mode=%d (%.0f,%.0f,%.0f)%s%s%s\n", e->info_name.c_str(), e->index,
                                e->category.c_str(), e->seq_name.c_str(), e->hp, e->control_enabled, e->ctrl_mode, position_of(*e).x,
                                position_of(*e).y, position_of(*e).z, e->hit && e->hit->player_bullet ? " bullet" : "",
                                e->hit && e->hit->player_talk ? " talk" : "", e->ai_maps && !e->ai_maps->empty() ? " aim" : "");
            for (const auto& e : models->rams())
                if (e->generated && e->node && e->hit && !e->is_player && flat(position_of(*e), me) < 60.0f)
                    for (const auto& [lo, hi] : e->hit->boxes)
                        std::printf("      %s#%d box (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n", e->info_name.c_str(), e->index, lo.x, lo.y, lo.z,
                                    hi.x, hi.y, hi.z);
        }
    no_control_ = controllable ? 0 : no_control_ + 1;
    if (!step) {
        // No objective left, or no way to its stage: messages only.
        if (stuck_since_ < 0) stuck_since_ = frame;
        if (no_control_ > 60 && no_control_ % 45 < 3) out.buttons |= pad_button::a;
        last_ = me;
        return out;
    }
    // A key item lying on the ground (a boss drop, g_000key; key items are
    // story progress) is picked up before anything else: the ExploreBot
    // takes an exit as soon as it opens, and the boss of a dungeon floor
    // does not come back (dng101b2 matg08.lua: flag 119) to drop it again.
    const engine::ModelCtrlManager::ModelRam* key_item = nullptr;
    if (auto* models = engine.get<engine::ModelCtrlManager>(); models && !lobby(stage))
        for (const auto& item : models->rams())
            if (item->generated && item->node && item->category == "ITEM" && item->item_table == 2) key_item = item.get();
    if (!key_item) key_since_ = -1;
    if (key_item && controllable && frame >= key_skip_until_) {
        if (key_since_ < 0) key_since_ = frame;
        if (frame - key_since_ > 1800) {
            // Not reached in half a minute: the ExploreBot's way first.
            key_skip_until_ = frame + 3000;
            key_since_ = -1;
        } else {
            if (walk_to(engine, *player, position_of(*key_item), 2.5f, frame, out) && frame % 20 < 3) out.buttons |= pad_button::a;
            last_ = me;
            return out;
        }
    }
    // Targets of a ride: faced and struck while they are in range.
    if (controllable)
        for (const auto& ride : kRideTargets) {
            if (stage != ride.stage) continue;
            auto* models = engine.get<engine::ModelCtrlManager>();
            if (!models) break;
            const std::string prefixes = std::string("|") + ride.prefixes + "|";
            const std::string avoid = std::string("|") + ride.avoid + "|";
            const engine::ModelCtrlManager::ModelRam* best = nullptr;
            bool hold = false; // an unwanted target within reach
            for (const auto& e : models->rams()) {
                if (!e->generated || !e->node || e->hp <= 0) continue;
                if (e->seq_name == "SQ_SWON" || e->seq_name == "SQ_SWOF" || e->seq_name == "SQ_POWEROF") continue;
                const bool listed = listed_in(prefixes, *e);
                if (!listed && ride.strike > 0.0f && listed_in(avoid, *e) && flat(nearest_hit_point(*e, me), me) <= ride.strike + 6.0f)
                    hold = true;
                if (listed && flat(position_of(*e), me) < ride.range && (!best || flat(position_of(*e), me) < flat(position_of(*best), me)))
                    best = e.get();
            }
            // Enemies boarding or next to the ride are struck down; shots
            // and throws on the way are guarded against (on the boat every
            // hit on a player costs the boat HP, stag0103 hit_p_*.lua; the
            // guard (B, SQ_GUARD) takes none).
            const engine::ModelCtrlManager::ModelRam* foe = nullptr;
            bool threat = false, incoming = false;
            for (const auto& e : models->rams()) {
                if (!e->generated || !e->node || e->is_player) continue;
                const float d = flat(position_of(*e), me);
                if (e->category == "EBULLET" && d < ride.guard) threat = true;
                if (e->category == "EBULLET" && d < 18.0f) incoming = true;
                if (e->category == "ENEMY" && e->hp > 0 && engine.has_script("hit_" + e->info_name + ".lua")) {
                    if (d < ride.fight && (!foe || d < flat(position_of(*foe), me))) foe = e.get();
                    if (d < ride.guard && e->seq_name.rfind("SQ_FIRE", 0) == 0) threat = true;
                }
            }
            // A shot about to arrive is guarded against; otherwise close
            // enemies first (their touch hurts too), then the targets, else
            // the guard while something is aimed at the ride. (A sign of the
            // train comes first: it passes once, and a fight would knock the
            // player off the edge it is struck from.)
            if (foe && (ride.strike <= 0.0f || !best)) best = foe;
            if (!best && !threat) break;
            const bool swing_now = best && best != foe && ride.strike > 0.0f && flat(nearest_hit_point(*best, me), me) <= ride.strike;
            if (best && (!incoming || swing_now)) {
                // Toward the nearest point of its hit box (on a ride: to the
                // ride's edge facing it), swinging once it is within reach.
                const auto at = ride.strike > 0.0f && best != foe ? nearest_hit_point(*best, me) : position_of(*best);
                out.walk = true;
                out.x = at.x;
                out.z = at.z;
                out.reach = 4.0f;
                if (frame % 12 < 3 && !hold && (ride.strike <= 0.0f || best == foe || flat(at, me) <= ride.strike))
                    out.buttons |= pad_button::a;
            } else {
                // (B is read on its press: pulsed so the guard is taken again.)
                if (frame % 8 < 4) out.buttons |= pad_button::b;
            }
            last_ = me;
            return out;
        }
    // Fields and dungeons: the ExploreBot plays them (it answers its own
    // prompts and text windows).
    if (step->action.rfind("explore:", 0) == 0) {
        const auto d = explorer_.update(engine, *player, step->action.substr(8), frame);
        out.walk = controllable;
        out.x = d.x;
        out.z = d.z;
        out.reach = d.reach;
        if (d.attack) {
            if (d.jump_attack) {
                if (frame % 40 < 3) out.buttons |= pad_button::x;
                else if (frame % 40 >= 10 && frame % 40 < 13) out.buttons |= pad_button::a;
            } else if (frame % (d.fast_attack ? 10 : 20) < 3) {
                out.buttons |= pad_button::a;
            }
        }
        if ((d.confirm && frame % 30 < 3) || (d.pick_up && frame % 20 < 3) || d.press_a) out.buttons |= pad_button::a;
        if (d.press_b) out.buttons |= pad_button::b;
        if (d.jump) out.buttons |= pad_button::x;
        if (d.press_r) out.buttons |= pad_button::r;
        last_ = me;
        return out;
    }
    if (answer_menu(engine, *player, *step, frame, out)) {
        last_ = me;
        return out;
    }
    if (!controllable) {
        // Demos and text windows: A moves the messages on.
        if (no_control_ > 60 && no_control_ % 45 < 3) out.buttons |= pad_button::a;
        last_ = me;
        return out;
    }
    // Outside the lobby, live enemies near the player are fought first (a
    // gimmick guarded by cannons cannot be reached while they knock the
    // player back).
    if (!lobby(stage))
        if (auto* models = engine.get<engine::ModelCtrlManager>()) {
            const engine::ModelCtrlManager::ModelRam* foe = nullptr;
            for (const auto& e : models->rams())
                if (e->generated && e->node && e->category == "ENEMY" && e->hp > 0 && engine.has_script("hit_" + e->info_name + ".lua") &&
                    flat(position_of(*e), me) < 45.0f && std::abs(position_of(*e).y - me.y) < 20.0f &&
                    (!foe || flat(position_of(*e), me) < flat(position_of(*foe), me)))
                    foe = e.get();
            if (foe) {
                const auto at = position_of(*foe);
                if (walk_to(engine, *player, at, 7.0f, frame, out)) {
                    out.walk = true;
                    out.x = at.x;
                    out.z = at.z;
                    out.reach = 4.0f;
                }
                if (flat(at, me) < 12.0f && frame % 20 < 3) out.buttons |= pad_button::a;
                last_ = me;
                return out;
            }
        }
    const auto colon = step->action.find(':');
    const std::string kind = step->action.substr(0, colon);
    const std::string what = colon == std::string::npos ? std::string() : step->action.substr(colon + 1);
    if (kind == "mat") {
        // Onto the mat (the nearest reached floor inside it), then stand.
        auto* mats = engine.get<engine::MatCtrlManager>();
        std::optional<engine::Vec3f> goal;
        const std::string wanted = "|" + what + "|";
        if (mats)
            for (const auto& box : mats->mat_boxes())
                if (wanted.find("|" + box.name + "|") != std::string::npos) {
                    const engine::Vec3f centre{(box.min.x + box.max.x) * 0.5f, me.y, (box.min.z + box.max.z) * 0.5f};
                    if (!goal || flat(centre, me) < flat(*goal, me)) goal = centre;
                }
        if (goal) walk_to(engine, *player, *goal, 1.5f, frame, out);
    } else if (kind == "talk" || kind == "strike" || kind == "evolve") {
        if (const auto* model = find_model(engine, kind == "evolve" ? "n_digilff#1" : what, me)) {
            // The player's hit or talk check has to meet the model's hit box,
            // which may lie well in front of the model (a console in a wall
            // niche): walked up to its middle, then A facing the model.
            const auto origin = position_of(*model);
            engine::Vec3f at = origin;
            float close = 9.0f;
            if (model->hit && !model->hit->boxes.empty()) {
                const auto& [lo, hi] = model->hit->boxes.front();
                const auto& m = model->node->matrix;
                const engine::Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
                at = {c.x * m(0, 0) + c.y * m(1, 0) + c.z * m(2, 0) + m(3, 0), origin.y, c.x * m(0, 2) + c.y * m(1, 2) + c.z * m(2, 2) + m(3, 2)};
                close = std::min(hi.x - lo.x, hi.z - lo.z) * 0.5f + 7.0f; // the check reaches ~8 ahead
            }
            if (walk_to(engine, *player, at, close, frame, out)) {
                // Face it (the stick toward it), then A.
                out.walk = true;
                out.x = origin.x;
                out.z = origin.z;
                // (A step or two toward the model turns the player to it.)
                const float stop = std::max(2.0f, flat(at, origin) - 2.0f);
                out.reach = stop * stop;
                if (frame % 20 < 3) out.buttons |= pad_button::a;
            }
        }
    } else if (kind == "step") {
        if (const auto* model = find_model(engine, what, me)) walk_to(engine, *player, position_of(*model), 1.0f, frame, out);
    } else if (kind == "wait") {
        ++waited_;
    }
    last_ = me;
    return out;
}

} // namespace dw4::game
