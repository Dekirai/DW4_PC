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
  contact, disks, key items and rolled equipment picked up with A, floating
  item names).
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
Ninja). SDL3 and Dear ImGui are fetched by CMake at pinned commits.

```bat
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Jobs 4
```

This configures `build\Release`, builds everything and runs the tests.
`-CoreOnly` skips the SDL/OpenGL runtime; `-Package` installs into
`dist\DW4_PC`. Builds never start the game.

## Run with your own game data

The runtime reads the USA disc (GDJEB2, revision 0) straight from an
uncompressed disc image (`.iso` / `.gcm`) or from a Dolphin extraction
(*Properties > Filesystem > Extract Entire Disc*). On the first start it asks
for one and checks it; compressed images (RVZ, WIA, CISO, NKit) can be
converted back to ISO in Dolphin (*Convert File...*). The choice is kept in
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
`[audio] volume` (percent), `[menu] pause` (pause while the menu is open) and
`[keyboard]` with up to three key names per button (`a=Return, Space, K`).

`tools/verify_game.py` checks an ISO or extraction against the expected
`main.dol` SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` without copying data.

```bat
dw4_game.exe                    rem boot logos, title, new game
dw4_game.exe --stage stagml11   rem jump straight to the Home lobby
```

`Play-DW4-Game.cmd` starts `build\Game\dw4_game.exe`. Further options:
`--smoke-test FRAMES` (hidden window, fixed steps), `--input SCRIPT` (lines
`frame BUTTON [duration [player]]`, `frame GOTO x z [duration]`, `frame SEEK duration [model-prefix]`
and `frame SEND CATEGORY COMMAND [arguments|- [row]]`), `--screenshot
FILE.bmp`, `--screenshot-frames N,M`, `--flag N=V`.
Test-run environment variables: `DW4_GOD` keeps players alive, `DW4_ONE_HIT` makes
player hits defeat their target,
`DW4_DEVICES=type:record:rank:value,...`, `DW4_DISKS=record:count,...` and
`DW4_BITS=n`, `DW4_XDATA=n`, `DW4_LEVEL=n` and `DW4_FORMS=species,...` fill a new player's folders, level and forms,
`DW4_PLAYERS=n` joins n players, `DW4_OPEN_MENU=1` opens the F1 menu,
`DW4_WINDOW=WxH` and `DW4_WIDESCREEN=1` size test windows,
`DW4_TRACE_SELECT`, `DW4_TRACE_TEXT`, `DW4_TRACE_PLAYER` and `DW4_TRACE_HIT`
log menu cursors, text, player state and hits.

### Controls

| Original | Keyboard (default, rebindable) | Gamepad |
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

F1 opens the menu: the Settings tab (display, widescreen, frame limit, anti-aliasing,
window size, volume, keyboard bindings, connected controllers), the Cheats tab
(god mode, infinite MP, one-hit defeats, refills, levels and bits per player;
session only, off by default) and the Debug tab (stage loader, player state,
notes, script log). F11 toggles fullscreen,
F2 a free camera.

## License

The repository's own code is MIT licensed (see [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). It does not license
Digimon World 4 or its content. Game data and generated disassembly are never
committed.
