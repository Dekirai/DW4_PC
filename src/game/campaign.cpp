#include "campaign.hpp"

#include "dw4/pad.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// more), G (the god mode test cheat is on); '!' negates an atom, '&' binds
// tighter than '|'; no parentheses.
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

// A hash of the story flags that are set (a maze place swept under one
// state is worth a new visit once a button or a wave changed it).
std::size_t flags_hash(engine::Engine& engine) {
    std::size_t h = 1469598103934665603ull;
    for (const auto& [flag, value] : engine.flags())
        if (value) h = (h ^ std::size_t(flag)) * 1099511628211ull;
    return h;
}

// `entry`: the player point the stage holding the condition was (or will be)
// entered at, -1 unknown (E atoms are false then).
bool atom(engine::Engine& engine, std::string_view text, int entry) {
    bool negate = false;
    while (!text.empty() && text.front() == '!') {
        negate = !negate;
        text.remove_prefix(1);
    }
    // G: the god mode test cheat is on (the survival steps are skipped).
    if (text == "G") return engine.cheats().god != negate;
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
    // On a maze floor (kMazeRegions): the stage counts when the region
    // entered reaches this region over the open links.
    const char* region = nullptr;
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
    // The dng310 fortress (kMazeRegions): Lucemon's gate (dng310f3 matz06)
    // opens once both waves of the centre are cleared (gene10.lua /
    // gene11.lua: flags 158 and 159, g_bossgate). The centre lies between
    // the barriers of the heal room; each barrier is switched off by the
    // wall button on the far side of the other (hit_g_dwbutton1.lua: #0 in
    // the east part, flag 186, opens the west barrier; #1 the east one). So
    // the east part of the heal room is reached first (behind the doors of
    // button 0, reached from player point 4 only, and button 1, and the
    // wave of matg04), wall button 0 struck, then the way in from the west
    // (the wave of matg07, buttons 3 and 2). The buttons and waves on the
    // way to those floor parts lie on dng310f1 and dng310f2, in this order.
    // "explore:none": the ExploreBot plays all of the reached floor (no mat
    // has that name) until the objective's flag is set.
    {"dng310f1 wave 1 (flag 100)", "F100|F69|P6", "dng310f1", "explore:none", -1, "start"},
    {"dng310f1 wave 3 (flag 102)", "F102|F69|P6", "dng310f1", "explore:none", -1, "hall"},
    {"dng310f2 button 3 (flag 147)", "F147|F69|P6", "dng310f2", "explore:none", -1, "e4"},
    {"dng310f3 button 2 (flag 182)", "F182|F69|P6", "dng310f3", "explore:none", -1, "b2"},
    {"dng310f2 button 0 (flag 144)", "F144|F69|P6", "dng310f2", "explore:none", -1, "e7"},
    {"dng310f2 button 1 (flag 145)", "F145|F69|P6", "dng310f2", "explore:none", -1, "e2"},
    {"dng310f3 button 0 (flag 180)", "F180|F69|P6", "dng310f3", "explore:none", -1, "b0"},
    {"dng310f1 wave 4 (flag 103)", "F103|F69|P6", "dng310f1", "explore:none", -1, "gene04"},
    {"dng310f2 button 2 (flag 146)", "F146|F69|P6", "dng310f2", "explore:none", -1, "e0"},
    {"dng310f2 waves 3 and 4 (flags 133, 134)", "F133&F134|F69|P6", "dng310f2", "explore:none", -1, "south"},
    {"dng310f3 wave 4 (flag 152)", "F152|F69|P6", "dng310f3", "explore:none", -1, "east"},
    {"dng310f3 button 1 (flag 181)", "F181|F69|P6", "dng310f3", "explore:none", -1, "x160"},
    {"dng310f3 wall button 0 (flag 186)", "F186|F69|P6", "dng310f3", "explore:none", -1, "eastheal"},
    {"dng310f3 wave 7 (flag 155)", "F155|F69|P6", "dng310f3", "explore:none", -1, "west"},
    {"dng310f3 button 3 (flag 183)", "F183|F69|P6", "dng310f3", "explore:none", -1, "x280"},
    {"dng310f3 centre waves (flags 158, 159)", "F158&F159|F69|P6", "dng310f3", "explore:none", -1, "centre"},
    {"Lucemon (stagBS03)", "F69|P6", "stagbs03", "explore:matz01"},
    {"report to Ophanimon (phase 6)", "P6", "stagml02", "mat:matf00"},
    // Phase 6. Area 4: key items 9 (dng411b3) and 10 (dng412b4) let the
    // starship in stag0401 (mate00) fly to stag0403; stag0404 leads to the
    // last three boss stages and the staff roll (stagBS06 matz01).
    {"digivolve back to Veemon (thunder for the scrap walls)", "C1|K10|P7|!S1", "stagml12", "evolve:1"},
    {"key item 9 (dng411b3 boss)", "K9|P7", "dng411b3", "explore:matz02"},
    {"key item 10 (dng412b4 boss)", "K10|P7", "dng412b4", "explore:matz02"},
    {"Daemon (stagBS06)", "F86|F70", "stagbs06", "explore:matz01"},
    // The return gate of stagBS06 leads to the staff roll (stagIS20
    // first.lua: j_endroll, then stagML11), whose start.lua turns flag 70
    // (hit_f_devl3.lua, Daemon down) into 86, the cleared game.
    {"staff roll and the cleared game (flag 86)", "F86", "stagis20", "wait"},
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
    {"stag0304", "mate00", "stag0303", ""},     {"stag0304", "matz00", "dng310f1", "", 0},
    {"dng307b1", "matz01", "stag0301", ""},     {"dng307b1", "matz02", "dng307b2", ""},
    {"dng307b2", "matz01", "dng307b1", ""},     {"dng307b2", "matz02", "stagml11", ""},
    {"dng308b1", "matz01", "stag0302", ""},     {"dng308b1", "matz02", "dng308b2", ""},
    {"dng308b2", "matz01", "dng308b1", ""},     {"dng308b2", "matz02", "dng308b3", ""},
    {"dng308b3", "matz01", "dng308b2", ""},     {"dng308b3", "matz02", "stagml11", ""},
    {"dng309b1", "matz01", "stag0301", ""},     {"dng309b1", "matz02", "dng309b2", ""},
    {"dng309b2", "matz01", "dng309b1", ""},     {"dng309b2", "matz02", "dng309b3", ""},
    {"dng309b3", "matz01", "dng309b2", ""},     {"dng309b3", "matz02", "dng309b4", ""},
    {"dng309b4", "matz01", "dng309b3", ""},     {"dng309b4", "matz02", "stagml11", ""},
    // The dng310 maze (Lucemon's fortress): three floors of small regions
    // joined by doors that open to buttons and cleared waves; every link
    // names the player point it leads to (funcStageChange), see kMazes.
    {"dng310f1", "matz01", "stag0304", "", 2},
    {"dng310f1", "matz02", "dng310f2", "", 7},  {"dng310f1", "matz03", "dng310f2", "", 8},
    {"dng310f1", "matz04", "dng310f2", "", 4},  {"dng310f1", "matz05", "dng310f2", "", 0},
    {"dng310f1", "matz06", "dng310f2", "", 1},  {"dng310f1", "matz07", "dng310f2", "", 3},
    {"dng310f1", "matz08", "dng310f2", "", 6},
    {"dng310f2", "matz01", "dng310f1", "", 4},  {"dng310f2", "matz02", "dng310f1", "", 5},
    {"dng310f2", "matz04", "dng310f1", "", 6},  {"dng310f2", "matz05", "dng310f1", "", 3},
    {"dng310f2", "matz07", "dng310f1", "", 7},  {"dng310f2", "matz08", "dng310f1", "", 1},
    {"dng310f2", "matz09", "dng310f1", "", 2},
    {"dng310f2", "matz03", "dng310f3", "", 4},  {"dng310f2", "matz06", "dng310f3", "", 3},
    {"dng310f2", "matz10", "dng310f3", "", 0},  {"dng310f2", "matz11", "dng310f3", "", 2},
    {"dng310f2", "matz12", "dng310f3", "", 1},
    {"dng310f3", "matz01", "dng310f2", "", 9},  {"dng310f3", "matz02", "dng310f2", "", 11},
    {"dng310f3", "matz03", "dng310f2", "", 10}, {"dng310f3", "matz04", "dng310f2", "", 5},
    {"dng310f3", "matz05", "dng310f2", "", 2},  {"dng310f3", "matz06", "stagbs03", "F158&F159"},
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

// Maze floors: their regions open with doors (buttons, cleared waves) that
// often lie in other regions or on other floors (dng310f3 matz06 needs the
// boss gate, flags 158/159 from the waves of the centre, and a way in
// through barrier 0 or 1, whose wall buttons stand on the far side of each
// other). The way is planned over the floors' regions (kMazeRegions); as a
// fallback, once the ExploreBot has nothing left to explore and the goal of
// a travel step is out of reach, another exit it can reach is taken, the one
// into the place (floor, player point) visited least, so the floors are
// swept like a player would until the doors line up.
constexpr const char* kMazes[] = {"dng310f1", "dng310f2", "dng310f3"};

// The regions of the maze floors with the switchable walls closed (from the
// campaign driver's research report, DW4_CAMPAIGN_REGIONS: each region the
// floor reached on foot), the player points arriving in each and the exit
// mats in it, and the doors and barriers joining them with the flag that
// opens each (the hit_g_dbutton1.lua / hit_g_dwbutton1.lua button flags and
// the gene*.lua wave flags; start.lua opens them again from the flags).
// Dead-end rooms are left out. An exit of a maze floor is usable from a
// place (floor, player point) when its mat's region is reached from the
// point's region over the links whose condition holds.
struct MazeRegion {
    const char* stage;
    const char* name;
    const char* points; // '|'-separated player points arriving here
    const char* mats;   // '|'-separated exit mats in it
};
constexpr MazeRegion kMazeRegions[] = {
    {"dng310f1", "start", "0", "matz01"},     {"dng310f1", "hall", "1|2", "matz02|matz03"},
    {"dng310f1", "west", "3", "matz04"},      {"dng310f1", "gene04", "5", "matz06"},
    {"dng310f1", "gene05", "6", "matz07"},    {"dng310f1", "east", "7", "matz08"},
    {"dng310f1", "south", "4", "matz05"},
    {"dng310f2", "e0", "0", "matz01"},        {"dng310f2", "e1", "1", "matz02"},
    {"dng310f2", "e2", "2|3", "matz03|matz04"}, {"dng310f2", "e4", "4", "matz05"},
    {"dng310f2", "e5", "5", "matz06"},        {"dng310f2", "e6", "6|9", "matz07|matz10"},
    {"dng310f2", "e7", "7", "matz08"},        {"dng310f2", "e8", "8", "matz09"},
    {"dng310f2", "strip", "", ""},            {"dng310f2", "south", "", ""},
    {"dng310f2", "north", "10|11", "matz11|matz12"},
    {"dng310f3", "east", "0|1", "matz01|matz02"}, {"dng310f3", "west", "2", "matz03"},
    {"dng310f3", "b2", "3", "matz04"},        {"dng310f3", "b0", "4", "matz05"},
    {"dng310f3", "b4", "", ""},               {"dng310f3", "x160", "", ""},
    {"dng310f3", "x200", "", ""},             {"dng310f3", "eastheal", "", ""},
    {"dng310f3", "centre", "", "matz06"},     {"dng310f3", "westheal", "", ""},
    {"dng310f3", "x240", "", ""},             {"dng310f3", "x280", "", ""},
};
struct MazeLink {
    const char* stage;
    const char* a;
    const char* b;
    const char* when;
    bool oneway = false; // only from a to b (a ledge walked off)
};
constexpr MazeLink kMazeLinks[] = {
    // dng310f1: the doors of the waves of matg01 (doorwall01/02), matg03
    // (doorwall04), matg04 (doorwall05) and matg05 (doorwall06).
    {"dng310f1", "start", "hall", "F100"},
    {"dng310f1", "hall", "west", "F102"},
    {"dng310f1", "hall", "gene04", "F103"},
    {"dng310f1", "hall", "gene05", "F104"},
    // dng310f2: button 3 (barrierwall05, doorwall01 to the matz06 room),
    // button 2 (barrierwall06, doorwall02 to the south), the waves of matg03
    // (doorwall03 to the north) and matg04 (doorwall04/05), matg06
    // (doorwall06); the barriers of the room of buttons 0 (barrierwall03)
    // and 1 (barrierwall02); the ledge from the matt05 corridor (safewall0
    // is off while the players stand on matt05, matt05.lua).
    {"dng310f2", "e4", "strip", "F147"},
    {"dng310f2", "strip", "e5", "F147"},
    {"dng310f2", "e0", "strip", "F146"},
    {"dng310f2", "strip", "south", "F146"},
    {"dng310f2", "south", "north", "F133"},
    {"dng310f2", "south", "e8", "F134"},
    {"dng310f2", "south", "e6", "F134"},
    {"dng310f2", "e8", "e6", "F136"},
    {"dng310f2", "e1", "e2", "F144"},
    {"dng310f2", "e7", "e0", "F145"},
    {"dng310f2", "e7", "e1", "", true},
    // dng310f3: the wave of matg04 (doorwall06), buttons 0 (doorwall01),
    // 1 (doorwall02), 2 (doorwall03), 3 (doorwall04), 5 (doorwall05), the
    // wave of matg07 (doorwall07), the heal room barriers (barrierwall01 by
    // wall button 1, barrierwall02 by wall button 0), barrier 3 (start.lua
    // keeps barrierwall03 off only with flag 183) and the ledge from the
    // east part of the heal room down to button 0 (safewall1, matt06.lua).
    {"dng310f3", "east", "x160", "F152"},
    {"dng310f3", "x160", "x200", "F180"},
    {"dng310f3", "x200", "eastheal", "F181"},
    {"dng310f3", "eastheal", "centre", "F187"},
    {"dng310f3", "westheal", "centre", "F186"},
    {"dng310f3", "westheal", "x240", "F183"},
    {"dng310f3", "x240", "x280", "F182"},
    {"dng310f3", "x280", "west", "F155"},
    {"dng310f3", "b2", "b4", "F185"},
    {"dng310f3", "b4", "west", "F183"},
    {"dng310f3", "eastheal", "b0", "", true},
};

bool in_list(std::string_view list, std::string_view item) {
    for (std::size_t at = 0; at <= list.size();) {
        const auto bar = std::min(list.find('|', at), list.size());
        if (list.substr(at, bar - at) == item) return true;
        at = bar + 1;
    }
    return false;
}

bool maze_floor(std::string_view stage) {
    return std::any_of(std::begin(kMazeRegions), std::end(kMazeRegions), [&](const MazeRegion& r) { return stage == r.stage; });
}

// The region a player point arrives in (nullptr: unknown).
const char* point_region(std::string_view stage, int point) {
    if (point < 0) return nullptr;
    for (const auto& r : kMazeRegions)
        if (stage == r.stage && in_list(r.points, std::to_string(point))) return r.name;
    return nullptr;
}

const char* mat_region(std::string_view stage, std::string_view mat) {
    for (const auto& r : kMazeRegions)
        if (stage == r.stage && in_list(r.mats, mat)) return r.name;
    return nullptr;
}

// The regions reached from `from` over the links open now.
std::set<std::string> maze_reach(engine::Engine& engine, std::string_view stage, const char* from) {
    std::set<std::string> reached{from};
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& link : kMazeLinks) {
            if (stage != link.stage || !holds(engine, link.when)) continue;
            const bool a = reached.count(link.a) > 0, b = reached.count(link.b) > 0;
            if (a && !b) grew = reached.insert(link.b).second || grew;
            else if (b && !a && !link.oneway) grew = reached.insert(link.a).second || grew;
        }
    }
    return reached;
}

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
// boxes reach 20 ahead of the player (kTrainSwing), while a gate sign's
// 40x40 box stays 7.5 off the car's edge: the player moves to the car's
// edge on the sign's side early and swings ahead of time (swing_plan) when
// a press meets the sign well; `strike` > 0 marks such targets (and is the
// reach for `avoid`). A sign of `avoid` that is not wanted (the other way at
// a junction) within reach, or met by a press while the ride carries the
// player on, holds the swing back, also at enemies.
// `goal` is the exit mat the targets lead to: the ride's other exits need
// them left alone (stag0103 matf00 -> stag0102 is the bomb itself; the
// stag0203 train runs straight to matz00-03 -> stag0201).
// `minus_x` / `plus_x` name targets that pass on the ridden model's -X / +X
// side (its node's row 0; the stag0203 car g_soukouz02 is some 55 units
// wide, measured at each sign's pass): the player has to be at that edge of
// the car when the target comes (the track turns before gate sign 1, which
// comes up ahead-left and passes on the right: a side chosen from the way
// ahead would be the wrong one, and the car is too wide to cross in the
// last moments while the mechas swarm the player).
// `timing` times the targets by the ride's own clock (ride_schedule): the
// ride runs a recorded path, so "<target>@<sequence>+<frames>" says that the
// target comes abreast of the ridden model's origin that many frames after
// the `clock` model (the stag0203 train r_trainz02) starts that sequence (its
// stretch of track; measured, the same in every run). The time until a sign
// is known long before it is in reach and also where the track turns before
// it (gate sign 0), where the distance to it says little.
struct RideTargets {
    const char* stage;
    const char* goal;
    const char* prefixes; // '|'-separated model name prefixes
    float range, fight, guard;
    float strike = 0.0f;     // > 0: signs swung at ahead of time (swing_plan)
    const char* avoid = "";  // '|'-separated prefixes of models not to hit
    const char* minus_x = ""; // '|'-separated targets passing on the ridden model's -X side
    const char* plus_x = "";  // ... and on its +X side
    const char* clock = "";   // the model whose sequences time the ride
    const char* timing = "";  // '|'-separated "<target>@<clock sequence>+<frames>"
};
constexpr RideTargets kRideTargets[] = {
    {"stag0103", "matz00", "g_facttowerz06|g_shelfz06", 70.0f, 20.0f, 40.0f},
    {"stag0203", "matz04", "g_turnright#0|g_turnright#4|g_turnleft#2|g_gatesign#0|g_gatesign#1", 520.0f, 20.0f, 0.0f, 16.0f,
     "g_turn|g_gatesign", "g_turnright#0|g_turnright#4|g_gatesign#0|g_gatesign#1", "g_turnleft#2", "r_trainz02",
     "g_turnright#0@SQ_RAIL00+1008|g_turnright#4@SQ_RAILDD+1321|g_turnleft#2@SQ_RAILEE+585|g_gatesign#0@SQ_RAILII+2371|"
     "g_gatesign#1@SQ_RAILJJ+917"},
};

