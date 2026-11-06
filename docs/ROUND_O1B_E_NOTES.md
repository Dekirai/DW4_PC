# Round O1b lane E (content): notes

Branch `feat-o1b-content` from main d0e005b0, worktree `DW4_PC-wt/o1b-content`. Design: SURVIVAL.md 15.2
(lane E), 4.4, 5, 6.4-6.5, 7, 14; OVERFLOW_DIRECTOR.md 10.7, 11.5, 11.6. Merge order of the round: E, D, F.
The lane contract (outside the repo): `DW4_PC-wt/o1b-contract.md`, entries "E content".

## 1. Done

| Part | Where | What |
| --- | --- | --- |
| Guardians of phases 1-3 | `src/engine/overflow/guardians.cpp` | drawn from 4.4's pairs by the seed (`draw_guardians`, its own splitmix64 stream: the spawner's draws are unchanged); loaded by H2 from their stages, never with a `ded_*.lua`; enter at 5:00 / 10:00 / 15:00, 40 units in front of the player, SQ_ENTR, the arena's boss bar (i_bosshp02 GENERATE V<0,205,0>/SQ_OPEN, METERCTRL bshp02 SETMODELHP family/I<ram>, SQ_CLOSE when it falls), EnemyClass::guardian (the original reactions); A(t) halves for at most 2 minutes while one lives; the next waits for the one before |
| Guardian music | `step_music` | BT_SBOSS (from dng101b2's sound list) while a Guardian lives, the phase music after it falls |
| Guardians' drops | `guardian_down` | Recovery (conItem 9) and ¶ Data UP 100 (insItem 3) |
| LordKnightmon and the finale | `guardians.cpp`, `director.cpp` | at 20:00 (a phase Guardian still alive leaves, SQ_ERAS), BT_BOSS2 (the Violet Ring's own), the quest timer to 25:00 (`finale_left_steps()`), QS_HURRY in its last minute, z3COUNT2 at 10 s, z3COUNT1 on the last three (dngq3b1e); its defeat clears the run (BT_WIN02, `Outcome::cleared`); the timer's 0:00 is `Outcome::time_up` (z3COUNT3, QS_DONOT, "TIME UP", enemies SQ_ERAS, results after 4 s). The return gate after a clear is not built (the results come after 4 s, as 4.4 says for other arenas) |
| Guardian reactions | `player_hit.cpp` (O1a) | the original reaction by damage rank for EnemyClass::guardian (already built in O1a lane B); new: a Guardian's hit takes at most `Tuning::guardian_clamp` (30 %) of MaxHP, `OverflowRules::Damage::guardian_clamp` |
| Separate packages' scripts | `donors.cpp`, `Engine::route_donor(..., scripts_only)` | the spike found SkullGreymon without `bck_e_skulgrey.lua` and TonosamaGekomon without `hit_e_tonogeko.lua` (it could not be damaged): a separate package's scripts come from its stage's main member now (scripts only; the story's randomizer never sets the flag) |
| Guilmon, Dorumon | `abilities.cpp` kArts | Fang Thrust (dagger rows 50-74, VATCE1, SQ_ATCK1 "dagger": two thrust boxes z 11 / 16, class factor 2.2), Crescent Draw (katana rows 75-99, VATCS1, "slash": the 5-box arc, 1.5); elements Dark / Ice (+15 %); the F1 dev start lists the four Rookies |
| Full technique lists | `abilities.cpp` kTechniques | every TECH1-6 of the four Rookies: + Ice 5, Dark 15, Venom 20 and Bomb 35 (at the densest group within 40), Resist 75 and Pwr Up 80 (buffs, with an enemy within 30, 15 s), Freeze 70, Pwr Dwn 85 (8 s) and Bind 95 (10 s) (pulses with three enemies within 30). Train Force shows now (the forms have force techniques) |
| Awakening | `equip_ladder` | with the last family forged and the class skill at the signature's SKILLPOINT (2500) the form's signature (weapon rows 250-265, its SOUBI bit) takes the socket and stays; "AWAKENING: <name>", plLVUP01, `awakened()` |
| Gear | `src/engine/overflow/gear.cpp` | Brave Core (armor1 0-2: alpha, beta at Lv 22, gamma at Lv 37, then +30/+60/+120/+240), one Board (Armor 21, Fire 9, Ice 12, Bolt 15, Dark 18: alpha, then +30 ... +480 value), chips in the weapon pocket (POWERPOCKET) and the core pocket (MAINPOCKET, open from the core's beta): Wisdom 0 / Spirit 15 / Speed 30 Chip, the form's PWR Medal (45 / 60 / 75 / 90), Drain Ram 105 (opened by `gear_open`); chips step to beta / gamma by offers (the Ram beta..epsilon); ¶ Data (Corrupted: UP 20, Guardians: UP 100) powers the pocket chips up at NEXTX / 20, weapon pocket first; the loadout (`RunConfig::loadout_armor / loadout_chip`) at the start; offers Core / Board / Chip at 30 % weight (`OfferState::gear`, filled by `gear_offers`); the build page shows core, board, chips and ¶ Data |
| Balance | `tables.inc`, `guardians.cpp` | Tortomon x 0.085 (OVERFLOW_DIRECTOR.md 11.6's next try); the Guardians' damage factors (Mammothmon 0.25, Diaboromon 0.45, LordKnightmon 0.4, the others 1) and HP (HPMAX x 6, the finale x 8, times the time's linear part 1 + 0.10 a minute only) |
| Tests | `tests/overflow_director_tests.cpp`, CMake | ctest `overflow_guardians` (plans, pairs, forced, gear keys, gear offers' weight, priority); `overflow_balance` covers the four Rookies (two hits early, no one-hit enemy) |
| Probe | `tools/overflow_probe.cpp` | `names TABLE COLUMNS...` (the gear rows) |

## 2. The spike per Guardian (SURVIVAL.md 4.4, 16)

`build/f/spike.sh EXE NAME [KEY=VALUE]`: each Guardian forced into phase 1 (`guardian_set=G,...`) at 0:30
(`guardian_first=30`), Agumon, kite bot, seed 1, the run ending at 4:00-5:00. First with x 6 HP (`sp1`), then,
after the scripts fix, with x 0.6 HP (`sp2`, whether it can die):

| Guardian | Enters | Fights (hits on the player at 0:30) | Dies without its ded_ | Note |
| --- | --- | --- | --- | --- |
| Blossomon | yes | 6 hits, up to 10 % | yes (108 s at x 6, 36 s at x 0.6) | |
| Mammothmon | yes | 7 hits, up to 16 % (kills a level 3 Agumon) | yes (5 s at x 0.6) | its swings 36-42 % at 5:00 with factor 1: 0.25 |
| SkullGreymon | yes | 3 hits, up to 11 % | yes after the scripts fix (27 s at x 0.6) | `bck_e_skulgrey.lua` was missing (separate package) |
| Anomalocarimon | yes | 6 hits, up to 22 % | yes in the T6 runs at 10:00 (71-186 s) | kills a level 3 Agumon at 0:30 |
| TonosamaGekomon | yes | 4 hits, up to 41 % | yes after the scripts fix (74 s at x 0.6) | `hit_e_tonogeko.lua` was missing: it took no damage |
| Diaboromon | yes | 3 hits, up to 42 % | (T6 runs at 15:00) | kills a level 3 Agumon at 0:30 |
| LordKnightmon | yes | 2 hits, up to 72 % (and 427 % with a special at 20:00, factor 1) | (T6 runs at 20:00) | factor 0.4 and the 30 % clamp |

No Guardian needs the elite fallback on the Violet Ring (`elite=` keeps it for a later arena). Every Guardian
walks to the player and attacks; none stands at its stage's own points.

## 3. Measurements

See section 5 (filled as the runs finish). The scripts: `build/f/q.sh EXE NAME TAG "FORMS" "SEEDS" KEY=VALUE...`
(one queue, at most four game processes, nice -n 10), `build/f/spike.sh`, `build/t/bal_targets.py DIR`.

## 4. Left (not done in this lane)

- See the end of this file (kept current).

## 5. Runs
