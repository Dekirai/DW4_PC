# Round O1b lane D (screens): notes

Branch `feat-o1b-screens` from main d0e005b0 (worktree `DW4_PC-wt/o1b-screens`).
Design: docs/SURVIVAL.md 3, 9, 13.6, 14 (T9, T2), 15.2. Coordination with
lanes E and F: `DW4_PC-wt/o1b-contract.md` (entries "D screens").

## Done

| Part | Where | State |
| --- | --- | --- |
| H0: the `talk_box` port event | `include/dw4/engine/port_events.hpp` (new kind at the end, `x/y/z`), `src/engine/model_ctrl.cpp` (where the main lobby's A pushes the talk hitbox; only while port events are collected) | built |
| Presentation actor core | `src/game/presentation_actor.*`: any model of a stage package's main member (or a `_common` package) on a worker thread, own scene cache, clips by sequence name (SQ_WAIT, SQ_TALK), fade 0.35 s, camera fade, shadow proxy (the ghost figures' technique; `ghost_matrix`, `ghost_insert_points`, `ghost_camera_alpha` shared) | built; n_gekoa loads from stag0302 (test) |
| SimUi | `src/game/sim_ui.*`: 2D items of the 640 x 480 layer (orthographic, 4:3 in widescreen), windows in the game's window colours, the game font (bectext.igb NORMAL, the engine's `layout_text`), cursor bars, bars, the talk prompt | built |
| Texts (9.7) | `src/game/overflow_text.inc` + `overflow_text.hpp/.cpp`: every string of the screens, the exact dialog of 3.4 | built |
| NPC rules (3.2) | `overflow_lobby.cpp` `npc_state`, `gate_open` (mate00.lua: flag 409 or 2 / 4 / 5 / 6 / 86, with flag 501), `DW4_OVERFLOW=1|show`, settings.ini `[overflow] npc` | built, tested |
| Talk (3.3) | `OverflowLobby::take_events` (talk_box out of the list before the tracker and the online client), `stepped` (opens one step later only if player 1 still controls: no original talk took the press), port events collected while the NPC is shown, F5 / F9 close the dialog first, story steps with neutral pads while it is open | built |
| Dialog and lobby screens (3.4, 9.2) | `LobbyMenu` (state machine, unit tested) + `draw_lobby_menu`: first meeting (6 pages), "What will you do?" list, Never mind, help (4 pages), crowd line, Welcome back, the character select (4 x 4 faces, the four Rookies open, the form's lines, Armour / Chip / Level / Start, "Enter the simulator? Yes / No"), Plug Lab page, Data Records pages | built |
| Run screens (9.3-9.6, 4.4) | `src/game/overflow_screens.*` + `overflow_driver.cpp` (`decorate` draws them, `fill_screen_data` reads the director; O1a's ImGui boxes stay only as the fallback without the font): HUD overlay (timer, EXP bar, Lv, Defeated, Bits, build row, notice, "Hold out until 25:00"), level-up window, pause, build page, Retire ask, results (lines appear one by one), the finale's quest-timer countdown (20:00-25:00 until lane E's `finale()`) and TIME UP | built |
| Run start from the select | `game_main.cpp`: the select's Yes asks `StorySwitch::request` with form, `armor=`, `chip=`, `tier=normal`, a clock seed | built |
| Tests | ctest `overflow_screens` (`tests/overflow_screens_tests.cpp`): T9 (every string against bectext.igb's NORMAL font with the game data; without against 9.1's list), NPC rules, talk reach, dialog flows, every screen's items, n_gekoa loads | passes (7,671 checks with data) |

## Left (see the report / remaining list)

- The game's window models (i_talkwn, i_messwn, i_stbase, ...) and their open
  / close clips: SimUi draws windows as quads in the windows' colours (the
  13.6 fallback without the models' textures). The face icon n_gekoaic in the
  talk window's face frame (a coloured frame stands for it).
- UI sounds (WINDOW_OPEN / CURSOR_*: a second audio stream, 13.6).
- Lane F's meta data: `LobbyMeta` is a stand-in in memory; the Plug Lab's buy
  action (`LobbyAction::buy_plug`) and `on_intro_seen` are not wired to a
  file. Lane E's getters (finale, Guardian bar, gear names, time_up): the
  driver's `fill_screen_data` is the one place to switch at the merge
  (contract D4). The driver does not yet copy `armor=` / `chip=` into
  `RunConfig::loadout_*` (E's fields do not exist on this branch).
- The welcome-back time is the run's steps (frozen steps included), not the
  director's run clock; the "Beaten by a Gekomon" line needs the defeating
  family from the driver (RunOutcome has none yet).
- Director strings (offers, build and result lines) are not in T9's table;
  a glyph the font lacks ('/' is missing!) is drawn as a gap.

## How to run the tests

```
build/bld.cmd                      # Ninja Release into build/rel (FETCHCONTENT from the main repo)
build/rel/dw4_overflow_screens_tests.exe "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted"
build/ctest.cmd                    # all ctest
# det.sh from the main repo path (exe copy):
cd C:/Users/shinz/source/repos/DW4_PC && bash work/dref/checks/det.sh <copy of build/rel/dw4_game.exe> o1bscreens
```
