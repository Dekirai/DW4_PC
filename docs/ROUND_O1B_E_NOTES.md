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
| Balance | `tables.inc`, `guardians.cpp`, `abilities.cpp` | Tortomon x 0.085 (OVERFLOW_DIRECTOR.md 11.6's next try); the Guardians' damage factors (Mammothmon 0.15, Diaboromon 0.45, LordKnightmon 0.4, the others 1) and HP (HPMAX x 6, the finale x 14, times the time's linear part 1 + 0.10 a minute only); Bomb x 0.5 (sections 5 and 7) |
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

## 4. Checks (the original stays the original)

| Check | Build | Result |
| --- | --- | --- |
| det.sh `o1b_content_1` | dff074d3 (e8) | 4004f056 / 396cb219 in both runs |
| r25c `o1b_content_r25c` (`segments.sh r25c 4`) | a1ac5dd (e10) | 10 / 10 segments, 1,748,011 frames, every save "same story" (wall 340 s) |
| ctest (full) | 90c0090 | 58 / 58 |
| ctest `-R overflow` | f5d6f07 (the shipped state) | 12 / 12 (overflow_guardians, overflow_gear new; overflow_balance with the four Rookies) |

The only change outside a run engine is `Engine::route_donor`'s new defaulted parameter `scripts_only`
(false everywhere but in `load_overflow_donors`): the story's randomizer takes the same path as before.
After e10 only run-engine values changed (Tuning, tables.inc, the balance test), so det.sh and r25c stand.
T3 / T4 / T5 / T8 / T11 (lane A's host scripts) were not run again in this lane: it changed no host code
except the F1 dev start's form list (`draw_dev_start`) and the driver's request keys.

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

**The answer tried first (a1fe8f6, "S"):** contact 0.7 / 1.1 / 2.4 / 3.6 / 3.8 at 0:00-20:00, the damage growth
0.12 a minute from 12:00, Rare x 0.055, Megadramon x 0.041, `overflow_balance` with the Brave Core by level.
Its final measurement (section 7) ended the human runs of Veemon, Guilmon and Dorumon at 9-12 minutes while
Agumon's lasted 22 minutes: reverted (99057dc).

**The shipped state (99057dc, f5d6f07):** O1a's damage numbers (contact 0.7 / 1.0 / 1.3 / 2.2 / 2.4, growth
0.13 / 0.09, the O1a family factors, the gear-free balance test), Bomb x 0.5 on all its ranks (its burning
area strikes a whole group, as Impact's rings do: Agumon's lead came from it), Mammothmon x 0.15 (1.2-1.6
MaxHP of kite Guilmon's phase 2 at x 0.25), LordKnightmon x 14 HP (x 8: 17 of 20 kite runs cleared, its fall
after 96-198 s), A(t) halved for two minutes while a Guardian lives.

## 6. The open points of OVERFLOW_DIRECTOR.md 10.7, 11.5, 11.6

- **Kite Veemon against Tortomon (11.6): OPEN.** Tortomon x 0.085 is in (from 0.10), but the full measurement
  11.6 asks for (seeds 201-220, once with `family_damage=e_torta:0.10`, once with 0.085) was never run; the only
  kite Veemon numbers are S (201-206). The review's head runs (201-204) still show Tortomon as the largest
  source in 3 of 4 (section 11), about as in 11.6 at x 0.10.
- **Human Agumon's phase-2 deaths (Mummymon, MRβ02 from 7:00; target at most 1 of 40 before 10:00):** with the
  gear and the full lists no human Agumon run of this round ended before 10:00 (m1 seeds 1-8, s1 v1-v3 seeds
  1-4, section 7); phase 2's contact knot (1.1 at 5:00) is barely raised, the new contact is after 10:00.
  **NOT MEASURED:** the target is over 40 runs (seeds 1-20 and 101-120) and those were never run on the head;
  4-12 seeds of 1-12 do not answer it. Mummymon is still the largest source in 2 of the review's 4 head runs.
- **Agumon's crowd answer beyond the Forge (11.5):** its own TECH5, Bomb (6.4: a bomb at the densest group
  within 40 units, a burning area), learned at run level 16 with Blast 661, fires now (k3 seed 1: 498 casts,
  1.64 M damage against Cleaver's 8.1 M and Cutter's 1.72 M), and TECH2 Pwr Up / TECH6 Resist cut what the
  crowd costs. No trait was built: the traits (5.2, Blaze: Fire's splash 12 units) are round O3's, and
  Fire's own splash of 6.4 is not built either (OVERFLOW_DIRECTOR.md 8). Before Lv 16 the Forge stays
  Agumon's crowd answer (the sure first Forge of 11.1).
- **Phase 4's entry / the late game (10.7, 11.5):** the finale replaces "25:00 stands in for the finale":
  LordKnightmon, the quest timer, BT_BOSS2, QS_HURRY, TIME UP.

## 7. The final measurements (build/f/runs_f1, headless, nice 10, four at a time)

Before: main d0e005b0 (OVERFLOW_DIRECTOR.md 11.3 / 11.6: Agumon and Veemon only, no Guardians, no gear, O1a's
five techniques, the run's end at 25:00). "S": a1fe8f6 (the late contact of section 5); "V": S with contact 1.8
at 10:00 and Bomb x 0.5 (keys); "F": an approximation of 99057dc (O1a's damage numbers, Bomb x 0.5) measured
with the e11 binary and keys meant to give F's values, with Mammothmon x 0.25 instead of the shipped 0.15
(only Guilmon seeds 3-4 ran on f5d6f07); the two gameplay commits after 18:00 (panel gap, Drain cap) are not
measured. F is NOT a measurement of the shipped binary (review 06.11, section 11). A clear counts at its time; seeds 1-n unless noted.

| Bot, form | Before (main) | S (a1fe8f6) | V | F (e11 + keys, an approximation; not the head binary) |
| --- | --- | --- | --- | --- |
| human Agumon | 16:33 (1-20), 1 of 40 before 10:00 | 22:06 (12 seeds, 20:26-23:51), 4 clear, 0 before 10:00 | 19:32 (3) | 22:13 (4, 20:36-22:35), 1 clear |
| human Veemon | 15:09 (1-20) | 10:59 (6, 9:43-18:07), 1 before 10:00 | 11:44 (4) | 15:41 (4, 12:09-24:15) |
| human Guilmon | - | 8:39 (3, 5:51-10:50) | | 13:41 (2, 6:04-21:18); with Mammothmon x 0.15 (f5d6f07, seeds 3-4): 17:53 (12:35-23:12) |
| human Dorumon | - | 11:24 (3, 8:53-13:59) | | 15:08 (2, 9:04-21:11) |
| kite Agumon | 22:07 (1-20) | 21:57 (6), 3 clear | | 22:21 (2), 2 clear |
| kite Veemon | 19:52 (201-220) | 15:22 (201-206, 11:56-19:09) | | |
| kite Guilmon | - | 10:23 (3, 10:06-21:42) | | 16:15 (2, 10:27-22:03), 1 clear; with Mammothmon x 0.15 (seeds 3-4): 16:27 (10:59-21:56), 1 clear |
| kite Dorumon | - | 25:01 (3, 23:08-25:01), 2 TIME UP | | |

Every S / V run: no defeat before 3:00, no Lua error, the first level-up within 17.1 s, the highest
non-Guardian hit 18.3 %, no clamp.

**What the S runs show.** Agumon (Cleaver, Cutter, Bomb at the densest group from Lv 16) outlasts the
others by ten minutes; Veemon's, Guilmon's and Dorumon's human runs end at 9-12 minutes, in phase 2 and at
the 10:00 Guardian: Mummymon and MRβ02 (1.0-1.3 MaxHP each over phase 2), Mammothmon still alive in phase 2
(Guilmon 1.2-1.6 MaxHP: Fang Thrust's two boxes are slow against one big target), Tyrannomon, then
Anomalocarimon / SkullGreymon (about 1.1 each). So S is not the answer: the late contact punished the forms
without Agumon's crowd damage. F keeps O1a's numbers and halves Bomb (as Impact).

**What F shows** (the shipped state, few seeds): every Rookie reaches the finale in some seeds (Agumon 4 of 4
human and 2 of 2 kite, three clears; Veemon 24:15; Guilmon 21:18 and a clear at 22:03; Dorumon 21:11), no
defeat before 3:00, no Lua error, the highest hit 11.2 %. The human Veemon (15:41), Guilmon (with Mammothmon x
0.15: 17:53) and Dorumon (15:08) sit in 14.2's 14-18 minutes; the human Agumon (22:13) does not: Agumon is
still the strongest Rookie by its techniques (Cleaver x 2.0, Cutter, Bomb), the forms are not within 15 % yet.
The next balance round (with 20 seeds a form and bot): Agumon's class factor or Bomb further down, the Guardians'
HP for Guilmon's Fang Thrust (one big target), then the kite bot's clear rate against LordKnightmon x 14.

## 8. Left (not done in this lane)

- **More seeds.** 14.2 asks 20 seeds a form and bot; this round's final measurement (section 7) has 3-12. Next:
  the four Rookies x human / kite on seeds 1-20 and 301-320, human Agumon on 101-120 (11.6's 40-run tail),
  kite Veemon on 201-220 (11.6's Tortomon comparison) with `build/f/jobs.sh` (about 2.5 minutes per four runs).
- **Traits** (5.2) and Fire's splash / Bolt's jump / Ice's slow (6.4) are not built (round O3; Agumon's crowd
  answer here is its Bomb, section 6).
- **Guilmon's and Dorumon's Arts** use their own SQ_ATCK1 boxes and Blade Arc's Forge rules (a strike on the next
  target, +20 %, the mirrored return line, +30 % size). Not built: Crescent Draw's aim at the densest group and
  its wider arc and crit, Fang Thrust's pierce and reach 30 (6.3).
- **Guardians:** no camera shake at the entry; the Violet Ring's return gate after the clear (the results come
  after 4 s); the elite fallback is coded (`elite=`) but no Guardian needs it on the Violet Ring. The boss bar
  (i_bosshp02) is sent but not looked at in a picture (lane D's screenshot set should show it).
- **Awakening:** the signature takes the socket and its model comes in hand by the ladder's own model change;
  the x_100charge01 burst on every hit (6.5) is not built.
- **Gear:** the ?????? offer, the other cores, boards and chips are O2's. (Done after 18:00, section 10: the
  Drain Ram's run cap and the chips' SQ_TECUP in the rules' panel gap.)
- **Lane A's host tests** T3 / T4 / T5 / T8 / T11 were not run again (no host code changed but the dev start's
  form list and the driver's request keys).
- **The user's run** decides the finale's HP (x 14) and the late contact (section 5): the bots model a newcomer
  and a kiting player, not him.

## 10. After 18:00 (06.11, second agent): two gear points, no long runs

- **Panel gap for every SQ_TECUP** (44754d47): `OverflowRules::show_panel(engine, slot, digimon)` is the one
  place that posts i_getitempnl01 SQ_TECUP in a run; the skill ups (`skill_up`), the train offers
  (`OverflowDirector::take`) and the chips' ¶ Data power-ups (`power_up_chips`) all go through it, so a chip
  step inside the 30-step gap shows no second panel (`tally.panels_skipped`). The chip's notice line stays.
- **The Drain Ram's run cap** (f964f0fb, SURVIVAL.md 4.8): `OverflowRules::drain(engine, slot, heal)` gives
  HPDRAIN's heal of a weapon hit only up to `damage.drain_cap` (0.02) of the slot's MaxHP per second
  (engine frame / 60; `tally.drain_given` / `drain_capped`). The hook in `lua_host.cpp` BLTgetDamageValue runs
  only with `overflow_run_`; the story's HPDRAIN is unchanged (the same `change_hp` call).
- Tests: `overflow_gear` checks both (15 checks: a second panel in the same frame is left out, the tally counts
  both; HPDRAIN 1 + (3 x cap) + 5 in one second gives 1, cap - 1, 0; a new second gives the cap again).
- Checks on f964f0fb: build ok, `ctest -R overflow` 12/12, det.sh (`o1b_content_e2`) 4004f056 / 396cb219 in
  both runs; the full ctest 58/58 (e0ea58c3). r25c was not run again (no long runs after 18:00): the only
  story-side line is the `overflow_run_` branch in BLTgetDamageValue, and det.sh is equal.
- Still open from section 8: the 20-seed measurements, the form balance (human Agumon 22:13 > 14-18 min),
  Guilmon's Fang Thrust pierce / reach and Crescent Draw's aim, traits (O3), the Guardians' camera shake and
  the Violet Ring's return gate, Awakening's x_100charge01 burst, lane A's host tests T3 / T4 / T5 / T8 / T11.
  The Drain Ram cap changes no bot measurement of section 7 (no bot run starts with or takes the Drain Ram
  before Blossom Data opens it), so the tables stand.

## 9. Appendix: the measurement scripts (build/ is not in git)

`build/f/jobs.sh` (one queue, at most four game processes):

```bash
#!/bin/bash
# o1b-content: jobs.sh EXE NAME JOBFILE -- each line "TAG FORM SEED KEY=VALUE..." one headless run, ONE queue,
# at most 4 game processes at once (nice -n 10); files in build/f/runs_NAME/<kind>_<TAG>_<FORM>_<SEED>.txt.
export PATH=/usr/bin:/mingw64/bin:/c/Windows/System32:/c/Windows:$PATH
exe=$1 name=$2 jobs=$3
R=/c/Users/shinz/source/repos/DW4_PC-wt/o1b-content/build/f/runs_$name
mkdir -p $R/cards
DATA="E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted"
run() {
  local tag=$1 form=$2 seed=$3; shift 3
  local C=$R/cards/${tag}_${form}_$seed; mkdir -p "$C/Card A" "$C/Card B"
  DW4_NO_ERROR_BOX=1 nice -n 10 "$exe" --data "$DATA" --smoke-test 100000 --card-dir "$(cygpath -w $C)" \
    --overflow-run form=$form field=stagbs02 tier=normal seed=$seed "$@" \
    summary="$(cygpath -w $R/sum_${tag}_${form}_$seed.txt)" log="$(cygpath -w $R/log_${tag}_${form}_$seed.txt)" > $R/out_${tag}_${form}_$seed.txt 2>&1
}
n=0
while read -r line; do
  [ -z "$line" ] && continue
  run $line &
  n=$((n+1)); if [ $n -ge 4 ]; then wait -n; n=$((n-1)); fi
done < $jobs
wait
echo done $name
```

`build/f/eval_e.py`:

```python
# o1b-content: eval_e.py DIR [DIR...] -- per tag/form/seed group: survival (a clear counts at its time), the targets
# of SURVIVAL.md 14.2 and OVERFLOW_DIRECTOR.md 11.6, the Guardians, the early deaths' largest damage sources.
import sys, os, re, statistics, collections
def mmss(s): return "%d:%02d" % (int(s)//60, int(s)%60)
runs = collections.defaultdict(list)
for d in sys.argv[1:]:
    for name in sorted(os.listdir(d)):
        m = re.match(r'sum_(\w+?)_(p_\w+)_(\d+)\.txt$', name)
        if not m: continue
        tag, form, seed = m.group(1), m.group(2), int(m.group(3))
        kv = {}; taken = collections.Counter(); gdef = []
        for line in open(os.path.join(d, name), encoding='utf-8', errors='replace'):
            parts = line.split()
            if not parts: continue
            if parts[0] == 'taken_by' and len(parts) >= 4: taken[(int(parts[1]), parts[2])] += float(parts[3])
            elif parts[0] == 'guardians_defeated': gdef = parts[2:]
            elif len(parts) >= 2 and parts[0] not in kv: kv[parts[0]] = parts[1]
        grp = 'a' if seed <= 20 else 'b' if seed <= 120 else 'c' if seed <= 220 else 'd'
        runs[(tag, form, grp)].append(dict(seed=seed, end=kv.get('end'), s=float(kv.get('run_seconds', 0)), level=kv.get('level'),
            hit=float(kv.get('highest_hit_share', 0)), first=float(kv.get('first_level_up_s', -1)), gdef=gdef, taken=taken,
            awake=kv.get('awakened'), clamps=kv.get('clamps', '0'), lua=kv.get('lua_errors', '0'), noctl=kv.get('no_control_share', '-')))
for key in sorted(runs):
    rs = sorted(runs[key], key=lambda r: r['s'])
    t = [r['s'] for r in rs]
    clears = sum(r['end'] == 'cleared' for r in rs); tu = sum(r['end'] == 'time_up' for r in rs)
    reach22 = sum(r['s'] >= 1320 or r['end'] == 'cleared' for r in rs)
    deaths = sorted(r['s'] for r in rs if r['end'] == 'defeated')
    win = max((sum(1 for x in deaths if a <= x < a + 120) for a in deaths), default=0)
    print("== %s %s seeds %s: %d runs  median %s (%s-%s)  before 3:00 %d  before 10:00 %d  reach 22:00 %d  cleared %d  time_up %d  most deaths in 2 min %d" % (
        key[0], key[1], key[2], len(rs), mmss(statistics.median(t)), mmss(t[0]), mmss(t[-1]), sum(x < 180 for x in t), sum(x < 600 for x in t),
        reach22, clears, tu, win))
    print("   highest hit %.1f %%  first level-up max %.1f s  awakened %d/%d  Guardians defeated (median) %s  lua errors %s" % (
        100 * max(r['hit'] for r in rs), max(r['first'] for r in rs), sum(r['awake'] == '1' for r in rs), len(rs),
        statistics.median(len(r['gdef']) for r in rs), sum(int(r['lua']) for r in rs if r['lua'].isdigit())))
    for r in rs:
        if r['s'] < 600 and r['end'] == 'defeated':
            top = ', '.join('%s %d %.2f' % (f, p, v) for (p, f), v in r['taken'].most_common(3))
            print("   early: seed %d %s Lv %s; most taken: %s" % (r['seed'], mmss(r['s']), r['level'], top))
```

`build/bld.sh DIR ON x [target]` calls `build/bld.cmd` (work/port-research/build_clean.cmd with its own build dir `build/rel`, Ninja, MSVC 18 Insiders, `--parallel 5`, the FETCHCONTENT dirs of the main repo's build/Release/_deps, `-DDW4_TEST_DATA=<game data>`).

## 11. Review 06.11 18:20: the honest state (no long runs possible, PC shuts down 18:30)

The reviewer's own head runs (copy of `build/det/dw4_game_e2.exe` = f964f0fb, headless): human Agumon seeds
101-104: 21:39 clear, 20:53, 21:31, 23:57 clear (median ~21:35, none before 10:00, 0 Lua errors; taken_by
e_mummy 1.07 / 0.75 / 1.13 / 0.62 MaxHP, e_mecha2 0.72 / 0.99). Kite Veemon seeds 201-204: 22:38, 15:20,
21:23, 21:18 (median ~21:20); e_torta phase 3 1.04 / 1.78 / 1.61 / 1.13 MaxHP, phase 4 1.72 / 1.96; Diaboromon
2.34 MaxHP (201, left at 20:00 with 8511 / 27647 HP), Anomalocarimon 1.67 (202).

**SURVIVAL.md 14.2, target by target (head = f964f0fb; "F" = the section 7 approximation):**

| Target | Agumon | Veemon | Guilmon | Dorumon |
| --- | --- | --- | --- | --- |
| human median 14:00-18:00 | MISSED (head 101-104 ~21:35; F 22:13) | F 15:41 (4 seeds, approx.) - not measured on head | F 17:53 (2 seeds) - not measured on head | F 15:08 (2 seeds) - not measured on head |
| kite median 20:00-24:00 | F 22:21 (2) - not measured on head | head 201-204 ~21:20 (4 seeds); 20 seeds not measured | MISSED (F 16:15 / 16:27) | not measured |
| kite >= 3 min over human | MISSED (F 22:21 vs 22:13) | not measured | MISSED (F 16:27 vs 17:53) | not measured |
| not every seed clears | at risk (kite F 2 of 2 clear; human head 2 of 4 clear) | not measured | ok in F (1 of 2) | not measured |
| forms within 15 % | MISSED (Agumon 22:13 vs Veemon 15:41) | | | |
| <= 1 of 40 human Agumon before 10:00 | NOT MEASURED (0 of 4-12 seeds; 40 never run) | | | |
| no defeat before 3:00, no Lua error | ok in every run seen | ok | ok | ok |
| clamp never (non-Guardian) | `rules_clamps` 0 in every run seen; the summary's `clamps` also counts Guardian clamps (see below) | | | |

So 14.2 is MISSED for Agumon (human median far over 18:00: Bomb x 0.5 was not enough; the newcomer bot reaches
the finale every run and beats LordKnightmon x 14 in 2 of 4), MISSED for kite Guilmon, and NOT MEASURED for the
rest on the head binary. 11.6 (Tortomon x 0.085) is OPEN.

**Next measurements (the first thing of the next balance round, `build/f/jobs.sh`, nice 10, 4 at a time):**
1. Head binary, the four Rookies x human / kite, seeds 1-20 (and 301-320): the table above, filled.
2. Human Agumon seeds 1-20 and 101-120: the 40-run target.
3. Kite Veemon seeds 201-220 twice: `family_damage=e_torta:0.10` and `e_torta:0.085`; read the Guardians'
   taken_by too (Veemon's Guardian HP = HPMAX x 6 x (1 + 0.1 / minute): Diaboromon left alive at 20:00).
4. Then Agumon's class factor or Bomb further down (or the Guardian / finale HP for the human bot) and measure
   again.

**Not looked at in a picture (no screenshots taken in this lane):** the boss bar i_bosshp02, TIME UP, the
AWAKENING line, the build page with Core / Board / Chips. Take hidden screenshots via `commands=` with
`guardian_first` and short `end_steps`, or add them to lane D's picture set. Process violation reported by the
lane itself: once 16 game processes ran at the same time (the round's rule is 4).

**Design gaps not named in the report before:** Awakening shows no SQ_TECUP panel (6.5), no x_100charge01
burst; no camera shake at a Guardian's entry; no Violet Ring return gate; Fang Thrust's pierce / reach 30 and
Crescent Draw's aim at the densest group are missing. E sends i_bgclear nowhere: per the contract D shows the
clear banner from `d.cleared` (to check at D's merge).

**T6's command (SURVIVAL.md 14):** with the run's end at 25:00 the run clock alone needs 90000 steps
(1500 s x 60) plus the start frames, so `--smoke-test 90000` cannot reach TIME UP; this lane uses 100000.

**Fixed after the review (18:20-18:26, build ok, `ctest -R overflow` 12/12 each; det.sh / r25c / T3-T5 / T8
not run again: PC shutdown 18:30):**
- 7eeb1656: the run log's header comes from the config: `loadout ARMOR CHIP` ('-' empty), `meta plug_lab <11
  values> gear_open <keys or ->`, `guardians 0|1 guardian_set <3 families or -> elite <families or ->
  guardian_hp X finale_hp Y` (SURVIVAL.md 13); `overflow_gear` checks the three lines. `loadout=,wisdom_chip`
  now keeps the empty armor field (positional parse).
- 845b9717: a Guardian's hit at `guardian_clamp` no longer counts as 14.2's clamp: own flag
  `LastTaken::guardian_clamped`, own tally `guardian_clamps` and summary line `guardian_clamps`;
  `clamps` / `rules_clamps` are the non-Guardian clamp only. Awakening calls `rules_->show_panel` (the SQ_TECUP
  panel of 6.5, inside the panel gap). No ctest covers these two yet.
- SURVIVAL.md 14 T6: `--smoke-test 100000`.

**Still open (review findings not done for lack of time):** the finale ctest mode (a Guardian by
`guardian_first`: boss bar, clamp, drops; short `end_steps`: TIME UP / Outcome::time_up; Awakening by a set
skill; A(t)'s halving; the scripts_only fix; the Drain hook in lua_host), a ctest for `guardian_clamps`, T3 / T4
/ T5 / T8 / T11 (T8 budget is directly affected: Guardians on by default, 25:00 now TIME UP), det.sh and r25c
on 845b9717, all measurements of the table above, the pictures.
- det.sh on 845b9717 (`build/det/dw4_game_e3.exe`, name `o1b_content_e3`): 4004f056 / 396cb219 in both runs
  (log `build/det/det_e3.log`). r25c not run again (the changes are run-only: the overflow log, the rules'
  Guardian branch, the Awakening branch; no story-side line).

## 12. The finish (07.11): section 11's open points

Head before: fc9b49d3. Built (commits 127a2017, 6b7225a2, cbcb739d and later):

| Part | Where | What |
| --- | --- | --- |
| ctest `overflow_finale` | `tests/overflow_director_tests.cpp` | 42 checks in three runs: (1) a Guardian by `guardian_first` (Blossomon at run step 90): the boss bar (`guardian_bar`, i_bosshp02 generated, HPMAX x 6), the camera shake (the frame camera's view still before the entry, steps of 1.2 after it), EnemyClass::guardian, A(t) halved (`alive_target()`) and whole again after `guardian_half_steps`, a hit of 3 MaxHP taken at 30 % as `guardian_clamps` (not `clamps`) with the summary line, the separate packages' scripts (`bck_e_skulgrey.lua`, `hit_e_tonogeko.lua`: Engine::route_donor's `scripts_only`), the defeat by the Art (one-hit cheat) and both drops; (2) short phases (5 s): LordKnightmon at four phases, the phase Guardian still alive leaves, the quest timer counts to the end, TIME UP (`Outcome::time_up`, the notice), the results without a gate; (3) `forge_start=5` and the class skill set to 2500: the Awakening (the signature in the socket, the AWAKENING line, the SQ_TECUP panel), its bursts (x_100charge01 generated), the Drain Ram through BLTgetDamageValue's hook (`drain_given`), LordKnightmon's fall (the clear) and the return gate (no results after the old 4 s; the kite bot walks in) |
| Fang Thrust | `abilities.cpp` | Forge 3 "pierces 2, +20 % damage": two more boxes of the tip's spacing (5) beyond the tip, the strike's struck list; Forge 4 "one more thrust" (the second thrust 6 frames later, as before); Forge 5 "reach 30": the reach at least 30 standing (instead of +30 % size) |
| Crescent Draw | `abilities.cpp` | the first arc goes to the target in reach with the most living enemies within 10 units (the nearest of equals); a Forge 2 arc to the nearest other one |
| Awakening's burst (6.5) | `abilities.cpp`, `damage_dealt` | the data: x_100charge01 is only started by the player's charge actions of SQ_ATCK1 (probe `effects x_100charge01`: KATC?1 the glow SQ_<class>01 / 02 at 6 / 18 frames, KATC?2 the release SQ_KIRIKAE + SQ_OWARI on the player at (0, 1, -1.5), KATC?3 a charged slash of other effects); no model sequence names it. The awakened Art broadcasts its section's KATC?2 (Cleaver KATCX2, Blade Arc / Crescent Draw KATCS2, Fang Thrust KATCE2) once for every strike that hits; a picture only. Summary `awakening_bursts` (kite Agumon: 450-750 a run) |
| Guardians' camera shake | `guardians.cpp` | CAMERA SHAKE `37/F<0.3>/F<3.0>/F<60.0>` at the GENERATE: the original's stomp shake of Mammothmon (dng102b3 `wht_e_mammos.lua`); only the box camera shakes (802F3B2C) |
| The Violet Ring's return gate (4.4) | `director.cpp` `step_gate`, `gate_pad` | the original: Belphemon's `hit_f_belvam.lua` generates g_dreturngate `V<0,10,-60>/V<0,0,0>/SQ_ENTR` and SQ_POWERON 30 frames later; `matz01.lua` (box (-6, -66)-(6, -54), probe `mats`) changes the stage with the prompt once every player stands in it. In a run: the clear pauses the arena's mats (MATCTRL PAUSE: no prompt), the gate comes `gate_wait` (120) steps later, powers on 30 later, the player keeps the stick, and walking into matz01's box opens the results (a player standing there when it comes steps out and in, as the mat's bit change); the kite and human bots walk in; a test bot that has not after `gate_bot_steps` (60 s) gets the results anyway. Retire during it: the results at once. Elsewhere or with `return_gate=0`: the results after 4 s. Summary `gate 1 entered` |
| Test keys | `RunConfig`, the driver | `forge_start=N` (Forge families open at the start), `art_skill=N` (the class skill at the start), `phase=SECONDS` (a phase's length; the finale at four), `return_gate=0`; `OverflowDirector::alive_target()` (the spawner's A(t)), `awakening_bursts()`, `return_gate_open()`, `gate_entered()` |
| Probe | `tools/overflow_probe.cpp` | `effects NAME [FORM]` (commands naming NAME, action starters of NAME*; `PROBE_SEQUENCES=1` dumps its sequences), `mats` |

Measurements: headless, `nice -n 15`, at most three game processes at once (another workflow ran timing-sensitive
scenarios): `build/f/jobs3.sh` (jobs.sh of section 9 with 3 and nice 15), `build/f/eval_e.py`.

### 12.1 The Agumon lever: the Art and Bomb do not shorten the human runs, Pwr Up does

Before (`build/f/runs_b1`, binary g1 = 6b7225a2: the content of this section, the balance of fc9b49d3), seeds 1-20:
human Agumon 22:34 (6:31-23:33), 1 before 10:00, 15 reach 22:00, 8 clear; kite Agumon 23:02 (21:35-23:56), every
one of the 20 clears. The human bot takes 0.3-0.65 MaxHP a minute from 7:00 (Veemon's 0.65-1.5) and Heal Lt
covers it; its deaths come at the finale (21-23.5 minutes).

Sweeps, seeds 1-8 each (`runs_b1`, `runs_g2`, `runs_g3`, `runs_g4`; `build/f/sweep.py`; a clear counts at its time):

| Variant (key) | human median (range), before 10:00, clears | kite median (range), clears / TIME UP |
| --- | --- | --- |
| before (Cleaver x 2.0, Bomb x 0.5, Pwr Up every 15 s) | 22:22 (21:08-23:29), 0, 3 | 23:03 (22:13-23:56), 8 / 0 |
| Cleaver x 1.6 (`art_class=1.6`) | 23:45 (6:25-25:01), 1, 4 (2 TIME UP) | 24:10, 7 / 0 |
| Cleaver x 1.4 | 21:31 (6:03-25:01), **3**, 2 | 24:32, 5 / 2 |
| Cutter x 0.5 and Bomb x 0.25 (`technique_damage=`) | 22:00, 0, 3 | |
| Pwr Up every 30 s (`technique_cooldown=80:1800`) | 21:47, 0, 1 | |
| Pwr Up every 45 s | 21:16 (20:18-23:26), 0, 2 | 22:33 (21:39-23:32), 3 / 0 |
| **Pwr Up every 60 s** (shipped) | 20:25 (6:28-23:40), 1, 0 | 22:34 (20:40-25:01), 4 / 1 |
| no Pwr Up (`80:0`) | 18:48 (6:25-21:26), 2, 0 | |
| no Pwr Up, no Resist | 20:49, 2, 0 | |
| the force buffs 6-10 s (6.4; `buff=360,60`) | 19:28 (6:26-21:55), 1, 0 | **18:12** (16:15-22:17), 0 / 0 |
| ... Resist keeps DW4's time (`buff_kept=75`), 5 seeds | 19:15, 1, 1 | 17:21 (16:12-18:01), 0 / 0 |
| ... 10-14 s (`buff=600,60`), 4-5 seeds | 18:17, 1, 1 | 18:10, 0 / 0 |

**Why.** A weaker Art does not shorten the human runs: the spawner keeps A(t) alive whatever dies, so the crowd
round the player stays (near15 1.1-2.5 either way), and the runs that reach the finale clear later (Cleaver x 1.6:
23:45), or a weak early Art ends some in phase 2 (x 1.4: 3 of 8 before 10:00). The human Agumon lives on Pwr Up:
TECH2 from run level 8, DW4's SETPOWERUP with TIME 1800-3240 frames plus the caster's INT and +80 / +240 / +480 /
+960 / +1920 STR, DEF and MaxHP by rank (pcTech 80-84), cast every 15 s while an enemy is within 30: on for good.
Without it 18:48. 6.4's 6-10 s by rank (OverflowRules::buff_time, kept as the key `buff=`) cut both bots to 17-19
minutes: the kite bot loses the buff as much as the newcomer, and the kite Agumon fell at 16-22 minutes (MRβ02's
shots and poison in phase 2, phase 4's families): no lever for 14.2. A longer cooldown keeps DW4's times: every 60 s
the buff is off for a while at Petty / Mega (30-36 s plus INT), on for good at Terra / ∞ later.

**Measured and not shipped** (the head keeps Pwr Up every 15 s; the cap off, `buff_steps` 0; the late cooldown
off, `pwr_up_late_cooldown` 0; both stay as keys): 120 s from 10:00 (a039783e, `runs_gF3`, the full measurement):
human Agumon 21:03 (seeds 1-20) and 20:08 (101-120), 2 of 40 before 10:00, kite 22:05, 4 of 20 clear, but 2 of 20
kite runs had hits over 35 % of MaxHP (the clamp: 4302-63924 raw against about 3100 MaxHP, e_rare, e_mecha4,
e_mecha5, e_maridevi in phase 4) and 10 of 20 kite deaths fell within two minutes (14.2: at most 9). Every variant
that shortens the buff's uptime clamps (the 6-10 s cap in 12 of 13 runs, 60 s from 10:00 in one of 8): the late
damage curve (contact 2.2 / 2.4, the growth after 12:00) was built on Pwr Up's +960 / +1920 DEF. The human target
needs that curve redone for every form (Dorumon, without Pwr Up, lasts as long), a new balance round. On the way: 60 s from the start (d401cc11, `runs_gF`) gave human Agumon 20:52 (seeds 1-20) and 20:49 (101-117) but 6 of
37 runs ended at 5:41-6:37, the first minute of phase 2 (before: 1 of 20); 60 s from 10:00 (1f6bb526, `runs_gF2`)
kept the buff on (Giga to ∞ last 42-54 s plus INT): seeds 1-8 human / kite 21:39 / 23:22, 6 of 8 clear. Seeds 1-8
(`runs_gF2`): 120 s from 10:00 20:58 (13:38-23:01) / 22:51 (20:34-24:05), 3 of 8 clear, none before 10:00; 90 s from
5:00 20:11 / 22:23 with one run ended at 6:35.

### 12.2 The head's measurement (c48738d1), 20 seeds a form and bot

`build/f/final` (`final_set.sh`): the runs whose code path the head shares bit for bit, checked by run hashes
(Agumon g1 = head: 08a6212c26fd2654 / 52e34d6ed1cbaebd for seed 1 human / kite; Dorumon g3 = head: 1c51ef04f0bb35ce /
c47b55c29e20d031; Veemon gF = head: e90cfff25d9a2a3f / f8ea436b118dbfdf): Agumon 1-20 from `runs_b1`, Agumon 101-120,
Guilmon and the rest of Veemon from `runs_gB` (the head's exe), Dorumon from `runs_g3` / `runs_g4`. Headless,
`--smoke-test 100000`, nice 15, three at a time. Before: section 7's F (an approximation, 2-4 seeds) and the review's
head runs (section 11).

| Bot, form, seeds | Before | After: median (range) | before 10:00 | reach 22:00 | clear / TIME UP | most deaths in 2 min |
| --- | --- | --- | --- | --- | --- | --- |
| human Agumon 1-20 | F 22:13 (4) | 22:34 (6:31-23:33) | 1 | 15 | 8 / 0 | 9 |
| human Agumon 101-120 | head 101-104 ~21:35 | 22:11 (6:31-24:15) | 1 | 15 | 11 / 0 | 8 |
| human Veemon 1-20 | F 15:41 (4) | 21:05 (6:20-25:01) | 1 | 5 | 1 / 1 | 9 |
| human Guilmon 1-20 | F 17:53 (2) | 21:59 (6:16-24:58) | 2 | 11 | 7 / 0 | 5 |
| human Dorumon 1-20 | F 15:08 (2) | 22:09 (11:34-25:01) | 0 | 11 | 0 / 4 | **11** |
| kite Agumon 1-20 | F 22:21 (2) | 23:02 (21:35-23:56) | 0 | 20 | **20** / 0 | 0 |
| kite Veemon 1-20 | - | 23:07 (16:37-25:01) | 0 | 14 | 2 / 3 | 9 |
| kite Guilmon 1-20 | F 16:27 (2) | 22:58 (20:58-24:05) | 0 | 20 | **20** / 0 | 0 |
| kite Dorumon 1-20 | - | **25:01** (21:58-25:01) | 0 | 19 | 3 / 14 | 2 |
| kite Veemon 201-220, Tortomon x 0.085 (shipped) | head 201-204 ~21:20 | 22:19 (15:41-25:01) | 0 | 14 | 2 / 3 | 8 |
| kite Veemon 201-220, Tortomon x 0.10 | (11.6: 19:52 on O1a's head) | 22:54 (13:05-25:01) | 0 | 13 | 0 / 3 | 8 |

(The F column is the approximation of section 7; its Veemon, Guilmon and Dorumon human numbers came from 2-4 seeds and
are not borne out by 20.)

**SURVIVAL.md 14.2 on the head:**

| Target | Agumon | Veemon | Guilmon | Dorumon |
| --- | --- | --- | --- | --- |
| human median 14:00-18:00 | MISSED 22:34 (101-120: 22:11) | MISSED 21:05 | MISSED 21:59 | MISSED 22:09 |
| kite median 20:00-24:00, at least 6 of 20 at 22:00 | ok 23:02, 20 | ok 23:07, 14 | ok 22:58, 20 | MISSED 25:01 (14 TIME UP), 19 |
| not every seed clears (kite) | MISSED 20 of 20 | ok 2 | MISSED 20 of 20 | ok 3 |
| kite at least 3 minutes over human | MISSED +0.5 | MISSED +2.0 | MISSED +1.0 | MISSED +2.9 |
| no wall (at most 9 of 20 in two minutes) | ok 9 / 0 | ok 9 / 9 | ok 5 / 0 | MISSED human 11 |
| forms within 15 % (Agumon / Veemon) | ok human 22:34 / 21:05 (7 %), kite 23:02 / 23:07 | | | |
| at most 1 of 40 human Agumon before 10:00 | MISSED narrowly: 2 of 40 (seeds 14 and 117, both 6:31, the first minute of phase 2) | | | |
| no defeat before 3:00, no Lua error | ok | ok | ok | ok |
| highest non-Guardian hit at most 35 %, no clamp | ok 11.1 % / 10.9 %, 0 | ok 13.3 % / 15.8 %, 0 | ok 12.8 % / 10.7 %, 0 | ok 10.7 % / 14.3 %, 0 |
| first level-up at most 18 s | ok 15.7 s | ok 18.0 s | ok 17.9 s | MISSED narrowly 18.2 s (human) |
| frames without control at most 5 % | ok 2.1 / 3.2 % | ok 2.0 / 4.0 % | ok 1.8 / 2.6 % | ok 2.0 / 3.1 % |
| enemies that close in at least 80 % | ok 98 / 94 % | ok 94 / 87 % | ok 93 / 93 % | ok 98 / 92 % |
| skill-up panels at most one every 30 frames | ok 30 | ok 30 | ok 30 | ok 30 |

**What it says.** The four Rookies now sit within 7 % of each other (human 21:05-22:34, kite 22:58-25:01): the
O1b content (gear, the full technique lists with Pwr Up / Resist / Freeze / Bind, the Guardians' halved A(t)) made
the run long for every form, not for Agumon alone. The newcomer reaches the finale in about half the runs and the
kite bot clears with Agumon and Guilmon every time. Section 12.1 shows why no Agumon lever fixes it: the late damage
curve (contact 2.2 / 2.4, the growth after 12:00) is survivable with the buffs on and a wall (hits past 35 %) without
them. **Next balance round:** the late curve for all four forms with the buffs as they are (for example the contact
from 10:00, LordKnightmon's HP or damage for Agumon's and Guilmon's clears), then the human target over 20 seeds a form.

**11.6 (Tortomon x 0.085 against 0.10, kite Veemon 201-220): answered.** Both meet the kite targets on the head (22:19
/ 22:54, 14 / 13 at 22:00, no wall); Tortomon is still the largest single source of those runs (1.19 / 1.23 MaxHP per
phase it is in, phases 3 and 4). The difference lies within the seeds' spread: x 0.085 stays.

### 12.3 Checks on the head (c48738d1, `build/det/dw4_game_fin.exe`; scripts in the ignored `build/t2`)

| Check | Result |
| --- | --- |
| T1 det.sh (`det15.sh`: nice 15) `o1b_content_fin` | 4004f056 / 396cb219 in both runs |
| T2 r25c (`segments.sh r25c 3`, nice 15) `o1b_content_fin_r25c` | 10 / 10 segments, 1,748,011 frames, every save "same story" (wall 302 s). Lane E has no NPC: the run with the NPC shown is lane D's and the integration's |
| ctest (full, `build/ctest_all.cmd`) | 59 / 59 (main's 58 + `overflow_finale`, 45 checks) |
| T3 (`t35.sh`) | the det script with a run at story frame 3000 (A: Agumon, kite bot, a Guardian after 10 s; A2: Guilmon, circle bot) and without (B): all 20 fingerprint lines equal (4004f056 = det's), the Guardian entered (Mammothmon / Blossomon at run step 600), the runs retired after 1,861 steps |
| T5 (`t35.sh`) | the card folders equal (the det script saves nothing: empty in all three), the achievement profiles equal (9f14f1fc) |
| T4 (`t4.sh`) | DW4_STATE_TEST at 2990 / 3400 / 4400 with the run at 3000: "identical after loading" (3010 / 2600 / 1600 frames compared) |
| T11 (`t11.sh`) | hidden pictures of the movie (1400), the title (2600), dng101b1 (4800, 5900) and the new game's lobby (4800, 5900) with main's client d0e005b0 and the head: all six equal |
| T8 (`t8.sh`, Agumon seed 1, kite, 25:00 with Guardians and the clear; nice 15 beside the full ctest and another workflow's scenarios) | the same run hash headless and drawn (52e34d6ed1cbaebd = the measurement's seed 1); headless step mean 0.90 ms, p95 3.06 ms, max 229.7 ms (budget p95 2 / max 6: MISSED; O1a had 0.57 / 1.96 / 9.7; a single 230 ms step under idle priority); drawn at 2560x1440: step p95 3.24, max 43.5 ms; traverse + draw list p95 0.82 ms (ok), draw items p95 287, max 370 (ok), a pass with its present p95 4.6 ms (ok), max 45 ms |
| T6 run hash twice | seed 1 kite: 52e34d6ed1cbaebd in `runs_b1`, `runs_gB`, T8 headless and drawn |

Hidden pictures (`build/t2/shots/*.png`, not in git; `run_shot.sh`): `guardian_overflow_1290` (Mammothmon by
`guardian_first=20`, the boss bar, "Mammothmon appears"), `finale_overflow_2460` (`phase=10 end=60`: LordKnightmon,
"LordKnightmon: the finale"), `timeup_overflow_3640` (TIME UP), `awaken_overflow_40` (`forge_start=5 art_skill=2500`
Guilmon: "AWAKENING: Data Destroyer"), `gate_overflow_2800` (`DW4_GOD=1 DW4_ONE_HIT=1 DW4_OVERFLOW_TEST_CHEATS=1`:
g_dreturngate on the far side of the ring, "The return gate is open"; the kite bot walked in, `gate 1 entered`). The
quest timer's digits are lane D's HUD.

### 12.4 Left after the finish

- The 14.2 balance above (all four forms' human runs 21-22.5 minutes; Agumon's and Guilmon's kite runs always clear;
  Dorumon's kite runs mostly TIME UP): a balance round over every form's late curve (12.2).
- T8's step p95 / max over budget on a loaded machine: measure again on a quiet one; warm the families up (O1a 8).
- Crescent Draw's wider arc and crit steps, Fang Thrust's numbers per 6.3's table (O2); traits (O3).
- The quest timer's digits, the clear banner and the gate's walk-in want a look in lane D's picture set after the merge.
