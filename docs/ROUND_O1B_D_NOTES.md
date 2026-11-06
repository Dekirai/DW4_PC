# Round O1b lane D (screens): notes

Branch `feat-o1b-screens` from main d0e005b0 (worktree `DW4_PC-wt/o1b-screens`).
Design: docs/SURVIVAL.md 3, 9, 13.6, 14 (T9, T2), 15.2. Coordination with
lanes E (content) and F (meta): `DW4_PC-wt/o1b-contract.md`, the entries
"D screens" (the merge notes of 16:55 say what E and F put where).

## 1. Done

| Part | Where | State |
| --- | --- | --- |
| H0: the `talk_box` port event | `include/dw4/engine/port_events.hpp` (a new kind at the end, `x/y/z` on every event), `src/engine/model_ctrl.cpp` (where the main lobby's A pushes the talk hitbox, only while port events are collected; reads the node matrix, writes nothing) | built; T1 and T2 unchanged |
| Presentation actor core (13.6) | `src/game/presentation_actor.*`: any model of a stage package's main member (or a `_common` package) read on a worker thread from a copy of the resources, its own scene cache, clips by sequence name (SQ_WAIT, SQ_TALK), fade 0.35 s, camera fade, shadow proxy, turning to a point; the ghost figures' technique (`ghost_matrix`, `ghost_insert_points`, `ghost_camera_alpha` shared) | built; n_gekoa from stag0302 |
| SimUi (9.1, 13.6) | `src/game/sim_ui.*`: 2D items of the 640 x 480 layer (orthographic, so the renderer keeps them 4:3 in widescreen), windows in the game's window colours (quads: the 13.6 fallback), the game font (bectext.igb NORMAL, the engine's own `layout_text`), pictures (`image`), cursor bars, bars, the talk prompt, word wrap | built |
| Talk window look | `SimUi::talk_frame`: full width at the bottom with the game's cyan edge lines, a slanted name tab, the face in a cyan frame, the prompt at the bottom centre (as the game's own talk window, see `npc_4700`) | built (7b2660f9) |
| Face icon | `model_texture` (overflow_lobby.cpp): the texture bind of n_gekoaic's igAttrSet in stag0302's main member (the document the NPC actor loaded), drawn upright in the talk window's face frame | built (9c762731) |
| Texts (9.7) | `src/game/overflow_text.inc` + `overflow_text.hpp/.cpp`: every string of the screens, the dialog of 3.4 word for word | built; T9 |
| NPC rules (3.2) | `overflow_lobby.cpp` `npc_state`, `gate_open` (mate00.lua's branches: flag 409 or 2 / 4 / 5 / 6 / 86; with flag 501), offline only, 2-4 players: the crowd line, test runs hidden unless `DW4_OVERFLOW=1` (talk) / `show` (drawn, no talk), settings.ini `[overflow] npc` and F1 > Gameplay > "Show the simulator NPC" | built, unit tested |
| Talk (3.3) | `OverflowLobby::take_events` takes `talk_box` out of the step's list before the tracker and the online client; a box of player 1 reaching the NPC's 10-unit cube opens the dialog a step later only if player 1 still has the control (no original talk took the press); port events are collected while the NPC is shown; F5 / F9 close the dialog first; the story steps with neutral pads while it is open; test runs: the input script drives the dialog | built; a hidden walk-and-talk run shows it (section 3) |
| Dialog and lobby screens (3.4, 9.2) | `LobbyMenu` (a state machine of menu keys, unit tested) + `draw_lobby_menu`: the first meeting (6 pages), "What will you do?" with the five choices, Never mind, How does it work? (4 pages), the crowd line, Welcome back (time, clear, the Gekomon joke, time up), the character select (4 x 4 faces with the four Rookies open, the form's class line, Art, signature, techniques with their element glyphs, trait, HP / MP, best; Armour / Chip / Level / Start with arrows; "Enter the simulator? Yes / No"), the Plug Lab page, the Data Records pages | built |
| The run from the select | `game_main.cpp` `overflow_lobby_action`: the select's Yes asks the StorySwitch with form, `armor=`, `chip=`, `tier=normal` and a clock seed (test runs add `DW4_OVERFLOW_TEST_RUN`'s keys); back from the run Gekomon opens with "Welcome back" (`RunOutcome::seconds`, the director's run clock) | built; e2e run (section 3) |
| Run screens (9.3-9.6, 4.4) | `src/game/overflow_screens.*`; `overflow_driver.cpp`: `decorate` draws them, `fill_screen_data` reads the director (O1a's ImGui boxes stay only as the fallback without the font): the HUD overlay (run timer, EXP bar, Lv, Defeated, Bits, the build row with the weapon in its value colour and its bar and the six technique glyphs with their ranks, the notice, "Hold out until 25:00"), the level-up window (wrapped line about the cursor's offer), pause, the build page, the Retire question, the results (lines one after another), the finale's quest-timer countdown (20:00-25:00 until lane E's `finale()`; test key `hud_finale=SECONDS`) and TIME UP | built |
| Tests | ctest `overflow_screens` (`tests/overflow_screens_tests.cpp`): T9 (every string against bectext.igb's NORMAL font with the game data, `DW4_GAME_DATA` or argv[1]; without against 9.1's glyph list), NPC rules, talk reach, the dialog's flows, the select's loadout memory, every lobby and run screen's 2D items, the build row parser, n_gekoa loads | 7,469 checks with the data, 3,150 without |

## 2. Results (Release, hidden or headless, nice 10)

| Test | Result |
| --- | --- |
| ctest | 57 / 57 (a57d98ac, 9c762731, 7b2660f9); commits d1089944-91945f51 failed `online` / `online_env` (the new DW4_OVERFLOW* variables were on neither list; fixed in 8c919404) |
| T9 | 128 strings, 0 missing glyphs (the font has 484 characters, no '/') |
| T1 det.sh | 4004f056 / 396cb219 both runs (d1089944, 0e38a86c, 9c762731) |
| T4-like | the lobby's select, a kite run of 900 steps and back, `DW4_STATE_TEST=4600` and `=5000` (the input log spans the dialog's neutral pads and the run): "identical after loading" (800 and 400 frames) |
| T2 r25c, NPC shown (`DW4_OVERFLOW=show`, port events collected in the lobby, H0 on) | 10 / 10 segments, 1,748,011 frames, every save "same story" (build of d1089944; `runs/seg/o1bD_show`) |
| T2 r25c, NPC hidden | 10 / 10, 1,748,011 frames, same story (8c919404; `runs/seg/o1bD_plain`) |
| Isolation (T3-like) | new game into the lobby, the select opened (`DW4_OVERFLOW_SCREEN=select`), A A by the script: a kite run of 1,800 steps, back, 7,000 story frames; reference: the same script without the two presses and without the NPC: 140 of 140 fingerprint lines equal (every 50 frames, `build/t3/A.log`, `B.log`) |
| H0 in the engine | `build/t3/walk.txt`: the new game, the lobby intro by A presses, `GOTO 50 -68`, A: "talk box at (48.3, 14.0, -74.6) reaches Gekomon", the dialog opens, A turns the page |

