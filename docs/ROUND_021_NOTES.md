# Round 0.2.1: progress notes

Release 0.2.1 (release numbers follow the server; the game's own version stays the CMake project's 0.46.0, protocol 50
unchanged: 0.2.0 clients keep working with a 0.2.1 server and the other way round). Lanes in their own worktrees and
branches: the relay lane (`fix-021-relay`) and the client lane (`fix-021-client`), both from main d230037f, each with
its own section below; merged on branch `int-021` for the check pass ("The integration" at the end).

## The relay lane (branch `fix-021-relay`, from main d230037f)

Branch `fix-021-relay` from main d230037f (release 0.2.0: protocol 50, schema 14, DW4Online 0.2.0), worktree
`DW4_PC-wt/r021relay`. Relay-only fixes for DW4Online 0.2.1; protocol stays 50 (0.2.0 clients work with a 0.2.1 server and
the other way round), no schema change, the game's code unchanged. Own resources: mysqld 3750-3754, ports 60600-60699,
builds and logs in `build/r021/` (gitignored; "How to run" below). Not merged, not pushed; the server's version
(`Directory.Build.props`, 0.2.0) is left to the release's integration.

The task: the four relay findings of "Open after 0.2.0" (docs/ROUND_INT_I1B_NOTES.md) that are bugs, the lag rule's gap
("The flapping host", left), and fix-d43's pre-existing finding on a removed member playing alone. SEAMLESS 19.32
("fix-021") has the rules; 6.4, 6.9, 19.6.6, 19.6.7 and 19.24 say them where they apply.

### Findings and fixes

| # | Finding | Fix | Test (fails without the fix) |
| --- | --- | --- | --- |
| 1 | `StateReporter` took the connected member longest: a host in a grace started by silence (half-open) stayed the reporter, the guests' STATEs were ignored, a held arrival was let in on his old flags or got a NEW (`HoldExpired`) | the reporter is the connected member longest that is not away: not a host in his grace, not resyncing, its slot not lagging, its engine not more than 180 ticks behind (its newest NETFP, `HubMember.ExecutedTick`); `StartGrace` and `HostBack` sync it | `A_host_silent_in_his_grace_hands_the_flags_...`, `A_guests_boss_fight_while_the_hosts_link_hangs_...`, `A_returned_host_who_still_lags_...`, `A_returned_host_whose_engine_still_catches_up_...`; W7h |
| 2 | `OnState` applied the reporter's STATE without the newer-by-tick check: a host back from a drop overrode a guest's newer STATE with the ones of the ticks he caught up | `Supersedes`, one rule for `OnState` and `SyncStateReporter`: none in effect, its member left, or newer by tick; at the same tick only the member in effect's own; an older one is kept (`state_stale`) | `A_host_back_from_a_drop_never_overrides_...` |
| 3 | `StartGrace` at a gate: the gathering stage group counted as shared with the guests, the absence debt shortened the grace although they went on at once | `SharedGroup`: a stage group at its barrier is shared with nobody (a fresh grace); and a grace the debt shortened is fresh again from its start once the host shares no stage group with a guest (`host_grace_fresh`: a link that closed a moment before his BARRIER came, his guests going on without him) | `A_host_who_drops_exactly_at_a_gate_...`, `A_host_whose_link_closes_a_moment_before_his_gate_...`; W7j |
| 4 | the lag rule's gap: drops each below the limit with short bursts in time between them never reached it (guests; a host below `World:HostSilenceSeconds`) | `SlotStream.LagDebt`: filled ticks that held a connected member in time up, minus ticks in time, never below 0 (not while every connected member was filled, a host in his grace, a member resyncing); a lagging member is out at `Relay:LagLeaveSeconds` of it (`lag_leave why=lag_debt`); a host's return restarts it; the fill streak's rule stays | `A_guest_whose_link_hangs_open_again_and_again_...`, `A_host_whose_link_hangs_open_below_the_silence_...` (control: `Brief_hiccups_paid_back_...`); W7i |
| 5 | `OnForeignStory` / `PlayContext` did not see a removed member playing alone (`HubMember.LoneWorld`): its own key reached its own keeper with the host's progress, the context said home | `OnForeignStory` reads `LoneWorld` for a member without a stage group (or in the empty hub one of a reconnect): foreign unless it holds its seat there or the story is its own | `A_guest_removed_while_it_plays_on_alone_is_on_...`, `A_guest_back_without_its_slot_and_removed_...` (control: `A_host_playing_apart_keeps_...`) |

