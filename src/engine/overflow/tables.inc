// Data Overflow (docs/SURVIVAL.md 4-6): the design tables as code. Included
// by offers.cpp only; the director and the tests read them through the
// accessors of dw4/engine/overflow_director.hpp.
//
// The enemy rows are each family's Normal row (4.2: the generatorn block,
// never a Hard or Very Hard row): the family's first row of its own area
// (e_mecha4: row 58, the regular MRδ04 of the Machine Pit, not the
// Guardian row 67; e_mecha5: row 341, not MRε05's 339). The names are ours
// (the game has no name table for its enemies, 9.7).

// The damage factors (14.3): DW4's area rows are made for characters with
// gear and levels far above a run's (about 16 at 5:00, 28 at 10:00, 37 at
// 15:00 in the kite bot's runs), so each family gets its own factor. With
// the review round's damage growth (e^(0.18 x minutes)) the factors brought
// each family's worst measured hit (critical hits and its poison included)
// at its phase's start to about 8 % of a Rookie's MaxHP in phase 2, 9 % in
// phase 3 and 10 % in phase 4, about what the phase before hits at its end
// (the balance round of 05.11 grows slower, Tuning::damage_growth, so every
// phase hits softer; the ratios stay): no wall at a phase change (the review of O1a: phase 2
// ended near 3 %, phase 3 began at 16-21 % and the runs ended in its first
// two minutes). Within a phase the hits grow a little (the growth outruns
// the levels' DEF and MaxHP); a Corrupted one hits x 1.5, the clamp is 35 %.
// Measured by tests/overflow_director_tests.cpp ("report"); `balance` checks
// them (35 %, 16 % at a phase's start, no step above x 3).
//
// The balance round of 05.11 (docs/OVERFLOW_DIRECTOR.md 10) lowered two:
// Mummymon (a slow walker a newcomer walks into: 0.45 MaxHP a minute of
// phase 2's contact for Agumon, who has no Impact) and Tortomon (it outlasts
// Veemon's Art and Impact: 0.55-0.65 MaxHP a minute in phase 3).
//
// The ranged families (Numemon, Sukamon, MarineDevimon) shoot from beyond
// the Art's reach: they come at half the weight of the others and hit
// softer, so their chip damage does not decide a run alone; a standing
// player takes most of their shots, a walking one leaves many behind.
//
// The forge round of 05.11 (docs/OVERFLOW_DIRECTOR.md 11) lowered two more
// and moved one: MRε05 (it attacks twice a second: on the 20-25 ring, in the
// Art's reach at once, it alone took 0.26-0.34 MaxHP a minute of phase 4)
// spawns with the slow walkers now and hits softer; Tortomon took Veemon
// 0.3-0.4 MaxHP a minute of phases 3 and 4 for both bots.
//
// family, name, Normal record, phase, ranged (S1), spawn ring (Tuning: 0 32-46, 1 26-36 for
// the slow walkers of S1, Tortomon and MRε05, 2 20-25 for Kokatorimon, which never walks,
// and Megadramon, the slowest walker), damage factor, spawn weight
const FamilyRow kFamilies[] = {
    // Phase 1, Death Valley (61 RAMs)
    {"e_goburi", "Goblimon", 0, 1, false, 0, 1.0f, 1.0f},
    {"e_nume", "Numemon", 12, 1, true, 0, 0.3f, 0.5f},
    {"e_scum", "Sukamon", 24, 1, true, 0, 0.3f, 0.5f},
    {"e_ogre", "Ogremon", 33, 1, false, 0, 0.8f, 1.0f},
    {"e_cockatri", "Kokatorimon", 91, 1, false, 2, 0.25f, 1.0f},
    {"e_mecha1", "MRα01", 42, 1, false, 1, 0.66f, 1.0f},
    // Phase 2, Dry Land (33)
    {"e_mummy", "Mummymon", 114, 2, false, 1, 0.16f, 1.0f},     // (0.235 before the balance round of 05.11)
    {"e_minotaru", "Minotarumon", 159, 2, false, 1, 0.2f, 1.0f},
    {"e_pharaoh", "Pharaohmon", 168, 2, false, 1, 0.22f, 0.5f},
    {"e_mecha2", "MRβ02", 126, 2, false, 0, 0.35f, 1.0f},
    {"e_tyrano", "Tyrannomon", 150, 2, false, 1, 0.2f, 1.0f},
    // Phase 3, Venom Jungle (38)
    {"e_otama", "Otamamon", 213, 3, false, 0, 0.27f, 1.0f},
    {"e_geko", "Gekomon", 225, 3, false, 0, 0.15f, 1.0f},
    {"e_mecha3", "MRγ03", 252, 3, false, 0, 0.11f, 1.0f},
    {"e_maridevi", "MarineDevimon", 243, 3, true, 0, 0.05f, 0.5f},
    {"e_torta", "Tortomon", 234, 3, false, 1, 0.10f, 1.0f},     // (0.215 before the balance round of 05.11, 0.13 before the forge round)
    // Phase 4, Machine Pit (34)
    {"e_infermo", "Infermon", 306, 4, false, 1, 0.035f, 1.0f},
    {"e_megadra", "Megadramon", 315, 4, false, 2, 0.045f, 1.0f},
    {"e_rare", "Rare", 324, 4, false, 1, 0.061f, 1.0f},
    {"e_mecha5", "MRε05", 341, 4, false, 1, 0.045f, 1.0f},      // (0.061 on the 20-25 ring before the forge round of 05.11)
    {"e_mecha4", "MRδ04", 58, 4, false, 1, 0.043f, 1.0f},
};

// The phase music (4.2) and a stage whose sound list has it (H2 streams).
const std::array<PhaseMusic, 4> kMusic = {{
    {"ST_DEATH", "stag0101"},
    {"ST_DRYLA", "stag0201"},
    {"ST_VENOM", "stag0301"},
    {"ST_MACHI", "stag0401"},
}};

// The UPCNT stats a Plug raises (7.4; GameSystem::PlayerSlot::upcnt: 0 MaxHP,
// 1 MaxMP, 2 Strength, 3 Def., 4 Wisdom, 5 Spirit, 6 Speed, 7-10 the
// resistances), in the order the test bot's priority list takes them.
const std::array<int, 11> kPlugStats = {2, 0, 3, 4, 6, 5, 1, 7, 8, 9, 10};
const char* const kPlugNames[11] = {"MaxHP", "MaxMP", "Strength", "Def.", "Wisdom", "Spirit", "Speed",
                                    "Fire RES", "Ice RES", "Bolt RES", "Dark RES"};
