# Development

Everything developer-facing about DW4_PC: what the port is technically, the
repository layout, building and testing on Windows, the validation rules, the
test-run switches and the online server. Players find their guide in
[PLAYER_GUIDE.md](PLAYER_GUIDE.md); the landing page is the
[README](../README.md).

## Contents

- [What the project is](#what-the-project-is)
- [Reconstruction status](#reconstruction-status)
- [Repository layout](#repository-layout)
- [Building on Windows](#building-on-windows)
- [Local validation](#local-validation)
- [Tests](#tests)
- [Test runs](#test-runs)
- [Feature notes and test switches](#feature-notes-and-test-switches)
- [Online development](#online-development)
- [Online server (DW4Online)](#online-server-dw4online)
- [Release builds and the server pins](#release-builds-and-the-server-pins)
- [Further documentation](#further-documentation)

## What the project is

A native PC port project for **Digimon World 4 (USA, GameCube, `GDJEB2`)**.

The game and engine are reconstructed as authored C++20 source from the
original `main.dol` and game data: the original `be*` managers, their
messenger, the embedded Lua 4 stage scripts and the Alchemy (IGB) scene graphs
run natively. No emulator, recompiled PowerPC code or original executable
bytes are part of this repository. Users supply their own copy of the game.
Builds and tests run locally; GitHub Actions is disabled (see
[Local validation](#local-validation)).

**It is not yet a complete or 1:1 playable reconstruction.** The game is
playable from a new game to the cleared game (automated story runs, also
without test cheats, see [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md)), but
smaller managers are still missing.

Contributor rules (English everywhere, no game data in Git, no hosted
workflows, the validation expected per change) are in
[AGENTS.md](../AGENTS.md).

## Reconstruction status

What runs today:

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
  `saves/savedata.dat` next to the program (see
  [PLAYER_GUIDE.md](PLAYER_GUIDE.md#saves)); GameCube saves from Dolphin,
  GCMM, GameShark, MaxDrive / Action Replay and memory card images import from
  the F1 menu's Saves tab.
- Lobby areas (`stagML11`, Central Area, Shop Area, Commander Room):
  first-visit story scenes, dialogs with portraits and prompts, talking to
  NPCs, walking, jumping, floor gates with the confirmation dialog and stage
  changes.
- The stage-select pad to Death Valley and its practice tutorial: lesson
  texts, enemy generators (GENERATER), weapon attacks with the original hitbox
  queue, FIGHTCALC damage, critical hits and damage reactions, the guard and
  hold-A attack chains, stage-end demo cameras and the following lessons.
- Death Valley combat: the original AIM enemy AI, enemy attacks and damage,
  player defeat with the bit capsule, EXP and level-ups with the HUD meters,
  hit effects, and item drops from the generator tables (bits picked up on
  contact, disks, key items and rolled equipment picked up with A, one
  floating item name per player, and only that item is picked up).
- Quick panels (L disks, R techniques, Z status, Y emergency heal), MP
  techniques with magic damage, and the START status window with its equipment
  page: weapon socket (reorder, chip pockets), armor and sub slots, the device
  folder list with pages, level/skill checks and stat previews; the folder
  page (discard, sort, give devices, disks and bits) and the skill page (forms,
  weapon skills, techniques).
- Lobby programs: weapon, armor and chip shops with their level-based stock
  and stat previews, the disk shop, selling, the junk shop with bit donations,
  the bank (devices and bits), the X data keeper (stats and chip upgrades), the
  appraiser for unknown devices and the Digi-Lab: digivolution into the forms
  reached (each form keeps its level, EXP, skills and stat ups), the strongest
  usable weapon equipped, then the evolution scene; the card keeper opens
  booster packs (three cards drawn by their weights, named in their rarity
  colours) and shows the album of the 50 cards.
- Status effects of players and enemies: poison ticks, sleep, stun, bind,
  attack and power down (plus the speed/power up, refract and metal commands)
  with the original FIGHTCALC chances and times, their DOWN/UP values in the
  stat recalculation and damage, and the icons above the model; the support
  techniques (heal, heal all, cure, speed and power up, refract, raise: a
  defeated player's bit capsule brings the player back, and with it the bits
  and part of the EXP lost at the defeat) and the weapon and technique skill
  growth with the skill up panel; the status quick panel lists the active
  effects, and the cure and raise disks work.
- The loading screens of every stage change (the "Licensed by Nintendo" screen
  at boot, the area title screens before stages and dungeons) with their
  minimum play time, and the box camera: the camera pivot follows the players'
  centre and each camera box of the stage turns and distances the camera,
  blended in as the players cross into it; camera shakes.
- The opening movie after the logos and in the title's attract mode: the
  Sofdec file's program stream is split natively; its MPEG-1 video is decoded
  with pl_mpeg and its ADX sound plays through the sound mixer, with the
  original's 30 Hz movie frames, Sofdec's start and end and its disc reads; A
  or START skips it.
- Music and ambience: the stage sound lists' BGM and ambience streams are the
  CRI ADX files of ndmw.afs, decoded natively (loop points, volume, the
  fade-out before a change) and played through SDL3 audio.
- Sound effects: the CRI Sound Factory banks (the common allse.sfp and each
  stage's banks) with their request sequences, drum-program splits and DSP
  ADPCM samples, triggered by menus, scripts and the models' SE sets (the
  sounds each action plays, such as jumps, landings, footsteps and swings).
- Rendering of original models, textures, skinned and animated actors (Enbaya
  clips) with the weapon parts on their bones, UV animations, text from the
  original fonts and the HUD layers, the projected character shadows (the
  shadow proxy meshes drawn from a light above the players onto the stages'
  receiver meshes), the stages' light sets and the particle effects (torches,
  warps, hits).

Collision follows the original HITLANDMODEL queries and HITLANDDELIVER
responses (walls, floors, falling and landing, the players' limit circle),
including rides on boats, trains and moving platforms and the conveyor,
quicksand and slippery floors; many smaller managers are still missing. See
[ROADMAP.md](ROADMAP.md) and [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).

## Repository layout

| Part | Contents |
| --- | --- |
| `include/dw4`, `src` | `dw4_core`: IGB documents, packages and resources, textures, meshes, skinning, Enbaya animation, Lua 4 bytecode, pads and motion primitives |
| `include/dw4/engine`, `src/engine` | `dw4_engine`: frame step, messenger and demo manager, Lua host, scene graph, and the reconstructed managers (stage loader, scene/layer/camera, model control, actions, text, menus, title, character select, floor mats, collision) |
| `src/game` | `dw4_game`: SDL3 window, OpenGL renderer, input mapping, settings and the F1 menu, the save store and GC save import, online sessions, Discord Rich Presence, name tags, achievements |
| `server/DW4Online` | The online backend (ASP.NET Core + MariaDB: accounts, online character, cloud save, achievements, the online world, relay, website), its tests and deployment kit; built with `dotnet`, separate from the CMake build |
| `tools` | `dw4_inspect` (IGB inspection), `dw4_catalog` (logical resource catalog), `dw4_texpack` (texture export for pack authors), `remaster/` (4x upscaling pipeline for texture packs), `verify_game.py` (read-only game identification), `flicker.py` and `blend_report.py` (presentation dumps), `online_test/` (the local online stack) |
| `tests` | Asset-free tests for the core formats and tools |
| `data` | Committed data that is not game data (language pack manifests and picture hashes, online definitions) |
| `docs` | Format and module notes; `docs/research` keeps the reverse-engineering notes (DOL addresses, recovered behavior) |

The engine design is described in [ARCHITECTURE.md](ARCHITECTURE.md), the
working method and next steps in [PORTING_STRATEGY.md](PORTING_STRATEGY.md).

## Building on Windows

Requirements: Windows x64 and Visual Studio with *Desktop development with
C++* (MSVC, CMake, Ninja). CMake 3.24 or newer, C++20, no vcpkg. SDL3, Dear
ImGui, pl_mpeg, stb and Zstandard are fetched by CMake at pinned commits and
checksums (the first configure needs internet access; the pins are listed in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)).

```bat
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Jobs 4
```

This configures `build\Release` (Ninja), builds everything and runs the tests
(`ctest --output-on-failure`). The script finds Visual Studio with `vswhere`,
imports its compiler environment into its own process only (it writes
`build\compiler-env.cmd` and does not change the global PATH) and uses
Visual Studio's CMake when present. Builds never start the game.

| Switch | Effect |
| --- | --- |
| `-Configuration Debug\|Release\|RelWithDebInfo` | Build type and folder (`build\<Configuration>`, default Release) |
| `-Jobs N` | Parallel build jobs (1-64, default 4) |
| `-CoreOnly` | Skips the SDL/OpenGL runtime (`DW4_BUILD_APP=OFF`) |
| `-Package` | Installs into `dist\DW4_PC`: one self-contained `DW4_PC.exe` (static C++ runtime, only system DLLs), the tools, licenses and docs, ready to copy anywhere |

The game builds as `dw4_game.exe` (in `build\<Configuration>`); the install
step renames it to `DW4_PC.exe`, the release's single entry point.
`Play-DW4-Game.cmd` starts `build\Game\dw4_game.exe` (a separately configured
build folder; `build.ps1` does not create it) with an optional stage name.

CMake options:

| Option | Meaning |
| --- | --- |
| `DW4_BUILD_APP` | Build the game runtime (SDL3 / OpenGL / ImGui); default ON on Windows |
| `DW4_STATIC_RUNTIME` | Link the C++ runtime statically (default ON): a clean Windows needs no Visual C++ redistributable |
| `DW4_ONLINE_DEV` | Online client development build (the loopback test server allowed, see [Online development](#online-development)) |
| `DW4_ONLINE_PINS`, `DW4_ONLINE_PINS_FILE`, `DW4_ONLINE_REQUIRE_PINS` | The server's certificate pins (see [Release builds](#release-builds-and-the-server-pins)) |
| `DW4_TEST_DATA` | Game data for the tests that need it (skipped without) |
| `BUILD_TESTING` | CTest targets (default ON) |

Targets besides the game: `dw4_inspect`, `dw4_catalog`, `dw4_texpack`,
`dw4_l10n` (language pack tool, [L10N.md](L10N.md)), `dw4_overflow_probe`,
`dw4_world_probe` and the test programs.

## Local validation

GitHub Actions is disabled for this repository at the owner's request. Hosted
workflows must not be restored or dispatched without a new explicit request.
Source pushes remain part of the development workflow. The former hosted build
definition remains recoverable in repository history.

The native source port is the active local validation scope:

- Core, engine and runtime: use `scripts/build.ps1 -Jobs 4`, which runs CTest
  after building. Add `-CoreOnly` for portable-core changes. Engine and
  graphics changes require hidden `dw4_game --smoke-test` checks (a finite run
  with an app-rendered screenshot, see [Test runs](#test-runs)).
- The DolRecomp/ModernGekko experiment was removed from the source tree; it
  remains in Git history only. Do not continue that build or optimization
  queue.
- Core CTest success alone does not validate complete original gameplay.
  Builds never start an interactive game. Keep generated builds, packages and
  original game data out of Git.
- Historical test records of the former core are kept in
  [research/VALIDATION.md](research/VALIDATION.md); they do not establish
  engine gameplay.

## Tests

- **CTest** (run by `build.ps1`): asset-free tests of the core formats, the
  engine pieces, settings, saves, online code and tools; the tests that need
  game data take it from `DW4_TEST_DATA` and are skipped without it.
- **Python tests** (not in CTest): `python tests/cli_tests.py
  <dw4_inspect.exe>`, `python tests/catalog_cli_tests.py <dw4_catalog.exe>`
  and `python tests/test_verify_game.py`.
- **Game checks:** hidden `--smoke-test` runs with screenshots, the automated
  story runs and the determinism references below.
- **Server:** `dotnet test` (see [Online server](#online-server-dw4online)).

## Test runs

`dw4_game --smoke-test FRAMES` runs fixed steps as fast as the machine runs
them, without a picture unless one is asked for. Test runs read no
`settings.ini`; they take their options from the command line and the
environment. `DW4_NO_ERROR_BOX=1` (and the `--smoke-test`, `--convert-rvz`,
`--import-save`, `--save-info` and `--export-presence-art` modes) keep the
error message box away; the reason still goes to stderr. Example:

```bat
dw4_game.exe --data GAME --stage stagml11 --smoke-test 600 --screenshot lobby.bmp
```

### Command line options of test runs

- `--smoke-test FRAMES` (fixed steps as fast as the machine runs them; without
  a picture, see below).
- `--input SCRIPT` (lines `frame BUTTON [duration [player]]`):
  - BUTTON A, B, X, Y, Z, L, R, START; UP, DOWN, LEFT, RIGHT, which set the
    D-pad bit and push the main stick fully (the scripts' combined direction,
    unchanged so older scripts, the determinism references and the campaign
    checkpoints keep their meaning; a GOTO / SEEK steering or a `STICK`
    replaces the push); DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT for the
    D-pad alone; `STICK:x,y` / `CSTICK:x,y` for the main stick / C-stick at raw
    device values -128..127; several joined by `+`; the quick panels' cursor
    takes the D-pad and the C-stick, walking the main stick.
  - `frame GOTO x z [duration]`, `frame SEEK duration [model-prefix]` and
    `frame SEND CATEGORY COMMAND [arguments|- [row]]`.
  - `SEEK duration mat:matz02` walks a route onto a mat and confirms its
    prompt, `SEEK duration explore:matz02` plays through the stage to it: it
    fights enemies (hovering ones from a ledge its swing reaches them from,
    turrets in the gaps between their volleys; on a moving ride such as the
    stag0103 boat only those it can strike from the ride, guarding against a
    shot just before it lands), picks up items, steps on the other mats and
    explores until the way is open.
  - `0 SEEK duration campaign` plays the story from the boot logos (the
    campaign bot, below).
- `--screenshot FILE.bmp`, `--screenshot-frames N,M`, `--flag N=V`,
  `--stage NAME`, `--loading original|fast`, `--card-dir GCI_ROOT`,
  `--save-file FILE`.
- `--load-state FILE` (start from a save state file; with
  `DW4_STAGE_STATES=dir` a test run leaves one at every stage start,
  `<frame>_<stage>.dw4state`, and the campaign driver's memory beside it,
  `<frame>_<stage>.mem`, which `--load-state` reads back;
  `DW4_CHEATS_FROM_ENV=1` replaces the state's test cheats by the
  environment's from the loaded frame on, and `DW4_LEVEL_ON_LOAD=n` /
  `DW4_DISKS_ON_LOAD=record:count,...` set player 1's level and disk counts on
  the loaded state, to try a later stage at the level and with the supplies a
  no-cheat run would bring there).

### The campaign bot

`0 SEEK duration campaign` plays the story from the boot logos: a new game,
then the story objectives in order up to the staff roll, see
`src/game/campaign.cpp`; the dng310 fortress is planned over its floors'
regions and the flags that open their doors, the Lucemon fight by a boss
routine; without `DW4_ONE_HIT` it casts its first technique while the MP lasts
(MP disks when it runs out) and puts the strongest devices found on at the
Digi-Lab (PWR Medals into the chip pockets of a weapon of rank 1+), and
without `DW4_GOD` it also guards against enemy blows and touches (measured
against the body's real height, its HIT boxes), heals with HP disks from the L
panel (the smallest disk that makes the missing HP up within 10 % of the
maximum; with Y at once while blows leave no gap, outside the boss stages only
below 20 % while it holds a Recovery, which Y takes first; in a boss stage the
biggest blow of the visit keeps the heals early), uses heal gates, the X-Data
shop's refill and Gate Disks, sells the weaker devices and restocks at the disk
shop (HP Disks L and Recovery too with bits to spare, every bit's worth before
the last boss, Daemon's three stages with no lobby between them; each lobby
errand waits until no other errand's shop or bank program runs, and one that
stalls is left until the next lobby room), goes back to restock on the way to
an objective when its heals were down to half a stock already as it entered
the stage (twice an objective; never from a stage behind the stag0304 cannon
climb, `kGauntlets`), donates at the junk shop and wears the Filter Board it
then sells while the way ahead crosses stag0303's swamp fog (`kProtections`),
buys stronger devices at the weapon and armor shops, takes the fire the dng308
/ dng309 cell walls need from a fire PWR Medal (stagML13 chip shop) in the
pocket of a rank 1+ weapon (weapon shop, whose stock is rolled anew on each
visit) instead of the Guilmon form, which begins at level 1, and the thunder
dng412's scrap walls need from a thunder PWR Medal in its place, keeps its bits
in the bank while it is out of the lobby (a defeat leaves them in the bit
capsule) and takes them out once per stay before the shops, recovers its bit
capsule with a Raise Disk after a defeat and trains in the area's field below
an objective's gate level (a Gate Disk escape raises that level like a defeat
unless the visit set a story flag never seen before: then it restocks and
comes back, at most eight times per objective; where the panel refuses the
Gate Disk, stagBS01 and Daemon's stages, the objective is taken up again and
the fight goes on; after a defeat in a field it trains in the area before
while short of HP disks).

With real blows it strikes the stag0103 boat's towers in passing, fights
e_mammos off its arena's wall and Apocalymon from its dais, dodging the blast
pillars, guards against shells in flight and turret beams (also in its
aggressive windows), fights turrets (a swing the moment the last one ends,
inside their hit reaction), leaves structures that heal themselves (the area 3
poison plants) and the ground round them alone unless an objective's gimmick
stands behind them (the stag0301 levers: the plant struck down for the moment
its body leaves the way, `Objective::wanted`), crosses a field to its exit
striking only the enemies that come near, crosses a floor that gave way under
it before (dng310f3's fading tiles) without a stop and keeps the spot a fall
began from off its routes for a while, chases no enemy over moving platforms,
holds waves away from pits and quicksand sinks (an enemy off the sand is
walked to), takes no guard with a pit, quicksand or a deep drop behind it (a
wall before the drop ends the push), fights Belphemon, dng412b4's knight (off
its homing orbs, struck where its walk will carry it when the swing lands) and
Daemon's forms by boss routines (the second form's blast rings walked off from
its wind-up on, swings only; the last form struck from a spot outside the
shape of its body boxes, kept out of the way its flights and sideways slides
will sweep and off the orbs it sets out), guards shots still queued on their
shooter and slow homing shots by their speed, walks away from a body after two
touch guards that blocked nothing, cures poison with Cure Disks, keeps off and
leaves alone structures seen blowing up where they stand (stag0401's mines),
steps on stag0403's floor switches in the solution's order by the states their
buttons show (one it was thrown back onto, which turned it off again, is
stepped off and on once more; `DW4_MAT_SWITCH`), hunts the members of waves
whose clearing powers or opens something even while crossing a field
(stag0304's switches; all of stag0404's tower), at the last stand (Daemon's
chain from stag0401 to stagBS06, no shop on the way) sells every device not
worn, keeps the HP Disks L and Recovery for the last form, farms stag0201 for
the bits of a full stock before an attempt it cannot pay and gives a stalled
boss stage of the chain up (no guard, no heal: the defeat starts the chain
anew), and dodges Lucemon's bomb rows from their markers (omens: a blast's
rings known before its boxes exist); a field step that stalls for 180000
frames leaves the field and comes back.

Checkpoints and segments: with `DW4_CAMPAIGN_CHECKPOINTS=dir` it saves the
game like a player at nine points of the story where it returns to the lobby
(the save Digi-Elf in stagML12, `kCheckpoints`) and copies Card A to
`dir/<checkpoint>`, with `DW4_CAMPAIGN_LOAD=1` it calls the saved character of
Card A at the title instead of starting a new game, so a story run can be
split into segments that start from such saves and run side by side (memory
card saves stay valid when the engine changes, save states do not);
`DW4_CAMPAIGN_STOP=1` ends a run once it has saved its first checkpoint or the
story is done, `DW4_CAMPAIGN_STOP=end` only at the story's end.

### Runs without a picture

A test run draws no pictures unless it needs them: without `--screenshot` /
`--screenshot-frames`, `DW4_TEXTURE_PACK` or `DW4_DUMP_TEXTURES` it opens no
window and no OpenGL context, decodes no movies and mixes no sound (unless
`DW4_AUDIO_DUMP`); with the faster collision queries and route planning a
campaign or survey run simulates about ten times faster than before (a
campaign segment 530 -> 5300 frames/s, four side by side 295 -> 3400 frames/s
each). The scene is still traversed after every step exactly as for a picture
(`Engine::traverse_scene`: animation clocks, motion combiners and recorded
matrices advance there), so such a run plays the same frames as one that draws
them: the fingerprints, logs and stage states are identical. `DW4_RENDER=1`
draws the pictures anyway in a hidden window (the old behaviour;
`DW4_TRACE_SHADOW` needs it); the last frame's draw list is collected once at
the end for the `draws` count and `DW4_DRAW_DUMP`.

### Test-run environment variables

- `DW4_GOD` keeps players alive, `DW4_ONE_HIT` makes player hits defeat their
  target, `DW4_INFINITE_MP` keeps MP full.
- `DW4_DEVICES=type:record:rank:value,...`, `DW4_DISKS=record:count,...` and
  `DW4_BITS=n`, `DW4_XDATA=n`, `DW4_LEVEL=n` and `DW4_FORMS=species,...` fill a
  new player's folders, level and forms; `DW4_FLAG_FOLDER=item,...` gives key
  items (story progress items such as boss IDs); `DW4_DIFFICULTY=n` sets the
  difficulty (0-2) before the first stage loads.
- `DW4_CARD_RECORD=FILE.gci:N` makes player 1 the character of save box N (as
  the character select loads it; FILE may be a `savedata.dat`).
  `DW4_CARD_DIR` (like `--card-dir`) plays on a GCI folder; test runs use GCI
  folders (`saves/GC/USA` next to the program). `DW4_CARD_A` / `DW4_CARD_B`
  set what the card slots hold (as `[data] card_a` / `card_b`).
- `DW4_ENTRY=n` starts a test run's first stage at player point group n (as
  when arriving from another exit), `DW4_PLAYERS=n` joins n players.
- `DW4_OPEN_MENU=1` opens the F1 menu (`DW4_MENU_TAB=Saves` on that tab,
  `DW4_MENU_IMPORT=FILE` with that file's import preview),
  `DW4_MIGRATE_FROM=<GCI root>` (with `--save-file`) copies that root's
  `Card A` / `Card B` saves in as a first start copies `saves/GC/USA`.
- `DW4_FAST_LOADING=1` loads with the fast loading option (`--loading
  original|fast` overrides it), `DW4_TRACE_LOADTIME=1` prints for every stage
  load the frames to the stage start and to player 1's control and the real
  time the load's frames took (`=2` every frame of the load with the loader
  state).
- `DW4_WINDOW=WxH` and `DW4_WIDESCREEN=1` size test windows,
  `DW4_SMOKE_STAGES=n` ends a test run once the n-th stage has loaded.
- `DW4_TRACE_SELECT`, `DW4_TRACE_TEXT`, `DW4_TRACE_PLAYER`, `DW4_TRACE_HIT`,
  `DW4_TRACE_EXPLORE` and `DW4_TRACE_CAMPAIGN` log menu cursors, text, player
  state, hits, the exploring bot's targets and the campaign driver's menu
  answers, heals, guards and trainings (`DW4_TRACE_GUARD` every enemy attack
  box the guard looks at, `DW4_TRACE_HITBY` the attack boxes near the player at
  every blow it takes); `DW4_TRACE_STATS` every player status calculation,
  `DW4_TRACE_PANEL` the quick panels' selected line whenever it changes;
  `DW4_TRACE_SCRIPT=1` (or a script name) logs script runs and
  `DW4_TRACE_SEQ=<model>` a model's sequence changes (`DW4_DUMP_SEQ=<model>`
  lists all its sequences with their commands).
- Campaign test aids: `DW4_CAMPAIGN_IDLE=n` lets go of the controls for n
  frames once player 1 has control, a run from the same save played on from
  another moment; `DW4_FARM_FIELD=<stage>` and `DW4_FARM_TEST=1` pick and force
  the last stand's farming.
- Campaign research reports (they change the stage):
  `DW4_CAMPAIGN_REGIONS=wall,...` prints a stage's regions with those
  HITLANDMODEL walls closed and what each wall joins, `DW4_CAMPAIGN_REACH=...`
  what is reached from the player (or `DW4_CAMPAIGN_REACH_FROM` points) as
  walls are switched off; `DW4_CAMPAIGN_STALL_DUMP=1` lists the enemies'
  control, freeze, HP and sequence once a campaign step has lasted 20000
  frames.
- `DW4_TRACE_FINGERPRINT=N` prints the state fingerprint (and a hash of the
  random generator) every N frames: two runs of the same input script must
  print the same lines, so the first differing line is the first frame where a
  run diverged.
- `DW4_FIXED_RANDOM=<hex>` makes every draw of the game's random generator
  return that constant (the Dolphin side: AR `0421EB40 3C60hhhh`,
  `0421EB44 6063llll`, `0421EB48 4E800020`), for frame-exact comparisons of
  fights. `DW4_CLOCK_FILE=<path>` with lines `<frame> <world ns>` sets the
  world clock to that value in the step that reaches the frame (the other
  frames keep adding one field), so a port run can replay the clock a Dolphin
  run logged, jitter included.
- `DW4_STATE_TEST=N` checks save states: save at frame N, load at the end and
  compare every frame again; `DW4_STATE_FILE=path` also sends the state through
  a file.

### Dev presentation tools

Test runs only, off unless set (`src/game/dev_present.hpp`). A test run
normally runs one step per pass and never blends, so it cannot show what a
high refresh rate with smooth motion presents.

- `DW4_DEV_PRESENT=HZ` presents at a fixed virtual refresh instead: every pass
  adds 1/HZ s to the step accumulator and runs the steps it covers, exactly as
  interactive play does with the real clock, and smooth motion blends as there
  (HZ above 75).
- `DW4_DEV_SETTINGS=FILE` takes the window (`width`, `height`, `widescreen`,
  `msaa`, `smooth_motion`), `loading` and `skip_intro` from a copy of a
  `settings.ini` (only read; with `skip_intro` the run boots at the title like
  the player's), `DW4_DEV_SMOOTH=0|1`, `DW4_DEV_WIDESCREEN=0|1`,
  `DW4_DEV_MSAA=n` and `DW4_DEV_WINDOW=WxH` override single values.
- `DW4_DEV_DUMP=FROM-TO[,FROM-TO...]` with `DW4_DEV_DUMP_DIR=dir` writes every
  picture presented during those steps (`present_NNNNNN.png`, counted over the
  run) and a line per present in `presents.csv` (step, blend factor, and what
  the blend did: items blended, unmatched, too far, swapped occurrences, jumps,
  sequence wraps, palettes, texture matrices, camera and the camera's change in
  this and the previous step); `blend_NNNNNN.txt` lists the items the blend
  flagged. `DW4_DEV_PICTURES=0` writes only the CSV and the blend lists (fast,
  whole routes).
- `DW4_DEV_IDS=1` adds an item-id picture per present (`ids_NNNNNN.png`: RGB =
  draw list index + 1, the list drawn again without blending and
  multisampling) and `items_NNNNNN.txt` (index, node, kind, IGB file and
  object, texture, flags, translation, 3x3, UV scroll, placement and sequence
  phases), so every pixel names its draw item.
- `DW4_DEV_HIDE=pat,...` leaves items out whose node or texture name contains a
  pattern, or a category (`:blend`, `:opaque`, `:2d`, `:3d`, `:skinned`,
  `:particles`, `:texmatrix`, `:shadow`), for A/B runs.
- `tools/flicker.py DUMP_DIR` (numpy, Pillow) finds flicker in such a dump:
  pixels that leave their value for one to three presents and come back (and,
  with item ids, pixels whose draw item does that), a heat map, the hot regions
  and the items owning them; `tools/blend_report.py DUMP_DIR` groups the
  blend's flagged items (`wrap`: a sequence that wrapped, stepped or whose
  clock went back, drawn as it is; `jump`: a change far larger than the step
  before that no sequence marked, still blended; `swap(near)`: blended from
  another occurrence's place; `texture_jump`: a texture matrix drawn as it is
  while its model blends).

Example, the lobby after loading a save at 144 Hz:
`DW4_DEV_SETTINGS=settings_copy.ini DW4_DEV_PRESENT=144 DW4_DEV_DUMP=4790-4800
DW4_DEV_DUMP_DIR=dump DW4_DEV_IDS=1 dw4_game --data GAME --card-dir CARDS
--input load.txt --smoke-test 4800`, then `python tools/flicker.py dump`.

## Feature notes and test switches

Implementation notes and test switches of the player features described in
[PLAYER_GUIDE.md](PLAYER_GUIDE.md).

### Fast loading measurement

In the automated story run (the r25c segments, 183-185 stage changes) the
loads take 90,913 frames (25 minutes) with the original timing and 2,825
frames (47 seconds) with Fast. The real time a load's frames take on the PC is
about the same either way (the same files are parsed and the same scripts run,
10-35 ms in the longest frame of a load), so a Fast stage change is over in
about a third of a second.

### Randomizer

Randomized games are not the original game (`Cheats::original()`), so they
count as their own category wherever that matters. The save bond is described
in [SAVES.md](SAVES.md#randomizer-bond-pc-option).

Test runs: `DW4_RANDOMIZER=seed[,enemies[,zones[,drops]]]` (a bare seed turns
everything on; `zones` is the rule number: 1 the round 48 table, 2 the current
one, which a bare seed takes); `DW4_CARD_DIR` places `randomizer.ini` in the
GCI root as `--card-dir` does. `DW4_RANDOMIZER_SWEEP=1` makes every enemy
generator of the stage spawn its wave in turn and logs one `sweep` line per
generator (models, problems such as a missing hit script, a sound without a
bank, a model that did not start or one the watchdog removed);
`DW4_RANDOMIZER_SWEEP=static` starts no wave and logs one `check` line per
enemy generator instead: whether it is shuffled (else why not), the floor
under its spawn points (none, a pit, quicksand) and what a pool family meets
that the generator's own does not (no hit script, hovering above the swing, a
smaller climb or a deeper drop of its land cylinder; wider bodies and a missing
entry sequence are noted).

### Name tags

They are drawn by the PC host over the game picture (ImGui, under the F1 menu)
from the players' state after each step, the camera the picture used and the
widescreen widening; nothing is added to the game's scene, so the simulation,
save states, online sessions and the Dolphin comparisons are the same with
them on. The tag sits just above the head: one unit above the form's head
height, measured for each of the 16 forms from the drawn figure in its stance
(the head with its horns, ears, a collar or wings close to it; weapons held up
and wing tips further out are left out), over the head joint (followed
smoothly sideways, at most 3.5 units from the Digimon's position). Raised arms
and weapons do not move it; when the head itself rises (a jump, the leap of a
charged attack, the bounce of a run) the tag goes up with it at once, so the
head never pushes through the bars, holds that height for half a second (a run
keeps it steady at the stride's top) and then eases back down (a model of no
known form: above its highest joint, else part of its bounding box). It
follows the smooth motion above 60 fps and fades with the distance from the
camera (pad input off hides it during demos and cutscenes). A player counts as
hidden when the depth buffer just drawn is nearer than three points just above
its head (the stage's collision cannot tell, it holds invisible walls); its
tag then turns faint over a few frames. The text uses Windows' Segoe UI Bold
(else Arial Bold, else ImGui's scalable font); the F1 menu keeps its own.
Players beyond the four local slots (online players) are handed in by the
netcode (`OverheadLabels::set_remote_players`) and drawn the same way when
they are in the same stage.

Test runs: `DW4_LABELS=all` and `DW4_TRACE_LABELS=N` (every N frames the
labels' head point and its height above the feet, screen position, the feet's
screen position, the distance and whether the last picture hid the player, for
the 1280x960 test window or `DW4_WINDOW`, without a window too); `DW4_MSAA=n`
gives a test run's hidden window the multisampling of a player's settings
(screenshots).

### Discord Rich Presence

The program talks to the Discord app over its local pipe (`discord-ipc-0..9`)
with its own small client. The default `[discord] client_id` is
`1560921673354453093`, the Discord application whose name shows as
"Playing ...".

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
suggested 512x512 pictures for the `logo` and `area_*` keys from your own copy
of the game (they stay on your PC; upload them under their file names): `logo`
is the title logo on a transparent square, each `area_*` picture the stage
picture its loading card shows in the middle of the loading screen (the card's
picture quad, without the name and "Now Loading" strip of the same texture),
upright, centre-cropped to a square without stretching. The form pictures are
yours to choose.

Test runs talk to Discord only through `DW4_DISCORD_PIPE=<pipe name prefix>`
(a test pipe, never the real app; `dw4_presence_tests --serve <pipe> <seconds>
[ready|silent|header|deaf]` is a fake Discord that prints what it gets:
`ready` answers like Discord, `silent` takes the connection and never answers,
`header` answers with a message header and no payload, `deaf` answers and then
stops reading) and `DW4_TRACE_PRESENCE=1` prints each new activity.

### Achievements

They come from a few observe-only hooks in the reconstructed game logic (boss
death scripts and defeat flags, level ups, digivolutions, new techniques,
cards, quest goals, player defeats, critical hits, enemy defeats, story flags;
`engine/port_events.hpp`) plus the players' state after each step. The engine
collects these events only while the host asks for them, never reads them
back, and they are in neither the state fingerprint nor a save state. Nothing
unlocks in test runs or while the campaign bot or a scripted walk plays.

Test runs: `DW4_ACHIEVEMENTS=1` (with `DW4_PROFILE=<file>` to keep the
profile) and `DW4_TRACE_EVENTS=1` (every event as it happens).

### Data Overflow

Test runs: `--overflow-run form=p_agumon field=stagbs02 seed=1 bot=kite
summary=<file>` ([OVERFLOW_DIRECTOR.md](OVERFLOW_DIRECTOR.md),
[OVERFLOW_HOST.md](OVERFLOW_HOST.md)); the design is
[SURVIVAL.md](SURVIVAL.md).

### Graphics options

Test runs read no settings.ini and take `DW4_TEXTURE_QUALITY`,
`DW4_TEXTURE_FILTER`, `DW4_LOD_BIAS`, `DW4_SHARPEN`, `DW4_SHADOW_QUALITY` and
`DW4_TEXTURE_MEMORY` ([TEXTURES.md](TEXTURES.md#graphics-options)).
`texture_memory` in settings.ini accepts 32-8192 MiB; the F1 menu offers 512
MiB to 4 GiB.

### Texture pack tools

Test runs take `DW4_TEXTURE_PACK=dir` and `DW4_DUMP_TEXTURES=dir`;
`DW4_TRACE_TEXTURES=1` prints each stage's texture memory and streaming
figures.

For pack authors, the development tool `dw4_texpack export --data ROOT --out
DIR` writes every texture of the game data at once (not only those a play
session draws) under the same names, with a manifest of formats, palette
variants, alpha, materials, languages, stages, UV use and atlas cells
([TEXTURES.md](TEXTURES.md)). Keep its output on your machine.
`tools/remaster` turns that export into a 4x pack with super-resolution models
and an edge reconstruction for lettering, and compares the result in hidden
game runs ([tools/remaster/README.md](../tools/remaster/README.md)).

### Controllers

The quick panels take the D-pad and the C-stick only, as the original does
(8034A24C).

How the Switch 2 Pro Controller works: the game reads the controller itself,
the way Steam does on Windows: the buttons through Windows' HID driver on its
interface 0, the start commands (taken from SDL's own Switch 2 driver) through
WinUSB on its interface 1; each connected controller becomes an SDL pad with
the controller's ids, so the pictures, the bindings and the player slots treat
it like any other.

Details: [ROUND_PADS_NOTES.md](ROUND_PADS_NOTES.md),
[ROUND_SW2WIN_NOTES.md](ROUND_SW2WIN_NOTES.md) (the Switch 2 Pro Controller;
[ROUND_LIBUSB_NOTES.md](ROUND_LIBUSB_NOTES.md) the earlier libusb round).

## Online development

```bat
powershell -ExecutionPolicy Bypass -File scripts\online-dev.ps1
```

starts a private database and the DW4Online server on 127.0.0.1 (nothing
leaves the PC), builds a development client (`build\OnlineDev`, needs
`DW4_ONLINE_DEV`) the first time, and opens two game windows that both press
Play; closing both windows stops the server. `-Data` names the game data
(default: the `[data]` path of `build\Game\settings.ini`), `-Card` a card
folder whose saves both start with; `-Hidden` / `-Frames` run both clients
without windows, `-GameExe` / `-NoBuild` use an existing client. It needs
Python 3, the .NET SDK and MariaDB binaries (XAMPP's); the full stack is
`tools/online_test/run.py`. The seamless online co-op's design is
[SEAMLESS.md](SEAMLESS.md) (section 19 for the world without rooms). Where
an arrival lands is 19.7: the arrival prompt and who arrives with whom
(decision 43), and the visibility rules the relay applies on every path
(decisions 48 and 49: a *New instance* chosen *Private*, or a remembered
*Private* default without an answer, is never another player's co-arrival,
never joins a starting instance and never takes the automatic join without
a prompt, `RoomHub.ChoiceAdmits` / `AdmittedStarting` / `AdmittedAutoJoin`;
*Friends* only a friend host's; one stage group passing one barrier and
travel inside an instance stay together whatever the visibility). The
host's grace and the absence debt that bounds a host whose link keeps
dropping (`Relay:LagLeaveSeconds` + `World:HostGraceSeconds`) are 19.6.7;
the scenarios of `run.py` are listed in 19.24.

## Online server (DW4Online)

`server/DW4Online` is the backend: one ASP.NET Core (.NET 10) service with
MariaDB that holds the accounts (open registration with rate limits, optional
e-mail, one-time recovery codes, admin resets), the online character and its
story, one private cloud backup of the local save per account (the latest and
five earlier revisions), the online achievements (only online play counts;
cheats, rebalancing and loaded save states never, randomizer runs as their own
category, speedrun times in in-game time without loading), friends and
blocks, the world (the instances, their history and the staff's moderation),
the TCP relay with one-time HMAC tickets (its port is set in
`appsettings.json`) and the website
(accounts, friends, profiles, achievements with their rarity, the instances,
admin, privacy page). Its tests run on loopback.

```bat
cd server\DW4Online
dotnet build DW4Online.slnx
dotnet test tests\DW4Online.Tests
```

The integration tests need a private MariaDB (`scripts\local-db.ps1`), the
end-to-end tests `scripts\e2e.ps1`. Building, configuration, the deployment
kit for a Windows server (Windows service, TLS, backups, updates) and the API
are described in [server/DW4Online/README.md](../server/DW4Online/README.md)
and [server/DW4Online/deploy/DEPLOY.md](../server/DW4Online/deploy/DEPLOY.md).

## Release builds and the server pins

Player builds connect only to the project's server and take its certificate
pins from the build (`local/online_pins.txt` or `-DDW4_ONLINE_PINS`,
[DEPLOY.md](../server/DW4Online/deploy/DEPLOY.md) 6.4); a build without them
refuses the server ("Online play is not available in this build yet"). Release
packages are meant to be configured with `-DDW4_ONLINE_REQUIRE_PINS=ON`, which
makes missing pins a configure error (`build.ps1` has no switch for it; pass it
to CMake). A build from the public source therefore plays offline and over LAN
only.

The online client and server compare versions: the client asks the server's
status and refuses to connect when the server's minimum protocol or minimum
version is newer ("Update required ..."); the game's version is the CMake
project version (`DW4_PC_VERSION`).

No path of the build machine ends up in the binaries: every build cuts the
checkout, the build folder and the FetchContent source folders off `__FILE__`
(`cmake/trim_build_paths.cmake`: `/d1trimfile:` for MSVC,
`-fmacro-prefix-map` for GCC and Clang; SDL's release asserts read
`sdl3-src/src/...`). The configure step warns when a dependency's sources lie
outside those folders. A release package's executables can be checked with
`grep -a -o -E "Users[/\\][ -~]+" DW4_PC.exe` (no output). Release builds
make no PDB; a build with one (RelWithDebInfo) names the PDB's absolute path.

## Further documentation

| Document | Content |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Engine layers, frame step, messenger, managers, scene graph, collision |
| [PORTING_STRATEGY.md](PORTING_STRATEGY.md) | Working method, research tooling, next steps |
| [ROADMAP.md](ROADMAP.md) | Acceptance gates and their evidence |
| [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md) | The original game's systems and their state |
| [SAVES.md](SAVES.md) | Save files, card states, GC save import, the character record |
| [TEXTURES.md](TEXTURES.md) | Texture decoding, graphics options, texture pack format 2, `dw4_texpack` |
| [L10N.md](L10N.md), [LOCALIZATION.md](LOCALIZATION.md) | Display languages and language packs |
| [SEAMLESS.md](SEAMLESS.md) | The online co-op design |
| [SURVIVAL.md](SURVIVAL.md) | Data Overflow (survival mode) |
| [research/](research/) | Reverse-engineering notes (DOL addresses, recovered behavior) |
