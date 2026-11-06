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
| Form unlocks of 5.3 (used from O3; the story's forms as a read-only bitmask, 17.1 answer 7) and their texts | `form_unlocked`, `form_unlock_text` |
| Sim Bits of 4.9: the run's bits x the tier (1, 1.5, 2), +500 x the tier on a clear | `run_sim_bits`, `tier_factor` |
| The run's bits (4.7): the run drops no g_000bit yet, so the driver credits 5 % of the defeats with the original's band for the level x (100 + BITRITSU) / 100, from its own splitmix64 (seeded from the run seed). The run engine and the director's draws are untouched | `defeat_bits`, `bits_seed`; `DirectorDriver::count_bits / run_bits` |
| The run driver: `apply_meta` puts the form's loadout, Plug Lab points and opened gear into the request (a request's own options win); `copy_meta` hands them to lane E's RunConfig fields where the build has them (requires-guards); until E's director takes `plug_lab` the host gives the run player the UPCNT once the run started; the run is recorded when the results open (or when the window closes: Retire); the O1a overlay shows "Sim Bits +N" and "NEW! <record>"; the HUD shows Bits | `src/game/overflow_driver.cpp` |
| Lane D's LobbyMeta from the profile (a template on D's field names), the select's start (`remember_start`), the Plug Lab row purchase | `fill_lobby_meta`, `remember_start`, `buy_plug_row` |
| T7 (profile) | `tests/overflow_profile_tests.cpp`, ctest `overflow_profile` (778 checks) |

## 2. Checks (06.11)

| Test | Result |
| --- | --- |
| T7 `overflow_profile` | 778 checks pass: the 8.5 example, round trip, later versions, damaged text, the store's writes and every corruption case (cut, garbage, empty, missing with `.bak`, both damaged, both unreadable, a failed write), migration from no file (nothing written until the first write), Plug Lab, bits, Sim Bits, records, loadouts, form unlocks, the request's options, the path rule, LobbyMeta |
| T1 det.sh (`det_o1bmeta`) | 4004f056 / 396cb219 (both runs) |
| T3 / T5 (`build/t3/t3meta.sh`, in the worktree's build folder) | the det script with a run at story frame 3000 (Agumon, kite bot, 1,800 steps) and without: 20 fingerprint lines equal (4004f056), the card folders equal (empty), the achievement profiles equal (unlock times aside), only the run writes the `DW4_OVERFLOW_PROFILE` file |
| a hidden run with a profile (`plug p_agumon mhp 10 str 4`, 18,000 steps) | the Plug Lab points reach the player (the first minute's highest hit 1/60 of MaxHP instead of 1/48); 817 defeats, 1,125 bits, Sim Bits 100 -> 1,225, the `.bak` written, the best line and totals right |
| the same run without a profile | nothing written (a test run keeps it in memory) |

## 3. Left / open

- **Merge glue (at F's merge, after D):** in game_main, D's `OverflowLobby`:
  `lobby.on_intro_seen = [] { meta().mark_intro_seen(); };`,
  `fill_lobby_meta(meta().profile(), lobby.meta())` at start and after every
  meta change and run, `LobbyAction::buy_plug` -> `buy_plug_row(form, stat)`,
  `LobbyAction::start_run` -> `remember_start(request)` before the switch.
  D's results screen shows `meta().last_reward()` (Sim Bits, NEW! records)
  as O1a's overlay does here; D's HUD reads `run_bits(run)`.
- **E:** apply `RunConfig::plug_lab` at the run start (UPCNT index; then the
  host's own `give_plug_lab` switches off by itself). The Adept records need
  a `tech_defeats()` getter (4 ints), Crate Breaker a `crates()` getter (O2):
  the driver reads them once they exist.
- The Data Records screen's second page (per form, clears per tier) and the
  Plug Lab screen are D's; the data is all here.
- Endless (O4) is in the format (`best_endless`, `RunReport::endless`), not
  played yet. The ?????? offer, Extensions and Hard / Very Hard are opened by
  records but not built (O2-O4).
- r25c (T2) was not run in this lane: lane F changes nothing in the story
  engine (only the host opens a file); det.sh and the T3 story lines are
  unchanged.

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
