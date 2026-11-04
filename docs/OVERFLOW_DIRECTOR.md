# Data Overflow: the director (round O1a, lane C)

What round O1a lane C built of [SURVIVAL.md](SURVIVAL.md) 15.1: the director
of a run's engine (13.5) on lane B's hooks ([SURVIVAL_O1A.md](SURVIVAL_O1A.md))
and lane A's host ([OVERFLOW_HOST.md](OVERFLOW_HOST.md)). With it the dev start
(F1 > Gameplay > Data Overflow (test)) plays a run: Agumon or Veemon on the
Violet Ring, four phases of enemy families, the Art and five techniques
firing on their own, the level-up offers, the pause, the results. Nothing of
it acts without a run engine: the story, det.sh and the campaign reference
are unchanged (section 7).

## 1. Files

| File | What |
| --- | --- |
| `include/dw4/engine/overflow_director.hpp` | `OverflowDirector` (the engine's `Director`), `RunConfig`, `Tuning` (the run's numbers), the offers' pure functions, the table accessors |
| `src/engine/overflow/director.cpp` | the clock, the phases and their music, the commands and screens (level-up, pause, Build, Retire, results), the freeze, the end (defeat, the clear at 20:00, Retire), the move-only pad filter, the kite bot, the run log and the summary |
| `src/engine/overflow/spawner.cpp` | the walkable cells, the spawn rings, A(t) with the surges, Corrupted enemies, recycling, the encirclements by reuse, the drops |
| `src/engine/overflow/abilities.cpp` | the Art (Cleaver, Blade Arc) from the form's own SQ_ATCK1 boxes, aimed; the weapon ladder and the model in hand; Fire, Cutter, Bolt, Impact and Heal Lt |
| `src/engine/overflow/offers.cpp`, `tables.inc` | the family table (Normal rows, names, rings, damage factors, weights), the phase music, the offers (Forge, Train, Plug) |
| `src/game/overflow_driver.cpp` | `make_run_driver`: the host's side (pads to moves, menu keys to commands, the overlay boxes, the summary and log files, timing) |
| `tests/overflow_director_tests.cpp` | T7: ctest `overflow_offers`, `overflow_rng`, `overflow_spawn`, `overflow_balance` |
| `tools/overflow_probe.cpp` | two more modes: `patterns FORM` (the Art boxes, the technique actions, the ladder) and `aim ACTION BEARING` (turning a broadcast's effects) |

## 2. A run as built

**Start.** The driver gives the engine `OverflowRules(RuleSet::run)` with the
director (`OverflowDirector::attach`): all 21 families as donors (S6: 166 RAMs),
the phase music from stag0101 / 0201 / 0301 / 0401 and QS_START / QS_DONOT
from dngq1b1e (H2 streams), the form's element (+15 % for its techniques),
the run's caps, curve and rule values. After the native field start the
director builds the arena's walkable cells (5-unit grid flooded from the
start: 1,837 cells, centre (0, 0), radius 120), takes the Art's boxes from
the player's sequences, puts family 1 alpha of the class in the socket
(Battle Hawk alpha / Long Sword alpha, the model in hand changed by S5's
recipe; the ladder's five weapon packages are read here, so a Forge later
reads none), plays QS_START and four seconds later the phase music.

**Clock and phases (4.2).** The run clock counts the steps the run is not
frozen. Phases 1-4 change every five minutes (ST_DEATH, ST_DRYLA, ST_VENOM,
ST_MACHI); at 20:00 the run is complete: the enemies leave (SQ_ERAS), BT_WIN02,
then the results. No Guardians in O1a.

