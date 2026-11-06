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

## 3. Measurements: how to run them

All headless (`--smoke-test 100000`), nice -n 10, at most four game processes at once (one queue):

- `build/f/jobs.sh EXE NAME JOBFILE`: each line `TAG FORM SEED KEY=VALUE...` is one run; files in
  `build/f/runs_NAME/{sum,log,out}_TAG_FORM_SEED.txt`. `build/f/q.sh` is the same for forms x seeds.
- `build/f/eval_e.py DIR...`: per tag, form and seed group (a 1-20, b 101-120, c 201-220, d 301-): the median
  survival (a clear counts at its time), before 3:00 / 10:00, reach 22:00, clears, TIME UP, the fullest
  two-minute window of defeats, the highest hit, the first level-up, Awakening, the Guardians, and the early
  deaths' largest damage sources. `build/t/bal_targets.py DIR [TAG]` gives the per-minute medians.
- `build/f/spike.sh EXE NAME [KEY=VALUE]`: the Guardian spike (section 2).
- ctest: `build/ctest_all.cmd -R overflow` (12 tests, about 70 s); the full suite `build/ctest_all.cmd` (58).
- det.sh: from the main repo, `bash work/dref/checks/det.sh <exe copy> <name>`.
- r25c: from the main repo, `EXE=<exe copy> NAME=<name> nice -n 10 bash work/campaign/segments.sh r25c 4`.

(The `build/` directory is not in git; the scripts are short and described here so a restarted agent can
rewrite them: runs.sh / q.sh / jobs.sh are the O1a `build/t/runs.sh` with the seed loop, `eval_e.py` reads
the summary lines `end`, `run_seconds`, `level`, `highest_hit_share`, `first_level_up_s`, `guardians_defeated`,
`awakened`, `taken_by PHASE FAMILY SHARE`.)

## 5. Balance: what the content did to the run, and the answer

**Step by step** (kite bot unless noted; Agumon, Veemon, Guilmon, Dorumon):

| Build | What changed | Result |
| --- | --- | --- |
| k1 (5b3fa03) | Guardians at factor 1, no gear | Mammothmon's swings (36-42 % of MaxHP) and the full reactions ended most runs at 5:05-6:10; LordKnightmon hit up to 427 % with a special |
| k2 (Guardian factors, 30 % clamp, no gear) | Mammothmon x 0.4 | Guilmon still fell to Mammothmon at 5:11-5:39 (3 of 3); TonosamaGekomon (75,878 HP at 15:00) and LordKnightmon (203,930) never fell |
| k3 (e5: gear, Mammothmon x 0.25, Guardian HP x (1 + 0.10 a minute)) | seeds 1-5 | every run reached the finale; 17 of 20 cleared (LordKnightmon in 96-198 s): Agumon median 21:43 (5/5 clear), Veemon 22:41 (2/5), Guilmon 21:37 (5/5), Dorumon 23:37 (5/5); every run awakened its signature |
| m1 (e8: LordKnightmon x 14) | human Agumon seeds 1-8 | median 23:26, 5 of 8 cleared: a newcomer outlasted the 14-18 minute target by six minutes |
| s1 sweep (e8, Agumon seeds 1-4) | v1 contact 1.0/1.6/2.9/3.2, late growth 0.12; v2 1.0/1.8/3.3/3.6, 0.14; v3 1.1/2.4/3.6/3.8, 0.12; v4 1.2/2.8/4.2/4.4, 0.12 (knots at 5:00/10:00/15:00/20:00) | human 21:52 / 21:36 / **16:38** / 12:39; kite 22:40 / 22:58 / **23:08** / 22:23 |

**Why.** The gear (the Brave Core's DEF +45 / +180 / +432 at alpha / beta / gamma, a Board, two chips that
power up) and the full technique lists (Agumon's Bomb at the densest group, the Resist and Pwr Up buffs, the
Freeze / Bind pulses) halved the damage a run takes: the human bot took 0.44-0.64 MaxHP a minute at 12:00-19:00
(O1a: 1.1-1.2) and lasted into the finale. The late *contact* is the lever that keeps the 14.2 targets: a
newcomer who walks into the crowd pays for it, a kiting player does not (v3: human 16:38, kite 23:08, +6.5
minutes).