// Boss fights the ExploreBot's way of fighting (walk at the nearest enemy,
// swing) does not win: the boss opens short windows in which a part of it
// can take a hit, and its attacks knock the player down for most of such a
// window (SQ_LDMG, then SQ_STUN). In these stages the driver fights: the
// listed models (`strike`, prefixes or "<model>#<n>", see listed_in) are
// struck while they can take a hit (alive, no MUTEKI, their hit body within
// the player's reach in height); meanwhile the player keeps `keep` from the
// boss (`follow`), and the guard (B, SQ_GUARD: the stages' hit_p_*.lua take
// no damage then) is held while an enemy attack box (kind / 10000 7 or 8)
// is about to meet the player. Once the boss is down the ExploreBot takes
// the player to the return gate.
struct BossFight {
    const char* stage;
    const char* strike;
    const char* follow;
    float keep;
    bool real_blows = false; // only without the one-hit cheat (it fells such a boss with a blow)
};
constexpr BossFight kBossFights[] = {
    // stagBS03: Lucemon (f_luce) takes damage only on the ground
    // (hit_f_luce.lua: g_Ground, set by gr2_f_luce.lua "GROUND" in its
    // SQ_SWITCH3), which the shield ball f_luce2 starts when it breaks (its
    // SQ_EXIT calls cha_f_luce2.lua). The shield can be hit only while it is
    // soft: after a landing sof_f_luce.lua "ON" switches it to its SOFT
    // label, which leaves out the MUTEKI of the NORM one.
    {"stagbs03", "f_luce2|f_luce", "f_luce", 22.0f},
    // dng102b3: Mammothmon (e_mammos) charges and tramples (x_007erunn02a,
    // its body box): the ExploreBot's walk-and-swing landed 4 blows in 9000
    // frames of a no-cheat run at level 11, its guard held off by the blows.
    {"dng102b3", "e_mammos", "e_mammos", 30.0f, true},
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

// The player's swing on the stag0203 train (w_sp0203 SQ_ATCK1, kind 10000):
// boxes 6x8x6 (authored extents, 80307EAC) at (side, ahead) of the player's
// node, checked from `from` up to (not including) `to` frames into the
// attack (MODEL_HITBOX delay / end: 803107E0 -> 80310754 starts a box once
// its time reaches the delay, 803102F4 drops it unchecked once it reaches
// the end).
struct SwingBox {
    float side, ahead, from, to;
};
constexpr SwingBox kTrainSwing[] = {{0.0f, 4.0f, 13.0f, 14.0f}, {0.0f, 9.0f, 14.0f, 19.0f}, {-4.0f, 13.0f, 15.0f, 21.0f},
                                    {4.0f, 13.0f, 15.0f, 21.0f}, {0.0f, 17.0f, 15.0f, 21.0f}};
constexpr float kSwingHalf = 3.0f;
// Frames from an A press to the attack's first frame (read on the press,
// the sequence starts on the next update but one).
constexpr int kSwingLatency = 2;
// An attack's boxes for swing_plan: the boxes, their half extent and the
// frames from the input to the attack's first frame.
struct SwingShape {
    const SwingBox* boxes;
    std::size_t count;
    float half;
    int latency;
};
constexpr SwingShape kSlash{kTrainSwing, std::size(kTrainSwing), kSwingHalf, kSwingLatency};
// The Slam Attack (SQ_ATCK3, kind 50000: 'PCBA', bullet_name 804DD2A0):
// with B held, a direction tapped twice within 15 frames (the slam taps of
// 8034C770, ModelCtrlManager::update_player). One 15x15x15 box 4 ahead of
// the player, checked from the sequence's first frame on for 16 frames
// while the player lunges forward (stopped by a wall it faces), measured on
// the stag0203 train; the sequence gives control back after 41 frames. The
// signs' hit scripts take any bullet (hit_g_gatesign.lua, hit_g_turn*.lua).
constexpr SwingBox kSlamBoxes[] = {{0.0f, 4.0f, 0.0f, 16.0f}};
constexpr SwingShape kSlam{kSlamBoxes, std::size(kSlamBoxes), 7.5f, 1};

// When to press A for the swing to meet the target's hit boxes while the
// ride carries the player along at `v` per frame, the player standing still
// and facing (fx, fz) (a unit vector). A press counts the swing's live boxes
// that overlap the target by `depth` or more, frame by frame. Another attack
// (`shape`, `latency` frames from now to its first frame, default its own)
// is planned the same way.
struct SwingPlan {
    int lead = -1;  // frames until the first press with three quarters of the best count, -1: none within the horizon
    int now = 0;    // the count of a press now
    int most = 0;   // the best count within the horizon
    int first = 0;  // frames from a press now to its first meeting box
    int last = -1;  // frames until the last press within the horizon that meets well (two thirds of the best count)
};
// (The earliest press that meets at all only grazes the target's edge:
// stag0203 g_turnleft#2, an 80x80 box, was missed by such a swing.)
constexpr float kSwingDepth = 1.5f;
SwingPlan swing_plan(const engine::ModelCtrlManager::ModelRam& player, const engine::ModelCtrlManager::ModelRam& target, const engine::Vec3f& v,
                     float fx, float fz, int horizon, float depth = kSwingDepth, const SwingShape& shape = kSlash, int latency = -1) {
    if (latency < 0) latency = shape.latency;
    SwingPlan plan;
    if (!player.node || !target.node || !target.hit || target.hit->boxes.empty()) return plan;
    const auto& m = target.node->matrix;
    struct Bounds {
        float x0, x1, z0, z1;
    };
    std::vector<Bounds> boxes;
    for (const auto& [lo, hi] : target.hit->boxes) {
        Bounds b{1e30f, -1e30f, 1e30f, -1e30f};
        for (int corner = 0; corner < 4; ++corner) {
            const float x = corner & 1 ? hi.x : lo.x, z = corner & 2 ? hi.z : lo.z, y = (lo.y + hi.y) * 0.5f;
            const float wx = x * m(0, 0) + y * m(1, 0) + z * m(2, 0) + m(3, 0), wz = x * m(0, 2) + y * m(1, 2) + z * m(2, 2) + m(3, 2);
            b = {std::min(b.x0, wx), std::max(b.x1, wx), std::min(b.z0, wz), std::max(b.z1, wz)};
        }
        boxes.push_back(b);
    }
    const auto& p = player.node->matrix;
    // (The node's X row is the side: (cos, -sin) for the +Z row (sin, cos).)
    const float sx = fz, sz = -fx;
    const auto meets = [&](const SwingBox& s, int k) {
        const float cx = p(3, 0) + v.x * float(k) + s.side * sx + s.ahead * fx;
        const float cz = p(3, 2) + v.z * float(k) + s.side * sz + s.ahead * fz;
        const float d = shape.half - depth;
        for (const auto& b : boxes)
            if (cx + d >= b.x0 && cx - d <= b.x1 && cz + d >= b.z0 && cz - d <= b.z1) return true;
        return false;
    };
    std::vector<int> count(std::size_t(horizon) + 1, 0);
    plan.first = 1 << 20;
    for (int lead = 0; lead <= horizon; ++lead) {
        for (std::size_t i = 0; i < shape.count; ++i)
            for (int k = lead + latency + int(shape.boxes[i].from); k < lead + latency + int(shape.boxes[i].to); ++k)
                if (meets(shape.boxes[i], k)) {
                    ++count[std::size_t(lead)];
                    if (lead == 0) plan.first = std::min(plan.first, k);
                }
        plan.most = std::max(plan.most, count[std::size_t(lead)]);
    }
    plan.now = count[0];
    if (plan.most == 0) return plan;
    for (int lead = 0; lead <= horizon; ++lead)
        if (4 * count[std::size_t(lead)] >= 3 * plan.most) {
            plan.lead = lead;
            break;
        }
    for (int lead = horizon; lead >= 0; --lead)
        if (3 * count[std::size_t(lead)] >= 2 * plan.most) {
            plan.last = lead;
            break;
        }
    return plan;
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

// The ridden model's ground axes: along its node's +Z row, sideways along
// its +X row (unit vectors), from its origin.
struct RideFrame {
    bool valid = false;
    float ox = 0.0f, oz = 0.0f, ax = 0.0f, az = 1.0f, sx = 1.0f, sz = 0.0f;
    float along(const engine::Vec3f& p) const { return (p.x - ox) * ax + (p.z - oz) * az; }
    float side(const engine::Vec3f& p) const { return (p.x - ox) * sx + (p.z - oz) * sz; }
    engine::Vec3f world(float a, float s, float y) const { return {ox + a * ax + s * sx, y, oz + a * az + s * sz}; }
};
RideFrame ride_frame_of(const engine::ModelCtrlManager::ModelRam& player) {
    RideFrame f;
    if (!player.ride || !player.ride->node) return f;
    const auto& n = player.ride->node->matrix;
    const float la = std::hypot(n(2, 0), n(2, 2)), ls = std::hypot(n(0, 0), n(0, 2));
    if (la < 0.2f || ls < 0.2f) return f;
    f = {true, n(3, 0), n(3, 2), n(2, 0) / la, n(2, 2) / la, n(0, 0) / ls, n(0, 2) / ls};
    return f;
}

// An enemy on the ride (or where its walk ends), in the ride's axes.
struct RideFoe {
    float along, side;
};

// The frames an AITIMER has left (a named RAM timer, 8031E238), 0 when it is
// missing or over.
float ai_frames_left(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& e) {
    if (!e.ai_timer) return 0.0f;
    return std::max(0.0f, float(e.ai_timer->end - engine::timer_time(*e.ai_timer, engine.time())) / 16666667.0f);
}

// An attack lunges its model forward over its first frames (stag0203
// e_mecha1 SQ_ATCK1: some 2.8 units along its facing, measured on the car);
// kLunge[k] is the step of its frame k + 1.
constexpr float kLunge[] = {0.95f, 0.8f, 0.55f, 0.35f, 0.15f};
constexpr float kLungeLength = 2.8f;
// An enemy's walk on the car (stag0203 e_mecha1 SQ_WALK, measured).
constexpr float kEnemyWalk = 0.24f;

// A yaw turned toward `want` by at most `step` (the shorter way).
float turned(float yaw, float want, float step) {
    const float delta = std::remainder(want - yaw, 6.2831853f);
    return yaw + std::clamp(delta, -step, step);
}

// When an enemy riding with the player can hurt it, from the enemy's AIM
// map (ModelCtrlManager::update_ai_map, 80308654): each time its AITIMER
// runs out (and at once after an attack: the attack's frames give no
// control) it picks by its distance band. stag0203 e_mecha1 (map 1) attacks
// within 15 (squared 225: SQ_ATCK1 for 12 of 16 rolls, else a short walk
// sideways) and walks at the player beyond (20-34 frames, then the next
// pick). A walk keeps its direction (turning to it at the map's rate, 0.07
// a frame) at kEnemyWalk; an attack lunges (kLunge) while it turns to the
// player and checks its box (6x6x6, 4 ahead) once, 12 frames in. The body
// box (3x4x3) hurts on touch. `certain`: a touch now or a queued box that
// meets the player's body (5x7x5, its origin at the feet); `possible`: the
// earliest blow its next picks could bring (a walk that touches, an attack
// that reaches). `shift` is added to the enemy's place: the stag0203 mechas
// come before their car in the update order and the player after it
// (ModelCtrlManager::update runs the RAMs in registration order), so after
// a frame they stand one frame of the car's travel behind the place their
// boxes meet the player at (both are checked against the car of the frame
// before).
struct Blow {
    float certain = 1e9f, possible = 1e9f;
    float due() const { return std::min(certain, possible); }
};
Blow blow_time(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& e, const engine::Vec3f& me, const engine::Vec3f& shift) {
    constexpr float kAttackLength = 26.0f, kBlowDelay = 12.0f, kAttackAhead = 4.0f, kBoxHalf = 3.0f;
    constexpr float kBodyHalf = 2.5f, kBodyHeight = 7.0f; // the player's hit body
    constexpr float kEnemyHalf = 1.5f;                    // e_mecha1's body box
    constexpr float kTouchGap = 0.3f;
    float turn = 0.07f, attack_band = 225.0f;
    if (e.ai_maps && !e.ai_maps->empty()) {
        const auto& map = (*e.ai_maps)[std::size_t(std::clamp(e.ai_map, 0, int(e.ai_maps->size()) - 1))];
        turn = map.f32(6, turn);
        attack_band = map.f32(3, attack_band);
    }
    const float attack_range = std::sqrt(std::max(attack_band, 0.0f));
    const auto& m = e.node->matrix;
    const engine::Vec3f p{m(3, 0) + shift.x, m(3, 1), m(3, 2) + shift.z};
    const float yaw = std::atan2(m(2, 0), m(2, 2)), want = e.desired[1];
    const bool attacking = e.seq_name.rfind("SQ_ATCK", 0) == 0;
    // (The body box every sequence queues, end 1e8, tells how long it ran.)
    float age = 0.0f;
    for (const auto& h : e.hitboxes)
        if ((h.kind / 10000 == 7 || h.kind / 10000 == 8) && h.end >= 1e7f) age = h.elapsed;
    // (The lunge still to come after `from` frames of the attack, frame by
    // frame, along the facing turning to `toward`.)
    const auto lunge_to = [&](float from, float until, float toward, float& x, float& z) {
        for (int k = 0; k < 5; ++k) {
            if (float(k + 1) <= from || float(k + 1) > until) continue;
            const float f = turned(yaw, toward, turn * (float(k + 1) - from));
            x += std::sin(f) * kLunge[k];
            z += std::cos(f) * kLunge[k];
        }
    };
    const auto meets = [&](float cx, float cy, float cz, const engine::Vec3f& size) {
        if (cy - size.y * 0.5f > me.y + kBodyHeight || cy + size.y * 0.5f < me.y) return 1e9f;
        return std::max(std::abs(cx - me.x) - size.x * 0.5f, std::abs(cz - me.z) - size.z * 0.5f) - kBodyHalf;
    };
    Blow blow;
    for (const auto& h : e.hitboxes) {
        const int group = h.kind / 10000;
        if (group != 7 && group != 8) continue;
        const float cy = h.offset.y + m(3, 1);
        if (h.end >= 1e7f) {
            if (meets(p.x + h.offset.x * m(0, 0) + h.offset.z * m(2, 0), cy, p.z + h.offset.x * m(0, 2) + h.offset.z * m(2, 2), h.size) < kTouchGap)
                blow.certain = 0.0f;
            // (Touching once the lunge has carried it on.)
            if (attacking) {
                float x = p.x, z = p.z;
                lunge_to(age, 5.0f, want, x, z);
                if (meets(x, cy, z, h.size) < kTouchGap) blow.certain = std::min(blow.certain, std::max(1.0f, 5.0f - age));
            }
            continue;
        }
        const float left = std::max(0.0f, h.delay - h.elapsed);
        float x = p.x, z = p.z;
        lunge_to(h.elapsed, h.delay, want, x, z);
        const float f = turned(yaw, want, turn * left);
        // (The authored offset turned with the model: side x, ahead z.)
        const float cx = x + h.offset.x * std::cos(f) + h.offset.z * std::sin(f);
        const float cz = z - h.offset.x * std::sin(f) + h.offset.z * std::cos(f);
        if (meets(cx, cy, cz, h.size) < kTouchGap) blow.certain = std::min(blow.certain, left);
    }
    // The next pick. After an attack the model stands where its lunge
    // ends; a walk goes on for the rest of its AITIMER.
    const float timer_left = ai_frames_left(engine, e);
    float t = 0.0f, x = p.x, z = p.z, f = yaw;
    if (attacking) {
        lunge_to(age, 5.0f, want, x, z);
        t = std::max(0.0f, kAttackLength - age) + 1.0f;
        f = want;
    } else if (e.seq_name == "SQ_WALK" && e.ctrl_mode != 0) {
        // (The walk: touching on the way?)
        for (int k = 1; float(k) <= timer_left && k <= 60; ++k) {
            f = turned(f, want, turn);
            x += std::sin(f) * kEnemyWalk;
            z += std::cos(f) * kEnemyWalk;
            if (std::max(std::abs(x - me.x), std::abs(z - me.z)) - kEnemyHalf - kBodyHalf < kTouchGap) {
                blow.possible = std::min(blow.possible, float(k));
                break;
            }
        }
        t = std::min(timer_left, 60.0f) + 1.0f;
    } else if (e.ctrl_mode == 0) {
        // (A sequence without control, a flinch or a guard: some frames yet.)
        t = 10.0f;
    } else {
        t = 1.0f;
    }
    // The pick then: within the attack band an attack (it turns to the
    // player while its box comes), else a walk at the player that brings it
    // into the band.
    const float d = std::hypot(me.x - x, me.z - z);
    const float reach = kAttackAhead + kBoxHalf + kLungeLength + kBodyHalf;
    float approach = 0.0f;
    if (d >= attack_range) approach = (d - attack_range) / kEnemyWalk + 1.0f;
    if (const float near = std::max(d - kEnemyWalk * approach, 0.0f); near <= reach) {
        // (Where its box comes: the model turns toward the player while it
        // walks in and attacks, lunges, and checks the box ahead of it.)
        const float g = turned(f, std::atan2(me.x - x, me.z - z), turn * (approach + kBlowDelay));
        const float bx = x + (me.x - x) / std::max(d, 0.01f) * (d - near) + std::sin(g) * (kAttackAhead + kLungeLength);
        const float bz = z + (me.z - z) / std::max(d, 0.01f) * (d - near) + std::cos(g) * (kAttackAhead + kLungeLength);
        if (std::max(std::abs(bx - me.x), std::abs(bz - me.z)) - kBoxHalf - kBodyHalf < kTouchGap + 1.0f)
            blow.possible = std::min(blow.possible, t + approach + kBlowDelay);
    } else {
        // (Its attack falls short: the walks after it close in some time.)
        blow.possible = std::min(blow.possible, t + approach + (d - reach) / kEnemyWalk + kBlowDelay + 30.0f);
    }
    blow.possible = std::max(blow.possible, 1.0f);
    return blow;
}

// The line the player keeps to on the car while a sign comes: kEdgeSide off
// the middle on the sign's side (the car's walls stop the player some 29.4
// off it; inside them the player walks along without touching them), and
// along it within kAlongSpan of the origin (the car floor reaches -32.5..30
// along, its walls stop the player at about -24).
constexpr float kEdgeSide = 26.0f;
constexpr float kAlongSpan = 19.0f;
// The player's walk on the car (it reaches 0.7 a frame some ten frames after
// standing), the frames a crossing needs beyond that, and the spare left
// when it starts.
constexpr float kWalkSpeed = 0.6f, kCrossSpare = 25.0f, kSpotSpare = 30.0f;
// Frames before the sign's first good press from which the player leans on
// the wall on the sign's side.
constexpr float kLeanLead = 40.0f;

// Where to stand on the car while a sign comes (car axes): of a grid of
// spots, the one with the most room to the enemies once the player is
// there (they walk at it meanwhile, kEnemyApproach), away from the car's
// corners and not past an enemy on the way; only spots from which the edge
// on the sign's side (`side`, 0: not known) is still reached in the `until`
// frames left. The spot chosen before (`last`) keeps a small lead.
struct Spot {
    float along, side;
};
Spot choose_spot(float me_along, float me_side, const std::vector<RideFoe>& foes, int side, float until, const Spot& last) {
    constexpr float kEnemyApproach = 0.25f, kRoomCap = 22.0f, kPathRoom = 7.0f;
    const float edge = float(side) * kEdgeSide;
    const float here_a = std::clamp(me_along, -kAlongSpan, kAlongSpan), here_s = std::clamp(me_side, -kEdgeSide, kEdgeSide);
    std::vector<Spot> spots{{here_a, here_s}, last};
    for (const float a : {-16.0f, -8.0f, 0.0f, 8.0f, 16.0f})
        for (const float s : {-26.0f, -13.0f, 0.0f, 13.0f, 26.0f}) spots.push_back({a, s});
    Spot best{here_a, side != 0 ? edge : here_s};
    float best_score = -1e9f;
    for (std::size_t i = 0; i < spots.size(); ++i) {
        const Spot& p = spots[i];
        const float dist = std::hypot(p.along - me_along, p.side - me_side);
        const float t = dist / kWalkSpeed;
        if (side != 0 && std::abs(p.side - edge) > 1.0f && t + std::abs(edge - p.side) / kWalkSpeed + kCrossSpare > until - kSpotSpare) continue;
        float room = kRoomCap;
        for (const auto& f : foes) {
            room = std::min(room, std::hypot(p.along - f.along, p.side - f.side) - kEnemyApproach * t);
            // (The way there: the enemy's distance to the segment.)
            const float ux = p.along - me_along, uz = p.side - me_side, len2 = ux * ux + uz * uz;
            const float k = len2 > 0.01f ? std::clamp(((f.along - me_along) * ux + (f.side - me_side) * uz) / len2, 0.0f, 1.0f) : 0.0f;
            const float pass = std::hypot(me_along + ux * k - f.along, me_side + uz * k - f.side);
            if (pass < kPathRoom && k > 0.05f) room = std::min(room, pass - kPathRoom);
        }
        float score = room - 0.03f * dist;
        if (std::abs(p.along) >= 15.0f && std::abs(p.side) >= 25.0f) score -= 2.0f;
        if (i == 1) score += 1.0f;
        if (i == 0) score += 0.5f;
        if (score > best_score) {
            best_score = score;
            best = p;
        }
    }
    return best;
}

// The point along the edge line farthest from the enemies on the car (up
// to 20: beyond that the player stays), not past an enemy standing on the
// line between the player and it, and a little nearer preferred.
float away_along(float me_along, float keep_side, const std::vector<RideFoe>& foes) {
    const auto room = [&](float a) {
        float r = 20.0f;
        for (const auto& f : foes) r = std::min(r, std::hypot(a - f.along, keep_side - f.side));
        return r;
    };
    const float here = std::clamp(me_along, -kAlongSpan, kAlongSpan);
    float best = here, best_score = room(here) + 0.5f;
    for (float a = -kAlongSpan; a <= kAlongSpan; a += 1.0f) {
        bool blocked = false;
        for (const auto& f : foes) {
            if (std::abs(f.side - keep_side) >= 7.0f) continue;
            const float lo = std::min(here, a), hi = std::max(here, a);
            if (f.along > lo && f.along < hi && std::abs(f.along - here) > 3.0f) blocked = true;
        }
        if (blocked) continue;
        const float score = room(a) - 0.1f * std::abs(a - here);
        if (score > best_score) {
            best_score = score;
            best = a;
        }
    }
    return best;
}

// The direction mask the pad makes of a stick (pad.cpp directions,
// 80311AA4..80311BBC): eight overlapping zones, none below 0.4.
std::uint32_t stick_mask(float x, float y) {
    if (std::sqrt(x * x + y * y) < 0.4f) return 0;
    const float angle = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
    constexpr float quarter_pi = 0x1.921fb6p-1f, three_quarter_pi = 0x1.2d97c8p+1f, half_overlap = 0.5f * quarter_pi;
    std::uint32_t result = 0;
    if (angle < -three_quarter_pi + half_overlap || angle > three_quarter_pi - half_overlap) result |= pad_direction::negative_x;
    if (angle >= -three_quarter_pi - half_overlap && angle <= -quarter_pi + half_overlap) result |= pad_direction::negative_y;
    if (angle > -quarter_pi - half_overlap && angle < quarter_pi + half_overlap) result |= pad_direction::positive_x;
    if (angle >= quarter_pi - half_overlap && angle <= three_quarter_pi + half_overlap) result |= pad_direction::positive_y;
    return result;
}

// The camera's heading (the yaw of its view's inverse +Z row), which turns
// the stick into the player's walking direction (8034C67C).
float camera_heading(engine::Engine& engine) {
    auto* cameras = engine.get<engine::CameraManager>();
    if (!cameras) return 0.0f;
    engine::FrameCamera camera;
    cameras->frame_camera(engine, camera);
    engine::Mat4 inverse;
    if (!engine::invert(camera.view, inverse)) return 0.0f;
    return std::atan2(inverse(2, 0), inverse(2, 2));
}

// The ride's next timed target (RideTargets::timing): the clock model's
// sequence changes are followed from frame to frame (`start`: the frame the
// current one was first seen); `abreast_in` counts the frames until the
// target comes abreast of the ridden model's origin. A target struck already
// (SQ_SWON) or passed more than kPassedFrames ago is skipped.
struct Scheduled {
    bool valid = false;
    std::string name;
    float abreast_in = 0.0f;
};
Scheduled ride_schedule(engine::ModelCtrlManager& models, const RideTargets& ride, int frame) {
    constexpr float kPassedFrames = 40.0f;
    Scheduled next;
    if (!*ride.clock || !*ride.timing) return next;
    const engine::ModelCtrlManager::ModelRam* clock = nullptr;
    for (const auto& e : models.rams())
        if (e->generated && e->info_name == ride.clock) clock = e.get();
    static std::string clock_seq;
    static int clock_start = 0, clock_seen = -100;
    if (!clock) return next;
    if (clock->seq_name != clock_seq || frame < clock_seen) {
        clock_seq = clock->seq_name;
        clock_start = frame;
    }
    clock_seen = frame;
    const std::string timing = std::string(ride.timing) + "|";
    for (std::size_t at = 0; at < timing.size();) {
        const auto bar = timing.find('|', at);
        const std::string entry = timing.substr(at, bar - at);
        at = bar + 1;
        const auto mark = entry.find('@'), plus = entry.find('+'), hash = entry.find('#');
        if (mark == std::string::npos || plus == std::string::npos || entry.substr(mark + 1, plus - mark - 1) != clock_seq) continue;
        const float in = float(clock_start + std::atoi(entry.c_str() + plus + 1) - frame);
        if (in < -kPassedFrames || (next.valid && in >= next.abreast_in)) continue;
        bool struck = false;
        for (const auto& e : models.rams())
            if (e->generated && hash < mark && e->info_name == entry.substr(0, hash) && e->index == std::atoi(entry.c_str() + hash + 1))
                struck = struck || e->seq_name == "SQ_SWON";
        if (!struck) next = {true, entry.substr(0, mark), in};
    }
    return next;
}

// ---------------------------------------------------------------------------
// Survival (runs without the test cheats)
// ---------------------------------------------------------------------------
// Without DW4_ONE_HIT the driver casts its first technique at enemies in
// front while the MP covers it (MP disks when it runs out, kTechReach), and
// in the lobby it puts the strongest devices found on at the Digi-Lab
// (next_equip, kEquipGain). The panels take no input during their FIRE wait
// after a pick (kPanelFire), so a panel walk starts only after it.
// Without DW4_GOD it keeps the character alive like a player would (all of
// it is off with the cheats, so the recorded cheat run keeps its path):
// - It guards (B) just before an enemy attack box lands and starts no attack
//   while one is under way (kHoldAttackLead), guards an enemy's touch
//   (kTouchGuard), and after kAggressAfter frames without an attack fights
//   for a while without guarding.
// - Below kHealBelow percent of its maximum HP it uses an HP disk from the
//   L panel (survive): the smallest that makes up the missing HP, else the
//   biggest held, Recovery last; every kHealGap frames at most. (Y, the quick
//   panel's emergency heal GENEEMGHEAL, 8034ABA0, always takes the highest
//   record held, Recovery first; a player saves that one.) Stages may forbid
//   healing (NDMWPANEL NOHEALING, the stag0103 boat).
// - In the lobby, from phase 2 on (stagML13 n_digilff_00.lua sells only once
//   flag 1 is off), it restocks at the disk shop: stagML13 n_digilff#0
//   (bML01008.lua "buy?" -> diskshop.lua -> SUBWND DISK, ShopDisk 803967A8:
//   Buy, the ten conItem rows at their HANBAI price, the count, yes) up to
//   `keep` of each supply that fits the character's maximum HP, in the
//   order listed, as far as the bits go; before that it sells the weaker
//   devices there (next_sale, kSellFrom). With bits to spare it buys
//   stronger devices at the weapon and armor shops (next_gear).
// - Low HP is also HP within reach of the biggest blow taken lately
//   (kBigHitMargin).
// - After a defeat the bit capsule (g_000bcaps, PLAYER2 slot of the player)
//   keeps the bits and a fifth of the level's EXP step (BKCHIPBITEXP
//   80377F70); it comes back where it was when the stage is entered again
//   (PLAYER2 GENESTAGE in funcPlayerSet). A Raise Disk (conItem 7, "Revives
//   backup chips from all around") used within 30 units of it (803B1328 ->
//   P2PKOUKAMAGIC -50 -> PLAYER2 RAISE, 59 % of the EXP) brings them back:
//   the driver walks to the capsule and uses the disk from the L panel.
// - With nothing left to heal with it walks to a heal gate of the stage or
//   uses a Gate Disk (kGateDisk); in the lobby the X-Data shop refills HP and
//   MP (kLobbyHealBelow); below an objective's gate level it trains in the
//   area's field (kTraining, kGates).
constexpr int kHealBelow = 40; // percent of the maximum HP
constexpr int kHealGap = 90;   // frames between two emergency heals
constexpr int kHealQuickBelow = 20; // percent: Y (whatever disk it takes) instead of the panel
// Also low HP: below kBigHitMargin percent of the biggest blow taken within
// kBigHitFor frames (survive).
constexpr int kBigHitMargin = 130, kBigHitFor = 3600;
// The guard (survive): no attack is started while an enemy blow lands within
// kHoldAttackLead frames; B goes in kGuardLead frames before it.
constexpr float kHoldAttackLead = 24.0f, kGuardLead = 3.0f;
// No attack (SQ_ATCK*, SQ_WAZA) for kAggressAfter frames, the guard holding
// it back from an enemy that attacks without pause (stag0101 goburi packs,
// the dng102b3 boss e_mammos): kAggressFor frames without guarding.
constexpr int kAggressAfter = 600, kAggressFor = 240;
// (Only once the guard took back kAggressHeld presses of A in that time: no
// attack for long because no enemy was near is no reason; dng102b3's
// e_mammos came up right after such a walk and its blows, unguarded for 240
// frames, ended a level 9 Veemon of 390 HP.)
constexpr int kAggressHeld = 8;
// Techniques (survive, not with the one-hit cheat): the first one is cast at
// an enemy within kTechReach in front whenever the MP covers it, at most every
// kTechGap frames (its DELAY is 75).
constexpr float kTechReach = 22.0f;
// The touch guard (survive): B once an enemy body box comes within this gap.
constexpr float kTouchGuard = 0.3f;
constexpr int kTechGap = 90;
// The disk panel's FIRE wait after a disk is picked (80349B0C returns 1 s;
// a technique's is its pcTech DELAY): the panel RAM's one timer, which blocks
// every cursor press, A and Y of either list until it runs out (8034A210).
constexpr int kPanelFire = 61;
struct Supply {
    int record;          // conItem row (DISKFOLDER record)
    int keep;            // bought up to this count (a slot holds 9)
    int from_hp, to_hp;  // for a maximum HP in [from_hp, to_hp)
};
constexpr Supply kSupplies[] = {
    {0, 9, 0, 1200},       // HP Disk S: 450 HP for 100 bits
    {1, 5, 1200, 1 << 30}, // HP Disk M: 1800 HP for 500 bits
    {3, 6, 0, 1 << 30},    // MP Disk S: 400 MP for 160 bits (techniques, kTechReach)
    {7, 1, 0, 1 << 30},    // Raise Disk (200 bits): the bit capsule after a defeat
    {8, 2, 0, 1 << 30},    // Gate Disk (120 bits): back to the lobby (kGateTrip)
};
constexpr int kRaiseDisk = 7;
constexpr int kRaiseWalk = 1500; // frames to reach the bit capsule
constexpr int kLobbyHealBelow = 90; // percent of HP or MP: refilled at the X-Data shop in the lobby
// The Gate Disk (conItem 8, 803B1390 -> gatedisc.lua: every player back to
// the lobby, stagML11; NOGATEDISK stages refuse it): used below
// kGateTripBelow percent of the maximum HP with nothing left to heal with (a
// defeat would lose the bits and part of the EXP), or while training with
// supplies worth kGateTripBits bits to buy or a full device folder (the shop
// is in the lobby). Giving up an objective that way raises its gate level.
constexpr int kGateDisk = 8;
constexpr int kGateTripBelow = 40;
constexpr int kGateTripBits = 400;

// Training (no god mode): an objective in a combat stage is taken on only
// at its gate level: the highest of the table's level for it and one more
// than the level a defeat at it came at (each defeat raises the bar). Below
// it the player trains in the area's field, whose radar generators send
// enemies again and again (the ExploreBot fights whatever it meets there with
// the goal "none"); once nothing is left to fight for a while it leaves by
// `leave` and the plan brings it back, which generates the field anew.
struct Training {
    const char* stages; // '|'-separated stage name prefixes of the objectives it serves
    const char* field;  // where to train
    const char* leave;  // the exit mats taken when the field is spent
};
constexpr Training kTraining[] = {
    {"stag01|dng10|staggf|stagbs01", "stag0101", "matz02"},
    {"stag02|dng20|stagbs02", "stag0201", "matz00"},
    {"stag03|dng30|dng310|stagbs03", "stag0301", "mate00"},
    {"stag04|dng41|stagbs0", "stag0401", "matz00"},
};
// Gate levels from the start (the objective's name, the level).
struct Gate {
    const char* objective;
    int level;
};
constexpr Gate kGates[] = {
    {"key item 0 (dng101b2 boss)", 4},
    {"key item 1 (dng102b3 boss)", 6},
    {"Apocalymon (stagBS01)", 9},
};
constexpr int kTrainSpent = 4000; // frames without EXP before the field is left
// Heal gates (heal_gate_step): sought below kGateBelow percent of the maximum
// HP without a disk, given up after kGateWalk frames.
constexpr int kGateBelow = 45;
constexpr int kGateWalk = 1500;

// The training an objective needs first: null when player 1 is at its gate
// level (kGates, and one above the level of a defeat at it, `defeats`).
const Objective* train_for(engine::Engine& engine, const Objective& objective, const std::map<std::string, int>& defeats) {
    auto* sys = engine.get<engine::GameSystem>();
    if (!sys) return nullptr;
    int need = 0;
    for (const auto& gate : kGates)
        if (std::string_view(objective.name) == gate.objective) need = gate.level;
    if (const auto it = defeats.find(objective.name); it != defeats.end()) need = std::max(need, it->second + 1);
    if (sys->players[0].stats.level >= need) return nullptr;
    const std::string_view stage = objective.stage;
    for (const auto& training : kTraining) {
        bool serves = false;
        const std::string_view list = training.stages;
        for (std::size_t at = 0; at <= list.size();) {
            const auto bar = std::min(list.find('|', at), list.size());
            if (bar > at && stage.substr(0, bar - at) == list.substr(at, bar - at)) serves = true;
            at = bar + 1;
        }
        if (!serves) continue;
        // (One training at a time: the strings live on until the next.)
        static std::string name, action;
        static Objective storage{"", "", "", ""};
        name = "train to level " + std::to_string(need) + " in " + training.field + " for " + objective.name;
        action = std::string("train:") + training.leave;
        storage = Objective{name.c_str(), "", training.field, action.c_str()};
        return &storage;
    }
    return nullptr;
}

// The DISKFOLDER count of a record (0 when the folder has no slot for it).
int disks_held(const engine::GameSystem::PlayerSlot& slot, int record) {
    for (const auto& disk : slot.disks)
        if (disk.record == record) return disk.count;
    return 0;
}

} // namespace

// The next purchase at the disk shop (kSupplies): record -1 when nothing is
// wanted or the bits do not go that far. `prices` are the HANBAI prices seen.
CampaignBot::Purchase CampaignBot::next_purchase(engine::Engine& engine) const {
    auto* sys = engine.get<engine::GameSystem>();
    if (!sys) return {};
    const auto& slot = sys->players[0];
    for (const auto& s : kSupplies) {
        if (slot.max_hp < s.from_hp || slot.max_hp >= s.to_hp) continue;
        const auto it = prices_.find(s.record);
        if (it == prices_.end() || it->second <= 0) continue;
        const int held = disks_held(slot, s.record);
        const int want = std::min({s.keep, 9}) - held;
        const int count = std::min(want, slot.bits / it->second);
        if (count > 0) return {s.record, count, held};
    }
    return {};
}

// The bits all the purchases due would spend (next_purchase in turn).
int CampaignBot::planned_spend(engine::Engine& engine) const {
    auto* sys = engine.get<engine::GameSystem>();
    if (!sys) return 0;
    const auto& slot = sys->players[0];
    int bits = slot.bits, spend = 0;
    for (const auto& s : kSupplies) {
        if (slot.max_hp < s.from_hp || slot.max_hp >= s.to_hp) continue;
        const auto it = prices_.find(s.record);
        if (it == prices_.end() || it->second <= 0) continue;
        const int count = std::min(std::min({s.keep, 9}) - disks_held(slot, s.record), bits / it->second);
        if (count <= 0) continue;
        bits -= count * it->second;
        spend += count * it->second;
    }
    return spend;
}

// The equipment (not with the one-hit cheat): the device folder fills with
// what the enemies drop (weapon, armor1 = boards, armor2 = accessories;
// DEVICEFOLDER holds 36), and a player puts the strongest on at the Digi-Lab
// (beNDMWShopCtrl20 "Equip"; the field status menu only reorders the
// weapons, SlotWindow::action). A device is
// weighed by what it changes in a fight (RECALCPLAYER 8036FDE8: the weapon in
// use adds TSUYOSA + value, a board MAMORI + value and MAXHP, an accessory its
// TSUYOSA / MAMORI / MAXHP plus the value on its main stat SHUSEINOURYOKU):
// the blows dealt, (TSUYOSA * 6 + weapon skill * 4) / enemy MAMORI (P2EDAMAGE
// 80372EC4), times the blows taken until a defeat, MAMORI * MHP (E2PDAMAGE
// 8037526C divides by MAMORI). One gaining kEquipGain percent is put on.
// Weapons stay of the kind in use (its SKILL row): the bots fight with that
// kind's moves and the skill points grow with use. A weapon needs the
// species bit in SOUBI and SKILLPOINT of its skill, a board or accessory the
// LVPOINT level (SlotWindow::can_equip).
constexpr int kEquipGain = 3;
constexpr int kEquipWalk = 6000; // frames for one pick (walk to the lab, its menus)

// How much stronger in a fight putting `d` on (in its slot: kind 0 the
// weapon in use, 1 the board, 2 the accessory) makes player 1 than what it
// wears there now: 1 the same, 0 when it cannot be put on or is not wanted.
double CampaignBot::equip_value(engine::Engine& engine, const engine::ItemDevice& d) const {
    auto* sys = engine.get<engine::GameSystem>();
    auto* db = engine.get<engine::DatabaseManager>();
    if (!sys || !db || !db->has_column("weapon", "TSUYOSA") || d.type < 0 || d.type > 2) return 0.0;
    const auto& p = sys->players[0];
    const auto& st = p.stats;
    static const char* const tables[3] = {"weapon", "armor1", "armor2"};
    const auto cell = [&](const engine::ItemDevice& e, const char* column) {
        return db->number(tables[std::clamp(e.type, 0, 2)], e.record, column).value_or(0);
    };
    // (The placeholder records of unidentified devices, DeviceList::unknown.)
    if ((d.type == 0 && d.record == 276) || (d.type == 1 && d.record == 31) || (d.type == 2 && d.record == 44)) return 0.0;
    // A device's (TSUYOSA, MAMORI, MAXHP) in its slot.
    struct Gain {
        int str = 0, def = 0, hp = 0;
    };
    const auto gain = [&](const engine::ItemDevice* e) {
        Gain g;
        if (!e) return g;
        if (e->type == 0) g.str = cell(*e, "TSUYOSA") + e->value;
        else if (e->type == 1) g.def = cell(*e, "MAMORI") + e->value, g.hp = cell(*e, "MAXHP");
        else if (e->type == 2) {
            const auto main = db->text("armor2", e->record, "SHUSEINOURYOKU");
            g.str = cell(*e, "TSUYOSA") + (main == "TSUYOSA" ? e->value : 0);
            g.def = cell(*e, "MAMORI") + (main == "MAMORI" ? e->value : 0);
            g.hp = cell(*e, "MAXHP");
        }
        return g;
    };
    const auto* weapon = p.device(p.power_socket.empty() ? -1 : p.power_socket[0]);
    const int skill_row = weapon ? std::clamp(cell(*weapon, "SKILL"), 0, 8) : 0;
    const double skill = st.skill[std::size_t(skill_row)];
    const auto score = [&](const Gain& off, const Gain& on) {
        const double str = std::max(1, st.tsuyosa - off.str + on.str), def = std::max(1, st.mamori - off.def + on.def);
        const double hp = std::max(1, p.max_hp - off.hp + on.hp);
        return (str * 6.0 + skill * 4.0) * def * hp;
    };
    if (d.type == 0) {
        const int row = cell(d, "SKILL");
        const auto soubi = std::uint32_t(cell(d, "SOUBI"));
        if (!weapon || row != skill_row || st.species < 0 || st.species >= 32 || !(soubi & (1u << st.species)) ||
            st.skill[std::size_t(std::clamp(row, 0, 8))] < cell(d, "SKILLPOINT"))
            return 0.0;
    } else if (st.level < cell(d, "LVPOINT")) {
        return 0.0;
    }
    const int worn_index = d.type == 0 ? (p.power_socket.empty() ? -1 : p.power_socket[0])
                           : d.type == 1 ? (p.main_slot.empty() ? -1 : p.main_slot[0])
                                         : (p.sub_slot.empty() ? -1 : p.sub_slot[0]);
    const Gain off = gain(p.device(worn_index));
    return score(off, gain(&d)) / score(off, off);
}

CampaignBot::EquipPick CampaignBot::next_equip(engine::Engine& engine) const {
    auto* sys = engine.get<engine::GameSystem>();
    if (!sys) return {};
    const auto& p = sys->players[0];
    for (int kind = 0; kind < 3; ++kind) {
        double best = (100 + kEquipGain) / 100.0;
        int pick = -1;
        for (std::size_t i = 0; i < p.devices.size(); ++i) {
            const auto& d = p.devices[i];
            if (d.type != kind || d.flag || equip_failed_.count({kind, d.record})) continue;
            if (const double v = equip_value(engine, d); v > best) {
                best = v;
                pick = int(i);
            }
        }
        if (pick >= 0) return {kind, pick};
    }
    return {};
}

// Gear from the weapon and armor shops (not in god mode, like the disk shop): stagML13
// n_digilfm#0 sells weapons (bML01009.lua -> weaponshop.lua -> SUBWND WPU,
// program 6) and n_digilff#1 boards and accessories (bML01010.lua ->
// armorshop.lua -> APU, 7), both while flag 1 is off. The stock is made when
// the program starts (80351794: shopWeapon by species and level row,
// shopArmor by level row; a stage change empties it), so each shop is looked
// at once per level with kGearBits bits or more: Buy, the stock (12 a page,
// beNDMWShopCtrlSales 803A2920, at HANBAI + 100 * value (+ 500 .. 24000 by
// rank for weapons and boards)), yes. Of what the bits buy beyond
// kGearReserve (for disks) the device that gains most over what is worn
// (equip_value, at least kGearGain percent) is bought; the Digi-Lab then
// puts it on.
constexpr int kGearBits = 900, kGearReserve = 600, kGearGain = 8;

int CampaignBot::next_gear(engine::Engine& engine, int program) const {
    auto* sys = engine.get<engine::GameSystem>();
    auto* db = engine.get<engine::DatabaseManager>();
    auto* windows = engine.get<engine::SubWindowManager>();
    if (!sys || !db || !windows || (program != 6 && program != 7)) return -1;
    const auto* stock = windows->shop_stock(program);
    const auto& p = sys->players[0];
    if (!stock || p.devices.size() >= 36) return -1;
    static const int buy_rank[6] = {0, 500, 1500, 6000, 12000, 24000};
    static const char* const tables[3] = {"weapon", "armor1", "armor2"};
    // (Measured against the best held of each kind too: a device bought is
    // put on at the lab only later.)
    std::array<double, 3> held{1.0, 1.0, 1.0};
    for (const auto& d : p.devices)
        if (d.type >= 0 && d.type <= 2 && !d.flag) held[std::size_t(d.type)] = std::max(held[std::size_t(d.type)], equip_value(engine, d));
    int pick = -1;
    double best = 0.0;
    for (std::size_t i = 0; i < stock->size(); ++i) {
        const auto& e = (*stock)[i];
        if (!e || e->type < 0 || e->type > 2) continue; // (gaps, chips, the sold-out abChip 185)
        int price = db->number(tables[e->type], e->record, "HANBAI").value_or(0) + 100 * e->value;
        if (e->type <= 1) price += buy_rank[std::clamp(e->rank, 0, 5)];
        if (price <= 0 || price > p.bits - kGearReserve) continue;
        const double v = equip_value(engine, *e) / held[std::size_t(e->type)];
        if (v > (100 + kGearGain) / 100.0 && v > best) {
            best = v;
            pick = int(i);
        }
    }
    return pick;
}

// Device sales (not in god mode): the disk shop buys devices too (Buy / Sell
// / Exit -> Sell -> Devices, beNDMWShopCtrlDeviceSell 803A01F0: the folder,
// 12 a page, at KAITORI + 25 * value (+ 250 .. 12000 by rank for weapons and
// boards, 803A0CDC's price); equipped devices cannot be sold). Of each kind
// the strongest unequipped device is kept (weapons per SKILL row by TSUYOSA +
// value, boards and accessories by their MAMORI / TSUYOSA + value and MAXHP);
// the rest goes, chips too (the driver plays no chips): bits for disks, and
// room in the folder, which takes no drop when it holds 36. The shop is
// visited for it once the folder holds kSellFrom devices.
constexpr int kSellFrom = 24;

int CampaignBot::next_sale(engine::Engine& engine) const {
    auto* sys = engine.get<engine::GameSystem>();
    auto* db = engine.get<engine::DatabaseManager>();
    if (!sys || !db || engine.cheats().god || !db->has_column("weapon", "KAITORI")) return -1;
    const auto& p = sys->players[0];
    static const char* const tables[4] = {"weapon", "armor1", "armor2", "abChip"};
    static const int sell_rank[6] = {0, 250, 750, 3000, 6000, 12000};
    const auto cell = [&](const engine::ItemDevice& d, const char* column) {
        return db->number(tables[std::clamp(d.type, 0, 3)], d.record, column).value_or(0);
    };
    const auto placeholder = [](const engine::ItemDevice& d) {
        return (d.type == 0 && d.record == 276) || (d.type == 1 && d.record == 31) || (d.type == 2 && d.record == 44);
    };
    std::map<std::pair<int, int>, std::pair<int, int>> kept; // (type, SKILL row) -> (strength, device)
    for (std::size_t i = 0; i < p.devices.size(); ++i) {
        const auto& d = p.devices[i];
        if (d.flag || placeholder(d) || d.type == 3) continue;
        int strength = 0;
        if (d.type == 0) strength = cell(d, "TSUYOSA") + d.value;
        else if (d.type == 1) strength = cell(d, "MAMORI") + d.value + cell(d, "MAXHP") / 4;
        else strength = cell(d, "TSUYOSA") + cell(d, "MAMORI") + d.value + cell(d, "MAXHP") / 4;
        const std::pair<int, int> key{d.type, d.type == 0 ? cell(d, "SKILL") : 0};
        if (const auto it = kept.find(key); it == kept.end() || strength > it->second.first) kept[key] = {strength, int(i)};
    }
    for (std::size_t i = 0; i < p.devices.size(); ++i) {
        const auto& d = p.devices[i];
        if (d.flag || placeholder(d)) continue;
        int price = cell(d, "KAITORI") + 25 * d.value;
        if (d.type <= 1) price += sell_rank[std::clamp(d.rank, 0, 5)];
        if (price < 1) continue;
        const auto it = kept.find({d.type, d.type == 0 ? cell(d, "SKILL") : 0});
        if (it != kept.end() && it->second.second == int(i)) continue;
        return int(i);
    }
    return -1;
}

bool ride_targets_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& stage,
                       const std::string& goal, int frame, CampaignBot::Output& out) {
    const auto me = position_of(player);
    for (const auto& ride : kRideTargets) {
        if (stage != ride.stage) continue;
        // (Only on the way to the ride's goal mat; an empty goal: any.)
        if (!goal.empty() && ("|" + goal + "|").find(std::string("|") + ride.goal + "|") == std::string::npos) continue;
        auto* models = engine.get<engine::ModelCtrlManager>();
        if (!models) return false;
        // The ride's travel per frame, from its node's last two frames here.
        static int ride_frame = -100;
        static engine::Vec3f ride_at{};
        engine::Vec3f ride_v{};
        if (player.ride && player.ride->node) {
            const auto& n = player.ride->node->matrix;
            if (ride_frame == frame - 1) ride_v = {n(3, 0) - ride_at.x, n(3, 1) - ride_at.y, n(3, 2) - ride_at.z};
            ride_at = {n(3, 0), n(3, 1), n(3, 2)};
            ride_frame = frame;
        }
        const RideFrame car = ride_frame_of(player);
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
        // When an enemy blow can meet the player (blow_time), and the
        // enemies standing on the ride (in its axes; a walking one also
        // where its walk ends: it keeps its way for the rest of its AITIMER).
        Blow blow;
        std::vector<RideFoe> foes;
        for (const auto& e : models->rams()) {
            if (!e->generated || !e->node || e->is_player) continue;
            const float d = flat(position_of(*e), me);
            if (e->category == "EBULLET" && d < ride.guard) threat = true;
            if (e->category == "EBULLET" && d < 18.0f) incoming = true;
            if (e->category != "ENEMY" || e->hp <= 0 || !engine.has_script("hit_" + e->info_name + ".lua")) continue;
            if (d < ride.fight && (!foe || d < flat(position_of(*foe), me))) foe = e.get();
            if (d < ride.guard && e->seq_name.rfind("SQ_FIRE", 0) == 0) threat = true;
            const bool same_car = e->ride && player.ride && e->ride->node == player.ride->node;
            const engine::Vec3f shift = same_car ? ride_v : engine::Vec3f{};
            if (d < 40.0f) {
                const Blow b = blow_time(engine, *e, me, shift);
                blow.certain = std::min(blow.certain, b.certain);
                blow.possible = std::min(blow.possible, b.possible);
            }
            if (car.valid && std::abs(e->node->matrix(3, 1) - me.y) < 8.0f) {
                const engine::Vec3f p{e->node->matrix(3, 0) + shift.x, e->node->matrix(3, 1), e->node->matrix(3, 2) + shift.z};
                if (std::abs(car.along(p)) < 40.0f && std::abs(car.side(p)) < 40.0f) foes.push_back({car.along(p), car.side(p)});
                if (e->seq_name == "SQ_WALK" && e->ctrl_mode != 0) {
                    const float walk = kEnemyWalk * std::min(ai_frames_left(engine, *e), 40.0f);
                    const engine::Vec3f q{p.x + std::sin(e->desired[1]) * walk, p.y, p.z + std::cos(e->desired[1]) * walk};
                    if (std::abs(car.along(q)) < 40.0f && std::abs(car.side(q)) < 40.0f) foes.push_back({car.along(q), car.side(q)});
                }
            }
        }
        // A shot about to arrive is guarded against; otherwise close
        // enemies first (their touch hurts too), then the targets, else
        // the guard while something is aimed at the ride. A sign of the
        // train within `range` comes before any fight: it passes once, and
        // the mechas boarding the train (stag0203 e_mecha1, 100 HP, 35 a
        // blow) guard blows from the front (hit_e_mecha1.lua: BLTisHitCheck
        // 0 -> SQ_GUARD) and knock the player down with theirs (SQ_MDMG /
        // SQ_LDMG, then SQ_STUN: well over 100 frames, the sign passes while
        // the player lies there). So while a sign comes the player keeps
        // away from them (choose_spot, away_along: they walk 0.24 a frame on
        // the car, the player 0.7), reaches the car's edge on the sign's
        // side (`minus_x` / `plus_x`) in time and leans on the wall there
        // shortly before it, and guards (B) against a blow about to land
        // (blow.certain, a shot about to arrive): the player's hit_p_*.lua
        // takes no damage and no knock-down in SQ_GUARD (-> SQ_GUARDR). The
        // guard lasts some 15 frames (then SQ_GUARD2, without guard), so B
        // goes in only shortly before the blow. The swing's own boxes come
        // 13-21 frames after A (w_sp0203 SQ_ATCK1, kind 10000, kTrainSwing)
        // while the train carries the player on by 1.6-2.2 units a frame,
        // so A goes in ahead of time (swing_plan). Any blow before the boxes
        // cancels the swing (and the attack delay, ATDELAY, then holds the
        // next one back). A blocked blow, though, ends SQ_GUARDR with MUTEKI
        // counting down from about 30 (hit_check takes no enemy box then; a
        // knock-down leaves about 120): a press that meets the sign well
        // (two thirds of the best press's count) goes in when the player
        // stays untouchable (MUTEKI, or no blow due) until its first meeting
        // box, or as the window's last press unless a blow is certain first.
        // The Slam Attack (kSlam) meets a sign from the wall at once: its box
        // is checked from the attack's first frame, which comes a frame after
        // the second tap, so the player is open to a blow for a few frames
        // only, against some 17 for the swing (the mechas' blows land on the
        // player in 15-20 frames out of 27 when two or three of them stand
        // by, which is what knocked the player down while the swing came).
        // B has to be held already for the taps, and a B pressed with
        // control starts the guard instead: B is pressed while the player
        // has no control (struck, guarding, attacking: 8030CCDC runs no input
        // then) and kept held, so the slam is ready when control comes back.
        // From kEdgeFrom frames before the sign's first good press (by the
        // ride's clock, ride_schedule) the player keeps to the edge on the
        // sign's side (crossing the car later failed where the mechas stood
        // between), and there the slam goes in when its box meets the sign
        // within a few frames of the attack's start and no blow lands first;
        // the swing then only where MUTEKI outlasts its first meeting box.
        constexpr int kLeadHorizon = 90;
        constexpr float kPressLead = 17.0f; // A to the swing's far boxes (kTrainSwing)
        constexpr float kEdgeFrom = 300.0f, kHoldFrom = 400.0f;
        const engine::ModelCtrlManager::ModelRam* sign = ride.strike > 0.0f ? best : nullptr;
        // (Frames until the next timed target's first good press: the swing's
        // far boxes come kPressLead frames after A, and a player toward the
        // ridden model's front meets it earlier.)
        const Scheduled sched = ride.strike > 0.0f ? ride_schedule(*models, ride, frame) : Scheduled{};
        const float speed = std::max(0.5f, std::hypot(ride_v.x, ride_v.z));
        const float press_in = sched.valid && car.valid ? sched.abreast_in - car.along(me) / speed - kPressLead : 1e9f;
        if (foe && !sign) best = foe;
        if (!best && !threat) return false;
        const bool swinging = player.seq_name.rfind("SQ_ATCK", 0) == 0;
        // B held for the slam (see above): set while the player has no
        // control and kept until a guard is wanted or the sign's approach
        // ends. (While this step leaves the frames to the ExploreBot, no sign
        // in range and no enemy within `fight`, B is let go; the first press
        // after that, with control, takes a guard when a mecha comes within
        // `fight` or the sign comes in range, and the guard makes the slam
        // ready again.)
        static bool b_hold = false;
        const bool b_down = (engine.pads()[0].buttons.held & pad_button::b) != 0;
        if (!(ride.strike > 0.0f && car.valid && (sign || press_in < kHoldFrom))) b_hold = false;
        else if (player.ctrl_mode == 0) b_hold = true;
        const bool slam_ready = b_hold && b_down && player.ctrl_mode != 0 && (player.seq_name == "SQ_WAIT" || player.seq_name == "SQ_WALK");
        // The side of the car the sign passes on: listed, else (the track
        // may turn before it) the side it lies on once less than kSideFrom
        // ahead; 0: not known yet.
        constexpr float kSideFrom = 150.0f;
        int side = 0;
        if (sign && car.valid) {
            if (listed_in(std::string("|") + ride.minus_x + "|", *sign)) side = -1;
            else if (listed_in(std::string("|") + ride.plus_x + "|", *sign)) side = 1;
            else if (const auto at = nearest_hit_point(*sign, me); std::abs(car.along(at)) <= kSideFrom)
                side = car.side(at) < 0.0f ? -1 : 1;
        }
        // The swing faces across the ride toward the sign's side (a step
        // turns the player at once, turn_rate_y 7).
        float face_x = player.node->matrix(2, 0), face_z = player.node->matrix(2, 2);
        if (sign && car.valid) {
            if (side != 0) {
                face_x = car.sx * float(side);
                face_z = car.sz * float(side);
            } else {
                const auto at = nearest_hit_point(*sign, me);
                const float along = (at.x - me.x) * car.ax + (at.z - me.z) * car.az;
                const float lx = at.x - me.x - along * car.ax, lz = at.z - me.z - along * car.az;
                if (const float d = std::hypot(lx, lz); d > 0.5f) {
                    face_x = lx / d;
                    face_z = lz / d;
                }
            }
        }
        const SwingPlan plan = sign ? swing_plan(player, *sign, ride_v, face_x, face_z, kLeadHorizon) : SwingPlan{};
        const int lead = plan.lead;
        // The untouchable moments are short (MUTEKI 30 against a first
        // meeting box 16-23 frames after A): besides across, the swing may
        // face 30 or 60 degrees toward or away from the ride's travel, which
        // meets the sign earlier or later. The facing whose press now meets
        // the sign best (two thirds of its best press's count or more) while
        // the player stays untouchable until its first meeting box is taken.
        const bool at_edge = sign && side != 0 && car.valid && car.side(me) * float(side) >= 24.0f;
        float swing_x = face_x, swing_z = face_z;
        bool swing_now = false;
        if (sign) {
            int best_now = 0;
            for (const float turn : {0.0f, 0.5236f, -0.5236f, 1.0472f, -1.0472f}) {
                const float c = std::cos(turn), s = std::sin(turn);
                const float fx = face_x * c - face_z * s, fz = face_x * s + face_z * c;
                const SwingPlan p = turn == 0.0f ? plan : swing_plan(player, *sign, ride_v, fx, fz, kLeadHorizon);
                const bool meets_well = p.now > 0 && 3 * p.now >= 2 * p.most;
                // (The last press of the window that meets well goes in
                // unless a blow is certain to come first.)
                const bool last_chance = p.last >= 0 && p.last <= 3;
                const bool untouchable = player.muteki > float(p.first) || blow.due() > float(p.first) ||
                                         (last_chance && blow.certain > float(p.first));
                if (meets_well && untouchable && p.now > best_now && (!(slam_ready && at_edge) || player.muteki > float(p.first))) {
                    best_now = p.now;
                    swing_x = fx;
                    swing_z = fz;
                    swing_now = true;
                }
            }
        }
        // (The press waits for the player to face that way: a step turns it
        // at once.)
        const bool facing_swing = player.node->matrix(2, 0) * swing_x + player.node->matrix(2, 2) * swing_z > 0.95f;
        // No guard while MUTEKI outlasts the blow due (a guard that blocks
        // nothing only holds the player and ends in SQ_GUARD2); B goes in
        // kGuardLead frames or less before the blow (the guard starts a frame
        // after the press).
        constexpr float kGuardLead = 8.0f;
        // The slam: the taps go in on the direction pad (game_main turns
        // them into a stick in the middle of a direction zone: the same mask
        // both times, stick_mask), the one nearest across toward the sign,
        // and it starts `slam_wait` frames from now (the taps still to come,
        // from the player's slam state, 8034C770). It goes in when its box
        // meets the sign within kSlamFirst frames of its start, no blow can
        // land before that (or MUTEKI outlasts it), and it meets no unwanted
        // target.
        constexpr int kSlamFirst = 3;
        bool slam_now = false;
        std::uint32_t slam_tap = 0;
        int slam_wait = 0;
        SwingPlan slam_plan;
        if (slam_ready && at_edge && !hold) {
            const float heading = camera_heading(engine);
            const float want = std::atan2(car.sx * float(side), car.sz * float(side)) - heading;
            const float r = float(std::lround(want / 0.7853982f)) * 0.7853982f;
            const float tx = std::sin(r) > 0.5f ? 1.0f : std::sin(r) < -0.5f ? -1.0f : 0.0f;
            const float ty = -std::cos(r) > 0.5f ? 1.0f : -std::cos(r) < -0.5f ? -1.0f : 0.0f;
            slam_tap = (tx > 0.0f ? pad_button::right : tx < 0.0f ? pad_button::left : 0u) |
                       (ty > 0.0f ? pad_button::up : ty < 0.0f ? pad_button::down : 0u);
            const std::uint32_t mask = stick_mask(tx, ty);
            const float yaw = heading + std::atan2(tx, -ty);
            slam_wait = player.slam_state == 3 ? (player.slam_direction == mask ? 1 : 5) : player.slam_state == 2 ? 2
                        : player.slam_state == 1                                  ? 3
                                                                                  : 4;
            slam_plan = swing_plan(player, *sign, ride_v, std::sin(yaw), std::cos(yaw), 0, kSwingDepth, kSlam, slam_wait);
            bool unwanted_slam = false;
            for (const auto& e : models->rams()) {
                if (!e->generated || !e->node || e->hp <= 0 || listed_in(prefixes, *e) || !listed_in(avoid, *e)) continue;
                if (e->seq_name == "SQ_SWON" || e->seq_name == "SQ_SWOF" || e->seq_name == "SQ_POWEROF") continue;
                if (flat(nearest_hit_point(*e, me), me) > 90.0f) continue;
                if (swing_plan(player, *e, ride_v, std::sin(yaw), std::cos(yaw), 0, -1.0f, kSlam, slam_wait).now > 0) unwanted_slam = true;
            }
            const bool safe = player.muteki > float(slam_plan.first) || blow.due() > float(slam_plan.first);
            slam_now = slam_plan.now > 0 && slam_plan.first <= slam_wait + kSlamFirst && safe && !unwanted_slam;
        }
        const bool guard_now = sign && !slam_now && !swing_now && !swinging && (incoming || blow.certain <= kGuardLead) &&
                               player.muteki <= blow.certain + 3.0f;
        // Frames until the sign's first good press (by the swing's plan
        // within its horizon, else by the ride's clock, else by distance).
        const float until = !sign            ? 1e9f
                            : plan.most > 0 ? float(plan.lead)
                            : sched.valid && sched.name == sign->info_name + "#" + std::to_string(sign->index)
                                ? std::max(0.0f, press_in)
                                : (flat(nearest_hit_point(*sign, me), me) - 25.0f) / speed;
        float keep_along = 0.0f, keep_side = 0.0f;
        if (best && !guard_now && (!incoming || swing_now)) {
            // Toward the nearest point of its hit box (on a ride: to the
            // ride's edge facing it), swinging once it is within reach.
            auto at = ride.strike > 0.0f && best != foe ? nearest_hit_point(*best, me) : position_of(*best);
            if (sign && car.valid) {
                // On the car, inside its walls (they stop the player without
                // sliding it along them): while the sign is still far
                // (`until`, frames until its first good press) the spot with
                // the most room to the mechas from which the edge on the
                // sign's side is still reached in time (choose_spot), then
                // that edge at the point along it farthest from them (the car
                // is some 55 units wide, and the track may turn before a sign
                // and bring it to the other side: gate sign 1 comes up
                // ahead-left and passes on the right). Shortly before the
                // sign the player leans on the wall: the swing reaches
                // farther out (gate signs stand 35 off the car's middle, the
                // wall stops the player at 29.4) and the wall holds the
                // player in place. The car's middle line while the side is
                // not known.
                const float edge = float(side) * kEdgeSide, me_s = car.side(me);
                const float cross = std::abs(me_s - edge) / kWalkSpeed + kCrossSpare;
                keep_side = edge;
                keep_along = away_along(car.along(me), keep_side, foes);
                static Spot spot{0.0f, 0.0f};
                static int spot_frame = -100;
                if (side != 0) {
                    // (Chosen again every few frames.)
                    if (frame - spot_frame >= 6 || frame < spot_frame) {
                        spot = choose_spot(car.along(me), me_s, foes, side, until, spot);
                        spot_frame = frame;
                    }
                    if (until > cross + kSpotSpare && until >= kEdgeFrom) {
                        keep_along = spot.along;
                        keep_side = spot.side;
                    }
                    if (until <= kLeanLead && std::abs(me_s - edge) < 6.0f) keep_side = float(side) * (kEdgeSide + 8.0f);
                }
                at = car.world(keep_along, keep_side, me.y);
            }
            if (swing_now) {
                // A step the way the swing is to face.
                at.x = me.x + swing_x * 10.0f;
                at.z = me.z + swing_z * 10.0f;
            }
            out.walk = true;
            out.x = at.x;
            out.z = at.z;
            out.reach = 4.0f;
            // (A is read on its press: every other frame while a press meets
            // the sign. Nor a press whose swing would meet an unwanted target
            // while the ride carries the player on, a sign of the other way
            // at a junction: the swing faces the way the player walks this
            // frame, a step turning it at once, or the way it faces.)
            const auto unwanted = [&](float fx, float fz) {
                if (ride.strike <= 0.0f) return false;
                for (const auto& e : models->rams()) {
                    if (!e->generated || !e->node || e->hp <= 0 || listed_in(prefixes, *e) || !listed_in(avoid, *e)) continue;
                    if (e->seq_name == "SQ_SWON" || e->seq_name == "SQ_SWOF" || e->seq_name == "SQ_POWEROF") continue;
                    if (flat(nearest_hit_point(*e, me), me) > 90.0f) continue;
                    if (swing_plan(player, *e, ride_v, fx, fz, 0, -1.0f).now > 0) return true;
                }
                return false;
            };
            const bool press = sign ? swing_now && facing_swing && frame % 2 == 0 : frame % 12 < 3;
            float press_x = player.node->matrix(2, 0), press_z = player.node->matrix(2, 2);
            if (sign) {
                press_x = swing_x;
                press_z = swing_z;
            } else if (const float d = std::hypot(at.x - me.x, at.z - me.z); d * d > out.reach && d > 0.01f) {
                press_x = (at.x - me.x) / d;
                press_z = (at.z - me.z) / d;
            }
            if (!hold && press && !slam_now && !unwanted(press_x, press_z) && !unwanted(player.node->matrix(2, 0), player.node->matrix(2, 2)))
                out.buttons |= pad_button::a;
            if (slam_now) {
                // (The taps: a direction from the middle, the middle, the
                // same direction again, by the player's slam state.)
                out.walk = false;
                if (player.slam_state == 1 || player.slam_state == 3) out.buttons |= slam_tap;
            }
        } else {
            // (B is read on its press: pulsed so the guard is taken again;
            // against a blow about to land every other frame.)
            if (guard_now ? frame % 2 == 0 : frame % 8 < 4) out.buttons |= pad_button::b;
        }
        // B held for the slam; a guard wanted with control lets go of it for
        // a frame first (the guard needs a press). A walk that would end the
        // slam's taps by chance (the second tap's direction while the tap
        // timer runs) waits for the timer instead.
        if (guard_now && player.ctrl_mode != 0 && b_hold) {
            b_hold = false;
            out.buttons &= ~pad_button::b;
        }
        if (b_hold) out.buttons |= pad_button::b;
        if (b_hold && !slam_now && player.slam_state == 3 && out.walk) {
            const float dx = out.x - me.x, dz = out.z - me.z;
            if (dx * dx + dz * dz > out.reach) {
                const float r = std::atan2(dx, dz) - camera_heading(engine);
                if (stick_mask(std::sin(r), -std::cos(r)) == player.slam_direction) out.walk = false;
            }
        }
        // DW4_TRACE_RIDE=5: the sign's hit boxes and the player's live attack
        // boxes in the world, every frame within 60 units of the sign.
        if (static const char* t = std::getenv("DW4_TRACE_RIDE"); t && std::string_view(t) == "5" && sign && sign->hit &&
            flat(nearest_hit_point(*sign, me), me) < 60.0f) {
            const auto& m = sign->node->matrix;
            for (const auto& [lo, hi] : sign->hit->boxes)
                std::printf("[%d] sign %s#%d local (%.1f..%.1f, %.1f..%.1f) origin (%.1f,%.1f) yaw x (%.2f,%.2f)\n", frame, sign->info_name.c_str(),
                            sign->index, lo.x, hi.x, lo.z, hi.z, m(3, 0), m(3, 2), m(0, 0), m(0, 2));
            const auto& p = player.node->matrix;
            std::printf("[%d] player (%.1f,%.1f) facing (%.2f,%.2f) v (%.2f,%.2f) %s lead %d\n", frame, me.x, me.z, p(2, 0), p(2, 2), ride_v.x, ride_v.z,
                        player.seq_name.c_str(), lead);
            for (const auto& h : player.hitboxes)
                if (h.kind / 10000 == 1 && h.elapsed >= h.delay && h.elapsed < h.end)
                    std::printf("[%d]   live box off (%.0f,%.0f) at (%.1f,%.1f) elapsed %.0f\n", frame, h.offset.x, h.offset.z,
                                p(3, 0) + h.offset.x * p(0, 0) + h.offset.z * p(2, 0), p(3, 2) + h.offset.x * p(0, 2) + h.offset.z * p(2, 2), h.elapsed);
        }
        if (static const char* t = std::getenv("DW4_TRACE_RIDE"); t && std::string_view(t) == "4" && sign && sign->hit && frame % 30 == 0) {
            const auto& m = sign->node->matrix;
            for (const auto& [lo, hi] : sign->hit->boxes)
                std::printf("[%d] sign %s#%d box y %.1f..%.1f (origin y %.1f) player y %.1f muteki %.0f\n", frame, sign->info_name.c_str(), sign->index,
                            lo.y + m(3, 1), hi.y + m(3, 1), m(3, 1), me.y, player.muteki);
        }
        if (static const char* t = std::getenv("DW4_TRACE_RIDE"); t && std::string_view(t) == "4" && player.ride && player.ride->node && frame % 30 == 0) {
            const auto& n = player.ride->node->matrix;
            std::printf("[%d] ridden %s at (%.0f,%.0f) x (%.2f,%.2f) z (%.2f,%.2f) last (%.0f,%.0f)\n", frame, player.ride->name.c_str(), n(3, 0), n(3, 2),
                        n(0, 0), n(0, 2), n(2, 0), n(2, 2), player.ride->last(3, 0), player.ride->last(3, 2));
        }
        if (static const char* t = std::getenv("DW4_TRACE_RIDE"); t && std::string_view(t) == "3")
            for (const auto& e : models->rams())
                if (e->generated && e->node && flat(position_of(*e), me) < 30.0f)
                    for (const auto& h : e->hitboxes)
                        std::printf("[%d] box %s#%d %s kind %d delay %.0f end %.0f elapsed %.0f size (%.0f,%.0f,%.0f) off (%.0f,%.0f,%.0f)\n", frame,
                                    e->info_name.c_str(), e->index, e->seq_name.c_str(), h.kind, h.delay, h.end, h.elapsed, h.size.x,
                                    h.size.y, h.size.z, h.offset.x, h.offset.y, h.offset.z);
        // DW4_TRACE_RIDE: what the ride step does, every sixth frame (any
        // other value than 1: every frame); the player's place in the car's
        // axes, the place it keeps to, the blows due (`c`: certain), the
        // enemies near with sequence, distance and HP.
        if (static const bool on = std::getenv("DW4_TRACE_RIDE") != nullptr; on && (frame % 6 == 0 || std::string_view(std::getenv("DW4_TRACE_RIDE")) != "1")) {
            std::string near;
            for (const auto& e : models->rams())
                if (e->generated && e->node && ((e->category == "ENEMY" && e->hp > 0) || e->category == "EBULLET") && flat(position_of(*e), me) < 30.0f)
                    near += " " + e->info_name + "#" + std::to_string(e->index) + ":" + e->seq_name + "@" +
                            std::to_string(int(flat(position_of(*e), me) * 10.0f) / 10.0f).substr(0, 4) + "/" + std::to_string(e->hp);
            if (sign && car.valid && std::string_view(std::getenv("DW4_TRACE_RIDE")) == "7") {
                const auto q = nearest_hit_point(*sign, me);
                std::printf("[%d] SIGN %s#%d car (%.1f,%.1f)\n", frame, sign->info_name.c_str(), sign->index, car.along(q), car.side(q));
            }
            // (The timed target, frames until its first good press, B held /
            // the slam ready, and the slam's plan.)
            if (sched.valid)
                std::printf("[%d] plan %s press_in %.0f%s%s slam %d first %d wait %d state %d\n", frame, sched.name.c_str(), press_in,
                            b_hold ? " hold" : "", slam_ready ? " ready" : "", slam_plan.now, slam_plan.now ? slam_plan.first : -1, slam_wait,
                            player.slam_state);
            std::printf("[%d] ride at (%.0f,%.0f,%.0f) car (%.1f,%.1f) keep (%.1f,%.1f) %s %s: %s#%d %.1f off lead %d now %d/%d first %d blow %.0f c%.0f mu %.0f%s%s%s%s |%s\n",
                        frame, me.x, me.y, me.z, car.along(me), car.side(me), keep_along, keep_side, player.seq_name.c_str(),
                        best && best == foe ? "foe" : guard_now ? "guard" : slam_now ? "slam" : "target", best ? best->info_name.c_str() : "-", best ? best->index : -1,
                        best ? flat(nearest_hit_point(*best, me), me) : 0.0f, lead, plan.now, plan.most, plan.now ? plan.first : -1,
                        std::min(blow.due(), 99.0f), std::min(blow.certain, 99.0f), player.muteki, hold ? " hold" : "", threat ? " threat" : "",
                        incoming ? " incoming" : "", out.buttons & pad_button::a ? " A" : out.buttons & pad_button::b ? " B" : "", near.c_str());
        }
        return true;
    }
    return false;
}

