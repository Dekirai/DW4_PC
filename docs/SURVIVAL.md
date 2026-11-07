# Data Overflow (survival mode)

Design of the survival mode the user asked for on 1 November 2026: a new NPC
next to the lobby's gate to the outer world offers a mode in the
survivors-like ("bullet heaven") genre, with different Digimon, abilities,
unlockable weapons and passives, single player and offline for now. This is
round 66, design only: nothing here is built yet. Base: main 870a416. Line
references are to that commit.

**Revision 2** (same round): the round 66 critique (`design-r66survival` @
440e2e2) found that revision 1 took DW4 rules over unfiltered where they do
not work in this genre (one-hit Corrupted enemies, hit reactions that take
the control away, full heals on every level-up, an unbreakable guard, Arts
that only strike forwards) and that its frame was too close to Vampire
Survivors. Revision 2 fixes every point; section 18 maps each finding to its
answer. The largest change: a build now grows through DW4's own systems
(skill points by use, the weapon ladder, the equipment window with its
sockets and pockets, plugs and ¶ X Data) instead of a slot-and-evolution
frame, and the first round is cut to a vertical slice the user plays first.

The mode is our own design in that genre. It is made from Digimon World 4's
own content: its 16 player forms, its weapon ladder and signature weapons,
its techniques and their skill thresholds, its cores, boards, chips and
plugs, its enemy families and mid-bosses, its stages, music, sound effects,
windows and font. What it shares with other games of the genre is only the
genre's common ground (section 1, rule 6).

