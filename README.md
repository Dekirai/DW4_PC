# Digimon World 4 PC

A native PC port project for **Digimon World 4 (USA, GameCube, `GDJEB2`)**.

The game and engine are reconstructed as authored C++20 source from the
original `main.dol` and game data: the original `be*` managers, their
messenger, the embedded Lua 4 stage scripts and the Alchemy (IGB) scene
graphs run natively. No emulator, recompiled PowerPC code or original
executable bytes are part of this repository. Users supply their own copy of
the game. Builds and tests run locally; GitHub Actions is disabled.

**It is not yet a complete or 1:1 playable reconstruction.**

## What runs today

- Boot flow: company logos, opening movie stub, title screen with
  "Press Start", Game Start / Option menu and the attract-demo timeout.
- New game: the four-panel character select (join prompt, Create/Call
  Digimon, partner selection with hologram preview, status window, START),
  `gameStart.lua` and the hand-off to the Home lobby, with the name entry
  reel and, once unlocked, the difficulty choice. Call Digimon loads a
  character from a memory card box.
- Memory card saves: the lobby's save NPC opens the save room (slot A/B,
  creating or formatting the game file, the four save boxes with status
  windows, overwrite check, timed write). The save lives in one file,
  `saves/savedata.dat` next to the program (see *Saves* below); GameCube
  saves from Dolphin, GCMM, GameShark, MaxDrive / Action Replay and memory
  card images import from the F1 menu's Saves tab.
- Lobby areas (`stagML11`, Central Area, Shop Area, Commander Room): first-visit
  story scenes, dialogs with portraits and prompts, talking to NPCs, walking,
  jumping, floor gates with the confirmation dialog and stage changes.
- The stage-select pad to Death Valley and its practice tutorial: lesson
  texts, enemy generators (GENERATER), weapon attacks with the original
  hitbox queue, FIGHTCALC damage, critical hits and damage reactions, the
  guard and hold-A attack chains, stage-end demo cameras and the following
  lessons.
- Death Valley combat: the original AIM enemy AI, enemy attacks and damage,
  player defeat with the bit capsule, EXP and level-ups with the HUD meters,
  hit effects, and item drops from the generator tables (bits picked up on
  contact, disks, key items and rolled equipment picked up with A, one
  floating item name per player, and only that item is picked up).
- Quick panels (L disks, R techniques, Z status, Y emergency heal), MP
  techniques with magic damage, and the START status window with its
  equipment page: weapon socket (reorder, chip pockets), armor and sub slots,
  the device folder list with pages, level/skill checks and stat previews;
  the folder page (discard, sort, give devices, disks and bits) and the skill
  page (forms, weapon skills, techniques).
- Lobby programs: weapon, armor and chip shops with their level-based stock
  and stat previews, the disk shop, selling, the junk shop with bit
  donations, the bank (devices and bits), the X data keeper (stats and chip
  upgrades), the appraiser for unknown devices and the Digi-Lab: digivolution
  into the forms reached (each form keeps its level, EXP, skills and stat
  ups), the strongest usable weapon equipped, then the evolution scene;
  the card keeper opens booster packs (three cards drawn by their weights,
  named in their rarity colours) and shows the album of the 50 cards.
- Status effects of players and enemies: poison ticks, sleep, stun, bind,
  attack and power down (plus the speed/power up, refract and metal
  commands) with the original FIGHTCALC chances and times, their DOWN/UP
  values in the stat recalculation and damage, and the icons above the model;
  the support techniques (heal, heal all, cure, speed and power up, refract,
  raise: a defeated player's bit capsule brings the player back, and with it
  the bits and part of the EXP lost at the defeat)
  and the weapon and technique skill growth with the skill up panel; the
  status quick panel lists the active effects, and the cure and raise disks
  work.
- The loading screens of every stage change (the "Licensed by Nintendo"
  screen at boot, the area title screens before stages and dungeons) with
  their minimum play time, and the box camera: the camera pivot follows the
  players' centre and each camera box of the stage turns and distances the
  camera, blended in as the players cross into it; camera shakes.
- The opening movie after the logos and in the title's attract mode: the
  Sofdec file's program stream is split natively; its MPEG-1 video is
  decoded with pl_mpeg and its ADX sound plays through the sound mixer, with
  the original's 30 Hz movie frames, Sofdec's start and end and its disc
  reads; A or START skips it.
- Music and ambience: the stage sound lists' BGM and ambience streams are
  the CRI ADX files of ndmw.afs, decoded natively (loop points, volume, the
  fade-out before a change) and played through SDL3 audio.
- Sound effects: the CRI Sound Factory banks (the common allse.sfp and each
  stage's banks) with their request sequences, drum-program splits and DSP
  ADPCM samples, triggered by menus, scripts and the models' SE sets (the
  sounds each action plays, such as jumps, landings, footsteps and swings).