// "<model>#<n>" names one RAM, "<model>#*" the nearest of that model that
// still has HP (and is not left alone for now, target_skip_ until `frame`).
const engine::ModelCtrlManager::ModelRam* CampaignBot::find_model(engine::Engine& engine, const std::string& spec, const engine::Vec3f& near,
                                                                  int frame) const {
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
        if (const auto it = target_skip_.find(ram.get()); any && it != target_skip_.end() && frame < it->second) continue;
        if (any && ram->hp > 0 && (!best || flat(position_of(*ram), near) < flat(position_of(*best), near))) best = ram.get();
    }
    return best;
}

// The maze region the player is in: of the regions whose exit mats a walk
// map from the player reaches, the one that reaches the most (a region a
// ledge leads down to reaches less than the one above it); empty when no
// exit mat is reached.
std::string locate_region(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& stage) {
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (!hitland || !mats) return {};
    const WithoutLimitCircle without_limit(*hitland);
    const WalkMap map(hitland->world(), position_of(player), 1400.0f, hitland->delivery_flags(player.info_name, player.index), {},
                      hitland->delivery_width(player.info_name, player.index));
    std::string best;
    std::size_t most = 0;
    for (const auto& region : kMazeRegions) {
        if (stage != region.stage || !*region.mats) continue;
        bool reached = false;
        for (const auto& box : mats->mat_boxes())
            if (in_list(region.mats, box.name) && map.reached_inside(box.min.x, box.min.z, box.max.x, box.max.z)) reached = true;
        if (!reached) continue;
        const auto reach = maze_reach(engine, stage, region.name).size();
        if (reach > most) {
            most = reach;
            best = region.name;
        }
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
    // Survival: in the lobby a purchase due at the disk shop comes first
    // (kSupplies; the shop sells from phase 2 on, flag 1 off).
    static constexpr Objective kShopErrand{"restock at the disk shop", "", "stagml13", "shop:n_digilff#0"};
    // (Not in god mode: the cheat runs need no disks.)
    // Before that, a full heal (HP or MP short): the X-Data shop refills both
    // when it is left (beNDMWShopCtrl10 80393B74, state 2: RECALCPLAYER, HP =
    // MHP, MP = MMP), stagML12 n_digilfm#1 (bML01003.lua -> xdatashop.lua).
    static constexpr Objective kHealErrand{"full heal at the X-Data shop", "", "stagml12", "heal:n_digilfm#1"};
    const auto* slot = engine.get<engine::GameSystem>() ? &engine.get<engine::GameSystem>()->players[0] : nullptr;
    const bool hurt = slot && (slot->hp * 100 < slot->max_hp * kLobbyHealBelow || slot->mp * 100 < slot->max_mp * kLobbyHealBelow);
    // Then a better device to put on (next_equip) at the Digi-Lab, stagML12
    // n_digilff#1 (n_digilff_01.lua: bML01004.lua "use the lab?" ->
    // digilabshop.lua while flags 1 and 409 are off). (Also in god mode, the
    // blows dealt count there too; not with the one-hit cheat, which makes
    // them all the same and keeps the recorded cheat run's path.)
    static constexpr Objective kEquipErrand{"equip at the Digi-Lab", "", "stagml12", "equip:n_digilff#1"};
    if (!engine.cheats().god && lobby(here) && !holds(engine, "F1") && hurt) objective = &kHealErrand;
    else if (!engine.cheats().one_hit && lobby(here) && !holds(engine, "F1|F409") && next_equip(engine).device >= 0)
        objective = &kEquipErrand;
    else if (!engine.cheats().god && lobby(here) && !holds(engine, "F1") &&
             (next_purchase(engine).record >= 0 || (slot && int(slot->devices.size()) >= kSellFrom && next_sale(engine) >= 0)))
        objective = &kShopErrand;
    // Then a look at the weapon and the armor shop (next_gear) once per level.
    static constexpr Objective kWeaponErrand{"gear from the weapon shop", "", "stagml13", "gear:n_digilfm#0"};
    static constexpr Objective kArmorErrand{"gear from the armor shop", "", "stagml13", "gear:n_digilff#1"};
    if (objective != &kHealErrand && objective != &kEquipErrand && objective != &kShopErrand && !engine.cheats().god &&
        lobby(here) && !holds(engine, "F1") && slot && slot->bits >= kGearBits && slot->devices.size() < 36) {
        if (gear_checked_[0] != slot->stats.level) objective = &kWeaponErrand;
        else if (gear_checked_[1] != slot->stats.level) objective = &kArmorErrand;
    }
    // Training (no god mode): below the objective's gate level the player
    // trains in the area's field first (kTraining, kGates).
    else if (!engine.cheats().god)
        if (const auto* training = train_for(engine, *objective, defeat_levels_)) objective = training;
    // The maze region of a place: from the point it was entered at, for the
    // place the player is in the region found by walking (region_now_: a
    // ledge walked off leaves the region entered).
    const auto place_region = [&](std::string_view stage, int entry) -> const char* {
        if (stage == here && entry == entry_ && !region_now_.empty()) return region_now_.c_str();
        return point_region(stage, entry);
    };
    // Whether a place (stage, the player point it was entered at) is where
    // the objective is done.
    const auto at_objective = [&](const std::string& stage, int entry) {
        if (stage != objective->stage) return false;
        if (objective->region && maze_floor(stage)) {
            const char* from = place_region(stage, entry);
            return from && maze_reach(engine, stage, from).count(objective->region) > 0;
        }
        return objective->entry < 0 || objective->entry == entry;
    };
    if (at_objective(here, entry_)) return Step{here, objective->action, -1, objective->name};
    // The cheapest way over the usable exits from here to the objective's
    // stage (entered at its point, if it names one): a stage change costs 1,
    // one into the lobby less (a cleared dungeon is left by its return gate
    // rather than walked back through). A place is a stage and the point it
    // was entered at (-1 where no exit names one).
    const auto areas = transporter_areas(engine);
    // (A maze place swept with the flags as they are now leads nowhere new.)
    const auto flags_now = flags_hash(engine);
    auto usable = [&](const Exit& exit, int entry) {
        if (std::string_view(exit.when) == "transporter")
            return std::find(areas.begin(), areas.end(), exit.target) != areas.end();
        if (const auto it = swept_.find({exit.target, exit.entry}); it != swept_.end() && it->second == flags_now) return false;
        // On a maze floor the exit's region has to be reached from the
        // region the floor was entered in.
        if (maze_floor(exit.stage)) {
            const char* from = place_region(exit.stage, entry);
            const char* to = mat_region(exit.stage, exit.mat);
            if (from && to && !maze_reach(engine, exit.stage, from).count(to)) return false;
        }
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
        if (at_objective(at->first, at->second)) {
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
    // Every usable mat of this stage leading there (to that player point)
    // is a goal.
    Step step{here, "", -1, objective->name};
    std::string mats;
    for (const auto& exit : kExits)
        if (here == exit.stage && next.first == exit.target && usable(exit, entry_) && next.second == exit.entry) {
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
    // The form an evolve step wants, kept while the lab program runs (the
    // step moves on as soon as the form changes, the lab's closing menus
    // still need their answers).
    if (step.action.rfind("evolve:", 0) == 0) {
        lab_target_ = std::atoi(step.action.c_str() + 7);
    } else if (lab_target_ >= 0) {
        engine::Packet running;
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, "ISRUN", running);
        if (running.bools.empty() || !running.bools[0]) {
            lab_target_ = -1;
            lab_leaving_ = false;
        }
    }
    // A shop errand's program keeps its answers while it runs (the errand
    // ends with the last purchase, the shop's menus still need theirs).
    if (step.action.rfind("shop:", 0) == 0) {
        shop_open_ = true;
    } else if (shop_open_) {
        engine::Packet running;
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, "ISRUN", running);
        if (running.bools.empty() || !running.bools[0]) shop_open_ = false;
    }
    if (step.action.rfind("heal:", 0) == 0) {
        heal_open_ = true;
    } else if (heal_open_) {
        engine::Packet running;
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, "ISRUN", running);
        if (running.bools.empty() || !running.bools[0]) heal_open_ = false;
    }
    // The equipment errand's lab program likewise (next_equip).
    if (step.action.rfind("equip:", 0) == 0) {
        equip_open_ = true;
    } else if (equip_open_) {
        engine::Packet running;
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, "ISRUN", running);
        if (running.bools.empty() || !running.bools[0]) equip_open_ = false;
    }
    // The gear errand's shop program likewise; once it has run and closed the
    // shop is looked at for this level (next_gear).
    if (step.action.rfind("gear:", 0) == 0) {
        gear_open_ = true;
        gear_program_ = step.action.find("n_digilfm") != std::string::npos ? 6 : 7;
    }
    if (gear_open_) {
        engine::Packet running;
        engine.messenger().send(engine, "SUBWND", std::nullopt, -1, "ISRUN", running);
        if (!running.bools.empty() && running.bools[0]) {
            gear_ran_ = true;
        } else if (gear_ran_ || step.action.rfind("gear:", 0) != 0) {
            if (gear_ran_)
                if (auto* sys = engine.get<engine::GameSystem>()) gear_checked_[std::size_t(gear_program_ - 6)] = sys->players[0].stats.level;
            gear_open_ = gear_buying_ = gear_ran_ = false;
        }
    }
    // A device list's page, counted from its opening (the lab's DeviceList
    // and the shop's DeviceSell, 12 devices a page: RIGHT / LEFT turn it
    // while the cursor rests, and the turn sends the cursor to the top,
    // FO2TOP, which takes no input while its SQ_FO2TOP plays: that marks a
    // turn taken).
    const engine::ModelCtrlManager::ModelRam* device_list = nullptr;
    if (equip_open_)
        if (auto* models = engine.get<engine::ModelCtrlManager>())
            if (const auto* folder = models->find("i_stfolder", 0); folder && folder->generated) device_list = folder;
    if (device_list && !equip_folder_) list_page_ = 0, list_turn_ = 0;
    equip_folder_ = device_list != nullptr;
    if (list_turn_ != 0 && frame - list_turn_at_ > 40) list_turn_ = 0; // (not taken: pressed again)
    // The page of a device list's folder index: true while a turn toward it
    // is pressed or awaited (RIGHT / LEFT once the cursor rests).
    const auto turn_to = [&](int index) {
        if (index / 12 == list_page_) return false;
        if (list_turn_ != 0 || frame - menu_pressed_ < 14) return true;
        list_turn_ = index / 12 > list_page_ ? 1 : -1;
        list_turn_at_ = frame;
        out.buttons |= list_turn_ > 0 ? pad_button::right : pad_button::left;
        menu_pressed_ = frame;
        if (trace()) std::printf("[%d] campaign menu device list page %d -> %s\n", frame, list_page_, list_turn_ > 0 ? "RIGHT" : "LEFT");
        return true;
    };
    for (const auto& menu : selects->open_selects()) {
        if (menu.index != 0 && menu.index != 4) continue;
        if (!menu.taking_input) {
            // (A page turn of a device list taken, see above.)
            if (list_turn_ != 0 && menu.name == "i_stcursor01") {
                list_page_ += list_turn_;
                list_turn_ = 0;
            }
            return true; // opening or closing: hands off
        }
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
        } else if (heal_open_ && !equip_open_ && !gear_open_) { // (the lab or a device shop first: an errand that follows waits)
            // The X-Data shop: "use it?" yes (i_messcr, 2 lines), then Exit
            // of Stats / Chips / Exit (i_stmesscr, 3 lines); a list is backed
            // out of.
            if (menu.name == "i_stmesscr" && menu.items >= 3) want = menu.items - 1;
            else if (menu.name.rfind("i_stcursor", 0) == 0) cancel = true;
        } else if (shop_open_ && !equip_open_ && !gear_open_) {
            // The disk shop (ShopDisk 803967A8): "buy?" yes (i_messcr, 2
            // lines), Buy / Sell / Exit (i_stmesscr, 3 lines: Buy while a
            // purchase is due, else Exit), the list of conItem 0..9
            // (i_stcursor01: the record, else back with B), the count
            // (i_stcursor05: line c buys 9 - held - c, 80397ABC) and yes to
            // the price (i_stmesscr, 2 lines). Sales (next_sale) first: Sell,
            // Devices of Devices / Disks (i_stmesscr, 2 lines; backed out of
            // when nothing is left to sell), the device's page and line in
            // the sale list (i_stcursor01) and yes (i_stmesscr, 2 lines).
            const auto buy = next_purchase(engine);
            const int sale = next_sale(engine);
            if (menu.name == "i_stmesscr" && menu.items >= 3) want = sale >= 0 ? 1 : buy.record >= 0 ? 0 : menu.items - 1;
            else if (shop_selling_ && menu.name == "i_stmesscr") {
                if (!sale_confirm_ && sale < 0) cancel = true;
            } else if (shop_selling_ && menu.name == "i_stcursor01") {
                if (sale_at_ >= 0) {
                    // (A sale of the last device of the last page, on its
                    // first line, turns the list back a page, 803A01F0.)
                    auto* sys = engine.get<engine::GameSystem>();
                    if (sys && sale_at_ >= int(sys->players[0].devices.size()) && sale_at_ % 12 == 0 && list_page_ > 0) --list_page_;
                    sale_at_ = -1;
                }
                if (sale < 0) cancel = true;
                else if (turn_to(sale)) return true;
                else want = sale % 12;
            } else if (menu.name == "i_stcursor01") {
                if (buy.record >= 0) want = buy.record;
                else cancel = true;
            } else if (menu.name == "i_stcursor05" && buy.record >= 0) {
                want = std::clamp(9 - buy.held - buy.count, 0, std::max(menu.items - 1, 0));
            }
        } else if (equip_open_) {
            // The Digi-Lab's equipment (next_equip): "use the lab?" and
            // "leave the lab?" yes (i_messcr, 2 lines), the lab menu Equip
            // while a pick is due, else Exit (3 lines); Power Socket / Main
            // Slot / Sub Slot (Select01, i_stcursor02) the pick's kind, the
            // slot cursor (i_stcursor01) its first slot, Change of Change /
            // Chips / Remove (i_stmesscr), and in the device list (i_stfolder
            // up) the pick's page and line; everything else backed out of
            // once the pick is done or of another kind.
            const auto pick = next_equip(engine);
            const bool mine = pick.device >= 0 && pick.kind == equip_kind_;
            if (menu.name == "i_messcr") {
                want = menu.items >= 3 ? (pick.device >= 0 ? 1 : menu.items - 1) : 0;
            } else if (menu.name == "i_stcursor02") {
                if (pick.device >= 0) want = pick.kind;
                else cancel = true;
            } else if (menu.name == "i_stcursor01" && device_list) {
                if (!mine) {
                    cancel = true;
                } else if (turn_to(pick.device)) {
                    return true;
                } else {
                    want = pick.device % 12;
                }
            } else if (menu.name == "i_stcursor01" || menu.name == "i_stmesscr") {
                if (mine) want = 0;
                else cancel = true;
            }
        } else if (gear_open_) {
            // The weapon / armor shop (ShopDevice 803A6914, next_gear): "use
            // the shop?" yes (i_messcr, 2 lines), Buy / Sell / Exit
            // (i_stmesscr, 3 lines: Buy while a device is wanted), the
            // stock's page and line (i_stcursor01), the message (A) and yes
            // (i_stmesscr, 2 lines); the list is backed out of when nothing
            // more is wanted.
            const int gear = next_gear(engine, gear_program_);
            if (menu.name == "i_stmesscr" && menu.items >= 3) want = gear >= 0 ? 0 : menu.items - 1;
            else if (menu.name == "i_stcursor01" && gear_buying_) {
                if (gear < 0) cancel = true;
                else if (turn_to(gear)) return true;
                else want = gear % 12;
            } else if (menu.name.rfind("i_stcursor", 0) == 0) {
                cancel = true;
            }
        } else if (lab_target_ >= 0) {
            // The Digi-Lab (bML01004.lua, beNDMWShopCtrl20 / ShopCtrlEvolve):
            // "use the lab?" and later "equip now?" (2 lines, i_messcr), the
            // lab menu Digivolve / Equip / Exit (3 lines), the forms reached
            // (i_stcursor01: shinka row 3 first, then ascending), the two
            // confirmations (i_stmesscr, 0 = yes); once the form is the
            // wanted one: no equipping, Exit, and yes to "leave the lab?"
            // (ShopSelect02 mode 1268 after Exit; any other answer returns
            // to the lab menu).
            const int target = lab_target_;
            auto* sys = engine.get<engine::GameSystem>();
            const bool done = sys && sys->players[0].stats.species == target;
            if (menu.name == "i_messcr") {
                if (menu.items >= 3) {
                    want = done ? menu.items - 1 : 0;
                    lab_leaving_ = done;
                } else {
                    want = done && !lab_leaving_ ? menu.items - 1 : 0;
                }
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
        if (!cancel && menu.cursor == want) {
            // What an A here starts: the lab's slot kind chosen (for the slot
            // windows that follow); in the disk shop Sell or not, the sale
            // list opening (Devices) or a device sold (yes), a device picked.
            if (equip_open_ && menu.name == "i_stcursor02") equip_kind_ = want;
            if (gear_open_ && !equip_open_ && !heal_open_ && !shop_open_) {
                if (menu.name == "i_stmesscr" && menu.items >= 3) {
                    gear_buying_ = want == 0;
                    list_page_ = 0;
                    list_turn_ = 0;
                } else if (gear_buying_ && menu.name == "i_stcursor01") {
                    if (auto* windows = engine.get<engine::SubWindowManager>(); windows && trace())
                        if (const auto* stock = windows->shop_stock(gear_program_))
                            if (const int at = 12 * list_page_ + want; at >= 0 && at < int(stock->size()) && (*stock)[std::size_t(at)])
                                if (auto* db = engine.get<engine::DatabaseManager>())
                                    std::printf("[%d] campaign: buys %s (stock line %d)\n", frame,
                                                db->text(engine::device_table(*(*stock)[std::size_t(at)]), (*stock)[std::size_t(at)]->record, "NAME").c_str(), at);
                }
            }
            if (shop_open_ && !equip_open_ && !heal_open_ && !gear_open_) {
                if (menu.name == "i_stmesscr" && menu.items >= 3) {
                    shop_selling_ = want == 1;
                    sale_confirm_ = false;
                } else if (shop_selling_ && menu.name == "i_stmesscr") {
                    if (sale_confirm_) {
                        if (auto* sys = engine.get<engine::GameSystem>(); sys && trace() && sale_target_ >= 0 &&
                                                                          sale_target_ < int(sys->players[0].devices.size()))
                            if (auto* db = engine.get<engine::DatabaseManager>())
                                std::printf("[%d] campaign: sells %s (device %d)\n", frame,
                                            db->text(engine::device_table(sys->players[0].devices[std::size_t(sale_target_)]),
                                                     sys->players[0].devices[std::size_t(sale_target_)].record, "NAME")
                                                .c_str(),
                                            sale_target_);
                        sale_at_ = sale_target_;
                        sale_confirm_ = false;
                    } else {
                        list_page_ = 0;
                        list_turn_ = 0;
                    }
                } else if (shop_selling_ && menu.name == "i_stcursor01") {
                    sale_confirm_ = true;
                    sale_target_ = 12 * list_page_ + want;
                }
            }
        }
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
    // (A walk map costs up to a third of a second on a large stage: sized to
    // the distance, and rebuilt at most every 15 frames for a moving goal or
    // a route walked to its end, else every second.)
    const int since = frame - route_frame_;
    if (hitland && ((since >= 15 && (flat(goal, route_goal_) > 8.0f || route_next_ >= route_.size())) || since > 60)) {
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
        const float range = std::clamp(flat(me, goal) * 1.5f + 80.0f, 150.0f, 700.0f);
        const WalkMap map(hitland->world(), me, range, hitland->delivery_flags(player.info_name, player.index), avoid,
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

// In a maze floor: after 15 seconds with nothing left to explore and the goal
// out of reach (or seven minutes in the place: waves that come back keep
// the ExploreBot busy), the reachable exit into the least visited place
// becomes the goal (see kMazes); a goal reached again starts over.
void CampaignBot::maze_turn(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& goal, int frame) {
    if (maze_since_ < 0) maze_since_ = frame;
    if (!explorer_.exhausted()) exhausted_since_ = -1;
    else if (exhausted_since_ < 0) exhausted_since_ = frame;
    if ((exhausted_since_ < 0 || frame - exhausted_since_ < 900) && frame - maze_since_ < 25000) return;
    exhausted_since_ = -1;
    maze_since_ = frame;
    swept_[{stage_, entry_}] = flags_hash(engine);
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (!hitland || !mats) return;
    const auto me = position_of(player);
    const WithoutLimitCircle without_limit(*hitland);
    const WalkMap map(hitland->world(), me, 1400.0f, hitland->delivery_flags(player.info_name, player.index), {},
                      hitland->delivery_width(player.info_name, player.index));
    auto reachable = [&](const std::string& mat) {
        for (const auto& box : mats->mat_boxes())
            if (box.name == mat && map.reached_inside(box.min.x, box.min.z, box.max.x, box.max.z)) return true;
        return false;
    };
    const Exit* best = nullptr;
    int fewest = 1 << 30;
    for (const auto& exit : kExits) {
        if (stage_ != exit.stage || ("|" + goal + "|").find(std::string("|") + exit.mat + "|") != std::string::npos) continue;
        if (!holds(engine, exit.when, entry_) || !reachable(exit.mat)) continue;
        const auto it = place_visits_.find({exit.target, exit.entry});
        const int visits = it == place_visits_.end() ? 0 : it->second;
        if (visits < fewest) {
            fewest = visits;
            best = &exit;
        }
    }
    maze_goal_ = best ? best->mat : "";
    std::printf("[%d] campaign: maze %s point %d swept, on to %s\n", frame, stage_.c_str(), entry_,
                best ? (std::string(best->mat) + " -> " + best->target + " point " + std::to_string(best->entry)).c_str() : "nothing reachable");
}

// DW4_CAMPAIGN_REACH=items (research, changes the stage): what the player
// reaches from where the stage put them: the mats and the g_d* gimmicks
// (buttons, doors, cells) within a walk map, first as the stage stands, then
// for each item: ';'-separated items of ','-separated HITLANDMODEL walls
// switched off on top of the ones before (a chain of doors). Further:
//   DW4_CAMPAIGN_REACH_CLOSE=walls   switched on first (doors open at start)
//   DW4_CAMPAIGN_REACH_EACH=1        each item alone (switched back after)
//   DW4_CAMPAIGN_REACH_FROM=x,y,z|.. from these points, not the player's
// Prints one line each; maps the dng310 maze's regions.
void reach_report(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int entry) {
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!hitland || !mats || !models) return;
    const auto env = [](const char* name) { const char* v = std::getenv(name); return std::string(v ? v : ""); };
    const auto split = [](const std::string& text, char by) {
        std::vector<std::string> parts;
        for (std::size_t at = 0; at <= text.size();) {
            const auto end = std::min(text.find(by, at), text.size());
            if (end > at) parts.push_back(text.substr(at, end - at));
            at = end + 1;
        }
        return parts;
    };
    std::vector<engine::Vec3f> starts;
    for (const auto& point : split(env("DW4_CAMPAIGN_REACH_FROM"), '|')) {
        engine::Vec3f p{};
        if (std::sscanf(point.c_str(), "%f,%f,%f", &p.x, &p.y, &p.z) == 3) starts.push_back(p);
    }
    if (starts.empty()) starts.push_back(position_of(player));
    for (const auto& wall : split(env("DW4_CAMPAIGN_REACH_CLOSE"), ','))
        hitland->set_child_hit(wall, true);
    const auto line = [&](const engine::Vec3f& me, const std::string& what) {
        const WithoutLimitCircle without_limit(*hitland);
        const WalkMap map(hitland->world(), me, 1400.0f, hitland->delivery_flags(player.info_name, player.index), {},
                          hitland->delivery_width(player.info_name, player.index));
        std::string text;
        for (const auto& box : mats->mat_boxes())
            if (map.reached_inside(box.min.x, box.min.z, box.max.x, box.max.z)) text += " " + box.name;
        text += " |";
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->info_name.rfind("g_d", 0) == 0 &&
                (e->info_name.find("button") != std::string::npos || e->info_name.find("door") != std::string::npos ||
                 e->info_name.find("cell") != std::string::npos || e->info_name.find("gate") != std::string::npos))
                if (map.nearest(position_of(*e), 14.0f)) text += " " + e->info_name + "#" + std::to_string(e->index);
        std::printf("REACH %s e%d from (%.0f,%.0f,%.0f) %s:%s\n", engine.stage_name().c_str(), entry, me.x, me.y, me.z, what.c_str(),
                    text.c_str());
    };
    // (once per stage: every mat's box and the g_d* gimmicks' places)
    static std::string listed;
    if (listed != engine.stage_name()) {
        listed = engine.stage_name();
        for (const auto& box : mats->mat_boxes())
            std::printf("REACHMAT %s %s (%.0f..%.0f, %.0f..%.0f, y %.0f..%.0f)\n", engine.stage_name().c_str(), box.name.c_str(), box.min.x,
                        box.max.x, box.min.z, box.max.z, box.min.y, box.max.y);
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->info_name.rfind("g_d", 0) == 0)
                std::printf("REACHMODEL %s %s#%d (%.0f,%.0f,%.0f) %s\n", engine.stage_name().c_str(), e->info_name.c_str(), e->index,
                            position_of(*e).x, position_of(*e).y, position_of(*e).z, e->seq_name.c_str());
    }
    const bool each = !env("DW4_CAMPAIGN_REACH_EACH").empty();
    for (const auto& me : starts) {
        line(me, "as is");
        std::vector<std::string> off;
        for (const auto& item : split(env("DW4_CAMPAIGN_REACH"), ';')) {
            std::vector<std::string> now;
            for (const auto& wall : split(item, ','))
                if (hitland->child_hit(wall).value_or(false)) {
                    hitland->set_child_hit(wall, false);
                    now.push_back(wall);
                    off.push_back(wall);
                }
            std::string names;
            for (const auto& wall : each ? now : off) names += (names.empty() ? "" : "+") + wall;
            if (!now.empty()) line(me, "without " + names);
            if (each)
                for (const auto& wall : now) hitland->set_child_hit(wall, true);
        }
        for (const auto& wall : each ? std::vector<std::string>{} : off) hitland->set_child_hit(wall, true);
    }
}

// One frame of a boss fight (see kBossFights); false once the boss is down
// (or not there yet).
bool CampaignBot::boss_fight(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, const std::string& strike,
                             const std::string& follow, float keep, int frame, Output& out) {
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!models) return false;
    const auto me = position_of(player);
    const auto* boss = find_model(engine, follow + "#0", me);
    if (!boss || boss->hp <= 0) return false;
    const bool tracing = std::getenv("DW4_TRACE_CAMPAIGN") && std::string_view(std::getenv("DW4_TRACE_CAMPAIGN")) == "2";
    const std::string targets = "|" + strike + "|";
    // The player's body (the stages' p_* hit body: 5 wide, 17 high).
    const float body_half = 2.5f, body_height = 17.0f;
    // An enemy attack box that has started (or starts within a few frames)
    // and meets the player's body grown by a small margin (boxes move).
    std::string threat;
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->is_player) continue;
        const auto& m = e->node->matrix;
        for (const auto& box : e->hitboxes) {
            const int group = box.kind / 10000;
            if ((group != 7 && group != 8) || box.elapsed + 4.0f < box.delay) continue;
            const engine::Vec3f o = box.offset;
            const float cx = o.x * m(0, 0) + o.y * m(1, 0) + o.z * m(2, 0) + m(3, 0);
            const float cy = o.x * m(0, 1) + o.y * m(1, 1) + o.z * m(2, 1) + m(3, 1);
            const float cz = o.x * m(0, 2) + o.y * m(1, 2) + o.z * m(2, 2) + m(3, 2);
            constexpr float margin = 2.5f;
            if (std::abs(me.x - cx) < box.size.x * 0.5f + body_half + margin && std::abs(me.z - cz) < box.size.z * 0.5f + body_half + margin &&
                me.y + body_height + 1.0f > cy - box.size.y * 0.5f && me.y - 1.0f < cy + box.size.y * 0.5f)
                threat = e->info_name + " " + std::to_string(box.kind);
        }
    }
    // The listed model in focus: the nearest one that can take a hit (alive,
    // no MUTEKI) and is not out of reach in the air; `in_reach`: its hit body
    // meets the height of the player's swing (the attack boxes reach from
    // the feet 8 up).
    const engine::ModelCtrlManager::ModelRam* focus = nullptr;
    engine::Vec3f at{};
    float best = 1e30f;
    bool in_reach = false;
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->hp <= 0 || e->muteki != 0.0f || !e->hit || e->hit->boxes.empty() || !listed_in(targets, *e))
            continue;
        const auto& m = e->node->matrix;
        float y0 = 1e30f, y1 = -1e30f;
        for (const auto& [lo, hi] : e->hit->boxes) {
            // (80307EAC: the matrix moves the box centre only.)
            const engine::Vec3f c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
            const float cy = c.x * m(0, 1) + c.y * m(1, 1) + c.z * m(2, 1) + m(3, 1);
            y0 = std::min(y0, cy - (hi.y - lo.y) * 0.5f);
            y1 = std::max(y1, cy + (hi.y - lo.y) * 0.5f);
        }
        if (tracing && frame % 10 == 0)
            std::printf("[%d] campaign boss part %s#%d %s body y %.0f..%.0f at (%.0f,%.0f)\n", frame, e->info_name.c_str(), e->index,
                        e->seq_name.c_str(), y0, y1, m(3, 0), m(3, 2));
        if (y0 > me.y + 40.0f) continue;
        const auto p = nearest_hit_point(*e, me);
        if (flat(p, me) < best) {
            best = flat(p, me);
            focus = e.get();
            at = p;
            in_reach = y0 < me.y + 8.0f && y1 > me.y;
        }
    }
    if (tracing && frame % 30 == 0)
        std::printf("[%d] campaign boss %s#%d hp %d %s, focus %s, threat %s, at (%.0f,%.0f,%.0f)\n", frame, boss->info_name.c_str(), boss->index,
                    boss->hp, boss->seq_name.c_str(),
                    focus ? (focus->info_name + " d " + std::to_string(int(best)) + (in_reach ? " in reach" : "")).c_str() : "-",
                    threat.empty() ? "-" : threat.c_str(), me.x, me.y, me.z);
    if (!threat.empty()) {
        // (B is read on its press: pulsed so the guard is taken again.)
        if (frame % 8 < 4) out.buttons |= pad_button::b;
        return true;
    }
    if (focus) {
        // The swing's boxes reach up to ~20 ahead: struck from 2.5..6.5 off
        // its hit body, outside the reach of a contact box around it.
        if (best > 6.5f) {
            walk_to(engine, player, at, 5.0f, frame, out);
        } else if (best < 2.5f) {
            const auto c = position_of(*focus);
            const float d = std::max(0.01f, flat(c, me));
            out.walk = true;
            out.x = me.x + (me.x - c.x) / d * 6.0f;
            out.z = me.z + (me.z - c.z) / d * 6.0f;
            out.reach = 1.0f;
        } else if (frame % 12 < 3) {
            out.buttons |= pad_button::a;
        }
        return true;
    }
    // Nothing to strike: near the boss, on the player's side of it.
    const auto b = position_of(*boss);
    const float d = flat(b, me);
    if (std::abs(d - keep) > 6.0f) {
        const float k = d > 0.01f ? keep / d : 0.0f;
        const engine::Vec3f spot{b.x + (me.x - b.x) * k, me.y, b.z + (me.z - b.z) * k};
        walk_to(engine, player, spot, 4.0f, frame, out);
    }
    return true;
}

