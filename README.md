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
  `gameStart.lua` and the hand-off to the Home lobby.
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
  upgrades) and the appraiser for unknown devices.
- Rendering of original models, textures, skinned and animated actors
  (Enbaya clips), text from the original fonts and the HUD layers.

Not yet reconstructed: the Digi-Lab and the card program, status effects,
evolution, homing enemy bullets, particles and shadows, audio and movies, name entry,
memory card saves, the options' text and many smaller managers. Collision uses a provisional host query built
on the original floor/wall geometry. See [ROADMAP.md](docs/ROADMAP.md).

## Layout

| Part | Contents |
| --- | --- |
| `include/dw4`, `src` | `dw4_core`: IGB documents, packages and resources, textures, meshes, skinning, Enbaya animation, Lua 4 bytecode, pads and motion primitives |
| `include/dw4/engine`, `src/engine` | `dw4_engine`: frame step, messenger and demo manager, Lua host, scene graph, and the reconstructed managers (stage loader, scene/layer/camera, model control, actions, text, menus, title, character select, floor mats, collision) |
| `src/game` | `dw4_game`: SDL3 window, OpenGL renderer, input mapping and the F1 debug menu |
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

Extract the USA disc (for example with Dolphin: *Properties > Filesystem >
Extract Entire Disc*) and tell the runtime where the extracted root is, either
with `--data PATH` or in `local\settings.ini`:

```ini
path=E:\Games\Digimon World 4 - Extracted
```

`tools/verify_game.py` checks an ISO or extraction against the expected
`main.dol` SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` without copying data.

```bat
dw4_game.exe                    rem boot logos, title, new game
dw4_game.exe --stage stagml11   rem jump straight to the Home lobby
```

`Play-DW4-Game.cmd` starts `build\Game\dw4_game.exe`. Further options:
`--smoke-test FRAMES` (hidden window, fixed steps), `--input SCRIPT` (lines
`frame BUTTON [duration]`), `--screenshot FILE.bmp`, `--screenshot-frames
N,M`, `--flag N=V`.
Test-run environment variables: `DW4_GOD` keeps players alive,
`DW4_DEVICES=type:record:rank:value,...`, `DW4_DISKS=record:count,...` and
`DW4_BITS=n` and `DW4_XDATA=n` fill a new player's folders,
`DW4_TRACE_SELECT`, `DW4_TRACE_TEXT`, `DW4_TRACE_PLAYER` and `DW4_TRACE_HIT`
log menu cursors, text, player state and hits.

### Controls

| Original | Keyboard | Gamepad |
| --- | --- | --- |
| Stick | Arrow keys / WASD | Left stick, D-pad |
| A (talk, confirm) | Enter, Space, K | South |
| B (cancel) | Backspace, L | East |
| X (jump) | J | West |
| Y | I | North |
| START | Esc, Tab | Start |
| L / R / Z | Q / E / Z | Shoulders |

F1 opens the debug menu (stage loader, player state, notes, script log); F2
toggles a free camera.

## License

The repository's own code is MIT licensed (see [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). It does not license
Digimon World 4 or its content. Game data and generated disassembly are never
committed.
