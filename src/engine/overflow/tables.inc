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
// gear and levels far above a run's (12 at 5:00, 22 at 10:00, 32 at 15:00,
// 40 at 20:00, 4.5), so each family gets its own factor that brings its
// worst measured hit (critical hits included) at its phase's start and end
// level to about 16 % of a Rookie's MaxHP in phase 1 and 12 % from phase 2
// on, where crowds hit more often (24 % / 18 % for a Corrupted one; the
// clamp is 35 %). Pharaohmon warps to the player: x 0.6 more. Measured by
// tests/overflow_director_tests.cpp ("report"); `balance` checks them.
//
// The ranged families (Numemon, Sukamon, MarineDevimon) shoot from beyond
// the Art's reach: they come at half the weight of the others and hit
// softer, so their chip damage does not decide a run alone.
//
// family, name, Normal record, phase, ranged (S1), spawn ring (Tuning: 0 32-46, 1 26-36 for
// the slow walkers of S1, 2 20-25 for Kokatorimon, which never walks, and
// Tortomon, Megadramon and MRε05, the slowest walkers), damage factor, spawn weight
const FamilyRow kFamilies[] = {
    // Phase 1, Death Valley (61 RAMs)
    {"e_goburi", "Goblimon", 0, 1, false, 0, 1.0f, 1.0f},
    {"e_nume", "Numemon", 12, 1, true, 0, 0.3f, 0.5f},
    {"e_scum", "Sukamon", 24, 1, true, 0, 0.3f, 0.5f},
    {"e_ogre", "Ogremon", 33, 1, false, 0, 0.8f, 1.0f},
    {"e_cockatri", "Kokatorimon", 91, 1, false, 2, 0.25f, 1.0f},
    {"e_mecha1", "MRα01", 42, 1, false, 1, 0.66f, 1.0f},
    // Phase 2, Dry Land (33)
    {"e_mummy", "Mummymon", 114, 2, false, 1, 0.28f, 1.0f},
    {"e_minotaru", "Minotarumon", 159, 2, false, 1, 0.23f, 1.0f},
    {"e_pharaoh", "Pharaohmon", 168, 2, false, 1, 0.17f, 1.0f},
    {"e_mecha2", "MRβ02", 126, 2, false, 0, 0.4f, 1.0f},
    {"e_tyrano", "Tyrannomon", 150, 2, false, 1, 0.24f, 1.0f},
    // Phase 3, Venom Jungle (38)
    {"e_otama", "Otamamon", 213, 3, false, 0, 1.3f, 1.0f},
    {"e_geko", "Gekomon", 225, 3, false, 0, 1.05f, 1.0f},
    {"e_mecha3", "MRγ03", 252, 3, false, 0, 0.63f, 1.0f},
    {"e_maridevi", "MarineDevimon", 243, 3, true, 0, 0.38f, 0.5f},
    {"e_torta", "Tortomon", 234, 3, false, 2, 1.1f, 1.0f},
    // Phase 4, Machine Pit (34)
    {"e_infermo", "Infermon", 306, 4, false, 1, 0.32f, 1.0f},
    {"e_megadra", "Megadramon", 315, 4, false, 2, 0.36f, 1.0f},
    {"e_rare", "Rare", 324, 4, false, 1, 0.66f, 1.0f},
    {"e_mecha5", "MRε05", 341, 4, false, 2, 0.55f, 1.0f},
    {"e_mecha4", "MRδ04", 58, 4, false, 1, 0.37f, 1.0f},
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