// DW4_CAMPAIGN_REGIONS=walls (research, changes the stage): the regions of
// a stage with the listed HITLANDMODEL walls closed (doors, barriers) and
// what each wall joins when it alone is open. The floor is the union of the
// walk maps from the player, every mat and every g_d* gimmick with all the
// walls open; regions are labelled by flooding from unlabelled floor in
// rising height (a ledge walked off leads down, never up, so a lower region
// is labelled before one that drops into it). Prints REGION lines (seed,
// mats, gimmicks), REGIONDROP (reached one way with the walls closed),
// REGIONLINK lines (a wall opened) and REGIONCELL x z id for a map.
void region_report(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player) {
    auto* hitland = engine.get<engine::HitLandManager>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!hitland || !mats || !models) return;
    std::vector<std::string> walls;
    {
        const std::string list = std::getenv("DW4_CAMPAIGN_REGIONS");
        for (std::size_t at = 0; at <= list.size();) {
            const auto end = std::min(list.find(',', at), list.size());
            if (end > at) walls.push_back(list.substr(at, end - at));
            at = end + 1;
        }
    }
    const WithoutLimitCircle without_limit(*hitland);
    const auto flags = hitland->delivery_flags(player.info_name, player.index);
    const auto width = hitland->delivery_width(player.info_name, player.index);
    const auto map_from = [&](const engine::Vec3f& p) { return WalkMap(hitland->world(), p, 1400.0f, flags, {}, width); };
    const auto key = [](const engine::Vec3f& p) { return std::make_pair(int(std::lround(p.x)), int(std::lround(p.z))); };
    // The floor, all walls open.
    std::vector<engine::Vec3f> seeds{position_of(player)};
    for (const auto& box : mats->mat_boxes())
        seeds.push_back({(box.min.x + box.max.x) * 0.5f, box.min.y + 2.0f, (box.min.z + box.max.z) * 0.5f});
    for (const auto& e : models->rams())
        if (e->generated && e->node && e->info_name.rfind("g_d", 0) == 0) seeds.push_back(position_of(*e) + engine::Vec3f{0, 2, 0});
    for (const auto& wall : walls) hitland->set_child_hit(wall, false);
    std::map<std::pair<int, int>, engine::Vec3f> floor;
    for (const auto& seed : seeds) {
        bool known = false;
        for (const auto& [k, q] : floor)
            if (std::abs(q.x - seed.x) < 4.0f && std::abs(q.z - seed.z) < 4.0f && std::abs(q.y - seed.y) < 6.0f) known = true;
        if (known) continue;
        for (const auto& q : map_from(seed).samples()) floor.emplace(key(q), q);
    }
    for (const auto& wall : walls) hitland->set_child_hit(wall, true);
    // Regions, flooded in rising height.
    std::vector<engine::Vec3f> order;
    for (const auto& [k, q] : floor) order.push_back(q);
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
    std::map<std::pair<int, int>, int> label;
    std::vector<engine::Vec3f> region_seed;
    std::vector<std::set<int>> drops;
    for (const auto& q : order) {
        if (label.count(key(q))) continue;
        const int id = int(region_seed.size());
        region_seed.push_back(q);
        drops.emplace_back();
        const auto map = map_from(q);
        for (const auto& r : map.samples()) {
            const auto it = label.find(key(r));
            if (it == label.end()) {
                if (floor.count(key(r))) label[key(r)] = id;
            } else if (it->second != id) {
                drops[std::size_t(id)].insert(it->second);
            }
        }
    }
    const auto region_of = [&](const engine::Vec3f& p, float tolerance) {
        int found = -1;
        float best = tolerance;
        for (const auto& [k, id] : label) {
            const auto& q = floor.at(k);
            const float d = std::hypot(q.x - p.x, q.z - p.z);
            if (d < best && std::abs(q.y - p.y) < 30.0f) {
                best = d;
                found = id;
            }
        }
        return found;
    };
    const std::string stage = engine.stage_name();
    for (std::size_t id = 0; id < region_seed.size(); ++id) {
        std::string text;
        int cells = 0;
        for (const auto& [k, l] : label) cells += l == int(id);
        if (cells < 3) continue;
        for (const auto& box : mats->mat_boxes()) {
            bool inside = false;
            for (const auto& [k, l] : label)
                if (l == int(id)) {
                    const auto& q = floor.at(k);
                    if (q.x >= box.min.x && q.x <= box.max.x && q.z >= box.min.z && q.z <= box.max.z && q.y >= box.min.y - 2 && q.y <= box.max.y)
                        inside = true;
                }
            if (inside) text += " " + box.name;
        }
        text += " |";
        for (const auto& e : models->rams())
            if (e->generated && e->node && e->info_name.rfind("g_d", 0) == 0 &&
                (e->info_name.find("button") != std::string::npos || e->info_name.find("door") != std::string::npos ||
                 e->info_name.find("cell") != std::string::npos || e->info_name.find("gate") != std::string::npos ||
                 e->info_name.find("barrierw2") != std::string::npos))
                if (region_of(position_of(*e), 12.0f) == int(id)) text += " " + e->info_name + "#" + std::to_string(e->index);
        std::string down;
        for (const int d : drops[id]) down += " R" + std::to_string(d);
        std::printf("REGION %s R%zu seed (%.0f,%.0f,%.0f) cells %d:%s%s\n", stage.c_str(), id, region_seed[id].x, region_seed[id].y,
                    region_seed[id].z, cells, text.c_str(), down.empty() ? "" : (" drops to" + down).c_str());
    }
    // Each wall alone open: the regions each region's seed reaches then.
    for (const auto& wall : walls) {
        hitland->set_child_hit(wall, false);
        for (std::size_t id = 0; id < region_seed.size(); ++id) {
            std::set<int> reached;
            for (const auto& r : map_from(region_seed[id]).samples())
                if (const auto it = label.find(key(r)); it != label.end() && it->second != int(id) && !drops[id].count(it->second))
                    reached.insert(it->second);
            if (reached.empty()) continue;
            std::string to;
            for (const int r : reached) to += " R" + std::to_string(r);
            std::printf("REGIONLINK %s R%zu --%s-->%s\n", stage.c_str(), id, wall.c_str(), to.c_str());
        }
        hitland->set_child_hit(wall, true);
    }
    for (const auto& [k, id] : label) std::printf("REGIONCELL %d %d %d %.0f\n", k.first, k.second, id, floor.at(k).y);
    std::fflush(stdout);
}

