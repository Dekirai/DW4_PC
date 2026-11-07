#pragma once

// Round I1b (docs/SEAMLESS.md 19.7.4, the departure census): what the own
// player's next confirming press would start in a hub stage. Read-only: the
// accessor reads the running cursor menu and the demo work that waits on it,
// it changes nothing in the simulation (online off stays bit-identical).
//
// The census of every departure from a hub stage into a joinable stage
// (decompiled area.afs scripts, work/port-research/dec):
//   stagML11 mate00  the lobby gate's destination select (SELECTCTRL
//                    i_zzstchgcur, RAM 0): one DMcase per area with
//                    SETGENERATEPOS and NEXTSTAGE; a quest board's start sets
//                    flag 409, and the same gate then offers one row,
//                    stag0401 point 3 (quest01..04 only set flags);
//   stagML11 mate02  the return mat to the Security Gate stage (SELECTCTRL
//                    i_zzstchgwn, RAM = the mat's player): Yes = DMcase 0
//                    with SETGENERATEPOS and NEXTSTAGE of SYSgetGateStage*.
// Every other hub departure leads into a hub or UI stage (the briefing room
// stagML02, stagML12-14, the shops' stagIS*, the gate discs, the event
// console's stagML02): no arrival, no prompt. Both named departures get the
// gate form; data/online/stage_graph.json has no static edge from a hub
// stage into a field (the census test).
#include <cstdint>
#include <string>

namespace dw4::engine {

struct DepartureChoice {
    enum class Purpose : std::uint8_t { gate = 1, return_mat = 2, other = 3 };
    std::string stage;   // the NEXTSTAGE target as the script names it
    int point = 0;       // its SETGENERATEPOS point
    Purpose purpose = Purpose::other;
    std::string menu;    // the cursor menu (i_zzstchgcur, i_zzstchgwn)
    int cursor = 0;      // its cursor (the DMcase the press starts)
    int items = 0;       // its lines
    int reader = 0;      // the slot whose pad the menu reads (Engine::pad_reader_slot)
    bool operator==(const DepartureChoice&) const = default;
};

} // namespace dw4::engine
