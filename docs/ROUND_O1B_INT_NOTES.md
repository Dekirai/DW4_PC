# Integration of round O1b (branch `int-o1b`)

Branch `int-o1b` from main d0e005b0, worktree `DW4_PC-wt/int-o1b`. Not pushed, not merged into main. Design:
docs/SURVIVAL.md 15.2 (the round, its tests T1-T11 in 14.1, the targets of 14.2), docs/OVERFLOW_DIRECTOR.md 10-13.
The lanes' notes: docs/ROUND_O1B_E_NOTES.md (content), docs/ROUND_O1B_D_NOTES.md (screens),
docs/ROUND_O1B_F_NOTES.md (meta); their contract (outside the repo): `DW4_PC-wt/o1b-contract.md`.

The user's decisions for the round stand: the run ends at 25:00 with the finale (LordKnightmon, the quest timer, the
finale music); offline only; the story engine never changes (det.sh 4004f056 / 396cb219, r25c 10/10 with 1,748,011
frames, with the NPC hidden and shown).

## 1. Merges (`--no-ff`, in the contract's order)

| Step | Tip | What | Conflicts |
| --- | --- | --- | --- |
| `feat-o1b-content` (E) | 463ac980 (last code c48738d1) | the Guardians of phases 1-3 and LordKnightmon's finale under the quest timer, TIME UP, the return gate, Guilmon and Dorumon, the four Rookies' techniques, gear, Awakening, ctest `overflow_finale` | none |
| `feat-o1b-screens` (D) | ea1f75a9 (last code 0e105d02) | H0's `talk_box` port event, the presentation actor, SimUi, Gekomon in the lobby with the dialog, the select, the Plug Lab and records pages, the run screens, the texts (T9) | none |
| `feat-o1b-meta` (F) | d1e5c3c8 | `saves/overflow.dat`, Sim Bits, the Plug Lab, the loadout, the 23 records, the run's bits | `game_main.cpp`'s includes: both kept (`overflow_lobby.hpp`, `overflow_profile.hpp`), as the contract says |
| cherry-pick bc1a3f39 | 73884246 | lane F's glue: `connect_lobby` / `lobby_action` / `refresh_lobby` in game_main, the save problem in the state message, `fill_meta_screen(d, run)` as the last line of D's `fill_screen_data` (E's finale lines above it) | none |

`CMakeLists.txt`, `overflow_driver.cpp`, `online.cpp` and `SURVIVAL.md` merged without a conflict: E's request keys
and outcome switch, D's screens and F's meta code sit in different places of the driver; D's DW4_OVERFLOW* variables
are on online.cpp's forbidden list, F's DW4_OVERFLOW_PROFILE on the presentation list.

## 2. The wiring (commits 20e8c1fd, 8dbe048a)

| What | Where | Why |
| --- | --- | --- |
| E's finale and TIME UP in D's screens | `overflow_driver.cpp` `fill_screen_data`: `finale()`, `finale_left_steps()` (x 1001/60000 s), `time_up()` read directly; D's O1a stand-in (20:00-25:00 from the run clock) and the requires-guards removed | the guards hid a renamed getter silently; with all three lanes merged there is nothing to guard |
| The results' meta lines once | `fill_screen_data` | after the glue commit D's guarded `meta_lines()` block and F's `fill_meta_screen` both appended them: every results screen would have shown "Sim Bits" and each "NEW!" line twice. D's block is gone; `fill_meta_screen` is the only owner (contract F 17:40) |
| The clear banner | `overflow_screens.cpp` `draw_hud`, `RunScreenData::clear_frames` | E sends no i_bgclear and asked D to show the banner from `d.cleared` (contract E 06.11 18:26), D had not: "SIMULATION COMPLETE" over the arena for 4 s after LordKnightmon falls; the quest timer stops with the run clock |
| E's `defeated_by` into D's Welcome back | `OverflowDirector::defeated_by()` (the last damaging hit's family once the run ended in a defeat; summary line `defeated_by`), `RunStatus::defeated_by`, `RunOutcome::defeated_by` (overflow_host), game_main's `RunSummary back` | D's "Beaten by a Gekomon?" line needed the defeating family (D notes 4); a poison tick never takes the last HP, so the last damaging hit is the defeat |
| F's run bits into the results | `meta_lines()`: "Bits N" (the run's bits as the meta file got them, `recorded_bits_`) above "Sim Bits +N   (balance)"; `fit_result_lines` keeps a "Bits " line with the meta lines | the results hide the HUD, so the run's Bits (pickups and credited defeats, 4.7) were nowhere on the results; the Sim Bits line now shows where its number comes from |
| The HUD's gear glyphs | `build_row` reads E's "Core: X   Board: Y" and "Chips: A / B   ¶ Data N" lines into `RunScreenData::gear` (∋β ∨α ∧γ) | 9.3's build row; D's notes listed the field as never filled |
| T9 over F's texts | `tests/overflow_screens_tests.cpp` `meta_texts()`: the 23 records (title, condition, what they open, the results' NEW! line), the 16 forms' unlock texts, the Plug Lab names, the four purchase answers, both save problems, the Bits and Sim Bits lines, E's fixed notices ("The return gate is open", "TIME UP", "AWAKENING: ...", the Guardian lines, ...): every code point a 9.1 glyph (without data) and in bectext.igb's NORMAL font (with it, no stand-in allowed); `overflow_profile.cpp` linked into the test | the contract's "T9 must cover F's texts" (F notes 3); F's strings stay beside their data in overflow_profile.cpp |
| Unit checks | `overflow_guardians`: `defeated_by` empty while the run goes on, the last damaging family after `run_end(player_defeated)`, empty after a stage exit; `overflow_screens`: the clear banner for 4 s, TIME UP's banner, the Bits line kept past 32 lines, the gear glyphs | |