Sources (none of them committed; the research tools link read-only against
the build's `dw4_engine.lib` / `dw4_core.lib`):

| Source | Where | What it gave |
| --- | --- | --- |
| engine map (round 66) | scratchpad `surv_engmap/` (`probe.cpp`, `info_*.txt`, `map_*.png`, `load_*.txt`, `donor_*.txt`, `fire_*.txt`, `gate_view.png`) | lobby NPCs and the talk chain, the gate's place, arena collision maps, direct spawning, RAM counts, donor loading, auto-fire by broadcast, CPU per enemy, isolation facts, measured hits on the player (`donor_staggf01.txt`) |
| content map (round 66) | scratchpad `surv/` (`db/*.tsv`: shinka, weapon, pcTech, abChip, armor1/2, insItem, conItem, enemy, eneatk; `allsnd.txt`, `gens.txt`, `ag_atck.txt`, `ag_waza.txt`) | forms, weapons, techniques, passives, enemies, bosses, pickups, sounds, hitbox patterns, formulas |
| round 64 maps | workflow journal `wf_57807143-c0e` | record layout, equipment rules (sockets, pockets, WU / BLUEAB, LVPOINT, value colours), gear tables |
| round 66 critique | workflow journal `wf_faf9728d-332` | the findings K1-K2, H1-H4, M1-M9, V1-V2, N1-N4 (section 18) |

ROADMAP gate 4 keeps new content (maps, Digimon, weapons) out of scope. This
mode is the user's explicit exception, and it adds rules and screens only:
every model, effect, sound and text style it shows is the game's own.

Numbers are measured (Ryzen 7 7800X3D, RTX 5070 Ti, hidden runs, other rounds
running in parallel) unless marked as an estimate or as an initial value.
Initial values are starting points for the balance passes, not promises; the
acceptance targets they must meet are in 14.2. The questions for the user
are in section 17.

## 0. Summary

- **Name and fiction.** *Data Overflow*: the Digital Security Guard's battle
  simulator. It copies enemy data from the servers and lets it pour in.
  Nothing earned inside leaves it except records and *Sim Bits*, the
  simulator's own currency. That is also the in-game reason why the mode
  never touches the story.
- **The NPC.** A Gekomon, the *Simulator Keeper*, stands at (50, 10, -80) in
  stagML11, east of the Outside World Gate (g_cytelep1). It appears once the
  gate is open, only offline, and jokes about the Gekomon in its own
  simulation. It is drawn by the host like the round 60 ghost figures, so it
  is never part of the simulation (3).
- **A run.** One Digimon, four phases of DW4 enemy families (Death Valley,
  Dry Land, Venom Jungle, Machine Pit), five minutes each, each closed by a
  *Guardian* (a DW4 mid-boss). The finale, LordKnightmon, comes after the
  fourth phase with a DW4 quest timer; defeating it clears the simulation,
  the timer running out ends it. In O3 every Guardian's defeat moves the run
  to the next arena and digivolves the Digimon along its line (4).
- **Feel.** Moving is the game. Every Art strikes the nearest enemy or group
  on its own, techniques fire on cooldowns, normal enemies die in one or two
  hits early on. A hit costs HP but never the control: a short flash and
  50 frames of invulnerability. Level-ups do not refill HP; healing comes
  from techniques, chips and pickups (2.1, 4.8).
- **Growth through DW4's own systems.** The Digimon's *Art* is its weapon
  class swinging on its own. Its class skill grows with every hit, and the
  weapon in hand climbs DW4's own ladder (Battle Hawk α up to Soul Burn ε)
  as the skill passes each weapon's SKILLPOINT. At skill 2500 the form's
  signature weapon awakens, by DW4's own equip rule. Techniques are the
  form's own six (TECH1-6), learned and ranked by DW4's level locks and
  skill thresholds. Level-ups offer three choices: *Forge* (the next weapon
  family), *Train* (a technique group's skill cap), gear for DW4's
  equipment window (a Core, a Board, chips in the pockets) and *Plugs* (6).
- **Digimon.** All 16 forms, each with its own class, signature weapon,
  six techniques, element and a trait of its own. The four Rookies are open
  from the start; Champions and Megas unlock through *Data Records* and
  start further up their ladder (5).
- **Meta progression.** Sim Bits buy permanent plugs per form in the *Plug
  Lab* (DW4's per-form UPCNT). Data Records open forms, gear for the offers
  and the starting loadout (one armour and one chip), harder simulations
  and the optional Endless mode. Everything is kept in `saves/overflow.dat`,
  never in the story save (8).
- **Look and sound.** English UI in the game's own windows, font, glyphs and
  colours: the talk window for the NPC, the status window for the character
  select, the shop list for the level-up choice, the equipment window for
  the build, the quest timer for the finale, the Game Over / clear
  presentation for the results. DW4's own music and sound effects (9, 11).
- **How it runs.** A second, isolated `Engine` for the run, offline only.
  The story engine is not stepped while a run lasts and afterwards continues
  bit for bit. The process-wide system random generator is saved before and
  restored after the run. A director in the run's engine spawns, fires and
  scales through the engine's own messenger commands. Engine hooks act only
  in that engine; two of them answer scripts natively in the run (the
  player's hit script and the field's first.lua). The story engine gets one
  observe-only port event for the talk (13).
- **Bit-identical original.** Without the mode, det.sh (4004f056 /
  396cb219) and the r25c campaign reference (1,748,011 frames) do not change.
  The NPC is hidden in test runs unless a test asks for it (14).
- **Rounds.** O1a is a vertical slice the user plays first: the run engine,
  the spawner, Agumon and Veemon with their aimed Arts and five techniques,
  the level-up choice as plain overlay boxes, started from a dev option.
  O1b adds the NPC, the game's windows, Guardians, gear and the meta file.
  O2-O4 add content, Digimon and arenas, tiers and balance (15).

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
   glyphs and value colours (9).
6. **Our own design.** The mode shares only the genre's common ground:
   automatic attacks, waves that grow over a timed run, a choice of three
   on level-up, a currency for permanent progress. It takes no character,
   weapon or item names, numbers or art from another game, and none of the
   specific systems revision 1 had borrowed: no equal split of weapon and
   passive slots, no evolution from a maxed weapon plus a partner passive
   plus a boss chest, no reroll / banish / skip charges, no shop list of
   global power-ups with a refund, no unstoppable reaper after the timer,
   no curse item. Where the mode needs a rule, it takes DW4's: skill points
   by use, the weapon ladder and SOUBI / SKILLPOINT, technique level locks
   and skill thresholds, sockets and pockets, plugs, ¶ X Data, per-form
   UPCNT, quest timers.

## 2. The mode in one page

The player talks to Gekomon in the lobby, picks a Digimon, a starting
loadout (one armour, one chip) and a simulation level, and enters. The
screen fades, the run's engine loads the arena (fast loading, about a
second) and the Digimon stands with its weapon in hand. Enemies come in from
outside the camera's view. The Art strikes the nearest enemy on its own, the
first technique fires on its cooldown; the player walks, dodges and picks
up what drops. Defeated enemies give EXP at once, as in the story. Every run
level brings the level-up window: three offers, take one.

Every hit raises the class skill, and the weapon in hand climbs the class's
ladder; every technique hit raises its group's skill, and the techniques
rank up from Petty to ∞ by the form's own thresholds. The Forge and Train
offers decide how far each ladder may climb. At class skill 2500 the form's
signature weapon awakens.

A Guardian closes each phase; LordKnightmon closes the run under a five
minute quest timer. Defeating it clears the simulation; being defeated or
running out of time ends the run. The results list time, level, defeats,
damage per ability, the build and the Sim Bits and Data Records won. Back in
the lobby the player stands where they talked to Gekomon, the story's music
goes on, and Gekomon comments on the run.

A run lasts about 22-25 minutes (question 5). The Retire choice in the pause
menu ends a run at any time and keeps what was earned.

### 2.1 The first five minutes (Agumon, Normal, the targets of 14.2)

| Time | What happens | Why it holds |
| --- | --- | --- |
| 0:00 | 14 Goblimon, Numemon and friends walk in from 70-95 units. Cleaver strikes the nearest every 1.25 s, a Fire shell flies every 2.5 s | Art and Fire each kill a phase 1 enemy in one or two hits (warm-up HP x 0.6, Art factor 2.5; 6.3) |
| about 0:15 | first level-up (80 EXP): for example *Forge: Van Blade* (a second strike at once), *Train Blast* (+100 Blast skill now), *Plug: Strength*. About then Battle Hawk β: the class skill passed 30, and the weapon's name shows over the Digimon with the original skill-up panel | every offer does something at once (4.6); skill +1 per Art hit (+1 on a crit), x 1.3 for the specialist class (6.2) |
| 0:30-1:30 | a level-up every 20-40 s; the first surge (+12 enemies) at 1:30; the first Corrupted enemy (an Ogremon with 8 times the HP and a metal look) | a Corrupted hit takes about 11 % of MaxHP (K1, 4.4) |
| 1:30-3:00 | the first hits taken: about 7 % HP each, a flash and 50 frames of invulnerability, never a stagger | the run's player hit rule (4.8) |
| about 3:00-4:00 | Fire reaches Mega at Blast 301, Cutter is learned at run level 8 with Blast 331 (technique hits plus two Train offers) | DW4's own thresholds of Agumon's TECH1 / TECH4 (6.4) |
| 3:00-5:00 | 25-30 enemies, the second surge, Battle Hawk γ, then δ; HP pressure: no refill on level-up, an HP Disk from the Corrupted | level-up keeps the HP fraction + 3 % (4.5) |
| 5:00 | Blossomon or Mammothmon arrives with BT_SBOSS; the waves halve for two minutes | the Guardian rules (4.4) |

## 3. The NPC

### 3.1 Who and where

- **Model.** n_gekoa ("Banpei Gekomon", the sentry Gekomon of the Venom
  Jungle field stag0302, with its face icon n_gekoaic). The talk tab shows
  "Gekomon". The original uses it only in stag0302, so it is new in the
  lobby, and it is not a story figure (question 1 has the alternatives).
  The run also has Gekomon enemies (e_geko, phase 3) and TonosamaGekomon as
  a Guardian; the dialog makes that a joke on purpose (3.4).
- **Place.** (50, 10, -80), yaw 0 (the model's Z axis towards +z, facing
  the lobby centre where the player comes from). That is on the main floor
  (y = 10), 45 units east of the centre of the Outside World Gate's pad
  (g_cytelep1 at (5, 27, -95), ring r about 20), outside its trigger mat
  mate00 (-5..15, -105..-85) and its info mat matf00 (-30..40, -130..-60),
  west of the Heal Circle (matf02 / mate03 from x 80). The walking Digi-Elf
  #2 patrols at z -35 / -10 (aim_n_digilff), about 45 units north. Leomon
  stands at (85, 10, -15). No original NPC, mat or route comes within 20
  units. O1b checks the place with a hidden screenshot like `gate_view.png`
  and keeps it as one constant.
- **Body.** No collision, as with the original NPCs (models only collide
  with stage geometry, ModelCtrlManager::collide), so players walk through
  it as they walk through Leomon. It plays SQ_WAIT's clip and turns to the
  player while talking (SQ_TALK's clip), all on the host's own clock (13.6).

### 3.2 When it is there

| Situation | NPC |
| --- | --- |
| stagML11, offline, story far enough (below) | shown, talk works |
| before the Outside World Gate first leads somewhere: flag 501 (lobby intro seen) off, or mate00's first destination not open yet (its first branch tests flag 2; O1b fixes the exact test from mate00.lua) | not shown |
| 2-4 local players joined (GameSystem players[i].joined / NINZU) | shown; it says: "The simulator only takes one Digimon at a time. Come back on your own!" |
| online room, direct LAN session, online rules on | not shown |
| test runs (`--smoke-test`, input scripts, campaign driver) | not shown, unless `DW4_OVERFLOW=1` (talk works) or `DW4_OVERFLOW=show` (drawn, never talks) |
| settings.ini `[overflow] npc = 0` (F1 > Gameplay: "Show the simulator NPC") | not shown |

The host reads the flags and the player count from the story engine and
writes nothing. The NPC fades in over 0.35 s when it starts to show, as the
ghost figures do, and does not pop out during demos (it is scenery).

### 3.3 Talking to it

In the main lobby, A makes a talk hitbox without an attack (model_ctrl.cpp
1331 / 1411: kind 60000, delay 1, end 2, offset (0, 4, 7), size 3). A real
NPC hit by it runs hit_n_<name>.lua. Our NPC is no model, so the box hits
nothing and the A press does exactly what it does today. The host learns of
the box through a new **observe-only port event** `talk_box {slot, centre}`
that model_ctrl sends where it pushes the box (port events never feed the
simulation, a snapshot or the fingerprint; port_events.hpp).

Port events are collected only on request. Today game_main asks for them
only for achievements, the event trace, the randomizer's bond or the online
character (game_main.cpp 2448-2452), so with achievements off the NPC could
never be spoken to. The host therefore also asks for them **while the NPC is
shown** (stagML11, the conditions of 3.2). The events of a step are taken
at game_main.cpp 1510 and handed to the AchievementTracker and the online
client; the host first takes the `talk_box` events out of that list, so the
tracker never sees them and never eats them.

When the box of player 1 overlaps the NPC's virtual talk cube (the original
NPCs' 10-unit player_talk cube on the model origin), and no original talk
started in the same or the next step (the player is not OFCTRL and no demo
runs), the host opens the dialog.

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
>
> Oh, and if you meet any Gekomon|in there... those are copies. I|am the original. Probably.

Then, and on every later talk: "What will you do?" with the choice list:

| Choice | Leads to |
| --- | --- |
| Enter the simulator | character select (9.2) |
| Plug Lab | per-form plugs for Sim Bits (8.2) |
| Data Records | the records and unlock list (8.4) |
| How does it work? | four pages of rules (controls, the weapon ladder, techniques, gear) |
| Never mind | "Come back any time!" and close |

After a run, before the list: "Welcome back! <one line about the run>", for
example "You lasted 12:41. Not bad at all!", "Simulation complete! You beat
the whole thing!" or, after a defeat by Gekomon or TonosamaGekomon, "Beaten
by a Gekomon? I will pretend I did not see that."

## 4. A run

### 4.1 Arenas and the route

O1a, O1b and O2 play the whole run on one arena, the **Violet Ring**; the
phases change the enemy families only. From O3 the run is a *route*: each
Guardian's defeat loads the next arena, and on a line the Digimon digivolves
on the way (5.3, question 6).

| Phase | Arena | Stage | Shape (collision map, BFS from the start, floor ±9) | Notes |
| --- | --- | --- | --- | --- |
| 1 | Violet Ring | stagBS02 (Belphemon's arena) | circle, diameter about 230 (r about 115 around (0, 0)), flat y = 10, ring wall, 42,700 u² | 56 g_box crates in the stage, i_bosshp02 for the Guardian bar, player start (-10, 10, 80) |
| 2 | Goblin Keep | staggf04 | central octagon r about 105, corridors and side rooms, 73,800 u² | its mate00 and the GENE-00 waves stay off (MATCTRL PAUSE, GENERATER STOP) |
| 3 | Crimson Core | stagBS04 (Daemon's first arena) | rounded square x -120..115 / z -120..115, flat y = 10, free circle r about 120, 50,500 u² | i_bosshp04, red data floor |
| 4 + finale | Dark Arena | stagBS06 | rectangle about 280 x 180, 49,700 u² | its mats matt01-04 at both ends paused; an O3 spike, fallback the Violet Ring |

The Violet Ring's return gate matz01 (g_dreturngate) is the exit portal
after a clear in O1b-O2. The boss of a stage (f_belvam in stagBS02, f_devl1b
in stagBS04, Daemon in stagBS06) is never generated: the field's first.lua
is answered by the run's own start (13.4, H1 / H10). A walk across the
Violet Ring takes about 5.5 s (Agumon at full stick: 0.70 units a frame).

**The field change** (O3). After a Guardian's defeat and its drops: the
clear banner, plWARP01, the fade (i_fadewn01), then the director's own
`request_stage(next arena)` with GameSystem::direct_start and, on a line,
the next form as players[0].digimon. The run clock stops during the load
(fast loading, about 16 frames measured). DW4 already carries a player's
state across stage changes (level, EXP, skills, equipment, UPCNT, HP), so
the run keeps them without help; the director restores only its own state
(clock, phase, cooldowns, offer state, its generator) and, for a
digivolution, copies the run's level, EXP, skills and UPCNT into the new
form's BASE row before `load_species` (13.4, H8). HP keeps its fraction.

### 4.2 Clock and phases

The run clock counts the steps in which the run is not frozen (the level-up
and pause windows freeze it), one field each (1001/60000 s). A phase spawns
for five minutes; then its Guardian comes, and the next phase starts when
the Guardian falls. The times below assume quick Guardian defeats. Normal
simulation:

| Phase (spawn time) | New families (RAMs) | Pool | RAMs in the pool | Highest A(t) | Guardian | Music |
| --- | --- | --- | --- | --- | --- | --- |
| 1 Death Valley (0:00-5:00) | e_goburi 10, e_nume 10, e_scum 10, e_ogre 9, e_cockatri 12, e_mecha1 10 (61) | phase 1 | 61 | 30 | Blossomon or Mammothmon | ST_DEATH |
| 2 Dry Land (5:00-10:00) | e_mummy 12, e_minotaru 4, e_pharaoh 3, e_mecha2 10, e_tyrano 4 (33) | phases 1-2 | 94 | 46 | SkullGreymon or Anomalocarimon | ST_DRYLA |
| 3 Venom Jungle (10:00-15:00) | e_otama 10, e_geko 10, e_mecha3 8, e_maridevi 6, e_torta 4 (38) | phases 1-3 | 132 | 60 | TonosamaGekomon or Diaboromon | ST_VENOM |
| 4 Machine Pit (15:00-20:00) | e_infermo 12, e_megadra 4, e_rare 8, e_mecha5 4, e_mecha4 6 (34) | phases 1-4 | 166 | 60 | finale: LordKnightmon | ST_MACHI |
| finale | as phase 4 | phases 1-4 | 166 | 60 | | BT_BOSS2, QS_HURRY in its last minute |

- **Families stay.** The families of all earlier phases stay in the pool
  (weight 0.5; the phase's own families weigh 3), so the pool always has
  RAMs to spare: a unit test checks for every phase that its RAMs are at
  least its highest A(t) + 28 (a surge of 12 and an encirclement of 16):
  61 ≥ 58, 94 ≥ 74, 132 ≥ 88, 166 ≥ 88.
- **No family is two things.** A family that is a Guardian is never a
  regular family of the same run. That is why MRδ04 (e_mecha4 #67) and
  MRε05 (e_mecha5 #339) are no Guardians: both families are regular in
  phase 4. The finale is LordKnightmon only.
- The Guardian of a phase is drawn from its pool with the run's seed.
  e_impdra is never used (it has no hit script). The enemy rows are the
  families' **Normal rows** (enemy table, the generatorn block) on every
  tier and for every enemy; the Hard / Very Hard rows are never used.

### 4.3 Spawning and scaling

- **Where.** Enemies appear on a ring 70-95 units from the player, just
  outside the camera's view, on walkable cells of the arena's collision map
  (precomputed at load, 5-unit grid, floor height ±9 of y = 10). They enter
  with their family's SQ_ENTR. A share of 70 % comes from the side the
  player walks towards, so standing still is no safe spot.
- **Do they come?** The AIM maps pick a near, middle, far or outside table
  by distance (model_ctrl.cpp 2466-2555); what "far" and "outside" do at
  70-95 units is not known yet, and a probe saw both idle and moving
  enemies. The O1a spike measures per family the share that closes in and
  its speed against the player's 0.70 units a frame. A family that idles
  gets the 40-60 unit ring (SQ_ENTR is its announcement). In every run, an
  enemy that has not come closer for 6 s while more than 30 units away is
  recycled near the player.
- **How many.** The target count of living enemies is
  `A(t) = min(60, 14 + 3.2 x minutes)` (initial): 30 at 5:00, 46 at 10:00,
  60 from about 14:30. The director tops up to `A(t)` with at most two
  spawns every 10 frames. Every 90 s a *surge* adds 12 for 20 s. At 7:30,
  12:30 and 17:30 an *encirclement* takes the 16 farthest living enemies
  away (SQ_ERAS) and generates them again on a ring of 45 units around the
  player: no new RAMs are needed. While a Guardian lives, `A(t)` halves for
  two minutes, then comes back in full.
- **Which.** Every spawn picks a family of the pool by weight. If the
  family's RAMs are all in use (`MODELCTRL <family> -1 FIND_SPACE` finds
  none), it picks another.
- **Recycling.** An enemy more than 140 units from the player for 5 s
  leaves with SQ_ERAS and its RAM spawns again near the player. So a family
  with 4 RAMs keeps pressure on.
- **Scaling (initial values, Normal).** Max HP = the row's HPMAX x
  (1 + 0.10 x minutes), and x 0.6 while phase 1 lasts (the warm-up). It goes
  with GENERATE, so it costs nothing. Enemy damage x (1 + 0.04 x minutes)
  through the run's damage-taken rule. EXP = the row's EXP. Hard: HP x 2,
  damage x 1.2, spawn rate x 1.25, EXP and Sim Bits x 1.5. Very Hard: HP x
  3, damage x 1.4, spawn rate x 1.5, EXP and Sim Bits x 2. On every tier no
  hit of a non-Guardian may take more than 35 % of a Rookie's MaxHP (4.8,
  14.3).

### 4.4 Corrupted enemies, Guardians, the finale

- **Corrupted.** From 1:30, every 60 s one enemy of the pool comes as
  *Corrupted*: its family's **Normal row** with HP x 8 and damage x 1.5 on
  top of the time scaling, and a metal look (SETMETAL, confirmed on enemy
  families in O1b; fallback: the family's own colour). The director's own
  pushes (Claw Rush, Impact) do not move it. It drops ¶ Data UP 20, a Plug
  and, at 20 %, an HP Disk α. (Revision 1 used the family's Hard row: for
  e_goburi TSUYOSA 1188 instead of 16, a hit of about 1,030 against
  Agumon's 180 HP.)
- **Guardians.** A Guardian is a DW4 mid-boss family loaded from its own
  stage. Its ded_ script is never loaded, so no story flag, scene or door
  runs. Normal rows and HP x 6 (initial; the finale x 8):

  | Guardian | Family, record (HPMAX) | From | Pool |
  | --- | --- | --- | --- |
  | Blossomon | e_blosso #76 (250) | dng101b2 | phase 1 |
  | Mammothmon | e_mammos #97 (302) | dng102b3 | phase 1 |
  | SkullGreymon | e_skulgrey #187 (1000) | stag0202 separate/e_skulgrey | phase 2 |
  | Anomalocarimon | e_anomalo #196 (1100) | dng205b3 | phase 2 |
  | TonosamaGekomon | e_tonogeko #280 (1522) | stag0302 separate/e_tonogeko | phase 3 |
  | Diaboromon | e_diabolo #289 (1842) | dng309b4 | phase 3 |
  | LordKnightmon | e_ldknight #348 (2200) | dng412b4 | finale |

  The Guardian enters 40 units in front of the player with a camera shake
  and BT_SBOSS (the finale: BT_BOSS2). Its HP shows in the arena's boss bar
  (i_bosshp02 / 04, METERCTRL SETPERCENT from GETHP; elsewhere a bar of the
  HUD overlay). Guardians are the only enemies whose hits use the
  original's full reactions (4.8). Defeating one gives a Recovery disk and
  ¶ Data UP 100, brings the phase music back and starts the next phase (in
  O3: the next arena).
- **Fallback.** If a Guardian's AI misbehaves in a foreign arena (its AIM
  maps may expect its own stage's points; the O1b spike tests each), that
  Guardian is replaced by an *elite*: the largest family of its area
  (Tyrannomon, Tortomon, Megadramon) with its Normal row, HP x 12 and the
  same bar.
- **The finale.** LordKnightmon arrives when phase 4's spawn time ends. The
  DW4 quest timer opens (i_timenm0 with NUMBERCTRL timenm0 / timenm1 at
  V<-290,217,0>, SQ_OPENUP) and counts down from 5:00; QS_HURRY plays in its
  last minute and z3COUNT1 / z3COUNT2 on its last three seconds. Defeating
  LordKnightmon clears the simulation: the enemies leave with SQ_ERAS,
  BT_WIN02 plays, the clear banner (i_bgclear) shows, and on the Violet Ring
  the return gate opens; walking into it ends the run (elsewhere the results
  come after 4 s). When the timer reaches 0:00, "TIME UP" shows, QS_DONOT
  plays and the run ends uncleared; everything earned is kept. There is no
  rising flood: the finale is the end.
- **Endless** (optional, O4, opened by the First Clear record). Chosen in
  the character select instead of a tier: after the finale's defeat the
  simulation goes on with phase 4's pool, `A(t)` +10 a minute up to the
  pool's RAMs and enemy HP x 1.3 a minute, until the Digimon falls. It has
  its own best time and no clear.
- **As built in round O1b** (lane E, docs/ROUND_O1B_E_NOTES.md): the phases
  stay on the run clock (the balance of docs/OVERFLOW_DIRECTOR.md 10-11 is
  built on it); a Guardian enters when its phase's five minutes end and the
  next phase's families come in over their ramp beside it; the next
  Guardian waits for the one before, and one still alive at 20:00 leaves
  for LordKnightmon. A Guardian's HP is its HPMAX x 6 (the finale x 14)
  times the time's linear part (1 + 0.10 a minute) only; its hits carry a
  damage factor of its own and take at most 30 % of MaxHP (no one-hit
  Guardian). The spike found two Guardians of separate packages without
  their scripts (TonosamaGekomon could not be damaged): their stage's main
  member gives them now. No Guardian needs the elite fallback on the Violet
  Ring. A Guardian's entry shakes the camera with the original's stomp
  shake (CAMERA SHAKE 37/F<0.3>/F<3.0>/F<60.0>, Mammothmon's in dng102b3).
  The return gate (the finish of lane E): 2 s after LordKnightmon's fall the
  Violet Ring's own g_dreturngate comes where Belphemon's hit script puts
  it (V<0,10,-60>, SQ_ENTR, SQ_POWERON 30 frames later); the arena's mats
  pause (its matz01 would open the stage change prompt), and walking into
  matz01's box ends the run with the results. A player standing there
  when it comes steps out and in again, as with the original's mat; a test
  bot walks in (and gets the results after 60 s without it).

### 4.5 EXP and run levels

The run's level is the DW4 level of the run's player: defeats give EXP at
once (BLTcalcEXPUp, with its expnum popup), the original level-up plays
(plLVUP01, i_getitempnl01 SQ_LVUP) and `recalc_player` gives the form's
stats for the level (grown_value).

Two things are the run's own (H4):

- **No refill.** DW4 refills HP and MP on every level-up (combat.cpp
  1112-1113). In a run a level-up keeps the HP and MP *fractions* (the new
  maximum times the old fraction, so a full bar stays full and a hurt
  Digimon stays hurt) and adds 3 % of the new MaxHP (initial, at most 5 %).
  HP comes back only from techniques, chips and pickups (4.8). One trap:
  RECALCPLAYER refills HP and MP at HP 0, so the run's defeat check runs in
  the player hit handler, before any recalc.
- **The curve.** `NEXTEXP` becomes the sum of `80 + 50 (L-1) + 6 (L-1)²`
  (initial): 80 EXP for level 2 (about six Goblimon), about 1,000 for level
  10 to 11. Expected levels (estimate until O1a's bot runs measure them, 14.2):
  about 12 at 5:00, 22 at 10:00, 32 at 15:00, 40 at 20:00. DW4's own curve
  ((L + 140) L² + 200 L, cumulative) would give the first choice only after
  341 EXP.

### 4.6 The level-up choice

Every run level opens the level-up window (several levels at once: one
window after another). The run freezes while it is open (13.5). It shows
**three offers**, and the player takes one. There are no reroll, banish or
skip charges: every offer does something at once, and the Plug offer is
always possible, so no window is ever empty.

| Offer | Glyph | Steps | What it does at once | What it opens | When it is offered |
| --- | --- | --- | --- | --- | --- |
| **Forge: <family>** | ∈ | 4 (families 2-5 of the class) | the Art's next pattern step (6.3) | the class skill cap: the new family's ε SKILLPOINT (Van Blade 1300, Crusher 1920, Destroyer 2460, Soul Burn 2810 for the axe) | while a family is left |
| **Train <Blast / Heal / Force>** | the group's top technique's glyph | 4 per group | +100 skill in that group | the group's cap: 300 → 750 → 1250 → 1750 → 2500 | the form has a technique of that group |
| **Core** | ∋ | equip α, then β (run level 22), γ (37), then +value | the core's stats | the core pocket at β | a core is open (8.4) |
| **Board** | ∨ | equip α, then +value (+30 / +60 / +120 / +240 / +480) | the board's stats; the value adds to its SHUSEINOURYOKU column | | a board is open |
| **Chip** | ∧ | equip into a free pocket; Rams and Defence ROM: the next record (β..ε) | the chip's stats | | a pocket is free or a chip can step |
| **Plug: <stat>** | the stat's name | unlimited | +UPCNT of one stat: 10 + 2 x run level points | | always (the filler) |
| **??????** | ∋ / ∨ grey | at most one line | appraised on taking, by the Connoisseur's own tables (coarmor1 / coarmor2, the band by the run level): a random Core or Board, sometimes one no offer gives | | from the Fifteen Minutes record, from phase 3 |

Weights (initial): Forge 25 %, Train 25 %, gear 30 %, Plug 20 %; a window
never shows the same offer twice, and Plug takes a line only when fewer than
three others are possible or by its weight. **The first Forge is sure**
(the forge round of 05.11, docs/OVERFLOW_DIRECTOR.md 11): a form whose
class has more than one family finds its first Forge on the first line of
each of its first three level-up offers until it takes it (the cursor
starts there, and the line under the window says what a Forge does); the
other lines are drawn as always. Agumon's crowd answer is its Art's Forge
steps (Veemon has Impact): a newcomer who took no Forge in the first three
minutes ended before 10:00 nearly every time. The Core and Board steps follow
DW4: a core needs the run level to reach its LVPOINT; boards have only
their α row in a run (β needs level 61) and grow by value.

**Colours** are DW4's own name colours of what the line gives: the value
colours for weapons, cores and boards (white 0, cyan 1-30, green 31-100,
yellow 101-500, orange 501-999, magenta from 1000), white for chips, plugs
and techniques, grey for ??????.

### 4.7 Pickups and crates

Pickups are the original drop models (8 RAMs per model, so at most 8 of a
kind lie on the floor; a drop that finds no RAM is credited at once instead).

| Pickup | Model | From | Effect in the run |
| --- | --- | --- | --- |
| Bits | g_000bit (record 0) | 5 % of defeats (BIT Board's BITRITSU adds), crates | the original bit value (band by level x BITRITSU) into the run player's BIT; at the end it becomes Sim Bits |
| ¶ Data UP 5 / 20 / 100 | g_000xdata1 (records 1-3) | crates 5 % (5), Corrupted (20), Guardians (100) | fuel for chip power-ups (7.5) |
| Plug | g_000xdata2 (records 4-25) | Corrupted, crates 20 % | +UPCNT of that stat for the run, as the Plug offer (Strength, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed, ⊂⊃∪∩) |
| Disks | g_000item (conItem) | Corrupted (HP Disk α 20 %), crates 15 %, Guardians (Recovery) | taken with A (the name label, as in the original) into the run's disk folder; used from the L item panel: HP Disk α/β/γ, MP Disk α/β/γ, Recovery |
| Boost | g_000boost1 | crates 5 % | 10 s of +50 % move speed and half cooldowns |

Instant pickups are taken by walking close to them (the original's 8 units,
d² < 64). EXP needs no pickup: DW4 gives it at the defeat. Crates are the
arena's breakables (g_box in the Violet Ring, 56 RAMs; HP 100, records
370-378): 8 stand at a time in random free places and a broken one comes
back elsewhere 30 s later (OBJ_CONTAINER_BREAK). On arenas without crates
the crate drops go to Corrupted enemies.

### 4.8 HP, MP, hits, defeat

**Damage taken.** Enemy attacks hit with the original eneatk rules
(physical TSUYOSA x power / MAMORI, combat.cpp 246-248; magic; proportional)
through the run's damage-taken rule (time factor, tier, Corrupted x 1.5).
As a safety net the rule clamps a single hit of a non-Guardian at 35 % of
MaxHP and logs it; on Normal the bot runs must never hit the clamp (14.2).

**Hits keep the control (the run's player hit rule).** In DW4 every enemy
hit sets SQ_SDMG, SQ_MDMG, SQ_LDMG or SQ_DDMG on the player by damage rank,
plus stun, bind or poison (dng101b1 hit_p_agumon.lua, decompiled lines 86-128), and with about
56 enemies near the player that was a hit every 13-47 frames
(`donor_staggf01.txt`). In a run the player's hit script is answered by a
native handler (H10) that keeps the script's own steps but changes the
reaction:

- a hit of a **normal or Corrupted enemy**: the damage, the damage number,
  the HUD updates (METERCTRL hpmt, NUMBERCTRL hpnm, the i_stpnl02 shake), a
  short flash of the model and PC_DMG01; **no damage sequence**, so the
  player keeps walking. Stun, sleep, bind and freeze from these enemies
  last at most 30 frames; poison and power-down keep their own times;
- a hit of a **Guardian**: the original reaction by damage rank (SQ_SDMG up
  to the knock-down SQ_DDMG) and statuses: Guardians are the moments that
  are allowed to throw the Digimon;
- after every damaging hit: **50 frames MUTEKI** on the player's RAM (the
  hit check refuses enemy hits on a RAM with MUTEKI, model_ctrl.cpp 2592;
  a positive count survives sequence starts, 401) and the Digimon blinks
  (the host leaves out its draw items every other 4 frames; no engine
  state);
- a **miss** (a core's BOUGYORITSU or the evasion of Defence ROM, combat.cpp
  224-232) shows the guard effect x_100guard01 and EFX_GUARD without
  SQ_GUARDR;
- heal and force techniques on the player (kinds 80000-89999) run as in the
  script (BLPcalcKoukaMagic, BLTcalcSkillUp);
- the handler counts the frames in which the player has no control (a
  damage sequence, a control status, OFCTRL); the bot runs must stay at or
  below 5 % (14.2).

**The guard** exists only with the classic buttons (question 4): in a run it
lets a guarded hit through at 50 % of its damage (DW4's SQ_GUARD turns every
hit into SQ_GUARDR without damage or direction check, hit_p_agumon.lua
lines 36-40), plays no reaction, and B is ignored for 3 s after a guard
ends.

**Healing** comes only from: Heal Lt and Aura (6.4), Drain Ram (HPDRAIN, a
share of the damage dealt, lua_host.cpp 616; at most 2 % of MaxHP a second
in a run), disks, the Guardians' Recovery and the 3 % of a level-up.

**MP** (M5, question 3). Techniques run on cooldowns; DW4's blast costs
grow 25-fold from Petty to ∞ while the power grows 5-fold, so MP on attack
techniques would make every rank-up a trap. MP pays only the **heal group**
(Heal Lt, Aura, Cure, Raise), at pcTech MP / 8, which is about in
proportion to what each rank heals (300-19,200 HP for 20-960 MP). MP
regenerates 2 % of MaxMP a second (initial), plus Dread Ram and MP disks. A
heal that lacks MP waits, and its glyph greys out.

**Defeat.** At 0 HP with Raise learned and its MP in hand, the Digimon rises
at once with the rank's HP, the effect and sound of the original Raise and
120 frames of MUTEKI. Otherwise the run ends: the original Game Over picture
(i_fadewn01 SQ_GAMEOVER, QS_DONOT), then the results. No bit capsule, no
end_g_000bcaps.lua, no stage change.

### 4.9 End of a run, results, rewards

A run ends by a clear, a defeat, the finale's timer or Retire (pause menu).
In every case:

- **Results** (9.6): time, run level, form(s), defeats (total and per
  family), damage per ability, the highest hit, the build (the weapon
  reached, core, board, chips), Sim Bits earned and the new Data Records.
- **Sim Bits** = the run's bits x the tier's factor, +500 for a clear (x
  tier). The BIT Board already raised the bits in the run.
- **Retire** keeps everything earned so far. Defeat and TIME UP keep it too:
  the simulator pays for the data either way.
- The meta file is written once, when the results open (8.5).

## 5. Digimon

### 5.1 The forms

All 16 forms, with their shinka rows: base stats at level 1, the growth rows
(`grown_value`), the specialist class (GROW 130), the four resistances and
the six techniques TECH1-6 with their level locks and skill thresholds. A
form in a run has:

- its **Art**: its class, the class of its signature weapon (6.3), starting
  on the family 1 α weapon of that class;
- its **six techniques**, TECH1-6 in the shinka order, learned and ranked by
  DW4's rules (6.4). Nothing else, except up to two *extensions* from Data
  Records (6.4). The ↑ / ↓ marks of the EXPLANATION line are not a weight:
  they are already in the thresholds (Agumon's ↓Heal Lt is TECH3 with ranks
  at 1 / 931 / 1271 / 1671 / 2171 instead of TECH1's 1 / 301 / 751 / 1251 /
  1751);
- its **element**: its highest resistance. Techniques of that element do
  +15 % damage. Its lowest resistance works against it through the original
  RES rules;
- its **trait** (5.2), and its **signature weapon** (6.5).

| # | Form (model) | Class: Art | Signature | TECH1-6 | Element (best / worst) | HP / MP L1 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Agumon (p_agumon) | Blunt: Cleaver | Judgment | Fire, Pwr Up, Heal Lt, Cutter, Bomb, Resist | Fire / Ice | 180 / 50 |
| 1 | Veemon (p_vmon) | Slash: Blade Arc | Light Bringer | Bolt, Freeze, Heal Lt, Impact, Resist, Bind | Bolt / Dark | 160 / 70 |
| 2 | Guilmon (p_girumon) | Stab: Fang Thrust | Data Destroyer | Fire, Pwr Dwn, Heal Lt, Dark, Pwr Up, Bomb | Dark / Bolt | 150 / 80 |
| 3 | Dorumon (p_dorumon) | Bash: Crescent Draw | Hyakki Yako | Ice, Bind, Heal Lt, Dark, Venom, Freeze | Ice / Fire | 170 / 60 |
| 4 | WereGarurumon (p_weregaru) | Slash: Twin Edge | Dark Shadow | Ice, Heal Lt, Speed, Impact, Cure Lt, Pwr Up | Ice / Fire | 200 / 100 |
| 5 | H-Kabuterimon (p_hekabut) | Blunt: Quake Maul | Nightmare Lance | Bolt, Heal Lt, Bind, Venom, Cure, Sleep | Bolt / Ice | 240 / 80 |
| 6 | WarGreymon (p_wargrey) | Crush: Claw Rush | Kaiser Knuckle | Impact, Aura, Sleep, Cure Lt, Resist, Pwr Up | Fire / Dark | 240 / 80 |
| 7 | Cl-Angemon (p_angelr) | Stab: Saber Lance | Stargazer | Aura, Ice, Cure, Bolt, Raise, Cutter | Ice / Dark | 180 / 110 |
| 8 | Beelzemon BM (p_beelzeb) | Shot: Blaster | Nosferatus | Venom, Heal Lt, Freeze, Cure Lt, Pwr Dwn, Bind | Dark / Bolt | 200 / 100 |
| 9 | Alphamon (p_alpha) | Bash: Glaive Wheel | Plasma Blade | Dark, Heal Lt, Freeze, Cutter, Cure Lt, Speed | Dark / Ice | 220 / 90 |
| 10 | BK WarGreymon (p_bwargrey) | Crush: Claw Rush | Demon Arm | Heal Lt, Dark, Pwr Up, Cure Lt, Raise, Speed | Dark / Fire | 180 / 110 |
| 11 | IPDramonFM (p_impdrafm) | Shot: Blaster | Dbl Positron | Aura, Bolt, Sleep, Cure Lt, Cutter, Bomb | Bolt / Dark | 200 / 130 |
| 12 | IPDramonPM (p_impdrapm) | Slash: Twin Edge | Omega Blade | Aura, Venom, Pwr Dwn, Cure, Raise, Sleep | Bolt / Fire | 240 / 110 |
| 13 | MetalGarurumon (p_metalgaru) | Shot: Blaster | Executor | Heal Lt, Ice, Resist, Cure Lt, Speed, Pwr Dwn | Ice / Bolt | 220 / 90 |
| 14 | Gallantmon CM (p_dukecrim) | Stab: Saber Lance | Invisible | Heal Lt, Fire, Freeze, Cure Lt, Bomb, Pwr Dwn | Fire / Ice | 220 / 120 |
| 15 | Susanomon (p_susanoo) | Blunt: Quake Maul | Orochi Slayer | Heal Lt, Fire, Speed, Cure Lt, Impact, Resist | Fire / Bolt | 260 / 100 |

The later forms are stronger by their data (STR 100 instead of 70, more HP
and MP); the run balance keeps the Rookies able to clear Normal (14.2).

### 5.2 Traits

Each form has one trait of its own, made from its data (its ↑ techniques,
its best stat, its class). Initial values:

| Form | Trait | Effect |
| --- | --- | --- |
| Agumon | Blaze | Fire shells splash 12 units instead of 8 |
| Veemon | Chain | Bolt jumps to two more enemies instead of one |
| Guilmon | Hazard | enemies weakened by Dark take +20 % from Fire |
| Dorumon | Frost Hide | Ice slows by 50 % instead of 30 % |
| WereGarurumon | Moonstep (DEX 100) | +10 % move speed |
| H-Kabuterimon | Heavy Shell (HP 240) | +15 % MaxHP; Quake Maul's shock ring +5 units |
| WarGreymon | Brave Shield | the first hit every 10 s does no damage |
| Cl-Angemon | Holy Ground | enemies inside Aura move 30 % slower |
| Beelzemon BM | Double Tap | Blaster fires one more shot |
| Alphamon | Royal Edge | Cutter beams pierce without limit |
| BK WarGreymon | Dark Force | Art damage +20 % while below half HP |
| IPDramonFM | Positron (INT 120) | technique cooldowns -15 % |
| IPDramonPM | Paladin | Art hits restore 0.5 % of MaxHP (at most 2 % a second) |
| MetalGarurumon | Cold Steel | Blaster shots freeze normal enemies for 20 frames |
| Gallantmon CM | Crimson | Heal Lt heals 10 % more and gives 30 frames of MUTEKI |
| Susanomon | All Elements | the Art takes the element each enemy is weakest to |

### 5.3 Start states, lines and unlocks

- **Start state.** A Rookie starts at run level 1 with class skill 1 (Forge
  1, family 1 α). A Champion picked as the start starts with class skill
  300 (family 1 γ). A Mega picked as the start starts at Forge 2 with class
  skill 810 (family 2 α): a pre-levelled Art that reaches its signature
  sooner. Every form starts with TECH1 learned (rank 1).
- **Lines** (O3, question 6). On the route of 4.1 a form on a line
  digivolves when a Guardian falls. The lines keep the weapon class, so the
  Art, its class skill and the signature stay legal by DW4's equip rules
  (the regular family weapons have SOUBI 65535, the signatures one form's
  bit and SKILLPOINT 2500; DW4 has no skill transfer between classes):

  | Class | Line (digivolves after Guardian ...) |
  | --- | --- |
  | Blunt | Agumon → H-Kabuterimon (1) → Susanomon (3) |
  | Slash | Veemon → WereGarurumon (1) → IPDramonPM (3) |
  | Stab | Guilmon → Cl-Angemon (1) → Gallantmon CM (3) |
  | Bash | Dorumon → Alphamon (2) |
  | Crush | WarGreymon → BK WarGreymon (3) |
  | Shot | Beelzemon BM, IPDramonFM, MetalGarurumon: no line |

  At a digivolution the new form brings its own six techniques and trait;
  the group skills carry over, so several of its techniques are learned at
  once by the same rule. The signature at the end of the line is the form
  the Digimon has when its class skill reaches 2500.
- **Unlocks as a start form** (Data Records, 8.4; available from O3):

  | Form | Unlock |
  | --- | --- |
  | Agumon, Veemon, Guilmon, Dorumon | start |
  | H-Kabuterimon / WereGarurumon / Cl-Angemon | survive 10:00 with the line's Rookie |
  | WarGreymon | survive 10:00 with any Rookie |
  | Alphamon | clear Normal with Dorumon |
  | Susanomon | clear Normal with all four Rookies |
  | IPDramonPM / Gallantmon CM | clear Normal with the line's Rookie or Champion |
  | BK WarGreymon | clear Normal with WarGreymon |
  | Beelzemon BM | 1,500 defeats in one run |
  | IPDramonFM | clear Hard with any form |
  | MetalGarurumon | reach run level 40 |

The run player holds its weapon's model (the socket's MODEL, POWERSOCKETNAME)
loaded at its GENERATE like a direct start (GameSystem::direct_start). When
the weapon changes family, the model in hand changes the way the original
equipment window changes it (an O1a check; fallback: the model stays until
the next stage load, the name and stats change at once).

## 6. Abilities

### 6.1 The build

The build is DW4's own equipment window of the run's player. The slots are
not equal on purpose:

| Part | DW4 place | In the run | Count |
| --- | --- | --- | --- |
| ∈ the Art | POWERSOCKET[0] | the form's weapon class, striking on its own; the weapon climbs the class's ladder (6.3) | 1 |
| Techniques | WAZALIST from TECH1-6 | the form's six, cast on cooldowns, learned and ranked by DW4's rules (6.4) | 6, + up to 2 extensions |
| ∋ Core | MAINSLOT | one core (armor1) | 1 |
| ∨ Board | SUBSLOT | one board (armor2) | 1 |
| ∧ weapon pocket | POWERPOCKET | one chip marked WU | 1, open from the start |
| ∧ core pocket | MAINPOCKET | one chip marked BLUEAB | 1, opens at the core's β (DW4: a device's rank opens its pockets) |
| Plugs | UPCNT | stat points, no slot | unlimited |

`recalc_player` turns the equipment into stats as in the story (combat.cpp
836-1020): the weapon's TSUYOSA into STR, the core into DEF, HP and
resistances, the board into its columns, the chips into crit, drain,
evasion, element and stats. The run adds only its rule values (H4).

### 6.2 Skill growth

DW4 raises a skill when a hit lands (BLTcalcSkillUp from the hit scripts,
`skill_up` in combat.cpp 702-745: a roll by the P2ESKILLUP table up to the
level's limit, level x 30 to level 36, then `recalc_player`, `store_species`
and the SQ_TECUP panel). Revision 1 fixed the skill points instead and did
not mention this; the values would have drifted, panels would have piled up
and the technique list hook would have fought the original. Revision 2 uses
it on purpose, with the run's rule in the run's engine only (H4):

- an **Art hit** adds 1 point to the class skill, 1 more on a critical hit,
  times the form's growth for that class / 100 (GROW 130 for the specialist
  class), kept as a fraction; no roll;
- a **blast technique hit** adds 2 points to the Blast skill; a **heal or
  force cast** adds 12 points to the Heal or Force skill (they cast rarely);
- the **limit** is not the level's but the run's **cap**: the class cap from
  Forge (6.3), the group caps from Train (4.6). A skill at its cap stops;
- `recalc_player` and `store_species` run as in DW4 (they write only the
  run player's rows); the **SQ_TECUP panel** shows only when something
  changes that the player sees: a new weapon rung, a new technique, a new
  rank. One panel at most every 30 frames;
- the rolls of the original are not used for the gain, so a run's skill is
  a pure function of its hits.

### 6.3 The Art

An Art is a set of the original's weapon hitboxes (the attack sequences'
HITBOX boxes of kind 10000, PCWA), attached to the player for their frames
without the attack animation, so the Digimon never stops walking (13.4, H6).
Each comes with a slash, swing or shot effect of the x_100 family and the
swing sounds (PC_SUBURI01-09, PC_SHOT01).

**Aim.** Every Art aims: it picks its target in its range (the nearest
enemy, or the densest group where the table says so), turns its pattern to
the target's bearing and, when the target is beyond the pattern's own
reach, shifts the pattern outward along that bearing so its reach ends at
the target. The range is at least 20 units (O1a: for a Digimon that walks;
standing it is 14 since the balance round of 05.11, a Digimon that never
walks must be reached, docs/OVERFLOW_DIRECTOR.md 10). The Digimon does not turn: in
this genre the player walks away from the enemies, and an Art that only
strikes forwards would never hit (revision 1's Cleaver reached z 4-17 and
Blade Arc z 8-14, in the walking direction only). A forwards-only variant
may come later as an option, not in O1-O3.

**Damage.** The original weapon formula `(STR x 6 + Skill x 4) / MAMORI`
with crit bonus by skill, where STR includes the socketed weapon's TSUYOSA,
times the Art factor 2.5 (initial; H4). Arts always hit (the original
weapon hit roll would make them miss). Example: Agumon at level 1 with
Battle Hawk α (STR 70 + 78, skill 1) does about 25 against a Goblimon
(MAMORI 90), whose warm-up HP is 30: one or two hits. Fire Petty does about
32 (`(INT x 10 + Skill x 5 + Power x 20) / SEISHIN x (1000 - RES) / 1000`
with Fire RES 400): one shot.

**The ladder.** The weapon in the socket is always the best weapon of the
Art's class whose SKILLPOINT the class skill reaches, among the families
the Forge has opened: DW4's own rule (`weapon_usable` /
`equip_strongest_weapon`, combat.cpp 1191-1235). For the axe: Battle Hawk α
1, β 30, γ 300, δ 510, ε 660; Van Blade α 810 ... ε 1300; Crusher 1400 ...
1920; Destroyer 2020 ... 2460; Soul Burn 2510 ... 2810. Forge 1 opens the
first family; each Forge offer opens the next one and adds the next
pattern step at once. The weapon's TSUYOSA grows from 78 (Battle Hawk α)
to 2590 (Soul Burn ε), so the climb is the Art's damage curve.

**Cooldown** = the class's DELAY x 2.2 frames (initial), -5 % per Forge.

| Art | Class (skill) | Pattern, aimed | Range | CD | Forge 2-5 |
| --- | --- | --- | --- | --- | --- |
| Cleaver | axe (Blunt) | the late line 6x8x6 from z 4 to 17 with the fan at its tip, laid on the nearest enemy; stun chance | 22 | 1.25 s | a second line on the next target, +20 % damage, side lines, a bigger fan |
| Blade Arc | sword (Slash) | the 5-box arc (x ±9) on the nearest enemy | 22 | 1.1 s | an arc on the next target, +damage, the return arc, +30 % size |
| Fang Thrust | dagger (Stab) | the thrust boxes at z 11 and 16 towards the nearest enemy | 24 | 0.95 s | a thrust at the next target, pierces 2, one more thrust, reach 30 |
| Crescent Draw | katana (Bash) | a wide arc (x ±12) on the densest group, high crit | 22 | 1.1 s | +crit, wider, a second arc 10 frames later, +crit damage |
| Quake Maul | maul (Blunt) | the 12x8x12 slam on the densest group; stun | 22 | 1.7 s | a shock ring r 20, a longer stun, ring r 25, ring r 30 and a second slam |
| Twin Edge | cutlas (Slash) | the two counter arcs of the dual swing on the nearest enemy | 20 | 1.25 s | +damage, a pair on the next target, +damage, a third pair |
| Saber Lance | saber (Stab) | the 4-box arc, then the lance of 3 boxes, along the bearing to the nearest enemy | 24 | 1.1 s | lance to 30, a second lance (±20°), +damage, two more lances |
| Claw Rush | arm (Crush) | the two arcs of the arm swing on the nearest enemy; knockback | 20 | 1.55 s | stronger knockback, a second pair, +damage, a shock at the end |
| Blaster | gun (Shot) | the gun shot effect x_100gunv01 at the nearest enemy | 40 | 1.25 s | one more shot, pierces, one more shot (spread), two more shots |
| Glaive Wheel | glave (Bash) | the Spiral ring (kind 30000 boxes 8³ at radius about 9, 46 frames) around the player | ring | 1.4 s | longer spin, radius 12, +damage, 60 % uptime |

The patterns are those of p_agumon's sequences; O1a takes each form's own
boxes (other forms' may differ slightly). A Jump Strike Art (the gun
class's jump attack, kind 20000, MUTEKI 18) would move the player and give
free invulnerability. It is left out on purpose.

### 6.4 Techniques

**Firing.** A technique fires the original's own effects with the action
broadcast that the quick panel's SQ_WAZA plays, but without the cast
animation: the director sends `ACTION_BROADCAST <action>` with strings
{"p_<form>"} and ints {slot}. That was verified with HFIRE1, HICEE1,
HTHDR1, HDARK1, CUTER1, BOMBB1, VENOM1, IMPLS1 (staggf04: 180 broadcasts,
32 defeats through the original hit scripts) and with the weapon specials.
Damage is the original technique formula (always hits) with the rank's
pcTech row. The cast sound the quick panel would start
(SoundManager::action_started) is played by the director: plMAGIC1,
plHEAL01, plMIND01, plVENOM or plIMPACT.

**Learning and ranks: DW4's own rules.** `recalc_player` builds the
technique list (WAZALIST) from TECHn_LEVEL and the group skills. A technique
is learned when the run level reaches its lock and the group's skill
reaches TECHn_SKILL1; its rank is the highest k whose TECHn_SKILLk the
skill reaches. The run changes one value (H8): the lock is TECHn_LEVEL x 2/3,
rounded up (DW4's 1 / 12 / 22 / 23 / 33 / 34 / 44 become 1 / 8 / 15 / 16 /
22 / 23 / 30), because a run ends around level 40. Agumon, for example,
learns Fire at once, Pwr Up at level 8, Cutter at level 8 once Blast reaches
331, Heal Lt at 15, Bomb from level 16 once Blast reaches 661 and Resist
from level 16 once Force reaches 661 (Pwr Up casts and Train Force).

**Extensions.** Up to two techniques from outside the form's six, opened by
the Extension I and II records (8.4) and picked in the character select from
the techniques of forms that survived 10:00. They are learned like TECH5
(lock 16, skill 661).

**Targets.** The shells home on their own (the original AIH). Bomb, Venom
and the debuff pulses aim at the densest group within 40 units (the cell of
a 5-unit grid with the most enemies around it). Cutter aims at the nearest
enemy. The heal and force techniques act on the player.

| Technique | Group | Action(s) | In the run | Ranks Petty..∞ | CD |
| --- | --- | --- | --- | --- | --- |
| Fire | Blast (fire) | HFIRE1-5 | 1-5 homing shells; a hit splashes 30 % in 8 units | shells = rank, power 160-800 | 2.5 s |
| Ice | Blast (ice) | HICEE1-5 | 1-5 shards; hit enemies slow 30 % for 2 s | as Fire | 2.5 s |
| Bolt | Blast (thunder) | HTHDR1-5 | 1-5 bolts; each jumps to one more enemy at 50 % | as Fire | 2.5 s |
| Dark | Blast (dark) | HDARK1-5 | 1-5 orbs; hit enemies lose 10 % DEF for 4 s | as Fire | 2.5 s |
| Venom | Blast (poison) | VENOM1-5 | a poison ball at the densest group, a pool that ticks every 2 s (power / 5) | pool 4-10 s | 3.0 s |
| Impact | Blast | IMPLS1-3 | rings around the player, knockback | 1-3 rings; Terra / ∞: IMPLS3 with +25 % / +50 % radius | 2.0 s |
| Cutter | Blast | CUTER1-5 | 1 / 1 / 3 / 3 / 5 piercing beams in a fan | as the original | 2.5 s |
| Bomb | Blast (fire) | BOMBB1-5 | a bomb at the densest group, a burning area | as the original | 2.0 s |
| Heal Lt | Heal | HEALS1 | heals the rank's amount, at most 20 % of MaxHP; costs MP | | 12 s |
| Aura | Heal | HEALA1 | a regeneration field of radius 30 for 5 s; costs MP | | 15 s |
| Cure Lt / Cure | Heal | CURES1 | removes poison and the other ailments, 3 s immunity; costs MP | | 10 s |
| Raise | Heal | RESTO1 | no cast: the revive of 4.8 (Petty 25 % HP, Giga 50 %, ∞ two revives); costs MP at the revive | | - |
| Speed | Force | SPEDU1 | the original buff: +move speed, +evasion, -10 % cooldowns while on | 6-10 s | 15 s |
| Resist | Force | DSILD1 | the original buff: +all RES; -10 % damage taken while on | 6-10 s | 15 s |
| Pwr Up | Force | POWEU1 | the original buff: +STR, +DEF, +MaxHP while on | 6-10 s | 15 s |
| Freeze | Force (enemies) | SPEDD1 | a pulse of radius 30: enemies slowed | | 8 s |
| Pwr Dwn | Force (enemies) | POWED1 | a pulse of radius 30: enemies weakened | | 8 s |
| Sleep | Force (enemies) | SLEEP1 | a pulse of radius 30: enemies fall asleep (chance by rank) | | 10 s |
| Bind | Force (enemies) | BINDD1 | a pulse of radius 30: enemies cannot move | | 10 s |

The force buffs are the original status effects with their icons
(stdownic: Speed, Resist, Pwr Up; on enemies: Freeze, Pwr Down, Sleep,
Bind). Cooldowns are initial values; Speed Chips and the Speed buff shorten
them.

As built in round O1b (lane E's finish): the buffs keep DW4's own times
(pcTech TIME 1800-3240 frames, 30-54 s by rank, plus the caster's INT for
Speed and Pwr Up), not the 6-10 s above: with 6-10 s the kite bot's Agumon
fell at 16-22 minutes as early as the newcomer's (docs/ROUND_O1B_E_NOTES.md
12.1; the cap stays as the balance key `buff=360,60`). Pwr Up's cooldown is
15 s until 10:00 and 120 s after: with 15 s throughout it never ended
(Agumon's TECH2 from run level 8: +80 to +1920 STR, DEF and MaxHP) and the
newcomer's Agumon reached the finale in 15 of 20 runs; 60 s from the start
ended 6 of 37 newcomers' runs right after the phase 2 start, and 60 s from
10:00 still kept it on (Giga to ∞ last 42-54 s plus INT).

### 6.5 Awakening

When the class skill reaches 2500 with the fifth family forged, the form's
signature weapon becomes usable by DW4's own rule (its SOUBI bit is the
form's, its SKILLPOINT is 2500) and `equip_strongest_weapon` puts it in the
socket: Judgment for Agumon, Light Bringer for Veemon, and so on (5.1). No
partner passive, no key item, no extra offer: the climb is the condition.

The awakened Art keeps its aim and gets the class's best pattern at once,
the x_100charge01 burst on every hit, and the signature's own stats through
`recalc_player` (higher crit, faster DELAY, the INT / MND / DEX the
signatures carry). The weapon model in hand becomes the signature's (w_*06 /
w_*07), the SQ_TECUP panel, plLVUP01 and an "AWAKENING" line show it. With
the gain of 6.2 a Rookie reaches it around 14-17 minutes (estimate), a Mega
start (skill 810) a few minutes earlier.

As built in round O1b (lane E): x_100charge01 is the original's charge
effect, started only by the player's charge actions of SQ_ATCK1 (KATC?1:
the charge glow, SQ_<class>01 / 02; KATC?2: the release, SQ_KIRIKAE and
SQ_OWARI on the player). The awakened Art broadcasts its class's release
(KATCX2 for the axe, KATCS2 for sword and katana, KATCE2 for the dagger)
once for every strike that hits: a picture only, no damage of its own.

As built for Guilmon and Dorumon (round O1b lane E, 6.3): Fang Thrust's
Forge 3 lays two more boxes beyond the tip (the thrust goes on through its
target: "pierces 2"), Forge 4 is one more thrust, Forge 5 reaches 30
standing; Crescent Draw's first arc goes to the target with the most living
enemies within 10 units. Its wider arc and its crit steps, and the 22-24
unit ranges of 6.3's table, wait for O2 (every Art reaches 14 standing and
34 walking since the balance round of 05.11).

## 7. Gear

Gear is DW4's own: rows of armor1, armor2 and abChip, equipped by DW4's
rules. Only what a run needs is changed (the core's LVPOINT counts run
levels, boards have only their α row, chips power up with ¶ Data at a
twentieth of the original cost).

### 7.1 Cores (∋, MAINSLOT)

| Core | α (level 1) | β (run level 22) | γ (run level 37) | Opens |
| --- | --- | --- | --- | --- |
| Brave Core | BOUGYORITSU 2, DEF +45, MaxHP +50, Speed +40, Fire RES +45 | 3, +180, +100, +80, +90 | 4, +432, +160, +128, +144 | start |
| Cipher Core | stun resistance, DEF +35, MaxMP +45, INT +40, MND +50, Dark RES +50 | doubled | about x 3 | Gate Keeper |
| Element Core | poison resistance, DEF +40, MND +40, all RES +100 | +160, +80, +200 | +384, +128, +320 | Mammoth Data |

BOUGYORITSU is the chance that a physical enemy attack misses outright
(combat.cpp 227). After γ, a core grows by value (+30 / +60 / +120 / +240),
which DW4 adds to its DEF. The core pocket opens at β.

### 7.2 Boards (∨, SUBSLOT)

Boards have only their α row in a run and grow by value (+30 / +60 / +120 /
+240 / +480), which DW4 adds to the column the board names
(SHUSEINOURYOKU).

| Board | α | Value goes to | Opens |
| --- | --- | --- | --- |
| Armor Board | DEF +90, MaxHP +60, Speed -35 | DEF | start |
| Fire / Ice / Bolt / Dark Board | that RES +150, small DEF, HP, INT | that RES | start |
| Atlas Board | STR +60, DEF +45, MaxHP +45, Speed -30 | STR | Ten Minutes |
| Achilles Board | STR +45, DEF +45, Speed +60, INT / MND -15 | Speed | Thousand |
| Beast Board | STR +90, Speed +60, INT / MND -35 | STR | First Awakening |
| Mana Board | MaxHP, MaxMP, INT, MND +60, STR / DEF -60 | MND | Caster |
| EXP Board | EXP +10 %, Speed +90, STR / DEF -35 | Speed | Run Level 25 |
| BIT Board | bits +10 %, DEF +90, INT / MND -35 | DEF | Banker |
| Rack Board | STR +30, DEF +60 (luck) | DEF | Crate Breaker |
| Resist Board | all RES +90 | DEF | Shogun Data |

### 7.3 Chips (∧)

| Chip | Pocket | Effect (α) | Steps | Opens |
| --- | --- | --- | --- | --- |
| Wisdom / Spirit / Speed Chip | both | INT / MND / Speed +30 | power up with ¶ Data (α+30 ... α+240: +270), then β (+120), γ (+240, up to +720) by offers | start |
| ⊂ ⊃ ∪ ∩ PWR Medal | weapon | STR +30 and the weapon's element (DW4's ATTACKZOKUSEI: up to +100 % against a weak RES) | ¶ Data, then β, γ (up to +720) | the form's element: start; the others: <Element> Adept |
| Drain Ram | weapon | HPDRAIN 4 % of damage dealt (at most 2 % of MaxHP a second) | β..ε by offers (to 12 %) | Blossom Data |
| Dread Ram | weapon | MPDRAIN 4 % | β..ε by offers | Diaboro Data |
| Assault Ram | weapon | crit +8 | β..ε by offers (to +16) | Skull Data |
| ⊂ ⊃ ∪ ∩ Resist ROM | core | that RES +30 | ¶ Data, then β, γ (up to +720) | Shogun Data |
| Defence ROM | core | evasion (CHOHATURITSU) 8 | β..ε by offers (to 16) | Scorpio Data |

DW4's rule "each value is raised by one chip per list" holds; the weapon
pocket and the core pocket are separate lists.

### 7.4 Plugs

A Plug (offer or pickup) adds UPCNT points to one stat for the run: 10 + 2
x run level (initial), so a plug stays worth taking late. The stats are
DW4's plug stats: Strength, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed and
the four element resistances. The Plug Lab's points (8.2) come on top.

### 7.5 ¶ X Data

¶ Data pickups fill the run's data count, and DW4's rule "Power-up with ¶
data" spends it: whenever the count reaches the NEXTX of a chip in a pocket
that can power up, divided by 20 (Wisdom Chip α: 3, 6, 120, 240), that chip
steps to its next record (weapon pocket first). The step shows the SQ_TECUP
panel and the chip's new name. Chips that DW4 marks "Cannot power-up with
¶" (the Rams, Defence ROM) step only through Chip offers. Data left at the
end of a run is gone.

## 8. Meta progression

### 8.1 Sim Bits

Sim Bits come only from runs (4.9). They never reach the story's BIT and
the story's BIT never reaches them. The Data Records screen shows the total
earned; the Plug Lab shows the balance.

### 8.2 The Plug Lab (permanent plugs per form)

DW4 keeps plugs per form (BASE_UPCNT[16][11]) and sells them in the X-Data
shop. The Plug Lab does the same with Sim Bits, for the run only:

- per form and per stat (Strength, Def., MaxHP, MaxMP, Wisdom, Spirit,
  Speed): 10 steps of +5 UPCNT;
- the price of step n is 100 x n Sim Bits (initial); there is no refund;
- a run uses the Plug Lab points of the form it starts with (on a line they
  stay with the run after a digivolution).

### 8.3 The starting loadout

In the character select the player picks **one armour** (a Core or a Board)
and **one chip** among the opened ones (8.4). They start in their slots at
α: the armour in its slot, the chip in its pocket (a BLUEAB-only chip
needs a Core as the armour, because the core pocket comes with it, opened
for the loadout). Nothing is bought: opening an item through a record
opens it for the loadout and for the offers at once.

### 8.4 Data Records (the unlock tree)

Records are checked when a run ends (and the in-run ones at once, shown with
"NEW!" in the results). Forms are in 5.3.

| Record | Condition | Opens |
| --- | --- | --- |
| Gate Keeper | defeat any Guardian | Cipher Core |
| Blossom Data | defeat Blossomon | Drain Ram |
| Mammoth Data | defeat Mammothmon | Element Core |
| Skull Data | defeat SkullGreymon | Assault Ram |
| Scorpio Data | defeat Anomalocarimon | Defence ROM |
| Shogun Data | defeat TonosamaGekomon | the Resist ROMs, Resist Board |
| Diaboro Data | defeat Diaboromon | Dread Ram |
| Ten Minutes | survive 10:00 with any form | Atlas Board |
| Fifteen Minutes | survive 15:00 with any form | the ?????? offer |
| First Clear | defeat LordKnightmon (clear Normal) | Hard, Extension I, Endless |
| Hard Clear | clear Hard | Very Hard, Extension II |
| Run Level 25 | reach it in a run | EXP Board |
| Thousand | 1,000 defeats in total | Achilles Board |
| <Element> Adept | 300 technique defeats with Fire / Ice / Bolt / Dark | that element's PWR Medal |
| Caster | 500 techniques cast in total | Mana Board |
| First Awakening | awaken a signature weapon | Beast Board |
| Banker | 5,000 Sim Bits earned in total | BIT Board |
| Crate Breaker | 100 crates broken in total | Rack Board |
| Rookie Mastery | clear Hard with a Rookie | Talis of Chaos among the ?????? results |
| Very Hard Clear | clear Very Hard | Talis of Order among the ?????? results |

### 8.5 The file

`saves/overflow.dat` beside the program, like `saves/profile.dat`: a text
file written with `dw4::save::write_file_atomic` (temporary file, flush,
read back, rename, a `.bak` only of a complete file; achievements.cpp 323 /
382 is the model). Unknown lines are skipped, so later versions can add
lines. Test runs write only where `DW4_OVERFLOW_PROFILE=<path>` says and
never to the real file. It is written when a run's results open, after a
Plug Lab purchase, after a loadout change and after the first meeting. It
is not part of the cloud backup or the online character.

```
DW4OVERFLOW 1
seen_intro 1
sim_bits 3420
earned_total 18950
plug p_agumon str 4 def 2 mhp 3
loadout p_agumon armor1 0 chip 105
form p_agumon runs 14 clears_normal 1 best_time 21:12.4 best_level 41 best_defeats 3511
form p_vmon runs 3 best_time 11:42.3 best_level 27 best_defeats 1290
record gate_keeper 1
record blossom_data 1
recorded_tech p_agumon
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
  i_stdigiface), the quick panel list (i_wzslbase00, i_wzslcursel) and the
  equipment window's socket and pocket lines (SQ_<n>LINEOPEN, subwnd.cpp
  113-119).
- **Text:** the game font (bectext.igb, `layout_text`), white text, the
  name tab yellow (as TALKFONT03: 0.9 / 0.9 / 0.47), only the glyphs the
  game's own texts use: ASCII and ∈ ∋ ∨ ∧ ¶ ⊂ ⊃ ∪ ∩ ⊆ ∞ ↑ ↓ α β γ δ ε. A test
  checks every string against the font's character list (T9).
- **Icons:** the item glyphs (∈ weapons, ∋ cores, ∨ boards, ∧ chips, ¶ X
  Data, ⊂⊃∪∩ elements), the face sheet (charaicon: 4 x 4 faces of 64 px, in
  the order agumon, vmon, girumon, dorumon, angelr, alpha, beelzeb, hekabut,
  weregaru, wargrey, impdrafm, impdrapm, dukecrim, susanoo, metalgaru,
  bwargrey; round 64), the status icons (stdownic00-10).
- **Colours:** DW4's value colours (white, cyan, green, yellow, orange,
  magenta), the HUD's purple line.
- **Sounds:** WINDOW_OPEN / CLOSE, CURSOR_MOVE / DECIDE / CANCEL / NG.
- **Layout:** the 2D layer is 640 x 480, centred, y up; widescreen keeps it
  4:3 like the game's own layers.

In O1a every screen is a plain overlay box with the game font (no window
models yet); O1b brings the window models.

### 9.2 Lobby screens (over the story engine)

1. **Talk** (3.4).
2. **Character select** ("Choose a Digimon"). The status window frame, a
   4 x 4 grid of faces (locked faces dark with "?"), the cursor on the
   faces. Beside it: the name, the shinka EXPLANATION line ("Blunt Skills
   Specialist  MP Skills: ↑Fire, ..."), the Art and the signature, the six
   techniques with their glyphs, the trait, HP / MP / STR / INT at level 1,
   the best time and level. For a locked face, its Data Record condition.
   Then lines with the status window's left / right arrows
   (i_stcursorleft / i_stcursorright): "Armour: ∋ Brave Core α", "Chip: ∧
   Wisdom Chip α", "Extensions" (once opened), "Level: Normal" (or Hard,
   Very Hard, Endless). Last, the stage change window (i_zzstchgwn look):
   "Enter the simulator?  Yes / No".
3. **Plug Lab**: the shop list look (the form's face, the seven stats with
   their steps as a bar, the next price; the Sim Bits at the top). A buys,
   B leaves, L / R change the form.
4. **Data Records**: two pages. *Records*: every record, done (white with
   its date) or open (grey with its condition). *Digimon*: per form, runs,
   best time, best level, clears per tier.

### 9.3 HUD in a run

- The original player HUD (i_stpnl01..05 at V<-260,-160>: face and HP
  ring, LV, HP / MP numbers, the attack delay ring as the Art's cooldown).
  The run's player 1 slot is the only one, so the other three places stay
  empty, as solo.
- Host overlay in the game font:
  - the **run timer** top centre (mm:ss); for the finale the DW4 quest timer
    model counts down instead (4.4);
  - the **EXP bar**, a thin bar in the HUD's purple under the top edge;
  - **Defeated** count and **Bits** top right;
  - the **build row** above the HUD: the weapon's name in its value colour
    with a thin bar to the next rung, the six technique glyphs (element
    glyph or the technique's initial) with their rank (P, M, G, T, ∞;
    learned ones bright, locked ones dark), the ∋ / ∨ / ∧ glyphs of the gear;
  - the **Guardian bar** where the arena has none.
- Damage numbers are the original's (i_dmgnum, 11 RAMs; more simply do not
  show). F1 > Gameplay can hide them in runs.

### 9.4 Level-up window

The run freezes (13.5) and the quick panel list opens in the middle:

```
 LEVEL UP!   Lv 17
 ┌──────────────────────────────────────────────────────┐
 │ ∈ Forge: Crusher        Blunt cap 1920, side lines    │ (white)
 │ ⊂ Train Blast           cap 750 > 1250, Blast +100    │ (white)
 │ ∧ Assault Ram β         crit +10                      │ (white)
 └──────────────────────────────────────────────────────┘
  A Take    (one line of text about the cursor's offer)
```

(The box lines stand for the window model; the game draws its own frame.)
The cursor sound on moves, CURSOR_DECIDE and plLVUP01 on taking.

### 9.5 Pause

START opens it (frozen): "PAUSE" and the list *Resume*, *Build*, *Retire*.
*Build* shows the run's equipment in the equipment window's look (the
socket with the weapon and its pocket, the core with its pocket, the
board), the six techniques with their ranks and skills, and the run's
numbers (time, level, defeats). *Retire* asks "End the simulation? Yes /
No".

### 9.6 Results

After the Game Over picture, TIME UP or the clear banner: the status window
frame with "SIMULATION COMPLETE" or "SIMULATION ENDED", then in lines that
appear one after another (a cursor tick each): time, run level, form(s),
defeats (total and the top five families), the damage of each ability (top
six), the highest hit, the build, Sim Bits, and the new Data Records with
"NEW!" in yellow. "A: Return to the lobby." The screen fades and the lobby
comes back with Gekomon's comment (3.4).

### 9.7 Texts

All strings of the mode live in one table (`src/game/overflow_text.inc`):
dialog pages, menu lines, the names of the Arts, traits, Guardians and
families, the help pages. The weapon, core, board, chip and technique names
are the game's own (the DB's NAME columns). The enemy families have no name
table in the game. The names are ours, and some are uncertain until O1b's
hidden screenshots: Goblimon, Numemon, Sukamon, Ogremon, Kokatorimon,
Mummymon, Minotarumon, Pharaohmon, Tyrannomon, Otamamon, Gekomon,
MarineDevimon, Tortomon (e_torta?), Infermon, Megadramon, e_rare, e_mecha1-5
(MRα01 / MRβ02 / MRγ03 / MRδ04 / MRε05?).

## 10. Controls

Default (keyboard and controller bindings as set in F1), **walking only**:

| Where | Input | Does |
| --- | --- | --- |
| lobby | A at the NPC | talk (as with every NPC) |
| dialog and menus | stick / D-pad, A, B | move, choose, back |
| run | main stick | walk (the original's walking and turning) |
| run | A | picks up a labelled item (disks); nothing else |
| run | L | the original item panel (use HP / MP disks) |
| run | B, X, R, Z | nothing |
| run | START | pause |
| level-up | A | take |

Everything that attacks is automatic. The DW4 buttons would undo the genre:
the guard turns every hit into SQ_GUARDR without damage while Arts and
techniques keep firing (an unbreakable tower), and A's combo stops the
Digimon (SQ_ATCK1: MOVE Stop). The **classic buttons** stay an option
(question 4): A the form's own attack combo, B the guard with the run's
limits of 4.8 (50 % damage, 3 s lockout), X the jump. Whether the jump has
invulnerable frames is checked in the O1a spike; if it has, the run gives X
a 2 s cooldown.

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
| Guardian | BT_SBOSS; after its defeat the phase music again |
| finale | BT_BOSS2; QS_HURRY in the timer's last minute; z3COUNT1 / z3COUNT2 on its last three seconds |
| clear | BT_WIN02 |
| defeat, TIME UP | QS_DONOT |
| level-up, pickups | plLVUP01, ITEM_GET |
| weapon rung, new technique or rank, Awakening, chip power-up | the skill-up panel's own sound with SQ_TECUP; plLVUP01 for the Awakening |
| hits taken | PC_DMG01 (normal enemies), the original reaction sounds (Guardians), EFX_GUARD on a miss |
| Arts | PC_SUBURI01-09 by pattern, PC_SHOT01, EFX_HIT_01, the element hits htFIRE01 / htICE01 / htTHUN01 / htDARK01 |
| techniques | the director's cast sound (6.4) and the effects' own sounds |
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
rendering, 30 more enemies +0.04 ms. The CPU is not the limit; the RAMs per
family are, and section 4.2 keeps them in reserve.

| Budget | O1 | Stretch |
| --- | --- | --- |
| enemies alive | 60 (cap of A(t)), 72 in a surge | 90 in Endless |
| RAMs in the pool | 61 / 94 / 132 / 166 by phase (4.2) | |
| Guardians | 1 at a time | |
| crates | 8 | 12 |
| pickups per model | 8 (RAMs) | |
| player effects | the RAMs per effect (x_200fire01 24, thunder01 20, cutter01 20, bomb01 12, impulse01 12, x_100slashv01 16, gunv01 12, armv01 8, the other x_100 4); a cast that finds no RAM waits a frame | |
| simulation step | p95 ≤ 2 ms, max ≤ 6 ms | |
| traverse + draw list | p95 ≤ 1.5 ms, ≤ 400 draw items | |
| presented frame | ≤ 8 ms at 2560x1440 on the documented PC | |
| run engine memory | ≤ 600 MB working set with all 21 families and the phase's Guardians loaded on one arena (O1a measures it; a lobby engine is 32 MB, staggf04 + 5 donors 80 MB). From O3 each arena loads only its phase's pool | |
| run start, field change | ≤ 3 s (the run's engine always loads fast) | |
| back to the lobby | ≤ 1 s (the story engine never left memory) | |

## 13. Technical plan

### 13.1 Two engines

```
 game_main (host)
 ├─ presentation, input, F1 panel: functions on an Engine& (13.3)
 ├─ story engine (engine_storage, as today)
 │    lobby: steps with neutral pads while the dialog is open
 │    run:   not stepped (frozen like the F1 pause), kept in memory
 ├─ Overflow host (src/game/overflow_host.*): the NPC, the screens, the run loop
 │    ├─ NPC actor + SimUi overlay (presentation only, 13.6)
 │    └─ run engine (std::optional<Engine>, only while a run lives)
 │         ├─ OverflowRules (the hooks' values and native scripts, 13.4)
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
| the system random generator (`fight::generator()`, the one static of combat.cpp 36-38; every Engine constructor reseeds it, engine.cpp 603) | `std::ostringstream << fight::generator()` before the run's engine is made, `>>` back after it is destroyed and before the story engine's next step (as StageSnapshot::random). The run's engine reseeds it with the run's seed after construction |
| other process-wide state | the audit in O1a lane A of every mutable static in `src/engine` and `src/*.cpp`. Known and harmless: the Lua identity counter, `rams_generation`, trace caches, the randomizer's text cache; the SNDF random state belongs to each instance; play time counts only online; the memory card store belongs to each engine. Done when the list is in the code review and T3 passes |
| story steps | none during a run; neutral pads during the dialog (in the pad log, so save states replay) |
| the story engine's state | no send, demo, model, sound or flag write ever; the NPC and the screens are host-side |
| the talk | the observe-only port event `talk_box` (13.4, H0); collecting port events while the NPC is shown changes nothing in the simulation (T2 runs r25c with it on) |
| story save, card folders, randomizer.ini, profile.dat | the run's engine has a card store on an empty throwaway folder, both slots CardSetting `none`, no Gate Disk (NDMWPANEL NOGATEDISK); the mode writes only overflow.dat |
| save states | F5 / F9 refuse in a run ("Not in the simulator"); can_save (game_main.cpp 1616) learns the reason |
| achievements, Discord | the run engine's port events stay off (the director has its own notifications, H5), so the AchievementTracker never sees a run; `talk_box` is taken out before the tracker sees the story engine's events; Discord shows "Data Overflow - <form> Lv <n> - <mm:ss>" |
| online | no NPC online; the F1 Online tab and title entry are out of reach in a run |
| renderer and smooth motion | `renderer->clear_cache()` (texture cache by document pointer) and an interpolator reset at both switches and at a field change; labels reset |
| pacing | the step accumulator and clock are reset on the return, so the story does not catch up the run's time |
| audio | one engine is mixed at a time; the UI sounds go through a second SDL audio stream |
| test variables (DW4_GOD, DW4_LEVEL, DW4_FORMS, DW4_DEVICES, ...) | they act on every new engine; O1a lane A finds where each is applied and keeps them out of run engines unless `DW4_OVERFLOW_TEST_CHEATS=1` |
| the original story cheats and randomizer | not inherited; the run's cheats are all off except fast loading |
| native script handlers (H10) | they live in the run engine's OverflowRules; every other engine has none |

### 13.3 The host loop

game_main's `run()` is one function of about 3,550 lines (game_main.cpp
563-4116). The presentation is written into it: the interpolator
(2829-2985), the render call (3009), keep_picture (3067), the film and
labels, and the ImGui F1 panel (3120). There is no host context to hand to
a second loop. Revision 1 planned a separate run loop that would have
copied hundreds of those lines, and texture packs, widescreen and the F1
panel would have drifted apart.

So O1a lane A starts with a **mechanical extraction**, without any change of
behaviour: the presentation (interpolate, traverse, draw list, render,
keep picture, film, labels), the input (keyboard and gamepads into pads)
and the F1 panel become functions that take an `Engine&` and a `HostView&`
(window, renderer, interpolator, labels, audio stream, settings). The main
loop calls them with the story engine exactly as before. Proof (T11):
det.sh unchanged and pixel-identical hidden screenshots in three scenes
(title, the lobby, a dng101b1 fight), each compared before and after.

Then the run gets its loop, `overflow::run(HostView&, Engine& run)` in
`src/game/overflow_host.cpp`, called between two passes of the main loop.
It calls the same functions with the run engine and adds only its own
pacing state (the main loop's fixed 59.94 Hz step, at most 4 steps a pass)
and the SimUi overlay. It returns when the results close, or at once when
the window closes (the run counts as retired, the meta file is written,
then the program quits as usual).

The switch:

1. The dialog's "Yes" (in O1a: the dev entry): the overlay fades to black
   over 30 frames (the story still steps with neutral pads; the host's
   master gain fades too).
2. Save the generator state. Create the run engine on a copy of the mounted
   resources, with the mods (`set_resource_overrides`), the card store as
   above, cheats off except fast loading, the OverflowRules, port events
   off. Set GameSystem::direct_start, players[0].digimon = "p_<form>",
   NINZU 1, apply the loadout and the Plug Lab points, then
   `request_stage(arena)`.
3. Run loop until the results close.
4. Destroy the run engine, restore the generator, clear the renderer cache,
   reset the interpolator, labels and pacing, set `pad_latch`, fade the
   lobby picture and the gain back in, open Gekomon's "Welcome back" page.

### 13.4 Engine hooks

All hooks act only while the engine has OverflowRules (a pointer set by the
host before the first `request_stage`; null in every other engine, never in
a snapshot or the fingerprint). With the pointer null, each hook is one
untaken branch, so det.sh and r25c cannot change.

| Hook | Where (main 870a416) | In the run's engine |
| --- | --- | --- |
| H0 talk_box port event (the only change that runs in the story engine, observe-only) | model_ctrl.cpp 1411, port_events.hpp; game_main.cpp 2448-2452 and 1510 | sends `talk_box {slot, centre}` when the main lobby's A pushes the talk box; nothing reads it inside the simulation. The host collects port events also while the NPC is shown and takes `talk_box` out before the tracker and the online client get the list (3.3) |
| H1 stage start | engine.cpp 1854 `run_script("first.lua")` | answered by the field's native start (H10) |
| H2 donors | engine.cpp 1775; randomizer.cpp 311 `load_enemy_donors`, 365 `route_donor`; managers.hpp 391 `add_donor_sounds` | `load_overflow_donors(families, guardians, streams)`: the phase's explicit list (4.2), also on stagbs* / staggf* (no `fixed_stage` lock), no 6-family pool limit, Guardian families without their ded_ scripts, the BGM / jingle stream entries of named stages |
| H3 exits | `request_stage`, STAGECTL NEXTSTAGE, STAGECTL reset (soft reset, managers_core.cpp 1044, `soft_reset_allowed`), the console reset (`set_reset_enabled`), end_g_000bcaps.lua's game over | no stage change except the director's own field changes (4.1), no reset: the director gets `run_end{reason}` |
| H4 rule values | lua_host.cpp 607 (damage dealt), 726 (damage taken); combat.cpp 964 (NEXTEXP), 1092 (EXP), 1112-1113 (the level-up refill), 702-745 (`skill_up`); items.cpp 103 / 231 (pickup radius); the player's walk speed; BLTisHitCheck | per bullet tag: the Art factor and always-hit Arts, technique and trait factors; damage taken with the 35 % clamp; the run curve; the level-up rule (fractions + 3 %); the skill rule of 6.2 (gain, caps, panel only on a visible change); the heal group's MP |
| H5 notifications | next to the `port_event` calls for enemy_defeated, player_defeated, level_up, item pickups | the director's own calls (deterministic, independent of port events) |
| H6 port hitbox | ModelCtrlManager (the ModelRam::Hitbox list of model_ctrl.cpp 1409) | `port_hitbox(ram, Hitbox, tag, yaw, shift)`: the Art's boxes on the player for their frames, kind 10000, without a sequence, turned to the target's bearing and shifted to it (6.3); hits run CHKBULLET and the hit scripts as usual |
| H7 camera | the box camera (802F2BC0 logic) | an optional distance / height factor for the run (spike in O1a: is the original view wide enough?) |
| H8 run player | `recalc_player` (combat.cpp 836-1020), `load_species` | the technique level lock TECHn_LEVEL x 2/3 (6.4); a core's LVPOINT read as run level, boards' α rows only (7); at a digivolution the run's level, EXP, skills and UPCNT go into the new form's BASE row before `load_species` (4.1) |
| H9 director step | the end of `Engine::step` | `director.step(engine)` once per step, after the managers |
| H10 native run scripts | lua_host.cpp 107 `Engine::run_script` | in a run engine `rules->script(name)` is asked first; a native handler answers `hit_p_<form>.lua` (the player hit rule of 4.8) and `first.lua` (the field's start); every other script runs as in DW4 |

**Why H10 is native.** The critique proposed overriding `hit_p_<form>.lua`
and `first.lua` through the resource overrides. That does not reach them:
`MountedResources::override_for` (resources.cpp 221-234) replaces whole
archive entries, and these scripts are members inside each stage's package
(stagBS02 member 1 has hit_p_* for all 16 forms); and the port reads the
original binary chunks only and does not compile Lua text (LUA4.md, line
6). So the run engine answers those two names natively. The handlers call
the same host functions the scripts call (`BLPgetDamageValue`, `MDLaddHp`,
`MDLdspDamageP`, `BLPgetDamageRank`, the status getters, the HUD sends),
with the run's changes of 4.8 on top.

**The native field start** replaces revision 1's short command list, which
missed stage setup. stagBS02's first.lua, for example, does, besides the
boss: BGMSTOP, NDMWPANEL NOGATEDISK I<1>, LAYERCTL WN00/CLR then WN00/SET,
CAMERA SETBOXCAMERA, i_fadewn01 SQ_BLIN60, PLAYER SQ_WAIT, STAGECTL
PADINPUT ENABLE, the HUD panels i_stpnl00..05 (SQ_OPEN / SQ_OPENWN / SQ_WAIT
for joined players), SWITCHCTRL ALLON and `funcFirstLayerSet()` (func.lua:
LAYERCTL REMOVE LOADING, LAYERCTL SWITCH ALL/DISPON). The native start of
each arena is that list line for line without the boss lines (the boss BGM
and ambience, SETDEMOCAMERA entry, GENERATER START GENE-00, the boss's
SQ_WAIT, the boss bar's SETMODELHP), plus the run's own: the phase music,
MATCTRL PAUSE for scripted mats, GENERATER STOP / RESET. T10 compares the
stage state after the native start with the original first.lua's.

### 13.5 The director

`src/engine/overflow/` in dw4_engine (deterministic and testable without a
window): `director.cpp` (clock, phases, freeze, field changes),
`spawner.cpp`, `arts.cpp`, `techniques.cpp`, `offers.cpp`, `gear.cpp`,
`pickups.cpp`, `guardians.cpp`, `player_hit.cpp` and `field_start.cpp` (the
H10 handlers), `tables.inc` (the design tables of sections 4-8 as code),
`run_log.cpp`.

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
  randomizer) for spawns, offers, Guardians, appraisals and drops; the
  engine's hit, crit and damage rolls use the system generator, reseeded
  per run. The skill gain has no roll (6.2).
- **Choices** (offers taken, pause, Retire) come from the host as commands
  for a given step. The run log keeps the seed, the form, the loadout, the
  tier, the meta state used, the pads of every step and the commands, so a
  run replays exactly (tests, later perhaps watching a best run again). The
  log also keeps per minute: level, defeats, hits taken, the frames without
  control, the highest hit taken in % of MaxHP, the clamp count, the
  median hits per kill of the Art, and per family the share of enemies that
  closed in (14.2).

### 13.6 NPC actor and SimUi overlay

- **Presentation actor core.** The round 60 ghost figures (branch
  `feat-r60ghosts`, `src/game/ghost_figures.*`, not in main yet) already
  load a model package on a worker thread from a copy of the resources,
  keep it in their own scene cache, traverse and skin it with the engine's
  own code at the present's time, add the draw items after the
  simulation's picture and give it a shadow proxy. O1b takes that core
  (shared with the ghost figures if round 60 is merged by then, otherwise
  the minimal part of it) and makes it load any model from any stage
  package member: n_gekoa and n_gekoaic from stag0302, the window models
  from stagML11 and a battle stage, playing a model's motion clips by name
  (SQ_WAIT, SQ_TALK, the windows' SQ_OPENL / SQ_CLOSEL clips).
- **NPC:** that actor at its place in stagML11, faded by the camera like
  the ghost figures, turning to the player while the dialog is open.
- **SimUi:** the screens of section 9: window models from that core in the
  2D layer (TitleEntry, title_entry.hpp, shows how 2D items of the 640 x 480
  layer are placed), text with the game font (a private FontImage from
  bectext.igb and the public `layout_text`, managers.hpp 1032), glyphs and
  colours as in 9.1. The same overlay serves the lobby and the run. O1a's
  plain boxes use only the font. If playing the window models' clips
  privately proves too costly (an O1b spike of one day), the fallback draws
  the windows as quads with the window models' own textures and colours.
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

### 14.1 Test list

| Test | How | Done when |
| --- | --- | --- |
| T1 det | `r44int/build/det.sh` | 4004f056 / 396cb219 unchanged |
| T2 r25c | the campaign reference run, once as usual (NPC hidden) and once with `DW4_OVERFLOW=show` (NPC drawn, port events collected for it, H0 on, no talk) | 1,748,011 frames and the same fingerprints in both |
| T3 isolation | hidden smoke run: new game into the lobby, a scripted talk (O1a: the dev start), a 60 s run with the kite bot, Retire, back, 2,000 more lobby frames. Reference: the same script with the dialog's frames as neutral pads and no run | `net_fingerprint()` (with the generator state) and the canonical snapshot bytes equal at the return and at the end |
| T4 save states | F5 before the talk, talk, run, return, walk, F9 | no "the replay differs"; the replay reaches the same fingerprint |
| T5 story files | the test card folder and `DW4_PROFILE` file hashed before and after T3 | unchanged; only the `DW4_OVERFLOW_PROFILE` file is new |
| T6 scripted run | `--overflow-run form=p_agumon field=stagbs02 tier=normal seed=1 bot=kite offers=priority --smoke-test 100000` (headless; the run clock alone needs 90000 steps to 25:00, plus the start frames), twice, and the 20-seed batch of 14.2 | the same run hash twice; no Lua error; no budget overrun in the log; the targets of 14.2; a summary file |
| T7 unit tests (ctest) | `overflow_profile_tests` (read, write, unknown lines, `.bak`), `overflow_offers_tests` (eligibility, weights, no empty window), `overflow_spawn_tests` (ring points on walkable cells, encirclement reuses RAMs), `overflow_rng_tests` (generator saved and restored across an Engine's construction), `overflow_balance_tests` (14.3) | pass; ctest 40 + 5 |
| T8 performance | T6 with per-step timing (as DW4_TRACE_TRAVERSAL_TIME) and a hidden render pass | inside the budget of section 12 |
| T9 texts | every string of overflow_text.inc against the font's character list | no missing glyph |
| T10 native scripts | (a) the native player hit handler with the rule set "original" against `hit_p_<form>.lua` on every hit of a recorded hidden fight (dng101b1, staggf01): HP, sequence, statuses, HUD sends; (b) each arena's native start against its first.lua with the boss lines skipped: switch states, layers, panels, camera mode | equal |
| T11 host extraction | det.sh and hidden screenshots in three scenes (title, lobby, a dng101b1 fight) before and after O1a lane A's extraction | det unchanged, screenshots pixel-identical |

The kite bot (a test helper of the director): each step it walks away from
the enemies within 40 units (weighted 1/d²), back towards the centre when it
is past 70 % of the arena's radius. `offers=priority` takes offers by a
fixed list; `offers=random` by the run's seed. All runs are hidden
(`--smoke-test`, screenshots only through DW4_RENDER=1 in a hidden window),
loopback only, never a visible window.

### 14.2 Acceptance targets (O1a and every later round)

Measured over 20 seeded T6 runs with the kite bot, a Rookie, no Plug Lab
points, Normal:

| Target | Value |
| --- | --- |
| hits of the start Art to defeat a normal enemy in phase 1 | median ≤ 2 |
| first level-up | ≤ 18 s |
| level-ups in phase 1 | one every 20-40 s |
| survival time of the kite bot | median 20:00-24:00, at least 6 of 20 seeds reach 22:00, not every seed clears (O1a's clear at 25:00 since the forge round of 05.11: phase 4 goes on for the finale's five minutes; 17.1's 22-25 minutes for a run that goes well. O1a had 7:00-14:00, the balance round of 05.11 15:00-20:00) |
| survival time of the human bot (`bot=human`, a newcomer; 05.11) | no defeat before 3:00, median 14:00-18:00, at most 1 of 40 Agumon runs (seeds 1-20 and 101-120) ends before 10:00 (the forge round: the first Forge is sure, docs/OVERFLOW_DIRECTOR.md 11) |
| skill after 10:00 | the kite bot's median at least 3 minutes above the human bot's, for each form |
| no wall | no two-minute window holds more than 9 of 20 defeats of one bot and form |
| Agumon against Veemon | the medians within about 15 % of each other, for the human and the kite bot |
| highest single hit of a non-Guardian | ≤ 35 % of MaxHP; the clamp never triggers |
| frames without control | ≤ 5 % of the run's frames |
| enemies that close in | ≥ 80 % of each family's spawns come within 30 units in 8 s |
| skill-up panels | at most one every 30 frames |

(The user's first run, 05.11: dead after two minutes; the human bot came
from it, docs/OVERFLOW_DIRECTOR.md 10.)
The user also plays O1a himself; his verdict on the first five minutes
counts more than the numbers.

### 14.3 Balance unit tests

`overflow_balance_tests` read the DB tables and `tables.inc` and fail the
build when a rule breaks:

- **No one-hit enemies.** For every family of every phase and every tier,
  the worst hit of its eneatk attacks (physical, magic and proportional,
  with the crit bonus, the time and tier factors and the Corrupted factor)
  against the Rookie with the lowest DEF at the phase's expected level must
  stay at or below 35 % of that Rookie's MaxHP. Guardians are exempt. A
  family that fails gets its own damage factor in `tables.inc`.
- **Quick early kills.** The start Art of each Rookie at levels 1-12
  defeats every phase 1 family's warm-up HP in at most 2 hits (the median
  roll), and TECH1 at Petty in at most 2. A class that fails gets its own
  Art factor in `tables.inc` (Guilmon's dagger, STR 55 + 48, is the
  closest case against e_mecha1's Normal rows of 68-167 HP).
- **Enough RAMs.** For every phase: the RAMs of the pool ≥ the phase's
  highest A(t) + 28.
- **No family twice.** No Guardian family is a regular family of any phase.

## 15. Round plan

The rounds start after the user's answers (17). Each lane works in its own
worktree from the round's main; the merge order is given per round, and
every round ends with T1, T2, ctest and the mode's own tests, then a review
lane (a critique of the merged round, as in earlier rounds).

### 15.1 Round O1a: the vertical slice

The core loop, felt as soon as possible. No NPC, no window models, no
Guardians, no gear, no meta file. Started from F1 > Gameplay > "Data
Overflow (test)" (offline only) or `--overflow-run`.

| Lane | Scope | Done when |
| --- | --- | --- |
| A host | the extraction of 13.3 (T11 first), the run loop and the switch, the generator save / restore, the static audit, renderer / audio / input / F-key / Discord / achievement isolation, pacing reset, the dev start | T11, T1, T3, T4, T5 pass; a run starts and returns in a hidden run |
| B hooks | OverflowRules and H2, H3, H4 (incl. the level-up rule and the skill rule), H6 with aim, H8, H9, H10 (the player hit handler and the Violet Ring's start); spikes: enemy approach and speed per family (4.3), the player's body push against crowds and the encirclement (N1), the camera (H7), the jump's MUTEKI, the weapon model change in hand (5.3), the memory of all 21 families on one arena | ctest passes; T1, T2 unchanged; T10 passes; a spike note per item |
| C director | spawner with the pools and reserves of 4.2, recycling and encirclement by reuse, phases 1-4 on the Violet Ring without Guardians (at 20:00 the run completed; since the forge round of 05.11 phase 4 goes on to 25:00, the finale's five minutes), Corrupted, Agumon and Veemon with Cleaver and Blade Arc (aimed), the techniques Fire, Cutter, Bolt, Impact and Heal Lt (the rest of both forms' lists stays locked in O1a), skill growth and caps, the ladder, offers Forge / Train / Plug, the run curve, disks from Corrupted with A and the L panel, defeat, overlay boxes for level-up, pause and results, the run log, the kite bot | T6 with the targets of 14.2, T7 (offers, spawn, rng, balance), T8 pass |

Merge order B, A, C. **Done:** the user starts a run from F1 with Agumon or
Veemon, plays it, and says whether the first five minutes are fun; the
targets of 14.2 hold in the hidden bot runs; det.sh, r25c, ctest and T3-T11
pass.

### 15.2 Round O1b: the mode in the lobby

| Lane | Scope | Done when |
| --- | --- | --- |
| D screens | the presentation actor core, the NPC (place, fade, talk via H0 with the port event rule of 3.3), the dialog, the character select for the four Rookies with loadout, the level-up window, pause with the build page, results, the HUD overlay, the finale timer, texts; hidden screenshots of each screen for review | T9 passes; T2 with the NPC shown; every screen in the screenshot set |
| E content | the four Guardians of phases 1-3 and LordKnightmon (the spike in the Violet Ring: enters, fights, dies without its ded_; else the elite fallback), the finale with the quest timer and TIME UP, Guardian reactions, Guardian music, Guardians' drops; Guilmon and Dorumon; the four Rookies' full technique lists; gear: Brave Core, Armor and element Boards, Wisdom / Spirit / Speed Chips, the form's PWR Medal, Drain Ram, the pockets, ¶ Data power-ups; Awakening for the four Rookies | T6 per Rookie reaches the finale in some seeds; a spike note per Guardian |
| F meta | `overflow.dat` (8.5), Sim Bits, the Plug Lab, the loadout, the records needed by O1b | T7 (profile) passes |

Lane E as built, its spike notes, measurements and what is left:
docs/ROUND_O1B_E_NOTES.md.

Merge order E, D, F. **Done:** offline and alone, talking to Gekomon opens
the menu; a run with any Rookie plays four phases with Guardians and the
finale, with gear and Awakening; the Sim Bits reach `overflow.dat`; back in
the lobby the story goes on bit for bit; det.sh, r25c, ctest and T3-T11
pass.

### 15.3 Round O2: content pass 1 (abilities and gear)

All 19 techniques and the techniques of every form's list, all chips,
cores and boards of section 7, the ?????? offer, the extensions, crates,
disks and Boost, damage and defeat statistics in the results. Done: each
technique, chip, core and board appears in a T6 variant that forces it (an
`offers=` list) and shows its effect in the run log; 14.2 still holds.

### 15.4 Round O3: Digimon, lines and the route

Forms 4-15 with their Arts, signatures and traits; the start states of 5.3;
the field change and the route of 4.1 (Goblin Keep, Crimson Core, the
stagBS06 spike); the lines and digivolution (question 6); each arena's
native start (T10); the Data Records screen and the unlock tree. Done: every
form starts a run (T6 per form); a line digivolves twice in a T6 run and the
run state survives each field change (level, skills, gear, HP fraction,
clock); each record can be met in a forced test run; the new arenas pass T6
and T8.

### 15.5 Round O4: tiers, Endless, balance, polish

Hard and Very Hard, Endless, a balance pass by hidden bot runs (survival
time per form and tier, damage share per ability, level at each phase, the
Awakening time), the sound limits, the options (damage numbers, NPC on /
off), the README and ROADMAP lines. Optional: a 3D preview of the Digimon
in the character select (the ghost figure loader can show p_* models).
Done: the balance targets agreed with the user are met in the bot runs, and
the user has played it.

## 16. Risks

| Risk | Effect | Answer |
| --- | --- | --- |
| enemies idle at the spawn distance (AIM bands) | no pressure | the O1a spike per family; the 40-60 ring; recycling of idlers (4.3) |
| the player's body pushes enemies apart (PCBA → SQ_PUSHED, hit_e_*.lua lines 3-5) | crowds clump, the player walks through an encirclement | the O1a spike; encirclements from ranged families; a soft separation of enemies only in the run |
| Guardians' AI in a foreign arena (AIM maps may expect their stage's points; death scripts expect their scenes) | a Guardian stands still, walks off or never dies | the O1b spike per Guardian; the elite fallback (4.4) |
| the native handlers drift from the scripts | the run's player behaves unlike DW4 in details | T10 with the "original" rule set on recorded fights |
| the host extraction | the original's presentation changes | T11 (pixel-identical screenshots, det.sh) before anything else in O1a |
| RAMs per family (3-12) and per pickup (8) | fewer enemies of one kind than the genre usually shows | the pools keep earlier families (4.2); recycling; 60 big 3D enemies fill DW4's camera |
| memory of 21 families and the Guardians on one arena | a large working set, a slower start | measured in O1a; from O3 each arena loads only its phase's pool; before that, the earlier phases' donors could be dropped at the phase change |
| the field change (O3) | lost run state, a visible hitch | DW4 carries player state across stages natively; the director's own state is small; fast loading measured at about 16 frames |
| the port hitbox (H6) with aim | Arts hit differently from the original swing | the boxes come from the sequences' own HITBOX data; T6 compares an Art's hits with the same swing done by the original A press in a hidden test |
| the box camera too close for the genre | enemies come in unseen | the H7 spike; the spawn ring stays just outside the view |
| window models played privately | the screens cost more than planned | the one-day O1b spike; the quad fallback (13.6) |
| static state shared between engines | the story's determinism | the audit and T3 (13.2) |
| SE of many hits | noise, lost important sounds | the SFP pool's priority eviction, the director's 4-frame limit |
| font glyphs | missing letters | only the game's glyphs; T9 |
| round 60 not merged when O1b starts | lane D lacks the actor core | lane D takes the minimal part of ghost_figures and the later merge shares it |

## 17. Questions for the user

The design assumes the first option of each question until the user
answers.

1. **NPC:** Gekomon (the sentry Gekomon of the Venom Jungle, with a joke
   about the Gekomon in its simulation) or a Digi-Elf in a new role (its
   face icon is in the lobby already) or BlackWarGreymon (a quest-5 figure:
   risk of a spoiler)?
2. **Names:** "Data Overflow" with "Sim Bits", "Plug Lab", "Data Records",
   and the offers "Forge" and "Train", or other names?
3. **MP:** techniques on cooldowns and MP only for the heal group (Heal
   Lt, Aura, Cure, Raise), or cooldowns only and no MP at all, or MP for
   every technique with costs in proportion to its power?
4. **Buttons in a run:** walking only (A just for disks, L for the disk
   panel), or DW4's buttons on top (A attack combo, which stops the
   Digimon; B guard with half damage and a lockout; X jump)?
5. **Run length:** four phases of five minutes plus the finale (about 22-25
   minutes), or longer (for example six phases, about 32 minutes)?
6. **Digivolving in a run:** along class lines at the Guardians (Agumon →
   H-Kabuterimon → Susanomon, Veemon → WereGarurumon → IPDramonPM, Guilmon
   → Cl-Angemon → Gallantmon CM, Dorumon → Alphamon, WarGreymon → BK
   WarGreymon; the Art and its skill stay), or the lines the anime knows
   (Agumon → WarGreymon, where the Art changes class and its skill starts
   again), or one form per run?
7. **Story link:** should forms the story save has reached already count as
   unlocked in the simulator (only read, never written), or should
   everything be unlocked in the mode itself?

### 17.1 Answers (user, 2026-11-04)

1. NPC: **Gekomon** with the name joke.
2. Names: **as proposed** (Data Overflow, Sim Bits, Plug Lab, Data Records, Forge, Train).
3. MP: **cooldowns only; MP pays heals only.**
4. Controls in a run: **move only** (A uses disks, L opens the disk panel).
5. Run length: **about 22-25 minutes** (4 phases + the finale; Endless as an option).
6. Digivolution in a run: **lines with the same weapon class** (e.g. Agumon -> H-Kabuterimon -> Susanomon).
7. Story link: **forms the story save has reached are unlocked in the mode** (read only, never written); the rest is
   unlocked in the mode.

## 18. Revision 2: the critique and the answers

| Finding | Revision 1 | Revision 2 | Where |
| --- | --- | --- | --- |
| K1 Corrupted kill with one hit | the family's Hard row (e_goburi TSUYOSA 1188, a hit of about 1,030 against 180 HP) | Normal row, HP x 8, damage x 1.5, never Hard / Very Hard rows on any tier; the 35 % balance test and the runtime clamp | 4.2, 4.4, 4.8, 14.3 |
| K2 hit reactions take the control | not mentioned | the native player hit rule (H10): normal hits flash without a sequence, control statuses ≤ 30 frames, 50 frames MUTEKI with blinking, only Guardians knock down; the frames without control are logged (≤ 5 %) | 4.8, 13.4, 14.2 |
| H1 full refill on every level-up | taken over | fractions kept + 3 % (≤ 5 %); healing from techniques, chips, pickups | 4.5, 4.8 |
| H2 the guard makes invulnerable | B kept as the default | walking only by default; the classic guard takes 50 % and locks out for 3 s; the jump's MUTEKI checked | 4.8, 10 |
| H3 two start Arts strike only forwards; slow kills | Cleaver and Blade Arc along the facing; 3 hits per kill | every Art aims at the nearest enemy or group with range ≥ 20; warm-up HP x 0.6 and Art factor 2.5: 1-2 hits | 6.3, 4.3, 14.3 |
| H4 too close to Vampire Survivors | 2 + 4 weapon and 3 + 3 passive slots; Awakening from level 5 + partner + key; Rescan / Delete / Skip; a Calibration list like the power-ups (Magnet, Greed, Backup Chip, Reset); heal and money when full; OVERFLOW; Talis of Chaos as a curse | growth through DW4's skill system and weapon ladder; Awakening by skill 2500 (SOUBI / SKILLPOINT); the form's own six techniques + ≤ 2 extensions; uneven slots from DW4's equipment window (1 Art, the techniques, Core, Board, 2 pockets); no reroll / banish / skip; Plug as the filler; meta = per-form plugs (Plug Lab) and a starting loadout, no global power-ups, no refund; the finale ends the run (quest timer), Endless only optional; Talis only as an appraisal result | 1, 4.4, 4.6, 6, 7, 8 |
| M1 RAMs exhausted from about 11:00 | the carried families only; new enemies for encirclements | all earlier families stay (weight 0.5); unit test RAMs ≥ highest A + 28; encirclement reuses the 16 farthest | 4.2, 4.3, 14.3 |
| M2 Guardians share models with regular enemies | MRδ04 and MRε05 as Guardians while e_mecha4 / e_mecha5 are regular | no family is both; MRδ04 / MRε05 are no Guardians; the finale is LordKnightmon | 4.2, 4.4 |
| M3 skill points keep growing | not mentioned, skills fixed by the director | used on purpose with the run's rule (no roll, caps, a panel only on a visible change) | 6.2, H4 |
| M4 chasing not checked | assumed | spike per family, 40-60 ring, recycling of idlers, target ≥ 80 % close in | 4.3, 14.2 |
| M5 MP costs make high ranks a trap | MP / 8 for every technique | cooldowns; MP only for the heal group, in proportion to the heal (question 3) | 4.8, 6.4 |
| M6 a separate host loop would copy code | `overflow::run(HostContext&)` beside an untouched main loop | a mechanical extraction first, proven by det.sh and pixel-identical screenshots (T11); pacing reset on the return | 13.3, 14.1 |
| M7 O1 too big | nine hooks, four Guardians, OVERFLOW, Awakening, windows and meta before the user felt the loop | O1a vertical slice the user plays from a dev option; O1b brings NPC, windows, Guardians, gear and meta | 15 |
| M8 no balance targets | T6 "reaches 5:00" | the targets of 14.2 | 14.2 |
| M9 talk_box only with port events on | not noticed | port events also while the NPC is shown; `talk_box` taken out before the tracker | 3.3, H0 |
| V1 forms alike | any Art and all 19 techniques for every form, ↓ read as affinity | own six techniques (↓ is in the thresholds), a trait per form, pre-levelled Art for Champion and Mega starts | 5 |
| V2 one empty arena for 20 minutes | one field per run; digivolving deep engine work | O3 route: the next arena after each Guardian, digivolving with it along class lines (question 6) | 4.1, 5.3 |
| N1 encirclements walked through | not considered | the O1a spike; ranged families; a soft separation in the run | 16 |
| N2 ¶ X Data no real hurdle | the Awakening key | removed as a key; ¶ Data powers chips up as in DW4 | 7.5 |
| N3 first.lua replaced by a short list | a C++ command list | the native field start mirrors each first.lua without its boss lines (incl. SWITCHCTRL ALLON, funcFirstLayerSet); T10 | 13.4, 14.1 |
| N4 Gekomon three times | not noticed | a deliberate joke in the dialog; question 1 keeps the Digi-Elf | 3.1, 3.4 |
| proposed means for K2 / N3: resource overrides | - | not possible (whole archive entries; no Lua text compiler in the port); a native hook H10 instead | 13.4 |
