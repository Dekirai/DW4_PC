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
  windows, overwrite check, timed write). Cards are Dolphin-style GCI folders
  (`saves/GC/USA/Card A` next to the program, or `--card-dir`), so Dolphin
  saves can be copied in and out.
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
- The opening movie after the logos: the Sofdec file's program stream is
  split natively; its MPEG-1 video is decoded with pl_mpeg and its ADX sound
  plays through the sound mixer; A or START skips it.
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
| `src/game` | `dw4_game`: SDL3 window, OpenGL renderer, input mapping, settings and the F1 menu |
| `tools` | `dw4_inspect` (IGB inspection), `dw4_catalog` (logical resource catalog), `verify_game.py` (read-only ISO identification) |
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
DLLs) with the tools, licenses and docs, ready to copy anywhere. Builds never
start the game.

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
picture blends between the last two updates) and `msaa` (2/4/8 samples, applied at start-up),
`[audio] volume` (percent), `[menu] pause` (pause while the menu is open),
`[preferences] skip_intro` (boot straight to the title), `[gameplay]`
`exp_rate`, `damage_dealt`, `damage_taken` (percent, 100 = original) and
`drop_seed` (item drop randomizer, 0 = off; key items stay), `[data] cards` (the
memory card folder with `Card A` / `Card B`, default `saves/GC/USA` next to the
program; Dolphin's `GC/USA` folder works too), `[data] card_a` / `card_b` (what
the slots hold: `none` for an empty slot, `unformatted`, and / or the card's
blocks such as `251`; default a 2043-block card in slot A and one in slot B when
its folder exists, also `DW4_CARD_A` / `DW4_CARD_B`) and
`[keyboard]` with up to three key names per button (`a=Return, Space, K`) and
`[gamepad]` with up to three SDL controller inputs per button (`l=lefttrigger,
leftshoulder`).

### Online co-op

Up to four machines play one session in lockstep (TCP, the host relays). The
F1 menu's Online tab hosts (players, port, input delay) or joins a host
address; everyone then starts at the title together. From the command line:

```bat
dw4_game.exe --host --players 2            rem waits for one guest on port 52040
dw4_game.exe --join 192.168.1.20           rem or --join host:port
```