**The answer (a1fe8f6):** contact 0.7 / 1.1 / 2.4 / 3.6 / 3.8 at 0:00-20:00 (was 0.7 / 1.0 / 1.3 / 2.2 / 2.4),
the damage growth 0.12 a minute from 12:00 (was 0.09); Rare x 0.055 (was 0.061: 17.4 % at phase 4's start
for Veemon), Megadramon x 0.041 (was 0.045: a Corrupted one 36.5 % for Guilmon at 25:00). `overflow_balance`
now equips the Brave Core a run carries at the measured level (alpha, beta from Lv 22, gamma from Lv 37; every
bot takes it among its first gear offers): without it the test's gear-free Rookie meets phase 4 at 16-23 %.
LordKnightmon x 14 HP (x 8: 17 of 20 kite runs cleared, its fall after 96-198 s).

## 6. The open points of OVERFLOW_DIRECTOR.md 10.7, 11.5, 11.6

- **Kite Veemon against Tortomon (11.6):** Tortomon x 0.085 is in (from 0.10). Measured with everything else of
  this round (section 7's table, kite Veemon seeds 201-206); the round's other changes (gear, Resist, Freeze,
  Bind) changed Veemon's phase 3 more than the factor, so the measurement is of the whole, not of the factor alone.
- **Human Agumon's phase-2 deaths (Mummymon, MRβ02 from 7:00; target at most 1 of 40 before 10:00):** with the
  gear and the full lists no human Agumon run of this round ended before 10:00 (m1 seeds 1-8, s1 v1-v3 seeds
  1-4, section 7); phase 2's contact knot (1.1 at 5:00) is barely raised, the new contact is after 10:00.
  Seeds 101-120 were not run again (time).
- **Agumon's crowd answer beyond the Forge (11.5):** its own TECH5, Bomb (6.4: a bomb at the densest group
  within 40 units, a burning area), learned at run level 16 with Blast 661, fires now (k3 seed 1: 498 casts,
  1.64 M damage against Cleaver's 8.1 M and Cutter's 1.72 M), and TECH2 Pwr Up / TECH6 Resist cut what the
  crowd costs. No trait was built: the traits (5.2, Blaze: Fire's splash 12 units) are round O3's, and
  Fire's own splash of 6.4 is not built either (OVERFLOW_DIRECTOR.md 8). Before Lv 16 the Forge stays
  Agumon's crowd answer (the sure first Forge of 11.1).
- **Phase 4's entry / the late game (10.7, 11.5):** the finale replaces "25:00 stands in for the finale":
  LordKnightmon, the quest timer, BT_BOSS2, QS_HURRY, TIME UP.

## 4. Checks (the original stays the original)

| Check | Build | Result |
| --- | --- | --- |
| det.sh `o1b_content_1` | dff074d3 (e8) | 4004f056 / 396cb219 in both runs |
| r25c `o1b_content_r25c` (`segments.sh r25c 4`) | a1ac5dd (e10) | 10 / 10 segments, 1,748,011 frames, every save "same story" (wall 340 s) |
| ctest (full) | 90c0090 | 58 / 58 |
| ctest `-R overflow` | a1fe8f6 (final defaults) | 12 / 12 (overflow_guardians, overflow_gear new; overflow_balance with the four Rookies) |

The only change outside a run engine is `Engine::route_donor`'s new defaulted parameter `scripts_only`
(false everywhere but in `load_overflow_donors`): the story's randomizer takes the same path as before.
After e10 only run-engine values changed (Tuning, tables.inc, the balance test), so det.sh and r25c stand.
T3 / T4 / T5 / T8 / T11 (lane A's host scripts) were not run again in this lane: it changed no host code
except the F1 dev start's form list (`draw_dev_start`) and the driver's request keys.
