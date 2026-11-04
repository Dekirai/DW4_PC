# Data Overflow, round O1a lane B: the run engine's hooks

Lane B of round O1a (docs/SURVIVAL.md 15.1): the rules of a survival run's
engine (`OverflowRules`), the engine hooks H2, H3, H4, H6, H8, H9 and H10 of
SURVIVAL.md 13.4 (with the H5 notifications the director needs), T10, and
the six O1a spikes. Base: main 75c2436. Everything here is English program
text; nothing of it is visible in the game without a run engine.

## 1. What the original keeps

Every hook acts only in an engine that has `OverflowRules` (null in every
story, test, replay and online engine). With the pointer null each hook is
one untaken branch: det.sh stays 4004f056 / 396cb219, the r25c campaign
reference 10/10 segments with 1,748,011 frames, st_all 17/17 (section 5).
The rules are in no snapshot and not in the fingerprint; the snapshot
version stays 43. No script, data file or save format changes.

## 2. The interfaces lane C builds on

All in `include/dw4/engine/overflow.hpp` (namespace `dw4::engine::overflow`)
unless named otherwise.

| Interface | What it is |
| --- | --- |
| `Engine::set_overflow_rules(std::shared_ptr<OverflowRules>)` | the host gives the run engine its rules **before** the first `request_stage(arena)`; `Engine::overflow()` / `overflow_run()` read them |
| `OverflowRules` | the hooks' values (`damage`, `levels`, `skills`, `player_hit`, `pickup_radius`, `walk_factor`, `hud_delay`, `donors`), the enemy classes (`set_enemy_class`), the exits (`field_change`, `ended`, `reset_end`), the tallies for the run log (`tally`: steps, steps without control, hits taken, misses, clamps, highest hit share, damage by source, panels), `last_taken` |
| `Director` (abstract, lane C's `OverflowDirector`) | `step` (H9), `field_started` (after the native field start), `run_end(RunEnd)` (H3, once), `revive` (Raise at 0 HP), `skill_changed` (the ladder re-equips here), `enemy_defeated`, `level_up`, `item_taken`, `player_hit(HitTaken)` (H5). `OverflowRules::director` points at it (not owned) |
| `DonorRequest` | `families` (the randomizer's donor table), `guardians` ({family, stage or separate package}), `streams` ({stage, sound names}); read at every stage load |
| `ModelCtrlManager::port_hitbox(ram, Hitbox)` | an Art's box: `kind` 10000, `delay` and `end` in frames, `offset`, `size`, `tag` (> 0, the director's numbering), `aimed`, `yaw` (world, `atan2(dx, dz)` of the bearing), `shift` (units along it), `max_targets` (0: any number). It checks every frame from its delay to its end, strikes each target once (a sequence's box hits again whenever the target's reaction allows; `repeat` keeps that, for a body box) and at most `max_targets` targets (the Art's "pierces"). It survives sequence starts, a GENERATE drops it. The hits carry the tag (`ModelScriptContext::port_tag`); `OverflowRules::damage.tag_factor[tag]` and `tally.dealt_by[tag]`. Agumon's Cleaver is several boxes (SURVIVAL.md 6.3): one call per box, the same tag |
| `ModelCtrlManager::set_muteki(engine, ram, frames)` | invulnerable frames (the director's Raise: 120) |
| `EnemyClass` + `set_enemy_class(family, index, class)` | the director marks Corrupted enemies and Guardians when it generates them; the damage-taken rule and the player hit rule read it |
| `digivolve(engine, slot, species)` | H8 / 4.1 (O3): level, EXP, skills and UPCNT into the new form's BASE row, BASE2CURRENT, RECALCPLAYER, HP / MP fractions |
| `on_screen(engine, point, widen)` | whether a point is in the box camera's picture (4:3: 1, 16:9: 4/3), for the spawner (S3) |
| `technique_mp_cost(engine, row)` | the run's MP: the heal group pcTech MP / 8, everything else 0 |
| `run_next_exp`, `run_technique_level`, `run_level_hp` | the pure rule functions (unit tested) |
| `Engine::host_call(method, args)` | a `be:` call as a script makes it (the native handlers use it) |
| `RuleSet::original` | T10 only: the native player hit script exactly as the script, every other hook inert |

The director sends its own messages in a `Messenger::PortOnly` scope, as
SURVIVAL.md 13.5 says. Spawning is the research's recipe: `MODELCTRL
<family> -1 FIND_SPACE`, then `MODELCTRL <family> <ram> GENERATE` with
{position, rotation}, {"SQ_ENTR"}, {record, max HP}. Techniques: `ACTION_BROADCAST
<action>` with strings {"p_<form>"}, ints {slot} (the cast sound is the
director's).

**Where the host hands them over** (lane A, docs/OVERFLOW_HOST.md 2 and 7):
`RunDriver::attach(run, request)` runs after `prepare_run_engine` and before
the host's `request_stage(field)`; that is where the rules go in
(`Engine::set_overflow_rules`) and `OverflowRules::director` is set. The host
destroys the run's engine before the driver, so a director the driver owns
outlives every hook call. Until lane C, the host's stand-in driver gives the
engine `OverflowRules` with this lane's initial values and no director (no
spawns, no revive) and ends the run on `OverflowRules::ended()`.

## 3. The hooks as built

| Hook | Where | In a run engine |
| --- | --- | --- |
| H2 donors | `Engine` loader state 3 (`load_overflow_donors`, `src/engine/overflow/donors.cpp`); `SoundManager::add_named_sounds` | the request's families from their donor stages (all 21 on any arena, no fixed-stage lock, no six-family pool), Guardians from a stage or a separate package, never a `ded_*.lua`; their SE banks; named stream entries of other stages |
| H3 exits | `Engine::request_stage`, `soft_reset_allowed`, `set_reset_enabled`; `end_g_000bcaps.lua` (H10) | the host's first request and `OverflowRules::field_change` pass; every other stage change (gates, the return gate matz01, a gate disc) is refused and becomes `RunEnd{stage_exit, target}`; no soft reset, no console reset; the game over script becomes `RunEnd{game_over}`. The director hears of the end once |
| H4 rule values | `lua_host.cpp` BLTisHitCheck, BLTgetDamageValue, BLPgetDamageValue; `combat.cpp` skill_up, recalc_player (NEXTEXP), experience_up, apply_level_ups; `items.cpp` update_item; `model_ctrl.cpp` MOVE | Arts always hit; dealt: Art factor x tag factor, technique family factors, element factors; taken: time x tier x Corrupted, the 35 % clamp for non-Guardians (logged, counted); the run curve; the tier's EXP factor; the level-up rule (fractions, +3 % HP, the meters show what is left); the skill rule (Art hit 1 + 1 on a crit, x GROW / 100, kept as a fraction; blast hit 2; heal / force cast 12; the run's caps; RECALCPLAYER and the CURRENT2BASE store; an SQ_TECUP panel only for a new technique, rank or weapon, at most one in 30 frames); the instant pickup reach; the walk speed factor |
| H5 notifications | next to the port events | `enemy_defeated`, `level_up`, `item_taken`, `player_hit` (and `run_end`) |
| H6 port hitbox | `ModelRam::port_hitboxes`, `update_port_hitboxes` after the model's own subcommands | see section 2 |
| H8 run player | `recalc_player` | TECHn_LEVEL x 2/3 rounded up; `digivolve`. A core's LVPOINT is checked only by the equipment window and the shops (subwnd*.cpp); the run's level is the player's DW4 level, so the director's gear offers read it directly |
| H9 director step | the end of `Engine::step_impl` | the run's tallies, then `Director::step` |
| H10 native scripts | `Engine::run_script` asks `OverflowRules::script` first | `hit_p_<form>.lua` (src/engine/overflow/player_hit.cpp), the Violet Ring's `first.lua` (field_start.cpp), `end_g_000bcaps.lua` |

**The player hit rule (4.8).** Every stage but the quests dngq3* / dngq4*
and stag0001, stag0103, stag0203, stag0303 (their copies lack the checks of
the player-only bullets 70126-70133) has one and the same
`hit_p_<form>.lua` for all 16 forms (1,152 of the 1,344 copies; the
Violet Ring, staggf01 / 04 and every dng* field among them). The native
handler is that script, every call through `Engine::host_call`, numbers
written as the Lua VM writes them. In a run: a normal or Corrupted enemy's
hit takes the HP and shows the number, the HP meter and the panel's shake,
the element's hit spark (`SDMG<F/I/T/D>1`'s own starter) and PC_DMG01, but
no damage sequence and no turn towards the attacker; stun, sleep and bind
last at most 30 frames; 50 frames of MUTEKI follow every damaging hit. A
Guardian's hit keeps the original reaction. A miss shows the guard effect
(`GURD<D/S/A/G>2`'s starter) and EFX_GUARD without SQ_GUARDR. A guarded hit
(classic buttons) passes at 50 % without a reaction. At 0 HP the director
may revive; otherwise `RunEnd{player_defeated}` and the original knock-down.
Every hit reaches `Director::player_hit` with the raw and clamped damage.

**The native field start.** The Violet Ring's first.lua line for line
without the boss lines (the table in field_start.cpp). One line more than
SURVIVAL.md 13.4 lists belongs to the intro: `LAYERCTL SWITCH WN00/CLR` is
posted 10 frames ahead (a demo record's delay posts and goes on), so
without the intro's waits it came after `WN00/SET` and `ALL/DISPON` and hid
the HUD for good. T10 found it. The HUD opens after `hud_delay` (30) frames
instead of the intro's 520.

**Not in lane B.** H0 (the talk port event) and the NPC are O1b; H7 is not
needed (S3); the Drain Ram's cap of 2 % of MaxHP a second (O2) would sit in
BLTgetDamageValue next to the drains.

## 4. T10

`tests/overflow_scripts_tests.cpp` (ctest `overflow_scripts`, with the game
data; `overflow_rules` is the asset-free part). Two engines one after the
other (the system generator is process-wide), the same spawns and pads.

| Part | Scenario | Result |
| --- | --- | --- |
| (a) player hit | dng101b1, Agumon +15 levels, 10 enemies at r 14, 3,600 steps | 23 hits (SQ_SDMG, SQ_MDMG, SQ_STUN, SQ_GUARDR); the net fingerprint equal on all 3,600 steps |
| (a) player hit | staggf01, Veemon level 1, 10 Goblimon at r 14, 3,000 steps | 10 hits to the defeat (SQ_SDMG, SQ_MDMG, SQ_STUN, SQ_DDMG, SQ_ERAS); equal on all 3,000 steps |
| (b) field start | stagBS02, 700 steps after the start | the 19 layers, the stage switch, the 21 HUD panel RAMs, the fade, the box camera and the pad input equal; Belphemon and its bar only after first.lua |
| run hooks | stagBS02, Agumon, donors Goblimon + Numemon and Death Valley's ST_DEATH, 9 + 5 enemies at r 16, an aimed Art (one 8-unit box) every 75 frames, damage taken x 0.5 | 20 donor RAMs, ST_DEATH plays; 25 Arts, 871 Art damage, 11 defeats; Blunt skill 1 -> 58; level 2 at 80 EXP (next 216), HP 27 / 192 after it (no refill); 23 enemy hits, no damage sequence, MUTEKI 50 after a hit, 0 of 2,745 steps without control; a stage change refused and reported; no soft or console reset; `on_screen` and the heal MP |

## 5. Regression

With the lane's last engine commit (c0d864c), Release, hidden runs:

| Test | Result |
| --- | --- |
| T1 det.sh | 4004f056 / 396cb219 in both runs (also after each earlier engine commit) |
| T2 r25c | 10/10 segments, 1,748,011 frames, every segment's frames and story equal to the reference (also with 1493115) |
| st_all | 17/17 "identical after loading" (also with 1493115) |
| ctest | 46 / 46 (two new: overflow_rules, overflow_scripts) |

T2's second run (the NPC drawn, `DW4_OVERFLOW=show`) belongs to O1b: the
NPC and H0 are not in lane B.

## 6. Spike notes

All with `dw4_overflow_probe` (tools/overflow_probe.cpp), headless, a run
engine on the Violet Ring with fast loading and god mode, Agumon.

### S1 Enemy approach and speed per family (4.3)

Four of each family at 80 and at 50 units around a standing player, 480
steps (8 s). "Close" = within 30 units in the 8 s; speed = the distance
closed in the first 4 s per frame.

| Family | RAMs | 80: close | 80: speed | 50: close | 50: speed | Note |
| --- | --- | --- | --- | --- | --- | --- |
| e_nume | 10 | 1/4 | 0.087 | 0/4 | 0.034 | ranged (SQ_FIRE), keeps away |
| e_goburi | 10 | 4/4 | 0.131 | 4/4 | 0.144 | |
| e_ogre | 9 | 4/4 | 0.185 | 4/4 | 0.137 | |
| e_scum | 10 | 1/4 | 0.132 | 0/4 | 0.040 | ranged (SQ_FIRE), keeps away |
| e_cockatri | 12 | 0/4 | -0.016 | 0/4 | -0.002 | does not walk at all |
| e_mecha1 | 10 | 1/4 | -0.017 | 4/4 | 0.036 | idles far, comes near |
| e_mummy | 12 | 0/4 | 0.080 | 4/4 | 0.076 | slow |
| e_minotaru | 4 | 1/4 | 0.077 | 3/4 | 0.087 | |
| e_pharaoh | 3 | 0/3 | 0.053 | 3/3 | 0.046 | warps (SQ_WARP) |
| e_mecha2 | 10 | 4/4 | 0.147 | 4/4 | 0.176 | |
| e_tyrano | 4 | 2/4 | 0.067 | 4/4 | 0.102 | |
| e_otama | 10 | 4/4 | 0.193 | 4/4 | 0.145 | |
| e_geko | 10 | 4/4 | 0.162 | 4/4 | 0.165 | |
| e_mecha3 | 8 | 4/4 | 0.191 | 4/4 | 0.174 | |
| e_maridevi | 6 | 3/4 | 0.081 | 3/4 | 0.016 | magic at range |
| e_torta | 4 | 1/4 | 0.090 | 3/4 | 0.034 | |
| e_mecha4 | 6 | 4/4 | 0.076 | 4/4 | 0.088 | |
| e_infermo | 12 | 3/4 | 0.135 | 4/4 | 0.004 | jumps |
| e_megadra | 4 | 0/4 | 0.033 | 4/4 | 0.057 | |
| e_rare | 8 | 3/4 | 0.047 | 4/4 | 0.054 | |
| e_mecha5 | 4 | 2/4 | 0.090 | 3/4 | 0.047 | |

Every family closes in 3 to 40 times slower than the player walks (0.64 measured in
the probe's free walk, 0.70 in the design), so no family catches a moving
player; the pressure has to come from where they appear and how many. From
80 units only 7 families close in; from 50, 14 of 21 do fully and 4 at
75 %. For lane C: spawn on the 40-60 ring by default, not 70-95; recycle an
enemy that has not come closer for 6 s (4.3); Kokatorimon never walks
(spawn it within 25 units or leave it out of the pool); Numemon, Sukamon
and MarineDevimon fight from range and keep 30+ units away, so the 14.2
target "within 30 units in 8 s" has to leave the ranged families out (or
measure them by their shots).

### S2 Body push and encirclement (N1)

A free walk: 57.1 units in 90 frames. Through a ring of 12 enemies at 9
units: 56.5 units in the same 90 frames; no enemy showed SQ_PUSHED (the
walk has no PCBA body box), none moved more than 5.5 units, one hit. An
encirclement of 16 at 45 units: after 3 s the nearest stood at 21, the mean
at 37; walking out passed 60 units after 94 frames with one hit.

The player's PCBA body box (kind 50000, 15 units, SQ_PUSHED on the enemy,
hit_e_*.lua lines 3-5) exists only in SQ_ATCK3 (the Slam), never in the
walk. As a repeating port hitbox around the walking player (probe push (d),
`Hitbox::repeat`) it shoves the crowd (an enemy moved up to 13 units
instead of 5.5) and slows the player a little (50.7 units instead of 56.5),
but does not stop it.

So crowds do not clump by the player's pushes (16's risk does not occur),
and neither crowds nor encirclements block the player: models collide only
with the stage (ModelCtrlManager::collide), the player walks through them.
An encirclement threatens only by its attacks. If the user finds that
toothless, the fix is a run-only body block (the player slides along enemy
bodies in its collide step), an O1b spike; encirclements of ranged
families (S1) also help, and a PCBA port box can keep a crowd apart.

### S3 The camera (H7)

The Violet Ring's box camera stands 122 units behind and 82 above Agumon.
The ground in the picture, distance from the player (rows bottom to top):

| | left edge | | centre | | right edge |
| --- | --- | --- | --- | --- | --- |
| bottom edge | 69 | 49 | 40 | 49 | 69 |
| | 72 | 37 | 6 | 37 | 72 |
| middle | 114 | 74 | 54 | 74 | 114 |
| | 250 | 206 | 189 | 206 | 250 |
| top edge | 902 | 817 | 787 | 817 | 902 |

(4:3; 16:9 reaches 96 at the sides at the player's depth, 85 at the bottom
corners.) The view is wide enough: ahead it reaches far past the arena's
wall (r 115), so a 70-95 ring is out of the picture only behind the player
and partly to the sides. H7 (a distance factor) is not built. For lane C:
test spawn points with `overflow::on_screen` and prefer the hidden ones
(behind, sides); the visible ones enter with SQ_ENTR, as DW4's own waves do.

### S4 The jump's MUTEKI

SQ_JUMP of all 16 forms has no MUTEKI subcommand; the jump attack
SQ_ATCKTM has MUTEKI 18 (in each of its four labels). At run time Agumon's
X jump had no frame of MUTEKI. So the classic X jump needs no cooldown
(10, question 4); the jump attack stays out (6.3).

### S5 The weapon model change in hand (5.3)

At run time: Battle Hawk α (weapon 0, w_axe01) into DEVICEFOLDER and
POWERSOCKET[0], RECALCPLAYER (TSUYOSA 70 + 78 = 148),
`Engine::load_common_now("_common/w_axe01.igb")` (0.2 ms), `PLAYERARMS
w_axe01 0 GENERATE` in a PortOnly scope: the three weapon parts under the
player's actors are replaced (w_common05's polySurfaceShape1 by w_axe01's
polySurfaceShape7) and the sequence labels come from the new weapon. The
model in hand changes at once; the fallback (the model stays until the next
stage load) is not needed. That is the ladder's recipe for lane C.

### S6 All 21 families on one arena

| Donors | Load (fast loading) | Working set | Step + traverse, every RAM alive |
| --- | --- | --- | --- |
| none | 16 frames, 0.05 s | 34 MB | 0.04 ms |
| all 21 (166 RAMs) | 16 frames, 0.75 s | 250 MB | 2.2 ms (166 alive) |

Within 12's budget (600 MB, run start 3 s). The 2.2 ms are with all 166
alive; the run keeps 60 (72 in a surge).

## 7. Reproduce

```
build/<dir>/dw4_overflow_rules_tests
build/<dir>/dw4_overflow_scripts_tests "<game data>"
build/<dir>/dw4_overflow_probe "<game data>" approach 80 480 | push | camera | jump | weapon | memory all
```