`--port N` and `--net-delay N` (input delay in steps, default 3) adjust the
session. Every machine starts the same way (the host's stage and cheats) and
each step waits for every player's input, so the simulation stays identical;
guests report state fingerprints and a desync shows on screen. Each machine's
keyboard and first controller play its own player (host 1, guests 2-4). The
host's Card A and the first guest's Card A (as Card B) are copied to every
machine, so both can call their Digimon at the character select; when the
session ends each owner gets its card back with what was saved. Save states
are off online; a guest that leaves plays on released, a host that leaves ends
the session for the others (they play on alone). The host needs the port
open (router / firewall) for guests outside the local network.

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

### Texture packs

With *Dump textures* in the F1 menu (or `[mods] dump_textures=true`) every
decoded texture is written once to `mods/dump/textures` as
`<name>_<width>x<height>_<hash>.png`. A PNG of any size whose name ends in the
same `_<hash>.png` anywhere below `mods/textures` (or the `[mods] textures`
folder) replaces that texture; *Reload texture pack* applies changes while
playing. `mods` is looked up next to the program first, then in the working
folder. Test runs take `DW4_TEXTURE_PACK=dir` and `DW4_DUMP_TEXTURES=dir`.

`tools/verify_game.py` checks an ISO or extraction against the expected
`main.dol` SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` without copying data.

```bat
dw4_game.exe                    rem boot logos, title, new game
dw4_game.exe --stage stagml11   rem jump straight to the Home lobby
```

`Play-DW4-Game.cmd` starts `build\Game\dw4_game.exe`. Further options:
`--smoke-test FRAMES` (hidden window, fixed steps), `--input SCRIPT` (lines
`frame BUTTON [duration [player]]` (BUTTON A, B, X, Y, Z, L, R, START, UP, DOWN, LEFT, RIGHT or `STICK:x,y`
with raw stick values -128..127, several joined by `+`), `frame GOTO x z [duration]`, `frame SEEK duration [model-prefix]`
and `frame SEND CATEGORY COMMAND [arguments|- [row]]`; `SEEK duration mat:matz02` walks a route onto
a mat and confirms its prompt, `SEEK duration explore:matz02` plays through the stage to it: it fights
enemies (hovering ones from a ledge its swing reaches them from, turrets in the gaps between their
volleys), picks up items, steps on the other mats and explores until the way is open; `0 SEEK duration
campaign` plays the story from the boot logos: a new game, then the story objectives in order up to
the staff roll, see `src/game/campaign.cpp`; the dng310 fortress is planned over its floors' regions
and the flags that open their doors, the Lucemon fight by a boss routine; without `DW4_ONE_HIT` it
casts its first technique while the MP lasts (MP disks when it runs out) and puts the strongest
devices found on at the Digi-Lab, and without `DW4_GOD` it
also guards against enemy blows and touches, heals with HP disks from the L panel, uses heal gates,
the X-Data shop's refill and Gate Disks, sells the weaker devices and restocks at the disk shop,
buys stronger devices at the weapon and armor shops, recovers its bit capsule with a
Raise Disk after a defeat and trains in the area's field below an objective's gate level (a Gate
Disk escape raises that level like a defeat; after a defeat in a field it trains in the area before
while short of HP disks); with real blows it strikes the stag0103 boat's towers in passing, fights
e_mammos off its arena's wall and Apocalymon from its dais, dodging the blast pillars; a field step
that stalls for 180000 frames leaves the field and comes back),
`--screenshot FILE.bmp`,
`--screenshot-frames N,M`, `--flag N=V`, `--load-state FILE` (start from a save state file; with `DW4_STAGE_STATES=dir` a test run leaves one at
every stage start, `<frame>_<stage>.dw4state`, and the campaign driver's memory beside it, `<frame>_<stage>.mem`,
which `--load-state` reads back; `DW4_CHEATS_FROM_ENV=1` replaces the state's test cheats by
the environment's from the loaded frame on).
Test-run environment variables: `DW4_GOD` keeps players alive, `DW4_ONE_HIT` makes
player hits defeat their target, `DW4_INFINITE_MP` keeps MP full,
`DW4_DEVICES=type:record:rank:value,...`, `DW4_DISKS=record:count,...` and
`DW4_BITS=n`, `DW4_XDATA=n`, `DW4_LEVEL=n` and `DW4_FORMS=species,...` fill a new player's folders, level and forms,
`DW4_FLAG_FOLDER=item,...` gives key items (story progress items such as boss IDs), `DW4_DIFFICULTY=n` sets the
difficulty (0-2) before the first stage loads, `DW4_CARD_RECORD=FILE.gci:N` makes player 1 the character of
save box N (as the character select loads it), `DW4_ENTRY=n` starts a
test run's first stage at player point group n (as when arriving from another exit),
`DW4_PLAYERS=n` joins n players, `DW4_OPEN_MENU=1` opens the F1 menu,
`DW4_WINDOW=WxH` and `DW4_WIDESCREEN=1` size test windows, `DW4_SMOKE_STAGES=n` ends a test run
once the n-th stage has loaded,
`DW4_TRACE_SELECT`, `DW4_TRACE_TEXT`, `DW4_TRACE_PLAYER`, `DW4_TRACE_HIT`, `DW4_TRACE_EXPLORE` and
`DW4_TRACE_CAMPAIGN` log menu cursors, text, player state, hits, the exploring bot's targets and the
campaign driver's menu answers, heals, guards and trainings (`DW4_TRACE_GUARD` every enemy attack box
the guard looks at), `DW4_TRACE_STATS` every player status calculation; `DW4_TRACE_SCRIPT=1` (or a
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

### Controls

| Original | Keyboard (default, rebindable) | Gamepad (default, rebindable) |
| --- | --- | --- |
| Stick | Arrow keys / WASD | Left stick, D-pad |
| A (talk, confirm) | Enter, Space, K | South |
| B (cancel) | Backspace, L | East |
| X (jump) | J | West |
| Y | I | North |
| START | Esc, Tab | Start |
| L / R | Q / E | Left / right trigger (L also on the left shoulder) |
| Z | Z | Right shoulder |
| C stick | | Right stick |

Player 1 uses the keyboard and the first gamepad; the second to fourth
gamepads control players 2 to 4. As on the original, holding B and X and
then START for a second on player 1's controller returns to the title
(outside the logo, opening, title and ending stages).

F1 opens the menu: the Settings tab (an Original / Modernized preset, display,
widescreen, frame limit, anti-aliasing,
window size, volume, keyboard and controller bindings), the Cheats tab
(god mode, infinite MP, one-hit defeats, refills, levels and bits per player,
session only and off by default; plus the saved rebalancing and randomizer
options) and the Debug tab (stage loader, player state,
notes, script log). F11 toggles fullscreen,
F2 a free camera.

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
