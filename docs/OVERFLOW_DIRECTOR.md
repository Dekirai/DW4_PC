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
| `tests/overflow_director_tests.cpp` | T7: ctest `overflow_offers`, `overflow_rng`, `overflow_spawn`, `overflow_poison`, `overflow_balance` |
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

**Spawner (4.3).** A(t) by knots: 10 at 0:00, 24 at 5:00, 40 at 10:00, 54 at
15:00, 60 at 20:00 (the review round: min(60, 14 + 3.2 a minute)), at most
two spawns every ten steps and at most 3 / 5 / 8 / 12 a second at those
times (a lull after the Art clears a group), from 3:00 every 90 s a surge of
12 for 20 s; until 10:00 a spawn point never closes the player's last
90-degree way out (section 10). The pool is the phase's families at
weight 3 and every earlier one at 0.5 (times the family's own weight, below);
a new phase's families come in over its first 90 s, their weight rising from
0, and their damage rises from a fifth of their factor to all of it over its
first two minutes (no wall at a phase change, section 9). Spawn points are on
the walkable cells; 70 % come on the side the player walks towards (its last
half second), beside its way (40-70 degrees off the heading, so a walking
player passes them in the Art's reach instead of running into them), out of
the picture where one is (`on_screen`, S3), else they enter visibly with
SQ_ENTR at least 40 units away (never next to the player; the families that
never walk keep their 20-25 ring). HP = HPMAX x (1 + 0.10 a minute) x
e^(0.08 x minutes), x 0.6 in phase 1 rising to x 1 from 4:00 to 6:00. From
2:30 one spawn a minute is Corrupted (HP x 8, the rules' damage x 1.5, the
status line's rare icon as its sign). An enemy is generated again in its own
RAM ahead of the player, with the HP share it had, when it has not moved 4
units in 6 s while more than 30 units away (not the ranged families), stays
past 140 units for 5 s, or is left behind by a walking player: it has not
come 2 units closer for 4 s while more than 40 units away (4.3's rule; not
the ranged families, whose shots a walking player leaves behind). At 7:30,
12:30 and 17:30 the 16 farthest living enemies are generated again in their
own RAMs on a 45-unit ring round the player, four a step, with the HP they
had.

**The Art (6.3).** Cleaver: SQ_ATCK1's "axe" section, five 6x8x6 boxes from
z 4 to 17 (the line and the fan at its tip); Blade Arc: the "slash" section,
five 8-unit boxes in an arc. Without the animation the boxes start at once
(their own spacing kept). A strike turns the pattern to the nearest enemy
within its reach and pushes it until its tip reaches it: 14 units standing (22 before section 10),
growing with the walking speed of the last half second to 34 at full stick
(the enemies close in 3 to 40 times slower than the player walks, S1, so a
walking Digimon passes them where a standing one waits); the boxes of a strike
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
Impact's rings strike every enemy within 20 at once: its damage x 0.5
(`technique_damage`).

**Offers (4.6).** A level opens the window two steps after the level-up (its
HUD values arrive first), frozen (subwnd.cpp's recipe), at most one window
(freeze) every 15 s of run time: levels that come sooner wait and open one
after another in the next window. Three different lines by weight: Forge 25,
Train 25 over the form's groups with an O1a technique (Blast and Heal for
both forms), Plug 20 over its eleven stats (a resistance plug at a quarter of
a stat plug's share: few O1a attacks carry an element). Train shows from two
levels before its group's first technique (Train Heal from level 13 for the
Heal Lt of level 15, not at level 2). Forge opens the next family; Train adds
100 to the group and raises its cap 300 > 750 > 1250 > 1750 > 2500; a Plug
adds 10 + 2 x level UPCNT points. A Plug pickup counts as the offer (its UP
plus the rest). Each line has a short detail ("Art: a second line on the next
target", "Heal Lt at Lv 15; Heal +100", "Strength +14"), and the line under
the cursor one sentence beside "A Take" ("The heart of the build: Cleaver
grows at once, and the weapon may climb to Van Blade ε (Blunt skill 1300).",
"Heal Lt is learned at Lv 15; until then this only stores Heal skill.",
"Fire RES +14 for this run: only fire attacks hit softer.").

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
QS_DONOT, then the results. Poison never ends a run: its ticks go through the
run's damage-taken rule, at most five of them and 10 % of MaxHP a poisoning,
and a tick never takes the last HP (SURVIVAL_O1A.md 3, the review round), so
every defeat is the hit rule's. (The original's game over, end_g_000bcaps.lua
through H3, would still end a run the same way.)

**The overlay.** O1a's boxes are ImGui windows in the host's bold UI face
(the game's window models come with O1b), sized with the 4:3 picture: the
clock, level and defeats at the top with the last visible change (a weapon
rung, a pickup) for three seconds, the screens in the middle.

## 3. The run's numbers after the balance pass

SURVIVAL.md's initial values are starting points (its section 0). The hidden
bot runs (T6) moved these, and the review round (section 9) moved them again;
everything is in `Tuning` and `tables.inc`:

| Value | SURVIVAL.md | O1a | Why |
| --- | --- | --- | --- |
| spawn ring | 70-95 (S1: 40-60) | 32-46; slow walkers and Tortomon 26-36; Kokatorimon, Megadramon and MRε05 20-25; in the picture at least 40 | 14.2's close-in target with a kiting player; no enemy enters next to the player |
| spawns ahead | 70 % from the walking side | 70 %, 40-70 degrees off the heading | a walking player passes them, it does not run into them |
| enemy HP growth | +10 % a minute | (1 + 0.10 a minute) x e^(0.08 x minutes), x 0.6 in phase 1 rising to x 1 from 4:00 to 6:00 | late crowds outlast the Art; no step at 5:00 |
| damage taken growth | +4 % a minute | e^(0.13 x minutes) (the review round: 0.18) | the review: the levels' DEF and MaxHP shrank every phase's hits to a third by its end, so each new phase came as a step (the wall at 10:00); the balance round of 05.11: a good run lasts 15-20 minutes (section 10) |
| the damage warm-up | (none) | x 0.2 at 0:00 rising to x 1 at 8:00 (the review round: x 0.4 to 5:00) | a newcomer's first minutes (section 10) |
| contact and shots | (none) | a hit from within 15 units x 0.7 / 1.0 / 1.3 / 1.5 / 1.6 at 0:00 / 5:00 / 10:00 / 15:00 / 20:00, from farther x 0.8 / 0.7 / 0.65 / 0.6 / 0.6 (its poison with it) | the first minutes' contact soft; later the crowd a good player walks away from is the threat, not the shots he cannot (section 10) |
| A(t) | 14 + 3.2 a minute | 10 / 24 / 40 / 54 / 60 at 0:00 / 5:00 / ... / 20:00; at most 3 / 5 / 8 / 12 / 12 spawns a second | a newcomer was surrounded in two minutes (section 10) |
| the way out | (none) | until 10:00 a spawn never closes the last 90 degrees round the player (enemies within 40 units) | "no gap to run" (section 10) |
| first surge, first Corrupted | 1:30, 1:30 | 3:00, 2:30 | the newcomer's deaths came just after 1:30 |
| EXP | x 1 | x 1.35 | fewer enemies, the same level pace (Heal Lt at Lv 15 near 5:00) |
| a new phase's families | at once | weight from 0 over 90 s, damage from x 0.2 over 120 s | no wall at a phase change |
| run curve | 80 / 50 / 6 | 70 / 65 / 13 | the first level-up within 20 s, one every 20-40 s in phase 1 |
| level-up windows | one per level | at most one freeze every 15 s (the waiting levels open one after another) | the first minute brought five freezes |
| level-up heal | 3 % | 5 % (4.5's upper bound) | |
| MUTEKI after a hit | 50 frames | 35 | a crowd round a standing Digimon costs; a walking one leaves it |
| a normal enemy's stun / sleep / bind | at most 30 frames | at most 6 | 14.2: at most 5 % of the frames without control (counted over the run's own steps; the defeat's knock-down belongs to the ending) |
| poison | DW4's | ticks through the damage-taken rule, at most 5 ticks and 10 % of MaxHP, never the last HP | the review: MRβ02's poison took 69 % from a hit of 1 |
| family damage factors | 1 | 0.035-1.0 per family (`tables.inc`; Mummymon 0.16 and Tortomon 0.13 since the balance round of 05.11) | 14.3: the worst hit (its poison included) at a phase's start about 8 / 9 / 10 % in phases 2 / 3 / 4, close to what the phase before hits at its end; Corrupted at most 35 % |
| family spawn weight | 1 | 0.5 for Numemon, Sukamon, MarineDevimon and Pharaohmon | the ranged shots and Pharaohmon's warps hit a walking player as much as a standing one |
| Art reach | 22 (Cleaver, Blade Arc) | 14 standing (the review round: 22), 34 at full stick; 20 from 30 % of the stick | the enemies are 3-40 times slower than the player (S1): a walking Digimon passes them; a standing one must be reached again (section 10) |
| Art class factor | 1 | Cleaver 2.2, Blade Arc 1.4 | 14.3: two hits for every phase 1 family at level 1; Cleaver more: Agumon has no Impact and no jumping Bolt |
| technique factor | 1 | Impact x 0.5 | its rings strike every enemy within 20 at once each time |

The expected levels of 4.5 (12 / 22 / 32 / 40): the kite bot reaches about
16 at 5:00, 26 at 10:00 and 33 at 15:00 now (section 6.1); the balance test
keeps the medians of lane C's runs, 15 / 25 / 32 (and 40 at 20:00). A slow
run meets a phase a few levels lower, where its hits are larger: the run's
clamp (35 %) takes a Corrupted enemy's critical hit then.

## 4. The engine hooks lane C added (run engines only)

| Hook | Where | What |
| --- | --- | --- |
| a strike's shared struck list | `ModelRam::Hitbox::shared_struck`, `update_port_hitboxes` | the boxes of one Art strike strike a target once |
| `Director::damage_dealt` | `OverflowRules::dealt` | the run log's hits per kill and damage per ability |
| `OverflowRules::damage.family_factor` | `OverflowRules::taken` | the families' damage factors |
| the heal cap | `native_player_hit`'s support branch, `RuleSet::run` only | a heal restores at most `player_hit.heal_cap` (20 %) of MaxHP |
| `ded_e_*.lua` | `OverflowRules::script` | MRδ04 and MRε05 call the death script H2 never loads: answered with nothing (no "script not found") |
| the run's poison (review round) | `native_player_hit` (`damage_statuses`, `RuleSet::run` only), `OverflowRules::poison`; `status.cpp`'s poison tick | a normal or Corrupted enemy's poison through the damage-taken rule, at most 5 ticks and 10 % of MaxHP, never the player's last HP (SURVIVAL_O1A.md 3); the hit's report carries it (`HitTaken::poison_tick`, `poison_ticks`) |

Port hitboxes, the director and the rules exist only in a run's engine; the
T10 comparison (`RuleSet::original`) is unchanged.

## 5. The host's side

`make_run_driver` returns the director's driver. Request keys beside lane A's:
`tier=normal`, `bot=kite` (the test bot), `human` (a newcomer, section
10.1), `circle` (lane A's T3: full stick round a slow circle, a player who
never stops walking) or `stand` (no input, a player who never walks),
`offers=priority|random|newcomer|player` (test runs default to priority,
the human bot to newcomer), `summary=PATH` and `log=PATH` (written when the results
close), `timing=1` (T8), `commands=900p,960m1,980a` (menu keys at director
steps, for pictures of the screens), and the balance passes' `curve=B,L,S`,
`damage_per_minute=`, `damage_growth=`, `damage_warm_up=`, `hp_per_minute=`,
`hp_growth=`, `warm_up=`, `alive_per_minute=`, `art_range=`,
`art_range_moving=`, `art_class=`, `ahead_share=`, `ahead_angle_min=`,
`ahead_angle_max=`, `visible_min=`, `behind_far=`, `phase_intro=`,
`bot_flee=`, `window_gap=`, `muteki=`, `mp_regen=`,
`family_damage=FAMILY:F,...` and `technique_damage=ROW:F,...`; since the
balance round of 05.11 also `alive=`, `refill=`, `contact=` and `shot=`
(five knots at 0:00-20:00), `surge_first=`, `keep_gap=`,
`corrupted_from=`, `phase_ramp=`, `phase_intro_time=` (seconds),
`surge_extra=`, `gap_radius=`, `damage_warm_minutes=`, `ring_min=`,
`ring_max=`, `slow_min=`, `slow_max=`, `corrupted_hp=` and `exp=`. Test runs
close the results after 60 steps. `OverlayFrame::font` hands the driver the
host's bold UI face.

**The kite bot** (14.1, as a player of the move-only mode plays it): it
never stands. With an enemy within 16 units it walks away from the enemies
within 40 (of 16 ways the one whose point 25 units ahead has the least sum of
1/d^2 to them; 0.7 of the stick, full stick below 8); else it walks at half
the stick towards the nearest enemy within 60 (into the Art's reach), else on
its last way; the centre's pull past 70 % of the radius, a small cost for
turning. A on a labelled item; L and A for the item panel below a third of
the HP with a disk held. (Lane C's bot stood until an enemy came within 20,
so the balance was made for a player who waits; the review round's.)

**The run log** (13.5): seed, form, field, tier, bot, offer policy, every
command with its step, the events (start, levels, offers taken, phases,
Corrupted spawns, encirclements, notices, the end with its reason), a line a
minute (level, defeats, hits taken, clamps, living enemies, steps without
control, the highest hit, the damage taken in the minute in MaxHPs, its
poison included) and the pads of every step run-length coded. The summary
holds 14.2's numbers, the end's reason (`player_defeated`, `game_over`,
`stage_exit`, `cleared`, `retired`), the level-up windows of the first minute
and their gaps, the damage per ability, the hits per family (a hit's share
with its poison), the poisonings and the largest, the close-in shares, the
spawner's counts, the casts and the engine's notes. `build/t/fix_targets.py`
reads them per bot and form (survival, the death spread, the damage taken a
minute, levels, windows, hits).

## 6. Tests (round O1a lane C and its review round)

Release build, all runs hidden or headless, other rounds running beside them.
The numbers are the review round's final build (section 9); lane C's first
numbers are in the git history of this file.

| Test | How | Result |
| --- | --- | --- |
| T6 | `build/t/runs.sh EXE NAME TAG FORM "SEEDS" bot=kite` (as `t6.sh`: `--overflow-run form=FORM field=stagbs02 tier=normal seed=N bot=kite offers=priority --smoke-test 90000`), 20 seeds a form, `build/t/fix_targets.py` | see 6.1 |
| T6 run hash | seed 1 of each form twice, Agumon once more drawn in a hidden window at a virtual 144 Hz (`DW4_RENDER=1 DW4_DEV_PRESENT=144 DW4_DEV_SMOOTH=1`) | the same hash and the same run log (Agumon 8da422d504d5b458, Veemon 6194b570b2fe81e9), drawn the same |
| movement | the same with `bot=stand` (8 seeds) and `bot=circle` (6 seeds) a form | see 6.3 |
| T7 | ctest `overflow_offers`, `overflow_rng`, `overflow_spawn`, `overflow_poison`, `overflow_balance`, `overflow_rules` | pass (ctest 52 / 52) |
| T8 | `build/t/t8.sh`: `timing=1`, headless and drawn in a hidden 2560x1440 window | see 6.2 |
| T3 / T4 / T5 | lane A's `t3.sh` with `bot=kite` and `bot=circle`, `t4.sh` | 12,200 story lines and 105 events equal; save states identical after loading at 3990 / 4600 / 5200; the cards and the achievement profile unchanged, no throwaway card folder left |
| T10 | ctest `overflow_scripts` | pass (the original rule set's player hit script unchanged) |
| pictures | `build/t/runshots.sh` | the start (no enemy enters next to the player), the level-up window with the line about the cursor's offer, crowds |

### 6.1 T6 against 14.2

20 seeds a form (1-20), the final build (`build/t/fix_targets.py runs_fx`):

| Target | Agumon | Veemon |
| --- | --- | --- |
| survival 7:00-14:00 in at least 70 % | 18 / 20 = 90 % (median 8.3 min, 4:21-12:55) | 20 / 20 = 100 % (median 10.5 min, 8:40-11:52) |
| first level-up within 20 s | median 5.3 s, at most 9.1 s | median 7.9 s, at most 11.4 s |
| phase 1: one level-up every 20-40 s | 13-14 level-ups (19 s apart); 3 level-up windows in the first minute, then one every 20 s or more | 16-17 (19 s apart); 3 windows in the first minute, then one every 20 s or more |
| Art hits per phase 1 defeat (median) | 1 | 1 |
| highest non-Guardian hit (its poison included), clamps | 18.3 %, 0 | 10.6 %, 0 |
| poison | 220 poisonings, the largest 10.0 % of MaxHP; no run ended by it | 250, 10.0 %; none |
| frames without control | at most 1.1 % | at most 0.7 % |
| close in within 30 units in 8 s (walking families) | at least 98 % for every family | at least 96 % |
| skill-up panels | at least 32 frames apart | at least 34 |
| Lua errors | 0 | 0 |

The damage taken a minute (the median over the runs alive through it, in
MaxHPs, poison included) rises without a step: Agumon 0.34 / 0.22 / 0.26 /
0.27 / 0.23 in phase 1, 0.75 in the minute after 5:00 (the new families come
in), then 1.28 / 1.47 / 1.36 / 1.27, 1.17 after 10:00; Veemon 0.36 / 0.25 /
0.14 / 0.12 / 0.09, 0.47, 1.08 / 1.38 / 1.37 / 1.44. The runs end when the
damage outgrows Heal Lt (about 1 MaxHP a minute), spread over phase 2 and
phase 3 (Agumon 4:21-12:55, at most 12 of 20 deaths in any two minutes;
Veemon 8:40-11:52, 13 of 20); before the review all ended in the first two
minutes of phase 3, where 40 s after 10:00 brought 142 % of MaxHP.

### 6.2 T8

`build/t/t8.sh`, Agumon seed 1 (28,559 steps, a defeat after about 7:50),
three runs of other rounds on the machine:

| Budget (12) | Headless | Hidden 2560x1440 window |
| --- | --- | --- |
| step p95 at most 2 ms | 1.96 ms (mean 0.57) | 2.04 ms (mean 0.61) |
| step max at most 6 ms | 9.7 ms | 10.5 ms |
| traverse + draw list p95 at most 1.5 ms | 0.37 ms (traverse only) | 0.66 ms |
| draw items at most 400 | | p95 195, max 279 |
| a pass with its present at most 8 ms | | p95 3.1 ms, max 12.1 ms |

The encirclement places four enemies a step now, and neither it nor the
phase 2 start is among the five slowest steps any more; the maximum is still
over budget at single steps in phase 2 (the first GENERATEs of the families
that come in, the recycling of the left-behind enemies), with other rounds'
runs beside it. A 60 Hz frame has room (16.7 ms); warming the families up at
the start stays open for O1b (section 8).

### 6.3 Moving against standing (the review)

The same build and request with the three bots, survival in minutes (median,
range) and the level at 5:00:

| Bot | Agumon | Veemon |
| --- | --- | --- |
| kite (moves all the time, 20 seeds) | 8.3 (4.4-12.9), Lv 15 | 10.5 (8.7-11.9), Lv 18 |
| circle (full stick round a circle, never stops; 6 seeds) | 4.7 (4.5-5.7), Lv 10, 7-8 level-ups in phase 1 | 5.2 (5.0-6.2), Lv 10, 8-10 |
| stand (no input; 8 seeds) | 3.4 (1.8-13.3; 6 of 8 below 4 min) | 2.3 (1.4-8.9) |

Before the review, standing (Agumon 10:53-11:16, Lv 31-32) lasted as long
as kiting and the circle got 6 level-ups in phase 1 (Lv 7-8 at 5:00). Now a
Digimon that walks outlasts one that stands, and one that never stops walking
gets 7-10 level-ups in phase 1 and outlasts one that never walks; the kite bot
(walks away from the near enemies, into the reach of the far ones) lasts
longest.

## 7. What the original keeps

Every addition acts in a run's engine only (`Engine::overflow_run`): the
director is owned by the run's driver, the hooks above take their branch only
with `OverflowRules` (the review round's poison tick in `status.cpp` too). On
the review round's final build: det.sh 4004f056 / 396cb219 in two runs of
two, the r25c campaign reference 10 / 10 segments with 1,748,011 frames and
every save "same story" (every segment's frames as lane C's), st_all 17 / 17
identical after loading, ctest 52 / 52, and T11's pictures identical to lane
C's final build (F1 Settings apart from its live frame-time graph, the same
box as between two runs of main).

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
- Phase 4 and the 20:00 end: the kite bot ends in phases 2 and 3 now (14.2's
  7-14 minutes); a human with O1b's gear may go further. Phase 3 is reached
  by 11 of 20 Veemon and 4 of 20 Agumon runs.
- T8's single-step maximum (section 6.2): warm the families up at the start.
- (Done: main 64d0b61 is merged into dref-o1a, round 60's ghost figures
  ported into `HostView`, docs/OVERFLOW_HOST.md 9. Round 63 added terms to
  the net fingerprint, so every run hash of section 6 changed with the merge
  while the runs stayed the same: Agumon seed 1 is d9ced6aba81ccd47 now,
  Veemon seed 1 e22487c8310bfd94, and the merged build without round 63's
  terms gives 8da422d504d5b458 again.)

## 9. The review round

The review of the merged O1a (branch dref-o1a) found three problems and a
list of small ones; each is answered here, with its test.

| Finding | Cause | Answer | Test |
| --- | --- | --- | --- |
| A wall at 10:00: 14 of 20 Agumon and 10 of 12 Veemon kite runs ended between 10:10 and 12:09; 40 s after 10:00 brought 142 % of MaxHP, 1,509 of it from Tortomon | the damage grew 14 % a minute, the levels' DEF and MaxHP faster: every phase's hits shrank to a third by its end (phase 2 ended near 3 %), and the factors put phase 3 at 16-21 % (Tortomon 22 %, on the 20-25 ring inside the Art's reach) | the damage grows e^(0.18 x minutes); the family factors bring each family's worst hit at its phase's start to about 8 / 9 / 10 % (phases 2-4), near what the phase before hits at its end; a new phase's families come in from weight 0 over 90 s and from a fifth of their damage over 120 s; Tortomon on the slow walkers' ring 26-36, x 0.45; the HP warm-up rises to x 1 from 4:00 to 6:00 instead of a step at 5:00 | ctest `overflow_balance` (no family above 16 % at its phase's start, no phase start above 3 times the phase before's end; it reports x 0.8-1.3 now, the review's build x 6-6.6 at phase 3); T6 (6.1): the damage taken a minute rises without a step, the deaths spread over phases 2 and 3 |
| Standing as good as dodging, walking all the time punished: `bot=stand` lasted as long as the kite bot (Agumon 10:53-11:16, Lv 31-32), `bot=circle` died at 5:10-5:56 with 6 level-ups in phase 1 | the Art reached 22 units, the enemies close in 3-40 times slower than the player walks, the followers of a walking player were never brought back, spawns came on its way, MUTEKI 50 let a standing Digimon take one hit a second whatever the crowd, and the kite bot stood until an enemy came within 20 | the Art's reach grows with the walking speed to 34 at full stick; a walking player's left-behind followers come again on its way (4.3's rule) and the spawns ahead come beside its way (40-70 degrees); MUTEKI 30; Pharaohmon (warps) at half weight; the kite bot keeps moving (away from enemies within 16, into the reach of the others); Cleaver x 2.0 and Impact x 0.5 so neither form outlasts the other by minutes | 6.3 (`bot=stand`, `bot=circle`, `bot=kite`); ctest `overflow_spawn` (a walking player's followers come again: 45 in 15 s against 4 standing) |
| Poison went around the run's damage rule: MRβ02's rank 3 poison took 69 % of MaxHP from a hit of 1; 7 of 32 kite runs ended by it, outside the hit rule | `damage_statuses` passed DW4's BLPgetPoisonTime(DAMAGE) through unchanged: MaxHP x KOUKARANK / 100 every 2 s for 23 ticks | the run's poison rule (section 4, SURVIVAL_O1A.md 3) | ctest `overflow_rules` (run_poison: at most 10 % a poisoning for every rank and factor), `overflow_poison` (5 HP and eight ticks of 50: 1 HP left, the run goes on; MRβ02 round the player: the largest poisoning 6.2 %); T6: no run ended by poison, the largest poisoning 10.0 %, and 14.2's highest hit counts it |
| T8: the step maximum over budget at the phase 2 start and the 7:30 encirclement | 16 GENERATEs in one step | the encirclement places four a step | ctest `overflow_spawn` (the 15 placed within five steps, 14 on the 45 ring); T8 (6.2) |
| Enemies enter next to the player (10 within 10-30 units at 0:01) | the 32-46 ring is in the box camera's picture nearly everywhere | a spawn in the picture comes at least 40 units away (the families that never walk keep 20-25) | ctest `overflow_spawn` (300 points of the 26-36 ring from the start: all in the picture, all at least 39 away); the start picture |
| Level-up windows heaped at the start (4.8 freezes in the first minute) | the early levels come every 10-12 s | at most one window every 20 s, the levels that come sooner open one after another in it; the curve 70 / 65 / 16 | T6: 3 windows in the first minute, then at least 20 s apart |
| The offers inform badly: Train Heal from level 2 for a Heal Lt of level 15, plugs like Fire RES +14 | Train came whenever the form had a technique of the group | Train waits until two levels before its group's first technique; a resistance plug weighs a quarter; a detail per line and a sentence about the cursor's offer (Forge as the heart of the build, a locked technique's level, what a resistance plug does) | ctest `overflow_offers`; the level-up picture |
| Recycling and encirclements brought hurt enemies back healed | GENERATE sets the full HP | the new HP keeps the old share (SETHP) | ctest `overflow_spawn` (an encircled enemy at half HP stays at half) |
| The run loop never called `DiscordPresence::configure` | | as the story loop: every pass with the F1 setting (test runs on) | T3 (the run's activity set as before); by reading (a test run always switches it on) |
| `game_main.cpp` conflicts with main 9700b11 | | the merge of main 64d0b61 (docs/OVERFLOW_HOST.md 9) | T11, T3, the run's log and summary, P1 / P3 / M1 / S1 (OVERFLOW_HOST.md 9) |

## 10. The balance round of 05.11 (the user's first run)

The user played O1a (05.11, Agumon): "feels quite good already, but I was
dead after 2 minutes - definitely has potential". Too many enemies (round
him quickly, no gap to run) and too much damage (a few hits cost a lot).
The targets (SURVIVAL.md 17.1: a good run lasts 22-25 minutes; the first
minutes gentle): a newcomer does not die in the first five minutes unless
he stands in a crowd, normally reaches phase 2 and sees 10:00. Branch
`fix-o1a-balance` from main b226bc0. This section is the round's progress
note.

### 10.1 The human bot (`bot=human`)

`src/engine/overflow/human_bot.cpp`: a newcomer as the user played. It sees
the enemies 15-18 steps late (250-300 ms) and looks again every 6-14 steps
while it flees, every 20-40 while it roams; its stick turns to the new way
in about five steps. With an enemy 11-16 units away (as it sees them) it
flees from the middle of the enemies within 22 (weighted 1/d), its way off
by up to 30 degrees and one flight in seven by 70 more, at full stick, and
holds the way 0.4-0.9 s: no search for the gap, so it runs into the next
group at times. Otherwise it roams to points of the arena (one goal in four
the middle of a group, to let the Art work) at 0.75-1.0 of the stick,
stops 0.3-1.2 s now and then when nothing is within 25 units, and bends
away from the ring's wall. It takes an HP disk 0.75 s after its HP fell
below a quarter, and the offers as a newcomer (`offers=newcomer`, its
default: the first line or, as often, a random one). Its draws come from
its own generator seeded from the run's seed (the spawner's draws are
untouched): one seed, one run (ctest `overflow_human`).

The run log's minute lines (and the summary's) add the damage taken by
source in MaxHPs (`contact`: the attacker within 15 units, `shot`: farther,
`poison`), the mean of the living enemies within 15 and 30 units
(`near15`, `near30`) and the share of steps without a 90-degree way out
between the enemies within 20 (`trapped`); the summary adds `taken_by
PHASE FAMILY` (the damage taken per phase and family) and `gap_refused`.
`build/t/bal_targets.py DIR [TAG...] [--md] [--fam]` reads them: the death
times, the round's targets, the per-minute medians over the runs alive
through the minute, the damage per family and phase a run-minute.

### 10.2 Before (main b226bc0 with the bot)

20 seeds a form for `human` and `kite`, 10 for `stand` and `circle`,
headless (`build/t/batch.sh`); the kite bot's runs are the review round's
(the same death times as 6.1, so the bot's helpers changed nothing):

| Bot | Agumon: death times | Veemon |
| --- | --- | --- |
| human | median 2:09 (1:20-3:31, one 13:07); 15 of 20 dead before 3:00, 1 reaches 5:00 | median 2:18 (1:16-3:24, one 12:11); 16 before 3:00, 1 reaches 5:00 |
| kite | median 8:18 (4:20-12:54); 4 reach 10:00 | median 10:32 (8:39-11:52); 11 reach 10:00 |
| stand | median 3:19 (1:48-13:15) | median 2:26 (1:25-8:55) |
| circle | median 4:55 (4:27-5:51) | median 5:00 (3:24-6:11) |

The human bot dies where the user died. Its minutes (Agumon; the medians
over the runs alive through the minute; damage in MaxHPs a minute):

| min | taken | contact | shot | poison | near15 | near30 | trapped | alive | level | runs |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 0.58 | 0.45 | 0.08 | 0.00 | 0.73 | 3.66 | 0.02 | 17 | 6 | 20 |
| 2 | 0.62 | 0.56 | 0.06 | 0.00 | 1.08 | 5.57 | 0.04 | 20 | 9 | 16 |
| 3 | 0.50 | 0.47 | 0.03 | 0.00 | 0.91 | 4.84 | 0.02 | 23 | 11 | 5 |

Veemon: 0.59 / 0.55 / 0.29 taken, 0.52 / 0.49 / 0.28 of it contact. The
kite bot (Agumon) in the same minutes: 0.34 / 0.22 / 0.26 taken, 0.07 /
0.06 / 0.08 contact (near15 0.18-0.24), then 0.23 at 4:00-5:00, 0.77 and
1.39 in the two minutes after 5:00 (shots 0.45 / 0.85, poison 0.14 / 0.31).

### 10.3 Why a newcomer died at about 2:00

- **Contact, not single hits.** The human bot's hits were 1.6-2.7 % of
  MaxHP each (1.8 % on average, the highest 10 %), but 27 a minute: 0.45-0.56
  MaxHP a minute came from attackers within 15 units, against 0.06-0.08 for
  the kite bot. It had 0.7-1.1 enemies within 15 units on average (the kite
  bot 0.2); a full ring without a way out was rare (2-4 % of the steps):
  the crowd did not wall it in, it walked into it and the hits came one
  after another (MUTEKI 30 frames: up to two a second). The user's "a few
  hits cost a lot" is this stream, not one large hit.
- **The phase 1 families.** Ogremon (the strongest walker), Kokatorimon (it
  never walks and spawns 20-25 units away, in the picture: a walking
  newcomer runs into it) and Goblimon brought most of it.
- **The ramp.** The damage-taken factor rose from x 0.4 at 0:00 to x 2.46 at
  5:00 (the warm-up times e^(0.18 x minutes)), made for the kite bot's
  levels (Lv 15 at 5:00); the newcomer was Lv 9 at 2:00 and Lv 11 at 3:00.
  Agumon's only heal in phase 1 is the level-up's 5 % until Heal Lt at Lv 15.
- **1:30.** The first surge (12 more for 20 s: 31 living) and the first
  Corrupted enemy came at 1:30, just before the deaths.
- **Fewer enemies alone starve the levels.** The first sweep (`s1`: A(t) 9
  at 0:00, at most 1.5 spawns a second) moved the newcomer's deaths to
  3:13-6:24, but the kite bot reached Lv 13 at 5:00 instead of 15, learned
  Heal Lt late and died at 6:07 instead of 8:18: the kills are the EXP.
- **5:00.** In phase 2 the kite bot died of shots (MRβ02's poison shots,
  Pharaohmon's magic, Tyrannomon: 0.7-1.0 MaxHP a minute of shots and
  0.2-0.5 of poison in s2's minutes 7-9) as the human bot of contact
  (Mummymon: 0.36 MaxHP a minute of phase 2 alone in s3; section 10.4).

### 10.4 The sweeps

Request keys on one build (`build/t/sweep.sh`, `QUICK=1`: the human and the
kite bot), 10 seeds a form and bot unless named; the median survival and the
range (A Agumon, V Veemon):

| Sweep | Change (on top of the row before, unless named) | human A | human V | kite A | kite V |
| --- | --- | --- | --- | --- | --- |
| b0 | main (20 seeds) | 2:09 (1:20-13:07) | 2:18 (1:16-12:11) | 8:18 (4:20-12:54) | 10:32 (8:39-11:52) |
| s1 | A(t) 9 / 22 / 40 / 56 / 64, at most 1.5 / 3 / 6 / 10 / 12 spawns a second, the way out until 10:00, the first surge 3:00, the first Corrupted 2:30, the damage warm-up x 0.2 to 8:00 (human 20 seeds) | 4:45 (3:13-6:24) | 5:23 (2:54-8:35) | 6:07 (5:51-7:20) | 6:09 (5:53-7:33) |
| s2 | s1 and the damage growth 0.13 (human 20 seeds) | 5:38 (3:42-8:08) | 5:55 (3:06-13:08) | 7:42 (6:11-16:13) | 7:13 (6:00-9:21) |
| s3 | s2 with A(t) 10 / 24 / 40 / 54 / 60, 3 / 5 / 8 / 12 / 12 a second, EXP x 1.35 | 17:10 (5:00-18:08) | 15:51 (12:08-18:19) | 17:29 (16:42-18:51) | 13:58 (10:54-17:44) |
| s4 | s3 and MUTEKI 45 (not taken) | 19:01 (17:57-20:01) | 17:48 (14:41-20:01) | 20:01 (9 of 10 clear) | 17:05 (13:22-19:04) |
| s5 | s3 and contact x 0.7 / 1.0 / 1.3 / 1.5 / 1.6, shots x 0.8 / 0.7 / 0.65 / 0.6 / 0.6 | 11:18 (5:42-18:13) | 14:18 (11:54-17:51) | 18:10 (17:20-19:59) | 14:14 (11:59-17:19) |
| s6 | s5 with the growth 0.16 (not taken) | 8:12 (5:14-13:52) | 12:18 (11:44-13:04) | 16:29 (12:48-17:08) | 12:15 (11:34-12:51) |
| s7 | s5 and Mummymon 0.16, Tortomon 0.13: the new defaults | 17:14 (5:47-17:33) | 16:39 (12:35-17:02) | 18:30 (17:22-19:08) | 17:27 (16:20-19:05) |

What the sweeps showed:

- Fewer and slower spawns, the way out and the soft warm-up end the deaths
  of the first three minutes (s1), but alone they cost levels: the kills are
  the EXP, and Agumon's Heal Lt (Lv 15) came late. EXP x 1.35 (s3) gives the
  old level pace back (the kite bot Lv 15 at 5:00, as before) with fewer
  enemies; it is the round's largest lever, with MUTEKI (s4: every
  frame more is a crowd's hit less; 45 makes the run easy, 30 stays).
- The phase 2 shots (MRβ02, Pharaohmon, Tyrannomon) killed the kite bot,
  contact (Mummymon) the human bot; the near / far factors (s5) make the
  crowd the threat a good player walks away from, and the shots softer.
- The forms differ in what hurts them: Agumon has no Impact, so the slow
  walkers that reach it (Mummymon: 0.45 MaxHP a minute of phase 2 for the
  human bot) decide its runs; Tortomon outlasts Veemon's Blade Arc, Bolt
  and Impact (0.55-0.65 MaxHP a minute of phase 3 for Veemon, 0.09-0.12 for
  Agumon). With both
  factors lower (s7) the medians are within 6 % of each other for both bots.
- The growth decides where the good runs end: 0.13 puts the kite bot at
  17-19 minutes, 0.16 at 12-17 (s6), and the human bot of s6 near 8:00.

The sweeps' last step (s8, on the s7 build with `art_range=14`): with the
softer shots and the smaller crowd `bot=stand` outlasted the kite bot (s7's
defaults: Agumon 18:08, 2 of 10 clear), its Art's 22 units standing
defeated every walker before it struck. The Art's reach standing is 14 now
(20 from 30 % of the stick, 34 at full stick): standing Agumon died at
2:45-4:16 again, the human and the kite bot kept their runs.

### 10.5 After (branch fix-o1a-balance, 0fcb0c9)

The same measurement as 10.2 (`build/t/sweep.sh EXE a2`, the defaults, no
request keys):

| Bot | Agumon: death times | Veemon |
| --- | --- | --- |
| human | median 16:32 (12:00-18:37); none before 3:00, 20 of 20 reach 5:00 and 10:00, 18 reach 15:00 | median 16:40 (12:37-18:19); none before 3:00, 20 reach 5:00 and 10:00, 15 reach 15:00 |
| kite | median 18:32 (17:22-20:01); 4 of 20 clear at 20:00 | median 16:52 (14:46-19:23); 19 of 20 between 15:00 and 20:00 |
| stand | median 3:28 (2:45-4:16) | median 9:54 (2:51-14:21) |
| circle | median 6:34 (6:11-7:08) | median 7:01 (6:22-7:25) |

Veemon / Agumon: human 1.01, kite 0.91. The human bot's minutes (Agumon;
before in 10.2):

| min | taken | contact | shot | poison | near15 | near30 | trapped | alive | level | runs |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 0.12 | 0.11 | 0.02 | 0.00 | 0.47 | 2.53 | 0.01 | 12 | 6 | 20 |
| 2 | 0.15 | 0.14 | 0.02 | 0.00 | 0.56 | 2.89 | 0.01 | 15 | 8 | 20 |
| 3 | 0.22 | 0.18 | 0.02 | 0.00 | 0.62 | 3.30 | 0.01 | 18 | 11 | 20 |
| 4 | 0.20 | 0.18 | 0.02 | 0.00 | 0.82 | 4.42 | 0.03 | 20 | 12 | 20 |
| 5 | 0.22 | 0.20 | 0.01 | 0.00 | 0.89 | 4.88 | 0.03 | 22 | 14 | 20 |
| 6 | 0.33 | 0.24 | 0.06 | 0.01 | 0.91 | 5.04 | 0.03 | 26 | 16 | 20 |
| 7 | 0.74 | 0.56 | 0.14 | 0.07 | 1.08 | 6.04 | 0.04 | 30 | 18 | 20 |
| 8 | 1.17 | 0.79 | 0.16 | 0.13 | 1.18 | 6.61 | 0.05 | 32 | 20 | 20 |
| 10 | 1.00 | 0.78 | 0.14 | 0.09 | 1.19 | 6.98 | 0.06 | 37 | 23 | 20 |
| 11 | 0.89 | 0.70 | 0.13 | 0.04 | 1.67 | 8.98 | 0.11 | 42 | 25 | 20 |
| 15 | 1.18 | 1.01 | 0.16 | 0.00 | 2.16 | 11.36 | 0.17 | 52 | 31 | 18 |
| 17 | 1.57 | 1.21 | 0.21 | 0.15 | 2.64 | 14.48 | 0.25 | 56 | 33 | 6 |

Veemon's human bot: 0.15 / 0.16 / 0.19 / 0.17 / 0.16 taken in phase 1, 0.56-0.66
in minutes 7-10, 1.1-1.2 from 13:00. The kite bot (Agumon): 0.13-0.17 in
phase 1 (shots), 0.65-0.92 in minutes 7-10 (shots and MRβ02's poison),
0.58-0.68 in phase 3, 1.2-1.5 from 17:00. The other targets of 14.2 over
the human and the kite bot's runs (`fix_targets.py` of the review round):
the first level-up at 6-11 s (median; at most 18 s), 12-15 level-ups in phase
1 (20-23 s apart), 3 level-up windows in the first minute and then at least
20 s apart, 1 Art hit per phase 1 defeat, the highest hit 11.1-16.4 % (its
poison included), no clamp, the largest poisoning 10.0 %, at most 0.62 % of
the frames without control, at least 92 % of every walking family close in,
skill-up panels at least 30 frames apart, no Lua error.

### 10.6 Tests, bounds and checks

| Check | Result |
| --- | --- |
| ctest | 55 / 55 (new: `overflow_gap`, `overflow_human`) |
| `overflow_gap` | six of eight sectors closed: 300 spawn points, none in the last way out |
| `overflow_human` | two runs of seed 7 (40 s): the same trail; walked 1,657 units, its Art defeated 27 |
| `overflow_balance` | the expected levels are the new kite medians (15 / 24 / 31 / 36 at 5:00 / 10:00 / 15:00 / 20:00; the review round's 15 / 25 / 32 / 40), the hits measured with the larger of the near and far factors of their time, the RAM check reads A(t) from `Tuning` (52 / 68 / 82 / 88 needed, 61 / 94 / 132 / 166 there); no wall: Agumon x 0.9 / 1.2 / 0.9, Veemon x 1.1 / 1.0 / 1.0 at the phases 2-4 |
| `overflow_spawn` | the mechanisms keep the review round's crowd (A(t) 14 + 3.2 a minute, no refill limit, no way out) explicitly |
| det.sh | 4004f056 / 396cb219 in two runs of two (`det_o1abal_1`, `_2`) |
| drawn run | `build/t/runshots.sh`: `bot=human` seed 1 in a hidden window, pictures at 1:00, 3:00 and 6:00; its minute lines 1-6 equal the headless run's |
| SURVIVAL.md 14.2 | the kite bot 15:00-20:00 (was 7:00-14:00), the human bot's targets, Agumon against Veemon within about 20 % |

### 10.7 Open

- **Phase 4's entry.** Most runs end in phase 4's first three minutes, 13-16
  of 20 deaths within two minutes for each bot and form (the review round
  accepted 12; human Agumon 15:20-17:27, kite Veemon 16:04-17:54): the
  phase 4 families come in by 17:00 (weight over 90 s, damage over 120 s)
  and MRε05 on the 20-25 ring brings 0.26-0.34 MaxHP a minute alone. Next:
  phase 4's intro over 180-240 s, MRε05 on the slow walkers' ring or a lower
  factor, then the growth again so the kite bot stays at 17-20 minutes.
- **Skill after 10:00.** The human bot lasts nearly as long as the kite bot
  (Agumon 16:32 against 18:32): from phase 3 on the time decides more than
  the play. A higher contact factor late, or the offers mattering more (O1b's
  gear), would widen it; the newcomer's targets of this round hold either way.
- **Standing Veemon** lasts 9:54 (Impact strikes the walkers round it),
  standing Agumon 3:28; the circle bot (it leaves its kills behind, Lv 9-11
  at 5:00) 6:34 / 7:01.
- **Kokatorimon** (never walks, enters 20-25 units away in the picture) is
  still the second source of the newcomer's phase 1 contact.
- **The user's next run** decides: the human bot is a model of his first
  two minutes, not of him.

### 10.8 The independent check (05.11, other seeds)

A second build of the branch (`build/chk`, 2440389 plus a comment) and
the same measurement on seeds 101-120 (`bot=human` and `bot=kite`, Agumon
and Veemon, 20 seeds each, headless; `build/chk_t/report_m1.txt`). Seed 1
of `bot=human` replays the round's run bit for bit (run hash
dc71c7f11dc9ed55, the same 59707 steps), and a drawn hidden run of seed 109
ends on the same step as the headless one.

| Bot | Agumon | Veemon |
| --- | --- | --- |
| human | median 16:04 (4:40-17:35); none before 3:00, 19 of 20 reach 5:00, 15 reach 10:00 | median 16:09 (12:09-17:44); 20 reach 10:00 |
| kite | median 18:13 (16:58-20:01), 1 clears | median 16:49 (13:18-18:59), 18 of 20 within 15:00-20:00 |

Veemon / Agumon: human 1.00, kite 0.92. The round's targets hold (no
defeat before 3:00, the human median above 8:00, the kite bot within
15:00-20:00 in at least 70 %, the forms within 20 %), but the Agumon
human runs have a tail the seeds 1-20 did not show: 4:40, 5:32, 5:39,
7:49, 9:24 (one defeat before 5:00, five before 10:00).

- **The tail is the offers, not the crowd.** The human bot takes the
  offers as a newcomer (the first line or a random one). Over the 40
  default Agumon runs (seeds 1-20 and 101-120): when its first Forge (Van
  Blade) came among its first three picks (28 runs, at about 0:25) no run
  ended before 12:00; when it came later (12 runs, from pick 4, about
  1:10-3:20) five of 12 ended before 10:00 (4:40-9:24). Veemon does not
  care (its 7 late-Forge runs: 12:09-17:21). The dying runs take 0.2-0.35
  MaxHP a minute of contact in minutes 2-5 against the median's 0.16-0.24,
  as a steady stream (about 40 hits a minute of about 1 % each), with no
  heal before Lv 15 or a Train Heal; seed 109's picture at 4:30 (the
  second surge, 30 living) shows the user's "no gap" with 39 HP left.
- **The damage knobs do not reach it.** Contact x 0.85 at 5:00 (instead of
  1.0) or the damage warm-up to 10:00 (instead of 8:00) moved the same
  seeds by 10-60 s (109: 4:40 / 4:58 / 5:19; 112: 5:32 / 5:47 / 5:53) and
  left the medians alone (16:34, 16:15). So a newcomer Agumon who does not
  take a Forge in the first minute meets 5:00 at the edge. Next (a design
  question, not a number): Agumon's start Art nearer Van Blade, or the
  first Forge offered surely (the first line of an early offer), or the
  offer text saying what a Forge does.
- **The human bot.** It moves 98-99 % of the steps (64-68 % at full
  stick) and turns the stick smoothly; it stands still about 1 % of the
  time, a newcomer who stops to look stands more and has the Art's reach
  of 14 then. It reads the enemies' old positions against its own present
  one (it flees from where they were relative to where it is). Its offers
  are blind, a reading player takes a Forge sooner. Before the round it
  died where the user died (2:09 against 2:00); after it, it is a lower
  bound for the offers and a fair model of the first minutes' movement.
- **Checks.** ctest 55 / 55 in `build/chk`; det.sh `o1abal_chk`: 4004f056
  / 396cb219 (both runs).

## 11. The forge round of 05.11 (the early Forge and phase 4)

Two things 10.7 and 10.8 left open: Agumon's early deaths depend on when
the newcomer takes its first Forge, and phase 4 is a wall (13-16 of 20
runs of every bot and form end within two minutes, mostly 15:30-18:00;
the kite bot lasts hardly longer than the human bot after 10:00). The
targets: the early Forge reliable without making it automatic; `bot=human`
median 14-18 minutes, `bot=kite` median 20-24 minutes with at least 6 of 20
reaching 22:00 (17.1: a run that goes well lasts 22-25 minutes), no
two-minute window with more than 9 of 20 deaths of one bot and form, the
kite bot at least 3 minutes above the human bot; phase 1 as it is (with
`bot=human` no defeat before 3:00, at most 1 of 40 Agumon runs, seeds 1-20
and 101-120, before 10:00, the first level-up within 18 s, the forms within
about 15 %). Branch `fix-o1a-forge` from main da44ddd; this section is the
round's progress note. The runs: `build/f/pair.sh` (`runs.sh` with nice 19
and at most two at a time), `build/f/targets.py` (this round's targets),
`build/f/minutes.py` (the minute medians with the kills by the Art and by
the techniques, new in the minute lines).

### 11.1 Why the early Forge decides Agumon's first minutes

The measurement: `bot=human` with `forge_from=300` (the newcomer's policy,
but it takes no Forge before 5:00), seeds 1-20, the build of 651bbb5
(`build/f/runs_e1`):

| Form | Death times | Before 10:00 |
| --- | --- | --- |
| Agumon | median 4:51 (3:55-7:55) | 20 of 20 |
| Veemon | median 13:12 (5:25-16:33) | 3 of 20 |

The minutes (medians over the runs alive; kills of the minute by the Art
and by the techniques):

| min | Agumon Art kills | Agumon technique kills | Agumon contact | Veemon Art kills | Veemon technique kills | Veemon contact |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 41 | 8 | 0.14 | 30 | 15 | 0.17 |
| 2 | 54 | 16 | 0.18 | 50 | 28 | 0.21 |
| 3 | 55 | 42 | 0.24 | 38 | 94 | 0.16 |
| 4 | 58 | 60 | 0.35 | 43 | 160 | 0.20 |
| 5 | 61 | 80 | 0.41 | 36 | 221 | 0.18 |

- **The start Art is not weaker.** Cleaver (x 2.0) defeats as many as
  Blade Arc (x 1.4) or more in every minute; changing it would not move
  the tail.
- **The gap is the crowd technique.** Both forms learn their TECH4 at run
  level 8 with Blast 331 (about 2:30-3:00): Veemon's Impact strikes every
  enemy within 20 units every 2 s (25 casts a minute, 160-220 kills a
  minute by 4:00), Agumon's Cutter is one shell at one target (13 casts a
  minute). Agumon's answer to a crowd is the Forge: its second strike from
  Forge 2, the side lines from Forge 4 (6.3, the class factor's comment in
  `abilities.cpp`). Without it the crowd round Agumon grows (near15 1.3 at
  5:00 against Veemon's 0.7) and the contact with it.
- **So the Forge has to come.** Over the balance round's 40 default Agumon
  runs the newcomer took its first Forge after the third offer in 12, and
  5 of them ended before 10:00. The human bot takes the first line half the
  time and a random one otherwise; a Forge that is merely among the lines
  of the first three offers changes little (a simulation of the draws:
  31 % of the newcomers without a Forge in their first three picks, 28 %
  with a Forge line in each), a Forge on the first line does (4 %).

The answer (`offers.cpp`, `kForgeSure`): a form's first Forge takes the
first line of each of its first three level-up offers until it is taken;
the cursor starts on it and the line under the window says what a Forge
does ("A Forge makes Cleaver stronger at once and lets its weapon grow into
Van Blade."). It stays a choice: the other two lines are drawn by weight
as before, and after the third offer the Forge is drawn by weight again.
Whether a form has a Forge, and how many, is the weapon table's: the
families of the Art's class (`OfferState::forge_families`); a class with
one family gets no Forge line at all. Veemon gets the same rule (its
late-Forge runs did not die early, but nothing in the data makes it a
special case either way).