- Rendering of original models, textures, skinned and animated actors
  (Enbaya clips) with the weapon parts on their bones, UV animations, text from the original fonts and the HUD layers,
  the projected character shadows (the shadow proxy meshes drawn from a light above the players onto the
  stages' receiver meshes), the stages' light sets and the particle effects (torches, warps, hits).

Collision follows the original HITLANDMODEL queries and HITLANDDELIVER
responses (walls, floors, falling and landing, the players' limit circle),
including rides on boats, trains and moving platforms and the conveyor,
quicksand and slippery floors; many smaller managers are still missing. See
[ROADMAP.md](docs/ROADMAP.md).

## Layout

| Part | Contents |
| --- | --- |
| `include/dw4`, `src` | `dw4_core`: IGB documents, packages and resources, textures, meshes, skinning, Enbaya animation, Lua 4 bytecode, pads and motion primitives |
| `include/dw4/engine`, `src/engine` | `dw4_engine`: frame step, messenger and demo manager, Lua host, scene graph, and the reconstructed managers (stage loader, scene/layer/camera, model control, actions, text, menus, title, character select, floor mats, collision) |
| `src/game` | `dw4_game`: SDL3 window, OpenGL renderer, input mapping, settings and the F1 menu, the save store and GC save import, online sessions, Discord Rich Presence, name tags, achievements |
| `server/DW4Online` | The online backend (ASP.NET Core + MariaDB: accounts, online character, cloud save, achievements, rooms, relay, website), its tests and deployment kit; built with `dotnet`, separate from the CMake build |
| `tools` | `dw4_inspect` (IGB inspection), `dw4_catalog` (logical resource catalog), `dw4_texpack` (texture export for pack authors), `remaster/` (4x upscaling pipeline for texture packs), `verify_game.py` (read-only ISO identification) |
| `tests` | Asset-free tests for the core formats and tools |
| `docs` | Format and module notes; `docs/research` keeps the reverse-engineering notes (DOL addresses, recovered behavior) |

The engine design is described in [ARCHITECTURE.md](docs/ARCHITECTURE.md), the
working method and next steps in [PORTING_STRATEGY.md](docs/PORTING_STRATEGY.md).

## Build on Windows

Requirements: Visual Studio with *Desktop development with C++* (MSVC, CMake,
Ninja). SDL3, Dear ImGui, pl_mpeg, stb and Zstandard are fetched by CMake at
pinned commits and checksums.

```bat
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Jobs 4
```

This configures `build\Release`, builds everything and runs the tests.
`-CoreOnly` skips the SDL/OpenGL runtime; `-Package` installs into
`dist\DW4_PC`: one self-contained `DW4_PC.exe` (static C++ runtime, only system
DLLs), the tools, licenses and docs, ready to copy anywhere. Builds never start
the game.

## Run with your own game data

The runtime reads the USA disc (GDJEB2, revision 0) straight from an
uncompressed disc image (`.iso` / `.gcm`) or from a Dolphin extraction
(*Properties > Filesystem > Extract Entire Disc*). Dolphin's RVZ images
(Zstandard or uncompressed) are converted once into `data/<name>.iso` next to
the program (a few seconds, bit-identical to the original disc;
`dw4_game --convert-rvz IN.rvz OUT.iso` does the same from the command line).
On the first start it asks for one and checks it; WIA, CISO and NKit images
can be converted to ISO or RVZ in Dolphin (*Convert File...*). The choice is kept in
`settings.ini` (`local\settings.ini` or `settings.ini` in the working folder,
else next to the program) and `--data PATH` overrides it:

```ini
[data]
path=E:\Games\Digimon World 4 (USA).iso
```

The same file keeps the PC settings that the F1 menu edits while playing:
`[window]` `width`, `height`, `fullscreen` (borderless, also F11), `widescreen`
(the 3D view fills wide windows at the original vertical field of view while
the 2D layers stay 4:3 and full-screen fades stretch), `vsync`,
`fps_limit` (presented frames per second, 0 = off; the game always updates
60 times a second), `smooth_motion` (above about 75 presents per second the
picture blends between the last two updates: objects, skinned characters, UV
scrolls, the camera and the shadow light; each placement of a shared object
blends on its own, a looping or stepped animation that jumps back or steps
(and an object that turns or moves too far in one update) is drawn as it is
in that update, UV scroll included, instead of being blended backwards, and the
picture also keeps what the previous update's camera still saw at its edges;
none of it changes the game) and `msaa` (2/4/8 samples, applied at start-up),
`[audio] volume` (percent), `[menu] pause` (pause while the menu is open),
`[preferences] skip_intro` (boot straight to the title), `[gameplay]`
`exp_rate`, `damage_dealt`, `damage_taken` (percent, 100 = original) and `loading`
(`original`, the default, or `fast`; see *Fast loading* below), `[randomizer]`
`seed` (0 = off), `drops`, `enemies` (`off`, `each`, `wave`) and `zones` (`off`,
`dungeons`) for new games (see *Randomizer* below; an old `[gameplay] drop_seed`
becomes `seed` with `drops` on), `[data] save` /
`save_b` (the save files of memory card slots A and B, default
`saves/savedata.dat` / `saves/savedata_b.dat`; relative paths are next to the
program), `[data] cards` (instead of the save files: a GCI folder root with
`Card A` / `Card B`, e.g. Dolphin's `GC/USA` folder to play on its saves),
`[data] card_a` / `card_b` (what
the slots hold: `none` for an empty slot, `unformatted`, `inserted`, and / or the card's
blocks such as `251`; default a 2043-block card in slot A and one in slot B when
its file or folder exists, also `DW4_CARD_A` / `DW4_CARD_B`) and
`[keyboard]` with up to three key names per control (`a=Return, Space, K`) and
`[gamepad]` with up to three SDL controller inputs per control (`l=lefttrigger,
leftshoulder`). The controls are `a`, `b`, `x`, `y`, `start`, `l`, `r`, `z`,
`stick_up` / `stick_down` / `stick_left` / `stick_right` (the main stick),
`dpad_up` ... `dpad_right` (the D-pad) and `cstick_up` ... `cstick_right` (the
C-stick); a controller's left and right stick are always the main stick and the
C-stick. Files from before the separate D-pad and C-stick (one `up` / `down` /
`left` / `right` input that set the D-pad bit and walked) are migrated when
they are read and saved with the new names: the keyboard's keys go to the main
stick except the arrow keys, which become the D-pad (so the old default `W, Up`
becomes WASD walking and arrow-key D-pad); a direction bound to arrow keys only
keeps them for walking and leaves the D-pad without keys (bind them in the F1
menu; the keypad C-stick still moves the quick panel cursor). The controller's
old inputs (default the D-pad buttons) become the D-pad, which no longer walks,
as on the GameCube (bind D-pad buttons to `stick_*` to walk with them). A
default key that the file uses for another control is left out.

### Fast loading

The port loads a stage the way the original does: every file comes off a
simulated GameCube disc drive in 64-sector reads, the loading thread spends the
original's time on each, the outgoing stage's teardown and the registration of
the new one take the updates they took on the GameCube, the loading screen
plays its minimum time, and the music is ready only once its first blocks came
off the disc. A stage change therefore takes 400-900 frames (7-15 seconds)
like the original. *Loading: Fast* in the F1 menu's Settings tab (also part of
the Modernized preset; `[gameplay] loading=fast`, `--loading fast` for one
run) is a deliberate deviation from that timing: the loader runs the same
states in the same order and the scripts run as always, but no file waits for
the drive, none of the GameCube's teardown / routing / registration time is
spent, the loading screen goes as soon as the files are in, and the sound
driver's disc reads end at its next update, so the stage's music is ready a
few frames after it starts. A stage change then takes about 16 frames (a
quarter of a second) plus the stage's own fades and opening scenes, which stay
as they are. Large files are parsed one per frame, smaller ones are grouped up
to 1 MiB per frame, so no frame takes much longer than with the original
timing. A change applies from the next stage change on. The option changes
the frames a session plays, so save states keep it (a state replays with the
timing it was made with; afterwards the current choice applies), online
sessions use the host's choice on every machine, and Original stays
frame-identical to the original timing. Memory card access keeps its original
speed: it takes well under two seconds per save and the save room's messages
follow it.

| Stage change (frames from the change to the stage start; 60 frames = 1 s) | Original | Fast |
| --- | --- | --- |
| Boot: logos / opening / title (`stagIS08` / `09` / `10`) | 234 / 62 / 118 | 15 / 15 / 15 |
| Boot straight to the title (skip intro) | 218 | 15 |
| Dungeon `dng101b1` (to player control) | 490 (550) | 16 (76) |
| Field `stag0101` / `stag0204` | 769 / 797 | 16 / 16 |
| Quest `dngQ1B1e` (to player control) | 575 (635) | 16 (76) |
| Boss `stagBS01` | 262 | 16 |
| Lobby `stagML11` | 516 | 15 |

In the automated story run (the r25c segments, 183-185 stage changes) the
loads take 90,913 frames (25 minutes) with the original timing and 2,825
frames (47 seconds) with Fast. The real time a load's frames take on the PC is
about the same either way (the same files are parsed and the same scripts
run, 10-35 ms in the longest frame of a load), so a Fast stage change is over
in about a third of a second.

### Randomizer

An optional randomizer (F1 Cheats tab, `settings.ini [randomizer]`; off by
default, and with it off the game is the original frame for frame). One seed
drives three parts, each switched on its own:

- **Item drops** change into other items of the same kind (weapons into
  weapons, disks into disks); key items stay. The same seed gives the same drops
  as the item drop seed of earlier versions.
- **Enemies** become other Digimon: every enemy of a wave drawn on its own
  (`each`, the default: four Numemon become four different Digimon) or one
  family per wave (`wave`). A swapped enemy keeps the original enemy's record:
  its HP, strength, defence, EXP and the wave's drops stay, only the body, its
  attacks and sounds change. Each enemy stage draws from a pool of up to six
  families: its own, filled up from the whole game weighted by area (a family
  of the stage's own area is eight times as likely as one three areas away).
  Families a stage does not have are loaded from the smallest story stage
  where they are ordinary enemies (their six model infos, attack effects, hit
  and AI scripts, sound effect bank and cues), like the original's own enemy
  packages (`stag0202/separate/e_arachne.igb`); a stage load with the
  randomizer reads up to five such members more (no extra loading time is
  simulated). Bosses, the one-of-a-kind mid-bosses
  (Blossomon, Mammothmon, SkullGreymon, Anomalocarimon, Tonosama Gekomon,
  Diaboromon, LordKnightmon), stag0404's Imperialdramon, the boss stages
  (`stagBS*`), the stagGF03 fights, every wave a stage script starts next to a
  model name (intro and demo fights wait for that model), a family whose
  only waves in the stage are such fights and every generator whose family the
  stage does not load itself (it never spawns in the original: no RAM for it,
  e.g. stag0403's e_mecha3 and stag0202's SkullGreymon generators) stay as
  they are. A wave keeps its
  entry move when the new family has it, else the family's plain entry; when
  all of a family's bodies are out, the next family of the pool (then the
  original) takes the place. A swapped enemy that falls off the stage or never
  lands (six seconds in the air or 150 units below its spawn point) is
  removed and counts as defeated, without a drop.
- **Loading zones** (`dungeons`): the dungeon entrances of each area are
  shuffled among the area's dungeons (area 1: Humid Cave / Cliff Dungeon, area
  2: Sand Labyrinth / Ancient Ruins, area 3: Geko Path / Vine Tunnel / Vein,
  area 4: Mecha Nest / Electro Mine); an entrance may keep its own dungeon
  (but at least one entrance leads somewhere new), so the seeds give 47
  layouts (2 x 2 x 6 x 2 without the unchanged one), and a dungeon's way out leads
  back to the entrance that was used. The exit prompt names the new target. A
  game bound before this rule (round 48: no entrance kept its own dungeon,
  which left only two layouts) keeps playing its old layout; its bond has no
  `zone_rule` and the F1 menu names it "rule 1".
  Towers, quests, the fortress, boss stages, the lobby gate, Gate Disks and
  defeats keep their targets, and every key item stays reachable (the gate
  in stag0301 that led to the Vein still opens with the stag0301 lever and the
  stag0302 console, whatever dungeon now lies behind it).

A new game binds the randomizer it starts with to its save: `randomizer.ini`
next to the save (`saves/randomizer.ini` beside `saves/savedata.dat`, or
beside the `[data] save` / `--save-file` file; the GCI root with `--card-dir`
/ `DW4_CARD_DIR`; `saves/` next to the program for a `[data] cards` folder,
so nothing is written into a Dolphin folder) keeps the seed and modes per
saved character (by the character's UNIQUE id), so changing the settings
later does not change a running game, and `randomizer_<UNIQUE>_spoiler.txt`
beside it lists the seed, the loading zone table and every stage's enemy
pool. Only a new game is bound by itself: a save from before (or one started
with the randomizer off) keeps playing the original game when it is called
with the randomizer on, and a notice says so; the F1 section shows what the
running save plays (the seed and modes of its bond, also when the settings or
the Settings tab's preset say otherwise), can bind the current settings to
it and can let it play the original game (*Play this save without the
randomizer*: its bond then says seed 0). Delete a character's section in
`randomizer.ini` to let it follow the settings again. Save states replay with the randomizer they were made with.
Online, every machine plays the host's randomizer and nothing is bound.
Randomized games are not the original game (`Cheats::original()`), so they
count as their own category wherever that matters (achievements unlock into
their own list with any of the three parts on).

Test runs: `DW4_RANDOMIZER=seed[,enemies[,zones[,drops]]]` (a bare seed turns
everything on; `zones` is the rule number: 1 the round 48 table, 2 the
current one, which a bare seed takes), `DW4_RANDOMIZER_SWEEP=1` makes every enemy generator of the
stage spawn its wave in turn and logs one `sweep` line per generator (models,
problems such as a missing hit script, a sound without a bank, a model that
did not start or one the watchdog removed); `DW4_RANDOMIZER_SWEEP=static` starts no wave and
logs one `check` line per enemy generator instead: whether it is shuffled (else why not), the floor
under its spawn points (none, a pit, quicksand) and what a pool family meets that the generator's own
does not (no hit script, hovering above the swing, a smaller climb or a deeper drop of its land
cylinder; wider bodies and a missing entry sequence are noted).

### Online co-op

Up to four machines play one session in lockstep (TCP, the host relays),
started from the command line (the F1 menu's Online tab belongs to the
online rooms below and shows such a session's state); everyone then starts at
the title together:

```bat
dw4_game.exe --host --players 2            rem waits for one guest on port 52040
dw4_game.exe --join 192.168.1.20           rem or --join host:port
```

`--port N` and `--net-delay N` (input delay in steps, default 3) adjust the
session. Every machine starts the same way (the host's stage, cheats,
rebalancing and loading choice) and
each step waits for every player's input, so the simulation stays identical;
guests report state fingerprints and a desync shows on screen. Each machine's
keyboard and first controller play its own player (host 1, guests 2-4). The
host's save (slot A) and the first guest's save (as slot B) are copied to every
machine, so both can call their Digimon at the character select; only the
Digimon World 4 save travels, never other files of a card folder. When the
session ends each owner gets its save back with what was saved. Save states
are off online; a guest that leaves plays on released, a host that leaves ends
the session for the others (they play on alone). The host needs the port
open (router / firewall) for guests outside the local network.

These LAN sessions are separate from the online play below, which goes
through the project's own server: all traffic is relayed by the server, so
nobody opens a port, and every account plays its own online character, which
starts from zero, separate from the local save.

### Online co-op (the world)

The seamless online co-op ([docs/SEAMLESS.md](docs/SEAMLESS.md), section 19)
has no rooms: everyone who presses *Play* is in one world. The title menu has
a third entry below Game Start and Option: **Online** (the same lettering,
the cursor moves to it with down from Option). It opens the account screens
over the title, connected to the project's server (fixed in the build): log
in or register (the recovery code is shown once). The home screen then has:

- **Play: <form> Lv <n> (Box 1)**: it ends an offline game (after asking),
  restarts the game online with your online character (a new account starts
  at Create Digimon; the save on this PC is not used) and takes you to **your
  own lobby, on your own story**: NPCs, shops, quest boards and the Digi-Lab
  behave as in single player. The character is saved on the server when the
  save room writes, at every stage change and when you go offline.
- **Settings**: the visibility of the instances you start (*Public*,
  *Friends*, *Private*), camera and light mode, fast loading and the
  randomizer of the instances you host. Your settings count at home and in
  every instance you host; as a guest you play with the host's. The
  difficulty is the character's, chosen at the Game Start; online there are
  no cheats and no rebalancing.
- **Account** (password, online character reset, log out everywhere) and
  **Log out**, and "Online now: <n> players, <m> instances".

B (or Esc) goes back a screen and from the first one back to the title menu.
The screens work with a controller first (text fields open an on-screen
keyboard), the keyboard and the mouse. Friends and blocks are kept on the
server's website (your account page).

**The lobby.** Other players in the same lobby stage appear as ghost
figures: their Digimon in its current form with its weapon and name tag,
walking and standing where they really are (look only: no collision, no
talks, nothing they do changes your game; a figure in your way or inside your
own Digimon turns see-through).

**Instances.** Leaving your lobby for a field or a dungeon (the lobby gate,
a quest board, the return mat) puts you into an *instance* of that stage's
area: a running one or a new one that you host. When instances you may see
run in the area, the **arrival prompt** asks which (round I1b): while you
confirm the gate's destination (or the return mat's *Yes*) the game asks the
server first and shows the rows over the gate's menu: each instance with its
host, where the host is, *Here (n)* for players at the very stage you arrive
in, its story point, ping and settings, and states such as *Boss fight*
(choosing it waits for the fight's end), *Loading*, *Arriving now* (its
players are on their way to that stage: you join them there), *Full* or
*Story not here yet* (greyed). The cursor starts on the row the server
suggests: a friend's instance, or one with players at your
arrival stage; otherwise on **New instance**, with Left/Right choosing its
visibility (*Public*, *Friends*, *Private*; remembered as your default). A
chooses, X shows a row's members, B keeps you standing at the gate (the
pictures are those of your controller). If the answer cannot come in time,
the same prompt appears on the loading screen, with B taking you back to the
lobby. Players who take the same gate within a few seconds of you arrive
together (also two who both choose *New instance*, as far as both chosen
visibilities let the other in: *New instance* with *Private* never puts you
into someone else's instance, with *Friends* only into a friend's). Without
anything to choose you start a new instance with your default visibility
and host it ("New instance (Public). View: Online menu"). Only instances whose
visibility lets you in are listed (*Public*; *Friends* for the host's
friends; *Private* for nobody else; a moderator sees every instance in the
prompt as his own choice, never in an automatic join). An instance belongs to
its area (a region of the map); inside it everyone travels freely, with their
own camera: members in the same field, dungeon or boss stage play one shared
stage instance (up to four, shared enemies and drops), a member who arrives
later joins it mid-stage, and exits and the gate disc take only the members
who say yes.

**Go along.** When members of your instance go into a dungeon while you are
in a field, a line at the top asks "Kim went into Ancient Ruins B1. Go
along?" for 20 s; the F1 menu's button opens the *Instance* page, whose *Go
along* box sends you after them (to the floor where the first of them is,
waiting while that floor is in a boss fight or loading) or closes the
question. "Going along..." follows; B on a loading screen that waits too
long takes you back to the lobby (the host is asked "End the instance for
everyone?" first).

**Removing and reporting.** On the *Instance* page every other member's row
opens a player card: **Report** with a reason (griefing, cheating, name,
other; "Report sent. Thank you.", a daily limit per player) for everyone,
and for the host (and a moderator playing) **Remove** ("Remove Kim from your
instance? They can't rejoin it for a while."): the removed player goes home
on his own story and the instance is hidden from him for a while. Moderators
work off the reports on the server's website (*Admin > Reports*).

**The instance's story is its host's.** A guest plays the host's story (its
chapter, quests and opened ways) with his own level, items, Bits and
Digimon, and everybody's field progress counts for the host's story. Going
home (the gate disc, an exit into the lobby, the game over, *Leave
instance*) gives you your own story back ("Back on your own story."); the
EXP, items and Bits you earned stay yours.

**The Online menu** is the F1 menu's *Online* tab (View/Back on the pad, F1
on the keyboard; online it pauses nothing): the instance's host, its
visibility (the host changes it with A; the members are told), the members
with their stage, level and connection, and **Leave instance** ("Leave Kim's
instance? You return to your lobby."; for the host "End the instance for
everyone?"). When the host leaves, is defeated or does not come back within
a minute after losing the connection ("Kim lost the connection. Waiting for
Kim..."), the instance ends for everyone ("Kim left. The instance ends.")
and every member goes home. **Go offline** saves the online character and
returns to the title.

Everyone plays for themselves as in an online game: the status window (START),
NPC talks and the map recorders in fields and dungeons are the member's own -
they open on that member's screen only (the others see "Kim is in the menu"
or "Kim is talking" over the name tag), several members can have their menus
open at once, and they pause nothing: enemies, generators, the other members,
timers and music run on, and a member in a menu stands still and can be hit.
Only one NPC talk runs in a stage instance at a time, as in local co-op:
while Kim talks, another member's NPC does not answer yet. An NPC's exchange
trades with the talking member's own folders.

```bat
powershell -ExecutionPolicy Bypass -File scripts\online-dev.ps1
```

starts a private database and the DW4Online server on 127.0.0.1 (nothing
leaves the PC), builds a development client (`build\OnlineDev`, needs
`DW4_ONLINE_DEV`) the first time, and opens two game windows that both press
Play; closing both windows stops the server. `-Data` names the game data
(default: the `[data]` path of `build\Game\settings.ini`), `-Card` a card
folder whose saves both start with. Player builds connect only to the
project's server and take its certificate pins from the build
(`local/online_pins.txt` or `-DDW4_ONLINE_PINS`,
[server/DW4Online/deploy/DEPLOY.md](server/DW4Online/deploy/DEPLOY.md) 6.4); a
build without them refuses the server. Online, achievements also count for
the online account (only your own player; the story challenges only on your
own story, at home or as a host), shown on the server's website.

While you play online, a small box in the lower left corner shows the ping
(the network's round trip to the server) and, while others share your stage,
the input delay: every member's input waits this many 60 Hz ticks so that all
machines run the same frames, about the ping plus 25-40 ms (3 ticks, 50 ms, at
20 ms of ping; 4 ticks, 67 ms, at 25-30 ms with a 60 Hz display). It is green,
yellow or red by how the link holds up; F1 > Settings can hide it, F1 > Online
shows the details. Every online session writes `saves/online.log` (the
versions and protocols, the input delay and why it changed, the link every ten
seconds, the game's pictures per second, vsync, the display's refresh rate and
whether its window was in front, losses and resyncs; no password or token;
rotated at about 1 MB; a second game started from the same folder writes
`saves/online-2.log`): send that file when online play lags (F1 > Online has
"Copy path" and "Open folder" for it).

Known limits:

- The instance browser (joining a friend wherever he is) and parties come
  with later rounds; until then friends meet by taking the gate into the
  area of the friend's instance (the arrival prompt lists it), or by
  arriving together.
- Camera tours and tutorials on mats, the shaking mats and the story scenes
  still pause the whole stage instance (every member in it), as in local
  co-op; only the status window, NPC talks and the map recorders are each
  member's own so far. A map recorder's floor map shows for the member who
  read it.
- Pits still darken every member's screen in the stage instance.
- Story progress that guests make while the host reconnects is held and kept
  only if he comes back; an instance that ends drops what was not saved yet.
- Nothing of the online play comes back into the local save; F1 > Saves >
  Cloud backup keeps one copy of the local save on the server.
- Online needs a CPU with FMA3 (every machine must compute alike); the game
  refuses online play without it.
- Ghost figures appear only in the lobby stages; in the fields and dungeons
  members of other stage instances show only as name tags.

### Online server (DW4Online)

`server/DW4Online` is the backend for that: one ASP.NET Core (.NET 10)
service with MariaDB that holds the accounts (open registration with rate
limits, optional e-mail, one-time recovery codes, admin resets), the online
character and its story, one private cloud backup of the local save per
account (the latest and five earlier revisions), the online achievements
(only online play counts; cheats, rebalancing and loaded save states never,
randomizer runs as their own category, speedrun times in in-game time without
loading), friends and blocks, the world (the instances, their history and
the staff's moderation), the TCP relay with one-time HMAC tickets (port
52041) and the website (accounts, friends, profiles, achievements with their
rarity, the instances, admin, privacy page). Its tests run on loopback;
building, configuration, the deployment kit for a Windows server (Windows
service, TLS, backups, updates) and the API are described in
[server/DW4Online/README.md](server/DW4Online/README.md) and
[server/DW4Online/deploy/DEPLOY.md](server/DW4Online/deploy/DEPLOY.md).

### Name tags

Name, level and HP / MP bars float over the players' Digimon (Settings tab,
*Name tags, Discord, achievements*; `settings.ini`):

```ini
[hud]
overhead_labels=auto   ; auto (only with two or more players), all, others (not over your own), off
label_scale=100        ; percent, 50-200 (the size also follows the window height)
label_numbers=false    ; HP / MP numbers beside the bars
```

They are drawn by the PC host over the game picture (ImGui, under the F1
menu) from the players' state after each step, the camera the picture used
and the widescreen widening; nothing is added to the game's scene, so the
simulation, save states, online sessions and the Dolphin comparisons are the
same with them on. The tag sits just above the head: one unit above the
form's head height, measured for each of the 16 forms from the drawn figure
in its stance (the head with its horns, ears, a collar or wings close to
it; weapons held up and wing tips further out are left out), over the head
joint (followed smoothly sideways, at most 3.5 units from the Digimon's
position). Raised arms and weapons do not move it; when the head itself
rises (a jump, the leap of a charged attack, the bounce of a run) the tag
goes up with it at once, so the head never pushes through the bars, holds
that height for half a second (a run keeps it steady at the stride's top)
and then eases back down (a model of no known form: above its highest
joint, else part of its bounding box). It follows the smooth
motion above 60 fps, fades with the distance from
the camera and is hidden during stage loads, demos and cutscenes (pad input
off), status and shop windows, the title and menu screens, and for a defeated
player. Tags that would overlap stack: the nearer player's keeps its place,
the one behind moves up with a thin line down to its player. A local
player's name starts with the colour of its HUD ring (red, blue, yellow,
green). When the picture hides a player (a wall or a pillar between it and
the camera: the depth buffer just drawn is nearer than three points just
above its head; the stage's collision cannot tell, it holds invisible walls)
its tag turns faint over a few frames, so it still shows where the player
is. The text uses Windows' Segoe UI Bold (else Arial Bold, else ImGui's
scalable font); the F1 menu keeps its own. Players beyond the four local
slots (the coming seamless co-op) are handed in by the netcode
(`OverheadLabels::set_remote_players`) and drawn the same way when they are
in the same stage. Test runs: `DW4_LABELS=all` and `DW4_TRACE_LABELS=N`
(every N frames the labels' head point and its height above the feet, screen
position, the feet's screen position, the distance and whether the last
picture hid the player, for the 1280x960 test window or `DW4_WINDOW`, without
a window too); `DW4_MSAA=n` gives a test run's hidden window the
multisampling of a player's settings (screenshots).

### Discord Rich Presence

With the Discord desktop app running, Discord shows what you play:
`<Form> Lv <level>` (for example *WarGreymon Lv 42*), the area (*Death Valley:
Humid Cave*, names from the loading cards), the area's picture with the form's
picture on it, the difficulty as the small picture's text, and the time since
the game started; *In Menus* on the title and menu screens. The program talks
to the app over its local pipe (`discord-ipc-0..9`) with its own small client
(no SDK, no DLL, no login): it looks for the app again every 15 seconds (a
Discord started later is found), sends a change at most every 15 seconds and
says goodbye when the game ends. Without Discord, or with the setting off,
nothing happens. A Discord that hangs never holds the game: every message to
or from it must get through within 2 seconds (the handshake's answer within
3), otherwise the connection is dropped and tried again after 15 seconds,
then 30, 60 ... up to 4 minutes while it keeps failing (the Settings tab shows
why), and quitting waits at most about a second for it. Settings: the Settings tab's *Discord Rich Presence* box, or

```ini
[discord]
enabled=true
client_id=1560921673354453093   ; the Discord application whose name shows as "Playing ..."
```

The pictures are the application's art assets, which its owner uploads once:
Discord Developer Portal > Applications > the application > Rich Presence >
Art Assets > *Add Image(s)* (PNG or JPG, at least 512x512, 1024x1024
recommended; the file name without extension becomes the key, lower case). The
keys the game uses:

| Key | Shown for |
| --- | --- |
| `logo` | title and menu screens (the large picture) |
| `area_home_server` | Home Server: Commander Room, Main Lobby (Terminal, Central, Shop Area), Digivolution |
| `area_practice` | Death Valley: Practice Stage |
| `area_death_valley` | Death Valley: Goblin Pass, Abyss of Grief, Numenume River, Doom Dome |
| `area_death_valley_dungeons` | Humid Cave, Cliff Dungeon, Dome Base |
| `area_dry_land` | Dry Land: Mummy Desert, Grief Wasteland, Train Line, Station X, Power Plant |
| `area_dry_land_dungeons` | Sand Labyrinth, Ancient Ruins, Virus Lab |
| `area_venom_jungle` | Venom Jungle: Gekogeko Land, Mangrove Forest, Venom Swamp, Dread Note |
| `area_venom_jungle_dungeons` | Geko Path, Vine Tunnel, Vein, Dread Bridge |
| `area_machine_pit` | Machine Pit: Battle Arena, Mechanical Core, Core Area |
| `area_machine_pit_dungeons` | Mecha Nest, Electro Mine |
| `area_apocalymon` | Apokarimon Space (boss) |
| `area_belphemon` | Demon Shift (boss) |
| `area_lucemon` | Hades Obelisk (boss) |
| `area_zone_x` | Zone X (boss) |
| `area_fortress` | Fortress: Guard Room, Rotating Hall, X Digimon Lab, GobGob Room |
| `area_quests` | the quest dungeons (Barbarian Cave ... Cursed Dungeon) |
| `form_agumon`, `form_veemon`, `form_guilmon`, `form_dorumon`, `form_weregarurumon`, `form_hkabuterimon`, `form_wargreymon`, `form_clangemon`, `form_beelzemon`, `form_alphamon`, `form_bkwargreymon`, `form_ipdramonfm`, `form_ipdramonpm`, `form_metalgarurumon`, `form_gallantmoncm`, `form_susanomon` | the 16 forms (the small picture) |

`dw4_game.exe --data <game data> --export-presence-art <folder>` writes
suggested 512x512 pictures for the `logo` and `area_*` keys from your own
copy of the game (they stay on your PC; upload them under their file names):
`logo` is the title logo on a transparent square, each `area_*` picture the
stage picture its loading card shows in the middle of the loading screen
(the card's picture quad, without the name and "Now Loading" strip of the
same texture), upright, centre-cropped to a square without stretching. The
form pictures are yours to choose. Test runs talk to Discord only
through `DW4_DISCORD_PIPE=<pipe name prefix>` (a test pipe, never the real
app; `dw4_presence_tests --serve <pipe> <seconds> [ready|silent|header|deaf]`
is a fake Discord that prints what it gets: `ready` answers like Discord,
`silent` takes the connection and never answers, `header` answers with a
message header and no payload, `deaf` answers and then stops reading) and
`DW4_TRACE_PRESENCE=1` prints each new activity.

### Achievements

The F1 menu's Achievements tab lists 72 achievements in eleven groups (story,
bosses, challenges, levels, forms, techniques, collection, quests, combat,
time, together) with the date of each unlock and the progress of the counted
ones; a notice shows at the top right when one unlocks (*Unlock notices*).
They are kept in `saves/profile.dat` next to the program (a `.bak` of the
previous file stays) and belong to this PC; the online account's achievements
come with the seamless co-op server. Rules:

- Nothing unlocks or counts while a cheat (god mode, infinite MP, one-hit
  defeats) or a rebalancing value other than 100% has been on in the running
  stage, after a save state was loaded in the running stage, while a save
  state replays, in test runs and while the campaign bot or a scripted walk
  plays. Fast loading does not matter.
- A run with the randomizer (item drops, enemies or loading zones) unlocks
  into its own list (shown beside the date).
- The run achievements (a story clear never defeated, under 8 / 4 hours, only
  rookie forms, Very Hard alone) need a new character tracked since level 1
  that never played with a closed gate (cheats, rebalancing, a loaded state).
  Speed counts the in-game time without stage loads.
- In local co-op every player of this PC counts; online only this machine's
  player (story achievements count for everyone in the session).

They come from a few observe-only hooks in the reconstructed game logic (boss
death scripts and defeat flags, level ups, digivolutions, new techniques,
cards, quest goals, player defeats, critical hits, enemy defeats, story flags;
`engine/port_events.hpp`) plus the players' state after each step. The engine
collects these events only while the host asks for them, never reads them
back, and they are in neither the state fingerprint nor a save state. Test
runs: `DW4_ACHIEVEMENTS=1` (with `DW4_PROFILE=<file>` to keep the profile)
and `DW4_TRACE_EVENTS=1` (every event as it happens).

### Data Overflow (survival mode)

Offline, a Gekomon stands by the Home lobby's Outside World Gate once the
lobby intro is done and the gate leads somewhere; talking to it alone opens
the battle simulator (with more players it says it takes one Digimon at a time)
(docs/SURVIVAL.md). Choose one of the four Rookies with a starting armour and
chip, then hold out on the Violet Ring while the enemies pour in: the Digimon
strikes on its own, the stick moves it, level-ups offer Forges, Training,
gear and Plugs, a Guardian comes every five minutes, LordKnightmon at 20:00
under the quest timer; beating it clears the run, the timer's 0:00 is TIME
UP. The run's Sim Bits buy permanent plugs in the Plug Lab, Data Records open
more gear. Nothing of a run reaches the story: the story engine waits and
goes on bit for bit. The meta file is `saves/overflow.dat` (a `.bak` beside
it); F1 > Gameplay "Show the simulator NPC" (settings.ini `[overflow] npc`)
hides Gekomon, and F1 > Gameplay "Data Overflow (test)" starts a run
directly. Test runs: `--overflow-run form=p_agumon field=stagbs02 seed=1
bot=kite summary=<file>` (docs/OVERFLOW_DIRECTOR.md, docs/OVERFLOW_HOST.md).

### Content mods

A mod is a folder `mods/<id>` (next to the program, else in the working
folder) with a versioned manifest `mod.ini` and the replacement files under
`files/` at their logical paths, for example
`mods/mydesert/files/loadingusa/ldag0101.igb` for the Death Valley loading
screen or `files/stag0101.igb` for a stage package (models, collision,
scripts and databases live inside the packages):

```ini
[mod]
name=My Desert
version=1.0
author=me
description=Replaces the Death Valley loading screen
priority=10     ; higher wins when two mods replace the same file
game=GDJEB2
```

`dw4_catalog ROOT --find TEXT` lists logical paths and `--read PATH --out
FILE` writes an original file as a template (ROOT may be the disc image). The
F1 menu lists the mods and switches them on or off for the next start
(`[mods] disabled=id,...`). Online sessions only accept machines whose game
data, mods included, match.

### Graphics options

F1 > Settings > Graphics (settings.ini `[graphics]`; every change applies at
once, nothing reaches the game itself, so online partners may differ):

- *Texture quality*: Original (the game's own textures), High (a texture
  pack's 2x level) or Ultra (its 4x level, the default; without a pack both
  show the game's textures). It changes the running stage too: the pack's
  textures come in within a few frames.
- *Texture filtering*: Bilinear (the GameCube's sampling, the default),
  Trilinear (mipmaps for the game's textures: less shimmer on distant
  floors) or Anisotropic 2x to 16x (slanted surfaces stay sharp).
- *Mip LOD bias* (-2 .. 2) for mipmapped textures.
- *Sharpening*: Off, Low or High, a contrast adaptive sharpening pass over
  the game picture (not the menus).
- *Shadow quality*: Original (the game's 128 x 128 shadow map), High (4x)
  or Ultra (8x the texels per side: sharper character shadows).
- *Texture memory*: 512 MiB to 4 GiB for textures; past it the least
  recently drawn ones make room.

The *Original* preset sets all of them to the GameCube's look; *Modernized*
picks Ultra, Anisotropic 16x and the High shadow map. A settings.ini from
before these options keeps the preset it showed (Original or Modernized
graphics). Test runs read no
settings.ini and take `DW4_TEXTURE_QUALITY`, `DW4_TEXTURE_FILTER`,
`DW4_LOD_BIAS`, `DW4_SHARPEN`, `DW4_SHADOW_QUALITY` and `DW4_TEXTURE_MEMORY`
([TEXTURES.md](docs/TEXTURES.md), *Graphics options*).

### Texture packs

A texture pack is a folder (F1 > Settings > Graphics > *Texture pack
folder*, default `mods/textures`, settings.ini `[mods] textures`; *Reload
texture pack* applies changes while playing, the folder read in the
background while the game runs on). Two layouts work:

- **Pack format 2**: a manifest `dw4pack.tsv` lists per texture hash its
  files (BC7, BC3, BC1 or RGBA8 DDS files with their mip chains, or PNG),
  each file's scale over the original and its levels; one 4x file serves
  Ultra and High. The format, the encodings and the rules a pack builder
  follows are in [TEXTURES.md](docs/TEXTURES.md) (*Texture packs: format 2*).
- **Loose files** (format 1): a PNG (or DDS) of any size whose name ends in
  `_<hash>.png` up to 8 folders below the folder replaces that texture (a
  folder of more than 200,000 files and folders is not read as a pack).

Pack textures load on a background thread: while a stage loads its
textures are read ahead, and until a replacement is on the GPU the game's
own texture is drawn, so no frame waits for a file. Identical textures load
once and stay in memory across stages within the *Texture memory* budget.

With *Dump textures* in the F1 menu (or `[mods] dump_textures=true`) every
decoded texture is written once to `mods/dump/textures` as
`<name>_<width>x<height>_<hash>.png`, the name a replacement needs. The hash
of a CMPR texture is taken with its transparent texels black (as decoded
before the GX average colour was kept there), so older packs still match.
`mods` is looked up next to the program first, then in the working folder.
Test runs take `DW4_TEXTURE_PACK=dir` and `DW4_DUMP_TEXTURES=dir`;
`DW4_TRACE_TEXTURES=1` prints each stage's texture memory and streaming
figures.

For pack authors, the development tool `dw4_texpack export --data ROOT --out
DIR` writes every texture of the game data at once (not only those a play
session draws) under the same names, with a manifest of formats, palette
variants, alpha, materials, languages, stages, UV use and atlas cells
([TEXTURES.md](docs/TEXTURES.md)). Keep its output on your machine.
`tools/remaster` turns that export into a 4x pack with super-resolution
models and an edge reconstruction for lettering, and compares the result in
hidden game runs ([tools/remaster/README.md](tools/remaster/README.md)).

`tools/verify_game.py` checks an ISO or extraction against the expected
`main.dol` SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` without copying data.

```bat
dw4_game.exe                    rem boot logos, title, new game
dw4_game.exe --stage stagml11   rem jump straight to the Home lobby
```

`Play-DW4-Game.cmd` starts `build\Game\dw4_game.exe`. Further options:
`--smoke-test FRAMES` (fixed steps as fast as the machine runs them; without a picture, see
below), `--input SCRIPT` (lines
`frame BUTTON [duration [player]]` (BUTTON A, B, X, Y, Z, L, R, START; UP, DOWN, LEFT, RIGHT, which set
the D-pad bit and push the main stick fully (the scripts' combined direction, unchanged so older scripts,
the determinism references and the campaign checkpoints keep their meaning; a GOTO / SEEK steering or a
`STICK` replaces the push); DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT for the D-pad alone; `STICK:x,y` /
`CSTICK:x,y` for the main stick / C-stick at raw device values -128..127; several joined by `+`; the
quick panels' cursor takes the D-pad and the C-stick, walking the main stick), `frame GOTO x z [duration]`, `frame SEEK duration [model-prefix]`
and `frame SEND CATEGORY COMMAND [arguments|- [row]]`; `SEEK duration mat:matz02` walks a route onto
a mat and confirms its prompt, `SEEK duration explore:matz02` plays through the stage to it: it fights
enemies (hovering ones from a ledge its swing reaches them from, turrets in the gaps between their
volleys; on a moving ride such as the stag0103 boat only those it can strike from the ride, guarding
against a shot just before it lands), picks up items, steps on the other mats and explores
until the way is open; `0 SEEK duration
campaign` plays the story from the boot logos: a new game, then the story objectives in order up to
the staff roll, see `src/game/campaign.cpp`; the dng310 fortress is planned over its floors' regions
and the flags that open their doors, the Lucemon fight by a boss routine; without `DW4_ONE_HIT` it
casts its first technique while the MP lasts (MP disks when it runs out) and puts the strongest
devices found on at the Digi-Lab (PWR Medals into the chip pockets of a weapon of rank 1+), and
without `DW4_GOD` it
also guards against enemy blows and touches (measured against the body's real height, its HIT
boxes), heals with HP disks from the L panel (the smallest disk that makes the missing HP up within
10 % of the maximum; with Y at once while blows leave no gap, outside the boss stages only below 20 % while
it holds a Recovery, which Y takes first; in a boss stage the biggest blow of the visit keeps the heals
early), uses heal gates,
the X-Data shop's refill and Gate Disks, sells the weaker devices and restocks at the disk shop (HP
Disks L and Recovery too with bits to spare, every bit's worth before the last boss, Daemon's three stages
with no lobby between them; each lobby errand waits until no other errand's shop or
bank program runs, and one that stalls is left until the next lobby room), goes back to restock on the
way to an objective when its heals were down to half a stock already as it entered the stage (twice an
objective; never from a stage behind the stag0304 cannon climb, `kGauntlets`), donates at the junk shop and wears the
Filter Board it then sells while the way ahead crosses stag0303's swamp fog (`kProtections`),
buys stronger devices at the weapon and armor shops, takes the fire the dng308 / dng309 cell walls
need from a fire PWR Medal (stagML13 chip shop) in the pocket of a rank 1+ weapon (weapon shop,
whose stock is rolled anew on each visit) instead of the Guilmon form, which begins at level 1,
and the thunder dng412's scrap walls need from a thunder PWR Medal in its place,
keeps its bits in the bank while it is out of the lobby (a defeat leaves them in the bit capsule)
and takes them out once per stay before the shops, recovers its bit capsule with a
Raise Disk after a defeat and trains in the area's field below an objective's gate level (a Gate
Disk escape raises that level like a defeat unless the visit set a story flag never seen before: then
it restocks and comes back, at most eight times per objective; where the panel refuses the Gate Disk, stagBS01 and Daemon's stages, the objective is taken up again and the fight goes on; after a defeat in a field it trains in
the area before while short of HP disks); with real blows it strikes the stag0103 boat's towers in
passing, fights e_mammos off its arena's wall and Apocalymon from its dais, dodging the blast pillars,
guards against shells in flight and turret beams (also in its aggressive windows), fights turrets
(a swing the moment the last one ends, inside their hit reaction), leaves structures that heal themselves (the area 3
poison plants) and the ground round them alone unless an objective's gimmick stands behind them (the
stag0301 levers: the plant struck down for the moment its body leaves the way, `Objective::wanted`),
crosses a field to its exit striking only the enemies
that come near, crosses a floor that gave way under it before (dng310f3's fading tiles) without a stop
and keeps the spot a fall began from off its routes for a while, chases no enemy over moving platforms, holds
waves away from pits and quicksand sinks (an enemy off the sand is walked to), takes no guard with a
pit, quicksand or a deep drop behind it (a wall before the drop ends the push), fights Belphemon, dng412b4's knight (off its homing orbs, struck where its walk will carry it when the swing lands) and Daemon's forms by boss routines (the second form's blast rings walked off from its wind-up on, swings only; the last form struck from a spot outside the shape of its body boxes, kept out of the way its flights and sideways slides will sweep and off the orbs it sets out), guards shots still queued on their shooter and slow homing shots by their speed, walks away from a body after two touch guards that blocked nothing, cures poison with Cure Disks, keeps off and leaves alone structures seen blowing up where they stand (stag0401's mines), steps on stag0403's floor switches in the solution's order by the states their buttons show (one it was thrown back onto, which turned it off again, is stepped off and on once more; `DW4_MAT_SWITCH`), hunts the members of waves whose clearing powers or opens something even while crossing a field (stag0304's switches; all of stag0404's tower), at the last stand (Daemon's chain from stag0401 to stagBS06, no shop on the way) sells every device not worn, keeps the HP Disks L and Recovery for the last form, farms stag0201 for the bits of a full stock before an attempt it cannot pay and gives a stalled boss stage of the chain up (no guard, no heal: the defeat starts the chain anew), and dodges Lucemon's bomb rows from their markers
(omens: a blast's rings known before its boxes exist); a field step that stalls for 180000 frames leaves the field and comes
back; with `DW4_CAMPAIGN_CHECKPOINTS=dir` it saves the game
like a player at nine points of the story where it returns to the lobby (the save Digi-Elf in
stagML12, `kCheckpoints`) and copies Card A to `dir/<checkpoint>`, with `DW4_CAMPAIGN_LOAD=1` it
calls the saved character of Card A at the title instead of starting a new game, so a story run can
be split into segments that start from such saves and run side by side (memory card saves stay
valid when the engine changes, save states do not); `DW4_CAMPAIGN_STOP=1` ends a run once it has
saved its first checkpoint or the story is done, `DW4_CAMPAIGN_STOP=end` only at the story's end),
`--screenshot FILE.bmp`,
`--screenshot-frames N,M`, `--flag N=V`, `--load-state FILE` (start from a save state file; with `DW4_STAGE_STATES=dir` a test run leaves one at
every stage start, `<frame>_<stage>.dw4state`, and the campaign driver's memory beside it, `<frame>_<stage>.mem`,
which `--load-state` reads back; `DW4_CHEATS_FROM_ENV=1` replaces the state's test cheats by
the environment's from the loaded frame on, and `DW4_LEVEL_ON_LOAD=n` / `DW4_DISKS_ON_LOAD=record:count,...`
set player 1's level and disk counts on the loaded state, to try a later stage at the level and
with the supplies a no-cheat run would bring there).
A test run draws no pictures unless it needs them: without `--screenshot` / `--screenshot-frames`,
`DW4_TEXTURE_PACK` or `DW4_DUMP_TEXTURES` it opens no window and no OpenGL context, decodes no
movies and mixes no sound (unless `DW4_AUDIO_DUMP`); with the faster collision queries and route
planning a campaign or survey run simulates about ten times faster than before (a campaign segment
530 -> 5300 frames/s, four side by side 295 -> 3400 frames/s each). The scene is still
traversed after every step exactly as for a picture (`Engine::traverse_scene`: animation clocks,
motion combiners and recorded matrices advance there), so such a run plays the same frames as one
that draws them: the fingerprints, logs and stage states are identical. `DW4_RENDER=1` draws the
pictures anyway in a hidden window (the old behaviour; `DW4_TRACE_SHADOW` needs it); the last
frame's draw list is collected once at the end for the `draws` count and `DW4_DRAW_DUMP`.
Test-run environment variables: `DW4_GOD` keeps players alive, `DW4_ONE_HIT` makes
player hits defeat their target, `DW4_INFINITE_MP` keeps MP full,
`DW4_DEVICES=type:record:rank:value,...`, `DW4_DISKS=record:count,...` and
`DW4_BITS=n`, `DW4_XDATA=n`, `DW4_LEVEL=n` and `DW4_FORMS=species,...` fill a new player's folders, level and forms,
`DW4_FLAG_FOLDER=item,...` gives key items (story progress items such as boss IDs), `DW4_DIFFICULTY=n` sets the
difficulty (0-2) before the first stage loads, `DW4_CARD_RECORD=FILE.gci:N` makes player 1 the character of
save box N (as the character select loads it; FILE may be a `savedata.dat`), `DW4_ENTRY=n` starts a
test run's first stage at player point group n (as when arriving from another exit),
`DW4_PLAYERS=n` joins n players, `DW4_OPEN_MENU=1` opens the F1 menu (`DW4_MENU_TAB=Saves` on that tab,
`DW4_MENU_IMPORT=FILE` with that file's import preview), `DW4_MIGRATE_FROM=<GCI root>` (with `--save-file`) copies
that root's `Card A` / `Card B` saves in as a first start copies `saves/GC/USA`,
`DW4_FAST_LOADING=1` loads with the fast loading option (`--loading original|fast` overrides it),
`DW4_TRACE_LOADTIME=1` prints for every stage load the frames to the stage start and to player 1's
control and the real time the load's frames took (`=2` every frame of the load with the loader state),
`DW4_WINDOW=WxH` and `DW4_WIDESCREEN=1` size test windows, `DW4_SMOKE_STAGES=n` ends a test run
once the n-th stage has loaded,
`DW4_TRACE_SELECT`, `DW4_TRACE_TEXT`, `DW4_TRACE_PLAYER`, `DW4_TRACE_HIT`, `DW4_TRACE_EXPLORE` and
`DW4_TRACE_CAMPAIGN` log menu cursors, text, player state, hits, the exploring bot's targets and the
campaign driver's menu answers, heals, guards and trainings (`DW4_TRACE_GUARD` every enemy attack box
the guard looks at, `DW4_TRACE_HITBY` the attack boxes near the player at every blow it takes; test aids: `DW4_CAMPAIGN_IDLE=n` lets go of the controls for n frames once player 1 has control, a run from the same save played on from another moment, `DW4_FARM_FIELD=<stage>` and `DW4_FARM_TEST=1` pick and force the last stand's farming), `DW4_TRACE_STATS` every player status calculation, `DW4_TRACE_PANEL` the quick
panels' selected line whenever it changes; `DW4_TRACE_SCRIPT=1` (or a
script name) logs script runs and `DW4_TRACE_SEQ=<model>` a model's sequence changes
(`DW4_DUMP_SEQ=<model>` lists all its sequences with their commands). Campaign research reports
(they change the stage): `DW4_CAMPAIGN_REGIONS=wall,...` prints a stage's regions with those
HITLANDMODEL walls closed and what each wall joins, `DW4_CAMPAIGN_REACH=...` what is reached from
the player (or `DW4_CAMPAIGN_REACH_FROM` points) as walls are switched off; `DW4_CAMPAIGN_STALL_DUMP=1`
lists the enemies' control, freeze, HP and sequence once a campaign step has lasted 20000 frames.
`DW4_TRACE_FINGERPRINT=N` prints the state fingerprint (and a hash of the random generator) every N
frames: two runs of the same input script must print the same lines, so the first differing line
is the first frame where a run diverged. `DW4_FIXED_RANDOM=<hex>` makes every draw of the game's random
generator return that constant (the Dolphin side: AR `0421EB40 3C60hhhh`, `0421EB44 6063llll`,
`0421EB48 4E800020`), for frame-exact comparisons of fights. `DW4_CLOCK_FILE=<path>` with lines
`<frame> <world ns>` sets the world clock to that value in the step that reaches the frame (the
other frames keep adding one field), so a port run can replay the clock a Dolphin run logged,
jitter included.

Dev presentation tools (test runs only, off unless set; `src/game/dev_present.hpp`): a test run
normally runs one step per pass and never blends, so it cannot show what a high refresh rate with
smooth motion presents. `DW4_DEV_PRESENT=HZ` presents at a fixed virtual refresh instead: every pass
adds 1/HZ s to the step accumulator and runs the steps it covers, exactly as interactive play does
with the real clock, and smooth motion blends as there (HZ above 75). `DW4_DEV_SETTINGS=FILE` takes
the window (`width`, `height`, `widescreen`, `msaa`, `smooth_motion`), `loading` and `skip_intro`
from a copy of a `settings.ini` (only read; with `skip_intro` the run boots at the title like the
player's), `DW4_DEV_SMOOTH=0|1`, `DW4_DEV_WIDESCREEN=0|1`, `DW4_DEV_MSAA=n` and
`DW4_DEV_WINDOW=WxH` override single values. `DW4_DEV_DUMP=FROM-TO[,FROM-TO...]` with
`DW4_DEV_DUMP_DIR=dir` writes every picture presented during those steps (`present_NNNNNN.png`,
counted over the run) and a line per present in `presents.csv` (step, blend factor, and what the
blend did: items blended, unmatched, too far, swapped occurrences, jumps, sequence wraps, palettes,
texture matrices, camera and the camera's change in this and the previous step);
`blend_NNNNNN.txt` lists the items the blend flagged. `DW4_DEV_PICTURES=0` writes only the CSV and
the blend lists (fast, whole routes). `DW4_DEV_IDS=1` adds an item-id picture per present
(`ids_NNNNNN.png`: RGB = draw list index + 1, the list drawn again without blending and
multisampling) and `items_NNNNNN.txt` (index, node, kind, IGB file and object, texture, flags,
translation, 3x3, UV scroll, placement and sequence phases), so every pixel names its draw item. `DW4_DEV_HIDE=pat,...` leaves items
out whose node or texture name contains a pattern, or a category (`:blend`, `:opaque`, `:2d`, `:3d`,
`:skinned`, `:particles`, `:texmatrix`, `:shadow`), for A/B runs. `tools/flicker.py DUMP_DIR`
(numpy, Pillow) finds flicker in such a dump: pixels that leave their value for one to three presents
and come back (and, with item ids, pixels whose draw item does that), a heat map, the hot regions and
the items owning them; `tools/blend_report.py DUMP_DIR` groups the blend's flagged items (`wrap`: a
sequence that wrapped, stepped or whose clock went back, drawn as it is; `jump`: a change far larger
than the step before that no sequence marked, still blended; `swap(near)`: blended from another
occurrence's place; `texture_jump`: a texture matrix drawn as it is while its model blends). Example, the lobby after loading a save at
144 Hz: `DW4_DEV_SETTINGS=settings_copy.ini DW4_DEV_PRESENT=144 DW4_DEV_DUMP=4790-4800
DW4_DEV_DUMP_DIR=dump DW4_DEV_IDS=1 dw4_game --data GAME --card-dir CARDS --input load.txt
--smoke-test 4800`, then `python tools/flicker.py dump`.

### Controls

| Original | Keyboard (default, rebindable) | Gamepad (default, rebindable) |
| --- | --- | --- |
| Main stick (walk, menus) | W / A / S / D | Left stick |
| D-pad (menus, quick panel cursor) | Arrow keys | D-pad |
| C-stick (quick panel cursor) | Keypad 8 / 4 / 2 / 6 | Right stick |
| A (talk, confirm, use the panel's line) | Enter, Space, K | South (Nintendo pads: the A button) |
| B (cancel) | Backspace, L | East (Nintendo pads: the B button) |
| X (jump) | J | West (Nintendo pads: the X button) |
| Y | I | North (Nintendo pads: the Y button) |
| START | Esc, Tab | Start |
| L / R (disk / technique panel) | Q / E | Left / right trigger (L also on the left shoulder) |
| Z | Z | Right shoulder |

As on the GameCube the three direction inputs are separate: the main stick
walks, the quick panels (R techniques, L disks) move their cursor with the
D-pad or the C-stick only (8034A24C), so walking with a panel open never picks
another line, and menus take the main stick and the D-pad. With the default
keys the left hand walks with WASD and opens the panels with Q / E, the right
hand picks the line with the arrow keys and Space or Enter uses it.

Player 1 uses the keyboard and the first gamepad; the second to fourth
gamepads control players 2 to 4. As on the original, holding B and X and
then START for a second on player 1's controller returns to the title
(outside the logo, opening, title and ending stages). Holding B, X and START
together on any controller for more than three seconds presses the console's
reset button, as the GameCube controller does: the game reboots with the
license screen, the memory card check, the logos and the opening (memory
cards, mods and PC options stay).

#### Controllers and button pictures

The game recognizes the controller in use and draws every button picture for
it, in the game's own texts (the GameCube button symbols, the word START, the
title's PRESS START) and in the port's menus and notices. The pictures follow
the last device player 1 used (keyboard or controller) from the next frame on;
in the character select every player's own lines show his own controller's
pictures. F1 > Controls > *Button prompts* can fix one family instead. Each
picture shows the button bound to that game button, so rebinding changes it.
All pictures are drawn by the program itself (no console logos).

| Controller | How it connects on Windows |
| --- | --- |
| DualSense (PS5), DualShock 4 (PS4) | USB or Bluetooth, nothing to install |
| Xbox 360, Xbox One, Xbox Series | USB, Bluetooth or the Xbox wireless adapter (XInput) |
| Switch Pro Controller | USB or Bluetooth. Read by the letters on its buttons (A is A) unless F1 > Controls > *Nintendo layout* says by position |
| Wii U Pro Controller | Bluetooth: Windows Settings > Bluetooth > Add device, press the red SYNC button, no PIN. *Wii U Pro support* (F1 > Controls) is automatic: SDL's Wii driver runs only while such a controller (057e:0330) is connected, so a Wii remote bar such as Mayflash's DolphinBar never reaches it; Wii remotes never take a player's place |
| Original GameCube controller, adapter in PC mode (Mayflash, EVORETRO) | Set the adapter's switch to PC; no driver. Every port is its own controller |
| Original GameCube controller, adapter in Wii U mode (Nintendo's WUP-028, Mayflash on Wii U) | Install the WinUSB driver on "WUP-028" with Zadig, as for Dolphin; the game finds the adapter when it is plugged in and every connected port is a controller. It lets go of the adapter while the game is in the background (Dolphin can use it then); F1 > Controls says when the driver is missing or another program holds it |
| Switch 2 Pro Controller | USB-C cable only (it does not play over Bluetooth). Nothing to install: Windows runs it with its own drivers (its HID driver for the buttons, the WinUSB driver Windows loads by itself on the part through which the game starts it). Read like the Switch Pro (by its labels unless F1 > Controls > *Nintendo layout* says by position); ZL and ZR are digital on this controller; C, GL and GR do nothing until bound in F1 > Controls. Steps below |

**Switch 2 Pro Controller, step by step.** (1) Connect the controller with a
USB-C data cable (a charge-only cable shows nothing) and start the game, or
plug it in while the game runs; it becomes a player's controller with the
Switch 2 Pro pictures within a second or two, and its player lights show the
player. F1 > Controls shows a line for it: *playing, 250 reports/s* when it
works. (2) If F1 > Controls instead says what is wrong, do that: *another
program is using it* - Steam with its Switch controller support, an emulator,
a controller tool or a second copy of the game holds it (the part through
which the game starts it, or its buttons for itself): close that program (the
game tries again every two seconds); *its input device cannot be opened* or *a driver
installed on it is in the way* - a driver was put on the controller by hand
(Zadig: WinUSB or libusbK on one of its parts): Device Manager > View >
Devices by connection, find the controller, uninstall that part with
*Attempt to remove the driver for this device* ticked and plug the controller
in again, Windows sets its own drivers up again; *Windows has no driver on
its interface 1* - unplug it and plug it in again or use another USB port
(only if that does not help: [Zadig](https://zadig.akeo.ie), Options > List
All Devices, the entry whose USB ID reads `057E 2069 01`, WinUSB; never the
entry ending in `00`: that part must keep Windows' own HID driver, the game
reads the buttons through it); *it did not start* - unplug it and plug it in
again, and send the log if it stays. Over Bluetooth it does not play; Windows
does not list it as a controller then, so the game may say nothing (it says
*connect it with a USB cable* only when Windows lists it as a Bluetooth input
device). How it works: the game reads the controller itself, the way Steam
does on Windows: the buttons through Windows' HID driver on its interface 0,
the start commands (taken from SDL's own Switch 2 driver) through WinUSB on
its interface 1; each connected controller becomes an SDL pad with the
controller's ids, so the pictures, the bindings and the player slots treat
it like any other. It keeps reading while the game is in the background (the
game ignores its buttons there, as every controller's). If Windows drops the
connection for a moment without the controller being unplugged (standby, a
USB reset), the game opens it again a second later and it plays on in the
same player slot; only when Windows no longer lists it does its pad go. Earlier builds shipped
a `libusb-1.0.dll` (and `libusb-1.0-COPYING.txt`) for this controller; nothing
uses them any more, they can be deleted. `saves\controllers.log` beside the game records each start's
controllers, what Windows lists and every step of the Switch 2 Pro
Controller's start (send it in when a controller does not work).

A GameCube controller plays 1:1 (its A is the game's A, L and R count at the
trigger's click, the C-stick is the C-stick) and the game keeps its own
GameCube pictures for it; F1 > Controls > *GameCube controller* can put it on
the bindings instead. Settings.ini keeps the choices in `[gamepad]`:
`prompts` (`auto`, `dualsense`, `dualshock4`, `xbox360`, `xbox_one`,
`switch_pro`, `switch2_pro`, `wiiu_pro`, `gamecube`, `keyboard`),
`nintendo_layout` (`label` / `position`; a file whose face buttons were
rebound before keeps `position`, so those bindings play as before),
`gamecube_layout` (`original` / `bindings`), `wii_driver` (`auto` / `on` /
`off`), `menu_buttons` and `gc_adapter` (`true` / `false`). The menus (F1, the
online screens, the on-screen keyboard) take the controller's own buttons:
A / Cross confirms, B / Circle goes back, whatever the game's bindings (a
Nintendo pad as its Nintendo layout reads it); `menu_buttons=bindings` makes
them follow the game's A and B bindings instead. Details:
[docs/ROUND_PADS_NOTES.md](docs/ROUND_PADS_NOTES.md),
[docs/ROUND_SW2WIN_NOTES.md](docs/ROUND_SW2WIN_NOTES.md) (the Switch 2 Pro
Controller; [docs/ROUND_LIBUSB_NOTES.md](docs/ROUND_LIBUSB_NOTES.md) the
earlier libusb round).

F1 opens the menu: the Settings tab (an Original / Modernized preset, the
loading speed (Original / Fast), display, widescreen, frame limit, anti-aliasing,
window size, volume, name tags, Discord, achievements), the Controls tab (the
detected controllers, the button pictures, the Nintendo and GameCube layouts,
keyboard and controller bindings), the Saves tab (where the save lives, the boxes of slots A and B,
importing GameCube saves), the Achievements tab, the Cheats tab
(god mode, infinite MP, one-hit defeats, refills, levels and bits per player,
session only and off by default; plus the saved rebalancing and randomizer
options, see *Randomizer*) and the Debug tab (stage loader, player state,
notes, script log). F11 toggles fullscreen,
F2 a free camera.

### Saves

The game saves like on the GameCube: in the save room, onto the memory card
in slot A or B, four boxes per card. The PC version keeps each card's Digimon
World 4 file in one file next to the program: `saves/savedata.dat` (slot A)
and `saves/savedata_b.dat` (slot B; the slot holds a card once that file
exists, or with `[data] card_b = inserted`). The file holds exactly the bytes
of Dolphin's `B2-GDJE-NDMWSaveData.gci`, so renaming it moves a save to
Dolphin and back. Every save first keeps the previous file as
`savedata.dat.bak`, and the new file replaces the old one only once it is
completely written. The F1 menu's Saves tab and the Settings tab show where
the save lives.

- First start: a save of an older build in `saves/GC/USA/Card A` (or
  `Card B`) is copied into `savedata.dat` (`savedata_b.dat`) once; the old
  file stays where it is (`[data] save_migrated` in settings.ini records it).
  Should the copy fail, the game says so and tries again at the next start; a
  damaged old save is not copied, and the game says that too (a notice at the
  start and the F1 Saves tab).
- `[data] cards = <folder>` plays from a GCI folder root instead (for example
  Dolphin's `GC/USA` folder, so both play on the same save). `--card-dir`,
  `DW4_CARD_DIR` and test runs use GCI folders too (`saves/GC/USA` next to the
  program in test runs).
- Importing GameCube saves (F1, Saves tab, or `--import-save`): Dolphin /
  GCMM / Swiss `.gci`, GameShark `.gcs`, MaxDrive / Action Replay `.sav`,
  memory card images `.raw` / `.gcp` (Dolphin, Nintendont, card dumps) and
  GCI folders. The preview shows each box (name, Digimon, level, bits, story
  phase, forms); the chosen boxes go into the chosen boxes of slot A or B, or
  the whole save replaces the current one. The current save is first copied
  to `saves/backups/savedata-<date>-<time>.dat`. The game keeps no play time
  per box; the preview shows when the file was last saved (its card
  directory entry's time).
- Command line: `dw4_game --save-info FILE` prints the preview,
  `dw4_game --import-save FILE[:BOXES] [--import-slot A|B]` imports (`BOXES`:
  `all`, the default, `1,3` for the same boxes or `1>2,4>1` to move them)
  into the save files (or `--card-dir DIR` / `--save-file FILE`), then
  exits. `--save-file FILE` makes any run (also a test run) use that file
  for slot A (`<name>_b<ext>` for slot B). Details: `docs/SAVES.md`.

Save states: F5 saves and F9 loads state 1; the Settings tab has four
slots, kept as `saves/states/state<N>.dw4state` next to the program. A state is the snapshot taken when
the running stage loaded plus every controller input since, replayed on a new
engine (a few hundred steps per second of play load in well under a second);
the game is deterministic, so the loaded state matches the saved one exactly
(`DW4_STATE_TEST=N` checks this in test runs: save at frame N, load at the end
and compare every frame again; `DW4_STATE_FILE=path` also sends the state
through a file). They work after the title screens.

## License

The repository's own code is MIT licensed (see [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). It does not license
Digimon World 4 or its content. Game data and generated disassembly are never
committed.