**Spawner (4.3).** A(t) = min(60, 14 + 3.2 a minute), two spawns every ten
steps, every 90 s a surge of 12 for 20 s. The pool is the phase's families at
weight 3 and every earlier one at 0.5 (times the family's own weight, below);
a new phase's families come in over its first 90 s. Spawn points are on the
walkable cells, 70 % ahead of the player's way (its last half second), out of
the picture where one is (`on_screen`, S3), else they enter visibly with
SQ_ENTR. HP = HPMAX x (1 + 0.15 a minute), x 0.6 in phase 1. From 1:30 one
spawn a minute is Corrupted (HP x 8, the rules' damage x 1.5, the status
line's rare icon as its sign). An enemy that has not moved 4 units in 6 s
while more than 30 units away (not the ranged families), or that stays past
140 units for 5 s, is generated again in its own RAM ahead of the player. At
7:30, 12:30 and 17:30 the 16 farthest living enemies are generated again in
their own RAMs on a 45-unit ring round the player.

**The Art (6.3).** Cleaver: SQ_ATCK1's "axe" section, five 6x8x6 boxes from
z 4 to 17 (the line and the fan at its tip); Blade Arc: the "slash" section,
five 8-unit boxes in an arc. Without the animation the boxes start at once
(their own spacing kept). A strike turns the pattern to the nearest enemy
within 22 units and pushes it until its tip reaches it; the boxes of a strike
share one struck list (a target is struck once per strike). The swing's own
effect action (VATCX1 / VATCS1) is broadcast for the picture and its effects
turned the same way. Cooldown: the weapon's DELAY x 2.2 frames, -5 % per
Forge (75 / 66 frames at the start). Forge 2: a second strike on the next
target; 3: +20 % damage; 4: Cleaver's side lines at +-45 degrees, Blade Arc's
return arc; 5: Cleaver's bigger fan, Blade Arc's boxes +30 %.

**The ladder (6.3).** At every Art skill change (lane B's `skill_changed`)
the socket weapon becomes the strongest rung of the forged families whose
SKILLPOINT the skill reaches; a new family changes the model in hand in the
next step. The class cap is the forged family's epsilon SKILLPOINT (Battle
Hawk epsilon 660 at Forge 1).

**Techniques (6.4).** The form's TECH1-6 as DW4's recalc_player learns them
(the run's level lock), O1a's five only: Fire, Cutter, Bolt (2.5 s), Impact
(2.0 s, when an enemy is within 20), Heal Lt (12 s, below 70 % HP, pcTech MP
/ 8). The action is the one SQ_WAZA plays for the rank's pcTech LABEL (HFIRE1,
CUTER3, ...), broadcast without the cast animation; the shells' queued
effects are turned to the nearest enemy within 40 units (they wait for one).
The cast sound (plMAGIC1 / plIMPACT / plHEAL01) at most every 4 steps. A
heal restores at most 20 % of MaxHP (lane B's handler, the run's cap).

**Offers (4.6).** Every level opens the window two steps after the level-up
(its HUD values arrive first), frozen (subwnd.cpp's recipe). Three different
lines by weight: Forge 25, Train 25 over the form's groups with an O1a
technique (Blast and Heal for both forms), Plug 20 over its eleven stats.
Forge opens the next family; Train adds 100 to the group and raises its cap
300 > 750 > 1250 > 1750 > 2500; a Plug adds 10 + 2 x level UPCNT points. A
Plug pickup counts as the offer (its UP plus the rest).

**Drops.** A Corrupted enemy drops a Plug (g_000xdata2) and at 20 % an HP Disk
alpha (g_000item). The disk is taken with A (the name label) into the disk
folder and used from the L panel (the original's item panel).

**Moves (10).** The run's engine gets the stick and the D-pad; A only while
an item's name is up for player 1 or the item panel is open; L opens the
panel; B only with the panel open; X, Y, R, Z and START never. START opens
the pause (Resume, Build, Retire; B resumes). Retire asks "End the
simulation?" (No first). The results ("SIMULATION COMPLETE" / "SIMULATION
ENDED": time, level, form, defeats and the top five families, the damage of
each ability, the highest hit taken, the weapon) close with A.

**Defeat (4.8).** The player hit rule reports 0 HP; the knock-down plays, two
seconds later the original Game Over picture (i_fadewn01 SQ_GAMEOVER) and
QS_DONOT, then the results. A Digimon that poison takes to 0 HP never meets
the hit rule: the original's game over (end_g_000bcaps.lua, H3) ends the run
the same way.

**The overlay.** O1a's boxes are ImGui windows in the host's bold UI face
(the game's window models come with O1b), sized with the 4:3 picture: the
clock, level and defeats at the top with the last visible change (a weapon
rung, a pickup) for three seconds, the screens in the middle.

## 3. The run's numbers after the balance pass

SURVIVAL.md's initial values are starting points (its section 0). The hidden
bot runs (T6) moved these; everything is in `Tuning` and `tables.inc`:

| Value | SURVIVAL.md | O1a | Why |
| --- | --- | --- | --- |
| spawn ring | 70-95 (S1: 40-60) | 32-46; slow walkers 26-36; Kokatorimon, Tortomon, Megadramon and MRε05 20-25 | 14.2's close-in target with a kiting player |
| enemy HP growth | +10 % a minute | +15 % | the runs ended too late |
| damage taken growth | +4 % a minute | +14 % | the same |
| the damage warm-up | (none) | x 0.6 at 0:00 rising to x 1 at 5:00 | the first hits about 1-3 % of MaxHP |
| run curve | 80 / 50 / 6 | 70 / 65 / 13 | the first level-up within 20 s, one every 20-40 s in phase 1 |
| level-up heal | 3 % | 5 % (4.5's upper bound) | |
| a normal enemy's stun / sleep / bind | at most 30 frames | at most 6 | 14.2: at most 5 % of the frames without control (counted over the run's own steps; the defeat's knock-down belongs to the ending) |
| a new phase's families | at once | over 90 s | no wall at 5:00 |
| family damage factors | 1 | 0.17-1.3 per family (`tables.inc`) | 14.3: the worst hit at the phase's levels, critical hits included, Corrupted at most 35 %; phase 2 lower still, where the runs met a wall at 5:00 |
| family spawn weight | 1 | 0.5 for Numemon, Sukamon, MarineDevimon | their shots from range decided runs alone |
| Art class factor | 1 | Cleaver 1.3, Blade Arc 1.4 | 14.3: two hits for every phase 1 family at level 1 (Kokatorimon's 54 HP); Cleaver a little more so Agumon is not behind Veemon at 5:00 |

The expected levels of 4.5 (12 / 22 / 32 / 40) are measured now: the kite bot
reaches 14-17 at 5:00, 23-27 at 10:00, 32-33 at 15:00; the balance test
takes the medians 15 / 25 / 32 (and 40 at 20:00). A slow run meets phase 3
two or three levels lower, where a Corrupted enemy's critical hit can pass
35 %: the run's clamp takes it then (it never did in the 40 runs of 6.1).

## 4. The engine hooks lane C added (run engines only)

| Hook | Where | What |
| --- | --- | --- |
| a strike's shared struck list | `ModelRam::Hitbox::shared_struck`, `update_port_hitboxes` | the boxes of one Art strike strike a target once |
| `Director::damage_dealt` | `OverflowRules::dealt` | the run log's hits per kill and damage per ability |
| `OverflowRules::damage.family_factor` | `OverflowRules::taken` | the families' damage factors |
| the heal cap | `native_player_hit`'s support branch, `RuleSet::run` only | a heal restores at most `player_hit.heal_cap` (20 %) of MaxHP |
| `ded_e_*.lua` | `OverflowRules::script` | MRδ04 and MRε05 call the death script H2 never loads: answered with nothing (no "script not found") |

Port hitboxes, the director and the rules exist only in a run's engine; the
T10 comparison (`RuleSet::original`) is unchanged.

## 5. The host's side

`make_run_driver` returns the director's driver. Request keys beside lane A's:
`tier=normal`, `bot=kite` (the test bot) or `circle` (lane A's T3), `offers=
priority|random|player` (test runs default to priority), `summary=PATH` and
`log=PATH` (written when the results close), `timing=1` (T8), `commands=
900p,960m1,980a` (menu keys at director steps, for pictures of the screens),
and the balance passes' `curve=B,L,S`, `damage_per_minute=`,
`hp_per_minute=`, `alive_per_minute=`. Test runs close the results after 60
steps. `OverlayFrame::font` hands the driver the host's bold UI face.

**The kite bot** (14.1): of 16 ways, the one whose point 25 units ahead has
the least sum of 1/d^2 to the enemies within 40 units, the centre's pull past
70 % of the radius; it stands while nothing is within 20 units (the Art
reaches 22), walks at half speed below 20 and runs below 12; A on a labelled
item; L and A for the item panel below a third of the HP with a disk held.

**The run log** (13.5): seed, form, field, tier, bot, offer policy, every
command with its step, the events (start, levels, offers taken, phases,
Corrupted spawns, encirclements, notices, the end), a line a minute (level,
defeats, hits taken, clamps, living enemies, steps without control, the
highest hit) and the pads of every step run-length coded. The summary holds
14.2's numbers, the damage per ability, the hits per family, the close-in
shares, the spawner's counts, the casts and the engine's notes.

## 6. Tests (round O1a lane C)

Release build, all runs hidden or headless, other rounds running beside them.

| Test | How | Result |
| --- | --- | --- |
| T6 | `build/t/t6.sh EXE NAME FORM "SEEDS"`: `--overflow-run form=FORM field=stagbs02 tier=normal seed=N bot=kite offers=priority --smoke-test 90000`, 20 seeds a form, `build/t/t6_targets.py` | see 6.1 |
| T6 run hash | seed 1 of each form twice | the same hash, the same run log (Agumon bf30f870a71ed398, Veemon 957c268a8bee77bf); headless and drawn the same |
| T7 | ctest `overflow_offers`, `overflow_rng`, `overflow_spawn`, `overflow_balance` | pass (ctest 51 / 51) |
| T8 | `build/t/t8.sh`: `timing=1`, headless and drawn in a hidden 2560x1440 window, alone on the machine | see 6.2 |
| T3 / T4 / T5 | lane A's `t3.sh` with `bot=kite` (the director spawning and fighting), `t4.sh` | 12,200 story lines and 105 events equal; save states identical after loading at 3990 / 4600 / 5200; the cards unchanged |
| pictures | `build/t/runshots.sh`, `runshots2.sh` | the start, crowds, the level-up window, the pause, Build, Retire, the results, a clear at 20:00 (god mode) |

### 6.1 T6 against 14.2

20 seeds a form (1-20), the final numbers (`build/t/t6_targets.py`):

| Target | Agumon | Veemon |
| --- | --- | --- |
| survival 7:00-14:00 in at least 70 % | 16 / 20 = 80 % (median 10.8 min; 3 below 7:00, 1 above 14:00) | 20 / 20 = 100 % (median 10.5 min) |
| first level-up within 20 s | median 11.9 s; one seed 20.2 s | median 9.1 s, at most 12.7 s |
| phase 1: one level-up every 20-40 s | 13-15 level-ups, 21.7 s apart | 14-16, 20.2 s apart |
| Art hits per phase 1 defeat (median) | 1 | 1 |
| highest non-Guardian hit, clamps | 16.1 %, 0 | 19.0 %, 0 |
| frames without control | at most 2.9 % | at most 2.6 % |
| close in within 30 units in 8 s (walking families) | 83-100 % for every family with 30 or more spawns; Rare 76 % of 17 | 83-100 % |
| skill-up panels | at least 41 frames apart | at least 33 |
| Lua errors | 0 | 0 |

The pickups worked in every batch: Agumon 4 HP disks taken and used through
the L panel, 35 plugs; Veemon 7 and 37. The deaths gather where a phase
comes in: most runs end in the first two minutes of phase 3 (10:00-12:00), a
few at the start of phase 2, the rare survivor early in phase 4.

### 6.2 T8

`build/t/t8.sh`, Agumon seed 1 (30,521 steps, a defeat after about 8:25), alone on
the machine:

| Budget (12) | Headless | Hidden 2560x1440 window |
| --- | --- | --- |
| step p95 at most 2 ms | 1.79 ms (mean 0.52) | 1.89 ms (mean 0.56) |
| step max at most 6 ms | 9.6 ms | 9.1 ms |
| traverse + draw list p95 at most 1.5 ms | 0.36 ms (traverse only) | 0.62 ms |
| draw items at most 400 | | p95 171, max 241 |
| a pass with its present at most 8 ms | | p95 3.1 ms, max 10.4 ms |
| working set at most 600 MB | peak 429 MB (the story's engine and the run's together) | |

The step's maximum is over budget at single steps: 18011 (the phase 2
start: its music and the first GENERATEs of its families), 27001 (the 7:30
encirclement: 16 enemies generated again in one step), and a few others near
the phase change (7-9 ms). A 60 Hz frame still has room (16.7 ms); O1b can
warm the families up at the start and spread an encirclement over a few
steps.

## 7. What the original keeps

Every addition acts in a run's engine only (`Engine::overflow_run`): the
director is owned by the run's driver, the hooks above take their branch only
with `OverflowRules`. On the final build: det.sh 4004f056 / 396cb219 in two
runs of two, the r25c campaign reference 10 / 10 segments with 1,748,011
frames and every save "same story", st_all 17 / 17 identical after loading,
ctest 51 / 51, and lane A's T11 pictures identical to the integration's (F1
Settings apart from its live frame-time graph, as two runs of main).

## 8. Open for O1b and later

- The game's window models, the font of the game for the boxes (9.1); the
  HUD's build row, the EXP bar (9.3).
- Corrupted's metal look (4.4): O1a marks them with the status line's rare
  icon; ¶ Data drops have no chip to power up before O1b.
- The traits (Blaze, Chain, 5.2) and the run additions of 6.4 (Fire's splash,
  Bolt's jump, Ice's slow) are not built; Fire and Bolt are DW4's own.
- Guardians, the finale and its timer, Sim Bits, the meta file, Raise.
- The close-in target counts the walking families only; the ranged ones keep
  their distance by design (S1).