CampaignBot::Output CampaignBot::update(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame) {
    // The disk shop's prices, wherever the conItem table is loaded.
    if (auto* db = engine.get<engine::DatabaseManager>(); db && db->has_column("conItem", "HANBAI"))
        for (const auto& s : kSupplies)
            if (!prices_.count(s.record))
                if (const auto price = db->number("conItem", s.record, "HANBAI"); price && *price > 0) prices_[s.record] = *price;
    // A defeat: player 1's HP reached 0 (the bit capsule's game over follows).
    if (auto* sys = engine.get<engine::GameSystem>()) {
        const auto& slot = sys->players[0];
        const bool alive = slot.joined && slot.hp > 0 && lower(engine.stage_name()).rfind("stagis", 0) != 0;
        if (alive_ && !alive && slot.joined && slot.hp == 0) {
            ++deaths_;
            // (Each defeat at an objective raises its gate level, kTraining.)
            if (!objective_now_.empty() && objective_now_.rfind("train", 0) != 0)
                defeat_levels_[objective_now_] = std::max(defeat_levels_[objective_now_], slot.stats.level);
            std::printf("[%d] campaign: defeated (%d so far) in %s at level %d with %d bits\n", frame, deaths_, engine.stage_name().c_str(),
                        slot.stats.level, slot.bits);
        }
        alive_ = alive;
    }
    Output out = play(engine, player, frame);
    if (player && player->node && lower(engine.stage_name()).rfind("stagis", 0) != 0) survive(engine, *player, frame, out);
    return out;
}

