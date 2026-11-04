# Data Overflow (survival mode)

Design of the survival mode the user asked for on 1 November 2026: a new NPC
next to the lobby's gate to the outer world offers a mode in the
survivors-like ("bullet heaven") genre, with different Digimon, abilities,
unlockable weapons and passives, single player and offline for now. This is
round 66, design only: nothing here is built yet. Base: main 870a416. Line
references are to that commit.

The mode is our own design in that genre. It copies nothing from Vampire
Survivors: no characters, weapon or item names, numbers, rules or art. It is
made from Digimon World 4's own content: its 16 player forms, its weapon
classes and techniques, its plugs, chips and boards, its enemy families and
mid-bosses, its stages, music, sound effects, windows and font.

Sources (none of them committed; the research tools link read-only against
the build's `dw4_engine.lib` / `dw4_core.lib`):

| Source | Where | What it gave |
| --- | --- | --- |
| engine map (round 66) | scratchpad `surv_engmap/` (`probe.cpp`, `info_*.txt`, `map_*.png`, `load_*.txt`, `donor_*.txt`, `fire_*.txt`, `gate_view.png`) | lobby NPCs and the talk chain, the gate's place, arena collision maps, direct spawning, RAM counts, donor loading, auto-fire by broadcast, CPU per enemy, isolation facts |
| content map (round 66) | scratchpad `surv/` (`db/*.tsv`: shinka, weapon, pcTech, abChip, armor1/2, insItem, conItem, enemy, eneatk; `allsnd.txt`, `gens.txt`, `ag_atck.txt`, `ag_waza.txt`) | forms, weapons, techniques, passives, enemies, bosses, pickups, sounds, hitbox patterns, formulas |
| round 64 maps | workflow journal `wf_57807143-c0e` | record layout, rules, gear tables, the face sheet's cell order |

ROADMAP gate 4 keeps new content (maps, Digimon, weapons) out of scope. This
mode is the user's explicit exception, and it adds rules and screens only:
every model, effect, sound and text style it shows is the game's own.

Numbers are measured (Ryzen 7 7800X3D, RTX 5070 Ti, hidden runs, other rounds
running in parallel) unless marked as an estimate or as an initial value.
Initial values are starting points for the balance passes (15.4), not
promises. The questions for the user are in section 17.

## 0. Summary

- **Name and fiction.** *Data Overflow*: the Digital Security Guard's battle
  simulator. It copies enemy data from the servers and lets it pour in until
  the simulator overflows. Nothing earned inside leaves it except records and
  *Sim Bits*, the simulator's own currency. That is also the in-game reason
  why the mode never touches the story.
- **The NPC.** A Gekomon, the *Simulator Keeper*, stands at (50, 10, -80) in
  stagML11, east of the Outside World Gate (g_cytelep1). It appears once the
  gate is open, only offline. With two or more local players it only says
  that the simulator takes one Digimon at a time. It is drawn by the host
  like the round 60 ghost figures, so it is never part of the simulation
  (3).
- **A run.** One Digimon on one *Sim Field* (first: the Violet Ring,
  stagBS02). Waves of DW4 enemies arrive area by area: Death Valley, Dry
  Land, Venom Jungle and Machine Pit families, five minutes each, each phase
  with its own field music. A *Guardian* (a DW4 mid-boss) closes every phase,
  and a finale Guardian comes at 20:00. If it is not defeated within two
  minutes, the *OVERFLOW* starts, and the flood grows until the end (4).
- **Growth.** Defeats give EXP at once, as in DW4. Every run level opens a
  choice of three: a new ability, a higher level of one or a passive, with
  *Rescan*, *Delete* and *Skip*. Abilities are *Arts* (the ten weapon
  classes as automatic attacks) and *Techniques* (DW4's 19 techniques cast
  automatically, ranks Petty to ∞ as levels 1-5). Passives are *Plugs* and
  *Chips*. At level 5 with a partner passive and an ¶ X Data, an Art
  *awakens* into its class's top weapon, or into the form's own signature
  weapon. A technique *overflows* into a bigger version (6).
- **Digimon.** All 16 forms. Each starts with its class's Art and its first
  attack technique, and has its own stats and growth (shinka), technique
  affinities and element. The four Rookies are open from the start. The
  others unlock through *Data Records*, the mode's milestones (5, 8).
- **Meta progression.** Sim Bits buy *Calibration* (permanent run bonuses).
  Data Records unlock forms, abilities, passives, fields and the Hard / Very
  Hard simulations. Everything is kept in `saves/overflow.dat`, never in the
  story save (8).
- **Look and sound.** English UI in the game's own windows, font, glyphs and
  colours: the talk window for the NPC, the status window for the
  character select, the quick panel list for the level-up choice and the
  Game Over / clear presentation for the results. DW4's own music and sound
  effects (9, 11).
- **How it runs.** A second, isolated `Engine` for the run, offline only.
  The story engine is not stepped while a run lasts, like the F1 pause, and
  afterwards it continues bit for bit. The process-wide system random
  generator is saved before and restored after the run. A director in the
  run's engine spawns, fires and scales through the engine's own messenger
  commands. A short list of engine hooks is active only in that engine; the
  story engine gets nothing but one observe-only port event for the talk
  (13).
- **Bit-identical original.** Without the mode, det.sh (4004f056 /
  396cb219) and the r25c campaign reference (1,748,011 frames) do not change.
  The NPC is hidden in test runs unless a test asks for it (14).
- **Rounds.** O1 brings a playable first version: four Rookies, one field,
  Normal, a core set of abilities, four Guardians, the meta file and the
  tests. O2-O4 add content, Digimon, unlocks, fields, tiers and balance (15).

## 1. Ground rules

1. **The original stays the original.** With the mode never used, every
   simulated frame is the same as today: det.sh 4004f056 / 396cb219, r25c,
   lobby replays, save states, the Dolphin frame comparisons and online
   sessions. Every engine hook is inert in the story engine (13.4).
2. **The story save is never touched.** No write to `savedata.dat`, the
   `.bak`, `randomizer.ini`, `profile.dat` or a memory card folder. The run
   has its own player built from the shinka table. Reading story flags to
   decide whether the NPC is there is allowed.
3. **The story's determinism is never touched.** During the NPC dialog the
   story engine steps with neutral pads. That is an ordinary input sequence
   and goes into the pad log. During a run it does not step at all. No
   message, demo, model or random draw of the mode ever reaches it.
4. **Single player, offline.** No NPC in online rooms (OnlineRun), direct
   LAN sessions (Netplay) or with online rules on. With 2-4 local players
   the NPC explains that the simulator takes one Digimon. Online play stays
   impossible while a run lives (`Engine::set_online` throws with two
   engines anyway).
5. **English, in the game's style.** All texts are English and use only the
   game font's glyphs. Screens use the game's windows, face icons, item
   glyphs and rarity colours (9).
6. **Our own design.** Names, rules and numbers are ours, built on DW4's
   data. When something resembles the genre (choices on level-up, timed
   runs), it is the genre's common ground, not another game's content.

## 2. The mode in one page

The player talks to Gekomon in the lobby, picks a Digimon, a Sim Field and a
simulation level, and enters. The screen fades, the run's engine loads the
field (fast loading, about a second, estimate) and the Digimon stands in the
arena with its first Art and technique. Enemies come in from outside the
camera's view. The Digimon's abilities fire on their own; the player walks,
dodges, picks up items and chooses upgrades. Defeated enemies give EXP at
once, as in the story. Every run level brings the level-up window: three
offers, take one. Guardians arrive at 5:00, 10:00 and 15:00; at 20:00 the
finale Guardian arrives. Defeating it clears the simulation. Being defeated
ends the run with the Game Over picture. The results list time, level,
defeats, damage per ability, Sim Bits and new Data Records. Back in the lobby
the player stands where they talked to Gekomon, the story's music goes on,
and Gekomon comments on the run. Sim Bits buy Calibration, and Data Records
open new Digimon, abilities, fields and harder simulations.

A run lasts about 20-22 minutes (question 5). The Retire choice in the pause
menu ends a run at any time and keeps what was earned.

## 3. The NPC

### 3.1 Who and where

- **Model.** n_gekoa ("Banpei Gekomon", the sentry Gekomon of the Venom
  Jungle field stag0302, with its face icon n_gekoaic). The talk tab shows
  "Gekomon". The original uses it only in stag0302, so it is new in the
  lobby, and it is not a story figure (question 1 has the alternatives).
- **Place.** (50, 10, -80), yaw 0 (the model's Z axis towards +z, facing
  the lobby centre where the player comes from). That is on the main floor
  (y = 10), 45 units east of the centre of the Outside World Gate's pad
  (g_cytelep1 at (5, 27, -95), ring r about 20), outside its trigger mat mate00 (-5..15,
  -105..-85) and its info mat matf00 (-30..40, -130..-60), west of the Heal
  Circle (matf02 / mate03 from x 80). The walking Digi-Elf #2 patrols at
  z -35 / -10 (aim_n_digilff), about 45 units north. Leomon stands at
  (85, 10, -15). No original NPC, mat or route comes within 20 units. The
  O1 lane D checks the place with a hidden screenshot like `gate_view.png`
  and keeps it as one constant.
- **Body.** No collision, as with the original NPCs (models only collide
  with stage geometry, ModelCtrlManager::collide), so players walk through
  it as they walk through Leomon. It plays SQ_WAIT's clip and turns to the
  player while talking (SQ_TALK's clip), all on the host's own clock (13.6).

### 3.2 When it is there

| Situation | NPC |
| --- | --- |
| stagML11, offline, story far enough (below) | shown, talk works |
| before the Outside World Gate first leads somewhere: flag 501 (lobby intro seen) off, or mate00's first destination not open yet (its first branch tests flag 2; O1 lane D fixes the exact test from mate00.lua) | not shown |
| 2-4 local players joined (GameSystem players[i].joined / NINZU) | shown; it says: "The simulator only takes one Digimon at a time. Come back on your own!" |
| online room, direct LAN session, online rules on | not shown |
| test runs (`--smoke-test`, input scripts, campaign driver) | not shown, unless `DW4_OVERFLOW=1` (talk works) or `DW4_OVERFLOW=show` (drawn, never talks) |
| settings.ini `[overflow] npc = 0` (F1 > Gameplay: "Show the simulator NPC") | not shown |

The host reads the flags and the player count from the story engine and
writes nothing. The NPC fades in over 0.35 s when it starts to show, as the
ghost figures do, and does not pop out during demos (it is scenery).

### 3.3 Talking to it

In the main lobby, A makes a talk hitbox without an attack (model_ctrl.cpp
1409: kind 60000, delay 1, end 2, offset (0, 4, 7), size 3). A real NPC hit
by it runs hit_n_<name>.lua. Our NPC is no model, so the box hits nothing
and the A press does exactly what it does today. The host learns of the box
through a new **observe-only port event** `talk_box {slot, centre}` that
model_ctrl sends where it pushes the box (port events: off by default, never
read by the simulation, not in the fingerprint or a snapshot;
port_events.hpp). When the box of player 1 overlaps the NPC's virtual talk
cube (the original NPCs' 10-unit player_talk cube on the model origin), and
no original talk started in the same or the next step (the player is not
OFCTRL and no demo runs), the host opens the dialog.

While the dialog is open:

- the story engine **keeps stepping with neutral pads** (as with the F1 menu
  open without pause: `live` stays empty). The lobby lives on: music, the
  walking Digi-Elf, the gate's effects. Our player stands, as the
  original's talks hold it with OFCTRL;
- the pads go to the dialog; on closing, `pad_latch` waits for released
  buttons, as after the F1 menu;
- F5 / F9 close the dialog first, then act as usual.

### 3.4 Dialog

The talk window as in every NPC talk: name tab "Gekomon", the face icon in
its frame, three lines of about 34 characters per page, the ▼ prompt. The
texts are ours (English, `|` breaks lines as in the game's own texts):

First meeting (once; the meta file remembers it):

> Hello there! I keep the battle|simulator of the Digital Security|Guard.
>
> It copies enemy data from the|servers and lets it pour in until|the simulator overflows.
>
> We call it Data Overflow! Nothing|you win in there comes out with|you...
>
> ...except your records, and the|Sim Bits the simulator pays you.
>
> Your Digimon fights on its own in|there. Just keep it moving and|survive as long as you can!

Then, and on every later talk: "What will you do?" with the choice list:

| Choice | Leads to |
| --- | --- |
| Enter the simulator | character select (9.2) |
| Calibrate | the Calibration shop (8.2) |
| Data Records | the records and unlock list (8.3) |
| How does it work? | four pages of rules (controls, level-ups, Awakening, Sim Bits) |
| Never mind | "Come back any time!" and close |

After a run, before the list: "Welcome back! <one line about the run>", for
example "You lasted 12:41. Not bad at all!" or "Simulation complete! You
beat the whole thing!".

## 4. A run

### 4.1 Sim Fields

| Field | Stage | Shape (collision map, BFS from the start, floor ±9) | Notes | Unlock |
| --- | --- | --- | --- | --- |
| Violet Ring | stagBS02 (Belphemon's arena) | circle, diameter about 230 (r about 115 around (0, 0)), flat y = 10, ring wall, 42,700 u² | 56 g_box crates in the stage, i_bosshp02 for the Guardian bar, the return gate matz01 (g_dreturngate) as the exit portal after a clear, player start (-10, 10, 80) | start |
| Crimson Core | stagBS04 (Daemon's arena) | rounded square x -120..115 / z -120..115, flat y = 10, free circle r about 120, 50,500 u² | i_bosshp04, red data floor | clear Normal once |
| Goblin Keep | staggf04 | central octagon r about 105, corridors and side rooms, 73,800 u² | an arena by design; its mate00 and the GENE-00 waves stay off (MATCTRL PAUSE, GENERATER STOP) | survive 15:00 on Crimson Core |

The first.lua of each field is not run (it plays the boss intro). The
director's start (13.5) does what the field needs instead. The boss of the
stage (f_belvam in stagBS02, f_devl1b in stagBS04) is never generated. In
the original, first.lua starts it. A walk across the Violet Ring takes about
5.5 s (Agumon at full stick: 0.70 units a frame).

### 4.2 Clock and phases

The run clock counts the steps in which the run is not frozen (the level-up
and pause windows freeze it), one field each (1001/60000 s). Normal
simulation:

| Time | Phase | Families (the info's RAMs = most alive at once) | Elite and Guardian | Field music |
| --- | --- | --- | --- | --- |
| 0:00-5:00 | 1 Death Valley | e_goburi 10, e_nume 10, e_scum 10, e_ogre 9, e_cockatri 12, e_mecha1 10 (61) | Corrupted from 1:30; Guardian at 5:00: Blossomon or Mammothmon | ST_DEATH |
| 5:00-10:00 | 2 Dry Land | e_mummy 12, e_minotaru 4, e_pharaoh 3, e_mecha2 10, e_tyrano 4 (33) + e_goburi, e_cockatri (55) | Guardian at 10:00: SkullGreymon or Anomalocarimon | ST_DRYLA |
| 10:00-15:00 | 3 Venom Jungle | e_otama 10, e_geko 10, e_mecha3 8, e_maridevi 6, e_torta 4 (38) + e_mummy (50) | Guardian at 15:00: TonosamaGekomon, MRδ04 or Diaboromon | ST_VENOM |
| 15:00-20:00 | 4 Machine Pit | e_infermo 12, e_megadra 4, e_rare 8, e_mecha5 4, e_mecha4 6 (34) + e_otama, e_geko (54) | finale at 20:00: LordKnightmon or MRε05 | ST_MACHI |
| 19:00-20:00 | last minute | as phase 4 | | QS_HURRY |
| from 22:00 | OVERFLOW | everything of phase 4, rising | the finale stays | QS_HURRY on |

The Guardian of a phase is drawn from its pool with the run's seed. e_impdra
is never used (it has no hit script). The enemy rows are the families'
Normal rows (enemy table, the generatorn block). The Hard and Very Hard
simulations scale them (4.3); the families' Hard / Very Hard rows are used
for Corrupted enemies only.

### 4.3 Spawning and scaling

- **Where.** Enemies appear on a ring 70-95 units from the player, just
  outside the camera's view, on walkable cells of the field's collision map
  (precomputed at load, 5-unit grid, floor height ±9 of y = 10). They enter
  with their family's SQ_ENTR. A share of 70 % comes from the side the
  player walks towards, so standing still is no safe spot.
- **How many.** The target count of living enemies is
  `A(t) = min(cap, 14 + 3.2 x minutes)` (initial): 30 at 5:00, 46 at 10:00,
  the cap 60 from about 14:30. The director tops up to `A(t)` with at most
  two spawns every 10 frames. Every 90 s a *surge* adds 12 for 20 s. At
  7:30, 12:30 and 17:30 an *encirclement* puts a ring of 16 around the
  player at 45 units. While a Guardian lives, `A(t)` halves.
- **Which.** Every spawn picks a family of the phase by weight (the newer
  area's families weigh 3, the carried ones 1). If the family's RAMs are all
  in use (`MODELCTRL <family> -1 FIND_SPACE` finds none), it picks another.
- **Recycling.** An enemy more than 140 units from the player for 5 s
  leaves with SQ_ERAS and its RAM spawns again near the player. So a family
  with 4 RAMs keeps pressure on.
- **Scaling (initial values, Normal).** Max HP = the row's HPMAX x
  (1 + 0.10 x minutes); it goes with GENERATE, so it costs nothing. Enemy
  damage x (1 + 0.04 x minutes) through the run's damage-taken rule. EXP =
  the row's EXP. Hard: HP x 2.5, damage x 1.6, EXP and Sim Bits x 1.5. Very
  Hard: HP x 5, damage x 2.5, EXP and Sim Bits x 2.
- **OVERFLOW.** From 22:00 if the finale still lives: the cap grows by 10
  every 30 s up to the RAMs of all loaded families, enemy HP x 1.5 per
  minute. The timer shows "OVERFLOW" in red. That is the end of every run
  that does not clear: there is no time limit, only the flood.

### 4.4 Corrupted enemies, Guardians, the finale

- **Corrupted.** From 1:30, every 60 s one enemy of the phase comes as
  *Corrupted*: its family's Hard row (HP about 10x the Normal row) with the
  time scaling on top, and a metal look (SETMETAL, to be confirmed on enemy
  families in O1). It drops an ¶ X Data and a Plug.
- **Guardians.** A Guardian is a DW4 mid-boss family loaded from its own
  stage. Its ded_ script is never loaded, so no story flag, scene or door
  runs. Normal rows and HP x 6 (initial; the finale x 8):

  | Guardian | Family, record (HPMAX) | From | Pool |
  | --- | --- | --- | --- |
  | Blossomon | e_blosso #76 (250) | dng101b2 | 5:00 |
  | Mammothmon | e_mammos #97 (302) | dng102b3 | 5:00 |
  | SkullGreymon | e_skulgrey #187 (1000) | stag0202 separate/e_skulgrey | 10:00 |
  | Anomalocarimon | e_anomalo #196 (1100) | dng205b3 | 10:00 |
  | TonosamaGekomon | e_tonogeko #280 (1522) | stag0302 separate/e_tonogeko | 15:00 |
  | MRδ04 | e_mecha4 #67 (1675) | dng308b3 | 15:00 |
  | Diaboromon | e_diabolo #289 (1842) | dng309b4 | 15:00 |
  | LordKnightmon | e_ldknight #348 (2200) | dng412b4 | finale |
  | MRε05 | e_mecha5 #339 (2540) | dng411b3 | finale |

  The Guardian enters 40 units in front of the player with a camera shake
  and BT_SBOSS (the finale: BT_BOSS2). Its HP shows in the field's boss bar
  (i_bosshp02 / 04, METERCTRL SETPERCENT from GETHP; on Goblin Keep a bar of
  the HUD overlay). Defeating it gives a Recovery disk, 3 ¶ X Data and one
  Rescan, and brings the phase music back.
- **Fallback.** If a Guardian's AI misbehaves in a foreign arena (its AIM
  maps may expect its own stage's points; the O1 spike tests each), that
  Guardian is replaced by an *elite*: the largest family of its area
  (Tyrannomon, Tortomon, Megadramon) with a Hard row and the same bar.
- **The finale.** Defeating it clears the simulation: the enemies leave with
  SQ_ERAS, BT_WIN02 plays, the clear banner (i_bgclear) shows, and on the
  Violet Ring the return gate opens. Walking into it ends the run (on the
  other fields the results come after 4 s).

### 4.5 EXP and run levels

The run's level is the DW4 level of the run's player: defeats give EXP at
once (BLTcalcEXPUp, with its expnum popup), the original level-up plays
(plLVUP01, i_getitempnl01 SQ_LVUP, full HP and MP) and `recalc_player`
gives the form's stats for the level (grown_value). Only the curve is
the run's: `NEXTEXP` becomes the sum of `120 + 60 (L-1) + 6 (L-1)²`
(initial). It needs 120 EXP for level 2, about 1,150 for level 10 to 11 and
about 11,600 for level 40 to 41. With about 250,000 EXP in a cleared Normal
run (estimate: defeats per minute times the phases' EXP), that ends around
level 45. DW4's own curve ((L + 140) L² + 200 L, cumulative) would end around
level 37 and give the first choices far too slowly (level 4 only after
1,887 EXP).

### 4.6 The level-up choice

Every run level opens the level-up window (several levels at once: one
window after another). The run freezes while it is open (13.5).

- **Three offers** (four with the Wide Scan calibration). An offer is a new
  ability or passive (if a slot of its kind is free), the next level of an
  owned one, or an Awakening / Overflow (6.4). Weights (initial): next
  level 40 %, new 60 % while slots are free; the form's technique
  affinities x 2 and its ↑ techniques x 3. An eligible Awakening or
  Overflow always takes a line. When everything is full and at level 5,
  the offers are *Recovery* (+30 % HP) and *Sim Bits +50*.
- **Rescan** (X): three new offers. 1 per run, +1 per Calibration level,
  +1 per Guardian defeated.
- **Delete** (Y on a line): that ability or passive leaves this run's pool,
  and a new offer takes its line. 0 per run until the first Guardian has
  been defeated once (Data Record), then 1, +1 per Calibration level.
- **Skip** (the last line): take nothing, heal 10 % HP.
- **Colours** are DW4's rarity colours (the item lists' value colours):
  white = new, cyan = level 2-3, green = level 4, yellow = level 5,
  magenta = Awakening / Overflow, orange = Recovery / Sim Bits.

### 4.7 Pickups and crates

Pickups are the original drop models (8 RAMs per model, so at most 8 of a
kind lie on the floor; a drop that finds no RAM is credited at once instead).

| Pickup | Model | From | Effect in the run |
| --- | --- | --- | --- |
| Bits | g_000bit (record 0) | 5 % of defeats (+ Rack Board), crates | the original bit value (band by level x BITRITSU) into the run player's BIT; at the end it becomes Sim Bits |
| ¶ X Data | g_000xdata1 (record 1-3) | Corrupted, Guardians (3), crates 5 % | the key for one Awakening or Overflow |
| Plug | g_000xdata2 (record 4-25) | Corrupted, crates 20 % | the original: +1 UPCNT of that stat (Strength, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed, ⊂⊃∪∩) for the run |
| Disks | g_000item (conItem) | crates 15 %, Guardians (Recovery) | taken with A (the name label, as in the original) into the run's disk folder; used from the L item panel: HP Disk α/β/γ, MP Disk α/β/γ, Recovery |
| Boost | g_000boost1 | crates 5 % | 10 s of +50 % move speed and half cooldowns (not a card pack here) |

Instant pickups are taken by walking close to them (the original's 8 units,
d² < 64, times the pickup radius bonuses). Crates are the field's breakables
(g_box in the Violet Ring, 56 RAMs; HP 100, records 370-378): 8 stand at a
time in random free places and a broken one comes back elsewhere 30 s later
(OBJ_CONTAINER_BREAK). On fields without crates the crate drops go to
Corrupted enemies.

### 4.8 HP, MP, defeat

- HP and MP are the original's (the HUD rings and numbers). Enemy attacks
  hit with the original eneatk rules (physical, magic, proportional; stun,
  poison, sleep, bind, freeze, power down).
- Techniques cost MP (question 3): a cast takes `pcTech MP / 8` (Petty 2,
  Mega 8, Giga 18, Terra 32, ∞ 50). MP comes back at 3 % of MaxMP a second,
  plus Dread Ram and MP disks. A technique without enough MP waits, and its
  icon greys out. Arts cost nothing.
- **Defeat.** At 0 HP with a revive (Raise, the Backup Chip calibration),
  the Digimon rises at once with that revive's HP, the effect and sound of
  the original Raise and 2 s of invulnerability. Without one, the run ends: the
  original Game Over picture (i_fadewn01 SQ_GAMEOVER, QS_DONOT), then the
  results. No bit capsule, no end_g_000bcaps.lua, no stage change.

### 4.9 End of a run, results, rewards

A run ends by a clear, a defeat or Retire (pause menu). In every case:

- **Results** (9.6): time, run level, form, defeats (total and per family),
  damage per ability, the highest hit, Sim Bits earned, ¶ X Data found and
  the new Data Records.
- **Sim Bits** = the run's bits x (1 + BIT Board + Greed calibration) x the
  tier's factor, +500 for a clear (x tier).
- **Retire** keeps everything earned so far. Defeat keeps it too: the
  simulator pays for the data either way.
- The meta file is written once, when the results open (8.4).

## 5. Digimon

All 16 forms, with their shinka rows: base stats at level 1, the growth rows
(`grown_value`), the specialist class (GROW 130), the four resistances and
the six techniques. A form's run start:

- its **start Art**: the Art of its class (the class of its signature
  weapon; the Rookies' practice weapons give their class);
- its **start technique**: its first attack technique among TECH1-6 (Blast,
  Venom, Impact, Cutter, Bomb), at Petty (level 1);
- its **affinities**: TECH1-6 come twice as often in the offers, the ones
  the form's EXPLANATION marks with ↑ three times;
- its **element**: its highest resistance. Techniques of that element do
  +15 % damage, and PWR Medal takes that element. Its lowest resistance
  works against it through the original RES rules;
- its **specialist bonus**: Arts of its class +30 % damage (as GROW 130 is
  in DW4);
- its **signature**: its start Art awakens into its own signature weapon
  (6.4).

| # | Form (model) | Class: start Art | Start technique | Affinities (↑) | Element (best / worst) | HP / MP L1 | Unlock (8.3) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Agumon (p_agumon) | Blunt: Cleaver | Fire | Fire↑, Pwr Up, Heal Lt, Cutter, Bomb, Resist | Fire / Ice | 180 / 50 | start |
| 1 | Veemon (p_vmon) | Slash: Blade Arc | Bolt | Bolt↑, Freeze, Heal Lt, Impact, Resist, Bind | Bolt / Dark | 160 / 70 | start |
| 2 | Guilmon (p_girumon) | Stab: Fang Thrust | Fire | Fire↑, Pwr Dwn, Heal Lt, Dark↑, Pwr Up, Bomb | Dark / Bolt | 150 / 80 | start |
| 3 | Dorumon (p_dorumon) | Bash: Crescent Draw | Ice | Ice↑, Bind, Heal Lt, Dark, Venom, Freeze | Ice / Fire | 170 / 60 | start |
| 4 | WereGarurumon (p_weregaru) | Slash: Twin Edge | Ice | Ice↑, Heal Lt, Speed, Impact, Cure Lt, Pwr Up | Ice / Fire | 200 / 100 | Dorumon survives 10:00 |
| 5 | H-Kabuterimon (p_hekabut) | Blunt: Quake Maul | Bolt | Bolt↑, Heal Lt, Bind, Venom, Cure, Sleep | Bolt / Ice | 240 / 80 | Veemon survives 10:00 |
| 6 | WarGreymon (p_wargrey) | Crush: Claw Rush | Impact | Impact↑, Aura, Sleep, Cure Lt, Resist, Pwr Up | Fire / Dark | 240 / 80 | Agumon survives 10:00 |
| 7 | Cl-Angemon (p_angelr) | Stab: Saber Lance | Ice | Aura↑, Ice, Cure, Bolt, Raise, Cutter | Ice / Dark | 180 / 110 | Guilmon survives 10:00 |
| 8 | Beelzemon BM (p_beelzeb) | Shot: Blaster | Venom | Venom↑, Heal Lt, Freeze, Cure Lt, Pwr Dwn, Bind | Dark / Bolt | 200 / 100 | 1,500 defeats in one run with Guilmon or Cl-Angemon |
| 9 | Alphamon (p_alpha) | Bash: Glaive Wheel | Dark | Dark, Heal Lt, Freeze, Cutter↑, Cure Lt, Speed | Dark / Ice | 220 / 90 | run level 35 with Dorumon or WereGarurumon |
| 10 | BK WarGreymon (p_bwargrey) | Crush: Claw Rush | Dark | Heal Lt↑, Dark, Pwr Up, Cure Lt, Raise, Speed | Dark / Fire | 180 / 110 | clear Normal with WarGreymon |
| 11 | IPDramonFM (p_impdrafm) | Shot: Blaster | Bolt | Aura↑, Bolt, Sleep, Cure Lt, Cutter, Bomb | Bolt / Dark | 200 / 130 | clear Normal with H-Kabuterimon |
| 12 | IPDramonPM (p_impdrapm) | Slash: Twin Edge | Venom | Aura↑, Venom, Pwr Dwn, Cure, Raise, Sleep | Bolt / Fire | 240 / 110 | two Awakenings in one run with Veemon or H-Kabuterimon |
| 13 | MetalGarurumon (p_metalgaru) | Shot: Blaster | Ice | Heal Lt↑, Ice, Resist, Cure Lt, Speed, Pwr Dwn | Ice / Bolt | 220 / 90 | clear Normal with WereGarurumon |
| 14 | Gallantmon CM (p_dukecrim) | Stab: Saber Lance | Fire | Heal Lt↑, Fire, Freeze, Cure Lt, Bomb, Pwr Dwn | Fire / Ice | 220 / 120 | clear Normal with Cl-Angemon |
| 15 | Susanomon (p_susanoo) | Blunt: Quake Maul | Fire | Heal Lt↑, Fire, Speed, Cure Lt, Impact, Resist | Fire / Bolt | 260 / 100 | clear Normal with all four Rookies |

The lines (Rookie, its first unlock, its Megas) are the mode's own: Agumon,
WarGreymon, BK WarGreymon; Veemon, H-Kabuterimon, IPDramonFM / PM; Guilmon,
Cl-Angemon, Gallantmon CM / Beelzemon; Dorumon, WereGarurumon,
MetalGarurumon / Alphamon; Susanomon caps them all. The later forms are
stronger by their data (STR 100 instead of 70, more HP and MP); the run
balance keeps the Rookies able to clear Normal (15.4). The run player
holds its form's start weapon model (INI_WEAPON1), loaded at its GENERATE
like a direct start (GameSystem::direct_start).

Digivolving inside a run (the model changes at run levels 15 and 35) fits
DW4, but swapping the player's model, actions and weapon package in a
running stage is new and deep engine work. It is not in O1-O3, only an
optional spike in O4 (question 6).

## 6. Abilities

### 6.1 Slots

| Kind | Slots | What |
| --- | --- | --- |
| Arts | 2 | the weapon classes as automatic attacks (6.2) |
| Techniques | 4 | DW4's techniques, cast automatically (6.3) |
| Plugs | 3 | stat passives (7.1) |
| Chips | 3 | special passives (7.2) |

The start Art and the start technique take a slot each. Every ability and
passive has five levels.

### 6.2 Arts

An Art is a set of the original's weapon hitboxes (the attack sequences'
HITBOX boxes of kind 10000, PCWA), attached to the player for their frames
without the attack animation, so the Digimon never stops walking (13.4, hook
H6). Each comes with a slash, swing or shot effect of the x_100 family and
the swing sounds (PC_SUBURI01-09, PC_SHOT01). Arts face the player's facing
unless the table says "nearest" (the nearest enemy within 30 units, the
original's "nearby"). Arts always hit (the original weapon hit roll would
make them miss). Their damage is the original weapon formula
(`(STR x 6 + Skill x 4) / MAMORI`, crit bonus by skill) with the Art's
virtual weapon: its class weapon of grade α..ε for levels 1-5 (the ε of the
class's fifth family for the Awakening) and the run player's skill points in
that class, set by the Art's level (300 / 600 / 1000 / 1500 / 2000, the
Awakening 2500 as the signature weapons' SKILLPOINT). The level factors are
x 1.0 / 1.15 / 1.3 / 1.5 / 1.75 (initial).

Cooldown at level 1 = the class's DELAY x 2.8 frames, minus 6 % per level
(initial). The patterns are those of p_agumon's sequences (other forms' may
differ slightly; O1 takes each form's own boxes).

| Art | Class (skill) | Pattern | CD L1 | Level 2-5 | Partner (Awakening) | Awakened (class ε) | Signatures |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Cleaver | axe (Blunt) | the late line 6x8x6 from z 4 to 17 with the fan at its tip; stun chance | 1.6 s | +back line, +20 % dmg, +side lines, bigger fan | Strength Plug | Soul Burn | Judgment (Agumon) |
| Blade Arc | sword (Slash) | the 5-box arc left to right, x ±9, z 8-14 | 1.4 s | +back arc, +dmg, +return arc (right to left), +30 % reach | Speed Plug | Onslaught | Light Bringer (Veemon) |
| Fang Thrust | dagger (Stab) | the thrust boxes at z 11 and 16, nearest | 1.2 s | +1 thrust, pierces 2, +1 thrust, reach 22 | Assault Ram | Dragoon | Data Destroyer (Guilmon) |
| Crescent Draw | katana (Bash) | a wide arc (x ±12) at the nearest, high crit | 1.4 s | +crit, wider, a second arc 10 frames later, +crit damage | Spirit Plug | Ankokuten | Hyakki Yako (Dorumon) |
| Quake Maul | maul (Blunt) | the 12x8x12 slam at z 11, then z 15; stun | 2.1 s | shock ring r 20, longer stun, ring r 25, ring r 30 and a second slam | Def. Plug | Meteor Force | Nightmare Lance (H-Kabuterimon), Orochi Slayer (Susanomon) |
| Twin Edge | cutlas (Slash) | the two counter arcs of the dual swing, reach about 12 | 1.6 s | +dmg, a pair behind, +dmg, a third pair at the sides | Drain Ram | Doom Bringer | Dark Shadow (WereGarurumon), Omega Blade (IPDramonPM) |
| Saber Lance | saber (Stab) | the 4-box arc, then the lance of 3 boxes to z 19 | 1.4 s | lance to 24, +1 lance (±20°), +dmg, +2 lances | Wisdom Plug | Photon Edge | Stargazer (Cl-Angemon), Invisible (Gallantmon CM) |
| Claw Rush | arm (Crush) | the two arcs of the arm swing, pushed forward; knockback | 2.0 s | stronger knockback, +1 pair, +dmg, a shock at the end | MaxHP Plug | Drill Bunker | Kaiser Knuckle (WarGreymon), Demon Arm (BK WarGreymon) |
| Blaster | gun (Shot) | the gun shot effect x_100gunv01, nearest | 1.6 s | +1 shot, pierces, +1 shot (spread), +2 shots | MaxMP Plug | Omega Raptor | Nosferatus (Beelzemon), Executor (MetalGarurumon), Dbl Positron (IPDramonFM) |
| Glaive Wheel | glave (Bash) | the Spiral ring (kind 30000 boxes 8³ at radius about 9, 46 frames) around the player | 1.8 s | longer spin, radius 12, +dmg, 60 % uptime | Defence ROM | Infinity | Plasma Blade (Alphamon) |

The four Rookie Arts are open from the start. The others open with the
first form that starts with them, or earlier through a Data Record
(Blaster: defeat an MR Guardian; Glaive Wheel: survive 15:00).

A Jump Strike Art (the gun class's jump attack, kind 20000, MUTEKI 18) would
move the player. It is left out on purpose: in this genre the player alone
decides where the Digimon goes.

### 6.3 Techniques

A technique fires the original's own effects with the action broadcast that
the quick panel's SQ_WAZA plays, but without the cast animation: the
director sends `ACTION_BROADCAST <action>` with strings {"p_<form>"} and
ints {slot}. That was verified with HFIRE1, HICEE1, HTHDR1, HDARK1, CUTER1,
BOMBB1, VENOM1, IMPLS1 (staggf04: 180 broadcasts, 32 defeats through the
original hit scripts) and with the weapon specials. Ranks 1-5 are the
original's progression (HTHDR1 makes 1 bolt, HTHDR5 makes 5; IMPLS3 three
rings with delays 0 / 8 / 16). Damage is the original technique formula
(`(INT x 10 + Skill x 5 + Power x 20) / SEISHIN x (1000 - RES) / 1000`,
always hits) with the rank's pcTech row: the run player's technique list
gets the technique at the level's rank (hook H8). The run player's blast,
heal and force skill points follow the rank thresholds (1 / 301 / 751 /
1251 / 1751). The cast sound the quick panel would start
(SoundManager::action_started) is played by the director: plMAGIC1,
plHEAL01, plMIND01, plVENOM or plIMPACT.

Targets: the shells home on their own (the original AIH). Bomb, Venom and
the debuff pulses aim at the densest group within 40 units (the cell of a
5-unit grid with the most enemies around it). Cutter aims along the facing.
The heal and force techniques act on the player.

| Technique | Group | Action(s) | In the run | Level 1-5 (= Petty..∞) | CD L1 | Overflow partner |
| --- | --- | --- | --- | --- | --- | --- |
| Fire | Blast (fire) | HFIRE1-5 | 1-5 homing shells; a hit splashes 30 % in 8 units | shells = level, power 160-800 | 2.5 s | ⊂ Fire Plug |
| Ice | Blast (ice) | HICEE1-5 | 1-5 shards; hit enemies slow 30 % for 2 s | as Fire | 2.5 s | ⊃ Ice Plug |
| Bolt | Blast (thunder) | HTHDR1-5 | 1-5 bolts; each jumps to a second enemy at 50 % | as Fire | 2.5 s | ∪ Bolt Plug |
| Dark | Blast (dark) | HDARK1-5 | 1-5 orbs; hit enemies lose 10 % DEF for 4 s | as Fire | 2.5 s | ∩ Dark Plug |
| Venom | Blast (poison) | VENOM1-5 | a poison ball at the densest group, a pool that ticks every 2 s (power / 5) | pool 4-10 s | 3.0 s | Spirit Plug |
| Impact | Blast | IMPLS1-3 | rings around the player, knockback | 1-3 rings; level 4-5: IMPLS3 with +25 % / +50 % radius | 2.0 s | Def. Plug |
| Cutter | Blast | CUTER1-5 | 1 / 1 / 3 / 3 / 5 piercing beams in a fan | as the original | 2.5 s | Assault Ram |
| Bomb | Blast (fire) | BOMBB1-5 | a bomb at the densest group, a burning area | as the original | 2.0 s | Strength Plug |
| Heal Lt | Heal | HEALS1 | heals the rank's amount (300-19200, at most 20 % of MaxHP) | | 12 s | MaxHP Plug |
| Aura | Heal | HEALA1 | a regeneration field of radius 30 for 5 s | | 15 s | MaxMP Plug |
| Cure | Heal | CURES1 | removes poison and the other ailments, 3 s immunity | | 10 s | - |
| Raise | Heal | RESTO1 | no cast: revives (4.8); level 1: one revive at 25 % HP, 3: 50 %, 5: two revives | | - | - |
| Speed | Force | SPEDU1 | the original buff: +move speed, +evasion, -10 % cooldowns while on | rank's SUBAYASA, 6-10 s | 15 s | - |
| Resist | Force | DSILD1 | the original buff: +all RES; -10 % damage taken while on | 6-10 s | 15 s | - |
| Pwr Up | Force | POWEU1 | the original buff: +STR, +DEF, +MaxHP while on | 6-10 s | 15 s | - |
| Freeze | Force (enemies) | SPEDD1 | a pulse of radius 30: enemies slowed | | 8 s | - |
| Pwr Dwn | Force (enemies) | POWED1 | a pulse of radius 30: enemies weakened | | 8 s | - |
| Sleep | Force (enemies) | SLEEP1 | a pulse of radius 30: enemies fall asleep (chance by rank) | | 10 s | - |
| Bind | Force (enemies) | BINDD1 | a pulse of radius 30: enemies cannot move | | 10 s | - |

Start pool: Fire, Ice, Bolt, Dark, Impact, Cutter, Heal Lt, Speed. The other
eleven open through Data Records (8.3). The force buffs are the original
status effects with their icons (stdownic: Speed, Resist, Pwr Up; on
enemies: Freeze, Pwr Down, Sleep, Bind). Cooldowns are initial values;
the Speed plug and the Tempo calibration shorten them.

### 6.4 Awakening and Overflow

An ability at level 5 whose partner passive is held at any level is
*eligible*. With an ¶ X Data in hand, the next level-up window shows it as a
magenta offer; taking it spends the ¶ X Data.

- **Awakening (Arts).** The Art becomes its class's top weapon (the ε grade
  of the fifth family: Soul Burn, Onslaught, Dragoon, Ankokuten, Meteor
  Force, Doom Bringer, Photon Edge, Drill Bunker, Omega Raptor, Infinity).
  When it is the form's start Art, it becomes that form's signature weapon
  instead (Judgment, Light Bringer, ...; 6.2 lists them). Awakened:
  damage x 2.2, skill 2500, the class's best pattern at once, every hit
  adds the x_100charge01 burst, and signature weapons also get their
  weapon's element and crit (13-18).
- **Overflow (Techniques).** The technique becomes "<Name> Overflow"
  (Fire Overflow, Ice Overflow, ...): its ∞ action plus the large blast
  effect of x_200blast04 / 05, twice the area, -30 % cooldown. Ten of the
  nineteen can overflow (the table's partner column); Raise, Cure and the
  force techniques cannot.

## 7. Passives

Where DW4 has the stat, a passive is real gear or a real stat of the run's
player (UPCNT, a chip, a board), so `recalc_player` and the original
formulas do the work. The rest are run rule values (13.4, H4). Levels 1-5
(α-ε, as DW4's chips grow):

### 7.1 Plugs (3 slots)

| Plug | Per level (initial) | DW4 base |
| --- | --- | --- |
| Strength Plug | +8 % Art damage | STR (UPCNT) |
| Def. Plug | -5 % damage taken | DEF (UPCNT) |
| MaxHP Plug | +10 % MaxHP | MaxHP (UPCNT) |
| MaxMP Plug | +10 % MaxMP, +10 % MP regeneration | MaxMP (UPCNT) |
| Wisdom Plug | +8 % technique damage | INT (UPCNT) |
| Spirit Plug | +10 % ailment chance and length, -4 % magic damage taken | MND (UPCNT) |
| Speed Plug | +5 % move speed, -4 % cooldowns | Speed (UPCNT) |
| ⊂ Fire / ⊃ Ice / ∪ Bolt / ∩ Dark Plug | +10 % damage of that element, +60 to its RES | RES (UPCNT) |

Start: Strength, Def., MaxHP, MaxMP, Wisdom, Speed.

### 7.2 Chips (3 slots)

| Chip | Per level (initial) | DW4 base |
| --- | --- | --- |
| Assault Ram | +2.5 % crit | abChip Assault Ram (crit 8-16) |
| Drain Ram | heals 1 % of damage dealt (at most 3 % MaxHP a second) | HPDRAIN 4-12 |
| Dread Ram | restores 1 % of damage dealt as MP | MPDRAIN 4-12 |
| Defence ROM | +3 % chance to block a hit fully | CHOHATURITSU 8-16 |
| PWR Medal | Arts deal the form's element; +6 % element damage | ⊂⊃∪∩ PWR Medal (weapon element) |
| EXP Board | +8 % EXP | EXPRITSU 10 / 20 / 30 |
| BIT Board | +10 % bits | BITRITSU 10 / 20 / 30 |
| Rack Board | +20 % pickup radius, +8 % drop chance | the luck board |
| Cipher Core | -15 % ailment length | MAHITEIKOU |
| Element Core | +40 to all four RES | the all-RES cores |
| Talis of Order | +4 % to every stat (3 levels) | Talis of Order |
| Talis of Chaos | +10 % EXP, bits and drops, +6 % damage taken (3 levels) | Talis of Chaos |

Start: Assault Ram, Drain Ram. The Talismans are the last unlocks.

## 8. Meta progression

### 8.1 Sim Bits

Sim Bits come only from runs (4.9). They never reach the story's BIT and
the story's BIT never reaches them. The Data Records screen shows the total
earned, and the Calibrate screen shows the balance.

### 8.2 Calibration (permanent run bonuses)

Bought from Gekomon in the shop list style. Prices double per level
(initial). "Reset calibration" gives all spent Sim Bits back, free.

| Calibration | Levels | Per level | Price of level 1 | Opens |
| --- | --- | --- | --- | --- |
| Power | 5 | +5 % all damage | 200 | start |
| Guard | 5 | -3 % damage taken | 200 | start |
| Vitality | 5 | +8 % MaxHP | 200 | start |
| Mind | 5 | +8 % MaxMP, +5 % MP regeneration | 200 | start |
| Haste | 3 | +4 % move speed | 300 | start |
| Tempo | 3 | -3 % cooldowns | 400 | first Guardian defeated |
| Magnet | 3 | +20 % pickup radius | 150 | start |
| Insight | 5 | +5 % EXP | 300 | start |
| Greed | 5 | +8 % Sim Bits | 250 | start |
| Rescan | 3 | +1 Rescan | 500 | start |
| Delete | 3 | +1 Delete | 500 | first Guardian defeated |
| Backup Chip | 1 | +1 revive at 50 % HP | 6000 | first clear |
| Wide Scan | 1 | four offers instead of three | 8000 | three clears |

### 8.3 Data Records (the unlock tree)

Records are checked when a run ends (and the in-run ones at once, shown with
"NEW!" in the results). Forms are in section 5.

| Record | Condition | Opens |
| --- | --- | --- |
| Gate Keeper | defeat any Guardian | Delete charges, the Delete and Tempo calibrations |
| Blossom Data | defeat Blossomon | Venom |
| Mammoth Data | defeat Mammothmon | Bomb |
| Skull Data | defeat SkullGreymon | Resist |
| Scorpio Data | defeat Anomalocarimon | Cure |
| Shogun Data | defeat TonosamaGekomon | Freeze |
| Machine Data | defeat MRδ04 or MRε05 | Bind, Blaster |
| Diaboro Data | defeat Diaboromon | Pwr Dwn |
| Knight Data | defeat LordKnightmon | Cipher Core |
| Ten Minutes | survive 10:00 with any form | Aura |
| Fifteen Minutes | survive 15:00 with any form | Glaive Wheel, Defence ROM |
| First Clear | clear any simulation | Crimson Core, Hard, Backup Chip |
| Crimson Run | survive 15:00 on Crimson Core | Goblin Keep |
| Hard Clear | clear Hard | Very Hard |
| Veteran | defeated five times in total | Raise |
| Run Level 25 / 30 | reach it in a run | EXP Board / Pwr Up |
| Thousand | 1,000 defeats in total | Spirit Plug |
| Five Thousand | 5,000 defeats in total | Sleep |
| <Element> Adept | 300 defeats with Fire / Ice / Bolt / Dark (total) | that element's Plug |
| Caster | 500 techniques cast in total | Dread Ram |
| First Awakening | awaken an Art | PWR Medal |
| First Overflow | overflow a technique | Element Core |
| Banker | 5,000 Sim Bits earned in total | BIT Board |
| Crate Breaker | 100 crates broken in total | Rack Board |
| Rookie Mastery | clear Hard with a Rookie | Talis of Chaos |
| Very Hard Clear | clear Very Hard | Talis of Order |
| Three Clears | clear three times | Wide Scan |

### 8.4 The file

`saves/overflow.dat` beside the program, like `saves/profile.dat`: a text
file written with `dw4::save::write_file_atomic` (temporary file, flush,
read back, rename, a `.bak` only of a complete file; achievements.cpp 323 /
382 is the model). Unknown lines are skipped, so later versions can add
lines. Test runs write only where `DW4_OVERFLOW_PROFILE=<path>` says and
never to the real file. It is written when a run's results open, after a
purchase and after the first meeting. It is not part of the cloud backup or
the online character.

```
DW4OVERFLOW 1
seen_intro 1
sim_bits 3420
earned_total 18950
calibration power 2
calibration magnet 1
form p_agumon runs 14 clears_normal 1 best_time 20:00.0 best_level 44 best_defeats 3511
form p_vmon runs 3 best_time 11:42.3 best_level 27 best_defeats 1290
record gate_keeper 1
record blossom_data 1
open art blade_arc
open tech venom
total defeats 21877
total runs 17
total casts 6120
total crates 133
end
```

What a record opens is derived from the records, not stored twice. A record
that a later version adds is checked against the totals the file already
has.

## 9. Screens

### 9.1 Style

- **Windows:** the game's own window models and their open / close
  animations, drawn by the host (13.6): the talk window (i_talkwn, name tab,
  face frame, i_talkky ▼), the message window with its cursor (i_messwn,
  i_messcr, i_messky), the status window pieces (i_stbase, i_stcursor*,
  i_stdigiface) and the quick panel list (i_wzslbase00, i_wzslcursel).
- **Text:** the game font (bectext.igb, `layout_text`), white text, the
  name tab yellow (as TALKFONT03: 0.9 / 0.9 / 0.47), only the glyphs the
  game's own texts use: ASCII and ∈ ∋ ∨ ∧ ¶ ⊂ ⊃ ∪ ∩ ⊆ ∞ ↑ ↓. O1 checks every
  string against the font's character list in a test.
- **Icons:** the item glyphs (∈ Arts, ⊂⊃∪∩ elements, ¶ X Data, ∧ chips,
  ∋ ∨ boards), the face sheet (charaicon: 4 x 4 faces of 64 px, in the
  order agumon, vmon, girumon, dorumon, angelr, alpha, beelzeb, hekabut,
  weregaru, wargrey, impdrafm, impdrapm, dukecrim, susanoo, metalgaru,
  bwargrey; round 64), the status icons (stdownic00-10).
- **Colours:** the rarity colours (white, cyan, green, yellow, orange,
  magenta), the HUD's purple line.
- **Sounds:** WINDOW_OPEN / CLOSE, CURSOR_MOVE / DECIDE / CANCEL / NG.
- **Layout:** the 2D layer is 640 x 480, centred, y up; widescreen keeps it
  4:3 like the game's own layers.

### 9.2 Lobby screens (over the story engine)

1. **Talk** (3.4).
2. **Character select** ("Choose a Digimon"). The status window frame, a
   4 x 4 grid of faces (locked faces dark with "?"), the cursor on the
   faces. Beside it: the name, the shinka EXPLANATION line ("Blunt Skills
   Specialist  MP Skills: ↑Fire, ..."), the start Art and technique with
   their glyphs, HP / MP / STR / INT at level 1, the best time and level.
   For a locked face, its Data Record condition. Then two lines with the
   status window's left / right arrows (i_stcursorleft / i_stcursorright):
   "Sim Field: Violet Ring" and "Level: Normal". Last, the
   stage change window (i_zzstchgwn look): "Enter the simulator?  Yes / No".
3. **Calibrate**: the shop list look (a glyph, the name, the level as
   α..ε, the price; the selected entry's text below; the Sim Bits at the
   top). A buys, B leaves, Y asks "Reset calibration?".
4. **Data Records**: two pages. *Records*: every record, done (white with
   its date) or open (grey with its condition). *Digimon*: per form, runs,
   best time, best level, clears per tier.

### 9.3 HUD in a run

- The original player HUD (i_stpnl01..05 at V<-260,-160>: face and HP
  ring, LV, HP / MP numbers, the attack delay ring). The run's player 1
  slot is the only one, so the other three places stay empty, as solo.
- Host overlay in the game font:
  - the **timer** top centre (mm:ss, after 20:00 "OVERFLOW" in red);
  - the **EXP bar**, a thin bar in the HUD's purple under the top edge;
  - **Defeated** count and **Bits** top right;
  - the **build row** above the HUD: two Art glyphs, four technique
    glyphs (element glyph or the technique's initial), each with its level
    as a small number, greyed while waiting for MP;
  - the **Guardian bar** where the field has none (Goblin Keep).
- Damage numbers are the original's (i_dmgnum, 11 RAMs; more simply do not
  show). F1 > Gameplay can hide them in runs.

### 9.4 Level-up window

The run freezes (13.5) and the quick panel list opens in the middle:

```
 LEVEL UP!   Lv 17                       Rescan 2  Delete 1
 ┌────────────────────────────────────────────────────────┐
 │ ⊂ Fire            Lv 3 > 4    4 shells                  │ (green)
 │ ∈ Twin Edge       NEW         two counter arcs          │ (white)
 │ ∧ Drain Ram       Lv 1 > 2    heal 2 % of damage        │ (cyan)
 │   Skip            heal 10 % HP                          │
 └────────────────────────────────────────────────────────┘
  A Take   X Rescan   Y Delete   (one line of text about the cursor's offer)
```

(The box lines stand for the window model; the game draws its own frame.)
The cursor sound on moves, CURSOR_DECIDE and plLVUP01 on taking, CURSOR_NG
when no charge is left.

### 9.5 Pause

START opens it (frozen): "PAUSE" and the list *Resume*, *Build*, *Retire*.
*Build* shows the status window pages with the run's abilities and
passives (glyph, level, a line of what it does now) and the run's numbers
(time, level, defeats). *Retire* asks "End the simulation? Yes / No".

### 9.6 Results

After the Game Over picture or the clear banner: the status window frame
with "SIMULATION COMPLETE" or "SIMULATION ENDED", then in lines that appear
one after another (a cursor tick each): time, run level, form, defeats
(total and the top five families), the damage of each ability (top six),
the highest hit, Sim Bits (with the bonuses), ¶ X Data found, and the new
Data Records with "NEW!" in yellow. "A: Return to the lobby." The screen
fades and the lobby comes back with Gekomon's comment (3.4).

### 9.7 Texts

The O1 lane D keeps all strings of the mode in one table
(`src/game/overflow_text.inc`): dialog pages, menu lines, the names of the
abilities, passives, Guardians and families, the help pages. The enemy
families have no name table in the game. The names are ours, and some are
uncertain until O1's hidden screenshots: Goblimon, Numemon, Sukamon,
Ogremon, Kokatorimon, Mummymon, Minotarumon, Pharaohmon, Tyrannomon,
Otamamon, Gekomon, MarineDevimon, Tortomon (e_torta?), Infermon, Megadramon,
e_rare, e_mecha1-3 (MRα01 / MRβ02 / MRγ03?), e_arachne (Arukenimon?).

## 10. Controls

Default (keyboard and controller bindings as set in F1):

| Where | Input | Does |
| --- | --- | --- |
| lobby | A at the NPC | talk (as with every NPC) |
| dialog and menus | stick / D-pad, A, B | move, choose, back |
| run | main stick | walk (the original's walking and turning) |
| run | A | the original: picks up a labelled item (disks), otherwise the form's own attack combo (Spiral, Slam too) |
| run | B | the original guard |
| run | X | the original jump |
| run | L | the original item panel (use HP / MP disks) |
| run | R, Z | nothing (techniques are automatic, the status is in the pause menu) |
| run | START | pause |
| level-up | A, X, Y | take, Rescan, Delete |

Keeping DW4's own buttons (they are its feel, and they never get in the way
of the automatic abilities) is a choice for the user (question 4). The
other way is walking only, with A for disks.

B + X + START (the soft reset) and the console reset do nothing in a run
(13.4, H3). The F1 menu opens over a run with the settings tabs only; it
pauses the run while open (settings.pause_in_menu or not: a run is single
player and offline). Cheats, save states, the Saves tab and the Online tab
are not offered in a run.

## 11. Audio

| Moment | Sound |
| --- | --- |
| lobby dialog and menus | CURSOR_*, WINDOW_* through the host's UI sound stream (13.6); the story's BGM goes on |
| run start | QS_START (the quests' start jingle), then the phase's field music |
| phases | ST_DEATH, ST_DRYLA, ST_VENOM, ST_MACHI (4.2) |
| Guardian | BT_SBOSS, the finale BT_BOSS2; after a defeat the phase music again |
| last minute, OVERFLOW | QS_HURRY |
| countdown to the finale | z3COUNT1 / z3COUNT2 on 19:57-19:59 |
| clear | BT_WIN02 |
| defeat | QS_DONOT |
| level-up, pickups | plLVUP01, ITEM_GET |
| Arts | PC_SUBURI01-09 by pattern, PC_SHOT01, EFX_HIT_01, the element hits htFIRE01 / htICE01 / htTHUN01 / htDARK01 |
| techniques | the director's cast sound (6.3) and the effects' own sounds |
| enemies | their families' own SE banks (from the donor stages) |

The field music comes from other stages: the run's engine adds their stream
entries from the donor stages' sound lists (hook H2), as the randomizer adds
the families' SE banks (SoundManager::add_donor_sounds). The SFP pool of 48
entries with its priority eviction (save state version 36) keeps many hits
from piling up. The director starts the same cast sound at most every 4
frames.

## 12. Performance budget

Measured with the research tool (main 870a416's library): CPU per frame
(Engine::step + traverse_scene + the draw list, no GL) in stag0001 with 41
active enemies: step 0.27 ms, traverse 0.22 ms, draw list 0.14 ms (p95
0.45 / 0.33 / 0.21). staggf04 with 61 enemies of 6 families and automatic
techniques: step 0.44 ms on average (p95 0.87, max 2.7), traverse 0.32 ms,
draw list 0.20 ms, 210 draw items. A stress run (rank 5 techniques every 5
frames, 61 enemies and generator waves): 91 effects at once, 185 generated
RAMs, 324 draw items, step 0.46 ms, max 4.6 ms. That is about 13.5 µs per
enemy and step. GL in a hidden window (dng308b2): about 0.8 ms a frame with
rendering, 30 more enemies +0.04 ms.

| Budget | O1 | Stretch |
| --- | --- | --- |
| enemies alive | 60 (cap of A(t)) | 90 with all families loaded |
| per family | its RAMs (3-12) | (RAM cloning is not planned, 16) |
| Guardians | 1 at a time | |
| crates | 8 | 12 |
| pickups per model | 8 (RAMs) | |
| player effects | the RAMs per effect (x_200fire01 24, thunder01 20, cutter01 20, bomb01 12, impulse01 12, x_100slashv01 16, gunv01 12, armv01 8, the other x_100 4); a cast that finds no RAM waits a frame | |
| simulation step | p95 ≤ 2 ms, max ≤ 6 ms | |
| traverse + draw list | p95 ≤ 1.5 ms, ≤ 400 draw items | |
| presented frame | ≤ 8 ms at 2560x1440 on the documented PC | |
| run engine memory | ≤ 600 MB working set with every O1 donor (to be measured; a lobby engine is 32 MB, staggf04 + 5 donors 80 MB) | |
| run start | ≤ 3 s (the run's engine always loads fast) | |
| back to the lobby | ≤ 1 s (the story engine never left memory) | |

## 13. Technical plan

### 13.1 Two engines

```
 game_main (host)
 ├─ story engine (engine_storage, as today)
 │    lobby: steps with neutral pads while the dialog is open
 │    run:   not stepped (frozen like the F1 pause), kept in memory
 ├─ Overflow host (src/game/overflow_host.*): the NPC, the screens, the run loop
 │    ├─ NPC actor + SimUi overlay (presentation only, 13.6)
 │    └─ run engine (std::optional<Engine>, only while a run lives)
 │         ├─ OverflowRules (the hooks' values, 13.4)
 │         └─ OverflowDirector (spawns, abilities, offers, clock, 13.5)
 └─ meta file saves/overflow.dat (src/game/overflow_profile.*)
```

Offline, several engines may live (`detail::engine_constructed` throws only
while an online engine lives; `set_online` throws while more than one
lives). Each engine has its own GameSystem, messenger, Lua VM, scene cache,
sound manager and card store.

### 13.2 Isolation checklist

| What could leak | How it is kept out |
| --- | --- |
| the system random generator (`fight::generator()`, one static; every Engine constructor reseeds it, engine.cpp 603) | `std::ostringstream << fight::generator()` before the run's engine is made, `>>` back after it is destroyed and before the story engine's next step (as StageSnapshot::random). The run's engine reseeds it with the run's seed after construction |
| other process-wide state | O1 lane A audits every mutable static in `src/engine` and `src/*.cpp` (known: trace caches, the `rams_generation` counter, the randomizer's text cache: harmless). Done when the list is in the code review and the isolation test (14) passes |
| story steps | none during a run; neutral pads during the dialog (in the pad log, so save states replay) |
| the story engine's state | no send, demo, model, sound or flag write ever; the NPC and the screens are host-side |
| the talk | the observe-only port event `talk_box` (13.4, H0) |
| story save, card folders, randomizer.ini, profile.dat | the run's engine has a card store on an empty throwaway folder, both slots CardSetting `none`, no Gate Disk (NDMWPANEL NOGATEDISK); the mode writes only overflow.dat |
| save states | F5 / F9 refuse in a run ("Not in the simulator"); can_save (game_main.cpp 1616) learns the reason |
| achievements, Discord | the run engine's port events stay off (the director has its own notifications, H5), so the AchievementTracker never sees a run; Discord shows "Data Overflow - <form> Lv <n> - <mm:ss>" |
| online | no NPC online; the F1 Online tab and title entry are out of reach in a run |
| renderer and smooth motion | `renderer->clear_cache()` (texture cache by document pointer) and an interpolator reset at both switches; labels reset |
| audio | one engine is mixed at a time; the UI sounds go through a second SDL audio stream |
| test variables (DW4_GOD, DW4_LEVEL, DW4_FORMS, DW4_DEVICES, ...) | they act on every new engine; O1 lane A finds where each is applied and keeps them out of run engines unless `DW4_OVERFLOW_TEST_CHEATS=1` |
| the original story cheats and randomizer | not inherited; the run's cheats are all off except fast loading |

### 13.3 The host loop

game_main's main loop uses `auto& engine = *engine_storage` everywhere
(about 4,300 lines; a state load replaces the engine in place, which would
destroy the story here). So the run gets **its own loop**: a function
`overflow::run(HostContext&)` in `src/game/overflow_host.cpp`, called
between two passes of the main loop when the dialog says "Yes". It reuses the
window, the renderer, the SDL audio stream, the gamepads, the settings and
the input functions (`keyboard_input`, `gamepad_input`), and has its own
pacing (the main loop's fixed 59.94 Hz step, at most 4 steps a pass), its own
interpolator for smooth motion and its own overlay drawing. It returns when
the results close, or at once when the window closes (the run counts as
retired, the meta file is written, then the program quits as usual).

The switch:

1. The dialog's "Yes": the overlay fades to black over 30 frames (the story
   still steps with neutral pads; the host's master gain fades too).
2. Save the generator state. Create the run engine on a copy of the mounted
   resources, with the mods (`set_resource_overrides`), the card store as
   above, cheats off except fast loading, the OverflowRules, port events
   off. Set GameSystem::direct_start, players[0].digimon =
   "p_<form>", NINZU 1, then `request_stage(field)`.
3. Run loop until the results close.
4. Destroy the run engine, restore the generator, clear the renderer cache,
   reset the interpolator and labels, set `pad_latch`, fade the lobby
   picture and the gain back in, open Gekomon's "Welcome back" page.

A rework of game_main to an "active engine" pointer would share more code,
but it touches every line that uses `engine` and risks the original paths.
The separate loop leaves the main loop as it is, apart from the call.

### 13.4 Engine hooks

All hooks act only while the engine has OverflowRules (a pointer set by the
host before the first `request_stage`; null in every other engine, never in
a snapshot or the fingerprint). With the pointer null, each hook is one
untaken branch, so det.sh and r25c cannot change.

| Hook | Where (main 870a416) | In the run's engine |
| --- | --- | --- |
| H0 talk_box port event (the only change that runs in the story engine, observe-only) | model_ctrl.cpp 1409, port_events.hpp | sends `talk_box {slot, centre}` when the main lobby's A pushes the talk box; nothing reads it inside the simulation |
| H1 stage start | engine.cpp 1854 `run_script("first.lua")` | the director's start instead: field music, NDMWPANEL NOGATEDISK I<1>, CAMERA SETBOXCAMERA, LAYERCTL WN00 SET, STAGECTL PADINPUT ENABLE, PLAYER SQ_WAIT, i_stpnl SQ_OPEN, i_fadewn01 SQ_BLIN60, MATCTRL PAUSE for scripted mats, GENERATER STOP / RESET |
| H2 donors | engine.cpp 1775; randomizer.cpp 311 `load_enemy_donors`, 365 `route_donor`; managers.hpp 391 `add_donor_sounds` | `load_overflow_donors(families, guardians, streams)`: an explicit list, also on stagbs* / staggf* (no `fixed_stage` lock), no 6-family pool limit, Guardian families without their ded_ scripts, the BGM / jingle stream entries of named stages |
| H3 exits | `request_stage`, STAGECTL NEXTSTAGE, STAGECTL reset (soft reset, managers_core.cpp 1044, `soft_reset_allowed`), the console reset (`set_reset_enabled`), end_g_000bcaps.lua's game over | no stage change, no reset: the director gets `run_end{reason}` |
| H4 rule values | lua_host.cpp 607 (damage dealt), 726 (damage taken); combat.cpp 964 (NEXTEXP), 1092 (EXP); items.cpp 103 / 231 (pickup radius, 64); the player's walk speed; BLTisHitCheck | per bullet tag: Art and technique multipliers, always-hit Arts; damage taken; the run curve; EXP factor; pickup radius; move speed |
| H5 notifications | next to the `port_event` calls for enemy_defeated, player_defeated, level_up, item pickups | the director's own calls (deterministic, independent of port events) |
| H6 port hitbox | ModelCtrlManager (the ModelRam::Hitbox list of model_ctrl.cpp 1409) | `port_hitbox(ram, Hitbox, tag)`: the Art's boxes on the player for their frames, kind 10000, without a sequence; hits run CHKBULLET and the hit scripts as usual |
| H7 camera | the box camera (802F2BC0 logic) | an optional distance / height factor for the run (spike in O1: is the original view wide enough?) |
| H8 run player | `recalc_player` | the technique list and skill points from the director (6.3), gear from the passives |
| H9 director step | the end of `Engine::step` | `director.step(engine)` once per step, after the managers |

### 13.5 The director

`src/engine/overflow/` in dw4_engine (deterministic and testable without a
window): `director.cpp` (clock, phases, freeze), `spawner.cpp`,
`abilities.cpp`, `offers.cpp`, `pickups.cpp`, `guardians.cpp`,
`tables.inc` (the design tables of sections 4-8 as code), `run_log.cpp`.

- It talks to the engine through its own messenger in a
  `Messenger::PortOnly` scope (no message object swaps, so the soft reset's
  shared bool is never touched), with the commands the research verified:
  `MODELCTRL <family> -1 FIND_SPACE`, `MODELCTRL <family> <ram> GENERATE`
  (position, rotation, SQ_ENTR, record, max HP), SQ_ERAS, REMOVE, SETHP /
  GETHP, SETEFFECT (with the rotation vector to aim), the status commands,
  `ACTION_BROADCAST`, METERCTRL / NUMBERCTRL for the HUD parts, SOUND PLAY.
- **Freeze** for the level-up and the pause, the original's recipe
  (subwnd.cpp 1040): MODELCTRL ONFREEZE, GENERATER ONFREEZE,
  `messenger.pause(true)`, SOUND ONFREEZE; and back with OFFREEZE and
  `pause(false)`. The music plays on; the run clock stops.
- **Randomness**: its own seeded generator (the splitmix64 pattern of the
  randomizer) for spawns, offers, Guardians and drops; the engine's hit,
  crit and damage rolls use the system generator, reseeded per run.
- **Choices** (offers taken, Rescan, Delete, Skip, pause, Retire) come from
  the host as commands for a given step. The run log keeps the seed, the
  form, the field, the tier, the meta state used, the pads of every step
  and the commands, so a run replays exactly (tests, later perhaps watching
  a best run again).

### 13.6 NPC actor and SimUi overlay

- **Presentation actor core.** The round 60 ghost figures (branch
  `feat-r60ghosts`, `src/game/ghost_figures.*`, not in main yet) already
  load a model package on a worker thread from a copy of the resources,
  keep it in their own scene cache, traverse and skin it with the engine's
  own code at the present's time, add the draw items after the
  simulation's picture and give it a shadow proxy. O1 lane D takes that
  core (shared with the ghost figures if round 60 is merged by then,
  otherwise the minimal part of it) and makes it load any model from any
  stage package member: n_gekoa and n_gekoaic from stag0302, the window
  models from stagML11 and a battle stage, playing a model's motion clips
  by name (SQ_WAIT, SQ_TALK, the windows' SQ_OPENL / SQ_CLOSEL clips).
- **NPC:** that actor at its place in stagML11, faded by the camera like
  the ghost figures, turning to the player while the dialog is open.
- **SimUi:** the screens of section 9: window models from that core in the
  2D layer (TitleEntry, title_entry.hpp, shows how 2D items of the 640 x 480
  layer are placed), text with the game font (a private FontImage from
  bectext.igb and the public `layout_text`, managers.hpp 1032), glyphs and
  colours as in 9.1. The same overlay serves the lobby and the run. If
  playing the window models' clips privately proves too costly (an O1-D
  spike of one day), the fallback draws the windows as quads with the
  window models' own textures and colours.
- **UI sounds:** a second SDL audio stream on the same device. The host
  decodes CURSOR_* / WINDOW_* from the common bank (allse.sfp) with the
  project's own SFP decoder, so the story engine's sound manager is never
  asked (its SFP pool and play ids are state).

### 13.7 What never changes

- The original's scripts, data files and save formats. The snapshot
  version stays 43 (nothing new in a snapshot).
- Every existing test, the det.sh hashes, the r25c reference and the
  Dolphin comparisons.
- Online: protocols, the server, the online character.

## 14. Tests

| Test | How | Done when |
| --- | --- | --- |
| T1 det | `r44int/build/det.sh` | 4004f056 / 396cb219 unchanged |
| T2 r25c | the campaign reference run, once as usual (NPC hidden) and once with `DW4_OVERFLOW=show` (NPC drawn, H0 port event on, no talk) | 1,748,011 frames and the same fingerprints in both |
| T3 isolation | hidden smoke run: new game into the lobby, a scripted talk, a 60 s run with the kite bot, Retire, back, 2,000 more lobby frames. Reference: the same script with the dialog's frames as neutral pads and no run | `net_fingerprint()` (with the generator state) and the canonical snapshot bytes equal at the return and at the end |
| T4 save states | F5 before the talk, talk, run, return, walk, F9 | no "the replay differs"; the replay reaches the same fingerprint |
| T5 story files | the test card folder and `DW4_PROFILE` file hashed before and after T3 | unchanged; only the `DW4_OVERFLOW_PROFILE` file is new |
| T6 scripted run | `--overflow-run form=p_agumon field=stagbs02 tier=normal seed=1 bot=kite offers=priority --smoke-test 80000` (headless), twice | the same run hash twice; it reaches 5:00 and its Guardian; no Lua error; no budget overrun in the log; a summary file |
| T7 unit tests (ctest) | `overflow_profile_tests` (read, write, unknown lines, `.bak`), `overflow_offers_tests` (weights, slots, Rescan / Delete, eligibility), `overflow_spawn_tests` (ring points on walkable cells), `overflow_rng_tests` (generator saved and restored across an Engine's construction) | pass; ctest 40 + 4 |
| T8 performance | T6 with per-step timing (as DW4_TRACE_TRAVERSAL_TIME) and a hidden render pass | inside the budget of section 12 |
| T9 texts | every string of overflow_text.inc against the font's character list | no missing glyph |

The kite bot (a test helper of the director): each step it walks away from
the enemies within 40 units (weighted 1/d²), back towards the centre when it
is past 70 % of the field's radius. `offers=priority` takes offers by a
fixed list; `offers=random` by the run's seed. All runs are hidden
(`--smoke-test`, screenshots only through DW4_RENDER=1 in a hidden window),
loopback only, never a visible window.

## 15. Round plan

The rounds start after the user's answers (17). Each lane works in its own
worktree from the round's main; the merge order is given per round, and
every round ends with T1, T2, ctest and the mode's own tests.

### 15.1 Round O1: a playable first version

| Lane | Scope | Done when |
| --- | --- | --- |
| A host | `overflow_host.*`: the run loop, the switch (13.3), the generator save / restore, the static audit, renderer / audio / input / F-key / Discord / achievement isolation, the second audio stream, the F1 settings tabs in the run loop | T1, T3, T4, T5 pass; a run starts and returns from the lobby in a hidden run |
| B hooks | OverflowRules and H0-H9; `load_overflow_donors` with stream entries; the Guardian spike (each Guardian of 4.4 in the Violet Ring: enters, fights, dies without its ded_) and the camera spike | ctest passes; T1 and T2 unchanged; a spike note per Guardian (works / fallback) |
| C director | spawner, phases 1-4 on the Violet Ring at Normal, four Guardians or their fallbacks, the finale and OVERFLOW, Corrupted, pickups (bits, ¶ X Data, plugs), crates, the four Rookie Arts, the eight start techniques, the eight start passives, the run curve, offers with Rescan / Delete / Skip, Awakening of the four Rookie Arts, defeat and Retire, the run log, the kite bot | T6, T7 (offers, spawn), T8 pass |
| D screens | the presentation actor core, the NPC (place, fade, talk via H0), the dialog, the character select for the four Rookies, the level-up window, pause, results, HUD overlay, texts, glyph test; hidden screenshots of each screen for review | T9 passes; every screen in the screenshot set |
| E meta | `overflow.dat` (8.4), Sim Bits, Calibrate with Power / Guard / Vitality / Mind / Magnet / Rescan, the records needed by O1 | T7 (profile) passes |

Merge order B, C, A, D, E, then a review lane (a critique of the merged
round, as in earlier rounds). **Done:** offline and alone, talking to
Gekomon opens the menu; a run with any Rookie on the Violet Ring plays 20
minutes or more with the four phases, the Guardians, level-ups with three
offers, Rescan / Delete / Skip, Awakening and the results; the Sim Bits
reach `overflow.dat`; back in the lobby the story goes on bit for bit;
det.sh, r25c, ctest and T3-T9 pass.

### 15.2 Round O2: content pass 1 (abilities)

All ten Arts (and the form-specific boxes), all 19 techniques, all Plugs and
Chips, all Awakenings with the signature weapons, the ten Overflows, disks
with A and the L panel, Boost, the Corrupted look (SETMETAL or a fallback),
the full Calibrate list, damage and defeat statistics in the results. Done:
each ability and passive appears in a T6 variant that forces it (an
`offers=` list) and shows its effect in the run log; the first balance pass
of the curves with the kite bot (Rookies reach 10:00 on Normal in at least
half of 20 seeded runs; estimate to be tuned).

### 15.3 Round O3: content pass 2 (Digimon, unlocks, fields)

All 16 forms (start Art and technique, affinities, element, signature),
the unlock tree and the Data Records screen, Crimson Core and Goblin Keep
(their starts, crates or no crates, bars), the field music from donor
streams, the Guardian pools of all phases. Done: every form starts a run
(T6 per form); each record can be met in a forced test run; both new fields
pass T6 and T8.

### 15.4 Round O4: tiers, balance, polish

Hard and Very Hard, the OVERFLOW tuning, a balance pass by hidden bot runs
(survival time per form and tier, damage share per ability, level at each
phase), the sound limits, the options (damage numbers, NPC on / off), the
README and ROADMAP lines. Optional spikes: digivolving inside a run
(question 6), a 3D preview of the Digimon in the character select (the
ghost figure loader can show p_* models). Done: the balance targets agreed
with the user are met in the bot runs, and the user has played it.

## 16. Risks

| Risk | Effect | Answer |
| --- | --- | --- |
| Guardians' AI in a foreign arena (AIM maps may expect their stage's points; death scripts expect their scenes) | a Guardian stands still, walks off or never dies | the O1 B spike per Guardian; the elite fallback (4.4) |
| RAMs per family (3-12) and per pickup (8) | fewer enemies of one kind than the genre usually shows | the director mixes families and recycles far enemies; 60 big 3D enemies fill DW4's camera; cloning RAMs (ModelCtrl, Action2, HitLandDeliv, Shadow, script names) is deep and not planned |
| memory of 21 families and 9 Guardians as whole donor members | a large working set, a slower start | measured in O1 B; if over budget, the phase's donors load in the background and attach at a fixed step at the phase change (deterministic, the step waits) |
| the port hitbox (H6) | Arts hit differently from the original swing | the boxes come from the sequences' own HITBOX data; T6 compares an Art's hits with the same swing done by the original A press in a hidden test |
| the box camera too close for the genre | enemies come in unseen | the H7 spike; the spawn ring stays just outside the view |
| window models played privately | the screens cost more than planned | the one-day O1 D spike; the quad fallback (13.6) |
| static state shared between engines | the story's determinism | the audit and T3 (13.2) |
| SE of many hits | noise, lost important sounds | the SFP pool's priority eviction, the director's 4-frame limit |
| font glyphs | missing letters | only the game's glyphs; T9 |
| round 60 not merged when O1 starts | lane D lacks the actor core | lane D takes the minimal part of ghost_figures and the later merge shares it |

## 17. Questions for the user

The design assumes the first option of each question until the user
answers.

1. **NPC:** Gekomon (the sentry Gekomon of the Venom Jungle) or a Digi-Elf
   in a new role (its face icon is in the lobby already) or
   BlackWarGreymon (a quest-5 figure: risk of a spoiler)?
2. **Name:** "Data Overflow" with "Sim Bits", "Calibrate" and "Data
   Records", or other names?
3. **Techniques:** MP and a cooldown (DW4-like; MaxMP, Dread Ram and MP
   disks matter) or only cooldowns?
4. **Buttons in a run:** keep DW4's own buttons (A attack / pick up, B
   guard, X jump, L item panel) on top of the automatic abilities, or walk
   only (A just for disks)?
5. **Run length:** 20 minutes and the finale, or 30 minutes?
6. **Digivolving inside a run** (the form changes at run levels 15 and 35)
   as a later extra, knowing it is deep engine work, or keep one form per
   run?
7. **Story link:** should forms the story save has reached already count as
   unlocked in the simulator (only read, never written), or should
   everything be unlocked in the mode itself?
