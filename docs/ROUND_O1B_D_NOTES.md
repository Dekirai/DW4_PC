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
| Tests | ctest `overflow_screens` (`tests/overflow_screens_tests.cpp`): T9 (every string against bectext.igb's NORMAL font with the game data, `DW4_GAME_DATA` or argv[1]; without against 9.1's glyph list; every printable ASCII and director-style lines through `fit_to_font`), NPC rules, talk reach, the dialog's flows, the lobby's key edges and latches (`test_lobby_keys`), Welcome back once, the EXP bar inside the level, the results' layout past 16 / 32 lines, the records pages from F's table (`test_records`), the select's loadout memory, every lobby and run screen's 2D items, the build row parser, n_gekoa loads | 7,731 checks with the data |

## 1b. Review fixes (review of 545ab233)

| Finding | Fix | Commit |
| --- | --- | --- |
| The dialog reopens after A (no latch on close; the talk's A acts at once in the interactive path) | `OverflowLobby`: keys / stick held when the dialog opens act only after everything was released (`held_latch_`); `take_story_latch()` is true once after it opened or closed: game_main sets `view.pad_latch` (live pads) or latches the test script's keys until the script releases them; the closed lobby now gets the live keys too | ef082240, 6ba9011a |
| Bolt / Dark glyphs swapped | Bolt U+222A (thunder), Dark U+2229 (dark ball) in overflow_text.inc and `build_row`; test fixed | ef082240 |
| EXP bar cumulative | `level_progress(exp, NEXTEXP(level-1), NEXTEXP(level))` with the run's rules | ef082240 |
| Results cut at 16 lines | two columns past 16 lines (a line wider than its column draws smaller), past 32 the run's lines are cut and Sim Bits / NEW! stay (`fit_result_lines`) | ef082240 |
| Merge glue left to E / F | `fill_screen_data` reads E's `finale()` / `finale_left_steps()` / `time_up()` and F's `run_bits(run)` / `meta_lines()` through requires-guards (generic lambdas: no hand work in the E, D, F merge); checked by compiling the guards against E's director header (a1ac5ddd) and F's profile header (bbcf846d) | ef082240 |
| Records page hard coded | `LobbyMeta::record_rows` {key, title, condition, opens, date} and `FormRecord::tier_clears` from `fill_records(profile, table, meta)` (a template); game_main calls it with F's `record_table()` / `meta().profile()` while the records page is open when `overflow_profile.hpp` exists (`__has_include`); 8 records a page, the Digimon page last with the clears per tier | 873130b8 |
| Director strings with glyphs the font lacks | `fit_to_font` (SimUi's text and widths): ASCII the font lacks (`# $ % * / ; = @ [ \ ] ` { } ~`) draws as its full-width form where the font has one, else a stand-in ('/' "-", '%' "pct", '[' "(", ...); T9 checks all printable ASCII and director-style lines against bectext.igb: 0 gaps | ef082240 |
| Welcome back on every talk, also in "show" and over a story talk | shown once (back_ cleared), opens only when the NPC talks and player 1 has the control (`stepped`) | ef082240 |
| Input scripts without --smoke-test showed a talking NPC | `overflow_scripted = smoke_frames || input_script`: NPC hidden (test_run), the script drives the dialog | ef082240 |
| Results' pace by presented pictures | by director steps (`screen_step_`) | 0e105d02 |
| Notes: evidence commits, T3 stuck in the intro talk | section 2 says which commit each run used; a T3 variant with a walk after the return (section 2) | this commit |

## 2. Results (Release, hidden or headless, nice 10)

| Test | Result |
| --- | --- |
| ctest | 57 / 57 on 0e105d02 (the review fixes; `build/ctest8.log`), also on ef082240, a57d98ac, 9c762731, 7b2660f9; commits d1089944-91945f51 failed `online` / `online_env` (the new DW4_OVERFLOW* variables were on neither list; fixed in 8c919404) |
| overflow_screens with the data | 7,731 checks, 0 failures on 0e105d02 (`build/screens_tests19.log`): T9 133 strings 0 missing, ASCII the font lacks `#$%*/;=@[\]`{}~` drawn with stand-ins, 0 gaps |
| T1 det.sh on the review fixes | 4004f056 / 396cb219 both runs on ef082240 and 0e105d02 (`work/dref/checks/det/det_o1bD_final`) |
| The reviewer's reopen walk (walk.txt + `DW4_OVERFLOW=1 DW4_OVERFLOW_SEEN=1`, 7600 B, 7700 A 8) on 6ba9011a | 7650 "Come back any time!", 7720 / 7790 / 7850 closed (the held A stays out), 8050 open again only from the script's new 7900 A (`build/shots/reopen.log`, `reopen_fixed_sheet.png`) |
| T9 | 128 strings, 0 missing glyphs (the font has 484 characters, no '/') |
| T1 det.sh | 4004f056 / 396cb219 both runs (d1089944, 0e38a86c, 9c762731) |
| T4-like | the lobby's select, a kite run of 900 steps and back, `DW4_STATE_TEST=4600` and `=5000` (the input log spans the dialog's neutral pads and the run): "identical after loading" (800 and 400 frames) |
| T2 r25c, NPC shown (`DW4_OVERFLOW=show`, port events collected in the lobby, H0 on) | 10 / 10 segments, 1,748,011 frames, every save "same story" on d1089944 (`runs/seg/o1bD_show`); on the head 0e105d02: T2_SHOW_FINAL |
| T2 r25c, NPC hidden | 10 / 10, 1,748,011 frames, same story on 8c919404 (`runs/seg/o1bD_plain`; the commits after it change no story code: the lobby, the screens, the driver's HUD and game_main's NPC block, which runs only with the NPC shown) |
| Isolation (T3-like) | new game into the lobby, the select opened (`DW4_OVERFLOW_SCREEN=select`), A A by the script: a kite run of 1,800 steps, back, 7,000 story frames; reference: the same script without the two presses and without the NPC: 140 of 140 fingerprint lines equal (every 50 frames, `build/t3/A.log`, `B.log`). Caveat (review): the story stood in its own intro talk the whole window (ctrl 0), so no movement after the return was compared there |
| Isolation with a walk after the return (T3 variant, 0e105d02) | `build/t3/walkback.sh`: the intro by A presses, GOTO to Gekomon, talk, Enter, select A A, a kite run of 600 steps, Welcome back, B, A (held 8), then three GOTOs through the lobby; reference: the same without the NPC and without the dialog's presses: 186 of 186 fingerprint lines equal (every 50 frames to frame 9,300; the run started at story frame 7,710, Welcome back opened, both runs end walking at (21.6, 10.0, -59.1) with the control; `build/t3/WB_run.log`, `WB_ref.log`, `walkback_result.txt`) |
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

After the review fixes (0e105d02): `build/shots/o1b_fix_sheet.png` with
`fix_sel_4450` (Dorumon: ⊃Ice, ∩Dark as the dark ball), `fix_hud_overflow_2400`
(Lv 5, the EXP bar inside the level), `fix_menus_overflow_660` (the build page:
"10 - cap 1300" without a gap), `fix_results_overflow_745` (the results' lines
by director steps), `reopen_7720` (closed after A, no reopening) and
`reopen_8050` (open again from the script's next A); `fix_sel_glyphs_zoom.png`.

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
  The finale timer is a plate in the quest timer's colours, not the quest
  timer's model (same reason).
- **UI sounds** (WINDOW_OPEN / CLOSE, CURSOR_*: a second audio stream, 13.6):
  not built.
- **Character select gaps against 9.2 / 9.3** (review): the faces are text
  stand-ins ("Agum", "Veem", ...), not the charaicon face sheet (doable with
  `SimUi::image` and the sheet's texture); STR / INT are not shown; locked
  faces show "?" without their record condition (`select_locked_name` /
  `_hint` unused); the class line is shortened instead of shinka's
  EXPLANATION line; the PWR Medal has no element.
- **The HUD's gear glyphs** (∋ / ∨ / ∧): `RunScreenData::gear` is never
  filled; `build_row` skips E's "Core: / Board: / Chips:" lines (a parser for
  them or E's gear getter after the merge).
- **Merge**: no hand work left in `fill_screen_data` and the records page
  (requires-guards / `__has_include`). F's three glue calls in game_main stay
  F's (contract F 16:50 / 16:59). `d.guardian` stays empty (the arena's own
  boss bar is E's).
- The "Beaten by a Gekomon" line needs the defeating family: the director
  (lane E's file) would keep the last damaging hit's `HitTaken::shooter`, the
  driver's `RunStatus` hand it to `RunOutcome` and game_main to
  `RunSummary::defeated_by` (ready, unit tested).
- In the interactive path, when one pass runs several story steps and the
  dialog opens in the first, the later steps of that pass still get the live
  pads (the next pass is neutral); not seen at 60 Hz.
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
# T3 variant with a walk after the return (EXE= a copy; two runs one after another):
bash build/t3/walkback.sh
#   walkback_ref.txt: work/dref/scripts/newgame_lobby.txt to 3500 START, "A 3" every 150 frames 4300-6700,
#   7000 GOTO 50 -68 400, 8000 GOTO 30 -40 300, 8400 GOTO 50 -68 300, 8800 GOTO 20 -60 300;
#   walkback_run.txt adds 7500 A, 7560 A, 7620 A, 7680 A, 7800 A (3 frames each), 7860 B 3, 7920 A 8;
#   run: DW4_OVERFLOW=1 DW4_OVERFLOW_SEEN=1 DW4_OVERFLOW_TEST_RUN="bot=kite frames=600", both DW4_TRACE_FINGERPRINT=50,
#   --smoke-test 9300; compare the "[N] fingerprint" lines.
# The reviewer's reopen walk: build/t3/reopen.txt with DW4_OVERFLOW=1 DW4_OVERFLOW_SEEN=1, --smoke-test 8300
# T2 with the NPC shown (3 at a time):
EXE=<copy> ENV="DW4_OVERFLOW=show" NAME=<name> nice -n 10 bash work/campaign/segments.sh r25c 3
```