// Disks used from the L panel (heals, the Raise Disk) and the upkeep of
// panels the driver opened, laid over the frame's output (see kHealBelow).
// A disk use: L opens the disk panel (GENERATEITEM, needs the player's
// control), UP to its top (the cursor is remembered between openings; one
// press per row but the first), DOWN
// to the disk's line, A (the panel uses the disk itself, 80349B0C, even while
// a hit holds the player), L closes it. Each press is one frame (an edge),
// the other buttons are held back and the stick centred meanwhile.
void CampaignBot::survive(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out) {
    auto* sys = engine.get<engine::GameSystem>();
    if (!sys || lobby(stage_)) {
        disk_use_ = -1;
        return;
    }
    const auto& slot = sys->players[0];
    // The biggest blow taken within kBigHitFor frames (the heals' threshold).
    if (last_hp_ > slot.hp && slot.hp > 0) {
        const int hit = last_hp_ - slot.hp;
        if (hit >= big_hit_ || frame - big_hit_at_ > kBigHitFor) big_hit_ = hit, big_hit_at_ = frame;
    }
    last_hp_ = slot.hp;
    if (tech_at_ >= 0) {
        // A technique cast: R opens the technique panel (GENERATEWAZA), UP
        // to its top (the first technique, WAZALIST 0; once per further
        // technique, none with one), A casts it (80349474: the MP drops,
        // SQ_WAZA with its label), R closes the panel.
        const int t = frame - tech_at_;
        const int ups = std::clamp(int(slot.wazalist.size()) - 1, 0, 5);
        const int a_at = 4 + 2 * ups;
        out.buttons = 0;
        out.walk = false; // (the panel takes stick directions as cursor moves)
        if (t < 1) out.buttons |= pad_button::r;
        else if (t >= 3 && t < 3 + 2 * ups && t % 2 == 1) out.buttons |= pad_button::up;
        else if (t == a_at) {
            out.buttons |= pad_button::a;
            // (The cast starts the panel's FIRE wait: its DELAY.)
            if (auto* db = engine.get<engine::DatabaseManager>(); db && !slot.wazalist.empty())
                panel_ready_at_ = frame + db->number("pcTech", slot.wazalist[0], "DELAY").value_or(0) + 1;
        } else if (t == a_at + 10 && player.panel_open) out.buttons |= pad_button::r;
        if (t > a_at + 14) tech_at_ = -1;
        return;
    }
    if (disk_use_ >= 0 && disk_quick_) {
        // The quick heal: Y (held three frames), then L closes the disk
        // panel it opened.
        const int t = frame - disk_use_at_;
        if (t >= 8 && t < 30 && !player.panel_open && disks_held(slot, disk_use_) == disk_held_) {
            // Not taken (a hit came first): tried again as soon as the player can.
            disk_use_ = -1;
            heal_at_ = frame - kHealGap + 4;
            return;
        }
        out.buttons = t < 3 ? pad_button::y : 0;
        if (t == 0) panel_ready_at_ = frame + kPanelFire;
        if (t == 30 && player.panel_open) out.buttons |= pad_button::l;
        if (t > 36) {
            disk_use_ = -1;
            panel_close_at_ = frame;
        }
        return;
    }
    if (disk_use_ >= 0) {
        const int t = frame - disk_use_at_;
        const int downs_at = 4 + 2 * disk_ups_, a_at = downs_at + 2 * disk_line_ + 2;
        if (t >= 3 && t < a_at && !player.panel_open) {
            // A hit closed the panel (the damage sequences REMOVE it,
            // 8034E1A4): tried again as soon as the player can.
            if (trace()) std::printf("[%d] campaign disk use interrupted (frame %d of it)\n", frame, t);
            if (disk_use_ == kGateDisk) gate_disk_at_ = -100000; // (its wait is for a trip made)
            disk_use_ = -1;
            if (heal_at_ == disk_use_at_) heal_at_ = frame - kHealGap + 4;
            return;
        }
        // (The stick stays centred: the panel takes its directions as cursor
        // moves too, 8034A210, and a walk would pick another disk.)
        out.buttons = 0;
        out.walk = false;
        if (t < 1) out.buttons |= pad_button::l;
        else if (t >= 4 && t < downs_at && t % 2 == 0) out.buttons |= pad_button::up;
        else if (t >= downs_at && t < downs_at + 2 * disk_line_ && t % 2 == 0) out.buttons |= pad_button::down;
        else if (t == a_at) {
            out.buttons |= pad_button::a;
            panel_ready_at_ = frame + kPanelFire;
        } else if (t == a_at + 24 && player.panel_open) out.buttons |= pad_button::l;
        if (t > a_at + 30) {
            // (A pick the panel did not take, the disk still held: tried again
            // soon; the Gate Disk and the Raise Disk have their own retries.)
            if (disk_use_ != kRaiseDisk && disks_held(slot, disk_use_) == disk_held_) {
                if (trace()) std::printf("[%d] campaign disk %d not taken by the panel\n", frame, disk_use_);
                if (heal_at_ == disk_use_at_) heal_at_ = frame - kHealGap + 4;
                // (A Gate Disk the panel refused, NOGATEDISK: stagBS01's
                // Apocalymon fight; the panel greys it, 80349684, and the
                // pick plays CURSOR_NG: none in this stage visit.)
                if (disk_use_ == kGateDisk) {
                    gate_refused_at_ = stage_frame_;
                    if (trace()) std::printf("[%d] campaign: the Gate Disk is refused in %s\n", frame, stage_.c_str());
                }
            }
            disk_use_ = -1;
            panel_close_at_ = frame;
        }
        return;
    }
    const bool controllable = player.control_enabled && player.ctrl_mode != 0 && player.seq_name.rfind("SQ_DEMO", 0) != 0 &&
                              player.seq_name != "SQ_JUMP" && player.seq_name != "SQ_FALL";
    // (The panel takes no cursor press, A or Y while its FIRE wait from the
    // last pick runs, kPanelFire: a walk started then picks the wrong line.)
    const bool panel_ready = frame + 3 >= panel_ready_at_;
    // (The biggest blow taken lately, see the top: below kBigHitMargin
    // percent of it the next one may be the last, so that is low HP too;
    // dng101b2's Blossomon hits a level 6 Veemon of 270 HP for 70..100 and
    // 222 when critical, and at kHealBelow percent, 108 HP, one blow ended it.)
    const bool big_hits = frame - big_hit_at_ <= kBigHitFor;
    const bool low_hp = slot.hp * 100 < slot.max_hp * kHealBelow ||
                        (big_hits && slot.hp * 100 < big_hit_ * kBigHitMargin && slot.hp * 100 < slot.max_hp * 90);
    // A heal: the smallest HP disk that makes up the missing HP, else the
    // biggest one held, Recovery last.
    // (Not in god mode: a pit there can still set the HP to 1, GAMERAM ADDHP
    // absolute, and the cheat runs keep their path.)
    if (!engine.cheats().god && slot.hp > 0 && low_hp && frame - heal_at_ >= kHealGap && controllable && !player.panel_open &&
        panel_ready) {
        int pick = -1;
        auto* db = engine.get<engine::DatabaseManager>();
        for (const int record : {0, 1, 2})
            if (disks_held(slot, record) > 0) {
                pick = record;
                if (db && db->number("conItem", record, "HP").value_or(0) >= slot.max_hp - slot.hp) break;
            }
        if (pick < 0 && disks_held(slot, 9) > 0) pick = 9;
        if (pick >= 0) {
            heal_at_ = frame;
            start_disk(slot, pick, frame);
            // Y does it at once when it would take the same disk (the
            // highest HP record held, 8034ABA0), or when the HP is low enough
            // that the panel's 30 frames are not to be risked.
            int highest = -1;
            for (const int record : {0, 1, 2, 9})
                if (disks_held(slot, record) > 0) highest = record;
            disk_quick_ = highest == pick || slot.hp * 100 < slot.max_hp * kHealQuickBelow;
            // (A panel walk waits for a gap between the blows: a hit closes
            // the panel, 8034E1A4.)
            const float walk = float(4 + 2 * disk_ups_ + 2 * disk_line_ + 2);
            if (!disk_quick_ && blow_due(engine, player, walk, frame) <= walk) {
                disk_use_ = -1;
                heal_at_ = frame - kHealGap + 2;
                return;
            }
            if (trace())
                std::printf("[%d] campaign heals at %d/%d HP with conItem %d (HP disks %d/%d/%d, Recovery %d)\n", frame, slot.hp, slot.max_hp,
                            pick, disks_held(slot, 0), disks_held(slot, 1), disks_held(slot, 2), disks_held(slot, 9));
            survive(engine, player, frame, out);
            return;
        }
    }
    // Out of an enemy's touch (not with the one-hit cheat): an enemy's
    // permanent body box (kind group 7, end 1e8, e_goburi 70000) hurts on
    // every frame it meets the player's body without MUTEKI, and the "common"
    // label of SQ_SDMG gives none after its knock-back (only the elemental
    // labels end with MUTEKI 30): an enemy that keeps walking into the player
    // hits it again on the first frame of each SQ_WAIT. So once the player has
    // control and a body box touches it, B: the guard takes the touch
    // (hit_p_*.lua: SQ_GUARD -> SQ_GUARDR, no damage, MOVE Back and MUTEKI 30
    // at its end), and while that MUTEKI lasts the player walks on away from
    // the enemy instead of attacking.
    // (The guards: not in god mode, where the HP does not matter and a boss
    // that attacks without pause would never be struck, nor in an aggressive
    // window, see kAggressAfter.)
    if (player.seq_name.rfind("SQ_ATCK", 0) == 0 || player.seq_name == "SQ_WAZA") last_attack_ = frame, held_presses_ = 0;
    if (frame >= aggressive_until_ && frame - last_attack_ > kAggressAfter && frame - guard_held_ < 60 &&
        held_presses_ >= kAggressHeld) {
        held_presses_ = 0;
        aggressive_until_ = frame + kAggressFor;
        last_attack_ = frame;
        if (trace()) std::printf("[%d] campaign: no attack for %d frames, %d frames without guarding\n", frame, kAggressAfter, kAggressFor);
    }
    const bool guarding = !engine.cheats().god && frame >= aggressive_until_;
    if (guarding && !engine.cheats().one_hit && player.control_enabled && player.node) {
        float gap = 0.0f;
        if (const auto away = touch_escape(engine, player, gap)) {
            if (gap <= kTouchGuard && player.muteki == 0.0f && controllable && !player.panel_open) {
                if (out.buttons & pad_button::a) ++held_presses_;
                out.buttons = frame % 4 < 2 ? pad_button::b : 0;
                guard_held_ = frame;
                if (trace() && frame - touch_traced_ >= 600) {
                    touch_traced_ = frame;
                    std::printf("[%d] campaign guards a touch (%s)\n", frame, guard_what_.c_str());
                }
                return;
            }
            // (Only over floor at the player's height: on a ledge the way
            // back may be a drop.)
            const auto floor_at = [&](float k) {
                auto* hitland = engine.get<engine::HitLandManager>();
                if (!hitland) return false;
                const auto me = position_of(player);
                const float d = std::max(0.01f, flat(*away, me));
                const engine::Vec3f q{me.x + (away->x - me.x) / d * k, me.y + 5.0f, me.z + (away->z - me.z) / d * k};
                const auto land = hitland->world().check_land(q, 1.0f, 400.0f, 1.0f);
                return land.found && std::abs(land.position.y - me.y) < 3.0f;
            };
            if ((player.muteki != 0.0f || player.seq_name == "SQ_GUARDR") && floor_at(3.0f) && floor_at(7.0f)) {
                out.walk = true;
                out.x = away->x;
                out.z = away->z;
                out.reach = 0.25f;
                out.buttons &= ~(pad_button::a | pad_button::b);
                return;
            }
        }
    }
    // The Gate Disk out (not in god mode): see kGateDisk.
    if (!engine.cheats().god && controllable && !player.panel_open && panel_ready && frame - gate_disk_at_ >= 3000 &&
        disks_held(slot, kGateDisk) > 0 && gate_refused_at_ != stage_frame_) {
        bool heals = false;
        for (const int record : {0, 1, 2, 9}) heals = heals || disks_held(slot, record) > 0;
        const bool dying = (slot.hp * 100 < slot.max_hp * kGateTripBelow || (big_hits && slot.hp * 100 < big_hit_ * kBigHitMargin)) && !heals;
        // (While training: supplies to buy, or a full device folder, which
        // takes no drop, to sell from.)
        // (Supplies worth kGateTripBits to buy: not for one disk used.)
        const bool shopping = objective_now_.rfind("train", 0) == 0 &&
                              (planned_spend(engine) >= kGateTripBits || (slot.devices.size() >= 36 && next_sale(engine) >= 0));
        if ((dying || shopping) && blow_due(engine, player, kHoldAttackLead + 8.0f, frame) > kHoldAttackLead + 8.0f) {
            gate_disk_at_ = frame;
            heal_at_ = frame;
            start_disk(slot, kGateDisk, frame);
            std::printf("[%d] campaign: Gate Disk back to the lobby (%s, %d/%d HP, %d bits)\n", frame, dying ? "no heal left" : "shopping",
                        slot.hp, slot.max_hp, slot.bits);
            // An objective given up for want of heals raises its gate level
            // like a defeat (kGates): a boss that outlasts the supplies would
            // otherwise be walked back to again and again (s2h: dng102b3's
            // e_mammos, four trips of ~35000 frames at level 7).
            if (dying && objective_now_.rfind("train", 0) != 0 && !objective_now_.empty() && escape_stage_frame_ != stage_frame_) {
                escape_stage_frame_ = stage_frame_;
                defeat_levels_[objective_now_] = std::max(defeat_levels_[objective_now_], slot.stats.level);
                std::printf("[%d] campaign: %s given up at level %d, trains first\n", frame, objective_now_.c_str(), slot.stats.level);
            }
            survive(engine, player, frame, out);
            return;
        }
    }
    // MP for the techniques (not with the one-hit cheat): below the first
    // technique's cost an MP disk is used (the smallest held), when no blow
    // is due.
    if (!engine.cheats().one_hit && controllable && !player.panel_open && panel_ready && frame - heal_at_ >= kHealGap && !slot.wazalist.empty())
        if (auto* db = engine.get<engine::DatabaseManager>(); db && db->number("pcTech", slot.wazalist[0], "HP").value_or(0) < 0 &&
                                                               slot.mp < db->number("pcTech", slot.wazalist[0], "MP").value_or(0) &&
                                                               blow_due(engine, player, kHoldAttackLead, frame) > kHoldAttackLead)
            for (const int record : {3, 4, 5})
                if (disks_held(slot, record) > 0) {
                    heal_at_ = frame;
                    start_disk(slot, record, frame);
                    if (trace()) std::printf("[%d] campaign MP disk %d at %d/%d MP\n", frame, record, slot.mp, slot.max_mp);
                    survive(engine, player, frame, out);
                    return;
                }
    // The guard against a blow about to land (not in god mode: the cheat runs
    // keep their way of fighting). The stages' hit_p_*.lua take no damage
    // while the player is in SQ_GUARD (-> SQ_GUARDR), whatever the side; but
    // SQ_GUARD lasts its action GURD*1 only (15 frames, then SQ_GUARD2 and
    // SQ_WAIT) and B is read only while the sequence takes control, not in an
    // attack (SQ_ATCK1 runs 29 frames). So while an enemy attack is under way
    // that will meet the player no attack is started, and B goes in just
    // before its box goes live.
    if (!engine.cheats().one_hit && !player.panel_open) {
        const float due = blow_due(engine, player, kHoldAttackLead, frame);
        // A technique at the enemy in front when the MP allows (survive).
        if (due > kHoldAttackLead && controllable && panel_ready && frame - tech_end_ >= kTechGap && tech_target(engine, player, slot)) {
            tech_at_ = frame;
            tech_end_ = frame + 4 + 2 * std::clamp(int(slot.wazalist.size()) - 1, 0, 5) + 14;
            survive(engine, player, frame, out);
            return;
        }
        if (guarding && due <= kHoldAttackLead) {
            if (out.buttons & pad_button::a) ++held_presses_;
            out.buttons &= ~pad_button::a;
            guard_held_ = frame;
        }
        if (guarding && due <= kGuardLead && player.muteki <= due + 1.0f && controllable) {
            if (frame % 4 < 2) out.buttons |= pad_button::b;
            if (trace() && frame - guard_traced_ >= 600) {
                guard_traced_ = frame;
                std::printf("[%d] campaign guards (%s)\n", frame, guard_what_.c_str());
            }
            return;
        }
    }
    // A panel the driver left open is closed with R (with a panel up, A
    // picks from it instead of attacking).
    if (player.panel_open && frame - panel_close_at_ < 600 && player.control_enabled) {
        out.buttons &= ~pad_button::a;
        if ((frame - panel_close_at_) % 30 == 15) out.buttons |= pad_button::r;
    }
}