## 3. The screenshot set (hidden, `DW4_RENDER=1`; not in git)

`build/shots/o1b_screens_sheet.png` (all 16 at half size) and each picture in
`build/shots/` (`.bmp` full, `.png` half): `npc_4700` (the NPC by the gate),
`walk_7650` (a real talk via H0), `talk_4700`, `choice_4250`, `help_4700`,
`select_4700`, `e2e_4310` (Enter the simulator?), `plug_lab_4700`,
(`talk_4250`, `choice_4250`, `help_4250`, `walk_7650` and `e2e_4600` with the face icon and the talk window look of 7b2660f9),
`records_4700`, `e2e_4600` (Welcome back), `run_hud_overflow_2400` (HUD with
the build row), `run_menus_overflow_300`, `run_levelup_overflow_1100`
(level-up and the finale timer), `run_menus_overflow_630` (pause),
`run_menus_overflow_660` (build page), `run_retire_overflow_660`,
`run_results_overflow_745`.

Scripts (ignored `build/`): `lobby_shot.sh NAME SCREEN [1|show] [FRAMES]`,
`run_shot.sh NAME "RUN KEYS" FRAMES`, `e2e.sh`, `lobby_select_run.txt`,
`t3/walk.txt`, `t3/talk.txt`, `t3/ref.txt`.

## 4. Left

- **The game's window models** (i_talkwn, i_messwn, i_stbase, the quick panel
  list) and their open / close clips: SimUi draws the windows as quads in the
  windows' colours (13.6's fallback, without the models' textures). Tried for
  the face icon n_gekoaic: its beAction2Info has no animation database and
  its geometry (a 64 x 64 quad) is scaled to 0 and untextured until its
  SQ_OPENL action runs, so playing 2D models (and their open / close clips)
  needs the engine's Action2 evaluation privately (not started). The face
  icon's picture itself is drawn (its attribute set's texture, 9c762731).
- **UI sounds** (WINDOW_OPEN / CLOSE, CURSOR_*: a second audio stream, 13.6):
  not built.
- **Merge glue** with E and F (contract, D 16:55): F's three calls in
  game_main, E's finale / time_up getters and F's run bits and reward lines in
  `fill_screen_data`; the driver copying `armor=` / `chip=` is F's.
- The "Beaten by a Gekomon" line needs the defeating family: the director
  (lane E's file) would keep the last damaging hit's `HitTaken::shooter`, the
  driver's `RunStatus` hand it to `RunOutcome` and game_main to
  `RunSummary::defeated_by` (ready, unit tested).
- Director strings (offers, build and result lines) are outside T9's table;
  '/' draws as a gap.
- T4 (F5 / F9 across a talk and a run), T5 (story files) and T8 / T11 were not
  run again in this lane: the story loop's only new behaviour without
  `DW4_OVERFLOW` in test runs is reading the flags and the player count; T3's
  equivalent and both T2 runs pass.

## 5. How to run the tests

```
build/bld.cmd            # Ninja Release into build/rel (FETCHCONTENT from the main repo's build/Release/_deps)
build/ctest.cmd          # all ctest
build/rel/dw4_overflow_screens_tests.exe "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted"   # T9 with the font
# det.sh from the main repo (a copy of the exe):
cd C:/Users/shinz/source/repos/DW4_PC && bash work/dref/checks/det.sh <copy of dw4_game.exe> <name>
# T2 with the NPC shown (3 at a time):
EXE=<copy> ENV="DW4_OVERFLOW=show" NAME=<name> nice -n 10 bash work/campaign/segments.sh r25c 3
```