## 3. Results

Build: Release, Ninja, tests on, `DW4_TEST_DATA` = the extracted game (`build/bld.cmd rel`). Exe copies:
`build/exe/dw4_game_i1.exe` (20e8c1fd), `build/exe/dw4_game_i2.exe` (8dbe048a; the code is unchanged after it).
Every game process ran hidden or headless with `nice -n 15`, at most three at a time (another workflow ran
timing-sensitive online scenarios); no process of another workflow was touched.

| Check | Result | Log (worktree `build/` unless noted) |
| --- | --- | --- |
| build | ok, no new warnings | `bld_rel0.log` (the merges), `bld_rel1.log`, `bld_rel2.log` |
| ctest (full) | 61 of 61 on i1 (main's 58 + `overflow_finale`, `overflow_profile`, `overflow_screens`) | `ctest_rel1.log` |
| T9 (`overflow_screens` with the data) | 133 table strings 0 missing; lane F's texts and E's notices 129 strings 0 missing; ASCII the font lacks drawn with stand-ins, 0 gaps; 10,870 checks | `screens_t9b.log` |
| T7 (`overflow_profile`, `overflow_offers`, `overflow_spawn`, `overflow_rng`, `overflow_balance`, ...) | pass (in the 61) | `ctest_rel1.log` |
| T10 (`overflow_scripts`) | pass (in the 61) | `ctest_rel1.log` |
| T1 det.sh, twice (i1) | 4004f056 / 396cb219 in all four runs | `det1.log`, `det2.log` (runs in main's `work/dref/checks/det/det_int_o1b_1`, `_2`) |
| T6, seed 1, kite, a full run per Rookie (i1) | see 3.1 | `t6_i1.log`, `t/t6_i1/` |

### 3.1 T6: one full run per Rookie

`build/t/t6.sh EXE NAME JOBFILE` (`--overflow-run form=F field=stagbs02 tier=normal seed=1 bot=kite offers=priority
--smoke-test 100000`, summary and log files):

| Form | End | Run clock | Lv | Defeats | Guardians defeated | Awakened | Gate | Highest hit | Lua errors | Run hash | Lane E's hash |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Agumon | cleared | 23:36.6 | 37 | 4,653 | Mammothmon, Anomalocarimon, TonosamaGekomon, LordKnightmon | yes | entered | 9.6 % | 0 | 52e34d6ed1cbaebd (twice) | 52e34d6ed1cbaebd |
| Veemon | cleared | 23:08.5 | 36 | 4,565 | the same four | yes | entered | 12.3 % | 0 | f8ea436b118dbfdf | f8ea436b118dbfdf |
| Guilmon | cleared | 23:25.6 | 37 | 4,719 | the same four | yes | entered | 9.9 % | 0 | 12b363fa1e29d593 | 12b363fa1e29d593 |
| Dorumon | TIME UP | 25:01.5 | 37 | 4,707 | Mammothmon, Anomalocarimon, TonosamaGekomon | yes | - | 11.1 % | 0 | c47b55c29e20d031 | c47b55c29e20d031 |

No non-Guardian clamp in any run. Each run hash equals lane E's head measurement of the same request
(ROUND_O1B_E_NOTES.md 12.2: `runs_b1`, `runs_gB`, `runs_g3`), so the integration did not change a run: lane E's
20-seed tables for the four Rookies and both bots are the integrated build's (section 5).

### 3.2 The story stays the story (T2-T5, T11, the save state suite; exe i2)

| Check | Result | Log / files |
| --- | --- | --- |
| T2 r25c, NPC hidden (`EXE=<i2> NAME=int_o1b_r25c_hidden nice -n 15 work/campaign/segments.sh r25c 3`) | 10 / 10 segments, 1,748,011 frames, every save "same story"; every segment's frames as lane E's (`o1b_content_fin_r25c`) and lane D's (`o1bD_show_0e105d02`) runs | `r25c_hidden.log`, main's `work/campaign/runs/seg/int_o1b_r25c_hidden` |
| T2 r25c, NPC shown (`ENV="DW4_OVERFLOW=show"`, NAME `int_o1b_r25c_show`) | 10 / 10, 1,748,011 frames, every save "same story", the same segment frames (as lane D's run: without pictures, segments.sh's default; the NPC's state, its actor load and the lobby's port events run) | `r25c_show.log`, `.../runs/seg/int_o1b_r25c_show` |
| T3 / T5 with the talk (`build/t/t3talk.sh`: lane D's walkback scripts; the lobby intro, GOTO to Gekomon, the talk by H0 ("talk box at (48.3, 14.0, -74.6) reaches Gekomon"), the select, a kite run of 600 steps at story frame 7710, Welcome back, B, A, three GOTOs; reference: the same walk without the NPC and the dialog's presses) | 186 of 186 fingerprint lines equal (every 50 frames to 9,300, the generator in them; the last 8a7778daab28a43e at the same place); the card folders equal, the achievement profiles equal (ccc3b2c5), only the run wrote `overflow.dat` | `chk_i2_t3talk.log`, `t/t3talk_i2/` |
| T3 / T5 with the det script (lane E's `t35.sh`: a run at story frame 3000, Agumon kite or Guilmon circle, a Guardian after 10 s, 1,800 steps, against none) | A, A2 and B: 20 fingerprint lines 4004f056 each, the card folders equal, the achievement profiles equal (9f14f1fc); Mammothmon / Blossomon entered at run step 600 | `chk_i2_t35.log` |
| T4 (lane E's `t4.sh`: DW4_STATE_TEST 2990 / 3400 / 4400 with the run at 3000, through DW4_STATE_FILE) | identical after loading, all three (3010, 2600, 1600 frames compared) | `chk_i2_t4.log` |
| T4 as 14.1 words it (`build/t/t4talk.sh`: the T3 walk with DW4_STATE_TEST 7400 (before the talk: the replay spans talk, run, return and walk), 7900 (after the return) and 8500 (on the walk)) | identical after loading, all three (1900, 1400, 800 frames compared); the 7400 replay ran the run again (four run lines) | `chk_i2_t4talk.log` |
| T11 (lane E's `t11.sh`: hidden pictures of the det script at 1400 / 2600 / 4800 / 5900 and the new game's lobby at 4800 / 5900, main's client d0e005b0 against i2) | all six pictures equal | `chk_i2_t11.log`, `t/t11_i2/` |
| The save state suite (main's `work/dref/checks/st` copied to `build/t/st`, nice 15, three at a time) | 17 of 17 identical after loading | `chk_i2_st.log` |