// Whether to cast the first technique (WAZALIST 0) now: it does damage
// (pcTech HP below 0), the MP covers it, and a live enemy it can hurt (its
// resistance to the technique's element, enemy RES_*, below 700 of 1000:
// P2EDAMAGE scales by (1000 - resistance)) stands within kTechReach in front
// of the player on its level (the shell flies along the facing; a magic
// bullet always hits, P2EISHITCHECK 80372A44).
bool CampaignBot::tech_target(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player,
                              const engine::GameSystem::PlayerSlot& slot) const {
    auto* db = engine.get<engine::DatabaseManager>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!db || !models || slot.wazalist.empty() || !player.node) return false;
    const int row = slot.wazalist[0];
    if (db->number("pcTech", row, "HP").value_or(0) >= 0 || slot.mp < db->number("pcTech", row, "MP").value_or(1 << 30)) return false;
    const std::string element = db->text("pcTech", row, "ZOKUSEI");
    static const char* const kElements[] = {"fire", "ice", "thunder", "dark"};
    static const char* const kColumns[] = {"RES_FIRE", "RES_ICE", "RES_THUNDER", "RES_DARK"};
    const char* column = nullptr;
    for (int i = 0; i < 4; ++i)
        if (element == kElements[i]) column = kColumns[i];
    const auto& m = player.node->matrix;
    const float fl = std::hypot(m(2, 0), m(2, 2));
    if (fl < 0.1f) return false;
    const float fx = m(2, 0) / fl, fz = m(2, 2) / fl;
    const auto me = position_of(player);
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->category != "ENEMY" || e->hp <= 0 || e->muteki != 0.0f) continue;
        if (!engine.has_script("hit_" + e->info_name + ".lua")) continue;
        // (Not at gimmicks: their hit scripts decide what a technique does,
        // and stag0205's energy plugs take none, hit_g_eneplugz02.lua's
        // funcTechWazaHitCheck branch; a god-mode run cast at them 70000
        // frames. The walls a technique breaks are the ExploreBot's.)
        if (e->info_name.rfind("g_", 0) == 0) continue;
        const auto at = position_of(*e);
        const float d = flat(at, me);
        if (d < 3.0f || d > kTechReach || std::abs(at.y - me.y) > 10.0f) continue;
        if (((at.x - me.x) * fx + (at.z - me.z) * fz) / d < 0.94f) continue;
        if (column && db->number("enemy", e->record, column).value_or(0) >= 700) continue;
        return true;
    }
    return false;
}

// A point to escape to when a live enemy's permanent body box (end 1e8)
// touches player 1's body or comes within kTouchGap of it: 10 units away from
// the touching box on the far side (nothing when no box touches); `gap` is
// the space left between the box and the body (below 0: they overlap).
std::optional<engine::Vec3f> CampaignBot::touch_escape(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float& gap) {
    constexpr float kBodyHalf = 2.5f, kBodyHeight = 17.0f, kTouchGap = 3.0f;
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!models || !player.node) return std::nullopt;
    const auto me = position_of(player);
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->is_player || e->category != "ENEMY" || e->hp <= 0) continue;
        if (flat(position_of(*e), me) > 40.0f) continue;
        const auto& m = e->node->matrix;
        for (const auto& box : e->hitboxes) {
            const int group = box.kind / 10000;
            if ((group != 7 && group != 8) || box.end < 1e7f || box.elapsed < box.delay) continue;
            const engine::Vec3f o = box.offset;
            const float cx = o.x * m(0, 0) + o.y * m(1, 0) + o.z * m(2, 0) + m(3, 0);
            const float cy = o.x * m(0, 1) + o.y * m(1, 1) + o.z * m(2, 1) + m(3, 1);
            const float cz = o.x * m(0, 2) + o.y * m(1, 2) + o.z * m(2, 2) + m(3, 2);
            if (std::abs(me.x - cx) < box.size.x * 0.5f + kBodyHalf + kTouchGap && std::abs(me.z - cz) < box.size.z * 0.5f + kBodyHalf + kTouchGap &&
                me.y + kBodyHeight > cy - box.size.y * 0.5f && me.y < cy + box.size.y * 0.5f) {
                const float dx = me.x - cx, dz = me.z - cz, d = std::max(0.01f, std::hypot(dx, dz));
                gap = std::max(std::abs(dx) - box.size.x * 0.5f - kBodyHalf, std::abs(dz) - box.size.z * 0.5f - kBodyHalf);
                guard_what_ = e->info_name + " " + std::to_string(box.kind);
                return engine::Vec3f{me.x + dx / d * 10.0f, me.y, me.z + dz / d * 10.0f};
            }
        }
    }
    return std::nullopt;
}

// Frames until an enemy blow lands on player 1 (1e9: none within `horizon`):
// an attack box (kind group 7 or 8, MODEL_HITBOX, queued with its delay when
// the attack starts) that has not ended and meets the player's body (5 wide,
// 17 high) grown by a margin (boxes move with their lunging owner, more so
// the further off their start; a shot's box flies further in a frame,
// EBULLET; a box that moves is taken ahead by its last step). Permanent body
// boxes (end 1e8) are left out.
float CampaignBot::blow_due(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, float horizon, int frame) {
    constexpr float kBodyHalf = 2.5f, kBodyHeight = 17.0f;
    float best = 1e9f;
    if (!player.node) return best;
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!models) return best;
    const auto me = position_of(player);
    for (const auto& e : models->rams()) {
        if (!e->generated || !e->node || e->is_player || e->hitboxes.empty()) continue;
        if (e->category == "ENEMY" && e->hp <= 0) continue;
        if (flat(position_of(*e), me) > 120.0f) continue;
        const auto& m = e->node->matrix;
        for (std::size_t index = 0; index < e->hitboxes.size(); ++index) {
            const auto& box = e->hitboxes[index];
            const int group = box.kind / 10000;
            // (A permanent body box, end 1e8, hurts on touch: no guard against
            // it, the attack goes on; only the blows of attacks count.)
            if ((group != 7 && group != 8) || box.elapsed >= box.end || box.end >= 1e7f) continue;
            const float due = std::max(0.0f, box.delay - box.elapsed);
            if (due > horizon || due >= best) continue;
            const engine::Vec3f o = box.offset;
            const float cx = o.x * m(0, 0) + o.y * m(1, 0) + o.z * m(2, 0) + m(3, 0);
            const float cy = o.x * m(0, 1) + o.y * m(1, 1) + o.z * m(2, 1) + m(3, 1);
            float cz = o.x * m(0, 2) + o.y * m(1, 2) + o.z * m(2, 2) + m(3, 2);
            float cx2 = cx, cy2 = cy;
            // A box that flies (a turret's shot moves its box along, g_cannon01
            // 70100) is taken where it will be when the guard is up.
            auto& seen = boxes_seen_[{e.get(), int(index)}];
            if (seen.frame != frame) {
                seen.prev = seen.at;
                seen.prev_frame = seen.frame;
                seen.at = {cx, cy, cz};
                seen.frame = frame;
            }
            if (seen.prev_frame == frame - 1) {
                const float ahead = due + kGuardLead;
                cx2 += (seen.at.x - seen.prev.x) * ahead;
                cy2 += (seen.at.y - seen.prev.y) * ahead;
                cz += (seen.at.z - seen.prev.z) * ahead;
            }
            const float grow = e->category == "EBULLET" ? 9.0f + due * 2.0f : 3.0f + due * 0.25f;
            if (static const bool t = std::getenv("DW4_TRACE_GUARD") != nullptr; t)
                std::printf("guard? %s#%d %s kind %d due %.0f el %.0f end %.0f gap x %.1f z %.1f y %.1f..%.1f me y %.1f mu %.0f seq %s\n",
                            e->info_name.c_str(), e->index, e->seq_name.c_str(), box.kind, due, box.elapsed, box.end,
                            std::abs(me.x - cx2) - box.size.x * 0.5f - kBodyHalf, std::abs(me.z - cz) - box.size.z * 0.5f - kBodyHalf,
                            cy2 - box.size.y * 0.5f, cy2 + box.size.y * 0.5f, me.y, player.muteki, player.seq_name.c_str());
            if (std::abs(me.x - cx2) < box.size.x * 0.5f + kBodyHalf + grow && std::abs(me.z - cz) < box.size.z * 0.5f + kBodyHalf + grow &&
                me.y + kBodyHeight + 1.0f > cy2 - box.size.y * 0.5f && me.y - 1.0f < cy2 + box.size.y * 0.5f) {
                best = due;
                guard_what_ = e->info_name + " " + std::to_string(box.kind);
            }
        }
    }
    return best;
}

