# Round O1b lane F (meta): notes

Lane F of round O1b (docs/SURVIVAL.md 15.2): the meta file
`saves/overflow.dat` (8.5), Sim Bits (8.1), the Plug Lab (8.2), the
starting loadout (8.3), the Data Records (8.4) and their hooks for the
lobby screens (lane D). Branch `feat-o1b-meta` from main d0e005b0, worktree
`DW4_PC-wt/o1b-meta`. Merge order of the round: E, D, F (this lane last).
The lanes' interface decisions are in `DW4_PC-wt/o1b-contract.md` (outside
the repo); the parts that concern this lane are repeated here.

## 1. Done

| What | Where |
| --- | --- |
| The profile: format of 8.5 (header, one fact per line, `end`); of a later version's file it keeps whole unknown lines, later `form` keys and (header version higher) known lines in a shape it cannot read, verbatim; it drops later keys on a `plug` line, keeps at most 512 unknown lines and writes its own version in the header; values clamped; CRLF and a BOM read. A line this version should read but cannot (a bad value, a wrong shape, damaged bytes) makes the file damaged (not complete) | `src/game/overflow_profile.{hpp,cpp}` `OverflowProfile::parse / serialize` |
| The store: load order file, `.bak`, what a damaged file still holds, a new profile; a damaged file kept as `overflow.dat.damaged` and never made the `.bak` (the atomic write keeps a `.bak` only of a complete file, `complete_profile`); a failed write keeps the change and says why; a file (or the `.bak` of a damaged file) that is there but locked is retried (4 tries, 150 ms apart), then the session is read only (never writes; `read_only()`, `save_problem()` for the screens) | `MetaStore::open / save`, `meta()` |
| The path rule: `DW4_OVERFLOW_PROFILE` wins, test runs (`--smoke-test`) without it keep the profile in memory and never write, else `<program>/saves/overflow.dat` | `profile_path`, opened in `game_main.cpp` before the StorySwitch |
| The writes of 8.5: the first meeting, a Plug Lab purchase, a loadout change, a run's results | `mark_intro_seen`, `buy_plug`, `set_loadout`, `record_run` (+ `set_last`) |
| Plug Lab: 7 stats per form, 10 steps of +5 UPCNT, step n costs 100 x n, no refund; rows in the screen's order Strength, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed; points by UPCNT index for the run | `plug_lab_rows`, `buy_plug`, `buy_plug_row`, `plug_lab_points` |
| Loadout: lane E's gear keys; the start set and what records opened; the slots; a core-pocket chip (Resist ROMs, Defence ROM) only with a Core (8.3) | `start_gear`, `record_gear`, `opened_gear`, `gear_open`, `loadout_allowed` |
| Data Records: all 23 of 8.4 with title, condition and what they open, checked from the profile's totals and bests (so a record a later version adds is met by an old file's numbers), dated | `record_table`, `check_records`, `add_run` |
| Form unlocks of 5.3 (used from O3; the story's forms as a read-only bitmask, 17.1 answer 7: player 1's BASE_ISUSE of the story engine) and their texts | `form_unlocked`, `form_unlock_text`, `story_forms` |
| Sim Bits of 4.9: the run's bits x the tier (1, 1.5, 2), +500 x the tier on a clear | `run_sim_bits`, `tier_factor` |
| The run's bits (4.7): the run drops no g_000bit yet, so the driver credits 5 % of the defeats with the original's band for the level x (100 + BITRITSU) / 100, from its own splitmix64 (seeded from the run seed). The run engine and the director's draws are untouched | `defeat_bits`, `bits_seed`; `DirectorDriver::count_bits / run_bits` |
| The run driver: `apply_meta` puts the form's loadout, Plug Lab points and opened gear into the request (a request's own options win); `copy_meta` hands the loadout and gear to lane E's RunConfig fields where the build has them (requires-guards; checked against E's headers of 5b3fa033); the Plug Lab's UPCNT is the host's alone (`give_plug_lab`, once the run started; RunConfig::plug_lab stays at zeros, so the points are given exactly once); the run is recorded when the results open (or when the window closes: Retire); the results show "Sim Bits +N", "NEW! <record>" and "Not saved: ..." when the profile is not saved, the HUD shows Bits: in O1a's overlay here, in D's SimUi screens through `fill_meta_screen(d, run)` (called at the end of D's `fill_screen_data` at the merge); the `log=` file ends with the `meta_used` line (13.5: a run again = its request keys + these options, no profile); `results_hold=STEPS` (test runs with pictures) | `src/game/overflow_driver.cpp` |
| Lane D's LobbyMeta from the profile (a template on D's field names, incl. `loadouts`: the select starts at the form's last allowed loadout), the select's start (`remember_start`), the Plug Lab row purchase, and the merge's glue on D's OverflowLobby / LobbyAction (compiled against D's final head 545ab233 in the trial merge) | `fill_lobby_meta`, `remember_start`, `buy_plug_row`, `connect_lobby`, `lobby_action`, `refresh_lobby` |
| `apply_meta` leaves `armor=` / `chip=` out when the request has lane E's test key `loadout=` | `apply_meta` |
| T7 (profile) | `tests/overflow_profile_tests.cpp`, ctest `overflow_profile` (829 checks) |
| The select's lists offer only gear the run can equip (the start set and the record gear lane E's gear_table has: in O1b the Drain Ram); RunConfig::gear_open names every record-opened key | `gear_built`, `kBuiltRecordGear` (add a key there when a round adds its gear row) |

## 2. Checks (06.11)

| Test | Result |
| --- | --- |
| T7 `overflow_profile` | 798 checks pass: the 8.5 example, round trip, later versions, damaged text, the store's writes and every corruption case (cut, garbage, empty, missing with `.bak`, both damaged, both unreadable, a failed write), migration from no file (nothing written until the first write), Plug Lab, bits, Sim Bits, records, loadouts, form unlocks, the request's options, the path rule, LobbyMeta, the story's forms, the lobby glue |
| ctest (all) | 57 of 57 (with `online` / `online_env`: DW4_OVERFLOW_PROFILE is on the presentation list) |
| T1 det.sh (`det_o1bmeta`) | 4004f056 / 396cb219 (both runs) |
| T3 / T5 (`build/t3/t3meta.sh`, in the worktree's build folder) | the det script with a run at story frame 3000 (Agumon, kite bot, 1,800 steps) and without: 20 fingerprint lines equal (4004f056), the card folders equal (empty), the achievement profiles equal (unlock times aside), only the run writes the `DW4_OVERFLOW_PROFILE` file |
| T4 (the T3 script with `DW4_STATE_TEST` 2990 / 3600 / 4200 and `DW4_STATE_FILE`, a run at story frame 3000 with a profile) | identical after loading, all three (3010, 2400, 1800 frames compared); the run's hash the same in all (471b26cdc67e1a58) |
| a hidden run with a profile (`plug p_agumon mhp 10 str 4`, 18,000 steps) | the Plug Lab points reach the player (the first minute's highest hit 1/60 of MaxHP instead of 1/48); 817 defeats, 1,125 bits, Sim Bits 100 -> 1,225, the `.bak` written, the best line and totals right |
| the same run without a profile | nothing written (a test run keeps it in memory) |
| T2 r25c (`EXE=<copy> NAME=seg_r25c_o1bmeta work/campaign/segments.sh r25c 4`, without the NPC) | 10 / 10 segments reached, 1,748,011 frames, every save "same story" |
| T6 (`--overflow-run form=p_agumon field=stagbs02 tier=normal seed=1 bot=kite offers=priority --smoke-test 90000`, twice) | the same run hash twice (770c6e5ef2d0a29c; defeated at 22:01, Lv 36, 13,690 Sim Bits, 5 records); with a profile (plugs str 6 def 4 mhp 8, Brave Core + Wisdom Chip) twice: the same hash twice (3bd37754fb5c94f3), the summary's `meta_used` line |

| **Review fixes (06.11 17:20-17:50)** | |
| T7 after the fixes | 829 checks: a locked file / a locked `.bak` of a damaged file (read only, nothing written), one damaged value with a good `.bak`, a later version's other shapes kept, D 545ab233's LobbyMeta with `loadouts`, `loadout=` key, T8 write time |
| ctest (all, F's branch d9f5d784) | 57 / 57 (`build/ctest4.log`) |
| T1 det.sh (`det_o1bmeta2`, F d9f5d784; `det_o1bmeta3`, F b94bd452+, the final driver) | 4004f056 / 396cb219 both runs, both times (`build/det2.log`, `build/det3.log`); ctest 57 / 57 again (`build/ctest5.log`). T2 r25c not rerun: no engine or story code changed in the fixes (only overflow_profile / overflow_driver, which run only in the mode) |
| T8 (a write) | 4.4-9.4 ms per Plug Lab purchase (temporary file, flush, read back, rename, `.bak`), printed by T7; the results' write is one such write in one frozen step |
| Trial merge 2 (local branch `tmp-o1b-trial2`, not for main): E a1ac5ddd, D 545ab233, F (then F again for `results_hold=`), glue commit bc1a3f39 | one conflict (game_main's includes: keep both); the glue commit is F's part of the merge (cherry-pick it after merging F); build ok; ctest 60 / 60 (`build/ctestT2.log`); det.sh 4004f056 / 396cb219 (`det_o1btrial2`, `build/detT2.log`). With F's final 5de64907 merged in (trial 2 head d6fa2dff): build ok, ctest 60 / 60 (`build/ctestT3.log`), det.sh 4004f056 / 396cb219 (`det_o1btrial2f`, `build/detT3.log`) |
| Hidden pictures of trial 2 (`DW4_RENDER=1`, not in git) | `build/shots3/hud_7000.png`: HUD "Defeated 203 / Bits 182" at 01:56 (the reviewer's run showed Bits 0); `build/shots2/results_850.png`: the results with "Sim Bits +0 (300)" and the yellow "NEW! Thousand opens Achilles Board" (profile with 995 defeats, `results_hold=300`, D's retire commands); `build/shots4/locked_results_850.png`: the profile locked for the whole run (`build/shots4/lock.ps1`): the `.bak`'s data used (plugs, Blossom Data's gear), "Not saved: overflow.dat was in use when the game started.", the file unchanged (hash equal) |
| T5 with a real save (`build/t3/t5meta.sh`, not in git: t3meta.sh with a campaign segment's save card copied into both card folders first) | A (a run at story frame 3000) and B (none): 20 fingerprint lines equal (fb137c77: the save on the card changes the title's path, the same in both), the card folders equal (812bd5c2), the save byte for byte as before (cc9685b4), only A writes the profile (`build/t3/t5.log`) |
| 13.5 replay from the log (`build/t3/replay.sh`) | a run with a profile (plugs, Brave Core + Wisdom Chip, Blossom Data), then the same request keys + the log's `meta_used` options without a profile: the same run hash twice (e9d5477a3ed64e61; `build/t3/replay.log`) |
| Balance sample (trial 2 exe = E's final a1ac5ddd; `build/bal2/bal.sh`, `BOTS="kite human" SEEDS=4`; Agumon, Normal, stagbs02, offers=priority; `build/bal2/results.txt`) | kite without plugs: 4 / 4 cleared (22:08-23:54, Lv 38), Sim Bits +15,634 / 16,740 / 17,662 / 16,449; kite with a full Plug Lab: 4 / 4 cleared (22:05-23:25, Lv 37-39), +17,023 / 18,302 / 16,187 / 17,299; human without plugs: 3 / 4 cleared (one defeated at 21:46.8), +19,835 / 15,725 (defeated) / 19,267 / 17,740; human with a full Plug Lab: 4 / 4 cleared (22:37-24:10, Lv 38-40), +15,713 / 19,548 / 21,668 / 18,736. Every first clear: 11-12 new records |
| Trial merge E, D, F (local branch `tmp-o1b-trial` @ 303ea640, not for main; E 57add3ee, D 8c919404, F ec80c130; superseded by trial 2) | one conflict (game_main's includes), the lobby glue added (three calls); builds; ctest 59 / 59; det.sh 4004f056 / 396cb219; T6 Agumon with a profile (plugs, Brave Core + Drain Ram, Blossom Data): cleared at 22:48.8, Lv 40, the four Guardians, Awakening, 1,724 casts, 265 Fire defeats, 20,961 Sim Bits, 12 new records, all in the file; Guilmon without a profile: cleared, 14,361 Sim Bits |
| T9 for lane F's strings (scratch check in the trial build: record titles, conditions, opens, the unlock texts, the Plug Lab names, the results' lines against bectext.igb) | 121 strings, 0 missing glyphs |

## 3. Left / open

- **Merge glue: lane F's job, at F's merge (F merges last).** After
  `git merge feat-o1b-meta` (one conflict: game_main's includes, keep both
  `overflow_lobby.hpp` and `overflow_profile.hpp`), cherry-pick the glue
  commit of `tmp-o1b-trial2` (bc1a3f39, "Trial merge 2 glue"): game_main
  `connect_lobby` after `OverflowLobby overflow_lobby;` (DW4_OVERFLOW_SEEN
  writes the first meeting), `lobby_action` at the top of D's
  `overflow_lobby_action` lambda, `refresh_lobby` before
  `overflow_lobby.returned(back)`, the save problem appended to the state
  message; overflow_driver: `fill_meta_screen(d, run);` as the last line of
  D's `fill_screen_data` (the HUD's Bits, the results' Sim Bits / NEW! /
  Not saved). E's finale getters in `fill_screen_data` are E's part.
- **T9 for F's strings at the merge:** F's texts (23 record titles,
  conditions, "opens", 12 unlock texts, the Plug Lab names,
  `buy_result_text`, "Sim Bits +", "NEW! ... opens", "Not saved: ...")
  live in overflow_profile.cpp / overflow_driver.cpp, not in D's
  overflow_text.inc; checked once (0 missing glyphs, trial 1). At the merge
  either move them into overflow_text.inc or let D's overflow_text_tests
  also walk `record_table()`, `form_unlock_text()`, `plug_lab_name()` and
  `buy_result_text()` (link overflow_profile.cpp).
- D's lobby shows no save problem itself (only the state message after a
  run and the results); `meta().save_problem()` is there for the Plug Lab
  screen.
- **E:** must not apply `RunConfig::plug_lab` (F's host side does; F leaves
  it at zeros). The Adept records need a `tech_defeats()` getter (4 ints),
  Crate Breaker a `crates()` getter (O2): the driver reads them once they
  exist. At the merge `overflow_driver.cpp` has E's lines next to F's in the
  constructor and after_step (E's `loadout=` parse before F's `copy_meta`).
- The Data Records screen's second page (per form, clears per tier) and the
  Plug Lab screen are D's; the data is all here.
- Endless (O4) is in the format (`best_endless`, `RunReport::endless`), not
  played yet. The ?????? offer, Extensions and Hard / Very Hard are opened by
  records but not built (O2-O4).
- Balance (O4, for the user's decision): a Plug Lab form costs 38,500 in
  all (7 x 5,500). Measured (see 2., E's final head a1ac5ddd): a cleared Agumon Normal
  run earns 15,600-18,300 Sim Bits with the kite bot and 15,700-21,700
  with the human bot (the reviewer's 21,000-24,000 were on E's older head
  57add3ee, before the Guardian and finale balance); a full Plug Lab adds
  no clear for the kite bot (4 / 4 either way) and one for the human bot
  (4 / 4 against 3 / 4) in this small sample. Every first clear brings
  12 new records (Banker included: 5,000 earned). So a form is full after
  two to three cleared runs (38,500 / about 17,000). Whether a full Plug Lab
  devalues Normal is open: the kite bot clears these seeds without plugs
  too. The 20-seed charge of 14.2 (with and without a full Plug Lab, kite
  and human) was not run in this round (the PC shuts down at 18:30); run it
  with `build/bal2/bal.sh` (`BOTS="kite human" SEEDS=20`, nice -n 10, at
  most 4 processes) before the user decides on 8.2 / 4.7.
- T2's second part (r25c with `DW4_OVERFLOW=show`, the NPC drawn) and T9 are
  lane D's; T10 is untouched by this lane (no engine code).

## 4. How to run the tests

```
cmd /c build\build_meta.cmd                     (Ninja, build/Meta; FETCHCONTENT from the main repo)
build/Meta/dw4_overflow_profile_tests.exe        (or ctest --test-dir build/Meta -R overflow_profile)
cp build/Meta/dw4_game.exe build/detexe/
cd ../../DW4_PC && bash work/dref/checks/det.sh <worktree>/build/detexe/dw4_game.exe o1bmeta
bash build/t3/t3meta.sh <worktree>/build/detexe/dw4_game.exe
```

A hidden run with a profile:

```
DW4_NO_ERROR_BOX=1 DW4_OVERFLOW_PROFILE=<file> dw4_game.exe --data <game> --card-dir <empty folder>
  --overflow-run form=p_agumon field=stagbs02 seed=1 bot=kite summary=<file> --smoke-test 18000
```

prints `overflow: meta <outcome> bits N -> Sim Bits +N (balance M), K new records, written to <file>`.

The T3 / T5 check (`build/t3/t3meta.sh`, not in git; recreate it from here):

```bash
#!/bin/bash
# Lane F's T3 / T5 check: the det script (new game into dng101b1, 6000 story frames) with a run at story frame 3000
# (A) and without (B); the story's fingerprint lines, the card folders and the achievement profiles must be equal,
# only A writes the DW4_OVERFLOW_PROFILE file.
export PATH=/usr/bin:/mingw64/bin:/c/Windows/System32:/c/Windows:$PATH
exe=$1; R=~/source/repos/DW4_PC-wt/o1b-meta/build/t3/out; rm -rf $R; mkdir -p $R
cd ~/source/repos/DW4_PC
for v in A B; do
  mkdir -p "$R/$v/card/Card A" "$R/$v/card/Card B"
  extra=(); [ $v = A ] && extra=(--overflow-run form=p_agumon seed=3 bot=kite at=3000 frames=1800)
  DW4_NO_ERROR_BOX=1 DW4_TRACE_FINGERPRINT=300 DW4_ACHIEVEMENTS=1 DW4_PROFILE="$(cygpath -w $R/$v/profile.dat)" \
  DW4_OVERFLOW_PROFILE="$(cygpath -w $R/$v/overflow.dat)" nice -n 10 "$exe" --data "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted" \
    --smoke-test 6000 --card-dir "$(cygpath -w $R/$v/card)" --input "$(cygpath -w work/dref/scripts/newgame_dng101b1.txt)" "${extra[@]}" > $R/$v/run.log 2>&1 &
done
wait
for v in A B; do echo "$v $(grep -c 'fingerprint' $R/$v/run.log) lines md5 $(grep 'fingerprint' $R/$v/run.log | md5sum | cut -c1-8) cards $(cd $R/$v/card && find . -type f | sort | xargs -r md5sum | md5sum | cut -c1-8) profile $( [ -f $R/$v/profile.dat ] && grep -v '^unlock' $R/$v/profile.dat | md5sum | cut -c1-8) overflow $( [ -f $R/$v/overflow.dat ] && echo yes || echo no)"; done
grep -h 'overflow: run\|overflow: meta' $R/A/run.log
```

## 5. The review of 06.11 (findings and answers)

| Finding | Answer |
| --- | --- |
| D's SimUi screens show no meta (HUD Bits 0, no Sim Bits / NEW!) | fixed: `fill_meta_screen(d, run)` (d9f5d784), called from D's `fill_screen_data` by the glue commit (trial 2 bc1a3f39); pictures in 2. |
| `fill_lobby_meta` leaves D's `LobbyMeta::loadouts` empty | fixed (d9f5d784); T7's stand-in is D 545ab233's struct |
| A locked `overflow.dat` starts a new profile that replaces it | fixed: retried, then read only (d9f5d784); T7 + a locked live run (2.) |
| The `log=` file keeps "loadout none / meta none" | fixed: the log ends with `meta_used ...` (d9f5d784). Commit 02d20a95's title ("the run log keeps the meta state") was true only for the summary file until then. E's director lines `loadout` / `meta` stay E's |
| Forward compatibility claimed too widely | fixed in part: a later version's known lines of another shape are kept (d9f5d784); SURVIVAL 8.5 and 1. now say exactly what is kept and dropped (6dcc63fa). Kept as is: the header is written with this program's version (the content is this version's plus the kept lines; a later program must not take it for its own format) |
| One damaged value in a complete file is dropped silently and the file becomes the `.bak` | fixed: such a file is damaged (not complete): the `.bak` wins, `.damaged` kept, never made the `.bak` (d9f5d784) |
| Balance claims weak | measured again on E's final head (2.: 16 runs, kite and human, with and without a full Plug Lab); 3. corrected; the 20-seed charge of 14.2 is left (time) |
| F's strings not in overflow_text.inc / T9 | left for the merge (3.) |
| `apply_meta` overrides E's `loadout=` | fixed (d9f5d784) |
| A failed write only on stderr | fixed: "Not saved: ..." on the results and in the state message after a run (glue), `save_problem()` |
| T8 not measured, T5 compares empty card folders, trial merge stale | T8: T7 prints a write's time (761b450a); trial 2 with the final heads (2.); T5 with a real save in both card folders (2.) |
