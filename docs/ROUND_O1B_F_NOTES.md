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
| The profile: format of 8.5 (header, one fact per line, `end`), unknown lines and form keys kept for a later version, values clamped, damaged lines dropped, CRLF and a BOM read | `src/game/overflow_profile.{hpp,cpp}` `OverflowProfile::parse / serialize` |
| The store: load order file, `.bak`, what a cut file still holds, a new profile; a damaged file kept as `overflow.dat.damaged`; the atomic write with a `.bak` only of a complete file (`complete_profile`); a failed write keeps the change and says why | `MetaStore::open / save`, `meta()` |
| The path rule: `DW4_OVERFLOW_PROFILE` wins, test runs (`--smoke-test`) without it keep the profile in memory and never write, else `<program>/saves/overflow.dat` | `profile_path`, opened in `game_main.cpp` before the StorySwitch |
| The writes of 8.5: the first meeting, a Plug Lab purchase, a loadout change, a run's results | `mark_intro_seen`, `buy_plug`, `set_loadout`, `record_run` (+ `set_last`) |
| Plug Lab: 7 stats per form, 10 steps of +5 UPCNT, step n costs 100 x n, no refund; rows in the screen's order Strength, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed; points by UPCNT index for the run | `plug_lab_rows`, `buy_plug`, `buy_plug_row`, `plug_lab_points` |
| Loadout: lane E's gear keys; the start set and what records opened; the slots; a core-pocket chip (Resist ROMs, Defence ROM) only with a Core (8.3) | `start_gear`, `record_gear`, `opened_gear`, `gear_open`, `loadout_allowed` |
| Data Records: all 23 of 8.4 with title, condition and what they open, checked from the profile's totals and bests (so a record a later version adds is met by an old file's numbers), dated | `record_table`, `check_records`, `add_run` |
| Form unlocks of 5.3 (used from O3; the story's forms as a read-only bitmask, 17.1 answer 7: player 1's BASE_ISUSE of the story engine) and their texts | `form_unlocked`, `form_unlock_text`, `story_forms` |
| Sim Bits of 4.9: the run's bits x the tier (1, 1.5, 2), +500 x the tier on a clear | `run_sim_bits`, `tier_factor` |
| The run's bits (4.7): the run drops no g_000bit yet, so the driver credits 5 % of the defeats with the original's band for the level x (100 + BITRITSU) / 100, from its own splitmix64 (seeded from the run seed). The run engine and the director's draws are untouched | `defeat_bits`, `bits_seed`; `DirectorDriver::count_bits / run_bits` |
| The run driver: `apply_meta` puts the form's loadout, Plug Lab points and opened gear into the request (a request's own options win); `copy_meta` hands the loadout and gear to lane E's RunConfig fields where the build has them (requires-guards; checked against E's headers of 5b3fa033); the Plug Lab's UPCNT is the host's alone (`give_plug_lab`, once the run started; RunConfig::plug_lab stays at zeros, so the points are given exactly once); the run is recorded when the results open (or when the window closes: Retire); the O1a overlay shows "Sim Bits +N" and "NEW! <record>"; the HUD shows Bits | `src/game/overflow_driver.cpp` |
| Lane D's LobbyMeta from the profile (a template on D's field names), the select's start (`remember_start`), the Plug Lab row purchase, and the merge's glue on D's OverflowLobby / LobbyAction (compiled against D's headers of d1089944) | `fill_lobby_meta`, `remember_start`, `buy_plug_row`, `connect_lobby`, `lobby_action`, `refresh_lobby` |
| T7 (profile) | `tests/overflow_profile_tests.cpp`, ctest `overflow_profile` (787 checks) |

## 2. Checks (06.11)

| Test | Result |
| --- | --- |
| T7 `overflow_profile` | 787 checks pass: the 8.5 example, round trip, later versions, damaged text, the store's writes and every corruption case (cut, garbage, empty, missing with `.bak`, both damaged, both unreadable, a failed write), migration from no file (nothing written until the first write), Plug Lab, bits, Sim Bits, records, loadouts, form unlocks, the request's options, the path rule, LobbyMeta, the story's forms, the lobby glue |
| ctest (all) | 57 of 57 (with `online` / `online_env`: DW4_OVERFLOW_PROFILE is on the presentation list) |
| T1 det.sh (`det_o1bmeta`) | 4004f056 / 396cb219 (both runs) |
| T3 / T5 (`build/t3/t3meta.sh`, in the worktree's build folder) | the det script with a run at story frame 3000 (Agumon, kite bot, 1,800 steps) and without: 20 fingerprint lines equal (4004f056), the card folders equal (empty), the achievement profiles equal (unlock times aside), only the run writes the `DW4_OVERFLOW_PROFILE` file |
| T4 (the T3 script with `DW4_STATE_TEST` 2990 / 3600 / 4200 and `DW4_STATE_FILE`, a run at story frame 3000 with a profile) | identical after loading, all three (3010, 2400, 1800 frames compared); the run's hash the same in all (471b26cdc67e1a58) |
| a hidden run with a profile (`plug p_agumon mhp 10 str 4`, 18,000 steps) | the Plug Lab points reach the player (the first minute's highest hit 1/60 of MaxHP instead of 1/48); 817 defeats, 1,125 bits, Sim Bits 100 -> 1,225, the `.bak` written, the best line and totals right |
| the same run without a profile | nothing written (a test run keeps it in memory) |
| T2 r25c (`EXE=<copy> NAME=seg_r25c_o1bmeta work/campaign/segments.sh r25c 4`, without the NPC) | 10 / 10 segments reached, 1,748,011 frames, every save "same story" |
| T6 (`--overflow-run form=p_agumon field=stagbs02 tier=normal seed=1 bot=kite offers=priority --smoke-test 90000`, twice) | the same run hash twice (770c6e5ef2d0a29c; defeated at 22:01, Lv 36, 13,690 Sim Bits, 5 records); with a profile (plugs str 6 def 4 mhp 8, Brave Core + Wisdom Chip) twice: the same hash twice (3bd37754fb5c94f3), the summary's `meta_used` line |

## 3. Left / open

- **Merge glue (at F's merge, after D), three calls in game_main** where
  D's `OverflowLobby` lives: `connect_lobby(lobby, story_forms(engine))`
  once; `lobby_action(lobby, action, story_forms(engine))` for every
  LobbyAction (for start_run before the StorySwitch's request);
  `refresh_lobby(lobby, story_forms(engine))` after a run came back. D's
  results screen shows `meta().last_reward()` (Sim Bits, NEW! records) as
  O1a's overlay does here; D's HUD reads `run_bits(run)`.
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
exe=$1; R=/c/Users/shinz/source/repos/DW4_PC-wt/o1b-meta/build/t3/out; rm -rf $R; mkdir -p $R
cd /c/Users/shinz/source/repos/DW4_PC
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