// Below kGateBelow percent of the maximum HP with no HP disk (or Recovery)
// held: a heal gate of the stage (a mat whose script runs funcHealGate "HP"
// or "ALL", which sets the HP to the maximum, GAMERAM ADDHP absolute, while a
// player stands in it; dungeons have one or more) is walked to and stood on
// until the HP is full. One not reached in kGateWalk frames is left for the
// stage. True while it steers.
bool CampaignBot::heal_gate_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out) {
    auto* sys = engine.get<engine::GameSystem>();
    auto* mats = engine.get<engine::MatCtrlManager>();
    if (!sys || !mats || engine.cheats().god) return false;
    const auto& slot = sys->players[0];
    if (gate_since_ >= 0 && slot.hp >= slot.max_hp) {
        if (trace()) std::printf("[%d] campaign: healed at the heal gate %s\n", frame, gate_mat_.c_str());
        gate_since_ = -1;
        gate_tried_.insert(stage_ + " " + gate_mat_ + " ok");
        return false;
    }
    if (gate_since_ < 0) {
        if (slot.hp * 100 >= slot.max_hp * kGateBelow) return false;
        for (const int record : {0, 1, 2, 9})
            if (disks_held(slot, record) > 0) return false;
        const auto me = position_of(player);
        std::optional<engine::Vec3f> best;
        for (const auto& box : mats->mat_boxes()) {
            if (gate_tried_.count(stage_ + " " + box.name)) continue;
            const std::string script = box.name + ".lua";
            if (!engine.script_mentions(script, "funcHealGate") || engine.script_mentions(script, "\"MP\"")) continue;
            const engine::Vec3f centre{(box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f};
            if (!best || flat(centre, me) < flat(*best, me)) {
                best = centre;
                gate_mat_ = box.name;
            }
        }
        if (!best) return false;
        gate_since_ = frame;
        gate_at_ = *best;
        if (trace()) std::printf("[%d] campaign: to the heal gate %s at %d/%d HP\n", frame, gate_mat_.c_str(), slot.hp, slot.max_hp);
    }
    if (frame - gate_since_ > kGateWalk) {
        gate_tried_.insert(stage_ + " " + gate_mat_);
        gate_since_ = -1;
        return false;
    }
    walk_to(engine, player, gate_at_, 1.5f, frame, out);
    return true;
}

// Starts a disk use (survive): the disk's line among the disks the panel
// shows (the first 10 folder slots with a count, 80349684).
void CampaignBot::start_disk(const engine::GameSystem::PlayerSlot& slot, int record, int frame) {
    disk_use_ = record;
    disk_use_at_ = frame;
    disk_quick_ = false;
    disk_held_ = disks_held(slot, record);
    disk_line_ = 0;
    bool found = false;
    int rows = 0;
    for (std::size_t i = 0; i + 2 < slot.disks.size(); ++i) {
        if (slot.disks[i].count <= 0) continue;
        ++rows;
        if (slot.disks[i].record == record) found = true;
        if (!found) ++disk_line_;
    }
    // (The cursor stays within the rows: one UP per row but the first takes
    // it to the top from anywhere.)
    disk_ups_ = std::max(rows - 1, 0);
}

// The bit capsule of a defeat in this stage, with a Raise Disk held: walked
// up to (within 8 units; the raise reaches 30) once no enemy is near, then the
// disk used (survive). True while it steers.
bool CampaignBot::raise_step(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam& player, int frame, Output& out) {
    auto* sys = engine.get<engine::GameSystem>();
    auto* models = engine.get<engine::ModelCtrlManager>();
    if (!sys || !models || engine.cheats().god) return false;
    if (disk_use_ == kRaiseDisk) return true; // (under way: stand)
    const auto& slot = sys->players[0];
    if (disks_held(slot, kRaiseDisk) <= 0 || raise_tries_ >= 3 || disk_use_ >= 0) return false;
    const engine::ModelCtrlManager::ModelRam* capsule = nullptr;
    for (const auto& e : models->rams())
        if (e->generated && e->node && e->category == "PLAYER2" && e->index == 0 && e->hp != 0) capsule = e.get();
    if (!capsule) return false;
    const auto me = position_of(player), at = position_of(*capsule);
    for (const auto& e : models->rams())
        if (e->generated && e->node && e->category == "ENEMY" && e->hp > 0 && flat(position_of(*e), me) < 35.0f &&
            engine.has_script("hit_" + e->info_name + ".lua"))
            return false; // (fought first)
    // (A capsule not reached in kRaiseWalk frames, e.g. on a ledge above the
    // route, is left for the stage.)
    if (raise_walk_since_ < 0 || frame - raise_walk_since_ > kRaiseWalk + 600) raise_walk_since_ = frame;
    if (frame - raise_walk_since_ > kRaiseWalk) {
        std::printf("[%d] campaign: bit capsule at (%.0f,%.0f,%.0f) not reached, left\n", frame, at.x, at.y, at.z);
        raise_tries_ = 99;
        return false;
    }
    if (!walk_to(engine, player, at, 8.0f, frame, out)) return true;
    if (player.panel_open) {
        if (frame - panel_close_at_ >= 600) panel_close_at_ = frame; // (closed by survive)
        return true;
    }
    if (!player.control_enabled || player.ctrl_mode == 0 || frame + 3 < panel_ready_at_) return true;
    ++raise_tries_;
    std::printf("[%d] campaign: Raise Disk at the bit capsule (%.0f,%.0f,%.0f), try %d\n", frame, at.x, at.y, at.z, raise_tries_);
    start_disk(slot, kRaiseDisk, frame);
    panel_close_at_ = frame;
    return true;
}

CampaignBot::Output CampaignBot::play(engine::Engine& engine, const engine::ModelCtrlManager::ModelRam* player, int frame) {
    Output out;
    const std::string stage = lower(engine.stage_name());
    if (stage != stage_) {
        stage_ = stage;
        entry_ = -1;
        maze_goal_.clear();
        region_now_.clear();
        region_check_ = frame;
        exhausted_since_ = -1;
        maze_since_ = -1;
        stage_frame_ = frame;
        route_.clear();
        route_frame_ = -100000;
        waited_ = 0;
        no_control_ = 0;
        stuck_since_ = -1;
        raise_tries_ = 0;
        train_leaving_ = false;
        train_exp_at_ = frame;
        boxes_seen_.clear();
        // A new visit: the ExploreBot is not asked in the lobby, so back in
        // the same field from there it kept its last visit's memory (visited
        // ground, mats, targets left alone by model pointer, which now name
        // other models): a fresh one, and the fights' pointer maps cleared.
        // (Not with the one-hit cheat: the recorded runs keep their path.)
        // A training run of the no-cheat driver wandered 160000 frames in
        // stag0101 without taking matz02 that way (s2i).
        if (!engine.cheats().one_hit) {
            explorer_ = ExploreBot{};
            foe_left_.clear();
            target_skip_.clear();
            foe_ = nullptr;
            target_ = nullptr;
        }
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
        if (!packet.ints.empty()) {
            entry_ = packet.ints[0];
            ++place_visits_[{stage, entry_}];
        }
    }
    if (stage.rfind("stagis", 0) == 0) return boot(engine, frame);
    if (!player || !player->node) {
        // Player 1 defeated: the bit capsule's game over message waits for A.
        auto* sys = engine.get<engine::GameSystem>();
        no_control_ = sys && sys->players[0].joined && sys->players[0].hp == 0 ? no_control_ + 1 : 0;
        if (no_control_ > 60 && no_control_ % 45 < 3) out.buttons |= pad_button::a;
        return out;
    }
    if (frame == stage_frame_ + 110 && std::getenv("DW4_CAMPAIGN_REACH")) reach_report(engine, *player, entry_);
    if (frame == stage_frame_ + 110 && std::getenv("DW4_CAMPAIGN_REGIONS")) region_report(engine, *player);
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
                const auto& st = slot.stats;
                std::printf("      stats STR %d DEF %d WIS %d SPI %d SPD %d skills", st.tsuyosa, st.mamori, st.kashikosa, st.seishin, st.subayasa);
                for (const int s : st.skill) std::printf(" %d", s);
                std::printf("\n");
                for (std::size_t i = 0; i < slot.devices.size(); ++i) {
                    const auto& d = slot.devices[i];
                    const char* table = tables[std::clamp(d.type, 0, 3)];
                    const auto cell = [&](const char* c) { return db->number(table, d.record, c).value_or(0); };
                    std::printf("      device %zu %s #%d %s rank %d value %d STR %d DEF %d MHP %d SOUBI %x SKILL %d/%d LV %d DELAY %d KAITORI %d%s%s\n",
                                i, table, d.record, db->text(table, d.record, "NAME").c_str(), d.rank, d.value, cell("TSUYOSA"), cell("MAMORI"),
                                cell("MAXHP"), unsigned(cell("SOUBI")), cell("SKILL"), cell("SKILLPOINT"), cell("LVPOINT"), cell("DELAY"), cell("KAITORI"),
                                d.type == 3 ? (" element " + db->text("abChip", d.record, "ZOKUSEI")).c_str() : "", d.flag ? " equipped" : "");
                }
            }
        }
    // On a maze floor: the region the player is in, every ten seconds.
    if (maze_floor(stage) && player->control_enabled && frame - region_check_ >= 600) {
        region_check_ = frame;
        const auto now = locate_region(engine, *player, stage);
        if (now != region_now_ && trace()) std::printf("[%d] campaign: in region %s of %s\n", frame, now.c_str(), stage.c_str());
        region_now_ = now;
    }
    auto step = next_step(engine);
    if (!step && !swept_.empty()) {
        // Every way led through swept maze places: sweep them again.
        swept_.clear();
        step = next_step(engine);
    }
    if (step) objective_now_ = step->why;
    // The equipment errand: the device it is for (traced); a pick the lab has
    // not taken within kEquipWalk frames is left out from then on.
    if (step && step->action.rfind("equip:", 0) == 0) {
        const auto pick = next_equip(engine);
        auto* sys = engine.get<engine::GameSystem>();
        if (sys && pick.device >= 0 && (equip_since_ < 0 || pick.kind != equip_tried_.kind || pick.device != equip_tried_.device)) {
            equip_since_ = frame;
            equip_tried_ = pick;
            const auto& d = sys->players[0].devices[std::size_t(pick.device)];
            if (auto* db = engine.get<engine::DatabaseManager>(); db && trace())
                std::printf("[%d] campaign: equips %s (device %d, %s) at the Digi-Lab\n", frame, db->text(engine::device_table(d), d.record, "NAME").c_str(),
                            pick.device, pick.kind == 0 ? "weapon" : pick.kind == 1 ? "board" : "accessory");
        } else if (sys && pick.device >= 0 && frame - equip_since_ > kEquipWalk) {
            equip_failed_.insert({pick.kind, sys->players[0].devices[std::size_t(pick.device)].record});
            std::printf("[%d] campaign: the Digi-Lab did not equip device %d, left out\n", frame, pick.device);
            equip_since_ = -1;
        }
    } else {
        equip_since_ = -1;
    }
    if (step && step->action.rfind("train:", 0) == 0) {
        // Training: the ExploreBot fights whatever it meets in the field
        // (goal "none"); once no kill has brought EXP for kTrainSpent frames
        // the field is left by its leave mats (and entered again).
        if (auto* sys = engine.get<engine::GameSystem>(); sys && sys->players[0].stats.exp != train_exp_) {
            train_exp_ = sys->players[0].stats.exp;
            train_exp_at_ = frame;
        }
        if (frame - std::max(train_exp_at_, stage_frame_) > kTrainSpent && !train_leaving_) {
            train_leaving_ = true;
            if (trace()) std::printf("[%d] campaign: training field spent, leaving by %s\n", frame, step->action.substr(6).c_str());
        }
        step->action = "explore:" + (train_leaving_ ? step->action.substr(6) : std::string("none"));
    }
    const std::string key = step ? step->stage + " " + step->action + " (" + step->why + ")" : "no step";
    if (key != traced_) {
        std::printf("[%d] campaign: %s\n", frame, key.c_str());
        traced_ = key;
        traced_at_ = frame;
    } else if ((frame - traced_at_) % 10000 == 0) {
        // A long step: the place in the log where a run stalls.
        std::printf("[%d] campaign: still %s for %d frames at (%.0f,%.0f,%.0f)\n", frame, key.c_str(), frame - traced_at_,
                    player->node->matrix(3, 0), player->node->matrix(3, 1), player->node->matrix(3, 2));
        // DW4_CAMPAIGN_STALL_DUMP=1 (research, prints only): from 20000 frames
        // on, the state of every model RAM (category, control, freeze, HP,
        // sequence) and the running demo-message chains.
        if (std::getenv("DW4_CAMPAIGN_STALL_DUMP") && frame - traced_at_ >= 20000)
            if (auto* models = engine.get<engine::ModelCtrlManager>()) {
                std::printf("[%d] stall dump: %zu demo chains running, player ctrl=%d frozen=%d seq %s\n", frame, engine.demo().running(),
                            player->control_enabled, player->frozen, player->seq_name.c_str());
                for (const auto& e : models->rams())
                    if (e->generated && e->node && (e->category == "ENEMY" || e->category == "PLAYER2" || e->info_name.rfind("g_", 0) == 0))
                        std::printf("      %s %s#%d ctrl=%d frozen=%d hp=%d mode=%d %s at (%.0f,%.0f,%.0f)\n", e->category.c_str(),
                                    e->info_name.c_str(), e->index, e->control_enabled, e->frozen, e->hp, e->ctrl_mode, e->seq_name.c_str(),
                                    position_of(*e).x, position_of(*e).y, position_of(*e).z);
            }
    }
    if (frame % 10000 == 0) std::fflush(stdout); // (a long run's log shows where it is)
    const auto me = position_of(*player);
    const bool controllable = player->control_enabled && player->seq_name.rfind("SQ_DEMO", 0) != 0;
    // DW4_TRACE_CAMPAIGN=2: the models around the player every 300 frames.
    if (frame % 300 == 0 && std::getenv("DW4_TRACE_CAMPAIGN") && std::string_view(std::getenv("DW4_TRACE_CAMPAIGN")) == "2")
        if (auto* models = engine.get<engine::ModelCtrlManager>()) {
            std::printf("[%d] campaign at (%.0f,%.0f,%.0f) %s ctrl=%d\n", frame, me.x, me.y, me.z, player->seq_name.c_str(), controllable);
            for (const auto& e : models->rams())
                if (e->generated && e->node && !e->is_player && flat(position_of(*e), me) < 150.0f && e->info_name.rfind("i_", 0) != 0)
                {
                    std::string record;
                    if (auto* db = engine.get<engine::DatabaseManager>(); db && e->category == "ENEMY" && e->record >= 0)
                        record = " record " + std::to_string(e->record) + " LV " + std::to_string(db->number("enemy", e->record, "LEVEL").value_or(-1)) +
                                 " STR " + std::to_string(db->number("enemy", e->record, "TSUYOSA").value_or(-1)) + " DEF " +
                                 std::to_string(db->number("enemy", e->record, "MAMORI").value_or(-1)) + " HPMAX " +
                                 std::to_string(db->number("enemy", e->record, "HPMAX").value_or(-1)) + " EXP " +
                                 std::to_string(db->number("enemy", e->record, "EXP").value_or(-1)) + " SPI " +
                                 std::to_string(db->number("enemy", e->record, "SEISHIN").value_or(-1)) + " RES " +
                                 std::to_string(db->number("enemy", e->record, "RES_FIRE").value_or(-1)) + "/" +
                                 std::to_string(db->number("enemy", e->record, "RES_ICE").value_or(-1)) + "/" +
                                 std::to_string(db->number("enemy", e->record, "RES_THUNDER").value_or(-1)) + "/" +
                                 std::to_string(db->number("enemy", e->record, "RES_DARK").value_or(-1));
                    std::printf("      %s#%d %s %s hp=%d ctrl=%d mode=%d (%.0f,%.0f,%.0f)%s%s%s%s\n", e->info_name.c_str(), e->index,
                                e->category.c_str(), e->seq_name.c_str(), e->hp, e->control_enabled, e->ctrl_mode, position_of(*e).x,
                                position_of(*e).y, position_of(*e).z, e->hit && e->hit->player_bullet ? " bullet" : "",
                                e->hit && e->hit->player_talk ? " talk" : "", e->ai_maps && !e->ai_maps->empty() ? " aim" : "", record.c_str());
                }
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
    // Survival: the bit capsule of a defeat here, recovered with a Raise Disk;
    // a heal gate when the HP runs low without a disk.
    if (controllable && !lobby(stage) && (raise_step(engine, *player, frame, out) || heal_gate_step(engine, *player, frame, out))) {
        last_ = me;
        return out;
    }
    // Targets of a ride: faced and struck while they are in range.
    if (controllable &&
        ride_targets_step(engine, *player, stage, step->action.rfind("explore:", 0) == 0 ? step->action.substr(8) : std::string(), frame, out)) {
        last_ = me;
        return out;
    }
    // Boss fights (kBossFights).
    if (controllable && step->action.rfind("explore:", 0) == 0)
        for (const auto& fight : kBossFights)
            if (stage == fight.stage && (!fight.real_blows || !engine.cheats().one_hit) &&
                boss_fight(engine, *player, fight.strike, fight.follow, fight.keep, frame, out)) {
                last_ = me;
                return out;
            }
    // Fields and dungeons: the ExploreBot plays them (it answers its own
    // prompts and text windows).
    if (step->action.rfind("explore:", 0) == 0) {
        const bool maze = std::any_of(std::begin(kMazes), std::end(kMazes), [&](const char* m) { return stage == m; });
        const std::string goal = maze && !maze_goal_.empty() ? maze_goal_ : step->action.substr(8);
        const auto d = explorer_.update(engine, *player, goal, frame);
        if (maze && step->action != "explore:none") maze_turn(engine, *player, step->action.substr(8), frame);
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
    // player back). One that loses no HP in ten seconds of fighting (a
    // poison plant, stag0302 g_poison01z03, or one out of reach) is left
    // alone for a minute.
    if (!lobby(stage))
        if (auto* models = engine.get<engine::ModelCtrlManager>()) {
            const engine::ModelCtrlManager::ModelRam* foe = nullptr;
            for (const auto& e : models->rams()) {
                if (!e->generated || !e->node || e->category != "ENEMY" || e->hp <= 0 || !engine.has_script("hit_" + e->info_name + ".lua"))
                    continue;
                if (const auto it = foe_left_.find(e.get()); it != foe_left_.end() && frame < it->second) continue;
                if (flat(position_of(*e), me) < 45.0f && std::abs(position_of(*e).y - me.y) < 20.0f &&
                    (!foe || flat(position_of(*e), me) < flat(position_of(*foe), me)))
                    foe = e.get();
            }
            if (foe != foe_) {
                // (The nearest of two out of reach flips as the player moves
                // between them, stag0205's block of plugs: a switch during a
                // fight keeps its time without a hit.)
                if (!foe_ || !foe || frame - foe_seen_ > 30) foe_since_ = frame;
                foe_ = foe;
                foe_hp_ = foe ? foe->hp : 0;
            } else if (foe && foe->hp < foe_hp_) {
                foe_since_ = frame;
                foe_hp_ = foe->hp;
            } else if (foe && frame - foe_since_ > 600) {
                foe_left_[foe] = frame + 3600;
                foe_ = nullptr;
                foe = nullptr;
            }
            if (foe) foe_seen_ = frame;
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
    } else if (kind == "talk" || kind == "strike" || kind == "evolve" || kind == "shop" || kind == "heal" ||
               kind == "equip" || kind == "gear") {
        if (const auto* model = find_model(engine, kind == "evolve" ? "n_digilff#1" : what, me, frame)) {
            // Ten seconds of walking without reaching one of several ("#*"; a
            // plug inside the block of the others, stag0205: the walk ends
            // short of it, and the nearest flips between two such): the one
            // walked to is left alone for a minute, the nearest other taken.
            if (target_ == nullptr || frame - target_since_ > 1200) target_since_ = frame; // (a new step)
            target_ = model;
            if (what.ends_with("#*") && frame - target_since_ > 600) {
                target_skip_[model] = frame + 3600;
                target_since_ = frame;
            }
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
                target_since_ = frame; // (reached: striking it is no stall)
                if (flat(at, origin) >= 2.0f) {
                    // Face it (the stick toward it), then A.
                    out.walk = true;
                    out.x = origin.x;
                    out.z = origin.z;
                    // (A step or two toward the model turns the player to it.)
                    const float stop = std::max(2.0f, flat(at, origin) - 2.0f);
                    out.reach = stop * stop;
                    if (frame % 20 < 3) out.buttons |= pad_button::a;
                } else {
                    // A body around the origin (the NPCs' 10-unit cube) or no
                    // hit box: A's check lies ahead of the player (the lobby
                    // talk box 5.5..8.5, the swings' talk boxes ~8.5), so from
                    // the origin it passes over the body and A never meets it
                    // (stagML12 Leomon when a walk ended in its origin). A
                    // only while 2..7 off the origin facing it; nearer, or
                    // turned away close by, the player backs off to 10 off it
                    // and walks up again.
                    const auto& pm = player->node->matrix;
                    const float fx = pm(2, 0), fz = pm(2, 2), fl = std::hypot(fx, fz);
                    const float d = flat(me, origin);
                    const float toward = fl > 0.0f && d > 0.0f ? (fx * (origin.x - me.x) + fz * (origin.z - me.z)) / (fl * d) : -1.0f;
                    if (d < 2.0f || (toward < 0.0f && d < 9.0f)) {
                        // Away along the line from the origin (from the origin
                        // itself: back the way the player came).
                        const float ax = d >= 0.5f ? (me.x - origin.x) / d : fl > 0.0f ? -fx / fl : 1.0f;
                        const float az = d >= 0.5f ? (me.z - origin.z) / d : fl > 0.0f ? -fz / fl : 0.0f;
                        if (trace() && frame % 60 == 0)
                            std::printf("[%d] campaign: backing off from %s#%d (%.1f off, facing %.2f)\n", frame, model->info_name.c_str(),
                                        model->index, d, toward);
                        walk_to(engine, *player, {origin.x + ax * 10.0f, me.y, origin.z + az * 10.0f}, 1.0f, frame, out);
                    } else if (toward > 0.87f && d <= 7.0f) {
                        // Facing it (within 30 degrees) at talking distance.
                        if (frame % 20 < 3) out.buttons |= pad_button::a;
                    } else {
                        // Up to it; the stick toward it turns the player.
                        out.walk = true;
                        out.x = origin.x;
                        out.z = origin.z;
                        out.reach = 0.0f;
                        if (toward > 0.87f && frame % 20 < 3) out.buttons |= pad_button::a;
                    }
                }
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