Counter-checks (`build/r021/counter.sh NAME FIX LOG FILTER`: a copy of HEAD's server tree in `build/r021/counter/NAME`
with one fix taken out by `counter_patch.py`, or main's four relay sources; the 13 tests of `new_filter.txt`): on main's
relay 11 of 13 fail, the two controls pass (`logs/counter_main_13.log`); without fix 1 (the whole away rule) the four
reporter tests (`counter_f1_13.log`), without its NETFP part alone (f1b) the catching-up test, without 2 its one, without
3's barrier rule (f3) the gate test, without the renewed grace (f3b) its one, without 4 its two, without 5 its two
(`counter_f1b.log` ... `counter_f5.log`).

**The test proxy** (tests/DW4Online.FakeClient `ImpairmentProxy`): it closed both sides a fixed 600 ms after one side's
end, so W7j's first run (`run_20261116_161608`, on 85f2bdf1) had a's close reach the relay 612 ms after a's BARRIER,
after b's (150 ms later) completed the barrier: the sealed NEW of both, a in it with b, an ordinary drop, 30 s of grace
(debt 40.3 s) and `host_lost`, the path the fix leaves as it was (W7e's). Now it waits for that direction's latest delivery
time (a half-open cut's end included), then 50 ms; `ProxyCutTests` pass.

### Checks

- **Unit tests** 716/716 (`logs/unit_4.log`, f6276ed4; 703 of 0.2.0 unchanged plus the 13 of `Fix021RelayTests`).
- **Integration tests** 155 passed, 3 skipped (the opt-in end-to-end tests; `logs/it_1.log`, 29e95f20, own mysqld on 3750,
  shut down by SQL).
- **The dev client**: `build/r021/dev` (Release, DW4_ONLINE_DEV=ON, main's client code), copied to
  `build/r021/scen/dw4_r021_dev.exe`, md5 b3fa754f.
- **Scenarios, first runs** (`logs/scen_new1.log`, `run_20261116_161608`, relay of 85f2bdf1): W7h passed (c held for b's
  scene at 196.3 s of the relay's clock, a's silence grace at 202.6 s, c's JOIN decided at 211.5 s when the scene ended, a
  back at 248.5 s; no `HoldExpired`), W7i passed (`lag_leave why=lag_debt lag_debt_s=10` at 211.8 s, 11-12 s after the
  first cut; a led on alone, b played apart), W7j failed on the proxy's late close (above).
- **Scenarios, the final code** (`marathon.sh`, `logs/marathon.log`, relay and proxy of f6276ed4, mysqld 3751, ports
  60650-60699, run.py `--parallel 1`): batch `m_new` (`scen_m_new.log`, `run_20261116_164553`) W7h, W7i, W7j passed.
  W7h: c held for b's scene at 197.4 s, a's silence grace at 203.4 s, c's JOIN decided at 212.5 s, `host_back` at 249.3 s.
  W7i: `lag_leave why=lag_debt lag_debt_s=10.1` at 211.6 s. W7j: the barrier-set path (a's BARRIER at 298.09 s, his close
  56 ms later: `host_away why=closed grace_s=60 debt_s=37.7`, b's BARRIER at 298.24 s, b on into stag0102; a back at
  338.1 s, `join_decided` of b's stage group, one instance, `host_left` at the scripts' end).
- **Scenarios, the rest of the marathon** (same relay build of f6276ed4, the later commits notes only; exe b3fa754f): every
  batch passed on its first run, no repeat needed. `m_w4` (`run_20261116_170430`): W4, W4b, W4c, W4i passed. `m_w7`
  (`run_20261116_171740`): W7, W7b, W7c, W7d, W7e, W7f, W7g passed. W7f twice more (`m_w7f_2` `run_20261116_175108`,
  `m_w7f_3` `run_20261116_175647`) and W7g twice more (`m_w7g_2` `run_20261116_180226`, `m_w7g_3`
  `run_20261116_180736`): passed, so W7f and W7g three times each. No `lag_leave` in any of these runs (the lag debt
  takes nobody out on the ordinary cuts and hiccups); W7g ends as in 0.2.0 (nine `host_away`, eight `host_back`,
  `host_lost` inside the closing cut). The PC restarted on 17.11 after the marathon's end; no process of the lane
  survived, every mysqld of the runs logged its shutdown.

### Left

- The server's version (`Directory.Build.props`) is still 0.2.0: the release's integration raises it to 0.2.1.
- No real-client scenario for findings 2 and 5. 2: back from a drop, a host is away for the reporter while his engine
  catches up (its NETFP behind), so his catch-up STATEs are ignored, not stale; the newer-by-tick rule is the unit test's.
  5: an unmodified client keeps sending the host's owner key until it is home (W10c), so its STORY was refused before too;
  the own key needs a modified client, the context an achievement report in that window; unit tests only.
- A JOIN's checkpoint donor (`AskCheckpoint`) is still the connected member longest, also one whose link hangs open or
  who catches up: the join waits `Instances:CheckpointWaitMs` (1 s) and takes the classic state (W7h's c). The reporter's
  away rule would fit there too.
- Seen in the code, not tested: a host whose link hangs open exactly at a stage load (after his BARRIER, before his
  SNAPHASH) holds the new sealed stage group's start for up to `Instances:SnapHashSeconds` (60 s): before sealing starts
  his slot does not fill, so neither the silence grace nor the lag rule sees him.
- W7j takes the barrier-set path while the proxy passes a's close on before b's BARRIER (b's uplink 150 ms slower); in the
  other order the renewed grace applies (its expectation takes either).

### How to run

- `build/r021/env.sh` (paths); `bld.cmd DIR TESTING [dev|nodev]` (the dev client: `bld.cmd dev OFF dev --target dw4_game`,
  copied to `build/r021/scen/dw4_r021_dev.exe`; the client code is main's); `utest.sh LOG [filter]` (unit tests at idle
  priority; `new_filter.txt` holds the fix-021 tests); `itest.sh LOG [filter]` (integration tests, own mysqld on 3750,
  data `build/r021/it-mysql`, test ports 60600-60649, SQL SHUTDOWN after); `scen.sh LOG SCENARIO... [--no-build]`
  (run.py through `wrap/run_wrap.py`, mysqld 3751, ports 60650-60699); `marathon.sh [BATCH...]` (batches `m_new`, `m_w4`,
  `m_w7`, `m_w7f_2`, `m_w7f_3`, `m_w7g_2`, `m_w7g_3`; a failed batch's scenarios once more); `counter.sh` (above).

## Client hardening and release packaging (branch `fix-021-client`, from main d230037f)

Worktree `DW4_PC-wt/r021client`; scripts, builds and logs in `build/r021/` (gitignored, "How to run" below).

| # | What | Where |
| --- | --- | --- |
| 1 | Barrier silence: the five seconds a solo group waits at a barrier without the relay's word (3.4) count only `barrier_pass_limit` (0.5 s) of each long pass of the own machine. W4b's failed run of 0.2.0 was a machine-wide stall (6.3 s): the relay on the same machine stalled too, and the client's first poll after it found no hold yet. A stall of the client alone never mattered (the relay's holds wait in the socket and are read before the check) | `session.cpp` `Session::poll`; unit test `barrier_stall` (with its counter-proof: limit 1000 = before 0.2.1); hooks `DW4_ONLINE_TEST_BARRIER_STALL` / `DW4_ONLINE_TEST_BARRIER_LIMIT` (development builds) and scenarios W4bs / W4bsx |
| 2 | F1 > Settings > Language: a pack without its `game.tsv` says "(pictures only)" | `game_main.cpp`, L10N.md, PLAYER_GUIDE |
| 3 | `--help` names the running program (`DW4_PC` in the release, `dw4_game` in a build folder) and lists every option the parser takes; the error lines use the same name | `game_main.cpp` `print_help` |
| 4 | The release game (DEV=OFF, `DW4_GAME_CONSOLE` OFF) is a Windows program (PE subsystem 2): no console window on a double click. Its lines go to the starter's pipe or file (test runs), else to the console it was started from (cmd, PowerShell: `AttachConsole`), else to `saves/game.log` (the start before: `saves/game.previous.log`). Development builds keep the console | `program_output.cpp`, CMakeLists.txt, README, PLAYER_GUIDE |
| 5 | `cmake --install` produces exactly the release zip's layout; `scripts/package_windows.ps1` builds, installs, checks and zips | CMakeLists.txt, DEVELOPMENT.md "Release packages", L10N.md |
| 6 | README and PLAYER_GUIDE link every doc the zip does not carry absolutely (`https://github.com/Dekirai/DW4_PC/blob/main/...`) | README.md, PLAYER_GUIDE.md |
| 7 | The e-mail texts (the game's Register screen, the website's register and account pages) are true with and without e-mail on the server (the server sends none) | `online_ui.cpp`, `Register.cshtml`, `Account/Index.cshtml` |
| 8 | `tools/online_test/run.py` puts its own folder on `sys.path` (under `python -I` `upload_card` imports `character_test`; no wrapper needed) | `run.py` |

### Checks

Logs in `build/r021/logs/`.

- **Builds** `rel` (Release, DEV=OFF, the options of the main checkout's `build/Game`; `dw4_game.exe` PE subsystem 2,
  `dw4_catalog.exe` 3) and `dev` (DEV=ON, tests; `dw4_game.exe` subsystem 3); 139 compiler warnings in `rel` as in
  0.2.0, none in the changed files.
- **ctest** dev: the first run 76/78 (`online`, `online_env`: the new hook names were on neither list of
  `src/engine/online.cpp`; the first name, `DW4_ONLINE_TEST_STALL`, was round 62's host-loop throttle of L2 and is
  `DW4_ONLINE_TEST_BARRIER_STALL` now); after the fix the online tests 13/13. `dw4_online_session_tests` 590 checks
  (`barrier_stall` with its counter-proof).
- **The Windows program** (`gui_test.py` on a copy as `DW4_PC.exe`): `--help` into a pipe; started by a launcher
  without a console and without handles (as Explorer), `--help` and a hidden `--smoke-test 300` into `saves/game.log`
  (the start before in `game.previous.log`); from a process with a console (one without a window), `--help` into that
  console; an unknown option's stderr line in both. The first try found a real bug: the log was opened twice with
  `freopen_s` (exclusive sharing), stderr was left without a handle and `Engine::log`'s first `std::cerr` line ended
  the game (0xC0000409); stderr now shares stdout's handle (`_dup2`).
- **det.sh** twice on `off/dw4_r021_off.exe` (md5 24baae29): all four runs 4004f056 / audio 396cb219 (`det.log`).
- **Save state suite** 17/17 English (`st_en.log`), 17/17 German (`st_de.log`; "game.tsv 4515 texts, 0 rejected").
- **Server**: unit 703/703 (`unit.log`); the web page tests (`WebTests`, `FriendPageTests`) 10/10 with an own mysqld on
  3755, shut down by SQL (`it_web.log`).
- **The package** (`pkg.cmd 0.2.1`, `pkg.log`, commit abf86e9e, the main checkout's two pins): a fresh Release build in
  `build/r021/pkg`, every content rule held, `DW4_PC-0.2.1-windows-x64.zip` (4.8 MB, 15 files, `sha256sum -c` OK).
  Its file list and order equal the hand-made 0.2.0 zip's (`unzip -Z1` of both). Sizes differ only in the changed
  texts and in line endings: files without an eol rule in `.gitattributes` (LICENSE, `licenses/*-MIT.txt`, the
  language pack) come from the checkout as it is (CRLF here, `core.autocrlf`), the 0.2.0 zip had them from `git show`
  (LF); both read alike. No profile path in any file (also `grep -a` on each), `DW4_PC.exe` subsystem 2 and only
  system DLLs. det.sh on a copy of the packaged `DW4_PC.exe` (md5 c6bf8cb2): 4004f056 / 396cb219 (`det_pkg.log`); the
  unpacked zip's `DW4_PC.exe --smoke-test 1800` exit 0 (`smoke_pkg.log`), `--help` names `DW4_PC`.
- **Scenarios** with the dev client `scen/dw4_r021_dev.exe` (md5 074337af, commit abf86e9e's code; `marathon.log`,
  run.py under `python -I` without a wrapper): W4bs pass (b's 6.3 s stall, 5 holds dropped, b a's guest, 3960 shared
  ticks), W4bsx pass as a counter-proof (b's FAIL at 59 s, "The server did not answer", no arrival toast; the relay's
  `join_ready_timeout` and `fallback_new` as in W4b's failed run of 0.2.0), W1, W4, W10 (`upload_card` imports
  `character_test`) pass, W4b three times pass. W4b's second run had a real machine-wide stall (both clients' longest
  pass 6.5 s, in the lobby before the gate; the user played a game meanwhile) and passed.
- **Final state** at 7b0ded0c (after the scenarios only `session.hpp`'s comment column and these notes changed): `rel`
  and `dev` built again (`dw4_game.exe` md5 72544c60 / 0d532820), ctest dev 78/78 (`ctest_dev_final.log`); the
  package again from the clean checkout: the existing zip refused without `-Force` (`pkg_refuse.log`), then with it
  the same file list, SHA-256 f96745e9... (`pkg_final.log`, `pkg_zip_list.txt`); its `DW4_PC.exe` (md5 437ffb46) det.sh
  4004f056 / 396cb219 (`det_pkg_final.log`) and `gui_test.py` all pass (`gui_test_pkg.log`). A negative run with an
  untracked pack `data/lang/xx` (language.ini and a game.tsv; moved to `build/r021/alt/` afterwards) stopped with
  "lang/xx/language.ini: not part of the release layout" and wrote no zip (`pkg_negative.log`); its game.tsv never
  reached the stage.

### Left

- Merge and push are the user's (not done in this lane). The zip in `build/r021/dist` is the script's test output from
  this branch; the release zip comes from main after the merge (`scripts/package_windows.ps1 -Version 0.2.1`).
- `build/Game` of the main checkout (`Play-DW4-Game.cmd`) becomes a Windows program at its next configure after the
  merge (DEV=OFF): cmd does not wait for it and its lines go to the cmd window only while that is still open, else to
  `saves/game.log`; `-DDW4_GAME_CONSOLE=ON` there keeps the old console.
- The AttachConsole path was checked from a process with a console but no window (no visible window was opened); a
  real cmd window is the user's check.
- Files without an eol rule (LICENSE, `licenses/*-MIT.txt`, `data/lang/*`) ship with the checkout's line endings; a
  `.gitattributes` rule would make the zip independent of `core.autocrlf` (not changed here).

### How to run

- Builds: `build\r021\bld.cmd DIR TESTING [dev|nodev]` (Release, Ninja, the main checkout's FetchContent sources): `rel OFF
  nodev` = the release game, `dev ON dev` = the development game with the tests; `ctest.cmd dev`.
- Server: `utest.sh LOG [filter]`; `itest.sh LOG [filter]` (own mysqld on 3755, data `build/r021/it-mysql`, test ports
  60700-60749, SQL SHUTDOWN afterwards).
- `checks.sh det st_en st_de`: det.sh (the main checkout's) twice on `off/dw4_r021_off.exe` (a copy of `rel`'s game), the
  save state suite (`build/r021/st`, a copy of r020's) on `off/dw4_r021_st.exe` in English and German (`build/r021/lang`:
  `data/lang/de` plus the main checkout's git-ignored `local/lang/de/game.tsv`).
- Scenarios: `scen.sh LOG ARGS...` (run.py under `python -I` directly, mysqld 3756, ports 60750-60799; the client
  `scen/dw4_r021_dev.exe`, a copy of `dev`'s game); `marathon.sh [BATCH...]` (`m_stall` W4bs W4bsx, `m_base` W1 W4 W10,
  `m_w4b_1..3` W4b; a batch whose log ends with EXIT is skipped).
- The Windows program: `gui_test.py FOLDER EXE DATA` (with `no_console_child.py`, `attach_child.py`; no window, no
  focus), `pe_subsystem.py EXE...`.
- The package: `scripts/package_windows.ps1` with `-BuildDirectory build\r021\pkg -OutputDirectory build\r021\dist`, the
  main checkout's pins file and `-CMakeArgs` with the FetchContent source folders (`pkg.cmd`).

## The integration (branch `int-021`, 2026-11-17)

Worktree `DW4_PC-wt/int021`, branch `int-021` from main d230037f. Main moved after the two lane merges (the Android UX
line `feat-android-cux`, merged into main as a304d6c9 after the user's Thor check), so main was merged in before the
check pass (step c). Scripts, builds and logs in `build/r021/` (gitignored; "How to run" below). No code changed in
this stage beyond the merges' conflict resolution (notes and harness logs only); not pushed, no tag.

| Step | What | Commit | Conflicts and their resolution |
| --- | --- | --- | --- |
| a | merge `fix-021-relay` (85040ddf) `--no-ff` | 558689ae | none |
| b | merge `fix-021-client` (9ba0fff6) `--no-ff` | f0297de4 | `docs/ROUND_021_NOTES.md` (both lanes added it): both kept, each lane's text as written in its own section (the relay lane's headings one level down), one common intro. SEAMLESS (3.4 and 19.24's W4bs row of the client lane, 6.4, 6.9, 19.6.6, 19.6.7, 19.24, 19.32 of the relay lane) and PLAYER_GUIDE merged by git with both sides' text (checked against each branch) |
| c | merge main a304d6c9 `--no-ff` (the Android port and its console F1, the focus pause on the PC too with default off, the LAN hello's platform tag, Android's online / LAN / cloud refusals, docs) | 7400b578 | `CMakeLists.txt`: the client lane's release layout (install rules) with main's "no DW4_PC on Android" (`if(DW4_BUILD_APP AND NOT ANDROID)`). `game_main.cpp` `main()`: Android's lines to logcat (main), the PC's to the pipe / console / `saves/game.log` (the lane's `route_program_output`, now the `#else` branch: a no-op off Windows anyway), `program_name` for both. `game_main.cpp` `--help`: the lane's `print_help()` with main's new options added (`--mute`, `--env`, `--menu-script`, `--import-disc`), so it still lists every option the parser takes. Merged by git with both sides: `online.cpp`'s test env list (the barrier hooks and main's entries), PLAYER_GUIDE (the lane's absolute links, main's focus pause / menu size / second screen / Thor text), DEVELOPMENT (the lane's "Release packages", main's `cmake` / `android` rows), `online_ui.cpp`. README and SEAMLESS: main did not touch them |

The check pass on 7400b578: below, as it runs. An earlier start of it on f0297de4 (before main moved) was stopped;
its logs are in `build/r021/alt/logs_f0297de4_stopped`.

### Checks of the integration

Logs in `build/r021/logs/`. Code of 7400b578 (the later commits notes only).

- **Builds** (Release, Ninja, the FetchContent sources of the main checkout): `rel` = the release game (DEV=OFF,
  BUILD_TESTING OFF; `bld_rel.log`, 139 compiler warnings as in 0.2.0, `dw4_game.exe` md5 5f0cc9b8, PE subsystem 2);
  `dev` (DEV=ON, tests; `bld_dev.log`, md5 a44e81f9, subsystem 3); `plain` (DEV=OFF with tests; `bld_plain.log`).
- **ctest** 81/81 in `dev` (`ctest_dev.log`) and 81/81 in `plain` (`ctest_plain.log`).
- **det.sh** (the main checkout's) twice on `off/dw4_int021_off.exe` (a copy of `rel`'s game): all four runs 4004f056 /
  audio 396cb219 (`det.log`).
- **Save state suite** (`build/r021/st`) on `off/dw4_int021_st.exe`: 17/17 English (`st_en.log`), 17/17 German
  (`st_de.log`; every case "game.tsv 4515 texts, 0 rejected").
- **The PC's F1 against main a304d6c9** (`f1cmp.sh`; main's release game built from a `git archive` of a304d6c9 in
  `build/r021/mainref`, md5 cab4799e; CONSOLE_MENU.md's P0 set: the 8 tabs plus Settings at Graphics and at Second
  screen, 100 % 1280x960 with Xbox prompts and 231 % 1920x1080 with the Thor's, `DW4_MENU_STILL`, frame 240, every exe
  as `dw4_game.exe` in the same folder beside the committed `lang/de`): 20 of 20 pictures bytewise identical. With the
  German pack chosen (`f1lang.sh`, `DW4_LANGUAGE=de`, no game.tsv) the Settings tab differs only in the Language box's
  label: main "Deutsch (Fan-Übersetzung) (pictures only, no game.tsv)", int-021 "... (pictures only)" (100 %: 113
  pixels in x 353-412, y 115-126; 231 %: 1451 pixels in x 916-1130, y 138-166).
- **The console menu's scripts** (`menu.sh`: `tools/menu/run.sh d` on a copy of `rel`'s game beside the committed
  `lang/de`): d1 to d8 without a failed expect (`menu_d.log`).
- **Server**: the solution builds with 0 warnings (`sln_build.log`), unit 716/716 (`unit.log`), integration 155
  passed, 3 skipped (the opt-in end-to-end tests; `it.log`; own mysqld on 3750, shut down by SQL).
- **Publish dry run** (`pub.sh`: `publish.ps1` from a clean `git archive` of HEAD in `build/r021/pub/src`): unit
  716/716 in Release, `DW4Online-0.2.0.zip` (50.6 MB, 374 files, the same file names as 0.2.0's), every assembly
  0.2.0+ecf97f25 (the version bump is the ship stage's), `WorldProtocol.Version` 50, the 14 embedded migrations up to
  0014_reports, no appsettings.Production / Development / Local.json, no key or certificate file
  (`publish.log`, `publish_inspect.log`, `publish_zip_list.txt`).
- **Package dry run** (`pkg.sh pkg 0.2.1`: `scripts/package_windows.ps1` through `pkg.cmd`, a fresh build in
  `build/r021/pkg`, the main checkout's pins file): every content rule held, `DW4_PC-0.2.1-windows-x64.zip` (5.0 MB,
  15 files), its file list and order equal the 0.2.0 zip's; no game.tsv, no PDB, no Android file (no `.so`, APK,
  Gradle, console menu or tools/menu file), no profile path; `DW4_PC.exe` subsystem 2, system DLLs only;
  `sha256sum -c` OK (`pkg.log`, `pkg_zip_list.txt`). The unpacked `DW4_PC.exe` (md5 fa11d8a1): det.sh 4004f056 /
  396cb219 (`det_pkg.log`), `--help` names `DW4_PC` and lists main's new options (`help_pkg.log`), a hidden
  `--smoke-test 1800 --mute` exit 0 (`smoke_pkg.log`).
- **Scenarios**: the marathon (below).

**The marathon** (`marathon.sh`, `logs/marathon.log`, run.py under `python -I` without a wrapper, `--parallel 1`,
BelowNormal; mysqld 3751, ports 60700-60799; the client `scen/dw4_int021_dev.exe`, a copy of `dev`'s game, md5
a44e81f9; W22's protocol 48 client `old48/dw4_old48_dev.exe`, md5 e527c2cf; resumable: a batch whose log ends with
EXIT is skipped, a batch's failures run once more in `scen_BATCH_rep.log`). Progress (18:35):

- Passed on their first run: `m_smoke` (W1 W4c W10 W31 S22b RGJ1), `m_new021` (W4bs W4bsx W7h W7i W7j), `ui home`,
  `ui goalong`, `m_w4` (W4 W4b W4d-W4i), `m_w7` (W7 W7b-W7g), `m_prompt` (W2 W2b W11 W11b W11r W30c), `m_drops`
  (S11a S11b), `m_ui` (the ten ui runs), `m_go` (W31b-W31i); in `m_mod` W10b W10c W10d W10s W5.
- **W5s failed once** (`scen_m_mod.log`, `run_20261117_143735`), every expectation held, the relay logged one
  `statehash_mismatch` and `checkpoint_suspect` (why=statehash_rebuilt): c (the moderator) rebuilt from a's checkpoint
  of tick 4179 (16 ticks replayed, READY at 4193 with its check fingerprint equal), joined at 4198, and its STATEHASH of
  4200, two ticks after the join, differed from a's (5817b4ee... / 89453ef7...); every NETFP equal (21 from 4200 to
  5400), the STATEHASHes of 4800 and 5400 equal again. The repeat passed (`scen_m_mod_rep.log`, `run_20261117_150742`:
  c joined at 4203, its first STATEHASH at 4800). No machine stall in the logs. This is the rebuild gap SEAMLESS 19.32
  notes for fix-d43's W7f run `135002` (a STATEHASH after a checkpoint rebuild differs for a while, every NETFP equal):
  a client matter, not from 0.2.1 (neither lane touched the checkpoint or the tick state). Places:
  `src/game/online/session.cpp:2616` (the client's `engine_.tick_state_hash()` at the first period tick after the
  join) and `server/DW4Online/src/DW4Online.Relay/Hub/RoomHub.cs:2381-2383` (the relay's compare and `MarkSuspect`).
- **S14p failed twice** (`scen_m_reg_1.log`, `run_20261117_201312`; `scen_m_reg_1_rep.log`, `run_20261117_204548`),
  both times only on run.py's `story_saved` precondition "the owner's online_story had flag 505 before g set it ((2,
  1))"; every NETFP equal (34 / 38 compared), g's STATEHASH-free run otherwise as expected. Analysis: g logs "505 set"
  (its script step, tick 3397 / 3502), then sends its STORY in the same pass (`online-trace: story ... rev=1`, then
  `story_ack rev=2`); the relay merged it into o's keeper and the server saved revision 2 at once (the relay's `story`
  event account 1 revision 2, at the tick of the set, not earlier: no foreign story reached o's row). run.py's watcher
  (`tools/online_test/run.py:870-879`) reads g's log only every 0.1 s and reads the database's "before" after it saw
  the line; when the save is quicker than that, the precondition (`run.py:1324-1326`) fails although the flag reached
  the owner's row in well under the ten seconds. The same harness race made S14p pass only on its second run in round
  I1b (SEAMLESS 19.32, "a story save faster than the harness's log poll"). Here it was the machine: from about 20:00 the
  user played a game at normal priority (about ten cores busy beside the BelowNormal marathon; also other workflows'
  idle-priority processes), g's longest pass 245 ms (first run) and 859 ms (repeat), g's slot `late` (input delay up to
  5), o's executions 67 ms on average in the repeat. No product fault; a harness race (the check would need the
  database's save time instead of the log poll's).
- `m_i1a_1..3` (W1x W3 W4bx W5f W6 W8 W9, W12 W18 W19 W22 W24 W25 W26, W27 W28 W29 W30 W30b W30x W32) passed;
  `m_reg_1` S1 S2 S3 S5 S14z S16 passed; `m_reg_2` S17a S17b S18b S19a S19b S19c passed.
- **S18 failed once on the busy machine** (`scen_m_reg_2.log`, `run_20261117_204921`): every NETFP equal (188), b's
  script step "mat matz02 no until answered>=1,asking<=0 ... frames>=400" (`S18/b.script` line 20) ran out of its
  400 frames before a's exit prompt asked b (asking=1 came 663 frames after the step began); the clients' longest
  passes 2.9-3.8 s, executions up to 4.0 s, input delay up to 15. Its repeat waits for a quiet machine (below).
- **The load gate**: with the user's game running, repeats would have failed on the same stalls, so the marathon was
  stopped after `m_reg_2` (21:25; its repeat deferred) and goes on as `marathon2.sh`: the same batches (the finished
  ones skipped), before every batch and every repeat `gate.ps1` waits until the processes at normal priority or above
  use less than 3 cores for 45 s (each wait in `marathon.log`), and at the end `m_s14p_q`: S14p once more on a quiet
  machine. Another workflow's normal-priority runs kept the machine above 3 cores, so the gate's threshold became 7 of
  the 16 cores (`gatekick.sh`, 22:22).
- `m_reg_3` (S21 S22a J1 M1 A1) passed. Two runs never started a scenario: S18's repeat (22:22) and `m_extra_1`'s first
  run (22:53) ended with "mysqld did not open port 3751" (run.py's 40 s start window, `run.py:285-286`; both
  `mysqld.err` stop after "Completed initialization of buffer pool" while Dolphin, MMXCM_PC and a game used 6-8 cores at
  normal priority); no scenario result. `m_extra_1`'s repeat (the whole batch) passed J5 JB JR JS JS2 L1; in L1s the
  session ended (17.11 23:21: the clients' exit 1073807364 = 0x40010004, the session end's termination; run.py was cut
  before its summary).
- **Resume (18.11, after the reboot of 09:15)**: HEAD 6274a65f, main still a304d6c9. No process of this stage was left;
  the scenario mysqld's data (`tools/online_test/.work/mysql-data`, 3751) had a stale pid file (moved to
  `build/r021/alt/stale_20261118`) and no clean shutdown: started once on its own for InnoDB's crash recovery and shut
  down by SQL (`logs/mysqld_recover_20261118.*`: "Shutdown complete"); `it-mysql` (3750) had shut down cleanly. The
  rest runs as `marathon3.sh` (`scen3.sh`: test ports 60600-60699, since the user's IDA MCP servers now listen on
  60734-60736; a run that ends before any scenario because mysqld did not open its port is kept as
  `scen_BATCH_infraN.log` and runs again after the gate, at most twice): `m_extra_1b` (L1s), `m_reg_2_s18` (S18's
  repeat), `m_extra_2`, `m_fake`, W7f and W7g three more times each, `m_s14p_q`.
